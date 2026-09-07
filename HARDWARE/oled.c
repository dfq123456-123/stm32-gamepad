/*******************************************************************************
 * oled.c - SSD1306 128x64 OLED 驱动(软件 I2C, SCL=PB12 SDA=PB13)
 ******************************************************************************/
#include "oled.h"
#include "font.h"

/*---------------------- 软件 I2C 引脚宏 ----------------------
 * SCL = PB12, SDA = PB13 (芯片物理引脚)
 * 注意: 原理图/板上丝印标为 PB8/PB9 —— 那是网络名, 走线到核心板排针 1/2 脚,
 *       即芯片 PB12/PB13。已由 PCB 网表 + 实物走线双向确认。
 *------------------------------------------------------------*/
#define OLED_SCL_PIN      GPIO_Pin_12
#define OLED_SDA_PIN      GPIO_Pin_13

#define OLED_SCL_HIGH()   GPIOB->BSRR = OLED_SCL_PIN
#define OLED_SCL_LOW()    GPIOB->BRR  = OLED_SCL_PIN
#define OLED_SDA_HIGH()   GPIOB->BSRR = OLED_SDA_PIN
#define OLED_SDA_LOW()    GPIOB->BRR  = OLED_SDA_PIN

/* SDA 方向切换(PB13 在 CRH 的 bit20~23)
 *   推挽输出 50MHz: CNF=00 MODE=11 -> 0x3
 *   输入上拉:       CNF=10 MODE=00 -> 0x8, 且 ODR=1 使能上拉            */
#define SDA_OUT_PP()      do { GPIOB->CRH = (GPIOB->CRH & ~(0xFu << 20)) | (0x3u << 20); } while (0)
#define SDA_IN_PU()       do { GPIOB->CRH = (GPIOB->CRH & ~(0xFu << 20)) | (0x8u << 20); \
                               GPIOB->BSRR = OLED_SDA_PIN; } while (0)

#define OLED_ADDR_DEF     0x78        /* SSD1306 默认写地址 (0x3C << 1) */

/* 当前 I2C 写地址(部分模块为 0x7A, 启动时自动探测) */
static uint8_t oled_addr = OLED_ADDR_DEF;

/*---------------------- 帧缓冲: 8 page x 128 列 ----------------------*/
static uint8_t framebuf[8][128];

/*---------------------- 软件 I2C 基本操作 ----------------------*/
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

static void i2c_stop(void)
{
    OLED_SDA_LOW();
    i2c_delay();
    OLED_SCL_HIGH();
    i2c_delay();
    OLED_SDA_HIGH();
    i2c_delay();
}

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
    /* 第9个时钟: 从机 ACK (释放 SDA 让从机拉低, 主机忽略 ACK) */
    OLED_SCL_LOW();
    i2c_delay();
    OLED_SDA_HIGH();
    i2c_delay();
    OLED_SCL_HIGH();
    i2c_delay();
    OLED_SCL_LOW();
    i2c_delay();
}

/* 带 ACK 检测的字节写(仅用于探测):
 * 第9个时钟把 SDA 切成输入上拉, 读从机是否拉低。
 * 返回 1 = 收到 ACK(总线上有该地址的设备) */
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
    SDA_IN_PU();                /* 释放 SDA, 由模块上拉电阻拉高 */
    i2c_delay();
    OLED_SCL_HIGH();
    i2c_delay();
    ack = (GPIOB->IDR & OLED_SDA_PIN) ? 0u : 1u;   /* 低电平 = ACK */
    OLED_SCL_LOW();
    i2c_delay();
    SDA_OUT_PP();               /* 切回推挽输出 */

    return ack;
}

/*---------------------- SSD1306 命令/数据写 ----------------------*/
static void oled_write_cmd(uint8_t cmd)
{
    i2c_start();
    i2c_write_byte(oled_addr);
    i2c_write_byte(0x00);   /* 控制字节: 0x00 = 后续为命令 */
    i2c_write_byte(cmd);
    i2c_stop();
}

/* 单字节数据写暂未使用(刷新走 OLED_Refresh 的整页批量写), 需要时在此补回 */

/*******************************************************************************
 * I2C 引脚初始化(幂等, 探测与正式初始化都会调用)
 ******************************************************************************/
void OLED_GPIOInit(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    gpio.GPIO_Pin   = OLED_SCL_PIN | OLED_SDA_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;   /* 推挽输出(模拟 I2C, 不依赖模块上拉) */
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    OLED_SCL_HIGH();
    OLED_SDA_HIGH();
}

/*******************************************************************************
 * 探测 I2C 总线上是否存在指定地址的设备
 * 返回 1 = 有应答(存在), 0 = 无应答
 ******************************************************************************/
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

void OLED_SetAddr(uint8_t addr)
{
    oled_addr = addr;
}

uint8_t OLED_GetAddr(void)
{
    return oled_addr;
}

/*******************************************************************************
 * 初始化: GPIO + 标准 SSD1306 初始化命令序列
 ******************************************************************************/
void OLED_Init(void)
{
    OLED_GPIOInit();

    /* 命令序列完全对齐中景园 0.96" IIC 官方例程(博主那套即基于此, 已实测点亮),
     * 含官方独有的 0xD8 0x05(关闭区域彩色模式), 对比度取官方的 0xFF。 */
    oled_write_cmd(0xAE);   /* 关显示 */
    oled_write_cmd(0x00);   /* 列地址低 4 位 */
    oled_write_cmd(0x10);   /* 列地址高 4 位 */
    oled_write_cmd(0x40);   /* 起始行 0 */
    oled_write_cmd(0xB0);   /* 页地址 0 */
    oled_write_cmd(0x81);   /* 对比度 */
    oled_write_cmd(0xFF);   /* 官方取 0xFF */
    oled_write_cmd(0xA1);   /* 段重映射 */
    oled_write_cmd(0xA6);   /* 正常显示(非反色) */
    oled_write_cmd(0xA8);   /* 复用率 */
    oled_write_cmd(0x3F);   /* 64 行 */
    oled_write_cmd(0xC8);   /* COM 扫描方向 */
    oled_write_cmd(0xD3);   /* 显示偏移 */
    oled_write_cmd(0x00);
    oled_write_cmd(0xD5);   /* 时钟分频 */
    oled_write_cmd(0x80);
    oled_write_cmd(0xD8);   /* 区域彩色模式关闭 */
    oled_write_cmd(0x05);
    oled_write_cmd(0xD9);   /* 预充电周期 */
    oled_write_cmd(0xF1);
    oled_write_cmd(0xDA);   /* COM 引脚配置 */
    oled_write_cmd(0x12);
    oled_write_cmd(0xDB);   /* VCOMH */
    oled_write_cmd(0x30);
    oled_write_cmd(0xA4);   /* 输出跟随 RAM */
    oled_write_cmd(0x8D);   /* 电荷泵使能 */
    oled_write_cmd(0x14);
    oled_write_cmd(0xAF);   /* 开显示 */

    OLED_Clear();
    OLED_Refresh();
}

/*******************************************************************************
 * 清帧缓冲
 ******************************************************************************/
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

/*******************************************************************************
 * 帧缓冲刷到屏(逐页写)
 ******************************************************************************/
void OLED_Refresh(void)
{
    uint8_t p, x;
    for (p = 0; p < 8; p++)
    {
        oled_write_cmd(0xB0 + p);   /* 页地址 */
        oled_write_cmd(0x10);       /* 列地址高 4 位(顺序同中景园例程) */
        oled_write_cmd(0x00);       /* 列地址低 4 位 */

        /* 批量写入 128 字节数据(一次 I2C 事务, 大幅减少开销) */
        i2c_start();
        i2c_write_byte(oled_addr);
        i2c_write_byte(0x40);       /* 控制字节: 0x40 = 后续为数据 */
        for (x = 0; x < 128; x++)
        {
            i2c_write_byte(framebuf[p][x]);
        }
        i2c_stop();
    }
}

/*******************************************************************************
 * 画点(x 0~127, y 0~63)
 ******************************************************************************/
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

/*******************************************************************************
 * 局部清帧缓冲(矩形, 像素坐标; 越界自动裁剪)
 * 注: 仅清零矩形覆盖到的整字节, 因此矩形高度应页对齐以避免误清相邻内容。
 ******************************************************************************/
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

/*******************************************************************************
 * 局部刷新(矩形, 像素坐标; 越界自动裁剪)
 * 仅写受影响的页/列, 数据量远小于全屏刷新 -> 动画更流畅。
 * 页地址模式: 逐页设置页 + 列起始, 再批量写该页内的列区间。
 ******************************************************************************/
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
        oled_write_cmd((uint8_t)(0xB0 + p));            /* 页地址 */
        oled_write_cmd((uint8_t)(0x10 | (c0 >> 4)));    /* 列地址高 4 位 */
        oled_write_cmd((uint8_t)(c0 & 0x0F));           /* 列地址低 4 位 */

        i2c_start();
        i2c_write_byte(oled_addr);
        i2c_write_byte(0x40);                            /* 控制字节: 数据 */
        for (x = c0; x <= c1; x++)
        {
            i2c_write_byte(framebuf[p][(uint8_t)x]);
        }
        i2c_stop();
    }
}

/*******************************************************************************
 * 8x16 ASCII 字符显示(x=列像素, line=行0~3)
 ******************************************************************************/
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

/*******************************************************************************
 * 8x16 ASCII 字符串显示
 ******************************************************************************/
void OLED_ShowString(uint8_t x, uint8_t line, const char *str)
{
    while (*str)
    {
        OLED_ShowChar(x, line, *str++);
        x += 8;
    }
}

/*******************************************************************************
 * 8x16 ASCII 字符串(像素坐标 x,y, 任意行; 用于测试界面等非 16px 网格对齐处)
 ******************************************************************************/
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

/*******************************************************************************
 * 16x16 中文显示(x=列像素, line=行0~3)
 ******************************************************************************/
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
        uint8_t g0 = CN_FONT[idx][row * 2];       /* 左 8 像素 */
        uint8_t g1 = CN_FONT[idx][row * 2 + 1];   /* 右 8 像素 */
        for (col = 0; col < 8; col++)
        {
            uint8_t b0 = (uint8_t)((g0 >> (7 - col)) & 0x01);
            uint8_t b1 = (uint8_t)((g1 >> (7 - col)) & 0x01);
            OLED_DrawPixel((uint8_t)(x + col),     (uint8_t)(y_base + row), b0);
            OLED_DrawPixel((uint8_t)(x + 8 + col), (uint8_t)(y_base + row), b1);
        }
    }
}

/*******************************************************************************
 * 矩形填充(x/y 为左上角像素坐标)
 ******************************************************************************/
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

/*******************************************************************************
 * 矩形边框(1px 轮廓, 像素坐标)
 ******************************************************************************/

/*******************************************************************************
 * 亮度(对比度)调节: SSD1306 命令 0x81 后跟 1 字节对比度(0~255)
 * 0xFF 最亮(默认), 0x00 最暗。命令寄存器在 0xAE/0xAF 开关显示时保持不变,
 * 故空闲熄屏再点亮后亮度仍生效。
 ******************************************************************************/
void OLED_SetContrast(uint8_t val)
{
    oled_write_cmd(0x81);
    oled_write_cmd(val);
}

/*******************************************************************************
 * 关显示(0xAE): 停止 OLED 驱动输出但保留 GDDRAM 帧缓冲, 用于空闲熄屏省电。
 * 有输入时再 OLED_DisplayOn(0xAF) 即可恢复最后画面。
 ******************************************************************************/
void OLED_DisplayOff(void)
{
    oled_write_cmd(0xAE);
}

void OLED_DisplayOn(void)
{
    oled_write_cmd(0xAF);
}
