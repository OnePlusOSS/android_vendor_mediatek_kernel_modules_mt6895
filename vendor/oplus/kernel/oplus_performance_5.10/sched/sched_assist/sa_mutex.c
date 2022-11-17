// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 Oplus. All rights reserved.
 */


#include "sched_assist.h"
#include "sa_common.h"
#include "sa_mutex.h"
#include <linux/version.h>

#define MUTEX_FLAGS		0x07
static inline struct task_struct *__mutex_owner(struct mutex *lock)
{
	return (struct task_struct *)(atomic_long_read(&lock->owner) & ~MUTEX_FLAGS);
}

static void mutex_list_add_ux(struct list_head *entry, struct list_head *head)
{
	struct list_head *pos = NULL;
	struct list_head *n = NULL;
	struct mutex_waiter *waiter = NULL;

	list_for_each_safe(pos, n, head) {
		waiter = list_entry(pos, struct mutex_waiter, list);
		if (!test_task_ux(waiter->task)) {
			list_add(entry, waiter->list.prev);
			return;
		}
	}

	if (pos == head)
		list_add_tail(entry, head);
}

bool mutex_list_add(struct task_struct *task, struct list_head *entry, struct list_head *head, struct mutex *lock)
{
	bool is_ux = test_task_ux(task);

	if (!entry || !head || !lock)
		return false;

	if (is_ux) {
		mutex_list_add_ux(entry, head);
		return true;
	}

	return false;
}

void mutex_set_inherit_ux(struct mutex *lock, struct task_struct *task)
{
	bool is_ux = false;
	struct task_struct *owner = NULL;

	if (!lock)
		return;

	is_ux = test_set_inherit_ux(task);

	owner = __mutex_owner(lock);

	if (is_ux && !test_inherit_ux(owner, INHERIT_UX_MUTEX)) {
		int type = get_ux_state_type(owner);

		if ((type == UX_STATE_NONE) || (type == UX_STATE_INHERIT))
			set_inherit_ux(owner, INHERIT_UX_MUTEX, oplus_get_ux_depth(task), oplus_get_ux_state(task));
	}
}

void mutex_unset_inherit_ux(struct mutex *lock, struct task_struct *task)
{
	if (test_inherit_ux(task, INHERIT_UX_MUTEX))
		unset_inherit_ux(task, INHERIT_UX_MUTEX);
}

/* implement vender hook in kernel/locking/mutex.c */
void android_vh_alter_mutex_list_add_handler(void *unused, struct mutex *lock,
			struct mutex_waiter *waiter, struct list_head *list, bool *already_on_list)
{
	if (unlikely(!global_sched_assist_enabled))
		return;

	*already_on_list = mutex_list_add(current, &waiter->list, list, lock);
}

void android_vh_mutex_wait_start_handler(void *unused, struct mutex *lock)
{
	if (unlikely(!global_sched_assist_enabled))
		return;

	mutex_set_inherit_ux(lock, current);
}

void android_vh_mutex_wait_finish_handler(void *unused, struct mutex *lock)
{
}

void android_vh_mutex_unlock_slowpath_handler(void *unused, struct mutex *lock)
{
	if (unlikely(!global_sched_assist_enabled))
		return;

	mutex_unset_inherit_ux(lock, current);
}
