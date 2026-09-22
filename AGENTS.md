# SIX Agent Guidelines

## Running & Testing Guest Commands in `./six`
- **ALWAYS use `./scripts/run_guest_cmd.py`** when running automated guest shell commands in `./six`:
  ```bash
  ./scripts/run_guest_cmd.py "cd /proc/20" "ls -al ./exe" "ls exe" "./exe"
  ```
- **Why `./scripts/run_guest_cmd.py` is required (do NOT use `subprocess.PIPE` or plain `pty.openpty()`):**
  - `six_host_tty_open_raw()` (`arch/six/kernel/host.c`) opens `/dev/tty` directly (`TERMFD`) and registers `O_ASYNC` (`SIGIO`). Without a controlling terminal (`setsid()` + `TIOCSCTTY`, which `pty.fork()` sets up), `open("/dev/tty", O_RDWR)` returns `-1` and `./six` ignores all input.
  - `six_host_tty_open_raw()` clears `ICRNL`, so keystrokes sent to the PTY master must use `\r` (carriage return) instead of `\n`, and `root` logs directly into `root@black:~#` with no `Password:` prompt.
  - Commands must be sent one per shell prompt (`# `), followed by `halt` (`/bin/halt`, not `/sbin/halt`), which unmounts filesystems via `/proc/mounts` and calls `reboot()` to exit `./six` cleanly.

## Git Remotes
- Always push `main` to `origin` (`git push origin main`), which is configured with pushurls for both `sso://user/motorman/six` and `ssh://github.com/sureshrwar/six.git`.
