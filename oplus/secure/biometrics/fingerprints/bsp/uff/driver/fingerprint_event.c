#include "include/fingerprint_event.h"
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
#include <linux/version.h>
#include <linux/kfifo.h>
#include <linux/spinlock.h>

int g_fp_driver_event_type = FP_DRIVER_INTERRUPT;
int g_reporte_cond = 0;
DECLARE_WAIT_QUEUE_HEAD(fp_wait_queue);
static struct kfifo fpfifo;
static int is_fingerprint_msg_empty(void);
static int enqueue_fingerprint_msg(struct fingerprint_message_t* msg);
static int dequeue_fingerprint_msg(struct fingerprint_message_t* msg);
static DEFINE_SPINLOCK(fp_lock);

int init_fingerprint_msg(void)
{
    int ret = 0;
    g_reporte_cond = 0;
    deinit_fingerprint_msg();
    ret = kfifo_alloc(&fpfifo, MAX_FPKFIFO_SIZE, GFP_KERNEL);
    if (ret) {
        pr_err("kfifo_alloc fail");
    }
    pr_debug("kfifo_size is %d", kfifo_size(&fpfifo));
    return ret;
}

void deinit_fingerprint_msg(void)
{
    kfifo_free(&fpfifo);

}

static void set_event_condition(int state)
{
    g_reporte_cond = state;
}

static int wake_up_fingerprint_event(int data) {
    int ret = 0;
    (void)data;
    set_event_condition(SEND_FINGERPRINT_EVENT_ENABLE);
    wake_up_interruptible(&fp_wait_queue);
    return ret;
}

int wait_fp_event(void *data, unsigned int size,
                           struct fingerprint_message_t *msg) {
    int ret;
    struct fingerprint_message_t rev_msg = {0};
    if (size == sizeof(rev_msg)) {
        memcpy(&rev_msg, data, size);
    }
    if (is_fingerprint_msg_empty()) {
        pr_debug("wait_event in");
        if ((ret = wait_event_interruptible(fp_wait_queue, g_reporte_cond == 1)) !=
            0) {
            pr_info("fp driver wait event fail, %d", ret);
        }
        pr_debug("wait_event out");
    }
    ret = dequeue_fingerprint_msg(msg);
    set_event_condition(SEND_FINGERPRINT_EVENT_DISABLE);
    return ret;
}

static ssize_t fp_event_node_read(struct file *file, char __user *buf, size_t count, loff_t *pos)
{
    pr_info("fp_event_node_read enter");
    if (file == NULL || count != sizeof(g_fp_driver_event_type)) {
        return -1;
    }
    pr_info("fp_event_node_read,  %d", g_fp_driver_event_type);
    if (copy_to_user(buf, &g_fp_driver_event_type, count)) {
        return -EFAULT;
    }
    pr_info("fp_event_node_read,  %d", g_fp_driver_event_type);
    return count;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
static const struct proc_ops fp_event_func = {
    .proc_write = NULL,
    .proc_read = fp_event_node_read,
};
#else
static struct file_operations fp_event_func = {
    .write = NULL,
    .read = fp_event_node_read,
};
#endif

int fp_evt_register_proc_fs(void)
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

void set_fp_driver_evt_type(int type)
{
    pr_info("set_fp_driver_evt_type, %d", type);
    g_fp_driver_event_type = type; // FP_DRIVER_INTERRUPT
}

int get_fp_driver_evt_type(void)
{
    return g_fp_driver_event_type;
}
int send_fingerprint_msg(int module, int event, void *data,
                             unsigned int size) {
    int ret = 0;
    int need_report = 0;
    struct fingerprint_message_t fingerprint_msg = {0};
    if (get_fp_driver_evt_type() != FP_DRIVER_INTERRUPT) {
        return 0;
    }
    switch (module) {
    case E_FP_TP:
        fingerprint_msg.module = E_FP_TP;
        fingerprint_msg.event = event == 1 ? E_FP_EVENT_TP_TOUCHDOWN : E_FP_EVENT_TP_TOUCHUP;
        fingerprint_msg.out_size = size <= MAX_MESSAGE_SIZE ? size : MAX_MESSAGE_SIZE;
        memcpy(fingerprint_msg.out_buf, data, fingerprint_msg.out_size);
        need_report = 1;
        break;
    case E_FP_LCD:
        fingerprint_msg.module = E_FP_LCD;
        fingerprint_msg.event =
            event == 1 ? E_FP_EVENT_UI_READY : E_FP_EVENT_UI_DISAPPEAR;
        need_report = 1;

        //pr_info("kernel module:%d event:%d - %d", fingerprint_msg.module, event, fingerprint_msg.event);
        break;
    case E_FP_HAL:
        fingerprint_msg.module = E_FP_HAL;
        fingerprint_msg.event = E_FP_EVENT_STOP_INTERRUPT;
        need_report = 1;
        break;
    default:
        fingerprint_msg.module = module;
        fingerprint_msg.event = event;
        need_report = 1;
        pr_info("unknow module, ignored");
        break;
    }
    if (need_report) {
        ret = enqueue_fingerprint_msg(&fingerprint_msg);
        if (ret) {
            pr_err("not enqueue, ignored");
            ret = 0;
            return ret;
        }
        ret = wake_up_fingerprint_event(0);
    }
    return ret;
}

static int is_fingerprint_msg_empty(void) {
    int ret = 0;
    spin_lock(&fp_lock);
    // for init success but kfree and then use fifo
    if (kfifo_size(&fpfifo) != MAX_FPKFIFO_SIZE) {
        pr_err("kfifo_size is NULL");
        ret = -1;
        goto err;
    }
    if (kfifo_len(&fpfifo) < sizeof(struct fingerprint_message_t)) {
        pr_err("msg is NULL");
        ret = -2;
        goto err;
    }
err:
    spin_unlock(&fp_lock);
    return ret;
}

static int enqueue_fingerprint_msg(struct fingerprint_message_t* msg) {
    int  ret = -1;
    int enqueuelen = 0;

    spin_lock(&fp_lock);
    if (kfifo_size(&fpfifo) != MAX_FPKFIFO_SIZE) {
        pr_err("kfifo_size is NULL");
        goto err;
    }
    //  sure the enqueues are effective
    if (kfifo_avail(&fpfifo) < sizeof(struct fingerprint_message_t)) {
        pr_err("kfifo_avail is not full throw module = %d, event = %d", msg->module, msg->event);
        goto err;
    }
    enqueuelen = kfifo_in(&fpfifo, (void*)msg, sizeof(struct fingerprint_message_t));
    if (enqueuelen == 0) {
        pr_err("kfifo_in fail");
        goto err;
    }
    ret = 0;
err:
    spin_unlock(&fp_lock);
    return ret;
}

static int dequeue_fingerprint_msg(struct fingerprint_message_t *msg) {
    int dequeuelen = 0;
    int ret = -1;
    if (is_fingerprint_msg_empty()) {
        pr_err("msg is empty");
        return -2;
    }
    spin_lock(&fp_lock);
    dequeuelen = kfifo_out(&fpfifo, (void*)(msg), sizeof(struct fingerprint_message_t));
    if (dequeuelen == 0) {
        pr_err("no msg ");
        ret = -2;
        goto err;
    }
    if (sizeof(*msg) == dequeuelen) {
        pr_err("msg ok current module = %d, event = %d", msg->module, msg->event);
        ret = 0;
    }
err:
    spin_unlock(&fp_lock);
    return ret;
}
