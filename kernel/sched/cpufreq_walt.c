// SPDX-License-Identifier: GPL-2.0-only
/*
 * This is based on schedutil governor but modified to work with
 * WALT.
 *
 * Copyright (C) 2016, Intel Corporation
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include "sched.h"

#include <linux/sched/cpufreq.h>
#include <trace/events/power.h>
#include <linux/sched/sysctl.h>

#include "walt.h"
#include <trace/events/walt.h>

struct waltgov_tunables {
	struct gov_attr_set	attr_set;
	unsigned int		up_rate_limit_us;
	unsigned int		down_rate_limit_us;
	unsigned int		hispeed_load;
	unsigned int		hispeed_freq;
	unsigned int		rtg_boost_freq;
	bool			pl;
};

struct waltgov_policy {
	struct cpufreq_policy	*policy;

	u64 last_ws;
	u64 curr_cycles;
	u64 last_cyc_update_time;
	unsigned long avg_cap;
	struct waltgov_tunables	*tunables;
	struct list_head	tunables_hook;
	unsigned long hispeed_util;
	unsigned long rtg_boost_util;
	unsigned long max;

	raw_spinlock_t		update_lock;	/* For shared policies */
	u64			last_freq_update_time;
	s64			min_rate_limit_ns;
	s64			up_rate_delay_ns;
	s64			down_rate_delay_ns;
	unsigned int		next_freq;
	unsigned int		cached_raw_freq;
	unsigned int		prev_cached_raw_freq;

	/* The next fields are only needed if fast switch cannot be used: */
	struct			irq_work irq_work;
	struct			kthread_work work;
	struct			mutex work_lock;
	struct			kthread_worker worker;
	struct task_struct	*thread;
	bool			work_in_progress;

	bool			limits_changed;
	bool			need_freq_update;
};

struct waltgov_cpu {
	struct update_util_data	update_util;
	struct waltgov_policy	*sg_policy;
	unsigned int		cpu;

	bool			iowait_boost_pending;
	unsigned int		iowait_boost;
	u64			last_update;

	struct sched_walt_cpu_load walt_load;

	unsigned long util;
	unsigned int flags;

	unsigned long		bw_dl;
	unsigned long		min;
	unsigned long		max;

	/* The field below is for single-CPU policies only: */
#ifdef CONFIG_NO_HZ_COMMON
	unsigned long		saved_idle_calls;
#endif
};

static DEFINE_PER_CPU(struct waltgov_cpu, waltgov_cpu);
static unsigned int stale_ns;
static DEFINE_PER_CPU(struct waltgov_tunables *, cached_tunables);

/************************ Governor internals ***********************/

static bool waltgov_should_update_freq(struct waltgov_policy *sg_policy, u64 time)
{
	s64 delta_ns;

	/*
	 * Since cpufreq_update_util() is called with rq->lock held for
	 * the @target_cpu, our per-CPU data is fully serialized.
	 *
	 * However, drivers cannot in general deal with cross-CPU
	 * requests, so while get_next_freq() will work, our
	 * waltgov_update_commit() call may not for the fast switching platforms.
	 *
	 * Hence stop here for remote requests if they aren't supported
	 * by the hardware, as calculating the frequency is pointless if
	 * we cannot in fact act on it.
	 *
	 * This is needed on the slow switching platforms too to prevent CPUs
	 * going offline from leaving stale IRQ work items behind.
	 */
	if (!cpufreq_this_cpu_can_update(sg_policy->policy))
		return false;

	if (unlikely(sg_policy->limits_changed)) {
		sg_policy->limits_changed = false;
		sg_policy->need_freq_update = true;
		return true;
	}

	/* No need to recalculate next freq for min_rate_limit_us
	 * at least. However we might still decide to further rate
	 * limit once frequency change direction is decided, according
	 * to the separate rate limits.
	 */

	delta_ns = time - sg_policy->last_freq_update_time;
	return delta_ns >= sg_policy->min_rate_limit_ns;
}

static inline bool use_pelt(void)
{
#ifdef CONFIG_SCHED_WALT
	return false;
#else
	return true;
#endif
}

static inline bool conservative_pl(void)
{
#ifdef CONFIG_SCHED_WALT
	return sysctl_sched_conservative_pl;
#else
	return false;
#endif
}

static bool waltgov_up_down_rate_limit(struct waltgov_policy *sg_policy, u64 time,
				     unsigned int next_freq)
{
	s64 delta_ns;

	delta_ns = time - sg_policy->last_freq_update_time;

	if (next_freq > sg_policy->next_freq &&
	    delta_ns < sg_policy->up_rate_delay_ns)
			return true;

	if (next_freq < sg_policy->next_freq &&
	    delta_ns < sg_policy->down_rate_delay_ns)
			return true;

	return false;
}

static bool waltgov_update_next_freq(struct waltgov_policy *sg_policy, u64 time,
				   unsigned int next_freq)
{
	if (sg_policy->next_freq == next_freq)
		return false;

	if (waltgov_up_down_rate_limit(sg_policy, time, next_freq)) {
		/* Restore cached freq as next_freq is not changed */
		sg_policy->cached_raw_freq = sg_policy->prev_cached_raw_freq;
		return false;
	}

	sg_policy->next_freq = next_freq;
	sg_policy->last_freq_update_time = time;

	return true;
}

static unsigned long freq_to_util(struct waltgov_policy *sg_policy,
				  unsigned int freq)
{
	return mult_frac(sg_policy->max, freq,
			 sg_policy->policy->cpuinfo.max_freq);
}

#define KHZ 1000
static void waltgov_track_cycles(struct waltgov_policy *sg_policy,
				unsigned int prev_freq,
				u64 upto)
{
	u64 delta_ns, cycles;
	u64 next_ws = sg_policy->last_ws + sched_ravg_window;

	if (use_pelt())
		return;

	upto = min(upto, next_ws);
	/* Track cycles in current window */
	delta_ns = upto - sg_policy->last_cyc_update_time;
	delta_ns *= prev_freq;
	do_div(delta_ns, (NSEC_PER_SEC / KHZ));
	cycles = delta_ns;
	sg_policy->curr_cycles += cycles;
	sg_policy->last_cyc_update_time = upto;
}

static void waltgov_calc_avg_cap(struct waltgov_policy *sg_policy, u64 curr_ws,
				unsigned int prev_freq)
{
	u64 last_ws = sg_policy->last_ws;
	unsigned int avg_freq;

	if (use_pelt())
		return;

	BUG_ON(curr_ws < last_ws);
	if (curr_ws <= last_ws)
		return;

	/* If we skipped some windows */
	if (curr_ws > (last_ws + sched_ravg_window)) {
		avg_freq = prev_freq;
		/* Reset tracking history */
		sg_policy->last_cyc_update_time = curr_ws;
	} else {
		waltgov_track_cycles(sg_policy, prev_freq, curr_ws);
		avg_freq = sg_policy->curr_cycles;
		avg_freq /= sched_ravg_window / (NSEC_PER_SEC / KHZ);
	}
	sg_policy->avg_cap = freq_to_util(sg_policy, avg_freq);
	sg_policy->curr_cycles = 0;
	sg_policy->last_ws = curr_ws;
}

static void waltgov_fast_switch(struct waltgov_policy *sg_policy, u64 time,
			      unsigned int next_freq)
{
	struct cpufreq_policy *policy = sg_policy->policy;
	unsigned int cpu;

	if (!waltgov_update_next_freq(sg_policy, time, next_freq))
		return;

	waltgov_track_cycles(sg_policy, sg_policy->policy->cur, time);
	next_freq = cpufreq_driver_fast_switch(policy, next_freq);
	if (!next_freq)
		return;

	policy->cur = next_freq;

	if (trace_cpu_frequency_enabled()) {
		for_each_cpu(cpu, policy->cpus)
			trace_cpu_frequency(next_freq, cpu);
	}
}

static void waltgov_deferred_update(struct waltgov_policy *sg_policy, u64 time,
				  unsigned int next_freq)
{
	if (!waltgov_update_next_freq(sg_policy, time, next_freq))
		return;

	if (use_pelt())
		sg_policy->work_in_progress = true;
	irq_work_queue(&sg_policy->irq_work);
}

#if IS_ENABLED(CONFIG_PACKAGE_RUNTIME_INFO)
__weak unsigned int glk_freq_limit(struct cpufreq_policy *policy,
		unsigned int *target_freq)
{
	return 0;
}

__weak unsigned long glk_cal_freq(struct cpufreq_policy *policy,
		unsigned long util, unsigned long max)
{
	return 0;
}
#endif

#define TARGET_LOAD 80
/**
 * get_next_freq - Compute a new frequency for a given cpufreq policy.
 * @sg_policy: schedutil policy object to compute the new frequency for.
 * @util: Current CPU utilization.
 * @max: CPU capacity.
 *
 * If the utilization is frequency-invariant, choose the new frequency to be
 * proportional to it, that is
 *
 * next_freq = C * max_freq * util / max
 *
 * Otherwise, approximate the would-be frequency-invariant utilization by
 * util_raw * (curr_freq / max_freq) which leads to
 *
 * next_freq = C * curr_freq * util_raw / max
 *
 * Take C = 1.25 for the frequency tipping point at (util / max) = 0.8.
 *
 * The lowest driver-supported frequency which is equal or greater than the raw
 * next_freq (as calculated above) is returned, subject to policy min/max and
 * cpufreq driver limitations.
 */
static unsigned int get_next_freq(struct waltgov_policy *sg_policy,
				  unsigned long util, unsigned long max)
{
	struct cpufreq_policy *policy = sg_policy->policy;
#if IS_ENABLED(CONFIG_PACKAGE_RUNTIME_INFO)
	unsigned int walt_freq;
#endif
	unsigned int raw_freq;
	unsigned int freq = arch_scale_freq_invariant() ?
				policy->cpuinfo.max_freq : policy->cur;

#if IS_ENABLED(CONFIG_PACKAGE_RUNTIME_INFO)
	walt_freq = map_util_freq(util, freq, max);
	freq = glk_cal_freq(policy, util, max);
	if (!freq)
		freq = glk_freq_limit(policy, &walt_freq);
	else
		sg_policy->need_freq_update = true;
#else
	freq = map_util_freq(util, freq, max);
#endif
	trace_waltgov_next_freq(policy->cpu, util, max, raw_freq, freq, policy->min, policy->max,
				sg_policy->cached_raw_freq, sg_policy->need_freq_update);

	if (freq == sg_policy->cached_raw_freq && !sg_policy->need_freq_update)
		return sg_policy->next_freq;

	sg_policy->need_freq_update = false;
	sg_policy->prev_cached_raw_freq = sg_policy->cached_raw_freq;
	sg_policy->cached_raw_freq = freq;
	return cpufreq_driver_resolve_freq(policy, freq);
}


#ifdef CONFIG_SCHED_WALT
static unsigned long waltgov_get_util(struct waltgov_cpu *sg_cpu)
{
	struct rq *rq = cpu_rq(sg_cpu->cpu);
	unsigned long max = arch_scale_cpu_capacity(NULL, sg_cpu->cpu);

	sg_cpu->max = max;
	sg_cpu->bw_dl = cpu_bw_dl(rq);

	return stune_util(sg_cpu->cpu, 0, &sg_cpu->walt_load);
}
#else
static unsigned long waltgov_get_util(struct waltgov_cpu *sg_cpu)
{
	struct rq *rq = cpu_rq(sg_cpu->cpu);

	unsigned long util_cfs = cpu_util_cfs(rq);
	unsigned long max = arch_scale_cpu_capacity(NULL, sg_cpu->cpu);

	sg_cpu->max = max;
	sg_cpu->bw_dl = cpu_bw_dl(rq);

	util = cpu_util_freq_walt(wg_cpu->cpu, &wg_cpu->walt_load);
	return uclamp_rq_util_with(rq, util, NULL);
}
#endif


static bool waltgov_iowait_reset(struct waltgov_cpu *sg_cpu, u64 time,
			       bool set_iowait_boost)
{
	s64 delta_ns = time - sg_cpu->last_update;

	/* Reset boost only if a tick has elapsed since last request */
	if (delta_ns <= TICK_NSEC)
		return false;

	sg_cpu->iowait_boost = set_iowait_boost ? sg_cpu->min : 0;
	sg_cpu->iowait_boost_pending = set_iowait_boost;

	return true;
}


static void waltgov_iowait_boost(struct waltgov_cpu *sg_cpu, u64 time,
			       unsigned int flags)
{
	bool set_iowait_boost = flags & SCHED_CPUFREQ_IOWAIT;

	/* Reset boost if the CPU appears to have been idle enough */
	if (sg_cpu->iowait_boost &&
	    waltgov_iowait_reset(sg_cpu, time, set_iowait_boost))
		return;

	/* Boost only tasks waking up after IO */
	if (!set_iowait_boost)
		return;

	/* Ensure boost doubles only one time at each request */
	if (sg_cpu->iowait_boost_pending)
		return;
	sg_cpu->iowait_boost_pending = true;

	/* Double the boost at each request */
	if (sg_cpu->iowait_boost) {
		sg_cpu->iowait_boost =
			min_t(unsigned int, sg_cpu->iowait_boost << 1, SCHED_CAPACITY_SCALE);
		return;
	}

	/* First wakeup after IO: start with minimum boost */
	sg_cpu->iowait_boost = sg_cpu->min;
}


static unsigned long waltgov_iowait_apply(struct waltgov_cpu *sg_cpu, u64 time,
					unsigned long util, unsigned long max)
{
	unsigned long boost;

	/* No boost currently required */
	if (!sg_cpu->iowait_boost)
		return util;

	/* Reset boost if the CPU appears to have been idle enough */
	if (waltgov_iowait_reset(sg_cpu, time, false))
		return util;

	if (!sg_cpu->iowait_boost_pending) {
		/*
		 * No boost pending; reduce the boost value.
		 */
		sg_cpu->iowait_boost >>= 1;
		if (sg_cpu->iowait_boost < sg_cpu->min) {
			sg_cpu->iowait_boost = 0;
			return util;
		}
	}

	sg_cpu->iowait_boost_pending = false;

	/*
	 * @util is already in capacity scale; convert iowait_boost
	 * into the same scale so we can compare.
	 */
	boost = (sg_cpu->iowait_boost * max) >> SCHED_CAPACITY_SHIFT;
	return max(boost, util);
}

#ifdef CONFIG_NO_HZ_COMMON
static bool waltgov_cpu_is_busy(struct waltgov_cpu *sg_cpu)
{
	unsigned long idle_calls = tick_nohz_get_idle_calls_cpu(sg_cpu->cpu);
	bool ret = idle_calls == sg_cpu->saved_idle_calls;

	sg_cpu->saved_idle_calls = idle_calls;
	return ret;
}
#else
static inline bool waltgov_cpu_is_busy(struct waltgov_cpu *sg_cpu) { return false; }
#endif /* CONFIG_NO_HZ_COMMON */

#define NL_RATIO 75
#define DEFAULT_HISPEED_LOAD 90
#define DEFAULT_CPU0_RTG_BOOST_FREQ 1000000
#define DEFAULT_CPU4_RTG_BOOST_FREQ 0
#define DEFAULT_CPU7_RTG_BOOST_FREQ 0
static void waltgov_walt_adjust(struct waltgov_cpu *sg_cpu, unsigned long *util,
			      unsigned long *max)
{
	struct waltgov_policy *sg_policy = sg_cpu->sg_policy;
	bool is_migration = sg_cpu->flags & SCHED_CPUFREQ_INTERCLUSTER_MIG;
	bool is_rtg_boost = sg_cpu->walt_load.rtgb_active;
	unsigned long nl = sg_cpu->walt_load.nl;
	unsigned long cpu_util = sg_cpu->util;
	bool is_hiload;
	unsigned long pl = sg_cpu->walt_load.pl;

	if (use_pelt())
		return;

	if (is_rtg_boost)
		*util = max(*util, sg_policy->rtg_boost_util);

	is_hiload = (cpu_util >= mult_frac(sg_policy->avg_cap,
					   sg_policy->tunables->hispeed_load,
					   100));

	if (is_hiload && !is_migration)
		*util = max(*util, sg_policy->hispeed_util);

	if (is_hiload && nl >= mult_frac(cpu_util, NL_RATIO, 100))
		*util = *max;

	if (sg_policy->tunables->pl) {
		if (conservative_pl())
			pl = mult_frac(pl, TARGET_LOAD, 100);
		*util = max(*util, pl);
	}
}

/*
 * Make waltgov_should_update_freq() ignore the rate limit when DL
 * has increased the utilization.
 */
static inline void ignore_dl_rate_limit(struct waltgov_cpu *sg_cpu, struct waltgov_policy *sg_policy)
{
	if (cpu_bw_dl(cpu_rq(sg_cpu->cpu)) > sg_cpu->bw_dl)
		sg_policy->limits_changed = true;
}

static inline unsigned long target_util(struct waltgov_policy *sg_policy,
				  unsigned int freq)
{
	unsigned long util;

	util = freq_to_util(sg_policy, freq);
	util = mult_frac(util, TARGET_LOAD, 100);
	return util;
}

static void waltgov_update_single(struct update_util_data *hook, u64 time,
				unsigned int flags)
{
	struct waltgov_cpu *sg_cpu = container_of(hook, struct waltgov_cpu, update_util);
	struct waltgov_policy *sg_policy = sg_cpu->sg_policy;
	unsigned long util, max, hs_util, boost_util;
	unsigned int next_f;
	bool busy;

	if (!sg_policy->tunables->pl && flags & SCHED_CPUFREQ_PL)
		return;

	waltgov_iowait_boost(sg_cpu, time, flags);
	sg_cpu->last_update = time;

	ignore_dl_rate_limit(sg_cpu, sg_policy);

	if (!waltgov_should_update_freq(sg_policy, time))
		return;

	/* Limits may have changed, don't skip frequency update */
	busy = use_pelt() && !sg_policy->need_freq_update &&
		waltgov_cpu_is_busy(sg_cpu);

	sg_cpu->util = util = waltgov_get_util(sg_cpu);
	max = sg_cpu->max;
	sg_cpu->flags = flags;

	if (sg_policy->max != max) {
		sg_policy->max = max;
		hs_util = target_util(sg_policy,
				       sg_policy->tunables->hispeed_freq);
		sg_policy->hispeed_util = hs_util;

		boost_util = target_util(sg_policy,
				    sg_policy->tunables->rtg_boost_freq);
		sg_policy->rtg_boost_util = boost_util;
	}

	util = waltgov_iowait_apply(sg_cpu, time, util, max);
	waltgov_calc_avg_cap(sg_policy, sg_cpu->walt_load.ws,
			   sg_policy->policy->cur);

	trace_waltgov_util_update(sg_cpu->cpu, sg_cpu->util,
				sg_policy->avg_cap, max, sg_cpu->walt_load.nl,
				sg_cpu->walt_load.pl,
				sg_cpu->walt_load.rtgb_active, flags);

	waltgov_walt_adjust(sg_cpu, &util, &max);
	next_f = get_next_freq(sg_policy, util, max);
	/*
	 * Do not reduce the frequency if the CPU has not been idle
	 * recently, as the reduction is likely to be premature then.
	 */
	if (busy && next_f < sg_policy->next_freq) {
		next_f = sg_policy->next_freq;

		/* Restore cached freq as next_freq has changed */
		sg_policy->cached_raw_freq = sg_policy->prev_cached_raw_freq;
	}

	/*
	 * This code runs under rq->lock for the target CPU, so it won't run
	 * concurrently on two different CPUs for the same target and it is not
	 * necessary to acquire the lock in the fast switch case.
	 */
	if (sg_policy->policy->fast_switch_enabled) {
		waltgov_fast_switch(sg_policy, time, next_f);
	} else {
		raw_spin_lock(&sg_policy->update_lock);
		waltgov_deferred_update(sg_policy, time, next_f);
		raw_spin_unlock(&sg_policy->update_lock);
	}
}

static unsigned int waltgov_next_freq_shared(struct waltgov_cpu *sg_cpu, u64 time)
{
	struct waltgov_policy *sg_policy = sg_cpu->sg_policy;
	struct cpufreq_policy *policy = sg_policy->policy;
	u64 last_freq_update_time = sg_policy->last_freq_update_time;
	unsigned long util = 0, max = 1;
	unsigned int j;

	for_each_cpu(j, policy->cpus) {
		struct waltgov_cpu *j_sg_cpu = &per_cpu(waltgov_cpu, j);
		unsigned long j_util, j_max;
		s64 delta_ns;

		/*
		 * If the CPU utilization was last updated before the previous
		 * frequency update and the time elapsed between the last update
		 * of the CPU utilization and the last frequency update is long
		 * enough, don't take the CPU into account as it probably is
		 * idle now (and clear iowait_boost for it).
		 */
		delta_ns = last_freq_update_time - j_sg_cpu->last_update;
		if (delta_ns > stale_ns) {
			waltgov_iowait_reset(j_sg_cpu, last_freq_update_time,
					   false);
			continue;
		}

		/*
		 * If the util value for all CPUs in a policy is 0, just using >
		 * will result in a max value of 1. WALT stats can later update
		 * the aggregated util value, causing get_next_freq() to compute
		 * freq = max_freq * 1.25 * (util / max) for nonzero util,
		 * leading to spurious jumps to fmax.
		 */
		j_util = j_sg_cpu->util;
		j_max = j_sg_cpu->max;
		j_util = waltgov_iowait_apply(j_sg_cpu, time, j_util, j_max);

		if (j_util * max >= j_max * util) {
			util = j_util;
			max = j_max;
		}

		waltgov_walt_adjust(j_sg_cpu, &util, &max);
	}

	return get_next_freq(sg_policy, util, max);
}

static void
waltgov_update_shared(struct update_util_data *hook, u64 time, unsigned int flags)
{
	struct waltgov_cpu *sg_cpu = container_of(hook, struct waltgov_cpu, update_util);
	struct waltgov_policy *sg_policy = sg_cpu->sg_policy;
	unsigned long hs_util, boost_util;
	unsigned int next_f;

	if (!sg_policy->tunables->pl && flags & SCHED_CPUFREQ_PL)
		return;

	sg_cpu->util = waltgov_get_util(sg_cpu);
	sg_cpu->flags = flags;
	raw_spin_lock(&sg_policy->update_lock);

	if (sg_policy->max != sg_cpu->max) {
		sg_policy->max = sg_cpu->max;
		hs_util = target_util(sg_policy,
					sg_policy->tunables->hispeed_freq);
		sg_policy->hispeed_util = hs_util;

		boost_util = target_util(sg_policy,
				    sg_policy->tunables->rtg_boost_freq);
		sg_policy->rtg_boost_util = boost_util;
	}

	waltgov_iowait_boost(sg_cpu, time, flags);
	sg_cpu->last_update = time;

	waltgov_calc_avg_cap(sg_policy, sg_cpu->walt_load.ws,
			   sg_policy->policy->cur);
	ignore_dl_rate_limit(sg_cpu, sg_policy);

	trace_waltgov_util_update(sg_cpu->cpu, sg_cpu->util, sg_policy->avg_cap,
				sg_cpu->max, sg_cpu->walt_load.nl,
				sg_cpu->walt_load.pl,
				sg_cpu->walt_load.rtgb_active, flags);

	if (waltgov_should_update_freq(sg_policy, time) &&
	    !(flags & SCHED_CPUFREQ_CONTINUE)) {
		next_f = waltgov_next_freq_shared(sg_cpu, time);

		if (sg_policy->policy->fast_switch_enabled)
			waltgov_fast_switch(sg_policy, time, next_f);
		else
			waltgov_deferred_update(sg_policy, time, next_f);
	}

	raw_spin_unlock(&sg_policy->update_lock);
}

static void waltgov_work(struct kthread_work *work)
{
	struct waltgov_policy *sg_policy = container_of(work, struct waltgov_policy, work);
	unsigned int freq;
	unsigned long flags;

	/*
	 * Hold sg_policy->update_lock shortly to handle the case where:
	 * incase sg_policy->next_freq is read here, and then updated by
	 * waltgov_deferred_update() just before work_in_progress is set to false
	 * here, we may miss queueing the new update.
	 *
	 * Note: If a work was queued after the update_lock is released,
	 * waltgov_work() will just be called again by kthread_work code; and the
	 * request will be proceed before the waltgov thread sleeps.
	 */
	raw_spin_lock_irqsave(&sg_policy->update_lock, flags);
	freq = sg_policy->next_freq;
	if (use_pelt())
		sg_policy->work_in_progress = false;
	waltgov_track_cycles(sg_policy, sg_policy->policy->cur,
			   ktime_get_ns());
	raw_spin_unlock_irqrestore(&sg_policy->update_lock, flags);

	mutex_lock(&sg_policy->work_lock);
	__cpufreq_driver_target(sg_policy->policy, freq, CPUFREQ_RELATION_L);
	mutex_unlock(&sg_policy->work_lock);
}

static void waltgov_irq_work(struct irq_work *irq_work)
{
	struct waltgov_policy *sg_policy;

	sg_policy = container_of(irq_work, struct waltgov_policy, irq_work);

	kthread_queue_work(&sg_policy->worker, &sg_policy->work);
}

/************************** sysfs interface ************************/

static struct waltgov_tunables *global_tunables;
static DEFINE_MUTEX(global_tunables_lock);

static inline struct waltgov_tunables *to_waltgov_tunables(struct gov_attr_set *attr_set)
{
	return container_of(attr_set, struct waltgov_tunables, attr_set);
}

static DEFINE_MUTEX(min_rate_lock);

static void update_min_rate_limit_ns(struct waltgov_policy *sg_policy)
{
	mutex_lock(&min_rate_lock);
	sg_policy->min_rate_limit_ns = min(sg_policy->up_rate_delay_ns,
					   sg_policy->down_rate_delay_ns);
	mutex_unlock(&min_rate_lock);
}

static ssize_t up_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct waltgov_tunables *tunables = to_waltgov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->up_rate_limit_us);
}

static ssize_t down_rate_limit_us_show(struct gov_attr_set *attr_set, char *buf)
{
	struct waltgov_tunables *tunables = to_waltgov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->down_rate_limit_us);
}

static ssize_t up_rate_limit_us_store(struct gov_attr_set *attr_set,
				      const char *buf, size_t count)
{
	struct waltgov_tunables *tunables = to_waltgov_tunables(attr_set);
	struct waltgov_policy *sg_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->up_rate_limit_us = rate_limit_us;

	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		sg_policy->up_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_ns(sg_policy);
	}

	return count;
}

static ssize_t down_rate_limit_us_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct waltgov_tunables *tunables = to_waltgov_tunables(attr_set);
	struct waltgov_policy *sg_policy;
	unsigned int rate_limit_us;

	if (kstrtouint(buf, 10, &rate_limit_us))
		return -EINVAL;

	tunables->down_rate_limit_us = rate_limit_us;

	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		sg_policy->down_rate_delay_ns = rate_limit_us * NSEC_PER_USEC;
		update_min_rate_limit_ns(sg_policy);
	}

	return count;
}

static struct governor_attr up_rate_limit_us = __ATTR_RW(up_rate_limit_us);
static struct governor_attr down_rate_limit_us = __ATTR_RW(down_rate_limit_us);

static ssize_t hispeed_load_show(struct gov_attr_set *attr_set, char *buf)
{
	struct waltgov_tunables *tunables = to_waltgov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->hispeed_load);
}

static ssize_t hispeed_load_store(struct gov_attr_set *attr_set,
				  const char *buf, size_t count)
{
	struct waltgov_tunables *tunables = to_waltgov_tunables(attr_set);

	if (kstrtouint(buf, 10, &tunables->hispeed_load))
		return -EINVAL;

	tunables->hispeed_load = min(100U, tunables->hispeed_load);

	return count;
}

static ssize_t hispeed_freq_show(struct gov_attr_set *attr_set, char *buf)
{
	struct waltgov_tunables *tunables = to_waltgov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->hispeed_freq);
}

static ssize_t hispeed_freq_store(struct gov_attr_set *attr_set,
					const char *buf, size_t count)
{
	struct waltgov_tunables *tunables = to_waltgov_tunables(attr_set);
	unsigned int val;
	struct waltgov_policy *sg_policy;
	unsigned long hs_util;
	unsigned long flags;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	tunables->hispeed_freq = val;
	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		raw_spin_lock_irqsave(&sg_policy->update_lock, flags);
		hs_util = target_util(sg_policy,
					sg_policy->tunables->hispeed_freq);
		sg_policy->hispeed_util = hs_util;
		raw_spin_unlock_irqrestore(&sg_policy->update_lock, flags);
	}

	return count;
}

static ssize_t rtg_boost_freq_show(struct gov_attr_set *attr_set, char *buf)
{
	struct waltgov_tunables *tunables = to_waltgov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->rtg_boost_freq);
}

static ssize_t rtg_boost_freq_store(struct gov_attr_set *attr_set,
				    const char *buf, size_t count)
{
	struct waltgov_tunables *tunables = to_waltgov_tunables(attr_set);
	unsigned int val;
	struct waltgov_policy *sg_policy;
	unsigned long boost_util;
	unsigned long flags;

	if (kstrtouint(buf, 10, &val))
		return -EINVAL;

	tunables->rtg_boost_freq = val;
	list_for_each_entry(sg_policy, &attr_set->policy_list, tunables_hook) {
		raw_spin_lock_irqsave(&sg_policy->update_lock, flags);
		boost_util = target_util(sg_policy,
					  sg_policy->tunables->rtg_boost_freq);
		sg_policy->rtg_boost_util = boost_util;
		raw_spin_unlock_irqrestore(&sg_policy->update_lock, flags);
	}

	return count;
}

static ssize_t pl_show(struct gov_attr_set *attr_set, char *buf)
{
	struct waltgov_tunables *tunables = to_waltgov_tunables(attr_set);

	return scnprintf(buf, PAGE_SIZE, "%u\n", tunables->pl);
}

static ssize_t pl_store(struct gov_attr_set *attr_set, const char *buf,
				   size_t count)
{
	struct waltgov_tunables *tunables = to_waltgov_tunables(attr_set);

	if (kstrtobool(buf, &tunables->pl))
		return -EINVAL;

	return count;
}

static struct governor_attr hispeed_load = __ATTR_RW(hispeed_load);
static struct governor_attr hispeed_freq = __ATTR_RW(hispeed_freq);
static struct governor_attr rtg_boost_freq = __ATTR_RW(rtg_boost_freq);
static struct governor_attr pl = __ATTR_RW(pl);

static struct attribute *waltgov_attributes[] = {
	&up_rate_limit_us.attr,
	&down_rate_limit_us.attr,
	&hispeed_load.attr,
	&hispeed_freq.attr,
	&rtg_boost_freq.attr,
	&pl.attr,
	NULL
};

static void waltgov_tunables_free(struct kobject *kobj)
{
	struct gov_attr_set *attr_set = container_of(kobj, struct gov_attr_set, kobj);

	kfree(to_waltgov_tunables(attr_set));
}

static struct kobj_type waltgov_tunables_ktype = {
	.default_attrs = waltgov_attributes,
	.sysfs_ops = &governor_sysfs_ops,
	.release = &waltgov_tunables_free,
};

/********************** cpufreq governor interface *********************/

static struct cpufreq_governor walt_gov;

static struct waltgov_policy *waltgov_policy_alloc(struct cpufreq_policy *policy)
{
	struct waltgov_policy *sg_policy;

	sg_policy = kzalloc(sizeof(*sg_policy), GFP_KERNEL);
	if (!sg_policy)
		return NULL;

	sg_policy->policy = policy;
	raw_spin_lock_init(&sg_policy->update_lock);
	return sg_policy;
}

static void waltgov_policy_free(struct waltgov_policy *sg_policy)
{
	kfree(sg_policy);
}

static int waltgov_kthread_create(struct waltgov_policy *sg_policy)
{
	struct task_struct *thread;
	struct sched_param param = { .sched_priority = MAX_USER_RT_PRIO / 2 };
	struct cpufreq_policy *policy = sg_policy->policy;
	int ret;

	/* kthread only required for slow path */
	if (policy->fast_switch_enabled)
		return 0;

	kthread_init_work(&sg_policy->work, waltgov_work);
	kthread_init_worker(&sg_policy->worker);
	thread = kthread_create(kthread_worker_fn, &sg_policy->worker,
				"waltgov:%d",
				cpumask_first(policy->related_cpus));
	if (IS_ERR(thread)) {
		pr_err("failed to create waltgov thread: %ld\n", PTR_ERR(thread));
		return PTR_ERR(thread);
	}

	ret = sched_setscheduler_nocheck(thread, SCHED_FIFO, &param);
	if (ret) {
		kthread_stop(thread);
		pr_warn("%s: failed to set SCHED_FIFO\n", __func__);
		return ret;
	}

	sg_policy->thread = thread;
	kthread_bind_mask(thread, policy->related_cpus);
	init_irq_work(&sg_policy->irq_work, waltgov_irq_work);
	mutex_init(&sg_policy->work_lock);

	wake_up_process(thread);

	return 0;
}

static void waltgov_kthread_stop(struct waltgov_policy *sg_policy)
{
	/* kthread only required for slow path */
	if (sg_policy->policy->fast_switch_enabled)
		return;

	kthread_flush_worker(&sg_policy->worker);
	kthread_stop(sg_policy->thread);
	mutex_destroy(&sg_policy->work_lock);
}

static struct waltgov_tunables *waltgov_tunables_alloc(struct waltgov_policy *sg_policy)
{
	struct waltgov_tunables *tunables;

	tunables = kzalloc(sizeof(*tunables), GFP_KERNEL);
	if (tunables) {
		gov_attr_set_init(&tunables->attr_set, &sg_policy->tunables_hook);
		if (!have_governor_per_policy())
			global_tunables = tunables;
	}
	return tunables;
}

static void waltgov_tunables_save(struct cpufreq_policy *policy,
		struct waltgov_tunables *tunables)
{
	int cpu;
	struct waltgov_tunables *cached = per_cpu(cached_tunables, policy->cpu);

	if (!have_governor_per_policy())
		return;

	if (!cached) {
		cached = kzalloc(sizeof(*tunables), GFP_KERNEL);
		if (!cached)
			return;

		for_each_cpu(cpu, policy->related_cpus)
			per_cpu(cached_tunables, cpu) = cached;
	}

	cached->pl = tunables->pl;
	cached->hispeed_load = tunables->hispeed_load;
	cached->rtg_boost_freq = tunables->rtg_boost_freq;
	cached->hispeed_freq = tunables->hispeed_freq;
	cached->up_rate_limit_us = tunables->up_rate_limit_us;
	cached->down_rate_limit_us = tunables->down_rate_limit_us;
}

static void waltgov_clear_global_tunables(void)
{
	if (!have_governor_per_policy())
		global_tunables = NULL;
}

static void waltgov_tunables_restore(struct cpufreq_policy *policy)
{
	struct waltgov_policy *sg_policy = policy->governor_data;
	struct waltgov_tunables *tunables = sg_policy->tunables;
	struct waltgov_tunables *cached = per_cpu(cached_tunables, policy->cpu);

	if (!cached)
		return;

	tunables->pl = cached->pl;
	tunables->hispeed_load = cached->hispeed_load;
	tunables->rtg_boost_freq = cached->rtg_boost_freq;
	tunables->hispeed_freq = cached->hispeed_freq;
	tunables->up_rate_limit_us = cached->up_rate_limit_us;
	tunables->down_rate_limit_us = cached->down_rate_limit_us;
}

static int waltgov_init(struct cpufreq_policy *policy)
{
	struct waltgov_policy *sg_policy;
	struct waltgov_tunables *tunables;
	unsigned long util;
	int ret = 0;

	/* State should be equivalent to EXIT */
	if (policy->governor_data)
		return -EBUSY;

	cpufreq_enable_fast_switch(policy);

	sg_policy = waltgov_policy_alloc(policy);
	if (!sg_policy) {
		ret = -ENOMEM;
		goto disable_fast_switch;
	}

	ret = waltgov_kthread_create(sg_policy);
	if (ret)
		goto free_sg_policy;

	mutex_lock(&global_tunables_lock);

	if (global_tunables) {
		if (WARN_ON(have_governor_per_policy())) {
			ret = -EINVAL;
			goto stop_kthread;
		}
		policy->governor_data = sg_policy;
		sg_policy->tunables = global_tunables;

		gov_attr_set_get(&global_tunables->attr_set, &sg_policy->tunables_hook);
		goto out;
	}

	tunables = waltgov_tunables_alloc(sg_policy);
	if (!tunables) {
		ret = -ENOMEM;
		goto stop_kthread;
	}

	tunables->up_rate_limit_us = cpufreq_policy_transition_delay_us(policy);
	tunables->down_rate_limit_us = cpufreq_policy_transition_delay_us(policy);
	tunables->hispeed_load = DEFAULT_HISPEED_LOAD;
	tunables->hispeed_freq = 0;

	switch (policy->cpu) {
	default:
	case 0:
		tunables->rtg_boost_freq = DEFAULT_CPU0_RTG_BOOST_FREQ;
		break;
	case 4:
		tunables->rtg_boost_freq = DEFAULT_CPU4_RTG_BOOST_FREQ;
		break;
	case 7:
		tunables->rtg_boost_freq = DEFAULT_CPU7_RTG_BOOST_FREQ;
		break;
	}

	policy->governor_data = sg_policy;
	sg_policy->tunables = tunables;

	util = target_util(sg_policy, sg_policy->tunables->rtg_boost_freq);
	sg_policy->rtg_boost_util = util;

	stale_ns = sched_ravg_window + (sched_ravg_window >> 3);

	waltgov_tunables_restore(policy);

	ret = kobject_init_and_add(&tunables->attr_set.kobj, &waltgov_tunables_ktype,
				   get_governor_parent_kobj(policy), "%s",
				   walt_gov.name);
	if (ret)
		goto fail;

out:
	mutex_unlock(&global_tunables_lock);
	return 0;

fail:
	kobject_put(&tunables->attr_set.kobj);
	policy->governor_data = NULL;
	waltgov_clear_global_tunables();

stop_kthread:
	waltgov_kthread_stop(sg_policy);
	mutex_unlock(&global_tunables_lock);

free_sg_policy:
	waltgov_policy_free(sg_policy);

disable_fast_switch:
	cpufreq_disable_fast_switch(policy);

	pr_err("initialization failed (error %d)\n", ret);
	return ret;
}

static void waltgov_exit(struct cpufreq_policy *policy)
{
	struct waltgov_policy *sg_policy = policy->governor_data;
	struct waltgov_tunables *tunables = sg_policy->tunables;
	unsigned int count;

	mutex_lock(&global_tunables_lock);

	count = gov_attr_set_put(&tunables->attr_set, &sg_policy->tunables_hook);
	policy->governor_data = NULL;
	if (!count) {
		waltgov_tunables_save(policy, tunables);
		waltgov_clear_global_tunables();
	}

	mutex_unlock(&global_tunables_lock);

	waltgov_kthread_stop(sg_policy);
	waltgov_policy_free(sg_policy);
	cpufreq_disable_fast_switch(policy);
}

static int waltgov_start(struct cpufreq_policy *policy)
{
	struct waltgov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	sg_policy->up_rate_delay_ns =
		sg_policy->tunables->up_rate_limit_us * NSEC_PER_USEC;
	sg_policy->down_rate_delay_ns =
		sg_policy->tunables->down_rate_limit_us * NSEC_PER_USEC;
	update_min_rate_limit_ns(sg_policy);
	sg_policy->last_freq_update_time	= 0;
	sg_policy->next_freq			= 0;
	sg_policy->work_in_progress		= false;
	sg_policy->limits_changed		= false;
	sg_policy->need_freq_update		= false;
	sg_policy->cached_raw_freq		= 0;
	sg_policy->prev_cached_raw_freq		= 0;

	for_each_cpu(cpu, policy->cpus) {
		struct waltgov_cpu *sg_cpu = &per_cpu(waltgov_cpu, cpu);

		memset(sg_cpu, 0, sizeof(*sg_cpu));
		sg_cpu->cpu			= cpu;
		sg_cpu->sg_policy		= sg_policy;
		sg_cpu->min			=
			(SCHED_CAPACITY_SCALE * policy->cpuinfo.min_freq) /
			policy->cpuinfo.max_freq;
	}

	for_each_cpu(cpu, policy->cpus) {
		struct waltgov_cpu *sg_cpu = &per_cpu(waltgov_cpu, cpu);

		cpufreq_add_update_util_hook(cpu, &sg_cpu->update_util,
					     policy_is_shared(policy) ?
							waltgov_update_shared :
							waltgov_update_single);
	}
	return 0;
}

static void waltgov_stop(struct cpufreq_policy *policy)
{
	struct waltgov_policy *sg_policy = policy->governor_data;
	unsigned int cpu;

	for_each_cpu(cpu, policy->cpus)
		cpufreq_remove_update_util_hook(cpu);

	synchronize_sched();

	if (!policy->fast_switch_enabled) {
		irq_work_sync(&sg_policy->irq_work);
		kthread_cancel_work_sync(&sg_policy->work);
	}
}

static void waltgov_limits(struct cpufreq_policy *policy)
{
	struct waltgov_policy *sg_policy = policy->governor_data;
	unsigned long flags, now;
	unsigned int freq;

	if (!policy->fast_switch_enabled) {
		mutex_lock(&sg_policy->work_lock);
		raw_spin_lock_irqsave(&sg_policy->update_lock, flags);
		waltgov_track_cycles(sg_policy, sg_policy->policy->cur,
				   ktime_get_ns());
		raw_spin_unlock_irqrestore(&sg_policy->update_lock, flags);
		cpufreq_policy_apply_limits(policy);
		mutex_unlock(&sg_policy->work_lock);
	} else {
		raw_spin_lock_irqsave(&sg_policy->update_lock, flags);
		freq = policy->cur;
		now = ktime_get_ns();

		/*
		 * cpufreq_driver_resolve_freq() has a clamp, so we do not need
		 * to do any sort of additional validation here.
		 */
		freq = cpufreq_driver_resolve_freq(policy, freq);
		sg_policy->cached_raw_freq = freq;
		waltgov_fast_switch(sg_policy, now, freq);
		raw_spin_unlock_irqrestore(&sg_policy->update_lock, flags);
	}

	sg_policy->limits_changed = true;
}

static struct cpufreq_governor walt_gov = {
	.name			= "walt",
	.owner			= THIS_MODULE,
	.dynamic_switching	= true,
	.init			= waltgov_init,
	.exit			= waltgov_exit,
	.start			= waltgov_start,
	.stop			= waltgov_stop,
	.limits			= waltgov_limits,
};

#ifdef CONFIG_CPU_FREQ_DEFAULT_GOV_SCHEDUTIL
struct cpufreq_governor *cpufreq_default_governor(void)
{
	return &walt_gov;
}
#endif

static int __init waltgov_register(void)
{
	return cpufreq_register_governor(&walt_gov);
}
fs_initcall(waltgov_register);
