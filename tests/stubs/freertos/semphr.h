#pragma once
typedef int SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void){return 1;}
static inline int xSemaphoreTake(SemaphoreHandle_t s,int t){(void)s;(void)t;return 1;}
static inline int xSemaphoreGive(SemaphoreHandle_t s){(void)s;return 1;}
