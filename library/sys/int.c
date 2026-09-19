
/*
 * the systemcall mechanism:
 * first calls getpid and gets our own pid. then calls kill using the
 * pid and SIGLWP as arguments so that we hit ourselves with a SIGLWP
 * causing the systemcall handler system_handler to be invoked.
 */


void int0x80()
{
#if (__i386__)
	__asm__("pushl  $0x21\n  /* push SIGLWP */
	  	 movl   $0x14,%eax\n  /* 0x14 is getpid */
		 lcall  $0x7,$0x0\n  /* call getpid */
		 pushl  %eax\n /* push  what getpid gave, ie our pid */
		 pushl $0x5\n  /* dummy */
		 movl   $0x25,%eax\n  /* 0x25 is kill */
		 lcall  $0x7,$0x0");  /* call kill */
#else
	int pid;
	pid = sparc_sys(20, 0, 0, 0);	/* get our pid */
	sparc_sys(37, pid, 33, 0);	/* hit us with a SIGLWP */
#endif
}
