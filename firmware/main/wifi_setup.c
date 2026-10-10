/* Isolated, time-bounded provisioning. No IR operations are exposed over HTTP. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_random.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs.h"
#include "cJSON.h"
#include "lwip/sockets.h"
#include "setup_address.h"
#include "wifi_setup.h"
#include "control.h"
#if __has_include("wifi_setup_local.h")
#include "wifi_setup_local.h"
#endif
#ifdef EC_LOCAL_SETUP_PASSWORD
_Static_assert(sizeof(EC_LOCAL_SETUP_PASSWORD) >= 9 && sizeof(EC_LOCAL_SETUP_PASSWORD) <= 64, "Setup password must be 8-63 bytes");
#endif

#define CONNECTED BIT0
#define BUSY BIT1
#define SAVED BIT2
#define FAILED BIT3
static EventGroupHandle_t state;
static QueueHandle_t pending;
static esp_netif_t *station, *ap;
static char csrf[33];
extern const char html_start[] asm("_binary_setup_html_start");

static void random_hex(char *out, size_t bytes) {
    for(size_t i=0;i<bytes;i++) sprintf(out+i*2,"%02x",(unsigned)(esp_random() & 255));
}
static void event(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if(base==WIFI_EVENT && (id==WIFI_EVENT_AP_STACONNECTED || id==WIFI_EVENT_AP_STADISCONNECTED)) {
        printf("{\"event\":\"setup_client\",\"connected\":%s}\n",id==WIFI_EVENT_AP_STACONNECTED?"true":"false");
    }
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(state, CONNECTED);
        ip_event_got_ip_t *got=data;
        printf("{\"event\":\"wifi_connected\",\"ip\":\"" IPSTR "\"}\n",IP2STR(&got->ip_info.ip));
    } else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(state, CONNECTED);
    }
}
/* HTTPD listens on both interfaces: reject every request arriving at STA. */
static bool from_ap(httpd_req_t *r) {
    struct sockaddr_storage local={0}; socklen_t len=sizeof(local);
    esp_netif_ip_info_t ip;
    bool allowed=getsockname(httpd_req_to_sockfd(r),(struct sockaddr*)&local,&len)==0 &&
        esp_netif_get_ip_info(ap,&ip)==ESP_OK && ec_setup_address(&local,len,ip.ip.addr);
    printf("{\"event\":\"setup_http\",\"allowed\":%s,\"family\":%d}\n",allowed?"true":"false",local.ss_family);
    return allowed;
}
static esp_err_t deny(httpd_req_t *r) { return httpd_resp_send_err(r,HTTPD_403_FORBIDDEN,"Setup network only"); }
static void headers(httpd_req_t *r) {
    httpd_resp_set_hdr(r,"Cache-Control","no-store");
    httpd_resp_set_hdr(r,"X-Content-Type-Options","nosniff");
    httpd_resp_set_hdr(r,"X-Frame-Options","DENY");
    httpd_resp_set_hdr(r,"Content-Security-Policy","default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; frame-ancestors 'none'; form-action 'self'");
}
static esp_err_t page(httpd_req_t *r) {
    if(!from_ap(r)) return deny(r);
    headers(r); httpd_resp_set_type(r,"text/html; charset=utf-8");
    /* Replace one explicit placeholder; HTML contains literal percent signs. */
    const char *p=strstr(html_start,"%s");
    if(!p) return ESP_FAIL;
    httpd_resp_send_chunk(r,html_start,p-html_start);
    httpd_resp_send_chunk(r,csrf,strlen(csrf));
    httpd_resp_send_chunk(r,p+2,strlen(p+2));
    return httpd_resp_send_chunk(r,NULL,0);
}
static esp_err_t status(httpd_req_t *r) {
    if(!from_ap(r)) return deny(r);
    headers(r); httpd_resp_set_type(r,"application/json");
    EventBits_t bits=xEventGroupGetBits(state); esp_netif_ip_info_t ip={0};
    esp_netif_get_ip_info(station,&ip);
    char body[120]; snprintf(body,sizeof(body),"{\"state\":\"%s\",\"ip\":\"" IPSTR "\"}",
        bits&SAVED?"saved":bits&BUSY?"connecting":bits&FAILED?"failed":"ready",IP2STR(&ip.ip));
    return httpd_resp_sendstr(r,body);
}
static esp_err_t configure(httpd_req_t *r) {
    if(!from_ap(r)) return deny(r);
    char token[40];
    if(httpd_req_get_hdr_value_str(r,"X-Setup-Token",token,sizeof(token))!=ESP_OK || strcmp(token,csrf)) return deny(r);
    if(r->content_len<1 || r->content_len>512) return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid size");
    if(xEventGroupGetBits(state)&(BUSY|SAVED)) return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Busy");
    char body[513]; size_t used=0;
    while(used<r->content_len) {
        int n=httpd_req_recv(r,body+used,r->content_len-used);
        if(n<=0) return ESP_FAIL;
        used+=n;
    }
    body[used]=0;
    cJSON *root=cJSON_ParseWithLength(body,used+1);
    cJSON *ssid=cJSON_GetObjectItemCaseSensitive(root,"ssid"), *password=cJSON_GetObjectItemCaseSensitive(root,"password");
    wifi_config_t cfg={0};
    bool valid=cJSON_IsString(ssid)&&cJSON_IsString(password) && strlen(ssid->valuestring)>0 &&
        strlen(ssid->valuestring)<=32 && strlen(password->valuestring)>=8 && strlen(password->valuestring)<=63;
    if(valid) {
        memcpy(cfg.sta.ssid,ssid->valuestring,strlen(ssid->valuestring));
        memcpy(cfg.sta.password,password->valuestring,strlen(password->valuestring));
        cfg.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
        cfg.sta.pmf_cfg.capable=true;
    }
    cJSON_Delete(root); memset(body,0,sizeof(body));
    if(!valid) return httpd_resp_send_err(r,HTTPD_400_BAD_REQUEST,"Invalid credentials");
    xEventGroupClearBits(state,FAILED); xEventGroupSetBits(state,BUSY);
    if(xQueueSend(pending,&cfg,0)!=pdTRUE) { xEventGroupClearBits(state,BUSY); memset(&cfg,0,sizeof(cfg)); return ESP_FAIL; }
    memset(&cfg,0,sizeof(cfg)); headers(r);
    return httpd_resp_sendstr(r,"{\"status\":\"connecting\"}");
}
esp_err_t ec_wifi_setup_next_boot(void) {
    nvs_handle_t n; esp_err_t e=nvs_open("ec_wifi",NVS_READWRITE,&n);
    if(e!=ESP_OK) return e;
    e=nvs_set_u8(n,"setup",1); if(e==ESP_OK)e=nvs_commit(n); nvs_close(n); return e;
}
static esp_err_t opened(httpd_handle_t server, int fd) {
    printf("{\"event\":\"setup_tcp_open\"}\n");
    return ESP_OK;
}
/* Exercise the real on-device TCP listener and page handler without credentials. */
static void selftest(void) {
    int fd=socket(AF_INET,SOCK_STREAM,0);
    if(fd<0)return;
    struct timeval timeout={.tv_sec=3};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons(80)};
    inet_pton(AF_INET,"192.168.4.1",&addr.sin_addr);
    int code=0;
    if(connect(fd,(struct sockaddr*)&addr,sizeof(addr))==0) {
        const char request[]="GET / HTTP/1.1\r\nHost: 192.168.4.1\r\nConnection: close\r\n\r\n";
        if(send(fd,request,sizeof(request)-1,0)>0) {
            char response[512]={0}; int n=recv(fd,response,sizeof(response)-1,0);
            if(n>0)sscanf(response,"HTTP/%*s %d",&code);
            /* Drain Connection: close response instead of resetting mid-page. */
            while(n>0)n=recv(fd,response,sizeof(response),0);
        }
    }
    close(fd);
    printf("{\"event\":\"setup_http_selftest\",\"status\":%d}\n",code);
}
static void run(void *arg) {
    nvs_handle_t n; ESP_ERROR_CHECK(nvs_open("ec_wifi",NVS_READWRITE,&n));
    wifi_config_t cfg={0}; size_t len=sizeof(cfg); uint8_t force=0;
    bool saved=nvs_get_blob(n,"station",&cfg,&len)==ESP_OK && len==sizeof(cfg);
    nvs_get_u8(n,"setup",&force); nvs_erase_key(n,"setup"); ESP_ERROR_CHECK(nvs_commit(n));
    bool setup=!saved || force;
    ESP_ERROR_CHECK(esp_wifi_set_mode(setup?WIFI_MODE_APSTA:WIFI_MODE_STA));
    if(saved) ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&cfg));
    char password[64]={0}, ssid[33]={0};
    if(setup) {
        uint8_t mac[6]; esp_read_mac(mac,ESP_MAC_WIFI_STA);
        #ifdef EC_LOCAL_SETUP_PASSWORD
        strcpy(password, EC_LOCAL_SETUP_PASSWORD);
#else
        random_hex(password,12);
#endif
        random_hex(csrf,16);
        snprintf(ssid,sizeof(ssid),"EutherCommand-%02X%02X%02X",mac[3],mac[4],mac[5]);
        wifi_config_t ac={0}; strcpy((char*)ac.ap.ssid,ssid); strcpy((char*)ac.ap.password,password);
        ac.ap.ssid_len=strlen(ssid); ac.ap.channel=1; ac.ap.max_connection=1; ac.ap.authmode=WIFI_AUTH_WPA2_PSK;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&ac)); memset(&ac,0,sizeof(ac));
    }
    ESP_ERROR_CHECK(esp_wifi_start());
    if(saved && !setup) esp_wifi_connect();
    httpd_handle_t server=NULL;
    if(setup) {
        httpd_config_t hc=HTTPD_DEFAULT_CONFIG(); hc.stack_size=6144; hc.recv_wait_timeout=3; hc.send_wait_timeout=3; hc.open_fn=opened; hc.lru_purge_enable=true;
        ESP_ERROR_CHECK(httpd_start(&server,&hc));
        httpd_uri_t routes[]={ {.uri="/",.method=HTTP_GET,.handler=page}, {.uri="/configure",.method=HTTP_POST,.handler=configure}, {.uri="/status",.method=HTTP_GET,.handler=status} };
        for(int i=0;i<3;i++)ESP_ERROR_CHECK(httpd_register_uri_handler(server,&routes[i]));
        /* Physical USB bootstrap only. Never expose this password on network logs. */
        printf("{\"event\":\"wifi_setup\",\"ssid\":\"%s\",\"password\":\"%s\",\"url\":\"http://192.168.4.1\",\"expires_seconds\":600}\n",ssid,password);
        memset(password,0,sizeof(password));
        selftest();
        int64_t end=esp_timer_get_time()+600000000;
        while(esp_timer_get_time()<end) {
            wifi_config_t next;
            if(xQueueReceive(pending,&next,pdMS_TO_TICKS(500))!=pdTRUE)continue;
            esp_wifi_disconnect(); vTaskDelay(pdMS_TO_TICKS(100)); xEventGroupClearBits(state,CONNECTED);
            esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&next);
            if(err==ESP_OK)err=esp_wifi_connect();
            bool ok=err==ESP_OK && (xEventGroupWaitBits(state,CONNECTED,pdFALSE,pdTRUE,pdMS_TO_TICKS(30000))&CONNECTED);
            if(ok) { err=nvs_set_blob(n,"station",&next,sizeof(next)); if(err==ESP_OK)err=nvs_commit(n); ok=err==ESP_OK; }
            if(ok){cfg=next;saved=true;}
            memset(&next,0,sizeof(next));
            xEventGroupClearBits(state,BUSY); xEventGroupSetBits(state,ok?SAVED:FAILED);
            if(ok){vTaskDelay(pdMS_TO_TICKS(15000));break;}
            esp_wifi_disconnect();
        }
        httpd_stop(server); ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        printf("{\"event\":\"wifi_setup_closed\"}\n");
        if(saved)esp_wifi_set_config(WIFI_IF_STA,&cfg);
    }
    memset(&cfg,0,sizeof(cfg)); nvs_close(n);
    if(saved)ec_control_start();
    for(;;) { if(saved && !(xEventGroupGetBits(state)&CONNECTED))esp_wifi_connect(); vTaskDelay(pdMS_TO_TICKS(10000)); }
}
void ec_wifi_start(void) {
    state=xEventGroupCreate(); pending=xQueueCreate(1,sizeof(wifi_config_t)); assert(state && pending);
    ESP_ERROR_CHECK(esp_netif_init()); ESP_ERROR_CHECK(esp_event_loop_create_default());
    station=esp_netif_create_default_wifi_sta(); ap=esp_netif_create_default_wifi_ap(); assert(station && ap);
    esp_netif_set_hostname(station,"euthercommand");
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT(); ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL));
    assert(xTaskCreate(run,"wifi_setup",6144,NULL,4,NULL)==pdPASS);
}
