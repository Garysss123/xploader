"""Preserve supported OS boot components in an xploader Android-v0 envelope.

Sony E6653 stock ELF64 deliberately declares EM_ARM(40) while its kernel is an
AArch64 Image. Inspect the actual Image ABI; do not mistake the container for
an ARM32 executable. Kernel, compressed ramdisk and appended DTBs stay intact.
No kernel patch, root injection, verity change or SELinux change is performed.
"""
import gzip
import hashlib
import struct
import zlib

PAGE = 4096
MAX_BOOT = 32 * 1024 * 1024
MAX_KERNEL = 24 * 1024 * 1024
SONY_NOTE = 0x8053494e


def sha(data):
    return hashlib.sha256(data).hexdigest()


def align(value, size=PAGE):
    return (value + size - 1) // size * size


def inflate(data, limit, tail=False):
    decoder = zlib.decompressobj(31)
    try:
        raw = decoder.decompress(data, limit + 1)
    except zlib.error as error:
        raise ValueError("Boot component gzip/CRC is invalid") from error
    if len(raw) > limit or decoder.unconsumed_tail or not decoder.eof or (decoder.unused_data and not tail):
        raise ValueError("Truncated, oversized or multi-member boot component")
    return raw, decoder.unused_data


def kernel_abi(raw):
    if (not 68 <= len(raw) <= MAX_KERNEL or struct.unpack_from("<I", raw)[0] != 0x14000010 or
        raw[56:60] != b"ARM\x64" or struct.unpack_from("<I", raw, 64)[0] != 0xaa0003f5 or
        struct.unpack_from("<Q", raw, 8)[0] != 0x80000 or
        struct.unpack_from("<Q", raw, 16)[0] not in (0, len(raw))):
        raise ValueError("OS kernel does not match the supported AArch64 entry ABI/capacity")


def fdt_bootargs(blob):
    args = []
    offset = 0
    count = 0
    while offset < len(blob):
        if len(blob) - offset < 40:
            raise ValueError("Truncated appended DTB")
        magic, total, so, strings, reserve, version, last, _, ns, ss = struct.unpack_from(">10I", blob, offset)
        if (magic != 0xd00dfeed or not 40 <= total <= len(blob)-offset or version != 17 or last > 17 or
            not 40 <= reserve < so <= strings <= total or so + ss != strings or strings + ns > total):
            raise ValueError("Unsupported appended DTB geometry")
        dtb = blob[offset:offset + total]
        pos, end, stack = so, strings, []
        seen_end = False
        while pos + 4 <= end:
            token = struct.unpack_from(">I", dtb, pos)[0]
            pos += 4
            if token == 1:
                stop = dtb.find(b"\0", pos, end)
                if stop < 0 or len(stack) >= 64:
                    raise ValueError("Invalid DTB node")
                stack.append(dtb[pos:stop])
                pos = align(stop + 1, 4)
            elif token == 2:
                if not stack:
                    raise ValueError("Unbalanced DTB node")
                stack.pop()
            elif token == 3:
                if not stack or pos + 8 > end:
                    raise ValueError("Invalid DTB property")
                size, name_offset = struct.unpack_from(">II", dtb, pos)
                pos += 8
                if name_offset >= ns or size > end-pos:
                    raise ValueError("Invalid DTB property extent")
                name_end = dtb.find(b"\0", strings + name_offset, strings + ns)
                if name_end < 0:
                    raise ValueError("Invalid DTB property name")
                if stack == [b"", b"chosen"] and dtb[strings+name_offset:name_end] == b"bootargs":
                    value = dtb[pos:pos+size]
                    if not value.endswith(b"\0") or b"\0" in value[:-1]:
                        raise ValueError("Invalid stock bootargs")
                    args.append(value[:-1].decode("ascii"))
                pos = align(pos + size, 4)
            elif token == 4:
                pass
            elif token == 9:
                if stack or pos != end:
                    raise ValueError("Invalid DTB end")
                seen_end = True
                break
            else:
                raise ValueError("Unknown DTB token")
        if not seen_end:
            raise ValueError("Missing DTB end")
        offset += total
        count += 1
        if count > 8:
            raise ValueError("Unexpected board count")
    if not count or len(set(args)) > 1:
        raise ValueError("Board-specific bootargs require a separate adapter")
    return " ".join(dict.fromkeys(args)), count


def android_v0(blob):
    if len(blob) < PAGE or blob[:8] != b"ANDROID!":
        raise ValueError("Not an Android boot image")
    ks, ka, rs, ra, second, sa, tags, page, version, osversion = struct.unpack_from("<10I", blob, 8)
    if page != PAGE or version or second or not ks or not rs:
        raise ValueError("Only supported Android-v0 kernel/ramdisk images can be preserved")
    end = PAGE + align(ks) + align(rs)
    if end > len(blob) or end > MAX_BOOT:
        raise ValueError("OS boot image exceeds the separate32MiB slot")
    kernel = blob[PAGE:PAGE+ks]
    ramdisk = blob[PAGE+align(ks):PAGE+align(ks)+rs]
    raw, tail = inflate(kernel, MAX_KERNEL, True)
    kernel_abi(raw)
    init, _ = inflate(ramdisk, 64*1024*1024)
    if not init.startswith(b"070701"):
        raise ValueError("Expected gzip/newc OS ramdisk")
    command = (blob[64:576] + blob[608:1632]).split(b"\0", 1)[0].decode("ascii")
    report = dict(source_format="Android boot v0", image_bytes=end, image_sha256=sha(blob[:end]),
                  kernel_raw_bytes=len(raw), kernel_raw_sha256=sha(raw), ramdisk_gzip_sha256=sha(ramdisk),
                  appended_DTBytes=len(tail), appended_DTB_sha256=sha(tail), cmdline=command,
                  kernel_patched=False, ramdisk_patched=False)
    return blob[:end], report


def preserve_os_boot(blob):
    if blob.startswith(b"ANDROID!"):
        return android_v0(blob)
    if len(blob) < 232 or blob[:8] != b"\x7fELF\x02\x01\x01\x61":
        raise ValueError("Unsupported original boot format")
    h = struct.unpack_from("<HHIQQQIHHHHHH", blob, 16)
    etype, machine, ev, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shstr = h
    if (etype, machine, ev, entry, phoff, flags, ehsize, phsize, phnum, shsize, shnum, shstr) != (
        2, 40, 1, 0x80000, 64, 0, 64, 56, 3, 64, 1, 0
    ) or not 232 <= shoff <= MAX_BOOT-64 or shoff+64 > len(blob):
        raise ValueError("Unsupported Sony ELF64 boot envelope")
    parts = []
    previous = 232
    for index, address in enumerate((0x80000, 0x2000000, 0x1e00000)):
        kind, pflags, offset, va, pa, size, memory, alignment = struct.unpack_from("<IIQQQQQQ", blob, phoff+index*phsize)
        if (kind != 1 or pflags or offset != previous or va != address or pa != address or
            not size or size != memory or alignment or size > shoff-offset):
            raise ValueError("Unsupported Sony kernel/ramdisk/DTB segment")
        parts.append(blob[offset:offset+size])
        previous = offset+size
    section = struct.unpack_from("<IIQQQQIIQQ", blob, shoff)
    if section != (0, SONY_NOTE, 0, 0, previous, 4096, 0, 0, 0, 0) or previous+4096 != shoff:
        raise ValueError("Unsupported Sony boot-argument/signature note")
    note = blob[previous:shoff]
    if struct.unpack_from("<II", note) != (1, 0x1e00000) or b"\0" not in note[8:520]:
        raise ValueError("Unsupported Sony command-line record")
    command = note[8:520].split(b"\0", 1)[0].decode("ascii")
    kernel, ramdisk, dtbs = parts
    if struct.unpack_from("<I", note, 520)[0] != 3:
        raise ValueError("Unsupported Sony component digest table")
    for index, (part, compression) in enumerate(zip(parts, (0, 1, 0))):
        at = 524 + 44*index
        if (struct.unpack_from("<III", note, at) != (index, compression, 2) or
            note[at+12:at+44] != hashlib.sha256(part).digest()):
            raise ValueError("Stock Sony component SHA256 differs")
    kernel_abi(kernel)
    init, _ = inflate(ramdisk, 64*1024*1024)
    if not init.startswith(b"070701"):
        raise ValueError("Expected unmodified stock gzip/newc ramdisk")
    chosen, boards = fdt_bootargs(dtbs)
    command = (chosen + " " + command).strip()
    cmd = command.encode("ascii")
    if len(cmd) >= 1536 or any(token == "androidboot.mode=charger" for token in command.split()):
        raise ValueError("Unsupported OS command line")
    compressed = gzip.compress(kernel, compresslevel=9, mtime=0) + dtbs
    header = bytearray(PAGE)
    header[:8] = b"ANDROID!"
    struct.pack_into("<10I", header, 8, len(compressed), 32768, len(ramdisk), 33554432, 0, 15728640, 31457280, PAGE, 0, 0)
    header[48:60] = b"stock-sumire"
    header[64:64+min(512,len(cmd))] = cmd[:512]
    if len(cmd) > 512:
        header[608:608+len(cmd)-512] = cmd[512:]
    ident = hashlib.sha1()
    for part in (compressed, ramdisk, b""):
        ident.update(part)
        ident.update(struct.pack("<I", len(part)))
    header[576:608] = ident.digest() + bytes(12)
    image = (bytes(header) + compressed + bytes(align(len(compressed))-len(compressed)) +
             ramdisk + bytes(align(len(ramdisk))-len(ramdisk)))
    output, report = android_v0(image)
    if report["kernel_raw_sha256"] != sha(kernel) or report["ramdisk_gzip_sha256"] != sha(ramdisk):
        raise AssertionError("Stock OS components changed during envelope conversion")
    report.update(source_format="Sony ELF64 E6653", original_used_bytes=shoff+64,
                  original_used_sha256=sha(blob[:shoff+64]), source_DTBytes=len(dtbs), source_DTB_sha256=sha(dtbs),
                  stock_board_count=boards, Sony_component_hashes_verified=True,
                  Sony_envelope_converted=True, bootable_on_hardware_verified=False)
    return output, report
