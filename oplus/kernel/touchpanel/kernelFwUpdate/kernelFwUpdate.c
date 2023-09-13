/***************************************************
 * File:kernelFwUpdate.c
 * 
 * Copyright (c)  2008- 2030  oplus Mobile communication Corp.ltd.
 * Description:
 *             kernelFwUpdate
 * Version:1.0:
 * Date created:2020/07/20
 * TAG: BSP.TP.Init
*/
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/firmware.h>
#include <linux/types.h>
#include <linux/device.h>
#include <linux/kdev_t.h>
#include <linux/major.h>

#include "kernelFwUpdate.h"

#define FW_UPDATE_DEVICE "kernelFwUpdate"
#define MAX_FW_NAME 256

#define FW_UPDATE_INFO(a, arg...)  pr_err("[FW_UPDATE]"FW_UPDATE_DEVICE ": " a, ##arg)

struct device *fw_update_dev = NULL;

static ssize_t debug_level(struct device *dev,
                           struct device_attribute *attr,
                           const char *buf, size_t count)
{
    FW_UPDATE_INFO("%s enter\n", __func__);

    return count;
}

static DEVICE_ATTR(debug_level, S_IRUGO | S_IWUSR, NULL, debug_level);

static const struct attribute *fw_update_attr[] = {
    &dev_attr_debug_level.attr,
    NULL,
};

static const struct attribute_group fw_update_attr_group = {
    .attrs = (struct attribute **) fw_update_attr,
};

static struct class kernel_fw_update_class = {
    .name =      "kernel_fw_update",
    .owner =     THIS_MODULE,
};

static void fw_update_uevent_env(const char *name)
{
    char *envp[2] = {NULL, NULL};
    char fw_name[MAX_FW_NAME] = {0};

    snprintf(fw_name, MAX_FW_NAME, "FIRMWARE=%s", name);
    envp[0] = fw_name;
    kobject_uevent_env(&fw_update_dev->kobj, KOBJ_CHANGE, envp);

    FW_UPDATE_INFO("%s envp[0]:%s\n", __func__, envp[0]);
    return;
}
int request_firmware_select(const struct firmware **firmware_p, const char *name,
                            struct device *device)
{
    int ret = 0;

    fw_update_uevent_env(name);

    ret = request_firmware(firmware_p, name, device);

    return ret;
}
EXPORT_SYMBOL(request_firmware_select);

static int __init kernel_fw_update_init(void)
{
    int ret = 0;

    FW_UPDATE_INFO("%s enter\n", __func__);

    ret = class_register(&kernel_fw_update_class);
    if (ret < 0) {
        FW_UPDATE_INFO("kernel_fw_update: class_register fail\n");
        return ret;
    }

    fw_update_dev = device_create(&kernel_fw_update_class, NULL,
            MKDEV(0, 0), NULL, "kernel_fw_update");
    if (fw_update_dev) {
        ret = sysfs_create_group(&fw_update_dev->kobj, &fw_update_attr_group);
        if(ret < 0) {
            FW_UPDATE_INFO("kernel_fw_update:sysfs_create_group fail\n");
            return ret;
        }
    } else {
        FW_UPDATE_INFO("kernel_fw_update:device_create fail\n");
        ret = -1;
        goto out_class;
        return ret;
    }

out_class:
    class_unregister(&kernel_fw_update_class);

    return ret;
}

static void __exit kernel_fw_update_exit(void)
{
    if (fw_update_dev) {
         sysfs_remove_group(&fw_update_dev->kobj, &fw_update_attr_group);
         class_unregister(&kernel_fw_update_class);
    }
}

fs_initcall(kernel_fw_update_init);
module_exit(kernel_fw_update_exit);
MODULE_AUTHOR("Qicai.gu");
MODULE_DESCRIPTION("oplus kernel firmware loading support");
MODULE_LICENSE("GPL");
