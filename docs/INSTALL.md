# Installing xploader v1.0

Use an **unlocked Sony Xperia Z5 E6653**, an exFAT MicroSD and a Windows PC.
Confirm the model from the phone's label; recovery model properties alone do
not establish the physical variant. Other Z5 variants remain unverified.

Full stock restoration has been verified. The standalone installer physically
created the split layout and preserved the OS boot; a resumed run then verified
fresh SD TWRP, both GPT copies and the complete boot windows, restored Always
Menu and observed screen-off. The corrected app uses TWRP's orderly System
restart. An uninterrupted first installation with this corrected app is still
being tested and is not marked passed here.

## Windows installer

1. Extract the complete v1.0 installer ZIP to a writable local folder.
2. Power off. Hold **Volume Up** while connecting USB; the indicator should
   turn blue. Ensure the Sony fastboot USB driver is installed.
3. Run **`xploader-v1.0.exe`**. Python installation is unnecessary. Missing
   Google Platform Tools and original sumire TWRP are downloaded directly from
   their publishers and checked against fixed size/hash values.
4. Read the detected device/layout and saved recovery-record path. Type
   **INSTALL** to proceed. Keep USB connected while the tool preserves the OS
   boot, installs the split layout and verifies a new SD TWRP boot.
5. Keep the local `installer/work` record. A successful run saves **Always
   Menu** and leaves the phone in original SD TWRP with screen timeout.

From original TWRP 3.7 with root ADB, this is a read-only phone check:

```text
xploader-v1.0.exe --check
```

It does not reboot or write phone partitions/SD files; missing host tools can
be downloaded. Source users with Python 3.10+ can run `Install-xploader.cmd`
after supplying the release assets. A GitHub source ZIP alone lacks those
binary assets. The standalone ZIP is assembled separately after validation.

The installer records exact before/after windows, checks both GPT copies,
retains the original Sony normal-boot GUID and refuses unknown layouts. Write
order is OS copy, backup GPT, primary GPT, then manager. It does not write S1,
TA/DRM, system, userdata or the SD partition table. A supported Sony ELF boot is
repackaged with the same kernel, initrd and DTBs; a supported Android boot image
is preserved. This preserves OS boot, not all personal data.

## Persistent layout

| Purpose | GPT name | Partition | Size |
| --- | --- | --- | --- |
| Sony normal-boot manager, original boot GUID retained | `xploader` | p29 | 32 MiB |
| Replaceable eMMC OS boot | `boot` | p44 | 32 MiB |

These extents occupy the original 64 MiB boot area; unrelated GPT entries are
preserved. Ordinary Linux/TWRP installers using `boot` update the OS partition.
**Never copy another phone's GPT.** An update to a supported split layout writes
only the manager and preserves the separate OS boot.

## Fastboot

Enter blue-light fastboot from power-off with **Volume Up + USB**. With Android
Platform Tools:

```text
fastboot devices
fastboot getvar product
```

An `Unknown` product response alone is not model verification. The installer
uses `fastboot boot` to start temporary original TWRP, then checks the layout
before its bounded migration/write operations.

For an advanced **stock-layout, SD-only trial**, a release's clean boot image
can replace the original boot payload:

```text
fastboot flash boot xploader.img
fastboot reboot
```

This short route does **not** create the persistent split layout or preserve
the original eMMC OS boot. Boot eMMC needs the split layout, and a later ROM
flash can overwrite a stock-layout manager. Use the installer for a persistent
installation. On an already split phone, do not assume fastboot's `boot` name
selects the manager. Never flash a native-core `.bin`.

## SD and menu

Put original sumire recovery at **`/twrp.img`** and an adapted OS folder such as
**`/lineageos19`** at the SD root. Choose it under
`Service → Boot Settings → AD SD Dir`, then **Boot SD**. QuickBoot targets eMMC,
the selected SD folder or TWRP; **Always Menu** restores the startup menu. Hold
Power during startup to override QuickBoot, releasing it when the menu appears.
Volume moves; short-press/release Power selects.

`Service → PC SD Access` provides a bounded 180-second native USB mass-storage
session. Eject/safely remove the PC disk and wait for the session to finish
before booting an OS. Use recovery MTP/ADB or a card reader for longer transfers.

## Remove xploader / return to Sony stock firmware

Use **Flashtool in green-light flashmode** with a complete matching E6653 FTF
covering the original partition layout, kernel, system and required firmware
components. From power-off, hold **Volume Down** while connecting USB. Flashmode
and blue-light fastboot use different Windows drivers.

The restoration test used Flashtool **0.9.36.0** and
**E6653_32.4.A.1.54_Customized_UK.ftf**. Its 32GB partition template matches the
phone's original layout, including the 64 MiB boot area. After the full flash,
both GPT copies and all 43 original entries matched that template. With USB
disconnected, a power-only startup entered Sony Android **7.1.1 / 32.4.A.1.54**
directly, without xploader; stock UI and ADB were verified. A boot-only flash
does not restore the partition layout.

The test phone has a pre-existing `Unknown` TA model string. The ordinary GUI
rejected that identity. The successful test used the unmodified official
Flashtool engine through a private, device-bound API wrapper with a temporary
model alias. Firmware signatures, hardware matching and TA contents were not
changed. This validates full restoration on this phone; it does not validate a
generic GUI workaround for every device reporting `Unknown`.

Stock restoration can clear internal user data. Keep SD payloads and do not
select SD formatting. Returning stock does not relock the bootloader or recover
lost DRM/TA keys. No relock or DRM operation is part of xploader installation.

A hung payload without USB can still need physical reset. Keep the local
installer record; unknown or partially written states are refused rather than
silently treated as a completed installation.
