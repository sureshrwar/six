! Ok this is shamelessly lifted from SMX - the solaris user mode minix. 
! Thanks guys :)

! Make a SunOS system call.  The system call number is the first parameter,
! with the system call parameters following (up to 6 must be catered for).
! This function was intended for use mainly by the kernel.  It is also
! needed to send and receive messages, and by the sunread command, so was
! put in the library.  It is not intended be called from anywhere else.

        .global sparc_sys, sparc_end
sparc_sys:	
	mov    %o0, %g1       ! System call number
	mov    %o1, %o0       ! Up to 6 parameters
	mov    %o2, %o1
	mov    %o3, %o2
	mov    %o4, %o3
	mov    %o5, %o4
	ld     [%sp + 92], %o5
        t      0x08
	bgeu   noerr
	nop
	st     %o0, [%g1]
	mov    -1, %o0
noerr:	
	retl
	nop
sparc_end:                     ! label needed by kernel for protection purposes

	
