#pragma once
#include "cloud_record.h"
#include "event_buffer.h"
/* No heap allocation. All JSON strings escaped, bounds checked, failures explicit. */
bool cloud_encode(const cloud_record_t *record,const char *kind,bool history,int64_t now_us,char *out,size_t cap);
bool cloud_attributes(char *out,size_t cap,const char *reset_reason);
bool cloud_health(const cloud_record_t *r,size_t heap,size_t minimum,size_t largest,event_stats_t stats,uint32_t journal_loss,unsigned stack,uint64_t step,uint64_t interval,unsigned safety_stack,char *out,size_t cap);
