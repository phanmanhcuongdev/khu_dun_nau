#pragma once
#include "cloud_record.h"
#define EVENT_BUFFER_MAX_CAPACITY 32
#define EVENT_INFLIGHT_MAX 4
typedef enum { EVENT_PRIO_EMERGENCY=1, EVENT_PRIO_WARNING=2, EVENT_PRIO_FAULT=3, EVENT_PRIO_INFO=4 } event_priority_t;
typedef struct { cloud_record_t record; event_priority_t priority; } kitchen_event_t;
typedef struct { uint32_t created,queued,accepted,completed,retries,rejected,evicted,expired,duplicates; } event_stats_t;
esp_err_t event_buffer_init(void);
esp_err_t event_buffer_push(const kitchen_event_t *event);
/* Atomically claim a record; no external code retains the lock while publishing. */
esp_err_t event_buffer_claim(kitchen_event_t *out);
esp_err_t event_buffer_complete(const char *boot,uint64_t seq);
void event_buffer_retry(const char *boot,uint64_t seq,bool expired);
void event_buffer_reconnect(void);
size_t event_buffer_get_count(void);
event_stats_t event_buffer_stats(void);

bool event_buffer_journal_candidate(kitchen_event_t *out);
void event_buffer_accepted(void);

void event_buffer_journal_rescan(void);
