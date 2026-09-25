/*
 * linux/drivers/char/power_dev.c
 *
 * Android / Linux Power Management, Opportunistic Suspend, & WakeLock
 * subsystem for SIX (/sys/power/* and /proc/wakelocks).
 *
 * Character device major 60 ("power"):
 *   minor 0: /sys/power/state          (freeze, mem, on)
 *   minor 1: /sys/power/wake_lock      (acquire userspace wakelock)
 *   minor 2: /sys/power/wake_unlock    (release userspace wakelock)
 *   minor 3: /sys/power/wakeup_count   (race-free atomic wakeup counter)
 *   minor 4: /sys/power/suspend_stats  (suspend/resume telemetry & wakeup reasons)
 *   minor 5: /sys/power/wakealarm      (RTC wakeup timer in s or ms)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/fcntl.h>
#include <asm/segment.h>
#include <asm/system.h>

#include "../../arch/six/kernel/host.h"

#define POWER_MAJOR		60
#define MAX_WAKELOCKS		32

#define PM_MINOR_STATE		0
#define PM_MINOR_WAKE_LOCK	1
#define PM_MINOR_WAKE_UNLOCK	2
#define PM_MINOR_WAKEUP_COUNT	3
#define PM_MINOR_SUSPEND_STATS	4
#define PM_MINOR_WAKEALARM	5

struct six_wakelock {
	int in_use;
	char name[48];
	int active;
	unsigned long count;
	unsigned long expire_count;
	unsigned long wake_count;
	unsigned long active_since_jiffies;
	unsigned long total_time_ms;
	unsigned long max_time_ms;
	unsigned long last_change_jiffies;
};

struct six_suspend_stats {
	unsigned long success;
	unsigned long fail;
	unsigned long failed_freeze;
	unsigned long failed_suspend;
	unsigned long last_sleep_time_ms;
	unsigned long total_sleep_time_ms;
	char last_failed_step[64];
	char last_wakeup_reason[64];
};

static struct six_wakelock wl_table[MAX_WAKELOCKS];
static struct six_suspend_stats pm_stats = {
	0, 0, 0, 0, 0, 0, "none", "boot"
};
static unsigned long pm_wakeup_event_count = 0;
static unsigned long pm_saved_wakeup_count = 0;
static int pm_events_check_enabled = 0;
static int pm_autosuspend_enabled = 0;
static int pm_in_suspend = 0;
static int pm_wakealarm_ms = 200; /* default 200ms RTC alarm if none specified */

extern int sadb_get_active_host_fds(int *fds_out, int max_fds);
extern void sadb_dev_poll(void);

static int pm_enter_suspend(const char *state_str);

static struct six_wakelock *find_or_alloc_wakelock(const char *name, int create)
{
	int i, free_idx = -1;

	for (i = 0; i < MAX_WAKELOCKS; i++) {
		if (wl_table[i].in_use) {
			if (strcmp(wl_table[i].name, name) == 0)
				return &wl_table[i];
		} else if (free_idx < 0) {
			free_idx = i;
		}
	}
	if (!create || free_idx < 0)
		return NULL;

	memset(&wl_table[free_idx], 0, sizeof(struct six_wakelock));
	wl_table[free_idx].in_use = 1;
	strncpy(wl_table[free_idx].name, name, sizeof(wl_table[free_idx].name) - 1);
	wl_table[free_idx].name[sizeof(wl_table[free_idx].name) - 1] = '\0';
	return &wl_table[free_idx];
}

static const char *pm_first_active_wakelock(void)
{
	int i;
	for (i = 0; i < MAX_WAKELOCKS; i++) {
		if (wl_table[i].in_use && wl_table[i].active)
			return wl_table[i].name;
	}
	return NULL;
}

static const char *pm_first_active_non_display_wakelock(void)
{
	int i;
	for (i = 0; i < MAX_WAKELOCKS; i++) {
		if (wl_table[i].in_use && wl_table[i].active &&
		    strcmp(wl_table[i].name, "PowerManagerService.Display") != 0)
			return wl_table[i].name;
	}
	return NULL;
}

int pm_wake_lock(const char *name)
{
	struct six_wakelock *wl;

	if (!name || !name[0])
		return -EINVAL;
	wl = find_or_alloc_wakelock(name, 1);
	if (!wl)
		return -ENOMEM;

	if (!wl->active) {
		wl->active = 1;
		wl->count++;
		wl->active_since_jiffies = jiffies;
	}
	wl->last_change_jiffies = jiffies;
	if (strcmp(name, "PowerManagerService.Display") == 0)
		pm_autosuspend_enabled = 0;
	return 0;
}

int pm_wake_unlock(const char *name)
{
	struct six_wakelock *wl;
	unsigned long held_ms;
	int is_display_unlock = 0;

	if (!name || !name[0])
		return -EINVAL;
	wl = find_or_alloc_wakelock(name, 0);
	if (!wl)
		return -ENOENT;

	if (strcmp(name, "PowerManagerService.Display") == 0) {
		is_display_unlock = 1;
		pm_autosuspend_enabled = 1;
	}

	if (wl->active) {
		held_ms = (jiffies - wl->active_since_jiffies) * (1000UL / HZ);
		if (held_ms == 0)
			held_ms = 1;
		wl->total_time_ms += held_ms;
		if (held_ms > wl->max_time_ms)
			wl->max_time_ms = held_ms;
		wl->active = 0;
		wl->active_since_jiffies = 0;
		wl->wake_count++;
		pm_wakeup_event_count++;
	}
	wl->last_change_jiffies = jiffies;

	/*
	 * Android SystemSuspend Opportunistic Autosuspend:
	 *  - If Display just turned off (PowerManagerService.Display unlocked),
	 *    attempt opportunistic suspend immediately (which succeeds if no
	 *    other wakelock is held, or defers and arms autosuspend if one is).
	 *  - If autosuspend is armed and the LAST blocking wakelock was just
	 *    unlocked, automatically enter suspend without requiring userspace
	 *    to write to /sys/power/state!
	 */
	if (!pm_in_suspend && pm_autosuspend_enabled) {
		const char *blocker = pm_first_active_wakelock();
		if (!blocker) {
			if (!is_display_unlock) {
				printk("SystemSuspend: '%s' unlocked -> entering suspend (mem)\n",
				       name);
			}
			pm_saved_wakeup_count = pm_wakeup_event_count;
			pm_events_check_enabled = 1;
			pm_enter_suspend("mem");
		} else if (is_display_unlock) {
			pm_enter_suspend("mem");
			printk("SystemSuspend: autosuspend armed (blocked by '%s')\n",
			       blocker);
		}
	}
	return 0;
}

unsigned long pm_get_total_sleep_ms(void)
{
	return pm_stats.total_sleep_time_ms;
}

void pm_notify_d_state_cleared(void)
{
	if (!pm_in_suspend && pm_autosuspend_enabled && !pm_first_active_wakelock()) {
		printk("SystemSuspend: D-state tasks cleared -> entering suspend (mem)\n");
		pm_saved_wakeup_count = pm_wakeup_event_count;
		pm_events_check_enabled = 1;
		pm_enter_suspend("mem");
	}
}

static int pm_enter_suspend(const char *state_str)
{
	const char *active_wl;
	int frozen_tasks = 0;
	struct task_struct *p;
	int sadb_fds[8];
	int num_sadb = 0;
	char wake_reason[64] = "irq:8:rtc_alarm";
	unsigned long slept_ms = 0;

	printk("PM: suspend entry (%s)\n", state_str);

	/* 1. Check active WakeLocks (ignoring Display if direct /sys/power/state write) */
	active_wl = pm_first_active_non_display_wakelock();
	if (active_wl) {
		pm_stats.fail++;
		sprintf(pm_stats.last_failed_step, "wakeup_source_active(%s)", active_wl);
		printk("PM: Wakeup pending, aborting suspend (active wakelock: %s)\n", active_wl);
		printk("PM: suspend exit\n");
		return -EBUSY;
	}

	if (pm_events_check_enabled && pm_wakeup_event_count != pm_saved_wakeup_count) {
		pm_stats.fail++;
		strcpy(pm_stats.last_failed_step, "wakeup_count_mismatch");
		pm_events_check_enabled = 0;
		printk("PM: Wakeup event count changed (%lu != %lu), aborting suspend\n",
		       pm_wakeup_event_count, pm_saved_wakeup_count);
		printk("PM: suspend exit\n");
		return -EBUSY;
	}
	pm_events_check_enabled = 0;
	pm_in_suspend = 1;

	/* Ensure Display wakelock is marked inactive during suspend */
	{
		struct six_wakelock *dwl = find_or_alloc_wakelock("PowerManagerService.Display", 0);
		if (dwl && dwl->active) {
			unsigned long held_ms = (jiffies - dwl->active_since_jiffies) * (1000UL / HZ);
			if (held_ms == 0) held_ms = 1;
			dwl->total_time_ms += held_ms;
			if (held_ms > dwl->max_time_ms) dwl->max_time_ms = held_ms;
			dwl->active = 0;
			dwl->active_since_jiffies = 0;
			dwl->wake_count++;
		}
	}

	/* 2. Sync filesystems */
	printk("PM: Syncing filesystems ... ");
	{
		extern int sys_sync(void);
		sys_sync();
	}
	printk("done.\n");

	/* 3. Freeze user space processes (__refrigerator / try_to_freeze_tasks) */
	printk("Freezing user space processes ... ");
	{
		int d_tasks = 0;
		struct task_struct *first_d = NULL;
		for_each_task(p) {
			if (p && p != current && p->pid > 1) {
				if (p->state == TASK_UNINTERRUPTIBLE) {
					d_tasks++;
					if (!first_d)
						first_d = p;
				} else {
					frozen_tasks++;
				}
			}
		}
		if (d_tasks > 0) {
			printk("\nFreezing of tasks failed after 0.01 seconds (%d tasks refusing to freeze, wq_busy=0):\n",
			       d_tasks);
			for_each_task(p) {
				if (p && p != current && p->pid > 1 && p->state == TASK_UNINTERRUPTIBLE) {
					printk("  task:%-15s state:D pid:%-5d ppid:%-5d (uninterruptible sleep)\n",
					       p->comm, p->pid, p->p_pptr ? p->p_pptr->pid : 0);
				}
			}
			printk("Restarting tasks ... done.\n");
			printk("PM: suspend exit\n");
			if (pm_autosuspend_enabled) {
				printk("SystemSuspend: autosuspend armed (blocked by D-state task '%s':%d)\n",
				       first_d->comm, first_d->pid);
			}
			pm_stats.fail++;
			pm_stats.failed_freeze++;
			sprintf(pm_stats.last_failed_step, "freeze(%s:%d in D-state)",
				first_d->comm, first_d->pid);
			pm_in_suspend = 0;
			return -EBUSY;
		}
	}
	printk("(elapsed 0.00 seconds) (%d tasks frozen) done.\n", frozen_tasks);

	/* 4. Device Power Management (dpm_suspend -> syscore_suspend) */
	printk("PM: Suspending console(s) and block/net/ipc devices (dpm_suspend_noirq)\n");
	printk("Disabling non-boot CPUs ...\n");
	printk("syscore_suspend: timekeeping suspended, entering PSCI_SYSTEM_SUSPEND (%s)\n",
	       state_str);

	/* 5. Host hardware sleep with ITIMER_REAL gated off */
	num_sadb = sadb_get_active_host_fds(sadb_fds, 8);
	six_host_pm_suspend_enter(pm_wakealarm_ms, sadb_fds, num_sadb,
				  wake_reason, sizeof(wake_reason), &slept_ms);

	/* 6. Timekeeping sleep injection & syscore_resume */
	xtime.tv_sec += slept_ms / 1000UL;
	xtime.tv_usec += (slept_ms % 1000UL) * 1000UL;
	if (xtime.tv_usec >= 1000000L) {
		xtime.tv_sec += xtime.tv_usec / 1000000L;
		xtime.tv_usec %= 1000000L;
	}

	pm_stats.success++;
	pm_stats.last_sleep_time_ms = slept_ms;
	pm_stats.total_sleep_time_ms += slept_ms;
	strncpy(pm_stats.last_wakeup_reason, wake_reason, sizeof(pm_stats.last_wakeup_reason) - 1);
	pm_stats.last_wakeup_reason[sizeof(pm_stats.last_wakeup_reason) - 1] = '\0';
	pm_wakeup_event_count++;

	printk("syscore_resume: woken by %s after %lu ms (CLOCK_BOOTTIME += %lu ms)\n",
	       wake_reason, slept_ms, slept_ms);
	printk("Enabling non-boot CPUs ...\n");
	printk("PM: resume of devices complete\n");
	printk("OOM killer enabled.\n");
	printk("Restarting tasks ... (%d tasks thawed) done.\n", frozen_tasks);
	printk("PM: suspend exit\n");

	pm_in_suspend = 0;
	pm_autosuspend_enabled = 0;
	pm_wake_lock("PowerManagerService.Display");

	sadb_dev_poll();
	return 0;
}

static int power_read(struct inode *inode, struct file *file, char *buf, int count)
{
	int minor = MINOR(inode->i_rdev);
	char kbuf[2048];
	int len = 0, i, rem, err;

	if (!file || file->f_pos > 0)
		return 0;

	switch (minor) {
	case PM_MINOR_STATE:
		len = sprintf(kbuf, "freeze mem on\n");
		break;

	case PM_MINOR_WAKE_LOCK:
		for (i = 0; i < MAX_WAKELOCKS; i++) {
			if (wl_table[i].in_use && wl_table[i].active) {
				len += sprintf(kbuf + len, "%s%s",
					       len > 0 ? " " : "", wl_table[i].name);
			}
		}
		len += sprintf(kbuf + len, "\n");
		break;

	case PM_MINOR_WAKE_UNLOCK:
		for (i = 0; i < MAX_WAKELOCKS; i++) {
			if (wl_table[i].in_use && !wl_table[i].active) {
				len += sprintf(kbuf + len, "%s%s",
					       len > 0 ? " " : "", wl_table[i].name);
			}
		}
		len += sprintf(kbuf + len, "\n");
		break;

	case PM_MINOR_WAKEUP_COUNT:
		if (pm_first_active_non_display_wakelock() != NULL)
			return -EBUSY;
		len = sprintf(kbuf, "%lu\n", pm_wakeup_event_count);
		break;

	case PM_MINOR_SUSPEND_STATS:
		len = sprintf(kbuf,
			      "success: %lu\n"
			      "fail: %lu\n"
			      "failed_freeze: %lu\n"
			      "failed_suspend: %lu\n"
			      "last_failed_step: %s\n"
			      "last_wakeup_reason: %s\n"
			      "last_sleep_time_ms: %lu\n"
			      "total_sleep_time_ms: %lu\n"
			      "wakeup_count: %lu\n"
			      "autosuspend: %s\n",
			      pm_stats.success,
			      pm_stats.fail,
			      pm_stats.failed_freeze,
			      pm_stats.failed_suspend,
			      pm_stats.last_failed_step,
			      pm_stats.last_wakeup_reason,
			      pm_stats.last_sleep_time_ms,
			      pm_stats.total_sleep_time_ms,
			      pm_wakeup_event_count,
			      pm_autosuspend_enabled ? "armed (screen_off)" : "interactive (screen_on)");
		break;

	case PM_MINOR_WAKEALARM:
		len = sprintf(kbuf, "%dms\n", pm_wakealarm_ms);
		break;

	default:
		return -EINVAL;
	}

	rem = len - (int)file->f_pos;
	if (rem <= 0)
		return 0;
	if (count < rem)
		rem = count;
	err = verify_area(VERIFY_WRITE, buf, rem);
	if (err)
		return err;
	memcpy_tofs(buf, kbuf + file->f_pos, rem);
	file->f_pos += rem;
	return rem;
}

static int power_write(struct inode *inode, struct file *file, const char *buf, int count)
{
	int minor = MINOR(inode->i_rdev);
	char kbuf[128];
	int n, err, i;
	char *p;

	if (count <= 0)
		return 0;
	n = count < (int)sizeof(kbuf) - 1 ? count : (int)sizeof(kbuf) - 1;
	err = verify_area(VERIFY_READ, buf, n);
	if (err)
		return err;
	memcpy_fromfs(kbuf, buf, n);
	kbuf[n] = '\0';

	/* Strip leading whitespace and trailing newline/whitespace */
	p = kbuf;
	while (*p == ' ' || *p == '\t')
		p++;
	for (i = 0; p[i]; i++) {
		if (p[i] == '\r' || p[i] == '\n' || p[i] == ' ' || p[i] == '\t') {
			p[i] = '\0';
			break;
		}
	}
	if (!p[0])
		return count;

	switch (minor) {
	case PM_MINOR_STATE:
		if (strcmp(p, "on") == 0) {
			pm_events_check_enabled = 0;
			pm_wake_lock("PowerManagerService.Display");
			return count;
		}
		if (strcmp(p, "autosuspend") == 0) {
			err = pm_wake_unlock("PowerManagerService.Display");
			return (err < 0) ? err : count;
		}
		if (strcmp(p, "mem") == 0 || strcmp(p, "freeze") == 0 || strcmp(p, "standby") == 0) {
			err = pm_enter_suspend(p);
			return (err < 0) ? err : count;
		}
		return -EINVAL;

	case PM_MINOR_WAKE_LOCK:
		err = pm_wake_lock(p);
		return (err < 0) ? err : count;

	case PM_MINOR_WAKE_UNLOCK:
		err = pm_wake_unlock(p);
		return (err < 0) ? err : count;

	case PM_MINOR_WAKEUP_COUNT: {
		unsigned long val = simple_strtoul(p, NULL, 10);
		if (pm_first_active_non_display_wakelock() != NULL || val != pm_wakeup_event_count) {
			pm_stats.fail++;
			strcpy(pm_stats.last_failed_step, "wakeup_count_race");
			return -EINVAL;
		}
		pm_saved_wakeup_count = val;
		pm_events_check_enabled = 1;
		return count;
	}

	case PM_MINOR_WAKEALARM: {
		char *endp = NULL;
		unsigned long val = simple_strtoul(p, &endp, 10);
		if (endp && (strcmp(endp, "ms") == 0 || strcmp(endp, "MS") == 0))
			pm_wakealarm_ms = (int)val;
		else
			pm_wakealarm_ms = (int)(val * 1000UL);
		if (pm_wakealarm_ms <= 0)
			pm_wakealarm_ms = 50;
		return count;
	}

	default:
		return -EINVAL;
	}
}

int get_wakelocks_info(char *buf)
{
	int len = 0, i;

	len += sprintf(buf + len,
		       "name\tcount\texpire_count\twake_count\tactive_since\ttotal_time\tsleep_time\tmax_time\tlast_change\n");
	for (i = 0; i < MAX_WAKELOCKS; i++) {
		if (wl_table[i].in_use) {
			unsigned long active_ms = 0;
			if (wl_table[i].active) {
				active_ms = (jiffies - wl_table[i].active_since_jiffies) * (1000UL / HZ);
				if (active_ms == 0)
					active_ms = 1;
			}
			len += sprintf(buf + len,
				       "\"%s\"\t%lu\t%lu\t%lu\t%lu\t%lu\t0\t%lu\t%lu\n",
				       wl_table[i].name,
				       wl_table[i].count,
				       wl_table[i].expire_count,
				       wl_table[i].wake_count,
				       active_ms,
				       wl_table[i].total_time_ms + active_ms,
				       wl_table[i].max_time_ms,
				       wl_table[i].last_change_jiffies);
		}
	}
	return len;
}

static struct file_operations power_fops = {
	NULL,		/* lseek */
	power_read,	/* read */
	power_write,	/* write */
	NULL,		/* readdir */
	NULL,		/* select */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	NULL,		/* open */
	NULL,		/* release */
	NULL,		/* fsync */
	NULL,		/* fasync */
	NULL,		/* check_media_change */
	NULL		/* revalidate */
};

int power_dev_init(void)
{
	memset(wl_table, 0, sizeof(wl_table));
	if (register_chrdev(POWER_MAJOR, "power", &power_fops) < 0) {
		printk("power: unable to register chrdev major %d\n", POWER_MAJOR);
		return -EIO;
	}
	/* Seed default Android PowerManagerService wakelocks: Display=ON, WakeLocks=0 */
	pm_wake_lock("PowerManagerService.Display");
	pm_wake_lock("PowerManagerService.WakeLocks");
	pm_wake_unlock("PowerManagerService.WakeLocks");
	printk("PM: Android Opportunistic Suspend & WakeLock driver initialized (/sys/power/*, /proc/wakelocks)\n");
	return 0;
}
