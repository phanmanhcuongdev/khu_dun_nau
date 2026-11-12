#include "oled_ui.h"
#include "risk_engine.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static const int64_t now=10000000;
static int64_t engine_now=1000000;
int64_t esp_timer_get_time(void) { return engine_now; }
static oled_ui_view_t v;
static kitchen_sensor_data_t d;
static void check(void) {
    unsigned char f[1026]; memset(f,0xa5,sizeof(f));
    assert(oled_ui_render(&v,true,f+1));
    assert(f[0]==0xa5 && f[1025]==0xa5);
    assert(strlen(v.title)<=16 && strlen(v.detail)<=16 && strlen(v.action)<=16 && strlen(v.footer)<=16);
}
static void compose(void) { oled_ui_compose(&d,true,true,now,&v); check(); }
static void preview(const char *name) {
    uint8_t f[1024]; assert(oled_ui_render(&v,true,f));
    char path[160]; snprintf(path,sizeof(path),"%s.pbm",name);
    FILE *out=fopen(path,"wb");assert(out);fprintf(out,"P1\n128 64\n");
    for(int y=0;y<64;y++){for(int x=0;x<128;x++)fprintf(out,"%d ",(f[y/8*128+x]>>(y%8))&1);fprintf(out,"\n");}fclose(out);
}
int main(void) {
    oled_ui_compose(NULL,false,false,now,&v);check();preview("boot");
    d.gy906_valid=true;d.gy906_quality=QUALITY_VALID;d.gy906_last_update=now;
    d.temp_object_filtered=25.4; d.temp_ambient_filtered=80;
    d.flame_quality=QUALITY_VALID;d.flame_state=FLAME_STATE_SAFE;
    d.risk_state.risk_level=RISK_NORMAL;d.risk_state.risk_reason=REASON_NORMAL_SAFE;
    compose();assert(!strcmp(v.title,"25.4C"));preview("normal");
    oled_ui_compose(&d,false,false,now,&v);check();assert(!strcmp(v.footer,"W:-- M:--"));
    oled_ui_compose(&d,true,false,now,&v);check();assert(!strcmp(v.footer,"W:OK M:--"));
    oled_ui_compose(&d,false,true,now,&v);check();assert(!strcmp(v.footer,"W:-- M:OK"));
    d.gy906_quality=QUALITY_STALE;compose();assert(!strcmp(v.title,"--.-C"));
    d.gy906_quality=QUALITY_VALID;d.gy906_last_update=now-3000001;compose();assert(!strcmp(v.title,"--.-C"));
    d.gy906_last_update=now;d.temp_object_filtered=NAN;compose();assert(!strcmp(v.title,"--.-C"));
    d.temp_object_filtered=25.4;
    d.risk_state.risk_level=RISK_WARNING;d.risk_state.risk_reason=REASON_GAS_SMOKE_ELEVATED;
    compose();assert(!strcmp(v.title,"CANH BAO"));preview("warning");
    d.risk_state.risk_level=RISK_NORMAL;compose();assert(!strcmp(v.title,"25.4C"));
    d.risk_state.risk_level=RISK_SUSPICIOUS;d.risk_state.risk_reason=REASON_UNATTENDED_COOKING;
    compose();preview("suspicious");
    d.risk_state.risk_level=RISK_EMERGENCY;d.risk_state.emergency_latched=true;
    d.risk_state.risk_reason=REASON_FIRE_DETECTED;compose();assert(!strcmp(v.title,"NGUY HIEM"));preview("emergency");
    d.risk_state.risk_reason=REASON_EMERGENCY_LATCHED;compose();assert(!strcmp(v.detail,"GIU NUT XAC NHAN"));preview("latched");
    d.temp_object_filtered=120;compose();assert(!strcmp(v.title,"NGUY HIEM"));
    d.temp_object_filtered=25.4;d.flame_quality=QUALITY_INVALID;compose();assert(!strcmp(v.title,"NGUY HIEM"));
    d.flame_quality=QUALITY_VALID;d.flame_state=FLAME_STATE_FIRE;compose();assert(!strcmp(v.title,"NGUY HIEM"));
    d.flame_state=FLAME_STATE_SAFE;d.risk_state.emergency_latched=false;d.risk_state.risk_level=RISK_NORMAL;
    d.risk_state.risk_reason=REASON_NORMAL_SAFE;compose();assert(!strcmp(v.title,"25.4C"));
    d.risk_state.risk_level=RISK_UNKNOWN;d.risk_state.monitoring_status=MONITORING_FAULT;
    d.gy906_quality=QUALITY_INVALID;compose();assert(!strcmp(v.title,"LOI CAM BIEN"));preview("fault");
    d.risk_state.monitoring_status=MONITORING_DEGRADED;d.gy906_quality=QUALITY_VALID;
    d.mq2_warming_up=true;d.mq2_warmup_remaining_sec=18;compose();preview("warmup");
    // Every reason, severity, network combination, extreme countdown fits. Input immutable.
    d.mq2_warmup_remaining_sec=UINT32_MAX;d.mute_remaining_sec=UINT32_MAX;
    d.risk_state.recovery_countdown_sec=UINT32_MAX;
    for(int risk=0;risk<=4;risk++)for(int reason=0;reason<=14;reason++)for(int m=0;m<3;m++)for(int net=0;net<4;net++) {
        d.risk_state.risk_level=risk;d.risk_state.risk_reason=reason;d.risk_state.monitoring_status=m;
        kitchen_sensor_data_t before=d;
        oled_ui_compose(&d,net&1,net&2,now,&v);check();
        assert(!memcmp(&before,&d,sizeof(d)));
        if(risk==RISK_EMERGENCY) assert(!strcmp(v.title,"NGUY HIEM"));
    }
    memset(&v,0,sizeof(v));strcpy(v.title,"123456789");v.temperature_large=true;
    uint8_t f[1024];assert(!oled_ui_render(&v,false,f));
    // Exercise existing engine, including real latch refusal and physical ACK API.
    memset(&d,0,sizeof(d)); risk_engine_init();
    d.gy906_valid=true;d.gy906_quality=QUALITY_VALID;d.gy906_last_update=now;
    d.flame_quality=d.mq2_quality=d.radar_quality=QUALITY_VALID;
    d.temp_object_filtered=25;d.flame_state=FLAME_STATE_SAFE;
    risk_engine_step(&d,&d.risk_state);risk_engine_persistence_step();compose();assert(d.risk_state.risk_level==RISK_NORMAL);
    d.flame_state=FLAME_STATE_FIRE;d.temp_object_filtered=150;engine_now+=1000000;
    risk_engine_step(&d,&d.risk_state);risk_engine_persistence_step();compose();assert(!strcmp(v.title,"NGUY HIEM"));
    assert(d.risk_state.risk_reason==REASON_FIRE_DETECTED);
    d.temp_object_filtered=25;d.flame_state=FLAME_STATE_SAFE;engine_now+=1000000;
    risk_engine_step(&d,&d.risk_state);risk_engine_persistence_step();compose();assert(!strcmp(v.detail,"GIU NUT XAC NHAN"));
    // Engine retains LATCHED reason when temperature cannot be proven safe.
    d.gy906_quality=QUALITY_INVALID;engine_now+=1000000;
    risk_engine_step(&d,&d.risk_state);risk_engine_persistence_step();compose();assert(!strcmp(v.title,"NGUY HIEM"));
    d.gy906_quality=QUALITY_VALID;engine_now+=1000000;
    risk_engine_step(&d,&d.risk_state);risk_engine_persistence_step();compose();assert(!strcmp(v.detail,"GIU NUT XAC NHAN"));
    assert(risk_engine_process_touch_hold(CONFIG_LONG_PRESS_ACK_SEC*1000));
    engine_now+=1000000;risk_engine_step(&d,&d.risk_state);risk_engine_persistence_step();
    engine_now+=1000000;risk_engine_step(&d,&d.risk_state);risk_engine_persistence_step();compose();
    assert(!d.risk_state.emergency_latched && d.risk_state.risk_level==RISK_NORMAL);
    assert(!strcmp(v.title,"25.0C"));
    puts("OLED UI states/reasons, bounds, stale data, transitions, ACK guard and input immutability: PASS");
}
