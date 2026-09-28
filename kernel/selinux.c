/*
 * linux/kernel/selinux.c
 *
 * Full-strictness Android Desktop style SELinux (SEAndroid) Mandatory Access
 * Control subsystem for SIX:
 *   - 3-tier policy architecture (system/public, system/private, vendor)
 *   - Default-deny Type Enforcement (TE) across process, file, dir, chr_file,
 *     blk_file, lnk_file, fifo_file, sock_file, fd, capability, filesystem,
 *     binder, service_manager, security, and system object classes
 *   - Extended permission filtering (allowxperm / neverallowxperm) on ioctl
 *     commands (e.g., SG_IO 0x2285 on UFS block devices)
 *   - Capability enforcement orthogonal to DAC uid=0 (e.g., sys_rawio,
 *     setuid, setgid, sys_admin, dac_override)
 *   - Automatic domain transitions on execve (e.g., init -> vold -> ntfs_3g,
 *     init -> fwupd, init -> storaged, init -> servicemanager)
 *   - Compile/load-time neverallow, neverallowxperm, and 3-tier visibility
 *     verification via /sys/fs/selinux/load
 *   - /sys/fs/selinux/* character device interface (major 56)
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/fs.h>
#include <linux/major.h>
#include <linux/selinux.h>
#include <asm/segment.h>

#define MAX_SIDS		96
#define SID_WORDS		3	/* 3 * 32 = 96 bits */
#define MAX_XPERM_RULES		128
#define MAX_NEVERALLOW_RULES	96
#define MAX_TRANS_RULES		64
#define MAX_FILE_CONTEXTS	128
#define MAX_SERVICE_CONTEXTS	32
#define MAX_INODE_SEC		2048
#define AVC_LOG_RING_SIZE	64

/* Type attribute flags */
#define ATTR_DOMAIN		(1UL << 0)
#define ATTR_COREDOMAIN		(1UL << 1)
#define ATTR_APPDOMAIN		(1UL << 2)
#define ATTR_VENDORDOMAIN	(1UL << 3)
#define ATTR_MLSTRUSTED		(1UL << 4)
#define ATTR_FILE_TYPE		(1UL << 5)
#define ATTR_SYSTEM_FILE_TYPE	(1UL << 6)
#define ATTR_VENDOR_FILE_TYPE	(1UL << 7)
#define ATTR_EXEC_TYPE		(1UL << 8)
#define ATTR_DEV_TYPE		(1UL << 9)
#define ATTR_SYSFS_TYPE		(1UL << 10)
#define ATTR_PROC_TYPE		(1UL << 11)
#define ATTR_DATA_FILE_TYPE	(1UL << 12)
#define ATTR_SVCMGR_TYPE	(1UL << 13)

struct sid_set {
	unsigned long bits[SID_WORDS];
};

struct selinux_type_entry {
	char name[32];
	char context[64];
	unsigned char is_domain;
	unsigned char tier;
	unsigned long attrs;
};

struct selinux_xperm_rule {
	unsigned short ssid;
	unsigned short tsid;
	unsigned char tclass;
	unsigned char is_neverallow;
	unsigned short lo;
	unsigned short hi;
};

struct selinux_neverallow_rule {
	struct sid_set src;
	struct sid_set tgt;
	unsigned char is_self;
	unsigned char tclass;
	unsigned long perms;
	char desc[64];
};

struct selinux_trans_rule {
	unsigned short ssid;
	unsigned short tsid;
	unsigned char tclass;
	unsigned short new_sid;
};

struct selinux_file_context {
	char pattern[64];
	unsigned char match_type; /* 0 = exact, 1 = prefix, 2 = regex-prefix */
	unsigned short sid;
};

struct selinux_service_context {
	char name[32];
	unsigned short sid;
};

struct selinux_inode_sec {
	unsigned short dev;
	unsigned long ino;
	unsigned short sid;
	unsigned char explicit_label;
	char path[96];
};

struct selinux_avc_record {
	char line[224];
};

/* Global SELinux state */
static int selinux_enforcing = 1;
static int selinux_initialized = 0;
static int selinux_checkreqprot = 0;
static unsigned long selinux_policy_seqno = 1;
static unsigned long avc_lookups = 0;
static unsigned long avc_hits = 0;
static unsigned long avc_denials = 0;

static struct selinux_type_entry type_table[MAX_SIDS];
static int nr_types = 0;

/* O(1) Access Vector permission matrix: av_allow[ssid][tsid][tclass] */
static unsigned long av_allow[MAX_SIDS][MAX_SIDS][SECCLASS_MAX];

/* Extended permission rules (ioctl) */
static struct selinux_xperm_rule xperm_rules[MAX_XPERM_RULES];
static int nr_xperm_rules = 0;
static unsigned char xperm_active[MAX_SIDS][SECCLASS_MAX];

/* Compile/Load-time Neverallow rules */
static struct selinux_neverallow_rule neverallow_rules[MAX_NEVERALLOW_RULES];
static int nr_neverallow_rules = 0;

/* Domain / Type transition rules */
static struct selinux_trans_rule trans_rules[MAX_TRANS_RULES];
static int nr_trans_rules = 0;

/* File contexts and Service contexts */
static struct selinux_file_context file_contexts[MAX_FILE_CONTEXTS];
static int nr_file_contexts = 0;

static struct selinux_service_context service_contexts[MAX_SERVICE_CONTEXTS];
static int nr_service_contexts = 0;

/* Inode security cache */
static struct selinux_inode_sec inode_sec_table[MAX_INODE_SEC];

/* Recent AVC denial ring buffer */
static struct selinux_avc_record avc_ring[AVC_LOG_RING_SIZE];
static unsigned int avc_ring_head = 0;
static unsigned int avc_ring_count = 0;

/* Response buffer for /sys/fs/selinux/access & /sys/fs/selinux/context */
static char selinux_access_reply[64] = "1\n";
static char selinux_context_reply[128] = "";
static char selinux_load_err[160] = "";

/* Well-known SIDs populated during selinux_init */
static unsigned short sid_unlabeled = 0;
static unsigned short sid_kernel = 1;
static unsigned short sid_init = 2;
static unsigned short sid_su = 3;
static unsigned short sid_shell = 4;
static unsigned short sid_untrusted_app = 5;
static unsigned short sid_servicemanager = 6;
static unsigned short sid_vold = 7;
static unsigned short sid_ntfs_3g = 8;
static unsigned short sid_storaged = 9;
static unsigned short sid_mediaprovider = 10;
static unsigned short sid_externalstoraged = 11;
static unsigned short sid_adbd = 12;
static unsigned short sid_fwupd = 13;
static unsigned short sid_httpd = 14;
static unsigned short sid_telnetd = 15;

static unsigned short sid_init_exec;
static unsigned short sid_shell_exec;
static unsigned short sid_su_exec;
static unsigned short sid_servicemanager_exec;
static unsigned short sid_vold_exec;
static unsigned short sid_ntfs_3g_exec;
static unsigned short sid_storaged_exec;
static unsigned short sid_mediaprovider_exec;
static unsigned short sid_externalstoraged_exec;
static unsigned short sid_adbd_exec;
static unsigned short sid_fwupd_exec;
static unsigned short sid_httpd_exec;
static unsigned short sid_telnetd_exec;

static unsigned short sid_rootfs;
static unsigned short sid_system_file;
static unsigned short sid_vendor_file;
static unsigned short sid_vendor_configs_file;
static unsigned short sid_sepolicy_file;
static unsigned short sid_system_data_file;
static unsigned short sid_vendor_data_file;
static unsigned short sid_app_data_file;
static unsigned short sid_media_rw_data_file;
static unsigned short sid_mnt_media_rw_file;
static unsigned short sid_mnt_media_rw_stub_file;
static unsigned short sid_vfat;
static unsigned short sid_ntfs;
static unsigned short sid_tmpfs;
static unsigned short sid_user_home_file;

static unsigned short sid_device;
static unsigned short sid_null_device;
static unsigned short sid_zero_device;
static unsigned short sid_random_device;
static unsigned short sid_tty_device;
static unsigned short sid_console_device;
static unsigned short sid_devpts;
static unsigned short sid_kmsg_device;
static unsigned short sid_kmem_device;
static unsigned short sid_binder_device;
static unsigned short sid_fuse_device;
static unsigned short sid_adb_device;
static unsigned short sid_power_device;
static unsigned short sid_block_device;
static unsigned short sid_root_block_device;
static unsigned short sid_swap_block_device;
static unsigned short sid_vold_device;
static unsigned short sid_sdx_block_device;
static unsigned short sid_ufs_dev;
static unsigned short sid_ufs_rpmb_device;
static unsigned short sid_nvme_device;

static unsigned short sid_proc;
static unsigned short sid_proc_meminfo;
static unsigned short sid_proc_stat;
static unsigned short sid_proc_version;
static unsigned short sid_proc_cmdline;
static unsigned short sid_proc_diskstats;
static unsigned short sid_proc_net;
static unsigned short sid_proc_sysrq;
static unsigned short sid_debugfs_tracing;

static unsigned short sid_sysfs;
static unsigned short sid_sysfs_power;
static unsigned short sid_sysfs_wake_lock;
static unsigned short sid_selinuxfs;
static unsigned short sid_pstorefs;

static unsigned short sid_default_android_service;
static unsigned short sid_power_service;
static unsigned short sid_sysinfo_service;
static unsigned short sid_vold_service;
static unsigned short sid_mount_service;
static unsigned short sid_suspend_service;
static unsigned short sid_mediaprovider_service;
static unsigned short sid_externalstorage_service;
static unsigned short sid_fwupd_service;

static void sid_set_zero(struct sid_set *s)
{
	int i;
	for (i = 0; i < SID_WORDS; i++)
		s->bits[i] = 0;
}

static void sid_set_add(struct sid_set *s, unsigned short sid)
{
	if (sid < MAX_SIDS)
		s->bits[sid >> 5] |= (1UL << (sid & 31));
}

static void sid_set_del(struct sid_set *s, unsigned short sid)
{
	if (sid < MAX_SIDS)
		s->bits[sid >> 5] &= ~(1UL << (sid & 31));
}

static int sid_set_test(const struct sid_set *s, unsigned short sid)
{
	if (sid >= MAX_SIDS)
		return 0;
	return (s->bits[sid >> 5] & (1UL << (sid & 31))) != 0;
}

static void sid_set_add_attr(struct sid_set *s, unsigned long attr_mask)
{
	int i;
	for (i = 1; i < nr_types; i++) {
		if ((type_table[i].attrs & attr_mask) == attr_mask)
			sid_set_add(s, (unsigned short)i);
	}
}

static void sid_set_del_attr(struct sid_set *s, unsigned long attr_mask)
{
	int i;
	for (i = 1; i < nr_types; i++) {
		if ((type_table[i].attrs & attr_mask) == attr_mask)
			sid_set_del(s, (unsigned short)i);
	}
}

static const char *secclass_to_name(int tclass)
{
	switch (tclass) {
	case SECCLASS_PROCESS:		return "process";
	case SECCLASS_FILE:		return "file";
	case SECCLASS_DIR:		return "dir";
	case SECCLASS_CHR_FILE:		return "chr_file";
	case SECCLASS_BLK_FILE:		return "blk_file";
	case SECCLASS_LNK_FILE:		return "lnk_file";
	case SECCLASS_FIFO_FILE:	return "fifo_file";
	case SECCLASS_SOCK_FILE:	return "sock_file";
	case SECCLASS_FD:		return "fd";
	case SECCLASS_CAPABILITY:	return "capability";
	case SECCLASS_FILESYSTEM:	return "filesystem";
	case SECCLASS_BINDER:		return "binder";
	case SECCLASS_SERVICE_MANAGER:	return "service_manager";
	case SECCLASS_SECURITY:		return "security";
	case SECCLASS_SYSTEM:		return "system";
	default:			return "file";
	}
}

static int name_to_secclass(const char *name)
{
	if (!strcmp(name, "process"))		return SECCLASS_PROCESS;
	if (!strcmp(name, "file"))		return SECCLASS_FILE;
	if (!strcmp(name, "dir"))		return SECCLASS_DIR;
	if (!strcmp(name, "chr_file"))		return SECCLASS_CHR_FILE;
	if (!strcmp(name, "blk_file"))		return SECCLASS_BLK_FILE;
	if (!strcmp(name, "lnk_file"))		return SECCLASS_LNK_FILE;
	if (!strcmp(name, "fifo_file"))		return SECCLASS_FIFO_FILE;
	if (!strcmp(name, "sock_file"))		return SECCLASS_SOCK_FILE;
	if (!strcmp(name, "fd"))		return SECCLASS_FD;
	if (!strcmp(name, "capability") ||
	    !strcmp(name, "capability2"))	return SECCLASS_CAPABILITY;
	if (!strcmp(name, "filesystem"))	return SECCLASS_FILESYSTEM;
	if (!strcmp(name, "binder"))		return SECCLASS_BINDER;
	if (!strcmp(name, "service_manager"))	return SECCLASS_SERVICE_MANAGER;
	if (!strcmp(name, "security"))		return SECCLASS_SECURITY;
	if (!strcmp(name, "system"))		return SECCLASS_SYSTEM;
	return SECCLASS_NONE;
}

static const char *cap_to_name(int cap)
{
	switch (cap) {
	case CAP_CHOWN:			return "chown";
	case CAP_DAC_OVERRIDE:		return "dac_override";
	case CAP_DAC_READ_SEARCH:	return "dac_read_search";
	case CAP_FOWNER:		return "fowner";
	case CAP_FSETID:		return "fsetid";
	case CAP_KILL:			return "kill";
	case CAP_SETGID:		return "setgid";
	case CAP_SETUID:		return "setuid";
	case CAP_SETPCAP:		return "setpcap";
	case CAP_NET_BIND_SERVICE:	return "net_bind_service";
	case CAP_NET_ADMIN:		return "net_admin";
	case CAP_NET_RAW:		return "net_raw";
	case CAP_IPC_LOCK:		return "ipc_lock";
	case CAP_IPC_OWNER:		return "ipc_owner";
	case CAP_SYS_MODULE:		return "sys_module";
	case CAP_SYS_RAWIO:		return "sys_rawio";
	case CAP_SYS_CHROOT:		return "sys_chroot";
	case CAP_SYS_PTRACE:		return "sys_ptrace";
	case CAP_SYS_PACCT:		return "sys_pacct";
	case CAP_SYS_ADMIN:		return "sys_admin";
	case CAP_SYS_BOOT:		return "sys_boot";
	case CAP_SYS_NICE:		return "sys_nice";
	case CAP_SYS_RESOURCE:		return "sys_resource";
	case CAP_SYS_TIME:		return "sys_time";
	case CAP_SYS_TTY_CONFIG:	return "sys_tty_config";
	case CAP_MKNOD:			return "mknod";
	default:			return "sys_admin";
	}
}

static unsigned long name_to_perm(int tclass, const char *name)
{
	if (tclass == SECCLASS_CAPABILITY) {
		if (!strcmp(name, "chown"))		return (1UL << CAP_CHOWN);
		if (!strcmp(name, "dac_override"))	return (1UL << CAP_DAC_OVERRIDE);
		if (!strcmp(name, "dac_read_search"))	return (1UL << CAP_DAC_READ_SEARCH);
		if (!strcmp(name, "fowner"))		return (1UL << CAP_FOWNER);
		if (!strcmp(name, "fsetid"))		return (1UL << CAP_FSETID);
		if (!strcmp(name, "kill"))		return (1UL << CAP_KILL);
		if (!strcmp(name, "setgid"))		return (1UL << CAP_SETGID);
		if (!strcmp(name, "setuid"))		return (1UL << CAP_SETUID);
		if (!strcmp(name, "setpcap"))		return (1UL << CAP_SETPCAP);
		if (!strcmp(name, "net_bind_service"))	return (1UL << CAP_NET_BIND_SERVICE);
		if (!strcmp(name, "net_admin"))		return (1UL << CAP_NET_ADMIN);
		if (!strcmp(name, "net_raw"))		return (1UL << CAP_NET_RAW);
		if (!strcmp(name, "ipc_lock"))		return (1UL << CAP_IPC_LOCK);
		if (!strcmp(name, "ipc_owner"))		return (1UL << CAP_IPC_OWNER);
		if (!strcmp(name, "sys_module"))	return (1UL << CAP_SYS_MODULE);
		if (!strcmp(name, "sys_rawio"))		return (1UL << CAP_SYS_RAWIO);
		if (!strcmp(name, "sys_chroot"))	return (1UL << CAP_SYS_CHROOT);
		if (!strcmp(name, "sys_ptrace"))	return (1UL << CAP_SYS_PTRACE);
		if (!strcmp(name, "sys_pacct"))		return (1UL << CAP_SYS_PACCT);
		if (!strcmp(name, "sys_admin"))		return (1UL << CAP_SYS_ADMIN);
		if (!strcmp(name, "sys_boot"))		return (1UL << CAP_SYS_BOOT);
		if (!strcmp(name, "sys_nice"))		return (1UL << CAP_SYS_NICE);
		if (!strcmp(name, "sys_resource"))	return (1UL << CAP_SYS_RESOURCE);
		if (!strcmp(name, "sys_time"))		return (1UL << CAP_SYS_TIME);
		if (!strcmp(name, "sys_tty_config"))	return (1UL << CAP_SYS_TTY_CONFIG);
		if (!strcmp(name, "mknod"))		return (1UL << CAP_MKNOD);
		return 0;
	}
	if (tclass == SECCLASS_PROCESS) {
		if (!strcmp(name, "fork"))		return SEPERM_PROC_FORK;
		if (!strcmp(name, "transition"))	return SEPERM_PROC_TRANSITION;
		if (!strcmp(name, "sigchld"))		return SEPERM_PROC_SIGCHLD;
		if (!strcmp(name, "sigkill"))		return SEPERM_PROC_SIGKILL;
		if (!strcmp(name, "sigstop"))		return SEPERM_PROC_SIGSTOP;
		if (!strcmp(name, "signal"))		return SEPERM_PROC_SIGNAL;
		if (!strcmp(name, "ptrace"))		return SEPERM_PROC_PTRACE;
		if (!strcmp(name, "getsched"))		return SEPERM_PROC_GETSCHED;
		if (!strcmp(name, "setsched"))		return SEPERM_PROC_SETSCHED;
		if (!strcmp(name, "getpgid"))		return SEPERM_PROC_GETPGID;
		if (!strcmp(name, "setpgid"))		return SEPERM_PROC_SETPGID;
		if (!strcmp(name, "getcap"))		return SEPERM_PROC_GETCAP;
		if (!strcmp(name, "setcap"))		return SEPERM_PROC_SETCAP;
		if (!strcmp(name, "setcurrent"))	return SEPERM_PROC_SETCURRENT;
		if (!strcmp(name, "setexec"))		return SEPERM_PROC_SETEXEC;
		if (!strcmp(name, "dyntransition"))	return SEPERM_PROC_DYNTRANSITION;
		if (!strcmp(name, "setrlimit"))		return SEPERM_PROC_SETRLIMIT;
		if (!strcmp(name, "execmem"))		return SEPERM_PROC_EXECMEM;
		return 0;
	}
	if (tclass == SECCLASS_BINDER) {
		if (!strcmp(name, "impersonate"))	return SEPERM_BINDER_IMPERSONATE;
		if (!strcmp(name, "call"))		return SEPERM_BINDER_CALL;
		if (!strcmp(name, "set_context_mgr"))	return SEPERM_BINDER_SET_CONTEXT_MGR;
		if (!strcmp(name, "transfer"))		return SEPERM_BINDER_TRANSFER;
		return 0;
	}
	if (tclass == SECCLASS_SERVICE_MANAGER) {
		if (!strcmp(name, "add"))		return SEPERM_SVCMGR_ADD;
		if (!strcmp(name, "find"))		return SEPERM_SVCMGR_FIND;
		if (!strcmp(name, "list"))		return SEPERM_SVCMGR_LIST;
		return 0;
	}
	if (tclass == SECCLASS_SECURITY) {
		if (!strcmp(name, "compute_av"))	return SEPERM_SEC_COMPUTE_AV;
		if (!strcmp(name, "compute_create"))	return SEPERM_SEC_COMPUTE_CREATE;
		if (!strcmp(name, "load_policy"))	return SEPERM_SEC_LOAD_POLICY;
		if (!strcmp(name, "setenforce"))	return SEPERM_SEC_SETENFORCE;
		if (!strcmp(name, "setcheckreqprot"))	return SEPERM_SEC_SETCHECKREQPROT;
		if (!strcmp(name, "check_context"))	return SEPERM_SEC_CHECK_CONTEXT;
		return 0;
	}
	if (tclass == SECCLASS_SYSTEM) {
		if (!strcmp(name, "ipc_info"))		return SEPERM_SYS_IPC_INFO;
		if (!strcmp(name, "syslog_read"))	return SEPERM_SYS_SYSLOG_READ;
		if (!strcmp(name, "syslog_mod"))	return SEPERM_SYS_SYSLOG_MOD;
		if (!strcmp(name, "syslog_console"))	return SEPERM_SYS_SYSLOG_CONSOLE;
		if (!strcmp(name, "module_request"))	return SEPERM_SYS_MODULE_REQUEST;
		return 0;
	}
	/* File / Dir / Dev / FS / FD classes */
	if (!strcmp(name, "read"))		return SEPERM_READ;
	if (!strcmp(name, "write"))		return SEPERM_WRITE;
	if (!strcmp(name, "execute"))		return SEPERM_EXECUTE;
	if (!strcmp(name, "open"))		return SEPERM_OPEN;
	if (!strcmp(name, "ioctl"))		return SEPERM_IOCTL;
	if (!strcmp(name, "getattr"))		return SEPERM_GETATTR;
	if (!strcmp(name, "setattr"))		return SEPERM_SETATTR;
	if (!strcmp(name, "create"))		return SEPERM_CREATE;
	if (!strcmp(name, "unlink"))		return SEPERM_UNLINK;
	if (!strcmp(name, "link"))		return SEPERM_LINK;
	if (!strcmp(name, "rename"))		return SEPERM_RENAME;
	if (!strcmp(name, "append"))		return SEPERM_APPEND;
	if (!strcmp(name, "lock"))		return SEPERM_LOCK;
	if (!strcmp(name, "search"))		return SEPERM_SEARCH;
	if (!strcmp(name, "add_name"))		return SEPERM_ADD_NAME;
	if (!strcmp(name, "remove_name"))	return SEPERM_REMOVE_NAME;
	if (!strcmp(name, "rmdir"))		return SEPERM_RMDIR;
	if (!strcmp(name, "execute_no_trans"))	return SEPERM_EXECUTE_NO_TRANS;
	if (!strcmp(name, "entrypoint"))	return SEPERM_ENTRYPOINT;
	if (!strcmp(name, "relabelfrom"))	return SEPERM_RELABELFROM;
	if (!strcmp(name, "relabelto"))		return SEPERM_RELABELTO;
	if (!strcmp(name, "mount") || !strcmp(name, "mounton"))	return SEPERM_MOUNT;
	if (!strcmp(name, "unmount"))		return SEPERM_UNMOUNT;
	if (!strcmp(name, "use"))		return SEPERM_USE;
	if (!strcmp(name, "r_file_perms"))	return (SEPERM_READ | SEPERM_OPEN | SEPERM_GETATTR | SEPERM_IOCTL | SEPERM_LOCK);
	if (!strcmp(name, "rw_file_perms") || !strcmp(name, "create_file_perms"))
		return (SEPERM_READ | SEPERM_WRITE | SEPERM_OPEN | SEPERM_GETATTR |
			SEPERM_SETATTR | SEPERM_IOCTL | SEPERM_LOCK | SEPERM_APPEND |
			SEPERM_CREATE | SEPERM_UNLINK | SEPERM_LINK | SEPERM_RENAME);
	if (!strcmp(name, "rx_file_perms"))
		return (SEPERM_READ | SEPERM_OPEN | SEPERM_GETATTR | SEPERM_EXECUTE | SEPERM_EXECUTE_NO_TRANS);
	if (!strcmp(name, "r_dir_perms"))
		return (SEPERM_READ | SEPERM_OPEN | SEPERM_GETATTR | SEPERM_SEARCH);
	if (!strcmp(name, "rw_dir_perms") || !strcmp(name, "create_dir_perms"))
		return (SEPERM_READ | SEPERM_WRITE | SEPERM_OPEN | SEPERM_GETATTR |
			SEPERM_SETATTR | SEPERM_SEARCH | SEPERM_ADD_NAME |
			SEPERM_REMOVE_NAME | SEPERM_CREATE | SEPERM_RMDIR | SEPERM_RENAME);
	return 0;
}

static const char *perm_to_name(int tclass, unsigned long perm)
{
	int i;
	if (tclass == SECCLASS_CAPABILITY) {
		for (i = 0; i < 32; i++) {
			if (perm & (1UL << i))
				return cap_to_name(i);
		}
		return "sys_admin";
	}
	if (tclass == SECCLASS_PROCESS) {
		if (perm & SEPERM_PROC_FORK)		return "fork";
		if (perm & SEPERM_PROC_TRANSITION)	return "transition";
		if (perm & SEPERM_PROC_SIGCHLD)		return "sigchld";
		if (perm & SEPERM_PROC_SIGKILL)		return "sigkill";
		if (perm & SEPERM_PROC_SIGSTOP)		return "sigstop";
		if (perm & SEPERM_PROC_SIGNAL)		return "signal";
		if (perm & SEPERM_PROC_PTRACE)		return "ptrace";
		if (perm & SEPERM_PROC_GETSCHED)	return "getsched";
		if (perm & SEPERM_PROC_SETSCHED)	return "setsched";
		if (perm & SEPERM_PROC_GETPGID)		return "getpgid";
		if (perm & SEPERM_PROC_SETPGID)		return "setpgid";
		if (perm & SEPERM_PROC_GETCAP)		return "getcap";
		if (perm & SEPERM_PROC_SETCAP)		return "setcap";
		if (perm & SEPERM_PROC_SETCURRENT)	return "setcurrent";
		if (perm & SEPERM_PROC_SETEXEC)		return "setexec";
		if (perm & SEPERM_PROC_DYNTRANSITION)	return "dyntransition";
		if (perm & SEPERM_PROC_SETRLIMIT)	return "setrlimit";
		if (perm & SEPERM_PROC_EXECMEM)		return "execmem";
		return "transition";
	}
	if (tclass == SECCLASS_BINDER) {
		if (perm & SEPERM_BINDER_IMPERSONATE)		return "impersonate";
		if (perm & SEPERM_BINDER_CALL)			return "call";
		if (perm & SEPERM_BINDER_SET_CONTEXT_MGR)	return "set_context_mgr";
		if (perm & SEPERM_BINDER_TRANSFER)		return "transfer";
		return "call";
	}
	if (tclass == SECCLASS_SERVICE_MANAGER) {
		if (perm & SEPERM_SVCMGR_ADD)	return "add";
		if (perm & SEPERM_SVCMGR_FIND)	return "find";
		if (perm & SEPERM_SVCMGR_LIST)	return "list";
		return "find";
	}
	if (tclass == SECCLASS_SECURITY) {
		if (perm & SEPERM_SEC_COMPUTE_AV)	return "compute_av";
		if (perm & SEPERM_SEC_COMPUTE_CREATE)	return "compute_create";
		if (perm & SEPERM_SEC_LOAD_POLICY)	return "load_policy";
		if (perm & SEPERM_SEC_SETENFORCE)	return "setenforce";
		if (perm & SEPERM_SEC_SETCHECKREQPROT)	return "setcheckreqprot";
		if (perm & SEPERM_SEC_CHECK_CONTEXT)	return "check_context";
		return "setenforce";
	}
	if (tclass == SECCLASS_SYSTEM) {
		if (perm & SEPERM_SYS_IPC_INFO)		return "ipc_info";
		if (perm & SEPERM_SYS_SYSLOG_READ)	return "syslog_read";
		if (perm & SEPERM_SYS_SYSLOG_MOD)	return "syslog_mod";
		if (perm & SEPERM_SYS_SYSLOG_CONSOLE)	return "syslog_console";
		if (perm & SEPERM_SYS_MODULE_REQUEST)	return "module_request";
		return "syslog_read";
	}
	if (perm & SEPERM_READ)			return "read";
	if (perm & SEPERM_WRITE)		return "write";
	if (perm & SEPERM_EXECUTE)		return "execute";
	if (perm & SEPERM_OPEN)			return "open";
	if (perm & SEPERM_IOCTL)		return "ioctl";
	if (perm & SEPERM_GETATTR)		return "getattr";
	if (perm & SEPERM_SETATTR)		return "setattr";
	if (perm & SEPERM_CREATE)		return "create";
	if (perm & SEPERM_UNLINK)		return "unlink";
	if (perm & SEPERM_LINK)			return "link";
	if (perm & SEPERM_RENAME)		return "rename";
	if (perm & SEPERM_APPEND)		return "append";
	if (perm & SEPERM_LOCK)			return "lock";
	if (perm & SEPERM_SEARCH)		return "search";
	if (perm & SEPERM_ADD_NAME)		return "add_name";
	if (perm & SEPERM_REMOVE_NAME)		return "remove_name";
	if (perm & SEPERM_RMDIR)		return "rmdir";
	if (perm & SEPERM_EXECUTE_NO_TRANS)	return "execute_no_trans";
	if (perm & SEPERM_ENTRYPOINT)		return "entrypoint";
	if (perm & SEPERM_RELABELFROM)		return "relabelfrom";
	if (perm & SEPERM_RELABELTO)		return "relabelto";
	if (perm & SEPERM_MOUNT)		return "mount";
	if (perm & SEPERM_UNMOUNT)		return "unmount";
	if (perm & SEPERM_USE)			return "use";
	return "access";
}

static unsigned short register_type(const char *name, int is_domain,
				    int tier, unsigned long attrs)
{
	int i;
	for (i = 0; i < nr_types; i++) {
		if (!strcmp(type_table[i].name, name)) {
			type_table[i].attrs |= attrs;
			if (tier == SELINUX_TIER_PUBLIC)
				type_table[i].tier = SELINUX_TIER_PUBLIC;
			return (unsigned short)i;
		}
	}
	if (nr_types >= MAX_SIDS)
		return 0;
	i = nr_types++;
	strncpy(type_table[i].name, name, sizeof(type_table[i].name) - 1);
	type_table[i].name[sizeof(type_table[i].name) - 1] = '\0';
	type_table[i].is_domain = (unsigned char)is_domain;
	type_table[i].tier = (unsigned char)tier;
	type_table[i].attrs = attrs;
	if (is_domain)
		sprintf(type_table[i].context, "u:r:%s:s0", name);
	else
		sprintf(type_table[i].context, "u:object_r:%s:s0", name);
	return (unsigned short)i;
}

unsigned short selinux_type_to_sid(const char *type_name)
{
	int i;
	if (!type_name || !type_name[0])
		return 0;
	if (!strcmp(type_name, "security"))
		return sid_selinuxfs;
	if (!strcmp(type_name, "suspend_service"))
		return sid_suspend_service;
	for (i = 0; i < nr_types; i++) {
		if (!strcmp(type_table[i].name, type_name))
			return (unsigned short)i;
	}
	return 0;
}

unsigned short selinux_context_to_sid(const char *ctx)
{
	char buf[64];
	char *p, *t, *end;
	int i = 0;

	if (!ctx || !ctx[0])
		return 0;
	while (*ctx == ' ' || *ctx == '\t')
		ctx++;
	while (ctx[i] && ctx[i] != '\n' && ctx[i] != '\r' &&
	       ctx[i] != ' ' && ctx[i] != '\t' && i < (int)sizeof(buf) - 1) {
		buf[i] = ctx[i];
		i++;
	}
	buf[i] = '\0';

	/* Check full context match first */
	for (i = 0; i < nr_types; i++) {
		if (!strcmp(type_table[i].context, buf))
			return (unsigned short)i;
	}

	/* Extract 3rd colon-separated field u:r:<type>:s0 or direct type name */
	p = strchr(buf, ':');
	if (p) {
		p = strchr(p + 1, ':');
		if (p) {
			t = p + 1;
			end = strchr(t, ':');
			if (end)
				*end = '\0';
			return selinux_type_to_sid(t);
		}
	}
	return selinux_type_to_sid(buf);
}

const char *selinux_sid_to_context(unsigned short sid)
{
	if (sid < (unsigned short)nr_types && type_table[sid].context[0])
		return type_table[sid].context;
	return "u:object_r:unlabeled:s0";
}

const char *selinux_sid_to_type(unsigned short sid)
{
	if (sid < (unsigned short)nr_types && type_table[sid].name[0])
		return type_table[sid].name;
	return "unlabeled";
}

int selinux_is_enforcing(void)
{
	return selinux_enforcing;
}

void selinux_set_enforcing(int enforce)
{
	selinux_enforcing = enforce ? 1 : 0;
}

/* Grant helper functions */
static void allow_sid(unsigned short ssid, unsigned short tsid,
		      int tclass, unsigned long perms)
{
	if (ssid < MAX_SIDS && tsid < MAX_SIDS &&
	    tclass > SECCLASS_NONE && tclass < SECCLASS_MAX)
		av_allow[ssid][tsid][tclass] |= perms;
}

static void allow_attr(unsigned short ssid, unsigned long tgt_attr,
		       int tclass, unsigned long perms)
{
	int t;
	for (t = 1; t < nr_types; t++) {
		if ((type_table[t].attrs & tgt_attr) == tgt_attr)
			allow_sid(ssid, (unsigned short)t, tclass, perms);
	}
}

static void allow_src_attr_sid(unsigned long src_attr, unsigned short tsid,
			       int tclass, unsigned long perms)
{
	int s;
	for (s = 1; s < nr_types; s++) {
		if ((type_table[s].attrs & src_attr) == src_attr)
			allow_sid((unsigned short)s, tsid, tclass, perms);
	}
}

static void allow_src_attr_tgt_attr(unsigned long src_attr, unsigned long tgt_attr,
				    int tclass, unsigned long perms)
{
	int s, t;
	for (s = 1; s < nr_types; s++) {
		if ((type_table[s].attrs & src_attr) != src_attr)
			continue;
		for (t = 1; t < nr_types; t++) {
			if ((type_table[t].attrs & tgt_attr) == tgt_attr)
				allow_sid((unsigned short)s, (unsigned short)t, tclass, perms);
		}
	}
}

static void add_xperm(unsigned short ssid, unsigned short tsid,
		      int tclass, unsigned short lo, unsigned short hi,
		      int is_neverallow)
{
	if (nr_xperm_rules < MAX_XPERM_RULES) {
		xperm_rules[nr_xperm_rules].ssid = ssid;
		xperm_rules[nr_xperm_rules].tsid = tsid;
		xperm_rules[nr_xperm_rules].tclass = (unsigned char)tclass;
		xperm_rules[nr_xperm_rules].is_neverallow = (unsigned char)is_neverallow;
		xperm_rules[nr_xperm_rules].lo = lo;
		xperm_rules[nr_xperm_rules].hi = hi;
		nr_xperm_rules++;
	}
	if (!is_neverallow && tsid < MAX_SIDS && tclass < SECCLASS_MAX)
		xperm_active[tsid][tclass] = 1;
}

static void add_trans(unsigned short ssid, unsigned short tsid,
		      int tclass, unsigned short new_sid)
{
	int i;
	for (i = 0; i < nr_trans_rules; i++) {
		if (trans_rules[i].ssid == ssid &&
		    trans_rules[i].tsid == tsid &&
		    trans_rules[i].tclass == tclass) {
			trans_rules[i].new_sid = new_sid;
			return;
		}
	}
	if (nr_trans_rules < MAX_TRANS_RULES) {
		trans_rules[nr_trans_rules].ssid = ssid;
		trans_rules[nr_trans_rules].tsid = tsid;
		trans_rules[nr_trans_rules].tclass = (unsigned char)tclass;
		trans_rules[nr_trans_rules].new_sid = new_sid;
		nr_trans_rules++;
	}
}

static void add_domain_auto_trans(unsigned short parent_dom,
				  unsigned short exec_sid,
				  unsigned short child_dom)
{
	add_trans(parent_dom, exec_sid, SECCLASS_PROCESS, child_dom);
	allow_sid(parent_dom, exec_sid, SECCLASS_FILE,
		  SEPERM_READ | SEPERM_OPEN | SEPERM_EXECUTE | SEPERM_GETATTR);
	allow_sid(parent_dom, child_dom, SECCLASS_PROCESS,
		  SEPERM_PROC_TRANSITION | SEPERM_PROC_SIGCHLD |
		  SEPERM_PROC_SIGKILL | SEPERM_PROC_SIGSTOP | SEPERM_PROC_SIGNAL);
	allow_sid(child_dom, parent_dom, SECCLASS_PROCESS, SEPERM_PROC_SIGCHLD);
	allow_sid(child_dom, parent_dom, SECCLASS_FD, SEPERM_USE);
	allow_sid(child_dom, exec_sid, SECCLASS_FILE,
		  SEPERM_READ | SEPERM_OPEN | SEPERM_EXECUTE |
		  SEPERM_GETATTR | SEPERM_ENTRYPOINT);
}

static void add_file_context(const char *pattern, int match_type, unsigned short sid)
{
	int i;
	for (i = 0; i < nr_file_contexts; i++) {
		if (!strcmp(file_contexts[i].pattern, pattern) &&
		    file_contexts[i].match_type == match_type) {
			file_contexts[i].sid = sid;
			return;
		}
	}
	if (nr_file_contexts < MAX_FILE_CONTEXTS) {
		strncpy(file_contexts[nr_file_contexts].pattern, pattern,
			sizeof(file_contexts[nr_file_contexts].pattern) - 1);
		file_contexts[nr_file_contexts].pattern[sizeof(file_contexts[nr_file_contexts].pattern) - 1] = '\0';
		file_contexts[nr_file_contexts].match_type = (unsigned char)match_type;
		file_contexts[nr_file_contexts].sid = sid;
		nr_file_contexts++;
	}
}

static void add_service_context(const char *name, unsigned short sid)
{
	int i;
	for (i = 0; i < nr_service_contexts; i++) {
		if (!strcmp(service_contexts[i].name, name)) {
			service_contexts[i].sid = sid;
			return;
		}
	}
	if (nr_service_contexts < MAX_SERVICE_CONTEXTS) {
		strncpy(service_contexts[nr_service_contexts].name, name,
			sizeof(service_contexts[nr_service_contexts].name) - 1);
		service_contexts[nr_service_contexts].name[sizeof(service_contexts[nr_service_contexts].name) - 1] = '\0';
		service_contexts[nr_service_contexts].sid = sid;
		nr_service_contexts++;
	}
}

static void add_neverallow(const struct sid_set *src, const struct sid_set *tgt,
			   int is_self, int tclass, unsigned long perms,
			   const char *desc)
{
	if (nr_neverallow_rules < MAX_NEVERALLOW_RULES) {
		neverallow_rules[nr_neverallow_rules].src = *src;
		neverallow_rules[nr_neverallow_rules].tgt = *tgt;
		neverallow_rules[nr_neverallow_rules].is_self = (unsigned char)is_self;
		neverallow_rules[nr_neverallow_rules].tclass = (unsigned char)tclass;
		neverallow_rules[nr_neverallow_rules].perms = perms;
		strncpy(neverallow_rules[nr_neverallow_rules].desc, desc,
			sizeof(neverallow_rules[nr_neverallow_rules].desc) - 1);
		neverallow_rules[nr_neverallow_rules].desc[sizeof(neverallow_rules[nr_neverallow_rules].desc) - 1] = '\0';
		nr_neverallow_rules++;
	}
}

/* Verify all neverallow and neverallowxperm assertions against av_allow */
static int selinux_verify_neverallows(char *err_buf, int err_len)
{
	int i, s, t, j;

	for (i = 0; i < nr_neverallow_rules; i++) {
		struct selinux_neverallow_rule *nr = &neverallow_rules[i];
		int tc = nr->tclass;

		for (s = 1; s < nr_types; s++) {
			if (!sid_set_test(&nr->src, (unsigned short)s))
				continue;
			if (nr->is_self) {
				unsigned long hit = av_allow[s][s][tc] & nr->perms;
				if (hit) {
					if (err_buf && err_len > 0) {
						sprintf(err_buf,
							"neverallow violation: %s self:%s { %s } (%s)",
							type_table[s].name,
							secclass_to_name(tc),
							perm_to_name(tc, hit),
							nr->desc);
					}
					printk(KERN_ERR "selinux: neverallow failure: allow %s self:%s { %s } violates %s\n",
					       type_table[s].name,
					       secclass_to_name(tc),
					       perm_to_name(tc, hit),
					       nr->desc);
					return -EACCES;
				}
				continue;
			}
			for (t = 1; t < nr_types; t++) {
				unsigned long hit;
				if (!sid_set_test(&nr->tgt, (unsigned short)t))
					continue;
				hit = av_allow[s][t][tc] & nr->perms;
				if (hit) {
					if (err_buf && err_len > 0) {
						sprintf(err_buf,
							"neverallow violation: %s %s:%s { %s } (%s)",
							type_table[s].name,
							type_table[t].name,
							secclass_to_name(tc),
							perm_to_name(tc, hit),
							nr->desc);
					}
					printk(KERN_ERR "selinux: neverallow failure: allow %s %s:%s { %s } violates %s\n",
					       type_table[s].name,
					       type_table[t].name,
					       secclass_to_name(tc),
					       perm_to_name(tc, hit),
					       nr->desc);
					return -EACCES;
				}
			}
		}
	}

	/* Verify neverallowxperm rules against allowxperm rules */
	for (i = 0; i < nr_xperm_rules; i++) {
		if (!xperm_rules[i].is_neverallow)
			continue;
		for (j = 0; j < nr_xperm_rules; j++) {
			if (xperm_rules[j].is_neverallow)
				continue;
			if (xperm_rules[i].ssid == xperm_rules[j].ssid &&
			    xperm_rules[i].tsid == xperm_rules[j].tsid &&
			    xperm_rules[i].tclass == xperm_rules[j].tclass &&
			    xperm_rules[j].lo <= xperm_rules[i].hi &&
			    xperm_rules[j].hi >= xperm_rules[i].lo) {
				if (err_buf && err_len > 0) {
					sprintf(err_buf,
						"neverallowxperm violation: %s %s:%s ioctl 0x%x",
						selinux_sid_to_type(xperm_rules[j].ssid),
						selinux_sid_to_type(xperm_rules[j].tsid),
						secclass_to_name(xperm_rules[j].tclass),
						xperm_rules[j].lo);
				}
				printk(KERN_ERR "selinux: neverallowxperm failure: %s %s:%s ioctl 0x%x\n",
				       selinux_sid_to_type(xperm_rules[j].ssid),
				       selinux_sid_to_type(xperm_rules[j].tsid),
				       secclass_to_name(xperm_rules[j].tclass),
				       xperm_rules[j].lo);
				return -EACCES;
			}
		}
	}

	return 0;
}

#define RW_FILE_PERMS	(SEPERM_READ | SEPERM_WRITE | SEPERM_OPEN | SEPERM_GETATTR | \
			 SEPERM_SETATTR | SEPERM_IOCTL | SEPERM_LOCK | SEPERM_APPEND | \
			 SEPERM_CREATE | SEPERM_UNLINK | SEPERM_LINK | SEPERM_RENAME)
#define RO_FILE_PERMS	(SEPERM_READ | SEPERM_OPEN | SEPERM_GETATTR | SEPERM_IOCTL | SEPERM_LOCK)
#define RX_FILE_PERMS	(SEPERM_READ | SEPERM_OPEN | SEPERM_GETATTR | SEPERM_EXECUTE | SEPERM_EXECUTE_NO_TRANS)
#define RW_DIR_PERMS	(SEPERM_READ | SEPERM_WRITE | SEPERM_OPEN | SEPERM_GETATTR | \
			 SEPERM_SETATTR | SEPERM_SEARCH | SEPERM_ADD_NAME | \
			 SEPERM_REMOVE_NAME | SEPERM_CREATE | SEPERM_RMDIR | SEPERM_RENAME)
#define RO_DIR_PERMS	(SEPERM_READ | SEPERM_OPEN | SEPERM_GETATTR | SEPERM_SEARCH)

/*
 * Populate the baseline 3-tier Android Desktop SELinux policy:
 *   Tier 1: system/public
 *   Tier 2: system/private
 *   Tier 3: vendor
 */
static void selinux_seed_baseline_policy(void)
{
	int s, d;
	struct sid_set sset, tset;

	nr_types = 0;
	nr_xperm_rules = 0;
	nr_neverallow_rules = 0;
	nr_trans_rules = 0;
	nr_file_contexts = 0;
	nr_service_contexts = 0;
	memset(av_allow, 0, sizeof(av_allow));
	memset(xperm_active, 0, sizeof(xperm_active));

	/* 0: unlabeled */
	sid_unlabeled = register_type("unlabeled", 0, SELINUX_TIER_PUBLIC, ATTR_FILE_TYPE);

	/* Process Domains (1..15) */
	sid_kernel = register_type("kernel", 1, SELINUX_TIER_PUBLIC,
				   ATTR_DOMAIN | ATTR_COREDOMAIN | ATTR_MLSTRUSTED);
	sid_init = register_type("init", 1, SELINUX_TIER_PUBLIC,
				 ATTR_DOMAIN | ATTR_COREDOMAIN | ATTR_MLSTRUSTED);
	sid_su = register_type("su", 1, SELINUX_TIER_PUBLIC,
			       ATTR_DOMAIN | ATTR_COREDOMAIN | ATTR_MLSTRUSTED);
	sid_shell = register_type("shell", 1, SELINUX_TIER_PUBLIC,
				  ATTR_DOMAIN | ATTR_COREDOMAIN);
	sid_untrusted_app = register_type("untrusted_app", 1, SELINUX_TIER_PUBLIC,
					  ATTR_DOMAIN | ATTR_COREDOMAIN | ATTR_APPDOMAIN);
	sid_servicemanager = register_type("servicemanager", 1, SELINUX_TIER_PUBLIC,
					   ATTR_DOMAIN | ATTR_COREDOMAIN);
	sid_vold = register_type("vold", 1, SELINUX_TIER_PUBLIC,
				 ATTR_DOMAIN | ATTR_COREDOMAIN | ATTR_MLSTRUSTED);
	/* ntfs_3g is system/private coredomain (ag/41792131) */
	sid_ntfs_3g = register_type("ntfs_3g", 1, SELINUX_TIER_PRIVATE,
				    ATTR_DOMAIN | ATTR_COREDOMAIN);
	sid_storaged = register_type("storaged", 1, SELINUX_TIER_PUBLIC,
				     ATTR_DOMAIN | ATTR_COREDOMAIN);
	sid_mediaprovider = register_type("mediaprovider", 1, SELINUX_TIER_PUBLIC,
					  ATTR_DOMAIN | ATTR_COREDOMAIN | ATTR_APPDOMAIN);
	sid_externalstoraged = register_type("externalstoraged", 1, SELINUX_TIER_PUBLIC,
					     ATTR_DOMAIN | ATTR_COREDOMAIN);
	sid_adbd = register_type("adbd", 1, SELINUX_TIER_PUBLIC,
				 ATTR_DOMAIN | ATTR_COREDOMAIN);
	/* fwupd is declared in system/public and implemented in vendor (b/467820671) */
	sid_fwupd = register_type("fwupd", 1, SELINUX_TIER_PUBLIC,
				  ATTR_DOMAIN | ATTR_VENDORDOMAIN);
	sid_httpd = register_type("httpd", 1, SELINUX_TIER_PRIVATE,
				  ATTR_DOMAIN | ATTR_COREDOMAIN);
	sid_telnetd = register_type("telnetd", 1, SELINUX_TIER_PRIVATE,
				    ATTR_DOMAIN | ATTR_COREDOMAIN);

	/* Executable Entrypoint Types */
	sid_init_exec = register_type("init_exec", 0, SELINUX_TIER_PUBLIC,
				      ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_shell_exec = register_type("shell_exec", 0, SELINUX_TIER_PUBLIC,
				       ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_su_exec = register_type("su_exec", 0, SELINUX_TIER_PUBLIC,
				    ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_servicemanager_exec = register_type("servicemanager_exec", 0, SELINUX_TIER_PUBLIC,
						ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_vold_exec = register_type("vold_exec", 0, SELINUX_TIER_PUBLIC,
				      ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_ntfs_3g_exec = register_type("ntfs_3g_exec", 0, SELINUX_TIER_PRIVATE,
					 ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_storaged_exec = register_type("storaged_exec", 0, SELINUX_TIER_PUBLIC,
					  ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_mediaprovider_exec = register_type("mediaprovider_exec", 0, SELINUX_TIER_PUBLIC,
					       ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_externalstoraged_exec = register_type("externalstoraged_exec", 0, SELINUX_TIER_PUBLIC,
						  ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_adbd_exec = register_type("adbd_exec", 0, SELINUX_TIER_PUBLIC,
				      ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_fwupd_exec = register_type("fwupd_exec", 0, SELINUX_TIER_PUBLIC,
				       ATTR_FILE_TYPE | ATTR_VENDOR_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_httpd_exec = register_type("httpd_exec", 0, SELINUX_TIER_PRIVATE,
				       ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_telnetd_exec = register_type("telnetd_exec", 0, SELINUX_TIER_PRIVATE,
					 ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);

	/* Filesystem & Data Types */
	sid_rootfs = register_type("rootfs", 0, SELINUX_TIER_PUBLIC,
				   ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE);
	sid_system_file = register_type("system_file", 0, SELINUX_TIER_PUBLIC,
					ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_vendor_file = register_type("vendor_file", 0, SELINUX_TIER_PUBLIC,
					ATTR_FILE_TYPE | ATTR_VENDOR_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_vendor_configs_file = register_type("vendor_configs_file", 0, SELINUX_TIER_PUBLIC,
						ATTR_FILE_TYPE | ATTR_VENDOR_FILE_TYPE);
	sid_sepolicy_file = register_type("sepolicy_file", 0, SELINUX_TIER_PUBLIC,
					  ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE);
	sid_system_data_file = register_type("system_data_file", 0, SELINUX_TIER_PUBLIC,
					     ATTR_FILE_TYPE | ATTR_DATA_FILE_TYPE);
	sid_vendor_data_file = register_type("vendor_data_file", 0, SELINUX_TIER_PUBLIC,
					     ATTR_FILE_TYPE | ATTR_DATA_FILE_TYPE | ATTR_VENDOR_FILE_TYPE);
	sid_app_data_file = register_type("app_data_file", 0, SELINUX_TIER_PUBLIC,
					  ATTR_FILE_TYPE | ATTR_DATA_FILE_TYPE);
	sid_media_rw_data_file = register_type("media_rw_data_file", 0, SELINUX_TIER_PUBLIC,
					       ATTR_FILE_TYPE | ATTR_DATA_FILE_TYPE);
	sid_mnt_media_rw_file = register_type("mnt_media_rw_file", 0, SELINUX_TIER_PUBLIC,
					      ATTR_FILE_TYPE);
	sid_mnt_media_rw_stub_file = register_type("mnt_media_rw_stub_file", 0, SELINUX_TIER_PUBLIC,
						   ATTR_FILE_TYPE);
	sid_vfat = register_type("vfat", 0, SELINUX_TIER_PUBLIC, ATTR_FILE_TYPE);
	sid_ntfs = register_type("ntfs", 0, SELINUX_TIER_PUBLIC, ATTR_FILE_TYPE);
	sid_tmpfs = register_type("tmpfs", 0, SELINUX_TIER_PUBLIC, ATTR_FILE_TYPE);
	sid_user_home_file = register_type("user_home_file", 0, SELINUX_TIER_PUBLIC,
					   ATTR_FILE_TYPE | ATTR_DATA_FILE_TYPE);

	/* Device Node Types */
	sid_device = register_type("device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_null_device = register_type("null_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_zero_device = register_type("zero_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_random_device = register_type("random_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_tty_device = register_type("tty_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_console_device = register_type("console_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_devpts = register_type("devpts", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_kmsg_device = register_type("kmsg_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_kmem_device = register_type("kmem_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_binder_device = register_type("binder_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_fuse_device = register_type("fuse_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_adb_device = register_type("adb_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_power_device = register_type("power_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_block_device = register_type("block_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_root_block_device = register_type("root_block_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_swap_block_device = register_type("swap_block_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_vold_device = register_type("vold_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_sdx_block_device = register_type("sdx_block_device", 0, SELINUX_TIER_PUBLIC, ATTR_DEV_TYPE);
	sid_ufs_dev = register_type("ufs_dev", 0, SELINUX_TIER_VENDOR, ATTR_DEV_TYPE);
	sid_ufs_rpmb_device = register_type("ufs_rpmb_device", 0, SELINUX_TIER_VENDOR, ATTR_DEV_TYPE);
	sid_nvme_device = register_type("nvme_device", 0, SELINUX_TIER_VENDOR, ATTR_DEV_TYPE);

	/* Procfs & Sysfs Types */
	sid_proc = register_type("proc", 0, SELINUX_TIER_PUBLIC, ATTR_PROC_TYPE);
	sid_proc_meminfo = register_type("proc_meminfo", 0, SELINUX_TIER_PUBLIC, ATTR_PROC_TYPE);
	sid_proc_stat = register_type("proc_stat", 0, SELINUX_TIER_PUBLIC, ATTR_PROC_TYPE);
	sid_proc_version = register_type("proc_version", 0, SELINUX_TIER_PUBLIC, ATTR_PROC_TYPE);
	sid_proc_cmdline = register_type("proc_cmdline", 0, SELINUX_TIER_PUBLIC, ATTR_PROC_TYPE);
	sid_proc_diskstats = register_type("proc_diskstats", 0, SELINUX_TIER_PUBLIC, ATTR_PROC_TYPE);
	sid_proc_net = register_type("proc_net", 0, SELINUX_TIER_PUBLIC, ATTR_PROC_TYPE);
	sid_proc_sysrq = register_type("proc_sysrq", 0, SELINUX_TIER_PUBLIC, ATTR_PROC_TYPE);
	sid_debugfs_tracing = register_type("debugfs_tracing", 0, SELINUX_TIER_PUBLIC, ATTR_PROC_TYPE);

	sid_sysfs = register_type("sysfs", 0, SELINUX_TIER_PUBLIC, ATTR_SYSFS_TYPE);
	sid_sysfs_power = register_type("sysfs_power", 0, SELINUX_TIER_PUBLIC, ATTR_SYSFS_TYPE);
	sid_sysfs_wake_lock = register_type("sysfs_wake_lock", 0, SELINUX_TIER_PUBLIC, ATTR_SYSFS_TYPE);
	register_type("sysfs_devices_block", 0, SELINUX_TIER_PUBLIC, ATTR_SYSFS_TYPE);
	register_type("sysfs_ufs", 0, SELINUX_TIER_PUBLIC, ATTR_SYSFS_TYPE);
	register_type("sysfs_nvme", 0, SELINUX_TIER_PUBLIC, ATTR_SYSFS_TYPE);
	register_type("shadow_file", 0, SELINUX_TIER_PUBLIC, ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE);
	register_type("fsck_exec", 0, SELINUX_TIER_PUBLIC, ATTR_FILE_TYPE | ATTR_SYSTEM_FILE_TYPE | ATTR_EXEC_TYPE);
	sid_selinuxfs = register_type("selinuxfs", 0, SELINUX_TIER_PUBLIC, ATTR_SYSFS_TYPE);
	sid_pstorefs = register_type("pstorefs", 0, SELINUX_TIER_PUBLIC, ATTR_SYSFS_TYPE);

	/* ServiceManager Types */
	sid_default_android_service = register_type("default_android_service", 0, SELINUX_TIER_PUBLIC, ATTR_SVCMGR_TYPE);
	sid_power_service = register_type("power_service", 0, SELINUX_TIER_PUBLIC, ATTR_SVCMGR_TYPE);
	sid_sysinfo_service = register_type("sysinfo_service", 0, SELINUX_TIER_PUBLIC, ATTR_SVCMGR_TYPE);
	sid_vold_service = register_type("vold_service", 0, SELINUX_TIER_PUBLIC, ATTR_SVCMGR_TYPE);
	sid_mount_service = register_type("mount_service", 0, SELINUX_TIER_PUBLIC, ATTR_SVCMGR_TYPE);
	sid_suspend_service = register_type("system_suspend_control_service", 0, SELINUX_TIER_PUBLIC, ATTR_SVCMGR_TYPE);
	sid_mediaprovider_service = register_type("mediaprovider_service", 0, SELINUX_TIER_PUBLIC, ATTR_SVCMGR_TYPE);
	sid_externalstorage_service = register_type("externalstorage_service", 0, SELINUX_TIER_PUBLIC, ATTR_SVCMGR_TYPE);
	sid_fwupd_service = register_type("fwupd_service", 0, SELINUX_TIER_PUBLIC, ATTR_SVCMGR_TYPE);

	/*
	 * ===================================================================
	 * 1. Base rules for all domains (sepolicy/system/public/domain.te)
	 * ===================================================================
	 */
	for (s = 1; s < nr_types; s++) {
		if (!(type_table[s].attrs & ATTR_DOMAIN))
			continue;
		d = s;
		/* Self process, fd, fifo, sock permissions */
		allow_sid(d, d, SECCLASS_PROCESS,
			  SEPERM_PROC_FORK | SEPERM_PROC_SIGCHLD |
			  SEPERM_PROC_SIGKILL | SEPERM_PROC_SIGSTOP |
			  SEPERM_PROC_SIGNAL | SEPERM_PROC_GETSCHED |
			  SEPERM_PROC_SETSCHED | SEPERM_PROC_GETPGID |
			  SEPERM_PROC_SETPGID | SEPERM_PROC_GETCAP |
			  SEPERM_PROC_SETRLIMIT);
		allow_sid(d, d, SECCLASS_FD, SEPERM_USE);
		allow_sid(d, d, SECCLASS_DIR, RO_DIR_PERMS);
		allow_sid(d, d, SECCLASS_FILE, RW_FILE_PERMS);
		allow_sid(d, d, SECCLASS_LNK_FILE, RO_FILE_PERMS);
		allow_sid(d, d, SECCLASS_FIFO_FILE, RW_FILE_PERMS);
		allow_sid(d, d, SECCLASS_SOCK_FILE, RW_FILE_PERMS);

		/* Inherit FDs from init / su / shell / adbd */
		allow_sid(d, sid_init, SECCLASS_FD, SEPERM_USE);
		allow_sid(d, sid_su, SECCLASS_FD, SEPERM_USE);
		allow_sid(d, sid_shell, SECCLASS_FD, SEPERM_USE);
		allow_sid(d, sid_adbd, SECCLASS_FD, SEPERM_USE);
		allow_sid(d, sid_init, SECCLASS_PROCESS, SEPERM_PROC_SIGCHLD);
		allow_sid(d, sid_su, SECCLASS_PROCESS, SEPERM_PROC_SIGCHLD);
		allow_sid(d, sid_shell, SECCLASS_PROCESS, SEPERM_PROC_SIGCHLD);

		/* Rootfs, system_file,device directory traversal & symlinks */
		allow_sid(d, sid_rootfs, SECCLASS_DIR, RO_DIR_PERMS);
		allow_sid(d, sid_rootfs, SECCLASS_LNK_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_rootfs, SECCLASS_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_system_file, SECCLASS_DIR, RO_DIR_PERMS);
		allow_sid(d, sid_system_file, SECCLASS_FILE, RO_FILE_PERMS | SEPERM_EXECUTE | SEPERM_ENTRYPOINT);
		allow_sid(d, sid_system_file, SECCLASS_LNK_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_shell_exec, SECCLASS_FILE, RO_FILE_PERMS | SEPERM_EXECUTE | SEPERM_ENTRYPOINT);
		allow_sid(d, sid_vendor_file, SECCLASS_DIR, RO_DIR_PERMS);
		allow_sid(d, sid_vendor_file, SECCLASS_LNK_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_device, SECCLASS_DIR, RO_DIR_PERMS);
		allow_sid(d, sid_device, SECCLASS_LNK_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_block_device, SECCLASS_DIR, RO_DIR_PERMS);

		/* /dev/null, /dev/zero, /dev/random, /dev/urandom, /dev/tty*, /dev/console */
		allow_sid(d, sid_null_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
		allow_sid(d, sid_zero_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
		allow_sid(d, sid_random_device, SECCLASS_CHR_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_tty_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
		allow_sid(d, sid_console_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
		allow_sid(d, sid_devpts, SECCLASS_DIR, RO_DIR_PERMS);
		allow_sid(d, sid_devpts, SECCLASS_CHR_FILE, RW_FILE_PERMS);

		/* /tmp scratch files & anonymous pipes */
		allow_sid(d, sid_tmpfs, SECCLASS_DIR, RW_DIR_PERMS);
		allow_sid(d, sid_tmpfs, SECCLASS_FILE, RW_FILE_PERMS);
		allow_sid(d, sid_tmpfs, SECCLASS_FIFO_FILE, RW_FILE_PERMS);
		allow_sid(d, sid_tmpfs, SECCLASS_SOCK_FILE, RW_FILE_PERMS);
		allow_sid(d, sid_system_file, SECCLASS_FIFO_FILE, RW_FILE_PERMS);

		/* Basic /proc & /sys/fs/selinux status queries */
		allow_sid(d, sid_proc, SECCLASS_DIR, RO_DIR_PERMS);
		allow_sid(d, sid_proc, SECCLASS_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_proc, SECCLASS_LNK_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_proc_meminfo, SECCLASS_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_proc_stat, SECCLASS_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_proc_version, SECCLASS_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_proc_cmdline, SECCLASS_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_sysfs, SECCLASS_DIR, RO_DIR_PERMS);
		allow_sid(d, sid_selinuxfs, SECCLASS_DIR, RO_DIR_PERMS);
		allow_sid(d, sid_selinuxfs, SECCLASS_FILE, RW_FILE_PERMS);
		allow_sid(d, sid_selinuxfs, SECCLASS_CHR_FILE, RW_FILE_PERMS);
		allow_sid(d, sid_sepolicy_file, SECCLASS_DIR, RO_DIR_PERMS);
		allow_sid(d, sid_sepolicy_file, SECCLASS_FILE, RO_FILE_PERMS);
		allow_sid(d, sid_vendor_configs_file, SECCLASS_DIR, RO_DIR_PERMS);
		allow_sid(d, sid_vendor_configs_file, SECCLASS_FILE, RO_FILE_PERMS);
	}

	/* Coredomains can execute system binaries without transition (except neverallow entrypoints) */
	for (s = 1; s < nr_types; s++) {
		if (!(type_table[s].attrs & ATTR_COREDOMAIN))
			continue;
		if (s == sid_ntfs_3g)
			continue; /* ntfs_3g is strictly confined */
		allow_sid(s, sid_system_file, SECCLASS_FILE, RX_FILE_PERMS | SEPERM_ENTRYPOINT);
		allow_sid(s, sid_shell_exec, SECCLASS_FILE, RX_FILE_PERMS | SEPERM_ENTRYPOINT);
		allow_sid(s, sid_system_data_file, SECCLASS_DIR, RO_DIR_PERMS);
	}

	/*
	 * ===================================================================
	 * 2. Automatic Domain Transitions
	 * ===================================================================
	 */
	add_domain_auto_trans(sid_kernel, sid_init_exec, sid_init);
	add_domain_auto_trans(sid_init, sid_servicemanager_exec, sid_servicemanager);
	add_domain_auto_trans(sid_init, sid_vold_exec, sid_vold);
	add_domain_auto_trans(sid_init, sid_ntfs_3g_exec, sid_ntfs_3g);
	add_domain_auto_trans(sid_init, sid_storaged_exec, sid_storaged);
	add_domain_auto_trans(sid_init, sid_mediaprovider_exec, sid_mediaprovider);
	add_domain_auto_trans(sid_init, sid_externalstoraged_exec, sid_externalstoraged);
	add_domain_auto_trans(sid_init, sid_adbd_exec, sid_adbd);
	add_domain_auto_trans(sid_init, sid_fwupd_exec, sid_fwupd);
	add_domain_auto_trans(sid_init, sid_httpd_exec, sid_httpd);
	add_domain_auto_trans(sid_init, sid_telnetd_exec, sid_telnetd);

	/* vold -> ntfs_3g domain transition (sepolicy/system/private/ntfs_3g.te) */
	add_domain_auto_trans(sid_vold, sid_ntfs_3g_exec, sid_ntfs_3g);

	/* Also allow su/shell/untrusted_app transitions */
	add_domain_auto_trans(sid_su, sid_vold_exec, sid_vold);
	add_domain_auto_trans(sid_su, sid_ntfs_3g_exec, sid_ntfs_3g);
	add_domain_auto_trans(sid_su, sid_storaged_exec, sid_storaged);
	add_domain_auto_trans(sid_su, sid_mediaprovider_exec, sid_mediaprovider);
	add_domain_auto_trans(sid_su, sid_externalstoraged_exec, sid_externalstoraged);
	add_domain_auto_trans(sid_su, sid_fwupd_exec, sid_fwupd);
	add_domain_auto_trans(sid_shell, sid_su_exec, sid_su);
	add_domain_auto_trans(sid_untrusted_app, sid_su_exec, sid_su);
	add_domain_auto_trans(sid_adbd, sid_shell_exec, sid_shell);

	/*
	 * ===================================================================
	 * 3. Privileged Bootstrap Domains: kernel, init, su
	 * ===================================================================
	 */
	{
		unsigned short priv_doms[3];
		int k, t, c;
		priv_doms[0] = sid_kernel;
		priv_doms[1] = sid_init;
		priv_doms[2] = sid_su;

		for (k = 0; k < 3; k++) {
			unsigned short pd = priv_doms[k];
			for (t = 0; t < nr_types; t++) {
				for (c = 1; c < SECCLASS_MAX; c++) {
					av_allow[pd][t][c] = ~0UL;
				}
			}
			/* Prevent neverallow violations on init/kernel/su:
			 * coredomain cannot execute_no_trans vendor_file_type */
			av_allow[pd][sid_vendor_file][SECCLASS_FILE] &= ~SEPERM_EXECUTE_NO_TRANS;
			av_allow[pd][sid_fwupd_exec][SECCLASS_FILE] &= ~SEPERM_EXECUTE_NO_TRANS;
			av_allow[pd][sid_vendor_configs_file][SECCLASS_FILE] &= ~(SEPERM_EXECUTE | SEPERM_EXECUTE_NO_TRANS);
			av_allow[pd][sid_vendor_data_file][SECCLASS_FILE] &= ~(SEPERM_EXECUTE | SEPERM_EXECUTE_NO_TRANS);
			/* No W^X execute on data files */
			for (t = 1; t < nr_types; t++) {
				if (type_table[t].attrs & ATTR_DATA_FILE_TYPE) {
					av_allow[pd][t][SECCLASS_FILE] &= ~(SEPERM_EXECUTE | SEPERM_EXECUTE_NO_TRANS | SEPERM_ENTRYPOINT);
				}
			}
		}
	}

	/*
	 * ===================================================================
	 * 4. servicemanager (sepolicy/system/private/servicemanager.te)
	 * ===================================================================
	 */
	allow_sid(sid_servicemanager, sid_binder_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_servicemanager, sid_servicemanager, SECCLASS_BINDER,
		  SEPERM_BINDER_SET_CONTEXT_MGR | SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_attr(sid_servicemanager, ATTR_DOMAIN, SECCLASS_BINDER,
		   SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_attr(sid_servicemanager, ATTR_SVCMGR_TYPE, SECCLASS_SERVICE_MANAGER,
		   SEPERM_SVCMGR_ADD | SEPERM_SVCMGR_FIND | SEPERM_SVCMGR_LIST);
	allow_attr(sid_servicemanager, ATTR_DOMAIN, SECCLASS_DIR, RO_DIR_PERMS);
	allow_attr(sid_servicemanager, ATTR_DOMAIN, SECCLASS_FILE, RO_FILE_PERMS);
	allow_sid(sid_servicemanager, sid_selinuxfs, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_servicemanager, sid_selinuxfs, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_servicemanager, sid_sysfs_power, SECCLASS_DIR, RO_DIR_PERMS);
	allow_sid(sid_servicemanager, sid_sysfs_power, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_servicemanager, sid_sysfs_power, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_servicemanager, sid_sysfs_wake_lock, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_servicemanager, sid_sysfs_wake_lock, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_servicemanager, sid_power_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_servicemanager, sid_servicemanager, SECCLASS_SECURITY,
		  SEPERM_SEC_COMPUTE_AV | SEPERM_SEC_CHECK_CONTEXT);

	/*
	 * ===================================================================
	 * 5. vold (sepolicy/system/private/vold.te)
	 * ===================================================================
	 * Note: vold drops to AID_MEDIA_RW (1023) before exec'ing ntfs-3g
	 * (applications/vold/Utils.cpp), requiring capability { setuid setgid }!
	 */
	allow_sid(sid_vold, sid_vold, SECCLASS_CAPABILITY,
		  (1UL << CAP_SYS_ADMIN) | (1UL << CAP_SYS_RAWIO) |
		  (1UL << CAP_MKNOD) | (1UL << CAP_CHOWN) |
		  (1UL << CAP_FOWNER) | (1UL << CAP_FSETID) |
		  (1UL << CAP_DAC_OVERRIDE) | (1UL << CAP_DAC_READ_SEARCH) |
		  (1UL << CAP_SETUID) | (1UL << CAP_SETGID) | (1UL << CAP_KILL));
	allow_sid(sid_vold, sid_binder_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_servicemanager, SECCLASS_BINDER,
		  SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_attr(sid_vold, ATTR_DOMAIN, SECCLASS_BINDER,
		   SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_sid(sid_vold, sid_vold_service, SECCLASS_SERVICE_MANAGER,
		  SEPERM_SVCMGR_ADD | SEPERM_SVCMGR_FIND);
	allow_sid(sid_vold, sid_mount_service, SECCLASS_SERVICE_MANAGER,
		  SEPERM_SVCMGR_ADD | SEPERM_SVCMGR_FIND);
	allow_sid(sid_vold, sid_mediaprovider_service, SECCLASS_SERVICE_MANAGER,
		  SEPERM_SVCMGR_FIND);
	allow_sid(sid_vold, sid_externalstorage_service, SECCLASS_SERVICE_MANAGER,
		  SEPERM_SVCMGR_FIND);
	allow_sid(sid_vold, sid_fuse_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_device, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_vold, sid_block_device, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_vold, sid_vold_device, SECCLASS_DIR, RW_DIR_PERMS | SEPERM_MOUNT | SEPERM_UNMOUNT);
	allow_sid(sid_vold, sid_vold_device, SECCLASS_BLK_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_block_device, SECCLASS_BLK_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_sdx_block_device, SECCLASS_BLK_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_nvme_device, SECCLASS_BLK_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_nvme_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_sysfs_power, SECCLASS_DIR, RO_DIR_PERMS);
	allow_sid(sid_vold, sid_sysfs_power, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_sysfs_power, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_sysfs_wake_lock, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_sysfs_wake_lock, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_power_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_mnt_media_rw_file, SECCLASS_DIR, RW_DIR_PERMS | SEPERM_MOUNT | SEPERM_UNMOUNT);
	allow_sid(sid_vold, sid_mnt_media_rw_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_mnt_media_rw_file, SECCLASS_LNK_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_mnt_media_rw_stub_file, SECCLASS_DIR, RW_DIR_PERMS | SEPERM_MOUNT | SEPERM_UNMOUNT);
	allow_sid(sid_vold, sid_mnt_media_rw_stub_file, SECCLASS_LNK_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_media_rw_data_file, SECCLASS_DIR, RW_DIR_PERMS | SEPERM_MOUNT | SEPERM_UNMOUNT);
	allow_sid(sid_vold, sid_media_rw_data_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_media_rw_data_file, SECCLASS_LNK_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_system_data_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_vold, sid_system_data_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_vfat, SECCLASS_FILESYSTEM, SEPERM_MOUNT | SEPERM_UNMOUNT | SEPERM_GETATTR);
	allow_sid(sid_vold, sid_vfat, SECCLASS_DIR, RW_DIR_PERMS | SEPERM_MOUNT | SEPERM_UNMOUNT);
	allow_sid(sid_vold, sid_vfat, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_ntfs, SECCLASS_FILESYSTEM, SEPERM_MOUNT | SEPERM_UNMOUNT | SEPERM_GETATTR);
	allow_sid(sid_vold, sid_ntfs, SECCLASS_DIR, RW_DIR_PERMS | SEPERM_MOUNT | SEPERM_UNMOUNT);
	allow_sid(sid_vold, sid_ntfs, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_vold, sid_proc_diskstats, SECCLASS_FILE, RO_FILE_PERMS);
	add_xperm(sid_vold, sid_vold_device, SECCLASS_BLK_FILE, 0x0000, 0xffff, 0);
	add_xperm(sid_vold, sid_block_device, SECCLASS_BLK_FILE, 0x0000, 0xffff, 0);
	add_xperm(sid_vold, sid_sdx_block_device, SECCLASS_BLK_FILE, 0x0000, 0xffff, 0);
	add_xperm(sid_vold, sid_nvme_device, SECCLASS_BLK_FILE, 0x0000, 0xffff, 0);
	add_xperm(sid_vold, sid_nvme_device, SECCLASS_CHR_FILE, 0x0000, 0xffff, 0);

	/*
	 * ===================================================================
	 * 6. ntfs_3g (sepolicy/system/private/ntfs_3g.te - ag/41792131)
	 * ===================================================================
	 * Strictly confined FUSE driver domain spawned by vold:
	 *   - Can use vold's inherited file descriptors
	 *   - Can read/write/open/ioctl /dev/fuse (fuse_device)
	 *   - Can read/write/open/ioctl storage block devices (vold_device, block_device)
	 *   - Can mount/unmount and read/write /mnt/media_rw/* (mnt_media_rw_file)
	 *   - Cannot access /data/system, /dev/binder, /dev/ufsa SG_IO, etc.
	 */
	allow_sid(sid_ntfs_3g, sid_ntfs_3g, SECCLASS_CAPABILITY,
		  (1UL << CAP_SYS_ADMIN) | (1UL << CAP_DAC_OVERRIDE) |
		  (1UL << CAP_DAC_READ_SEARCH) | (1UL << CAP_SETUID) | (1UL << CAP_SETGID));
	allow_sid(sid_ntfs_3g, sid_vold, SECCLASS_FD, SEPERM_USE);
	allow_sid(sid_ntfs_3g, sid_fuse_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_ntfs_3g, sid_vold_device, SECCLASS_BLK_FILE, RW_FILE_PERMS);
	allow_sid(sid_ntfs_3g, sid_swap_block_device, SECCLASS_BLK_FILE, RW_FILE_PERMS);
	allow_sid(sid_ntfs_3g, sid_block_device, SECCLASS_BLK_FILE, RW_FILE_PERMS);
	allow_sid(sid_ntfs_3g, sid_mnt_media_rw_file, SECCLASS_DIR,
		  RW_DIR_PERMS | SEPERM_MOUNT | SEPERM_UNMOUNT);
	allow_sid(sid_ntfs_3g, sid_mnt_media_rw_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_ntfs_3g, sid_mnt_media_rw_file, SECCLASS_LNK_FILE, RW_FILE_PERMS);
	allow_sid(sid_ntfs_3g, sid_mnt_media_rw_stub_file, SECCLASS_DIR,
		  RW_DIR_PERMS | SEPERM_MOUNT | SEPERM_UNMOUNT);
	allow_sid(sid_ntfs_3g, sid_ntfs, SECCLASS_FILESYSTEM,
		  SEPERM_MOUNT | SEPERM_UNMOUNT | SEPERM_GETATTR);
	allow_sid(sid_ntfs_3g, sid_ntfs, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_ntfs_3g, sid_ntfs, SECCLASS_FILE, RW_FILE_PERMS);
	add_xperm(sid_ntfs_3g, sid_vold_device, SECCLASS_BLK_FILE, 0x0300, 0x033f, 0);
	add_xperm(sid_ntfs_3g, sid_vold_device, SECCLASS_BLK_FILE, 0x125d, 0x127f, 0);
	add_xperm(sid_ntfs_3g, sid_block_device, SECCLASS_BLK_FILE, 0x0300, 0x033f, 0);
	add_xperm(sid_ntfs_3g, sid_block_device, SECCLASS_BLK_FILE, 0x125d, 0x127f, 0);

	/*
	 * ===================================================================
	 * 7. fwupd (sepolicy/system/public/fwupd.te + sepolicy/vendor/fwupd.te)
	 *    Refs: b/467820671, ag/41847641, arsp/9037735
	 * ===================================================================
	 * Runs as vendor daemon (/vendor/bin/fwupd), registers "fwupd" Binder
	 * service, writes firmware staging/history in /data/vendor/fwupd
	 * (vendor_data_file), and performs UFS/NVMe/SCSI firmware updates using
	 * capability { sys_rawio sys_admin } and allowxperm ioctl 0x2285 (SG_IO).
	 */
	allow_sid(sid_fwupd, sid_fwupd, SECCLASS_CAPABILITY,
		  (1UL << CAP_SYS_RAWIO) | (1UL << CAP_SYS_ADMIN) |
		  (1UL << CAP_DAC_OVERRIDE) | (1UL << CAP_DAC_READ_SEARCH));
	allow_sid(sid_fwupd, sid_vendor_file, SECCLASS_FILE, RX_FILE_PERMS);
	allow_sid(sid_fwupd, sid_fwupd_exec, SECCLASS_FILE,
		  RX_FILE_PERMS | SEPERM_ENTRYPOINT);
	allow_sid(sid_fwupd, sid_binder_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_fwupd, sid_servicemanager, SECCLASS_BINDER,
		  SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_attr(sid_fwupd, ATTR_DOMAIN, SECCLASS_BINDER,
		   SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_sid(sid_fwupd, sid_fwupd_service, SECCLASS_SERVICE_MANAGER,
		  SEPERM_SVCMGR_ADD | SEPERM_SVCMGR_FIND);
	allow_sid(sid_fwupd, sid_vendor_data_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_fwupd, sid_vendor_data_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_fwupd, sid_vendor_data_file, SECCLASS_LNK_FILE, RW_FILE_PERMS);
	allow_sid(sid_fwupd, sid_vendor_configs_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_fwupd, sid_vendor_configs_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_fwupd, sid_system_data_file, SECCLASS_DIR, RO_DIR_PERMS);
	allow_sid(sid_fwupd, sid_sdx_block_device, SECCLASS_BLK_FILE, RW_FILE_PERMS);
	allow_sid(sid_fwupd, sid_ufs_dev, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_fwupd, sid_ufs_rpmb_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_fwupd, sid_nvme_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_fwupd, sid_nvme_device, SECCLASS_BLK_FILE, RW_FILE_PERMS);
	allow_sid(sid_fwupd, sid_vold_device, SECCLASS_BLK_FILE, RW_FILE_PERMS);
	allow_sid(sid_fwupd, sid_block_device, SECCLASS_BLK_FILE, RO_FILE_PERMS);
	allow_sid(sid_fwupd, sid_proc_diskstats, SECCLASS_FILE, RO_FILE_PERMS);

	/* Explicit allowxperm for UFS/NVMe/SCSI firmware update ioctls */
	add_xperm(sid_fwupd, sid_sdx_block_device, SECCLASS_BLK_FILE, SG_IO, SG_IO, 0);
	add_xperm(sid_fwupd, sid_sdx_block_device, SECCLASS_BLK_FILE, 0x5540, 0x5543, 0);
	add_xperm(sid_fwupd, sid_ufs_dev, SECCLASS_CHR_FILE, SG_IO, SG_IO, 0);
	add_xperm(sid_fwupd, sid_ufs_dev, SECCLASS_CHR_FILE, 0x5540, 0x5543, 0);
	add_xperm(sid_fwupd, sid_ufs_rpmb_device, SECCLASS_CHR_FILE, 0x5540, 0x5543, 0);
	add_xperm(sid_fwupd, sid_nvme_device, SECCLASS_CHR_FILE, 0x4e40, 0x4e43, 0);
	add_xperm(sid_fwupd, sid_nvme_device, SECCLASS_BLK_FILE, 0x4e40, 0x4e43, 0);
	add_xperm(sid_fwupd, sid_vold_device, SECCLASS_BLK_FILE, SG_IO, SG_IO, 0);
	add_xperm(sid_fwupd, sid_vold_device, SECCLASS_BLK_FILE, 0x5382, 0x5386, 0);
	add_xperm(sid_fwupd, sid_vold_device, SECCLASS_BLK_FILE, 0x5540, 0x5543, 0);

	/*
	 * ===================================================================
	 * 8. storaged (sepolicy/system/private/storaged.te - ag/38427513)
	 * ===================================================================
	 * Handles power, sysinfo, suspend services, and storage metrics.
	 * Explicitly does NOT have sys_rawio or SG_IO 0x2285!
	 */
	allow_sid(sid_storaged, sid_storaged, SECCLASS_CAPABILITY,
		  (1UL << CAP_SYS_ADMIN) | (1UL << CAP_DAC_OVERRIDE) | (1UL << CAP_DAC_READ_SEARCH));
	allow_sid(sid_storaged, sid_binder_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_storaged, sid_servicemanager, SECCLASS_BINDER,
		  SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_attr(sid_storaged, ATTR_DOMAIN, SECCLASS_BINDER,
		   SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_sid(sid_storaged, sid_power_service, SECCLASS_SERVICE_MANAGER,
		  SEPERM_SVCMGR_ADD | SEPERM_SVCMGR_FIND);
	allow_sid(sid_storaged, sid_sysinfo_service, SECCLASS_SERVICE_MANAGER,
		  SEPERM_SVCMGR_ADD | SEPERM_SVCMGR_FIND);
	allow_sid(sid_storaged, sid_suspend_service, SECCLASS_SERVICE_MANAGER,
		  SEPERM_SVCMGR_ADD | SEPERM_SVCMGR_FIND);
	allow_sid(sid_storaged, sid_vold_service, SECCLASS_SERVICE_MANAGER, SEPERM_SVCMGR_FIND);
	allow_sid(sid_storaged, sid_mount_service, SECCLASS_SERVICE_MANAGER,
		  SEPERM_SVCMGR_ADD | SEPERM_SVCMGR_FIND);
	allow_sid(sid_storaged, sid_system_data_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_storaged, sid_media_rw_data_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_storaged, sid_rootfs, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_storaged, sid_sysfs_power, SECCLASS_DIR, RO_DIR_PERMS);
	allow_sid(sid_storaged, sid_sysfs_power, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_storaged, sid_sysfs_power, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_storaged, sid_sysfs_wake_lock, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_storaged, sid_sysfs_wake_lock, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_storaged, sid_power_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_storaged, sid_pstorefs, SECCLASS_DIR, RO_DIR_PERMS);
	allow_sid(sid_storaged, sid_pstorefs, SECCLASS_FILE, RO_FILE_PERMS);
	allow_sid(sid_storaged, sid_proc_diskstats, SECCLASS_FILE, RO_FILE_PERMS);
	allow_attr(sid_storaged, ATTR_DOMAIN, SECCLASS_DIR, RO_DIR_PERMS);
	allow_attr(sid_storaged, ATTR_DOMAIN, SECCLASS_FILE, RO_FILE_PERMS);

	/*
	 * ===================================================================
	 * 9. mediaprovider & externalstoraged
	 * ===================================================================
	 */
	allow_sid(sid_mediaprovider, sid_mediaprovider, SECCLASS_CAPABILITY,
		  (1UL << CAP_SYS_ADMIN) | (1UL << CAP_DAC_OVERRIDE) |
		  (1UL << CAP_DAC_READ_SEARCH) | (1UL << CAP_CHOWN) | (1UL << CAP_FOWNER));
	allow_sid(sid_mediaprovider, sid_fuse_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_mediaprovider, sid_binder_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_mediaprovider, sid_servicemanager, SECCLASS_BINDER,
		  SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_attr(sid_mediaprovider, ATTR_DOMAIN, SECCLASS_BINDER,
		   SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_sid(sid_mediaprovider, sid_mediaprovider_service, SECCLASS_SERVICE_MANAGER,
		  SEPERM_SVCMGR_ADD | SEPERM_SVCMGR_FIND);
	allow_sid(sid_mediaprovider, sid_mount_service, SECCLASS_SERVICE_MANAGER, SEPERM_SVCMGR_FIND);
	allow_sid(sid_mediaprovider, sid_externalstorage_service, SECCLASS_SERVICE_MANAGER, SEPERM_SVCMGR_FIND);
	allow_sid(sid_mediaprovider, sid_mnt_media_rw_stub_file, SECCLASS_DIR,
		  RW_DIR_PERMS | SEPERM_MOUNT | SEPERM_UNMOUNT);
	allow_sid(sid_mediaprovider, sid_mnt_media_rw_file, SECCLASS_DIR,
		  RW_DIR_PERMS | SEPERM_MOUNT | SEPERM_UNMOUNT);
	allow_sid(sid_mediaprovider, sid_mnt_media_rw_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_mediaprovider, sid_mnt_media_rw_file, SECCLASS_LNK_FILE, RW_FILE_PERMS);
	allow_sid(sid_mediaprovider, sid_media_rw_data_file, SECCLASS_DIR,
		  RW_DIR_PERMS | SEPERM_MOUNT | SEPERM_UNMOUNT);
	allow_sid(sid_mediaprovider, sid_media_rw_data_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_mediaprovider, sid_media_rw_data_file, SECCLASS_LNK_FILE, RW_FILE_PERMS);
	allow_sid(sid_mediaprovider, sid_system_data_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_mediaprovider, sid_system_data_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_mediaprovider, sid_rootfs, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_mediaprovider, sid_app_data_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_mediaprovider, sid_app_data_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_mediaprovider, sid_vfat, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_mediaprovider, sid_vfat, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_mediaprovider, sid_ntfs, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_mediaprovider, sid_ntfs, SECCLASS_FILE, RW_FILE_PERMS);

	allow_sid(sid_externalstoraged, sid_externalstoraged, SECCLASS_CAPABILITY,
		  (1UL << CAP_DAC_OVERRIDE) | (1UL << CAP_DAC_READ_SEARCH));
	allow_sid(sid_externalstoraged, sid_binder_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_externalstoraged, sid_servicemanager, SECCLASS_BINDER,
		  SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_attr(sid_externalstoraged, ATTR_DOMAIN, SECCLASS_BINDER,
		   SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_sid(sid_externalstoraged, sid_externalstorage_service, SECCLASS_SERVICE_MANAGER,
		  SEPERM_SVCMGR_ADD | SEPERM_SVCMGR_FIND);
	allow_sid(sid_externalstoraged, sid_mediaprovider_service, SECCLASS_SERVICE_MANAGER, SEPERM_SVCMGR_FIND);
	allow_sid(sid_externalstoraged, sid_mount_service, SECCLASS_SERVICE_MANAGER, SEPERM_SVCMGR_FIND);
	allow_sid(sid_externalstoraged, sid_vold_service, SECCLASS_SERVICE_MANAGER, SEPERM_SVCMGR_FIND);
	allow_sid(sid_externalstoraged, sid_mnt_media_rw_stub_file, SECCLASS_DIR, RO_DIR_PERMS);
	allow_sid(sid_externalstoraged, sid_mnt_media_rw_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_externalstoraged, sid_mnt_media_rw_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_externalstoraged, sid_mnt_media_rw_file, SECCLASS_LNK_FILE, RO_FILE_PERMS);
	allow_sid(sid_externalstoraged, sid_media_rw_data_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_externalstoraged, sid_media_rw_data_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_externalstoraged, sid_media_rw_data_file, SECCLASS_LNK_FILE, RO_FILE_PERMS);
	allow_sid(sid_externalstoraged, sid_system_data_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_externalstoraged, sid_system_data_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_externalstoraged, sid_vfat, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_externalstoraged, sid_vfat, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_externalstoraged, sid_ntfs, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_externalstoraged, sid_ntfs, SECCLASS_FILE, RW_FILE_PERMS);

	/*
	 * ===================================================================
	 * 10. adbd, httpd, telnetd
	 * ===================================================================
	 */
	allow_sid(sid_adbd, sid_adbd, SECCLASS_CAPABILITY,
		  (1UL << CAP_SETUID) | (1UL << CAP_SETGID) |
		  (1UL << CAP_DAC_OVERRIDE) | (1UL << CAP_DAC_READ_SEARCH) |
		  (1UL << CAP_KILL) | (1UL << CAP_SYS_PTRACE) | (1UL << CAP_SYS_TTY_CONFIG));
	allow_sid(sid_adbd, sid_adb_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_adbd, sid_binder_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_adbd, sid_servicemanager, SECCLASS_BINDER,
		  SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_attr(sid_adbd, ATTR_DOMAIN, SECCLASS_BINDER,
		   SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_attr(sid_adbd, ATTR_SVCMGR_TYPE, SECCLASS_SERVICE_MANAGER,
		   SEPERM_SVCMGR_FIND | SEPERM_SVCMGR_LIST);
	allow_sid(sid_adbd, sid_shell, SECCLASS_PROCESS,
		  SEPERM_PROC_TRANSITION | SEPERM_PROC_DYNTRANSITION |
		  SEPERM_PROC_SIGCHLD | SEPERM_PROC_SIGNAL | SEPERM_PROC_SIGKILL);
	allow_sid(sid_adbd, sid_su, SECCLASS_PROCESS,
		  SEPERM_PROC_TRANSITION | SEPERM_PROC_DYNTRANSITION |
		  SEPERM_PROC_SIGCHLD | SEPERM_PROC_SIGNAL | SEPERM_PROC_SIGKILL);
	allow_sid(sid_adbd, sid_adbd, SECCLASS_PROCESS,
		  SEPERM_PROC_SETCURRENT | SEPERM_PROC_SETEXEC);
	allow_sid(sid_adbd, sid_system_data_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_adbd, sid_system_data_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_adbd, sid_user_home_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_adbd, sid_user_home_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_attr(sid_adbd, ATTR_DOMAIN, SECCLASS_DIR, RO_DIR_PERMS);
	allow_attr(sid_adbd, ATTR_DOMAIN, SECCLASS_FILE, RO_FILE_PERMS);

	allow_sid(sid_httpd, sid_httpd, SECCLASS_CAPABILITY, (1UL << CAP_NET_BIND_SERVICE));
	allow_sid(sid_httpd, sid_proc_net, SECCLASS_FILE, RO_FILE_PERMS);
	allow_sid(sid_httpd, sid_user_home_file, SECCLASS_DIR, RO_DIR_PERMS);
	allow_sid(sid_httpd, sid_user_home_file, SECCLASS_FILE, RO_FILE_PERMS);

	allow_sid(sid_telnetd, sid_telnetd, SECCLASS_CAPABILITY,
		  (1UL << CAP_NET_BIND_SERVICE) | (1UL << CAP_SETUID) |
		  (1UL << CAP_SETGID) | (1UL << CAP_SYS_TTY_CONFIG));
	allow_sid(sid_telnetd, sid_proc_net, SECCLASS_FILE, RO_FILE_PERMS);
	allow_sid(sid_telnetd, sid_su, SECCLASS_PROCESS,
		  SEPERM_PROC_TRANSITION | SEPERM_PROC_DYNTRANSITION | SEPERM_PROC_SIGCHLD);
	allow_sid(sid_telnetd, sid_shell, SECCLASS_PROCESS,
		  SEPERM_PROC_TRANSITION | SEPERM_PROC_DYNTRANSITION | SEPERM_PROC_SIGCHLD);

	/*
	 * ===================================================================
	 * 11. shell (u:r:shell:s0)
	 * ===================================================================
	 * Can run CLI tools (service, dumpsys, sm, fwupdmgr via Binder, ps,
	 * top, strace, etc.), talk to Binder services (including fwupd_service),
	 * read /proc stats, /sys/fs/pstore, /mnt/media_rw, /home.
	 * Cannot directly open /dev/ufsa or /dev/nvme0n1 for raw writes or
	 * issue SG_IO (0x2285) or use sys_rawio!
	 */
	allow_sid(sid_shell, sid_shell, SECCLASS_CAPABILITY, (1UL << CAP_SYS_PTRACE));
	allow_sid(sid_shell, sid_shell_exec, SECCLASS_FILE, RX_FILE_PERMS | SEPERM_ENTRYPOINT);
	allow_sid(sid_shell, sid_adbd, SECCLASS_FD, SEPERM_USE);
	allow_sid(sid_shell, sid_binder_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_shell, sid_servicemanager, SECCLASS_BINDER,
		  SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_attr(sid_shell, ATTR_DOMAIN, SECCLASS_BINDER,
		   SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_attr(sid_shell, ATTR_SVCMGR_TYPE, SECCLASS_SERVICE_MANAGER,
		   SEPERM_SVCMGR_FIND | SEPERM_SVCMGR_LIST);
	allow_sid(sid_shell, sid_vendor_configs_file, SECCLASS_DIR, RO_DIR_PERMS);
	allow_sid(sid_shell, sid_vendor_configs_file, SECCLASS_FILE, RO_FILE_PERMS);
	allow_sid(sid_shell, sid_proc_diskstats, SECCLASS_FILE, RO_FILE_PERMS);
	allow_sid(sid_shell, sid_proc_net, SECCLASS_FILE, RO_FILE_PERMS);
	allow_sid(sid_shell, sid_debugfs_tracing, SECCLASS_FILE, RO_FILE_PERMS);
	allow_sid(sid_shell, sid_pstorefs, SECCLASS_DIR, RO_DIR_PERMS);
	allow_sid(sid_shell, sid_pstorefs, SECCLASS_FILE, RO_FILE_PERMS);
	allow_sid(sid_shell, sid_sysfs_power, SECCLASS_DIR, RO_DIR_PERMS);
	allow_sid(sid_shell, sid_sysfs_power, SECCLASS_CHR_FILE, RO_FILE_PERMS);
	allow_sid(sid_shell, sid_sysfs_power, SECCLASS_FILE, RO_FILE_PERMS);
	allow_sid(sid_shell, sid_mnt_media_rw_stub_file, SECCLASS_DIR, RO_DIR_PERMS);
	allow_sid(sid_shell, sid_mnt_media_rw_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_shell, sid_mnt_media_rw_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_shell, sid_mnt_media_rw_file, SECCLASS_LNK_FILE, RO_FILE_PERMS);
	allow_sid(sid_shell, sid_media_rw_data_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_shell, sid_media_rw_data_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_shell, sid_media_rw_data_file, SECCLASS_LNK_FILE, RO_FILE_PERMS);
	allow_sid(sid_shell, sid_vfat, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_shell, sid_vfat, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_shell, sid_ntfs, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_shell, sid_ntfs, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_shell, sid_user_home_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_shell, sid_user_home_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_shell, sid_su_exec, SECCLASS_FILE, RX_FILE_PERMS);
	allow_sid(sid_shell, sid_su, SECCLASS_PROCESS,
		  SEPERM_PROC_TRANSITION | SEPERM_PROC_SIGCHLD);
	allow_attr(sid_shell, ATTR_DOMAIN, SECCLASS_DIR, RO_DIR_PERMS);
	allow_attr(sid_shell, ATTR_DOMAIN, SECCLASS_FILE, RO_FILE_PERMS);
	allow_attr(sid_shell, ATTR_DOMAIN, SECCLASS_LNK_FILE, RO_FILE_PERMS);

	/*
	 * ===================================================================
	 * 12. untrusted_app (u:r:untrusted_app:s0 - sepolicy/system/private/untrusted_app.te)
	 * ===================================================================
	 * Strictly sandboxed app domain (users six, guest, alice):
	 *   - Can access its own /home/* (user_home_file), /tmp (tmpfs),
	 *     FUSE Scoped Storage (/storage/*, /sdcard -> media_rw_data_file),
	 *     and public Binder services (power_service, sysinfo_service,
	 *     mount_service, mediaprovider_service, externalstorage_service)
	 *   - DENIED: fwupd_service, vold_service, system_suspend_control_service
	 *   - DENIED: raw block devices (sdx_block_device, vold_device, nvme_device,
	 *     ufs_dev, root_block_device), raw /mnt/media_rw (mnt_media_rw_file),
	 *     fuse_device, kmsg_device, pstorefs, sysfs_power, sysfs_wake_lock,
	 *     system_data_file, vendor_data_file!
	 */
	allow_sid(sid_untrusted_app, sid_shell_exec, SECCLASS_FILE,
		  RX_FILE_PERMS | SEPERM_ENTRYPOINT);
	allow_sid(sid_untrusted_app, sid_su_exec, SECCLASS_FILE, RX_FILE_PERMS);
	allow_sid(sid_untrusted_app, sid_su, SECCLASS_PROCESS,
		  SEPERM_PROC_TRANSITION | SEPERM_PROC_SIGCHLD);
	allow_sid(sid_untrusted_app, sid_binder_device, SECCLASS_CHR_FILE, RW_FILE_PERMS);
	allow_sid(sid_untrusted_app, sid_servicemanager, SECCLASS_BINDER,
		  SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_sid(sid_untrusted_app, sid_storaged, SECCLASS_BINDER,
		  SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_sid(sid_untrusted_app, sid_vold, SECCLASS_BINDER,
		  SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_sid(sid_untrusted_app, sid_mediaprovider, SECCLASS_BINDER,
		  SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_sid(sid_untrusted_app, sid_externalstoraged, SECCLASS_BINDER,
		  SEPERM_BINDER_CALL | SEPERM_BINDER_TRANSFER);
	allow_sid(sid_untrusted_app, sid_power_service, SECCLASS_SERVICE_MANAGER, SEPERM_SVCMGR_FIND);
	allow_sid(sid_untrusted_app, sid_sysinfo_service, SECCLASS_SERVICE_MANAGER, SEPERM_SVCMGR_FIND);
	allow_sid(sid_untrusted_app, sid_mount_service, SECCLASS_SERVICE_MANAGER, SEPERM_SVCMGR_FIND);
	allow_sid(sid_untrusted_app, sid_mediaprovider_service, SECCLASS_SERVICE_MANAGER, SEPERM_SVCMGR_FIND);
	allow_sid(sid_untrusted_app, sid_externalstorage_service, SECCLASS_SERVICE_MANAGER, SEPERM_SVCMGR_FIND);
	allow_sid(sid_untrusted_app, sid_user_home_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_untrusted_app, sid_user_home_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_untrusted_app, sid_app_data_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_untrusted_app, sid_app_data_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_untrusted_app, sid_media_rw_data_file, SECCLASS_DIR, RW_DIR_PERMS);
	allow_sid(sid_untrusted_app, sid_media_rw_data_file, SECCLASS_FILE, RW_FILE_PERMS);
	allow_sid(sid_untrusted_app, sid_media_rw_data_file, SECCLASS_LNK_FILE, RO_FILE_PERMS);

	/*
	 * ===================================================================
	 * 13. Compile/Load-Time Neverallow Assertions (AOSP / Android Desktop)
	 * ===================================================================
	 */
	/* Rule 1: neverallow { domain -kernel -init -su -vold -fwupd } self:capability sys_rawio; */
	sid_set_zero(&sset);
	sid_set_add_attr(&sset, ATTR_DOMAIN);
	sid_set_del(&sset, sid_kernel);
	sid_set_del(&sset, sid_init);
	sid_set_del(&sset, sid_su);
	sid_set_del(&sset, sid_vold);
	sid_set_del(&sset, sid_fwupd);
	sid_set_zero(&tset);
	add_neverallow(&sset, &tset, 1, SECCLASS_CAPABILITY,
		       (1UL << CAP_SYS_RAWIO),
		       "neverallow { domain -kernel -init -vold -fwupd } self:capability sys_rawio");

	/* Rule 2: neverallow { coredomain } vendor_file_type:file execute_no_trans; */
	sid_set_zero(&sset);
	sid_set_add_attr(&sset, ATTR_COREDOMAIN);
	sid_set_zero(&tset);
	sid_set_add_attr(&tset, ATTR_VENDOR_FILE_TYPE);
	add_neverallow(&sset, &tset, 0, SECCLASS_FILE,
		       SEPERM_EXECUTE_NO_TRANS,
		       "neverallow coredomain vendor_file_type:file execute_no_trans (system_executes_vendor_violators)");

	/* Rule 3: neverallow { vendordomain } system_data_file:file { write create unlink }; */
	sid_set_zero(&sset);
	sid_set_add_attr(&sset, ATTR_VENDORDOMAIN);
	sid_set_zero(&tset);
	sid_set_add(&tset, sid_system_data_file);
	add_neverallow(&sset, &tset, 0, SECCLASS_FILE,
		       SEPERM_WRITE | SEPERM_CREATE | SEPERM_UNLINK,
		       "neverallow vendordomain system_data_file:file { write create unlink }");

	/* Rule 4: neverallow domain data_file_type:file { execute execute_no_trans entrypoint }; (W^X) */
	sid_set_zero(&sset);
	sid_set_add_attr(&sset, ATTR_DOMAIN);
	sid_set_zero(&tset);
	sid_set_add_attr(&tset, ATTR_DATA_FILE_TYPE);
	add_neverallow(&sset, &tset, 0, SECCLASS_FILE,
		       SEPERM_EXECUTE | SEPERM_EXECUTE_NO_TRANS | SEPERM_ENTRYPOINT,
		       "neverallow domain data_file_type:file execute (W^X)");

	/* Rule 5: neverallow untrusted_app { sdx_block_device vold_device nvme_device ufs_dev root_block_device }:{ blk_file chr_file } { read write open ioctl }; */
	sid_set_zero(&sset);
	sid_set_add(&sset, sid_untrusted_app);
	sid_set_zero(&tset);
	sid_set_add(&tset, sid_sdx_block_device);
	sid_set_add(&tset, sid_vold_device);
	sid_set_add(&tset, sid_nvme_device);
	sid_set_add(&tset, sid_ufs_dev);
	sid_set_add(&tset, sid_ufs_rpmb_device);
	sid_set_add(&tset, sid_root_block_device);
	add_neverallow(&sset, &tset, 0, SECCLASS_BLK_FILE,
		       SEPERM_READ | SEPERM_WRITE | SEPERM_OPEN | SEPERM_IOCTL,
		       "neverallow untrusted_app raw_block_device:blk_file { read write open ioctl }");
	add_neverallow(&sset, &tset, 0, SECCLASS_CHR_FILE,
		       SEPERM_READ | SEPERM_WRITE | SEPERM_OPEN | SEPERM_IOCTL,
		       "neverallow untrusted_app raw_char_device:chr_file { read write open ioctl }");

	/* Rule 6: neverallow untrusted_app { fwupd_service vold_service system_suspend_control_service }:service_manager { add find }; */
	sid_set_zero(&sset);
	sid_set_add(&sset, sid_untrusted_app);
	sid_set_zero(&tset);
	sid_set_add(&tset, sid_fwupd_service);
	sid_set_add(&tset, sid_vold_service);
	sid_set_add(&tset, sid_suspend_service);
	add_neverallow(&sset, &tset, 0, SECCLASS_SERVICE_MANAGER,
		       SEPERM_SVCMGR_ADD | SEPERM_SVCMGR_FIND,
		       "neverallow untrusted_app privileged_service:service_manager { add find }");

	/* Rule 7: neverallow { domain -kernel -init -su } security:security { load_policy setenforce }; */
	sid_set_zero(&sset);
	sid_set_add_attr(&sset, ATTR_DOMAIN);
	sid_set_del(&sset, sid_kernel);
	sid_set_del(&sset, sid_init);
	sid_set_del(&sset, sid_su);
	sid_set_zero(&tset);
	sid_set_add_attr(&tset, ATTR_DOMAIN);
	sid_set_add(&tset, sid_selinuxfs);
	add_neverallow(&sset, &tset, 0, SECCLASS_SECURITY,
		       SEPERM_SEC_LOAD_POLICY | SEPERM_SEC_SETENFORCE,
		       "neverallow { domain -kernel -init -su } security:security { load_policy setenforce }");

	/* Rule 8: neverallow { domain -kernel -init -su -servicemanager } *:binder set_context_mgr; */
	sid_set_zero(&sset);
	sid_set_add_attr(&sset, ATTR_DOMAIN);
	sid_set_del(&sset, sid_kernel);
	sid_set_del(&sset, sid_init);
	sid_set_del(&sset, sid_su);
	sid_set_del(&sset, sid_servicemanager);
	sid_set_zero(&tset);
	sid_set_add_attr(&tset, ATTR_DOMAIN);
	add_neverallow(&sset, &tset, 0, SECCLASS_BINDER,
		       SEPERM_BINDER_SET_CONTEXT_MGR,
		       "neverallow { domain -init -servicemanager } *:binder set_context_mgr");

	/* Rule 9: neverallow { domain -kernel -init -su } kmsg_device:chr_file { read open }; */
	sid_set_zero(&sset);
	sid_set_add_attr(&sset, ATTR_DOMAIN);
	sid_set_del(&sset, sid_kernel);
	sid_set_del(&sset, sid_init);
	sid_set_del(&sset, sid_su);
	sid_set_zero(&tset);
	sid_set_add(&tset, sid_kmsg_device);
	add_neverallow(&sset, &tset, 0, SECCLASS_CHR_FILE,
		       SEPERM_READ | SEPERM_OPEN,
		       "neverallow { domain -kernel -init -su } kmsg_device:chr_file { read open }");

	/* Rule 10: neverallowxperm { domain -kernel -init -su -vold -fwupd } sdx_block_device:blk_file ioctl 0x2285; */
	add_xperm(sid_untrusted_app, sid_sdx_block_device, SECCLASS_BLK_FILE, SG_IO, SG_IO, 1);
	add_xperm(sid_shell, sid_sdx_block_device, SECCLASS_BLK_FILE, SG_IO, SG_IO, 1);
	add_xperm(sid_storaged, sid_sdx_block_device, SECCLASS_BLK_FILE, SG_IO, SG_IO, 1);
	add_xperm(sid_ntfs_3g, sid_sdx_block_device, SECCLASS_BLK_FILE, SG_IO, SG_IO, 1);

	/*
	 * ===================================================================
	 * 14. File Contexts (plat_file_contexts + vendor_file_contexts)
	 *     Ordered most-specific first!
	 * ===================================================================
	 */
	/* Entrypoint binaries */
	add_file_context("/etc/init", 0, sid_init_exec);
	add_file_context("/bin/init", 0, sid_init_exec);
	add_file_context("/bin/sh", 0, sid_shell_exec);
	add_file_context("/bin/su", 0, sid_su_exec);
	add_file_context("/bin/servicemanager", 0, sid_servicemanager_exec);
	add_file_context("/bin/vold", 0, sid_vold_exec);
	add_file_context("/bin/ntfs-3g", 0, sid_ntfs_3g_exec);
	add_file_context("/bin/ntfsfix", 0, sid_ntfs_3g_exec);
	add_file_context("/bin/mkntfs", 0, sid_ntfs_3g_exec);
	add_file_context("/system/bin/ntfs-3g", 0, sid_ntfs_3g_exec);
	add_file_context("/system/bin/ntfsfix", 0, sid_ntfs_3g_exec);
	add_file_context("/system/bin/mkntfs", 0, sid_ntfs_3g_exec);
	add_file_context("/bin/storaged", 0, sid_storaged_exec);
	add_file_context("/bin/mediaproviderd", 0, sid_mediaprovider_exec);
	add_file_context("/bin/externalstoraged", 0, sid_externalstoraged_exec);
	add_file_context("/bin/sadbd", 0, sid_adbd_exec);
	add_file_context("/vendor/bin/fwupd", 0, sid_fwupd_exec);
	add_file_context("/bin/httpd", 0, sid_httpd_exec);
	add_file_context("/bin/telnetd", 0, sid_telnetd_exec);

	/* Device nodes (/dev/*) */
	add_file_context("/dev/null", 0, sid_null_device);
	add_file_context("/dev/zero", 0, sid_zero_device);
	add_file_context("/dev/random", 0, sid_random_device);
	add_file_context("/dev/urandom", 0, sid_random_device);
	add_file_context("/dev/console", 0, sid_console_device);
	add_file_context("/dev/tty", 1, sid_tty_device);
	add_file_context("/dev/ptmx", 0, sid_tty_device);
	add_file_context("/dev/pts", 1, sid_devpts);
	add_file_context("/dev/kmsg", 0, sid_kmsg_device);
	add_file_context("/dev/mem", 0, sid_kmem_device);
	add_file_context("/dev/kmem", 0, sid_kmem_device);
	add_file_context("/dev/port", 0, sid_kmem_device);
	add_file_context("/dev/binder", 0, sid_binder_device);
	add_file_context("/dev/fuse", 0, sid_fuse_device);
	add_file_context("/dev/sadb", 0, sid_adb_device);
	add_file_context("/dev/suspend_blocker", 0, sid_power_device);
	add_file_context("/dev/hda1", 0, sid_root_block_device);
	add_file_context("/dev/hda2", 0, sid_swap_block_device);
	add_file_context("/dev/hda", 0, sid_root_block_device);
	add_file_context("/dev/hdb", 1, sid_vold_device);
	add_file_context("/dev/hdc", 1, sid_vold_device);
	add_file_context("/dev/hdd", 1, sid_vold_device);
	add_file_context("/dev/sda", 1, sid_vold_device);
	add_file_context("/dev/ufs-bsg", 1, sid_ufs_dev);
	add_file_context("/dev/ufs-rpmb", 1, sid_ufs_rpmb_device);
	add_file_context("/dev/ufs", 1, sid_sdx_block_device);
	add_file_context("/dev/nvme", 1, sid_nvme_device);
	add_file_context("/dev", 1, sid_device);

	/* Sysfs & Selinuxfs & Pstorefs (/sys/*) */
	add_file_context("/sys/fs/selinux", 1, sid_selinuxfs);
	add_file_context("/sys/fs/pstore", 1, sid_pstorefs);
	add_file_context("/sys/power/wake_lock", 0, sid_sysfs_wake_lock);
	add_file_context("/sys/power/wake_unlock", 0, sid_sysfs_wake_lock);
	add_file_context("/sys/power", 1, sid_sysfs_power);
	add_file_context("/sys", 1, sid_sysfs);

	/* Procfs (/proc/*) */
	add_file_context("/proc/meminfo", 0, sid_proc_meminfo);
	add_file_context("/proc/stat", 0, sid_proc_stat);
	add_file_context("/proc/uptime", 0, sid_proc_stat);
	add_file_context("/proc/loadavg", 0, sid_proc_stat);
	add_file_context("/proc/version", 0, sid_proc_version);
	add_file_context("/proc/cmdline", 0, sid_proc_cmdline);
	add_file_context("/proc/nvme", 0, sid_proc_diskstats);
	add_file_context("/proc/ufs", 0, sid_proc_diskstats);
	add_file_context("/proc/scsi", 0, sid_proc_diskstats);
	add_file_context("/proc/sysrq-trigger", 0, sid_proc_sysrq);
	add_file_context("/proc/kmsg", 0, sid_kmsg_device);
	add_file_context("/proc/ftrace", 0, sid_debugfs_tracing);
	add_file_context("/proc/locks", 0, sid_debugfs_tracing);
	add_file_context("/proc/timers", 0, sid_debugfs_tracing);
	add_file_context("/proc/net", 1, sid_proc_net);
	add_file_context("/proc", 1, sid_proc);

	/* SELinux policy directories */
	add_file_context("/etc/selinux", 1, sid_sepolicy_file);
	add_file_context("/system/etc/selinux", 1, sid_sepolicy_file);
	add_file_context("/vendor/etc/selinux", 1, sid_vendor_configs_file);
	add_file_context("/etc/fwupd", 1, sid_vendor_data_file);
	add_file_context("/var/lib/fwupd", 1, sid_vendor_data_file);
	add_file_context("/var/db", 1, sid_media_rw_data_file);

	/* Vendor, Data, Media, Home, System directories */
	add_file_context("/vendor/bin", 1, sid_vendor_file);
	add_file_context("/vendor/etc", 1, sid_vendor_configs_file);
	add_file_context("/vendor", 1, sid_vendor_file);
	add_file_context("/data/vendor", 1, sid_vendor_data_file);
	add_file_context("/data/media", 1, sid_media_rw_data_file);
	add_file_context("/data/app", 1, sid_app_data_file);
	add_file_context("/data", 1, sid_system_data_file);
	add_file_context("/mnt/media_rw", 1, sid_mnt_media_rw_file);
	add_file_context("/mnt/runtime", 1, sid_mnt_media_rw_file);
	add_file_context("/mnt/user", 1, sid_mnt_media_rw_file);
	add_file_context("/mnt/fuse", 1, sid_mnt_media_rw_file);
	add_file_context("/mnt", 1, sid_mnt_media_rw_stub_file);
	add_file_context("/aux", 1, sid_mnt_media_rw_file);
	add_file_context("/storage", 1, sid_media_rw_data_file);
	add_file_context("/sdcard", 1, sid_media_rw_data_file);
	add_file_context("/tmp", 1, sid_tmpfs);
	add_file_context("/home", 1, sid_user_home_file);
	add_file_context("/bin", 1, sid_system_file);
	add_file_context("/system", 1, sid_system_file);
	add_file_context("/etc", 1, sid_system_file);
	add_file_context("/usr", 1, sid_system_file);
	add_file_context("/", 0, sid_rootfs);

	/*
	 * ===================================================================
	 * 15. ServiceManager Contexts (service_contexts)
	 * ===================================================================
	 */
	add_service_context("power", sid_power_service);
	add_service_context("sysinfo", sid_sysinfo_service);
	add_service_context("vold", sid_vold_service);
	add_service_context("mount", sid_mount_service);
	add_service_context("suspend", sid_suspend_service);
	add_service_context("media.provider", sid_mediaprovider_service);
	add_service_context("externalstorage", sid_externalstorage_service);
	add_service_context("fwupd", sid_fwupd_service);
}

/* Match a canonical path against file_contexts */
static unsigned short selinux_match_path_sid(const char *path)
{
	int i;
	if (!path || !path[0])
		return sid_unlabeled;

	/* Special case for /proc/<pid> */
	if (!strncmp(path, "/proc/", 6)) {
		const char *p = path + 6;
		if (*p >= '0' && *p <= '9') {
			int pid = 0;
			int k;
			while (*p >= '0' && *p <= '9') {
				pid = pid * 10 + (*p - '0');
				p++;
			}
			for (k = 0; k < NR_TASKS; k++) {
				if (task[k] && task[k]->pid == pid)
					return task[k]->sec_sid ? task[k]->sec_sid : sid_kernel;
			}
		} else if (!strncmp(p, "self", 4) && (p[4] == '\0' || p[4] == '/')) {
			if (current && current->sec_sid)
				return current->sec_sid;
		}
	}

	for (i = 0; i < nr_file_contexts; i++) {
		const char *pat = file_contexts[i].pattern;
		int plen = strlen(pat);
		if (file_contexts[i].match_type == 0) {
			if (!strcmp(path, pat))
				return file_contexts[i].sid;
		} else {
			if (!strncmp(path, pat, plen)) {
				if (path[plen] == '\0' || path[plen] == '/' ||
				    !strncmp(pat, "/dev/", 5))
					return file_contexts[i].sid;
			}
		}
	}
	return sid_system_file;
}

static struct selinux_inode_sec *find_inode_sec(unsigned short dev, unsigned long ino, int alloc)
{
	int idx = (int)(((unsigned long)dev * 131UL + ino) & (MAX_INODE_SEC - 1));
	int i;

	for (i = 0; i < 64; i++) {
		int slot = (idx + i) & (MAX_INODE_SEC - 1);
		if (inode_sec_table[slot].ino == ino &&
		    inode_sec_table[slot].dev == dev &&
		    inode_sec_table[slot].sid != 0)
			return &inode_sec_table[slot];
	}
	if (!alloc)
		return NULL;
	for (i = 0; i < 64; i++) {
		int slot = (idx + i) & (MAX_INODE_SEC - 1);
		if (inode_sec_table[slot].sid == 0 ||
		    (inode_sec_table[slot].ino == ino && inode_sec_table[slot].dev == dev)) {
			inode_sec_table[slot].dev = dev;
			inode_sec_table[slot].ino = ino;
			return &inode_sec_table[slot];
		}
	}
	return NULL;
}

/* Determine object class from inode mode */
static int inode_to_secclass(struct inode *inode)
{
	if (!inode)
		return SECCLASS_FILE;
	if (S_ISDIR(inode->i_mode))
		return SECCLASS_DIR;
	if (S_ISCHR(inode->i_mode))
		return SECCLASS_CHR_FILE;
	if (S_ISBLK(inode->i_mode))
		return SECCLASS_BLK_FILE;
	if (S_ISLNK(inode->i_mode))
		return SECCLASS_LNK_FILE;
	if (S_ISFIFO(inode->i_mode))
		return SECCLASS_FIFO_FILE;
	if (S_ISSOCK(inode->i_mode))
		return SECCLASS_SOCK_FILE;
	return SECCLASS_FILE;
}

/* Resolve SID for an inode using cached SID, device major/minor, or superblock magic */
unsigned short selinux_inode_sid(struct inode *inode)
{
	struct selinux_inode_sec *isec;
	unsigned long magic;

	if (!inode)
		return sid_unlabeled;

	isec = find_inode_sec((unsigned short)inode->i_dev, inode->i_ino, 0);
	if (isec && isec->explicit_label && isec->sid != 0) {
		inode->i_sec_sid = isec->sid;
		return isec->sid;
	}

	/* Pin root directories of mounted filesystems */
	if (S_ISDIR(inode->i_mode) && inode->i_ino == 2) {
		if (MAJOR(inode->i_dev) == NVME_MAJOR || MAJOR(inode->i_dev) == UFS_MAJOR)
			return (inode->i_sec_sid = sid_system_data_file);
		if (MAJOR(inode->i_dev) == IDE0_MAJOR && MINOR(inode->i_dev) == 1)
			return (inode->i_sec_sid = sid_rootfs);
	}

	if (isec && isec->sid != 0) {
		inode->i_sec_sid = isec->sid;
		return isec->sid;
	}
	if (inode->i_sec_sid != 0)
		return inode->i_sec_sid;

	/* Character & Block Device fallback by major/minor */
	if (S_ISCHR(inode->i_mode)) {
		int maj = MAJOR(inode->i_rdev);
		int min = MINOR(inode->i_rdev);
		switch (maj) {
		case MEM_MAJOR:
			if (min == 3 || min == 7) return (inode->i_sec_sid = sid_null_device);
			if (min == 5) return (inode->i_sec_sid = sid_zero_device);
			if (min == 8 || min == 9) return (inode->i_sec_sid = sid_random_device);
			if (min == 11) return (inode->i_sec_sid = sid_kmsg_device);
			return (inode->i_sec_sid = sid_kmem_device);
		case PTY_MASTER_MAJOR:
		case PTY_SLAVE_MAJOR:
		case TTY_MAJOR:
		case TTYAUX_MAJOR:
			if (maj == TTYAUX_MAJOR && min == 1)
				return (inode->i_sec_sid = sid_console_device);
			return (inode->i_sec_sid = sid_tty_device);
		case MISC_MAJOR:
			if (min == 229)
				return (inode->i_sec_sid = sid_fuse_device);
			return (inode->i_sec_sid = sid_device);
		case SELINUX_MAJOR:
			return (inode->i_sec_sid = sid_selinuxfs);
		case UFS_CHR_MAJOR:
			if (min == 1)
				return (inode->i_sec_sid = sid_ufs_rpmb_device);
			return (inode->i_sec_sid = sid_ufs_dev);
		case NVME_CHR_MAJOR:
			return (inode->i_sec_sid = sid_nvme_device);
		case 60: /* /sys/power/* and /dev/power */
			if (min == 1 || min == 2)
				return (inode->i_sec_sid = sid_sysfs_wake_lock);
			if (min == 16)
				return (inode->i_sec_sid = sid_power_device);
			return (inode->i_sec_sid = sid_sysfs_power);
		case 61: /* /dev/sadb */
			return (inode->i_sec_sid = sid_adb_device);
		case 63: /* /dev/binder */
			return (inode->i_sec_sid = sid_binder_device);
		default:
			return (inode->i_sec_sid = sid_device);
		}
	}
	if (S_ISBLK(inode->i_mode)) {
		int maj = MAJOR(inode->i_rdev);
		int min = MINOR(inode->i_rdev);
		switch (maj) {
		case IDE0_MAJOR:
			if (min >= 128)
				return (inode->i_sec_sid = sid_vold_device);
			if (min == 64)
				return (inode->i_sec_sid = sid_swap_block_device);
			return (inode->i_sec_sid = sid_root_block_device);
		case SCSI_DISK_MAJOR:
		case DM_MAJOR:
			return (inode->i_sec_sid = sid_vold_device);
		case NVME_MAJOR:
			return (inode->i_sec_sid = sid_nvme_device);
		case UFS_MAJOR:
			return (inode->i_sec_sid = sid_sdx_block_device);
		default:
			return (inode->i_sec_sid = sid_block_device);
		}
	}

	/* Superblock magic fallback (procfs, pstorefs, fuse) */
	magic = inode->i_sb ? inode->i_sb->s_magic : 0;
	if (magic == 0x9fa0UL) { /* PROC_SUPER_MAGIC */
		unsigned long ino = inode->i_ino;
		if (ino >= 0x10000UL) {
			unsigned int pid = (unsigned int)(ino >> 16);
			int k;
			for (k = 0; k < NR_TASKS; k++) {
				if (task[k] && task[k]->pid == (int)pid)
					return (inode->i_sec_sid = (task[k]->sec_sid ? task[k]->sec_sid : sid_kernel));
			}
		}
		if (ino == 4) return (inode->i_sec_sid = sid_proc_meminfo);
		if (ino == 2 || ino == 3 || ino == 10) return (inode->i_sec_sid = sid_proc_stat);
		if (ino == 6) return (inode->i_sec_sid = sid_proc_version);
		if (ino == 59) return (inode->i_sec_sid = sid_proc_cmdline);
		if (ino == 60) return (inode->i_sec_sid = sid_proc_sysrq);
		if (ino == 9) return (inode->i_sec_sid = sid_kmsg_device);
		if (ino >= 61 && ino <= 63) return (inode->i_sec_sid = sid_debugfs_tracing);
		if (ino >= 66 && ino <= 68) return (inode->i_sec_sid = sid_proc_diskstats);
		if (ino >= 128 && ino < 256) return (inode->i_sec_sid = sid_proc_net);
		return (inode->i_sec_sid = sid_proc);
	}
	if (magic == 0x6165676cUL) { /* PSTOREFS_MAGIC */
		return (inode->i_sec_sid = sid_pstorefs);
	}
	if (magic == 0x65735546UL) { /* FUSE_SUPER_MAGIC */
		if (inode->i_sb && inode->i_sb->s_covered &&
		    inode->i_sb->s_covered->i_sec_sid == sid_media_rw_data_file)
			return (inode->i_sec_sid = sid_media_rw_data_file);
		return (inode->i_sec_sid = sid_ntfs);
	}
	if (MAJOR(inode->i_dev) == NVME_MAJOR) {
		return (inode->i_sec_sid = sid_system_data_file);
	}
	if (inode->i_ino == 2 && S_ISDIR(inode->i_mode)) {
		return (inode->i_sec_sid = sid_rootfs);
	}
	return (inode->i_sec_sid = sid_system_file);
}

/* Instantiate canonical path and label when lookup() resolves a child inode */
void selinux_d_instantiate(unsigned short dir_dev, unsigned long dir_ino,
			   unsigned short dir_sid, const char *name, int len,
			   struct inode *result)
{
	struct selinux_inode_sec *dir_sec, *res_sec;
	char full_path[96];
	int plen = 0;
	unsigned short sid;

	if (!result || !name || len <= 0)
		return;
	if ((len == 1 && name[0] == '.') ||
	    (len == 2 && name[0] == '.' && name[1] == '.'))
		return;

	res_sec = find_inode_sec((unsigned short)result->i_dev, result->i_ino, 1);
	if (res_sec && res_sec->explicit_label && res_sec->sid != 0) {
		result->i_sec_sid = res_sec->sid;
		return;
	}

	if (S_ISDIR(result->i_mode) && result->i_ino == 2) {
		if (MAJOR(result->i_dev) == NVME_MAJOR) {
			result->i_sec_sid = sid_system_data_file;
			if (res_sec) {
				res_sec->sid = sid_system_data_file;
				strcpy(res_sec->path, "/data");
			}
			return;
		}
		if (MAJOR(result->i_dev) == UFS_MAJOR) {
			result->i_sec_sid = sid_system_data_file;
			if (res_sec) {
				res_sec->sid = sid_system_data_file;
				strcpy(res_sec->path, "/ufs");
			}
			return;
		}
		if (MAJOR(result->i_dev) == IDE0_MAJOR && MINOR(result->i_dev) == 1) {
			result->i_sec_sid = sid_rootfs;
			if (res_sec) {
				res_sec->sid = sid_rootfs;
				strcpy(res_sec->path, "/");
			}
			return;
		}
	}

	dir_sec = find_inode_sec(dir_dev, dir_ino, 0);
	if (dir_sec && dir_sec->path[0]) {
		strncpy(full_path, dir_sec->path, sizeof(full_path) - 1);
		full_path[sizeof(full_path) - 1] = '\0';
		plen = strlen(full_path);
	} else if (MAJOR(dir_dev) == NVME_MAJOR && dir_ino == 2) {
		strcpy(full_path, "/data");
		plen = 5;
	} else if (MAJOR(dir_dev) == UFS_MAJOR && dir_ino == 2) {
		strcpy(full_path, "/ufs");
		plen = 4;
	} else if (result->i_sb && result->i_sb->s_magic == 0x794c7630UL) {
		strcpy(full_path, "/bin");
		plen = 4;
	} else if (dir_ino == 2) {
		strcpy(full_path, "/");
		plen = 1;
	} else if (current && current->fs && current->fs->pwd &&
		   current->fs->pwd->i_ino == 2 &&
		   MAJOR(current->fs->pwd->i_dev) != NVME_MAJOR) {
		strcpy(full_path, "/");
		plen = 1;
	}

	if (plen > 0) {
		if (plen > 1 && full_path[plen - 1] != '/' &&
		    plen + 1 < (int)sizeof(full_path)) {
			full_path[plen++] = '/';
			full_path[plen] = '\0';
		}
		if (plen + len < (int)sizeof(full_path)) {
			memcpy(full_path + plen, name, len);
			full_path[plen + len] = '\0';
		}
		sid = selinux_match_path_sid(full_path);
	} else {
		full_path[0] = '\0';
		sid = (dir_sid && dir_sid != sid_rootfs) ? dir_sid : selinux_inode_sid(result);
	}

	/* Device/procfs/pstorefs/fuse nodes use their exact device/magic SID */
	if (S_ISCHR(result->i_mode) || S_ISBLK(result->i_mode) ||
	    (result->i_sb && (result->i_sb->s_magic == 0x9fa0UL ||
			      result->i_sb->s_magic == 0x6165676cUL ||
			      result->i_sb->s_magic == 0x65735546UL))) {
		if (res_sec)
			res_sec->sid = 0;
		result->i_sec_sid = 0;
		sid = selinux_inode_sid(result);
	}

	result->i_sec_sid = sid;
	if (res_sec) {
		res_sec->sid = sid;
		if (full_path[0]) {
			strncpy(res_sec->path, full_path, sizeof(res_sec->path) - 1);
			res_sec->path[sizeof(res_sec->path) - 1] = '\0';
		}
	}
}

void selinux_label_inode_path(struct inode *inode, const char *pathname)
{
	struct selinux_inode_sec *isec;
	unsigned short sid;

	if (!inode || !pathname || !pathname[0])
		return;
	isec = find_inode_sec((unsigned short)inode->i_dev, inode->i_ino, 1);
	if (isec && isec->explicit_label && isec->sid != 0) {
		inode->i_sec_sid = isec->sid;
		return;
	}
	if (pathname[0] == '/') {
		sid = selinux_match_path_sid(pathname);
		if (S_ISCHR(inode->i_mode) || S_ISBLK(inode->i_mode) ||
		    (inode->i_sb && inode->i_sb->s_magic == 0x65735546UL)) {
			if (isec)
				isec->sid = 0;
			inode->i_sec_sid = 0;
			sid = selinux_inode_sid(inode);
		}
		inode->i_sec_sid = sid;
		if (isec) {
			isec->sid = sid;
			strncpy(isec->path, pathname, sizeof(isec->path) - 1);
			isec->path[sizeof(isec->path) - 1] = '\0';
		}
	} else {
		selinux_inode_sid(inode);
	}
}

void selinux_label_new_inode(struct inode *dir, struct inode *inode, const char *name)
{
	unsigned short dsid;
	if (!inode)
		return;
	dsid = dir ? selinux_inode_sid(dir) : sid_system_data_file;
	if (dir && name) {
		selinux_d_instantiate((unsigned short)dir->i_dev, dir->i_ino,
				      dsid, name, strlen(name), inode);
	} else {
		inode->i_sec_sid = dsid;
	}
}

/* Format and record an AVC denial in dmesg and the AVC ring buffer */
static void selinux_log_avc_denial(unsigned short ssid, unsigned short tsid,
				   int tclass, unsigned long denied_perm,
				   const char *audit_path, struct inode *audit_inode,
				   int ioctlcmd, int cap_nr)
{
	char msg[224];
	int pos = 0;
	const char *pname = perm_to_name(tclass, denied_perm);
	const char *sctx = selinux_sid_to_context(ssid);
	const char *tctx = selinux_sid_to_context(tsid);
	const char *cname = secclass_to_name(tclass);
	int pid = current ? current->pid : 0;
	const char *comm = (current && current->comm[0]) ? current->comm : "kernel";

	avc_denials++;

	pos += sprintf(msg + pos,
		       "audit: type=1400 avc:  denied  { %s } for  pid=%d comm=\"%s\"",
		       pname, pid, comm);
	if (cap_nr >= 0) {
		pos += sprintf(msg + pos, " capability=%d", cap_nr);
	}
	if (audit_path && audit_path[0]) {
		pos += sprintf(msg + pos, " path=\"%s\"", audit_path);
	} else if (audit_inode) {
		struct selinux_inode_sec *isec =
			find_inode_sec((unsigned short)audit_inode->i_dev, audit_inode->i_ino, 0);
		if (isec && isec->path[0])
			pos += sprintf(msg + pos, " path=\"%s\"", isec->path);
	}
	if (audit_inode) {
		pos += sprintf(msg + pos, " ino=%lu", audit_inode->i_ino);
	}
	if (ioctlcmd >= 0) {
		pos += sprintf(msg + pos, " ioctlcmd=0x%x", (unsigned int)ioctlcmd);
	}
	pos += sprintf(msg + pos, " scontext=%s tcontext=%s tclass=%s permissive=%d",
		       sctx, tctx, cname, selinux_enforcing ? 0 : 1);

	strncpy(avc_ring[avc_ring_head & (AVC_LOG_RING_SIZE - 1)].line, msg,
		sizeof(avc_ring[0].line) - 1);
	avc_ring[avc_ring_head & (AVC_LOG_RING_SIZE - 1)].line[sizeof(avc_ring[0].line) - 1] = '\0';
	avc_ring_head++;
	if (avc_ring_count < AVC_LOG_RING_SIZE)
		avc_ring_count++;

	printk(KERN_WARNING "%s\n", msg);
}

int selinux_check_access(unsigned short ssid, unsigned short tsid,
			 int tclass, unsigned long perms,
			 const char *audit_path, struct inode *audit_inode,
			 int ioctlcmd, int cap_nr)
{
	unsigned long allowed;
	unsigned long missing;

	if (!selinux_initialized)
		return 0;
	if (!ssid)
		ssid = (current && current->sec_sid) ? current->sec_sid : sid_kernel;
	if (!tsid)
		tsid = sid_unlabeled;
	if (ssid >= MAX_SIDS || tsid >= MAX_SIDS ||
	    tclass <= SECCLASS_NONE || tclass >= SECCLASS_MAX)
		return 0;

	avc_lookups++;
	allowed = av_allow[ssid][tsid][tclass];
	missing = perms & ~allowed;
	if (!missing) {
		avc_hits++;
		return 0;
	}

	selinux_log_avc_denial(ssid, tsid, tclass, missing,
			       audit_path, audit_inode, ioctlcmd, cap_nr);
	return selinux_enforcing ? -EACCES : 0;
}

int selinux_check_xperm(unsigned short ssid, unsigned short tsid,
			int tclass, unsigned int ioctlcmd,
			const char *audit_path, struct inode *audit_inode)
{
	int i;
	unsigned short cmd16 = (unsigned short)(ioctlcmd & 0xffff);
	int need_xperm = 0;

	if (!selinux_initialized)
		return 0;
	if (!ssid)
		ssid = (current && current->sec_sid) ? current->sec_sid : sid_kernel;
	if (ssid == sid_kernel || ssid == sid_init || ssid == sid_su)
		return 0;

	/* Check if extended permission filtering applies to this target/ioctl */
	if (tsid < MAX_SIDS && tclass < SECCLASS_MAX && xperm_active[tsid][tclass])
		need_xperm = 1;
	if (cmd16 == SG_IO ||
	    (cmd16 >= 0x5540 && cmd16 <= 0x5543) ||
	    (cmd16 >= 0x4e40 && cmd16 <= 0x4e43) ||
	    (cmd16 >= 0x5382 && cmd16 <= 0x5386))
		need_xperm = 1;

	if (!need_xperm)
		return 0;

	for (i = 0; i < nr_xperm_rules; i++) {
		if (xperm_rules[i].is_neverallow)
			continue;
		if (xperm_rules[i].ssid == ssid &&
		    xperm_rules[i].tsid == tsid &&
		    xperm_rules[i].tclass == tclass &&
		    cmd16 >= xperm_rules[i].lo &&
		    cmd16 <= xperm_rules[i].hi) {
			return 0;
		}
	}

	selinux_log_avc_denial(ssid, tsid, tclass, SEPERM_IOCTL,
			       audit_path, audit_inode, (int)cmd16, -1);
	return selinux_enforcing ? -EACCES : 0;
}

int selinux_capable(int cap)
{
	unsigned short ssid;
	int rc;

	if (!selinux_initialized || !current)
		return 0;
	if (!current->user_mode && current->pid == 0)
		return 0;
	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	rc = selinux_check_access(ssid, ssid, SECCLASS_CAPABILITY,
				  (1UL << cap), NULL, NULL, -1, cap);
	return rc ? -EPERM : 0;
}

int suser_cap(int cap)
{
	if (!current || current->euid != 0)
		return 0;
	if (selinux_capable(cap) != 0)
		return 0;
	current->flags |= PF_SUPERPRIV;
	return 1;
}

/* VFS & Process Enforcement Hooks */
int selinux_inode_permission(struct inode *inode, int mask)
{
	unsigned short ssid, tsid;
	int tclass;
	unsigned long perms = 0;

	if (!selinux_initialized || !inode || !current)
		return 0;
	if (!current->user_mode && current->pid <= 4)
		return 0;

	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	tsid = selinux_inode_sid(inode);
	tclass = inode_to_secclass(inode);

	if (mask & MAY_READ)
		perms |= SEPERM_READ;
	if (mask & MAY_WRITE)
		perms |= SEPERM_WRITE;
	if (mask & MAY_EXEC)
		perms |= (tclass == SECCLASS_DIR) ? SEPERM_SEARCH : SEPERM_EXECUTE;
	if (!perms)
		perms = SEPERM_GETATTR;

	return selinux_check_access(ssid, tsid, tclass, perms, NULL, inode, -1, -1);
}

int selinux_file_open(struct inode *inode, int flag, const char *pathname)
{
	unsigned short ssid, tsid;
	int tclass;
	unsigned long perms = SEPERM_OPEN;

	if (!selinux_initialized || !inode || !current)
		return 0;
	if (pathname && pathname[0])
		selinux_label_inode_path(inode, pathname);
	if (!current->user_mode && current->pid <= 4)
		return 0;
	/* flag == 0 is do_execve(), which is checked by selinux_bprm_transition */
	if ((flag & 3) == 0)
		return 0;

	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	tsid = selinux_inode_sid(inode);
	tclass = inode_to_secclass(inode);

	if ((flag & 3) == 1)
		perms |= SEPERM_READ;
	else if ((flag & 3) == 2)
		perms |= (flag & O_APPEND) ? SEPERM_APPEND : SEPERM_WRITE;
	else
		perms |= SEPERM_READ | SEPERM_WRITE;

	return selinux_check_access(ssid, tsid, tclass, perms, pathname, inode, -1, -1);
}

int selinux_file_io(struct file *filp, int is_write)
{
	struct inode *inode;
	unsigned short ssid, tsid;
	int tclass;
	unsigned long perms;

	if (!selinux_initialized || !filp || !(inode = filp->f_inode) || !current)
		return 0;
	if (!current->user_mode && current->pid <= 4)
		return 0;
	/* Skip per-byte re-checks on regular TTY/pipe/socket stdio to keep console fast */
	if (S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode))
		return 0;
	if (S_ISCHR(inode->i_mode)) {
		int maj = MAJOR(inode->i_rdev);
		if (maj == TTY_MAJOR || maj == TTYAUX_MAJOR || maj == SELINUX_MAJOR)
			return 0;
	}

	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	tsid = selinux_inode_sid(inode);
	tclass = inode_to_secclass(inode);
	perms = is_write ? SEPERM_WRITE : SEPERM_READ;

	return selinux_check_access(ssid, tsid, tclass, perms, NULL, inode, -1, -1);
}

int selinux_file_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct inode *inode;
	unsigned short ssid, tsid;
	int tclass, rc;

	(void)arg;
	if (!selinux_initialized || !filp || !(inode = filp->f_inode) || !current)
		return 0;
	if (!current->user_mode && current->pid <= 4)
		return 0;
	/* Standard terminal ioctls (TIOC*) on TTYs */
	if (S_ISCHR(inode->i_mode) &&
	    (MAJOR(inode->i_rdev) == TTY_MAJOR || MAJOR(inode->i_rdev) == TTYAUX_MAJOR))
		return 0;

	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	tsid = selinux_inode_sid(inode);
	tclass = inode_to_secclass(inode);

	rc = selinux_check_access(ssid, tsid, tclass, SEPERM_IOCTL,
				  NULL, inode, (int)(cmd & 0xffff), -1);
	if (rc)
		return rc;

	return selinux_check_xperm(ssid, tsid, tclass, cmd, NULL, inode);
}

int selinux_inode_create(struct inode *dir, const char *name, int mode)
{
	unsigned short ssid, dsid;
	int rc;

	(void)mode;
	if (!selinux_initialized || !dir || !current)
		return 0;
	if (!current->user_mode && current->pid <= 4)
		return 0;
	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	dsid = selinux_inode_sid(dir);
	rc = selinux_check_access(ssid, dsid, SECCLASS_DIR,
				  SEPERM_WRITE | SEPERM_SEARCH | SEPERM_ADD_NAME,
				  name, dir, -1, -1);
	if (rc)
		return rc;
	return selinux_check_access(ssid, dsid, SECCLASS_FILE,
				    SEPERM_CREATE | SEPERM_WRITE, name, dir, -1, -1);
}

int selinux_inode_unlink(struct inode *dir, struct inode *victim, const char *name)
{
	unsigned short ssid, dsid, vsid;
	int rc;

	if (!selinux_initialized || !dir || !current)
		return 0;
	if (!current->user_mode && current->pid <= 4)
		return 0;
	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	dsid = selinux_inode_sid(dir);
	rc = selinux_check_access(ssid, dsid, SECCLASS_DIR,
				  SEPERM_WRITE | SEPERM_SEARCH | SEPERM_REMOVE_NAME,
				  name, dir, -1, -1);
	if (rc || !victim)
		return rc;
	vsid = selinux_inode_sid(victim);
	return selinux_check_access(ssid, vsid, inode_to_secclass(victim),
				    SEPERM_UNLINK, name, victim, -1, -1);
}

int selinux_inode_mkdir(struct inode *dir, const char *name, int mode)
{
	unsigned short ssid, dsid;
	(void)mode;
	if (!selinux_initialized || !dir || !current)
		return 0;
	if (!current->user_mode && current->pid <= 4)
		return 0;
	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	dsid = selinux_inode_sid(dir);
	return selinux_check_access(ssid, dsid, SECCLASS_DIR,
				    SEPERM_WRITE | SEPERM_SEARCH | SEPERM_ADD_NAME | SEPERM_CREATE,
				    name, dir, -1, -1);
}

int selinux_inode_rmdir(struct inode *dir, struct inode *victim, const char *name)
{
	unsigned short ssid, dsid, vsid;
	int rc;

	if (!selinux_initialized || !dir || !current)
		return 0;
	if (!current->user_mode && current->pid <= 4)
		return 0;
	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	dsid = selinux_inode_sid(dir);
	rc = selinux_check_access(ssid, dsid, SECCLASS_DIR,
				  SEPERM_WRITE | SEPERM_SEARCH | SEPERM_REMOVE_NAME,
				  name, dir, -1, -1);
	if (rc || !victim)
		return rc;
	vsid = selinux_inode_sid(victim);
	return selinux_check_access(ssid, vsid, SECCLASS_DIR,
				    SEPERM_RMDIR, name, victim, -1, -1);
}

int selinux_inode_rename(struct inode *old_dir, struct inode *old_inode,
			 struct inode *new_dir, const char *new_name)
{
	unsigned short ssid, odsid, ndsid;
	int rc;

	if (!selinux_initialized || !old_dir || !new_dir || !current)
		return 0;
	if (!current->user_mode && current->pid <= 4)
		return 0;
	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	odsid = selinux_inode_sid(old_dir);
	ndsid = selinux_inode_sid(new_dir);
	rc = selinux_check_access(ssid, odsid, SECCLASS_DIR,
				  SEPERM_WRITE | SEPERM_REMOVE_NAME, new_name, old_dir, -1, -1);
	if (rc)
		return rc;
	rc = selinux_check_access(ssid, ndsid, SECCLASS_DIR,
				  SEPERM_WRITE | SEPERM_ADD_NAME, new_name, new_dir, -1, -1);
	if (rc || !old_inode)
		return rc;
	return selinux_check_access(ssid, selinux_inode_sid(old_inode),
				    inode_to_secclass(old_inode),
				    SEPERM_RENAME, new_name, old_inode, -1, -1);
}

int selinux_inode_setattr(struct inode *inode)
{
	unsigned short ssid, tsid;
	if (!selinux_initialized || !inode || !current)
		return 0;
	if (!current->user_mode && current->pid <= 4)
		return 0;
	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	tsid = selinux_inode_sid(inode);
	return selinux_check_access(ssid, tsid, inode_to_secclass(inode),
				    SEPERM_SETATTR, NULL, inode, -1, -1);
}

int selinux_sb_mount(const char *dev_name, struct inode *dir_inode, const char *type)
{
	unsigned short ssid, dsid;
	int rc;

	(void)dev_name;
	(void)type;
	if (!selinux_initialized || !dir_inode || !current)
		return 0;
	if (!current->user_mode && current->pid <= 4)
		return 0;
	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	dsid = selinux_inode_sid(dir_inode);
	rc = selinux_check_access(ssid, ssid, SECCLASS_CAPABILITY,
				  (1UL << CAP_SYS_ADMIN), NULL, dir_inode, -1, CAP_SYS_ADMIN);
	if (rc)
		return -EPERM;
	return selinux_check_access(ssid, dsid, SECCLASS_DIR,
				    SEPERM_MOUNT | SEPERM_SEARCH, NULL, dir_inode, -1, -1);
}

int selinux_sb_umount(struct inode *mounted_inode)
{
	unsigned short ssid, msid;
	int rc;

	if (!selinux_initialized || !mounted_inode || !current)
		return 0;
	if (!current->user_mode && current->pid <= 4)
		return 0;
	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	msid = selinux_inode_sid(mounted_inode);
	rc = selinux_check_access(ssid, ssid, SECCLASS_CAPABILITY,
				  (1UL << CAP_SYS_ADMIN), NULL, mounted_inode, -1, CAP_SYS_ADMIN);
	if (rc)
		return -EPERM;
	return selinux_check_access(ssid, msid, SECCLASS_DIR,
				    SEPERM_UNMOUNT, NULL, mounted_inode, -1, -1);
}

void selinux_task_fork(struct task_struct *parent, struct task_struct *child, int is_kthread)
{
	if (!child)
		return;
	if (is_kthread || !parent || !parent->sec_sid) {
		child->sec_sid = sid_kernel ? sid_kernel : 1;
		child->sec_exec_sid = 0;
	} else {
		child->sec_sid = parent->sec_sid;
		child->sec_exec_sid = parent->sec_exec_sid;
	}
}

/*
 * Domain transition and execute_no_trans enforcement on execve()
 */
int selinux_bprm_transition(struct inode *exec_inode, const char *filename)
{
	unsigned short old_sid, tsid, new_sid = 0;
	int i, rc;

	if (!selinux_initialized || !current || !exec_inode)
		return 0;

	if (filename && filename[0])
		selinux_label_inode_path(exec_inode, filename);

	old_sid = current->sec_sid ? current->sec_sid : sid_kernel;
	tsid = selinux_inode_sid(exec_inode);

	/* Also check explicit entrypoint paths for /bin/ntfs-3g, /vendor/bin/fwupd, etc. */
	if (filename && filename[0] == '/') {
		unsigned short psid = selinux_match_path_sid(filename);
		if (psid && (type_table[psid].attrs & ATTR_EXEC_TYPE))
			tsid = psid;
	}

	/* 1. Check basic read/open/execute on the binary */
	rc = selinux_check_access(old_sid, tsid, SECCLASS_FILE,
				  SEPERM_READ | SEPERM_OPEN | SEPERM_EXECUTE,
				  filename, exec_inode, -1, -1);
	if (rc)
		return rc;

	/* 2. Determine target domain: explicit setexeccon or automatic type_transition */
	if (current->sec_exec_sid != 0) {
		new_sid = current->sec_exec_sid;
	} else {
		for (i = 0; i < nr_trans_rules; i++) {
			if (trans_rules[i].ssid == old_sid &&
			    trans_rules[i].tsid == tsid &&
			    trans_rules[i].tclass == SECCLASS_PROCESS) {
				new_sid = trans_rules[i].new_sid;
				break;
			}
		}
	}

	if (new_sid != 0 && new_sid != old_sid) {
		/* Domain transition: verify process:transition and file:entrypoint */
		rc = selinux_check_access(old_sid, new_sid, SECCLASS_PROCESS,
					  SEPERM_PROC_TRANSITION, filename, exec_inode, -1, -1);
		if (rc)
			return rc;
		rc = selinux_check_access(new_sid, tsid, SECCLASS_FILE,
					  SEPERM_ENTRYPOINT, filename, exec_inode, -1, -1);
		if (rc)
			return rc;
		/* Verify inherited file descriptor use permission */
		rc = selinux_check_access(new_sid, old_sid, SECCLASS_FD,
					  SEPERM_USE, filename, exec_inode, -1, -1);
		if (rc)
			return rc;

		current->sec_sid = new_sid;
		current->sec_exec_sid = 0;
	} else {
		/* No domain transition: must have file:execute_no_trans! */
		rc = selinux_check_access(old_sid, tsid, SECCLASS_FILE,
					  SEPERM_EXECUTE_NO_TRANS, filename, exec_inode, -1, -1);
		if (rc)
			return rc;
		current->sec_exec_sid = 0;
	}
	return 0;
}

int selinux_task_kill(struct task_struct *victim, int sig)
{
	unsigned short ssid, tsid;
	unsigned long perm = SEPERM_PROC_SIGNAL;

	if (!selinux_initialized || !current || !victim)
		return 0;
	if (!current->user_mode && current->pid <= 4)
		return 0;
	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	tsid = victim->sec_sid ? victim->sec_sid : sid_kernel;
	if (sig == SIGKILL)
		perm = SEPERM_PROC_SIGKILL;
	else if (sig == SIGSTOP || sig == SIGTSTP)
		perm = SEPERM_PROC_SIGSTOP;
	else if (sig == SIGCHLD)
		perm = SEPERM_PROC_SIGCHLD;

	return selinux_check_access(ssid, tsid, SECCLASS_PROCESS, perm, NULL, NULL, -1, -1);
}

int selinux_task_ptrace(struct task_struct *victim)
{
	unsigned short ssid, tsid;
	int rc;

	if (!selinux_initialized || !current || !victim)
		return 0;
	ssid = current->sec_sid ? current->sec_sid : sid_kernel;
	tsid = victim->sec_sid ? victim->sec_sid : sid_kernel;
	rc = selinux_check_access(ssid, ssid, SECCLASS_CAPABILITY,
				  (1UL << CAP_SYS_PTRACE), NULL, NULL, -1, CAP_SYS_PTRACE);
	if (rc)
		return -EPERM;
	return selinux_check_access(ssid, tsid, SECCLASS_PROCESS,
				    SEPERM_PROC_PTRACE, NULL, NULL, -1, -1);
}

/* Binder & ServiceManager MAC Hooks */
int selinux_binder_set_context_mgr(struct task_struct *mgr)
{
	unsigned short ssid;
	if (!selinux_initialized || !mgr)
		return 0;
	ssid = mgr->sec_sid ? mgr->sec_sid : sid_kernel;
	return selinux_check_access(ssid, ssid, SECCLASS_BINDER,
				    SEPERM_BINDER_SET_CONTEXT_MGR, "/dev/binder", NULL, -1, -1);
}

int selinux_binder_transaction(struct task_struct *from, struct task_struct *to)
{
	unsigned short ssid, tsid;
	if (!selinux_initialized || !from || !to)
		return 0;
	ssid = from->sec_sid ? from->sec_sid : sid_kernel;
	tsid = to->sec_sid ? to->sec_sid : sid_kernel;
	return selinux_check_access(ssid, tsid, SECCLASS_BINDER,
				    SEPERM_BINDER_CALL, "/dev/binder", NULL, -1, -1);
}

int selinux_binder_transfer_binder(struct task_struct *from, struct task_struct *to)
{
	unsigned short ssid, tsid;
	if (!selinux_initialized || !from || !to)
		return 0;
	ssid = from->sec_sid ? from->sec_sid : sid_kernel;
	tsid = to->sec_sid ? to->sec_sid : sid_kernel;
	return selinux_check_access(ssid, tsid, SECCLASS_BINDER,
				    SEPERM_BINDER_TRANSFER, "/dev/binder", NULL, -1, -1);
}

int selinux_service_check(const char *scontext, const char *service_name, const char *perm_name)
{
	unsigned short ssid, tsid = sid_default_android_service;
	unsigned long perm;
	int i;

	if (!selinux_initialized)
		return 0;
	ssid = selinux_context_to_sid(scontext);
	if (!ssid)
		ssid = (current && current->sec_sid) ? current->sec_sid : sid_untrusted_app;

	if (service_name && service_name[0]) {
		for (i = 0; i < nr_service_contexts; i++) {
			if (!strcmp(service_contexts[i].name, service_name)) {
				tsid = service_contexts[i].sid;
				break;
			}
		}
		/* Also allow passing the service type directly (e.g. fwupd_service) */
		if (tsid == sid_default_android_service) {
			unsigned short direct_sid = selinux_context_to_sid(service_name);
			if (direct_sid && (type_table[direct_sid].attrs & ATTR_SVCMGR_TYPE))
				tsid = direct_sid;
		}
	}

	perm = name_to_perm(SECCLASS_SERVICE_MANAGER, perm_name ? perm_name : "find");
	if (!perm)
		perm = SEPERM_SVCMGR_FIND;

	return selinux_check_access(ssid, tsid, SECCLASS_SERVICE_MANAGER,
				    perm, service_name, NULL, -1, -1);
}

/* Process /proc/<pid>/attr/current and /proc/<pid>/attr/exec accessors */
int selinux_task_get_context(struct task_struct *p, char *buf, int maxlen)
{
	const char *ctx;
	if (!p || !buf || maxlen <= 0)
		return 0;
	ctx = selinux_sid_to_context(p->sec_sid ? p->sec_sid : sid_kernel);
	strncpy(buf, ctx, maxlen - 1);
	buf[maxlen - 1] = '\0';
	return strlen(buf);
}

int selinux_task_get_exec_context(struct task_struct *p, char *buf, int maxlen)
{
	const char *ctx;
	if (!p || !buf || maxlen <= 0)
		return 0;
	if (!p->sec_exec_sid) {
		buf[0] = '\0';
		return 0;
	}
	ctx = selinux_sid_to_context(p->sec_exec_sid);
	strncpy(buf, ctx, maxlen - 1);
	buf[maxlen - 1] = '\0';
	return strlen(buf);
}

int selinux_task_set_context(struct task_struct *p, const char *ctx, int is_exec)
{
	unsigned short old_sid, new_sid;
	int rc;

	if (!p || !ctx)
		return -EINVAL;
	if (!ctx[0] && is_exec) {
		p->sec_exec_sid = 0;
		return 0;
	}
	new_sid = selinux_context_to_sid(ctx);
	if (!new_sid || !type_table[new_sid].is_domain)
		return -EINVAL;

	old_sid = p->sec_sid ? p->sec_sid : sid_kernel;
	if (is_exec) {
		rc = selinux_check_access(old_sid, old_sid, SECCLASS_PROCESS,
					  SEPERM_PROC_SETEXEC, NULL, NULL, -1, -1);
		if (rc)
			return rc;
		p->sec_exec_sid = new_sid;
		return 0;
	} else {
		rc = selinux_check_access(old_sid, old_sid, SECCLASS_PROCESS,
					  SEPERM_PROC_SETCURRENT, NULL, NULL, -1, -1);
		if (rc)
			return rc;
		rc = selinux_check_access(old_sid, new_sid, SECCLASS_PROCESS,
					  SEPERM_PROC_DYNTRANSITION, NULL, NULL, -1, -1);
		if (rc)
			return rc;
		p->sec_sid = new_sid;
		return 0;
	}
}

/* Runtime chcon / getfilecon support */
int selinux_chcon_path(const char *pathname, const char *context)
{
	struct inode *inode = NULL;
	unsigned short ssid, old_sid, new_sid;
	struct selinux_inode_sec *isec;
	int tclass, rc;

	if (!pathname || !context)
		return -EINVAL;
	new_sid = selinux_context_to_sid(context);
	if (!new_sid)
		return -EINVAL;

	rc = lnamei(pathname, &inode);
	if (rc)
		return rc;

	ssid = (current && current->sec_sid) ? current->sec_sid : sid_kernel;
	old_sid = selinux_inode_sid(inode);
	tclass = inode_to_secclass(inode);

	rc = selinux_check_access(ssid, old_sid, tclass, SEPERM_RELABELFROM,
				  pathname, inode, -1, -1);
	if (rc) {
		iput(inode);
		return rc;
	}
	rc = selinux_check_access(ssid, new_sid, tclass, SEPERM_RELABELTO,
				  pathname, inode, -1, -1);
	if (rc) {
		iput(inode);
		return rc;
	}

	inode->i_sec_sid = new_sid;
	isec = find_inode_sec((unsigned short)inode->i_dev, inode->i_ino, 1);
	if (isec) {
		isec->sid = new_sid;
		isec->explicit_label = 1;
		if (pathname[0] == '/') {
			strncpy(isec->path, pathname, sizeof(isec->path) - 1);
			isec->path[sizeof(isec->path) - 1] = '\0';
		}
	}
	iput(inode);
	return 0;
}

int selinux_getfilecon_path(const char *pathname, char *buf, int maxlen)
{
	struct inode *inode = NULL;
	unsigned short sid;
	const char *ctx;
	int rc;

	if (!pathname || !buf || maxlen <= 0)
		return -EINVAL;
	rc = lnamei(pathname, &inode);
	if (rc)
		return rc;
	if (pathname[0] == '/')
		selinux_label_inode_path(inode, pathname);
	sid = selinux_inode_sid(inode);
	iput(inode);

	ctx = selinux_sid_to_context(sid);
	strncpy(buf, ctx, maxlen - 1);
	buf[maxlen - 1] = '\0';
	return strlen(buf);
}

/*
 * ===================================================================
 * Dynamic .te Policy Statement Parser & Validator (/sys/fs/selinux/load)
 * ===================================================================
 * Supports loading/extending policies from userland (/bin/load_policy)
 * and enforces:
 *   1. security:load_policy permission check
 *   2. 3-tier visibility check: vendor policy statements (@tier vendor)
 *      cannot reference system/private types (e.g. ntfs_3g, ntfs_3g_exec,
 *      httpd, telnetd)
 *   3. Full neverallow and neverallowxperm verification (rolls back if
 *      any neverallow or neverallowxperm assertion is violated!)
 */
static const char *skip_ws(const char *p)
{
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
		p++;
	return p;
}

static const char *next_token(const char *p, char *tok, int maxlen)
{
	int i = 0;
	p = skip_ws(p);
	while (*p == '#') {
		while (*p && *p != '\n')
			p++;
		p = skip_ws(p);
	}
	if (!*p) {
		tok[0] = '\0';
		return p;
	}
	if (*p == '{' || *p == '}' || *p == '(' || *p == ')' ||
	    *p == ':' || *p == ';' || *p == ',') {
		tok[0] = *p++;
		tok[1] = '\0';
		return p;
	}
	while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' &&
	       *p != '{' && *p != '}' && *p != '(' && *p != ')' &&
	       *p != ':' && *p != ';' && *p != ',') {
		if (i < maxlen - 1)
			tok[i++] = *p;
		p++;
	}
	tok[i] = '\0';
	return p;
}

static unsigned long name_to_attr(const char *name)
{
	if (!strcmp(name, "domain"))			return ATTR_DOMAIN;
	if (!strcmp(name, "coredomain"))		return ATTR_COREDOMAIN;
	if (!strcmp(name, "appdomain"))			return ATTR_APPDOMAIN;
	if (!strcmp(name, "vendordomain"))		return ATTR_VENDORDOMAIN;
	if (!strcmp(name, "mlstrustedsubject"))		return ATTR_MLSTRUSTED;
	if (!strcmp(name, "file_type"))			return ATTR_FILE_TYPE;
	if (!strcmp(name, "system_file_type"))		return ATTR_SYSTEM_FILE_TYPE;
	if (!strcmp(name, "vendor_file_type"))		return ATTR_VENDOR_FILE_TYPE;
	if (!strcmp(name, "exec_type"))			return ATTR_EXEC_TYPE;
	if (!strcmp(name, "dev_type"))			return ATTR_DEV_TYPE;
	if (!strcmp(name, "sysfs_type"))		return ATTR_SYSFS_TYPE;
	if (!strcmp(name, "proc_type"))			return ATTR_PROC_TYPE;
	if (!strcmp(name, "data_file_type"))		return ATTR_DATA_FILE_TYPE;
	if (!strcmp(name, "service_manager_type"))	return ATTR_SVCMGR_TYPE;
	return 0;
}

static int parse_sid_expr(const char **pp, struct sid_set *out, int *is_self,
			  int cur_tier, char *err_buf)
{
	char tok[64];
	const char *p = *pp;
	int in_brace = 0;

	sid_set_zero(out);
	if (is_self)
		*is_self = 0;

	p = next_token(p, tok, sizeof(tok));
	if (!tok[0])
		return -EINVAL;
	if (!strcmp(tok, "{"))
		in_brace = 1;

	do {
		int neg = 0;
		const char *name = tok;
		unsigned long attr;
		unsigned short sid;

		if (in_brace) {
			p = next_token(p, tok, sizeof(tok));
			if (!tok[0] || !strcmp(tok, "}"))
				break;
			name = tok;
		}
		if (name[0] == '-') {
			neg = 1;
			name++;
		}
		if (!strcmp(name, "self")) {
			if (is_self)
				*is_self = 1;
		} else if (!strcmp(name, "*")) {
			sid_set_add_attr(out, ATTR_DOMAIN);
		} else if ((attr = name_to_attr(name)) != 0) {
			if (neg)
				sid_set_del_attr(out, attr);
			else
				sid_set_add_attr(out, attr);
		} else {
			sid = selinux_type_to_sid(name);
			if (!sid) {
				sprintf(err_buf, "unknown type '%s'", name);
				printk(KERN_ERR "selinux: unknown type '%s' in policy\n", name);
				return -EINVAL;
			}
			/* 3-tier visibility enforcement: vendor cannot reference system/private! */
			if (cur_tier == SELINUX_TIER_VENDOR &&
			    type_table[sid].tier == SELINUX_TIER_PRIVATE) {
				sprintf(err_buf,
					"3-tier violation: vendor policy references system/private type '%s'",
					name);
				printk(KERN_ERR "selinux: vendor policy violation: type '%s' is system/private and not in system/public\n",
				       name);
				return -EINVAL;
			}
			if (neg)
				sid_set_del(out, sid);
			else
				sid_set_add(out, sid);
		}
	} while (in_brace);

	*pp = p;
	return 0;
}

static int selinux_parse_and_load_policy(const char *buf, int len)
{
	const char *p = buf;
	char tok[64];
	int cur_tier = SELINUX_TIER_PUBLIC;
	int rc;

	(void)len;
	selinux_load_err[0] = '\0';

	while (*p) {
		p = next_token(p, tok, sizeof(tok));
		if (!tok[0])
			break;

		if (!strcmp(tok, "@tier")) {
			p = next_token(p, tok, sizeof(tok));
			if (!strcmp(tok, "public"))
				cur_tier = SELINUX_TIER_PUBLIC;
			else if (!strcmp(tok, "private"))
				cur_tier = SELINUX_TIER_PRIVATE;
			else if (!strcmp(tok, "vendor"))
				cur_tier = SELINUX_TIER_VENDOR;
			continue;
		}

		if (!strcmp(tok, "@reset")) {
			selinux_seed_baseline_policy();
			continue;
		}

		if (!strcmp(tok, "type")) {
			char tname[32];
			unsigned long attrs = 0;
			int is_dom = 0;

			p = next_token(p, tname, sizeof(tname));
			while (*p) {
				p = next_token(p, tok, sizeof(tok));
				if (!tok[0] || !strcmp(tok, ";"))
					break;
				if (!strcmp(tok, ","))
					continue;
				attrs |= name_to_attr(tok);
				if (!strcmp(tok, "domain"))
					is_dom = 1;
			}
			register_type(tname, is_dom, cur_tier, attrs);
			continue;
		}

		if (!strcmp(tok, "typeattribute")) {
			char tname[32];
			unsigned short sid;
			unsigned long attrs = 0;

			p = next_token(p, tname, sizeof(tname));
			sid = selinux_type_to_sid(tname);
			while (*p) {
				p = next_token(p, tok, sizeof(tok));
				if (!tok[0] || !strcmp(tok, ";"))
					break;
				if (!strcmp(tok, ","))
					continue;
				attrs |= name_to_attr(tok);
			}
			if (sid)
				type_table[sid].attrs |= attrs;
			continue;
		}

		if (!strcmp(tok, "type_transition") || !strcmp(tok, "domain_auto_trans")) {
			int is_macro = !strcmp(tok, "domain_auto_trans");
			char sname[32], tname[32], dname[32];
			unsigned short ssid, tsid, dsid;

			p = next_token(p, sname, sizeof(sname));
			if (!strcmp(sname, "("))
				p = next_token(p, sname, sizeof(sname));
			p = next_token(p, tname, sizeof(tname));
			if (!strcmp(tname, ","))
				p = next_token(p, tname, sizeof(tname));
			if (!is_macro) {
				/* skip : process */
				p = next_token(p, tok, sizeof(tok));
				if (!strcmp(tok, ":"))
					p = next_token(p, tok, sizeof(tok));
			}
			p = next_token(p, dname, sizeof(dname));
			if (!strcmp(dname, ","))
				p = next_token(p, dname, sizeof(dname));
			while (*p) {
				const char *save_p = p;
				p = next_token(p, tok, sizeof(tok));
				if (!strcmp(tok, ")") || !strcmp(tok, ";"))
					continue;
				p = save_p;
				break;
			}

			ssid = selinux_type_to_sid(sname);
			tsid = selinux_type_to_sid(tname);
			dsid = selinux_type_to_sid(dname);
			if (!ssid || !tsid || !dsid)
				return -EINVAL;
			if (cur_tier == SELINUX_TIER_VENDOR &&
			    (type_table[ssid].tier == SELINUX_TIER_PRIVATE ||
			     type_table[tsid].tier == SELINUX_TIER_PRIVATE ||
			     type_table[dsid].tier == SELINUX_TIER_PRIVATE)) {
				sprintf(selinux_load_err,
					"3-tier violation: vendor transition references system/private type");
				printk(KERN_ERR "selinux: %s\n", selinux_load_err);
				return -EINVAL;
			}
			if (is_macro)
				add_domain_auto_trans(ssid, tsid, dsid);
			else
				add_trans(ssid, tsid, SECCLASS_PROCESS, dsid);
			continue;
		}

		if (!strcmp(tok, "allow") || !strcmp(tok, "neverallow")) {
			int is_never = !strcmp(tok, "neverallow");
			struct sid_set sset, tset;
			int is_self = 0;
			int tclass = SECCLASS_NONE;
			unsigned long tclass_mask = 0;
			unsigned long perms = 0;
			int s, t, c;

			rc = parse_sid_expr(&p, &sset, NULL, cur_tier, selinux_load_err);
			if (rc)
				return rc;
			rc = parse_sid_expr(&p, &tset, &is_self, cur_tier, selinux_load_err);
			if (rc)
				return rc;

			p = next_token(p, tok, sizeof(tok));
			if (!strcmp(tok, ":"))
				p = next_token(p, tok, sizeof(tok));
			if (!strcmp(tok, "{")) {
				while (*p) {
					int tc;
					p = next_token(p, tok, sizeof(tok));
					if (!tok[0] || !strcmp(tok, "}"))
						break;
					tc = name_to_secclass(tok);
					if (tc != SECCLASS_NONE) {
						tclass_mask |= (1UL << tc);
						if (tclass == SECCLASS_NONE)
							tclass = tc;
					}
				}
			} else {
				tclass = name_to_secclass(tok);
				if (tclass != SECCLASS_NONE)
					tclass_mask = (1UL << tclass);
			}

			p = next_token(p, tok, sizeof(tok));
			if (!strcmp(tok, "{")) {
				while (*p) {
					p = next_token(p, tok, sizeof(tok));
					if (!tok[0] || !strcmp(tok, "}"))
						break;
					perms |= name_to_perm(tclass, tok);
				}
			} else {
				perms |= name_to_perm(tclass, tok);
			}
			p = next_token(p, tok, sizeof(tok)); /* ; */

			if (!tclass_mask || !perms)
				continue;

			for (c = 1; c < SECCLASS_MAX; c++) {
				if (!(tclass_mask & (1UL << c)))
					continue;
				if (is_never) {
					add_neverallow(&sset, &tset, is_self, c, perms,
						       "policy neverallow");
				} else {
					for (s = 1; s < nr_types; s++) {
						if (!sid_set_test(&sset, (unsigned short)s))
							continue;
						if (is_self) {
							allow_sid((unsigned short)s, (unsigned short)s,
								  c, perms);
						}
						for (t = 1; t < nr_types; t++) {
							if (sid_set_test(&tset, (unsigned short)t))
								allow_sid((unsigned short)s, (unsigned short)t,
									  c, perms);
						}
					}
				}
			}
			continue;
		}

		if (!strcmp(tok, "allowxperm") || !strcmp(tok, "neverallowxperm")) {
			int is_never = !strcmp(tok, "neverallowxperm");
			struct sid_set sset, tset;
			int is_self = 0;
			int tclass;
			int s, t;

			rc = parse_sid_expr(&p, &sset, NULL, cur_tier, selinux_load_err);
			if (rc)
				return rc;
			rc = parse_sid_expr(&p, &tset, &is_self, cur_tier, selinux_load_err);
			if (rc)
				return rc;
			p = next_token(p, tok, sizeof(tok));
			if (!strcmp(tok, ":"))
				p = next_token(p, tok, sizeof(tok));
			tclass = name_to_secclass(tok);
			p = next_token(p, tok, sizeof(tok)); /* ioctl */

			p = next_token(p, tok, sizeof(tok));
			if (!strcmp(tok, "{")) {
				while (*p) {
					char *endp = NULL;
					unsigned long lo, hi;
					p = next_token(p, tok, sizeof(tok));
					if (!tok[0] || !strcmp(tok, "}"))
						break;
					lo = simple_strtoul(tok, &endp, 0);
					hi = lo;
					if (endp && (*endp == '-' || (*endp == '.' && endp[1] == '.'))) {
						endp += (*endp == '.') ? 2 : 1;
						hi = simple_strtoul(endp, NULL, 0);
					}
					for (s = 1; s < nr_types; s++) {
						if (!sid_set_test(&sset, (unsigned short)s))
							continue;
						for (t = 1; t < nr_types; t++) {
							if (sid_set_test(&tset, (unsigned short)t))
								add_xperm((unsigned short)s, (unsigned short)t,
									  tclass, (unsigned short)lo,
									  (unsigned short)hi, is_never);
						}
					}
				}
			} else {
				char *endp = NULL;
				unsigned long lo = simple_strtoul(tok, &endp, 0);
				unsigned long hi = lo;
				if (endp && (*endp == '-' || (*endp == '.' && endp[1] == '.'))) {
					endp += (*endp == '.') ? 2 : 1;
					hi = simple_strtoul(endp, NULL, 0);
				}
				for (s = 1; s < nr_types; s++) {
					if (!sid_set_test(&sset, (unsigned short)s))
						continue;
					for (t = 1; t < nr_types; t++) {
						if (sid_set_test(&tset, (unsigned short)t))
							add_xperm((unsigned short)s, (unsigned short)t,
								  tclass, (unsigned short)lo,
								  (unsigned short)hi, is_never);
					}
				}
			}
			p = next_token(p, tok, sizeof(tok)); /* ; */
			continue;
		}

		/* Skip unrecognized statement to semicolon */
		while (*p && strcmp(tok, ";") != 0)
			p = next_token(p, tok, sizeof(tok));
	}

	rc = selinux_verify_neverallows(selinux_load_err, sizeof(selinux_load_err));
	if (rc) {
		/* Roll back to valid baseline policy on neverallow violation! */
		selinux_seed_baseline_policy();
		return rc;
	}

	selinux_policy_seqno++;
	return 0;
}

/*
 * ===================================================================
 * /sys/fs/selinux/* Character Device Driver (Major 56)
 * ===================================================================
 */
static int selinux_dev_read(struct inode *inode, struct file *filp,
			    char *buf, int count)
{
	int min = MINOR(inode->i_rdev);
	static char kbuf[4096];
	int len = 0;
	int avail;

	if (!buf || count <= 0)
		return 0;

	switch (min) {
	case SELINUX_MINOR_ENFORCE:
		len = sprintf(kbuf, "%d\n", selinux_enforcing);
		break;
	case SELINUX_MINOR_CHECKREQPROT:
		len = sprintf(kbuf, "%d\n", selinux_checkreqprot);
		break;
	case SELINUX_MINOR_STATUS:
		len = sprintf(kbuf,
			      "version:        30\n"
			      "enforcing:      %d\n"
			      "policyload:     %lu\n"
			      "deny_unknown:   1\n"
			      "types:          %d\n"
			      "neverallows:    %d\n"
			      "xperm_rules:    %d\n"
			      "file_contexts:  %d\n"
			      "svc_contexts:   %d\n"
			      "avc_lookups:    %lu\n"
			      "avc_hits:       %lu\n"
			      "avc_denials:    %lu\n",
			      selinux_enforcing, selinux_policy_seqno,
			      nr_types - 1, nr_neverallow_rules,
			      nr_xperm_rules, nr_file_contexts,
			      nr_service_contexts,
			      avc_lookups, avc_hits, avc_denials);
		break;
	case SELINUX_MINOR_ACCESS:
		len = strlen(selinux_access_reply);
		memcpy(kbuf, selinux_access_reply, len + 1);
		break;
	case SELINUX_MINOR_CONTEXT:
		len = strlen(selinux_context_reply);
		memcpy(kbuf, selinux_context_reply, len + 1);
		break;
	case SELINUX_MINOR_LOAD:
		len = strlen(selinux_load_err);
		memcpy(kbuf, selinux_load_err, len + 1);
		break;
	case SELINUX_MINOR_POLICY: {
		int i;
		len += sprintf(kbuf + len,
			       "# SELinux Compiled Policy (version 30, 3-tier: public/private/vendor)\n"
			       "# Types (%d):\n", nr_types - 1);
		for (i = 1; i < nr_types && len < (int)sizeof(kbuf) - 128; i++) {
			const char *tier_str = (type_table[i].tier == SELINUX_TIER_PRIVATE) ? "system/private" :
					       (type_table[i].tier == SELINUX_TIER_VENDOR) ? "vendor" : "system/public";
			len += sprintf(kbuf + len, "type %-26s [%-14s] (%s)\n",
				       type_table[i].name, tier_str, type_table[i].context);
		}
		break;
	}
	case SELINUX_MINOR_AVC:
		len = selinux_get_avc_log(kbuf, sizeof(kbuf));
		break;
	default:
		return 0;
	}

	if (filp->f_pos >= len)
		return 0;
	avail = len - (int)filp->f_pos;
	if (count > avail)
		count = avail;
	memcpy_tofs(buf, kbuf + filp->f_pos, count);
	filp->f_pos += count;
	return count;
}

static const char *next_word(const char *p, char *tok, int maxlen)
{
	int i = 0;
	tok[0] = '\0';
	if (!p)
		return "";
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
		p++;
	while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
		if (i < maxlen - 1)
			tok[i++] = *p;
		p++;
	}
	tok[i] = '\0';
	return p;
}

static int selinux_dev_write(struct inode *inode, struct file *filp,
			     const char *buf, int count)
{
	int min = MINOR(inode->i_rdev);
	static char kbuf[8192];
	int copy_len = count;
	unsigned short ssid;
	int rc;

	(void)filp;
	if (!buf || count <= 0)
		return 0;
	if (copy_len >= (int)sizeof(kbuf))
		copy_len = sizeof(kbuf) - 1;
	memcpy_fromfs(kbuf, buf, copy_len);
	kbuf[copy_len] = '\0';

	ssid = (current && current->sec_sid) ? current->sec_sid : sid_kernel;

	switch (min) {
	case SELINUX_MINOR_ENFORCE: {
		int val = (kbuf[0] == '1') ? 1 : 0;
		rc = selinux_check_access(ssid, sid_selinuxfs, SECCLASS_SECURITY,
					  SEPERM_SEC_SETENFORCE, "/sys/fs/selinux/enforce",
					  inode, -1, -1);
		if (rc)
			return rc;
		selinux_enforcing = val;
		printk(KERN_INFO "selinux: enforcing=%d (by pid=%d comm=%s scontext=%s)\n",
		       selinux_enforcing, current ? current->pid : 0,
		       current ? current->comm : "kernel", selinux_sid_to_context(ssid));
		return count;
	}
	case SELINUX_MINOR_CHECKREQPROT:
		selinux_checkreqprot = (kbuf[0] == '1') ? 1 : 0;
		return count;
	case SELINUX_MINOR_ACCESS: {
		/*
		 * Format: "<scontext> <tcontext_or_service> <tclass> <perm>"
		 * Used by servicemanager and security check tools.
		 */
		char sctx[64], tctx[64], cls[32], prm[32];
		const char *p = kbuf;
		p = next_word(p, sctx, sizeof(sctx));
		p = next_word(p, tctx, sizeof(tctx));
		p = next_word(p, cls, sizeof(cls));
		p = next_word(p, prm, sizeof(prm));

		if (!strcmp(cls, "service_manager")) {
			rc = selinux_service_check(sctx, tctx, prm);
		} else {
			unsigned short s = selinux_context_to_sid(sctx);
			unsigned short t = selinux_context_to_sid(tctx);
			int tc = name_to_secclass(cls);
			unsigned long pm = name_to_perm(tc, prm);
			if (!s || !t || tc == SECCLASS_NONE || !pm)
				rc = -EINVAL;
			else
				rc = selinux_check_access(s, t, tc, pm, NULL, NULL, -1, -1);
		}
		sprintf(selinux_access_reply, "%d\n", (rc == 0) ? 1 : 0);
		return (rc == 0) ? count : rc;
	}
	case SELINUX_MINOR_CONTEXT: {
		/*
		 * Supports:
		 *   "get <path>"          -> populates selinux_context_reply with file context
		 *   "chcon <ctx> <path>"  -> relabels path
		 *   "restorecon <path>"   -> resets path to file_contexts default
		 *   "<ctx>"               -> validates context string
		 */
		char cmd[64], arg1[64], arg2[96];
		const char *p = kbuf;
		p = next_word(p, cmd, sizeof(cmd));
		p = next_word(p, arg1, sizeof(arg1));
		p = next_word(p, arg2, sizeof(arg2));

		if (!strcmp(cmd, "get") && arg1[0]) {
			rc = selinux_getfilecon_path(arg1, selinux_context_reply,
						     sizeof(selinux_context_reply));
			if (rc < 0)
				return rc;
			return count;
		}
		if (!strcmp(cmd, "chcon") && arg1[0] && arg2[0]) {
			rc = selinux_chcon_path(arg2, arg1);
			return (rc < 0) ? rc : count;
		}
		if (!strcmp(cmd, "restorecon") && arg1[0]) {
			struct inode *rinode = NULL;
			struct selinux_inode_sec *risec;
			unsigned short def_sid = selinux_match_path_sid(arg1);
			if (lnamei(arg1, &rinode) == 0 && rinode) {
				risec = find_inode_sec((unsigned short)rinode->i_dev, rinode->i_ino, 0);
				if (risec) {
					risec->explicit_label = 0;
					risec->sid = 0;
				}
				if (S_ISCHR(rinode->i_mode) || S_ISBLK(rinode->i_mode)) {
					rinode->i_sec_sid = 0;
					def_sid = selinux_inode_sid(rinode);
				}
				iput(rinode);
			}
			rc = selinux_chcon_path(arg1, selinux_sid_to_context(def_sid));
			if (rc == 0 && lnamei(arg1, &rinode) == 0 && rinode) {
				risec = find_inode_sec((unsigned short)rinode->i_dev, rinode->i_ino, 0);
				if (risec)
					risec->explicit_label = 0;
				iput(rinode);
			}
			strncpy(selinux_context_reply, selinux_sid_to_context(def_sid),
				sizeof(selinux_context_reply) - 1);
			return (rc < 0) ? rc : count;
		}
		if (!selinux_context_to_sid(cmd))
			return -EINVAL;
		strncpy(selinux_context_reply, selinux_sid_to_context(selinux_context_to_sid(cmd)),
			sizeof(selinux_context_reply) - 1);
		return count;
	}
	case SELINUX_MINOR_LOAD:
		rc = selinux_check_access(ssid, sid_selinuxfs, SECCLASS_SECURITY,
					  SEPERM_SEC_LOAD_POLICY, "/sys/fs/selinux/load",
					  inode, -1, -1);
		if (rc)
			return rc;
		rc = selinux_parse_and_load_policy(kbuf, copy_len);
		if (rc < 0)
			return rc;
		return count;
	case SELINUX_MINOR_AVC:
		if (!strncmp(kbuf, "clear", 5)) {
			avc_ring_head = 0;
			avc_ring_count = 0;
		}
		return count;
	default:
		return count;
	}
}

static struct file_operations selinux_fops = {
	NULL,			/* lseek */
	selinux_dev_read,	/* read */
	selinux_dev_write,	/* write */
	NULL,			/* readdir */
	NULL,			/* select */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

int selinux_get_proc_info(char *buf)
{
	return sprintf(buf,
		       "SELinux status:                 enabled\n"
		       "SELinuxfs mount:                /sys/fs/selinux\n"
		       "SELinux root directory:         /etc/selinux\n"
		       "Loaded policy name:             sepolicy_v1 (3-tier: system/public, system/private, vendor)\n"
		       "Current mode:                   %s\n"
		       "Mode from config file:          enforcing\n"
		       "Policy MLS status:              enabled\n"
		       "Policy deny_unknown status:     denied\n"
		       "Max kernel policy version:      30\n"
		       "Types defined:                  %d\n"
		       "Neverallow assertions verified: %d\n"
		       "Extended ioctl rules (xperm):   %d\n"
		       "AVC lookups / hits / denials:   %lu / %lu / %lu\n",
		       selinux_enforcing ? "enforcing" : "permissive",
		       nr_types - 1, nr_neverallow_rules, nr_xperm_rules,
		       avc_lookups, avc_hits, avc_denials);
}

int get_selinux_proc_info(char *buf)
{
	return selinux_get_proc_info(buf);
}

int selinux_get_avc_log(char *buf, int maxlen)
{
	unsigned int i, start;
	int pos = 0;

	if (!buf || maxlen <= 0)
		return 0;
	start = (avc_ring_head >= avc_ring_count) ? (avc_ring_head - avc_ring_count) : 0;
	for (i = 0; i < avc_ring_count && pos + 230 < maxlen; i++) {
		unsigned int idx = (start + i) & (AVC_LOG_RING_SIZE - 1);
		pos += sprintf(buf + pos, "%s\n", avc_ring[idx].line);
	}
	buf[pos] = '\0';
	return pos;
}

void selinux_init(void)
{
	char err[128];

	if (selinux_initialized)
		return;

	memset(inode_sec_table, 0, sizeof(inode_sec_table));
	selinux_seed_baseline_policy();

	if (selinux_verify_neverallows(err, sizeof(err)) != 0) {
		panic("selinux: fatal neverallow violation in baseline policy: %s", err);
	}

	if (register_chrdev(SELINUX_MAJOR, "selinux", &selinux_fops)) {
		printk(KERN_ERR "selinux: unable to register chrdev major %d\n", SELINUX_MAJOR);
	}

	init_task.sec_sid = sid_kernel;
	init_task.sec_exec_sid = 0;
	if (current) {
		current->sec_sid = sid_kernel;
		current->sec_exec_sid = 0;
	}

	selinux_enforcing = 1;
	selinux_initialized = 1;
	printk(KERN_INFO "SELinux: initialized (mode=enforcing, policy=3-tier public/private/vendor, types=%d, neverallows=%d, xperms=%d)\n",
	       nr_types - 1, nr_neverallow_rules, nr_xperm_rules);
}
