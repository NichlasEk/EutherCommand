/* USB serial bench node. Network control terminates at the host core.
 * No GPIO is selected until the actual board is known. */
#include <stdio.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/rmt_rx.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_mac.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "wifi_setup.h"
#include "esp_system.h"
#include "esp_random.h"
#include "freertos/semphr.h"
#include "control.h"
#include "registered_signals.h"
#include "nec_press.h"

static QueueHandle_t received;
static rmt_channel_handle_t rx, tx;
static rmt_encoder_handle_t encoder;
static rmt_symbol_word_t capture[256];
static nvs_handle_t store;
static bool disabled = true;
static bool fast_blocked;
static bool fast_operation;
static int64_t last_tx;
static int64_t arm_deadline;
static unsigned bench_sends;
static char reply_id[65];
static SemaphoreHandle_t ir_lock;
static int64_t network_until;
static unsigned network_remaining;
static int64_t learning_until;
static char learning_id[33], learned_capture[4096];
static nvs_handle_t learned_store;
static int64_t activity_until;
static void activity(void) {
    if(CONFIG_EC_ACTIVITY_GPIO<0)return;
    activity_until=esp_timer_get_time()+100000;
#ifdef CONFIG_EC_ACTIVITY_ACTIVE_LOW
    gpio_set_level(CONFIG_EC_ACTIVITY_GPIO,0);
#else
    gpio_set_level(CONFIG_EC_ACTIVITY_GPIO,1);
#endif
}
static void activity_idle(void) {
    if(CONFIG_EC_ACTIVITY_GPIO<0 || esp_timer_get_time()<activity_until)return;
#ifdef CONFIG_EC_ACTIVITY_ACTIVE_LOW
    gpio_set_level(CONFIG_EC_ACTIVITY_GPIO,1);
#else
    gpio_set_level(CONFIG_EC_ACTIVITY_GPIO,0);
#endif
}

static bool rx_done(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *event, void *context) {
    (void)channel; (void)context;
    BaseType_t wake = pdFALSE;
    xQueueSendFromISR(received, event, &wake);
    return wake == pdTRUE;
}
static void emit(const char *status) {
    printf("{\"status\":\"%s\",\"id\":\"%s\"}\n", status, reply_id);
}
static bool armed(void) {
    if (disabled || esp_timer_get_time() >= arm_deadline) return false;
    if (CONFIG_EC_ENABLE_GPIO >= 0) return gpio_get_level(CONFIG_EC_ENABLE_GPIO) == 0;
#ifdef CONFIG_EC_USB_BENCH_ARM
    return bench_sends < 3;
#else
    return false;
#endif
}
static bool signal_symbols(cJSON *signal, rmt_symbol_word_t *out, size_t *count, int *carrier) {
    cJSON *freq = cJSON_GetObjectItemCaseSensitive(signal, "carrier_hz");
    cJSON *dur = cJSON_GetObjectItemCaseSensitive(signal, "durations_us");
    int n = cJSON_GetArraySize(dur);
    if (!cJSON_IsNumber(freq) || freq->valuedouble != freq->valueint || freq->valueint < 30000 || freq->valueint > 60000 || !cJSON_IsArray(dur) || n < 3 || n > 511 || !(n & 1)) return false;
    unsigned total = 0;
    memset(out, 0, 256 * sizeof(*out));
    for (int i = 0; i < n; i++) {
        cJSON *v = cJSON_GetArrayItem(dur, i);
        if (!cJSON_IsNumber(v) || v->valuedouble != v->valueint || v->valueint < 100 || v->valueint > 30000) return false;
        total += v->valueint;
        if (i & 1) { out[i/2].level1 = 0; out[i/2].duration1 = v->valueint; }
        else { out[i/2].level0 = 1; out[i/2].duration0 = v->valueint; }
    }
    if (total > 250000) return false;
    /* End with a short LOW idle slot; carrier applies only to HIGH marks. */
    out[n/2].duration1 = 1;
    *count = (n+1)/2; *carrier = freq->valueint;
    return true;
}
static void command(char *line) {
    reply_id[0] = 0;
    cJSON *root = cJSON_Parse(line);
    if (!root) { emit("invalid"); return; }
    cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (id) {
        if (!cJSON_IsString(id) || strlen(id->valuestring) > 64 ||
            strspn(id->valuestring, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") != strlen(id->valuestring)) goto invalid;
        strcpy(reply_id, id->valuestring);
    }
    cJSON *op = cJSON_GetObjectItemCaseSensitive(root, "op");
    if (!cJSON_IsString(op)) goto invalid;
    if (!strcmp(op->valuestring, "wifi_setup")) {
        disabled = true;
        if (ec_wifi_setup_next_boot() != ESP_OK) { emit("storage_error"); goto done; }
        emit("restarting"); fflush(stdout); vTaskDelay(pdMS_TO_TICKS(100)); esp_restart();
    } else if (!strcmp(op->valuestring, "disable")) { fast_blocked=true;disabled = true; network_until=0; network_remaining=0; emit("disabled"); }
    else if (!strcmp(op->valuestring, "arm")) {
        arm_deadline = esp_timer_get_time() + 2000000;
        disabled = false;
        bool allowed = tx && armed();
        if (!allowed) disabled = true;
        emit(allowed ? "armed" : "arm_denied");
    }
    else if (!strcmp(op->valuestring, "save")) {
        cJSON *signal = cJSON_GetObjectItemCaseSensitive(root, "signal");
        rmt_symbol_word_t symbols[256]; size_t count; int carrier;
        if (!signal_symbols(signal, symbols, &count, &carrier)) goto invalid;
        char *text = cJSON_PrintUnformatted(signal);
        esp_err_t err = nvs_set_str(store, "learned", text);
        free(text);
        if (err == ESP_OK) err = nvs_commit(store);
        emit(err == ESP_OK ? "saved" : "storage_failed");
    } else if (!strcmp(op->valuestring, "replay")) {
        if (!tx || !armed()) { emit("disabled"); goto done; }
        if (esp_timer_get_time() - last_tx < 500000) { emit("rate_limited"); goto done; }
        size_t len = 0;
        if (nvs_get_str(store, "learned", NULL, &len) != ESP_OK || len > 8192) goto invalid;
        char *text = malloc(len);
        if (!text) goto invalid;
        esp_err_t err = nvs_get_str(store, "learned", text, &len);
        cJSON *signal = err == ESP_OK ? cJSON_Parse(text) : NULL;
        free(text);
        rmt_symbol_word_t symbols[256]; size_t count; int carrier;
        bool valid = signal_symbols(signal, symbols, &count, &carrier);
        cJSON_Delete(signal);
        if (!valid) goto invalid;
        rmt_carrier_config_t cc = {.frequency_hz=carrier, .duty_cycle=.33};
        rmt_transmit_config_t tc = {.loop_count=0};
        last_tx = esp_timer_get_time();
        err = rmt_apply_carrier(tx, &cc);
        if (err == ESP_OK && armed()) {
            disabled = true; /* Consume permission BEFORE any physical side effect. */
            bench_sends++;
            activity();
            err = rmt_transmit(tx, encoder, symbols, count*sizeof(*symbols), &tc);
        } else { disabled = true; err = ESP_ERR_INVALID_STATE; }
        if (err == ESP_OK) err = rmt_tx_wait_all_done(tx, 500);
        emit(err == ESP_OK ? "transmitted" : "unknown");
    } else goto invalid;
    goto done;
invalid: emit("invalid");
done: cJSON_Delete(root);
}
char *ec_ir_learn_begin(void) {
    xSemaphoreTake(ir_lock,portMAX_DELAY);
    disabled=true;network_until=0;network_remaining=0;
    learned_capture[0]=0;learning_until=esp_timer_get_time()+60000000;
    for(unsigned i=0;i<16;i++)sprintf(learning_id+2*i,"%02x",(unsigned)(esp_random()&255));
    char *id=strdup(learning_id);xSemaphoreGive(ir_lock);return id;
}
char *ec_ir_learn_get(void) {
    xSemaphoreTake(ir_lock,portMAX_DELAY);
    cJSON *root=cJSON_CreateObject();
    cJSON_AddStringToObject(root,"capture_id",learning_id);
    bool expired=esp_timer_get_time()>=learning_until;
    cJSON_AddStringToObject(root,"status",expired?"expired":learned_capture[0]?"captured":"listening");
    if(!expired && learned_capture[0])cJSON_AddItemToObject(root,"signal",cJSON_Parse(learned_capture));
    char *result=cJSON_PrintUnformatted(root);cJSON_Delete(root);xSemaphoreGive(ir_lock);return result;
}
const char *ec_ir_register(const char *name,cJSON *signal) {
    if(strlen(name)!=15 || strncmp(name,"ir_",3) || strspn(name+3,"0123456789abcdef")!=12)return "invalid";
    rmt_symbol_word_t symbols[256];size_t count;int carrier;
    if(!signal_symbols(signal,symbols,&count,&carrier))return "invalid";
    char *text=cJSON_PrintUnformatted(signal);if(!text)return "storage_error";
    if(strlen(text)>=4096){free(text);return "invalid";}
    xSemaphoreTake(ir_lock,portMAX_DELAY);
    size_t existing=0;uint8_t used=0;nvs_get_u8(learned_store,"count",&used);
    esp_err_t found=nvs_get_str(learned_store,name,NULL,&existing);
    const char *result="storage_error";
    if(found==ESP_OK){
        char *old=malloc(existing);
        if(old && nvs_get_str(learned_store,name,old,&existing)==ESP_OK)result=!strcmp(old,text)?"saved":"conflict";
        free(old);
    } else if(used>=8)result="storage_full";
    else if(found==ESP_ERR_NVS_NOT_FOUND && nvs_set_str(learned_store,name,text)==ESP_OK && nvs_set_u8(learned_store,"count",used+1)==ESP_OK && nvs_commit(learned_store)==ESP_OK)result="saved";
    xSemaphoreGive(ir_lock);free(text);return result;
}
/* Shared lock serializes USB and LAN access to the RMT peripheral. */
static const char *control_locked(const char *operation) {
    const char *result="invalid";
    int64_t now=esp_timer_get_time();
    if(!strcmp(operation,"fast_status"))result=fast_blocked?"disabled":"enabled";
    else if(!strcmp(operation,"status"))result=network_remaining && now<network_until?"enabled":"disabled";
    else if(!strcmp(operation,"disable")){fast_blocked=true;network_until=0;network_remaining=0;disabled=true;result="disabled";}
    else if(!strcmp(operation,"enable")){
        fast_blocked=false;network_until=now+60000000;network_remaining=3;result="enabled";
    } else if(!strcmp(operation,"volume_up") || !strcmp(operation,"volume_down") || (strlen(operation)==15 && !strncmp(operation,"ir_",3))) {
        if(!tx || !network_remaining || now>=network_until)result="disabled";
        else if(CONFIG_EC_ENABLE_GPIO>=0 && gpio_get_level(CONFIG_EC_ENABLE_GPIO)!=0)result="disabled";
        else if(now-last_tx<(fast_operation?160000:1000000))result="rate_limited";
        else {
            cJSON *signal=NULL;
            if(!strncmp(operation,"ir_",3)){
                size_t size=0;
                if(nvs_get_str(learned_store,operation,NULL,&size)==ESP_OK && size<=4096){
                    char *data=malloc(size);
                    if(data && nvs_get_str(learned_store,operation,data,&size)==ESP_OK)signal=cJSON_Parse(data);
                    free(data);
                }
            }else signal=cJSON_Parse(!strcmp(operation,"volume_up")?volume_up_json:volume_down_json);
            rmt_symbol_word_t symbols[256];size_t count;int carrier;
            bool valid=signal_symbols(signal,symbols,&count,&carrier);cJSON_Delete(signal);
            if(valid && (!strcmp(operation,"volume_up") || !strcmp(operation,"volume_down")))valid=ec_nec_press(symbols,&count,256);
            if(valid){
                rmt_carrier_config_t cc={.frequency_hz=carrier,.duty_cycle=.33};
                rmt_transmit_config_t tc={.loop_count=0};
                network_remaining--;last_tx=now;disabled=true;
                esp_err_t err=rmt_apply_carrier(tx,&cc);
                if(err==ESP_OK){activity();err=rmt_transmit(tx,encoder,symbols,count*sizeof(*symbols),&tc);}
                if(err==ESP_OK)err=rmt_tx_wait_all_done(tx,500);
                result=err==ESP_OK?"transmitted":"unknown";
                if(err!=ESP_OK){network_until=0;network_remaining=0;}
            }
        }
    }
    return result;
}
const char *ec_ir_control(const char *operation) {
    xSemaphoreTake(ir_lock,portMAX_DELAY);const char *result=control_locked(operation);xSemaphoreGive(ir_lock);return result;
}
const char *ec_ir_fast_volume(const char *operation) {
    if(strcmp(operation,"volume_up") && strcmp(operation,"volume_down"))return "invalid";
    if(xSemaphoreTake(ir_lock,0)!=pdTRUE)return "busy";
    const char *result="disabled";
    if(!fast_blocked && !(learning_until>esp_timer_get_time() && !learned_capture[0])) {
        fast_operation=true;network_remaining=1;network_until=esp_timer_get_time()+500000;
        result=control_locked(operation);
        network_remaining=0;network_until=0;fast_operation=false;disabled=true;
    }
    xSemaphoreGive(ir_lock);return result;
}
void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init()); /* Never erase credentials on recovery failure. */
    ESP_ERROR_CHECK(nvs_open("euthercommand", NVS_READWRITE, &store));
    ESP_ERROR_CHECK(nvs_open("ec_ir", NVS_READWRITE, &learned_store));
    uint8_t mac[6]; ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    printf("{\"event\":\"boot\",\"firmware\":\"0.1.0\",\"node\":\"ec-%02x%02x%02x%02x%02x%02x\",\"disabled\":true}\n", mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    setvbuf(stdin, NULL, _IONBF, 0);
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    if ((CONFIG_EC_TX_GPIO >= 0 && !GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_EC_TX_GPIO)) ||
        (CONFIG_EC_RX_GPIO >= 0 && !GPIO_IS_VALID_GPIO(CONFIG_EC_RX_GPIO)) ||
        (CONFIG_EC_ENABLE_GPIO >= 0 && !GPIO_IS_VALID_GPIO(CONFIG_EC_ENABLE_GPIO)) ||
        (CONFIG_EC_TX_GPIO >= 0 && (CONFIG_EC_TX_GPIO == CONFIG_EC_RX_GPIO || CONFIG_EC_TX_GPIO == CONFIG_EC_ENABLE_GPIO)) ||
        (CONFIG_EC_RX_GPIO >= 0 && CONFIG_EC_RX_GPIO == CONFIG_EC_ENABLE_GPIO)) {
        emit("invalid_pin_configuration");
        while (true) { esp_task_wdt_reset(); vTaskDelay(pdMS_TO_TICKS(100)); }
    }
    if (CONFIG_EC_ENABLE_GPIO >= 0) {
        ESP_ERROR_CHECK(gpio_set_direction(CONFIG_EC_ENABLE_GPIO, GPIO_MODE_INPUT));
        ESP_ERROR_CHECK(gpio_set_pull_mode(CONFIG_EC_ENABLE_GPIO, GPIO_PULLUP_ONLY));
    }
    if (CONFIG_EC_TX_GPIO >= 0) {
        ESP_ERROR_CHECK(gpio_set_level(CONFIG_EC_TX_GPIO, 0));
        ESP_ERROR_CHECK(gpio_set_direction(CONFIG_EC_TX_GPIO, GPIO_MODE_OUTPUT));
        rmt_tx_channel_config_t cfg = {.gpio_num=CONFIG_EC_TX_GPIO, .clk_src=RMT_CLK_SRC_DEFAULT, .resolution_hz=1000000, .mem_block_symbols=64, .trans_queue_depth=1};
        ESP_ERROR_CHECK(rmt_new_tx_channel(&cfg, &tx));
        rmt_copy_encoder_config_t ec = {};
        ESP_ERROR_CHECK(rmt_new_copy_encoder(&ec, &encoder));
        ESP_ERROR_CHECK(rmt_enable(tx));
    }
    rmt_receive_config_t rc = {.signal_range_min_ns=1250, .signal_range_max_ns=15000000};
    if (CONFIG_EC_RX_GPIO >= 0) {
        received = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
        assert(received);
        rmt_rx_channel_config_t cfg = {.gpio_num=CONFIG_EC_RX_GPIO, .clk_src=RMT_CLK_SRC_DEFAULT, .resolution_hz=1000000, .mem_block_symbols=64};
        ESP_ERROR_CHECK(rmt_new_rx_channel(&cfg, &rx));
        /* IDF 5.4 rmt_new_rx_channel enables pull-up internally. Undo it AFTER
         * channel creation so the input cannot bias the receiver's output.
         * Set input-only explicitly; this must not disconnect the RMT matrix. */
        ESP_ERROR_CHECK(gpio_set_direction(CONFIG_EC_RX_GPIO, GPIO_MODE_INPUT));
        ESP_ERROR_CHECK(gpio_set_pull_mode(CONFIG_EC_RX_GPIO, GPIO_FLOATING));
        ESP_ERROR_CHECK(gpio_dump_io_configuration(stdout, 1ULL << (CONFIG_EC_RX_GPIO >= 0 ? CONFIG_EC_RX_GPIO : 0)));
        rmt_rx_event_callbacks_t cb = {.on_recv_done=rx_done};
        ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(rx, &cb, NULL));
        ESP_ERROR_CHECK(rmt_enable(rx));
        ESP_ERROR_CHECK(rmt_receive(rx, capture, sizeof(capture), &rc));
    }
    printf("{\"event\":\"ready\",\"rx_gpio\":%d,\"tx_gpio\":%d,\"rx_level\":%d}\n", CONFIG_EC_RX_GPIO, CONFIG_EC_TX_GPIO, rx ? gpio_get_level(CONFIG_EC_RX_GPIO) : -1);
    if(CONFIG_EC_ACTIVITY_GPIO>=0){
        if(!GPIO_IS_VALID_OUTPUT_GPIO(CONFIG_EC_ACTIVITY_GPIO) || CONFIG_EC_ACTIVITY_GPIO==CONFIG_EC_RX_GPIO || CONFIG_EC_ACTIVITY_GPIO==CONFIG_EC_TX_GPIO || CONFIG_EC_ACTIVITY_GPIO==CONFIG_EC_ENABLE_GPIO){
            emit("invalid_activity_pin");abort();
        }
        ESP_ERROR_CHECK(gpio_set_direction(CONFIG_EC_ACTIVITY_GPIO,GPIO_MODE_OUTPUT));activity_idle();
    }
    ir_lock=xSemaphoreCreateMutex();assert(ir_lock);
    ec_wifi_start();
    static char line[8192]; size_t used = 0; bool overflow = false;
    while (true) {
        ESP_ERROR_CHECK(esp_task_wdt_reset());
        rmt_rx_done_event_data_t event;
        if (rx && xQueueReceive(received, &event, 0)) {
            /* Demodulating receiver active LOW. Carrier cannot be measured here. */
            cJSON *root = cJSON_CreateObject(), *a = cJSON_CreateArray();
            bool started = false; int previous = -1; unsigned duration = 0;
            for (size_t i=0; i<event.num_symbols*2; i++) {
                rmt_symbol_word_t s = event.received_symbols[i/2];
                int level = (i&1) ? s.level1 : s.level0;
                unsigned d = (i&1) ? s.duration1 : s.duration0;
                if (!d) break;
                if (!started && level) continue;
                started = true;
                if (previous != -1 && previous != level) { cJSON_AddItemToArray(a, cJSON_CreateNumber(duration)); duration=0; }
                duration += d; previous = level;
            }
            if (previous == 0) cJSON_AddItemToArray(a, cJSON_CreateNumber(duration));
            cJSON_AddStringToObject(root,"event","capture");
            cJSON_AddNumberToObject(root,"observed_us",(double)esp_timer_get_time());
            cJSON_AddBoolToObject(root,"possibly_truncated", event.num_symbols >= 64 /* classic ESP32 RX hardware capacity */);
            cJSON_AddStringToObject(root,"carrier_source","assumed_receiver_nominal");
            cJSON_AddNumberToObject(root,"carrier_hz",38000);
            cJSON_AddItemToObject(root,"durations_us",a);
            xSemaphoreTake(ir_lock,portMAX_DELAY);
            if(esp_timer_get_time()<learning_until && !learned_capture[0] && event.num_symbols<64){
                cJSON *signal=cJSON_CreateObject();cJSON_AddNumberToObject(signal,"carrier_hz",38000);cJSON_AddItemToObject(signal,"durations_us",cJSON_Duplicate(a,true));
                rmt_symbol_word_t symbols[256];size_t count;int carrier;
                if(signal_symbols(signal,symbols,&count,&carrier)){
                    activity();
                    char *text=cJSON_PrintUnformatted(signal);
                    if(text && strlen(text)<sizeof(learned_capture))strcpy(learned_capture,text);
                    free(text);
                }cJSON_Delete(signal);
            }
            xSemaphoreGive(ir_lock);
            char *text = cJSON_PrintUnformatted(root); puts(text); free(text); cJSON_Delete(root);
            ESP_ERROR_CHECK(rmt_receive(rx, capture, sizeof(capture), &rc));
        }
        unsigned char ch;
        /* Drain a bounded batch: one byte per 10 ms tick would time out on JSON. */
        for (unsigned budget = 0; budget < 128 && read(STDIN_FILENO, &ch, 1) == 1; budget++) {
            if (ch == '\n') { if (!overflow) {line[used]=0; xSemaphoreTake(ir_lock,portMAX_DELAY); command(line); xSemaphoreGive(ir_lock);} else emit("oversized"); used=0; overflow=false; }
            else if (used < sizeof(line)-1 && !overflow) line[used++]=ch;
            else overflow=true;
        }
        xSemaphoreTake(ir_lock,portMAX_DELAY);activity_idle();xSemaphoreGive(ir_lock);
        vTaskDelay(1); /* At least one tick so the idle watchdog can run. */
    }
}
