// SPDX-License-Identifier: GPL-2.0
/*
 * Anxiety I/O Scheduler
 *
 * Copyright (c) 2020, Tyler Nijmeh <tylernij@gmail.com>
 */

#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

/* Batch this many synchronous requests at a time */
#define	DEFAULT_SYNC_RATIO	(4)

enum {
	SYNC,
	ASYNC
};

struct anxiety_data {
	struct list_head queue[2];

	/* Tunables */
	uint8_t sync_ratio;
};

static inline struct request *anxiety_next_entry(struct list_head *queue)
{
	return list_first_entry(queue, struct request,
		queuelist);
}

static void anxiety_merged_requests(struct request_queue *q, struct request *rq,
		struct request *next)
{
	list_del_init(&next->queuelist);
}

static inline int __anxiety_dispatch(struct request_queue *q,
		struct request *rq)
{
	if (unlikely(!rq))
		return -EINVAL;

	list_del_init(&rq->queuelist);
	elv_dispatch_sort(q, rq);

	return 0;
}

static uint16_t anxiety_dispatch_batch(struct request_queue *q)
{
	struct anxiety_data *adata = q->elevator->elevator_data;
	uint8_t i;
	uint16_t dispatched = 0;

	/* Batch sync requests according to tunables */
	for (i = 0; i < adata->sync_ratio; i++) {
		if (list_empty(&adata->queue[SYNC]))
			break;

		__anxiety_dispatch(q,
			anxiety_next_entry(&adata->queue[SYNC]));

		dispatched++;
	}

	/* Submit one async request after the sync batch to avoid starvation */
	if (!list_empty(&adata->queue[ASYNC])) {
		__anxiety_dispatch(q,
			anxiety_next_entry(&adata->queue[ASYNC]));

		dispatched++;
	}

	return dispatched;
}

static uint16_t anxiety_dispatch_drain(struct request_queue *q)
{
	struct anxiety_data *adata = q->elevator->elevator_data;
	uint16_t dispatched = 0;

	/*
	 * Drain out all of the synchronous requests first,
	 * then drain the asynchronous requests.
	 */
	while (!list_empty(&adata->queue[SYNC])) {
		__anxiety_dispatch(q,
			anxiety_next_entry(&adata->queue[SYNC]));

		dispatched++;
	}

	while (!list_empty(&adata->queue[ASYNC])) {
		__anxiety_dispatch(q,
			anxiety_next_entry(&adata->queue[ASYNC]));

		dispatched++;
	}

	return dispatched;
}

static int anxiety_dispatch(struct request_queue *q, int force)
{
	/*
	 * When requested by the elevator, a full queue drain can be
	 * performed in one scheduler dispatch.
	 */
	if (unlikely(force))
		return anxiety_dispatch_drain(q);

	return anxiety_dispatch_batch(q);
}

static void anxiety_add_request(struct request_queue *q, struct request *rq)
{
	const uint8_t dir = rq_is_sync(rq);
	struct anxiety_data *adata = q->elevator->elevator_data;

	list_add_tail(&rq->queuelist, &adata->queue[dir]);
}

static int anxiety_init_queue(struct request_queue *q,
		struct elevator_type *elv)
{
	struct anxiety_data *adata;
	struct elevator_queue *eq = elevator_alloc(q, elv);

	if (!eq)
		return -ENOMEM;

	/* Allocate the data */
	adata = kmalloc_node(sizeof(*adata), GFP_KERNEL, q->node);
	if (!adata) {
		kobject_put(&eq->kobj);
		return -ENOMEM;
	}

	/* Set the elevator data */
	eq->elevator_data = adata;

	/* Initialize */
	INIT_LIST_HEAD(&adata->queue[SYNC]);
	INIT_LIST_HEAD(&adata->queue[ASYNC]);
	adata->sync_ratio = DEFAULT_SYNC_RATIO;

	/* Set elevator to Anxiety */
	spin_lock_irq(q->queue_lock);
	q->elevator = eq;
	spin_unlock_irq(q->queue_lock);

	return 0;
}

/* Sysfs access */
static ssize_t anxiety_sync_ratio_show(struct elevator_queue *e, char *page)
{
	struct anxiety_data *adata = e->elevator_data;

	return snprintf(page, PAGE_SIZE, "%u\n", adata->sync_ratio);
}

static ssize_t anxiety_sync_ratio_store(struct elevator_queue *e,
		const char *page, size_t count)
{
	struct anxiety_data *adata = e->elevator_data;
	int ret;

	ret = kstrtou8(page, 0, &adata->sync_ratio);
	if (ret < 0)
		return ret;

	return count;
}

static struct elv_fs_entry anxiety_attrs[] = {
	__ATTR(sync_ratio, 0644, anxiety_sync_ratio_show,
		anxiety_sync_ratio_store),
	__ATTR_NULL
};

static struct elevator_type elevator_anxiety = {
	.ops.sq = {
		.elevator_merge_req_fn	= anxiety_merged_requests,
		.elevator_dispatch_fn	= anxiety_dispatch,
		.elevator_add_req_fn	= anxiety_add_request,
		.elevator_former_req_fn	= elv_rb_former_request,
		.elevator_latter_req_fn	= elv_rb_latter_request,
		.elevator_init_fn	= anxiety_init_queue,
	},
	.elevator_name = "anxiety",
	.elevator_attrs = anxiety_attrs,
	.elevator_owner = THIS_MODULE,
};

static int __init anxiety_init(void)
{
	return elv_register(&elevator_anxiety);
}

static void __exit anxiety_exit(void)
{
	elv_unregister(&elevator_anxiety);
}

module_init(anxiety_init);
module_exit(anxiety_exit);

MODULE_AUTHOR("Tyler Nijmeh");
MODULE_LICENSE("GPLv3");
MODULE_DESCRIPTION("Anxiety I/O scheduler");
