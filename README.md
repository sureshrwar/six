# SIX (Solaris / Linux 2.0.11 User-Mode Kernel)

**SIX** (*SIX 1.0 Solaris UML*) is a 2003–2005 port of the **Linux 2.0.11** kernel and a **Minix** userland designed to run the entire operating system as an unprivileged user-space process. Originally developed on Solaris (SPARC and x86), it has been revived on `main` to build from source and run (`gcc -m32`) on modern 64-bit Linux (`x86_64`) systems.

```text
  #####    #*#   #     #
 #     #    #     #   #
 #          #      # #
  #####     #       #
       #    #      # #
 #     #    #     #   #
  #####    ###   #     #

Linux  Release 2.0.11 Version #5 Sat Sep 19 10:27:04 AM UTC 2026

[black] login: root

SIX 1.0

bash#
```

## How It Works

* **Emulated Physical RAM & Paging**: At boot, `./six` creates a 32 MB backing file (`mem_file`) and maps it into a low 32-bit address range. Guest page tables (`arch/six/mm/`) translate guest virtual memory operations (`fork`, `execve`, `brk`, `mmap`) into host `mmap`/`mprotect`/`munmap` calls against offsets in `mem_file`.
* **Context Switching & Preemption**: Each kernel task maintains user and kernel `ucontext_t` states (`getcontext`/`setcontext`/`swapcontext`) with dedicated 8 KB kernel stacks. A `10 Hz` interval timer (`SIGALRM`) drives `jiffies` and preemptive scheduling.
* **System Call Trap**: Guest ELF32 binaries (`library/libc` + `library/sys`) issue system calls via software trap (`int $0x90` on x86), which raises `SIGSEGV` in the host process. The signal handler (`sun_handler` in `arch/six/kernel/irq.c`) switches to the task's kernel stack, dispatches through `sys_call_table[]`, and restores the guest context on return.
* **Block & Console I/O**:
  * **Hard Disk (`/dev/hda`)**: The stock Linux 2.0.11 IDE driver (`drivers/block/hd.c`) is backed by a 5 MB rev-0 `ext2` filesystem image (`disk/x86/root`). IDE port reads/writes (`0x1f0–0x1f7`) are intercepted and translated to `lseek`/`read`/`write` on the disk image.
  * **Console (`/dev/console`)**: Host terminal input is delivered asynchronously via `O_ASYNC`/`SIGIO` (`drivers/char/keyboard.c`), and console framebuffer writes (`drivers/char/console.c`, `drivers/char/tga.c`) stream to the host terminal.

## Building and Running

### Prerequisites (Debian / Ubuntu / gLinux)
```bash
sudo apt-get install build-essential gcc-multilib e2fsprogs fakeroot
```

### Build Everything
A single `make` builds the `./six` kernel, the 32-bit guest `libc.a`, all 29 guest userland programs under `applications/`, and assembles the root `ext2` disk image (`disk/x86/root` via `port/image/mkimage.sh`):

```bash
make
```

### Boot SIX
```bash
./six
```
* Log in at `[black] login:` as **`root`** (no password).
* Included guest utilities in `/bin`: `advent` (*Colossal Cave Adventure*), `banner`, `cat`, `clear`, `date`, `echo`, `fortune`, `getty`, `gomoku` (Five-in-a-Row), `grep`, `halt`, `hello`, `id`, `init` (`/etc/init`), `kill`, `last`, `life` (Conway's Game of Life), `login`, `ls`, `ps`, `pwd`, `rm`, `sethostname`, `sh` (with `~/.bash_history` and Up/Down arrow recall), `sync`, `ttt` (Tic-Tac-Toe), `tty`, `vi` (`elvis`).
* Run **`halt`** at the shell prompt to flush buffers, mark the `ext2` superblock clean, restore the host terminal, and exit — or press **`Ctrl+]`** at any time for an immediate exit.

### Command-Line Options
```text
Usage: ./six [-w|--wait] [-s|--single] [-d|--disk <path>] [single]

Options:
  -w, --wait         Pause before boot and print host PID for gdb attach
  -s, --single       Boot into built-in single-user shell (go>)
  -d, --disk <path>  Root filesystem image (overrides $DISKFILE)
  -h, --help         Show this help message and exit
```

## Branches
* **`main`**: 2026 32-bit x86 port for modern Linux (`x86_64` host with `gcc -m32`).
* **`legacy_2005`** (tag **`v2005-cvs`**): Pristine 2003–2005 Solaris SPARC/x86 CVS tree as originally archived.
