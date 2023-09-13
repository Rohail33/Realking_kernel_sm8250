// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/types.h>
#include <net/sock.h>
#include <net/netlink.h>
#ifdef CONFIG_MTK_DRIVER
#include "et713_mtk.h"
#else
#include "et713.h"
#endif
#define MAX_MSGSIZE 32
#define NETLINK_EGIS 23

static int pid = -1;
static struct sock *nl_sk = NULL;

void egis_sendnlmsg(char *msg)
{
	struct sk_buff *skb_1;
	struct nlmsghdr *nlh;
	int len = NLMSG_SPACE(MAX_MSGSIZE);
	int ret = 0;
	if (!msg || !nl_sk || !pid) {
		return ;
	}
	skb_1 = alloc_skb(len, GFP_KERNEL);
	if (!skb_1) {
		pr_err("alloc_skb error\n");
		return;
	}

	nlh = nlmsg_put(skb_1, 0, 0, 0, MAX_MSGSIZE, 0);

	NETLINK_CB(skb_1).portid = 0;
	NETLINK_CB(skb_1).dst_group = 0;

	if (nlh != NULL) {
		memcpy(NLMSG_DATA(nlh), msg, sizeof(char));
		pr_debug("send message: %d\n", *(char *)NLMSG_DATA(nlh));
	}

	ret = netlink_unicast(nl_sk, skb_1, pid, MSG_DONTWAIT);
	if (!ret) {
		pr_err("send msg from kernel to usespace failed ret 0x%x\n", ret);
	}
}

static void nl_data_ready(struct sk_buff *__skb)
{
	struct sk_buff *skb;
	struct nlmsghdr *nlh;
	char str[100];
	skb = skb_get (__skb);
	if(skb->len >= NLMSG_SPACE(0))
	{
		nlh = nlmsg_hdr(skb);

		if (nlh != NULL) {
			memcpy(str, NLMSG_DATA(nlh), sizeof(str));
			pid = nlh->nlmsg_pid;
		}

		kfree_skb(skb);
	}
}


int egis_netlink_init(void)
{
	struct netlink_kernel_cfg netlink_cfg;
	memset(&netlink_cfg, 0, sizeof(struct netlink_kernel_cfg));

	netlink_cfg.groups = 0;
	netlink_cfg.flags = 0;
	netlink_cfg.input = nl_data_ready;
	netlink_cfg.cb_mutex = NULL;

	nl_sk = netlink_kernel_create(&init_net, NETLINK_EGIS,
			&netlink_cfg);

	if(!nl_sk){
		pr_err("create netlink socket error\n");
		return 1;
	}

	return 0;
}

void egis_netlink_exit(void)
{
	if(nl_sk != NULL){
		netlink_kernel_release(nl_sk);
		nl_sk = NULL;
	}

	pr_info("self module exited\n");
}

