/* SPDX-License-Identifier: MIT. Freestanding helpers for compiler aggregate copies. */
#include <stddef.h>
void *memcpy(void *destination,const void *source,size_t bytes) {
    unsigned char *to=destination;
    const unsigned char *from=source;
    for(size_t i=0;i<bytes;i++)to[i]=from[i];
    return destination;
}
void *memset(void *destination,int value,size_t bytes) {
    unsigned char *to=destination;
    for(size_t i=0;i<bytes;i++)to[i]=(unsigned char)value;
    return destination;
}
