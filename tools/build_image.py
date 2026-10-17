"""Build the native-only E6653 carrier. Pure host format code; no flashing."""
import argparse, gzip, hashlib, json, struct, zlib
from pathlib import Path
from xpl_image import BootImage, CpioEntry, align, exclusive_write, inflate_gzip, pack_newc, parse_newc
RESERVE = 327680
PROPERTY = b'xploader,menu-proof'

def carrier_gzip(data, compresslevel=9, mtime=0):
    # Preserve the accepted carrier's historical OS byte across Python 3.12/
    # 3.13 and Windows/Linux hosts. The deflate stream and CRC are unchanged.
    result = bytearray(gzip.compress(data, compresslevel=compresslevel, mtime=mtime))
    result[9] = 10
    return bytes(result)
def inspect_fdt(blob):
    blob=bytes(blob)
    if len(blob)<40:
        raise ValueError("short FDT header")
    h=list(struct.unpack_from(">10I",blob))
    magic,total,so,no,ro,version,last,bootcpu,ns,ss=h
    if magic!=0xd00dfeed or total!=len(blob) or version!=17 or last>17:
        raise ValueError("unsupported FDT header")
    if not (40<=ro<so<=no<=total and so%4==0 and ro%8==0 and so+ss==no and no+ns<=total):
        raise ValueError("unsupported FDT layout")
    r=ro
    while r+16<=so:
        pair=struct.unpack_from(">QQ",blob,r);r+=16
        if pair==(0,0):break
    else:
        raise ValueError("unterminated reserve map")
    if any(blob[no+ns:]):
        raise ValueError("nonzero FDT padding")
    stack=[];props=[];begins={};ends={};pos=so
    while pos+4<=no:
        token=struct.unpack_from(">I",blob,pos)[0]; token_pos=pos;pos+=4
        if token==1:
            stop=blob.find(b"\0",pos,no)
            if stop<0 or stop-pos>255 or len(stack)>=64:
                raise ValueError("invalid node name")
            name=blob[pos:stop]
            if not stack and (name or token_pos!=so):
                raise ValueError("invalid root")
            stack.append(name);pos=align(stop+1,4)
            path=tuple(stack)
            if path in begins:raise ValueError("duplicate node")
            begins[path]=pos
        elif token==2:
            if not stack:raise ValueError("unbalanced node")
            ends[tuple(stack)]=token_pos;stack.pop()
        elif token==3:
            if not stack or pos+8>no:raise ValueError("invalid property")
            size,name_off=struct.unpack_from(">II",blob,pos);pos+=8
            if name_off>=ns or pos+size>no:raise ValueError("invalid property bound")
            stop=blob.find(b"\0",no+name_off,no+ns)
            if stop<0:raise ValueError("unterminated property name")
            name=blob[no+name_off:stop]
            props.append((tuple(stack),name,blob[pos:pos+size],pos))
            pos=align(pos+size,4)
        elif token==4:pass
        elif token==9:
            if stack or pos!=no:raise ValueError("invalid FDT end")
            return {"header":h,"props":props,"begins":begins,"ends":ends,
                    "reserve":blob[ro:r]}
        else:raise ValueError("unknown FDT token")
    raise ValueError("missing FDT end")

def add_proof(blob):
    tree=inspect_fdt(blob); h=tree['header']
    if any(p[1]==PROPERTY for p in tree['props']):raise ValueError('existing menu proof')
    so,no,ns=h[2],h[3],h[8]
    prop=struct.pack('>III',3,4096,ns)+bytes(4096)
    chosen=(b'',b'chosen')
    if chosen in tree['begins']:
        at=tree['begins'][chosen];insert=prop
    else:
        at=tree['ends'][(b'',)]
        insert=struct.pack('>I',1)+b'chosen\0\0'+prop+struct.pack('>I',2)
    structure=blob[so:at]+insert+blob[at:no]
    strings=blob[no:no+ns]+PROPERTY+b'\0'
    out=bytearray(blob[:so]+structure+strings)
    out+=bytes(align(len(out),8)-len(out))
    h[1]=len(out);h[3]=so+len(structure);h[8]=len(strings);h[9]=len(structure)
    struct.pack_into('>10I',out,0,*h)
    new=inspect_fdt(out)
    if [p[:3] for p in tree['props']] != [p[:3] for p in new['props'] if p[1]!=PROPERTY]:
        raise ValueError('original DT semantics changed')
    if tree['reserve']!=new['reserve']:raise ValueError('reservation changed')
    return bytes(out)

STUB = 24 * 1024 * 1024
RAW_BYTES = STUB + RESERVE
FALLBACK = 0x1000
ABORT_RECORD = (0x800, 64)
PAGE = 4096
GEOMETRY = (32768, 33554432, 15728640, 31457280)
CHOSEN = (b"", b"chosen")
WDT_PROPERTY = b"xploader,entry-watchdog"
CARRIER_PROPERTY = b"xploader,carrier"
OWN_PROPERTIES = {b"xploader,menu-proof", WDT_PROPERTY, CARRIER_PROPERTY}
CMDLINE = (b"androidboot.hardware=qcom user_debug=31 lpm_levels.sleep_disabled=1 "
           b"boot_cpus=0-5 loop.max_part=7 coherent_pool=2M swiotlb=2048")
BOARD_HASHES = (
    "c5fa5e438d82c1c67a9be22dfa0f4d3173fb780f327c82ea6aadd4c5a7550d13",
    "ad857d55d9dc26b938de1836b59d8107230d2b44c5b8a16348eb08dd9f9c4d4e",
)
MODES = {"normal": 20, "fallback20": 20, "fallback28": 28}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def branch(source, target):
    delta = target - source
    if delta % 4 or not -(1 << 27) <= delta < (1 << 27):
        raise ValueError("ARM64 branch range/alignment")
    return 0x14000000 | ((delta // 4) & 0x3ffffff)


def marker_initrd(label):
    text = ("xploader owned native carrier\n" + label +
            "\nNo Linux kernel or private recovery ramdisk is bundled.\n").encode("ascii")
    directory = [1, 0o40755, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0]
    regular = [2, 0o100644, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0]
    raw = pack_newc([CpioEntry("xploader", directory, b""),
                     CpioEntry("xploader/carrier.txt", regular, text)])
    return carrier_gzip(raw, compresslevel=9, mtime=0)


def add_properties(board, label):
    if any(p[1] in OWN_PROPERTIES for p in inspect_fdt(board)["props"]):
        raise ValueError("Board already contains an owned carrier property")
    original = inspect_fdt(board)
    marked = add_proof(board)
    tree = inspect_fdt(marked)
    h = tree["header"]
    so, no, ns = h[2], h[3], h[8]
    at = tree["begins"].get(CHOSEN)
    if at is None:
        raise ValueError("Board /chosen is required")
    strings = bytearray(marked[no:no + ns])
    insert = bytearray()
    for name, value in [(WDT_PROPERTY, bytes(64)), (CARRIER_PROPERTY, label.encode("ascii") + b"\0")]:
        offset = len(strings)
        strings += name + b"\0"
        insert += struct.pack(">III", 3, len(value), offset) + value
        insert += bytes(align(len(insert), 4) - len(insert))
    structure = marked[so:at] + insert + marked[at:no]
    out = bytearray(marked[:so] + structure + strings)
    out += bytes(align(len(out), 8) - len(out))
    h[1], h[3], h[8], h[9] = len(out), so + len(structure), len(strings), len(structure)
    struct.pack_into(">10I", out, 0, *h)
    updated = inspect_fdt(out)
    if [p[:3] for p in original["props"]] != [p[:3] for p in updated["props"] if p[1] not in OWN_PROPERTIES]:
        raise ValueError("Original board properties changed")
    if original["reserve"] != updated["reserve"] or original["begins"].keys() != updated["begins"].keys():
        raise ValueError("Original board nodes/reservations changed")
    return bytes(out)


def raw_image(native, fallback, mode):
    if mode not in MODES or not native or len(native) > RESERVE or not 0 < len(fallback) <= 0x1000:
        raise ValueError("Invalid bounded carrier inputs")
    raw = bytearray(RAW_BYTES)
    struct.pack_into("<IIQQQQQQII", raw, 0, branch(0, 64), 0xd503201f,
                     0x80000, 0, 0, 0, 0, 0, 0x644d5241, 0)
    struct.pack_into("<II", raw, 64, branch(64, STUB if mode == "normal" else FALLBACK), branch(68, FALLBACK))
    raw[FALLBACK:FALLBACK + len(fallback)] = fallback
    raw[STUB:STUB + len(native)] = native
    return bytes(raw)


def boot_header(kernel, ramdisk):
    header = bytearray(PAGE)
    header[:8] = b"ANDROID!"
    ka, ra, sa, ta = GEOMETRY
    struct.pack_into("<10I", header, 8, len(kernel), ka, len(ramdisk), ra, 0, sa, ta, PAGE, 0, 0)
    header[48:56] = b"xploader"
    header[64:64 + len(CMDLINE)] = CMDLINE
    identity = hashlib.sha1()
    for part in (kernel, ramdisk, b""):
        identity.update(part)
        identity.update(struct.pack("<I", len(part)))
    header[576:608] = identity.digest() + bytes(12)
    return bytes(header)


def build(native, fallback, boards, label, mode="normal"):
    if len(boards) != 2 or tuple(map(sha, boards)) != BOARD_HASHES:
        raise ValueError("Exact attributed E6653 board pair required")
    kernel = carrier_gzip(raw_image(native, fallback, mode), compresslevel=9, mtime=0)
    kernel += b"".join(add_properties(board, label) for board in boards)
    ramdisk = marker_initrd(label)
    return (boot_header(kernel, ramdisk) + kernel + bytes(align(len(kernel), PAGE) - len(kernel)) +
            ramdisk + bytes(align(len(ramdisk), PAGE) - len(ramdisk)))


def preflight(blob, native, fallback, boards, label, mode="normal"):
    if mode not in MODES or len(blob) > 33554432:
        raise ValueError("Bounded carrier mode/partition required")
    boot = BootImage(blob)
    # This rejects clean001's empty initrd before any native-path assumptions.
    if not boot.ramdisk:
        raise ValueError("Nonempty owned initrd required before display/key setup")
    if boot.second or boot.tail or boot.page != PAGE:
        raise ValueError("Unexpected component, tail or page geometry")
    if tuple(getattr(boot, k) for k in ("kernel_addr", "ramdisk_addr", "second_addr", "tags_addr")) != GEOMETRY:
        raise ValueError("Measured stockTWRP address geometry required")
    if blob[:PAGE] != boot_header(boot.kernel, boot.ramdisk):
        raise ValueError("Owned header/identifier/cmdline changed")
    for start, part in zip(boot.offsets, (boot.kernel, boot.ramdisk, boot.second)):
        if any(blob[start + len(part):align(start + len(part), PAGE)]):
            raise ValueError("Nonzero component padding")
    try:
        initrd, _ = inflate_gzip(boot.ramdisk, limit=4096)
    except zlib.error as error:
        raise ValueError("Invalid owned initrd gzip/CRC") from error
    expected_initrd, _ = inflate_gzip(marker_initrd(label), limit=4096)
    if initrd != expected_initrd:
        raise ValueError("Only the owned newc marker is permitted")
    entries = parse_newc(initrd)
    if [e.name for e in entries] != ["xploader", "xploader/carrier.txt"]:
        raise ValueError("Unexpected initrd member")
    try:
        raw, tail = inflate_gzip(boot.kernel, limit=RAW_BYTES, allow_tail=True)
    except zlib.error as error:
        raise ValueError("Invalid native Image gzip/CRC") from error
    if raw != raw_image(native, fallback, mode):
        raise ValueError("Image header, entry/abort branches, fallback or native core differs")
    if any(raw[ABORT_RECORD[0]:sum(ABORT_RECORD)]):
        raise ValueError("Abort record must start empty")
    # Header offsets do not establish S1's final physical placement. These are
    # conservative host contracts matching the independently booted stockTWRP.
    kernel_end = 0x80000 + len(raw)
    if not kernel_end < GEOMETRY[3] < GEOMETRY[1]:
        raise ValueError("Native reservation overlaps inherited FDT/initrd")
    if GEOMETRY[1] + 16461904 > 0x12000000:
        raise ValueError("Largest accepted payload initrd overlaps SD staging")
    pos = 0
    for number, original in enumerate(boards):
        if pos + 40 > len(tail):
            raise ValueError("Missing appended board DTB")
        size = struct.unpack_from(">I", tail, pos + 4)[0]
        if size < 40 or size > len(tail) - pos:
            raise ValueError("Invalid appended DTB extent")
        board = tail[pos:pos + size]
        tree = inspect_fdt(board)
        props = [(p[0], p[1], p[2]) for p in tree["props"] if p[1] in OWN_PROPERTIES]
        if len(props) != 3 or {p[1] for p in props} != OWN_PROPERTIES or any(p[0] != CHOSEN for p in props):
            raise ValueError("Duplicate/missing/misplaced carrier proof slot")
        if board != add_properties(original, label):
            raise ValueError("Board semantics/reservations/carrier proof changed")
        if sha(original) != BOARD_HASHES[number]:
            raise ValueError("Unattributed board")
        pos += size
    if len(boards) != 2 or pos != len(tail):
        raise ValueError("Unexpected appended board count or tail")
    return dict(image_bytes=len(blob), image_sha256=sha(blob), raw_image_bytes=len(raw),
                native_binary_bytes=len(native), native_binary_sha256=sha(native),
                fallback_sha256=sha(fallback), fallback_seconds=MODES[mode],
                ramdisk_bytes=len(boot.ramdisk), ramdisk_members=[e.name for e in entries],
                board_count=2, entry_watchdog_record_bytes=64, own_linux_kernel_included=False,
                private_donor_ramdisk_included=False, host_preflight=True,
                hardware_verified=False, phone_written=False)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--core", type=Path, default=root / "build/v1.0/menu.bin")
    parser.add_argument("--fallback", type=Path, default=root / "build/v1.0/fallback.bin")
    parser.add_argument("--output", type=Path, default=root / "build/v1.0/xploader.img")
    args = parser.parse_args()
    native, fallback = args.core.read_bytes(), args.fallback.read_bytes()
    manifest = json.loads((root / "releases/v1.0.json").read_text())
    if sha(native) != manifest["binary_sha256"]:
        raise ValueError("Pinned v1.0 native core required")
    boards = [(root / f"vendor/sumire-board/board-{i}.dtb").read_bytes() for i in range(2)]
    blob = build(native, fallback, boards, manifest["carrier_label"])
    result = preflight(blob, native, fallback, boards, manifest["carrier_label"])
    if sha(blob) != manifest["image"]["sha256"] or len(blob) != manifest["image"]["bytes"]:
        raise ValueError("Rebuilt image differs from accepted v1.0 package")
    exclusive_write(args.output, blob)
    print(json.dumps(result, indent=2))

if __name__ == "__main__":
    main()
