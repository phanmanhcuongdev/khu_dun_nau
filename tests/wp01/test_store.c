#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../main/src/latch_store.c"
int task_ok=1,queue_ok=1;
static uint8_t disk[RECORD_SIZE],staged[RECORD_SIZE];
static size_t disk_len;
static int missing=1,commit_fail,set_fail,read_fail,open_fail;
esp_err_t nvs_open(const char *n,int mode,nvs_handle_t *h){(void)n;(void)mode;*h=1;return open_fail?ESP_FAIL:ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *k,void *p,size_t *n){(void)h;(void)k;if(read_fail)return ESP_FAIL;if(missing)return ESP_ERR_NVS_NOT_FOUND;if(*n<disk_len)return ESP_ERR_INVALID_SIZE;memcpy(p,disk,disk_len);*n=disk_len;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *k,const void *p,size_t n){(void)h;(void)k;assert(n==RECORD_SIZE);memcpy(staged,p,n);return set_fail?ESP_FAIL:ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;if(commit_fail)return ESP_FAIL;memcpy(disk,staged,RECORD_SIZE);disk_len=RECORD_SIZE;missing=0;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
static void commit(bool latched,uint32_t ack){uint32_t gen;assert(latch_store_request(latched,ack,&gen)==ESP_OK);request_t r={latched,ack,gen};assert(process_request(&r));assert(latch_store_status().health==LATCH_STORE_DURABLE);}
int main(void){
 assert(latch_store_init(ESP_OK)==ESP_OK);assert(latch_store_status().latched && latch_store_status().health==LATCH_STORE_FAULT);
 commit(true,0);assert(latch_store_init(ESP_OK)==ESP_OK && latch_store_status().latched);
 for(unsigned i=1;i<=100;i++){commit(false,i);assert(latch_store_init(ESP_OK)==ESP_OK && !latch_store_status().latched && latch_store_status().ack_seq==i);commit(true,i);}
 uint32_t gen;commit_fail=1;latch_store_request(false,101,&gen);request_t clear={false,101,gen};assert(!process_request(&clear));assert(latch_store_status().latched && latch_store_status().health==LATCH_STORE_FAULT);
 commit_fail=0;assert(latch_store_init(ESP_OK)==ESP_OK && latch_store_status().latched);
 set_fail=1;latch_store_request(false,101,&gen);clear.generation=gen;assert(!process_request(&clear));set_fail=0;
 // New SET supersedes queued CLEAR, stale completion cannot delete record.
 latch_store_request(false,101,&gen);clear.generation=gen;uint32_t newer;latch_store_request(true,100,&newer);assert(process_request(&clear));assert(latch_store_status().latched);
 disk[0]^=1;assert(latch_store_init(ESP_OK)==ESP_OK && latch_store_status().latched && latch_store_status().health==LATCH_STORE_FAULT);
 commit(true,100);disk[4]=2;assert(latch_store_init(ESP_OK)==ESP_OK && latch_store_status().health==LATCH_STORE_FAULT);
 missing=0;disk_len=3;assert(latch_store_init(ESP_OK)==ESP_OK && latch_store_status().latched);
 read_fail=1;assert(latch_store_init(ESP_OK)==ESP_OK && latch_store_status().latched);read_fail=0;
 assert(latch_store_init(ESP_FAIL)!=ESP_OK && latch_store_status().latched && !latch_store_status().available);
 open_fail=1;assert(latch_store_init(ESP_OK)!=ESP_OK && latch_store_status().latched);open_fail=0;
 assert(latch_store_init(ESP_OK)==ESP_OK);commit(false,100);queue_ok=0;assert(latch_store_init(ESP_OK)!=ESP_OK && latch_store_status().latched && latch_store_status().health==LATCH_STORE_FAULT);
 queue_ok=1;task_ok=0;assert(latch_store_init(ESP_OK)!=ESP_OK && latch_store_status().latched && latch_store_status().health==LATCH_STORE_FAULT);
 puts("WP01 real store codec/version/CRC, missing/corrupt/short record, failures, stale command, 100 set/clear/reload cycles: PASS (mock NVS, not power-cut proof)");
}
