#include "gy906_sensor.h"
#include "board_pins.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "GY906";
static bool s_i2c0_initialized = false;

static esp_err_t read_mlx_reg(uint8_t reg, float *temp_out, float min_limit, float max_limit)
{
    uint8_t buffer[3] = {0};
    esp_err_t ret = i2c_master_write_read_device(
        I2C_MASTER_NUM,
        MLX90614_ADDR,
        &reg, 1,
        buffer, 3,
        pdMS_TO_TICKS(100)
    );

    if (ret != ESP_OK) {
        return ret; // Lỗi giao tiếp I2C bus (ESP_ERR_TIMEOUT, ESP_FAIL, ...)
    }

    uint16_t raw = (uint16_t)(buffer[0] | (buffer[1] << 8));

    // Theo datasheet MLX90614, bit 15 là cờ lỗi cảm biến nội bộ
    if (raw & 0x8000) {
        ESP_LOGW(TAG, "Reg 0x%02X co bit bao loi noi bo: 0x%04X", reg, raw);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Giá trị thô toàn 0 hoặc toàn 1 thường là đường I2C bị treo hoặc hở mạch
    if (raw == 0x0000 || raw == 0xFFFF) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    float calc_temp = (raw * 0.02f) - 273.15f;

    // Kiểm tra giới hạn dải đo vật lý của cảm biến
    if (calc_temp < min_limit || calc_temp > max_limit || isnan(calc_temp)) {
        ESP_LOGW(TAG, "Reg 0x%02X nhiet do ngoai dai ky thuat: %.2f C (gioi han: %.1f .. %.1f)",
                 reg, calc_temp, min_limit, max_limit);
        return ESP_ERR_INVALID_RESPONSE;
    }

    *temp_out = calc_temp;
    return ESP_OK;
}

esp_err_t gy906_sensor_init(void)
{
    if (s_i2c0_initialized) {
        return ESP_OK;
    }

    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };

    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Loi i2c_param_config I2C0: %d", err);
        return err;
    }

    err = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (err == ESP_OK) {
        s_i2c0_initialized = true;
        ESP_LOGI(TAG, "Khoi tao I2C0 cho GY-906 thanh cong (SDA:%d, SCL:%d)", I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);
    } else {
        ESP_LOGE(TAG, "Loi i2c_driver_install I2C0: %d", err);
    }
    return err;
}

esp_err_t gy906_sensor_read(float *ambient, float *object, bool *valid, esp_err_t *err_code)
{
    if (!s_i2c0_initialized) {
        if (valid) *valid = false;
        if (err_code) *err_code = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }

    float ta_val = 0.0f;
    float tobj_val = 0.0f;

    esp_err_t err_ta = read_mlx_reg(MLX90614_TA, &ta_val, GY906_TA_MIN_DEG_C, GY906_TA_MAX_DEG_C);
    esp_err_t err_tobj = read_mlx_reg(MLX90614_TOBJ1, &tobj_val, GY906_TOBJ_MIN_DEG_C, GY906_TOBJ_MAX_DEG_C);

    if (err_ta == ESP_OK && err_tobj == ESP_OK) {
        if (ambient) *ambient = ta_val;
        if (object) *object = tobj_val;
        if (valid) *valid = true;
        if (err_code) *err_code = ESP_OK;
        return ESP_OK;
    }

    // Nếu một trong 2 phép đọc thất bại: Đánh dấu KHÔNG hợp lệ, không gán giá trị 0
    if (valid) *valid = false;
    esp_err_t final_err = (err_ta != ESP_OK) ? err_ta : err_tobj;
    if (err_code) *err_code = final_err;
    return final_err;
}

esp_err_t gy906_read_temperatures(float *ambient, float *object)
{
    bool valid = false;
    esp_err_t err = ESP_OK;
    return gy906_sensor_read(ambient, object, &valid, &err);
}
