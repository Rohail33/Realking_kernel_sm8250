// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2021 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[OP10]([%s][%d]): " fmt, __func__, __LINE__

#define VOOC_ASIC_OP10

#include <linux/kernel.h>
#include <linux/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/version.h>
#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <soc/oplus/device_info.h>
#else
#include <linux/i2c.h>
#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <soc/oplus/device_info.h>
#endif
#include <linux/firmware.h>
#include <soc/oplus/system/oplus_project.h>
#include <oplus_chg_module.h>
#include <oplus_chg.h>
#include <oplus_chg_ic.h>
#include <oplus_hal_vooc.h>

struct op10_chip {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;
	struct manufacture_info manufacture_info;

	const struct firmware *fw;
	const unsigned char *firmware_data;
	unsigned int fw_data_count;
	int fw_data_version;
	int fw_mcu_version;

	bool upgrading;
	bool boot_by_gpio;
	bool vooc_fw_check;
	bool vooc_fw_update_newmethod;
	char *fw_path;
};

extern int charger_abnormal_log;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
int __attribute__((weak))
request_firmware_select(const struct firmware **firmware_p, const char *name,
			struct device *device)
{
	return 1;
}
int __attribute__((weak))
register_devinfo(char *name, struct manufacture_info *info)
{
	return 1;
}
#else
__attribute__((weak)) int
request_firmware_select(const struct firmware **firmware_p, const char *name,
			struct device *device);
__attribute__((weak)) int register_devinfo(char *name,
					   struct manufacture_info *info);
#endif

static int op10_parse_fw_data_by_bin(struct op10_chip *chip);
static int op10_get_fw_old_version(struct op10_chip *chip, u8 version_info[]);

#ifdef CONFIG_OPLUS_CHARGER_MTK
#define I2C_MASK_FLAG	(0x00ff)
#define I2C_ENEXT_FLAG	(0x0200)
#define I2C_DMA_FLAG	(0xdead2000)
#endif

#define GTP_DMA_MAX_TRANSACTION_LENGTH	255 /* for DMA mode */

#define ERASE_COUNT			959 /*0x0000-0x3BFF*/

#define BYTE_OFFSET			2
#define BYTES_TO_WRITE		16
#define FW_CHECK_FAIL		0
#define FW_CHECK_SUCCESS	1

#define POLYNOMIAL				0x04C11DB7
#define INITIAL_REMAINDER		0xFFFFFFFF
#define FINAL_XOR_VALUE		0xFFFFFFFF

#define WIDTH		(8 * sizeof(u32))
#define TOPBIT		(1U << (WIDTH - 1))
#define REFLECT_DATA(X)			(X)
#define REFLECT_REMAINDER(X)	(X)

#define CMD_SET_ADDR			0x01
#define CMD_XFER_W_DAT		0x02
#define CMD_XFER_R_DATA		0x03
#define CMD_PRG_START			0x05
#define CMD_USER_BOOT			0x06
#define CMD_CHIP_ERASE			0x07
#define CMD_GET_VERSION			0x08
#define CMD_GET_CRC32			0x09
#define CMD_SET_CKSM_LEN		0x0A
#define CMD_DEV_STATUS			0x0B

#define ASIC_TYPE_SY6610		0x02
#define ASIC_TYPE_SY6610C		0x11

#define I2C_RW_LEN_MAX			32
#define ONE_WRITE_LEN_MAX		256
#define FW_VERSION_LEN			11

#define MAX_FW_NAME_LENGTH	60
#define MAX_DEVICE_VERSION_LENGTH 16
#define MAX_DEVICE_MANU_LENGTH    60

struct wakeup_source *op10_update_wake_lock = NULL;

#ifdef CONFIG_OPLUS_CHARGER_MTK
#define GTP_SUPPORT_I2C_DMA		0
#define I2C_MASTER_CLOCK			300

DEFINE_MUTEX(dma_wr_access_op10);

/*static char gpDMABuf_pa[GTP_DMA_MAX_TRANSACTION_LENGTH] = {0};*/

#if GTP_SUPPORT_I2C_DMA
static int i2c_dma_write(struct i2c_client *client, u8 addr, s32 len, u8 *txbuf);
static int i2c_dma_read(struct i2c_client *client, u8 addr, s32 len, u8 *txbuf);
static u8 *gpDMABuf_va = NULL;
static dma_addr_t gpDMABuf_pa = 0;
#endif

#if GTP_SUPPORT_I2C_DMA
static int i2c_dma_read(struct i2c_client *client, u8 addr, s32 len, u8 *rxbuf)
{
	int ret;
	s32 retry = 0;
	u8 buffer[1];

	struct i2c_msg msg[2] = {
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = 0,
			.buf = buffer,
			.len = 1,
			.timing = I2C_MASTER_CLOCK
		},
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
			.flags = I2C_M_RD,
			.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
			.len = len,
			.timing = I2C_MASTER_CLOCK
		},
	};

	mutex_lock(&dma_wr_access_op10);
	/*buffer[0] = (addr >> 8) & 0xFF;*/
	buffer[0] = addr & 0xFF;
	if (rxbuf == NULL) {
		mutex_unlock(&dma_wr_access_op10);
		return -1;
	}
	/*chg_err("vooc dma i2c read: 0x%x, %d bytes(s)\n", addr, len);*/
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0) {
			continue;
		}
		memcpy(rxbuf, gpDMABuf_va, len);
		mutex_unlock(&dma_wr_access_op10);
		return 0;
	}
	/*chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);*/
	mutex_unlock(&dma_wr_access_op10);
	return ret;
}

static int i2c_dma_write(struct i2c_client *client, u8 addr, s32 len, u8 const *txbuf)
{
	int ret = 0;
	s32 retry = 0;
	u8 *wr_buf = gpDMABuf_va;
	struct i2c_msg msg = {
		.addr = (client->addr & I2C_MASK_FLAG),
		.ext_flag = (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
		.flags = 0,
		.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
		.len = 1 + len,
		.timing = I2C_MASTER_CLOCK
	};

	mutex_lock(&dma_wr_access_op10);
	wr_buf[0] = (u8)(addr & 0xFF);
	if (txbuf == NULL) {
		mutex_unlock(&dma_wr_access_op10);
		return -1;
	}
	memcpy(wr_buf + 1, txbuf, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0) {
			continue;
		}
		mutex_unlock(&dma_wr_access_op10);
		return 0;
	}
	/*chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);*/
	mutex_unlock(&dma_wr_access_op10);
	return ret;
}
#endif /*GTP_SUPPORT_I2C_DMA*/
#endif /*CONFIG_OPLUS_CHARGER_MTK*/

static int oplus_vooc_i2c_read(struct i2c_client *client, u8 addr, s32 len, u8 *rxbuf)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
#if GTP_SUPPORT_I2C_DMA
	return i2c_dma_read(client, addr, len, rxbuf);
#else
	return i2c_smbus_read_i2c_block_data(client, addr, len, rxbuf);
#endif
#else
	return i2c_smbus_read_i2c_block_data(client, addr, len, rxbuf);
#endif
}

static int oplus_vooc_i2c_write(struct i2c_client *client, u8 addr, s32 len, u8 const *txbuf)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
#if GTP_SUPPORT_I2C_DMA
	return i2c_dma_write(client, addr, len, txbuf);
#else
	return i2c_smbus_write_i2c_block_data(client, addr, len, txbuf);
#endif
#else
	return i2c_smbus_write_i2c_block_data(client, addr, len, txbuf);
#endif
}

static int check_flash_idle(struct op10_chip *chip, u32 try_count)
{
	u8 rx_buf;
	int rc = 0;

	do {
		rx_buf = 0xff;
		rc = oplus_vooc_i2c_read(chip->client, CMD_DEV_STATUS, 1, &rx_buf);
		if (rc < 0) {
			chg_debug("read CMD_DEV_STATUS error:%0x\n", rx_buf);
			goto i2c_err;
		}
		/* chg_debug("the rx_buf=%0x\n", rx_buf); */
		if ((rx_buf & 0x01) == 0x0) {/* check OP10 flash is idle */
			return 0;
		}
		try_count--;
		msleep(20);
	} while (try_count);

i2c_err:
	return -1;
}

static int check_crc32_available(struct op10_chip *chip, u32 try_count)
{
	u8 rx_buf;
	int rc = 0;

	do {
		rx_buf = 0x0;
		rc = oplus_vooc_i2c_read(chip->client, CMD_DEV_STATUS, 1, &rx_buf);
		if (rc < 0) {
			chg_debug("read CMD_DEV_STATUS error:%0x\n", rx_buf);
			goto i2c_err;
		}
		/* chg_debug("the rx_buf=%0x\n", rx_buf); */
		if ((rx_buf & 0x02) == 0x2) {
			return 0;
		}
		try_count--;
		msleep(20);
	} while (try_count);

i2c_err:
	return -1;
}

static u32 crc32_sram(const u8 *fw_buf, u32 size)
{
	u32 remainder = INITIAL_REMAINDER;
	u32 byte;
	u8 bit;

	/* Perform modulo-2 division, a byte at a time. */
	for (byte = 0; byte < size; ++byte) {
		/* Bring the next byte into the remainder. */
		remainder ^= (REFLECT_DATA(fw_buf[byte]) << (WIDTH - 8));

		/* Perform modulo-2 division, a bit at a time.*/
		for (bit = 8; bit > 0; --bit) {
			/* Try to divide the current data bit. */
			if (remainder & TOPBIT) {
				remainder = (remainder << 1) ^ POLYNOMIAL;
			} else {
				remainder = (remainder << 1);
			}
		}
	}
	/* The final remainder is the CRC result. */
	return (REFLECT_REMAINDER(remainder) ^ FINAL_XOR_VALUE);
}

static int op10_set_clock_active(struct op10_chip *chip)
{
	struct oplus_chg_ic_dev *parent = chip->ic_dev->parent;
	int rc;

	if (parent == NULL) {
		chg_err("parent not found\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(parent, OPLUS_IC_FUNC_VOOC_SET_CLOCK_ACTIVE);
	if (rc < 0) {
		chg_err("set clock active error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int op10_set_clock_sleep(struct op10_chip *chip)
{
	struct oplus_chg_ic_dev *parent = chip->ic_dev->parent;
	int rc;

	if (parent == NULL) {
		chg_err("parent not found\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(parent, OPLUS_IC_FUNC_VOOC_SET_CLOCK_SLEEP);
	if (rc < 0) {
		chg_err("set clock sleep error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int op10_set_reset_active(struct op10_chip *chip)
{
	struct oplus_chg_ic_dev *parent = chip->ic_dev->parent;
	int rc;

	if (parent == NULL) {
		chg_err("parent not found\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(parent, OPLUS_IC_FUNC_VOOC_RESET_ACTIVE);
	if (rc < 0) {
		chg_err("set reset active error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int op10_set_reset_active_force(struct op10_chip *chip)
{
	struct oplus_chg_ic_dev *parent = chip->ic_dev->parent;
	int rc;

	if (parent == NULL) {
		chg_err("parent not found\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(parent, OPLUS_IC_FUNC_VOOC_RESET_ACTIVE_FORCE);
	if (rc < 0) {
		chg_err("set reset active force error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int op10_set_reset_sleep(struct op10_chip *chip)
{
	struct oplus_chg_ic_dev *parent = chip->ic_dev->parent;
	int rc;

	if (parent == NULL) {
		chg_err("parent not found\n");
		return -ENODEV;
	}

	rc = oplus_chg_ic_func(parent, OPLUS_IC_FUNC_VOOC_RESET_SLEEP);
	if (rc < 0) {
		chg_err("set reset sleep error, rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static bool op10_fw_update_check(struct op10_chip *chip, const u8 *fw_buf,
				 u32 size)
{
	int i = 0;
	int ret = 0;
	u8 fw_version[FW_VERSION_LEN] = {0};
	u8 rx_buf[4] = {0};
	u32 check_status_try_count = 100;/* try 2s */
	u32 fw_status_address = 0x4000 - 0x10;
	u32 new_fw_crc32 = 0;

	ret = op10_get_fw_old_version(chip, fw_version);
	if (ret == 1)
		return false;

	/* chip version */
	ret = oplus_vooc_i2c_read(chip->client, CMD_GET_VERSION, 1, rx_buf);
	if (ret < 0) {
		chg_err("read CMD_GET_VERSION error:%d\n", ret);
	} else {
		switch (rx_buf[0]) {
		case 0x01:
		case 0x02:
			chg_debug("chip is sy6610:0x%02x\n", rx_buf[0]);
			break;
		case 0x11:
			chg_debug("chip is sy6610c:0x%02x\n", rx_buf[0]);
			break;
		default:
			chg_debug("invalid chip version:0x%02x\n", rx_buf[0]);
		}
	}

	/*read fw status*/
	rx_buf[0] = fw_status_address & 0xFF;
	rx_buf[1] = (fw_status_address >> 8) & 0xFF;
	oplus_vooc_i2c_write(chip->client, CMD_SET_ADDR, 2, rx_buf);
	msleep(1);
	memset(rx_buf, 0, 4);
	oplus_vooc_i2c_read(chip->client, CMD_XFER_R_DATA, 4, rx_buf);
	chg_debug("fw crc32 status:0x%08x\n", *((u32 *)rx_buf));

	chip->fw_mcu_version = fw_version[FW_VERSION_LEN-4];

	for (i = 0; i < FW_VERSION_LEN; i++) {
		chg_debug("the old version: %0x, the fw version: %0x\n", fw_version[i], fw_buf[size - FW_VERSION_LEN + i]);
		if (fw_version[i] != fw_buf[size - FW_VERSION_LEN + i])
			return false;
	}

	/*noticefy OP10 to update the CRC32 and check it*/
	*((u32 *)rx_buf) = size;
	oplus_vooc_i2c_write(chip->client, CMD_SET_CKSM_LEN, 4, rx_buf);
	msleep(5);
	if (check_crc32_available(chip, check_status_try_count) == -1) {
		chg_debug("crc32 is not available, timeout!\n");
		return false;
	}

	/* check crc32 is correct */
	memset(rx_buf, 0, 4);
	oplus_vooc_i2c_read(chip->client, CMD_GET_CRC32, 4, rx_buf);
	new_fw_crc32 = crc32_sram(fw_buf, size);
	chg_debug("fw_data_crc:0x%0x, the read data_crc32:0x%0x\n", new_fw_crc32, *((u32 *)rx_buf));
	if (*((u32 *)rx_buf) != new_fw_crc32) {
		chg_debug("crc32 compare fail!\n");
		return false;
	}

	/* fw update success,jump to new fw */
	/*oplus_vooc_i2c_read(chip->client, CMD_USER_BOOT, 1, rx_buf);*/

	return true;
}
static int op10_fw_update(struct op10_chip *chip, const u8 *fw_buf, u32 fw_size)
{
	u32 check_status_try_count = 100;/* try 2s */
	u32 write_done_try_count = 500;/* max try 10s */
	u8 rx_buf[4] = {0};
	u32 fw_len = 0, fw_offset = 0;
	u32 write_len = 0, write_len_temp = 0, chunk_index = 0, chunk_len = 0;
	u32 new_fw_crc32 = 0;
	int rc = 0;
	u32 fw_status_address = 0x4000 - 0x10;

	chg_debug("start op_fw_update now, fw length is: %d\n", fw_size);

	/* chip erase */
	rc = oplus_vooc_i2c_read(chip->client, CMD_CHIP_ERASE, 1, rx_buf);
	if (rc < 0) {
		chg_debug("read CMD_CHIP_ERASE error:%d\n", rc);
		goto update_fw_err;
	}
	msleep(100);

	/* check device status */
	if (check_flash_idle(chip, check_status_try_count) == -1) {
		chg_debug("device is always busy, timeout!\n");
		goto update_fw_err;
	}

	/* start write the fw array */
	fw_len = fw_size;
	fw_offset = 0;
	while (fw_len) {
		write_len = (fw_len < ONE_WRITE_LEN_MAX) ? fw_len : ONE_WRITE_LEN_MAX;

		/* set flash start address */
		*((u32 *)rx_buf) = fw_offset;
		oplus_vooc_i2c_write(chip->client, CMD_SET_ADDR, 2, rx_buf);
		msleep(1);

		/* send data which will be written in future */
		chunk_index = 0;
		write_len_temp = write_len;
		while (write_len_temp) {
			chunk_len = (write_len_temp < I2C_RW_LEN_MAX) ? write_len_temp : I2C_RW_LEN_MAX;
			oplus_vooc_i2c_write(chip->client, CMD_XFER_W_DAT, chunk_len, fw_buf + fw_offset + chunk_index * I2C_RW_LEN_MAX);
			msleep(1);

			write_len_temp -= chunk_len;
			chunk_index++;
		}
		oplus_vooc_i2c_read(chip->client, CMD_PRG_START, 1, rx_buf);
		msleep(5);
		if (check_flash_idle(chip, write_done_try_count) == -1) {
			chg_debug("cannot wait flash write done, timeout!\n");
			goto update_fw_err;
		}

		/* chg_debug("current write address: %d,to bw write length:%d\n", fw_offset, write_len); */
		fw_offset += write_len;
		fw_len -= write_len;
	}

	/*noticefy OP10 to update the CRC32 and check it*/
	*((u32 *)rx_buf) = fw_size;
	oplus_vooc_i2c_write(chip->client, CMD_SET_CKSM_LEN, 4, rx_buf);
	msleep(5);
	if (check_crc32_available(chip, check_status_try_count) == -1) {
		chg_debug("crc32 is not available after flash write done, timeout!\n");
		goto update_fw_err;
	}

	/* check crc32 is correct */
	memset(rx_buf, 0, 4);
	oplus_vooc_i2c_read(chip->client, CMD_GET_CRC32, 4, rx_buf);
	new_fw_crc32 = crc32_sram(fw_buf, fw_size);
	if (*((u32 *)rx_buf) != new_fw_crc32) {
		chg_debug("fw_data_crc:0x%0x, the read data_crc32:0x%0x\n", new_fw_crc32, *((u32 *)rx_buf));
		chg_debug("crc32 compare fail!\n");

		/*write FAIL*/
		rx_buf[0] = fw_status_address & 0xFF;
		rx_buf[1] = (fw_status_address >> 8) & 0xFF;
		oplus_vooc_i2c_write(chip->client, CMD_SET_ADDR, 2, rx_buf);
		msleep(1);
		*((u32 *)rx_buf) = 0x4641494C;/*FAIL*/
		oplus_vooc_i2c_write(chip->client, CMD_XFER_W_DAT, 4, rx_buf);
		msleep(1);
		oplus_vooc_i2c_read(chip->client, CMD_PRG_START, 1, rx_buf);
		msleep(10);
		if (check_flash_idle(chip, write_done_try_count) == -1) {
			chg_debug("cannot wait flash write fail done, timeout!\n");
		}
		goto update_fw_err;
	}

	/*write SUCC*/
	rx_buf[0] = fw_status_address & 0xFF;
	rx_buf[1] = (fw_status_address >> 8) & 0xFF;
	oplus_vooc_i2c_write(chip->client, CMD_SET_ADDR, 2, rx_buf);
	msleep(1);
	*((u32 *)rx_buf) = 0x53554343;/*SUCC*/
	oplus_vooc_i2c_write(chip->client, CMD_XFER_W_DAT, 4, rx_buf);
	msleep(1);
	oplus_vooc_i2c_read(chip->client, CMD_PRG_START, 1, rx_buf);
	msleep(10);
	if (check_flash_idle(chip, write_done_try_count) == -1) {
		chg_debug("cannot wait flash write succ done, timeout!\n");
	}

	/* fw update success,jump to new fw */
	oplus_vooc_i2c_read(chip->client, CMD_USER_BOOT, 1, rx_buf);
	chip->fw_mcu_version = chip->fw_data_version;
	chg_debug("success!\n");
	return 0;

update_fw_err:
	charger_abnormal_log = CRITICAL_LOG_VOOC_FW_UPDATE_ERR;
	chg_err("fail\n");
	return 1;
}

static int op10_get_fw_old_version(struct op10_chip *chip, u8 version_info[])
{
	u8 rx_buf[4] = {0};/* i = 0; */
	u32 fw_version_address = 0;
	u32 check_status_try_count = 100;/* try 2s */
	u32 fw_len_address = 0x4000 - 8;

	memset(version_info, 0xFF, FW_VERSION_LEN);/* clear version info at first */

	if (check_flash_idle(chip, check_status_try_count) == -1) {
		chg_debug("cannot get the fw old version because of the device is always busy!\n");
		return 1;
	}

	rx_buf[0] = fw_len_address & 0xFF;
	rx_buf[1] = (fw_len_address >> 8) & 0xFF;
	oplus_vooc_i2c_write(chip->client, CMD_SET_ADDR, 2, rx_buf);
	msleep(1);
	oplus_vooc_i2c_read(chip->client, CMD_XFER_R_DATA, 4, rx_buf);
	if (*((u32 *)rx_buf) < fw_len_address) {
		fw_version_address = *((u32 *)rx_buf) - FW_VERSION_LEN;
		rx_buf[0] = fw_version_address & 0xFF;
		rx_buf[1] = (fw_version_address >> 8) & 0xFF;
		oplus_vooc_i2c_write(chip->client, CMD_SET_ADDR, 2, rx_buf);
		msleep(1);
		oplus_vooc_i2c_read(chip->client, CMD_XFER_R_DATA, FW_VERSION_LEN, version_info);
	} else {
		chg_debug("warning:fw length is invalid\n");
	}


	/* below code is used for debug log,pls comment it after this interface test pass */
	/*chg_debug("the fw old version is:\n");
	for (i = 0; i < FW_VERSION_LEN; i++) {
		chg_debug("0x%x,", version_info[i]);
	}
	chg_debug("\n");*/

	return 0;
}

static int op10_get_fw_verion_from_ic(struct op10_chip *chip)
{
	unsigned char addr_buf[2] = {0x3B, 0xF0};
	unsigned char data_buf[4] = {0};
	int rc = 0;
	int update_result = 0;

	if (oplus_is_power_off_charging() || oplus_is_charger_reboot()) {
		chip->upgrading = true;
		update_result = op10_fw_update(chip, chip->firmware_data,
					       chip->fw_data_count);
		chip->upgrading = false;
		if (update_result) {
			msleep(30);
			op10_set_clock_sleep(chip);
			op10_set_reset_active(chip);
		}
	} else {
		op10_set_clock_active(chip);
		chip->boot_by_gpio = true;
		msleep(10);
		op10_set_reset_active(chip);
		chip->upgrading = true;
		msleep(2500);
		chip->boot_by_gpio = false;
		op10_set_clock_sleep(chip);

		/* first:set address */
		rc = oplus_vooc_i2c_write(chip->client, 0x01, 2, &addr_buf[0]);
		if (rc < 0) {
			chg_err("i2c_write 0x01 error\n");
			return FW_CHECK_FAIL;
		}
		msleep(2);
		oplus_vooc_i2c_read(chip->client, 0x03, 4, data_buf);
		/* strcpy(ver,&data_buf[0]); */
		chg_err("data:%x %x %x %x, fw_ver:%x\n", data_buf[0], data_buf[1], data_buf[2], data_buf[3], data_buf[0]);

		msleep(5);
		chip->upgrading = false;
		op10_set_reset_active(chip);
	}
	return data_buf[0];
}

static int __op10_fw_check_then_recover(struct op10_chip *chip)
{
	int update_result = 0;
	int try_count = 5;
	int ret = 0;

	if (oplus_is_rf_ftm_mode()) {
		op10_set_reset_sleep(chip);
		return 0;
	}
	if (chip->vooc_fw_update_newmethod)
		(void)op10_parse_fw_data_by_bin(chip);

	if (!chip->firmware_data) {
		chg_err("op10_fw_data Null, Return\n");
		return FW_ERROR_DATA_MODE;
	} else {
		chg_debug("begin\n");
	}

	if (oplus_is_power_off_charging() || oplus_is_charger_reboot()) {
		chip->upgrading = false;
		op10_set_clock_sleep(chip);
		op10_set_reset_sleep(chip);
		ret = FW_NO_CHECK_MODE;
	} else {
		op10_set_clock_active(chip);
		chip->boot_by_gpio = true;
		msleep(10);
		op10_set_reset_active_force(chip);
		chip->upgrading = true;
		msleep(2500);
		chip->boot_by_gpio = false;
		op10_set_clock_sleep(chip);
		__pm_stay_awake(op10_update_wake_lock);
		if (!op10_fw_update_check(chip, chip->firmware_data,
					  chip->fw_data_count)) {
			chg_info("firmware update start\n");
			do {
				update_result =
					op10_fw_update(chip,
						       chip->firmware_data,
						       chip->fw_data_count);
				if (!update_result)
					break;
				op10_set_clock_active(chip);
				chip->boot_by_gpio = true;
				msleep(10);
				/* chip->upgrading = false; */
				op10_set_reset_active_force(chip);
				/* chip->upgrading = true; */
				msleep(2500);
				chip->boot_by_gpio = false;
				op10_set_clock_sleep(chip);
			} while ((update_result) && (--try_count > 0));
			chg_info("firmware update end, retry times %d\n", 5 - try_count);
		} else {
			chip->vooc_fw_check = true;
			chg_info("fw check ok\n");
		}
		__pm_relax(op10_update_wake_lock);
		msleep(5);
		chip->upgrading = false;
		op10_set_reset_active(chip);
		ret = FW_CHECK_MODE;
	}
	op10_set_reset_sleep(chip);

	if (chip->vooc_fw_update_newmethod) {
		if (chip->fw)
			release_firmware(chip->fw);
		chip->fw = NULL;
		chip->firmware_data = NULL;
	}

	return ret;
}

static void register_vooc_devinfo(void)
{
	int ret = 0;
	char *version;
	char *manufacture;

	version = "op10";
	manufacture = "SILERGY";

	ret = register_device_proc("vooc", version, manufacture);
	if (ret) {
		chg_err(" fail\n");
	}
}

static ssize_t vooc_fw_check_read(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	struct op10_chip *chip = PDE_DATA(file_inode(filp));
	char page[256] = {0};
	char read_data[32] = {0};
	int len = 0;

	if (chip->vooc_fw_check) {
		read_data[0] = '1';
	} else {
		read_data[0] = '0';
	}
	len = sprintf(page, "%s", read_data);
	if (len > *off) {
		len -= *off;
	} else {
		len = 0;
	}
	if (copy_to_user(buff, page, (len < count ? len : count))) {
		return -EFAULT;
	}
	*off += len < count ? len : count;
	return (len < count ? len : count);
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations vooc_fw_check_proc_fops = {
	.read = vooc_fw_check_read,
	.llseek = noop_llseek,
};
#else
static const struct proc_ops vooc_fw_check_proc_fops = {
	.proc_read = vooc_fw_check_read,
	.proc_lseek = noop_llseek,
};
#endif
static int init_proc_vooc_fw_check(struct op10_chip *chip)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create_data("vooc_fw_check", 0444, NULL, &vooc_fw_check_proc_fops, chip);
	if (!p) {
		chg_err("proc_create vooc_fw_check_proc_fops fail!\n");
	}
	return 0;
}

static bool op10_is_used(struct i2c_client *client)
{
	struct op10_chip *chip = i2c_get_clientdata(client);
	u8 value = 0;
	int rc = 0;

	op10_set_clock_active(chip);
	msleep(10);
	op10_set_reset_active_force(chip);
	msleep(2500);
	op10_set_clock_sleep(chip);

	rc = oplus_vooc_i2c_read(chip->client, 0x08, 1, &value);
	if (rc < 0) {
		chg_err("op10 read register 0x08 fail, rc = %d\n", rc);
		return false;
	} else {
		chg_err("register 0x08: 0x%x\n", value);
		if (value == ASIC_TYPE_SY6610 || value == ASIC_TYPE_SY6610C) {
			msleep(5);
			op10_set_reset_active(chip);
			op10_set_reset_sleep(chip);
			return true;
		}
	}
	msleep(5);
	op10_set_reset_active(chip);
	op10_set_reset_sleep(chip);

	return false;
}

static int op10_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct op10_chip *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!op10_is_used(chip->client)) {
		chg_info("op10 not used\n");
		return -ENODEV;
	}

#ifdef CONFIG_OPLUS_CHARGER_MTK
#if GTP_SUPPORT_I2C_DMA
	chip->dev->coherent_dma_mask = DMA_BIT_MASK(32);
	gpDMABuf_va = (u8 *)dma_alloc_coherent(chip->dev,
					       GTP_DMA_MAX_TRANSACTION_LENGTH,
					       &gpDMABuf_pa, GFP_KERNEL);
	if (!gpDMABuf_va) {
		chg_err("Allocate DMA I2C Buffer failed!\n");
		return -ENOMEM;
	} else {
		chg_debug(" ppp dma_alloc_coherent success\n");
	}
	memset(gpDMABuf_va, 0, GTP_DMA_MAX_TRANSACTION_LENGTH);
#endif
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 9, 0))
	op10_update_wake_lock =
		wakeup_source_register("op10_update_wake_lock");
#else
	op10_update_wake_lock =
		wakeup_source_register(NULL, "op10_update_wake_lock");
#endif

	register_vooc_devinfo();
	init_proc_vooc_fw_check(chip);
	ic_dev->online = true;

	return OPLUS_VOOC_IC_OP10;
}

static int op10_exit(struct oplus_chg_ic_dev *ic_dev)
{
	struct op10_chip *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	if (!ic_dev->online)
		return 0;

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	ic_dev->online = false;
	wakeup_source_remove(op10_update_wake_lock);
#ifdef CONFIG_OPLUS_CHARGER_MTK
#if GTP_SUPPORT_I2C_DMA
	dma_free_coherent(chip->dev, GTP_DMA_MAX_TRANSACTION_LENGTH,
			  gpDMABuf_va, gpDMABuf_pa);
#endif
#endif

	return 0;
}

static int op10_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	return 0;
}

static int op10_fw_upgrade(struct oplus_chg_ic_dev *ic_dev)
{
	struct op10_chip *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (chip->firmware_data == NULL) {
		chg_err("firmware_data is NULL");
		return -ENODATA;
	}

	rc = op10_fw_update(chip, chip->firmware_data, chip->fw_data_count);
	if (rc != 0) {
		chg_err("firmware upgrade error, rc=%d\n", rc);
		return -EINVAL;
	}

	return 0;
}

static int op10_user_fw_upgrade(struct oplus_chg_ic_dev *ic_dev,
				 const u8 *fw_buf, u32 fw_size)
{
	return -ENOTSUPP;
}

static int op10_fw_check_then_recover(struct oplus_chg_ic_dev *ic_dev)
{
	struct op10_chip *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev or fw_buf is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	rc = __op10_fw_check_then_recover(chip);
	if (rc != FW_CHECK_MODE) {
		chg_err("fw_check_then_recover error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int op10_get_fw_version(struct oplus_chg_ic_dev *ic_dev, u32 *version)
{
	struct op10_chip *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev or fw_buf is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*version = op10_get_fw_verion_from_ic(chip);

	return 0;
}

static int op10_get_upgrade_status(struct oplus_chg_ic_dev *ic_dev,
				    bool *upgrading)
{
	struct op10_chip *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev or fw_buf is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*upgrading = chip->upgrading;

	return 0;
}

static int op10_check_fw_status(struct oplus_chg_ic_dev *ic_dev, bool *pass)
{
	*pass = true;

	return 0;
}

static int op10_boot_by_gpio(struct oplus_chg_ic_dev *ic_dev, bool *boot_by_gpio)
{
	struct op10_chip *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev or fw_buf is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*boot_by_gpio = chip->boot_by_gpio;

	return 0;
}

static void *op10_get_func(struct oplus_chg_ic_dev *ic_dev,
			    enum oplus_chg_ic_func func_id)
{
	void *func = NULL;

	if (!ic_dev->online && (func_id != OPLUS_IC_FUNC_INIT) &&
	    (func_id != OPLUS_IC_FUNC_EXIT)) {
		chg_err("%s is offline\n", ic_dev->name);
		return NULL;
	}

	switch (func_id) {
	case OPLUS_IC_FUNC_INIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT, op10_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT, op10_exit);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST,
					       op10_smt_test);
		break;
	case OPLUS_IC_FUNC_VOOC_FW_UPGRADE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_FW_UPGRADE,
					       op10_fw_upgrade);
		break;
	case OPLUS_IC_FUNC_VOOC_USER_FW_UPGRADE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_USER_FW_UPGRADE,
			op10_user_fw_upgrade);
		break;
	case OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER,
			op10_fw_check_then_recover);
		break;
	case OPLUS_IC_FUNC_VOOC_GET_FW_VERSION:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_GET_FW_VERSION,
			op10_get_fw_version);
		break;
	case OPLUS_IC_FUNC_VOOC_UPGRADING:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_UPGRADING,
					       op10_get_upgrade_status);
		break;
	case OPLUS_IC_FUNC_VOOC_CHECK_FW_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_CHECK_FW_STATUS,
					       op10_check_fw_status);
		break;
	case OPLUS_IC_FUNC_VOOC_BOOT_BY_GPIO:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_BOOT_BY_GPIO,
					       op10_boot_by_gpio);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq op10_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
};

static int op10_parse_fw_data_by_dts(struct op10_chip *chip)
{
	struct device_node *node = chip->dev->of_node;
	const char *data;
	int len = 0;

	data = of_get_property(node, "oplus,firmware_data", &len);
	if (!data) {
		chg_err("%s: parse vooc fw failed\n", __func__);
		return -ENOMEM;
	}

	chip->firmware_data = data;
	chip->fw_data_count = len;
	chip->fw_data_version = data[len - 4];
	chg_info("version: 0x%x, count: %d\n", chip->fw_data_version,
		 chip->fw_data_count);

	return 0;
}

#define RETRY_COUNT	5
static int op10_parse_fw_data_by_bin(struct op10_chip *chip)
{
	const struct firmware *fw = NULL;
	int retry = RETRY_COUNT;
	int rc;
	char version[10];

	do {
		rc = request_firmware_select(&fw, chip->fw_path, chip->dev);
		if (!rc)
			break;
	} while ((rc < 0) && (--retry > 0));
	chg_debug("retry times %d, fw_path[%s]\n", RETRY_COUNT - retry,
		  chip->fw_path);

	if (!rc) {
		chip->fw = fw;
		chip->firmware_data = fw->data;
		chip->fw_data_count = fw->size;
		chip->fw_data_version =
			chip->firmware_data[chip->fw_data_count - 4];
		sprintf(version, "%d", chip->fw_data_version);
		sprintf(chip->manufacture_info.version, "%s", version);
	} else {
		chg_err("%s request failed, rc=%d\n", chip->fw_path, rc);
		chip->fw = NULL;
		chip->firmware_data = NULL;
		chip->fw_data_count = 0;
		chip->fw_data_version = 0;
	}
	chg_info("version: 0x%x, count: %d, fw_path:%s\n",
		 chip->fw_data_version, chip->fw_data_count, chip->fw_path);

	return 0;
}

static int init_voocbin_proc(struct op10_chip *chip)
{
	strcpy(chip->manufacture_info.version, "0");
	snprintf(chip->fw_path, MAX_FW_NAME_LENGTH, "fastchg/%d/oplus_vooc_fw_op10.bin",
		 get_project());
	memcpy(chip->manufacture_info.manufacture, chip->fw_path, MAX_FW_NAME_LENGTH);
	register_devinfo("fastchg", &chip->manufacture_info);
	chg_debug(" version:%s, fw_path:%s\n", chip->manufacture_info.version, chip->fw_path);
	return 0;
}

static int op10_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct op10_chip *chip;
	struct device_node *node = client->dev.of_node;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc;

	chip = devm_kzalloc(&client->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		dev_err(&client->dev, "Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);

	chip->vooc_fw_update_newmethod =
		of_property_read_bool(node, "oplus,vooc_fw_update_newmethod");
	if (!chip->vooc_fw_update_newmethod) {
		rc = op10_parse_fw_data_by_dts(chip);
		if (rc < 0)
			goto error;
	}

	rc = of_property_read_u32(node, "oplus,ic_type", &ic_type);
	if (rc < 0) {
		chg_err("can't get ic type, rc=%d\n", rc);
		goto error;
	}
	rc = of_property_read_u32(node, "oplus,ic_index", &ic_index);
	if (rc < 0) {
		chg_err("can't get ic index, rc=%d\n", rc);
		goto error;
	}
	ic_cfg.name = node->name;
	ic_cfg.index = ic_index;
	sprintf(ic_cfg.manu_name, "op10");
	sprintf(ic_cfg.fw_id, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = op10_get_func;
	ic_cfg.virq_data = op10_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(op10_virq_table);
	chip->ic_dev =
		devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
	if (!chip->ic_dev) {
		rc = -ENODEV;
		chg_err("register %s error\n", node->name);
		goto error;
	}
	chg_info("register %s\n", node->name);

	if (chip->vooc_fw_update_newmethod) {
		/*Alloc fw_name/devinfo memory space*/
		chip->fw_path = kzalloc(MAX_FW_NAME_LENGTH, GFP_KERNEL);
		if (chip->fw_path == NULL) {
			rc = -ENOMEM;
			chg_err("panel_data.fw_name kzalloc error\n");
			goto manu_fwpath_alloc_err;
		}
		chip->manufacture_info.version = kzalloc(MAX_DEVICE_VERSION_LENGTH, GFP_KERNEL);
		if (chip->manufacture_info.version == NULL) {
			rc = -ENOMEM;
			chg_err("manufacture_info.version kzalloc error\n");
			goto manu_version_alloc_err;
		}
		chip->manufacture_info.manufacture = kzalloc(MAX_DEVICE_MANU_LENGTH, GFP_KERNEL);
		if (chip->manufacture_info.manufacture == NULL) {
			rc = -ENOMEM;
			chg_err("panel_data.manufacture kzalloc error\n");
			goto manu_info_alloc_err;
		}
		init_voocbin_proc(chip);

		chg_info("op10 probe success\n");
		return 0;

manu_fwpath_alloc_err:
		kfree(chip->fw_path);
manu_info_alloc_err:
		kfree(chip->manufacture_info.manufacture);
manu_version_alloc_err:
		kfree(chip->manufacture_info.version);
	}

	chg_info("op10 probe success\n");
	return 0;

error:
	i2c_set_clientdata(client, NULL);
	devm_kfree(&client->dev, chip);
	return rc;
}

static int op10_driver_remove(struct i2c_client *client)
{
	struct op10_chip *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return 0;

	if (chip->ic_dev->online)
		op10_exit(chip->ic_dev);
	devm_oplus_chg_ic_unregister(&client->dev, chip->ic_dev);
	i2c_set_clientdata(client, NULL);
	devm_kfree(&client->dev, chip);

	return 0;
}

static void op10_shutdown(struct i2c_client *client)
{
}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/
static const struct of_device_id op10_match[] = {
	{ .compatible = "oplus,op10-fastcg"},
	{ .compatible = "oplus,sy6610-fastcg"},
	{},
};

static const struct i2c_device_id op10_id[] = {
	{"op10-fastcg", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, op10_id);

struct i2c_driver op10_i2c_driver = {
	.driver = {
		.name = "op10-fastcg",
		.owner = THIS_MODULE,
		.of_match_table = op10_match,
	},
	.probe = op10_driver_probe,
	.remove	= op10_driver_remove,
	.shutdown = op10_shutdown,
	.id_table = op10_id,
};

int op10_driver_init(void)
{
	int ret = 0;

	chg_debug("init start\n");
	/* init_hw_version(); */

	if (i2c_add_driver(&op10_i2c_driver) != 0) {
		chg_err(" failed to register op10 i2c driver.\n");
	} else {
		chg_debug(" Success to register op10 i2c driver.\n");
	}

	return ret;
}

void op10_driver_exit(void)
{
	i2c_del_driver(&op10_i2c_driver);
}
oplus_chg_module_register(op10_driver);

MODULE_DESCRIPTION("Driver for oplus vooc op10 fast mcu");
MODULE_LICENSE("GPL v2");
