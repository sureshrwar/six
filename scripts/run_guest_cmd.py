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
import os
import pty
import select
import signal
import sys
import time


def run_guest_commands(commands, six_bin="./six", timeout=60.0, show_boot=False):
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    pid, master = pty.fork()
    if pid == 0:
        os.chdir(repo_root)
        os.execv(six_bin, [six_bin])

    buf = ""
    sent_login = False
    cmd_queue = list(commands) + ["halt"]
    last_prompt_pos = -1
    start = time.time()

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
                if buf.endswith("# "):
                    prompt_pos = buf.rfind("# ")
                    if prompt_pos > last_prompt_pos and "root@" in buf[:prompt_pos]:
                        last_prompt_pos = prompt_pos
                        next_cmd = cmd_queue.pop(0)
                        time.sleep(0.05)
                        os.write(master, (next_cmd + "\r").encode("utf-8"))
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
    args = parser.parse_args()

    out = run_guest_commands(
        args.commands,
        timeout=args.timeout,
        show_boot=args.boot_log,
    )
    sys.stdout.write(out)
    if not out.endswith("\n"):
        sys.stdout.write("\n")


if __name__ == "__main__":
    main()
