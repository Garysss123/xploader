/* SPDX-License-Identifier: MIT */
#include "../src/menu/xpl_menu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static xpl_menu menu;
static xpl_menu_folder_entry folders[32];
static uint64_t time_ms;
static unsigned checks,boots,opens,selections;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static enum xpl_menu_action_result action(void *p,enum xpl_menu_action a) {(void)p;CHECK(a==XPL_MENU_BOOT_SD);boots++;return XPL_MENU_ACTION_RETURNED;}
static void selected(void *p,unsigned n) {
    (void)p;selections++;CHECK(n<32);
    if(!n){menu.page=XPL_MENU_BOOT_SETTINGS_PAGE;menu.selected=2;menu.dirty=1;return;}
    strcpy(menu.sd_directory,folders[n].path);menu.page=XPL_MENU_MAIN;menu.selected=XPL_MENU_BOOT_SD;menu.dirty=1;
}
static void opened(void *p) {(void)p;opens++;xpl_menu_show_folders(&menu,"/",folders,32,31,0,selected,0);}
static void poll(unsigned key,unsigned delay) {time_ms+=delay;xpl_menu_poll(&menu,key,time_ms);}
static void press(unsigned key) {poll(key,1);poll(key,15);poll(0,1);poll(0,15);}
int main(void) {
    struct xpl_menu_config config={XPL_MENU_ALWAYS,XPL_MENU_BOOT_EMMC,"test","/lineageos17","en"};
    strcpy(folders[0].label,"< Back");strcpy(folders[0].path,"/");
    for(unsigned i=1;i<32;i++){snprintf(folders[i].label,128,"ROM-%u",i);snprintf(folders[i].path,256,"/ROM-%u",i);folders[i].kind=2;}
    xpl_menu_init(&menu,&config,XPL_MENU_AVAILABLE(XPL_MENU_BOOT_SD),action,0,0,0);menu.folder_open=opened;
    for(unsigned i=0;i<3;i++)press(XPL_MENU_KEY_DOWN);
    CHECK(menu.selected==XPL_MENU_SERVICE);press(XPL_MENU_KEY_POWER);
    CHECK(menu.page==XPL_MENU_SERVICE_PAGE&&menu.selected==0);press(XPL_MENU_KEY_POWER);
    CHECK(menu.page==XPL_MENU_BOOT_SETTINGS_PAGE&&menu.selected==0);
    press(XPL_MENU_KEY_DOWN);press(XPL_MENU_KEY_DOWN);CHECK(menu.selected==2);press(XPL_MENU_KEY_POWER);
    CHECK(opens==1&&menu.page==XPL_MENU_FOLDERS_PAGE&&menu.selected==31&&boots==0);
    press(XPL_MENU_KEY_DOWN);CHECK(menu.selected==0);press(XPL_MENU_KEY_POWER);
    CHECK(menu.page==XPL_MENU_BOOT_SETTINGS_PAGE&&menu.selected==2&&boots==0);
    press(XPL_MENU_KEY_POWER);CHECK(opens==2&&menu.page==XPL_MENU_FOLDERS_PAGE);
    press(XPL_MENU_KEY_POWER);CHECK(menu.page==XPL_MENU_MAIN&&menu.selected==XPL_MENU_BOOT_SD);
    CHECK(!strcmp(menu.sd_directory,"/ROM-31")&&boots==0&&selections==2);
    press(XPL_MENU_KEY_POWER);CHECK(boots==1);
    xpl_menu_show_folders(&menu,"/",folders,32,29,0,selected,0);
    xpl_menu_show_error(&menu,"bad card",7,time_ms);poll(0,3000);
    CHECK(menu.page==XPL_MENU_FOLDERS_PAGE&&menu.selected==29&&boots==1);
    xpl_menu_show_folders(&menu,"/",folders,1,0,1,selected,0);press(XPL_MENU_KEY_POWER);CHECK(selections==2);
    /* New scene consumes an already-held Power, including its release. */
    poll(XPL_MENU_KEY_POWER,1);poll(XPL_MENU_KEY_POWER,15);
    xpl_menu_show_folders(&menu,"/",folders,32,2,0,selected,0);poll(0,1);poll(0,15);
    CHECK(menu.page==XPL_MENU_FOLDERS_PAGE&&selections==2);
    uint8_t *pixels=malloc(1080u*1920u*3u+64u);CHECK(pixels);memset(pixels,0xa5,1080u*1920u*3u+64u);
    xpl_fb fb={pixels+16,1080u*1918u*3u,1080,1918,3240,3,0xff,0xff00,0xff0000,0};
    xpl_menu_render_cache cache={0};
    for(unsigned n=0;n<32;n++) {
        menu.selected=(uint8_t)n;CHECK(!xpl_menu_render_update(&menu,&fb,&cache));
        CHECK(cache.kind==XPL_MENU_RENDER_FULL);
        CHECK(pixels[15]==0xa5&&pixels[16+1080u*1918u*3u]==0xa5);
    }
    unsigned generation=menu.folder_generation;xpl_menu_show_folders(&menu,"/nested",folders,32,31,0,selected,0);
    CHECK(menu.folder_generation!=generation);CHECK(!xpl_menu_render_update(&menu,&fb,&cache)&&cache.kind==XPL_MENU_RENDER_FULL);
    free(pixels);printf("PASS: %u service/folder UI checks\n",checks);return 0;
}
