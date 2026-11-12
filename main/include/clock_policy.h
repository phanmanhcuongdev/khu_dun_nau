#pragma once
#include <stdbool.h>
#include <stdint.h>
static inline bool clock_sane(int64_t epoch_ms,int64_t build_epoch){return epoch_ms>=(build_epoch-86400)*1000 && epoch_ms<(build_epoch+5LL*366*86400)*1000;}
static inline bool clock_trusted(int64_t sync_us,int64_t sync_epoch,int64_t now,int64_t wall){int64_t age=now-sync_us,expected=sync_epoch+age/1000;return sync_us>0&&age>=0&&age<86400000000LL&&wall>=expected-5000&&wall<=expected+5000;}
