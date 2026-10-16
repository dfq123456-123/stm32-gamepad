//usart.h - F103 串口1 驱动 (PA9=TX, PA10=RX)
#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"
#include <stdio.h>
#include "menu.h"	 /* BT_CONFIG */

extern volatile uint8_t bt_recv;

void MY_USART1_Init(uint32_t baud);
void USART1_SendByte(uint8_t ch);
void USART1_SendString(uint8_t *str);
void USART1_SendBuffer(uint8_t *buf, uint16_t len);
void BT_SendFrame(uint16_t axis[6], uint16_t btn_bitmap, BT_CONFIG *cfg);

#endif	//__USART_H
