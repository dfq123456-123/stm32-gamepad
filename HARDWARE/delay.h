//delay.h  SysTick 毫秒/微秒延时(F103 @72MHz)

#ifndef __DELAY_H
#define __DELAY_H

#include "stm32f10x.h"

void delay_ms(uint32_t ms);
void delay_us(uint32_t us);

#endif
