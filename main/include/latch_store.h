#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { LATCH_STORE_PENDING, LATCH_STORE_DURABLE, LATCH_STORE_FAULT } latch_store_health_t;
typedef struct {
    bool latched; /* Last confirmed record; defaults true when unknown. */
    bool available;
    uint32_t ack_seq;
    uint32_t committed_generation;
    latch_store_health_t health;
    esp_err_t error;
} latch_store_status_t;
/* Init once, after nvs_flash_init. Never erases NVS. Missing/corrupt => latched. */
esp_err_t latch_store_init(esp_err_t nvs_result);
latch_store_status_t latch_store_status(void);
/* Single producer: safety task, AFTER local buzzer update. No NVS I/O here. */
esp_err_t latch_store_request(bool latched, uint32_t ack_seq, uint32_t *generation);
