#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <sys/types.h>
#include <sched.h>
#include <time.h>

#define PTHREAD_CREATE_JOINABLE         0
#define PTHREAD_CREATE_DETACHED         1

#define PTHREAD_MUTEX_NORMAL            0
#define PTHREAD_MUTEX_RECURSIVE         1
#define PTHREAD_MUTEX_ERRORCHECK        2
#define PTHREAD_MUTEX_DEFAULT           PTHREAD_MUTEX_NORMAL

#define PTHREAD_CANCELED                ((void *)-1)
#define PTHREAD_BARRIER_SERIAL_THREAD   (-1)
#define PTHREAD_ONCE_INIT               0
#define PTHREAD_KEYS_MAX                32

typedef int pthread_t;
typedef volatile int pthread_once_t;
typedef int pthread_key_t;

typedef struct pthread_attr {
	size_t stacksize;
	int detachstate;
} pthread_attr_t;

typedef struct pthread_mutexattr {
	int type;
} pthread_mutexattr_t;

typedef struct pthread_mutex {
	volatile int locked;
	int owner;
	int count;
	int type;
} pthread_mutex_t;

#define PTHREAD_MUTEX_INITIALIZER       { 0, 0, 0, 0 }

typedef struct pthread_condattr {
	int dummy;
} pthread_condattr_t;

typedef struct pthread_cond {
	volatile int tickets;
	volatile int waiters;
	volatile int bcast_gen;
} pthread_cond_t;

#define PTHREAD_COND_INITIALIZER        { 0, 0, 0 }

typedef struct pthread_rwlockattr {
	int dummy;
} pthread_rwlockattr_t;

typedef struct pthread_rwlock {
	volatile int state; /* 0 = unlocked, >0 = reader count, -1 = writer */
	int writer_tid;
} pthread_rwlock_t;

#define PTHREAD_RWLOCK_INITIALIZER      { 0, 0 }

typedef struct pthread_barrierattr {
	int dummy;
} pthread_barrierattr_t;

typedef struct pthread_barrier {
	volatile int count;
	volatile int total;
	volatile int phase;
} pthread_barrier_t;

/* Thread creation & lifecycle */
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);
int pthread_detach(pthread_t thread);
void pthread_exit(void *retval);
pthread_t pthread_self(void);
int pthread_equal(pthread_t t1, pthread_t t2);
int pthread_kill(pthread_t thread, int sig);
int pthread_yield(void);

/* Thread attributes */
int pthread_attr_init(pthread_attr_t *attr);
int pthread_attr_destroy(pthread_attr_t *attr);
int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate);
int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate);
int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize);
int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize);

/* Mutexes */
int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);
int pthread_mutexattr_init(pthread_mutexattr_t *attr);
int pthread_mutexattr_destroy(pthread_mutexattr_t *attr);
int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type);
int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type);

/* Condition variables */
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);
int pthread_condattr_init(pthread_condattr_t *attr);
int pthread_condattr_destroy(pthread_condattr_t *attr);

/* Read-write locks */
int pthread_rwlock_init(pthread_rwlock_t *rwlock, const pthread_rwlockattr_t *attr);
int pthread_rwlock_destroy(pthread_rwlock_t *rwlock);
int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock);
int pthread_rwlock_unlock(pthread_rwlock_t *rwlock);

/* Barriers */
int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr, unsigned int count);
int pthread_barrier_destroy(pthread_barrier_t *barrier);
int pthread_barrier_wait(pthread_barrier_t *barrier);

/* One-time initialization & Thread-specific data (TLS keys) */
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void));
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *));
int pthread_key_delete(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *value);
void *pthread_getspecific(pthread_key_t key);

#endif /* _PTHREAD_H */
