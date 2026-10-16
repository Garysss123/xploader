/* SPDX-License-Identifier: MIT */
/* Host previews: executes the frozen native074 renderer; no device evidence. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "../src/menu/xpl_menu.h"
static enum xpl_menu_action_result action(void *p,enum xpl_menu_action a){(void)p;(void)a;return XPL_MENU_ACTION_RETURNED;}
static enum xpl_menu_action_result service(void *p,enum xpl_menu_service_action a){(void)p;(void)a;return XPL_MENU_ACTION_RETURNED;}
static void open_folder(void*p){(void)p;}
static void select_folder(void*p,unsigned i){(void)p;(void)i;}
static void save(void*p,unsigned a,unsigned b,const char*c){(void)p;(void)a;(void)b;(void)c;}
static void language(void*p,const char*s){(void)p;(void)s;}
static void defaults(void*p){(void)p;}
static void tools(void*p,enum xpl_menu_tool t){(void)p;(void)t;}
static void write_ppm(const xpl_fb*f,const char*name){
    char path[512];snprintf(path,sizeof path,"%s.ppm",name);FILE*out=fopen(path,"wb");if(!out)exit(2);
    fprintf(out,"P6\n%u %u\n255\n",f->width,f->height);
    for(unsigned y=0;y<f->height;y++)for(unsigned x=0;x<f->width;x++){
        const uint8_t *p=(const uint8_t*)f->pixels+y*f->stride_bytes+x*4;
        unsigned char rgb[3]={p[2],p[1],p[0]};if(fwrite(rgb,1,3,out)!=3)exit(3);
    }fclose(out);
}
int main(void){
    uint8_t*p=calloc(1080*1920,4);if(!p)return 1;
    xpl_fb f={p,1080*1920*4,1080,1920,1080*4,4,0xff0000,0xff00,0xff,0xff000000};
    struct xpl_menu_config c={XPL_MENU_ALWAYS,XPL_MENU_BOOT_SD,"native-menu-074","/lineageos19","en"};
    xpl_menu m;xpl_menu_init(&m,&c,63,action,0,0,0);m.folder_open=open_folder;
    xpl_menu_set_settings(&m,save,0);xpl_menu_set_services(&m,3,service,0);xpl_menu_set_extended(&m,language,defaults,tools,0);
    const struct {enum xpl_menu_page page;const char*name;} pages[]={
        {XPL_MENU_MAIN,"menu-main"},{XPL_MENU_SERVICE_PAGE,"menu-service"},
        {XPL_MENU_BOOT_SETTINGS_PAGE,"menu-boot-settings"},{XPL_MENU_QUICK_TARGET_PAGE,"menu-quickboot"},
        {XPL_MENU_TOOLS_PAGE,"menu-tools"},{XPL_MENU_ONCE_PAGE,"menu-boot-once"},
        {XPL_MENU_RESCUE_PAGE,"menu-rescue"},{XPL_MENU_DEFAULTS_PAGE,"menu-reset-settings"},
        {XPL_MENU_PC_SD_PAGE,"menu-pc-sd"}};
    for(unsigned i=0;i<sizeof pages/sizeof pages[0];i++){m.page=pages[i].page;m.selected=0;
        if(xpl_menu_render(&m,&f))return 4;write_ppm(&f,pages[i].name);}
    const xpl_menu_folder_entry dirs[]={
        {"< Back","/",0},{"lineageos19","/lineageos19",1},
        {"postmarketos","/postmarketos",1},{"sailfishos","/sailfishos",1}};
    xpl_menu_show_folders(&m,"/",dirs,4,1,0,select_folder,0);
    if(xpl_menu_render(&m,&f))return 5;write_ppm(&f,"menu-sd-directory");
    m.page=XPL_MENU_MAIN;xpl_menu_show_error(&m,"/twrp.img was not found.",0x301,0);
    if(xpl_menu_render(&m,&f))return 6;write_ppm(&f,"menu-error");
    for(unsigned i=0;i<3;i++){
        if(xpl_boot_render_target(&f,XPL_BOOT_ACCEPTED,XPL_MENU_BOOT_SD))return 7;
        if(xpl_boot_render_target(&f,(enum xpl_boot_phase)(XPL_BOOT_COUNT_3+i),XPL_MENU_BOOT_SD))return 8;
        char name[40];snprintf(name,sizeof name,"loading-%u",3-i);write_ppm(&f,name);
    }
    if(xpl_boot_render_target(&f,XPL_BOOT_HANDOFF,XPL_MENU_BOOT_SD))return 9;
    write_ppm(&f,"loading-go");free(p);return 0;
}
