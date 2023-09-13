// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#define pr_fmt(fmt) "[RK826]([%s][%d]): " fmt, __func__, __LINE__

#define VOOC_ASIC_RK826

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

struct rk826_chip {
	struct i2c_client *client;
	struct device *dev;
	struct oplus_chg_ic_dev *ic_dev;
	struct manufacture_info manufacture_info;

	const struct firmware *fw;
	const unsigned char *firmware_data;
	unsigned int fw_data_count;
	int fw_data_version;

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

static int rk826_parse_fw_data_by_bin(struct rk826_chip *chip);

/* #ifdef CONFIG_OPLUS_CHARGER_MTK */
#define I2C_MASK_FLAG (0x00ff)
#define I2C_ENEXT_FLAG (0x0200)
#define I2C_DMA_FLAG (0xdead2000)
/* #endif */

#define GTP_DMA_MAX_TRANSACTION_LENGTH 255 /* for DMA mode */

#define ERASE_COUNT 959 /*0x0000-0x3BFF*/

#define BYTE_OFFSET 2
#define BYTES_TO_WRITE 16
#define FW_CHECK_FAIL 0
#define FW_CHECK_SUCCESS 1

#define PAGE_UNIT 128
#define TRANSFER_LIMIT 72
#define I2C_ADDR 0x14
#define REG_RESET 0x5140
#define REG_SYS0 0x52C0
#define REG_HOST 0x52C8
#define REG_SLAVE 0x52CC
#define REG_STATE 0x52C4
#define REG_MTP_SELECT 0x4308
#define REG_MTP_ADDR 0x4300
#define REG_MTP_DATA 0x4304
#define REG_SRAM_BEGIN 0x2000
#define SYNC_FLAG 0x53594E43
#define NOT_SYNC_FLAG (~SYNC_FLAG)
#define REC_01_FLAG 0x52454301
#define REC_0O_FLAG 0x52454300
#define RESTART_FLAG 0x52455354
#define MTP_SELECT_FLAG 0x000f0001
#define MTP_ADDR_FLAG 0xffff8000
#define SLAVE_IDLE 0x49444C45
#define SLAVE_BUSY 0x42555359
#define SLAVE_ACK 0x41434B00
#define SLAVE_ACK_01 0x41434B01
#define FORCE_UPDATE_FLAG 0xaf1c0b76
#define SW_RESET_FLAG 0X0000fdb9

#define STATE_READY 0x0
#define STATE_SYNC 0x1
#define STATE_REQUEST 0x2
#define STATE_FIRMWARE 0x3
#define STATE_FINISH 0x4

#define MAX_FW_NAME_LENGTH	60
#define MAX_DEVICE_VERSION_LENGTH 16
#define MAX_DEVICE_MANU_LENGTH    60

#define FW_CODE_SIZE_START_ADDR 0X4
typedef struct {
	u32 tag;
	u32 length;
	u32 timeout;
	u32 ram_offset;
	u32 fw_crc;
	u32 header_crc;
} struct_req, *pstruct_req;

struct rk826_bat {
	int uv_bat;
	int current_bat;
	int temp_bat;
	int soc_bat;
	int pre_uv_bat;
	int pre_current_bat;
	int pre_temp_bat;
	int pre_soc_bat;
	int reset_status;
};

static struct rk826_bat the_bat;
struct wakeup_source *rk826_update_wake_lock = NULL;

#ifdef CONFIG_OPLUS_CHARGER_MTK
#define GTP_SUPPORT_I2C_DMA 0
#define I2C_MASTER_CLOCK 300

DEFINE_MUTEX(dma_wr_access_rk826);

static char gpDMABuf_pa[GTP_DMA_MAX_TRANSACTION_LENGTH] = { 0 };

#if GTP_SUPPORT_I2C_DMA
static int i2c_dma_write(struct i2c_client *client, u8 addr, s32 len,
			 u8 *txbuf);
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
		{ .addr = (client->addr & I2C_MASK_FLAG),
		  .flags = 0,
		  .buf = buffer,
		  .len = 1,
		  .timing = I2C_MASTER_CLOCK },
		{ .addr = (client->addr & I2C_MASK_FLAG),
		  .ext_flag =
			  (client->ext_flag | I2C_ENEXT_FLAG | I2C_DMA_FLAG),
		  .flags = I2C_M_RD,
		  .buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
		  .len = len,
		  .timing = I2C_MASTER_CLOCK },
	};

	mutex_lock(&dma_wr_access_rk826);
	/*buffer[0] = (addr >> 8) & 0xFF;*/
	buffer[0] = addr & 0xFF;
	if (rxbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	/*chg_err("vooc dma i2c read: 0x%x, %d bytes(s)\n", addr, len);*/
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0) {
			continue;
		}
		memcpy(rxbuf, gpDMABuf_va, len);
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	/*chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);*/
	mutex_unlock(&dma_wr_access_rk826);
	return ret;
}

static int i2c_dma_write(struct i2c_client *client, u8 addr, s32 len,
			 u8 const *txbuf)
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

	mutex_lock(&dma_wr_access_rk826);
	wr_buf[0] = (u8)(addr & 0xFF);
	if (txbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	memcpy(wr_buf + 1, txbuf, len);
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0) {
			continue;
		}
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	/*chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);*/
	mutex_unlock(&dma_wr_access_rk826);
	return ret;
}
#endif /*GTP_SUPPORT_I2C_DMA*/

static int oplus_i2c_dma_read(struct i2c_client *client, u16 addr, s32 len,
			      u8 *rxbuf)
{
	int ret = 0;
	s32 retry = 0;
	u8 buffer[2] = { 0 };
	struct i2c_msg msg[2] = {
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = 0,
			.buf = buffer,
			.len = 2,
		},
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = I2C_M_RD,
			.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
			.len = len,
		},
	};

	mutex_lock(&dma_wr_access_rk826);
	buffer[0] = (u8)(addr & 0xFF);
	buffer[1] = (u8)((addr >> 8) & 0xFF);
	if (rxbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	/* chg_debug("vooc dma i2c read: 0x%x, %d bytes(s)\n", addr, len); */
	for (retry = 0; retry < 5; ++retry) {
		if (unlikely(retry > 0))
			usleep_range(10000, 10001); /* try again after 10ms */
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0)
			continue;
		memcpy(rxbuf, gpDMABuf_pa, len);
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
	mutex_unlock(&dma_wr_access_rk826);
	return ret;
}

static int oplus_i2c_dma_write(struct i2c_client *client, u16 addr, s32 len,
			       u8 const *txbuf)
{
	int ret = 0;
	s32 retry = 0;
	u8 *wr_buf = gpDMABuf_pa;
	struct i2c_msg msg = {
		.addr = (client->addr & I2C_MASK_FLAG),
		.flags = 0,
		.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
		.len = 2 + len,
	};

	mutex_lock(&dma_wr_access_rk826);
	wr_buf[0] = (u8)(addr & 0xFF);
	wr_buf[1] = (u8)((addr >> 8) & 0xFF);
	if (txbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	memcpy(wr_buf + 2, txbuf, len);
	/* chg_debug("vooc dma i2c write: 0x%x, %d bytes(s)\n", addr, len); */
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0) {
			continue;
		}
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
	mutex_unlock(&dma_wr_access_rk826);
	return ret;
}

#else /*CONFIG_OPLUS_CHARGER_MTK*/

DEFINE_MUTEX(dma_wr_access_rk826);
static char gpDMABuf_pa[GTP_DMA_MAX_TRANSACTION_LENGTH] = { 0 };
static int oplus_i2c_dma_read(struct i2c_client *client, u16 addr, s32 len,
			      u8 *rxbuf)
{
	int ret;
	s32 retry = 0;
	u8 buffer[2] = { 0 };
	struct i2c_msg msg[2] = {
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = 0,
			.buf = buffer,
			.len = 2,
		},
		{
			.addr = (client->addr & I2C_MASK_FLAG),
			.flags = I2C_M_RD,
			.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
			.len = len,
		},
	};
	/* 	chg_debug("kilody in\n"); */

	mutex_lock(&dma_wr_access_rk826);
	buffer[0] = (u8)(addr & 0xFF);
	buffer[1] = (u8)((addr >> 8) & 0xFF);
	if (rxbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	/* chg_debug("vooc dma i2c read: 0x%x, %d bytes(s)\n", addr, len); */
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg[0], 2);
		if (ret < 0) {
			continue;
		}
		memcpy(rxbuf, gpDMABuf_pa, len);
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
	mutex_unlock(&dma_wr_access_rk826);

	/* 	chg_debug("kilody out\n"); */
	return ret;
}

static int oplus_i2c_dma_write(struct i2c_client *client, u16 addr, s32 len,
			       u8 const *txbuf)
{
	int ret = 0;
	s32 retry = 0;
	u8 *wr_buf = gpDMABuf_pa;
	struct i2c_msg msg = {
		.addr = (client->addr & I2C_MASK_FLAG),
		.flags = 0,
		.buf = (__u8 *)gpDMABuf_pa, /*modified by PengNan*/
		.len = 2 + len,
	};
	/* 	chg_debug("kilody in\n"); */

	mutex_lock(&dma_wr_access_rk826);
	wr_buf[0] = (u8)(addr & 0xFF);
	wr_buf[1] = (u8)((addr >> 8) & 0xFF);
	if (txbuf == NULL) {
		mutex_unlock(&dma_wr_access_rk826);
		return -1;
	}
	memcpy(wr_buf + 2, txbuf, len);
	/* chg_debug("vooc dma i2c write: 0x%x, %d bytes(s)\n", addr, len); */
	for (retry = 0; retry < 5; ++retry) {
		ret = i2c_transfer(client->adapter, &msg, 1);
		if (ret < 0) {
			continue;
		}
		mutex_unlock(&dma_wr_access_rk826);
		return 0;
	}
	chg_err(" Error: 0x%04X, %d byte(s), err-code: %d\n", addr, len, ret);
	mutex_unlock(&dma_wr_access_rk826);

	/* 	chg_debug("kilody out\n"); */
	return ret;
}
#endif /*CONFIG_OPLUS_CHARGER_MTK*/

static int oplus_vooc_i2c_read(struct i2c_client *client, u8 addr, s32 len,
			       u8 *rxbuf)
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

static int oplus_vooc_i2c_write(struct i2c_client *client, u8 addr, s32 len,
				u8 const *txbuf)
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

static int rk826_set_clock_active(struct rk826_chip *chip)
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

static int rk826_set_clock_sleep(struct rk826_chip *chip)
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

static int rk826_set_reset_active(struct rk826_chip *chip)
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

static int rk826_set_reset_active_force(struct rk826_chip *chip)
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

static int rk826_set_reset_sleep(struct rk826_chip *chip)
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

static bool rk826_fw_update_check(struct rk826_chip *chip, const u8 *fw_buf,
				  u32 size)
{
	int ret = 0;
	u8 data_buf[4] = { 0 };
	u32 mtp_select_flag = MTP_SELECT_FLAG;
	u32 mtp_addr_flag = MTP_ADDR_FLAG;
	u32 i = 0, n = 0;
	u32 j = 0;
	u16 fw_size_tmp[2] = { 0 };
	u16 fw_size;
	u16 code_size;
	bool fw_type_check_result = 0;

	ret = oplus_i2c_dma_write(chip->client, REG_MTP_SELECT, 4,
				  (u8 *)(&mtp_select_flag));
	if (ret < 0) {
		chg_err("write mtp select reg error\n");
		goto fw_update_check_err;
	}

	for (i = FW_CODE_SIZE_START_ADDR, j = 0;
	     i < FW_CODE_SIZE_START_ADDR + 2; i++, j++) {
		mtp_addr_flag = (MTP_ADDR_FLAG | i);
		ret = oplus_i2c_dma_write(chip->client, REG_MTP_ADDR, 4,
					  (u8 *)(&mtp_addr_flag));
		if (ret < 0) {
			chg_err("write mtp addr error\n");
			goto fw_update_check_err;
		}

		do {
			ret = oplus_i2c_dma_read(chip->client, REG_MTP_SELECT,
						 4, data_buf);
			if (ret < 0) {
				chg_err("read mtp select reg error\n");
				goto fw_update_check_err;
			}
		} while (!(data_buf[1] & 0x01));

		ret = oplus_i2c_dma_read(chip->client, REG_MTP_DATA, 4,
					 data_buf);
		if (ret < 0) {
			chg_err("read mtp data error\n");
			goto fw_update_check_err;
		}
		chg_debug("the read FW size data: %d\n", data_buf[0]);
		fw_size_tmp[j] = data_buf[0];
	}
	fw_size = (fw_size_tmp[1] << 8) | fw_size_tmp[0];
	chg_err("fw_size[%d], fw_size_tmp[1]=[%d],fw_size_tmp[0]=%d\n", fw_size,
		fw_size_tmp[1], fw_size_tmp[0]);
	if ((fw_size % 128))
		code_size = (fw_size / 128 + 1) * 128 + 128 + 64; /* 128 is page ,64 is extended space */
	else
		code_size = (fw_size / 128) * 128 + 128 + 64; /* 128 is page ,64 is extended space */
	for (i = code_size - 11, n = size - 11; i <= code_size - 4; i++, n++) {
		mtp_addr_flag = (MTP_ADDR_FLAG | i);
		ret = oplus_i2c_dma_write(chip->client, REG_MTP_ADDR, 4,
					  (u8 *)(&mtp_addr_flag));
		if (ret < 0) {
			chg_err("write mtp addr error\n");
			goto fw_update_check_err;
		}

		do {
			ret = oplus_i2c_dma_read(chip->client, REG_MTP_SELECT,
						 4, data_buf);
			if (ret < 0) {
				chg_err("read mtp select reg error\n");
				goto fw_update_check_err;
			}
		} while (!(data_buf[1] & 0x01));

		ret = oplus_i2c_dma_read(chip->client, REG_MTP_DATA, 4,
					 data_buf);
		if (ret < 0) {
			chg_err("read mtp data error\n");
			goto fw_update_check_err;
		}
		chg_debug("the read compare data: %d\n", data_buf[0]);
		if (i == code_size - 4) {
			chg_info("fw_mcu_version :0x%x\n", data_buf[0]);
		}
		if (data_buf[0] != fw_buf[n]) {
			/* chg_err("rk826_fw_data check fail\n"); */
			/* goto fw_update_check_err; */
			fw_type_check_result = 1;
		}
	}
	if (fw_type_check_result)
		goto fw_update_check_err;
	return FW_CHECK_SUCCESS;

fw_update_check_err:
	chg_err("rk826_fw_data check fail\n");
	return FW_CHECK_FAIL;
}

u32 js_hash_en(u32 hash, const u8 *buf, u32 len)
{
	u32 i;

	for (i = 0; i < len; i++)
		hash ^= ((hash << 5) + buf[i] + (hash >> 2));
	return hash;
}

u32 js_hash(const u8 *buf, u32 len)
{
	return js_hash_en(0x47C6A7E6, buf, len);
}

int WriteSram(struct rk826_chip *chip, const u8 *buf, u32 size)
{
	u8 offset = 0;
	u16 reg_addr;
	int ret = 0;
	int i = 0;
	int cur_size = 0;
	int try_count = 5;
	u8 readbuf[4] = { 0 };
	u8 tx_buff[4] = { 0 };
	u32 rec_0O_flag = REC_0O_FLAG;
	u32 rec_01_flag = REC_01_FLAG;

	while (size) {
		if (size >= TRANSFER_LIMIT) {
			cur_size = TRANSFER_LIMIT;
		} else
			cur_size = size;
		for (i = 0; i < cur_size / 4; i++) {
			reg_addr = REG_SRAM_BEGIN + i * 4;
			memcpy(tx_buff, buf + offset + i * 4, 4);
			ret = oplus_i2c_dma_write(chip->client, reg_addr, 4,
						  tx_buff);
			if (ret < 0) {
				chg_err("write SRAM fail");
				return -1;
			}
		}
		/* ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, readbuf); */
		/* chg_err("teh REG_STATE1: %d", *(u32*)readbuf); */
		ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4,
					  (u8 *)(&rec_0O_flag));
		/* mdelay(3); */
		/* write rec_00 into host */
		if (ret < 0) {
			chg_err("write rec_00 into host");
			return -1;
		}
		/* read slave */
		do {
			ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4,
						 readbuf);
			chg_err(" the try_count: %d, the REG_STATE: %d",
				try_count, *(u32 *)readbuf);
			msleep(10);
			ret = oplus_i2c_dma_read(chip->client, REG_SLAVE, 4,
						 readbuf);
			if (ret < 0) {
				chg_err("read slave ack fail");
				return -1;
			}
			try_count--;
		} while (*(u32 *)readbuf == SLAVE_BUSY);
		chg_debug("the try_count: %d, the readbuf: %x\n", try_count,
			  *(u32 *)readbuf);
		if ((*(u32 *)readbuf != SLAVE_ACK) &&
		    (*(u32 *)readbuf != SLAVE_ACK_01)) {
			chg_err(" slave ack fail");
			return -1;
		}

		/* write rec_01 into host */
		ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4,
					  (u8 *)(&rec_01_flag));
		/* write rec_00 into host */
		if (ret < 0) {
			chg_err("write rec_00 into host");
			return -1;
		}

		/* msleep(50); */
		offset += cur_size;
		size -= cur_size;
		try_count = 5;
	}
	return 0;
}

int Download_00_code(struct rk826_chip *chip)
{
	u8 transfer_buf[TRANSFER_LIMIT];
	u32 onetime_size = TRANSFER_LIMIT - 8;
	u32 index = 0;
	u32 offset = 0;
	int ret = 0;
	int size = 16384; /* erase 16kb */

	chg_debug("size: %d\n", size);
	chg_err("%s: erase_rk826_00  start\n", __func__);
	do {
		memset(transfer_buf, 0, TRANSFER_LIMIT);

		if (size >= onetime_size) {
			/* memcpy(transfer_buf, buf + offset, onetime_size); */
			size -= onetime_size;
			offset += onetime_size;
		} else {
			/* memcpy(transfer_buf, buf + offset, size); */
			offset += size;
			size = 0;
		}
		*((u32 *)(transfer_buf + onetime_size)) = index;
		*((u32 *)(transfer_buf + onetime_size + 4)) =
			js_hash(transfer_buf, onetime_size + 4);
		ret = WriteSram(chip, transfer_buf, TRANSFER_LIMIT);
		if (ret != 0) {
			return ret;
		}
		chg_debug("index: %d\n", index);
		index++;
	} while (size);
	chg_err("%s: erase_rk826_00 end\n", __func__);
	return 0;
}

int Download_ff_code(struct rk826_chip *chip)
{
	u8 transfer_buf[TRANSFER_LIMIT];
	u32 onetime_size = TRANSFER_LIMIT - 8;
	u32 index = 0;
	u32 offset = 0;
	int ret = 0;
	int size = 16384; /* erase 16kb */
	chg_debug("size: %d\n", size);
	chg_err("%s: erase_rk826_ff start\n", __func__);
	do {
		memset(transfer_buf, 0xff, TRANSFER_LIMIT);

		if (size >= onetime_size) {
			/* memcpy(transfer_buf, buf + offset, onetime_size); */
			size -= onetime_size;
			offset += onetime_size;
		} else {
			/* memcpy(transfer_buf, buf + offset, size); */
			offset += size;
			size = 0;
		}
		*((u32 *)(transfer_buf + onetime_size)) = index;
		*((u32 *)(transfer_buf + onetime_size + 4)) =
			js_hash(transfer_buf, onetime_size + 4);
		ret = WriteSram(chip, transfer_buf, TRANSFER_LIMIT);
		if (ret != 0) {
			return ret;
		}
		chg_debug("index: %d\n", index);
		index++;
	} while (size);
	chg_err("%s: erase_rk826_ff end\n", __func__);
	return 0;
}
static int rk826_fw_write_00_code(struct rk826_chip *chip, const u8 *fw_buf,
				  u8 fw_size)
{
	int ret = 0;
	int try_count = 3;
	struct_req req = { 0 };
	u32 sync_flag = SYNC_FLAG;
	u32 force_update_flag = FORCE_UPDATE_FLAG;
	u32 sw_reset_flag = SW_RESET_FLAG;
	u32 rec_01_flag = REC_01_FLAG;
	u8 read_buf[4] = { 0 };

	oplus_i2c_dma_write(chip->client, REG_SYS0, 4,
			    (u8 *)(&force_update_flag));
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	while (try_count) {
		ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4,
					  (u8 *)(&sync_flag));
		if (ret < 0) {
			chg_err("write sync failed!");
			goto update_fw_err;
		}
		msleep(10);
		/* 2.check ~sync */
		ret = oplus_i2c_dma_read(chip->client, REG_HOST, 4, read_buf);
		printk("the data: %x, %x, %x, %x\n", read_buf[0], read_buf[1],
		       read_buf[2], read_buf[3]);
		printk("the data: %x, %x, %x, %x\n", *(u8 *)(&sync_flag),
		       *((u8 *)(&sync_flag) + 1), *((u8 *)(&sync_flag) + 2),
		       *((u8 *)(&sync_flag) + 3));

		if (ret < 0) {
			chg_err("read sync failed!");
			goto update_fw_err;
		}
		if (*(u32 *)read_buf != NOT_SYNC_FLAG) {
			chg_err("check ~sync failed!");
			try_count--;
			msleep(50);
			continue;
		}
		break;
	}

	if (try_count == 0) {
		chg_err("Failed to sync!");
		goto update_fw_err;
	}

	/* write rec_01 */
	ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4,
				  (u8 *)(&rec_01_flag));
	if (ret < 0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	msleep(10);

	/* read reg_state */
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret < 0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_REQUEST) {
		chg_err("Failed to go into request_state!");
		goto update_fw_err;
	}

	/* send req */
	req.tag = 0x51455220;
	req.ram_offset = 0;
	req.length = 16384; /* for erase */
	req.timeout = 0;
	req.fw_crc = js_hash(fw_buf, fw_size); /* for crc hash */
	req.header_crc = js_hash((const u8 *)&req, sizeof(req) - 4);
	if ((ret = WriteSram(chip, (const u8 *)&req, sizeof(req))) != 0) {
		chg_err("failed to send request!err=%d\n", ret);
		goto update_fw_err;
	}
	msleep(10);

	/* read state firwware */
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	printk("read state firwware: %x\n", *(u32 *)read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FIRMWARE) {
		chg_err("Failed to go into firmware_state");
		goto update_fw_err;
	}

	/* send fw */
	if ((ret = Download_00_code(chip)) != 0) {
		chg_err("failed to send firmware");
		goto update_fw_err;
	}

	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FINISH) {
		chg_err("Failed to go into finish_state");
		goto update_fw_err;
	}
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	chg_debug("success\n");
	return 0;

update_fw_err:
	charger_abnormal_log = CRITICAL_LOG_VOOC_FW_UPDATE_ERR;
	chg_err("fail\n");
	return 1;
}
static int rk826_fw_write_ff_code(struct rk826_chip *chip, const u8 *fw_buf,
				  u8 fw_size)
{
	int ret = 0;
	int try_count = 3;
	struct_req req = { 0 };
	u32 sync_flag = SYNC_FLAG;
	u32 force_update_flag = FORCE_UPDATE_FLAG;
	u32 sw_reset_flag = SW_RESET_FLAG;
	u32 rec_01_flag = REC_01_FLAG;
	u8 read_buf[4] = { 0 };

	oplus_i2c_dma_write(chip->client, REG_SYS0, 4,
			    (u8 *)(&force_update_flag));
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	while (try_count) {
		ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4,
					  (u8 *)(&sync_flag));
		if (ret < 0) {
			chg_err("write sync failed!");
			goto update_fw_err;
		}
		msleep(10);
		/* 2.check ~sync */
		ret = oplus_i2c_dma_read(chip->client, REG_HOST, 4, read_buf);
		printk("the data: %x, %x, %x, %x\n", read_buf[0], read_buf[1],
		       read_buf[2], read_buf[3]);
		printk("the data: %x, %x, %x, %x\n", *(u8 *)(&sync_flag),
		       *((u8 *)(&sync_flag) + 1), *((u8 *)(&sync_flag) + 2),
		       *((u8 *)(&sync_flag) + 3));

		if (ret < 0) {
			chg_err("read sync failed!");
			goto update_fw_err;
		}
		if (*(u32 *)read_buf != NOT_SYNC_FLAG) {
			chg_err("check ~sync failed!");
			try_count--;
			msleep(50);
			continue;
		}
		break;
	}

	if (try_count == 0) {
		chg_err("Failed to sync!");
		goto update_fw_err;
	}

	/* write rec_01 */
	ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4,
				  (u8 *)(&rec_01_flag));
	if (ret < 0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	msleep(10);

	/* read reg_state */
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret < 0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_REQUEST) {
		chg_err("Failed to go into request_state!");
		goto update_fw_err;
	}

	/* send req */
	req.tag = 0x51455220;
	req.ram_offset = 0;
	req.length = 16384; /* for erase */
	req.timeout = 0;
	req.fw_crc = js_hash(fw_buf, fw_size); /* for crc hash */
	req.header_crc = js_hash((const u8 *)&req, sizeof(req) - 4);
	if ((ret = WriteSram(chip, (const u8 *)&req, sizeof(req))) != 0) {
		chg_err("failed to send request!err=%d\n", ret);
		goto update_fw_err;
	}
	msleep(10);

	/* read state firwware */
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	printk("read state firwware: %x\n", *(u32 *)read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FIRMWARE) {
		chg_err("Failed to go into firmware_state");
		goto update_fw_err;
	}

	/* send fw */
	if ((ret = Download_ff_code(chip)) != 0) {
		chg_err("failed to send firmware");
		goto update_fw_err;
	}

	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FINISH) {
		chg_err("Failed to go into finish_state");
		goto update_fw_err;
	}
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	chg_debug("success\n");
	return 0;

update_fw_err:
	charger_abnormal_log = CRITICAL_LOG_VOOC_FW_UPDATE_ERR;
	chg_err("fail\n");
	return 1;
}
int DownloadFirmware(struct rk826_chip *chip, const u8 *buf, u32 size)
{
	u8 transfer_buf[TRANSFER_LIMIT];
	u32 onetime_size = TRANSFER_LIMIT - 8;
	u32 index = 0;
	u32 offset = 0;
	int ret = 0;

	chg_debug("size: %d\n", size);
	do {
		memset(transfer_buf, 0, TRANSFER_LIMIT);
		if (size >= onetime_size) {
			memcpy(transfer_buf, buf + offset, onetime_size);
			size -= onetime_size;
			offset += onetime_size;
		} else {
			memcpy(transfer_buf, buf + offset, size);
			offset += size;
			size = 0;
		}
		*((u32 *)(transfer_buf + onetime_size)) = index;
		*((u32 *)(transfer_buf + onetime_size + 4)) =
			js_hash(transfer_buf, onetime_size + 4);
		ret = WriteSram(chip, transfer_buf, TRANSFER_LIMIT);
		if (ret != 0) {
			return ret;
		}
		chg_debug("index: %d\n", index);
		index++;
	} while (size);
	return 0;
}

static int rk826_fw_update(struct rk826_chip *chip, const u8 *fw_buf,
			   u32 fw_size)
{
	int ret = 0;
	int try_count = 3;
	struct_req req = { 0 };
	u32 sync_flag = SYNC_FLAG;
	u32 force_update_flag = FORCE_UPDATE_FLAG;
	u32 force_dis_update_flag = 0x00000000;
	u32 sw_reset_flag = SW_RESET_FLAG;
	u32 rec_01_flag = REC_01_FLAG;
	u8 read_buf[4] = { 0 };

	oplus_i2c_dma_write(chip->client, REG_SYS0, 4,
			    (u8 *)(&force_update_flag));
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	while (try_count) {
		ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4,
					  (u8 *)(&sync_flag));
		if (ret < 0) {
			chg_err("write sync failed!");
			goto update_fw_err;
		}
		msleep(10);
		/* 2.check ~sync */
		ret = oplus_i2c_dma_read(chip->client, REG_HOST, 4, read_buf);
		printk("the data: %x, %x, %x, %x\n", read_buf[0], read_buf[1],
		       read_buf[2], read_buf[3]);
		printk("the data: %x, %x, %x, %x\n", *(u8 *)(&sync_flag),
		       *((u8 *)(&sync_flag) + 1), *((u8 *)(&sync_flag) + 2),
		       *((u8 *)(&sync_flag) + 3));

		if (ret < 0) {
			chg_err("read sync failed!");
			goto update_fw_err;
		}
		if (*(u32 *)read_buf != NOT_SYNC_FLAG) {
			chg_err("check ~sync failed!");
			try_count--;
			msleep(50);
			continue;
		}
		break;
	}

	if (try_count == 0) {
		chg_err("Failed to sync!");
		goto update_fw_err;
	}

	/* write rec_01 */
	ret = oplus_i2c_dma_write(chip->client, REG_HOST, 4,
				  (u8 *)(&rec_01_flag));
	if (ret < 0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	msleep(10);

	/* read reg_state */
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret < 0) {
		chg_err("write rec_01 flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_REQUEST) {
		chg_err("Failed to go into request_state!");
		goto update_fw_err;
	}

	/* send req */
	req.tag = 0x51455220;
	req.ram_offset = 0;
	req.length = fw_size;
	req.timeout = 0;
	req.fw_crc = js_hash(fw_buf, req.length);
	req.header_crc = js_hash((const u8 *)&req, sizeof(req) - 4);
	if ((ret = WriteSram(chip, (const u8 *)&req, sizeof(req))) != 0) {
		chg_err("failed to send request!err=%d\n", ret);
		goto update_fw_err;
	}
	msleep(10);

	/* read state firwware */
	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	printk("read state firwware: %x\n", *(u32 *)read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FIRMWARE) {
		chg_err("Failed to go into firmware_state");
		goto update_fw_err;
	}

	/* send fw */
	if ((ret = DownloadFirmware(chip, fw_buf, fw_size)) != 0) {
		chg_err("failed to send firmware");
		goto update_fw_err;
	}

	ret = oplus_i2c_dma_read(chip->client, REG_STATE, 4, read_buf);
	if (ret < 0) {
		chg_err("write REG_STATE flag failed!");
		goto update_fw_err;
	}
	if (*(u32 *)read_buf != STATE_FINISH) {
		chg_err("Failed to go into finish_state");
		goto update_fw_err;
	}
	msleep(10);
	oplus_i2c_dma_write(chip->client, REG_SYS0, 4,
			    (u8 *)(&force_dis_update_flag));
	msleep(2);
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	sprintf(chip->ic_dev->fw_id, "0x%x", fw_buf[fw_size - 4]);
	chg_debug("success\n");
	return 0;

update_fw_err:
	charger_abnormal_log = CRITICAL_LOG_VOOC_FW_UPDATE_ERR;
	chg_err("fail\n");
	return 1;
}

static int rk826_get_fw_verion_from_ic(struct rk826_chip *chip)
{
	unsigned char addr_buf[2] = { 0x3B, 0xF0 };
	unsigned char data_buf[4] = { 0 };
	int rc = 0;
	int update_result = 0;

	if (oplus_is_power_off_charging() || oplus_is_charger_reboot()) {
		chip->upgrading = true;
		update_result = rk826_fw_update(chip, chip->firmware_data,
						chip->fw_data_count);
		chip->upgrading = false;
		if (update_result) {
			msleep(30);
			rk826_set_clock_sleep(chip);
			rk826_set_reset_active(chip);
		}
	} else {
		rk826_set_clock_active(chip);
		chip->boot_by_gpio = true;
		msleep(10);
		rk826_set_reset_active(chip);
		chip->upgrading = true;
		msleep(2500);
		chip->boot_by_gpio = false;
		rk826_set_clock_sleep(chip);

		/* first:set address */
		rc = oplus_vooc_i2c_write(chip->client, 0x01, 2, &addr_buf[0]);
		if (rc < 0) {
			chg_err(" i2c_write 0x01 error\n");
			return FW_CHECK_FAIL;
		}
		msleep(2);
		oplus_vooc_i2c_read(chip->client, 0x03, 4, data_buf);
		/* strcpy(ver,&data_buf[0]); */
		chg_err("data:%x %x %x %x, fw_ver:%x\n", data_buf[0],
			data_buf[1], data_buf[2], data_buf[3], data_buf[0]);

		chip->upgrading = false;
		msleep(5);
		rk826_set_reset_active(chip);
	}
	return data_buf[0];
}

static int __rk826_fw_check_then_recover(struct rk826_chip *chip)
{
	int update_result = 0;
	int try_count = 5;
	int ret = 0;
	int rc = 0;
	u32 force_dis_update_flag = 0x00000000;
	u32 sw_reset_flag = SW_RESET_FLAG;
	u8 value_buf[2] = { 0 };
	int fw_check_err = 0;

	if (oplus_is_rf_ftm_mode()) {
		rk826_set_reset_sleep(chip);
		return 0;
	}
	if (chip->vooc_fw_update_newmethod)
		(void)rk826_parse_fw_data_by_bin(chip);
	if (!chip->firmware_data) {
		chg_err("rk826_fw_data Null, Return\n");
		return FW_ERROR_DATA_MODE;
	} else {
		chg_info("begin\n");
	}

	if (oplus_is_power_off_charging() == true ||
	    oplus_is_charger_reboot() == true) {
		chip->upgrading = true;
		rk826_set_reset_active_force(chip);
		msleep(5);
		update_result = rk826_fw_update(chip, chip->firmware_data,
						chip->fw_data_count);
		chip->upgrading = false;
		if (update_result) {
			msleep(30);
			rk826_set_clock_sleep(chip);
			rk826_set_reset_active_force(chip);
		}
		ret = FW_NO_CHECK_MODE;
	} else {
	update_asic_fw:
		rk826_set_clock_active(chip);
		chip->boot_by_gpio = true;
		msleep(10);
		rk826_set_reset_active_force(chip);
		chip->upgrading = true;
		msleep(2500);
		chip->boot_by_gpio = false;
		rk826_set_clock_sleep(chip);
		__pm_stay_awake(rk826_update_wake_lock);
		if (rk826_fw_update_check(chip, chip->firmware_data,
					  chip->fw_data_count) ==
			    FW_CHECK_FAIL ||
		    fw_check_err) {
			chg_info("firmware update start\n");
			do {
				update_result = rk826_fw_write_00_code(
					chip, chip->firmware_data,
					chip->fw_data_count);
				update_result = rk826_fw_write_ff_code(
					chip, chip->firmware_data,
					chip->fw_data_count);
				update_result = rk826_fw_write_00_code(
					chip, chip->firmware_data,
					chip->fw_data_count);
				update_result =
					rk826_fw_update(chip,
							chip->firmware_data,
							chip->fw_data_count);
				update_result =
					rk826_fw_update(chip,
							chip->firmware_data,
							chip->fw_data_count);
				if (!update_result)
					break;
			} while ((update_result) && (--try_count > 0));
			chg_info("firmware update end, retry times %d\n",
				 5 - try_count);
		} else {
			chip->vooc_fw_check = true;
			chg_info("fw check ok\n");
		}
		if (!update_result) {
			msleep(10);
			oplus_i2c_dma_write(chip->client, REG_SYS0, 4,
					    (u8 *)(&force_dis_update_flag));
			msleep(2);
			oplus_i2c_dma_write(chip->client, REG_RESET, 4,
					    (u8 *)(&sw_reset_flag));
			usleep_range(1000000, 1000001);
			rc = oplus_i2c_dma_read(chip->client, 0x52f8, 2,
						value_buf);
			chg_info("rk826 read register 0x52f8 rc = %d\n", rc);
			if (rc < 0) {
				chg_info(
					"rk826 read register 0x52f8 fail, rc = %d\n",
					rc);
				chg_info("rk826 fw upgrade check ok.");
			} else {
				chg_err("read 0x52f8 success 0x%x,0x%x",
					value_buf[0], value_buf[1]);
				fw_check_err++;
				if (fw_check_err > 3)
					goto update_fw_err;
				msleep(1000);
				goto update_asic_fw;
			}
		}
	update_fw_err:
		__pm_relax(rk826_update_wake_lock);
		chip->upgrading = false;
		msleep(5);
		rk826_set_reset_active(chip);
		ret = FW_CHECK_MODE;
	}
	rk826_set_reset_sleep(chip);

	if (chip->vooc_fw_update_newmethod) {
		if (chip->fw)
			release_firmware(chip->fw);
		chip->fw = NULL;
		chip->firmware_data = NULL;
	}

	return ret;
}

static int __rk826_fw_check_then_recover_fix(struct rk826_chip *chip)
{
	int update_result = 0;
	int try_count = 5;
	int ret = 0;
	int rc = 0;
	u8 value_buf[4] = { 0 };
	int fw_check_err = 0;
	u32 force_dis_update_flag = 0x00000000;
	u32 sw_reset_flag = SW_RESET_FLAG;

	if (!oplus_is_rf_ftm_mode() && chip->vooc_fw_update_newmethod)
		(void)rk826_parse_fw_data_by_bin(chip);
	if (!chip->firmware_data) {
		chg_err("rk826_fw_data Null, Return\n");
		return FW_ERROR_DATA_MODE;
	} else {
		chg_debug("begin\n");
	}

	if (oplus_is_power_off_charging() == true ||
	    oplus_is_charger_reboot() == true) {
		chip->upgrading = true;
		rk826_set_reset_active_force(chip);
		msleep(5);
		update_result = rk826_fw_update(chip, chip->firmware_data,
						chip->fw_data_count);
		chip->upgrading = false;
		if (update_result) {
			msleep(30);
			rk826_set_clock_sleep(chip);
			rk826_set_reset_active_force(chip);
		}
		the_bat.reset_status = 0;
		ret = FW_NO_CHECK_MODE;
	} else {
	update_asic_fw:
		rk826_set_clock_active(chip);
		chip->boot_by_gpio = true;
		msleep(10);
		rk826_set_reset_active_force(chip);
		chip->upgrading = true;
		msleep(2500);
		chip->boot_by_gpio = false;
		rk826_set_clock_sleep(chip);
		__pm_stay_awake(rk826_update_wake_lock);
		if (rk826_fw_update_check(chip, chip->firmware_data,
					  chip->fw_data_count) ==
			    FW_CHECK_FAIL ||
		    fw_check_err) {
			chg_debug("firmware update start\n");
			do {
				update_result =
					rk826_fw_update(chip,
							chip->firmware_data,
							chip->fw_data_count);
				if (!update_result)
					break;
			} while ((update_result) && (--try_count > 0));
			chg_debug("firmware update end, retry times %d\n",
				  5 - try_count);
		} else {
			chip->vooc_fw_check = true;
			chg_debug("fw check ok\n");
		}
		if (!update_result) {
			oplus_i2c_dma_write(chip->client, REG_SYS0, 4,
					    (u8 *)(&force_dis_update_flag));
			oplus_i2c_dma_write(chip->client, REG_SLAVE, 4,
					    (u8 *)(&force_dis_update_flag));
			msleep(10);
			oplus_i2c_dma_write(chip->client, REG_RESET, 4,
					    (u8 *)(&sw_reset_flag));
			usleep_range(1000000, 1000000);
			memset(value_buf, 0, ARRAY_SIZE(value_buf));
			rc = oplus_i2c_dma_read(chip->client, 0x52cc, 4,
						value_buf);
			chg_err("rk826 read register 0x52cc rc = %d\n", rc);
			if ((value_buf[0] == 0x45) && (value_buf[1] == 0x4c) &&
			    (value_buf[2] == 0x44) && (value_buf[3] == 0x49)) {
				chg_info(
					"read 0x52cc success 0x%x,0x%x,0x%x,0x%x",
					value_buf[0], value_buf[1],
					value_buf[2], value_buf[3]);
				fw_check_err++;
				if (fw_check_err > 3)
					goto update_fw_err;
				msleep(1000);
				goto update_asic_fw;
			} else {
				chg_err("rk826 read register 0x52cc fail, rc = %d\n",
					rc);
				chg_info("rk826 fw upgrade check ok.");
			}
		}
	update_fw_err:
		__pm_relax(rk826_update_wake_lock);
		chip->upgrading = false;
		msleep(5);
		rk826_set_reset_active(chip);
		ret = FW_CHECK_MODE;
	}
	rk826_set_reset_sleep(chip);
	the_bat.reset_status = 0;
	oplus_chg_ic_virq_trigger(chip->ic_dev, OPLUS_IC_VIRQ_ERR);

	if (chip->vooc_fw_update_newmethod) {
		if (chip->fw)
			release_firmware(chip->fw);
		chip->fw = NULL;
		chip->firmware_data = NULL;
	}

	return ret;
}

static int __rk826_user_fw_upgrade(struct rk826_chip *chip, const u8 *fw_buf,
				   u32 fw_size)
{
	int update_result = 0;
	int try_count = 5;

	rk826_set_clock_active(chip);
	chip->boot_by_gpio = true;
	msleep(10);
	rk826_set_reset_active_force(chip);
	chip->upgrading = true;
	msleep(2500);
	chip->boot_by_gpio = false;
	rk826_set_clock_sleep(chip);
	__pm_stay_awake(rk826_update_wake_lock);
	chg_info("firmware update start\n");
	do {
		rk826_set_clock_active(chip);
		chip->boot_by_gpio = true;
		msleep(10);
		chip->upgrading = false;
		rk826_set_reset_active(chip);
		chip->upgrading = true;
		msleep(2500);
		chip->boot_by_gpio = false;
		rk826_set_clock_sleep(chip);
		update_result = rk826_fw_update(chip, fw_buf, fw_size);
		if (!update_result)
			break;
	} while ((update_result) && (--try_count > 0));
	chg_info("firmware update end, retry times %d\n", 5 - try_count);
	__pm_relax(rk826_update_wake_lock);
	chip->upgrading = false;
	msleep(5);
	rk826_set_reset_active(chip);

	return update_result;
}

int rk826_asic_fw_status(struct rk826_chip *chip)
{
	u32 force_dis_update_flag = 0x00000000;
	u32 sw_reset_flag = SW_RESET_FLAG;
	u8 value_buf[4] = { 0 };
	int rc = 0;

	if (!chip)
		return -EINVAL;
	oplus_i2c_dma_write(chip->client, REG_SYS0, 4,
			    (u8 *)(&force_dis_update_flag));
	oplus_i2c_dma_write(chip->client, REG_SLAVE, 4,
			    (u8 *)(&force_dis_update_flag));
	msleep(10);
	oplus_i2c_dma_write(chip->client, REG_RESET, 4, (u8 *)(&sw_reset_flag));
	usleep_range(1000000, 1000000);
	rc = oplus_i2c_dma_read(chip->client, 0x52cc, 4, value_buf);
	chg_info("rk826 read register 0x52cc rc = %d\n", rc);
	if ((value_buf[0] == 0x45) && (value_buf[1] == 0x4c) &&
	    (value_buf[2] == 0x44) && (value_buf[3] == 0x49)) {
		chg_info("read 0x52cc success 0x%x,0x%x,0x%x,0x%x",
			 value_buf[0], value_buf[1], value_buf[2],
			 value_buf[3]);
		return 0;
	} else {
		chg_err("rk826 read register 0x52cc fail, rc = %d\n", rc);
		return 1;
	}
}

static void register_vooc_devinfo(void)
{
	int ret = 0;
	char *version;
	char *manufacture;

	version = "rk826";
	manufacture = "ROCKCHIP";

	ret = register_device_proc("vooc", version, manufacture);
	if (ret) {
		chg_err(" fail\n");
	}
}

static void rk826_shutdown(struct i2c_client *client)
{
}

static ssize_t vooc_fw_check_read(struct file *filp, char __user *buff,
				  size_t count, loff_t *off)
{
	struct rk826_chip *chip = PDE_DATA(file_inode(filp));
	char page[256] = { 0 };
	char read_data[32] = { 0 };
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
	.owner = THIS_MODULE,
};
#else
static const struct proc_ops vooc_fw_check_proc_fops = {
	.proc_read = vooc_fw_check_read,
	.proc_lseek = noop_llseek,
};
#endif

static int init_proc_vooc_fw_check(struct rk826_chip *chip)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create_data("vooc_fw_check", 0444, NULL,
			     &vooc_fw_check_proc_fops, chip);
	if (!p) {
		chg_err("proc_create vooc_fw_check_proc_fops fail!\n");
	}
	return 0;
}

static bool rk826_is_used(struct i2c_client *client)
{
	struct rk826_chip *chip = i2c_get_clientdata(client);
	u8 value_buf[2] = { 0 };
	u32 value = 0;
	int rc = 0;

	rk826_set_clock_active(chip);
	msleep(10);
	rk826_set_reset_active_force(chip);
	msleep(2500);
	rk826_set_clock_sleep(chip);

	rc = oplus_i2c_dma_read(chip->client, 0x52f8, 2, value_buf);
	if (rc < 0) {
		chg_err("rk826 read register 0x52f8 fail, rc = %d\n", rc);
		return false;
	} else {
		chg_err("register 0x52f8: 0x%x, 0x%x\n", value_buf[0],
			value_buf[1]);
		value = value_buf[0] | (value_buf[1] << 8);
		chg_err("register 0x52f8: 0x%x\n", value);
		if (value == 0x826A) {
			chg_err("rk826 detected, register 0x52f8: 0x%x\n",
				value);
			msleep(5);
			rk826_set_reset_active(chip);
			rk826_set_reset_sleep(chip);
			return true;
		}
	}
	msleep(5);
	rk826_set_reset_active(chip);
	rk826_set_reset_sleep(chip);

	return false;
}

static int rk826_init(struct oplus_chg_ic_dev *ic_dev)
{
	struct rk826_chip *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (!rk826_is_used(chip->client)) {
		chg_info("rk826 not used\n");
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
	rk826_update_wake_lock =
		wakeup_source_register("rk826_update_wake_lock");
#else
	rk826_update_wake_lock =
		wakeup_source_register(NULL, "rk826_update_wake_lock");
#endif

	register_vooc_devinfo();
	init_proc_vooc_fw_check(chip);
	ic_dev->online = true;

	return OPLUS_VOOC_IC_RK826;
}

static int rk826_exit(struct oplus_chg_ic_dev *ic_dev)
{
	__maybe_unused struct rk826_chip *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev is NULL");
		return -ENODEV;
	}
	if (!ic_dev->online)
		return 0;

	chip = oplus_chg_ic_get_drvdata(ic_dev);
	ic_dev->online = false;
	wakeup_source_remove(rk826_update_wake_lock);
#ifdef CONFIG_OPLUS_CHARGER_MTK
#if GTP_SUPPORT_I2C_DMA
	dma_free_coherent(chip->dev, GTP_DMA_MAX_TRANSACTION_LENGTH,
			  gpDMABuf_va, gpDMABuf_pa);
#endif
#endif

	return 0;
}

static int rk826_smt_test(struct oplus_chg_ic_dev *ic_dev, char buf[], int len)
{
	return 0;
}

static int rk826_fw_upgrade(struct oplus_chg_ic_dev *ic_dev)
{
	struct rk826_chip *chip;
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

	rc = rk826_fw_update(chip, chip->firmware_data, chip->fw_data_count);
	if (rc != 0) {
		chg_err("firmware upgrade error, rc=%d\n", rc);
		return -EINVAL;
	}

	return 0;
}

static int rk826_user_fw_upgrade(struct oplus_chg_ic_dev *ic_dev,
				 const u8 *fw_buf, u32 fw_size)
{
	struct rk826_chip *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev or fw_buf is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	if (fw_buf == NULL) {
		chg_err("fw_buf is NULL");
		return -ENODATA;
	}

	rc = __rk826_user_fw_upgrade(chip, fw_buf, fw_size);
	if (rc != 0) {
		chg_err("firmware upgrade error, rc=%d\n", rc);
		return -EINVAL;
	}

	return 0;
}

static int rk826_fw_check_then_recover(struct oplus_chg_ic_dev *ic_dev)
{
	struct rk826_chip *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev or fw_buf is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	rc = __rk826_fw_check_then_recover(chip);
	if (rc != FW_CHECK_MODE) {
		chg_err("fw_check_then_recover error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int rk826_fw_check_then_recover_fix(struct oplus_chg_ic_dev *ic_dev)
{
	struct rk826_chip *chip;
	int rc = 0;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev or fw_buf is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	rc = __rk826_fw_check_then_recover_fix(chip);
	if (rc != FW_CHECK_MODE) {
		chg_err("fw_check_then_recover_fix error, rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int rk826_get_fw_version(struct oplus_chg_ic_dev *ic_dev, u32 *version)
{
	struct rk826_chip *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev or fw_buf is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*version = rk826_get_fw_verion_from_ic(chip);

	return 0;
}

static int rk826_get_upgrade_status(struct oplus_chg_ic_dev *ic_dev,
				    bool *upgrading)
{
	struct rk826_chip *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev or fw_buf is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*upgrading = chip->upgrading;

	return 0;
}

static int rk826_check_fw_status(struct oplus_chg_ic_dev *ic_dev, bool *pass)
{
	struct rk826_chip *chip;
	int rc;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev or fw_buf is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	rc = rk826_asic_fw_status(chip);
	if (rc < 0) {
		chg_err("get firmware status error, rc=%d\n", rc);
		return rc;
	}

	*pass = !!rc;

	return 0;
}

static int rk826_boot_by_gpio(struct oplus_chg_ic_dev *ic_dev, bool *boot_by_gpio)
{
	struct rk826_chip *chip;

	if (ic_dev == NULL) {
		chg_err("oplus_chg_ic_dev or fw_buf is NULL");
		return -ENODEV;
	}
	chip = oplus_chg_ic_get_drvdata(ic_dev);

	*boot_by_gpio = chip->boot_by_gpio;

	return 0;
}

static void *rk826_get_func(struct oplus_chg_ic_dev *ic_dev,
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
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_INIT, rk826_init);
		break;
	case OPLUS_IC_FUNC_EXIT:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_EXIT, rk826_exit);
		break;
	case OPLUS_IC_FUNC_SMT_TEST:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_SMT_TEST,
					       rk826_smt_test);
		break;
	case OPLUS_IC_FUNC_VOOC_FW_UPGRADE:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_FW_UPGRADE,
					       rk826_fw_upgrade);
		break;
	case OPLUS_IC_FUNC_VOOC_USER_FW_UPGRADE:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_USER_FW_UPGRADE,
			rk826_user_fw_upgrade);
		break;
	case OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER,
			rk826_fw_check_then_recover);
		break;
	case OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER_FIX:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_FW_CHECK_THEN_RECOVER_FIX,
			rk826_fw_check_then_recover_fix);
		break;
	case OPLUS_IC_FUNC_VOOC_GET_FW_VERSION:
		func = OPLUS_CHG_IC_FUNC_CHECK(
			OPLUS_IC_FUNC_VOOC_GET_FW_VERSION,
			rk826_get_fw_version);
		break;
	case OPLUS_IC_FUNC_VOOC_UPGRADING:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_UPGRADING,
					       rk826_get_upgrade_status);
		break;
	case OPLUS_IC_FUNC_VOOC_CHECK_FW_STATUS:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_CHECK_FW_STATUS,
					       rk826_check_fw_status);
		break;
	case OPLUS_IC_FUNC_VOOC_BOOT_BY_GPIO:
		func = OPLUS_CHG_IC_FUNC_CHECK(OPLUS_IC_FUNC_VOOC_BOOT_BY_GPIO,
					       rk826_boot_by_gpio);
		break;
	default:
		chg_err("this func(=%d) is not supported\n", func_id);
		func = NULL;
		break;
	}

	return func;
}

struct oplus_chg_ic_virq rk826_virq_table[] = {
	{ .virq_id = OPLUS_IC_VIRQ_ERR },
};

static int rk826_parse_fw_data_by_dts(struct rk826_chip *chip)
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
static int rk826_parse_fw_data_by_bin(struct rk826_chip *chip)
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

static int init_voocbin_proc(struct rk826_chip *chip)
{
	strcpy(chip->manufacture_info.version, "0");
	snprintf(chip->fw_path, MAX_FW_NAME_LENGTH, "fastchg/%d/oplus_vooc_fw_rk826.bin",
		 get_project());
	memcpy(chip->manufacture_info.manufacture, chip->fw_path, MAX_FW_NAME_LENGTH);
	register_devinfo("fastchg", &chip->manufacture_info);
	chg_debug(" version:%s, fw_path:%s\n", chip->manufacture_info.version, chip->fw_path);
	return 0;
}

static int rk826_driver_probe(struct i2c_client *client,
			      const struct i2c_device_id *id)
{
	struct rk826_chip *chip;
	struct device_node *node = client->dev.of_node;
	struct oplus_chg_ic_cfg ic_cfg = { 0 };
	enum oplus_chg_ic_type ic_type;
	int ic_index;
	int rc;

	chip = devm_kzalloc(&client->dev, sizeof(struct rk826_chip),
			    GFP_KERNEL);
	if (!chip) {
		chg_err("Couldn't allocate memory\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	i2c_set_clientdata(client, chip);

	chip->vooc_fw_update_newmethod =
		of_property_read_bool(node, "oplus,vooc_fw_update_newmethod");
	if (!chip->vooc_fw_update_newmethod) {
		rc = rk826_parse_fw_data_by_dts(chip);
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
	sprintf(ic_cfg.manu_name, "rk826");
	sprintf(ic_cfg.fw_id, "0x00");
	ic_cfg.type = ic_type;
	ic_cfg.get_func = rk826_get_func;
	ic_cfg.virq_data = rk826_virq_table;
	ic_cfg.virq_num = ARRAY_SIZE(rk826_virq_table);
	chip->ic_dev = devm_oplus_chg_ic_register(chip->dev, &ic_cfg);
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

		chg_info("rk826 probe success\n");
		return 0;

manu_fwpath_alloc_err:
		kfree(chip->fw_path);
manu_info_alloc_err:
		kfree(chip->manufacture_info.manufacture);
manu_version_alloc_err:
		kfree(chip->manufacture_info.version);
	}

	chg_info("rk826 probe success\n");
	return 0;

error:
	i2c_set_clientdata(client, NULL);
	devm_kfree(&client->dev, chip);
	return rc;
}

static int rk826_driver_remove(struct i2c_client *client)
{
	struct rk826_chip *chip = i2c_get_clientdata(client);

	if (chip == NULL)
		return 0;

	if (chip->ic_dev->online)
		rk826_exit(chip->ic_dev);
	devm_oplus_chg_ic_unregister(&client->dev, chip->ic_dev);
	i2c_set_clientdata(client, NULL);
	devm_kfree(&client->dev, chip);

	return 0;
}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/
static const struct of_device_id rk826_match[] = {
	{ .compatible = "oplus,rk826-fastcg" },
	{},
};

static const struct i2c_device_id rk826_id[] = {
	{ "rk826-fastcg", 0 },
	{},
};
MODULE_DEVICE_TABLE(i2c, rk826_id);

struct i2c_driver rk826_i2c_driver = {
	.driver = {
		.name = "rk826-fastcg",
		.owner        = THIS_MODULE,
		.of_match_table = rk826_match,
	},
	.probe = rk826_driver_probe,
	.remove	= rk826_driver_remove,
	.shutdown = rk826_shutdown,
	.id_table = rk826_id,
};

int rk826_driver_init(void)
{
	int ret = 0;

	chg_debug("init start\n");
	/* init_hw_version(); */

	if (i2c_add_driver(&rk826_i2c_driver) != 0) {
		chg_err(" failed to register rk826 i2c driver.\n");
	} else {
		chg_debug(" Success to register rk826 i2c driver.\n");
	}

	return ret;
}

void rk826_driver_exit(void)
{
	i2c_del_driver(&rk826_i2c_driver);
}
oplus_chg_module_register(rk826_driver);

MODULE_DESCRIPTION("Driver for oplus vooc rk826 fast mcu");
MODULE_LICENSE("GPL v2");
