// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, The Linux Foundation. All rights reserved.
 */

#define pr_fmt(fmt) "touch-boost: " fmt

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/cpufreq.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/time.h>
#include <linux/sysfs.h>
#include <linux/pm_qos.h>
#include <linux/module.h>
#include <linux/atomic.h>

enum {
	UP    = 0,
	DOWN  = 1,
	COUNT = 2,
};

struct cpu_sync {
	int cpu;
	unsigned int touch_boost_min;
};

#define MAX_CLUSTERS 3
static atomic_t touch_boost_duration_ms[COUNT] = {ATOMIC_INIT(70), ATOMIC_INIT(70)};
static int touch_boost_trace_debug = 0;
static int cur_event;
static int cpu_num;

static struct workqueue_struct *touch_boost_wq;
static struct work_struct touch_boost_work;
static struct delayed_work touch_boost_rem;

static DEFINE_PER_CPU(struct cpu_sync, sync_info);
static DEFINE_PER_CPU(struct freq_qos_request, qos_req);


struct touch_boost_freq {
	struct kobject kobj;
	atomic_t touch_boost_freq[COUNT];
};

struct touch_boost_freq tb_array[MAX_CLUSTERS] = {
	/* silver */
	{
		.touch_boost_freq = {
			[DOWN] = ATOMIC_INIT(0),
		},
	},
	/* gold */
	{
		.touch_boost_freq = {
			[DOWN] = ATOMIC_INIT(0),
		},
	},
	/* gold prime */
	{
		.touch_boost_freq = {
			[DOWN] = ATOMIC_INIT(0),
		},
	},
};

struct oplus_touch_boost_attr {
	struct attribute attr;
	ssize_t (*show)(struct touch_boost_freq *tb, char *buf);
	ssize_t (*store)(struct touch_boost_freq *tb, const char *buf,
		size_t count);
};

static noinline int tracing_mark_write(const char *buf)
{
	trace_printk(buf);
	return 0;
}

static void cpu_freq_systrace_c(int cpu, int freq)
{
	if (touch_boost_trace_debug) {
		char buf[256];
		snprintf(buf, sizeof(buf), "C|9999|touch_boost_min_cpu%d|%d\n",
			cpu, freq);
		tracing_mark_write(buf);
	}
}


static ssize_t store_touch_down_boost_freq(struct touch_boost_freq *tb,
	const char *buf, size_t count) {
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;

	atomic_set(&tb->touch_boost_freq[DOWN], val);

	return count;
}

static ssize_t show_touch_down_boost_freq(struct touch_boost_freq *tb,
	char *buf) {

	return scnprintf(buf, PAGE_SIZE, "%u\n",
		atomic_read(&tb->touch_boost_freq[DOWN]));
}

static ssize_t store_touch_up_boost_freq(struct touch_boost_freq *tb,
	const char *buf, size_t count) {
	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;

	atomic_set(&tb->touch_boost_freq[UP], val);

	return count;
}

static ssize_t show_touch_up_boost_freq(struct touch_boost_freq *tb, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%u\n",
			atomic_read(&tb->touch_boost_freq[UP]));
}


#define oplus_touch_boost_freq_attr_ro(_name)		\
static struct oplus_touch_boost_attr _name =	\
__ATTR(_name, 0444, show_##_name, NULL)

#define oplus_touch_boost_freq_attr_rw(_name)			\
static struct oplus_touch_boost_attr _name =		\
__ATTR(_name, 0664, show_##_name, store_##_name)


oplus_touch_boost_freq_attr_rw(touch_down_boost_freq);
oplus_touch_boost_freq_attr_rw(touch_up_boost_freq);


static struct attribute *default_attrs[] = {
	&touch_down_boost_freq.attr,
	&touch_up_boost_freq.attr,
	NULL
};

#define to_oplus_touch_boost(k) \
		container_of(k, struct touch_boost_freq, kobj)
#define to_attr(a) container_of(a, struct oplus_touch_boost_attr, attr)

static ssize_t show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct touch_boost_freq *tb = to_oplus_touch_boost(kobj);
	struct oplus_touch_boost_attr *cattr = to_attr(attr);
	ssize_t ret = -EIO;

	if (cattr->show)
		ret = cattr->show(tb, buf);

	return ret;
}

static ssize_t store(struct kobject *kobj, struct attribute *attr,
		     const char *buf, size_t count)
{
	struct touch_boost_freq *tb = to_oplus_touch_boost(kobj);
	struct oplus_touch_boost_attr *cattr = to_attr(attr);
	ssize_t ret = -EIO;

	if (cattr->store)
		ret = cattr->store(tb, buf, count);

	return ret;
}

static const struct sysfs_ops sysfs_ops = {
	.show	= show,
	.store	= store,
};

static struct kobj_type ktype_oplus_touch_boost = {
	.sysfs_ops	= &sysfs_ops,
	.default_attrs	= default_attrs,
};


static ssize_t store_touch_down_boost_ms(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count) {

	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;

	atomic_set(&touch_boost_duration_ms[DOWN], val);

	return count;
}

static ssize_t show_touch_down_boost_ms(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf) {

	return scnprintf(buf, PAGE_SIZE, "%u\n",
		atomic_read(&touch_boost_duration_ms[DOWN]));
}

static ssize_t store_touch_up_boost_ms(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count) {

	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;

	atomic_set(&touch_boost_duration_ms[UP], val);

	return count;
}

static ssize_t show_touch_up_boost_ms(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf) {

	return scnprintf(buf, PAGE_SIZE, "%u\n",
		atomic_read(&touch_boost_duration_ms[UP]));
}

static ssize_t store_touch_boost_debug(struct kobject *kobj,
	struct kobj_attribute *attr, const char *buf, size_t count) {

	unsigned int val;

	if (sscanf(buf, "%u\n", &val) != 1)
		return -EINVAL;

	touch_boost_trace_debug = !!val;

	return count;
}

static ssize_t show_touch_boost_debug(struct kobject *kobj,
	struct kobj_attribute *attr, char *buf) {

	return scnprintf(buf, PAGE_SIZE, "%u\n", touch_boost_trace_debug);
}


#define oplus_touch_boost_common_attr_rw(_name)	\
	static struct kobj_attribute _name =		\
	__ATTR(_name, 0664, show_##_name, store_##_name)

oplus_touch_boost_common_attr_rw(touch_down_boost_ms);
oplus_touch_boost_common_attr_rw(touch_up_boost_ms);
oplus_touch_boost_common_attr_rw(touch_boost_debug);


static struct attribute *touch_boost_common_attrs[] = {
	&touch_down_boost_ms.attr,
	&touch_up_boost_ms.attr,
	&touch_boost_debug.attr,
	NULL,
};

static struct attribute_group touch_boost_common_attr_group = {
	.attrs = touch_boost_common_attrs,
};

/* Returns 0 on success or error code on failure. */
static int init_touch_boost_common_sysfs(void)
{
	int ret = 0;

	struct kobject *kobj = kobject_create_and_add("touch_boost",
		&cpu_subsys.dev_root->kobj);;
	if (!kobj) {
		pr_err("touch_boost folder create failed\n");
		return -ENOMEM;
	}

	ret = sysfs_create_group(kobj, &touch_boost_common_attr_group);
	if (ret)
		return ret;

	return 0;
}


static void boost_adjust_notify(struct cpufreq_policy *policy)
{
	unsigned int cpu = policy->cpu;
	struct cpu_sync *s = &per_cpu(sync_info, cpu);
	unsigned int ib_min = s->touch_boost_min;
	struct freq_qos_request *req = &per_cpu(qos_req, cpu);
	int ret;
	int orig_min = policy->min;

	cpu_freq_systrace_c(cpu, ib_min);

	ret = freq_qos_update_request(req, ib_min);
	if (ret < 0)
		pr_err("Failed to update freq constraint in boost_adjust: %d\n", ib_min);

	pr_debug("CPU%u policy min before boost: %u kHz, boost min: %u kHz, after"
		"boost: %u kHz\n", cpu, orig_min, ib_min, policy->min);
}

static void update_policy_online(void)
{
	unsigned int i;
	struct cpufreq_policy *policy;
	struct cpumask online_cpus;

	/* Re-evaluate policy to trigger adjust notifier for online CPUs */
	get_online_cpus();
	online_cpus = *cpu_online_mask;
	for_each_cpu(i, &online_cpus) {
		policy = cpufreq_cpu_get(i);
		if (!policy) {
			pr_err("%s: cpufreq policy not found for cpu%d\n",
							__func__, i);
			return;
		}

		cpumask_andnot(&online_cpus, &online_cpus, policy->related_cpus);
		boost_adjust_notify(policy);
	}
	put_online_cpus();
}

static void do_touch_boost_rem(struct work_struct *work)
{
	unsigned int i;
	struct cpu_sync *i_sync_info;

	/* Reset the touch_boost_min for all CPUs in the system */
	pr_debug("Resetting touch boost min for all CPUs\n");
	for_each_possible_cpu(i) {
		i_sync_info = &per_cpu(sync_info, i);
		i_sync_info->touch_boost_min = 0;
	}

	/* Update policies for all online CPUs */
	update_policy_online();

}

static void do_touch_boost(struct work_struct *work)
{
	unsigned int i;
	struct cpu_sync *i_sync_info;
	unsigned int first_cpu;
	int cluster_id;
	int duration_ms;

	cancel_delayed_work_sync(&touch_boost_rem);

	/* Set the touch_boost_min for all CPUs in the system */
	pr_debug("Setting touch boost min for all CPUs\n");
	for (i = 0; i < cpu_num; i++) {
		struct cpufreq_policy *policy;
		struct touch_boost_freq *tb;

		policy = cpufreq_cpu_get(i);
		if (!policy) {
			pr_info("cpu %d, policy is null\n", i);
			continue;
		}
		first_cpu = cpumask_first(policy->related_cpus);
		cluster_id = topology_physical_package_id(first_cpu);
		cpufreq_cpu_put(policy);
		if (cluster_id >= MAX_CLUSTERS)
			continue;
		tb = &tb_array[cluster_id];

		i_sync_info = &per_cpu(sync_info, i);
		i_sync_info->touch_boost_min = atomic_read(&tb->touch_boost_freq[cur_event]);
	}

	/* Update policies for all online CPUs */
	update_policy_online();

	duration_ms = atomic_read(&touch_boost_duration_ms[cur_event]);
	queue_delayed_work(touch_boost_wq, &touch_boost_rem, msecs_to_jiffies(
duration_ms));
}

static void touchboost_input_event(struct input_handle *handle,
		unsigned int type, unsigned int code, int value)
{
	int cpu;
	int enabled = 0;
	unsigned int first_cpu;
	int cluster_id;

	/* Only process touch down(value=1) and up(value=0) */
	if (type != EV_KEY || code != BTN_TOUCH || (value != 1 && value != 0)) {
		return;
	}

	if (work_pending(&touch_boost_work))
		return;

	cur_event = value;

	for_each_possible_cpu(cpu) {
		struct cpufreq_policy *policy;
		struct touch_boost_freq *tb;

		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			pr_info("cpu %d, policy is null\n", cpu);
			continue;
		}
		first_cpu = cpumask_first(policy->related_cpus);
		cluster_id = topology_physical_package_id(first_cpu);
		cpufreq_cpu_put(policy);
		if (cluster_id >= MAX_CLUSTERS)
			continue;
		tb = &tb_array[cluster_id];

		if (atomic_read(&tb->touch_boost_freq[cur_event]) > 0) {
			enabled = 1;
			break;
		}
	}
	if (!enabled)
		return;

	queue_work(touch_boost_wq, &touch_boost_work);
}

static int touchboost_input_connect(struct input_handler *handler,
		struct input_dev *dev, const struct input_device_id *id)
{
	struct input_handle *handle;
	int error;

	handle = kzalloc(sizeof(struct input_handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "touch_boost_cpufreq";

	error = input_register_handle(handle);
	if (error)
		goto err2;

	error = input_open_device(handle);
	if (error)
		goto err1;

	return 0;
err1:
	input_unregister_handle(handle);
err2:
	kfree(handle);
	return error;
}

static void touchboost_input_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

/* Only match touchpanel */
static bool touchboost_input_match(struct input_handler *handler, struct
	input_dev *dev)
{
	const char *dev_match_name = "touchpanel";

	if(strncmp(dev_match_name, dev->name, strlen(dev_match_name)) == 0)
		return true;

	return false;
}

static const struct input_device_id touchboost_ids[] = {
	/* multi-touch touchscreen */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.evbit = { BIT_MASK(EV_ABS) },
		.absbit = { [BIT_WORD(ABS_MT_POSITION_X)] =
			BIT_MASK(ABS_MT_POSITION_X) |
			BIT_MASK(ABS_MT_POSITION_Y)
		},
	},
	/* touchpad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_KEYBIT |
			INPUT_DEVICE_ID_MATCH_ABSBIT,
		.keybit = { [BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH) },
		.absbit = { [BIT_WORD(ABS_X)] =
			BIT_MASK(ABS_X) | BIT_MASK(ABS_Y)
		},
	},
	/* Keypad */
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT,
		.evbit = { BIT_MASK(EV_KEY) },
	},
	{ },
};

static struct input_handler touchboost_input_handler = {
	.event		= touchboost_input_event,
	.match		= touchboost_input_match,
	.connect	= touchboost_input_connect,
	.disconnect	= touchboost_input_disconnect,
	.name		= "touch_boost",
	.id_table	= touchboost_ids,
};

static int cluster_init(int first_cpu, struct device *dev)
{
	int cluster_id;
	struct touch_boost_freq *tb;
	struct cpufreq_policy *policy;

	policy = cpufreq_cpu_get(first_cpu);
	if (!policy)
		return -EINVAL;

	cluster_id = topology_physical_package_id(first_cpu);
	if (cluster_id >= MAX_CLUSTERS) {
		pr_err("Unsupported number of clusters(%d). Only %u supported\n",
				cluster_id, MAX_CLUSTERS);
		cpufreq_cpu_put(policy);
		return -EINVAL;
	}
	pr_info("cluster idx = %d, cpumask = 0x%x\n", cluster_id,
			(int)cpumask_bits(policy->related_cpus)[0]);

	tb = &tb_array[cluster_id];

	cpufreq_cpu_put(policy);

	kobject_init(&tb->kobj, &ktype_oplus_touch_boost);
	return kobject_add(&tb->kobj, &dev->kobj, "touch_boost");
};

static int __init touch_boost_init(void)
{
	int cpu, ret;
	struct cpu_sync *s;
	struct cpufreq_policy *policy;
	struct freq_qos_request *req;
	unsigned int first_cpu;
	struct device *cpu_dev;

	touch_boost_wq = alloc_ordered_workqueue("%s", WQ_HIGHPRI, "touch_boost_wq");
	if (!touch_boost_wq)
		return -EFAULT;

	INIT_WORK(&touch_boost_work, do_touch_boost);
	INIT_DELAYED_WORK(&touch_boost_rem, do_touch_boost_rem);

	for_each_possible_cpu(cpu) {
		s = &per_cpu(sync_info, cpu);
		s->cpu = cpu;
		cpu_num++;
		req = &per_cpu(qos_req, cpu);
		policy = cpufreq_cpu_get(cpu);
		if (!policy) {
			pr_err("%s: cpufreq policy not found for cpu%d\n",
							__func__, cpu);
			return -ESRCH;
		}

		first_cpu = cpumask_first(policy->related_cpus);
		cpu_dev = get_cpu_device(first_cpu);
		if (!cpu_dev)
			return -ENODEV;

		cluster_init(first_cpu, cpu_dev);

#if IS_ENABLED(CONFIG_OPLUS_OMRG)
		ret = freq_qos_add_request(&policy->constraints, req,
						FREQ_QOS_MIN, policy->cpuinfo.min_freq);
#else
		ret = freq_qos_add_request(&policy->constraints, req,
						FREQ_QOS_MIN, policy->min);
#endif
		if (ret < 0) {
			pr_err("%s: Failed to add freq constraint (%d)\n",
							__func__, ret);
			return ret;
		}
	}

	ret = init_touch_boost_common_sysfs();
	if (ret < 0)
		return ret;

	ret = input_register_handler(&touchboost_input_handler);

	return 0;
}

static void __exit touch_boost_exit(void)
{
	pr_info("touch boost exit\n");
}

module_init(touch_boost_init);
module_exit(touch_boost_exit);
MODULE_DESCRIPTION("OPLUS TOUCH BOOST");
MODULE_LICENSE("GPL v2");
