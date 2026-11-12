#pragma once
#include "cloud_record.h"
#define JOURNAL_SLOTS 4
typedef struct {bool used;char boot[33];uint64_t seq;} journal_entry_t;
/* Network-task-only NVS owner; no safety task writes or erases NVS. */
esp_err_t critical_journal_init(void);
esp_err_t critical_journal_add(const cloud_record_t *record,const char *json);
esp_err_t critical_journal_read(int slot,char *json,size_t cap);
journal_entry_t critical_journal_entry(int slot);
void critical_journal_complete(const char *boot,uint64_t seq);
uint32_t critical_journal_losses(void);
