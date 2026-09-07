/*******************************************************************************
 * menu.c - 双模式菜单系统 + 测试手柄界面
 *
 * 状态机: CLOSED -> OPEN(列表) -> TEST(测试手柄界面)
 * 导航: 十字键/左摇杆 上下移光标; 十字键左右调参或进测试(游戏模式 idx2); 返回仅 HOME(中间三键左侧)
 *
 * 游戏模式菜单: 死区 / 滤波 / 测试手柄
 * 蓝牙模式菜单: 包头 / 包尾 / 十六进制 / 换行
 ******************************************************************************/
#include "menu.h"
#include "joystick.h"
#include "adc.h"
#include "oled.h"
#include <string.h>

/*---------------------- 全局状态 ----------------------*/
static uint8_t    mode = MODE_GAME;
static BT_CONFIG  bt_cfg = { 0xAA, 0x55, 0, 1 };   /* 默认: 包头AA 包尾55 无hex 有换行 */

/* 亮度 / 空闲熄屏 / 连发(Turbo) 状态(可被菜单修改) */
static uint8_t oled_brightness = 0xFF;   /* 当前 OLED 对比度(默认最亮) */
static uint8_t idle_idx        = 0;      /* 空闲熄屏选项: 0=关 1=5s 2=10s 3=30s */
static uint8_t turbo_key_idx   = 0;      /* 连发目标键索引(见 turbo_keys) */
static uint8_t turbo_hz_idx    = 2;      /* 连发频率索引(见 turbo_hz, 默认 8Hz) */

/* 连发可选键(排除走 Hat 的十字键 UP/DOWN/LEFT/RIGHT, 它们不在 HID 报告位内): 0=关 */
static const uint8_t turbo_keys[] =
{
    0, KEY_A, KEY_B, KEY_X, KEY_Y, KEY_LB, KEY_RB, KEY_L3, KEY_R3, KEY_HOME, KEY_MENU, KEY_VIEW
};
/* 连发频率选项(Hz) */
static const uint8_t turbo_hz[] = { 2, 4, 8, 16 };
/* 空闲熄屏秒数(对应 idle_idx) */
static const uint8_t idle_secs[] = { 0, 5, 10, 30 };

/* 连发键英文短名(键号 -> 字符串) */
static const char *turbo_key_name(uint8_t key_id)
{
    switch (key_id)
    {
        case KEY_A:    return "A";
        case KEY_B:    return "B";
        case KEY_X:    return "X";
        case KEY_Y:    return "Y";
        case KEY_LB:   return "LB";
        case KEY_RB:   return "RB";
        case KEY_L3:   return "L3";
        case KEY_R3:   return "R3";
        case KEY_HOME: return "HOME";
        case KEY_MENU: return "MENU";
        case KEY_VIEW: return "VIEW";
        default:       return "";
    }
}

/* 左摇杆控制菜单光标的边沿状态 */
static uint8_t    stick_active = 0;

/* 左摇杆上下移动光标的触发阈值(ADC 偏差, 满量程约 ±2048) */
#define STICK_MOVE_TH   900

/* 摇杆 X/Y 交叉映射(90°旋转修正)已统一收口到 joystick.h 的 AXIS_CH_*, 此处不再重复定义 */

/* 左摇杆"垂直轴"的 ADC 通道: 与测试界面一致(垂直方向对应 VRX) */
static uint8_t stick_vert_ch(void)
{
    return AXIS_CH_LY;
}

/* 左摇杆垂直方向偏差(原始值, 无滤波延迟) */
static int16_t stick_vert_diff(void)
{
    uint8_t ch = stick_vert_ch();
    return (int16_t)Joystick_GetRaw(ch) - (int16_t)Joystick_GetCenter(ch);
}

typedef enum
{
    MENU_CLOSED = 0,
    MENU_OPEN,
    MENU_TEST          /* 测试手柄界面(实时刷新) */
} MENU_STATE;

static uint8_t menu_state = MENU_CLOSED;
static uint8_t menu_idx   = 0;      /* 当前菜单项索引 */
static uint16_t menu_dirty = 0;     /* 需要重绘标志 */

/*---------------------- 工具: 数字转字符串 ----------------------*/
static char *u2dec(uint16_t val, char *buf)
{
    char tmp[6];
    uint8_t i = 0, j = 0;
    if (val == 0)
    {
        tmp[i++] = '0';
    }
    while (val > 0)
    {
        tmp[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (i > 0)
    {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
    return buf;
}

static char *u2hex(uint8_t val, char *buf)
{
    static const char hex[] = "0123456789ABCDEF";
    buf[0] = hex[(val >> 4) & 0x0F];
    buf[1] = hex[val & 0x0F];
    buf[2] = '\0';
    return buf;
}

/*---------------------- 菜单项数量/取值/设置 ----------------------*/
static uint8_t item_count(void)
{
    /* 游戏模式 7 项(死区/滤波/亮度/关屏/连发键/连发速/测试手柄); 蓝牙模式 4 项 */
    return (mode == MODE_GAME) ? 7 : 4;
}

static uint16_t item_get(uint8_t idx)
{
    if (mode == MODE_GAME)
    {
        if (idx == 0) return Joystick_GetDeadzone();
        if (idx == 1) return Joystick_GetFilter();
        if (idx == 2) return oled_brightness;
        if (idx == 3) return idle_idx;
        if (idx == 4) return turbo_key_idx;
        if (idx == 5) return turbo_hz_idx;
        /* idx==6 是"测试手柄"动作项, 无取值 */
    }
    else
    {
        if (idx == 0) return bt_cfg.header;
        if (idx == 1) return bt_cfg.tail;
        if (idx == 2) return bt_cfg.use_hex;
        if (idx == 3) return bt_cfg.use_newline;
    }
    return 0;
}

static void item_set(uint8_t idx, uint16_t val)
{
    if (mode == MODE_GAME)
    {
        if (idx == 0) Joystick_SetDeadzone(val);
        if (idx == 1) Joystick_SetFilter((uint8_t)val);
        if (idx == 2) { oled_brightness = (uint8_t)val; OLED_SetContrast(oled_brightness); }
        if (idx == 3) { idle_idx = (uint8_t)val; }
        if (idx == 4) { turbo_key_idx = (uint8_t)val; Joystick_SetTurboKey(turbo_keys[turbo_key_idx]); }
        if (idx == 5) { turbo_hz_idx  = (uint8_t)val; Joystick_SetTurboHz(turbo_hz[turbo_hz_idx]); }
    }
    else
    {
        if (idx == 0) bt_cfg.header = (uint8_t)val;
        if (idx == 1) bt_cfg.tail   = (uint8_t)val;
        if (idx == 2) bt_cfg.use_hex = (uint8_t)(val & 1);
        if (idx == 3) bt_cfg.use_newline = (uint8_t)(val & 1);
    }
}

static void item_range(uint8_t idx, uint16_t *min, uint16_t *max, uint16_t *step)
{
    *min = 0; *max = 255; *step = 1;
    if (mode == MODE_GAME)
    {
        if (idx == 0) { *min = 0; *max = 512; *step = 10; }   /* 死区 */
        if (idx == 1) { *min = 0; *max = 8;   *step = 1;  }   /* 滤波 */
        if (idx == 2) { *min = 0; *max = 255; *step = 16; }   /* 亮度(对比度) */
        if (idx == 3) { *min = 0; *max = (uint16_t)(sizeof(idle_secs)/sizeof(idle_secs[0]) - 1); *step = 1; } /* 熄屏 */
        if (idx == 4) { *min = 0; *max = (uint16_t)(sizeof(turbo_keys)/sizeof(turbo_keys[0]) - 1); *step = 1; } /* 连发键 */
        if (idx == 5) { *min = 0; *max = (uint16_t)(sizeof(turbo_hz)/sizeof(turbo_hz[0]) - 1); *step = 1; } /* 连发速 */
    }
    else
    {
        if (idx == 2 || idx == 3) { *min = 0; *max = 1; *step = 1; }  /* 开关 */
    }
}

/* 直接调节当前菜单项参数: dir=-1 减一档, +1 加一档(列表态即可调, 无需进入编辑态) */
static void adjust_cur(int8_t dir)
{
    uint16_t mn, mx, st;
    int32_t  val;

    item_range(menu_idx, &mn, &mx, &st);
    if (st == 0)
    {
        st = 1;
    }

    val = (int32_t)item_get(menu_idx) + (int32_t)dir * (int32_t)st;
    if (val < (int32_t)mn) val = (int32_t)mn;
    if (val > (int32_t)mx) val = (int32_t)mx;

    item_set(menu_idx, (uint16_t)val);
}

/*---------------------- 菜单项绘制 ----------------------*/
/* 选中项前用 '>' 光标标识(替代反白高亮条) */
static void draw_item(uint8_t line, uint8_t idx, uint8_t selected)
{
    uint8_t x = 8;
    char buf[8];
    uint16_t v = item_get(idx);

    if (selected)
    {
        OLED_ShowChar(0, line, '>');
    }

    if (mode == MODE_GAME)
    {
        if (idx == 0)   /* 死区 */
        {
            OLED_ShowCN(x, line, CN_SI);   x += 16;
            OLED_ShowCN(x, line, CN_QU);   x += 16;
            OLED_ShowString(x, line, u2dec(v, buf));
        }
        else if (idx == 1)   /* 滤波强度 */
        {
            OLED_ShowCN(x, line, CN_LV);    x += 16;
            OLED_ShowCN(x, line, CN_BO);    x += 16;
            OLED_ShowCN(x, line, CN_QIANG); x += 16;
            OLED_ShowCN(x, line, CN_DU);    x += 16;
            OLED_ShowString(x, line, u2dec(v, buf));
        }
        else if (idx == 2)   /* 亮度(对比度) */
        {
            OLED_ShowCN(x, line, CN_LIANG); x += 16;
            OLED_ShowCN(x, line, CN_DU);    x += 16;
            OLED_ShowString(x, line, u2dec(v, buf));
        }
        else if (idx == 3)   /* 关屏(空闲熄屏) */
        {
            OLED_ShowCN(x, line, CN_GUAN); x += 16;
            OLED_ShowCN(x, line, CN_PING); x += 16;
            if (v == 0)
            {
                OLED_ShowCN(x, line, CN_GUAN);
            }
            else
            {
                buf[0] = (char)('0' + (idle_secs[v] / 10));
                buf[1] = (char)('0' + (idle_secs[v] % 10));
                buf[2] = 's';
                buf[3] = 0;
                OLED_ShowString(x, line, buf);
            }
        }
        else if (idx == 4)   /* 连发键 */
        {
            OLED_ShowCN(x, line, CN_LIAN); x += 16;
            OLED_ShowCN(x, line, CN_FA);   x += 16;
            if (v == 0)
            {
                OLED_ShowCN(x, line, CN_GUAN);
            }
            else
            {
                OLED_ShowString(x, line, turbo_key_name(turbo_keys[v]));
            }
        }
        else if (idx == 5)   /* 连发速度 */
        {
            OLED_ShowCN(x, line, CN_SU); x += 16;
            {
                uint8_t hz = turbo_hz[v];
                buf[0] = (char)('0' + (hz / 10));
                buf[1] = (char)('0' + (hz % 10));
                buf[2] = 'H'; buf[3] = 'z'; buf[4] = 0;
                OLED_ShowString(x, line, buf);
            }
        }
        else            /* idx==6: 测试手柄(动作项, 英文标签避免扩充中文字库) */
        {
            OLED_ShowString(x, line, "TEST");
        }
    }
    else
    {
        switch (idx)
        {
            case 0:     /* 包头 */
                OLED_ShowCN(x, line, CN_BAO);  x += 16;
                OLED_ShowCN(x, line, CN_TOU);  x += 16;
                OLED_ShowString(x, line, u2hex((uint8_t)v, buf));
                break;
            case 1:     /* 包尾 */
                OLED_ShowCN(x, line, CN_BAO);  x += 16;
                OLED_ShowCN(x, line, CN_WEI);  x += 16;
                OLED_ShowString(x, line, u2hex((uint8_t)v, buf));
                break;
            case 2:     /* 十六进制 */
                OLED_ShowCN(x, line, CN_SHI2); x += 16;
                OLED_ShowCN(x, line, CN_LIU);  x += 16;
                OLED_ShowCN(x, line, CN_JIN);  x += 16;
                OLED_ShowCN(x, line, CN_ZHI);  x += 16;
                if (v) OLED_ShowCN(x, line, CN_KAI);
                else   OLED_ShowCN(x, line, CN_GUAN);
                break;
            case 3:     /* 换行 */
                OLED_ShowCN(x, line, CN_HUAN); x += 16;
                OLED_ShowCN(x, line, CN_XING); x += 16;
                if (v) OLED_ShowCN(x, line, CN_KAI);
                else   OLED_ShowCN(x, line, CN_GUAN);
                break;
            default:
                break;
        }
    }
}

static void menu_draw(void)
{
    uint8_t cnt = item_count();
    uint8_t i, scroll;

    OLED_Clear();


    /* 滚动窗口: 保证 menu_idx 在可见的 3 行内 */
    scroll = 0;
    if (menu_idx >= 3)
    {
        scroll = menu_idx - 2;
    }

    for (i = 0; i < 3 && (scroll + i) < cnt; i++)
    {
        draw_item((uint8_t)(1 + i), (uint8_t)(scroll + i),
                  (scroll + i) == menu_idx);
    }
    OLED_Refresh();
}

/*---------------------- 模式切换提示 ----------------------*/
static void show_mode_banner(void)
{
    OLED_Clear();
    if (mode == MODE_GAME)
    {
        OLED_ShowCN(24, 1, CN_YOU_XI);   /* 游 */
        OLED_ShowCN(40, 1, CN_XI);       /* 戏 */
        OLED_ShowCN(56, 1, CN_MO);       /* 模 */
        OLED_ShowCN(72, 1, CN_SHI);      /* 式 */
    }
    else
    {
        OLED_ShowCN(24, 1, CN_LAN);      /* 蓝 */
        OLED_ShowCN(40, 1, CN_YA);       /* 牙 */
        OLED_ShowCN(56, 1, CN_MO);       /* 模 */
        OLED_ShowCN(72, 1, CN_SHI);      /* 式 */
    }
    OLED_Refresh();
}

/*---------------------- 测试手柄界面: 绘图基元 ----------------------*/
/* 空心方框(左上角 x,y, 宽 w 高 h) */
static void draw_box(uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
    uint8_t i;
    for (i = 0; i < w; i++)
    {
        OLED_DrawPixel((uint8_t)(x + i), y, 1);
        OLED_DrawPixel((uint8_t)(x + i), (uint8_t)(y + h - 1), 1);
    }
    for (i = 0; i < h; i++)
    {
        OLED_DrawPixel(x, (uint8_t)(y + i), 1);
        OLED_DrawPixel((uint8_t)(x + w - 1), (uint8_t)(y + i), 1);
    }
}

/* 实心圆(cx,cy 圆心, r 半径) */
static void draw_disc(uint8_t cx, uint8_t cy, uint8_t r)
{
    int16_t dx, dy;
    for (dy = -(int16_t)r; dy <= (int16_t)r; dy++)
    {
        for (dx = -(int16_t)r; dx <= (int16_t)r; dx++)
        {
            if (dx * dx + dy * dy <= (int16_t)r * (int16_t)r)
            {
                OLED_DrawPixel((uint8_t)((int16_t)cx + dx),
                               (uint8_t)((int16_t)cy + dy), 1);
            }
        }
    }
}

/* 中心十字标记(中性位置指示) */
static void draw_cross(uint8_t cx, uint8_t cy, uint8_t len)
{
    int16_t i;
    for (i = -(int16_t)len; i <= (int16_t)len; i++)
    {
        OLED_DrawPixel(cx, (uint8_t)((int16_t)cy + i), 1);
        OLED_DrawPixel((uint8_t)((int16_t)cx + i), cy, 1);
    }
}

/*---------------------- 测试手柄: 增量刷新状态(文件级, 供进入界面时重置) ------*/
/* 用屏幕像素坐标做判据: 像素量化后静止噪声不触发重画, 主循环保持 10ms, 三键跟手 */
static uint8_t  prev_lx = 0xFF, prev_ly = 0xFF;   /* 左摇杆圆点像素坐标 */
static uint8_t  prev_rx = 0xFF, prev_ry = 0xFF;   /* 右摇杆圆点像素坐标 */
static uint8_t  prev_fl = 0,    prev_fr = 0;       /* 左右扳机条填充宽度 */
static uint8_t  test_force = 1;     /* 1=强制全量重画(进入界面/首次) */

/* 进入测试手柄界面时调用: 重置增量刷新, 保证首帧必定重画
 * (否则 prev_* 残留上次值 + 摇杆未动 -> 直接 return, 屏幕停在菜单 = 假死) */
static void test_enter(void)
{
    Joystick_Calibrate_Ex(0);   /* 进测试界面前先重新校准中心 + 重置自适应量程(与开机一致) */
    prev_lx = 0xFF; prev_ly = 0xFF;
    prev_rx = 0xFF; prev_ry = 0xFF;
    prev_fl = 0;    prev_fr = 0;
    test_force = 1;
}

/*---------------------- 测试手柄界面绘制(每帧调用) ----------------------*/
/* 最简洁界面: 顶部左右独立扳机条(纯图形) + 中部左右摇杆框(中心十字 + 位置圆点), 无数字 */

static void test_draw(void)
{
    int16_t  nlx, nly, nrx, nry;   /* 归一化(±1024): 摇杆轴(自适应量程, 满推必到边界, 不写死±2048) */
    int16_t  ntl, ntr;            /* 扳机归一化(0~1024) */
    int16_t  new_lx, new_ly, new_rx, new_ry;  /* 位置圆点屏幕坐标 */
    uint8_t  new_fl, new_fr;      /* 扳机条填充宽度 */

    /* 布局: 顶部左右独立扳机条(关于 x=64 对称) + 中部左右摇杆框(关于 x=64 对称) */
    uint8_t by = 0, bh = 4;
    uint8_t bx_l = 6,  bx_r = 74, bw = 48;            /* 左/右扳机条 6..54 / 74..122 */
    uint8_t lx = 6,  ly = 8,  lw = 48, lh = 46;      /* 左摇杆框 6..54 / 8..53 */
    uint8_t rx = 74;                                   /* 右摇杆框 74..122, 关于 x=64 对称 */
    uint8_t cx_l, cy_l, cx_r, cy_r;
    int16_t hx, hy;                                     /* 像素半量程(留圆点半径) */

    cx_l = (uint8_t)(lx + lw / 2);
    cy_l = (uint8_t)(ly + lh / 2);
    cx_r = (uint8_t)(rx + lw / 2);
    cy_r = (uint8_t)(ly + lh / 2);
    hx = (int16_t)(lw / 2 - 4);   /* 20 */
    hy = (int16_t)(lh / 2 - 4);   /* 19 */

    /* 采集: 用滤波后归一化 GetNorm(与 HID 同源 joy_val), 自适应滤波移动时 n=1 跟手, 静止 n=3 去抖;
       屏幕与 HID 共用 AXIS_CH + 同一滤波数据源, 彻底消除相邻 ADC 通道(PA1↔PA2)残余串扰在屏幕上的表现 */
    nlx = Joystick_GetNorm(AXIS_CH_LX);
    nly = Joystick_GetNorm(AXIS_CH_LY);
    nrx = Joystick_GetNorm(AXIS_CH_RX);
    nry = Joystick_GetNorm(AXIS_CH_RY);
    ntl = Joystick_GetNormAbs(ADC_CH_LT);   /* 0~1024 */
    ntr = Joystick_GetNormAbs(ADC_CH_RT);

    new_lx = (int16_t)cx_l + (int16_t)((int32_t)nlx * hx / 1024);
    new_ly = (int16_t)cy_l + (int16_t)((int32_t)nly * hy / 1024);
    new_rx = (int16_t)cx_r + (int16_t)((int32_t)nrx * hx / 1024);
    new_ry = (int16_t)cy_r + (int16_t)((int32_t)nry * hy / 1024);
    {
        int32_t f;
        f = (int32_t)ntl * bw / 1024; new_fl = (f > bw) ? (uint8_t)bw : (uint8_t)f;
        f = (int32_t)ntr * bw / 1024; new_fr = (f > bw) ? (uint8_t)bw : (uint8_t)f;
    }

    /* 增量刷新: 屏幕像素坐标不变则跳过整屏重画(像素量化吸收 ADC 噪声, 主循环保 10ms, 三键跟手) */
    if (!test_force &&
        prev_lx == (uint8_t)new_lx && prev_ly == (uint8_t)new_ly &&
        prev_rx == (uint8_t)new_rx && prev_ry == (uint8_t)new_ry &&
        prev_fl == new_fl && prev_fr == new_fr)
    {
        return;
    }

    /* 全量重画(首帧或数据变化时) */
    OLED_Clear();

    /* 顶部: 左/右扳机条(实时行程, 纯图形, 无文字) */
    draw_box(bx_l, by, bw, bh);
    if (new_fl > 0)
        OLED_FillRect((uint8_t)(bx_l + 1), (uint8_t)(by + 1), new_fl, (uint8_t)(bh - 2), 1);
    draw_box(bx_r, by, bw, bh);
    if (new_fr > 0)
        OLED_FillRect((uint8_t)(bx_r + 1), (uint8_t)(by + 1), new_fr, (uint8_t)(bh - 2), 1);

    /* 左摇杆: 框 + 中心十字(中性位) + 位置圆点 */
    draw_box(lx, ly, lw, lh);
    draw_cross(cx_l, cy_l, 3);
    draw_disc((uint8_t)new_lx, (uint8_t)new_ly, 4);
    /* 右摇杆 */
    draw_box(rx, ly, lw, lh);
    draw_cross(cx_r, cy_r, 3);
    draw_disc((uint8_t)new_rx, (uint8_t)new_ry, 4);

    OLED_Refresh();

    prev_lx = (uint8_t)new_lx; prev_ly = (uint8_t)new_ly;
    prev_rx = (uint8_t)new_rx; prev_ry = (uint8_t)new_ry;
    prev_fl = new_fl; prev_fr = new_fr;
    test_force = 0;
}

/*---------------------- 初始化 ----------------------*/
void Menu_Init(void)
{
    mode = MODE_GAME;
    menu_state = MENU_CLOSED;
    menu_idx = 0;
    menu_dirty = 1;  /* 设为1让开机首帧显示模式横幅 */
}

/*---------------------- 主循环每帧调用 ----------------------*/
void Menu_Tick(void)
{
    /* ===== 0. 测试手柄界面(最高优先: 实时刷新; 中间三键 HOME/左键VIEW/右键MENU 均可返回) ===== */
    if (menu_state == MENU_TEST)
    {
        if (Joystick_KeyPressed(KEY_HOME) || Joystick_KeyPressed(KEY_VIEW) || Joystick_KeyPressed(KEY_MENU))
        {
            menu_state = MENU_OPEN;     /* 返回菜单列表(仅退出测试, 不切模式) */
            menu_dirty = 1;
        }
        else
        {
            test_draw();
        }
        return;
    }

    /* ===== 1. 中间三键(全局, MENU_CLOSED/MENU_OPEN 生效) ===== */
    if (Joystick_KeyPressed(KEY_VIEW))                  /* PB5: 切换模式 */
    {
        mode = (mode == MODE_GAME) ? MODE_BT : MODE_GAME;
        menu_state = MENU_CLOSED;   /* 切模式时退出菜单, 显示横幅 */
        menu_idx = 0;
        menu_dirty = 1;
    }
    else if (Joystick_KeyPressed(KEY_MENU))            /* PB4: 打开/关闭菜单 */
    {
        if (menu_state == MENU_CLOSED)
        {
            menu_state = MENU_OPEN;
            menu_idx = 0;
            menu_dirty = 1;
        }
        else
        {
            menu_state = MENU_CLOSED;
            menu_dirty = 1;
        }
    }
    else if (Joystick_KeyPressed(KEY_HOME))            /* PB3: 菜单返回 */
    {
        if (menu_state == MENU_OPEN) { menu_state = MENU_CLOSED; menu_dirty = 1; }
        /* MENU_CLOSED: HOME 无作用 */
    }

    /* ===== 3. 菜单列表态: 上下移光标 / 左右调参或进测试 / 返回仅 HOME ===== */
    if (menu_state == MENU_OPEN)
    {
        uint8_t cnt = item_count();
        int8_t  delta = 0;

        /* 3a. 十字键上下: 移动光标 */
        if (Joystick_KeyPressed(KEY_UP))         { delta = -1; }
        else if (Joystick_KeyPressed(KEY_DOWN))  { delta =  1; }

        /* 3b. 左摇杆上下: 移动光标(边沿触发, 回中后才能再触发一次)
         * 与测试界面同通道(AXIS_CH_LY): 推上 -> raw<center(sd<0) -> 光标上 */
        if (delta == 0)
        {
            int16_t sd = stick_vert_diff();
            if (sd >  STICK_MOVE_TH)      { if (!stick_active) { delta =  1; stick_active = 1; } }  /* 推下: 光标下 */
            else if (sd < -STICK_MOVE_TH) { if (!stick_active) { delta = -1; stick_active = 1; } }  /* 推上: 光标上 */
            else                          { stick_active = 0; }
        }
        else
        {
            stick_active = 1;   /* 十字键按下期间也抑制摇杆重复触发 */
        }

        if (delta != 0)
        {
            if (delta < 0)
            {
                menu_idx = (menu_idx == 0) ? (uint8_t)(cnt - 1) : (uint8_t)(menu_idx - 1);
            }
            else
            {
                menu_idx++;
                if (menu_idx >= cnt) menu_idx = 0;
            }
            menu_dirty = 1;
        }

        /* 3c. 十字键左右: 参数项直接调参; "测试手柄"项(游戏模式 idx6)左右即进入测试 */
        if (Joystick_KeyPressed(KEY_LEFT) || Joystick_KeyPressed(KEY_RIGHT))
        {
            if (mode == MODE_GAME && menu_idx == 6)
            {
                test_enter();
                menu_state = MENU_TEST;
                return;
            }
            adjust_cur(Joystick_KeyPressed(KEY_LEFT) ? (int8_t)-1 : (int8_t)1);
            menu_dirty = 1;
        }

        /* 3d/3e. 不再使用右侧 A/B 键: 进入测试由左右键(idx2)触发, 返回仅 HOME */
    }

    /* ===== 5. 重绘 ===== */
    if (menu_dirty)
    {
        menu_dirty = 0;
        if (menu_state == MENU_CLOSED)
        {
            show_mode_banner();
        }
        else
        {
            menu_draw();
        }
    }
}

/*---------------------- 访问接口 ----------------------*/
uint8_t Menu_GetMode(void)
{
    return mode;
}

BT_CONFIG *Menu_GetBTConfig(void)
{
    return &bt_cfg;
}

/*******************************************************************************
 * 直接进入"测试手柄"界面(开机校准完成后由 main 自动调用)
 ******************************************************************************/
void Menu_EnterTest(void)
{
    menu_state = MENU_TEST;
    menu_dirty = 0;   /* 测试界面走每帧 test_draw 实时刷新, 不依赖 dirty */
}

/*******************************************************************************
 * 空闲熄屏秒数(供 main 循环判定): 0=关闭, 否则 5/10/30 秒
 ******************************************************************************/
uint8_t Menu_GetIdleTimeout(void)
{
    return idle_secs[idle_idx];
}
