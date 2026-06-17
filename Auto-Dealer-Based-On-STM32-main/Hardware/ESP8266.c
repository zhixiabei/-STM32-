#include "stm32f10x.h"                  // Device header
#include <string.h>
#include <stdio.h>
#include "Serial.h"
#include "Delay.h"
#include "LED.h"

/* WiFi */
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

/* OneNET Studio MQTT */
const char* ProductID  = "YOUR_PRODUCT_ID";     // 产品 ID
const char* DeviceName = "YOUR_DEVICE_NAME";           // 设备名称
const char* DeviceKey  = "YOUR_DEVICE_KEY";  // 设备密钥

/* MQTT 服务器（不用改） */
const char* MQTT_BROKER = "mqtts.heclouds.com";
const int   MQTT_PORT   = 1883;
/* ============================================================ */

/* MQTT topic 缓存 */
static char Topic_CmdReq[128];
static char Topic_DataUp[128];


/**
 * ESP8266 初始化
 * 1. 连接 WiFi
 * 2. 连接 OneNET Studio MQTT
 * 3. 订阅命令 topic
 */
void ESP8266_Init(void)
{
    Serial_Init();

    /* 1. 设置 WiFi 模式为 Station，连接路由器 */
    memset(Serial_RxPacket, 0, 250);
    printf("AT+CWMODE=1\r\n");
    Delay_ms(1000);

    memset(Serial_RxPacket, 0, 250);
    printf("AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);
    Delay_ms(8000);  // WiFi 连接需要时间

    /* 2. 等待获取 IP */
    while (1)
    {
        if (strstr((const char*)Serial_RxPacket, "WIFI GOT IP"))
        {
            memset(Serial_RxPacket, 0, 250);
            break;
        }
    }
    Delay_ms(2000);

    /* 3. 配置 MQTT 用户信息 */
    memset(Serial_RxPacket, 0, 250);
    printf("AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
           DeviceName, ProductID, DeviceKey);
    Delay_ms(2000);

    /* 4. 配置 MQTT 连接参数 */
    memset(Serial_RxPacket, 0, 250);
    printf("AT+MQTTCONNCFG=0,0,120,\"%s\",%d,0\r\n",
           MQTT_BROKER, MQTT_PORT);
    Delay_ms(2000);

    /* 5. 连接 MQTT broker */
    memset(Serial_RxPacket, 0, 250);
    printf("AT+MQTTCONN=0\r\n");
    Delay_ms(5000);

    /* 6. 构建 topic */
    sprintf(Topic_CmdReq, "$sys/%s/%s/cmd/request/+",
            ProductID, DeviceName);
    sprintf(Topic_DataUp, "$sys/%s/%s/dp/post/json",
            ProductID, DeviceName);

    /* 7. 订阅命令下发 topic */
    memset(Serial_RxPacket, 0, 250);
    printf("AT+MQTTSUB=0,\"%s\",1\r\n", Topic_CmdReq);
    Delay_ms(2000);
}


/**
 * 上传 JSON 数据到 OneNET Studio
 */
void ESP8266_UploadData(const char* json)
{
    memset(Serial_RxPacket, 0, 250);
    printf("AT+MQTTPUB=0,\"%s\",\"%s\",1,0\r\n", Topic_DataUp, json);
    Delay_ms(200);
}


/**
 * 检查是否收到出货命令（DISPENSE_0 / 2 / 4 / 6）
 * @return  商品通道号，未收到返回 -1
 */
int8_t ESP8266_CheckCmd(void)
{
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_0") != NULL)
    {
        memset(Serial_RxPacket, 0, 250);
        return 0;
    }
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_2") != NULL)
    {
        memset(Serial_RxPacket, 0, 250);
        return 2;
    }
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_4") != NULL)
    {
        memset(Serial_RxPacket, 0, 250);
        return 4;
    }
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_6") != NULL)
    {
        memset(Serial_RxPacket, 0, 250);
        return 6;
    }
    return -1;
}
