#include "stm32f10x.h"
#include "stdio.h"
#include "usart.h"
#include "adc.h"
#include "joystick.h"
#include "delay.h"
#include "oled.h"
#include "menu.h"
#include "boot_splash.h"
#include "hw_config.h"
#include "usb_lib.h"
#include "usb_pwr.h"

//主循环 10ms 一帧
int main(void)
{
	uint16_t axis[ADC_CH_NUM];
	uint16_t btn_bitmap;

	//USB 初始化(48MHz)
	Set_System();
	USB_Interrupts_Config();
	Set_USBClock();
	USB_Init();

	MY_USART1_Init(115200);	 //串口1 接 HC-05/06 蓝牙模块
	MY_Joystick_Init();

	//延时等 OLED 上电稳定
	delay_ms(800);
	{
		uint8_t ack78, ack7A;

		ack78 = OLED_Probe(0x78);
		ack7A = OLED_Probe(0x7A);
		printf("OLED probe: 0x78=%u 0x7A=%u\r\n",
			   (unsigned int)ack78, (unsigned int)ack7A);
		if (ack7A && !ack78)
		{
			OLED_SetAddr(0x7A);
			printf("OLED: using I2C addr 0x7A\r\n");
		}
		else if (!ack78 && !ack7A)
		{
			printf("WARNING: OLED not found on I2C bus, check wiring/power\r\n");
		}
	}

	OLED_Init();
	Boot_Splash();
	Menu_Init();
	Joystick_Calibrate_Ex(0);	 //开机自动校准(无进度 UI)
	Menu_EnterTest();			 //校准完直接进测试界面
	printf("STM32F103 GamePad Ready! USB HID + BT dual mode.\r\n");

	{
		static uint32_t idle_acc = 0;	 //无活动累计时间(ms)
		static uint8_t  oled_off = 0;	 //空闲熄屏状态

		while (1)
		{
			Joystick_Scan();

			//空闲熄屏
			{
				uint8_t active = Joystick_IsActive();
				if (active)
				{
					idle_acc = 0;
					if (oled_off)
					{
						OLED_DisplayOn();
						Menu_ForceRedraw();	 //清掉熄屏期间残留的坏像素
						oled_off = 0;
					}
				}
				else if (!oled_off)
				{
					uint8_t to = Menu_GetIdleTimeout();
					if (to != 0)
					{
						idle_acc += 10;
						if (idle_acc >= (uint32_t)to * 1000)
						{
							OLED_DisplayOff();
							oled_off = 1;
						}
					}
				}
			}

			Menu_Tick();
			Joystick_GetProcessed(axis, &btn_bitmap);

			if (Menu_GetMode() == MODE_GAME)
			{
				//游戏模式发 HID
				if ((bDeviceState == CONFIGURED) && PrevXferComplete)
				{
					Joystick_Send();
				}
			}
			else
			{
				BT_SendFrame(axis, btn_bitmap, Menu_GetBTConfig());
			}

			delay_ms(10);
		}
	}
}
