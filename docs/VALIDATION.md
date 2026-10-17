# Validation scope

This project uses one unlocked Sony Xperia Z5 E6653. These are separate kinds of evidence:

| Area | Evidence obtained | Boundary |
| --- | --- | --- |
| Native manager | Menu/keys and original OS handoff accepted on the physical E6653; native074 installed image read back matching its package | One device/firmware; a new build needs its own acceptance |
| SD recovery | Original TWRP 3.7 boots from `/twrp.img`; native074 runtime proof matches the source image; installed manager readback matches | Recovery screen appearance and root ADB were tested separately; no arbitrary recovery compatibility claim |
| SD Android | LineageOS 17.1/19.1 folder switching and completed boots tested; publication screenshots taken during a fresh SD19.1 boot | Boot/UI is not a complete device-services test |
| postmarketOS | Xorg/Xfce desktop, physical touch/window control and repeat boot accepted | Wider hardware services, heat and sustained power need more work |
| Sailfish OS | Original systemd/Lipstick UI, physical touch/unlock/control and original application execution observed | Wider hardware services, shutdown and long-session reliability are not release guarantees |
| Ordinary ROM flashing | Tested split layout survived unmodified TWRP kernel/full-ROM writes, system/data/cache clearing, damaged OS boot and disconnected cold boot | No guarantee against raw manager/GPT writes, every ZIP, every firmware or stock PC flashing |
| PC SD access | Native USB SD exchange/eject and subsequent exFAT read were verified | Current session is bounded to 180 seconds; large/long transfers need further work |
| QuickBoot / Always Menu | Saved-target automatic boot, held Power override, on-device save and subsequent reboot verified | Settings require the SD configuration file |
| Reset / PowerOff | Reset return and user-confirmed physical power-off tested | Not a general recovery guarantee for every hung payload |
| Stock restore | Planned matching E6653 full firmware + partition-layout restoration through Flashtool | Not yet hardware-validated |

The public v1.0 source uses the frozen native-menu-076 core. GCC13 reproduced
its 270,248-byte binary; Windows and Linux carrier builds reproduced the
accepted 905,216-byte image. The installed image entered original SD TWRP and
provided fresh source-bound normal entry/watchdog-restoration proof. Forced
early-failure reset controls remain unpassed.

The standalone Windows installer completed a real-phone read-only check,
including its automatic pinned Google Platform Tools download. Host tests cover
the actual C guard and transaction ordering/interruption. The physical stock
restore and first-install round trip is pending. Other Z5 variants and
XNU/UEFI/Windows CE/NT are not accepted release capabilities.
