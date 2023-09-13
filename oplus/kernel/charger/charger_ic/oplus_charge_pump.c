// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 . All rights reserved.
 */

#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/version.h>
#ifdef CONFIG_OPLUS_CHARGER_MTK
#include <linux/slab.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>
#include <asm/atomic.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
#include <linux/xlog.h>
#include <mt-plat/mtk_rtc.h>
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
#include <mt-plat/charger_type.h>
#else
#include <mt-plat/v1/charger_type.h>
#endif
#include <soc/oplus/device_info.h>
#endif
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/module.h>
#else
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
#include <soc/oplus/device_info.h>
#endif

#include "../oplus_vooc.h"
#include "../oplus_gauge.h"
#include "oplus_charge_pump.h"
#include <linux/proc_fs.h>

static struct chip_charge_pump *the_chip = NULL;
static DEFINE_MUTEX(charge_pump_i2c_access);

static int __charge_pump_read_reg(int reg, int *returnData)
{
    int ret = 0;
    int retry = 3;
    struct chip_charge_pump *chip = the_chip;

    if(chip == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return -1;
    }

    ret = i2c_smbus_read_byte_data(chip->client, reg);
    if (ret < 0) {
        while(retry > 0) {
            msleep(10);
            ret = i2c_smbus_read_byte_data(chip->client, reg);
            if (ret < 0) {
                retry--;
            } else {
                *returnData = ret;
                return 0;
            }
        }
        chg_err("i2c read fail: can't read from %02x: %d\n", reg, ret);
        return ret;
    } else {
        *returnData = ret;
    }

    return 0;
}

static int charge_pump_read_reg(int reg, int *returnData)
{
    int ret = 0;

    mutex_lock(&charge_pump_i2c_access);
    ret = __charge_pump_read_reg(reg, returnData);
    mutex_unlock(&charge_pump_i2c_access);
    return ret;
}

static int __charge_pump_write_reg(int reg, int val)
{
    int ret = 0;
    struct chip_charge_pump *chip = the_chip;
    int retry = 3;

    if(chip == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return -1;
    }

    ret = i2c_smbus_write_byte_data(chip->client, reg, val);
    if (ret < 0) {
        while(retry > 0) {
            usleep_range(5000, 5000);
            ret = i2c_smbus_write_byte_data(chip->client, reg, val);
            if (ret < 0) {
                retry--;
            } else {
                break;
            }
        }
    }

    if (ret < 0) {
        chg_err("i2c write fail: can't write %02x to %02x: %d\n",
                val, reg, ret);
        return ret;
    }

    return 0;
}

static int charge_pump_write_reg(int reg, int val)
{
    int ret = 0;

    mutex_lock(&charge_pump_i2c_access);
    ret = __charge_pump_write_reg(reg, val);
    mutex_unlock(&charge_pump_i2c_access);
    return ret;
}

/**********************************************************
  *
  *   [Read / Write Function]
  *
  *********************************************************/
/*
static int charge_pump_read_interface (int RegNum, int *val, int MASK, int SHIFT)
{
    int charge_pump_reg = 0;
    int ret = 0;

   //chg_err("--------------------------------------------------\n");

    ret = charge_pump_read_reg(RegNum, &charge_pump_reg);

   //chg_err(" Reg[%x]=0x%x\n", RegNum, charge_pump_reg);

    charge_pump_reg &= (MASK << SHIFT);
    *val = (charge_pump_reg >> SHIFT);

   //chg_err(" val=0x%x\n", *val);

    return ret;
}
*/

static int charge_pump_config_interface(int RegNum, int val, int MASK)
{
    int charge_pump_reg = 0;
    int ret = 0;
    struct chip_charge_pump *divider_ic = the_chip;

    if(divider_ic == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return ret;
    }

    mutex_lock(&charge_pump_i2c_access);
    ret = __charge_pump_read_reg(RegNum, &charge_pump_reg);

    if (ret >= 0) {
        charge_pump_reg &= ~MASK;
        charge_pump_reg |= val;

        if (RegNum == CHARGE_PUMP_ADDRESS_REG04 && divider_ic->hwid == HWID_DA9313) {
            if ((charge_pump_reg & 0x01) == 0) {
                chg_err("DA9313 REG04 can't write 0 to bit0, charge_pump_reg[0x%x] bug here\n", charge_pump_reg);
                dump_stack();
                charge_pump_reg |= 0x01;
            }
        }
        ret = __charge_pump_write_reg(RegNum, charge_pump_reg);
    }

    mutex_unlock(&charge_pump_i2c_access);

    return ret;
}

void charge_pump_dump_registers(void)
{
    int rc;
    int addr;
    unsigned int val_buf[CHARGE_PUMP_REG2_NUMBER] = {0x0};

    for (addr = CHARGE_PUMP_FIRST_REG; addr <= CHARGE_PUMP_LAST_REG; addr++) {
        rc = charge_pump_read_reg(addr, &val_buf[addr]);
        if (rc) {
            chg_err("Couldn't read 0x%02x rc = %d\n", addr, rc);
        } else {
            chg_err("success addr = 0x%02x, value = 0x%02x\n",
                addr, val_buf[addr]);
        }
    }

    for (addr = CHARGE_PUMP_FIRST2_REG; addr <= CHARGE_PUMP_LAST2_REG; addr++) {
        rc = charge_pump_read_reg(addr, &val_buf[addr]);
        if (rc) {
            chg_err("Couldn't read 0x%02x rc = %d\n", addr, rc);
        } else {
            chg_err("success addr = 0x%02x, value = 0x%02x\n",
                addr, val_buf[addr]);
        }
    }
}

static int charge_pump_set_work_mode(int work_mode)
{
    int rc = 0;
    struct chip_charge_pump *divider_ic = the_chip;

    if(divider_ic == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return rc;
    }
    if (atomic_read(&divider_ic->suspended) == 1) {
        return 0;
    }
    if(divider_ic->fixed_mode_set_by_dev_file == true
       && work_mode == CHARGE_PUMP_WORK_MODE_AUTO) {
        chg_err("maybe in high GSM work fixed mode ,return here\n");
        return rc;
    }

    if (oplus_vooc_get_allow_reading() == false) {
        return -1;
    }
    chg_err("work_mode [%d]\n", work_mode);
    switch (divider_ic->hwid) {
    case HWID_DA9313:
        if(work_mode != 0) {
            rc = charge_pump_config_interface(DA9313_WORK_MODE_REG, DA9313_WORK_MODE_AUTO, DA9313_WORK_MODE_MASK);
        } else {
            rc = charge_pump_config_interface(DA9313_WORK_MODE_REG, DA9313_WORK_MODE_FIXED, DA9313_WORK_MODE_MASK);
        }
        break;
    case HWID_MAX77932:
        if(work_mode != 0) {
            rc = charge_pump_config_interface(MAX77932_WORK_MODE_REG, MAX77932_WORK_MODE_AUTO, MAX77932_WORK_MODE_MASK);
        } else {
            rc = charge_pump_config_interface(MAX77932_WORK_MODE_REG, MAX77932_WORK_MODE_FIXED, MAX77932_WORK_MODE_MASK);
        }
        break;
    case HWID_MAX77938:
        if(work_mode != 0) {
            rc = charge_pump_config_interface(MAX77938_WORK_MODE_REG, MAX77938_WORK_MODE_AUTO, MAX77938_WORK_MODE_MASK);
        } else {
            rc = charge_pump_config_interface(MAX77938_WORK_MODE_REG, MAX77938_WORK_MODE_FIXED, MAX77938_WORK_MODE_MASK);
        }
        break;
    case HWID_SD77313:
        if(work_mode != 0) {
            rc = charge_pump_config_interface(SD77313_WORK_MODE_REG, SD77313_WORK_MODE_AUTO, SD77313_WORK_MODE_MASK);
        } else {
            rc = charge_pump_config_interface(SD77313_WORK_MODE_REG, SD77313_WORK_MODE_FIXED, SD77313_WORK_MODE_MASK);
        }
        break;
    default:
        chg_err("divider_ic->hwid: %d unexpect", divider_ic->hwid);
        break;
    }
    return rc;
}

#ifdef CONFIG_OPLUS_CHARGER_MTK
int oplus_set_divider_work_mode(int work_mode)
{
    return charge_pump_set_work_mode(work_mode);
}
EXPORT_SYMBOL(oplus_set_divider_work_mode);
#endif /* CONFIG_OPLUS_CHARGER_MTK */

int da9313_hardware_config(void)
{
    int rc = 0;
    struct chip_charge_pump *divider_ic = the_chip;

    if(divider_ic == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return 0;
    }

    if (atomic_read(&divider_ic->suspended) == 1) {
        return 0;
    }

    chg_err("da9313 hardware config\n");
    charge_pump_set_work_mode(CHARGE_PUMP_WORK_MODE_AUTO);
    return rc;
}

int max77932_hardware_config(void)
{
    int rc = 0;
    struct chip_charge_pump *divider_ic = the_chip;

    if(divider_ic == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return 0;
    }

    if (atomic_read(&divider_ic->suspended) == 1) {
        return 0;
    }

    chg_err("max77932 hardware config\n");
    charge_pump_set_work_mode(CHARGE_PUMP_WORK_MODE_AUTO);
    charge_pump_config_interface(CHARGE_PUMP_ADDRESS_REG05, (BIT(4) | BIT(5)), (BIT(0) | BIT(4) | BIT(5)));
    charge_pump_config_interface(CHARGE_PUMP_ADDRESS_REG06, BIT(5), BIT(5));
    charge_pump_config_interface(CHARGE_PUMP_ADDRESS_REG07, BIT(3), BIT(3));
    charge_pump_config_interface(CHARGE_PUMP_ADDRESS_REG08, BIT(1), BIT(1));
    charge_pump_config_interface(CHARGE_PUMP_ADDRESS_REG09, (BIT(0) | BIT(2)), (BIT(0) | BIT(2)));
    return rc;
}

int max77938_hardware_config(void)
{
    int rc = 0;
    struct chip_charge_pump *divider_ic = the_chip;

    if(divider_ic == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return 0;
    }
    if (atomic_read(&divider_ic->suspended) == 1) {
        return 0;
    }
    chg_err("max77938 hardware config\n");
    rc = charge_pump_config_interface(CHARGE_PUMP_ADDRESS_REG08, (BIT(4) | BIT(5)), (BIT(4) | BIT(5)));
    return rc;
}

int sd77313_hardware_config(void)
{
    int rc = 0;
    struct chip_charge_pump *divider_ic = the_chip;

    if(divider_ic == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return 0;
    }
    if (atomic_read(&divider_ic->suspended) == 1) {
        return 0;
    }

    chg_err("sd77313 hardware config\n");
    charge_pump_config_interface(CHARGE_PUMP_ADDRESS_REG10, BIT(2),  (BIT(2) | BIT(3) | BIT(4) | BIT(5)));
    //charge_pump_config_interface(CHARGE_PUMP_ADDRESS_REG0E, BIT(4), (BIT(4) | BIT(5) |  BIT(6) | BIT(7)));
    return rc;
}

int max77932_read_value_from_reg(int reg_num)
{
    int reg_value = 0;

    charge_pump_read_reg(reg_num, &reg_value);
    chg_err("charge_pump_read_reg reg_num = 0x%02x reg_value = 0x%02x\n", reg_num, reg_value);

    return reg_value;
}

static int charge_pump_get_hwid(struct chip_charge_pump *chip)
{
    int ret = 0;
    int gpio_state_pull_up = 0;
    int gpio_state_pull_down = 0;

    if (!chip) {
        chg_err("charge_pump driver is not ready!\n");
        return 0;
    }

    chip->hwid = HWID_UNKNOW;

    if (chip->charge_pump_hwid_gpio <= 0) {
        chg_err("charge_pump_hwid_gpio not exist, return\n");
        ret = max77932_read_value_from_reg(CHARGE_PUMP_ADDRESS_REG31);
        if(ret == HWID_DA9313) {
            chip->hwid = HWID_DA9313;
        }
        if (ret == HWID_SD77313) {
            chip->hwid = HWID_SD77313;
        }
        chg_err("chip->hwid = %d\n", chip->hwid);
        return chip->hwid;
    }

    if (IS_ERR_OR_NULL(chip->pinctrl)
        || IS_ERR_OR_NULL(chip->charge_pump_hwid_active)
        || IS_ERR_OR_NULL(chip->charge_pump_hwid_sleep)
        || IS_ERR_OR_NULL(chip->charge_pump_hwid_default)) {
        chg_err("pinctrl null, return\n");
        return chip->hwid;
    }

    pinctrl_select_state(chip->pinctrl, chip->charge_pump_hwid_active);
    usleep_range(10000, 10000);
    gpio_state_pull_up = gpio_get_value(chip->charge_pump_hwid_gpio);
    chg_err("gpio_state_pull_up = %d, charge_pump_hwid_gpio : %d\n", gpio_state_pull_up, chip->charge_pump_hwid_gpio);
    pinctrl_select_state(chip->pinctrl, chip->charge_pump_hwid_sleep);
    usleep_range(10000, 10000);
    gpio_state_pull_down = gpio_get_value(chip->charge_pump_hwid_gpio);
    chg_err("gpio_state_pull_down = %d, charge_pump_hwid_gpio : %d\n", gpio_state_pull_down, chip->charge_pump_hwid_gpio);
    if (gpio_state_pull_up == 0 && gpio_state_pull_down == 0) {
        ret = max77932_read_value_from_reg(CHARGE_PUMP_ADDRESS_REG31);
        if(ret == HWID_DA9313) {
            chip->hwid = HWID_DA9313;
        }
        if (ret == HWID_SD77313) {
            chip->hwid = HWID_SD77313;
        }
        chg_debug("reg31 value is 0x%02x, hwid = 0x%02x\n", ret, chip->hwid);
        return chip->hwid;
    } else if (gpio_state_pull_up == 1 && gpio_state_pull_down == 1) {
        ret = max77932_read_value_from_reg(CHARGE_PUMP_ADDRESS_REG16);
        if(ret == HWID_MAX77932) {
            chip->hwid = HWID_MAX77932;
        }
        if (ret == HWID_MAX77938) {
            chip->hwid = HWID_MAX77938;
        }
        chg_debug("reg16 value is 0x%02x, hwid = 0x%02x\n", ret, chip->hwid);
        return chip->hwid;
    }

    chg_err("No device id match.\n");
    return chip->hwid;
}

static void charge_pump_chip_init(struct chip_charge_pump *chip)
{
    struct device_node *node = chip->dev->of_node;

    chip->charge_pump_hwid_gpio = of_get_named_gpio(node, "oplus,charge_pump-hwid-gpio", 0);
    if (chip->charge_pump_hwid_gpio < 0) {
        pr_err("charge_pump_hwid_gpio not specified\n");
        goto HWID_HANDLE;
    }

    chip->pinctrl = devm_pinctrl_get(chip->dev);
    if (IS_ERR_OR_NULL(chip->pinctrl)) {
        chg_err("get charge_pump pinctrl fail\n");
        goto HWID_HANDLE;
    }

    chip->charge_pump_hwid_active = pinctrl_lookup_state(chip->pinctrl, "charge_pump_hwid_active");
    if (IS_ERR_OR_NULL(chip->charge_pump_hwid_active)) {
        chg_err("get charge_pump_hwid_active fail\n");
        goto HWID_HANDLE;
    }

    chip->charge_pump_hwid_sleep = pinctrl_lookup_state(chip->pinctrl, "charge_pump_hwid_sleep");
    if (IS_ERR_OR_NULL(chip->charge_pump_hwid_sleep)) {
        chg_err("get charge_pump_hwid_sleep fail\n");
        goto HWID_HANDLE;
    }

    chip->charge_pump_hwid_default = pinctrl_lookup_state(chip->pinctrl, "charge_pump_hwid_default");
    if (IS_ERR_OR_NULL(chip->charge_pump_hwid_default)) {
        chg_err("get charge_pump_hwid_default fail\n");
        goto HWID_HANDLE;
    }
HWID_HANDLE:
    charge_pump_get_hwid(chip);
    switch (chip->hwid) {
    case HWID_DA9313:
        da9313_hardware_config();
        break;
    case HWID_MAX77932:
        max77932_hardware_config();
        break;
    case HWID_MAX77938:
        max77938_hardware_config();
        break;
    case HWID_SD77313:
        sd77313_hardware_config();
        break;
    default:
        chg_err("No half voltage chip hwid matched!!!\n");
        break;
    }

    charge_pump_dump_registers();

    return;
}

static ssize_t proc_charge_pump_work_mode_read(struct file *file, char __user *buf, size_t count, loff_t *ppos)
{
    uint8_t ret = 0;
    char page[10] = {0};
    int work_mode = 0;
    struct chip_charge_pump *divider_ic = PDE_DATA(file_inode(file));

    if(divider_ic == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return 0;
    }
    if (atomic_read(&divider_ic->suspended) == 1) {
        return 0;
    }

    if (oplus_vooc_get_allow_reading() == false) {
        return 0;
    }

    switch (divider_ic->hwid) {
    case HWID_DA9313:
        ret = charge_pump_read_reg(DA9313_WORK_MODE_REG, &work_mode);
        work_mode = ((work_mode & DA9313_WORK_MODE_MASK) ? 1 : 0);
        break;
    case HWID_SD77313:
        ret = charge_pump_read_reg(SD77313_WORK_MODE_REG, &work_mode);
        work_mode = ((work_mode & SD77313_WORK_MODE_MASK) ? 1 : 0);
        break;
    case HWID_MAX77932:
        ret = charge_pump_read_reg(MAX77932_WORK_MODE_REG, &work_mode);
        work_mode = ((work_mode & MAX77932_WORK_MODE_MASK) ? 0 : 1);
        break;
    case HWID_MAX77938:
        ret = charge_pump_read_reg(MAX77938_WORK_MODE_REG, &work_mode);
        work_mode = ((work_mode & MAX77938_WORK_MODE_MASK) ? 0 : 1);
        break;
    default:
        chg_err("divider_ic->hwid: %d unexpect", divider_ic->hwid);
        break;
    }

    chg_err("work_mode = %d.\n", work_mode);
    ret = snprintf(page, sizeof(page) - 1, "%d", work_mode);
    ret = simple_read_from_buffer(buf, count, ppos, page, strlen(page));

    return ret;
}

static ssize_t proc_charge_pump_work_mode_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
    char buffer[2] = {0};
    int work_mode = 0;
    int rc = 0;
    struct chip_charge_pump *divider_ic = PDE_DATA(file_inode(file));

    if (divider_ic == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return -ENODEV;
    }

    if (atomic_read(&divider_ic->suspended) == 1) {
        return -EBUSY;
    }

    if (copy_from_user(buffer, buf, 2)) {
        chg_err("read proc input error.\n");
        return -EFAULT;
    }

    if (buffer[0] < '0' || buffer[0] > '9') {
        return -EINVAL;
    }

    if (1 != sscanf(buffer, "%d", &work_mode)) {
        chg_err("invalid content: '%s', length = %zd\n", buf, count);
        return -EFAULT;
    }

    if (oplus_vooc_get_allow_reading() == false) {
        return count;
    }

    if (work_mode != 0) {
        divider_ic->fixed_mode_set_by_dev_file = false;
    } else {
        divider_ic->fixed_mode_set_by_dev_file = true;
    }

    switch (divider_ic->hwid) {
    case HWID_DA9313:
        if(work_mode != 0) {
            rc = charge_pump_config_interface(DA9313_WORK_MODE_REG, DA9313_WORK_MODE_AUTO, DA9313_WORK_MODE_MASK);
        } else {
            rc = charge_pump_config_interface(DA9313_WORK_MODE_REG, DA9313_WORK_MODE_FIXED, DA9313_WORK_MODE_MASK);
        }
        break;
    case HWID_MAX77932:
        if(work_mode != 0) {
            rc = charge_pump_config_interface(MAX77932_WORK_MODE_REG, MAX77932_WORK_MODE_AUTO, MAX77932_WORK_MODE_MASK);
        } else {
            rc = charge_pump_config_interface(MAX77932_WORK_MODE_REG, MAX77932_WORK_MODE_FIXED, MAX77932_WORK_MODE_MASK);
        }
        break;
    case HWID_MAX77938:
        if(work_mode != 0) {
            rc = charge_pump_config_interface(MAX77938_WORK_MODE_REG, MAX77938_WORK_MODE_AUTO, MAX77938_WORK_MODE_MASK);
        } else {
            rc = charge_pump_config_interface(MAX77938_WORK_MODE_REG, MAX77938_WORK_MODE_FIXED, MAX77938_WORK_MODE_MASK);
        }
        break;
    case HWID_SD77313:
        if(work_mode != 0) {
            rc = charge_pump_config_interface(SD77313_WORK_MODE_REG, SD77313_WORK_MODE_AUTO, SD77313_WORK_MODE_MASK);
        } else {
            rc = charge_pump_config_interface(SD77313_WORK_MODE_REG, SD77313_WORK_MODE_FIXED, SD77313_WORK_MODE_MASK);
        }
        break;
    default:
        chg_err("divider_ic->hwid:0x%02x unexpect", divider_ic->hwid);
        break;
    }

    chg_err("divider_ic->hwid:0x%02x new work_mode -> %s\n", divider_ic->hwid, ((work_mode != 0) ? "auto" : "fixed"));

    return count;
}

static int charge_pump_read_func(struct seq_file *s, void *v)
{
    int ret = 0;
    int addr;
    struct chip_charge_pump *divider_ic = s->private;
    unsigned int val_buf[CHARGE_PUMP_REG2_NUMBER] = {0x0};

    if(divider_ic == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return 0;
    }

    if (atomic_read(&divider_ic->suspended) == 1) {
        return 0;
    }

    if (oplus_vooc_get_allow_reading() == false) {
        return 0;
    }

    for (addr = CHARGE_PUMP_FIRST_REG; addr <= CHARGE_PUMP_LAST_REG; addr++) {
        ret = charge_pump_read_reg(addr, &val_buf[addr]);
        if (ret) {
            chg_err("Couldn't read 0x%02x ret = %d\n", addr, ret);
        } else {
            chg_err("success addr = 0x%02x, value = 0x%02x\n",
                addr, val_buf[addr]);
            seq_printf(s, "0x%02x : 0x%02x\n", addr, val_buf[addr]);
        }
    }

    for (addr = CHARGE_PUMP_FIRST2_REG; addr <= CHARGE_PUMP_LAST2_REG; addr++) {
        ret = charge_pump_read_reg(addr, &val_buf[addr]);
        if (ret) {
            chg_err("Couldn't read 0x%02x ret = %d\n", addr, ret);
        } else {
            chg_err("success addr = 0x%02x, value = 0x%02x\n",
                addr, val_buf[addr]);
            seq_printf(s, "0x%02x : 0x%02x\n", addr, val_buf[addr]);
        }
    }

    return 0;
}

static ssize_t proc_charge_pump_reg_write(struct file *file, const char __user *buf, size_t count, loff_t *lo)
{
    char buffer[10] = {0};
    int addr = 0;
    int val = 0;
    struct chip_charge_pump *divider_ic = PDE_DATA(file_inode(file));

    if (divider_ic == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return -ENODEV;
    }

    if (atomic_read(&divider_ic->suspended) == 1) {
        return -EBUSY;
    }

    if (copy_from_user(buffer, buf, 10)) {
        chg_err("read proc input error.\n");
        return -EFAULT;
    }

    if (!sscanf(buffer, "0x%x:0x%x", &addr, &val)) {
        chg_err("invalid content: '%s', length = %zd\n", buf, count);
        return -EFAULT;
    }
    chg_err("addr:0x%02x, val:0x%02x.\n", addr, val);
    if (oplus_vooc_get_allow_reading() == false) {
        return count;
    }

    charge_pump_write_reg(addr, val);

    return count;
}

int proc_charge_pump_open(struct inode *inode, struct file *file)
{
    return single_open(file, charge_pump_read_func, PDE_DATA(inode));
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations proc_work_mode_ops = {
    .read  = proc_charge_pump_work_mode_read,
    .write = proc_charge_pump_work_mode_write,
    .open  = simple_open,
    .owner = THIS_MODULE,
};

static const struct file_operations proc_register_ops = {
    .read  = seq_read,
    .write = proc_charge_pump_reg_write,
    .open  = proc_charge_pump_open,
    .owner = THIS_MODULE,
};
#else
static const struct proc_ops proc_work_mode_ops = {
	.proc_read  = proc_charge_pump_work_mode_read,
	.proc_write = proc_charge_pump_work_mode_write,
	.proc_open  = simple_open,
	.proc_lseek = seq_lseek,
};

static const struct proc_ops proc_register_ops = {
	.proc_read  = seq_read,
	.proc_write = proc_charge_pump_reg_write,
	.proc_open  = proc_charge_pump_open,
	.proc_lseek = seq_lseek,
};
#endif

static int init_charge_pump_proc(struct chip_charge_pump *da)
{
    int ret = 0;
    struct proc_dir_entry *prEntry_da = NULL;
    struct proc_dir_entry *prEntry_tmp = NULL;
    struct proc_dir_entry *prEntry_set_reg = NULL;

    prEntry_da = proc_mkdir("oplus_charge_pump", NULL);
    if (prEntry_da == NULL) {
        ret = -ENOMEM;
        chg_debug("Couldn't create charge_pump proc entry\n");
    }

    prEntry_tmp = proc_create_data("work_mode", 0664, prEntry_da, &proc_work_mode_ops, da);
    if (prEntry_tmp == NULL) {
        ret = -ENOMEM;
        chg_debug("Couldn't create proc entry\n");
    }

    prEntry_set_reg = proc_create_data("register", 0664, prEntry_da, &proc_register_ops, da);
    if (prEntry_set_reg == NULL) {
        ret = -ENOMEM;
        chg_err("Couldn't create proc entry\n");
    }

    return ret;
}

static int charge_pump_driver_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    struct chip_charge_pump *divider_ic;

    divider_ic = devm_kzalloc(&client->dev, sizeof(struct chip_charge_pump), GFP_KERNEL);
    if (!divider_ic) {
        chg_err("failed to allocate divider_ic\n");
        return -ENOMEM;
    }

    chg_debug("call\n");
    divider_ic->client = client;
    divider_ic->dev = &client->dev;
    the_chip = divider_ic;
    divider_ic->fixed_mode_set_by_dev_file = false;

    charge_pump_chip_init(divider_ic);
    init_charge_pump_proc(divider_ic);

    return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
static int charge_pump_pm_resume(struct device *dev)
{
    if (!the_chip) {
        return 0;
    }
    atomic_set(&the_chip->suspended, 0);
    return 0;
}

static int charge_pump_pm_suspend(struct device *dev)
{
    if (!the_chip) {
        return 0;
    }
    atomic_set(&the_chip->suspended, 1);
    return 0;
}

static const struct dev_pm_ops charge_pump_pm_ops = {
    .resume                = charge_pump_pm_resume,
    .suspend               = charge_pump_pm_suspend,
};
#else /*(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))*/
static int charge_pump_resume(struct i2c_client *client)
{
    if (!the_chip) {
        return 0;
    }
    atomic_set(&the_chip->suspended, 0);
    return 0;
}

static int charge_pump_suspend(struct i2c_client *client, pm_message_t mesg)
{
    if (!the_chip) {
        return 0;
    }
    atomic_set(&the_chip->suspended, 1);
    return 0;
}
#endif /*(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))*/

static struct i2c_driver charge_pump_i2c_driver;

static int charge_pump_driver_remove(struct i2c_client *client)
{
    chg_debug("enter.\n");
    return 0;
}

static void charge_pump_shutdown(struct i2c_client *client)
{
    struct chip_charge_pump *divider_ic = the_chip;

    chg_debug("enter.\n");

    if (divider_ic == NULL) {
        chg_err("charge_pump driver is not ready\n");
        return;
    }
    if (atomic_read(&divider_ic->suspended) == 1) {
        return;
    }
    switch (divider_ic->hwid) {
    case HWID_DA9313:
        da9313_hardware_config();
        break;
    case HWID_MAX77932:
        max77932_hardware_config();
        break;
    case HWID_MAX77938:
        max77938_hardware_config();
        break;
    case HWID_SD77313:
        sd77313_hardware_config();
        break;
    default:
        chg_err("No charge pump chip hwid matched!!!\n");
        break;
    }

    return;
}

/**********************************************************
  *
  *   [platform_driver API]
  *
  *********************************************************/
static const struct of_device_id charge_pump_match[] = {
    { .compatible = "oplus,charge_pump-divider"},
    { },
};

static const struct i2c_device_id charge_pump_id[] = {
    {"charge_pump-divider", 0},
    {},
};
MODULE_DEVICE_TABLE(i2c, charge_pump_id);

static struct i2c_driver charge_pump_i2c_driver = {
    .driver		= {
        .name = "charge_pump-divider",
        .owner	= THIS_MODULE,
        .of_match_table = charge_pump_match,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0))
        .pm = &charge_pump_pm_ops,
#endif
    },
    .probe		= charge_pump_driver_probe,
    .remove		= charge_pump_driver_remove,
    .id_table	= charge_pump_id,
#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 4, 0))
    .resume         = charge_pump_resume,
    .suspend        = charge_pump_suspend,
#endif
    .shutdown	= charge_pump_shutdown,
};

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0))
module_i2c_driver(charge_pump_i2c_driver);
#else
int charge_pump_driver_init(void)
{
	int ret = 0;

	chg_debug(" start\n");

	if (i2c_add_driver(&charge_pump_i2c_driver) != 0) {
		chg_err(" failed to register charge pump i2c driver.\n");
	} else {
		chg_debug( " Success to register charge pump i2c driver.\n");
	}

	return ret;
}

void charge_pump_driver_exit(void)
{
	i2c_del_driver(&charge_pump_i2c_driver);
}

#endif /*LINUX_VERSION_CODE < KERNEL_VERSION(5, 4, 0)*/

MODULE_DESCRIPTION("Driver for charge_pump divider chip");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("i2c:charge_pump-divider");
