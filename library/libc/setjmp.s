! $Id: setjmp.s,v 1.1.1.1 2005/03/30 08:40:50 motorbreathing Exp $
!
! In Minix, setjmp and longjmp save and restore the the signal mask.
! _setjmp and _longjmp should be used if the signal mask is not to be
! saved and restored.
!
! The jmp_buf and sigjmp_buf are identical in minix.  They start
! with flags and saved signal mask, and then have space for 16 registers.

!#include </usr/include/sys/trap.h>

	.global _setjmp, setjmp, _longjmp, longjmp

SC_SIGCONTEXT=2                     ! Must agree with the Minix .h files!
SIGSETMASK=2                        ! Must agree with <signal.h>

	.text
_setjmp:
	ba 	__setjmp            ! Zero flags and jump to the common code
	st	%g0, [%o0]        

setjmp:
	save    %sp, -96, %sp       
	mov	SC_SIGCONTEXT, %l0
	st	%l0, [%i0]          ! we are going to save the signal context

	clr     %o0                 ! Get the current signal mask
	clr     %o1                 ! Pass NULL for set---returns the current
	call    sigprocmask
	add     %i0, 4, %o2         ! signal mask in oset

	ba	__setjmp            ! Jump to the common code
	restore                     ! Need to have the caller's register set

! Save values in __regs (starts at offset 8 in jmp_buf after flags and mask)
__setjmp:
	st     	%sp, [%o0 + 8]      ! Save %sp
	st     	%o7, [%o0 + 12]     ! ... address of setjmp call
	st     	%fp, [%o0 + 16]     ! ... frame pointer
	st     	%i7, [%o0 + 20]     ! ... return address from setjmp caller
	retl
	clr    	%o0                 ! Returns 0	


! Have the same entry point for both of these.  Use the flags to
! decide whether to restore the signal mask.
_longjmp:
longjmp:
	ta     	0x03
	ld	[%o0], %o2        ! Load flags
	mov	SC_SIGCONTEXT, %o3
	orcc    %o2, %o3, %g0     ! Is the SIGCONTEXT bit set in flags
	bz      nomaskrestore
	nop

	save	%sp, -96, %sp     ! It is set---have to call sigprocmask
	mov	SIGSETMASK, %o0   ! to set mask
	add	%i0, 4, %o1       ! Mask field in jmp_buf
	call	sigprocmask
	clr	%o2               ! Don't return old mask
	restore                   ! Get back to longjmp caller's register set
	
nomaskrestore:
	ld     	[%o0 + 8], %o2    ! Load saved sp
	ldd    	[%o2], %l0        ! Restore l registers
	ldd    	[%o2 + 8], %l2
	ldd    	[%o2 + 16], %l4
	ldd    	[%o2 + 24], %l6
	ldd    	[%o2 + 32], %i0   ! Restore i registers
	ldd    	[%o2 + 40], %i2
	ldd    	[%o2 + 48], %i4

	ld     	[%o0 + 16], %fp   ! Restore fp
	mov    	%o2, %sp          ! and sp
	ld     	[%o0 + 20], %i7   ! and return address for setjmp caller
	ld     	[%o0 + 12], %o7   ! address of call to setjmp

	orcc   	%g0, %o1, %g0     ! Is the value to "return" from setjmp 0?
	bne    	nonzero_return
	nop
	mov    	1, %o1            ! Yes---can't allow that, return 1 instead

nonzero_return:
	retl                      ! Return from earlier setjmp
	mov    	%o1, %o0          ! Return setjmp return value
