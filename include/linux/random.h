
/*
 * include/linux/random.h
 *
 * Include file for the random number generator.
 */

#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

#include <linux/ioctl.h>


extern void rand_initialize_irq(int irq);
extern void add_interrupt_randomness(int irq);


extern struct file_operations random_fops, urandom_fops;

#endif
