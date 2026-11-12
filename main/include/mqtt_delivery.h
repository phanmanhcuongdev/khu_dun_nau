#pragma once
#include <stdbool.h>
#include <stdint.h>
#define DELIVERY_MAX 4
typedef struct {char boot[33];uint64_t seq;bool event;} delivery_token_t;
typedef struct {int mid;int64_t accepted_us;delivery_token_t token;} delivery_slot_t;
typedef struct {uint32_t epoch;unsigned count;uint8_t used_mid[8192];delivery_slot_t slots[DELIVERY_MAX];} delivery_t;
void delivery_reset(delivery_t *d,uint32_t epoch);
bool delivery_accept(delivery_t *d,int mid,delivery_token_t token,int64_t now);
bool delivery_ack(delivery_t *d,uint32_t epoch,int mid,delivery_token_t *out);
bool delivery_contains(const delivery_t *d,const char *boot,uint64_t seq);
bool delivery_timed_out(const delivery_t *d,int64_t now);
