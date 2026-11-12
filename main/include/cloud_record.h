#pragma once
#include "sensor_types.h"
#include "buzzer.h"
#define CLOUD_JSON_MAX 2304
#define CLOUD_BOOT_ID_SIZE 33
typedef struct {
    char boot_id[CLOUD_BOOT_ID_SIZE];
    uint64_t seq;
    int64_t captured_us, epoch_ms;
    bool time_synced, replayed;
    buzzer_mode_t buzzer_mode;
    kitchen_sensor_data_t data;
} cloud_record_t;
