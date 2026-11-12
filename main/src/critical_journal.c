#include "critical_journal.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#define HEADER 56
static nvs_handle_t handle;
static bool ready;
static journal_entry_t entries[JOURNAL_SLOTS];
static bool blocked[JOURNAL_SLOTS];
static uint32_t loss;
static uint8_t wire[CLOUD_JSON_MAX+HEADER];
static uint32_t crc(const uint8_t *p,size_t n){uint32_t c=~0U;for(size_t i=0;i<n;i++){c^=p[i];for(int b=0;b<8;b++)c=(c>>1)^(0xedb88320U&(0U-(c&1U)));}return ~c;}
static void put(uint8_t *p,uint64_t v,int n){for(int i=0;i<n;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint64_t get(const uint8_t *p,int n){uint64_t v=0;for(int i=0;i<n;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static void key(int slot,char k[8]){snprintf(k,8,"ev%d",slot);}
static bool valid(size_t len){return len>=HEADER+1&&len<=sizeof(wire)&&get(wire,4)==0x314a4350&&get(wire+4,4)==1&&wire[40]==0&&wire[len-1]==0&&get(wire+52,4)==(crc(wire,52)^crc(wire+HEADER,len-HEADER))&&get(wire+48,4)==len-HEADER;}
static esp_err_t load(int slot){char k[8];key(slot,k);size_t len=sizeof(wire);esp_err_t e=nvs_get_blob(handle,k,wire,&len);if(e!=ESP_OK)return e;return valid(len)?ESP_OK:ESP_ERR_INVALID_CRC;}
esp_err_t critical_journal_init(void)
{
 ready=false;memset(entries,0,sizeof(entries));memset(blocked,0,sizeof(blocked));esp_err_t e=nvs_open("pccc_events",NVS_READWRITE,&handle);
 if(e!=ESP_OK){loss++;return e;}ready=true;
 for(int i=0;i<JOURNAL_SLOTS;i++){
  e=load(i);if(e==ESP_ERR_NVS_NOT_FOUND)continue;
  if(e!=ESP_OK){blocked[i]=true;loss++;ESP_LOGE("EVENT_JOURNAL","slot%d corrupt/unreadable: %s",i,esp_err_to_name(e));continue;}
  entries[i].used=true;memcpy(entries[i].boot,wire+8,33);entries[i].seq=get(wire+41,7);
 }
 return ESP_OK;
}
journal_entry_t critical_journal_entry(int slot){return entries[slot];}
esp_err_t critical_journal_add(const cloud_record_t *r,const char *json)
{
 if(!ready){loss++;return ESP_ERR_INVALID_STATE;}
 int slot=-1;for(int i=0;i<JOURNAL_SLOTS;i++){
  if(entries[i].used&&!strcmp(entries[i].boot,r->boot_id)&&entries[i].seq==r->seq)return ESP_OK;
  if(!entries[i].used&&!blocked[i])slot=i;
 }
 if(slot<0){loss++;return ESP_ERR_NO_MEM;}
 size_t len=strlen(json)+1;if(len>CLOUD_JSON_MAX||r->seq>9007199254740991ULL){loss++;return ESP_ERR_INVALID_SIZE;}
 memset(wire,0,HEADER);put(wire,0x314a4350,4);put(wire+4,1,4);memcpy(wire+8,r->boot_id,33);put(wire+41,r->seq,7);put(wire+48,len,4);memcpy(wire+HEADER,json,len);put(wire+52,crc(wire,52)^crc(wire+HEADER,len),4);
 char k[8];key(slot,k);esp_err_t e=nvs_set_blob(handle,k,wire,HEADER+len);if(e==ESP_OK)e=nvs_commit(handle);
 if(e==ESP_OK)e=load(slot);
 if(e==ESP_OK && (memcmp(wire+8,r->boot_id,33)||get(wire+41,7)!=r->seq||strcmp((const char*)wire+HEADER,json)))e=ESP_ERR_INVALID_CRC;
 if(e==ESP_OK){entries[slot].used=true;memcpy(entries[slot].boot,r->boot_id,33);entries[slot].seq=r->seq;}
 else loss++;
 return e;
}
esp_err_t critical_journal_read(int slot,char *json,size_t cap)
{
 esp_err_t e=load(slot);if(e!=ESP_OK){loss++;entries[slot].used=false;blocked[slot]=true;return e;}size_t len=get(wire+48,4);
 if(len>cap)return ESP_ERR_INVALID_SIZE;
 memcpy(json,wire+HEADER,len);return ESP_OK;
}
void critical_journal_complete(const char *boot,uint64_t seq)
{
 for(int i=0;i<JOURNAL_SLOTS;i++)if(entries[i].used&&!strcmp(entries[i].boot,boot)&&entries[i].seq==seq){
  char k[8];key(i,k);esp_err_t e=nvs_erase_key(handle,k);if(e==ESP_OK)e=nvs_commit(handle);
  if(e==ESP_OK || e==ESP_ERR_NVS_NOT_FOUND)entries[i].used=false;else loss++;
 }
}
uint32_t critical_journal_losses(void){return loss;}
