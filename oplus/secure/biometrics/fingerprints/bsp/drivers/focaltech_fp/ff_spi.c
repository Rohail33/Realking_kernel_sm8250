// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/module.h>
/*#include <linux/init.h>*/
#include <linux/errno.h>
#include <linux/spi/spi.h>
#include <linux/miscdevice.h>
#include <linux/ioctl.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include "ff_log.h"
#include "ff_core.h"

/*****************************************************************************
* Private constant and macro definitions using #define
*****************************************************************************/
#undef LOG_TAG
#define LOG_TAG "focaltech_spi"

#define FF_SPI_DRIVER_NAME            "focaltech_fpspi"
