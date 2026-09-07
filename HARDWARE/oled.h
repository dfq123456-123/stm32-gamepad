/*******************************************************************************
 * oled.h - SSD1306 128x64 OLED 驱动(软件 I2C)
 *
 * 引脚: SCL = PB12, SDA = PB13 (芯片实际引脚; 板上丝印标为 PB8/PB9 易混淆, 已实测走线确认)
 * I2C 地址: 0x78 (0x3C << 1)
 *
 * 显示布局: 128x64 像素 = 8 page x 128 列
 *   ASCII 8x16: 4 行 x 16 字符
 *   中文 16x16: 4 行 x 8 汉字
 ******************************************************************************/
#ifndef __OLED_H
#define __OLED_H

#include "stm32f10x.h"

/*---------------------- 中文索引(与 font.h 的 CN_FONT 顺序一致) ----------------------*/
typedef enum
{
    CN_WO = 0,     /* 0  我 */
    CN_DE,         /* 1  的 */
    CN_YOU,        /* 2  又 */
    CN_SHAO,       /* 3  烧 */
    CN_LA,         /* 4  啦 */
    CN_BAN,        /* 5  版 */
    CN_BEN,        /* 6  本 */
    CN_YOU_XI,     /* 7  游 */
    CN_XI,         /* 8  戏 */
    CN_MO,         /* 9  模 */
    CN_SHI,        /* 10 式 */
    CN_LAN,        /* 11 蓝 */
    CN_YA,         /* 12 牙 */
    CN_SI,         /* 13 死 */
    CN_QU,         /* 14 区 */
    CN_LV,         /* 15 滤 */
    CN_BO,         /* 16 波 */
    CN_QIANG,      /* 17 强 */
    CN_DU,         /* 18 度 */
    CN_CAI,        /* 19 菜 */
    CN_DAN,        /* 20 单 */
    CN_BAO,        /* 21 包 */
    CN_TOU,        /* 22 头 */
    CN_WEI,        /* 23 尾 */
    CN_SHI2,       /* 24 十 */
    CN_LIU,        /* 25 六 */
    CN_JIN,        /* 26 进 */
    CN_ZHI,        /* 27 制 */
    CN_HUAN,       /* 28 换 */
    CN_XING,       /* 29 行 */
    CN_KAI,        /* 30 开 */
    CN_QI,         /* 31 启 */
    CN_GUAN,       /* 32 关 */
    CN_BI,         /* 33 闭 */
    CN_QUE,        /* 34 确 */
    CN_REN,        /* 35 认 */
    CN_FAN,        /* 36 返 */
    CN_HUI,        /* 37 回 */
    CN_JIAO,       /* 38 校 */
    CN_ZHUN,       /* 39 准 */
    CN_ZHONG,      /* 40 中 */
    CN_LIANG,      /* 41 亮 */
    CN_LIAN,       /* 42 连 */
    CN_FA,         /* 43 发 */
    CN_SU,         /* 44 速 */
    CN_PING,       /* 45 屏 */
    CN_MAX
} CN_INDEX;

/*---------------------- API ----------------------*/
void    OLED_GPIOInit(void);                            /* 仅初始化 I2C 引脚(探测前调用) */
uint8_t OLED_Probe(uint8_t addr);                       /* 探测地址是否应答: 1=有设备 */
void    OLED_SetAddr(uint8_t addr);                     /* 切换 I2C 写地址 */
uint8_t OLED_GetAddr(void);                             /* 当前 I2C 写地址 */
void OLED_Init(void);                                   /* 初始化 + 清屏 */
void OLED_Clear(void);                                  /* 清帧缓冲 */
void OLED_Refresh(void);                                /* 帧缓冲刷到屏 */
void OLED_ShowChar(uint8_t x, uint8_t line, char ch);   /* 8x16 ASCII, line=0~3 */
void OLED_ShowString(uint8_t x, uint8_t line, const char *str);
void OLED_ShowCN(uint8_t x, uint8_t line, uint8_t idx); /* 16x16 中文, line=0~3 */
void OLED_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t on);
void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t on);
void OLED_ClearRect(uint8_t x0, uint8_t y0, uint8_t w, uint8_t h);   /* 局部清缓冲 */
void OLED_RefreshRect(uint8_t x0, uint8_t y0, uint8_t w, uint8_t h); /* 局部刷新(流畅动画用) */
void OLED_ShowStringXY(uint8_t x, uint8_t y, const char *str);     /* 8x16 ASCII, 像素坐标(x,y) */

/* 亮度 / 显示开关(SSD1306 命令) */
void OLED_SetContrast(uint8_t val);   /* 对比度 0~255(=亮度), 写 0x81 + val */
void OLED_DisplayOff(void);          /* 关显示 0xAE(空闲熄屏用, 保留帧缓冲) */
void OLED_DisplayOn(void);           /* 开显示 0xAF */

#endif /* __OLED_H */
