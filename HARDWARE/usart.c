#include "usart.h"

volatile uint8_t bt_recv = 0;	 //最近收到的字节

//串口1 初始化: PA9=TX PA10=RX, 开接收中断
void MY_USART1_Init(uint32_t baud)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef  NVIC_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);	 //2位抢占 + 2位响应

	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	USART_InitStructure.USART_BaudRate            = baud;
	USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits            = USART_StopBits_1;
	USART_InitStructure.USART_Parity              = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART1, &USART_InitStructure);

	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	USART_Cmd(USART1, ENABLE);
}

//TC 等待上限: 115200 下一字节约 87us
#define USART_TC_TIMEOUT  1000000u

//等上一个字节发完
static void usart1_wait_tc(void)
{
	uint32_t t = 0;
	while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET)
	{
		if (++t > USART_TC_TIMEOUT)
		{
			return;
		}
	}
}

//发一个字节
void USART1_SendByte(uint8_t ch)
{
	usart1_wait_tc();
	USART_SendData(USART1, ch);
}

//发字符串, 遇 0 结束
void USART1_SendString(uint8_t *str)
{
	while (*str)
	{
		USART1_SendByte(*str++);
	}
}

//发定长缓冲
void USART1_SendBuffer(uint8_t *buf, uint16_t len)
{
	uint16_t i;
	for (i = 0; i < len; i++)
	{
		USART1_SendByte(buf[i]);
	}
}

//组蓝牙帧: 包头+6轴+按键+包尾
//use_hex=1 时转 ASCII 十六进制
void BT_SendFrame(uint16_t axis[6], uint16_t btn_bitmap, BT_CONFIG *cfg)
{
	static const char hex_chars[] = "0123456789ABCDEF";
	uint8_t raw[16];
	uint8_t n = 0;
	uint8_t i;

	raw[n++] = cfg->header;
	for (i = 0; i < 6; i++)	 //6 路轴, 低字节在前
	{
		raw[n++] = (uint8_t)(axis[i] & 0xFF);
		raw[n++] = (uint8_t)((axis[i] >> 8) & 0xFF);
	}
	raw[n++] = (uint8_t)(btn_bitmap & 0xFF);
	raw[n++] = (uint8_t)((btn_bitmap >> 8) & 0xFF);
	raw[n++] = cfg->tail;

	if (cfg->use_hex)
	{
		for (i = 0; i < n; i++)
		{
			USART1_SendByte((uint8_t)hex_chars[(raw[i] >> 4) & 0x0F]);
			USART1_SendByte((uint8_t)hex_chars[raw[i] & 0x0F]);
		}
	}
	else
	{
		USART1_SendBuffer(raw, n);
	}

	if (cfg->use_newline)
	{
		USART1_SendByte('\r');
		USART1_SendByte('\n');
	}
}

//ORE 清标志
void USART1_IRQHandler(void)
{
	if (USART_GetITStatus(USART1, USART_IT_ORE) == SET)
	{
		(void)USART_ReceiveData(USART1);
		return;
	}
	if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
	{
		bt_recv = USART_ReceiveData(USART1);
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	}
}

//printf 重定向到串口1
#pragma import(__use_no_semihosting_swi)

struct __FILE { int handle; };
FILE __stdout;
FILE __stdin;

int fputc(int ch, FILE *f)
{
	usart1_wait_tc();
	USART_SendData(USART1, ch);
	return ch;
}

void _sys_exit(int return_code)
{
label:
	goto label;
}
