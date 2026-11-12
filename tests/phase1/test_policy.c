#include "publish_policy.h"
#include "clock_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 publish_policy_t p={0};assert(publish_measure_due(&p,0,false));p.measured=true;p.measurements=0;
 assert(!publish_measure_due(&p,9999999,false)&&publish_measure_due(&p,10000000,false));
 assert(!publish_measure_due(&p,1999999,true)&&publish_measure_due(&p,2000000,true));
 p.healthy=true;p.health=0;assert(!publish_health_due(&p,59999999)&&publish_health_due(&p,60000000));
 assert(!publish_heartbeat_due(&p,29999999)&&publish_heartbeat_due(&p,30000000));
 int64_t epoch=1794358800000LL,build=epoch/1000;
 assert(clock_sane(epoch,build));assert(!clock_sane(epoch-86400001,build));
 assert(!clock_trusted(0,epoch,1000000,epoch));assert(clock_trusted(1000000,epoch,2000000,epoch+1000));
 assert(!clock_trusted(1000000,epoch,2000000,epoch+7000));assert(!clock_trusted(1000000,epoch,86401000000LL,epoch));
 puts("WP04 publish cadence independent of sampling and trusted-clock gates/backward/forward jump/expiry: PASS");
}
