
#ifndef _ASM_SEGMENT_H
#define _ASM_SEGMENT_H

#define KERNEL_CS       0x10
#define KERNEL_DS       0x18

#define USER_CS         0x23
#define USER_DS         0x2B

/*
 * Uh, these should become the main single-value transfer routines..
 * They automatically use the right size if we just have the right
 * pointer type..
 */
#define put_user(x,ptr) __put_user((unsigned long)(x),(ptr),sizeof(*(ptr)))
#define get_user(ptr) ((__typeof__(*(ptr)))__get_user((ptr),sizeof(*(ptr))))

/*
 * This is a silly but good way to make sure that
 * the __put_user function is indeed always optimized,
 * and that we use the correct sizes..
 */
extern int bad_user_access_length(void);

static inline void __put_user(unsigned long x, void * y, int size)
{
	char c;
	short s;
	int i;
        switch (size) {
                case 1:
			//c = *((char *)&x);
			c = (char)x;
			*((char *)y) = c;
			break;
                case 2:
			//s = *((short *)&x);
			s = (short)x;
			*((short *)y) = s;
                        break;
                case 4:
			i = *((int *)&x);
			*((int *)y) = i;
                        break;
                default:
                        bad_user_access_length();
        }
}


static inline unsigned long  __get_user(void * y, int size)
{
	unsigned long result;
	char c;
	short s;
	int i;
        switch (size) {
                case 1:
			c = *((char *)y);
			result = c;
			break;
                case 2:
			s = *((short *)y);
			result = s;
                        break;
                case 4:
			i = *((int *)y);
			result = i;
                        break;
                default:
                        return bad_user_access_length();
        }
	return result;
}

#define memcpy_tofs(to, from, n) memcpy(to, from, n)
#define memcpy_fromfs(to, from, n) memcpy(to, from, n)

#define get_fs_byte(addr) __get_user((const unsigned char *)(addr),1)
#define get_fs_word(addr) __get_user((const unsigned short *)(addr),2)
#define get_fs_long(addr) __get_user((const unsigned int *)(addr),4)


#define put_fs_byte(x,addr) __put_user((x),(unsigned char *)(addr),1)
#define put_fs_word(x,addr) __put_user((x),(unsigned short *)(addr),2)
#define put_fs_long(x,addr) __put_user((x),(unsigned int *)(addr),4)


static inline int set_fs(unsigned long val)
{
	return 1;
}

static inline unsigned long get_fs(void)
{
	return KERNEL_DS;
}

static inline unsigned long get_ds(void)
{
	return KERNEL_DS;
}


#endif
