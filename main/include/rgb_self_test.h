#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "rgb_status.h"
/* Called only by app_main after risk evaluation; no task, delay or I/O. */
typedef struct {
    bool started;
    bool finished;
    int64_t start_us;
} rgb_self_test_t;
static inline rgb_status_t rgb_self_test_select(rgb_self_test_t *test,
                                               rgb_status_t live, int64_t now_us)
{
    if (test->finished) return live;
    /* A real alarm cancels testing, including when the current test is red. */
    if (live == RGB_FIRE || live == RGB_WARNING || live == RGB_SENSOR_ERROR) {
        test->finished = true;
        return live;
    }
    if (!test->started) {
        test->started = true;
        test->start_us = now_us;
    }
    int64_t elapsed = now_us - test->start_us;
    if (elapsed < 0 || elapsed >= 25000000) {
        test->finished = true;
        return live;
    }
    static const rgb_status_t sequence[] = {
        RGB_BOOTING, RGB_INFO, RGB_WARNING, RGB_FIRE, RGB_SENSOR_ERROR
    };
    return sequence[elapsed / 5000000];
}
