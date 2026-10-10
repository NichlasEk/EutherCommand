#include <stdint.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <assert.h>
#include "../firmware/main/setup_address.h"
int main(void) {
 struct sockaddr_storage s={0};
 uint32_t ap=inet_addr("192.168.4.1");
 struct sockaddr_in *v4=(void*)&s;v4->sin_family=AF_INET;v4->sin_addr.s_addr=ap;
 assert(ec_setup_address(&s,sizeof(*v4),ap));
 v4->sin_addr.s_addr=inet_addr("192.168.1.20");assert(!ec_setup_address(&s,sizeof(*v4),ap));
 memset(&s,0,sizeof(s));struct sockaddr_in6 *v6=(void*)&s;v6->sin6_family=AF_INET6;
 inet_pton(AF_INET6,"::ffff:192.168.4.1",&v6->sin6_addr);assert(ec_setup_address(&s,sizeof(*v6),ap));
 assert(!ec_setup_address(&s,sizeof(*v4),ap));
 inet_pton(AF_INET6,"::ffff:192.168.1.20",&v6->sin6_addr);assert(!ec_setup_address(&s,sizeof(*v6),ap));
 inet_pton(AF_INET6,"2001:db8::1",&v6->sin6_addr);assert(!ec_setup_address(&s,sizeof(*v6),ap));
}
