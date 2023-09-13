// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2018-2020 Oplus. All rights reserved.
 */
/*
 * oplus_misc.c
 *
 * api interface about oneplus
 *
 */
#include <linux/reboot.h>
#include <linux/input.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/timer.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <soc/oplus/system/oplus_misc.h>
#include <linux/sched/signal.h>
#include <linux/syscalls.h>

void send_sig_to_get_trace(char *name)
{
	struct task_struct *g, *t;

	for_each_process_thread(g, t) {
		if (!strncmp(t->comm, name, TASK_COMM_LEN)) {
			do_send_sig_info(SIGQUIT, SEND_SIG_FORCED, t, PIDTYPE_TGID);
			msleep(500);
			ksys_sync();
			break;
		}
	}
}


