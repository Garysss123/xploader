/* SPDX-License-Identifier: MIT */
#ifndef XPL_MENU_H
#define XPL_MENU_H
#include <stddef.h>
#include <stdint.h>

#define XPL_MENU_KEY_UP 1u
#define XPL_MENU_KEY_DOWN 2u
#define XPL_MENU_KEY_POWER 4u
#define XPL_MENU_DEBOUNCE_MS 15u
#define XPL_MENU_REPEAT_DELAY_MS 450u
#define XPL_MENU_REPEAT_INTERVAL_MS 120u
#define XPL_MENU_POWER_SHORT_MAX_MS 1200u
#define XPL_MENU_ERROR_BACK_MS 3000u

enum xpl_menu_action {
    XPL_MENU_BOOT_EMMC = 0, XPL_MENU_BOOT_SD, XPL_MENU_TWRP,
    XPL_MENU_SERVICE, XPL_MENU_RESET, XPL_MENU_POWEROFF, XPL_MENU_ACTION_COUNT
};
#define XPL_MENU_AVAILABLE(action) (1u << (unsigned)(action))
enum xpl_menu_action_result {
    XPL_MENU_ACTION_UNSUPPORTED = 0,
    XPL_MENU_ACTION_RETURNED = 1, /* Handoff did not leave this caller. */
    XPL_MENU_ACTION_FAILED = 2
};
typedef enum xpl_menu_action_result (*xpl_menu_action_fn)(
    void *context, enum xpl_menu_action action);
enum xpl_menu_startup { XPL_MENU_ALWAYS = 0, XPL_MENU_QUICK = 1 };
enum xpl_menu_page { XPL_MENU_MAIN = 0, XPL_MENU_SERVICE_PAGE = 1, XPL_MENU_ERROR_PAGE = 2 };
enum xpl_menu_notice {
    XPL_MENU_NOTICE_NONE = 0, XPL_MENU_NOTICE_PENDING,
    XPL_MENU_NOTICE_RETURNED, XPL_MENU_NOTICE_FAILED,
    XPL_MENU_NOTICE_QUICK_RETURNED, XPL_MENU_NOTICE_INVALID_CONFIG,
    XPL_MENU_NOTICE_HELD_FOR_MENU, XPL_MENU_NOTICE_LONG_POWER
};

struct xpl_menu_config {
    enum xpl_menu_startup startup; /* Zero-initialized => indefinite menu. */
    enum xpl_menu_action quick_target; /* eMMC / SD / TWRP only. */
    const char *version;           /* Copied, at most 47 bytes. */
    const char *sd_directory;      /* Display only, copied, at most 255 bytes. */
    const char *language;          /* Requested locale, copied, at most 15 bytes. */
};

typedef struct {
    xpl_menu_action_fn action;
    void *action_context;
    uint64_t last_ms, candidate_since, repeat_since, power_since, error_since;
    uint32_t available_mask, error_code;
    enum xpl_menu_startup startup;
    enum xpl_menu_action quick_target;
    enum xpl_menu_page page;
    enum xpl_menu_page error_return_page;
    enum xpl_menu_notice notice;
    uint8_t selected, raw_keys, stable_keys, repeat_key, repeat_fired;
    uint8_t power_armed, power_pending, dirty;
    uint8_t error_return_selected;
    char version[48], sd_directory[256], language[16];
    char error_message[128];
} xpl_menu;

/* The caller owns all storage. No heap, OS, device I/O, settings write, touch
 * driver, or framebuffer access occurs here. Service is an internal status
 * page and always available; other rows require both a mask bit and callback.
 * initial_keys is the first hardware snapshot. Held startup Power never
 * confirms an entry when released; any startup key also cancels QuickBoot.
 * QuickBoot calls the selected callback once during init only if requested,
 * valid, available, and no startup key is held. If it returns, menu stays.
 * A callback that successfully transfers execution normally does not return. */
void xpl_menu_init(xpl_menu *menu, const struct xpl_menu_config *config,
                   uint32_t available_mask, xpl_menu_action_fn action,
                   void *context, uint32_t initial_keys, uint64_t now_ms);
/* Poll with a monotonic millisecond timestamp and the UP/DOWN/POWER bitmask.
 * Unknown key bits are ignored. Clock regression resets debounce and cancels
 * a pending confirm. 15ms stable groups generate events; volume repeats use
 * bounded single steps (no catch-up storm). Both volumes/chords cancel Power.
 * Confirm happens on a short debounced release; held Power never repeats.
 * Return 1 if a redraw is needed. Time alone never exits AlwaysMenu. */
int xpl_menu_poll(xpl_menu *menu, uint32_t raw_keys, uint64_t now_ms);
void xpl_menu_mark_clean(xpl_menu *menu);
int xpl_menu_action_available(const xpl_menu *menu, enum xpl_menu_action action);
const char *xpl_menu_action_label(enum xpl_menu_action action);

/* Call only for a real caller-observed failure (for example missing /twrp.img),
 * not merely an unavailable driver. Message is copied/sanitized (127 bytes max).
 * Shows Back immediately and returns to the saved page/selection after 3000ms
 * or a fresh debounced Power press/release. An already-held Power is consumed.
 * Repeated errors refresh the deadline without losing the original return page.
 * Clock regression restarts the window. No sleeping, I/O or action callback.
 * A callback may call this before returning FAILED; activate preserves the page. */
void xpl_menu_show_error(xpl_menu *menu, const char *message,
                         uint32_t error_code, uint64_t now_ms);

/* Little-endian packed, linear RGB framebuffer; no rotation or hardware
 * refresh. The HAL owns mapping and display flush. 16/24/32-bit pixels and
 * disjoint contiguous channel masks are supported. Alpha may be zero.
 * Writes touch visible pixels only, not row padding. */
typedef struct {
    volatile uint8_t *pixels;
    uint64_t buffer_bytes;
    uint32_t width, height, stride_bytes;
    uint8_t bytes_per_pixel;
    uint32_t red_mask, green_mask, blue_mask, alpha_mask;
} xpl_fb;
enum xpl_fb_status { XPL_FB_OK = 0, XPL_FB_ARGUMENT = -1,
                     XPL_FB_BOUNDS = -2, XPL_FB_FORMAT = -3,
                     XPL_FB_TOO_SMALL = -4 };
int xpl_fb_validate(const xpl_fb *surface);
/* English ASCII prototype. Requested locale is shown in Service, without
 * claiming translated glyphs, touch, or saved settings exist. */
int xpl_menu_render(const xpl_menu *menu, const xpl_fb *surface);

#endif
