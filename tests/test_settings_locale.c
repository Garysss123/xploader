/* SPDX-License-Identifier: MIT. Cross-version wire compatibility, not a mirror. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/native_menu/settings.h"
typedef struct {uint64_t sequence;uint32_t startup,target;char directory[256];} old_settings;
int xpl_settings_decode_v1(const uint8_t *,size_t,old_settings *);
int xpl_settings_encode_v1(const old_settings *,uint8_t [512]);
int main(void){
    xpl_settings now,decoded;old_settings old={.sequence=41,.startup=1,.target=2};
    uint8_t wire[512],legacy[512];strcpy(old.directory,"/sailfishos");
    assert(!xpl_settings_encode_v1(&old,legacy));assert(!xpl_settings_decode(legacy,512,&now));
    assert(!now.language[0]&&now.sequence==41&&now.target==2&&!strcmp(now.directory,"/sailfishos"));
    assert(!xpl_settings_encode(&now,wire)&&!memcmp(wire,legacy,512));
    strcpy(now.language,"zh-TW");now.sequence=42;assert(!xpl_settings_encode(&now,wire));
    assert(!xpl_settings_decode(wire,512,&decoded)&&!strcmp(decoded.language,"zh-TW"));
    assert(!xpl_settings_decode_v1(wire,512,&old)&&old.sequence==42&&old.startup==1&&old.target==2&&!strcmp(old.directory,"/sailfishos"));
    strcpy(now.language,"fr-FR");assert(!xpl_settings_encode(&now,wire));assert(!xpl_settings_decode(wire,512,&decoded)&&!strcmp(decoded.language,"fr-FR"));
    memset(now.language,'x',16);assert(xpl_settings_encode(&now,wire));
    strcpy(now.language,"zh/TW");assert(xpl_settings_encode(&now,wire));
    strcpy(now.language,"zh-TW");assert(!xpl_settings_encode(&now,wire));
    for(unsigned cut=0;cut<=512;cut++){
        uint8_t records[1024]={0};memcpy(records,legacy,512);memcpy(records+512,wire,cut);unsigned slot;
        assert(!xpl_settings_choose(records,&decoded,&slot));
        assert((slot==0&&decoded.sequence==41&&!decoded.language[0])||(slot==1&&decoded.sequence==42&&!strcmp(decoded.language,"zh-TW")));
    }
    puts("PASS: measured067 wire reader accepts new language extension; old records remain byte-identical; locale torn writes retain valid boot policy");
}
