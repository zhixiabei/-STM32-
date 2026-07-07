#include "stm32f10x.h"
#include "ESP8266.h"
#include "OLED.h"
#include "DHT11.h"
#include "LED.h"
#include "KEY.h"
#include "BEEP.h"
#include "Upload.h"
#include <string.h>

extern u8 OLED_GRAM[144][8];
u8 inventory[16] = {10,0,10,0,10,0,10,0,9,0,10,0,10,0,10,0};
u8 price[16]     = {2,0,1,0,2,0,3,0,4,0,1,0,1,0,4,0};

void OLED_List(u8 x, u8 SelectKey)
{
    u8 i, n;
    for (i = 0; i < 8; i++)
        for (n = 0; n < 128; n++)
            OLED_GRAM[n][i] = 0;
    for (int i = x, j = 0; i < 8 + x; i += 2, j += 16)
    {
        OLED_ShowCartChinese(0,  j, i,   16, SelectKey);
        OLED_ShowCartChinese(16, j, i+1, 16, SelectKey);
        OLED_ShowNum(48, j, price[i], 1, 16, 1);
        OLED_ShowChinese(56, j, 27, 16, 1);
        OLED_ShowNum(88, j, inventory[i], 2, 16, 1);
        OLED_ShowChinese(104, j, 28, 16, 1);
    }
}

static void Wait(volatile u32 n) { while (n--) __NOP(); }

int8_t  SelectKey = -2;
u8      x = 0;
int8_t  cart = 0;

int main(void)
{
    OLED_Init();
    OLED_Clear();
    OLED_ShowString(0, 0, "Vending...", 16, 1);
    OLED_Refresh();

    Key_Init();
    DHT11_Init();
    Mortor_Init();
    Beep_Init();
    ESP8266_Init();   // 最多 20s，连不上也继续
    TIM3IT_Init();

    OLED_ShowString(0, 16, "Ready!", 16, 1);
    OLED_Refresh();
    Wait(8000000);

    while (1)
    {
        /* 告警 */
        if (temp > 30 || humi > 90)
            BEEP_Alert();

        /* 云端命令 */
        int8_t cmd = ESP8266_CheckCmd();
        if (cmd != -1 && x == 0)
        {
            if (inventory[cmd] > 0)
            {
                SelectKey = cmd;
                cart = 1;
                x = 4;
            }
        }

        /* PB12: 上一个 */
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_12) == 0)
        {
            Wait(500000); while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_12) == 0);
            if (x == 1) SelectKey += 2;
            x = 1;
        }
        /* PB13: 下一个 */
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) == 0)
        {
            Wait(500000); while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) == 0);
            if (x == 1) SelectKey -= 2;
            x = 1;
        }
        /* PB1: 返回 */
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0)
        {
            Wait(500000); while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0);
            if (x > 0) x--;
        }
        /* PB0: 确认 */
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0) == 0)
        {
            Wait(500000); while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0) == 0);
            if (x == 1 && SelectKey >= 0 && inventory[SelectKey] != 0)
            { x = 2; cart = 0; }
            else if (x == 2 && cart != 0)
                x = 4;
        }
        /* PB11: 减购 */
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 0)
        {
            Wait(500000); while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 0);
            if (x != 2) cart = 0;
            else { if (--cart < 0) cart = 0; }
        }
        /* PB10: 加购 */
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_10) == 0)
        {
            Wait(500000); while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_10) == 0);
            if (x != 2) cart = 0;
            else if (cart < inventory[SelectKey]) cart++;
        }

        /* ---- 界面 ---- */
        if (x == 0)
        {
            u8 i, n;
            for (i = 0; i < 8; i++)
                for (n = 0; n < 128; n++)
                    OLED_GRAM[n][i] = 0;
            DHT11_Read_data((u8*)&humi, (u8*)&temp, &dechumi, &dectemp);
            OLED_ShowChinese(20,16,16,16,1);
            OLED_ShowChinese(36,16,17,16,1);
            OLED_ShowChinese(52,16,18,16,1);
            OLED_ShowChinese(68,16,19,16,1);
            OLED_ShowChinese(84,16,20,16,1);
            OLED_ShowChinese(20,32,21,16,1);
            OLED_ShowChinese(36,32,22,16,1);
            OLED_ShowChinese(50,32,23,16,1);
            OLED_ShowNum(66,32,temp,2,16,1);
            OLED_ShowChinese(20,48,24,16,1);
            OLED_ShowChinese(36,48,25,16,1);
            OLED_ShowChinese(50,48,26,16,1);
            OLED_ShowNum(66,48,humi,2,16,1);
        }
        if (x == 1)
        {
            if (SelectKey < 7) OLED_List(0, SelectKey);
            else if (SelectKey > 7 && SelectKey < 15) OLED_List(8, SelectKey);
            else SelectKey = -2;
            if (SelectKey < -2) SelectKey = 14;
        }
        if (x == 2)
        {
            u8 i, n;
            for (i = 0; i < 8; i++)
                for (n = 0; n < 128; n++)
                    OLED_GRAM[n][i] = 0;
            OLED_ShowChinese(0, 0, SelectKey, 16, 1);
            OLED_ShowChinese(16, 0, SelectKey+1, 16, 1);
            OLED_ShowNum(48, 0, cart, 2, 16, 1);
            OLED_ShowChinese(64, 0, 28, 16, 1);
            OLED_ShowNum(88, 0, cart * price[SelectKey], 2, 16, 1);
            OLED_ShowChinese(104, 0, 27, 16, 1);
        }
        if (x == 4)
        {
            OLED_Clear();
            OLED_ShowChinese(0, 0, 29, 16, 1);
            OLED_ShowChinese(16, 0, 30, 16, 1);
            OLED_ShowChinese(32, 0, 31, 16, 1);
            OLED_ShowString(48, 0, "...", 16, 1);
            OLED_Refresh();
            Mortor_Turn();
            inventory[SelectKey] -= cart;
            x = 0;
            SelectKey = -2;
        }

        DataUpload();
        OLED_Refresh();
        Wait(1000000);
    }
}
