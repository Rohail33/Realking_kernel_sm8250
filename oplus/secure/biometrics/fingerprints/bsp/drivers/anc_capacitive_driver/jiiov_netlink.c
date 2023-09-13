/**
 * Netlink interface for JIIOV's fingerprint sensor.
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

#include <linux/init.h>
#include <linux/types.h>
#include <net/sock.h>
#include <linux/netlink.h>
#include "jiiov_platform.h"


#define NETLINK_ANC     30
#define USER_PORT       100


static struct sock *gp_netlink_sock = NULL;
extern struct net init_net; // by default

static int netlink_send_message(const char *p_buffer, uint16_t length)
{
    int ret = 0;
    struct sk_buff *p_sk_buff = NULL;
    struct nlmsghdr *p_nlmsghdr = NULL;

    /* 创建sk_buff 空间 */
    p_sk_buff = nlmsg_new(length, GFP_ATOMIC);
    if(NULL == p_sk_buff)
    {
        printk("netlink alloc failure\n");
        return -1;
    }

    /* 设置netlink消息头部 */
    p_nlmsghdr = nlmsg_put(p_sk_buff, 0, 0, NETLINK_ANC, length, 0);
    if(NULL == p_nlmsghdr)
    {
        printk("nlmsg_put failaure \n");
        nlmsg_free(p_sk_buff);
        return -1;
    }

    /* 拷贝数据发送 */
    memcpy(nlmsg_data(p_nlmsghdr), p_buffer, length);
    ret = netlink_unicast(gp_netlink_sock, p_sk_buff, USER_PORT, MSG_DONTWAIT);

    return ret;
}

int anc_cap_netlink_send_message_to_user(const char *p_buffer, size_t length)
{
    int ret = -1;

    if ((NULL != p_buffer) && (length > 0))
    {
        printk("send message to user: %d\n", *p_buffer);
        ret = netlink_send_message(p_buffer, length);
    }

    return ret;
}

static void netlink_receive_message(struct sk_buff *p_sk_buffer)
{
    struct nlmsghdr *nlh = NULL;
    char *p_user_message = NULL;


    if (p_sk_buffer->len >= nlmsg_total_size(0))
    {
        nlh = nlmsg_hdr(p_sk_buffer);
        p_user_message = NLMSG_DATA(nlh);
        if(p_user_message)
        {
            printk("received message:%s, length:%lu\n", p_user_message, strlen(p_user_message));
            anc_cap_netlink_send_message_to_user(p_user_message, strlen(p_user_message));
        }
    }
}

static struct netlink_kernel_cfg g_netlink_kernel_config = {
    .input  = netlink_receive_message, /* set recv callback */
};

int anc_cap_netlink_init(void)
{
    /* create netlink socket */
    gp_netlink_sock = (struct sock *)netlink_kernel_create(&init_net, NETLINK_ANC, &g_netlink_kernel_config);
    if(NULL == gp_netlink_sock)
    {
        printk("netlink_kernel_create error !\n");
        return -1;
    }
    printk("anc_cap_netlink_init\n");

    return 0;
}

void anc_cap_netlink_exit(void)
{
    if (NULL != gp_netlink_sock)
    {
        netlink_kernel_release(gp_netlink_sock); /* release ..*/
        gp_netlink_sock = NULL;
    }
    printk("anc_cap_netlink_exit!\n");
}