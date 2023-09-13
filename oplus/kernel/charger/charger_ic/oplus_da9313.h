/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */


#ifndef __OPLUS_DA9313_H__

#define __OPLUS_DA9313_H__

#include <linux/power_supply.h>
#include "../oplus_charger.h"


#define DA9313_FIRST_REG                                    0x00
#define DA9313_LAST_REG                                     0x16
#define DA9313_REG_NUMBER                                   DA9313_LAST_REG + 1

#define DA9313_FIRST2_REG                                   0x30
#define DA9313_LAST2_REG                                    0x33
#define DA9313_REG2_NUMBER                                  0x34

//0x04
#define REG04_DA9313_ADDRESS                                0x04
#define REG01_DA9313_ADDRESS                                0x01
#define REG16_DA9313_ADDRESS                                0x16

#define REG04_DA9313_PVC_MODE_MASK                          BIT(1)
#define REG04_DA9313_PVC_MODE_FIXED                         0
#define REG04_DA9313_PVC_MODE_AUTO                          BIT(1)

#define REG04_SD77313_PVC_MODE_MASK                         BIT(1)
#define REG04_SD77313_PVC_MODE_FIXED                        0
#define REG04_SD77313_PVC_MODE_AUTO                         BIT(1)

#define REG04_MAX77932_PVC_MODE_MASK                        BIT(0)
#define REG04_MAX77932_PVC_MODE_FIXED                       BIT(0)
#define REG04_MAX77932_PVC_MODE_AUTO                        0

#define REG06_MAX77938_ADDRESS                              0x06
#define REG04_MAX77938_PVC_MODE_MASK                        BIT(0)
#define REG04_MAX77938_PVC_MODE_FIXED                       BIT(0)
#define REG04_MAX77938_PVC_MODE_AUTO                        0

//0x0E
#define REG0E_DA9313_ADDRESS                                0x0E

#define REG0E_DA9313_PVC_DROP_MASK                          (BIT(7) | BIT(6))
#define REG0E_DA9313_PVC_DROP_20MV                          0
#define REG0E_DA9313_PVC_DROP_30MV                          BIT(6)
#define REG0E_DA9313_PVC_DROP_40MV                          BIT(7)//default
#define REG0E_DA9313_PVC_DROP_50MV                          (BIT(7) | BIT(6))

#define REG0E_DA9313_PVC_HYST_MASK                          (BIT(5) | BIT(4))
#define REG0E_DA9313_PVC_HYST_00MV                          0
#define REG0E_DA9313_PVC_HYST_10MV                          BIT(4)
#define REG0E_DA9313_PVC_HYST_20MV                          BIT(5)//default
#define REG0E_DA9313_PVC_HYST_30MV                          (BIT(5) | BIT(4))

#define REG0E_DA9313_PVC_MS_DROP_MASK                       (BIT(3) | BIT(2))
#define REG0E_DA9313_PVC_MS_DROP_15MV                       0
#define REG0E_DA9313_PVC_MS_DROP_30MV                       BIT(2)//default
#define REG0E_DA9313_PVC_MS_DROP_45MV                       BIT(3)
#define REG0E_DA9313_PVC_MS_DROP_60MV                       (BIT(3) | BIT(2))

#define REG0E_DA9313_PVC_MS_HYST_MASK                       (BIT(1) | BIT(0))
#define REG0E_DA9313_PVC_MS_HYST_00MV                       0
#define REG0E_DA9313_PVC_MS_HYST_15MV                       BIT(0)
#define REG0E_DA9313_PVC_MS_HYST_30MV                       BIT(1)//default
#define REG0E_DA9313_PVC_MS_HYST_45MV                       (BIT(1) | BIT(0))

#define DA9313_WORK_MODE_AUTO			1
#define DA9313_WORK_MODE_FIXED			0

#define HWID_DA9313   1
#define HWID_SD77313   2
#define HWID_MAX77932   0x60
#define HWID_MAX77938   0x63
#define HWID_UNKNOW   -1

struct chip_da9313 {
        struct i2c_client           *client;
        struct device               *dev;
        bool                        fixed_mode_set_by_dev_file;
        atomic_t                    suspended;
        struct pinctrl              *pinctrl;
        int da9313_hwid_gpio;
        struct pinctrl_state        *da9313_hwid_active;
        struct pinctrl_state        *da9313_hwid_sleep;
        struct pinctrl_state        *da9313_hwid_default;
        int                         hwid;
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
int da9313_driver_init(void);
void da9313_driver_exit(void);
#endif

#endif /*__OPLUS_DA9313_H__*/
