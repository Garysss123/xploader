"""Exercise the transport's filesystem identity check without device writes."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from installer.device import Recovery


class Card(Recovery):
    def __init__(self, driver="fuseblk", source="/dev/block/mmcblk1p1", exfat=True):
        self.driver, self.source, self.exfat = driver, source, exfat
        self.calls = []

    def shell(self, command, timeout=20):
        self.calls.append(command)
        if command == "cat /proc/mounts":
            return f"{self.source} /external_sd {self.driver} rw 0 0"
        if command == "cat /sys/class/block/mmcblk1/device/cid":
            return "0123456789abcdef" * 2
        raise AssertionError("Unexpected command: " + command)

    def binary(self, command, size, timeout=60):
        self.calls.append(command)
        assert command == "dd if=/dev/block/mmcblk1p1 bs=512 count=1 2>/dev/null" and size == 512
        sector = bytearray(512)
        sector[3:11] = b"EXFAT   " if self.exfat else b"NTFS    "
        sector[510:] = b"\x55\xaa"
        return bytes(sector)


class TransportTests(unittest.TestCase):
    def test_kernel_and_userspace_exfat(self):
        for driver in ("exfat", "fuseblk"):
            with self.subTest(driver=driver):
                card = Card(driver)
                self.assertEqual(card.sd()["mount_driver"], driver)
                self.assertFalse(any("of=" in command or command.startswith("mount ") for command in card.calls))

    def test_other_fuse_filesystem_rejected(self):
        with self.assertRaises(RuntimeError):
            Card(exfat=False).sd()

    def test_wrong_mount_source_or_driver_rejected(self):
        for options in ({"source": "/dev/block/mmcblk0p43"}, {"driver": "ext4"}):
            with self.subTest(options=options), self.assertRaises(RuntimeError):
                Card(**options).sd()


if __name__ == "__main__":
    unittest.main()
