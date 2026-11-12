#include "risk_engine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int64_t clock_us=1000000;
int64_t esp_timer_get_time(void){return clock_us;}
int main(void){
 for(int fault=0;fault<3;fault++) {
  kitchen_sensor_data_t d={0};
  d.gy906_quality=d.flame_quality=d.mq2_quality=d.radar_quality=QUALITY_VALID;
  d.mq2_raw_filtered=200;d.temp_object_filtered=100;
  risk_engine_init();risk_engine_step(&d,&d.risk_state);assert(d.risk_state.risk_level==RISK_WARNING);
  clock_us+=1000000;d.temp_object_filtered=25;risk_engine_step(&d,&d.risk_state);
  clock_us+=2000000;
  if(fault==2)d.temp_object_filtered=85;else d.gy906_quality=fault?QUALITY_STALE:QUALITY_INVALID;
  risk_engine_step(&d,&d.risk_state);clock_us+=10000000;
  d.gy906_quality=QUALITY_VALID;d.temp_object_filtered=25;risk_engine_step(&d,&d.risk_state);
  assert(d.risk_state.risk_level==RISK_WARNING);
  clock_us+=4999999;risk_engine_step(&d,&d.risk_state);assert(d.risk_state.risk_level==RISK_WARNING);
  clock_us++;risk_engine_step(&d,&d.risk_state);assert(d.risk_state.risk_level==RISK_NORMAL);
 }
 puts("WP01 continuous recovery INVALID/STALE/unsafe and full 5s: PASS");
}
