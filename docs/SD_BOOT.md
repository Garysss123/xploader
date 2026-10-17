# SD payloads

The xploader v1.0 loader reads exFAT and a small `startup.txt` in the selected folder. The supplied examples describe the current profiles; they do not contain kernels or complete OS images.

## Combined Android image

`/lineageos19/startup.txt`:

```ini
mode=bootimg
kernel=boot.img
abi=linux-aarch64
```

Use the **adapted SD boot image**, system/data/cache image files and exact paths provided with that test profile. A stock NAND ROM's boot image ordinarily mounts internal partitions, so changing its location alone does not make it an SD ROM.

## Split components

```ini
mode=split
kernel=Image.gz
initrd=initrd.gz
dtb=board.dtb
abi=linux-aarch64
cmdline=androidboot.hardware=qcom xploader.rootfs=/my-linux/rootfs.img
```

The `cmdline` above is illustrative. The initrd must implement that root image parameter; the loader does not supply a universal Linux root-mount policy. Use each test system's supplied descriptor. Filename conventions such as `zImage`, `initrd.gz` or `startup.txt` do not translate a CPU/boot ABI automatically. Current accepted Linux profiles use AArch64 Image payloads and a compatible Z5 handoff.

Kernel and initrd can remain separate, so an adapter payload can implement a future non-Linux handoff. Direct XNU, UEFI and Windows support is future work.

## Settings and checks

`/xploader.cfg` contains two 512-byte records with generation counters and CRCs. The menu writes settings itself. This settings CRC is distinct from ROM verification: users do **not** register per-ROM SHA256 values before selecting their own SD payloads. Build/download fingerprints are used to identify development artifacts.

Host inspection/creation is available for developers:

```sh
python3 tools/xpl_settings.py inspect xploader.cfg
python3 tools/xpl_settings.py create new-xploader.cfg --mode always --target sd --directory /lineageos19 --language en
```

`create` refuses to replace an existing host file. Prefer the on-device Boot Settings menu for normal use.

## Compatibility boundaries

Bounds, decompression, payload headers and RAM placement are checked. This does not provide arbitrary ARM/ARM64 kernel compatibility. Keep the test profile's folder path unless its initrd/root references have also been adapted. Do not format the source SD from recovery if you want to retain its recoveries and systems. Wi-Fi/modem/camera/graphics acceleration/power support belongs to each OS port.
