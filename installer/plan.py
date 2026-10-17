"""Pure installation planning and replayable, before-byte-checked operations."""
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path

from .boot import preserve_os_boot
from .layout import inspect, plan_split, BOOT_START, OS_START, HALF, DISK_SECTORS

HALF_BYTES = HALF * 512


def sha(data):
    return hashlib.sha256(data).hexdigest()


@dataclass(frozen=True)
class Operation:
    kind: str
    offset: int
    before: bytes
    after: bytes

    def summary(self):
        return dict(kind=self.kind, first_sector=self.offset//512, bytes=len(self.after),
                    before_sha256=sha(self.before), after_sha256=sha(self.after))


@dataclass(frozen=True)
class Plan:
    mode: str
    original: object
    target: object
    operations: tuple
    report: dict


def prepare(primary, backup, manager_region, os_region, manager_image, manager_sha256):
    if sha(manager_image) != manager_sha256 or not manager_image.startswith(b"ANDROID!") or len(manager_image) % 4096:
        raise ValueError("Verified release manager image required")
    if not 4096 <= len(manager_image) <= HALF_BYTES or len(manager_region) != HALF_BYTES or len(os_region) != HALF_BYTES:
        raise ValueError("Two exact32MiB live boot windows required")
    original = inspect(primary, backup)
    target = plan_split(original)
    operations = []
    os_after = os_region
    conversion = dict(source_format="Existing split OS slot; untouched")
    if original.state == "stock":
        os_image, conversion = preserve_os_boot(manager_region)
        operations.append(Operation("os", OS_START*512, os_region[:len(os_image)], os_image))
        os_after = os_image + os_region[len(os_image):]
        # Preserve a bootable original first32MiB until both GPT mirrors are
        # complete. The native manager prefix is always the final disk write.
        operations += [Operation("backup", (DISK_SECTORS-33)*512, original.backup, target.backup),
                       Operation("primary", 0, original.primary, target.primary)]
    operations.append(Operation("manager", BOOT_START*512, manager_region[:len(manager_image)], manager_image))
    manager_after = manager_image + manager_region[len(manager_image):]
    report = dict(mode="first-install" if original.state == "stock" else "update", original=original.summary(), target=target.summary(),
                  manager_release_sha256=manager_sha256, OS_boot=conversion,
                  manager_region_before_sha256=sha(manager_region), manager_region_after_sha256=sha(manager_after),
                  OS_region_before_sha256=sha(os_region), OS_region_after_sha256=sha(os_after),
                  GPT_changed=original.state == "stock", SD_payload_changed=False,
                  writes=[op.summary() for op in operations], bootable_on_hardware_verified=False)
    return Plan(report["mode"], original, target, tuple(operations), report)


def store(plan, folder, identity):
    folder = Path(folder)
    folder.mkdir(exist_ok=False)
    manifest = dict(version=1, identity=identity, **plan.report)
    files = []
    for index, op in enumerate(plan.operations):
        name = f"{index:02}-{op.kind}"
        before, after = name + "-before.bin", name + "-after.bin"
        (folder / before).write_bytes(op.before)
        (folder / after).write_bytes(op.after)
        files.append(dict(**op.summary(), before=before, after=after))
    manifest["operations"] = files
    manifest["original_GPT"] = {"primary": "original-primary.gpt", "backup": "original-backup.gpt"}
    (folder / "original-primary.gpt").write_bytes(plan.original.primary)
    (folder / "original-backup.gpt").write_bytes(plan.original.backup)
    (folder / "target-primary.gpt").write_bytes(plan.target.primary)
    (folder / "target-backup.gpt").write_bytes(plan.target.backup)
    (folder / "plan.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def load(folder):
    folder = Path(folder).resolve()
    meta = json.loads((folder / "plan.json").read_text())
    if meta.get("version") != 1:
        raise ValueError("Unsupported installer job")
    original = inspect((folder / "original-primary.gpt").read_bytes(), (folder / "original-backup.gpt").read_bytes())
    target = inspect((folder / "target-primary.gpt").read_bytes(), (folder / "target-backup.gpt").read_bytes())
    if original.summary() != meta["original"] or target.summary() != meta["target"]:
        raise ValueError("Job GPT snapshot differs")
    ops = []
    allowed = {"os": OS_START*512, "manager": BOOT_START*512, "primary": 0, "backup": (DISK_SECTORS-33)*512}
    expected_order = ["os", "backup", "primary", "manager"] if meta["mode"] == "first-install" else ["manager"] if meta["mode"] == "update" else []
    if not expected_order or target.state != "split" or (meta["mode"] == "first-install") != (original.state == "stock"):
        raise ValueError("Job layout transition differs")
    if [item["kind"] for item in meta["operations"]] != expected_order:
        raise ValueError("Job write ordering differs")
    for item in meta["operations"]:
        files = []
        for field in ("before", "after"):
            name = item[field]
            if Path(name).name != name:
                raise ValueError("Bounded job filenames required")
            data = (folder / name).read_bytes()
            if sha(data) != item[field + "_sha256"] or len(data) != item["bytes"]:
                raise ValueError("Job source snapshot changed")
            files.append(data)
        offset = item["first_sector"] * 512
        if offset != allowed.get(item["kind"]) or not 0 < len(files[0]) <= HALF_BYTES or len(files[0]) % 512:
            raise ValueError("Job write range differs")
        ops.append(Operation(item["kind"], offset, *files))
    return Plan(meta["mode"], original, target, tuple(ops), meta)


def validate_resume(plan, manager_region, os_region, primary, backup):
    if len(manager_region) != HALF_BYTES or len(os_region) != HALF_BYTES:
        raise ValueError("Complete live boot windows required for resume")
    for label, data in (("manager", manager_region), ("OS", os_region)):
        if sha(data) not in (plan.report[label + "_region_before_sha256"], plan.report[label + "_region_after_sha256"]):
            raise ValueError("Live " + label + " boot window is neither the saved before nor after state")
    if primary not in (plan.original.primary, plan.target.primary) or backup not in (plan.original.backup, plan.target.backup):
        raise ValueError("Live GPT contains an unknown/interrupted partial state")
    # Do not treat unrelated edits made after an old job as installer recovery.
    # Each operation is checked again immediately before its bounded write.


def execute(plan, backend, event):
    """backend.read/write own the real device binding; all writes are typed."""
    for index, op in enumerate(plan.operations):
        observed = backend.read(op.offset, len(op.after))
        if observed == op.after:
            event(dict(index=index, kind=op.kind, state="already-equal", exact_readback=True))
            continue
        if observed != op.before:
            raise RuntimeError("Live before bytes differ for " + op.kind + "; stopped without that write")
        event(dict(index=index, state="write-intent", **op.summary()))
        result = backend.write(op)
        if not result.get("accepted") or not result.get("exact_readback"):
            raise RuntimeError("Guard did not confirm " + op.kind + "; inspect the preserved job")
        if backend.read(op.offset, len(op.after)) != op.after:
            raise RuntimeError("Independent readback differs for " + op.kind)
        event(dict(index=index, kind=op.kind, state="verified", guard=result, exact_readback=True))
