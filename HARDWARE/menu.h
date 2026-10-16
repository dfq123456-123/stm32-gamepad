//menu.h - 双模式菜单 + 测试手柄界面
//游戏模式(USB HID) / 蓝牙模式(UART 透传)
//HOME=菜单返回, MENU=打开/关闭菜单, VIEW=切换模式
#ifndef __MENU_H
#define __MENU_H

#include "stm32f10x.h"

typedef enum
{
	MODE_GAME = 0,	//USB HID
	MODE_BT		//UART 透传
} DEV_MODE;

typedef struct
{
	uint8_t header;
	uint8_t tail;
	uint8_t use_hex;	 //1=hex->ASCII
	uint8_t use_newline;	 //1=追加 \r\n
} BT_CONFIG;

void       Menu_Init(void);
void       Menu_Tick(void);
void       Menu_EnterTest(void);
uint8_t    Menu_GetMode(void);
BT_CONFIG *Menu_GetBTConfig(void);
uint8_t    Menu_GetIdleTimeout(void);	 //0=关闭, 否则秒数
void       Menu_ForceRedraw(void);

#endif	//__MENU_H
