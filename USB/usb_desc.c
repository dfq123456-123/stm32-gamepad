/**
  ******************************************************************************
  * @file    usb_desc.c
  * @brief   USB HID GamePad 描述符
  *          9 字节报告: [0]按钮1-8 [1]按钮9-15+1padding [2]Hat Switch(方向键/十字键)
 *                      [3]X(左摇杆横) [4]Y(左摇杆纵) [5]Z(左扳机) [6]Rx(右摇杆横) [7]Ry(右摇杆纵)
 *                      [8]Rz(右扳机) —— LT/RT 各自独立成轴, 可同时按下(原为两扳机差动合成单轴)
 *          按钮序(XInput, 由 joystick.c 的 report_btn_bit 重映射): 1=A 2=B 3=X 4=Y 5=LB 6=RB
 *                      7=View 8=Menu 9=Home 10=L3 11=R3  12=UP 13=DOWN 14=LEFT 15=RIGHT(十字键走 Hat+按钮12~15)
  *          VID=0x0483(ST) PID=0x5750 (与博主固件一致)
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "usb_lib.h"
#include "usb_desc.h"

/* USB Standard Device Descriptor */
const uint8_t Joystick_DeviceDescriptor[JOYSTICK_SIZ_DEVICE_DESC] =
  {
    0x12,                       /*bLength */
    USB_DEVICE_DESCRIPTOR_TYPE, /*bDescriptorType*/
    0x00,                       /*bcdUSB */
    0x02,
    0x00,                       /*bDeviceClass*/
    0x00,                       /*bDeviceSubClass*/
    0x00,                       /*bDeviceProtocol*/
    0x40,                       /*bMaxPacketSize 64*/
    0x83,                       /*idVendor (0x0483)*/
    0x04,
    0x50,                       /*idProduct = 0x5750*/
    0x57,
    0x01,                       /*bcdDevice rel. 2.01: 改版本号触发系统重建控制器配置*/
    0x02,
    1,                          /*iManufacturer*/
    2,                          /*iProduct*/
    3,                          /*iSerialNumber*/
    0x01                        /*bNumConfigurations*/
  };

/* USB Configuration Descriptor */
const uint8_t Joystick_ConfigDescriptor[JOYSTICK_SIZ_CONFIG_DESC] =
  {
    0x09, /* bLength: Configuration Descriptor size */
    USB_CONFIGURATION_DESCRIPTOR_TYPE, /* bDescriptorType: Configuration */
    JOYSTICK_SIZ_CONFIG_DESC,
    0x00, /* wTotalLength */
    0x01, /* bNumInterfaces: 1 interface */
    0x01, /* bConfigurationValue */
    0x00, /* iConfiguration */
    0x80, /* bmAttributes: bus powered */
    0x32, /* MaxPower 100 mA */

    /************** Descriptor of GamePad interface ****************/
    0x09,         /*bLength: Interface Descriptor size*/
    USB_INTERFACE_DESCRIPTOR_TYPE,/*bDescriptorType*/
    0x00,         /*bInterfaceNumber*/
    0x00,         /*bAlternateSetting*/
    0x01,         /*bNumEndpoints*/
    0x03,         /*bInterfaceClass: HID*/
    0x00,         /*bInterfaceSubClass: 0=no boot*/
    0x00,         /*bInterfaceProtocol: 0=none*/
    0,            /*iInterface*/

    /******************** Descriptor of HID ********************/
    0x09,         /*bLength: HID Descriptor size*/
    HID_DESCRIPTOR_TYPE, /*bDescriptorType: HID*/
    0x00,         /*bcdHID: HID Class Spec release number*/
    0x01,
    0x00,         /*bCountryCode*/
    0x01,         /*bNumDescriptors*/
    0x22,         /*bDescriptorType: Report*/
    JOYSTICK_SIZ_REPORT_DESC,/*wItemLength: Report descriptor length*/
    0x00,

    /******************** Descriptor of endpoint ********************/
    0x07,          /*bLength: Endpoint Descriptor size*/
    USB_ENDPOINT_DESCRIPTOR_TYPE, /*bDescriptorType*/
    0x81,          /*bEndpointAddress: EP1 IN*/
    0x03,          /*bmAttributes: Interrupt endpoint*/
    JOYSTICK_REPORT_LEN, /*wMaxPacketSize: 8 Byte*/
    0x00,
    0x0A,          /*bInterval: 10 ms*/
  };

/* HID Report Descriptor: Game Pad
 * 报告布局(9 字节):
 *   [0] 按键 bit0~7   [1] 按键 bit8~14   [2] 方向键 Hat Switch(0~8)
 *   [3] X(摇杆1X) [4] Y(摇杆1Y) [5] Z(左扳机) [6] Rx(摇杆2X) [7] Ry(摇杆2Y) [8] Rz(右扳机)
 * 描述符: 15 按钮 + 1 位 padding + Hat Switch(4位) + 4 位 padding + 6 轴(8位各)
 * ⚠️ 原为 8 字节/5 轴: byte5 = Z 承载"两扳机差动"(RT-LT), 导致两扳机同时按下且深度
 *    接近时 diff≈0 落到中位 → PC 判为"没按扳机". 故扩为 9 字节/6 轴, LT/RT 各占独立轴. */
const uint8_t Joystick_ReportDescriptor[JOYSTICK_SIZ_REPORT_DESC] =
  {
    0x05, 0x01,          /* Usage Page (Generic Desktop) */
    0x09, 0x05,          /* Usage (Game Pad) */
    0xA1, 0x01,          /* Collection (Application) */
    0xA1, 0x00,          /*   Collection (Physical) */

    0x05, 0x09,          /*     Usage Page (Button) */
    0x19, 0x01,          /*     Usage Minimum (1) */
    0x29, 0x0F,          /*     Usage Maximum (15): 声明 15 个通用按钮(Usage 1~15),
                                   具体哪个物理键映射到位由固件 report_btn_bit 决定(位0=A, 位11~14=十字键) */
    0x15, 0x00,          /*     Logical Minimum (0) */
    0x25, 0x01,          /*     Logical Maximum (1) */
    0x75, 0x01,          /*     Report Size (1) */
    0x95, 0x0F,          /*     Report Count (15): 与固件 15 位键位图一致 */
    0x81, 0x02,          /*     Input (Data,Var,Abs): byte0 + byte1低7位 */

    0x75, 0x01,          /*     Report Size (1) */
    0x95, 0x01,          /*     Report Count (1) */
    0x81, 0x03,          /*     Input (Const,Var,Abs): 1 位 padding (byte1 bit7) */

    0x05, 0x01,          /*     Usage Page (Generic Desktop) */
    0x09, 0x39,          /*     Usage (Hat Switch) */
    0x15, 0x00,          /*     Logical Minimum (0) */
    0x25, 0x07,          /*     Logical Maximum (7): 方向 0~7, 值8 越界+Null态 = 无方向(居中) */
    0x35, 0x00,          /*     Physical Minimum (0) */
    0x46, 0x68, 0x01,    /*     Physical Maximum (360) */
    0x65, 0x14,          /*     Unit (Eng Rot: degree) */
    0x75, 0x04,          /*     Report Size (4) */
    0x95, 0x01,          /*     Report Count (1) */
    0x81, 0x42,          /*     Input (Data,Var,Abs,Null): byte2 低4位 = 方向键(0~8) */

    0x75, 0x04,          /*     Report Size (4) */
    0x95, 0x01,          /*     Report Count (1) */
    0x81, 0x03,          /*     Input (Const,Var,Abs): 4 位 padding */

    0x09, 0x30,          /*     Usage (X)  = 摇杆1X */
    0x09, 0x31,          /*     Usage (Y)  = 摇杆1Y */
    0x09, 0x32,          /*     Usage (Z)  = 左扳机 LT(独立轴, 原为两扳机差动合成) */
    0x09, 0x33,          /*     Usage (Rx) = 摇杆2X */
    0x09, 0x34,          /*     Usage (Ry) = 摇杆2Y(右纵) */
    0x09, 0x35,          /*     Usage (Rz) = 右扳机 RT(独立轴) */
    0x15, 0x00,          /*     Logical Minimum (0) */
    0x26, 0xFF, 0x00,    /*     Logical Maximum (255) */
    0x75, 0x08,          /*     Report Size (8) */
    0x95, 0x06,          /*     Report Count (6) */
    0x81, 0x02,          /*     Input (Data,Var,Abs): byte3~8 = 6 轴 */

    0xC0,                /*   End Collection */
    0xC0                 /* End Collection */
  };

/* USB String Descriptors */
const uint8_t Joystick_StringLangID[JOYSTICK_SIZ_STRING_LANGID] =
  {
    JOYSTICK_SIZ_STRING_LANGID,
    USB_STRING_DESCRIPTOR_TYPE,
    0x09,
    0x04
  };

const uint8_t Joystick_StringVendor[JOYSTICK_SIZ_STRING_VENDOR] =
  {
    JOYSTICK_SIZ_STRING_VENDOR, /* bLength */
    USB_STRING_DESCRIPTOR_TYPE, /* bDescriptorType */
    /* "STM32" */
    'S', 0, 'T', 0, 'M', 0, '3', 0, '2', 0
  };

const uint8_t Joystick_StringProduct[JOYSTICK_SIZ_STRING_PRODUCT] =
  {
    JOYSTICK_SIZ_STRING_PRODUCT, /* bLength */
    USB_STRING_DESCRIPTOR_TYPE,  /* bDescriptorType */
    /* "STM32 GamePad" */
    'S', 0, 'T', 0, 'M', 0, '3', 0, '2', 0, ' ', 0, 'G', 0, 'a', 0,
    'm', 0, 'e', 0, 'P', 0, 'a', 0, 'd', 0
  };

uint8_t Joystick_StringSerial[JOYSTICK_SIZ_STRING_SERIAL] =
  {
    JOYSTICK_SIZ_STRING_SERIAL, /* bLength */
    USB_STRING_DESCRIPTOR_TYPE, /* bDescriptorType */
    'S', 0, 'T', 0, 'M', 0, '3', 0, '2', 0, 'G', 0, '3', 0  /* STM32G3: 序列号+1 强制系统重新枚举, 重读新描述符(去掉Hat越界误判) */
  };

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
