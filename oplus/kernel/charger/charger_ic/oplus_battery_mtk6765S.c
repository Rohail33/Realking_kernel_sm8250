/*
 * SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/init.h>		/* For init/exit macros */
#include <linux/module.h>	/* For MODULE_ marcros  */
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/kdev_t.h>
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/power_supply.h>
#include <linux/pm_wakeup.h>
#include <linux/time.h>
#include <linux/mutex.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/scatterlist.h>
#include <linux/suspend.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/reboot.h>
#include <linux/alarmtimer.h>
#include <mtk_musb.h>
#ifdef OPLUS_FEATURE_CHG_BASIC
#ifndef CONFIG_TCPC_CLASS
#include <linux/regmap.h>
#include <linux/of_platform.h>
#include <linux/mfd/mt6397/core.h>/* PMIC MFD core header */
#endif
#endif

#include <mt-plat/mtk_boot.h>
#include <tcpm.h>

#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include "../oplus_charger.h"
#include "../oplus_gauge.h"
#include "../oplus_vooc.h"
#include "../oplus_configfs.h"
#include "op_charge.h"
#include "../../../misc/mediatek/typec/tcpc/inc/tcpci.h"
#include <linux/iio/consumer.h>
#ifndef CONFIG_TCPC_CLASS
#include "../../../misc/mediatek/extcon/extcon-mtk-usb.h"
#endif

#ifdef OPLUS_FEATURE_CHG_BASIC
#ifndef CONFIG_TCPC_CLASS
#define MT6357_VS1_ANA_CON2 0x1736
#define PMIC_RG_VS1_MODESET_MASK 0x1
#define PMIC_RG_VS1_MODESET_SHIFT 8
#define FORCE_PWM 1
#define AUTO_MODE 0
#endif
#endif

extern void mt_usb_connect_v1(void);
extern void mt_usb_disconnect_v1(void);
extern unsigned int is_project(int project);
extern int charger_ic_flag;

extern bool oplus_tchg_01c_precision(void);

static bool is_mtksvooc_project = false;

extern bool oplus_chg_check_chip_is_null(void);
extern bool oplus_chg_check_chip_is_null(void);

/************ kpoc_charger *******************/
extern int oplus_chg_get_ui_soc(void);
extern int oplus_chg_get_notify_flag(void);
extern int oplus_chg_show_vooc_logo_ornot(void);
extern int mt6357_get_vbus_voltage(void);
extern bool mt6357_get_vbus_status(void);
/*extern bool pmic_chrdet_status(void);*/
extern int oplus_get_prop_status(void);
struct oplus_chg_chip *g_oplus_chip = NULL;
int oplus_usb_switch_gpio_gpio_init(void);
static bool oplus_ship_check_is_gpio(struct oplus_chg_chip *chip);
int oplus_ship_gpio_init(struct oplus_chg_chip *chip);
static bool is_usbtemp_thread_init_done = false;
void smbchg_enter_shipmode(struct oplus_chg_chip *chip);
bool oplus_shortc_check_is_gpio(struct oplus_chg_chip *chip);
int oplus_shortc_gpio_init(struct oplus_chg_chip *chip);

extern int oplus_get_usb_status(void);
#ifdef CONFIG_CHARGER_BQ2589X
extern struct oplus_chg_operations  oplus_chg_bq2589x_ops;
#endif
extern struct oplus_chg_operations  oplus_chg_bq2560x_ops;
#ifdef CONFIG_CHARGER_SY6970
extern struct oplus_chg_operations  oplus_chg_sy6970_ops;
#endif
struct iio_channel *usb_chan1; /*usb_temp_auxadc_channel 1*/
struct iio_channel *usb_chan2; /*usb_temp_auxadc_channel 2*/
static int usbtemp_log_control = 0;
static int usb_debug_temp = 65535;
static int charger_ic__det_flag = 0;
int ap_temp_debug = 65535;
module_param(usb_debug_temp, int, 0644);
module_param(ap_temp_debug, int, 0644);
module_param(usbtemp_log_control, int, 0644);
#define USBTEMP_LOG_PRINT(fmt, args...)					\
do {								\
	if (usbtemp_log_control) {	\
		pr_debug(fmt, ##args);				\
	}							\
} while (0)

static struct alarm usbotp_recover_timer;
static struct mtk_charger *pinfo;
static struct list_head consumer_head = LIST_HEAD_INIT(consumer_head);
static DEFINE_MUTEX(consumer_mutex);

extern void oplus_voocphy_set_pdqc_config(void);

extern struct oplus_chg_operations * oplus_get_chg_ops(void);
extern int oplus_usbtemp_monitor_common(void *data);
extern void oplus_usbtemp_recover_func(struct oplus_chg_chip *chip);

extern enum alarmtimer_restart usbtemp_alarm_timer_func(struct alarm *alarm, ktime_t now);

extern bool oplus_gauge_ic_chip_is_null(void);
extern bool oplus_vooc_check_chip_is_null(void);
extern bool oplus_adapter_check_chip_is_null(void);
int oplus_tbatt_power_off_task_init(struct oplus_chg_chip *chip);

extern int oplus_get_typec_cc_orientation(void);
static struct task_struct *oplus_usbtemp_kthread;
static DECLARE_WAIT_QUEUE_HEAD(oplus_usbtemp_wq);
void oplus_set_otg_switch_status(bool value);
void oplus_wake_up_usbtemp_thread(void);
extern void oplus_chg_turn_off_charging(struct oplus_chg_chip *chip);
/* ==================================================================== */
#ifdef OPLUS_FEATURE_CHG_BASIC
bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip);
int oplus_ccdetect_gpio_init(struct oplus_chg_chip *chip);
void oplus_ccdetect_irq_init(struct oplus_chg_chip *chip);
bool oplus_ccdetect_support_check(void);
int oplus_chg_ccdetect_parse_dt(struct oplus_chg_chip *chip);
#endif /* OPLUS_FEATURE_CHG_BASIC */

#ifdef OPLUS_FEATURE_CHG_BASIC
void oplus_set_typec_cc_open(void);
void oplus_usbtemp_recover_cc_open(void);
#endif /* OPLUS_FEATURE_CHG_BASIC */

#define USB_TEMP_HIGH		0x01       /*bit0*/
#define USB_WATER_DETECT	0x02       /*bit1*/
#define USB_RESERVE2		0x04       /*bit2*/
#define USB_RESERVE3		0x08       /*bit3*/
#define USB_RESERVE4		0x10       /*bit4*/
#define USB_DONOT_USE		0x80000000 /*bit31*/

#define CHARGER_25C_VOLT	2457//mv
#define CHARGER_DEFAULT_TEMP	25
extern bool is_kthread_get_adc(void);
extern int get_charger_ntc_volt(void);
extern int get_bat_con_ntc_volt(void);

int oplus_get_usb_status(void)
{
	printk("%s: usb_status:[%d]", __func__, g_oplus_chip->usb_status);
	return g_oplus_chip->usb_status;
}

int is_vooc30_cfg = false;
static bool is_vooc30_project(void)
{
	return is_vooc30_cfg;
}

void charger_ic_enable_ship_mode(struct oplus_chg_chip *chip)
{
	if (!chip->is_double_charger_support) {
		if (chip->chg_ops->enable_shipmode) {
			chip->chg_ops->enable_shipmode(true);
		}
	} else {
		if (chip->sub_chg_ops->enable_shipmode) {
			chip->sub_chg_ops->enable_shipmode(true);
		}
		if (chip->chg_ops->enable_shipmode) {
			chip->chg_ops->enable_shipmode(true);
		}
	}
}
/* ==================================================================== */
int oplus_battery_meter_get_battery_voltage(void)
{
	return g_oplus_chip->batt_volt;
}

bool is_vooc_project(void)
{
	return false;
}

bool meter_fg_30_get_battery_authenticate(void);
int charger_ic_flag = 1;
int oplus_which_charger_ic(void)
{
	return charger_ic_flag;
}

int get_charger_ic_det(struct oplus_chg_chip *chip)
{
	int count = 0;
	int n = charger_ic__det_flag;

	if (!chip) {
		return charger_ic__det_flag;
	}

	while (n) {
		++count;
		n = (n - 1) & n;
	}

	chip->is_double_charger_support = count > 1 ? true:false;
	chg_err("charger_ic__det_flag:%d\n", charger_ic__det_flag);
	return charger_ic__det_flag;
}

static struct temp_param chargeric_temp_table[] = {
	{-40, 4251000}, {-39, 3962000}, {-38, 3695000}, {-37, 3447000}, {-36, 3218000}, {-35, 3005000}, {-34, 2807000}, {-33, 2624000},
	{-32, 2454000}, {-31, 2296000}, {-30, 2149000}, {-29, 2012000}, {-28, 1885000}, {-27, 1767000}, {-26, 1656000}, {-25, 1554000},
	{-24, 1458000}, {-23, 1369000}, {-22, 1286000}, {-21, 1208000}, {-20, 1135000}, {-19, 1068000}, {-18, 1004000}, {-17,  945000},
	{-16,  889600}, {-15,  837800}, {-14,  789300}, {-13,  743900}, {-12,  701300}, {-11,  661500}, {-10,  624100}, {-9,   589000},
	{-8,   556200}, {-7,   525300}, {-6,   496300}, {-5,   469100}, {-4,   443500}, {-3,   419500}, {-2,   396900}, {-1,   375600},
	{0,    355600}, {1,    336800}, {2,    319100}, {3,    302400}, {4,    286700}, {5,    271800}, {6,    257800}, {7,    244700},
	{8,    232200}, {9,    220500}, {10,   209400}, {11,   198900}, {12,   189000}, {13,   179700}, {14,   170900}, {15,   162500},
	{16,   154600}, {17,   147200}, {18,   140100}, {19,   133400}, {20,   127000}, {21,   121000}, {22,   115400}, {23,   110000},
	{24,   104800}, {25,   100000}, {26,   95400 }, {27,   91040 }, {28,   86900 }, {29,   82970 }, {30,   79230 }, {31,   75690 },
	{32,   72320 }, {33,   69120 }, {34,   66070 }, {35,   63180 }, {36,   60420 }, {37,   57810 }, {38,   55310 }, {39,   52940 },
	{40,   50680 }, {41,   48530 }, {42,   46490 }, {43,   44530 }, {44,   42670 }, {45,   40900 }, {46,   39210 }, {47,   37600 },
	{48,   36060 }, {49,   34595 }, {50,   33195 }, {51,   31859 }, {52,   30584 }, {53,   29366 }, {54,   28203 }, {55,   27091 },
	{56,   26028 }, {57,   25013 }, {58,   24042 }, {59,   23113 }, {60,   22224 }, {61,   21374 }, {62,   20561 }, {63,   19782 },
	{64,   19036 }, {65,   18323 }, {66,   17640 }, {67,   16986 }, {68,   16360 }, {69,   15760 }, {70,   15184 }, {71,   14631 },
	{72,   14101 }, {73,   13592 }, {74,   13104 }, {75,   12635 }, {76,   12187 }, {77,   11757 }, {78,   11344 }, {79,   10947 },
	{80,   10566 }, {81,   10200 }, {82,     9848}, {83,     9510}, {84,     9185}, {85,     8873}, {86,     8572}, {87,     8283},
	{88,     8006}, {89,     7738}, {90,     7481}, {91,     7234}, {92,     6997}, {93,     6769}, {94,     6548}, {95,     6337},
	{96,     6132}, {97,     5934}, {98,     5744}, {99,     5561}, {100,    5384}, {101,    5214}, {102,    5051}, {103,    4893},
	{104,    4769}, {105,    4623}, {106,    4482}, {107,    4346}, {108,    4215}, {109,    4088}, {110,    3966}, {111,    3848},
	{112,    3734}, {113,    3624}, {114,    3518}, {115,    3415}, {116,    3316}, {117,    3220}, {118,    3128}, {119,    3038},
	{120,    2952}, {121,    2868}, {122,    2787}, {123,    2709}, {124,    2634}, {125,    2561}
};

static struct temp_param chargeric_temp_table_390k[] = {
	{-40, 4251000}, {-39, 3962000}, {-38, 3695000}, {-37, 3447000}, {-36, 3218000}, {-35, 3005000},
	{-34, 2807000}, {-33, 2624000}, {-32, 2454000}, {-31, 2296000}, {-30, 2149000}, {-29, 2012000},
	{-28, 1885000}, {-27, 1767000}, {-26, 1656000}, {-25, 1554000}, {-24, 1458000}, {-23, 1369000},
	{-22, 1286000}, {-21, 1208000}, {-20, 1135000}, {-19, 1068000}, {-18, 1004000}, {-17, 945000 },
	{-16, 889600 }, {-15, 837800 }, {-14, 789300 }, {-13, 743900 }, {-12, 701300 }, {-11, 661500 },
	{-10, 624100 }, {-9, 589000  }, {-8, 556200  }, {-7, 525300  }, {-6, 496300  }, {-5, 469100  },
	{-4, 443500  }, {-3, 419500  }, {-2, 396900  }, {-1, 375600  }, {0, 355600   }, {1, 336800   },
	{2, 319100   }, {3, 302400   }, {4, 286700   }, {5, 271800   }, {6, 257800   }, {7, 244700   },
	{8, 232200   }, {9, 220500   }, {10, 209400  }, {11, 198900  }, {12, 189000  }, {13, 179700  },
	{14, 170900  }, {15, 162500  }, {16, 154600  }, {17, 147200  }, {18, 140100  }, {19, 133400  },
	{20, 127000  }, {21, 121000  }, {22, 115400  }, {23, 110000  }, {24, 104800  }, {25, 100000  },
	{26, 95400   }, {27, 91040   }, {28, 86900   }, {29, 82970   }, {30, 79230   }, {31, 75690   },
	{32, 72320   }, {33, 69120   }, {34, 66070   }, {35, 63180   }, {36, 60420   }, {37, 57810   },
	{38, 55310   }, {39, 52940   }, {40, 50680   }, {41, 48530   }, {42, 46490   }, {43, 44530   },
	{44, 42670   }, {45, 40900   }, {46, 39210   }, {47, 37600   }, {48, 36060   }, {49, 34600   },
	{50, 33190   }, {51, 31860   }, {52, 30580   }, {53, 29360   }, {54, 28200   }, {55, 27090   },
	{56, 26030   }, {57, 25010   }, {58, 24040   }, {59, 23110   }, {60, 22220   }, {61, 21370   },
	{62, 20560   }, {63, 19780   }, {64, 19040   }, {65, 18320   }, {66, 17640   }, {67, 16990   },
	{68, 16360   }, {69, 15760   }, {70, 15180   }, {71, 14630   }, {72, 14100   }, {73, 13600   },
	{74, 13110   }, {75, 12640   }, {76, 12190   }, {77, 11760   }, {78, 11350   }, {79, 10960   },
	{80, 10580   }, {81, 10210   }, {82, 9859    }, {83, 9522    }, {84, 9198    }, {85, 8887    },
	{86, 8587    }, {87, 8299    }, {88, 8022    }, {89, 7756    }, {90, 7500    }, {91, 7254    },
	{92, 7016    }, {93, 6788    }, {94, 6568    }, {95, 6357    }, {96, 6153    }, {97, 5957    },
	{98, 5768    }, {99, 5586    }, {100, 5410   }, {101, 5241   }, {102, 5078   }, {103, 4921   },
	{104, 4769   }, {105, 4623   }, {106, 4482   }, {107, 4346   }, {108, 4215   }, {109, 4088   },
	{110, 3965   }, {111, 3847   }, {112, 3733   }, {113, 3623   }, {114, 3517   }, {115, 3415   },
	{116, 3315   }, {117, 3220   }, {118, 3127   }, {119, 3038   }, {120, 2951   }, {121, 2868   },
	{122, 2787   }, {123, 2709   }, {124, 2633   }, {125, 2560   }
};

#define OPLUS_TAP_VALUE 		-2000
#define OPLUS_TCHG_01C_PRECISION	10
static __s16 oplus_ch_thermistor_conver_temp(__s32 res, struct ntc_temp *ntc_param)
{
	int i = 0;
	int asize = 0;
	__s32 res1 = 0, res2 = 0;
	__s32 tap_value = OPLUS_TAP_VALUE, tmp1 = 0, tmp2 = 0;

	if (!ntc_param)
		return tap_value;

	asize = ntc_param->i_table_size;

	if (res >= ntc_param->pst_temp_table[0].temperature_r) {
		tap_value = ntc_param->i_tap_min;	/* min */
	} else if (res <= ntc_param->pst_temp_table[asize - 1].temperature_r) {
		tap_value = ntc_param->i_tap_max;	/* max */
	} else {
		res1 = ntc_param->pst_temp_table[0].temperature_r;
		tmp1 = ntc_param->pst_temp_table[0].bts_temp;

		for (i = 0; i < asize; i++) {
			if (res >= ntc_param->pst_temp_table[i].temperature_r) {
				res2 = ntc_param->pst_temp_table[i].temperature_r;
				tmp2 = ntc_param->pst_temp_table[i].bts_temp;
				break;
			}
			res1 = ntc_param->pst_temp_table[i].temperature_r;
			tmp1 = ntc_param->pst_temp_table[i].bts_temp;
		}

		tap_value = (((res - res2) * tmp1) + ((res1 - res) * tmp2)) * OPLUS_TCHG_01C_PRECISION / (res1 - res2);
	}

	return tap_value;
}

#define OPLUS_CHG_TMP -100
static __s16 oplus_res_to_temp(struct ntc_temp *ntc_param)
{
	__s32 tres;
	__u64 dwvcrich = 0;
	__s32 chg_tmp = OPLUS_CHG_TMP;
	__u64 dwvcrich2 = 0;

	if (!ntc_param) {
		return CHARGER_DEFAULT_TEMP;
	}
	dwvcrich = ((__u64)ntc_param->i_tap_over_critical_low * (__u64)ntc_param->i_rap_pull_up_voltage);
	dwvcrich2 = (ntc_param->i_tap_over_critical_low + ntc_param->i_rap_pull_up_r);
	do_div(dwvcrich, dwvcrich2);

	if (ntc_param->ui_dwvolt > ((__u32)dwvcrich)) {
		tres = ntc_param->i_tap_over_critical_low;
	} else {
		tres = (ntc_param->i_rap_pull_up_r * ntc_param->ui_dwvolt) / (ntc_param->i_rap_pull_up_voltage - ntc_param->ui_dwvolt);
	}

	printk("[oplus_res_to_temp] tres = %d!\n", tres);

	/* convert register to temperature */
	chg_tmp = oplus_ch_thermistor_conver_temp(tres, ntc_param);

	return chg_tmp;
}

static int oplus_get_temp_volt(struct ntc_temp *ntc_param)
{
	int rc = 0;
	int ntc_temp_volt = 0;
	if (!ntc_param) {
		return -1;
	}

	switch (ntc_param->e_ntc_type) {
	case NTC_SUB_BOARD:
		break;
	case NTC_CHARGER_IC:
		if (is_kthread_get_adc()) {
			 ntc_temp_volt = get_charger_ntc_volt();
		}
		break;
	case NTC_BATTERY_CON:
                if (is_kthread_get_adc()) {
                         ntc_temp_volt = get_bat_con_ntc_volt();
                }
                break;
	default:
		break;
	}

	if (ntc_temp_volt <= 0) {
		ntc_temp_volt = ntc_param->i_25c_volt;
	}

	chg_err("ntc_temp_volt:%d\n", ntc_temp_volt);

	return ntc_temp_volt;
}

#define DEFAULT_PULL_UP_R0      			100000 /*pull up 100K*/
#define PULL_UP_R0_390K         			390000 /*Pull up 390K*/
#define DEFAULT_BATCON_I_TAP_OVER_CRITICAL_LOW		425100
#define DEFAULT_CHARGERIC_I_TAP_OVER_CRITICAL_LOW	DEFAULT_BATCON_I_TAP_OVER_CRITICAL_LOW
#define DEFAULT_I_RAP_UP_VOLT				1800
#define DEFAULT_I_TAP_MIN				-400
#define DEFAULT_I_TAP_MAX				1250
#define DEFAULT_BATCON_I_25C_VOLT			3673
static int g_rap_pull_up_r0 = PULL_UP_R0_390K;/*pull_up_r */
static int get_batcon_temp(void)
{
        int batt_btb_temp = CHARGER_DEFAULT_TEMP;
        static bool is_param_init = false;
        static struct ntc_temp ntc_param = {0};

        if (!is_param_init) {
                ntc_param.e_ntc_type = NTC_BATTERY_CON;
                ntc_param.i_tap_over_critical_low = DEFAULT_BATCON_I_TAP_OVER_CRITICAL_LOW;
                ntc_param.i_rap_pull_up_r = g_rap_pull_up_r0;
                ntc_param.i_rap_pull_up_voltage = DEFAULT_I_RAP_UP_VOLT;
                ntc_param.i_tap_min = DEFAULT_I_TAP_MIN;
                ntc_param.i_tap_max = DEFAULT_I_TAP_MAX;
                ntc_param.i_25c_volt = DEFAULT_BATCON_I_25C_VOLT;
                if(g_rap_pull_up_r0 == PULL_UP_R0_390K) {
                        ntc_param.pst_temp_table = chargeric_temp_table_390k;
                        ntc_param.i_table_size = (sizeof(chargeric_temp_table_390k) / sizeof(struct temp_param));
                } else {
                        ntc_param.pst_temp_table = chargeric_temp_table;
                        ntc_param.i_table_size = (sizeof(chargeric_temp_table) / sizeof(struct temp_param));
                }
                is_param_init = true;
                chg_err("ntc_type:%d,critical_low:%d,pull_up_r=%d,pull_up_voltage=%d,tap_min=%d,tap_max=%d,table_size=%d\n",
                        ntc_param.e_ntc_type, ntc_param.i_tap_over_critical_low, ntc_param.i_rap_pull_up_r,
                        ntc_param.i_rap_pull_up_voltage, ntc_param.i_tap_min, ntc_param.i_tap_max, ntc_param.i_table_size);
        }

        ntc_param.ui_dwvolt = oplus_get_temp_volt(&ntc_param);
        batt_btb_temp = oplus_res_to_temp(&ntc_param);

        chg_err("batt_btb_temp:%d\n", batt_btb_temp);
        return batt_btb_temp;
}

static int get_chargeric_temp(void)
{
        int chargeric_temp = CHARGER_DEFAULT_TEMP;
        static bool is_param_init = false;
        static struct ntc_temp ntc_param = {0};

        if (!is_param_init) {
                ntc_param.e_ntc_type = NTC_CHARGER_IC;
                ntc_param.i_tap_over_critical_low = DEFAULT_CHARGERIC_I_TAP_OVER_CRITICAL_LOW;
                ntc_param.i_rap_pull_up_r = g_rap_pull_up_r0;
                ntc_param.i_rap_pull_up_voltage = DEFAULT_I_RAP_UP_VOLT;
                ntc_param.i_tap_min = DEFAULT_I_TAP_MIN;
                ntc_param.i_tap_max = DEFAULT_I_TAP_MAX;
                ntc_param.i_25c_volt = DEFAULT_BATCON_I_25C_VOLT;
                if(g_rap_pull_up_r0 == PULL_UP_R0_390K) {
                        ntc_param.pst_temp_table = chargeric_temp_table_390k;
                        ntc_param.i_table_size = (sizeof(chargeric_temp_table_390k) / sizeof(struct temp_param));
                } else {
                        ntc_param.pst_temp_table = chargeric_temp_table;
                        ntc_param.i_table_size = (sizeof(chargeric_temp_table) / sizeof(struct temp_param));
                }
                is_param_init = true;
                chg_err("ntc_type:%d,critical_low:%d,pull_up_r=%d,pull_up_voltage=%d,tap_min=%d,tap_max=%d,table_size=%d\n",
                        ntc_param.e_ntc_type, ntc_param.i_tap_over_critical_low, ntc_param.i_rap_pull_up_r,
                        ntc_param.i_rap_pull_up_voltage, ntc_param.i_tap_min, ntc_param.i_tap_max, ntc_param.i_table_size);
        }

        ntc_param.ui_dwvolt = oplus_get_temp_volt(&ntc_param);
        chargeric_temp = oplus_res_to_temp(&ntc_param);

        chg_err("chargeric_temp:%d\n", chargeric_temp);
        return chargeric_temp;
}

int oplus_chg_get_battery_btb_temp_cal(void)
{
	return get_batcon_temp() / OPLUS_TCHG_01C_PRECISION;
}
EXPORT_SYMBOL(oplus_chg_get_battery_btb_temp_cal);

int oplus_chg_get_usb_btb_temp_cal(void)
{
	return get_chargeric_temp() / OPLUS_TCHG_01C_PRECISION;
}
EXPORT_SYMBOL(oplus_chg_get_usb_btb_temp_cal);

void set_charger_ic(int sel)
{
	charger_ic__det_flag |= 1 << sel;
}
EXPORT_SYMBOL(set_charger_ic);

static int mtk_chgstat_notify(struct mtk_charger *info)
{
	int ret = 0;
	char *env[2] = { "CHGSTAT=1", NULL };

	chg_err("%s: 0x%x\n", __func__, info->notify_code);
	ret = kobject_uevent_env(&info->pdev->dev.kobj, KOBJ_CHANGE, env);
	if (ret)
		chg_err("%s: kobject_uevent_fail, ret=%d", __func__, ret);

	return ret;
}

static int oplus_chg_parse_custom_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip)
		node = chip->dev->of_node;
	if (!node) {
			pr_err("device tree node missing\n");
			return -EINVAL;
	}

	if (chip) {
		chip->normalchg_gpio.chargerid_switch_gpio =
				of_get_named_gpio(node, "qcom,chargerid_switch-gpio", 0);
		if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
			chg_err("Couldn't read chargerid_switch-gpio rc = %d, chargerid_switch_gpio:%d\n",
					rc, chip->normalchg_gpio.chargerid_switch_gpio);
		} else {
			if (gpio_is_valid(chip->normalchg_gpio.chargerid_switch_gpio)) {
				rc = gpio_request(chip->normalchg_gpio.chargerid_switch_gpio, "charging-switch1-gpio");
				if (rc) {
					chg_err("unable to request chargerid_switch_gpio:%d\n", chip->normalchg_gpio.chargerid_switch_gpio);
				} else {
					oplus_usb_switch_gpio_gpio_init();
				}
			}
			chg_err("chargerid_switch_gpio:%d\n", chip->normalchg_gpio.chargerid_switch_gpio);
		}
	}

	if (chip) {
		chip->disable_ship_mode = of_property_read_bool(node, "qcom,disable-ship-mode");
		chg_err("disable_ship_mode:%d!!!\n", chip->disable_ship_mode);
		chip->normalchg_gpio.ship_gpio =
				of_get_named_gpio(node, "qcom,ship-gpio", 0);
		if (chip->normalchg_gpio.ship_gpio <= 0) {
			chg_err("Couldn't read qcom,ship-gpio rc = %d, qcom,ship-gpio:%d\n",
					rc, chip->normalchg_gpio.ship_gpio);
		} else {
			if (oplus_ship_check_is_gpio(chip) == true) {
				rc = gpio_request(chip->normalchg_gpio.ship_gpio, "ship-gpio");
				if (rc) {
					chg_err("unable to request ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
				} else {
					oplus_ship_gpio_init(chip);
					if (rc)
						chg_err("unable to init ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
				}
			}
			chg_err("ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
		}
	}

	if (chip) {
		chip->normalchg_gpio.shortc_gpio =
				of_get_named_gpio(node, "qcom,shortc-gpio", 0);
		if (chip->normalchg_gpio.shortc_gpio <= 0) {
			chg_err("Couldn't read qcom,shortc-gpio rc = %d, qcom,shortc-gpio:%d\n",
					rc, chip->normalchg_gpio.shortc_gpio);
		} else {
			if (oplus_shortc_check_is_gpio(chip) == true) {
				rc = gpio_request(chip->normalchg_gpio.shortc_gpio, "shortc-gpio");
				if (rc) {
					chg_err("unable to request shortc-gpio:%d\n", chip->normalchg_gpio.shortc_gpio);
				} else {
					oplus_shortc_gpio_init(chip);
					if (rc)
						chg_err("unable to init ship-gpio:%d\n", chip->normalchg_gpio.ship_gpio);
				}
			}
			chg_err("shortc-gpio:%d\n", chip->normalchg_gpio.shortc_gpio);
		}
	}

	rc = oplus_chg_ccdetect_parse_dt(chip);
	if (rc) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chg_ccdetect_parse_dt fail!\n", __func__);
	}
	return rc;
}

static bool oplus_ship_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (gpio_is_valid(chip->normalchg_gpio.ship_gpio))
		return true;

	return false;
}

int oplus_ship_gpio_init(struct oplus_chg_chip *chip)
{
	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	chip->normalchg_gpio.ship_active =
		pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
			"ship_active");

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active)) {
		chg_err("get ship_active fail\n");
		return -EINVAL;
	}
	chip->normalchg_gpio.ship_sleep =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
				"ship_sleep");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
		chg_err("get ship_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl,
		chip->normalchg_gpio.ship_sleep);
	return 0;
}
#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
int mtk_chg_enable_vbus_ovp(bool enable)
{
	return 0;
}
#endif

static ssize_t charger_log_level_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chg_err("%s: %d\n", __func__, pinfo->log_level);
	return sprintf(buf, "%d\n", pinfo->log_level);
}

static ssize_t charger_log_level_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp < 0) {
			chg_err("%s: val is invalid: %ld\n", __func__, temp);
			temp = 0;
		}
		pinfo->log_level = temp;
		chg_err("%s: log_level=%d\n", __func__, pinfo->log_level);

	} else {
		chg_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR_RW(charger_log_level);

static ssize_t BatteryNotify_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	pr_info("%s: 0x%x\n", __func__, pinfo->notify_code);

	return sprintf(buf, "%u\n", pinfo->notify_code);
}

static ssize_t BatteryNotify_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret = 0;

	if (buf != NULL && size != 0) {
		ret = kstrtouint(buf, 16, &reg);
		if (ret < 0) {
			chg_err("%s: failed, ret = %d\n", __func__, ret);
			return ret;
		}
		pinfo->notify_code = reg;
		pr_info("%s: store code=0x%x\n", __func__, pinfo->notify_code);
		mtk_chgstat_notify(pinfo);
	}
	return size;
}

static DEVICE_ATTR_RW(BatteryNotify);

static int mtk_charger_setup_files(struct platform_device *pdev)
{
	int ret = 0;

	ret = device_create_file(&(pdev->dev), &dev_attr_charger_log_level);
	if (ret)
		goto _out;

	/* Battery warning */
	ret = device_create_file(&(pdev->dev), &dev_attr_BatteryNotify);
	if (ret)
		goto _out;
_out:
	return ret;
}

#define SHIP_MODE_CONFIG		0x40
#define SHIP_MODE_MASK			BIT(0)
#define SHIP_MODE_ENABLE		0
#define PWM_COUNT				5
void smbchg_enter_shipmode(struct oplus_chg_chip *chip)
{
	int i = 0;
	chg_err("enter smbchg_enter_shipmode\n");

	if (oplus_ship_check_is_gpio(chip) == true) {
		chg_err("select gpio control\n");
		if (!IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active) && !IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
			pinctrl_select_state(chip->normalchg_gpio.pinctrl,
				chip->normalchg_gpio.ship_sleep);
			for (i = 0; i < PWM_COUNT; i++) {
				pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_active);
				mdelay(3);
				pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_sleep);
				mdelay(3);
			}
		}
		chg_err("power off after 15s\n");
	}
}
void enter_ship_mode_function(struct oplus_chg_chip *chip)
{
	if (chip != NULL) {
		if (chip->enable_shipmode && !chip->disable_ship_mode) {
			chg_err("enter_ship_mode_function\n");
			smbchg_enter_shipmode(chip);
			charger_ic_enable_ship_mode(chip);
		} else {
			chg_err("The program does not support ship_mode.\n");
		}
	}
}

static bool is_disable_charger(void)
{
	if (pinfo == NULL)
		return true;

	if (pinfo->disable_charger == true || IS_ENABLED(CONFIG_POWER_EXT))
		return true;
	else
		return false;
}

void BATTERY_SetUSBState(int usb_state_value)
{
	if (is_disable_charger()) {
		chg_err("[%s] in FPGA/EVB, no service\n", __func__);
	} else {
		if ((usb_state_value < USB_SUSPEND) ||
			((usb_state_value > USB_CONFIGURED))) {
			chg_err("%s Fail! Restore to default value\n",
				__func__);
			usb_state_value = USB_UNCONFIGURED;
		} else {
			chg_err("%s Success! Set %d\n", __func__,
				usb_state_value);
			if (pinfo)
				pinfo->usb_state = usb_state_value;
		}
	}
}

unsigned int set_chr_input_current_limit(int current_limit)
{
	return 500;
}

int get_chr_temperature(int *min_temp, int *max_temp)
{
	*min_temp = 25;
	*max_temp = 30;

	return 0;
}

int set_chr_boost_current_limit(unsigned int current_limit)
{
	return 0;
}

int set_chr_enable_otg(unsigned int enable)
{
	if (!g_oplus_chip) {
		printk(KERN_ERR "[oplus_CHG][%s]: otg [%s] failed.\n", __func__, enable?"Enable":"Disable");
		return -1;
	}

	if (enable) {
		g_oplus_chip->chg_ops->otg_enable();
	} else {
		g_oplus_chip->chg_ops->otg_disable();
	}
	return 0;
}

int mtk_chr_is_charger_exist(unsigned char *exist)
{
	if (mt_get_charger_type() == CHARGER_UNKNOWN)
		*exist = 0;
	else
		*exist = 1;
	return 0;
}

#ifdef CONFIG_TCPC_CLASS
void oplus_set_otg_switch_status(bool value)
{
	if (pinfo != NULL && pinfo->tcpc != NULL) {
		printk(KERN_ERR "[oplus_CHG][%s]: otg switch[%d]\n", __func__, value);
		tcpm_typec_change_role_postpone(pinfo->tcpc, value ? TYPEC_ROLE_TRY_SNK : TYPEC_ROLE_SNK, true);
	} else {
		printk(KERN_ERR "[oplus_CHG][%s]: tcpc device fail\n", __func__);
	}
}
EXPORT_SYMBOL(oplus_set_otg_switch_status);
#else
extern void otg_switch_mode(bool value);
void oplus_set_otg_switch_status(bool value)
{
	if (pinfo != NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: otg switch[%d]\n", __func__, value);
		otg_switch_mode(value);
	}
}
EXPORT_SYMBOL(oplus_set_otg_switch_status);
#endif

#define VBUS_9V	9000
#define VBUS_5V	5000
#define IBUS_2A	2000
#define IBUS_3A	3000
#define PD_CHG_9V2A_VOLT_THRESHOLD 6500
#define PD_CHG_5V3A_VOLT_THRESHOLD 7500
#define PD_CHG_BATT_VOLT_THRESHOLD 4450
#define PD_CHG_SOC_THRESHOLD 90
#define PD_SVOOC_CHG_TEMP_THRESHOLD 530
#define PD_CHG_TEMP_THRESHOLD 420
bool oplus_chg_get_pd_type(void)
{
	if (pinfo != NULL) {
		if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK ||
			pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30 ||
			pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO)
			return true;
		//return mtk_pdc_check_charger(pinfo);
	}
	return false;
}

int oplus_mt6765_pd_setup_forsvooc(void)
{
	int vbus_mv = VBUS_5V;
	int ibus_ma = IBUS_2A;
	int ret = -1;
	struct adapter_power_cap cap;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int i;

	if (chip->pd_svooc) {
		printk(KERN_ERR, "%s pd_svooc support\n", __func__);
		return 0;
	}

	cap.nr = 0;
	cap.pdp = 0;
	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
		cap.max_mv[i] = 0;
		cap.min_mv[i] = 0;
		cap.ma[i] = 0;
		cap.type[i] = 0;
		cap.pwr_limit[i] = 0;
	}

	printk(KERN_ERR "%s: pd_type %d pd9v svooc [%d %d %d]",
		__func__,
		pinfo->pd_type,
		chip->limits.vbatt_pdqc_to_9v_thr,
		chip->limits.vbatt_pdqc_to_5v_thr,
		chip->batt_volt);

	if (oplus_vooc_get_allow_reading() == true) {
		chip->charger_volt = chip->chg_ops->get_charger_volt();
	}

	if (!chip->calling_on && !chip->camera_on
		&& chip->charger_volt < PD_CHG_9V2A_VOLT_THRESHOLD
		&& chip->soc < PD_CHG_SOC_THRESHOLD
		&& chip->temperature <= PD_SVOOC_CHG_TEMP_THRESHOLD
		&& chip->cool_down_force_5v == false
		&& (chip->batt_volt < chip->limits.vbatt_pdqc_to_9v_thr)) {
		if (is_vooc_support_single_batt_svooc() == true) {
			vooc_enable_cp_for_pdqc();
		}
		if (oplus_chg_get_voocphy_support()) {
			oplus_voocphy_set_pdqc_config();
		}
		if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
			adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD_APDO, &cap);
			for (i = 0; i < cap.nr; i++) {
				printk(KERN_ERR "PD APDO cap %d: mV:%d,%d mA:%d type:%d pwr_limit:%d pdp:%d\n",
					i, cap.max_mv[i], cap.min_mv[i], cap.ma[i],
					cap.type[i], cap.pwr_limit[i], cap.pdp);
			}

			for (i = 0; i < cap.nr; i++) {
				if (cap.min_mv[i] <= VBUS_9V && VBUS_9V <= cap.max_mv[i]) {
					vbus_mv = VBUS_9V;
					ibus_ma = cap.ma[i];
					if (ibus_ma > IBUS_2A)
						ibus_ma = IBUS_2A;
					break;
				}
			}
		} else if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK
			|| pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30) {
			adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD, &cap);
			for (i = 0; i < cap.nr; i++) {
				printk(KERN_ERR "PD cap %d: mV:%d,%d mA:%d type:%d\n", i,
					cap.max_mv[i], cap.min_mv[i], cap.ma[i], cap.type[i]);
			}

			for (i = 0; i < cap.nr; i++) {
				if (VBUS_9V <= cap.max_mv[i]) {
					vbus_mv = cap.max_mv[i];
					ibus_ma = cap.ma[i];
					if (ibus_ma > IBUS_2A)
						ibus_ma = IBUS_2A;
					break;
				}
			}
		} else {
			vbus_mv = VBUS_5V;
			ibus_ma = IBUS_2A;
		}

		printk(KERN_ERR "PD request: %dmV, %dmA\n", vbus_mv, ibus_ma);
		ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
	} else {
		if (chip->charger_volt > PD_CHG_5V3A_VOLT_THRESHOLD
			&& (chip->calling_on
			|| chip->soc >= PD_CHG_SOC_THRESHOLD
			|| chip->camera_on
			|| chip->batt_volt >= chip->limits.vbatt_pdqc_to_5v_thr
			|| chip->temperature > PD_CHG_TEMP_THRESHOLD
			|| chip->cool_down_force_5v == true)) {
			vbus_mv = VBUS_5V;
			ibus_ma = IBUS_3A;

			printk(KERN_ERR "PD request: %dmV, %dmA\n", vbus_mv, ibus_ma);
			ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
		}

		printk(KERN_ERR "%s: pd9v svooc  default[%d]", __func__, chip->batt_volt);
	}

	return ret;
}

int oplus_mt6765_pd_setup_forvooc(void)
{
	int vbus_mv = VBUS_5V;
	int ibus_ma = IBUS_2A;
	int ret = -1;
	struct adapter_power_cap cap;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int i;

	if (chip->pd_svooc) {
		printk(KERN_ERR, "%s pd_svooc support\n", __func__);
		return 0;
	}

	cap.nr = 0;
	cap.pdp = 0;
	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
		cap.max_mv[i] = 0;
		cap.min_mv[i] = 0;
		cap.ma[i] = 0;
		cap.type[i] = 0;
		cap.pwr_limit[i] = 0;
	}

	printk(KERN_ERR "%s: pd_type %d pd for vooc30_project [%d]",
			__func__, pinfo->pd_type, chip->batt_volt);

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
		adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD_APDO, &cap);
		for (i = 0; i < cap.nr; i++) {
			printk(KERN_ERR "PD APDO cap %d: mV:%d,%d mA:%d type:%d pwr_limit:%d pdp:%d\n",
				i, cap.max_mv[i], cap.min_mv[i], cap.ma[i],
				cap.type[i], cap.pwr_limit[i], cap.pdp);
		}

		for (i = 0; i < cap.nr; i++) {
			if (cap.min_mv[i] <= VBUS_5V && VBUS_5V <= cap.max_mv[i]) {
				vbus_mv = VBUS_5V;
				ibus_ma = cap.ma[i];
				if (ibus_ma > IBUS_3A)
					ibus_ma = IBUS_3A;
				break;
			}
		}
	} else if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK
		|| pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30) {
		adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD, &cap);
		for (i = 0; i < cap.nr; i++) {
			printk(KERN_ERR "PD cap %d: mV:%d,%d mA:%d type:%d\n", i,
				cap.max_mv[i], cap.min_mv[i], cap.ma[i], cap.type[i]);
		}

		for (i = 0; i < cap.nr; i++) {
			if (VBUS_5V <= cap.max_mv[i]) {
				vbus_mv = cap.max_mv[i];
				ibus_ma = cap.ma[i];
				if (ibus_ma > IBUS_3A)
					ibus_ma = IBUS_3A;
				break;
			}
		}
	} else {
		vbus_mv = VBUS_5V;
		ibus_ma = IBUS_2A;
	}

	printk(KERN_ERR "PD request: %dmV, %dmA\n", vbus_mv, ibus_ma);
	ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);

	return ret;
}

int oplus_chg_pd_setup(void)
{
	int vbus_mv = VBUS_5V;
	int ibus_ma = IBUS_2A;
	int ret = -1;
	struct adapter_power_cap cap;
	struct oplus_chg_chip *chip = g_oplus_chip;
	int i;

	if (oplus_chg_get_voocphy_support()) {
		ret = oplus_mt6765_pd_setup_forsvooc();
	} else if (is_vooc30_project()) {
		ret = oplus_mt6765_pd_setup_forvooc();
	} else {
		cap.nr = 0;
		cap.pdp = 0;
		for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
			cap.max_mv[i] = 0;
			cap.min_mv[i] = 0;
			cap.ma[i] = 0;
			cap.type[i] = 0;
			cap.pwr_limit[i] = 0;
		}

		printk(KERN_ERR "pd_type: %d\n", pinfo->pd_type);
		printk(KERN_ERR "%s: [%d-%d-%d-%d-%d-%d]",
			__func__, chip->calling_on,
			chip->camera_on, chip->charger_volt,
			chip->soc, chip->temperature, chip->cool_down_force_5v);
		if (!chip->calling_on
			&& !chip->camera_on
			&& chip->charger_volt < PD_CHG_9V2A_VOLT_THRESHOLD
			&& chip->soc < PD_CHG_SOC_THRESHOLD
			&& chip->temperature <= PD_CHG_TEMP_THRESHOLD
			&& chip->cool_down_force_5v == false) {
			if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
				adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD_APDO, &cap);
				for (i = 0; i < cap.nr; i++) {
					printk(KERN_ERR "PD APDO cap %d: mV:%d,%d mA:%d type:%d pwr_limit:%d pdp:%d\n",
						i, cap.max_mv[i], cap.min_mv[i], cap.ma[i],
						cap.type[i], cap.pwr_limit[i], cap.pdp);
				}

				for (i = 0; i < cap.nr; i++) {
					if (cap.min_mv[i] <= VBUS_9V && VBUS_9V <= cap.max_mv[i]) {
						vbus_mv = VBUS_9V;
						ibus_ma = cap.ma[i];
						if (ibus_ma > IBUS_2A)
							ibus_ma = IBUS_2A;
						break;
					}
				}
			} else if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK
				|| pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30) {
				adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD, &cap);
				for (i = 0; i < cap.nr; i++) {
					printk(KERN_ERR "PD cap %d: mV:%d,%d mA:%d type:%d\n", i,
						cap.max_mv[i], cap.min_mv[i], cap.ma[i], cap.type[i]);
				}

				for (i = 0; i < cap.nr; i++) {
					if (VBUS_9V <= cap.max_mv[i]) {
						vbus_mv = cap.max_mv[i];
						ibus_ma = cap.ma[i];
						if (ibus_ma > IBUS_2A)
							ibus_ma = IBUS_2A;
						break;
					}
				}
			} else {
				vbus_mv = VBUS_5V;
				ibus_ma = IBUS_2A;
			}

			printk(KERN_ERR "PD request: %dmV, %dmA\n", vbus_mv, ibus_ma);
			ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
		} else {
			if (chip->charger_volt > PD_CHG_5V3A_VOLT_THRESHOLD &&
				(chip->calling_on
				|| chip->camera_on
				|| chip->soc >= PD_CHG_SOC_THRESHOLD
				|| chip->batt_volt >= PD_CHG_BATT_VOLT_THRESHOLD
				|| chip->temperature > PD_CHG_TEMP_THRESHOLD
				|| chip->cool_down_force_5v == true)) {
				vbus_mv = VBUS_5V;
				ibus_ma = IBUS_3A;

				printk(KERN_ERR "PD request: %dmV, %dmA\n", vbus_mv, ibus_ma);
				ret = oplus_pdc_setup(&vbus_mv, &ibus_ma);
			}
		}
	}

	return ret;
}

int oplus_chg_get_charger_subtype(void)
{
	if (!pinfo)
		return CHARGER_SUBTYPE_DEFAULT;

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK ||
		pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30 ||
		pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO)
		return CHARGER_SUBTYPE_PD;

	return CHARGER_SUBTYPE_DEFAULT;
}

/*=============== fix me==================*/
int chargerlog_level = CHRLOG_ERROR_LEVEL;

#ifdef mtk_charger_EXP
#include <linux/string.h>

char chargerlog[1000];
#define LOG_LENGTH 500
int chargerlog_level = 10;
int chargerlog_idx;

int charger_get_debug_level(void)
{
	return chargerlog_level;
}

void charger_log(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsprintf(chargerlog + chargerlog_idx, fmt, args);
	va_end(args);
	chargerlog_idx = strlen(chargerlog);
	if (chargerlog_idx >= LOG_LENGTH) {
		chg_err("%s", chargerlog);
		chargerlog_idx = 0;
		memset(chargerlog, 0, 1000);
	}
}

void charger_log_flash(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsprintf(chargerlog + chargerlog_idx, fmt, args);
	va_end(args);
	chg_err("%s", chargerlog);
	chargerlog_idx = 0;
	memset(chargerlog, 0, 1000);
}
#endif /*mtk_charger_EXP*/

bool oplus_tchg_01c_precision(void)
{
	if (!pinfo) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: charger_data not ready!\n", __func__);
		return false;
	}

	return pinfo->support_ntc_01c_precision;
}
EXPORT_SYMBOL(oplus_tchg_01c_precision);

/* mtk_charger ops  */
static int _mtk_charger_change_current_setting(struct mtk_charger *info)
{
	if (info != NULL && info->algo.change_current_setting)
		return info->algo.change_current_setting(info);

	return 0;
}

static int _mtk_charger_do_charging(struct mtk_charger *info, bool en)
{
	if (info != NULL && info->algo.do_charging)
		info->algo.do_charging(info, en);
	return 0;
}
/* mtk_charger ops end */

/* user interface */

struct charger_consumer *charger_manager_get_by_name(struct device *dev,
 	const char *name)
{
	struct charger_consumer *puser;

	puser = kzalloc(sizeof(struct charger_consumer), GFP_KERNEL);
	if (puser == NULL)
		return NULL;

	mutex_lock(&consumer_mutex);
	puser->dev = dev;

	list_add(&puser->list, &consumer_head);
	if (pinfo != NULL)
		puser->cm = pinfo;

	mutex_unlock(&consumer_mutex);

	return puser;
}
EXPORT_SYMBOL(charger_manager_get_by_name);

struct charger_consumer *mtk_charger_get_by_name(struct device *dev,
	const char *name)
{
	struct charger_consumer *puser;

	puser = kzalloc(sizeof(struct charger_consumer), GFP_KERNEL);
	if (puser == NULL)
		return NULL;

	mutex_lock(&consumer_mutex);
	puser->dev = dev;

	list_add(&puser->list, &consumer_head);
	if (pinfo != NULL)
		puser->cm = pinfo;

	mutex_unlock(&consumer_mutex);

	return puser;
}
EXPORT_SYMBOL(mtk_charger_get_by_name);

int mtk_charger_enable_high_voltage_charging(
			struct charger_consumer *consumer, bool en)
{
	struct mtk_charger *info = consumer->cm;
	struct list_head *pos;
	struct list_head *phead = &consumer_head;
	struct charger_consumer *ptr;

	if (!info)
		return -EINVAL;

	pr_debug("[%s] %s, %d\n", __func__, dev_name(consumer->dev), en);

	if (!en && consumer->hv_charging_disabled == false)
		consumer->hv_charging_disabled = true;
	else if (en && consumer->hv_charging_disabled == true)
		consumer->hv_charging_disabled = false;
	else {
		pr_info("[%s] already set: %d %d\n", __func__,
			consumer->hv_charging_disabled, en);
		return 0;
	}

	mutex_lock(&consumer_mutex);
	list_for_each(pos, phead) {
		ptr = container_of(pos, struct charger_consumer, list);
		if (ptr->hv_charging_disabled == true) {
			info->enable_hv_charging = false;
			break;
		}
		if (list_is_last(pos, phead))
			info->enable_hv_charging = true;
	}
	mutex_unlock(&consumer_mutex);

	pr_info("%s: user: %s, en = %d\n", __func__, dev_name(consumer->dev),
		info->enable_hv_charging);
	_wake_up_charger(info);

	return 0;
}
EXPORT_SYMBOL(mtk_charger_enable_high_voltage_charging);

int mtk_charger_enable_power_path(struct charger_consumer *consumer,
	int idx, bool en)
{
	int ret = 0;
	bool is_en = true;
	struct mtk_charger *info = consumer->cm;
	struct charger_device *chg_dev;


	if (!info)
		return -EINVAL;

	switch (idx) {
	case MAIN_CHARGER:
		chg_dev = info->chg1_dev;
		break;
	case SLAVE_CHARGER:
		chg_dev = info->chg2_dev;
		break;
	default:
		return -EINVAL;
	}

	ret = charger_dev_is_powerpath_enabled(chg_dev, &is_en);
	if (ret < 0) {
		chg_err("%s: get is power path enabled failed\n", __func__);
		return ret;
	}
	if (is_en == en) {
		chg_err("%s: power path is already en = %d\n", __func__, is_en);
		return 0;
	}

	pr_info("%s: enable power path = %d\n", __func__, en);
	return charger_dev_enable_powerpath(chg_dev, en);
}

static int _mtk_charger_enable_charging(struct charger_consumer *consumer,
	int idx, bool en)
{
	struct mtk_charger *info = consumer->cm;

	chg_err("%s: dev:%s idx:%d en:%d\n", __func__, dev_name(consumer->dev),
		idx, en);

	if (info != NULL) {
		struct charger_data *pdata;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg_data[CHG1_SETTING];
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg_data[CHG2_SETTING];
		else
			return -ENOTSUPP;

		if (en == false) {
			_mtk_charger_do_charging(info, en);
			pdata->disable_charging_count++;
		} else {
			if (pdata->disable_charging_count == 1) {
				_mtk_charger_do_charging(info, en);
				pdata->disable_charging_count = 0;
			} else if (pdata->disable_charging_count > 1)
				pdata->disable_charging_count--;
		}
		chg_err("%s: dev:%s idx:%d en:%d cnt:%d\n", __func__,
			dev_name(consumer->dev), idx, en,
			pdata->disable_charging_count);

		return 0;
	}
	return -EBUSY;
}

#define OPLUS_SVID 0x22D9
int oplus_get_adapter_svid(void)
{
	int i = 0;
	uint32_t vdos[VDO_MAX_NR] = {0};
	struct tcpc_device *tcpc_dev = tcpc_dev_get_by_name("type_c_port0");
	struct tcpm_svid_list svid_list = {0, {0}};

	if (tcpc_dev == NULL || !g_oplus_chip) {
		chg_err("tcpc_dev is null return\n");
		return -1;
	}

	tcpm_inquire_pd_partner_svids(tcpc_dev, &svid_list);
	for (i = 0; i < svid_list.cnt; i++) {
		chg_err("svid[%d] = %d\n", i, svid_list.svids[i]);
		if (svid_list.svids[i] == OPLUS_SVID) {
			g_oplus_chip->pd_svooc = true;
			chg_err("match svid and this is oplus adapter\n");
			break;
		}
	}

	if (!g_oplus_chip->pd_svooc) {
		tcpm_inquire_pd_partner_inform(tcpc_dev, vdos);
		if ((vdos[0] & 0xFFFF) == OPLUS_SVID) {
				g_oplus_chip->pd_svooc = true;
				chg_err("match svid and this is oplus adapter 11\n");
		}
	}

	return 0;
}

bool oplus_chg_check_pd_svooc_adapater(void)
{
	if (!g_oplus_chip) {
		chr_err("g_oplus_chip is null return \n");
		return false;
	}

	chg_err("pd_svooc = %d\n", g_oplus_chip->pd_svooc);

	return (g_oplus_chip->pd_svooc);
}
EXPORT_SYMBOL(oplus_chg_check_pd_svooc_adapater);

int mtk_charger_enable_charging(struct charger_consumer *consumer,
	int idx, bool en)
{
	struct mtk_charger *info = consumer->cm;
	int ret = 0;

#if defined(OPLUS_FEATURE_CHG_BASIC) && defined(CONFIG_OPLUS_CHARGER_MT6370_TYPEC)
	return -EBUSY;
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6765S
		return -EBUSY;
#endif /*CONFIG_OPLUS_CHARGER_MTK6765S*/

	mutex_lock(&info->charger_lock);
	ret = _mtk_charger_enable_charging(consumer, idx, en);
	mutex_unlock(&info->charger_lock);
	return ret;
}

int mtk_charger_set_input_current_limit(struct charger_consumer *consumer,
	int idx, int input_current)
{
	struct mtk_charger *info = consumer->cm;

#if defined(OPLUS_FEATURE_CHG_BASIC) && defined(CONFIG_OPLUS_CHARGER_MT6370_TYPEC)
	return -EBUSY;
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6765S
	return -EBUSY;
#endif /*CONFIG_OPLUS_CHARGER_MTK6765S*/
#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
	if (info != NULL) {
		struct charger_data *pdata;

		if (info->data.parallel_vbus) {
			if (idx == TOTAL_CHARGER) {
				info->chg1_data.thermal_input_current_limit =
					input_current;
				info->chg2_data.thermal_input_current_limit =
					input_current;
			} else
				return -ENOTSUPP;
		} else {
			if (idx == MAIN_CHARGER)
				pdata = &info->chg1_data;
			else if (idx == SLAVE_CHARGER)
				pdata = &info->chg2_data;
			else
				return -ENOTSUPP;
			pdata->thermal_input_current_limit = input_current;
		}

		chg_err("%s: dev:%s idx:%d en:%d\n", __func__,
			dev_name(consumer->dev), idx, input_current);
		_mtk_charger_change_current_setting(info);
		_wake_up_charger(info);
		return 0;
	}
	return -EBUSY;
#endif
}

int mtk_charger_set_charging_current_limit(
	struct charger_consumer *consumer, int idx, int charging_current)
{
	struct mtk_charger *info = consumer->cm;

#if defined(OPLUS_FEATURE_CHG_BASIC) && defined(CONFIG_OPLUS_CHARGER_MT6370_TYPEC)
	return -EBUSY;
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6765
		return -EBUSY;
#endif /*CONFIG_OPLUS_CHARGER_MTK6765*/

	if (info != NULL) {
		struct charger_data *pdata;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg_data[CHG1_SETTING];
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg_data[CHG2_SETTING];
		else
			return -ENOTSUPP;

		pdata->thermal_charging_current_limit = charging_current;
		chg_err("%s: dev:%s idx:%d en:%d\n", __func__,
			dev_name(consumer->dev), idx, charging_current);
		_mtk_charger_change_current_setting(info);
		_wake_up_charger(info);
		return 0;
	}
	return -EBUSY;
}

int mtk_charger_get_charger_temperature(struct charger_consumer *consumer,
	int idx, int *tchg_min,	int *tchg_max)
{
	struct mtk_charger *info = consumer->cm;

#if defined(OPLUS_FEATURE_CHG_BASIC) && defined(CONFIG_OPLUS_CHARGER_MT6370_TYPEC)
	return -EBUSY;
#endif
#ifdef CONFIG_OPLUS_CHARGER_MTK6765S
		return -EBUSY;
#endif /*CONFIG_OPLUS_CHARGER_MTK6765S*/

	if (info != NULL) {
		struct charger_data *pdata;
#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
		if (!upmu_get_rgs_chrdet()) {
			pr_debug("[%s] No cable in, skip it\n", __func__);
			*tchg_min = -127;
			*tchg_max = -127;
			return -EINVAL;
		}
#endif
		if (idx == MAIN_CHARGER)
			pdata = &info->chg_data[CHG1_SETTING];
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg_data[CHG2_SETTING];
		else
			return -ENOTSUPP;

		*tchg_min = pdata->junction_temp_min;
		*tchg_max = pdata->junction_temp_max;

		return 0;
	}
	return -EBUSY;
}

int mtk_charger_force_charging_current(struct charger_consumer *consumer,
	int idx, int charging_current)
{
	struct mtk_charger *info = consumer->cm;

	if (info != NULL) {
		struct charger_data *pdata;

		if (idx == MAIN_CHARGER)
			pdata = &info->chg_data[CHG1_SETTING];
		else if (idx == SLAVE_CHARGER)
			pdata = &info->chg_data[CHG2_SETTING];
		else
			return -ENOTSUPP;

		pdata->force_charging_current = charging_current;
		_mtk_charger_change_current_setting(info);
		_wake_up_charger(info);
		return 0;
	}
	return -EBUSY;
}

int mtk_charger_get_current_charging_type(struct charger_consumer *consumer)
{
	struct mtk_charger *info = consumer->cm;

	if (info != NULL) {
		/* if (mtk_pe20_get_is_connect(info)) */
		return 0;
	}

	return 0;
}

int mtk_charger_get_zcv(struct charger_consumer *consumer, int idx, u32 *uV)
{
	struct mtk_charger *info = consumer->cm;
	int ret = 0;
	struct charger_device *pchg;


	if (info != NULL) {
		if (idx == MAIN_CHARGER) {
			pchg = info->chg1_dev;
			ret = charger_dev_get_zcv(pchg, uV);
		} else if (idx == SLAVE_CHARGER) {
			pchg = info->chg2_dev;
			ret = charger_dev_get_zcv(pchg, uV);
		} else
			ret = -1;

	} else {
		chg_err("%s info is null\n", __func__);
	}
	chg_err("%s zcv:%d ret:%d\n", __func__, *uV, ret);

	return 0;
}

int mtk_charger_enable_chg_type_det(struct charger_consumer *consumer,
	bool en)
{
	struct mtk_charger *info = consumer->cm;
	struct charger_device *chg_dev;
	int ret = 0;

	if (info != NULL) {
		switch (info->data.bc12_charger) {
		case MAIN_CHARGER:
			chg_dev = info->chg1_dev;
			break;
		case SLAVE_CHARGER:
			chg_dev = info->chg2_dev;
			break;
		default:
			chg_dev = info->chg1_dev;
			chg_err("%s: invalid number, use main charger as default\n",
				__func__);
			break;
		}

		chg_err("%s: chg%d is doing bc12\n", __func__,
			info->data.bc12_charger + 1);
		ret = charger_dev_enable_chg_type_det(chg_dev, en);
		if (ret < 0) {
			chg_err("%s: en chgdet fail, en = %d\n", __func__, en);
			return ret;
		}
	} else
		chg_err("%s: mtk_charger is null\n", __func__);



	return 0;
}

int register_mtk_charger_notifier(struct charger_consumer *consumer,
	struct notifier_block *nb)
{
	int ret = 0;
	struct mtk_charger *info = consumer->cm;


	mutex_lock(&consumer_mutex);
	if (info != NULL)
		ret = srcu_notifier_chain_register(&info->evt_nh, nb);
	else
		consumer->pnb = nb;
	mutex_unlock(&consumer_mutex);

	return ret;
}

int unregister_mtk_charger_notifier(struct charger_consumer *consumer,
				struct notifier_block *nb)
{
	int ret = 0;
	struct mtk_charger *info = consumer->cm;

	mutex_lock(&consumer_mutex);
	if (info != NULL)
		ret = srcu_notifier_chain_unregister(&info->evt_nh, nb);
	else
		consumer->pnb = NULL;
	mutex_unlock(&consumer_mutex);

	return ret;
}

bool oplus_shortc_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (gpio_is_valid(chip->normalchg_gpio.shortc_gpio)) {
		return true;
	}
	return false;
}

int oplus_shortc_gpio_init(struct oplus_chg_chip *chip)
{
	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
	chip->normalchg_gpio.shortc_active =
		pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
			"shortc_active");

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.shortc_active)) {
		chg_err("get shortc_active fail\n");
		return -EINVAL;
        }

	pinctrl_select_state(chip->normalchg_gpio.pinctrl,
		chip->normalchg_gpio.shortc_active);
	return 0;
}
#ifdef CONFIG_OPLUS_SHORT_HW_CHECK
bool oplus_chg_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = 1;

	if(oplus_shortc_check_is_gpio(g_oplus_chip) == true) {
		shortc_hw_status = !!(gpio_get_value(g_oplus_chip->normalchg_gpio.shortc_gpio));
	}
	return shortc_hw_status;
}
#else
bool oplus_chg_get_shortc_hw_gpio_status(void)
{
	bool shortc_hw_status = 1;

	return shortc_hw_status;
}
#endif
int oplus_chg_shortc_hw_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip)
		node = chip->dev->of_node;
	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return -EINVAL;
	}

	if (chip) {
		chip->normalchg_gpio.shortc_gpio = of_get_named_gpio(node, "qcom,shortc_gpio", 0);
		if (chip->normalchg_gpio.shortc_gpio <= 0) {
			chg_err("Couldn't read qcom,shortc_gpio rc = %d, qcom,shortc_gpio:%d\n",
			rc, chip->normalchg_gpio.shortc_gpio);
		} else {
			if(oplus_shortc_check_is_gpio(chip) == true) {
				chg_debug("This project use gpio for shortc hw check\n");
				rc = gpio_request(chip->normalchg_gpio.shortc_gpio, "shortc_gpio");
				if (rc) {
					chg_err("unable to request shortc_gpio:%d\n",
					chip->normalchg_gpio.shortc_gpio);
				} else {
					oplus_shortc_gpio_init(chip);
				}
			} else {
				chg_err("chip->normalchg_gpio.shortc_gpio is not valid or get_PCB_Version() < V0.3:%d\n",
				get_PCB_Version());
			}
			chg_err("shortc_gpio:%d\n", chip->normalchg_gpio.shortc_gpio);
		}
	}
	return rc;
}

static int psy_charger_property_is_writeable(struct power_supply *psy,
					       enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		return 1;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		return 1;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		return 1;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		return 1;
	default:
		return 0;
	}
}

static enum power_supply_property charger_psy_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
};

static int psy_charger_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	struct mtk_charger *info;
	struct charger_device *chg;

	info = (struct mtk_charger *)power_supply_get_drvdata(psy);

	if (info->psy1 != NULL &&
		info->psy1 == psy)
		chg = info->chg1_dev;
	else if (info->psy2 != NULL &&
		info->psy2 == psy)
		chg = info->chg2_dev;
	else {
		chg_err("%s fail\n", __func__);
		return 0;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		/* val->intval = is_charger_exist(info); */
		if (mt6357_get_vbus_status())
			val->intval = 1;
		else
			val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = info->enable_hv_charging;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		val->intval = mt6357_get_vbus_voltage();
		break;
	case POWER_SUPPLY_PROP_TEMP:
		val->intval = get_charger_temperature(info, chg);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = get_charger_charging_current(info, chg);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = get_charger_input_current(info, chg);
		break;
	case POWER_SUPPLY_PROP_USB_TYPE:
		val->intval = g_oplus_chip->chg_ops->get_charger_type();
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int psy_charger_set_property(struct power_supply *psy,
			enum power_supply_property psp,
			const union power_supply_propval *val)
{
	struct mtk_charger *info;
	static struct power_supply *battery_psy = NULL;
	int charger_type;
	int idx;

	if (!battery_psy) {
		battery_psy = power_supply_get_by_name("battery");
	}

	chg_err("%s: prop:%d %d\n", __func__, psp, val->intval);

	info = (struct mtk_charger *)power_supply_get_drvdata(psy);

	if (info->psy1 != NULL && info->psy1 == psy)
		idx = CHG1_SETTING;
	else if (info->psy2 != NULL && info->psy2 == psy)
		idx = CHG2_SETTING;
	else {
		chg_err("%s fail\n", __func__);
		return 0;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_ONLINE:
		oplus_chg_wake_update_work();
		if (battery_psy) {
			power_supply_changed(battery_psy);
			oplus_chg_wake_update_work();
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		if (val->intval > 0)
			info->enable_hv_charging = true;
		else
			info->enable_hv_charging = false;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		info->chg_data[idx].thermal_charging_current_limit = val->intval;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		info->chg_data[idx].thermal_input_current_limit = val->intval;
		break;
	default:
		return -EINVAL;
	}
	_wake_up_charger(info);
	charger_type = g_oplus_chip->chg_ops->get_charger_type();
#ifndef CONFIG_TCPC_CLASS
	if (charger_type == POWER_SUPPLY_TYPE_USB_CDP
		|| charger_type == POWER_SUPPLY_TYPE_USB) {
		oplus_usb_extcon_set_role(DUAL_PROP_DR_DEVICE);
	} else {
		oplus_usb_extcon_set_role(DUAL_PROP_DR_NONE);
	}
#else
	if (charger_type == POWER_SUPPLY_TYPE_USB_CDP
		|| charger_type == POWER_SUPPLY_TYPE_USB) {
		mt_usb_connect_v1();
	} else {
		mt_usb_disconnect_v1();
	}
#endif
	return 0;
}

static void mtk_charger_external_power_changed(struct power_supply *psy)
{
	struct mtk_charger *info;
	union power_supply_propval prop, prop2, type_prop;
	struct power_supply *chg_psy = NULL;
	int ret;

	info = (struct mtk_charger *)power_supply_get_drvdata(psy);
#ifdef CONFIG_CHARGER_BQ2560X
	chg_psy = power_supply_get_by_name("bq2560x");
#else
	chg_psy = devm_power_supply_get_by_phandle(&info->pdev->dev,
						       "charger");
#endif
	if (IS_ERR_OR_NULL(chg_psy)) {
		pr_notice("%s Couldn't get chg_psy\n", __func__);
	} else {
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_ONLINE, &prop);
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_USB_TYPE, &prop2);
		ret = power_supply_get_property(chg_psy,
			POWER_SUPPLY_PROP_TYPE, &type_prop);
	}

	pr_notice("%s event, name:%s online:%d usb_type:%d type:%d vbus:%d\n", __func__,
		psy->desc->name, prop.intval, prop2.intval, type_prop.intval,
		get_vbus(info));
#ifndef OPLUS_FEATURE_CHG_BASIC
	mtk_is_charger_on(info);
#endif
	_wake_up_charger(info);
}

static int notify_adapter_event(struct notifier_block *notifier,
			unsigned long evt, void *unused)
{
	struct mtk_charger *pinfo = NULL;

	chg_err("%s %d\n", __func__, evt);

	pinfo = container_of(notifier,
		struct mtk_charger, pd_nb);

	switch (evt) {
	case  MTK_PD_CONNECT_NONE:
		mutex_lock(&pinfo->pd_lock);
		chg_err("PD Notify Detach\n");
		pinfo->pd_type = MTK_PD_CONNECT_NONE;
		mutex_unlock(&pinfo->pd_lock);
		/* reset PE40 */
		break;

	case MTK_PD_CONNECT_HARD_RESET:
		mutex_lock(&pinfo->pd_lock);
		chg_err("PD Notify HardReset\n");
		pinfo->pd_type = MTK_PD_CONNECT_NONE;
		pinfo->pd_reset = true;
		mutex_unlock(&pinfo->pd_lock);
		_wake_up_charger(pinfo);
		/* reset PE40 */
		break;

	case MTK_PD_CONNECT_PE_READY_SNK:
		mutex_lock(&pinfo->pd_lock);
		chg_err("PD Notify fixe voltage ready\n");
		pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK;
		mutex_unlock(&pinfo->pd_lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
		pinfo->in_good_connect = true;
		oplus_get_adapter_svid();
		chg_err("MTK_PD_CONNECT_PE_READY_SNK_PD30 in_good_connect true\n");
#endif
		/* PD is ready */
		break;
	case MTK_PD_CONNECT_PE_READY_SNK_PD30:
		mutex_lock(&pinfo->pd_lock);
		chg_err("PD Notify PD30 ready\r\n");
		pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK_PD30;
		mutex_unlock(&pinfo->pd_lock);
#ifdef OPLUS_FEATURE_CHG_BASIC
		pinfo->in_good_connect = true;
		oplus_get_adapter_svid();
		chr_err("MTK_PD_CONNECT_PE_READY_SNK_PD30 in_good_connect true\n");
#endif
		/* PD30 is ready */
		break;

	case MTK_PD_CONNECT_PE_READY_SNK_APDO:
		mutex_lock(&pinfo->pd_lock);
		chg_err("PD Notify APDO Ready\n");
		pinfo->pd_type = MTK_PD_CONNECT_PE_READY_SNK_APDO;
		mutex_unlock(&pinfo->pd_lock);
		/* PE40 is ready */
		_wake_up_charger(pinfo);
#ifdef OPLUS_FEATURE_CHG_BASIC
		pinfo->in_good_connect = true;
		oplus_get_adapter_svid();
		chg_err("MTK_PD_CONNECT_PE_READY_SNK_PD30 in_good_connect true\n");
#endif
		break;

	case MTK_PD_CONNECT_TYPEC_ONLY_SNK:
		mutex_lock(&pinfo->pd_lock);
		chg_err("PD Notify Type-C Ready\n");
		pinfo->pd_type = MTK_PD_CONNECT_TYPEC_ONLY_SNK;
		mutex_unlock(&pinfo->pd_lock);
		/* type C is ready */
		_wake_up_charger(pinfo);
		break;
	}
	return NOTIFY_DONE;
}

#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
int mt_power_supply_type_check(void)
{
	int charger_type = POWER_SUPPLY_TYPE_UNKNOWN;
	int charger_type_final = 0;
	/*if(otg_is_exist == 1)
		return g_chr_type;
	chg_debug("mt_power_supply_type_check-----1---------charger_type = %d\r\n",charger_type);*/
	if(true == upmu_is_chr_det()) {
		if(CHG_BQ24190 == charger_ic_flag) {
			charger_type_final = mt_charger_type_detection();
		} else if (CHG_BQ25890H == charger_ic_flag) {
			charger_type_final = mt_charger_type_detection_bq25890h();
		/* charger_type_final = STANDARD_HOST; */
		} else if (CHG_BQ25601D == charger_ic_flag) {
			charger_type_final = mt_charger_type_detection_bq25601d();
		} else if (CHG_SMB1351 == charger_ic_flag) {
			#ifdef CONFIG_OPLUS_CHARGER_MTK6771
			  charger_type_final = smb1351_get_charger_type();
			#else
			  chg_err("Un supported charger type");
			#endif
		}
		g_chr_type = charger_type_final;
	}
	else {
		chg_debug(" call first type\n");
		charger_type_final = g_chr_type;
	}

	switch(charger_type_final) {
	case CHARGER_UNKNOWN:
		break;
	case STANDARD_HOST:
		charger_type = POWER_SUPPLY_TYPE_USB;
		break;
	case CHARGING_HOST:
		charger_type = POWER_SUPPLY_TYPE_USB_CDP;
		break;
	case NONSTANDARD_CHARGER:
	case APPLE_0_5A_CHARGER:
	case STANDARD_CHARGER:
	case APPLE_2_1A_CHARGER:
	case APPLE_1_0A_CHARGER:
		charger_type = POWER_SUPPLY_TYPE_USB_DCP;
		break;
	default:
		break;
	}
	chg_debug("mt_power_supply_type_check-----2---------\
		charger_type = %d, charger_type_final = %d, g_chr_type = %d\r\n",
		charger_type, charger_type_final, g_chr_type);
	if (g_oplus_chip) {
		if ((g_oplus_chip->charger_type != charger_type) && g_oplus_chip->usb_psy)
			power_supply_changed(g_oplus_chip->usb_psy);
	}

	return charger_type;
}
#endif /*ODM_WT_EDIT*/
#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
enum {
	Channel_12 = 2,
	Channel_13,
	Channel_14,
};
extern int IMM_GetOneChannelValue(int dwChannel, int data[4], int *rawdata);
extern int IMM_IsAdcInitReady(void);
int mt_vadc_read(int times, int Channel)
{
	int ret = 0, data[4], i, ret_value = 0, ret_temp = 0;
	if(IMM_IsAdcInitReady() == 0) {
		return 0;
	}
	i = times;
	while (i--) {
		ret_value = IMM_GetOneChannelValue(Channel, data, &ret_temp);
		if (ret_value != 0) {
			i++;
			continue;
		}
		ret += ret_temp;
	}
	ret = ret*1500/4096;
	ret = ret/times;
	/* chg_debug("[mt_vadc_read] Channel %d: vol_ret=%d\n",Channel,ret); */
	return ret;
}
#endif

static void set_usbswitch_to_rxtx(struct oplus_chg_chip *chip)
{
	int ret = 0;

	if ((chip == NULL) || (chip->normalchg_gpio.chargerid_switch_gpio <= 0)) {
		chg_err("chargerid_switch_gpio not exist\n");
		return;
	}

	gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 1);
	ret = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.charger_gpio_as_output2);
	if (ret < 0) {
		chg_err("failed to set pinctrl int\n");
		return;
	}
	chg_err("set_usbswitch_to_rxtx \n");
}
static void set_usbswitch_to_dpdm(struct oplus_chg_chip *chip)
{
	int ret = 0;

	if ((chip == NULL) || (chip->normalchg_gpio.chargerid_switch_gpio <= 0)) {
		chg_err("chargerid_switch_gpio not exist\n");
		return;
	}

	gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 0);
	ret = pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.charger_gpio_as_output1);
	if (ret < 0) {
		chg_err("failed to set pinctrl int\n");
		return;
	}
	chg_err("set_usbswitch_to_dpdm \n");
}

static bool is_support_chargerid_check(void)
{
#ifdef CONFIG_OPLUS_CHECK_CHARGERID_VOLT
	return true;
#else
	return false;
#endif
}

int mt_get_chargerid_volt(void)
{
	int chargerid_volt = 0;
	if (is_support_chargerid_check() == true) {
		chg_debug("chargerid_volt = %d \n",
					   chargerid_volt);
	} else {
		chg_debug("is_support_chargerid_check = false !\n");
		return 0;
	}
	return chargerid_volt;
}

void mt_set_chargerid_switch_val(int value)
{
	chg_debug("set_value= %d\n", value);
	if(NULL == g_oplus_chip)
		return;
	if(is_support_chargerid_check() == false)
		return;
	if(g_oplus_chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
		chg_err("chargerid_switch_gpio not exist, return\n");
		return;
	}
	if(IS_ERR_OR_NULL(g_oplus_chip->normalchg_gpio.pinctrl)
		|| IS_ERR_OR_NULL(g_oplus_chip->normalchg_gpio.charger_gpio_as_output1)
		|| IS_ERR_OR_NULL(g_oplus_chip->normalchg_gpio.charger_gpio_as_output2)) {
		chg_err("pinctrl null, return\n");
		return;
	}
	if (1 == value) {
		set_usbswitch_to_rxtx(g_oplus_chip);
	} else if (0 == value) {
		set_usbswitch_to_dpdm(g_oplus_chip);
	} else {
		/* do nothing */
	}
	chg_debug("get_val:%d\n", gpio_get_value(g_oplus_chip->normalchg_gpio.chargerid_switch_gpio));
}

int mt_get_chargerid_switch_val(void)
{
	int gpio_status = 0;

	if((NULL == g_oplus_chip) || (g_oplus_chip->normalchg_gpio.chargerid_switch_gpio <= 0)) {
		chg_err("chargerid_switch_gpio not exist\n");
		return 0;
	}

	if(is_support_chargerid_check() == false)
		return 0;

	gpio_status = gpio_get_value(g_oplus_chip->normalchg_gpio.chargerid_switch_gpio);

	chg_debug("mt_get_chargerid_switch_val:%d\n", gpio_status);
	return gpio_status;
}


int oplus_usb_switch_gpio_gpio_init(void)
{
	chg_err("---1-----");
	g_oplus_chip->normalchg_gpio.pinctrl = devm_pinctrl_get(g_oplus_chip->dev);
	if (IS_ERR_OR_NULL(g_oplus_chip->normalchg_gpio.pinctrl)) {
		chg_err("get normalchg_gpio.chargerid_switch_gpio pinctrl falil\n");
		return -EINVAL;
	}
	g_oplus_chip->normalchg_gpio.charger_gpio_as_output1 = pinctrl_lookup_state(g_oplus_chip->normalchg_gpio.pinctrl,
								"charger_gpio_as_output_low");
	if (IS_ERR_OR_NULL(g_oplus_chip->normalchg_gpio.charger_gpio_as_output1)) {
		chg_err("get charger_gpio_as_output_low fail\n");
		return -EINVAL;
	}
	g_oplus_chip->normalchg_gpio.charger_gpio_as_output2 = pinctrl_lookup_state(g_oplus_chip->normalchg_gpio.pinctrl,
								"charger_gpio_as_output_high");
	if (IS_ERR_OR_NULL(g_oplus_chip->normalchg_gpio.charger_gpio_as_output2)) {
		chg_err("get charger_gpio_as_output_high fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(g_oplus_chip->normalchg_gpio.pinctrl, g_oplus_chip->normalchg_gpio.charger_gpio_as_output1);
	return 0;
}


int charger_pretype_get(void)
{
	int chg_type = STANDARD_HOST;
	/* chg_type = hw_charging_get_charger_type(); */
	return chg_type;
}


bool oplus_pmic_check_chip_is_null(void)
{
	if (!is_vooc_project()) {
		return true;
	} else {
		return false;
	}
}

/* ==================================================================== */
static bool oplus_usbtemp_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.dischg_gpio))
		return true;

	return false;
}

static bool oplus_usbtemp_check_is_support(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (oplus_usbtemp_check_is_gpio(chip) == true)
		return true;

	chg_err("not support, return false\n");

	return false;
}

#define USB_20C		20                /* degreeC */
#define USB_50C		50
#define USB_57C		57
#define USB_100C	100
#define USB_25C_VOLT	1192              /* 900  mv */
#define USB_50C_VOLT	450
#define USB_55C_VOLT	448
#define USB_60C_VOLT	327
#define VBUS_VOLT_THRESHOLD	3000      /* 3V */
#define RETRY_CNT_DELAY		5         /* ms */
#define MIN_MONITOR_INTERVAL	50        /* 50ms */
#define VBUS_MONITOR_INTERVAL	3000      /* 3s */

#define USB_PORT_PULL_UP_R      390000    /* 390K */
#define USB_PORT_PULL_UP_VOLT   1800      /* 1.8V */
#define USB_NTC_TABLE_SIZE 74

struct USB_PORT_TEMPERATURE {
	signed int temp;
	signed int temperature_r;
};

struct USB_PORT_TEMPERATURE Usb_Port_Temperature_Table[USB_NTC_TABLE_SIZE] = {
/* NCP15WF104F03RC(100K) */
	{-400, 4397119},
	{-350, 3088599},
	{-300, 2197225},
	{-250, 1581881},
	{-200, 1151037},
	{-150, 846579},
	{-100, 628988},
	{-50, 471632},
	{0, 357012},
	{50, 272500},
	{100, 209710},
	{150, 162651},
	{200, 127080},
	{250, 100000},		/* 100K */
	{300, 79222},
	{350, 63167},
	{400, 50677},
	{410, 48528},
	{420, 46482},
	{430, 44533},
	{440, 42675},
	{450, 40904},
	{460, 39213},
	{470, 37601},
	{480, 36063},
	{490, 34595},
	{500, 33195},
	{510, 31859},
	{520, 30584},
	{530, 29366},
	{540, 28203},
	{550, 27091},
	{560, 26028},
	{570, 25013},
	{580, 24042},
	{590, 23113},
	{600, 22224},
	{610, 21374},
	{620, 20560},
	{630, 19782},
	{640, 19036},
	{650, 18322},
	{660, 17640},
	{670, 16986},
	{680, 16360},
	{690, 15759},
	{700, 15184},
	{710, 14631},
	{720, 14100},
	{730, 13591},
	{740, 13103},
	{750, 12635},
	{760, 12187},
	{770, 11756},
	{780, 11343},
	{790, 10946},
	{800, 10565},
	{810, 10199},
	{820,  9847},
	{830,  9509},
	{840,  9184},
	{850,  8872},
	{860,  8572},
	{870,  8283},
	{880,  8005},
	{890,  7738},
	{900,  7481},
	{950,  6337},
	{1000, 5384},
	{1050, 4594},
	{1100, 3934},
	{1150, 3380},
	{1200, 2916},
	{1250, 2522}
};

int usbtemp_channel_burn_init(struct device *dev)
{
	int ret = 0;
	usb_chan1 = iio_channel_get(dev, "usbtemp-ch2");
	if(IS_ERR_OR_NULL(usb_chan1)) {
		chg_err("usb_chan1 init fial,err = %d", PTR_ERR(usb_chan1));
		ret = -1;
	}

	usb_chan2 = iio_channel_get(dev, "usbtemp-ch4");
	if(IS_ERR_OR_NULL(usb_chan2)) {
		chg_err("usb_chan2 init fial,err = %d", PTR_ERR(usb_chan2));
		ret = -1;
	}
	return ret;
}

void oplus_get_usbtemp_burn_volt(struct oplus_chg_chip *chip)
{
	int usbtemp_volt = 0;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}

	iio_read_channel_processed(usb_chan1, &usbtemp_volt);
	if (usbtemp_volt <= 0) {
		usbtemp_volt = USB_25C_VOLT;
	}
	/*usbtemp_volt = (usbtemp_volt * 1500) >> 12;*/
	chip->usbtemp_volt_r = usbtemp_volt;
	iio_read_channel_processed(usb_chan2, &usbtemp_volt);
	if (usbtemp_volt <= 0) {
		usbtemp_volt = USB_25C_VOLT;
	}
	/*usbtemp_volt = (usbtemp_volt * 1500) >> 12;*/
	chip->usbtemp_volt_l = usbtemp_volt;

	USBTEMP_LOG_PRINT("usbtemp_volt: %d, %d\n", chip->usbtemp_volt_r, chip->usbtemp_volt_l);
	return;
}

int usb_port_volt_to_temp(int volt)
{
	int i = 0;
	int usb_r = 0;
	int res1 = 0, res2 = 0;
	int usb_temp_value = 25, tmp1 = 0, tmp2 = 0;

	if(usb_debug_temp != 65535)
		return usb_debug_temp;

	usb_r = volt * USB_PORT_PULL_UP_R / (USB_PORT_PULL_UP_VOLT - volt);
	USBTEMP_LOG_PRINT("[UsbTemp] NTC_R = %d\n", usb_r);
	if (usb_r >= Usb_Port_Temperature_Table[0].temperature_r) {
		usb_temp_value = -40;
	} else if (usb_r <= Usb_Port_Temperature_Table[USB_NTC_TABLE_SIZE - 1].temperature_r) {
		usb_temp_value = 125;
	} else {
		res1 = Usb_Port_Temperature_Table[0].temperature_r;
		tmp1 = Usb_Port_Temperature_Table[0].temp;

		for (i = 0; i < USB_NTC_TABLE_SIZE; i++) {
			if (usb_r >= Usb_Port_Temperature_Table[i].temperature_r) {
				res2 = Usb_Port_Temperature_Table[i].temperature_r;
				tmp2 = Usb_Port_Temperature_Table[i].temp;
				break;
			} {	/* hidden else */
				res1 = Usb_Port_Temperature_Table[i].temperature_r;
				tmp1 = Usb_Port_Temperature_Table[i].temp;
			}
		}

		usb_temp_value = (((usb_r - res2) * tmp1) +
			((res1 - usb_r) * tmp2)) / (res1 - res2);
	}

	USBTEMP_LOG_PRINT(
		"[%s] %d %d %d %d %d %d\n",
		__func__,
		res1, res2, usb_r, tmp1,
		tmp2, usb_temp_value);

	return usb_temp_value;
}

int get_chgntc_adc_temp(struct charger_device *chg_dev, int *tmin, int *tmax)
{
	int usbtemp_volt = 0;

	if (!is_kthread_get_adc())
		return 0;

	usbtemp_volt = get_charger_ntc_volt();
	*tmin = usb_port_volt_to_temp(usbtemp_volt);
	usbtemp_volt = get_charger_ntc_volt();
	*tmax = usb_port_volt_to_temp(usbtemp_volt);
	return 0;
}

static bool oplus_chg_get_vbus_status(struct oplus_chg_chip *chip)
{
	int charger_type;
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	charger_type = chip->chg_ops->get_charger_type();
	if(charger_type)
		return true;
	else
		return false;
}

enum charger_type mt_get_charger_type()
{
	return g_oplus_chip->charger_type;
}

int charger_get_vbus(void);

static int battery_meter_get_charger_voltage(void)
{
	return mt6357_get_vbus_voltage();
}

/* factory mode start*/
#define CHARGER_DEVNAME "charger_ftm"
#define GET_IS_SLAVE_CHARGER_EXIST _IOW('k', 13, int)

static struct class *charger_class;
static struct cdev *charger_cdev;
static int charger_major;
static dev_t charger_devno;

static int is_slave_charger_exist(void)
{
	if (get_charger_by_name("secondary_chg") == NULL)
		return 0;
	return 1;
}

static long charger_ftm_ioctl(struct file *file, unsigned int cmd,
				unsigned long arg)
{
	int ret = 0;
	int out_data = 0;
	void __user *user_data = (void __user *)arg;

	switch (cmd) {
	case GET_IS_SLAVE_CHARGER_EXIST:
		out_data = is_slave_charger_exist();
		ret = copy_to_user(user_data, &out_data, sizeof(out_data));
		chg_err("[%s] SLAVE_CHARGER_EXIST: %d\n", __func__, out_data);
		break;
	default:
		chg_err("[%s] Error ID\n", __func__);
		break;
	}

	return ret;
}
#ifdef CONFIG_COMPAT
static long charger_ftm_compat_ioctl(struct file *file,
			unsigned int cmd, unsigned long arg)
{
	int ret = 0;

	switch (cmd) {
	case GET_IS_SLAVE_CHARGER_EXIST:
		ret = file->f_op->unlocked_ioctl(file, cmd, arg);
		break;
	default:
		chg_err("[%s] Error ID\n", __func__);
		break;
	}

	return ret;
}
#endif
static int charger_ftm_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int charger_ftm_release(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations charger_ftm_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = charger_ftm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl = charger_ftm_compat_ioctl,
#endif
	.open = charger_ftm_open,
	.release = charger_ftm_release,
};

void charger_ftm_init(void)
{
	struct class_device *class_dev = NULL;
	int ret = 0;

	ret = alloc_chrdev_region(&charger_devno, 0, 1, CHARGER_DEVNAME);
	if (ret < 0) {
		chg_err("[%s]Can't get major num for charger_ftm\n", __func__);
		return;
	}

	charger_cdev = cdev_alloc();
	if (!charger_cdev) {
		chg_err("[%s]cdev_alloc fail\n", __func__);
		goto unregister;
	}
	charger_cdev->owner = THIS_MODULE;
	charger_cdev->ops = &charger_ftm_fops;

	ret = cdev_add(charger_cdev, charger_devno, 1);
	if (ret < 0) {
		chg_err("[%s] cdev_add failed\n", __func__);
		goto free_cdev;
	}

	charger_major = MAJOR(charger_devno);
	charger_class = class_create(THIS_MODULE, CHARGER_DEVNAME);
	if (IS_ERR(charger_class)) {
		chg_err("[%s] class_create failed\n", __func__);
		goto free_cdev;
	}

	class_dev = (struct class_device *)device_create(charger_class,
				NULL, charger_devno, NULL, CHARGER_DEVNAME);
	if (IS_ERR(class_dev)) {
		chg_err("[%s] device_create failed\n", __func__);
		goto free_class;
	}

	pr_debug("%s done\n", __func__);
	return;

free_class:
	class_destroy(charger_class);
free_cdev:
	cdev_del(charger_cdev);
unregister:
	unregister_chrdev_region(charger_devno, 1);
}
/* factory mode end */

#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
void mtk_charger_get_atm_mode(struct mtk_charger *info)
{
	char atm_str[64];
	char *ptr, *ptr_e;

	memset(atm_str, 0x0, sizeof(atm_str));
	ptr = strstr(saved_command_line, "androidboot.atm=");
	if (ptr != 0) {
		ptr_e = strstr(ptr, " ");

		if (ptr_e != 0) {
			strncpy(atm_str, ptr + 16, ptr_e - ptr - 16);
			atm_str[ptr_e - ptr - 16] = '\0';
		}

		if (!strncmp(atm_str, "enable", strlen("enable")))
			info->atm_enabled = true;
		else
			info->atm_enabled = false;
	} else
		info->atm_enabled = false;

	pr_info("%s: atm_enabled = %d\n", __func__, info->atm_enabled);
}
#endif

/* internal algorithm common function */
bool is_dual_charger_supported(struct mtk_charger *info)
{
	if (info->chg2_dev == NULL)
		return false;
	return true;
}

int charger_enable_vbus_ovp(struct mtk_charger *pinfo, bool enable)
{
	int ret = 0;
	u32 sw_ovp = 0;

	if (enable)
		sw_ovp = pinfo->data.max_charger_voltage_setting;
	else
		sw_ovp = 15000000;

	/* Enable/Disable SW OVP status */
	pinfo->data.max_charger_voltage = sw_ovp;

	chg_err("[%s] en:%d ovp:%d\n",
			    __func__, enable, sw_ovp);
	return ret;
}

bool is_typec_adapter(struct mtk_charger *info)
{
	int rp;

	rp = adapter_dev_get_property(info->pd_adapter, TYPEC_RP_LEVEL);
	if (info->pd_type == MTK_PD_CONNECT_TYPEC_ONLY_SNK &&
			rp != 500 &&
			info->chr_type != STANDARD_HOST &&
			info->chr_type != CHARGING_HOST &&
			/* 
			 * mtk_pe20_get_is_connect(info) == false &&
			 * mtk_pe_get_is_connect(info) == false &&
			 */
			info->enable_type_c == true)
		return true;

	return false;
}

int charger_get_vbus(void)
{
	int ret = 0;
	int vchr = 0;

	if (pinfo == NULL)
		return 0;
	ret = charger_dev_get_vbus(pinfo->chg1_dev, &vchr);
	if (ret < 0) {
		chg_err("%s: get vbus failed: %d\n", __func__, ret);
		return ret;
	}

	vchr = vchr / 1000;
	return vchr;
}

/* internal algorithm common function end */

#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
/* sw jeita */
void do_sw_jeita_state_machine(struct mtk_charger *info)
{
	struct sw_jeita_data *sw_jeita;

	sw_jeita = &info->sw_jeita;
	sw_jeita->pre_sm = sw_jeita->sm;
	sw_jeita->charging = true;

	/* JEITA battery temp Standard */
	if (info->battery_temp >= info->data.temp_t4_thres) {
		chg_err("[SW_JEITA] Battery Over high Temperature(%d) !!\n",
			info->data.temp_t4_thres);

		sw_jeita->sm = TEMP_ABOVE_T4;
		sw_jeita->charging = false;
	} else if (info->battery_temp > info->data.temp_t3_thres) {
		/* control 45 degree to normal behavior */
		if ((sw_jeita->sm == TEMP_ABOVE_T4)
		    && (info->battery_temp
			>= info->data.temp_t4_thres_minus_x_degree)) {
			chg_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
				info->data.temp_t4_thres_minus_x_degree,
				info->data.temp_t4_thres);

			sw_jeita->charging = false;
		} else {
			chg_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t3_thres,
				info->data.temp_t4_thres);

			sw_jeita->sm = TEMP_T3_TO_T4;
		}
	} else if (info->battery_temp >= info->data.temp_t2_thres) {
		if (((sw_jeita->sm == TEMP_T3_TO_T4)
		     && (info->battery_temp
			 >= info->data.temp_t3_thres_minus_x_degree))
		    || ((sw_jeita->sm == TEMP_T1_TO_T2)
			&& (info->battery_temp
			    <= info->data.temp_t2_thres_plus_x_degree))) {
			chg_err("[SW_JEITA] Battery Temperature not recovery to normal temperature charging mode yet!!\n");
		} else {
			chg_err("[SW_JEITA] Battery Normal Temperature between %d and %d !!\n",
				info->data.temp_t2_thres,
				info->data.temp_t3_thres);
			sw_jeita->sm = TEMP_T2_TO_T3;
		}
	} else if (info->battery_temp >= info->data.temp_t1_thres) {
		if ((sw_jeita->sm == TEMP_T0_TO_T1
		     || sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temp
			<= info->data.temp_t1_thres_plus_x_degree)) {
			if (sw_jeita->sm == TEMP_T0_TO_T1) {
				chg_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
					info->data.temp_t1_thres_plus_x_degree,
					info->data.temp_t2_thres);
			}
			if (sw_jeita->sm == TEMP_BELOW_T0) {
				chg_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
					info->data.temp_t1_thres,
					info->data.temp_t1_thres_plus_x_degree);
				sw_jeita->charging = false;
			}
		} else {
			chg_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t1_thres,
				info->data.temp_t2_thres);

			sw_jeita->sm = TEMP_T1_TO_T2;
		}
	} else if (info->battery_temp >= info->data.temp_t0_thres) {
		if ((sw_jeita->sm == TEMP_BELOW_T0)
		    && (info->battery_temp
			<= info->data.temp_t0_thres_plus_x_degree)) {
			chg_err("[SW_JEITA] Battery Temperature between %d and %d,not allow charging yet!!\n",
				info->data.temp_t0_thres,
				info->data.temp_t0_thres_plus_x_degree);

			sw_jeita->charging = false;
		} else {
			chg_err("[SW_JEITA] Battery Temperature between %d and %d !!\n",
				info->data.temp_t0_thres,
				info->data.temp_t1_thres);

			sw_jeita->sm = TEMP_T0_TO_T1;
		}
	} else {
		chg_err("[SW_JEITA] Battery below low Temperature(%d) !!\n",
			info->data.temp_t0_thres);
		sw_jeita->sm = TEMP_BELOW_T0;
		sw_jeita->charging = false;
	}

	/* set CV after temperature changed */
	/* In normal range, we adjust CV dynamically */
	if (sw_jeita->sm != TEMP_T2_TO_T3) {
		if (sw_jeita->sm == TEMP_ABOVE_T4)
			sw_jeita->cv = info->data.jeita_temp_above_t4_cv;
		else if (sw_jeita->sm == TEMP_T3_TO_T4)
			sw_jeita->cv = info->data.jeita_temp_t3_to_t4_cv;
		else if (sw_jeita->sm == TEMP_T2_TO_T3)
			sw_jeita->cv = 0;
		else if (sw_jeita->sm == TEMP_T1_TO_T2)
			sw_jeita->cv = info->data.jeita_temp_t1_to_t2_cv;
		else if (sw_jeita->sm == TEMP_T0_TO_T1)
			sw_jeita->cv = info->data.jeita_temp_t0_to_t1_cv;
		else if (sw_jeita->sm == TEMP_BELOW_T0)
			sw_jeita->cv = info->data.jeita_temp_below_t0_cv;
		else
			sw_jeita->cv = info->data.battery_cv;
	} else {
		sw_jeita->cv = 0;
	}

	chg_err("[SW_JEITA]preState:%d newState:%d tmp:%d cv:%d\n",
		sw_jeita->pre_sm, sw_jeita->sm, info->battery_temp,
		sw_jeita->cv);
}
#endif

static ssize_t show_sw_jeita(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chg_err("%s: %d\n", __func__, pinfo->enable_sw_jeita);
	return sprintf(buf, "%d\n", pinfo->enable_sw_jeita);
}

static ssize_t store_sw_jeita(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_sw_jeita = false;
		else
			pinfo->enable_sw_jeita = true;

	} else {
		chg_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR(sw_jeita, 0664, show_sw_jeita,
		   store_sw_jeita);
/* sw jeita end*/

#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
/* pump express series */
bool mtk_is_pep_series_connect(struct mtk_charger *info)
{
	if (mtk_pe20_get_is_connect(info) || mtk_pe_get_is_connect(info))
		return true;

	return false;
}

static ssize_t show_pe20(struct device *dev, struct device_attribute *attr,
					       char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	chg_err("%s: %d\n", __func__, pinfo->enable_pe_2);
	return sprintf(buf, "%d\n", pinfo->enable_pe_2);
}

static ssize_t store_pe20(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	signed int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		if (temp == 0)
			pinfo->enable_pe_2 = false;
		else
			pinfo->enable_pe_2 = true;

	} else {
		chg_err("%s: format error!\n", __func__);
	}
	return size;
}

static DEVICE_ATTR(pe20, 0664, show_pe20, store_pe20);

/* pump express series end*/
#endif

int mtk_pdc_get_max_watt(struct mtk_charger *info);

static ssize_t show_pdc_max_watt_level(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	return sprintf(buf, "%d\n", mtk_pdc_get_max_watt(pinfo));
}

void mtk_pdc_set_max_watt(struct mtk_charger *info, int watt);

static ssize_t store_pdc_max_watt_level(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int temp;

	if (kstrtoint(buf, 10, &temp) == 0) {
		mtk_pdc_set_max_watt(pinfo, temp);
		chg_err("[store_pdc_max_watt]:%d\n", temp);
	} else
		chg_err("[store_pdc_max_watt]: format error!\n");

	return size;
}
static DEVICE_ATTR(pdc_max_watt, 0664, show_pdc_max_watt_level,
		store_pdc_max_watt_level);

int mtk_get_dynamic_cv(struct mtk_charger *info, unsigned int *cv)
{
	int ret = 0;
#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
	u32 _cv, _cv_temp;
	unsigned int vbat_threshold[4] = {3400000, 0, 0, 0};
	u32 vbat_bif = 0, vbat_auxadc = 0, vbat = 0;
	u32 retry_cnt = 0;

	if (pmic_is_bif_exist()) {
		do {
			vbat_auxadc = battery_get_bat_voltage() * 1000;
			vbat_auxdac = oplus_battery_meter_get_battery_voltage();
			ret = pmic_get_bif_battery_voltage(&vbat_bif);
			vbat_bif = vbat_bif * 1000;
			if (ret >= 0 && vbat_bif != 0 &&
			    vbat_bif < vbat_auxadc) {
				vbat = vbat_bif;
				chg_err("%s: use BIF vbat = %duV, dV to auxadc = %duV\n",
					__func__, vbat, vbat_auxadc - vbat_bif);
				break;
			}
			retry_cnt++;
		} while (retry_cnt < 5);

		if (retry_cnt == 5) {
			ret = 0;
			vbat = vbat_auxadc;
			chg_err("%s: use AUXADC vbat = %duV, since BIF vbat = %duV\n",
				__func__, vbat_auxadc, vbat_bif);
		}

		/* Adjust CV according to the obtained vbat */
		vbat_threshold[1] = info->data.bif_threshold1;
		vbat_threshold[2] = info->data.bif_threshold2;
		_cv_temp = info->data.bif_cv_under_threshold2;

		if (!info->enable_dynamic_cv && vbat >= vbat_threshold[2]) {
			_cv = info->data.battery_cv;
			goto out;
		}

		if (vbat < vbat_threshold[1])
			_cv = 4608000;
		else if (vbat >= vbat_threshold[1] && vbat < vbat_threshold[2])
			_cv = _cv_temp;
		else {
			_cv = info->data.battery_cv;
			info->enable_dynamic_cv = false;
		}
out:
		*cv = _cv;
		chg_err("%s: CV = %duV, enable_dynamic_cv = %d\n",
			__func__, _cv, info->enable_dynamic_cv);
	} else
		ret = -ENOTSUPP;

#endif
	return ret;
}

static int mtk_charger_notifier(struct mtk_charger *info, int event)
{
	return srcu_notifier_call_chain(&info->evt_nh, event, NULL);
}

int notify_battery_full(void)
{
	printk("notify_battery_full_is_ok\n");

	if (mtk_charger_notifier(pinfo, CHARGER_NOTIFY_EOC)) {
		printk("notifier fail\n");
		return 1;
	} else {
		return 0;
	}
}

int charger_psy_event(struct notifier_block *nb, unsigned long event, void *v)
{
	struct mtk_charger *info =
			container_of(nb, struct mtk_charger, psy_nb);
	struct power_supply *psy = v;
	union power_supply_propval val;
	int ret;
	int tmp = 0;

	if (strcmp(psy->desc->name, "battery") == 0) {
		ret = power_supply_get_property(psy,
				POWER_SUPPLY_PROP_TEMP, &val);
		if (!ret) {
			tmp = val.intval / 10;
			if (info->battery_temp != tmp
			    && mt_get_charger_type() != CHARGER_UNKNOWN) {
				_wake_up_charger(info);
				chg_err("%s: %ld %s tmp:%d %d chr:%d\n",
					__func__, event, psy->desc->name, tmp,
					info->battery_temp,
					mt_get_charger_type());
			}
		}
	}

	return NOTIFY_DONE;
}

void mtk_charger_int_handler(void)
{
	if (pinfo == NULL) {
		chg_err("charger is not rdy ,skip1\n");
		return;
	}

	if (pinfo->init_done != true) {
		chg_err("charger is not rdy ,skip2\n");
		return;
	}

	if (mt_get_charger_type() == CHARGER_UNKNOWN) {
		mutex_lock(&pinfo->cable_out_lock);
		pinfo->cable_out_cnt++;
		chg_err("cable_out_cnt=%d\n", pinfo->cable_out_cnt);
		mutex_unlock(&pinfo->cable_out_lock);
		mtk_charger_notifier(pinfo, CHARGER_NOTIFY_STOP_CHARGING);
	} else {
#ifdef CONFIG_OPLUS_CHARGER_MTK6765S
		oplus_wake_up_usbtemp_thread();
#endif /*CONFIG_OPLUS_CHARGER_MTK6765S*/
		mtk_charger_notifier(pinfo, CHARGER_NOTIFY_START_CHARGING);
	}
	chg_err("wake_up_charger\n");
	_wake_up_charger(pinfo);
}

#ifdef CONFIG_PM
static int charger_pm_event(struct notifier_block *notifier,
			unsigned long pm_event, void *unused)
{
	struct timespec now;

	switch (pm_event) {
	case PM_SUSPEND_PREPARE:
		pinfo->is_suspend = true;
		chr_debug("%s: enter PM_SUSPEND_PREPARE\n", __func__);
		break;
	case PM_POST_SUSPEND:
		pinfo->is_suspend = false;
		chr_debug("%s: enter PM_POST_SUSPEND\n", __func__);
		get_monotonic_boottime(&now);

		if (timespec_compare(&now, &pinfo->endtime) >= 0 &&
			pinfo->endtime.tv_sec != 0 &&
			pinfo->endtime.tv_nsec != 0) {
			chg_err("%s: alarm timeout, wake up charger\n",
				__func__);
			/* __pm_relax(&pinfo->charger_wakelock); */
			pinfo->endtime.tv_sec = 0;
			pinfo->endtime.tv_nsec = 0;
			_wake_up_charger(pinfo);
		}
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block charger_pm_notifier_func = {
	.notifier_call = charger_pm_event,
	.priority = 0,
};
#endif /* CONFIG_PM */

static enum alarmtimer_restart
	mtk_charger_alarm_timer_func(struct alarm *alarm, ktime_t now)
{
	struct mtk_charger *info =
	container_of(alarm, struct mtk_charger, charger_timer);

	if (info->is_suspend == false) {
		chg_err("%s: not suspend, wake up charger\n", __func__);
		_wake_up_charger(info);
	} else {
		chg_err("%s: alarm timer timeout\n", __func__);
		/* __pm_stay_awake(&info->charger_wakelock); */
	}

	return ALARMTIMER_NORESTART;
}

static void mtk_charger_start_timer(struct mtk_charger *info)
{
	struct timespec time, time_now;
	ktime_t ktime;

	get_monotonic_boottime(&time_now);
	time.tv_sec = info->polling_interval;
	time.tv_nsec = 0;
	info->endtime = timespec_add(time_now, time);

	ktime = ktime_set(info->endtime.tv_sec, info->endtime.tv_nsec);

	chg_err("%s: alarm timer start:%ld %ld\n", __func__,
		info->endtime.tv_sec, info->endtime.tv_nsec);
	alarm_start(&pinfo->charger_timer, ktime);
}

static void mtk_charger_init_timer(struct mtk_charger *info)
{
	alarm_init(&info->charger_timer, ALARM_BOOTTIME,
			mtk_charger_alarm_timer_func);
	mtk_charger_start_timer(info);

#ifdef CONFIG_PM
	if (register_pm_notifier(&charger_pm_notifier_func))
		chg_err("%s: register pm failed\n", __func__);
#endif /* CONFIG_PM */
}

static int mtk_charger_parse_dt(struct mtk_charger *info,
				struct device *dev)
{
	struct device_node *np = dev->of_node;
	u32 val;

	chg_err("%s: starts\n", __func__);

	if (!np) {
		chg_err("%s: no device node\n", __func__);
		return -EINVAL;
	}

	if (of_property_read_string(np, "algorithm_name",
		&info->algorithm_name) < 0) {
		chg_err("%s: no algorithm_name name\n", __func__);
		info->algorithm_name = "SwitchCharging";
	}

#ifdef CONFIG_MTK_DUAL_CHARGER_SUPPORT
	if (strcmp(info->algorithm_name, "DualSwitchCharging") == 0) {
		pr_debug("found DualSwitchCharging\n");
		mtk_dual_switch_charging_init(info);
	}
#endif

	info->disable_charger = of_property_read_bool(np, "disable_charger");
	info->enable_sw_safety_timer =
			of_property_read_bool(np, "enable_sw_safety_timer");
	info->sw_safety_timer_setting = info->enable_sw_safety_timer;
	info->enable_sw_jeita = of_property_read_bool(np, "enable_sw_jeita");

	info->enable_type_c = of_property_read_bool(np, "enable_type_c");
	info->enable_dynamic_mivr =
			of_property_read_bool(np, "enable_dynamic_mivr");
	info->disable_pd_dual = of_property_read_bool(np, "disable_pd_dual");

	info->enable_hv_charging = true;

	/* common */
	if (of_property_read_u32(np, "battery_cv", &val) >= 0)
		info->data.battery_cv = val;
	else {
		chg_err("use default BATTERY_CV:%d\n", BATTERY_CV);
		info->data.battery_cv = BATTERY_CV;
	}

	if (of_property_read_u32(np, "max_charger_voltage", &val) >= 0)
		info->data.max_charger_voltage = val;
	else {
		chg_err("use default V_CHARGER_MAX:%d\n", V_CHARGER_MAX);
		info->data.max_charger_voltage = V_CHARGER_MAX;
	}
	info->data.max_charger_voltage_setting = info->data.max_charger_voltage;

	if (of_property_read_u32(np, "min_charger_voltage", &val) >= 0)
		info->data.min_charger_voltage = val;
	else {
		chg_err("use default V_CHARGER_MIN:%d\n", V_CHARGER_MIN);
		info->data.min_charger_voltage = V_CHARGER_MIN;
	}

	/* dynamic mivr */
	if (of_property_read_u32(np, "min_charger_voltage_1", &val) >= 0)
		info->data.min_charger_voltage_1 = val;
	else {
		chg_err("use default V_CHARGER_MIN_1:%d\n", V_CHARGER_MIN_1);
		info->data.min_charger_voltage_1 = V_CHARGER_MIN_1;
	}

	if (of_property_read_u32(np, "min_charger_voltage_2", &val) >= 0)
		info->data.min_charger_voltage_2 = val;
	else {
		chg_err("use default V_CHARGER_MIN_2:%d\n", V_CHARGER_MIN_2);
		info->data.min_charger_voltage_2 = V_CHARGER_MIN_2;
	}

	if (of_property_read_u32(np, "max_dmivr_charger_current", &val) >= 0)
		info->data.max_dmivr_charger_current = val;
	else {
		chg_err("use default MAX_DMIVR_CHARGER_CURRENT:%d\n",
			MAX_DMIVR_CHARGER_CURRENT);
		info->data.max_dmivr_charger_current =
					MAX_DMIVR_CHARGER_CURRENT;
	}

	/* charging current */
	if (of_property_read_u32(np, "usb_charger_current_suspend", &val) >= 0)
		info->data.usb_charger_current_suspend = val;
	else {
		chg_err("use default USB_CHARGER_CURRENT_SUSPEND:%d\n",
			USB_CHARGER_CURRENT_SUSPEND);
		info->data.usb_charger_current_suspend =
						USB_CHARGER_CURRENT_SUSPEND;
	}

	if (of_property_read_u32(np, "usb_charger_current_unconfigured", &val)
		>= 0) {
		info->data.usb_charger_current_unconfigured = val;
	} else {
		chg_err("use default USB_CHARGER_CURRENT_UNCONFIGURED:%d\n",
			USB_CHARGER_CURRENT_UNCONFIGURED);
		info->data.usb_charger_current_unconfigured =
					USB_CHARGER_CURRENT_UNCONFIGURED;
	}

	if (of_property_read_u32(np, "usb_charger_current_configured", &val)
		>= 0) {
		info->data.usb_charger_current_configured = val;
	} else {
		chg_err("use default USB_CHARGER_CURRENT_CONFIGURED:%d\n",
			USB_CHARGER_CURRENT_CONFIGURED);
		info->data.usb_charger_current_configured =
					USB_CHARGER_CURRENT_CONFIGURED;
	}

	if (of_property_read_u32(np, "usb_charger_current", &val) >= 0) {
		info->data.usb_charger_current = val;
	} else {
		chg_err("use default USB_CHARGER_CURRENT:%d\n",
			USB_CHARGER_CURRENT);
		info->data.usb_charger_current = USB_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ac_charger_current", &val) >= 0) {
		info->data.ac_charger_current = val;
	} else {
		chg_err("use default AC_CHARGER_CURRENT:%d\n",
			AC_CHARGER_CURRENT);
		info->data.ac_charger_current = AC_CHARGER_CURRENT;
	}

	info->data.pd_charger_current = 3000000;

	if (of_property_read_u32(np, "ac_charger_input_current", &val) >= 0)
		info->data.ac_charger_input_current = val;
	else {
		chg_err("use default AC_CHARGER_INPUT_CURRENT:%d\n",
			AC_CHARGER_INPUT_CURRENT);
		info->data.ac_charger_input_current = AC_CHARGER_INPUT_CURRENT;
	}

	if (of_property_read_u32(np, "non_std_ac_charger_current", &val) >= 0)
		info->data.non_std_ac_charger_current = val;
	else {
		chg_err("use default NON_STD_AC_CHARGER_CURRENT:%d\n",
			NON_STD_AC_CHARGER_CURRENT);
		info->data.non_std_ac_charger_current =
					NON_STD_AC_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "charging_host_charger_current", &val)
		>= 0) {
		info->data.charging_host_charger_current = val;
	} else {
		chg_err("use default CHARGING_HOST_CHARGER_CURRENT:%d\n",
			CHARGING_HOST_CHARGER_CURRENT);
		info->data.charging_host_charger_current =
					CHARGING_HOST_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "apple_1_0a_charger_current", &val) >= 0)
		info->data.apple_1_0a_charger_current = val;
	else {
		chg_err("use default APPLE_1_0A_CHARGER_CURRENT:%d\n",
			APPLE_1_0A_CHARGER_CURRENT);
		info->data.apple_1_0a_charger_current =
					APPLE_1_0A_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "apple_2_1a_charger_current", &val) >= 0)
		info->data.apple_2_1a_charger_current = val;
	else {
		chg_err("use default APPLE_2_1A_CHARGER_CURRENT:%d\n",
			APPLE_2_1A_CHARGER_CURRENT);
		info->data.apple_2_1a_charger_current =
					APPLE_2_1A_CHARGER_CURRENT;
	}

	if (of_property_read_u32(np, "ta_ac_charger_current", &val) >= 0)
		info->data.ta_ac_charger_current = val;
	else {
		chg_err("use default TA_AC_CHARGING_CURRENT:%d\n",
			TA_AC_CHARGING_CURRENT);
		info->data.ta_ac_charger_current =
					TA_AC_CHARGING_CURRENT;
	}

	/* sw jeita */
	if (of_property_read_u32(np, "jeita_temp_above_t4_cv", &val) >= 0)
		info->data.jeita_temp_above_t4_cv = val;
	else {
		chg_err("use default JEITA_TEMP_ABOVE_T4_CV:%d\n",
			JEITA_TEMP_ABOVE_T4_CV);
		info->data.jeita_temp_above_t4_cv = JEITA_TEMP_ABOVE_T4_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t3_to_t4_cv", &val) >= 0)
		info->data.jeita_temp_t3_to_t4_cv = val;
	else {
		chg_err("use default JEITA_TEMP_T3_TO_T4_CV:%d\n",
			JEITA_TEMP_T3_TO_T4_CV);
		info->data.jeita_temp_t3_to_t4_cv = JEITA_TEMP_T3_TO_T4_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t2_to_t3_cv", &val) >= 0)
		info->data.jeita_temp_t2_to_t3_cv = val;
	else {
		chg_err("use default JEITA_TEMP_T2_TO_T3_CV:%d\n",
			JEITA_TEMP_T2_TO_T3_CV);
		info->data.jeita_temp_t2_to_t3_cv = JEITA_TEMP_T2_TO_T3_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t1_to_t2_cv", &val) >= 0)
		info->data.jeita_temp_t1_to_t2_cv = val;
	else {
		chg_err("use default JEITA_TEMP_T1_TO_T2_CV:%d\n",
			JEITA_TEMP_T1_TO_T2_CV);
		info->data.jeita_temp_t1_to_t2_cv = JEITA_TEMP_T1_TO_T2_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_t0_to_t1_cv", &val) >= 0)
		info->data.jeita_temp_t0_to_t1_cv = val;
	else {
		chg_err("use default JEITA_TEMP_T0_TO_T1_CV:%d\n",
			JEITA_TEMP_T0_TO_T1_CV);
		info->data.jeita_temp_t0_to_t1_cv = JEITA_TEMP_T0_TO_T1_CV;
	}

	if (of_property_read_u32(np, "jeita_temp_below_t0_cv", &val) >= 0)
		info->data.jeita_temp_below_t0_cv = val;
	else {
		chg_err("use default JEITA_TEMP_BELOW_T0_CV:%d\n",
			JEITA_TEMP_BELOW_T0_CV);
		info->data.jeita_temp_below_t0_cv = JEITA_TEMP_BELOW_T0_CV;
	}

	if (of_property_read_u32(np, "temp_t4_thres", &val) >= 0)
		info->data.temp_t4_thres = val;
	else {
		chg_err("use default TEMP_T4_THRES:%d\n",
			TEMP_T4_THRES);
		info->data.temp_t4_thres = TEMP_T4_THRES;
	}

	if (of_property_read_u32(np, "temp_t4_thres_minus_x_degree", &val) >= 0)
		info->data.temp_t4_thres_minus_x_degree = val;
	else {
		chg_err("use default TEMP_T4_THRES_MINUS_X_DEGREE:%d\n",
			TEMP_T4_THRES_MINUS_X_DEGREE);
		info->data.temp_t4_thres_minus_x_degree =
					TEMP_T4_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t3_thres", &val) >= 0)
		info->data.temp_t3_thres = val;
	else {
		chg_err("use default TEMP_T3_THRES:%d\n",
			TEMP_T3_THRES);
		info->data.temp_t3_thres = TEMP_T3_THRES;
	}

	if (of_property_read_u32(np, "temp_t3_thres_minus_x_degree", &val) >= 0)
		info->data.temp_t3_thres_minus_x_degree = val;
	else {
		chg_err("use default TEMP_T3_THRES_MINUS_X_DEGREE:%d\n",
			TEMP_T3_THRES_MINUS_X_DEGREE);
		info->data.temp_t3_thres_minus_x_degree =
					TEMP_T3_THRES_MINUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t2_thres", &val) >= 0)
		info->data.temp_t2_thres = val;
	else {
		chg_err("use default TEMP_T2_THRES:%d\n",
			TEMP_T2_THRES);
		info->data.temp_t2_thres = TEMP_T2_THRES;
	}

	if (of_property_read_u32(np, "temp_t2_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t2_thres_plus_x_degree = val;
	else {
		chg_err("use default TEMP_T2_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T2_THRES_PLUS_X_DEGREE);
		info->data.temp_t2_thres_plus_x_degree =
					TEMP_T2_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t1_thres", &val) >= 0)
		info->data.temp_t1_thres = val;
	else {
		chg_err("use default TEMP_T1_THRES:%d\n",
			TEMP_T1_THRES);
		info->data.temp_t1_thres = TEMP_T1_THRES;
	}

	if (of_property_read_u32(np, "temp_t1_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t1_thres_plus_x_degree = val;
	else {
		chg_err("use default TEMP_T1_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T1_THRES_PLUS_X_DEGREE);
		info->data.temp_t1_thres_plus_x_degree =
					TEMP_T1_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_t0_thres", &val) >= 0)
		info->data.temp_t0_thres = val;
	else {
		chg_err("use default TEMP_T0_THRES:%d\n",
			TEMP_T0_THRES);
		info->data.temp_t0_thres = TEMP_T0_THRES;
	}

	if (of_property_read_u32(np, "temp_t0_thres_plus_x_degree", &val) >= 0)
		info->data.temp_t0_thres_plus_x_degree = val;
	else {
		chg_err("use default TEMP_T0_THRES_PLUS_X_DEGREE:%d\n",
			TEMP_T0_THRES_PLUS_X_DEGREE);
		info->data.temp_t0_thres_plus_x_degree =
					TEMP_T0_THRES_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "temp_neg_10_thres", &val) >= 0)
		info->data.temp_neg_10_thres = val;
	else {
		chg_err("use default TEMP_NEG_10_THRES:%d\n",
			TEMP_NEG_10_THRES);
		info->data.temp_neg_10_thres = TEMP_NEG_10_THRES;
	}

	/* battery temperature protection */
	info->thermal.sm = BAT_TEMP_NORMAL;
	info->thermal.enable_min_charge_temp =
		of_property_read_bool(np, "enable_min_charge_temp");

	if (of_property_read_u32(np, "min_charge_temp", &val) >= 0)
		info->thermal.min_charge_temp = val;
	else {
		chg_err("use default MIN_CHARGE_TEMP:%d\n",
			MIN_CHARGE_TEMP);
		info->thermal.min_charge_temp = MIN_CHARGE_TEMP;
	}

	if (of_property_read_u32(np, "min_charge_temp_plus_x_degree", &val)
	    >= 0) {
		info->thermal.min_charge_temp_plus_x_degree = val;
	} else {
		chg_err("use default MIN_CHARGE_TEMP_PLUS_X_DEGREE:%d\n",
			MIN_CHARGE_TEMP_PLUS_X_DEGREE);
		info->thermal.min_charge_temp_plus_x_degree =
					MIN_CHARGE_TEMP_PLUS_X_DEGREE;
	}

	if (of_property_read_u32(np, "max_charge_temp", &val) >= 0)
		info->thermal.max_charge_temp = val;
	else {
		chg_err("use default MAX_CHARGE_TEMP:%d\n",
			MAX_CHARGE_TEMP);
		info->thermal.max_charge_temp = MAX_CHARGE_TEMP;
	}

	if (of_property_read_u32(np, "max_charge_temp_minus_x_degree", &val)
	    >= 0) {
		info->thermal.max_charge_temp_minus_x_degree = val;
	} else {
		chg_err("use default MAX_CHARGE_TEMP_MINUS_X_DEGREE:%d\n",
			MAX_CHARGE_TEMP_MINUS_X_DEGREE);
		info->thermal.max_charge_temp_minus_x_degree =
					MAX_CHARGE_TEMP_MINUS_X_DEGREE;
	}

	/* PD */
	if (of_property_read_u32(np, "pd_vbus_upper_bound", &val) >= 0) {
		info->data.pd_vbus_upper_bound = val;
	} else {
		chg_err("use default pd_vbus_upper_bound:%d\n",
			PD_VBUS_UPPER_BOUND);
		info->data.pd_vbus_upper_bound = PD_VBUS_UPPER_BOUND;
	}

	if (of_property_read_u32(np, "pd_vbus_low_bound", &val) >= 0) {
		info->data.pd_vbus_low_bound = val;
	} else {
		chg_err("use default pd_vbus_low_bound:%d\n",
			PD_VBUS_LOW_BOUND);
		info->data.pd_vbus_low_bound = PD_VBUS_LOW_BOUND;
	}

	if (of_property_read_u32(np, "pd_ichg_level_threshold", &val) >= 0)
		info->data.pd_ichg_level_threshold = val;
	else {
		chg_err("use default pd_ichg_level_threshold:%d\n",
			PD_ICHG_LEAVE_THRESHOLD);
		info->data.pd_ichg_level_threshold = PD_ICHG_LEAVE_THRESHOLD;
	}

	if (of_property_read_u32(np, "pd_stop_battery_soc", &val) >= 0)
		info->data.pd_stop_battery_soc = val;
	else {
		chg_err("use default pd_stop_battery_soc:%d\n",
			PD_STOP_BATTERY_SOC);
		info->data.pd_stop_battery_soc = PD_STOP_BATTERY_SOC;
	}

	if (of_property_read_u32(np, "vsys_watt", &val) >= 0) {
		info->data.vsys_watt = val;
	} else {
		chg_err("use default vsys_watt:%d\n",
			VSYS_WATT);
		info->data.vsys_watt = VSYS_WATT;
	}

	if (of_property_read_u32(np, "ibus_err", &val) >= 0) {
		info->data.ibus_err = val;
	} else {
		chg_err("use default ibus_err:%d\n",
			IBUS_ERR);
		info->data.ibus_err = IBUS_ERR;
	}

	/* dual charger */
	if (of_property_read_u32(np, "chg1_ta_ac_charger_current", &val) >= 0)
		info->data.chg1_ta_ac_charger_current = val;
	else {
		chg_err("use default TA_AC_MASTER_CHARGING_CURRENT:%d\n",
			TA_AC_MASTER_CHARGING_CURRENT);
		info->data.chg1_ta_ac_charger_current =
						TA_AC_MASTER_CHARGING_CURRENT;
	}

	if (of_property_read_u32(np, "chg2_ta_ac_charger_current", &val) >= 0)
		info->data.chg2_ta_ac_charger_current = val;
	else {
		chg_err("use default TA_AC_SLAVE_CHARGING_CURRENT:%d\n",
			TA_AC_SLAVE_CHARGING_CURRENT);
		info->data.chg2_ta_ac_charger_current =
						TA_AC_SLAVE_CHARGING_CURRENT;
	}

	if (of_property_read_u32(np, "slave_mivr_diff", &val) >= 0)
		info->data.slave_mivr_diff = val;
	else {
		chg_err("use default SLAVE_MIVR_DIFF:%d\n", SLAVE_MIVR_DIFF);
		info->data.slave_mivr_diff = SLAVE_MIVR_DIFF;
	}

	/* slave charger */
	if (of_property_read_u32(np, "chg2_eff", &val) >= 0)
		info->data.chg2_eff = val;
	else {
		chg_err("use default CHG2_EFF:%d\n", CHG2_EFF);
		info->data.chg2_eff = CHG2_EFF;
	}

	info->data.parallel_vbus = of_property_read_bool(np, "parallel_vbus");

	/* cable measurement impedance */
	if (of_property_read_u32(np, "cable_imp_threshold", &val) >= 0)
		info->data.cable_imp_threshold = val;
	else {
		chg_err("use default CABLE_IMP_THRESHOLD:%d\n",
			CABLE_IMP_THRESHOLD);
		info->data.cable_imp_threshold = CABLE_IMP_THRESHOLD;
	}

	if (of_property_read_u32(np, "vbat_cable_imp_threshold", &val) >= 0)
		info->data.vbat_cable_imp_threshold = val;
	else {
		chg_err("use default VBAT_CABLE_IMP_THRESHOLD:%d\n",
			VBAT_CABLE_IMP_THRESHOLD);
		info->data.vbat_cable_imp_threshold = VBAT_CABLE_IMP_THRESHOLD;
	}

	/* BIF */
	if (of_property_read_u32(np, "bif_threshold1", &val) >= 0)
		info->data.bif_threshold1 = val;
	else {
		chg_err("use default BIF_THRESHOLD1:%d\n",
			BIF_THRESHOLD1);
		info->data.bif_threshold1 = BIF_THRESHOLD1;
	}

	if (of_property_read_u32(np, "bif_threshold2", &val) >= 0)
		info->data.bif_threshold2 = val;
	else {
		chg_err("use default BIF_THRESHOLD2:%d\n",
			BIF_THRESHOLD2);
		info->data.bif_threshold2 = BIF_THRESHOLD2;
	}

	if (of_property_read_u32(np, "bif_cv_under_threshold2", &val) >= 0)
		info->data.bif_cv_under_threshold2 = val;
	else {
		chg_err("use default BIF_CV_UNDER_THRESHOLD2:%d\n",
			BIF_CV_UNDER_THRESHOLD2);
		info->data.bif_cv_under_threshold2 = BIF_CV_UNDER_THRESHOLD2;
	}

	info->data.power_path_support =
				of_property_read_bool(np, "power_path_support");
	chr_debug("%s: power_path_support: %d\n",
		__func__, info->data.power_path_support);

	if (of_property_read_u32(np, "max_charging_time", &val) >= 0)
		info->data.max_charging_time = val;
	else {
		chg_err("use default MAX_CHARGING_TIME:%d\n",
			MAX_CHARGING_TIME);
		info->data.max_charging_time = MAX_CHARGING_TIME;
	}

	if (of_property_read_u32(np, "bc12_charger", &val) >= 0)
		info->data.bc12_charger = val;
	else {
		chg_err("use default BC12_CHARGER:%d\n",
			DEFAULT_BC12_CHARGER);
		info->data.bc12_charger = DEFAULT_BC12_CHARGER;
	}

	chg_err("algorithm name:%s\n", info->algorithm_name);

	info->support_ntc_01c_precision = of_property_read_bool(np, "qcom,support_ntc_01c_precision");
	chr_err("%s: support_ntc_01c_precision: %d\n", __func__, info->support_ntc_01c_precision);

	return 0;
}

#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
static ssize_t show_Pump_Express(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;
	int is_ta_detected = 0;

	pr_debug("[%s] chr_type:%d UISOC:%d startsoc:%d stopsoc:%d\n", __func__,
		mt_get_charger_type(), battery_get_uisoc(),
		pinfo->data.ta_start_battery_soc,
		pinfo->data.ta_stop_battery_soc);

	if (IS_ENABLED(CONFIG_MTK_PUMP_EXPRESS_PLUS_20_SUPPORT)) {
		/* Is PE+20 connect */
		if (mtk_pe20_get_is_connect(pinfo))
			is_ta_detected = 1;
	}

	if (IS_ENABLED(CONFIG_MTK_PUMP_EXPRESS_PLUS_SUPPORT)) {
		/* Is PE+ connect */
		if (mtk_pe_get_is_connect(pinfo))
			is_ta_detected = 1;
	}

	if (mtk_is_TA_support_pd_pps(pinfo) == true)
		is_ta_detected = 1;

	pr_debug("%s: detected = %d, pe20_connect = %d, pe_connect = %d\n",
		__func__, is_ta_detected,
		mtk_pe20_get_is_connect(pinfo),
		mtk_pe_get_is_connect(pinfo));

	return sprintf(buf, "%u\n", is_ta_detected);
}

static DEVICE_ATTR(Pump_Express, 0444, show_Pump_Express, NULL);
#endif

static ssize_t show_input_current(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	pr_debug("[Battery] %s : %x\n",
		__func__, pinfo->chg_data[CHG1_SETTING].thermal_input_current_limit);
	return sprintf(buf, "%u\n",
			pinfo->chg_data[CHG1_SETTING].thermal_input_current_limit);
}

static ssize_t store_input_current(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] %s\n", __func__);
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->chg_data[CHG1_SETTING].thermal_input_current_limit = reg;
		if (pinfo->data.parallel_vbus)
			pinfo->chg_data[CHG2_SETTING].thermal_input_current_limit = reg;
		pr_debug("[Battery] %s: %x\n",
			__func__, pinfo->chg_data[CHG1_SETTING].thermal_input_current_limit);
	}
	return size;
}
static DEVICE_ATTR(input_current, 0664, show_input_current,
		store_input_current);

static ssize_t show_chg1_current(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	pr_debug("[Battery] %s : %x\n",
		__func__, pinfo->chg_data[CHG1_SETTING].thermal_charging_current_limit);
	return sprintf(buf, "%u\n",
			pinfo->chg_data[CHG1_SETTING].thermal_charging_current_limit);
}

static ssize_t store_chg1_current(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] %s\n", __func__);
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->chg_data[CHG1_SETTING].thermal_charging_current_limit = reg;
		pr_debug("[Battery] %s: %x\n", __func__,
			pinfo->chg_data[CHG1_SETTING].thermal_charging_current_limit);
	}
	return size;
}
static DEVICE_ATTR(chg1_current, 0664, show_chg1_current, store_chg1_current);

static ssize_t show_chg2_current(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	pr_debug("[Battery] %s : %x\n",
		__func__, pinfo->chg_data[CHG2_SETTING].thermal_charging_current_limit);
	return sprintf(buf, "%u\n",
			pinfo->chg_data[CHG2_SETTING].thermal_charging_current_limit);
}

static ssize_t store_chg2_current(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] %s\n", __func__);
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->chg_data[CHG2_SETTING].thermal_charging_current_limit = reg;
		pr_debug("[Battery] %s: %x\n", __func__,
			pinfo->chg_data[CHG2_SETTING].thermal_charging_current_limit);
	}
	return size;
}
static DEVICE_ATTR(chg2_current, 0664, show_chg2_current, store_chg2_current);

static ssize_t show_BN_TestMode(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct mtk_charger *pinfo = dev->driver_data;

	pr_debug("[Battery] %s : %x\n", __func__, pinfo->notify_test_mode);
	return sprintf(buf, "%u\n", pinfo->notify_test_mode);
}

static ssize_t store_BN_TestMode(struct device *dev,
		struct device_attribute *attr, const char *buf,  size_t size)
{
	struct mtk_charger *pinfo = dev->driver_data;
	unsigned int reg = 0;
	int ret;

	pr_debug("[Battery] %s\n", __func__);
	if (buf != NULL && size != 0) {
		pr_debug("[Battery] buf is %s and size is %zu\n", buf, size);
		ret = kstrtouint(buf, 16, &reg);
		pinfo->notify_test_mode = reg;
		pr_debug("[Battery] store mode: %x\n", pinfo->notify_test_mode);
	}
	return size;
}
static DEVICE_ATTR(BN_TestMode, 0664, show_BN_TestMode, store_BN_TestMode);

static ssize_t show_ADC_Charger_Voltage(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int vbus = battery_meter_get_charger_voltage();

	if (!atomic_read(&pinfo->enable_kpoc_shdn) || vbus < 0) {
		chg_err("HardReset or get vbus failed, vbus:%d:5000\n", vbus);
		vbus = 5000;
	}

	pr_debug("[%s]: %d\n", __func__, vbus);
	return sprintf(buf, "%d\n", vbus);
}

static DEVICE_ATTR(ADC_Charger_Voltage, 0444, show_ADC_Charger_Voltage, NULL);

static int mtk_chg_current_cmd_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;

	seq_printf(m, "%d %d\n", pinfo->usb_unlimited, pinfo->cmd_discharging);
	return 0;
}

static ssize_t mtk_chg_current_cmd_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0;
	char desc[32];
	int current_unlimited = 0;
	int cmd_discharging = 0;
	struct mtk_charger *info = PDE_DATA(file_inode(file));

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return 0;

	desc[len] = '\0';

	if (sscanf(desc, "%d %d", &current_unlimited, &cmd_discharging) == 2) {
		info->usb_unlimited = current_unlimited;
		if (cmd_discharging == 1) {
			info->cmd_discharging = true;
			charger_dev_enable(info->chg1_dev, false);
			mtk_charger_notifier(info,
						CHARGER_NOTIFY_STOP_CHARGING);
		} else if (cmd_discharging == 0) {
			info->cmd_discharging = false;
			charger_dev_enable(info->chg1_dev, true);
			mtk_charger_notifier(info,
						CHARGER_NOTIFY_START_CHARGING);
		}

		pr_debug("%s current_unlimited=%d, cmd_discharging=%d\n",
			__func__, current_unlimited, cmd_discharging);
		return count;
	}

	chg_err("bad argument, echo [usb_unlimited] [disable] > current_cmd\n");
	return count;
}

static int mtk_chg_en_power_path_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;
	bool power_path_en = true;

	charger_dev_is_powerpath_enabled(pinfo->chg1_dev, &power_path_en);
	seq_printf(m, "%d\n", power_path_en);

	return 0;
}

static ssize_t mtk_chg_en_power_path_write(struct file *file,
		const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32];
	unsigned int enable = 0;
	struct mtk_charger *info = PDE_DATA(file_inode(file));


	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return 0;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_powerpath(info->chg1_dev, enable);
		pr_debug("%s: enable power path = %d\n", __func__, enable);
		return count;
	}

	chg_err("bad argument, echo [enable] > en_power_path\n");
	return count;
}

static int mtk_chg_en_safety_timer_show(struct seq_file *m, void *data)
{
	struct mtk_charger *pinfo = m->private;
	bool safety_timer_en = false;

	charger_dev_is_safety_timer_enabled(pinfo->chg1_dev, &safety_timer_en);
	seq_printf(m, "%d\n", safety_timer_en);

	return 0;
}

static ssize_t mtk_chg_en_safety_timer_write(struct file *file,
	const char *buffer, size_t count, loff_t *data)
{
	int len = 0, ret = 0;
	char desc[32];
	unsigned int enable = 0;
	struct mtk_charger *info = PDE_DATA(file_inode(file));

	len = (count < (sizeof(desc) - 1)) ? count : (sizeof(desc) - 1);
	if (copy_from_user(desc, buffer, len))
		return 0;

	desc[len] = '\0';

	ret = kstrtou32(desc, 10, &enable);
	if (ret == 0) {
		charger_dev_enable_safety_timer(info->chg1_dev, enable);
		pr_debug("%s: enable safety timer = %d\n", __func__, enable);

		/* SW safety timer */
		if (info->sw_safety_timer_setting == true) {
			if (enable)
				info->enable_sw_safety_timer = true;
			else
				info->enable_sw_safety_timer = false;
		}

		return count;
	}

	chg_err("bad argument, echo [enable] > en_safety_timer\n");
	return count;
}

/* PROC_FOPS_RW(battery_cmd); */
/* PROC_FOPS_RW(discharging_cmd); */
PROC_FOPS_RW(current_cmd);
PROC_FOPS_RW(en_power_path);
PROC_FOPS_RW(en_safety_timer);

static int proc_dump_log_show(struct seq_file *m, void *v)
{
	struct adapter_power_cap cap;
	int i;

	cap.nr = 0;
	cap.pdp = 0;
	for (i = 0; i < ADAPTER_CAP_MAX_NR; i++) {
		cap.max_mv[i] = 0;
		cap.min_mv[i] = 0;
		cap.ma[i] = 0;
		cap.type[i] = 0;
		cap.pwr_limit[i] = 0;
	}

	if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_APDO) {
		seq_puts(m, "********** PD APDO cap Dump **********\n");

		adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD_APDO, &cap);
		for (i = 0; i < cap.nr; i++) {
			seq_printf(m,
			"%d: mV:%d,%d mA:%d type:%d pwr_limit:%d pdp:%d\n", i,
			cap.max_mv[i], cap.min_mv[i], cap.ma[i],
			cap.type[i], cap.pwr_limit[i], cap.pdp);
		}
	} else if (pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK
		|| pinfo->pd_type == MTK_PD_CONNECT_PE_READY_SNK_PD30) {
		seq_puts(m, "********** PD cap Dump **********\n");

		adapter_dev_get_cap(pinfo->pd_adapter, MTK_PD, &cap);
		for (i = 0; i < cap.nr; i++) {
			seq_printf(m, "%d: mV:%d,%d mA:%d type:%d\n", i,
			cap.max_mv[i], cap.min_mv[i], cap.ma[i], cap.type[i]);
		}
	}

	return 0;
}

static ssize_t proc_write(
	struct file *file, const char __user *buffer,
	size_t count, loff_t *f_pos)
{
	return count;
}


static int proc_dump_log_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_dump_log_show, NULL);
}

static const struct file_operations charger_dump_log_proc_fops = {
	.open = proc_dump_log_open,
	.read = seq_read,
	.llseek	= seq_lseek,
	.write = proc_write,
};

void charger_debug_init(void)
{
	struct proc_dir_entry *charger_dir;

	charger_dir = proc_mkdir("charger", NULL);
	if (!charger_dir) {
		chg_err("fail to mkdir /proc/charger\n");
		return;
	}

	proc_create("dump_log", 0644,
		charger_dir, &charger_dump_log_proc_fops);
}

int oplus_get_typec_cc_orientation(void)
{
	int typec_cc_orientation = 0;
	static struct tcpc_device *tcpc_dev = NULL;

	if (tcpc_dev == NULL)
		tcpc_dev = tcpc_dev_get_by_name("type_c_port0");

	if (tcpc_dev != NULL) {
		if (tcpm_inquire_typec_attach_state(tcpc_dev) != TYPEC_UNATTACHED) {
			typec_cc_orientation = (int)tcpm_inquire_cc_polarity(tcpc_dev) + 1;
		} else {
			typec_cc_orientation = 0;
		}
		if (typec_cc_orientation != 0)
			printk(KERN_ERR "[OPLUS_CHG][%s]: cc[%d]\n", __func__, typec_cc_orientation);
	} else {
		typec_cc_orientation = 0;
	}
	return typec_cc_orientation;
}
EXPORT_SYMBOL(oplus_get_typec_cc_orientation);

bool oplus_ccdetect_check_is_gpio(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->chgic_mtk.oplus_info->ccdetect_gpio)){
		return true;
	}

	return false;
}

bool oplus_ccdetect_support_check(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;

	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return false;
	}

	if (oplus_ccdetect_check_is_gpio(chip) == true)
		return true;

	return false;
}

int oplus_ccdetect_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		chg_err("oplus_chip not ready!\n");
		return -EINVAL;
	}

	chip->chgic_mtk.oplus_info->pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chip->chgic_mtk.oplus_info->pinctrl)) {
		chg_err("get ccdetect_pinctrl fail\n");
		return -EINVAL;
	}

	chip->chgic_mtk.oplus_info->ccdetect_active = pinctrl_lookup_state(chip->chgic_mtk.oplus_info->pinctrl, "ccdetect_active");
	if (IS_ERR_OR_NULL(chip->chgic_mtk.oplus_info->ccdetect_active)) {
		chg_err("get ccdetect_active fail\n");
		return -EINVAL;
	}

	chip->chgic_mtk.oplus_info->ccdetect_sleep = pinctrl_lookup_state(chip->chgic_mtk.oplus_info->pinctrl, "ccdetect_sleep");
	if (IS_ERR_OR_NULL(chip->chgic_mtk.oplus_info->ccdetect_sleep)) {
		chg_err("get ccdetect_sleep fail\n");
		return -EINVAL;
	}
	if (chip->chgic_mtk.oplus_info->ccdetect_gpio > 0) {
		gpio_direction_input(chip->chgic_mtk.oplus_info->ccdetect_gpio);
	}

	pinctrl_select_state(chip->chgic_mtk.oplus_info->pinctrl,  chip->chgic_mtk.oplus_info->ccdetect_active);
	return 0;
}

void oplus_ccdetect_irq_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: oplus_chip not ready!\n", __func__);
		return;
	}
	chip->chgic_mtk.oplus_info->ccdetect_irq = gpio_to_irq(chip->chgic_mtk.oplus_info->ccdetect_gpio);
	printk(KERN_ERR "[OPLUS_CHG][%s]: chip->chgic_mtk.oplus_info->ccdetect_gpio[%d]!\n", __func__, chip->chgic_mtk.oplus_info->ccdetect_gpio);

}

int oplus_chg_ccdetect_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip)
		node = chip->dev->of_node;
	if (node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}
	chip->chgic_mtk.oplus_info->ccdetect_gpio = of_get_named_gpio(node, "qcom,ccdetect-gpio", 0);
	if (chip->chgic_mtk.oplus_info->ccdetect_gpio <= 0) {
		chg_err("Couldn't read qcom,ccdetect-gpio rc=%d, qcom,ccdetect-gpio:%d\n",
				rc, chip->chgic_mtk.oplus_info->ccdetect_gpio);
	} else {
		if (oplus_ccdetect_support_check() == true) {
			rc = gpio_request(chip->chgic_mtk.oplus_info->ccdetect_gpio, "ccdetect-gpio");
			if (rc) {
				chg_err("unable to request ccdetect_gpio:%d\n",
						chip->chgic_mtk.oplus_info->ccdetect_gpio);
			} else {
				rc = oplus_ccdetect_gpio_init(chip);
				if (rc){
					chg_err("unable to init ccdetect_gpio:%d\n",
							chip->chgic_mtk.oplus_info->ccdetect_gpio);
				}else{
					oplus_ccdetect_irq_init(chip);
					}
			}
		}
		chg_err("ccdetect-gpio:%d\n", chip->chgic_mtk.oplus_info->ccdetect_gpio);
	}

	return rc;
}

static enum alarmtimer_restart recover_charge_hrtimer_func(
	struct alarm *alarm, ktime_t now)
{
	if(g_oplus_chip == NULL) {
		chg_err("get chip status latter\n");
		return ALARMTIMER_NORESTART;
	}

	oplus_usbtemp_recover_func(g_oplus_chip);

	return ALARMTIMER_NORESTART;
}

static int oplus_dischg_gpio_init(struct oplus_chg_chip *chip)
{
	if (!chip) {
		chg_err("oplus_chip not ready!\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get dischg_pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_enable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_enable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)) {
		chg_err("get dischg_enable fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_disable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_disable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_disable)) {
		chg_err("get dischg_disable fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);

	return 0;
}

static void oplus_usbtemp_thread_init(void)
{
	alarm_init(&usbotp_recover_timer, ALARM_BOOTTIME, recover_charge_hrtimer_func);
	oplus_usbtemp_kthread =
			kthread_run(oplus_usbtemp_monitor_common, g_oplus_chip, "usbtemp_kthread");
	if (IS_ERR(oplus_usbtemp_kthread)) {
		chg_err("failed to cread oplus_usbtemp_kthread\n");
	}
}

void oplus_wake_up_usbtemp_thread(void)
{
	if (oplus_usbtemp_check_is_support() == true) {
		wake_up_interruptible(&oplus_usbtemp_wq);
		chg_debug("wake_up_usbtemp_thread, vbus:%d, otg:%d", oplus_chg_get_vbus_status(g_oplus_chip), oplus_chg_get_otg_online());
	}
}
EXPORT_SYMBOL(oplus_wake_up_usbtemp_thread);

static int oplus_chg_usbtemp_parse_dt(struct oplus_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = NULL;

	if (chip)
		node = chip->dev->of_node;
	if (node == NULL) {
		chg_err("oplus_chip or device tree info. missing\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_gpio = of_get_named_gpio(node, "qcom,dischg-gpio", 0);
	if (chip->normalchg_gpio.dischg_gpio <= 0) {
		chg_err("Couldn't read qcom,dischg-gpio rc=%d, qcom,dischg-gpio:%d\n",
				rc, chip->normalchg_gpio.dischg_gpio);
	} else {
		if (oplus_usbtemp_check_is_support() == true) {
			rc = gpio_request(chip->normalchg_gpio.dischg_gpio, "dischg-gpio");
			if (rc) {
				chg_err("unable to request dischg-gpio:%d\n",
						chip->normalchg_gpio.dischg_gpio);
			} else {
				rc = oplus_dischg_gpio_init(chip);
				if (rc)
					chg_err("unable to init dischg-gpio:%d\n",
						chip->normalchg_gpio.dischg_gpio);
				rc = usbtemp_channel_burn_init(chip->dev);
				if (rc)
					chg_err("unable to init usbtemp_channel\n");
			}
		}
		chg_err("dischg-gpio:%d\n", chip->normalchg_gpio.dischg_gpio);
	}

	return rc;
}
/* ==================================================================== */

/************************************************/
/* Power Supply Functions
*************************************************/
static int mt_ac_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	int rc = 0;
	rc = oplus_ac_get_property(psy, psp, val);

	return 0;
}

static int oplus_usb_set_property(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	int ret = 0;

	switch (psp) {
	default:
		pr_err("set prop %d is not supported in usb\n", psp);
		ret = -EINVAL;
		break;
	}
	return ret;
}

static int oplus_usb_property_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int ret = 0;
	switch (psp) {
	default:
		pr_err("writeable prop %d is not supported in usb\n", psp);
		ret = -EINVAL;
		break;
	}
	return 0;
}

static int mt_usb_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int rc = 0;
	rc = oplus_usb_property_is_writeable(psy, psp);
	return rc;
}

static int mt_usb_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val) {
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION:
		val->intval = oplus_get_typec_cc_orientation();
		break;
	default:
		rc = oplus_usb_get_property(psy, psp, val);
	}

	return rc;
}

static int mt_usb_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	oplus_usb_set_property(psy, psp, val);
	return 0;
}

static int battery_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	int rc = 0;
	rc = oplus_battery_property_is_writeable(psy, psp);
	return rc;
}

static int battery_set_property(struct power_supply *psy,
	enum power_supply_property psp, const union power_supply_propval *val)
{
	oplus_battery_set_property(psy, psp, val);
	return 0;
}

static int battery_get_property(struct power_supply *psy,
	enum power_supply_property psp, union power_supply_propval *val)
{
	int rc = 0;
	if (!g_oplus_chip) {
		pr_err("%s, oplus_chip null\n", __func__);
		return -1;
	}

	switch (psp) {
	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		if (g_oplus_chip && (g_oplus_chip->ui_soc == 0)) {
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
			pr_err("bat pro POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL, should shutdown!!!\n");
		}
		break;
	default:
		rc = oplus_battery_get_property(psy, psp, val);
		break;
	}
	return 0;
}


static enum power_supply_property mt_ac_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
#ifdef CONFIG_OPLUS_FAST2NORMAL_CHG
	POWER_SUPPLY_PROP_FAST2NORMAL_CHG,
#endif
};

static enum power_supply_property mt_usb_properties[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION,
};
static enum power_supply_property battery_properties[] = {
        POWER_SUPPLY_PROP_STATUS,
        POWER_SUPPLY_PROP_HEALTH,
        POWER_SUPPLY_PROP_PRESENT,
        POWER_SUPPLY_PROP_TECHNOLOGY,
        POWER_SUPPLY_PROP_CAPACITY,
        POWER_SUPPLY_PROP_TEMP,
        POWER_SUPPLY_PROP_VOLTAGE_NOW,
        POWER_SUPPLY_PROP_VOLTAGE_MIN,
        POWER_SUPPLY_PROP_CURRENT_NOW,
        POWER_SUPPLY_PROP_CHARGE_NOW,
#ifdef CONFIG_OPLUS_CHARGER_MTK
        POWER_SUPPLY_PROP_CHARGE_FULL,
        POWER_SUPPLY_PROP_CHARGE_COUNTER,
        POWER_SUPPLY_PROP_CURRENT_MAX,
#endif
#ifdef CONFIG_OPLUS_SHORT_C_BATT_CHECK
#ifdef CONFIG_OPLUS_SHORT_USERSPACE
        POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
        POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
#else
        POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE,
        POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE,
        POWER_SUPPLY_PROP_SHORT_C_BATT_CV_STATUS,
#endif /*CONFIG_OPLUS_SHORT_USERSPACE*/
#endif
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
};


static int oplus_power_supply_init(struct oplus_chg_chip *chip) {
	int ret = 0;
	struct oplus_chg_chip *mt_chg = NULL;
	mt_chg = chip;
	mt_chg->ac_psd.name = "ac";
	mt_chg->ac_psd.type = POWER_SUPPLY_TYPE_MAINS;
	mt_chg->ac_psd.properties = mt_ac_properties;
	mt_chg->ac_psd.num_properties = ARRAY_SIZE(mt_ac_properties);
	mt_chg->ac_psd.get_property = mt_ac_get_property;
	mt_chg->ac_cfg.drv_data = mt_chg;

	mt_chg->usb_psd.name = "usb";
	mt_chg->usb_psd.type = POWER_SUPPLY_TYPE_USB;
	mt_chg->usb_psd.properties = mt_usb_properties;
	mt_chg->usb_psd.num_properties = ARRAY_SIZE(mt_usb_properties);
	mt_chg->usb_psd.get_property = mt_usb_get_property;
	mt_chg->usb_psd.set_property = mt_usb_set_property;
	mt_chg->usb_psd.property_is_writeable = mt_usb_prop_is_writeable;
	mt_chg->usb_cfg.drv_data = mt_chg;

	mt_chg->battery_psd.name = "battery";
	mt_chg->battery_psd.type = POWER_SUPPLY_TYPE_BATTERY;
	mt_chg->battery_psd.properties = battery_properties;
	mt_chg->battery_psd.num_properties = ARRAY_SIZE(battery_properties);
	mt_chg->battery_psd.get_property = battery_get_property;
	mt_chg->battery_psd.set_property = battery_set_property;
	mt_chg->battery_psd.property_is_writeable = battery_prop_is_writeable,

	mt_chg->ac_psy = power_supply_register(mt_chg->dev, &mt_chg->ac_psd,
		&mt_chg->ac_cfg);
	if (IS_ERR(mt_chg->ac_psy)) {
		dev_err(mt_chg->dev, "Failed to register power supply: %ld\n",
			PTR_ERR(mt_chg->ac_psy));
		ret = PTR_ERR(mt_chg->ac_psy);
		goto err_ac_psy;
	}
	mt_chg->usb_psy = power_supply_register(mt_chg->dev, &mt_chg->usb_psd,
		&mt_chg->usb_cfg);
	if (IS_ERR(mt_chg->usb_psy)) {
		dev_err(mt_chg->dev, "Failed to register power supply: %ld\n",
		PTR_ERR(mt_chg->usb_psy));
		ret = PTR_ERR(mt_chg->usb_psy);
		goto err_usb_psy;
	}
	mt_chg->batt_psy = power_supply_register(mt_chg->dev, &mt_chg->battery_psd,
		NULL);
	if (IS_ERR(mt_chg->batt_psy)) {
	dev_err(mt_chg->dev, "Failed to register power supply: %ld\n",
		PTR_ERR(mt_chg->batt_psy));
		ret = PTR_ERR(mt_chg->batt_psy);
		goto err_battery_psy;
	}
	pr_info("%s\n", __func__);
	return 0;

err_usb_psy:
	power_supply_unregister(mt_chg->ac_psy);
err_ac_psy:
	power_supply_unregister(mt_chg->usb_psy);
err_battery_psy:
	power_supply_unregister(mt_chg->batt_psy);

	return ret;
}

#ifndef CONFIG_OPLUS_CHARGER_MTK6765S
static int oplus_chg_parse_charger_dt_2nd_override(struct oplus_chg_chip *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	if (!node) {
		dev_err(chip->dev, "device tree info. missing\n");
		return -EINVAL;
	}

	if (is_project(0x206AC)) {
		if (chip->fast_node && chip->is_double_charger_support) {
			node = chip->fast_node;
			pr_err("%s fastcharger changed node\n", __func__);
		}
	}

	rc = of_property_read_u32(node, "qcom,iterm_ma_2nd", &chip->limits.iterm_ma);
	if (rc < 0) {
		chip->limits.iterm_ma = 300;
	}

	rc = of_property_read_u32(node, "qcom,recharge-mv_2nd", &chip->limits.recharge_mv);
	if (rc < 0) {
		chip->limits.recharge_mv = 121;
	}

	rc = of_property_read_u32(node, "qcom,temp_little_cold_vfloat_mv_2nd",
			&chip->limits.temp_little_cold_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_little_cold_vfloat_mv = 4391;
	}

	rc = of_property_read_u32(node, "qcom,temp_cool_vfloat_mv_2nd",
			&chip->limits.temp_cool_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_cool_vfloat_mv = 4391;
	}

	rc = of_property_read_u32(node, "qcom,temp_little_cool_vfloat_mv_2nd",
			&chip->limits.temp_little_cool_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_little_cool_vfloat_mv = 4391;
	}

	rc = of_property_read_u32(node, "qcom,temp_normal_vfloat_mv_2nd",
			&chip->limits.temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.temp_normal_vfloat_mv = 4391;
	}

	rc = of_property_read_u32(node, "qcom,little_cold_vfloat_over_sw_limit_2nd",
			&chip->limits.little_cold_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.little_cold_vfloat_over_sw_limit = 4395;
	}

	rc = of_property_read_u32(node, "qcom,cool_vfloat_over_sw_limit_2nd",
			&chip->limits.cool_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.cool_vfloat_over_sw_limit = 4395;
	}

	rc = of_property_read_u32(node, "qcom,little_cool_vfloat_over_sw_limit_2nd",
			&chip->limits.little_cool_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.little_cool_vfloat_over_sw_limit = 4395;
	}

	rc = of_property_read_u32(node, "qcom,normal_vfloat_over_sw_limit_2nd",
			&chip->limits.normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.normal_vfloat_over_sw_limit = 4395;
	}

	rc = of_property_read_u32(node, "qcom,default_iterm_ma_2nd",
			&chip->limits.default_iterm_ma);
	if (rc < 0) {
		chip->limits.default_iterm_ma = 300;
	}

	rc = of_property_read_u32(node, "qcom,default_temp_normal_vfloat_mv_2nd",
			&chip->limits.default_temp_normal_vfloat_mv);
	if (rc < 0) {
		chip->limits.default_temp_normal_vfloat_mv = 4391;
	}

	rc = of_property_read_u32(node, "qcom,default_normal_vfloat_over_sw_limit_2nd",
			&chip->limits.default_normal_vfloat_over_sw_limit);
	if (rc < 0) {
		chip->limits.default_normal_vfloat_over_sw_limit = 4395;
	}

	chg_err("iterm_ma = %d, recharge_mv = %d, temp_little_cold_vfloat_mv = %d, \
		temp_cool_vfloat_mv = %d, temp_little_cool_vfloat_mv = %d, \
		temp_normal_vfloat_mv = %d, little_cold_vfloat_over_sw_limit = %d, \
		cool_vfloat_over_sw_limit = %d, little_cool_vfloat_over_sw_limit = %d, \
		normal_vfloat_over_sw_limit = %d, default_iterm_ma = %d, \
		default_temp_normal_vfloat_mv = %d, default_normal_vfloat_over_sw_limit = %d\n",
			chip->limits.iterm_ma, chip->limits.recharge_mv, chip->limits.temp_little_cold_vfloat_mv,
			chip->limits.temp_cool_vfloat_mv, chip->limits.temp_little_cool_vfloat_mv,
			chip->limits.temp_normal_vfloat_mv, chip->limits.little_cold_vfloat_over_sw_limit,
			chip->limits.cool_vfloat_over_sw_limit, chip->limits.little_cool_vfloat_over_sw_limit,
			chip->limits.normal_vfloat_over_sw_limit, chip->limits.default_iterm_ma,
			chip->limits.default_temp_normal_vfloat_mv, chip->limits.default_normal_vfloat_over_sw_limit);

	return rc;
}
#endif
/* ==================================================================== */
static ssize_t show_StopCharging_Test(struct device *dev, struct device_attribute *attr, char *buf)
{
	g_oplus_chip->stop_chg = false;
	oplus_chg_turn_off_charging(g_oplus_chip);
	printk("StopCharging_Test\n");
	return sprintf(buf, "chr=%d\n", g_oplus_chip->stop_chg);
}

static ssize_t store_StopCharging_Test(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	return -1;
}
static DEVICE_ATTR(StopCharging_Test, 0664, show_StopCharging_Test, store_StopCharging_Test);

static ssize_t show_StartCharging_Test(struct device *dev, struct device_attribute *attr, char *buf)
{
	g_oplus_chip->stop_chg = true;
	oplus_chg_turn_on_charging(g_oplus_chip);
	printk("StartCharging_Test\n");
	return sprintf(buf, "chr=%d\n", g_oplus_chip->stop_chg);
}
static ssize_t store_StartCharging_Test(struct device *dev, struct device_attribute *attr, const char *buf, size_t size)
{
	return -1;
}
static DEVICE_ATTR(StartCharging_Test, 0664, show_StartCharging_Test, store_StartCharging_Test);

void oplus_chg_default_method0(void)
{
	pr_err("charger ic default %d\n", charger_ic__det_flag);
}

int oplus_chg_default_method1(void)
{
	return 0;
}

int oplus_chg_default_method2(int n)
{
	return 0;
}

void oplus_chg_default_method3(int n)
{
	return;
}

bool oplus_chg_default_method4(void)
{
	return false;
}

int mtk_hvdcp_v20_init(struct mtk_charger *pinfo)
{
	int ret = 0;

	pinfo->hvdcp.is_enabled = 0;
	pinfo->hvdcp.is_connect = 0;

	return ret;
}

bool mtk_pdc_init(struct mtk_charger *info)
{
	info->pdc.pdc_max_watt_setting = -1;
	return true;
}

int mtk_pdc_get_max_watt(struct mtk_charger *info)
{
	int charging_current = info->data.pd_charger_current / 1000;
	int vbat = oplus_battery_meter_get_battery_voltage();

	if (info->pdc.pdc_max_watt_setting != -1)
		info->pdc.pdc_max_watt = info->pdc.pdc_max_watt_setting;
	else {
		if (info->chg_data[CHG1_SETTING].thermal_charging_current_limit != -1)
			charging_current =
			info->chg_data[CHG1_SETTING].thermal_charging_current_limit / 1000;

		info->pdc.pdc_max_watt = vbat * charging_current;
	}
	chg_err("[%s]watt:%d:%d vbat:%d c:%d=>\n", __func__,
		info->pdc.pdc_max_watt_setting,
		info->pdc.pdc_max_watt, vbat, charging_current);

	return info->pdc.pdc_max_watt;
}

void mtk_pdc_set_max_watt(struct mtk_charger *info, int watt)
{
	info->pdc.vbus_h = 10000;
	info->pdc.pdc_max_watt_setting = watt;
}

void oplus_mt_usb_connect(void)
{
	return;
}

void oplus_mt_usb_disconnect(void)
{
	return;
}

void oplus_set_typec_sinkonly(void)
{
	if (pinfo != NULL && pinfo->tcpc != NULL) {
		printk(KERN_ERR "[OPLUS_CHG][%s]: usbtemp occur otg switch[0]\n", __func__);
		pinfo->tcpc->typec_role_new = TYPEC_ROLE_SRC;
		tcpm_typec_change_role_postpone(pinfo->tcpc, TYPEC_ROLE_SNK, true);
	}
}
EXPORT_SYMBOL(oplus_set_typec_sinkonly);

#ifdef OPLUS_FEATURE_CHG_BASIC
void oplus_set_typec_cc_open(void)
{
	if (pinfo == NULL || pinfo->tcpc == NULL)
		return;
#ifdef CONFIG_TCPC_CLASS
	tcpm_typec_disable_function(pinfo->tcpc, true);
#endif /* CONFIG_TCPC_CLASS */
	chg_err(" !\n");
}
EXPORT_SYMBOL(oplus_set_typec_cc_open);

void oplus_usbtemp_recover_cc_open(void)
{
	if (pinfo == NULL || pinfo->tcpc == NULL)
		return;
#ifdef CONFIG_TCPC_CLASS
	tcpm_typec_disable_function(pinfo->tcpc, false);
#endif /* CONFIG_TCPC_CLASS */
	chg_err(" !\n");
}
#endif /* OPLUS_FEATURE_CHG_BASIC */

bool oplus_usbtemp_condition(void)
{
	struct oplus_chg_chip *chip = g_oplus_chip;
	if(!chip) {
		return false;
	}
	return oplus_chg_get_vbus_status(chip);
}
EXPORT_SYMBOL(oplus_usbtemp_condition);

struct oplus_chg_operations  oplus_chg_default_ops = {
	.dump_registers = oplus_chg_default_method0,
	.kick_wdt = oplus_chg_default_method1,
	.hardware_init = oplus_chg_default_method1,
	.charging_current_write_fast = oplus_chg_default_method2,
	.set_aicl_point = oplus_chg_default_method3,
	.input_current_write = oplus_chg_default_method2,
	.float_voltage_write = oplus_chg_default_method2,
	.term_current_set = oplus_chg_default_method2,
	.charging_enable = oplus_chg_default_method1,
	.charging_disable = oplus_chg_default_method1,
	.get_charging_enable = oplus_chg_default_method1,
	.charger_suspend = oplus_chg_default_method1,
	.charger_unsuspend = oplus_chg_default_method1,
	.set_rechg_vol = oplus_chg_default_method2,
	.reset_charger = oplus_chg_default_method1,
	.read_full = oplus_chg_default_method1,
	.otg_enable = oplus_chg_default_method1,
	.otg_disable = oplus_chg_default_method1,
	.set_charging_term_disable = oplus_chg_default_method1,
	.check_charger_resume = oplus_chg_default_method4,

	.get_charger_type = oplus_chg_default_method1,
	.get_charger_volt = oplus_chg_default_method1,
	.get_chargerid_volt = NULL,
	.set_chargerid_switch_val = oplus_chg_default_method3,
	.get_chargerid_switch_val = oplus_chg_default_method1,
	.check_chrdet_status = NULL,

	.get_boot_mode = (int (*)(void))get_boot_mode,
	.get_boot_reason = oplus_chg_default_method1,
	.get_instant_vbatt = oplus_battery_meter_get_battery_voltage,
	.get_rtc_soc = NULL,
	.set_rtc_soc = NULL,
	.set_power_off = NULL,
	.usb_connect = oplus_mt_usb_connect,
	.usb_disconnect = oplus_mt_usb_disconnect,
	.get_chg_current_step = oplus_chg_default_method1,
	.need_to_check_ibatt = oplus_chg_default_method4,
	.get_dyna_aicl_result = oplus_chg_default_method1,
	.get_shortc_hw_gpio_status = oplus_chg_default_method4,
	.oplus_chg_get_pd_type = NULL,
	.oplus_chg_pd_setup = NULL,
	.get_charger_subtype = oplus_chg_default_method1,
	.set_qc_config = NULL,
	.enable_qc_detect = NULL,
	.oplus_chg_set_high_vbus = NULL,
	.get_usbtemp_volt = oplus_get_usbtemp_burn_volt,
	.set_typec_sinkonly = oplus_set_typec_sinkonly,
	.oplus_usbtemp_monitor_condition = oplus_usbtemp_condition,
	.set_typec_cc_open = oplus_set_typec_cc_open,
};

#ifdef OPLUS_FEATURE_CHG_BASIC
#ifndef CONFIG_TCPC_CLASS
struct mt6397_chip *pmic_chip;

void pmic_set_register_value(struct regmap *map,
	unsigned int addr,
	unsigned int mask,
	unsigned int shift,
	unsigned int val)
{
	regmap_update_bits(map,
		addr,
		mask << shift,
		val << shift);
}

static ssize_t gsm_pmic_pwm_node_write(struct file *file, const char __user *buf, size_t count, loff_t *pos)
{
	char upmu_vs1_mode[4];
	ssize_t ret = 1;

	if (NULL == buf) {
		chg_err("gsm_pmic_pwm_node_write parameter error.\n");
		return -EFAULT;
	}

	if (copy_from_user(&upmu_vs1_mode, buf, count)) {
		chg_err("gsm_pmic_pwm_node_write copy_from_user error.\n");
		return -EFAULT;
	}

	if (strncmp(upmu_vs1_mode, "1", 1) == 0) {
		pmic_set_register_value(pmic_chip->regmap,
			MT6357_VS1_ANA_CON2,
			PMIC_RG_VS1_MODESET_MASK,
			PMIC_RG_VS1_MODESET_SHIFT,
			FORCE_PWM);
	} else if (strncmp(upmu_vs1_mode, "0", 1) == 0) {
		pmic_set_register_value(pmic_chip->regmap,
			MT6357_VS1_ANA_CON2,
			PMIC_RG_VS1_MODESET_MASK,
			PMIC_RG_VS1_MODESET_SHIFT,
			AUTO_MODE);
	} else
		chg_err("gsm_pmic_pwm_node_write parameter error.\n");

	return ret;
}

static ssize_t gsm_pmic_pwm_node_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
	return count;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations gsm_pmic_pwm_ctrl =
{
	.write = gsm_pmic_pwm_node_write,
	.read = gsm_pmic_pwm_node_read,
};
#else
static const struct proc_ops gsm_pmic_pwm_ctrl =
{
	.proc_write = gsm_pmic_pwm_node_write,
	.proc_read = gsm_pmic_pwm_node_read,
	.proc_lseek = seq_lseek,
};
#endif

static void init_proc_upmu_vs1_config(void)
{
	if (!proc_create("gsm_pmic_pwm", S_IWUSR | S_IWGRP | S_IWOTH,
		NULL, &gsm_pmic_pwm_ctrl)) {
		chg_err("proc_create gsm_pmic_pwm fail!\n");
	}
}

static int oplus_pmic_chip_init()
{
	static struct mtk_charger *pinfo;
	int ret = 0;
	struct device_node *pmic_node;
	struct platform_device *pmic_pdev;
	struct power_supply *psy;

	if (pinfo == NULL) {
		psy = power_supply_get_by_name("mtk-master-charger");
		if (psy == NULL) {
			chr_err("[%s]psy is not rdy\n", __func__);
			return -1;
		}

		pinfo = (struct mtk_charger *)power_supply_get_drvdata(psy);
		if (pinfo == NULL) {
			chr_err("[%s]mtk_gauge is not rdy\n", __func__);
			return -1;
		}
	}

	pmic_node = of_parse_phandle(pinfo->pdev->dev.of_node, "pmic", 0);
	if (!pmic_node) {
		chr_err("get pmic_node fail\n");
		return -1;
	}

	pmic_pdev = of_find_device_by_node(pmic_node);
	if (!pmic_pdev) {
		chr_err("get pmic_pdev fail\n");
		return -1;
	}

	pmic_chip = dev_get_drvdata(&(pmic_pdev->dev));
	if (!pmic_chip) {
		chr_err("get chip fail\n");
		return -1;
	}

	init_proc_upmu_vs1_config();

	return ret;
}
#endif
#endif

static int oplus_charger_probe(struct platform_device *pdev)
{
	struct mtk_charger *info = NULL;
	struct list_head *pos;
	struct list_head *phead = &consumer_head;
	struct charger_consumer *ptr;
	int ret = 0;
	struct oplus_chg_chip *oplus_chip = NULL;
	/* struct mt_charger *mt_chg = NULL; */
	chg_err("%s: starts\n", __func__);
	oplus_chip = devm_kzalloc(&pdev->dev, sizeof(struct oplus_chg_chip), GFP_KERNEL);
	if (!oplus_chip) {
		chg_err(" kzalloc() failed\n");
			return -ENOMEM;
	}
	oplus_chip->dev = &pdev->dev;
	ret = oplus_chg_parse_svooc_dt(oplus_chip);
	if (oplus_chip->vbatt_num == 1) {
		if (oplus_gauge_check_chip_is_null()) {
			chg_err("gauge chip null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		charger_ic__det_flag = get_charger_ic_det(oplus_chip);
		if (charger_ic__det_flag == 0) {
			chg_err("charger IC is null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}

		if (oplus_chip->is_double_charger_support) {
			switch(charger_ic__det_flag) {
			case (1 << BQ2591X|1 << BQ2589X):
#ifdef CONFIG_CHARGER_BQ2589X
					oplus_chip->chg_ops = &oplus_chg_bq2589x_ops;
#endif
					break;
			case (1 << BQ2589X | 1 << BQ2560X):
#ifdef CONFIG_CHARGER_BQ2589X
					oplus_chip->chg_ops = &oplus_chg_bq2589x_ops;
#endif
					oplus_chip->sub_chg_ops = &oplus_chg_bq2560x_ops;
					break;
			default:
					oplus_chip->chg_ops = &oplus_chg_default_ops;
					oplus_chip->sub_chg_ops = &oplus_chg_default_ops;
			}
		} else {
			switch(charger_ic__det_flag) {
			case (1 << BQ2560X):
					oplus_chip->chg_ops = &oplus_chg_bq2560x_ops;
					break;
			default:
					oplus_chip->chg_ops = &oplus_chg_default_ops;
			}
		}
	} else {
		if (oplus_gauge_ic_chip_is_null() || oplus_vooc_check_chip_is_null()
				|| oplus_adapter_check_chip_is_null()) {
			chg_err("[oplus_chg_init] vooc || gauge || chg not ready, will do after bettery init.\n");
		}
		is_mtksvooc_project = true;
	}

	if (is_vooc_support_single_batt_svooc() == true) {
		is_mtksvooc_project = true;
		chg_err("%s is_vooc_support_single_batt_svooc\n", __func__);
	}

	if (oplus_chip->vooc_project == 1 && is_mtksvooc_project == false) {
		if (oplus_gauge_check_chip_is_null()) {
			chg_err("[oplus_chg_init] gauge null, will do after bettery init.\n");
			return -EPROBE_DEFER;
		}
		is_vooc30_cfg = true;
		chg_err("%s is_vooc30_cfg = %d\n", __func__, is_vooc30_cfg);
	}

	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	pinfo = info;

	platform_set_drvdata(pdev, info);
	info->pdev = pdev;

	mtk_charger_parse_dt(info, &pdev->dev);

	oplus_chip->chgic_mtk.oplus_info = info;

	if (oplus_chg_get_voocphy_support()) {
		is_mtksvooc_project = true;
		chg_err("%s oplus_chg_get_voocphy_support\n", __func__);
	}

	info->chg1_dev = get_charger_by_name("primary_chg");
	if (info->chg1_dev) {
		chg_err("found primary charger [%s]\n",
			info->chg1_dev->props.alias_name);
	} else {
		chg_err("can't find primary charger!\n");
	}

	mutex_init(&info->charger_lock);
	mutex_init(&info->cable_out_lock);

	spin_lock_init(&info->slock);

	if (info->chg1_dev != NULL && info->algo.do_event != NULL) {
		info->chg1_nb.notifier_call = info->algo.do_event;
		register_charger_device_notifier(info->chg1_dev, &info->chg1_nb);
		charger_dev_set_drvdata(info->chg1_dev, info);
	}
	/* init thread */
	g_oplus_chip = oplus_chip;
	oplus_power_supply_init(oplus_chip);
	oplus_chg_parse_custom_dt(oplus_chip);
	oplus_chg_parse_charger_dt(oplus_chip);
	oplus_chip->chg_ops->hardware_init();
	oplus_chip->authenticate = oplus_gauge_get_batt_authenticate();
	oplus_chg_init(oplus_chip);
	if(oplus_chip->is_double_charger_support) {
		pr_err("%s: dual charger ic ,set charger_hv_thr to 10v!\n", __func__);
		oplus_chip->limits.charger_hv_thr = 10000;
		oplus_chip->limits.charger_recv_thr = 9700;
	}
	oplus_chg_wake_update_work();
	if (get_boot_mode() != KERNEL_POWER_OFF_CHARGING_BOOT) {
		oplus_tbatt_power_off_task_init(oplus_chip);
	}

#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chg_configfs_init(oplus_chip);
#endif

	srcu_init_notifier_head(&info->evt_nh);
	mtk_charger_setup_files(pdev);

	info->enable_hv_charging = true;

	info->psy_desc1.name = "mtk-master-charger";
	info->psy_desc1.type = POWER_SUPPLY_TYPE_UNKNOWN;
	info->psy_desc1.properties = charger_psy_properties;
	info->psy_desc1.num_properties = ARRAY_SIZE(charger_psy_properties);
	info->psy_desc1.get_property = psy_charger_get_property;
	info->psy_desc1.set_property = psy_charger_set_property;
	info->psy_desc1.property_is_writeable =
			psy_charger_property_is_writeable;
	info->psy_desc1.external_power_changed =
		mtk_charger_external_power_changed;
	info->psy_cfg1.drv_data = info;
	info->psy1 = power_supply_register(&pdev->dev, &info->psy_desc1,
			&info->psy_cfg1);
	if (IS_ERR(info->psy1))
		chg_err("register psy1 fail:%d\n", PTR_ERR(info->psy1));

	info->log_level = CHRLOG_DEBUG_LEVEL;

	oplus_chg_usbtemp_parse_dt(oplus_chip);
	if (oplus_usbtemp_check_is_support() == true) {
		oplus_chip->con_volt = con_volt_20131;
		oplus_chip->con_temp = con_temp_20131;
		oplus_chip->len_array = ARRAY_SIZE(con_temp_20131);
		if (alarmtimer_get_rtcdev()) {
			alarm_init(&oplus_chip->usbtemp_alarm_timer, ALARM_BOOTTIME, usbtemp_alarm_timer_func);
		} else {
			chg_err("Failed to get soc alarm-timer");
		}

		oplus_usbtemp_thread_init();
		is_usbtemp_thread_init_done = true;
		oplus_wake_up_usbtemp_thread();
	}

	mtk_charger_init_timer(info);

	info->pd_adapter = get_adapter_by_name("pd_adapter");
	if (!info->pd_adapter)
		chg_err("%s: No pd adapter found\n");
	else {
		info->pd_nb.notifier_call = notify_adapter_event;
		register_adapter_device_notifier(info->pd_adapter,
						 &info->pd_nb);
	}

	info->tcpc = tcpc_dev_get_by_name("type_c_port0");
	if (!info->tcpc) {
		chg_err("%s get tcpc device type_c_port0 fail\n", __func__);
	}

	chg_err("%s get tcpc device type_c_port0 successful\n", __func__);

	mutex_lock(&consumer_mutex);
	list_for_each(pos, phead) {
		ptr = container_of(pos, struct charger_consumer, list);
		ptr->cm = info;
		if (ptr->pnb != NULL) {
			srcu_notifier_chain_register(&info->evt_nh, ptr->pnb);
			ptr->pnb = NULL;
		}
	}
	mutex_unlock(&consumer_mutex);
	info->chg1_consumer = charger_manager_get_by_name(&pdev->dev, "charger_port1");

	if (IS_ERR(oplus_chip->batt_psy) == 0) {
		ret = device_create_file(&oplus_chip->batt_psy->dev, &dev_attr_StopCharging_Test);/* stop charging */
		ret = device_create_file(&oplus_chip->batt_psy->dev, &dev_attr_StartCharging_Test);
	}

#ifdef OPLUS_FEATURE_CHG_BASIC
#ifndef CONFIG_TCPC_CLASS
	oplus_pmic_chip_init();
#endif
#endif

	return 0;
}

static int oplus_charger_remove(struct platform_device *dev)
{
#ifdef OPLUS_FEATURE_CHG_BASIC
	oplus_chg_configfs_exit();
#endif
	return 0;
}

static void oplus_charger_shutdown(struct platform_device *dev)
{
	if (g_oplus_chip != NULL)
		enter_ship_mode_function(g_oplus_chip);
}

static const struct of_device_id oplus_charger_of_match[] = {
	{.compatible = "mediatek,charger", },
	{},
};

MODULE_DEVICE_TABLE(of, oplus_charger_of_match);

struct platform_device oplus_charger_device = {
	.name = "charger",
	.id = -1,
};

static struct platform_driver oplus_charger_driver = {
	.probe = oplus_charger_probe,
	.remove = oplus_charger_remove,
	.shutdown = oplus_charger_shutdown,
	.driver = {
		   .name = "charger",
		   .of_match_table = oplus_charger_of_match,
		   },
};


static int __init oplus_charger_init(void)
{
	int ret;
	printk("oplus_charger_init\n");
	ret = platform_driver_register(&oplus_charger_driver);
	printk("oplus_charger_init: ret = %d\n", ret);
	return ret;
}
module_init(oplus_charger_init);


static void __exit mtk_charger_exit(void)
{
	platform_driver_unregister(&oplus_charger_driver);
}
module_exit(mtk_charger_exit);


MODULE_AUTHOR("wy.chuang <wy.chuang@mediatek.com>");
MODULE_DESCRIPTION("MTK Charger Driver");
MODULE_LICENSE("GPL");
