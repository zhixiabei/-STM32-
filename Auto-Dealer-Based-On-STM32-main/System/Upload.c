#include "stm32f10x.h"                  // Device header
#include "DHT11.h"
#include "ESP8266.h"
#include "Serial.h"
#include <stdio.h>
#include <string.h>
#include <Delay.h>

u8 upload = 0;
extern u8 inventory[];


/* 定时器中断初始化 */
void TIM3IT_Init(void)
{
	/* 开启TIM时钟 */
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
	TIM_InternalClockConfig(TIM3);
	TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
	TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
	TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
	TIM_TimeBaseInitStructure.TIM_Period = 50000 - 1;
	TIM_TimeBaseInitStructure.TIM_Prescaler = 7200 - 1;
	TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
	TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);
	TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
	TIM_ClearFlag(TIM3, TIM_FLAG_Update);

	/* 配置NVIC优先级 */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStructure);

	TIM_Cmd(TIM3, ENABLE);
}


/**
 * 定时上传——把所有数据打包成一个 JSON，通过 MQTT 发到 OneNET Studio
 */
void DataUpload(void)
{
	if (upload == 1)
	{
		char json[256];
		sprintf(json,
			"{\"temp\":%.1f,\"humi\":%.1f,"
			"\"Apple\":%d,\"Banana\":%d,"
			"\"Orange\":%d,\"Mango\":%d}",
			(float)temp, (float)humi,
			inventory[0], inventory[2],
			inventory[4], inventory[6]);

		ESP8266_UploadData(json);
	}
	upload = 0;
}


void TIM3_IRQHandler(void)
{
	if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET)
	{
		upload = 1;
	}
	TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
}
