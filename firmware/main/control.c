/* Authenticated HTTPS control and bounded learning; no arbitrary GPIO operations. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_https_server.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "setup_address.h"
#include "cJSON.h"
#include "control.h"
#include "mbedtls/md.h"
#include "fast_packet.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if __has_include("control_local.h")
#include "control_local.h"
static const unsigned char cert[]=EC_CONTROL_CERT, key[]=EC_CONTROL_KEY;
static char boot_id[33],fast_epoch[33];
static uint64_t fast_sequence;
static struct {char id[65],op[20],status[24];} history[64];
static unsigned history_count;
static int64_t last_request;
static bool started;
static void new_epoch(void) {
    for(unsigned i=0;i<16;i++)sprintf(boot_id+2*i,"%02x",(unsigned)(esp_random()&255));
    memset(history,0,sizeof(history));history_count=0;last_request=0;
}
static bool identity(const char *s) {
    return s && strlen(s)>0 && strlen(s)<=64 && strspn(s,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==strlen(s);
}
static bool authorized(httpd_req_t *r) {
    struct sockaddr_storage local={0};socklen_t len=sizeof(local);esp_netif_ip_info_t ip;
    esp_netif_t *sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if(!sta || esp_netif_get_ip_info(sta,&ip)!=ESP_OK || !ip.ip.addr ||
       getsockname(httpd_req_to_sockfd(r),(struct sockaddr*)&local,&len)!=0 || !ec_setup_address(&local,len,ip.ip.addr))return false;
    char value[80]; const char expected[]="Bearer " EC_CONTROL_TOKEN;
    if(httpd_req_get_hdr_value_str(r,"Authorization",value,sizeof(value))!=ESP_OK || strlen(value)!=sizeof(expected)-1)return false;
    unsigned difference=0;for(size_t i=0;i<sizeof(expected)-1;i++)difference|=(unsigned char)value[i]^(unsigned char)expected[i];
    memset(value,0,sizeof(value));return difference==0;
}
static esp_err_t response(httpd_req_t *r,const char *status,bool duplicate) {
    char body[384];snprintf(body,sizeof(body),"{\"status\":\"%s\",\"boot_id\":\"%s\",\"duplicate\":%s,\"device_state\":\"unverified\",\"history_remaining\":%u,\"fast_epoch\":\"%s\",\"uptime_ms\":%llu,\"fast_port\":7426,\"fast_enabled\":%s}",status,boot_id,duplicate?"true":"false",64-history_count,fast_epoch,(unsigned long long)(esp_timer_get_time()/1000),!strcmp(ec_ir_control("fast_status"),"enabled")?"true":"false");
    httpd_resp_set_type(r,"application/json");httpd_resp_set_hdr(r,"Cache-Control","no-store");
    return httpd_resp_sendstr(r,body);
}
static esp_err_t status_handler(httpd_req_t *r) {
    if(!authorized(r))return httpd_resp_send_err(r,HTTPD_401_UNAUTHORIZED,"Unauthorized");
    return response(r,ec_ir_control("status"),false);
}
static esp_err_t execute(httpd_req_t *r) {
    if(!authorized(r))return httpd_resp_send_err(r,HTTPD_401_UNAUTHORIZED,"Unauthorized");
    if(r->content_len<1 || r->content_len>512)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid size");
    char body[513];size_t used=0;
    while(used<r->content_len){int n=httpd_req_recv(r,body+used,r->content_len-used);if(n<=0)return ESP_FAIL;used+=n;}
    body[used]=0;cJSON *root=cJSON_ParseWithLength(body,used+1);
    cJSON *id=cJSON_GetObjectItemCaseSensitive(root,"request_id"),*op=cJSON_GetObjectItemCaseSensitive(root,"command"),*boot=cJSON_GetObjectItemCaseSensitive(root,"boot_id");
    if(!cJSON_IsObject(root) || cJSON_GetArraySize(root)!=3 || !cJSON_IsString(id) || !identity(id->valuestring) || !cJSON_IsString(op) || !cJSON_IsString(boot) || strcmp(boot->valuestring,boot_id) ||
       (strcmp(op->valuestring,"enable") && strcmp(op->valuestring,"disable") && strcmp(op->valuestring,"volume_up") && strcmp(op->valuestring,"volume_down") && !(strlen(op->valuestring)==15 && !strncmp(op->valuestring,"ir_",3) && strspn(op->valuestring+3,"0123456789abcdef")==12))) {
        cJSON_Delete(root);return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid registered operation or boot");
    }
    for(unsigned i=0;i<history_count;i++)if(!strcmp(history[i].id,id->valuestring)) {
        bool match=!strcmp(history[i].op,op->valuestring);cJSON_Delete(root);
        if(!match)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Request conflict");
        return response(r,history[i].status,true);
    }
    if(history_count==64){
        bool disable=!strcmp(op->valuestring,"disable");cJSON_Delete(root);
        return response(r,disable?ec_ir_control("disable"):"history_full",false);
    }
    int64_t now=esp_timer_get_time();
    if(strcmp(op->valuestring,"disable") && now-last_request<250000){cJSON_Delete(root);return response(r,"rate_limited",false);}
    last_request=now;
    unsigned slot=history_count++; /* Reserve BEFORE hardware action. Boot nonce changes on reboot. */
    strcpy(history[slot].id,id->valuestring);strcpy(history[slot].op,op->valuestring);strcpy(history[slot].status,"unknown");
    const char *result=ec_ir_control(op->valuestring);
    snprintf(history[slot].status,sizeof(history[slot].status),"%s",result);
    printf("{\"event\":\"lan_command\",\"command\":\"%s\",\"status\":\"%s\"}\n",op->valuestring,result);
    cJSON_Delete(root);return response(r,history[slot].status,false);
}
/* Rotate only while disabled. Old epoch requests remain invalid after clearing the ledger. */
static esp_err_t renew_session(httpd_req_t *r) {
    if(!authorized(r))return httpd_resp_send_err(r,HTTPD_401_UNAUTHORIZED,"Unauthorized");
    if(r->content_len<1 || r->content_len>80)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid size");
    char body[81];int used=0;
    while(used<r->content_len){int n=httpd_req_recv(r,body+used,r->content_len-used);if(n<=0)return ESP_FAIL;used+=n;}body[used]=0;
    cJSON *root=cJSON_Parse(body),*boot=cJSON_GetObjectItemCaseSensitive(root,"boot_id");
    bool valid=cJSON_IsObject(root) && cJSON_GetArraySize(root)==1 && cJSON_IsString(boot) && !strcmp(boot->valuestring,boot_id);
    cJSON_Delete(root);
    if(!valid)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid epoch");
    if(strcmp(ec_ir_control("status"),"disabled"))return response(r,"must_disable",false);
    new_epoch();return response(r,"disabled",false);
}
static esp_err_t learn_begin(httpd_req_t *r){
    if(!authorized(r))return httpd_resp_send_err(r,HTTPD_401_UNAUTHORIZED,"Unauthorized");
    if(r->content_len>0)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"No body expected");
    char *id=ec_ir_learn_begin();if(!id)return ESP_FAIL;
    char body[128];snprintf(body,sizeof(body),"{\"capture_id\":\"%s\",\"status\":\"listening\"}",id);free(id);
    httpd_resp_set_type(r,"application/json");return httpd_resp_sendstr(r,body);
}
static esp_err_t learn_get(httpd_req_t *r){
    if(!authorized(r))return httpd_resp_send_err(r,HTTPD_401_UNAUTHORIZED,"Unauthorized");
    char *body=ec_ir_learn_get();if(!body)return ESP_FAIL;
    httpd_resp_set_type(r,"application/json");esp_err_t err=httpd_resp_sendstr(r,body);free(body);return err;
}
static esp_err_t register_signal(httpd_req_t *r){
    if(!authorized(r))return httpd_resp_send_err(r,HTTPD_401_UNAUTHORIZED,"Unauthorized");
    if(!r->content_len || r->content_len>4096)return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid size");
    char *body=malloc(r->content_len+1);if(!body)return ESP_FAIL;
    int used=0;while(used<r->content_len){int n=httpd_req_recv(r,body+used,r->content_len-used);if(n<=0){free(body);return ESP_FAIL;}used+=n;}body[used]=0;
    cJSON *root=cJSON_Parse(body);free(body);
    cJSON *name=cJSON_GetObjectItemCaseSensitive(root,"name"),*signal=cJSON_GetObjectItemCaseSensitive(root,"signal");
    const char *result=cJSON_IsString(name)?ec_ir_register(name->valuestring,signal):"invalid";
    cJSON_Delete(root);return response(r,result,false);
}
/* One datagram is one attempt. No ACK, retries or backlog. HMAC key never crosses UDP. */
static void fast_worker(void *arg) {
    (void)arg;int sock=-1;uint32_t bound_ip=0;
    for(;;) {
        esp_netif_ip_info_t ip={0};esp_netif_t *sta=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        if(sta)esp_netif_get_ip_info(sta,&ip);
        if(sock>=0 && bound_ip!=ip.ip.addr){close(sock);sock=-1;}
        if(!ip.ip.addr){vTaskDelay(pdMS_TO_TICKS(200));continue;}
        if(sock<0){
            sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
            struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(7426),.sin_addr.s_addr=ip.ip.addr};
            struct timeval timeout={.tv_sec=0,.tv_usec=200000};
            if(sock<0){vTaskDelay(pdMS_TO_TICKS(200));continue;}
            setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
            if(bind(sock,(struct sockaddr*)&addr,sizeof(addr))<0){close(sock);sock=-1;vTaskDelay(pdMS_TO_TICKS(200));continue;}
            bound_ip=ip.ip.addr;
        }
        char body[192],copy[192];int n=recv(sock,body,sizeof(body)-1,0);if(n<=0)continue;vTaskDelay(1);body[n]=0;
        memcpy(copy,body,n+1);uint64_t seq;char *direction,*signature;
        if(!ec_fast_parse(copy,n,fast_epoch,esp_timer_get_time()/1000,fast_sequence,&seq,&direction,&signature))continue;
        size_t signed_len=(size_t)(strrchr(body,'|')-body);unsigned char mac[32];
        if(mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),(const unsigned char*)EC_CONTROL_TOKEN,strlen(EC_CONTROL_TOKEN),(unsigned char*)body,signed_len,mac)!=0)continue;
        unsigned diff=0;const char hex[]="0123456789abcdef";
        for(unsigned i=0;i<32;i++){diff|=(unsigned)(signature[2*i]^hex[mac[i]>>4]);diff|=(unsigned)(signature[2*i+1]^hex[mac[i]&15]);}
        if(diff)continue;
        fast_sequence=seq; /* Consume even if output is busy/blocked; never replay later. */
        const char *result=ec_ir_fast_volume(!strcmp(direction,"up")?"volume_up":"volume_down");
        printf("{\"event\":\"fast_volume\",\"direction\":\"%s\",\"status\":\"%s\"}\n",direction,result);
    }
}
void ec_control_start(void) {
    if(started)return;
    for(unsigned i=0;i<16;i++)sprintf(fast_epoch+2*i,"%02x",(unsigned)(esp_random()&255));
    new_epoch();
    httpd_ssl_config_t cfg=HTTPD_SSL_CONFIG_DEFAULT();
    cfg.servercert=cert;cfg.servercert_len=sizeof(cert);cfg.prvtkey_pem=key;cfg.prvtkey_len=sizeof(key);
    cfg.httpd.max_open_sockets=2;cfg.httpd.recv_wait_timeout=3;cfg.httpd.send_wait_timeout=3;
    httpd_handle_t server=NULL;ESP_ERROR_CHECK(httpd_ssl_start(&server,&cfg));
    httpd_uri_t routes[]={ {.uri="/v1/session",.method=HTTP_POST,.handler=renew_session}, {.uri="/v1/status",.method=HTTP_GET,.handler=status_handler}, {.uri="/v1/commands",.method=HTTP_POST,.handler=execute}, {.uri="/v1/learn",.method=HTTP_POST,.handler=learn_begin}, {.uri="/v1/learn",.method=HTTP_GET,.handler=learn_get}, {.uri="/v1/signals",.method=HTTP_POST,.handler=register_signal} };
    for(unsigned i=0;i<sizeof(routes)/sizeof(routes[0]);i++)ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));
    xTaskCreate(fast_worker,"ir_fast",6144,NULL,5,NULL);
    started=true;printf("{\"event\":\"control_ready\",\"tls\":true,\"port\":443}\n");
}
#else
void ec_control_start(void) {printf("{\"event\":\"control_unprovisioned\"}\n");}
#endif
