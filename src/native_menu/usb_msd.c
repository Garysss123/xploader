/* SPDX-License-Identifier: MIT. Bounded BOT/SCSI, synchronous optional writes. */
#include "usb_msd.h"
static uint32_t le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void putle(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8u));}
static void putbe(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>((3u-i)*8u));}
static void sense(xpl_usb_msd *d,unsigned key,unsigned asc){d->sense=(uint8_t)key;d->asc=(uint8_t)asc;d->ascq=0;d->status=1;}
void xpl_usb_msd_reset(xpl_usb_msd *d){if(d){d->phase=XPL_MSD_CBW;d->prepared=d->status=d->sector_stream=0;d->done=d->remaining=d->discard=d->pending=0;}}
int xpl_usb_msd_init(xpl_usb_msd *d,uint64_t sectors,void *context,int (*read)(void *,uint64_t,uint8_t [512])){
    if(!d||!read||!sectors)return -1;*d=(xpl_usb_msd){.context=context,.read_sector=read,.sectors=sectors};return 0;
}
int xpl_usb_msd_enable_write(xpl_usb_msd *d,int (*write)(void *,uint64_t,const uint8_t [512])){
    if(!d||!d->read_sector||!write||d->phase!=XPL_MSD_CBW||d->commands||d->ejected)return -1;
    d->write_sector=write;return 0;
}
static void failed_data(xpl_usb_msd *d,unsigned direction){
    d->remaining=0;
    if(!d->host_bytes)d->phase=XPL_MSD_CSW;
    else if(direction)d->phase=XPL_MSD_ZLP;
    else {d->discard=d->host_bytes;d->phase=XPL_MSD_DISCARD_OUT;}
}
static int cbw(xpl_usb_msd *d,const uint8_t *p,size_t bytes){
    if(bytes!=31||le32(p)!=0x43425355u||(p[12]&127u)||p[13]||!p[14]||p[14]>16){d->phase=XPL_MSD_RESET_REQUIRED;return -1;}
    d->tag=le32(p+4);d->host_bytes=le32(p+8);d->done=d->remaining=d->pending=d->discard=0;
    d->prepared=d->status=d->sector_stream=d->response_bytes=0;d->commands++;
    for(unsigned i=0;i<sizeof d->response;i++)d->response[i]=0;
    const uint8_t *c=p+15;unsigned direction=p[12]>>7,needed=0,allocation=0,minimum=6;
    d->opcode=c[0];
    uint64_t lba=0,count=0;
    switch(c[0]){
    case 0x12: /* INQUIRY incl common VPD pages */
        allocation=c[4];
        if(c[1]&1u){
            d->response[1]=c[2];
            if(c[2]==0){d->response[3]=2;d->response[4]=0;d->response[5]=0x80;needed=6;}
            else if(c[2]==0x80){static const char serial[]="XPLOADER-SD";d->response[3]=11;for(unsigned i=0;i<11;i++)d->response[4+i]=(uint8_t)serial[i];needed=15;}
            else {sense(d,5,0x24);break;}
        }else{
            d->response[1]=0x80;d->response[2]=4;d->response[3]=2;d->response[4]=31;
            static const char identity[]="XPLOADERXperia Z5 SD    0001";
            _Static_assert(sizeof identity==29,"SCSI identity width");
            for(unsigned i=0;i<28;i++)d->response[8+i]=(uint8_t)identity[i];needed=36;
        }break;
    case 0x03:
        allocation=c[4];needed=18;d->response[0]=0x70;d->response[2]=d->sense;d->response[7]=10;d->response[12]=d->asc;d->response[13]=d->ascq;break;
    case 0x00:if(d->ejected)sense(d,2,0x3a);break;
    case 0x1e:if(c[4]&~1u)sense(d,5,0x24);else d->prevent=c[4]&1u;break;
    case 0x1b:
        if((c[4]&3u)==2u){if(d->prevent)sense(d,5,0x53);else d->ejected=1;}break;
    case 0x35:minimum=10;break; /* No write cache in this implementation. */
    case 0x25:
        minimum=10;needed=allocation=8;
        if(d->ejected){sense(d,2,0x3a);break;}
        putbe(d->response,d->sectors-1>UINT32_MAX?UINT32_MAX:(uint32_t)(d->sectors-1));putbe(d->response+4,512);break;
    case 0x9e:
        minimum=16;if((c[1]&31u)!=0x10u){sense(d,5,0x20);break;}
        allocation=be32(c+10);needed=32;
        if(d->ejected){sense(d,2,0x3a);break;}
        putbe(d->response,(uint32_t)((d->sectors-1)>>32));putbe(d->response+4,(uint32_t)(d->sectors-1));putbe(d->response+8,512);break;
    case 0x23:
        minimum=10;allocation=(unsigned)c[7]<<8|c[8];needed=12;d->response[3]=8;
        putbe(d->response+4,d->sectors>UINT32_MAX?UINT32_MAX:(uint32_t)d->sectors);d->response[8]=2;d->response[10]=2;break;
    case 0x1a:case 0x5a:{
        unsigned ten=c[0]==0x5a,page=c[2]&63u;
        minimum=ten?10u:6u;allocation=ten?((unsigned)c[7]<<8|c[8]):c[4];needed=ten?8u:4u;
        d->response[ten?3:2]=d->write_sector?0:0x80;
        if(page==8||page==63){d->response[needed]=8;d->response[needed+1]=18;needed+=20;}
        else if(page!=0){sense(d,5,0x24);break;}
        if(ten){d->response[0]=0;d->response[1]=(uint8_t)(needed-2);}else d->response[0]=(uint8_t)(needed-1);break;
    }
    case 0xa0:minimum=12;allocation=be32(c+6);needed=16;d->response[3]=8;break;
    case 0x28:case 0xa8:case 0x88:case 0x2a:case 0xaa:case 0x8a:{
        unsigned writing=(c[0]&2u)?1u:0u,variant=c[0]&~2u;
        minimum=variant==0x28?10u:variant==0xa8?12u:16u;
        if(writing&&!d->write_sector){sense(d,7,0x27);break;}
        if(variant==0x88){lba=(uint64_t)be32(c+2)<<32|be32(c+6);count=be32(c+10);}
        else {lba=be32(c+2);count=variant==0x28?((unsigned)c[7]<<8|c[8]):be32(c+6);}
        if(d->ejected){sense(d,2,0x3a);break;}
        if(lba>d->sectors||count>d->sectors-lba){sense(d,5,0x21);break;}
        if(count>UINT32_MAX/512u||d->host_bytes!=count*512u||(count&&direction==writing)){d->status=2;break;}
        d->lba=lba;d->remaining=(uint32_t)(count*512u);d->sector_stream=writing?2u:1u;break;
    }
    default:sense(d,5,0x20);break;
    }
    if(p[14]<minimum){d->status=2;d->remaining=0;d->sector_stream=0;}
    if(d->status){failed_data(d,direction);return 0;}
    if(!d->sector_stream){
        if(needed>allocation)needed=allocation;
        if(needed>d->host_bytes)needed=d->host_bytes;
        if(needed&&!direction){d->status=2;failed_data(d,direction);return 0;}
        d->response_bytes=(uint8_t)needed;d->remaining=needed;
    }
    if(d->remaining)d->phase=d->sector_stream==2?XPL_MSD_WRITE_OUT:XPL_MSD_IN;
    else if(d->host_bytes){d->status=2;failed_data(d,direction);}
    else d->phase=XPL_MSD_CSW;return 0;
}
int xpl_usb_msd_out(xpl_usb_msd *d,const uint8_t *p,size_t bytes){
    if(!d||!p||bytes>512)return -1;
    if(d->phase==XPL_MSD_CBW)return cbw(d,p,bytes);
    if(d->phase==XPL_MSD_WRITE_OUT){
        if(!d->write_sector||bytes!=512||d->remaining<512){d->phase=XPL_MSD_RESET_REQUIRED;return -1;}
        d->remaining-=512;
        if(d->write_sector(d->context,d->lba,p)){
            sense(d,3,0x0c);d->discard=d->remaining;d->remaining=0;
            d->phase=d->discard?XPL_MSD_DISCARD_OUT:XPL_MSD_CSW;return 0;
        }
        d->writes++;d->done+=512;d->lba++;
        if(!d->remaining)d->phase=XPL_MSD_CSW;return 0;
    }
    if(d->phase!=XPL_MSD_DISCARD_OUT||bytes>d->discard){d->phase=XPL_MSD_RESET_REQUIRED;return -1;}
    d->discard-=(uint32_t)bytes;
    if(!bytes&&d->discard){d->discard=0;d->status=2;}
    if(!d->discard)d->phase=XPL_MSD_CSW;return 0;
}
size_t xpl_usb_msd_out_bytes(const xpl_usb_msd *d){return !d?0:d->phase==XPL_MSD_CBW?31u:d->phase==XPL_MSD_WRITE_OUT?512u:d->phase==XPL_MSD_DISCARD_OUT?(d->discard<512?d->discard:512):0;}
int xpl_usb_msd_prepare_in(xpl_usb_msd *d,uint8_t out[512],size_t *bytes){
    if(!d||!out||!bytes||d->prepared)return -1;*bytes=0;
    if(d->phase==XPL_MSD_IN){
        if(d->sector_stream){
            if(d->remaining<512)return -1;
            if(d->read_sector(d->context,d->lba,out)){sense(d,3,0x11);d->remaining=0;d->phase=XPL_MSD_ZLP;}
            else {d->reads++;*bytes=512;}
        }else {*bytes=d->remaining;for(size_t i=0;i<*bytes;i++)out[i]=d->response[i];}
    }else if(d->phase==XPL_MSD_CSW){
        putle(out,0x53425355u);putle(out+4,d->tag);putle(out+8,d->host_bytes-d->done);out[12]=d->status;*bytes=13;
    }else if(d->phase!=XPL_MSD_ZLP)return -1;
    d->pending=(uint32_t)*bytes;d->prepared=1;return 0;
}
int xpl_usb_msd_in_complete(xpl_usb_msd *d,size_t bytes){
    if(!d||!d->prepared||bytes!=d->pending)return -1;d->prepared=0;
    if(d->phase==XPL_MSD_CSW){d->phase=XPL_MSD_CBW;return 0;}
    if(d->phase==XPL_MSD_ZLP){d->phase=XPL_MSD_CSW;return 0;}
    if(d->phase!=XPL_MSD_IN||bytes>d->remaining)return -1;
    d->done+=(uint32_t)bytes;d->remaining-=(uint32_t)bytes;if(d->sector_stream)d->lba++;
    if(!d->remaining){d->phase=XPL_MSD_CSW;if(d->opcode==3)d->sense=d->asc=d->ascq=0;}return 0;
}
