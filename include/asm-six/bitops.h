
#ifndef _I386_BITOPS_H
#define _I386_BITOPS_H

/*
 * This stuff gave me a lot of trouble. All these are borrowed from 
 * sparc specific code in linux. Ie, from include/asm-sparc/bitops.h.
 * Take a look at, say, the two versions of set_bit() and ext2_set_bit(),
 * and notice the subtle(?) difference.
 */


#define SMPVOL
#define LOCK_PREFIX ""

#if (!__i386__)
#define BIT(n) 1<<(n&0x1F)
typedef unsigned long BITFIELD;
#else
struct __dummy { unsigned long a[100]; };
#define ADDR (*(struct __dummy *) addr)
#define CONST_ADDR (*(const struct __dummy *) addr)
#endif

#if (__i386__)
static int set_bit(int nr, SMPVOL void * addr)
{ 
        int oldbit;
  
        __asm__ (LOCK_PREFIX
                "btsl %2,%1\n\tsbbl %0,%0"
                :"=r" (oldbit),"=m" (ADDR)
                :"ir" (nr));   
        return oldbit;
} 
#else
static inline set_bit(int nr, SMPVOL void  *addr) {
        int mask, flags;
        unsigned long *ADDR = (unsigned long *) addr;
        unsigned long oldbit;

        ADDR += nr >> 5;
        mask = 1 << (nr & 31);
        oldbit = (mask & *ADDR);
        *ADDR |= mask;
        return oldbit != 0;
}

static inline ext2_set_bit(int nr,void * addr)
{
        int             mask, retval, flags;
        unsigned char   *ADDR = (unsigned char *) addr;

        ADDR += nr >> 3;
        mask = 1 << (nr & 0x07);
        retval = (mask & *ADDR) != 0;
        *ADDR |= mask;
        return retval;
}
#endif

#if (__i386__)
static inline int clear_bit(int nr, SMPVOL void * addr)
{              
        int oldbit;
               
        __asm__ (LOCK_PREFIX
                "btrl %2,%1\n\tsbbl %0,%0"
                :"=r" (oldbit),"=m" (ADDR)
                :"ir" (nr));
        return oldbit;
}
#else
static inline unsigned long clear_bit(unsigned long nr, SMPVOL void *addr)
{
        int mask, flags;
        unsigned long *ADDR = (unsigned long *) addr;
        unsigned long oldbit;

        ADDR += nr >> 5;
        mask = 1 << (nr & 31);
        oldbit = (mask & *ADDR);
        *ADDR &= ~mask;
        return oldbit != 0;
}

static inline int ext2_clear_bit(int nr, void * addr)
{
        int             mask, retval, flags;
        unsigned char   *ADDR = (unsigned char *) addr;

        ADDR += nr >> 3;
        mask = 1 << (nr & 0x07);
        retval = (mask & *ADDR) != 0;
        *ADDR &= ~mask;
        return retval;
}
#endif

#if (__i386__)
static inline int change_bit(int nr, SMPVOL void * addr)
{
        int oldbit;

        __asm__ (LOCK_PREFIX
                "btcl %2,%1\n\tsbbl %0,%0"
                :"=r" (oldbit),"=m" (ADDR)
                :"ir" (nr));
        return oldbit;
}
#else
static inline  unsigned long change_bit(unsigned long nr, SMPVOL void *addr)
{
        int mask, flags;
        unsigned long *ADDR = (unsigned long *) addr;
        unsigned long oldbit;

        ADDR += nr >> 5;
        mask = 1 << (nr & 31);
        oldbit = (mask & *ADDR);
        *ADDR ^= mask;
        return oldbit != 0;
}
#endif

static inline unsigned long test_bit(int nr, const SMPVOL void *addr)
{
        return ((1UL << (nr & 31)) & (((const unsigned int *) addr)[nr >> 5])) != 0;
}

#if (!__i386__)
static inline int ext2_test_bit(int nr, const void * addr)
{
        int                     mask;
        const unsigned char     *ADDR = (const unsigned char *) addr;

        ADDR += nr >> 3;
        mask = 1 << (nr & 0x07);
        return ((mask & *ADDR) != 0);
}
#endif

#if (__i386__)
static inline unsigned long ffz(unsigned long word)
{
        __asm__("bsfl %1,%0"
                :"=r" (word)
                :"r" (~word));
        return word;
}
#else
/* The easy/cheese version for now. */
static inline  unsigned long ffz(unsigned long word)
{       
        unsigned long result = 0;
        
        while(word & 1) {
                result++;
                word >>= 1;
        }       
        return result; 
}       
#endif



#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

#define ext2_find_first_zero_bit(addr, size) \
        ext2_find_next_zero_bit((addr), (size), 0)


static inline unsigned long ext2_find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
        unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
        unsigned long result = offset & ~31UL;
        unsigned long tmp;

        if (offset >= size)
                return size;
        size -= result;
        offset &= 31UL;
        if(offset) {
                tmp = *(p++);
                tmp |= ~0UL << (32-offset);
                if(size < 32)
                        goto found_first;
                if(~tmp)
                        goto found_middle;
                size -= 32;
                result += 32;
        }
        while(size & ~31UL) {
                if(~(tmp = *(p++)))
                        goto found_middle;
                result += 32;
                size -= 32;
        }
        if(!size)
                return result;
        tmp = *p;

found_first:
        tmp |= ~0UL << size;
found_middle:
        tmp = ((tmp>>24) | ((tmp>>8)&0xff00) | ((tmp<<8)&0xff0000) | (tmp<<24));
        return result + ffz(tmp);
}

static inline unsigned long find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
{
        unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
        unsigned long result = offset & ~31UL;
        unsigned long tmp;

        if (offset >= size)
                return size;
        size -= result;
        offset &= 31UL;
        if (offset) {
                tmp = *(p++);
                tmp |= ~0UL >> (32-offset);
                if (size < 32)
                        goto found_first;
                if (~tmp)
                        goto found_middle;
                size -= 32;
                result += 32;
        }
        while (size & ~31UL) {
                if (~(tmp = *(p++)))
                        goto found_middle;
                result += 32;
                size -= 32;
        }
        if (!size)
                return result;
        tmp = *p;

found_first:
        tmp |= ~0UL >> size;
found_middle:
        return result + ffz(tmp);
}

static inline  unsigned short swab16(unsigned short val)
{
        return (val >> 8) | (val << 8);
}

static inline  unsigned int  swab32(unsigned int  val)
{
        return ((val>>24) | ((val>>8)&0xFF00) |
                ((val<<8)&0xFF0000) | (val<<24));
}

#endif
