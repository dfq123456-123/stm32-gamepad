/*******************************************************************************
 * boot_splash.c - 开机动画实现(粒子聚集版, 局部刷新保证流畅)
 *
 * 显示布局(128x64 OLED):
 *   名字水印 "达芬奇w" 16x16 : 右下角(x=64, y=48), 从动画一开始就常驻
 *   头像 28x28              : 水平居中(x=50), 顶部(y=2), 由粒子从散开态聚集成形
 *   "肚子隆起来了" 16x16    : 整行居中(x=16, y=32), 头像聚完之后才逐字浮现
 *
 * 流畅要点:
 *   - 粒子只在"头像脏区"(约 44x40 像素, 5 个页)内清+刷, 数据量 ~1/5 全屏
 *   - 名字水印/文字各有独立小区域刷新, 互不干扰
 *   - 每帧用整数插值 + ease-out, 帧率稳定, 无卡顿
 ******************************************************************************/
#include "boot_splash.h"
#include "oled.h"
#include "delay.h"

/* 位图数据(见 boot_splash_data.c) */
extern const uint8_t g_face_bmp[BOOT_FACE_H * ((BOOT_FACE_W + 7) / 8)];
extern const uint8_t g_cn_duzi[BOOT_DUZI_NUM * 32];
extern const uint8_t g_cn_dfq[BOOT_DFQ_NUM * 32];

/*---------------------- 布局坐标 ----------------------*/
#define FX  ((128 - BOOT_FACE_W) / 2)        /* 头像左上 x = 48 */
#define FY  1U
#define TX  ((128 - BOOT_DUZI_NUM * 16U) / 2) /* 文字 x = 16 */
#define TY  34U
#define WX  (128 - BOOT_DFQ_NUM * 14U)        /* 名字水印 x = 72 */
#define WY  50U

/* 粒子脏区: 头像区(32x32) + 四周散开余量, 仅此区域刷新 */
#define PX0 (FX - 8U)
#define PY0 0U
#define PW  (BOOT_FACE_W + 16U)               /* 48 */
#define PHH 40U                                /* 页 0~4, 覆盖头像 y1~32 */

#define MAXP (BOOT_FACE_W * BOOT_FACE_H)       /* 粒子数上限 = 784 */

/*---------------------- 粒子状态 ----------------------*/
static uint8_t tx[MAXP], ty[MAXP];   /* 目标位置(头像上的点亮像素) */
static uint8_t sx[MAXP], sy[MAXP];   /* 起始散开位置 */
static uint8_t ph[MAXP];             /* 错峰相位 0~29 */
static uint16_t pcount = 0;
static uint32_t rng_state = 0x9E3779B9u;

static uint32_t rng(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

/* 通用单色位图绘制: 行优先, 每行 MSB 在前(左像素=高位), 1=点亮 */
static void draw_bitmap(uint8_t x0, uint8_t y0, uint8_t w, uint8_t h,
                        const uint8_t *bmp)
{
    uint8_t r, c;
    uint8_t stride = (w + 7) / 8;
    for (r = 0; r < h; r++)
    {
        for (c = 0; c < w; c++)
        {
            uint8_t on = (bmp[(uint16_t)r * stride + (c >> 3)] >> (7 - (c & 7))) & 0x01u;
            OLED_DrawPixel((uint8_t)(x0 + c), (uint8_t)(y0 + r), on);
        }
    }
}

/* 扫描头像位图, 收集所有点亮像素作为粒子目标, 并随机散布起始位置 */
static void build_particles(void)
{
    uint8_t ly, lx;
    uint8_t stride = (BOOT_FACE_W + 7) / 8;   /* 28 -> 4 */
    pcount = 0;
    for (ly = 0; ly < BOOT_FACE_H; ly++)
    {
        for (lx = 0; lx < BOOT_FACE_W; lx++)
        {
            uint8_t on = (g_face_bmp[(uint16_t)ly * stride + (lx >> 3)]
                          >> (7 - (lx & 7))) & 0x01u;
            if (on && pcount < MAXP)
            {
                uint16_t k = pcount;
                tx[k] = (uint8_t)(FX + lx);
                ty[k] = (uint8_t)(FY + ly);
                sx[k] = (uint8_t)(PX0 + (rng() % PW));
                sy[k] = (uint8_t)(PY0 + (rng() % PHH));
                ph[k] = (uint8_t)(rng() % 30);   /* 0~29, 错峰到达更自然 */
                pcount++;
            }
        }
    }
}

void Boot_Splash(void)
{
    uint8_t f;
    uint16_t i;              /* 必须 uint16_t: pcount 可达数百(>255), 否则 uint8_t 回卷死循环 */
    const uint8_t FRAMES  = 50;
    const uint8_t STEP_MS = 14;

    OLED_Clear();

    /* 1) 名字水印: 从最开始就显示在右下角(只刷其小区域, 之后一直常驻) */
    for (i = 0; i < BOOT_DFQ_NUM; i++)
    {
        draw_bitmap((uint8_t)(WX + i * 14U), WY, 14U, 14U,
                    g_cn_dfq + (uint16_t)i * 28U);
    }
    OLED_RefreshRect(WX, WY, BOOT_DFQ_NUM * 14U, 14U);

    /* 2) 构建粒子(起始散开在脏区内) */
    build_particles();

    /* 3) 粒子聚集: 每帧只清+刷脏区, 帧率稳定 -> 流畅无卡顿 */
    for (f = 0; f < FRAMES; f++)
    {
        uint8_t t = (uint8_t)((uint16_t)f * 100u / (FRAMES - 1));
        OLED_ClearRect(PX0, PY0, PW, PHH);
        for (i = 0; i < pcount; i++)
        {
            uint8_t local, e;
            int16_t dx, dy, px, py;
            if (t <= ph[i])
            {
                local = 0;
            }
            else
            {
                local = (uint8_t)(((uint16_t)(t - ph[i]) * 100u) / (100u - ph[i]));
                if (local > 100) local = 100;
            }
            /* ease-out: e = 100 - (100-local)^2 / 100 */
            e = (uint8_t)(100u - (uint16_t)(100u - local) * (100u - local) / 100u);
            dx = (int16_t)tx[i] - (int16_t)sx[i];
            dy = (int16_t)ty[i] - (int16_t)sy[i];
            px = (int16_t)sx[i] + (int16_t)((int32_t)dx * e / 100);
            py = (int16_t)sy[i] + (int16_t)((int32_t)dy * e / 100);
            OLED_DrawPixel((uint8_t)px, (uint8_t)py, 1);
        }
        OLED_RefreshRect(PX0, PY0, PW, PHH);
        delay_ms(STEP_MS);
    }

    /* 4) 头像聚完后, 再显示"肚子隆起来了"(逐字左->右浮现) */
    delay_ms(150);
    for (i = 0; i < BOOT_DUZI_NUM; i++)
    {
        draw_bitmap((uint8_t)(TX + i * 16U), TY, 16U, 16U,
                    g_cn_duzi + (uint16_t)i * 32U);
        OLED_RefreshRect(TX, TY, (uint8_t)((i + 1) * 16U), 16U);
        delay_ms(55);
    }

    /* 5) 停留展示 */
    delay_ms(1300);

    /* 清屏, 交给校准进度界面 */
    OLED_Clear();
    OLED_Refresh();
}
