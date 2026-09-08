/*******************************************************************************
 * menu.h - 双模式菜单系统 + 测试手柄界面
 *
 * 模式: 游戏控制器模式(USB HID) / 蓝牙控制器模式(UART 透传)
 * 中间三键(用户最新定义):
 *   PB3 KEY_HOME = 仅菜单返回(OPEN/EDIT/TEST -> 返回, 关闭态无作用)
 *   PB4 KEY_MENU = 打开/关闭手柄自身菜单
 *   PB5 KEY_VIEW = 切换 游戏手柄模式 / 蓝牙模式
 *
 * 游戏模式菜单: 摇杆死区 / 滤波强度 / 测试手柄
 * 蓝牙模式菜单: 包头 / 包尾 / 十六进制 / 换行符
 ******************************************************************************/
#ifndef __MENU_H
#define __MENU_H

#include "stm32f10x.h"

/*---------------------- 工作模式 ----------------------*/
typedef enum
{
    MODE_GAME = 0,      /* 游戏控制器模式(USB HID) */
    MODE_BT             /* 蓝牙控制器模式(UART 透传) */
} DEV_MODE;

/*---------------------- 蓝牙帧配置 ----------------------*/
typedef struct
{
    uint8_t header;         /* 包头字节 */
    uint8_t tail;           /* 包尾字节 */
    uint8_t use_hex;        /* 1=十六进制(hex->ASCII)编码 */
    uint8_t use_newline;    /* 1=追加换行 \r\n */
} BT_CONFIG;

/*---------------------- API ----------------------*/
void       Menu_Init(void);
void       Menu_Tick(void);              /* 主循环每帧调用: 按键检测 + 状态机 */
void       Menu_EnterTest(void);         /* 直接进入"测试手柄"界面(开机校准后自动调用) */
uint8_t    Menu_GetMode(void);           /* 返回 DEV_MODE */
BT_CONFIG *Menu_GetBTConfig(void);       /* 返回蓝牙帧配置指针 */
uint8_t    Menu_GetIdleTimeout(void);    /* 空闲熄屏秒数: 0=关闭, 否则 5/10/30 */
void       Menu_ForceRedraw(void);       /* 熄屏唤醒后强制全量重画(清掉残影) */

#endif /* __MENU_H */
