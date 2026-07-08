#ifndef __UPLOAD_H
#define __UPLOAD_H
#include "stm32f10x.h"                  // Device header

void TIM3IT_Init(void);
extern u8 upload;

/* Flash 库存持久化 */
int  FlashStore_Load(u8 *inventory, u8 *price);
int  FlashStore_Save(u8 *inventory, u8 *price);

#endif
