# xploader v1.0 optional protected SD TWRP

This is an additional recovery-kernel defense for **E6653 with the tested xploader split29/44 layout**. It is optional: ordinary unmodified TWRP name-based ROM flashing also passed the split-layout tests. The guard does not create that layout.

## Get a usable `twrp.img`

Download and extract **`xploader-v1.0-protected-twrp-kit.zip`** from the [v1.0 release](https://github.com/Garysss123/xploader/releases/tag/v1.0). Install Python 3 if necessary.

On Windows, double-click **`build-protected-twrp.cmd`**. On Linux/macOS, or from a terminal:

```sh
python3 tools/make_protected_twrp.py
```

The tool downloads the exact original [TWRP 3.7.0_9-0 sumire image from its publisher](https://github.com/Sony-MSM8994-Dev/OTA/releases/download/archives/twrp-3.7.0_9-0-sumire.img), checks its hash, inserts the provided guard004 kernel and changes only the source SD fstab entry to read-only. It **does not access or flash a phone**. The original TWRP userspace is obtained directly from its publisher rather than bundled in this kit.

For an offline input already downloaded from that publisher:

```sh
python3 tools/make_protected_twrp.py --stock twrp-3.7.0_9-0-sumire.img
```

The resulting **`twrp.img`** and a JSON receipt appear in the extracted kit folder. Existing output files are preserved. A Python/zlib version may produce different compressed bytes; the tool checks the exact kernel and every decompressed userspace entry against the source plus the single documented fstab change.

## Use it from SD

1. Confirm the E6653 has the supported split layout below. This is not a general stock-Z5 recovery release.
2. Keep the original SD `twrp.img` as **`twrp-stock.img`**.
3. Copy the generated image to the SD root as **`twrp.img`**.
4. Choose **TWRP** in xploader. Replacing this SD file does not install a recovery to eMMC.

The protected recovery mounts the SD **read-only**. SD MTP/ADB writes, editing OS folders and saving settings/logs onto that card can therefore fail. Use xploader's PC SD Access, a card reader, or the original recovery when SD writes are needed. Revert by restoring the original file as `/twrp.img`.

## What is protected

- The 32 MiB manager at p29 and early firmware/TA/FOTA/GPT ranges.
- Source SD card writes, discard and write-same paths; SD/combo/unknown MMC cards are treated as read-only.
- Raw MMC/RPMB ioctl routes that could bypass ordinary block I/O checks.
- Normal eMMC OS writes remain possible in the accepted layout, including the separate p44 OS boot partition.

| Required layout property | Value |
| --- | --- |
| eMMC capacity | 61,071,360 sectors of 512 bytes |
| p29 manager | start 244,224; length 65,536 sectors |
| p44 OS boot | start 309,760; length 65,536 sectors |
| p32 FOTAKernel | start 506,368; length 131,072 sectors |

An unexpected layout is refused by the write policy. Do not use this as a stock-layout recovery or on other Z5 models. Do not flash this recovery image over the manager with fastboot.

The exact kernel was tested in the lab: manager/FOTA/SD and raw MMC/RPMB writes were refused, p44 remained writable, a deliberately forbidden installation reported failure, and a normal complete LineageOS 19.1 installation succeeded. Destructive physical SD formatting was **not** tested; discard/write-same policy branches were checked on the host. This is a kernel defense, not protection from every recovery modification, external PC firmware tool, or native loader operation.

## Source, license and rebuilding

The recovery kernel is **GPL-2.0**, with upstream per-file notices retained. [COPYING](COPYING), the four modified source files in `overlay/`, the exact used `kernel.config`, and `manifest.json` are provided here. The complete modified source/configuration archive **`xploader-v1.0-protected-twrp-source.tar.gz`** is supplied in the same release as the binary kit. `guard004` is the internal source/build identifier.

Upstream kernel: [Sony-MSM8994-Dev/android_kernel_sony_msm8994 at ced7acdc4d1bf7af8343b2aa715a1770cca961c1](https://github.com/Sony-MSM8994-Dev/android_kernel_sony_msm8994/tree/ced7acdc4d1bf7af8343b2aa715a1770cca961c1). The lab used the Android AArch64 GCC 4.9 toolchain (20150123). With the full source archive extracted, an appropriate Linux host and that compiler prefix:

```sh
mkdir kernel-out
cp recovery/guard004/kernel.config kernel-out/.config
make -C xploader-guard004-kernel O="$PWD/kernel-out" ARCH=arm64 CROSS_COMPILE=/path/to/aarch64-linux-android-4.9/bin/aarch64-linux-android- HOSTCFLAGS="-O2 -fcommon" olddefconfig
make -C xploader-guard004-kernel O="$PWD/kernel-out" ARCH=arm64 CROSS_COMPILE=/path/to/aarch64-linux-android-4.9/bin/aarch64-linux-android- HOSTCFLAGS="-O2 -fcommon" LOCALVERSION=-xpl-guard004 -j4 Image
```

The host image-generation tool is MIT licensed. The unchanged original recovery programs retain their upstream licenses and sources; [TeamWin recovery source](https://github.com/TeamWin/android_bootable_recovery/tree/android-9.0) and the original publisher's recovery/device trees are separate from xploader's native core license.
