#include <assert.h>
#include <stdio.h>
typedef struct {unsigned duration0:15;unsigned level0:1;unsigned duration1:15;unsigned level1:1;} rmt_symbol_word_t;
#include "../firmware/main/nec_press.h"
int main(void){
 rmt_symbol_word_t s[36]={0};size_t n=34;
 s[0]=(rmt_symbol_word_t){.level0=1,.duration0=9000,.duration1=4500};
 for(int i=1;i<=32;i++)s[i]=(rmt_symbol_word_t){.level0=1,.duration0=560,.duration1=i<=16?560:1690};
 s[33]=(rmt_symbol_word_t){.level0=1,.duration0=560,.duration1=1};
 assert(!ec_nec_press(s,&n,34));assert(ec_nec_press(s,&n,36));assert(n==36);
 unsigned repeat_start=0;for(int i=0;i<34;i++)repeat_start+=s[i].duration0+s[i].duration1;
 repeat_start+=s[34].duration0;assert(repeat_start==110000);
 assert(s[34].level0==0 && s[34].level1==1 && s[34].duration1==9000);
 assert(s[35].level0==0 && s[35].duration0==2250 && s[35].level1==1 && s[35].duration1==560);
 assert(repeat_start+s[34].duration1+s[35].duration0+s[35].duration1==121810);
 assert(!ec_nec_press(s,&n,36));
 puts("PASS NEC press: intact first frame, one repeat at 110 ms, total 121.81 ms");
}
