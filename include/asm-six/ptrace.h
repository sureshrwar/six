
#ifndef _SIX_ASM_PTRACE_H
#define _SIX_ASM_PTRACE_H

#if (__i386__)
#define SS              18      
#define UESP            17      
#define EFL             16
#define CS              15
#define EIP             14
#define ERR             13
#define TRAPNO          12
#define EAX             11
#define ECX             10
#define EDX             9
#define EBX             8
#define ESP             7
#define EBP             6
#define ESI             5
#define EDI             4
#define DS              3
#define ES              2
#define FS              1
#define GS              0

#else

#define REG_PSR (0)
#define REG_PC  (1)
#define REG_nPC (2)
#define REG_Y   (3)
#define REG_G1  (4)
#define REG_G2  (5)
#define REG_G3  (6)
#define REG_G4  (7)
#define REG_G5  (8)
#define REG_G6  (9)
#define REG_G7  (10)
#define REG_O0  (11)
#define REG_O1  (12)
#define REG_O2  (13)
#define REG_O3  (14)
#define REG_O4  (15)
#define REG_O5  (16)
#define REG_O6  (17)
#define REG_O7  (18)
        
/* the following defines are for portability */
#define REG_PS  REG_PSR
#define REG_SP  REG_O6
#define REG_R0  REG_O0
#define REG_R1  REG_O1

#endif


struct pt_regs {
#if (__i386__)
        unsigned int uu1[6];
	/*
	 * note - this is what makecontext() expects to find an address
	 * which it can use to setup a stack. this should not be confused
	 * with the stack pointer. same applies for sparc scenario as well.
	 */
        unsigned int uc_sp; 
        unsigned int uc_sp_size;
        unsigned int uu2[7];
        unsigned int ebp;
        unsigned int uu3[7];
        unsigned int pc;
        unsigned int uu4;
        unsigned int psw;
	/*
	 * and this - this is the stack pointer. same with sparc.
	 */
        unsigned int esp;
        unsigned int uu5[8];
        unsigned int g2;
        unsigned int g3;
        unsigned int uu6[3];
        unsigned int g4;
        unsigned int g5;
        unsigned int uu7[3];
        unsigned int g6;
        unsigned int g7;
        unsigned int uu8[3];
        unsigned int g8;
        unsigned int g9;
        unsigned uu9[76];
#else
        unsigned int uu1[6];
        unsigned int uc_sp;
        unsigned int uc_sp_size;
        unsigned int uu2[2];
        unsigned int psw;
        unsigned int pc;
        unsigned int npc;
        unsigned int uu3[2];
        unsigned int g2;
        unsigned int g3;
        unsigned int g4;
        unsigned int g5;
        unsigned int g6;
        unsigned int g7;
        unsigned uu4[6];
        unsigned int esp;
        unsigned uu5[84];
#endif
};


#define user_mode(regs) (current->user_mode)
#define instruction_pointer(regs) ((regs)->pc)

#define USER_MODE 	1
#define KERNEL_MODE	0

#define go_user_mode()	current->user_mode = USER_MODE
#define go_kernel_mode()  current->user_mode = KERNEL_MODE 

#endif
