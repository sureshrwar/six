#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <time.h>
#include <signal.h>
#include <termios.h>
#include <syscall.h>
#include <sys/time.h>
#include <linux/kernel.h>

extern int sysinfo(struct sysinfo *info);

#define MAX_PROCS 128
#define HZ_VAL    10

struct proc_sample {
	int pid;
	int ppid;
	char state;
	char comm[24];
	long priority;
	long nice;
	unsigned long utime;
	unsigned long stime;
	unsigned long vsize_kb;
	unsigned long rss_kb;
	unsigned long delta_ticks;
	unsigned long cpu_pct10; /* tenths of % */
	unsigned long mem_pct10; /* tenths of % */
};

static struct proc_sample prev_procs[MAX_PROCS];
static int prev_count = 0;
static struct proc_sample cur_procs[MAX_PROCS];
static int cur_count = 0;

static int sort_mode = 0; /* 0 = %CPU, 1 = %MEM, 2 = PID */

static unsigned long
find_prev_ticks(int pid)
{
	int i;
	for (i = 0; i < prev_count; i++) {
		if (prev_procs[i].pid == pid)
			return prev_procs[i].utime + prev_procs[i].stime;
	}
	return 0;
}

static int
read_proc_stat(int pid, struct proc_sample *ps)
{
	char path[64], buf[512];
	int fd, n, idx;
	char *lp, *rp, *p;
	long vals[25];

	snprintf(path, sizeof(path), "/proc/%d/stat", pid);
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';

	lp = strchr(buf, '(');
	rp = strrchr(buf, ')');
	if (!lp || !rp || rp <= lp)
		return -1;

	ps->pid = pid;
	{
		int clen = (int)(rp - (lp + 1));
		if (clen >= (int)sizeof(ps->comm))
			clen = (int)sizeof(ps->comm) - 1;
		memcpy(ps->comm, lp + 1, clen);
		ps->comm[clen] = '\0';
	}

	p = rp + 2;
	ps->state = *p ? *p : 'S';
	p++;
	while (*p == ' ')
		p++;

	for (idx = 0; idx < 24; idx++) {
		vals[idx] = strtol(p, &p, 10);
		while (*p == ' ')
			p++;
	}

	ps->ppid = (int)vals[0];
	ps->utime = (unsigned long)vals[10];
	ps->stime = (unsigned long)vals[11];
	ps->priority = vals[14];
	ps->nice = vals[15];
	ps->vsize_kb = ((unsigned long)vals[19]) >> 10;
	ps->rss_kb = ((unsigned long)vals[20]) * 4UL;
	return 0;
}

static unsigned long
collect_procs(unsigned long total_ram_kb, unsigned long elapsed_ticks)
{
	DIR *d = opendir("/proc");
	struct dirent *de;
	unsigned long sum_delta = 0;
	int i;

	cur_count = 0;
	if (!d)
		return 0;

	while ((de = readdir(d)) != NULL && cur_count < MAX_PROCS) {
		int pid;
		if (!isdigit((unsigned char)de->d_name[0]))
			continue;
		pid = atoi(de->d_name);
		if (pid <= 0)
			continue;
		if (read_proc_stat(pid, &cur_procs[cur_count]) == 0) {
			unsigned long now_t = cur_procs[cur_count].utime + cur_procs[cur_count].stime;
			unsigned long old_t = find_prev_ticks(pid);
			unsigned long dt = (now_t >= old_t) ? (now_t - old_t) : now_t;
			cur_procs[cur_count].delta_ticks = dt;
			sum_delta += dt;
			cur_count++;
		}
	}
	closedir(d);

	if (elapsed_ticks == 0)
		elapsed_ticks = sum_delta > 0 ? sum_delta : HZ_VAL;

	for (i = 0; i < cur_count; i++) {
		unsigned long cp = (cur_procs[i].delta_ticks * 1000UL) / elapsed_ticks;
		if (cp > 1000UL)
			cp = 1000UL;
		cur_procs[i].cpu_pct10 = cp;
		cur_procs[i].mem_pct10 = total_ram_kb > 0
		                         ? (cur_procs[i].rss_kb * 1000UL) / total_ram_kb : 0;
	}

	/* Sort processes */
	for (i = 0; i < cur_count - 1; i++) {
		int j;
		for (j = i + 1; j < cur_count; j++) {
			int swap = 0;
			if (sort_mode == 1) {
				if (cur_procs[j].rss_kb > cur_procs[i].rss_kb)
					swap = 1;
			} else if (sort_mode == 2) {
				if (cur_procs[j].pid < cur_procs[i].pid)
					swap = 1;
			} else {
				unsigned long ti = cur_procs[i].utime + cur_procs[i].stime;
				unsigned long tj = cur_procs[j].utime + cur_procs[j].stime;
				if (cur_procs[j].cpu_pct10 > cur_procs[i].cpu_pct10 ||
				    (cur_procs[j].cpu_pct10 == cur_procs[i].cpu_pct10 && tj > ti) ||
				    (cur_procs[j].cpu_pct10 == cur_procs[i].cpu_pct10 && tj == ti &&
				     cur_procs[j].pid > cur_procs[i].pid))
					swap = 1;
			}
			if (swap) {
				struct proc_sample tmp = cur_procs[i];
				cur_procs[i] = cur_procs[j];
				cur_procs[j] = tmp;
			}
		}
	}

	memcpy(prev_procs, cur_procs, cur_count * sizeof(struct proc_sample));
	prev_count = cur_count;
	return sum_delta;
}

static void
render_frame(int batch_mode, unsigned long delay_ticks)
{
	struct sysinfo si;
	time_t now = time(NULL);
	struct tm *tm_now = localtime(&now);
	unsigned long total_kb, free_kb, used_kb, buff_kb;
	unsigned long sum_delta, busy_pct10, idle_pct10;
	int n_run = 0, n_slp = 0, n_stop = 0, n_zomb = 0;
	unsigned long l1, l5, l15;
	int i, max_rows = batch_mode ? cur_count : 18;

	if (sysinfo(&si) < 0)
		memset(&si, 0, sizeof(si));

	total_kb = si.totalram >> 10;
	free_kb = si.freeram >> 10;
	used_kb = (total_kb >= free_kb) ? (total_kb - free_kb) : 0;
	buff_kb = (si.bufferram + si.sharedram) >> 10;

	sum_delta = collect_procs(total_kb, delay_ticks);
	if (batch_mode)
		max_rows = cur_count;

	for (i = 0; i < cur_count; i++) {
		switch (cur_procs[i].state) {
		case 'R': n_run++; break;
		case 'T': n_stop++; break;
		case 'Z': n_zomb++; break;
		default:  n_slp++; break;
		}
	}

	busy_pct10 = delay_ticks ? (sum_delta * 1000UL) / delay_ticks : 0;
	if (busy_pct10 > 1000UL)
		busy_pct10 = 1000UL;
	idle_pct10 = 1000UL - busy_pct10;

	l1 = (si.loads[0] * 100UL) >> 16;
	l5 = (si.loads[1] * 100UL) >> 16;
	l15 = (si.loads[2] * 100UL) >> 16;

	if (!batch_mode)
		printf("\033[H\033[2J");

	printf("top - %02d:%02d:%02d up %ld min,  1 user,  load average: %lu.%02lu, %lu.%02lu, %lu.%02lu\n",
	       tm_now ? tm_now->tm_hour : 0,
	       tm_now ? tm_now->tm_min : 0,
	       tm_now ? tm_now->tm_sec : 0,
	       si.uptime / 60,
	       l1 / 100, l1 % 100,
	       l5 / 100, l5 % 100,
	       l15 / 100, l15 % 100);
	printf("Tasks: %3d total, %3d running, %3d sleeping, %3d stopped, %3d zombie\n",
	       cur_count, n_run, n_slp, n_stop, n_zomb);
	printf("%%Cpu(s): %3lu.%lu sys+usr, %3lu.%lu idle\n",
	       busy_pct10 / 10, busy_pct10 % 10,
	       idle_pct10 / 10, idle_pct10 % 10);
	printf("KiB Mem : %8lu total, %8lu free, %8lu used, %8lu buff/shrd\n\n",
	       total_kb, free_kb, used_kb, buff_kb);

	if (!batch_mode)
		printf("\033[7m");
	printf("  PID USER      PR  NI    VIRT    RES S  %%CPU  %%MEM     TIME+ COMMAND         ");
	if (!batch_mode)
		printf("\033[0m");
	printf("\n");

	for (i = 0; i < cur_count && i < max_rows; i++) {
		unsigned long tot_ticks = cur_procs[i].utime + cur_procs[i].stime;
		unsigned long mins = (tot_ticks / HZ_VAL) / 60;
		unsigned long secs = (tot_ticks / HZ_VAL) % 60;
		unsigned long csec = (tot_ticks % HZ_VAL) * (100 / HZ_VAL);

		printf("%5d %-8s %3ld %3ld %7lu %6lu %c %3lu.%lu %3lu.%lu %3lu:%02lu.%02lu %-15s\n",
		       cur_procs[i].pid,
		       "root",
		       cur_procs[i].priority,
		       cur_procs[i].nice,
		       cur_procs[i].vsize_kb,
		       cur_procs[i].rss_kb,
		       cur_procs[i].state,
		       cur_procs[i].cpu_pct10 / 10, cur_procs[i].cpu_pct10 % 10,
		       cur_procs[i].mem_pct10 / 10, cur_procs[i].mem_pct10 % 10,
		       mins, secs, csec,
		       cur_procs[i].comm);
	}
	fflush(stdout);
}

static struct termios orig_tio;
static int tio_saved = 0;

static void
restore_tty(void)
{
	if (tio_saved) {
		tcsetattr(0, TCSANOW, &orig_tio);
		tio_saved = 0;
	}
}

static void
on_sig(int sig)
{
	(void)sig;
	restore_tty();
	printf("\n");
	_exit(0);
}

int
main(int argc, char **argv)
{
	int batch_mode = 0, max_iter = 0, delay_sec = 2;
	int i, iter = 0;
	struct sysinfo si;

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-b") == 0) {
			batch_mode = 1;
		} else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
			max_iter = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
			delay_sec = atoi(argv[++i]);
			if (delay_sec < 1)
				delay_sec = 1;
		} else if (strcmp(argv[i], "-m") == 0) {
			sort_mode = 1;
		}
	}

	if (!isatty(0) || max_iter == 1)
		batch_mode = 1;

	/* Prime baseline CPU tick counts */
	if (sysinfo(&si) == 0)
		collect_procs(si.totalram >> 10, HZ_VAL);

	if (max_iter == 1) {
		struct timespec ts;
		ts.tv_sec = 0;
		ts.tv_nsec = 200000000L; /* 200ms baseline sample window */
		nanosleep(&ts, NULL);
		render_frame(1, 2);
		return 0;
	}

	if (!batch_mode) {
		struct termios raw;
		if (tcgetattr(0, &orig_tio) == 0) {
			tio_saved = 1;
			raw = orig_tio;
			raw.c_lflag &= ~(ICANON | ECHO);
			raw.c_cc[VMIN] = 0;
			raw.c_cc[VTIME] = 0;
			tcsetattr(0, TCSANOW, &raw);
		}
		signal(SIGINT, on_sig);
		signal(SIGTERM, on_sig);
	}

	for (;;) {
		render_frame(batch_mode, (unsigned long)(delay_sec * HZ_VAL));
		iter++;
		if (max_iter > 0 && iter >= max_iter)
			break;

		if (!batch_mode) {
			fd_set rfds;
			struct timeval tv;
			FD_ZERO(&rfds);
			FD_SET(0, &rfds);
			tv.tv_sec = delay_sec;
			tv.tv_usec = 0;
			if (select(1, &rfds, NULL, NULL, &tv) > 0) {
				char ch = 0;
				if (read(0, &ch, 1) == 1) {
					if (ch == 'q' || ch == 'Q')
						break;
					if (ch == 'm' || ch == 'M')
						sort_mode = 1;
					else if (ch == 'p' || ch == 'P')
						sort_mode = 0;
					else if (ch == 'n' || ch == 'N')
						sort_mode = 2;
				}
			}
		} else {
			sleep(delay_sec);
		}
	}

	restore_tty();
	return 0;
}
