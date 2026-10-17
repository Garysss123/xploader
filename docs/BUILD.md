# Building xploader v1.0

Install a GNU AArch64 Linux cross compiler/binutils, native C compiler and
Python 3.10+. From Linux or WSL:

```sh
bash tools/build.sh
python3 tools/build_image.py
bash tools/check.sh
```

Outputs are `build/v1.0/menu.bin` and `build/v1.0/xploader.img`. The carrier
builder checks the pinned core, owned initrd, board hashes, geometry, entry
branches and final accepted image hash; it preserves an existing output.
`CROSS_COMPILE`, `CC` and `XPL_EXTRA_CFLAGS` select compiler/sysroot paths.
GCC 13 reproduced the accepted 270,248-byte core. Windows and Linux Python
both reproduced the accepted 905,216-byte image byte-for-byte.

`releases/v1.0.json` binds exact source/config/core/image hashes. The internal
source identifier remains native-menu-076; public release names use v1.0. A
changed compiler/configuration needs its own acceptance. Host compilation is
not a new hardware result.

The carrier has no Linux kernel or donor recovery ramdisk. The two GPL board
DTBs and their complete source correspondence are attributed under
`vendor/sumire-board/`. Nonempty marker initrd/address geometry are measured
Sony S1 entry requirements. Normal watchdog arm/restore was observed on
hardware; forced early-failure reset controls remain a separate unpassed test.

Installer source is under `installer/`. Its bounded helper is MIT source linked
statically with musl 1.2.5; see `tools/build_installer_guard.sh` and dependency
notices. It accepts a runtime captured device CID and the supported geometry,
not a device-bound GPT. Host tests execute the C validator with ASAN/UBSAN and
test interrupted transactions against synthetic windows.

Use the pinned build requirements and `tools/build_installer.ps1` for the
portable Windows application. Keep generated binaries, downloaded tools and
local device work records out of the source commit. Binary assets are assembled
separately after physical validation.

## Renderer previews

With Pillow installed for the host Python:

```sh
bash tools/render_gallery.sh
```

This executes the frozen renderer on representative states and converts PPM to
PNG without retouching. It performs no phone I/O. Gallery provenance separates
these previews and real device screenshots.
