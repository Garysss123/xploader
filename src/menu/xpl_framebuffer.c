/* SPDX-License-Identifier: MIT */
#include "xpl_menu.h"
#include "xpl_font5x7.h"
#include "xpl_locale.h"
static const char *render_language="en";
void xpl_menu_render_set_locale(const char *language){render_language=xpl_locale_canonical(language);}

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
        } else if (f->bytes_per_pixel == 3) {
            uint32_t remaining = width;
            uint32_t rgb = pixel & 0xffffffu;
            uint32_t words[3];
            words[0] = rgb | (rgb << 24);
            words[1] = (rgb >> 8) | (rgb << 16);
            words[2] = (rgb >> 16) | (rgb << 8);
            /* Preserve byte phase by consuming complete pixels until aligned.
             * Four RGB888 pixels then occupy exactly three32-bit stores. */
            while (remaining && ((uintptr_t)p & 3u)) {
                p[0] = (uint8_t)rgb; p[1] = (uint8_t)(rgb >> 8); p[2] = (uint8_t)(rgb >> 16);
                p += 3; --remaining;
            }
            while (remaining >= 4) {
                volatile uint32_t *q = (volatile uint32_t *)p;
                q[0] = words[0]; q[1] = words[1]; q[2] = words[2];
                p += 12; remaining -= 4;
            }
            while (remaining--) {
                p[0] = (uint8_t)rgb; p[1] = (uint8_t)(rgb >> 8); p[2] = (uint8_t)(rgb >> 16);
                p += 3;
            }
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

static void text_raw(const xpl_fb *f, uint32_t x, uint32_t y, const char *s,
                 size_t limit, uint32_t scale, uint32_t pixel)
{
    size_t i=0;uint64_t at=x;
    while(i<limit&&s[i]){
        if (at >= f->width || (uint64_t)y + 7u * scale > f->height) break;
        size_t used;uint32_t cp=xpl_utf8_next(s+i,limit-i,&used);
        if(!used)break;
        i+=used;
        const xpl_cjk_glyph *g=cp>126?xpl_locale_glyph(cp):0;
        if(g){
            for(unsigned row=0;row<XPL_CJK_EDGE;row++)for(unsigned col=0;col<XPL_CJK_EDGE;col++)if(g->row[row]&(1u<<(XPL_CJK_EDGE-1u-col))){
                uint32_t gx=col*7u*scale/XPL_CJK_EDGE,gy=row*7u*scale/XPL_CJK_EDGE;
                rectangle(f,(uint32_t)at+gx,y+gy,(col+1u)*7u*scale/XPL_CJK_EDGE-gx,(row+1u)*7u*scale/XPL_CJK_EDGE-gy,pixel);
            }
            at+=8u*(uint64_t)scale;
        }else{glyph(f,(uint32_t)at,y,(unsigned char)(cp>126?'?':cp),scale,pixel);at+=6u*(uint64_t)scale;}
    }
}
static void text(const xpl_fb *f,uint32_t x,uint32_t y,const char *s,size_t limit,uint32_t scale,uint32_t pixel){
    const char *translated=xpl_locale_translate(render_language,s);
    if(translated!=s){s=translated;limit=512;}
    text_raw(f,x,y,s,limit,scale,pixel);
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
    case XPL_MENU_NOTICE_LAB_AUTO: return "LAB TEST: auto eMMC in 5 seconds.";
    case XPL_MENU_NOTICE_LAB_SD_AUTO: return "LAB TEST: auto SD in 5 seconds.";
    case XPL_MENU_NOTICE_LAB_TWRP_AUTO: return "LAB TEST: auto recovery in 5 seconds.";
    case XPL_MENU_NOTICE_S1_REBOOT: return "Returning to Sony S1 Fastboot...";
    case XPL_MENU_NOTICE_LAB_S1_AUTO: return "LAB TEST: auto Sony S1 in 5 seconds.";
    case XPL_MENU_NOTICE_LAB_GUARD_AUTO: return "LAB TEST: watchdog return to Sony S1.";
    case XPL_MENU_NOTICE_LAB_PC_SD_AUTO: return "LAB TEST: PC SD in 5s, then recovery.";
    case XPL_MENU_NOTICE_SETTINGS_SAVE:return "Saving boot settings...";
    case XPL_MENU_NOTICE_SETTINGS_SAVED:return "Boot settings saved for next start.";
    default: return "Select an entry. No boot timeout.";
    }
}

static void main_row(const xpl_menu *m, const xpl_fb *f, uint32_t s, unsigned row,
                     uint32_t fg, uint32_t accent, uint32_t selected,
                     uint32_t background, unsigned erase)
{
    char label[48]; size_t used = 0;
    uint32_t margin = 4u * s, y = (48u + row * 24u) * s;
    int available = xpl_menu_action_available(m, (enum xpl_menu_action)row);
    if (erase || m->selected == row)
        rectangle(f, margin, y, f->width - margin * 2u, 22u * s,
                  m->selected == row ? selected : background);
    label[used++] = (char)('1' + row); label[used++] = '.'; label[used++] = ' '; label[used] = 0;
    used = append(label, used, sizeof label, xpl_menu_action_label((enum xpl_menu_action)row), 24);
    if (!available) (void)append(label, used, sizeof label, " [pending]", 10);
    text(f, margin + 2u * s, y + 7u * s, label, sizeof label, s, available ? fg : accent);
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

static void folder_page(const xpl_menu *m,const xpl_fb *f,uint32_t s,uint32_t fg,
    uint32_t muted,uint32_t selected)
{
    uint32_t margin=4u*s;
    text(f,margin,46u*s,m->folder_scanning?"Reading SD directories...":"Choose the SD boot directory",29,s,muted);
    text_raw(f,margin,61u*s,m->folder_path,sizeof m->folder_path,s,fg);
    unsigned first=(m->selected/6u)*6u;
    for(unsigned row=0;row<6u&&first+row<m->folder_count;row++) {
        unsigned item=first+row;uint32_t y=(78u+row*22u)*s;
        if(item==m->selected)rectangle(f,margin,y,f->width-2u*margin,20u*s,selected);
        text_raw(f,margin+4u*s,y+6u*s,m->folders[item].label,sizeof m->folders[item].label,s,fg);
    }
    text(f,margin,220u*s,"VOL: move  Power: open / select",31,s,muted);
}

static void settings_page(const xpl_menu *m,const xpl_fb *f,uint32_t s,uint32_t fg,
    uint32_t muted,uint32_t selected)
{
    static const char *const service_rows[]={"1. Boot Settings","2. Language [pending]","3. < Back"};
    const char *const service_extended[]={"1. Boot Settings",m->language_save?"2. Language":"2. Language [pending]",
        m->service_available&1u?"3. PC SD Access":"3. PC SD Access [pending]",
        m->service_available&2u?"4. Fastboot (Sony S1)":"4. Fastboot [pending]",m->tools_action?"5. Tools":"5. < Back","6. < Back"};
    const char *const boot_rows[]={m->settings_save?(m->startup==XPL_MENU_QUICK?"1. QuickBoot [on]":"1. QuickBoot"):"1. QuickBoot [pending]",
        m->settings_save?(m->startup==XPL_MENU_ALWAYS?"2. Always Menu [on]":"2. Always Menu"):"2. Always Menu [pending]","3. AD SD Dir","< Back"};
    static const char *const quick_rows[]={"1. Boot eMMC","2. Boot SD","3. TWRP","< Back"};
    unsigned extended=m->page==XPL_MENU_SERVICE_PAGE&&m->service_action;
    const char *const *rows=m->page==XPL_MENU_QUICK_TARGET_PAGE?quick_rows:m->page==XPL_MENU_BOOT_SETTINGS_PAGE?boot_rows:extended?service_extended:service_rows;
    unsigned count=(m->page==XPL_MENU_BOOT_SETTINGS_PAGE||m->page==XPL_MENU_QUICK_TARGET_PAGE)?4u:extended?(m->tools_action?6u:5u):3u;uint32_t margin=4u*s;
    for(unsigned row=0;row<count;row++) {
        unsigned step=m->tools_action&&extended?22u:26u;uint32_t y=(49u+row*step)*s;
        if(row==m->selected)rectangle(f,margin,y,f->width-2u*margin,(step-2u)*s,selected);
        text(f,margin+4u*s,y+(m->tools_action&&extended?6u:8u)*s,rows[row],32,s,fg);
    }
    text(f,margin,(extended?190u:175u)*s,"Current SD directory:",22,s,muted);
    text_raw(f,margin,(extended?206u:191u)*s,m->sd_directory,sizeof m->sd_directory,s,fg);
    text(f,margin,(extended?240u:219u)*s,m->notice==XPL_MENU_NOTICE_S1_REBOOT||m->notice==XPL_MENU_NOTICE_SETTINGS_SAVE||m->notice==XPL_MENU_NOTICE_SETTINGS_SAVED?
        notice_text(m->notice):m->settings_save?"Saved on SD: /xploader.cfg":"Settings persistence: pending",40,s,muted);
}
static void extended_page(const xpl_menu *m,const xpl_fb *f,uint32_t s,uint32_t fg,uint32_t muted,uint32_t selected){
    static const char *const tools[]={"1. Boot Once","2. Payload Info","3. Storage Info","4. Boot Log","5. Rescue","6. Reset Settings","7. About","8. < Back"};
    static const char *const language[]={"1. English","2. 繁體中文","< Back"};
    static const char *const once[]={"1. Boot eMMC","2. Boot SD","3. TWRP","< Back"};
    static const char *const rescue[]={"1. SD TWRP","2. Sony S1 Fastboot","< Back"};
    static const char *const defaults[]={"1. Restore Defaults","2. < Back"};
    uint32_t margin=4u*s;
    if(m->page==XPL_MENU_INFO_PAGE){
        text(f,margin,49u*s,m->info_title,sizeof m->info_title,s,fg);
        for(unsigned n=0;n<m->info_count&&n<12;n++)text_raw(f,margin,(66u+n*12u)*s,m->info_lines[n],96,s,muted);
        rectangle(f,margin,220u*s,f->width-2u*margin,20u*s,selected);text(f,margin+4u*s,226u*s,"< Back",8,s,fg);return;
    }
    unsigned count;const char *const *rows;
    if(m->page==XPL_MENU_LANGUAGE_PAGE){rows=language;count=3;}
    else if(m->page==XPL_MENU_TOOLS_PAGE){rows=tools;count=8;}
    else if(m->page==XPL_MENU_ONCE_PAGE){rows=once;count=4;}
    else if(m->page==XPL_MENU_RESCUE_PAGE){rows=rescue;count=3;}
    else {rows=defaults;count=2;text(f,margin,46u*s,"Restore default settings?",26,s,muted);}
    for(unsigned row=0;row<count;row++){
        uint32_t y=(m->page==XPL_MENU_DEFAULTS_PAGE?70u:49u)+row*21u;y*=s;
        if(row==m->selected)rectangle(f,margin,y,f->width-2u*margin,19u*s,selected);
        text(f,margin+4u*s,y+6u*s,rows[row],64,s,fg);
    }
    text(f,margin,230u*s,m->notice==XPL_MENU_NOTICE_SETTINGS_SAVE||m->notice==XPL_MENU_NOTICE_SETTINGS_SAVED?notice_text(m->notice):
        m->page==XPL_MENU_DEFAULTS_PAGE?"Only loader settings change.":m->page==XPL_MENU_LANGUAGE_PAGE?m->language:"",64,s,muted);
}

int xpl_menu_render(const xpl_menu *m, const xpl_fb *f)
{
    uint32_t s, margin, fg, muted, accent, selected, background;
    unsigned row;
    int r = xpl_fb_validate(f);
    if (r) return r;
    if (!m) return XPL_FB_ARGUMENT;
    xpl_menu_render_set_locale(m->language);
    if(m->page==XPL_MENU_FOLDERS_PAGE&&(!m->folders||!m->folder_count||
       m->folder_count>XPL_MENU_FOLDER_MAX||m->selected>=m->folder_count))return XPL_FB_ARGUMENT;
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
         m->page == XPL_MENU_SERVICE_PAGE ? "Service" :m->page==XPL_MENU_FOLDERS_PAGE?"SD Dir":
         m->page==XPL_MENU_BOOT_SETTINGS_PAGE?"Boot Set":m->page==XPL_MENU_QUICK_TARGET_PAGE?"QuickBoot":m->page==XPL_MENU_PC_SD_PAGE?"PC SD":
         m->page==XPL_MENU_LANGUAGE_PAGE?"Language":m->page==XPL_MENU_TOOLS_PAGE?"Tools":m->page==XPL_MENU_INFO_PAGE?"Info":
         m->page==XPL_MENU_ONCE_PAGE?"Boot Once":m->page==XPL_MENU_RESCUE_PAGE?"Rescue":m->page==XPL_MENU_DEFAULTS_PAGE?"Settings":"XPLoader", 10, s * 2u, fg);
    text(f, margin, 25u * s, "E6653 / English prototype", 26, s, muted);
    rectangle(f, margin, 39u * s, f->width - margin * 2u, s, selected);
    if(m->page==XPL_MENU_PC_SD_PAGE){
        text(f,margin,60u*s,"SD card access over USB",28,s,fg);
        text(f,margin,86u*s,"Use Safely Remove on PC.",28,s,fg);
        text(f,margin,112u*s,"Then wait for this page",28,s,muted);
        text(f,margin,128u*s,"to close before booting.",28,s,muted);
        text(f,margin,164u*s,"USB session: 180 seconds",28,s,accent);
    }
    else if (m->page == XPL_MENU_ERROR_PAGE) error_page(m, f, s, fg, muted, accent, selected);
    else if (m->page == XPL_MENU_SERVICE_PAGE&&!m->folder_open) service(m, f, s, fg, muted, accent);
    else if(m->page==XPL_MENU_SERVICE_PAGE||m->page==XPL_MENU_BOOT_SETTINGS_PAGE||m->page==XPL_MENU_QUICK_TARGET_PAGE)settings_page(m,f,s,fg,muted,selected);
    else if (m->page==XPL_MENU_FOLDERS_PAGE)folder_page(m,f,s,fg,muted,selected);
    else if(m->page>=XPL_MENU_LANGUAGE_PAGE&&m->page<=XPL_MENU_DEFAULTS_PAGE)extended_page(m,f,s,fg,muted,selected);
    else {
        for (row = 0; row < XPL_MENU_ACTION_COUNT; ++row)
            main_row(m, f, s, row, fg, accent, selected, background, 0);
        text(f, margin, 201u * s, notice_text(m->notice), 48, s,
             m->notice == XPL_MENU_NOTICE_NONE ? muted : accent);
    }
    rectangle(f, margin, f->height - 38u * s, f->width - margin * 2u, s, selected);
    text(f, margin, f->height - 31u * s, m->page==XPL_MENU_PC_SD_PAGE?"Safely remove on PC first.":m->page == XPL_MENU_ERROR_PAGE ? "Power: Back" : "VOL +/-: move   Power: select", 30, s, fg);
    text(f, margin, f->height - 19u * s, m->page==XPL_MENU_PC_SD_PAGE?"Please keep USB connected.":m->page == XPL_MENU_ERROR_PAGE ? "Release held Power first." : "Release startup Power first.", 29, s, muted);
    text(f, margin, f->height - 8u * s, m->settings_save?"Saved settings on SD":"Touch / saved settings pending", 32, s, muted);
    return XPL_FB_OK;
}

static int equal_bytes(const void *a, const void *b, size_t size)
{
    const uint8_t *x = a, *y = b;
    while (size--) if (*x++ != *y++) return 0;
    return 1;
}

int xpl_boot_render_target(const xpl_fb *f, enum xpl_boot_phase phase,
                           enum xpl_menu_action target)
{
    uint32_t s, margin, white, black;
    int r = xpl_fb_validate(f);
    if (r) return r;
    if ((unsigned)phase > XPL_BOOT_COUNT_1) return XPL_FB_ARGUMENT;
    if (target!=XPL_MENU_BOOT_EMMC&&target!=XPL_MENU_TWRP&&target!=XPL_MENU_BOOT_SD) return XPL_FB_ARGUMENT;
    if (f->width < 216 || f->height < 280) return XPL_FB_TOO_SMALL;
    if (f->width > 8192 || f->height > 16384) return XPL_FB_BOUNDS;
    s = f->width / 216u;
    if (s > f->height / 280u) s = f->height / 280u;
    margin = 4u * s;
    white = color(f, 0xffffff); black = color(f, 0x000000);
    if (phase == XPL_BOOT_ACCEPTED) {
        rectangle(f, 0, 0, f->width, f->height, black);
        text(f, margin, 6u * s, "XPLoader", 8, 2u * s, white);
        text(f, margin, 35u * s, target==XPL_MENU_TWRP?"Booting SD TWRP...":target==XPL_MENU_BOOT_SD?"Booting SD...":"Booting eMMC...", 18, s, white);
        text(f, margin, 53u * s, target==XPL_MENU_BOOT_EMMC?"Kernel and ramdisk: already in RAM":"Loading kernel and ramdisk...", 34, s, white);
        text(f, margin, 71u * s, "Native loader / before kernel", 29, s, white);
        text(f, margin, 116u * s, "MAGLDR / HD2 tribute", 20, s, white);
        /* Temporary crop diagnostics: actual drawable viewport corners. */
        uint32_t marker=s>1?s/2u:1u;
        rectangle(f,0,0,f->width,1,white);
        rectangle(f,0,f->height-1,f->width,1,white);
        rectangle(f,0,0,1,f->height,white);
        rectangle(f,f->width-1,0,1,f->height,white);
        uint32_t inset=4u*s;
        text(f,inset,inset,"TL",2,marker,white);
        text(f,f->width-12u*marker-inset,inset,"TR",2,marker,white);
        text(f,inset,f->height-7u*marker-inset,"BL",2,marker,white);
        text(f,f->width-12u*marker-inset,f->height-7u*marker-inset,"BR",2,marker,white);
    } else {
        rectangle(f, margin, 89u * s, f->width - margin * 2u, 14u * s, black);
        const char *line=phase==XPL_BOOT_COUNT_3 ? "3" :
                         phase==XPL_BOOT_COUNT_2 ? "2" :
                         phase==XPL_BOOT_COUNT_1 ? "1" : "GO GO GO";
        text(f, margin, 89u * s, line, 8, 2u * s, white);
    }
    return XPL_FB_OK;
}

int xpl_boot_render(const xpl_fb *f,enum xpl_boot_phase phase)
{
    return xpl_boot_render_target(f,phase,XPL_MENU_BOOT_EMMC);
}

static void copy_snapshot(void *out, const void *in, size_t size)
{
    /* Volatile byte stores keep this freestanding under size-optimized builds;
     * an aggregate assignment may introduce a compiler memcpy dependency. */
    volatile uint8_t *dst = out;
    const uint8_t *src = in;
    while (size--) *dst++ = *src++;
}

static int same_surface(const xpl_fb *a, const xpl_fb *b)
{
    return a->pixels == b->pixels && a->buffer_bytes == b->buffer_bytes &&
        a->width == b->width && a->height == b->height && a->stride_bytes == b->stride_bytes &&
        a->bytes_per_pixel == b->bytes_per_pixel && a->red_mask == b->red_mask &&
        a->green_mask == b->green_mask && a->blue_mask == b->blue_mask && a->alpha_mask == b->alpha_mask;
}

static int same_config(const xpl_menu *a, const xpl_menu *b)
{
    return a->startup == b->startup && a->quick_target == b->quick_target &&
        equal_bytes(a->version, b->version, sizeof a->version) &&
        equal_bytes(a->sd_directory, b->sd_directory, sizeof a->sd_directory) &&
        equal_bytes(a->language, b->language, sizeof a->language);
}

void xpl_menu_render_invalidate(xpl_menu_render_cache *cache)
{
    if (!cache) return;
    cache->valid = cache->notice_updated = 0;
    cache->row_mask = 0;
    cache->kind = XPL_MENU_RENDER_NONE;
}

int xpl_menu_render_update(const xpl_menu *m, const xpl_fb *f, xpl_menu_render_cache *cache)
{
    uint32_t available = 0, rows = 0, s;
    unsigned full, notice = 0;
    int r = xpl_fb_validate(f);
    if (r) return r;
    if (!m || !cache) return XPL_FB_ARGUMENT;
    xpl_menu_render_set_locale(m->language);
    if (f->width < 216 || f->height < 280) return XPL_FB_TOO_SMALL;
    if (f->width > 8192 || f->height > 16384) return XPL_FB_BOUNDS;
    for (unsigned row = 0; row < XPL_MENU_ACTION_COUNT; ++row)
        if (xpl_menu_action_available(m, (enum xpl_menu_action)row)) available |= XPL_MENU_AVAILABLE(row);
    full = !cache->valid || !same_surface(f, &cache->surface) ||
        m->page != cache->previous.page || !same_config(m, &cache->previous);
    if(!full&&(m->page==XPL_MENU_FOLDERS_PAGE||m->page==XPL_MENU_BOOT_SETTINGS_PAGE||m->page==XPL_MENU_QUICK_TARGET_PAGE||
       (m->page>=XPL_MENU_LANGUAGE_PAGE&&m->page<=XPL_MENU_DEFAULTS_PAGE)||
       (m->page==XPL_MENU_SERVICE_PAGE&&m->folder_open)))
        full=m->selected!=cache->previous.selected||m->notice!=cache->previous.notice||
            m->folder_generation!=cache->previous.folder_generation||m->folder_scanning!=cache->previous.folder_scanning||m->info_generation!=cache->previous.info_generation;
    if (!full && m->page == XPL_MENU_MAIN) {
        if (m->selected >= XPL_MENU_ACTION_COUNT || cache->previous.selected >= XPL_MENU_ACTION_COUNT) full = 1;
        else if (m->selected != cache->previous.selected)
            rows = XPL_MENU_AVAILABLE(m->selected) | XPL_MENU_AVAILABLE(cache->previous.selected);
        rows |= available ^ cache->previous_available_mask;
        notice = m->notice != cache->previous.notice;
    } else if (!full && m->page == XPL_MENU_ERROR_PAGE) {
        full = m->error_code != cache->previous.error_code ||
            !equal_bytes(m->error_message, cache->previous.error_message, sizeof m->error_message);
    }
    if (full) {
        r = xpl_menu_render(m, f);
        if (r) return r;
        cache->kind = XPL_MENU_RENDER_FULL;
        cache->row_mask = m->page == XPL_MENU_MAIN ? (1u << XPL_MENU_ACTION_COUNT) - 1u : 0;
        cache->notice_updated = (uint8_t)(m->page == XPL_MENU_MAIN);
    } else if (rows || notice) {
        uint32_t fg = color(f, 0xeff4f6), accent = color(f, 0xe3ae60), selected = color(f, 0x284a60);
        uint32_t background = color(f, 0x101820), muted = color(f, 0x8295a3);
        s = f->width / 216u;
        if (s > f->height / 280u) s = f->height / 280u;
        for (unsigned row = 0; row < XPL_MENU_ACTION_COUNT; ++row)
            if (rows & XPL_MENU_AVAILABLE(row)) main_row(m, f, s, row, fg, accent, selected, background, 1);
        if (notice) {
            rectangle(f, 0, 201u * s, f->width, 7u * s, background);
            text(f, 4u * s, 201u * s, notice_text(m->notice), 48, s,
                 m->notice == XPL_MENU_NOTICE_NONE ? muted : accent);
        }
        cache->kind = XPL_MENU_RENDER_PARTIAL;
        cache->row_mask = rows;
        cache->notice_updated = (uint8_t)notice;
    } else {
        cache->kind = XPL_MENU_RENDER_NONE;
        cache->row_mask = 0;
        cache->notice_updated = 0;
    }
    copy_snapshot(&cache->previous, m, sizeof *m);
    copy_snapshot(&cache->surface, f, sizeof *f);
    cache->previous_available_mask = available;
    cache->valid = 1;
    return XPL_FB_OK;
}
