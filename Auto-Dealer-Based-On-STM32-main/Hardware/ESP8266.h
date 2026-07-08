#ifndef __ESP8266_H
#define __ESP8266_H
#include "stm32f10x.h"                  // Device header

int ESP8266_Init(void);
int ESP8266_UploadData(const char* json);
int8_t ESP8266_CheckCmd(void);
int8_t ESP8266_ParseInventory(u8* qty_out);
void  ESP8266_FeedCmdBuf(void);         /* 从串口缓冲区提取 +IPD 数据 */

#endif
