"""Simple E6653 installer: check, preserve OS, install, verify in SDTWRP.

The Windows launcher uses this same entry point. --check is read-only and does
not reboot. Installation starts only after the local plan is saved and --apply
is requested. A new phone needs an unlocked bootloader and an exFAT MicroSD.
"""
import argparse
import datetime
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
import time
import uuid

RESOURCES = Path(sys._MEIPASS) if getattr(sys, "frozen", False) else Path(__file__).resolve().parents[1]
ROOT = Path(sys.executable).resolve().parent if getattr(sys, "frozen", False) else RESOURCES
sys.path.insert(0, str(RESOURCES))
from installer.device import Recovery, GuardBackend, tool, devices, run, fetch, frame
from installer.layout import inspect, BOOT_START, OS_START, DISK_SECTORS, PRIMARY_BYTES, BACKUP_BYTES
from installer.plan import prepare, store, load, execute, validate_resume, HALF_BYTES
from tools.xpl_settings import choose, encode
from installer.bootstrap import ensure_platform_tools


def sha(data):
    return hashlib.sha256(data).hexdigest()


def note(message):
    print(message, flush=True)


def release():
    assets = RESOURCES / "installer/assets"
    meta = json.loads((assets / "release.json").read_text())
    for key in ("manager", "guard"):
        row = meta[key]
        if Path(row["file"]).name != row["file"]:
            raise ValueError("Bounded release filenames required")
        data = (assets / row["file"]).read_bytes()
        if sha(data) != row["sha256"] or len(data) != row["bytes"]:
            raise RuntimeError("Release asset differs: " + key)
    return meta, assets


def get_recovery(meta):
    row = meta["recovery"]
    note("Preparing the original TWRP recovery from its publisher...")
    return fetch(row["url"], ROOT / "installer/cache/twrp-3.7.0_9-0-sumire.img", row["sha256"], row["bytes"])


def connected(adb, fastboot, serial, meta, check):
    adb_rows, fb_rows = devices(adb), devices(fastboot)
    candidates = {s for s, state in adb_rows if state in ("device", "recovery")}
    candidates |= {s for s, state in fb_rows if state == "fastboot"}
    if serial:
        if serial not in candidates:
            raise RuntimeError("Selected phone is not available. Connect it in TWRP or blue-light fastboot")
    elif len(candidates) != 1:
        raise RuntimeError("Connect one E6653, or select its serial explicitly")
    else:
        serial = candidates.pop()
    recovery = Recovery(adb, serial)
    try:
        recovery.identity()
        return recovery
    except (RuntimeError, ValueError):
        if check:
            raise RuntimeError("Read-only check requires the original TWRP3.7 with root ADB")
    if serial in {s for s, state in adb_rows if state == "device"}:
        if recovery.shell("getprop ro.product.model") not in ("E6653", "Xperia Z5"):
            raise RuntimeError("Connected Android model is unsupported")
        note("Restarting the phone to Sony fastboot...")
        p = recovery.call("reboot", "bootloader", timeout=15)
        if p.returncode:
            raise RuntimeError("Bootloader restart was not acknowledged")
        end = time.monotonic() + 40
        while serial not in {s for s, state in devices(fastboot) if state == "fastboot"}:
            if time.monotonic() > end:
                raise RuntimeError("Enter blue-light fastboot: power off, hold Volume Up and connect USB")
            time.sleep(1)
    identity = {}
    for key in ("product", "version-bootloader", "unlocked"):
        p = run(fastboot, "-s", serial, "getvar", key, timeout=10)
        identity[key] = (p.stdout+p.stderr).decode(errors="replace")
    if re.search(r"(?mi)^unlocked:\s*(?:no|false)\s*$", identity["unlocked"]):
        raise RuntimeError("Unlock the bootloader before installing xploader")
    if not re.search(r"(?m)^version-bootloader: 1289-5620 S1_Boot_MSM8994_LA1\.2\.3_(?:45|47)\s*$", identity["version-bootloader"]):
        raise RuntimeError("This Sony S1 firmware has not been accepted for this E6653 installer")
    product = identity["product"]
    if not (re.search(r"(?m)^product: E6653\s*$", product) or
            re.search(r"(?m)^product: Unknown: Sep 20 2017/12:00:48\s*$", product)):
        raise RuntimeError("Fastboot product identity is unsupported")
    image = get_recovery(meta)
    note("Starting temporary TWRP. No partition has been flashed...")
    p = run(fastboot, "-s", serial, "boot", str(image), timeout=45)
    if p.returncode:
        raise RuntimeError("Temporary recovery boot failed: " + (p.stdout+p.stderr).decode(errors="replace"))
    end = time.monotonic() + 180
    while True:
        try:
            recovery.identity()
            return recovery
        except (RuntimeError, ValueError):
            if time.monotonic() > end:
                raise RuntimeError("Recovery did not appear. No partition was flashed; a physical reset may be needed")
            time.sleep(3)


def read_windows(recovery):
    return (recovery.read(0, PRIMARY_BYTES), recovery.read((DISK_SECTORS-33)*512, BACKUP_BYTES),
            recovery.read(BOOT_START*512, HALF_BYTES), recovery.read(OS_START*512, HALF_BYTES))


def save_state(folder, state):
    target = Path(folder) / "state.json"
    target.write_text(json.dumps(state, indent=2) + "\n")


def set_settings(recovery, folder, mode, target, directory, state):
    if recovery.sd()["cid"] != state["sd"]["cid"]:
        raise RuntimeError("MicroSD changed during installation")
    exists = recovery.shell("[ -f /external_sd/xploader.cfg ] && echo YES || echo NO") == "YES"
    blob = recovery.binary("cat /external_sd/xploader.cfg", 1024) if exists else bytes(1024)
    selected = choose(blob) if exists else None
    if exists and selected is None:
        raise RuntimeError("Existing xploader.cfg is unreadable; no settings were overwritten")
    old, active = selected if selected else (dict(sequence=0), 1)
    sequence = old["sequence"] + 1
    data = encode(mode, target, directory, sequence, old.get("language", ""))
    after = bytearray(blob)
    after[(1-active)*512:(2-active)*512] = data
    local = Path(folder) / ("settings-" + str(sequence) + ".bin")
    local.write_bytes(data if exists else bytes(after))
    remote = "/tmp/xploader-install-" + state["job_id"] + "-cfg.bin"
    recovery.push(local, remote)
    if exists:
        recovery.shell("/sbin/toybox dd if=" + remote + " of=/external_sd/xploader.cfg bs=512 seek=" + str(1-active) + " count=1 conv=notrunc && sync")
    else:
        recovery.shell("cp " + remote + " /external_sd/xploader.cfg && sync")
    observed = recovery.binary("cat /external_sd/xploader.cfg", 1024)
    if observed != bytes(after):
        raise RuntimeError("SD settings readback differs")
    return choose(observed)[0]


def prepare_sd(recovery, folder, meta, state):
    state["sd"] = recovery.sd()
    exists = recovery.shell("[ -f /external_sd/xploader.cfg ] && echo YES || echo NO") == "YES"
    if exists:
        original = recovery.binary("cat /external_sd/xploader.cfg", 1024)
        selected = choose(original)
        if not selected:
            raise RuntimeError("Existing SD settings are invalid")
        (Path(folder) / "settings-original.bin").write_bytes(original)
        state["operator_settings"] = selected[0]
    else:
        state["operator_settings"] = dict(startup=0, target=0, directory="/")
    stock = get_recovery(meta)
    row = meta["recovery"]
    exists = recovery.shell("[ -f /external_sd/twrp.img ] && echo YES || echo NO") == "YES"
    if exists and recovery.file_hash("/external_sd/twrp.img") != row["sha256"]:
        state["original_twrp_backup"] = "/external_sd/xploader-installer-original-" + state["job_id"] + ".img"
        state["original_twrp_sha256"] = recovery.file_hash("/external_sd/twrp.img")
    save_state(folder, state)
    if not exists or state.get("original_twrp_backup"):
        staged = "/external_sd/xploader-installer-new-" + state["job_id"] + ".img"
        recovery.push(stock, staged, row["sha256"])
        if exists:
            if recovery.shell("[ ! -e " + state["original_twrp_backup"] + " ] && echo FREE") != "FREE":
                raise RuntimeError("Recovery backup path already exists")
            recovery.shell("mv /external_sd/twrp.img " + state["original_twrp_backup"])
        recovery.shell("mv " + staged + " /external_sd/twrp.img && sync")
        if recovery.file_hash("/external_sd/twrp.img") != row["sha256"]:
            raise RuntimeError("Standard SD recovery readback differs")
    state["temporary_settings"] = set_settings(recovery, folder, 1, 2, state["operator_settings"]["directory"], state)
    state["SD_prepared"] = True
    save_state(folder, state)


def verify_recovery(recovery, plan, meta, before_id, folder, state):
    note("Starting xploader and SD TWRP. This can take about two minutes...")
    # Use recovery's System path so it completes its unmount/shutdown work.
    # A bare adbd reboot failed the first SD handoff on the test phone.
    p = recovery.call("shell", "twrp reboot system", timeout=15)
    if p.returncode:
        raise RuntimeError("Recovery System restart was not acknowledged; saved job remains available")
    end = time.monotonic() + 240
    while True:
        try:
            identity = recovery.identity()
            if identity["boot_id"] != before_id:
                break
        except (RuntimeError, ValueError, __import__("subprocess").TimeoutExpired):
            pass
        if time.monotonic() > end:
            raise RuntimeError("New SD recovery did not appear; stop and keep the saved job for recovery")
        time.sleep(3)
    if identity["cid"] != state["identity"]["cid"] or recovery.sd()["cid"] != state["sd"]["cid"]:
        raise RuntimeError("Device/card identity changed after restart")
    raw = recovery.binary("cat /proc/device-tree/chosen/xploader,menu-proof", 4096)
    if not raw.startswith(b"XPLMENU-001\n"):
        raise RuntimeError("Fresh native proof is missing")
    fields = {}
    for line in raw.rstrip(b"\0").decode("ascii").splitlines()[1:]:
        key, value = line.split("=", 1)
        if key in fields:
            raise RuntimeError("Duplicate native proof key")
        fields[key] = value if key == "package" else int(value, 16)
    wanted = dict(package=meta["package_id"], stage=7, result=0, proof_overflow=0, boot_target=2,
                  kernel_replaced=1, countdown_completed=3, sd_ram_bytes=meta["recovery"]["bytes"],
                  sd_ram_fingerprint=int(meta["recovery"]["image_fnv64"], 16),
                  sd_raw_fingerprint=int(meta["recovery"]["kernel_raw_fnv64"], 16))
    if any(fields.get(k) != v for k, v in wanted.items()):
        raise RuntimeError("New xploader/recovery source proof differs")
    guard = recovery.binary("cat /proc/device-tree/chosen/xploader,entry-watchdog", 64)
    words = struct.unpack_from("<14I", guard, 8)
    if guard[:8] != b"XPLWDT01" or words[:7] != (1, 64, 4, 0, 20, 0, 0):
        raise RuntimeError("Native entry watchdog restoration proof differs")
    primary, backup, manager, os_boot = read_windows(recovery)
    if (primary != plan.target.primary or backup != plan.target.backup or
        sha(manager) != plan.report["manager_region_after_sha256"] or sha(os_boot) != plan.report["OS_region_after_sha256"]):
        raise RuntimeError("Installed layout/boot readback differs")
    (Path(folder) / "native-proof.bin").write_bytes(raw)
    (Path(folder) / "entry-watchdog.bin").write_bytes(guard)
    state.update(after_identity=identity, source_bound_SD_TWRP_verified=True, installed_bytes_verified=True,
                 GPT_verified=True, OS_boot_preserved_or_converted=True, fields=fields)
    operator = state["operator_settings"]
    state["saved_settings"] = set_settings(recovery, folder, 0, operator["target"], operator["directory"], state)
    if state.get("original_twrp_backup"):
        if recovery.file_hash(state["original_twrp_backup"]) != state["original_twrp_sha256"]:
            raise RuntimeError("Original SD recovery backup differs")
        if recovery.file_hash("/external_sd/twrp.img") != meta["recovery"]["sha256"]:
            raise RuntimeError("Temporary recovery changed; refusing replacement")
        recovery.shell("rm -f /external_sd/twrp.img && mv " + state["original_twrp_backup"] + " /external_sd/twrp.img && sync")
        state["original_twrp_restored"] = recovery.file_hash("/external_sd/twrp.img") == state["original_twrp_sha256"]
        if not state["original_twrp_restored"]:
            raise RuntimeError("Original SD recovery restoration differs")
    recovery.shell("twrp set tw_screen_timeout_secs 60")
    recovery.shell("twrp set tw_no_screen_timeout 0")
    end = time.monotonic() + 85
    while recovery.shell("cat /sys/class/leds/lcd-backlight/brightness") != "0":
        if time.monotonic() > end:
            raise RuntimeError("Installation verified; recovery screen-off is still pending")
        time.sleep(3)
    state.pop("error", None)
    state.update(accepted=True, AlwaysMenu_restored=True, screen_off_observed=True,
                 stock_Android_UI_tested=False, cold_power_cycle_tested=False, ordinary_ROM_flash_tested=False)
    save_state(folder, state)


def main():
    if getattr(sys, "frozen", False) and len(sys.argv) == 1:
        sys.argv.append("--apply")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Read-only check from root TWRP; no restart")
    parser.add_argument("--apply", action="store_true", help="Install/update after preserving the boot image")
    parser.add_argument("--yes", action="store_true", help="Use the caller's existing installation authorization")
    parser.add_argument("--serial")
    parser.add_argument("--adb")
    parser.add_argument("--fastboot")
    parser.add_argument("--resume", type=Path)
    args = parser.parse_args()
    if args.check == args.apply:
        parser.error("Choose --check or --apply")
    meta, assets = release()
    if not args.adb and not args.fastboot:
        try:
            adb, fastboot = tool("adb"), tool("fastboot")
        except RuntimeError:
            note("Preparing Android Platform Tools from Google's pinned download...")
            ensure_platform_tools(ROOT, meta)
            adb, fastboot = tool("adb"), tool("fastboot")
    else:
        adb, fastboot = tool("adb", args.adb), tool("fastboot", args.fastboot)
    note("xploader " + meta["release"] + " E6653 installer - unlocked Xperia Z5 only")
    recovery = connected(adb, fastboot, args.serial, meta, args.check)
    identity = recovery.identity()
    note("Connected: " + identity["model"] + " / " + identity["serial"])
    primary, backup, manager_region, os_region = read_windows(recovery)
    sd = recovery.sd(allow_mount=args.apply)
    root = ROOT / "installer/work"
    root.mkdir(parents=True, exist_ok=True)
    if args.resume:
        folder = args.resume.resolve()
        plan = load(folder)
        state = json.loads((folder / "state.json").read_text())
        if (state["identity"]["serial"], state["identity"]["cid"], state["sd"]["cid"]) != (identity["serial"], identity["cid"], sd["cid"]):
            raise RuntimeError("Saved installer job belongs to another phone/card")
        validate_resume(plan, manager_region, os_region, primary, backup)
    else:
        job_id = uuid.uuid4().hex[:12]
        folder = root / (datetime.datetime.now().strftime("%Y%m%d-%H%M%S-") + job_id)
        plan = prepare(primary, backup, manager_region, os_region,
                       (assets / meta["manager"]["file"]).read_bytes(), meta["manager"]["sha256"])
        store(plan, folder, identity)
        state = dict(job_id=job_id, identity=identity, sd=sd, mode=plan.mode, accepted=False,
                     partition_write_attempted=False, phone_partitions_written=False, source="simple installer package")
        save_state(folder, state)
    note("Plan: " + ("First installation; preserve the eMMC OS boot" if plan.mode == "first-install" else "Update xploader; keep the eMMC OS boot"))
    note("Saved recovery record: " + str(folder))
    if args.check:
        note("CHECK PASSED. No partition or SD file was written; no restart requested.")
        return
    if not args.yes and input("Type INSTALL to continue: ").strip() != "INSTALL":
        note("Cancelled before any write.")
        return
    def event(row):
        with (folder / "events.jsonl").open("a", encoding="utf-8") as output:
            output.write(json.dumps(dict(time_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(), **row)) + "\n")
        if row["state"] == "verified":
            state["phone_partitions_written"] = True
            note("Verified: " + row["kind"])
        save_state(folder, state)
    try:
        note("Preparing SD recovery and automatic verification...")
        if not state.get("SD_prepared"):
            prepare_sd(recovery, folder, meta, state)
        # User confirmation or recovery setup may take time. Recheck complete
        # windows, not only the soon-to-be-written prefixes, before disk I/O.
        live_primary, live_backup, live_manager, live_os = read_windows(recovery)
        validate_resume(plan, live_manager, live_os, live_primary, live_backup)
        backend = GuardBackend(recovery, identity["cid"], state["job_id"], folder,
                               assets / meta["guard"]["file"], meta["guard"]["sha256"])
        state["partition_write_attempted"] = True
        save_state(folder, state)
        execute(plan, backend, event)
        state["disk_install_verified"] = True
        save_state(folder, state)
        recovery.shell("sync")
        verify_recovery(recovery, plan, meta, identity["boot_id"], folder, state)
        note("SUCCESS. xploader installed; Always Menu saved; phone is in SD TWRP with the screen off.")
        note("Keep the saved recovery record. Stock Android/cold-boot/ROM-flash tests are separate.")
    except Exception as error:
        state["error"] = str(error)
        save_state(folder, state)
        raise RuntimeError(str(error) + "\nSaved record: " + str(folder)) from error


if __name__ == "__main__":
    interactive = bool(getattr(sys, "frozen", False) and len(sys.argv) == 1)
    status = 0
    try:
        main()
    except Exception as error:
        print("STOPPED: " + str(error), file=sys.stderr, flush=True)
        status = 1
    if interactive:
        input("Press Enter to close...")
    raise SystemExit(status)
