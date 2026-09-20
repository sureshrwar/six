#ifndef __ARCH_I386_ATOMIC__
#define __ARCH_I386_ATOMIC__

typedef int atomic_t;

#define atomic_add(i, v) (*(v) += (i))
#define atomic_sub(i, v) (*(v) -= (i))
#define atomic_inc(v)    (*(v))++
#define atomic_dec(v)    (*(v))--

static inline int atomic_dec_and_test(atomic_t *v) 
{
	return (--(*v) == 0);
}

#endif
