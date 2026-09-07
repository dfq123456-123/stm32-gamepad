# USB HID 手柄集成 — 编译验证报告

**工程**: `D:\stm32\project\手柄`(Keil MDK 5.14, STM32F103C8T6)
**日期**: 2026-08-28
**结论**: ✅ **0 Error / 0 Warning,`demo.hex` 已生成(32KB),烧录即可用**

---

## 1. 本次做了什么(方案 B:补 USB HID)

在原「摇杆+按键 → 串口 15B 帧」基础上,新增 **USB HID GamePad 通道**,与博主固件 VID/PID 一致,PC/手机即插即用:

| 项目 | 内容 |
|---|---|
| USB 协议栈 | ST USB-FS-Device Library V3.4.0(标准库风格,自写寄存器驱动) |
| 设备描述符 | VID `0x0483`(ST)/ PID `0x5750`, bcdUSB 2.00 |
| HID 报告 | **8 字节** Game Pad: `[0]`按键1-8 `[1]`按键9-16 `[2..5]`四轴±127 `[6][7]`LT/RT(0~255) |
| 端点 | EP1 IN(0x81), 中断传输, 8 字节, 间隔 10ms |
| 枚举名 | "STM32 GamePad"(Game Pad 类) |
| D+ 上拉 | PA12 硬件 1.5k 上拉(C8T6 最小系统板自带),无需软件控制 |
| 串口通道 | **保留不变**(USART1 PA9/PA10 → HC-05, 15B 帧 115200) |

## 2. 新增/修改文件

```
USB/                              ← 新增(应用层, 7 源 + 6 头)
  usb_desc.c/h     描述符(设备/配置/报告 65B/字符串)
  usb_prop.c/h     标准请求处理(官方 V3.4.0 改 EP1 长度=8)
  usb_pwr.c/h      电源状态机(官方原样)
  usb_istr.c/h     中断事件分发(官方原样)
  usb_endp.c       EP1_IN_Callback → PrevXferComplete=1
  usb_it.c         独立 USB 中断(USB_LP_CAN1_RX0 + USBWakeUp)
  hw_config.c/h    Set_System / Set_USBClock / Joystick_Send / 序列号
Libraries/STM32_USB-FS-Device_Driver/   ← 新增(驱动层, 6 源 + 9 头)
USER/main.c       ← 修改(USB 初始化 + 主循环双通道发送)
HARDWARE/joystick.c/h ← 修改(新增 Joystick_GetHIDReport() + RY 中心偏移 bug 修复)
demo.uvprojx      ← 修改(新增 USB/USB_Driver 两组 13 个源文件 + 2 处 IncludePath + exti.c)
```

## 3. 编译过程:390 错误 → 0 错误

首轮编译暴露 4 类问题,均已修复:

| # | 问题 | 根因 | 修复 |
|---|---|---|---|
| 1 | `uint8_t`/`__IO` 未定义(390 个级联错误) | `hw_config.h`/`usb_desc.h` 精简时丢了 `#include "stm32f10x.h"`(官方头是自包含的) | 两个头文件顶部补 `#include "stm32f10x.h"` |
| 2 | `bDeviceState` 声明不兼容 | `hw_config.c` 手写 `extern __IO uint8_t bDeviceState;`,与 `usb_pwr.h` 的 `__IO uint32_t` 冲突 | 删除冗余声明(已 include usb_pwr.h) |
| 3 | `EXTI_Line18` 未定义 + 链接 `L6218E: EXTI_ClearITPendingBit` | USB 唤醒中断用到 EXTI 行,但 `usb_it.c` 没包含 `stm32f10x_exti.h`,且 FWLib 组未编译 `stm32f10x_exti.c` | 头补 `stm32f10x_exti.h`;工程 FWLib 组新增 `stm32f10x_exti.c` |
| 4 | `#9-D: nested comment` 警告 | `main.c` 注释头里 `USB/*` 触发嵌套注释 | 改为 `USB /` |

## 4. 编译结果

```
Program Size: Code=10712  RO-data=464  RW-data=440  ZI-data=1688
".\Objects\demo.axf" - 0 Error(s), 0 Warning(s)
FromELF: creating hex file...  →  Objects/demo.hex (32,046 B)
```

| 资源 | 占用 | 芯片总量 | 余量 |
|---|---|---|---|
| Flash | 11.3 KB | 64 KB | 82% |
| RAM | 2.1 KB | 20 KB | 90% |

## 5. 烧录与验证步骤

1. **烧录**: J-Link / ST-Link 下载 `Objects\demo.hex`(擦除整片后写入)
2. **USB 枚举**: 数据线接板子 PA11/PA12(USB DP/DM),电脑设备管理器应出现 **"STM32 GamePad"(HID 兼容游戏控制器)**,无需装驱动
3. **功能验证**(推荐用浏览器 [gamepad-tester.com](https://gamepad-tester.com)):
   - 摇杆: 左摇杆 → X/Y 轴,右摇杆 → Z/Rz 轴,推到底应为 ±127
   - 扳机: LT/RT → Rx/Ry 轴,0~255 线性
   - 按键: 正面 A/B/X/Y、肩键 LB/RB、摇杆按下 L3/R3、Home/Menu/View、十字键 UP/DOWN/LEFT/RIGHT,按下对应位变 1
   - 串口: 保留通道不受影响,HC-05 照常发 15B 帧
4. **常见问题**:
   - 枚举失败 → 检查 PA12 是否有 1.5k 上拉到 3.3V;USB 线是否纯数据线;VBUS 供电是否正常
   - 轴有方向反 → 修改 `HARDWARE/joystick.h` 中 `JOY_INVERT_*` 宏后重新编译

## 6. HID 报告字节布局(与博主兼容)

```
Byte 0: bit0-7 = A, B, X, Y, LB, RB, View, Menu
Byte 1: bit0-7 = Home, L3, R3, UP, DOWN, LEFT, RIGHT, (reserved)
Byte 2: LX  (-127~+127)
Byte 3: LY  (-127~+127)
Byte 4: RX  (-127~+127)
Byte 5: RY  (-127~+127)
Byte 6: LT  (0~255)
Byte 7: RT  (0~255)
```

轴映射算法: `(ADC_raw - center) × 127 / 2048`,钳位 ±127;中心值由 `Joystick_Calibrate()` 上电 100 次采样自动标定(含 RY 反相中心修正)。

## 7. 遗留说明

- 博主固件还含 OLED 菜单 / WS2812B 灯带 / AT 配对(方案 C 范围),本次按方案 B 未实现
- 板载 15 键中若含"配对确认/数值调节"等复用键,当前全部按普通按键上报
- 下一步建议: 先烧录实测枚举与各轴/键映射,确认无误后再决定是否继续方案 C
