#include <linux/cgroup.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/percpu.h>
#include <linux/printk.h>
#include <linux/rcupdate.h>
#include <linux/slab.h>

#include <trace/events/sched.h>
#include <linux/list.h>

#include "sched.h"

bool schedtune_initialized = false;
extern struct reciprocal_value schedtune_spc_rdiv;

#ifdef CONFIG_DYNAMIC_STUNE_BOOST
#define DYNAMIC_BOOST_SLOTS_COUNT 5
static DEFINE_MUTEX(boost_slot_mutex);
static DEFINE_MUTEX(stune_boost_mutex);
struct boost_slot {
	struct list_head list;
	int idx;
};
#endif /* CONFIG_DYNAMIC_STUNE_BOOST */

/* We hold schedtune boost in effect for at least this long */
#define SCHEDTUNE_BOOST_HOLD_NS 50000000ULL

/*
 * EAS scheduler tunables for task groups.
 *
 * When CGroup support is enabled, we have to synchronize two different
 * paths:
 *  - slow path: where CGroups are created/updated/removed
 *  - fast path: where tasks in a CGroups are accounted
 *
 * The slow path tracks (a limited number of) CGroups and maps each on a
 * "boost_group" index. The fastpath accounts tasks currently RUNNABLE on each
 * "boost_group".
 *
 * Once a new CGroup is created, a boost group idx is assigned and the
 * corresponding "boost_group" marked as valid on each CPU.
 * Once a CGroup is release, the corresponding "boost_group" is marked as
 * invalid on each CPU. The CPU boost value (boost_max) is aggregated by
 * considering only valid boost_groups with a non null tasks counter.
 *
 * .:: Locking strategy
 *
 * The fast path uses a spin lock for each CPU boost_group which protects the
 * tasks counter.
 *
 * The "valid" and "boost" values of each CPU boost_group is instead
 * protected by the RCU lock provided by the CGroups callbacks. Thus, only the
 * slow path can access and modify the boost_group attribtues of each CPU.
 * The fast path will catch up the most updated values at the next scheduling
 * event (i.e. enqueue/dequeue).
 *
 *                                                        |
 *                                             SLOW PATH  |   FAST PATH
 *                              CGroup add/update/remove  |   Scheduler enqueue/dequeue events
 *                                                        |
 *                                                        |
 *                                                        |     DEFINE_PER_CPU(struct boost_groups)
 *                                                        |     +--------------+----+---+----+----+
 *                                                        |     |  idle        |    |   |    |    |
 *                                                        |     |  boost_max   |    |   |    |    |
 *                                                        |  +---->lock        |    |   |    |    |
 *  struct schedtune                  allocated_groups    |  |  |  group[    ] |    |   |    |    |
 *  +------------------------------+         +-------+    |  |  +--+---------+-+----+---+----+----+
 *  | idx                          |         |       |    |  |     |  valid  |
 *  | boots / prefer_idle          |         |       |    |  |     |  boost  |
 *  | perf_{boost/constraints}_idx | <---------+(*)  |    |  |     |  tasks  | <------------+
 *  | css                          |         +-------+    |  |     +---------+              |
 *  +-+----------------------------+         |       |    |  |     |         |              |
 *    ^                                      |       |    |  |     |         |              |
 *    |                                      +-------+    |  |     +---------+              |
 *    |                                      |       |    |  |     |         |              |
 *    |                                      |       |    |  |     |         |              |
 *    |                                      +-------+    |  |     +---------+              |
 *    | zmalloc                              |       |    |  |     |         |              |
 *    |                                      |       |    |  |     |         |              |
 *    |                                      +-------+    |  |     +---------+              |
 *    +                              BOOSTGROUPS_COUNT    |  |     BOOSTGROUPS_COUNT        |
 *  schedtune_boostgroup_init()                           |  +                              |
 *                                                        |  schedtune_{en,de}queue_task()  |
 *                                                        |                                 +
 *                                                        |          schedtune_tasks_update()
 *                                                        |
 */

/* SchdTune tunables for a group of tasks */
struct schedtune {
	/* SchedTune CGroup subsystem */
	struct cgroup_subsys_state css;

	/* Boost group allocated ID */
	int idx;

	/* Boost value for tasks on that SchedTune CGroup */
	int boost;

#ifdef CONFIG_SCHED_WALT
	/* Toggle ability to override sched boost enabled */
	bool sched_boost_no_override;

	/*
	 * Controls whether a cgroup is eligible for sched boost or not. This
	 * can temporariliy be disabled by the kernel based on the no_override
	 * flag above.
	 */
	bool sched_boost_enabled;

	/*
	 * Controls whether tasks of this cgroup should be colocated with each
	 * other and tasks of other cgroups that have the same flag turned on.
	 */
	bool colocate;

	/* Controls whether further updates are allowed to the colocate flag */
	bool colocate_update_disabled;
#endif /* CONFIG_SCHED_WALT */

	/* Hint to bias scheduling of tasks on that SchedTune CGroup
	 * towards idle CPUs */
	int prefer_idle;
#ifdef CONFIG_DYNAMIC_STUNE_BOOST
	/*
	 * This tracks the default boost value and is used to restore
	 * the value when Dynamic SchedTune Boost is reset.
	 */
	int boost_default;

	/* Sched Boost value for tasks on that SchedTune CGroup */
	int sched_boost;

	/* Number of ongoing boosts for this SchedTune CGroup */
	int boost_count;

	/* Lists of active and available boost slots */
	struct boost_slot active_boost_slots;
	struct boost_slot available_boost_slots;

	/* Array of tracked boost values of each slot */
	int slot_boost[DYNAMIC_BOOST_SLOTS_COUNT];
#endif /* CONFIG_DYNAMIC_STUNE_BOOST */
};

static inline struct schedtune *css_st(struct cgroup_subsys_state *css)
{
	return css ? container_of(css, struct schedtune, css) : NULL;
}

static inline struct schedtune *task_schedtune(struct task_struct *tsk)
{
	return css_st(task_css(tsk, schedtune_cgrp_id));
}

static inline struct schedtune *parent_st(struct schedtune *st)
{
	return css_st(st->css.parent);
}

/*
 * SchedTune root control group
 * The root control group is used to defined a system-wide boosting tuning,
 * which is applied to all tasks in the system.
 * Task specific boost tuning could be specified by creating and
 * configuring a child control group under the root one.
 * By default, system-wide boosting is disabled, i.e. no boosting is applied
 * to tasks which are not into a child control group.
 */
static struct schedtune
root_schedtune = {
	.boost	= 0,
#ifdef CONFIG_SCHED_WALT
	.sched_boost_no_override = false,
	.sched_boost_enabled = true,
	.colocate = false,
	.colocate_update_disabled = false,
#endif
	.prefer_idle = 0,
#ifdef CONFIG_DYNAMIC_STUNE_BOOST
	.boost_default = 0,
	.sched_boost = 0,
	.boost_count = 0,
	.active_boost_slots = {
		.list = LIST_HEAD_INIT(root_schedtune.active_boost_slots.list),
		.idx = 0,
	},
	.available_boost_slots = {
		.list = LIST_HEAD_INIT(root_schedtune.available_boost_slots.list),
		.idx = 0,
	},
	.slot_boost = {0},
#endif /* CONFIG_DYNAMIC_STUNE_BOOST */
};

/*
 * Maximum number of boost groups to support
 * When per-task boosting is used we still allow only limited number of
 * boost groups for two main reasons:
 * 1. on a real system we usually have only few classes of workloads which
 *    make sense to boost with different values (e.g. background vs foreground
 *    tasks, interactive vs low-priority tasks)
 * 2. a limited number allows for a simpler and more memory/time efficient
 *    implementation especially for the computation of the per-CPU boost
 *    value
 */
#define BOOSTGROUPS_COUNT 8

/* Array of configured boostgroups */
static struct schedtune *allocated_group[BOOSTGROUPS_COUNT] = {
	&root_schedtune,
	NULL,
};

/* SchedTune boost groups
 * Keep track of all the boost groups which impact on CPU, for example when a
 * CPU has two RUNNABLE tasks belonging to two different boost groups and thus
 * likely with different boost values.
 * Since on each system we expect only a limited number of boost groups, here
 * we use a simple array to keep track of the metrics required to compute the
 * maximum per-CPU boosting value.
 */
struct boost_groups {
	/* Maximum boost value for all RUNNABLE tasks on a CPU */
	int boost_max;
	u64 boost_ts;
	struct {
		/* True when this boost group maps an actual cgroup */
		bool valid;
		/* The boost for tasks on that boost group */
		int boost;
		/* Count of RUNNABLE tasks on that boost group */
		unsigned tasks;
		/* Timestamp of boost activation */
		u64 ts;
	} group[BOOSTGROUPS_COUNT];
	/* CPU's boost group locking */
	raw_spinlock_t lock;
};

/* Boost groups affecting each CPU in the system */
DEFINE_PER_CPU(struct boost_groups, cpu_boost_groups);

#ifdef CONFIG_SCHED_WALT
static inline void init_sched_boost(struct schedtune *st)
{
	st->sched_boost_no_override = false;
	st->sched_boost_enabled = true;
	st->colocate = false;
	st->colocate_update_disabled = false;
}

void update_cgroup_boost_settings(void)
{
	int i;

	for (i = 0; i < BOOSTGROUPS_COUNT; i++) {
		if (!allocated_group[i])
			break;

		if (allocated_group[i]->sched_boost_no_override)
			continue;

		allocated_group[i]->sched_boost_enabled = false;
	}
}

void restore_cgroup_boost_settings(void)
{
	int i;

	for (i = 0; i < BOOSTGROUPS_COUNT; i++) {
		if (!allocated_group[i])
			break;

		allocated_group[i]->sched_boost_enabled = true;
	}
}

bool task_sched_boost(struct task_struct *p)
{
	struct schedtune *st;
	bool sched_boost_enabled;

	rcu_read_lock();
	st = task_schedtune(p);
	sched_boost_enabled = st->sched_boost_enabled;
	rcu_read_unlock();

	return sched_boost_enabled;
}

static u64
sched_boost_override_read(struct cgroup_subsys_state *css,
					struct cftype *cft)
{
	struct schedtune *st = css_st(css);

	return st->sched_boost_no_override;
}

static int sched_boost_override_write(struct cgroup_subsys_state *css,
				struct cftype *cft, u64 override)
{
	struct schedtune *st = css_st(css);

	st->sched_boost_no_override = !!override;

	return 0;
}

#endif /* CONFIG_SCHED_WALT */

static inline bool schedtune_boost_timeout(u64 now, u64 ts)
{
	return ((now - ts) > SCHEDTUNE_BOOST_HOLD_NS);
}

static inline bool
schedtune_boost_group_active(int idx, struct boost_groups* bg, u64 now)
{
	if (bg->group[idx].tasks)
		return true;

	return !schedtune_boost_timeout(now, bg->group[idx].ts);
}

static void
schedtune_cpu_update(int cpu, u64 now)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	int boost_max;
	u64 boost_ts;
	int idx;

	/* The root boost group is always active */
	boost_max = bg->group[0].boost;
	boost_ts = now;
	for (idx = 1; idx < BOOSTGROUPS_COUNT; ++idx) {

		/* Ignore non boostgroups not mapping a cgroup */
		if (!bg->group[idx].valid)
			continue;

		/*
		 * A boost group affects a CPU only if it has
		 * RUNNABLE tasks on that CPU or it has hold
		 * in effect from a previous task.
		 */
		if (!schedtune_boost_group_active(idx, bg, now))
			continue;

		/* This boost group is active */
		if (boost_max > bg->group[idx].boost)
			continue;

		boost_max = bg->group[idx].boost;
		boost_ts =  bg->group[idx].ts;
	}

	/* Ensures boost_max is non-negative when all cgroup boost values
	 * are neagtive. Avoids under-accounting of cpu capacity which may cause
	 * task stacking and frequency spikes.*/
	boost_max = max(boost_max, 0);
	bg->boost_max = boost_max;
	bg->boost_ts = boost_ts;
}

static int
schedtune_boostgroup_update(int idx, int boost)
{
	struct boost_groups *bg;
	int cur_boost_max;
	int old_boost;
	int cpu;
	u64 now;

	/* Update per CPU boost groups */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);

		/* CGroups are never associated to non active cgroups */
		BUG_ON(!bg->group[idx].valid);

		/*
		 * Keep track of current boost values to compute the per CPU
		 * maximum only when it has been affected by the new value of
		 * the updated boost group
		 */
		cur_boost_max = bg->boost_max;
		old_boost = bg->group[idx].boost;

		/* Update the boost value of this boost group */
		bg->group[idx].boost = boost;

		/* Check if this update increase current max */
		now = sched_clock_cpu(cpu);
		if (boost > cur_boost_max &&
			schedtune_boost_group_active(idx, bg, now)) {
			bg->boost_max = boost;
			bg->boost_ts = bg->group[idx].ts;

			trace_sched_tune_boostgroup_update(cpu, 1, bg->boost_max);
			continue;
		}

		/* Check if this update has decreased current max */
		if (cur_boost_max == old_boost && old_boost > boost) {
			schedtune_cpu_update(cpu, now);
			trace_sched_tune_boostgroup_update(cpu, -1, bg->boost_max);
			continue;
		}

		trace_sched_tune_boostgroup_update(cpu, 0, bg->boost_max);
	}

	return 0;
}

#define ENQUEUE_TASK  1
#define DEQUEUE_TASK -1

static inline bool
schedtune_update_timestamp(struct task_struct *p)
{
	if (sched_feat(SCHEDTUNE_BOOST_HOLD_ALL))
		return true;

	return task_has_rt_policy(p);
}

static inline void
schedtune_tasks_update(struct task_struct *p, int cpu, int idx, int task_count)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	int tasks = bg->group[idx].tasks + task_count;

	/* Update boosted tasks count while avoiding to make it negative */
	bg->group[idx].tasks = max(0, tasks);

	/* Update timeout on enqueue */
	if (task_count > 0) {
		u64 now = sched_clock_cpu(cpu);

		if (schedtune_update_timestamp(p))
			bg->group[idx].ts = now;

		/* Boost group activation or deactivation on that RQ */
		if (bg->group[idx].tasks == 1)
			schedtune_cpu_update(cpu, now);
	}

	trace_sched_tune_tasks_update(p, cpu, tasks, idx,
			bg->group[idx].boost, bg->boost_max,
			bg->group[idx].ts);
}

/*
 * NOTE: This function must be called while holding the lock on the CPU RQ
 */
void schedtune_enqueue_task(struct task_struct *p, int cpu)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	unsigned long irq_flags;
	int idx;

	if (unlikely(!schedtune_initialized))
		return;

	/*
	 * Boost group accouting is protected by a per-cpu lock and requires
	 * interrupt to be disabled to avoid race conditions for example on
	 * do_exit()::cgroup_exit() and task migration.
	 */
	raw_spin_lock_irqsave(&bg->lock, irq_flags);

	idx = p->stune_idx;

	schedtune_tasks_update(p, cpu, idx, ENQUEUE_TASK);

	raw_spin_unlock_irqrestore(&bg->lock, irq_flags);
}

int schedtune_can_attach(struct cgroup_taskset *tset)
{
	return 0;
}

#ifdef CONFIG_SCHED_WALT
static u64 sched_colocate_read(struct cgroup_subsys_state *css,
						struct cftype *cft)
{
	struct schedtune *st = css_st(css);

	return st->colocate;
}

static int sched_colocate_write(struct cgroup_subsys_state *css,
				struct cftype *cft, u64 colocate)
{
	struct schedtune *st = css_st(css);

	if (st->colocate_update_disabled)
		return -EPERM;

	st->colocate = !!colocate;
	st->colocate_update_disabled = true;
	return 0;
}

bool schedtune_task_colocated(struct task_struct *p)
{
	struct schedtune *st;
	bool colocated;

	if (unlikely(!schedtune_initialized))
		return false;

	/* Get task boost value */
	rcu_read_lock();
	st = task_schedtune(p);
	colocated = st->colocate;
	rcu_read_unlock();

	return colocated;
}

#else /* CONFIG_SCHED_WALT */

static inline void init_sched_boost(struct schedtune *st) { }

#endif /* CONFIG_SCHED_WALT */

void schedtune_cancel_attach(struct cgroup_taskset *tset)
{
	/* This can happen only if SchedTune controller is mounted with
	 * other hierarchies ane one of them fails. Since usually SchedTune is
	 * mouted on its own hierarcy, for the time being we do not implement
	 * a proper rollback mechanism */
	WARN(1, "SchedTune cancel attach not implemented");
}

/*
 * NOTE: This function must be called while holding the lock on the CPU RQ
 */
void schedtune_dequeue_task(struct task_struct *p, int cpu)
{
	struct boost_groups *bg = &per_cpu(cpu_boost_groups, cpu);
	unsigned long irq_flags;
	int idx;

	if (unlikely(!schedtune_initialized))
		return;

	/*
	 * Boost group accouting is protected by a per-cpu lock and requires
	 * interrupt to be disabled to avoid race conditions on...
	 */
	raw_spin_lock_irqsave(&bg->lock, irq_flags);

	idx = p->stune_idx;

	schedtune_tasks_update(p, cpu, idx, DEQUEUE_TASK);

	raw_spin_unlock_irqrestore(&bg->lock, irq_flags);
}

int schedtune_cpu_boost_with(int cpu, struct task_struct *p)
{
	struct boost_groups *bg;
	u64 now;
	int task_boost = p ? schedtune_task_boost(p) : -100;

	bg = &per_cpu(cpu_boost_groups, cpu);
	now = sched_clock_cpu(cpu);

	/* Check to see if we have a hold in effect */
	if (schedtune_boost_timeout(now, bg->boost_ts))
		schedtune_cpu_update(cpu, now);

	return max(bg->boost_max, task_boost);
}

int schedtune_task_boost(struct task_struct *p)
{
	struct schedtune *st;
	int task_boost;

	if (unlikely(!schedtune_initialized))
		return 0;

	/* Get task boost value */
	rcu_read_lock();
	st = task_schedtune(p);
	task_boost = st->boost;
#if IS_ENABLED(CONFIG_MIHW)
	if (sched_boost_top_app()) {
		if (1 == st->sched_boost_no_override)
			task_boost = 20;
		if (is_critical_task(p))
			task_boost = 40;
	}
#endif
#if IS_ENABLED(CONFIG_PERF_HUMANTASK)
	if (p->human_task)
		task_boost = 40;
#endif
	rcu_read_unlock();

	return task_boost;
}

int schedtune_prefer_idle(struct task_struct *p)
{
	struct schedtune *st;
	int prefer_idle;

	if (unlikely(!schedtune_initialized))
		return 0;

	/* Get prefer_idle value */
	rcu_read_lock();
	st = task_schedtune(p);
	prefer_idle = st->prefer_idle;
#if IS_ENABLED(CONFIG_MIHW)
	if (sched_boost_top_app()
		&& 1 == st->sched_boost_no_override)
		prefer_idle = 1;
#endif
	rcu_read_unlock();

	return prefer_idle;
}

static u64
prefer_idle_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct schedtune *st = css_st(css);

	return st->prefer_idle;
}

static int
prefer_idle_write(struct cgroup_subsys_state *css, struct cftype *cft,
	    u64 prefer_idle)
{
	struct schedtune *st = css_st(css);
	st->prefer_idle = !!prefer_idle;

	return 0;
}

static s64
boost_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct schedtune *st = css_st(css);

	return st->boost;
}

static void schedtune_attach(struct cgroup_taskset *tset)
{
	struct task_struct *task;
	struct cgroup_subsys_state *css;
	struct boost_groups *bg;
	struct rq_flags rq_flags;
	unsigned int cpu;
	struct rq *rq;
	int src_idx; /* Source boost group index */
	int dst_idx; /* Destination boost group index */
	int tasks;
	u64 now;
#ifdef CONFIG_SCHED_WALT
	struct schedtune *st;
	bool colocate;

	cgroup_taskset_first(tset, &css);
	st = css_st(css);

	colocate = st->colocate;

	cgroup_taskset_for_each(task, css, tset)
		sync_cgroup_colocation(task, colocate);
#endif

	cgroup_taskset_for_each(task, css, tset) {
		/*
		 * Lock the CPU's RQ the task is enqueued to avoid race
		 * conditions with migration code while the task is being
		 * accounted
		 */
		rq = task_rq_lock(task, &rq_flags);

		/*
		 * Boost group accouting is protected by a per-cpu lock and
		 * requires interrupt to be disabled to avoid race conditions
		 * on...
		 */
		cpu = cpu_of(rq);
		bg = &per_cpu(cpu_boost_groups, cpu);
		raw_spin_lock(&bg->lock);

		dst_idx = task_schedtune(task)->idx;
		src_idx = task->stune_idx;

		/*
		 * Current task is not changing boostgroup, which can
		 * happen when the new hierarchy is in use.
		 */
		if (unlikely(dst_idx == src_idx)) {
			raw_spin_unlock(&bg->lock);
			task_rq_unlock(rq, task, &rq_flags);
			continue;
		}

		task->stune_idx = dst_idx;

		if (!task_on_rq_queued(task)) {
			raw_spin_unlock(&bg->lock);
			task_rq_unlock(rq, task, &rq_flags);
			continue;
		}

		/*
		 * This is the case of a RUNNABLE task which is switching its
		 * current boost group.
		 */

		/* Move task from src to dst boost group */
		tasks = bg->group[src_idx].tasks - 1;
		bg->group[src_idx].tasks = max(0, tasks);
		bg->group[dst_idx].tasks += 1;

		/* Update boost hold start for this group */
		now = sched_clock_cpu(cpu);
		bg->group[dst_idx].ts = now;

		/* Force boost group re-evaluation at next boost check */
		bg->boost_ts = now - SCHEDTUNE_BOOST_HOLD_NS;

		raw_spin_unlock(&bg->lock);
		task_rq_unlock(rq, task, &rq_flags);
	}
}

static int
boost_write(struct cgroup_subsys_state *css, struct cftype *cft,
	    s64 boost)
{
	struct schedtune *st = css_st(css);

	if (boost < 0 || boost > 100)
		return -EINVAL;

	st->boost = boost;
#ifdef CONFIG_DYNAMIC_STUNE_BOOST
	st->boost_default = boost;
#endif /* CONFIG_DYNAMIC_STUNE_BOOST */

	/* Update CPU boost */
	schedtune_boostgroup_update(st->idx, st->boost);

	return 0;
}


#ifdef CONFIG_STUNE_ASSIST
static int sched_boost_override_write_wrapper(struct cgroup_subsys_state *css,
					      struct cftype *cft, u64 override)
{
	if (task_is_booster(current))
		return 0;

	return sched_boost_override_write(css, cft, override);
}

#ifdef CONFIG_SCHED_WALT
static int sched_colocate_write_wrapper(struct cgroup_subsys_state *css,
					struct cftype *cft, u64 colocate)
{
	if (task_is_booster(current))
		return 0;

	return sched_colocate_write(css, cft, colocate);
}
#endif

static int boost_write_wrapper(struct cgroup_subsys_state *css,
			       struct cftype *cft, s64 boost)
{
	if (task_is_booster(current))
		return 0;

	return boost_write(css, cft, boost);
}

static int prefer_idle_write_wrapper(struct cgroup_subsys_state *css,
				     struct cftype *cft, u64 prefer_idle)
{
	if (task_is_booster(current))
		return 0;

	return prefer_idle_write(css, cft, prefer_idle);
}
#endif

#ifdef CONFIG_DYNAMIC_STUNE_BOOST
static s64
sched_boost_read(struct cgroup_subsys_state *css, struct cftype *cft)
{
	struct schedtune *st = css_st(css);

	return st->sched_boost;
}

static int
sched_boost_write(struct cgroup_subsys_state *css, struct cftype *cft,
	    s64 sched_boost)
{
	struct schedtune *st = css_st(css);
	st->sched_boost = sched_boost;

	return 0;
}

static void
boost_slots_init(struct schedtune *st)
{
	int i;
	struct boost_slot *slot;

	/* Initialize boost slots */
	INIT_LIST_HEAD(&st->active_boost_slots.list);
	INIT_LIST_HEAD(&st->available_boost_slots.list);

	/* Populate available_boost_slots */
	for (i = 0; i < DYNAMIC_BOOST_SLOTS_COUNT; ++i) {
		slot = kmalloc(sizeof(*slot), GFP_KERNEL);
		slot->idx = i;
		list_add_tail(&slot->list, &st->available_boost_slots.list);
	}
}

static void
boost_slots_release(struct schedtune *st)
{
	struct boost_slot *slot, *next_slot;

	list_for_each_entry_safe(slot, next_slot,
				 &st->available_boost_slots.list, list) {
		list_del(&slot->list);
		kfree(slot);
	}
	list_for_each_entry_safe(slot, next_slot,
				 &st->active_boost_slots.list, list) {
		list_del(&slot->list);
		kfree(slot);
	}
}
#endif // CONFIG_DYNAMIC_STUNE_BOOST

static u64 prefer_high_cap_read(struct cgroup_subsys_state *css,
				struct cftype *cft)
{
	return 0;
}

static int prefer_high_cap_write(struct cgroup_subsys_state *css,
				 struct cftype *cft, u64 prefer_high_cap)
{
	return 0;
}

static struct cftype files[] = {
#ifdef CONFIG_SCHED_WALT
	{
		.name = "sched_boost_no_override",
		.read_u64 = sched_boost_override_read,
		.write_u64 = sched_boost_override_write,
	},
	{
		.name = "colocate",
		.read_u64 = sched_colocate_read,
		.write_u64 = sched_colocate_write,
	},
#endif
	{
		.name = "boost",
		.read_s64 = boost_read,
		.write_s64 = boost_write,
	},
	{
		.name = "prefer_idle",
		.read_u64 = prefer_idle_read,
		.write_u64 = prefer_idle_write,
	},
#ifdef CONFIG_DYNAMIC_STUNE_BOOST
	{
		.name = "sched_boost",
		.read_s64 = sched_boost_read,
		.write_s64 = sched_boost_write,
	},
#endif // CONFIG_DYNAMIC_STUNE_BOOST
	{ }	/* terminate */
};

static void
schedtune_boostgroup_init(struct schedtune *st, int idx)
{
	struct boost_groups *bg;
	int cpu;

#ifdef CONFIG_DYNAMIC_STUNE_BOOST
	boost_slots_init(st);
#endif // CONFIG_DYNAMIC_STUNE_BOOST

	/* Initialize per CPUs boost group support */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);
		bg->group[idx].boost = 0;
		bg->group[idx].valid = true;
		bg->group[idx].ts = 0;
	}

	/* Keep track of allocated boost groups */
	allocated_group[idx] = st;
	st->idx = idx;
}

static struct cgroup_subsys_state *
schedtune_css_alloc(struct cgroup_subsys_state *parent_css)
{
	struct schedtune *st;
	int idx;

	if (!parent_css)
		return &root_schedtune.css;

	/* Allow only single level hierachies */
	if (parent_css != &root_schedtune.css) {
		pr_err("Nested SchedTune boosting groups not allowed\n");
		return ERR_PTR(-ENOMEM);
	}

	/* Allow only a limited number of boosting groups */
	for (idx = 1; idx < BOOSTGROUPS_COUNT; ++idx)
		if (!allocated_group[idx])
			break;
	if (idx == BOOSTGROUPS_COUNT) {
		pr_err("Trying to create more than %d SchedTune boosting groups\n",
		       BOOSTGROUPS_COUNT);
		return ERR_PTR(-ENOSPC);
	}

	st = kzalloc(sizeof(*st), GFP_KERNEL);
	if (!st)
		goto out;

	/* Initialize per CPUs boost group support */
	init_sched_boost(st);
	schedtune_boostgroup_init(st, idx);

	return &st->css;

out:
	return ERR_PTR(-ENOMEM);
}

static void
schedtune_boostgroup_release(struct schedtune *st)
{
#ifdef CONFIG_DYNAMIC_STUNE_BOOST
	/* Free dynamic boost slots */
	boost_slots_release(st);
#endif // CONFIG_DYNAMIC_STUNE_BOOST

	struct boost_groups *bg;
	int cpu;

	/* Reset per CPUs boost group support */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);
		bg->group[st->idx].valid = false;
		bg->group[st->idx].boost = 0;
	}

	/* Keep track of allocated boost groups */
	allocated_group[st->idx] = NULL;
}

static void
schedtune_css_free(struct cgroup_subsys_state *css)
{
	struct schedtune *st = css_st(css);

	/* Release per CPUs boost group support */
	schedtune_boostgroup_release(st);
	kfree(st);
}

struct cgroup_subsys schedtune_cgrp_subsys = {
	.css_alloc	= schedtune_css_alloc,
	.css_free	= schedtune_css_free,
	.attach		= schedtune_attach,
	.can_attach	= schedtune_can_attach,
	.cancel_attach	= schedtune_cancel_attach,
	.legacy_cftypes	= files,
	.early_init	= 1,
};

static inline void
schedtune_init_cgroups(void)
{
	struct boost_groups *bg;
	int cpu;

	/* Initialize the per CPU boost groups */
	for_each_possible_cpu(cpu) {
		bg = &per_cpu(cpu_boost_groups, cpu);
		memset(bg, 0, sizeof(struct boost_groups));
		bg->group[0].valid = true;
		raw_spin_lock_init(&bg->lock);
	}

	pr_info("schedtune: configured to support %d boost groups\n",
		BOOSTGROUPS_COUNT);

	schedtune_initialized = true;
}

#ifdef CONFIG_DYNAMIC_STUNE_BOOST
static struct schedtune *stune_get_by_name(char *st_name)
{
	int idx;

	for (idx = 1; idx < BOOSTGROUPS_COUNT; ++idx) {
		char name_buf[NAME_MAX + 1];
		struct schedtune *st = allocated_group[idx];

		if (!st) {
			pr_warn("schedtune: could not find %s\n", st_name);
			break;
		}

		cgroup_name(st->css.cgroup, name_buf, sizeof(name_buf));
		if (strncmp(name_buf, st_name, strlen(st_name)) == 0)
			return st;
	}

	return NULL;
}

static int dynamic_boost(struct schedtune *st, int boost)
{
	int ret;
	/* Backup boost_default */
	int boost_default_backup = st->boost_default;

	ret = boost_write(&st->css, NULL, boost);

	/* Restore boost_default */
	st->boost_default = boost_default_backup;

	return ret;
}

static inline bool is_valid_boost_slot(int slot)
{
	return slot >= 0 && slot < DYNAMIC_BOOST_SLOTS_COUNT;
}

static int activate_boost_slot(struct schedtune *st, int boost, int *slot)
{
	int ret = 0;
	struct boost_slot *curr_slot;
	struct list_head *head;
	*slot = -1;

	mutex_lock(&boost_slot_mutex);

	/* Check for slots in available_boost_slots */
	if (list_empty(&st->available_boost_slots.list)) {
		ret = -EINVAL;
		goto exit;
	}

	/*
	 * Move one slot from available_boost_slots to active_boost_slots
	 */

	/* Get first slot from available_boost_slots */
	head = &st->available_boost_slots.list;
	curr_slot = list_first_entry(head, struct boost_slot, list);

	/* Store slot value and boost value*/
	*slot = curr_slot->idx;
	st->slot_boost[*slot] = boost;

	/* Delete slot from available_boost_slots */
	list_del(&curr_slot->list);
	kfree(curr_slot);

	/* Create new slot with same value at tail of active_boost_slots */
	curr_slot = kmalloc(sizeof(*curr_slot), GFP_KERNEL);
	curr_slot->idx = *slot;
	list_add_tail(&curr_slot->list, &st->active_boost_slots.list);

exit:
	mutex_unlock(&boost_slot_mutex);
	return ret;
}

static int deactivate_boost_slot(struct schedtune *st, int slot)
{
	int ret = 0;
	struct boost_slot *curr_slot, *next_slot;

	mutex_lock(&boost_slot_mutex);

	if (!is_valid_boost_slot(slot)) {
		ret = -EINVAL;
		goto exit;
	}

	/* Delete slot from active_boost_slots */
	list_for_each_entry_safe(curr_slot, next_slot,
				 &st->active_boost_slots.list, list) {
		if (curr_slot->idx == slot) {
			st->slot_boost[slot] = 0;
			list_del(&curr_slot->list);
			kfree(curr_slot);

			/* Create same slot at tail of available_boost_slots */
			curr_slot = kmalloc(sizeof(*curr_slot), GFP_KERNEL);
			curr_slot->idx = slot;
			list_add_tail(&curr_slot->list,
				      &st->available_boost_slots.list);

			goto exit;
		}
	}

	/* Reaching here means that we did not find the slot to delete */
	ret = -EINVAL;

exit:
	mutex_unlock(&boost_slot_mutex);
	return ret;
}

static int max_active_boost(struct schedtune *st)
{
	struct boost_slot *slot;
	int max_boost;

	mutex_lock(&boost_slot_mutex);
	mutex_lock(&stune_boost_mutex);

	/* Set initial value to default boost */
	max_boost = st->boost_default;

	/* Check for active boosts */
	if (list_empty(&st->active_boost_slots.list)) {
		goto exit;
	}

	/* Get largest boost value */
	list_for_each_entry(slot, &st->active_boost_slots.list, list) {
		int boost = st->slot_boost[slot->idx];
		if (boost > max_boost)
			max_boost = boost;
	}

exit:
	mutex_unlock(&stune_boost_mutex);
	mutex_unlock(&boost_slot_mutex);

	return max_boost;
}

static int _do_stune_boost(struct schedtune *st, int boost, int *slot)
{
	int ret = 0;

	/* Try to obtain boost slot */
	ret = activate_boost_slot(st, boost, slot);

	/* Check if boost slot obtained successfully */
	if (ret)
		return -EINVAL;

	/* Boost if new value is greater than current */
	mutex_lock(&stune_boost_mutex);
	if (boost > st->boost)
		ret = dynamic_boost(st, boost);
	mutex_unlock(&stune_boost_mutex);

	return ret;
}

int reset_stune_boost(char *st_name, int slot)
{
	int ret = 0;
	int boost = 0;
	struct schedtune *st = stune_get_by_name(st_name);

	if (!st)
		return -EINVAL;

	ret = deactivate_boost_slot(st, slot);
	if (ret)
		return -EINVAL;

	/* Find next largest active boost or reset to default */
	boost = max_active_boost(st);

	mutex_lock(&stune_boost_mutex);
	/* Boost only if value changed */
	if (boost != st->boost)
		ret = dynamic_boost(st, boost);
	mutex_unlock(&stune_boost_mutex);

	return ret;
}

int do_stune_sched_boost(char *st_name, int *slot)
{
	struct schedtune *st = stune_get_by_name(st_name);

	if (!st)
		return -EINVAL;

	return _do_stune_boost(st, st->sched_boost, slot);
}

int do_stune_boost(char *st_name, int boost, int *slot)
{
	struct schedtune *st = stune_get_by_name(st_name);

	if (!st)
		return -EINVAL;

	return _do_stune_boost(st, boost, slot);
}

#endif /* CONFIG_DYNAMIC_STUNE_BOOST */



/*
 * Initialize the cgroup structures
 */
static int
schedtune_init(void)
{
	schedtune_spc_rdiv = reciprocal_value(100);
	schedtune_init_cgroups();
	return 0;
}
postcore_initcall(schedtune_init);
