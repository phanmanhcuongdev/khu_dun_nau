#include "cloud_codec.h"
#include "risk_engine.h"
#include "sensor_pipeline.h"
#include "pccc_build_config.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <inttypes.h>
typedef struct {char *p;size_t cap,len;bool ok,first;const char *prefix;} writer_t;
static void put(writer_t *w,const char *fmt,...)
{
 if(!w->ok)return;
 va_list a;va_start(a,fmt);int n=vsnprintf(w->p+w->len,w->cap-w->len,fmt,a);va_end(a);
 if(n<0 || (size_t)n>=w->cap-w->len){w->ok=false;return;}w->len+=(size_t)n;
}
static void quote(writer_t *w,const char *s)
{
 put(w,"\"");for(const unsigned char *p=(const unsigned char *)s;*p;p++){
  if(*p=='"' || *p=='\\')put(w,"\\%c",*p);else if(*p<32)put(w,"\\u%04x",*p);else put(w,"%c",*p);
 }put(w,"\"");
}
static void key(writer_t *w,const char *k){if(!w->first)put(w,",");w->first=false;put(w,"\"%s%s\":",w->prefix,k);}
static void str(writer_t *w,const char *k,const char *v){key(w,k);quote(w,v);}
static void num(writer_t *w,const char *k,uint64_t v){key(w,k);put(w,"%" PRIu64,v);}
static void boolean(writer_t *w,const char *k,bool v){key(w,k);put(w,v?"true":"false");}
static void real(writer_t *w,const char *k,float v){if(isfinite(v)){key(w,k);put(w,"%.3f",(double)v);}}
static const char *quality(data_quality_t q){switch(q){case QUALITY_VALID:return "VALID";case QUALITY_INVALID:return "INVALID";case QUALITY_STALE:return "STALE";default:return "INITIALIZING";}}
static data_quality_t fresh(data_quality_t q,int64_t stamp,int64_t now,int timeout)
{return q==QUALITY_VALID && (stamp<=0 || now<stamp || now-stamp>(int64_t)timeout*1000)?QUALITY_STALE:q;}
static writer_t begin(const cloud_record_t *r,const char *kind,bool history,char *out,size_t cap)
{
 writer_t w={.p=out,.cap=cap,.ok=cap>0,.first=true,.prefix=""};
 if(r->time_synced && r->epoch_ms>0)put(&w,"{\"ts\":%" PRId64 ",\"values\":",r->epoch_ms);
 put(&w,"{");num(&w,"schema_version",1);str(&w,"record_kind",kind);
 w.prefix=history?"event_":"";
 str(&w,"boot_id",r->boot_id);num(&w,"record_seq",r->seq);boolean(&w,"replayed",r->replayed);
 boolean(&w,"time_synced",r->time_synced);num(&w,"captured_uptime_ms",r->captured_us/1000);
 return w;
}
static bool end(writer_t *w,const cloud_record_t *r){put(w,"}");if(r->time_synced && r->epoch_ms>0)put(w,"}");return w->ok;}
bool cloud_encode(const cloud_record_t *r,const char *kind,bool history,int64_t now,char *out,size_t cap)
{
 writer_t w=begin(r,kind,history,out,cap);const kitchen_sensor_data_t *d=&r->data;
 const risk_state_t *s=&d->risk_state;
 str(&w,"risk_level",risk_level_to_str(s->risk_level));str(&w,"risk_reason",risk_reason_to_str(s->risk_reason));
 str(&w,"monitoring_status",monitoring_status_to_str(s->monitoring_status));
 str(&w,"overall_quality",d->v1_quality==V1_QUALITY_OK?"OK":d->v1_quality==V1_QUALITY_DEGRADED?"DEGRADED":"CRITICAL");
 boolean(&w,"emergency_latched",s->emergency_latched);boolean(&w,"ack_eligible",s->ack_eligible);
 num(&w,"physical_ack_seq",s->physical_ack_seq);num(&w,"persistence_health",s->persistence_health);
 str(&w,"detection_state",s->detection_state==DETECTION_EMERGENCY_PRESENT?"EMERGENCY_PRESENT":s->detection_state==DETECTION_NO_EMERGENCY?"NO_EMERGENCY":"UNKNOWN");
 static const char *modes[]={"OFF","CONTINUOUS","INTERMITTENT","FAULT_BEEP"};
 str(&w,"buzzer_mode",r->buzzer_mode>=BUZZER_MODE_OFF && r->buzzer_mode<=BUZZER_MODE_FAULT_BEEP?modes[r->buzzer_mode]:"OFF");
 boolean(&w,"buzzer_output_on",d->buzzer_state!=0);boolean(&w,"muted",d->is_muted);
 num(&w,"mute_remaining_s",d->mute_remaining_sec);num(&w,"recovery_remaining_s",s->recovery_countdown_sec);
 data_quality_t tq=fresh(d->gy906_quality,d->gy906_last_update,now,GY906_STALE_TIMEOUT_MS);
 data_quality_t gq=fresh(d->mq2_quality,d->mq2_last_update,now,MQ2_STALE_TIMEOUT_MS);
 data_quality_t fq=fresh(d->flame_quality,d->flame_last_update,now,FLAME_STALE_TIMEOUT_MS);
 data_quality_t rq=fresh(d->radar_quality,d->radar_last_update,now,RADAR_STALE_TIMEOUT_MS);
 if(!isfinite(d->temp_object_filtered)||!isfinite(d->temp_ambient_filtered))tq=QUALITY_INVALID;
 str(&w,"gy906_quality",quality(tq));str(&w,"mq2_quality",quality(gq));str(&w,"flame_quality",quality(fq));str(&w,"radar_quality",quality(rq));
 if(tq==QUALITY_VALID && d->gy906_valid){real(&w,"temp_object_c",d->temp_object_filtered);real(&w,"temp_ambient_c",d->temp_ambient_filtered);if(d->dT_dt_valid)real(&w,"temp_rate_cps",d->dT_dt);}
 if(gq==QUALITY_VALID && !d->mq2_warming_up)boolean(&w,"gas_detected",d->mq2_gas_detected);
 if(fq==QUALITY_VALID)boolean(&w,"flame_detected",d->flame_state==FLAME_STATE_FIRE);
 if(rq==QUALITY_VALID)boolean(&w,"presence",d->radar_presence!=0);
 str(&w,"radar_source",d->radar_uart_valid?"UART":"OUT_FALLBACK");
 /* AO sample has independent read/calibration validity. Never label pseudo-raw as ADC. */
 bool ao_fresh=now>=d->mq2_last_update && now-d->mq2_last_update<=MQ2_STALE_TIMEOUT_MS*1000LL;
 boolean(&w,"mq2_ao_valid",d->mq2_ao_valid && ao_fresh);
 boolean(&w,"mq2_ao_mv_valid",d->mq2_ao_mv_valid && d->mq2_ao_valid && ao_fresh);
 if(d->mq2_ao_valid && ao_fresh && d->mq2_ao_adc_raw>=0 && d->mq2_ao_adc_raw<=4095){num(&w,"mq2_adc_counts",d->mq2_ao_adc_raw);if(d->mq2_ao_mv_valid && d->mq2_ao_adc_mv>=0)num(&w,"mq2_adc_mv",d->mq2_ao_adc_mv);}
 return end(&w,r);
}
bool cloud_attributes(char *out,size_t cap,const char *reset_reason)
{
 writer_t w={.p=out,.cap=cap,.ok=cap>0,.first=true,.prefix=""};put(&w,"{");
 str(&w,"reset_reason",reset_reason);num(&w,"schema_version",1);str(&w,"firmware_version",PCCC_FIRMWARE_VERSION);str(&w,"build_id",PCCC_BUILD_ID);
 str(&w,"config_id",PCCC_CONFIG_ID);str(&w,"hardware_model","ESP32-S3 N16R8");str(&w,"zone_id","V1");
 str(&w,"capabilities","mlx90614,mq2_do,mq2_ao,flame_do,flame_ao,ld2410,physical_ack,buzzer,rgb,oled,read_only_cloud");
 num(&w,"recommended_inactivity_ms",90000);put(&w,"}");return w.ok;
}
bool cloud_health(const cloud_record_t *r,size_t heap,size_t minimum,size_t largest,event_stats_t s,uint32_t journal_loss,unsigned stack,uint64_t step,uint64_t interval,unsigned safety_stack,char *out,size_t cap)
{
 writer_t w=begin(r,"HEARTBEAT",false,out,cap);
 num(&w,"free_heap_bytes",heap);num(&w,"min_free_heap_bytes",minimum);num(&w,"largest_free_block_bytes",largest);
 num(&w,"event_drop_count",s.rejected+s.evicted+journal_loss);num(&w,"event_expiry_count",s.expired);
 num(&w,"event_reject_count",s.rejected);num(&w,"event_eviction_count",s.evicted);num(&w,"event_completed_count",s.completed);
 num(&w,"event_retry_count",s.retries);num(&w,"journal_loss_count",journal_loss);num(&w,"network_stack_free_bytes",stack);num(&w,"safety_stack_free_bytes",safety_stack);num(&w,"safety_step_max_us",step);num(&w,"safety_interval_max_us",interval);
 return end(&w,r);
}
