#ifndef __ESP8266_H
#define __ESP8266_H
#include "stm32f10x.h"                  // Device header

void ESP8266_Init(void);
void ESP8266_UploadData(const char* json);
int8_t ESP8266_CheckCmd(void);

#endif
