#include "event_buffer.h"
#include "mqtt_delivery.h"
#include <assert.h>
#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <sched.h>
static kitchen_event_t make(uint64_t seq,int priority){kitchen_event_t e={0};strcpy(e.record.boot_id,"00000000000000000000000000000001");e.record.seq=seq;e.priority=priority;return e;}
static atomic_int done;
static void *producer(void *arg){uintptr_t base=(uintptr_t)arg;for(int i=0;i<1000;i++){kitchen_event_t e=make(base+i,i%4+1);event_buffer_push(&e);if(i%17==0)sched_yield();}atomic_fetch_add(&done,1);return NULL;}
static void *consumer(void *unused){(void)unused;while(atomic_load(&done)<2||event_buffer_get_count()){
 kitchen_event_t e;if(event_buffer_claim(&e)==ESP_OK)assert(event_buffer_complete(e.record.boot_id,e.record.seq)==ESP_OK);else sched_yield();
 }return NULL;}
int main(void){
 event_buffer_init();kitchen_event_t info=make(1,4),fire=make(2,1),claimed;
 assert(event_buffer_push(&info)==ESP_OK);assert(event_buffer_claim(&claimed)==ESP_OK);
 assert(event_buffer_push(&fire)==ESP_OK);assert(event_buffer_complete(claimed.record.boot_id,claimed.record.seq)==ESP_OK);
 assert(event_buffer_claim(&claimed)==ESP_OK&&claimed.record.seq==2);event_buffer_retry(claimed.record.boot_id,2,false);
 assert(event_buffer_push(&fire)==ESP_OK&&event_buffer_get_count()==1);assert(event_buffer_stats().duplicates==1);
 event_buffer_init();for(int i=0;i<32;i++){kitchen_event_t e=make(i+1,4);assert(event_buffer_push(&e)==ESP_OK);}
 assert(event_buffer_push(&fire)==ESP_OK); // ID2 duplicate existing record, no overwrite!
 fire=make(100,1);assert(event_buffer_push(&fire)==ESP_OK);assert(event_buffer_stats().evicted==1);
 event_buffer_init();for(int i=0;i<32;i++){kitchen_event_t e=make(i+1,1);assert(event_buffer_push(&e)==ESP_OK);}
 fire=make(100,1);assert(event_buffer_push(&fire)==ESP_ERR_NO_MEM);assert(event_buffer_stats().rejected==1);
 delivery_t d;delivery_reset(&d,1);delivery_token_t a={.seq=1,.event=true},b={.seq=2,.event=true},out;
 strcpy(a.boot,info.record.boot_id);strcpy(b.boot,a.boot);
 assert(delivery_accept(&d,10,a,0));assert(delivery_accept(&d,11,b,0));
 assert(delivery_ack(&d,1,11,&out)&&out.seq==2);assert(!delivery_ack(&d,1,11,&out));
 assert(delivery_ack(&d,1,10,&out)&&out.seq==1);assert(!delivery_accept(&d,10,b,0));
 delivery_reset(&d,2);assert(delivery_accept(&d,10,b,0));assert(!delivery_ack(&d,1,10,&out));assert(delivery_ack(&d,2,10,&out)&&out.seq==2);
 delivery_reset(&d,3);for(int i=0;i<4;i++)assert(delivery_accept(&d,20+i,a,0));assert(!delivery_accept(&d,30,a,0));assert(delivery_timed_out(&d,35000001));
 // Reconnect resets transport ownership without removing pending records.
 event_buffer_reconnect();assert(event_buffer_get_count()==32);
 event_buffer_init();pthread_t p[2],c[2];atomic_store(&done,0);
 for(int i=0;i<2;i++)pthread_create(&c[i],NULL,consumer,NULL);
 for(int i=0;i<2;i++)pthread_create(&p[i],NULL,producer,(void*)(uintptr_t)(i*10000+1));
 for(int i=0;i<2;i++)pthread_join(p[i],NULL);
 for(int i=0;i<2;i++)pthread_join(c[i],NULL);
 event_stats_t s=event_buffer_stats();assert(s.created==2000&&s.completed+s.evicted+s.rejected==s.created&&event_buffer_get_count()==0);
 puts("WP02 claim/identity, INFO-complete vs FIRE-insert, full/duplicates/concurrent producers-consumers, out-of-order/duplicate/old-epoch ACK, ID reuse quarantine, in-flight cap: PASS");
}
