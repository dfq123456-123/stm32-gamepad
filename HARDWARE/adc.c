/*******************************************************************************
 * adc.c - F103 6 路 ADC 驱动 (ADC1, PA0~PA5)
 *
 * 采样时间用 ADC_SampleTime_239Cycles5 (最慢档), 因为摇杆电位器内阻大,
 * 采样时间太短会导致读数跳动不稳。12MHz 下单通道约 21us, 6 路约 126us,
 * 相对 10ms 发送周期完全可以忽略。
 ******************************************************************************/
#include "adc.h"

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
 * 函数名: ADC_GetValue
 * 参数:   ch = 0~5 (见 adc.h 的 ADC_CH_xxx 宏)
 * 返回:   12bit 原始值 0~4095
 * 说明:   软件单次转换轮询, 与 day11/joystick 的 ADC_GetValue 行为一致
 ******************************************************************************/
uint16_t ADC_GetValue(uint8_t ch)
{
    /* 配置规则组: 通道 ch, 采样顺序 1, 采样时间 239.5 周期 */
    ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_239Cycles5);

    /* 软件启动转换 */
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    /* 等待转换完成 */
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET);

    /* 返回转换结果 */
    return (uint16_t)ADC_GetConversionValue(ADC1);
}

/*******************************************************************************
 * 函数名: ADC_ReadAll
 * 参数:   buf 指向至少 6 个 uint16_t 的数组
 * 说明:   按顺序填充 buf[0..5] = L-VRX, L-VRY, R-VRX, R-VRY, LT, RT
 ******************************************************************************/
void ADC_ReadAll(uint16_t *buf)
{
    uint8_t i;
    for (i = 0; i < ADC_CH_NUM; i++)
    {
        buf[i] = ADC_GetValue(i);
    }
}
