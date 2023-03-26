// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 LibXZR <i@xzr.moe>.
 */

#define pr_fmt(fmt) "jank_detect_demo: " fmt

#include <linux/hwui_mon.h>
#include <linux/module.h>

static void handler(unsigned int ui_frame_time)
{
	pr_info("Detect jank in %s with frametime = %d",
	        current->comm, ui_frame_time);
}

static struct hwui_mon_receiver receiver = {
	.jank_frame_time = 4000,
	.jank_callback = handler
};

static int __init demo_init(void)
{
	return register_hwui_mon(&receiver);
}

module_init(demo_init);
