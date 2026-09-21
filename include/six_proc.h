
#ifndef SIX_PROC
#define SIX_PROC

struct six_proc {
	char comm[64];
	int pid;
	int ppid;
	int memsize;
	int index;
	int uid;
	int euid;
	int gid;
	int pgrp;
	int session;
	int state;
	int tty_nr;
	char tty_name[16];
	unsigned long utime;
	unsigned long stime;
	unsigned long start_time;
	unsigned long jiffies_now;
	long priority;
	long nice;
	unsigned long vsize_kb;
	unsigned long rss_kb;
	int is_kthread;
	unsigned long wchan_addr;
	char wchan[32];
	char args[128];
};

#endif

