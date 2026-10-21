#include "adc.h"

//每通道过采样 8 次取均值, 配合交错顺序压相邻通道串扰
#ifndef ADC_OVERSAMPLE
#define ADC_OVERSAMPLE  8
#endif

//ADC1 初始化: PA0~PA5 六路, 单次非扫描, 每路手动切通道
void MY_ADC_Init(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
	ADC_InitTypeDef   ADC_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_ADC1, ENABLE);
	RCC_ADCCLKConfig(RCC_PCLK2_Div6);	 //PCLK2(72MHz)/6=12MHz (ADC最高14MHz)

	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2 |
								   GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	ADC_InitStructure.ADC_Mode               = ADC_Mode_Independent;
	ADC_InitStructure.ADC_ScanConvMode       = DISABLE;
	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
	ADC_InitStructure.ADC_ExternalTrigConv   = ADC_ExternalTrigConv_None;
	ADC_InitStructure.ADC_DataAlign          = ADC_DataAlign_Right;
	ADC_InitStructure.ADC_NbrOfChannel       = 1;
	ADC_Init(ADC1, &ADC_InitStructure);

	ADC_Cmd(ADC1, ENABLE);

	//F103 必须自校准, 不做精度会飘
	ADC_ResetCalibration(ADC1);
	while (ADC_GetResetCalibrationStatus(ADC1));
	ADC_StartCalibration(ADC1);
	while (ADC_GetCalibrationStatus(ADC1));
}

#define ADC_EOC_TIMEOUT  200000u	 //单次转换约 21us

//等转换完成, 超时返回 0 防止外设异常卡死
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

//读单路: 先 dummy 一次, 再过采样取均值
uint16_t ADC_GetValue(uint8_t ch)
{
	uint32_t sum = 0;
	uint8_t  i;

	ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_239Cycles5);	 //最慢档: 摇杆电位器内阻大, 采快了读数跳

	//dummy 一次, 冲掉采样电容上上一通道的残留电荷
	ADC_SoftwareStartConvCmd(ADC1, ENABLE);
	if (adc_wait_eoc() == 0)
	{
		return 0;
	}
	(void)ADC_GetConversionValue(ADC1);

	for (i = 0; i < ADC_OVERSAMPLE; i++)
	{
		ADC_SoftwareStartConvCmd(ADC1, ENABLE);
		if (adc_wait_eoc() == 0)
		{
			return 0;
		}
		sum += ADC_GetConversionValue(ADC1);
	}

	return (uint16_t)(sum / ADC_OVERSAMPLE);
}

//一次读满 6 路
void ADC_ReadAll(uint16_t *buf)
{
	//交错顺序 0,2,4,1,3,5: 隔开相邻通道
	static const uint8_t order[ADC_CH_NUM] = { 0, 2, 4, 1, 3, 5 };
	uint8_t i;
	for (i = 0; i < ADC_CH_NUM; i++)
	{
		buf[order[i]] = ADC_GetValue(order[i]);
	}
}
