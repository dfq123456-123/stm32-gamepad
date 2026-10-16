#include "oled.h"
#include "font.h"

#define OLED_SCL_PIN      GPIO_Pin_12
#define OLED_SDA_PIN      GPIO_Pin_13

#define OLED_SCL_HIGH()   GPIOB->BSRR = OLED_SCL_PIN
#define OLED_SCL_LOW()    GPIOB->BRR  = OLED_SCL_PIN
#define OLED_SDA_HIGH()   GPIOB->BSRR = OLED_SDA_PIN
#define OLED_SDA_LOW()    GPIOB->BRR  = OLED_SDA_PIN

//PB13 在 CRH bit20~23: 推挽50MHz=0x3, 输入上拉=0x8(ODR=1 使能上拉)
#define SDA_OUT_PP()      do { GPIOB->CRH = (GPIOB->CRH & ~(0xFu << 20)) | (0x3u << 20); } while (0)
#define SDA_IN_PU()       do { GPIOB->CRH = (GPIOB->CRH & ~(0xFu << 20)) | (0x8u << 20); \
							   GPIOB->BSRR = OLED_SDA_PIN; } while (0)

#define OLED_ADDR_DEF     0x78	 //SSD1306 默认(0x3C<<1), 部分模块为 0x7A

static uint8_t oled_addr = OLED_ADDR_DEF;
static uint8_t framebuf[8][128];	 //8 page x 128 列

//软件 I2C 时序延时, 6 个 NOP 撑够建立时间
static void i2c_delay(void)
{
	volatile uint8_t i;
	for (i = 0; i < 6; i++)
	{
		__NOP();
	}
}

static void i2c_start(void)
{
	OLED_SDA_HIGH();
	OLED_SCL_HIGH();
	i2c_delay();
	OLED_SDA_LOW();
	i2c_delay();
	OLED_SCL_LOW();
	i2c_delay();
}

//停止信号: SCL 高电平期间把 SDA 拉高
//停止信号: SCL 高电平期间把 SDA 拉高
static void i2c_stop(void)
{
	OLED_SDA_LOW();
	i2c_delay();
	OLED_SCL_HIGH();
	i2c_delay();
	OLED_SDA_HIGH();
	i2c_delay();
}

//写一字节, 高位在前; 第9个时钟忽略 ACK(正常写不查应答)
static void i2c_write_byte(uint8_t byte)
{
	uint8_t i;
	for (i = 0; i < 8; i++)
	{
		OLED_SCL_LOW();
		i2c_delay();
		if (byte & 0x80)
		{
			OLED_SDA_HIGH();
		}
		else
		{
			OLED_SDA_LOW();
		}
		i2c_delay();
		OLED_SCL_HIGH();
		i2c_delay();
		byte <<= 1;
	}
	//第9个时钟: 从机 ACK(主机忽略)
	OLED_SCL_LOW();
	i2c_delay();
	OLED_SDA_HIGH();
	i2c_delay();
	OLED_SCL_HIGH();
	i2c_delay();
	OLED_SCL_LOW();
	i2c_delay();
}

//写一字节并读 ACK: 第9个时钟把 SDA 切输入上拉, 从机拉低=有设备
static uint8_t i2c_write_byte_ack(uint8_t byte)
{
	uint8_t i;
	uint8_t ack;

	for (i = 0; i < 8; i++)
	{
		OLED_SCL_LOW();
		i2c_delay();
		if (byte & 0x80)
		{
			OLED_SDA_HIGH();
		}
		else
		{
			OLED_SDA_LOW();
		}
		i2c_delay();
		OLED_SCL_HIGH();
		i2c_delay();
		byte <<= 1;
	}

	OLED_SCL_LOW();
	i2c_delay();
	SDA_IN_PU();
	i2c_delay();
	OLED_SCL_HIGH();
	i2c_delay();
	ack = (GPIOB->IDR & OLED_SDA_PIN) ? 0u : 1u;	 //低=ACK
	OLED_SCL_LOW();
	i2c_delay();
	SDA_OUT_PP();

	return ack;
}

//写命令(控制字节 0x00)
static void oled_write_cmd(uint8_t cmd)
{
	i2c_start();
	i2c_write_byte(oled_addr);
	i2c_write_byte(0x00);	 //控制字节: 命令
	i2c_write_byte(cmd);
	i2c_stop();
}

//SCL=PB12 SDA=PB13 推挽输出, 初始化完先都拉高
void OLED_GPIOInit(void)
{
	GPIO_InitTypeDef gpio;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

	gpio.GPIO_Pin   = OLED_SCL_PIN | OLED_SDA_PIN;
	gpio.GPIO_Mode  = GPIO_Mode_Out_PP;	 //推挽(不依赖模块上拉)
	gpio.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &gpio);

	OLED_SCL_HIGH();
	OLED_SDA_HIGH();
}

//探地址: 发一字节看有没有 ACK, 1=总线上有这个设备
uint8_t OLED_Probe(uint8_t addr)
{
	uint8_t ack;
	uint8_t old = oled_addr;

	OLED_GPIOInit();
	oled_addr = addr;
	i2c_start();
	ack = i2c_write_byte_ack(addr);
	i2c_stop();
	oled_addr = old;

	return ack;
}

//切 I2C 地址, 探测完确认是 0x7A 才改
void OLED_SetAddr(uint8_t addr)
{
	oled_addr = addr;
}

uint8_t OLED_GetAddr(void)
{
	return oled_addr;
}

void OLED_Init(void)
{
	OLED_GPIOInit();

	//照抄中景园 0.96" IIC 例程的初始化序列(含 0xD8 0x05 关区域彩色)
	oled_write_cmd(0xAE);
	oled_write_cmd(0x00);
	oled_write_cmd(0x10);
	oled_write_cmd(0x40);
	oled_write_cmd(0xB0);
	oled_write_cmd(0x81);
	oled_write_cmd(0xFF);
	oled_write_cmd(0xA1);
	oled_write_cmd(0xA6);
	oled_write_cmd(0xA8);
	oled_write_cmd(0x3F);
	oled_write_cmd(0xC8);
	oled_write_cmd(0xD3);
	oled_write_cmd(0x00);
	oled_write_cmd(0xD5);
	oled_write_cmd(0x80);
	oled_write_cmd(0xD8);
	oled_write_cmd(0x05);
	oled_write_cmd(0xD9);
	oled_write_cmd(0xF1);
	oled_write_cmd(0xDA);
	oled_write_cmd(0x12);
	oled_write_cmd(0xDB);
	oled_write_cmd(0x30);
	oled_write_cmd(0xA4);
	oled_write_cmd(0x8D);
	oled_write_cmd(0x14);
	oled_write_cmd(0xAF);

	OLED_Clear();
	OLED_Refresh();
}

//清显存, 不刷屏(要配 OLED_Refresh 才显示出来)
void OLED_Clear(void)
{
	uint8_t p, x;
	for (p = 0; p < 8; p++)
	{
		for (x = 0; x < 128; x++)
		{
			framebuf[p][x] = 0;
		}
	}
}

//整屏刷: 8 个 page 一个一个写过去
void OLED_Refresh(void)
{
	uint8_t p, x;
	for (p = 0; p < 8; p++)
	{
		oled_write_cmd(0xB0 + p);
		oled_write_cmd(0x10);
		oled_write_cmd(0x00);

		i2c_start();
		i2c_write_byte(oled_addr);
		i2c_write_byte(0x40);	 //控制字节: 数据
		for (x = 0; x < 128; x++)
		{
			i2c_write_byte(framebuf[p][x]);
		}
		i2c_stop();
	}
}

//画点: y>>3 定 page, y&7 定页内位
void OLED_DrawPixel(uint8_t x, uint8_t y, uint8_t on)
{
	if (x >= 128 || y >= 64)
	{
		return;
	}
	if (on)
	{
		framebuf[y >> 3][x] |=  (uint8_t)(1u << (y & 0x07));
	}
	else
	{
		framebuf[y >> 3][x] &= (uint8_t)~(1u << (y & 0x07));
	}
}

//清矩形(只改显存, 越界部分自动裁掉)
void OLED_ClearRect(uint8_t x0, uint8_t y0, uint8_t w, uint8_t h)
{
	int16_t p, x;
	int16_t p0, p1, c0, c1;

	if (x0 >= 128 || y0 >= 64 || w == 0 || h == 0)
	{
		return;
	}
	c0 = x0;  c1 = x0 + w - 1;  if (c1 > 127) c1 = 127;
	p0 = y0 >> 3; p1 = (y0 + h - 1) >> 3; if (p1 > 7) p1 = 7;

	for (p = p0; p <= p1; p++)
	{
		for (x = c0; x <= c1; x++)
		{
			framebuf[p][(uint8_t)x] = 0;
		}
	}
}

//只刷矩形盖住的那几个 page, 比整屏刷省时间
void OLED_RefreshRect(uint8_t x0, uint8_t y0, uint8_t w, uint8_t h)
{
	int16_t p, x;
	int16_t p0, p1, c0, c1;

	if (x0 >= 128 || y0 >= 64 || w == 0 || h == 0)
	{
		return;
	}
	c0 = x0;  c1 = x0 + w - 1;  if (c1 > 127) c1 = 127;
	p0 = y0 >> 3; p1 = (y0 + h - 1) >> 3; if (p1 > 7) p1 = 7;

	for (p = p0; p <= p1; p++)
	{
		oled_write_cmd((uint8_t)(0xB0 + p));
		oled_write_cmd((uint8_t)(0x10 | (c0 >> 4)));
		oled_write_cmd((uint8_t)(c0 & 0x0F));

		i2c_start();
		i2c_write_byte(oled_addr);
		i2c_write_byte(0x40);
		for (x = c0; x <= c1; x++)
		{
			i2c_write_byte(framebuf[p][(uint8_t)x]);
		}
		i2c_stop();
	}
}

//8x16 ASCII, line 0~3, 越界或不 printable 直接不画
void OLED_ShowChar(uint8_t x, uint8_t line, char ch)
{
	uint8_t c, row, col;
	uint16_t y_base = (uint16_t)line * 16;

	if (line > 3 || ch < 0x20 || ch > 0x7E)
	{
		return;
	}
	c = (uint8_t)(ch - 0x20);

	for (row = 0; row < 16; row++)
	{
		uint8_t glyph = FONT8x16[c][row];
		for (col = 0; col < 8; col++)
		{
			uint8_t bit = (uint8_t)((glyph >> (7 - col)) & 0x01);
			OLED_DrawPixel((uint8_t)(x + col), (uint8_t)(y_base + row), bit);
		}
	}
}

//按行显示字符串, 一个字符占 8 列
void OLED_ShowString(uint8_t x, uint8_t line, const char *str)
{
	while (*str)
	{
		OLED_ShowChar(x, line, *str++);
		x += 8;
	}
}

//按像素 y 显示, 不受 16 像素行对齐限制
void OLED_ShowStringXY(uint8_t x, uint8_t y, const char *str)
{
	uint8_t c, row, col;
	while (*str)
	{
		char ch = *str++;
		if (ch < 0x20 || ch > 0x7E)
		{
			x += 8;
			continue;
		}
		c = (uint8_t)(ch - 0x20);
		for (row = 0; row < 16; row++)
		{
			uint8_t glyph = FONT8x16[c][row];
			for (col = 0; col < 8; col++)
			{
				if ((uint8_t)((glyph >> (7 - col)) & 0x01))
					OLED_DrawPixel((uint8_t)(x + col), (uint8_t)(y + row), 1);
			}
		}
		x += 8;
	}
}

//16x16 中文, idx 是字模下标
void OLED_ShowCN(uint8_t x, uint8_t line, uint8_t idx)
{
	uint8_t row, col;
	uint16_t y_base = (uint16_t)line * 16;

	if (line > 3 || idx >= CN_FONT_NUM)
	{
		return;
	}

	for (row = 0; row < 16; row++)
	{
		uint8_t g0 = CN_FONT[idx][row * 2];
		uint8_t g1 = CN_FONT[idx][row * 2 + 1];
		for (col = 0; col < 8; col++)
		{
			uint8_t b0 = (uint8_t)((g0 >> (7 - col)) & 0x01);
			uint8_t b1 = (uint8_t)((g1 >> (7 - col)) & 0x01);
			OLED_DrawPixel((uint8_t)(x + col),     (uint8_t)(y_base + row), b0);
			OLED_DrawPixel((uint8_t)(x + 8 + col), (uint8_t)(y_base + row), b1);
		}
	}
}

//填充矩形
void OLED_FillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint8_t on)
{
	uint8_t i, j;
	for (j = 0; j < h; j++)
	{
		for (i = 0; i < w; i++)
		{
			OLED_DrawPixel((uint8_t)(x + i), (uint8_t)(y + j), on);
		}
	}
}

//对比度 0~255; 关显示(0xAE)时寄存器保持, 再点亮亮度还在
void OLED_SetContrast(uint8_t val)
{
	oled_write_cmd(0x81);
	oled_write_cmd(val);
}

void OLED_DisplayOff(void)
{
	oled_write_cmd(0xAE);	 //保留 GDDRAM, 再 On 可恢复画面
}

//开显示
void OLED_DisplayOn(void)
{
	oled_write_cmd(0xAF);
}
