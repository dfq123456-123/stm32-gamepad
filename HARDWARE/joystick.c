#include "joystick.h"
#include "adc.h"

typedef struct
{
	GPIO_TypeDef *port;
	uint16_t      pin;
	uint8_t       key_id;	 //键号 1~15
	uint8_t       state;	 //确认状态: 1=按下
	uint8_t       cnt;	 //消抖计数
} KEY_T;

static KEY_T key_tbl[KEY_NUM] =
{
	{GPIOB, GPIO_Pin_11, KEY_UP,    0, 0},	 //1  UP    PB11
	{GPIOB, GPIO_Pin_1,  KEY_DOWN,  0, 0},	 //2  DOWN  PB1
	{GPIOB, GPIO_Pin_0,  KEY_RIGHT, 0, 0},	 //3  RIGHT PB0
	{GPIOB, GPIO_Pin_10, KEY_LEFT,  0, 0},	 //4  LEFT  PB10
	{GPIOB, GPIO_Pin_9,  KEY_X,     0, 0},	 //5  X     PB9
	{GPIOB, GPIO_Pin_6,  KEY_Y,     0, 0},	 //6  Y     PB6
	{GPIOB, GPIO_Pin_7,  KEY_B,     0, 0},	 //7  B     PB7
	{GPIOB, GPIO_Pin_8,  KEY_A,     0, 0},	 //8  A     PB8
	{GPIOB, GPIO_Pin_3,  KEY_HOME,  0, 0},	 //9  Home  PB3
	{GPIOB, GPIO_Pin_4,  KEY_MENU,  0, 0},	 //10 Menu  PB4
	{GPIOB, GPIO_Pin_5,  KEY_VIEW,  0, 0},	 //11 View  PB5
	{GPIOA, GPIO_Pin_6,  KEY_R3,    0, 0},	 //12 R3    PA6
	{GPIOA, GPIO_Pin_7,  KEY_L3,    0, 0},	 //13 L3    PA7
	{GPIOB, GPIO_Pin_15, KEY_LB,    0, 0},	 //14 LB    PB15
	{GPIOB, GPIO_Pin_14, KEY_RB,    0, 0},	 //15 RB    PB14
};

//十字键只走 Hat Switch, 不占按钮位图
static const uint8_t report_btn_bit[KEY_NUM + 1] =
{
	0,	 //索引0未用
	0xFF,	 //KEY_UP    -> 仅走 Hat
	0xFF,	 //KEY_DOWN  -> 仅走 Hat
	0xFF,	 //KEY_RIGHT -> 仅走 Hat
	0xFF,	 //KEY_LEFT  -> 仅走 Hat
	2,	 //KEY_X  -> 位2  (XInput X = 按钮3)
	3,	 //KEY_Y  -> 位3  (Y = 按钮4)
	1,	 //KEY_B  -> 位1  (B = 按钮2)
	0,	 //KEY_A  -> 位0  (A = 按钮1)
	6,	 //KEY_HOME -> 位6 (Guide)
	7,	 //KEY_MENU  -> 位7 (Start)
	8,	 //KEY_VIEW  -> 位8 (Back)
	10,	 //KEY_R3 -> 位10
	9,	 //KEY_L3 -> 位9
	4,	 //KEY_LB -> 位4 (LB = 按钮5)
	5,	 //KEY_RB -> 位5 (RB = 按钮6)
};

static uint16_t joy_center[ADC_CH_NUM];	 //校准中心值
static uint16_t joy_raw[ADC_CH_NUM];	 //原始 ADC 值
static uint16_t joy_val[ADC_CH_NUM];	 //滤波+死区后值
static uint16_t ax_lo[ADC_CH_NUM];	 //自适应量程: 观测最小值
static uint16_t ax_hi[ADC_CH_NUM];	 //自适应量程: 观测最大值
static uint16_t btn_bitmap;
static uint8_t  prev_state[KEY_NUM];	 //上一帧状态(边沿检测)

static uint16_t joy_deadzone = JOY_DEADZONE_DEFAULT;
static uint8_t  joy_filter  = JOY_FILTER_DEFAULT;
static uint8_t  stable_cnt = 0;	 //动态校准静止计数

//按键 GPIO + ADC 初始化
void MY_Joystick_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	uint8_t i;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
	//关 JTAG 释放 PB3/PB4, 保留 SWD
	GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);

	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	for (i = 0; i < KEY_NUM; i++)
	{
		GPIO_InitStructure.GPIO_Pin = key_tbl[i].pin;
		GPIO_Init(key_tbl[i].port, &GPIO_InitStructure);
	}

	MY_ADC_Init();
	//校准在 main 里 OLED 就绪后做
}

void Joystick_Calibrate(void)
{
	Joystick_Calibrate_Ex(0);
}

//连采 JOY_CAL_NUM 次取均值作中心
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
		if (cb != 0 && ((i + 1) % 10) == 0)
		{
			cb((uint8_t)(((i + 1) * 100) / JOY_CAL_NUM));
		}
	}
	for (ch = 0; ch < ADC_CH_NUM; ch++)
	{
		joy_center[ch] = (uint16_t)(sum[ch] / JOY_CAL_NUM);
		joy_val[ch]    = joy_center[ch];
		ax_lo[ch]      = joy_center[ch];
		ax_hi[ch]      = joy_center[ch];
	}
	if (cb != 0)
	{
		cb(100);
	}
}

//一阶低通: val=(val*(2^n-1)+raw)>>n
static uint16_t lowpass(uint16_t val, uint16_t raw, uint8_t n)
{
	if (n == 0)
	{
		return raw;
	}
	return (uint16_t)(((uint32_t)val * ((1u << n) - 1) + raw) >> n);
}

//单帧扫描: 按键消抖 -> ADC -> 滤波死区 -> 零漂 -> 位图
void Joystick_Scan(void)
{
	uint8_t i, ch, level;
	uint8_t any_pressed = 0;
	uint8_t all_stable  = 1;

	//存上一帧状态供边沿检测
	for (i = 0; i < KEY_NUM; i++)
	{
		prev_state[i] = key_tbl[i].state;
	}

	//消抖: 连续 5 帧相同才翻转
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

	ADC_ReadAll(joy_raw);

	//自适应量程: 记录每轴实际观测 min/max
	for (ch = 0; ch < ADC_CH_NUM; ch++)
	{
		if (joy_raw[ch] < ax_lo[ch]) ax_lo[ch] = joy_raw[ch];
		if (joy_raw[ch] > ax_hi[ch]) ax_hi[ch] = joy_raw[ch];
	}

	for (ch = 0; ch < ADC_CH_NUM; ch++)
	{
		//滤波强度自适应
		//阈值: 串扰 20-40, 推杆 >100
		int32_t  vdelta = (int32_t)joy_raw[ch] - (int32_t)joy_val[ch];
		uint8_t  fn = (vdelta > 120 || vdelta < -120) ? 1u :
					 (vdelta > 60  || vdelta < -60)  ? 1u :
					 joy_filter;
		uint16_t f = lowpass(joy_val[ch], joy_raw[ch], fn);
		int16_t diff = (int16_t)f - (int16_t)joy_center[ch];

		//死区归零, 6 轴都做
		if (diff > -((int16_t)joy_deadzone) && diff < (int16_t)joy_deadzone)
		{
			f = joy_center[ch];
			diff = 0;
		}
		joy_val[ch] = f;

		if (diff > 4 || diff < -4)
		{
			all_stable = 0;
		}
	}

	//零漂消除: 全静止时收敛中心
	if (all_stable && !any_pressed)
	{
		if (++stable_cnt >= 50)	 //连续 50 帧(~0.5s)静止才更新
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

	//按 XInput 序重排位图
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

//取键状态, 1=按下
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

//取按键位图
uint16_t Joystick_GetBtnBitmap(void)
{
	return btn_bitmap;
}

//取按下沿
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
			return (key_tbl[i].state && !prev_state[i]) ? 1 : 0;
		}
	}
	return 0;
}

//半量程(中心到极值), 下限 256
static int32_t joy_half_range(uint8_t ch)
{
	int32_t lo = (int32_t)joy_center[ch] - (int32_t)ax_lo[ch];
	int32_t hi = (int32_t)ax_hi[ch]    - (int32_t)joy_center[ch];
	int32_t half = (lo > hi) ? lo : hi;
	if (half < 0) half = -half;	 //动态校准后中心可能越过极值
	if (half < 256) half = 256;
	return half;
}

//摇杆轴 -> HID 字节, 128 中心
static uint8_t axis_to_hid(uint8_t ch)
{
	int32_t d = (int32_t)joy_val[ch] - (int32_t)joy_center[ch];
	int32_t v = d * 127 / joy_half_range(ch);
	if (v >  127) v =  127;
	if (v < -127) v = -127;
	return (uint8_t)(v + 0x80);
}

//扳机轴 -> HID 字节, 0=松开 255=到底
static uint8_t trigger_to_hid(uint8_t ch)
{
	int32_t v = (int32_t)Joystick_GetNormAbs(ch);	 //0~1024
	v = v * 255 / 1024;
	if (v > 255) v = 255;
	if (v <   0) v =   0;
	return (uint8_t)v;
}

//USB HID 9 字节报告
//[0]按键bit0~7 [1]bit8~10 [2]Hat [3][4]左杆XY [5]LT [6]右杆X [7]右杆Y [8]RT
void Joystick_GetHIDReport(uint8_t *buf)
{
	uint8_t  dpad = 0;

	buf[0] = (uint8_t)(btn_bitmap & 0xFF);
	buf[1] = (uint8_t)((btn_bitmap >> 8) & 0xFF);

	//Hat 读键真实状态
	{
		uint8_t up    = Joystick_GetKey(KEY_UP);
		uint8_t down  = Joystick_GetKey(KEY_DOWN);
		uint8_t right = Joystick_GetKey(KEY_RIGHT);
		uint8_t left  = Joystick_GetKey(KEY_LEFT);

		//Hat: 0=上 顺时针, 8=Null
		if (up && right)        dpad = 1;
		else if (right && down) dpad = 3;
		else if (down && left)  dpad = 5;
		else if (left && up)    dpad = 7;
		else if (up)            dpad = 0;
		else if (right)         dpad = 2;
		else if (down)          dpad = 4;
		else if (left)          dpad = 6;
		else                    dpad = 8;
	}
	buf[2] = dpad;

	buf[3] = axis_to_hid(AXIS_CH_LX);
	buf[4] = axis_to_hid(AXIS_CH_LY);
	buf[6] = axis_to_hid(AXIS_CH_RX);

	//右Y反向
	{
		int32_t d = (int32_t)joy_val[AXIS_CH_RY] - (int32_t)joy_center[AXIS_CH_RY];
		int32_t v = d * 127 / joy_half_range(AXIS_CH_RY);
		if (v >  127) v =  127;
		if (v < -127) v = -127;
		buf[7] = (uint8_t)(0x80 - v);
	}

	buf[5] = trigger_to_hid(ADC_CH_LT);	 //Z  = 左扳机
	buf[8] = trigger_to_hid(ADC_CH_RT);	 //Rz = 右扳机
}

//打包接口: 6 轴值 + 位图
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

//设死区, 上限 512
void Joystick_SetDeadzone(uint16_t dz)
{
	if (dz > 512)
	{
		dz = 512;
	}
	joy_deadzone = dz;
}

uint16_t Joystick_GetDeadzone(void)
{
	return joy_deadzone;
}

//设滤波强度, 上限 8
void Joystick_SetFilter(uint8_t f)
{
	if (f > 8)
	{
		f = 8;
	}
	joy_filter = f;
}

uint8_t Joystick_GetFilter(void)
{
	return joy_filter;
}

//取滤波+死区处理后的轴值
uint16_t Joystick_GetAxis(uint8_t ch)
{
	if (ch < ADC_CH_NUM)
	{
		return joy_val[ch];
	}
	return 0;
}

//取中心值
uint16_t Joystick_GetCenter(uint8_t ch)
{
	if (ch < ADC_CH_NUM)
	{
		return joy_center[ch];
	}
	return 0;
}

//取原始 ADC 值
uint16_t Joystick_GetRaw(uint8_t ch)
{
	if (ch < ADC_CH_NUM)
	{
		return joy_raw[ch];
	}
	return 0;
}

//归一化: (raw-center)/半量程*1024
int16_t Joystick_GetNormRaw(uint8_t ch)
{
	int32_t d = (int32_t)joy_raw[ch] - (int32_t)joy_center[ch];
	int32_t span = joy_half_range(ch);
	int32_t v = d * 1024 / span;
	if (v >  1024) v =  1024;
	if (v < -1024) v = -1024;
	return (int16_t)v;
}

//滤波后归一化 ±1024
int16_t Joystick_GetNorm(uint8_t ch)
{
	int32_t d = (int32_t)joy_val[ch] - (int32_t)joy_center[ch];
	int32_t span = joy_half_range(ch);
	int32_t v = d * 1024 / span;
	if (v >  1024) v =  1024;
	if (v < -1024) v = -1024;
	return (int16_t)v;
}

//归一化绝对值 0~1024, 扳机用
int16_t Joystick_GetNormAbs(uint8_t ch)
{
	int16_t v = Joystick_GetNorm(ch);
	return v < 0 ? (int16_t)-v : v;
}

//死区归一化, 画死区圈用
int16_t Joystick_GetDeadzoneNorm(uint8_t ch)
{
	int32_t v = (int32_t)joy_deadzone * 1024 / joy_half_range(ch);
	if (v >  1024) v =  1024;
	if (v <     0) v =     0;
	return (int16_t)v;
}

//是否有活动, 供熄屏判断
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
