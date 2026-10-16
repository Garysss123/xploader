#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
mkdir -p "$root/build/previews"
"${HOST_CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wno-misleading-indentation "$root/tools/render_gallery.c" "$root/src/menu/xpl_menu.c" "$root/src/menu/xpl_framebuffer.c" "$root/src/menu/xpl_locale.c" -o "$root/build/render-gallery"
cd "$root/build/previews"
"$root/build/render-gallery"
python3 "$root/tools/preview_png.py" "$root/build/previews" "$root/docs/images"
