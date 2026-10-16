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

The public source is the frozen native074 core. Local export checks verify 83 source files and its 267,928-byte supplied native binary. Rebuilding the curated source with the lab's GCC13 toolchain reproduced that binary byte-for-byte. Host checks cover menu/controller/framebuffer equivalence against an independent older renderer, folder interaction, saved-settings torn writes/corruption and old/new settings compatibility. Host tests and renderer previews are not phone screenshots or substitute hardware boot tests.

The public first-install tool, self-contained flashable image, other Z5 variants, and XNU/UEFI/Windows CE/NT OS support are not accepted release capabilities in this source preview.
