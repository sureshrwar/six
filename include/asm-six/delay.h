
#ifndef _I386_DELAY_H
#define _I386_DELAY_H

/*
 * Copyright (C) 1993 Linus Torvalds
 *
 * Delay routines, using a pre-computed "loops_per_second" value.
 */

#ifdef __SMP__
#include <asm/smp.h>
#endif



static inline void __delay(int loops)
{
	while(loops--) ;
}


#endif
