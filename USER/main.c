/*******************************************************************************
 * main.c - STM32F103C8T6 多功能游戏手柄固件(根据博主固件反汇编逆向重写)
 *
 * 功能:
 *   采集双摇杆(4轴) + 左右扳机(2轴) + 15 键, 双模式上报:
 *     1. 游戏控制器模式(USB HID, View 键切换): 8 字节标准 HID 报告(11按钮+dpad+5轴)
 *     2. 蓝牙控制器模式(UART 透传, View 键切换): 可配置包头/包尾/hex/换行的串口帧
 *   外加: OLED 菜单(Menu 键打开, 含死区/滤波/测试手柄)、WS2812B 氛围灯强制熄灭(PA8 持续拉低)
 *
 * 主循环 10ms 一帧(100Hz):
 *   Joystick_Scan()  -> 15 键消抖 + 6 路 ADC + 滤波 + 死区 + 动态校准
 *   Menu_Tick()      -> 三键语义(Home=返回/Menu=开关菜单/View=切换模式) + OLED 刷新
 *   Joystick_Send()  -> 游戏模式下组 8 字节 USB HID 报告并经 EP1 发送
 *   BT_SendFrame()   -> 蓝牙模式下组帧并经 USART1 发送
 *
 * 开机流程: USB(提前, 上电即就绪) -> 串口 -> 手柄(无校准) -> 灯带(熄灭) -> OLED 自检 ->
 *   OLED -> 开机动画 -> 菜单 -> 开机自动校准 -> 自动进入"测试手柄"界面 -> 主循环
 *
 * 工程结构:
 *   USER/main.c          主循环
 *   HARDWARE/usart.c/h   串口1(PA9=TX, PA10=RX) + printf + 蓝牙帧发送
 *   HARDWARE/adc.c/h     ADC1 6 路(PA0~PA5)
 *   HARDWARE/joystick.c/h按键扫描 + HID 报告组包 + 校准(含进度回调)
 *   HARDWARE/oled.c/h    软件 I2C OLED(PB12=SCL, PB13=SDA) + 校准进度界面
 *   HARDWARE/menu.c/h    双模式菜单系统 + 测试手柄界面
 *   HARDWARE/delay.c/h   SysTick 延时
 *   USB /                USB HID GamePad(ST USB-FS-Device 库)
 *   Libraries/           STM32 标准外设库 + USB 驱动库
 ******************************************************************************/
#include "stm32f10x.h"
#include "stdio.h"
#include "usart.h"
#include "adc.h"
#include "joystick.h"
#include "delay.h"
#include "oled.h"
#include "menu.h"
#include "boot_splash.h"
#include "hw_config.h"
#include "usb_lib.h"
#include "usb_pwr.h"

/* 氛围灯(WS2812B, DIN=PA8)强制熄灭
 * 背景: 该灯效是本助手早前加入手柄项目时点亮的, 之后三颗灯常亮无法熄灭; 删除原驱动后
 * PA8 浮空, 灯珠锁存最后状态仍亮。WS2812B 在 DIN 持续低电平超过复位时间(>=280us)后会
 * 清除内部锁存、所有 LED 熄灭并保持。纯低电平(0V)在 5V 供电下也能可靠识别为低,
 * 不依赖之前 3.3V 高电平不足(高阈值≈0.7*VDD=3.5V)导致发"全0帧"失败的坑。 */
static void WS2812_ForceOff(void)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    gpio.GPIO_Pin   = GPIO_Pin_8;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(GPIOA, &gpio);
    GPIO_ResetBits(GPIOA, GPIO_Pin_8);   /* 持续拉低 -> 复位熄灭, 此后不再改变 */
}

int main(void)
{
    uint16_t axis[ADC_CH_NUM];
    uint16_t btn_bitmap;

    /* 1. 初始化 USB HID 手柄(PA11=DM, PA12=DP, D+ 硬件 1.5k 上拉)
     *    提前到最前: 时钟已由 SystemInit 配成 72MHz, USB = PLL/1.5 = 48MHz;
     *    D+ 上拉后主机立即开始枚举, USB 越早就绪越不会被后续 delay(800ms) + 开机动画 +
     *    摇杆校准(合计 2~4s)拖到主机枚举超时 -> 表现为"PC 认不到设备"。 */
    Set_System();
    USB_Interrupts_Config();
    Set_USBClock();
    USB_Init();

    /* 2. 初始化串口1: 115200, 接 HC-05/06 蓝牙模块(TX<->RX 交叉) */
    MY_USART1_Init(115200);

    /* 3. 初始化手柄: 按键 GPIO + ADC1(此时不做校准) */
    MY_Joystick_Init();

    /* 3.5 氛围灯强制熄灭: PA8 推挽持续拉低(灯效早前加入后常亮, 见 WS2812_ForceOff) */
    WS2812_ForceOff();

    /* 4. OLED 上电等待 + I2C 总线自检(结果经串口输出) */
    delay_ms(800);
    {
        uint8_t ack78, ack7A;

        ack78 = OLED_Probe(0x78);
        ack7A = OLED_Probe(0x7A);
        printf("OLED probe: 0x78=%u 0x7A=%u\r\n",
               (unsigned int)ack78, (unsigned int)ack7A);
        if (ack7A && !ack78)
        {
            OLED_SetAddr(0x7A);   /* 自动适配 0x7A 地址 */
            printf("OLED: using I2C addr 0x7A\r\n");
        }
        else if (!ack78 && !ack7A)
        {
            printf("WARNING: OLED not found on I2C bus, check wiring/power\r\n");
        }
    }

    /* 5. 初始化 OLED(软件 I2C) */
    OLED_Init();

    /* 5.5 开机动画: 头像 + "肚子隆起来了" + 名字水印(停留后清屏) */
    Boot_Splash();

    /* 6. 初始化菜单系统(默认游戏模式) */
    Menu_Init();

    /* 7. 开机自动校准摇杆/扳机(无进度 UI) */
    Joystick_Calibrate_Ex(0);   /* 开机自动校准(无进度 UI, 与今早最初态一致) */

    /* 8. 校准完成 -> 自动进入"测试手柄"界面 */
    Menu_EnterTest();

    /* 9. 上电提示(串口助手可见) */
    printf("STM32F103 GamePad Ready! USB HID + BT dual mode.\r\n");

    /* 10. 主循环: 100Hz 采集 + 模式分发 + 显示 */
    {
        static uint32_t idle_acc = 0;   /* 无活动累计时间(ms) */
        static uint8_t  oled_off = 0;   /* 空闲熄屏状态: 1=已关显示 */

        while (1)
        {
            /* 采集一帧: 按键消抖 + ADC + 滤波 + 死区 + 动态校准 */
            Joystick_Scan();

            /* 空闲熄屏: 无活动超过设定秒数则关显示省电, 任意输入再点亮 */
            {
                uint8_t active = Joystick_IsActive();
                if (active)
                {
                    idle_acc = 0;
                    if (oled_off)
                    {
                        OLED_DisplayOn();       /* 恢复显示 */
                        Menu_ForceRedraw();     /* 强制全量重画, 清掉熄屏期间残留的坏像素 */
                        oled_off = 0;
                    }
                }
                else if (!oled_off)
                {
                    uint8_t to = Menu_GetIdleTimeout();   /* 秒, 0=关闭 */
                    if (to != 0)
                    {
                        idle_acc += 10;    /* 主循环 10ms 一帧 */
                        if (idle_acc >= (uint32_t)to * 1000)
                        {
                            OLED_DisplayOff();
                            oled_off = 1;
                        }
                    }
                }
            }

            /* 菜单/模式状态机(含 OLED 刷新, 测试界面每帧实时刷新) */
            Menu_Tick();

        /* 取处理后数据(供蓝牙帧组包用) */
        Joystick_GetProcessed(axis, &btn_bitmap);

        /* 按当前模式分发上报 */
        if (Menu_GetMode() == MODE_GAME)
        {
            /* 游戏模式: USB HID, 枚举完成且上一帧发送完毕才发 */
            if ((bDeviceState == CONFIGURED) && PrevXferComplete)
            {
                Joystick_Send();    /* 8 字节 HID 报告 */
            }
        }
        else
        {
            /* 蓝牙模式: UART 组帧发送 */
            BT_SendFrame(axis, btn_bitmap, Menu_GetBTConfig());
        }

        delay_ms(10);   /* 10ms 周期 */
        }
    }
}
