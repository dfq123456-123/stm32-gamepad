/*******************************************************************************
 * usart.h - F103 串口1 驱动 (PA9=TX, PA10=RX)
 *
 * 移植自 F4 参考工程 day11/joystick 与 day8/chuankou，API 风格保持一致:
 *   - MY_USART1_Init(baud)   初始化 USART1
 *   - USART1_SendByte(ch)    阻塞发送 1 字节
 *   - USART1_SendString(s)   发送字符串
 *   - USART1_IRQHandler      中断收, 存 bt_recv
 *   - printf 重定向到 USART1 (禁用半主机模式)
 *
 * 接线: 核心板 PA9(TX) -> 蓝牙/串口模块 RX
 *       核心板 PA10(RX)-> 蓝牙/串口模块 TX
 *       波特率默认 115200 (与 HC-05 配对后一致)
 ******************************************************************************/
#ifndef __USART_H
#define __USART_H

#include "stm32f10x.h"
#include <stdio.h>
#include "menu.h"      /* BT_CONFIG 蓝牙帧配置结构体 */

/* 串口接收缓冲(1字节，适合指令/单字节应答场景) */
extern volatile uint8_t bt_recv;

/* 初始化 USART1, baud=波特率 */
void MY_USART1_Init(uint32_t baud);

/* 发送 1 字节, 等待发送完成 */
void USART1_SendByte(uint8_t ch);

/* 发送字符串 (以 \0 结尾) */
void USART1_SendString(uint8_t *str);

/* 发送指定长度的原始数据 (用于数据帧) */
void USART1_SendBuffer(uint8_t *buf, uint16_t len);

/* 组包并发送蓝牙帧: 包头 + 6轴(各2字节,低字节在前) + 2字节按键位图 + 包尾
 * 依 BT_CONFIG: use_hex=1 时逐字节转十六进制 ASCII, use_newline=1 时追加 \r\n */
void BT_SendFrame(uint16_t axis[6], uint16_t btn_bitmap, BT_CONFIG *cfg);

#endif /* __USART_H */
