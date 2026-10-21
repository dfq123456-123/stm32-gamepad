//flash_save.h - 参数断电保存(片内 Flash 末页)
//STM32F103C8: 64KB Flash, 页 1KB, 用末页 0x0800FC00
#ifndef __FLASH_SAVE_H
#define __FLASH_SAVE_H

#include <stdint.h>

	//参数结构体(断电保存的字段, 改结构时递增 VERSION 让旧数据失效自动回默认)
#define PARAM_VERSION  2u	//v2: deadzone 改 uint16_t

typedef struct
{
	uint8_t  version;	//结构版本号, 与 PARAM_VERSION 不符视为无效
	uint16_t deadzone;	//摇杆死区 0~512
	uint8_t  filter;	//滤波强度
	uint8_t  brightness;	//OLED 对比度
	uint8_t  idle_idx;	//熄屏索引 0=关 1=3s 2=5s 3=10s 4=30s
	uint8_t  mode;	//工作模式(0=游戏 1=蓝牙)
	uint8_t  bt_header;	//蓝牙包头
	uint8_t  bt_tail;	//蓝牙包尾
	uint8_t  bt_use_hex;	//蓝牙十六进制
	uint8_t  bt_use_newline;	//蓝牙换行
} PARAMS;

	//读取参数: 有效返回 1 并填充 p; 无效(首次/版本不符)返回 0, p 填默认值
uint8_t FlashSave_Load(PARAMS *p);

	//保存参数到 Flash(擦除+写入); 成功返回 1
uint8_t FlashSave_Save(const PARAMS *p);

#endif	//__FLASH_SAVE_H
