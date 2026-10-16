"""Host-only guard004 recovery image builder. Never writes/reboots a phone.

Download the original TWRP from its publisher, retain its userspace, replace
the kernel and set the source SD mount read-only. SPDX-License-Identifier: MIT
"""
import argparse,gzip,hashlib,json,struct,urllib.request
from pathlib import Path
from xpl_image import BootImage,CpioEntry,inflate_gzip,pack_newc,parse_newc,align
ROOT=Path(__file__).resolve().parents[1]
def digest(data):return hashlib.sha256(data).hexdigest()
def main():
    p=argparse.ArgumentParser();p.add_argument('--stock',type=Path);p.add_argument('--kernel',type=Path,default=ROOT/'Image-guard004.gz');p.add_argument('--output',type=Path,default=ROOT/'twrp.img');a=p.parse_args()
    meta=json.loads((ROOT/'recovery/guard004/manifest.json').read_text(encoding='utf-8'))
    compressed=a.kernel.read_bytes()
    if digest(compressed)!=meta['kernel_gzip_sha256']:raise ValueError('Pinned guard004 compressed kernel required')
    raw,_=inflate_gzip(compressed,64*1024*1024,False)
    if digest(raw)!=meta['kernel_raw_sha256']:raise ValueError('Guard004 kernel decode differs')
    if a.stock:original=a.stock.read_bytes()
    else:
        print('Downloading original TWRP from its publisher...')
        with urllib.request.urlopen(meta['original_twrp_url'],timeout=60) as response:original=response.read(64*1024*1024+1)
    if digest(original)!=meta['original_twrp_sha256']:raise ValueError('Original TWRP version/checksum differs')
    boot=BootImage(original);_,dtbs=inflate_gzip(boot.kernel,64*1024*1024,True)
    entries=boot.entries();matches=[e for e in entries if e.name=='etc/recovery.fstab']
    if len(matches)!=1:raise ValueError('Expected original recovery fstab')
    replacement=(ROOT/'recovery/guard004/recovery.fstab').read_bytes()
    before=matches[0].data.decode();lines=before.splitlines(True);changed=0
    for i,line in enumerate(lines):
        if line.startswith('/external_sd'):
            if 'wipeingui;' not in line:raise ValueError('Source mount flags differ')
            lines[i]=line.replace('wipeingui;','').rstrip('\n')+'fsflags=ro;\n';changed+=1
    if changed!=1 or ''.join(lines).encode()!=replacement:raise ValueError('Pinned read-only fstab change differs')
    updated=[CpioEntry(e.name,list(e.fields),replacement if e.name=='etc/recovery.fstab' else e.data) for e in entries]
    ramdisk=gzip.compress(pack_newc(updated),compresslevel=9,mtime=0);kernel=compressed+dtbs
    header=bytearray(original[:boot.page]);struct.pack_into('<I',header,8,len(kernel));struct.pack_into('<I',header,16,len(ramdisk))
    ident=hashlib.sha1()
    for part in (kernel,ramdisk,b''):ident.update(part);ident.update(struct.pack('<I',len(part)))
    header[576:608]=ident.digest()+bytes(12)
    result=bytes(header)+kernel+bytes(align(len(kernel),boot.page)-len(kernel))+ramdisk+bytes(align(len(ramdisk),boot.page)-len(ramdisk))
    check=BootImage(result);seen,tail=inflate_gzip(check.kernel,64*1024*1024,True)
    if seen!=raw or tail!=dtbs or check.cmdline!=boot.cmdline or len(result)>33554432:raise ValueError('Output geometry/content differs')
    back=check.entries()
    if len(back)!=len(entries):raise ValueError('Output userspace entry count differs')
    for old,new in zip(entries,back):
        if old.name!=new.name or (new.data!=(replacement if old.name=='etc/recovery.fstab' else old.data)):raise ValueError('Unexpected userspace change')
    with a.output.open('xb') as f:f.write(result)
    report=dict(image=a.output.name,bytes=len(result),sha256=digest(result),kernel_raw_sha256=digest(raw),source_stock_sha256=digest(original),userspace_change='Only source SD read-only fstab; every other entry data preserved',no_phone_written=True,layout_required='E6653 tested split29/44; not stock layout',compressed_bytes_may_vary_with_zlib=True)
    with a.output.with_suffix('.json').open('x',encoding='utf-8') as f:json.dump(report,f,indent=2);f.write('\n')
    print(json.dumps(report,indent=2));print('Copy the resulting twrp.img to the SD root. Keep a stock recovery copy. The protected SD mount is read-only.')
if __name__=='__main__':main()
