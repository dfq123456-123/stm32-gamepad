/*******************************************************************************
 * joystick.h - STM32F103C8T6 游戏手柄采集核心
 *
 * 本文件为"根据博主固件反汇编逆向结果重写"的权威实现。
 * 依据逆向笔记(权威规格):
 *   1. 按键表: 15 键, 键号 1~15(1基, = 内部位图中的位序), 顺序与 PCB 网络表一致
 *  2. HID 报告: 8 字节, byte0/1 = 15 位原始键位图(忠实原固件, 不重映射), byte2 = 方向键(dpad 0~8),
 *      byte3~7 = 摇杆1X/Y + 差动比例 + 摇杆2X/Y(128 为中心)
 *   3. 摇杆映射: Δ=raw-center, 死区归零(150), HID=Δ/实际半量程*127 (自适应量程, 满推必到 ±127), 字节=HID+0x80
 *   4. 差动比例: sl=(ΔRT-ΔLT+0xFFF)*255/0x1FFE 钳位 5~250 (左右扳机方向相反, 差值归正)
 *   5. 校准: 开机 100 次×6 通道平均 + 运行时动态跟随(|Δ|≤4 静止收敛)
 *
 * 按键 GPIO 映射(PCB 网络表权威, 上拉输入, 按下=低):
 *   UP=PB11  DOWN=PB1  RIGHT=PB0  LEFT=PB10  X=PB9  Y=PB6  B=PB7  A=PB8
 *   Home=PB3  Menu=PB4  View=PB5  R3=PA6  L3=PA7  LB=PB15  RB=PB14
 *
 * 中间三键语义(PCB 实测): 左键 PB3(KEY_HOME)=仅菜单返回, 右键 PB4(KEY_MENU)=开关菜单,
 *   上键 PB5(KEY_VIEW)=切换游戏手柄/蓝牙模式。左键仅返回, 不切模式。
 ******************************************************************************/
#ifndef __JOYSTICK_H
#define __JOYSTICK_H

#include "stm32f10x.h"
#include "adc.h"   /* ADC_CH_* 通道定义: AXIS_CH 交叉映射依赖之 */

/* 校准进度回调: percent=0~100, 由 Joystick_Calibrate_Ex 每完成若干样本调用一次 */
typedef void (*Joystick_CalProgressCB)(uint8_t percent);

/*---------------------- 用户可调参数(运行时可被菜单修改) ----------------------*/
#define JOY_DEADZONE_DEFAULT     150     /* 摇杆死区默认值: |raw-center|<150 视为回中 */
#define JOY_FILTER_DEFAULT       3       /* 滤波强度默认值: 一阶低通移位量(0=无滤波) */
#define JOY_CAL_NUM              100     /* 开机中心校准采样次数(博主固件=100) */
#define JOY_TRIG_DIFF_DEAD       120     /* 扳机差动比例死区: |NormRT-NormLT|<120 视为零差, 抑制中位噪声 */

/* HID 报告长度: 2 按键字节 + 1 方向键字节 + 5 轴字节 = 8 */
#define HID_REPORT_LEN           8

/*---------------------- 按键键号(1 基, 与博主固件 bitpos 一致) ----------------------*/
#define KEY_UP      1   /* PB11 十字键上 */
#define KEY_DOWN    2   /* PB1  十字键下 */
#define KEY_RIGHT   3   /* PB0  十字键右 */
#define KEY_LEFT    4   /* PB10 十字键左 */
#define KEY_X       5   /* PB9  X 键 */
#define KEY_Y       6   /* PB6  Y 键 */
#define KEY_B       7   /* PB7  B 键 */
#define KEY_A       8   /* PB8  A 键 */
#define KEY_HOME    9   /* PB3  中间键·Home(菜单返回) */
#define KEY_MENU    10  /* PB4  中间键·Menu(打开菜单) */
#define KEY_VIEW    11  /* PB5  中间键·View(切换模式) */
#define KEY_R3      12  /* PA6  右摇杆按下 */
#define KEY_L3      13  /* PA7  左摇杆按下 */
#define KEY_LB      14  /* PB15 左肩键 */
#define KEY_RB      15  /* PB14 右肩键 */

#define KEY_NUM     15

/*---------------------- 摇杆逻辑轴交叉映射(90°旋转修正, 单一事实来源) ----------------------*/
/* 实测板子上摇杆模块与电位器轴存在 90° 错位: 物理 VRX 对应手持"上下", VRY 对应"左右"。
 * 故逻辑轴需与物理通道交叉, 否则 PC/屏幕上的 X/Y 会反。测试界面与 HID 报告必须共用本映射。
 *   逻辑 X(横) <- 物理 VRY,   逻辑 Y(纵) <- 物理 VRX
 * 该方向已由用户实测确认正确, 不提供交换开关。 */
#define AXIS_CH_LX      ADC_CH_L_VRY
#define AXIS_CH_LY      ADC_CH_L_VRX
#define AXIS_CH_RX      ADC_CH_R_VRY
#define AXIS_CH_RY      ADC_CH_R_VRX

/*---------------------- API ----------------------*/
/* 初始化: 配置按键 GPIO(上拉输入) + 初始化 ADC1 + 上电自动校准 */
void MY_Joystick_Init(void);

/* 上电中心校准: 每路采样 JOY_CAL_NUM 次取平均(校准期间勿动摇杆/扳机) */
void Joystick_Calibrate(void);

/* 带进度回调的校准: 每完成若干样本回调一次 cb(percent 0~100), 供开机界面显示进度 */
void Joystick_Calibrate_Ex(Joystick_CalProgressCB cb);

/* 扫描一帧: 15 键消抖 + 6 路 ADC + 滤波 + 死区 + 动态校准 */
void Joystick_Scan(void);

/* 组 USB HID 8 字节报告(忠实原固件, 反汇编笔记 12.2/4.1):
 *   [0] 15位键位图 bit0~7 (UP/DOWN/RIGHT/LEFT/X/Y/B/A)
 *   [1] 15位键位图 bit8~14 (Home/Menu/View/R3/L3/LB/RB)
 *   [2] 方向键 dpad(0~8)
 *   [3] 摇杆1X(128中心) [4] 摇杆1Y(128中心) [5] 差动比例(5~250)
 *   [6] 摇杆2X(128中心) [7] 摇杆2Y(128中心,反向)
 *   注: 摇杆 X/Y 经 AXIS_CH 交叉映射(模块90°旋转修正), 与"测试手柄"界面一致 */
void Joystick_GetHIDReport(uint8_t *buf);

/* 读取按键当前电平(1=按下) */
uint8_t Joystick_GetKey(uint8_t key_id);

/* 读取按键按下边沿(本次扫描新按下=1, 供菜单导航用) */
uint8_t Joystick_KeyPressed(uint8_t key_id);

/* 参数读写(供菜单调节) */
void     Joystick_SetDeadzone(uint16_t dz);
uint16_t Joystick_GetDeadzone(void);
void     Joystick_SetFilter(uint8_t f);
uint8_t  Joystick_GetFilter(void);

/* 取某通道处理后值 / 校准中心值(供"测试手柄"界面实时显示, ch 用 ADC_CH_xxx) */
uint16_t Joystick_GetAxis(uint8_t ch);
uint16_t Joystick_GetCenter(uint8_t ch);
uint16_t Joystick_GetRaw(uint8_t ch);   /* 原始值(未滤波/死区, 无延迟) */
uint16_t Joystick_GetBtnBitmap(void);    /* 当前 15 位按键位图(供测试界面显示) */

/* 自适应归一化(供测试界面/ HID 共用): 满推=±1024(带符号)/ 0~1024(绝对值) */
int16_t Joystick_GetNormRaw(uint8_t ch);
int16_t Joystick_GetNormAbs(uint8_t ch);
int16_t Joystick_GetNorm(uint8_t ch);   /* 滤波后归一化(±1024), 供"测试手柄"界面消除抖动 */

/* 死区归一化(供测试界面画死区圈): 死区(raw 单位)相对该轴自适应半量程, 0~1024 */
int16_t Joystick_GetDeadzoneNorm(uint8_t ch);

/* 是否有活动(供空闲熄屏判定): 任意键按下 或 任意轴超出死区外(归一化|v|>128) */
uint8_t Joystick_IsActive(void);

/* 按键连发(Turbo): 选定键按住时按设定频率自动通断, 模拟快速点按 */
void    Joystick_SetTurboKey(uint8_t key_id);   /* key_id=0 关闭; 否则 1~15 */
uint8_t Joystick_GetTurboKey(void);
void    Joystick_SetTurboHz(uint8_t hz);        /* 连发频率 Hz(2/4/8/16...) */
uint8_t Joystick_GetTurboHz(void);
void    Joystick_ApplyTurbo(uint16_t *bm);      /* 对传入键位图应用 Turbo 相位(就地修改) */

/* 取处理后数据(供蓝牙帧组包用, buf 至少 6 个 uint16_t + 1 个 uint16_t 键位图) */
void Joystick_GetProcessed(uint16_t axis[6], uint16_t *btn_bitmap);

#endif /* __JOYSTICK_H */
