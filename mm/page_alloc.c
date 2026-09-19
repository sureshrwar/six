
/*
 *  linux/mm/page_alloc.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/swapctl.h>
#include <linux/interrupt.h>

#include <asm/dma.h>
#include <asm/system.h> /* for cli()/sti() */
#include <asm/segment.h> /* for memcpy_to/fromfs */
#include <asm/bitops.h>
#include <asm/pgtable.h>

int nr_swap_pages = 0;
int nr_free_pages = 0;

/*
 * Free area management
 *
 * The free_area_list arrays point to the queue heads of the free areas
 * of different sizes
 */
#if (SIX)
/*
 * Im hiking this up - go sue
 */
#define NR_MEM_LISTS 8
#else
#define NR_MEM_LISTS 6
#endif

struct free_area_struct {
        struct page list;
        unsigned int * map;
};

static struct free_area_struct free_area[NR_MEM_LISTS];

#define LONG_ALIGN(x) (((x)+(sizeof(long))-1)&~((sizeof(long))-1))

static inline void init_mem_queue(struct page * head)
{
        head->next = head;
        head->prev = head;
}


static inline void add_mem_queue(struct page * head, struct page * entry)
{
        struct page * next = head->next;

        entry->prev = head;
        entry->next = next;
        next->prev = entry;
        head->next = entry;
}

static inline void remove_mem_queue(struct page * head, struct page * entry)
{
        struct page * next = entry->next;
        struct page * prev = entry->prev;
        next->prev = prev;
        prev->next = next;
}

/*
 * Set up the free-area data structures:
 *   - mark all pages reserved
 *   - mark all memory queues empty
 *   - clear the memory bitmaps
 */

unsigned long free_area_init(unsigned long start_mem, unsigned long end_mem)
{
	mem_map_t * p;
	unsigned long mask = PAGE_MASK;
        int i;

	printk("Free area initializaton...");
        /*
         * Select nr of pages we try to keep free for important stuff
         * with a minimum of 17 pages. This is totally arbitrary
         */
        i = (end_mem - PAGE_OFFSET) >> (PAGE_SHIFT+7);
        if (i < 16)
              i = 16;
        min_free_pages = i;
        free_pages_low = i + (i>>1);
        free_pages_high = i + i;
	start_mem = init_swap_cache(start_mem, end_mem);
	mem_map = (mem_map_t *) start_mem;
	p = mem_map + MAP_NR(end_mem);
	start_mem = LONG_ALIGN((unsigned long) p);
	memset(mem_map, 0, start_mem - (unsigned long) mem_map);
        do {	/* for all entries in mem_map */
                --p;
	        p->flags = (1 << PG_DMA)  | (1 << PG_reserved);
                p->map_nr = p - mem_map;
        } while (p > mem_map);

	/*
	 * NR_MEM_LISTS is 6
	 * we start with I = 0
	 * what we do here is : take the end_mem and divide it by page size.
	 * What we get is the bitmap size. Divide it by 8, since we have 8 bits
	 * in a byte. Allocate bitmap_size bytes from start_mem into free_area[i].map.
	 * Now increment I. This time  we divide end_mem by double of page size.
	 * Divide by 8, allocate the memory in free_area[i].map.
	 * Next time divide by four times page size, etc...This is the loop
	 */
        for (i = 0 ; i < NR_MEM_LISTS ; i++) {
                unsigned long bitmap_size;
                init_mem_queue(&free_area[i].list);
                mask += mask;
                end_mem = (end_mem + ~mask) & mask;
                bitmap_size = (end_mem - PAGE_OFFSET) >> (PAGE_SHIFT + i);
                bitmap_size = (bitmap_size + 7) >> 3;
                bitmap_size = LONG_ALIGN(bitmap_size);
                free_area[i].map = (unsigned int *) start_mem;
                memset((void *) start_mem, 0, bitmap_size);
                start_mem += bitmap_size;
        }


	printk("Done\n");
	printk("Free area init: memory start shifted to 0x%x\n", start_mem);
	return start_mem;
}


/*
 * Buddy system. Hairy. You really aren't expected to understand this
 */
static inline void free_pages_ok(unsigned long map_nr, unsigned long order)
{
        unsigned long index = map_nr >> (1 + order);
        unsigned long mask = (~0UL) << order;
        unsigned long flags;

        save_flags(flags);
        cli();
#define list(x) (mem_map+(x))

        map_nr &= mask;
        nr_free_pages += 1 << order;
        while (order < NR_MEM_LISTS-1) {
                if (!change_bit(index, free_area[order].map))
                        break;
                remove_mem_queue(&free_area[order].list, list(map_nr ^ (1+~mask)));
                mask <<= 1;
                order++;
                index >>= 1;
                map_nr &= mask;
        }
        add_mem_queue(&free_area[order].list, list(map_nr));

#undef list

        restore_flags(flags);

}


void free_pages(unsigned long addr, unsigned long order)
{
        unsigned long map_nr = MAP_NR(addr);

        if (map_nr < MAP_NR(high_memory)) {
                mem_map_t * map = mem_map + map_nr;
                if (PageReserved(map))
                        return;
                if (atomic_dec_and_test(&map->count)) {
                        delete_from_swap_cache(map_nr);
                        free_pages_ok(map_nr, order);
                        return;
                }
        }
}


/*
 * Some ugly macros to speed up __get_free_pages()..
 */
#define MARK_USED(index, order, area) \
        change_bit((index) >> (1+(order)), (area)->map)
#define CAN_DMA(x) (PageDMA(x))
#define ADDRESS(x) (PAGE_OFFSET + ((x) << PAGE_SHIFT))
#define RMQUEUE(order, dma) \
do { struct free_area_struct * area = free_area+order; \
     unsigned long new_order = order; \
        do { struct page *prev = &area->list, *ret; \
                while (&area->list != (ret = prev->next)) { \
                        if (!dma || CAN_DMA(ret)) { \
                                unsigned long map_nr = ret->map_nr; \
                                (prev->next = ret->next)->prev = prev; \
                                MARK_USED(map_nr, new_order, area); \
                                nr_free_pages -= 1 << order; \
                                EXPAND(ret, map_nr, order, new_order, area); \
                                restore_flags(flags); \
                                return ADDRESS(map_nr); \
                        } \
                        prev = ret; \
                } \
                new_order++; area++; \
        } while (new_order < NR_MEM_LISTS); \
} while (0)


#define EXPAND(map,index,low,high,area) \
do { unsigned long size = 1 << high; \
        while (high > low) { \
                area--; high--; size >>= 1; \
                add_mem_queue(&area->list, map); \
                MARK_USED(index, high, area); \
                index += size; \
                map += size; \
        } \
        map->count = 1; \
        map->age = PAGE_INITIAL_AGE; \
} while (0)

unsigned long __get_free_pages(int priority, unsigned long order, int dma)
{
        unsigned long flags;
        int reserved_pages;

        if (order >= NR_MEM_LISTS)
                return 0;
        if (intr_count && priority != GFP_ATOMIC) {
                static int count = 0;
                if (++count < 5) {
                        printk("gfp called nonatomically from interrupt %p\n",
                                __builtin_return_address(0));
                        priority = GFP_ATOMIC;
                }
        }
        reserved_pages = 5;
        if (priority != GFP_NFS)
                reserved_pages = min_free_pages;
        save_flags(flags);
repeat:
        cli();
        if ((priority==GFP_ATOMIC) || nr_free_pages > reserved_pages) {
#if (SIX)
do { struct free_area_struct * area = free_area+order; 
     unsigned long new_order = order; 
        do { struct page *prev = &area->list, *ret; 
                while (&area->list != (ret = prev->next)) { 
                        if (!dma || CAN_DMA(ret)) { 
                          unsigned long map_nr = ret->map_nr; 
                          (prev->next = ret->next)->prev = prev; 
                          MARK_USED(map_nr, new_order, area); 
                          nr_free_pages -= 1 << order; 
			  do { unsigned long size = 1 << new_order; 
			       while (new_order > order) { 
                			area--; new_order--; size >>= 1; 
                			add_mem_queue(&area->list, ret); 
			                MARK_USED(map_nr, new_order, area); 
			                map_nr += size; 
			                ret += size; 
        				} 
			        ret->count = 1; 
			        ret->age = PAGE_INITIAL_AGE; 
			  } while (0);
                          restore_flags(flags); 
			  sti();
                          return ADDRESS(map_nr); 
                        } 
                        prev = ret; 
                } 
                new_order++; area++; 
        } while (new_order < NR_MEM_LISTS); 
} while (0);
#else
		RMQUEUE(order, dma);
#endif
                restore_flags(flags);
                return 0;
        }
        restore_flags(flags);
        if (priority != GFP_BUFFER && try_to_free_page(priority, dma, 1))
                goto repeat;
        return 0;
}

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 */
void show_free_areas(void)
{
        unsigned long order, flags;
        unsigned long total = 0;

        printk("Free pages:      %6dkB\n ( ",nr_free_pages<<(PAGE_SHIFT-10));
        save_flags(flags);
        cli();
        for (order=0 ; order < NR_MEM_LISTS; order++) {
                struct page * tmp;
                unsigned long nr = 0;
                for (tmp = free_area[order].list.next ; tmp != &free_area[order].list ; tmp = tmp->next) {
                        nr ++;
                }
                total += nr * ((PAGE_SIZE>>10) << order);
                printk("%lu*%lukB ", nr, (PAGE_SIZE>>10) << order);
        }
        restore_flags(flags);
        printk("= %lukB)\n", total);
#ifdef SWAP_CACHE_INFO
        show_swap_cache_info();
#endif
}

