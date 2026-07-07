#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>
#include "Serial.h"
#include "LED.h"

static void NopDelayMs(volatile u32 ms)
{
    while (ms--) {
        volatile u32 n = 2000;
        while (n--) __NOP();
    }
}

/* WiFi */
const char* WIFI_SSID     = "IQOO12";
const char* WIFI_PASSWORD = "zuoxiaobo1126";

/* OneNET Studio MQTT */
const char* ProductID  = "gtx9E8EU09";
const char* DeviceName = "demo";
const char* DeviceKey  = "dUdWeUJscW16S3k2MVBpZE5HbzlNS3hZQzZyZE0xclU=";

/* MQTT server */
const char* MQTT_BROKER = "mqtts.heclouds.com";
const int   MQTT_PORT   = 1883;

static char Topic_CmdReq[128];
static char Topic_DataUp[128];

void ESP8266_Init(void)
{
    int timeout;

    Serial_Init();

    /* 1. WiFi mode = Station */
    memset(Serial_RxPacket, 0, 250);
    Serial_Printf("AT+CWMODE=1\r\n");
    NopDelayMs(1000);

    /* 2. Disconnect previous WiFi first */
    memset(Serial_RxPacket, 0, 250);
    Serial_Printf("AT+CWQAP\r\n");
    NopDelayMs(500);

    /* 3. Connect to AP */
    memset(Serial_RxPacket, 0, 250);
    Serial_Printf("AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);

    /* Wait up to 15 seconds for "GOT IP" or "OK" or "FAIL" */
    timeout = 0;
    while (timeout < 200) {
        NopDelayMs(100);
        timeout++;
        if (strstr((const char*)Serial_RxPacket, "GOT IP") ||
            strstr((const char*)Serial_RxPacket, "WIFI CONNECTED"))
            break;
        if (strstr((const char*)Serial_RxPacket, "FAIL"))
            break;
    }
    memset(Serial_RxPacket, 0, 250);
    NopDelayMs(1000);

    /* Continue with MQTT only if WiFi connected */
    if (timeout >= 150) return;  // WiFi failed, skip MQTT

    /* 3. MQTT user config */
    memset(Serial_RxPacket, 0, 250);
    Serial_Printf("AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
           DeviceName, ProductID, DeviceKey);
    NopDelayMs(2000);

    /* 4. MQTT connection config */
    memset(Serial_RxPacket, 0, 250);
    Serial_Printf("AT+MQTTCONNCFG=0,0,120,\"%s\",%d,0\r\n",
           MQTT_BROKER, MQTT_PORT);
    NopDelayMs(2000);

    /* 5. Connect MQTT broker */
    memset(Serial_RxPacket, 0, 250);
    Serial_Printf("AT+MQTTCONN=0\r\n");
    NopDelayMs(5000);

    /* 6. Build topics */
    sprintf(Topic_CmdReq, "$sys/%s/%s/cmd/request/+",
            ProductID, DeviceName);
    sprintf(Topic_DataUp, "$sys/%s/%s/dp/post/json",
            ProductID, DeviceName);

    /* 7. Subscribe command topic */
    memset(Serial_RxPacket, 0, 250);
    Serial_Printf("AT+MQTTSUB=0,\"%s\",1\r\n", Topic_CmdReq);
    NopDelayMs(2000);
}

void ESP8266_UploadData(const char* json)
{
    memset(Serial_RxPacket, 0, 250);
    Serial_Printf("AT+MQTTPUB=0,\"%s\",\"%s\",1,0\r\n", Topic_DataUp, json);
    NopDelayMs(200);
}

int8_t ESP8266_CheckCmd(void)
{
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_0") != NULL)
    { memset(Serial_RxPacket, 0, 250); return 0; }
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_2") != NULL)
    { memset(Serial_RxPacket, 0, 250); return 2; }
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_4") != NULL)
    { memset(Serial_RxPacket, 0, 250); return 4; }
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_6") != NULL)
    { memset(Serial_RxPacket, 0, 250); return 6; }
    return -1;
}
