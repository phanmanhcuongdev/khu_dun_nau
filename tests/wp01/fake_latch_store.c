#include "latch_store.h"
static latch_store_status_t status={.available=true,.health=LATCH_STORE_DURABLE};
static uint32_t generation;
static bool automatic=true,pending_latched;
static uint32_t pending_ack;
void fake_store_reset(void){status=(latch_store_status_t){.available=true,.health=LATCH_STORE_DURABLE};generation=0;automatic=true;}
void fake_store_fault(void){status.health=LATCH_STORE_FAULT;status.available=false;status.latched=true;}
void fake_store_auto(bool enabled){automatic=enabled;}
void fake_store_fail(void){status.health=LATCH_STORE_FAULT;}
void fake_store_complete(void){status.latched=pending_latched;status.ack_seq=pending_ack;status.committed_generation=generation;status.health=LATCH_STORE_DURABLE;}
esp_err_t latch_store_init(esp_err_t e){return e;}
latch_store_status_t latch_store_status(void){return status;}
esp_err_t latch_store_request(bool latched,uint32_t ack,uint32_t *gen){
 if(!status.available)return -1;
 pending_latched=latched;pending_ack=ack;*gen=++generation;status.health=LATCH_STORE_PENDING;
 if(automatic)fake_store_complete();
 return ESP_OK;
}
