/*
 * applications/gdb/gdb.c
 *
 * SIX Symbolic Source & Machine-Level Debugger (/bin/gdb)
 *
 * Supports:
 *   1. Interactive source-level & assembly-level debugging inside SIX
 *      (on the console or via `sadb shell`):
 *        - Full C source line stepping (`step`, `next`, `finish`, `list`)
 *        - Instruction-level stepping (`stepi`, `nexti`, `disassemble`)
 *        - Symbolic & line breakpoints (`break main`, `break power.c:45`, `break *0x...`)
 *        - Register & memory inspection/modification (`info regs`, `x/10i $eip`,
 *          `x/4xw $esp`, `x/s`, `print`, `set $eax = ...`)
 *        - Stack backtraces (`bt`) with function names, args, and `file:line`
 *        - Live process attach/detach (`gdb -p <pid>`, `attach <pid>`, `detach`)
 *        - Both host-built `/bin/*` binaries (DWARF `.debug_line` + `.symtab`)
 *          and in-guest TinyCC (`tcc -g`) binaries (STABS `.stab` + `.symtab`).
 *   2. GDB Remote Serial Protocol (RSP) server mode (`gdb --rsp <prog>` or
 *      `gdb --rsp -p <pid>`) so external host `/usr/bin/gdb` can attach
 *      transparently via `./sadb gdb`.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>
#include <sys/wait.h>
#include <sys/ptrace.h>

/* Minimal ELF32 definitions for reading .symtab/.strtab and .stab/.stabstr */
typedef unsigned short Elf32_Half;
typedef unsigned int   Elf32_Word;
typedef unsigned int   Elf32_Addr;
typedef unsigned int   Elf32_Off;

#define EI_NIDENT 16
typedef struct {
	unsigned char e_ident[EI_NIDENT];
	Elf32_Half    e_type;
	Elf32_Half    e_machine;
	Elf32_Word    e_version;
	Elf32_Addr    e_entry;
	Elf32_Off     e_phoff;
	Elf32_Off     e_shoff;
	Elf32_Word    e_flags;
	Elf32_Half    e_ehsize;
	Elf32_Half    e_phentsize;
	Elf32_Half    e_phnum;
	Elf32_Half    e_shentsize;
	Elf32_Half    e_shnum;
	Elf32_Half    e_shstrndx;
} Elf32_Ehdr;

typedef struct {
	Elf32_Word sh_name;
	Elf32_Word sh_type;
	Elf32_Word sh_flags;
	Elf32_Addr sh_addr;
	Elf32_Off  sh_offset;
	Elf32_Word sh_size;
	Elf32_Word sh_link;
	Elf32_Word sh_info;
	Elf32_Word sh_addralign;
	Elf32_Word sh_entsize;
} Elf32_Shdr;

typedef struct {
	Elf32_Word    st_name;
	Elf32_Addr    st_value;
	Elf32_Word    st_size;
	unsigned char st_info;
	unsigned char st_other;
	Elf32_Half    st_shndx;
} Elf32_Sym;

typedef struct {
	unsigned int   n_strx;
	unsigned char  n_type;
	unsigned char  n_other;
	unsigned short n_desc;
	unsigned int   n_value;
} Stab_Sym;

#define N_FUN   0x24
#define N_SLINE 0x44
#define N_SO    0x64
#define N_SOL   0x84

#define MAX_SYMS   1024
#define MAX_LINES  2048
#define MAX_FILES  32
#define MAX_BPS    32
#define MAX_ARGS   16
#define MAX_EX_CMDS 32

static struct six_dbg_sym  g_syms[MAX_SYMS];
static int                 g_num_syms = 0;
static struct six_dbg_line g_lines[MAX_LINES];
static int                 g_num_lines = 0;
static char                g_files[MAX_FILES][96];
static int                 g_num_files = 0;

struct bp_entry {
	int id;
	int used;
	int enabled;
	int planted;
	int temporary;
	unsigned long addr;
	unsigned char orig_byte;
	char desc[64];
};

static struct bp_entry g_bps[MAX_BPS];
static int             g_next_bp_id = 1;

static char g_exe_path[128];
static char g_arg_storage[MAX_ARGS][128];
static int  g_argc = 0;

static int  g_child_pid = -1;
static int  g_attached = 0;
static int  g_last_sig = 0;
static struct user_regs_struct g_regs;

static int  g_list_file_idx = 0;
static int  g_list_line = 1;
static unsigned long g_last_x_addr = 0x03000000UL;
static int  g_last_x_count = 4;
static char g_last_x_fmt = 'x';
static char g_last_x_size = 'w';
static char g_last_cmd[128] = "";

/* -------------------------------------------------------------------------
 * Tracee Memory & Register Helpers
 * ------------------------------------------------------------------------- */
static int tracee_read_mem(unsigned long addr, void *buf, int len)
{
	struct six_ptrace_mem_req req;
	if (g_child_pid <= 0 || len <= 0)
		return -1;
	req.addr = addr;
	req.buf = buf;
	req.len = len;
	return (int)ptrace(PTRACE_SIX_READMEM, g_child_pid, 0, (long)&req);
}

static int tracee_write_mem(unsigned long addr, const void *buf, int len)
{
	struct six_ptrace_mem_req req;
	if (g_child_pid <= 0 || len <= 0)
		return -1;
	req.addr = addr;
	req.buf = (void *)buf;
	req.len = len;
	return (int)ptrace(PTRACE_SIX_WRITEMEM, g_child_pid, 0, (long)&req);
}

static int tracee_get_regs(void)
{
	if (g_child_pid <= 0)
		return -1;
	return (int)ptrace(PTRACE_GETREGS, g_child_pid, 0, (long)&g_regs);
}

static int tracee_set_regs(void)
{
	if (g_child_pid <= 0)
		return -1;
	return (int)ptrace(PTRACE_SETREGS, g_child_pid, 0, (long)&g_regs);
}

/* -------------------------------------------------------------------------
 * Symbol, Line Table & Source File Helpers
 * ------------------------------------------------------------------------- */
static int find_or_add_src_file(const char *path)
{
	int i;
	if (!path || !path[0])
		return 0;
	for (i = 0; i < g_num_files; i++) {
		if (strcmp(g_files[i], path) == 0)
			return i;
	}
	if (g_num_files >= MAX_FILES)
		return g_num_files - 1;
	i = g_num_files++;
	strncpy(g_files[i], path, 95);
	g_files[i][95] = '\0';
	return i;
}

static struct six_dbg_sym *lookup_sym_by_name(const char *name);

static void load_elf_sym_and_stabs(const char *path)
{
	int fd;
	Elf32_Ehdr ehdr;
	Elf32_Shdr *shdrs = NULL;
	char *shstr = NULL;
	int i;
	int sym_sh = -1, str_sh = -1, stab_sh = -1, stabstr_sh = -1;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return;
	if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr) ||
	    ehdr.e_ident[0] != 0x7f || ehdr.e_ident[1] != 'E' ||
	    ehdr.e_ident[2] != 'L'  || ehdr.e_ident[3] != 'F' ||
	    ehdr.e_shnum == 0 || ehdr.e_shnum > 64) {
		close(fd);
		return;
	}

	shdrs = (Elf32_Shdr *)malloc(ehdr.e_shnum * sizeof(Elf32_Shdr));
	if (!shdrs) {
		close(fd);
		return;
	}
	lseek(fd, ehdr.e_shoff, SEEK_SET);
	if (read(fd, shdrs, ehdr.e_shnum * sizeof(Elf32_Shdr)) !=
	    (int)(ehdr.e_shnum * sizeof(Elf32_Shdr))) {
		free(shdrs);
		close(fd);
		return;
	}

	if (ehdr.e_shstrndx < ehdr.e_shnum && shdrs[ehdr.e_shstrndx].sh_size < 16384) {
		shstr = (char *)malloc(shdrs[ehdr.e_shstrndx].sh_size + 1);
		if (shstr) {
			lseek(fd, shdrs[ehdr.e_shstrndx].sh_offset, SEEK_SET);
			read(fd, shstr, shdrs[ehdr.e_shstrndx].sh_size);
			shstr[shdrs[ehdr.e_shstrndx].sh_size] = '\0';
		}
	}

	for (i = 0; i < ehdr.e_shnum; i++) {
		const char *sname = shstr ? (shstr + shdrs[i].sh_name) : "";
		if (strcmp(sname, ".symtab") == 0) {
			sym_sh = i;
			str_sh = shdrs[i].sh_link;
		} else if (strcmp(sname, ".stab") == 0) {
			stab_sh = i;
		} else if (strcmp(sname, ".stabstr") == 0) {
			stabstr_sh = i;
		}
	}

	/* Load .symtab if g_num_syms is empty */
	if (g_num_syms == 0 && sym_sh >= 0 && str_sh >= 0 && str_sh < ehdr.e_shnum &&
	    shdrs[str_sh].sh_size < 65536) {
		char *strtab = (char *)malloc(shdrs[str_sh].sh_size + 1);
		if (strtab) {
			int n = shdrs[sym_sh].sh_size / sizeof(Elf32_Sym);
			int k;
			lseek(fd, shdrs[str_sh].sh_offset, SEEK_SET);
			read(fd, strtab, shdrs[str_sh].sh_size);
			strtab[shdrs[str_sh].sh_size] = '\0';

			lseek(fd, shdrs[sym_sh].sh_offset, SEEK_SET);
			for (k = 0; k < n && g_num_syms < MAX_SYMS; k++) {
				Elf32_Sym es;
				unsigned char st_type;
				if (read(fd, &es, sizeof(es)) != sizeof(es))
					break;
				st_type = es.st_info & 0xf;
				if (es.st_value >= 0x03000000UL && es.st_name > 0 &&
				    es.st_name < shdrs[str_sh].sh_size &&
				    (st_type == 1 || st_type == 2 || es.st_shndx != 0)) {
					const char *nm = strtab + es.st_name;
					if (nm[0] && nm[0] != '.') {
						g_syms[g_num_syms].addr = es.st_value;
						g_syms[g_num_syms].size = es.st_size;
						g_syms[g_num_syms].type = (st_type == 2) ? 'T' : 'D';
						strncpy(g_syms[g_num_syms].name, nm, 54);
						g_syms[g_num_syms].name[54] = '\0';
						g_num_syms++;
					}
				}
			}
			free(strtab);
			/* Sort symbols by address */
			for (k = 1; k < g_num_syms; k++) {
				struct six_dbg_sym key = g_syms[k];
				int j = k - 1;
				while (j >= 0 && g_syms[j].addr > key.addr) {
					g_syms[j + 1] = g_syms[j];
					j--;
				}
				g_syms[j + 1] = key;
			}
			for (k = 0; k + 1 < g_num_syms; k++) {
				if (g_syms[k].size == 0 && g_syms[k + 1].addr > g_syms[k].addr)
					g_syms[k].size = g_syms[k + 1].addr - g_syms[k].addr;
			}
		}
	}

	/* Load .stab / .stabstr (from tcc -g) if g_num_lines is empty */
	if (g_num_lines == 0 && stab_sh >= 0 && stabstr_sh >= 0 &&
	    shdrs[stabstr_sh].sh_size < 65536) {
		char *stabstr = (char *)malloc(shdrs[stabstr_sh].sh_size + 1);
		if (stabstr) {
			int n = shdrs[stab_sh].sh_size / sizeof(Stab_Sym);
			int k, cur_file = 0;
			unsigned long func_addr = 0;
			lseek(fd, shdrs[stabstr_sh].sh_offset, SEEK_SET);
			read(fd, stabstr, shdrs[stabstr_sh].sh_size);
			stabstr[shdrs[stabstr_sh].sh_size] = '\0';

			lseek(fd, shdrs[stab_sh].sh_offset, SEEK_SET);
			for (k = 0; k < n && g_num_lines < MAX_LINES; k++) {
				Stab_Sym st;
				if (read(fd, &st, sizeof(st)) != sizeof(st))
					break;
				if (st.n_type == N_SO || st.n_type == N_SOL) {
					if (st.n_strx > 0 && st.n_strx < shdrs[stabstr_sh].sh_size) {
						const char *s = stabstr + st.n_strx;
						int slen = strlen(s);
						if (slen > 0 && s[slen - 1] != '/')
							cur_file = find_or_add_src_file(s);
					}
				} else if (st.n_type == N_FUN) {
					if (st.n_strx != 0 && st.n_strx < shdrs[stabstr_sh].sh_size) {
						func_addr = st.n_value;
						if (func_addr < 0x03000000UL) {
							char fname[64];
							const char *s = stabstr + st.n_strx;
							const char *colon = strchr(s, ':');
							size_t flen = colon ? (size_t)(colon - s) : strlen(s);
							struct six_dbg_sym *fsym;
							if (flen >= sizeof(fname))
								flen = sizeof(fname) - 1;
							memcpy(fname, s, flen);
							fname[flen] = '\0';
							fsym = lookup_sym_by_name(fname);
							if (fsym)
								func_addr = fsym->addr;
						}
					} else {
						func_addr = 0;
					}
				} else if (st.n_type == N_SLINE) {
					unsigned long laddr = st.n_value;
					if (laddr < 0x03000000UL && func_addr >= 0x03000000UL)
						laddr += func_addr;
					if (laddr >= 0x03000000UL && st.n_desc > 0) {
						if (g_num_lines > 0 &&
						    g_lines[g_num_lines - 1].addr == laddr) {
							g_lines[g_num_lines - 1].line = st.n_desc;
							g_lines[g_num_lines - 1].file_idx = cur_file;
						} else {
							g_lines[g_num_lines].addr = laddr;
							g_lines[g_num_lines].line = st.n_desc;
							g_lines[g_num_lines].file_idx = cur_file;
							g_num_lines++;
						}
					}
				}
			}
			free(stabstr);
		}
	}

	if (shstr)
		free(shstr);
	free(shdrs);
	close(fd);
}

static void load_debug_info(const char *exe_path, int quiet)
{
	struct six_ptrace_dbg_req req;

	g_num_syms = 0;
	g_num_lines = 0;
	g_num_files = 0;

	memset(&req, 0, sizeof(req));
	req.exe_path  = exe_path;
	req.syms      = g_syms;
	req.max_syms  = MAX_SYMS;
	req.lines     = g_lines;
	req.max_lines = MAX_LINES;
	req.files     = g_files;
	req.max_files = MAX_FILES;

	if (ptrace(PTRACE_SIX_LOAD_DBG, 0, 0, (long)&req) == 0) {
		g_num_syms  = req.num_syms;
		g_num_lines = req.num_lines;
		g_num_files = req.num_files;
	}

	/* Also inspect guest ELF directly (for in-guest tcc -g binaries or local .symtab) */
	load_elf_sym_and_stabs(exe_path);

	/* Pick default primary source file (prefer application's own .c over cstart.c) */
	g_list_file_idx = 0;
	if (g_num_files > 1) {
		int i;
		for (i = 0; i < g_num_files; i++) {
			if (strstr(g_files[i], "cstart.c") == NULL) {
				g_list_file_idx = i;
				break;
			}
		}
	}

	if (!quiet) {
		if (g_num_syms > 0 || g_num_lines > 0) {
			printf("Reading symbols from %s... done (%d symbols, %d source lines)\n",
			       exe_path, g_num_syms, g_num_lines);
		} else {
			printf("Reading symbols from %s... (no debugging symbols found)\n",
			       exe_path);
		}
	}
}

static struct six_dbg_sym *lookup_sym_by_name(const char *name)
{
	int i;
	for (i = 0; i < g_num_syms; i++) {
		if (strcmp(g_syms[i].name, name) == 0)
			return &g_syms[i];
	}
	return NULL;
}

static struct six_dbg_sym *lookup_sym_by_addr(unsigned long addr, unsigned long *off_out)
{
	int i, best = -1;
	for (i = 0; i < g_num_syms; i++) {
		if (g_syms[i].addr <= addr)
			best = i;
		else
			break;
	}
	if (best >= 0) {
		unsigned long diff = addr - g_syms[best].addr;
		unsigned long max_span = g_syms[best].size ? g_syms[best].size : 0x4000UL;
		if (diff < max_span) {
			if (off_out)
				*off_out = diff;
			return &g_syms[best];
		}
	}
	return NULL;
}

static int lookup_line_by_addr(unsigned long addr, int *file_idx_out, int *line_out)
{
	int i, best = -1;
	for (i = 0; i < g_num_lines; i++) {
		if (g_lines[i].addr <= addr)
			best = i;
		else
			break;
	}
	if (best >= 0 && (addr - g_lines[best].addr) < 0x400UL) {
		if (file_idx_out)
			*file_idx_out = g_lines[best].file_idx;
		if (line_out)
			*line_out = g_lines[best].line;
		return 1;
	}
	return 0;
}

static unsigned long lookup_addr_by_line(const char *file_match, int target_line,
					 int *actual_file_out, int *actual_line_out)
{
	int i, best = -1;
	for (i = 0; i < g_num_lines; i++) {
		int fidx = g_lines[i].file_idx;
		if (file_match && file_match[0]) {
			const char *f = (fidx < g_num_files) ? g_files[fidx] : "";
			const char *base = strrchr(f, '/');
			base = base ? (base + 1) : f;
			if (strcmp(f, file_match) != 0 && strcmp(base, file_match) != 0)
				continue;
		} else if (fidx != g_list_file_idx) {
			continue;
		}
		if (g_lines[i].line >= target_line) {
			if (best < 0 || g_lines[i].line < g_lines[best].line ||
			    (g_lines[i].line == g_lines[best].line &&
			     g_lines[i].addr < g_lines[best].addr)) {
				best = i;
			}
		}
	}
	if (best >= 0) {
		if (actual_file_out)
			*actual_file_out = g_lines[best].file_idx;
		if (actual_line_out)
			*actual_line_out = g_lines[best].line;
		return g_lines[best].addr;
	}
	return 0;
}

/*
 * Find the first source-line address inside a function (skipping the standard
 * `push %ebp; mov %esp, %ebp; sub $N, %esp` prologue if a second line entry
 * exists within the first 16 bytes of the function).
 */
static unsigned long find_func_entry_addr(struct six_dbg_sym *sym,
					  int *file_idx_out, int *line_out)
{
	int i;
	if (!sym)
		return 0;
	for (i = 0; i < g_num_lines; i++) {
		if (g_lines[i].addr >= sym->addr &&
		    g_lines[i].addr < sym->addr + 32) {
			int pick = i;
			if (i + 1 < g_num_lines &&
			    g_lines[i].addr == sym->addr &&
			    g_lines[i + 1].addr > sym->addr &&
			    g_lines[i + 1].addr <= sym->addr + 16 &&
			    g_lines[i + 1].file_idx == g_lines[i].file_idx) {
				pick = i + 1;
			}
			if (file_idx_out)
				*file_idx_out = g_lines[pick].file_idx;
			if (line_out)
				*line_out = g_lines[pick].line;
			return g_lines[pick].addr;
		}
	}
	if (lookup_line_by_addr(sym->addr, file_idx_out, line_out))
		return sym->addr;
	return sym->addr;
}

static int read_source_line(const char *file, int line_no, char *buf, int buflen)
{
	FILE *fp;
	struct six_ptrace_srcline_req req;

	if (!file || !file[0] || line_no <= 0 || !buf || buflen <= 0)
		return -1;
	buf[0] = '\0';

	/* 1. Try reading directly from guest VFS (e.g., /etc/demos/hello.c) */
	fp = fopen(file, "r");
	if (fp) {
		char line[256];
		int cur = 0;
		while (fgets(line, sizeof(line), fp)) {
			cur++;
			if (cur == line_no) {
				int len = strlen(line);
				while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
					line[--len] = '\0';
				strncpy(buf, line, buflen - 1);
				buf[buflen - 1] = '\0';
				fclose(fp);
				return strlen(buf);
			}
		}
		fclose(fp);
	}

	/* 2. Fallback to host workspace source file via PTRACE_SIX_SRCLINE */
	req.file   = file;
	req.line   = line_no;
	req.buf    = buf;
	req.buflen = buflen;
	if (ptrace(PTRACE_SIX_SRCLINE, 0, 0, (long)&req) >= 0)
		return strlen(buf);

	return -1;
}

/* -------------------------------------------------------------------------
 * Built-in 32-bit x86 Disassembler
 * ------------------------------------------------------------------------- */
static const char *reg32_names[8] = {
	"%eax", "%ecx", "%edx", "%ebx", "%esp", "%ebp", "%esi", "%edi"
};
static const char *reg16_names[8] = {
	"%ax", "%cx", "%dx", "%bx", "%sp", "%bp", "%si", "%di"
};
static const char *reg8_names[8] = {
	"%al", "%cl", "%dl", "%bl", "%ah", "%ch", "%dh", "%bh"
};
static const char *cc_names[16] = {
	"o", "no", "b", "ae", "e", "ne", "be", "a",
	"s", "ns", "p", "np", "l", "ge", "le", "g"
};
static const char *alu_names[8] = {
	"add", "or", "adc", "sbb", "and", "sub", "xor", "cmp"
};
static const char *shift_names[8] = {
	"rol", "ror", "rcl", "rcr", "shl", "shr", "sal", "sar"
};

static int format_modrm(const unsigned char *code, int pos, int size_bytes,
			char *rm_buf, int rm_len, int *reg_out)
{
	unsigned char modrm = code[pos++];
	int mod = (modrm >> 6) & 3;
	int reg = (modrm >> 3) & 7;
	int rm  = modrm & 7;
	int disp = 0;

	if (reg_out)
		*reg_out = reg;

	if (mod == 3) {
		const char **rnames = (size_bytes == 1) ? reg8_names :
				      (size_bytes == 2) ? reg16_names : reg32_names;
		snprintf(rm_buf, rm_len, "%s", rnames[rm]);
		return pos;
	}

	if (rm == 4) {
		unsigned char sib = code[pos++];
		int scale = 1 << ((sib >> 6) & 3);
		int index = (sib >> 3) & 7;
		int base  = sib & 7;
		char idx_part[24] = "";

		if (index != 4)
			snprintf(idx_part, sizeof(idx_part), ",%s,%d", reg32_names[index], scale);

		if (base == 5 && mod == 0) {
			memcpy(&disp, &code[pos], 4);
			pos += 4;
			snprintf(rm_buf, rm_len, "0x%x(,%s,%d)",
				 (unsigned int)disp,
				 (index != 4) ? reg32_names[index] : "%eiz", scale);
			return pos;
		}
		if (mod == 1) {
			disp = (signed char)code[pos++];
			snprintf(rm_buf, rm_len, "%d(%s%s)", disp, reg32_names[base], idx_part);
		} else if (mod == 2) {
			memcpy(&disp, &code[pos], 4);
			pos += 4;
			snprintf(rm_buf, rm_len, "%d(%s%s)", disp, reg32_names[base], idx_part);
		} else {
			snprintf(rm_buf, rm_len, "(%s%s)", reg32_names[base], idx_part);
		}
		return pos;
	}

	if (rm == 5 && mod == 0) {
		memcpy(&disp, &code[pos], 4);
		pos += 4;
		snprintf(rm_buf, rm_len, "0x%x", (unsigned int)disp);
		return pos;
	}

	if (mod == 1) {
		disp = (signed char)code[pos++];
		snprintf(rm_buf, rm_len, "%d(%s)", disp, reg32_names[rm]);
	} else if (mod == 2) {
		memcpy(&disp, &code[pos], 4);
		pos += 4;
		snprintf(rm_buf, rm_len, "%d(%s)", disp, reg32_names[rm]);
	} else {
		snprintf(rm_buf, rm_len, "(%s)", reg32_names[rm]);
	}
	return pos;
}

/*
 * Disassemble one x86 instruction at `pc` from `code[0..15]`, returning byte length.
 */
static int disasm_one(unsigned long pc, const unsigned char *code,
		      char *out, int outlen, int *is_call_out)
{
	int pos = 0, op16 = 0;
	unsigned char op;
	char rm[64];
	int reg = 0;

	if (is_call_out)
		*is_call_out = 0;

	while (pos < 4) {
		op = code[pos];
		if (op == 0x66) {
			op16 = 1;
			pos++;
		} else if (op == 0xF2 || op == 0xF3 || op == 0xF0) {
			pos++;
		} else {
			break;
		}
	}

	op = code[pos++];

	if (op == 0x90) {
		snprintf(out, outlen, "nop");
		return pos;
	}
	if (op == 0xC3) {
		snprintf(out, outlen, "ret");
		return pos;
	}
	if (op == 0xC9) {
		snprintf(out, outlen, "leave");
		return pos;
	}
	if (op == 0xCC) {
		snprintf(out, outlen, "int3");
		return pos;
	}
	if (op >= 0x50 && op <= 0x57) {
		snprintf(out, outlen, "push   %s", reg32_names[op & 7]);
		return pos;
	}
	if (op >= 0x58 && op <= 0x5F) {
		snprintf(out, outlen, "pop    %s", reg32_names[op & 7]);
		return pos;
	}
	if (op >= 0x40 && op <= 0x47) {
		snprintf(out, outlen, "inc    %s", reg32_names[op & 7]);
		return pos;
	}
	if (op >= 0x48 && op <= 0x4F) {
		snprintf(out, outlen, "dec    %s", reg32_names[op & 7]);
		return pos;
	}
	if (op >= 0xB8 && op <= 0xBF) {
		unsigned int imm = 0;
		memcpy(&imm, &code[pos], 4);
		pos += 4;
		snprintf(out, outlen, "mov    $0x%x,%s", imm, reg32_names[op & 7]);
		return pos;
	}
	if (op >= 0xB0 && op <= 0xB7) {
		unsigned char imm = code[pos++];
		snprintf(out, outlen, "movb   $0x%x,%s", imm, reg8_names[op & 7]);
		return pos;
	}
	if (op == 0x68) {
		unsigned int imm = 0;
		memcpy(&imm, &code[pos], 4);
		pos += 4;
		snprintf(out, outlen, "push   $0x%x", imm);
		return pos;
	}
	if (op == 0x6A) {
		int imm = (signed char)code[pos++];
		snprintf(out, outlen, "push   $0x%x", (unsigned int)imm);
		return pos;
	}
	if (op == 0xCD) {
		unsigned char vec = code[pos++];
		snprintf(out, outlen, "int    $0x%x", vec);
		return pos;
	}
	if (op == 0xE8 || op == 0xE9) {
		int rel = 0;
		unsigned long dst;
		unsigned long off = 0;
		struct six_dbg_sym *sym;
		memcpy(&rel, &code[pos], 4);
		pos += 4;
		dst = pc + (unsigned long)pos + (unsigned long)rel;
		if (op == 0xE8 && is_call_out)
			*is_call_out = 1;
		sym = lookup_sym_by_addr(dst, &off);
		if (sym && off == 0)
			snprintf(out, outlen, "%s   0x%08lx <%s>",
				 (op == 0xE8) ? "call" : "jmp ", dst, sym->name);
		else if (sym)
			snprintf(out, outlen, "%s   0x%08lx <%s+0x%lx>",
				 (op == 0xE8) ? "call" : "jmp ", dst, sym->name, off);
		else
			snprintf(out, outlen, "%s   0x%08lx",
				 (op == 0xE8) ? "call" : "jmp ", dst);
		return pos;
	}
	if (op == 0xEB) {
		int rel = (signed char)code[pos++];
		unsigned long dst = pc + (unsigned long)pos + (unsigned long)rel;
		snprintf(out, outlen, "jmp    0x%08lx", dst);
		return pos;
	}
	if (op >= 0x70 && op <= 0x7F) {
		int rel = (signed char)code[pos++];
		unsigned long dst = pc + (unsigned long)pos + (unsigned long)rel;
		snprintf(out, outlen, "j%-2s    0x%08lx", cc_names[op & 15], dst);
		return pos;
	}
	if (op == 0x0F) {
		unsigned char op2 = code[pos++];
		if (op2 >= 0x80 && op2 <= 0x8F) {
			int rel = 0;
			unsigned long dst;
			memcpy(&rel, &code[pos], 4);
			pos += 4;
			dst = pc + (unsigned long)pos + (unsigned long)rel;
			snprintf(out, outlen, "j%-2s    0x%08lx", cc_names[op2 & 15], dst);
			return pos;
		}
		if (op2 == 0xB6 || op2 == 0xB7 || op2 == 0xBE || op2 == 0xBF) {
			pos = format_modrm(code, pos, (op2 & 1) ? 2 : 1, rm, sizeof(rm), &reg);
			snprintf(out, outlen, "mov%c%c  %s,%s",
				 (op2 & 8) ? 's' : 'z',
				 (op2 & 1) ? 'w' : 'b', rm, reg32_names[reg]);
			return pos;
		}
		if (op2 == 0xAF) {
			pos = format_modrm(code, pos, 4, rm, sizeof(rm), &reg);
			snprintf(out, outlen, "imul   %s,%s", rm, reg32_names[reg]);
			return pos;
		}
		if (op2 >= 0x90 && op2 <= 0x9F) {
			pos = format_modrm(code, pos, 1, rm, sizeof(rm), &reg);
			snprintf(out, outlen, "set%-2s  %s", cc_names[op2 & 15], rm);
			return pos;
		}
		pos = format_modrm(code, pos, 4, rm, sizeof(rm), &reg);
		snprintf(out, outlen, "op0f_%02x %s,%s", op2, rm, reg32_names[reg]);
		return pos;
	}
	if ((op & 0xC4) == 0x00) { /* Standard ALU reg/mem */
		int alu = (op >> 3) & 7;
		int d   = (op >> 1) & 1;
		int w   = op & 1;
		if ((op & 6) == 4) { /* AL / EAX, imm */
			if (w == 0) {
				unsigned char imm = code[pos++];
				snprintf(out, outlen, "%-6s $0x%x,%%al", alu_names[alu], imm);
			} else {
				unsigned int imm = 0;
				memcpy(&imm, &code[pos], 4);
				pos += 4;
				snprintf(out, outlen, "%-6s $0x%x,%%eax", alu_names[alu], imm);
			}
			return pos;
		}
		pos = format_modrm(code, pos, w ? 4 : 1, rm, sizeof(rm), &reg);
		if (d)
			snprintf(out, outlen, "%-6s %s,%s",
				 alu_names[alu], rm, w ? reg32_names[reg] : reg8_names[reg]);
		else
			snprintf(out, outlen, "%-6s %s,%s",
				 alu_names[alu], w ? reg32_names[reg] : reg8_names[reg], rm);
		return pos;
	}
	if (op == 0x80 || op == 0x81 || op == 0x83) {
		int w = op & 1;
		int imm = 0;
		pos = format_modrm(code, pos, w ? 4 : 1, rm, sizeof(rm), &reg);
		if (op == 0x81) {
			memcpy(&imm, &code[pos], op16 ? 2 : 4);
			pos += op16 ? 2 : 4;
		} else {
			imm = (signed char)code[pos++];
		}
		snprintf(out, outlen, "%-6s $0x%x,%s", alu_names[reg], (unsigned int)imm, rm);
		return pos;
	}
	if (op == 0x84 || op == 0x85) {
		int w = op & 1;
		pos = format_modrm(code, pos, w ? 4 : 1, rm, sizeof(rm), &reg);
		snprintf(out, outlen, "test   %s,%s",
			 w ? reg32_names[reg] : reg8_names[reg], rm);
		return pos;
	}
	if (op >= 0x88 && op <= 0x8B) {
		int d = (op >> 1) & 1;
		int w = op & 1;
		pos = format_modrm(code, pos, w ? 4 : 1, rm, sizeof(rm), &reg);
		if (d)
			snprintf(out, outlen, "mov    %s,%s",
				 rm, w ? reg32_names[reg] : reg8_names[reg]);
		else
			snprintf(out, outlen, "mov    %s,%s",
				 w ? reg32_names[reg] : reg8_names[reg], rm);
		return pos;
	}
	if (op == 0x8D) {
		pos = format_modrm(code, pos, 4, rm, sizeof(rm), &reg);
		snprintf(out, outlen, "lea    %s,%s", rm, reg32_names[reg]);
		return pos;
	}
	if (op == 0xC6 || op == 0xC7) {
		int w = op & 1;
		unsigned int imm = 0;
		pos = format_modrm(code, pos, w ? 4 : 1, rm, sizeof(rm), &reg);
		if (w) {
			memcpy(&imm, &code[pos], op16 ? 2 : 4);
			pos += op16 ? 2 : 4;
			snprintf(out, outlen, "movl   $0x%x,%s", imm, rm);
		} else {
			imm = code[pos++];
			snprintf(out, outlen, "movb   $0x%x,%s", imm, rm);
		}
		return pos;
	}
	if (op == 0xC0 || op == 0xC1 || op == 0xD0 || op == 0xD1 || op == 0xD2 || op == 0xD3) {
		int w = op & 1;
		pos = format_modrm(code, pos, w ? 4 : 1, rm, sizeof(rm), &reg);
		if (op == 0xC0 || op == 0xC1) {
			unsigned char imm = code[pos++];
			snprintf(out, outlen, "%-6s $0x%x,%s", shift_names[reg], imm, rm);
		} else if (op == 0xD2 || op == 0xD3) {
			snprintf(out, outlen, "%-6s %%cl,%s", shift_names[reg], rm);
		} else {
			snprintf(out, outlen, "%-6s $1,%s", shift_names[reg], rm);
		}
		return pos;
	}
	if (op == 0xF6 || op == 0xF7) {
		int w = op & 1;
		static const char *g3_names[8] = {
			"test", "test", "not", "neg", "mul", "imul", "div", "idiv"
		};
		pos = format_modrm(code, pos, w ? 4 : 1, rm, sizeof(rm), &reg);
		if (reg == 0 || reg == 1) {
			unsigned int imm = 0;
			if (w) {
				memcpy(&imm, &code[pos], op16 ? 2 : 4);
				pos += op16 ? 2 : 4;
			} else {
				imm = code[pos++];
			}
			snprintf(out, outlen, "test   $0x%x,%s", imm, rm);
		} else {
			snprintf(out, outlen, "%-6s %s", g3_names[reg], rm);
		}
		return pos;
	}
	if (op == 0xFF) {
		static const char *g5_names[8] = {
			"inc", "dec", "call   *", "lcall  *", "jmp    *", "ljmp   *", "push", "opff_7"
		};
		pos = format_modrm(code, pos, 4, rm, sizeof(rm), &reg);
		if (reg == 2 && is_call_out)
			*is_call_out = 1;
		snprintf(out, outlen, "%s%s%s",
			 g5_names[reg], (reg == 2 || reg == 4) ? "" : "   ", rm);
		return pos;
	}
	if (op >= 0xA0 && op <= 0xA3) {
		unsigned int moffs = 0;
		memcpy(&moffs, &code[pos], 4);
		pos += 4;
		if (op < 0xA2)
			snprintf(out, outlen, "mov    0x%x,%s", moffs, (op & 1) ? "%eax" : "%al");
		else
			snprintf(out, outlen, "mov    %s,0x%x", (op & 1) ? "%eax" : "%al", moffs);
		return pos;
	}

	snprintf(out, outlen, ".byte  0x%02x", op);
	return pos;
}

/* -------------------------------------------------------------------------
 * Breakpoint Management & Execution Control
 * ------------------------------------------------------------------------- */
static struct bp_entry *find_bp_at(unsigned long addr)
{
	int i;
	for (i = 0; i < MAX_BPS; i++) {
		if (g_bps[i].used && g_bps[i].enabled && g_bps[i].addr == addr)
			return &g_bps[i];
	}
	return NULL;
}

static void plant_all_bps(void)
{
	int i;
	unsigned char int3 = 0xcc;
	if (g_child_pid <= 0)
		return;
	for (i = 0; i < MAX_BPS; i++) {
		if (g_bps[i].used && g_bps[i].enabled && !g_bps[i].planted) {
			unsigned char b = 0;
			if (tracee_read_mem(g_bps[i].addr, &b, 1) == 0) {
				if (b != 0xcc)
					g_bps[i].orig_byte = b;
				tracee_write_mem(g_bps[i].addr, &int3, 1);
				g_bps[i].planted = 1;
			}
		}
	}
}

static void unplant_all_bps(void)
{
	int i;
	if (g_child_pid <= 0)
		return;
	for (i = 0; i < MAX_BPS; i++) {
		if (g_bps[i].used && g_bps[i].planted) {
			tracee_write_mem(g_bps[i].addr, &g_bps[i].orig_byte, 1);
			g_bps[i].planted = 0;
		}
	}
}

static int add_breakpoint(unsigned long addr, const char *desc, int temporary)
{
	int i;
	for (i = 0; i < MAX_BPS; i++) {
		if (g_bps[i].used && g_bps[i].addr == addr) {
			g_bps[i].enabled = 1;
			return g_bps[i].id;
		}
	}
	for (i = 0; i < MAX_BPS; i++) {
		if (!g_bps[i].used) {
			g_bps[i].id        = temporary ? 0 : g_next_bp_id++;
			g_bps[i].used      = 1;
			g_bps[i].enabled   = 1;
			g_bps[i].planted   = 0;
			g_bps[i].temporary = temporary;
			g_bps[i].addr      = addr;
			g_bps[i].orig_byte = 0x90;
			strncpy(g_bps[i].desc, desc ? desc : "", sizeof(g_bps[i].desc) - 1);
			g_bps[i].desc[sizeof(g_bps[i].desc) - 1] = '\0';
			if (g_child_pid > 0)
				plant_all_bps();
			return g_bps[i].id;
		}
	}
	return -1;
}

static void clear_temp_bps(void)
{
	int i;
	for (i = 0; i < MAX_BPS; i++) {
		if (g_bps[i].used && g_bps[i].temporary) {
			if (g_bps[i].planted && g_child_pid > 0)
				tracee_write_mem(g_bps[i].addr, &g_bps[i].orig_byte, 1);
			g_bps[i].used = 0;
			g_bps[i].planted = 0;
		}
	}
}

static void print_current_location(int show_asm_if_no_src)
{
	unsigned long pc = g_regs.eip;
	unsigned long off = 0;
	struct six_dbg_sym *sym = lookup_sym_by_addr(pc, &off);
	int fidx = -1, lno = -1;

	if (lookup_line_by_addr(pc, &fidx, &lno) && fidx >= 0 && fidx < g_num_files) {
		char sline[256];
		g_list_file_idx = fidx;
		g_list_line = lno;
		if (read_source_line(g_files[fidx], lno, sline, sizeof(sline)) >= 0) {
			printf("%d\t%s\n", lno, sline);
			return;
		}
	}

	if (show_asm_if_no_src) {
		unsigned char code[16];
		char insn[128];
		memset(code, 0x90, sizeof(code));
		unplant_all_bps();
		tracee_read_mem(pc, code, sizeof(code));
		plant_all_bps();
		disasm_one(pc, code, insn, sizeof(insn), NULL);
		if (sym)
			printf("=> 0x%08lx <%s+0x%lx>:\t%s\n", pc, sym->name, off, insn);
		else
			printf("=> 0x%08lx:\t%s\n", pc, insn);
	}
}

/*
 * Wait for child to stop or exit, adjusting EIP if it hit a planted `int3` (0xcc).
 * Returns: 1 if stopped at a breakpoint/trap, 0 if exited/terminated.
 */
static int wait_for_tracee(int print_stop_banner)
{
	unsigned int status = 0;
	int r = waitpid(g_child_pid, &status, WUNTRACED);
	if (r < 0) {
		printf("waitpid failed: %s\n", strerror(errno));
		g_child_pid = -1;
		return 0;
	}

	if (WIFEXITED(status)) {
		if (print_stop_banner)
			printf("[Inferior 1 (process %d) exited with code %d]\n",
			       g_child_pid, WEXITSTATUS(status));
		g_child_pid = -1;
		return 0;
	}
	if (WIFSIGNALED(status)) {
		if (print_stop_banner)
			printf("[Inferior 1 (process %d) terminated with signal %d]\n",
			       g_child_pid, WTERMSIG(status));
		g_child_pid = -1;
		return 0;
	}
	if (WIFSTOPPED(status)) {
		struct bp_entry *bp;
		g_last_sig = WSTOPSIG(status);
		tracee_get_regs();

		/* Check if tracee hit one of our planted 0xcc breakpoints at eip - 1 */
		bp = find_bp_at(g_regs.eip - 1);
		if (bp && bp->planted && g_last_sig == SIGTRAP) {
			g_regs.eip -= 1;
			tracee_set_regs();
			if (bp->temporary) {
				clear_temp_bps();
			} else if (print_stop_banner) {
				unsigned long off = 0;
				struct six_dbg_sym *sym = lookup_sym_by_addr(g_regs.eip, &off);
				int fidx = -1, lno = -1;
				printf("\nBreakpoint %d, ", bp->id);
				if (sym && off == 0)
					printf("%s ()", sym->name);
				else if (sym)
					printf("0x%08lx in %s ()", g_regs.eip, sym->name);
				else
					printf("0x%08lx", g_regs.eip);
				if (lookup_line_by_addr(g_regs.eip, &fidx, &lno) &&
				    fidx >= 0 && fidx < g_num_files) {
					printf(" at %s:%d\n", g_files[fidx], lno);
				} else {
					printf("\n");
				}
				print_current_location(1);
				return 1;
			}
		}

		clear_temp_bps();
		if (print_stop_banner) {
			if (g_last_sig != SIGTRAP && g_last_sig != SIGSTOP) {
				printf("\nProgram received signal %d at 0x%08lx.\n",
				       g_last_sig, g_regs.eip);
			}
			print_current_location(1);
		}
		return 1;
	}
	return 0;
}

/*
 * Step one instruction (handling stepping off a planted breakpoint cleanly).
 */
static int step_one_instruction(int print_loc)
{
	int r;
	if (g_child_pid <= 0) {
		printf("The program is not being run.\n");
		return 0;
	}
	unplant_all_bps();
	if (ptrace(PTRACE_SINGLESTEP, g_child_pid, 1, 0) < 0) {
		printf("ptrace(PTRACE_SINGLESTEP) failed: %s\n", strerror(errno));
		return 0;
	}
	r = wait_for_tracee(0);
	if (r) {
		plant_all_bps();
		if (print_loc)
			print_current_location(1);
	}
	return r;
}

static int continue_execution(int print_banner)
{
	struct bp_entry *bp;
	if (g_child_pid <= 0) {
		printf("The program is not being run.\n");
		return 0;
	}
	tracee_get_regs();
	bp = find_bp_at(g_regs.eip);
	if (bp) {
		/* Step over the breakpoint at current EIP first */
		if (!step_one_instruction(0))
			return 0;
	}
	plant_all_bps();
	if (ptrace(PTRACE_CONT, g_child_pid, 1, 0) < 0) {
		printf("ptrace(PTRACE_CONT) failed: %s\n", strerror(errno));
		return 0;
	}
	return wait_for_tracee(print_banner);
}

/*
 * Source-level step (`step` when step_over_calls==0, `next` when step_over_calls==1)
 */
static void do_source_step(int step_over_calls)
{
	int start_fidx = -1, start_lno = -1;
	unsigned long start_off = 0;
	struct six_dbg_sym *start_sym;
	int steps = 0;

	if (g_child_pid <= 0) {
		printf("The program is not being run.\n");
		return;
	}

	tracee_get_regs();
	start_sym = lookup_sym_by_addr(g_regs.eip, &start_off);
	if (!lookup_line_by_addr(g_regs.eip, &start_fidx, &start_lno)) {
		/* No line info at current PC: fall back to instruction step */
		if (step_over_calls) {
			unsigned char code[16];
			char insn[128];
			int is_call = 0, len;
			unplant_all_bps();
			tracee_read_mem(g_regs.eip, code, sizeof(code));
			len = disasm_one(g_regs.eip, code, insn, sizeof(insn), &is_call);
			plant_all_bps();
			if (is_call) {
				add_breakpoint(g_regs.eip + (unsigned long)len, "<nexti>", 1);
				continue_execution(1);
				return;
			}
		}
		step_one_instruction(1);
		return;
	}

	while (steps < 512 && g_child_pid > 0) {
		unsigned char code[16];
		char insn[128];
		int is_call = 0, len;
		int cur_fidx = -1, cur_lno = -1;
		unsigned long cur_off = 0;
		struct six_dbg_sym *cur_sym;

		unplant_all_bps();
		memset(code, 0x90, sizeof(code));
		tracee_read_mem(g_regs.eip, code, sizeof(code));
		len = disasm_one(g_regs.eip, code, insn, sizeof(insn), &is_call);
		plant_all_bps();

		if (is_call && step_over_calls) {
			add_breakpoint(g_regs.eip + (unsigned long)len, "<next>", 1);
			if (!continue_execution(0))
				return;
		} else {
			if (!step_one_instruction(0))
				return;
		}
		steps++;

		/* Stop immediately if we hit a user breakpoint */
		if (find_bp_at(g_regs.eip)) {
			struct bp_entry *bp = find_bp_at(g_regs.eip);
			printf("\nBreakpoint %d, 0x%08lx\n", bp->id, g_regs.eip);
			print_current_location(1);
			return;
		}

		cur_sym = lookup_sym_by_addr(g_regs.eip, &cur_off);
		if (lookup_line_by_addr(g_regs.eip, &cur_fidx, &cur_lno)) {
			if (cur_fidx != start_fidx || cur_lno != start_lno) {
				if (cur_sym && cur_sym != start_sym) {
					printf("%s () at %s:%d\n",
					       cur_sym->name, g_files[cur_fidx], cur_lno);
				}
				print_current_location(1);
				return;
			}
		} else if (!step_over_calls && is_call) {
			/* Stepped into a library call (e.g. printf) that has no line info:
			 * automatically step out back to caller so `step` stays in source! */
			unsigned long ret_addr = 0;
			if (tracee_read_mem(g_regs.esp, &ret_addr, 4) == 0 &&
			    ret_addr >= 0x03000000UL) {
				add_breakpoint(ret_addr, "<step-out>", 1);
				if (!continue_execution(0))
					return;
				if (lookup_line_by_addr(g_regs.eip, &cur_fidx, &cur_lno) &&
				    (cur_fidx != start_fidx || cur_lno != start_lno)) {
					print_current_location(1);
					return;
				}
			}
		}
	}
	print_current_location(1);
}

/* -------------------------------------------------------------------------
 * Expression & Address Parser
 * ------------------------------------------------------------------------- */
static int eval_expr(const char *s, unsigned long *val_out)
{
	struct six_dbg_sym *sym;
	while (*s == ' ' || *s == '\t')
		s++;
	if (!*s)
		return -1;

	if (*s == '*') {
		unsigned long ptr = 0, deref = 0;
		if (eval_expr(s + 1, &ptr) < 0)
			return -1;
		if (tracee_read_mem(ptr, &deref, 4) < 0)
			return -1;
		*val_out = deref;
		return 0;
	}
	if (*s == '&') {
		sym = lookup_sym_by_name(s + 1);
		if (sym) {
			*val_out = sym->addr;
			return 0;
		}
		return -1;
	}
	if (*s == '$') {
		const char *r = s + 1;
		tracee_get_regs();
		if (strcmp(r, "eax") == 0)      *val_out = g_regs.eax;
		else if (strcmp(r, "ebx") == 0) *val_out = g_regs.ebx;
		else if (strcmp(r, "ecx") == 0) *val_out = g_regs.ecx;
		else if (strcmp(r, "edx") == 0) *val_out = g_regs.edx;
		else if (strcmp(r, "esi") == 0) *val_out = g_regs.esi;
		else if (strcmp(r, "edi") == 0) *val_out = g_regs.edi;
		else if (strcmp(r, "ebp") == 0 || strcmp(r, "fp") == 0) *val_out = g_regs.ebp;
		else if (strcmp(r, "esp") == 0 || strcmp(r, "sp") == 0) *val_out = g_regs.esp;
		else if (strcmp(r, "eip") == 0 || strcmp(r, "pc") == 0) *val_out = g_regs.eip;
		else if (strcmp(r, "eflags") == 0) *val_out = g_regs.eflags;
		else return -1;
		return 0;
	}

	sym = lookup_sym_by_name(s);
	if (sym) {
		*val_out = sym->addr;
		return 0;
	}

	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		*val_out = strtoul(s + 2, NULL, 16);
		return 0;
	}
	if ((s[0] >= '0' && s[0] <= '9') || s[0] == '-') {
		*val_out = (unsigned long)strtol(s, NULL, 0);
		return 0;
	}
	return -1;
}

/* -------------------------------------------------------------------------
 * Interactive Command Implementations
 * ------------------------------------------------------------------------- */
static void cmd_run(const char *arg_str)
{
	char *argv_exec[MAX_ARGS + 2];
	int i, pid;

	if (!g_exe_path[0]) {
		printf("No executable file specified.\n");
		return;
	}

	if (g_child_pid > 0) {
		ptrace(PTRACE_KILL, g_child_pid, 0, 0);
		waitpid(g_child_pid, NULL, 0);
		g_child_pid = -1;
	}

	for (i = 0; i < MAX_BPS; i++)
		g_bps[i].planted = 0;

	if (arg_str && *arg_str) {
		char buf[256];
		char *p;
		g_argc = 0;
		strncpy(buf, arg_str, sizeof(buf) - 1);
		buf[sizeof(buf) - 1] = '\0';
		p = strtok(buf, " \t");
		while (p && g_argc < MAX_ARGS) {
			strncpy(g_arg_storage[g_argc++], p, 127);
			p = strtok(NULL, " \t");
		}
	}

	printf("Starting program: %s", g_exe_path);
	for (i = 0; i < g_argc; i++)
		printf(" %s", g_arg_storage[i]);
	printf("\n");

	pid = fork();
	if (pid < 0) {
		printf("fork failed: %s\n", strerror(errno));
		return;
	}
	if (pid == 0) {
		argv_exec[0] = g_exe_path;
		for (i = 0; i < g_argc; i++)
			argv_exec[i + 1] = g_arg_storage[i];
		argv_exec[g_argc + 1] = NULL;
		ptrace(PTRACE_TRACEME, 0, 0, 0);
		execv(g_exe_path, argv_exec);
		printf("Failed to exec %s: %s\n", g_exe_path, strerror(errno));
		_exit(127);
	}

	g_child_pid = pid;
	g_attached = 0;

	/* Catch initial SIGTRAP on execve */
	if (!wait_for_tracee(0))
		return;

	plant_all_bps();
	if (ptrace(PTRACE_CONT, g_child_pid, 1, 0) < 0) {
		printf("ptrace(PTRACE_CONT) failed: %s\n", strerror(errno));
		return;
	}
	wait_for_tracee(1);
}

static void cmd_attach(int pid)
{
	struct six_ptrace_proc_info pinfo;

	if (pid <= 1) {
		printf("Illegal process-id: %d.\n", pid);
		return;
	}
	if (g_child_pid > 0) {
		printf("Already debugging process %d.\n", g_child_pid);
		return;
	}
	printf("Attaching to process %d\n", pid);
	if (ptrace(PTRACE_SIX_GET_PROC, pid, 0, (long)&pinfo) == 0 && pinfo.exe_path[0]) {
		strncpy(g_exe_path, pinfo.exe_path, sizeof(g_exe_path) - 1);
		load_debug_info(g_exe_path, 0);
	}
	if (ptrace(PTRACE_ATTACH, pid, 0, 0) < 0) {
		printf("ptrace(PTRACE_ATTACH) failed: %s\n", strerror(errno));
		return;
	}
	g_child_pid = pid;
	g_attached = 1;
	if (wait_for_tracee(0)) {
		unsigned long off = 0;
		struct six_dbg_sym *sym = lookup_sym_by_addr(g_regs.eip, &off);
		plant_all_bps();
		if (sym)
			printf("0x%08lx in %s ()\n", g_regs.eip, sym->name);
		else
			printf("0x%08lx in ?? ()\n", g_regs.eip);
		print_current_location(1);
	}
}

static void cmd_break(const char *arg)
{
	unsigned long addr = 0;
	int fidx = -1, lno = -1;
	char desc[64];
	int id;

	while (*arg == ' ' || *arg == '\t')
		arg++;
	if (!*arg) {
		if (g_child_pid > 0) {
			tracee_get_regs();
			addr = g_regs.eip;
			snprintf(desc, sizeof(desc), "*0x%08lx", addr);
		} else {
			printf("Usage: break <function | file:line | line | *addr>\n");
			return;
		}
	} else if (*arg == '*') {
		if (eval_expr(arg + 1, &addr) < 0) {
			printf("Invalid breakpoint address: %s\n", arg);
			return;
		}
		snprintf(desc, sizeof(desc), "%s", arg);
	} else if (strchr(arg, ':') != NULL) {
		char fpart[64];
		const char *colon = strchr(arg, ':');
		int target_line = atoi(colon + 1);
		size_t flen = (size_t)(colon - arg);
		if (flen >= sizeof(fpart))
			flen = sizeof(fpart) - 1;
		memcpy(fpart, arg, flen);
		fpart[flen] = '\0';
		addr = lookup_addr_by_line(fpart, target_line, &fidx, &lno);
		if (!addr) {
			printf("No line %d in file \"%s\".\n", target_line, fpart);
			return;
		}
		snprintf(desc, sizeof(desc), "%s:%d", fpart, lno);
	} else if (arg[0] >= '0' && arg[0] <= '9') {
		int target_line = atoi(arg);
		addr = lookup_addr_by_line(NULL, target_line, &fidx, &lno);
		if (!addr) {
			printf("No compiled code at line %d.\n", target_line);
			return;
		}
		snprintf(desc, sizeof(desc), "%s:%d",
			 (fidx >= 0 && fidx < g_num_files) ? g_files[fidx] : "?", lno);
	} else {
		struct six_dbg_sym *sym = lookup_sym_by_name(arg);
		if (!sym) {
			printf("Function \"%s\" not defined.\n", arg);
			return;
		}
		addr = find_func_entry_addr(sym, &fidx, &lno);
		snprintf(desc, sizeof(desc), "%s", sym->name);
	}

	id = add_breakpoint(addr, desc, 0);
	if (id < 0) {
		printf("Breakpoint table full.\n");
		return;
	}
	if (fidx >= 0 && fidx < g_num_files && lno > 0)
		printf("Breakpoint %d at 0x%08lx: file %s, line %d.\n",
		       id, addr, g_files[fidx], lno);
	else
		printf("Breakpoint %d at 0x%08lx (%s)\n", id, addr, desc);
}

static void cmd_list(const char *arg)
{
	int start_line, end_line, i;
	int cur_fidx = -1, cur_lno = -1;

	while (*arg == ' ' || *arg == '\t')
		arg++;

	if (*arg) {
		if (strchr(arg, ':') != NULL) {
			char fpart[64];
			const char *colon = strchr(arg, ':');
			size_t flen = (size_t)(colon - arg);
			if (flen >= sizeof(fpart))
				flen = sizeof(fpart) - 1;
			memcpy(fpart, arg, flen);
			fpart[flen] = '\0';
			for (i = 0; i < g_num_files; i++) {
				const char *b = strrchr(g_files[i], '/');
				b = b ? (b + 1) : g_files[i];
				if (strcmp(g_files[i], fpart) == 0 || strcmp(b, fpart) == 0) {
					g_list_file_idx = i;
					break;
				}
			}
			g_list_line = atoi(colon + 1) - 4;
		} else if (arg[0] >= '0' && arg[0] <= '9') {
			g_list_line = atoi(arg) - 4;
		} else {
			struct six_dbg_sym *sym = lookup_sym_by_name(arg);
			if (sym) {
				int fidx = -1, lno = -1;
				find_func_entry_addr(sym, &fidx, &lno);
				if (fidx >= 0 && lno > 0) {
					g_list_file_idx = fidx;
					g_list_line = lno - 2;
				}
			}
		}
	}

	if (g_list_line < 1)
		g_list_line = 1;
	start_line = g_list_line;
	end_line   = start_line + 9;

	if (g_child_pid > 0) {
		tracee_get_regs();
		lookup_line_by_addr(g_regs.eip, &cur_fidx, &cur_lno);
	}

	if (g_num_files == 0) {
		printf("No source file information loaded.\n");
		return;
	}

	for (i = start_line; i <= end_line; i++) {
		char sline[256];
		if (read_source_line(g_files[g_list_file_idx], i, sline, sizeof(sline)) < 0) {
			if (i == start_line)
				printf("Line number %d out of range; %s has fewer lines.\n",
				       start_line, g_files[g_list_file_idx]);
			break;
		}
		if (g_list_file_idx == cur_fidx && i == cur_lno)
			printf("%4d =>\t%s\n", i, sline);
		else
			printf("%4d\t%s\n", i, sline);
		g_list_line = i + 1;
	}
}

static void cmd_backtrace(void)
{
	unsigned long pc, ebp;
	int frame = 0;

	if (g_child_pid <= 0) {
		printf("No stack.\n");
		return;
	}
	tracee_get_regs();
	pc  = g_regs.eip;
	ebp = g_regs.ebp;

	while (frame < 16 && pc >= 0x03000000UL && pc < 0x20000000UL) {
		unsigned long off = 0;
		struct six_dbg_sym *sym = lookup_sym_by_addr(pc, &off);
		int fidx = -1, lno = -1;
		unsigned long a0 = 0, a1 = 0;
		int has_args = 0;

		if (ebp >= 0x03000000UL && ebp <= 0x1ffffff0UL) {
			if (tracee_read_mem(ebp + 8, &a0, 4) == 0 &&
			    tracee_read_mem(ebp + 12, &a1, 4) == 0)
				has_args = 1;
		}

		printf("#%-2d 0x%08lx in %s (",
		       frame, pc, sym ? sym->name : "??");
		if (has_args)
			printf("0x%lx, 0x%lx", a0, a1);
		printf(")");
		if (lookup_line_by_addr(pc, &fidx, &lno) &&
		    fidx >= 0 && fidx < g_num_files) {
			printf(" at %s:%d", g_files[fidx], lno);
		}
		printf("\n");

		if (sym && (strcmp(sym->name, "main") == 0 ||
			    strcmp(sym->name, "cstart") == 0))
			break;
		if (ebp < 0x03000000UL || ebp > 0x1ffffff8UL || (ebp & 3))
			break;
		{
			unsigned long next_ebp = 0, ret_pc = 0;
			if (tracee_read_mem(ebp, &next_ebp, 4) < 0 ||
			    tracee_read_mem(ebp + 4, &ret_pc, 4) < 0)
				break;
			if (ret_pc < 0x03000000UL || (next_ebp != 0 && next_ebp <= ebp))
				break;
			pc  = ret_pc;
			ebp = next_ebp;
		}
		frame++;
	}
}

static void cmd_info_regs(void)
{
	unsigned long ef;
	if (g_child_pid <= 0) {
		printf("The program has no registers now.\n");
		return;
	}
	tracee_get_regs();
	ef = g_regs.eflags;
	printf("eax            0x%08lx\t%ld\n", g_regs.eax, (long)g_regs.eax);
	printf("ecx            0x%08lx\t%ld\n", g_regs.ecx, (long)g_regs.ecx);
	printf("edx            0x%08lx\t%ld\n", g_regs.edx, (long)g_regs.edx);
	printf("ebx            0x%08lx\t%ld\n", g_regs.ebx, (long)g_regs.ebx);
	printf("esp            0x%08lx\t0x%08lx\n", g_regs.esp, g_regs.esp);
	printf("ebp            0x%08lx\t0x%08lx\n", g_regs.ebp, g_regs.ebp);
	printf("esi            0x%08lx\t%ld\n", g_regs.esi, (long)g_regs.esi);
	printf("edi            0x%08lx\t%ld\n", g_regs.edi, (long)g_regs.edi);
	printf("eip            0x%08lx\t0x%08lx", g_regs.eip, g_regs.eip);
	{
		unsigned long off = 0;
		struct six_dbg_sym *sym = lookup_sym_by_addr(g_regs.eip, &off);
		if (sym)
			printf(" <%s+%lu>", sym->name, off);
		printf("\n");
	}
	printf("eflags         0x%08lx\t[ %s%s%s%s%s%s]\n", ef,
	       (ef & 0x001) ? "CF " : "",
	       (ef & 0x040) ? "ZF " : "",
	       (ef & 0x080) ? "SF " : "",
	       (ef & 0x100) ? "TF " : "",
	       (ef & 0x200) ? "IF " : "",
	       (ef & 0x800) ? "OF " : "");
	printf("cs             0x%04lx\t\tss             0x%04lx\n",
	       g_regs.cs & 0xffff, g_regs.ss & 0xffff);
	printf("ds             0x%04lx\t\tes             0x%04lx\n",
	       g_regs.ds & 0xffff, g_regs.es & 0xffff);
}

static void cmd_disassemble(const char *arg)
{
	unsigned long start = 0, end = 0, addr;
	struct six_dbg_sym *sym = NULL;

	while (*arg == ' ' || *arg == '\t')
		arg++;

	if (g_child_pid > 0)
		tracee_get_regs();

	if (!*arg) {
		unsigned long off = 0;
		if (g_child_pid <= 0) {
			printf("No frame selected.\n");
			return;
		}
		sym = lookup_sym_by_addr(g_regs.eip, &off);
		if (sym) {
			start = sym->addr;
			end   = sym->addr + (sym->size ? sym->size : 64);
		} else {
			start = g_regs.eip;
			end   = start + 32;
		}
	} else {
		sym = lookup_sym_by_name(arg);
		if (sym) {
			start = sym->addr;
			end   = sym->addr + (sym->size ? sym->size : 64);
		} else if (eval_expr(arg, &start) == 0) {
			end = start + 32;
		} else {
			printf("Invalid disassemble target: %s\n", arg);
			return;
		}
	}

	if (end - start > 256)
		end = start + 256;

	if (sym)
		printf("Dump of assembler code for function %s:\n", sym->name);
	else
		printf("Dump of assembler code from 0x%08lx to 0x%08lx:\n", start, end);

	unplant_all_bps();
	addr = start;
	while (addr < end) {
		unsigned char code[16];
		char insn[128];
		int len;
		unsigned long off = 0;
		struct six_dbg_sym *s;

		memset(code, 0x90, sizeof(code));
		if (g_child_pid > 0) {
			if (tracee_read_mem(addr, code, sizeof(code)) < 0)
				break;
		} else {
			printf("   (Run the program first to inspect mapped memory)\n");
			break;
		}
		len = disasm_one(addr, code, insn, sizeof(insn), NULL);
		if (len <= 0)
			len = 1;
		s = lookup_sym_by_addr(addr, &off);
		printf("%s 0x%08lx", (g_child_pid > 0 && addr == g_regs.eip) ? "=>" : "  ", addr);
		if (s)
			printf(" <+%lu>:\t%s\n", off, insn);
		else
			printf(":\t%s\n", insn);
		addr += (unsigned long)len;
	}
	plant_all_bps();
	printf("End of assembler dump.\n");
}

static void cmd_examine(const char *spec)
{
	const char *p = spec;
	int count = g_last_x_count;
	char fmt  = g_last_x_fmt;
	char sz   = g_last_x_size;
	unsigned long addr = g_last_x_addr;
	int i;

	if (*p == '/') {
		p++;
		if (*p >= '0' && *p <= '9') {
			count = 0;
			while (*p >= '0' && *p <= '9')
				count = count * 10 + (*p++ - '0');
		}
		while (*p && *p != ' ' && *p != '\t') {
			if (*p == 'i' || *p == 'x' || *p == 'd' ||
			    *p == 'u' || *p == 's' || *p == 'c')
				fmt = *p;
			else if (*p == 'b' || *p == 'h' || *p == 'w')
				sz = *p;
			p++;
		}
	}
	while (*p == ' ' || *p == '\t')
		p++;
	if (*p) {
		if (eval_expr(p, &addr) < 0) {
			printf("Invalid address expression: %s\n", p);
			return;
		}
	} else if (g_child_pid > 0 && (fmt == 'i' || addr == 0x03000000UL)) {
		tracee_get_regs();
		addr = g_regs.eip;
	}

	g_last_x_count = count;
	g_last_x_fmt   = fmt;
	g_last_x_size  = sz;

	if (g_child_pid <= 0) {
		printf("The program is not being run.\n");
		return;
	}

	unplant_all_bps();
	if (fmt == 'i') {
		for (i = 0; i < count; i++) {
			unsigned char code[16];
			char insn[128];
			int len;
			unsigned long off = 0;
			struct six_dbg_sym *sym;
			if (tracee_read_mem(addr, code, sizeof(code)) < 0) {
				printf("Cannot access memory at address 0x%08lx\n", addr);
				break;
			}
			len = disasm_one(addr, code, insn, sizeof(insn), NULL);
			if (len <= 0)
				len = 1;
			sym = lookup_sym_by_addr(addr, &off);
			printf("%s 0x%08lx", (addr == g_regs.eip) ? "=>" : "  ", addr);
			if (sym)
				printf(" <%s+%lu>:\t%s\n", sym->name, off, insn);
			else
				printf(":\t%s\n", insn);
			addr += (unsigned long)len;
		}
	} else if (fmt == 's') {
		for (i = 0; i < count; i++) {
			char sbuf[128];
			int k;
			memset(sbuf, 0, sizeof(sbuf));
			for (k = 0; k < 120; k++) {
				if (tracee_read_mem(addr + k, &sbuf[k], 1) < 0 || sbuf[k] == '\0')
					break;
			}
			sbuf[k] = '\0';
			printf("0x%08lx:\t\"%s\"\n", addr, sbuf);
			addr += (unsigned long)(k + 1);
		}
	} else {
		int unit = (sz == 'b') ? 1 : (sz == 'h') ? 2 : 4;
		int per_row = (unit == 1) ? 8 : 4;
		for (i = 0; i < count; i++) {
			unsigned long val = 0;
			if ((i % per_row) == 0) {
				if (i > 0)
					printf("\n");
				printf("0x%08lx:\t", addr);
			}
			if (tracee_read_mem(addr, &val, unit) < 0) {
				printf("<cannot access 0x%08lx>", addr);
				break;
			}
			if (fmt == 'd')
				printf("%-10ld ", (unit == 1) ? (long)(signed char)val :
						  (unit == 2) ? (long)(short)val : (long)val);
			else if (fmt == 'u')
				printf("%-10lu ", val);
			else if (fmt == 'c')
				printf("%3lu '%c'  ", val & 0xff,
				       (val >= 32 && val < 127) ? (char)val : '.');
			else if (unit == 1)
				printf("0x%02lx ", val & 0xff);
			else if (unit == 2)
				printf("0x%04lx ", val & 0xffff);
			else
				printf("0x%08lx ", val);
			addr += (unsigned long)unit;
		}
		printf("\n");
	}
	plant_all_bps();
	g_last_x_addr = addr;
}

static void execute_command(char *line)
{
	char *cmd, *arg;
	int len = strlen(line);

	while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
		line[--len] = '\0';

	cmd = line;
	while (*cmd == ' ' || *cmd == '\t')
		cmd++;

	if (*cmd == '\0') {
		if (g_last_cmd[0]) {
			char rep[128];
			strcpy(rep, g_last_cmd);
			execute_command(rep);
		}
		return;
	}

	/* Record repeatable commands */
	strncpy(g_last_cmd, cmd, sizeof(g_last_cmd) - 1);

	/* Special case `x/...` without space */
	if (cmd[0] == 'x' && (cmd[1] == '/' || cmd[1] == ' ' || cmd[1] == '\0')) {
		cmd_examine(cmd + 1);
		return;
	}

	arg = cmd;
	while (*arg && *arg != ' ' && *arg != '\t')
		arg++;
	if (*arg) {
		*arg++ = '\0';
		while (*arg == ' ' || *arg == '\t')
			arg++;
	}

	if (strcmp(cmd, "q") == 0 || strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
		if (g_child_pid > 0) {
			if (g_attached) {
				unplant_all_bps();
				ptrace(PTRACE_DETACH, g_child_pid, 0, 0);
			} else {
				ptrace(PTRACE_KILL, g_child_pid, 0, 0);
				waitpid(g_child_pid, NULL, 0);
			}
		}
		exit(0);
	} else if (strcmp(cmd, "r") == 0 || strcmp(cmd, "run") == 0) {
		cmd_run(arg);
	} else if (strcmp(cmd, "start") == 0) {
		struct six_dbg_sym *msym = lookup_sym_by_name("main");
		if (msym) {
			unsigned long maddr = find_func_entry_addr(msym, NULL, NULL);
			add_breakpoint(maddr, "main", 1);
		}
		cmd_run(arg);
	} else if (strcmp(cmd, "attach") == 0) {
		cmd_attach(atoi(arg));
	} else if (strcmp(cmd, "detach") == 0) {
		if (g_child_pid <= 0) {
			printf("The program is not being run.\n");
		} else {
			unplant_all_bps();
			ptrace(PTRACE_DETACH, g_child_pid, 0, 0);
			printf("Detaching from program: %s, process %d\n", g_exe_path, g_child_pid);
			g_child_pid = -1;
		}
	} else if (strcmp(cmd, "k") == 0 || strcmp(cmd, "kill") == 0) {
		if (g_child_pid > 0) {
			ptrace(PTRACE_KILL, g_child_pid, 0, 0);
			waitpid(g_child_pid, NULL, 0);
			printf("[Inferior 1 (process %d) killed]\n", g_child_pid);
			g_child_pid = -1;
		}
	} else if (strcmp(cmd, "b") == 0 || strcmp(cmd, "break") == 0) {
		cmd_break(arg);
	} else if (strcmp(cmd, "d") == 0 || strcmp(cmd, "del") == 0 || strcmp(cmd, "delete") == 0) {
		int target = *arg ? atoi(arg) : -1;
		int i;
		unplant_all_bps();
		for (i = 0; i < MAX_BPS; i++) {
			if (g_bps[i].used && !g_bps[i].temporary &&
			    (target < 0 || g_bps[i].id == target)) {
				g_bps[i].used = 0;
			}
		}
		plant_all_bps();
	} else if (strcmp(cmd, "disable") == 0 || strcmp(cmd, "enable") == 0) {
		int target = atoi(arg);
		int en = (strcmp(cmd, "enable") == 0);
		int i;
		unplant_all_bps();
		for (i = 0; i < MAX_BPS; i++) {
			if (g_bps[i].used && g_bps[i].id == target)
				g_bps[i].enabled = en;
		}
		plant_all_bps();
	} else if (strcmp(cmd, "c") == 0 || strcmp(cmd, "cont") == 0 || strcmp(cmd, "continue") == 0) {
		printf("Continuing.\n");
		continue_execution(1);
	} else if (strcmp(cmd, "s") == 0 || strcmp(cmd, "step") == 0) {
		do_source_step(0);
	} else if (strcmp(cmd, "n") == 0 || strcmp(cmd, "next") == 0) {
		do_source_step(1);
	} else if (strcmp(cmd, "si") == 0 || strcmp(cmd, "stepi") == 0) {
		step_one_instruction(1);
	} else if (strcmp(cmd, "ni") == 0 || strcmp(cmd, "nexti") == 0) {
		if (g_child_pid <= 0) {
			printf("The program is not being run.\n");
		} else {
			unsigned char code[16];
			char insn[128];
			int is_call = 0, len;
			tracee_get_regs();
			unplant_all_bps();
			tracee_read_mem(g_regs.eip, code, sizeof(code));
			len = disasm_one(g_regs.eip, code, insn, sizeof(insn), &is_call);
			plant_all_bps();
			if (is_call) {
				add_breakpoint(g_regs.eip + (unsigned long)len, "<nexti>", 1);
				continue_execution(1);
			} else {
				step_one_instruction(1);
			}
		}
	} else if (strcmp(cmd, "fin") == 0 || strcmp(cmd, "finish") == 0) {
		if (g_child_pid <= 0) {
			printf("The program is not being run.\n");
		} else {
			unsigned long ret_addr = 0;
			tracee_get_regs();
			if ((g_regs.ebp >= 0x03000000UL &&
			     tracee_read_mem(g_regs.ebp + 4, &ret_addr, 4) == 0 &&
			     ret_addr >= 0x03000000UL) ||
			    (tracee_read_mem(g_regs.esp, &ret_addr, 4) == 0 &&
			     ret_addr >= 0x03000000UL)) {
				add_breakpoint(ret_addr, "<finish>", 1);
				if (continue_execution(1)) {
					printf("Value returned is $eax = 0x%lx (%ld)\n",
					       g_regs.eax, (long)g_regs.eax);
				}
			} else {
				printf("Cannot determine caller return address.\n");
			}
		}
	} else if (strcmp(cmd, "l") == 0 || strcmp(cmd, "list") == 0) {
		strcpy(g_last_cmd, "list");
		cmd_list(arg);
	} else if (strcmp(cmd, "bt") == 0 || strcmp(cmd, "where") == 0 || strcmp(cmd, "backtrace") == 0) {
		cmd_backtrace();
	} else if (strcmp(cmd, "regs") == 0) {
		cmd_info_regs();
	} else if (strcmp(cmd, "disas") == 0 || strcmp(cmd, "disassemble") == 0) {
		cmd_disassemble(arg);
	} else if (strcmp(cmd, "p") == 0 || strcmp(cmd, "print") == 0) {
		unsigned long val = 0;
		struct six_dbg_sym *sym = lookup_sym_by_name(arg);
		if (sym && sym->type != 'T' && sym->type != 't' && g_child_pid > 0) {
			unsigned long dval = 0;
			if (tracee_read_mem(sym->addr, &dval, 4) == 0) {
				printf("$1 = %ld (0x%lx) [at %s = 0x%08lx]\n",
				       (long)dval, dval, sym->name, sym->addr);
				return;
			}
		}
		if (eval_expr(arg, &val) == 0) {
			printf("$1 = 0x%lx (%ld)\n", val, (long)val);
		} else {
			printf("No symbol \"%s\" in current context.\n", arg);
		}
	} else if (strcmp(cmd, "set") == 0) {
		char *eq = strchr(arg, '=');
		if (!eq) {
			printf("Usage: set $<reg> = <value>  or  set *<addr> = <value>\n");
		} else {
			unsigned long rhs = 0;
			*eq++ = '\0';
			while (*arg == ' ' || *arg == '\t') arg++;
			{
				int al = strlen(arg);
				while (al > 0 && (arg[al - 1] == ' ' || arg[al - 1] == '\t'))
					arg[--al] = '\0';
			}
			if (eval_expr(eq, &rhs) < 0) {
				printf("Invalid value expression: %s\n", eq);
				return;
			}
			if (arg[0] == '$' && g_child_pid > 0) {
				const char *r = arg + 1;
				tracee_get_regs();
				if (strcmp(r, "eax") == 0) g_regs.eax = rhs;
				else if (strcmp(r, "ebx") == 0) g_regs.ebx = rhs;
				else if (strcmp(r, "ecx") == 0) g_regs.ecx = rhs;
				else if (strcmp(r, "edx") == 0) g_regs.edx = rhs;
				else if (strcmp(r, "esi") == 0) g_regs.esi = rhs;
				else if (strcmp(r, "edi") == 0) g_regs.edi = rhs;
				else if (strcmp(r, "ebp") == 0) g_regs.ebp = rhs;
				else if (strcmp(r, "esp") == 0) g_regs.esp = rhs;
				else if (strcmp(r, "eip") == 0 || strcmp(r, "pc") == 0) g_regs.eip = rhs;
				tracee_set_regs();
			} else if (arg[0] == '*' && g_child_pid > 0) {
				unsigned long dst = 0;
				if (eval_expr(arg + 1, &dst) == 0)
					tracee_write_mem(dst, &rhs, 4);
			}
		}
	} else if (strcmp(cmd, "i") == 0 || strcmp(cmd, "info") == 0) {
		if (strncmp(arg, "r", 1) == 0) {
			cmd_info_regs();
		} else if (strncmp(arg, "b", 1) == 0) {
			int i, any = 0;
			printf("Num     Type           Disp Enb Address    What\n");
			for (i = 0; i < MAX_BPS; i++) {
				if (g_bps[i].used && !g_bps[i].temporary) {
					printf("%-7d breakpoint     keep %-3s 0x%08lx %s\n",
					       g_bps[i].id, g_bps[i].enabled ? "y" : "n",
					       g_bps[i].addr, g_bps[i].desc);
					any = 1;
				}
			}
			if (!any)
				printf("No breakpoints or watchpoints.\n");
		} else if (strncmp(arg, "func", 4) == 0) {
			int i;
			printf("All defined functions:\n");
			for (i = 0; i < g_num_syms; i++) {
				if (g_syms[i].type == 'T' || g_syms[i].type == 't' ||
				    g_syms[i].type == 'W' || g_syms[i].type == 'w') {
					printf("0x%08lx  %s\n", g_syms[i].addr, g_syms[i].name);
				}
			}
		} else if (strncmp(arg, "var", 3) == 0) {
			int i;
			printf("All defined variables:\n");
			for (i = 0; i < g_num_syms; i++) {
				if (g_syms[i].type != 'T' && g_syms[i].type != 't')
					printf("0x%08lx  %s (%c)\n",
					       g_syms[i].addr, g_syms[i].name, g_syms[i].type);
			}
		} else if (strncmp(arg, "proc", 4) == 0) {
			struct six_ptrace_proc_info pinfo;
			if (g_child_pid > 0 &&
			    ptrace(PTRACE_SIX_GET_PROC, g_child_pid, 0, (long)&pinfo) == 0) {
				printf("process %d\n", pinfo.pid);
				printf("cmdline = '%s'\n", pinfo.comm);
				printf("exe     = '%s'\n", pinfo.exe_path);
				printf("Mapped address spaces:\n");
				printf("  Start Addr   End Addr     Section\n");
				printf("  0x%08lx   0x%08lx   .text / .rodata\n",
				       pinfo.start_code, pinfo.end_code);
				printf("  0x%08lx   0x%08lx   .data / .bss / heap\n",
				       pinfo.end_code, pinfo.brk);
				printf("  0x%08lx   0x20000000   [stack]\n",
				       pinfo.start_stack);
			} else {
				printf("No current process.\n");
			}
		} else if (strncmp(arg, "src", 3) == 0 || strncmp(arg, "source", 6) == 0) {
			int i;
			printf("Loaded source files (%d):\n", g_num_files);
			for (i = 0; i < g_num_files; i++)
				printf("  [%d] %s%s\n", i, g_files[i],
				       (i == g_list_file_idx) ? " (current)" : "");
		} else {
			printf("Available info subcommands: registers, break, functions, variables, proc, source\n");
		}
	} else if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
		printf("SIX Symbolic Debugger (gdb) commands:\n"
		       "  run [args] (r)         -- Start debugged program\n"
		       "  start [args]           -- Break at main() and start program\n"
		       "  attach <pid> / detach  -- Attach to or detach from a live guest PID\n"
		       "  break <loc> (b)        -- Set breakpoint (func, file:line, line, *addr)\n"
		       "  delete [num] (d)       -- Delete breakpoint(s)\n"
		       "  continue (c)           -- Continue program execution\n"
		       "  step (s) / next (n)    -- Step into / over C source line\n"
		       "  stepi (si) / nexti (ni)-- Step into / over one x86 machine instruction\n"
		       "  finish (fin)           -- Execute until current stack frame returns\n"
		       "  list [loc] (l)         -- List C source code lines\n"
		       "  backtrace (bt)         -- Print stack backtrace\n"
		       "  disassemble [fn] (disas)-- Disassemble machine instructions\n"
		       "  info regs / break / func / var / proc / source\n"
		       "  x/<N><fmt><sz> <addr>  -- Examine memory (e.g. x/5i $eip, x/4xw $esp, x/s)\n"
		       "  print <expr> (p)       -- Print register ($eax..$eip) or symbol value\n"
		       "  set $<reg> = <val>     -- Modify register or *addr in tracee\n"
		       "  quit (q)               -- Exit debugger\n");
	} else {
		printf("Undefined command: \"%s\".  Try \"help\".\n", cmd);
	}
}

/* -------------------------------------------------------------------------
 * GDB Remote Serial Protocol (RSP) Server Mode (`gdb --rsp ...`)
 * Allows external host `/usr/bin/gdb` to debug a SIX guest via `./sadb gdb`
 * ------------------------------------------------------------------------- */
static int hex_val(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return 0;
}

static void rsp_send_pkt(const char *payload)
{
	unsigned char csum = 0;
	const char *p = payload;
	char hdr = '$';
	char tail[4];
	static const char hex[] = "0123456789abcdef";

	while (*p)
		csum += (unsigned char)*p++;
	write(1, &hdr, 1);
	if (p > payload)
		write(1, payload, (size_t)(p - payload));
	tail[0] = '#';
	tail[1] = hex[(csum >> 4) & 0xf];
	tail[2] = hex[csum & 0xf];
	write(1, tail, 3);
}

static int rsp_recv_pkt(char *buf, int maxlen)
{
	char c;
	int len = 0;
	while (read(0, &c, 1) == 1) {
		if (c == '$') {
			len = 0;
			while (read(0, &c, 1) == 1) {
				if (c == '#') {
					char ck[2];
					read(0, &ck[0], 1);
					read(0, &ck[1], 1);
					write(1, "+", 1);
					buf[len] = '\0';
					return len;
				}
				if (len + 1 < maxlen)
					buf[len++] = c;
			}
		} else if (c == 0x03) {
			strcpy(buf, "vCtrlC");
			return 6;
		}
	}
	return -1;
}

static void encode_le32_hex(unsigned long v, char *out)
{
	static const char hex[] = "0123456789abcdef";
	int i;
	for (i = 0; i < 4; i++) {
		unsigned char b = (unsigned char)((v >> (i * 8)) & 0xff);
		out[i * 2]     = hex[(b >> 4) & 0xf];
		out[i * 2 + 1] = hex[b & 0xf];
	}
	out[8] = '\0';
}

static unsigned long decode_le32_hex(const char *in)
{
	unsigned long v = 0;
	int i;
	for (i = 0; i < 4; i++) {
		unsigned long b = (unsigned long)((hex_val(in[i * 2]) << 4) | hex_val(in[i * 2 + 1]));
		v |= (b << (i * 8));
	}
	return v;
}

static int run_rsp_server(int attach_pid)
{
	struct termios tio, saved_tio;
	int have_tio = 0;
	char pkt[2048];
	char reply[2048];

	if (tcgetattr(0, &saved_tio) == 0) {
		have_tio = 1;
		tio = saved_tio;
		tio.c_iflag &= ~(ICRNL | IXON | INLCR | IGNCR);
		tio.c_oflag &= ~(OPOST);
		tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
		tcsetattr(0, TCSANOW, &tio);
	}

	if (attach_pid > 0) {
		if (ptrace(PTRACE_ATTACH, attach_pid, 0, 0) < 0)
			return 1;
		g_child_pid = attach_pid;
		g_attached = 1;
		wait_for_tracee(0);
	} else {
		char *argv_exec[MAX_ARGS + 2];
		int i, pid = fork();
		if (pid == 0) {
			int null_fd = open("/dev/null", O_RDWR);
			if (null_fd >= 0) {
				dup2(null_fd, 0);
				dup2(null_fd, 1);
				dup2(null_fd, 2);
				if (null_fd > 2)
					close(null_fd);
			}
			argv_exec[0] = g_exe_path;
			for (i = 0; i < g_argc; i++)
				argv_exec[i + 1] = g_arg_storage[i];
			argv_exec[g_argc + 1] = NULL;
			ptrace(PTRACE_TRACEME, 0, 0, 0);
			execv(g_exe_path, argv_exec);
			_exit(127);
		}
		g_child_pid = pid;
		wait_for_tracee(0);
	}

	while (rsp_recv_pkt(pkt, sizeof(pkt)) >= 0) {
		if (strcmp(pkt, "?") == 0) {
			snprintf(reply, sizeof(reply), "S%02x", g_last_sig ? g_last_sig : 5);
			rsp_send_pkt(reply);
		} else if (strncmp(pkt, "qSupported", 10) == 0) {
			rsp_send_pkt("PacketSize=7ff;swbreak+");
		} else if (strncmp(pkt, "qAttached", 9) == 0) {
			rsp_send_pkt(g_attached ? "1" : "0");
		} else if (strcmp(pkt, "qC") == 0) {
			snprintf(reply, sizeof(reply), "QC%x", g_child_pid);
			rsp_send_pkt(reply);
		} else if (strcmp(pkt, "qfThreadInfo") == 0) {
			snprintf(reply, sizeof(reply), "m%x", g_child_pid);
			rsp_send_pkt(reply);
		} else if (strcmp(pkt, "qsThreadInfo") == 0) {
			rsp_send_pkt("l");
		} else if (pkt[0] == 'H') {
			rsp_send_pkt("OK");
		} else if (strncmp(pkt, "vCont?", 6) == 0) {
			rsp_send_pkt("vCont;c;C;s;S");
		} else if (pkt[0] == 'g') {
			/* i386 16 registers: eax, ecx, edx, ebx, esp, ebp, esi, edi, eip, eflags, cs, ss, ds, es, fs, gs */
			unsigned long r16[16];
			int i;
			tracee_get_regs();
			r16[0]  = g_regs.eax;    r16[1]  = g_regs.ecx;
			r16[2]  = g_regs.edx;    r16[3]  = g_regs.ebx;
			r16[4]  = g_regs.esp;    r16[5]  = g_regs.ebp;
			r16[6]  = g_regs.esi;    r16[7]  = g_regs.edi;
			r16[8]  = g_regs.eip;    r16[9]  = g_regs.eflags;
			r16[10] = g_regs.cs;     r16[11] = g_regs.ss;
			r16[12] = g_regs.ds;     r16[13] = g_regs.es;
			r16[14] = g_regs.fs;     r16[15] = g_regs.gs;
			for (i = 0; i < 16; i++)
				encode_le32_hex(r16[i], reply + i * 8);
			rsp_send_pkt(reply);
		} else if (pkt[0] == 'G') {
			unsigned long r16[16];
			int i;
			tracee_get_regs();
			for (i = 0; i < 16 && strlen(pkt + 1) >= (size_t)((i + 1) * 8); i++)
				r16[i] = decode_le32_hex(pkt + 1 + i * 8);
			g_regs.eax = r16[0]; g_regs.ecx = r16[1];
			g_regs.edx = r16[2]; g_regs.ebx = r16[3];
			g_regs.esp = r16[4]; g_regs.ebp = r16[5];
			g_regs.esi = r16[6]; g_regs.edi = r16[7];
			g_regs.eip = r16[8]; g_regs.eflags = r16[9];
			tracee_set_regs();
			rsp_send_pkt("OK");
		} else if (pkt[0] == 'p') {
			int rno = (int)strtoul(pkt + 1, NULL, 16);
			unsigned long r16[16];
			tracee_get_regs();
			r16[0]  = g_regs.eax;    r16[1]  = g_regs.ecx;
			r16[2]  = g_regs.edx;    r16[3]  = g_regs.ebx;
			r16[4]  = g_regs.esp;    r16[5]  = g_regs.ebp;
			r16[6]  = g_regs.esi;    r16[7]  = g_regs.edi;
			r16[8]  = g_regs.eip;    r16[9]  = g_regs.eflags;
			r16[10] = g_regs.cs;     r16[11] = g_regs.ss;
			r16[12] = g_regs.ds;     r16[13] = g_regs.es;
			r16[14] = g_regs.fs;     r16[15] = g_regs.gs;
			if (rno >= 0 && rno < 16)
				encode_le32_hex(r16[rno], reply);
			else
				encode_le32_hex(0, reply);
			rsp_send_pkt(reply);
		} else if (pkt[0] == 'P') {
			char *eq = strchr(pkt + 1, '=');
			if (eq) {
				int rno = (int)strtoul(pkt + 1, NULL, 16);
				unsigned long val = decode_le32_hex(eq + 1);
				tracee_get_regs();
				if (rno == 0) g_regs.eax = val;
				else if (rno == 1) g_regs.ecx = val;
				else if (rno == 2) g_regs.edx = val;
				else if (rno == 3) g_regs.ebx = val;
				else if (rno == 4) g_regs.esp = val;
				else if (rno == 5) g_regs.ebp = val;
				else if (rno == 6) g_regs.esi = val;
				else if (rno == 7) g_regs.edi = val;
				else if (rno == 8) g_regs.eip = val;
				else if (rno == 9) g_regs.eflags = val;
				tracee_set_regs();
			}
			rsp_send_pkt("OK");
		} else if (pkt[0] == 'm') {
			char *comma = strchr(pkt + 1, ',');
			if (!comma) {
				rsp_send_pkt("E01");
			} else {
				unsigned long addr = strtoul(pkt + 1, NULL, 16);
				int len = (int)strtoul(comma + 1, NULL, 16);
				unsigned char mbuf[512];
				static const char hex[] = "0123456789abcdef";
				int i;
				if (len > (int)sizeof(mbuf))
					len = sizeof(mbuf);
				unplant_all_bps();
				if (tracee_read_mem(addr, mbuf, len) < 0) {
					plant_all_bps();
					rsp_send_pkt("E14");
				} else {
					plant_all_bps();
					for (i = 0; i < len; i++) {
						reply[i * 2]     = hex[(mbuf[i] >> 4) & 0xf];
						reply[i * 2 + 1] = hex[mbuf[i] & 0xf];
					}
					reply[len * 2] = '\0';
					rsp_send_pkt(reply);
				}
			}
		} else if (pkt[0] == 'M') {
			char *comma = strchr(pkt + 1, ',');
			char *colon = comma ? strchr(comma + 1, ':') : NULL;
			if (!comma || !colon) {
				rsp_send_pkt("E01");
			} else {
				unsigned long addr = strtoul(pkt + 1, NULL, 16);
				int len = (int)strtoul(comma + 1, NULL, 16);
				unsigned char mbuf[512];
				int i;
				if (len > (int)sizeof(mbuf))
					len = sizeof(mbuf);
				for (i = 0; i < len; i++) {
					mbuf[i] = (unsigned char)((hex_val(colon[1 + i * 2]) << 4) |
								  hex_val(colon[1 + i * 2 + 1]));
				}
				unplant_all_bps();
				if (tracee_write_mem(addr, mbuf, len) < 0) {
					plant_all_bps();
					rsp_send_pkt("E14");
				} else {
					plant_all_bps();
					rsp_send_pkt("OK");
				}
			}
		} else if (strncmp(pkt, "Z0,", 3) == 0) {
			unsigned long addr = strtoul(pkt + 3, NULL, 16);
			add_breakpoint(addr, "rsp", 0);
			rsp_send_pkt("OK");
		} else if (strncmp(pkt, "z0,", 3) == 0) {
			unsigned long addr = strtoul(pkt + 3, NULL, 16);
			int i;
			unplant_all_bps();
			for (i = 0; i < MAX_BPS; i++) {
				if (g_bps[i].used && g_bps[i].addr == addr)
					g_bps[i].used = 0;
			}
			plant_all_bps();
			rsp_send_pkt("OK");
		} else if (pkt[0] == 's' || strncmp(pkt, "vCont;s", 7) == 0 ||
			   strncmp(pkt, "vCont;S", 7) == 0) {
			if (step_one_instruction(0)) {
				snprintf(reply, sizeof(reply), "S%02x", g_last_sig ? g_last_sig : 5);
				rsp_send_pkt(reply);
			} else {
				rsp_send_pkt("W00");
				break;
			}
		} else if (pkt[0] == 'c' || strncmp(pkt, "vCont;c", 7) == 0 ||
			   strncmp(pkt, "vCont;C", 7) == 0) {
			if (continue_execution(0)) {
				if ((g_last_sig == 5 || g_last_sig == 0) && find_bp_at(g_regs.eip))
					snprintf(reply, sizeof(reply), "T05swbreak:;");
				else
					snprintf(reply, sizeof(reply), "S%02x", g_last_sig ? g_last_sig : 5);
				rsp_send_pkt(reply);
			} else {
				rsp_send_pkt("W00");
				break;
			}
		} else if (pkt[0] == 'D') {
			unplant_all_bps();
			if (g_child_pid > 0)
				ptrace(PTRACE_DETACH, g_child_pid, 0, 0);
			g_child_pid = -1;
			rsp_send_pkt("OK");
			break;
		} else if (pkt[0] == 'k' || strncmp(pkt, "vKill", 5) == 0) {
			if (g_child_pid > 0) {
				ptrace(PTRACE_KILL, g_child_pid, 0, 0);
				waitpid(g_child_pid, NULL, 0);
			}
			g_child_pid = -1;
			rsp_send_pkt("OK");
			break;
		} else {
			rsp_send_pkt("");
		}
	}

	if (have_tio)
		tcsetattr(0, TCSANOW, &saved_tio);
	return 0;
}

/* -------------------------------------------------------------------------
 * Entry Point
 * ------------------------------------------------------------------------- */
int main(int argc, char **argv)
{
	int i;
	int quiet = 0, batch = 0, rsp_mode = 0;
	int attach_pid = -1;
	const char *ex_cmds[MAX_EX_CMDS];
	int num_ex = 0;
	char line[256];

	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0) {
			quiet = 1;
		} else if (strcmp(argv[i], "-batch") == 0 || strcmp(argv[i], "--batch") == 0) {
			batch = 1;
		} else if (strcmp(argv[i], "--rsp") == 0) {
			rsp_mode = 1;
			quiet = 1;
		} else if (strcmp(argv[i], "-ex") == 0 && i + 1 < argc) {
			if (num_ex < MAX_EX_CMDS)
				ex_cmds[num_ex++] = argv[++i];
		} else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			attach_pid = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--args") == 0) {
			i++;
			if (i < argc) {
				strncpy(g_exe_path, argv[i++], sizeof(g_exe_path) - 1);
				while (i < argc && g_argc < MAX_ARGS)
					strncpy(g_arg_storage[g_argc++], argv[i++], 127);
			}
			break;
		} else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
			printf("Usage: gdb [-q] [-batch] [-ex CMD]... [-p PID] [--args] [PROG [ARGS...]]\n"
			       "       gdb --rsp [-p PID | PROG [ARGS...]]\n");
			return 0;
		} else if (!g_exe_path[0]) {
			strncpy(g_exe_path, argv[i], sizeof(g_exe_path) - 1);
		} else if (g_argc < MAX_ARGS) {
			strncpy(g_arg_storage[g_argc++], argv[i], 127);
		}
	}

	if (g_exe_path[0] && access(g_exe_path, R_OK) != 0 && strchr(g_exe_path, '/') == NULL) {
		char try_bin[128];
		snprintf(try_bin, sizeof(try_bin), "/bin/%s", g_exe_path);
		if (access(try_bin, R_OK) == 0)
			strcpy(g_exe_path, try_bin);
	}

	if (rsp_mode)
		return run_rsp_server(attach_pid);

	if (!quiet) {
		printf("SIX Symbolic Debugger (gdb) 1.0 for i386-six-linux\n"
		       "Type \"help\" for a list of commands.\n");
	}

	if (g_exe_path[0])
		load_debug_info(g_exe_path, quiet);

	if (attach_pid > 0)
		cmd_attach(attach_pid);

	for (i = 0; i < num_ex; i++) {
		strncpy(line, ex_cmds[i], sizeof(line) - 1);
		line[sizeof(line) - 1] = '\0';
		if (!quiet)
			printf("(gdb) %s\n", line);
		execute_command(line);
	}

	if (batch) {
		if (g_child_pid > 0) {
			if (g_attached) {
				unplant_all_bps();
				ptrace(PTRACE_DETACH, g_child_pid, 0, 0);
			} else {
				ptrace(PTRACE_KILL, g_child_pid, 0, 0);
				waitpid(g_child_pid, NULL, 0);
			}
		}
		return 0;
	}

	while (1) {
		printf("(gdb) ");
		fflush(stdout);
		if (!fgets(line, sizeof(line), stdin)) {
			printf("quit\n");
			execute_command("quit");
			break;
		}
		execute_command(line);
	}
	return 0;
}
