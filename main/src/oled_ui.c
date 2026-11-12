#include "oled_ui.h"
#include "sensor_pipeline.h"
#include "safety_config.h"
#include "font8x8_basic.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static const char *reason_text(risk_reason_t reason)
{
    switch (reason) {
    case REASON_NONE: return "DANG THEO DOI";
    case REASON_INITIALIZING: return "CHO CAM BIEN";
    case REASON_FIRE_DETECTED: return "PHAT HIEN LUA";
    case REASON_CRITICAL_TEMPERATURE: return "NHIET DO RAT CAO";
    case REASON_HIGH_TEMPERATURE: return "NHIET DO CAO";
    case REASON_RAPID_TEMPERATURE_RISE: return "NHIET TANG NHANH";
    case REASON_UNATTENDED_COOKING: return "BEP VANG NGUOI";
    case REASON_GAS_SMOKE_ELEVATED: return "KHI GAS/KHOI CAO";
    case REASON_SENSOR_FAULT: return "LOI CAM BIEN";
    case REASON_SENSOR_STALE: return "DU LIEU QUA HAN";
    case REASON_RECOVERY_PENDING: return "CHO PHUC HOI";
    case REASON_EMERGENCY_LATCHED: return "BAO DONG DA CHOT";
    case REASON_EMERGENCY_ACKNOWLEDGED: return "DA XAC NHAN";
    case REASON_NORMAL_SAFE: return "DANG GIAM SAT";
    default: return "KIEM TRA HE THONG";
    }
}
static void text(char dst[17], const char *src) { snprintf(dst, 17, "%s", src); }
static void duration(char dst[17], const char *label, uint32_t seconds)
{
    char value[6];
    if (seconds > 999) snprintf(value,sizeof(value),">999s");
    else snprintf(value,sizeof(value),"%us",(unsigned)seconds);
    snprintf(dst,17,"%s%s",label,value);
}
void oled_ui_compose(const kitchen_sensor_data_t *d, bool wifi, bool mqtt,
                     int64_t now, oled_ui_view_t *v)
{
    memset(v, 0, sizeof(*v));
    snprintf(v->footer, sizeof(v->footer), "W:%s M:%s", wifi?"OK":"--", mqtt?"OK":"--");
    if (!d) {
        text(v->title,"DANG KHOI DONG"); text(v->detail,"CHO CAM BIEN"); return;
    }
    const risk_state_t *r = &d->risk_state;
    bool fresh = d->gy906_valid && d->gy906_quality == QUALITY_VALID &&
        d->gy906_last_update > 0 && now >= d->gy906_last_update &&
        now - d->gy906_last_update <= GY906_STALE_TIMEOUT_MS * 1000LL &&
        isfinite(d->temp_object_filtered);
    text(v->detail, reason_text(r->risk_reason));
    if (r->risk_level == RISK_EMERGENCY || r->emergency_latched) {
        /* Reason can linger while engine refuses ACK: recheck its existing safety
         * prerequisites for the prompt only. Never acknowledge or lower risk. */
        bool can_ack = r->emergency_latched && r->risk_reason == REASON_EMERGENCY_LATCHED &&
            fresh && d->temp_object_filtered < CONFIG_TEMP_EMERGENCY_RECOVER_C &&
            d->flame_quality == QUALITY_VALID && d->flame_state == FLAME_STATE_SAFE;
        if (can_ack) {
            text(v->title,"BAO DONG DA CHOT");
            text(v->detail,"GIU NUT XAC NHAN");
            snprintf(v->action, sizeof(v->action), "GIU %ds TAI CHO", CONFIG_LONG_PRESS_ACK_SEC);
        } else {
            text(v->title, "NGUY HIEM");
            if (r->risk_reason == REASON_EMERGENCY_LATCHED) text(v->detail,"CHUA DU DK ACK");
            text(v->action,"ROI XA KHU VUC");
            text(v->footer,"GOI HO TRO");
        }
    } else if (r->risk_level == RISK_WARNING) {
        text(v->title,"CANH BAO"); text(v->action,"KIEM TRA KHU VUC");
    } else if (r->monitoring_status == MONITORING_FAULT ||
               r->risk_reason == REASON_SENSOR_FAULT || r->risk_reason == REASON_SENSOR_STALE) {
        text(v->title,"LOI CAM BIEN");
        if (d->gy906_quality != QUALITY_VALID) text(v->detail,"CAM BIEN NHIET");
        else if (d->flame_quality != QUALITY_VALID) text(v->detail,"CAM BIEN LUA");
        else text(v->detail,"KIEM TRA KET NOI");
        text(v->action,"CAN KIEM TRA");
    } else if (r->risk_level == RISK_SUSPICIOUS) {
        text(v->title,"DANG THEO DOI"); text(v->action,"KIEM TRA BEP");
    } else if (r->risk_level == RISK_NORMAL) {
        v->temperature_large = true;
        /* 8 large glyphs max; reject out-of-format values without inventing thresholds. */
        char temp[48];
        snprintf(temp, sizeof(temp), "%.1fC", (double)d->temp_object_filtered);
        text(v->title, fresh && strlen(temp) <= 8 ? temp : "--.-C");
        text(v->detail,"NHIET DO VAT THE");
        text(v->action, r->monitoring_status == MONITORING_DEGRADED ? "GIAM SAT HAN CHE" : "DANG GIAM SAT");
    } else {
        text(v->title,"DANG KHOI DONG");
        if (d->mq2_warming_up) duration(v->detail,"CHO MQ2 ",d->mq2_warmup_remaining_sec);
        text(v->action,"VUI LONG CHO");
    }
    if (r->risk_level != RISK_EMERGENCY && !r->emergency_latched) {
        if (r->recovery_countdown_sec) duration(v->action,"ON DINH ",r->recovery_countdown_sec);
        else if (d->is_muted) duration(v->footer,"TAT COI ",d->mute_remaining_sec);
    }
}
static bool draw(uint8_t *f,int y,const char *s,int sx,int sy)
{
    size_t n = strlen(s);
    if (n * 8 * sx > 128 || y < 0 || y + 8 * sy > 64) return false;
    int x0 = (128 - (int)n * 8 * sx) / 2;
    for (size_t i=0;i<n;i++) {
        unsigned c=(unsigned char)s[i];
        if (c>=128) return false;
        for (int x=0;x<8;x++) for(int b=0;b<8;b++) {
            if (!(font8x8_basic_tr[c][x] & (1<<b))) continue;
            for(int dx=0;dx<sx;dx++) for(int dy=0;dy<sy;dy++) {
                int px=x0+(int)i*8*sx+x*sx+dx, py=y+b*sy+dy;
                f[py/8*128+px] |= 1<<(py%8);
            }
        }
    }
    return true;
}
bool oled_ui_render(const oled_ui_view_t *v,bool heartbeat,uint8_t frame[OLED_UI_BYTES])
{
    memset(frame,0,OLED_UI_BYTES);
    bool ok=draw(frame,0,"PCCC V1",1,1);
    if (heartbeat) frame[124] = 0x18;
    ok=draw(frame,16,v->title,v->temperature_large?2:1,2) && ok;
    ok=draw(frame,36,v->detail,1,1) && ok;
    ok=draw(frame,46,v->action,1,1) && ok;
    return draw(frame,56,v->footer,1,1) && ok;
}
