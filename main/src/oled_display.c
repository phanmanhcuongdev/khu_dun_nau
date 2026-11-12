#include "oled_display.h"
#include "oled_ui.h"
#include "board_pins.h"
#include "ssd1306.h"
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#define OLED_I2C_ADDRESS 0x3C
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
static SSD1306_t s_oled;
static QueueHandle_t s_queue;
static bool s_initialized;
typedef struct { kitchen_sensor_data_t data; bool wifi, mqtt; } display_snapshot_t;
static void display_worker(void *unused)
{
    (void)unused;
    display_snapshot_t snapshot={0};
    bool have_snapshot=false;
    oled_ui_view_t view;
    uint8_t frame[OLED_UI_BYTES], previous[OLED_UI_BYTES];
    memset(previous,0xff,sizeof(previous));
    oled_ui_compose(NULL,false,false,esp_timer_get_time(),&view);
    bool boot=true;
    while (1) {
        if (!oled_ui_render(&view, (esp_timer_get_time()/1000000)%2, frame)) {
            ESP_LOGE("OLED", "UI layout exceeds display bounds");
        } else {
            /* Only send changed pages: no clear-screen flash on updates. */
            for(int page=0;page<8;page++) {
                if (memcmp(frame+page*128,previous+page*128,128))
                    ssd1306_display_image(&s_oled,page,0,frame+page*128,128);
            }
            memcpy(previous,frame,sizeof(frame));
        }
        if (boot) { ESP_LOGI("OLED", "Adaptive UI V2 ready 128x64 SDA21/SCL47"); boot=false; }
        if (xQueueReceive(s_queue,&snapshot,pdMS_TO_TICKS(1000)) == pdTRUE) have_snapshot=true;
        oled_ui_compose(have_snapshot?&snapshot.data:NULL,snapshot.wifi,snapshot.mqtt,esp_timer_get_time(),&view);
    }
}
esp_err_t oled_display_init(void)
{
    if (s_initialized) return ESP_OK;
    i2c_config_t config = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = OLED_I2C_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = OLED_I2C_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = OLED_I2C_FREQ_HZ,
    };
    esp_err_t err = i2c_param_config(OLED_I2C_NUM, &config);
    if (err == ESP_OK) err = i2c_driver_install(OLED_I2C_NUM, config.mode, 0, 0, 0);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_add(&s_oled, OLED_I2C_NUM, -1, OLED_I2C_ADDRESS);
    err = ssd1306_init(&s_oled, OLED_WIDTH, OLED_HEIGHT);
    if (err != ESP_OK) return err;
    s_queue=xQueueCreate(1,sizeof(display_snapshot_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(display_worker,"oled_ui",4096,NULL,1,NULL)!=pdPASS) {
        vQueueDelete(s_queue); s_queue=NULL; return ESP_ERR_NO_MEM;
    }
    s_initialized=true;
    return ESP_OK;
}
void oled_display_update(const kitchen_sensor_data_t *data,bool wifi,bool mqtt)
{
    if (!s_initialized || !data) return;
    display_snapshot_t snapshot={.data=*data,.wifi=wifi,.mqtt=mqtt};
    xQueueOverwrite(s_queue,&snapshot);
}
