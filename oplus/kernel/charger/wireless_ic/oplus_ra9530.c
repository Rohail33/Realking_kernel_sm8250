// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>

#include <linux/debugfs.h>
#include <linux/gpio.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/bitops.h>
#include <linux/mutex.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/rtc.h>
#include <linux/random.h>
#include <linux/notifier.h>
#include <linux/alarmtimer.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/regmap.h>
#include <linux/iio/consumer.h>
#include <uapi/linux/qg.h>
#include <linux/timer.h>
#include <linux/fs.h>

#include <soc/oplus/device_info.h>
#include <soc/oplus/system/oplus_project.h>

#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "../oplus_charger.h"
#include "../oplus_wireless.h"
#include "../oplus_debug_info.h"
#include "oplus_chargepump.h"
#include "oplus_ra9530.h"
#include "oplus_ra9530_fw.h"
#include <soc/oplus/system/boot_mode.h>

#define DEBUG_BY_FILE_OPS
#define RA9530_WAIT_TIME             50             /* sec */
#define POWER_EXPIRED_TIME_DEFAULT   120
#define CHECK_PRIVATE_DELAY          300
#define CHECK_IRQ_DELAY              5
#define DEBUG_BUFF_SIZE              32
#define FULL_SOC                     100
#define UEVENT_MESSAGE_MAX           64

struct oplus_ra9530_ic *ra9530_chip = NULL;

static int g_pen_ornot;
static struct work_struct ra9530_idt_timer_work;
static struct wakeup_source *present_wakelock;
static unsigned long long g_ble_timeout_cnt = 0;
static unsigned long long g_verify_failed_cnt = 0;
static DECLARE_WAIT_QUEUE_HEAD(i2c_waiter);

extern struct oplus_chg_chip *g_oplus_chip;
extern int oplus_get_idt_en_val(void);
extern struct oplus_chg_debug_info oplus_chg_debug_info;

int ra9530_get_idt_int_val(void);
int ra9530_get_vbat_en_val(void);
int ra9530_hall_notifier_callback(struct notifier_block *nb, unsigned long event, void *data);
static void ra9530_power_onoff_switch(int value);

void __attribute__((weak)) notify_pen_state(int state) {return;}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
extern int wireless_register_notify(struct notifier_block *nb);
extern int wireless_unregister_notify(struct notifier_block *nb);
#else
extern struct blocking_notifier_head hall_notifier;
#endif
static struct notifier_block ra9530_notifier ={
	.notifier_call = ra9530_hall_notifier_callback,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
/* only for GKI compile */
unsigned int __attribute__((weak)) get_PCB_Version(void)
{
	return EVT2 + 1;
}

static inline void do_gettimeofday(struct timeval *tv)
{
        struct timespec64 now;

        ktime_get_real_ts64(&now);
        tv->tv_sec = now.tv_sec;
        tv->tv_usec = now.tv_nsec/1000;
}
#endif
static DEFINE_MUTEX(ra9530_i2c_access);

#define RA9530_ADD_COUNT      2
static int __ra9530_read_reg(struct oplus_ra9530_ic *chip, int reg, char *returnData, int count)
{
	/* We have 16-bit i2c addresses - care for endianness */
	char cmd_buf[2]={ reg >> 8, reg & 0xff };
	int ret = 0;
	int i;
	char val_buf[20] = {0};

	for (i = 0; i < count; i++) {
		val_buf[i] = 0;
	}

	ret = i2c_master_send(chip->client, cmd_buf, RA9530_ADD_COUNT);
	if (ret < RA9530_ADD_COUNT) {
		chg_err("%s: i2c read error, reg: %x\n", __func__, reg);
		return ret < 0 ? ret : -EIO;
	}

	ret = i2c_master_recv(chip->client, val_buf, count);
	if (ret < count) {
		chg_err("%s: i2c read error, reg: %x\n", __func__, reg);
		return ret < 0 ? ret : -EIO;
	}

	for (i = 0; i < count; i++) {
		*(returnData + i) = val_buf[i];
	}

	return 0;
}

static int __ra9530_write_reg(struct oplus_ra9530_ic *chip, int reg, int val)
{
	int ret;
	unsigned char data[3] = { reg >> 8, reg & 0xff, val };

	ret = i2c_master_send(chip->client, data, 3);
	if (ret < 3) {
		chg_err("%s: i2c write error, reg: %x\n", __func__, reg);
		return ret < 0 ? ret : -EIO;
	}

	return 0;
}

static int ra9530_write_reg_multi_byte(struct oplus_ra9530_ic *chip, int reg, char *cbuf, int length)
{
	int ret;
	int send_length;
	unsigned char *data_w;

	send_length = length + 2;
	data_w = kzalloc(send_length, GFP_KERNEL);
	if (!data_w) {
		chg_err("can't alloc memory!\n");
		return -1;
	}

	data_w[0] = reg >> 8;
	data_w[1] = reg & 0xff;

	memcpy(data_w + 2, cbuf, length);

	mutex_lock(&ra9530_i2c_access);

	ret = i2c_master_send(chip->client, data_w, send_length);
	if (ret < send_length) {
		chg_err("%s: i2c write error, reg: %x\n", __func__, reg);
		kfree(data_w);
		mutex_unlock(&ra9530_i2c_access);
		return ret < 0 ? ret : -EIO;
	}

	mutex_unlock(&ra9530_i2c_access);

	kfree(data_w);
	return 0;
}

static int ra9530_read_reg(struct oplus_ra9530_ic *chip, int reg, char *returnData, int count)
{
	int ret = 0;

	mutex_lock(&ra9530_i2c_access);
	ret = __ra9530_read_reg(chip, reg, returnData, count);
	mutex_unlock(&ra9530_i2c_access);
	return ret;
}

static int ra9530_config_interface (struct oplus_ra9530_ic *chip, int RegNum, int val, int MASK)
{
	char ra9530_reg = 0;
	int ret = 0;

	mutex_lock(&ra9530_i2c_access);
	ret = __ra9530_read_reg(chip, RegNum, &ra9530_reg, 1);

	ra9530_reg &= ~MASK;
	ra9530_reg |= val;

	ret = __ra9530_write_reg(chip, RegNum, ra9530_reg);

	mutex_unlock(&ra9530_i2c_access);

	return ret;
}

static void ra9530_check_clear_irq(struct oplus_ra9530_ic *chip)
{
	int rc;

	rc = ra9530_read_reg(chip, RA9530_REG_INT_FLAG, chip->int_flag_data, 4);
	if (rc) {
		chg_err("Couldn't read 0x%04x rc = %x\n", RA9530_REG_INT_FLAG, rc);
	} else {
		ra9530_write_reg_multi_byte(chip, RA9530_REG_INT_CLR, chip->int_flag_data, 4);
		ra9530_config_interface(chip, RA9530_REG_RTX_CMD, 0x02, 0xFF);
	}
}

static void check_int_enable(struct oplus_ra9530_ic *chip)
{
	char reg_int[3] = {0, 0, 0};
	int rc;

	chg_err("ra9530 check_int_enable----------\n");

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: RA9530_chip not ready!\n", __func__);
		return;
	}
	rc = ra9530_read_reg(chip, RA9530_REG_INT_EN, reg_int, 3);
	if (rc) {
		chg_err("Couldn't read 0x%04x rc = %x\n", RA9530_REG_INT_EN, rc);
	} else {
		if ((reg_int[0] != 0xDF) || (reg_int[1] != 0x81)
			|| (reg_int[2] != 0x02)) {
			reg_int[0] = 0xDF;
			reg_int[1] = 0x81;
			reg_int[2] = 0x02;
			ra9530_write_reg_multi_byte(chip, RA9530_REG_INT_EN, reg_int, 3);
		}
	}
}

void ra9530_check_point_function(struct work_struct *work)
{
	struct oplus_ra9530_ic *chip = ra9530_chip;
	int cnt = 0;

	chg_err("check point work start \n");
	oplus_chg_debug_info.wirelesspen_info.support = 1;/* wireless pen check point */
	/* count ble addr get timeout */
	do {
		msleep(500);
		cnt++;
		if (cnt > RA9530_GET_BLE_ADDR_TIMEOUT) { /* 5s get ble addr time out*/
			g_ble_timeout_cnt++;
			chg_err("check point update ble addr get timeout n");
			break;
		}
	} while (g_pen_ornot && !chip->ble_mac_addr);

	chg_err("check point work ble timeout:%lld  verify failed:%lld\n", g_ble_timeout_cnt, g_verify_failed_cnt);
	oplus_chg_debug_info.wirelesspen_info.ble_timeout_cnt = g_ble_timeout_cnt;
	oplus_chg_debug_info.wirelesspen_info.ble_timeout_cnt = g_verify_failed_cnt;

	chg_err("check point work end \n");
}

static void ra9530_set_tx_mode(int value)
{
	struct oplus_ra9530_ic *chip = ra9530_chip;
	char reg_tx;
	int rc;
	unsigned char wbuff[2] = {0xA0, 0x0F};

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: ra9530_chip not ready!\n", __func__);
		return;
	}

	rc = ra9530_read_reg(chip, RA9530_REG_RTX_STATUS, &reg_tx, 1);
	if (rc) {
		chg_err("Couldn't read 0x%04x rc = %x\n", RA9530_REG_RTX_STATUS, rc);
	} else {
		if (value && (RA9530_RTX_READY & reg_tx)) {
			chg_err("set tx enable\n");
			ra9530_write_reg_multi_byte(chip, RA9530_REG_UVLO_THD, wbuff, 2);
			ra9530_config_interface(chip, RA9530_REG_RTX_CMD, 0x01, 0xFF);
		} else if (!value && (RA9530_RTX_TRANSFER & reg_tx)) {
			chg_err("set tx disable\n");
			ra9530_config_interface(chip, RA9530_REG_RTX_CMD, 0x04, 0xFF);
		} else {
			chg_err("tx status err, return\n");
			return;
		}
	}
}

static void ra9530_set_ble_addr(struct oplus_ra9530_ic *chip)
{
	int rc = 0;
	unsigned char addr[6];
	unsigned char decode_addr[6];
	unsigned long int ble_addr = 0;

	rc = ra9530_read_reg(chip, RA9530_BLE_MAC_ADDR0, &addr[0], 1);
	if (rc) {
		chg_err("Couldn't read 0x%02x rc = %x\n", RA9530_BLE_MAC_ADDR0, rc);
		return;
	}

	rc = ra9530_read_reg(chip, RA9530_BLE_MAC_ADDR1, &addr[1], 1);
	if (rc) {
		chg_err("Couldn't read 0x%02x rc = %x\n", RA9530_BLE_MAC_ADDR1, rc);
		return;
	}

	rc = ra9530_read_reg(chip, RA9530_BLE_MAC_ADDR2, &addr[2], 1);
	if (rc) {
		chg_err("Couldn't read 0x%02x rc = %x\n", RA9530_BLE_MAC_ADDR2, rc);
		return;
	}

	rc = ra9530_read_reg(chip, RA9530_BLE_MAC_ADDR3, &addr[3], 1);
	if (rc) {
		chg_err("Couldn't read 0x%02x rc = %x\n", RA9530_BLE_MAC_ADDR3, rc);
		return;
	}

	rc = ra9530_read_reg(chip, RA9530_BLE_MAC_ADDR4, &addr[4], 1);
	if (rc) {
		chg_err("Couldn't read 0x%02x rc = %x\n", RA9530_BLE_MAC_ADDR4, rc);
		return;
	}

	rc = ra9530_read_reg(chip, RA9530_BLE_MAC_ADDR5, &addr[5], 1);
	if (rc) {
		chg_err("Couldn't read 0x%02x rc = %x\n", RA9530_BLE_MAC_ADDR5, rc);
		return;
	}

	/* decode high 3 bytes */
	decode_addr[5] = ((addr[5] & 0x0F) << 4) | ((addr[2] & 0xF0) >> 4);
	decode_addr[4] = ((addr[4] & 0x0F) << 4) | ((addr[1] & 0xF0) >> 4);
	decode_addr[3] = ((addr[3] & 0x0F) << 4) | ((addr[0] & 0xF0) >> 4);

	/* decode low 3 bytes */
	decode_addr[2] = ((addr[2] & 0x0F) << 4) | ((addr[5] & 0xF0) >> 4);
	decode_addr[1] = ((addr[1] & 0x0F) << 4) | ((addr[4] & 0xF0) >> 4);
	decode_addr[0] = ((addr[0] & 0x0F) << 4) | ((addr[3] & 0xF0) >> 4);

	/* caculate final ble mac addr */
	ble_addr = decode_addr[5]  << 16 | decode_addr[4] << 8 | decode_addr[3];
	chip->ble_mac_addr = ble_addr << 24 | decode_addr[2] << 16 | decode_addr[1] << 8 | decode_addr[0];
	chg_err("ra9530 ble_mac_addr = 0x%016llx,\n", chip->ble_mac_addr);
}

static void ra9530_set_protect_parameter(struct oplus_ra9530_ic *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: ra9530_chip not ready!\n", __func__);
		return;
	}

	chg_err("config ocp_threshold = 0x%02x, ovp_threshold = 0x%02x \
			lvp_threshold = 0x%02x, fod_threshold = 0x%02x \
			pcop_threshold1 = 0x%02x, pcop_threshold2 = 0x%02x\n",
			chip->ocp_threshold, chip->ovp_threshold,
			chip->lvp_threshold, chip->fod_threshold,
			chip->pcop_threshold1, chip->pcop_threshold2);
}

static void ra9530_set_private_data(struct oplus_ra9530_ic *chip)
{
	int rc = 0, i = 0, count = 3;
	unsigned char data[6];
	unsigned long int private_data = 0;

	rc = ra9530_read_reg(chip, RA9530_PRIVATE_DATA_REG, &data[0], count);
	if (rc) {
		chg_err("Couldn't read 0x%02x rc = %x\n", RA9530_PRIVATE_DATA_REG, rc);
		return;
	}

	for (i = 0; i < count; i++) {
		chg_err("ra9530 private data %d = 0x%02x\n", i, data[count-1-i]);
		private_data += data[count-1-i];
		if (i < count - 1) {
			private_data = private_data << 8;
		}
	}

	chip->private_pkg_data = private_data;
	chg_err("ra9530 private_pkg_data = 0x%llx,\n", chip->private_pkg_data);
}

static void ra9530_send_uevent(struct device *dev, bool status, unsigned long int mac_addr)
{
	char status_string[UEVENT_MESSAGE_MAX] = {0};
	char addr_string[UEVENT_MESSAGE_MAX] = {0};
	char *envp[] = {status_string, addr_string, NULL};
	int ret = 0;

	sprintf(status_string, "pencil_status=%d", status);
	sprintf(addr_string, "pencil_addr=%llx", mac_addr);
	ret = kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
	if (ret)
		chg_err("%s: kobject_uevent_fail, ret = %d", __func__, ret);

	chg_err("send uevent:%s, %s.\n", status_string, addr_string);
}

static void ra9530_power_enable(struct oplus_ra9530_ic *chip, bool enable)
{
	if (!chip) {
		return;
	}

	if (enable) {
		ra9530_set_vbat_en_val(chip, 1);
		udelay(1000);
		ra9530_set_booster_en_val(chip, 1);
		chip->is_power_on = true;
	} else {
		ra9530_set_booster_en_val(chip, 0);
		udelay(1000);
		ra9530_set_vbat_en_val(chip, 0);
		chip->tx_current = NUM_0;
		chip->tx_voltage = NUM_0;
		chip->is_power_on = false;
	}

	return;
}

static void ra9530_disable_tx_power(struct oplus_ra9530_ic *chip)
{
	chg_err("<~WPC~> ra9530_disable_tx_power\n");
	ra9530_set_tx_mode(0);
	ra9530_power_enable(chip, false);

	chip->ble_mac_addr = 0;
	chip->private_pkg_data = 0;
}

void ra9530_reg_print(void)
{
	char debug_data[6];

	ra9530_read_reg(ra9530_chip, RA9530_REG_RTX_ERR_STATUS, debug_data, 2);
	chg_err("0x74 REG: 0x%02X 0x%02X\n",
			debug_data[0], debug_data[1]);

	ra9530_read_reg(ra9530_chip, RA9530_REG_RTX_STATUS, debug_data, 1);
	chg_err("0x78 REG: 0x%02X\n", debug_data[0]);

	ra9530_read_reg(ra9530_chip, RA9530_BLE_MAC_ADDR0, &debug_data[0], 1);
	ra9530_read_reg(ra9530_chip, RA9530_BLE_MAC_ADDR1, &debug_data[1], 1);
	ra9530_read_reg(ra9530_chip, RA9530_BLE_MAC_ADDR2, &debug_data[2], 1);
	ra9530_read_reg(ra9530_chip, RA9530_BLE_MAC_ADDR3, &debug_data[3], 1);
	ra9530_read_reg(ra9530_chip, RA9530_BLE_MAC_ADDR4, &debug_data[4], 1);
	ra9530_read_reg(ra9530_chip, RA9530_BLE_MAC_ADDR5, &debug_data[5], 1);
	chg_err("0xbe-0xc3 REG: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
			debug_data[0], debug_data[1], debug_data[2],
			debug_data[3], debug_data[4], debug_data[5]);
}

static int RA9530_load_bootloader(struct oplus_ra9530_ic *chip)
{
	int rc = 0;
	uint16_t package_count = NUM_0, index = NUM_0;

	rc = __ra9530_write_reg(chip, 0x3000, 0x5a); /* write key*/
	if (rc != 0) {
		chg_err("[%s] <IDT UPDATE>Write 0x3000 reg error!\n", __func__);
		return rc;
	}

	rc = __ra9530_write_reg(chip, REG_3040, HEX_10); /* halt microcontroller M0*/
	if (rc != NUM_0) {
		chg_err("<IDT UPDATE>Write 0x3040 reg error!\n");
		return rc;
	}

	rc = __ra9530_write_reg(chip, HEX_3020, HEX_5A); /* disable watchdog*/
	if (rc != NUM_0) {
		chg_err("[%s] <IDT UPDATE>Write 0x3020 reg error!\n", __func__);
		return rc;
	}
	chg_err("[%s] <IDT UPDATE>-b-2--!\n", __func__);
	rc = __ra9530_write_reg(chip, 0x3040, 0x10); /* halt microcontroller M0*/
	if (rc != 0) {
		chg_err("<IDT UPDATE>Write 0x3040 reg error!\n");
		return rc;
	}

	msleep(10);

	chg_err("[%s] <IDT UPDATE>-b-3--!\n", __func__);
	package_count = (sizeof(MTPBootloader9530) + RA9530_FW_CODE_LENGTH - 1)/RA9530_FW_CODE_LENGTH;
	chg_err("[%s] <IDT UPDATE> start loader bootloader(package_count:%d) into ram. \n", __func__, package_count);
	for (index = 0; index < package_count - 1; index++) {
		rc = ra9530_write_reg_multi_byte(chip, RA9530_FW_CODE_LENGTH * index,
							MTPBootloader9530 + RA9530_FW_CODE_LENGTH * index, RA9530_FW_CODE_LENGTH); /* load provided by IDT array */
		if (rc != NUM_0) {
			chg_err("[%s] <IDT UPDATE>Write bootloader reg(Index:%d) error!\n", __func__, index);
			return rc;
		}
	}
	if (package_count) {	/* write the last bootloader part */
		rc = ra9530_write_reg_multi_byte(chip, RA9530_FW_CODE_LENGTH * (package_count - 1),
							MTPBootloader9530 + RA9530_FW_CODE_LENGTH * (package_count - 1),
							sizeof(MTPBootloader9530) - RA9530_FW_CODE_LENGTH * (package_count - 1)); /* load provided by IDT array */
		if (rc != NUM_0) {
			chg_err("[%s] <IDT UPDATE>Write bootloader reg(the last part) error!\n", __func__);
			return rc;
		}
	}

	chg_err("[%s] <IDT UPDATE>-b-4--!\n", __func__);
	rc = __ra9530_write_reg(chip, 0x3048, 0xF0); /* map RAM address 0x1c00 to OTP 0x0000*/
	if (rc != 0) {
		chg_err("[%s] <IDT UPDATE>Write 0x3048 reg error!\n", __func__);
		return rc;
	}

	chg_err("[%s] <IDT UPDATE>-b-5--!\n", __func__);
	rc = __ra9530_write_reg(chip, 0x3040, 0x80); /* run M0*/

	return 0;
}

static int RA9530_e2p_check(struct oplus_ra9530_ic *chip)
{
	int rc = NUM_0;
	uint8_t write_ack = NUM_0, retry_times = NUM_0;
	uint16_t boot_version = NUM_0, package_count = NUM_0, index = NUM_0;

	msleep(MS_5);
	chg_err("[%s] <IDT UPDATE> start unlock system and halt the uc. \n", __func__);
	rc = __ra9530_write_reg(chip, HEX_3000, HEX_5A); /* write key*/
	if (rc != NUM_0) {
		chg_err("[%s] <IDT UPDATE>Write 0x3000 reg error!\n", __func__);
		return rc;
	}

	rc = __ra9530_write_reg(chip, REG_3040, HEX_10); /* halt microcontroller M0*/
	if (rc != NUM_0) {
		chg_err("<IDT UPDATE>Write 0x3040 reg error!\n");
		return rc;
	}

	rc = __ra9530_write_reg(chip, HEX_3020, HEX_5A); /* disable watchdog*/
	if (rc != NUM_0) {
		chg_err("[%s] <IDT UPDATE>Write 0x3020 reg error!\n", __func__);
		return rc;
	}

	rc = __ra9530_write_reg(chip, REG_3040, HEX_10); /* halt microcontroller M0*/
	if (rc != NUM_0) {
		chg_err("<IDT UPDATE>Write 0x3040 reg error!\n");
		return rc;
	}

	rc = __ra9530_write_reg(chip, REG_5C50, HEX_5A); /* write key*/
	if (rc != NUM_0) {
		chg_err("[%s] <IDT UPDATE>Write 0x5C50 reg error!\n", __func__);
		return rc;
	}

	chg_err("[%s] <IDT UPDATE> start check register status. \n", __func__);
	retry_times = NUM_0;
	do {
		rc = __ra9530_write_reg(chip, REG_5C2C, HEX_2);
		if (rc != NUM_0) {
			chg_err("[%s] <IDT UPDATE>Write HEX_5C2C reg error!\n", __func__);
			return rc;
		}

		msleep(MS_30);
		write_ack = 0;
		rc = ra9530_read_reg(chip, REG_5C2C, &write_ack, NUM_1);
		if (rc != NUM_0) {
			chg_err("<IDT UPDATE>IIC error\n");
			return rc;
		}
		retry_times++;
	} while (((write_ack & HEX_80) == NUM_0) && (retry_times < NUM_3));

	/* check status*/
	chip->boot_check_status = ((write_ack >> 4) & 0x07) << 8 | (chip->boot_check_status & 0xFF);
	if (NUM_0 == (write_ack & 0x70)) { /* not OK*/
		chg_err("<IDT UPDATE>E2P_check1 ok\n");
	} else {
		chg_err("<IDT UPDATE>E2P_check1 fail:%d\n", write_ack);
		rc = ERR_SRAM;
		return rc;
	}

	package_count = (sizeof(MTPBootloader9530) + RA9530_FW_CODE_LENGTH - 1)/RA9530_FW_CODE_LENGTH;
	chg_err("[%s] <IDT UPDATE> start loader bootloader(package_count:%d) into ram. \n", __func__, package_count);
	for (index = 0; index < package_count - 1; index++) {
		rc = ra9530_write_reg_multi_byte(chip, RA9530_FW_CODE_LENGTH * index,
			MTPBootloader9530 + RA9530_FW_CODE_LENGTH * index, RA9530_FW_CODE_LENGTH); /* load provided by IDT array */
		if (rc != NUM_0) {
			chg_err("[%s] <IDT UPDATE>Write bootloader reg(Index:%d) error!\n", __func__, index);
			return rc;
		}
	}

	if (package_count) {	/* write the last bootloader part */
		rc = ra9530_write_reg_multi_byte(chip, RA9530_FW_CODE_LENGTH * (package_count - 1),
			MTPBootloader9530 + RA9530_FW_CODE_LENGTH * (package_count - 1),
			sizeof(MTPBootloader9530) - RA9530_FW_CODE_LENGTH * (package_count - 1)); /* load provided by IDT array */
		if (rc != NUM_0) {
			chg_err("[%s] <IDT UPDATE>Write bootloader reg(the last part) error!\n", __func__);
			return rc;
		}
	}

	rc = __ra9530_write_reg(chip, REG_3048, HEX_F0); /* map RAM address 0x1c00 to OTP 0x0000*/
	if (rc != NUM_0) {
		chg_err("[%s] <IDT UPDATE>Write 0x3048 reg error!\n", __func__);
		return rc;
	}

	__ra9530_write_reg(chip, REG_3040, HEX_80); /* run M0*/

	msleep(MS_100);
	rc = ra9530_read_reg(chip, REG_1000, (unsigned char*)&boot_version, NUM_2);
	if (rc) {
		chg_err("<IDT UPDATE>Couldn't read REG_1000 rc = %x\n", rc);
	} else {
		chg_err("<IDT UPDATE>E2P_check boot version: 0x%02x\n", boot_version);
	}

	chg_err("[%s] <IDT UPDATE> start program readiness check. \n", __func__);
	rc = __ra9530_write_reg(chip, REG_1008, DATA_31);
	if (rc != NUM_0) {
		chg_err("<IDT UPDATE>ERROR: on OTP buffer validation\n");
		return rc;
	}

	retry_times = NUM_0;
	do {
		write_ack = 0;
		msleep(MS_1000);
		rc = ra9530_read_reg(chip, REG_1008, &write_ack, NUM_1);
		chg_err("<IDT UPDATE>E2P_check2 :%d\n", write_ack);
		if (rc != NUM_0) {
			chg_err("<IDT UPDATE>ERROR: IIC\n");
			return rc;
		}
		retry_times++;
	} while (((write_ack & HEX_1) != NUM_0) && (retry_times < NUM_8));

	/* check status*/
	rc = ra9530_read_reg(chip, REG_1009, &write_ack, NUM_1);
	if (rc != NUM_0) {
		chg_err("<IDT UPDATE>IIC error\n");
		return rc;
	}
	chip->boot_check_status = (chip->boot_check_status & 0xFF00) | write_ack;
	if (NUM_0 == write_ack) { /* not OK*/
		chg_err("<IDT UPDATE>E2P_check2 ok\n");
	} else {
		chg_err("<IDT UPDATE>E2P_check2 fail:%d\n", write_ack);
		rc = ERR_EEPROM;	/* continue to power cycle for update */
	}

	/*disable power RA9530*/
	chg_err("<IDT UPDATE> Disable power RA9530.\n");
	ra9530_power_enable(chip, false);
	msleep(MS_5000);

	/*power RA9530 again*/
	chg_err("<IDT UPDATE> Power RA9530 again.\n");
	ra9530_power_enable(chip, true);
	msleep(MS_500);

	return rc;
}

static int RA9530_load_fw(struct oplus_ra9530_ic *chip, unsigned char *fw_data, int codelength)
{
	unsigned char write_ack = 0;
	int rc = NUM_0, retry_times = NUM_0;

	rc = ra9530_write_reg_multi_byte(chip, 0x1008, fw_data,
							((codelength + 8 + 15) / 16) * 16);
	if (rc != 0) {
		chg_err("<IDT UPDATE>ERROR: write multi byte data error!\n");
		goto LOAD_ERR;
	}

	rc = __ra9530_write_reg(chip, 0x1008, 0x01);
	if (rc != 0) {
		chg_err("<IDT UPDATE>ERROR: on OTP buffer validation\n");
		goto LOAD_ERR;
	}

	retry_times = NUM_0;
	do {
		msleep(MS_20);
		rc = ra9530_read_reg(chip, REG_1008, &write_ack, NUM_1);
		if (rc != NUM_0) {
			chg_err("<IDT UPDATE>ERROR: on reading OTP buffer status\n");
			goto LOAD_ERR;
		}
		retry_times++;
	} while (((write_ack & HEX_1) != NUM_0) && (retry_times < NUM_10));

	/* check status*/
	rc = ra9530_read_reg(chip, REG_1009, &write_ack, NUM_1);
	if (rc != NUM_0) {
		chg_err("<IDT UPDATE>IIC error\n");
		return rc;
	}
	if (write_ack != 2) { /* not OK*/
		if (write_ack == 4)
			chg_err("<IDT UPDATE>ERROR: WRITE ERR\n");
		else if (write_ack == 8)
			chg_err("<IDT UPDATE>ERROR: CHECK SUM ERR\n");
		else
			chg_err("<IDT UPDATE>ERROR: UNKNOWN ERR write_ack=%d\n", write_ack);

		rc = -1;
	}
LOAD_ERR:
	return rc;
}

static int RA9530_Crc32_fw(struct oplus_ra9530_ic *chip, int start_addr, int fw_size, unsigned int crc)
{
	unsigned char write_ack = 0;
	int rc = NUM_0, retry_times = NUM_0;
	unsigned char crc_buff[4]={0, 0, 0, 0};

	memcpy(crc_buff, (char *)&start_addr, 2);
	rc = ra9530_write_reg_multi_byte(chip, 0x100A, crc_buff, 2);

	memcpy(crc_buff, (char *)&fw_size, 2);
	chg_err("RA9530_Crc32_fw fw_size buff[0]=%x buff[1]=%x] fw_size=0x%x!\n", crc_buff[0], crc_buff[1], fw_size);

	rc = ra9530_write_reg_multi_byte(chip, 0x100C, crc_buff, 2);

	memcpy(crc_buff, (char *)&crc, 4);
	chg_err("RA9530_Crc32_fw crc data buff[0]=%x buff[1]=%x] buff[2]=%x buff[3]=%x crc=%x!\n", crc_buff[0], crc_buff[1], crc_buff[2], crc_buff[3], crc);
	rc = ra9530_write_reg_multi_byte(chip, 0x1010, crc_buff, 4);

	if (rc != 0) {
		chg_err("<IDT UPDATE>ERROR: write multi byte data error!\n");
		goto LOAD_ERR;
	}
	rc = __ra9530_write_reg(chip, 0x1008, 0x11);
	if (rc != 0) {
		chg_err("<IDT UPDATE>ERROR: on OTP buffer validation\n");
		goto LOAD_ERR;
	}

	retry_times = NUM_0;
	do {
		msleep(MS_150);
		rc = ra9530_read_reg(chip, REG_1008, &write_ack, NUM_1);
		if (rc != NUM_0) {
			chg_err("<IDT UPDATE>ERROR: on reading OTP buffer status\n");
			goto LOAD_ERR;
		}
		retry_times++;
	} while (((write_ack & HEX_1) != NUM_0) && (retry_times < NUM_10));

	/* check status*/
	rc = ra9530_read_reg(chip, REG_1009, &write_ack, NUM_1);
	if (rc != NUM_0) {
		chg_err("<IDT UPDATE>IIC error\n");
		return rc;
	}
	if (write_ack != NUM_2) {
		if (write_ack == HEX_1) {
			chg_err("<IDT UPDATE>ERROR: CRC32 BUSY\n");
		} else if (write_ack == HEX_54) {
			chg_err("<IDT UPDATE>ERROR: CRC32 Failure\n");
		} else {
			chg_err("<IDT UPDATE>ERROR: CRC32 UNKNOWN ERR(0x%02x)\n", write_ack);
		}
		rc = ERR_NUM;
	}
LOAD_ERR:
	return rc;
}

static int RA9530_MTP(struct oplus_ra9530_ic *chip, unsigned char *fw_buf, int fw_size)
{
	int rc = -1;
	int i = 0, j = 0;
	unsigned char *fw_data = NULL;
	unsigned short int startaddr = 0;
	unsigned short int checksum = 0;
	unsigned short int codelength = 0;
	int pure_fw_size = fw_size - RA9530_PURESIZE_OFFSET;
	unsigned int crc32value = 0;

	if (chip->unhealth_memory_handle_support) {
		rc = RA9530_e2p_check(chip);
		if (0 == rc) {
			chg_err("<IDT UPDATE>e2p totally check ok, continue to update!\n");
		} else if (ERR_EEPROM == rc) {
			chg_err("<IDT UPDATE>e2p check eeprom failed, continue to update reliable firmware!\n");
			fw_buf = chip->match_fw_buf;
			fw_size = chip->match_fw_size;
			pure_fw_size = fw_size - RA9530_PURESIZE_OFFSET;
		} else if (ERR_SRAM == rc) {
			chg_err("<IDT UPDATE>e2p check sram failed, return!\n");
			return rc;
		} else {
			chg_err("<IDT UPDATE>e2p check other error, return!\n");
			return rc;
		}
	}

	memcpy(&crc32value, fw_buf + 0x7eec, 4);
	chg_err("<IDT UPDATE>--1--crc32value=%x!\n", crc32value);

	rc = RA9530_load_bootloader(chip);
	if (rc != 0) {
		chg_err("<IDT UPDATE>Update bootloader 1 error!\n");
		return rc;
	}

	msleep(100);

	chg_err("<IDT UPDATE>The idt firmware size: %d!\n", fw_size);

	/* program pages of 128 bytes
	 8-bytes header, 128-bytes data, 8-bytes padding to round to 16-byte boundary*/
	fw_data = kzalloc(RA9530_FW_PAGE_SIZE, GFP_KERNEL);
	if (!fw_data) {
		chg_err("<IDT UPDATE>can't alloc memory!\n");
		return -EINVAL;
	}

	/*ERASE FW VERSION(the last 128 byte of the MTP)*/
	memset(fw_data, 0x00, RA9530_FW_PAGE_SIZE);
	startaddr = pure_fw_size;
	checksum = startaddr;
	codelength = RA9530_FW_CODE_LENGTH;
	for (j = RA9530_FW_CODE_LENGTH - 1; j >= 0; j--)
		checksum += fw_data[j + 8]; /* add the non zero values.*/

	checksum += codelength; /* finish calculation of the check sum*/
	memcpy(fw_data + 2, (char *)&startaddr, 2);
	memcpy(fw_data + 4, (char *)&codelength, 2);
	memcpy(fw_data + 6, (char *)&checksum, 2);
	rc = RA9530_load_fw(chip, fw_data, codelength);
	if (rc < 0) {
		chg_err("<IDT UPDATE>ERROR: erase fw version ERR\n");
		goto MTP_ERROR;
	}

	/* upgrade fw*/
	memset(fw_data, 0x00, RA9530_FW_PAGE_SIZE);
	for (i = 0; i < fw_size - 20; i += RA9530_FW_CODE_LENGTH) {
		chg_err("<IDT UPDATE>Begin to write chunk %d!\n", i);

		startaddr = i;
		checksum = startaddr;
		codelength = RA9530_FW_CODE_LENGTH;

		memcpy(fw_data + 8, fw_buf + i, RA9530_FW_CODE_LENGTH);

		j = fw_size - i;
		if (j < RA9530_FW_CODE_LENGTH) {
			j = ((j + 15) / 16) * 16;
			codelength = (unsigned short int)j;
		} else {
			j = RA9530_FW_CODE_LENGTH;
		}

		j -= 1;
		for (; j >= 0; j--)
			checksum += fw_data[j + 8]; /* add the non zero values*/

		checksum += codelength; /* finish calculation of the check sum*/

		memcpy(fw_data + 2, (char *)&startaddr, 2);
		memcpy(fw_data + 4, (char *)&codelength, 2);
		memcpy(fw_data + 6, (char *)&checksum, 2);

/*		typedef struct {  write to structure at address 0x400
		 u16 Status;
		 u16 startaddr;
		 u16 codelength;
		 u16 DataChksum;
		 u8 DataBuf[128];
		} P9220PgmStrType;
		 read status is guaranteed to be != 1 at this point
*/
		rc = RA9530_load_fw(chip, fw_data, codelength);
		if (rc < 0) {
			chg_err("<IDT UPDATE>ERROR: write chunk %d ERR\n", i);
			goto MTP_ERROR;
		}
	}

	msleep(100);
	/*disable power RA9530*/
	chg_err("<IDT UPDATE> Disable power RA9530.\n");
	ra9530_power_enable(chip, false);
	msleep(3000);

	/*power RA9530 again*/
	chg_err("<IDT UPDATE> Power RA9530 again.\n");
	ra9530_power_enable(chip, true);
	msleep(500);

	/* Verify*/
	rc = RA9530_load_bootloader(chip);

	msleep(100);

	startaddr = 0;
	rc = RA9530_Crc32_fw(chip, startaddr, fw_size - 20, crc32value);
	if (rc < 0) {
		chg_err("crc32 err\n");
		goto MTP_ERROR;
	}
	else
		chg_err("crc32 ok\n");

	memset(fw_data, 0x00, RA9530_FW_PAGE_SIZE);
	startaddr = pure_fw_size;
	checksum = startaddr;
	codelength = RA9530_FW_CODE_LENGTH;
	memcpy(fw_data + 8, fw_buf + startaddr, RA9530_FW_CODE_LENGTH);
	j = RA9530_FW_CODE_LENGTH - 1;

	if (chip->unhealth_memory_handle_support && chip->boot_check_status) {
		/* Add write E2P error flag */
		fw_data[j + NUM_8- NUM_4] =  S_MAGIC_WORDS;
		fw_data[j + NUM_8- NUM_5] =  S_MAGIC_WORDS;
	}
	for (; j >= 0; j--)
		checksum += fw_data[j + 8]; /* add the non zero values.*/

	checksum += codelength; /* finish calculation of the check sum*/
	memcpy(fw_data + 2, (char *)&startaddr, 2);
	memcpy(fw_data + 4, (char *)&codelength, 2);
	memcpy(fw_data + 6, (char *)&checksum, 2);

	rc = RA9530_load_fw(chip, fw_data, codelength);
	if (rc < 0) {
		chg_err("<IDT UPDATE>ERROR: write fw version ERR\n");
		goto MTP_ERROR;
	}

	msleep(100);
	/*disable power RA9530*/
	chg_err("<IDT UPDATE> Disable power RA9530.\n");
	ra9530_power_enable(chip, false);
	msleep(3000);

	/*power RA9530 again*/
	chg_err("<IDT UPDATE> Power RA9530 again.\n");
	ra9530_power_enable(chip, true);
	msleep(500);

	chg_err("<IDT UPDATE>OTP Programming finished\n");

	kfree(fw_data);
	return 0;

MTP_ERROR:
	kfree(fw_data);
	return -EINVAL;
}

static void ra9530_set_match_firmware(struct oplus_ra9530_ic *chip)
{
	int rc = -1;
	int fw_size = 0;
	unsigned char *fw_buf = NULL;
	int fw_ver_start_addr = 0;
	char temp[5] = {0, 0, 0, 0, 0};

	if (!chip) {
		return;
	}

	chip->match_fw_buf = p9530_idt_firmware_olso_mp8;
	chip->match_fw_size = ARRAY_SIZE(p9530_idt_firmware_olso_mp8);

	rc = ra9530_read_reg(chip, 0x001C, temp, 4);
	if (rc) {
		chg_err("%s: Couldn't read 0x%04x rc = %x\n", __func__, 0x001C, rc);
	}

	rc = ra9530_read_reg(chip, 0x0017, temp + 4, 1);
	if (rc) {
		chg_err("%s: Couldn't read 0x0017 rc = %x\n", __func__, 0x0017, rc);
	}

	fw_buf = p9530_idt_firmware_olso_mp7;
	fw_size = ARRAY_SIZE(p9530_idt_firmware_olso_mp7);
	fw_ver_start_addr = fw_size - RA9530_FW_VERSION_OFFSET;
	if ((temp[0] == fw_buf[fw_ver_start_addr + 0x05])\
		&& (temp[1] == fw_buf[fw_ver_start_addr + 0x06])\
		&& (temp[2] == fw_buf[fw_ver_start_addr + 0x07])\
		&& (temp[3] == fw_buf[fw_ver_start_addr + 0x08])\
		&& (temp[4] == fw_buf[fw_ver_start_addr])) {
		chip->match_fw_buf = fw_buf;
		chip->match_fw_size = fw_size;
		chg_err("%s: set match firmware mp7 version.\n", __func__);
	} else {
		chg_err("%s: set match firmware mp8 version.\n", __func__);
	}

	return;
}

static int ra9530_check_idt_fw_update(struct oplus_ra9530_ic *chip, bool force_update)
{
	static int idt_update_retry_cnt = 0;
	int rc = -1, mtp_result = 0;
	char temp[5] = {0, 0, 0, 0, 0};
	char flag[2] = {0, 0};
	unsigned char *fw_buf = NULL;
	int fw_size = 0;
	int fw_ver_start_addr = 0;
	bool power_state = false;

	chg_err("<IDT UPDATE> check idt fw (force:%d)<><><><><><><><>\n", force_update);

	if (!chip) {
		chg_err("<IDT UPDATE> ra9530 isn't ready!\n");
		return rc;
	}

	mutex_lock(&chip->flow_mutex);

	power_state = chip->is_power_on;		/* remember current power status before start upgrade */
	if (power_state) {
		ra9530_power_enable(chip, false);
		msleep(10);
	}
	ra9530_power_enable(chip, true);
	msleep(1000);

	chip->boot_check_status = 0xFFFF;		/*set check status default value*/
	rc = ra9530_read_reg(chip, 0x001C, temp, 4);
	if (rc) {
		chg_err("<IDT UPDATE>Couldn't read 0x%04x rc = %x\n", 0x001C, rc);
	} else {
		rc = ra9530_read_reg(chip, 0x0017, temp + 4, 1);
		if (rc)
			chg_err("<IDT UPDATE>Couldn't read 0x0017 rc = %x\n", 0x001C, rc);

		if (chip->unhealth_memory_handle_support) {
			rc = ra9530_read_reg(chip, REG_1A, flag, NUM_2);
			if (rc) {
				chg_err("<IDT UPDATE>Couldn't read 0x001A rc = %x\n",  rc);
			}
		}

		chg_err("<IDT UPDATE>The idt fw version: %02x %02x %02x %02x beta version:%02x old_flag:%02x %02x\n", \
					temp[NUM_0], temp[NUM_1], temp[NUM_2], temp[NUM_3], temp[NUM_4], flag[NUM_0], flag[NUM_1]);

		fw_buf = p9530_idt_firmware_latest;
		fw_size = ARRAY_SIZE(p9530_idt_firmware_latest);

		chg_err("<IDT UPDATE>The idt fw size=%d\n", fw_size);

		fw_ver_start_addr = fw_size - RA9530_FW_VERSION_OFFSET;
		chg_err("<IDT UPDATE>The new fw version: %02x %02x %02x %02x beta version:%02x\n",
				fw_buf[fw_ver_start_addr + 0x05], fw_buf[fw_ver_start_addr + 0x06],
				fw_buf[fw_ver_start_addr + 0x07], fw_buf[fw_ver_start_addr + 0x08], fw_buf[fw_ver_start_addr]);

		if (force_update
			|| (((temp[NUM_0] != fw_buf[fw_ver_start_addr + HEX_5])\
			|| (temp[NUM_1] != fw_buf[fw_ver_start_addr + HEX_6])\
			|| (temp[NUM_2] != fw_buf[fw_ver_start_addr + HEX_7])\
			|| (temp[NUM_3] != fw_buf[fw_ver_start_addr + HEX_8])\
			|| (temp[NUM_4] != fw_buf[fw_ver_start_addr]))\
			&& (flag[NUM_0] != S_MAGIC_WORDS)\
			&& (flag[NUM_1] != S_MAGIC_WORDS))) {
			chg_err("<IDT UPDATE>Need update the idt fw!\n");

			if (chip->unhealth_memory_handle_support) {
				ra9530_set_match_firmware(chip);
			}
			mtp_result = RA9530_MTP(chip, fw_buf, fw_size);
			if (NUM_0 == mtp_result) {
				chg_err("<IDT UPDATE>Update success!!!\n");
			} else if (ERR_SRAM == mtp_result) {
				chg_err("<IDT UPDATE>No More Update!!!\n");
			} else {
				idt_update_retry_cnt++;
				if (idt_update_retry_cnt > NUM_2) {
					chg_err("<IDT UPDATE>Update fail!!!\n");
				} else {
					chg_err("<IDT UPDATE>Update fail, retry %d!\n", idt_update_retry_cnt);
					rc = ERR_NUM;
				}
			}
		} else {
			chg_err("<IDT UPDATE>No Need update the idt fw!\n");
		}
	}

	ra9530_power_enable(chip, false);
	msleep(10);
	if (power_state) {					/* restore power state when already powered up */
		chg_err("ra9530 start restore power!\n");
		ra9530_power_enable(chip, true);
		msleep(100);
		ra9530_set_protect_parameter(chip);
		ra9530_set_tx_mode(1);
	}
	mutex_unlock(&chip->flow_mutex);

	return rc;
}

static int oplus_wpc_chg_parse_chg_dt(struct oplus_ra9530_ic *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		chg_err("device tree info. missing\n");
		return -EINVAL;
	}

	rc = of_property_read_u32(node, "qcom,ocp_threshold",
			&chip->ocp_threshold);
	if (rc) {
		chip->ocp_threshold = RA9530_OCP_THRESHOLD;
	}
	chg_err("ocp_threshold[%d]\n", chip->ocp_threshold);

	rc = of_property_read_u32(node, "qcom,ovp_threshold",
			&chip->ovp_threshold);
	if (rc) {
		chip->ovp_threshold = RA9530_OVP_THRESHOLD;
	}
	chg_err("ovp_threshold[%d]\n", chip->ovp_threshold);

	rc = of_property_read_u32(node, "qcom,lvp_threshold",
			&chip->lvp_threshold);
	if (rc) {
		chip->lvp_threshold = RA9530_LVP_THRESHOLD;
	}
	chg_err("lvp_threshold[%d]\n", chip->lvp_threshold);

	rc = of_property_read_u32(node, "qcom,pcop_threshold1",
			&chip->pcop_threshold1);
	if (rc) {
		chip->pcop_threshold1 = RA9530_PCOP_THRESHOLD1;
	}
	chg_err("pcop_threshold1[%d]\n", chip->pcop_threshold1);

	rc = of_property_read_u32(node, "qcom,pcop_threshold2",
			&chip->pcop_threshold2);
	if (rc) {
		chip->pcop_threshold2 = RA9530_PCOP_THRESHOLD2;
	}
	chg_err("pcop_threshold2[%d]\n", chip->pcop_threshold2);

	rc = of_property_read_u32(node, "qcom,fod_threshold",
			&chip->fod_threshold);
	if (rc) {
		chip->fod_threshold = RA9530_FOD_THRESHOLD2;
	}
	chg_err("fod_threshold[%d]\n", chip->fod_threshold);

	rc = of_property_read_u32(node, "rx,soc_threshold",
			&chip->soc_threshould);
	if (rc) {
		chip->soc_threshould = RA9530_SOC_THRESHOLD2;
	}

	rc = of_property_read_u32(node, "tx,enable_expired_time",
			&chip->power_expired_time);
	if (rc) {
		chip->power_expired_time = POWER_EXPIRED_TIME_DEFAULT;
	}

	/*add feature to control the unhealth memory handle for oslo pad, default false value */
	chip->unhealth_memory_handle_support = of_property_read_bool(node, "unhealth_memory_handle_support");

	return 0;
}

void ra9530_update_powerdown_info(struct oplus_ra9530_ic *chip, uint8_t type)
{
	struct timeval now_time;

	if (type >= PEN_OFF_REASON_MAX) {
		chg_err("update power type error:%d.\n", type);
		return;
	} else if (PEN_REASON_FAR == type) {
		chip->power_disable_reason = type;
		chip->charger_done_time = 0;
		return;
	}

	chip->power_disable_reason = type;
	chip->power_disable_times[type]++;
	do_gettimeofday(&now_time);
	chip->charger_done_time = (now_time.tv_sec * 1000 + now_time.tv_usec / 1000 - chip->tx_start_time)/MIN_TO_MS;
}
void ra9530_ept_type_detect_func(struct oplus_ra9530_ic *chip)
{
	unsigned short recv_data = 0;
	int rc = 0, count_limit = 0;
	static int count = 0;

	rc = ra9530_read_reg(chip, RA9530_REG_RTX_ERR_STATUS, (char *)&recv_data, 2);
	if (rc) {
		chg_err("Couldn't read 0x%04x rc = %x\n", RA9530_REG_RTX_ERR_STATUS, rc);
		return;
	} else {
		chg_err(" read RA9530_REG_RTX_ERR_STATUS value: %x\n", recv_data);
		if ((recv_data & RA9530_RTX_ERR_TX_POCP) ||
			(recv_data & RA9530_RTX_ERR_TX_OTP) ||
			(recv_data & RA9530_RTX_ERR_TX_FOD) ||
			(recv_data & RA9530_RTX_ERR_TX_LVP) ||
			(recv_data & RA9530_RTX_ERR_TX_OVP) ||
			(recv_data & RA9530_RTX_ERR_TX_OCP)) {
			chg_err("error happen %02x! ra9530 will poweroff soon!\n", recv_data);
			ra9530_disable_tx_power(chip);
			ra9530_update_powerdown_info(chip, PEN_REASON_CHARGE_OCP);
		} else if (recv_data & RA9530_RTX_ERR_TX_CEP_TIMEOUT) {
			if (!chip->cep_count_flag) {
				count_limit = 5;
				chip->cep_count_flag = 1;
				count = 0;
			}

			count++;
			if (count > count_limit) {
				chg_err("cep_timeout, count_limit = %d! ra9530 will poweroff soon!\n", count_limit);
				count = 0;
				chip->cep_count_flag = 0;
				/*ra9530_disable_tx_power(); */ /*No used to disable tx power, ra9530 will try it again. */
			}
		} else if (recv_data & RA9530_RTX_ERR_TX_EPT_CMD) {
			chg_err("power transfer terminated! ra9530 will poweroff soon!\n");
			ra9530_disable_tx_power(chip);
			ra9530_update_powerdown_info(chip, PEN_REASON_CHARGE_EPT);
		} else {
			chg_err("ept_type detect error %02x!", recv_data);
			return;
		}
	}

	return;
}

static bool ra9530_valid_check(struct oplus_ra9530_ic *chip)
{
	unsigned long int addr = 0;
	unsigned long int pdata = 0;

	if (!chip) {
		return false;
	}

	addr = chip->ble_mac_addr;
	pdata = chip->private_pkg_data;
	if ((((addr >> 40) & 0xFF) ^ ((addr >> 16) & 0xFF)) != ((pdata >> 16) & 0xFF)) {
		return false;
	}
	if ((((addr >> 32) & 0xFF) ^ ((addr >> 8) & 0xFF)) != ((pdata >> 8) & 0xFF)) {
		return false;
	}
	if ((((addr >> 24) & 0xFF) ^ (addr & 0xFF)) != (pdata & 0xFF)) {
		return false;
	}

	chg_err("ra9530_valid_check: %02x %02x %02x.\n", ((pdata >> 16) & 0xFF), ((pdata >> 8) & 0xFF), (pdata & 0xFF));
	return true;
}

static void ra9530_check_private_flag(struct oplus_ra9530_ic *chip)
{
	uint8_t rc = 0, val = 0;

	rc = ra9530_read_reg(chip, RA9530_REG_CHARGE_PERCENT, &val, 1);
	if (rc) {
		chg_err("0x3A READ FAIL.\n");
		return;
	}

	chg_err("[%s] charge_percent=%d\n", __func__, val);

	msleep(CHECK_PRIVATE_DELAY);
	ra9530_disable_tx_power(chip);
	if (100 == val) {               /* charger full, can stop charging now */
		ra9530_update_powerdown_info(chip, PEN_REASON_CHARGE_FULL);
	} else {
		ra9530_update_powerdown_info(chip, PEN_REASON_CHARGE_STOP);
	}

	return;
}

static void ra9530_commu_data_process(struct oplus_ra9530_ic *chip)
{
	int rc = 0;
	struct timeval now_time;

/* for ato cal ping time start*/
	char ping_int_flag[NUM_4];

	rc = ra9530_read_reg(chip, RA9530_REG_PING, ping_int_flag, NUM_1);
	if (rc) {
		chg_err("ra9530 0x2C  FAIL\n");
	}
	chg_err("int ATO 0x2C: %02x %02x %02x %02x\n", chip->int_flag_data[NUM_0], chip->int_flag_data[NUM_1], chip->int_flag_data[NUM_2], chip->int_flag_data[NUM_3]);

	if ((ping_int_flag[NUM_0] & RA9530_PING_SUCC) && !chip->ping_succ_time) {
		do_gettimeofday(&now_time);
		chip->ping_succ_time = now_time.tv_sec * 1000 + now_time.tv_usec / 1000 - chip->tx_start_time;
		chg_err("ra9530 ping_succ_time(%lld)\n", chip->ping_succ_time);
	}
/* for ato cal ping time end*/

	rc = ra9530_read_reg(chip, RA9530_REG_INT_FLAG, chip->int_flag_data, 4);
	if (rc) {
		chg_err("RA9530 x30 READ FAIL\n");
	}
	chg_err("int: %02x %02x %02x %02x\n", chip->int_flag_data[0], chip->int_flag_data[1], chip->int_flag_data[2], chip->int_flag_data[3]);

	if (chip->int_flag_data[1] & RA9530_INT_FLAG_BLE_ADDR) {
		if (!chip->ble_mac_addr) {
			ra9530_set_ble_addr(chip);
			do_gettimeofday(&now_time);
			chip->upto_ble_time = now_time.tv_sec * 1000 + now_time.tv_usec / 1000 - chip->tx_start_time;
			chg_err("GET_BLE_ADDR.(%d)\n", chip->upto_ble_time);
			if (ra9530_valid_check(chip)) {
				if (PEN_REASON_RECHARGE != chip->power_enable_reason) {
					ra9530_send_uevent(chip->wireless_dev, chip->present, chip->ble_mac_addr);
				}
			} else {
				chg_err("check valid data failed!!!\n");
				g_verify_failed_cnt++;
				ra9530_disable_tx_power(chip);
				return;
			}
		}
	}

	if (chip->int_flag_data[2] & RA9530_INT_FLAG_PRIVATE_PKG) {
		if (!chip->private_pkg_data) {
			chg_err("GET_PRIVATE_PKG.\n");
			chip->present = 1;
			notify_pen_state(1);
			check_int_enable(chip);
			ra9530_set_private_data(chip);
		}
	}

	if (chip->int_flag_data[1] & RA9530_INT_FLAG_STOP_CHARGING) {
		chg_err("GET_PRIVATE_FLAG!!!\n");
		ra9530_check_private_flag(chip);
	}

	if (chip->int_flag_data[0] & RA9530_INT_FLAG_EPT) {
		ra9530_ept_type_detect_func(chip);
	}

	if (chip->int_flag_data[0] & RA9530_INT_FLAG_DPING) {
		/*DPING int*/
		chg_err("DPING int!!!\n");
	}

	if (chip->int_flag_data[0] & RA9530_INT_FLAG_SS) {
		/*SS int*/
		chip->present = 1;
		notify_pen_state(1);
		check_int_enable(chip);
		schedule_delayed_work(&chip->check_point_dwork, round_jiffies_relative(msecs_to_jiffies(RA9530_CHECK_SS_INT_TIME)));
		chg_err("ra9530 ss int ,present value :%d.\n", chip->present);
	}

	if (chip->is_power_on) {                /* avoid some i2c error when powered off before check here */
		/*clear irq*/
		ra9530_check_clear_irq(chip);
		msleep(CHECK_IRQ_DELAY);
		if (ra9530_get_idt_int_val() == 0) {
			chg_err("clear IRT again!\n");
			ra9530_check_clear_irq(chip);
		}
	}

	return;
}

int ra9530_get_idt_int_val(void)
{
	struct oplus_ra9530_ic *chip = ra9530_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: ra9530_chip not ready!\n", __func__);
		return -1;
	}

	if (chip->idt_int_gpio <= 0) {
		chg_err("idt_int_gpio not exist, return\n");
		return -1;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl)
		|| IS_ERR_OR_NULL(chip->idt_int_active)
		|| IS_ERR_OR_NULL(chip->idt_int_sleep)) {
		chg_err("pinctrl null, return\n");
		return -1;
	}

	return gpio_get_value(chip->idt_int_gpio);
}

static irqreturn_t irq_idt_event_int_handler(int irq, void *dev_id)
{
	struct oplus_ra9530_ic *chip = (struct oplus_ra9530_ic *)dev_id;

	__pm_stay_awake(chip->bus_wakelock);
	wait_event_interruptible_timeout(i2c_waiter, chip->i2c_ready, msecs_to_jiffies(50));
	mutex_lock(&chip->flow_mutex);
	if (chip->i2c_ready && chip->is_power_on) {
		ra9530_commu_data_process(chip);
	} else {
		chg_err("ra9530_idt_event_int unhandled by i2c:%d, power:%d!\n", chip->i2c_ready, chip->is_power_on);
	}
	mutex_unlock(&chip->flow_mutex);
	__pm_relax(chip->bus_wakelock);

	return IRQ_HANDLED;
}

static void ra9530_set_idt_int_active(struct oplus_ra9530_ic *chip)
{
	gpio_direction_input(chip->idt_int_gpio);
	pinctrl_select_state(chip->pinctrl, chip->idt_int_active);
}

static void ra9530_idt_int_irq_init(struct oplus_ra9530_ic *chip)
{
	chip->idt_int_irq = gpio_to_irq(chip->idt_int_gpio);
	chg_err("chip->idt_int_irq[%d]\n", __func__, chip->idt_int_irq);
}

static void ra9530_idt_int_eint_register(struct oplus_ra9530_ic *chip)
{
	int retval = 0;

	ra9530_set_idt_int_active(chip);

	/* 0x01:rising edge,0x02:falling edge */
	retval = request_threaded_irq(chip->idt_int_irq, NULL, irq_idt_event_int_handler,
                                   IRQF_TRIGGER_LOW | IRQF_ONESHOT, "ra9530_idt_int", chip);
	if (retval < 0) {
		chg_err("%s request idt_int irq failed.\n", __func__);
	}
	retval = enable_irq_wake(chip->idt_int_irq);
	if (retval != 0) {
		chg_err("enable_irq_wake: idt_int_irq failed %d\n", retval);
	}
}

static int ra9530_idt_int_gpio_init(struct oplus_ra9530_ic *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_ra9530_ic not ready!\n", __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	/*idt_int*/
	chip->idt_int_active =
			pinctrl_lookup_state(chip->pinctrl, "idt_int_active");
	if (IS_ERR_OR_NULL(chip->idt_int_active)) {
		chg_err("get idt_int_active fail\n");
		return -EINVAL;
	}

	chip->idt_int_sleep =
			pinctrl_lookup_state(chip->pinctrl, "idt_int_sleep");
	if (IS_ERR_OR_NULL(chip->idt_int_sleep)) {
		chg_err("get idt_int_sleep fail\n");
		return -EINVAL;
	}

	chip->idt_int_default =
			pinctrl_lookup_state(chip->pinctrl, "idt_int_default");
	if (IS_ERR_OR_NULL(chip->idt_int_default)) {
		chg_err("get idt_int_default fail\n");
		return -EINVAL;
	}

	if (chip->idt_int_gpio > 0) {
		gpio_direction_input(chip->idt_int_gpio);
	}

	pinctrl_select_state(chip->pinctrl, chip->idt_int_active);

	return 0;
}

static int ra9530_vbat_en_gpio_init(struct oplus_ra9530_ic *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_ra9530_ic not ready!\n", __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	/*vbat_en*/
	chip->vbat_en_active =
			pinctrl_lookup_state(chip->pinctrl, "vbat_en_active");
	if (IS_ERR_OR_NULL(chip->vbat_en_active)) {
		chg_err("get vbat_en_active fail\n");
		return -EINVAL;
	}

	chip->vbat_en_sleep =
			pinctrl_lookup_state(chip->pinctrl, "vbat_en_sleep");
	if (IS_ERR_OR_NULL(chip->vbat_en_sleep)) {
		chg_err("get vbat_en_sleep fail\n");
		return -EINVAL;
	}

	chip->vbat_en_default =
			pinctrl_lookup_state(chip->pinctrl, "vbat_en_default");
	if (IS_ERR_OR_NULL(chip->vbat_en_default)) {
		chg_err("get vbat_en_default fail\n");
		return -EINVAL;
	}

	gpio_direction_output(chip->vbat_en_gpio, 0);
	pinctrl_select_state(chip->pinctrl,
			chip->vbat_en_sleep);

	return 0;
}

void ra9530_set_vbat_en_val(struct oplus_ra9530_ic *chip, int value)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_ra9530_ic not ready!\n", __func__);
		return;
	}

	if (chip->vbat_en_gpio <= 0) {
		chg_err("vbat_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl)
		|| IS_ERR_OR_NULL(chip->vbat_en_active)
		|| IS_ERR_OR_NULL(chip->vbat_en_sleep)
		|| IS_ERR_OR_NULL(chip->vbat_en_default)) {
		chg_err("pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(chip->vbat_en_gpio, 1);
		pinctrl_select_state(chip->pinctrl,
				chip->vbat_en_default);
	} else {
		gpio_direction_output(chip->vbat_en_gpio, 0);
		pinctrl_select_state(chip->pinctrl,
				chip->vbat_en_sleep);
	}

	chg_err("<~WPC~>set value:%d, gpio_val:%d\n",
		value, gpio_get_value(chip->vbat_en_gpio));
}

int ra9530_get_vbat_en_val(void)
{
	struct oplus_ra9530_ic *chip = ra9530_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: ra9530_chip not ready!\n", __func__);
		return -1;
	}

	if (chip->vbat_en_gpio <= 0) {
		chg_err("vbat_en_gpio not exist, return\n");
		return -1;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl)
		|| IS_ERR_OR_NULL(chip->vbat_en_active)
		|| IS_ERR_OR_NULL(chip->vbat_en_sleep)
		|| IS_ERR_OR_NULL(chip->vbat_en_default)) {
		chg_err("pinctrl null, return\n");
		return -1;
	}

	return gpio_get_value(chip->vbat_en_gpio);
}

static int ra9530_booster_en_gpio_init(struct oplus_ra9530_ic *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_ra9530_ic not ready!\n", __func__);
		return -EINVAL;
	}

	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		chg_err("get pinctrl fail\n");
		return -EINVAL;
	}

	/*booster_en*/
	chip->booster_en_active =
			pinctrl_lookup_state(chip->pinctrl, "booster_en_active");
	if (IS_ERR_OR_NULL(chip->booster_en_active)) {
		chg_err("get booster_en_active fail\n");
		return -EINVAL;
	}

	chip->booster_en_sleep =
			pinctrl_lookup_state(chip->pinctrl, "booster_en_sleep");
	if (IS_ERR_OR_NULL(chip->booster_en_sleep)) {
		chg_err("get booster_en_sleep fail\n");
		return -EINVAL;
	}

	chip->booster_en_default =
			pinctrl_lookup_state(chip->pinctrl, "booster_en_default");
	if (IS_ERR_OR_NULL(chip->booster_en_default)) {
		chg_err("get booster_en_default fail\n");
		return -EINVAL;
	}

	gpio_direction_output(chip->booster_en_gpio, 0);
	pinctrl_select_state(chip->pinctrl,
			chip->booster_en_sleep);

	chg_err("gpio_val:%d\n", gpio_get_value(chip->booster_en_gpio));

	return 0;
}

void ra9530_set_booster_en_val(struct oplus_ra9530_ic *chip, int value)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_ra9530_ic not ready!\n", __func__);
		return;
	}

	if (chip->booster_en_gpio <= 0) {
		chg_err("booster_en_gpio not exist, return\n");
		return;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl)
		|| IS_ERR_OR_NULL(chip->booster_en_active)
		|| IS_ERR_OR_NULL(chip->booster_en_sleep)
		|| IS_ERR_OR_NULL(chip->booster_en_default)) {
		chg_err("pinctrl null, return\n");
		return;
	}

	if (value) {
		gpio_direction_output(chip->booster_en_gpio, 1);
		pinctrl_select_state(chip->pinctrl,
				chip->booster_en_active);
	} else {
		gpio_direction_output(chip->booster_en_gpio, 0);
		pinctrl_select_state(chip->pinctrl,
				chip->booster_en_sleep);
	}

	chg_err("<~WPC~>set value:%d, gpio_val:%d\n",
		value, gpio_get_value(chip->booster_en_gpio));
}

int ra9530_get_booster_en_val(void)
{
	struct oplus_ra9530_ic *chip = ra9530_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: ra9530_chip not ready!\n", __func__);
		return -1;
	}

	if (chip->booster_en_gpio <= 0) {
		chg_err("booster_en_gpio not exist, return\n");
		return -1;
	}

	if (IS_ERR_OR_NULL(chip->pinctrl)
		|| IS_ERR_OR_NULL(chip->booster_en_active)
		|| IS_ERR_OR_NULL(chip->booster_en_sleep)
		|| IS_ERR_OR_NULL(chip->booster_en_default)) {
		chg_err("pinctrl null, return\n");
		return -1;
	}

	return gpio_get_value(chip->booster_en_gpio);
}

void ra9530_update_cc_cv(struct oplus_ra9530_ic *chip)
{
	int rc = 0;
	unsigned char temp[2] = {0, 0};

	if (!chip) {
		return;
	}

	rc = ra9530_read_reg(chip, RA9530_REG_CURRENT_MA, temp, 2);
	if (rc) {
		chg_err("Couldn't read tx_current reg 0x006E rc = %d\n", rc);
	}
	chip->tx_current = temp[1] << 8;
	chip->tx_current += temp[0];
	chg_err("tx_current: %02x %02x  dec:%d mA\n", temp[0], temp[1], chip->tx_current);

	rc = ra9530_read_reg(chip, RA9530_REG_AVR_CURRENT_MA, temp, 2);
	if (rc) {
		chg_err("Couldn't read tx_current reg 0x006E rc = %d\n", rc);
	}
	chip->tx_avr_current = temp[1] << 8;
	chip->tx_avr_current += temp[0];
	chg_err("tx_avr_current: %02x %02x  dec:%d mA\n", temp[0], temp[1], chip->tx_avr_current);


	rc = ra9530_read_reg(chip, RA9530_REG_VOLTAGE_MV, temp, 2);
	if (rc) {
		chg_err("Couldn't read tx_voltage reg 0x0070 rc = %d\n", rc);
	}
	chip->tx_voltage = temp[1] << 8;
	chip->tx_voltage += temp[0];
	chg_err("tx_voltage: %02x %02x  dec:%d mV\n", temp[0], temp[1], chip->tx_voltage);

	rc = ra9530_read_reg(chip, RA9530_REG_OUTPUT_VOLTAGE_MV, temp, 2);
	if (rc) {
		chg_err("Couldn't read tx_output_voltage reg 0x0082 rc = %d\n", rc);
	}
	chip->tx_output_voltage = temp[1] << 8;
	chip->tx_output_voltage += temp[0];
	chg_err("tx_output_voltage: %02x %02x  dec:%d mV\n", temp[0], temp[1], chip->tx_output_voltage);

	return;
}

void ra9530_timer_inhall_function(struct work_struct *work)
{
	struct oplus_ra9530_ic *chip = ra9530_chip;
	int count = 0;

	__pm_stay_awake(present_wakelock);

	chg_err("%s:  enter!\n", __func__);

	while (count++ < RA9530_WAIT_TIME && !chip->present && chip->is_power_on) {
		msleep(MS_100);
	}

	if (chip->present) {/*check present flag */
		chg_err("%s:  find pen,count=%d\n", __func__, count);
		ra9530_update_cc_cv(chip);
		g_pen_ornot = NUM_1;
	} else {
		/* no ss int for P9418_WAIT_TIME sec, power off */
		chg_err("%s:  find not pen,count=%d\n", __func__, count);
		ra9530_power_enable(chip, false);
		g_pen_ornot = NUM_0;
	}

	__pm_relax(present_wakelock);

	chg_err("%s:  exit!\n", __func__);

	return;
}

void ra9530_power_expired_do_check(struct oplus_ra9530_ic *chip)
{
	struct timeval now_time;
	u64 time_offset = 0;

	if (chip->is_power_on) {
		do_gettimeofday(&now_time);
		time_offset = now_time.tv_sec * 1000 + now_time.tv_usec / 1000 - chip->tx_start_time;
		chg_err("%s:time_offset(%lld).\n", __func__, time_offset);
		if ((time_offset/MIN_TO_MS) >= chip->power_expired_time) {
			ra9530_disable_tx_power(chip);
			ra9530_update_powerdown_info(chip, PEN_REASON_CHARGE_TIMEOUT);
		}
	}

	return;
}

void ra9530_power_expired_check_func(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_ra9530_ic *chip = container_of(dwork, struct oplus_ra9530_ic, power_check_work);

	if(!chip) {
		return;
	}

	__pm_stay_awake(chip->bus_wakelock);
	wait_event_interruptible(i2c_waiter, chip->i2c_ready);
	mutex_lock(&chip->flow_mutex);

	chg_err("%s:time_offset check.\n", __func__);
	ra9530_power_expired_do_check(chip);

	mutex_unlock(&chip->flow_mutex);
	__pm_relax(chip->bus_wakelock);

	return;
}

static void ra9530_do_switch(struct oplus_ra9530_ic *chip, uint8_t status)
{
	struct timeval now_time;

	if (!chip) {
		return;
	}

	if (status == PEN_STATUS_NEAR) {/* hall near, power on*/
		ra9530_power_enable(chip, true);
		msleep(100);
		ra9530_set_protect_parameter(chip);
		ra9530_set_tx_mode(1);
		chip->power_enable_reason = PEN_REASON_NEAR;
		do_gettimeofday(&now_time);
		chip->tx_start_time = now_time.tv_sec * 1000 + now_time.tv_usec / 1000;
		schedule_work(&ra9530_idt_timer_work);
		schedule_delayed_work(&chip->power_check_work, \
						round_jiffies_relative(msecs_to_jiffies(chip->power_expired_time * MIN_TO_MS)));
	} else if (status == PEN_STATUS_FAR) {/* hall far, power off */
		ra9530_power_enable(chip, false);

		chip->present = 0;
		chip->ble_mac_addr = 0;
		chip->private_pkg_data = 0;
		chip->tx_current = 0;
		chip->tx_voltage = 0;
		chip->ping_succ_time = 0;
		ra9530_update_powerdown_info(chip, PEN_REASON_FAR);
		chg_err("ra9530 hall far away,present value :%d", chip->present);
		cancel_work_sync(&ra9530_idt_timer_work);
		cancel_delayed_work(&chip->power_check_work);
		notify_pen_state(0);
		ra9530_send_uevent(chip->wireless_dev, chip->present, chip->ble_mac_addr);
	}

	return;
}

static void ra9530_power_switch_func(struct work_struct *work)
{
	struct oplus_ra9530_ic *chip = container_of(work, struct oplus_ra9530_ic, power_switch_work);

	if (!chip) {
		return;
	}
	chg_err("ra9530 power work started, i2c_ready:%d.\n", chip->i2c_ready);

	__pm_stay_awake(chip->bus_wakelock);
	wait_event_interruptible(i2c_waiter, chip->i2c_ready);
	mutex_lock(&chip->flow_mutex);
	chg_err("ra9530 do switch now.\n");
	ra9530_do_switch(chip, chip->pen_status);
	mutex_unlock(&chip->flow_mutex);
	__pm_relax(chip->bus_wakelock);

	return;
}

static void ra9530_power_onoff_switch(int value)
{
	struct oplus_ra9530_ic *chip = ra9530_chip;

	if (!chip) {
		return;
	}
	chg_err("%s:  switch value: %d, i2c_ready:%d.\n", __func__, value, chip->i2c_ready);

	__pm_stay_awake(chip->bus_wakelock);
	wait_event_interruptible_timeout(i2c_waiter, chip->i2c_ready, msecs_to_jiffies(50));
	mutex_lock(&chip->flow_mutex);

	chip->pen_status = value > 0 ? PEN_STATUS_NEAR : PEN_STATUS_FAR;

	if (chip->i2c_ready) {
		ra9530_do_switch(chip, chip->pen_status);
	} else {
		chg_err("%s: do switch by work.\n", __func__);
		schedule_work(&chip->power_switch_work);
	}

	mutex_unlock(&chip->flow_mutex);
	__pm_relax(chip->bus_wakelock);

	return;
}

static void ra9530_enable_func(struct work_struct *work)
{
	struct timeval now_time;
	struct oplus_ra9530_ic *chip = container_of(work, struct oplus_ra9530_ic, power_enable_work);

	if(!chip) {
		return;
	}

	__pm_stay_awake(chip->bus_wakelock);
	wait_event_interruptible(i2c_waiter, chip->i2c_ready);
	mutex_lock(&chip->flow_mutex);

	if ((chip->rx_soc > chip->soc_threshould) || \
			(chip->pen_status != PEN_STATUS_NEAR) || \
			(false != chip->is_power_on) || \
			(PEN_REASON_CHARGE_FULL != chip->power_disable_reason)) {                       /* check second time */
		chg_err("enable failed for (%d %d %d %d).\n", chip->rx_soc, chip->pen_status, \
				chip->is_power_on, chip->power_disable_reason);
		goto OUT;
	}

	ra9530_power_enable(chip, true);
	msleep(100);
	ra9530_set_protect_parameter(chip);
	ra9530_set_tx_mode(1);
	chip->power_enable_times++;
	chip->power_enable_reason = PEN_REASON_RECHARGE;
	do_gettimeofday(&now_time);
	chip->tx_start_time = now_time.tv_sec * 1000 + now_time.tv_usec / 1000;
	schedule_work(&ra9530_idt_timer_work);
	cancel_delayed_work(&chip->power_check_work);
	schedule_delayed_work(&chip->power_check_work, \
			round_jiffies_relative(msecs_to_jiffies(chip->power_expired_time * MIN_TO_MS)));

OUT:
	mutex_unlock(&chip->flow_mutex);
	__pm_relax(chip->bus_wakelock);

	return;
}

int ra9530_hall_notifier_callback(struct notifier_block *nb, unsigned long event, void *data)
{
	int value = event;
	chg_err("ra9530_hall_notifier_callback enter pen_status:%d", value);
	ra9530_power_onoff_switch(value);

	return NOTIFY_DONE;
}

static int ra9530_idt_gpio_init(struct oplus_ra9530_ic *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;
	chg_err("test %s start\n", __func__);

	/* Parsing gpio idt_int*/
	chip->idt_int_gpio = of_get_named_gpio(node, "qcom,idt_int-gpio", 0);
	if (chip->idt_int_gpio < 0) {
		chg_err("chip->idt_int_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->idt_int_gpio)) {
			rc = gpio_request(chip->idt_int_gpio, "idt-int-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n", chip->idt_int_gpio);
			} else {
				rc = ra9530_idt_int_gpio_init(chip);
				if (rc)
					chg_err("unable to init idt_int_gpio:%d\n", chip->idt_int_gpio);
				else {
					ra9530_idt_int_irq_init(chip);
					ra9530_idt_int_eint_register(chip);
				}
			}
		}
		chg_err("chip->idt_int_gpio =%d\n", chip->idt_int_gpio);
	}

	/* Parsing gpio vbat_en*/
	chip->vbat_en_gpio = of_get_named_gpio(node, "qcom,vbat_en-gpio", 0);
	if (chip->vbat_en_gpio < 0) {
		chg_err("chip->vbat_en_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->vbat_en_gpio)) {
			rc = gpio_request(chip->vbat_en_gpio, "vbat-en-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n", chip->vbat_en_gpio);
			} else {
				rc = ra9530_vbat_en_gpio_init(chip);
				if (rc)
					chg_err("unable to init vbat_en_gpio:%d\n", chip->vbat_en_gpio);
			}
		}
		chg_err("chip->vbat_en_gpio =%d\n", chip->vbat_en_gpio);
	}

	/* Parsing gpio booster_en*/
	chip->booster_en_gpio = of_get_named_gpio(node, "qcom,booster_en-gpio", 0);
	if (chip->booster_en_gpio < 0) {
		chg_err("chip->booster_en_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->booster_en_gpio)) {
			rc = gpio_request(chip->booster_en_gpio, "booster-en-gpio");
			if (rc) {
				chg_err("unable to request gpio [%d]\n", chip->booster_en_gpio);
			} else {
				rc = ra9530_booster_en_gpio_init(chip);
				if (rc)
					chg_err("unable to init booster_en_gpio:%d\n", chip->booster_en_gpio);
			}
		}
		chg_err("chip->booster_en_gpio =%d\n", chip->booster_en_gpio);
	}

	return rc;
}

#ifdef DEBUG_BY_FILE_OPS
static int ra9530_add = 0;
static ssize_t ra9530_reg_store(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	char write_data[32] = {0};
	char val_buf = 0;
	int rc = 0;

	if (copy_from_user(&write_data, buff, len)) {
		chg_err("ra9530_reg_store error.\n");
		return -EFAULT;
	}

	if (len >= ARRAY_SIZE(write_data)) {
		chg_err("data len error.\n");
		return -EFAULT;
	}
	write_data[len] = '\0';
	if (write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}

	ra9530_add = (int)simple_strtoul(write_data, NULL, 0);

	chg_err("%s:received data=%s, ra9530_register address: 0x%02x\n", __func__, write_data, ra9530_add);

	rc = ra9530_read_reg(ra9530_chip, ra9530_add, &val_buf, 1);
	if (rc) {
		 chg_err("Couldn't read 0x%02x rc = %d\n", ra9530_add, rc);
	} else {
		 chg_err("ra9530_read 0x%02x = 0x%02x\n", ra9530_add, val_buf);
	}

	return len;
}

static ssize_t ra9530_reg_show(struct file *filp, char __user *buff, size_t count, loff_t *off)
{
	char page[256] = {0};
	char val_buf;
	int rc;
	int len = 0;

	rc = ra9530_read_reg(ra9530_chip, ra9530_add, &val_buf, 1);
	if (rc) {
		 chg_err("Couldn't read 0x%02x rc = %d\n", ra9530_add, rc);
	}

	len = sprintf(page, "reg = 0x%x, data = 0x%x\n", ra9530_add, val_buf);
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

static const struct file_operations ra9530_add_log_proc_fops = {
	.write = ra9530_reg_store,
	.read = ra9530_reg_show,
};

static void init_ra9530_add_log(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("ra9530_add_log", 0664, NULL, &ra9530_add_log_proc_fops);
	if (!p) {
		chg_err("proc_create init_ra9530_add_log_proc_fops fail!\n");
	}
}

static ssize_t ra9530_data_log_write(struct file *filp, const char __user *buff, size_t len, loff_t *data)
{
	char write_data[32] = {0};
	int critical_log = 0;
	int rc = 0;

	if (copy_from_user(&write_data, buff, len)) {
		chg_err("bat_log_write error.\n");
		return -EFAULT;
	}

	if (len >= ARRAY_SIZE(write_data)) {
		chg_err("data len error.\n");
		return -EFAULT;
	}
	write_data[len] = '\0';
	if (write_data[len - 1] == '\n') {
		write_data[len - 1] = '\0';
	}

	critical_log = (int)simple_strtoul(write_data, NULL, 0);
	if (critical_log > 0xFF) {
		critical_log = 0xFF;
	}

	chg_err("%s:received data=%s, ra9530_data=%x\n", __func__, write_data, critical_log);

	rc = ra9530_config_interface(ra9530_chip, ra9530_add, critical_log, 0xFF);
	if (rc) {
		 chg_err("Couldn't write 0x%02x rc = %d\n", ra9530_add, rc);
	}

	return len;
}

static const struct file_operations ra9530_data_log_proc_fops = {
	.write = ra9530_data_log_write,
};

static void init_ra9530_data_log(void)
{
	struct proc_dir_entry *p = NULL;

	p = proc_create("ra9530_data_log", 0664, NULL, &ra9530_data_log_proc_fops);
	if (!p)
		chg_err("proc_create init_ra9530_data_log_proc_fops fail!\n");
}
#endif /*DEBUG_BY_FILE_OPS*/

static void ra9530_update_work_process(struct work_struct *work)
{
	struct delayed_work *dwork = to_delayed_work(work);
	struct oplus_ra9530_ic *chip = container_of(dwork, struct oplus_ra9530_ic, ra9530_update_work);
	int rc = 0;
	int boot_mode = get_boot_mode();

	if (boot_mode == MSM_BOOT_MODE__FACTORY) {
		chg_err("<IDT UPDATE> MSM_BOOT_MODE__FACTORY do not update\n");
		return;
	}
	chg_err("<IDT UPDATE> ra9530_update_work_process\n");
	rc = ra9530_check_idt_fw_update(chip, false);
	if (rc == ERR_NUM) {
		/* run again after interval */
		schedule_delayed_work(&chip->ra9530_update_work, RA9530_UPDATE_RETRY_INTERVAL);
	}
}

static ssize_t ble_mac_addr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_ra9530_ic *chip = NULL;
	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (chip->is_power_on) {
		if (chip->ble_mac_addr) {
			return sprintf(buf, "0x%lx_(Time:%dms)", chip->ble_mac_addr, chip->upto_ble_time);
		} else {
			return sprintf(buf, "%s", "wait_to_get_addr.");
		}
	} else {
		return sprintf(buf, "%s", "wait_to_connect.");
	}
}
static DEVICE_ATTR_RO(ble_mac_addr);

static ssize_t present_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_ra9530_ic *chip = NULL;

	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d", chip->present);
}
static DEVICE_ATTR_RO(present);

static ssize_t fw_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_ra9530_ic *chip = NULL;
	int rc;
	char temp[4] = {0, 0, 0, 0};

	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (chip->is_power_on) {
		rc = ra9530_read_reg(chip, 0x001C, temp, 4);
		if (rc) {
			chg_err("ra9530 Couldn't read 0x%04x after update rc = %x\n", 0x001C, rc);
		} else {
			chg_err("fw_version_show: %02x %02x %02x %02x\n", temp[0], temp[1], temp[2], temp[3]);
		}

		chip->idt_fw_version = (temp[3] << 24) | (temp[2] << 16) | (temp[1] << 8) | temp[0];

		return sprintf(buf, "0x%x", chip->idt_fw_version);
	}

	return sprintf(buf, "%s", "wait_to_connect.");
}
static DEVICE_ATTR_RO(fw_version);

static ssize_t tx_voltage_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_ra9530_ic *chip = NULL;

	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	chg_err("[%s] enter! is_power_on=%d\n", __func__, chip->is_power_on);
	if (chip->is_power_on) {
		ra9530_update_cc_cv(chip);
	}

	return sprintf(buf, "%d", chip->tx_voltage);
}
static DEVICE_ATTR_RO(tx_voltage);

static ssize_t tx_current_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_ra9530_ic *chip = NULL;

	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	chg_err("[%s] enter! is_power_on=%d\n", __func__, chip->is_power_on);
	if (chip->is_power_on) {
		ra9530_update_cc_cv(chip);
	}

	return sprintf(buf, "%d", chip->tx_current);
}
static DEVICE_ATTR_RO(tx_current);

static ssize_t rx_soc_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_ra9530_ic *chip = NULL;

	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d", chip->rx_soc);
}

static ssize_t rx_soc_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_ra9530_ic *chip = NULL;
	int val = 0;

	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	WRITE_ONCE(chip->rx_soc, val);

	if ((chip->rx_soc <= chip->soc_threshould) && \
			(chip->pen_status == PEN_STATUS_NEAR) && \
			(false == chip->is_power_on) && \
			(PEN_REASON_CHARGE_FULL == chip->power_disable_reason)) {                       /* check first time */
		schedule_work(&chip->power_enable_work);
	} else {
		chg_err("ra9530 value: %d, %d, %d.\n", chip->rx_soc, chip->pen_status, chip->is_power_on);
	}

	return count;
}

static DEVICE_ATTR_RW(rx_soc);

static ssize_t wireless_debug_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	int i = 0, count = 0;
	char tmp_buf[DEBUG_BUFF_SIZE] = {0};
	struct oplus_ra9530_ic *chip = NULL;

	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	for (i = 0; i < PEN_OFF_REASON_MAX; i++) {
		count += sprintf(tmp_buf + count, ":%d", chip->power_disable_times[i]);
	}

	return sprintf(buf, "%d:%d\n%d\n%d:%d\n%d:%d\n%d %s\n", \
			chip->rx_soc, chip->soc_threshould, \
			chip->is_power_on, \
			chip->power_enable_reason, chip->power_enable_times, \
			chip->charger_done_time, chip->power_expired_time, \
			chip->power_disable_reason, tmp_buf);
}

static ssize_t wireless_debug_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = 0;
	struct oplus_ra9530_ic *chip = NULL;

	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, 0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	if (val <= 100) {
		chip->soc_threshould = val;                     /* we use 0-100 as soc threshould */
	} else {
		chip->power_expired_time = val - FULL_SOC;              /* we use more than 100 as power expired time */
	}

	return count;
}

static DEVICE_ATTR_RW(wireless_debug);
static ssize_t ping_time_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_ra9530_ic *chip = NULL;

	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "%d", chip->ping_succ_time);
}
static DEVICE_ATTR_RO(ping_time);
static ssize_t tx_status_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_ra9530_ic *chip = NULL;
	int rc;
	char reg_tx[NUM_6];

	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	rc = ra9530_read_reg(chip, RA9530_REG_RTX_STATUS, reg_tx, 1);
	if (rc) {
		chg_err("9530 tx status read fail\n");
	}

	if (reg_tx[NUM_0] & RA9530_RTX_DIGITALPING) {
		return sprintf(buf, "%s", "Ping");
	} else if (reg_tx[NUM_1] & RA9530_RTX_READY) {
		return sprintf(buf, "%s", "Ready");
	} else if (reg_tx[NUM_3] & RA9530_RTX_TRANSFER) {
		return sprintf(buf, "%s", "Transfer");
	} else if (chip->present) {
		return sprintf(buf, "%s", "Connected");
	}

	return sprintf(buf, "%s", "Disconnect");
}
static DEVICE_ATTR_RO(tx_status);

static ssize_t e2p_check_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int val = NUM_0;
	struct oplus_ra9530_ic *chip = NULL;

	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	if (kstrtos32(buf, NUM_0, &val)) {
		chg_err("buf error\n");
		return -EINVAL;
	}
	if (NUM_1 == val) {
		__pm_stay_awake(chip->bus_wakelock);
		ra9530_check_idt_fw_update(chip, true);
		__pm_relax(chip->bus_wakelock);
	}

	return count;
}

static ssize_t e2p_check_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct oplus_ra9530_ic *chip = NULL;

	chip = (struct oplus_ra9530_ic *)dev_get_drvdata(dev);
	if (!chip) {
		chg_err("chip is NULL\n");
		return -EINVAL;
	}

	return sprintf(buf, "0x%04x", chip->boot_check_status);
}
static DEVICE_ATTR_RW(e2p_check);

static struct device_attribute *pencil_attributes[] = {
	&dev_attr_ble_mac_addr,
	&dev_attr_present,
	&dev_attr_fw_version,
	&dev_attr_tx_current,
	&dev_attr_tx_voltage,
	&dev_attr_rx_soc,
	&dev_attr_wireless_debug,
	&dev_attr_ping_time,
	&dev_attr_tx_status,
	&dev_attr_e2p_check,
	NULL
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
static enum power_supply_property ra9530_wireless_props[] = {
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_PRESENT,
	/* POWER_SUPPLY_PROP_BLE_MAC_ADDR, */
};

static int ra9530_wireless_get_prop(struct power_supply *psy,
				   enum power_supply_property psp,
				   union power_supply_propval *val)
{
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		break;
	/* case POWER_SUPPLY_PROP_BLE_MAC_ADDR: */
	/* 	break; */

	default:
		return -EINVAL;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int ra9530_wireless_set_prop(struct power_supply *psy,
				   enum power_supply_property psp,
				   const union power_supply_propval *val)
{
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		break;
	/* case POWER_SUPPLY_PROP_BLE_MAC_ADDR: */
	/* 	break; */

	default:
		chg_err("set prop %d is not supported\n", psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int ra9530_wireless_prop_is_writeable(struct power_supply *psy,
					    enum power_supply_property psp)
{
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
	/* case POWER_SUPPLY_PROP_BLE_MAC_ADDR: */
		rc = 1;
		break;

	default:
		rc = 0;
		break;
	}

	return rc;
}


static const struct power_supply_desc wireless_psy_desc = {
	.name = "wireless",
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	.properties = ra9530_wireless_props,
	.num_properties = ARRAY_SIZE(ra9530_wireless_props),
	.get_property = ra9530_wireless_get_prop,
	.set_property = ra9530_wireless_set_prop,
	.property_is_writeable = ra9530_wireless_prop_is_writeable,
};

static int ra9530_init_wireless_psy(struct oplus_ra9530_ic *chip)
{
	struct power_supply_config wireless_cfg = {};

	wireless_cfg.drv_data = chip;
	wireless_cfg.of_node = chip->dev->of_node;
	chip->wireless_psy = devm_power_supply_register(
		chip->dev, &wireless_psy_desc, &wireless_cfg);
	if (IS_ERR(chip->wireless_psy)) {
		chg_err("Couldn't register wireless power supply\n");
		return PTR_ERR(chip->wireless_psy);
	}

	return 0;
}
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

static int init_wireless_device(struct oplus_ra9530_ic *chip)
{
	int err = 0, status = 0;
	dev_t devt;
	struct class *wireless_class = NULL;
	struct device_attribute **attrs, *attr;

	wireless_class = class_create(THIS_MODULE, "oplus_wireless");

	status = alloc_chrdev_region(&devt, 0, 1, "tx_wireless");
	chip->wireless_dev = device_create(wireless_class, NULL, devt, NULL, "%s", "pencil");
	chip->wireless_dev->devt = devt;
	dev_set_drvdata(chip->wireless_dev, chip);

	attrs = pencil_attributes;
	while ((attr = *attrs++)) {
		err = device_create_file(chip->wireless_dev, attr);
		if (err) {
			chg_err("device_create_file fail!\n");
			return err;
		}
	}

	return 0;
}

bool ra9530_check_chip_is_null(void)
{
	if (ra9530_chip)
		return false;
	return true;
}

static void ra9530_idt_gpio_deinit(struct oplus_ra9530_ic *chip)
{
	if (!chip) {
		return;
	}

	if (gpio_is_valid(chip->idt_int_gpio)) {
		free_irq(chip->idt_int_irq, chip);
		gpio_free(chip->idt_int_gpio);
	}

	if (gpio_is_valid(chip->vbat_en_gpio)) {
		gpio_free(chip->vbat_en_gpio);
	}

	if (gpio_is_valid(chip->booster_en_gpio)) {
		gpio_free(chip->booster_en_gpio);
	}

	return;
}

static int ra9530_identity_check(struct oplus_ra9530_ic *chip)
{
	int rc = -1;
	char buf[5] = {0};
	int err_cnt = 0;

	if (!chip) {
		return -1;
	}

	disable_irq(chip->idt_int_irq);
	ra9530_power_enable(chip, true);
	msleep(10);
	rc = ra9530_read_reg(chip, RA9530_REG_IDENTITY, buf, 5);
	while (rc < 0) {
		err_cnt++;
		msleep(10);
		rc = ra9530_read_reg(chip, RA9530_REG_IDENTITY, buf, 5);
		chg_err("ra9530 read identify failed.(%d) \n", rc);
		if (err_cnt >= 3) {
			ra9530_power_enable(chip, false);
			return rc;
		}
	}
	ra9530_power_enable(chip, false);
	enable_irq_wake(chip->idt_int_irq);
	enable_irq(chip->idt_int_irq);

	chg_err("chip id = 0x%02x%02x\n", buf[0], buf[4]);
	 if ((buf[0] == 0x95) && (buf[4] == 0x30)) {
		rc = 0;
	} else {
		rc = -1;
	}

	return rc;
}

static int ra9530_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct oplus_ra9530_ic	*chip = NULL;
	int rc = 0;

	chg_err("ra9530_driver_probe call \n");

	chip = devm_kzalloc(&client->dev,
		sizeof(struct oplus_ra9530_ic), GFP_KERNEL);
	if (!chip) {
		chg_err(" kzalloc() failed\n");
		return -ENOMEM;
	}

	chip->client = client;
	chip->dev = &client->dev;
	chip->cep_count_flag = 0;
	chip->i2c_ready = true;
	i2c_set_clientdata(client, chip);
	chip->power_enable_times = 0;
	chip->power_enable_reason = PEN_REASON_UNDEFINED;
	mutex_init(&chip->flow_mutex);
	ra9530_idt_gpio_init(chip);
	oplus_wpc_chg_parse_chg_dt(chip);

	rc = ra9530_identity_check(chip);
	if (rc < 0) {
		chg_err("ra9530 indentity check failed!\n");
		ra9530_idt_gpio_deinit(chip);
		devm_kfree(&client->dev, chip);
		return -1;
	}

	chip->bus_wakelock = wakeup_source_register(NULL, "ra9530_wireless_wakelock");
	present_wakelock = wakeup_source_register(NULL, "ra9530_present_wakelock");

#ifdef DEBUG_BY_FILE_OPS
	init_ra9530_add_log();
	init_ra9530_data_log();
#endif

	INIT_DELAYED_WORK(&chip->ra9530_update_work, ra9530_update_work_process);
	INIT_WORK(&ra9530_idt_timer_work, ra9530_timer_inhall_function);
	INIT_DELAYED_WORK(&chip->power_check_work, ra9530_power_expired_check_func);
	INIT_DELAYED_WORK(&chip->check_point_dwork, ra9530_check_point_function);
	INIT_WORK(&chip->power_enable_work, ra9530_enable_func);
	INIT_WORK(&chip->power_switch_work, ra9530_power_switch_func);
	ra9530_chip = chip;

	schedule_delayed_work(&chip->ra9530_update_work, RA9530_UPDATE_INTERVAL);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	rc = blocking_notifier_chain_register(&hall_notifier, &ra9530_notifier);
#else
	rc = wireless_register_notify(&ra9530_notifier);
#endif
	if (rc < 0) {
		chg_err("blocking_notifier_chain_register error");
	}

	rc = init_wireless_device(chip);
	if (rc < 0) {
		chg_err("Create wireless charge device error.");
	}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	ra9530_init_wireless_psy(chip);
#endif
	chg_err("call end\n");

	return 0;
}

static struct i2c_driver ra9530_i2c_driver;

static int ra9530_driver_remove(struct i2c_client *client)
{
	return 0;
}


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int ra9530_pm_resume(struct device *dev)
{
	struct oplus_ra9530_ic *chip = dev_get_drvdata(dev);

	if (chip) {
		chip->i2c_ready = true;
		chg_err("ra9530_pm_resume.\n");
		wake_up_interruptible(&i2c_waiter);
		ra9530_power_expired_do_check(chip);
	}

	return 0;
}

static int ra9530_pm_suspend(struct device *dev)
{
	struct oplus_ra9530_ic *chip = dev_get_drvdata(dev);

	if (chip) {
		chip->i2c_ready = false;
	}

	return 0;
}

static const struct dev_pm_ops ra9530_pm_ops = {
	.resume		= ra9530_pm_resume,
	.suspend	= ra9530_pm_suspend,
};
#else
static int ra9530_resume(struct i2c_client *client)
{
	return 0;
}

static int ra9530_suspend(struct i2c_client *client, pm_message_t mesg)
{
	return 0;
}
#endif

static void ra9530_reset(struct i2c_client *client)
{
	ra9530_power_enable(ra9530_chip, true);
	check_int_enable(ra9530_chip);
	return;
}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/

static const struct of_device_id ra9530_match[] = {
	{ .compatible = "oplus,ra9530-charger"},
	{ .compatible = "oplus,pencil-wireless-charger"},
	{ },
};

static const struct i2c_device_id ra9530_id[] = {
	{"ra9530-charger", 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, ra9530_id);


static struct i2c_driver ra9530_i2c_driver = {
	.driver		= {
		.name = "ra9530-charger",
		.owner	= THIS_MODULE,
		.of_match_table = ra9530_match,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
		.pm 	= &ra9530_pm_ops,
#endif
	},
	.probe		= ra9530_driver_probe,
	.remove		= ra9530_driver_remove,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
	.resume		= ra9530_resume,
	.suspend	= ra9530_suspend,
#endif
	.shutdown	= ra9530_reset,
	.id_table	= ra9530_id,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(ra9530_i2c_driver);
#else
int ra9530_driver_init(void)
{
	int ret = 0;

	chg_err(" start\n");

	if (i2c_add_driver(&ra9530_i2c_driver) != 0) {
		chg_err("Failed to register ra9530 i2c driver.\n");
	} else {
		chg_err("Success to register ra9530 i2c driver.\n");
	}

	return ret;
}

void ra9530_driver_exit(void)
{
	i2c_del_driver(&ra9530_i2c_driver);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
	blocking_notifier_chain_unregister(&hall_notifier, &ra9530_notifier);
#else
	wireless_unregister_notify(&ra9530_notifier);
#endif
}
#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/
MODULE_DESCRIPTION("Driver for ra9530 charger chip");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:ra9530-charger");

