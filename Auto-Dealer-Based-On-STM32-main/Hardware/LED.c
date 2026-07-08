#include "stm32f10x.h"                  // Device header
#include "Serial.h"
#include "String.h"
#include "stdio.h"
#include "Delay.h"

/*LEDʹ��*/
void LED_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA,ENABLE);
	GPIO_InitTypeDef GPIO_Initstructure;
	GPIO_Initstructure.GPIO_Mode = GPIO_Mode_Out_PP ;
	GPIO_Initstructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3
	                            | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_Initstructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA,&GPIO_Initstructure);
	GPIO_SetBits(GPIOA, GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 | GPIO_Pin_3
	                    | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7);	// 默认全灭(高电平)

}

void Mortor_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB,ENABLE);
	GPIO_InitTypeDef GPIO_Initstructure;
	GPIO_Initstructure.GPIO_Mode = GPIO_Mode_Out_PP ;
	GPIO_Initstructure.GPIO_Pin = GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_Initstructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB,&GPIO_Initstructure);
	GPIO_ResetBits(GPIOB,GPIO_Pin_7);	//Ĭ��ȫ��
	GPIO_ResetBits(GPIOB,GPIO_Pin_6);	//Ĭ��ȫ��
}
void Mortor_Turn(void)
{
	GPIO_SetBits(GPIOB,GPIO_Pin_6);	
	Delay_ms(2500);
	GPIO_ResetBits(GPIOB,GPIO_Pin_6);	
}
void LED0_ON(void)
{
	GPIO_ResetBits(GPIOA,GPIO_Pin_0);
}

void LED1_ON(void)
{
	GPIO_ResetBits(GPIOA,GPIO_Pin_1);
}

void LED2_ON(void)
{
	GPIO_ResetBits(GPIOA,GPIO_Pin_2);
}

void LED3_ON(void)
{
	GPIO_ResetBits(GPIOA,GPIO_Pin_3);
}
void LED4_ON(void)
{
	GPIO_ResetBits(GPIOA,GPIO_Pin_4);
}

void LED5_ON(void)
{
	GPIO_ResetBits(GPIOA,GPIO_Pin_5);
}

void LED6_ON(void)
{
	GPIO_ResetBits(GPIOA,GPIO_Pin_6);
}

void LED7_ON(void)
{
	GPIO_ResetBits(GPIOA,GPIO_Pin_7);
}


void LED1_Turn(void)
{
	/*���LED�ǵ͵�ƽ���͸���һ���෴���źţ��ߵ�ƽͬ��*/
	if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_1) == 0)
		GPIO_WriteBit(GPIOA,GPIO_Pin_1,Bit_SET);
	else
		GPIO_WriteBit(GPIOA,GPIO_Pin_1,Bit_RESET);
}

void LED2_Turn(void)
{
	if(GPIO_ReadInputDataBit(GPIOA,GPIO_Pin_2) == 0)
		GPIO_WriteBit(GPIOA,GPIO_Pin_2,Bit_SET);
	else
		GPIO_WriteBit(GPIOA,GPIO_Pin_2,Bit_RESET);
}



void LED0_OFF(void)
{
	GPIO_SetBits(GPIOA,GPIO_Pin_0);
}

void LED1_OFF(void)
{
	GPIO_SetBits(GPIOA,GPIO_Pin_1);
}

void LED2_OFF(void)
{
	GPIO_SetBits(GPIOA,GPIO_Pin_2);
}

void LED3_OFF(void)
{
	GPIO_SetBits(GPIOA,GPIO_Pin_3);
}
void LED4_OFF(void)
{
	GPIO_SetBits(GPIOA,GPIO_Pin_4);
}

void LED5_OFF(void)
{
	GPIO_SetBits(GPIOA,GPIO_Pin_5);
}

void LED6_OFF(void)
{
	GPIO_SetBits(GPIOA,GPIO_Pin_6);
}

void LED7_OFF(void)
{
	GPIO_SetBits(GPIOA,GPIO_Pin_7);
}

void LED_APPSwitch(void)
{
	if (strstr((const char*)Serial_RxPacket, "LED_ON"))			// 收到指令
		LED0_ON();
	if (strstr((const char*)Serial_RxPacket, "LED_OFF"))	// 收到指令
		LED0_OFF();
}

/* 根据库存数组更新所有 LED：有货亮，无货灭 */
void LED_UpdateAll(u8 *inventory)
{
	/* 8 个货道对应 GPIOA Pin 0~7，库存索引 0,2,4,6,8,10,12,14 */
	u8 i;
	for (i = 0; i < 8; i++)
	{
		if (inventory[i * 2] > 0)
			GPIO_ResetBits(GPIOA, (uint16_t)(1 << i));   // 有货 → LED 亮（低电平）
		else
			GPIO_SetBits(GPIOA,   (uint16_t)(1 << i));   // 无货 → LED 灭（高电平）
	}
}



