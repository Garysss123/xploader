# Validation scope

This project uses one unlocked Sony Xperia Z5 E6653. These are separate kinds of evidence:

| Area | Evidence obtained | Boundary |
| --- | --- | --- |
| Native manager | Menu/keys and OS handoff accepted on the physical E6653; v1.0 clean carrier readback and fresh eMMC LineageOS 19.1 handoff/desktop verified | One device; a new build or firmware needs its own acceptance |
| SD recovery | Original TWRP 3.7 boots from `/twrp.img`; v1.0 runtime proof matches the source image; installed manager readback matches | Recovery screen appearance and root ADB were tested separately; no arbitrary recovery compatibility claim |
| SD Android | LineageOS 17.1/19.1 folder switching and completed boots tested; publication screenshots taken during a fresh SD19.1 boot | Boot/UI is not a complete device-services test |
| postmarketOS | Xorg/Xfce desktop, physical touch/window control and repeat boot accepted | Wider hardware services, heat and sustained power need more work |
| Sailfish OS | Original systemd/Lipstick UI, physical touch/unlock/control and original application execution observed | Wider hardware services, shutdown and long-session reliability are not release guarantees |
| Ordinary ROM flashing | Tested split layout survived unmodified TWRP kernel/full-ROM writes, system/data/cache clearing, damaged OS boot and disconnected cold boot; on the v1.0 installer-created layout, a new data reset and original full 19.1 ZIP preserved the complete manager and both GPT copies, followed by eMMC desktop boot | No guarantee against raw manager/GPT writes, every ZIP, every firmware or stock PC flashing; earlier cold-boot layout tests and the new v1.0 warm boot are distinct |
| PC SD access | Native USB SD exchange/eject and subsequent exFAT read were verified | Current session is bounded to 180 seconds; large/long transfers need further work |
| QuickBoot / Always Menu | Saved-target automatic boot, held Power override, on-device save and subsequent reboot verified | Settings require the SD configuration file |
| Reset / PowerOff | Reset return and user-confirmed physical power-off tested | Not a general recovery guarantee for every hung payload |
| Stock restore | Flashtool 0.9.36.0 full matching FTF restore; both GPT copies and all 43 original entries verified; disconnected power-only boot directly into Sony Android 7.1.1 / 32.4.A.1.54, stock UI and ADB | One E6653; its pre-existing `Unknown` TA model required a private device-bound wrapper around the official engine, as documented in the removal notes |

The public v1.0 source uses the frozen native-menu-076 core. GCC13 reproduced
its 270,248-byte binary; Windows and Linux carrier builds reproduced the
accepted 905,216-byte image. The installed image entered original SD TWRP and
provided fresh source-bound normal entry/watchdog-restoration proof. Forced
early-failure reset controls remain unpassed.

The standalone Windows installer completed a real-phone read-only check,
including its automatic pinned Google Platform Tools download. It physically
wrote the first-install OS copy, backup GPT, primary GPT and manager in that
order, with exact readback. The first bare ADB restart failed to load SD; two
proper TWRP System restarts then passed, followed by a successful standalone
resume using that path. Resume verified fresh source-bound SD TWRP, both GPT
copies and complete boot windows, restored Always Menu and observed screen-off.
The corrected standalone app subsequently completed a fresh updater run from
eMMC LineageOS 19.1 through automatic fastboot, original temporary TWRP and
source-bound SD TWRP. It preserved the eMMC OS boot, restored Always Menu and
observed screen-off. Its manager bytes were already equal, so that update
required no manager write; the earlier first-install writes are a separate test.
A later cold-fastboot operator test used the one-prompt Windows app and its
locally generated installer screen. It reached root recovery in about 13 seconds,
then completed actual first-install writes from the restored stock GPT and a
fresh source-bound SD TWRP boot. Both GPT copies and complete manager/OS boot
windows matched; Always Menu was saved and read back. The operator restarted
during the final screen-off wait, so that particular session stopped and is not
claimed as an uninterrupted `SUCCESS`. The same one-prompt app was then run
again by the operator from fastboot and completed the entire updater cycle:
source-bound SD recovery, full GPT/boot readback, saved Always Menu and observed
screen-off, ending with `SUCCESS`. Its already-equal manager needed no rewrite. The app keeps early diagnostic logs and reports
elapsed waiting times; it does not require manual TWRP navigation.
Host tests cover the actual C guard and transaction ordering/interruption.
Other Z5 variants and XNU/UEFI/Windows CE/NT are not accepted release capabilities.
