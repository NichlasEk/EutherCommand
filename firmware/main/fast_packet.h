#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* Mutable packet: EC1|epoch32|seq16hex|deadline_ms16hex|up/down|hmac64hex. */
static bool ec_fast_parse(char *data,size_t size,const char *epoch,uint64_t now_ms,uint64_t last_seq,uint64_t *seq,char **direction,char **signature) {
    if(size<138 || size>140 || strlen(data)!=size)return false;
    char *parts[6],*cursor=data;
    for(int i=0;i<6;i++) { parts[i]=cursor;char *end=strchr(cursor,'|');if(i<5){if(!end)return false;*end=0;cursor=end+1;}else if(end)return false; }
    if(strcmp(parts[0],"EC1") || strlen(parts[1])!=32 || strcmp(parts[1],epoch) || strlen(parts[2])!=16 || strlen(parts[3])!=16 || strlen(parts[5])!=64)return false;
    const char *hex="0123456789abcdef";
    if(strspn(parts[2],hex)!=16 || strspn(parts[3],hex)!=16 || strspn(parts[5],hex)!=64)return false;
    if(strcmp(parts[4],"up") && strcmp(parts[4],"down"))return false;
    *seq=strtoull(parts[2],NULL,16);uint64_t deadline=strtoull(parts[3],NULL,16);
    if(*seq<=last_seq || deadline<now_ms || deadline-now_ms>2000)return false;
    *direction=parts[4];*signature=parts[5];return true;
}
