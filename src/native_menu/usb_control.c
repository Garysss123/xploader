/* SPDX-License-Identifier: MIT */
#include "usb_control.h"
static unsigned le16(const uint8_t *p){return (unsigned)p[0]|(unsigned)p[1]<<8;}
static int copy(uint8_t *out,size_t capacity,size_t *used,const uint8_t *p,size_t count,unsigned requested){
    if(count>requested)count=requested;if(count>capacity)return -1;
    for(size_t i=0;i<count;i++)out[i]=p[i];*used=count;return XPL_USB_CONTROL_IN;
}
int xpl_usb_control_request(xpl_usb_control *c,const uint8_t setup[8],uint8_t *out,size_t capacity,size_t *bytes){
    if(!c||!setup||!out||!bytes)return -1;*bytes=0;
    unsigned value=le16(setup+2),index=le16(setup+4),length=le16(setup+6);
    static const uint8_t device[]={18,1,0,2,0,0,0,64,9,0x12,1,0,0,1,1,2,3,1};
    static const uint8_t config[]={9,2,18,0,1,1,0,0x80,50,9,4,0,0,0,0xff,0,0,0};
    static const uint8_t disk_config[]={9,2,32,0,1,1,0,0x80,250,9,4,0,0,2,8,6,0x50,0,
        7,5,1,2,0,2,0,7,5,0x81,2,0,2,0};
    static const uint8_t qualifier[]={10,6,0,2,0,0,0,64,1,0};
    static const uint8_t language[]={4,3,9,4};
    if(setup[0]==0x80&&setup[1]==6){
        unsigned type=value>>8,number=value&255u;
        if(type==1&&number==0&&index==0){int r=copy(out,capacity,bytes,device,sizeof device,length);if(r>0&&c->mass_storage&&*bytes>10)out[10]=2;return r;}
        if((type==2||type==7)&&number==0&&index==0){
            const uint8_t *source=c->mass_storage?disk_config:config;size_t count=c->mass_storage?sizeof disk_config:sizeof config;
            int r=copy(out,capacity,bytes,source,count,length);
            if(r>0&&type==7){if(*bytes>1)out[1]=7;if(c->mass_storage){if(*bytes>22)out[22]=64;if(*bytes>23)out[23]=0;if(*bytes>29)out[29]=64;if(*bytes>30)out[30]=0;}}return r;
        }
        if(type==6&&number==0&&index==0)return copy(out,capacity,bytes,qualifier,sizeof qualifier,length);
        if(type==3){
            if(number==0&&index==0)return copy(out,capacity,bytes,language,sizeof language,length);
            if(index!=0x409u)return -1;
            const char *text=number==1?"xploader":number==2?(c->mass_storage?(c->writable?"xploader SD access":"xploader SD read-only"):"xploader native USB"):number==3?c->serial:0;
            if(!text)return -1;
            uint8_t descriptor[126];unsigned used=2;descriptor[1]=3;
            unsigned limit=number==3?(unsigned)sizeof c->serial:62u,j=0;
            while(j<limit&&text[j]&&used+2u<=sizeof descriptor){descriptor[used++]=(uint8_t)text[j++];descriptor[used++]=0;}
            if(j==limit||text[j])return -1;descriptor[0]=(uint8_t)used;
            return copy(out,capacity,bytes,descriptor,used,length);
        }
        return -1;
    }
    if(setup[0]==0&&setup[1]==5&&index==0&&length==0&&value<128u){c->address=(uint8_t)value;return 0;}
    if(setup[0]==0&&setup[1]==9&&index==0&&length==0&&value<=1u){c->configured=(uint8_t)value;return 0;}
    if(setup[0]==0x80&&setup[1]==8&&value==0&&index==0&&length==1)return copy(out,capacity,bytes,&c->configured,1,length);
    if(setup[0]==0x80&&setup[1]==0&&value==0&&index==0&&length==2){const uint8_t status[2]={0,0};return copy(out,capacity,bytes,status,2,length);}
    if(setup[0]==0x81&&setup[1]==10&&value==0&&index==0&&length==1){const uint8_t alt=0;return copy(out,capacity,bytes,&alt,1,length);}
    if(setup[0]==1&&setup[1]==11&&value==0&&index==0&&length==0)return 0;
    if(c->mass_storage&&setup[0]==0xa1&&setup[1]==0xfe&&value==0&&index==0&&length==1){const uint8_t lun=0;return copy(out,capacity,bytes,&lun,1,length);}
    if(c->mass_storage&&setup[0]==0x21&&setup[1]==0xff&&value==0&&index==0&&length==0)return 0;
    if(c->mass_storage&&setup[0]==2&&setup[1]==1&&value==0&&(index==1||index==0x81)&&length==0){c->halted&=(uint8_t)~(index==1?1u:2u);return 0;}
    if(c->mass_storage&&setup[0]==0x82&&setup[1]==0&&value==0&&(index==1||index==0x81)&&length==2){const uint8_t status[2]={(uint8_t)!!(c->halted&(index==1?1u:2u)),0};return copy(out,capacity,bytes,status,2,length);}
    return -1;
}
