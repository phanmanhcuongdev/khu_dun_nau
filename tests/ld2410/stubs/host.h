#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 1
#define ESP_ERR_NO_MEM 2
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdPASS 1
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
#define RADAR_OUT_PIN 8
#define RADAR_RX_PIN 18
#define RADAR_TX_PIN 17
#define RADAR_UART_NUM 1
#define RADAR_BAUD_RATE 256000
#define RADAR_BUF_SIZE 512
#define GPIO_INTR_DISABLE 0
#define GPIO_MODE_INPUT 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_PULLUP_DISABLE 0
#define UART_DATA_8_BITS 8
#define UART_PARITY_DISABLE 0
#define UART_STOP_BITS_1 1
#define UART_HW_FLOWCTRL_DISABLE 0
#define UART_SCLK_DEFAULT 0
#define UART_PIN_NO_CHANGE -1
#define ESP_LOG_INFO 1
#define ESP_LOG_DEBUG 0
typedef struct {int intr_type,mode; uint64_t pin_bit_mask; int pull_down_en,pull_up_en;} gpio_config_t;
typedef struct {int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk;} uart_config_t;
typedef void *QueueHandle_t;
typedef struct { int type; size_t size; } uart_event_t;
enum {UART_DATA, UART_FIFO_OVF, UART_BUFFER_FULL, UART_FRAME_ERR, UART_PARITY_ERR, UART_BREAK};
static uart_event_t queued_events[16];
static size_t event_count, event_index;
static inline int xQueueReceive(QueueHandle_t q, void *event, int wait) {
    (void)q; (void)wait;
    if (event_index == event_count) return 0;
    *(uart_event_t *)event = queued_events[event_index++]; return pdTRUE;
}
static uint8_t queued_rx[256];
static size_t queued_rx_len;
static int64_t clock_us;
static int out_level, api_error, task_ok=1, write_result=-99;
static size_t writes, tx_size;
static uint8_t tx[64];
static char captured_logs[16384];
static size_t captured_len;
static size_t hex_bytes;
static void host_log(const char *tag, const char *fmt, ...) {
    (void)tag;
    if (captured_len+512>=sizeof captured_logs) return;
    va_list args;va_start(args,fmt);
    int n=vsnprintf(captured_logs+captured_len,sizeof captured_logs-captured_len,fmt,args);
    va_end(args);
    if(n>0) captured_len+=(size_t)n;
    captured_logs[captured_len++]='\n';captured_logs[captured_len]=0;
}
static inline void host_hex(const void *b,size_t n) {(void)b;hex_bytes+=n;}
#define ESP_LOGI(...) host_log(__VA_ARGS__)
#define ESP_LOGW(...) host_log(__VA_ARGS__)
#define ESP_LOGE(...) host_log(__VA_ARGS__)
#define ESP_LOGD(...) host_log(__VA_ARGS__)
#define ESP_LOG_BUFFER_HEX_LEVEL(t,b,n,l) ((void)(t),host_hex(b,n),(void)(l))
static int64_t esp_timer_get_time(void) {return clock_us;}
static int gpio_config(const gpio_config_t *c) {(void)c;return 0;}
static int gpio_get_level(int p) {(void)p;return out_level;}
static int uart_driver_install(int p,int r,int t,int q,QueueHandle_t *h,int f) {(void)p;(void)r;(void)t;(void)q;(void)f;if(h)*h=(void *)1;return api_error;}
static int uart_driver_delete(int p) {(void)p;return 0;}
static int uart_param_config(int p,const uart_config_t *c) {(void)p;(void)c;return api_error;}
static int uart_set_pin(int p,int t,int r,int a,int b) {(void)p;(void)t;(void)r;(void)a;(void)b;return api_error;}
static inline int uart_write_bytes(int p,const void *b,size_t n) {(void)p;memcpy(tx,b,n);tx_size=n;writes++;return write_result==-99?(int)n:write_result;}
static int uart_read_bytes(int p,void *b,size_t n,int wait) {(void)p;(void)wait;size_t count=queued_rx_len<n?queued_rx_len:n;memcpy(b,queued_rx,count);queued_rx_len=0;return (int)count;}
static int xTaskCreate(void (*f)(void *),const char *n,int stack,void *a,int prio,void *h) {(void)f;(void)n;(void)stack;(void)a;(void)prio;(void)h;return task_ok;}
static void vTaskDelay(int n) {(void)n;}
