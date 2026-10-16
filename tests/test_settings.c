/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/native_menu/settings.h"
static uint32_t seed=0x77115588;
static uint32_t random32(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void){
    xpl_settings a,b,out;unsigned slot=99;uint8_t records[1024]={0},next[512],saved[1024];
    xpl_settings_default(&a);assert(!a.startup&&!a.sequence&&!strcmp(a.directory,"/lineageos17"));
    assert(xpl_settings_choose(records,&out,&slot)==-1);
    a.sequence=1;a.target=2;assert(!xpl_settings_encode(&a,records));
    assert(!xpl_settings_choose(records,&out,&slot)&&slot==0&&out.sequence==1&&out.target==2&&!out.startup);
    b=a;b.sequence=2;b.startup=1;b.target=1;strcpy(b.directory,"/postmarketos");
    assert(!xpl_settings_encode(&b,next));memcpy(saved,records,sizeof saved);
    for(unsigned cut=0;cut<=512;cut++){
        memcpy(records,saved,1024);memcpy(records+512,next,cut);
        assert(!xpl_settings_choose(records,&out,&slot));
        assert((slot==0&&out.sequence==1&&!out.startup)||(slot==1&&out.sequence==2&&out.startup==1&&!strcmp(out.directory,"/postmarketos")));
    }
    for(unsigned bit=0;bit<4096;bit++){
        memcpy(records,saved,1024);memcpy(records+512,next,512);records[512+bit/8]^=(uint8_t)(1u<<(bit%8));
        assert(!xpl_settings_choose(records,&out,&slot)&&slot==0&&out.sequence==1);
    }
    memcpy(records,saved,1024);b.sequence=1;assert(!xpl_settings_encode(&b,records+512));assert(xpl_settings_choose(records,&out,&slot)==-2);
    b.startup=2;assert(xpl_settings_encode(&b,next));b.startup=1;b.target=3;assert(xpl_settings_encode(&b,next));b.target=1;
    strcpy(b.directory,"/a/../b");assert(xpl_settings_encode(&b,next));strcpy(b.directory,"/a/.");assert(xpl_settings_encode(&b,next));
    memset(b.directory,'a',256);assert(xpl_settings_encode(&b,next));
    for(unsigned n=0;n<100000;n++){
        for(unsigned at=0;at<1024;at++)records[at]=(uint8_t)random32();
        assert(xpl_settings_choose(records,&out,&slot)<0);
    }
    puts("PASS: saved mode/target/path, all513 torn-prefix states,4096 single-bit corruptions, equal-sequence conflict, bounds,100000 malformed pairs");
}
