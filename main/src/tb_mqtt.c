#include "tb_mqtt.h"
#include "wifi_app.h"
#include "cloud_codec.h"
#include "critical_journal.h"
#include "mqtt_delivery.h"
#include "publish_policy.h"
#include "clock_policy.h"
#include "pccc_build_config.h"
#include "mqtt_client.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#define TELEMETRY "v1/devices/me/telemetry"
#define ATTRIBUTES "v1/devices/me/attributes"
static const char *TAG="TB_PROD";
extern const uint8_t isrgrootx1_pem_start[] asm("_binary_isrgrootx1_pem_start");
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static char boot_id[33];static uint64_t sequence;
static bool connected,callback_overflow;
static int64_t sync_us,sync_epoch_ms;
static QueueHandle_t snapshots,callbacks;
static TaskHandle_t task;
static delivery_t delivery;
static char json[CLOUD_JSON_MAX];
static kitchen_sensor_data_t previous;
static bool have_previous;
typedef struct {uint32_t epoch;int type,mid;} notice_t;
static uint32_t callback_epoch;
static bool transport_recycle;
static uint64_t max_step_us,max_interval_us;
static unsigned safety_stack;
void tb_safety_timing(int64_t step_us,int64_t interval_us)
{
 unsigned stack=uxTaskGetStackHighWaterMark(NULL);
 portENTER_CRITICAL(&lock);
 if(step_us>0&&(uint64_t)step_us>max_step_us)max_step_us=step_us;
 if(interval_us>0&&(uint64_t)interval_us>max_interval_us)max_interval_us=interval_us;
 safety_stack=stack;portEXIT_CRITICAL(&lock);
}
static int64_t wall_ms(void){struct timeval t;gettimeofday(&t,NULL);return (int64_t)t.tv_sec*1000+t.tv_usec/1000;}
static void time_received(struct timeval *tv)
{
 int64_t epoch=(int64_t)tv->tv_sec*1000+tv->tv_usec/1000;
 bool sane=clock_sane(epoch,PCCC_BUILD_EPOCH);
 portENTER_CRITICAL(&lock);sync_us=sane?esp_timer_get_time():0;sync_epoch_ms=epoch;portEXIT_CRITICAL(&lock);
 ESP_LOGI(TAG,"SNTP received sane=%d",sane);
 if(task)xTaskNotifyGive(task);
}
bool tb_time_is_synced(void)
{
 int64_t now=esp_timer_get_time(),wall=wall_ms();
 portENTER_CRITICAL(&lock);bool yes=clock_trusted(sync_us,sync_epoch_ms,now,wall);portEXIT_CRITICAL(&lock);return yes;
}
int64_t tb_get_current_timestamp(void){return tb_time_is_synced()?wall_ms()/1000:0;}
bool tb_mqtt_is_connected(void){portENTER_CRITICAL(&lock);bool yes=connected;portEXIT_CRITICAL(&lock);return yes;}
static cloud_record_t capture(const kitchen_sensor_data_t *d)
{
 cloud_record_t r={0};r.captured_us=d->risk_state.last_eval_time_us;
 if(r.captured_us<=0)r.captured_us=esp_timer_get_time();
 r.time_synced=tb_time_is_synced();if(r.time_synced)r.epoch_ms=wall_ms()-(esp_timer_get_time()-r.captured_us)/1000;
 portENTER_CRITICAL(&lock);r.seq=++sequence;memcpy(r.boot_id,boot_id,33);portEXIT_CRITICAL(&lock);
 r.data=*d;r.buzzer_mode=buzzer_get_mode();return r;
}
static bool changed(const kitchen_sensor_data_t *a,const kitchen_sensor_data_t *b)
{
 return a->risk_state.risk_level!=b->risk_state.risk_level||a->risk_state.risk_reason!=b->risk_state.risk_reason||
 a->risk_state.monitoring_status!=b->risk_state.monitoring_status||a->risk_state.emergency_latched!=b->risk_state.emergency_latched||
 a->risk_state.physical_ack_seq!=b->risk_state.physical_ack_seq||a->risk_state.ack_eligible!=b->risk_state.ack_eligible||
 a->risk_state.persistence_health!=b->risk_state.persistence_health||a->is_muted!=b->is_muted||
 a->risk_state.detection_state!=b->risk_state.detection_state||a->gy906_quality!=b->gy906_quality||
 a->mq2_quality!=b->mq2_quality||a->flame_quality!=b->flame_quality||a->radar_quality!=b->radar_quality;
}
esp_err_t tb_send_telemetry_async(const kitchen_sensor_data_t *d)
{
 if(!snapshots||!d)return ESP_ERR_INVALID_STATE;
 cloud_record_t r=capture(d);
 if(!have_previous||changed(d,&previous)){
  kitchen_event_t e={.record=r,.priority=d->risk_state.emergency_latched||d->risk_state.risk_level==RISK_EMERGENCY?EVENT_PRIO_EMERGENCY:d->risk_state.risk_level==RISK_WARNING?EVENT_PRIO_WARNING:d->risk_state.monitoring_status==MONITORING_FAULT?EVENT_PRIO_FAULT:EVENT_PRIO_INFO};
  /* Do not advance previous on rejection: retry state handoff next sensor cycle. */
  if(event_buffer_push(&e)==ESP_OK){previous=*d;have_previous=true;}
 }
 xQueueOverwrite(snapshots,&r);if(task)xTaskNotifyGive(task);return ESP_OK;
}
esp_err_t tb_send_telemetry(const kitchen_sensor_data_t *d){return tb_send_telemetry_async(d);}
static void mqtt_callback(void *arg,esp_event_base_t base,int32_t id,void *data)
{
 (void)arg;(void)base;esp_mqtt_event_handle_t e=data;
 if(id!=MQTT_EVENT_CONNECTED&&id!=MQTT_EVENT_DISCONNECTED&&id!=MQTT_EVENT_PUBLISHED&&id!=MQTT_EVENT_DELETED&&id!=MQTT_EVENT_ERROR)return;
 notice_t n={.epoch=callback_epoch,.type=id,.mid=e?e->msg_id:0};
 if(xQueueSend(callbacks,&n,0)!=pdTRUE){portENTER_CRITICAL(&lock);callback_overflow=true;portEXIT_CRITICAL(&lock);}
 if(task)xTaskNotifyGive(task);
}
static bool send(esp_mqtt_client_handle_t client,const char *topic,delivery_token_t token,int64_t now)
{
 if(delivery.count>=DELIVERY_MAX || transport_recycle)return false;
 int mid=esp_mqtt_client_enqueue(client,topic,json,0,1,0,true);
 if(mid<0)return false;
 if(!delivery_accept(&delivery,mid,token,now)){transport_recycle=true;return false;}
 if(token.event)event_buffer_accepted();
 return true;
}
static void stop_client(esp_mqtt_client_handle_t *client)
{
 if(*client){esp_mqtt_client_stop(*client);esp_mqtt_client_destroy(*client);*client=NULL;}
 event_buffer_reconnect();
 portENTER_CRITICAL(&lock);connected=false;portEXIT_CRITICAL(&lock);
}
static void network(void *arg)
{
 (void)arg;esp_mqtt_client_handle_t client=NULL;cloud_record_t latest={0};bool have_latest=false,resync=true,attrs=true,boot=true,recycle=false,reconnected=false;
 uint32_t epoch=0,backoff=1;int64_t next_connect=0,last_clock_try=0,last_wait_log=0;bool clock_started=false;publish_policy_t policy={0};
 esp_err_t je=critical_journal_init();if(je!=ESP_OK)ESP_LOGE(TAG,"Critical journal unavailable: %s",esp_err_to_name(je));
 for(;;){
  int64_t now=esp_timer_get_time();wifi_app_poll(now);
  /* Do not race initial DNS/IP acquisition; bound retries while clock untrusted. */
  if(PCCC_PROVISIONED && wifi_is_connected() && (!clock_started || (!tb_time_is_synced() && now-last_clock_try>=60000000))){
   if(clock_started)esp_sntp_stop();
   else {esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);esp_sntp_setservername(0,"pool.ntp.org");esp_sntp_setservername(1,"time.google.com");esp_sntp_set_sync_interval(3600000);esp_sntp_set_time_sync_notification_cb(time_received);}
   esp_sntp_init();clock_started=true;last_clock_try=now;
  }
  if(!tb_mqtt_is_connected() && now-last_wait_log>=60000000){
   ESP_LOGI(TAG,"Waiting wifi=%d trusted_clock=%d heap=%u largest=%u",wifi_is_connected(),tb_time_is_synced(),(unsigned)esp_get_free_heap_size(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));last_wait_log=now;
  }
  cloud_record_t incoming;
  if(xQueueReceive(snapshots,&incoming,0)==pdTRUE){
   if(!have_latest||changed(&incoming.data,&latest.data))resync=true;
   latest=incoming;have_latest=true;
  }
  /* Journal service runs even with broker offline; no writes from safety task. */
  kitchen_event_t persist;
  if(event_buffer_journal_candidate(&persist)){
   persist.record.replayed=true;
   if(cloud_encode(&persist.record,"TRANSITION",true,persist.record.captured_us,json,sizeof(json)))critical_journal_add(&persist.record,json);
  }
  notice_t n;while(xQueueReceive(callbacks,&n,0)==pdTRUE){
   if(n.epoch!=epoch)continue;
   if(n.type==MQTT_EVENT_CONNECTED){portENTER_CRITICAL(&lock);connected=true;portEXIT_CRITICAL(&lock);backoff=1;resync=attrs=true;reconnected=true;ESP_LOGI(TAG,"MQTT_CONNECTED epoch=%lu",(unsigned long)epoch);}
   else if(n.type==MQTT_EVENT_PUBLISHED){delivery_token_t token;if(delivery_ack(&delivery,n.epoch,n.mid,&token)&&token.event){event_buffer_complete(token.boot,token.seq);critical_journal_complete(token.boot,token.seq);event_buffer_journal_rescan();ESP_LOGI(TAG,"Event PUBACK completed seq=%llu",(unsigned long long)token.seq);}}
   else if(n.type==MQTT_EVENT_DELETED){for(int i=0;i<DELIVERY_MAX;i++)if(delivery.slots[i].mid==n.mid&&delivery.slots[i].token.event)event_buffer_retry(delivery.slots[i].token.boot,delivery.slots[i].token.seq,true);recycle=true;}
   else if(n.type==MQTT_EVENT_DISCONNECTED||n.type==MQTT_EVENT_ERROR)recycle=true;
  }
  portENTER_CRITICAL(&lock);if(callback_overflow){recycle=true;callback_overflow=false;}portEXIT_CRITICAL(&lock);
  if(client&&(!tb_time_is_synced()||delivery_timed_out(&delivery,now)))recycle=true;
  if(transport_recycle){recycle=true;transport_recycle=false;}
  if(recycle){
   for(int i=0;i<DELIVERY_MAX;i++)if(delivery.slots[i].mid&&delivery.slots[i].token.event)event_buffer_retry(delivery.slots[i].token.boot,delivery.slots[i].token.seq,true);
   stop_client(&client);delivery_reset(&delivery,epoch);recycle=false;
   next_connect=now+(backoff*1000+esp_random()%1000)*1000LL;backoff=backoff<32?backoff*2:60;
  }
  if(!client&&PCCC_PROVISIONED&&wifi_is_connected()&&tb_time_is_synced()&&now>=next_connect){
   callback_epoch=++epoch;delivery_reset(&delivery,epoch);
   esp_mqtt_client_config_t cfg={.broker={.address.uri=TB_BROKER_URI,.verification.certificate=(const char*)isrgrootx1_pem_start},
    .credentials.username=PCCC_TB_ACCESS_TOKEN,.network={.disable_auto_reconnect=true,.timeout_ms=5000},
    .outbox.limit=16384,.session={.keepalive=30,.disable_clean_session=false},.task={.priority=3,.stack_size=6144}};
   client=esp_mqtt_client_init(&cfg);
   if(!client || esp_mqtt_client_register_event(client,ESP_EVENT_ANY_ID,mqtt_callback,NULL)!=ESP_OK || esp_mqtt_client_start(client)!=ESP_OK){recycle=true;}
   else next_connect=now+15000000;
  }
  /* Initial connect may hang without DISCONNECTED; bound each attempt. */
  if(client&&!tb_mqtt_is_connected()&&now>next_connect)recycle=true;
  if(client&&tb_mqtt_is_connected()&&have_latest&&delivery.count<DELIVERY_MAX){
   delivery_token_t control={0};bool sent=false;
   if(attrs){if(cloud_attributes(json,sizeof(json),esp_reset_reason()==ESP_RST_POWERON?"POWERON":esp_reset_reason()==ESP_RST_SW?"SOFTWARE":esp_reset_reason()==ESP_RST_BROWNOUT?"BROWNOUT":"OTHER")&&send(client,ATTRIBUTES,control,now)){attrs=false;sent=true;}}
   else if(resync){if(cloud_encode(&latest,boot?"BOOT":reconnected?"RECONNECT":"SNAPSHOT",false,now,json,sizeof(json))&&send(client,TELEMETRY,control,now)){resync=false;boot=false;reconnected=false;sent=true;policy.measured=true;policy.measurements=now;}}
   else {
    for(int i=0;i<JOURNAL_SLOTS&&!sent;i++){
     journal_entry_t e=critical_journal_entry(i);
     if(e.used&&!delivery_contains(&delivery,e.boot,e.seq)&&critical_journal_read(i,json,sizeof(json))==ESP_OK){
      delivery_token_t token={.seq=e.seq,.event=true};memcpy(token.boot,e.boot,33);sent=send(client,TELEMETRY,token,now);
     }
    }
    if(!sent){kitchen_event_t e;if(event_buffer_claim(&e)==ESP_OK){
     if(delivery_contains(&delivery,e.record.boot_id,e.record.seq)){event_buffer_retry(e.record.boot_id,e.record.seq,false);}
     else {delivery_token_t token={.seq=e.record.seq,.event=true};memcpy(token.boot,e.record.boot_id,33);
      if(cloud_encode(&e.record,"TRANSITION",true,e.record.captured_us,json,sizeof(json)))sent=send(client,TELEMETRY,token,now);
      if(!sent)event_buffer_retry(e.record.boot_id,e.record.seq,false);
     }
    }}
    bool abnormal=latest.data.risk_state.risk_level!=RISK_NORMAL||latest.data.risk_state.monitoring_status!=MONITORING_OK;
    if(!sent&&publish_measure_due(&policy,now,abnormal)){
     if(cloud_encode(&latest,"SNAPSHOT",false,now,json,sizeof(json))&&send(client,TELEMETRY,control,now)){sent=true;policy.measured=true;policy.measurements=now;}
    }
    if(!sent&&(publish_health_due(&policy,now)||publish_heartbeat_due(&policy,now))){
     cloud_record_t health=latest;health.captured_us=now;health.time_synced=tb_time_is_synced();health.epoch_ms=health.time_synced?wall_ms():0;
     portENTER_CRITICAL(&lock);health.seq=++sequence;portEXIT_CRITICAL(&lock);
     size_t free=esp_get_free_heap_size(),min=esp_get_minimum_free_heap_size(),largest=heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
     unsigned stack=uxTaskGetStackHighWaterMark(NULL);
     portENTER_CRITICAL(&lock);uint64_t step=max_step_us,interval=max_interval_us;unsigned main_stack=safety_stack;portEXIT_CRITICAL(&lock);
     if(cloud_health(&health,free,min,largest,event_buffer_stats(),critical_journal_losses(),stack,step,interval,main_stack,json,sizeof(json))&&send(client,TELEMETRY,control,now)){
      sent=true;policy.healthy=true;policy.health=now;
      ESP_LOGI(TAG,"health heap=%u min=%u largest=%u network_stack=%u safety_stack=%u step_max_us=%llu interval_max_us=%llu outbox=%d inflight=%u",(unsigned)free,(unsigned)min,(unsigned)largest,stack,main_stack,(unsigned long long)step,(unsigned long long)interval,esp_mqtt_client_get_outbox_size(client),delivery.count);
     }
    }
   }
   if(sent)policy.traffic=now;
   /* An accepted reused message ID must never be correlated to a different event. */
   if(esp_mqtt_client_get_outbox_size(client)>16384)recycle=true;
  }
  ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(100));
 }
}
esp_err_t tb_mqtt_init(void)
{
 if(task)return ESP_ERR_INVALID_STATE;
 event_buffer_init();
 uint8_t id[16];esp_fill_random(id,sizeof(id));for(int i=0;i<16;i++)snprintf(boot_id+i*2,3,"%02x",id[i]);
 snapshots=xQueueCreate(1,sizeof(cloud_record_t));callbacks=xQueueCreate(32,sizeof(notice_t));
 if(!snapshots||!callbacks){if(snapshots)vQueueDelete(snapshots);if(callbacks)vQueueDelete(callbacks);snapshots=callbacks=NULL;return ESP_ERR_NO_MEM;}
 if(xTaskCreate(network,"tb_production",8192,NULL,2,&task)!=pdPASS){vQueueDelete(snapshots);vQueueDelete(callbacks);snapshots=callbacks=NULL;return ESP_ERR_NO_MEM;}
 ESP_LOGI(TAG,"schema=1 provisioning=%s outbox=16384 inflight=4",PCCC_PROVISIONED?"ready":"absent (network disabled)");return ESP_OK;
}
