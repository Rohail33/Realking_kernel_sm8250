/*
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/of_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/backlight.h>

#include "ktz8866_init.h"

#define BL_I2C_ADDRESS			  0x11


#define LCD_ID_NAME "ktz8866"

/*****************************************************************************
 * GLobal Variable
 *****************************************************************************/
static struct i2c_client *g_i2c_client = NULL;
static struct ktz8866_gpio_info gpio_arrays[32];
static DEFINE_MUTEX(read_lock);

/*****************************************************************************
 * Extern Area
 *****************************************************************************/

static int lcd_bl_write_byte(unsigned char addr, unsigned char value)
{
    int ret = 0;
    unsigned char write_data[2] = {0};

    write_data[0] = addr;
    write_data[1] = value;

    if (NULL == g_i2c_client) {
	pr_err("[LCD][BL] g_i2c_client is null!!\n");
	return -EINVAL;
    }
    ret = i2c_master_send(g_i2c_client, write_data, 2);

    if (ret < 0)
	pr_err("[LCD][BL] i2c write data fail !!\n");

    return ret;
}
int lcd_bl_enable(bool enable)
{
	if (enable) {
		usleep_range(30000,30001);/* keep mipi on-bl on 40ms,now 42ms */
		lcd_bl_write_byte(0x02, 0xD3);/* BL_CFG1 OVP=34V Exponential dimming PWM Enable */
		lcd_bl_write_byte(0x03, 0xCD);/* diming 256ms PWM_HYST 10 LSBs*/
		lcd_bl_write_byte(0x11, 0xB7);/* BL_OPTION2 10uH BL_CURRENT_LIMIT 2.5A*/
		lcd_bl_write_byte(0x15, 0xC1);/* Backlight Full-scale LED Current 24.4mA/ PWM transent time 4ms*/
		lcd_bl_write_byte(0x08, 0x5F);/* BL enabled and Current sink 1/2/3/4 /5 enabled£»*/
	} else {
		lcd_bl_write_byte(0x08, 0x00);/* Disable BL */
	}
	return 0;
}

extern unsigned int dis_set_first_level;
int lcd_bl_set_led_brightness(int value)//for set bringhtness
{
	unsigned int mapping_value;
	if (value < 0) {
		pr_err("%d %s --lyd invalid value=%d\n", __LINE__, __func__, value);
		return 0;
	}
	mapping_value = backlight_buf[value];
	pr_info("%s:hyper bl = %d, mapping value = %d\n", __func__, value, mapping_value);

	if (value > 0) {
		if(dis_set_first_level) {
			lcd_bl_enable(true); /* BL enabled and Current sink 1/2/3/4/5 enabled */
			dis_set_first_level = 0;
			pr_info("%d %s dis_set_first_level=%d\n", __LINE__, __func__, dis_set_first_level);
		}
		lcd_bl_write_byte(0x04, mapping_value & 0x07); /* lsb */
		lcd_bl_write_byte(0x05, (mapping_value >> 3) & 0xFF); /* msb */
	}

	if (value == 0) {
		lcd_bl_write_byte(0x04, 0x00);// lsb
		lcd_bl_write_byte(0x05, 0x00);// msb
		lcd_bl_enable(false); /* BL disabled and Current sink 1/2/3/4/5 disabled */
	}
	return 0;
}
EXPORT_SYMBOL(lcd_bl_set_led_brightness);

int lcd_set_bias(bool enable)
{
	pr_info("--lcd, enter lcd_disable_bias function,value = %d", enable);
	if (enable) {
		lcd_bl_write_byte(0x0C, 0x2C);/* LCD_BOOST_CFG 6.2V*/
		lcd_bl_write_byte(0x0D, 0x24);/* OUTP_CFG OUTP = 5.8V */
		lcd_bl_write_byte(0x0E, 0x24);/* OUTN_CFG OUTN = -5.8V */
		lcd_bl_write_byte(0x09, 0x9C);/* enable OUTP */
	    	usleep_range(5000,5001);
		lcd_bl_write_byte(0x09, 0x9E);/* enable OUTN */
		usleep_range(13000,13001);/* avee-reset >10ms */
	} else {
		lcd_bl_write_byte(0x09, 0x9C);/* Disable OUTN */
		usleep_range(5000,5001);
		lcd_bl_write_byte(0x09, 0x98);/* Disable OUTP */
		usleep_range(50000,50001);
	}
	return 0;
}
EXPORT_SYMBOL(lcd_set_bias);

static const struct of_device_id lcd_ktz8866_of_match[] = {
    { .compatible = "ktz,ktz8866", },
    {},
};

static int ktz8866_pwr_parse_gpios(struct device *dev)
{
	int rc = 0;
 	int index = 0;
	struct device_node *np = dev->of_node;
	gpio_arrays[KTZ8866_HW_EN].gpio_name = "ktz8866_hw_en";

	for(index = 0; index < LCD_PWR_GPIO_MAX; index++) {
		gpio_arrays[index].gpio_num = of_get_named_gpio(np, gpio_arrays[index].gpio_name, 0);
		if (!gpio_is_valid(gpio_arrays[index].gpio_num)) {
			rc = gpio_arrays[index].gpio_num;
			pr_err("[%s] failed get gpio from dts, rc=%d\n", __func__,rc);
		} else {
			rc = gpio_request(gpio_arrays[index].gpio_num, gpio_arrays[index].gpio_name);
			if(rc) {
				pr_err("request %s gpio %d failed,rc = %d\n", gpio_arrays[index].gpio_name, gpio_arrays[index].gpio_num,rc);
			} else {
				pr_err("names:%s gpio %d requested success!\n", gpio_arrays[index].gpio_name, gpio_arrays[index].gpio_num);
			}
		}
	}
	return rc;
}

int turn_on_ktz8866_hw_en(bool on)
{
	int ret;
	u8 value  = on ? 1 : 0;

	if(gpio_is_valid(gpio_arrays[KTZ8866_HW_EN].gpio_num)) {
		ret = gpio_direction_output(gpio_arrays[KTZ8866_HW_EN].gpio_num, value);
		if(ret){
			pr_err("failed to set %s gpio %d, ret = %d\n", gpio_arrays[KTZ8866_HW_EN].gpio_name, value, ret);
			return ret;
		}
	} else {
		pr_err("KTZ8866_HW_EN is not vaild,KTZ8866_HW_EN is %d\n", gpio_arrays[KTZ8866_HW_EN].gpio_num);
	}

	return 0;
}
EXPORT_SYMBOL(turn_on_ktz8866_hw_en);

static int lcd_power_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int ret;

	if(!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_warn(&client->dev, "xx adapter does not support I2C\n");
		return -EIO;
	} else {
		g_i2c_client = client;
	}

	//parse dts
	if (client->dev.of_node) {
		ret = ktz8866_pwr_parse_gpios(&client->dev);
		if(ret){
			dev_err(&client->dev,"failed to parse dts\n");
			return -EINVAL;
		}

	}
	//turn on hw_en
	ret = turn_on_ktz8866_hw_en(true);
	if(ret) {
		pr_err("failed to turn on hwen!\n");
	}
    return 0;
}

static int lcd_power_remove(struct i2c_client *client)
{
    g_i2c_client = NULL;
    i2c_unregister_device(client);

    return 0;
}

/************************************************************
Attention:
Althouh i2c_bus do not use .id_table to match, but it must be defined,
otherwise the probe function will not be executed!
************************************************************/
static struct i2c_driver lcd_ktz8866_i2c_driver = {
    .probe = lcd_power_probe,
    .remove = lcd_power_remove,
    .driver = {
	.owner = THIS_MODULE,
	.name = LCD_ID_NAME,
	.of_match_table = lcd_ktz8866_of_match,

    },
};


static int __init lcd_ktz8866_init(void)
{
	pr_info("lcd_ktz8866_init\n");

	if (i2c_add_driver(&lcd_ktz8866_i2c_driver)) {
		pr_err("[LCD][BL] Failed to register lcd_ktz8866_i2c_driver!\n");
		return -EINVAL;
	}

    return 0;
}

static void __exit lcd_ktz8866_exit(void)
{
    i2c_del_driver(&lcd_ktz8866_i2c_driver);
}

subsys_initcall(lcd_ktz8866_init);
module_exit(lcd_ktz8866_exit);

MODULE_AUTHOR("<xx>");
MODULE_DESCRIPTION("QCOM ktz8866 Driver");
MODULE_LICENSE("GPL");

