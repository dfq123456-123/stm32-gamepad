/*******************************************************************************
 * flash_save.c - 参数断电保存实现(STM32F103 内部 Flash 寄存器级操作)
 *
 * Flash 布局(STM32F103C8, 64KB, 1KB/页):
 *   page 63 = 0x0800FC00 ~ 0x0800FFFF  ← 参数存储区
 *
 * 写入流程: 解锁 → 擦除整页 → 按半字(16bit)写入 → 锁定
 *   F103 Flash 编程必须按半字(16bit)对齐写入, 不能单字节写。
 ******************************************************************************/
#include "flash_save.h"
#include "stm32f10x.h"

#define SAVE_PAGE_ADDR   0x0800FC00u   /* 最后一页起始地址 */
#define SAVE_MAGIC       0x53415645u   /* "SAVE" ASCII, 小端: 45 56 41 53 */

/*---------------------- Flash 寄存器级操作 ----------------------*/

/* 解锁 Flash: 向 KEYR 写两个魔数序列 */
static void flash_unlock(void)
{
    FLASH->KEYR = 0x45670123;
    FLASH->KEYR = 0xCDEF89AB;
}

/* 锁定 Flash(写完后恢复只读) */
static void flash_lock(void)
{
    FLASH->CR |= FLASH_CR_LOCK;
}

/* 等待 Flash 操作完成(检查 BSY 位) */
static void flash_wait_busy(void)
{
    while (FLASH->SR & FLASH_SR_BSY) {}
}

/* 擦除一页: 置 PER + AR(页地址) + STRT, 等 BSY 清, 检查 EOP */
static void flash_erase_page(uint32_t addr)
{
    flash_wait_busy();
    FLASH->CR |=  FLASH_CR_PER;           /* 页擦除模式 */
    FLASH->AR  =  addr;                    /* 目标页地址 */
    FLASH->CR |=  FLASH_CR_STRT;          /* 启动擦除 */
    flash_wait_busy();
    FLASH->CR &= ~FLASH_CR_PER;           /* 清页擦除模式 */
    FLASH->SR  =  FLASH_SR_EOP;           /* 清 EOP 标志 */
}

/* 半字(16bit)编程: 置 PG, 写入半字, 等 BSY, 清 EOP */
static void flash_program_halfword(uint32_t addr, uint16_t data)
{
    flash_wait_busy();
    FLASH->CR |=  FLASH_CR_PG;            /* 编程模式 */
    *(volatile uint16_t *)addr = data;    /* 写半字 */
    flash_wait_busy();
    FLASH->CR &= ~FLASH_CR_PG;            /* 退出编程模式 */
    FLASH->SR  =  FLASH_SR_EOP;          /* 清 EOP */
}

/*---------------------- CRC16-CCITT(校验参数完整性) ----------------------*/
static uint16_t crc16(const uint8_t *buf, uint32_t len)
{
    uint16_t crc = 0xFFFF;
    uint32_t i;
    for (i = 0; i < len; i++)
    {
        uint8_t b = buf[i];
        uint8_t j;
        crc ^= (uint16_t)b << 8;
        for (j = 0; j < 8; j++)
        {
            if (crc & 0x8000) crc = (uint16_t)((crc << 1) ^ 0x1021);
            else               crc <<= 1;
        }
    }
    return crc;
}

/*---------------------- 对外接口 ----------------------*/

/* 读取参数: 返回 1=有效, 0=无效(首次/损坏/版本不符) */
uint8_t FlashSave_Load(PARAMS *p)
{
    const uint8_t *base = (const uint8_t *)SAVE_PAGE_ADDR;
    const uint8_t *src;
    uint8_t *dst;
    uint8_t i;
    uint16_t stored_crc;
    uint16_t calc_crc;
    uint32_t magic = *(const uint32_t *)base;
    uint8_t  ver   = base[4];

    /* 检查 magic 和版本 */
    if (magic != SAVE_MAGIC || ver != PARAM_VERSION)
    {
        return 0;   /* 无效, 调用者用默认值 */
    }

    /* 拷贝参数体(跳过 magic[0..3] + version[4]) */
    dst = (uint8_t *)p;
    src = base + 5;
    for (i = 0; i < sizeof(PARAMS); i++)
    {
        dst[i] = src[i];
    }

    /* 校验 CRC: 覆盖 magic(4) + version(1) + params(sizeof(PARAMS)) */
    stored_crc = *(const uint16_t *)(base + 5 + sizeof(PARAMS));
    calc_crc   = crc16(base, 5 + sizeof(PARAMS));
    if (stored_crc != calc_crc)
    {
        return 0;   /* CRC 不符, 数据损坏 */
    }

    return 1;   /* 有效 */
}

/* 保存参数到 Flash */
uint8_t FlashSave_Save(const PARAMS *p)
{
    uint16_t buf[512];   /* 1KB 页 = 512 半字 */
    uint8_t  raw[5 + sizeof(PARAMS) + 2];
    uint16_t crc;
    uint16_t i;

    /* 组装写入缓冲: magic(4) + version(1) + params + crc(2) */
    raw[0] = (uint8_t)(SAVE_MAGIC & 0xFF);
    raw[1] = (uint8_t)((SAVE_MAGIC >> 8) & 0xFF);
    raw[2] = (uint8_t)((SAVE_MAGIC >> 16) & 0xFF);
    raw[3] = (uint8_t)((SAVE_MAGIC >> 24) & 0xFF);
    raw[4] = PARAM_VERSION;
    {
        const uint8_t *src = (const uint8_t *)p;
        for (i = 0; i < sizeof(PARAMS); i++)
        {
            raw[5 + i] = src[i];
        }
    }
    crc = crc16(raw, 5 + sizeof(PARAMS));
    raw[5 + sizeof(PARAMS)]     = (uint8_t)(crc & 0xFF);
    raw[5 + sizeof(PARAMS) + 1] = (uint8_t)((crc >> 8) & 0xFF);

    /* 整页缓冲初始化为 0xFF(擦除态) */
    for (i = 0; i < 512; i++)
    {
        buf[i] = 0xFFFF;
    }

    /* 将 raw 字节按半字打包到 buf */
    {
        uint16_t total = 5 + sizeof(PARAMS) + 2;
        uint16_t lo, hi;
        for (i = 0; i < total; i += 2)
        {
            lo = raw[i];
            hi = (i + 1 < total) ? raw[i + 1] : 0xFF;
            buf[i / 2] = (uint16_t)(lo | (hi << 8));
        }
    }

    /* 执行: 解锁 → 擦除 → 编程 → 锁定 */
    flash_unlock();
    flash_erase_page(SAVE_PAGE_ADDR);

    for (i = 0; i < 512; i++)
    {
        if (buf[i] != 0xFFFF)   /* 只写非空半字, 减少磨损(全 0xFF 跳过) */
        {
            flash_program_halfword(SAVE_PAGE_ADDR + i * 2, buf[i]);
        }
    }

    flash_lock();
    return 1;
}
