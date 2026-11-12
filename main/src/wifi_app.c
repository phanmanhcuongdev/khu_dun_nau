#include "wifi_app.h"
#include "pccc_build_config.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <string.h>
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static bool ready,online;
static int64_t retry_at;
static uint32_t backoff=1;
static esp_event_handler_instance_t wifi_handler,ip_handler;
static void event(void *arg,esp_event_base_t base,int32_t id,void *data)
{
 (void)arg;(void)data;
 portENTER_CRITICAL(&lock);
 if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP){online=true;backoff=1;retry_at=INT64_MAX;}
 else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){online=false;retry_at=esp_timer_get_time()+(backoff*1000+esp_random()%1000)*1000LL;backoff=backoff<32?backoff*2:60;}
 else if(base==WIFI_EVENT && id==WIFI_EVENT_STA_START)retry_at=0;
 portEXIT_CRITICAL(&lock);
 if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP)ESP_LOGI("WIFI","IP ready (credentials redacted)");
 if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED){
  unsigned reason=data?((wifi_event_sta_disconnected_t*)data)->reason:0;
  ESP_LOGW("WIFI","Disconnected reason=%u; bounded retry scheduled",reason);
 }
}
esp_err_t wifi_init_sta(void)
{
 if(!PCCC_PROVISIONED){ESP_LOGW("WIFI","Not provisioned: local safety only");return ESP_ERR_INVALID_STATE;}
 if(ready)return ESP_OK;
 /* SDK INFO connection logs include the SSID; keep provisioning out of logs. */
 esp_log_level_set("wifi",ESP_LOG_WARN);
 esp_err_t err=esp_netif_init();if(err!=ESP_OK && err!=ESP_ERR_INVALID_STATE)return err;
 err=esp_event_loop_create_default();if(err!=ESP_OK && err!=ESP_ERR_INVALID_STATE)return err;
 /* The convenience create_default helper asserts internally on OOM/errors. */
 esp_netif_config_t netcfg=ESP_NETIF_DEFAULT_WIFI_STA();
 esp_netif_t *netif=esp_netif_new(&netcfg);if(!netif)return ESP_ERR_NO_MEM;
 err=esp_netif_attach_wifi_station(netif);
 if(err==ESP_OK)err=esp_wifi_set_default_wifi_sta_handlers();
 if(err!=ESP_OK){esp_netif_destroy_default_wifi(netif);return err;}
 wifi_init_config_t cfg=WIFI_INIT_CONFIG_DEFAULT();
 err=esp_wifi_init(&cfg);if(err!=ESP_OK){esp_netif_destroy_default_wifi(netif);return err;}
 err=esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL,&wifi_handler);
 if(err!=ESP_OK)goto fail;
 err=esp_event_handler_instance_register(IP_EVENT,IP_EVENT_STA_GOT_IP,event,NULL,&ip_handler);
 if(err!=ESP_OK){esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_handler);goto fail;}
 wifi_config_t config={0};
 memcpy(config.sta.ssid,PCCC_WIFI_SSID,strlen(PCCC_WIFI_SSID));
 memcpy(config.sta.password,PCCC_WIFI_PASSWORD,strlen(PCCC_WIFI_PASSWORD));
 config.sta.threshold.authmode=WIFI_AUTH_WPA2_PSK;
 err=esp_wifi_set_storage(WIFI_STORAGE_RAM);
 if(err==ESP_OK)err=esp_wifi_set_mode(WIFI_MODE_STA);
 if(err==ESP_OK)err=esp_wifi_set_config(WIFI_IF_STA,&config);
 if(err==ESP_OK)err=esp_wifi_start();
 if(err==ESP_OK){ready=true;return ESP_OK;}
 esp_event_handler_instance_unregister(IP_EVENT,IP_EVENT_STA_GOT_IP,ip_handler);
 esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_handler);
fail:
 esp_wifi_deinit();esp_netif_destroy_default_wifi(netif);return err;
}
/* Network task owns retry execution, callbacks only schedule it. */
void wifi_app_poll(int64_t now)
{
 portENTER_CRITICAL(&lock);bool due=ready&&!online&&now>=retry_at;if(due)retry_at=now+10000000;portEXIT_CRITICAL(&lock);
 if(due){esp_err_t e=esp_wifi_connect();if(e!=ESP_OK)ESP_LOGW("WIFI","Connect attempt failed: %s",esp_err_to_name(e));}
}
bool wifi_is_connected(void){portENTER_CRITICAL(&lock);bool yes=online;portEXIT_CRITICAL(&lock);return yes;}
