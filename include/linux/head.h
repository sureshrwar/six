
#ifndef _LINUX_HEAD_H
#define _LINUX_HEAD_H

typedef struct desc_struct {
        unsigned long a,b;
#if (SIX)
} desc_table[2048];
#else
} desc_table[256];
#endif



#endif

