#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$root"
mkdir -p build/tests
menu_sources=(src/menu/xpl_menu.c src/menu/xpl_framebuffer.c src/menu/xpl_locale.c)
"${HOST_CC:-cc}" -std=c11 -O2 -Dxpl_fb_validate=xpl_fb_validate_005 -Dxpl_menu_render=xpl_menu_render_005 -c tests/reference005/xpl_framebuffer.c -o build/tests/reference005.o
"${HOST_CC:-cc}" -std=c11 -O2 -Wno-misleading-indentation tests/test_menu.c "${menu_sources[@]}" build/tests/reference005.o -o build/tests/menu
build/tests/menu build/tests
for name in folder_menu menu_extended; do
  "${HOST_CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wno-misleading-indentation "tests/test_${name}.c" "${menu_sources[@]}" -o "build/tests/$name"
  "build/tests/$name"
done
for name in settings; do
  "${HOST_CC:-cc}" -std=c11 -O2 -Wall -Wextra -Wno-misleading-indentation "tests/test_${name}.c" src/native_menu/settings.c -o "build/tests/$name"
  "build/tests/$name"
done
"${HOST_CC:-cc}" -std=c11 -O2 -Wno-misleading-indentation -Dxpl_settings_default=xpl_settings_default_v1 -Dxpl_settings_decode=xpl_settings_decode_v1 -Dxpl_settings_encode=xpl_settings_encode_v1 -Dxpl_settings_choose=xpl_settings_choose_v1 -c tests/reference-settings-v1/settings.c -o build/tests/settings-v1.o
"${HOST_CC:-cc}" -std=c11 -O2 -Wno-misleading-indentation tests/test_settings_locale.c src/native_menu/settings.c build/tests/settings-v1.o -o build/tests/settings-locale
build/tests/settings-locale
python3 tools/verify_core.py
"${HOST_CC:-cc}" -std=c11 -O1 -g -Wall -Wextra -Werror -Wno-misleading-indentation -fsanitize=address,undefined installer/guard.c -o build/tests/installer-guard
XPL_HOST_GUARD="$root/build/tests/installer-guard" python3 tests/test_installer.py
python3 tests/test_installer_device.py
python3 tests/test_installer_recovery.py
