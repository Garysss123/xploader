"""Build the RAM-only installer environment from the pinned publisher image.

The Linux kernel, DTBs, executables and init scripts stay byte-identical. Only
the recovery boot-mode header and installer GUI pages are adapted locally.
The original publisher image remains the SD recovery source.
"""
import copy
import gzip
import hashlib
from pathlib import Path
import re
import struct
import xml.etree.ElementTree as ET

from tools.xpl_image import BootImage, align, pack_newc


def sha(data):
    return hashlib.sha256(data).hexdigest()


def installer_page(name, actions=""):
    lines = [
        (210, "font_l", "#ffffffff", "xploader"),
        (300, "font_m", "#25b8ffff", "Installer v1.0"),
        (570, "font_m", "#ffffffff", "Setting up the boot manager"),
        (680, "font_s", "#d3deebff", "Follow the progress on your PC."),
        (750, "font_s", "#d3deebff", "Keep the USB cable connected."),
        (1030, "font_s", "#d3deebff", "This temporary session is automatic."),
        (1700, "font_s", "#8a9fb8ff", "xploader installation environment"),
        (1760, "font_s", "#8a9fb8ff", "Based on TeamWin TWRP"),
    ]
    body = ['<fill color="#08111eff"><placement x="0" y="0" w="1080" h="1920"/></fill>',
            '<fill color="#25b8ffff"><placement x="80" y="430" w="920" h="5"/></fill>']
    for y, font, color, value in lines:
        body.append(f'<text><placement x="80" y="{y}"/><font resource="{font}" color="{color}"/>'
                    f'<text>{value}</text></text>')
    return f'<page name="{name}">\n' + "\n".join(body) + "\n" + actions + "\n</page>"


def installer_theme(data):
    tree = ET.fromstring(data)
    single = tree.find(".//page[@name='singleaction_page']")
    if single is None or len(single.findall("action")) != 3:
        raise ValueError("Unsupported recovery CLI page structure")
    actions = "\n".join(ET.tostring(a, encoding="unicode") for a in single.findall("action"))
    readonly = ('<action><actions><action function="mountsystemtoggle">1</action>'
                '<action function="set">tw_page_done=1</action>'
                '<action function="page">%tw_back%</action></actions></action>')
    cancel_decrypt = '<action><action function="page">canceldecrypt</action></action>'
    text = data.decode("utf-8")
    for name, logic in (("main2", ""), ("singleaction_page", actions),
                        ("system_readonly", readonly), ("decrypt", cancel_decrypt), ("lock", "")):
        text, count = re.subn(r'<page name="'+name+r'">.*?</page>',
                             lambda _: installer_page(name, logic), text, count=1, flags=re.S)
        if count != 1:
            raise ValueError("Required recovery page missing: " + name)
    after = ET.fromstring(text)
    def action_xml(action):
        return ET.canonicalize(ET.tostring(action, encoding="unicode"), strip_text=True)
    old_actions = [action_xml(a) for a in single.findall("action")]
    new_actions = [action_xml(a) for a in after.find(".//page[@name='singleaction_page']").findall("action")]
    if old_actions != new_actions:
        raise AssertionError("Recovery CLI action handlers changed")
    return text.encode("utf-8")


def build(source, expected_sha256, expected_bytes):
    if len(source) != expected_bytes or sha(source) != expected_sha256:
        raise ValueError("Pinned original recovery image required")
    original = BootImage(source)
    if original.second or original.tail or original.page != 4096:
        raise ValueError("Unsupported recovery boot envelope")
    entries = original.entries()
    before = {entry.name: entry.data for entry in entries}
    modified = copy.deepcopy(entries)
    for entry in modified:
        if entry.name == "twres/portrait.xml":
            entry.data = installer_theme(entry.data)
    after = {entry.name: entry.data for entry in modified}
    if set(before) != set(after) or [k for k in before if before[k] != after[k]] != ["twres/portrait.xml"]:
        raise AssertionError("Unexpected installer ramdisk change")
    ramdisk = bytearray(gzip.compress(pack_newc(modified), compresslevel=9, mtime=0))
    ramdisk[9] = 255  # stable gzip OS byte across supported host Python builds
    ramdisk = bytes(ramdisk)
    words = [w for w in original.cmdline.split() if w.partition("=")[0] != "androidboot.mode"]
    command = (" ".join(words) + " androidboot.mode=recovery").encode("ascii")
    if len(command) >= 1536:
        raise ValueError("Installer command line exceeds boot header")
    header = bytearray(source[:original.page])
    struct.pack_into("<I", header, 16, len(ramdisk))
    header[64:576] = bytes(512)
    header[608:1632] = bytes(1024)
    header[64:64+min(512, len(command))] = command[:512]
    if len(command) > 512:
        header[608:608+len(command)-512] = command[512:]
    ident = hashlib.sha1()
    for part in (original.kernel, ramdisk, original.second):
        ident.update(part)
        ident.update(struct.pack("<I", len(part)))
    header[576:608] = ident.digest() + bytes(12)
    image = bytes(header)
    for part in (original.kernel, ramdisk, original.second):
        image += part + bytes(align(len(part), original.page)-len(part))
    result = BootImage(image)
    if result.kernel != original.kernel or result.kernel_addr != original.kernel_addr:
        raise AssertionError("Original recovery kernel changed")
    if result.ramdisk_addr != original.ramdisk_addr or result.tags_addr != original.tags_addr:
        raise AssertionError("Recovery load addresses changed")
    observed = {e.name: e.data for e in result.entries()}
    if observed != after:
        raise AssertionError("Installer ramdisk repack differs")
    return image


def prepare(source_path, cache, meta):
    source = Path(source_path).read_bytes()
    image = build(source, meta["sha256"], meta["bytes"])
    fingerprint = sha(image)
    target = Path(cache) / ("xploader-v1.0-installer-" + fingerprint[:16] + ".img")
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists():
        if target.read_bytes() != image:
            raise RuntimeError("Cached temporary installer image differs")
    else:
        with target.open("xb") as stream:
            stream.write(image)
        if target.read_bytes() != image:
            raise RuntimeError("Temporary installer image readback differs")
    return target, dict(image_sha256=fingerprint, image_bytes=len(image),
                        source_sha256=meta["sha256"], kernel_unchanged=True,
                        only_ramdisk_change="twres/portrait.xml", boot_mode="recovery")
