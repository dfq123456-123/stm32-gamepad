//joystick.h - 游戏手柄采集核心
//15 键(1基) + 6 路 ADC, 自适应量程
#ifndef __JOYSTICK_H
#define __JOYSTICK_H

#include "stm32f10x.h"
#include "adc.h"

typedef void (*Joystick_CalProgressCB)(uint8_t percent);	 //校准进度回调

#define JOY_DEADZONE_DEFAULT     300     //|raw-center|<300 视为回中(盖住 PA1↔PA2 串扰尖峰)
#define JOY_FILTER_DEFAULT       3	//一阶低通移位量(0=无滤波)
#define JOY_CAL_NUM              100	//开机中心校准采样次数
#define JOY_TRIG_DIFF_DEAD       120	//(已废弃) 原"两扳机差动"死区, 保留备查

#define HID_REPORT_LEN           8

//按键键号(1基)
#define KEY_UP      1	//PB11
#define KEY_DOWN    2	//PB1
#define KEY_RIGHT   3	//PB0
#define KEY_LEFT    4	//PB10
#define KEY_X       5	//PB9
#define KEY_Y       6	//PB6
#define KEY_B       7	//PB7
#define KEY_A       8	//PB8
#define KEY_HOME    9	//PB3  菜单返回
#define KEY_MENU    10	//PB4  打开菜单
#define KEY_VIEW    11	//PB5  切换模式
#define KEY_R3      12	//PA6
#define KEY_L3      13	//PA7
#define KEY_LB      14	//PB15
#define KEY_RB      15	//PB14

#define KEY_NUM     15

//摇杆轴映射: 模块 90° 错位安装, VRX 感应上下、VRY 感应左右
#define AXIS_CH_LX      ADC_CH_L_VRY
#define AXIS_CH_LY      ADC_CH_L_VRX
#define AXIS_CH_RX      ADC_CH_R_VRY
#define AXIS_CH_RY      ADC_CH_R_VRX

void MY_Joystick_Init(void);
void Joystick_Calibrate(void);
void Joystick_Calibrate_Ex(Joystick_CalProgressCB cb);
void Joystick_Scan(void);
void Joystick_GetHIDReport(uint8_t *buf);	//9字节 HID 报告
uint8_t Joystick_GetKey(uint8_t key_id);
uint8_t Joystick_KeyPressed(uint8_t key_id);	 //边沿: 本次扫描新按下
void     Joystick_SetDeadzone(uint16_t dz);
uint16_t Joystick_GetDeadzone(void);
void     Joystick_SetFilter(uint8_t f);
uint8_t  Joystick_GetFilter(void);
uint16_t Joystick_GetAxis(uint8_t ch);
uint16_t Joystick_GetCenter(uint8_t ch);
uint16_t Joystick_GetRaw(uint8_t ch);
uint16_t Joystick_GetBtnBitmap(void);
int16_t Joystick_GetNormRaw(uint8_t ch);	 //原始归一化 ±1024
int16_t Joystick_GetNormAbs(uint8_t ch);	 //绝对归一化 0~1024
int16_t Joystick_GetNorm(uint8_t ch);	 //滤波后归一化 ±1024
int16_t Joystick_GetDeadzoneNorm(uint8_t ch);	 //死区归一化 0~1024
uint8_t Joystick_IsActive(void);
void Joystick_GetProcessed(uint16_t axis[6], uint16_t *btn_bitmap);

#endif	//__JOYSTICK_H
