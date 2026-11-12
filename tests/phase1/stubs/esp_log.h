#pragma once
static inline void mock_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
#define ESP_LOGI(tag,...) mock_log(tag,__VA_ARGS__)
#define ESP_LOGW(tag,...) mock_log(tag,__VA_ARGS__)
#define ESP_LOGE(tag,...) mock_log(tag,__VA_ARGS__)
#define ESP_LOG_WARN 2
static inline void esp_log_level_set(const char *tag,int level){(void)tag;(void)level;}
