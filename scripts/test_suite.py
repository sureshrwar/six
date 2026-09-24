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
        description="Android Binder IPC servicemanager, vold, and storaged (mount) registration",
        cmd=(
            "service list && "
            "service check vold && "
            "service check mount && "
            "sm list-disks"
        ),
        expected_substrings=[
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
        description="NTFS-3G in both vold FUSE/block fd mode (ag/41989403) and direct path mode + mkntfs/ntfsfix",
        cmd=(
            "usbctl plug ntfs && "
            "echo 'ntfs_vold_fd_mode_ok' > /mnt/media_rw/6A1B-8E42/vold_fd.txt && "
            "cat /mnt/media_rw/6A1B-8E42/vold_fd.txt && "
            "sm partition disk:8,0 ntfs && "
            "usbctl unplug && "
            "grep 'ForkExecvpAsyncAsUser' /tmp/vold.log && "
            "grep 'Reaping NTFS driver PID' /tmp/vold.log && "
            "mkdir -p /tmp/direct_ntfs && "
            "mkntfs -f -Q /dev/sda1 && "
            "ntfsfix /dev/sda1 && "
            "ntfs-3g /dev/sda1 /tmp/direct_ntfs && "
            "echo 'ntfs_direct_path_mode_ok' > /tmp/direct_ntfs/direct.txt && "
            "cat /tmp/direct_ntfs/direct.txt && "
            "umount /tmp/direct_ntfs && "
            "usbctl unplug"
        ),
        expected_substrings=[
            "ntfs_vold_fd_mode_ok",
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
            "usbctl unplug"
        ),
        expected_substrings=[
            "private:8,1",
            "mounted",
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
        description="CLI applications: Tiny BASIC interpreter, cowsay, fortune, cal, banner, dhrystone",
        cmd=(
            "echo '10 LET A = 6 * 7' > /tmp/test.bas && "
            "echo '20 PRINT \"BASIC_RESULT=\"; A' >> /tmp/test.bas && "
            "echo '30 END' >> /tmp/test.bas && "
            "basic /tmp/test.bas && "
            "rm /tmp/test.bas && "
            "cowsay 'regression_moo' && "
            "fortune && "
            "cal 9 2026 && "
            "banner SIX && "
            "dhrystone"
        ),
        expected_substrings=[
            "BASIC_RESULT=42",
            "regression_moo",
            "2026",
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


def parse_test_results(raw_output, selected_tests):
    results = {}
    for tc in selected_tests:
        begin_marker = f"__BEGIN:{tc.name}__"
        end_pattern = re.compile(rf"__END:{re.escape(tc.name)}:(\d+)__")

        b_idx = raw_output.find(begin_marker)
        if b_idx == -1:
            results[tc.name] = {
                "passed": False,
                "rc": -1,
                "output": "",
                "reason": "Test start marker not found (guest command timed out or aborted)",
            }
            continue

        sub = raw_output[b_idx + len(begin_marker) :]
        m = end_pattern.search(sub)
        if not m:
            results[tc.name] = {
                "passed": False,
                "rc": -1,
                "output": sub,
                "reason": "Test end marker not found",
            }
            continue

        rc = int(m.group(1))
        body = sub[: m.start()].strip()
        missing = [s for s in tc.expected_substrings if s not in body]
        passed = (rc == 0) and (len(missing) == 0)
        reason = ""
        if rc != 0:
            reason = f"Non-zero exit status: {rc}"
        elif missing:
            reason = f"Missing expected output: {missing}"

        results[tc.name] = {
            "passed": passed,
            "rc": rc,
            "output": body,
            "reason": reason,
        }
    return results


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

    print(f"=== Running {len(selected)} SIX Regression Test(s) in a single guest session ===")
    t0 = time.time()
    cmd_queue = build_guest_command_queue(selected)
    raw_out = run_guest_commands(cmd_queue, timeout=args.timeout)
    elapsed = time.time() - t0

    results = parse_test_results(raw_out, selected)
    passed_count = 0
    failed_count = 0

    for tc in selected:
        res = results[tc.name]
        if res["passed"]:
            passed_count += 1
            print(f"  [PASS] {tc.name:<34} ({tc.description})")
            if args.verbose:
                for line in res["output"].splitlines():
                    print(f"         | {line}")
        else:
            failed_count += 1
            print(f"  [FAIL] {tc.name:<34} -> {res['reason']}")
            for line in res["output"].splitlines()[-25:]:
                print(f"         | {line}")

    print(
        f"=== Summary: {passed_count}/{len(selected)} passed, {failed_count} failed in {elapsed:.2f}s ==="
    )
    return 0 if failed_count == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
