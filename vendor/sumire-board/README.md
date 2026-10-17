# E6653 carrier board DTBs

These are unchanged board DTBs compiled from the GPL Sony MSM8994 Linux source
at the exact revision in `source.json`. The carrier adds its own proof slots
while packaging; those slots are absent in these original inputs.

The v1.0 release supplies the complete source archive named in `source.json`.
After extracting and checking its SHA256, install GCC, cpp, bison and flex, then
run `bash tools/build_board_dtbs.sh /path/to/extracted/kernel`. The script builds
the source tree's DTC and checks that both results match the supplied DTBs.

The DTBs remain GPL-2.0 with upstream per-file notices; see `COPYING`. They are
separate from the MIT native core and the owned marker initrd. No compiled
Linux kernel is bundled in the native carrier.
