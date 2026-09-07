/**
  ******************************************************************************
  * @file    usb_it.c
  * @brief   USB 中断服务(替代官方 stm32_it.c, 避免与工程其他中断冲突)
  *          F103 非互联型: USB_LP_CAN1_RX0 + USBWakeUp
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f10x_exti.h"   /* EXTI_Line18: USB 唤醒事件 */
#include "usb_lib.h"
#include "usb_istr.h"
#include "usb_pwr.h"

/*******************************************************************************
* Function Name  : USB_LP_CAN1_RX0_IRQHandler
* Description    : USB Low Priority 中断(枚举/端点传输)
*******************************************************************************/
void USB_LP_CAN1_RX0_IRQHandler(void)
{
  USB_Istr();
}

/*******************************************************************************
* Function Name  : USBWakeUp_IRQHandler
* Description    : USB 唤醒中断
*******************************************************************************/
void USBWakeUp_IRQHandler(void)
{
  EXTI_ClearITPendingBit(EXTI_Line18);
}

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
