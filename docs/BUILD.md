# Building the native074 core

Install a GNU AArch64 bare Linux cross compiler (`aarch64-linux-gnu-gcc`, binutils), a native C compiler and Python 3. Run from a Linux shell or WSL:

```sh
bash tools/build.sh
bash tools/check.sh
```

`build/native074/menu.bin` and `menu.elf` are the results. `CROSS_COMPILE` selects another GNU prefix; `CC` may select the cross compiler explicitly. `XPL_EXTRA_CFLAGS` is available for a compiler sysroot/search paths. `HOST_CC` selects the native compiler used by tests/previews.

`config/native_menu_config.h` is the exact configuration from the accepted E6653 native074 core, with the measured carrier hook, 24 MiB staging geometry and original entry ABI. It is **not** a configuration for every Android boot image. Changing a compiler or configuration creates a new build needing its own hardware acceptance. `tools/verify_core.py` checks the frozen source and supplied binary, not an arbitrary rebuilt binary's hardware behavior.

The native core expects a compatible Android boot-image carrier, firmware-provided display/device-tree state and the tested old AArch64 Image entry contract. A `.bin` is not a boot image. A universal carrier generator/first-install package is not included in this preview; see [installation status](INSTALL.md). The source tree deliberately excludes lab device credentials, ROM userdata images, research logs and phone-bound raw GPT writers.

The frozen entry also requires a **non-empty firmware initrd** before display/menu initialization. A native-only candidate with no initrd did not return a recovery USB channel during a RAM test and is excluded from this release. A new carrier must satisfy every entry precondition and have a tested automatic recovery path before unattended hardware use.

## Renderer previews

```sh
# Pillow must be available to python3.
bash tools/render_gallery.sh
```

This compiles and executes the **same frozen menu renderer** on the host, with representative menu state. Its PPM output is converted to PNG without retouching. It performs no phone I/O and does not establish hardware acceptance. Preview metadata and real OS capture provenance are recorded in `docs/images/provenance.json`.
