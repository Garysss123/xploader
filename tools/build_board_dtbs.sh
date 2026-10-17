#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
src="${1:?Pass the extracted kernel source directory from vendor/sumire-board/source.json}"
out="$root/build/board-dtbs"
mkdir -p "$out"
dtcsrc="$src/scripts/dtc"
printf '#define DTC_VERSION "DTC 1.4.0 xploader pinned source"\n' > "$out/version_gen.h"
bison -d -o "$out/dtc-parser.tab.c" "$dtcsrc/dtc-parser.y"
flex -o "$out/dtc-lexer.lex.c" "$dtcsrc/dtc-lexer.l"
gcc -O2 -fcommon -I "$out" -I "$dtcsrc" -I "$dtcsrc/libfdt" \
 "$dtcsrc/dtc.c" "$dtcsrc/flattree.c" "$dtcsrc/fstree.c" "$dtcsrc/data.c" \
 "$dtcsrc/livetree.c" "$dtcsrc/treesource.c" "$dtcsrc/srcpos.c" "$dtcsrc/checks.c" "$dtcsrc/util.c" \
 "$out/dtc-parser.tab.c" "$out/dtc-lexer.lex.c" -o "$out/dtc"
index=0
for ver in 2.0 2.1; do
  name="msm8994-v$ver-kitakami_sumire_generic"
  cpp -nostdinc -undef -D__DTS__ -x assembler-with-cpp -I "$src/include" \
    -I "$src/arch/arm/boot/dts" "$src/arch/arm/boot/dts/qcom/$name.dts" -o "$out/$name.pp.dts"
  "$out/dtc" -I dts -O dtb -b 0 -i "$src/arch/arm/boot/dts/qcom" \
    -o "$out/board-$index.dtb" "$out/$name.pp.dts" > "$out/$name.log" 2>&1
  cmp "$out/board-$index.dtb" "$root/vendor/sumire-board/board-$index.dtb"
  index=$((index+1))
done
sha256sum "$out"/*.dtb
