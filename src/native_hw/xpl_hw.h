/* SPDX-License-Identifier: MIT */
#ifndef XPL_HW_H
#define XPL_HW_H
#include <stddef.h>
#include <stdint.h>

#define XPL_HW_MAX_RAM 8u
#define XPL_HW_MAX_RESERVED 32u
#define XPL_HW_CTL_COUNT 5u
#define XPL_HW_MIXER_COUNT 6u
#define XPL_HW_PIPE_COUNT 10u
#define XPL_HW_KEY_UP 1u
#define XPL_HW_KEY_DOWN 2u
#define XPL_HW_KEY_POWER 4u

enum xpl_hw_status {
    XPL_HW_OK = 0, XPL_HW_ARGUMENT, XPL_HW_FDT_INVALID, XPL_HW_FDT_LIMIT,
    XPL_HW_RESOURCE_MISSING, XPL_HW_RESOURCE_UNSUPPORTED, XPL_HW_NO_RAM,
    XPL_HW_MMIO_FAULT, XPL_HW_DISPLAY_OFF, XPL_HW_DISPLAY_TOPOLOGY,
    XPL_HW_DISPLAY_FORMAT, XPL_HW_SURFACE_RANGE, XPL_HW_CHANGED,
    XPL_HW_SPMI_UNSUPPORTED, XPL_HW_SPMI_TIMEOUT, XPL_HW_SPMI_ERROR,
    XPL_HW_KEYS_UNCONFIGURED
};
enum xpl_hw_phase {
    XPL_HW_PHASE_NONE = 0, XPL_HW_PHASE_FDT, XPL_HW_PHASE_POWER,
    XPL_HW_PHASE_TOPOLOGY, XPL_HW_PHASE_SURFACE, XPL_HW_PHASE_PRESENT,
    XPL_HW_PHASE_SPMI, XPL_HW_PHASE_KEYS
};
struct xpl_hw_diag {
    enum xpl_hw_status status;
    enum xpl_hw_phase phase;
    uint64_t address;
    uint32_t value;
    uint32_t detail;
    uint32_t reads, writes;
};
struct xpl_hw_range { uint64_t base, bytes; };
struct xpl_hw_config {
    struct xpl_hw_range ram[XPL_HW_MAX_RAM], reserved[XPL_HW_MAX_RESERVED];
    unsigned ram_count, reserved_count;
    struct xpl_hw_range splash, mdp, gdsc, clocks, spmi;
    struct xpl_hw_range watchdog; /* Optional exact APPS watchdog; zero if absent. */
    uint32_t ctl_offsets[XPL_HW_CTL_COUNT], pipe_offsets[XPL_HW_PIPE_COUNT];
    uint32_t mixer_offsets[XPL_HW_MIXER_COUNT]; /* Zero for absent mixer IDs. */
    uint32_t spmi_channel, spmi_ee;
    unsigned complete;
};

/* Callbacks return 1 on success, 0 on synchronous abort/failure; device accesses
 * must be ordered. Root supplies abort guards. This module has no direct MMIO.
 * fence must order CPU framebuffer stores before an optional display trigger.
 * Cache cleaning, if caches are enabled, belongs to the caller before present. */
struct xpl_hw_io {
    void *context;
    int (*read32)(void *context, uint64_t address, uint32_t *value);
    int (*write32)(void *context, uint64_t address, uint32_t value);
    void (*fence)(void *context);
};

struct xpl_hw_pipe_snapshot {
    uint32_t size, image_size, xy, out_size, out_xy;
    uint32_t address, stride, format, unpack, opmode, secure, decimation;
};
struct xpl_hw_display_snapshot {
    uint32_t gdsc, ahb_clock, core_clock, revision;
    uint32_t top[XPL_HW_CTL_COUNT];
    uint32_t layer[XPL_HW_MIXER_COUNT], layer_ext[XPL_HW_MIXER_COUNT];
    uint32_t mixer_size, timing_enabled;
    unsigned ctl, mixer, pipe, intf; /* intf is CTL OUT_SEL: 2/3 -> physical INTF1/2. */
    struct xpl_hw_pipe_snapshot source;
};
struct xpl_hw_surface {
    uint64_t base_pa, buffer_bytes;
    uint32_t width, height, stride_bytes;
    uint64_t scanout_base_pa, scanout_bytes;
    uint32_t scanout_height, top_rows_skipped; /* Draw-only viewport; registers unchanged. */
    uint8_t bytes_per_pixel;
    uint32_t red_mask, green_mask, blue_mask, alpha_mask;
    uint64_t ctl_start_pa;
    uint64_t ctl_flush_pa; /* Exact validated MSM8994 RGB0/LM0/INTF1 path only. */
    uint32_t ctl_flush_mask;
    uint32_t ctl_top;
    unsigned command_mode, ready;
};

/* Caller provides the actually readable DTB byte count. Output diagnostics and
 * partial resource fields survive failure, but complete remains zero. No MMIO. */
enum xpl_hw_status xpl_hw_resolve_fdt(const void *dtb, size_t available,
    struct xpl_hw_config *out, struct xpl_hw_diag *diag);

/* Pure data decoder, also used by host tests. Excluded ranges must cover this
 * loader/kernel/stack/DTB/initrd. Pixels are not read or written by this API.
 * For the exact measured 004 one-page prefix, two top scanout rows are excluded
 * from the returned drawable viewport; the splash reservation is not enlarged. */
enum xpl_hw_status xpl_hw_decode_display(const struct xpl_hw_config *config,
    const struct xpl_hw_display_snapshot *snapshot,
    const struct xpl_hw_range *excluded, size_t excluded_count,
    struct xpl_hw_surface *out, struct xpl_hw_diag *diag);

/* Reads only a bounded, source-defined register set on an already active
 * display. No power/clock/panel initialization. Snapshot remains for evidence
 * even if the surface cannot be accepted. */
enum xpl_hw_status xpl_hw_display_discover(const struct xpl_hw_config *config,
    const struct xpl_hw_io *io, const struct xpl_hw_range *excluded,
    size_t excluded_count, struct xpl_hw_surface *out,
    struct xpl_hw_display_snapshot *snapshot, struct xpl_hw_diag *diag);

/* Confirms unchanged CTL_TOP and fences stores. For the measured MDP1.9
 * RGB888 RGB0/LM0/INTF1 path, commits the validated shadow state with its fixed
 * donor FLUSH mask before CTL_START; otherwise keeps the inherited-start path.
 * Video mode needs no write. No pipe address/format/clock/panel reprogramming. */
enum xpl_hw_status xpl_hw_present(const struct xpl_hw_config *config,
    const struct xpl_hw_io *io, const struct xpl_hw_surface *surface,
    struct xpl_hw_diag *diag);

struct xpl_hw_keys {
    uint32_t arbiter_version;
    uint32_t poll_limit;
    uint8_t gpio_enable_mask[2];
    uint8_t last_up, last_down, last_power;
    unsigned ready;
};
/* Only v1, DT channel0/EE0 and SID0 are supported. Reads issue controller CMD
 * writes but never PMIC configuration writes. poll_limit must be 1..1000. */
enum xpl_hw_status xpl_hw_keys_init(const struct xpl_hw_config *config,
    const struct xpl_hw_io *io, uint32_t poll_limit,
    struct xpl_hw_keys *keys, struct xpl_hw_diag *diag);
enum xpl_hw_status xpl_hw_keys_poll(const struct xpl_hw_config *config,
    const struct xpl_hw_io *io, struct xpl_hw_keys *keys,
    unsigned *key_bits, struct xpl_hw_diag *diag);
/* On key-read failure the caller must ignore key_bits entirely: feeding a
 * synthetic zero/release to a menu could accidentally activate its selection. */

const char *xpl_hw_status_string(enum xpl_hw_status status);
#endif
