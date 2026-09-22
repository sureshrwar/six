#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <syscall.h>
#include <sys/wait.h>

#define SIX_STRACE_ATTACH   1
#define SIX_STRACE_DETACH   2
#define SIX_STRACE_READ     3
#define SIX_STRACE_SELF     4

struct six_strace_event {
	int pid;
	int nr;
	long a1;
	long a2;
	long a3;
	long ret;
	char str1[64];
	char str2[64];
};

static const char *syscall_names[166] = {
	[0] = "setup", [1] = "_exit", [2] = "fork", [3] = "read",
	[4] = "write", [5] = "open", [6] = "close", [7] = "waitpid",
	[8] = "creat", [9] = "link", [10] = "unlink", [11] = "execve",
	[12] = "chdir", [13] = "time", [14] = "mknod", [15] = "chmod",
	[16] = "chown", [17] = "break", [18] = "oldstat", [19] = "lseek",
	[20] = "getpid", [21] = "mount", [22] = "umount", [23] = "setuid",
	[24] = "getuid", [25] = "stime", [26] = "ptrace", [27] = "alarm",
	[28] = "oldfstat", [29] = "pause", [30] = "utime", [31] = "stty",
	[32] = "gtty", [33] = "access", [34] = "nice", [35] = "ftime",
	[36] = "sync", [37] = "kill", [38] = "rename", [39] = "mkdir",
	[40] = "rmdir", [41] = "dup", [42] = "pipe", [43] = "times",
	[44] = "prof", [45] = "brk", [46] = "setgid", [47] = "getgid",
	[48] = "signal", [49] = "geteuid", [50] = "getegid", [51] = "acct",
	[54] = "ioctl", [55] = "fcntl", [57] = "setpgid", [60] = "umask",
	[61] = "chroot", [62] = "ustat", [63] = "dup2", [64] = "getppid",
	[65] = "getpgrp", [66] = "setsid", [67] = "sigaction",
	[68] = "sgetmask", [69] = "ssetmask", [70] = "setreuid",
	[71] = "setregid", [72] = "sigsuspend", [73] = "sigpending",
	[74] = "sethostname", [75] = "setrlimit", [76] = "getrlimit",
	[77] = "getrusage", [78] = "gettimeofday", [79] = "settimeofday",
	[80] = "getgroups", [81] = "setgroups", [82] = "select",
	[83] = "symlink", [84] = "oldlstat", [85] = "readlink",
	[86] = "uselib", [87] = "swapon", [88] = "reboot", [89] = "readdir",
	[90] = "mmap", [91] = "munmap", [92] = "truncate", [93] = "ftruncate",
	[94] = "fchmod", [95] = "fchown", [96] = "getpriority",
	[97] = "setpriority", [99] = "statfs", [100] = "fstatfs",
	[101] = "ioperm", [102] = "socketcall", [103] = "syslog",
	[104] = "setitimer", [105] = "getitimer", [106] = "stat",
	[107] = "lstat", [108] = "fstat", [109] = "olduname", [110] = "iopl",
	[111] = "vhangup", [112] = "idle", [113] = "vm86", [114] = "wait4",
	[115] = "swapoff", [116] = "sysinfo", [117] = "ipc", [118] = "fsync",
	[119] = "sigreturn", [120] = "clone", [121] = "setdomainname",
	[122] = "uname", [123] = "modify_ldt", [124] = "adjtimex",
	[125] = "mprotect", [126] = "sigprocmask", [132] = "getpgid",
	[133] = "fchdir", [134] = "bdflush", [136] = "personality",
	[138] = "setfsuid", [139] = "setfsgid", [140] = "_llseek",
	[141] = "getdents", [142] = "_newselect", [143] = "flock",
	[144] = "msync", [145] = "readv", [146] = "writev", [147] = "getsid",
	[148] = "fdatasync", [149] = "_sysctl", [150] = "mlock",
	[151] = "munlock", [152] = "mlockall", [153] = "munlockall",
	[154] = "sched_setparam", [155] = "sched_getparam",
	[156] = "sched_setscheduler", [157] = "sched_getscheduler",
	[158] = "sched_yield", [159] = "sched_get_priority_max",
	[160] = "sched_get_priority_min", [161] = "sched_rr_get_interval",
	[162] = "nanosleep", [163] = "mremap", [164] = "six_ps"
};

static const char *
errno_name(int err)
{
	switch (err) {
	case 1: return "EPERM";
	case 2: return "ENOENT";
	case 3: return "ESRCH";
	case 4: return "EINTR";
	case 5: return "EIO";
	case 6: return "ENXIO";
	case 7: return "E2BIG";
	case 8: return "ENOEXEC";
	case 9: return "EBADF";
	case 10: return "ECHILD";
	case 11: return "EAGAIN";
	case 12: return "ENOMEM";
	case 13: return "EACCES";
	case 14: return "EFAULT";
	case 15: return "ENOTBLK";
	case 16: return "EBUSY";
	case 17: return "EEXIST";
	case 18: return "EXDEV";
	case 19: return "ENODEV";
	case 20: return "ENOTDIR";
	case 21: return "EISDIR";
	case 22: return "EINVAL";
	case 23: return "ENFILE";
	case 24: return "EMFILE";
	case 25: return "ENOTTY";
	case 27: return "EFBIG";
	case 28: return "ENOSPC";
	case 29: return "ESPIPE";
	case 30: return "EROFS";
	case 32: return "EPIPE";
	case 38: return "ENOSYS";
	case 39: return "ENOTEMPTY";
	default: return "ERRNO";
	}
}

static void
fprint_escaped(FILE *out, const char *s, int maxlen, int total_len)
{
	int i;
	fputc('"', out);
	for (i = 0; i < maxlen && s[i] != '\0'; i++) {
		unsigned char c = (unsigned char)s[i];
		if (c == '\n')
			fputs("\\n", out);
		else if (c == '\r')
			fputs("\\r", out);
		else if (c == '\t')
			fputs("\\t", out);
		else if (c == '\\' || c == '"') {
			fputc('\\', out);
			fputc(c, out);
		} else if (c >= 32 && c < 127)
			fputc(c, out);
		else
			fprintf(out, "\\x%02x", c);
	}
	fputc('"', out);
	if (total_len > maxlen)
		fputs("...", out);
}

static void
format_open_flags(FILE *out, long flags)
{
	int acc = flags & 3;
	if (acc == 0)
		fputs("O_RDONLY", out);
	else if (acc == 1)
		fputs("O_WRONLY", out);
	else
		fputs("O_RDWR", out);
	if (flags & 0100)
		fputs("|O_CREAT", out);
	if (flags & 0200)
		fputs("|O_EXCL", out);
	if (flags & 01000)
		fputs("|O_TRUNC", out);
	if (flags & 02000)
		fputs("|O_APPEND", out);
	if (flags & 04000)
		fputs("|O_NONBLOCK", out);
}

static int
match_filter(const char *filter, const char *scname)
{
	const char *p;
	int nlen;
	if (!filter || !*filter)
		return 1;
	if (strncmp(filter, "trace=", 6) == 0)
		filter += 6;
	nlen = strlen(scname);
	p = filter;
	while (*p) {
		const char *comma = strchr(p, ',');
		int len = comma ? (int)(comma - p) : (int)strlen(p);
		if (len == nlen && strncmp(p, scname, len) == 0)
			return 1;
		if (!comma)
			break;
		p = comma + 1;
	}
	return 0;
}

static void
format_event(FILE *out, const struct six_strace_event *ev, int show_pid)
{
	const char *name = (ev->nr >= 0 && ev->nr < 166 && syscall_names[ev->nr])
	                   ? syscall_names[ev->nr] : "syscall";

	if (show_pid)
		fprintf(out, "[pid %2d] ", ev->pid);

	switch (ev->nr) {
	case 1: /* _exit */
		fprintf(out, "_exit(%ld) = ?\n", ev->a1);
		return;
	case 3: /* read */
		fprintf(out, "read(%ld, ", ev->a1);
		if (ev->ret > 0)
			fprint_escaped(out, ev->str1, 32, (int)ev->ret);
		else
			fprintf(out, "0x%lx", (unsigned long)ev->a2);
		fprintf(out, ", %ld)", ev->a3);
		break;
	case 4: /* write */
		fprintf(out, "write(%ld, ", ev->a1);
		fprint_escaped(out, ev->str1, 32, (int)ev->a3);
		fprintf(out, ", %ld)", ev->a3);
		break;
	case 5: /* open */
		fprintf(out, "open(");
		fprint_escaped(out, ev->str1, 60, 0);
		fputs(", ", out);
		format_open_flags(out, ev->a2);
		if (ev->a2 & 0100)
			fprintf(out, ", 0%lo", (unsigned long)ev->a3 & 07777);
		fputc(')', out);
		break;
	case 6: /* close */
	case 41: /* dup */
	case 118: /* fsync */
	case 133: /* fchdir */
		fprintf(out, "%s(%ld)", name, ev->a1);
		break;
	case 11: /* execve */
		fprintf(out, "execve(");
		fprint_escaped(out, ev->str1, 60, 0);
		if (ev->str2[0]) {
			fprintf(out, ", [");
			fprint_escaped(out, ev->str1, 32, 0);
			fputs(", ", out);
			fprint_escaped(out, ev->str2, 32, 0);
			fprintf(out, "], 0x%lx)", (unsigned long)ev->a3);
		} else {
			fprintf(out, ", [");
			fprint_escaped(out, ev->str1, 32, 0);
			fprintf(out, "], 0x%lx)", (unsigned long)ev->a3);
		}
		break;
	case 8: /* creat */
	case 10: /* unlink */
	case 12: /* chdir */
	case 22: /* umount */
	case 40: /* rmdir */
	case 61: /* chroot */
		fprintf(out, "%s(", name);
		fprint_escaped(out, ev->str1, 60, 0);
		fputc(')', out);
		break;
	case 15: /* chmod */
	case 39: /* mkdir */
		fprintf(out, "%s(", name);
		fprint_escaped(out, ev->str1, 60, 0);
		fprintf(out, ", 0%lo)", (unsigned long)ev->a2 & 07777);
		break;
	case 33: /* access */
		fprintf(out, "access(");
		fprint_escaped(out, ev->str1, 60, 0);
		fprintf(out, ", %s)", ev->a2 == 0 ? "F_OK" : "R_OK|W_OK|X_OK");
		break;
	case 9: /* link */
	case 38: /* rename */
	case 83: /* symlink */
		fprintf(out, "%s(", name);
		fprint_escaped(out, ev->str1, 48, 0);
		fputs(", ", out);
		fprint_escaped(out, ev->str2, 48, 0);
		fputc(')', out);
		break;
	case 85: /* readlink */
		fprintf(out, "readlink(");
		fprint_escaped(out, ev->str1, 48, 0);
		fputs(", ", out);
		if (ev->ret > 0)
			fprint_escaped(out, ev->str2, 48, (int)ev->ret);
		else
			fprintf(out, "0x%lx", (unsigned long)ev->a2);
		fprintf(out, ", %ld)", ev->a3);
		break;
	case 106: /* stat */
	case 107: /* lstat */
	case 99:  /* statfs */
		fprintf(out, "%s(", name);
		fprint_escaped(out, ev->str1, 60, 0);
		fprintf(out, ", 0x%lx)", (unsigned long)ev->a2);
		break;
	case 19: /* lseek */
		fprintf(out, "lseek(%ld, %ld, %s)", ev->a1, ev->a2,
		        ev->a3 == 0 ? "SEEK_SET" : (ev->a3 == 1 ? "SEEK_CUR" : "SEEK_END"));
		break;
	case 45: /* brk */
		fprintf(out, "brk(0x%lx) = 0x%lx\n",
		        (unsigned long)ev->a1, (unsigned long)ev->ret);
		return;
	case 90: /* mmap */
		fprintf(out, "mmap(0x%lx, %lu, %ld, ...) = 0x%lx\n",
		        (unsigned long)ev->a1, (unsigned long)ev->a2,
		        ev->a3, (unsigned long)ev->ret);
		return;
	case 20: /* getpid */
	case 24: /* getuid */
	case 47: /* getgid */
	case 49: /* geteuid */
	case 50: /* getegid */
	case 64: /* getppid */
	case 65: /* getpgrp */
	case 66: /* setsid */
	case 36: /* sync */
	case 2:  /* fork */
		fprintf(out, "%s()", name);
		break;
	default:
		fprintf(out, "%s(0x%lx, 0x%lx, 0x%lx)",
		        name, (unsigned long)ev->a1,
		        (unsigned long)ev->a2, (unsigned long)ev->a3);
		break;
	}

	if (ev->ret < 0 && ev->ret >= -255) {
		int err = (int)(-ev->ret);
		fprintf(out, " = -1 %s (%s)\n", errno_name(err), strerror(err));
	} else {
		fprintf(out, " = %ld\n", ev->ret);
	}
}

static int
resolve_path(const char *cmd, char *out, int outlen)
{
	const char *path, *p;
	if (strchr(cmd, '/')) {
		strncpy(out, cmd, outlen - 1);
		out[outlen - 1] = '\0';
		return access(out, 1) == 0 ? 0 : -1;
	}
	path = getenv("PATH");
	if (!path || !*path)
		path = "/bin:/usr/bin";
	p = path;
	while (*p) {
		const char *colon = strchr(p, ':');
		int dlen = colon ? (int)(colon - p) : (int)strlen(p);
		if (dlen + 1 + (int)strlen(cmd) + 1 < outlen) {
			memcpy(out, p, dlen);
			out[dlen] = '/';
			strcpy(out + dlen + 1, cmd);
			if (access(out, 1) == 0)
				return 0;
		}
		if (!colon)
			break;
		p = colon + 1;
	}
	return -1;
}

static volatile int got_sigint = 0;
static void
on_sigint(int sig)
{
	(void)sig;
	got_sigint = 1;
}

static struct six_strace_event evbuf[32];
static unsigned long sc_calls[166];
static unsigned long sc_errs[166];

int
main(int argc, char **argv)
{
	int follow_fork = 0, summary_only = 0, attach_pid = 0, max_events = 0;
	const char *filter = NULL, *outfile = NULL;
	FILE *out = stderr;
	int i, child_pid = -1, total_events = 0;

	for (i = 1; i < argc && argv[i][0] == '-'; i++) {
		if (strcmp(argv[i], "-f") == 0) {
			follow_fork = 1;
		} else if (strcmp(argv[i], "-c") == 0) {
			summary_only = 1;
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			attach_pid = atoi(argv[++i]);
		} else if (strcmp(argv[i], "-e") == 0 && i + 1 < argc) {
			filter = argv[++i];
		} else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
			outfile = argv[++i];
		} else if (strcmp(argv[i], "-n") == 0 && i + 1 < argc) {
			max_events = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--") == 0) {
			i++;
			break;
		} else {
			fprintf(stderr, "Usage: strace [-f] [-c] [-e expr] [-o file] [-n count] [-p pid | cmd [args...]]\n");
			return 1;
		}
	}

	if (attach_pid <= 0 && i >= argc) {
		fprintf(stderr, "Usage: strace [-f] [-c] [-e expr] [-o file] [-n count] [-p pid | cmd [args...]]\n");
		return 1;
	}

	if (outfile) {
		out = fopen(outfile, "w");
		if (!out) {
			perror(outfile);
			return 1;
		}
	}

	signal(SIGINT, on_sigint);

	if (attach_pid > 0) {
		if (syscall(__NR_ptrace, SIX_STRACE_ATTACH, attach_pid, follow_fork) < 0) {
			perror("strace: attach");
			return 1;
		}
		fprintf(out, "strace: Process %d attached\n", attach_pid);
	} else {
		char fullpath[256];
		if (resolve_path(argv[i], fullpath, sizeof(fullpath)) < 0) {
			fprintf(stderr, "strace: Can't stat '%s': No such file or directory\n", argv[i]);
			return 1;
		}
		child_pid = fork();
		if (child_pid < 0) {
			perror("strace: fork");
			return 1;
		}
		if (child_pid == 0) {
			extern char **environ;
			syscall(__NR_ptrace, SIX_STRACE_SELF, follow_fork, 0);
			execve(fullpath, &argv[i], environ);
			_exit(127);
		}
	}

	while (!got_sigint) {
		int n = syscall(__NR_ptrace, SIX_STRACE_READ, 32, (long)evbuf);
		int j;
		if (n == 0)
			break;
		if (n < 0) {
			if (n == -EINTR) {
				if (got_sigint)
					break;
				continue;
			}
			break;
		}
		for (j = 0; j < n; j++) {
			int nr = evbuf[j].nr;
			const char *name = (nr >= 0 && nr < 166 && syscall_names[nr])
			                   ? syscall_names[nr] : "syscall";
			if (!match_filter(filter, name))
				continue;
			if (nr >= 0 && nr < 166) {
				sc_calls[nr]++;
				if (evbuf[j].ret < 0 && evbuf[j].ret >= -255)
					sc_errs[nr]++;
			}
			if (!summary_only)
				format_event(out, &evbuf[j], follow_fork);
			total_events++;
			if (max_events > 0 && total_events >= max_events) {
				got_sigint = 1;
				break;
			}
		}
	}

	syscall(__NR_ptrace, SIX_STRACE_DETACH, 0, 0);

	if (attach_pid > 0) {
		fprintf(out, "strace: Process %d detached\n", attach_pid);
	} else if (child_pid > 0) {
		int status = 0;
		waitpid(child_pid, &status, 0);
		if (!summary_only) {
			if ((status & 0x7f) == 0)
				fprintf(out, "+++ exited with %d +++\n", (status >> 8) & 0xff);
			else
				fprintf(out, "+++ killed by signal %d +++\n", status & 0x7f);
		}
	}

	if (summary_only) {
		unsigned long total_c = 0, total_e = 0;
		int k;
		for (k = 0; k < 166; k++) {
			total_c += sc_calls[k];
			total_e += sc_errs[k];
		}
		fprintf(out, "%% calls     calls    errors syscall\n");
		fprintf(out, "------- --------- --------- ----------------\n");
		for (k = 0; k < 166; k++) {
			if (sc_calls[k] > 0) {
				unsigned long pct100 = total_c ? (sc_calls[k] * 10000UL) / total_c : 0;
				fprintf(out, "%4lu.%02lu %9lu %9lu %s\n",
				        pct100 / 100, pct100 % 100,
				        sc_calls[k], sc_errs[k],
				        syscall_names[k] ? syscall_names[k] : "syscall");
			}
		}
		fprintf(out, "------- --------- --------- ----------------\n");
		fprintf(out, " 100.00 %9lu %9lu total\n", total_c, total_e);
	}

	if (outfile && out != stderr)
		fclose(out);
	return 0;
}
