#include <linux/init.h>
#include <linux/module.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/err.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/slab.h>
#include <linux/compat.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <net/netlink.h>
#include <linux/uaccess.h>
#include <linux/types.h>
#include <linux/notifier.h>
#include "fingerprint_log.h"
#include "fingerprint_ree_dts.h"
#include "fingerprint_ree_spi.h"
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/of_gpio.h>
#include <linux/timer.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/pm_qos.h>
#include <linux/cpufreq.h>
#include <linux/time.h>
#include <linux/types.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/version.h>
#include "../include/fingerprint_event.h"
#include "../include/wakelock.h"

#define FP_DEV_NAME "fingerprint_dev"
struct fp_underscreen_info g_fp_touchinfo;
static unsigned int        g_last_tp_state = 0;
#define SPIDEV_MAJOR            153
#define N_SPI_MINORS            32  /* ... up to 256 */
#define WAKELOCK_HOLD_IRQ_TIME 500 /* in ms */

static DECLARE_BITMAP(minors, N_SPI_MINORS);
#define SPI_MODE_MASK (SPI_CPHA|SPI_CPOL|SPI_CS_HIGH|SPI_LSB_FIRST|SPI_3WIRE|SPI_LOOP|SPI_NO_CS|SPI_READY)

static LIST_HEAD(device_list);
static DEFINE_MUTEX(device_list_lock);
static unsigned bufsiz = 4096;
module_param(bufsiz, uint, S_IRUGO);
MODULE_PARM_DESC(bufsiz, "data bytes in biggest supported SPI message");
static struct wake_lock fp_wakelock;

static void ree_spi_complete(void *arg)
{
    func_enter();
    complete(arg);
    func_exit();
}
int spi_malloc_buffer(struct spidev_data  *spidev, unsigned int size)
{
    if (!spidev->buffer) {
        spidev->buffer = kmalloc(bufsiz, GFP_KERNEL);
        if (!spidev->buffer) {
            dev_dbg(&spidev->spi->dev, "open/ENOMEM\n");
            return -2;
        }
    }
    return 0;
}

void spi_buffer_free(struct spidev_data  *spidev)
{
    if (spidev->buffer) {
        kfree(spidev->buffer);
        spidev->buffer = NULL;
    }
}

static ssize_t ree_spi_sync(struct spidev_data *spidev, struct spi_message *message)
{
    DECLARE_COMPLETION_ONSTACK(done);
    int status;
    func_enter();
    message->complete = ree_spi_complete;
    message->context = &done;

    spin_lock_irq(&spidev->spi_lock);
    if (spidev->spi == NULL) {
        status = -ESHUTDOWN;
    } else {
        status = spi_async(spidev->spi, message);
    }
    spin_unlock_irq(&spidev->spi_lock);

    if (status == 0) {
        wait_for_completion(&done);
        status = message->status;
        if (status == 0) {
            status = message->actual_length;
        }
    }
    func_exit();
    return status;
}
struct ree_spi_t {
    char *tx_buf;
    char *rx_buf;
    unsigned int len;
};

static inline ssize_t spi_sync_read_internal(struct spidev_data *spidev, char *tx_buf, char *rx_buf, size_t len)
{
    int ret = 0;
    struct spi_transfer t = {
        .tx_buf     = tx_buf,
        .rx_buf     = rx_buf,
        .len        = len,
        .speed_hz = 2000000
    };
    struct spi_message  m;
    struct spi_device   *spi;
    spin_lock_irq(&spidev->spi_lock);
    spi = spi_dev_get(spidev->spi);
    spin_unlock_irq(&spidev->spi_lock);
    t.speed_hz = spi->max_speed_hz;
    FP_LOGI("speed_hz =%d", t.speed_hz);
    spi_message_init(&m);
    spi_message_add_tail(&t, &m);
    ret = ree_spi_sync(spidev, &m);
    return ret;
}

static ssize_t fingerprint_read(struct file * f, char __user *buf, size_t count, loff_t *offset)
{
    struct fingerprint_message_t rcv_msg = {0};
    if (buf == NULL || f == NULL || count != sizeof(struct fingerprint_message_t)) {
        return -1;
    }
    FP_LOGI("begin wait for driver event");
    if (wait_fp_event(NULL, 0, &rcv_msg)) {
        return -2;
    }
    if (copy_to_user(buf, &rcv_msg, count)) {
        return -EFAULT;
    }
    FP_LOGI("end wait for driver event");
    return count;
}

static ssize_t fingerprint_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
    int retval = 0;
    struct spidev_data  *spidev;
    spidev = filp->private_data;
    struct ree_spi_t spi_data;
    memset(&spi_data, 0, sizeof(spi_data));
    u8 *tx_buf = NULL;
    u8 *rx_buf = NULL;
    uint32_t len;

    FP_LOGI("%s: fingerprint read debug 0\n", __func__);
    if (count < sizeof(struct ree_spi_t)) {
        FP_LOGI("%s: count:%d, sizof ree_spi_t:%d \n", __func__, count, sizeof(struct ree_spi_t));
        retval = -EINVAL;
        return retval;
    }

    if (copy_from_user(&spi_data, buf, sizeof(struct ree_spi_t))) {
        FP_LOGI("%s: Failed to copy 0 from user to kernel\n", __func__);
        retval = -EFAULT;
        return retval;
    }

    if (spi_data.rx_buf == NULL || spi_data.tx_buf == NULL) {
        FP_LOGI("%s: read buf and write buf can not equal to NULL simultaneously.\n", __func__);
        retval = -EINVAL;
        return retval;
    }
    len = spi_data.len;
    if (len % 1024 != 0 && len > 1024) {
        len = ((spi_data.len / 1024) + 1) * 1024;
    }

    tx_buf = kzalloc(len, GFP_KERNEL);
    if (NULL == tx_buf) {
        FP_LOGI("%s: failed to allocate raw tx buffer\n", __func__);
        retval = -EMSGSIZE;
        return retval;
    }

    rx_buf = kzalloc(len, GFP_KERNEL);
    if (NULL == rx_buf) {
        kfree(tx_buf);
        FP_LOGI("%s: failed to allocate raw rx buffer\n", __func__);
        retval = -EMSGSIZE;
        return retval;
    }

    if (copy_from_user(tx_buf, spi_data.tx_buf, spi_data.len)) {
        kfree(tx_buf);
        kfree(rx_buf);
        FP_LOGI("Failed to copy transfer data from user to kernel\n");
        retval = -EFAULT;
        return retval;
    }
    spi_sync_read_internal(spidev, tx_buf, rx_buf, len);
    if (copy_to_user(spi_data.rx_buf, rx_buf, spi_data.len)) {
        FP_LOGI("Failed to copy gf_ioc_transfer_raw from kernel to user\n");
        retval = -EFAULT;
    }
    kfree(tx_buf);
    kfree(rx_buf);
    return spi_data.len;
}


void fingerprint_setup_conf(struct spidev_data *dev, u32 speed)
{
    dev->spi->mode = SPI_MODE_0;
    // dev->spi->max_speed_hz = max_speed_hz;
    dev->spi->bits_per_word = 8;
    if (spi_setup(dev->spi)) {
        FP_LOGI("%s, failed to setup spi conf\n", __func__);
    }
}

static void fp_enable_irq(struct spidev_data *fp_dev) {
    if (fp_dev->irq_enabled) {
        pr_warn("IRQ has been enabled.\n");
    } else {
        enable_irq(fp_dev->irq);
        fp_dev->irq_enabled = 1;
    }
}

static void fp_disable_irq(struct spidev_data *fp_dev) {
    if (fp_dev->irq_enabled) {
        fp_dev->irq_enabled = 0;
        disable_irq(fp_dev->irq);
    } else {
        pr_warn("IRQ has been disabled.\n");
    }
}

static int fp_read_irq_value(struct spidev_data *fp_dev) {
    if (fp_dev->irq_gpio < 0) {
        pr_warn("err, irq_gpio not init.\n");
        return -1;
    }

    int irq_value = gpio_get_value(fp_dev->irq_gpio);
    pr_info("%s, irq_value = %d\n", __func__, irq_value);
    return irq_value;
}

static irqreturn_t fp_irq_handler(int irq, void *handle) {
    char msg = NETLINK_EVENT_IRQ;
    wake_lock_timeout(&fp_wakelock, msecs_to_jiffies(WAKELOCK_HOLD_IRQ_TIME));
    send_fingerprint_msg(E_FP_SENSOR, msg, NULL, 0);
    return IRQ_HANDLED;
}

static int irq_setup(struct spidev_data *fp_dev) {
    int status;

    fp_dev->irq = fp_irq_num(fp_dev);
    status      = request_threaded_irq(
        fp_dev->irq, NULL, fp_irq_handler, IRQF_TRIGGER_RISING | IRQF_ONESHOT, "oplusfp", fp_dev);

    if (status) {
        pr_err("failed to request IRQ:%d\n", fp_dev->irq);
        return status;
    }
    enable_irq_wake(fp_dev->irq);
    fp_dev->irq_enabled = 1;

    return status;
}

static void irq_cleanup(struct spidev_data *fp_dev) {
    fp_dev->irq_enabled = 0;
    disable_irq(fp_dev->irq);
    disable_irq_wake(fp_dev->irq);
    free_irq(fp_dev->irq, fp_dev);  // need modify
}


int opticalfp_irq_handler(struct fp_underscreen_info *tp_info) {
    char msg     = 0;
    g_fp_touchinfo = *tp_info;
    if (tp_info->touch_state == g_last_tp_state) {
        return IRQ_HANDLED;
    }
    //add for debug
    pr_info("[%s] opticalfp_irq_handler tp_info->touch_state =%d, tp_info->x =%d, tp_info->y =%d, \n", \
        __func__, tp_info->touch_state, tp_info->x, tp_info->y);
    if (1 == tp_info->touch_state) {
        send_fingerprint_msg(E_FP_TP, tp_info->touch_state, tp_info, sizeof(struct fp_underscreen_info));
        g_last_tp_state = tp_info->touch_state;
    } else {
        send_fingerprint_msg(E_FP_TP, tp_info->touch_state, tp_info, sizeof(struct fp_underscreen_info));
        g_last_tp_state = tp_info->touch_state;
    }

    return IRQ_HANDLED;
}
EXPORT_SYMBOL(opticalfp_irq_handler);



static long fingerprint_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int err = 0;
    int retval = 0;
    struct spidev_data  *spidev;
    struct spi_device   *spi;
    u32 tmp;
    func_enter();
    if (_IOC_TYPE(cmd) != FP_IOC_MAGIC) {
        return -ENOTTY;
    }

    if (_IOC_DIR(cmd) & _IOC_READ) {
        err = !access_ok(VERIFY_WRITE, (void __user *)arg, _IOC_SIZE(cmd));
    }

    if (err == 0 && _IOC_DIR(cmd) & _IOC_WRITE) {
        err = !access_ok(VERIFY_READ, (void __user *)arg, _IOC_SIZE(cmd));
    }

    if (err) {
        return -EFAULT;
    }

    spidev = filp->private_data;
    spin_lock_irq(&spidev->spi_lock);
    spi = spi_dev_get(spidev->spi);
    spin_unlock_irq(&spidev->spi_lock);
    if (spi == NULL) {
        return -ESHUTDOWN;
    }
    mutex_lock(&spidev->buf_lock);
    
    switch (cmd) {
    case FP_SPI_IOC_RD_MODE:
        retval = __put_user(spi->mode & SPI_MODE_MASK, (__u8 __user *)arg);
        break;
    case FP_SPI_IOC_RD_LSB_FIRST:
        retval = __put_user((spi->mode & SPI_LSB_FIRST) ?  1 : 0, (__u8 __user *)arg);
        break;
    case FP_SPI_IOC_RD_BITS_PER_WORD:
        retval = __put_user(spi->bits_per_word, (__u8 __user *)arg);
        break;
    case FP_SPI_IOC_RD_MAX_SPEED_HZ:
        retval = __put_user(spi->max_speed_hz, (__u32 __user *)arg);
        break;
    case FP_SPI_IOC_WR_MODE:
        retval = __get_user(tmp, (u8 __user *)arg);
        if (retval == 0) {
            u8  save = spi->mode;

            if (tmp & ~SPI_MODE_MASK) {
                retval = -EINVAL;
                break;
            }

            tmp |= spi->mode & ~SPI_MODE_MASK;
            spi->mode = (u8)tmp;
            retval = spi_setup(spi);
            if (retval < 0) {
                spi->mode = save;
            } else {
                FP_LOGI("spi mode %02x\n", tmp);
            }
        }
        break;
    case FP_SPI_IOC_WR_LSB_FIRST:
        retval = __get_user(tmp, (__u8 __user *)arg);
        if (retval == 0) {
            u8  save = spi->mode;
            if (tmp) {
                spi->mode |= SPI_LSB_FIRST;
            } else {
                spi->mode &= ~SPI_LSB_FIRST;
            }
            retval = spi_setup(spi);
            if (retval < 0) {
                spi->mode = save;
            } else {
                FP_LOGI("%csb first\n", tmp ? 'l' : 'm');
            }
        }
        break;
    case FP_SPI_IOC_WR_BITS_PER_WORD:
        retval = __get_user(tmp, (__u8 __user *)arg);
        if (retval == 0) {
            u8  save = spi->bits_per_word;

            spi->bits_per_word = tmp;
            retval = spi_setup(spi);
            if (retval < 0) {
                spi->bits_per_word = save;
            } else {
                FP_LOGI("%d bits per word\n", tmp);
            }
        }
        break;
    case FP_SPI_IOC_WR_MAX_SPEED_HZ:
        retval = __get_user(tmp, (__u32 __user *)arg);
        if (retval == 0) {
            u32 save = spi->max_speed_hz;

            spi->max_speed_hz = tmp;
            retval = spi_setup(spi);
            if (retval < 0) {
                spi->max_speed_hz = save;
            } else {
                FP_LOGI("%d Hz (max)\n", tmp);
            }
        }
        break;
    case FP_IOC_ENABLE_POWER:
        fp_power_on(spidev);
        break;
    case FP_IOC_DISABLE_POWER:
        fp_power_off(spidev);
        break;
    case SET_DEFALUT_CONFIG:
        fingerprint_setup_conf(spidev, 8);
        break;
    case FP_IOC_RESET:
        pr_info("%s FP_IOC_RESET\n", __func__);
        fp_hw_reset(spidev, 60);
        break;
    case FP_IOC_DISABLE_IRQ:
        pr_info("%s FP_IOC_DISABLE_IRQ\n", __func__);
        fp_disable_irq(spidev);
        break;
    case FP_IOC_ENABLE_IRQ:
        pr_info("%s FP_IOC_ENABLE_IRQ\n", __func__);
        fp_enable_irq(spidev);
        break;
    case FP_IOC_RD_IRQ_VALUE:
        pr_info("%s FP_IOC_RD_IRQ_VALUE\n", __func__);
        int irq_value = fp_read_irq_value(spidev);
        retval = __put_user(irq_value, (int32_t __user *)arg);
        break;
    default:
        FP_LOGI("unkonw type\n");
        break;
    }
    func_exit();
    mutex_unlock(&spidev->buf_lock);
    spi_dev_put(spi);
    return retval;
}

static int fingerprint_open(struct inode *inode, struct file *filp)
{
    struct spidev_data  *spidev;
    int status = -ENXIO;
    func_enter();
    mutex_lock(&device_list_lock);
    list_for_each_entry(spidev, &device_list, device_entry) {
        if (spidev->devt == inode->i_rdev) {
            status = 0;
            break;
        }
    }
    if (status == 0) {
        spidev->users++;
        filp->private_data = spidev;
        nonseekable_open(inode, filp);
    } else {
        FP_LOGD("No device for minor %d", iminor(inode));
    }
    mutex_unlock(&device_list_lock);
    func_exit();
    return status;
}

static int ree_spi_release(struct inode *inode, struct file *filp)
{
    struct spidev_data  *spidev;
    int status = 0;
    func_enter();
    mutex_lock(&device_list_lock);
    spidev = filp->private_data;
    filp->private_data = NULL;
    spidev->users--;
    if (!spidev->users) {
        int     dofree;
        if (spidev->buffer) {
            kfree(spidev->buffer);
        }
        spidev->buffer = NULL;
        spin_lock_irq(&spidev->spi_lock);
        dofree = (spidev->spi == NULL);
        spin_unlock_irq(&spidev->spi_lock);
        if (dofree) {
            kfree(spidev);
        }
    }
    mutex_unlock(&device_list_lock);
    func_exit();
    return status;
}

static const struct file_operations fp_operation = {
    .owner =    THIS_MODULE,
    .write =    fingerprint_write,
    .read =     fingerprint_read,
    .unlocked_ioctl = fingerprint_ioctl,
    .open =     fingerprint_open,
    .release =  ree_spi_release,
    .llseek =   no_llseek,
};

static struct class *fingerprint_class;

static int fingerprint_driver_probe(struct spi_device *spi)
{
    struct spidev_data  *spidev;
    int status;
    unsigned long  minor;

    FP_LOGI("%s enter", __func__);
    spidev = kzalloc(sizeof(*spidev), GFP_KERNEL);
    if (!spidev) {
        return -ENOMEM;
    }
    spidev->spi = spi;
    status = fp_parse_dts(spidev);
    if (status) {
        goto fp_out;
    }
    spin_lock_init(&spidev->spi_lock);
    mutex_init(&spidev->buf_lock);
    INIT_LIST_HEAD(&spidev->device_entry);
    mutex_lock(&device_list_lock);
    minor = find_first_zero_bit(minors, N_SPI_MINORS);
    if (minor < N_SPI_MINORS) {
        struct device *dev;
        spidev->devt = MKDEV(SPIDEV_MAJOR, minor);
        dev = device_create(fingerprint_class, &spi->dev, spidev->devt, spidev, FP_DEV_NAME);
        status = IS_ERR(dev) ? PTR_ERR(dev) : 0;
    } else {
        // dev_dbg(&spi->dev, "no minor number available!\n");
        status = -ENODEV;
    }
    if (status == 0) {
        set_bit(minor, minors);
        list_add(&spidev->device_entry, &device_list);
    }
    mutex_unlock(&device_list_lock);

    if (status == 0) {
        spi_set_drvdata(spi, spidev);
    } else {
        kfree(spidev);
    }

    wake_lock_init(&fp_wakelock, WAKE_LOCK_SUSPEND, "fp_wakelock");
    status = irq_setup(spidev);
fp_out:
    FP_LOGI("%s init status:%d", __func__, status);
    return status;
}

static int spidev_remove(struct spi_device *spi)
{
    struct spidev_data  *spidev = spi_get_drvdata(spi);
    spin_lock_irq(&spidev->spi_lock);
    spidev->spi = NULL;
    spi_set_drvdata(spi, NULL);
    spin_unlock_irq(&spidev->spi_lock);
    mutex_lock(&device_list_lock);
    list_del(&spidev->device_entry);
    device_destroy(fingerprint_class, spidev->devt);
    clear_bit(MINOR(spidev->devt), minors);
    if (spidev->users == 0) {
        kfree(spidev);
    }
    mutex_unlock(&device_list_lock);
    wake_lock_destroy(&fp_wakelock);
    return 0;
}

static struct of_device_id match_table[] = {
    { .compatible = "oplus,oplus_fp"},
    { .compatible = "goodix,goodix_fp"},
    {},
};

static struct spi_driver spidev_spi_driver = {
    .driver = {
        .name =     FP_DEV_NAME,
        .owner =    THIS_MODULE,
        .of_match_table  = match_table,
    },
    .probe =    fingerprint_driver_probe,
    .remove =   spidev_remove,
};

static int fingerprint_driver_init(void)
{
    int status;
    FP_LOGI("%s, enter\n", __func__);
    BUILD_BUG_ON(N_SPI_MINORS > 256);

    status = register_chrdev(SPIDEV_MAJOR, "spi", &fp_operation);
    if (status < 0) {
        return status;
    }
    fingerprint_class = class_create(THIS_MODULE, "fpspidev");
    if (IS_ERR(fingerprint_class)) {
        unregister_chrdev(SPIDEV_MAJOR, spidev_spi_driver.driver.name);
        return PTR_ERR(fingerprint_class);
    }
    status = spi_register_driver(&spidev_spi_driver);
    if (status < 0) {
        class_destroy(fingerprint_class);
        unregister_chrdev(SPIDEV_MAJOR, spidev_spi_driver.driver.name);
    }
    FP_LOGI("%s, exit\n", __func__);
    return status;
}
module_init(fingerprint_driver_init);

static void fingerprint_driver_exit(void) {
    FP_LOGI("%s, exit\n", __func__);
    spi_unregister_driver(&spidev_spi_driver);
    class_destroy(fingerprint_class);
    unregister_chrdev(SPIDEV_MAJOR, spidev_spi_driver.driver.name);
}
module_exit(fingerprint_driver_exit);

MODULE_AUTHOR("Security group");
MODULE_DESCRIPTION("Fingerprint spi");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:spidev");
