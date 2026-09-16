/*******************************************************************************
 * joystick.c - STM32F103C8T6 游戏手柄采集核心(根据反汇编逆向结果重写)
 *
 * 扫描时序: 主循环 10ms 一帧调用 Joystick_Scan()
 * 消抖策略: 按键电平连续 5 帧(50ms)相同才确认翻转
 ******************************************************************************/
#include "joystick.h"
#include "adc.h"

/*---------------------- 按键表(权威顺序, 键号=1基=HID位序) ----------------------*/
typedef struct
{
    GPIO_TypeDef *port;     /* GPIO 端口 */
    uint16_t      pin;      /* 引脚 */
    uint8_t       key_id;   /* 键号 1~15 */
    uint8_t       state;    /* 当前确认状态: 1=按下 */
    uint8_t       cnt;      /* 消抖计数 */
} KEY_T;

static KEY_T key_tbl[KEY_NUM] =
{
    {GPIOB, GPIO_Pin_11, KEY_UP,    0, 0},   /* 1  UP    PB11 */
    {GPIOB, GPIO_Pin_1,  KEY_DOWN,  0, 0},   /* 2  DOWN  PB1  */
    {GPIOB, GPIO_Pin_0,  KEY_RIGHT, 0, 0},   /* 3  RIGHT PB0  */
    {GPIOB, GPIO_Pin_10, KEY_LEFT,  0, 0},   /* 4  LEFT  PB10 */
    {GPIOB, GPIO_Pin_9,  KEY_X,     0, 0},   /* 5  X     PB9  */
    {GPIOB, GPIO_Pin_6,  KEY_Y,     0, 0},   /* 6  Y     PB6  */
    {GPIOB, GPIO_Pin_7,  KEY_B,     0, 0},   /* 7  B     PB7  */
    {GPIOB, GPIO_Pin_8,  KEY_A,     0, 0},   /* 8  A     PB8  */
    {GPIOB, GPIO_Pin_3,  KEY_HOME,  0, 0},   /* 9  Home  PB3  */
    {GPIOB, GPIO_Pin_4,  KEY_MENU,  0, 0},   /* 10 Menu  PB4  */
    {GPIOB, GPIO_Pin_5,  KEY_VIEW,  0, 0},   /* 11 View  PB5  */
    {GPIOA, GPIO_Pin_6,  KEY_R3,    0, 0},   /* 12 R3    PA6  */
    {GPIOA, GPIO_Pin_7,  KEY_L3,    0, 0},   /* 13 L3    PA7  */
    {GPIOB, GPIO_Pin_15, KEY_LB,    0, 0},   /* 14 LB    PB15 */
    {GPIOB, GPIO_Pin_14, KEY_RB,    0, 0},   /* 15 RB    PB14 */
};

/* 按键 → HID 报告位 重映射: 符合 XInput/标准手柄序(按钮1=A,2=B,3=X,4=Y,5=LB,6=RB...),
 * 否则 Steam/PC 会把"按钮1(我们的十字键上)"误当成 A。十字键同时仍走 Hat Switch(方向键)。 */
static const uint8_t report_btn_bit[KEY_NUM + 1] =
{
    0,              /* 索引0未用 */
    0xFF,           /* KEY_UP=1     -> 退出按钮位图: 十字键仅走 Hat Switch(POV), 避免与按钮重复上报致 Steam 绑定时抢占 */
    0xFF,           /* KEY_DOWN=2   -> 退出按钮位图: 同上, 仅走 Hat */
    0xFF,           /* KEY_RIGHT=3  -> 退出按钮位图: 同上, 仅走 Hat */
    0xFF,           /* KEY_LEFT=4   -> 退出按钮位图: 同上, 仅走 Hat */
    2,              /* KEY_X=5      -> 位2  (XInput X = 按钮3) */
    3,              /* KEY_Y=6      -> 位3  (Y = 按钮4) */
    1,              /* KEY_B=7      -> 位1  (B = 按钮2) */
    0,              /* KEY_A=8      -> 位0  (A = 按钮1) */
    6,              /* KEY_HOME=9   -> 位6  (Guide) */
    7,              /* KEY_MENU=10  -> 位7  (Start) */
    8,              /* KEY_VIEW=11  -> 位8  (Back) */
    10,             /* KEY_R3=12    -> 位10 */
    9,              /* KEY_L3=13    -> 位9 */
    4,              /* KEY_LB=14    -> 位4  (LB = 按钮5) */
    5,              /* KEY_RB=15    -> 位5  (RB = 按钮6) */
};

/*---------------------- 全局状态 ----------------------*/
static uint16_t joy_center[ADC_CH_NUM];    /* 校准中心值 */
static uint16_t joy_raw[ADC_CH_NUM];       /* 原始 ADC 值 */
static uint16_t joy_val[ADC_CH_NUM];       /* 滤波+死区后值 */
static uint16_t ax_lo[ADC_CH_NUM];         /* 自适应量程: 观测到的最小原始值 */
static uint16_t ax_hi[ADC_CH_NUM];         /* 自适应量程: 观测到的最大原始值 */
static uint16_t btn_bitmap;                /* 15 位按键位图, 已按 XInput 序重映射: bit = report_btn_bit(键号) */
static uint8_t  prev_state[KEY_NUM];       /* 上一帧按键状态(边沿检测) */

static uint16_t joy_deadzone = JOY_DEADZONE_DEFAULT;  /* 死区(可被菜单修改) */
static uint8_t  joy_filter  = JOY_FILTER_DEFAULT;     /* 滤波强度(可被菜单修改) */
static uint8_t  stable_cnt = 0;                       /* 动态校准静止计数 */

/*******************************************************************************
 * 初始化: 按键全部上拉输入(按下=低) + ADC1 + 上电自动校准
 ******************************************************************************/
void MY_Joystick_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    uint8_t i;

    /* 1. 使能 GPIOA + GPIOB + AFIO 时钟 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);

    /* 1.5 禁用 JTAG 释放 PB3(JTDO)/PB4(NJTRST), 保留 SWD(PA13/PA14) 可继续烧录调试 */
    GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

    /* 2. 按键引脚: 上拉输入 */
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    for (i = 0; i < KEY_NUM; i++)
    {
        GPIO_InitStructure.GPIO_Pin = key_tbl[i].pin;
        GPIO_Init(key_tbl[i].port, &GPIO_InitStructure);
    }

    /* 3. 初始化 ADC1 (PA0~PA5) */
    MY_ADC_Init();

    /* 4. 上电自动中心校准(由 main 在 OLED 就绪后调用 Joystick_Calibrate_Ex,
     *    以便屏幕显示校准进度; 此处不再内部校准, 避免重复) */
}

/*******************************************************************************
 * 上电中心校准: 每路采样 JOY_CAL_NUM(100) 次取平均, 写入 joy_center
 ******************************************************************************/
void Joystick_Calibrate(void)
{
    Joystick_Calibrate_Ex(0);
}

/*******************************************************************************
 * 带进度回调的校准: 每完成 10 个样本回调一次 cb(percent 0~100)
 * cb 传 0 则不回调(与 Joystick_Calibrate 等价)
 ******************************************************************************/
void Joystick_Calibrate_Ex(Joystick_CalProgressCB cb)
{
    uint8_t  ch, i;
    uint32_t sum[ADC_CH_NUM] = {0};

    for (i = 0; i < JOY_CAL_NUM; i++)
    {
        for (ch = 0; ch < ADC_CH_NUM; ch++)
        {
            sum[ch] += ADC_GetValue(ch);
        }
        /* 每 10 个样本(10%)回报一次进度 */
        if (cb != 0 && ((i + 1) % 10) == 0)
        {
            cb((uint8_t)(((i + 1) * 100) / JOY_CAL_NUM));
        }
    }
    for (ch = 0; ch < ADC_CH_NUM; ch++)
    {
        joy_center[ch] = (uint16_t)(sum[ch] / JOY_CAL_NUM);
        joy_val[ch]    = joy_center[ch];
        ax_lo[ch]      = joy_center[ch];   /* 量程从头学: 观测范围重置为单点 */
        ax_hi[ch]      = joy_center[ch];
    }
    if (cb != 0)
    {
        cb(100);   /* 完成 */
    }
}

/*******************************************************************************
 * 一阶低通滤波: val = (val*(2^n-1) + raw) >> n, n=滤波强度(0=无滤波)
 ******************************************************************************/
static uint16_t lowpass(uint16_t val, uint16_t raw, uint8_t n)
{
    if (n == 0)
    {
        return raw;
    }
    return (uint16_t)(((uint32_t)val * ((1u << n) - 1) + raw) >> n);
}

/*******************************************************************************
 * 扫描一帧: 15 键消抖 + 6 路 ADC + 滤波 + 死区 + 动态校准 + 边沿记录
 ******************************************************************************/
void Joystick_Scan(void)
{
    uint8_t i, ch, level;
    uint8_t any_pressed = 0;
    uint8_t all_stable  = 1;

    /* 0. 进入本帧时先保存上一帧的确认状态, 供 Joystick_KeyPressed 做边沿检测。
     *    (不能放在 state 更新之后, 否则 prev_state 与 state 永远相等, 边沿恒为假) */
    for (i = 0; i < KEY_NUM; i++)
    {
        prev_state[i] = key_tbl[i].state;
    }

    /* 1. 按键消抖: 电平连续 5 帧相同才翻转 state(加强以滤除十字键接触抖动的误触发) */
    for (i = 0; i < KEY_NUM; i++)
    {
        level = (GPIO_ReadInputDataBit(key_tbl[i].port, key_tbl[i].pin) == Bit_RESET) ? 1 : 0;
        if (level != key_tbl[i].state)
        {
            if (++key_tbl[i].cnt >= 5)
            {
                key_tbl[i].state = level;
                key_tbl[i].cnt   = 0;
            }
        }
        else
        {
            key_tbl[i].cnt = 0;
        }
        if (key_tbl[i].state)
        {
            any_pressed = 1;
        }
    }

    /* 2. 读 6 路 ADC 原始值 */
    ADC_ReadAll(joy_raw);

    /* 2.5 自适应量程: 记录每轴实际观测 min/max(满推必到屏幕/ HID 边界) */
    for (ch = 0; ch < ADC_CH_NUM; ch++)
    {
        if (joy_raw[ch] < ax_lo[ch]) ax_lo[ch] = joy_raw[ch];
        if (joy_raw[ch] > ax_hi[ch]) ax_hi[ch] = joy_raw[ch];
    }

    /* 3. 滤波 + 死区处理 */
    for (ch = 0; ch < ADC_CH_NUM; ch++)
    {
        /* 滤波强度自适应: 用 raw 与 joy_val 的差距判断是否在移动。
         * 阈值选取关键: ADC 残余串扰约 20-40 LSB, 必须落入重滤波区被压住;
         *   真正推杆 raw 偏离 >100, 走轻/无滤波保证跟手。
         * vdelta>120: 大幅移动, 轻滤波(fn=1, 每帧取1/2, 压串扰又不钝)
         * vdelta>60:  小幅移动, 轻滤波(跟手)
         * 其余:        静止 + 串扰残余, 重滤波去抖 */
        int32_t  vdelta = (int32_t)joy_raw[ch] - (int32_t)joy_val[ch];
        uint8_t  fn = (vdelta > 120 || vdelta < -120) ? 1u :  /* 大幅移动: 轻滤波(压串扰, 直通会放过尖峰) */
                     (vdelta > 60  || vdelta < -60)  ? 1u :   /* 小幅移动: 轻滤波 */
                     joy_filter;                             /* 静止+串扰: 重滤波 */
        uint16_t f = lowpass(joy_val[ch], joy_raw[ch], fn);
        int16_t diff = (int16_t)f - (int16_t)joy_center[ch];

        /* 死区归零: |Δ| < 死区 → 强制回中心(摇杆轴) */
        if (diff > -((int16_t)joy_deadzone) && diff < (int16_t)joy_deadzone)
        {
            /* 摇杆 4 轴做死区; 扳机 2 轴(通道4/5)也做死区归零, 保证静止时两扳机轴都为 0 */
            f = joy_center[ch];
            diff = 0;
        }
        joy_val[ch] = f;

        /* 动态校准判定: |Δ| ≤ 4 视为静止 */
        if (diff > 4 || diff < -4)
        {
            all_stable = 0;
        }
    }

    /* 4. 动态零漂消除: 全部通道静止(偏差≤4)且无按键按下时, 平滑收敛中心 */
    if (all_stable && !any_pressed)
    {
        if (++stable_cnt >= 50)   /* 连续 50 帧(约 0.5s)静止 → 更新中心 */
        {
            for (ch = 0; ch < ADC_CH_NUM; ch++)
            {
                joy_center[ch] = joy_val[ch];
            }
            stable_cnt = 0;
        }
    }
    else
    {
        stable_cnt = 0;
    }

    /* 5. 组按键位图: 按 XInput 标准序重映射(report_btn_bit)
     *    A/B/X/Y 占按钮 1~4(前 4 位), LB/RB/Back/Start/Guide/L3/R3 顺延,
     *    十字键 UP/DOWN/LEFT/RIGHT 让出前 4 位, 改放到按钮 12~15(bit11~14),
     *    同时十字键仍走 Hat Switch(见 GetHIDReport) */
    btn_bitmap = 0;
    for (i = 0; i < KEY_NUM; i++)
    {
        uint8_t rb = report_btn_bit[key_tbl[i].key_id];
        if (key_tbl[i].state && rb != 0xFF)
        {
            btn_bitmap |= (uint16_t)(1u << rb);
        }
    }
}

/*******************************************************************************
 * 按键当前电平读取(1=按下)
 ******************************************************************************/
uint8_t Joystick_GetKey(uint8_t key_id)
{
    uint8_t i;
    if (key_id < 1 || key_id > KEY_NUM)
    {
        return 0;
    }
    for (i = 0; i < KEY_NUM; i++)
    {
        if (key_tbl[i].key_id == key_id)
        {
            return key_tbl[i].state;
        }
    }
    return 0;
}

/*******************************************************************************
 * 取当前 15 位按键位图(供测试界面实时显示, 排查"常按"键)
 ******************************************************************************/
uint16_t Joystick_GetBtnBitmap(void)
{
    return btn_bitmap;
}

/*******************************************************************************
 * 按键按下边沿检测(本次扫描新按下=1)
 ******************************************************************************/
uint8_t Joystick_KeyPressed(uint8_t key_id)
{
    uint8_t i;
    if (key_id < 1 || key_id > KEY_NUM)
    {
        return 0;
    }
    for (i = 0; i < KEY_NUM; i++)
    {
        if (key_tbl[i].key_id == key_id)
        {
            /* 当前按下 且 上一帧未按下 */
            return (key_tbl[i].state && !prev_state[i]) ? 1 : 0;
        }
    }
    return 0;
}

/*******************************************************************************
 * 实际半量程(中心到观测极值的距离), 下限保护避免量程未探明时过度放大
 * 自适应量程: 满推(无论实际是否达 ±2048)必归一化到 ±127 / 屏幕边界
 ******************************************************************************/
static int32_t joy_half_range(uint8_t ch)
{
    int32_t lo = (int32_t)joy_center[ch] - (int32_t)ax_lo[ch];   /* 中心 - 最小 */
    int32_t hi = (int32_t)ax_hi[ch]    - (int32_t)joy_center[ch]; /* 最大 - 中心 */
    int32_t half = (lo > hi) ? lo : hi;
    if (half < 0) half = -half;     /* 动态校准后中心可能越过极值, 取绝对值 */
    if (half < 256) half = 256;     /* 下限保护: 量程未探明前不至于过放大 */
    return half;
}

/*******************************************************************************
 * 摇杆轴 → HID 字节(128 中心): 用自适应实际量程归一化, 满推必到 ±127, 再 +0x80
 ******************************************************************************/
static uint8_t axis_to_hid(uint8_t ch)
{
    int32_t d = (int32_t)joy_val[ch] - (int32_t)joy_center[ch];
    int32_t v = d * 127 / joy_half_range(ch);
    if (v >  127) v =  127;
    if (v < -127) v = -127;
    return (uint8_t)(v + 0x80);
}

/*******************************************************************************
 * 扳机轴 → HID 字节: 0=松开, 255=按到底(自适应量程, 满按必到 255)
 * 用 GetNormAbs(绝对偏差)与扳机极性无关; 静止噪声已由 Joystick_Scan 内死区归零
 ******************************************************************************/
static uint8_t trigger_to_hid(uint8_t ch)
{
    int32_t v = (int32_t)Joystick_GetNormAbs(ch);   /* 0~1024 */
    v = v * 255 / 1024;
    if (v > 255) v = 255;
    if (v <   0) v =   0;
    return (uint8_t)v;
}

/*******************************************************************************
 * 组 USB HID 9 字节报告(DirectInput 标准手柄格式 + 独立双扳机轴)
 *   [0] 按键 bit0~7  (A/B/X/Y/LB/RB/View/Menu)
 *   [1] 按键 bit8~10 (L3/R3/Home)
 *   [2] 方向键 dpad (0~8)
 *   [3] 摇杆1X [4] 摇杆1Y [5] 左扳机Z [6] 摇杆2X [7] 摇杆2Y(反向) [8] 右扳机Rz
 ******************************************************************************/
void Joystick_GetHIDReport(uint8_t *buf)
{
    uint8_t  dpad = 0;

    /* 1. 按键字节: 透传已按 XInput 标准序重映射的 15 位键位图(report_btn_bit)
     *    位图 bit 序 = Steam/PC 看到的按钮序(按钮1~15):
     *      bit0=A  bit1=B bit2=X bit3=Y bit4=LB bit5=RB bit6=Home(Guide)
     *      bit7=Menu(Start) bit8=View(Back) bit9=L3 bit10=R3
     *      bit11=UP(已屏蔽) bit12=DOWN bit13=LEFT(已屏蔽) bit14=RIGHT(十字键同时走 Hat Switch)
     *    byte0 = 位图 bit0~7,  byte1 = 位图 bit8~14 */
    {
        buf[0] = (uint8_t)(btn_bitmap & 0xFF);
        buf[1] = (uint8_t)((btn_bitmap >> 8) & 0xFF);
    }

    /* 2. 方向键 Hat Switch: 直接读键真实状态(key_tbl), 不受按钮重映射影响
     *    键号 1~4(UP/DOWN/RIGHT/LEFT) → 标准 8 方向 Hat Switch 编码
     *    十字键在 report_btn_bit 中标为 0xFF(退出按钮位图), 仅走 Hat Switch;
     *    蓝牙帧经 Joystick_GetProcessed() 把十字键物理状态回填到 bit11~14 */
    {
        uint8_t up    = Joystick_GetKey(KEY_UP);
        uint8_t down  = Joystick_GetKey(KEY_DOWN);
        uint8_t right = Joystick_GetKey(KEY_RIGHT);
        uint8_t left  = Joystick_GetKey(KEY_LEFT);

        /* 标准 HID Hat Switch 编码(顺时针, 0=北/上, 8=Null/无方向) */
        if (up && right)        dpad = 1;   /* 右上 NE */
        else if (right && down) dpad = 3;   /* 右下 SE */
        else if (down && left)  dpad = 5;   /* 左下 SW */
        else if (left && up)    dpad = 7;   /* 左上 NW */
        else if (up)            dpad = 0;   /* 上 N */
        else if (right)         dpad = 2;   /* 右 E */
        else if (down)          dpad = 4;   /* 下 S */
        else if (left)          dpad = 6;   /* 左 W */
        else                    dpad = 8;   /* 无 Null */
    }
    buf[2] = dpad;

    /* 3. 摇杆轴(128 中心): 用 AXIS_CH 映射(与测试界面一致) + 自适应量程, 满推必到 ±127 */
    buf[3] = axis_to_hid(AXIS_CH_LX);  /* 摇杆1X */
    buf[4] = axis_to_hid(AXIS_CH_LY);  /* 摇杆1Y */
    buf[6] = axis_to_hid(AXIS_CH_RX);  /* 摇杆2X */

    /* 摇杆2Y 反向: 0x80 - v (右摇杆 Y 轴物理方向相反) */
    {
        int32_t d = (int32_t)joy_val[AXIS_CH_RY] - (int32_t)joy_center[AXIS_CH_RY];
        int32_t v = d * 127 / joy_half_range(AXIS_CH_RY);
        if (v >  127) v =  127;
        if (v < -127) v = -127;
        buf[7] = (uint8_t)(0x80 - v);
    }

    /* 4. 扳机轴: LT/RT 各占一根独立轴(Z / Rz), 0=松开, 255=按到底
     *    原实现为"两扳机差动合成单根 Z 轴"(diff = RT - LT): 两扳机同时按下且
     *    深度接近时 diff≈0 落到中位 → PC 判为"没按扳机", 无法同时按。
     *    改为独立双轴后两扳机可同时上报(报告因此由 8 字节扩为 9 字节)。 */
    buf[5] = trigger_to_hid(ADC_CH_LT);   /* Z  = 左扳机 LT */
    buf[8] = trigger_to_hid(ADC_CH_RT);   /* Rz = 右扳机 RT */
}

/*******************************************************************************
 * 取处理后数据(供蓝牙帧组包)
 *   axis[0..5] = L-VRX/L-VRY/R-VRX/R-VRY/LT/RT(joy_val, 0~4095)
 *   *bm       = 15 位键位图: bit0~10 为 A/B/X/Y/LB/RB/Home/Menu/View/L3/R3,
 *              bit11~14 为十字键 UP/DOWN/LEFT/RIGHT(物理状态回填, 见下)
 *
 * 十字键回填说明: 十字键已改为仅走 USB HID 的 Hat Switch(POV)、移出按钮位图
 *   (避免同一物理键被当成"POV方向"和"按钮"双重上报, 导致 Steam 绑定抢占)。
 *   但蓝牙帧里没有 Hat 字段, 若直接透传 btn_bitmap 则蓝牙接收端收不到十字键。
 *   故此处把十字键消抖后的物理状态回填到 bit11~14, 帧长与协议均不变。
 ******************************************************************************/
void Joystick_GetProcessed(uint16_t axis[6], uint16_t *bm)
{
    uint8_t ch;
    for (ch = 0; ch < ADC_CH_NUM; ch++)
    {
        axis[ch] = joy_val[ch];
    }
    if (bm != 0)
    {
        uint16_t v = btn_bitmap;
        if (Joystick_GetKey(KEY_UP))    v |= (uint16_t)(1u << 11);
        if (Joystick_GetKey(KEY_DOWN))  v |= (uint16_t)(1u << 12);
        if (Joystick_GetKey(KEY_LEFT))  v |= (uint16_t)(1u << 13);
        if (Joystick_GetKey(KEY_RIGHT)) v |= (uint16_t)(1u << 14);
        *bm = v;
    }
}

/*******************************************************************************
 * 参数读写(供菜单调节)
 ******************************************************************************/
void Joystick_SetDeadzone(uint16_t dz)
{
    if (dz > 512)
    {
        dz = 512;   /* 死区上限, 避免过大导致摇杆无响应 */
    }
    joy_deadzone = dz;
}

uint16_t Joystick_GetDeadzone(void)
{
    return joy_deadzone;
}

void Joystick_SetFilter(uint8_t f)
{
    if (f > 8)
    {
        f = 8;      /* 一阶低通移位量上限 */
    }
    joy_filter = f;
}

uint8_t Joystick_GetFilter(void)
{
    return joy_filter;
}

/*******************************************************************************
 * 取某通道处理后值 / 校准中心值(供"测试手柄"界面实时显示)
 *   ch 用 ADC_CH_xxx 宏(0=L-VRX 1=L-VRY 2=R-VRX 3=R-VRY 4=LT 5=RT)
 ******************************************************************************/
uint16_t Joystick_GetAxis(uint8_t ch)
{
    if (ch < ADC_CH_NUM)
    {
        return joy_val[ch];
    }
    return 0;
}

uint16_t Joystick_GetCenter(uint8_t ch)
{
    if (ch < ADC_CH_NUM)
    {
        return joy_center[ch];
    }
    return 0;
}

/*******************************************************************************
 * 取某通道原始 ADC 值(未经滤波/死区, 供"测试手柄"界面实时无延迟显示)
 ******************************************************************************/
uint16_t Joystick_GetRaw(uint8_t ch)
{
    if (ch < ADC_CH_NUM)
    {
        return joy_raw[ch];
    }
    return 0;
}

/* 自适应归一化(供测试界面/ HID 共用): (raw-center)/实际半量程*1024, 满推=±1024 */
int16_t Joystick_GetNormRaw(uint8_t ch)
{
    int32_t d = (int32_t)joy_raw[ch] - (int32_t)joy_center[ch];
    int32_t span = joy_half_range(ch);
    int32_t v = d * 1024 / span;
    if (v >  1024) v =  1024;
    if (v < -1024) v = -1024;
    return (int16_t)v;
}

/*******************************************************************************
 * 滤波后归一化(供"测试手柄"界面消除抖动): 用 joy_val(已一阶低通+死区)替代原始 ADC
 *   满推=±1024; 静止(死区内)joy_val 被钳到中心 → 归一化为 0, 从根本上消除圆点像素级抖动
 ******************************************************************************/
int16_t Joystick_GetNorm(uint8_t ch)
{
    int32_t d = (int32_t)joy_val[ch] - (int32_t)joy_center[ch];
    int32_t span = joy_half_range(ch);
    int32_t v = d * 1024 / span;
    if (v >  1024) v =  1024;
    if (v < -1024) v = -1024;
    return (int16_t)v;
}

/* 触发用: |raw-center| 归一化到 0~1024(用滤波后值, 与 GetNorm 一致, 避免扳机抖动) */
int16_t Joystick_GetNormAbs(uint8_t ch)
{
    int16_t v = Joystick_GetNorm(ch);
    return v < 0 ? (int16_t)-v : v;
}

/*******************************************************************************
 * 死区归一化(供测试界面画死区圈): 死区(raw 单位)相对该轴自适应半量程, 0~1024
 ******************************************************************************/
int16_t Joystick_GetDeadzoneNorm(uint8_t ch)
{
    int32_t v = (int32_t)joy_deadzone * 1024 / joy_half_range(ch);
    if (v >  1024) v =  1024;
    if (v <     0) v =     0;
    return (int16_t)v;
}

/*******************************************************************************
 * 是否有活动(供空闲熄屏判定): 任意键按下, 或任意轴超出轻微阈值(归一化|v|>128)
 *   阈值取 ~12.5% 行程, 摇杆静止(死区→归零)不会误触发活动。
 ******************************************************************************/
uint8_t Joystick_IsActive(void)
{
    uint8_t i, ch;
    for (i = 0; i < KEY_NUM; i++)
    {
        if (key_tbl[i].state)
        {
            return 1;
        }
    }
    for (ch = 0; ch < ADC_CH_NUM; ch++)
    {
        int16_t n = Joystick_GetNorm(ch);
        if (n > 128 || n < -128)
        {
            return 1;
        }
    }
    return 0;
}
