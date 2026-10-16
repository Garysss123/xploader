#!/usr/bin/env bash
set -euo pipefail
task_root="$(cd -- "$(dirname -- "$0")/.." && pwd)"
build="$task_root/build/native074"
mkdir -p "$build"
cp "$task_root/config/native_menu_config.h" "$build/native_menu_config.h"
cross="${CROSS_COMPILE:-aarch64-linux-gnu-}"
compiler="${CC:-${cross}gcc}"
read -r -a extra_flags <<< "${XPL_EXTRA_CFLAGS:-}"
"$compiler" "${extra_flags[@]}" \
 -nostdlib -pie -O2 -Wall -Wextra -Werror -Wno-misleading-indentation -Wno-implicit-fallthrough -ffreestanding -fno-builtin \
 -DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_MALLOC -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_DEFLATE_APIS -DMINIZ_NO_ZLIB_APIS -DMINIZ_USE_UNALIGNED_LOADS_AND_STORES=0 -DNDEBUG \
 -fno-stack-protector -fpie -fvisibility=hidden -fno-tree-loop-distribute-patterns -ffunction-sections -fdata-sections \
 -fno-unwind-tables -fno-asynchronous-unwind-tables -mgeneral-regs-only -mstrict-align \
 -I "$build" -Wl,--build-id=none,--no-dynamic-linker,-Bsymbolic,--gc-sections \
 -Wl,-T,"$task_root/src/native_menu/linker.ld",-Map,"$build/menu.map" \
 "$task_root/src/native_menu/entry.S" "$task_root/src/native_menu/main.c" "$task_root/src/native_menu/runtime.c" "$task_root/src/native_menu/bootargs.c" "$task_root/src/native_menu/dsi_window.c" "$task_root/src/native_menu/dsi_frame.c" "$task_root/src/native_menu/service_probe.c" "$task_root/src/native_menu/service_reset.c" "$task_root/src/native_menu/service_poweroff.c" "$task_root/src/native_menu/usb_power_probe.c" "$task_root/src/native_menu/usb_guard.c" "$task_root/src/native_menu/usb_control.c" "$task_root/src/native_menu/usb_enum.c" "$task_root/src/native_menu/usb_msd.c" \
 "$task_root"/src/native_hw/*.c "$task_root"/src/menu/*.c \
 "$task_root/src/sdhci/xpl_sd_board.c" "$task_root/src/sdhci/xpl_sd_probe.c" "$task_root/src/sdhci/xpl_sd_core_probe.c" "$task_root/src/sdhci/xpl_sd_mode_probe.c" "$task_root/src/sdhci/xpl_sd_powered.c" "$task_root/src/sdhci/xpl_sdhci.c" \
 "$task_root/src/storage/xpl_exfat.c" "$task_root/src/sdhci/xpl_emmc_probe.c" "$task_root/src/sdhci/xpl_emmc_read.c" "$task_root/src/sdhci/xpl_emmc_image.c" \
 "$task_root/src/sdhci/xpl_sd_fast.c" \
 "$task_root/src/payload/xpl_payload.c" "$task_root/src/native_menu/settings.c" "$task_root/src/native_menu/gzip.c" "$task_root/src/native_menu/fdt_payload.c" "$task_root/src/native_menu/instruction_cache.c" "$task_root/src/third_party/miniz-3.0.2/miniz_tinfl.c" \
 -o "$build/menu.elf"
"${cross}objcopy" -O binary "$build/menu.elf" "$build/menu.bin"
"${cross}readelf" -h -r "$build/menu.elf" > "$build/menu-elf.txt"
printf 'Native core built: %s\n' "$build/menu.bin"
