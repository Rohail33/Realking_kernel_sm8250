// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 LibXZR <i@xzr.moe>.
 */
#ifndef _HWUI_MON_H_
#define _HWUI_MON_H_
#include <linux/fs.h>
#include <linux/list.h>

typedef void (*hwui_frame_handler) (unsigned int ui_frame_time);

struct hwui_mon_receiver {
	struct list_head list;
	unsigned int jank_frame_time;
	/**
	 * This callback runs in application process context.
	 * You MUST NOT do heavy jobs in it, otherwise it'll
	 * block the UI thread and lead to extra janks.
	 */
	hwui_frame_handler jank_callback;
};

void hwui_mon_handle_exec(struct filename *);

// You should NOT free the receiver before unregister.
// DO NOT allocate it on stack.
int register_hwui_mon(struct hwui_mon_receiver *receiver);
int unregister_hwui_mon(struct hwui_mon_receiver *receiver);

#endif /* _HWUI_MON_H_ */
