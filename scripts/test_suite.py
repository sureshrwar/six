#!/usr/bin/env python3
"""SIX Comprehensive Regression Test Suite (powered by run_guest_cmd.py).

Executes a batched suite of kernel, storage (VFS, ext2/ext4/NTFS FUSE,
Device-Mapper, Android Binder/vold/storaged/sm/usbctl), toolchain (TCC,
pthreads, binutils, strace), networking (loopback TCP httpd + lynx), and
userland application tests inside a single SIX guest boot session.

Usage:
  ./scripts/test_suite.py                 # Run all regression tests
  ./scripts/test_suite.py -k vold         # Run only tests matching 'vold'
  ./scripts/test_suite.py --list          # List all available test cases
  ./scripts/test_suite.py --verbose       # Print guest output for each test
"""

import argparse
import os
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from run_guest_cmd import run_guest_commands  # noqa: E402


class TestCase:
    def __init__(self, name, description, cmd, expected_substrings=None, raw_post_cmds=None):
        self.name = name
        self.description = description
        self.cmd = cmd
        self.expected_substrings = expected_substrings or []
        self.raw_post_cmds = raw_post_cmds or []


TESTS = [
    TestCase(
        name="vfs.mounts_and_proc",
        description="Root ext4, secondary NTFS (/bin/mount_all), overlay, tmpfs, /proc",
        cmd=(
            "df && "
            "ls -la /aux/storage-1/README && "
            "cat /proc/filesystems && "
            "cat /proc/binder && "
            "readlink /proc/1/exe"
        ),
        expected_substrings=[
            "/dev/hda",
            "/aux/storage-1",
            "/aux/linear",
            "/aux/crypt",
            "ext4",
            "fuse",
            "fuse.ntfs-3g",
            "overlay",
            "/etc/init",
        ],
    ),
    TestCase(
        name="sys.info_tools",
        description="System introspection utilities (uname, sysctl, dmesg, free, uptime, lsblk, ext4info)",
        cmd=(
            "uname -a && "
            "sysctl kernel.hostname && "
            "free && "
            "uptime && "
            "lsblk && "
            "ext4info -t /dev/hda && "
            "ext4info -t /dev/hdb && "
            "id && "
            "pwd"
        ),
        expected_substrings=[
            "Linux",
            "2.0.11",
            "kernel.hostname",
            "Mem:",
            "hda",
            "ext4",
            "NTFS",
            "uid=0(root)",
        ],
    ),
    TestCase(
        name="dm.linear_and_crypt",
        description="Device-Mapper dm-linear (/dev/dm-1) and ChaCha20-256 dm-crypt (/dev/dm-2)",
        cmd=(
            "dmsetup status && "
            "dmsetup table && "
            "echo 'dm_linear_payload' > /aux/linear/test_dm.txt && "
            "cat /aux/linear/test_dm.txt && "
            "echo 'dm_crypt_secret_payload' > /aux/crypt/secret.txt && "
            "cat /aux/crypt/secret.txt"
        ),
        expected_substrings=[
            "linear",
            "crypt",
            "dm_linear_payload",
            "dm_crypt_secret_payload",
        ],
    ),
    TestCase(
        name="binder.services",
        description="Android Binder IPC servicemanager, vold (/etc/fstab DiskSource), and storaged (mount) registration",
        cmd=(
            "cat /etc/fstab && "
            "grep 'fstab DiskSource registered' /tmp/vold.log && "
            "service list && "
            "service check vold && "
            "service check mount && "
            "sm list-disks"
        ),
        expected_substrings=[
            "voldmanaged=usb:auto,encryptable=userdata",
            "fstab DiskSource registered",
            "vold",
            "mount",
            "Service vold: found",
            "Service mount: found",
        ],
    ),
    TestCase(
        name="vold.usb_ext2",
        description="USB hotplug ext2 image + vold PublicVolume mount/read/write/unplug",
        cmd=(
            "usbctl plug ext2 && "
            "echo 'ext2_usb_ok' > /mnt/media_rw/4A8F-9C21/ext2_check.txt && "
            "cat /mnt/media_rw/4A8F-9C21/ext2_check.txt && "
            "sm list-volumes && "
            "usbctl unplug"
        ),
        expected_substrings=[
            "ext2_usb_ok",
            "mounted",
            "4A8F-9C21",
        ],
    ),
    TestCase(
        name="vold.usb_ext4",
        description="USB hotplug ext4 image + vold PublicVolume mount/read/write/unplug",
        cmd=(
            "usbctl plug ext4 && "
            "echo 'ext4_usb_ok' > /mnt/media_rw/7B9E-3D10/ext4_check.txt && "
            "cat /mnt/media_rw/7B9E-3D10/ext4_check.txt && "
            "sm list-volumes && "
            "usbctl unplug"
        ),
        expected_substrings=[
            "ext4_usb_ok",
            "mounted",
            "7B9E-3D10",
        ],
    ),
    TestCase(
        name="vold.usb_ntfs_fd_and_path_modes",
        description="NTFS-3G in vold FUSE/block fd mode (R/W + dirty R/O fallback + ntfsfix -d recovery) and direct path mode",
        cmd=(
            "usbctl plug ntfs && "
            "echo 'ntfs_vold_fd_mode_ok' > /mnt/media_rw/6A1B-8E42/vold_fd.txt && "
            "cat /mnt/media_rw/6A1B-8E42/vold_fd.txt && "
            "sm unmount public:8,1 && "
            "ntfsfix /dev/sda1 && "
            "sm mount public:8,1 && "
            "grep 'fuse.ntfs-3g ro' /proc/mounts && "
            "cat /mnt/media_rw/6A1B-8E42/vold_fd.txt && "
            "sm unmount public:8,1 && "
            "ntfsfix -d /dev/sda1 && "
            "sm mount public:8,1 && "
            "grep 'fuse.ntfs-3g rw' /proc/mounts && "
            "sm partition disk:8,0 ntfs && "
            "sm unmount public:8,1 && "
            "grep 'ForkExecvpAsyncAsUser' /tmp/vold.log && "
            "grep 'attempting R/O fallback' /tmp/vold.log && "
            "grep 'Reaping NTFS driver PID' /tmp/vold.log && "
            "mkdir -p /tmp/direct_ntfs && "
            "mkntfs -f -Q /dev/sda1 && "
            "ntfsfix -d /dev/sda1 && "
            "ntfs-3g /dev/sda1 /tmp/direct_ntfs && "
            "echo 'ntfs_direct_path_mode_ok' > /tmp/direct_ntfs/direct.txt && "
            "cat /tmp/direct_ntfs/direct.txt && "
            "umount /tmp/direct_ntfs && "
            "usbctl unplug"
        ),
        expected_substrings=[
            "ntfs_vold_fd_mode_ok",
            "fuse.ntfs-3g ro",
            "fuse.ntfs-3g rw",
            "attempting R/O fallback",
            "partitioned disk:8,0 as ntfs",
            "--ready-fd",
            "/dev/fd/",
            "Reaping NTFS driver PID",
            "mkntfs completed successfully",
            "NTFS partition /dev/sda1 was processed successfully",
            "ntfs_direct_path_mode_ok",
        ],
    ),
    TestCase(
        name="vold.adoptable_private_storage",
        description="Adoptable Storage (sm partition disk:8,0 private -> dm-crypt + ext4)",
        cmd=(
            "usbctl plug ext2 && "
            "sm partition disk:8,0 private && "
            "sm list-volumes && "
            "lsblk -f && "
            "usbctl unplug"
        ),
        expected_substrings=[
            "private:8,1",
            "mounted",
            "sda            8:0",
            "`-sda1         8:1",
            "  `-crypt_usb",
        ],
    ),
    TestCase(
        name="power.suspend_and_wakelocks",
        description="Android Opportunistic Suspend / Resume (/bin/power, /sys/power/*, wakeup_count, wake_lock, wakealarm, /proc/wakelocks, ISystemSuspend)",
        cmd=(
            "cat /sys/power/state && "
            "power lock test_suspend_lock && "
            "power sleep 80ms && "
            "power status && "
            "power unlock test_suspend_lock && "
            "power status && "
            "service check suspend"
        ),
        expected_substrings=[
            "freeze mem on",
            "active wakelock: test_suspend_lock",
            "SystemSuspend: autosuspend armed (blocked by 'test_suspend_lock')",
            "PowerState=DOZE_AUTOSUSPEND interactive=false",
            "SystemSuspend: 'test_suspend_lock' unlocked -> entering suspend (mem)",
            "syscore_suspend: timekeeping suspended, entering PSCI_SYSTEM_SUSPEND (mem)",
            "syscore_resume: woken by irq:8:rtc_alarm",
            "last_wakeup_reason: irq:8:rtc_alarm",
            "PowerState=AWAKE interactive=true",
            "\"test_suspend_lock\"",
            "Service suspend: found",
        ],
    ),
    TestCase(
        name="unix.file_ops",
        description="File and directory operations (mkdir, touch, cp, mv, ln, readlink, chmod, stat, du, rm)",
        cmd=(
            "mkdir -p /tmp/t_ops/sub && "
            "echo 'hello_six_fs' > /tmp/t_ops/a.txt && "
            "cp /tmp/t_ops/a.txt /tmp/t_ops/sub/b.txt && "
            "mv /tmp/t_ops/sub/b.txt /tmp/t_ops/c.txt && "
            "ln -s /tmp/t_ops/c.txt /tmp/t_ops/sym.txt && "
            "readlink /tmp/t_ops/sym.txt && "
            "chmod 755 /tmp/t_ops/c.txt && "
            "stat /tmp/t_ops/c.txt && "
            "du /tmp/t_ops && "
            "cat /tmp/t_ops/sym.txt && "
            "rm -rf /tmp/t_ops"
        ),
        expected_substrings=[
            "/tmp/t_ops/c.txt",
            "hello_six_fs",
        ],
    ),
    TestCase(
        name="unix.text_pipelines",
        description="Text processing pipelines (tr, sort, uniq, grep, sed, awk, cut, tee, wc, head, tail, diff, find, xargs, hexdump, strings)",
        cmd=(
            "echo 'beta' > /tmp/p.txt && echo 'alpha' >> /tmp/p.txt && echo 'beta' >> /tmp/p.txt && "
            "cat /tmp/p.txt | sort | uniq | sed 's/alpha/ALPHA/' | awk '{printf \"%d:%s\\n\", NR, $1}' && "
            "echo 'one:two:three' | cut -d: -f2 && "
            "echo 'hex_check' | hexdump && "
            "echo 'diff_line' > /tmp/d1.txt && cp /tmp/d1.txt /tmp/d2.txt && diff /tmp/d1.txt /tmp/d2.txt && "
            "find /tmp -name 'd1.txt' | xargs cat && "
            "rm /tmp/p.txt /tmp/d1.txt /tmp/d2.txt"
        ),
        expected_substrings=[
            "1:ALPHA",
            "2:beta",
            "two",
            "diff_line",
        ],
    ),
    TestCase(
        name="unix.tar_archive",
        description="POSIX tar archive creation (-cf), listing (-tf), and extraction (-xf)",
        cmd=(
            "mkdir -p /tmp/tar_in /tmp/tar_out && "
            "echo 'tar_archive_payload_42' > /tmp/tar_in/item.txt && "
            "tar -cf /tmp/archive.tar -C /tmp/tar_in item.txt && "
            "tar -tf /tmp/archive.tar && "
            "tar -xf /tmp/archive.tar -C /tmp/tar_out && "
            "cat /tmp/tar_out/item.txt && "
            "rm -rf /tmp/tar_in /tmp/tar_out /tmp/archive.tar"
        ),
        expected_substrings=[
            "item.txt",
            "tar_archive_payload_42",
        ],
    ),
    TestCase(
        name="toolchain.tcc_and_binutils",
        description="In-guest TinyCC C compiler (/etc/demos/hello.c & pthread_demo.c) + file/nm/size/strings",
        cmd=(
            "tcc -o /tmp/hello_bin /etc/demos/hello.c && "
            "/tmp/hello_bin && "
            "file /tmp/hello_bin && "
            "size /tmp/hello_bin && "
            "nm /tmp/hello_bin | head -n 5 && "
            "tcc -o /tmp/pthread_bin /etc/demos/pthread_demo.c && "
            "/tmp/pthread_bin && "
            "rm /tmp/hello_bin /tmp/pthread_bin"
        ),
        expected_substrings=[
            "ELF 32-bit LSB executable",
            "text",
            "Final shared_counter = 1000",
        ],
    ),
    TestCase(
        name="proc.strace_lsof_ps",
        description="System call tracing (strace), open file listing (lsof), and process table (ps)",
        cmd=(
            "strace /bin/echo strace_target_ok && "
            "ps -a && "
            "lsof | head -n 10"
        ),
        expected_substrings=[
            "strace_target_ok",
            "write(",
            "servicemanager",
            "vold",
        ],
    ),
    TestCase(
        name="net.loopback_httpd_lynx",
        description="Loopback TCP networking: ifconfig + in-guest httpd server + lynx -dump client",
        cmd=(
            "ifconfig && "
            "lynx -dump http://127.0.0.1/ | head -n 15"
        ),
        expected_substrings=[
            "127.0.0.1",
            "SIX",
        ],
    ),
    TestCase(
        name="apps.cli_and_basic",
        description="CLI applications: BASIC, eliza, advent, zork, top, testdir, dd, cowsay, fortune, cal, banner, sixanim, dhrystone",
        cmd=(
            "echo '10 LET A = 6 * 7' > /tmp/test.bas && "
            "echo '20 PRINT \"BASIC_RESULT=\"; A' >> /tmp/test.bas && "
            "echo '30 END' >> /tmp/test.bas && "
            "basic /tmp/test.bas && "
            "rm /tmp/test.bas && "
            "echo 'CAN YOU TEST SIX' > /tmp/eliza.in && "
            "echo 'BYE' >> /tmp/eliza.in && "
            "eliza < /tmp/eliza.in && "
            "rm /tmp/eliza.in && "
            "echo 'no' > /tmp/adv.in && echo 'quit' >> /tmp/adv.in && echo 'yes' >> /tmp/adv.in && "
            "advent < /tmp/adv.in && "
            "rm /tmp/adv.in && "
            "echo 'look' > /tmp/zork.in && echo 'quit' >> /tmp/zork.in && echo 'y' >> /tmp/zork.in && "
            "zork < /tmp/zork.in && "
            "rm /tmp/zork.in && "
            "top -n 1 | head -n 8 && "
            "testdir /etc && "
            "dd if=/dev/zero of=/tmp/dd.bin bs=1024 count=4 && "
            "rm /tmp/dd.bin && "
            "cowsay 'regression_moo' && "
            "fortune && "
            "cal 9 2026 && "
            "banner SIX && "
            "sixanim && "
            "echo 'd3q' | reversi && "
            "dhrystone"
        ),
        expected_substrings=[
            "BASIC_RESULT=42",
            "ELIZA: Goodbye",
            "Welcome to ADVENTURE!",
            "Welcome to Dungeon.",
            "KiB Mem :",
            "closedir OK",
            "4+0 records out",
            "regression_moo",
            "2026",
            "SIX REVERSI",
            "Computer (O) played",
            "Dhrystone",
        ],
    ),
    TestCase(
        name="apps.interactive_vi",
        description="Interactive vi editor session via PTY keystroke injection",
        cmd="vi /tmp/vi_regression.txt",
        raw_post_cmds=[
            r"__KEYS__:iVI_INTERACTIVE_OK\x1b:wq\r",
            "cat /tmp/vi_regression.txt && echo __VI_CAT_DONE__",
        ],
        expected_substrings=[
            "VI_INTERACTIVE_OK",
            "__VI_CAT_DONE__",
        ],
    ),
    TestCase(
        name="sadb.bridge_and_shell",
        description="SIX Android Debug Bridge (/dev/sadb, sadbd, sadb devices/shell/push/pull/vi)",
        cmd=(
            "for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do if [ -f /tmp/sadb_done_flag ]; then break; fi; sleep 0.25; done && "
            "cat /proc/sadb && "
            "cat /tmp/sadb_pushed.txt && "
            "cat /tmp/sadb_pty_out.txt && "
            "cat /tmp/sadb_vi_out.txt && "
            "cat /tmp/sadbd.log"
        ),
        expected_substrings=[
            "SIX Android Debug Bridge (sadb) Transport:",
            "device:            /dev/sadb (char 61:0)",
            "SADB_HOST_PUSH_PAYLOAD_OK",
            "/dev/ttyp",
            "SADB_INTERACTIVE_PTY_OK",
            "SADB_VI_FIRST",
            "SADB_VI_SECOND",
        ],
    ),
    TestCase(
        name="fs.erofs",
        description="EROFS v1 read-only filesystem on /bin (dm-verity) and vold USB hotplug (usb_erofs.img)",
        cmd=(
            "grep verity_bin /proc/mounts && "
            "grep '/bin.*erofs' /etc/fstab && "
            "usbctl plug erofs && "
            "sleep 1 && "
            "sm list-volumes && "
            "lsblk -f && "
            "cat /mnt/media_rw/usb/README_USB.txt && "
            "usbctl unplug"
        ),
        expected_substrings=[
            "/dev/mapper/verity_bin /bin erofs ro",
            "PUBLIC(EROFS)",
            "bin_verity",
            "SANDISK_EROFS",
            "SanDisk Extreme EROFS Read-Only Flash Drive",
        ],
    ),
    TestCase(
        name="kernel.hung_task_sys_info",
        description="Panic/kernel/hung_task sys_info bitmask configuration and live khungtaskd D-state detection",
        cmd=(
            "sysctl -w kernel.panic_sys_info=tasks,mem,locks && "
            "sysctl -w kernel.panic_sys_info=+ftrace,-mem && "
            "sysctl -w kernel.kernel_sys_info=timers,blocked_tasks && "
            "sysctl -w kernel.hung_task_panic=0 && "
            "sysctl -w kernel.hung_task_warnings=1 && "
            "sysctl -w kernel.hung_task_timeout_secs=1 && "
            "sysctl -w kernel.hung_task_sys_info=blocked_tasks,timers && "
            "set_d && "
            "sleep 3 && "
            "clear_d && "
            "sysctl kernel.hung_task_detect_count && "
            "sysctl kernel.hung_task_warnings && "
            "sysctl -w kernel.hung_task_timeout_secs=120 && "
            "sysctl -w kernel.hung_task_warnings=10 && "
            "sysctl -w kernel.hung_task_sys_info=0 && "
            "sysctl -w kernel.panic_sys_info=0 && "
            "sysctl -w kernel.kernel_sys_info=0"
        ),
        expected_substrings=[
            "kernel.panic_sys_info = 11 (0x0b: tasks,mem,locks)",
            "kernel.panic_sys_info = 25 (0x19: tasks,locks,ftrace)",
            "kernel.kernel_sys_info = 68 (0x44: timers,blocked_tasks)",
            "kernel.hung_task_sys_info = 68 (0x44: timers,blocked_tasks)",
            "blocked for more than 1 seconds.",
            "blocked on a buffer likely last held by task kflushd",
            "Blocked tasks (TASK_UNINTERRUPTIBLE / D state):",
            "Timer List Dump",
            "Showing all locks held in the system",
            "kernel.hung_task_warnings = 0",
        ],
    ),
    TestCase(
        name="debug.gdb_and_ptrace",
        description="Ptrace syscall, DWARF/.stab source-level & assembly debugging, and live PID attach via /bin/gdb",
        cmd=(
            'gdb -q -batch -ex "list main" -ex "b call_power_binder" -ex "r" -ex "bt" -ex "info regs" -ex "n" -ex "finish" -ex "c" /bin/power && '
            "tcc -g /etc/demos/hello.c -o /tmp/hello_dbg && "
            'gdb -q -batch -ex "b main" -ex "r" -ex "bt" -ex "n" -ex "n" -ex "disas main" -ex "c" /tmp/hello_dbg && '
            "rm -f /tmp/hello_dbg && "
            'gdb -q -batch -p `ps -e | grep servicemanager | awk \'{print $1}\'` -ex "info proc" -ex "bt" -ex "x/2i \\$eip" -ex "detach" && '
            "service list"
        ),
        expected_substrings=[
            "Breakpoint 1 at",
            "call_power_binder () at applications/power/power.c:53",
            "Value returned is $eax = 0x0 (0)",
            "[Binder IPowerManager] PowerState=AWAKE",
            "main () at /etc/demos/hello.c:6",
            "TCC Self-Hosting Verification on SIX",
            "Dump of assembler code for function main:",
            "Attaching to process ",
            "Reading symbols from /bin/servicemanager... done",
            "at applications/servicemanager/servicemanager.c:118",
            "Detaching from program: /bin/servicemanager, process ",
            "Found 6 services:",
        ],
    ),
    TestCase(
        name="sys.user_and_passwd_mgmt",
        description="User & password management (useradd/adduser, passwd, su setuid, usermod, chown, whoami, who, userdel)",
        cmd=(
            "whoami && who && "
            "useradd -m -c 'Alice Test' alice && "
            "grep '^alice:' /etc/passwd && "
            "echo 's3cr3t' | passwd --stdin alice && "
            "passwd -S alice && "
            "su - -c 'id; whoami; pwd' alice && "
            "su -c 'echo s3cr3t | su -c whoami alice' guest && "
            "usermod -c 'Alice Updated' alice && "
            "grep 'Alice Updated' /etc/passwd && "
            "passwd -l alice && passwd -S alice && "
            "passwd -u alice && passwd -d alice && passwd -S alice && "
            "touch /tmp/u_file && chown alice:alice /tmp/u_file && "
            "ls -lg /tmp/u_file && rm /tmp/u_file && "
            "userdel -r alice"
        ),
        expected_substrings=[
            "Alice Test:/home/alice:/bin/sh",
            "passwd: password updated successfully",
            "alice P",
            "uid=1000(alice) gid=1000(alice)",
            "/home/alice",
            "Password: alice",
            "Alice Updated:/home/alice:/bin/sh",
            "alice L",
            "alice NP",
            "/tmp/u_file",
        ],
    ),
    TestCase(
        name="android.mediaprovider_fuse",
        description="Android MediaProvider (/dev/fuse fuse.mediaprovider daemon, Scoped Storage per-UID enforcement, EXIF GPS redaction, content://media CLI)",
        cmd=(
            "content status && "
            "cd /storage/emulated/0/Documents && pwd && "
            "cd /storage/emulated/0/DCIM/Camera && pwd && "
            "cd .. && pwd && cd / && "
            "content query --uri content://media/external/file && "
            "usbctl plug ext2 SAN_DISK_USB XYZ && "
            "sleep 1 && "
            "content status && "
            "su six -c 'ls /mnt/media_rw/XYZ || echo RAW_USB_DENIED' && "
            "su six -c 'echo hello_from_six > /storage/XYZ/Pictures/six_note.txt' && "
            "su six -c 'content query --uri content://media/external/images/media' && "
            "content query --uri content://media/external/images/media && "
            "su six -c 'grep EXIF: /sdcard/DCIM/Camera/IMG_20260927_GPS.jpg' && "
            "grep EXIF: /sdcard/DCIM/Camera/IMG_20260927_GPS.jpg && "
            "su six -c 'echo secret_token > /storage/XYZ/Android/data/six/token.txt' && "
            "su guest -c 'cat /storage/XYZ/Android/data/six/token.txt || echo SANDBOX_DENIED' && "
            "su guest -c 'rm /storage/XYZ/Pictures/six_note.txt || echo DELETE_DENIED' && "
            "su six -c 'files --dump /storage/XYZ' && "
            "su six -c 'rm /storage/XYZ/Pictures/six_note.txt' && "
            "usbctl unplug && "
            "content status"
        ),
        expected_substrings=[
            "volume external_primary : /data/media/0 -> /storage/emulated/0 (fuse.mediaprovider)",
            "/storage/emulated/0/Documents",
            "/storage/emulated/0/DCIM/Camera",
            "/storage/emulated/0/DCIM",
            "volume xyz              : /mnt/media_rw/XYZ -> /storage/XYZ (fuse.mediaprovider)",
            "RAW_USB_DENIED",
            "lat=REDACTED, lon=REDACTED",
            "lat=37.4220N, lon=122.0841W",
            "GPSLatitude=REDACTED;GPSLongitude=REDACTED",
            "GPSLatitude=37.4220N;GPSLongitude=122.0841W;",
            "SANDBOX_DENIED",
            "DELETE_DENIED",
            "[STORAGE] SAN_DISK_USB (USB) -> /storage/XYZ",
        ],
    ),
    TestCase(
        name="apps.interactive_files",
        description="Android Desktop curses Files app (/bin/files) with highlighted selection, Scoped Storage Inspector, live USB hotplug auto-refresh, and dynamic full-terminal resize",
        cmd=(
            "sh -c 'sleep 1; usbctl plug ext4 >/dev/null 2>&1' & "
            "su six -c 'files /storage/emulated/0/Pictures'"
        ),
        raw_post_cmds=[
            "__SLEEP__:1.5",
            "__RESIZE__:40:120",
            "__SLEEP__:0.4",
            r"__KEYS__:nsix_ui_note.txt\r",
            r"__KEYS__:\t\x1b[A\x1b[A\x1b[A\x1b[A\r",
            "__SLEEP__:0.4",
            "__RESIZE__:24:80",
            r"__KEYS__:q",
            "cat /storage/emulated/0/Pictures/six_ui_note.txt && rm /storage/emulated/0/Pictures/six_ui_note.txt && usbctl unplug",
        ],
        expected_substrings=[
            "Files (Android Desktop)",
            "LIBRARIES",
            "STORAGE DEVICES",
            "INSPECTOR",
            "SANDISK_EXT4 (USB)",
            "[HOTPLUG] USB Mounted: SANDISK_EXT4 (ext4) -> /storage/7B9E-3D10",
            "[RESIZE] Terminal resized to 120x40 (full-window layout active)",
            "[120x40]",
            "MEDIA METADATA",
            "[FILES] Created six_ui_note.txt (owner=six)",
            "EXIF GPS REDACTED",
            "Created by six(uid=1005) in SIX Files app.",
        ],
    ),
    TestCase(
        name="nvme.controller_and_userdata",
        description="NVMe 1.4 controller (/dev/nvme0) & namespace (/dev/nvme0n1 -> /data userdata ext4, SQ/CQ rings, Identify, SMART log, Flush, DSM/TRIM)",
        cmd=(
            "lsblk -f && "
            "df && "
            "nvme list && "
            "nvme id-ctrl /dev/nvme0 && "
            "nvme id-ns /dev/nvme0n1 && "
            "echo nvme_userdata_ok > /storage/emulated/0/Documents/nvme_test.txt && "
            "sync && "
            "cat /data/media/0/Documents/nvme_test.txt && "
            "rm /storage/emulated/0/Documents/nvme_test.txt && "
            "nvme flush /dev/nvme0n1 && "
            "nvme dsm /dev/nvme0n1 -s 32000 -b 16 && "
            "nvme smart-log /dev/nvme0 && "
            "cat /proc/nvme"
        ),
        expected_substrings=[
            "nvme0n1        63:0     0   16M  0 disk    ext4   userdata   /data",
            "/dev/nvme0n1",
            "SIX-NVME-2026-0001",
            "SIX Virtual NVMe SSD Controller",
            "ver       : 0x00010400 (NVMe 1.4.0)",
            "sqes      : 0x66 (64 bytes)",
            "cqes      : 0x44 (16 bytes)",
            "subnqn    : nqn.2026-09.org.six:nvme:userdata-ssd-0001",
            "nsze      : 0x8000 (32768 sectors / 16384 KB)",
            "nguid     : SIXNVME0N1USER01",
            "lbaf  0   : ms:0   lbads:9 (512 B)  rp:0x0 (in use)",
            "nvme_userdata_ok",
            "NVMe Flush: success (NSID 1)",
            "NVMe DSM (Deallocate/TRIM): success (slba=32000, blocks=16 on /dev/nvme0n1)",
            "dsm_trim_commands                   : 1 (16 sectors trimmed)",
            "NVMe Controller:   /dev/nvme0 (char 59:0, PCIe 0000:01:00.0)",
            "Namespace 1:       /dev/nvme0n1 (block 63:0, host=./disk/x86/nvme0n1.img)",
        ],
    ),
    TestCase(
        name="ufs.controller_multilun_and_rpmb",
        description="JEDEC UFS 4.0 controller (/dev/ufs-bsg0), Multi-LUNs (/dev/ufsa -> /ufs ext4, /dev/ufsb & /dev/ufsc A/B boot slots), WriteBooster, and HMAC-authenticated RPMB (/dev/ufs-rpmb)",
        cmd=(
            "lsblk -f && "
            "df && "
            "cat /ufs/UFS.TXT && "
            "echo ufs_lun0_write_ok > /ufs/test_ufs.txt && "
            "sync && "
            "cat /ufs/test_ufs.txt && "
            "rm /ufs/test_ufs.txt && "
            "ufs list && "
            "ufs desc && "
            "ufs boot-slot b && "
            "ufs boot-slot a && "
            "ufs writebooster toggle && "
            "ufs writebooster on && "
            "ufs flush /dev/ufsa && "
            "ufs unmap /dev/ufsa -s 8000 -b 8 && "
            "ufs rpmb write-key six_avb_rpmb_key_2026 && "
            "ufs rpmb write 0 avb_rollback_index=42 six_avb_rpmb_key_2026 && "
            "(ufs rpmb write 0 forged_payload wrong_key || true) && "
            "ufs rpmb read 0 && "
            "ufs rpmb counter && "
            "cat /proc/ufs"
        ),
        expected_substrings=[
            "ufsa           58:0     0    4M  0 disk    ext4   ufs_data   /ufs",
            "ufsb           58:1     0  384K  0 disk    boot   slot_a",
            "ufsc           58:2     0  384K  0 disk    boot   slot_b",
            "/dev/ufsa",
            "SIX JEDEC UFS 4.0 Multi-LUN Storage (/dev/ufsa -> /ufs)",
            "ufs_lun0_write_ok",
            "SIX-UFS-4.0-5M-CHIP",
            "wSpecVersion                    : 0x0400 (JEDEC UFS 4.0)",
            "bPreEOLInfo                     : 0x01 (Normal)",
            "UFS Boot Slot switched: bBootLunID=0x02 -> Slot B (/dev/ufsc)",
            "UFS Boot Slot switched: bBootLunID=0x01 -> Slot A (/dev/ufsb)",
            "UFS 4.0 SLC WriteBooster: fWriteBoosterEn=1 (ENABLED), AvailBuffer=100%",
            "UFS SYNCHRONIZE_CACHE(10): success (LUN 0)",
            "UFS SCSI UNMAP: success (LUN 0, lba=8000, blocks=8)",
            "RPMB Program Authentication Key: success (256-bit HMAC-SHA256 key active)",
            "RPMB Authenticated Write: success (block=0, new_write_counter=1)",
            "RPMB Authenticated Write REJECTED: result=0x0002 (RPMB_RES_AUTH_FAILURE)",
            "RPMB Block 0 (write_counter=1): avb_rollback_index=42",
            "RPMB Status: Key PROGRAMMED, Write Counter = 1 (result=0x0000 OK)",
            "UFS Host Controller: /dev/ufs-bsg0 (char 57:0, UFSHCI v4.0, MIPI UniPro/M-PHY)",
            "Host Flash Image:    ./disk/x86/ufs0.img (5120 KB / 5 MB unified chip image)",
        ],
    ),
    TestCase(
        name="fwupd.lvfs_nvme_and_scsi_ffu",
        description="Firmware Update Manager (/bin/fwupdmgr) with nvme, ufs, and scsi plugins and real-world .bin firmware support",
        cmd=(
            "fwupdmgr clear-history && "
            "fwupdmgr get-plugins && "
            "fwupdmgr refresh && "
            "fwupdmgr get-devices && "
            "fwupdmgr get-updates && "
            "fwupdmgr examine /etc/fwupd/remotes.d/lvfs/packages/six-nvme-ssd-1.4.2.bin && "
            "fwupdmgr update && "
            "nvme fw-log /dev/nvme0 && "
            "fwupdmgr verify && "
            "echo 'VENDOR_NVME_MICROCODE_BLOB_REV_1.5.0_PADDING_0123456789ABCDEF0123456789ABCDEF' > /tmp/vendor_nvme.bin && "
            "fwupdmgr examine /tmp/vendor_nvme.bin && "
            "fwupdmgr install-blob /tmp/vendor_nvme.bin SIX-NVME-SSD && "
            "fwupdmgr verify SIX-NVME-SSD && "
            "rm /tmp/vendor_nvme.bin && "
            "usbctl plug ext4 && "
            "fwupdmgr get-updates && "
            "fwupdmgr update SIX-USB-SCSI && "
            "usbctl unplug && "
            "cp /etc/fwupd/remotes.d/lvfs/packages/six-nvme-ssd-1.4.0.bin /tmp/bad.bin && "
            "dd if=/dev/zero of=/tmp/bad.bin bs=1 seek=200 count=16 && "
            "(fwupdmgr install /tmp/bad.bin --allow-older || true) && "
            "rm /tmp/bad.bin && "
            "fwupdmgr activate SIX-NVME-SSD 1 && "
            "fwupdmgr install /etc/fwupd/remotes.d/lvfs/packages/six-ufs-flash-4.00.bin --allow-older && "
            "fwupdmgr get-history"
        ),
        expected_substrings=[
            "nvme     [ENABLED]  NVM Express 1.4 Controller Firmware Update Plugin",
            "ufs      [ENABLED]  JEDEC UFS 4.0 Field Firmware Update (FFU) Plugin",
            "scsi     [ENABLED]  SPC-4 SCSI Field Firmware Update (FFU) Plugin",
            "Successfully refreshed LVFS metadata (3 component releases, 6 firmware .bin images staged",
            "Target GUID    : b585990a-003e-5270-89d5-3705a17f9a43",
            "[VALID SIGNATURE]",
            "[nvme] NVME_ADMIN_DOWNLOAD_FW (0x11): chunk 1/3 (512/1184 bytes, OFST=0 dwords)",
            "[nvme] NVME_ADMIN_ACTIVATE_FW (0x10): committed & activated Slot 2 (CA=3, FS=2)",
            "Successfully updated SIX Virtual NVMe SSD Controller (SIX-NVME-SSD) from 1.4.0 to 1.4.2 [VERIFIED]",
            "[ufs] UFS FFU WRITE_BUFFER (0x3B, Mode 0x0E): chunk 1/3 (512/1184 bytes, offset=0x0000)",
            "[ufs] UFS FFU WRITE_BUFFER (0x3B, Mode 0x0F): deferred microcode verified & activated",
            "Successfully updated SIX JEDEC UFS 4.0 Flash Controller (SIX-UFS-FLASH) from 4.00 to 4.10 [VERIFIED]",
            "afi  : 0x22 (Active Slot: 2, Next Reset Slot: 2)",
            "frs1 : 1.4.0 [RO Factory]",
            "frs2 : 1.4.2 [RW Updatable]",
            "Verified SIX Virtual NVMe SSD Controller (SIX-NVME-SSD, plugin=nvme, version=1.4.2):",
            "Verified SIX JEDEC UFS 4.0 Flash Controller (SIX-UFS-FLASH, plugin=ufs, version=4.10):",
            "Raw Vendor Controller Microcode Binary (.bin)",
            "Successfully updated SIX Virtual NVMe SSD Controller (SIX-NVME-SSD) from 1.4.2 to 1.5.0 [VERIFIED]",
            "Verified SIX Virtual NVMe SSD Controller (SIX-NVME-SSD, plugin=nvme, version=1.5.0):",
            "[scsi] SCSI WRITE_BUFFER (0x3B, Mode 0x0E): chunk 1/3 (512/1184 bytes, offset=0x0000)",
            "[scsi] SCSI WRITE_BUFFER (0x3B, Mode 0x0F): deferred microcode verified & activated",
            "Successfully updated SIX USB Mass Storage SCSI Disk (SIX-USB-SCSI) from 1.00 to 1.10 [VERIFIED]",
            "[nvme] NVME_ADMIN_ACTIVATE_FW (0x10) REJECTED by controller (status=NVME_SC_FW_IMAGE_ERROR)",
            "fwupdmgr: firmware update FAILED on SIX-NVME-SSD (hardware rejected .bin image)",
            "Switched SIX Virtual NVMe SSD Controller (SIX-NVME-SSD) active firmware slot to Slot 1 (version 1.4.0)",
            "Successfully updated SIX JEDEC UFS 4.0 Flash Controller (SIX-UFS-FLASH) from 4.10 to 4.00 [VERIFIED]",
            "status=failed-signature",
        ],
    ),
]


def build_guest_command_queue(selected_tests):
    queue = []
    for tc in selected_tests:
        queue.append(f'echo "__B""EGIN:{tc.name}__"')
        for sub_cmd in tc.cmd.split(" && "):
            sub_cmd = sub_cmd.strip()
            if sub_cmd:
                assert len(sub_cmd) < 240, f"Command exceeds TTY MAX_CANON: {sub_cmd}"
                queue.append(sub_cmd)
        if tc.raw_post_cmds:
            queue.extend(tc.raw_post_cmds)
        queue.append(f'echo "__E""ND:{tc.name}:$?__"')
    return queue


GREEN_PASSED = "\033[1;32mPASSED\033[0m"
RED_FAILED = "\033[1;31mFAILED\033[0m"


def evaluate_single_test(raw_output, tc):
    begin_marker = f"__BEGIN:{tc.name}__"
    end_pattern = re.compile(rf"__END:{re.escape(tc.name)}:(\d+)__")

    b_idx = raw_output.find(begin_marker)
    if b_idx == -1:
        return None

    sub = raw_output[b_idx + len(begin_marker) :]
    m = end_pattern.search(sub)
    if not m:
        return None

    rc = int(m.group(1))
    body = sub[: m.start()].strip()
    missing = [s for s in tc.expected_substrings if s not in body]
    passed = (rc == 0) and (len(missing) == 0)
    reason = ""
    if rc != 0:
        reason = f"Non-zero exit status: {rc}"
    elif missing:
        reason = f"Missing expected output: {missing}"

    return {
        "passed": passed,
        "rc": rc,
        "output": body,
        "reason": reason,
    }


def parse_test_results(raw_output, selected_tests):
    results = {}
    for tc in selected_tests:
        res = evaluate_single_test(raw_output, tc)
        if res is None:
            b_idx = raw_output.find(f"__BEGIN:{tc.name}__")
            results[tc.name] = {
                "passed": False,
                "rc": -1,
                "output": raw_output[b_idx:] if b_idx != -1 else "",
                "reason": "Test end marker not found (guest command timed out or aborted)",
            }
        else:
            results[tc.name] = res
    return results


def run_shard(
    shard_id,
    total_shards,
    shard_tests,
    repo_root,
    timeout,
    use_temp_workspace,
    idx_map,
    total_tests,
    print_lock=None,
):
    worker_prefix = f"[Worker {shard_id}/{total_shards}] " if total_shards > 1 else ""
    work_dir = repo_root
    tmp_dir = None

    try:
        if use_temp_workspace:
            import shutil
            import subprocess
            import tempfile

            tmp_dir = tempfile.mkdtemp(prefix=f"six_test_w{shard_id}_")
            work_dir = tmp_dir
            for entry in ("six", "applications", "library", "include"):
                os.symlink(os.path.join(repo_root, entry), os.path.join(work_dir, entry))
            os.makedirs(os.path.join(work_dir, "disk"), exist_ok=True)
            subprocess.run(
                [
                    "cp",
                    "-r",
                    "--reflink=auto",
                    os.path.join(repo_root, "disk", "x86"),
                    os.path.join(work_dir, "disk", "x86"),
                ],
                check=True,
            )

        print(
            f"[INFO] {worker_prefix}Booting SIX guest instance for {len(shard_tests)} test suite(s)...",
            flush=True,
        )

        tc_by_name = {tc.name: tc for tc in shard_tests}
        active_test = [None]
        completed_tests = set()

        def run_host_sadb_exercise():
            import pty
            import subprocess
            import tempfile
            import threading

            def _worker():
                sadb_bin = os.path.join(repo_root, "tools", "sadb")
                try:
                    subprocess.run(
                        [sadb_bin, "wait-for-device"],
                        cwd=work_dir,
                        timeout=5,
                        check=False,
                        stdout=subprocess.DEVNULL,
                        stderr=subprocess.DEVNULL,
                    )
                    # 1. sadb devices -l
                    subprocess.run(
                        [sadb_bin, "devices", "-l"],
                        cwd=work_dir,
                        timeout=5,
                        check=False,
                        stdout=subprocess.DEVNULL,
                    )
                    # 2. sadb push
                    with tempfile.NamedTemporaryFile("w", delete=False) as tf:
                        tf.write("SADB_HOST_PUSH_PAYLOAD_OK\n")
                        tf_path = tf.name
                    subprocess.run(
                        [sadb_bin, "push", tf_path, "/tmp/sadb_pushed.txt"],
                        cwd=work_dir,
                        timeout=5,
                        check=False,
                        stdout=subprocess.DEVNULL,
                    )
                    os.unlink(tf_path)
                    # 3. sadb interactive PTY shell (allocating /dev/ttypX)
                    mfd, sfd = pty.openpty()
                    proc = subprocess.Popen(
                        [sadb_bin, "shell"],
                        cwd=work_dir,
                        stdin=sfd,
                        stdout=sfd,
                        stderr=sfd,
                        close_fds=True,
                    )
                    os.close(sfd)
                    time.sleep(0.15)
                    # Verify Ctrl-C (\x03) and Ctrl-X (\x18) cancel partially typed commands in sadb shell
                    os.write(mfd, b"echo CANCELLED_BY_CTRL_C >> /tmp/sadb_pty_out.txt\x03")
                    time.sleep(0.10)
                    os.write(mfd, b"echo CANCELLED_BY_CTRL_X >> /tmp/sadb_pty_out.txt\x18")
                    time.sleep(0.10)
                    os.write(
                        mfd,
                        b"tty >> /tmp/sadb_pty_out.txt; echo SADB_INTERACTIVE_PTY_OK >> /tmp/sadb_pty_out.txt; exit\r",
                    )
                    try:
                        proc.wait(timeout=4)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                    os.close(mfd)
                    # 4. sadb shell vi /tmp/sadb_vi_out.txt (with TERM=xterm-256color and multiple ESC keystrokes)
                    mfd2, sfd2 = pty.openpty()
                    env_vi = dict(os.environ)
                    env_vi["TERM"] = "xterm-256color"
                    proc2 = subprocess.Popen(
                        [sadb_bin, "shell", "vi", "/tmp/sadb_vi_out.txt"],
                        cwd=work_dir,
                        env=env_vi,
                        stdin=sfd2,
                        stdout=sfd2,
                        stderr=sfd2,
                        close_fds=True,
                    )
                    os.close(sfd2)
                    time.sleep(0.25)
                    os.write(mfd2, b"iSADB_VI_FIRST\x1b")
                    time.sleep(0.20)
                    os.write(mfd2, b"oSADB_VI_SECOND\x1b")
                    time.sleep(0.20)
                    os.write(mfd2, b":wq\r")
                    try:
                        proc2.wait(timeout=4)
                    except subprocess.TimeoutExpired:
                        proc2.kill()
                    os.close(mfd2)
                    # 5. sadb shell <cmd> to mark completion
                    subprocess.run(
                        [sadb_bin, "shell", "echo DONE > /tmp/sadb_done_flag"],
                        cwd=work_dir,
                        timeout=5,
                        check=False,
                        stdout=subprocess.DEVNULL,
                    )
                except Exception:
                    pass

            t = threading.Thread(target=_worker, daemon=True)
            t.start()

        def flush_active_test(buf):
            tc = active_test[0]
            if tc and tc.name not in completed_tests:
                res = evaluate_single_test(buf, tc)
                if res is not None:
                    completed_tests.add(tc.name)
                    status_str = GREEN_PASSED if res["passed"] else RED_FAILED
                    if total_shards == 1:
                        sys.stdout.write(f"{status_str}\n")
                        sys.stdout.flush()
                    else:
                        idx = idx_map.get(tc.name, 0)
                        desc = f" ({tc.description})"
                        line = (
                            f"[INFO] {worker_prefix}[{idx}/{total_tests}] "
                            f"Running test suite '{tc.name}'{desc}...{status_str}\n"
                        )
                        if print_lock:
                            with print_lock:
                                sys.stdout.write(line)
                                sys.stdout.flush()
                        else:
                            sys.stdout.write(line)
                            sys.stdout.flush()

        last_buf = [""]

        def on_cmd(cmd):
            m = re.match(r'^echo "__B""EGIN:(.+)__"$', cmd)
            if m:
                flush_active_test(last_buf[0])
                tname = m.group(1)
                tc = tc_by_name.get(tname)
                idx = idx_map.get(tname, 0)
                desc = f" ({tc.description})" if tc else ""
                active_test[0] = tc
                if tname == "sadb.bridge_and_shell":
                    run_host_sadb_exercise()
                if total_shards == 1:
                    sys.stdout.write(
                        f"[INFO] [{idx}/{total_tests}] Running test suite '{tname}'{desc}..."
                    )
                    sys.stdout.flush()

        def on_out(buf):
            last_buf[0] = buf
            flush_active_test(buf)

        cmd_queue = build_guest_command_queue(shard_tests)
        raw_out = run_guest_commands(
            cmd_queue,
            six_bin=os.path.join(work_dir, "six"),
            timeout=timeout,
            cwd=work_dir,
            on_command_sent=on_cmd,
            on_output=on_out,
        )
        # If the last active test in sequential mode timed out without __END__, close its line
        if total_shards == 1 and active_test[0] and active_test[0].name not in completed_tests:
            sys.stdout.write(f"{RED_FAILED}\n")
            sys.stdout.flush()

        return parse_test_results(raw_out, shard_tests)
    finally:
        if tmp_dir and os.path.exists(tmp_dir):
            import shutil

            shutil.rmtree(tmp_dir, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(
        description="Run the SIX kernel & userland regression test suite via run_guest_cmd.py."
    )
    parser.add_argument(
        "-k",
        "--filter",
        type=str,
        default="",
        help="Only run tests whose name or description matches this substring.",
    )
    parser.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=1,
        help="Number of concurrent SIX guest worker sessions (default: 1 = sequential).",
    )
    parser.add_argument(
        "--parallel",
        action="store_true",
        help="Shorthand for --jobs 4 (run test suites concurrently across 4 isolated SIX workers).",
    )
    parser.add_argument(
        "--list",
        action="store_true",
        help="List all test cases and exit.",
    )
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Show captured guest output for every test (not just failures).",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=90.0,
        help="Per-command timeout in seconds (default: 90.0).",
    )
    args = parser.parse_args()

    selected = [
        tc
        for tc in TESTS
        if args.filter.lower() in tc.name.lower()
        or args.filter.lower() in tc.description.lower()
    ]

    if args.list:
        for tc in selected:
            print(f"  {tc.name:<34} {tc.description}")
        return 0

    if not selected:
        print(f"No tests matched filter '{args.filter}'.", file=sys.stderr)
        return 1

    jobs = 4 if args.parallel and args.jobs == 1 else max(1, args.jobs)
    jobs = min(jobs, len(selected))
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    idx_map = {tc.name: i + 1 for i, tc in enumerate(selected)}

    mode_str = (
        "in a single sequential guest session"
        if jobs == 1
        else f"across {jobs} concurrent guest worker sessions"
    )
    print(f"=== Running {len(selected)} SIX Regression Test(s) {mode_str} ===", flush=True)
    t0 = time.time()

    if jobs == 1:
        results = run_shard(
            shard_id=1,
            total_shards=1,
            shard_tests=selected,
            repo_root=repo_root,
            timeout=args.timeout,
            use_temp_workspace=True,
            idx_map=idx_map,
            total_tests=len(selected),
        )
    else:
        import threading
        from concurrent.futures import ThreadPoolExecutor

        print_lock = threading.Lock()
        shards = [[] for _ in range(jobs)]
        for i, tc in enumerate(selected):
            shards[i % jobs].append(tc)

        results = {}
        with ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = [
                pool.submit(
                    run_shard,
                    shard_id=w_idx + 1,
                    total_shards=jobs,
                    shard_tests=shard,
                    repo_root=repo_root,
                    timeout=args.timeout,
                    use_temp_workspace=True,
                    idx_map=idx_map,
                    total_tests=len(selected),
                    print_lock=print_lock,
                )
                for w_idx, shard in enumerate(shards)
                if shard
            ]
            for fut in futures:
                results.update(fut.result())

    elapsed = time.time() - t0

    passed_count = 0
    failed_count = 0

    for tc in selected:
        res = results[tc.name]
        if res["passed"]:
            passed_count += 1
            if args.verbose:
                print(f"\n--- Output for {tc.name} ---")
                for line in res["output"].splitlines():
                    print(f"  | {line}")
        else:
            failed_count += 1
            print(f"\n  [FAIL] {tc.name} -> {res['reason']}")
            for line in res["output"].splitlines()[-25:]:
                print(f"         | {line}")

    print(
        f"=== Summary: {passed_count}/{len(selected)} passed, {failed_count} failed in {elapsed:.2f}s ===",
        flush=True,
    )
    return 0 if failed_count == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
