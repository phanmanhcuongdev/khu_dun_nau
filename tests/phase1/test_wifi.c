#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../main/src/wifi_app.c"
const char *WIFI_EVENT="wifi",*IP_EVENT="ip";
static int stage,fail_at,netifs,driver,handlers,connections;
static int next(void){stage++;return stage==fail_at?ESP_FAIL:ESP_OK;}
esp_err_t esp_netif_init(void){return next();}
esp_err_t esp_event_loop_create_default(void){return next();}
esp_netif_t *esp_netif_new(const esp_netif_config_t *p){(void)p;if(next()!=ESP_OK)return NULL;netifs++;return (esp_netif_t*)1;}
void esp_netif_destroy_default_wifi(void *p){assert(p&&netifs==1);netifs--;}
esp_err_t esp_netif_attach_wifi_station(esp_netif_t *p){(void)p;return next();}
esp_err_t esp_wifi_set_default_wifi_sta_handlers(void){return next();}
esp_err_t esp_wifi_init(const wifi_init_config_t *p){(void)p;int e=next();if(e==ESP_OK)driver++;return e;}
esp_err_t esp_wifi_deinit(void){assert(driver==1);driver--;return ESP_OK;}
esp_err_t esp_event_handler_instance_register(esp_event_base_t b,int32_t id,void(*fn)(void*,esp_event_base_t,int32_t,void*),void *ctx,esp_event_handler_instance_t *h){(void)b;(void)id;(void)fn;(void)ctx;int e=next();if(e==ESP_OK){handlers++;*h=handlers;}return e;}
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t b,int32_t id,esp_event_handler_instance_t h){(void)b;(void)id;(void)h;assert(handlers>0);handlers--;return ESP_OK;}
esp_err_t esp_wifi_set_storage(int v){assert(v==WIFI_STORAGE_RAM);return next();}
esp_err_t esp_wifi_set_mode(int v){(void)v;return next();}
esp_err_t esp_wifi_set_config(int v,const wifi_config_t *c){(void)v;assert(!memcmp(c->sta.ssid,PCCC_WIFI_SSID,strlen(PCCC_WIFI_SSID)));return next();}
esp_err_t esp_wifi_start(void){return next();}
esp_err_t esp_wifi_connect(void){connections++;return ESP_OK;}
int64_t esp_timer_get_time(void){return 1000000;}
uint32_t esp_random(void){return 123;}
int main(void){
 for(fail_at=1;fail_at<=12;fail_at++){stage=0;assert(wifi_init_sta()!=ESP_OK);assert(!ready&&!online&&netifs==0&&driver==0&&handlers==0);}
 fail_at=0;stage=0;assert(wifi_init_sta()==ESP_OK&&stage==12);assert(ready&&netifs==1&&driver==1&&handlers==2);
 assert(wifi_init_sta()==ESP_OK&&stage==12);
 event(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);wifi_app_poll(1);assert(connections==1);wifi_app_poll(2);assert(connections==1);
 event(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,NULL);assert(wifi_is_connected());wifi_app_poll(INT64_MAX-1);assert(connections==1);
 event(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,NULL);assert(!wifi_is_connected());int64_t due=retry_at;assert(due>1000000);wifi_app_poll(due-1);assert(connections==1);wifi_app_poll(due);assert(connections==2);
 puts("WP06 real Wi-Fi source: 12 startup failures fail-soft/cleanup, init-once, RAM credentials, scheduled reconnect: PASS (mock SDK)");
}
