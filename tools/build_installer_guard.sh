#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
musl="${1:?Pass the prefix of a static AArch64 musl 1.2.5 build}"
cross="${CROSS_COMPILE:-aarch64-linux-gnu-}"
cc="${CC:-${cross}gcc}"
uapi="${XPL_LINUX_UAPI:-/usr/aarch64-linux-gnu/include}"
out="$root/build/installer-v1.0"
mkdir -p "$out"
test -f "$musl/lib/musl-gcc.specs"
read -r -a extra <<< "${XPL_EXTRA_CFLAGS:-}"
"$cc" "${extra[@]}" -specs="$musl/lib/musl-gcc.specs" -idirafter "$uapi" \
 -static -Os -march=armv8-a -mno-outline-atomics -Wall -Wextra -Werror -Wno-misleading-indentation \
 -ffunction-sections -fdata-sections -Wl,--build-id=none,--gc-sections \
 "$root/installer/guard.c" -o "$out/xploader-install-guard"
"${cross}readelf" -h -l "$out/xploader-install-guard" > "$out/elf.txt"
if grep -q INTERP "$out/elf.txt"; then echo 'Helper must be static' >&2; exit 1; fi
sha256sum "$out/xploader-install-guard"
