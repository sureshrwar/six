/*
 * /bin/sysctl - Configure kernel parameters at runtime in SIX via _sysctl(2)
 *
 * Supports:
 *   sysctl -a                             List all kernel & VM sysctl parameters
 *   sysctl kernel.panic_print             Read a sysctl parameter
 *   sysctl [-w] kernel.panic_print=0x15   Write decimal or hex value
 *   sysctl [-w] kernel.panic_print=-ftrace_info,+lock_info
 *                                         Toggle individual panic_print bits
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int syscall(int num, long a1, long a2, long a3);

#define __NR__sysctl 149

#define CTL_KERN 1
#define CTL_VM   2

#define KERN_OSTYPE      1
#define KERN_OSRELEASE   2
#define KERN_VERSION     4
#define KERN_NODENAME    7
#define KERN_DOMAINNAME  8
#define KERN_NRINODE     9
#define KERN_MAXINODE    10
#define KERN_NRFILE      11
#define KERN_MAXFILE     12
#define KERN_SECURELVL   14
#define KERN_PANIC       15
#define KERN_PANIC_PRINT 21
#define KERN_HUNG_TASK_TIMEOUT_SECS 22
#define KERN_HUNG_TASK_PANIC 23
#define KERN_HUNG_TASK_WARNINGS 24
#define KERN_HUNG_TASK_SYS_INFO 25
#define KERN_BLK_IO_TIMEOUT_MS  26
#define KERN_PANIC_SYS_INFO     27
#define KERN_HUNG_TASK_DETECT_COUNT 28
#define KERN_KERNEL_SYS_INFO    29

#define VM_FREEPG        3

struct __sysctl_args {
	int *name;
	int nlen;
	void *oldval;
	unsigned int *oldlenp;
	void *newval;
	unsigned int newlen;
	unsigned long __unused[4];
};

#define TYPE_STRING  1
#define TYPE_INT     2
#define TYPE_INT2    3
#define TYPE_INT3    4
#define TYPE_PANIC_P 5

struct sysctl_entry {
	const char *name;
	const char *short_name;
	int ctl_top;
	int ctl_id;
	int type;
	int writable;
};

static struct sysctl_entry entries[] = {
	{ "kernel.ostype",                 "ostype",                 CTL_KERN, KERN_OSTYPE,                 TYPE_STRING,  0 },
	{ "kernel.osrelease",              "osrelease",              CTL_KERN, KERN_OSRELEASE,              TYPE_STRING,  0 },
	{ "kernel.version",                "version",                CTL_KERN, KERN_VERSION,                TYPE_STRING,  0 },
	{ "kernel.hostname",               "hostname",               CTL_KERN, KERN_NODENAME,               TYPE_STRING,  1 },
	{ "kernel.domainname",             "domainname",             CTL_KERN, KERN_DOMAINNAME,             TYPE_STRING,  1 },
	{ "kernel.inode-nr",               "inode-nr",               CTL_KERN, KERN_NRINODE,                TYPE_INT2,    0 },
	{ "kernel.inode-max",              "inode-max",              CTL_KERN, KERN_MAXINODE,               TYPE_INT,     1 },
	{ "kernel.file-nr",                "file-nr",                CTL_KERN, KERN_NRFILE,                 TYPE_INT,     0 },
	{ "kernel.file-max",               "file-max",               CTL_KERN, KERN_MAXFILE,                TYPE_INT,     1 },
	{ "kernel.securelevel",            "securelevel",            CTL_KERN, KERN_SECURELVL,              TYPE_INT,     1 },
	{ "kernel.panic",                  "panic",                  CTL_KERN, KERN_PANIC,                  TYPE_INT,     1 },
	{ "kernel.panic_print",            "panic_print",            CTL_KERN, KERN_PANIC_PRINT,            TYPE_PANIC_P, 1 },
	{ "kernel.panic_sys_info",         "panic_sys_info",         CTL_KERN, KERN_PANIC_SYS_INFO,         TYPE_PANIC_P, 1 },
	{ "kernel.kernel_sys_info",        "kernel_sys_info",        CTL_KERN, KERN_KERNEL_SYS_INFO,        TYPE_PANIC_P, 1 },
	{ "kernel.hung_task_timeout_secs", "hung_task_timeout_secs", CTL_KERN, KERN_HUNG_TASK_TIMEOUT_SECS, TYPE_INT,     1 },
	{ "kernel.hung_task_panic",        "hung_task_panic",        CTL_KERN, KERN_HUNG_TASK_PANIC,        TYPE_INT,     1 },
	{ "kernel.hung_task_warnings",     "hung_task_warnings",     CTL_KERN, KERN_HUNG_TASK_WARNINGS,     TYPE_INT,     1 },
	{ "kernel.hung_task_sys_info",     "hung_task_sys_info",     CTL_KERN, KERN_HUNG_TASK_SYS_INFO,     TYPE_PANIC_P, 1 },
	{ "kernel.hung_task_detect_count", "hung_task_detect_count", CTL_KERN, KERN_HUNG_TASK_DETECT_COUNT, TYPE_INT,     0 },
	{ "kernel.blk_io_timeout_ms",      "blk_io_timeout_ms",      CTL_KERN, KERN_BLK_IO_TIMEOUT_MS,      TYPE_INT,     1 },
	{ "vm.freepages",                  "freepages",              CTL_VM,   VM_FREEPG,                   TYPE_INT3,    1 },
	{ NULL, NULL, 0, 0, 0, 0 }
};

struct flag_bit {
	const char *name;
	const char *alias;
	unsigned long bit;
};

static struct flag_bit panic_flags[] = {
	{ "tasks",         "task_info",   0x01UL },
	{ "mem",           "mem_info",    0x02UL },
	{ "timers",        "timer_info",  0x04UL },
	{ "locks",         "lock_info",   0x08UL },
	{ "ftrace",        "ftrace_info", 0x10UL },
	{ "all_bt",        "all_cpu_bt",  0x20UL },
	{ "blocked_tasks", "blocked",     0x40UL },
	{ NULL, NULL, 0 }
};

static int do_kern_sysctl(int top, int id, void *oldv, unsigned int *oldl,
			  void *newv, unsigned int newl)
{
	int mib[2];
	struct __sysctl_args args;

	mib[0] = top;
	mib[1] = id;
	memset(&args, 0, sizeof(args));
	args.name = mib;
	args.nlen = 2;
	args.oldval = oldv;
	args.oldlenp = oldl;
	args.newval = newv;
	args.newlen = newl;
	return syscall(__NR__sysctl, (long)&args, 0, 0);
}

static void print_panic_flags(unsigned long val)
{
	int i, first = 1;
	printf(" (0x%02lx: ", val);
	if ((val & 0x7fUL) == 0) {
		printf("none)");
		return;
	}
	for (i = 0; panic_flags[i].name; i++) {
		if (val & panic_flags[i].bit) {
			if (!first)
				printf(",");
			printf("%s", panic_flags[i].name);
			first = 0;
		}
	}
	printf(")");
}

static unsigned long parse_num(const char *s, int *ok)
{
	unsigned long val = 0;
	*ok = 0;
	if (!s || !*s)
		return 0;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
		if (!*s)
			return 0;
		while (*s) {
			char c = *s++;
			val <<= 4;
			if (c >= '0' && c <= '9')
				val |= (unsigned long)(c - '0');
			else if (c >= 'a' && c <= 'f')
				val |= (unsigned long)(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				val |= (unsigned long)(c - 'A' + 10);
			else
				return 0;
		}
		*ok = 1;
		return val;
	}
	if (s[0] >= '0' && s[0] <= '9') {
		while (*s) {
			char c = *s++;
			if (c < '0' || c > '9')
				return 0;
			val = val * 10 + (unsigned long)(c - '0');
		}
		*ok = 1;
		return val;
	}
	return 0;
}

static unsigned long parse_panic_print_expr(const char *expr, unsigned long cur)
{
	int ok = 0, i;
	unsigned long num = parse_num(expr, &ok);
	char buf[128], *tok, *next;
	int relative = 0;
	unsigned long result;

	if (ok)
		return num & 0x7fUL;
	if (strcmp(expr, "all") == 0)
		return 0x7fUL;
	if (strcmp(expr, "none") == 0)
		return 0UL;

	if (expr[0] == '+' || expr[0] == '-' || expr[0] == '^')
		relative = 1;

	result = relative ? cur : 0UL;
	strncpy(buf, expr, sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = '\0';

	tok = buf;
	while (tok && *tok) {
		char op = '+';
		unsigned long bit = 0;

		next = strchr(tok, ',');
		if (next)
			*next++ = '\0';

		if (*tok == '+' || *tok == '-' || *tok == '^')
			op = *tok++;

		for (i = 0; panic_flags[i].name; i++) {
			if (strcmp(tok, panic_flags[i].name) == 0 ||
			    strcmp(tok, panic_flags[i].alias) == 0) {
				bit = panic_flags[i].bit;
				break;
			}
		}
		if (!bit) {
			bit = parse_num(tok, &ok);
		}
		if (op == '+')
			result |= bit;
		else if (op == '-')
			result &= ~bit;
		else if (op == '^')
			result ^= bit;

		tok = next;
	}
	return result & 0x7fUL;
}

static struct sysctl_entry *find_entry(const char *key)
{
	int i;
	for (i = 0; entries[i].name; i++) {
		if (strcmp(key, entries[i].name) == 0 ||
		    strcmp(key, entries[i].short_name) == 0)
			return &entries[i];
	}
	return NULL;
}

static int show_entry(struct sysctl_entry *e)
{
	if (e->type == TYPE_STRING) {
		char sbuf[128];
		unsigned int slen = sizeof(sbuf) - 1;
		memset(sbuf, 0, sizeof(sbuf));
		if (do_kern_sysctl(e->ctl_top, e->ctl_id, sbuf, &slen, NULL, 0) < 0)
			return -1;
		sbuf[slen] = '\0';
		printf("%s = %s\n", e->name, sbuf);
	} else if (e->type == TYPE_INT) {
		int val = 0;
		unsigned int vlen = sizeof(val);
		if (do_kern_sysctl(e->ctl_top, e->ctl_id, &val, &vlen, NULL, 0) < 0)
			return -1;
		printf("%s = %d\n", e->name, val);
	} else if (e->type == TYPE_INT2) {
		int val[2] = {0, 0};
		unsigned int vlen = sizeof(val);
		if (do_kern_sysctl(e->ctl_top, e->ctl_id, val, &vlen, NULL, 0) < 0)
			return -1;
		printf("%s = %d\t%d\n", e->name, val[0], val[1]);
	} else if (e->type == TYPE_INT3) {
		int val[3] = {0, 0, 0};
		unsigned int vlen = sizeof(val);
		if (do_kern_sysctl(e->ctl_top, e->ctl_id, val, &vlen, NULL, 0) < 0)
			return -1;
		printf("%s = %d\t%d\t%d\n", e->name, val[0], val[1], val[2]);
	} else if (e->type == TYPE_PANIC_P) {
		unsigned long val = 0;
		unsigned int vlen = sizeof(val);
		if (do_kern_sysctl(e->ctl_top, e->ctl_id, &val, &vlen, NULL, 0) < 0)
			return -1;
		printf("%s = %lu", e->name, val);
		print_panic_flags(val);
		printf("\n");
	}
	return 0;
}

static int write_entry(const char *arg)
{
	char key[64];
	const char *eq = strchr(arg, '=');
	const char *valstr;
	struct sysctl_entry *e;
	unsigned int len;

	if (!eq)
		return -1;
	len = (unsigned int)(eq - arg);
	if (len >= sizeof(key))
		len = sizeof(key) - 1;
	memcpy(key, arg, len);
	key[len] = '\0';
	valstr = eq + 1;

	e = find_entry(key);
	if (!e) {
		printf("sysctl: unknown key '%s'\n", key);
		return -1;
	}
	if (!e->writable) {
		printf("sysctl: key '%s' is read-only\n", e->name);
		return -1;
	}

	if (e->type == TYPE_STRING) {
		if (do_kern_sysctl(e->ctl_top, e->ctl_id, NULL, NULL,
				   (void *)valstr, strlen(valstr)) < 0) {
			printf("sysctl: permission denied writing '%s'\n", e->name);
			return -1;
		}
	} else if (e->type == TYPE_INT) {
		int ok = 0;
		int nval = (int)parse_num(valstr, &ok);
		if (!ok)
			nval = atoi(valstr);
		if (do_kern_sysctl(e->ctl_top, e->ctl_id, NULL, NULL,
				   &nval, sizeof(nval)) < 0) {
			printf("sysctl: failed writing '%s'\n", e->name);
			return -1;
		}
	} else if (e->type == TYPE_PANIC_P) {
		unsigned long cur = 0, nval;
		unsigned int clen = sizeof(cur);
		do_kern_sysctl(e->ctl_top, e->ctl_id, &cur, &clen, NULL, 0);
		nval = parse_panic_print_expr(valstr, cur);
		if (do_kern_sysctl(e->ctl_top, e->ctl_id, NULL, NULL,
				   &nval, sizeof(nval)) < 0) {
			printf("sysctl: failed writing '%s'\n", e->name);
			return -1;
		}
	}
	return show_entry(e);
}

static void usage(void)
{
	printf("Usage:\n");
	printf("  sysctl -a                              List all sysctl parameters\n");
	printf("  sysctl <key>                           Read a parameter\n");
	printf("  sysctl [-w] <key>=<value>              Write a parameter\n\n");
	printf("Examples for kernel.panic_print:\n");
	printf("  sysctl kernel.panic_print\n");
	printf("  sysctl -w kernel.panic_print=0x3f\n");
	printf("  sysctl -w kernel.panic_print=task_info,mem_info\n");
	printf("  sysctl -w kernel.panic_print=-ftrace_info,-timer_info\n");
}

int main(int argc, char **argv)
{
	int i;

	if (argc == 1 || (argc == 2 && strcmp(argv[1], "-a") == 0)) {
		for (i = 0; entries[i].name; i++)
			show_entry(&entries[i]);
		return 0;
	}

	if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
		usage();
		return 0;
	}

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-w") == 0)
			continue;
		if (strchr(argv[i], '=')) {
			write_entry(argv[i]);
		} else {
			struct sysctl_entry *e = find_entry(argv[i]);
			if (!e)
				printf("sysctl: cannot stat /proc/sys/%s: No such file or directory\n", argv[i]);
			else
				show_entry(e);
		}
	}
	return 0;
}
