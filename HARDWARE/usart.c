/*******************************************************************************
 * usart.c - F103 串口1 驱动 (PA9=TX, PA10=RX)
 *
 * F4 -> F103 移植要点:
 *  1. 时钟: RCC_AHB1PeriphClockCmd  ->  RCC_APB2PeriphClockCmd
 *     (USART1/GPIOA 都挂在 APB2 总线上, 72MHz)
 *  2. 引脚模式: F4 用 GPIO_Mode_AF + GPIO_PinAFConfig 两步绑定,
 *     F103 直接用 GPIO_Mode_AF_PP(TX) / GPIO_Mode_IN_FLOATING(RX)
 *  3. printf 重定向代码与 F4 参考工程完全相同 (半主机禁用方案)
 ******************************************************************************/
#include "usart.h"

volatile uint8_t bt_recv = 0;   /* 最近一次收到的字节 */

/*******************************************************************************
 * 函数名: MY_USART1_Init
 * 参数:   baud 波特率 (常用 9600 / 115200)
 * 说明:   初始化 USART1: PA9=TX(复用推挽), PA10=RX(浮空输入)
 *          开启 RXNE 接收中断, 抢占优先级 0
 ******************************************************************************/
void MY_USART1_Init(uint32_t baud)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef  NVIC_InitStructure;

    /* 1. 使能时钟: USART1 + GPIOA 都在 APB2 上 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);

    /* 2. 优先级分组 2 (2 位抢占 + 2 位响应) */
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);

    /* 3. 配置 PA9 = TX, 复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;      /* 复用推挽 */
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 4. 配置 PA10 = RX, 浮空输入 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING; /* 浮空输入 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 5. 配置串口参数 */
    USART_InitStructure.USART_BaudRate            = baud;
    USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits            = USART_StopBits_1;
    USART_InitStructure.USART_Parity              = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &USART_InitStructure);

    /* 6. 开接收中断 */
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    /* 7. NVIC: 中断通道 USART1_IRQn, 抢占 0 子 0 */
    NVIC_InitStructure.NVIC_IRQChannel                   = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    /* 8. 使能串口 */
    USART_Cmd(USART1, ENABLE);
}

/*******************************************************************************
 * 函数名: USART1_SendByte
 * 说明:   阻塞发送 1 字节, 等待 TC 标志置位 (参考工程写法)
 ******************************************************************************/
void USART1_SendByte(uint8_t ch)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
    USART_SendData(USART1, ch);
}

/*******************************************************************************
 * 函数名: USART1_SendString
 * 说明:   发送以 \0 结尾的字符串
 ******************************************************************************/
void USART1_SendString(uint8_t *str)
{
    while (*str)
    {
        USART1_SendByte(*str++);
    }
}

/*******************************************************************************
 * 函数名: USART1_SendBuffer
 * 说明:   发送 len 字节原始数据 (数据帧用, 注意非阻塞填满则等待)
 ******************************************************************************/
void USART1_SendBuffer(uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        USART1_SendByte(buf[i]);
    }
}

/*******************************************************************************
 * 函数名: BT_SendFrame
 * 说明:   组包并发送蓝牙数据帧(可配置包头/包尾/hex/换行, 由菜单设定)
 *
 * 帧结构(二进制): [包头] [axis0_L axis0_H ... axis5_L axis5_H] [btn_L btn_H] [包尾]
 *               共 1 + 12 + 2 + 1 = 16 字节
 *   use_hex     = 1 时, 上述 16 字节逐字节转成 32 个十六进制 ASCII 字符发送
 *   use_newline = 1 时, 帧尾追加 "\r\n"
 ******************************************************************************/
void BT_SendFrame(uint16_t axis[6], uint16_t btn_bitmap, BT_CONFIG *cfg)
{
    static const char hex_chars[] = "0123456789ABCDEF";
    uint8_t raw[16];
    uint8_t n = 0;
    uint8_t i;

    /* 1. 包头 */
    raw[n++] = cfg->header;

    /* 2. 6 路轴数据(低字节在前, 小端) */
    for (i = 0; i < 6; i++)
    {
        raw[n++] = (uint8_t)(axis[i] & 0xFF);
        raw[n++] = (uint8_t)((axis[i] >> 8) & 0xFF);
    }

    /* 3. 按键位图(低字节在前) */
    raw[n++] = (uint8_t)(btn_bitmap & 0xFF);
    raw[n++] = (uint8_t)((btn_bitmap >> 8) & 0xFF);

    /* 4. 包尾 */
    raw[n++] = cfg->tail;

    /* 5. 发送(可选 hex 编码) */
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

    /* 6. 可选换行 */
    if (cfg->use_newline)
    {
        USART1_SendByte('\r');
        USART1_SendByte('\n');
    }
}

/*******************************************************************************
 * 函数名: USART1_IRQHandler
 * 说明:   接收中断, 数据存入 bt_recv (与 day11/joystick 行为一致)
 ******************************************************************************/
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
    {
        bt_recv = USART_ReceiveData(USART1);
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

/*---------------------- printf 重定向 (半主机禁用) ----------------------*/
/* 与 F4 参考工程完全相同的写法, 保证 printf 走 USART1 输出 */
#pragma import(__use_no_semihosting_swi)

struct __FILE { int handle; };  /* 此处增加自己需要的内容 */
FILE __stdout;
FILE __stdin;

int fputc(int ch, FILE *f)
{
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET);
    USART_SendData(USART1, ch);
    return ch;
}

void _sys_exit(int return_code)
{
label:
    goto label;
}
