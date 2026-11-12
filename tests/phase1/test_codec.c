#include "cloud_codec.h"
#include "risk_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
int64_t esp_timer_get_time(void){return 10000000;}
int main(int argc,char **argv){
 cloud_record_t r={0};strcpy(r.boot_id,"0123456789abcdef0123456789abcdef");r.seq=10;r.captured_us=10000000;r.epoch_ms=1794358800000;r.time_synced=true;
 kitchen_sensor_data_t *d=&r.data;d->gy906_valid=true;d->gy906_quality=d->flame_quality=d->mq2_quality=d->radar_quality=QUALITY_VALID;
 d->gy906_last_update=d->flame_last_update=d->mq2_last_update=d->radar_last_update=r.captured_us;
 d->temp_object_filtered=25.4;d->temp_ambient_filtered=30;d->radar_presence=true;d->radar_uart_valid=true;
 d->risk_state.persistence_health=1;d->risk_state.risk_level=RISK_NORMAL;d->risk_state.risk_reason=REASON_NORMAL_SAFE;d->risk_state.detection_state=DETECTION_NO_EMERGENCY;
 int scenario=argc>1?atoi(argv[1]):0;bool history=scenario>=1&&scenario<=5;
 switch(scenario){
 case 1:d->risk_state.risk_level=RISK_WARNING;d->risk_state.risk_reason=REASON_GAS_SMOKE_ELEVATED;d->mq2_gas_detected=true;r.buzzer_mode=BUZZER_MODE_INTERMITTENT;break;
 case 2:d->risk_state.risk_level=RISK_EMERGENCY;d->risk_state.risk_reason=REASON_FIRE_DETECTED;d->risk_state.emergency_latched=true;d->flame_state=FLAME_STATE_FIRE;d->risk_state.detection_state=DETECTION_EMERGENCY_PRESENT;r.buzzer_mode=BUZZER_MODE_CONTINUOUS;break;
 case 3:d->risk_state.risk_level=RISK_EMERGENCY;d->risk_state.risk_reason=REASON_EMERGENCY_LATCHED;d->risk_state.emergency_latched=true;d->risk_state.ack_eligible=true;r.buzzer_mode=BUZZER_MODE_CONTINUOUS;break;
 case 4:d->risk_state.risk_level=RISK_UNKNOWN;d->risk_state.risk_reason=REASON_SENSOR_FAULT;d->risk_state.monitoring_status=MONITORING_FAULT;d->gy906_quality=QUALITY_INVALID;d->gy906_valid=false;d->risk_state.detection_state=DETECTION_UNKNOWN;r.buzzer_mode=BUZZER_MODE_FAULT_BEEP;break;
 case 5:d->risk_state.physical_ack_seq=1;break;
 case 6:r.time_synced=false;r.epoch_ms=0;break;
 }
 char out[CLOUD_JSON_MAX];
 if(scenario==7){r.time_synced=false;r.epoch_ms=0;d->risk_state.risk_level=RISK_UNKNOWN;d->risk_state.risk_reason=REASON_INITIALIZING;d->gy906_quality=d->flame_quality=d->mq2_quality=d->radar_quality=QUALITY_INITIALIZING;}
 if(scenario==8){assert(cloud_health(&r,100000,85000,80000,(event_stats_t){0},0,4000,100000,1100000,2000,out,sizeof(out)));puts(out);return 0;}
 if(scenario==9){assert(cloud_attributes(out,sizeof(out),"POWERON"));puts(out);return 0;}
 assert(cloud_encode(&r,history?"TRANSITION":scenario==6?"RECONNECT":scenario==7?"BOOT":"SNAPSHOT",history,r.captured_us,out,sizeof(out)));
 if(argc>1){puts(out);return 0;}
 char tiny[8];assert(!cloud_encode(&r,"SNAPSHOT",false,r.captured_us,tiny,sizeof(tiny)));
 d->temp_object_filtered=NAN;assert(cloud_encode(&r,"SNAPSHOT",false,r.captured_us,out,sizeof(out)));assert(!strstr(out,"temp_object_c"));assert(!strstr(out,"NaN"));
 d->temp_object_filtered=25.4;assert(cloud_encode(&r,"SNAPSHOT",false,r.captured_us+4000000,out,sizeof(out)));assert(!strstr(out,"temp_object_c"));assert(!strstr(out,"gas_detected"));
 d->mq2_ao_valid=true;d->mq2_ao_adc_raw=123;d->mq2_ao_mv_valid=false;assert(cloud_encode(&r,"SNAPSHOT",false,r.captured_us,out,sizeof(out)));assert(strstr(out,"mq2_adc_counts")&&!strstr(out,"\"mq2_adc_mv\":"));
 assert(!strstr(out,"MQ2_raw")&&!strstr(out,"ppm"));assert(cloud_attributes(out,sizeof(out),"POWERON"));assert(strstr(out,"build_id")&&!strstr(out,"token"));
 for(int q=QUALITY_INITIALIZING;q<=QUALITY_STALE;q++){
 d->gy906_quality=d->mq2_quality=d->flame_quality=d->radar_quality=q;
 assert(cloud_encode(&r,"SNAPSHOT",false,r.captured_us,out,sizeof(out)));
 if(q!=QUALITY_VALID){assert(!strstr(out,"temp_object_c"));assert(!strstr(out,"gas_detected"));assert(!strstr(out,"flame_detected"));assert(!strstr(out,"\"presence\":"));}
 }
 d->mq2_quality=QUALITY_VALID;d->mq2_warming_up=true;assert(cloud_encode(&r,"SNAPSHOT",false,r.captured_us,out,sizeof(out)));assert(!strstr(out,"gas_detected"));
 for(int reason=REASON_NONE;reason<=REASON_NORMAL_SAFE;reason++){
 d->risk_state.risk_reason=reason;assert(cloud_encode(&r,"TRANSITION",true,r.captured_us,out,sizeof(out)));assert(strstr(out,risk_reason_to_str(reason)));
 }
 puts("WP03 allocation-free bounded serializer, finite/fresh measurements, independent AO validity, attributes: PASS");
}
