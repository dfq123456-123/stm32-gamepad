/*******************************************************************************
 * adc.h - F103 6 路 ADC 驱动 (ADC1, PA0~PA5)
 *
 * 硬件映射 (已通过 PCB 实测 + LCSC 官方 pinout 交叉确证):
 *   PA0 = L-VR1X  左摇杆 X  (ADC1_IN0)
 *   PA1 = L-VR1Y  左摇杆 Y  (ADC1_IN1)
 *   PA2 = R-VR2X  右摇杆 X  (ADC1_IN2)
 *   PA3 = R-VR2Y  右摇杆 Y  (ADC1_IN3)
 *   PA4 = LT      左扳机    (ADC1_IN4)
 *   PA5 = RT      右扳机    (ADC1_IN5)
 *
 * F4 -> F103 移植要点:
 *  1. 时钟: RCC_AHB1PeriphClockCmd -> RCC_APB2PeriphClockCmd (ADC1 在 APB2)
 *  2. F4 的 ADC_CommonInit 在 F103 不存在, 只用单个 ADC_Init 结构体
 *  3. ADC 时钟分频: RCC_ADCCLKConfig(RCC_PCLK2_Div6) -> 72/6 = 12MHz
 *  4. 单通道轮询模式 (与 day11/joystick 的 ADC_GetValue 行为一致)
 ******************************************************************************/
#ifndef __ADC_H
#define __ADC_H

#include "stm32f10x.h"

/* ADC 通道宏: 与硬件引脚一一对应 */
#define ADC_CH_L_VRX   0   /* PA0  左摇杆 X */
#define ADC_CH_L_VRY   1   /* PA1  左摇杆 Y */
#define ADC_CH_R_VRX   2   /* PA2  右摇杆 X */
#define ADC_CH_R_VRY   3   /* PA3  右摇杆 Y */
#define ADC_CH_LT      4   /* PA4  左扳机   */
#define ADC_CH_RT      5   /* PA5  右扳机   */

#define ADC_CH_NUM     6   /* 通道总数 */

/* 初始化 ADC1: 独立模式, 单次转换, 右对齐, 12bit */
void MY_ADC_Init(void);

/* 读取单个通道原始值 (0~4095), ch 用上面的 ADC_CH_xxx 宏 */
uint16_t ADC_GetValue(uint8_t ch);

/* 一次读取全部 6 路, 存入 buf[6] (顺序: L-VRX, L-VRY, R-VRX, R-VRY, LT, RT) */
void ADC_ReadAll(uint16_t *buf);

#endif /* __ADC_H */
