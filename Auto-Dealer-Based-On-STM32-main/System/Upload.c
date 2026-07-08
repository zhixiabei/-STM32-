#include "stm32f10x.h"
#include "Upload.h"

u8 upload = 0;

/* 定时器 3 —— 每 5 秒触发一次上传 */
void TIM3IT_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    TIM_InternalClockConfig(TIM3);

    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period        = 50000 - 1;
    TIM_TimeBaseInitStructure.TIM_Prescaler     = 7200 - 1;
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);

    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel                   = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_Init(&NVIC_InitStructure);

    TIM_Cmd(TIM3, ENABLE);
}

void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET)
    {
        upload = 1;
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    }
}

/* ================================================================
 * Flash 存储：库存持久化到 STM32 内部 Flash 最后一页 (0x0800FC00)
 * ================================================================ */
#include <string.h>

#define FLASH_STORE_ADDR    0x0800FC00
#define FLASH_MAGIC         0xA55AA55A

typedef struct {
    uint32_t magic;
    uint8_t  inventory[16];
    uint8_t  price[16];
    uint32_t checksum;
} FlashStore;

static uint32_t flash_checksum(FlashStore *s)
{
    uint32_t cs = 0;
    uint8_t i;
    for (i = 0; i < 16; i++) {
        cs ^= (uint32_t)s->inventory[i] << ((i & 3) * 8);
        cs ^= (uint32_t)s->price[i]     << ((i & 3) * 8);
    }
    return cs;
}

int FlashStore_Load(u8 *inventory, u8 *price)
{
    FlashStore *store = (FlashStore*)FLASH_STORE_ADDR;
    if (store->magic != FLASH_MAGIC) return 0;
    if (store->checksum != flash_checksum(store)) return 0;
    memcpy(inventory, (void*)store->inventory, 16);
    memcpy(price,     (void*)store->price,     16);
    return 1;
}

int FlashStore_Save(u8 *inventory, u8 *price)
{
    FlashStore buf;
    uint32_t addr = FLASH_STORE_ADDR;
    uint16_t *p16 = (uint16_t*)&buf;
    int i, count = sizeof(FlashStore) / 2;
    FLASH_Status status;

    buf.magic = FLASH_MAGIC;
    memcpy(buf.inventory, inventory, 16);
    memcpy(buf.price,     price,     16);
    buf.checksum = flash_checksum(&buf);

    FLASH_Unlock();
    status = FLASH_ErasePage(FLASH_STORE_ADDR);
    if (status != FLASH_COMPLETE) { FLASH_Lock(); return 0; }

    for (i = 0; i < count; i++) {
        status = FLASH_ProgramHalfWord(addr, *p16);
        if (status != FLASH_COMPLETE) { FLASH_Lock(); return 0; }
        addr += 2; p16++;
    }

    FLASH_Lock();
    return 1;
}
