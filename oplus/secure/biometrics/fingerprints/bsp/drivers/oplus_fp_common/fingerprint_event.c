#include "../include/fingerprint_event.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>

static struct fingerprint_message_t g_fingerprint_msg = {0};
int g_fp_dirver_event_type = FP_DIRVER_NETLINK;
int g_reporte_condition = 0;
DECLARE_WAIT_QUEUE_HEAD(fp_queue);

static void set_event_condition(int state)
{
    g_reporte_condition = state;
}

static int wake_up_fingerprint_event(int data) {
    int ret = 0;
    (void)data;
    set_event_condition(SEND_FINGERPRINT_EVENT_ENABLE);
    wake_up_interruptible(&fp_queue);
    return ret;
}

int wait_fingerprint_event(void *data, unsigned int size,
                           struct fingerprint_message_t **msg) {
    int ret;
    struct fingerprint_message_t rev_msg = {0};
    if (size == sizeof(rev_msg)) {
      memcpy(&rev_msg, data, size);
    }

    if ((ret = wait_event_interruptible(fp_queue, g_reporte_condition == 1)) !=
        0) {
      pr_info("fp driver wait event fail, %d", ret);
    }
    if (msg != NULL) {
      *msg = &g_fingerprint_msg;
    }
    set_event_condition(SEND_FINGERPRINT_EVENT_DISABLE);
    return ret;
}

static ssize_t fp_event_node_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
    pr_info("fp_event_node_read enter");
    if (file == NULL || count != sizeof(g_fp_dirver_event_type)) {
        return -1;
    }
    pr_info("fp_event_node_read,  %d", g_fp_dirver_event_type);
    if (copy_to_user(buf, &g_fp_dirver_event_type, count)) {
      return -EFAULT;
    }
    pr_info("fp_event_node_read,  %d", g_fp_dirver_event_type);
    return count;
}

static struct file_operations fp_event_func = {
    .write = NULL,
    .read = fp_event_node_read,
};

int fp_event_register_proc_fs(void)
{
    int ret = 0;
    char *tee_node = "fp_kernel_event";
    struct proc_dir_entry *event_node_dir = NULL;

    event_node_dir = proc_create(tee_node, 0666, NULL, &fp_event_func);
    if (event_node_dir == NULL) {
        ret = -1;
        goto exit;
    }

    return 0;
exit :
    return ret;
}

void set_fp_driver_event_type(int type)
{
    pr_info("set_fp_driver_event_type, %d", type);
    g_fp_dirver_event_type = type; // FP_DRIVER_INTERRUPT
}

int get_fp_driver_event_type(void)
{
    return g_fp_dirver_event_type;
}

int send_fingerprint_message(int module, int event, void *data,
                             unsigned int size) {
    int ret = 0;
    int need_report = 0;
    if (get_fp_driver_event_type() != FP_DRIVER_INTERRUPT) {
        return 0;
    }
    memset(&g_fingerprint_msg, 0, sizeof(g_fingerprint_msg));
    switch (module) {
    case E_FP_TP:
        g_fingerprint_msg.module = E_FP_TP;
        g_fingerprint_msg.event = event == 1 ? E_FP_EVENT_TP_TOUCHDOWN : E_FP_EVENT_TP_TOUCHUP;
        g_fingerprint_msg.out_size = size <= MAX_MESSAGE_SIZE ? size : MAX_MESSAGE_SIZE;
        memcpy(g_fingerprint_msg.out_buf, data, g_fingerprint_msg.out_size);
        need_report = 1;
        break;
    case E_FP_LCD:
        g_fingerprint_msg.module = E_FP_LCD;
        g_fingerprint_msg.event =
            event == 1 ? E_FP_EVENT_UI_READY : E_FP_EVENT_UI_DISAPPEAR;
        need_report = 1;
        break;
    case E_FP_HAL:
        g_fingerprint_msg.module = E_FP_HAL;
        g_fingerprint_msg.event = E_FP_EVENT_STOP_INTERRUPT;
        need_report = 1;
        break;
    default:
        g_fingerprint_msg.module = module;
        g_fingerprint_msg.event = event;
        need_report = 1;
        pr_info("unknow module, ignored");
        break;
    }
    if (need_report)
        ret = wake_up_fingerprint_event(0);
    return ret;
}