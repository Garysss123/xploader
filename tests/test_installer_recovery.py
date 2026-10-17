"""Check boot-mode correction and strict preservation outside installer UI."""
from pathlib import Path
import gzip
import hashlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from installer.recovery_boot import build, prepare
from tools.xpl_image import BootImage, CpioEntry, align, pack_newc


def fixture():
    actions = '<action><action function="set">tw_page_done=1</action></action>' * 3
    theme = ('<recovery><pages>' + ''.join('<page name="'+name+'">'+
             (actions if name == "singleaction_page" else '<text><text>Original menu</text></text>')+
             '</page>' for name in ("main2", "singleaction_page", "system_readonly", "decrypt", "lock"))+
             '</pages></recovery>').encode()
    entries = []
    for name, data in (("init", b"original init executable\0"), ("sbin/recovery", b"original recovery executable\0"),
                       ("init.rc", b"original init services\n"), ("twres/portrait.xml", theme)):
        fields = [0] * 13
        fields[1], fields[4] = 0o100644, 1
        entries.append(CpioEntry(name, fields, data))
    kernel = gzip.compress(b"unchanged kernel and appended board data", mtime=0)
    ramdisk = gzip.compress(pack_newc(entries), mtime=0)
    header = bytearray(4096)
    header[:8] = b"ANDROID!"
    struct.pack_into("<10I", header, 8, len(kernel), 32768, len(ramdisk), 33554432,
                     0, 15728640, 31457280, 4096, 0, 0)
    command = b"console=ttyS0 androidboot.mode=charger buildvariant=eng"
    header[64:64+len(command)] = command
    image = bytes(header)
    for part in (kernel, ramdisk):
        image += part + bytes(align(len(part), 4096)-len(part))
    return image


class InstallerRecoveryTests(unittest.TestCase):
    def test_correct_mode_and_preserve_kernel_executables_and_init(self):
        source = fixture()
        result = build(source, hashlib.sha256(source).hexdigest(), len(source))
        old, new = BootImage(source), BootImage(result)
        self.assertEqual(new.kernel, old.kernel)
        self.assertEqual((new.kernel_addr, new.ramdisk_addr, new.tags_addr),
                         (old.kernel_addr, old.ramdisk_addr, old.tags_addr))
        modes = [word for word in new.cmdline.split() if word.startswith("androidboot.mode=")]
        self.assertEqual(modes, ["androidboot.mode=recovery"])
        before, after = ({e.name: e.data for e in image.entries()} for image in (old, new))
        self.assertEqual(set(before), set(after))
        self.assertEqual([name for name in before if before[name] != after[name]], ["twres/portrait.xml"])
        self.assertIn(b"xploader", after["twres/portrait.xml"])
        self.assertIn(b"TeamWin TWRP", after["twres/portrait.xml"])
        self.assertEqual(result, build(source, hashlib.sha256(source).hexdigest(), len(source)))

    def test_wrong_source_is_rejected(self):
        source = fixture()
        for digest, size in (("0"*64, len(source)), (hashlib.sha256(source).hexdigest(), len(source)+1)):
            with self.subTest(digest=digest, size=size), self.assertRaises(ValueError):
                build(source, digest, size)

    def test_corrupt_existing_generated_cache_is_rejected(self):
        source = fixture()
        meta = dict(sha256=hashlib.sha256(source).hexdigest(), bytes=len(source))
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            original = base / "original.img"
            original.write_bytes(source)
            generated, report = prepare(original, base / "cache", meta)
            self.assertEqual(hashlib.sha256(generated.read_bytes()).hexdigest(), report["image_sha256"])
            generated.write_bytes(b"interrupted or corrupt cache")
            with self.assertRaises(RuntimeError):
                prepare(original, base / "cache", meta)


if __name__ == "__main__":
    unittest.main()
