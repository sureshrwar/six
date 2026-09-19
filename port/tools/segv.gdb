set pagination off
set confirm off
set height 0
set width 0
handle all nostop noprint pass
handle SIGSEGV stop print nopass
handle SIGTRAP stop print nopass
handle SIGINT stop print nopass
run
echo \n===== BACKTRACE =====\n
bt 40
echo \n===== REGISTERS =====\n
info registers eip esp ebp
echo \n===== TASK =====\n
p current_set[0]->pid
p/x current_set[0]->_sigreturn
p/x current_set[0]->ucontext.pc
p/x current_set[0]->ucontext.kesp
p/x current_set[0]->ucontext.esp
p current_set[0]->user_mode
p current_set[0]->kernel_level
echo \n===== END =====\n
quit
