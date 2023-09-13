/* SPDX-License-Identifier: GPL-2.0-only  */
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
#ifndef __OPLUS_SP6001_H__
#define __OPLUS_SP6001_H__

#define WRITE_REG_00               0x0000
#define WRITE_REG_01               0x0001
#define WRITE_REG_02               0x0002
#define WRITE_REG_03               0x0003
#define WRITE_REG_04               0x0004
#define WRITE_REG_05               0x0005

#define READ_REG_00                0x0020
#define READ_REG_01                0x0021
#define READ_REG_02                0x0022
#define READ_REG_03                0x0023
#define READ_REG_04                0x0024
#define READ_REG_05                0x0025

#define WRITE_REG_TX               0x000C

#define READ_REG_CEP               0x0008
#define READ_REG_VOUT              0x0009
#define READ_REG_VRECT             0x000A
#define READ_REG_IOUT              0x000B

struct oplus_sp6001_ic{
	struct i2c_client				 *client;
	struct device					 *dev;
    atomic_t                         suspended;
};

int sp6001_init_registers(void);
bool sp6001_device_reconize(void);
void sp6001_commu_data_process(struct oplus_sp6001_ic *chip, char *tx_command, char *tx_data);

#endif

