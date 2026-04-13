/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_RCUPDATE_TRACE_H
#define _LINUX_RCUPDATE_TRACE_H

#include <linux/rcupdate.h>

static inline void rcu_read_lock_trace(void)
{
	rcu_read_lock();
}

static inline void rcu_read_unlock_trace(void)
{
	rcu_read_unlock();
}

static inline int rcu_read_lock_trace_held(void)
{
	return rcu_read_lock_held();
}

static inline void call_rcu_tasks_trace(struct rcu_head *head,
					rcu_callback_t func)
{
	call_rcu_tasks(head, func);
}

static inline void synchronize_rcu_tasks_trace(void)
{
	synchronize_rcu_tasks();
}

#endif /* _LINUX_RCUPDATE_TRACE_H */
