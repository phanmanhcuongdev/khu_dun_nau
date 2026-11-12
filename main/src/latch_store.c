#include "latch_store.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
/* Explicit little-endian wire format: magic/version/latched/ack/generation/CRC32.
 * NVS supplies atomic blob replacement; CRC/version reject malformed records.
 * Async persistence has an uncommitted exposure window. No power-cut guarantee. */
#define RECORD_SIZE 24
#define RECORD_MAGIC 0x31434350U
static const char *TAG="LATCH_STORE";
typedef struct { bool latched; uint32_t ack, generation; } request_t;
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static nvs_handle_t s_nvs;
static QueueHandle_t s_queue;
static uint32_t s_generation;
static latch_store_status_t s_status={.latched=true,.health=LATCH_STORE_FAULT};
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(uint8_t *p,uint32_t v){for(int i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint32_t crc32(const uint8_t *p,size_t n)
{
    uint32_t c=~0U;
    for(size_t i=0;i<n;i++){c^=p[i];for(int b=0;b<8;b++)c=(c>>1)^(0xedb88320U & (0U-(c&1U)));}
    return ~c;
}
static void encode(const request_t *r,uint8_t b[RECORD_SIZE])
{
    put32(b,RECORD_MAGIC);put32(b+4,1);put32(b+8,r->latched?1:0);
    put32(b+12,r->ack);put32(b+16,r->generation);put32(b+20,crc32(b,20));
}
static bool valid(const uint8_t *b,size_t n)
{
    return n==RECORD_SIZE && get32(b)==RECORD_MAGIC && get32(b+4)==1 &&
        get32(b+8)<=1 && get32(b+20)==crc32(b,20);
}
latch_store_status_t latch_store_status(void)
{
    portENTER_CRITICAL(&s_lock);latch_store_status_t s=s_status;portEXIT_CRITICAL(&s_lock);return s;
}
static bool current(uint32_t gen)
{
    portENTER_CRITICAL(&s_lock);bool yes=gen==s_generation;portEXIT_CRITICAL(&s_lock);return yes;
}
static bool process_request(const request_t *r)
{
    if(!current(r->generation))return true; /* Superseded clear must not be applied. */
    uint8_t b[RECORD_SIZE],verify[RECORD_SIZE];encode(r,b);
    esp_err_t err=nvs_set_blob(s_nvs,"state",b,sizeof(b));
    if(err==ESP_OK && current(r->generation))err=nvs_commit(s_nvs);
    else if(err==ESP_OK) return true;
    size_t len=sizeof(verify);
    if(err==ESP_OK)err=nvs_get_blob(s_nvs,"state",verify,&len);
    if(err==ESP_OK && (!valid(verify,len)||memcmp(b,verify,sizeof(b))))err=ESP_ERR_INVALID_CRC;
    portENTER_CRITICAL(&s_lock);
    if(err==ESP_OK){
        s_status.latched=r->latched;s_status.ack_seq=r->ack;
        s_status.committed_generation=r->generation;
    }
    if(r->generation==s_generation){s_status.error=err;s_status.health=err==ESP_OK?LATCH_STORE_DURABLE:LATCH_STORE_FAULT;}
    portEXIT_CRITICAL(&s_lock);
    if(err==ESP_OK)ESP_LOGI(TAG,"Committed latch=%d ack_seq=%lu generation=%lu",r->latched,(unsigned long)r->ack,(unsigned long)r->generation);
    else ESP_LOGE(TAG,"Persistence failed: %s; runtime latch retained",esp_err_to_name(err));
    return err==ESP_OK;
}
static void worker(void *arg)
{
    (void)arg;request_t r;
    for(;;){
        if(xQueueReceive(s_queue,&r,portMAX_DELAY)!=pdTRUE)continue;
        /* Bounded retries per transition, never every sensor evaluation. */
        for(int attempt=0;attempt<3;attempt++){
            if(process_request(&r))break;
            if(attempt<2)vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}
esp_err_t latch_store_init(esp_err_t nvs_result)
{
    s_status=(latch_store_status_t){.latched=true,.health=LATCH_STORE_FAULT,.error=nvs_result};
    if(nvs_result!=ESP_OK)return nvs_result;
    esp_err_t err=nvs_open("pccc_latch",NVS_READWRITE,&s_nvs);
    if(err!=ESP_OK){s_status.error=err;return err;}
    uint8_t b[RECORD_SIZE];size_t len=sizeof(b);
    err=nvs_get_blob(s_nvs,"state",b,&len);
    if(err==ESP_OK && valid(b,len)){
        s_status.latched=get32(b+8)!=0;s_status.ack_seq=get32(b+12);
        s_generation=get32(b+16);s_status.committed_generation=s_generation;
        s_status.health=LATCH_STORE_DURABLE;
    }else{
        s_generation=0;s_status.error=err==ESP_OK?ESP_ERR_INVALID_CRC:err;
        ESP_LOGE(TAG,"Record unavailable/invalid (%s): restoring conservative latch; physical ACK required",esp_err_to_name(s_status.error));
    }
    s_queue=xQueueCreate(1,sizeof(request_t));
    if(!s_queue){nvs_close(s_nvs);s_status.error=ESP_ERR_NO_MEM;s_status.health=LATCH_STORE_FAULT;s_status.latched=true;return ESP_ERR_NO_MEM;}
    if(xTaskCreate(worker,"latch_nvs",3072,NULL,1,NULL)!=pdPASS){
        vQueueDelete(s_queue);s_queue=NULL;nvs_close(s_nvs);s_status.error=ESP_ERR_NO_MEM;s_status.health=LATCH_STORE_FAULT;s_status.latched=true;return ESP_ERR_NO_MEM;
    }
    s_status.available=true;
    ESP_LOGI(TAG,"Restore latch=%d ack_seq=%lu health=%d",s_status.latched,(unsigned long)s_status.ack_seq,s_status.health);
    return ESP_OK;
}
esp_err_t latch_store_request(bool latched,uint32_t ack,uint32_t *generation)
{
    if(!s_queue || !s_status.available)return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&s_lock);
    if(s_generation==UINT32_MAX){
        s_status.health=LATCH_STORE_FAULT;s_status.error=ESP_ERR_INVALID_STATE;
        portEXIT_CRITICAL(&s_lock);return ESP_ERR_INVALID_STATE;
    }
    request_t r={.latched=latched,.ack=ack,.generation=++s_generation};
    s_status.health=LATCH_STORE_PENDING;s_status.error=ESP_OK;
    portEXIT_CRITICAL(&s_lock);
    if(generation)*generation=r.generation;
    return xQueueOverwrite(s_queue,&r)==pdPASS?ESP_OK:ESP_FAIL;
}
