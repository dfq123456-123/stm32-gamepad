/**
  ******************************************************************************
  * @file    usb_conf.h
  * @brief   USB HID GamePad 配置(基于 ST JoyStickMouse V3.4.0 改造)
  *          EP0 控制端点 + EP1 IN 中断端点(9 字节 HID 报告)
  ******************************************************************************
  */
#ifndef __USB_CONF_H
#define __USB_CONF_H

/* EP 数量: EP0(控制) + EP1 IN(HID 报告) */
#define EP_NUM     (2)

/*-------------------------------------------------------------*/
/* --------------   Buffer Description Table  -----------------*/
/*-------------------------------------------------------------*/
/* buffer table base address */
#define BTABLE_ADDRESS      (0x00)

/* EP0  rx/tx buffer base address */
#define ENDP0_RXADDR        (0x18)
#define ENDP0_TXADDR        (0x58)

/* EP1  tx buffer base address (9 字节报告, 0x100 起 128 字节空间足够) */
#define ENDP1_TXADDR        (0x100)

/*-------------------------------------------------------------*/
/* -------------------   ISTR events  -------------------------*/
/*-------------------------------------------------------------*/
/* IMR_MSK: 需处理的中断事件 */
#define IMR_MSK (CNTR_CTRM  | CNTR_WKUPM | CNTR_SUSPM | CNTR_ERRM  | CNTR_SOFM \
                 | CNTR_ESOFM | CNTR_RESETM )

/* CTR 服务例程(端点回调) */
#define  EP2_IN_Callback   NOP_Process
#define  EP3_IN_Callback   NOP_Process
#define  EP4_IN_Callback   NOP_Process
#define  EP5_IN_Callback   NOP_Process
#define  EP6_IN_Callback   NOP_Process
#define  EP7_IN_Callback   NOP_Process

#define  EP1_OUT_Callback  NOP_Process
#define  EP2_OUT_Callback  NOP_Process
#define  EP3_OUT_Callback  NOP_Process
#define  EP4_OUT_Callback  NOP_Process
#define  EP5_OUT_Callback  NOP_Process
#define  EP6_OUT_Callback  NOP_Process
#define  EP7_OUT_Callback  NOP_Process

/* EP1_IN_Callback 由 usb_endp.c 定义(传输完成置 PrevXferComplete) */

#endif /*__USB_CONF_H*/
