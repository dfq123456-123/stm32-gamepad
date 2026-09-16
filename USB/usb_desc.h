/**
  ******************************************************************************
  * @file    usb_desc.h
  * @brief   USB HID GamePad 描述符头(基于 ST JoyStickMouse V3.4.0 改造)
  ******************************************************************************
  */
#ifndef __USB_DESC_H
#define __USB_DESC_H

#include "stm32f10x.h"   /* 提供 uint8_t 等类型定义 */

#define USB_DEVICE_DESCRIPTOR_TYPE              0x01
#define USB_CONFIGURATION_DESCRIPTOR_TYPE       0x02
#define USB_STRING_DESCRIPTOR_TYPE              0x03
#define USB_INTERFACE_DESCRIPTOR_TYPE           0x04
#define USB_ENDPOINT_DESCRIPTOR_TYPE            0x05

#define HID_DESCRIPTOR_TYPE                     0x21
#define JOYSTICK_SIZ_HID_DESC                   0x09
#define JOYSTICK_OFF_HID_DESC                   0x12    /* 配置描述符内 HID 描述符偏移: 9(配置)+9(接口) */

#define JOYSTICK_SIZ_DEVICE_DESC                18
#define JOYSTICK_SIZ_CONFIG_DESC                34      /* 9配置 + 9接口 + 9HID + 7端点 */
#define JOYSTICK_SIZ_REPORT_DESC                82      /* Game Pad 报告描述符: 15按钮+dpad+6轴 */
#define JOYSTICK_SIZ_STRING_LANGID              4
#define JOYSTICK_SIZ_STRING_VENDOR              38
#define JOYSTICK_SIZ_STRING_PRODUCT             36
#define JOYSTICK_SIZ_STRING_SERIAL              26

#define STANDARD_ENDPOINT_DESC_SIZE             0x09

/* HID 报告长度: 2 按键字节 + 1 Hat 字节 + 6 轴 = 9 字节
 * 6 轴 = 双摇杆 4 轴(X/Y/Rx/Ry) + 左扳机 Z + 右扳机 Rz(各自独立, 可同时按) */
#define JOYSTICK_REPORT_LEN                     9

extern const uint8_t Joystick_DeviceDescriptor[JOYSTICK_SIZ_DEVICE_DESC];
extern const uint8_t Joystick_ConfigDescriptor[JOYSTICK_SIZ_CONFIG_DESC];
extern const uint8_t Joystick_ReportDescriptor[JOYSTICK_SIZ_REPORT_DESC];
extern const uint8_t Joystick_StringLangID[JOYSTICK_SIZ_STRING_LANGID];
extern const uint8_t Joystick_StringVendor[JOYSTICK_SIZ_STRING_VENDOR];
extern const uint8_t Joystick_StringProduct[JOYSTICK_SIZ_STRING_PRODUCT];
extern uint8_t Joystick_StringSerial[JOYSTICK_SIZ_STRING_SERIAL];

#endif /* __USB_DESC_H */
