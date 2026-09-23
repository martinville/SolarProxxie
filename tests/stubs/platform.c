#include "nvs.h"
#include <string.h>
#include <stdint.h>
static uint8_t saved[32768];static size_t saved_len;
int test_fail_commit;
esp_err_t nvs_flash_init(void){return ESP_OK;}
esp_err_t nvs_flash_erase(void){saved_len=0;return ESP_OK;}
esp_err_t nvs_open(const char *s,int mode,nvs_handle_t *h){(void)s;(void)mode;*h=1;return ESP_OK;}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *k,void *p,size_t *n){(void)h;(void)k;if(!saved_len)return ESP_ERR_NVS_NOT_FOUND;if(p){if(*n<saved_len)return ESP_ERR_INVALID_ARG;memcpy(p,saved,saved_len);}*n=saved_len;return ESP_OK;}
esp_err_t nvs_set_blob(nvs_handle_t h,const char *k,const void *p,size_t n){(void)h;(void)k;if(test_fail_commit)return ESP_FAIL;if(n>sizeof(saved))return ESP_ERR_INVALID_ARG;memcpy(saved,p,n);saved_len=n;return ESP_OK;}
esp_err_t nvs_commit(nvs_handle_t h){(void)h;return ESP_OK;}
void esp_fill_random(void *out,size_t n){static uint32_t state=42;uint8_t *p=out;while(n--){state=1664525*state+1013904223;*p++=state>>24;}}
