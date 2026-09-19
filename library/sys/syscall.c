



int syscall(int num, long one, long two, long three)
{
#if (__i386__)
	int ret;
        long long val;
        long *valp;
        valp = (long *)&val;
	val = one;
	val <<= 32;
	val += num;
	__asm__("movq (%1), %%mm0" : "=r" (ret) : "r" (valp));	

	val = (long)two;
	val <<= 32;
	val += three;
	__asm__("movq (%1), %%mm2" : "=r" (ret) : "r" (valp));

	int0x80();

	__asm__("movq %%mm0, (%1)" : "=r" (ret) : "r" (valp));

	return (long)val;
#else 
        long ret;
	
	/*
	 * This is how the system call mechanism works :
	 *
	 * stage 0:
	 * 
	 * the "user" program calls the system call stub,
	 * which in turn calls this function, syscall(). 
	 * we are given the system call number, as well as
	 * the various arguments to be passed. our aim - to
	 * transfer control somehow to a particular function
	 * inside the kernel. for example, the system call
	 * number is 3, then our target is sys_read().
	 *
	 * stage 1:
	 *
	 * The system call number goes into g2, and
	 * the three arguments go into g3, g4, and g5.
	 * 
	 * stage 2: 
	 * 
	 * we find our pid, and send ourselves a SIGLWP.
         * this stuff happens inside int0x80().
	 * 
	 * stage 3:
	 * 
	 * so we receive a SIGLWP. now if you look inside
	 * init_IRQ(), you will see that system_call() is
	 * being registered as the handler for SIGLWP.
	 * In fact all signals are caught by the function
	 * sun_handler(). This function then calls do_IRQ()
	 * which in turn calls whatever function that 
	 * was registered for the particular signal which
	 * came in. anyway to cut a long one short, 
	 * system_call() gets called.
	 * 
	 * stage 4:
	 * 
	 * inside system_call(). the contents of g2 are
	 * checked and its used as an index into the
	 * funtion pointer array sys_call_table. this
	 * array contains pointers to all system call
	 * entry points. a system call entry point is
	 * typically a very simply function, say 5-10 lines,
	 * that does the following :
	 * 
	 * stage 5:
	 * 
	 * inside the system call entry point.
	 * for example consider the system call read(). the
	 * entry point is a function called six_read(). 
	 * six_read knows that it is supposed to call sys_read().
	 * it also knows that sys_read() takes three arguments.
	 * so it uses the function grab_args() to retrieve
	 * the three arguments from g3, g4, and g5. this
	 * is done using the help of macros B(u), C(u) and D(u).
	 * these macros take a pointer to a ucontext_t
	 * structure and change into the appropriate member that
	 * contains the value of g3, g4 or g5.  
	 * so in short grab_args() provides the args, and then :
	 * 
	 * stage 6:
	 * 
	 * the actualy system call routine, say sys_read() gets
	 * called. the return value is stored in a local variable
	 * and now this has to be returned to the address space 
	 * of the "user" program that triggered the system call.
	 * 
	 * stage 7:
	 * 
	 * this is done by put_ret(). this function takes the
	 * return value and stuffs it into the appropriate 
	 * member of the ucontext_t structure passed to it.
	 * the helper macro used is A(u), which changes into
	 * the member that represents the register g2. yes
	 * g2 is used for passing the system call number as
	 * well as for bringing back the return value.
	 * so thats it - the entry point function returns into
	 * system_call(), system_call() returns into do_IRQ(),
	 * do_IRQ() returns to sun_handler(), and sun_handler()
	 * does some crazy things and restores the context
	 * back to that of the "user" program.
	 */

        __asm__("mov %1, %%g2"   :  "=r" (ret)  : "r" (num));
        __asm__("mov %1, %%g3"   :  "=r" (ret)  : "r" (one));
        __asm__("mov %1, %%g4"   :  "=r" (ret)  : "r" (two));
        __asm__("mov %1, %%g5"   :  "=r" (ret)  : "r" (three));
        __asm__("mov 0, %g6");

	/*
	 * get our pid, and shoot ourselves with a SIGLWP. 
	 */
        int0x80();

	/*
	 * prise out the return value
	 */
        __asm__("mov %%g2, %0" : "=r" (ret));

        return ret;
#endif
}
