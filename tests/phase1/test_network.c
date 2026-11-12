#define gettimeofday mock_gettimeofday
#include <sys/time.h>
#include <setjmp.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "../../main/src/tb_mqtt.c"
static jmp_buf finish;
static int64_t mono=1000000;
static int scenario,iterations,created_clients,destroyed_clients,published,max_inflight,control_before_history,allocation_fail,ack_delay=5;
static bool disconnected_once,current_emergency;
static int queue_allocations,live_queues;
static int64_t wall_offset;
struct mock_queue {unsigned capacity,size,count,head;unsigned char *data;};
struct fake_mqtt {int mid;void(*cb)(void*,esp_event_base_t,int32_t,void*);void *ctx;} fake;
static struct{int mid,due;} pending[32];static int pending_count;
const uint8_t _binary_isrgrootx1_pem_start[]="fixture-ca";
int mock_gettimeofday(struct timeval *tv,void *tz){(void)tz;int64_t ms=1794358800000LL+mono/1000+wall_offset;tv->tv_sec=ms/1000;tv->tv_usec=ms%1000*1000;return 0;}
int64_t esp_timer_get_time(void){return mono;}
uint32_t esp_random(void){return 123;}
void esp_fill_random(void *p,size_t n){memset(p,0xab,n);}
int esp_reset_reason(void){return ESP_RST_POWERON;}
size_t esp_get_free_heap_size(void){return 100000;}
size_t esp_get_minimum_free_heap_size(void){return 85000;}
size_t heap_caps_get_largest_free_block(int c){(void)c;return 80000;}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t h){(void)h;return 4000;}
void wifi_app_poll(int64_t t){(void)t;}
bool wifi_is_connected(void){return scenario!=11&&(scenario!=12||iterations>=100);}
buzzer_mode_t buzzer_get_mode(void){return BUZZER_MODE_OFF;}
QueueHandle_t xQueueCreate(unsigned n,size_t s){queue_allocations++;if(allocation_fail==1||(allocation_fail==2&&queue_allocations%2==0))return NULL;live_queues++;struct mock_queue *q=calloc(1,sizeof(*q));q->data=calloc(n,s);q->capacity=n;q->size=s;return q;}
int xQueueSend(QueueHandle_t q,const void *p,unsigned t){(void)t;if(q->count==q->capacity)return 0;memcpy(q->data+((q->head+q->count)%q->capacity)*q->size,p,q->size);q->count++;return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void *p,unsigned t){(void)t;if(!q||!q->count)return 0;memcpy(p,q->data+q->head*q->size,q->size);q->head=(q->head+1)%q->capacity;q->count--;return pdTRUE;}
int xQueueOverwrite(QueueHandle_t q,const void *p){q->count=0;q->head=0;return xQueueSend(q,p,0);}
void vQueueDelete(QueueHandle_t q){free(q->data);free(q);live_queues--;}
int xTaskCreate(void(*fn)(void*),const char *n,unsigned stack,void *p,int pri,TaskHandle_t *h){(void)fn;(void)n;(void)stack;(void)p;(void)pri;if(allocation_fail==3)return 0;*h=(void*)1;return pdPASS;}
void xTaskNotifyGive(TaskHandle_t h){(void)h;}
static void emit(int type,int mid){esp_mqtt_event_t e={mid};if(fake.cb)fake.cb(fake.ctx,NULL,type,&e);}
unsigned ulTaskNotifyTake(int clear,unsigned ticks){
 (void)clear;(void)ticks;mono+=100000;iterations++;
 if((int)delivery.count>max_inflight)max_inflight=delivery.count;
 assert(delivery.count<=4);
 if(scenario!=1){
  for(int i=0;i<pending_count;){if(iterations>=pending[i].due){emit(MQTT_EVENT_PUBLISHED,pending[i].mid);pending[i]=pending[--pending_count];}else i++;}
 }
 if(scenario==2 && published>=3 && !disconnected_once){emit(MQTT_EVENT_DISCONNECTED,0);disconnected_once=true;pending_count=0;}
 if(scenario==3 && iterations%25==0)emit(MQTT_EVENT_DISCONNECTED,0);
 if(scenario==4&&iterations==10)for(int i=0;i<64;i++)emit(MQTT_EVENT_PUBLISHED,65000);
 if(scenario==8&&iterations==30)wall_offset=10000;
 if(iterations>900)longjmp(finish,1);
 return 0;
}
void esp_sntp_setoperatingmode(int m){(void)m;assert(wifi_is_connected());}
void esp_sntp_setservername(int i,const char *s){(void)i;(void)s;}
void esp_sntp_set_sync_interval(unsigned ms){(void)ms;}
void esp_sntp_set_time_sync_notification_cb(void(*fn)(struct timeval*)){(void)fn;}
void esp_sntp_stop(void){}
void esp_sntp_init(void){struct timeval tv;mock_gettimeofday(&tv,NULL);if(scenario==7)tv.tv_sec=0;time_received(&tv);}
esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *c){assert(c->network.disable_auto_reconnect&&c->outbox.limit==16384&&c->broker.verification.certificate);created_clients++;if(scenario==5&&created_clients<=3)return NULL;fake.mid=0;return &fake;}
esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t c,int id,void(*cb)(void*,esp_event_base_t,int32_t,void*),void *ctx){(void)c;(void)id;if(scenario==6&&created_clients<=2)return ESP_FAIL;fake.cb=cb;fake.ctx=ctx;return ESP_OK;}
esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t c){(void)c;emit(MQTT_EVENT_CONNECTED,0);return ESP_OK;}
esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t c){(void)c;return ESP_OK;}
esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t c){(void)c;destroyed_clients++;pending_count=0;return ESP_OK;}
int esp_mqtt_client_enqueue(esp_mqtt_client_handle_t c,const char *topic,const char *payload,int len,int qos,int retain,bool store){
 (void)c;(void)len;(void)store;assert(qos==1&&!retain);published++;
 if(scenario==10&&published<=3)return -2;
 if(strstr(payload,"\"risk_level\":\"EMERGENCY\"")&&!strstr(payload,"\"record_kind\":\"TRANSITION\""))current_emergency=true;
 if(strstr(payload,"\"record_kind\":\"BOOT\"")||strstr(payload,"\"record_kind\":\"RECONNECT\""))control_before_history=1;
 if(strstr(payload,"TRANSITION"))assert(control_before_history);
 assert(!strcmp(topic,TELEMETRY)||!strcmp(topic,ATTRIBUTES));
 int mid=++fake.mid;if(pending_count<32)pending[pending_count++]=(typeof(pending[0])){mid,iterations+ack_delay};return mid;
}
int esp_mqtt_client_get_outbox_size(esp_mqtt_client_handle_t c){(void)c;return pending_count*1000;}
esp_err_t critical_journal_init(void){return ESP_OK;}
esp_err_t critical_journal_add(const cloud_record_t *r,const char *p){(void)r;(void)p;return ESP_OK;}
esp_err_t critical_journal_read(int s,char *p,size_t n){(void)s;(void)p;(void)n;return ESP_FAIL;}
journal_entry_t critical_journal_entry(int i){(void)i;return (journal_entry_t){0};}
void critical_journal_complete(const char *b,uint64_t seq){(void)b;(void)seq;}
uint32_t critical_journal_losses(void){return 0;}
int main(int argc,char **argv){
 scenario=argc>1?atoi(argv[1]):0;
 for(allocation_fail=1;allocation_fail<=3;allocation_fail++){queue_allocations=0;assert(tb_mqtt_init()==ESP_ERR_NO_MEM);assert(live_queues==0);}
 allocation_fail=0;
 assert(tb_mqtt_init()==ESP_OK);
 kitchen_sensor_data_t d={0};d.gy906_quality=d.flame_quality=d.mq2_quality=d.radar_quality=QUALITY_VALID;d.risk_state.risk_level=RISK_NORMAL;d.risk_state.risk_reason=REASON_NORMAL_SAFE;d.risk_state.last_eval_time_us=mono;
 assert(tb_send_telemetry_async(&d)==ESP_OK); // Event is not completed until broker ACK.
 assert(event_buffer_stats().completed==0);
 if(scenario==9){
  event_buffer_init();
  for(unsigned i=0;i<32;i++){kitchen_event_t e={.priority=EVENT_PRIO_EMERGENCY};strcpy(e.record.boot_id,"00000000000000000000000000000001");e.record.seq=5000+i;assert(event_buffer_push(&e)==ESP_OK);}
  d.risk_state.risk_level=RISK_EMERGENCY;d.risk_state.emergency_latched=true;d.risk_state.detection_state=DETECTION_EMERGENCY_PRESENT;
  assert(tb_send_telemetry_async(&d)==ESP_OK);assert(event_buffer_stats().rejected==1);
 }
 if(!setjmp(finish))network(NULL);
 assert(max_inflight<=4);
 if(scenario==7||scenario==11)assert(!published&&!created_clients);else assert(published>0);
 if(scenario==1)assert(event_buffer_stats().completed==0&&destroyed_clients>0);
 if(scenario==0||scenario==10)assert(event_buffer_stats().completed==1);
 if(scenario==9)assert(current_emergency&&event_buffer_stats().rejected==1);
 if(scenario==2)assert(disconnected_once&&destroyed_clients>0&&event_buffer_stats().completed==1);
 if(scenario==3)assert(destroyed_clients>5);
 if(scenario==4||scenario==8)assert(destroyed_clients>0);
 if(scenario==5)assert(created_clients>3);
 if(scenario==6)assert(created_clients>2);
 if(created_clients){stop_client((esp_mqtt_client_handle_t[]){&fake});}
 vQueueDelete(snapshots);vQueueDelete(callbacks);assert(live_queues==0);
 printf("WP04 actual network-owner source with injected transport: scenario=%d publishes=%d clients=%d max_inflight=%d PASS\n",scenario,published,created_clients,max_inflight);
}
