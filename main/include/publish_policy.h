#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {int64_t measurements,health,traffic;bool measured,healthy;} publish_policy_t;
static inline bool publish_measure_due(const publish_policy_t *p,int64_t now,bool abnormal){return !p->measured || now-p->measurements>=(abnormal?2000000LL:10000000LL);}
static inline bool publish_health_due(const publish_policy_t *p,int64_t now){return !p->healthy || now-p->health>=60000000LL;}
static inline bool publish_heartbeat_due(const publish_policy_t *p,int64_t now){return now-p->traffic>=30000000LL;}
