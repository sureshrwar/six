/*
 * include/linux/selinux.h - SELinux (SEAndroid) Mandatory Access Control for SIX
 *
 * Full-strictness Android Desktop style Type Enforcement (TE), Extended
 * Permission (allowxperm / neverallowxperm ioctl filtering), Capability
 * checks orthogonal to DAC uid=0, Domain Transitions, File/Genfs/Procfs
 * Labeling, Binder & ServiceManager MAC, and Compile/Load-time Neverallow
 * verification.
 */

#ifndef _LINUX_SELINUX_H
#define _LINUX_SELINUX_H

#include <linux/types.h>

/* Major number for /sys/fs/selinux/* character device nodes */
#define SELINUX_MAJOR		56

/* /sys/fs/selinux/* minor numbers */
#define SELINUX_MINOR_ENFORCE		0
#define SELINUX_MINOR_STATUS		1
#define SELINUX_MINOR_ACCESS		2
#define SELINUX_MINOR_LOAD		3
#define SELINUX_MINOR_POLICY		4
#define SELINUX_MINOR_CONTEXT		5
#define SELINUX_MINOR_CHECKREQPROT	6
#define SELINUX_MINOR_AVC		7
#define SELINUX_MINOR_NULL		8

/* Security Object Classes */
#define SECCLASS_NONE			0
#define SECCLASS_PROCESS		1
#define SECCLASS_FILE			2
#define SECCLASS_DIR			3
#define SECCLASS_CHR_FILE		4
#define SECCLASS_BLK_FILE		5
#define SECCLASS_LNK_FILE		6
#define SECCLASS_FIFO_FILE		7
#define SECCLASS_SOCK_FILE		8
#define SECCLASS_FD			9
#define SECCLASS_CAPABILITY		10
#define SECCLASS_FILESYSTEM		11
#define SECCLASS_BINDER			12
#define SECCLASS_SERVICE_MANAGER	13
#define SECCLASS_SECURITY		14
#define SECCLASS_SYSTEM			15
#define SECCLASS_MAX			16

/* Common File / Dir / Process / IPC Permission Bits (32-bit mask per class) */
#define SEPERM_READ			(1UL << 0)
#define SEPERM_WRITE			(1UL << 1)
#define SEPERM_EXECUTE			(1UL << 2)
#define SEPERM_OPEN			(1UL << 3)
#define SEPERM_IOCTL			(1UL << 4)
#define SEPERM_GETATTR			(1UL << 5)
#define SEPERM_SETATTR			(1UL << 6)
#define SEPERM_CREATE			(1UL << 7)
#define SEPERM_UNLINK			(1UL << 8)
#define SEPERM_LINK			(1UL << 9)
#define SEPERM_RENAME			(1UL << 10)
#define SEPERM_APPEND			(1UL << 11)
#define SEPERM_LOCK			(1UL << 12)
#define SEPERM_SEARCH			(1UL << 13)
#define SEPERM_ADD_NAME			(1UL << 14)
#define SEPERM_REMOVE_NAME		(1UL << 15)
#define SEPERM_RMDIR			(1UL << 16)
#define SEPERM_EXECUTE_NO_TRANS		(1UL << 17)
#define SEPERM_ENTRYPOINT		(1UL << 18)
#define SEPERM_RELABELFROM		(1UL << 19)
#define SEPERM_RELABELTO		(1UL << 20)
#define SEPERM_MOUNT			(1UL << 21)
#define SEPERM_UNMOUNT			(1UL << 22)
#define SEPERM_USE			(1UL << 23)

/* Process class specific permission bits */
#define SEPERM_PROC_FORK		(1UL << 0)
#define SEPERM_PROC_TRANSITION		(1UL << 1)
#define SEPERM_PROC_SIGCHLD		(1UL << 2)
#define SEPERM_PROC_SIGKILL		(1UL << 3)
#define SEPERM_PROC_SIGSTOP		(1UL << 4)
#define SEPERM_PROC_SIGNAL		(1UL << 5)
#define SEPERM_PROC_PTRACE		(1UL << 6)
#define SEPERM_PROC_GETSCHED		(1UL << 7)
#define SEPERM_PROC_SETSCHED		(1UL << 8)
#define SEPERM_PROC_GETPGID		(1UL << 9)
#define SEPERM_PROC_SETPGID		(1UL << 10)
#define SEPERM_PROC_GETCAP		(1UL << 11)
#define SEPERM_PROC_SETCAP		(1UL << 12)
#define SEPERM_PROC_SETCURRENT		(1UL << 13)
#define SEPERM_PROC_SETEXEC		(1UL << 14)
#define SEPERM_PROC_DYNTRANSITION	(1UL << 15)
#define SEPERM_PROC_SETRLIMIT		(1UL << 16)
#define SEPERM_PROC_EXECMEM		(1UL << 17)

/* Binder class permission bits */
#define SEPERM_BINDER_IMPERSONATE	(1UL << 0)
#define SEPERM_BINDER_CALL		(1UL << 1)
#define SEPERM_BINDER_SET_CONTEXT_MGR	(1UL << 2)
#define SEPERM_BINDER_TRANSFER		(1UL << 3)

/* Service Manager class permission bits */
#define SEPERM_SVCMGR_ADD		(1UL << 0)
#define SEPERM_SVCMGR_FIND		(1UL << 1)
#define SEPERM_SVCMGR_LIST		(1UL << 2)

/* Security class permission bits */
#define SEPERM_SEC_COMPUTE_AV		(1UL << 0)
#define SEPERM_SEC_COMPUTE_CREATE	(1UL << 1)
#define SEPERM_SEC_LOAD_POLICY		(1UL << 2)
#define SEPERM_SEC_SETENFORCE		(1UL << 3)
#define SEPERM_SEC_SETCHECKREQPROT	(1UL << 4)
#define SEPERM_SEC_CHECK_CONTEXT	(1UL << 5)

/* System class permission bits */
#define SEPERM_SYS_IPC_INFO		(1UL << 0)
#define SEPERM_SYS_SYSLOG_READ		(1UL << 1)
#define SEPERM_SYS_SYSLOG_MOD		(1UL << 2)
#define SEPERM_SYS_SYSLOG_CONSOLE	(1UL << 3)
#define SEPERM_SYS_MODULE_REQUEST	(1UL << 4)

/* Linux Capabilities (orthogonal to DAC uid=0 in SELinux) */
#define CAP_CHOWN		0
#define CAP_DAC_OVERRIDE	1
#define CAP_DAC_READ_SEARCH	2
#define CAP_FOWNER		3
#define CAP_FSETID		4
#define CAP_KILL		5
#define CAP_SETGID		6
#define CAP_SETUID		7
#define CAP_SETPCAP		8
#define CAP_LINUX_IMMUTABLE	9
#define CAP_NET_BIND_SERVICE	10
#define CAP_NET_BROADCAST	11
#define CAP_NET_ADMIN		12
#define CAP_NET_RAW		13
#define CAP_IPC_LOCK		14
#define CAP_IPC_OWNER		15
#define CAP_SYS_MODULE		16
#define CAP_SYS_RAWIO		17
#define CAP_SYS_CHROOT		18
#define CAP_SYS_PTRACE		19
#define CAP_SYS_PACCT		20
#define CAP_SYS_ADMIN		21
#define CAP_SYS_BOOT		22
#define CAP_SYS_NICE		23
#define CAP_SYS_RESOURCE	24
#define CAP_SYS_TIME		25
#define CAP_SYS_TTY_CONFIG	26
#define CAP_MKNOD		27

/* Standard Predefined Security IDs (SIDs) */
#define SELINUX_SID_UNLABELED		0
#define SELINUX_SID_KERNEL		1
#define SELINUX_SID_INIT		2
#define SELINUX_SID_SU			3
#define SELINUX_SID_SHELL		4
#define SELINUX_SID_UNTRUSTED_APP	5
#define SELINUX_SID_SERVICEMANAGER	6
#define SELINUX_SID_VOLD		7
#define SELINUX_SID_NTFS_3G		8
#define SELINUX_SID_STORAGED		9
#define SELINUX_SID_MEDIAPROVIDER	10
#define SELINUX_SID_EXTERNALSTORAGED	11
#define SELINUX_SID_ADBD		12
#define SELINUX_SID_FWUPD		13
#define SELINUX_SID_HTTPD		14
#define SELINUX_SID_TELNETD		15

/* Policy Tier classification for 3-tier visibility enforcement */
#define SELINUX_TIER_PUBLIC		0
#define SELINUX_TIER_PRIVATE		1
#define SELINUX_TIER_VENDOR		2

/* SCSI Generic SG_IO ioctl command (0x2285) */
#ifndef SG_IO
#define SG_IO	0x2285
#endif

#ifdef __KERNEL__

struct task_struct;
struct inode;
struct file;
struct super_block;

/* Core initialization and status */
void selinux_init(void);
int selinux_is_enforcing(void);
void selinux_set_enforcing(int enforce);

/* SID and Context String resolution */
unsigned short selinux_context_to_sid(const char *ctx);
unsigned short selinux_type_to_sid(const char *type_name);
const char *selinux_sid_to_context(unsigned short sid);
const char *selinux_sid_to_type(unsigned short sid);
int selinux_task_get_context(struct task_struct *p, char *buf, int maxlen);
int selinux_task_get_exec_context(struct task_struct *p, char *buf, int maxlen);
int selinux_task_set_context(struct task_struct *p, const char *ctx, int is_exec);

/* Inode labeling */
unsigned short selinux_inode_sid(struct inode *inode);
void selinux_d_instantiate(unsigned short dir_dev, unsigned long dir_ino,
			   unsigned short dir_sid, const char *name, int len,
			   struct inode *result);
void selinux_label_inode_path(struct inode *inode, const char *pathname);
void selinux_label_new_inode(struct inode *dir, struct inode *inode, const char *name);
int selinux_chcon_path(const char *pathname, const char *context);
int selinux_getfilecon_path(const char *pathname, char *buf, int maxlen);

/* Permission & Capability enforcement hooks */
int selinux_check_access(unsigned short ssid, unsigned short tsid,
			 int tclass, unsigned long perms,
			 const char *audit_path, struct inode *audit_inode,
			 int ioctlcmd, int cap_nr);
int selinux_check_xperm(unsigned short ssid, unsigned short tsid,
			int tclass, unsigned int ioctlcmd,
			const char *audit_path, struct inode *audit_inode);
int selinux_capable(int cap);
int suser_cap(int cap);

/* VFS & Process Lifecycle Hooks */
int selinux_inode_permission(struct inode *inode, int mask);
int selinux_file_open(struct inode *inode, int flag, const char *pathname);
int selinux_file_io(struct file *filp, int is_write);
int selinux_file_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
int selinux_inode_create(struct inode *dir, const char *name, int mode);
int selinux_inode_unlink(struct inode *dir, struct inode *victim, const char *name);
int selinux_inode_mkdir(struct inode *dir, const char *name, int mode);
int selinux_inode_rmdir(struct inode *dir, struct inode *victim, const char *name);
int selinux_inode_rename(struct inode *old_dir, struct inode *old_inode,
			 struct inode *new_dir, const char *new_name);
int selinux_inode_setattr(struct inode *inode);
int selinux_sb_mount(const char *dev_name, struct inode *dir_inode, const char *type);
int selinux_sb_umount(struct inode *mounted_inode);
void selinux_task_fork(struct task_struct *parent, struct task_struct *child, int is_kthread);
int selinux_bprm_transition(struct inode *exec_inode, const char *filename);
int selinux_task_kill(struct task_struct *victim, int sig);
int selinux_task_ptrace(struct task_struct *victim);

/* Binder & ServiceManager MAC Hooks */
int selinux_binder_set_context_mgr(struct task_struct *mgr);
int selinux_binder_transaction(struct task_struct *from, struct task_struct *to);
int selinux_binder_transfer_binder(struct task_struct *from, struct task_struct *to);
int selinux_service_check(const char *scontext, const char *service_name, const char *perm_name);

/* /proc/selinux & /sys/fs/selinux char device support */
int selinux_get_proc_info(char *buf);
int selinux_get_avc_log(char *buf, int maxlen);

#endif /* __KERNEL__ */

#endif /* _LINUX_SELINUX_H */
