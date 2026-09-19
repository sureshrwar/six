
/*
 * linux/ipc/sem.c
 * Copyright (C) 1992 Krishna Balasubramanian
 * Copyright (C) 1995 Eric Schenk, Bruno Haible
 *
 * IMPLEMENTATION NOTES ON CODE REWRITE (Eric Schenk, January 1995):
 * This code underwent a massive rewrite in order to solve some problems
 * with the original code. In particular the original code failed to
 * wake up processes that were waiting for semval to go to 0 if the
 * value went to 0 and was then incremented rapidly enough. In solving
 * this problem I have also modified the implementation so that it
 * processes pending operations in a FIFO manner, thus give a guarantee
 * that processes waiting for a lock on the semaphore won't starve
 * unless another locking process fails to unlock.
 * In addition the following two changes in behavior have been introduced:
 * - The original implementation of semop returned the value
 *   last semaphore element examined on success. This does not
 *   match the manual page specifications, and effectively
 *   allows the user to read the semaphore even if they do not
 *   have read permissions. The implementation now returns 0
 *   on success as stated in the manual page.
 * - There is some confusion over whether the set of undo adjustments
 *   to be performed at exit should be done in an atomic manner.
 *   That is, if we are attempting to decrement the semval should we queue
 *   up and wait until we can do so legally?
 *   The original implementation attempted to do this.
 *   The current implementation does not do so. This is because I don't
 *   think it is the right thing (TM) to do, and because I couldn't
 *   see a clean way to get the old behavior with the new design.
 *   The POSIX standard and SVID should be consulted to determine
 *   what behavior is mandated.
 */

#include <linux/errno.h>
#include <asm/segment.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/sem.h>
#include <linux/ipc.h>
#include <linux/stat.h>
#include <linux/malloc.h>

static int newary (key_t, int, int);
static int findkey (key_t key);
static void freeary (int id);

static struct semid_ds *semary[SEMMNI];
static int used_sems = 0, used_semids = 0;
static struct wait_queue *sem_lock = NULL;
static int max_semid = 0;

static unsigned short sem_seq = 0;

void sem_init (void)
{
        int i;

        sem_lock = NULL;
        used_sems = used_semids = max_semid = sem_seq = 0;
        for (i = 0; i < SEMMNI; i++)
                semary[i] = (struct semid_ds *) IPC_UNUSED;
        return;
}



/* Manage the doubly linked list sma->sem_pending as a FIFO:
 * insert new queue elements at the tail sma->sem_pending_last.
 */
static inline void insert_into_queue (struct semid_ds * sma, struct sem_queue * q)
{
        *(q->prev = sma->sem_pending_last) = q;
        *(sma->sem_pending_last = &q->next) = NULL;
}
static inline void remove_from_queue (struct semid_ds * sma, struct sem_queue * q)
{
        *(q->prev) = q->next;
        if (q->next)
                q->next->prev = q->prev;
        else /* sma->sem_pending_last == &q->next */
                sma->sem_pending_last = q->prev;
        q->prev = NULL; /* mark as removed */
}


/* Determine whether a sequence of semaphore operations would succeed
 * all at once. Return 0 if yes, 1 if need to sleep, else return error code.
 */
static int try_semop (struct semid_ds * sma, struct sembuf * sops, int nsops)
{
        int result = 0;
        int i = 0;

        while (i < nsops) {
                struct sembuf * sop = &sops[i];
                struct sem * curr = &sma->sem_base[sop->sem_num];
                if (sop->sem_op + curr->semval > SEMVMX) {
                        result = -ERANGE;
                        break;
                }
                if (!sop->sem_op && curr->semval) {
                        if (sop->sem_flg & IPC_NOWAIT)
                                result = -EAGAIN;
                        else
                                result = 1;
                        break;
                }
                i++;
                curr->semval += sop->sem_op;
                if (curr->semval < 0) {
                        if (sop->sem_flg & IPC_NOWAIT)
                                result = -EAGAIN;
                        else
                                result = 1;
                        break;
                }
        }
        while (--i >= 0) {
                struct sembuf * sop = &sops[i];
                struct sem * curr = &sma->sem_base[sop->sem_num];
                curr->semval -= sop->sem_op;
        }
        return result;
}


/* Actually perform a sequence of semaphore operations. Atomically. */
/* This assumes that try_semop() already returned 0. */
static int do_semop (struct semid_ds * sma, struct sembuf * sops, int nsops,
                     struct sem_undo * un, int pid)
{
        int i;

        for (i = 0; i < nsops; i++) {
                struct sembuf * sop = &sops[i];
                struct sem * curr = &sma->sem_base[sop->sem_num];
                if (sop->sem_op + curr->semval > SEMVMX) {
                        printk("do_semop: race\n");
                        break;
                }
                if (!sop->sem_op) {
                        if (curr->semval) {
                                printk("do_semop: race\n");
                                break;
                        }
                } else {
                        curr->semval += sop->sem_op;
                        if (curr->semval < 0) {
                                printk("do_semop: race\n");
                                break;
                        }
                        if (sop->sem_flg & SEM_UNDO)
                                un->semadj[sop->sem_num] -= sop->sem_op;
                }
                curr->sempid = pid;
        }
        sma->sem_otime = CURRENT_TIME;

        /* Previous implementation returned the last semaphore's semval.
         * This is wrong because we may not have checked read permission,
         * only write permission.
         */
        return 0;
}


/* Go through the pending queue for the indicated semaphore
 * looking for tasks that can be completed. Keep cycling through
 * the queue until a pass is made in which no process is woken up.
 */
static void update_queue (struct semid_ds * sma)
{
        int wokeup, error;
        struct sem_queue * q;

        do {
                wokeup = 0;
                for (q = sma->sem_pending; q; q = q->next) {
                        error = try_semop(sma, q->sops, q->nsops);
                        /* Does q->sleeper still need to sleep? */
                        if (error > 0)
                                continue;
                        /* Perform the operations the sleeper was waiting for */
                        if (!error)
                                error = do_semop(sma, q->sops, q->nsops, q->undo, q->pid);
                        q->status = error;
                        /* Remove it from the queue */
                        remove_from_queue(sma,q);
                        /* Wake it up */
                        wake_up_interruptible(&q->sleeper); /* doesn't sleep! */
                        wokeup++;
                }
        } while (wokeup);
}



/*
 * add semadj values to semaphores, free undo structures.
 * undo structures are not freed when semaphore arrays are destroyed
 * so some of them may be out of date.
 * IMPLEMENTATION NOTE: There is some confusion over whether the
 * set of adjustments that needs to be done should be done in an atomic
 * manner or not. That is, if we are attempting to decrement the semval
 * should we queue up and wait until we can do so legally?
 * The original implementation attempted to do this (queue and wait).
 * The current implementation does not do so. The POSIX standard
 * and SVID should be consulted to determine what behavior is mandated.
 */
void sem_exit (void)
{
        struct sem_queue *q;
        struct sem_undo *u, *un = NULL, **up, **unp;
        struct semid_ds *sma;
        int nsems, i;

        /* If the current process was sleeping for a semaphore,
         * remove it from the queue.
         */
        if ((q = current->semsleeping)) {
                if (q->prev)
                        remove_from_queue(q->sma,q);
                current->semsleeping = NULL;
        }

        for (up = &current->semundo; (u = *up); *up = u->proc_next, kfree(u)) {
                if (u->semid == -1)
                        continue;
                sma = semary[(unsigned int) u->semid % SEMMNI];
                if (sma == IPC_UNUSED || sma == IPC_NOID)
                        continue;
                if (sma->sem_perm.seq != (unsigned int) u->semid / SEMMNI)
                        continue;
                /* remove u from the sma->undo list */
                for (unp = &sma->undo; (un = *unp); unp = &un->id_next) {
                        if (u == un)
                                goto found;
                }
                printk ("sem_exit undo list error id=%d\n", u->semid);
                break;
found:
                *unp = un->id_next;
                /* perform adjustments registered in u */
                nsems = sma->sem_nsems;
                for (i = 0; i < nsems; i++) {
                        struct sem * sem = &sma->sem_base[i];
                        sem->semval += u->semadj[i];
                        if (sem->semval < 0)
                                sem->semval = 0; /* shouldn't happen */
                        sem->sempid = current->pid;
                }
                sma->sem_otime = CURRENT_TIME;
                /* maybe some queued-up processes were waiting for this */
                update_queue(sma);
        }
        current->semundo = NULL;
}

