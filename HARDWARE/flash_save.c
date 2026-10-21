//flash_save.c  参数断电保存(F103 内部 Flash)
//最后一页 0x0800FC00~0x0800FFFF 专门放参数, 不写代码
//存储格式: magic(4)+version(1)+参数体+crc(2)

#include "flash_save.h"
#include "stm32f10x.h"

#define SAVE_PAGE_ADDR   0x0800FC00u	//最后一页起始地址
#define SAVE_MAGIC       0x53415645u	//"SAVE", 用来判断是不是首次上电

//解锁 Flash: 往 KEYR 依次写两个魔数
static void flash_unlock(void)
{
	FLASH->KEYR=0x45670123;
	FLASH->KEYR=0xCDEF89AB;
}

//上锁: 写完恢复只读
static void flash_lock(void)
{
	FLASH->CR|=FLASH_CR_LOCK;
}

//等上一次操作做完
static void flash_wait_busy(void)
{
	while(FLASH->SR & FLASH_SR_BSY){}
}

//擦除一页
static void flash_erase_page(uint32_t addr)
{
	flash_wait_busy();
	FLASH->CR|=FLASH_CR_PER;			//切到页擦除模式
	FLASH->AR=addr;						//目标页地址
	FLASH->CR|=FLASH_CR_STRT;			//启动擦除
	flash_wait_busy();
	FLASH->CR&=~FLASH_CR_PER;
	FLASH->SR=FLASH_SR_EOP;
}

//写半字, F103 的 Flash 必须按 16bit 对齐写, 不能单字节塞
static void flash_program_halfword(uint32_t addr, uint16_t data)
{
	flash_wait_busy();
	FLASH->CR|=FLASH_CR_PG;				//编程模式
	*(volatile uint16_t *)addr=data;
	flash_wait_busy();
	FLASH->CR&=~FLASH_CR_PG;
	FLASH->SR=FLASH_SR_EOP;
}

//CRC16-CCITT, 校验参数有没有损坏
static uint16_t crc16(const uint8_t *buf, uint32_t len)
{
	uint16_t crc=0xFFFF;
	uint32_t i;
	uint8_t j;

	for(i=0;i<len;i++)
	{
		uint8_t b=buf[i];
		crc^=(uint16_t)b<<8;
		for(j=0;j<8;j++)
		{
			if(crc & 0x8000)crc=(uint16_t)((crc<<1)^0x1021);
			else crc<<=1;
		}
	}
	return crc;
}

//读参数, 1=有效 0=无效
uint8_t FlashSave_Load(PARAMS *p)
{
	const uint8_t *base=(const uint8_t *)SAVE_PAGE_ADDR;
	const uint8_t *src;
	uint8_t *dst;
	uint8_t i;
	uint16_t stored_crc, calc_crc;
	uint32_t magic=*(const uint32_t *)base;
	uint8_t ver=base[4];

	//先看 magic 和版本
	if(magic!=SAVE_MAGIC || ver!=PARAM_VERSION)return 0;

	//拷参数体(跳过 magic 和 version)
	dst=(uint8_t *)p;
	src=base+5;
	for(i=0;i<sizeof(PARAMS);i++)dst[i]=src[i];

	//校验 CRC, 覆盖 magic + version + 参数体
	stored_crc=*(const uint16_t *)(base+5+sizeof(PARAMS));
	calc_crc=crc16(base, 5+sizeof(PARAMS));
	if(stored_crc!=calc_crc)return 0;

	return 1;
}

//保存参数: 解锁→擦页→写入→上锁
uint8_t FlashSave_Save(const PARAMS *p)
{
	uint8_t raw[5+sizeof(PARAMS)+2];
	uint16_t crc, i;
	uint16_t total=(uint16_t)(5+sizeof(PARAMS)+2);

	//组装缓冲: magic + version + 参数体
	raw[0]=(uint8_t)(SAVE_MAGIC & 0xFF);
	raw[1]=(uint8_t)((SAVE_MAGIC >> 8) & 0xFF);
	raw[2]=(uint8_t)((SAVE_MAGIC >> 16) & 0xFF);
	raw[3]=(uint8_t)((SAVE_MAGIC >> 24) & 0xFF);
	raw[4]=PARAM_VERSION;

	{
		const uint8_t *src=(const uint8_t *)p;
		for(i=0;i<sizeof(PARAMS);i++)raw[5+i]=src[i];
	}

	crc=crc16(raw, 5+sizeof(PARAMS));
	raw[5+sizeof(PARAMS)]=(uint8_t)(crc & 0xFF);
	raw[5+sizeof(PARAMS)+1]=(uint8_t)((crc >> 8) & 0xFF);

	flash_unlock();
	flash_erase_page(SAVE_PAGE_ADDR);

	//按半字逐个写入, Stack_Size 只有 0x400
	for(i=0;i<total;i+=2)
	{
		uint16_t lo=raw[i];
		uint16_t hi=(uint16_t)((i+1<total)?raw[i+1]:0xFF);
		flash_program_halfword(SAVE_PAGE_ADDR+i, (uint16_t)(lo | (hi<<8)));
	}
	flash_lock();

	//写完回读比对, 有一个字节不对就算失败
	{
		const uint8_t *chk=(const uint8_t *)SAVE_PAGE_ADDR;
		for(i=0;i<total;i++)
		{
			if(chk[i]!=raw[i])return 0;
		}
	}
	return 1;
}
