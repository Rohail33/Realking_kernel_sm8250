/**
 * User space driver API for JIIOV's fingerprint sensor.
 *
 * Copyright (C) 2020 JIIOV Corporation. <http://www.jiiov.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General
 * Public License for more details.
**/

#ifndef __JIIOV_PLATFORM_H__
#define __JIIOV_PLATFORM_H__


#define ANC_USE_NETLINK
//#define ANC_USE_SPI
//#define ANC_USE_IRQ
//#define ANC_USE_POWER_GPIO


/* Magic code for IOCTL-subsystem */
#define ANC_IOC_MAGIC            'a'

/* Allocate/Release driver resource (GPIO/SPI etc.) */
#define ANC_IOC_INIT             _IO(ANC_IOC_MAGIC, 0)
#define ANC_IOC_DEINIT           _IO(ANC_IOC_MAGIC, 1)

/* HW reset the fingerprint module */
#define ANC_IOC_RESET            _IO(ANC_IOC_MAGIC, 2)

/* Low-level IRQ control */
#define ANC_IOC_ENABLE_IRQ       _IO(ANC_IOC_MAGIC, 3)
#define ANC_IOC_DISABLE_IRQ      _IO(ANC_IOC_MAGIC, 4)

/* SPI bus clock control, for power-saving purpose */
#define ANC_IOC_ENABLE_SPI_CLK   _IO(ANC_IOC_MAGIC, 5)
#define ANC_IOC_DISABLE_SPI_CLK  _IO(ANC_IOC_MAGIC, 6)

/* Fingerprint module power control */
#define ANC_IOC_ENABLE_POWER     _IO(ANC_IOC_MAGIC, 7)
#define ANC_IOC_DISABLE_POWER    _IO(ANC_IOC_MAGIC, 8)
#define ANC_IOC_CLEAR_FLAG       _IO(ANC_IOC_MAGIC, 20)

/* SPI speed related */
#define ANC_IOC_SPI_SPEED        _IOW(ANC_IOC_MAGIC, 9, uint32_t)


#define ANC_UI_DISAPPREAR        0
#define ANC_UI_READY             1

typedef enum {
    ANC_NETLINK_EVENT_TEST = 0,
    ANC_NETLINK_EVENT_IRQ,
    ANC_NETLINK_EVENT_SCR_OFF,
    ANC_NETLINK_EVENT_SCR_ON,
    ANC_NETLINK_EVENT_TOUCH_DOWN,
    ANC_NETLINK_EVENT_TOUCH_UP,
    ANC_NETLINK_EVENT_UI_READY,
    ANC_NETLINK_EVENT_EXIT,
    ANC_NETLINK_EVENT_INVALID,
    ANC_NETLINK_EVENT_MAX
}ANC_NETLINK_EVENT_TYPE;

int netlink_send_message_to_user(const char *p_buffer, size_t length);
int anc_netlink_init(void);
void anc_netlink_exit(void);


#endif /* __JIIOV_PLATFORM_H__ */