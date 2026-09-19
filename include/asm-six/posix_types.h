
#ifndef __ARCH_I386_POSIX_TYPES_H
#define __ARCH_I386_POSIX_TYPES_H

/*
 * This file is generally used by user-level software, so you need to
 * be a little careful about namespace pollution etc.  Also, we cannot
 * assume GCC is being used.
 */

typedef unsigned short	__kernel_dev_t;
typedef unsigned long	__kernel_ino_t;
typedef unsigned short	__kernel_mode_t;
typedef unsigned short	__kernel_nlink_t;
typedef long		__kernel_off_t;
typedef int		__kernel_pid_t;
typedef unsigned short	__kernel_uid_t;
typedef unsigned short	__kernel_gid_t;
typedef unsigned int	__kernel_size_t;
typedef int		__kernel_ssize_t;
typedef int		__kernel_ptrdiff_t;
typedef long		__kernel_time_t;
typedef long		__kernel_clock_t;
typedef int		__kernel_daddr_t;
typedef char *		__kernel_caddr_t;

#ifdef __GNUC__
typedef long long	__kernel_loff_t;
#endif

typedef struct {
#if defined(__KERNEL__) || defined(__USE_ALL)
	int	val[2];
#else /* !defined(__KERNEL__) && !defined(__USE_ALL) */
	int	__val[2];
#endif /* !defined(__KERNEL__) && !defined(__USE_ALL) */
} __kernel_fsid_t;

#if (SIX)
#undef	__FD_SET
static inline void __FD_SET(unsigned long fd, __kernel_fd_set *fdsetp)
{       
        unsigned long _tmp = fd / __NFDBITS;
        unsigned long _rem = fd % __NFDBITS;
        fdsetp->fds_bits[_tmp] |= (1UL<<_rem);
}

#undef __FD_CLR
static inline void __FD_CLR(unsigned long fd, __kernel_fd_set *fdsetp)
{
        unsigned long _tmp = fd / __NFDBITS;
        unsigned long _rem = fd % __NFDBITS;
        fdsetp->fds_bits[_tmp] &= ~(1UL<<_rem);
}
#undef __FD_ISSET
static __inline__ int __FD_ISSET(unsigned long fd, const __kernel_fd_set *p)
{
        unsigned long _tmp = fd / __NFDBITS;
        unsigned long _rem = fd % __NFDBITS;
        return (p->fds_bits[_tmp] & (1UL<<_rem)) != 0;
}
/*
 * This will unroll the loop for the normal constant case (8 ints,
 * for a 256-bit fd_set)
 */
#undef __FD_ZERO
static __inline__ void __FD_ZERO(__kernel_fd_set *p)
{
        unsigned long *tmp = p->fds_bits;
        int i;

        if (__builtin_constant_p(__FDSET_LONGS)) {
                switch (__FDSET_LONGS) {
                      case 16:
                        tmp[ 0] = 0; tmp[ 1] = 0; tmp[ 2] = 0; tmp[ 3] = 0;
                        tmp[ 4] = 0; tmp[ 5] = 0; tmp[ 6] = 0; tmp[ 7] = 0;
                        tmp[ 8] = 0; tmp[ 9] = 0; tmp[10] = 0; tmp[11] = 0;
                        tmp[12] = 0; tmp[13] = 0; tmp[14] = 0; tmp[15] = 0;
                        return;

                      case 8:
                        tmp[ 0] = 0; tmp[ 1] = 0; tmp[ 2] = 0; tmp[ 3] = 0;
                        tmp[ 4] = 0; tmp[ 5] = 0; tmp[ 6] = 0; tmp[ 7] = 0;
                        return;

                      case 4:
                        tmp[ 0] = 0; tmp[ 1] = 0; tmp[ 2] = 0; tmp[ 3] = 0;
                        return;
                }
        }
        i = __FDSET_LONGS;
        while (i) {
                i--;
                *tmp = 0;
                tmp++;
        }
}
#else
#define __FD_SET(fd,fdsetp) \
		__asm__ __volatile__("btsl %1,%0": \
			"=m" (*(__kernel_fd_set *) (fdsetp)):"r" ((int) (fd)))

#undef	__FD_CLR
#define __FD_CLR(fd,fdsetp) \
		__asm__ __volatile__("btrl %1,%0": \
			"=m" (*(__kernel_fd_set *) (fdsetp)):"r" ((int) (fd)))
#undef	__FD_ISSET
#define __FD_ISSET(fd,fdsetp) (__extension__ ({ \
		unsigned char __result; \
		__asm__ __volatile__("btl %1,%2 ; setb %0" \
			:"=q" (__result) :"r" ((int) (fd)), \
			"m" (*(__kernel_fd_set *) (fdsetp))); \
		__result; }))

#undef	__FD_ZERO
#define __FD_ZERO(fdsetp) \
		__asm__ __volatile__("cld ; rep ; stosl" \
			:"=m" (*(__kernel_fd_set *) (fdsetp)) \
			:"a" (0), "c" (__FDSET_LONGS), \
			"D" ((__kernel_fd_set *) (fdsetp)) :"cx","di")

#endif
#endif

