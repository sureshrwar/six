/*
 * include/linux/binder.h
 *
 * Android Binder IPC kernel interface for SIX (Linux 2.0.11).
 *
 * Implements /dev/binder (char major 10, minor 58):
 *   - Context Manager (handle 0 = servicemanager)
 *   - Service registration & discovery (SVC_MGR_GET_SERVICE,
 *     SVC_MGR_CHECK_SERVICE, SVC_MGR_ADD_SERVICE, SVC_MGR_LIST_SERVICES)
 *   - Synchronous RPC transactions (BC_TRANSACTION -> BR_TRANSACTION ->
 *     BC_REPLY -> BR_REPLY) and asynchronous one-way transactions (TF_ONE_WAY)
 *   - Kernel-enforced caller identity (sender_pid, sender_euid)
 */

#ifndef _LINUX_BINDER_H
#define _LINUX_BINDER_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define BINDER_CHAR_MAJOR		63
#define BINDER_CHAR_MINOR		0
#define BINDER_CURRENT_PROTOCOL_VERSION	8

#define BINDER_MAX_DATA_SIZE		512
#define BINDER_MAX_NAME_LEN		64
#define BINDER_MAX_DESC_LEN		64
#define BINDER_MAX_SERVICES		32

/* Transaction flags */
#define TF_ONE_WAY			0x01
#define TF_ROOT_OBJECT			0x04
#define TF_STATUS_CODE			0x08
#define TF_ACCEPT_FDS			0x10

/* Well-known handle for servicemanager */
#define BINDER_CONTEXT_MGR_HANDLE	0

/* Standard Android IServiceManager transaction codes (handle 0) */
#define PING_TRANSACTION		0x5f504e47	/* '_PNG' */
#define DUMP_TRANSACTION		0x5f444d50	/* '_DMP' */
#define INTERFACE_TRANSACTION		0x5f4e5446	/* '_NTF' */
#define SVC_MGR_GET_SERVICE		1
#define SVC_MGR_CHECK_SERVICE		2
#define SVC_MGR_ADD_SERVICE		3
#define SVC_MGR_LIST_SERVICES		4

/* Binder command protocol (BC_*) and return protocol (BR_*) */
#define BC_TRANSACTION			0x40006300
#define BC_REPLY			0x40006301
#define BC_ENTER_LOOPER			0x0000630c
#define BC_EXIT_LOOPER			0x0000630d

#define BR_OK				0x00007201
#define BR_TRANSACTION			0x80007202
#define BR_REPLY			0x80007203
#define BR_DEAD_REPLY			0x00007205
#define BR_TRANSACTION_COMPLETE		0x00007206
#define BR_FAILED_REPLY			0x00007211
#define BR_NOOP				0x0000720c

struct binder_version {
	int protocol_version;
};

struct binder_write_read {
	unsigned long write_size;
	unsigned long write_consumed;
	unsigned long write_buffer;
	unsigned long read_size;
	unsigned long read_consumed;
	unsigned long read_buffer;
};

/*
 * Self-contained Parcel / Transaction message used by BINDER_WRITE_READ
 * and the convenience ioctls (BINDER_IOC_TRANSACT / BINDER_IOC_RECV /
 * BINDER_IOC_REPLY) for zero-boilerplate C services and clients in SIX.
 */
struct binder_ipc_msg {
	int target_handle;		/* 0 = servicemanager, >=1 = service handle */
	unsigned int code;		/* Transaction code (e.g. SVC_MGR_*, method ID) */
	unsigned int flags;		/* 0 = synchronous RPC, TF_ONE_WAY = async */
	int sender_pid;			/* Filled by kernel (unforgeable) */
	int sender_euid;		/* Filled by kernel (unforgeable) */
	int status;			/* Reply status (0 = OK, <0 = error) */
	int reply_handle;		/* Returned service handle (if applicable) */
	unsigned int txn_id;		/* Kernel transaction ID for matching reply */
	char interface_token[BINDER_MAX_DESC_LEN];
	unsigned int data_size;
	char data[BINDER_MAX_DATA_SIZE];
};

struct binder_service_info {
	int handle;
	int owner_pid;
	int owner_uid;
	unsigned int txn_count;
	char name[BINDER_MAX_NAME_LEN];
	char descriptor[BINDER_MAX_DESC_LEN];
};

/* Binder ioctl commands */
#define BINDER_WRITE_READ		0x6201
#define BINDER_SET_MAX_THREADS		0x6205
#define BINDER_SET_CONTEXT_MGR		0x6207
#define BINDER_VERSION			0x6209

/* Direct structured transaction ioctls (used by C daemons & /bin/service) */
#define BINDER_IOC_TRANSACT		0x6214
#define BINDER_IOC_RECV			0x6215
#define BINDER_IOC_REPLY		0x6216
#define BINDER_IOC_REGISTER_SVC		0x6217
#define BINDER_IOC_LOOKUP_SVC		0x6218
#define BINDER_IOC_LIST_SVCS		0x6219

#ifdef __KERNEL__
extern int binder_init(void);
extern int get_binder_info(char *buf);
#endif

#endif /* _LINUX_BINDER_H */
