
/*
 * linux/ipc/msg.c
 * Copyright (C) 1992 Krishna Balasubramanian
 *
 * Kerneld extensions by Bjorn Ekwall <bj0rn@blox.se> in May 1995, and May 1996
 *
 * See <linux/kerneld.h> for the (optional) new kerneld protocol
 */

#include <solaris.h>

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/msg.h>
#include <linux/stat.h>
#include <linux/malloc.h>
#include <linux/kerneld.h>
#include <linux/interrupt.h>

#include <asm/segment.h>


static void freeque (int id);
static int newque (key_t key, int msgflg);
static int findkey (key_t key);

static struct msqid_ds *msgque[MSGMNI];
static int msgbytes = 0;
static int msghdrs = 0;
static unsigned short msg_seq = 0;
static int used_queues = 0;
static int max_msqid = 0;
static struct wait_queue *msg_lock = NULL;
static int kerneld_msqid = -1;

#define MAX_KERNELDS 20
static int kerneld_arr[MAX_KERNELDS];
static int n_kernelds = 0;


void msg_init (void)
{
        int id;

        for (id = 0; id < MSGMNI; id++)
                msgque[id] = (struct msqid_ds *) IPC_UNUSED;
        msgbytes = msghdrs = msg_seq = max_msqid = used_queues = 0;
        msg_lock = NULL;
        return;
}


asmlinkage int sys_msgctl (int msqid, int cmd, struct msqid_ds *buf)
{
        int id, err;
        struct msqid_ds *msq;
        struct msqid_ds tbuf;
        struct ipc_perm *ipcp;

        if (msqid < 0 || cmd < 0)
                return -EINVAL;
        switch (cmd) {
        case IPC_INFO:
        case MSG_INFO:
                if (!buf)
                        return -EFAULT;
        {
                struct msginfo msginfo;
                msginfo.msgmni = MSGMNI;
                msginfo.msgmax = MSGMAX;
                msginfo.msgmnb = MSGMNB;
                msginfo.msgmap = MSGMAP;
                msginfo.msgpool = MSGPOOL;
                msginfo.msgtql = MSGTQL;
                msginfo.msgssz = MSGSSZ;
                msginfo.msgseg = MSGSEG;
                if (cmd == MSG_INFO) {
                        msginfo.msgpool = used_queues;
                        msginfo.msgmap = msghdrs;
                        msginfo.msgtql = msgbytes;
                }
                err = verify_area (VERIFY_WRITE, buf, sizeof (struct msginfo));
                if (err)
                        return err;
                memcpy_tofs (buf, &msginfo, sizeof(struct msginfo));
                return max_msqid;
        }
        case MSG_STAT:
                if (!buf)
                        return -EFAULT;
                err = verify_area (VERIFY_WRITE, buf, sizeof (*buf));
                if (err)
                        return err;
                if (msqid > max_msqid)
                        return -EINVAL;
                msq = msgque[msqid];
                if (msq == IPC_UNUSED || msq == IPC_NOID)
                        return -EINVAL;
                if (ipcperms (&msq->msg_perm, S_IRUGO))
                        return -EACCES;
                id = (unsigned int) msq->msg_perm.seq * MSGMNI + msqid;
                tbuf.msg_perm   = msq->msg_perm;
                tbuf.msg_stime  = msq->msg_stime;
                tbuf.msg_rtime  = msq->msg_rtime;
                tbuf.msg_ctime  = msq->msg_ctime;
                tbuf.msg_cbytes = msq->msg_cbytes;
                tbuf.msg_qnum   = msq->msg_qnum;
                tbuf.msg_qbytes = msq->msg_qbytes;
                tbuf.msg_lspid  = msq->msg_lspid;
                tbuf.msg_lrpid  = msq->msg_lrpid;
                memcpy_tofs (buf, &tbuf, sizeof(*buf));
                return id;
        case IPC_SET:
                if (!buf)
                        return -EFAULT;
                err = verify_area (VERIFY_READ, buf, sizeof (*buf));
                if (err)
                        return err;
                memcpy_fromfs (&tbuf, buf, sizeof (*buf));
                break;
        case IPC_STAT:
                if (!buf)
                        return -EFAULT;
                err = verify_area (VERIFY_WRITE, buf, sizeof(*buf));
                if (err)
                        return err;
                break;
        }

        id = (unsigned int) msqid % MSGMNI;
        msq = msgque [id];
        if (msq == IPC_UNUSED || msq == IPC_NOID)
                return -EINVAL;
        if (msq->msg_perm.seq != (unsigned int) msqid / MSGMNI)
                return -EIDRM;
        ipcp = &msq->msg_perm;

        switch (cmd) {
        case IPC_STAT:
                if (ipcperms (ipcp, S_IRUGO))
                        return -EACCES;
                tbuf.msg_perm   = msq->msg_perm;
                tbuf.msg_stime  = msq->msg_stime;
                tbuf.msg_rtime  = msq->msg_rtime;
                tbuf.msg_ctime  = msq->msg_ctime;
                tbuf.msg_cbytes = msq->msg_cbytes;
                tbuf.msg_qnum   = msq->msg_qnum;
                tbuf.msg_qbytes = msq->msg_qbytes;
                tbuf.msg_lspid  = msq->msg_lspid;
                tbuf.msg_lrpid  = msq->msg_lrpid;
                memcpy_tofs (buf, &tbuf, sizeof (*buf));
                return 0;
        case IPC_SET:
                if (!suser() && current->euid != ipcp->cuid &&
                    current->euid != ipcp->uid)
                        return -EPERM;
                if (tbuf.msg_qbytes > MSGMNB && !suser())
                        return -EPERM;
                msq->msg_qbytes = tbuf.msg_qbytes;
                ipcp->uid = tbuf.msg_perm.uid;
                ipcp->gid =  tbuf.msg_perm.gid;
                ipcp->mode = (ipcp->mode & ~S_IRWXUGO) |
                        (S_IRWXUGO & tbuf.msg_perm.mode);
                msq->msg_ctime = CURRENT_TIME;
                return 0;
        case IPC_RMID:
                if (!suser() && current->euid != ipcp->cuid &&
                    current->euid != ipcp->uid)
                        return -EPERM;
                /*
                 * There is only one kerneld message queue,
                 * mark it as non-existent
                 */
                if ((kerneld_msqid >= 0) && (msqid == kerneld_msqid))
                        kerneld_msqid = -1;
                freeque (id);
                return 0;
        default:
                return -EINVAL;
        }
}



/*
 * We do perhaps need a "flush" for waiting processes,
 * so that if they are terminated, a call from do_exit
 * will minimize the possibility of orphaned received
 * messages in the queue.  For now we just make sure
 * that the queue is shut down whenever all kernelds have died.
 */
void kerneld_exit(void)
{
        int i;

        if (kerneld_msqid == -1)
                return;
        for (i = 0; i < MAX_KERNELDS; ++i) {
                if (kerneld_arr[i] == current->pid) {
                        kerneld_arr[i] = 0;
                        --n_kernelds;
                        if (n_kernelds == 0)
                                sys_msgctl(kerneld_msqid, IPC_RMID, NULL);
                        break;
                }
        }
}

