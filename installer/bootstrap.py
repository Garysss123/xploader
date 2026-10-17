"""Download pinned Google Platform Tools locally; no phone or driver writes."""
import os
from pathlib import Path, PurePosixPath
import shutil
import uuid
import zipfile
from .device import fetch


def ensure_platform_tools(root, meta):
    if os.name != "nt":
        raise RuntimeError("Install adb and fastboot with the host package manager")
    root = Path(root).resolve()
    target = root / "platform-tools"
    if target.exists():
        raise RuntimeError("An incomplete platform-tools folder exists beside the installer; inspect it before retrying")
    row = meta["platform_tools"]
    cache = root / "installer/cache"
    archive = fetch(row["url"], cache / "platform-tools_r37.0.1-win.zip", row["sha256"], row["bytes"])
    staging = cache / ("platform-tools-unpack-" + uuid.uuid4().hex)
    staging.mkdir()
    with zipfile.ZipFile(archive) as pack:
        total = 0
        members = pack.infolist()
        if len(members) > 300:
            raise RuntimeError("Unexpected Platform Tools archive member count")
        for item in members:
            path = PurePosixPath(item.filename)
            total += item.file_size
            if (path.is_absolute() or not path.parts or path.parts[0] != "platform-tools" or
                any(p in ("..", ".") or ":" in p or "\\" in p for p in path.parts) or
                (item.external_attr >> 16) & 0o170000 == 0o120000 or total > 100*1024*1024):
                raise RuntimeError("Unexpected Platform Tools archive path/type/size")
            local = staging.joinpath(*path.parts)
            if not local.resolve().is_relative_to(staging.resolve()):
                raise RuntimeError("Archive destination escapes its staging folder")
            if item.is_dir():
                local.mkdir(parents=True, exist_ok=True)
            else:
                local.parent.mkdir(parents=True, exist_ok=True)
                with pack.open(item) as source, local.open("xb") as output:
                    shutil.copyfileobj(source, output)
    source = staging / "platform-tools"
    for name in ("adb.exe", "fastboot.exe", "AdbWinApi.dll", "AdbWinUsbApi.dll", "NOTICE.txt", "source.properties"):
        if not (source / name).is_file():
            raise RuntimeError("Incomplete official Platform Tools archive")
    if "Pkg.Revision=37.0.1" not in (source / "source.properties").read_text().replace(" ", ""):
        raise RuntimeError("Platform Tools revision differs")
    # Both resolved targets remain inside the extracted portable application.
    if not source.resolve().is_relative_to(root) or not target.resolve().is_relative_to(root):
        raise RuntimeError("Platform Tools destination escapes application folder")
    source.rename(target)
    return target
