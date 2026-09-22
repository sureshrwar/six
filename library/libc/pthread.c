#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define PTHREAD_MAX_THREADS     64
#define PTHREAD_STACK_DEFAULT   32768
#define PTHREAD_STACK_MIN_SIZE  8192

#define PTHREAD_STATE_UNUSED    0
#define PTHREAD_STATE_RUNNING   1
#define PTHREAD_STATE_EXITED    2

struct pthread_tcb {
	volatile int used;
	volatile int state;
	int detached;
	int tid;
	void *(*start_routine)(void *);
	void *arg;
	void *retval;
	void *stack_base;
	size_t stack_size;
	void *tls[PTHREAD_KEYS_MAX];
};

static struct pthread_tcb tcb_table[PTHREAD_MAX_THREADS];
static volatile int pthread_global_lock = 0;

static int key_used[PTHREAD_KEYS_MAX];
static void (*key_dtor[PTHREAD_KEYS_MAX])(void *);
static void *main_tls[PTHREAD_KEYS_MAX];

static inline int atomic_xchg(volatile int *ptr, int val)
{
#if defined(__i386__)
	__asm__ __volatile__("xchgl %0, %1"
			     : "=r"(val), "+m"(*ptr)
			     : "0"(val)
			     : "memory");
	return val;
#else
	int old = *ptr;
	*ptr = val;
	return old;
#endif
}

static void lock_global(void)
{
	while (atomic_xchg(&pthread_global_lock, 1) != 0)
		sched_yield();
}

static void unlock_global(void)
{
	atomic_xchg(&pthread_global_lock, 0);
}

static struct pthread_tcb *find_tcb_by_tid(int tid)
{
	int i;
	for (i = 0; i < PTHREAD_MAX_THREADS; i++) {
		if (tcb_table[i].used && tcb_table[i].tid == tid)
			return &tcb_table[i];
	}
	return NULL;
}

static void run_tls_destructors(struct pthread_tcb *tcb)
{
	int k, pass;
	if (!tcb)
		return;
	for (pass = 0; pass < 4; pass++) {
		int any = 0;
		for (k = 0; k < PTHREAD_KEYS_MAX; k++) {
			void (*dtor)(void *) = NULL;
			void *val = NULL;
			lock_global();
			if (key_used[k] && key_dtor[k] && tcb->tls[k]) {
				dtor = key_dtor[k];
				val = tcb->tls[k];
				tcb->tls[k] = NULL;
			}
			unlock_global();
			if (dtor && val) {
				dtor(val);
				any = 1;
			}
		}
		if (!any)
			break;
	}
}

static void reap_detached_zombies_locked(void)
{
	int i, status;
	for (i = 0; i < PTHREAD_MAX_THREADS; i++) {
		if (tcb_table[i].used && tcb_table[i].detached &&
		    tcb_table[i].state == PTHREAD_STATE_EXITED) {
			if (tcb_table[i].tid > 0)
				waitpid(tcb_table[i].tid, &status, WNOHANG);
			if (tcb_table[i].stack_base) {
				free(tcb_table[i].stack_base);
				tcb_table[i].stack_base = NULL;
			}
			tcb_table[i].used = 0;
			tcb_table[i].state = PTHREAD_STATE_UNUSED;
			tcb_table[i].tid = 0;
		}
	}
}

static int pthread_entry_trampoline(void *arg)
{
	struct pthread_tcb *tcb = (struct pthread_tcb *)arg;
	void *ret = NULL;

	if (!tcb->tid)
		tcb->tid = getpid();

	if (tcb->start_routine)
		ret = tcb->start_routine(tcb->arg);

	pthread_exit(ret);
	return 0;
}

pthread_t pthread_self(void)
{
	return (pthread_t)getpid();
}

int pthread_equal(pthread_t t1, pthread_t t2)
{
	return (t1 == t2);
}

int pthread_yield(void)
{
	return sched_yield();
}

int pthread_kill(pthread_t thread, int sig)
{
	if (thread <= 0)
		return ESRCH;
	if (kill((pid_t)thread, sig) < 0)
		return errno;
	return 0;
}

void pthread_exit(void *retval)
{
	int tid = getpid();
	struct pthread_tcb *tcb;

	lock_global();
	tcb = find_tcb_by_tid(tid);
	unlock_global();

	if (tcb) {
		run_tls_destructors(tcb);
		lock_global();
		tcb->retval = retval;
		tcb->state = PTHREAD_STATE_EXITED;
		unlock_global();
	}
	_exit(0);
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg)
{
	struct pthread_tcb *tcb = NULL;
	size_t stacksize = PTHREAD_STACK_DEFAULT;
	int detached = 0;
	void *stack_base;
	int i, tid, flags;

	if (!thread || !start_routine)
		return EINVAL;

	if (attr) {
		if (attr->stacksize >= PTHREAD_STACK_MIN_SIZE)
			stacksize = attr->stacksize;
		if (attr->detachstate == PTHREAD_CREATE_DETACHED)
			detached = 1;
	}

	lock_global();
	reap_detached_zombies_locked();
	for (i = 0; i < PTHREAD_MAX_THREADS; i++) {
		if (!tcb_table[i].used) {
			tcb = &tcb_table[i];
			break;
		}
	}
	if (!tcb) {
		unlock_global();
		return EAGAIN;
	}

	stack_base = malloc(stacksize);
	if (!stack_base) {
		unlock_global();
		return ENOMEM;
	}

	memset(tcb, 0, sizeof(*tcb));
	tcb->used = 1;
	tcb->state = PTHREAD_STATE_RUNNING;
	tcb->detached = detached;
	tcb->start_routine = start_routine;
	tcb->arg = arg;
	tcb->retval = NULL;
	tcb->stack_base = stack_base;
	tcb->stack_size = stacksize;

	flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | SIGCHLD;
	tid = clone(pthread_entry_trampoline,
	            (char *)stack_base + stacksize,
	            flags,
	            (void *)tcb);
	if (tid < 0) {
		int err = errno ? errno : EAGAIN;
		free(stack_base);
		memset(tcb, 0, sizeof(*tcb));
		unlock_global();
		return err;
	}

	tcb->tid = tid;
	*thread = (pthread_t)tid;
	unlock_global();
	return 0;
}

int pthread_join(pthread_t thread, void **retval)
{
	struct pthread_tcb *tcb;
	int status = 0;

	if (thread <= 0)
		return ESRCH;
	if (thread == pthread_self())
		return EDEADLK;

	lock_global();
	tcb = find_tcb_by_tid((int)thread);
	if (!tcb) {
		unlock_global();
		return ESRCH;
	}
	if (tcb->detached) {
		unlock_global();
		return EINVAL;
	}
	unlock_global();

	int reaped = 0;

	while (tcb->state == PTHREAD_STATE_RUNNING) {
		int r = waitpid((pid_t)thread, &status, 0);
		if (r == (int)thread) {
			reaped = 1;
			break;
		}
		if (r < 0 && errno == ECHILD) {
			reaped = 1;
			if (tcb->state != PTHREAD_STATE_RUNNING)
				break;
			sched_yield();
		} else {
			sched_yield();
		}
	}

	/* Ensure the kernel task has fully exited its stack before freeing */
	if (!reaped)
		waitpid((pid_t)thread, &status, 0);

	lock_global();
	if (retval)
		*retval = tcb->retval;
	if (tcb->stack_base) {
		free(tcb->stack_base);
		tcb->stack_base = NULL;
	}
	tcb->used = 0;
	tcb->state = PTHREAD_STATE_UNUSED;
	tcb->tid = 0;
	unlock_global();
	return 0;
}

int pthread_detach(pthread_t thread)
{
	struct pthread_tcb *tcb;

	if (thread <= 0)
		return ESRCH;
	lock_global();
	tcb = find_tcb_by_tid((int)thread);
	if (!tcb) {
		unlock_global();
		return ESRCH;
	}
	tcb->detached = 1;
	reap_detached_zombies_locked();
	unlock_global();
	return 0;
}

/* Thread attributes */
int pthread_attr_init(pthread_attr_t *attr)
{
	if (!attr)
		return EINVAL;
	attr->stacksize = PTHREAD_STACK_DEFAULT;
	attr->detachstate = PTHREAD_CREATE_JOINABLE;
	return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr)
{
	if (!attr)
		return EINVAL;
	return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate)
{
	if (!attr || (detachstate != PTHREAD_CREATE_JOINABLE &&
	              detachstate != PTHREAD_CREATE_DETACHED))
		return EINVAL;
	attr->detachstate = detachstate;
	return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate)
{
	if (!attr || !detachstate)
		return EINVAL;
	*detachstate = attr->detachstate;
	return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize)
{
	if (!attr || stacksize < PTHREAD_STACK_MIN_SIZE)
		return EINVAL;
	attr->stacksize = stacksize;
	return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize)
{
	if (!attr || !stacksize)
		return EINVAL;
	*stacksize = attr->stacksize;
	return 0;
}

/* Mutexes */
int pthread_mutexattr_init(pthread_mutexattr_t *attr)
{
	if (!attr)
		return EINVAL;
	attr->type = PTHREAD_MUTEX_NORMAL;
	return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr)
{
	return attr ? 0 : EINVAL;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type)
{
	if (!attr || type < PTHREAD_MUTEX_NORMAL || type > PTHREAD_MUTEX_ERRORCHECK)
		return EINVAL;
	attr->type = type;
	return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type)
{
	if (!attr || !type)
		return EINVAL;
	*type = attr->type;
	return 0;
}

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
	if (!mutex)
		return EINVAL;
	mutex->locked = 0;
	mutex->owner = 0;
	mutex->count = 0;
	mutex->type = attr ? attr->type : PTHREAD_MUTEX_NORMAL;
	return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
	if (!mutex)
		return EINVAL;
	if (mutex->locked)
		return EBUSY;
	return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
	int self;
	if (!mutex)
		return EINVAL;
	self = getpid();
	if (mutex->locked && mutex->owner == self) {
		if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
			mutex->count++;
			return 0;
		}
		if (mutex->type == PTHREAD_MUTEX_ERRORCHECK)
			return EDEADLK;
	}
	while (atomic_xchg(&mutex->locked, 1) != 0)
		sched_yield();
	mutex->owner = self;
	mutex->count = 1;
	return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
	int self;
	if (!mutex)
		return EINVAL;
	self = getpid();
	if (mutex->locked && mutex->owner == self &&
	    mutex->type == PTHREAD_MUTEX_RECURSIVE) {
		mutex->count++;
		return 0;
	}
	if (atomic_xchg(&mutex->locked, 1) != 0)
		return EBUSY;
	mutex->owner = self;
	mutex->count = 1;
	return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
	if (!mutex)
		return EINVAL;
	if (!mutex->locked)
		return EPERM;
	if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->count > 1) {
		mutex->count--;
		return 0;
	}
	mutex->owner = 0;
	mutex->count = 0;
	atomic_xchg(&mutex->locked, 0);
	return 0;
}

/* Condition variables */
int pthread_condattr_init(pthread_condattr_t *attr)
{
	if (!attr)
		return EINVAL;
	attr->dummy = 0;
	return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr)
{
	return attr ? 0 : EINVAL;
}

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
	(void)attr;
	if (!cond)
		return EINVAL;
	cond->tickets = 0;
	cond->waiters = 0;
	cond->bcast_gen = 0;
	return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond)
{
	if (!cond)
		return EINVAL;
	if (cond->waiters > 0)
		return EBUSY;
	return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
	int my_bcast;
	if (!cond || !mutex)
		return EINVAL;

	lock_global();
	my_bcast = cond->bcast_gen;
	cond->waiters++;
	unlock_global();

	pthread_mutex_unlock(mutex);

	for (;;) {
		int woke = 0;
		lock_global();
		if (cond->bcast_gen != my_bcast) {
			woke = 1;
		} else if (cond->tickets > 0) {
			cond->tickets--;
			woke = 1;
		}
		if (woke) {
			if (cond->waiters > 0)
				cond->waiters--;
			unlock_global();
			break;
		}
		unlock_global();
		sched_yield();
	}

	pthread_mutex_lock(mutex);
	return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime)
{
	(void)abstime;
	return pthread_cond_wait(cond, mutex);
}

int pthread_cond_signal(pthread_cond_t *cond)
{
	if (!cond)
		return EINVAL;
	lock_global();
	if (cond->waiters > cond->tickets)
		cond->tickets++;
	unlock_global();
	return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond)
{
	if (!cond)
		return EINVAL;
	lock_global();
	if (cond->waiters > 0) {
		cond->bcast_gen++;
		cond->tickets = 0;
	}
	unlock_global();
	return 0;
}

/* Read-write locks */
int pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr)
{
	(void)attr;
	if (!rwlock)
		return EINVAL;
	rwlock->state = 0;
	rwlock->writer_tid = 0;
	return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock)
{
	if (!rwlock)
		return EINVAL;
	return (rwlock->state == 0) ? 0 : EBUSY;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
	if (!rwlock)
		return EINVAL;
	for (;;) {
		lock_global();
		if (rwlock->state >= 0) {
			rwlock->state++;
			unlock_global();
			return 0;
		}
		unlock_global();
		sched_yield();
	}
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock)
{
	if (!rwlock)
		return EINVAL;
	lock_global();
	if (rwlock->state >= 0) {
		rwlock->state++;
		unlock_global();
		return 0;
	}
	unlock_global();
	return EBUSY;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
	if (!rwlock)
		return EINVAL;
	for (;;) {
		lock_global();
		if (rwlock->state == 0) {
			rwlock->state = -1;
			rwlock->writer_tid = getpid();
			unlock_global();
			return 0;
		}
		unlock_global();
		sched_yield();
	}
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock)
{
	if (!rwlock)
		return EINVAL;
	lock_global();
	if (rwlock->state == 0) {
		rwlock->state = -1;
		rwlock->writer_tid = getpid();
		unlock_global();
		return 0;
	}
	unlock_global();
	return EBUSY;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
	if (!rwlock)
		return EINVAL;
	lock_global();
	if (rwlock->state < 0) {
		rwlock->state = 0;
		rwlock->writer_tid = 0;
	} else if (rwlock->state > 0) {
		rwlock->state--;
	}
	unlock_global();
	return 0;
}

/* Barriers */
int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned int count)
{
	(void)attr;
	if (!barrier || count == 0)
		return EINVAL;
	barrier->count = 0;
	barrier->total = (int)count;
	barrier->phase = 0;
	return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier)
{
	return barrier ? 0 : EINVAL;
}

int pthread_barrier_wait(pthread_barrier_t *barrier)
{
	int my_phase;
	if (!barrier || barrier->total <= 0)
		return EINVAL;

	lock_global();
	my_phase = barrier->phase;
	barrier->count++;
	if (barrier->count >= barrier->total) {
		barrier->count = 0;
		barrier->phase++;
		unlock_global();
		return PTHREAD_BARRIER_SERIAL_THREAD;
	}
	unlock_global();

	while (barrier->phase == my_phase)
		sched_yield();
	return 0;
}

/* One-time init & TLS keys */
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void))
{
	if (!once_control || !init_routine)
		return EINVAL;
	if (*once_control == 2)
		return 0;
	lock_global();
	if (*once_control == 0) {
		*once_control = 1;
		unlock_global();
		init_routine();
		lock_global();
		*once_control = 2;
		unlock_global();
		return 0;
	}
	unlock_global();
	while (*once_control != 2)
		sched_yield();
	return 0;
}

int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
	int k, i;
	if (!key)
		return EINVAL;
	lock_global();
	for (k = 0; k < PTHREAD_KEYS_MAX; k++) {
		if (!key_used[k]) {
			key_used[k] = 1;
			key_dtor[k] = destructor;
			main_tls[k] = NULL;
			for (i = 0; i < PTHREAD_MAX_THREADS; i++)
				tcb_table[i].tls[k] = NULL;
			*key = k;
			unlock_global();
			return 0;
		}
	}
	unlock_global();
	return EAGAIN;
}

int pthread_key_delete(pthread_key_t key)
{
	if (key < 0 || key >= PTHREAD_KEYS_MAX)
		return EINVAL;
	lock_global();
	if (!key_used[key]) {
		unlock_global();
		return EINVAL;
	}
	key_used[key] = 0;
	key_dtor[key] = NULL;
	unlock_global();
	return 0;
}

int pthread_setspecific(pthread_key_t key, const void *value)
{
	struct pthread_tcb *tcb;
	if (key < 0 || key >= PTHREAD_KEYS_MAX)
		return EINVAL;
	lock_global();
	if (!key_used[key]) {
		unlock_global();
		return EINVAL;
	}
	tcb = find_tcb_by_tid(getpid());
	if (tcb)
		tcb->tls[key] = (void *)value;
	else
		main_tls[key] = (void *)value;
	unlock_global();
	return 0;
}

void *pthread_getspecific(pthread_key_t key)
{
	struct pthread_tcb *tcb;
	void *val;
	if (key < 0 || key >= PTHREAD_KEYS_MAX)
		return NULL;
	lock_global();
	if (!key_used[key]) {
		unlock_global();
		return NULL;
	}
	tcb = find_tcb_by_tid(getpid());
	val = tcb ? tcb->tls[key] : main_tls[key];
	unlock_global();
	return val;
}
