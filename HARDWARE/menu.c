#include "menu.h"
#include "joystick.h"
#include "adc.h"
#include "oled.h"
#include "flash_save.h"
#include <string.h>

static uint8_t    mode = MODE_GAME;
static BT_CONFIG  bt_cfg = { 0xAA, 0x55, 0, 1 };	 //默认: AA头 55尾 无hex 有换行

static uint8_t oled_brightness = 0xFF;	 //OLED 对比度
static uint8_t idle_idx        = 1;	 //熄屏: 0=关 1=3s 2=5s 3=10s 4=30s
static const uint8_t idle_secs[] = { 0, 3, 5, 10, 30 };

static uint8_t menu_dirty_for_save = 0;	 //有改动待保存
static uint8_t save_delay_cnt = 0;	 //延时计数, 改动后等若干帧再写

//把当前各项设置打包成待存结构
static void pack_params(PARAMS *p)
{
	p->version      = PARAM_VERSION;
	p->deadzone     = Joystick_GetDeadzone();
	p->filter       = (uint8_t)Joystick_GetFilter();
	p->brightness   = oled_brightness;
	p->idle_idx     = idle_idx;
	p->mode         = (uint8_t)mode;
	p->bt_header    = bt_cfg.header;
	p->bt_tail      = bt_cfg.tail;
	p->bt_use_hex   = bt_cfg.use_hex;
	p->bt_use_newline = bt_cfg.use_newline;
}

//把存下来的参数还原回各模块
static void unpack_params(const PARAMS *p)
{
	Joystick_SetDeadzone(p->deadzone);
	Joystick_SetFilter(p->filter);
	oled_brightness = p->brightness;  OLED_SetContrast(oled_brightness);
	idle_idx        = p->idle_idx;
	if (idle_idx >= sizeof(idle_secs))
	{
		idle_idx = 1;	 //防越界
	}
	mode            = (p->mode == MODE_BT) ? MODE_BT : MODE_GAME;
	bt_cfg.header       = p->bt_header;
	bt_cfg.tail         = p->bt_tail;
	bt_cfg.use_hex      = p->bt_use_hex;
	bt_cfg.use_newline  = p->bt_use_newline;
}

//标记有改动待落盘
static void mark_param_changed(void)
{
	menu_dirty_for_save = 1;
	save_delay_cnt = 0;
}

//延迟落盘: 100 帧无改动再写
static void save_if_pending(void)
{
	PARAMS p;
	if (!menu_dirty_for_save) return;

	if (save_delay_cnt < 100)
	{
		save_delay_cnt++;
		return;
	}

	pack_params(&p);
	FlashSave_Save(&p);
	menu_dirty_for_save = 0;
	save_delay_cnt = 0;
}

static uint8_t    stick_active = 0;	 //左摇杆控制菜单的边沿状态
#define STICK_MOVE_TH   900	 //摇杆移动光标触发阈值(ADC 偏差, 满量程±2048)

//菜单上下方向走左摇杆 Y 轴
static uint8_t stick_vert_ch(void)
{
	return AXIS_CH_LY;
}

//摇杆相对中心的偏差, 正=往下推
static int16_t stick_vert_diff(void)
{
	uint8_t ch = stick_vert_ch();
	return (int16_t)Joystick_GetRaw(ch) - (int16_t)Joystick_GetCenter(ch);
}

typedef enum
{
	MENU_CLOSED = 0,
	MENU_OPEN,
	MENU_TEST
} MENU_STATE;

static uint8_t menu_state = MENU_CLOSED;
static uint8_t test_from_boot = 0;	 //1=开机自动进测试, 0=菜单进入
static uint8_t menu_idx   = 0;
static uint16_t menu_dirty = 0;

//转十进制字符串
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

//转两位十六进制
static char *u2hex(uint8_t val, char *buf)
{
	static const char hex[] = "0123456789ABCDEF";
	buf[0] = hex[(val >> 4) & 0x0F];
	buf[1] = hex[val & 0x0F];
	buf[2] = '\0';
	return buf;
}

//当前模式的菜单项数
static uint8_t item_count(void)
{
	return (mode == MODE_GAME) ? 5 : 4;	 //游戏5项 蓝牙4项
}

//取某项当前值
static uint16_t item_get(uint8_t idx)
{
	if (mode == MODE_GAME)
	{
		if (idx == 0) return Joystick_GetDeadzone();
		if (idx == 1) return Joystick_GetFilter();
		if (idx == 2) return oled_brightness;
		if (idx == 3) return idle_idx;
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

//改某项值并标脏
static void item_set(uint8_t idx, uint16_t val)
{
	if (mode == MODE_GAME)
	{
		if (idx == 0) { Joystick_SetDeadzone(val);      mark_param_changed(); }
		if (idx == 1) { Joystick_SetFilter((uint8_t)val); mark_param_changed(); }
		if (idx == 2) { oled_brightness = (uint8_t)val; OLED_SetContrast(oled_brightness); mark_param_changed(); }
		if (idx == 3) { idle_idx = (uint8_t)val;        mark_param_changed(); }
	}
	else
	{
		if (idx == 0) { bt_cfg.header = (uint8_t)val;       mark_param_changed(); }
		if (idx == 1) { bt_cfg.tail   = (uint8_t)val;       mark_param_changed(); }
		if (idx == 2) { bt_cfg.use_hex = (uint8_t)(val & 1); mark_param_changed(); }
		if (idx == 3) { bt_cfg.use_newline = (uint8_t)(val & 1); mark_param_changed(); }
	}
}

//取某项的调节范围
static void item_range(uint8_t idx, uint16_t *min, uint16_t *max, uint16_t *step)
{
	*min = 0; *max = 255; *step = 1;
	if (mode == MODE_GAME)
	{
		if (idx == 0) { *min = 0; *max = 512; *step = 10; }
		if (idx == 1) { *min = 0; *max = 8;   *step = 1;  }
		if (idx == 2) { *min = 0; *max = 255; *step = 16; }
		if (idx == 3) { *min = 0; *max = (uint16_t)(sizeof(idle_secs)/sizeof(idle_secs[0]) - 1); *step = 1; }
	}
	else
	{
		if (idx == 2 || idx == 3) { *min = 0; *max = 1; *step = 1; }
	}
}

//按 dir 调当前项, 到边界就钳住
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

//选中项用 > 光标标识
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
		if (idx == 0)	//死区
		{
			OLED_ShowCN(x, line, CN_SI);   x += 16;
			OLED_ShowCN(x, line, CN_QU);   x += 16;
			OLED_ShowString(x, line, u2dec(v, buf));
		}
		else if (idx == 1)	//滤波强度
		{
			OLED_ShowCN(x, line, CN_LV);    x += 16;
			OLED_ShowCN(x, line, CN_BO);    x += 16;
			OLED_ShowCN(x, line, CN_QIANG); x += 16;
			OLED_ShowCN(x, line, CN_DU);    x += 16;
			OLED_ShowString(x, line, u2dec(v, buf));
		}
		else if (idx == 2)	//亮度
		{
			OLED_ShowCN(x, line, CN_LIANG); x += 16;
			OLED_ShowCN(x, line, CN_DU);    x += 16;
			OLED_ShowString(x, line, u2dec(v, buf));
		}
		else if (idx == 3)	//关屏
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
		else	//手柄校准
		{
			OLED_ShowCN(x, line, CN_SHOU);  x += 16;
			OLED_ShowCN(x, line, CN_BING);  x += 16;
			OLED_ShowCN(x, line, CN_JIAO);  x += 16;
			OLED_ShowCN(x, line, CN_ZHUN);
		}
	}
	else
	{
		switch (idx)
		{
			case 0:	//包头
				OLED_ShowCN(x, line, CN_BAO);  x += 16;
				OLED_ShowCN(x, line, CN_TOU);  x += 16;
				OLED_ShowString(x, line, u2hex((uint8_t)v, buf));
				break;
			case 1:	//包尾
				OLED_ShowCN(x, line, CN_BAO);  x += 16;
				OLED_ShowCN(x, line, CN_WEI);  x += 16;
				OLED_ShowString(x, line, u2hex((uint8_t)v, buf));
				break;
			case 2:	//十六进制
				OLED_ShowCN(x, line, CN_SHI2); x += 16;
				OLED_ShowCN(x, line, CN_LIU);  x += 16;
				OLED_ShowCN(x, line, CN_JIN);  x += 16;
				OLED_ShowCN(x, line, CN_ZHI);  x += 16;
				if (v) OLED_ShowCN(x, line, CN_KAI);
				else   OLED_ShowCN(x, line, CN_GUAN);
				break;
			case 3:	//换行
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

//画菜单: 标题 + 可见 3 行, 选中项超出就滚动
static void menu_draw(void)
{
	uint8_t cnt = item_count();
	uint8_t i, scroll;

	OLED_Clear();

	OLED_ShowCN(0, 0, CN_CAI);	//菜
	OLED_ShowCN(16, 0, CN_DAN);	//单

	scroll = 0;
	if (menu_idx >= 3)
	{
		scroll = menu_idx - 2;	 //保证选中项在可见 3 行内
	}

	for (i = 0; i < 3 && (scroll + i) < cnt; i++)
	{
		draw_item((uint8_t)(1 + i), (uint8_t)(scroll + i),
				  (scroll + i) == menu_idx);
	}
	OLED_Refresh();
}

//菜单关掉时显示当前模式的大字
static void show_mode_banner(void)
{
	OLED_Clear();
	if (mode == MODE_GAME)
	{
		OLED_ShowCN(32, 1, CN_YOU_XI);
		OLED_ShowCN(48, 1, CN_XI);
		OLED_ShowCN(64, 1, CN_MO);
		OLED_ShowCN(80, 1, CN_SHI);
	}
	else
	{
		OLED_ShowCN(32, 1, CN_LAN);
		OLED_ShowCN(48, 1, CN_YA);
		OLED_ShowCN(64, 1, CN_MO);
		OLED_ShowCN(80, 1, CN_SHI);
	}
	OLED_Refresh();
}

//画空心矩形框
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

//画实心圆, 摇杆位置点用
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

//画十字准星
static void draw_cross(uint8_t cx, uint8_t cy, uint8_t len)
{
	int16_t i;
	for (i = -(int16_t)len; i <= (int16_t)len; i++)
	{
		OLED_DrawPixel(cx, (uint8_t)((int16_t)cy + i), 1);
		OLED_DrawPixel((uint8_t)((int16_t)cx + i), cy, 1);
	}
}

//增量刷新状态: 像素坐标没变就跳过整屏重画, 吸收 ADC 噪声
static uint8_t  prev_lx = 0xFF, prev_ly = 0xFF;
static uint8_t  prev_rx = 0xFF, prev_ry = 0xFF;
static uint8_t  prev_fl = 0,    prev_fr = 0;
static uint8_t  test_force = 1;	 //1=强制全量重画(进入界面/首次)

//进测试界面重置 prev_*, 强制全量重画
static void test_enter(void)
{
	Joystick_Calibrate_Ex(0);	 //重新校准中心 + 重置自适应量程
	prev_lx = 0xFF; prev_ly = 0xFF;
	prev_rx = 0xFF; prev_ry = 0xFF;
	prev_fl = 0;    prev_fr = 0;
	test_force = 1;
}

void Menu_ForceRedraw(void)
{
	prev_lx = 0xFF; prev_ly = 0xFF;
	prev_rx = 0xFF; prev_ry = 0xFF;
	prev_fl = 0;    prev_fr = 0;
	test_force   = 1;
	menu_dirty   = 1;
}

//测试界面: 扳机条 + 摇杆框
static void test_draw(void)
{
	int16_t  nlx, nly, nrx, nry;	 //归一化(±1024)
	int16_t  ntl, ntr;		 //扳机归一化(0~1024)
	int16_t  new_lx, new_ly, new_rx, new_ry;
	uint8_t  new_fl, new_fr;

	uint8_t by = 0, bh = 4;
	uint8_t bx_l = 6,  bx_r = 74, bw = 48;	 //扳机条: 6..54 / 74..122
	uint8_t lx = 6,  ly = 8,  lw = 48, lh = 46;	 //左摇杆框
	uint8_t rx = 74;				 //右摇杆框
	uint8_t cx_l, cy_l, cx_r, cy_r;
	int16_t hx, hy;

	cx_l = (uint8_t)(lx + lw / 2);
	cy_l = (uint8_t)(ly + lh / 2);
	cx_r = (uint8_t)(rx + lw / 2);
	cy_r = (uint8_t)(ly + lh / 2);
	hx = (int16_t)(lw / 2 - 4);
	hy = (int16_t)(lh / 2 - 4);

	//取 GetNorm, 与 HID 同源
	nlx = Joystick_GetNorm(AXIS_CH_LX);
	nly = Joystick_GetNorm(AXIS_CH_LY);
	nrx = Joystick_GetNorm(AXIS_CH_RX);
	nry = Joystick_GetNorm(AXIS_CH_RY);
	ntl = Joystick_GetNormAbs(ADC_CH_LT);
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

	if (!test_force &&
		prev_lx == (uint8_t)new_lx && prev_ly == (uint8_t)new_ly &&
		prev_rx == (uint8_t)new_rx && prev_ry == (uint8_t)new_ry &&
		prev_fl == new_fl && prev_fr == new_fr)
	{
		return;
	}

	OLED_Clear();

	draw_box(bx_l, by, bw, bh);
	if (new_fl > 0)
		OLED_FillRect((uint8_t)(bx_l + 1), (uint8_t)(by + 1), new_fl, (uint8_t)(bh - 2), 1);
	draw_box(bx_r, by, bw, bh);
	if (new_fr > 0)
		OLED_FillRect((uint8_t)(bx_r + 1), (uint8_t)(by + 1), new_fr, (uint8_t)(bh - 2), 1);

	draw_box(lx, ly, lw, lh);
	draw_cross(cx_l, cy_l, 3);
	draw_disc((uint8_t)new_lx, (uint8_t)new_ly, 4);
	draw_box(rx, ly, lw, lh);
	draw_cross(cx_r, cy_r, 3);
	draw_disc((uint8_t)new_rx, (uint8_t)new_ry, 4);

	OLED_Refresh();

	prev_lx = (uint8_t)new_lx; prev_ly = (uint8_t)new_ly;
	prev_rx = (uint8_t)new_rx; prev_ry = (uint8_t)new_ry;
	prev_fl = new_fl; prev_fr = new_fr;
	test_force = 0;
}

//从 Flash 读参数还原设置, 读不到就用默认值
void Menu_Init(void)
{
	PARAMS p;
	if (FlashSave_Load(&p))
	{
		unpack_params(&p);
	}

	menu_state = MENU_CLOSED;
	menu_idx = 0;
	menu_dirty = 1;
}

//每帧: 测试 -> 全局键 -> 导航 -> 重画 -> 落盘
//状态 CLOSED(模式页) / OPEN(列表) / TEST(测试界面)
void Menu_Tick(void)
{
	//测试手柄界面(最高优先, 实时刷新)
	if (menu_state == MENU_TEST)
	{
		//HOME/MENU 退出: 开机进的回模式界面, 菜单进的回列表
		if (Joystick_KeyPressed(KEY_HOME) || Joystick_KeyPressed(KEY_MENU))
		{
			menu_state = test_from_boot ? MENU_CLOSED : MENU_OPEN;
			test_from_boot = 0;
			menu_dirty = 1;
		}
		else if (Joystick_KeyPressed(KEY_VIEW))
		{
			mode = (mode == MODE_GAME) ? MODE_BT : MODE_GAME;
			menu_state = MENU_CLOSED;
			menu_idx = 0;
			test_from_boot = 0;
			mark_param_changed();
			menu_dirty = 1;
		}
		else
		{
			test_draw();
		}
		return;
	}

	//中间三键(全局)
	if (Joystick_KeyPressed(KEY_VIEW))	 //切换模式
	{
		mode = (mode == MODE_GAME) ? MODE_BT : MODE_GAME;
		menu_state = MENU_CLOSED;
		menu_idx = 0;
		mark_param_changed();
		menu_dirty = 1;
	}
	else if (Joystick_KeyPressed(KEY_MENU))	 //打开/关闭菜单
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
	else if (Joystick_KeyPressed(KEY_HOME))	 //菜单返回
	{
		if (menu_state == MENU_OPEN) { menu_state = MENU_CLOSED; menu_dirty = 1; }
	}

	//菜单列表态: 上下移光标 / 左右调参或进测试
	if (menu_state == MENU_OPEN)
	{
		uint8_t cnt = item_count();
		int8_t  delta = 0;

		if (Joystick_KeyPressed(KEY_UP))         { delta = -1; }
		else if (Joystick_KeyPressed(KEY_DOWN))  { delta =  1; }

		//左摇杆上下移光标(边沿触发, 回中后才能再触发)
		if (delta == 0)
		{
			int16_t sd = stick_vert_diff();
			if (sd >  STICK_MOVE_TH)      { if (!stick_active) { delta =  1; stick_active = 1; } }
			else if (sd < -STICK_MOVE_TH) { if (!stick_active) { delta = -1; stick_active = 1; } }
			else                          { stick_active = 0; }
		}
		else
		{
			stick_active = 1;
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

		//左右调参; 游戏模式 idx4 左右即进入测试
		if (Joystick_KeyPressed(KEY_LEFT) || Joystick_KeyPressed(KEY_RIGHT))
		{
			if (mode == MODE_GAME && menu_idx == 4)
			{
				test_enter();
				menu_state = MENU_TEST;
				return;
			}
			adjust_cur(Joystick_KeyPressed(KEY_LEFT) ? (int8_t)-1 : (int8_t)1);
			menu_dirty = 1;
		}
	}

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

	save_if_pending();
}

//当前模式: MODE_GAME / MODE_BT
uint8_t Menu_GetMode(void)
{
	return mode;
}

//蓝牙帧配置(包头包尾/hex/换行)
BT_CONFIG *Menu_GetBTConfig(void)
{
	return &bt_cfg;
}

//开机直接进测试界面, 退出时回模式页
void Menu_EnterTest(void)
{
	test_from_boot = 1;
	menu_state = MENU_TEST;
	menu_dirty = 0;	 //测试界面走每帧 test_draw, 不依赖 dirty
}

//熄屏秒数, 0=不熄
uint8_t Menu_GetIdleTimeout(void)
{
	return idle_secs[idle_idx];
}
