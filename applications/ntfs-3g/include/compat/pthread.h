#ifndef _COMPAT_PTHREAD_H
#define _COMPAT_PTHREAD_H
#include <time.h>
typedef int pthread_t;
typedef int pthread_mutex_t;
typedef int pthread_mutexattr_t;
typedef int pthread_rwlock_t;
typedef int pthread_rwlockattr_t;
typedef int pthread_cond_t;
typedef int pthread_condattr_t;
typedef int pthread_key_t;
#define PTHREAD_MUTEX_INITIALIZER 0
#define PTHREAD_RWLOCK_INITIALIZER 0
#define PTHREAD_COND_INITIALIZER 0
#define PTHREAD_MUTEX_ADAPTIVE_NP 0

int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a);
int pthread_mutex_destroy(pthread_mutex_t *m);
int pthread_mutex_lock(pthread_mutex_t *m);
int pthread_mutex_unlock(pthread_mutex_t *m);
int pthread_mutexattr_init(pthread_mutexattr_t *a);
int pthread_mutexattr_settype(pthread_mutexattr_t *a, int t);
int pthread_mutexattr_destroy(pthread_mutexattr_t *a);

int pthread_rwlock_init(pthread_rwlock_t *l, const pthread_rwlockattr_t *a);
int pthread_rwlock_destroy(pthread_rwlock_t *l);
int pthread_rwlock_rdlock(pthread_rwlock_t *l);
int pthread_rwlock_wrlock(pthread_rwlock_t *l);
int pthread_rwlock_unlock(pthread_rwlock_t *l);

int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a);
int pthread_cond_destroy(pthread_cond_t *c);
int pthread_cond_broadcast(pthread_cond_t *c);
int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const struct timespec *t);

int pthread_key_create(pthread_key_t *key, void (*destr)(void *));
int pthread_key_delete(pthread_key_t key);
void *pthread_getspecific(pthread_key_t key);
int pthread_setspecific(pthread_key_t key, const void *val);

pthread_t pthread_self(void);
int pthread_kill(pthread_t thread, int sig);
#endif
