#include "stm32f10x.h"
#include <string.h>
#include <stdio.h>
#include "Serial.h"
#include "secret.h"       /* WiFi + MQTT 凭据（不提交到 Git） */
extern volatile int pRxPacket;

/* ---- WiFi + MQTT 凭据（定义在 secret.h）---- */
/* WIFI_SSID / WIFI_PASSWORD / PRODUCT_ID / DEVICE_NAME / MQTT_PASS */
/* ---- MQTT 命令接收缓冲区（独立于 Serial_RxPacket，不会被上传操作清空）---- */
#define CMD_BUF_SIZE  512
char  MqttRxBuf[CMD_BUF_SIZE];
volatile int pMqttRxBuf = 0;

static void NopDelayMs(volatile u32 ms)
{
    while (ms--) { volatile u32 n = 2000; while (n--) __NOP(); }
}

const char* WIFI_SSID     = SECRET_WIFI_SSID;
const char* WIFI_PASSWORD = SECRET_WIFI_PASSWORD;

const char* MQTT_BROKER = "mqtts.heclouds.com";
const int   MQTT_PORT   = 8883;
const char* PRODUCT_ID  = SECRET_ONENET_PRODUCT_ID;
const char* DEVICE_NAME = SECRET_ONENET_DEVICE_NAME;
const char* MQTT_PASS   = SECRET_ONENET_MQTT_PASS;

static char Topic_DataUp[128];
static char Topic_CmdDown[128];       /* 订阅：接收云端命令 */
static u16  mqtt_packet_id = 1;

static int tcp_send(const u8* data, int len)
{
    int timeout, i;

    memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0; pRxPacket = 0;
    Serial_Printf("AT+CIPSEND=%d\r\n", len);

    timeout = 0;
    while (timeout < 50) {
        NopDelayMs(100); timeout++;
        if (strstr((const char*)Serial_RxPacket, ">"))     break;
        if (strstr((const char*)Serial_RxPacket, "ERROR")) break;
        if (strstr((const char*)Serial_RxPacket, "CLOSED"))break;
    }
    if (!strstr((const char*)Serial_RxPacket, ">"))
        return 0;

    for (i = 0; i < len; i++)
        Serial_SendByte(data[i]);

    timeout = 0;
    while (timeout < 80) {
        NopDelayMs(100); timeout++;
        if (strstr((const char*)Serial_RxPacket, "SEND OK")) break;
        if (strstr((const char*)Serial_RxPacket, "ERROR"))  break;
        if (strstr((const char*)Serial_RxPacket, "CLOSED")) break;
    }
    if (!strstr((const char*)Serial_RxPacket, "SEND OK"))
        return 0;

    return 1;
}

/* ---- 底层: 发送一条 MQTT 报文（不带 topic/payload 拼接）---- */
static int mqtt_send_raw(const u8* pkt, int len)
{
    return tcp_send(pkt, len);
}

static int mqtt_connect(void)
{
    const char *cid  = DEVICE_NAME;
    const char *user = PRODUCT_ID;
    const char *pass = MQTT_PASS;

    int cl = strlen(cid), ul = strlen(user), pl = strlen(pass);
    int rl = 6 + 1 + 1 + 2 + 2 + cl + 2 + ul + 2 + pl;
    u8 pkt[300]; int pos = 0;

    pkt[pos++] = 0x10;
    int rem = rl;
    do { u8 d = rem % 128; rem /= 128; if (rem > 0) d |= 0x80; pkt[pos++] = d; } while (rem > 0);

    pkt[pos++] = 0x00; pkt[pos++] = 0x04;
    pkt[pos++] = 'M'; pkt[pos++] = 'Q'; pkt[pos++] = 'T'; pkt[pos++] = 'T';
    pkt[pos++] = 0x04;
    pkt[pos++] = 0xC2;
    pkt[pos++] = 0x00; pkt[pos++] = 0x3C;

    pkt[pos++] = (cl >> 8) & 0xFF; pkt[pos++] = cl & 0xFF;
    memcpy(pkt + pos, cid, cl); pos += cl;
    pkt[pos++] = (ul >> 8) & 0xFF; pkt[pos++] = ul & 0xFF;
    memcpy(pkt + pos, user, ul); pos += ul;
    pkt[pos++] = (pl >> 8) & 0xFF; pkt[pos++] = pl & 0xFF;
    memcpy(pkt + pos, pass, pl); pos += pl;

    return tcp_send(pkt, pos);
}

/* ---- MQTT SUBSCRIBE —— 订阅 OneNET 属性设置主题 ---- */
static int mqtt_subscribe(void)
{
    const char* topic = Topic_CmdDown;
    int tl = strlen(topic);
    int rl = 2 + 2 + tl + 1;   /* PacketID(2) + TopicLen(2) + Topic + QoS(1) */
    u8 pkt[256]; int pos = 0;

    /* Fixed header: SUBSCRIBE = 0x82 */
    pkt[pos++] = 0x82;

    int rem = rl;
    do { u8 d = rem % 128; rem /= 128; if (rem > 0) d |= 0x80; pkt[pos++] = d; } while (rem > 0);

    /* Packet Identifier */
    pkt[pos++] = (mqtt_packet_id >> 8) & 0xFF;
    pkt[pos++] =  mqtt_packet_id       & 0xFF;
    mqtt_packet_id++;

    /* Topic */
    pkt[pos++] = (tl >> 8) & 0xFF;
    pkt[pos++] =  tl       & 0xFF;
    memcpy(pkt + pos, topic, tl); pos += tl;

    /* QoS = 1 */
    pkt[pos++] = 0x01;

    return mqtt_send_raw(pkt, pos);
}

static int mqtt_publish(const char* topic, const char* payload)
{
    int tl = strlen(topic), pl = strlen(payload);
    int rl = 2 + tl + pl;               /* QoS0 无 PacketID */
    u8 pkt[512]; int pos = 0;

    pkt[pos++] = 0x30;                  /* QoS0, RETAIN=0 */

    int rem = rl;
    do { u8 d = rem % 128; rem /= 128; if (rem > 0) d |= 0x80; pkt[pos++] = d; } while (rem > 0);

    pkt[pos++] = (tl >> 8) & 0xFF; pkt[pos++] = tl & 0xFF;
    memcpy(pkt + pos, topic, tl); pos += tl;
    memcpy(pkt + pos, payload, pl); pos += pl;

    return tcp_send(pkt, pos);
}

/* ---- 从 Serial_RxPacket 中提取 +IPD 数据到 MqttRxBuf ---- */
void ESP8266_FeedCmdBuf(void)
{
    char* ipd;
    char* start;

    ipd = strstr((const char*)Serial_RxPacket, "+IPD,");
    if (!ipd) return;

    /* 跳过 "+IPD," */
    ipd += 5;

    /* 跳过长度数字 */
    while (*ipd >= '0' && *ipd <= '9') ipd++;

    /* 跳过 ':' */
    if (*ipd != ':') {
        /* 格式不对，清掉这段避免重复扫描 */
        memset(Serial_RxPacket, 0, SERIAL_RX_SIZE);
        pRxPacket = 0;
        return;
    }
    ipd++;

    /* 复制 payload 到 MqttRxBuf（跳过 MQTT 头部，直接存原始字节）*/
    start = ipd;
    while (*ipd) {
        if (pMqttRxBuf >= CMD_BUF_SIZE - 1) {
            /* 缓冲区满，清空重来 */
            memset(MqttRxBuf, 0, CMD_BUF_SIZE);
            pMqttRxBuf = 0;
            break;
        }
        /* 遇到下一个 +IPD 或 CR/LF 就停 */
        if (*ipd == '\r' || *ipd == '\n') {
            ipd++;
            continue;
        }
        if (*ipd == '+' && ipd > start &&
            (*(ipd-1) == '\n' || *(ipd-1) == '\r')) {
            /* 下一个 +IPD 开始 */
            break;
        }
        MqttRxBuf[pMqttRxBuf++] = *ipd++;
    }
    MqttRxBuf[pMqttRxBuf] = '\0';

    /* 清除 Serial_RxPacket 中已提取的部分 */
    memset(Serial_RxPacket, 0, SERIAL_RX_SIZE);
    pRxPacket = 0;
}

int ESP8266_Init(void)
{
    int timeout;
    Serial_Init();

    /* 0. 测试 AT 通信 */
    memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0;
    Serial_Printf("AT\r\n");
    NopDelayMs(2000);
    if (!strstr((const char*)Serial_RxPacket, "OK"))
        return 0;   /* AT 通信超时 */

    /* 1. CWMODE=1 —— Station 模式 */
    memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0;
    Serial_Printf("AT+CWMODE=1\r\n");
    NopDelayMs(1000);
    if (!strstr((const char*)Serial_RxPacket, "OK"))
        return -1;  /* CWMODE 失败 */

    /* 2. CWQAP —— 断开已有连接 */
    memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0;
    Serial_Printf("AT+CWQAP\r\n");
    NopDelayMs(1000);

    /* 3. CWJAP —— 连接 WiFi，最长等待 20 秒               */
    memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0;
    Serial_Printf("AT+CWJAP=\"%s\",\"%s\"\r\n", WIFI_SSID, WIFI_PASSWORD);

    timeout = 0;
    while (timeout < 200) {
        NopDelayMs(100); timeout++;
        /* 通用失败: ERROR / FAIL */
        if (strstr((const char*)Serial_RxPacket, "ERROR") ||
            strstr((const char*)Serial_RxPacket, "FAIL"))
            return -2;
        /* 新固件失败码: +CWJAP:0(通用) / 2(超时) / 3(密码错) / 4(找不到) */
        if (strstr((const char*)Serial_RxPacket, "+CWJAP:0") ||
            strstr((const char*)Serial_RxPacket, "+CWJAP:2") ||
            strstr((const char*)Serial_RxPacket, "+CWJAP:3") ||
            strstr((const char*)Serial_RxPacket, "+CWJAP:4"))
            return -3;
        /* 成功标志 */
        if (strstr((const char*)Serial_RxPacket, "GOT IP") ||
            strstr((const char*)Serial_RxPacket, "+CWJAP:1") ||
            strstr((const char*)Serial_RxPacket, "ALREADY"))
            break;
        /* 老固件: 等 3 秒后只有 OK → 阻塞返回，连接成功 */
        if (timeout > 30 && strstr((const char*)Serial_RxPacket, "OK"))
            break;
    }
    if (timeout >= 200)
        return -4;  /* CWJAP 等待超时 */

    memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0;
    NopDelayMs(500);

    /* 构建数据上传和命令接收 Topic */
    sprintf(Topic_DataUp,  "$sys/%s/%s/thing/property/post", PRODUCT_ID, DEVICE_NAME);
    sprintf(Topic_CmdDown, "$sys/%s/%s/thing/property/set",  PRODUCT_ID, DEVICE_NAME);

    return 1;
}

int ESP8266_UploadData(const char* json)
{
    static int mqtt_online = 0;
    static int pub_count   = 0;
    static int sub_done    = 0;
    int timeout, ret = 0;
    int tcp_ok;

    /* 检测断线 —— 仅凭 CLOSED 判断，不再用 pub_count 强制定时断连 */
    if (mqtt_online) {
        pub_count++;
        if (strstr((const char*)Serial_RxPacket, "CLOSED")) {
            mqtt_online = 0;
            sub_done    = 0;
            Serial_Printf("AT+CIPCLOSE\r\n");
            NopDelayMs(1000);
        }
    }

    /* 建立连接 */
    if (!mqtt_online) {
        pub_count = 0;

        /* 等 ESP8266 TCP/IP 栈就绪 */
        NopDelayMs(2000);

        /* 建立 TCP/SSL */
        memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0;
        Serial_Printf("AT+CIPSTART=\"SSL\",\"%s\",%d\r\n", MQTT_BROKER, MQTT_PORT);

        timeout = 0;
        while (timeout < 80) {
            NopDelayMs(100); timeout++;
            if (strstr((const char*)Serial_RxPacket, "CONNECT")) break;
            if (strstr((const char*)Serial_RxPacket, "ERROR"))   break;
            if (strstr((const char*)Serial_RxPacket, "ALREADY")) break;
        }
        if (!strstr((const char*)Serial_RxPacket, "CONNECT") &&
            !strstr((const char*)Serial_RxPacket, "ALREADY"))
            return 1;

        /* MQTT CONNECT */
        tcp_ok = mqtt_connect();
        if (!tcp_ok) {
            mqtt_online = 0;
            return (!strstr((const char*)Serial_RxPacket, ">")) ? 2 : 3;
        }

        NopDelayMs(2000);
        if (strstr((const char*)Serial_RxPacket, "CLOSED")) {
            mqtt_online = 0;
            return 4;
        }

        mqtt_online = 1;
        sub_done    = 0;
    }

    /* ---- MQTT SUBSCRIBE（连接成功后做一次）---- */
    if (mqtt_online && !sub_done) {
        memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0;
        if (mqtt_subscribe()) {
            sub_done = 1;
            memset(MqttRxBuf, 0, CMD_BUF_SIZE);
            pMqttRxBuf = 0;
        }
        NopDelayMs(500);
    }

    /* PUBLISH */
    memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0;
    if (!mqtt_publish(Topic_DataUp, json)) {
        mqtt_online = 0;
        sub_done    = 0;
        ret = 5;
    }

    return ret;
}

int8_t ESP8266_CheckCmd(void)
{
    /* 先尝试从 MqttRxBuf 中提取 +IPD 数据 */
    ESP8266_FeedCmdBuf();

    /* 在 MqttRxBuf 中搜索出货命令 */
    if (strstr((const char*)MqttRxBuf, "DISPENSE_0"))
        { memset(MqttRxBuf, 0, CMD_BUF_SIZE); pMqttRxBuf = 0; return 0; }
    if (strstr((const char*)MqttRxBuf, "DISPENSE_2"))
        { memset(MqttRxBuf, 0, CMD_BUF_SIZE); pMqttRxBuf = 0; return 2; }
    if (strstr((const char*)MqttRxBuf, "DISPENSE_4"))
        { memset(MqttRxBuf, 0, CMD_BUF_SIZE); pMqttRxBuf = 0; return 4; }
    if (strstr((const char*)MqttRxBuf, "DISPENSE_6"))
        { memset(MqttRxBuf, 0, CMD_BUF_SIZE); pMqttRxBuf = 0; return 6; }

    /* 也搜一下 Serial_RxPacket（兜底，防止 +IPD 提取遗漏） */
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_0"))
        { memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0; return 0; }
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_2"))
        { memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0; return 2; }
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_4"))
        { memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0; return 4; }
    if (strstr((const char*)Serial_RxPacket, "DISPENSE_6"))
        { memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0; return 6; }

    return -1;
}

/* 解析 SET_X_N 命令：云端改写库存，返回商品索引(0-14)，-1 无命令 */
int8_t ESP8266_ParseInventory(u8* qty_out)
{
    char* p;
    char* found;

    /* 先尝试从 MqttRxBuf 中提取 +IPD 数据 */
    ESP8266_FeedCmdBuf();

    /* 优先搜索 MqttRxBuf */
    found = strstr((const char*)MqttRxBuf, "SET_");
    if (found) p = (char*)found;
    else {
        /* 兜底：搜索 Serial_RxPacket */
        found = strstr((const char*)Serial_RxPacket, "SET_");
        if (found) p = (char*)found;
        else return -1;
    }

    /* 跳过 "SET_" */
    p += 4;
    {
        int idx = 0;
        while (*p >= '0' && *p <= '9')
            { idx = idx * 10 + (*p - '0'); p++; }
        if (*p != '_') return -1;
        p++;  /* 跳过 _ */

        int val = 0;
        while (*p >= '0' && *p <= '9')
            { val = val * 10 + (*p - '0'); p++; }

        if (idx < 0 || idx > 14 || val < 0 || val > 99) return -1;

        *qty_out = (u8)val;

        /* 清除对应缓冲区中的命令 */
        if (found == (char*)MqttRxBuf)
            { memset(MqttRxBuf, 0, CMD_BUF_SIZE); pMqttRxBuf = 0; }
        else
            { memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0; }

        return (int8_t)idx;
    }
}
