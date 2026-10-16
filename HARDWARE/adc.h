//adc.h - F103 6 路 ADC (ADC1, PA0~PA5)
//PA0=L-VRX PA1=L-VRY PA2=R-VRX PA3=R-VRY PA4=LT PA5=RT
#ifndef __ADC_H
#define __ADC_H

#include "stm32f10x.h"

#define ADC_CH_L_VRX   0	//PA0
#define ADC_CH_L_VRY   1	//PA1
#define ADC_CH_R_VRX   2	//PA2
#define ADC_CH_R_VRY   3	//PA3
#define ADC_CH_LT      4	//PA4
#define ADC_CH_RT      5	//PA5
#define ADC_CH_NUM     6

void MY_ADC_Init(void);
uint16_t ADC_GetValue(uint8_t ch);
void ADC_ReadAll(uint16_t *buf);

#endif	//__ADC_H
