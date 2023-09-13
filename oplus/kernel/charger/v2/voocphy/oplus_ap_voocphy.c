#include <linux/gpio.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/module.h>
#include <linux/power_supply.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/err.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/debugfs.h>
#include <linux/bitops.h>
#include <linux/math64.h>
#include <linux/proc_fs.h>
#include <linux/pm_qos.h>
#include <linux/cpufreq.h>

#include <trace/events/sched.h>
#include <linux/ktime.h>
#include "oplus_voocphy.h"
#include "chglib/oplus_chglib.h"
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#include <linux/sched/topology.h>
#endif

enum {
	FASTCHG_TEMP_RANGE_INIT = 0,
	FASTCHG_TEMP_RANGE_LITTLE_COLD,/*0 ~ 5*/
	FASTCHG_TEMP_RANGE_COOL, /*5 ~ 12*/
	FASTCHG_TEMP_RANGE_LITTLE_COOL, /*12~16*/
	FASTCHG_TEMP_RANGE_NORMAL_LOW, /*16~25*/
	FASTCHG_TEMP_RANGE_NORMAL_HIGH, /*25~43*/
	FASTCHG_TEMP_RANGE_WARM, /*43-52*/
	FASTCHG_TEMP_RANGE_NORMAL,
};

enum {
	BAT_TEMP_NATURAL = 0,
	BAT_TEMP_HIGH0,
	BAT_TEMP_HIGH1,
	BAT_TEMP_HIGH2,
	BAT_TEMP_HIGH3,
	BAT_TEMP_HIGH4,
	BAT_TEMP_HIGH5,
	BAT_TEMP_LOW0,
	BAT_TEMP_LOW1,
	BAT_TEMP_LOW2,
	BAT_TEMP_LITTLE_COOL,
	BAT_TEMP_LITTLE_COOL_LOW,
	BAT_TEMP_COOL,
	BAT_TEMP_NORMAL_LOW,
	BAT_TEMP_NORMAL_HIGH,
	BAT_TEMP_LITTLE_COLD,
	BAT_TEMP_WARM,
	BAT_TEMP_EXIT,
};

#define AP_ALLOW_FASTCHG	(1 << 6)
#define TARGET_VOL_OFFSET_THR	250
#define TARGET_CURR_OFFSET_THR  500
#define DELAY_TEMP_MONITOR_COUNTS		2
#define trace_oplus_tp_sched_change_ux(x, y)
#define CHARGER_UP_CPU_FREQ	2000000	//1.8 x 1000 x 1000
#define SEND_FASTCHG_ONGOING_NOTIFY_INTERVAL 2000 /* ms */

/* dts default configuration */
#define VOOCPHY_SVOOC_CP_MAX_IBUS_DEFAULT	3200
#define VOOCPHY_VOOC_CP_MAX_IBUS_DEFAULT	3500
#define BATT_PWD_CURR_THD1_DEFAULT		3300
#define BATT_PWD_VOL_THD1_DEFAULT		4454
#define VOOC_WARM_TEMP_RANGE_THD    20
#define CPU_CHG_FREQ_STAT_UP	1
#define CPU_CHG_FREQ_STAT_AUTO	0
#define MAX_IGNORE 6
#define FIRST_FRAME 0xA8
#define DEFUALT_VBUS_LOW 100
#define DEFUALT_VBUS_HIGH 200

int voocphy_log_level = 3;
#define voocphy_info(fmt, ...)	\
do {						\
	if (voocphy_log_level >= 3)	\
		printk(KERN_INFO "%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

#define voocphy_dbg(fmt, ...)	\
do {					\
	if (voocphy_log_level >= 2)	\
		printk(KERN_DEBUG "%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

#define voocphy_err(fmt, ...)   \
do {                                    \
	if (voocphy_log_level >= 1)     \
		printk(KERN_ERR "%s:" fmt, __func__, ##__VA_ARGS__);\
} while(0);

static struct oplus_voocphy_manager *g_voocphy_chip;
ktime_t calltime, rettime;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static struct pm_qos_request pm_qos_req;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
static struct freq_qos_request freq_qos_req;
#endif
#else
static struct pm_qos_request pm_qos_req;
#endif
static int disable_sub_cp_count = 0;
static int slave_trouble_count = 0;
int base_cpufreq_for_chg = CHARGER_UP_CPU_FREQ;
unsigned char svooc_batt_sys_curve[BATT_SYS_ARRAY][7] = {0};
unsigned char vooc_batt_sys_curve[BATT_SYS_ARRAY][7] = {0};
struct parallel_curve {
	int svooc_current;
	int parallel_switch_current;
};

typedef union {
	struct parallel_curve curve_data;
	u32 curve_val[2];
} svooc_parallel_curve_t;

static svooc_parallel_curve_t svooc_parallel_curve[7] = {
/* svooc_current/100   parallel_switch_current */
	[0].curve_val = { 25, 2800},
	[1].curve_val = { 20, 2300},
	[2].curve_val = { 15, 1900},
	[3].curve_val = { 10, 1400},
	[5].curve_val = { 0, 0},
	[5].curve_val = { 0, 0},
	[6].curve_val = { 0, 0},
};

/* svooc curv */
/* 0~50 */
struct batt_sys_curves svooc_curves_soc0_2_50[BATT_SYS_MAX] = {0};
/* 50~75 */
struct batt_sys_curves svooc_curves_soc50_2_75[BATT_SYS_MAX] = {0};
/* 75~85 */
struct batt_sys_curves svooc_curves_soc75_2_85[BATT_SYS_MAX] = {0};
/* 85~90 */
struct batt_sys_curves svooc_curves_soc85_2_90[BATT_SYS_MAX] = {0};

/* vooc curv */
/* 0~50 */
struct batt_sys_curves vooc_curves_soc0_2_50[BATT_SYS_MAX] = {0};
/* 50~75 */
struct batt_sys_curves vooc_curves_soc50_2_75[BATT_SYS_MAX] = {0};
/* 75~85 */
struct batt_sys_curves vooc_curves_soc75_2_85[BATT_SYS_MAX] = {0};
/* 85~90 */
struct batt_sys_curves vooc_curves_soc85_2_90[BATT_SYS_MAX] = {0};

extern int ppm_sys_boost_min_cpu_freq_set(int freq_min, int freq_mid, int freq_max, unsigned int clear_time);
extern int ppm_sys_boost_min_cpu_freq_clear(void);
static void oplus_voocphy_set_status_and_notify_ap(struct oplus_voocphy_manager *chip, int fastchg_notify_status);
static int oplus_voocphy_commu_process_handle(struct device *dev, unsigned long enable);
static void oplus_voocphy_reset_fastchg_after_usbout(struct oplus_voocphy_manager *chip);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
#define MAX_SUPPORT_CLUSTER 8
#define MAX_BOOST_TIME	    15000
#define LITTER_CLUSTER	    0
#define MIN_CLUSTER	    1
#define BIG_CLUSTER	    2

static bool is_clear_timer_clear = true;
unsigned long last_jiffies = 0;
struct freq_qos_request charger_boost_req[MAX_SUPPORT_CLUSTER];
int max_cluster_num;
bool initialized = false;
static DEFINE_MUTEX(charger_boost);

bool get_freq_limit_info(void)
{
	return is_clear_timer_clear;
}

static bool oplus_voocphy_pm_qos_init(void)
{
	int i, ret, cur_cluster;
	int last_cluster = -1;
	struct cpufreq_policy *policy;
	struct freq_qos_request *req;

	for_each_possible_cpu(i) {
		cur_cluster = topology_physical_package_id(i);
		if (cur_cluster == last_cluster)
			continue;

		if (cur_cluster > last_cluster + 1)
			return false;

		if (cur_cluster >= MAX_SUPPORT_CLUSTER)
			break;

		req = &charger_boost_req[cur_cluster];
		policy = cpufreq_cpu_get(i);
		if (!policy) {
			continue;
		}

		ret = freq_qos_add_request(&policy->constraints, req, FREQ_QOS_MIN, policy->cpuinfo.min_freq);
		if (ret < 0) {
			continue;
		}

		last_cluster = cur_cluster;
	}
	max_cluster_num = cur_cluster + 1;

	return true;
}

static int charger_boost_min_cpu_freq_set(struct oplus_voocphy_manager *chip,
					  int *freq, unsigned int clear_time)
{
	int i;

	if (!chip) {
		voocphy_err("voocphy_chip is null !\n");
		return -ENODEV;
	}

	if (!initialized)
		initialized = oplus_voocphy_pm_qos_init();

	if (!initialized)
		return -EINVAL;

	for (i = 0; i < max_cluster_num; i++) {
		freq_qos_update_request(&charger_boost_req[i], freq[i]);
	}

	clear_time = clear_time > MAX_BOOST_TIME ? MAX_BOOST_TIME : clear_time;
	cancel_delayed_work(&chip->clear_boost_work);

	mutex_lock(&charger_boost);
	is_clear_timer_clear = false;
	last_jiffies = jiffies + msecs_to_jiffies(clear_time);
	schedule_delayed_work(&chip->clear_boost_work, msecs_to_jiffies(clear_time));
	mutex_unlock(&charger_boost);

	return 0;
}

static int charger_boost_min_cpu_freq_clear(struct oplus_voocphy_manager *chip)
{
	int i;

	if (!chip) {
		voocphy_err("voocphy_chip is null !\n");
		return -ENODEV;
	}

	if (!initialized)
		return -EINVAL;

	for (i = 0; i < max_cluster_num; i++) {
		freq_qos_update_request(&charger_boost_req[i], 0);
	}

	mutex_lock(&charger_boost);

	is_clear_timer_clear = true;
	if (jiffies < last_jiffies)
		cancel_delayed_work(&chip->clear_boost_work);

	mutex_unlock(&charger_boost);

	return 0;
}

static void oplus_voocphy_clear_boost_work(struct work_struct *work)
{
	struct oplus_voocphy_manager *chip = container_of(work,
			struct oplus_voocphy_manager, clear_boost_work.work);

	if (!is_clear_timer_clear) {
		charger_boost_min_cpu_freq_clear(chip);
	}
}
#endif

static void oplus_voocphy_wake_modify_cpufeq_work(struct oplus_voocphy_manager *chip, int flag)
{
	voocphy_info("%s %s\n", __func__, flag == CPU_CHG_FREQ_STAT_UP ?"request":"release");
	schedule_delayed_work(&chip->modify_cpufeq_work, 0);
}

static void oplus_voocphy_modify_cpufeq_work(struct work_struct *work)
{
	struct oplus_voocphy_manager *chip = container_of(work,
		struct oplus_voocphy_manager, modify_cpufeq_work.work);

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	if (atomic_read(&chip->voocphy_freq_state) == 1) {
		ppm_sys_boost_min_cpu_freq_set(chip->voocphy_freq_mincore,
					       chip->voocphy_freq_midcore,
					       chip->voocphy_freq_maxcore,
					       chip->voocphy_current_change_timeout);
	} else {
		ppm_sys_boost_min_cpu_freq_clear();
	}
#else
	int freq[3] = {0};

	freq[LITTER_CLUSTER] = chip->voocphy_freq_mincore;
	freq[MIN_CLUSTER] = chip->voocphy_freq_midcore;
	freq[BIG_CLUSTER] = chip->voocphy_freq_maxcore;
	if (atomic_read(&chip->voocphy_freq_state) == 1) {
		charger_boost_min_cpu_freq_set(chip, freq,
					chip->voocphy_current_change_timeout);
	} else {
		charger_boost_min_cpu_freq_clear(chip);
	}
#endif
}

static void oplus_voocphy_pm_qos_update(int new_value)
{
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	static int last_value = 0;

	if (!pm_qos_request_active(&pm_qos_req)) {
		pm_qos_add_request(&pm_qos_req, PM_QOS_CPU_DMA_LATENCY, new_value);
	} else {
		pm_qos_update_request(&pm_qos_req, new_value);
	}

	if (last_value != new_value) {
		last_value = new_value;

		if (new_value ==  PM_QOS_DEFAULT_VALUE) {
			voocphy_info("oplus_voocphy_pm_qos_update PM_QOS_DEFAULT_VALUE \n");
		} else {
			voocphy_info("oplus_voocphy_pm_qos_update value =%d \n", new_value);
		}
	}
#else
	if (new_value == PM_QOS_DEFAULT_VALUE) {
		cpu_latency_qos_remove_request(&pm_qos_req);
	} else {
		if (!cpu_latency_qos_request_active(&pm_qos_req)) {
			cpu_latency_qos_add_request(&pm_qos_req, 0);
		} else {
			cpu_latency_qos_update_request(&pm_qos_req, 0);
		}
	}

	if (new_value ==  PM_QOS_DEFAULT_VALUE) {
		voocphy_info("oplus_voocphy_pm_qos_update PM_QOS_DEFAULT_VALUE \n");
	} else {
		voocphy_info("oplus_voocphy_pm_qos_update value = %d \n", new_value);
	}
#endif
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 4, 0))
void cpuboost_charge_event(int flag)
{
	struct cpufreq_policy *policy;
	int ret;
	static bool initialized = false;

	policy = cpufreq_cpu_get(0); //CPU0
	if (!policy) {
		voocphy_err("%s: cpufreq policy not found for cpu0\n", __func__);
		return;
	}

	if (!initialized) {
		ret = freq_qos_add_request(&policy->constraints, &freq_qos_req, FREQ_QOS_MIN, policy->cpuinfo.min_freq);
		if (ret < 0) {
			voocphy_err("%s:  freq_qos_add_request failed!!! ret=%d \n", __func__, ret);
			return;
		} else {
			initialized = true;
		}
	}

	if (flag == 1) {
		ret = freq_qos_update_request(&freq_qos_req, CHARGER_UP_CPU_FREQ);
	} else {
		ret = freq_qos_update_request(&freq_qos_req, policy->cpuinfo.min_freq);
	}
	voocphy_info("%s: set cpufreq boost value=%d ret=%d\n", __func__, flag, ret);
}
#else
void __attribute__((weak)) cpuboost_charge_event(int flag)
{
	return;
}
#endif
#else
void __attribute__((weak)) cpuboost_charge_event(int flag)
{
	return;
}
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static void voocphy_cpufreq_init(struct oplus_voocphy_manager *chip)
{
#ifdef CONFIG_OPLUS_CHARGER_MTK
	voocphy_info("%s \n", __func__);
	atomic_set(&chip->voocphy_freq_state, 1);
	ppm_sys_boost_min_cpu_freq_set(chip->voocphy_freq_mincore,
				       chip->voocphy_freq_midcore,
				       chip->voocphy_freq_maxcore,
				       15000);
#else
	atomic_set(&chip->voocphy_freq_state, 2);
	cpuboost_charge_event(CPU_CHG_FREQ_STAT_UP);
#endif
}

static void voocphy_cpufreq_release(struct oplus_voocphy_manager *chip)
{
	atomic_set(&chip->voocphy_freq_state, 0);
#ifdef CONFIG_OPLUS_CHARGER_MTK
	voocphy_info("%s RESET_DELAY_30S\n", __func__);
	ppm_sys_boost_min_cpu_freq_clear();
#else
	cpuboost_charge_event(CPU_CHG_FREQ_STAT_AUTO);
#endif
}

static void voocphy_cpufreq_update(struct oplus_voocphy_manager *chip, int flag)
{
	if (flag == CPU_CHG_FREQ_STAT_UP) {
#ifdef CONFIG_OPLUS_CHARGER_MTK
		voocphy_info("%s CPU_CHG_FREQ_STAT_UP\n", __func__);
		atomic_set(&chip->voocphy_freq_state, 1);
		oplus_voocphy_wake_modify_cpufeq_work(chip, CPU_CHG_FREQ_STAT_UP);
#else
		atomic_set(&chip->voocphy_freq_state, 2);
		cpuboost_charge_event(CPU_CHG_FREQ_STAT_UP);
#endif
	} else {
		if (atomic_read(&chip->voocphy_freq_state) == 0)
			return;
		atomic_set(&chip->voocphy_freq_state, 0);
#ifdef CONFIG_OPLUS_CHARGER_MTK
		voocphy_info("%s CPU_CHG_FREQ_STAT_AUTO\n", __func__);
		oplus_voocphy_wake_modify_cpufeq_work(chip, CPU_CHG_FREQ_STAT_AUTO);
#else
		cpuboost_charge_event(CPU_CHG_FREQ_STAT_AUTO);
#endif
	}
}
#else
static void voocphy_cpufreq_init(struct oplus_voocphy_manager *chip)
{
	int freq[3] = {0};

	voocphy_info("%s \n", __func__);
	atomic_set(&chip->voocphy_freq_state, 1);
	freq[LITTER_CLUSTER] = chip->voocphy_freq_mincore;
	freq[MIN_CLUSTER] = chip->voocphy_freq_midcore;
	freq[BIG_CLUSTER] = chip->voocphy_freq_maxcore;
	charger_boost_min_cpu_freq_set(chip, freq, MAX_BOOST_TIME);
}

static void voocphy_cpufreq_release(struct oplus_voocphy_manager *chip)
{
	voocphy_info("%s RESET_DELAY_30S\n", __func__);
	atomic_set(&chip->voocphy_freq_state, 0);
	ppm_sys_boost_min_cpu_freq_clear();
}

static void voocphy_cpufreq_update(struct oplus_voocphy_manager *chip, int flag)
{
	if (flag == CPU_CHG_FREQ_STAT_UP) {
		voocphy_info("%s CPU_CHG_FREQ_STAT_UP\n", __func__);
		atomic_set(&chip->voocphy_freq_state, 1);
		oplus_voocphy_wake_modify_cpufeq_work(chip, CPU_CHG_FREQ_STAT_UP);
	} else {
		if (atomic_read(&chip->voocphy_freq_state) == 0)
			return;
		voocphy_info("%s CPU_CHG_FREQ_STAT_AUTO\n", __func__);
		atomic_set(&chip->voocphy_freq_state, 0);
		oplus_voocphy_wake_modify_cpufeq_work(chip, CPU_CHG_FREQ_STAT_AUTO);
	}
}
#endif

static int oplus_voocphy_batt_fake_temp_show(struct seq_file *seq_filp, void *v)
{
	seq_printf(seq_filp, "%d\n", g_voocphy_chip->batt_fake_temp);

	return 0;
}

static int oplus_voocphy_batt_fake_temp_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = single_open(file, oplus_voocphy_batt_fake_temp_show, NULL);

	return ret;
}

static ssize_t oplus_voocphy_batt_fake_temp_write(struct file *filp,
        const char __user *buff, size_t len, loff_t *data)
{
	int batt_fake_temp;
	char temp[16];

	if (len > sizeof(temp)) {
		return -EINVAL;
	}
	if (copy_from_user(temp, buff, len)) {
		voocphy_info("%s error.\n", __func__);
		return -EFAULT;
	}
	sscanf(temp, "%d", &batt_fake_temp);
	g_voocphy_chip->batt_fake_temp = batt_fake_temp;
	voocphy_info("batt_fake_temp is %d\n", batt_fake_temp);

	return len;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations oplus_voocphy_batt_fake_temp_fops = {
	.open = oplus_voocphy_batt_fake_temp_open,
	.write = oplus_voocphy_batt_fake_temp_write,
	.read = seq_read,
};
#else
static const struct proc_ops oplus_voocphy_batt_fake_temp_fops = {
	.proc_open = oplus_voocphy_batt_fake_temp_open,
	.proc_write = oplus_voocphy_batt_fake_temp_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
};
#endif

static int oplus_voocphy_batt_fake_soc_show(struct seq_file *seq_filp, void *v)
{
	seq_printf(seq_filp, "%d\n", g_voocphy_chip->batt_fake_soc);

	return 0;
}

static int oplus_voocphy_batt_fake_soc_open(struct inode *inode, struct file *file)
{
	int ret;

	ret = single_open(file, oplus_voocphy_batt_fake_soc_show, NULL);

	return ret;
}

static ssize_t oplus_voocphy_batt_fake_soc_write(struct file *filp,
        const char __user *buff, size_t len, loff_t *data)
{
	int batt_fake_soc;
	char temp[16];

	if (len > sizeof(temp)) {
		return -EINVAL;
	}
	if (copy_from_user(temp, buff, len)) {
		voocphy_info("voocphy_batt_fake_soc_write error.\n");
		return -EFAULT;
	}
	sscanf(temp, "%d", &batt_fake_soc);
	g_voocphy_chip->batt_fake_soc = batt_fake_soc;
	voocphy_info("batt_fake_soc is %d\n", batt_fake_soc);

	return len;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations oplus_voocphy_batt_fake_soc_fops = {
	.open = oplus_voocphy_batt_fake_soc_open,
	.write = oplus_voocphy_batt_fake_soc_write,
	.read = seq_read,
};
#else
static const struct proc_ops oplus_voocphy_batt_fake_soc_fops = {
	.proc_open = oplus_voocphy_batt_fake_soc_open,
	.proc_write = oplus_voocphy_batt_fake_soc_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
};
#endif

static int oplus_voocphy_loglevel_show(struct seq_file *seq_filp, void *v)
{
	seq_printf(seq_filp, "%d\n", voocphy_log_level);
	return 0;
}
static int oplus_voocphy_loglevel_open(struct inode *inode, struct file *file)
{
	int ret;
	ret = single_open(file, oplus_voocphy_loglevel_show, NULL);
	return ret;
}

static ssize_t oplus_voocphy_loglevel_write(struct file *filp,
        const char __user *buff, size_t len, loff_t *data)
{
	char temp[16];

	if (len > sizeof(temp)) {
		return -EINVAL;
	}
	if (copy_from_user(temp, buff, len)) {
		voocphy_info("voocphy_loglevel_write error.\n");
		return -EFAULT;
	}
	sscanf(temp, "%d", &voocphy_log_level);
	voocphy_info("voocphy_log_level is %d\n", voocphy_log_level);

	return len;
}

#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
static const struct file_operations oplus_voocphy_loglevel_fops = {
	.open = oplus_voocphy_loglevel_open,
	.write = oplus_voocphy_loglevel_write,
	.read = seq_read,
};
#else
static const struct proc_ops oplus_voocphy_loglevel_fops = {
	.proc_open = oplus_voocphy_loglevel_open,
	.proc_write = oplus_voocphy_loglevel_write,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
};
#endif

void init_proc_voocphy_debug(struct oplus_voocphy_manager *chip)
{
	struct proc_dir_entry *p_batt_fake_temp = NULL;
	struct proc_dir_entry *p_batt_fake_soc = NULL;
	struct proc_dir_entry *p_batt_loglevel = NULL;

	p_batt_fake_temp = proc_create("voocphy_batt_fake_temp", 0664, NULL,
	                               &oplus_voocphy_batt_fake_temp_fops);
	if (!p_batt_fake_temp) {
		voocphy_info("proc_create voocphy_batt_fake_temp_fops fail!\n");
	}
	chip->batt_fake_temp = 0;

	p_batt_fake_soc = proc_create("voocphy_batt_fake_soc", 0664, NULL,
	                              &oplus_voocphy_batt_fake_soc_fops);
	if (!p_batt_fake_soc) {
		voocphy_info("proc_create voocphy_batt_fake_soc_fops fail!\n");
	}
	chip->batt_fake_soc = 0;


	p_batt_loglevel = proc_create("voocphy_loglevel", 0664, NULL,
	                              &oplus_voocphy_loglevel_fops);
	if (!p_batt_loglevel) {
		voocphy_info("proc_create voocphy_loglevel_fops fail!\n");
	}

}

static void oplus_voocphy_reset_temp_range(struct oplus_voocphy_manager *chip)
{
	if (chip != NULL) {
		chip->vooc_normal_high_temp = chip->vooc_normal_high_temp_default;
		chip->vooc_little_cold_temp = chip->vooc_little_cold_temp_default;
		chip->vooc_cool_temp = chip->vooc_cool_temp_default;
		chip->vooc_little_cool_temp = chip->vooc_little_cool_temp_default;
		chip->vooc_normal_low_temp = chip->vooc_normal_low_temp_default;
	}
}

static bool oplus_voocphy_get_bidirect_cp_support(struct oplus_voocphy_manager *chip)
{
	if (!chip)
		return false;

	return chip->voocphy_bidirect_cp_support;
}

/*switching start*/
int oplus_switching_support_parallel_chg(void)
{
	///if (g_switching_chip == NULL) {
	///	return 0;
	///} else {
	///	return 1;
	///}
	return 0;
}
/*switching end*/

static int oplus_voocphy_monitor_timer_start(struct oplus_voocphy_manager *chip,
					     int timer_id, int duration_in_ms)
{
	if( (timer_id >= VOOC_THREAD_TIMER_INVALID) || (duration_in_ms < 0))
		return VOOCPHY_EFATAL;

	if (!chip) {
		voocphy_err( "oplus_voocphy_monitor_timer_start chip null\n");
		return VOOCPHY_EFATAL;
	}

	chip->mornitor_evt[timer_id].cnt = 0;
	chip->mornitor_evt[timer_id].timeout = false;
	chip->mornitor_evt[timer_id].status = VOOC_MONITOR_START;

	voocphy_dbg("timerid %d\n", timer_id);

	return VOOCPHY_SUCCESS;
}

static int oplus_voocphy_monitor_timer_stop(struct oplus_voocphy_manager *chip,
					    int timer_id, int duration_in_ms)
{
	if( (timer_id >= VOOC_THREAD_TIMER_INVALID) || (duration_in_ms < 0))
		return VOOCPHY_EFATAL;

	if (!chip) {
		voocphy_err( "oplus_voocphy_monitor_timer_stop chip is null");
		return VOOCPHY_EFATAL;
	}

	chip->mornitor_evt[timer_id].cnt = 0;
	chip->mornitor_evt[timer_id].timeout = false;
	chip->mornitor_evt[timer_id].status = VOOC_MONITOR_STOP;
	voocphy_dbg("timerid %d\n", timer_id);

	return VOOCPHY_SUCCESS;
}

static void oplus_voocphy_basetimer_monitor_start(struct oplus_voocphy_manager *chip)
{
	if (chip && !hrtimer_active(&chip->monitor_btimer)) {
		chip->moniotr_kt = ktime_set(0,5000000);// 0s	5000000ns	= 5ms
		hrtimer_start(&chip->monitor_btimer, chip->moniotr_kt, HRTIMER_MODE_REL);
		voocphy_info("finihed\n");
	}
}

static void oplus_voocphy_basetimer_monitor_stop(struct oplus_voocphy_manager *chip)
{
	if (chip && hrtimer_active(&chip->monitor_btimer)) {
		hrtimer_cancel(&chip->monitor_btimer);
		voocphy_info("finihed");
	}
	oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_COMMU, VOOC_COMMU_EVENT_TIME);
	chip->fastchg_commu_ing = false;
}

static int oplus_voocphy_read_mesg_mask(unsigned char mask,
					unsigned char data,
					unsigned char *read_data)
{
	int status = VOOCPHY_SUCCESS;

	*read_data = (data & mask) >> VOOC_SHIFT_FROM_MASK(mask);

	return status;
}

static int oplus_voocphy_write_mesg_mask(unsigned char mask,
					 unsigned char *pdata,
					 unsigned char write_data)
{
	int status = VOOCPHY_SUCCESS;

	write_data <<= VOOC_SHIFT_FROM_MASK(mask);
	*pdata = (*pdata & ~mask) | (write_data & mask);

	return status;
}

#define BTB_CHECK_MAX_CNT	    3
#define BTB_CHECK_TIME_US	    10000
#define BTB_OVER_TEMP		    80
static bool oplus_voocphy_check_fastchg_real_allow(struct oplus_voocphy_manager *chip)
{
	int batt_soc = 0;
	int batt_temp = 0;
	int btb_temp;
	int usb_temp;
	int btb_check_cnt = BTB_CHECK_MAX_CNT;
	bool ret = false;

	/* get soc */
	batt_soc = oplus_chglib_get_soc(chip->dev);
	if (chip->batt_fake_soc) {
		batt_soc = chip->batt_fake_soc;
	}

	/* get shell temp */
	batt_temp = oplus_chglib_get_shell_temp(chip->dev);

	if (chip->batt_fake_temp) {
		batt_temp = chip->batt_fake_temp;
	}
	if (chip->vooc_normal_high_temp != -EINVAL
		&& (batt_temp > chip->vooc_normal_high_temp)
		&& (!(batt_soc < chip->vooc_warm_allow_soc
		&& (oplus_chglib_gauge_vbatt(chip->dev) < chip->vooc_warm_allow_vol)))) {
		voocphy_info(" temp warm no allow enter fastchg\n");
		return false;
	}

	if (chip->fastchg_to_warm_full == true
		&& batt_temp > chip->vooc_normal_high_temp - VOOC_WARM_TEMP_RANGE_THD) {
		voocphy_info(" fastchg_to_warm_full is true\n");
		return false;
	}

	if (batt_soc >= chip->vooc_low_soc && batt_soc <= chip->vooc_high_soc
	    && batt_temp >= chip->vooc_low_temp && batt_temp <= chip->vooc_high_temp)
		ret = true;
	else
		ret = false;

	while (btb_check_cnt != 0) {
		btb_temp = oplus_chglib_get_battery_btb_temp_cal();
		usb_temp = oplus_chglib_get_usb_btb_temp_cal();
		voocphy_err("btb_temp: %d, usb_temp = %d", btb_temp, usb_temp);

		if (btb_temp < BTB_OVER_TEMP && usb_temp < BTB_OVER_TEMP) {
			break;
		}

		btb_check_cnt--;
		if (btb_check_cnt > 0) {
			usleep_range(BTB_CHECK_TIME_US, BTB_CHECK_TIME_US);
		}
	}

	if (btb_check_cnt == 0) {
		ret = false;
		chip->btb_temp_over = true;
	}

	voocphy_info("ret:%d, temp:%d, soc:%d", ret, batt_temp, batt_soc);

	return ret;
}

static int oplus_voocphy_slave_hw_setting(struct oplus_voocphy_manager *chip,
					  int reason)
{
	int rc = 0;
	if (!chip) {
		voocphy_info("oplus_voocphy_manager chip null\n");
		return -EINVAL;
	}

	if (chip->slave_ops && chip->slave_ops->hw_setting) {
		rc = chip->slave_ops->hw_setting(chip, reason);
	}

	return rc;
}

static bool oplus_voocphy_set_fastchg_state(struct oplus_voocphy_manager *chip,
					    unsigned char fastchg_state)
{
	if (chip) {
		voocphy_info("fastchg_state %d\n", fastchg_state);
		chip->fastchg_stage = fastchg_state;
	}

	return true;
}

static int oplus_voocphy_reset_variables(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;
	int batt_temp = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_reset_variables chip null!\n");
		return VOOCPHY_EFATAL;
	}

	chip->voocphy_rx_buff = 0;
	chip->voocphy_tx_buff[0] = 0;
	chip->voocphy_tx_buff[1] = 0;
	if (chip->fastchg_reactive == false) {
		chip->adapter_check_ok = false;
		chip->adapter_model_detect_count = 3;
	}
	chip->adapter_ask_check_count = 3;
	chip->adapter_is_vbus_ok_count = 0;
	chip->adapter_ask_fastchg_ornot_count = 0;
	chip->fastchg_adapter_ask_cmd = VOOC_CMD_UNKNOW;
	chip->vooc2_next_cmd = VOOC_CMD_ASK_FASTCHG_ORNOT;
	chip->adapter_check_cmmu_count = 0;
	chip->code_id_far = 0;
	chip->code_id_local = 0xFFFF;
	chip->code_id_temp_h = 0;
	chip->code_id_temp_l = 0;
	if (chip->fastchg_reactive == false) {
		chip->adapter_model_ver = 0;
	}
	chip->adapter_model_count = 0;
	chip->ask_batt_sys = 0;
	chip->current_expect = chip->current_default;
	chip->current_max = chip->current_default;
	chip->current_spec = chip->current_default;
	chip->current_ap = chip->current_default;
	chip->current_batt_temp = chip->current_default;
	chip->batt_temp_plugin = VOOCPHY_BATT_TEMP_NORMAL;
	if (chip->fastchg_reactive == false) {
		chip->adapter_rand_start = false;
		chip->adapter_type = ADAPTER_SVOOC;
	}
	chip->adapter_mesg = 0;
	chip->fastchg_allow = false;
	chip->ask_vol_again = false;
	chip->ap_need_change_current = 0;
	chip->adjust_curr = ADJ_CUR_STEP_DEFAULT;
	chip->adjust_fail_cnt = 0;
	chip->pre_adapter_ask_cmd = VOOC_CMD_UNKNOW;
	chip->ignore_first_frame = true;
	chip->vooc_vbus_status = VOOC_VBUS_UNKNOW;
	if (chip->fastchg_reactive == false) {
		chip->vooc_head = VOOC_DEFAULT_HEAD;
	}
	chip->ask_vooc3_detect = false;
	chip->force_2a = false;
	chip->force_3a = false;
	chip->force_2a_flag = false;
	chip->force_3a_flag = false;

	chip->btb_err_first = false;
	chip->fastchg_timeout_time = chip->fastchg_timeout_time_init;
	chip->fastchg_3c_timeout_time = FASTCHG_3000MA_TIMEOUT;
	chip->gauge_vbatt = 0;
	chip->vbus = 0;
	chip->fastchg_batt_temp_status = BAT_TEMP_NATURAL;
	chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_INIT;
	chip->fastchg_commu_stop = false;
	chip->fastchg_check_stop = false;
	chip->fastchg_monitor_stop = false;
	chip->current_pwd = chip->current_expect;
	chip->curr_pwd_count = 10;
	chip->usb_bad_connect = false;
	chip->vooc_move_head = false;
	if (oplus_voocphy_get_bidirect_cp_support(chip)) {
		chip->fastchg_err_commu = false;
	}
	chip->ask_current_first = true;
	chip->copycat_icheck = false;
	chip->ask_bat_model_finished = false;
	chip->fastchg_need_reset = 0;
	oplus_voocphy_set_fastchg_state(chip, OPLUS_FASTCHG_STAGE_1);
	chip->fastchg_recover_cnt = 0;
	chip->fastchg_recovering = false;
	chip->reply_bat_model_end = false;
	if (chip->parallel_charge_support) {
		chip->parallel_change_current_count = 8;
		chip->parallel_switch_current = 0;
#ifdef OPLUS_CHG_UNDEF /* TODO */
		oplus_switching_set_current(2800);
#endif
	}
	/* get initial soc */
	chip->batt_soc = oplus_chglib_get_soc(chip->dev);

	if (chip->batt_fake_soc) {
		chip->batt_soc = chip->batt_fake_soc;
	}

	/* get initial temp */
	batt_temp = oplus_chglib_get_shell_temp(chip->dev);
	if (chip->batt_fake_temp) {
		batt_temp = chip->batt_fake_temp;
	}

	chip->fastchg_real_allow = false;

	voocphy_info( "!soc:%d, temp:%d, real_allow:%d", chip->batt_soc,
	              batt_temp, chip->fastchg_real_allow);

	/* note exception info */
	chip->vooc_flag = 0;
	chip->interrupt_flag = 0;
	chip->irq_total_num = 0;
	chip->irq_rcvok_num = 0;
	chip->irq_rcverr_num = 0;
	chip->irq_tx_timeout_num = 0;
	chip->irq_tx_timeout = 0;
	chip->irq_hw_timeout_num = 0;
	chip->irq_tx_fail_num = 0;
	chip->adapter_check_vooc_head_count = 0;
	chip->adapter_check_cmd_data_count = 0;
	chip->start_vaild_frame = false;
	chip->irq_rxdone_num = 0;
	chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
	chip->max_div_cp_ichg = 0;
	memset(chip->int_column, 0, sizeof(chip->int_column));
	memset(chip->int_column_pre, 0, sizeof(chip->int_column_pre));
	/* default vooc head as svooc */
	status = oplus_voocphy_write_mesg_mask(VOOC_INVERT_HEAD_MASK,
	                                       &chip->voocphy_tx_buff[0], chip->vooc_head);

	return status;
}

bool oplus_voocphy_chip_is_null(void)
{
	if (!g_voocphy_chip)
		return true;
	else
		return false;
}

static bool oplus_vooc_wake_monitor_start_work(struct oplus_voocphy_manager *chip)
{
	schedule_delayed_work(&chip->monitor_start_work, 0);

	return true;
}

static int oplus_voocphy_monitor_start(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;
	if (!chip) {
		voocphy_info( "chip is null");
		return VOOCPHY_EFATAL;
	}

	chip->fastchg_monitor_stop = false;
	oplus_vooc_wake_monitor_start_work(chip);

	voocphy_info( "vooc_monitor hrstart success");
	return status;
}

static int oplus_voocphy_monitor_stop(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_AP, VOOC_AP_EVENT_TIME);
	oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_CURR, VOOC_CURR_EVENT_TIME);
	oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_SOC, VOOC_SOC_EVENT_TIME);
	oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_VOL, VOOC_VOL_EVENT_TIME);
	oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_TEMP, VOOC_TEMP_EVENT_TIME);
	//oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_BTB, VOOC_BTB_EVENT_TIME);
	oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_SAFE, VOOC_SAFE_EVENT_TIME);
	oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_IBUS_CHECK, VOOC_IBUS_CHECK_TIME);
	chip->fastchg_monitor_stop = true;
	voocphy_info( "vooc_monitor stop hrtimer success");

	return status;
}

static int oplus_voocphy_hardware_monitor_stop(void)
{
	int status = VOOCPHY_SUCCESS;

	/*disable batt adc*/

	/*disable temp adc*/

	/*disable dchg irq*/

	return status;
}

static int oplus_voocphy_reset_voocphy(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFAILED;
	}

	//stop hardware monitor	//lkl need modify
	status = oplus_voocphy_hardware_monitor_stop();
	if (status != VOOCPHY_SUCCESS)
		return VOOCPHY_EFAILED;

	if (chip->slave_ops && chip->slave_ops->reset_voocphy) {
		chip->slave_ops->reset_voocphy(chip);
	}

	if (chip->ops && chip->ops->reset_voocphy) {
		chip->ops->reset_voocphy(chip);
	}

	/* When OVP control by slave CP, this interface must be implemented */
	if (chip->ovp_ctrl_cpindex == SLAVE_CP_ID &&
	    chip->slave_ops->reset_voocphy_ovp) {
		chip->slave_ops->reset_voocphy_ovp(chip);
	}

	return VOOCPHY_SUCCESS;
}

static int oplus_voocphy_set_adc_forcedly_enable(struct oplus_voocphy_manager *chip, int mode)
{
	int rc = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -ENODEV;
        }

	if (chip->ops && chip->ops->set_adc_forcedly_enable) {
		rc = chip->ops->set_adc_forcedly_enable(chip, mode);
	}

	return rc;
}

void oplus_voocphy_get_soc_and_temp_with_enter_fastchg(struct oplus_voocphy_manager *chip)
{
        int sys_curve_temp_idx = BATT_SYS_CURVE_TEMP_NORMAL_LOW;
        int batt_temp = 0;

        if (!chip) {
                voocphy_info("%s, chip null\n", __func__);
                return;
        }

        batt_temp = chip->plug_in_batt_temp;
        chip->batt_sys_curv_found = false;

        /* step1: find sys curv by soc */
        if (chip->batt_soc <= 50) {
                chip->batt_soc_plugin = BATT_SOC_0_TO_50;
        } else if (chip->batt_soc <= 75) {
                chip->batt_soc_plugin = BATT_SOC_50_TO_75;
        } else if (chip->batt_soc <= 85) {
                chip->batt_soc_plugin = BATT_SOC_75_TO_85;
        } else if (chip->batt_soc <= 90) {
                chip->batt_soc_plugin = BATT_SOC_85_TO_90;
        } else {
                chip->batt_soc_plugin = BATT_SOC_90_TO_100;
        }

        /* step2: find sys curv by temp range */
        /* update batt_temp_plugin status */
        if (batt_temp < chip->vooc_little_cold_temp) { /* 0-5C */
                chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COLD;
                chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COLD;
                sys_curve_temp_idx = BATT_SYS_CURVE_TEMP_LITTLE_COLD;
                chip->batt_temp_plugin = VOOCPHY_BATT_TEMP_LITTLE_COLD;
        } else if (batt_temp < chip->vooc_cool_temp) { /* 5-12C */
                chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
                chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
                sys_curve_temp_idx = BATT_SYS_CURVE_TEMP_COOL;
                chip->batt_temp_plugin = VOOCPHY_BATT_TEMP_COOL;
        } else if (batt_temp < chip->vooc_little_cool_temp) { /* 12-16C */
                chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
                chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
                sys_curve_temp_idx = BATT_SYS_CURVE_TEMP_LITTLE_COOL;
                chip->batt_temp_plugin = VOOCPHY_BATT_TEMP_LITTLE_COOL;
        } else if (batt_temp < chip->vooc_normal_low_temp) { /* 16-35C */
                chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
                chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
                sys_curve_temp_idx = BATT_SYS_CURVE_TEMP_NORMAL_LOW;
                chip->batt_temp_plugin = VOOCPHY_BATT_TEMP_NORMAL;
        } else {/* 35C-43C */
                if (chip->vooc_normal_high_temp == -EINVAL || batt_temp < chip->vooc_normal_high_temp) {
                        chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_HIGH;
                        chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_HIGH;
                        sys_curve_temp_idx = BATT_SYS_CURVE_TEMP_NORMAL_HIGH;
                        chip->batt_temp_plugin = VOOCPHY_BATT_TEMP_NORMAL;
                } else {
                        chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_WARM;
                        chip->fastchg_batt_temp_status = BAT_TEMP_WARM;
                        sys_curve_temp_idx = BATT_SYS_CURVE_TEMP_WARM;
                        chip->batt_temp_plugin = VOOCPHY_BATT_TEMP_WARM;
                }
        }

        chip->sys_curve_temp_idx = sys_curve_temp_idx;

        voocphy_info("current_expect:%d batt_soc_plugin:%d batt_temp_plugin:%d sys_curve_index:%d",
                     chip->current_expect, chip->batt_soc_plugin,
                     chip->batt_temp_plugin, sys_curve_temp_idx);
        return;
}

static int oplus_voocphy_get_adc_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int rc = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}

	if (chip->ops && chip->ops->get_adc_enable) {
		rc = chip->ops->get_adc_enable(chip, data);
		if (rc < 0) {
			voocphy_info("failed to get voocphy adc status(%d)\n", rc);
			return rc;
		}
	}

	return rc;
}

static int oplus_voocphy_set_adc_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	int rc = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}

	if (chip->ops && chip->ops->set_adc_enable) {
		rc = chip->ops->set_adc_enable(chip, enable);
		if (rc < 0) {
			voocphy_info("failed to %s voocphy adc(%d)\n",
				     enable ? "enable" : "disable", rc);
			return rc;
		}
	}

	return rc;
}

static int oplus_voocphy_init_vooc(struct oplus_voocphy_manager *chip)
{
	int rc = 0;
	u8 adc_reg_data;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -1;
	}

	oplus_voocphy_reset_variables(chip);
	chip->fastchg_real_allow = oplus_voocphy_check_fastchg_real_allow(chip);

	if (chip->ops && chip->ops->init_vooc) {
		rc = chip->ops->init_vooc(chip);
	}

	oplus_voocphy_get_adc_enable(chip, &adc_reg_data);
	if (adc_reg_data == 0) {
		voocphy_info("Enable cp adc");
		oplus_voocphy_set_adc_enable(chip, true);
	}

	chip->batt_soc = oplus_chglib_get_soc(chip->dev);
	if (chip->batt_fake_soc) {
		chip->batt_soc = chip->batt_fake_soc;
	}
	chip->plug_in_batt_temp = oplus_chglib_get_shell_temp(chip->dev);
	if (chip->batt_fake_temp) {
		chip->plug_in_batt_temp = chip->batt_fake_temp;
	}

	chip->fastchg_real_allow = oplus_voocphy_check_fastchg_real_allow(chip);

	chip->ask_current_first = true;
	chip->copycat_icheck = false;
	oplus_voocphy_get_soc_and_temp_with_enter_fastchg(chip);

	voocphy_info("batt_soc %d, plug_in_batt_temp = %d\n", chip->batt_soc, chip->plug_in_batt_temp);

	return rc;
}

static int oplus_voocphy_print_dbg_info(struct oplus_voocphy_manager *chip)
{
	int i = 0;
	u8 value = 0;
	bool fg_dump_reg = false;
	bool fg_send_info = false;
	int report_flag = 0;

	if (chip->ops && chip->ops->check_cp_int_happened &&
	    chip->ops->check_cp_int_happened(chip, &fg_dump_reg, &fg_send_info))
		goto chg_exception;

	/*print irq event*/
	for (i = 0; i < IRQ_EVNET_NUM; i++) {
		if ((vooc_flag[i].mask & chip->vooc_flag) && vooc_flag[i].mark_except) {
			voocphy_err("vooc happened %s\n", vooc_flag[i].except_info);
			goto chg_exception;
		}
	}

	if (chip->fastchg_adapter_ask_cmd != VOOC_CMD_GET_BATT_VOL) {
		goto chg_exception;
	} else {
		if ((chip->irq_total_num % 50) == 0) {		//about 2s
			goto chg_exception;
		}
	}
	return 0;

chg_exception:
	chip->voocphy_cp_irq_flag = chip->interrupt_flag;
	chip->voocphy_vooc_irq_flag = chip->vooc_flag;
	chip->disconn_pre_vbat_calc = chip->vbat_calc;
	if (chip->ops && chip->ops->get_voocphy_enable) {
		chip->ops->get_voocphy_enable(chip, &value);
		chip->voocphy_enable = value;
	}
	if (chip->voocphy_dual_cp_support &&
	    chip->slave_ops && chip->slave_ops->get_voocphy_enable) {
	    	value = 0;
		chip->slave_ops->get_voocphy_enable(chip, &value);
		chip->slave_voocphy_enable = value;
	}
	chip->vbat_calc = 0;
	if (fg_dump_reg) {
		if (chip->ops && chip->ops->dump_voocphy_reg) {
			chip->ops->dump_voocphy_reg(chip);
			fg_dump_reg = false;
		}
		if (chip->voocphy_dual_cp_support &&
		    chip->slave_ops && chip->slave_ops->dump_voocphy_reg) {
			chip->slave_ops->dump_voocphy_reg(chip);
			fg_dump_reg = false;
		}
		if (fg_send_info) {
			report_flag |= (1 << 4);
		}
	}

	voocphy_err("voocphydbg data[%d %d %d %d %d], status[%d, %d], init[%d, %d], set[%d, %d]"
		    "comm[rcv:%d, 0x%0x, 0x%0x, %d, 0x%0x, 0x%0x reply:0x%0x 0x%0x], "
		    "irqinfo[%d, %d, %d, %d, %d, %d, %d, %d] other[%d]\n",
		    chip->cp_vsys, chip->cp_ichg, chip->icharging, chip->cp_vbus,chip->gauge_vbatt,
		    chip->vooc_vbus_status, chip->vbus_vbatt,
		    chip->batt_soc_plugin, chip->batt_temp_plugin,
		    chip->current_expect, chip->current_max,
		    chip->vooc_move_head, chip->voocphy_rx_buff,
		    chip->vooc_head, chip->adapter_type,
		    chip->adapter_mesg, chip->fastchg_adapter_ask_cmd,
		    chip->voocphy_tx_buff[0], chip->voocphy_tx_buff[1],
		    chip->vooc_flag, chip->interrupt_flag, chip->irq_total_num,
		    chip->irq_rcvok_num, chip->irq_rcverr_num, chip->irq_tx_timeout_num,
		    chip->irq_tx_timeout, chip->irq_hw_timeout_num,
		    base_cpufreq_for_chg);

	return 0;
}

static void voocphy_service(struct work_struct *work)
{
	struct oplus_voocphy_manager *chip
	    = container_of(work, struct oplus_voocphy_manager,
	                   voocphy_service_work.work);

	if (!chip) {
		voocphy_err("oplus_voocphy_manager is null\n");
		return;
	}

	if (chip->parallel_charge_support) {
		if (chip->adapter_mesg == VOOC_CMD_IS_VUBS_OK) {
		chip->main_vbatt = oplus_chglib_gauge_vbatt(chip->dev);
		chip->sub_vbatt = oplus_chglib_gauge_sub_vbatt();
		chip->pre_gauge_vbatt = chip->main_vbatt > chip->sub_vbatt ? chip->main_vbatt : chip->sub_vbatt;
		chip->main_batt_icharging = -oplus_chglib_gauge_current(chip->dev);
		chip->sub_batt_icharging = -oplus_chglib_gauge_sub_current();
		chip->icharging = chip->main_batt_icharging + chip->sub_batt_icharging;
		voocphy_info("batt[%d, %d], main_batt[%d, %d], sub_batt[%d, %d]\n",
				chip->pre_gauge_vbatt, chip->icharging, chip->main_vbatt, chip->main_batt_icharging,
				chip->sub_vbatt, chip->sub_batt_icharging);
		}
		chip->icharging = chip->main_batt_icharging + chip->sub_batt_icharging;
	} else {
		if ((chip->external_gauge_support) && (chip->adapter_mesg == VOOC_CMD_IS_VUBS_OK)) {
			chip->gauge_vbatt = chip->pre_gauge_vbatt = oplus_chglib_gauge_vbatt(chip->dev);
		}
		chip->icharging = -oplus_chglib_gauge_current(chip->dev);
	}

	oplus_voocphy_print_dbg_info(chip);
}

static bool oplus_vooc_wake_voocphy_service_work(struct oplus_voocphy_manager *chip, int request)
{
	chip->voocphy_request = request;
	schedule_delayed_work(&chip->voocphy_service_work, 0);

	if (oplus_voocphy_get_bidirect_cp_support(chip)) {
		memcpy(chip->int_column_pre, chip->int_column, sizeof(chip->int_column));
		voocphy_dbg("request %d 09~0E[0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x]\n",
			    request, chip->int_column_pre[0], chip->int_column_pre[1],
			    chip->int_column_pre[2], chip->int_column_pre[3],
			    chip->int_column_pre[4], chip->int_column_pre[5]);
	}

	return true;
}

static bool oplus_vooc_wake_monitor_work(struct oplus_voocphy_manager *chip)
{
	schedule_delayed_work(&chip->monitor_work, 0);

	return true;
}

static int oplus_voocphy_get_adapter_info(struct oplus_voocphy_manager *chip)
{
	int rc = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}

	if (chip->ops && chip->ops->get_adapter_info) {
		oplus_voocphy_pm_qos_update(400);
		rc = chip->ops->get_adapter_info(chip);
	}

	return rc;
}

static bool oplus_voocphy_ap_allow_fastchg(struct oplus_voocphy_manager *chip)
{
	bool ap_allow_fastchg = false;

	if (!chip) {
		voocphy_info("%s, chip null\n", __func__);
		return false;
	}

	if (chip->fastchg_adapter_ask_cmd == VOOC_CMD_ASK_FASTCHG_ORNOT) {
		ap_allow_fastchg = (chip->voocphy_tx_buff[0] & AP_ALLOW_FASTCHG);
		if (ap_allow_fastchg) {
			voocphy_info("fastchg_stage OPLUS_FASTCHG_STAGE_2\n");
			oplus_voocphy_set_fastchg_state(chip, OPLUS_FASTCHG_STAGE_2);
		}
		return ap_allow_fastchg;
	}

	return false;
}

static int oplus_voocphy_get_write_txbuff_error(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return 0;
	}

	return chip->vooc_flag & (1 << 3);
}

static u8 oplus_voocphy_get_int_value(struct oplus_voocphy_manager *chip)
{
	u8 value = 0;

	if (!chip) {
		voocphy_err("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}

	if (chip->ops && chip->ops->get_int_value) {
		oplus_voocphy_pm_qos_update(400);
		value = chip->ops->get_int_value(chip);
	}

	return value;
}

static bool oplus_voocphy_int_disable_chg(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		voocphy_err("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}
	chip->interrupt_flag = oplus_voocphy_get_int_value(chip);
	voocphy_err("chip->int_column[1] = 0x%x\n", chip->interrupt_flag);

	if ((chip->interrupt_flag & BIT(0)) == 0)
		return true;
	else
		return false;
}

static int oplus_voocphy_clear_interrupts(struct oplus_voocphy_manager *chip)
{
	int rc = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}

	if (chip->ops && chip->ops->clear_interrupts) {
		rc = chip->ops->clear_interrupts(chip);
	}

	return rc;
}

int oplus_voocphy_parallel_curve_convert(unsigned char curr)
{
	int i = 0;

	for(i = 0;i < 7;i++) {
		if (svooc_parallel_curve[i].curve_data.svooc_current == 0)
			return svooc_parallel_curve[i-1].curve_data.parallel_switch_current;

		if (curr >= svooc_parallel_curve[i].curve_data.svooc_current)
			return svooc_parallel_curve[i].curve_data.parallel_switch_current;
	}

	return 2800;
}

static void oplus_voocphy_update_data(struct oplus_voocphy_manager *chip)
{
	int div_cp_ichg;
	int slave_cp_ichg;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return ;
	}

	if (chip->ops && chip->ops->update_data) {
		oplus_voocphy_pm_qos_update(400);
		chip->ops->update_data(chip);
		chip->master_cp_ichg = chip->cp_ichg;
	}

	if (chip->external_gauge_support) {
		if (chip->parallel_charge_support) {
			chip->gauge_vbatt = chip->pre_gauge_vbatt;
		} else {
			chip->gauge_vbatt = oplus_chglib_gauge_pre_vbatt(chip->dev);
		}
	} else {
		chip->gauge_vbatt = chip->cp_vbat;
	}

	voocphy_info("parallel_change_current_count:%d\n", chip->parallel_change_current_count);
	if (chip->parallel_charge_support && (chip->parallel_change_current_count > 0)) {
		if (chip->parallel_change_current_count == 1) {
			chip->parallel_change_current_count = 0;
			if (chip->parallel_switch_current == 0) {
				chip->parallel_switch_current = oplus_voocphy_parallel_curve_convert(chip->current_expect);
				voocphy_info("current init,parallel_switch_current:%d", chip->parallel_switch_current);
			}
			voocphy_info("current set parallel_switch_current:%d", chip->parallel_switch_current);
#ifdef OPLUS_CHG_UNDEF /* TODO */
			oplus_switching_set_current(chip->parallel_switch_current);
#endif
		} else {
			chip->parallel_change_current_count--;
			voocphy_info("waiting :%d,parallel_switch_current:%d", chip->parallel_change_current_count, chip->parallel_switch_current);
		}
	}

	if (chip->voocphy_dual_cp_support) {
		if (!chip->slave_ops) {
			voocphy_err("slave_ops is NULL!\n");
		} else {
			slave_cp_ichg = chip->slave_ops->get_ichg(chip);
			div_cp_ichg = (chip->cp_ichg > slave_cp_ichg)? (chip->cp_ichg - slave_cp_ichg) : (slave_cp_ichg - chip->cp_ichg);
			chip->cp_ichg = chip->cp_ichg + slave_cp_ichg;
			voocphy_err("total cp_ichg = %d div_ichg = %d\n", chip->cp_ichg, div_cp_ichg);
		}
	}

	return;
}

static int oplus_voocphy_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	int rc = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}

	if (chip->ops && chip->ops->get_chg_enable) {
		rc = chip->ops->get_chg_enable(chip, data);
		if (rc < 0) {
			voocphy_info("oplus_voocphy_get_chg_enable, rc=%d.\n", rc);
			return rc;
		}
	}

	return rc;
}

static int oplus_voocphy_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	int rc = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}

	if (chip->ops && chip->ops->set_chg_enable) {
		rc = chip->ops->set_chg_enable(chip, enable);
		if (rc < 0) {
			voocphy_info("oplus_voocphy_set_chg_enable fail, rc=%d.\n", rc);
			return rc;
		}
	}

	return rc;
}

static int oplus_voocphy_get_mesg_from_adapter(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;
	unsigned char vooc_head = 0;
	unsigned char vooc_move_head = 0;
	unsigned char dchg_enable_status = 0;

	if (!chip) {
		voocphy_info("%s, chip null\n", __func__);
		return VOOCPHY_EFATAL;
	}

	/* handle the frame head & adapter_data */
	vooc_head = (chip->voocphy_rx_buff & VOOC_HEAD_MASK) >> VOOC_HEAD_SHIFT;

	/* handle the shift frame head */
	vooc_move_head = (chip->voocphy_rx_buff & VOOC_MOVE_HEAD_MASK)
	                 >> VOOC_MOVE_HEAD_SHIFT;

	if (vooc_head == SVOOC_HEAD || vooc_move_head == SVOOC_HEAD) {
		if (vooc_head == SVOOC_HEAD) {
			voocphy_info("SVOOC_HEAD");
		} else {
			voocphy_info("SVOOC_MOVE_HEAD");
			chip->vooc_move_head = true;
		}
		chip->vooc_head = SVOOC_INVERT_HEAD;
		chip->adapter_type = ADAPTER_SVOOC; /* detect svooc adapter */
		chip->adapter_check_vooc_head_count = 0;
	} else if (vooc_head == VOOC3_HEAD || vooc_move_head == VOOC3_HEAD) {
		if (vooc_head == VOOC3_HEAD) {
			voocphy_info("VOOC30_HEAD");
		} else {
			voocphy_info("VOOC30_MOVE_HEAD");
			chip->vooc_move_head = true;
		}
		chip->vooc_head = VOOC3_INVERT_HEAD;
		chip->adapter_type = ADAPTER_VOOC30; /* detect vooc30 adapter */
		chip->adapter_check_vooc_head_count = 0;
	} else if (vooc_head == VOOC2_HEAD ||vooc_move_head == VOOC2_HEAD) {
		chip->adapter_check_vooc_head_count = 0;
		if (vooc_move_head == VOOC2_HEAD) {
			voocphy_info("VOOC20_MOVE_HEAD");
			chip->vooc_move_head = true;
		}

		if (chip->adapter_model_detect_count) {
			chip->adapter_model_detect_count--; /* detect three times */
		} else {	/* adapter_model_detect_count */
			chip->adapter_type = ADAPTER_VOOC30; /* detect svooc adapter */
			if (oplus_voocphy_get_bidirect_cp_support(chip) == false) {
				if (!chip->ask_vooc3_detect) {
					chip->ask_vooc3_detect = true;
					chip->adapter_model_detect_count = 3;
					chip->fastchg_allow = false;
					chip->vooc_head = VOOC3_INVERT_HEAD; /* set vooc3 head to detect */
				} else {
					voocphy_info("VOOC20_HEAD chip->fastchg_start = %d, chip->fastchg_allow = %d\n",
						     chip->fastchg_start, chip->fastchg_allow);
					chip->adapter_type = ADAPTER_VOOC20; /* old vooc2.0 adapter */
					chip->vooc_head = VOOC2_INVERT_HEAD;
					chip->adapter_model_ver = FASTCHG_CHARGER_TYPE_VOOC;
					if (!chip->fastchg_start) {
						if (chip->fastchg_real_allow) {
							oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_PRESENT);
							chip->fastchg_allow = true;
							chip->fastchg_start = true;
							chip->fastchg_to_warm = false;
							chip->fastchg_dummy_start = false;
						} else {
							chip->fastchg_allow = false;
							chip->fastchg_start = false;
							chip->fastchg_to_warm = false;
							chip->fastchg_dummy_start = true;
							oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_DUMMY_START);
							voocphy_info("fastchg_dummy_start and reset voocphy");
						}
						chip->reply_bat_model_end = true;
					}
				}
			} else {
				voocphy_info("POWER_BANK_MODE");
				chip->vooc_head = VOOC2_INVERT_HEAD;
				/* power_bank mode */
				chip->adapter_type = ADAPTER_VOOC20;
				/* power_bank mode need set fastchg_allow false */
				chip->fastchg_allow = false;
				chip->reply_bat_model_end = true;
			}
		}
	} else {
		chip->rcv_date_err_num++;
		chip->adapter_check_vooc_head_count++;
		oplus_voocphy_get_chg_enable(chip, &dchg_enable_status);
		if (dchg_enable_status != FAST_CHARGER_MOS_DISABLE &&
		    chip->pre_adapter_ask_cmd == VOOC_CMD_GET_BATT_VOL &&
		    chip->adapter_check_vooc_head_count < ADAPTER_CHECK_VOOC_HEAD_TIMES &&
		    chip->adjust_curr != ADJ_CUR_STEP_REPLY_VBATT) {
			voocphy_info("unknow frame head error, try again adapter_check_vooc_head_count =%d",
				     chip->adapter_check_vooc_head_count);
			return VOOCPHY_EUNSUPPORTED;
		} else {
			voocphy_info("unknow frame head, reset voocphy & fastchg rerun check"
				     " adapter_check_vooc_head_count = %d\n",
				     chip->adapter_check_vooc_head_count);
			oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ERR_COMMU);
			return VOOCPHY_EUNSUPPORTED;
		}
	}

	if (!chip->vooc_move_head) {
		status = oplus_voocphy_read_mesg_mask(VOOC_MESG_READ_MASK,
		                                      chip->voocphy_rx_buff, &chip->adapter_mesg);
	} else {
		status = oplus_voocphy_read_mesg_mask(VOOC_MESG_MOVE_READ_MASK,
		                                      chip->voocphy_rx_buff, &chip->adapter_mesg);
	}

	if (chip->fastchg_reactive == true
	    && chip->adapter_mesg == VOOC_CMD_ASK_FASTCHG_ORNOT) {
		chip->fastchg_reactive = false;
	}

	if (chip->fastchg_reactive == true) {
		voocphy_info("(fastchg_dummy_start or err commu) && adapter_mesg != 0x04");
		status = VOOCPHY_EUNSUPPORTED;
	}
	voocphy_info("adapter_mesg=0x%0x", chip->adapter_mesg);

	return status;
}

static int oplus_voocphy_handle_ask_fastchg_ornot_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	chip->adapter_ask_fastchg_ornot_count++;

	switch (chip->adapter_type) {
	case ADAPTER_VOOC20 :
		if (chip->vooc2_next_cmd != VOOC_CMD_ASK_FASTCHG_ORNOT) {
			voocphy_info("copycat adapter");
			/* reset voocphy */
			if (chip->copycat_vooc_support == true)
				chip->copycat_vooc_adapter = true;
			status = oplus_voocphy_reset_voocphy(chip);
		}

		/* reset vooc2_next_cmd */
		if (chip->fastchg_allow) {
			chip->vooc2_next_cmd = VOOC_CMD_IS_VUBS_OK;
		} else {
			chip->vooc2_next_cmd = VOOC_CMD_ASK_FASTCHG_ORNOT;
		}

		voocphy_info("VOOC20 handle ask_fastchg_ornot\n");
		/* set bit3 as fastchg_allow & set circuit R */
		if (chip->parallel_charge_support) {
			status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
							       &chip->voocphy_tx_buff[0],
							       VOOC2_CIRCUIT_PARALLEL_R_L_DEF | chip->fastchg_allow);
			status = oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
							       &chip->voocphy_tx_buff[1],
							       VOOC2_CIRCUIT_PARALLEL_R_H_DEF);
		} else {
			status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
							       &chip->voocphy_tx_buff[0],
							       VOOC2_CIRCUIT_R_L_DEF | chip->fastchg_allow);
			status = oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
							       &chip->voocphy_tx_buff[1],
							       VOOC2_CIRCUIT_R_H_DEF);
		}
		break;
	case ADAPTER_VOOC30 :
		voocphy_info("VOOC30 handle ask_fastchg_ornot");
		/* set bit3 as fastchg_allow & set circuit R */
		if (chip->parallel_charge_support) {
			status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
							       &chip->voocphy_tx_buff[0],
							       NO_VOOC3_CIRCUIT_PARALLEL_R_L_DEF | chip->fastchg_allow);
			status = oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
							       &chip->voocphy_tx_buff[1],
							       NO_VOOC3_CIRCUIT_PARALLEL_R_H_DEF);
		} else {
			status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
							       &chip->voocphy_tx_buff[0],
							       NO_VOOC3_CIRCUIT_R_L_DEF | chip->fastchg_allow);
			status = oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
							       &chip->voocphy_tx_buff[1],
							       NO_VOOC3_CIRCUIT_R_H_DEF);
		}
		break;
	case ADAPTER_SVOOC :
		voocphy_info("SVOOC handle ask_fastchg_ornot");
		/* set bit3 as fastchg_allow & set circuit R */
		if (oplus_voocphy_get_bidirect_cp_support(chip)) {
			status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
							       &chip->voocphy_tx_buff[0],
							       BIDRECT_CIRCUIT_R_L_DEF | chip->fastchg_allow);
			status = oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
							       &chip->voocphy_tx_buff[1],
							       NO_VOOC2_CIRCUIT_R_H_DEF);
		} else if (chip->parallel_charge_support) {
			if (chip->impedance_calculation_newmethod) {
				status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
						&chip->voocphy_tx_buff[0], chip->svooc_circuit_r_l
						| chip->fastchg_allow);
				status = oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
						&chip->voocphy_tx_buff[1], chip->svooc_circuit_r_h);
			} else {
				status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
						&chip->voocphy_tx_buff[0], NO_VOOC2_CIRCUIT_PARALLEL_R_L_DEF
						| chip->fastchg_allow);
				status = oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
						&chip->voocphy_tx_buff[1], NO_VOOC2_CIRCUIT_PARALLEL_R_H_DEF);
			}
		} else {
			status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
							       &chip->voocphy_tx_buff[0],
							       NO_VOOC2_CIRCUIT_R_L_DEF | chip->fastchg_allow);
			status = oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
							       &chip->voocphy_tx_buff[1],
							       NO_VOOC2_CIRCUIT_R_H_DEF);
		}
		break;
	default:
		voocphy_info("should not go to here");
		break;
	}

	/* for vooc3.0  and svooc adapter checksum */
	chip->adapter_rand_start = true;

	return status;
}

static int oplus_voocphy_calcucate_identification_code(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;
	unsigned char code_id_temp_l = 0, code_id_temp_h = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	chip->code_id_local = ((chip->adapter_rand_h << 9)
	                       |(chip->adapter_rand_l << 1));

	/* set present need send mesg */
	code_id_temp_l = ((((chip->adapter_rand_l >> 6) & 0x1) << TX0_DET_BIT0_SHIFT)
	                  | (((chip->adapter_rand_l >> 4) & 0x1) << TX0_DET_BIT1_SHIFT)
	                  | (((chip->adapter_rand_h >> 6) & 0x1) << TX0_DET_BIT2_SHIFT)
	                  | ((chip->adapter_rand_l & 0x1) << TX0_DET_BIT3_SHIFT)
	                  | ((chip->adapter_rand_h & 0x1) << TX0_DET_BIT4_SHIFT));
	code_id_temp_h = ((((chip->adapter_rand_h >> 4) & 0x1) << TX1_DET_BIT0_SHIFT)
	                  | (((chip->adapter_rand_l >> 2) & 0x1) << TX1_DET_BIT1_SHIFT));
	status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
	                                       &chip->voocphy_tx_buff[0], code_id_temp_l);
	status |= oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
	                                        &chip->voocphy_tx_buff[1], code_id_temp_h);

	/* save next time need send mesg */
	chip->code_id_temp_l = ((((chip->adapter_rand_l >> 3) & 0x1))
	                        | (((chip->adapter_rand_h >> 5) & 0x1) << TX0_DET_BIT1_SHIFT)
	                        | (((chip->adapter_rand_l >> 5) & 0x1) << TX0_DET_BIT2_SHIFT)
	                        | (((chip->adapter_rand_h >> 1) & 0x1) << TX0_DET_BIT3_SHIFT)
	                        | (((chip->adapter_rand_h >> 2) & 0x1) << TX0_DET_BIT4_SHIFT));

	chip->code_id_temp_h = ((((chip->adapter_rand_l >> 1) & 0x1))
	                        | (((chip->adapter_rand_h >> 3) & 0x1) << TX1_DET_BIT1_SHIFT));

	voocphy_info("present_code_id_temp_l:%0x, present_code_id_temp_h: %0x,"
	             "next_code_id_temp_l:%0x, next_code_id_temp_h:%0x",
	             code_id_temp_l, code_id_temp_h, chip->code_id_temp_l, chip->code_id_temp_h);

	return status;
}

static int oplus_voocphy_handle_identification_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;
	uint16_t temp = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	/* choose response the identification cmd */
	if (chip->adapter_ask_check_count == 0
	    ||chip->adapter_rand_start == false)
		return status;

	/* single vooc the idenficaition should not over 3 times */
	chip->adapter_ask_check_count--;
	chip->adapter_check_cmmu_count = ADAPTER_CHECK_COMMU_TIMES;
	get_random_bytes(&temp, 2);
	chip->adapter_rand_l = ((unsigned char)temp) & 0x7F;
	chip->adapter_rand_h = ((unsigned char)(temp >> 8)) & 0x7F;
	voocphy_info("oplus_vooc_handle_identification_cmd,adapter_rand_l:%d, adapter_rand_h:%d\n",
		chip->adapter_rand_l, chip->adapter_rand_h);
	/* next adapter ask cmd should be adapater_check_commu_process */
	chip->fastchg_adapter_ask_cmd = VOOC_CMD_ADAPTER_CHECK_COMMU_PROCESS;

	/* calculate the identification code that need send to adapter */
	status = oplus_voocphy_calcucate_identification_code(chip);

	return status;
}

static int oplus_voocphy_handle_adapter_check_commu_process_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	if (chip->adapter_check_cmmu_count == 5) { /* send before save rand information */
		status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
		                                       &chip->voocphy_tx_buff[0], chip->code_id_temp_l);
		status = oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
		                                       &chip->voocphy_tx_buff[1], chip->code_id_temp_h);
	} else if (chip->adapter_check_cmmu_count == 4) {
		chip->code_id_far = chip->adapter_mesg << 12;
		status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT5_MASK,
		                                       &chip->voocphy_tx_buff[0], BIT_ACTIVE);

	} else if (chip->adapter_check_cmmu_count == 3) {
		chip->code_id_far = chip->code_id_far
		                    | (chip->adapter_mesg << 8);
		status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT5_MASK,
		                                       &chip->voocphy_tx_buff[0], BIT_ACTIVE);

	} else if (chip->adapter_check_cmmu_count== 2) {
		chip->code_id_far = chip->code_id_far
		                    | (chip->adapter_mesg << 4);
		status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT5_MASK,
		                                       &chip->voocphy_tx_buff[0], BIT_ACTIVE);

	} else if (chip->adapter_check_cmmu_count == 1) {
		chip->code_id_far = chip->code_id_far |chip->adapter_mesg;
		chip->code_id_far &= 0xfefe;
		voocphy_info( "!!!!code_id_far: 0x%0x, code_id_local: 0x%0x",
		              chip->code_id_far, chip->code_id_local);
		if (chip->code_id_far == chip->code_id_local) {
			chip->adapter_check_ok = true;
			voocphy_info( "adapter check ok");
		}
		status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT5_MASK,
		                                       &chip->voocphy_tx_buff[0], BIT_ACTIVE);
		chip->fastchg_adapter_ask_cmd = VOOC_CMD_UNKNOW; /* reset cmd type */

		if (chip->code_id_far != chip->code_id_local && chip->irq_tx_fail_num) {
			voocphy_info( "adapter check fail because TXDATA_WR_FAIL");
			oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ADAPTER_STATUS_ABNORMAL);
		}
	} else {
		voocphy_info( "should not go here");
	}

	return status;
}

static int oplus_voocphy_handle_tell_model_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	chip->adapter_model_count = ADAPTER_MODEL_TIMES;

	/* next adapter ask cmd should be adapater_tell_model_process */
	chip->fastchg_adapter_ask_cmd = VOOC_CMD_TELL_MODEL_PROCESS;

	status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT4_MASK,
	                                       &chip->voocphy_tx_buff[0], BIT_ACTIVE);
	status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT6_MASK,
	                                       &chip->voocphy_tx_buff[0], BIT_ACTIVE);

	return status;
}

static int oplus_voocphy_handle_tell_model_process_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	if (chip->adapter_model_count == 2) {
		chip->adapter_model_ver = (chip->adapter_mesg << 4);
		status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT5_MASK,
		                                       &chip->voocphy_tx_buff[0], BIT_ACTIVE);
	} else if (chip->adapter_model_count == 1) {
		status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT5_MASK,
		                                       &chip->voocphy_tx_buff[0], BIT_ACTIVE);
		chip->adapter_model_ver = chip->adapter_model_ver
		                          | chip->adapter_mesg;
		/* adapter id bit7 is need to clear */
		chip->adapter_model_ver &= 0x7F;
		/* some 0x12/0x13 adapter is 20W */
		if ((chip->adapter_model_ver == 0x12 || chip->adapter_model_ver == 0x13) &&
		     chip->adapter_type != ADAPTER_SVOOC) {
			chip->adapter_model_ver = FASTCHG_CHARGER_TYPE_VOOC;
		}
		voocphy_info( "adapter_model_ver:0x%0x",
		              chip->adapter_model_ver );
		chip->fastchg_adapter_ask_cmd = VOOC_CMD_UNKNOW; /* reset cmd type */
	} else {
		voocphy_info( "should not go here");
	}

	return status;
}

static int oplus_voocphy_handle_ask_bat_model_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	chip->ask_batt_sys = BATT_SYS_ARRAY + 1;

	/* next adapter ask cmd should be ask_batt_sys_process */
	chip->fastchg_adapter_ask_cmd = VOOC_CMD_ASK_BATT_SYS_PROCESS;

	status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT6_MASK,
	                                       &chip->voocphy_tx_buff[0], BATT_SYS_VOL_CUR_PAIR);

	if (chip->copycat_vooc_support)
		oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_CHECK_FASTCHG_REAL_ALLOW);

	return status;
}

static int oplus_voocphy_hw_setting(struct oplus_voocphy_manager *chip, int reason)
{
	int rc = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}

	if (chip->ops && chip->ops->hw_setting)
		rc = chip->ops->hw_setting(chip, reason);

	return rc;
}

static int oplus_voocphy_handle_ask_bat_model_process_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;
	unsigned char temp_data_l = 0;
	unsigned char temp_data_h = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	if (chip->ask_batt_sys) {
		if (chip->adapter_type == ADAPTER_SVOOC) {  /* svooc battery curve */
			temp_data_l = (svooc_batt_sys_curve[chip->ask_batt_sys - 1][0] << 0)
			              | (svooc_batt_sys_curve[chip->ask_batt_sys - 1][1] << 1)
			              | (svooc_batt_sys_curve[chip->ask_batt_sys - 1][2] << 2)
			              | (svooc_batt_sys_curve[chip->ask_batt_sys - 1][3] << 3)
			              | (svooc_batt_sys_curve[chip->ask_batt_sys - 1][4] << 4);

			temp_data_h = (svooc_batt_sys_curve[chip->ask_batt_sys - 1][5] << 0)
			              | (svooc_batt_sys_curve[chip->ask_batt_sys - 1][6] << 1);
		} else if (chip->adapter_type == ADAPTER_VOOC30) {	//for cp
			temp_data_l = (vooc_batt_sys_curve[chip->ask_batt_sys - 1][0] << 0)
			              | (vooc_batt_sys_curve[chip->ask_batt_sys - 1][1] << 1)
			              | (vooc_batt_sys_curve[chip->ask_batt_sys - 1][2] << 2)
			              | (vooc_batt_sys_curve[chip->ask_batt_sys - 1][3] << 3)
			              | (vooc_batt_sys_curve[chip->ask_batt_sys - 1][4] << 4);

			temp_data_h = (vooc_batt_sys_curve[chip->ask_batt_sys - 1][5] << 0)
			              | (vooc_batt_sys_curve[chip->ask_batt_sys - 1][6] << 1);
		} else {
			voocphy_info( "error adapter type when write batt_sys_curve");
		}

		oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
		                              &chip->voocphy_tx_buff[0], temp_data_l);
		oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
		                              &chip->voocphy_tx_buff[1], temp_data_h);
		if (chip->ask_batt_sys == 1) {
			if (chip->adapter_model_ver == 0 && (chip->adapter_type == ADAPTER_SVOOC
			    || chip->adapter_type == ADAPTER_VOOC30)) {
				voocphy_info( "set adapter status abnormal");
				oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ADAPTER_STATUS_ABNORMAL);
			} else {
				if (chip->fastchg_real_allow) {
					chip->fastchg_allow = true; //batt allow dchg
					if (oplus_voocphy_get_bidirect_cp_support(chip))
						chip->fastchg_start = false;
					else
						chip->fastchg_start = true;
					chip->fastchg_to_warm = false;
					chip->fastchg_dummy_start = false;
					oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_PRESENT);
					voocphy_info( "fastchg_real_allow = true");
					if (chip->adapter_type == ADAPTER_SVOOC && chip->copycat_vooc_support == true) {
						oplus_voocphy_hw_setting(chip, SETTING_REASON_COPYCAT_SVOOC);
						if (chip->voocphy_dual_cp_support)
							oplus_voocphy_slave_hw_setting(chip, SETTING_REASON_COPYCAT_SVOOC);
					}
				} else {
					chip->fastchg_allow = false;
					chip->fastchg_start = false;
					chip->fastchg_to_warm = false;
					chip->fastchg_dummy_start = true;
					oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_DUMMY_START);
					voocphy_info( "fastchg_dummy_start and reset voocphy");
				}
				chip->reply_bat_model_end = true;
			}
			chip->ask_bat_model_finished = true;
			chip->fastchg_adapter_ask_cmd = VOOC_CMD_UNKNOW; //reset cmd
		}
	} else {
		voocphy_info( "should not go here");
	}

	return status;
}

#define NORMAL_VBUS_MIN		2000
#define NORMAL_VBAT_MIN		2000
#define VBUS_VBATT_ERR_NUM	3
static int oplus_voocphy_vbus_vbatt_detect(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;
	int vbus_vbatt = 0;
	u8 vbus_status = 0;
	static int vbus_vbatt_detect_err_count = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	if (oplus_voocphy_get_bidirect_cp_support(chip)) {
		if (chip->ops->get_vbus_status) {
			vbus_status = chip->ops->get_vbus_status(chip);
		} else {
			voocphy_info("get_vbus_status is null\n");
			chip->vooc_vbus_status = VOOC_VBUS_LOW;
			return VOOCPHY_EFATAL;
		}
		if (vbus_status  == 0x01) {
			chip->vooc_vbus_status = VOOC_VBUS_LOW;
		} else if ((vbus_status == 0x02) || (vbus_status == 0x03)) {
			chip->vooc_vbus_status = VOOC_VBUS_HIGH;
		} else {
			chip->vooc_vbus_status = VOOC_VBUS_NORMAL;
		}
	} else {
		vbus_vbatt = chip->vbus = chip->cp_vbus;

		if (chip->adapter_type == ADAPTER_SVOOC)
			vbus_vbatt = chip->vbus - chip->gauge_vbatt * 2;
		else
			vbus_vbatt = chip->vbus - chip->gauge_vbatt;

		if (vbus_vbatt < chip->voocphy_vbus_low) {
			chip->vooc_vbus_status = VOOC_VBUS_LOW;
		} else if (vbus_vbatt > chip->voocphy_vbus_high) {
			chip->vooc_vbus_status = VOOC_VBUS_HIGH;
		} else {
			chip->vooc_vbus_status = VOOC_VBUS_NORMAL;
		}

		if ((chip->vooc_vbus_status == VOOC_VBUS_NORMAL) &&
		    (chip->vbus < NORMAL_VBUS_MIN || chip->gauge_vbatt < NORMAL_VBAT_MIN)) {
			if (vbus_vbatt_detect_err_count < VBUS_VBATT_ERR_NUM) {
				chip->vooc_vbus_status = VOOC_VBUS_HIGH;
				++vbus_vbatt_detect_err_count;
			} else {
				vbus_vbatt_detect_err_count = 0;
				oplus_voocphy_set_status_and_notify_ap(chip,
						FAST_NOTIFY_ADAPTER_STATUS_ABNORMAL);
				status = VOOCPHY_EFATAL;
			}

			voocphy_err("vbus=%d, vbatt=%d, vooc_vbus_status=%d, err_count=%d",
				    chip->vbus, chip->gauge_vbatt, chip->vooc_vbus_status,
				    vbus_vbatt_detect_err_count);
			return status;
		}

		vbus_vbatt_detect_err_count = 0;
	}

	chip->vbus_vbatt = vbus_vbatt;
	voocphy_info( "!!vbus=%d, vbatt=%d, vbus_vbatt=%d, vooc_vbus_status=%d",
	              chip->vbus, chip->gauge_vbatt, vbus_vbatt, chip->vooc_vbus_status);

	return status;
}

static bool oplus_voocphy_get_pdsvooc_adapter_config(struct oplus_voocphy_manager *chip)
{
	if (!chip)
		return false;

	if (chip->ops && chip->ops->get_pd_svooc_config) {
		return chip->ops->get_pd_svooc_config(chip);
	} else {
		return false;
	}
}

static int oplus_voocphy_get_ichg(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return 0;
	}

	if (chip->ops && chip->ops->get_ichg) {
		return chip->ops->get_ichg(chip);
	}
	return 0;
}

static int oplus_voocphy_get_slave_ichg(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return 0;
	}

	if (chip->slave_ops && chip->slave_ops->get_ichg) {
		return chip->slave_ops->get_ichg(chip);
	}
	return 0;
}

static void oplus_voocphy_reset_slave_cp(struct oplus_voocphy_manager *chip)
{
	if (!chip)
		return;

	if (chip->slave_ops && chip->slave_ops->reset_voocphy) {
		chip->slave_ops->reset_voocphy(chip);
	} else {
		return;
	}
}

static int oplus_voocphy_slave_set_chg_enable(struct oplus_voocphy_manager *chip, bool enable)
{
	int rc = 0;

	if (!chip) {
		voocphy_info("%s chip null\n", __func__);
		return 0;
	}

	if (chip->slave_ops && chip->slave_ops->set_chg_enable) {
		rc = chip->slave_ops->set_chg_enable(chip, enable);
		if (rc < 0) {
			voocphy_info("oplus_voocphy_set_chg_enable fail, rc=%d.\n", rc);
			return rc;
		}
	}

	return rc;
}

static int oplus_voocphy_slave_get_chg_enable(struct oplus_voocphy_manager *chip, u8 *data)
{
	if (!chip) {
		voocphy_info("%s chip null\n", __func__);
		return 0;
	}

	if (chip->slave_ops && chip->slave_ops->get_chg_enable) {
		return chip->slave_ops->get_chg_enable(chip, data);
	}
	return 0;
}

static int oplus_voocphy_get_ichg_devation(struct oplus_voocphy_manager *chip)
{
	int cp_ibus_devation = 0;
	int main_cp_ibus = 0;
	int slave_cp_ibus = 0;

	if (!chip)
		return 0;

	main_cp_ibus = oplus_voocphy_get_ichg(chip);
	slave_cp_ibus = oplus_voocphy_get_slave_ichg(chip);

	if (main_cp_ibus >= slave_cp_ibus) {
		cp_ibus_devation = main_cp_ibus - slave_cp_ibus;
	} else {
		cp_ibus_devation = slave_cp_ibus - main_cp_ibus;
	}

	voocphy_info("cp ibus devation = %d\n", cp_ibus_devation);

	return cp_ibus_devation;
}

static bool oplus_voocphy_check_slave_cp_status(struct oplus_voocphy_manager *chip)
{
	int count = 0;
	int i;
	u8 slave_cp_status = 0;

	if (!chip)
		return false;

	if (chip->slave_ops && chip->slave_ops->get_cp_status) {
		for (i=0; i<3; i++) {
			oplus_voocphy_slave_get_chg_enable(chip, &slave_cp_status);
			if (oplus_voocphy_get_slave_ichg(chip) < 500 ||
			    chip->slave_ops->get_cp_status(chip) == 0 ||
			    oplus_voocphy_get_ichg_devation(chip) > chip->cp_ibus_devation) {
				count = count + 1;
			} else {
				count = 0;
			}
			msleep(10);
		}

		if (count >= 3) {
			voocphy_err("count >= 3, return false, slave cp is in trouble!\n");
			return false;
		} else {
			voocphy_err("count < 3, return true, slave cp is not in trouble!\n");
			return true;
		}
	} else {
		voocphy_err("slave cp don't exist, return false!\n");
		return false;
	}
}

static int oplus_voocphy_set_chg_auto_mode(struct oplus_voocphy_manager *chip,
					   bool enable)
{
	int rc = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return 0;
	}

	if (chip->ops && chip->ops->set_chg_auto_mode) {
		rc = chip->ops->set_chg_auto_mode(chip, enable);
		if (rc < 0) {
			voocphy_info("oplus_voocphy_set_chg_auto_mode fail, rc=%d.\n", rc);
			return rc;
		}
	}

	return rc;
}

void oplus_voocphy_get_chip(struct oplus_voocphy_manager **chip)
{
	*chip = g_voocphy_chip;
}

static int oplus_voocphy_handle_is_vbus_ok_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	chip->vbus_adjust_cnt++;
	if (chip->copycat_vooc_support == true)
		chip->adapter_is_vbus_ok_count++;

	if (chip->adapter_type == ADAPTER_VOOC20) {
		if (chip->vooc2_next_cmd != VOOC_CMD_IS_VUBS_OK) {
			status = oplus_voocphy_reset_voocphy(chip);
			if (chip->copycat_vooc_support == true)
				chip->copycat_vooc_adapter = true;
		} else {
			oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_MASK,
			                              &chip->voocphy_tx_buff[0], BIT_ACTIVE);
		}
	}

	/* detect vbus status */
	status = oplus_voocphy_vbus_vbatt_detect(chip);
	if (chip->vooc_vbus_status == VOOC_VBUS_HIGH) {
		/*vbus high*/
		oplus_voocphy_write_mesg_mask(TX0_DET_BIT4_MASK,
		                              &chip->voocphy_tx_buff[0], BIT_ACTIVE);
	} else if (chip->vooc_vbus_status == VOOC_VBUS_NORMAL) {
		/* vbus ok */
		oplus_voocphy_write_mesg_mask(TX0_DET_BIT4_MASK,
		                              &chip->voocphy_tx_buff[0], BIT_ACTIVE);
		oplus_voocphy_write_mesg_mask(TX0_DET_BIT5_MASK,
		                              &chip->voocphy_tx_buff[0], BIT_ACTIVE);
		oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_MASK,
		                              &chip->voocphy_tx_buff[0], BIT_SLEEP);
	} else {
		/* vbus low */
		oplus_voocphy_write_mesg_mask(TX0_DET_BIT5_MASK,
		                              &chip->voocphy_tx_buff[0], BIT_ACTIVE);
	}

	if (chip->vooc_vbus_status == VOOC_VBUS_NORMAL) { /*vbus-vbatt = ok*/
		oplus_voocphy_set_chg_enable(chip, true);
	}

	if (chip->vooc_vbus_status == VOOC_VBUS_NORMAL) {
		switch (chip->adapter_type) {
		case ADAPTER_VOOC20 :
			voocphy_info("ADAPTER_VOOC20\n");
			chip->vooc2_next_cmd = VOOC_CMD_ASK_CURRENT_LEVEL;
			oplus_voocphy_monitor_start(chip);
			break;
		case ADAPTER_VOOC30 :
		case ADAPTER_SVOOC :
			oplus_voocphy_monitor_start(chip);
			break;
		default:
			voocphy_info( " should not go to here");
			break;
		}
	}

	return status;
}

static void oplus_voocphy_set_pdsvooc_adapter_config(struct oplus_voocphy_manager *chip, bool enable)
{
	if (!chip)
		return;

	if (chip->ops && chip->ops->set_pd_svooc_config)
		chip->ops->set_pd_svooc_config(chip, enable);
}

static int oplus_voocphy_get_cp_vbat(struct oplus_voocphy_manager *chip)
{
	int cp_vbat = 0;
	u8 cp_adc_reg;

	if (!chip) {
		voocphy_err("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}

	oplus_voocphy_get_adc_enable(chip, &cp_adc_reg);
	if (cp_adc_reg == 0)
		oplus_voocphy_set_adc_enable(chip, true);

	if (chip->ops && chip->ops->get_cp_vbat) {
		cp_vbat = chip->ops->get_cp_vbat(chip);
	}

	return cp_vbat;
}

static void oplus_voocphy_choose_batt_sys_curve(struct oplus_voocphy_manager *chip)
{
	int sys_curve_temp_idx = 0;
	int idx = 0;
	int convert_ibus = 0;
	struct batt_sys_curves *batt_sys_curv_by_tmprange = NULL;
	struct batt_sys_curve *batt_sys_curve = NULL;

	if (!chip) {
		voocphy_info("%s chip null\n", __func__);
		return;
	}

	if (BATT_SOC_90_TO_100 != chip->batt_soc_plugin) {
		if (chip->batt_sys_curv_found == false) {
			/* step1: find sys curv by soc */
			if (chip->adapter_type == ADAPTER_SVOOC) {
				if (chip->batt_soc_plugin == BATT_SOC_0_TO_50) {
					chip->batt_sys_curv_by_soc = svooc_curves_soc0_2_50;
				} else if (chip->batt_soc_plugin == BATT_SOC_50_TO_75) {
					chip->batt_sys_curv_by_soc = svooc_curves_soc50_2_75;
				} else if (chip->batt_soc_plugin == BATT_SOC_75_TO_85) {
					chip->batt_sys_curv_by_soc = svooc_curves_soc75_2_85;
				} else if (chip->batt_soc_plugin == BATT_SOC_85_TO_90) {
					chip->batt_sys_curv_by_soc = svooc_curves_soc85_2_90;
				} else {
					/* do nothing */
				}
			} else {
				if (chip->batt_soc_plugin == BATT_SOC_0_TO_50) {
					chip->batt_sys_curv_by_soc = vooc_curves_soc0_2_50;
				} else if (chip->batt_soc_plugin == BATT_SOC_50_TO_75) {
					chip->batt_sys_curv_by_soc = vooc_curves_soc50_2_75;
				} else if (chip->batt_soc_plugin == BATT_SOC_75_TO_85) {
					chip->batt_sys_curv_by_soc = vooc_curves_soc75_2_85;
				} else if (chip->batt_soc_plugin == BATT_SOC_85_TO_90) {
					chip->batt_sys_curv_by_soc = vooc_curves_soc85_2_90;
				} else {
					/* do nothing */
				}
			}

			/* step2: find sys curv by temp range */
			sys_curve_temp_idx = chip->sys_curve_temp_idx;

			/*
			 * step3: note sys curv location by temp range,
			 * for example BATT_SYS_CURVE_TEMP160_TO_TEMP430
			 */
			chip->batt_sys_curv_by_tmprange = &(chip->batt_sys_curv_by_soc[sys_curve_temp_idx]);
			chip->cur_sys_curv_idx = 0;

			/* step4: note init current_expect and current_max */
			if (chip->external_gauge_support) {
				if (chip->parallel_charge_support) {
					chip->gauge_vbatt = chip->main_vbatt > chip->sub_vbatt ? chip->main_vbatt : chip->sub_vbatt;
				} else {
					chip->gauge_vbatt = oplus_chglib_gauge_pre_vbatt(chip->dev);
				}
			} else {
				chip->gauge_vbatt = oplus_voocphy_get_cp_vbat(chip);
			}

			batt_sys_curv_by_tmprange = chip->batt_sys_curv_by_tmprange;
			for (idx = 0; idx <= batt_sys_curv_by_tmprange->sys_curv_num - 1; idx++) {	//big -> small
				batt_sys_curve = &(batt_sys_curv_by_tmprange->batt_sys_curve[idx]);
				convert_ibus = batt_sys_curve->target_ibus * 100;
				voocphy_info("!batt_sys_curve[%d] gauge_vbatt,exit,target_vbat,cp_ichg,target_ibus:[%d, %d, %d, %d, %d]\n", idx,
				             chip->gauge_vbatt, batt_sys_curve->exit, batt_sys_curve->target_vbat, chip->cp_ichg, convert_ibus);
				if (batt_sys_curve->exit == false) {
					if (chip->gauge_vbatt <= batt_sys_curve->target_vbat) {
						chip->cur_sys_curv_idx = idx;
						chip->batt_sys_curv_found = true;
						voocphy_info("! found batt_sys_curve first idx[%d]\n", idx);
						break;
					}
				} else {
					/* exit fastchg */
					voocphy_info("! not found batt_sys_curve first\n");
				}
			}
			if (chip->batt_sys_curv_found) {
				voocphy_info("! found batt_sys_curve idx idx[%d]\n", idx);
				chip->current_expect = chip->batt_sys_curv_by_tmprange->batt_sys_curve[idx].target_ibus;
				chip->current_max = chip->current_expect;
				chip->batt_sys_curv_by_tmprange->batt_sys_curve[idx].chg_time = 0;
			} else {
				chip->current_expect = chip->batt_sys_curv_by_tmprange->batt_sys_curve[0].target_ibus;
				chip->current_max = chip->current_expect;
				chip->batt_sys_curv_by_tmprange->batt_sys_curve[0].chg_time = 0;
			}
		}
	} else {
		/* do nothing */
	}

	voocphy_info("current_max:%d current_expect:%d batt_soc_plugin:%d batt_temp_plugin:%d sys_curve_index:%d ibus:%d ",
	             chip->current_max, chip->current_expect, chip->batt_soc_plugin,
	             chip->batt_temp_plugin, sys_curve_temp_idx, chip->cp_ichg);
}

static int oplus_voocphy_handle_ask_current_level_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	if (chip->ask_current_first) {
		oplus_voocphy_choose_batt_sys_curve(chip);
		chip->ask_current_first = false;
	}

	if (chip->adapter_type == ADAPTER_VOOC20) {
		if (chip->vooc2_next_cmd != VOOC_CMD_ASK_CURRENT_LEVEL) {
			status = oplus_voocphy_reset_voocphy(chip);
		} else {
			 //if (chip->batt_soc <= VOOC_SOC_HIGH) { // soc_high is not
			 if (chip->voocphy_dual_cp_support) {	//10v3a design is not need to go here
				//set current 3.75A
				oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_MASK,
					&chip->voocphy_tx_buff[1], BIT_SLEEP); //bit 8
				oplus_voocphy_write_mesg_mask(TX1_DET_BIT1_MASK,
					&chip->voocphy_tx_buff[1], BIT_SLEEP); //bit 9
				oplus_voocphy_write_mesg_mask(TX0_DET_BIT7_MASK,
					&chip->voocphy_tx_buff[0], BIT_ACTIVE); //bit 7
			 } else {
				//do nothing, tx_buff keep default zero.
				//set current 3.0A
			}
			chip->vooc2_next_cmd = VOOC_CMD_GET_BATT_VOL; //vooc2_next_cmd;
		}
	} else { //svooc&vooc30
		oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_MASK,
		                              &chip->voocphy_tx_buff[0], (chip->current_expect >> 6) & 0x1);
		oplus_voocphy_write_mesg_mask(TX0_DET_BIT4_MASK,
		                              &chip->voocphy_tx_buff[0], (chip->current_expect >> 5) & 0x1);
		oplus_voocphy_write_mesg_mask(TX0_DET_BIT5_MASK,
		                              &chip->voocphy_tx_buff[0], (chip->current_expect >> 4) & 0x1);
		oplus_voocphy_write_mesg_mask(TX0_DET_BIT6_MASK,
		                              &chip->voocphy_tx_buff[0], (chip->current_expect >> 3) & 0x1);
		oplus_voocphy_write_mesg_mask(TX0_DET_BIT7_MASK,
		                              &chip->voocphy_tx_buff[0], (chip->current_expect >> 2) & 0x1);
		oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_MASK,
		                              &chip->voocphy_tx_buff[1], (chip->current_expect >> 1) & 0x1);
		oplus_voocphy_write_mesg_mask(TX1_DET_BIT1_MASK,
		                              &chip->voocphy_tx_buff[1], (chip->current_expect >> 0) & 0x1);

		chip->ap_need_change_current = 0; //clear need change flag
		chip->current_pwd = chip->current_expect * 100 + 300;
		chip->curr_pwd_count = 10;
		if (chip->adjust_curr == ADJ_CUR_STEP_REPLY_VBATT) {	//step1
			chip->adjust_curr = ADJ_CUR_STEP_SET_CURR_DONE;		//step2
		}
		voocphy_info( "!! set expect_current:%d", chip->current_expect);
	}

	return status;
}

static int oplus_voocphy_non_vooc20_handle_get_batt_vol_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;
	unsigned int vbatt = 0;
	unsigned char data_temp_l = 0, data_temp_h = 0;
	unsigned int vbatt1 = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	if (chip->btb_temp_over) {
		if (!chip->btb_err_first) { //1st->b'1100011:4394mv , 2nd->b'0001000:3484mv
			data_temp_l = 0x03; //bit 0 0011
			data_temp_h = 0x3; //bit 11
			chip->btb_err_first = true;
		} else {
			data_temp_l = 0x08; //bit 0 1000
			data_temp_h = 0x0; //bit 00
			chip->btb_err_first = false;
		}
	} else if (chip->ap_need_change_current && chip->adjust_fail_cnt <= 3) {
		data_temp_l = 0x1f; //bit 1 1111
		data_temp_h = 0x3; //bit 11
		chip->ap_need_change_current--;
		voocphy_cpufreq_update(chip, CPU_CHG_FREQ_STAT_UP);
		oplus_voocphy_pm_qos_update(400);
		trace_oplus_tp_sched_change_ux(1, task_cpu(current));
		trace_oplus_tp_sched_change_ux(0, task_cpu(current));
		chip->adjust_curr = ADJ_CUR_STEP_REPLY_VBATT;
		voocphy_info( " ap_need_change_current adjust_fail_cnt[%d]\n", chip->adjust_fail_cnt);
	} else {
		/* calculate R */
		if (oplus_voocphy_get_bidirect_cp_support(chip) == false) {
			if (chip->adapter_type == ADAPTER_SVOOC) {
				if (oplus_switching_support_parallel_chg()) {
					if (chip->impedance_calculation_newmethod) {
						vbatt1 = chip->cp_vac/2 - (signed)((chip->cp_vac - chip->cp_vbus) *
							 (chip->cp_ichg / chip->master_cp_ichg)) / 2 - (chip->cp_ichg * 75)/1000;
					} else {
						if (chip->main_batt_icharging > 500) {
							vbatt1 = (chip->cp_vbus/2) -
								 (signed)((chip->cp_ichg*(chip->cp_vsys - chip->main_vbatt))/chip->main_batt_icharging)/2 -
								 (chip->cp_ichg * 50)/1000;
						} else {
							vbatt1 = chip->cp_vbus/2;
						}
					}
				} else {
				vbatt1 = (chip->cp_vbus/2) - (signed)(chip->cp_vsys - chip->gauge_vbatt)/4 - (chip->cp_ichg * 75)/1000;
				}
			} else {
				vbatt1 = chip->gauge_vbatt;
			}
		} else {
			vbatt1 = chip->gauge_vbatt;
		}
		if (vbatt1 < VBATT_BASE_FOR_ADAPTER) {
			vbatt = 0;
		} else {
			vbatt = (vbatt1 - VBATT_BASE_FOR_ADAPTER) / VBATT_DIV_FOR_ADAPTER;
		}

		data_temp_l =  ((vbatt >> 6) & 0x1) | (((vbatt >> 5) & 0x1) << 1)
		               | (((vbatt >> 4) & 0x1) << 2) | (((vbatt >> 3) & 0x1) << 3)
		               | (((vbatt >> 2) & 0x1) << 4);
		data_temp_h = ((vbatt >> 1) & 0x1) | (((vbatt >> 0) & 0x1) << 1);
	}
	chip->vbat_calc = vbatt;

	voocphy_info( "gauge_vbatt=%d, vbatt=0x%0x, "
			"data_temp_l=0x%0x, data_temp_h=0x%0x cp:%d %d %d %d %d %d",
	              chip->gauge_vbatt, vbatt,data_temp_l, data_temp_h, chip->cp_vsys,
			chip->cp_ichg, chip->cp_vbus, vbatt1, chip->main_batt_icharging, chip->main_vbatt);

	oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
	                              &chip->voocphy_tx_buff[0], data_temp_l);
	oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
	                              &chip->voocphy_tx_buff[1], data_temp_h);
	return status;
}

static int oplus_voocphy_vooc20_handle_get_batt_vol_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;
	unsigned int vbatt = 0;
	unsigned char data_temp_l = 0, data_temp_h = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	if (chip->vooc2_next_cmd != VOOC_CMD_GET_BATT_VOL)
		status = oplus_voocphy_reset_voocphy(chip);

	oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_MASK,
	                              &chip->voocphy_tx_buff[0], chip->fastchg_allow); //need refresh bit3

	if (chip->batt_soc > VOOC_SOC_HIGH &&  !chip->ask_vol_again) {
		data_temp_l = chip->fastchg_allow | 0x0e;
		data_temp_h = 0x01;
		chip->ask_vol_again = true;
	} else if (chip->gauge_vbatt > 3391) {

		vbatt = (chip->gauge_vbatt - 3392) / 16;		//+40mv
		if(vbatt > 61)	 {	// b'111110,vbatt >= 4384mv
			vbatt = 61;
		}
		if (chip->force_2a && (!chip->force_2a_flag)
		    && (vbatt < FORCE2A_VBATT_REPORT)) {
			vbatt = FORCE2A_VBATT_REPORT;		//adapter act it as 4332mv
			chip->force_2a_flag = true;
		} else if (chip->force_3a && (!chip->force_3a_flag)
		           && (vbatt < FORCE3A_VBATT_REPORT)) {
			vbatt = FORCE3A_VBATT_REPORT;		//adapter act it as 4316mv
			chip->force_3a_flag = true;
		}
		data_temp_l = (((vbatt >> 5) & 0x1) << 1) | (((vbatt >> 4) & 0x1) << 2)
		              | (((vbatt >> 3) & 0x1) << 3) | (((vbatt >> 2) & 0x1) << 4)
		              | chip->fastchg_allow;
		data_temp_h = ((vbatt & 0x1) << 1) | ((vbatt >> 1) & 0x1);

		//reply adapter when btb_temp_over
		if (chip->btb_temp_over) {
			if (!chip->btb_err_first) {
				data_temp_l = chip->fastchg_allow | 0x1e;
				data_temp_h = 0x01;
				chip->btb_err_first = true;
			} else {
				data_temp_l = chip->fastchg_allow | 0x10;
				data_temp_h = 0x02;
				chip->btb_err_first = false;
			}
		}
	} else if (chip->gauge_vbatt < 3000) {
		data_temp_l = 0;
		voocphy_info( "vbatt too low at vooc2 mode");
	} else {
		data_temp_l |= chip->fastchg_allow;
	}
	chip->vbat_calc = vbatt;

	voocphy_info( "data_temp_l=0x%0x, data_temp_h=0x%0x",
	              data_temp_l, data_temp_h);
	oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
	                              &chip->voocphy_tx_buff[0], data_temp_l);
	oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
	                              &chip->voocphy_tx_buff[1], data_temp_h);

	chip->vooc2_next_cmd = VOOC_CMD_GET_BATT_VOL;

	return status;
}

static int oplus_voocphy_handle_get_batt_vol_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	switch (chip->adapter_type) {
	case ADAPTER_VOOC30 :
	case ADAPTER_SVOOC :
		status = oplus_voocphy_non_vooc20_handle_get_batt_vol_cmd(chip);
		break;
	case ADAPTER_VOOC20 :
		status = oplus_voocphy_vooc20_handle_get_batt_vol_cmd(chip);
		break;
	default :
		voocphy_info( " should not go to here");
		break;
	}

	return status;
}

static int oplus_voocphy_handle_null_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;
	unsigned char dchg_enable_status = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	chip->adapter_check_cmd_data_count++;
	oplus_voocphy_get_chg_enable(chip, &dchg_enable_status);
	if (dchg_enable_status != FAST_CHARGER_MOS_DISABLE &&
	    chip->pre_adapter_ask_cmd == VOOC_CMD_GET_BATT_VOL &&
	    chip->adapter_check_cmd_data_count < ADAPTER_CHECK_CMD_DATA_TIMES &&
	    chip->adjust_curr != ADJ_CUR_STEP_REPLY_VBATT) {
		voocphy_info("get error cmd data, try again adapter_check_cmd_data_count =%d",
			chip->adapter_check_cmd_data_count);
		return VOOCPHY_EUNSUPPORTED;
	} else {
		voocphy_info("get error cmd data, reset voocphy & fastchg rerun check"
			     " adapter_check_cmd_data_count = %d\n",
			     chip->adapter_check_cmd_data_count);
		oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ERR_COMMU);
	}

	return status;
}

static int oplus_voocphy_handle_tell_usb_bad_cmd(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	//status = oplus_voocphy_reset_voocphy();
	oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_BAD_CONNECTED);
	chip->usb_bad_connect = true;
	chip->adapter_check_ok = 0;
	chip->adapter_model_ver = 0;
	chip->ask_batt_sys = 0;
	chip->adapter_check_cmmu_count = 0;
	chip->r_state = 1;
	voocphy_info( " usb bad connect");

	return status;
}

static int oplus_voocphy_handle_ask_ap_status(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	//read vbus&vbatt need suspend charger

	return status;
}

static int oplus_voocphy_reply_adapter_mesg(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;
	bool cur_data_valid = true;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return VOOCPHY_EFATAL;
	}

	/* memset set tx_buffer head & tx_buffer data */
	status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
	                                       &chip->voocphy_tx_buff[0], 0);
	status |= oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
	                                        &chip->voocphy_tx_buff[1], 0);
	status = oplus_voocphy_write_mesg_mask(VOOC_INVERT_HEAD_MASK,
	                                       &chip->voocphy_tx_buff[0], chip->vooc_head);

	/* handle special status */
	if ((chip->fastchg_adapter_ask_cmd != VOOC_CMD_ADAPTER_CHECK_COMMU_PROCESS)
	    && (chip->fastchg_adapter_ask_cmd != VOOC_CMD_ASK_BATT_SYS_PROCESS)
	    && (chip->fastchg_adapter_ask_cmd != VOOC_CMD_TELL_MODEL_PROCESS)) {
		chip->fastchg_adapter_ask_cmd = chip->adapter_mesg;
	}
	voocphy_info("rx::0x%0x adapter_ask:0x%0x",chip->voocphy_rx_buff, chip->fastchg_adapter_ask_cmd);

	/* handle the adaper request mesg */
	switch (chip->fastchg_adapter_ask_cmd) {
	case VOOC_CMD_ASK_FASTCHG_ORNOT:
		status = oplus_voocphy_handle_ask_fastchg_ornot_cmd(chip);
		break;
	case VOOC_CMD_IDENTIFICATION:
		status = oplus_voocphy_handle_identification_cmd(chip);
		break;
	case VOOC_CMD_ADAPTER_CHECK_COMMU_PROCESS:
		status = oplus_voocphy_handle_adapter_check_commu_process_cmd(chip);
		break;
	case VOOC_CMD_TELL_MODEL:
		status = oplus_voocphy_handle_tell_model_cmd(chip);
		break;
	case VOOC_CMD_TELL_MODEL_PROCESS:
		status = oplus_voocphy_handle_tell_model_process_cmd(chip);
		break;
	case VOOC_CMD_ASK_BAT_MODEL:
		status = oplus_voocphy_handle_ask_bat_model_cmd(chip);
		break;
	case VOOC_CMD_ASK_BATT_SYS_PROCESS:
		status = oplus_voocphy_handle_ask_bat_model_process_cmd(chip);
		break;
	case VOOC_CMD_IS_VUBS_OK:
		status = oplus_voocphy_handle_is_vbus_ok_cmd(chip);
		break;
	case VOOC_CMD_ASK_CURRENT_LEVEL:
		status = oplus_voocphy_handle_ask_current_level_cmd(chip);
		break;
	case VOOC_CMD_GET_BATT_VOL:
		status = oplus_voocphy_handle_get_batt_vol_cmd(chip);
		break;
	case VOOC_CMD_NULL:
	case VOOC_CMD_RECEVICE_DATA_0E:
	case VOOC_CMD_RECEVICE_DATA_0F:
		status = oplus_voocphy_handle_null_cmd(chip);
		cur_data_valid = false;
		chip->rcv_date_err_num++;
		break;
	case VOOC_CMD_TELL_USB_BAD:
		status = oplus_voocphy_handle_tell_usb_bad_cmd(chip);
		break;
	case VOOC_CMD_ASK_AP_STATUS :
		status = oplus_voocphy_handle_ask_ap_status(chip);
		break;
	default:
		chip->rcv_date_err_num++;
		voocphy_info("cmd not support, default handle");
		break;
	}

	if (cur_data_valid)
		chip->adapter_check_cmd_data_count = 0;

	return status;
}

static void oplus_voocphy_convert_tx_bit0_to_bit9(struct oplus_voocphy_manager *chip)
{
	unsigned int i = 0;
	unsigned int src_data = 0, des_data = 0, temp_data = 0;

	if (!chip) {
		voocphy_info("%s chip null\n", __func__);
		return;
	}

	src_data = chip->voocphy_tx_buff[1] & TX1_DET_BIT0_TO_BIT1_MASK;
	src_data = (src_data << 8) | (chip->voocphy_tx_buff[0]);


	for (i = 0; i < 10; i++) {
		temp_data = (src_data >> i) & 0x1;
		des_data = des_data | (temp_data << (10 - i - 1));
	}

	chip->voocphy_tx_buff[1] = (des_data  >> 8) & TX1_DET_BIT0_TO_BIT1_MASK;
	chip->voocphy_tx_buff[0] = (des_data & 0xff);
}

static void oplus_voocphy_convert_tx_bit0_to_bit9_for_move_model(struct oplus_voocphy_manager *chip)
{
	unsigned int i = 0;
	unsigned int src_data = 0, des_data = 0, temp_data = 0;

	if (!chip) {
		voocphy_info("%s chip null\n", __func__);
		return;
	}

	src_data = chip->voocphy_tx_buff[1] & TX1_DET_BIT0_TO_BIT1_MASK;
	src_data = (src_data << 8) | (chip->voocphy_tx_buff[0]);


	for (i = 0; i < 10; i++) {
		temp_data = (src_data >> i) & 0x1;
		des_data = des_data | (temp_data << (10 - i - 1));
	}

	des_data = (des_data >> 1);
	chip->voocphy_tx_buff[1] = (des_data  >> 8) & TX1_DET_BIT0_TO_BIT1_MASK;
	chip->voocphy_tx_buff[0] = (des_data & 0xff);
}

static int oplus_voocphy_set_predata(struct oplus_voocphy_manager *chip, u16 val)
{
	int rc = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}

	if (chip->ops && chip->ops->set_predata) {
		oplus_voocphy_pm_qos_update(400);
		rc = chip->ops->set_predata(chip, val);
		if (rc < 0) {
			voocphy_info("oplus_voocphy_set_predata fail, rc=%d.\n", rc);
			return rc;
		}
	}

	return rc;
}

static int oplus_voocphy_set_is_vbus_ok_predata (struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		voocphy_info("%s, chip null\n", __func__);
		return VOOCPHY_EFATAL;
	}

	if (chip->adapter_type == ADAPTER_SVOOC) {
		if (chip->ask_bat_model_finished) {
			chip->ask_bat_model_finished = false;
			oplus_voocphy_set_predata(chip, OPLUS_IS_VUBS_OK_PREDATA_SVOOC);
		}
	} else if (chip->adapter_type == ADAPTER_VOOC30) {
		if (chip->ask_bat_model_finished) {
			chip->ask_bat_model_finished = false;
			oplus_voocphy_set_predata(chip, OPLUS_IS_VUBS_OK_PREDATA_VOOC30);
		}
	} else if (chip->adapter_type == ADAPTER_VOOC20) {
		oplus_voocphy_set_predata(chip, OPLUS_IS_VUBS_OK_PREDATA_VOOC20);
	} else {
		voocphy_info("adapter_type error !!!\n");
		return -EINVAL;
	}

	return 0;
}

static int oplus_voocphy_slave_init_vooc(struct oplus_voocphy_manager *chip)
{
	int rc = 0;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -EINVAL;
	}

	if (chip->slave_ops && chip->slave_ops->init_vooc) {
		rc = chip->slave_ops->init_vooc(chip);
	}

	return 0;
}

#define GET_VBAT_PREDATA_DEFAULT (0x64 << SC8547_DATA_H_SHIFT) | 0x02;
#define EXEC_TIME_THR	1500
static int oplus_voocphy_set_txbuff(struct oplus_voocphy_manager *chip)
{
	int rc = 0;
	u16 val = 0;
	ktime_t delta;
	unsigned long long duration;

	if (!chip) {
		voocphy_err("chip is null\n");
		return -1;
	}

	if (chip->ops && chip->ops->set_txbuff) {
		//write txbuff //0x2c
		val = (chip->voocphy_tx_buff[0] << VOOCPHY_DATA_H_SHIFT) | chip->voocphy_tx_buff[1];
		oplus_voocphy_pm_qos_update(400);
		rc = chip->ops->set_txbuff(chip, val);
		if (rc < 0) {
			voocphy_info("oplus_vooc_set_txbuff failed, rc=%d.\n", rc);
			return rc;
		}
	} else {
		voocphy_info("failed: chip->ops or chip->ops->set_txbuff null\n");
	}

	//end time
	rettime = ktime_get();
	delta = ktime_sub(rettime,calltime);
	duration = (unsigned long long) ktime_to_ns(delta) >> 10;
	if (duration >= EXEC_TIME_THR) {
		//note exception for systrace
		chip->irq_tx_timeout_num++;
		voocphy_dbg("dur time %lld usecs\n", duration);
	}

	voocphy_dbg("write txbuff 0x%0x usecs %lld\n", val, duration);
	//write predata
	if (chip->fastchg_adapter_ask_cmd == VOOC_CMD_GET_BATT_VOL) {
		rc = oplus_voocphy_set_predata(chip, val);
		if (rc < 0) {
			voocphy_info("oplus_vooc_set_predata fialed, rc=%d.\n", rc);
			return rc;
		}

		voocphy_dbg("write predata 0x%0x\n", val);

		//stop fastchg check
		chip->fastchg_err_commu = false;
		chip->fastchg_to_warm = false;
	}

	return 0;
}

static int oplus_voocphy_send_mesg_to_adapter(struct oplus_voocphy_manager *chip)
{
	int ret;
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("%s chip null\n", __func__);
		return VOOCPHY_EFATAL;
	}

	if (chip->adapter_check_cmmu_count) {
		chip->adapter_check_cmmu_count--;
	} else if (chip->adapter_model_count) {
		chip->adapter_model_count--;
	} else if (chip->ask_batt_sys) {
		chip->ask_batt_sys--;
		voocphy_info("ask_batt_sys %d\n", chip->ask_batt_sys);
	}

	voocphy_info("before convert tx_buff[0]=0x%0x, tx_buff[1]=0x%0x",
	             chip->voocphy_tx_buff[0], chip->voocphy_tx_buff[1]);

	/* convert the tx_buff */
	if (!chip->vooc_move_head) {
		oplus_voocphy_convert_tx_bit0_to_bit9(chip);
	} else {
		oplus_voocphy_convert_tx_bit0_to_bit9_for_move_model(chip);
	}
	chip->vooc_move_head = false;

	voocphy_info("tx_buff[0]=0x%0x, tx_buff[1]=0x%0x adapter_ask_cmd = 0x%0x adjust_curr = %d",
	             chip->voocphy_tx_buff[0], chip->voocphy_tx_buff[1], chip->fastchg_adapter_ask_cmd, chip->adjust_curr);

	/* send tx_buff 10 bits to adapter */
	ret = oplus_voocphy_set_txbuff(chip);
	/* deal error when fastchg adjust current write txbuff */
	if (VOOC_CMD_ASK_CURRENT_LEVEL == chip->fastchg_adapter_ask_cmd && ADJ_CUR_STEP_SET_CURR_DONE == chip->adjust_curr) {
		voocphy_info("adjust curr ADJ_CUR_STEP_SET_CURR_DONE\n");
	} else {
		if (VOOC_CMD_GET_BATT_VOL == chip->fastchg_adapter_ask_cmd && ADJ_CUR_STEP_DEFAULT == chip->adjust_curr) {
			voocphy_cpufreq_update(chip, CPU_CHG_FREQ_STAT_AUTO);
			voocphy_info("cpuboost_charge_event ADJ_CUR_STEP_DEFAULT\n");
			//trace_oplus_tp_sched_change_ux(1, task_cpu(current));
			//trace_oplus_tp_sched_change_ux(0, task_cpu(current));
		}
	}

	if (ret < 0)
		status =  VOOCPHY_EFATAL;
	else
		status =  VOOCPHY_SUCCESS;

	return status;
}

static int oplus_voocphy_adapter_commu_with_voocphy(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}

	/* get data from adapter */
	status = oplus_voocphy_get_mesg_from_adapter(chip);
	if (status != VOOCPHY_SUCCESS) {
		voocphy_info(" oplus_voocphy_get_mesg_from_adapter fail");
		goto error;
	}

	/* calculate mesg for voocphy need reply to adapter */
	status = oplus_voocphy_reply_adapter_mesg(chip);
	if (status != VOOCPHY_SUCCESS) {
		voocphy_info(" oplus_vooc_process_mesg fail");
		goto error;
	}

	/* send data to adapter */
	status = oplus_voocphy_send_mesg_to_adapter(chip);
	if (status != VOOCPHY_SUCCESS) {
		voocphy_info(" oplus_voocphy_send_mesg_to_adapter fail");
	}

error:
	return status;
}

static int oplus_voocphy_svooc_commu_with_voocphy(struct oplus_voocphy_manager *chip)
{
	oplus_voocphy_adapter_commu_with_voocphy(chip);

	if (oplus_voocphy_ap_allow_fastchg(chip)) {
		if (chip->fastchg_stage == OPLUS_FASTCHG_STAGE_2 && oplus_chglib_get_flash_led_status()) {
			chip->fastchg_need_reset = 1;
			voocphy_info("OPLUS_FASTCHG_STAGE_2 and open torch exit fastchg\n");
			return 1;
		} else {
			oplus_chglib_suspend_charger(true);
			voocphy_info("allow fastchg adapter type %d\n", chip->adapter_type);
		}

		/* handle timeout of adapter ask cmd 0x4 */
		oplus_voocphy_set_is_vbus_ok_predata(chip);
		if (chip->adapter_type == ADAPTER_SVOOC) {
			oplus_voocphy_hw_setting(chip, SETTING_REASON_SVOOC);
			if (chip->voocphy_dual_cp_support)
				oplus_voocphy_slave_hw_setting(chip, SETTING_REASON_SVOOC);
			/*
			 * TODO: to be fixed -- HL7138
			 * http://gerrit.scm.adc.com:8080/#/c/16657922/
			 */
			if (oplus_chglib_is_pd_svooc_adapter(chip->dev)) {
				voocphy_err("pd_svooc adapter, set config!\n");
				oplus_voocphy_set_pdsvooc_adapter_config(chip, true);
			}
		} else if (chip->adapter_type == ADAPTER_VOOC20 || chip->adapter_type == ADAPTER_VOOC30) {
			oplus_voocphy_hw_setting(chip, SETTING_REASON_VOOC);
			if (chip->voocphy_dual_cp_support)
				oplus_voocphy_slave_hw_setting(chip, SETTING_REASON_VOOC);
			/*
			 * TODO: to be fixed -- HL7138
			 * http://gerrit.scm.adc.com:8080/#/c/16657922/
			 */
		} else {
			voocphy_info("SETTING_REASON_DEFAULT\n");
		}
	}

	return 0;
}

static int oplus_voocphy_set_bidirection_predata (struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		voocphy_info("%s, chip null\n", __func__);
		return VOOCPHY_EFATAL;
	}

	if (chip->adapter_type == ADAPTER_SVOOC) {
		if (chip->ask_bat_model_finished) {
			chip->ask_bat_model_finished = false;
			oplus_voocphy_set_predata(chip, OPLUS_IS_VUBS_OK_PREDATA_SVOOC);
		}
	} else if (chip->adapter_type == ADAPTER_VOOC30) {
		oplus_voocphy_set_predata(chip, OPLUS_BIDIRECTION_PREDATA_VOOC30);
	} else if (chip->adapter_type == ADAPTER_VOOC20) {
		oplus_voocphy_set_predata(chip, OPLUS_BIDIRECTION_PREDATA_VOOC20);
	} else {
		voocphy_info("adapter_type error !!!\n");
		return -1;
	}

	return 0;
}

static int oplus_vooc_adapter_work_as_power_bank(struct oplus_voocphy_manager *chip)
{
	int ret;
	int status = VOOCPHY_SUCCESS;
	unsigned char vooc_head = 0;
	unsigned char vooc_move_head = 0;
	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return -ENODEV;
	}

	voocphy_info("!!!!![power_bank]rx_data:%d", chip->voocphy_rx_buff);
	vooc_head = (chip->voocphy_rx_buff & VOOC_HEAD_MASK) >> VOOC_HEAD_SHIFT;
	vooc_move_head = (chip->voocphy_rx_buff & VOOC_MOVE_HEAD_MASK)
					>> VOOC_MOVE_HEAD_SHIFT;
	if (vooc_head != VOOC2_HEAD && vooc_move_head != VOOC2_HEAD) {
		voocphy_info("!!!!![power_bank] vooc20 frame head error ");
		goto HEAD_ERR;
	} else {
		if (vooc_move_head == VOOC2_HEAD) {
			voocphy_info("!!!!![power_bank] vooc20 frame move head");
			chip->vooc_move_head = true;
		}

		if (chip->vooc_move_head == false) {
			status = oplus_voocphy_read_mesg_mask(VOOC_MESG_READ_MASK,
					chip->voocphy_rx_buff, &chip->adapter_mesg);
		} else {
			status = oplus_voocphy_read_mesg_mask(VOOC_MESG_MOVE_READ_MASK,
					chip->voocphy_rx_buff, &chip->adapter_mesg);
		}
		voocphy_info("!!!!![power_bank] adapter_mesg=0x%0x", chip->adapter_mesg);

		status = oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_TO_BIT7_MASK,
					&chip->voocphy_tx_buff[0], 0);
		status |= oplus_voocphy_write_mesg_mask(TX1_DET_BIT0_TO_BIT1_MASK,
					&chip->voocphy_tx_buff[1], 0);
		status |= oplus_voocphy_write_mesg_mask(VOOC_INVERT_HEAD_MASK,
					&chip->voocphy_tx_buff[0], chip->vooc_head);

		switch(chip->adapter_mesg) {
		case VOOC_CMD_ASK_FASTCHG_ORNOT :
			if (chip->vooc_move_head == false) {
				if (chip->fastchg_real_allow) {
					status |= oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_MASK,
                                                                                &chip->voocphy_tx_buff[0], 1);
				} else {
					status |= oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_MASK,
                                                                        &chip->voocphy_tx_buff[0], 0);
				}
			} else {
				status |= oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_MASK,
                                                                        &chip->voocphy_tx_buff[0], 0);
			}

			status |= oplus_voocphy_write_mesg_mask(TX0_DET_BIT7_MASK,
                                                                &chip->voocphy_tx_buff[0], 1);
			status |= oplus_voocphy_write_mesg_mask(TX1_DET_BIT1_MASK,
                                                                &chip->voocphy_tx_buff[1], 1);

			if (chip->vooc_move_head == false) {
				chip->adapter_model_ver = FASTCHG_CHARGER_TYPE_VOOC;
				if (chip->fastchg_real_allow) {
					chip->fastchg_allow = true;
					chip->fastchg_start = true;
					chip->fastchg_to_warm = false;
					chip->fastchg_dummy_start = false;
					oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_PRESENT);
					voocphy_info("!!!!!fastchg_real_allow = true");
					schedule_delayed_work(&chip->voocphy_send_ongoing_notify,
							      msecs_to_jiffies(SEND_FASTCHG_ONGOING_NOTIFY_INTERVAL));
				} else {
					chip->fastchg_allow = false;
					chip->fastchg_start = false;
					chip->fastchg_to_warm = false;
					chip->fastchg_dummy_start = true;
					oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_DUMMY_START);
					voocphy_info("!!!!!fastchg_dummy_start and reset voocphy");
				}
				chip->reply_bat_model_end = true;
			}
			break;
		case VOOC_CMD_ASK_POWER_BANK :
			status |= oplus_voocphy_write_mesg_mask(TX0_DET_BIT3_MASK,
                                                                &chip->voocphy_tx_buff[0], 1);
			break;
		default :
			voocphy_info("!!!!![power_bank] keep default reply");
			break;
		}
	}

	voocphy_info("!!!!![power_bank] b_tx_buff[0]=0x%0x, b_tx_buff[1]=0x%0x",
				chip->voocphy_tx_buff[0], chip->voocphy_tx_buff[1]);

	if (!chip->vooc_move_head) {
		oplus_voocphy_convert_tx_bit0_to_bit9(chip);
	} else {
		oplus_voocphy_convert_tx_bit0_to_bit9_for_move_model(chip);
	}
	chip->vooc_move_head = false;

	voocphy_info("!!!![power_bank] a_tx_buff[0]=0x%0x, a_tx_buff[1]=0x%0x",
				chip->voocphy_tx_buff[0], chip->voocphy_tx_buff[1]);

	ret = oplus_voocphy_set_txbuff(chip);
	if (ret < 0)
		status =  VOOCPHY_EFATAL;
	else
		status =  VOOCPHY_SUCCESS;

	oplus_voocphy_set_bidirection_predata(chip);

	if (chip->adapter_type == ADAPTER_VOOC20 || chip->adapter_type == ADAPTER_VOOC30) {
		oplus_voocphy_hw_setting(chip, SETTING_REASON_VOOC);
		if(chip->voocphy_dual_cp_support)
			oplus_voocphy_slave_hw_setting(chip, SETTING_REASON_VOOC);
	} else {
		voocphy_info("SETTING_REASON_DEFAULT\n");
	}
	return status;

HEAD_ERR:
	oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ERR_COMMU);
	return status;
}

static irqreturn_t oplus_voocphy_interrupt_handler(int irq, void *dev_id)
{
	struct oplus_voocphy_manager *chip = dev_id;
	int status = VOOCPHY_SUCCESS;
	int torch_exit_fastchg = 0;

	chip->irq_total_num++;

	/* for flash led */
	if (chip->fastchg_need_reset) {
		voocphy_info("fastchg_need_reset\n");
		chip->fastchg_need_reset = 0;
		oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_USER_EXIT_FASTCHG);
		goto handle_done;
	}

	oplus_voocphy_get_adapter_info(chip);

	if (VOOC_CMD_ASK_CURRENT_LEVEL == chip->pre_adapter_ask_cmd && ADJ_CUR_STEP_SET_CURR_DONE == chip->adjust_curr) {
		if (oplus_voocphy_get_write_txbuff_error(chip)) {
			chip->ap_need_change_current = 5;
			chip->adjust_fail_cnt++;
			voocphy_info("adjust curr wtxbuff fail %d times\n", chip->adjust_fail_cnt);
		} else {
			voocphy_cpufreq_update(chip, CPU_CHG_FREQ_STAT_AUTO);
			//trace_oplus_tp_sched_change_ux(1, task_cpu(current));
			//trace_oplus_tp_sched_change_ux(0, task_cpu(current));
			voocphy_info("adjust cpu to default");
			chip->adjust_fail_cnt = 0;
			chip->adjust_curr = ADJ_CUR_STEP_DEFAULT;
		}
	}

	/* do nothing */
	if (chip->vooc_flag & RXDATA_DONE_FLAG_MASK) {
		chip->irq_rxdone_num++;

		if (chip->irq_rxdone_num <= MAX_IGNORE &&
		    !chip->start_vaild_frame) {
			if (chip->voocphy_rx_buff == FIRST_FRAME) {
				voocphy_info("valid data of the first frame , irq_num = %d\n",
					     chip->irq_rxdone_num);
				chip->start_vaild_frame = true;
			} else {
				if (oplus_voocphy_get_bidirect_cp_support(chip))
					oplus_voocphy_int_disable_chg(chip);
				voocphy_err("ignore abnormal frame, rx_buff=0x%02x, irq_num=%d\n",
					chip->voocphy_rx_buff, chip->irq_rxdone_num);
				goto handle_done;
			}
		}

		if (!chip->start_vaild_frame &&
		    chip->irq_rxdone_num > MAX_IGNORE) {
			chip->fastchg_allow = false;
			chip->fastchg_start = false;
			chip->fastchg_to_warm = false;
			chip->fastchg_dummy_start = false;
			voocphy_info("invalid data of the first frame , irq_num = %d\n",
				     chip->irq_rxdone_num);
			oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ADAPTER_STATUS_ABNORMAL);
			goto handle_done;
		}

		/* feed soft monitor watchdog */
		if (!chip->fastchg_commu_stop) {
			status = oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_COMMU, VOOC_COMMU_EVENT_TIME);
			if (status != VOOCPHY_SUCCESS) {
				voocphy_info("stop commu timer fail");
			}
			status = oplus_voocphy_monitor_timer_start(chip, VOOC_THREAD_TIMER_COMMU, VOOC_COMMU_EVENT_TIME);
			if (status != VOOCPHY_SUCCESS) {
				voocphy_info("restart commu timer fail");
			}
			chip->fastchg_commu_ing = true;
		}
	}

	if (chip->vooc_flag & TXDATA_WR_FAIL_FLAG_MASK) {
		chip->ap_handle_timeout_num++;
		chip->irq_tx_fail_num++;
	}
	if (chip->vooc_flag == 0xF) {
		if (chip->start_vaild_frame && !chip->reply_bat_model_end) {
			oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ADAPTER_STATUS_ABNORMAL);
			voocphy_info("!RX_START_FLAG & TXDATA_WR_FAIL_FLAG occured, retry");
			goto handle_done;
		} else {
			voocphy_info("!RX_START_FLAG & TXDATA_WR_FAIL_FLAG occured do nothing");
		}
	} else if (chip->vooc_flag & RXDATA_DONE_FLAG_MASK) {	//rxdata recv done

		chip->irq_rcvok_num++;
		if (oplus_voocphy_get_bidirect_cp_support(chip) == false) {
			torch_exit_fastchg = oplus_voocphy_svooc_commu_with_voocphy(chip);
			if (chip->fastchg_adapter_ask_cmd == VOOC_CMD_IS_VUBS_OK &&
			    chip->copycat_vooc_support && chip->adapter_is_vbus_ok_count == 2 &&
			    chip->adapter_type == ADAPTER_SVOOC && (chip->cp_vbus < SVOOC_INIT_VBUS_VOL_LOW ||
			    chip->cp_vbus > SVOOC_INIT_VBUS_VOL_HIGH || chip->adapter_ask_fastchg_ornot_count == 1)) {
				voocphy_info("ADAPTER_COPYCAT,init cp_vbus = %d, ask_fastchg_ornot_count=%d\n",
						chip->cp_vbus, chip->adapter_ask_fastchg_ornot_count);
				oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ADAPTER_COPYCAT);
				goto handle_done;
			}

			if (chip->fastchg_adapter_ask_cmd == VOOC_CMD_ASK_CURRENT_LEVEL &&
			    chip->copycat_vooc_support && chip->adapter_type == ADAPTER_SVOOC &&
			    chip->adapter_is_vbus_ok_count == 1) {
				voocphy_info("ADAPTER_COPYCAT,init adapter_is_vbus_ok_count=%d vooc_vbus_status %d\n",
					chip->adapter_is_vbus_ok_count, chip->vooc_vbus_status);
				oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ADAPTER_COPYCAT);
				goto handle_done;
			}
		} else {
			switch (chip->adapter_type) {
			case ADAPTER_SVOOC:
				torch_exit_fastchg = oplus_voocphy_svooc_commu_with_voocphy(chip);
				break;
			case ADAPTER_VOOC20:
				status = oplus_vooc_adapter_work_as_power_bank(chip);
				break;
			default:
				voocphy_info("should not go here, reset voocphy");
				oplus_voocphy_reset_voocphy(chip);
				break;
			}
		}
		if(torch_exit_fastchg != 0)
			goto handle_done;
		chip->pre_adapter_ask_cmd = chip->fastchg_adapter_ask_cmd;
		oplus_voocphy_update_data(chip);
	}
	if (chip->vooc_flag & RXDATA_DONE_FLAG_MASK) {
		chip->irq_hw_timeout_num++;
	}

	if (oplus_voocphy_get_bidirect_cp_support(chip) == false) {
		if (chip->vooc_flag & RXDATA_DONE_FLAG_MASK) {
			/* move to sc8547_update_chg_data for improving time of interrupt */
		} else {
			chip->interrupt_flag = oplus_voocphy_get_int_value(chip);
		}
	} else {
		if(oplus_voocphy_int_disable_chg(chip)) {
			voocphy_err(", oplus_voocphy_int_disable_chg\n");
			oplus_chglib_disable_charger(true);
		}
	}

	voocphy_info("pre_adapter_ask: 0x%0x, adapter_ask_cmd: 0x%0x, "
	             "int_flag: 0x%0x vooc_flag: 0x%0x\n",
	             chip->pre_adapter_ask_cmd, chip->fastchg_adapter_ask_cmd,
	             chip->interrupt_flag, chip->vooc_flag);
	oplus_vooc_wake_voocphy_service_work(chip, VOOCPHY_REQUEST_UPDATE_DATA);

handle_done:
	oplus_voocphy_clear_interrupts(chip);
	return IRQ_HANDLED;
}

static void oplus_voocphy_reset_ibus_trouble_flag(void)
{
	slave_trouble_count = 0;
}

static bool oplus_voocphy_set_user_exit_fastchg(struct oplus_voocphy_manager *chip,
						unsigned char exit)
{
	if (chip) {
		voocphy_info("exit %d\n", exit);
		chip->user_exit_fastchg = exit;
	}

	return true;
}

static const char * const strategy_soc[] = {
	[BATT_SOC_0_TO_50]	= "strategy_soc_0_to_50",
	[BATT_SOC_50_TO_75]	= "strategy_soc_50_to_75",
	[BATT_SOC_75_TO_85]	= "strategy_soc_75_to_85",
	[BATT_SOC_85_TO_90]	= "strategy_soc_85_to_90",
};

static const char * const strategy_temp[] = {
	[BATT_SYS_CURVE_TEMP_LITTLE_COLD]	= "strategy_temp_little_cold",
	[BATT_SYS_CURVE_TEMP_COOL]	= "strategy_temp_cool",
	[BATT_SYS_CURVE_TEMP_LITTLE_COOL]	= "strategy_temp_little_cool",
	[BATT_SYS_CURVE_TEMP_NORMAL_LOW]	= "strategy_temp_normal_low",
	[BATT_SYS_CURVE_TEMP_NORMAL_HIGH] = "strategy_temp_normal_high",
	[BATT_SYS_CURVE_TEMP_WARM] = "strategy_temp_warm",
};

static void oplus_voocphy_parse_dt(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node * node = chip->dev->of_node;

	rc = of_property_read_u32(node, "ovp_reg",
				  &chip->ovp_reg);
	if (rc) {
		chip->ovp_reg = 0xE;
	} else {
		voocphy_err("ovp_reg is %d\n", chip->ovp_reg);
	}

	rc = of_property_read_u32(node, "ocp_reg",
	                          &chip->ocp_reg);
	if (rc) {
		chip->ocp_reg = 0x8;
	} else {
		voocphy_err("ocp_reg is %d\n", chip->ocp_reg);
	}

	rc = of_property_read_u32(node, "oplus_spec,voocphy_vbus_low",
	                          &chip->voocphy_vbus_low);
	if (rc) {
		chip->voocphy_vbus_low = DEFUALT_VBUS_LOW;
	}
	voocphy_info("voocphy_vbus_high is %d\n", chip->voocphy_vbus_low);

	rc = of_property_read_u32(node, "oplus_spec,voocphy_vbus_high",
	                          &chip->voocphy_vbus_high);
	if (rc) {
		chip->voocphy_vbus_high = DEFUALT_VBUS_HIGH;
	}
	voocphy_info("voocphy_vbus_high is %d\n", chip->voocphy_vbus_high);
}

static int oplus_voocphy_parse_svooc_batt_curves(struct oplus_voocphy_manager *chip)
{
	struct device_node *node, *svooc_node, *soc_node;
	int rc = 0, i, j, length;

	node = chip->dev->of_node;

	svooc_node = of_get_child_by_name(node, "svooc_charge_strategy");
	if (!svooc_node) {
		voocphy_info("Can not find svooc_charge_strategy node\n");
		return -EINVAL;
	}

	for (i = 0; i < BATT_SOC_90_TO_100; i++) {
		soc_node = of_get_child_by_name(svooc_node, strategy_soc[i]);
		if (!soc_node) {
			voocphy_info("Can not find %s node\n", strategy_soc[i]);
			return -EINVAL;
		}

		for (j = 0; j < BATT_SYS_CURVE_MAX; j++) {
			rc = of_property_count_elems_of_size(soc_node, strategy_temp[j], sizeof(u32));
			if (rc < 0) {
				if (j == BATT_SYS_CURVE_TEMP_WARM) {
					continue;
				} else {
					voocphy_info("Count %s failed, rc=%d\n", strategy_temp[j], rc);
					return rc;
				}
			}

			length = rc;

			switch(i) {
			case BATT_SOC_0_TO_50:
				rc = of_property_read_u32_array(soc_node, strategy_temp[j],
				                                (u32 *)svooc_curves_soc0_2_50[j].batt_sys_curve,
				                                length);
				svooc_curves_soc0_2_50[j].sys_curv_num = length/5;
				break;
			case BATT_SOC_50_TO_75:
				rc = of_property_read_u32_array(soc_node, strategy_temp[j],
				                                (u32 *)svooc_curves_soc50_2_75[j].batt_sys_curve,
				                                length);
				svooc_curves_soc50_2_75[j].sys_curv_num = length/5;
				break;
			case BATT_SOC_75_TO_85:
				rc = of_property_read_u32_array(soc_node, strategy_temp[j],
				                                (u32 *)svooc_curves_soc75_2_85[j].batt_sys_curve,
				                                length);
				svooc_curves_soc75_2_85[j].sys_curv_num = length/5;
				break;
			case BATT_SOC_85_TO_90:
				rc = of_property_read_u32_array(soc_node, strategy_temp[j],
				                                (u32 *)svooc_curves_soc85_2_90[j].batt_sys_curve,
				                                length);
				svooc_curves_soc85_2_90[j].sys_curv_num = length/5;
				break;
			default:
				break;
			}
		}
	}

	return rc;
}

static int oplus_voocphy_parse_vooc_batt_curves(struct oplus_voocphy_manager *chip)
{
	struct device_node *node, *vooc_node, *soc_node;
	int rc = 0, i, j, length;

	node = chip->dev->of_node;

	vooc_node = of_get_child_by_name(node, "vooc_charge_strategy");
	if (!vooc_node) {
		voocphy_info("Can not find vooc_charge_strategy node\n");
		return -EINVAL;
	}

	for (i = 0; i < BATT_SOC_90_TO_100; i++) {
		soc_node = of_get_child_by_name(vooc_node, strategy_soc[i]);
		if (!soc_node) {
			voocphy_info("Can not find %s node\n", strategy_soc[i]);
			return -EINVAL;
		}

		for (j = 0; j < BATT_SYS_CURVE_MAX; j++) {
			rc = of_property_count_elems_of_size(soc_node, strategy_temp[j], sizeof(u32));
			if (rc < 0) {
				if (j == BATT_SYS_CURVE_TEMP_WARM) {
					continue;
				} else {
					voocphy_info("Count %s failed, rc=%d\n", strategy_temp[j], rc);
					return rc;
				}
			}

			length = rc;

			switch(i) {
			case BATT_SOC_0_TO_50:
				rc = of_property_read_u32_array(soc_node, strategy_temp[j],
				                                (u32 *)vooc_curves_soc0_2_50[j].batt_sys_curve,
				                                length);
				vooc_curves_soc0_2_50[j].sys_curv_num = length/5;
				break;
			case BATT_SOC_50_TO_75:
				rc = of_property_read_u32_array(soc_node, strategy_temp[j],
				                                (u32 *)vooc_curves_soc50_2_75[j].batt_sys_curve,
				                                length);
				vooc_curves_soc50_2_75[j].sys_curv_num = length/5;
				break;
			case BATT_SOC_75_TO_85:
				rc = of_property_read_u32_array(soc_node, strategy_temp[j],
				                                (u32 *)vooc_curves_soc75_2_85[j].batt_sys_curve,
				                                length);
				vooc_curves_soc75_2_85[j].sys_curv_num = length/5;
				break;
			case BATT_SOC_85_TO_90:
				rc = of_property_read_u32_array(soc_node, strategy_temp[j],
				                                (u32 *)vooc_curves_soc85_2_90[j].batt_sys_curve,
				                                length);
				vooc_curves_soc85_2_90[j].sys_curv_num = length/5;
				break;
			default:
				break;
			}
		}
	}

	return 0;
}

static void oplus_voocphy_check_chg_out_work_func(struct oplus_voocphy_manager *chip)
{
	int chg_vol = 0;
	u8 cp_adc_reg;

	if (!chip) {
		voocphy_err("chip is null\n");
		return;
	}

	chip->fastchg_commu_ing = false;
	chg_vol = oplus_chglib_get_charger_voltage();
	if (chg_vol >= 0 && chg_vol < 2000) {
		if (chip->adapter_type == ADAPTER_VOOC20)
			cancel_delayed_work(&chip->voocphy_send_ongoing_notify);
		oplus_voocphy_reset_temp_range(chip);
		oplus_voocphy_reset_fastchg_after_usbout(chip);
		oplus_voocphy_get_adc_enable(chip, &cp_adc_reg);
		if (cp_adc_reg == 1)
			oplus_voocphy_set_adc_enable(chip, false);
		voocphy_info("chg_vol = %d %d\n", chg_vol, chip->fastchg_start);
		if (oplus_chglib_get_vooc_is_started(chip->dev))
			oplus_chglib_notify_ap(chip->dev, FAST_NOTIFY_ABSENT);
		if (chip->copycat_vooc_support)
			oplus_chglib_vooc_fastchg_disable(COPYCAT_ADAPTER, false);
	}

	voocphy_info("notify chg work chg_vol = %d\n", chg_vol);
}

static void oplus_voocphy_check_chg_out_work(struct work_struct *work)
{
	struct oplus_voocphy_manager *chip = container_of(work,
			struct oplus_voocphy_manager, check_chg_out_work.work);

	oplus_voocphy_check_chg_out_work_func(chip);
}

static void oplus_voocphy_wake_check_chg_out_work(struct oplus_voocphy_manager *chip,
						  unsigned int delay_ms)
{
	voocphy_info("check chg out after %d ms\n", delay_ms);
	schedule_delayed_work(&chip->check_chg_out_work,
			      round_jiffies_relative(msecs_to_jiffies(delay_ms)));
}

static void oplus_ap_voocphy_send_ongoing_notify(struct work_struct *work)
{
	struct oplus_voocphy_manager *chip = container_of(work,
		struct oplus_voocphy_manager, voocphy_send_ongoing_notify.work);
	int chg_vol = 0;

	if (!chip->fastchg_start) {
		chg_debug("fastchg_start is false\n");
		return;
	}

	chg_vol = oplus_chglib_get_charger_voltage();
	if ((chg_vol >= 0 && chg_vol < 2000)) {
		chg_debug("vbus does not exist\n");
		return;
	}

	if (oplus_chglib_get_vooc_sid_is_config(chip->dev))
		oplus_chglib_notify_ap(chip->dev, FAST_NOTIFY_ONGOING);
	schedule_delayed_work(&chip->voocphy_send_ongoing_notify,
		msecs_to_jiffies(SEND_FASTCHG_ONGOING_NOTIFY_INTERVAL));
}

static void oplus_voocphy_handle_present_notify(struct oplus_voocphy_manager *chip)
{
	chip->fastchg_to_warm = false;
	chip->fastchg_dummy_start = false;
	chip->fastchg_to_normal = false;
	chip->fastchg_to_warm_full = false;
	chip->fastchg_ing = false;
	chip->fast_chg_type = chip->adapter_model_ver;
	if (oplus_voocphy_get_bidirect_cp_support(chip)) {
		if (chip->fastchg_start == false) {
			chip->fastchg_start = true;
		}
	} else {
		chip->fastchg_start = true;
	}

	voocphy_info("fastchg start: [%d], adapter version: [0x%0x]\n",
		     chip->fastchg_start, chip->fast_chg_type);
	oplus_chglib_push_break_code(chip->dev, TRACK_CP_VOOCPHY_BREAK_DEFAULT);
}

static void oplus_voocphy_handle_dummy_notify(struct oplus_voocphy_manager *chip)
{
	chip->fastchg_start = false;
	chip->fastchg_to_warm = false;
	chip->fastchg_dummy_start = true;
	chip->fastchg_to_normal = false;
	chip->fastchg_to_warm_full = false;
	chip->fastchg_ing = false;
	chip->fast_chg_type = chip->adapter_model_ver;
	voocphy_info("fastchg dummy start: [%d], adapter version: [0x%0x]\n",
		     chip->fastchg_dummy_start, chip->fast_chg_type);
	oplus_chglib_push_break_code(chip->dev, TRACK_CP_VOOCPHY_BREAK_DEFAULT);
}

static void oplus_voocphy_handle_ongoing_notify(struct oplus_voocphy_manager *chip)
{
	chip->fastchg_ing = true;
	voocphy_info("voocphy fastchg_ing: [%d]\n", chip->fastchg_ing);
}

static void oplus_voocphy_handle_full_notify(struct oplus_voocphy_manager *chip)
{
	voocphy_info("%s\n", chip->fastchg_notify_status == FAST_NOTIFY_FULL ?
		     "FAST_NOTIFY_FULL" : "FAST_NOTIFY_BAD_CONNECTED");
	chip->fastchg_to_normal = true;
	chip->fastchg_real_allow = false;
	chip->fastchg_start = false;
	chip->fastchg_to_warm = false;
	chip->fastchg_to_warm_full = false;
	chip->fastchg_dummy_start = false;
	chip->fastchg_ing = false;

	if (chip->fastchg_notify_status == FAST_NOTIFY_FULL) {
		if (chip->vooc_temp_cur_range == FASTCHG_TEMP_RANGE_WARM) {
			chip->fastchg_to_normal = false;
			chip->fastchg_to_warm = true;
			chip->fastchg_to_warm_full = true;
			chg_debug("fastchg to warm full\n");
			chip->fastchg_check_stop = false;
		}
		oplus_chglib_push_break_code(chip->dev, TRACK_CP_VOOCPHY_FULL);
	} else {
		oplus_chglib_push_break_code(chip->dev,
					     TRACK_CP_VOOCPHY_BAD_CONNECTED);
	}

	voocphy_info("fastchg to normal: [%d]\n",
		     chip->fastchg_to_normal);
}

static void oplus_voocphy_handle_batt_temp_over_notify(struct oplus_voocphy_manager *chip)
{
	voocphy_info("FAST_NOTIFY_BATT_TEMP_OVER\n");
	chip->fastchg_to_warm = true;
	chip->fastchg_real_allow = false;
	chip->fastchg_start = false;
	chip->fastchg_dummy_start = true;
	chip->fastchg_to_normal = false;
	chip->fastchg_to_warm_full = false;
	chip->fastchg_ing = false;
	voocphy_info("fastchg to warm: [%d]\n",
		     chip->fastchg_to_warm);
	chip->fastchg_check_stop = false;
	oplus_voocphy_monitor_timer_start(chip,
					  VOOC_THREAD_TIMER_FASTCHG_CHECK,
					  VOOC_FASTCHG_CHECK_TIME);
	oplus_chglib_push_break_code(chip->dev,
				     TRACK_CP_VOOCPHY_BATT_TEMP_OVER);
}

static void oplus_voocphy_handle_err_commu_notify(struct oplus_voocphy_manager *chip)
{
	chip->fastchg_start = false;
	chip->fastchg_to_warm = false;
	chip->fastchg_dummy_start = false;
	chip->fastchg_to_normal = false;
	chip->fastchg_to_warm_full = false;
	chip->fastchg_ing = false;
	chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
	voocphy_info("fastchg err commu\n");
	oplus_chglib_push_break_code(chip->dev, TRACK_CP_VOOCPHY_FRAME_H_ERR);
}

static void oplus_voocphy_handle_user_exit_notify(struct oplus_voocphy_manager *chip)
{
	voocphy_info("FAST_NOTIFY_USER_EXIT_FASTCHG\n");
	oplus_voocphy_set_user_exit_fastchg(chip, true);
	chip->fastchg_start = false;
	chip->fastchg_dummy_start = true;
	chip->fastchg_to_normal = false;
	chip->fastchg_ing = false;
	chip->fastchg_to_warm_full = false;
	voocphy_info("user_exit_fastchg: [%d]\n",
		     chip->user_exit_fastchg);
	oplus_chglib_push_break_code(chip->dev,
				     TRACK_CP_VOOCPHY_USER_EXIT_FASTCHG);
}

static void oplus_voocphy_handle_switch_temp_range_notify(struct oplus_voocphy_manager *chip)
{
	voocphy_info("FAST_NOTIFY_SWITCH_TEMP_RANGE\n");
	chip->fastchg_to_warm = false;
	chip->fastchg_start = false;
	chip->fastchg_dummy_start = true;
	chip->fastchg_to_normal = false;
	chip->fastchg_to_warm_full = false;
	chip->fastchg_ing = false;
	/* check fastchg out and temp recover */
	chip->fastchg_check_stop = false;
	oplus_voocphy_monitor_timer_start(chip,
					  VOOC_THREAD_TIMER_FASTCHG_CHECK,
					  VOOC_FASTCHG_CHECK_TIME);
}

static void oplus_voocphy_handle_adapter_abnormal_notify(struct oplus_voocphy_manager *chip)
{
	voocphy_info("FAST_NOTIFY_ADAPTER_STATUS_ABNORMAL\n");
	chip->fastchg_start = false;
	chip->fastchg_to_warm = false;
	chip->fastchg_dummy_start = false;
	chip->fastchg_to_normal = false;
	chip->fastchg_to_warm_full = false;
	chip->fastchg_ing = false;
	voocphy_info("Adapter status is abnormal need reset:\n");
}

static void oplus_voocphy_handle_adapter_copycat_notify(struct oplus_voocphy_manager *chip)
{
	if (chip->copycat_vooc_support == true) {
		usleep_range(SVOOC_COPYCAT_VBUS_DOWN_DELAY_MS, SVOOC_COPYCAT_VBUS_DOWN_DELAY_MS);
		oplus_chglib_notify_ap(chip->dev, FAST_NOTIFY_ABSENT);
		oplus_chglib_vooc_fastchg_disable(COPYCAT_ADAPTER, true);
		oplus_voocphy_set_fastchg_state(chip, OPLUS_FASTCHG_STAGE_1);
	}
}

static void oplus_voocphy_notify_fastchg_work(struct work_struct *work)
{
	struct oplus_voocphy_manager *chip = container_of(work,
		struct oplus_voocphy_manager, notify_fastchg_work.work);
	int intval = 0;

	if (!chip) {
		voocphy_err("oplus_voocphy_manager is null\n");
		return;
	}
	intval = chip->fastchg_notify_status;

	voocphy_info("vooc status: [0x%x]\n", intval);
	switch (intval) {
	case FAST_NOTIFY_PRESENT:
		oplus_voocphy_handle_present_notify(chip);
		oplus_chglib_notify_ap(chip->dev, intval);
		break;
	case FAST_NOTIFY_DUMMY_START:
		oplus_voocphy_handle_dummy_notify(chip);
		oplus_chglib_notify_ap(chip->dev, FAST_NOTIFY_PRESENT);
		break;
	case FAST_NOTIFY_ONGOING:
		oplus_voocphy_handle_ongoing_notify(chip);
		if (oplus_chglib_get_vooc_sid_is_config(chip->dev))
			oplus_chglib_notify_ap(chip->dev, intval);
		break;
	case FAST_NOTIFY_FULL:
	case FAST_NOTIFY_BAD_CONNECTED:
		oplus_voocphy_handle_full_notify(chip);
		oplus_chglib_notify_ap(chip->dev, intval);
		oplus_voocphy_wake_check_chg_out_work(chip, 3000);
		break;
	case FAST_NOTIFY_BATT_TEMP_OVER:
		oplus_voocphy_handle_batt_temp_over_notify(chip);
		oplus_chglib_notify_ap(chip->dev, intval);
		oplus_voocphy_wake_check_chg_out_work(chip, 3000);
		break;
	case FAST_NOTIFY_ERR_COMMU:
		oplus_voocphy_handle_err_commu_notify(chip);
		oplus_chglib_notify_ap(chip->dev, intval);
		break;
	case FAST_NOTIFY_USER_EXIT_FASTCHG:
		oplus_voocphy_handle_user_exit_notify(chip);
		oplus_chglib_notify_ap(chip->dev, intval);
		oplus_voocphy_wake_check_chg_out_work(chip, 3000);
		break;
	case FAST_NOTIFY_BTB_TEMP_OVER:
		voocphy_info("FAST_NOTIFY_BTB_TEMP_OVER: [%d]\n", intval);
		oplus_chglib_notify_ap(chip->dev, intval);
		oplus_chglib_push_break_code(chip->dev,
					     TRACK_CP_VOOCPHY_BTB_TEMP_OVER);
		break;
	case FAST_NOTIFY_SWITCH_TEMP_RANGE:
		oplus_voocphy_handle_switch_temp_range_notify(chip);
		oplus_voocphy_wake_check_chg_out_work(chip, 3000);
		break;
	case FAST_NOTIFY_ADAPTER_STATUS_ABNORMAL:
		oplus_voocphy_handle_adapter_abnormal_notify(chip);
		break;
	case FAST_NOTIFY_ADAPTER_COPYCAT:
		oplus_voocphy_handle_adapter_copycat_notify(chip);
		break;
	case FAST_NOTIFY_CHECK_FASTCHG_REAL_ALLOW:
		chip->fastchg_real_allow = oplus_voocphy_check_fastchg_real_allow(chip);
		break;
	default:
		oplus_chglib_push_break_code(chip->dev,
					     TRACK_CP_VOOCPHY_FAST_ABSENT);
		voocphy_info("non handle status: [%d]\n", intval);
		break;
	}
}

static void oplus_voocphy_wake_notify_fastchg_work(struct oplus_voocphy_manager *chip)
{
	schedule_delayed_work(&chip->notify_fastchg_work, 0);
}

static void oplus_voocphy_handle_dummy_status(struct oplus_voocphy_manager *chip)
{
	voocphy_info("FAST_NOTIFY_DUMMY_START or FAST_NOTIFY_ADAPTER_STATUS_ABNORMAL:%d",
		     chip->fastchg_notify_status);
	/* reset vooc_phy */
	oplus_voocphy_reset_voocphy(chip);
	/* stop voocphy commu with adapter time */
	oplus_voocphy_commu_process_handle(chip->dev, false);
	voocphy_cpufreq_update(chip, CPU_CHG_FREQ_STAT_AUTO);
}

static void oplus_voocphy_common_handle(struct oplus_voocphy_manager *chip)
{
	chip->ap_disconn_issue = chip->fastchg_notify_status;
	oplus_voocphy_monitor_timer_start(chip, VOOC_THREAD_TIMER_DISCON, VOOC_DISCON_EVENT_TIME);
	if ((chip->fastchg_notify_status == FAST_NOTIFY_FULL &&
	    chip->vooc_temp_cur_range != FASTCHG_TEMP_RANGE_WARM) ||
	    chip->fastchg_notify_status == FAST_NOTIFY_BAD_CONNECTED ||
	    chip->fastchg_notify_status == FAST_NOTIFY_USER_EXIT_FASTCHG) {
		oplus_voocphy_reset_temp_range(chip);
	};

	/* stop monitor thread */
	oplus_voocphy_monitor_stop(chip);
	if (chip->fastchg_notify_status == FAST_NOTIFY_BATT_TEMP_OVER) {
		/* start fastchg_check timer */
		chip->fastchg_to_warm = true;
	} else if (chip->fastchg_notify_status == FAST_NOTIFY_USER_EXIT_FASTCHG) {
		oplus_voocphy_set_user_exit_fastchg(chip, true);
		chip->fastchg_dummy_start = true;
	} else if (chip->fastchg_notify_status == FAST_NOTIFY_FULL
		   || chip->fastchg_notify_status == FAST_NOTIFY_BAD_CONNECTED) {
		chip->fastchg_to_normal = true;
	}

	/* reset voocphy */
	oplus_voocphy_reset_voocphy(chip);
	/* stop voocphy commu with adapter time */
	oplus_voocphy_commu_process_handle(chip->dev, false);
}

static void oplus_voocphy_handle_err_commu_status(struct oplus_voocphy_manager *chip)
{
	unsigned char dchg_enable_status = 0;

	chip->ap_disconn_issue = chip->fastchg_notify_status;
	oplus_voocphy_reset_temp_range(chip);
	/*
         * if dchg enable, then reset_voocphy will create pluginout, so wo need
	 * not run 5s fastchg check, if not we need run the 5s thread to recovery
	 * the vooc
	 */
	oplus_voocphy_get_chg_enable(chip, &dchg_enable_status);
	if (chip->fastchg_err_commu == false && ((chip->adapter_type == ADAPTER_SVOOC && dchg_enable_status == 0)
	    ||(chip->adapter_type == ADAPTER_VOOC20 && chip->fastchg_allow))) {
		voocphy_info("!VOOC_THREAD_TIMER_FASTCHG_CHECK abnormal status, should run fastchg check");
		chip->fastchg_err_commu = true;
		oplus_voocphy_reset_fastchg_after_usbout(chip);
		chip->fastchg_start = false;
	} else {
		chip->fastchg_start = false;
		oplus_voocphy_reset_fastchg_after_usbout(chip);
	}

	/* reset voocphy */
	oplus_voocphy_reset_voocphy(chip);
	/* stop voocphy commu with adapter time */
	oplus_voocphy_commu_process_handle(chip->dev, false);
	/* stop monitor thread */
	oplus_voocphy_monitor_stop(chip);
}

static void oplus_voocphy_handle_other_error_status(struct oplus_voocphy_manager *chip)
{
	/*handle other error status*/
	chip->fastchg_start = false;
	oplus_voocphy_reset_fastchg_after_usbout(chip);
	/* reset voocphy */
	oplus_voocphy_reset_voocphy(chip);
	/* stop voocphy commu with adapter time */
	oplus_voocphy_commu_process_handle(chip->dev, false);
	/* stop monitor thread */
	oplus_voocphy_monitor_stop(chip);
}

static void oplus_voocphy_handle_btb_temp_over_status(struct oplus_voocphy_manager *chip)
{
	chip->ap_disconn_issue = chip->fastchg_notify_status;
	chip->fastchg_to_normal = true;
	oplus_voocphy_reset_temp_range(chip);
	/* stop monitor thread */
	oplus_voocphy_monitor_stop(chip);
	/* reset voocphy */
	oplus_voocphy_reset_voocphy(chip);
	/* stop voocphy commu with adapter time */
	oplus_voocphy_commu_process_handle(chip->dev, false);
	voocphy_err("FAST_NOTIFY_BTB_TEMP_OVER");
}

static void oplus_voocphy_handle_adapter_copycat_status(struct oplus_voocphy_manager *chip,
							int fastchg_notify_status)
{
	chip->fastchg_start = false;
	oplus_voocphy_reset_fastchg_after_usbout(chip);
	/* reset voocphy */
	oplus_voocphy_reset_voocphy(chip);
	/* stop voocphy commu with adapter time */
	oplus_voocphy_commu_process_handle(chip->dev, false);
	/* stop monitor thread */
	oplus_voocphy_monitor_stop(chip);
	if (chip->copycat_vooc_support == true) {
		chip->fastchg_notify_status = fastchg_notify_status;
		oplus_voocphy_set_fastchg_state(chip, OPLUS_FASTCHG_STAGE_2);
	}
	voocphy_err("FAST_NOTIFY_ADAPTER_COPYCAT");
}

static void oplus_voocphy_set_status_and_notify_ap(struct oplus_voocphy_manager *chip,
        int fastchg_notify_status)
{
	if (!chip) {
		voocphy_err("oplus_voocphy_manager is null\n");
		return;
	}
	voocphy_info( "monitor fastchg_notify: 0x%x", fastchg_notify_status);

	switch (fastchg_notify_status) {
	case FAST_NOTIFY_PRESENT:
		chip->fastchg_notify_status = fastchg_notify_status;
		break;
	case FAST_NOTIFY_DUMMY_START:
	case FAST_NOTIFY_ADAPTER_STATUS_ABNORMAL:
		chip->fastchg_notify_status = fastchg_notify_status;
		oplus_voocphy_handle_dummy_status(chip);
		break;
	case FAST_NOTIFY_ONGOING:
		chip->fastchg_notify_status = fastchg_notify_status;
		break;
	case FAST_NOTIFY_FULL:
	case FAST_NOTIFY_BATT_TEMP_OVER:
	case FAST_NOTIFY_BAD_CONNECTED:
	case FAST_NOTIFY_USER_EXIT_FASTCHG:
	case FAST_NOTIFY_SWITCH_TEMP_RANGE:
		chip->fastchg_notify_status = fastchg_notify_status;
		oplus_voocphy_common_handle(chip);
		break;
	case FAST_NOTIFY_ERR_COMMU:
		chip->fastchg_notify_status = fastchg_notify_status;
		oplus_voocphy_handle_err_commu_status(chip);
		break;
	case FAST_NOTIFY_BTB_TEMP_OVER:
		chip->fastchg_notify_status = fastchg_notify_status;
		oplus_voocphy_handle_btb_temp_over_status(chip);
		break;
	case FAST_NOTIFY_ADAPTER_COPYCAT:
		oplus_voocphy_handle_adapter_copycat_status(chip, fastchg_notify_status);
		break;
	case FAST_NOTIFY_CHECK_FASTCHG_REAL_ALLOW:
		chip->fastchg_notify_status = fastchg_notify_status;
		break;
	default:
		oplus_voocphy_handle_other_error_status(chip);
		break;
	}
	oplus_voocphy_wake_notify_fastchg_work(chip);
}

unsigned char oplus_voocphy_set_fastchg_current(struct oplus_voocphy_manager *chip)
{
	unsigned char current_expect_temp = chip->current_expect;

	voocphy_info( "max_curr[%d] ap_curr[%d] temp_curr[%d]",
	              chip->current_max, chip->current_ap, chip->current_batt_temp);

	chip->current_expect = chip->current_max > chip->current_ap ? chip->current_ap : chip->current_max;

	chip->current_expect = chip->current_expect > chip->current_batt_temp ? chip->current_batt_temp : chip->current_expect;

	if (current_expect_temp != chip->current_expect) {
		voocphy_info( "current_expect[%d --> %d]",
		              current_expect_temp, chip->current_expect);
		if (chip->parallel_charge_support) {
			if (current_expect_temp <= chip->current_expect) {
				chip->parallel_change_current_count = 0;
				chip->parallel_switch_current = oplus_voocphy_parallel_curve_convert(chip->current_expect);
				voocphy_info("current up,parallel_switch_current:%d", chip->parallel_switch_current);
#ifdef OPLUS_CHG_UNDEF /* TODO */
				oplus_switching_set_current(chip->parallel_switch_current);
#endif
			} else {
				chip->parallel_change_current_count = 8;
				chip->parallel_switch_current = oplus_voocphy_parallel_curve_convert(chip->current_expect);
				voocphy_info("current down waiting :%d,parallel_switch_current:%d",
					     chip->parallel_switch_current, chip->parallel_change_current_count);
			}
		}
		return 5;
	}
	voocphy_err("set current expect = %d\n", chip->current_expect);

	return 0;
}

int oplus_voocphy_get_ap_current(struct oplus_voocphy_manager *chip)
{
	unsigned char ap_request_current = 0;
	unsigned char screenoff_current = 0;
	unsigned char smartchg_current = 0;
	int svooc_current_factor = 1;
	bool led_on = oplus_chglib_get_led_on(chip->dev);
	int cool_down = chip->cool_down;

	if (!led_on)
		screenoff_current = chip->screenoff_current / 100;

	voocphy_info("%s: led_on = %d, screenoff_cur = %d, cool_down = %d\n",
		     __func__, led_on, chip->screenoff_current, cool_down);

	if (oplus_gauge_get_batt_num() == 1) {
		svooc_current_factor = 2;
	}

	if (chip->adapter_type == ADAPTER_SVOOC) {
		if (cool_down > chip->svooc_cool_down_num -1 || cool_down < 0)
			cool_down =  0;

		smartchg_current = chip->svooc_cool_down_current_limit[cool_down];
		smartchg_current /= svooc_current_factor;

		if (screenoff_current == 0 || screenoff_current > chip->svooc_cool_down_current_limit[0]/svooc_current_factor) {
			screenoff_current = chip->svooc_cool_down_current_limit[0]/svooc_current_factor;
		}

	} else if (chip->adapter_type == ADAPTER_VOOC20
		   || chip->adapter_type == ADAPTER_VOOC30) {
		if (cool_down > chip->vooc_cool_down_num -1 || cool_down < 0)
			cool_down =  0;
		smartchg_current = chip->vooc_cool_down_current_limit[cool_down];

		if (screenoff_current == 0 || screenoff_current > chip->vooc_cool_down_current_limit[0]) {
			screenoff_current = chip->vooc_cool_down_current_limit[0];
		}
	}

	ap_request_current = smartchg_current > screenoff_current ? screenoff_current : smartchg_current;
	voocphy_info("ap_current = %u screenoff_current=%u smartchg_current=%u  led_on =%d adapter_type =%d\n",
		      ap_request_current, screenoff_current, smartchg_current, led_on, chip->adapter_type);

	return ap_request_current;
}

static int oplus_voocphy_ap_event_handle(struct device *dev, unsigned long data)
{
	int status = VOOCPHY_SUCCESS;
	static bool ignore_first_monitor = true;
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	if (chip->fastchg_monitor_stop == true) {
		voocphy_info( "oplus_voocphy_ap_event_handle ignore");
		return status;
	}
	if ((chip->fastchg_notify_status & 0XFF) == FAST_NOTIFY_PRESENT) {
		ignore_first_monitor = true;
	}

	oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ONGOING);
	chip->current_ap = oplus_voocphy_get_ap_current(chip);
	voocphy_info( "ignore_first_monitor %d %d\n", ignore_first_monitor, chip->current_ap);
	if (!chip->btb_temp_over) {
		/* btb temp is normal */
		if ((!chip->ap_need_change_current)
		    && (ignore_first_monitor == false)) {
			chip->ap_need_change_current
			    = oplus_voocphy_set_fastchg_current(chip);
			voocphy_info( "ap_need_change_current %d\n", chip->ap_need_change_current);
		} else {
			ignore_first_monitor = false;
		}

		status = oplus_voocphy_monitor_timer_start(chip, VOOC_THREAD_TIMER_AP, VOOC_AP_EVENT_TIME);
	}

	return status;
}

static int oplus_voocphy_curr_event_handle(struct device *dev, unsigned long data)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	int status = VOOCPHY_SUCCESS;
	int vbat_temp_cur = 0;
	static unsigned char curr_over_count = 0;
	static unsigned char chg_curr_over_count = 0;
	static unsigned char term_curr_over_count = 0;
	static unsigned char sub_term_curr_over_count = 0;
	static unsigned char curr_curve_pwd_count = 10;
	u8 slave_cp_enable;
	bool pd_svooc_status;
	int chg_temp = oplus_chglib_get_shell_temp(chip->dev);
	int i;
	bool low_curr = false;
	int temp_current = 0;
	int temp_vbatt = 0;

	if (chip->fastchg_monitor_stop == true) {
		voocphy_info( "oplus_voocphy_curr_event_handle ignore");
		return status;
	}

	if ((chip->fastchg_notify_status & 0XFF) == FAST_NOTIFY_PRESENT) {
		curr_over_count = 0;
		chg_curr_over_count = 0;
		term_curr_over_count = 0;
		curr_curve_pwd_count = 10;
	}

	vbat_temp_cur = chip->icharging;

	if (chip->voocphy_dual_cp_support) {
		oplus_voocphy_slave_get_chg_enable(chip, &slave_cp_enable);
		voocphy_info("slave_status = %d\n", slave_cp_enable);
		if (VOOC_VBUS_NORMAL == chip->vooc_vbus_status
		    && chip->cp_ichg > chip->slave_cp_enable_thr
		    && slave_cp_enable == 0
		    && slave_trouble_count <= 1
		    && (chip->adapter_type == ADAPTER_SVOOC
		        || chip->adapter_type == ADAPTER_VOOC20
		        ||chip->adapter_type == ADAPTER_VOOC30)) {
			if (chip->adapter_type == ADAPTER_SVOOC && slave_trouble_count == 1) {
				oplus_voocphy_slave_hw_setting(chip, SETTING_REASON_SVOOC);
			} else if ((chip->adapter_type == ADAPTER_VOOC20 ||
				   chip->adapter_type == ADAPTER_VOOC30) && slave_trouble_count == 1) {
				oplus_voocphy_slave_hw_setting(chip, SETTING_REASON_VOOC);
			}
			voocphy_info("chip->cp_vbus = %d chip->cp_vbat = %d!!!!!\n", chip->cp_vbus, chip->cp_vbat);
			oplus_voocphy_slave_set_chg_enable(chip, true);
			msleep(200);
			if (oplus_voocphy_check_slave_cp_status(chip)) {
				voocphy_err("slave cp is normal, then set current!\n");
			} else {
				slave_trouble_count = slave_trouble_count + 1;
				voocphy_err("slave cp is abnormal, disable it!\n");
				oplus_voocphy_slave_set_chg_enable(chip, false);
				oplus_voocphy_reset_slave_cp(chip);
			}
		}

		if (slave_trouble_count == 1) {
			if (chip->current_max > 30) {
				chip->current_max = 30;
				if (!chip->ap_need_change_current)
					chip->ap_need_change_current = oplus_voocphy_set_fastchg_current(chip);
			}
		}

		if (chip->cp_ichg < chip->slave_cp_disable_thr_high
		    && slave_cp_enable == 1
		    && (chip->adapter_type == ADAPTER_SVOOC
		        || chip->adapter_type == ADAPTER_VOOC20
		        ||chip->adapter_type == ADAPTER_VOOC30)) {
			if(disable_sub_cp_count == 3) {
				voocphy_err("Ibus < 1.5A 3 times, disable sub cp!\n");
				oplus_voocphy_slave_set_chg_enable(chip, false);
				disable_sub_cp_count = 0;
			} else {
				disable_sub_cp_count = disable_sub_cp_count + 1;
				voocphy_err("Ibus < 1.5A count = %d\n", disable_sub_cp_count);
			}
		} else {
			disable_sub_cp_count = 0;
			voocphy_err("Discontinuity satisfies the condition, count = 0!\n");
		}
	}

	voocphy_info("[oplus_voocphy_curr_event_handle] chg_temp: %d, current: %d, vbatt: %d btb: %d current_pwd[%d %d]",
	              chg_temp, vbat_temp_cur, chip->gauge_vbatt, chip->btb_temp_over,
	              chip->curr_pwd_count, chip->current_pwd);

	/*
	 * TODO: to be fixed -- HL7138
	 * http://gerrit.scm.adc.com:8080/#/c/16657922/
	 */
	if (oplus_chglib_is_pd_svooc_adapter(chip->dev)) {
		pd_svooc_status = oplus_voocphy_get_pdsvooc_adapter_config(chip);
		voocphy_err("pd_svooc_status = %d, chip->cp_ichg = %d\n", pd_svooc_status, chip->cp_ichg);
		if (pd_svooc_status == true && chip->cp_ichg > 500) {
			voocphy_err("IBUS > 500mA set 0x5 to 0x28!\n");
			oplus_voocphy_set_pdsvooc_adapter_config(chip, false);
		}
	}

	if (!chip->btb_temp_over) {//non btb temp over
		if (vbat_temp_cur < -2000) { 	//BATT OUPUT 2000MA
			curr_over_count++;
			if (curr_over_count > 3) {
				voocphy_info("vcurr low than -2000mA\n");
				oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ABSENT);
			}
		} else {
			curr_over_count = 0;
		}

		if (vbat_temp_cur > chip->voocphy_ibat_over_current) {
			chg_curr_over_count++;
			if (chg_curr_over_count > 7) {
				oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_BAD_CONNECTED);
			}
		} else {
			chg_curr_over_count = 0;
		}
		if (chip->parallel_charge_support) {
			temp_current = chip->main_batt_icharging;
			temp_vbatt = chip->main_vbatt;
			voocphy_info("main range lowcurr ntime fastchg full %d, %d, %d\n", temp_vbatt, temp_current, chg_temp);
		} else {
			temp_current = vbat_temp_cur;
			temp_vbatt = chip->gauge_vbatt;
		}
		if (chg_temp >= chip->low_curr_full_t1 && chg_temp <= chip->low_curr_full_t2) {
			if (chip->range1_low_curr_full_num != 0 && chip->range1_low_curr_full) {
				for (i = 0; i < chip->range1_low_curr_full_num; i++) {
					if (temp_current <= chip->range1_low_curr_full[i].curr && temp_vbatt >= chip->range1_low_curr_full[i].vbatt) {
						low_curr = true;
						break;
					}
				}
				if (low_curr) {
					term_curr_over_count++;
				} else {
					term_curr_over_count = 0;
				}
				if (term_curr_over_count > 5) {
					voocphy_info("range1 lowcurr ntime fastchg full %d, %d\n", temp_vbatt, temp_current);
					oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_FULL);
				}
			}

		} else if (chg_temp > chip->low_curr_full_t2 && chg_temp <= chip->low_curr_full_t3) {
			if (chip->range2_low_curr_full_num != 0 && chip->range2_low_curr_full) {
				for (i = 0; i < chip->range2_low_curr_full_num; i++) {
					if (temp_current <= chip->range2_low_curr_full[i].curr && temp_vbatt >= chip->range2_low_curr_full[i].vbatt) {
						low_curr = true;
						break;
					}
				}
				if (low_curr) {
					term_curr_over_count++;
				} else {
					term_curr_over_count = 0;
				}
				if (term_curr_over_count > 5) {
					voocphy_info("range2 lowcurr ntime fastchg full %d, %d\n", temp_vbatt, temp_current);
					oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_FULL);
				}
			}
		}
		if (chip->parallel_charge_support) {
			temp_current = chip->sub_batt_icharging;
			temp_vbatt = chip->sub_vbatt;
			low_curr = 0;
			voocphy_info("sub range lowcurr ntime fastchg full %d, %d, %d\n", temp_vbatt, temp_current, chg_temp);
			if (chg_temp >= chip->low_curr_full_t1 && chg_temp <= chip->low_curr_full_t2) {
				if (chip->sub_range1_low_curr_full_num != 0 && chip->sub_range1_low_curr_full) {
					for (i = 0; i < chip->sub_range1_low_curr_full_num; i++) {
						if (temp_current <= chip->sub_range1_low_curr_full[i].curr && temp_vbatt >= chip->sub_range1_low_curr_full[i].vbatt) {
							low_curr = true;
							break;
						}
					}
					if (low_curr) {
						sub_term_curr_over_count++;
					} else {
						sub_term_curr_over_count = 0;
					}
					if (sub_term_curr_over_count > 5) {
						voocphy_info("sub_range1 lowcurr ntime fastchg full %d, %d\n", temp_vbatt, temp_current);
						oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_FULL);
					}
				}
			} else if (chg_temp > chip->low_curr_full_t2 && chg_temp <= chip->low_curr_full_t3) {
				if (chip->sub_range2_low_curr_full_num != 0 && chip->sub_range2_low_curr_full) {
					for (i = 0; i < chip->sub_range2_low_curr_full_num; i++) {
						if (temp_current <= chip->sub_range2_low_curr_full[i].curr && temp_vbatt >= chip->sub_range2_low_curr_full[i].vbatt) {
							low_curr = true;
							break;
						}
					}
					if (low_curr) {
						sub_term_curr_over_count++;
					} else {
						sub_term_curr_over_count = 0;
					}
					if (sub_term_curr_over_count > 5) {
						voocphy_info("sub_range2 lowcurr ntime fastchg full %d, %d\n", temp_vbatt, temp_current);
						oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_FULL);
					}
				}
			}
		}

		if (oplus_voocphy_get_bidirect_cp_support(chip)) {
			voocphy_info("copycat_icheck = %d, current_expect = %d, vbat_temp_cur = %d, chip->current_pwd = %d, chip->adapter_type = %d",
				chip->copycat_icheck, chip->current_expect, vbat_temp_cur, chip->current_pwd, chip->adapter_type);
			if ((chip->current_expect > 23) && (vbat_temp_cur > chip->current_pwd) && (chip->adapter_type == ADAPTER_SVOOC)) {
				if (chip->curr_pwd_count) {
					chip->curr_pwd_count--;
					if (chip->copycat_icheck == false && chip->curr_pwd_count == 5) {
						chip->copycat_icheck = true;
						chip->ap_need_change_current = 5;
						voocphy_info("vbat_temp_cur = %d current_pwd = %d changed current again", vbat_temp_cur, chip->current_pwd);
					}
				} else {
					voocphy_info("FAST_NOTIFY_ADAPTER_COPYCAT for over current");
					oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ADAPTER_COPYCAT);
				}
			}
			if ((chip->gauge_vbatt > chip->batt_pwd_vol_thd1)
			    && (vbat_temp_cur > chip->batt_pwd_curr_thd1)) {
				curr_curve_pwd_count--;
				if (curr_curve_pwd_count == 0) {
					voocphy_info("FAST_NOTIFY_ADAPTER_COPYCAT for over vbatt && ibat[%d %d]\n", chip->batt_pwd_vol_thd1, chip->batt_pwd_curr_thd1);
					oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_ADAPTER_COPYCAT);
				}
			} else {
				curr_curve_pwd_count = 10;
			}
		} else {
			if (vbat_temp_cur/2 > chip->current_pwd && chip->adapter_type == ADAPTER_SVOOC) {
				if (chip->curr_pwd_count) {
					chip->curr_pwd_count--;
				} else {
					voocphy_info("FAST_NOTIFY_ADAPTER_COPYCAT for over current");
					/*oplus_voocphy_set_status_and_notify_ap(FAST_NOTIFY_ADAPTER_COPYCAT);*/
				}
			}
			if ((chip->gauge_vbatt > BATT_PWD_VOL_THD1)
				&& (vbat_temp_cur > BATT_PWD_CURR_THD1)) {
				curr_curve_pwd_count--;
				if (curr_curve_pwd_count == 0) {
					voocphy_info("FAST_NOTIFY_ADAPTER_COPYCAT for over vbatt && ibat[%d %d]\n", BATT_PWD_VOL_THD1, BATT_PWD_CURR_THD1);
					/*oplus_voocphy_set_status_and_notify_ap(FAST_NOTIFY_ADAPTER_COPYCAT);*/
				}
			} else {
				curr_curve_pwd_count = 10;
			}
		}

		status = oplus_voocphy_monitor_timer_start(chip, VOOC_THREAD_TIMER_CURR, VOOC_CURR_EVENT_TIME);

	}

	return status;
}

void oplus_voocphy_request_fastchg_curv(struct oplus_voocphy_manager *chip)
{
	static int cc_cnt = 0;
	int idx = 0;
	int convert_ibus = 0;
	static int switch_ocp_cnt = 0;
	struct batt_sys_curves *batt_sys_curv_by_tmprange = NULL;
	struct batt_sys_curve *batt_sys_curve = NULL;
	struct batt_sys_curve *batt_sys_curve_next = NULL;

	if (!chip) {
		voocphy_info("chip is NULL pointer\n");
		return;
	}

	batt_sys_curv_by_tmprange = chip->batt_sys_curv_by_tmprange;
	if (!batt_sys_curv_by_tmprange) {
		voocphy_info("batt_sys_curv_by_tmprange is NULL pointer\n");
		return;
	}

	if (!chip->batt_sys_curv_found) {	/*first enter and find index*/
		//use default chg
		idx = chip->cur_sys_curv_idx;
		voocphy_info("!fastchg curv1 [%d %d %d]\n",
			     chip->current_max,
			     (&(batt_sys_curv_by_tmprange->batt_sys_curve[idx]))->target_ibus,
			     (&(batt_sys_curv_by_tmprange->batt_sys_curve[idx]))->target_vbat);
	} else {	/*adjust current*/
		idx = chip->cur_sys_curv_idx;
		if (idx >= BATT_SYS_ROW_MAX) {
			voocphy_info("idx out of bound of array!!!!\n");
			return;
		}
		batt_sys_curve = &(batt_sys_curv_by_tmprange->batt_sys_curve[idx]);
		if(!batt_sys_curve) {
			voocphy_info("batt_sys_curve is a NULL pointer!!!!\n");
			return;
		}

		/*
		 * After ap voocphy returns the charging current to the
		 * adapter (chip->current_expect), do not start the charging
		 * timing immediately, wait until the output current of the
		 * adapter is close to the preset charging current, and then
		 * start the charging timing.
		 */
		if (abs(chip->cp_ichg - chip->current_expect) <= TARGET_CURR_OFFSET_THR)
			batt_sys_curve->chg_time++;

		if (batt_sys_curve->exit == false && (idx < BATT_SYS_ROW_MAX - 1)) {
			voocphy_info("!fastchg curv2 [%d %d %d %d %d %d %d %d %d]\n", cc_cnt, idx, batt_sys_curve->exit,
			             batt_sys_curve->target_time, batt_sys_curve->chg_time,
			             chip->gauge_vbatt, chip->current_max,
			             (&(batt_sys_curv_by_tmprange->batt_sys_curve[idx]))->target_ibus,
			             (&(batt_sys_curv_by_tmprange->batt_sys_curve[idx]))->target_vbat);
			convert_ibus = batt_sys_curve->target_ibus * 100;
			if (oplus_switching_support_parallel_chg()) {
				if (chip->sub_batt_icharging > chip->voocphy_max_sub_ibat ||
				    chip->main_batt_icharging > chip->voocphy_max_main_ibat) {
					switch_ocp_cnt++;
					if (switch_ocp_cnt > 6) {
						voocphy_info("switch error??? sub_batt_icharging:%d, main_batt_icharging:%d\n",
							     chip->sub_batt_icharging, chip->main_batt_icharging);
						batt_sys_curve->chg_time = 0;
						voocphy_info("!bf switch fastchg curv [%d %d %d]\n", chip->current_max,
								(&(batt_sys_curv_by_tmprange->batt_sys_curve[idx]))->target_ibus,
								(&(batt_sys_curv_by_tmprange->batt_sys_curve[idx]))->target_vbat);
						batt_sys_curve_next = &(batt_sys_curv_by_tmprange->batt_sys_curve[idx+1]);
						if(!batt_sys_curve_next) {
							voocphy_info("batt_sys_curve is a NULL pointer!!!!\n");
							return;
						}
						chip->cur_sys_curv_idx += 1;
						batt_sys_curve_next->chg_time = 0;
						cc_cnt = 0;
						chip->current_max = chip->current_max > batt_sys_curve_next->target_ibus ? batt_sys_curve_next->target_ibus : chip->current_max;
						voocphy_info("!switch error? switch fastchg curv [%d %d %d]\n",
							     chip->current_max, batt_sys_curve_next->target_ibus,
							     batt_sys_curve_next->target_vbat);
					}
				} else {
					switch_ocp_cnt = 0;
				}
			}
			if ((chip->gauge_vbatt > batt_sys_curve->target_vbat			//switch by vol_thr
			     && chip->cp_ichg < convert_ibus + TARGET_VOL_OFFSET_THR)
			    || (batt_sys_curve->target_time > 0 && batt_sys_curve->chg_time >= batt_sys_curve->target_time)		//switch by chg time
			   ) {
				cc_cnt++;
				if (cc_cnt > 3) {
					if (batt_sys_curve->exit == false) {
						if (batt_sys_curve->chg_time >= batt_sys_curve->target_time) {
							voocphy_info("switch fastchg curv by chgtime[%d, %d]\n",
								     batt_sys_curve->chg_time,
								     batt_sys_curve->target_time);
						}
						batt_sys_curve->chg_time = 0;
						voocphy_info("!bf switch fastchg curv [%d %d %d]\n",
							     chip->current_max,
							     (&(batt_sys_curv_by_tmprange->batt_sys_curve[idx]))->target_ibus,
							     (&(batt_sys_curv_by_tmprange->batt_sys_curve[idx]))->target_vbat);
						batt_sys_curve_next = &(batt_sys_curv_by_tmprange->batt_sys_curve[idx+1]);
						if(!batt_sys_curve_next) {
							voocphy_info("batt_sys_curve is a NULL pointer!!!!\n");
							return;
						}
						chip->cur_sys_curv_idx += 1;
						batt_sys_curve_next->chg_time = 0;
						cc_cnt = 0;
						chip->current_max = chip->current_max > batt_sys_curve_next->target_ibus ? batt_sys_curve_next->target_ibus : chip->current_max;
						voocphy_info("!af switch fastchg curv [%d %d %d]\n",
							     chip->current_max,
							     batt_sys_curve_next->target_ibus,
							     batt_sys_curve_next->target_vbat);
					}
				}
			} else {
				cc_cnt = 0;
			}
		} else {
			//exit fastchg
			voocphy_info("! exit fastchg\n");
		}
	}

	voocphy_info( "curv info [%d %d %d %d]\n", chip->gauge_vbatt, chip->cp_ichg, chip->current_max, cc_cnt);
}

static int oplus_voocphy_vol_event_handle(struct device *dev, unsigned long data)
{
	int status = VOOCPHY_SUCCESS;
	static unsigned char fast_full_count = 0;
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	if (chip->fastchg_monitor_stop == true) {
		voocphy_info( "oplus_voocphy_vol_event_handle ignore");
		return status;
	}
	if (chip->parallel_charge_support) {
		chip->main_vbatt = oplus_chglib_gauge_vbatt(chip->dev);
		chip->sub_vbatt = oplus_chglib_gauge_sub_vbatt();
		chip->pre_gauge_vbatt = chip->main_vbatt > chip->sub_vbatt ? chip->main_vbatt : chip->sub_vbatt;
		chip->main_batt_icharging = oplus_chglib_gauge_current(chip->dev);
		chip->sub_batt_icharging = oplus_chglib_gauge_sub_current();
		chip->icharging = chip->main_batt_icharging + chip->sub_batt_icharging;
		voocphy_info("batt[%d, %d], main_batt[%d, %d], sub_batt[%d, %d]\n",
			     chip->pre_gauge_vbatt, chip->icharging, chip->main_vbatt, chip->main_batt_icharging,
			     chip->sub_vbatt, chip->sub_batt_icharging);
	} else if (chip->external_gauge_support) {
		chip->gauge_vbatt = oplus_chglib_gauge_vbatt(chip->dev);
	}

	voocphy_info( "[oplus_voocphy_vol_event_handle] vbatt: %d",chip->gauge_vbatt);
	if ((chip->fastchg_notify_status & 0xFF) == FAST_NOTIFY_PRESENT)
		fast_full_count = 0;

	if (!chip->btb_temp_over) {
		oplus_voocphy_request_fastchg_curv(chip);
		//set above calculate max current
		if (!chip->ap_need_change_current)
			chip->ap_need_change_current
			    = oplus_voocphy_set_fastchg_current(chip);

		//notify full at some condition
		if ((chip->batt_temp_plugin == VOOCPHY_BATT_TEMP_LITTLE_COLD			/*0-5 chg to 4430mV*/
		     && chip->gauge_vbatt > chip->vooc_little_cold_full_voltage)
		    || (chip->batt_temp_plugin == VOOCPHY_BATT_TEMP_COOL				/*5-12 chg to 4430mV*/
		        && chip->gauge_vbatt > chip->vooc_cool_full_voltage)
		    || (chip->batt_temp_plugin == VOOCPHY_BATT_TEMP_WARM				/*43-52 chg to 4130mV*/
		    	&& chip->vooc_warm_full_voltage != -EINVAL
		        && chip->gauge_vbatt > chip->vooc_warm_full_voltage)
		    || (chip->gauge_vbatt > chip->vooc_1time_full_voltage)) {
			voocphy_info( "vbatt 1time fastchg full: %d",chip->gauge_vbatt);
			oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_FULL);
		} else if (chip->gauge_vbatt > chip->vooc_ntime_full_voltage) {
			fast_full_count++;
			if (fast_full_count > 5) {
				voocphy_info( "vbatt ntime fastchg full: %d",chip->gauge_vbatt);
				oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_FULL);
			}
		}

		status = oplus_voocphy_monitor_timer_start(chip, VOOC_THREAD_TIMER_VOL, VOOC_VOL_EVENT_TIME);
	}

	return status;
}

static int oplus_vooc_set_current_warm_range(struct oplus_voocphy_manager *chip, int vbat_temp_cur)
{
	int ret = chip->vooc_strategy_normal_current;

	if (chip->vooc_batt_over_high_temp != -EINVAL
	    && vbat_temp_cur > chip->vooc_batt_over_high_temp) {
		chip->vooc_strategy_change_count++;
		if (chip->vooc_strategy_change_count >= VOOC_TEMP_OVER_COUNTS) {
			chip->vooc_strategy_change_count = 0;
			chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
			ret = chip->vooc_strategy_normal_current;
			voocphy_info("[vooc_monitor] temp_over:%d", vbat_temp_cur);
			oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_BATT_TEMP_OVER);
		}
	} else if (vbat_temp_cur < chip->vooc_normal_high_temp) {
		chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_INIT;
		ret = chip->vooc_strategy_normal_current;
		oplus_voocphy_reset_temp_range(chip);
		chip->vooc_normal_high_temp += VOOC_TEMP_RANGE_THD;
		voocphy_info("[vooc_monitor] switch temp range:%d", vbat_temp_cur);
		oplus_chglib_notify_ap(chip->dev, FAST_NOTIFY_ONGOING);
	} else {
		chip->fastchg_batt_temp_status = BAT_TEMP_WARM;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_WARM;
		ret = chip->vooc_strategy_normal_current;
	}

	return ret;
}

static int oplus_voocphy_set_current_temp_normal_range(struct oplus_voocphy_manager *chip, int vbat_temp_cur)
{
	int ret = 0;
	int vooc_strategy1_high_current0 = chip->vooc_strategy1_high_current0;
	int vooc_strategy1_high_current1 = chip->vooc_strategy1_high_current1;
	int vooc_strategy1_high_current2 = chip->vooc_strategy1_high_current2;
	int vooc_strategy1_low_current0 = chip->vooc_strategy1_low_current0;
	int vooc_strategy1_low_current1 = chip->vooc_strategy1_low_current1;
	int vooc_strategy1_low_current2 = chip->vooc_strategy1_low_current2;

	if (chip->adapter_type == ADAPTER_VOOC20
	    || chip->adapter_type == ADAPTER_VOOC30) {
		vooc_strategy1_high_current0 = chip->vooc_strategy1_high_current0_vooc;
		vooc_strategy1_high_current1 = chip->vooc_strategy1_high_current1_vooc;
		vooc_strategy1_high_current2 = chip->vooc_strategy1_high_current2_vooc;
		vooc_strategy1_low_current0 = chip->vooc_strategy1_low_current0_vooc;
		vooc_strategy1_low_current1 = chip->vooc_strategy1_low_current1_vooc;
		vooc_strategy1_low_current2 = chip->vooc_strategy1_low_current2_vooc;
	}

	chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_HIGH;

	switch (chip->fastchg_batt_temp_status) {
	case BAT_TEMP_NORMAL_HIGH:
		if (vbat_temp_cur > chip->vooc_strategy1_batt_high_temp0) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
			ret = vooc_strategy1_high_current0;
		} else if (vbat_temp_cur >= chip->vooc_normal_low_temp) {
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_HIGH;
			ret = chip->vooc_strategy_normal_current;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
			ret = chip->vooc_strategy_normal_current;
			oplus_voocphy_reset_temp_range(chip);
			chip->vooc_normal_low_temp += VOOC_TEMP_RANGE_THD;
		}
		break;
	case BAT_TEMP_HIGH0:
		if (vbat_temp_cur > chip->vooc_strategy1_batt_high_temp1) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
			ret = vooc_strategy1_high_current1;
		} else if (vbat_temp_cur < chip->vooc_strategy1_batt_low_temp0) {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW0;
			ret = vooc_strategy1_low_current0;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
			ret = vooc_strategy1_high_current0;
		}
		break;
	case BAT_TEMP_HIGH1:
		if (vbat_temp_cur > chip->vooc_strategy1_batt_high_temp2) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
			ret = vooc_strategy1_high_current2;
		} else if (vbat_temp_cur < chip->vooc_strategy1_batt_low_temp1) {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW1;
			ret = vooc_strategy1_low_current1;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
			ret = vooc_strategy1_high_current1;
		}
		break;
	case BAT_TEMP_HIGH2:
		if (chip->vooc_normal_high_temp != -EINVAL
			&& vbat_temp_cur > chip->vooc_normal_high_temp) {
			chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_INIT;
			ret = chip->vooc_strategy_normal_current;
			oplus_voocphy_reset_temp_range(chip);
			chip->vooc_normal_high_temp -= VOOC_TEMP_RANGE_THD;
			voocphy_info("[vooc_monitor] switch temp range:%d", vbat_temp_cur);
			oplus_chglib_notify_ap(chip->dev, FAST_NOTIFY_ONGOING);
		} else if (chip->vooc_batt_over_high_temp != -EINVAL
		    && vbat_temp_cur > chip->vooc_batt_over_high_temp) {
			chip->vooc_strategy_change_count++;
			if (chip->vooc_strategy_change_count >= VOOC_TEMP_OVER_COUNTS) {
				chip->vooc_strategy_change_count = 0;
				chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
				voocphy_info( "[vooc_monitor] temp_over:%d", vbat_temp_cur);
				oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_BATT_TEMP_OVER);
			}
		} else if (vbat_temp_cur < chip->vooc_strategy1_batt_low_temp2) {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW2;
			ret = vooc_strategy1_low_current2;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
			ret = vooc_strategy1_high_current2;
		}
		break;
	case BAT_TEMP_LOW0:
		if (vbat_temp_cur > chip->vooc_strategy1_batt_high_temp0) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH0;
			ret = vooc_strategy1_high_current0;
		} else if (vbat_temp_cur < chip->vooc_normal_low_temp) {/*T<25C*/
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
			ret = chip->vooc_strategy_normal_current;
			oplus_voocphy_reset_temp_range(chip);
			chip->vooc_normal_low_temp += VOOC_TEMP_RANGE_THD;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW0;
			ret = vooc_strategy1_low_current0;
		}
		break;
	case BAT_TEMP_LOW1:
		if (vbat_temp_cur > chip->vooc_strategy1_batt_high_temp1) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH1;
			ret = vooc_strategy1_high_current1;
		} else if (vbat_temp_cur < chip->vooc_strategy1_batt_low_temp0) {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW0;
			ret = vooc_strategy1_low_current0;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW1;
			ret = vooc_strategy1_low_current1;
		}
		break;
	case BAT_TEMP_LOW2:
		if (vbat_temp_cur > chip->vooc_strategy1_batt_high_temp2) {
			chip->fastchg_batt_temp_status = BAT_TEMP_HIGH2;
			ret = vooc_strategy1_high_current2;
		} else if (vbat_temp_cur < chip->vooc_strategy1_batt_low_temp1) {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW1;
			ret = vooc_strategy1_low_current1;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_LOW2;
			ret = vooc_strategy1_low_current2;
		}
		break;
	default:
		break;
	}
	voocphy_info("the ret: %d, the temp =%d, status = %d\r\n", ret, vbat_temp_cur, chip->fastchg_batt_temp_status);
	return ret;
}

static int oplus_voocphy_set_current_temp_low_normal_range(struct oplus_voocphy_manager *chip, int vbat_temp_cur)
{
	int ret = 0;

	if (vbat_temp_cur < chip->vooc_normal_low_temp
	    && vbat_temp_cur >= chip->vooc_little_cool_temp) { /*16C<=T<25C*/
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
		chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
		ret = chip->vooc_strategy_normal_current;
	} else {
		if (vbat_temp_cur >= chip->vooc_normal_low_temp) {
			chip->vooc_normal_low_temp -= VOOC_TEMP_RANGE_THD;
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_HIGH;
			ret = chip->vooc_strategy_normal_current;
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_HIGH;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
			ret = chip->vooc_strategy_normal_current;
			oplus_voocphy_reset_temp_range(chip);
			chip->vooc_little_cool_temp += VOOC_TEMP_RANGE_THD;
		}
	}

	return ret;
}

static int oplus_voocphy_set_current_temp_little_cool_range(struct oplus_voocphy_manager *chip, int vbat_temp_cur)
{
	int ret = 0;

	if (vbat_temp_cur < chip->vooc_little_cool_temp
	    && vbat_temp_cur >= chip->vooc_cool_temp) {/*12C<=T<16C*/
		chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
		ret = chip->vooc_strategy_normal_current;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
	} else {
		if (vbat_temp_cur >= chip->vooc_little_cool_temp) {
			chip->vooc_little_cool_temp -= VOOC_TEMP_RANGE_THD;
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
			ret = chip->vooc_strategy_normal_current;
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
		} else {
			if (oplus_chglib_get_soc(chip->dev) <= 90) {
				chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_INIT;
				voocphy_info("[vooc_monitor] switch temp range:%d", vbat_temp_cur);
				oplus_chglib_notify_ap(chip->dev, FAST_NOTIFY_ONGOING);
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
			}
			ret = chip->vooc_strategy_normal_current;
			oplus_voocphy_reset_temp_range(chip);
			chip->vooc_cool_temp += VOOC_TEMP_RANGE_THD;
		}
	}

	return ret;
}

static int oplus_voocphy_set_current_temp_cool_range(struct oplus_voocphy_manager *chip, int vbat_temp_cur)
{
	int ret = 0;
	if (chip->vooc_batt_over_high_temp != -EINVAL
	    && vbat_temp_cur < chip->vooc_batt_over_low_temp) {
		chip->vooc_strategy_change_count++;
		if (chip->vooc_strategy_change_count >= VOOC_TEMP_OVER_COUNTS) {
			chip->vooc_strategy_change_count = 0;
			chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
			voocphy_info( "[vooc_monitor] temp_over:%d", vbat_temp_cur);
			oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_BATT_TEMP_OVER);
		}
	} else if (vbat_temp_cur < chip->vooc_cool_temp
	           && vbat_temp_cur >= chip->vooc_little_cold_temp) {/*5C <=T<12C*/
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
		chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
		ret = chip->vooc_strategy_normal_current;
	} else {
		if (vbat_temp_cur >= chip->vooc_cool_temp) {
			if (oplus_chglib_get_soc(chip->dev) <= 90) {
				chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_INIT;
				voocphy_info("[vooc_monitor] switch temp range:%d", vbat_temp_cur);
				oplus_chglib_notify_ap(chip->dev, FAST_NOTIFY_ONGOING);
			} else {
				chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
			}
			oplus_voocphy_reset_temp_range(chip);
			chip->vooc_cool_temp -= VOOC_TEMP_RANGE_THD;
			ret = chip->vooc_strategy_normal_current;
		} else {
			chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COLD;
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COLD;
			ret = chip->vooc_strategy_normal_current;
			oplus_voocphy_reset_temp_range(chip);
			chip->vooc_little_cold_temp += VOOC_TEMP_RANGE_THD;
		}
	}

	return ret;
}

static int oplus_voocphy_set_current_temp_little_cold_range(struct oplus_voocphy_manager *chip, int vbat_temp_cur)
{
	int ret = 0;
	if (chip->vooc_batt_over_high_temp != -EINVAL
	    && vbat_temp_cur < chip->vooc_batt_over_low_temp) {
		chip->vooc_strategy_change_count++;
		if (chip->vooc_strategy_change_count >= VOOC_TEMP_OVER_COUNTS) {
			chip->vooc_strategy_change_count = 0;
			chip->fastchg_batt_temp_status = BAT_TEMP_EXIT;
			voocphy_info( "[vooc_monitor] temp_over:%d", vbat_temp_cur);
			oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_BATT_TEMP_OVER);
		}
	} else if (vbat_temp_cur < chip->vooc_little_cold_temp) { /*0C<=T<5C*/
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COLD;
		chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COLD;
		ret = chip->vooc_strategy_normal_current;
	} else {
		chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
		ret = chip->vooc_strategy_normal_current;
		chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
		oplus_voocphy_reset_temp_range(chip);
		chip->vooc_little_cold_temp -= VOOC_TEMP_RANGE_THD;
	}

	return ret;
}

static int oplus_voocphy_set_current_by_batt_temp(struct oplus_voocphy_manager *chip,
					int vbat_temp_cur)
{
	int ret = 0;

	if (chip->vooc_temp_cur_range == FASTCHG_TEMP_RANGE_INIT) {
		if (vbat_temp_cur < chip->vooc_little_cold_temp) { /*0-5C*/
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COLD;
			chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COLD;
		} else if (vbat_temp_cur < chip->vooc_cool_temp) { /*5-12C*/
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_COOL;
			chip->fastchg_batt_temp_status = BAT_TEMP_COOL;
		} else if (vbat_temp_cur < chip->vooc_little_cool_temp) { /*12-16C*/
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_LITTLE_COOL;
			chip->fastchg_batt_temp_status = BAT_TEMP_LITTLE_COOL;
		} else if (vbat_temp_cur < chip->vooc_normal_low_temp) { /*16-35C*/
			chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_LOW;
			chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_LOW;
		} else {
			if (chip->vooc_normal_high_temp == -EINVAL || vbat_temp_cur < chip->vooc_normal_high_temp) {/*35C-43C*/
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_NORMAL_HIGH;
				chip->fastchg_batt_temp_status = BAT_TEMP_NORMAL_HIGH;
			} else {
				chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_WARM;
				chip->fastchg_batt_temp_status = BAT_TEMP_WARM;
			}
		}
	}

	switch (chip->vooc_temp_cur_range) {
	case FASTCHG_TEMP_RANGE_WARM:
		ret = oplus_vooc_set_current_warm_range(chip, vbat_temp_cur);
		break;
	case FASTCHG_TEMP_RANGE_NORMAL_HIGH:
		ret = oplus_voocphy_set_current_temp_normal_range(chip, vbat_temp_cur);
		break;
	case FASTCHG_TEMP_RANGE_NORMAL_LOW:
		ret = oplus_voocphy_set_current_temp_low_normal_range(chip, vbat_temp_cur);
		break;
	case FASTCHG_TEMP_RANGE_LITTLE_COOL:
		ret = oplus_voocphy_set_current_temp_little_cool_range(chip, vbat_temp_cur);
		break;
	case FASTCHG_TEMP_RANGE_COOL:
		ret = oplus_voocphy_set_current_temp_cool_range(chip, vbat_temp_cur);
		break;
	case FASTCHG_TEMP_RANGE_LITTLE_COLD:
		ret = oplus_voocphy_set_current_temp_little_cold_range(chip, vbat_temp_cur);
		break;
	default:
		break;
	}

	voocphy_info("the ret: %d, the temp =%d, temp_status = %d, temp_range = %d\r\n",
	             ret, vbat_temp_cur, chip->fastchg_batt_temp_status, chip->vooc_temp_cur_range);
	return ret;
}

static int oplus_voocphy_temp_event_handle(struct device *dev, unsigned long data)
{
	int status = VOOCPHY_SUCCESS;
	int batt_temp = 0;
	static bool allow_temp_monitor = false;
	static unsigned char delay_temp_count = 0;
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	if (chip->fastchg_monitor_stop == true) {
		voocphy_info( "oplus_voocphy_temp_event_handle ignore");
		return status;
	}

	batt_temp = oplus_chglib_get_shell_temp(chip->dev);
	if (chip->batt_fake_temp) {
		batt_temp = chip->batt_fake_temp;
	}
	if ((chip->fastchg_notify_status & 0XFF) == FAST_NOTIFY_PRESENT) {
		delay_temp_count = 0;
		allow_temp_monitor = false;
	}

	if (delay_temp_count < DELAY_TEMP_MONITOR_COUNTS) {
		delay_temp_count++;
		allow_temp_monitor = false;
	} else {
		allow_temp_monitor = true;
		delay_temp_count = DELAY_TEMP_MONITOR_COUNTS;
	}

	voocphy_info( "!batt_temp = %d %d %d %d\n", batt_temp, allow_temp_monitor,
	              chip->vooc_temp_cur_range, chip->fastchg_batt_temp_status);

	if ((!chip->btb_temp_over) && (allow_temp_monitor == true)) {
		chip->current_batt_temp = oplus_voocphy_set_current_by_batt_temp(chip, batt_temp);

		if (chip->voocphy_dual_cp_support) {
			if(chip->adapter_type == ADAPTER_SVOOC) {
				chip->current_batt_temp /= 2;
			} else if (chip->adapter_type == ADAPTER_VOOC20
			           || chip->adapter_type == ADAPTER_VOOC30) {
				if (chip->current_batt_temp > 59) {
					chip->current_batt_temp = 59;
				}
			}
		} else {
			if (oplus_voocphy_get_bidirect_cp_support(chip)) {
				if (chip->current_batt_temp > chip->voocphy_cp_max_ibus) {
					chip->current_batt_temp = chip->voocphy_cp_max_ibus;
				}

				/* add for pd+svooc adapter to limit the current */
				if (oplus_chglib_is_pd_svooc_adapter(chip->dev) &&
				   (chip->current_batt_temp > 40) && (chip->adapter_type == ADAPTER_SVOOC)) {
					chip->current_batt_temp = 40;
					voocphy_info(" pd svooc adapter, limit the current 4A");
				}
			} else {
				if(chip->adapter_type == ADAPTER_SVOOC) {
					chip->current_batt_temp /= 2;
				} else if (chip->adapter_type == ADAPTER_VOOC20
				           || chip->adapter_type == ADAPTER_VOOC30) {
				if (chip->current_batt_temp > 30) {
					chip->current_batt_temp = 30;
					}
				}
			}
		}

		if (!chip->ap_need_change_current) {
			chip->ap_need_change_current = oplus_voocphy_set_fastchg_current(chip);
		}
	}

	if (batt_temp >= chip->vooc_batt_over_high_temp || batt_temp <= chip->vooc_batt_over_low_temp) {
		oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_BATT_TEMP_OVER);
	}


	status = oplus_voocphy_monitor_timer_start(chip, VOOC_THREAD_TIMER_TEMP, VOOC_TEMP_EVENT_TIME);

	return status;
}

static bool oplus_voocphy_btb_and_usb_temp_detect(struct oplus_voocphy_manager *chip)
{
	bool detect_over = false;
	int status = VOOCPHY_SUCCESS;
	int btb_temp =0, usb_temp = 0;
	static unsigned char temp_over_count = 0;

	if ((chip->fastchg_notify_status & 0XFF) == FAST_NOTIFY_PRESENT) {
		temp_over_count = 0;
	}

	btb_temp = oplus_chglib_get_battery_btb_temp_cal();
	usb_temp = oplus_chglib_get_usb_btb_temp_cal();

	if (status != VOOCPHY_SUCCESS) {
		voocphy_info( "get btb and usb temp error");
		return detect_over;
	}

	voocphy_err( "btb_temp: %d, usb_temp: %d", btb_temp, usb_temp);
	if (btb_temp >= 80 || usb_temp >= 80) {
		temp_over_count++;
		if (temp_over_count > 9) {
			detect_over = true;
			voocphy_info( "btb_and_usb temp over");
		}
	} else {
		temp_over_count = 0;
	}

	return detect_over;
}

static int oplus_voocphy_btb_event_handle(struct device *dev, unsigned long data)
{
	int status = VOOCPHY_SUCCESS;
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	bool btb_detect = false;
	static bool btb_err_report = false;

	voocphy_info( "oplus_voocphy_btb_event_handle");
	if (chip->fastchg_monitor_stop == true) {
		voocphy_info( "oplus_voocphy_btb_event_handle ignore");
		return status;
	}

	btb_detect = oplus_voocphy_btb_and_usb_temp_detect(chip);
	if ((chip->fastchg_notify_status & 0XFF) == FAST_NOTIFY_PRESENT) {
		btb_err_report = false;
	}

	if (!chip->btb_temp_over) {
		if (btb_detect) {
			oplus_voocphy_slave_set_chg_enable(chip, false);
			oplus_voocphy_set_chg_enable(chip, false);
			oplus_voocphy_set_adc_forcedly_enable(chip, ADC_FORCEDLY_ENABLED);
			chip->btb_temp_over = true;
		}
		status = oplus_voocphy_monitor_timer_start(chip, VOOC_THREAD_TIMER_BTB, VOOC_BTB_EVENT_TIME);
	} else {
		if (!btb_err_report) {
			btb_err_report = true;
			oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_BTB_TEMP_OVER);
		}
		status = oplus_voocphy_monitor_timer_start(chip, VOOC_THREAD_TIMER_BTB, VOOC_BTB_OVER_EVENT_TIME);
	}

	return status;
}

static int oplus_voocphy_safe_event_handle(struct device *dev, unsigned long data)
{
	int status = VOOCPHY_SUCCESS;
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	if (chip->fastchg_monitor_stop == true) {
		voocphy_info( "oplus_voocphy_safe_event_handle ignore");
		return status;
	}

	if (chip->fastchg_timeout_time)
		chip->fastchg_timeout_time--;
	if (chip->fastchg_3c_timeout_time)
		chip->fastchg_3c_timeout_time--;

	if (chip->fastchg_timeout_time == 0
	    && (!chip->btb_temp_over)) {
		voocphy_info("safe time fastchg full\n");
		oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_FULL);
	}

	if (chip->usb_bad_connect) {
		oplus_voocphy_set_status_and_notify_ap(chip, FAST_NOTIFY_BAD_CONNECTED);
		//reset vooc_phy
		oplus_voocphy_reset_voocphy(chip);
	}
	status = oplus_voocphy_monitor_timer_start(chip, VOOC_THREAD_TIMER_SAFE, VOOC_SAFE_EVENT_TIME);

	return status;
}

static void oplus_voocphy_reset_fastchg_after_usbout(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		voocphy_err ("chip is null\n");
		return;
	}

	if (chip->fastchg_start == false) {
		chip->fastchg_notify_status = FAST_NOTIFY_UNKNOW;
		voocphy_info ("fastchg_start");
	}
	chip->fastchg_to_normal = false;
	chip->fastchg_to_warm_full = false;
	chip->fastchg_to_warm = false;
	chip->fastchg_dummy_start = false;
	chip->fastchg_ing = false;
	chip->fastchg_start = false;
	chip->fastchg_commu_ing = false;
	chip->adapter_check_vooc_head_count = 0;
	chip->adapter_check_cmd_data_count = 0;
	/*avoid displaying vooc symbles when btb_temp_over and plug out*/
	if (chip->btb_temp_over) {
		voocphy_info("btb_temp_over plug out\n");
		chip->btb_temp_over = false;
	} else {
		chip->fast_chg_type = FASTCHG_CHARGER_TYPE_UNKOWN;
	}
	chip->fastchg_reactive = false;
	oplus_voocphy_set_user_exit_fastchg(chip, false);
	oplus_voocphy_set_fastchg_state(chip, OPLUS_FASTCHG_STAGE_1);

	oplus_voocphy_reset_variables(chip);
	oplus_voocphy_basetimer_monitor_stop(chip);
	oplus_voocphy_monitor_stop(chip);
	oplus_voocphy_reset_ibus_trouble_flag();
	voocphy_cpufreq_update(chip, CPU_CHG_FREQ_STAT_AUTO);
	if (oplus_voocphy_get_bidirect_cp_support(chip)) {
		oplus_voocphy_set_chg_auto_mode(chip, false);
		voocphy_info("oplus_voocphy_set_chg_auto_mode  false");
	}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
	if (pm_qos_request_active(&pm_qos_req)) {
		pm_qos_update_request(&pm_qos_req, PM_QOS_DEFAULT_VALUE);
		voocphy_info("pm_qos_remove_request after usbout");
	}
#else
	if (cpu_latency_qos_request_active(&pm_qos_req)) {
		cpu_latency_qos_remove_request(&pm_qos_req);
		voocphy_info("pm_qos_remove_request after usbout");
	}
#endif
	voocphy_info("reset fastchg after usbout");
}

static int oplus_voocphy_commu_process_handle(struct device *dev, unsigned long enable)
{
	int status = VOOCPHY_SUCCESS;
	unsigned long long duration = 1500;
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	if (status != VOOCPHY_SUCCESS) {
		voocphy_info("get commu wait lock fail");
		return status;
	}

	/* note exception for systrace */
	voocphy_info("dur time %lld usecs\n", duration);

	if (enable && chip->fastchg_commu_stop == false) {
		voocphy_info("[vooc_monitor] commu timeout");
		chip->rcv_done_200ms_timeout_num++;
		if (chip->adapter_type == ADAPTER_VOOC20)
			cancel_delayed_work(&chip->voocphy_send_ongoing_notify);
		chip->fastchg_start = false;
		oplus_chglib_push_break_code(chip->dev, TRACK_CP_VOOCPHY_COMMU_TIME_OUT);
		oplus_voocphy_reset_temp_range(chip);
		oplus_voocphy_reset_fastchg_after_usbout(chip);
		status = oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_BTB, VOOC_BTB_EVENT_TIME);
		status = oplus_voocphy_monitor_stop(chip);
		status |= oplus_voocphy_reset_voocphy(chip);
		chip->fastchg_commu_ing = false;
		if (chip->copycat_vooc_adapter && chip->copycat_vooc_support == true) {
			chip->copycat_vooc_adapter = false;
			oplus_chglib_vooc_fastchg_disable(COPYCAT_ADAPTER, true);
		}
		if (oplus_chglib_get_vooc_is_started(chip->dev))
			oplus_chglib_notify_ap(chip->dev, FAST_NOTIFY_ABSENT);
	} else {
		chip->fastchg_commu_stop = true;
		status = oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_COMMU, VOOC_COMMU_EVENT_TIME);
		chip->fastchg_commu_ing = false;
		if (oplus_voocphy_get_bidirect_cp_support(chip)) {
			oplus_voocphy_set_chg_auto_mode(chip, false);
		}
		voocphy_info("[vooc_monitor] commu time stop [%d]", chip->fastchg_notify_status);
	}

	return status;
}

static bool oplus_voocphy_get_fastchg_to_warm(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		return false;
	} else {
		return chip->fastchg_to_warm;
	}
}

static int oplus_voocphy_fastchg_check_process_handle(struct device *dev, unsigned long enable)
{
	int status = VOOCPHY_SUCCESS;
	bool bPlugIn = false;
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	bPlugIn = oplus_chglib_get_chg_stats();
	chip->fastchg_real_allow = oplus_voocphy_check_fastchg_real_allow(chip);

	voocphy_info( "fastchg_to_warm[%d %d] fastchg_dummy_start[%d] "
		      "fastchg_err_commu[%d] fastchg_check_stop[%d] "
		      "enable[%ld] bPlugIn[%d] fastchg_real_allow[%d]\n",
	              chip->fastchg_to_warm,
		      oplus_voocphy_get_fastchg_to_warm(chip),
		      chip->fastchg_dummy_start, chip->fastchg_err_commu,
	              chip->fastchg_check_stop, enable, bPlugIn,
		      chip->fastchg_real_allow);
	if ((chip->fastchg_to_warm || oplus_voocphy_get_fastchg_to_warm(chip)
	     || chip->fastchg_dummy_start
	     || chip->fastchg_err_commu)
	    && (!chip->fastchg_check_stop)
	    && enable && bPlugIn) {
		if (chip->fastchg_real_allow == true) {
			oplus_voocphy_reset_variables(chip);
			chip->fastchg_real_allow = oplus_voocphy_check_fastchg_real_allow(chip);
		} else {
			voocphy_info( "[vooc_monitor] VOOC_THREAD_TIMER_FASTCHG_CHECK start for self fastchg check time start");
			status = oplus_voocphy_monitor_timer_start(chip, VOOC_THREAD_TIMER_FASTCHG_CHECK, VOOC_FASTCHG_CHECK_TIME);
		}
	} else {
		chip->fastchg_check_stop = true;
		status = oplus_voocphy_monitor_timer_stop(chip, VOOC_THREAD_TIMER_FASTCHG_CHECK, VOOC_FASTCHG_CHECK_TIME);
		voocphy_info( "[vooc_monitor] VOOC_THREAD_TIMER_FASTCHG_CHECK stop fastchg check time stop");
	}

	return status;
}

static int oplus_voocphy_ibus_check_event_handle(struct device *dev, unsigned long data)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);
	static int ibus_trouble_count = 0;
	int status = VOOCPHY_SUCCESS;
	int main_cp_ibus = 0;
	int slave_cp_ibus = 0;
	u8 slave_cp_status = 0;

	if (chip->fastchg_monitor_stop == true) {
		voocphy_info( "oplus_voocphy_ibus_check_event_handle ignore");
		return status;
	}

	oplus_voocphy_slave_get_chg_enable(chip, &slave_cp_status);
	if (slave_cp_status == 1) {
		main_cp_ibus = chip->master_cp_ichg;
		slave_cp_ibus = oplus_voocphy_get_slave_ichg(chip);

		voocphy_err("main_cp_ibus = -%d slave_cp_ibus = -%d", main_cp_ibus, slave_cp_ibus);
		if (chip->adapter_type == ADAPTER_SVOOC) {
			voocphy_err("adapter type = %d", chip->adapter_type);
			if (main_cp_ibus > chip->voocphy_cp_max_ibus || slave_cp_ibus > chip->voocphy_cp_max_ibus) {
				ibus_trouble_count = ibus_trouble_count + 1;
			} else {
				voocphy_err("Discontinuity satisfies the condition, ibus_trouble_count = 0!\n");
				ibus_trouble_count = 0;
			}

			if (ibus_trouble_count >= 3) {
				voocphy_err("ibus_trouble_count >= 3, limit current to 3A!\n");
				if (chip->current_max > 30) {
					chip->current_max = 30;
					if (!chip->ap_need_change_current)
						chip->ap_need_change_current = oplus_voocphy_set_fastchg_current(chip);
				}
			}
		} else if (chip->adapter_type == ADAPTER_VOOC30
					|| chip->adapter_type == ADAPTER_VOOC20) {
			voocphy_err("adapter type = %d", chip->adapter_type);
			if (main_cp_ibus > chip->voocphy_cp_max_ibus || slave_cp_ibus > chip->voocphy_cp_max_ibus) {
				ibus_trouble_count = ibus_trouble_count + 1;
			} else {
				voocphy_err("Discontinuity satisfies the condition, ibus_trouble_count = 0!\n");
				ibus_trouble_count = 0;
			}

			if (ibus_trouble_count >= 3) {
				voocphy_err("ibus_trouble_count >= 3, limit current to 3A!\n");
				if (chip->current_max > 30) {
					chip->current_max = 30;
					if (!chip->ap_need_change_current)
						chip->ap_need_change_current = oplus_voocphy_set_fastchg_current(chip);
				}
			}
		}
	} else if (slave_cp_status == 0) {
		main_cp_ibus = chip->master_cp_ichg;

		voocphy_err("main_cp_ibus = %d", main_cp_ibus);
		if (chip->adapter_type == ADAPTER_SVOOC) {
			voocphy_err("adapter type = %d", chip->adapter_type);
			if (main_cp_ibus > chip->voocphy_cp_max_ibus) {
				ibus_trouble_count = ibus_trouble_count + 1;
			} else {
				voocphy_err("Discontinuity satisfies the condition, ibus_trouble_count = 0!\n");
				ibus_trouble_count = 0;
			}
		if (ibus_trouble_count >= 3) {
				voocphy_err("ibus_trouble_count >= 3, limit current to 3A!\n");
				if (chip->current_max > 30) {
					chip->current_max = 30;
					if (!chip->ap_need_change_current)
						chip->ap_need_change_current = oplus_voocphy_set_fastchg_current(chip);
				}
			}
		} else if (chip->adapter_type == ADAPTER_VOOC30
					|| chip->adapter_type == ADAPTER_VOOC20) {
			voocphy_err("adapter type = %d", chip->adapter_type);
			if (main_cp_ibus > chip->voocphy_cp_max_ibus) {
				ibus_trouble_count = ibus_trouble_count + 1;
			} else {
				voocphy_err("Discontinuity satisfies the condition, ibus_trouble_count = 0!\n");
				ibus_trouble_count = 0;
			}
			if (ibus_trouble_count >= 3) {
				voocphy_err("ibus_trouble_count >= 3, limit current to 3A!\n");
				if (chip->current_max > 30) {
					chip->current_max = 30;
					if (!chip->ap_need_change_current)
						chip->ap_need_change_current = oplus_voocphy_set_fastchg_current(chip);
				}
			}
		}
	}

	status = oplus_voocphy_monitor_timer_start(chip, VOOC_THREAD_TIMER_IBUS_CHECK, VOOC_IBUS_CHECK_TIME);

	return status;
}

void oplus_voocphy_monitor_start_work(struct work_struct *work)
{
	struct oplus_voocphy_manager *chip = container_of(work,
			struct oplus_voocphy_manager, monitor_start_work.work);

	oplus_voocphy_safe_event_handle(chip->dev, 0);
	oplus_voocphy_btb_event_handle(chip->dev, 0);
	oplus_voocphy_vol_event_handle(chip->dev, 0);
	oplus_voocphy_temp_event_handle(chip->dev, 0);
	oplus_voocphy_curr_event_handle(chip->dev, 0);
	oplus_voocphy_ap_event_handle(chip->dev, 0);
	oplus_voocphy_ibus_check_event_handle(chip->dev, 0);
}

enum hrtimer_restart oplus_voocphy_monitor_base_timer(struct hrtimer* timer)
{
	int i = 0;
	struct oplus_voocphy_manager *chip = container_of(timer,
				struct oplus_voocphy_manager, monitor_btimer);

	for (i = 0; i < MONITOR_EVENT_NUM; i++) {
		if (chip->mornitor_evt[i].status == VOOC_MONITOR_START) {
			chip->mornitor_evt[i].cnt++;
			if (chip->mornitor_evt[i].cnt == chip->mornitor_evt[i].expires) {
				chip->mornitor_evt[i].timeout = true;
				chip->mornitor_evt[i].cnt = 0;
				chip->mornitor_evt[i].status = VOOC_MONITOR_STOP;
				oplus_vooc_wake_monitor_work(chip);
			}
		}
	}
	hrtimer_forward(&chip->monitor_btimer, chip->monitor_btimer.base->get_time(), chip->moniotr_kt);

	return HRTIMER_RESTART;
}

void oplus_voocphy_monitor_process_events(struct work_struct *work)
{
	int evt_i = 0;
	unsigned long data = 0;
	struct oplus_voocphy_manager *chip = container_of(work,
			struct oplus_voocphy_manager, monitor_work.work);

	/* event loop */
	for (evt_i = 0; evt_i < MONITOR_EVENT_NUM; evt_i++) {
		if (chip->mornitor_evt[evt_i].timeout) {
			chip->mornitor_evt[evt_i].timeout = false; //avoid repeating run monitor event
			data = chip->mornitor_evt[evt_i].data;
			if (chip->mornitor_evt[evt_i].mornitor_hdler)
				chip->mornitor_evt[evt_i].mornitor_hdler(chip->dev, data);
		}
	}

	return;
}

static void oplus_voocphy_monitor_timer_init (struct oplus_voocphy_manager *chip)
{
        if (!chip) {
                voocphy_err("oplus_voocphy_manager is null\n");
                return;
        }

        chip->mornitor_evt[VOOC_THREAD_TIMER_AP].expires = VOOC_AP_EVENT_TIME / VOOC_BASE_TIME_STEP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_AP].data = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_AP].mornitor_hdler = oplus_voocphy_ap_event_handle;
        chip->mornitor_evt[VOOC_THREAD_TIMER_AP].status = VOOC_MONITOR_STOP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_AP].cnt = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_AP].timeout = false;

        chip->mornitor_evt[VOOC_THREAD_TIMER_CURR].expires = VOOC_CURR_EVENT_TIME / VOOC_BASE_TIME_STEP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_CURR].data = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_CURR].mornitor_hdler = oplus_voocphy_curr_event_handle;
        chip->mornitor_evt[VOOC_THREAD_TIMER_CURR].status = VOOC_MONITOR_STOP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_CURR].cnt = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_CURR].timeout = false;

        chip->mornitor_evt[VOOC_THREAD_TIMER_SOC].expires = VOOC_SOC_EVENT_TIME / VOOC_BASE_TIME_STEP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_SOC].data = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_SOC].mornitor_hdler = NULL;
        chip->mornitor_evt[VOOC_THREAD_TIMER_SOC].status = VOOC_MONITOR_STOP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_SOC].cnt = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_SOC].timeout = false;

        chip->mornitor_evt[VOOC_THREAD_TIMER_VOL].expires = VOOC_VOL_EVENT_TIME / VOOC_BASE_TIME_STEP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_VOL].data = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_VOL].mornitor_hdler = oplus_voocphy_vol_event_handle;
        chip->mornitor_evt[VOOC_THREAD_TIMER_VOL].status = VOOC_MONITOR_STOP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_VOL].cnt = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_VOL].timeout = false;

        chip->mornitor_evt[VOOC_THREAD_TIMER_TEMP].expires = VOOC_TEMP_EVENT_TIME / VOOC_BASE_TIME_STEP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_TEMP].data = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_TEMP].mornitor_hdler = oplus_voocphy_temp_event_handle;
        chip->mornitor_evt[VOOC_THREAD_TIMER_TEMP].status = VOOC_MONITOR_STOP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_TEMP].cnt = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_TEMP].timeout = false;

        chip->mornitor_evt[VOOC_THREAD_TIMER_BTB].expires = VOOC_BTB_EVENT_TIME / VOOC_BASE_TIME_STEP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_BTB].data = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_BTB].mornitor_hdler = oplus_voocphy_btb_event_handle;
        chip->mornitor_evt[VOOC_THREAD_TIMER_BTB].status = VOOC_MONITOR_STOP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_BTB].cnt = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_BTB].timeout = false;

        chip->mornitor_evt[VOOC_THREAD_TIMER_SAFE].expires = VOOC_SAFE_EVENT_TIME / VOOC_BASE_TIME_STEP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_SAFE].data = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_SAFE].mornitor_hdler = oplus_voocphy_safe_event_handle;
        chip->mornitor_evt[VOOC_THREAD_TIMER_SAFE].status = VOOC_MONITOR_STOP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_SAFE].cnt = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_SAFE].timeout = false;

        chip->mornitor_evt[VOOC_THREAD_TIMER_COMMU].expires = VOOC_COMMU_EVENT_TIME / VOOC_BASE_TIME_STEP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_COMMU].data = true;
        chip->mornitor_evt[VOOC_THREAD_TIMER_COMMU].mornitor_hdler = oplus_voocphy_commu_process_handle;
        chip->mornitor_evt[VOOC_THREAD_TIMER_COMMU].status = VOOC_MONITOR_STOP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_COMMU].cnt = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_COMMU].timeout = false;

        chip->mornitor_evt[VOOC_THREAD_TIMER_DISCON].expires = VOOC_DISCON_EVENT_TIME / VOOC_BASE_TIME_STEP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_DISCON].data = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_DISCON].mornitor_hdler = NULL;
        chip->mornitor_evt[VOOC_THREAD_TIMER_DISCON].status = VOOC_MONITOR_STOP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_DISCON].cnt = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_DISCON].timeout = false;

        chip->mornitor_evt[VOOC_THREAD_TIMER_FASTCHG_CHECK].expires = VOOC_FASTCHG_CHECK_TIME / VOOC_BASE_TIME_STEP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_FASTCHG_CHECK].data = true;
        chip->mornitor_evt[VOOC_THREAD_TIMER_FASTCHG_CHECK].mornitor_hdler = oplus_voocphy_fastchg_check_process_handle;
        chip->mornitor_evt[VOOC_THREAD_TIMER_FASTCHG_CHECK].status = VOOC_MONITOR_STOP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_FASTCHG_CHECK].cnt = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_FASTCHG_CHECK].timeout = false;

        chip->mornitor_evt[VOOC_THREAD_TIMER_TEST_CHECK].expires = VOOC_TEST_CHECK_TIME / VOOC_BASE_TIME_STEP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_TEST_CHECK].data = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_TEST_CHECK].mornitor_hdler = NULL;
        chip->mornitor_evt[VOOC_THREAD_TIMER_TEST_CHECK].status = VOOC_MONITOR_STOP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_TEST_CHECK].cnt = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_TEST_CHECK].timeout = false;

        chip->mornitor_evt[VOOC_THREAD_TIMER_IBUS_CHECK].expires = VOOC_IBUS_CHECK_TIME / VOOC_BASE_TIME_STEP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_IBUS_CHECK].data = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_IBUS_CHECK].mornitor_hdler = oplus_voocphy_ibus_check_event_handle;
        chip->mornitor_evt[VOOC_THREAD_TIMER_IBUS_CHECK].status = VOOC_MONITOR_STOP;
        chip->mornitor_evt[VOOC_THREAD_TIMER_IBUS_CHECK].cnt = 0;
        chip->mornitor_evt[VOOC_THREAD_TIMER_IBUS_CHECK].timeout = false;

        //init hrtimer
        hrtimer_init(&chip->monitor_btimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        chip->monitor_btimer.function = oplus_voocphy_monitor_base_timer;

        voocphy_dbg( "vooc: create timers successfully");
}

static int oplus_voocphy_parse_batt_curves(struct oplus_voocphy_manager *chip)
{
	struct device_node *node;
	int rc = 0, i, j;
	int length = BATT_SYS_ARRAY*7;
	u32 data[BATT_SYS_ARRAY*7] = {0};
	char buf[32];

	node = chip->dev->of_node;
	chip->parallel_charge_support = of_property_read_bool(node, "parallel_charge_support");
	voocphy_info("parallel_charge_support:%d\n", chip->parallel_charge_support);
	if(chip->parallel_charge_support) {
		for(i = 0; i < 7; i++) {
			sprintf(buf, "svooc_parallel_curve_%d", i);
			voocphy_info("%s\n", buf);
			rc = of_property_read_u32_array(node, buf, svooc_parallel_curve[i].curve_val, 2);
			if (rc) {
				svooc_parallel_curve[i].curve_val[0] = 0;
				svooc_parallel_curve[i].curve_val[1] = 0;
				chg_err("%d,%d\n", svooc_parallel_curve[i].curve_val[0], svooc_parallel_curve[i].curve_val[1]);
				break;
			}
			chg_err("%d,%d\n", svooc_parallel_curve[i].curve_val[0], svooc_parallel_curve[i].curve_val[1]);
		}
	}
	chip->voocphy_dual_cp_support = of_property_read_bool(node,
	                                "oplus_spec,voocphy_dual_cp_support");
	voocphy_info("voocphy_dual_cp_support = %d\n", chip->voocphy_dual_cp_support);

	chip->voocphy_bidirect_cp_support = of_property_read_bool(node, "oplus_spec,voocphy_bidirect_cp_support");
	voocphy_info("voocphy_bidirect_cp_support = %d\n", chip->voocphy_bidirect_cp_support);

	chip->version_judge_support = of_property_read_bool(node, "oplus_spec,version_judge_support");
	voocphy_info("version_judge_support = %d\n", chip->version_judge_support);

	chip->impedance_calculation_newmethod = of_property_read_bool(node, "oplus_spec,impedance_calculation_newmethod");
	voocphy_info("impedance_calculation_newmethod = %d\n", chip->impedance_calculation_newmethod);

	rc = of_property_read_u32(node, "oplus_spec,svooc_circuit_r_l", &chip->svooc_circuit_r_l);
	if (rc) {
		chip->svooc_circuit_r_l = NO_VOOC2_CIRCUIT_PARALLEL_R_L_DEF;
	}
	voocphy_info("svooc_circuit_r_l is 0x%x\n", chip->svooc_circuit_r_l);

	rc = of_property_read_u32(node, "oplus_spec,svooc_circuit_r_h", &chip->svooc_circuit_r_h);
	if (rc) {
		chip->svooc_circuit_r_h = NO_VOOC2_CIRCUIT_PARALLEL_R_H_DEF;
	}
	voocphy_info("svooc_circuit_r_h is 0x%x\n", chip->svooc_circuit_r_h);

	chip->copycat_vooc_support = of_property_read_bool(node,
	                                "oplus_spec,copycat_vooc_support");
	voocphy_info("copycat_vooc_support = %d\n", chip->copycat_vooc_support);

	rc = of_property_read_u32(node, "oplus,ovp_ctrl_cp_index",
				&chip->ovp_ctrl_cpindex);
	if (rc) {
		chip->ovp_ctrl_cpindex = MASTER_CP_ID;
	}
	voocphy_info("ovp_ctrl_cpindex = %d\n", chip->ovp_ctrl_cpindex);

	rc = of_property_read_u32(node, "oplus,cp_ibus_devation",
				&chip->cp_ibus_devation);
	if (rc) {
		chip->cp_ibus_devation = DEFAULT_CP_IBUS_DEVATION;
	}
	voocphy_info("cp_ibus_devation is %d\n", chip->cp_ibus_devation);

	rc = of_property_read_u32(node, "oplus_spec,slave_cp_enable_thr",
	                          &chip->slave_cp_enable_thr);
	if (rc) {
		chip->slave_cp_enable_thr = 1900;
	}
	voocphy_info("slave_cp_enable_thr is %d\n", chip->slave_cp_enable_thr);

	rc = of_property_read_u32(node, "oplus_spec,slave_cp_disable_thr_high",
	                          &chip->slave_cp_disable_thr_high);
	if (rc) {
		chip->slave_cp_disable_thr_high = 1600;
	}
	voocphy_info("slave_cp_disable_thr_high is %d\n", chip->slave_cp_disable_thr_high);

	rc = of_property_read_u32(node, "oplus_spec,slave_cp_disable_thr_low",
	                          &chip->slave_cp_disable_thr_low);
	if (rc) {
		chip->slave_cp_disable_thr_low = 1400;
	}
	voocphy_info("slave_cp_disable_thr_low is %d\n", chip->slave_cp_disable_thr_low);

	rc = of_property_read_u32(node, "oplus_spec,voocphy_current_default",
	                          &chip->current_default);
	if (rc) {
		chip->current_default = 30;
	} else {
		voocphy_info("oplus_spec,voocphy_current_default is %d\n",
		             chip->current_default);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_little_cool_temp",
	                          &chip->vooc_little_cool_temp);
	if (rc) {
		chip->vooc_little_cool_temp = 160;
	} else {
		voocphy_info("oplus_spec,vooc_little_cool_temp is %d\n", chip->vooc_little_cool_temp);
	}
	chip->vooc_little_cool_temp_default = chip->vooc_little_cool_temp;

	rc = of_property_read_u32(node, "oplus_spec,vooc_cool_temp",
	                          &chip->vooc_cool_temp);
	if (rc) {
		chip->vooc_cool_temp = 120;
	} else {
		voocphy_info("oplus_spec,vooc_cool_temp is %d\n", chip->vooc_cool_temp);
	}
	chip->vooc_cool_temp_default = chip->vooc_cool_temp;

	rc = of_property_read_u32(node, "oplus_spec,vooc_little_cold_temp",
	                          &chip->vooc_little_cold_temp);
	if (rc) {
		chip->vooc_little_cold_temp = 50;
	} else {
		voocphy_info("oplus_spec,vooc_little_cold_temp is %d\n", chip->vooc_little_cold_temp);
	}
	chip->vooc_little_cold_temp_default = chip->vooc_little_cold_temp;


	rc = of_property_read_u32(node, "oplus_spec,vooc_normal_low_temp",
	                          &chip->vooc_normal_low_temp);
	if (rc) {
		chip->vooc_normal_low_temp = 250;
	} else {
		voocphy_info("oplus_spec,vooc_normal_low_temp is %d\n", chip->vooc_normal_low_temp);
	}
	chip->vooc_normal_low_temp_default = chip->vooc_normal_low_temp;

	rc = of_property_read_u32(node, "oplus_spec,vooc_normal_high_temp",
	                          &chip->vooc_normal_high_temp);
	if (rc) {
		chip->vooc_normal_high_temp = -EINVAL;
	} else {
		voocphy_dbg("oplus_spec,vooc_normal_high_temp is %d\n", chip->vooc_normal_high_temp);
	}
	chip->vooc_normal_high_temp_default = chip->vooc_normal_high_temp;

	rc = of_property_read_u32(node, "oplus_spec,vooc_warm_vol_thr", &chip->vooc_warm_allow_vol);
	if (rc) {
		chip->vooc_warm_allow_vol = -EINVAL;
	} else {
		voocphy_dbg("oplus_spec,vooc_warm_vol_thr is %d\n", chip->vooc_warm_allow_vol);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_warm_soc_thr", &chip->vooc_warm_allow_soc);
	if (rc) {
		chip->vooc_warm_allow_soc = -EINVAL;
	} else {
		voocphy_dbg("oplus_spec,vooc_warm_soc_thr is %d\n", chip->vooc_warm_allow_soc);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_multistep_initial_batt_temp",
	                          &chip->vooc_multistep_initial_batt_temp);
	if (rc) {
		chip->vooc_multistep_initial_batt_temp = 305;
	} else {
		voocphy_info("oplus_spec,vooc_multistep_initial_batt_temp is %d\n",
		             chip->vooc_multistep_initial_batt_temp);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy_normal_current",
	                          &chip->vooc_strategy_normal_current);
	if (rc) {
		chip->vooc_strategy_normal_current = 0x03;
	} else {
		voocphy_info("oplus_spec,vooc_strategy_normal_current is %d\n",
		             chip->vooc_strategy_normal_current);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_batt_low_temp1",
	                          &chip->vooc_strategy1_batt_low_temp1);
	if (rc) {
		chip->vooc_strategy1_batt_low_temp1  = chip->vooc_multistep_initial_batt_temp;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_batt_low_temp1 is %d\n",
		             chip->vooc_strategy1_batt_low_temp1);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_batt_low_temp2",
	                          &chip->vooc_strategy1_batt_low_temp2);
	if (rc) {
		chip->vooc_strategy1_batt_low_temp2 = chip->vooc_multistep_initial_batt_temp;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_batt_low_temp2 is %d\n",
		             chip->vooc_strategy1_batt_low_temp2);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_batt_low_temp0",
	                          &chip->vooc_strategy1_batt_low_temp0);
	if (rc) {
		chip->vooc_strategy1_batt_low_temp0 = chip->vooc_multistep_initial_batt_temp;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_batt_low_temp0 is %d\n",
		             chip->vooc_strategy1_batt_low_temp0);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_batt_high_temp0",
	                          &chip->vooc_strategy1_batt_high_temp0);
	if (rc) {
		chip->vooc_strategy1_batt_high_temp0 = chip->vooc_multistep_initial_batt_temp;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_batt_high_temp0 is %d\n",
		             chip->vooc_strategy1_batt_high_temp0);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_batt_high_temp1",
	                          &chip->vooc_strategy1_batt_high_temp1);
	if (rc) {
		chip->vooc_strategy1_batt_high_temp1 = chip->vooc_multistep_initial_batt_temp;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_batt_high_temp1 is %d\n",
		             chip->vooc_strategy1_batt_high_temp1);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_batt_high_temp2",
	                          &chip->vooc_strategy1_batt_high_temp2);
	if (rc) {
		chip->vooc_strategy1_batt_high_temp2 = chip->vooc_multistep_initial_batt_temp;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_batt_high_temp2 is %d\n",
		             chip->vooc_strategy1_batt_high_temp2);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_high_current0",
	                          &chip->vooc_strategy1_high_current0);
	if (rc) {
		chip->vooc_strategy1_high_current0  = chip->vooc_strategy_normal_current;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_high_current0 is %d\n",
		             chip->vooc_strategy1_high_current0);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_high_current1",
	                          &chip->vooc_strategy1_high_current1);
	if (rc) {
		chip->vooc_strategy1_high_current1  = chip->vooc_strategy_normal_current;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_high_current1 is %d\n",
		             chip->vooc_strategy1_high_current1);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_high_current2",
	                          &chip->vooc_strategy1_high_current2);
	if (rc) {
		chip->vooc_strategy1_high_current2  = chip->vooc_strategy_normal_current;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_high_current2 is %d\n",
		             chip->vooc_strategy1_high_current2);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_low_current2",
	                          &chip->vooc_strategy1_low_current2);
	if (rc) {
		chip->vooc_strategy1_low_current2  = chip->vooc_strategy_normal_current;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_low_current2 is %d\n",
		             chip->vooc_strategy1_low_current2);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_low_current1",
	                          &chip->vooc_strategy1_low_current1);
	if (rc) {
		chip->vooc_strategy1_low_current1  = chip->vooc_strategy_normal_current;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_low_current1 is %d\n",
		             chip->vooc_strategy1_low_current1);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_low_current0",
	                          &chip->vooc_strategy1_low_current0);
	if (rc) {
		chip->vooc_strategy1_low_current0  = chip->vooc_strategy_normal_current;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_low_current0 is %d\n",
		             chip->vooc_strategy1_low_current0);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_high_current0_vooc",
	                          &chip->vooc_strategy1_high_current0_vooc);
	if (rc) {
		chip->vooc_strategy1_high_current0_vooc = chip->vooc_strategy1_high_current0;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_high_current0_vooc is %d\n",
		             chip->vooc_strategy1_high_current0_vooc);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_high_current1_vooc",
	                          &chip->vooc_strategy1_high_current1_vooc);
	if (rc) {
		chip->vooc_strategy1_high_current1_vooc = chip->vooc_strategy1_high_current1;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_high_current1_vooc is %d\n",
		             chip->vooc_strategy1_high_current1_vooc);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_high_current2_vooc",
	                          &chip->vooc_strategy1_high_current2_vooc);
	if (rc) {
		chip->vooc_strategy1_high_current2_vooc = chip->vooc_strategy1_high_current2;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_high_current2_vooc is %d\n",
		             chip->vooc_strategy1_high_current2_vooc);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_low_current2_vooc",
	                          &chip->vooc_strategy1_low_current2_vooc);
	if (rc) {
		chip->vooc_strategy1_low_current2_vooc = chip->vooc_strategy1_low_current2;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_low_current2_vooc is %d\n",
		             chip->vooc_strategy1_low_current2_vooc);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_low_current1_vooc",
	                          &chip->vooc_strategy1_low_current1_vooc);
	if (rc) {
		chip->vooc_strategy1_low_current1_vooc = chip->vooc_strategy1_low_current1;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_low_current1_vooc is %d\n",
		             chip->vooc_strategy1_low_current1_vooc);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy1_low_current0_vooc",
	                          &chip->vooc_strategy1_low_current0_vooc);
	if (rc) {
		chip->vooc_strategy1_low_current0_vooc = chip->vooc_strategy1_low_current0;
	} else {
		voocphy_info("oplus_spec,vooc_strategy1_low_current0_vooc is %d\n",
		             chip->vooc_strategy1_low_current0_vooc);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy2_batt_up_temp1",
	                          &chip->vooc_strategy2_batt_up_temp1);
	if (rc) {
		chip->vooc_strategy2_batt_up_temp1  = chip->vooc_multistep_initial_batt_temp;
	} else {
		voocphy_info("oplus_spec,vooc_strategy2_batt_up_temp1 is %d\n",
		             chip->vooc_strategy2_batt_up_temp1);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy2_batt_up_down_temp2",
	                          &chip->vooc_strategy2_batt_up_down_temp2);
	if (rc) {
		chip->vooc_strategy2_batt_up_down_temp2  = chip->vooc_multistep_initial_batt_temp;
	} else {
		voocphy_info("oplus_spec,vooc_strategy2_batt_up_down_temp2 is %d\n",
		             chip->vooc_strategy2_batt_up_down_temp2);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy2_batt_up_temp3",
	                          &chip->vooc_strategy2_batt_up_temp3);
	if (rc) {
		chip->vooc_strategy2_batt_up_temp3  = chip->vooc_multistep_initial_batt_temp;
	} else {
		voocphy_info("oplus_spec,vooc_strategy2_batt_up_temp3 is %d\n",
		             chip->vooc_strategy2_batt_up_temp3);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy2_batt_up_down_temp4",
	                          &chip->vooc_strategy2_batt_up_down_temp4);
	if (rc) {
		chip->vooc_strategy2_batt_up_down_temp4  = chip->vooc_multistep_initial_batt_temp;
	} else {
		voocphy_info("oplus_spec,vooc_strategy2_batt_up_down_temp4 is %d\n",
		             chip->vooc_strategy2_batt_up_down_temp4);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy2_batt_up_temp5",
	                          &chip->vooc_strategy2_batt_up_temp5);
	if (rc) {
		chip->vooc_strategy2_batt_up_temp5  = chip->vooc_multistep_initial_batt_temp;
	} else {
		voocphy_info("oplus_spec,vooc_strategy2_batt_up_temp5 is %d\n",
		             chip->vooc_strategy2_batt_up_temp5);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy2_batt_up_temp6",
	                          &chip->vooc_strategy2_batt_up_temp6);
	if (rc) {
		chip->vooc_strategy2_batt_up_temp6  = chip->vooc_multistep_initial_batt_temp;
	} else {
		voocphy_info("oplus_spec,vooc_strategy2_batt_up_temp6 is %d\n",
		             chip->vooc_strategy2_batt_up_temp6);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy2_high0_current",
	                          &chip->vooc_strategy2_high0_current);
	if (rc) {
		chip->vooc_strategy2_high0_current	= chip->vooc_strategy_normal_current;
	} else {
		voocphy_info("oplus_spec,vooc_strategy2_high0_current is %d\n",
		             chip->vooc_strategy2_high0_current);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy2_high1_current",
	                          &chip->vooc_strategy2_high1_current);
	if (rc) {
		chip->vooc_strategy2_high1_current	= chip->vooc_strategy_normal_current;
	} else {
		voocphy_info("oplus_spec,vooc_strategy2_high1_current is %d\n",
		             chip->vooc_strategy2_high1_current);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy2_high2_current",
	                          &chip->vooc_strategy2_high2_current);
	if (rc) {
		chip->vooc_strategy2_high2_current	= chip->vooc_strategy_normal_current;
	} else {
		voocphy_info("oplus_spec,vooc_strategy2_high2_current is %d\n",
		             chip->vooc_strategy2_high2_current);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_strategy2_high3_current",
	                          &chip->vooc_strategy2_high3_current);
	if (rc) {
		chip->vooc_strategy2_high3_current	= chip->vooc_strategy_normal_current;
	} else {
		voocphy_info("oplus_spec,vooc_strategy2_high3_current is %d\n",
		             chip->vooc_strategy2_high3_current);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_low_soc", &chip->vooc_low_soc);
	if (rc) {
		chip->vooc_low_soc = VOOC_LOW_SOC;
	} else {
		voocphy_dbg("oplus_spec,vooc_low_soc is %d\n", chip->vooc_low_soc);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_high_soc", &chip->vooc_high_soc);
	if (rc) {
		chip->vooc_high_soc = VOOC_HIGH_SOC;
	} else {
		voocphy_dbg("oplus_spec,vooc_high_soc is %d\n", chip->vooc_high_soc);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_low_temp",
		&chip->vooc_low_temp);
	if (rc) {
		chip->vooc_low_temp = VOOC_LOW_TEMP;
	} else {
		voocphy_dbg("oplus_spec,vooc_low_temp is %d\n", chip->vooc_low_temp);
	}

	chip->vooc_batt_over_low_temp = chip->vooc_low_temp - 5;

	rc = of_property_read_u32(node, "oplus_spec,vooc_high_temp", &chip->vooc_high_temp);
	if (rc) {
		chip->vooc_high_temp = VOOC_HIGH_TEMP;
	} else {
		voocphy_dbg("oplus_spec,vooc_high_temp is %d\n", chip->vooc_high_temp);
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_over_high_temp",
	                          &chip->vooc_batt_over_high_temp);
	if (rc) {
		chip->vooc_batt_over_high_temp = -EINVAL;
	} else {
		voocphy_info("oplus_spec,vooc_over_high_temp is %d\n",
		             chip->vooc_batt_over_high_temp);
	}

	rc = of_property_read_u32_array(node, "svooc_batt_sys_curve",
	                                data, length);
	if (rc < 0) {
		voocphy_info("Couldn't read svooc_batt_sys_curve rc = %d\n", rc);
		return rc;
	}

	for(i=0; i<BATT_SYS_ARRAY; i++) {
		for(j=0; j<7; j++) {
			svooc_batt_sys_curve[i][j] = data[i*7 + j];
		}
	}

	rc = of_property_read_u32_array(node, "vooc_batt_sys_curve",
	                                data, length);
	if (rc < 0) {
		voocphy_info("Couldn't read vooc_batt_sys_curve rc = %d\n", rc);
		return rc;
	}

	for(i=0; i<BATT_SYS_ARRAY; i++) {
		for(j=0; j<7; j++) {
			vooc_batt_sys_curve[i][j] = data[i*7 + j];
		}
	}

	chip->external_gauge_support = of_property_read_bool(node,
	                               "oplus_spec,external_gauge_support");
	voocphy_info("external_gauge_support = %d\n", chip->external_gauge_support);

	rc = of_property_count_elems_of_size(node, "oplus_spec,svooc_cool_down_current_limit", sizeof(u32));
	if (rc < 0) {
		voocphy_info("Count svooc_cool_down_current_limit failed, rc=%d\n", rc);
		chip->svooc_cool_down_num = 0;
		return rc;
	}

	length = rc;

	chip->svooc_cool_down_num = length;

	voocphy_info("parse svooc_cool_down_current_limit, size=%d svooc_cool_down_num =%d\n", length, chip->svooc_cool_down_num);

	rc = of_property_read_u32_array(node, "oplus_spec,svooc_cool_down_current_limit",
	                                chip->svooc_cool_down_current_limit,
	                                length);
	if (rc < 0) {
		voocphy_info("parse svooc_cool_down_current_limit failed, rc=%d\n", rc);
		return rc;
	}

	rc = of_property_count_elems_of_size(node, "oplus_spec,vooc_cool_down_current_limit", sizeof(u32));
	if (rc < 0) {
		voocphy_info("Count vooc_cool_down_current_limit failed, rc=%d\n", rc);
		chip->vooc_cool_down_num = 0;
		return rc;
	}

	length = rc;

	chip->vooc_cool_down_num = length;

	voocphy_info("parse vooc_cool_down_current_limit, size=%d vooc_cool_down_num =%d\n", length, chip->vooc_cool_down_num);

	rc = of_property_read_u32_array(node, "oplus_spec,vooc_cool_down_current_limit",
	                                chip->vooc_cool_down_current_limit,
	                                length);
	if (rc < 0) {
		voocphy_info("parse vooc_cool_down_current_limit failed, rc=%d\n", rc);
		return rc;
	}

	rc = of_property_read_u32(node, "oplus_spec,fastchg_timeout_time_init",
	                                &chip->fastchg_timeout_time_init);
	if (rc < 0) {
		voocphy_info("parse fastchg_timeout_time_init failed, rc=%d\n", rc);
		chip->fastchg_timeout_time_init = FASTCHG_TOTAL_TIME;
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_little_cold_full_voltage",
	                                &chip->vooc_little_cold_full_voltage);
	if (rc < 0) {
		voocphy_info("parse vooc_little_cold_full_voltage failed, rc=%d\n", rc);
		chip->vooc_little_cold_full_voltage = TEMP0_2_TEMP45_FULL_VOLTAGE;
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_cool_full_voltage",
	                                &chip->vooc_cool_full_voltage);
	if (rc < 0) {
		voocphy_info("parse vooc_cool_full_voltage failed, rc=%d\n", rc);
		chip->vooc_cool_full_voltage = TEMP45_2_TEMP115_FULL_VOLTAGE;
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_warm_full_voltage",
	                                &chip->vooc_warm_full_voltage);
	if (rc < 0) {
		voocphy_info("parse vooc_warm_full_voltage failed, rc=%d\n", rc);
		chip->vooc_warm_full_voltage = -EINVAL;
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_1time_full_voltage",
	                                &chip->vooc_1time_full_voltage);
	if (rc < 0) {
		voocphy_info("parse vooc_1time_full_voltage failed, rc=%d\n", rc);
		chip->vooc_1time_full_voltage = BAT_FULL_1TIME_THD;
	}

	rc = of_property_read_u32(node, "oplus_spec,vooc_ntime_full_voltage",
	                                &chip->vooc_ntime_full_voltage);
	if (rc < 0) {
		voocphy_info("parse vooc_ntime_full_voltage failed, rc=%d\n", rc);
		chip->vooc_ntime_full_voltage = BAT_FULL_NTIME_THD;
	}

	rc = of_property_read_u32(node, "oplus_spec,low_curr_full_t1",
	                                &chip->low_curr_full_t1);
	if (rc < 0) {
		voocphy_info("parse low_curr_full_t1 failed, rc=%d\n", rc);
		chip->low_curr_full_t1 = 120;
	}

	rc = of_property_read_u32(node, "oplus_spec,low_curr_full_t2",
	                                &chip->low_curr_full_t2);
	if (rc < 0) {
		voocphy_info("parse low_curr_full_t2 failed, rc=%d\n", rc);
		chip->low_curr_full_t2 = 350;
	}

	rc = of_property_read_u32(node, "oplus_spec,low_curr_full_t3",
	                                &chip->low_curr_full_t3);
	if (rc < 0) {
		voocphy_info("parse low_curr_full_t3 failed, rc=%d\n", rc);
		chip->low_curr_full_t3 = 430;
	}

	rc = of_property_count_elems_of_size(node, "oplus_spec,range1_low_curr_full", sizeof(u32));
	if (rc < 0) {
		voocphy_info("Count range1_low_curr_full failed, rc=%d\n", rc);
		rc = 0;
	}
	length = rc;
	chip->range1_low_curr_full_num = length/2;
	if (length != 0 && length % 2 == 0) {
		chip->range1_low_curr_full = devm_kzalloc(chip->dev, chip->range1_low_curr_full_num * sizeof(struct low_curr_full_condition), GFP_KERNEL);

		if (chip->range1_low_curr_full) {
			rc = of_property_read_u32_array(node, "oplus_spec,range1_low_curr_full",
			                                (u32 *)chip->range1_low_curr_full,
			                                length);
			if (rc < 0) {
				voocphy_info("parse range1_low_curr_full failed, rc=%d\n", rc);
				devm_kfree(chip->dev, chip->range1_low_curr_full);
				chip->range1_low_curr_full_num = 0;
			} else {
				voocphy_info("range1_low_curr_full length =%d\n", chip->range1_low_curr_full_num);
				for (i = 0; i < chip->range1_low_curr_full_num; i++) {
					voocphy_info("curr: %d , vbatt: %d\n", chip->range1_low_curr_full[i].curr, chip->range1_low_curr_full[i].vbatt);
				}
			}
		}
	} else {
		voocphy_info("range1_low_curr_full error length\n");
	}

	rc = of_property_count_elems_of_size(node, "oplus_spec,range2_low_curr_full", sizeof(u32));
	if (rc < 0) {
		voocphy_info("Count range2_low_curr_full failed, rc=%d\n", rc);
		rc = 0;
	}
	length = rc;
	chip->range2_low_curr_full_num = length/2;
	if (length != 0 && length % 2 == 0) {
		chip->range2_low_curr_full = devm_kzalloc(chip->dev, chip->range2_low_curr_full_num * sizeof(struct low_curr_full_condition), GFP_KERNEL);

		if (chip->range2_low_curr_full) {
			rc = of_property_read_u32_array(node, "oplus_spec,range2_low_curr_full",
			                                (u32 *)chip->range2_low_curr_full,
			                                length);
			if (rc < 0) {
				voocphy_info("parse range2_low_curr_full failed, rc=%d\n", rc);
				devm_kfree(chip->dev, chip->range2_low_curr_full);
				chip->range2_low_curr_full_num = 0;
			} else {
				voocphy_info("range2_low_curr_full length =%d\n", chip->range2_low_curr_full_num);
				for (i = 0; i < chip->range2_low_curr_full_num; i++) {
					voocphy_info("curr: %d , vbatt: %d\n", chip->range2_low_curr_full[i].curr, chip->range2_low_curr_full[i].vbatt);
				}
			}
		}
	} else {
		voocphy_info("range2_low_curr_full error length\n");
	}

	if (chip->parallel_charge_support) {
		rc = of_property_count_elems_of_size(node, "oplus_spec,sub_range1_low_curr_full", sizeof(u32));
		if (rc < 0) {
			voocphy_info("Count sub_range1_low_curr_full failed, rc=%d\n", rc);
			rc = 0;
		}
		length = rc;
		chip->sub_range1_low_curr_full_num = length/2;
		if (length != 0 && length % 2 == 0) {
			chip->sub_range1_low_curr_full = devm_kzalloc(chip->dev,
				chip->sub_range1_low_curr_full_num * sizeof(struct low_curr_full_condition), GFP_KERNEL);

			if (chip->sub_range1_low_curr_full) {
				rc = of_property_read_u32_array(node, "oplus_spec,sub_range1_low_curr_full",
				                                (u32 *)chip->sub_range1_low_curr_full,
				                                length);
				if (rc < 0) {
					voocphy_info("parse sub_range1_low_curr_full failed, rc=%d\n", rc);
					devm_kfree(chip->dev, chip->sub_range1_low_curr_full);
					chip->sub_range1_low_curr_full_num = 0;
				} else {
					voocphy_info("sub_range1_low_curr_full length =%d\n", chip->sub_range1_low_curr_full_num);
					for (i = 0; i < chip->sub_range1_low_curr_full_num; i++) {
						voocphy_info("curr: %d , vbatt: %d\n", chip->sub_range1_low_curr_full[i].curr, chip->sub_range1_low_curr_full[i].vbatt);
					}
				}
			}
		} else {
			voocphy_info("sub_range1_low_curr_full error length\n");
		}

		rc = of_property_count_elems_of_size(node, "oplus_spec,sub_range2_low_curr_full", sizeof(u32));
		if (rc < 0) {
			voocphy_info("Count sub_range2_low_curr_full failed, rc=%d\n", rc);
			rc = 0;
		}
		length = rc;
		chip->sub_range2_low_curr_full_num = length/2;
		if (length != 0 && length % 2 == 0) {
			chip->sub_range2_low_curr_full = devm_kzalloc(chip->dev,
				chip->sub_range2_low_curr_full_num * sizeof(struct low_curr_full_condition), GFP_KERNEL);
			if (chip->sub_range2_low_curr_full) {
				rc = of_property_read_u32_array(node, "oplus_spec,sub_range2_low_curr_full",
				                                (u32 *)chip->sub_range2_low_curr_full,
				                                length);
				if (rc < 0) {
					voocphy_info("parse sub_range2_low_curr_full failed, rc=%d\n", rc);
					devm_kfree(chip->dev, chip->sub_range2_low_curr_full);
					chip->sub_range2_low_curr_full_num = 0;
				} else {
					voocphy_info("sub_range2_low_curr_full length =%d\n", chip->sub_range2_low_curr_full_num);
					for (i = 0; i < chip->sub_range2_low_curr_full_num; i++) {
						voocphy_info("curr: %d , vbatt: %d\n", chip->sub_range2_low_curr_full[i].curr, chip->sub_range2_low_curr_full[i].vbatt);
					}
				}
			}
		} else {
			voocphy_info("sub_range2_low_curr_full error length\n");
		}
	}

	rc = of_property_read_u32(node, "oplus_spec,voocphy_freq_mincore",
	                          &chip->voocphy_freq_mincore);
	if (rc) {
		chip->voocphy_freq_mincore = 925000;
	}
	voocphy_info("voocphy_freq_mincore is %d\n", chip->voocphy_freq_mincore);

	rc = of_property_read_u32(node, "oplus_spec,voocphy_freq_midcore",
	                          &chip->voocphy_freq_midcore);
	if (rc) {
		chip->voocphy_freq_midcore = 925000;
	}
	voocphy_info("voocphy_freq_midcore is %d\n", chip->voocphy_freq_midcore);

	rc = of_property_read_u32(node, "oplus_spec,voocphy_freq_maxcore",
	                          &chip->voocphy_freq_maxcore);
	if (rc) {
		chip->voocphy_freq_maxcore = 925000;
	}
	voocphy_info("voocphy_freq_maxcore is %d\n", chip->voocphy_freq_maxcore);

	rc = of_property_read_u32(node, "oplus_spec,voocphy_current_change_timeout",
	                          &chip->voocphy_current_change_timeout);
	if (rc) {
		chip->voocphy_current_change_timeout = 100;
	}
	voocphy_info("voocphy_current_change_timeout is %d\n", chip->voocphy_current_change_timeout);

	rc = of_property_read_u32(node, "oplus_spec,voocphy_ibat_over_current",
	                          &chip->voocphy_ibat_over_current);
	if (rc) {
		chip->voocphy_ibat_over_current = VOOC_OVER_CURRENT_VALUE;
	}
	voocphy_info("voocphy_ibat_over_current is %d\n", chip->voocphy_ibat_over_current);

	rc = of_property_read_u32(node, "oplus_spec,voocphy_cp_max_ibus",
	                          &chip->voocphy_cp_max_ibus);
	if (rc) {
		chip->voocphy_cp_max_ibus = 3200;
	}
	voocphy_info("voocphy_cp_max_ibus is %d\n", chip->voocphy_cp_max_ibus);
	rc = of_property_read_u32(node, "oplus_spec,voocphy_svooc_cp_max_ibus",
	                          &chip->voocphy_svooc_cp_max_ibus);
	if (rc) {
		chip->voocphy_svooc_cp_max_ibus = VOOCPHY_SVOOC_CP_MAX_IBUS_DEFAULT;
	}
	voocphy_info("voocphy_svooc_cp_max_ibus is %d\n", chip->voocphy_svooc_cp_max_ibus);
	rc = of_property_read_u32(node, "oplus_spec,voocphy_vooc_cp_max_ibus",
	                          &chip->voocphy_vooc_cp_max_ibus);
	if (rc) {
		chip->voocphy_vooc_cp_max_ibus = VOOCPHY_VOOC_CP_MAX_IBUS_DEFAULT;
	}
	voocphy_info("voocphy_vooc_cp_max_ibus is %d\n", chip->voocphy_vooc_cp_max_ibus);

	rc = of_property_read_u32(node, "oplus_spec,voocphy_max_main_ibat", &chip->voocphy_max_main_ibat);
	if (rc) {
		chip->voocphy_max_main_ibat = 3600;
	}
	voocphy_info("voocphy_max_main_ibat is %d\n", chip->voocphy_max_main_ibat);

	rc = of_property_read_u32(node, "oplus_spec,voocphy_max_sub_ibat", &chip->voocphy_max_sub_ibat);
	if (rc) {
		chip->voocphy_max_sub_ibat = 3000;
	}
	voocphy_info("voocphy_max_sub_ibat is %d\n", chip->voocphy_max_sub_ibat);

	rc = of_property_read_u32(node, "oplus_spec,batt_pwd_curr_thd1", &chip->batt_pwd_curr_thd1);
	if (rc) {
		chip->batt_pwd_curr_thd1 = BATT_PWD_CURR_THD1_DEFAULT;
	}
	voocphy_info("batt_pwd_curr_thd1 is %d\n", chip->batt_pwd_curr_thd1);

	rc = of_property_read_u32(node, "oplus_spec,batt_pwd_vol_thd1", &chip->batt_pwd_vol_thd1);
	if (rc) {
		chip->batt_pwd_vol_thd1 = BATT_PWD_VOL_THD1_DEFAULT;
	}
	voocphy_info("batt_pwd_vol_thd1 is %d\n", chip->batt_pwd_vol_thd1);

	oplus_voocphy_parse_svooc_batt_curves(chip);

	oplus_voocphy_parse_vooc_batt_curves(chip);

	return 0;
}

static int oplus_voocphy_variables_init(struct oplus_voocphy_manager *chip)
{
	int status = VOOCPHY_SUCCESS;

	chip->voocphy_rx_buff = 0;
	chip->voocphy_tx_buff[0] = 0;
	chip->voocphy_tx_buff[1] = 0;
	chip->adapter_check_ok = false;
	chip->adapter_ask_check_count = 3;
	chip->adapter_model_detect_count = 3;
	chip->adapter_is_vbus_ok_count = 0;
	chip->adapter_ask_fastchg_ornot_count = 0;
	chip->fastchg_adapter_ask_cmd = VOOC_CMD_UNKNOW;
	chip->vooc2_next_cmd = VOOC_CMD_ASK_FASTCHG_ORNOT;
	chip->adapter_check_cmmu_count = 0;
	chip->adapter_rand_h = 0;
	chip->adapter_rand_l = 0;
	chip->code_id_far = 0;
	chip->code_id_local = 0xFFFF;
	chip->code_id_temp_h = 0;
	chip->code_id_temp_l = 0;
	chip->adapter_model_ver = 0;
	chip->adapter_model_count = 0;
	chip->ask_batt_sys = 0;
	chip->current_expect = chip->current_default;
	chip->current_max = chip->current_default;
	chip->current_spec = chip->current_default;
	chip->current_ap = chip->current_default;
	chip->current_batt_temp = chip->current_default;
	chip->batt_temp_plugin = VOOCPHY_BATT_TEMP_NORMAL;
	chip->adapter_rand_start = false;
	chip->adapter_type = ADAPTER_SVOOC;
	chip->adapter_mesg = 0;
	chip->fastchg_allow = false;
	chip->ask_vol_again = false;
	chip->ap_need_change_current = 0;
	chip->adjust_curr = ADJ_CUR_STEP_DEFAULT;
	chip->adjust_fail_cnt = 0;
	chip->pre_adapter_ask_cmd = VOOC_CMD_UNKNOW;
	chip->ignore_first_frame = true;
	chip->vooc_vbus_status = VOOC_VBUS_UNKNOW;
	chip->vooc_head = VOOC_DEFAULT_HEAD;
	chip->ask_vooc3_detect = false;
	chip->force_2a = false;
	chip->force_3a = false;
	chip->force_2a_flag = false;
	chip->force_3a_flag = false;
	chip->btb_temp_over = false;
	chip->btb_err_first = false;
	chip->fastchg_notify_status = FAST_NOTIFY_UNKNOW;
	chip->fastchg_timeout_time = chip->fastchg_timeout_time_init;
	chip->fastchg_3c_timeout_time = FASTCHG_3000MA_TIMEOUT;
	chip->gauge_vbatt = 0;
	chip->vbus = 0;
	chip->batt_soc = 50;
	chip->fastchg_batt_temp_status = BAT_TEMP_NATURAL;
	chip->vooc_temp_cur_range = FASTCHG_TEMP_RANGE_INIT;
	chip->fastchg_start = false;
	chip->fastchg_commu_ing = false;
	chip->fastchg_to_normal = false;
	chip->fastchg_to_warm_full = false;
	chip->fastchg_to_warm = false;
	chip->fastchg_dummy_start = false;
	chip->fastchg_real_allow = false;
	chip->fastchg_commu_stop = false;
	chip->fastchg_check_stop = false;
	chip->fastchg_monitor_stop = false;
	chip->current_pwd = chip->current_expect;
	chip->curr_pwd_count = 10;
	chip->usb_bad_connect = false;
	chip->vooc_move_head = false;
	chip->fastchg_err_commu = false;
	chip->fastchg_reactive = false;
	chip->ask_bat_model_finished = false;
	oplus_voocphy_set_user_exit_fastchg(chip, false);
	chip->fastchg_need_reset = 0;
	chip->fastchg_recover_cnt = 0;
	chip->fastchg_recovering = false;
	chip->copycat_vooc_adapter = false;

	/* note exception info */
	chip->vooc_flag = 0;
	chip->interrupt_flag = 0;
	chip->irq_total_num = 0;
	chip->irq_rcvok_num = 0;
	chip->irq_rcverr_num = 0;
	chip->irq_tx_timeout_num = 0;
	chip->irq_tx_timeout = 0;
	chip->irq_hw_timeout_num = 0;
	chip->irq_rxdone_num = 0;
	chip->adapter_check_vooc_head_count = 0;
	chip->adapter_check_cmd_data_count = 0;
	chip->max_div_cp_ichg = 0;
	chip->vbat_deviation_check = 0;
	chip->voocphy_iic_err_num = 0;
	chip->slave_voocphy_iic_err_num = 0;
	chip->master_error_ibus = 0;
	chip->slave_error_ibus = 0;
	chip->error_current_expect = 0;
	chip->irq_tx_fail_num = 0;
	chip->start_vaild_frame = false;
	chip->reply_bat_model_end = false;
	if (chip->ops->get_chip_id)
		chip->chip_id = chip->ops->get_chip_id(chip);
	else
		chip->chip_id = 0;
	memset(chip->int_column, 0, sizeof(chip->int_column));
	memset(chip->int_column_pre, 0, sizeof(chip->int_column_pre));

	/* default vooc head as svooc */
	status = oplus_voocphy_write_mesg_mask(VOOC_INVERT_HEAD_MASK,
	                                       &chip->voocphy_tx_buff[0], chip->vooc_head);

	return status;
}

static void oplus_voocphy_clear_dbg_info(struct oplus_voocphy_manager *chip)
{
	if (!chip)
		return;

	chip->ap_disconn_issue = 0;
	chip->disconn_pre_ibat = 0;
	chip->disconn_pre_ibus = 0;
	chip->disconn_pre_vbat = 0;
	chip->disconn_pre_vbat_calc = 0;
	chip->disconn_pre_vbus = 0;
	chip->r_state = 0;
	chip->vbus_adjust_cnt = 0;
	chip->voocphy_cp_irq_flag = 0;
	chip->voocphy_enable = 0;
	chip->slave_voocphy_enable = 0;
	chip->voocphy_iic_err = 0;
	chip->slave_voocphy_iic_err = 0;
	chip->voocphy_vooc_irq_flag = 0;
	chip->vbat_deviation_check = 0;
	memset(chip->reg_dump, 0, DUMP_REG_CNT);
	memset(chip->slave_reg_dump, 0, DUMP_REG_CNT);
}

static void oplus_voocphy_clear_cnt_info(struct oplus_voocphy_manager *chip)
{
	if (chip) {
		chip->ap_handle_timeout_num = 0;
		chip->rcv_done_200ms_timeout_num = 0;
		chip->rcv_date_err_num = 0;
	}
}

static int voocphy_irq_gpio_init(struct oplus_voocphy_manager *chip)
{
	int rc;
	struct device_node *node = chip->dev->of_node;

	chip->irq_gpio = of_get_named_gpio(node,
	                                   "oplus_spec,irq_gpio", 0);
	if (chip->irq_gpio < 0) {
		voocphy_err("chip->irq_gpio not specified\n");
	} else {
		if (gpio_is_valid(chip->irq_gpio)) {
			rc = gpio_request(chip->irq_gpio,
			                  "irq_gpio");
			if (rc) {
				voocphy_info("unable to request gpio [%d]\n",
					     chip->irq_gpio);
			}
		}
		voocphy_err("chip->irq_gpio =%d\n", chip->irq_gpio);
	}

	chip->irq = gpio_to_irq(chip->irq_gpio);
	chip->pinctrl = devm_pinctrl_get(chip->dev);
	if (IS_ERR_OR_NULL(chip->pinctrl)) {
		voocphy_err("get pinctrl fail\n");
		return -EINVAL;
	}

	chip->charging_inter_active =
	    pinctrl_lookup_state(chip->pinctrl, "charging_inter_active");
	if (IS_ERR_OR_NULL(chip->charging_inter_active)) {
		voocphy_err("failed to get the pinctrl state(%d)\n", __LINE__);
		return -EINVAL;
	}

	chip->charging_inter_sleep =
	    pinctrl_lookup_state(chip->pinctrl, "charging_inter_sleep");
	if (IS_ERR_OR_NULL(chip->charging_inter_sleep)) {
		voocphy_err("Failed to get the pinctrl state(%d)\n", __LINE__);
		return -EINVAL;
	}

	gpio_direction_input(chip->irq_gpio);
	pinctrl_select_state(chip->pinctrl, chip->charging_inter_active); /* no_PULL */
	rc = gpio_get_value(chip->irq_gpio);
	voocphy_info("irq_gpio = %d, irq_gpio_stat = %d\n", chip->irq_gpio, rc);

	return 0;
}

static int voocphy_irq_register(struct oplus_voocphy_manager *chip)
{
	int ret;

	ret = voocphy_irq_gpio_init(chip);
	if (ret < 0) {
		voocphy_err("failed to irq gpio init(%d)\n", ret);
		return ret;
	}

	if (chip->irq) {
		ret = request_threaded_irq(chip->irq, NULL,
					   oplus_voocphy_interrupt_handler,
					   IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
					   "voocphy_irq", chip);
		if (ret < 0) {
			voocphy_err("request irq for irq=%d failed, ret =%d\n",
				    chip->irq, ret);
			return ret;
		}
		enable_irq_wake(chip->irq);
		pr_debug("request irq ok\n");
	}

	return 0;
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
void update_highcap_mask(struct cpumask *cpu_highcap_mask)
{
	unsigned int cpu, min_capacity = arch_scale_cpu_capacity(0);

	for_each_possible_cpu(cpu) {
		if (arch_scale_cpu_capacity(cpu) <= min_capacity)
			min_capacity = arch_scale_cpu_capacity(cpu);
		cpumask_set_cpu(cpu, cpu_highcap_mask);
	}
	for_each_possible_cpu(cpu) {
		if (arch_scale_cpu_capacity(cpu) == min_capacity)
			cpumask_clear_cpu(cpu, cpu_highcap_mask);
	}
	voocphy_info("cpu_highcap_mask:%lx\n", cpumask_bits(cpu_highcap_mask)[0]);
}
#endif

static int oplus_voocphy_init(struct oplus_voocphy_manager *chip)
{
	struct irq_desc *desc;
	struct cpumask current_mask;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	cpumask_var_t cpu_highcap_mask;
#endif
	int ret;

	oplus_voocphy_parse_dt(chip);
	oplus_voocphy_parse_batt_curves(chip);
	oplus_voocphy_variables_init(chip);
	oplus_voocphy_monitor_timer_init(chip);
	atomic_set(&chip->voocphy_freq_state, 0);
	INIT_DELAYED_WORK(&(chip->voocphy_service_work), voocphy_service);
	INIT_DELAYED_WORK(&(chip->notify_fastchg_work), oplus_voocphy_notify_fastchg_work);
	INIT_DELAYED_WORK(&(chip->monitor_work), oplus_voocphy_monitor_process_events);
	INIT_DELAYED_WORK(&(chip->monitor_start_work), oplus_voocphy_monitor_start_work);
	INIT_DELAYED_WORK(&(chip->modify_cpufeq_work), oplus_voocphy_modify_cpufeq_work);
	INIT_DELAYED_WORK(&(chip->check_chg_out_work), oplus_voocphy_check_chg_out_work);
	INIT_DELAYED_WORK(&chip->voocphy_send_ongoing_notify, oplus_ap_voocphy_send_ongoing_notify);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	INIT_DELAYED_WORK(&(chip->clear_boost_work), oplus_voocphy_clear_boost_work);
#endif
	if (chip->ops && chip->ops->hardware_init)
		chip->ops->hardware_init(chip);

	ret = voocphy_irq_register(chip);
	if (ret < 0) {
		return ret;
	}

	desc = irq_to_desc(chip->irq);
	if (desc == NULL) {
		voocphy_info("desc is null\n");
		return -ENODEV;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 0))
	update_highcap_mask(cpu_highcap_mask);
	cpumask_and(&current_mask, cpu_online_mask, cpu_highcap_mask);
#else
	cpumask_setall(&current_mask);
	cpumask_and(&current_mask, cpu_online_mask, &current_mask);
#endif
	ret = set_cpus_allowed_ptr(desc->action->thread, &current_mask);
	g_voocphy_chip = chip;

	oplus_voocphy_clear_dbg_info(chip);
	oplus_voocphy_clear_cnt_info(chip);
	init_proc_voocphy_debug(chip);

	return 0;
}

void oplus_voocphy_slave_init(struct oplus_voocphy_manager *chip)
{
	if (!chip) {
		voocphy_info("%s chip null\n", __func__);
		return;
        }
	g_voocphy_chip->slave_client = chip->slave_client;
	g_voocphy_chip->slave_dev = chip->slave_dev;
	g_voocphy_chip->slave_ops = chip->slave_ops;
}

static void oplus_apvphy_set_screenoff_current(struct device *dev, int curr_ma)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	chip->screenoff_current = curr_ma;
}

static void oplus_apvphy_set_cool_down(struct device *dev, int cool_down)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	chip->cool_down = cool_down;
	chip->current_ap = oplus_voocphy_get_ap_current(chip);
	if (!chip->ap_need_change_current)
		chip->ap_need_change_current = oplus_voocphy_set_fastchg_current(chip);
}

static void oplus_apvphy_set_chg_auto_mode(struct device *dev, bool enable)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	oplus_voocphy_set_chg_auto_mode(chip, enable);
}

static bool oplus_voocphy_get_real_fastchg_allow(struct oplus_voocphy_manager *chip)
{
	chip->fastchg_real_allow = oplus_voocphy_check_fastchg_real_allow(chip);

	return chip->fastchg_real_allow;
}


static void oplus_voocphy_send_handshake(struct oplus_voocphy_manager *chip)
{
	if (chip->ops && chip->ops->send_handshake)
		chip->ops->send_handshake(chip);
}

static void oplus_apvphy_switch_chg_mode(struct device *dev, int mode)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	if (!chip) {
		voocphy_info("oplus_voocphy_manager is null\n");
		return;
	}

	voocphy_info("switch charger mode: %s\n", mode ? "vooc" : "normal");

	if (mode == 1) {
		if (chip->fastchg_to_normal
		    || (!oplus_voocphy_get_real_fastchg_allow(chip)
		    && chip->fastchg_dummy_start) || chip->user_exit_fastchg) {
			voocphy_info(" not allow to fastchg [%d, %d, %d]\n",
				     chip->fastchg_to_normal,
				     chip->fastchg_dummy_start,
				     chip->user_exit_fastchg);
			voocphy_cpufreq_update(chip, CPU_CHG_FREQ_STAT_AUTO);
			oplus_voocphy_pm_qos_update(PM_QOS_DEFAULT_VALUE);
			return;
		}

		if (oplus_voocphy_get_real_fastchg_allow(chip) == false
		    && chip->fastchg_to_warm == true) {
			chg_err(" fastchg_allow false, to_warm true, don't switch to vooc mode\n");
			return;
		}
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0))
		if (!pm_qos_request_active(&pm_qos_req)) {
			pm_qos_add_request(&pm_qos_req, PM_QOS_CPU_DMA_LATENCY, 1000);
		}
#else
		if (!cpu_latency_qos_request_active(&pm_qos_req)) {
			cpu_latency_qos_add_request(&pm_qos_req, 1000);
		}
#endif

		oplus_voocphy_basetimer_monitor_start(chip);
		voocphy_cpufreq_init(chip);
		oplus_voocphy_pm_qos_update(400);
		chip->vbus_adjust_cnt = 0;
		oplus_voocphy_init_vooc(chip);
		oplus_voocphy_reset_ibus_trouble_flag();
		oplus_voocphy_slave_init_vooc(chip);
		oplus_voocphy_send_handshake(chip);
		/*
		 * Voocphy sends fastchg status 0x54 to notify
		 * vooc to update the current soc and temp range.
		 */
		oplus_chglib_notify_ap(chip->dev, FAST_NOTIFY_LOW_TEMP_FULL);
	} else if (mode == 0) {
		if (oplus_chglib_is_switch_temp_range()) {
			voocphy_info("enter switch temp range processimg");
			oplus_voocphy_set_status_and_notify_ap(chip,
						FAST_NOTIFY_SWITCH_TEMP_RANGE);
		}

		if (atomic_read(&chip->voocphy_freq_state) != 0) {
			voocphy_cpufreq_release(chip);
			oplus_voocphy_pm_qos_update(PM_QOS_DEFAULT_VALUE);
		}
	} else {
		/* not need handle */
	}
}

static void oplus_apvphy_set_pdqc_config(struct device *dev)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	if (!chip) {
		voocphy_info("chip is null\n");
		return;
	}

	oplus_voocphy_hw_setting(chip, SETTING_REASON_PDQC);
	if (chip->voocphy_dual_cp_support)
		oplus_voocphy_slave_hw_setting(chip, SETTING_REASON_PDQC);
}

static void oplus_apvphy_disconnect_detect(struct device *dev)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	if (!hrtimer_active(&chip->monitor_btimer)) {
		voocphy_info("not need to start check chg out work\n");
		return;
	}

	cancel_delayed_work(&chip->check_chg_out_work);
	oplus_voocphy_check_chg_out_work_func(chip);
}

static int oplus_apvphy_get_fastchg_type(struct device *dev)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	return chip != NULL ? chip->fast_chg_type : FASTCHG_CHARGER_TYPE_UNKOWN;
}

static int oplus_apvphy_get_fastchg_notify_status(struct device *dev)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	return chip->fastchg_notify_status;
}

static int oplus_apvphy_get_cp_vbat(struct device *dev)
{
	struct oplus_voocphy_manager *chip = dev_get_drvdata(dev);

	return oplus_voocphy_get_cp_vbat(chip);
}

static struct hw_vphy_info ap_vinf = {
	.vphy_set_screenoff_current	= oplus_apvphy_set_screenoff_current,
	.vphy_set_cool_down		= oplus_apvphy_set_cool_down,
	.vphy_set_chg_auto_mode		= oplus_apvphy_set_chg_auto_mode,
	.vphy_switch_chg_mode		= oplus_apvphy_switch_chg_mode,
	.vphy_set_pdqc_config		= oplus_apvphy_set_pdqc_config,
	.vphy_get_fastchg_type  	= oplus_apvphy_get_fastchg_type,
	.vphy_get_fastchg_notify_status = oplus_apvphy_get_fastchg_notify_status,
	.vphy_get_cp_vbat		= oplus_apvphy_get_cp_vbat,
	.vphy_disconnect_detect		= oplus_apvphy_disconnect_detect,
};

int oplus_register_voocphy(struct oplus_voocphy_manager *chip)
{
	int ret;

	ret = oplus_voocphy_init(chip);
	if (ret < 0) {
		voocphy_info("failed to voocphy init, ret = %d\n", ret);
		return ret;
	}

	chip->vchip = oplus_chglib_register_vphy(chip->dev, &ap_vinf);
	if (IS_ERR(chip->vchip)) {
		voocphy_info("fail to register vphy\n");
		return PTR_ERR(IS_ERR);
	}

	return 0;
}
