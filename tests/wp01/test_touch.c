#include <assert.h>
#include <stdio.h>
#include "../../main/src/touch_btn.c"
static int pin,add_error,service_error;
static int64_t clock_us=1000000;
int64_t esp_timer_get_time(void){return clock_us;}
int gpio_get_level(int p){(void)p;return pin;}
esp_err_t gpio_config(const gpio_config_t *c){(void)c;return ESP_OK;}
esp_err_t gpio_install_isr_service(int f){(void)f;return service_error;}
esp_err_t gpio_isr_handler_add(int p,void(*fn)(void*),void *a){(void)p;(void)fn;(void)a;return add_error;}
esp_err_t gpio_isr_handler_remove(int p){(void)p;return ESP_OK;}
esp_err_t gpio_intr_disable(int p){(void)p;return ESP_OK;}
esp_err_t gpio_intr_enable(int p){(void)p;return ESP_OK;}
static void edge(int p){pin=p;touch_gpio_isr_handler(NULL);}
int main(void){
 service_error=ESP_ERR_INVALID_STATE;assert(touch_btn_init()!=ESP_OK);service_error=0;
 add_error=ESP_FAIL;assert(touch_btn_init()!=ESP_OK);add_error=0;
 service_error=ESP_FAIL;assert(touch_btn_init()!=ESP_OK);service_error=ESP_ERR_INVALID_STATE;
 pin=1;assert(touch_btn_init()==ESP_OK);clock_us+=10000000;assert(touch_btn_hold_ms()==0);
 edge(0);edge(1);clock_us+=20000;assert(touch_btn_hold_ms()==0);clock_us+=3010000;assert(touch_btn_hold_ms()>=3000);
 edge(0);assert(touch_btn_hold_ms()==0);
 for(int i=0;i<50;i++){edge(1);clock_us+=100000;edge(0);clock_us+=1000000;int state,e,raw;bool valid;touch_btn_read_state(&state,&e,&valid,&raw);assert(!state && e==1 && raw==0 && valid && touch_btn_hold_ms()==0);}
 edge(1);clock_us+=3030000;assert(touch_btn_hold_ms()==3000);edge(0);assert(touch_btn_hold_ms()==0);
 puts("WP01 actual touch driver ISR registration/debounce/boot-held/release/50 separate taps/continuous hold: PASS (host GPIO)");
}
