#include "event_buffer.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static struct { kitchen_event_t event; bool used,claimed,journal_attempted; } slots[EVENT_BUFFER_MAX_CAPACITY];
static event_stats_t stats;
static unsigned count,claimed;
static bool same(const cloud_record_t *r,const char *boot,uint64_t seq){return r->seq==seq && strcmp(r->boot_id,boot)==0;}
esp_err_t event_buffer_init(void){memset(slots,0,sizeof(slots));memset(&stats,0,sizeof(stats));count=claimed=0;return ESP_OK;}
esp_err_t event_buffer_push(const kitchen_event_t *e)
{
 if(!e)return ESP_ERR_INVALID_ARG;
 portENTER_CRITICAL(&lock);stats.created++;
 int free_slot=-1,worst=-1;
 for(int i=0;i<EVENT_BUFFER_MAX_CAPACITY;i++){
  if(!slots[i].used){free_slot=i;continue;}
  if(same(&slots[i].event.record,e->record.boot_id,e->record.seq)){stats.duplicates++;portEXIT_CRITICAL(&lock);return ESP_OK;}
  if(!slots[i].claimed && slots[i].event.priority>e->priority && (worst<0 || slots[i].event.priority>slots[worst].event.priority))worst=i;
 }
 if(free_slot<0){
  if(worst<0){stats.rejected++;portEXIT_CRITICAL(&lock);return ESP_ERR_NO_MEM;}
  free_slot=worst;stats.evicted++;
 }else count++;
 slots[free_slot].event=*e;slots[free_slot].used=true;slots[free_slot].claimed=false;slots[free_slot].journal_attempted=false;stats.queued++;
 portEXIT_CRITICAL(&lock);return ESP_OK;
}
esp_err_t event_buffer_claim(kitchen_event_t *out)
{
 if(!out)return ESP_ERR_INVALID_ARG;
 portENTER_CRITICAL(&lock);
 if(claimed>=EVENT_INFLIGHT_MAX){portEXIT_CRITICAL(&lock);return ESP_ERR_INVALID_STATE;}
 int best=-1;
 for(int i=0;i<EVENT_BUFFER_MAX_CAPACITY;i++)if(slots[i].used && !slots[i].claimed &&
  (best<0 || slots[i].event.priority<slots[best].event.priority ||
  (slots[i].event.priority==slots[best].event.priority && slots[i].event.record.seq<slots[best].event.record.seq)))best=i;
 if(best<0){portEXIT_CRITICAL(&lock);return ESP_ERR_NOT_FOUND;}
 slots[best].claimed=true;claimed++;*out=slots[best].event;
 portEXIT_CRITICAL(&lock);return ESP_OK;
}
esp_err_t event_buffer_complete(const char *boot,uint64_t seq)
{
 portENTER_CRITICAL(&lock);
 for(int i=0;i<EVENT_BUFFER_MAX_CAPACITY;i++)if(slots[i].used && same(&slots[i].event.record,boot,seq)){
  if(slots[i].claimed){claimed--;}
  slots[i].used=false;slots[i].claimed=false;count--;stats.completed++;
  portEXIT_CRITICAL(&lock);return ESP_OK;
 }
 portEXIT_CRITICAL(&lock);return ESP_ERR_NOT_FOUND;
}
void event_buffer_retry(const char *boot,uint64_t seq,bool expired)
{
 portENTER_CRITICAL(&lock);
 for(int i=0;i<EVENT_BUFFER_MAX_CAPACITY;i++)if(slots[i].used && same(&slots[i].event.record,boot,seq)){
  if(slots[i].claimed){slots[i].claimed=false;claimed--;}
  slots[i].event.record.replayed=true;stats.retries++;if(expired)stats.expired++;break;
 }
 portEXIT_CRITICAL(&lock);
}
void event_buffer_reconnect(void)
{
 portENTER_CRITICAL(&lock);
 for(int i=0;i<EVENT_BUFFER_MAX_CAPACITY;i++)if(slots[i].used){slots[i].claimed=false;slots[i].event.record.replayed=true;}
 claimed=0;portEXIT_CRITICAL(&lock);
}
size_t event_buffer_get_count(void){portENTER_CRITICAL(&lock);size_t n=count;portEXIT_CRITICAL(&lock);return n;}
event_stats_t event_buffer_stats(void){portENTER_CRITICAL(&lock);event_stats_t s=stats;portEXIT_CRITICAL(&lock);return s;}

bool event_buffer_journal_candidate(kitchen_event_t *out)
{
 portENTER_CRITICAL(&lock);
 for(int i=0;i<EVENT_BUFFER_MAX_CAPACITY;i++)if(slots[i].used&&!slots[i].journal_attempted&&slots[i].event.priority==EVENT_PRIO_EMERGENCY){
  slots[i].journal_attempted=true;*out=slots[i].event;portEXIT_CRITICAL(&lock);return true;
 }
 portEXIT_CRITICAL(&lock);return false;
}
void event_buffer_accepted(void){portENTER_CRITICAL(&lock);stats.accepted++;portEXIT_CRITICAL(&lock);}

void event_buffer_journal_rescan(void)
{
 portENTER_CRITICAL(&lock);
 for(int i=0;i<EVENT_BUFFER_MAX_CAPACITY;i++)if(slots[i].used)slots[i].journal_attempted=false;
 portEXIT_CRITICAL(&lock);
}
