# Installing xploader

## Current package status

This is the **native074 developer source preview**. `releases/xploader-native074.bin` is the freestanding native core, **not** a boot image. Never pass that `.bin` to `fastboot flash`.

A public self-contained boot-image package and a first-install layout migration tool are being prepared. The laboratory's original boot image is not included: it contains a donor Linux kernel/ramdisk and Magisk configuration. A public package must have clean contents, the correct carrier geometry and its kernel source/license materials. The instructions below describe the two installation routes; they are not a claim that a universal installer is already supplied.

## Enter Sony fastboot

Use an **unlocked E6653**. Power off, unplug USB, hold **Volume Up**, and connect USB to the PC. The indicator should turn blue. Install Android Platform Tools and the Sony fastboot USB driver if Windows does not detect the phone.

From a terminal in the Platform Tools folder:

```text
fastboot devices
fastboot getvar product
```

Confirm the phone is an **E6653**, using its model label and the known device/firmware identity before flashing. Some bootloader states report an unhelpful `Unknown` product value; that response alone does not establish a supported device or layout. Other Z5 models have not been accepted for installation.

## Stock-layout boot-image route

Once a compatible, **flashable Android boot image** is available, the simple stock-layout command sequence is:

```text
fastboot flash boot xploader.img
fastboot reboot
```

Here `xploader.img` means the packaged image for that release, not a renamed `.bin`, OS image or GitHub source ZIP. These commands replace the stock-layout boot payload. Preserve your existing boot image through recovery before replacing it if you want to restore that system. This source-preview repository currently supplies no image named `xploader.img`.

**This route alone is not the ROM-flash-resistant setup.** It does not create a separate OS boot partition; the current separate-partition Boot eMMC path requires the split layout. SD boot and SD TWRP are the intended entry paths while configuring an SD-only trial. A later ordinary ROM flash to the original stock `boot` partition can overwrite the manager.

For a compatible image that supports RAM boot, `fastboot boot xploader.img` can be used for a temporary trial instead. RAM boot does not install the manager or migrate the partition table. Do not assume every Sony firmware accepts an arbitrary image.

## Tested persistent split layout

| Purpose | Lab GPT name | Lab partition | Size |
| --- | --- | --- | --- |
| Sony normal-boot manager | `xploader` (original Sony boot GUID retained) | p29 | 32 MiB |
| Replaceable eMMC OS boot | `boot` | p44 | 32 MiB |

The laboratory migrated a measured original 64 MiB boot extent into these two extents, retained the original manager GUID and preserved every unrelated GPT entry. This requires device-specific inspection, OS boot relocation and verification of both GPT copies. **Do not copy another phone's GPT image.** The lab helper is intentionally bound to its original phone and is not distributed as a general installer.

Sony S1's normal boot selection and its fastboot partition lookup must be distinguished from recovery's Linux `by-name` lookup. On an already migrated phone, do not assume `fastboot flash boot` updates the manager. The current verified laboratory update path writes and reads back the independently identified manager partition from root recovery. The public Windows tool will automate the correct route after layout detection.

## Prepare the SD card and use the menu

1. Use exFAT. Place a Z5-compatible TWRP image at the SD root as **`twrp.img`**.
2. Extract an xploader-adapted OS folder at the root, for example **`/lineageos19`**. Keep its supplied `startup.txt` and companion image filenames.
3. In xploader, use `Service → Boot Settings → AD SD Dir` to select it, then **Boot SD**.
4. Use **TWRP** on the main menu to boot the root recovery image. Missing/invalid files produce an error page with Back; this does not download a recovery.
5. Use **Always Menu** for a menu at each boot, or select a target under **QuickBoot**. Hold Power during startup to override QuickBoot, and release Power after the menu appears.

To copy files while in xploader, use `Service → PC SD Access`. The current session lasts 180 seconds. Safely remove/eject the PC disk and wait until the loader finishes the session before booting an OS. Use a card reader or recovery MTP/ADB for transfers that exceed the current USB session window.

## Remove xploader / return to Sony stock firmware

The intended removal route is **Flashtool in flashmode with a complete, matching E6653 stock firmware package**. A stock firmware flash can overwrite the custom manager, in the same general sense that a full stock ROM restore removes an aftermarket boot manager.

**Full Flashtool removal/restoration of this split layout has not been hardware-tested yet.** Flashtool supports Xperia firmware flashing ([upstream project](https://github.com/Androxyde/Flashtool)); that does not by itself prove a particular FTF bundle restores this phone's GPT. For the split layout, restoration must cover the original partition layout and matching stock boot/system components. A boot-only flash may remove the manager while leaving the split GPT behind. Do not advertise that as a complete stock restoration.

The release restoration guide must identify the exact firmware bundle, confirm its partition-table restoration behavior and verify stock cold boot before this route is marked accepted. Restoring stock Android is also separate from relocking the bootloader or recovering DRM/TA material. No relock is required or performed by xploader.

## Other troubleshooting

Keep the original Sony fastboot/flashmode access available. For a stock-layout trial, restore the original matching boot image through fastboot if needed. Split-layout restoration additionally requires reversing the layout migration; replacing one boot image does not merge GPT partitions. No restoration command for an unverified layout is provided here.

If a payload hangs without ADB, physical reset may still be needed. Reset recovery for arbitrary hung payloads is not guaranteed. A PC firmware flasher may overwrite the persistent setup. See the README's exact ordinary-ROM flashing scope.
