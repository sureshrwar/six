
/*
 * Implementation of the diskquota system for the LINUX operating
 * system. QUOTA is implemented using the BSD systemcall interface as
 * the means of communication with the user level. Currently only the
 * ext2-filesystem has support for diskquotas. Other filesystems may
 * be added in future time. This file contains the generic routines
 * called by the different filesystems on allocation of an inode or
 * block. These routines take care of the administration needed to
 * have a consistent diskquota tracking system. The ideas of both
 * user and group quotas are based on the Melbourne quota system as
 * used on BSD derived systems. The internal implementation is
 * based on the LINUX inode-subsystem with added complexity of the
 * diskquota system. This implementation is not based on any BSD
 * kernel sourcecode.
 *
 * Version: $Id: dquot.c,v 1.1.1.1 2005/03/30 08:40:46 motorbreathing Exp $
 *
 * Author:  Marco van Wieringen <mvw@mcs.ow.nl> <mvw@tnix.net>
 *
 * Fixes:   Dmitry Gorodchanin <begemot@bgm.rosprint.net>, 11 Feb 96
 *          removed race conditions in dqput(), dqget() and iput().
 *
 * (C) Copyright 1994, 1995 Marco van Wieringen
 *
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/mount.h>

#include <asm/segment.h>

#define __DQUOT_VERSION__       "dquot_5.6.0"

static char quotamessage[MAX_QUOTA_MESSAGE];
static char *quotatypes[] = INITQFNAMES;

static int nr_dquots = 0, nr_free_dquots = 0;
static struct dquot *hash_table[NR_DQHASH];
static struct dquot *first_dquot;
static struct dqstats dqstats;

static struct wait_queue *dquot_wait = (struct wait_queue *)NULL;









void dquot_init(void)
{
        printk(KERN_NOTICE "VFS: Diskquotas version %s initialized\r\n",
               __DQUOT_VERSION__);
        memset(hash_table, 0, sizeof(hash_table));
        memset((caddr_t)&dqstats, 0, sizeof(dqstats));
        first_dquot = NODQUOT;
}



