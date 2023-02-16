// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021-2023 Dakkshesh <dakkshesh5@gmail.com>.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#ifdef CONFIG_AUTO_KPROFILES_MSM_DRM
#include <linux/msm_drm_notify.h>
#elif defined(CONFIG_AUTO_KPROFILES_MI_DRM)
#include <drm/drm_notifier_mi.h>
#elif defined(CONFIG_AUTO_KPROFILES_FB)
#include <linux/fb.h>
#endif
#include "version.h"

#ifdef CONFIG_AUTO_KPROFILES_MSM_DRM
#define KP_EVENT_BLANK MSM_DRM_EVENT_BLANK
#define KP_BLANK_POWERDOWN MSM_DRM_BLANK_POWERDOWN
#define KP_BLANK_UNBLANK MSM_DRM_BLANK_UNBLANK
#define kprofiles_events msm_drm_notifier
#elif defined(CONFIG_AUTO_KPROFILES_MI_DRM)
#define KP_EVENT_BLANK MI_DRM_EVENT_BLANK
#define KP_BLANK_POWERDOWN MI_DRM_BLANK_POWERDOWN
#define KP_BLANK_UNBLANK MI_DRM_BLANK_UNBLANK
#define kprofiles_events mi_drm_notifier
#elif defined(CONFIG_AUTO_KPROFILES_FB)
#define KP_EVENT_BLANK FB_EVENT_BLANK
#define KP_BLANK_POWERDOWN FB_BLANK_POWERDOWN
#define KP_BLANK_UNBLANK FB_BLANK_UNBLANK
#define kprofiles_events fb_event
#endif

static unsigned int kp_override_mode;
static bool kp_override;
#ifdef CONFIG_AUTO_KPROFILES
static bool screen_on = true;
#endif

static bool auto_kprofiles __read_mostly = true;
module_param(auto_kprofiles, bool, 0664);

static unsigned int kp_mode = CONFIG_DEFAULT_KP_MODE;
module_param(kp_mode, int, 0664);

DEFINE_MUTEX(kplock);

/*
 * This function can be used to change profile to any given mode
 * for a specific period of time during any in-kernel event,
 * then return to the previously active mode.
 *
 * usage example: kp_set_mode_rollback(3, 55)
 */
void kp_set_mode_rollback(unsigned int level, unsigned int duration_ms)
{
#ifdef CONFIG_AUTO_KPROFILES
	if (!screen_on)
		return;
#endif

	if (!auto_kprofiles)
		return;

	if (unlikely(level > 3)) {
		pr_err("%s: Invalid mode requested, Skipping mode change\n",
		       __func__);
		return;
	}

	mutex_lock(&kplock);
	kp_override_mode = level;
	kp_override = true;
	msleep(duration_ms);
	kp_override = false;
	mutex_unlock(&kplock);
}
EXPORT_SYMBOL(kp_set_mode_rollback);

/*
 * This function can be used to change profile to
 * any given mode during any in-kernel event.
 *
 * usage example: kp_set_mode(3)
 */
void kp_set_mode(unsigned int level)
{
#ifdef CONFIG_AUTO_KPROFILES
	if (!screen_on)
		return;
#endif

	if (unlikely(level > 3)) {
		pr_err("%s: Invalid mode requested, Skipping mode change\n",
		       __func__);
		return;
	}

	if (auto_kprofiles)
		kp_mode = level;
}
EXPORT_SYMBOL(kp_set_mode);

/*
 * This function returns a number from 0 and 3 depending on the profile
 * selected. The former can be used in conditions to disable/enable
 * or tune kernel features according to a profile mode.
 *
 * usage exmaple:
 *
 * switch (kp_active_mode()) {
 * case 1:
 *	things to be done when battery profile is active
 *	break;
 * case 2:
 *	things to be done when balanced profile is active
 * 	break;
 * case 3:
 *	things to be done when performance profile is active
 *	break;
 * default:
 *	things to be done when kprofiles is disabled
 *	break;
 * }
 *
 */
int kp_active_mode(void)
{
#ifdef CONFIG_AUTO_KPROFILES
	if (!screen_on && auto_kprofiles)
		return 1;
#endif

	if (kp_override)
		return kp_override_mode;

	if (unlikely(kp_mode > 3)) {
		kp_mode = 0;
		pr_info("%s: Invalid value passed, falling back to level 0\n",
			__func__);
	}

	return kp_mode;
}
EXPORT_SYMBOL(kp_active_mode);

#ifdef CONFIG_AUTO_KPROFILES
static inline int kp_notifier_callback(struct notifier_block *self,
				       unsigned long event, void *data)
{
	struct kprofiles_events *evdata = data;
	unsigned int blank;

	if (event != KP_EVENT_BLANK)
		return 0;

	if (evdata && evdata->data) {
		blank = *(int *)(evdata->data);
		switch (blank) {
		case KP_BLANK_POWERDOWN:
			if (!screen_on)
				break;
			screen_on = false;
			break;
		case KP_BLANK_UNBLANK:
			if (screen_on)
				break;
			screen_on = true;
			break;
		default:
			break;
		}
	}

	return NOTIFY_OK;
}

static struct notifier_block kp_notifier_block = {
	.notifier_call = kp_notifier_callback,
};

static int kprofiles_register_notifier(void)
{
	int ret = 0;

#ifdef CONFIG_AUTO_KPROFILES_MSM_DRM
	ret = msm_drm_register_client(&kp_notifier_block);
#elif defined(CONFIG_AUTO_KPROFILES_MI_DRM)
	ret = mi_drm_register_client(&kp_notifier_block);
#elif defined(CONFIG_AUTO_KPROFILES_FB)
	ret = fb_register_client(&kp_notifier_block);
#endif

	return ret;
}

static void kprofiles_unregister_notifier(void)
{
#ifdef CONFIG_AUTO_KPROFILES_MSM_DRM
	msm_drm_unregister_client(&kp_notifier_block);
#elif defined(CONFIG_AUTO_KPROFILES_MI_DRM)
	mi_drm_unregister_client(&kp_notifier_block);
#elif defined(CONFIG_AUTO_KPROFILES_FB)
	fb_unregister_client(&kp_notifier_block);
#endif
}

#else
static inline int kprofiles_register_notifier(void)
{
	return 0;
}
static inline void kprofiles_unregister_notifier(void)
{
}
#endif

static int __init kp_init(void)
{
	int ret = 0;

	ret = kprofiles_register_notifier();
	if (ret)
		pr_err("Failed to register notifier, err: %d\n", ret);

	pr_info("Kprofiles " KPROFILES_VERSION
		" loaded. Visit https://github.com/dakkshesh07/Kprofiles/blob/main/README.md for information.\n");
	pr_info("Copyright (C) 2021-2023 Dakkshesh <dakkshesh5@gmail.com>.\n");

	return ret;
}
module_init(kp_init);

static void __exit kp_exit(void)
{
	kprofiles_unregister_notifier();
}
module_exit(kp_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("KernelSpace Profiles");
MODULE_AUTHOR("Dakkshesh <dakkshesh5@gmail.com>");
MODULE_VERSION(KPROFILES_VERSION);
