#include <assert.h>
#include <stdio.h>
#include "../firmware/main/fast_packet.h"
int main(void){
 const char *epoch="00000000000000000000000000000001";char b[200],*d,*sig;uint64_t seq;
 #define PACK(E,S,T,D) snprintf(b,sizeof(b),"EC1|%s|%016llx|%016llx|%s|%064d",E,(unsigned long long)S,(unsigned long long)T,D,0)
 PACK(epoch,1,1500,"up");assert(ec_fast_parse(b,strlen(b),epoch,1000,0,&seq,&d,&sig));assert(seq==1 && !strcmp(d,"up"));
 PACK(epoch,1,1500,"down");assert(!ec_fast_parse(b,strlen(b),epoch,1000,1,&seq,&d,&sig));
 PACK(epoch,2,999,"up");assert(!ec_fast_parse(b,strlen(b),epoch,1000,1,&seq,&d,&sig));
 PACK(epoch,2,4000,"up");assert(!ec_fast_parse(b,strlen(b),epoch,1000,1,&seq,&d,&sig));
 PACK(epoch,2,1500,"xx");assert(!ec_fast_parse(b,strlen(b),epoch,1000,1,&seq,&d,&sig));
 PACK(epoch,2,1500,"up");assert(!ec_fast_parse(b,strlen(b),"00000000000000000000000000000002",1000,1,&seq,&d,&sig));
 puts("PASS fast datagram bounds, epoch, expiry, direction and replay checks");
}
