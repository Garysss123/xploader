# Third-party notices and OS image publication

The xploader native core is MIT licensed; see `LICENSE` and per-file SPDX headers.

| Component | License / source |
| --- | --- |
| miniz 3.0.2 inflate code | MIT; notice in `src/third_party/miniz-3.0.2/LICENSE` |
| Noto Sans TC bitmap subset | SIL Open Font License 1.1, `src/menu/fonts/OFL.txt`; exact upstream font revision and hashes in `src/menu/fonts/source.json` |
| Historical renderer test reference | xploader MIT source retained in `tests/reference005` |
| Optional guard004 Linux recovery kernel | GPL-2.0 and upstream per-file notices; `recovery/guard004/COPYING`, source overlays/configuration and complete modified source archive in the release |

MAGLDR/cLK inspired the interaction and workflow; their binaries are not distributed in this native-core preview. Independent reference projects retain their own licenses.

## Demonstration systems

All three SD OS downloads are separate **experimental test images**. Boot and basic UI are demonstrated, while wider Z5 hardware compatibility remains incomplete. In particular, postmarketOS and Sailfish OS should not be presented as supported daily-driver ports.

- **LineageOS 19.1**: based on the sumire community build `lineage-19.1-20220807-UNOFFICIAL-sumire.zip` from [Sony-MSM8994-Dev's archive](https://github.com/Sony-MSM8994-Dev/OTA/releases/tag/archives). The adapted SD package is a separate derivative; link the original project and retain its package/kernel/vendor notices.
- **postmarketOS**: contains an Alpine/postmarketOS userspace and a Z5 kernel/BSP adaptation. Retain package licenses and provide corresponding source/build materials for GPL components and modifications with a binary release.
- **Sailfish OS**: the project includes both open and closed components under different terms. [Official open-source overview](https://docs.sailfishos.org/Develop/Open_Source/) and [HADK](https://docs.sailfishos.org/Develop/HADK/) describe the platform/community-port context. Check the actual redistributed packages, artwork, firmware and vendor files against their terms; Linux kernel licensing does not cover every component.

The current repository contains screenshots and the native loader core, **not** the full OS images. Before adding OS download links, prepare clean test images without used `data.img`, USB authorization keys, SSH private keys, accounts, machine identity files or personal logs. Preserve each distribution's notices and exact kernel/BSP source and build changes. This is an image-content review task, not a blanket claim that community images cannot be shared.

The v1.0 native-only carrier contains the native core, a tiny owned marker initrd and two GPL board DTBs. It contains no Linux kernel, donor recovery ramdisk or Magisk configuration. The board files, GPL license, exact source revision and full corresponding kernel source archive are attributed under `vendor/sumire-board/`. Historical donor-based laboratory carriers are not release artifacts.

The installer helper is MIT licensed and statically linked with musl 1.2.5; its notice is in `installer/licenses/musl-COPYRIGHT.txt`. The helper also links GCC soft-float runtime code under GPL-3 with the GCC Runtime Library Exception 3.1; its complete notice and license accompany the package. The portable Windows executable includes Python under its PSF license, OpenSSL 3.0.18 with its Apache-2.0 license and notice, and the PyInstaller bootloader under its GPL exception for bundled applications. Platform Tools and original TWRP are downloaded directly from their publishers; their licenses remain separate. Local installer work records contain device identity and boot/GPT recovery material and are excluded from publication.

The optional TWRP kit distributes the modified GPL recovery kernel with its complete modified kernel source/configuration. Its host builder fetches the unchanged original recovery image directly from the original publisher and performs the documented fstab/kernel changes locally. Original TWRP userspace/vendor binaries are not bundled in the kit; they retain their own licenses. The builder itself is MIT licensed. See [guard004](recovery/guard004/README.md) for source and usage.
