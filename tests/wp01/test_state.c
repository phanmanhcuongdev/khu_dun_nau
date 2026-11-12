#include "risk_engine.h"
#include "local_alarm_controller.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int64_t now=1000000;
static uint32_t physical_hold;
static kitchen_sensor_data_t d;
static local_alarm_status_t alarm;
static buzzer_mode_t mode;
int64_t esp_timer_get_time(void){return now;}
uint32_t touch_btn_hold_ms(void){return physical_hold;}
void buzzer_set_mode(buzzer_mode_t m){mode=m;}
void buzzer_update_pattern(int64_t t){(void)t;}
bool buzzer_get_state(void){return mode!=BUZZER_MODE_OFF;}
void oled_display_update(const kitchen_sensor_data_t *s,bool w,bool m){(void)s;(void)w;(void)m;}
void fake_store_reset(void);void fake_store_auto(bool);void fake_store_complete(void);void fake_store_fail(void);void fake_store_fault(void);
static void step(void){now+=1000000;risk_engine_step(&d,&d.risk_state);local_alarm_controller_step(&d,false,false,&alarm);risk_engine_persistence_step();}
static void init(void){fake_store_reset();memset(&d,0,sizeof(d));d.gy906_quality=d.flame_quality=d.mq2_quality=d.radar_quality=d.touch_quality=QUALITY_VALID;d.temp_object_filtered=25;d.mq2_raw_filtered=200;physical_hold=0;risk_engine_init();local_alarm_controller_init();}
static void fire(void){d.flame_state=FLAME_STATE_FIRE;step();assert(d.risk_state.emergency_latched && mode==BUZZER_MODE_CONTINUOUS && d.risk_state.detection_state==DETECTION_EMERGENCY_PRESENT);}
static void safe(void){d.flame_state=FLAME_STATE_SAFE;step();assert(d.risk_state.emergency_latched && d.risk_state.ack_eligible);}
static void ack(void){physical_hold=0;step();physical_hold=3000;step();step();step();physical_hold=0;step();}
int main(void){
 init();fire();safe();
 for(int i=0;i<30;i++){d.touch_state=1;d.touch_edge=1;physical_hold=0;step();assert(d.risk_state.emergency_latched && d.risk_state.physical_ack_seq==0);}
 // Reboot after fire+safe restores persistent latch, never physical ACK.
 risk_engine_init();step();assert(d.risk_state.emergency_latched && d.risk_state.physical_ack_seq==0);
 ack();assert(!d.risk_state.emergency_latched && d.risk_state.physical_ack_seq==1);
 risk_engine_init();step();assert(!d.risk_state.emergency_latched && d.risk_state.physical_ack_seq==1);
 for(int cycle=2;cycle<=5;cycle++){
  fire();physical_hold=3000;step();assert(!alarm.ack_triggered);physical_hold=0;step();
  d.gy906_quality=QUALITY_INVALID;d.flame_state=FLAME_STATE_SAFE;physical_hold=3000;step();assert(!alarm.ack_triggered && !d.risk_state.ack_eligible);physical_hold=0;step();
  d.gy906_quality=QUALITY_VALID;safe();ack();assert(!d.risk_state.emergency_latched && d.risk_state.physical_ack_seq==(uint32_t)cycle);
 }
 // Real concurrent fire takes primary reason over high temperature.
 d.temp_object_filtered=150;fire();assert(d.risk_state.risk_reason==REASON_FIRE_DETECTED);d.temp_object_filtered=25;safe();
 fake_store_auto(false);physical_hold=3000;step();step();assert(d.risk_state.emergency_latched);fake_store_fail();step();assert(d.risk_state.emergency_latched && d.risk_state.physical_ack_seq==5);
 fake_store_complete();step();assert(!d.risk_state.emergency_latched && d.risk_state.physical_ack_seq==6);
 // Pending CLEAR must be cancelled by new unsafe evaluation before durable success.
 fake_store_auto(true);physical_hold=0;fire();safe();fake_store_auto(false);physical_hold=3000;step();step();fire();fake_store_complete();physical_hold=0;step();assert(d.risk_state.emergency_latched && d.risk_state.physical_ack_seq==6);
 fake_store_fault();risk_engine_init();step();assert(d.risk_state.emergency_latched && !d.risk_state.ack_eligible && mode==BUZZER_MODE_CONTINUOUS);
 puts("WP01 engine/controller tap vs hold, unsafe/invalid reject, reboot restore, accepted ACK seq, repeated cycles, delayed/failing commit, new-fire cancellation, persistence-unavailable: PASS");
}
