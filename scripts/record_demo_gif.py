#!/usr/bin/env python3
"""Record a live SIX guest session via PTY and render to docs/six_demo.gif (and .mp4).

Captures:
  1. Live boot sequence + 3D rotating SIX boot splash + login
  2. lsblk -f (multi-tier storage: ext4, NTFS-3G, dm-linear, dm-crypt, dm-verity EROFS, NVMe, UFS)
  3. service list (Android Binder services)
  4. Interactive Android Desktop Files UI (/bin/files) with Scoped Storage EXIF redaction + live USB hotplug
  5. Android Opportunistic Suspend / Deep Sleep (power sleep)
"""

import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import termios
import time
from PIL import Image, ImageDraw, ImageFont

ROWS = 28
COLS = 96

# Standard 16-color dark terminal palette (TokyoNight / OneDark inspired)
PALETTE_16 = {
    0: (22, 24, 33),       # Black
    1: (247, 118, 142),    # Red
    2: (158, 206, 106),    # Green
    3: (224, 175, 104),    # Yellow
    4: (122, 162, 247),    # Blue
    5: (187, 154, 247),    # Magenta
    6: (125, 207, 255),    # Cyan
    7: (192, 202, 245),    # White (default fg)
    8: (86, 95, 137),      # Bright Black (Gray)
    9: (255, 140, 160),    # Bright Red
    10: (180, 230, 120),   # Bright Green
    11: (245, 200, 120),   # Bright Yellow
    12: (145, 185, 255),   # Bright Blue
    13: (210, 180, 255),   # Bright Magenta
    14: (155, 225, 255),   # Bright Cyan
    15: (255, 255, 255),   # Bright White
}

DEFAULT_FG = 7
DEFAULT_BG = 0

ACS_MAP = {
    "q": "─",
    "x": "│",
    "l": "┌",
    "k": "┐",
    "m": "└",
    "j": "┘",
    "t": "├",
    "u": "┤",
    "w": "┬",
    "v": "┴",
    "n": "┼",
    "a": "▒",
}


class VT100Screen:
    def __init__(self, rows=ROWS, cols=COLS):
        self.rows = rows
        self.cols = cols
        self.reset()

    def reset(self):
        self.grid = [
            [[" ", DEFAULT_FG, DEFAULT_BG, False, False] for _ in range(self.cols)]
            for _ in range(self.rows)
        ]
        self.cr = 0
        self.cc = 0
        self.fg = DEFAULT_FG
        self.bg = DEFAULT_BG
        self.bold = False
        self.rev = False
        self.acs = False
        self.esc_state = 0
        self.esc_buf = ""

    def snapshot(self):
        return (
            self.cr,
            self.cc,
            tuple(
                tuple((c[0], c[1], c[2], c[3], c[4]) for c in row)
                for row in self.grid
            ),
        )

    def _scroll_up(self):
        self.grid.pop(0)
        self.grid.append(
            [[" ", DEFAULT_FG, DEFAULT_BG, False, False] for _ in range(self.cols)]
        )

    def _put_char(self, ch):
        if self.acs and ch in ACS_MAP:
            ch = ACS_MAP[ch]
        if self.cc >= self.cols:
            self.cc = 0
            self.cr += 1
            if self.cr >= self.rows:
                self.cr = self.rows - 1
                self._scroll_up()
        self.grid[self.cr][self.cc] = [ch, self.fg, self.bg, self.bold, self.rev]
        self.cc += 1

    def feed(self, data: bytes):
        text = data.decode("latin-1", errors="replace")
        for ch in text:
            if self.esc_state == 0:
                if ch == "\x1b":
                    self.esc_state = 1
                    self.esc_buf = ""
                elif ch == "\r":
                    self.cc = 0
                elif ch == "\n":
                    self.cr += 1
                    if self.cr >= self.rows:
                        self.cr = self.rows - 1
                        self._scroll_up()
                elif ch == "\b":
                    if self.cc > 0:
                        self.cc -= 1
                elif ch == "\t":
                    next_tab = ((self.cc // 8) + 1) * 8
                    while self.cc < min(next_tab, self.cols):
                        self._put_char(" ")
                elif ch == "\x0e":
                    self.acs = True
                elif ch == "\x0f":
                    self.acs = False
                elif ord(ch) >= 32:
                    self._put_char(ch)
            elif self.esc_state == 1:
                if ch == "[":
                    self.esc_state = 2
                    self.esc_buf = ""
                elif ch == "(":
                    self.esc_state = 3
                else:
                    self.esc_state = 0
            elif self.esc_state == 3:
                if ch == "0":
                    self.acs = True
                elif ch in ("B", "A"):
                    self.acs = False
                self.esc_state = 0
            elif self.esc_state == 2:
                if ("A" <= ch <= "Z") or ("a" <= ch <= "z"):
                    self._handle_csi(self.esc_buf, ch)
                    self.esc_state = 0
                else:
                    self.esc_buf += ch

    def _handle_csi(self, params_str, cmd):
        clean = params_str.lstrip("?")
        parts = [int(x) if x.isdigit() else 0 for x in clean.split(";")] if clean else []

        if cmd in ("H", "f"):
            r = (parts[0] - 1) if len(parts) >= 1 and parts[0] > 0 else 0
            c = (parts[1] - 1) if len(parts) >= 2 and parts[1] > 0 else 0
            self.cr = max(0, min(self.rows - 1, r))
            self.cc = max(0, min(self.cols - 1, c))
        elif cmd == "A":
            n = parts[0] if parts and parts[0] > 0 else 1
            self.cr = max(0, self.cr - n)
        elif cmd == "B":
            n = parts[0] if parts and parts[0] > 0 else 1
            self.cr = min(self.rows - 1, self.cr + n)
        elif cmd == "C":
            n = parts[0] if parts and parts[0] > 0 else 1
            self.cc = min(self.cols - 1, self.cc + n)
        elif cmd == "D":
            n = parts[0] if parts and parts[0] > 0 else 1
            self.cc = max(0, self.cc - n)
        elif cmd == "J":
            mode = parts[0] if parts else 0
            if mode == 2:
                for r in range(self.rows):
                    for c in range(self.cols):
                        self.grid[r][c] = [" ", DEFAULT_FG, DEFAULT_BG, False, False]
                self.cr = 0
                self.cc = 0
            elif mode == 0:
                for c in range(self.cc, self.cols):
                    self.grid[self.cr][c] = [" ", DEFAULT_FG, DEFAULT_BG, False, False]
                for r in range(self.cr + 1, self.rows):
                    for c in range(self.cols):
                        self.grid[r][c] = [" ", DEFAULT_FG, DEFAULT_BG, False, False]
        elif cmd == "K":
            mode = parts[0] if parts else 0
            if mode == 0:
                for c in range(self.cc, self.cols):
                    self.grid[self.cr][c] = [" ", self.fg, self.bg, self.bold, self.rev]
            elif mode == 2:
                for c in range(self.cols):
                    self.grid[self.cr][c] = [" ", self.fg, self.bg, self.bold, self.rev]
        elif cmd == "m":
            if not parts:
                parts = [0]
            for p in parts:
                if p == 0:
                    self.fg = DEFAULT_FG
                    self.bg = DEFAULT_BG
                    self.bold = False
                    self.rev = False
                elif p == 1:
                    self.bold = True
                elif p == 22:
                    self.bold = False
                elif p == 7:
                    self.rev = True
                elif p == 27:
                    self.rev = False
                elif 30 <= p <= 37:
                    self.fg = p - 30
                elif p == 39:
                    self.fg = DEFAULT_FG
                elif 40 <= p <= 47:
                    self.bg = p - 40
                elif p == 49:
                    self.bg = DEFAULT_BG
                elif 90 <= p <= 97:
                    self.fg = (p - 90) + 8
                elif 100 <= p <= 107:
                    self.bg = (p - 100) + 8


class TerminalRenderer:
    def __init__(self, rows=ROWS, cols=COLS):
        self.rows = rows
        self.cols = cols
        self.font_reg = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf", 14
        )
        self.font_bold = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 14
        )
        self.title_font = ImageFont.truetype(
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 13
        )
        self.cw = 9
        self.ch = 19
        self.pad_x = 16
        self.pad_y = 14
        self.title_h = 34
        self.width = self.cols * self.cw + self.pad_x * 2
        self.height = self.title_h + self.rows * self.ch + self.pad_y * 2

    def render(self, snap):
        cr, cc, grid = snap
        img = Image.new("RGB", (self.width, self.height), PALETTE_16[DEFAULT_BG])
        draw = ImageDraw.Draw(img)

        # Title bar
        draw.rectangle([0, 0, self.width, self.title_h], fill=(31, 35, 53))
        draw.line([0, self.title_h, self.width, self.title_h], fill=(59, 66, 97), width=1)
        # Window traffic light dots
        draw.ellipse([14, 11, 26, 23], fill=(247, 118, 142))
        draw.ellipse([34, 11, 46, 23], fill=(224, 175, 104))
        draw.ellipse([54, 11, 66, 23], fill=(158, 206, 106))
        title = "SIX 1.0 — Modern Linux & Android Subsystems on Linux 2.0.11"
        draw.text((self.width // 2 - 210, 9), title, font=self.title_font, fill=(169, 177, 214))

        ox = self.pad_x
        oy = self.title_h + self.pad_y

        for r in range(self.rows):
            y = oy + r * self.ch
            row = grid[r]
            for c in range(self.cols):
                ch, fg_idx, bg_idx, is_bold, is_rev = row[c]
                if is_bold and fg_idx < 8:
                    fg_color = PALETTE_16[fg_idx + 8]
                else:
                    fg_color = PALETTE_16.get(fg_idx, PALETTE_16[DEFAULT_FG])
                bg_color = PALETTE_16.get(bg_idx, PALETTE_16[DEFAULT_BG])

                if is_rev:
                    fg_color, bg_color = bg_color, fg_color

                x = ox + c * self.cw
                if bg_color != PALETTE_16[DEFAULT_BG]:
                    draw.rectangle([x, y, x + self.cw, y + self.ch], fill=bg_color)
                if ch != " ":
                    font = self.font_bold if is_bold else self.font_reg
                    draw.text((x, y + 1), ch, font=font, fill=fg_color)

        # Draw block cursor if within bounds
        if 0 <= cr < self.rows and 0 <= cc < self.cols:
            cx = ox + cc * self.cw
            cy = oy + cr * self.ch
            draw.rectangle(
                [cx, cy + self.ch - 3, cx + self.cw, cy + self.ch],
                fill=(125, 207, 255),
            )

        return img


def record_session(repo_root):
    six_bin = os.path.join(repo_root, "six")
    pid, mfd = pty.fork()
    if pid == 0:
        os.chdir(repo_root)
        winsz = struct.pack("HHHH", ROWS, COLS, 0, 0)
        try:
            fcntl.ioctl(0, termios.TIOCSWINSZ, winsz)
        except Exception:
            pass
        env = dict(os.environ)
        env["TERM"] = "vt100"
        os.execvpe(six_bin, [six_bin], env)
        os._exit(1)

    winsz = struct.pack("HHHH", ROWS, COLS, 0, 0)
    try:
        fcntl.ioctl(mfd, termios.TIOCSWINSZ, winsz)
    except Exception:
        pass

    screen = VT100Screen(ROWS, COLS)
    frames = []  # list of (timestamp, snapshot)
    raw_text = ""
    recording = True

    def pump(duration):
        nonlocal raw_text
        deadline = time.time() + duration
        last_snap_t = 0.0
        dirty = False
        while time.time() < deadline:
            rem = max(0.01, min(0.04, deadline - time.time()))
            r, _, _ = select.select([mfd], [], [], rem)
            if mfd in r:
                try:
                    chunk = os.read(mfd, 4096)
                except OSError:
                    break
                if not chunk:
                    break
                raw_text += chunk.decode("latin-1", errors="replace")
                screen.feed(chunk)
                dirty = True
                now = time.time()
                if recording and (now - last_snap_t >= 0.05):
                    frames.append((now, screen.snapshot()))
                    last_snap_t = now
                    dirty = False
            else:
                if recording and dirty:
                    now = time.time()
                    frames.append((now, screen.snapshot()))
                    last_snap_t = now
                    dirty = False
        if recording and dirty:
            frames.append((time.time(), screen.snapshot()))

    def wait_until(substr, max_wait=12.0):
        deadline = time.time() + max_wait
        while time.time() < deadline:
            if substr in raw_text:
                return True
            pump(0.06)
        return False

    def type_cmd(cmd_str, char_delay=0.035):
        for ch in cmd_str:
            os.write(mfd, ch.encode("latin-1"))
            pump(char_delay)
        os.write(mfd, b"\r")

    # 1. Wait for login prompt (captures boot + 3D rotating SIX splash)
    wait_until("login:", max_wait=15.0)
    pump(0.5)
    type_cmd("root", char_delay=0.06)
    wait_until("# ", max_wait=8.0)
    pump(0.8)

    # 2. Show multi-tier block devices (ext4, NTFS, dm-linear, dm-crypt, EROFS verity, NVMe, UFS)
    raw_text = ""
    type_cmd("lsblk -f")
    wait_until("# ", max_wait=8.0)
    pump(1.6)

    # 3. Show Android Binder services
    raw_text = ""
    type_cmd("service list")
    wait_until("# ", max_wait=8.0)
    pump(1.4)

    # 4. Launch Android Desktop Files app with live USB hotplug
    raw_text = ""
    type_cmd("sh -c 'sleep 1.2; usbctl plug ext4 >/dev/null 2>&1' & su six -c 'files /sdcard/DCIM/Camera'")
    pump(2.6)  # Wait for Files UI + live USB hotplug notification

    # Switch to left sidebar and select Images library to show Scoped Storage EXIF GPS redaction
    os.write(mfd, b"\t")
    pump(0.6)
    os.write(mfd, b"\x1b[A\x1b[A\x1b[A\x1b[A\r")
    pump(1.8)
    os.write(mfd, b"q")
    wait_until("# ", max_wait=8.0)
    pump(0.6)

    # 5. Demonstrate Android Opportunistic Suspend / Deep Sleep
    raw_text = ""
    type_cmd("power sleep 400ms")
    wait_until("# ", max_wait=8.0)
    pump(1.8)

    # Stop recording before clean shutdown
    recording = False
    os.write(mfd, b"usbctl unplug >/dev/null 2>&1; halt\r")
    pump(0.8)
    try:
        os.close(mfd)
    except OSError:
        pass
    try:
        os.waitpid(pid, 0)
    except OSError:
        pass

    return frames


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    docs_dir = os.path.join(repo_root, "docs")
    os.makedirs(docs_dir, exist_ok=True)

    print("[1/3] Recording live SIX boot, storage, Binder, Files UI & Suspend session...")
    raw_frames = record_session(repo_root)
    print(f"      Captured {len(raw_frames)} raw terminal state snapshots.")

    # Deduplicate consecutive identical snapshots and compute frame durations (ms)
    dedup = []
    for t, snap in raw_frames:
        if not dedup or dedup[-1][1] != snap:
            dedup.append([t, snap])

    renderer = TerminalRenderer(ROWS, COLS)
    pil_frames = []
    durations = []

    print(f"[2/3] Rendering {len(dedup)} unique terminal frames ({renderer.width}x{renderer.height})...")
    for i in range(len(dedup)):
        t_cur, snap = dedup[i]
        if i + 1 < len(dedup):
            dt_ms = int((dedup[i + 1][0] - t_cur) * 1000)
        else:
            dt_ms = 2200
        dt_ms = max(60, min(1800, dt_ms))
        img = renderer.render(snap)
        pil_frames.append(img.convert("P", palette=Image.Palette.ADAPTIVE, colors=64))
        durations.append(dt_ms)

    gif_path = os.path.join(docs_dir, "six_demo.gif")
    print(f"[3/3] Saving animated GIF to {gif_path}...")
    pil_frames[0].save(
        gif_path,
        save_all=True,
        append_images=pil_frames[1:],
        duration=durations,
        loop=0,
        optimize=True,
    )
    size_kb = os.path.getsize(gif_path) / 1024.0
    print(f"      Done! {gif_path} ({size_kb:.1f} KB, {len(pil_frames)} frames)")


if __name__ == "__main__":
    main()
