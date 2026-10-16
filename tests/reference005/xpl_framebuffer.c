/* SPDX-License-Identifier: MIT */
#include "xpl_menu.h"
#include "xpl_font5x7.h"

static int mask_contiguous(uint32_t mask)
{
    if (!mask) return 0;
    while (!(mask & 1u)) mask >>= 1;
    return !(mask & (mask + 1u));
}

int xpl_fb_validate(const xpl_fb *f)
{
    uint64_t row, needed, bits;
    uint32_t masks;
    if (!f || !f->pixels || !f->width || !f->height) return XPL_FB_ARGUMENT;
    if (f->bytes_per_pixel < 2 || f->bytes_per_pixel > 4) return XPL_FB_FORMAT;
    row = (uint64_t)f->width * f->bytes_per_pixel;
    if (row > f->stride_bytes) return XPL_FB_BOUNDS;
    needed = (uint64_t)f->stride_bytes * (f->height - 1u) + row;
    if (needed > f->buffer_bytes || needed > SIZE_MAX ||
        needed - 1u > UINTPTR_MAX - (uintptr_t)f->pixels) return XPL_FB_BOUNDS;
    masks = f->red_mask | f->green_mask | f->blue_mask;
    if (!mask_contiguous(f->red_mask) || !mask_contiguous(f->green_mask) ||
        !mask_contiguous(f->blue_mask) ||
        (f->alpha_mask && !mask_contiguous(f->alpha_mask)) ||
        (f->red_mask & f->green_mask) || (f->red_mask & f->blue_mask) ||
        (f->green_mask & f->blue_mask) || (masks & f->alpha_mask)) return XPL_FB_FORMAT;
    bits = (UINT64_C(1) << (f->bytes_per_pixel * 8u)) - 1u;
    if (((uint64_t)masks | f->alpha_mask) & ~bits) return XPL_FB_FORMAT;
    return XPL_FB_OK;
}

static uint32_t channel(uint8_t color, uint32_t mask)
{
    unsigned shift = 0;
    uint32_t maximum;
    if (!mask) return 0;
    while (!((mask >> shift) & 1u)) ++shift;
    maximum = mask >> shift;
    return (uint32_t)(((uint64_t)color * maximum + 127u) / 255u) << shift;
}

static uint32_t color(const xpl_fb *f, uint32_t rgb)
{
    return channel((uint8_t)(rgb >> 16), f->red_mask) |
           channel((uint8_t)(rgb >> 8), f->green_mask) |
           channel((uint8_t)rgb, f->blue_mask) | f->alpha_mask;
}

static void rectangle(const xpl_fb *f, uint32_t x, uint32_t y,
                      uint32_t width, uint32_t height, uint32_t pixel)
{
    uint32_t row, col;
    if (x >= f->width || y >= f->height) return;
    if (width > f->width - x) width = f->width - x;
    if (height > f->height - y) height = f->height - y;
    for (row = 0; row < height; ++row) {
        volatile uint8_t *p = f->pixels + (size_t)((uint64_t)(y + row) * f->stride_bytes +
                                                  (uint64_t)x * f->bytes_per_pixel);
        if (f->bytes_per_pixel == 4 && !((uintptr_t)p & 3u)) {
            volatile uint32_t *q = (volatile uint32_t *)p;
            for (col = 0; col < width; ++col) q[col] = pixel;
        } else if (f->bytes_per_pixel == 2 && !((uintptr_t)p & 1u)) {
            volatile uint16_t *q = (volatile uint16_t *)p;
            for (col = 0; col < width; ++col) q[col] = (uint16_t)pixel;
        } else {
            for (col = 0; col < width; ++col) {
                unsigned b;
                for (b = 0; b < f->bytes_per_pixel; ++b) *p++ = (uint8_t)(pixel >> (b * 8u));
            }
        }
    }
}

static void glyph(const xpl_fb *f, uint32_t x, uint32_t y, unsigned char c,
                  uint32_t scale, uint32_t pixel)
{
    unsigned row, col;
    if (c < 32 || c > 126) c = '?';
    for (row = 0; row < 7; ++row) {
        uint8_t pattern = xpl_font5x7[c - 32][row];
        for (col = 0; col < 5; ++col)
            if (pattern & (1u << (4u - col)))
                rectangle(f, x + col * scale, y + row * scale, scale, scale, pixel);
    }
}

static void text(const xpl_fb *f, uint32_t x, uint32_t y, const char *s,
                 size_t limit, uint32_t scale, uint32_t pixel)
{
    size_t i;
    for (i = 0; i < limit && s[i]; ++i) {
        uint64_t at = (uint64_t)x + i * (uint64_t)6u * scale;
        if (at >= f->width || (uint64_t)y + 7u * scale > f->height) break;
        glyph(f, (uint32_t)at, y, (unsigned char)s[i], scale, pixel);
    }
}

static size_t append(char *out, size_t used, size_t capacity, const char *s, size_t limit)
{
    size_t i = 0;
    while (i < limit && s[i] && used + 1 < capacity) out[used++] = s[i++];
    out[used] = 0;
    return used;
}

static const char *notice_text(enum xpl_menu_notice notice)
{
    switch (notice) {
    case XPL_MENU_NOTICE_PENDING: return "Pending: action not connected.";
    case XPL_MENU_NOTICE_RETURNED: return "Action returned to this menu.";
    case XPL_MENU_NOTICE_FAILED: return "Action failed; menu remains.";
    case XPL_MENU_NOTICE_QUICK_RETURNED: return "QuickBoot returned; menu remains.";
    case XPL_MENU_NOTICE_INVALID_CONFIG: return "Invalid config; AlwaysMenu.";
    case XPL_MENU_NOTICE_HELD_FOR_MENU: return "Held key: QuickBoot cancelled.";
    case XPL_MENU_NOTICE_LONG_POWER: return "Long Power ignored; press briefly.";
    default: return "Select an entry. No boot timeout.";
    }
}

static void service(const xpl_menu *m, const xpl_fb *f, uint32_t s,
                    uint32_t fg, uint32_t muted, uint32_t accent)
{
    uint32_t y = 48u * s, margin = 4u * s;
    size_t columns = (f->width - margin * 2u) / (6u * s), at = 0;
    unsigned lines;
    char label[64];
    text(f, margin, y, "Version:", 8, s, muted); y += 11u * s;
    text(f, margin, y, m->version, sizeof m->version, s, fg); y += 16u * s;
    text(f, margin, y, "SD directory (configured):", 26, s, muted); y += 11u * s;
    for (lines = 0; lines < 3 && at < sizeof m->sd_directory && m->sd_directory[at]; ++lines) {
        size_t n = 0;
        while (n < columns && at + n < sizeof m->sd_directory && m->sd_directory[at + n]) ++n;
        text(f, margin, y, m->sd_directory + at, n, s, fg);
        at += n; y += 11u * s;
    }
    if (at < sizeof m->sd_directory && m->sd_directory[at]) {
        text(f, margin, y, "... (display shortened)", 24, s, muted); y += 11u * s;
    }
    y += 5u * s;
    text(f, margin, y, m->startup == XPL_MENU_QUICK ? "Startup: QuickBoot config" : "Startup: AlwaysMenu", 27, s, fg);
    y += 15u * s;
    text(f, margin, y, "Language: English prototype", 28, s, fg); y += 11u * s;
    {
        size_t used = append(label, 0, sizeof label, "Requested locale: ", 19);
        (void)append(label, used, sizeof label, m->language, sizeof m->language);
        text(f, margin, y, label, sizeof label, s, muted); y += 15u * s;
    }
    text(f, margin, y, "Other language glyphs: pending", 31, s, accent); y += 11u * s;
    text(f, margin, y, "Settings persistence: pending", 31, s, accent); y += 11u * s;
    text(f, margin, y, "Touch controls: pending", 24, s, accent); y += 16u * s;
    text(f, margin, y, "Power: back to main menu", 25, s, fg);
}

static void error_page(const xpl_menu *m, const xpl_fb *f, uint32_t s,
                       uint32_t fg, uint32_t muted, uint32_t accent, uint32_t selected)
{
    static const char hex[] = "0123456789abcdef";
    char code[9];
    uint32_t margin = 4u * s, y = 48u * s;
    size_t columns = (f->width - margin * 2u) / (6u * s), at = 0;
    for (unsigned i = 0; i < 8; ++i) code[i] = hex[(m->error_code >> ((7u - i) * 4u)) & 15u];
    code[8] = 0;
    text(f, margin, y, "Code: 0x", 8, s, muted);
    text(f, margin + 48u * s, y, code, sizeof code, s, muted); y += 20u * s;
    for (unsigned line = 0; line < 4 && at < sizeof m->error_message && m->error_message[at]; ++line) {
        size_t n = 0;
        while (n < columns && at + n < sizeof m->error_message && m->error_message[at + n]) ++n;
        text(f, margin, y, m->error_message + at, n, s, accent);
        at += n; y += 12u * s;
    }
    rectangle(f, margin, 145u * s, f->width - margin * 2u, 24u * s, selected);
    text(f, margin + 4u * s, 153u * s, "< Back", 6, s, fg);
    text(f, margin, 182u * s, "Power: press and release for Back", 33, s, fg);
    text(f, margin, 198u * s, "Auto Back after 3 seconds.", 26, s, muted);
}

int xpl_menu_render(const xpl_menu *m, const xpl_fb *f)
{
    uint32_t s, margin, fg, muted, accent, selected, background;
    unsigned row;
    int r = xpl_fb_validate(f);
    if (r) return r;
    if (!m) return XPL_FB_ARGUMENT;
    /* Bounded drawing arithmetic and practical prototype display sizes. */
    if (f->width < 216 || f->height < 280) return XPL_FB_TOO_SMALL;
    if (f->width > 8192 || f->height > 16384) return XPL_FB_BOUNDS;
    s = f->width / 216u;
    if (s > f->height / 280u) s = f->height / 280u;
    margin = 4u * s;
    fg = color(f, 0xeff4f6); muted = color(f, 0x8295a3); accent = color(f, 0xe3ae60);
    selected = color(f, 0x284a60); background = color(f, 0x101820);
    rectangle(f, 0, 0, f->width, f->height, background);
    text(f, margin, 5u * s, m->page == XPL_MENU_ERROR_PAGE ? "Error" :
         m->page == XPL_MENU_SERVICE_PAGE ? "Service" : "XPLoader", 8, s * 2u, fg);
    text(f, margin, 25u * s, "E6653 / English prototype", 26, s, muted);
    rectangle(f, margin, 39u * s, f->width - margin * 2u, s, selected);
    if (m->page == XPL_MENU_ERROR_PAGE) error_page(m, f, s, fg, muted, accent, selected);
    else if (m->page == XPL_MENU_SERVICE_PAGE) service(m, f, s, fg, muted, accent);
    else {
        for (row = 0; row < XPL_MENU_ACTION_COUNT; ++row) {
            char label[48]; size_t used = 0;
            uint32_t y = (48u + row * 24u) * s;
            int available = xpl_menu_action_available(m, (enum xpl_menu_action)row);
            if (m->selected == row) rectangle(f, margin, y, f->width - margin * 2u, 22u * s, selected);
            label[used++] = (char)('1' + row); label[used++] = '.'; label[used++] = ' '; label[used] = 0;
            used = append(label, used, sizeof label, xpl_menu_action_label((enum xpl_menu_action)row), 24);
            if (!available) (void)append(label, used, sizeof label, " [pending]", 10);
            text(f, margin + 2u * s, y + 7u * s, label, sizeof label, s, available ? fg : accent);
        }
        text(f, margin, 201u * s, notice_text(m->notice), 48, s,
             m->notice == XPL_MENU_NOTICE_NONE ? muted : accent);
    }
    rectangle(f, margin, f->height - 38u * s, f->width - margin * 2u, s, selected);
    text(f, margin, f->height - 31u * s, m->page == XPL_MENU_ERROR_PAGE ? "Power: Back" : "VOL +/-: move   Power: select", 30, s, fg);
    text(f, margin, f->height - 19u * s, m->page == XPL_MENU_ERROR_PAGE ? "Release held Power first." : "Release startup Power first.", 29, s, muted);
    text(f, margin, f->height - 8u * s, "Touch / saved settings pending", 32, s, muted);
    return XPL_FB_OK;
}
