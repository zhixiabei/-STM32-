#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>
#include "Serial.h"
extern int pRxPacket;

static void NopDelayMs(volatile u32 ms)
{
    while (ms--) { volatile u32 n = 2000; while (n--) __NOP(); }
}

/* WiFi */
const char* WIFI_SSID     = "IQOO12";
const char* WIFI_PASSWORD = "zuoxiaobo1126";

/* OneNET */
const char* ProductID  = "gtx9E8EU09";
const char* DeviceName = "demo";
const char* DeviceKey  = "N1p2YmhBcVFzZ3BHWk5kcXNqeUhtcTFxSGJybndDREQ=";
const char* MQTT_BROKER = "mqtts.heclouds.com";
const int   MQTT_PORT   = 1883;

static char Topic_DataUp[128];

/* ---- TCP 发送：等 ">" 提示 → 发数据 → 等 SEND OK ---- */
static void tcp_send(u8* data, int len)
{
    volatile u32 w;
    memset(Serial_RxPacket, 0, 250); pRxPacket = 0;
    Serial_Printf("AT+CIPSEND=%d\r\n", len);
    for (w = 0; w < 6000000; w++) {
        __NOP();
        if (strstr((const char*)Serial_RxPacket, ">")) break;
    }
    for (int i = 0; i < len; i++) Serial_SendByte(data[i]);
    for (w = 0; w < 10000000; w++) {
        __NOP();
        if (strstr((const char*)Serial_RxPacket, "SEND OK")) break;
    }
}

/* ---- MQTT CONNECT ---- */
static void mqtt_connect(void)
{
    char *cid  = "demo";
    char *user = "gtx9E8EU09";
    char *pass = "N1p2YmhBcVFzZ3BHWk5kcXNqeUhtcTFxSGJybndDREQ=";

    int cl = strlen(cid), ul = strlen(user), pl = strlen(pass);
    int rl = 6 + 1 + 1 + 2 + 2 + cl + 2 + ul + 2 + pl;
    u8 pkt[256]; int pos = 0;

    pkt[pos++] = 0x10;  // CONNECT
    /* variable-length remaining length */
    int rem = rl;
    do { u8 d = rem % 128; rem /= 128; if (rem > 0) d |= 0x80; pkt[pos++] = d; } while (rem > 0);

    pkt[pos++]=0x00; pkt[pos++]=0x04;  // Protocol name length
    pkt[pos++]='M'; pkt[pos++]='Q'; pkt[pos++]='T'; pkt[pos++]='T';
    pkt[pos++]=0x04;                    // MQTT 3.1.1
    pkt[pos++]=0xC2;                    // username+password+clean session
    pkt[pos++]=0x00; pkt[pos++]=0x78;   // keepalive 120s

    pkt[pos++]=(cl>>8)&0xFF; pkt[pos++]=cl&0xFF;
    memcpy(pkt+pos, cid, cl); pos += cl;
    pkt[pos++]=(ul>>8)&0xFF; pkt[pos++]=ul&0xFF;
    memcpy(pkt+pos, user, ul); pos += ul;
    pkt[pos++]=(pl>>8)&0xFF; pkt[pos++]=pl&0xFF;
    memcpy(pkt+pos, pass, pl); pos += pl;

    tcp_send(pkt, pos);
}

/* ---- MQTT PUBLISH ---- */
static void mqtt_publish(const char* topic, const char* payload)
{
    int tl = strlen(topic), pl = strlen(payload);
    int rl = 2 + tl + pl + 2;  // topic(2+tl) + packetID(2) + payload(pl)
    u8 pkt[300]; int pos = 0;

    pkt[pos++] = 0x32;  // PUBLISH, QoS 1
    int rem = rl;
    do { u8 d = rem % 128; rem /= 128; if (rem > 0) d |= 0x80; pkt[pos++] = d; } while (rem > 0);

    pkt[pos++]=(tl>>8)&0xFF; pkt[pos++]=tl&0xFF;
    memcpy(pkt+pos, topic, tl); pos += tl;
    pkt[pos++]=0x00; pkt[pos++]=0x01;  // packet ID
    memcpy(pkt+pos, payload, pl); pos += pl;

    tcp_send(pkt, pos);
}

/* ---- 公开接口 ---- */

void ESP8266_Init(void)
{
    int timeout;
    Serial_Init();

    /* 1. CWMODE=1 */
    memset(Serial_RxPacket, 0, 250);
    Serial_Printf("AT+CWMODE=1\r\n");
    NopDelayMs(1000);

    /* 2. CWQAP */
    memset(Serial_RxPacket, 0, 250);
    Serial_Printf("AT+CWQAP\r\n");
    NopDelayMs(500);

    /* 3. CWJAP */
    memset(Serial_RxPacket, 0, 250);
    Serial_Printf("AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);

    timeout = 0;
    while (timeout < 200) {
        NopDelayMs(100); timeout++;
        if (strstr((const char*)Serial_RxPacket, "OK") ||
            strstr((const char*)Serial_RxPacket, "GOT IP")) break;
    }
    memset(Serial_RxPacket, 0, 250);
    NopDelayMs(1000);

    /* 4. TCP connect */
    memset(Serial_RxPacket, 0, 250);
    Serial_Printf("AT+CIPSTART=\"TCP\",\"%s\",%d\r\n", MQTT_BROKER, MQTT_PORT);
    NopDelayMs(5000);

    /* 5. MQTT CONNECT */
    mqtt_connect();

    /* 6. Build publish topic */
    sprintf(Topic_DataUp, "$sys/%s/%s/dp/post/json", ProductID, DeviceName);
}

void ESP8266_UploadData(const char* json)
{
    mqtt_publish(Topic_DataUp, json);
}

int8_t ESP8266_CheckCmd(void)
{
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_0"))
    { memset(Serial_RxPacket, 0, 250); return 0; }
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_2"))
    { memset(Serial_RxPacket, 0, 250); return 2; }
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_4"))
    { memset(Serial_RxPacket, 0, 250); return 4; }
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_6"))
    { memset(Serial_RxPacket, 0, 250); return 6; }
    return -1;
}
