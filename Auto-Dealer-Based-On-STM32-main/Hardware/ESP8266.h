#ifndef __ESP8266_H
#define __ESP8266_H
#include "stm32f10x.h"                  // Device header

int ESP8266_Init(void);
int ESP8266_UploadData(const char* json);
int8_t ESP8266_CheckCmd(void);
int8_t ESP8266_ParseInventory(u8* qty_out);
void  ESP8266_FeedCmdBuf(void);         /* 从串口缓冲区提取 +IPD 数据 */
int8_t ESP8266_HandlePropertySet(u8 *inventory); /* 处理 OneNET 属性设置并回复 */

/* 服务调用异步回复 */
void  ESP8266_ServiceReply(int code);   /* 回复最近一次服务调用, code: 0=成功 */
void  ESP8266_ClearPendingSvc(void);    /* 清除待回复的服务调用 */

#endif
