/*
 * /bin/panic - Trigger a real Linux kernel panic in SIX
 *
 * Usage:
 *   panic [-p mask] [message ...]
 *
 * panic_print bitmask flags (default 0x3f = all 6 sections):
 *   0x01  PANIC_PRINT_TASK_INFO    Task table & stack traces of all threads
 *   0x02  PANIC_PRINT_MEM_INFO     Memory, buddy allocator, swap, & buffers
 *   0x04  PANIC_PRINT_TIMER_INFO   Active kernel timers & jiffies
 *   0x08  PANIC_PRINT_LOCK_INFO    Held locks (superblocks, inodes, flocks)
 *   0x10  PANIC_PRINT_FTRACE_INFO  Recent syscall trace ring buffer
 *   0x20  PANIC_PRINT_ALL_CPU_BT   Per-CPU state & stack backtrace
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int syscall(int num, long a1, long a2, long a3);

static unsigned long parse_mask(const char *s)
{
	unsigned long val = 0;
	if (!s)
		return 0x3fUL;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
		while (*s) {
			char c = *s++;
			val <<= 4;
			if (c >= '0' && c <= '9')
				val |= (unsigned long)(c - '0');
			else if (c >= 'a' && c <= 'f')
				val |= (unsigned long)(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F')
				val |= (unsigned long)(c - 'A' + 10);
		}
	} else {
		val = (unsigned long)atoi(s);
	}
	return val & 0x7fUL;
}

static void usage(void)
{
	printf("Usage: panic [-p mask] [message ...]\n\n");
	printf("Trigger a Linux kernel panic with configurable panic_print bitmask:\n");
	printf("  0x01  TASK_INFO    Task table & stack traces of all threads\n");
	printf("  0x02  MEM_INFO     Memory, page allocator, swap, & buffers\n");
	printf("  0x04  TIMER_INFO   Active kernel timer queues & jiffies\n");
	printf("  0x08  LOCK_INFO    Held superblock, inode, & file locks\n");
	printf("  0x10  FTRACE_INFO  Syscall trace ring buffer\n");
	printf("  0x20  ALL_CPU_BT   Per-CPU state & stack backtrace\n");
	printf("  0x3f  ALL (default)\n");
}

int main(int argc, char **argv)
{
	unsigned long flag = 0xdead0000UL;
	static char msg[256];
	int i = 1;

	if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
		usage();
		return 0;
	}

	if (argc > 2 && strcmp(argv[1], "-p") == 0) {
		flag = 0xdead0100UL | (parse_mask(argv[2]) & 0x7fUL);
		i = 3;
	}

	msg[0] = '\0';
	for (; i < argc; i++) {
		if (msg[0] != '\0' && strlen(msg) + 2 < sizeof(msg))
			strcat(msg, " ");
		if (strlen(msg) + strlen(argv[i]) + 1 < sizeof(msg))
			strcat(msg, argv[i]);
	}

	if (msg[0] == '\0')
		strcpy(msg, "SysRq : Trigger a crashdump (via /bin/panic)");

	sync();
	syscall(88, 0xfee1deadL, (long)msg, (long)flag);
	return 0;
}
