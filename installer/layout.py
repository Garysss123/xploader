"""Strict E6653 GPT inspection and GUID-preserving32+32MiB migration plans.

Pure host functions. They never open a block device. Unknown layouts are refused.
"""
from dataclasses import dataclass
import hashlib
import struct
import uuid
import zlib

SECTOR = 512
DISK_SECTORS = 61071360
BOOT_START = 244224
BOOT_SECTORS = 131072
HALF = 65536
OS_START = BOOT_START + HALF
ENTRY_COUNT = 128
ENTRY_BYTES = 128
ARRAY_BYTES = ENTRY_COUNT * ENTRY_BYTES
PRIMARY_BYTES = 34 * SECTOR
BACKUP_BYTES = 33 * SECTOR


def digest(data):
    return hashlib.sha256(data).hexdigest()


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def u64(data, offset):
    return struct.unpack_from("<Q", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


def _header(block, backup):
    if len(block) != SECTOR or block[:8] != b"EFI PART" or u32(block, 8) != 0x10000 or u32(block, 12) != 92:
        raise ValueError("Unsupported GPT header")
    current, alternate = (DISK_SECTORS - 1, 1) if backup else (1, DISK_SECTORS - 1)
    if (u64(block, 24), u64(block, 32), u64(block, 72), u32(block, 80), u32(block, 84)) != (
        current, alternate, DISK_SECTORS - 33 if backup else 2, ENTRY_COUNT, ENTRY_BYTES
    ):
        raise ValueError("Unsupported E6653 GPT geometry")
    if u64(block, 40) != 34 or u64(block, 48) != DISK_SECTORS - 34 or any(block[20:24]) or any(block[92:]):
        raise ValueError("Unsupported GPT usable range/padding")
    copy = bytearray(block[:92])
    copy[16:20] = bytes(4)
    if crc(copy) != u32(block, 16):
        raise ValueError("Invalid GPT header CRC")
    if not any(block[56:72]):
        raise ValueError("Missing GPT disk GUID")


def _entries(array):
    result = []
    for number in range(ENTRY_COUNT):
        raw = array[number * ENTRY_BYTES:(number + 1) * ENTRY_BYTES]
        if not any(raw):
            continue
        if not any(raw[:16]) or not any(raw[16:32]):
            raise ValueError("Partially populated GPT entry")
        text = raw[56:128].decode("utf-16le", "strict")
        name = text.split("\0", 1)[0]
        if not name or any(ord(c) < 32 for c in name) or any(ord(c) for c in text[len(name):]):
            raise ValueError("Invalid GPT name")
        first, last = u64(raw, 32), u64(raw, 40)
        if not 34 <= first <= last <= DISK_SECTORS - 34:
            raise ValueError("Partition outside usable disk")
        result.append(dict(number=number + 1, name=name, first=first, last=last, sectors=last-first+1,
                           type_guid=raw[:16].hex(), unique_guid=raw[16:32].hex(), attributes=u64(raw, 48)))
    ids = [p["unique_guid"] for p in result]
    if len(ids) != len(set(ids)):
        raise ValueError("Duplicate partition GUID")
    ordered = sorted(result, key=lambda p: p["first"])
    if any(a["last"] >= b["first"] for a, b in zip(ordered, ordered[1:])):
        raise ValueError("Overlapping GPT entries")
    return result


@dataclass(frozen=True)
class Layout:
    state: str
    primary: bytes
    backup: bytes
    entries: tuple

    @property
    def array(self):
        return self.primary[1024:]

    def summary(self):
        return dict(state=self.state, sectors512=DISK_SECTORS,
                    primary_sha256=digest(self.primary), backup_sha256=digest(self.backup),
                    manager_partition=29, OS_boot_partition=44 if self.state == "split" else 29)


def inspect(primary, backup, disk_sectors=DISK_SECTORS):
    if disk_sectors != DISK_SECTORS or len(primary) != PRIMARY_BYTES or len(backup) != BACKUP_BYTES:
        raise ValueError("Only the measured32GB E6653 disk geometry is supported")
    _header(primary[512:1024], False)
    _header(backup[-512:], True)
    array = primary[1024:]
    if array != backup[:ARRAY_BYTES] or crc(array) != u32(primary, 512 + 88) or crc(array) != u32(backup, ARRAY_BYTES + 88):
        raise ValueError("GPT arrays/CRCs disagree")
    if primary[512 + 40:512 + 72] != backup[ARRAY_BYTES + 40:ARRAY_BYTES + 72]:
        raise ValueError("GPT mirrors identify different disks")
    mbr = primary[:512]
    protective = mbr[446:462]
    if (mbr[510:] != b"\x55\xaa" or protective[4] != 0xee or
        u32(protective, 8) != 1 or u32(protective, 12) != DISK_SECTORS - 1 or any(mbr[462:510])):
        raise ValueError("Unsupported protective MBR")
    entries = _entries(array)
    table = {p["number"]: p for p in entries}
    manager = table.get(29)
    if not manager or manager["first"] != BOOT_START:
        raise ValueError("Original boot extent not found")
    names = [p["name"] for p in entries]
    if names.count("boot") != 1:
        raise ValueError("One unambiguous boot name is required")
    if manager["name"] == "boot" and manager["sectors"] == BOOT_SECTORS and 44 not in table and "xploader" not in names:
        state = "stock"
    elif manager["name"] == "xploader" and manager["sectors"] == HALF and names.count("xploader") == 1:
        os_boot = table.get(44)
        if (not os_boot or os_boot["name"] != "boot" or os_boot["first"] != OS_START or
            os_boot["sectors"] != HALF or os_boot["type_guid"] != manager["type_guid"] or os_boot["attributes"] != 0):
            raise ValueError("Unsupported split OS boot extent")
        state = "split"
    else:
        raise ValueError("Unsupported boot layout; no write plan can be created")
    return Layout(state, bytes(primary), bytes(backup), tuple(entries))


def _refresh(block, array):
    header = bytearray(block)
    struct.pack_into("<I", header, 88, crc(array))
    header[16:20] = bytes(4)
    struct.pack_into("<I", header, 16, crc(header[:92]))
    return bytes(header)


def plan_split(layout, os_guid=None):
    if layout.state == "split":
        return layout
    if layout.state != "stock":
        raise ValueError("Validated stock layout required")
    unique = (os_guid or uuid.uuid4()).bytes_le
    if not any(unique) or unique.hex() in {p["unique_guid"] for p in layout.entries}:
        raise ValueError("New OS GUID collides with an existing partition")
    array = bytearray(layout.array)
    manager = bytearray(array[28 * 128:29 * 128])
    manager[56:128] = "xploader".encode("utf-16le") + bytes(72 - 16)
    struct.pack_into("<Q", manager, 40, BOOT_START + HALF - 1)
    array[28 * 128:29 * 128] = manager
    os_boot = bytearray(128)
    os_boot[:16], os_boot[16:32] = manager[:16], unique
    struct.pack_into("<QQ", os_boot, 32, OS_START, BOOT_START + BOOT_SECTORS - 1)
    os_boot[56:64] = "boot".encode("utf-16le")
    array[43 * 128:44 * 128] = os_boot
    primary = layout.primary[:512] + _refresh(layout.primary[512:1024], array) + array
    backup = bytes(array) + _refresh(layout.backup[-512:], array)
    result = inspect(primary, backup)
    for slot in range(128):
        if slot not in (28, 43) and result.array[slot*128:(slot+1)*128] != layout.array[slot*128:(slot+1)*128]:
            raise AssertionError("Unrelated partition changed")
    if result.array[28*128+16:28*128+32] != layout.array[28*128+16:28*128+32]:
        raise AssertionError("Sony normal-boot GUID changed")
    return result
