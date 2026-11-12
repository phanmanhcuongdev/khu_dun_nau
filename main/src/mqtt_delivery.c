#include "mqtt_delivery.h"
#include <string.h>
void delivery_reset(delivery_t *d,uint32_t epoch){memset(d,0,sizeof(*d));d->epoch=epoch;}
bool delivery_accept(delivery_t *d,int mid,delivery_token_t t,int64_t now)
{
 if(mid<=0||mid>65535||d->count==DELIVERY_MAX||(d->used_mid[mid/8]&(1U<<(mid%8))))return false;
 for(int i=0;i<DELIVERY_MAX;i++)if(!d->slots[i].mid){d->slots[i]=(delivery_slot_t){mid,now,t};d->count++;d->used_mid[mid/8]|=1U<<(mid%8);return true;}
 return false;
}
bool delivery_ack(delivery_t *d,uint32_t epoch,int mid,delivery_token_t *out)
{
 if(epoch!=d->epoch)return false;
 for(int i=0;i<DELIVERY_MAX;i++)if(d->slots[i].mid==mid && mid>0){*out=d->slots[i].token;d->slots[i].mid=0;d->count--;return true;}
 return false;
}
bool delivery_contains(const delivery_t *d,const char *boot,uint64_t seq)
{for(int i=0;i<DELIVERY_MAX;i++)if(d->slots[i].mid&&d->slots[i].token.seq==seq&&!strcmp(d->slots[i].token.boot,boot))return true;return false;}
bool delivery_timed_out(const delivery_t *d,int64_t now)
{for(int i=0;i<DELIVERY_MAX;i++)if(d->slots[i].mid&&now-d->slots[i].accepted_us>35000000)return true;return false;}
