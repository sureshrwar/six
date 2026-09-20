# SIX (Solaris / Linux 2.0.11 User-Mode Kernel)

**SIX** (*SIX 1.0 Solaris UML*) is a 2003–2005 port of the **Linux 2.0.11** kernel designed to run the entire operating system—along with its own self-contained guest C library (`library/libc`, `library/sys`) and a suite of classic Unix/Minix user-space utilities—as an unprivileged user-space process. Originally developed on Solaris (SPARC and x86), it has been revived on `main` to build from source and run (`gcc -m32`) on modern 64-bit Linux (`x86_64`) systems.

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

* **Emulated Physical RAM & Paging**: At startup, `./six` creates a 32 MB temporary file (`tmpfile()`) that acts as the guest machine's physical RAM. Data belonging to different guest processes simply lives in different 4 KB sections of this file. Because all guest programs live inside the single `./six` host process and every program expects to run at virtual address `0x03000000`, `set_proc_mappings()` calls `mmap(..., MAP_SHARED | MAP_FIXED)` on each context switch to point the `0x03000000` address range at the incoming process's pages inside the 32 MB file. When a process is scheduled out, its memory stays untouched in its part of the file until it is mapped back into view.
* **Context Switching & Preemption**: Each kernel task maintains user and kernel `ucontext_t` states (`getcontext`/`setcontext`/`swapcontext`) with dedicated 8 KB kernel stacks. A virtual interval timer (`ITIMER_VIRTUAL` / `SIGVTALRM`) drives `jiffies` and preemptive scheduling.
* **System Call Trap**: Because guest programs run as ordinary user-space code with no hardware privilege ring to trap into, a program enters the kernel by sending a signal (`kill(getpid(), SIX_TRAPSIG)`) to the `./six` process itself, passing a pointer to its system call arguments in the `%esi` register. The kernel's signal handler (`sun_handler` in `arch/six/kernel/irq.c`) catches the signal, switches to the task's kernel stack, runs the requested Linux 2.0.11 system call (`sys_call_table[]`), writes the return value back, and resumes the guest program.
* **Block & Console I/O**:
  * **Hard Disk (`/dev/hda`)**: The stock Linux 2.0.11 IDE driver (`drivers/block/hd.c`) is backed by a 50 MB rev-0 `ext2` filesystem image (`disk/x86/root`). IDE port reads/writes (`0x1f0–0x1f7`) are intercepted and translated to `lseek`/`read`/`write` on the disk image.
  * **Console (`/dev/console`)**: Host terminal input is delivered asynchronously via `O_ASYNC`/`SIGIO` (`drivers/char/keyboard.c`), and console writes (`drivers/char/console.c`) stream directly to the host terminal.

## Building and Running

### Prerequisites (Debian / Ubuntu / gLinux)
```bash
sudo apt-get install build-essential gcc-multilib e2fsprogs fakeroot
```

### Build Everything
A single `make` builds the `./six` kernel, the 32-bit guest `libc.a`, all 46 guest userland programs under `applications/`, and assembles the root `ext2` disk image (`disk/x86/root` via `port/image/mkimage.sh`):

```bash
make
```

### Boot SIX
```bash
./six
```
* Log in at `[black] login:` as **`root`** (no password).
* Included guest utilities in `/bin`: `advent` (*Colossal Cave Adventure*), `banner`, `basic` (interactive Dartmouth/Tiny BASIC interpreter), `cal`, `cat`, `clear`, `cowsay` (configurable ASCII cow and `cowthink`), `cp` (copy files), `date`, `df` (report filesystem disk space), `dhrystone` (Dhrystone 1.1 benchmark), `echo`, `eliza` (classic 1966 Rogerian psychotherapist chatbot), `fortune`, `getty`, `gomoku` (Five-in-a-Row), `grep`, `halt`, `head` (output first lines of files), `hello`, `id`, `init` (`/etc/init`), `kill`, `last`, `life` (Conway's Game of Life), `login`, `ls`, `matrix` (Matrix digital rain screensaver), `mkdir` (make directories), `mv` (move / rename files), `ps`, `pwd`, `rm`, `rmdir` (remove empty directories), `rogue` (classic BSD-style dungeon crawler), `sethostname`, `sh` (Minix Bourne shell with `~/.bash_history` and Up/Down arrow recall), `sl` (animated steam locomotive), `sync`, `tetris` (colored ANSI Tetris), `touch` (create empty files / update timestamp), `ttt` (Tic-Tac-Toe), `tty`, `vi` (`elvis`), `wc` (count lines, words, bytes).
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
