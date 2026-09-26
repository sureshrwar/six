/*
 *  arch/six/kernel/ptrace.c
 *
 *  Ptrace & GDB debugging support for SIX guest processes (32-bit x86).
 *
 *  Supports:
 *    - Standard Linux ptrace requests:
 *        PTRACE_TRACEME, PTRACE_PEEKTEXT, PTRACE_PEEKDATA, PTRACE_PEEKUSR,
 *        PTRACE_POKETEXT, PTRACE_POKEDATA, PTRACE_POKEUSR,
 *        PTRACE_GETREGS, PTRACE_SETREGS, PTRACE_CONT, PTRACE_SINGLESTEP,
 *        PTRACE_SYSCALL, PTRACE_KILL, PTRACE_ATTACH, PTRACE_DETACH
 *    - Kernel-managed software single-stepping (PTRACE_SINGLESTEP) via
 *      x86 instruction decoding and next-PC int3 (0xcc) trap injection so
 *      single-stepping works cleanly across glibc setcontext() and SIX
 *      syscall traps.
 *    - SIX debugging extensions (PTRACE_SIX_READMEM, PTRACE_SIX_WRITEMEM,
 *      PTRACE_SIX_GET_PROC, PTRACE_SIX_LOAD_DBG, PTRACE_SIX_SRCLINE) for
 *      in-guest /bin/gdb and /bin/gdbserver.
 */

#include <solaris.h>
#include "host.h"

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/string.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/segment.h>

extern struct task_struct *mapped_proc;
extern const char *six_get_task_exe(struct task_struct *p);

struct six_step_bp {
	int active;
	unsigned long addr[2];
	unsigned char orig_byte[2];
	int count;
};

static struct six_step_bp six_task_step[NR_TASKS];

static int task_slot(struct task_struct *tsk)
{
	int i;
	if (!tsk)
		return -1;
	for (i = 0; i < NR_TASKS; i++) {
		if (task[i] == tsk)
			return i;
	}
	return -1;
}

static struct task_struct *find_task_by_pid(int pid)
{
	int i;
	if (pid <= 0)
		return NULL;
	for (i = 0; i < NR_TASKS; i++) {
		if (task[i] && task[i]->pid == pid)
			return task[i];
	}
	return NULL;
}

/*
 * Read or write bytes in `tsk`'s virtual address space [0x03000000 .. TASK_SIZE)
 * directly through its page tables in `_ram_start .. high_memory`.
 * If `write` is set on a shared page, performs Copy-On-Write so breakpoints
 * (0xcc) remain private to `tsk`.
 */
int six_ptrace_access_mem(struct task_struct *tsk, unsigned long vaddr,
			  void *buf, int len, int write)
{
	unsigned char *p = (unsigned char *)buf;
	int i;

	if (!tsk || !tsk->mm || !buf || len < 0)
		return -EIO;
	if (len == 0)
		return 0;
	if (vaddr < 0x03000000UL || vaddr >= TASK_SIZE ||
	    (unsigned long)len > TASK_SIZE - vaddr)
		return -EIO;

	for (i = 0; i < len; i++) {
		unsigned long va = vaddr + (unsigned long)i;
		pgd_t *pgd = pgd_offset(tsk->mm, va);
		pmd_t *pmd;
		pte_t *pte;
		unsigned long kpage;

		if (!pgd)
			return -EIO;
		pmd = pmd_offset(pgd, va);
		if (!pmd || pmd_none(*pmd))
			return -EIO;
		pte = pte_offset(pmd, va);
		if (!pte || pte_none(*pte))
			return -EIO;

		kpage = pte_page(*pte);
		if (kpage < _ram_start || kpage >= high_memory)
			return -EIO;

		if (write) {
			if (mem_map[MAP_NR(kpage)].count > 1) {
				unsigned long new_page = get_free_page(GFP_KERNEL);
				if (!new_page)
					return -ENOMEM;
				memcpy((void *)new_page, (void *)kpage, PAGE_SIZE);
				mem_map[MAP_NR(kpage)].count--;
				pte_val(*pte) = _PAGE_TABLE | new_page;
				kpage = new_page;
				if (mapped_proc == tsk)
					mapped_proc = NULL;
			}
			((unsigned char *)kpage)[va & ~PAGE_MASK] = p[i];
		} else {
			p[i] = ((unsigned char *)kpage)[va & ~PAGE_MASK];
		}
	}
	return 0;
}

static unsigned long get_reg32_by_idx(struct pt_regs *r, int idx)
{
	switch (idx & 7) {
	case 0: return r->uu2[3]; /* EAX */
	case 1: return r->uu2[2]; /* ECX */
	case 2: return r->uu2[1]; /* EDX */
	case 3: return r->uu2[0]; /* EBX */
	case 4: return r->kesp;   /* ESP */
	case 5: return r->ebp;    /* EBP */
	case 6: return r->esi;    /* ESI */
	case 7: return r->edi;    /* EDI */
	}
	return 0;
}

/*
 * Decode the ModR/M (+ optional SIB + displacement) starting at `code[pos]`.
 * Optionally computes the effective address (or register value for mod==3)
 * when `ea_out` / `val_out` are requested for indirect call/jmp (`FF /2`, `FF /4`).
 */
static int decode_modrm32(struct task_struct *tsk, const unsigned char *code,
			  int pos, unsigned long *val_out)
{
	unsigned char modrm = code[pos++];
	int mod = (modrm >> 6) & 3;
	int rm  = modrm & 7;
	unsigned long addr = 0;
	int disp = 0;

	if (mod == 3) {
		if (val_out)
			*val_out = get_reg32_by_idx(&tsk->ucontext, rm);
		return pos;
	}

	if (rm == 4) {
		unsigned char sib = code[pos++];
		int scale = (sib >> 6) & 3;
		int index = (sib >> 3) & 7;
		int base  = sib & 7;

		if (base == 5 && mod == 0) {
			memcpy(&disp, &code[pos], 4);
			pos += 4;
			addr = (unsigned long)disp;
		} else {
			addr = get_reg32_by_idx(&tsk->ucontext, base);
		}
		if (index != 4)
			addr += get_reg32_by_idx(&tsk->ucontext, index) << scale;
	} else if (rm == 5 && mod == 0) {
		memcpy(&disp, &code[pos], 4);
		pos += 4;
		addr = (unsigned long)disp;
	} else {
		addr = get_reg32_by_idx(&tsk->ucontext, rm);
	}

	if (mod == 1) {
		disp = (signed char)code[pos++];
		addr += (unsigned long)disp;
	} else if (mod == 2) {
		memcpy(&disp, &code[pos], 4);
		pos += 4;
		addr += (unsigned long)disp;
	}

	if (val_out) {
		unsigned long target = 0;
		if (six_ptrace_access_mem(tsk, addr, &target, 4, 0) == 0)
			*val_out = target;
		else
			*val_out = 0;
	}
	return pos;
}

/*
 * Compute the next guest PC(s) after executing the instruction at `tsk->ucontext.pc`.
 * Returns the number of target addresses (1 or 2).
 */
static int compute_next_pcs(struct task_struct *tsk, unsigned long targets[2])
{
	unsigned char code[16];
	unsigned long pc = tsk->ucontext.pc;
	int pos = 0, op16 = 0;
	unsigned char op;

	memset(code, 0x90, sizeof(code));
	if (six_ptrace_access_mem(tsk, pc, code, sizeof(code), 0) < 0) {
		targets[0] = pc + 1;
		return 1;
	}

	/* Consume instruction prefixes */
	while (pos < 8) {
		op = code[pos];
		if (op == 0x66) {
			op16 = 1;
			pos++;
		} else if (op == 0xF0 || op == 0xF2 || op == 0xF3 ||
			   op == 0x26 || op == 0x2E || op == 0x36 ||
			   op == 0x3E || op == 0x64 || op == 0x65 || op == 0x67) {
			pos++;
		} else {
			break;
		}
	}

	op = code[pos++];

	/* Direct control flow */
	if (op == 0xE8 || op == 0xE9) { /* call rel32 / jmp rel32 */
		int rel = 0;
		memcpy(&rel, &code[pos], 4);
		pos += 4;
		targets[0] = pc + (unsigned long)pos + (unsigned long)rel;
		return 1;
	}
	if (op == 0xEB) { /* jmp rel8 */
		int rel = (signed char)code[pos++];
		targets[0] = pc + (unsigned long)pos + (unsigned long)rel;
		return 1;
	}
	if ((op >= 0x70 && op <= 0x7F) || (op >= 0xE0 && op <= 0xE3)) { /* jcc / loop / jecxz rel8 */
		int rel = (signed char)code[pos++];
		targets[0] = pc + (unsigned long)pos;
		targets[1] = pc + (unsigned long)pos + (unsigned long)rel;
		return (targets[1] != targets[0]) ? 2 : 1;
	}
	if (op == 0xC3 || op == 0xC2) { /* ret / ret imm16 */
		unsigned long ret_addr = 0;
		if (six_ptrace_access_mem(tsk, tsk->ucontext.kesp, &ret_addr, 4, 0) == 0 &&
		    ret_addr >= 0x03000000UL && ret_addr < TASK_SIZE) {
			targets[0] = ret_addr;
			return 1;
		}
		targets[0] = pc + (unsigned long)(op == 0xC2 ? (pos + 2) : pos);
		return 1;
	}
	if (op == 0xFF) { /* Group 5 */
		int reg_op = (code[pos] >> 3) & 7;
		unsigned long ind_target = 0;
		pos = decode_modrm32(tsk, code, pos,
				     (reg_op == 2 || reg_op == 4) ? &ind_target : NULL);
		if ((reg_op == 2 || reg_op == 4) &&
		    ind_target >= 0x03000000UL && ind_target < TASK_SIZE) {
			targets[0] = ind_target;
			return 1;
		}
		targets[0] = pc + (unsigned long)pos;
		return 1;
	}
	if (op == 0x0F) { /* Two-byte opcodes */
		unsigned char op2 = code[pos++];
		if (op2 >= 0x80 && op2 <= 0x8F) { /* jcc rel32 */
			int rel = 0;
			memcpy(&rel, &code[pos], 4);
			pos += 4;
			targets[0] = pc + (unsigned long)pos;
			targets[1] = pc + (unsigned long)pos + (unsigned long)rel;
			return (targets[1] != targets[0]) ? 2 : 1;
		}
		if (op2 == 0x31 || op2 == 0xA2 || op2 == 0x0B ||
		    op2 == 0xA0 || op2 == 0xA1 || op2 == 0xA8 || op2 == 0xA9 ||
		    (op2 >= 0xC8 && op2 <= 0xCF)) {
			targets[0] = pc + (unsigned long)pos;
			return 1;
		}
		pos = decode_modrm32(tsk, code, pos, NULL);
		if (op2 == 0xA4 || op2 == 0xAC || op2 == 0xBA ||
		    op2 == 0xC2 || op2 == 0xC4 || op2 == 0xC5 || op2 == 0xC6 ||
		    (op2 >= 0x70 && op2 <= 0x73))
			pos += 1;
		targets[0] = pc + (unsigned long)pos;
		return 1;
	}

	/* Single-byte no-ModR/M opcodes */
	if ((op >= 0x40 && op <= 0x5F) || (op >= 0x90 && op <= 0x9F) ||
	    op == 0x60 || op == 0x61 ||
	    (op >= 0xA4 && op <= 0xA7) || (op >= 0xAA && op <= 0xAF) ||
	    op == 0xC9 || op == 0xCC || op == 0xCE || op == 0xCF ||
	    op == 0xD6 || op == 0xD7 || (op >= 0xEC && op <= 0xEF) ||
	    op == 0xF4 || op == 0xF5 || (op >= 0xF8 && op <= 0xFD) ||
	    op == 0x06 || op == 0x07 || op == 0x0E || op == 0x16 ||
	    op == 0x17 || op == 0x1E || op == 0x1F || op == 0x27 ||
	    op == 0x2F || op == 0x37 || op == 0x3F) {
		targets[0] = pc + (unsigned long)pos;
		return 1;
	}
	if (op == 0x04 || op == 0x0C || op == 0x14 || op == 0x1C ||
	    op == 0x24 || op == 0x2C || op == 0x34 || op == 0x3C ||
	    op == 0x6A || op == 0xA8 || (op >= 0xB0 && op <= 0xB7) ||
	    op == 0xCD || op == 0xD4 || op == 0xD5 || (op >= 0xE4 && op <= 0xE7)) {
		targets[0] = pc + (unsigned long)(pos + 1);
		return 1;
	}
	if (op == 0xC8) { /* enter imm16, imm8 */
		targets[0] = pc + (unsigned long)(pos + 3);
		return 1;
	}
	if (op == 0x05 || op == 0x0D || op == 0x15 || op == 0x1D ||
	    op == 0x25 || op == 0x2D || op == 0x35 || op == 0x3D ||
	    op == 0x68 || op == 0xA9 || (op >= 0xB8 && op <= 0xBF)) {
		targets[0] = pc + (unsigned long)(pos + (op16 ? 2 : 4));
		return 1;
	}
	if (op >= 0xA0 && op <= 0xA3) { /* mov al/eax, moffs32 */
		targets[0] = pc + (unsigned long)(pos + 4);
		return 1;
	}

	/* ModR/M opcodes */
	{
		int reg_op = (code[pos] >> 3) & 7;
		pos = decode_modrm32(tsk, code, pos, NULL);
		if (op == 0x80 || op == 0x82 || op == 0x83 ||
		    op == 0xC0 || op == 0xC1 || op == 0xC6 || op == 0x6B) {
			pos += 1;
		} else if (op == 0x81 || op == 0xC7 || op == 0x69) {
			pos += (op16 ? 2 : 4);
		} else if (op == 0xF6 && (reg_op == 0 || reg_op == 1)) {
			pos += 1;
		} else if (op == 0xF7 && (reg_op == 0 || reg_op == 1)) {
			pos += (op16 ? 2 : 4);
		}
		targets[0] = pc + (unsigned long)pos;
		return 1;
	}
}

void six_ptrace_disarm_step(struct task_struct *tsk)
{
	int slot = task_slot(tsk);
	int i;

	if (slot < 0 || !six_task_step[slot].active)
		return;

	for (i = 0; i < six_task_step[slot].count; i++) {
		unsigned char cur_b = 0;
		unsigned long addr = six_task_step[slot].addr[i];
		if (six_ptrace_access_mem(tsk, addr, &cur_b, 1, 0) == 0 && cur_b == 0xcc) {
			six_ptrace_access_mem(tsk, addr, &six_task_step[slot].orig_byte[i], 1, 1);
		}
	}
	six_task_step[slot].active = 0;
	six_task_step[slot].count = 0;
}

static void six_ptrace_arm_step(struct task_struct *tsk)
{
	int slot = task_slot(tsk);
	unsigned long targets[2];
	int n, i, armed = 0;
	unsigned char int3 = 0xcc;

	if (slot < 0)
		return;
	six_ptrace_disarm_step(tsk);

	n = compute_next_pcs(tsk, targets);
	for (i = 0; i < n && armed < 2; i++) {
		unsigned char orig = 0;
		if (targets[i] >= 0x03000000UL && targets[i] < TASK_SIZE &&
		    six_ptrace_access_mem(tsk, targets[i], &orig, 1, 0) == 0) {
			six_task_step[slot].addr[armed] = targets[i];
			six_task_step[slot].orig_byte[armed] = orig;
			six_ptrace_access_mem(tsk, targets[i], &int3, 1, 1);
			armed++;
		}
	}
	if (armed > 0) {
		six_task_step[slot].count = armed;
		six_task_step[slot].active = 1;
	}
}

/*
 * Called from TRAP_action (arch/six/kernel/irq.c) when SIGTRAP arrives.
 * If this SIGTRAP came from an internal PTRACE_SINGLESTEP breakpoint,
 * restores the original instruction byte(s) and rewinds `regs->pc` by 1
 * so `regs->pc` points directly at the instruction that trapped.
 */
void six_ptrace_handle_sigtrap(struct task_struct *tsk, struct pt_regs *regs)
{
	int slot = task_slot(tsk);
	int i;

	if (slot >= 0 && six_task_step[slot].active && regs) {
		for (i = 0; i < six_task_step[slot].count; i++) {
			if (regs->pc == six_task_step[slot].addr[i] + 1) {
				regs->pc = six_task_step[slot].addr[i];
				break;
			}
		}
		six_ptrace_disarm_step(tsk);
	}
	if (tsk)
		send_sig(SIGTRAP, tsk, 1);
}

static void pt_regs_to_user_regs(const struct pt_regs *pt, struct user_regs_struct *ur)
{
	memset(ur, 0, sizeof(*ur));
#if (__i386__)
	ur->ebx      = pt->uu2[0];
	ur->ecx      = pt->uu2[2];
	ur->edx      = pt->uu2[1];
	ur->esi      = pt->esi;
	ur->edi      = pt->edi;
	ur->ebp      = pt->ebp;
	ur->eax      = pt->uu2[3];
	ur->ds       = pt->ds;
	ur->es       = pt->es;
	ur->fs       = pt->fs;
	ur->gs       = pt->gs;
	ur->orig_eax = pt->g2;
	ur->eip      = pt->pc;
	ur->cs       = pt->cs;
	ur->eflags   = pt->psw;
	ur->esp      = pt->kesp ? pt->kesp : pt->esp;
	ur->ss       = pt->ss;
#endif
}

static void user_regs_to_pt_regs(const struct user_regs_struct *ur, struct pt_regs *pt)
{
#if (__i386__)
	pt->uu2[0] = ur->ebx;
	pt->uu2[2] = ur->ecx;
	pt->uu2[1] = ur->edx;
	pt->esi    = ur->esi;
	pt->edi    = ur->edi;
	pt->ebp    = ur->ebp;
	pt->uu2[3] = ur->eax;
	pt->pc     = ur->eip;
	pt->psw    = ur->eflags;
	pt->kesp   = ur->esp;
	pt->esp    = ur->esp;
#endif
}

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int ret;

	if (request == PTRACE_TRACEME) {
		if (current->flags & PF_PTRACED)
			return -EPERM;
		current->flags |= PF_PTRACED;
		return 0;
	}

	/* Host-assisted symbol/DWARF line table and source file lookup (no child PID needed) */
	if (request == PTRACE_SIX_LOAD_DBG) {
		struct six_ptrace_dbg_req *req = (struct six_ptrace_dbg_req *)data;
		if (!req || (unsigned long)req < 0x03000000UL ||
		    (unsigned long)req + sizeof(*req) > TASK_SIZE)
			return -EFAULT;
		return six_host_load_guest_debug(
			req->exe_path,
			req->syms, req->max_syms, &req->num_syms,
			req->lines, req->max_lines, &req->num_lines,
			req->files, req->max_files, &req->num_files);
	}

	if (request == PTRACE_SIX_SRCLINE) {
		struct six_ptrace_srcline_req *req = (struct six_ptrace_srcline_req *)data;
		if (!req || (unsigned long)req < 0x03000000UL ||
		    (unsigned long)req + sizeof(*req) > TASK_SIZE)
			return -EFAULT;
		if (!req->buf || (unsigned long)req->buf < 0x03000000UL ||
		    req->buflen <= 0 || (unsigned long)req->buf + req->buflen > TASK_SIZE)
			return -EFAULT;
		return six_host_read_source_line(req->file, req->line, req->buf, req->buflen);
	}

	if (pid <= 1)
		return -EPERM;

	child = find_task_by_pid((int)pid);
	if (!child)
		return -ESRCH;

	if (request == PTRACE_SIX_GET_PROC) {
		struct six_ptrace_proc_info *info = (struct six_ptrace_proc_info *)data;
		const char *exe;
		if (!info || (unsigned long)info < 0x03000000UL ||
		    (unsigned long)info + sizeof(*info) > TASK_SIZE)
			return -EFAULT;
		memset(info, 0, sizeof(*info));
		info->pid = child->pid;
		info->ppid = child->p_pptr ? child->p_pptr->pid : 0;
		info->state = (int)child->state;
		strncpy(info->comm, child->comm, sizeof(info->comm) - 1);
		exe = six_get_task_exe(child);
		if (exe && exe[0])
			strncpy(info->exe_path, exe, sizeof(info->exe_path) - 1);
		else if (child->comm[0]) {
			strcpy(info->exe_path, "/bin/");
			strncat(info->exe_path, child->comm, sizeof(info->exe_path) - 7);
		}
		if (child->mm) {
			info->start_code  = child->mm->start_code;
			info->end_code    = child->mm->end_code;
			info->start_data  = child->mm->start_data;
			info->end_data    = child->mm->end_data;
			info->start_brk   = child->mm->start_brk;
			info->brk         = child->mm->brk;
			info->start_stack = child->mm->start_stack;
		}
		return 0;
	}

	if (request == PTRACE_ATTACH) {
		if (child == current)
			return -EPERM;
		if (child->flags & PF_PTRACED)
			return -EPERM;
		child->flags |= PF_PTRACED;
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		send_sig(SIGSTOP, child, 1);
		return 0;
	}

	if (!(child->flags & PF_PTRACED))
		return -ESRCH;
	if (child->state != TASK_STOPPED && request != PTRACE_KILL)
		return -ESRCH;
	if (child->p_pptr != current)
		return -ESRCH;

	switch (request) {
	case PTRACE_PEEKTEXT:
	case PTRACE_PEEKDATA: {
		unsigned long word = 0;
		ret = six_ptrace_access_mem(child, (unsigned long)addr, &word, sizeof(word), 0);
		if (ret < 0)
			return ret;
		if ((unsigned long)data >= 0x03000000UL &&
		    (unsigned long)data + sizeof(word) <= TASK_SIZE) {
			*(unsigned long *)data = word;
		}
		return 0;
	}

	case PTRACE_POKETEXT:
	case PTRACE_POKEDATA: {
		unsigned long word = (unsigned long)data;
		return six_ptrace_access_mem(child, (unsigned long)addr, &word, sizeof(word), 1);
	}

	case PTRACE_PEEKUSR: {
		struct user_regs_struct ur;
		unsigned long word;
		if (addr < 0 || (unsigned long)addr + 4 > sizeof(ur) || (addr & 3))
			return -EIO;
		pt_regs_to_user_regs(&child->ucontext, &ur);
		word = ((unsigned long *)&ur)[addr >> 2];
		if ((unsigned long)data >= 0x03000000UL &&
		    (unsigned long)data + sizeof(word) <= TASK_SIZE) {
			*(unsigned long *)data = word;
		}
		return 0;
	}

	case PTRACE_POKEUSR: {
		struct user_regs_struct ur;
		if (addr < 0 || (unsigned long)addr + 4 > sizeof(ur) || (addr & 3))
			return -EIO;
		pt_regs_to_user_regs(&child->ucontext, &ur);
		((unsigned long *)&ur)[addr >> 2] = (unsigned long)data;
		user_regs_to_pt_regs(&ur, &child->ucontext);
		return 0;
	}

	case PTRACE_GETREGS: {
		struct user_regs_struct *dst = (struct user_regs_struct *)data;
		if (!dst || (unsigned long)dst < 0x03000000UL ||
		    (unsigned long)dst + sizeof(*dst) > TASK_SIZE)
			return -EFAULT;
		pt_regs_to_user_regs(&child->ucontext, dst);
		return 0;
	}

	case PTRACE_SETREGS: {
		const struct user_regs_struct *src = (const struct user_regs_struct *)data;
		if (!src || (unsigned long)src < 0x03000000UL ||
		    (unsigned long)src + sizeof(*src) > TASK_SIZE)
			return -EFAULT;
		user_regs_to_pt_regs(src, &child->ucontext);
		return 0;
	}

	case PTRACE_SIX_READMEM: {
		struct six_ptrace_mem_req *req = (struct six_ptrace_mem_req *)data;
		if (!req || (unsigned long)req < 0x03000000UL ||
		    (unsigned long)req + sizeof(*req) > TASK_SIZE)
			return -EFAULT;
		if (!req->buf || req->len < 0 ||
		    (unsigned long)req->buf < 0x03000000UL ||
		    (unsigned long)req->buf + req->len > TASK_SIZE)
			return -EFAULT;
		return six_ptrace_access_mem(child, req->addr, req->buf, req->len, 0);
	}

	case PTRACE_SIX_WRITEMEM: {
		struct six_ptrace_mem_req *req = (struct six_ptrace_mem_req *)data;
		if (!req || (unsigned long)req < 0x03000000UL ||
		    (unsigned long)req + sizeof(*req) > TASK_SIZE)
			return -EFAULT;
		if (!req->buf || req->len < 0 ||
		    (unsigned long)req->buf < 0x03000000UL ||
		    (unsigned long)req->buf + req->len > TASK_SIZE)
			return -EFAULT;
		return six_ptrace_access_mem(child, req->addr, req->buf, req->len, 1);
	}

	case PTRACE_SYSCALL:
	case PTRACE_CONT:
		if ((unsigned long)data > NSIG)
			return -EIO;
		if (request == PTRACE_SYSCALL)
			child->flags |= PF_TRACESYS;
		else
			child->flags &= ~PF_TRACESYS;
		child->exit_code = (int)data;
		if (addr != 1 && (unsigned long)addr >= 0x03000000UL &&
		    (unsigned long)addr < TASK_SIZE)
			child->ucontext.pc = (unsigned int)addr;
		six_ptrace_disarm_step(child);
		wake_up_process(child);
		return 0;

	case PTRACE_SINGLESTEP:
		if ((unsigned long)data > NSIG)
			return -EIO;
		child->flags &= ~PF_TRACESYS;
		if (addr != 1 && (unsigned long)addr >= 0x03000000UL &&
		    (unsigned long)addr < TASK_SIZE)
			child->ucontext.pc = (unsigned int)addr;
		six_ptrace_arm_step(child);
		child->exit_code = (int)data;
		wake_up_process(child);
		return 0;

	case PTRACE_KILL:
		six_ptrace_disarm_step(child);
		if (child->state == TASK_ZOMBIE)
			return 0;
		child->exit_code = SIGKILL;
		wake_up_process(child);
		return 0;

	case PTRACE_DETACH:
		if ((unsigned long)data > NSIG)
			return -EIO;
		six_ptrace_disarm_step(child);
		child->flags &= ~(PF_PTRACED | PF_TRACESYS);
		child->exit_code = (int)data;
		REMOVE_LINKS(child);
		child->p_pptr = child->p_opptr;
		SET_LINKS(child);
		wake_up_process(child);
		return 0;

	default:
		return -EIO;
	}
}
