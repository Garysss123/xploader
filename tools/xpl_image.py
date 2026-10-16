"""Host-only Android boot/newc/full-OTA inspection. Never opens a block device."""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import gzip
import hashlib
import json
import pathlib
import re
import struct
import zipfile
import zlib

MAX_RAMDISK = 64 * 1024 * 1024
MAX_BOOT = 128 * 1024 * 1024


def align(value, boundary):
    return (value + boundary - 1) // boundary * boundary


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def inflate_gzip(data, limit=MAX_RAMDISK, allow_tail=False):
    decoder = zlib.decompressobj(31)
    raw = decoder.decompress(data, limit + 1)
    if len(raw) > limit or decoder.unconsumed_tail:
        raise ValueError("expanded gzip exceeds bound")
    if not decoder.eof:
        raise ValueError("truncated gzip")
    if decoder.unused_data and not allow_tail:
        raise ValueError("unexpected bytes after gzip member")
    return raw, decoder.unused_data


@dataclass
class CpioEntry:
    name: str
    fields: list[int]
    data: bytes


def parse_newc(raw):
    result = []
    offset = 0
    names = set()
    while True:
        if offset + 110 > len(raw) or raw[offset:offset + 6] != b"070701":
            raise ValueError("invalid or truncated newc header")
        try:
            fields = [int(raw[offset + 6 + i * 8:offset + 14 + i * 8], 16) for i in range(13)]
        except ValueError:
            raise ValueError("invalid newc numeric field") from None
        size, name_size = fields[6], fields[11]
        if not 1 <= name_size <= 4096:
            raise ValueError("invalid newc name size")
        start = offset + 110
        name_end = start + name_size
        if name_end > len(raw) or raw[name_end - 1] != 0:
            raise ValueError("truncated newc name")
        name = raw[start:name_end - 1].decode("utf-8", "strict")
        data_start = align(name_end, 4)
        data_end = data_start + size
        if data_end > len(raw):
            raise ValueError("truncated newc data")
        if name == "TRAILER!!!":
            if size or any(raw[align(data_end, 4):]):
                raise ValueError("unexpected bytes after newc trailer")
            break
        if not name or name.startswith("/") or ".." in name.split("/") or name in names:
            raise ValueError("unsafe or duplicate newc name")
        names.add(name)
        result.append(CpioEntry(name, fields, raw[data_start:data_end]))
        offset = align(data_end, 4)
        if len(result) > 65536:
            raise ValueError("too many newc entries")
    return result


def pack_newc(entries):
    out = bytearray()
    names = set()
    for entry in [*entries, CpioEntry("TRAILER!!!", [0] * 13, b"")]:
        if entry.name in names:
            raise ValueError("duplicate newc name")
        names.add(entry.name)
        fields = list(entry.fields)
        name = entry.name.encode() + b"\0"
        fields[6], fields[11], fields[12] = len(entry.data), len(name), 0
        if any(not 0 <= v <= 0xffffffff for v in fields):
            raise ValueError("newc field overflow")
        out.extend(b"070701" + b"".join(f"{v:08x}".encode() for v in fields))
        out.extend(name)
        out.extend(b"\0" * (align(len(out), 4) - len(out)))
        out.extend(entry.data)
        out.extend(b"\0" * (align(len(out), 4) - len(out)))
    return bytes(out)


class BootImage:
    def __init__(self, blob):
        if len(blob) > MAX_BOOT or len(blob) < 1632 or blob[:8] != b"ANDROID!":
            raise ValueError("not a bounded Android boot image")
        self.blob = blob
        values = struct.unpack_from("<10I", blob, 8)
        (ks, self.kernel_addr, rs, self.ramdisk_addr, ss, self.second_addr,
         self.tags_addr, self.page, self.header_version, self.os_version) = values
        if self.header_version != 0:
            raise ValueError("only modern header v0 supported; legacy dt_size/ELF need separate adapters")
        if self.page not in (2048, 4096, 8192, 16384) or not ks or ks > MAX_BOOT or rs > MAX_BOOT:
            raise ValueError("invalid boot geometry")
        offset = self.page
        parts = []
        self.offsets = []
        for size in (ks, rs, ss):
            if offset + size > len(blob):
                raise ValueError("truncated boot component")
            self.offsets.append(offset)
            parts.append(blob[offset:offset + size])
            offset = align(offset + size, self.page)
        if offset > len(blob):
            raise ValueError("missing boot page padding")
        self.kernel, self.ramdisk, self.second = parts
        self.tail = blob[offset:]
        self.cmdline = (blob[64:576].split(b"\0", 1)[0] + blob[608:1632].split(b"\0", 1)[0]).decode("ascii", "replace")

    def entries(self):
        raw, _ = inflate_gzip(self.ramdisk)
        return parse_newc(raw)

    def rebuild(self, entries, kernel=None):
        if self.tail and any(self.tail):
            raise ValueError("signed or unknown trailing data; refusing to invalidate it")
        ramdisk = gzip.compress(pack_newc(entries), mtime=0)
        kernel = self.kernel if kernel is None else kernel
        if not kernel or len(kernel) > MAX_BOOT:
            raise ValueError("invalid rebuilt kernel")
        header = bytearray(self.blob[:self.page])
        struct.pack_into("<I", header, 8, len(kernel))
        struct.pack_into("<I", header, 16, len(ramdisk))
        ident = hashlib.sha1()
        for part in (kernel, ramdisk, self.second):
            ident.update(part)
            ident.update(struct.pack("<I", len(part)))
        header[576:608] = ident.digest() + b"\0" * 12
        out = bytes(header)
        for part in (kernel, ramdisk, self.second):
            out += part + b"\0" * (align(len(part), self.page) - len(part))
        return out + self.tail

    def audit(self):
        entries = self.entries()
        raw_kernel, dtbs = inflate_gzip(self.kernel, limit=128 * 1024 * 1024, allow_tail=True)
        linux = re.search(rb"Linux version [^\0\n]+", raw_kernel)
        fstabs = {e.name: e.data.decode("utf-8", "replace") for e in entries if "fstab" in e.name}
        return {"format": "android-boot-v0", "sha256": sha256(self.blob), "bytes": len(self.blob),
                "page_size": self.page, "cmdline": self.cmdline,
                "kernel_sha256": sha256(self.kernel), "ramdisk_sha256": sha256(self.ramdisk),
                "kernel_bytes": len(self.kernel), "ramdisk_bytes": len(self.ramdisk),
                "kernel_architecture": "aarch64" if raw_kernel[56:60] == b"ARM\x64" else "unidentified",
                "linux_version": linux.group().decode("ascii", "replace") if linux else None,
                "appended_dtb_bytes": len(dtbs),
                "ramdisk_entries": [{"name": e.name, "mode": oct(e.fields[1]), "bytes": len(e.data)} for e in entries],
                "fstabs": fstabs, "hardware_verified": False}


def zip_audit(path):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)):
            raise ValueError("duplicate ZIP entries")
        if "boot.img" not in names or archive.getinfo("boot.img").file_size > MAX_BOOT:
            raise ValueError("missing/oversized boot.img")
        result = {"archive": str(path), "files": [{"name": i.filename, "bytes": i.file_size} for i in archive.infolist()],
                  "boot": BootImage(archive.read("boot.img")).audit()}
        for name in ("META-INF/com/google/android/updater-script", "META-INF/com/android/metadata", "system.transfer.list"):
            if name in names:
                if archive.getinfo(name).file_size > 4 * 1024 * 1024:
                    raise ValueError("oversized metadata")
                result[name] = archive.read(name).decode("utf-8", "strict")
        return result


def exclusive_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("xb") as stream:
        stream.write(data)


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="command", required=True)
    audit = sub.add_parser("audit")
    audit.add_argument("source", type=pathlib.Path)
    audit.add_argument("--output", type=pathlib.Path)
    stage = sub.add_parser("stage-rom")
    stage.add_argument("source", type=pathlib.Path)
    stage.add_argument("destination", type=pathlib.Path)
    args = parser.parse_args()
    if args.command == "audit":
        result = zip_audit(args.source) if zipfile.is_zipfile(args.source) else BootImage(args.source.read_bytes()).audit()
        text = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
        if args.output:
            exclusive_write(args.output, text.encode())
        else:
            print(text)
    else:
        result = zip_audit(args.source)
        args.destination.mkdir(parents=True, exist_ok=False)
        with zipfile.ZipFile(args.source) as archive:
            for name in ("boot.img", "system.new.dat.br", "system.transfer.list"):
                if name not in archive.namelist():
                    raise ValueError("only full block OTA staging supported")
                with archive.open(name) as incoming, (args.destination / name).open("xb") as outgoing:
                    while chunk := incoming.read(1024 * 1024):
                        outgoing.write(chunk)
        exclusive_write(args.destination / "audit.json", (json.dumps(result, indent=2) + "\n").encode())
        print(json.dumps({"status": "staged_host_files_only", "directory": str(args.destination)}))


if __name__ == "__main__":
    main()
