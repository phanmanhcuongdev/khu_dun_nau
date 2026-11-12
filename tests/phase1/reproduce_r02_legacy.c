/* Compile only against the original baseline event_buffer.h/.c, not the new API. */
#include "event_buffer.h"
#include <stdio.h>
int main(void)
{
    kitchen_event_t info={.priority=EVENT_PRIO_INFO,.risk_level=RISK_NORMAL};
    kitchen_event_t fire={.priority=EVENT_PRIO_EMERGENCY,.risk_level=RISK_EMERGENCY};
    kitchen_event_t out;
    event_buffer_init();event_buffer_push(&info);event_buffer_peek(&out);
    event_buffer_push(&fire);event_buffer_drop_top();
    if(event_buffer_peek(&out)!=ESP_OK || out.risk_level!=RISK_EMERGENCY){
        puts("BASELINE R02 FAIL reproduced: completing INFO deleted unsent EMERGENCY");
        return 1;
    }
    return 0;
}
