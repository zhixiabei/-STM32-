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
static char MqttTopicBuf[256];          /* 保存最近收到的 MQTT topic */

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
static char Topic_CmdDown[128];       /* 订阅：接收属性设置 */
static char Topic_SvcInvoke[4][100];  /* 订阅：4 个 DISPENSE 服务调用 topic */
static u16  mqtt_packet_id = 1;

/* 最近一次服务调用的消息 ID 和槽位（异步回复用） */
static char   SvcMsgId[64];
static int8_t SvcSlot = -1;          /* 当前服务调用对应的货道号 */

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

/* ---- MQTT SUBSCRIBE —— 订阅指定主题 ---- */
static int mqtt_subscribe_topic(const char* topic)
{
    if (topic == NULL || *topic == '\0') return 0;

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

/* ---- 从 Serial_RxPacket 中提取 +IPD 数据到 MqttRxBuf ----
   MQTT 报文是二进制数据，不能当 C 字符串处理（topic 长度字段含 0x00）。
   改为按 +IPD,<len>: 中的长度精确拷贝 len 个字节，跳过 MQTT 头部，
   只把 JSON payload 部分复制到 MqttRxBuf，方便 strstr() 搜索命令。 */
void ESP8266_FeedCmdBuf(void)
{
    char* ipd;
    int   data_len;
    int   topic_len = 0;
    int   copy_start;
    int   copy_len;
    int   hdr_end = 0;        /* MQTT 固定头结束位置（save_raw 兜底也需要） */

    ipd = strstr((const char*)Serial_RxPacket, "+IPD,");
    if (!ipd) return;

    ipd += 5;                           /* 跳过 "+IPD," */

    /* 解析长度 */
    data_len = 0;
    while (*ipd >= '0' && *ipd <= '9') {
        data_len = data_len * 10 + (*ipd - '0');
        ipd++;
    }
    if (*ipd != ':' || data_len <= 0) {
        memset(Serial_RxPacket, 0, SERIAL_RX_SIZE);
        pRxPacket = 0;
        return;
    }
    ipd++;                              /* 跳过 ':' */
    if (data_len < 3) goto save_raw;    /* 太短，兜底 */

    /* 解析 MQTT 头部以跳过它，只保留 JSON payload */
    {
        int qos, has_pkt_id;
        hdr_end = 1;                /* type 之后 */

        /* QoS: 取自首字节 bit1-2; QoS>0 时 MQTT PUBLISH 有 2 字节 Packet ID */
        qos = (ipd[0] >> 1) & 0x03;
        has_pkt_id = (qos > 0) ? 2 : 0;

        /* 跳过 remaining length 变长编码 */
        while (hdr_end < data_len && (ipd[hdr_end] & 0x80)) hdr_end++;
        hdr_end++;                      /* 最后一个字节 */

        /* topic length 紧跟剩余长度之后（2 字节大端） */
        if (hdr_end + 2 > data_len) goto save_raw;
        topic_len = ((unsigned char)ipd[hdr_end] << 8) | (unsigned char)ipd[hdr_end + 1];
        if (topic_len <= 0 || topic_len > data_len) goto save_raw;

        /* 保存 topic（DISPENSE_X 命令标识在 topic 里，不在 payload 里） */
        {
            int tl_save = topic_len < 255 ? topic_len : 255;
            memcpy(MqttTopicBuf, ipd + hdr_end + 2, tl_save);
            MqttTopicBuf[tl_save] = '\0';
        }

        /* payload = 跳过 头 + topic_len字段(2) + topic + packetID */
        copy_start = hdr_end + 2 + topic_len + has_pkt_id;
        if (copy_start >= data_len) goto save_raw;
    }

    copy_len = data_len - copy_start;
    if (copy_len > CMD_BUF_SIZE - 1)
        copy_len = CMD_BUF_SIZE - 1;

    memcpy(MqttRxBuf, ipd + copy_start, copy_len);
    MqttRxBuf[copy_len] = '\0';
    pMqttRxBuf = copy_len;

    goto clear_rx;

save_raw:
    /* 兜底：直接拷贝整个 MQTT 报文（包含二进制头），由 CheckCmd 搜索 */
    copy_len = data_len;
    if (copy_len > CMD_BUF_SIZE - 1)
        copy_len = CMD_BUF_SIZE - 1;
    memcpy(MqttRxBuf, ipd, copy_len);
    MqttRxBuf[copy_len] = '\0';
    pMqttRxBuf = copy_len;

    /* 如果 topic 已成功解析，同样保存（DISPENSE 命令匹配需要 topic） */
    if (topic_len > 0 && (hdr_end + 2 + topic_len) <= data_len) {
        int tl_save = topic_len < 255 ? topic_len : 255;
        memcpy(MqttTopicBuf, ipd + hdr_end + 2, tl_save);
        MqttTopicBuf[tl_save] = '\0';
    }

clear_rx:
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
    sprintf(Topic_DataUp,    "$sys/%s/%s/thing/property/post",  PRODUCT_ID, DEVICE_NAME);
    sprintf(Topic_CmdDown,   "$sys/%s/%s/thing/property/set",   PRODUCT_ID, DEVICE_NAME);
    /* 显式订阅 4 个 per-service topic（不用通配符 #，避免被 OneNET 拒绝） */
    sprintf(Topic_SvcInvoke[0], "$sys/%s/%s/thing/service/DISPENSE_0/invoke", PRODUCT_ID, DEVICE_NAME);
    sprintf(Topic_SvcInvoke[1], "$sys/%s/%s/thing/service/DISPENSE_2/invoke", PRODUCT_ID, DEVICE_NAME);
    sprintf(Topic_SvcInvoke[2], "$sys/%s/%s/thing/service/DISPENSE_4/invoke", PRODUCT_ID, DEVICE_NAME);
    sprintf(Topic_SvcInvoke[3], "$sys/%s/%s/thing/service/DISPENSE_6/invoke", PRODUCT_ID, DEVICE_NAME);

    /* 清空待回复的服务调用 */
    SvcMsgId[0] = 0;
    SvcSlot     = -1;

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
        int svc_i;
        int svc_ok;
        memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0;

        /* 1. 订阅属性设置 */
        if (!mqtt_subscribe_topic(Topic_CmdDown)) goto sub_end;
        NopDelayMs(200);

        /* 2. 依次订阅 4 个 DISPENSE 服务调用 topic（不用通配符，确保 OneNET 接受）*/
        svc_ok = 1;
        for (svc_i = 0; svc_i < 4; svc_i++) {
            if (!mqtt_subscribe_topic(Topic_SvcInvoke[svc_i]))
                svc_ok = 0;
            NopDelayMs(150);
        }

        /* 即使个别服务订阅失败也标记完成，避免反复重试清空缓冲区 */
        sub_done = 1;
    sub_end:
        NopDelayMs(300);
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

/* ---- 从 JSON 中提取 "id" 字段值（用于服务调用异步回复）---- */
static void extract_msg_id(const char* buf, char* out, int out_len)
{
    const char *p, *q;
    /* 查找 "id":" */
    p = buf;
    while (*p) {
        if (p[0] == '"' && p[1] == 'i' && p[2] == 'd' && p[3] == '"' && p[4] == ':') {
            p += 5;
            /* 跳过可能存在的空白 */
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '"') {
                p++;  /* 跳过开头的双引号 */
                q = p;
                while (*q && *q != '"' && (q - p) < (out_len - 1)) q++;
                {
                    int len = (int)(q - p);
                    if (len > 0 && len < out_len) {
                        memcpy(out, p, len);
                        out[len] = '\0';
                        return;
                    }
                }
            }
        }
        p++;
    }
    /* 没找到 id 字段，置空 */
    if (out_len > 0) out[0] = '\0';
}

int8_t ESP8266_CheckCmd(void)
{
    char* found = NULL;
    int8_t slot = -1;

    /* 先从 MqttRxBuf 中提取 +IPD 数据 */
    ESP8266_FeedCmdBuf();

    /* 在 Topic 中搜索出货命令（DISPENSE_X 在 MQTT topic 里，不在 payload 里） */
    if      (strstr((const char*)MqttTopicBuf, "DISPENSE_0")) { found = MqttTopicBuf; slot = 0; }
    else if (strstr((const char*)MqttTopicBuf, "DISPENSE_2")) { found = MqttTopicBuf; slot = 2; }
    else if (strstr((const char*)MqttTopicBuf, "DISPENSE_4")) { found = MqttTopicBuf; slot = 4; }
    else if (strstr((const char*)MqttTopicBuf, "DISPENSE_6")) { found = MqttTopicBuf; slot = 6; }

    if (found) {
        /* 从 JSON payload 中提取 msgId，用于服务回复 */
        extract_msg_id((const char*)MqttRxBuf, SvcMsgId, sizeof(SvcMsgId));
        SvcSlot = slot;
        memset(MqttRxBuf, 0, CMD_BUF_SIZE); pMqttRxBuf = 0;
        memset(MqttTopicBuf, 0, sizeof(MqttTopicBuf));
        return slot;
    }

    /* 兜底：搜 Serial_RxPacket */
    {
        char* buf = (char*)Serial_RxPacket;
        found = NULL;
        slot  = -1;
        if      (strstr((const char*)buf, "DISPENSE_0")) { found = buf; slot = 0; }
        else if (strstr((const char*)buf, "DISPENSE_2")) { found = buf; slot = 2; }
        else if (strstr((const char*)buf, "DISPENSE_4")) { found = buf; slot = 4; }
        else if (strstr((const char*)buf, "DISPENSE_6")) { found = buf; slot = 6; }

        if (found) {
            extract_msg_id((const char*)Serial_RxPacket, SvcMsgId, sizeof(SvcMsgId));
            SvcSlot = slot;
            memset(Serial_RxPacket, 0, SERIAL_RX_SIZE); pRxPacket = 0;
            memset(MqttTopicBuf, 0, sizeof(MqttTopicBuf));
            return slot;
        }
    }

    return -1;
}


/* ---- 服务调用异步回复 ---- */
void ESP8266_ServiceReply(int code)
{
    char json[256];
    char reply_topic[128];

    if (SvcMsgId[0] == '\0') return;  /* 没有待回复的服务调用 */
    if (SvcSlot < 0)           return;  /* 没有记录货道号 */

    sprintf(json,
        "{\"id\":\"%s\",\"code\":%d,\"msg\":\"%s\"}",
        SvcMsgId,
        code,
        (code == 0) ? "ok" : "sold out");

    /* 按 OneNET 物模型 per-service 格式构造回复 topic:
       $sys/{pid}/{device-name}/thing/service/{identifier}/invoke_reply */
    sprintf(reply_topic, "$sys/%s/%s/thing/service/DISPENSE_%d/invoke_reply",
            PRODUCT_ID, DEVICE_NAME, (int)SvcSlot);

    mqtt_publish(reply_topic, json);

    /* 回复完成，清除 */
    SvcMsgId[0] = '\0';
    SvcSlot     = -1;
}

void ESP8266_ClearPendingSvc(void)
{
    SvcMsgId[0] = '\0';
    SvcSlot     = -1;
}

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
