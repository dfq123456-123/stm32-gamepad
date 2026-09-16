/**
  ******************************************************************************
  * @file    hw_config.h
  * @brief   USB 硬件配置头(基于 ST JoyStickMouse V3.4.0 精简)
  *          去掉了 STM_EVAL 评估板依赖;D+ 上拉为硬件 1.5k,无需软件控制
  ******************************************************************************
  */
#ifndef __HW_CONFIG_H
#define __HW_CONFIG_H

#include "stm32f10x.h"
#include "usb_type.h"
#include "usb_desc.h"

/* Exported variables ------------------------------------------------------- */
extern __IO uint8_t PrevXferComplete;   /* USB 上一次发送是否完成 */

/* Exported functions ------------------------------------------------------- */
void Set_System(void);
void Set_USBClock(void);
void Enter_LowPowerMode(void);
void Leave_LowPowerMode(void);
void USB_Interrupts_Config(void);
void USB_Cable_Config (FunctionalState NewState);
void Joystick_Send(void);       /* 组 9 字节 HID 报告并经 EP1 发送 */
void Get_SerialNum(void);

#endif  /*__HW_CONFIG_H*/
