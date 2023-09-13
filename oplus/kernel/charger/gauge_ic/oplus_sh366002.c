// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2023 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[sh366002] %s(%d): " fmt, __func__, __LINE__

#include <linux/version.h>
#include <asm/unaligned.h>
#include <linux/acpi.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/idr.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/param.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/random.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <asm/div64.h>
#include <stdbool.h>

#include "oplus_bq27541.h"
#include "oplus_sh366002.h"

#include "../oplus_charger.h"

#define DUMP_SH366002_BLOCK BIT(0)
#define DUMP_SH366002_CURRENT_BLOCK BIT(1)

#define GAUGE_LOG_MIN_TIMESPAN 10

#define CMD_CNTLSTATUS_SEAL 0x6000
#define CMD_UNSEALKEY 0x19861115
#define CMD_FULLKEY 0xFFFFFFFF

#define AFI_DATE_BLOCKA_INDEX 24
#define AFI_MANU_BLOCKA_INDEX 28
#define AFI_MANU_SINOWEALTH 0x02
#define CHECK_VERSION_ERR -1
#define CHECK_VERSION_OK 0
#define CHECK_VERSION_FW BIT(0)
#define CHECK_VERSION_AFI BIT(1)
#define CHECK_VERSION_TS BIT(2)
#define CHECK_VERSION_WHOLE_CHIP (CHECK_VERSION_FW | CHECK_VERSION_AFI | CHECK_VERSION_TS)
#define FILE_DECODE_RETRY 2
#define FILE_DECODE_DELAY 100

#define WRITE_BUF_MAX_LEN 32
#define MAX_BUF_LEN 1024
#define DF_PAGE_LEN 32
#define CMD_SBS_DELAY 3
#define CMDMASK_MASK 0xFF000000
#define CMDMASK_SINGLE 0x01000000
#define CMDMASK_WRITE 0x80000000
#define CMDMASK_CNTL_R 0x08000000
#define CMDMASK_CNTL_W (CMDMASK_WRITE | CMDMASK_CNTL_R)
#define CMDMASK_MANUBLOCK_R 0x04000000
#define CMDMASK_MANUBLOCK_W (CMDMASK_WRITE | CMDMASK_MANUBLOCK_R)

#define CMD_CNTLSTATUS (CMDMASK_CNTL_R | 0x0000)
#define CMD_CNTL 0x00
#define CMD_SEAL 0x0020
#define CMD_DFSTART 0x61
#define CMD_DFCLASS 0x3E
#define CMD_DFPAGE 0x3F
#define CMD_BLOCK 0x40
#define CMD_CHECKSUM 0x60
#define CMD_BLOCKA (CMDMASK_MANUBLOCK_W | 0x01)
#define CMD_FWDATE1 (CMDMASK_CNTL_R | 0xD0)
#define CMD_FWDATE2 (CMDMASK_CNTL_R | 0xE2)

#define CMD_TEMPER 0x06
#define CMD_VOLTAGE 0x08
#define CMD_CURRENT 0x14
#define CMD_FILRC 0x10
#define CMD_FILFCC 0x12
#define CMD_CYCLECOUNT 0x2A
#define CMD_PASSEDC 0x34
#define CMD_DOD0 0x36
#define CMD_PACKCON 0x3A

#define CMD_GAUGEBLOCK1 (CMDMASK_CNTL_R | 0x00E3)
#define CMD_GAUGEBLOCK2 (CMDMASK_CNTL_R | 0x00E4)
#define CMD_GAUGEBLOCK3 (CMDMASK_CNTL_R | 0x00E5)
#define CMD_GAUGEBLOCK4 (CMDMASK_CNTL_R | 0x00E6)
#define CMD_GAUGEBLOCK5 (CMDMASK_CNTL_R | 0x00E7)
#define CMD_CURRENTBLOCK1 (CMDMASK_CNTL_R | 0x00EA)
#define CMD_CURRENTBLOCK2 (CMDMASK_CNTL_R | 0x00EB)
#define CMD_CURRENTBLOCK3 (CMDMASK_CNTL_R | 0x00EC)
#define CMD_CURRENTBLOCK4 (CMDMASK_CNTL_R | 0x00ED)

#define BUF2U16_BG(p) ((u16)(((u16)(u8)((p)[0]) << 8) | (u8)((p)[1])))
#define BUF2U16_LT(p) ((u16)(((u16)(u8)((p)[1]) << 8) | (u8)((p)[0])))
#define BUF2U32_BG(p)                                                                                                  \
	((u32)(((u32)(u8)((p)[0]) << 24) | ((u32)(u8)((p)[1]) << 16) | ((u32)(u8)((p)[2]) << 8) | (u8)((p)[3])))
#define BUF2U32_LT(p)                                                                                                  \
	((u32)(((u32)(u8)((p)[3]) << 24) | ((u32)(u8)((p)[2]) << 16) | ((u32)(u8)((p)[1]) << 8) | (u8)((p)[0])))
#define GAUGEINFO_LEN 32
#define GAUGESTR_LEN 512
#define U64_MAXVALUE 0xFFFFFFFFFFFFFFFF
#define TEMPER_OFFSET 2731
#define READBUF_MAXLEN 32

#define IIC_ADDR_OF_2_KERNEL(addr) ((u8)((u8)addr >> 1))

/* file_decode_process */
#define OPERATE_READ 1
#define OPERATE_WRITE 2
#define OPERATE_COMPARE 3
#define OPERATE_WAIT 4

#define ERRORTYPE_NONE 0
#define ERRORTYPE_ALLOC 1
#define ERRORTYPE_LINE 2
#define ERRORTYPE_COMM 3
#define ERRORTYPE_COMPARE 4
#define ERRORTYPE_FINAL_COMPARE 5

/* delay: b0: operate, b1: 2, b2-b3: time, big-endian */
/* other: b0: operate, b1: TWIADR, b2: reg, b3: data_length, b4...end: data */
#define INDEX_TYPE 0
#define INDEX_ADDR 1
#define INDEX_REG 2
#define INDEX_LENGTH 3
#define INDEX_DATA 4
#define INDEX_WAIT_LENGTH 1
#define INDEX_WAIT_HIGH 2
#define INDEX_WAIT_LOW 3
#define LINELEN_WAIT 4
#define LINELEN_READ 4
#define LINELEN_COMPARE 4
#define LINELEN_WRITE 4
#define FILEDECODE_STRLEN 96
#define COMPARE_RETRY_CNT 2
#define COMPARE_RETRY_WAIT 50
#define BUF_MAX_LENGTH 512

int sh366002_dbg = 0;
module_param(sh366002_dbg, int, 0644);
MODULE_PARM_DESC(sh366002_dbg, "debug sh366002");

static s32 __fg_read_byte(struct chip_bq27541 *chip, u8 reg, u8 *val)
{
	s32 ret;

	if (!chip || !chip->client) {
		pr_err("chip or chip->client NULL,return\n");
		return 0;
	}

	if (oplus_is_rf_ftm_mode())
		return 0;

	mutex_lock(&chip->chip_mutex);
	ret = i2c_smbus_read_byte_data(chip->client, reg);
	if (ret < 0) {
		pr_err("i2c read byte fail: can't read from reg 0x%02X\n", reg);
		mutex_unlock(&chip->chip_mutex);
		return ret;
	}
	mutex_unlock(&chip->chip_mutex);
	*val = (u8)ret;

	return 0;
}

static s32 __fg_write_byte(struct chip_bq27541 *chip, u8 reg, u8 val)
{
	s32 ret;

	if (!chip || !chip->client) {
		pr_err("chip or chip->client NULL,return\n");
		return 0;
	}

	if (oplus_is_rf_ftm_mode())
		return 0;

	mutex_lock(&chip->chip_mutex);
	ret = i2c_smbus_write_byte_data(chip->client, reg, val);
	if (ret < 0) {
		pr_err("i2c write byte fail: can't write 0x%02X to reg 0x%02X\n", val, reg);
		mutex_unlock(&chip->chip_mutex);
		return ret;
	}
	mutex_unlock(&chip->chip_mutex);

	return 0;
}
static s32 __fg_read_word(struct chip_bq27541 *chip, u8 reg, u16 *val)
{
	s32 ret;

	if (!chip || !chip->client) {
		pr_err("chip or chip->client NULL,return\n");
		return 0;
	}

	if (oplus_is_rf_ftm_mode())
		return 0;

	mutex_lock(&chip->chip_mutex);
	ret = i2c_smbus_read_word_data(chip->client, reg);
	if (ret < 0) {
		pr_err("i2c read word fail: can't read from reg 0x%02X\n", reg);
		mutex_unlock(&chip->chip_mutex);
		return ret;
	}
	mutex_unlock(&chip->chip_mutex);
	*val = (u16)ret;

	return 0;
}

static s32 __fg_write_word(struct chip_bq27541 *chip, u8 reg, u16 val)
{
	s32 ret;

	if (!chip || !chip->client) {
		pr_err("chip or chip->client NULL,return\n");
		return 0;
	}

	if (oplus_is_rf_ftm_mode())
		return 0;

	mutex_lock(&chip->chip_mutex);
	ret = i2c_smbus_write_word_data(chip->client, reg, val);
	if (ret < 0) {
		pr_err("i2c write word fail: can't write 0x%02X to reg 0x%02X\n", val, reg);
		mutex_unlock(&chip->chip_mutex);
		return ret;
	}
	mutex_unlock(&chip->chip_mutex);

	return 0;
}

static s32 __fg_read_buffer(struct chip_bq27541 *chip, u8 reg, u8 length, u8 *val)
{
	static struct i2c_msg msg[2];
	s32 ret;

	if (!chip || !chip->client || !chip->client->adapter)
		return -ENODEV;

	if (oplus_is_rf_ftm_mode())
		return 0;

	msg[0].addr = chip->client->addr;
	msg[0].flags = 0;
	msg[0].buf = &reg;
	msg[0].len = sizeof(u8);
	msg[1].addr = chip->client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = val;
	msg[1].len = length;

	mutex_lock(&chip->chip_mutex);
	ret = (s32)i2c_transfer(chip->client->adapter, msg, ARRAY_SIZE(msg));
	mutex_unlock(&chip->chip_mutex);
	return ret;
}

static s32 __fg_write_buffer(struct chip_bq27541 *chip, u8 reg, u8 length, u8 *val)
{
	static struct i2c_msg msg[1];
	static u8 write_buf[WRITE_BUF_MAX_LEN];
	s32 ret;

	if (!chip || !chip->client || !chip->client->adapter)
		return -ENODEV;

	if (oplus_is_rf_ftm_mode())
		return 0;

	if ((length <= 0) || (length + 1 >= WRITE_BUF_MAX_LEN)) {
		pr_err("i2c write buffer fail: length invalid!\n");
		return -1;
	}

	memset(write_buf, 0, WRITE_BUF_MAX_LEN * sizeof(u8));
	write_buf[0] = reg;
	memcpy(&write_buf[1], val, length);

	msg[0].addr = chip->client->addr;
	msg[0].flags = 0;
	msg[0].buf = write_buf;
	msg[0].len = sizeof(u8) * (length + 1);

	mutex_lock(&chip->chip_mutex);
	ret = i2c_transfer(chip->client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0) {
		pr_err("i2c write buffer fail: can't write reg 0x%02X\n", reg);
		mutex_unlock(&chip->chip_mutex);
		return (s32)ret;
	}
	mutex_unlock(&chip->chip_mutex);

	return 0;
}

static s32 fg_read_sbs_word(struct chip_bq27541 *chip, u32 reg, u16 *val)
{
	s32 ret = -1;

	if ((reg & CMDMASK_CNTL_R) == CMDMASK_CNTL_R) {
		mutex_lock(&chip->bq28z610_alt_manufacturer_access);
		ret = __fg_write_word(chip, CMD_CNTL, (u16)reg);
		if (ret < 0) {
			mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
			return ret;
		}

		mdelay(CMD_SBS_DELAY);
		ret = __fg_read_word(chip, CMD_CNTL, val);
		mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	} else {
		ret = __fg_read_word(chip, (u8)reg, val);
	}

	return ret;
}

static int fg_write_sbs_word(struct chip_bq27541 *chip, u32 reg, u16 val)
{
	int ret;

	ret = __fg_write_word(chip, (u8)reg, val);

	return ret;
}

static int fg_block_checksum_calculate(u8 *buffer, u8 length)
{
	u8 sum = 0;
	if (length > 32)
		return -1;

	while (length--)
		sum += buffer[length];
	sum = ~sum;
	return (int)((u8)sum);
}

static int fg_read_block(struct chip_bq27541 *chip, u32 reg, u8 startIndex, u8 length, u8 *val)
{
	int ret = -1;
	int sum;
	u8 checksum;

	if ((startIndex >= READBUF_MAXLEN) || (length == 0))
		return -1;

	if (length > READBUF_MAXLEN)
		length = READBUF_MAXLEN;
	memset(val, 0, length);

	if (startIndex + length >= READBUF_MAXLEN)
		length = READBUF_MAXLEN - startIndex;

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	if ((reg & CMDMASK_CNTL_R) == CMDMASK_CNTL_R) {
		ret = __fg_write_word(chip, CMD_CNTL, (u16)reg);
		if (ret < 0) {
			pr_err("TYPE CNTL, write 0x00 fail! command=0x%08X\n", reg);
			goto fg_read_block_end;
		}
		msleep(CMD_SBS_DELAY);

		ret = __fg_read_buffer(chip, (u8)(CMD_BLOCK + startIndex), length, &val[startIndex]);
		if (ret < 0) {
			pr_err("TYPE CNTL, read 0x40 fail! command=0x%08X\n", reg);
			goto fg_read_block_end;
		}
		msleep(CMD_SBS_DELAY);

		/* check buffer */
		ret = __fg_read_buffer(chip, CMD_CHECKSUM, 1, &checksum);
		if (ret < 0)
			goto fg_read_block_end;

		if (startIndex != 0) {
			sum = fg_block_checksum_calculate(&val[startIndex], 32);
			if (sum != (int)((u8)checksum)) {
				ret = -1;
				pr_err("TYPE CNTL, verify checksum fail! command=0x%08X\n", reg);
				goto fg_read_block_end;
			} else {
				ret = 0;
			}
		} else {
			ret = 0;
		}
	} else if ((reg & CMDMASK_MANUBLOCK_R) == CMDMASK_MANUBLOCK_R) {
		ret = __fg_write_byte(chip, CMD_DFSTART, 0x01);
		if (ret < 0) {
			pr_err("TYPE MANUBLOCK, write 0x61 fail! command=0x%08X\n", reg);
			goto fg_read_block_end;
		}
		msleep(CMD_SBS_DELAY);

		ret = __fg_write_byte(chip, CMD_DFPAGE, (u8)reg);
		if (ret < 0) {
			pr_err("TYPE MANUBLOCK, write 0x3F fail! command=0x%08X\n", reg);
			goto fg_read_block_end;
		}
		msleep(15);

		ret = __fg_read_buffer(chip, (u8)(CMD_BLOCK + startIndex), length, &val[startIndex]);
		if (ret < 0) {
			pr_err("TYPE MANUBLOCK, read 0x40 fail! command=0x%08X\n", reg);
			goto fg_read_block_end;
		}
		msleep(CMD_SBS_DELAY);

		/* check buffer */
		ret = __fg_read_byte(chip, CMD_CHECKSUM, &checksum);
		if (ret < 0)
			goto fg_read_block_end;

		if (startIndex != 0) {
			sum = fg_block_checksum_calculate(&val[startIndex], 32);
			if (sum != (int)((u8)checksum)) {
				ret = -1;
				pr_err("TYPE MANUBLOCK, verify checksum fail! command=0x%08X\n", reg);
				goto fg_read_block_end;
			} else {
				ret = 0;
			}
		} else {
			ret = 0;
		}
	} else {
		ret = __fg_read_buffer(chip, reg, length, val);
	}

fg_read_block_end:
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);

	return ret;
}

static __maybe_unused int fg_write_block(struct chip_bq27541 *chip, u32 reg, u8 length, u8 *val)
{
	int ret;
	int i;
	u8 sum;
	static u8 write_buffer[DF_PAGE_LEN];

	if ((length > DF_PAGE_LEN) || (length == 0))
		return -1;

	memcpy(val, write_buffer, length);
	i = DF_PAGE_LEN - length;
	if (i > 0)
		memset(write_buffer, 0, i);

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);
	if ((reg & CMDMASK_MANUBLOCK_R) == CMDMASK_MANUBLOCK_R) {
		ret = __fg_write_byte(chip, CMD_DFSTART, 0x01);
		if (ret < 0) {
			pr_err("TYPE MANUBLOCK, write 0x61 fail! command=0x%08X\n", reg);
			goto fg_write_block_end;
		}
		msleep(CMD_SBS_DELAY);

		ret = __fg_write_byte(chip, CMD_DFPAGE, (u8)reg);
		if (ret < 0) {
			pr_err("TYPE MANUBLOCK, write 0x3F fail! command=0x%08X\n", reg);
			goto fg_write_block_end;
		}
		msleep(15);

		ret = __fg_write_buffer(chip, CMD_BLOCK, DF_PAGE_LEN, write_buffer);
		if (ret < 0) {
			pr_err("TYPE MANUBLOCK, read 0x40 fail! command=0x%08X\n", reg);
			goto fg_write_block_end;
		}
		msleep(CMD_SBS_DELAY);

		sum = fg_block_checksum_calculate(write_buffer, DF_PAGE_LEN);
		ret = __fg_write_byte(chip, CMD_CHECKSUM, (u8)sum);
		if (ret < 0) {
			pr_err("TYPE MANUBLOCK, write 0x60 fail! command=0x%08X\n", reg);
			goto fg_write_block_end;
		}
		msleep(15);
	} else {
		ret = __fg_write_buffer(chip, (u8)reg, length, val);
	}
fg_write_block_end:
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);

	return ret;
}

static __maybe_unused int fg_read_dataflash(struct chip_bq27541 *chip, s32 address, s32 length, u8 *val)
{
	int ret = -1;

	u8 sum_read;
	u8 buf[DF_PAGE_LEN];
	u8 classID = (u8)(address >> 8);
	s32 sum;
	s32 pageLen = 0;
	s32 valIndex = 0;
	address &= 0xFF;

	if (length <= 0)
		return -1;

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);

	while (length > 0) {
		pageLen = DF_PAGE_LEN - (address % DF_PAGE_LEN);
		if (pageLen > length)
			pageLen = length;
		pr_debug("fg_read_dataflash: pageLen=%u, class=0x%02X, index=0x%02X\n", pageLen, classID, address);

		ret = __fg_write_byte(chip, CMD_DFSTART, 0x00);
		if (ret < 0) {
			pr_err("write 0x61 fail! class=0x%02X, index=0x%02X\n", classID, address);
			goto fg_read_dataflash_end;
		}
		msleep(CMD_SBS_DELAY);

		ret = __fg_write_byte(chip, CMD_DFCLASS, classID);
		if (ret < 0) {
			pr_err("write 0x3E fail! class=0x%02X, index=0x%02X\n", classID, address);
			goto fg_read_dataflash_end;
		}
		msleep(CMD_SBS_DELAY);

		ret = __fg_write_byte(chip, CMD_DFPAGE, (u8)(address / DF_PAGE_LEN));
		if (ret < 0) {
			pr_err("write 0x3F fail! class=0x%02X, index=0x%02X\n", classID, address);
			goto fg_read_dataflash_end;
		}
		msleep(15);

		ret = __fg_read_buffer(chip, CMD_BLOCK, DF_PAGE_LEN, buf);
		if (ret < 0) {
			pr_err("read 0x40 fail! class=0x%02X, index=0x%02X\n", classID, address);
			goto fg_read_dataflash_end;
		}

		sum = fg_block_checksum_calculate(buf, DF_PAGE_LEN);

		ret = __fg_read_byte(chip, CMD_CHECKSUM, &sum_read);
		if ((ret < 0) || (sum != (s32)((u8)sum_read))) {
			ret = -1;
			pr_err("read 0x60 fail! class=0x%02X, index=0x%02X, sum=0x%02X, readsum=0x%02X\n", classID,
			       address, sum, sum_read);
			goto fg_read_dataflash_end;
		}

		memcpy(&val[valIndex], &buf[address % DF_PAGE_LEN], pageLen);

		valIndex += pageLen;
		address += pageLen;
		length -= pageLen;
		ret = 0;
	}

fg_read_dataflash_end:
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	return ret;
}

static __maybe_unused int fg_write_dataflash(struct chip_bq27541 *chip, s32 address, s32 length, u8 *val)
{
	int ret = -1;

	s32 sum;
	u8 buf[DF_PAGE_LEN];
	u8 classID = (u8)(address >> 8);
	s32 pageLen = 0;
	s32 valIndex = 0;
	address &= 0xFF;

	if (length <= 0)
		return -1;

	mutex_lock(&chip->bq28z610_alt_manufacturer_access);

	while (length > 0) {
		pageLen = DF_PAGE_LEN - (address % DF_PAGE_LEN);
		if (pageLen > length)
			pageLen = length;
		pr_debug("fg_write_dataflash: pageLen=%u, class=0x%02X, index=0x%02X\n", pageLen, classID, address);

		ret = __fg_write_byte(chip, CMD_DFSTART, 0x00);
		if (ret < 0) {
			pr_err("write 0x61 fail! class=0x%02X, index=0x%02X\n", classID, address);
			goto fg_write_dataflash_end;
		}
		msleep(CMD_SBS_DELAY);

		ret = __fg_write_byte(chip, CMD_DFCLASS, classID);
		if (ret < 0) {
			pr_err("write 0x3E fail! class=0x%02X, index=0x%02X\n", classID, address);
			goto fg_write_dataflash_end;
		}
		msleep(CMD_SBS_DELAY);

		ret = __fg_write_byte(chip, CMD_DFPAGE, (u8)(address / DF_PAGE_LEN));
		if (ret < 0) {
			pr_err("write 0x3F fail! class=0x%02X, index=0x%02X\n", classID, address);
			goto fg_write_dataflash_end;
		}
		msleep(15);

		ret = __fg_read_buffer(chip, CMD_BLOCK, DF_PAGE_LEN, buf);
		if (ret < 0) {
			pr_err("read 0x40 fail! class=0x%02X, index=0x%02X\n", classID, address);
			goto fg_write_dataflash_end;
		}

		memcpy(&buf[address % DF_PAGE_LEN], &val[valIndex], pageLen);

		ret = __fg_write_buffer(chip, CMD_BLOCK, DF_PAGE_LEN, buf);
		if (ret < 0) {
			pr_err("write 0x40 fail! class=0x%02X, index=0x%02X\n", classID, address);
			goto fg_write_dataflash_end;
		}

		sum = fg_block_checksum_calculate(buf, DF_PAGE_LEN);

		ret = __fg_write_byte(chip, CMD_CHECKSUM, (u8)sum);
		if (ret < 0) {
			pr_err("write 0x60 fail! class=0x%02X, index=0x%02X\n", classID, address);
			goto fg_write_dataflash_end;
		}
		msleep(100);

		valIndex += pageLen;
		address += pageLen;
		length -= pageLen;
		ret = 0;
	}

fg_write_dataflash_end:
	mutex_unlock(&chip->bq28z610_alt_manufacturer_access);
	return ret;
}

static s32 print_buffer(char *str, s32 strlen, u8 *buf, s32 buflen)
{
#define PRINT_BUFFER_FORMAT_LEN 3
	s32 i, j;

	if ((strlen <= 0) || (buflen <= 0))
		return -1;

	memset(str, 0, strlen * sizeof(char));

	j = min(buflen, strlen / PRINT_BUFFER_FORMAT_LEN);
	for (i = 0; i < j; i++) {
		sprintf(&str[i * PRINT_BUFFER_FORMAT_LEN], "%02X ", buf[i]);
	}

	return i * PRINT_BUFFER_FORMAT_LEN;
}

struct sh_decoder {
	u8 addr;
	u8 reg;
	u8 length;
	u8 buf_first_val;
};

static s32 fg_decode_iic_read(struct chip_bq27541 *chip, struct sh_decoder *decoder, u8 *pBuf)
{
	static struct i2c_msg msg[2];
	u8 addr = IIC_ADDR_OF_2_KERNEL(decoder->addr);
	s32 ret;

	if (!chip || !chip->client || !chip->client->adapter)
		return -ENODEV;

	if (oplus_is_rf_ftm_mode())
		return 0;

	mutex_lock(&chip->chip_mutex);

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].buf = &(decoder->reg);
	msg[0].len = sizeof(u8);
	msg[1].addr = addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = pBuf;
	msg[1].len = decoder->length;
	ret = (s32)i2c_transfer(chip->client->adapter, msg, ARRAY_SIZE(msg));

	mutex_unlock(&chip->chip_mutex);
	return ret;
}

static s32 fg_decode_iic_write(struct chip_bq27541 *chip, struct sh_decoder *decoder)
{
	static struct i2c_msg msg[1];
	static u8 write_buf[WRITE_BUF_MAX_LEN];
	u8 addr = IIC_ADDR_OF_2_KERNEL(decoder->addr);
	u8 length = decoder->length;
	s32 ret;

	if (!chip || !chip->client || !chip->client->adapter)
		return -ENODEV;

	if (oplus_is_rf_ftm_mode())
		return 0;

	if ((length <= 0) || (length > WRITE_BUF_MAX_LEN)) {
		pr_err("i2c write buffer fail: length invalid!\n");
		return -1;
	}

	mutex_lock(&chip->chip_mutex);
	memset(write_buf, 0, WRITE_BUF_MAX_LEN * sizeof(u8));
	write_buf[0] = decoder->reg;
	memcpy(&write_buf[1], &(decoder->buf_first_val), length);

	msg[0].addr = addr;
	msg[0].flags = 0;
	msg[0].buf = write_buf;
	msg[0].len = sizeof(u8) * (length + 1);

	ret = i2c_transfer(chip->client->adapter, msg, ARRAY_SIZE(msg));
	if (ret < 0) {
		pr_err("i2c write buffer fail: can't write reg 0x%02X\n", decoder->reg);
	}

	mutex_unlock(&chip->chip_mutex);
	return (ret < 0) ? ret : 0;
}

static s32 fg_soc_calculate(s32 remCap, s32 fullCap)
{
	s32 soc = 0;
	if ((remCap < 0) || (fullCap <= 0))
		return 0;
	remCap *= 100;
	soc = remCap / fullCap;
	if ((remCap % fullCap) != 0)
		soc++;
	if (soc > 100)
		soc = 100;
	return soc;
}

s32 sh366002_read_gaugeinfo_block(struct chip_bq27541 *chip)
{
	static u8 buf[GAUGEINFO_LEN];
	static char str[GAUGESTR_LEN];
	int i, j = 0;
	int k, current_index;
	int ret;
	s16 temp1, temp2;
	u16 data;
	u64 jiffies_now;
	s64 tick;

	if (chip->device_type != DEVICE_ZY0602)
		return 0;

	if (!chip->dump_sh366002_block && !(sh366002_dbg & DUMP_SH366002_BLOCK))
		return 0;

	if (oplus_is_rf_ftm_mode())
		return 0;

	jiffies_now = get_jiffies_64();
	tick = (s64)(jiffies_now - chip->log_last_update_tick);
	if (tick < 0) {
		tick = (s64)(U64_MAXVALUE - (u64)chip->log_last_update_tick);
		tick += jiffies_now + 1;
	}
	tick /= HZ;
	if (tick < GAUGE_LOG_MIN_TIMESPAN)
		return 0;

	chip->log_last_update_tick = jiffies_now;

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "Elasp=%d, ", tick);

	ret = fg_read_sbs_word(chip, CMD_CNTLSTATUS, &data);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_CNTLSTATUS, ret = %d\n", ret);
		return ret;
	}
	i += sprintf(&str[i], "ControlStatus=0x%04X, ", data);

	ret = fg_read_sbs_word(chip, CMD_TEMPER, &data);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_TEMPER, ret = %d\n", ret);
		return ret;
	}
	data -= TEMPER_OFFSET;
	i += sprintf(&str[i], "Temper=%d, ", (s16)data);

	ret = fg_read_sbs_word(chip, CMD_VOLTAGE, &data);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_VOLTAGE, ret = %d\n", ret);
		return ret;
	}
	i += sprintf(&str[i], "VCell=%d, ", (s16)data);

	ret = fg_read_sbs_word(chip, CMD_CURRENT, &data);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_CURRENT, ret = %d\n", ret);
		return ret;
	}
	i += sprintf(&str[i], "Current=%d, ", (s16)data);

	ret = fg_read_sbs_word(chip, CMD_CYCLECOUNT, &data);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_CYCLECOUNT, ret = %d\n", ret);
		return ret;
	}
	i += sprintf(&str[i], "CycleCount=%u, ", data);

	ret = fg_read_sbs_word(chip, CMD_PASSEDC, &data);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_PASSEDC, ret = %d\n", ret);
		return ret;
	}
	i += sprintf(&str[i], "PassedCharge=%d, ", (s16)data);

	ret = fg_read_sbs_word(chip, CMD_DOD0, &data);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_DOD0, ret = %d\n", ret);
		return ret;
	}
	i += sprintf(&str[i], "DOD0=%d, ", (s16)data);

	ret = fg_read_sbs_word(chip, CMD_PACKCON, &data);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_PACKCON, ret = %d\n", ret);
		return ret;
	}
	i += sprintf(&str[i], "PackConfig=0x%04X, ", data);
	j = max(i, j);
	pr_err("SH366002_GaugeLog: SBS_Info is %s", str);

	/* Gauge Block 1 */
	ret = fg_read_block(chip, CMD_GAUGEBLOCK1, 0, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_GAUGEBLOCK1, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "GaugeStatus=0x%02X, ", buf[0]);
	i += sprintf(&str[i], "QmaxFlag=0x%02X, ", buf[1]);
	i += sprintf(&str[i], "UpdateStatus=0x%02X, ", buf[2]);
	i += sprintf(&str[i], "GaugeUpdateIndex=0x%02X, ", buf[3]);
	i += sprintf(&str[i], "GaugeUpdateStatus=0x%04X, ", BUF2U16_LT(&buf[4]));
	i += sprintf(&str[i], "EODLoad=%d, ", (s16)BUF2U16_LT(&buf[6]));
	i += sprintf(&str[i], "CellRatio=%d, ", BUF2U16_LT(&buf[8]));
	i += sprintf(&str[i], "DODCAL=%d, ", (s16)BUF2U16_LT(&buf[10]));
	i += sprintf(&str[i], "DODEOC=%d, ", (s16)BUF2U16_LT(&buf[12]));
	i += sprintf(&str[i], "DODEOD=%d, ", (s16)BUF2U16_LT(&buf[14]));
	i += sprintf(&str[i], "HighTimer=%u, ", BUF2U16_LT(&buf[16]));
	i += sprintf(&str[i], "LowTimer=%u, ", BUF2U16_LT(&buf[18]));
	i += sprintf(&str[i], "OCVFGTimer=%u, ", BUF2U16_LT(&buf[20]));
	i += sprintf(&str[i], "PrevRC=%u, ", BUF2U16_LT(&buf[22]));
	i += sprintf(&str[i], "CoulombOffset=%u, ", BUF2U16_LT(&buf[24]));
	i += sprintf(&str[i], "T_out=%d, ", (s16)BUF2U16_LT(&buf[26]));
	i += sprintf(&str[i], "EOCdelta_T=%d, ", (s16)BUF2U16_LT(&buf[28]));
	i += sprintf(&str[i], "ThermalT=%d, ", (s16)BUF2U16_LT(&buf[30]));
	j = max(i, j);
	pr_err("SH366002_GaugeLog: CMD_GAUGEINFO is %s", str);

	/* Gauge Block 2 */
	ret = fg_read_block(chip, CMD_GAUGEBLOCK2, 0, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_GAUGEBLOCK2, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "ChargingVoltage=%d, ", (s16)BUF2U16_LT(&buf[0]));
	i += sprintf(&str[i], "ChargingCurrent=%d, ", (s16)BUF2U16_LT(&buf[2]));
	i += sprintf(&str[i], "TaperCurrent=%d, ", (s16)BUF2U16_LT(&buf[4]));
	i += sprintf(&str[i], "VOLatEOC=%d, ", (s16)BUF2U16_LT(&buf[6]));
	i += sprintf(&str[i], "CURatEOC=%d, ", (s16)BUF2U16_LT(&buf[8]));
	i += sprintf(&str[i], "GaugeConfig=0x%04X, ", BUF2U16_LT(&buf[10]));
	i += sprintf(&str[i], "workstate2=0x%04X, ", BUF2U16_LT(&buf[12]));
	i += sprintf(&str[i], "UpdateStatus=0x%02X, ", buf[14]);
	i += sprintf(&str[i], "OCVRaUpdate=0x%02X, ", buf[15]);
	i += sprintf(&str[i], "Qmax1=%d, ", (s16)BUF2U16_LT(&buf[16]));
	i += sprintf(&str[i], "DODQmax=%d, ", (s16)BUF2U16_LT(&buf[18]));
	i += sprintf(&str[i], "QmaxPass=%d, ", (s16)BUF2U16_LT(&buf[20]));
	i += sprintf(&str[i], "PassedEnergy=%d, ", (s16)BUF2U16_LT(&buf[22]));
	i += sprintf(&str[i], "Qstart=%d, ", (s16)BUF2U16_LT(&buf[24]));
	i += sprintf(&str[i], "Estart=%d, ", (s16)BUF2U16_LT(&buf[26]));
	i += sprintf(&str[i], "DODGridIndex=0x%02X, ", buf[28]);
	i += sprintf(&str[i], "RCFilterFlag=0x%02X, ", buf[29]);
	i += sprintf(&str[i], "DeltaCap_Timer=%u, ", BUF2U16_LT(&buf[30]));
	j = max(i, j);
	pr_err("SH366002_GaugeLog: GAUGEBLOCK2 is %s", str);

	/* Gauge Block 3 */
	ret = fg_read_block(chip, CMD_GAUGEBLOCK3, 0, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_GAUGEBLOCK3, ret = %d\n", ret);
		return ret;
	}
	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	temp1 = (s16)BUF2U16_LT(&buf[0]);
	temp2 = (s16)BUF2U16_LT(&buf[2]);
	i += sprintf(&str[i], "RemCap=%d, ", temp1);
	i += sprintf(&str[i], "FullCap=%d, ", temp2);
	temp1 = fg_soc_calculate(temp1, temp2);
	i += sprintf(&str[i], "RSOC=%d, ", temp1);
	temp1 = (s16)BUF2U16_LT(&buf[4]);
	temp2 = (s16)BUF2U16_LT(&buf[6]);
	i += sprintf(&str[i], "FilRC=%d, ", temp1);
	i += sprintf(&str[i], "FilFCC=%d, ", temp2);
	temp1 = fg_soc_calculate(temp1, temp2);
	i += sprintf(&str[i], "FSOC=%d, ", temp1);
	temp1 = (s16)BUF2U16_LT(&buf[8]);
	temp2 = (s16)BUF2U16_LT(&buf[10]);
	i += sprintf(&str[i], "RemEgy=%d, ", temp1);
	i += sprintf(&str[i], "FullEgy=%d, ", temp2);
	temp1 = fg_soc_calculate(temp1, temp2);
	i += sprintf(&str[i], "RSOCW=%d, ", temp1);
	temp1 = (s16)BUF2U16_LT(&buf[12]);
	temp2 = (s16)BUF2U16_LT(&buf[14]);
	i += sprintf(&str[i], "FilRE=%d, ", temp1);
	i += sprintf(&str[i], "FilFCE=%d, ", temp2);
	temp1 = fg_soc_calculate(temp1, temp2);
	i += sprintf(&str[i], "FSOCW=%d, ", temp1);

	i += sprintf(&str[i], "RemCapEQU=%d, ", (s16)BUF2U16_LT(&buf[16]));
	i += sprintf(&str[i], "FullCalEQU=%d, ", (s16)BUF2U16_LT(&buf[18]));
	i += sprintf(&str[i], "IdealFCC=%d, ", (s16)BUF2U16_LT(&buf[20]));
	i += sprintf(&str[i], "IIRCUR=%d, ", (s16)BUF2U16_LT(&buf[22]));
	i += sprintf(&str[i], "RemCapRAW=%d, ", (s16)BUF2U16_LT(&buf[24]));
	i += sprintf(&str[i], "FullCapRAW=%d, ", (s16)BUF2U16_LT(&buf[26]));
	i += sprintf(&str[i], "RemEgyRAW=%d, ", (s16)BUF2U16_LT(&buf[28]));
	i += sprintf(&str[i], "FullEgyRAW=%d, ", (s16)BUF2U16_LT(&buf[30]));
	j = max(i, j);
	pr_err("SH366002_GaugeLog: GAUGEBLOCK3 is %s", str);

	/* Gauge Block 4 */
	ret = fg_read_block(chip, CMD_GAUGEBLOCK4, 0, GAUGEINFO_LEN, buf);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_GAUGEBLOCK4, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "FusionModel=");
	for (ret = 0; ret < 15; ret++)
		i += sprintf(&str[i], "0x%04X ", BUF2U16_LT(&buf[ret * 2]));
	j = max(i, j);
	pr_err("SH366002_GaugeLog: GAUGEBLOCK4 is %s", str);

	/* Gauge Block 5 */
	ret = fg_read_block(chip, CMD_GAUGEBLOCK5, 0, 15, buf);
	if (ret < 0) {
		pr_err("SH366002_GaugeLog: could not read CMD_GAUGEBLOCK5, ret = %d\n", ret);
		return ret;
	}

	memset(str, 0, GAUGESTR_LEN);
	i = 0;
	i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
	i += sprintf(&str[i], "FC_Lock_DSG_Timer=%u, ", BUF2U16_LT(&buf[10]));
	i += sprintf(&str[i], "FC_Lock_Relax_Timer=%u, ", buf[12]);
	i += sprintf(&str[i], "FlashUpdateVoltage=%d, ", (s16)BUF2U16_LT(&buf[13]));
	j = max(i, j);
	pr_err("SH366002_GaugeLog: GAUGEBLOCK5 is %s", str);

	if ((chip->dump_sh366002_block & DUMP_SH366002_CURRENT_BLOCK) || (sh366002_dbg & DUMP_SH366002_CURRENT_BLOCK)) {
		/* Current Block 1 */
		current_index = 0;
		ret = fg_read_block(chip, CMD_CURRENTBLOCK1, 0, 32, buf);
		if (ret < 0) {
			pr_err("SH366002_GaugeLog: could not read CMD_CURRENTBLOCK1, ret = %d\n", ret);
			return ret;
		}

		memset(str, 0, GAUGESTR_LEN);
		i = 0;
		i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
		for (k = 0; k < 16; k++) {
			i += sprintf(&str[i], "Current%d=%d, ", current_index++, (s16)BUF2U16_LT(&buf[k * 2]));
		}
		j = max(i, j);
		pr_err("SH366002_GaugeLog: CURRENTBLOCK1 is %s", str);

		/* Current Block 2 */
		ret = fg_read_block(chip, CMD_CURRENTBLOCK2, 0, 32, buf);
		if (ret < 0) {
			pr_err("SH366002_GaugeLog: could not read CMD_CURRENTBLOCK2, ret = %d\n", ret);
			return ret;
		}

		memset(str, 0, GAUGESTR_LEN);
		i = 0;
		i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
		for (k = 0; k < 16; k++) {
			i += sprintf(&str[i], "Current%d=%d, ", current_index++, (s16)BUF2U16_LT(&buf[k * 2]));
		}
		j = max(i, j);
		pr_err("SH366002_GaugeLog: CURRENTBLOCK2 is %s", str);

		/* Current Block 3 */
		ret = fg_read_block(chip, CMD_CURRENTBLOCK3, 0, 32, buf);
		if (ret < 0) {
			pr_err("SH366002_GaugeLog: could not read CMD_CURRENTBLOCK3, ret = %d\n", ret);
			return ret;
		}

		memset(str, 0, GAUGESTR_LEN);
		i = 0;
		i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
		for (k = 0; k < 16; k++) {
			i += sprintf(&str[i], "Current%d=%d, ", current_index++, (s16)BUF2U16_LT(&buf[k * 2]));
		}
		j = max(i, j);
		pr_err("SH366002_GaugeLog: CURRENTBLOCK3 is %s", str);

		/* Current Block 4 */
		ret = fg_read_block(chip, CMD_CURRENTBLOCK4, 0, 32, buf);
		if (ret < 0) {
			pr_err("SH366002_GaugeLog: could not read CMD_CURRENTBLOCK4, ret = %d\n", ret);
			return ret;
		}

		memset(str, 0, GAUGESTR_LEN);
		i = 0;
		i += sprintf(&str[i], "Tick=%d, ", (u32)jiffies_now);
		for (k = 0; k < 16; k++) {
			i += sprintf(&str[i], "Current%d=%d, ", current_index++, (s16)BUF2U16_LT(&buf[k * 2]));
		}
		j = max(i, j);
		pr_err("SH366002_GaugeLog: CURRENTBLOCK4 is %s", str);
	}
	ret = 0;

	return ret;
}

static s32 fg_gauge_unseal(struct chip_bq27541 *chip)
{
	s32 ret;
	s32 i;
	u16 cntl_status;

	for (i = 0; i < 5; i++) {
		ret = fg_write_sbs_word(chip, CMD_CNTL, (u16)CMD_UNSEALKEY);
		if (ret < 0)
			goto fg_gauge_unseal_End;
		msleep(CMD_SBS_DELAY);

		ret = fg_write_sbs_word(chip, CMD_CNTL, (u16)(CMD_UNSEALKEY >> 16));
		if (ret < 0)
			goto fg_gauge_unseal_End;
		msleep(CMD_SBS_DELAY);

		ret = fg_write_sbs_word(chip, CMD_CNTL, (u16)CMD_FULLKEY);
		if (ret < 0)
			goto fg_gauge_unseal_End;
		msleep(CMD_SBS_DELAY);

		ret = fg_write_sbs_word(chip, CMD_CNTL, (u16)(CMD_FULLKEY >> 16));
		if (ret < 0)
			goto fg_gauge_unseal_End;
		msleep(CMD_SBS_DELAY);

		ret = fg_read_sbs_word(chip, CMD_CNTLSTATUS, &cntl_status);
		if (ret < 0)
			goto fg_gauge_unseal_End;
		msleep(CMD_SBS_DELAY);

		if ((cntl_status & CMD_CNTLSTATUS_SEAL) == 0)
			break;
		msleep(CMD_SBS_DELAY);
	}

	ret = (i < 5) ? 0 : -1;
fg_gauge_unseal_End:
	return ret;
}

static s32 fg_gauge_seal(struct chip_bq27541 *chip)
{
	return fg_write_sbs_word(chip, CMD_CNTL, CMD_SEAL);
}

static s32 Check_Chip_Version(struct chip_bq27541 *chip)
{
	struct device *dev = &chip->client->dev;
	struct device_node *np = dev->of_node;
	s32 ret = CHECK_VERSION_ERR;
	u32 chip_afi, dtsi_afi;
	u32 chip_fw, dtsi_fw;
	u8 chip_manu;
	u8 buffer[DF_PAGE_LEN];
	u16 temp16;

	fg_gauge_unseal(chip);

	/* battery_params node*/
	np = of_find_node_by_name(of_node_get(np), "battery_params");
	if (np == NULL) {
		pr_err("Check_Chip_Version: Cannot find child node \"battery_params\"\n");
		return CHECK_VERSION_ERR;
	}

	if (of_property_read_u32(np, "version_fw", &dtsi_fw) != 0)
		dtsi_fw = 0xFFFFFFFF;

	if (of_property_read_u32(np, "version_afi", &dtsi_afi) != 0)
		dtsi_afi = 0xFFFFFFFF;

	pr_err("Check_Chip_Version: fw_date=0x%08X, afi_date=0x%08X, afi_manu=0x%02X\n", dtsi_fw, dtsi_afi,
	       AFI_MANU_SINOWEALTH);

	if (fg_read_sbs_word(chip, CMD_FWDATE1, &temp16) < 0) {
		pr_err("Check_Chip_Version Error: cannot read CNTL 0xD0!");
		goto Check_Chip_Version_End;
	}
	chip_fw = temp16 * 0x10000;
	msleep(5);

	if (fg_read_sbs_word(chip, CMD_FWDATE2, &temp16) < 0) {
		pr_err("Check_Chip_Version Error: cannot read CNTL 0xE2!");
		goto Check_Chip_Version_End;
	}
	chip_fw += temp16;
	msleep(5);

	if (fg_read_block(chip, CMD_BLOCKA, 0, DF_PAGE_LEN, buffer) < 0) {
		pr_err("Check_Chip_Version Error: cannot read block a!");
		goto Check_Chip_Version_End;
	}

	ret = CHECK_VERSION_OK;
	chip_afi = BUF2U32_BG(&buffer[AFI_DATE_BLOCKA_INDEX]);
	chip_manu = buffer[AFI_MANU_BLOCKA_INDEX];
	if ((dtsi_fw != chip_fw) && (dtsi_fw != 0xFFFFFFFF))
		ret |= CHECK_VERSION_FW;

	if (dtsi_afi != 0xFFFFFFFF) {
		if ((chip_afi != dtsi_afi) || (chip_manu != AFI_MANU_SINOWEALTH))
			ret |= CHECK_VERSION_AFI;
	}

	pr_err("Check_Chip_Version: chip_fw=0x%08X, chip_afi_date=0x%08X, chip_afi_manu=0x%02X, ret=%d\n", chip_fw,
	       chip_afi, chip_manu, ret);

Check_Chip_Version_End:
	fg_gauge_seal(chip);
	return ret;
}

int file_decode_process(struct chip_bq27541 *chip, char *profile_name)
{
	struct device *dev = &chip->client->dev;
	struct device_node *np = dev->of_node;
	u8 *pBuf = NULL;
	u8 *pBuf_Read = NULL;
	char strDebug[FILEDECODE_STRLEN];
	int buflen;
	int wait_ms;
	int i, j;
	int line_length;
	int result = -1;
	int retry;
	struct sh_decoder *ptr_decoder;

	pr_err("file_decode_process: start\n");

	/* battery_params node*/
	np = of_find_node_by_name(of_node_get(np), "battery_params");
	if (np == NULL) {
		pr_err("file_decode_process: Cannot find child node \"battery_params\"");
		return -EINVAL;
	}

	buflen = of_property_count_u8_elems(np, profile_name);
	pr_err("file_decode_process: ele_len=%d, key=%s\n", buflen, profile_name);

	pBuf = (u8 *)devm_kzalloc(dev, buflen, 0);
	pBuf_Read = (u8 *)devm_kzalloc(dev, BUF_MAX_LENGTH, 0);

	if ((pBuf == NULL) || (pBuf_Read == NULL)) {
		result = ERRORTYPE_ALLOC;
		pr_err("file_decode_process: kzalloc error\n");
		goto main_process_error;
	}

	result = of_property_read_u8_array(np, profile_name, pBuf, buflen);
	if (result) {
		pr_err("file_decode_process: read dts fail %s\n", profile_name);
		goto main_process_error;
	}
	print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN, pBuf, 32);
	pr_err("file_decode_process: first data=%s\n", strDebug);

	i = 0;
	j = 0;
	while (i < buflen) {
		/* delay: b0: operate, b1: 2, b2-b3: time, big-endian */
		/* other: b0: operate, b1: TWIADR, b2: reg, b3: data_length, b4...end: item */
		if (pBuf[i + INDEX_TYPE] == OPERATE_WAIT) {
			wait_ms = ((int)pBuf[i + INDEX_WAIT_HIGH] * 256) + pBuf[i + INDEX_WAIT_LOW];

			if (pBuf[i + INDEX_WAIT_LENGTH] == 2) {
				msleep(wait_ms);
				i += LINELEN_WAIT;
			} else {
				print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN, &pBuf[i + INDEX_TYPE], 32);
				pr_err("file_decode_process wait error! index=%d, str=%s\n", i, strDebug);
				result = ERRORTYPE_LINE;
				goto main_process_error;
			}
		} else if (pBuf[i + INDEX_TYPE] == OPERATE_READ) {
			line_length = pBuf[i + INDEX_LENGTH];
			if (line_length <= 0) {
				result = ERRORTYPE_LINE;
				goto main_process_error;
			}

			if (fg_decode_iic_read(chip, (struct sh_decoder *)&pBuf[i + INDEX_ADDR], pBuf_Read) < 0) {
				print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN, &pBuf[i + INDEX_TYPE], 32);
				pr_err("file_decode_process read error! index=%d, str=%s\n", i, strDebug);
				result = ERRORTYPE_COMM;
				goto main_process_error;
			}

			i += LINELEN_READ;
		} else if (pBuf[i + INDEX_TYPE] == OPERATE_COMPARE) {
			line_length = pBuf[i + INDEX_LENGTH];
			if (line_length <= 0) {
				result = ERRORTYPE_LINE;
				goto main_process_error;
			}

			ptr_decoder = (struct sh_decoder *)&pBuf[i + INDEX_ADDR];
			for (retry = 0; retry < COMPARE_RETRY_CNT; retry++) {
				if (fg_decode_iic_read(chip, ptr_decoder, pBuf_Read) < 0) {
					print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN, &pBuf[i + INDEX_TYPE],
						     32);
					pr_err("file_decode_process compare_read error! index=%d, str=%s\n", i,
					       strDebug);
					result = ERRORTYPE_COMM;
					goto file_decode_process_compare_loop_end;
				}

				print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN, pBuf_Read, line_length);
				pr_debug("file_decode_process loop compare: IC read=%s\n", strDebug);

				result = 0;
				for (j = 0; j < line_length; j++) {
					if (pBuf[INDEX_DATA + i + j] != pBuf_Read[j]) {
						result = ERRORTYPE_COMPARE;
						break;
					}
				}

				if (result == 0)
					break;

				/* compare fail */
				print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN, &pBuf[i + INDEX_TYPE], 32);
				pr_err("file_decode_process compare error! index=%d, retry=%d, host=%s\n", i, retry,
				       strDebug);
				print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN, pBuf_Read, 32);
				pr_err("ic=%s\n", strDebug);

			file_decode_process_compare_loop_end:
				msleep(COMPARE_RETRY_WAIT);
			}

			if (retry >= COMPARE_RETRY_CNT) {
				result = ERRORTYPE_COMPARE;
				goto main_process_error;
			}

			i += LINELEN_COMPARE + line_length;
		} else if (pBuf[i + INDEX_TYPE] == OPERATE_WRITE) {
			line_length = pBuf[i + INDEX_LENGTH];
			if (line_length <= 0) {
				result = ERRORTYPE_LINE;
				goto main_process_error;
			}

			ptr_decoder = (struct sh_decoder *)&pBuf[i + INDEX_ADDR];
			if (fg_decode_iic_write(chip, ptr_decoder) != 0) {
				print_buffer(strDebug, sizeof(char) * FILEDECODE_STRLEN, &pBuf[i + INDEX_TYPE], 32);
				pr_err("file_decode_process write error! index=%d, str=%s\n", i, strDebug);
				result = ERRORTYPE_COMM;
				goto main_process_error;
			}

			i += LINELEN_WRITE + line_length;
		} else {
			result = ERRORTYPE_LINE;
			goto main_process_error;
		}
	}
	result = ERRORTYPE_NONE;

main_process_error:
	if (pBuf)
		devm_kfree(dev, pBuf);
	if (pBuf_Read)
		devm_kfree(dev, pBuf_Read);
	pr_err("file_decode_process end: result=%d\n", result);
	return result;
}

s32 sh366002_init(struct chip_bq27541 *chip)
{
	s32 ret;
	s32 version_ret;
	s32 retry;
	struct device_node *np = NULL;

	if (chip->device_type != DEVICE_ZY0602)
		return 0;

	chip->log_last_update_tick = get_jiffies_64();

	ret = of_property_read_u32(chip->dev->of_node, "qcom,dump_sh366002_block", &chip->dump_sh366002_block);
	if (ret)
		chip->dump_sh366002_block = 0;

	if (oplus_is_rf_ftm_mode())
		return 0;

	np = of_find_node_by_name(chip->dev->of_node, "battery_params");
	if (np == NULL) {
		pr_err("Not found battery_params,return\n");
		return 0;
	}

	version_ret = Check_Chip_Version(chip);
	if (version_ret == CHECK_VERSION_ERR) {
		pr_err("Probe: Check version error!\n");
	} else if (version_ret == CHECK_VERSION_OK) {
		pr_err("Probe: Check version ok!\n");
	} else {
		pr_err("Probe: Check version update: %X\n", version_ret);

		ret = ERRORTYPE_NONE;
		if (version_ret & CHECK_VERSION_FW) {
			version_ret = 0;
			for (retry = 0; retry < FILE_DECODE_RETRY; retry++) {
				pr_err("Probe: FW Update start, retry=%d\n", retry);
				if (fg_gauge_unseal(chip) < 0) {
					pr_err("Probe: FW Update unseal fail! retry=%d\n", retry);
					continue;
				}

				ret = file_decode_process(chip, "sinofs_fw_data");
				pr_err("Probe: FW Update end, retry=%d, ret=%d\n", retry, ret);
				if (ret == ERRORTYPE_NONE) {
					version_ret = Check_Chip_Version(chip);
					if ((version_ret & CHECK_VERSION_FW) == 0)
						break;
					ret = ERRORTYPE_FINAL_COMPARE;
					pr_err("Probe: FW Verify fail! retry=%d, ret=0x%04X\n", retry, version_ret);
				}
				msleep(FILE_DECODE_DELAY);
			}
		}

		if (version_ret & CHECK_VERSION_AFI) {
			version_ret = 0;
			for (retry = 0; retry < FILE_DECODE_RETRY; retry++) {
				pr_err("Probe: AFI Update start, retry=%d\n", retry);
				if (fg_gauge_unseal(chip) < 0) {
					pr_err("Probe: AFI Update unseal fail! retry=%d\n", retry);
					continue;
				}

				ret = file_decode_process(chip, "sinofs_afi_data");
				pr_err("Probe: AFI Update end, retry=%d, ret=%d\n", retry, ret);
				if (ret == ERRORTYPE_NONE) {
					version_ret = Check_Chip_Version(chip);
					if ((version_ret & CHECK_VERSION_AFI) == 0)
						break;
					ret = ERRORTYPE_FINAL_COMPARE;
					pr_err("Probe: AFI Verify fail! retry=%d, ret=0x%04X\n", retry, version_ret);
				}
				msleep(FILE_DECODE_DELAY);
			}
		}
		pr_err("Probe: afi update finish! ret=%d\n", ret);
	}
	fg_gauge_seal(chip);

	return 0;
}
