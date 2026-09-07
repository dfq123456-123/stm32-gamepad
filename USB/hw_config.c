/**
  ******************************************************************************
  * @file    hw_config.c
  * @brief   USB 硬件配置(基于 ST JoyStickMouse V3.4.0 精简重写)
  *
  * 与官方例程的差异:
  *   1. 删除 GPIO_AINConfig() —— 会把全部 GPIO 配成模拟输入,
  *      破坏手柄按键(上拉输入)与 ADC 配置, 本工程 GPIO 已由
  *      MY_Joystick_Init() 负责初始化;
  *   2. 删除 STM_EVAL 按键初始化 —— 按键由 joystick.c 管理;
  *   3. USB_Cable_Config 空实现 —— C8T6 最小系统板 D+ (PA12)
  *      已通过 1.5k 电阻硬件上拉到 3.3V, 无需软件控制;
  *   4. 低功耗简化 —— 只置状态位, 不进 STOP 模式(手柄无需深度省电)。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "hw_config.h"
#include "usb_lib.h"
#include "usb_pwr.h"
#include "joystick.h"

/* Private variables ---------------------------------------------------------*/
__IO uint8_t PrevXferComplete = 1;   /* USB 上一次发送完成标志 */

/*******************************************************************************
* Function Name  : Set_System
* Description    : 系统级初始化(时钟已在 SystemInit 完成, 此处仅使能 PWR)
*******************************************************************************/
void Set_System(void)
{
  /* 使能 PWR 时钟(低功耗相关, usb_pwr.c 需要) */
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
}

/*******************************************************************************
* Function Name  : Set_USBClock
* Description    : 配置 USB 时钟 48MHz (PLL 72MHz / 1.5 = 48MHz)
*******************************************************************************/
void Set_USBClock(void)
{
  /* 选择 USBCLK 源: PLLCLK / 1.5 = 72/1.5 = 48MHz */
  RCC_USBCLKConfig(RCC_USBCLKSource_PLLCLK_1Div5);

  /* 使能 USB 时钟 (APB1) */
  RCC_APB1PeriphClockCmd(RCC_APB1Periph_USB, ENABLE);
}

/*******************************************************************************
* Function Name  : Enter_LowPowerMode
* Description    : 进入挂起(简化: 只更新状态, 不切时钟)
*******************************************************************************/
void Enter_LowPowerMode(void)
{
  /* 更新设备状态为挂起 */
  bDeviceState = SUSPENDED;
}

/*******************************************************************************
* Function Name  : Leave_LowPowerMode
* Description    : 退出挂起(简化: 恢复状态, 时钟一直运行无需恢复)
*******************************************************************************/
void Leave_LowPowerMode(void)
{
  DEVICE_INFO *pInfo = &Device_Info;

  /* 恢复设备状态 */
  if (pInfo->Current_Configuration != 0)
  {
    bDeviceState = CONFIGURED;
  }
  else
  {
    bDeviceState = ATTACHED;
  }
}

/*******************************************************************************
* Function Name  : USB_Interrupts_Config
* Description    : 配置 USB 中断 (F103: USB_LP_CAN1_RX0 + USBWakeUp)
*******************************************************************************/
void USB_Interrupts_Config(void)
{
  NVIC_InitTypeDef NVIC_InitStructure;

  /* 2 bit 抢占 + 2 bit 子优先级 (与 usart.c 保持一致) */
  NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

  /* USB Low Priority 中断 (枚举/传输) */
  NVIC_InitStructure.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);

  /* USB 唤醒中断 */
  NVIC_InitStructure.NVIC_IRQChannel = USBWakeUp_IRQn;
  NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
  NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
  NVIC_Init(&NVIC_InitStructure);
}

/*******************************************************************************
* Function Name  : USB_Cable_Config
* Description    : D+ 上拉控制 —— 硬件 1.5k 直连 3.3V, 空实现
*******************************************************************************/
void USB_Cable_Config (FunctionalState NewState)
{
  /* PA12(USB_DP) 已通过板载 1.5k 电阻上拉, 无需软件控制 */
}

/*******************************************************************************
* Function Name  : Joystick_Send
* Description    : 组 8 字节 HID 报告并发送 (EP1 IN, 中断传输)
*                  调用前确保 bDeviceState == CONFIGURED 且 PrevXferComplete
*******************************************************************************/
void Joystick_Send(void)
{
  uint8_t hid_buf[JOYSTICK_REPORT_LEN];

  /* 从采集层组包: 2 按键字节 + 4 摇杆轴 + 2 扳机 */
  Joystick_GetHIDReport(hid_buf);

  /* 置发送中标志, EP1_IN_Callback 完成后恢复 */
  PrevXferComplete = 0;

  /* 写入端点 TX 缓冲并发送 */
  USB_SIL_Write(EP1_IN, hid_buf, JOYSTICK_REPORT_LEN);
  SetEPTxValid(ENDP1);
}

/*******************************************************************************
* Function Name  : Get_SerialNum
* Description    : 用芯片唯一 ID 生成序列号字符串描述符
*******************************************************************************/
void Get_SerialNum(void)
{
  uint32_t Device_Serial0, Device_Serial1, Device_Serial2;
  uint8_t idx;

  Device_Serial0 = *(__IO uint32_t*)(0x1FFFF7E8);
  Device_Serial1 = *(__IO uint32_t*)(0x1FFFF7EC);
  Device_Serial2 = *(__IO uint32_t*)(0x1FFFF7F0);

  Device_Serial0 += Device_Serial2;

  if (Device_Serial0 != 0)
  {
    /* 序列号字符串为 12 个十六进制字符, 从偏移 2 开始填充 */
    for (idx = 0; idx < 12; idx++)
    {
      uint8_t hex = (uint8_t)((idx < 8) ? (Device_Serial0 >> (28 - idx * 4))
                                        : (Device_Serial1 >> (44 - idx * 4)));
      hex &= 0x0F;
      Joystick_StringSerial[2 + idx * 2] = (hex < 0xA) ? (hex + '0') : (hex + 'A' - 10);
      Joystick_StringSerial[2 + idx * 2 + 1] = 0;
    }
  }
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
