#include <assert.h>
#include <stdio.h>
#include "../../main/src/critical_journal.c"
static uint8_t disk[4][HEADER+CLOUD_JSON_MAX],pending[HEADER+CLOUD_JSON_MAX];
static size_t sizes[4],pending_size;
static int target,operation,set_fail,commit_fail,read_fail,open_fail;
static unsigned writes;
esp_err_t nvs_open(const char *name,int mode,nvs_handle_t *h){(void)name;(void)mode;*h=1;return open_fail?ESP_FAIL:ESP_OK;}
static int idx(const char *k){return k[2]-'0';}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *key,void *p,size_t *n){(void)h;if(read_fail)return ESP_FAIL;int i=idx(key);if(!sizes[i])return ESP_ERR_NVS_NOT_FOUND;if(*n<sizes[i])return ESP_ERR_INVALID_SIZE;memcpy(p,disk[i],sizes[i]);*n=sizes[i];return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *key,const void *p,size_t n){(void)h;if(set_fail)return ESP_FAIL;target=idx(key);operation=1;pending_size=n;memcpy(pending,p,n);writes++;return ESP_OK;}
esp_err_t nvs_erase_key(nvs_handle_t h,const char *key){(void)h;target=idx(key);operation=2;return sizes[target]?ESP_OK:ESP_ERR_NVS_NOT_FOUND;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;if(commit_fail)return ESP_FAIL;if(operation==1){memcpy(disk[target],pending,pending_size);sizes[target]=pending_size;}else sizes[target]=0;return ESP_OK;}
void nvs_close(nvs_handle_t h){(void)h;}
int main(void){
 assert(critical_journal_init()==ESP_OK);cloud_record_t r={0};strcpy(r.boot_id,"0123456789abcdef0123456789abcdef");
 char payload[]={"{\"schema_version\":1,\"record_kind\":\"TRANSITION\"}"};
 for(int i=1;i<=4;i++){r.seq=i;assert(critical_journal_add(&r,payload)==ESP_OK);}
 unsigned old=writes;r.seq=1;assert(critical_journal_add(&r,payload)==ESP_OK&&writes==old);
 r.seq=5;assert(critical_journal_add(&r,payload)==ESP_ERR_NO_MEM&&critical_journal_losses()>0);
 critical_journal_complete(r.boot_id,2);assert(critical_journal_init()==ESP_OK);
 for(int i=0;i<4;i++)assert(!entries[i].used||entries[i].seq!=2);
 commit_fail=1;assert(critical_journal_add(&r,payload)!=ESP_OK);commit_fail=0;
 assert(critical_journal_init()==ESP_OK);for(int i=0;i<4;i++)assert(!entries[i].used||entries[i].seq!=5);
 assert(critical_journal_add(&r,payload)==ESP_OK);
 char out[CLOUD_JSON_MAX];for(int i=0;i<4;i++)if(entries[i].used){assert(critical_journal_read(i,out,sizeof(out))==ESP_OK&&!strcmp(out,payload));}
 // Corrupt metadata and payload, reject rather than completing an unrelated ID.
 disk[0][41]^=1;assert(critical_journal_init()==ESP_OK);assert(!entries[0].used);
 for(int i=0;i<4;i++)if(entries[i].used){uint64_t free_seq=entries[i].seq;critical_journal_complete(r.boot_id,free_seq);break;}
 set_fail=1;r.seq=6;assert(critical_journal_add(&r,payload)==ESP_FAIL);set_fail=0;
 read_fail=1;r.seq=7;assert(critical_journal_add(&r,payload)==ESP_FAIL);read_fail=0;
 assert(critical_journal_init()==ESP_OK);bool recovered_uncertain=false;for(int i=0;i<4;i++)if(entries[i].used&&entries[i].seq==7)recovered_uncertain=true;assert(recovered_uncertain);
 read_fail=1;assert(critical_journal_init()==ESP_OK);for(int i=0;i<4;i++)assert(!entries[i].used);read_fail=0;
 open_fail=1;assert(critical_journal_init()!=ESP_OK);assert(critical_journal_add(&r,payload)!=ESP_OK);
 puts("WP02 bounded four-slot journal, duplicate/no extra writes, reload/completion, capacity, CRC, set/commit/read/open failures: PASS (mock NVS)");
}
