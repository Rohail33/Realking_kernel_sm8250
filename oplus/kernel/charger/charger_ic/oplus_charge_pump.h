/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#ifndef __OPLUS_CHARGE_PUMP_H__
#define __OPLUS_CHARGE_PUMP_H__

#include <linux/power_supply.h>
#include "../oplus_charger.h"

#define CHARGE_PUMP_FIRST_REG                                    0x00
#define CHARGE_PUMP_ADDRESS_REG00                                0x00
#define CHARGE_PUMP_ADDRESS_REG01                                0x01
#define CHARGE_PUMP_ADDRESS_REG02                                0x02
#define CHARGE_PUMP_ADDRESS_REG03                                0x03
#define CHARGE_PUMP_ADDRESS_REG04                                0x04
#define CHARGE_PUMP_ADDRESS_REG05                                0x05
#define CHARGE_PUMP_ADDRESS_REG06                                0x06
#define CHARGE_PUMP_ADDRESS_REG07                                0x07
#define CHARGE_PUMP_ADDRESS_REG08                                0x08
#define CHARGE_PUMP_ADDRESS_REG09                                0x09
#define CHARGE_PUMP_ADDRESS_REG0A                                0x0A
#define CHARGE_PUMP_ADDRESS_REG0B                                0x0B
#define CHARGE_PUMP_ADDRESS_REG0C                                0x0C
#define CHARGE_PUMP_ADDRESS_REG0D                                0x0D
#define CHARGE_PUMP_ADDRESS_REG0E                                0x0E
#define CHARGE_PUMP_ADDRESS_REG0F                                0x0F
#define CHARGE_PUMP_ADDRESS_REG10                                0x10
#define CHARGE_PUMP_ADDRESS_REG11                                0x11
#define CHARGE_PUMP_ADDRESS_REG12                                0x12
#define CHARGE_PUMP_ADDRESS_REG13                                0x13
#define CHARGE_PUMP_ADDRESS_REG14                                0x14
#define CHARGE_PUMP_ADDRESS_REG15                                0x15
#define CHARGE_PUMP_ADDRESS_REG16                                0x16
#define CHARGE_PUMP_LAST_REG                                     0x16
#define CHARGE_PUMP_REG_NUMBER                                   CHARGE_PUMP_LAST_REG + 1

#define CHARGE_PUMP_FIRST2_REG                                   0x30
#define CHARGE_PUMP_ADDRESS_REG30                                0x30
#define CHARGE_PUMP_ADDRESS_REG31                                0x31
#define CHARGE_PUMP_ADDRESS_REG32                                0x32
#define CHARGE_PUMP_ADDRESS_REG33                                0x33
#define CHARGE_PUMP_LAST2_REG                                    0x33
#define CHARGE_PUMP_REG2_NUMBER                                  CHARGE_PUMP_LAST2_REG + 1

#define DA9313_WORK_MODE_REG                           CHARGE_PUMP_ADDRESS_REG04
#define DA9313_WORK_MODE_MASK                          BIT(1)
#define DA9313_WORK_MODE_FIXED                         0
#define DA9313_WORK_MODE_AUTO                          BIT(1)

#define SD77313_WORK_MODE_REG                          CHARGE_PUMP_ADDRESS_REG04
#define SD77313_WORK_MODE_MASK                         BIT(1)
#define SD77313_WORK_MODE_FIXED                        0
#define SD77313_WORK_MODE_AUTO                         BIT(1)

#define MAX77932_WORK_MODE_REG                         CHARGE_PUMP_ADDRESS_REG04
#define MAX77932_WORK_MODE_MASK                        BIT(0)
#define MAX77932_WORK_MODE_FIXED                       BIT(0)
#define MAX77932_WORK_MODE_AUTO                        0

#define MAX77938_WORK_MODE_REG                         CHARGE_PUMP_ADDRESS_REG06
#define MAX77938_WORK_MODE_MASK                        BIT(0)
#define MAX77938_WORK_MODE_FIXED                       BIT(0)
#define MAX77938_WORK_MODE_AUTO                        0

#define CHARGE_PUMP_WORK_MODE_AUTO          1
#define CHARGE_PUMP_WORK_MODE_FIXED         0

#define HWID_DA9313     0x81
#define HWID_SD77313    0xF2
#define HWID_MAX77932   0x60
#define HWID_MAX77938   0x63
#define HWID_UNKNOW     0x00

struct chip_charge_pump {
        struct i2c_client           *client;
        struct device               *dev;
        bool                        fixed_mode_set_by_dev_file;
        atomic_t                    suspended;
        struct pinctrl              *pinctrl;
        int                         charge_pump_hwid_gpio;
        struct pinctrl_state        *charge_pump_hwid_active;
        struct pinctrl_state        *charge_pump_hwid_sleep;
        struct pinctrl_state        *charge_pump_hwid_default;
        int                         hwid;
};
#endif
