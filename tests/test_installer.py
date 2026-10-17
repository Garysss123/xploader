"""Synthetic disk/ELF tests plus the actual native guard's host validator."""
import gzip
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from installer import layout
from installer.boot import preserve_os_boot, android_v0, SONY_NOTE
from installer.plan import prepare, store, load, execute, validate_resume, HALF_BYTES


def refresh(header, array):
    header = bytearray(header)
    struct.pack_into("<I", header, 88, zlib.crc32(array) & 0xffffffff)
    header[16:20] = bytes(4)
    struct.pack_into("<I", header, 16, zlib.crc32(header[:92]) & 0xffffffff)
    return bytes(header)


def stock_gpt():
    array = bytearray(16384)
    for slot, name, first, last in [(0, "misc", 2048, 4095), (27, "firmware", 8192, 16383),
                                   (28, "boot", 244224, 375295), (29, "system", 400000, 1400000),
                                   (42, "userdata", 2000000, layout.DISK_SECTORS-34)]:
        p = bytearray(128)
        p[:16] = uuid.UUID("11111111-2222-3333-4444-555555555555").bytes_le
        p[16:32] = uuid.uuid5(uuid.NAMESPACE_OID, "xploader-synthetic-slot-" + str(slot)).bytes_le
        struct.pack_into("<QQ", p, 32, first, last)
        p[56:56+len(name)*2] = name.encode("utf-16le")
        array[slot*128:(slot+1)*128] = p
    def header(backup):
        h = bytearray(512)
        h[:8] = b"EFI PART"
        struct.pack_into("<IIII", h, 8, 0x10000, 92, 0, 0)
        struct.pack_into("<QQQQ", h, 24, layout.DISK_SECTORS-1 if backup else 1,
                         1 if backup else layout.DISK_SECTORS-1, 34, layout.DISK_SECTORS-34)
        h[56:72] = uuid.uuid5(uuid.NAMESPACE_OID, "xploader-synthetic-disk").bytes_le
        struct.pack_into("<QII", h, 72, layout.DISK_SECTORS-33 if backup else 2, 128, 128)
        return refresh(h, array)
    mbr = bytearray(512)
    mbr[446+4] = 0xee
    struct.pack_into("<II", mbr, 446+8, 1, layout.DISK_SECTORS-1)
    mbr[510:] = b"\x55\xaa"
    return bytes(mbr) + header(False) + array, bytes(array) + header(True)


def synthetic_elf():
    kernel = bytearray(68)
    struct.pack_into("<IIQ", kernel, 0, 0x14000010, 0, 0x80000)
    kernel[56:60] = b"ARM\x64"
    struct.pack_into("<I", kernel, 64, 0xaa0003f5)
    fields = [0]*13
    fields[4], fields[11] = 1, 11
    cpio = b"070701" + b"".join(f"{n:08x}".encode() for n in fields) + b"TRAILER!!!\0"
    cpio += bytes((-len(cpio)) % 4)
    ramdisk = gzip.compress(cpio, mtime=0)
    s = bytearray(struct.pack(">I", 1) + b"\0\0\0\0" + struct.pack(">I", 1) + b"chosen\0\0")
    value = b"sched_enable_hmp=1\0"
    s += struct.pack(">III", 3, len(value), 0) + value
    s += bytes((-len(s)) % 4)
    s += struct.pack(">III", 2, 2, 9)
    strings = b"bootargs\0"
    dtb = struct.pack(">10I", 0xd00dfeed, 56+len(s)+len(strings), 56, 56+len(s), 40, 17, 16, 0, len(strings), len(s)) + bytes(16) + s + strings
    parts = [bytes(kernel), ramdisk, dtb]
    note = bytearray(4096)
    struct.pack_into("<II", note, 0, 1, 0x1e00000)
    command = b"androidboot.hardware=qcom buildvariant=user"
    note[8:8+len(command)] = command
    struct.pack_into("<I", note, 520, 3)
    for i, (part, compression) in enumerate(zip(parts, (0, 1, 0))):
        struct.pack_into("<III", note, 524+44*i, i, compression, 2)
        note[536+44*i:568+44*i] = hashlib.sha256(part).digest()
    note_offset = 232 + sum(map(len, parts))
    shoff = note_offset + 4096
    h = bytearray(64)
    h[:8] = b"\x7fELF\x02\x01\x01\x61"
    struct.pack_into("<HHIQQQIHHHHHH", h, 16, 2, 40, 1, 0x80000, 64, shoff, 0, 64, 56, 3, 64, 1, 0)
    program = bytearray()
    at = 232
    for part, pa in zip(parts, (0x80000, 0x2000000, 0x1e00000)):
        program += struct.pack("<IIQQQQQQ", 1, 0, at, pa, pa, len(part), len(part), 0)
        at += len(part)
    elf = bytes(h) + program + b"".join(parts) + note + struct.pack("<IIQQQQIIQQ", 0, SONY_NOTE, 0, 0, note_offset, 4096, 0, 0, 0, 0)
    return bytes(elf), parts


class Disk:
    def __init__(self, primary, backup, manager, os_boot, fail_after=None, partial=False):
        self.regions = {0: bytearray(primary), (layout.DISK_SECTORS-33)*512: bytearray(backup),
                        layout.BOOT_START*512: bytearray(manager), layout.OS_START*512: bytearray(os_boot)}
        self.calls = 0
        self.fail_after = fail_after
        self.partial = partial
        self.writes = []
        self.unrelated = b"System, userdata, SD, TA and S1 remain outside this backend"

    def read(self, offset, size):
        for start, value in self.regions.items():
            if start <= offset and offset+size <= start+len(value):
                return bytes(value[offset-start:offset-start+size])
        raise AssertionError("I/O outside the approved regions")

    def write(self, op):
        self.calls += 1
        size = len(op.after)//2 if self.partial and self.calls == self.fail_after else len(op.after)
        self.regions[op.offset][:size] = op.after[:size]
        self.writes.append(op.summary())
        if self.calls == self.fail_after:
            raise OSError("Injected connection loss after write")
        return dict(accepted=True, exact_readback=True)


class InstallerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.primary, cls.backup = stock_gpt()
        cls.elf, cls.parts = synthetic_elf()
        cls.os_image, cls.conversion = preserve_os_boot(cls.elf)
        manager = bytearray(cls.os_image)
        manager[48:64] = b"xploader" + bytes(8)
        cls.manager_image = bytes(manager)
        cls.manager_region = cls.elf + bytes(HALF_BYTES-len(cls.elf))
        cls.os_region = bytes(HALF_BYTES)
        cls.plan = prepare(cls.primary, cls.backup, cls.manager_region, cls.os_region,
                           cls.manager_image, hashlib.sha256(cls.manager_image).hexdigest())

    def test_stock_conversion_preserves_every_component(self):
        self.assertTrue(self.conversion["Sony_component_hashes_verified"])
        self.assertEqual(self.conversion["kernel_raw_sha256"], hashlib.sha256(self.parts[0]).hexdigest())
        self.assertEqual(self.conversion["ramdisk_gzip_sha256"], hashlib.sha256(self.parts[1]).hexdigest())
        self.assertEqual(self.conversion["appended_DTB_sha256"], hashlib.sha256(self.parts[2]).hexdigest())
        same, _ = preserve_os_boot(self.os_image)
        self.assertEqual(same, self.os_image)

    def test_corrupt_stock_components_or_envelope_are_rejected(self):
        for offset in (4, 18, 24, 32, 54, 56, 64, 64+8, 232+64, len(self.elf)-64+4):
            bad = bytearray(self.elf)
            bad[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                preserve_os_boot(bytes(bad))

    def test_first_install_keeps_every_unrelated_GPT_byte_and_boot_GUID(self):
        self.assertEqual([op.kind for op in self.plan.operations], ["os", "backup", "primary", "manager"])
        a, b = self.plan.original.array, self.plan.target.array
        for i in range(128):
            if i not in (28, 43):
                self.assertEqual(a[i*128:(i+1)*128], b[i*128:(i+1)*128])
        self.assertEqual(a[28*128+16:28*128+32], b[28*128+16:28*128+32])
        self.assertEqual(self.plan.target.state, "split")

    def test_unknown_layout_and_mismatched_mirrors_are_refused(self):
        for offset in (510, 512+16, 512+40, 1024+28*128+32, 1024+43*128+16):
            bad = bytearray(self.primary)
            bad[offset] ^= 1
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                layout.inspect(bytes(bad), self.backup)
        with self.assertRaises(ValueError):
            layout.inspect(self.primary, self.backup, layout.DISK_SECTORS+1)

    def test_complete_transaction_and_idempotent_repeat(self):
        disk = Disk(self.primary, self.backup, self.manager_region, self.os_region)
        events = []
        execute(self.plan, disk, events.append)
        self.assertEqual(len(disk.writes), 4)
        before = len(disk.writes)
        execute(self.plan, disk, events.append)
        self.assertEqual(len(disk.writes), before)
        self.assertEqual(disk.read(layout.OS_START*512, len(self.os_image)), self.os_image)
        self.assertEqual(disk.read(layout.BOOT_START*512, len(self.manager_image)), self.manager_image)

    def test_link_loss_after_each_complete_step_can_resume_without_repeating_writes(self):
        for failure in range(1, 5):
            with self.subTest(failure=failure):
                disk = Disk(self.primary, self.backup, self.manager_region, self.os_region, fail_after=failure)
                with self.assertRaises(OSError):
                    execute(self.plan, disk, lambda _: None)
                validate_resume(self.plan, disk.read(layout.BOOT_START*512, HALF_BYTES), disk.read(layout.OS_START*512, HALF_BYTES),
                                disk.read(0, len(self.primary)), disk.read((layout.DISK_SECTORS-33)*512, len(self.backup)))
                disk.fail_after = None
                execute(self.plan, disk, lambda _: None)
                self.assertEqual(len(disk.writes), 4)

    def test_partial_write_is_not_mistaken_for_a_resumable_whole_step(self):
        disk = Disk(self.primary, self.backup, self.manager_region, self.os_region, fail_after=1, partial=True)
        with self.assertRaises(OSError):
            execute(self.plan, disk, lambda _: None)
        with self.assertRaises(ValueError):
            validate_resume(self.plan, disk.read(layout.BOOT_START*512, HALF_BYTES), disk.read(layout.OS_START*512, HALF_BYTES),
                            disk.read(0, len(self.primary)), disk.read((layout.DISK_SECTORS-33)*512, len(self.backup)))

    def test_update_writes_only_manager_and_preserves_the_OS_slot(self):
        disk = Disk(self.primary, self.backup, self.manager_region, self.os_region)
        execute(self.plan, disk, lambda _: None)
        after_os = disk.read(layout.OS_START*512, HALF_BYTES)
        new = prepare(self.plan.target.primary, self.plan.target.backup,
                      disk.read(layout.BOOT_START*512, HALF_BYTES), after_os, self.manager_image, hashlib.sha256(self.manager_image).hexdigest())
        self.assertEqual([op.kind for op in new.operations], ["manager"])
        execute(new, disk, lambda _: None)
        self.assertEqual(after_os, disk.read(layout.OS_START*512, HALF_BYTES))

    def test_job_snapshots_are_reloaded_and_tampering_is_rejected(self):
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="installer-test-", dir=build) as temp:
            self.assertTrue(Path(temp).resolve().is_relative_to(build.resolve()))
            folder = Path(temp) / "job"
            store(self.plan, folder, dict(serial="synthetic", cid="0"*32))
            loaded = load(folder)
            self.assertEqual(loaded.mode, self.plan.mode)
            bad = folder / "03-manager-after.bin"
            bad.write_bytes(bad.read_bytes()[:-1] + b"x")
            with self.assertRaises(ValueError):
                load(folder)

    def test_actual_C_guard_validates_pair_and_rejects_valid_CRC_forbidden_mutations(self):
        guard = os.environ.get("XPL_HOST_GUARD")
        if not guard:
            self.skipTest("Set XPL_HOST_GUARD to the host guard executable")
        build = ROOT / "build"
        build.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="installer-C-test-", dir=build) as temp:
            self.assertTrue(Path(temp).resolve().is_relative_to(build.resolve()))
            temp = Path(temp)
            def run(kind, before, after, accepted):
                (temp / "before").write_bytes(before)
                (temp / "after").write_bytes(after)
                p = subprocess.run([guard, "--validate", kind, str(temp / "before"), str(temp / "after")], capture_output=True)
                self.assertNotIn(b"AddressSanitizer", p.stderr)
                self.assertNotIn(b"runtime error", p.stderr)
                self.assertEqual(p.returncode, 0 if accepted else 1, p.stderr.decode(errors="replace"))
            for op in self.plan.operations:
                run(op.kind, op.before, op.after, True)
            for offset in (0, 512+56, 512+120, 1024+28*128+16, 1024+28*128+32, 1024+29*128+56,
                           1024+43*128, 1024+43*128+32, 1024+43*128+48, 1024+43*128+56):
                bad = bytearray(self.plan.target.primary)
                bad[offset] ^= 1
                bad[512:1024] = refresh(bad[512:1024], bad[1024:])
                run("primary", self.primary, bytes(bad), False)


if __name__ == "__main__":
    unittest.main(verbosity=2)
