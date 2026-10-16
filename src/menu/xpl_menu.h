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
enum xpl_menu_service_action {XPL_SERVICE_PC_SD=0,XPL_SERVICE_FASTBOOT=1};
typedef enum xpl_menu_action_result (*xpl_menu_service_fn)(void *,enum xpl_menu_service_action);
enum xpl_menu_startup { XPL_MENU_ALWAYS = 0, XPL_MENU_QUICK = 1 };
enum xpl_menu_page { XPL_MENU_MAIN = 0, XPL_MENU_SERVICE_PAGE = 1, XPL_MENU_ERROR_PAGE = 2,
                     XPL_MENU_FOLDERS_PAGE = 3, XPL_MENU_BOOT_SETTINGS_PAGE = 4,XPL_MENU_PC_SD_PAGE=5,XPL_MENU_QUICK_TARGET_PAGE=6,
                     XPL_MENU_LANGUAGE_PAGE=7,XPL_MENU_TOOLS_PAGE=8,XPL_MENU_INFO_PAGE=9,XPL_MENU_ONCE_PAGE=10,
                     XPL_MENU_RESCUE_PAGE=11,XPL_MENU_DEFAULTS_PAGE=12 };
enum xpl_menu_tool {XPL_TOOL_PAYLOAD=0,XPL_TOOL_STORAGE=1,XPL_TOOL_LOG=2,XPL_TOOL_ABOUT=3};
#define XPL_MENU_FOLDER_MAX 32u
typedef struct {char label[128],path[256];uint8_t kind;} xpl_menu_folder_entry;
typedef void (*xpl_menu_folder_select_fn)(void *context,unsigned selected);
typedef void (*xpl_menu_folder_open_fn)(void *context);
enum xpl_menu_notice {
    XPL_MENU_NOTICE_NONE = 0, XPL_MENU_NOTICE_PENDING,
    XPL_MENU_NOTICE_RETURNED, XPL_MENU_NOTICE_FAILED,
    XPL_MENU_NOTICE_QUICK_RETURNED, XPL_MENU_NOTICE_INVALID_CONFIG,
    XPL_MENU_NOTICE_HELD_FOR_MENU, XPL_MENU_NOTICE_LONG_POWER, XPL_MENU_NOTICE_LAB_AUTO,
    XPL_MENU_NOTICE_LAB_SD_AUTO, XPL_MENU_NOTICE_LAB_TWRP_AUTO,XPL_MENU_NOTICE_S1_REBOOT,
    XPL_MENU_NOTICE_LAB_S1_AUTO,XPL_MENU_NOTICE_LAB_GUARD_AUTO,XPL_MENU_NOTICE_LAB_PC_SD_AUTO,
    XPL_MENU_NOTICE_SETTINGS_SAVE,XPL_MENU_NOTICE_SETTINGS_SAVED
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
    const xpl_menu_folder_entry *folders;
    xpl_menu_folder_select_fn folder_select;
    void *folder_context;
    uint32_t folder_generation;
    uint8_t folder_count,folder_scanning;
    char folder_path[256];
    xpl_menu_folder_open_fn folder_open;
    xpl_menu_service_fn service_action;
    void *service_context;
    uint32_t service_available;
    void (*settings_save)(void *,unsigned,unsigned,const char *);
    void *settings_context;
    void (*language_save)(void *,const char *);
    void (*defaults_save)(void *);
    void (*tools_action)(void *,enum xpl_menu_tool);
    void *extended_context;
    const char (*info_lines)[96];
    char info_title[32];
    uint32_t info_generation;
    uint8_t info_count,info_return_selected;
} xpl_menu;
void xpl_menu_set_extended(xpl_menu *,void (*)(void *,const char *),void (*)(void *),void (*)(void *,enum xpl_menu_tool),void *);
void xpl_menu_show_info(xpl_menu *,const char *title,const char (*lines)[96],unsigned count,unsigned return_selected);
void xpl_menu_set_settings(xpl_menu *,void (*)(void *,unsigned,unsigned,const char *),void *);
void xpl_menu_set_services(xpl_menu *,uint32_t,xpl_menu_service_fn,void *);
/* Caller-owned list remains valid until replaced. No filesystem work here.
 * Row zero is caller-supplied Back; initial selection is explicit, never boots.
 * A scan page consumes confirmations; callback runs only on a short release. */
void xpl_menu_show_folders(xpl_menu *menu,const char *path,
    const xpl_menu_folder_entry *entries,unsigned count,unsigned selected,
    unsigned scanning,xpl_menu_folder_select_fn callback,void *context);

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

/* MAGLDR tribute boot page, separate from navigation state. ACCEPTED redraws
 * the page; HANDOFF only updates its final line and requires that page first.
 * Only the native caller committing to a real handoff may display GO GO GO.
 * Neither phase claims the destination kernel has executed. */
enum xpl_boot_phase {
    XPL_BOOT_ACCEPTED = 0, XPL_BOOT_HANDOFF = 1,
    XPL_BOOT_COUNT_3 = 2, XPL_BOOT_COUNT_2 = 3, XPL_BOOT_COUNT_1 = 4
};
int xpl_boot_render(const xpl_fb *surface, enum xpl_boot_phase phase);
int xpl_boot_render_target(const xpl_fb *surface,enum xpl_boot_phase phase,
                           enum xpl_menu_action target);

enum xpl_menu_render_kind {
    XPL_MENU_RENDER_NONE = 0, XPL_MENU_RENDER_FULL = 1, XPL_MENU_RENDER_PARTIAL = 2
};
typedef struct {
    xpl_menu previous;
    xpl_fb surface;
    uint32_t previous_available_mask;
    uint32_t row_mask; /* Main-menu rows written by the most recent call. */
    enum xpl_menu_render_kind kind;
    uint8_t notice_updated, valid;
} xpl_menu_render_cache;

/* Caller-owned cache, no heap/global state. Invalidate before first use or if
 * another writer changed the framebuffer. A zero-initialized cache is valid.
 * Full repaint for first frame, page/layout/config changes; main-page selection,
 * availability and notices repaint only the affected rows/band. NONE means no
 * pixel stores and needs no HAL present. Cache is committed only on success.
 * RGB888 fills use bounded aligned32-bit groups with whole-pixel prefix/tails;
 * this does not change framebuffer format, mapping, cache or device registers. */
void xpl_menu_render_invalidate(xpl_menu_render_cache *cache);
/* One native display, canonical built-in locale; also used before QuickBoot. */
void xpl_menu_render_set_locale(const char *language);
int xpl_menu_render_update(const xpl_menu *menu, const xpl_fb *surface,
                           xpl_menu_render_cache *cache);

#endif
