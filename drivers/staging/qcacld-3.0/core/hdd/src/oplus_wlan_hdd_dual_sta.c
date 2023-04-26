/**************************************************************
* Copyright (c) 2019 - 2022 OPLUS Mobile Comm Corp., Ltd.
* *
* File : oplus_wlan_hdd_dual_sta.cpp
* Description: create proc file to show if current router is 1x1 IOT router
* Version : 1.0
* Date : 2019-9-12
* Author : zhaomengqing@oplus.com
* ---------------- Revision History: --------------------------
* <version> <date> < author > <desc>
*
****************************************************************/
//#ifdef VENDOR_EDIT
//Add for dual Sta: do not enable dual sta when main wifi's Router is 1x1 IOT Router

#include <sme_api.h>
#include <wlan_hdd_includes.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/proc_fs.h> /* Necessary because we use the proc fs */
#include <linux/uaccess.h> /* for copy_to_user */


#define PROCFS_DRIVER_OPLUS_DIR "oplus_dualSta"
#define PROCFS_DRIVER_1x1IOT_NAME "is1x1IOTRouter"
#define PROCFS_DRIVER_DUMP_PERM 0644

static struct proc_dir_entry *proc_file_driver, *proc_dir_driver;

int is1x1IOTRouter = 0;
char routerBssid[32] = {0};

/**
 * hdd_driver_oplus_read() - perform read operation in proc file
 * memory dump proc file
 * @file  - handle for the proc file.
 * @buf   - pointer to user space buffer.
 * @count - number of bytes to be read.
 * @pos   - offset in the from buffer.
 *
 * This function performs read operation for the is1x1IOTRouter proc file.
 *
 * Return: number of bytes read on success
 *         negative error code in case of failure
 *         0 in case of no more data
 */
static ssize_t hdd_driver_oplus_read(struct file *file, char __user *buf,
                       size_t count, loff_t *pos)
{
    ssize_t len;
    char isIotRouter[64] = {0};

    len = sprintf(isIotRouter, "%s=%d\n", routerBssid, is1x1IOTRouter);

    if (len > *pos) {
        len -= *pos;
    } else {
        len = 0;
    }

    if (copy_to_user(buf, isIotRouter, (len < count ? len : count))) {
        return -EFAULT;
    }

    *pos += len < count ? len : count;
    return (len < count ? len : count);
}

/**
 * struct oplus_dual_sta_fops - file operations
 * @read - read function for proc file is1x1IOTRouter.
 *
 * This structure initialize the file operation handle
 */
static const struct file_operations oplus_dual_sta_fops = {
    read: hdd_driver_oplus_read
};

/**
 * hdd_driver_oplus_procfs_init() - Initialize procfs
 *
 * This function create file under proc file system to be used later for
 * getting if current router is a 1x1 IOT Router
 *
 * Return:   0 on success, error code otherwise.
 */
static int hdd_driver_oplus_procfs_init(void)
{
    proc_dir_driver = proc_mkdir(PROCFS_DRIVER_OPLUS_DIR, NULL);
    if (proc_dir_driver == NULL) {
        pr_debug("Could not initialize /proc/%s\n",
             PROCFS_DRIVER_OPLUS_DIR);
        return -ENOMEM;
    }

    proc_file_driver = proc_create_data(PROCFS_DRIVER_1x1IOT_NAME,
                     PROCFS_DRIVER_DUMP_PERM, proc_dir_driver,
                     &oplus_dual_sta_fops, NULL);
    if (proc_file_driver == NULL) {
        remove_proc_entry(PROCFS_DRIVER_1x1IOT_NAME, proc_dir_driver);
        pr_debug("Could not initialize /proc/%s\n",
              PROCFS_DRIVER_1x1IOT_NAME);
        return -ENOMEM;
    }

    pr_debug("/proc/%s/%s created\n", PROCFS_DRIVER_OPLUS_DIR,
         PROCFS_DRIVER_1x1IOT_NAME);
    return 0;
}

/**
 * hdd_driver_oplus_procfs_remove() - Remove file/dir under procfs
 *
 * This function removes file/dir under proc file system
 *
 * Return:  None
 */
static void hdd_driver_oplus_procfs_remove(void)
{
    remove_proc_entry(PROCFS_DRIVER_1x1IOT_NAME, proc_dir_driver);
    pr_debug("/proc/%s/%s removed\n", PROCFS_DRIVER_OPLUS_DIR,
                      PROCFS_DRIVER_1x1IOT_NAME);
    remove_proc_entry(PROCFS_DRIVER_OPLUS_DIR, NULL);
    pr_debug("/proc/%s removed\n", PROCFS_DRIVER_OPLUS_DIR);
}

/**
 * hdd_driver_oplus_init() - Intialization function for driver
 *
 * This function creates proc file
 *
 * Return - 0 on success, error otherwise
 */
int hdd_driver_oplus_init(void)
{
    int status;

    status = hdd_driver_oplus_procfs_init();
    if (status) {
        hdd_err("Failed to create proc file");
        return status;
    }

    return 0;
}

/**
 * hdd_driver_oplus_deinit() - De initialize driver
 *
 * This function removes proc file.
 *
 * Return: None
 */
void hdd_driver_oplus_deinit(void)
{
    hdd_driver_oplus_procfs_remove();
}

//#endif /* VENDOR_EDIT */

