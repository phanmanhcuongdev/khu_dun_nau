#include "rgb_status.h"
#include <stdbool.h>
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t lock;
static esp_timer_handle_t timer;
static int pins[3], configured;
static bool timer_configured;
static rgb_status_t state = RGB_OFF;
static int64_t epoch;
static uint8_t manual[3];
static void render(void *unused)
{
    (void)unused;
    if (xSemaphoreTake(lock, 0) != pdTRUE) return;
    uint8_t c[3] = {0};
    int64_t ms = (esp_timer_get_time() - epoch) / 1000;
    switch (state) {
    case RGB_BOOTING: if (ms % 1000 < 500) c[2]=255; break;
    case RGB_INFO: c[1]=180; break;
    case RGB_WARNING: if (ms % 1000 < 500) { c[0]=255; c[1]=100; } break;
    case RGB_FIRE: if (ms % 320 < 160) c[0]=255; break;
    case RGB_SENSOR_ERROR:
        if (ms % 1000 < 100 || (ms % 1000 >= 200 && ms % 1000 < 300)) { c[0]=160; c[2]=255; } break;
    case RGB_MANUAL: for (int i=0;i<3;i++) c[i]=manual[i]; break;
    default: break;
    }
    for (int i=0;i<3;i++) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5+i, c[i]);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5+i);
    }
    xSemaphoreGive(lock);
}
void rgb_status_deinit(void)
{
    if (timer) {
        /* Lifecycle is called from a normal task with runtime callers quiesced. */
        if (esp_timer_stop_blocking(timer, portMAX_DELAY) != ESP_OK) return;
        if (esp_timer_delete(timer) != ESP_OK) return;
        timer=NULL;
    }
    if (lock) xSemaphoreTake(lock, portMAX_DELAY);
    for (int i=0;i<configured;i++) {
        ledc_stop(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_5+i, 0);
        ledc_channel_config_t c = { .speed_mode=LEDC_LOW_SPEED_MODE,
            .channel=LEDC_CHANNEL_5+i, .deconfigure=true };
        ledc_channel_config(&c);
        gpio_reset_pin(pins[i]);
    }
    configured=0;
    ledc_timer_config_t t = { .speed_mode=LEDC_LOW_SPEED_MODE, .timer_num=LEDC_TIMER_3, .deconfigure=true };
    if (timer_configured) {
        ledc_timer_pause(LEDC_LOW_SPEED_MODE, LEDC_TIMER_3);
        ledc_timer_config(&t);
        timer_configured=false;
    }
    if (lock) { xSemaphoreGive(lock); vSemaphoreDelete(lock); lock=NULL; }
}
esp_err_t rgb_status_init(int r, int g, int b)
{
    if (lock) return ESP_ERR_INVALID_STATE;
    int proposed[3]={r,g,b};
    for (int i=0;i<3;i++) {
        /* Never permit flash/Octal memory pins, even when PSRAM is disabled. */
        if (!GPIO_IS_VALID_OUTPUT_GPIO(proposed[i]) || (proposed[i]>=26 && proposed[i]<=37)) return ESP_ERR_INVALID_ARG;
        for (int j=0;j<i;j++) if (proposed[i]==proposed[j]) return ESP_ERR_INVALID_ARG;
        pins[i]=proposed[i];
    }
    lock=xSemaphoreCreateMutex();
    if (!lock) return ESP_ERR_NO_MEM;
    ledc_timer_config_t t={.speed_mode=LEDC_LOW_SPEED_MODE,.timer_num=LEDC_TIMER_3,.duty_resolution=LEDC_TIMER_8_BIT,.freq_hz=5000,.clk_cfg=LEDC_AUTO_CLK};
    esp_err_t e=ledc_timer_config(&t);
    if (e!=ESP_OK) goto fail;
    timer_configured=true;
    for (int i=0;i<3;i++) {
        ledc_channel_config_t c={.gpio_num=pins[i],.speed_mode=LEDC_LOW_SPEED_MODE,.channel=LEDC_CHANNEL_5+i,.timer_sel=LEDC_TIMER_3,.duty=0};
        e=ledc_channel_config(&c);
        if (e!=ESP_OK) goto fail;
        configured++;
    }
    esp_timer_create_args_t a={.callback=render,.dispatch_method=ESP_TIMER_TASK,.name="rgb_status"};
    e=esp_timer_create(&a,&timer);
    if (e!=ESP_OK) goto fail;
    state=RGB_BOOTING; epoch=esp_timer_get_time();
    e=esp_timer_start_periodic(timer,10000);
    if (e!=ESP_OK) goto fail;
    render(NULL);
    return ESP_OK;
fail:
    rgb_status_deinit(); return e;
}
void rgb_status_set(rgb_status_t s)
{
    if (!lock || s<RGB_BOOTING || s>RGB_OFF) return;
    xSemaphoreTake(lock,portMAX_DELAY);
    if (s!=state) { state=s; epoch=esp_timer_get_time(); }
    xSemaphoreGive(lock); render(NULL);
}
void rgb_status_color(uint8_t r,uint8_t g,uint8_t b)
{
    if (!lock) return;
    xSemaphoreTake(lock,portMAX_DELAY);
    manual[0]=r; manual[1]=g; manual[2]=b; state=RGB_MANUAL;
    xSemaphoreGive(lock); render(NULL);
}
void rgb_status_off(void) { rgb_status_set(RGB_OFF); }
