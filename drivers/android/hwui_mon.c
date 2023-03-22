// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 LibXZR <i@xzr.moe>.
 */

#define pr_fmt(fmt) "hwui_mon: " fmt

#include <crypto/hash.h>
#include <crypto/sha.h>
#include <linux/hwui_mon.h>
#include <linux/namei.h>
#include <linux/uaccess.h>
#include <linux/uprobes.h>

#define ZYGOTE_PATH "/system/bin/app_process64"
#define HWUI_PATH "/system/lib64/libhwui.so"
#define UI_FRAME_INFO(pt_regs, regn) ((s64 *)((pt_regs)->regs[regn]))
#define UI_FRAME_INFO_SIZE 12
#define VSYNC_TIME(buf) ((ktime_t)buf[3])
#define CHECKSUM_BUF_SIZE 1024

static struct {
	// sha1sum of libhwui.so
	char checksum[SHA1_DIGEST_SIZE * 2 + 1];
	// Both inject into android_view_ThreadedRenderer_syncAndDrawFrame()
	/**
	 * Inject 1. Save the destination pointer before calling
	 * env->GetLongArrayRegion() as the register is most
	 * likely to be overwritten.
	 */
	loff_t inject1_offset;
	// The register to save.
	int reg;
	/**
	 * Inject 2. After calling env->GetLongArrayRegion(),
	 * it's time to fetch the result.
	 */
	loff_t inject2_offset;
} hwui_info[] = {
// Here, record info to support different versions of libhwui.so.
#define INFO_COUNT 1
	{
		.checksum = "dea5f6def7787d5c89fe66e7f888455953fd8880",
		.inject1_offset = 0x26868C,
		.reg = 4,
		.inject2_offset = 0x2686A0
	}
};

static int hwui_index;
static LIST_HEAD(receivers);
static DECLARE_RWSEM(receiver_lock);

static int hwui_inject1_handler(
        struct uprobe_consumer *self, struct pt_regs *regs)
{
	current->ui_frame_info = UI_FRAME_INFO(regs, hwui_info[hwui_index].reg);
	return 0;
}

static struct uprobe_consumer hwui_inject1_consumer = {
	.handler = &hwui_inject1_handler
};

static int hwui_inject2_handler(
        struct uprobe_consumer *self, struct pt_regs *regs)
{
	s64 buf[UI_FRAME_INFO_SIZE];
	unsigned int ui_frame_time;
	struct hwui_mon_receiver *receiver;
	int ret;

	ret = copy_from_user(buf, current->ui_frame_info, sizeof(buf));
	if (ret)
		goto error;

	ui_frame_time = ktime_sub(ktime_get(), VSYNC_TIME(buf)) / NSEC_PER_USEC;

	down_read(&receiver_lock);
	list_for_each_entry(receiver, &receivers, list) {
		if (ui_frame_time >= receiver->jank_frame_time)
			receiver->jank_callback(ui_frame_time);
	}
	up_read(&receiver_lock);

	return 0;
error:
	pr_err("Failed to get ui_frame_info, ret = %d", ret);
	return 0;
}

static struct uprobe_consumer hwui_inject2_consumer = {
	.handler = &hwui_inject2_handler
};

static int hwui_mon_checksum(void)
{
	const char *TAB = "0123456789abcdef";
	struct crypto_shash *shash;
	struct file *f;
	u8 buf[CHECKSUM_BUF_SIZE];
	u8 digest[SHA1_DIGEST_SIZE];
	char digest_str[SHA1_DIGEST_SIZE * 2 + 1];
	SHASH_DESC_ON_STACK(desc, NULL);
	ssize_t count;
	int i, ret;

	f = filp_open(HWUI_PATH, O_RDONLY, 0);
	if (IS_ERR(f)) {
		ret = PTR_ERR(f);
		pr_err("Unable to open libhwui, ret = %d", ret);
		return ret;
	}

	shash = crypto_alloc_shash("sha1", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(shash)) {
		ret = PTR_ERR(shash);
		pr_err("Unable to alloc shash, ret = %d", ret);
		goto close_file;
	}

	desc->tfm = shash;
	desc->flags = 0;
	ret = crypto_shash_init(desc);
	if (ret) {
		pr_err("Unable to init shash, ret = %d", ret);
		goto free_shash;
	}

	while (1) {
		count = kernel_read(f, buf, CHECKSUM_BUF_SIZE, &f->f_pos);
		if (!count)
			break;

		ret = crypto_shash_update(desc, buf, count);
		if (ret) {
			pr_err("Unable to update shash, ret = %d", ret);
			goto free_shash;
		}
	}

	ret = crypto_shash_final(desc, digest);
	if (ret) {
		pr_err("Unable to calculate shash, ret = %d", ret);
		goto free_shash;
	}

	for (i = 0; i < SHA1_DIGEST_SIZE; i++) {
		digest_str[i * 2] = TAB[(digest[i] >> 4) & 0xF];
		digest_str[i * 2 + 1] = TAB[digest[i] & 0xF];
	}
	digest_str[2 * SHA1_DIGEST_SIZE] = '\0';

	ret = -ENOENT;
	for (i = 0; i < INFO_COUNT; i++) {
		if (!strcmp(digest_str, hwui_info[i].checksum)) {
			hwui_index = i;
			ret = 0;
			break;
		}
	}
	if (ret)
		pr_err("Current checksum is %s, not recorded", digest_str);
	else
		pr_info("Current checksum is %s, found at index %d",
		        digest_str, hwui_index);

free_shash:
	crypto_free_shash(shash);
close_file:
	filp_close(f, NULL);
	return ret;
}

static void hwui_mon_init(void)
{
	struct path hwui_path;
	struct inode *hwui_inode;
	int ret;

	if (hwui_mon_checksum())
		goto error;

	ret = kern_path(HWUI_PATH, 0, &hwui_path);
	if (ret)
		goto error;

	hwui_inode = d_inode(hwui_path.dentry);

	ret = uprobe_register(hwui_inode,
	    hwui_info[hwui_index].inject1_offset, &hwui_inject1_consumer);
	if (ret)
		goto clean;

	ret = uprobe_register(hwui_inode,
	    hwui_info[hwui_index].inject2_offset, &hwui_inject2_consumer);
	if (ret)
		goto clean;

	return;
clean:
	path_put(&hwui_path);
error:
	pr_err("Unable to init, ret = %d", ret);
}

void hwui_mon_handle_exec(struct filename *filename)
{
	static atomic_t need_initialize = ATOMIC_INIT(1);

	// Fast path.
	if (likely(!atomic_read(&need_initialize)))
		return;

	// Initialize it when zygote starts to make sure filesystems are
	// properly mounted. It should only be initialized once.
	if (unlikely(!strcmp(ZYGOTE_PATH, filename->name) &&
	        atomic_fetch_add_unless(&need_initialize, -1, 0))) {
		hwui_mon_init();
	}
}

int register_hwui_mon(struct hwui_mon_receiver *receiver)
{
	down_write(&receiver_lock);
	list_add(&receiver->list, &receivers);
	up_write(&receiver_lock);
	return 0;
}

int unregister_hwui_mon(struct hwui_mon_receiver *receiver)
{
	down_write(&receiver_lock);
	list_del(&receiver->list);
	up_write(&receiver_lock);
	return 0;
}
