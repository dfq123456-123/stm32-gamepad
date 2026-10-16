//delay.c  SysTick 延时

#include "delay.h"

//毫秒延时: 重装载法, 每次重填 LOAD 然后等 COUNTFLAG
void delay_ms(uint32_t ms)
{
	while(ms--)
	{
		SysTick->CTRL=0;					//先关掉才能配寄存器
		SysTick->LOAD=72000-1;				//72MHz/1000=72000, 就是 1ms
		SysTick->VAL=0;						//清当前值, 顺带清计数标志
		SysTick->CTRL=5;					//使能, 时钟源选系统时钟
		while((SysTick->CTRL & (1<<16))==0);//等这次数完
		SysTick->CTRL=0;
	}
}

//微秒延时: 最大 233016us(SysTick 只有 24bit), 超了自动钳位
//以前 LOAD 直接填 0xFFFFFFFF, 实测会卡出 0.23s, 这里专门封顶
void delay_us(uint32_t us)
{
	if(us==0)return;
	if(us>233016u)us=233016u;

	SysTick->CTRL=0;
	SysTick->LOAD=72*us-1;					//72MHz, us 微秒
	SysTick->VAL=0;
	SysTick->CTRL=5;
	while((SysTick->CTRL & (1<<16))==0);
	SysTick->CTRL=0;
}
