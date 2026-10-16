"""Host utility for the public SD /xploader.cfg record; no ROM registration."""
import argparse,binascii,json,struct
from pathlib import Path
MAGIC=b'XPLCFG01'
def encode(mode,target,directory,sequence,language=''):
    path=directory.encode('utf-8')
    if mode not in (0,1) or target not in (0,1,2) or not 0<sequence<2**64 or not path.startswith(b'/') or len(path)>255 or b'\x00' in path or any(c<32 or c==127 for c in path) or '\\' in directory or any(p in ('.','..') for p in directory.split('/')):raise ValueError('Invalid boot settings')
    result=bytearray(512);result[:8]=MAGIC
    struct.pack_into('<IIQII',result,8,1,512,sequence,mode,target)
    struct.pack_into('<Q',result,40,sequence^((1<<64)-1));result[64:64+len(path)]=path
    if language:
        if len(language)>15 or not language[0].isalpha() or any(not(c.isascii() and (c.isalpha() or c.isdigit() or c=='-')) for c in language):raise ValueError('Invalid locale')
        struct.pack_into('<II',result,32,0x474e414c,1);raw=language.encode('ascii');result[320:320+len(raw)]=raw
    struct.pack_into('<I',result,60,binascii.crc32(result)&0xffffffff)
    return bytes(result)
def decode(record):
    if len(record)!=512 or record[:8]!=MAGIC:return None
    version,size,sequence,mode,target=struct.unpack_from('<IIQII',record,8)
    expected=struct.unpack_from('<I',record,60)[0];copy=bytearray(record);copy[60:64]=bytes(4)
    if version!=1 or size!=512 or binascii.crc32(copy)&0xffffffff!=expected or struct.unpack_from('<Q',record,40)[0]!=(sequence^((1<<64)-1)):return None
    try:
        directory=record[64:320].split(b'\0',1)[0].decode('utf-8');encode(mode,target,directory,sequence)
    except (ValueError,UnicodeError):return None
    result=dict(sequence=sequence,startup=mode,target=target,directory=directory)
    if struct.unpack_from('<II',record,32)==(0x474e414c,1):
        try:
            part=record[320:336]
            if b'\0' not in part:raise ValueError('Locale not terminated')
            language=part.split(b'\0',1)[0].decode('ascii');encode(mode,target,directory,sequence,language)
            if language:result['language']=language
        except (ValueError,UnicodeError):pass
    return result
def choose(blob):
    if len(blob)!=1024:raise ValueError('Exactly1024 bytes required')
    candidates=[(decode(blob[i*512:(i+1)*512]),i) for i in range(2)]
    candidates=[(record,slot) for record,slot in candidates if record is not None]
    if not candidates:return None
    candidates.sort(key=lambda item:item[0]['sequence'],reverse=True)
    if len(candidates)==2 and candidates[0][0]['sequence']==candidates[1][0]['sequence'] and blob[:512]!=blob[512:]:raise ValueError('Ambiguous generation')
    return candidates[0]
def main():
    p=argparse.ArgumentParser();p.add_argument('action',choices=['create','inspect']);p.add_argument('path',type=Path)
    p.add_argument('--mode',choices=['always','quick'],default='always');p.add_argument('--target',choices=['emmc','sd','twrp'],default='emmc')
    p.add_argument('--directory',default='/lineageos17');p.add_argument('--language',choices=['','en','zh-TW'],default='');a=p.parse_args()
    if a.action=='create':
        blob=encode(int(a.mode=='quick'),['emmc','sd','twrp'].index(a.target),a.directory,1,a.language)+bytes(512)
        with a.path.open('xb') as stream:stream.write(blob)
    print(json.dumps(choose(a.path.read_bytes()),ensure_ascii=False))
if __name__=='__main__':main()
