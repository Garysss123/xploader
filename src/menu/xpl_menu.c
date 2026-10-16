/* SPDX-License-Identifier: MIT */
#include "xpl_menu.h"

#define KEYS (XPL_MENU_KEY_UP | XPL_MENU_KEY_DOWN | XPL_MENU_KEY_POWER)
#define ROWS_MASK ((1u << XPL_MENU_ACTION_COUNT) - 1u)

static void clear(void *p, size_t size)
{
    uint8_t *d = p;
    while (size--) *d++ = 0;
}
static enum xpl_menu_page return_page(enum xpl_menu_page page){
    return (unsigned)page<=XPL_MENU_DEFAULTS_PAGE&&page!=XPL_MENU_ERROR_PAGE&&page!=XPL_MENU_PC_SD_PAGE?page:XPL_MENU_MAIN;
}
static unsigned page_rows(const xpl_menu *m,enum xpl_menu_page page){
    if(page==XPL_MENU_FOLDERS_PAGE)return m->folder_count;
    if(page==XPL_MENU_TOOLS_PAGE)return 8;
    if(page==XPL_MENU_SERVICE_PAGE&&m->folder_open)return m->service_action?(m->tools_action?6u:5u):3u;
    if(page==XPL_MENU_BOOT_SETTINGS_PAGE||page==XPL_MENU_QUICK_TARGET_PAGE||page==XPL_MENU_ONCE_PAGE)return 4;
    if(page==XPL_MENU_LANGUAGE_PAGE||page==XPL_MENU_RESCUE_PAGE)return 3;
    if(page==XPL_MENU_DEFAULTS_PAGE)return 2;
    if(page==XPL_MENU_INFO_PAGE)return 1;
    return XPL_MENU_ACTION_COUNT;
}

static void text_copy(char *out, size_t size, const char *in, const char *fallback)
{
    size_t i = 0;
    if (!in || !*in) in = fallback;
    while (i + 1 < size && in[i]) {
        unsigned char c = (unsigned char)in[i];
        out[i] = c < 32 || c == 127 ? '?' : (char)c;
        ++i;
    }
    out[i] = 0;
}

const char *xpl_menu_action_label(enum xpl_menu_action action)
{
    static const char *const labels[XPL_MENU_ACTION_COUNT] = {
        "Boot eMMC", "Boot SD", "TWRP", "Service", "Reset", "PowerOff"
    };
    return (unsigned)action < XPL_MENU_ACTION_COUNT ? labels[action] : "Unknown";
}

int xpl_menu_action_available(const xpl_menu *m, enum xpl_menu_action action)
{
    if (!m || (unsigned)action >= XPL_MENU_ACTION_COUNT) return 0;
    if (action == XPL_MENU_SERVICE) return 1;
    return m->action && !!(m->available_mask & XPL_MENU_AVAILABLE(action));
}

void xpl_menu_show_error(xpl_menu *m, const char *message, uint32_t code, uint64_t now)
{
    if (!m) return;
    if (m->page != XPL_MENU_ERROR_PAGE) {
        m->error_return_page = return_page(m->page);
        unsigned count=page_rows(m,m->error_return_page);
        m->error_return_selected = m->selected < count ? m->selected : 0;
    }
    m->error_since = m->last_ms = now;
    m->error_code = code;
    text_copy(m->error_message, sizeof m->error_message, message, "Unable to load the selected image.");
    m->page = XPL_MENU_ERROR_PAGE;
    m->notice = XPL_MENU_NOTICE_NONE;
    m->power_pending = m->repeat_key = m->repeat_fired = 0;
    m->power_armed = !((m->raw_keys | m->stable_keys) & XPL_MENU_KEY_POWER);
    m->dirty = 1;
}

static void error_back(xpl_menu *m, uint8_t keys, uint64_t now)
{
    m->page = return_page(m->error_return_page);
    unsigned count=page_rows(m,m->page);
    m->selected = m->error_return_selected < count ? m->error_return_selected : 0;
    m->notice = XPL_MENU_NOTICE_NONE;
    m->raw_keys = m->stable_keys = keys;
    m->candidate_since = m->repeat_since = m->last_ms = now;
    m->power_pending = m->repeat_key = m->repeat_fired = 0;
    m->power_armed = !(keys & XPL_MENU_KEY_POWER);
    m->dirty = 1;
}

static void activate(xpl_menu *m, enum xpl_menu_action action, int quick)
{
    enum xpl_menu_action_result result;
    m->dirty = 1;
    if (!xpl_menu_action_available(m, action)) {
        m->notice = XPL_MENU_NOTICE_PENDING;
        return;
    }
    if (action == XPL_MENU_SERVICE) {
        m->page = XPL_MENU_SERVICE_PAGE;
        if(m->folder_open)m->selected=0;
        m->notice = XPL_MENU_NOTICE_NONE;
        return;
    }
    /* If this returns, do not tell the user a boot/reset/poweroff succeeded. */
    result = m->action(m->action_context, action);
    if (m->page == XPL_MENU_ERROR_PAGE||m->page==XPL_MENU_FOLDERS_PAGE) return;
    if (result == XPL_MENU_ACTION_UNSUPPORTED) {
        m->available_mask &= ~XPL_MENU_AVAILABLE(action);
        m->notice = XPL_MENU_NOTICE_PENDING;
    } else if (result == XPL_MENU_ACTION_RETURNED) {
        m->notice = quick ? XPL_MENU_NOTICE_QUICK_RETURNED : XPL_MENU_NOTICE_RETURNED;
    } else m->notice = XPL_MENU_NOTICE_FAILED;
}

static uint8_t volume_key(uint8_t keys)
{
    uint8_t volume = keys & (XPL_MENU_KEY_UP | XPL_MENU_KEY_DOWN);
    if (keys & XPL_MENU_KEY_POWER) return 0;
    return volume == XPL_MENU_KEY_UP || volume == XPL_MENU_KEY_DOWN ? volume : 0;
}

void xpl_menu_init(xpl_menu *m, const struct xpl_menu_config *config,
                   uint32_t available, xpl_menu_action_fn action,
                   void *context, uint32_t keys, uint64_t now)
{
    int invalid = 0;
    if (!m) return;
    clear(m, sizeof *m);
    m->action = action;
    m->action_context = context;
    m->available_mask = (available & ROWS_MASK) | XPL_MENU_AVAILABLE(XPL_MENU_SERVICE);
    m->last_ms = m->candidate_since = m->repeat_since = now;
    m->raw_keys = m->stable_keys = (uint8_t)(keys & KEYS);
    m->repeat_key = volume_key(m->stable_keys);
    m->power_armed = !(m->stable_keys & XPL_MENU_KEY_POWER);
    m->dirty = 1;
    text_copy(m->version, sizeof m->version, config ? config->version : 0, "development");
    text_copy(m->sd_directory, sizeof m->sd_directory, config ? config->sd_directory : 0, "/lineageos17");
    text_copy(m->language, sizeof m->language, config ? config->language : 0, "en");
    if (config) {
        if (config->startup != XPL_MENU_ALWAYS && config->startup != XPL_MENU_QUICK) invalid = 1;
        else m->startup = config->startup;
        if ((unsigned)config->quick_target <= XPL_MENU_TWRP) m->quick_target = config->quick_target;
        else if (m->startup == XPL_MENU_QUICK) invalid = 1;
    }
    if (invalid) {
        m->startup = XPL_MENU_ALWAYS;
        m->notice = XPL_MENU_NOTICE_INVALID_CONFIG;
    } else if (m->startup == XPL_MENU_QUICK) {
        if (m->stable_keys) m->notice = XPL_MENU_NOTICE_HELD_FOR_MENU;
        else activate(m, m->quick_target, 1);
    }
}

static void move(xpl_menu *m, uint8_t key)
{
    unsigned count;
    if(m->page==XPL_MENU_MAIN)count=XPL_MENU_ACTION_COUNT;
    else if(m->page==XPL_MENU_FOLDERS_PAGE)count=m->folder_count;
    else if(m->page==XPL_MENU_SERVICE_PAGE&&m->folder_open)count=m->service_action?(m->tools_action?6u:5u):3u;
    else if(m->page==XPL_MENU_BOOT_SETTINGS_PAGE&&m->folder_open)count=4;
    else if(m->page==XPL_MENU_QUICK_TARGET_PAGE&&m->settings_save)count=4;
    else if(m->page==XPL_MENU_LANGUAGE_PAGE)count=3;
    else if(m->page==XPL_MENU_TOOLS_PAGE)count=8;
    else if(m->page==XPL_MENU_ONCE_PAGE)count=4;
    else if(m->page==XPL_MENU_RESCUE_PAGE)count=3;
    else if(m->page==XPL_MENU_DEFAULTS_PAGE)count=2;
    else return;
    if(!count||(m->page==XPL_MENU_FOLDERS_PAGE&&m->folder_scanning))return;
    if (key == XPL_MENU_KEY_UP)
        m->selected = m->selected ? (uint8_t)(m->selected - 1) : (uint8_t)(count - 1u);
    else if (key == XPL_MENU_KEY_DOWN)
        m->selected = (uint8_t)((m->selected + 1u) % count);
    else return;
    m->notice = XPL_MENU_NOTICE_NONE;
    m->dirty = 1;
}

int xpl_menu_poll(xpl_menu *m, uint32_t raw, uint64_t now)
{
    uint8_t keys = (uint8_t)(raw & KEYS);
    if (!m) return 0;
    if (now < m->last_ms) {
        m->raw_keys = m->stable_keys = keys;
        m->candidate_since = m->repeat_since = m->last_ms = now;
        m->repeat_key = volume_key(keys);
        m->repeat_fired = m->power_pending = 0;
        m->power_armed = !(keys & XPL_MENU_KEY_POWER);
        if (m->page == XPL_MENU_ERROR_PAGE) m->error_since = now;
        return m->dirty;
    }
    m->last_ms = now;
    if(m->page==XPL_MENU_PC_SD_PAGE){
        m->raw_keys=m->stable_keys=keys;m->candidate_since=now;
        m->power_pending=0;m->power_armed=!(keys&XPL_MENU_KEY_POWER);return m->dirty;
    }
    if (m->page == XPL_MENU_ERROR_PAGE && now - m->error_since >= XPL_MENU_ERROR_BACK_MS) {
        error_back(m, keys, now);
        return m->dirty; /* Consume this poll; it must not activate the old row. */
    }
    if (keys != m->raw_keys) {
        m->raw_keys = keys;
        m->candidate_since = now;
    }
    if (m->stable_keys != m->raw_keys && now - m->candidate_since >= XPL_MENU_DEBOUNCE_MS) {
        uint8_t old = m->stable_keys, old_volume = volume_key(old);
        uint8_t current_volume;
        m->stable_keys = keys;
        current_volume = volume_key(keys);
        if ((keys & XPL_MENU_KEY_POWER) && (keys & (XPL_MENU_KEY_UP | XPL_MENU_KEY_DOWN)))
            m->power_pending = 0;
        if (!(old & XPL_MENU_KEY_POWER) && (keys & XPL_MENU_KEY_POWER)) {
            m->power_since = now;
            m->power_pending = (uint8_t)(m->power_armed && !(keys & (XPL_MENU_KEY_UP | XPL_MENU_KEY_DOWN)));
        }
        if ((old & XPL_MENU_KEY_POWER) && !(keys & XPL_MENU_KEY_POWER)) {
            unsigned confirm = m->power_pending && !(keys & (XPL_MENU_KEY_UP | XPL_MENU_KEY_DOWN));
            m->power_pending = 0;
            m->power_armed = 1;
            if (confirm) {
                if (m->page == XPL_MENU_ERROR_PAGE) {
                    error_back(m, keys, now);
                    return m->dirty;
                } else if (now - m->power_since <= XPL_MENU_POWER_SHORT_MAX_MS) {
                    if (m->page == XPL_MENU_SERVICE_PAGE) {
                        if(!m->folder_open) m->page = XPL_MENU_MAIN;
                        else if(m->selected==0){m->page=XPL_MENU_BOOT_SETTINGS_PAGE;m->selected=0;}
                        else if(m->selected==1&&m->language_save){m->page=XPL_MENU_LANGUAGE_PAGE;m->selected=m->language[0]=='z'?1:0;}
                        else if(m->selected==(m->service_action?(m->tools_action?5u:4u):2u)){m->page=XPL_MENU_MAIN;m->selected=XPL_MENU_SERVICE;}
                        else if(m->selected==4&&m->tools_action){m->page=XPL_MENU_TOOLS_PAGE;m->selected=0;}
                        else if(m->service_action&&(m->selected==2||m->selected==3)) {
                            enum xpl_menu_service_action service=m->selected==2?XPL_SERVICE_PC_SD:XPL_SERVICE_FASTBOOT;
                            if(!(m->service_available&(1u<<(unsigned)service)))m->notice=XPL_MENU_NOTICE_PENDING;
                            else {
                                enum xpl_menu_action_result r=m->service_action(m->service_context,service);
                                if(m->page!=XPL_MENU_ERROR_PAGE)m->notice=r==XPL_MENU_ACTION_FAILED?XPL_MENU_NOTICE_FAILED:
                                    r==XPL_MENU_ACTION_UNSUPPORTED?XPL_MENU_NOTICE_PENDING:XPL_MENU_NOTICE_RETURNED;
                            }
                            m->dirty=1;return m->dirty;
                        }
                        else {m->notice=XPL_MENU_NOTICE_PENDING;m->dirty=1;return m->dirty;}
                        m->notice = XPL_MENU_NOTICE_NONE;
                        m->dirty = 1;
                    } else if(m->page==XPL_MENU_LANGUAGE_PAGE){
                        if(m->selected<2&&m->language_save){m->language_save(m->extended_context,m->selected?"zh-TW":"en");m->notice=XPL_MENU_NOTICE_SETTINGS_SAVE;}
                        else {m->page=XPL_MENU_SERVICE_PAGE;m->selected=1;m->notice=XPL_MENU_NOTICE_NONE;}
                        m->dirty=1;
                    } else if(m->page==XPL_MENU_TOOLS_PAGE){
                        unsigned selected=m->selected;
                        if(!selected){m->page=XPL_MENU_ONCE_PAGE;m->selected=0;}
                        else if(selected==4){m->page=XPL_MENU_RESCUE_PAGE;m->selected=0;}
                        else if(selected==5){m->page=XPL_MENU_DEFAULTS_PAGE;m->selected=1;}
                        else if(selected==7){m->page=XPL_MENU_SERVICE_PAGE;m->selected=4;}
                        else if(m->tools_action)m->tools_action(m->extended_context,selected==6?XPL_TOOL_ABOUT:(enum xpl_menu_tool)(selected-1));
                        m->dirty=1;
                    } else if(m->page==XPL_MENU_INFO_PAGE){
                        m->page=XPL_MENU_TOOLS_PAGE;m->selected=m->info_return_selected;m->dirty=1;
                    } else if(m->page==XPL_MENU_ONCE_PAGE){
                        if(m->selected<3)activate(m,(enum xpl_menu_action)m->selected,0);
                        else {m->page=XPL_MENU_TOOLS_PAGE;m->selected=0;}
                        m->dirty=1;
                    } else if(m->page==XPL_MENU_RESCUE_PAGE){
                        if(m->selected==0)activate(m,XPL_MENU_TWRP,0);
                        else if(m->selected==1){
                            if(!m->service_action||!(m->service_available&(1u<<XPL_SERVICE_FASTBOOT)))m->notice=XPL_MENU_NOTICE_PENDING;
                            else {enum xpl_menu_action_result r=m->service_action(m->service_context,XPL_SERVICE_FASTBOOT);
                                if(m->page!=XPL_MENU_ERROR_PAGE)m->notice=r==XPL_MENU_ACTION_FAILED?XPL_MENU_NOTICE_FAILED:
                                    r==XPL_MENU_ACTION_UNSUPPORTED?XPL_MENU_NOTICE_PENDING:XPL_MENU_NOTICE_RETURNED;}
                        }else {m->page=XPL_MENU_TOOLS_PAGE;m->selected=4;}
                        m->dirty=1;
                    } else if(m->page==XPL_MENU_DEFAULTS_PAGE){
                        if(m->selected==0&&m->defaults_save){m->defaults_save(m->extended_context);m->notice=XPL_MENU_NOTICE_SETTINGS_SAVE;}
                        else {m->page=XPL_MENU_TOOLS_PAGE;m->selected=5;m->notice=XPL_MENU_NOTICE_NONE;}
                        m->dirty=1;
                    } else if(m->page==XPL_MENU_BOOT_SETTINGS_PAGE) {
                        if(m->selected==2&&m->folder_open)m->folder_open(m->folder_context);
                        else if(m->selected==0&&m->settings_save){m->page=XPL_MENU_QUICK_TARGET_PAGE;m->selected=(uint8_t)m->quick_target;m->notice=XPL_MENU_NOTICE_NONE;}
                        else if(m->selected==1&&m->settings_save){m->settings_save(m->settings_context,XPL_MENU_ALWAYS,(unsigned)m->quick_target,m->sd_directory);m->notice=XPL_MENU_NOTICE_SETTINGS_SAVE;}
                        else if(m->selected==3){m->page=XPL_MENU_SERVICE_PAGE;m->selected=0;m->notice=XPL_MENU_NOTICE_NONE;}
                        else m->notice=XPL_MENU_NOTICE_PENDING;
                        m->dirty=1;
                    } else if(m->page==XPL_MENU_QUICK_TARGET_PAGE){
                        if(m->selected<3&&m->settings_save){
                            m->settings_save(m->settings_context,XPL_MENU_QUICK,m->selected,m->sd_directory);
                            m->page=XPL_MENU_BOOT_SETTINGS_PAGE;m->selected=0;m->notice=XPL_MENU_NOTICE_SETTINGS_SAVE;
                        }else {m->page=XPL_MENU_BOOT_SETTINGS_PAGE;m->selected=0;m->notice=XPL_MENU_NOTICE_NONE;}
                        m->dirty=1;
                    } else if(m->page==XPL_MENU_FOLDERS_PAGE) {
                        if(!m->folder_scanning&&m->folder_select&&m->selected<m->folder_count)
                            m->folder_select(m->folder_context,m->selected);
                        m->dirty=1;
                    } else activate(m, (enum xpl_menu_action)m->selected, 0);
                } else {
                    m->notice = XPL_MENU_NOTICE_LONG_POWER;
                    m->dirty = 1;
                }
            }
        }
        if (current_volume != old_volume) {
            m->repeat_key = current_volume;
            m->repeat_since = now;
            m->repeat_fired = 0;
            if (current_volume) move(m, current_volume);
        }
    }
    if (m->repeat_key && m->repeat_key == volume_key(m->stable_keys) &&
        m->raw_keys == m->stable_keys &&
        now - m->repeat_since >= (m->repeat_fired ? XPL_MENU_REPEAT_INTERVAL_MS : XPL_MENU_REPEAT_DELAY_MS)) {
        move(m, m->repeat_key);
        m->repeat_since = now;
        m->repeat_fired = 1;
    }
    return m->dirty;
}

void xpl_menu_mark_clean(xpl_menu *m)
{
    if (m) m->dirty = 0;
}

void xpl_menu_set_services(xpl_menu *m,uint32_t available,xpl_menu_service_fn callback,void *context)
{
    if(!m)return;
    m->service_available=available&3u;m->service_action=callback;m->service_context=context;m->dirty=1;
}
void xpl_menu_set_settings(xpl_menu *m,void (*save)(void *,unsigned,unsigned,const char *),void *context){
    if(m){m->settings_save=save;m->settings_context=context;m->dirty=1;}
}
void xpl_menu_set_extended(xpl_menu *m,void (*language)(void *,const char *),void (*defaults)(void *),void (*tools)(void *,enum xpl_menu_tool),void *context){
    if(!m)return;
    m->language_save=language;m->defaults_save=defaults;m->tools_action=tools;m->extended_context=context;m->dirty=1;
}
void xpl_menu_show_info(xpl_menu *m,const char *title,const char (*lines)[96],unsigned count,unsigned selected){
    if(!m||!lines||count>12)return;
    text_copy(m->info_title,sizeof m->info_title,title,"Info");
    m->info_lines=lines;m->info_count=(uint8_t)count;m->info_return_selected=(uint8_t)(selected<8?selected:0);
    m->info_generation++;m->page=XPL_MENU_INFO_PAGE;m->selected=0;m->notice=XPL_MENU_NOTICE_NONE;m->dirty=1;
}

void xpl_menu_show_folders(xpl_menu *m,const char *path,
    const xpl_menu_folder_entry *entries,unsigned count,unsigned selected,
    unsigned scanning,xpl_menu_folder_select_fn callback,void *context)
{
    if(!m||!entries||!count||count>XPL_MENU_FOLDER_MAX)return;
    m->folders=entries;m->folder_count=(uint8_t)count;m->folder_select=callback;m->folder_context=context;
    text_copy(m->folder_path,sizeof m->folder_path,path,"/");
    m->folder_scanning=(uint8_t)!!scanning;m->folder_generation++;
    m->selected=(uint8_t)(selected<count?selected:0);m->page=XPL_MENU_FOLDERS_PAGE;
    m->notice=XPL_MENU_NOTICE_NONE;m->power_pending=m->repeat_key=m->repeat_fired=0;
    m->power_armed=!((m->raw_keys|m->stable_keys)&XPL_MENU_KEY_POWER);m->dirty=1;
}
