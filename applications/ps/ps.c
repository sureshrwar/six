/*
 * /bin/ps - Report a snapshot of current processes in SIX
 *
 * Supports both UNIX/SysV (-e, -A, -f, -l, -H, -p) and BSD (a, u, x, l, f)
 * options:
 *   ps              Processes on current terminal
 *   ps -ef          SysV full listing (UID, PID, PPID, C, STIME, TTY, TIME, CMD)
 *   ps aux          BSD user-oriented listing (USER, PID, %CPU, %MEM, VSZ, RSS, TTY, STAT, START, TIME, COMMAND)
 *   ps -el / ps al  Long listing including symbolic kernel WCHAN
 *   ps -efH / aufx  ASCII process tree hierarchy (\_ child)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <time.h>
#include <six_proc.h>

#define MAX_PROCS 64
#define HZ_VAL    10

static struct six_proc procs[MAX_PROCS];
static int proc_depth[MAX_PROCS];
static int proc_order[MAX_PROCS];
static int proc_visited[MAX_PROCS];
static int nprocs = 0;

static const char *uid_to_name(int uid)
{
	static char numbuf[16];
	struct passwd *pw = getpwuid(uid);
	if (pw && pw->pw_name && pw->pw_name[0])
		return pw->pw_name;
	if (uid == 0)
		return "root";
	sprintf(numbuf, "%d", uid);
	return numbuf;
}

static char state_char(int state)
{
	switch (state) {
	case 0: return 'R';
	case 1: return 'S';
	case 2: return 'D';
	case 3: return 'Z';
	case 4: return 'T';
	case 5: return 'W';
	default: return '?';
	}
}

static void format_stat_str(struct six_proc *p, char *out)
{
	int pos = 0;
	out[pos++] = state_char(p->state);
	if (p->nice < 0)
		out[pos++] = '<';
	else if (p->nice > 0)
		out[pos++] = 'N';
	if (p->pid == p->session && p->session > 0)
		out[pos++] = 's';
	if (!p->is_kthread && p->tty_nr != 0 && p->pgrp == p->pid)
		out[pos++] = '+';
	out[pos] = '\0';
}

static void format_cputime(struct six_proc *p, char *out, int short_fmt)
{
	unsigned long ticks = p->utime + p->stime;
	unsigned long secs = ticks / HZ_VAL;
	unsigned long mins = secs / 60;
	unsigned long hrs = mins / 60;
	secs %= 60;
	mins %= 60;
	if (short_fmt)
		sprintf(out, "%2lu:%02lu", mins + hrs * 60, secs);
	else
		sprintf(out, "%02lu:%02lu:%02lu", hrs, mins, secs);
}

static void format_stime(struct six_proc *p, time_t now_epoch, char *out)
{
	unsigned long elapsed_j = 0;
	time_t start_epoch;
	struct tm *tm_p;

	if (p->jiffies_now >= p->start_time)
		elapsed_j = p->jiffies_now - p->start_time;
	start_epoch = now_epoch - (time_t)(elapsed_j / HZ_VAL);
	tm_p = localtime(&start_epoch);
	if (tm_p)
		sprintf(out, "%02d:%02d", tm_p->tm_hour, tm_p->tm_min);
	else
		strcpy(out, "00:00");
}

static void build_tree_order(int parent_pid, int depth, int *out_idx)
{
	int i;
	for (i = 0; i < nprocs; i++) {
		if (!proc_visited[i] && procs[i].ppid == parent_pid && procs[i].pid != parent_pid) {
			proc_visited[i] = 1;
			proc_depth[i] = depth;
			proc_order[(*out_idx)++] = i;
			build_tree_order(procs[i].pid, depth + 1, out_idx);
		}
	}
}

static void sort_for_forest(void)
{
	int i, out_idx = 0;
	memset(proc_visited, 0, sizeof(proc_visited));
	for (i = 0; i < nprocs; i++) {
		if (!proc_visited[i] && (procs[i].pid == 0 || procs[i].ppid == 0)) {
			proc_visited[i] = 1;
			proc_depth[i] = 0;
			proc_order[out_idx++] = i;
			build_tree_order(procs[i].pid, 1, &out_idx);
		}
	}
	for (i = 0; i < nprocs; i++) {
		if (!proc_visited[i]) {
			proc_visited[i] = 1;
			proc_depth[i] = 0;
			proc_order[out_idx++] = i;
		}
	}
}

static void format_cmd_with_tree(struct six_proc *p, int depth, int use_full_args,
				 int use_forest, char *out, int maxlen)
{
	const char *base = use_full_args ? p->args : p->comm;
	int pos = 0, d;

	if (use_forest && depth > 0) {
		for (d = 0; d < depth - 1 && pos + 4 < maxlen; d++) {
			out[pos++] = ' ';
			out[pos++] = ' ';
		}
		if (pos + 4 < maxlen) {
			out[pos++] = '\\';
			out[pos++] = '_';
			out[pos++] = ' ';
		}
	}
	strncpy(out + pos, base, maxlen - 1 - pos);
	out[maxlen - 1] = '\0';
}

static void usage(void)
{
	printf("Usage: ps [options]\n\n");
	printf("Options:\n");
	printf("  -e, -A        Select all processes (including daemons & kernel threads)\n");
	printf("  -a            Select all processes on a terminal\n");
	printf("  -f            Full-format listing (UID, PID, PPID, C, STIME, TTY, TIME, CMD)\n");
	printf("  -l            Long format (F, S, UID, PID, PPID, PRI, NI, SZ, WCHAN, TTY, TIME, CMD)\n");
	printf("  -H, --forest  ASCII process tree hierarchy\n");
	printf("  -p <pid>      Select by PID\n");
	printf("  aux, u, x     BSD user-oriented format (%CPU, %MEM, VSZ, RSS, STAT, START, COMMAND)\n");
}

int main(int argc, char *argv[])
{
	int opt_all = 0;
	int opt_full = 0;
	int opt_long = 0;
	int opt_bsd_u = 0;
	int opt_forest = 0;
	int filter_pid = -1;
	int i, k;
	struct six_proc s;
	time_t now_epoch = time(NULL);

	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		if (strcmp(a, "--help") == 0 || strcmp(a, "-h") == 0) {
			usage();
			return 0;
		}
		if (strcmp(a, "--forest") == 0) {
			opt_forest = 1;
			continue;
		}
		if (strcmp(a, "-p") == 0 && i + 1 < argc) {
			filter_pid = atoi(argv[++i]);
			opt_all = 1;
			continue;
		}
		if (a[0] == '-')
			a++;
		while (*a) {
			switch (*a++) {
			case 'e':
			case 'A':
			case 'x':
				opt_all = 1;
				break;
			case 'a':
				opt_all = 1;
				break;
			case 'f':
				if (argv[i][0] != '-')
					opt_forest = 1;
				else
					opt_full = 1;
				break;
			case 'l':
				opt_long = 1;
				break;
			case 'u':
				opt_bsd_u = 1;
				opt_all = 1;
				break;
			case 'H':
				opt_forest = 1;
				break;
			case 'w':
				break;
			default:
				break;
			}
		}
	}

	memset(&s, 0, sizeof(s));
	s.index = 0;
	nprocs = 0;
	while (nprocs < MAX_PROCS && !getproc(&s)) {
		procs[nprocs] = s;
		proc_order[nprocs] = nprocs;
		proc_depth[nprocs] = 0;
		nprocs++;
	}

	if (opt_forest)
		sort_for_forest();

	if (opt_bsd_u) {
		printf("USER       PID %%CPU %%MEM    VSZ   RSS TTY      STAT START   TIME COMMAND\n");
	} else if (opt_long) {
		printf("F S   UID   PID  PPID  C PRI  NI ADDR   SZ WCHAN            TTY          TIME CMD\n");
	} else if (opt_full) {
		printf("UID        PID  PPID  C STIME TTY          TIME CMD\n");
	} else {
		printf("  PID TTY      STAT     TIME CMD\n");
	}

	for (k = 0; k < nprocs; k++) {
		struct six_proc *p = &procs[proc_order[k]];
		int depth = proc_depth[proc_order[k]];
		char tbuf[16], sbuf[16], statbuf[8], cmdbuf[128];
		unsigned int cpu_tenths = 0, mem_tenths = 0;
		unsigned long elapsed;

		if (filter_pid >= 0 && p->pid != filter_pid)
			continue;
		if (!opt_all && filter_pid < 0) {
			/* Default ps: show processes attached to a terminal */
			if (p->is_kthread || p->tty_nr == 0)
				continue;
		}

		format_cputime(p, tbuf, opt_bsd_u ? 1 : 0);
		format_stime(p, now_epoch, sbuf);
		format_stat_str(p, statbuf);
		format_cmd_with_tree(p, depth,
				     (opt_full || opt_bsd_u || opt_forest) ? 1 : 0,
				     opt_forest, cmdbuf, sizeof(cmdbuf));

		elapsed = (p->jiffies_now > p->start_time) ? (p->jiffies_now - p->start_time) : 1;
		cpu_tenths = (unsigned int)(((p->utime + p->stime) * 1000UL) / elapsed);
		if (cpu_tenths > 999)
			cpu_tenths = 999;
		/* 32768 KB total RAM */
		mem_tenths = (unsigned int)((p->rss_kb * 1000UL) / 32768UL);

		if (opt_bsd_u) {
			printf("%-8s %5d %2u.%u %2u.%u %6lu %5lu %-8s %-4s %-5s %6s %s\n",
			       uid_to_name(p->uid),
			       p->pid,
			       cpu_tenths / 10, cpu_tenths % 10,
			       mem_tenths / 10, mem_tenths % 10,
			       p->vsize_kb,
			       p->rss_kb,
			       p->tty_name,
			       statbuf,
			       sbuf,
			       tbuf,
			       cmdbuf);
		} else if (opt_long) {
			printf("%d %c %5d %5d %5d %2u %3ld %3ld    - %4lu %-16s %-8s %8s %s\n",
			       p->is_kthread ? 1 : 4,
			       state_char(p->state),
			       p->uid,
			       p->pid,
			       p->ppid,
			       cpu_tenths / 10,
			       p->priority,
			       p->nice,
			       p->vsize_kb / 4,
			       p->wchan,
			       p->tty_name,
			       tbuf,
			       cmdbuf);
		} else if (opt_full) {
			printf("%-8s %5d %5d %2u %-5s %-8s %8s %s\n",
			       uid_to_name(p->uid),
			       p->pid,
			       p->ppid,
			       cpu_tenths / 10,
			       sbuf,
			       p->tty_name,
			       tbuf,
			       cmdbuf);
		} else {
			printf("%5d %-8s %-4s %8s %s\n",
			       p->pid,
			       p->tty_name,
			       statbuf,
			       tbuf,
			       cmdbuf);
		}
	}

	return 0;
}
