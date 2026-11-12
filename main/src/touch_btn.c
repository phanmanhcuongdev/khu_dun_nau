#include "touch_btn.h"
#include "board_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_intr_alloc.h"
#include "hal/gpio_ll.h"
#include "freertos/FreeRTOS.h"
/* Release resets hold immediately. Rising input must stay stable for 30ms.
 * ANYEDGE ISR preserves releases between slow sensor samples. */
#define TOUCH_DEBOUNCE_US 30000LL
static const char *TAG="TOUCH_BTN";
static portMUX_TYPE s_lock=portMUX_INITIALIZER_UNLOCKED;
static bool s_initialized,s_level,s_armed,s_rising;
static bool s_service_iram;
static int64_t s_high_since;
static int s_previous;
static void IRAM_ATTR touch_gpio_isr_handler(void *arg)
{
    (void)arg;
    /* Cache-safe ISR read: NVS flash activity must not hide release edges. */
    bool high=gpio_ll_get_level(&GPIO,TOUCH_PIN)==TOUCH_ACTIVE_LEVEL;
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL_ISR(&s_lock);
    if(!high){s_level=false;s_high_since=0;s_armed=true;}
    else if(!s_level){s_level=true;s_high_since=now;s_rising=true;}
    portEXIT_CRITICAL_ISR(&s_lock);
}
esp_err_t touch_btn_init(void)
{
    if(s_initialized)return ESP_OK;
    gpio_config_t c={.intr_type=GPIO_INTR_ANYEDGE,.mode=GPIO_MODE_INPUT,
        .pin_bit_mask=1ULL<<TOUCH_PIN,.pull_down_en=GPIO_PULLDOWN_ENABLE,
        .pull_up_en=GPIO_PULLUP_DISABLE};
    esp_err_t err=gpio_config(&c);if(err!=ESP_OK)return err;
    err=gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if(err==ESP_OK)s_service_iram=true;
    else if(err!=ESP_ERR_INVALID_STATE || !s_service_iram)return err;
    /* Reuse only a service whose IRAM allocation this module established. */
    /* Install with interrupts disabled to establish a coherent initial level. */
    gpio_intr_disable(TOUCH_PIN);
    bool high=gpio_get_level(TOUCH_PIN)==TOUCH_ACTIVE_LEVEL;
    s_level=high;s_armed=!high;s_high_since=esp_timer_get_time();s_rising=false;s_previous=0;
    err=gpio_isr_handler_add(TOUCH_PIN,touch_gpio_isr_handler,NULL);
    if(err!=ESP_OK)return err;
    err=gpio_intr_enable(TOUCH_PIN);
    if(err!=ESP_OK){gpio_isr_handler_remove(TOUCH_PIN);return err;}
    s_initialized=true;
    ESP_LOGI(TAG,"Continuous physical hold ready; boot-high requires release");
    return ESP_OK;
}
int touch_btn_read(void){return gpio_get_level(TOUCH_PIN);}
uint32_t touch_btn_hold_ms(void)
{
    if(!s_initialized)return 0;
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    bool held=s_armed && s_level && gpio_get_level(TOUCH_PIN)==TOUCH_ACTIVE_LEVEL;
    int64_t elapsed=now-s_high_since-TOUCH_DEBOUNCE_US;
    portEXIT_CRITICAL(&s_lock);
    if(!held || elapsed<=0)return 0;
    uint64_t ms=(uint64_t)elapsed/1000;
    return ms>UINT32_MAX?UINT32_MAX:(uint32_t)ms;
}
esp_err_t touch_btn_read_state(int *state,int *edge,bool *valid,int *gpio_level)
{
    if(!s_initialized){
        if(valid)*valid=false;
        if(state)*state=0;
        if(edge)*edge=0;
        return ESP_ERR_INVALID_STATE;
    }
    int raw=gpio_get_level(TOUCH_PIN);
    int64_t now=esp_timer_get_time();
    portENTER_CRITICAL(&s_lock);
    bool rising=s_rising;s_rising=false; /* atomic consume against ISR */
    int pressed=s_armed && s_level && raw==TOUCH_ACTIVE_LEVEL && now-s_high_since>=TOUCH_DEBOUNCE_US;
    portEXIT_CRITICAL(&s_lock);
    int e=rising?1:(!pressed && s_previous?-1:0);
    s_previous=pressed;
    if(state)*state=pressed;
    if(edge)*edge=e;
    if(valid)*valid=true;
    if(gpio_level)*gpio_level=raw;
    return ESP_OK;
}
