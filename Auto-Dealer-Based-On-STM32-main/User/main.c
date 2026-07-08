#include "stm32f10x.h"
#include "OLED.h"
#include "ESP8266.h"
#include "Serial.h"
#include "DHT11.h"
#include "LED.h"
#include "KEY.h"
#include "BEEP.h"
#include "Upload.h"
#include <string.h>
#include <stdio.h>

/* DHT11 全局变量（定义在 DHT11.c） */
extern u8 humi, temp, dechumi, dectemp;

/* ---- 商品数据 ---- */
/* inventory/price: 8 个槽，偶数索引=商品 */
u8 inventory[16] = {10,0, 10,0, 10,0, 10,0,  9,0, 10,0, 10,0, 10,0};
u8 price[16]     = { 2,0,  1,0,  2,0,  3,0,  4,0,  1,0,  1,0,  4,0};

/* ---- 保存库存到 Flash（每次变更后调用）---- */
static void save_inventory(void)
{
    FlashStore_Save(inventory, price);
}

/* ---- 加载库存：优先从 Flash 读取，失败则用默认值 ---- */
static void load_inventory(void)
{
    if (!FlashStore_Load(inventory, price))
    {
        /* Flash 为空或损坏，写入默认值 */
        FlashStore_Save(inventory, price);
    }
}

/* ---- 延时（约等于 ms）---- */
static void dms(volatile u32 ms)
{
    while (ms--) { volatile u32 n = 2000; while (n--) __NOP(); }
}

/* ---- OLED 快捷显示 ---- */
static void show(const char* l1, const char* l2)
{
    OLED_Clear();
    OLED_ShowString(0, 0,  (u8*)l1, 16, 1);
    OLED_ShowString(0, 16, (u8*)l2, 16, 1);
    OLED_Refresh();
}

/* ---- 错误死循环 ---- */
static void die(const char* m)
{
    show("FAIL", m);
    while (1) { __NOP(); }
}

/* 商品名 */
static const char* names[] = {"Apple","Banana","Orange","Mango"};

int main(void)
{
    /* ---- 状态变量 ---- */
    int8_t  page    = 0;    /* 0=主页 1=选商品 2=选数量 4=出货 */
    int8_t  prev    = -1;   /* 上一页，用于判断刷新 */
    int8_t  sel     = 0;    /* 当前选中商品索引 (0,2,4,6,8,10,12,14) */
    int8_t  cnt     = 0;    /* 购买数量 */
    int8_t  remote  = 0;    /* 0=本地按键出货 1=云端远程出货（需回复服务调用） */
    u8      i;
    u16     tick = 0;
    char    buf[32], json[320];

    /* ---- 初始化硬件 ---- */
    OLED_Init();
    OLED_Clear();
    OLED_ShowString(0, 0, "Vending...", 16, 1);
    OLED_Refresh();

    /* 从 Flash 恢复上次的库存数据 */
    load_inventory();

    Key_Init();
    LED_Init();
    DHT11_Init();
    Mortor_Init();
    Beep_Init();

    /* ---- WiFi + MQTT ---- */
    show("WiFi", "init...");
    {
        int r = ESP8266_Init();
        if (r <= 0) {
            sprintf(buf, "ERR=%d", r);
            die(buf);
        }
    }

    show("WiFi+MQTT", "OK!");
    dms(5000);  /* 等 TCP/IP 栈就绪 */

    /* 定时上传 */
    TIM3IT_Init();

    /* 初始化 LED 状态：有货的通道亮灯 */
    LED_UpdateAll(inventory);

    /* ======== 主循环 ======== */
    while (1)
    {
        tick++;

        /* ---- 传感器（关串口中断，防 DHT11 时序被干扰）---- */
        if (tick % 20 == 0) {
            USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);
            DHT11_Read_data((u8*)&humi, (u8*)&temp, &dechumi, &dectemp);
            USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
        }

        /* ---- 告警：高温 / 高湿 ---- */
        if (temp > 30 || humi > 90)
            BEEP_Alert();

        /* ---- 云端命令 ---- */
        {
            int8_t cmd = ESP8266_CheckCmd();
            if (cmd != -1)
            {
                if (page == 0)
                {
                    if (inventory[cmd] > 0) {
                        sel = cmd;
                        cnt = 1;
                        remote = 1;             /* 遥控出货，需回报结果 */
                        page = 4;
                    } else {
                        /* 库存为零，回复服务调用失败 */
                        ESP8266_ServiceReply(1);
                    }
                }
                else
                {
                    /* 设备忙，无法处理，清除待回复 ID */
                    ESP8266_ClearPendingSvc();
                }
            }
            else
            {
                /* 库存改写: SET_X_N  e.g. SET_0_15 */
                u8 new_qty;
                int8_t idx = ESP8266_ParseInventory(&new_qty);
                if (idx >= 0 && idx < 16) {
                    inventory[idx] = new_qty;
                    LED_UpdateAll(inventory);   /* 云端改库存后刷新 LED */
                    save_inventory();           /* 持久化到 Flash */
                }
            }
        }

        /* ---- 按键处理 ---- */
        /* PB12: 上一个 */
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_12) == 0)
        {
            dms(20); while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_12) == 0);
            if (page == 1) { sel += 2; if (sel > 14) sel = 0; }
        }
        /* PB13: 下一个 */
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) == 0)
        {
            dms(20); while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_13) == 0);
            if (page == 1) { sel -= 2; if (sel < 0) sel = 14; }
        }
        /* PB1: 返回 / 进入选择 */
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0)
        {
            dms(20); while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_1) == 0);
            if (page == 0)      { page = 1; sel = 0; }      /* 进入选择 */
            else if (page == 1) { page = 0; }                /* 返回主页 */
            else if (page == 2) { page = 1; cnt = 0; }       /* 返回选商品 */
        }
        /* PB0: 确认 */
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0) == 0)
        {
            dms(20); while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_0) == 0);
            if (page == 1 && inventory[sel] > 0)
                { page = 2; cnt = 1; }                       /* 进入选数量 */
            else if (page == 2 && cnt > 0)
                { page = 4; }                                /* 确认购买 */
        }
        /* PB11: 减 */
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 0)
        {
            dms(20); while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_11) == 0);
            if (page == 2) { if (--cnt < 1) cnt = 1; }
        }
        /* PB10: 加 */
        if (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_10) == 0)
        {
            dms(20); while (GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_10) == 0);
            if (page == 2 && cnt < inventory[sel]) cnt++;
        }

        /* ---- 界面渲染（仅页面切换时全刷新）---- */
        if (page != prev) {
            prev = page;
            OLED_Clear();

            if (page == 0)
            {
                OLED_ShowString(0,  0, "Temp:",   16, 1);
                OLED_ShowString(0, 16, "Humi:",   16, 1);
                OLED_ShowString(0, 32, "Press K1", 16, 1);
                OLED_ShowString(0, 48, "to buy",   16, 1);
            }
            else if (page == 1)
            {
                /* 商品列表页 —— 只画静态标签 */
            }
            else if (page == 2)
            {
                OLED_ShowString(0,  0, "Item:",   16, 1);
                OLED_ShowString(0, 16, "Count:",  16, 1);
                OLED_ShowString(0, 32, "Total $", 16, 1);
                OLED_ShowString(0, 48, "K0=OK K1=Back", 16, 1);
            }
            else if (page == 4)
            {
                OLED_ShowString(0,  0, "Dispensing...", 16, 1);
                OLED_Refresh();

                /* 1. 先扣库存 + 持久化（业务逻辑不应依赖硬件） */
                inventory[sel] -= cnt;
                LED_UpdateAll(inventory);
                save_inventory();
                upload = 1;              /* 立即上传最新库存到 OneNET */

                /* 2. 云端远程出货（暂不回复服务调用，避免 tcp_send 干扰 MQTT 上传链路） */
                if (remote) {
                    /* ESP8266_ServiceReply(0);  —— 暂屏蔽，防止破坏 MQTT 连接 */
                    remote = 0;
                }

                /* 3. 最后转动电机（硬件动作，放最后不影响业务逻辑） */
                Mortor_Turn();

                page = 0;
                sel  = 0;
                cnt  = 0;
                prev = -1;  /* 强制下次全刷新 */
            }
        }

        /* 动态数据更新（每次循环都刷新，不清屏） */
        if (page == 0) {
            OLED_ShowNum(48, 0,  temp, 2, 16, 1);
            OLED_ShowString(64, 0, "C", 16, 1);
            OLED_ShowNum(48, 16, humi, 2, 16, 1);
            OLED_ShowString(64,16, "%", 16, 1);
        }
        else if (page == 1) {
            for (i = 0; i < 4; i++) {
                u8 idx = i * 2, row = i * 16;
                u8 hl  = (idx == sel) ? 0 : 1;
                char line[17];
                int j, k = 0;
                /* 手工拼字符串 —— MicroLIB 不支持 %-8s */
                {
                    const char* s = names[i];
                    while (*s) line[k++] = *s++;
                }
                while (k < 9)  line[k++] = ' ';
                line[k++] = '$';
                line[k++] = '0' + price[idx];
                line[k++] = ' ';
                line[k++] = 'x';
                {
                    u8 q = inventory[idx];
                    if (q >= 10) { line[k++] = '0' + q/10; line[k++] = '0' + q%10; }
                    else         { line[k++] = '0' + q;    line[k++] = ' '; }
                }
                while (k < 16) line[k++] = ' ';
                line[16] = '\0';
                OLED_ShowString(0, row, (u8*)line, 16, hl);
            }
        }
        else if (page == 2) {
            OLED_ShowNum(48, 0,  sel,               2, 16, 1);
            OLED_ShowNum(56,16,  cnt,               2, 16, 1);
            OLED_ShowNum(64,32,  cnt * price[sel],  2, 16, 1);
        }

        /* ---- 定时上传数据到 OneNET ---- */
        if (upload == 1)
        {
            upload = 0;
            sprintf(json,
                "{\"id\":\"0\",\"version\":\"1.0\",\"params\":{"
                "\"temp\":{\"value\":%d},"
                "\"humi\":{\"value\":%d},"
                "\"Apple\":{\"value\":%d},"
                "\"Banana\":{\"value\":%d},"
                "\"Orange\":{\"value\":%d},"
                "\"Mango\":{\"value\":%d}"
                "}}",
                (int)temp, (int)humi,
                (int)inventory[0], (int)inventory[2],
                (int)inventory[4], (int)inventory[6]);

            ESP8266_UploadData(json);
        }

        OLED_Refresh();
        dms(50);  /* 主循环 ~20Hz */
    }
}
