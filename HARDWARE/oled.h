//oled.h - SSD1306 128x64, 软件 I2C(PB12=SCL, PB13=SDA)
#ifndef __OLED_H
#define __OLED_H

#include "stm32f10x.h"

//中文索引(与 font.h CN_FONT 顺序一致)
typedef enum
{
	CN_WO = 0,	//我
	CN_DE,	//的
	CN_YOU,	//又
	CN_SHAO,	//烧
	CN_LA,	//啦
	CN_BAN,	//版
	CN_BEN,	//本
	CN_YOU_XI,	//游
	CN_XI,	//戏
	CN_MO,	//模
	CN_SHI,	//式
	CN_LAN,	//蓝
	CN_YA,	//牙
	CN_SI,	//死
	CN_QU,	//区
	CN_LV,	//滤
	CN_BO,	//波
	CN_QIANG,	//强
	CN_DU,	//度
	CN_CAI,	//菜
	CN_DAN,	//单
	CN_BAO,	//包
	CN_TOU,	//头
	CN_WEI,	//尾
	CN_SHI2,	//十
	CN_LIU,	//六
	CN_JIN,	//进
	CN_ZHI,	//制
	CN_HUAN,	//换
	CN_XING,	//行
	CN_KAI,	//开
	CN_QI,	//启
	CN_GUAN,	//关
	CN_BI,	//闭
	CN_QUE,	//确
	CN_REN,	//认
	CN_FAN,	//返
	CN_HUI,	//回
	CN_JIAO,	//校
	CN_ZHUN,	//准
	CN_ZHONG,	//中
	CN_LIANG,	//亮
	CN_LIAN,	//连
	CN_FA,	//发
	CN_SU,	//速
	CN_PING,	//屏
	CN_SHOU,	//手
	CN_BING,	//柄
	CN_MAX
} CN_INDEX;

void    OLED_GPIOInit(void);
uint8_t OLED_Probe(uint8_t addr);
void    OLED_SetAddr(uint8_t addr);
uint8_t OLED_GetAddr(void);
void OLED_Init(void);
void OLED_Clear(void);
void OLED_Refresh(void);
void OLED_ShowChar(uint8_t x, uint8_t line, char ch);	 //8x16, line=0~3
void OLED_ShowString(uint8_t x, uint8_t line, const char *str);
void OLED_ShowCN(uint8_t x, uint8_t line, uint8_t idx);	 //16x16, line=0~3
void OLED_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t on);
void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t on);
void OLED_ClearRect(uint8_t x0, uint8_t y0, uint8_t w, uint8_t h);
void OLED_RefreshRect(uint8_t x0, uint8_t y0, uint8_t w, uint8_t h);	 //局部刷新
void OLED_ShowStringXY(uint8_t x, uint8_t y, const char *str);
void OLED_SetContrast(uint8_t val);	 //对比度 0~255
void OLED_DisplayOff(void);
void OLED_DisplayOn(void);

#endif	//__OLED_H
