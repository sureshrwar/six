#!/usr/bin/env python3
"""Run one or more guest shell commands inside SIX and halt cleanly.

Why this script exists:
- six_host_tty_open_raw() in arch/six/kernel/host.c opens "/dev/tty" directly
  (not fd 0) and configures O_ASYNC (SIGIO) + raw mode (~ICRNL, ~ECHO).
- Therefore, automated tests MUST allocate a PTY with a controlling terminal
  via pty.fork() (setsid + TIOCSCTTY) and send carriage returns ('\\r') rather
  than newlines ('\\n').
- Commands are sent one by one as each shell prompt ('# ') appears, followed
  by 'halt' (/bin/halt), which unmounts filesystems via /proc/mounts and
  invokes reboot() to exit the host ./six process cleanly.

Usage:
  ./scripts/run_guest_cmd.py "cd /proc/20" "ls -al ./exe" "ls exe" "./exe"
"""

import argparse
import fcntl
import os
import pty
import select
import signal
import struct
import sys
import termios
import time


def set_pty_winsize(fd, rows, cols):
    ws = struct.pack("HHHH", int(rows), int(cols), 0, 0)
    fcntl.ioctl(fd, termios.TIOCSWINSZ, ws)


def run_guest_commands(commands, six_bin="./six", timeout=60.0, show_boot=False, winsize=None):
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    pid, master = pty.fork()
    if pid == 0:
        if winsize:
            set_pty_winsize(0, winsize[0], winsize[1])
        os.chdir(repo_root)
        os.execv(six_bin, [six_bin])

    if winsize:
        set_pty_winsize(master, winsize[0], winsize[1])

    buf = ""
    sent_login = False
    cmd_queue = list(commands) + ["halt"]
    last_prompt_pos = -1
    start = time.time()
    raw_mode = False

    try:
        while time.time() - start < timeout:
            r, _, _ = select.select([master], [], [], 0.05)
            if r:
                try:
                    data = os.read(master, 4096).decode("utf-8", errors="replace")
                    if not data:
                        break
                    buf += data
                except OSError:
                    # Child ./six exited cleanly after 'halt' -> reboot()
                    break

            if not sent_login and "login:" in buf:
                os.write(master, b"root\r")
                sent_login = True
                start = time.time()
                continue

            if sent_login and cmd_queue:
                if raw_mode or cmd_queue[0].startswith("__"):
                    next_cmd = cmd_queue.pop(0)
                    if next_cmd.startswith("__RESIZE__:"):
                        _, r_str, c_str = next_cmd.split(":")
                        time.sleep(0.3)
                        set_pty_winsize(master, int(r_str), int(c_str))
                        os.kill(pid, signal.SIGWINCH)
                        time.sleep(0.3)
                    elif next_cmd.startswith("__KEYS__:"):
                        payload = next_cmd[len("__KEYS__:"):].encode("utf-8").decode("unicode_escape")
                        time.sleep(0.3)
                        os.write(master, payload.encode("utf-8"))
                        time.sleep(0.3)
                        if ":q" in payload or ":wq" in payload or "ZZ" in payload:
                            raw_mode = False
                    start = time.time()
                elif buf.endswith("# "):
                    prompt_pos = buf.rfind("# ")
                    if prompt_pos > last_prompt_pos and "root@" in buf[:prompt_pos]:
                        last_prompt_pos = prompt_pos
                        next_cmd = cmd_queue.pop(0)
                        time.sleep(0.05)
                        os.write(master, (next_cmd + "\r").encode("utf-8"))
                        if next_cmd.startswith("vi "):
                            raw_mode = True
                        start = time.time()
    finally:
        try:
            os.close(master)
        except OSError:
            pass
        try:
            os.kill(pid, signal.SIGKILL)
        except OSError:
            pass
        try:
            os.waitpid(pid, 0)
        except OSError:
            pass

    if show_boot:
        return buf
    idx = buf.find("black login:")
    if idx == -1:
        idx = buf.find("login:")
    return buf[idx:] if idx != -1 else buf


def main():
    parser = argparse.ArgumentParser(
        description="Boot SIX in a controlling PTY, run guest commands, and halt cleanly."
    )
    parser.add_argument(
        "commands",
        nargs="+",
        help="Guest shell command(s) to execute after root login.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=60.0,
        help="Per-command timeout in seconds (default: 60.0).",
    )
    parser.add_argument(
        "--boot-log",
        action="store_true",
        help="Include full kernel boot log before login prompt.",
    )
    parser.add_argument(
        "--winsize",
        type=str,
        default=None,
        help="Initial PTY window size as ROWS,COLS (e.g. 50,80).",
    )
    args = parser.parse_args()

    ws = None
    if args.winsize:
        r_s, c_s = args.winsize.split(",")
        ws = (int(r_s), int(c_s))

    out = run_guest_commands(
        args.commands,
        timeout=args.timeout,
        show_boot=args.boot_log,
        winsize=ws,
    )
    sys.stdout.write(out)
    if not out.endswith("\n"):
        sys.stdout.write("\n")


if __name__ == "__main__":
    main()
