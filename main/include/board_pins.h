#pragma once

#include "driver/gpio.h"
#include "esp_adc/adc_oneshot.h"

// GY-906 (MLX90614) Non-contact IR Temperature Sensor
#define I2C_MASTER_SCL_IO       GPIO_NUM_5
#define I2C_MASTER_SDA_IO       GPIO_NUM_6
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      100000
#define MLX90614_ADDR           0x5A
#define MLX90614_TA             0x06
#define MLX90614_TOBJ1          0x07

// OLED SSD1306 (Dedicated I2C bus)
#define OLED_I2C_SCL_IO         GPIO_NUM_47    // SCK oled 0.96 (doi tu GPIO 20 tranh xung dot USB D+)
#define OLED_I2C_SDA_IO         GPIO_NUM_21    // SDA oled 0.96
#define OLED_I2C_NUM            I2C_NUM_1
#define OLED_I2C_FREQ_HZ        100000

// Flame Sensor
#define FLAME_DO_PIN            GPIO_NUM_4     // DO flame sensor (chân duy nhất đang dùng)
#define FLAME_USE_AO            1
#define FLAME_AO_PIN            ADC_CHANNEL_6  // GPIO7 / ADC1_CH6
#define FLAME_DO_ACTIVE_LEVEL   0              // 0: Active-Low (0 = FIRE, 1 = SAFE)

// MQ-2 Gas & Smoke Sensor
#define MQ2_DO_PIN              GPIO_NUM_11    // DO mq2 (chân duy nhất đang dùng)
#define MQ2_USE_AO              1
#define MQ2_AO_PIN              ADC_CHANNEL_9  // GPIO10 / ADC1_CH9
#define MQ2_DO_ACTIVE_LEVEL     0              // 0: Active-Low (LM393 kéo LOW khi vượt ngưỡng so sánh)

// HLK-LD2410 24GHz mmWave Radar
#define RADAR_OUT_PIN           GPIO_NUM_8     // OUT radar
#define RADAR_RX_PIN            GPIO_NUM_18    // LD2410C TX -> ESP UART1 RX
#define RADAR_TX_PIN            GPIO_NUM_17    // ESP UART1 TX -> LD2410C RX
#define RADAR_UART_NUM          UART_NUM_1
#define RADAR_BAUD_RATE         256000
#define RADAR_BUF_SIZE          512

// Buzzer Module (BC337 NPN Transistor driver)
#define BUZZER_PIN              GPIO_NUM_40    // Coi bao dong (doi tu GPIO 41 sang GPIO 40)
#define BUZZER_ACTIVE_LEVEL     1              // 1: HIGH bật transistor BC337, 0: LOW tắt

// Touch Sensor Button (TTP223 / TMP223)
#define TOUCH_PIN               GPIO_NUM_2     // Nut cam ung TTP223
#define TOUCH_ACTIVE_LEVEL      1              // 1: HIGH khi chạm (TOUCH), 0: LOW khi nhả (RELEASE)
