/*******************************************************************************
 * boot_splash.h - 开机动画(头像 + "肚子隆起来了" + 名字水印)
 *
 * 数据见 boot_splash_data.c(由 tools/gen_splash_data.py 自动生成):
 *   g_face_bmp : 头像 32x32 单色位图(128 字节, 行优先 MSB 在前)
 *   g_cn_duzi  : "肚子隆起来了" 6 字 16x16 字模(192 字节)
 *   g_cn_dfq   : "达芬奇w" 4 字符 12x12 字模(96 字节)
 ******************************************************************************/
#ifndef __BOOT_SPLASH_H
#define __BOOT_SPLASH_H

#include "stm32f10x.h"

#define BOOT_FACE_W    32
#define BOOT_FACE_H    32
#define BOOT_DUZI_NUM  6      /* "肚子隆起来了" */
#define BOOT_DFQ_NUM   4      /* "达芬奇w" */

/* 开机动画: 画完停留后清屏(校准界面会接着覆盖) */
void Boot_Splash(void);

#endif /* __BOOT_SPLASH_H */
