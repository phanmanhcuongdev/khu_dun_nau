#pragma once
#include <sys/time.h>
#define SNTP_OPMODE_POLL 0
void esp_sntp_setoperatingmode(int);
void esp_sntp_setservername(int,const char*);
void esp_sntp_set_sync_interval(unsigned);
void esp_sntp_set_time_sync_notification_cb(void(*)(struct timeval*));
void esp_sntp_init(void);

void esp_sntp_stop(void);
