#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#define NUM_THREADS 4
#define ITERS_PER_THREAD 250

static pthread_mutex_t counter_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t start_cond = PTHREAD_COND_INITIALIZER;
static pthread_barrier_t phase_barrier;
static pthread_once_t once_ctrl = PTHREAD_ONCE_INIT;
static pthread_key_t tls_key;

static int start_ready = 0;
static int shared_counter = 0;
static int once_called = 0;

static void init_once_fn(void)
{
	once_called++;
}

static void *worker_fn(void *arg)
{
	long id = (long)arg;
	int i;

	pthread_once(&once_ctrl, init_once_fn);
	pthread_setspecific(tls_key, (void *)(id * 100 + 7));

	/* Wait for main thread's broadcast start signal */
	pthread_mutex_lock(&counter_lock);
	while (!start_ready)
		pthread_cond_wait(&start_cond, &counter_lock);
	pthread_mutex_unlock(&counter_lock);

	for (i = 0; i < ITERS_PER_THREAD; i++) {
		pthread_mutex_lock(&counter_lock);
		shared_counter++;
		pthread_mutex_unlock(&counter_lock);
		if ((i & 63) == 0)
			sched_yield();
	}

	/* Synchronize all worker threads at barrier */
	pthread_barrier_wait(&phase_barrier);

	printf("  [Thread %ld | tid=%d] TLS=%ld shared_counter=%d\n",
	       id, (int)pthread_self(),
	       (long)pthread_getspecific(tls_key),
	       shared_counter);

	return (void *)(id * 10 + 1);
}

int main(void)
{
	pthread_t threads[NUM_THREADS];
	long i;
	int ok = 1;

	printf("========================================\n");
	printf("   POSIX Threads (pthread) Demo on SIX  \n");
	printf("========================================\n");

	pthread_key_create(&tls_key, NULL);
	pthread_barrier_init(&phase_barrier, NULL, NUM_THREADS);

	for (i = 0; i < NUM_THREADS; i++) {
		int rc = pthread_create(&threads[i], NULL, worker_fn, (void *)(i + 1));
		if (rc != 0) {
			printf("pthread_create(%ld) failed: %d\n", i, rc);
			return 1;
		}
	}

	/* Signal all worker threads to begin */
	pthread_mutex_lock(&counter_lock);
	start_ready = 1;
	pthread_cond_broadcast(&start_cond);
	pthread_mutex_unlock(&counter_lock);

	for (i = 0; i < NUM_THREADS; i++) {
		void *ret = NULL;
		pthread_join(threads[i], &ret);
		printf("  [Main] Joined thread %ld (tid=%d) -> retval=%ld\n",
		       i + 1, (int)threads[i], (long)ret);
		if ((long)ret != (i + 1) * 10 + 1)
			ok = 0;
	}

	pthread_barrier_destroy(&phase_barrier);
	pthread_key_delete(tls_key);

	printf("----------------------------------------\n");
	printf("  Final shared_counter = %d (expected %d)\n",
	       shared_counter, NUM_THREADS * ITERS_PER_THREAD);
	printf("  pthread_once count   = %d (expected 1)\n", once_called);
	printf("  Result               = %s\n",
	       (ok && shared_counter == NUM_THREADS * ITERS_PER_THREAD && once_called == 1)
	       ? "PASS" : "FAIL");
	printf("========================================\n");
	return (ok && shared_counter == NUM_THREADS * ITERS_PER_THREAD) ? 0 : 1;
}
