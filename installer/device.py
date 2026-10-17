"""Serial-bound original-TWRP transport. Writes use only the native guard."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import time
import urllib.request
import sys

from .layout import DISK_SECTORS

RECOVERY_KERNEL = "3.10.108-gced7acdc4d1"
RECOVERY_VERSION = "3.7.0_9-0"


def sha(data):
    return hashlib.sha256(data).hexdigest()


def frame(raw):
    while raw.startswith(b"libc: Access denied finding property "):
        line, sep, raw = raw.partition(b"\n")
        if not sep or not re.fullmatch(rb'libc: Access denied finding property "[A-Za-z0-9._]+"\r?', line):
            raise RuntimeError("Unknown recovery output framing")
    return raw


def tool(name, supplied=None):
    if supplied:
        path = Path(supplied).resolve()
        if not path.is_file():
            raise RuntimeError("Platform Tool not found: " + str(path))
        return str(path)
    found = shutil.which(name)
    if not found:
        root = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else Path(__file__).resolve().parents[1]
        candidate = root / "platform-tools" / (name + (".exe" if __import__("os").name == "nt" else ""))
        if candidate.is_file():
            return str(candidate)
        raise RuntimeError("Install Android Platform Tools or place its platform-tools folder beside the launcher")
    return found


def run(executable, *args, timeout=30):
    return subprocess.run([str(executable), *map(str, args)], capture_output=True, timeout=timeout)


def devices(executable):
    p = run(executable, "devices", timeout=10)
    if p.returncode:
        raise RuntimeError("Cannot list USB devices: " + p.stderr.decode(errors="replace"))
    return [(parts[0], parts[1]) for line in p.stdout.decode().splitlines()
            if len(parts := line.split()) >= 2 and not line.startswith(("List of", "*"))]


def fetch(url, target, expected_sha256, expected_bytes):
    target = Path(target)
    if target.exists():
        data = target.read_bytes()
        if len(data) == expected_bytes and sha(data) == expected_sha256:
            return target
        raise RuntimeError("Cached download differs: " + str(target))
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + ".part")
    if partial.exists():
        raise RuntimeError("An incomplete download remains: " + str(partial))
    with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "xploader-installer/1.0"}), timeout=45) as response, partial.open("xb") as output:
        total = 0
        while data := response.read(1024*1024):
            total += len(data)
            if total > expected_bytes:
                raise RuntimeError("Download exceeds its pinned size")
            output.write(data)
    data = partial.read_bytes()
    if len(data) != expected_bytes or sha(data) != expected_sha256:
        raise RuntimeError("Download size/hash differs; no flashing performed")
    partial.rename(target)
    return target


class Recovery:
    def __init__(self, adb, serial):
        if not re.fullmatch(r"[A-Za-z0-9_.:-]{1,100}", serial):
            raise ValueError("Invalid device serial")
        self.adb, self.serial = adb, serial

    def call(self, *args, timeout=30):
        return run(self.adb, "-s", self.serial, *args, timeout=timeout)

    def shell(self, command, timeout=20):
        p = self.call("shell", command, timeout=timeout)
        if p.returncode:
            raise RuntimeError("Recovery command failed: " + p.stderr.decode(errors="replace"))
        return frame(p.stdout).decode("utf-8", "strict").strip()

    def binary(self, command, size, timeout=60):
        p = self.call("exec-out", command, timeout=timeout)
        raw = frame(p.stdout)
        if p.returncode or len(raw) != size:
            raise RuntimeError(f"Exact recovery read unavailable: received{len(raw)}, expected{size}")
        return raw

    def read(self, offset, size):
        if offset < 0 or size <= 0 or size > 33554432 or offset+size > DISK_SECTORS*512 or offset % 512 or size % 512:
            raise ValueError("Bounded aligned disk read required")
        unit = 4096 if offset % 4096 == size % 4096 == 0 else 512
        return self.binary(f"dd if=/dev/block/mmcblk0 bs={unit} skip={offset//unit} count={size//unit} 2>/dev/null", size)

    def file_size(self, path):
        if not re.fullmatch(r"/(?:tmp|external_sd)/[A-Za-z0-9/._-]+", path):
            raise ValueError("Owned recovery/SD path required")
        value = self.shell("stat -c %s " + path)
        if not re.fullmatch(r"[0-9]{1,10}", value):
            raise RuntimeError("File size response differs")
        size = int(value)
        if not 0 <= size <= 67108864:
            raise RuntimeError("Unexpected installer file size")
        return size

    def file_hash(self, path):
        if not re.fullmatch(r"/(?:tmp|external_sd)/[A-Za-z0-9/._-]+", path):
            raise ValueError("Owned recovery/SD path required")
        value = self.shell("sha256sum " + path, timeout=45)
        if not re.fullmatch(r"[a-f0-9]{64}  " + re.escape(path), value):
            raise RuntimeError("Recovery file hash response differs")
        return value[:64]

    def push(self, host, remote, expected=None):
        if not re.fullmatch(r"/(?:tmp|external_sd)/[A-Za-z0-9/._-]+", remote):
            raise ValueError("Owned staging path required")
        data = Path(host).read_bytes()
        if expected is not None and sha(data) != expected:
            raise RuntimeError("Host staging source changed")
        p = self.call("push", str(host), remote, timeout=45)
        if p.returncode or self.file_hash(remote) != sha(data):
            raise RuntimeError("Recovery staging readback differs")

    def identity(self):
        if self.shell("getprop ro.twrp.version") != RECOVERY_VERSION or self.shell("uname -r") != RECOVERY_KERNEL or self.shell("id -u") != "0":
            raise RuntimeError("Start the original sumire TWRP3.7 recovery with root ADB")
        values = dict(serial=self.serial, model=self.shell("getprop ro.product.model"),
                      device=self.shell("getprop ro.product.device"),
                      board=self.binary("cat /proc/device-tree/model", len(b"SoMC Sumire-ROW\0")).rstrip(b"\0").decode(),
                      cid=self.shell("cat /sys/class/block/mmcblk0/device/cid"),
                      sectors512=int(self.shell("cat /sys/class/block/mmcblk0/size")),
                      boot_id=self.shell("cat /proc/sys/kernel/random/boot_id"),
                      battery=int(self.shell("cat /sys/class/power_supply/battery/capacity")))
        if (values["device"] != "sumire" or values["model"] not in ("E6653", "Xperia Z5") or
            values["board"] != "SoMC Sumire-ROW" or values["sectors512"] != DISK_SECTORS or
            not re.fullmatch(r"[0-9a-f]{32}", values["cid"]) or values["battery"] < 30):
            raise RuntimeError("Unsupported device/layout or insufficient battery")
        return values

    def sd(self, allow_mount=False):
        mounts = self.shell("cat /proc/mounts").splitlines()
        mounted = [line.split() for line in mounts if len(line.split()) >= 3 and line.split()[1] == "/external_sd"]
        if not mounted:
            if not allow_mount:
                raise RuntimeError("Mount MicroSD in TWRP before a read-only check")
            self.shell("mount /external_sd")
            mounts = self.shell("cat /proc/mounts").splitlines()
            mounted = [line.split() for line in mounts if len(line.split()) >= 3 and line.split()[1] == "/external_sd"]
        if len(mounted) != 1 or mounted[0][0] != "/dev/block/mmcblk1p1" or mounted[0][2] not in ("exfat", "fuseblk"):
            raise RuntimeError("A mounted exFAT MicroSD is required; the installer does not format cards")
        # TWRP's userspace exFAT driver reports fuseblk in /proc/mounts.
        # Identify the actual filesystem on the same partition, rather than
        # treating every FUSE filesystem as an exFAT card.
        sector = self.binary("dd if=/dev/block/mmcblk1p1 bs=512 count=1 2>/dev/null", 512)
        if sector[3:11] != b"EXFAT   " or sector[510:512] != b"\x55\xaa":
            raise RuntimeError("The mounted MicroSD does not have an exFAT boot sector")
        cid = self.shell("cat /sys/class/block/mmcblk1/device/cid")
        if not re.fullmatch(r"[0-9a-f]{32}", cid):
            raise RuntimeError("MicroSD identity unavailable")
        return dict(cid=cid, source=mounted[0][0], filesystem="exfat", mount_driver=mounted[0][2])


class GuardBackend:
    def __init__(self, recovery, cid, job_id, folder, helper, helper_sha):
        if not re.fullmatch(r"[a-f0-9]{12}", job_id):
            raise ValueError("Bounded installer job ID required")
        self.recovery, self.cid, self.folder = recovery, cid, Path(folder)
        self.prefix = "/tmp/xploader-install-" + job_id
        self.helper = self.prefix + "-guard"
        recovery.push(helper, self.helper, helper_sha)
        recovery.shell("chmod 700 " + self.helper)
        p = recovery.call("shell", self.helper + " --check-device " + cid)
        if p.returncode or not json.loads(frame(p.stdout)).get("read_only"):
            raise RuntimeError("Native installer guard rejected the connected device: " + p.stderr.decode(errors="replace"))

    def read(self, offset, size):
        return self.recovery.read(offset, size)

    def write(self, op):
        if op.kind not in ("os", "backup", "primary", "manager"):
            raise ValueError("Unknown write role")
        before = self.folder / ("stage-" + op.kind + "-before.bin")
        after = self.folder / ("stage-" + op.kind + "-after.bin")
        before.write_bytes(op.before)
        after.write_bytes(op.after)
        rb, ra = self.prefix + "-" + op.kind + "-before.bin", self.prefix + "-" + op.kind + "-after.bin"
        self.recovery.push(before, rb)
        self.recovery.push(after, ra)
        p = self.recovery.call("shell", self.helper + " --write " + op.kind + " " + self.cid + " " + rb + " " + ra, timeout=60)
        try:
            status = json.loads(frame(p.stdout))
        except (ValueError, UnicodeError):
            status = {}
        (self.folder / ("guard-" + op.kind + ".json")).write_text(json.dumps(dict(returncode=p.returncode,
            status=status, stderr=p.stderr.decode(errors="replace")), indent=2) + "\n")
        if p.returncode or status.get("accepted") is not True or status.get("exact_readback") is not True:
            raise RuntimeError("Native write guard stopped at " + op.kind + ": " + p.stderr.decode(errors="replace"))
        self.recovery.shell("rm -f " + rb + " " + ra)
        return status
