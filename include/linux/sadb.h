#ifndef _LINUX_SADB_H
#define _LINUX_SADB_H

/*
 * SIX Android Debug Bridge (sadb / sadbd) kernel character device definitions.
 *
 * Device node: /dev/sadb (char major 61, minor 0)
 * Procfs node: /proc/sadb
 */

#define SADB_CHAR_MAJOR		61
#define SADB_CHAR_MINOR		0
#define SADB_MAX_SLOTS		16

/* ioctl commands on /dev/sadb */
#define SADB_IOC_MAGIC		'S'
#define SADB_IOC_ACCEPT		0x5301	/* Block until a host sadb client connects; attach filp */
#define SADB_IOC_GET_SERIAL	0x5302	/* Copy emulator serial string (char[32]) to user */
#define SADB_IOC_GET_PORT	0x5303	/* Return host TCP listen port */

#ifdef __KERNEL__
int  sadb_dev_init(void);
void sadb_dev_poll(void);
int  sadb_get_proc_info(char *buf);
#endif

#endif /* _LINUX_SADB_H */
