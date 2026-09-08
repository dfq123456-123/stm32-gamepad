/*******************************************************************************
 * adc.c - F103 6 路 ADC 驱动 (ADC1, PA0~PA5)
 *
 * 采样时间用 ADC_SampleTime_239Cycles5 (最慢档), 因为摇杆电位器内阻大,
 * 采样时间太短会导致读数跳动不稳。12MHz 下单通道约 21us, 6 路约 126us,
 * 相对 10ms 发送周期完全可以忽略。
 ******************************************************************************/
#include "adc.h"

/* 过采样次数: 每通道连续采样 N 次取均值。
 * 根治相邻通道(PA1↔PA2)寄生耦合串扰: 串扰尖峰是随机/瞬态的,
 * 多次采样平均后正负抵消, 从采样源头磨平, 而非靠后级滤波事后擦。
 * 配合 ADC_ReadAll 的交错采样顺序(0,2,4,1,3,5)双保险。
 * N=8 时单通道约 8×21us≈170us, 6 路共约 1ms, 相对 10ms 帧周期可忽略。 */
#ifndef ADC_OVERSAMPLE
#define ADC_OVERSAMPLE  8
#endif

/*******************************************************************************
 * 函数名: MY_ADC_Init
 * 说明:   ADC1 独立模式 + 软件触发 + 单次转换, 每次读通道前重新配置规则组
 *         校准: 上电执行一次 ADC 自校准 (F103 必做)
 ******************************************************************************/
void MY_ADC_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure;
    ADC_InitTypeDef   ADC_InitStructure;

    /* 1. 使能时钟: GPIOA + ADC1 都在 APB2 上 */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);

    /* 2. ADC 时钟分频: PCLK2(72MHz) / 6 = 12MHz (ADC 最高 14MHz) */
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);

    /* 3. PA0~PA5 全部配为模拟输入 */
    GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 |
                                   GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;   /* 模拟输入 */
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* 4. ADC1 参数 */
    ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent;   /* 独立模式 */
    ADC_InitStructure.ADC_ScanConvMode       = DISABLE;                /* 单通道轮询 */
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;                /* 单次转换 */
    ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None; /* 软件触发 */
    ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;    /* 右对齐 12bit */
    ADC_InitStructure.ADC_NbrOfChannel       = 1;
    ADC_Init(ADC1, &ADC_InitStructure);

    /* 5. 使能 ADC1 */
    ADC_Cmd(ADC1, ENABLE);

    /* 6. ADC 自校准 (F103 标准库必须执行, 否则精度异常) */
    ADC_ResetCalibration(ADC1);
    while (ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while (ADC_GetCalibrationStatus(ADC1));
}

/*******************************************************************************
 * 内部: EOC 忙等超时上限(正常单次转换约 21us, 超时说明外设异常, 避免死等卡死主循环)
 ******************************************************************************/
#define ADC_EOC_TIMEOUT  200000u

static uint8_t adc_wait_eoc(void)
{
    uint32_t t = 0;
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET)
    {
        if (++t > ADC_EOC_TIMEOUT)
        {
            return 0;
        }
    }
    return 1;
}

/*******************************************************************************
 * 函数名: ADC_GetValue
 * 参数:   ch = 0~5 (见 adc.h 的 ADC_CH_xxx 宏)
 * 返回:   12bit 原始值 0~4095
 * 说明:   软件单次转换轮询, 与 day11/joystick 的 ADC_GetValue 行为一致
 *          通道切换后先做一次"丢弃转换"(dummy), 让采样保持电容充分稳定到新通道
 *          电压, 再做第二次转换取值, 消除相邻通道串扰(PA1↔PA2 等)。
 ******************************************************************************/
uint16_t ADC_GetValue(uint8_t ch)
{
    uint32_t sum = 0;
    uint8_t  i;

    /* 配置规则组: 通道 ch, 采样顺序 1, 采样时间 239.5 周期 */
    ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_239Cycles5);

    /* 第 1 次转换: dummy, 冲刷采样保持电容上上一通道的残留电荷 */
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);
    if (adc_wait_eoc() == 0)
    {
        return 0;   /* 超时保护 */
    }
    (void)ADC_GetConversionValue(ADC1);          /* 丢弃, 仅为让采样电容充到 ch 真实电压 */

    /* 过采样: 连续 ADC_OVERSAMPLE 次采样取平均, 磨平耦合尖峰(根治串扰) */
    for (i = 0; i < ADC_OVERSAMPLE; i++)
    {
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        if (adc_wait_eoc() == 0)
        {
            return 0;   /* 超时保护 */
        }
        sum += ADC_GetConversionValue(ADC1);
    }

    return (uint16_t)(sum / ADC_OVERSAMPLE);
}

/*******************************************************************************
 * 函数名: ADC_ReadAll
 * 参数:   buf 指向至少 6 个 uint16_t 的数组
 * 说明:   按顺序填充 buf[0..5] = L-VRX, L-VRY, R-VRX, R-VRY, LT, RT
 ******************************************************************************/
void ADC_ReadAll(uint16_t *buf)
{
    /* 交错顺序读取: 0,2,4,1,3,5 (存储仍按 ADC_CH_xxx 下标对应)
     * 目的: 原顺序 0,1,2,3,4,5 中 PA1(左摇杆X) 紧挨在 PA2(右摇杆数据源) 的
     * 前一次转换, 多路开关切换/采样电容残留的通道间串扰会落在 PA2 上,
     * 死区调低后表现为"动左摇杆X, 右摇杆Y 跟着动"。交错后 PA2 的前一次
     * 转换变为 PA4(扳机), 与摇杆通道在时间上隔开, 消除该串扰路径。 */
    static const uint8_t order[ADC_CH_NUM] = { 0, 2, 4, 1, 3, 5 };
    uint8_t i;
    for (i = 0; i < ADC_CH_NUM; i++)
    {
        buf[order[i]] = ADC_GetValue(order[i]);
    }
}
