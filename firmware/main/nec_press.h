#pragma once
#include <stddef.h>
#include <stdbool.h>
/* Append one observed NEC repeat to the registered Logitech frame.
 * Split the low gap to respect RMT's 15-bit duration fields. */
static bool ec_nec_press(rmt_symbol_word_t *s,size_t *count,size_t capacity) {
    if(*count!=34 || capacity<*count+2)return false;
    unsigned duration=0;
    for(size_t i=0;i<*count;i++)duration+=s[i].duration0+s[i].duration1;
    if(s[*count-1].level0!=1 || s[*count-1].duration1!=1 || duration>=110000)return false;
    unsigned gap=110000-(duration-1);
    if(gap<2 || gap>65534)return false;
    s[*count-1].duration1=gap/2;
    s[*count]=(rmt_symbol_word_t){.level0=0,.duration0=gap-gap/2,.level1=1,.duration1=9000};
    s[*count+1]=(rmt_symbol_word_t){.level0=0,.duration0=2250,.level1=1,.duration1=560};
    *count+=2;return true;
}
