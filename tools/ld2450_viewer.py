#!/usr/bin/env python3
"""
LD2450 live target viewer for the esp32c6-mmwave-lux project.

Reads the "$LD2450,..." telemetry lines the firmware prints on its console
(mixed with normal ESP-IDF log output), and draws a top-down view of the
sensor's 120-degree field of view with the three tracked targets, their
motion trails, and the three region-filter rectangles.

Line format (24 comma-separated signed ints, newline terminated):

    $LD2450,x1,y1,res1,spd1,rx1a,ry1a,rx1b,ry1b,
            x2,y2,res2,spd2,rx2a,ry2a,rx2b,ry2b,
            x3,y3,res3,spd3,rx3a,ry3a,rx3b,ry3b

    x/y      target position, mm  (+x right, +y away from the sensor)
    res      distance resolution, mm (range gate size; 0 = empty slot)
    spd      radial speed, cm/s  (+ moving away, - approaching)
    rx/ry    region filter corners, mm (two opposite corners)

Dependencies:  pip install pygame pyserial

Usage:
    python3 ld2450_viewer.py                      # /dev/ttyACM0 @ 115200
    python3 ld2450_viewer.py --port /dev/ttyACM1 --trail 5
    python3 ld2450_viewer.py --echo               # also print ESP log lines
    python3 ld2450_viewer.py --demo               # synthetic targets, no board

Keys:
    g  grid on/off        t  trails on/off       r  resolution rings on/off
    e  region edit mode   1/2/3  select region   p  print regions as C code
    space  pause          q / Esc  quit (Esc leaves edit mode first)

Region edit mode:
    drag a corner handle to resize, drag inside the box to move it, or drag
    on empty space to draw a region that is currently all zeros. Edited
    regions are frozen (incoming values are ignored) until you press Esc.
"""

import argparse
import collections
import math
import sys
import threading
import time

try:
    import pygame
except ImportError:  # pragma: no cover
    sys.exit("pygame is required:  pip install pygame")

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    serial = None

# --------------------------------------------------------------------------
# Protocol / geometry constants
# --------------------------------------------------------------------------

PREFIX = "$LD2450,"
N_FIELDS = 24
N_TARGETS = 3

MAX_RANGE_MM = 6000          # LD2450 max tracking distance
HALF_FOV_DEG = 60            # azimuth +/-60 deg  (pitch +/-35 is not shown)
SPEED_FULL_BRIGHT = 100.0    # |cm/s| at which a trail segment is fully bright

TARGET_COLORS = [
    (70, 230, 100),          # target 1 green
    (240, 240, 240),         # target 2 white
    (240, 70, 70),           # target 3 red
]

BG = (0, 0, 0)
CONE_FILL = (38, 38, 38)
GRID_MINOR = (58, 58, 58)
GRID_MAJOR = (92, 92, 92)
GRID_AXIS = (130, 130, 130)
TEXT = (200, 200, 200)
TEXT_DIM = (120, 120, 120)
PANEL_BG = (16, 16, 16)
WARN = (255, 170, 40)

Target = collections.namedtuple("Target", "x y res speed")
Region = collections.namedtuple("Region", "x1 y1 x2 y2")


class Frame:
    __slots__ = ("targets", "regions", "t")

    def __init__(self, targets, regions, t):
        self.targets = targets
        self.regions = regions
        self.t = t


# --------------------------------------------------------------------------
# Parsing
# --------------------------------------------------------------------------

def parse_line(line, t=None):
    """Parse one telemetry line. Returns a Frame or None if not a valid line."""
    if not line.startswith(PREFIX):
        return None
    parts = line[len(PREFIX):].strip().split(",")
    if len(parts) != N_FIELDS:
        return None
    try:
        v = [int(p) for p in parts]
    except ValueError:
        return None
    targets, regions = [], []
    for i in range(N_TARGETS):
        b = i * 8
        targets.append(Target(v[b], v[b + 1], v[b + 2], v[b + 3]))
        regions.append(Region(v[b + 4], v[b + 5], v[b + 6], v[b + 7]))
    return Frame(targets, regions, time.monotonic() if t is None else t)


def target_present(tg):
    return tg.x != 0 or tg.y != 0


def region_present(rg):
    return any((rg.x1, rg.y1, rg.x2, rg.y2))


def normalize_region(rg):
    return Region(min(rg.x1, rg.x2), min(rg.y1, rg.y2),
                  max(rg.x1, rg.x2), max(rg.y1, rg.y2))


# --------------------------------------------------------------------------
# Serial reader thread (auto-reconnect)
# --------------------------------------------------------------------------

class SerialReader(threading.Thread):
    def __init__(self, port, baud, echo=False):
        super().__init__(daemon=True)
        self.port, self.baud, self.echo = port, baud, echo
        self.latest = None
        self.seq = 0
        self.connected = False
        self.error = ""
        self.arrivals = collections.deque(maxlen=50)
        self._stop = threading.Event()

    def stop(self):
        self._stop.set()

    def run(self):
        if serial is None:
            self.error = "pyserial not installed (pip install pyserial)"
            return
        while not self._stop.is_set():
            try:
                with serial.Serial(self.port, self.baud, timeout=1) as ser:
                    self.connected, self.error = True, ""
                    ser.reset_input_buffer()
                    ser.readline()  # drop the partial line we connected into
                    while not self._stop.is_set():
                        raw = ser.readline()
                        if not raw:
                            continue
                        line = raw.decode("utf-8", errors="ignore").strip()
                        frame = parse_line(line)
                        if frame is not None:
                            self.latest = frame
                            self.seq += 1
                            self.arrivals.append(frame.t)
                        elif self.echo and line:
                            print(line)
            except (OSError, ValueError) as exc:  # SerialException is an OSError
                self.connected = False
                self.error = str(exc).splitlines()[0][:70]
                time.sleep(1.0)

    def rate_hz(self):
        if len(self.arrivals) < 2:
            return 0.0
        span = self.arrivals[-1] - self.arrivals[0]
        return (len(self.arrivals) - 1) / span if span > 0 else 0.0


class DemoReader(threading.Thread):
    """Synthetic frames at 10 Hz so the viewer can be tried without a board."""

    def __init__(self):
        super().__init__(daemon=True)
        self.latest = None
        self.seq = 0
        self.connected = True
        self.error = ""
        self.arrivals = collections.deque(maxlen=50)
        self.port = "demo"
        self._stop = threading.Event()

    def stop(self):
        self._stop.set()

    def run(self):
        t0 = time.monotonic()
        while not self._stop.is_set():
            t = time.monotonic() - t0
            # target 1: slow circle at ~2.5 m; target 2: walking in/out;
            # target 3: appears for 6 s every 10 s, moving fast across
            x1, y1 = 1500 * math.cos(t * 0.5), 2500 + 1000 * math.sin(t * 0.5)
            s1 = 30 * math.cos(t * 0.5)
            y2 = 3500 + 2000 * math.sin(t * 0.25)
            x2, s2 = -1800 + 300 * math.sin(t), 50 * math.cos(t * 0.25)
            if (t % 10) < 6:
                x3, y3, s3 = -3000 + 1000 * (t % 10), 1200, -90 + 30 * (t % 10)
            else:
                x3 = y3 = s3 = 0
            vals = [int(x1), int(y1), 360, int(s1), -2000, 1000, 0, 4000,
                    int(x2), int(y2), 360, int(s2), 500, 2500, 2500, 5000,
                    int(x3), int(y3), 360 if x3 else 0, int(s3), 0, 0, 0, 0]
            frame = parse_line(PREFIX + ",".join(map(str, vals)))
            self.latest, self.seq = frame, self.seq + 1
            self.arrivals.append(frame.t)
            time.sleep(0.1)

    rate_hz = SerialReader.rate_hz


# --------------------------------------------------------------------------
# Viewer
# --------------------------------------------------------------------------

class Viewer:
    PANEL_H = 118
    STATUS_H = 46
    MARGIN = 44
    HANDLE_PX = 9

    def __init__(self, reader, trail_s, fps):
        self.reader = reader
        self.trail_s = trail_s
        self.fps = fps

        pygame.init()
        pygame.display.set_caption("LD2450 target viewer")
        self.screen = pygame.display.set_mode((1000, 800), pygame.RESIZABLE)
        self.font = pygame.font.SysFont("monospace", 15)
        self.font_small = pygame.font.SysFont("monospace", 12)
        self.font_big = pygame.font.SysFont("monospace", 18, bold=True)
        self.clock = pygame.time.Clock()

        self.frame = None
        self.last_seq = -1
        self.trails = [collections.deque() for _ in range(N_TARGETS)]

        self.show_grid = True
        self.show_trails = True
        self.show_rings = True
        self.paused = False
        self.pause_started = 0.0
        self.pause_total = 0.0

        # region editing
        self.edit_mode = False
        self.sel_region = 0
        self.edited = {}           # index -> Region (frozen, in mm)
        self.drag = None           # ("corner", idx, which) | ("move", idx, dx, dy) | ("new", idx, x0, y0)

        self._layout()

    # ---- time (virtual clock that stops while paused) ----
    def now(self):
        t = time.monotonic() - self.pause_total
        if self.paused:
            t -= time.monotonic() - self.pause_started
        return t

    # ---- geometry ----
    def _layout(self):
        w, h = self.screen.get_size()
        plot_top = self.PANEL_H
        plot_bottom = h - self.STATUS_H
        avail_w = w - 2 * self.MARGIN
        avail_h = plot_bottom - plot_top - 2 * self.MARGIN
        cone_w_mm = 2 * MAX_RANGE_MM * math.sin(math.radians(HALF_FOV_DEG))
        self.scale = min(avail_w / cone_w_mm, avail_h / MAX_RANGE_MM)
        self.origin = (w / 2, plot_bottom - self.MARGIN)   # sensor position, px
        self.plot_rect = pygame.Rect(0, plot_top, w, plot_bottom - plot_top)

    def w2s(self, x_mm, y_mm):
        ox, oy = self.origin
        return (ox + x_mm * self.scale, oy - y_mm * self.scale)

    def s2w(self, sx, sy):
        ox, oy = self.origin
        return ((sx - ox) / self.scale, (oy - sy) / self.scale)

    # ---- data ----
    def ingest(self):
        rd = self.reader
        if rd.seq == self.last_seq or rd.latest is None or self.paused:
            return
        self.last_seq = rd.seq
        self.frame = rd.latest
        now = self.now()
        for i, tg in enumerate(self.frame.targets):
            if target_present(tg):
                self.trails[i].append((tg.x, tg.y, now, tg.speed))
        self.prune_trails(now)

    def prune_trails(self, now):
        for tr in self.trails:
            while tr and now - tr[0][2] > self.trail_s:
                tr.popleft()

    def current_region(self, i):
        if i in self.edited:
            return self.edited[i]
        if self.frame is None:
            return Region(0, 0, 0, 0)
        return self.frame.regions[i]

    # ---- drawing ----
    def draw(self):
        self.screen.fill(BG)
        self.draw_cone()
        self.draw_regions()
        if self.show_trails:
            self.draw_trails()
        self.draw_targets()
        self.draw_panel()
        self.draw_status()
        pygame.display.flip()

    def arc_points(self, r_mm, a0=-HALF_FOV_DEG, a1=HALF_FOV_DEG, step=2):
        pts = []
        a = a0
        while a <= a1 + 1e-9:
            rad = math.radians(a)
            pts.append(self.w2s(r_mm * math.sin(rad), r_mm * math.cos(rad)))
            a += step
        return pts

    def draw_cone(self):
        apex = self.w2s(0, 0)
        rim = self.arc_points(MAX_RANGE_MM)
        pygame.draw.polygon(self.screen, CONE_FILL, [apex] + rim)
        if self.show_grid:
            # range arcs every 0.5 m (minor) / 1 m (major), clipped to the wedge
            for r in range(500, MAX_RANGE_MM + 1, 500):
                major = r % 1000 == 0
                pts = self.arc_points(r)
                pygame.draw.lines(self.screen, GRID_MAJOR if major else GRID_MINOR,
                                  False, pts, 1)
                if major:
                    lbl = self.font_small.render(f"{r // 1000} m", True, TEXT_DIM)
                    px, py = self.w2s(0, r)
                    self.screen.blit(lbl, (px + 4, py - 14))
            # radial lines every 15 deg, 0 deg brighter, labels past the rim
            for a in range(-HALF_FOV_DEG, HALF_FOV_DEG + 1, 15):
                rad = math.radians(a)
                end = self.w2s(MAX_RANGE_MM * math.sin(rad), MAX_RANGE_MM * math.cos(rad))
                col = GRID_AXIS if a == 0 else GRID_MAJOR
                pygame.draw.line(self.screen, col, apex, end, 1)
                lr = MAX_RANGE_MM + 260
                lx, ly = self.w2s(lr * math.sin(rad), lr * math.cos(rad))
                lbl = self.font_small.render(f"{a:+d}°" if a else "0°", True, TEXT_DIM)
                self.screen.blit(lbl, lbl.get_rect(center=(lx, ly)))
        pygame.draw.lines(self.screen, GRID_AXIS, False, rim, 1)
        # sensor marker
        pygame.draw.circle(self.screen, TEXT, apex, 5)
        pygame.draw.circle(self.screen, BG, apex, 2)

    def draw_regions(self):
        for i in range(N_TARGETS):
            rg = self.current_region(i)
            if not region_present(rg):
                continue
            rg = normalize_region(rg)
            col = TARGET_COLORS[i]
            x0, y0 = self.w2s(rg.x1, rg.y2)   # top-left on screen = min x, max y
            x1, y1 = self.w2s(rg.x2, rg.y1)
            rect = pygame.Rect(x0, y0, x1 - x0, y1 - y0)
            selected = self.edit_mode and i == self.sel_region
            pygame.draw.rect(self.screen, col, rect, 3 if selected else 2)
            tag = self.font_small.render(f"R{i + 1}" + (" *" if i in self.edited else ""),
                                         True, col)
            self.screen.blit(tag, (rect.left + 4, rect.top + 3))
            if selected:
                for hx, hy in self.region_handles(rg):
                    pygame.draw.rect(self.screen, col,
                                     (hx - self.HANDLE_PX / 2, hy - self.HANDLE_PX / 2,
                                      self.HANDLE_PX, self.HANDLE_PX))

    def region_handles(self, rg):
        """Screen positions of the 4 corner handles, index = corner id 0..3."""
        return [self.w2s(rg.x1, rg.y1), self.w2s(rg.x2, rg.y1),
                self.w2s(rg.x2, rg.y2), self.w2s(rg.x1, rg.y2)]

    def draw_trails(self):
        now = self.now()
        self.prune_trails(now)
        surf = pygame.Surface(self.screen.get_size(), pygame.SRCALPHA)
        for i, tr in enumerate(self.trails):
            if len(tr) < 2:
                continue
            base = TARGET_COLORS[i]
            pts = list(tr)
            for (xa, ya, _, _), (xb, yb, tb, sb) in zip(pts, pts[1:]):
                age = now - tb
                alpha = max(0.0, 1.0 - age / self.trail_s)
                bright = 0.30 + 0.70 * min(abs(sb) / SPEED_FULL_BRIGHT, 1.0)
                col = (int(base[0] * bright), int(base[1] * bright),
                       int(base[2] * bright), int(255 * alpha))
                pygame.draw.line(surf, col, self.w2s(xa, ya), self.w2s(xb, yb), 3)
        self.screen.blit(surf, (0, 0))

    def draw_targets(self):
        if self.frame is None:
            return
        for i, tg in enumerate(self.frame.targets):
            if not target_present(tg):
                continue
            col = TARGET_COLORS[i]
            p = self.w2s(tg.x, tg.y)
            if self.show_rings and tg.res > 0:
                r = max(2, int(tg.res / 2 * self.scale))
                ring = pygame.Surface((2 * r + 2, 2 * r + 2), pygame.SRCALPHA)
                pygame.draw.circle(ring, (*col, 70), (r + 1, r + 1), r, 1)
                self.screen.blit(ring, (p[0] - r - 1, p[1] - r - 1))
            pygame.draw.circle(self.screen, col, p, 8)
            pygame.draw.circle(self.screen, BG, p, 8, 1)
            lbl = self.font_small.render(str(i + 1), True, BG)
            self.screen.blit(lbl, lbl.get_rect(center=p))

    def draw_panel(self):
        w = self.screen.get_width()
        pygame.draw.rect(self.screen, PANEL_BG, (0, 0, w, self.PANEL_H))
        gap, pad = 12, 10
        box_w = (w - gap * (N_TARGETS + 1)) / N_TARGETS
        for i in range(N_TARGETS):
            col = TARGET_COLORS[i]
            rect = pygame.Rect(gap + i * (box_w + gap), pad, box_w, self.PANEL_H - 2 * pad)
            pygame.draw.rect(self.screen, col, rect, 2, border_radius=4)
            title = self.font_big.render(f"TARGET {i + 1}", True, col)
            self.screen.blit(title, (rect.left + 10, rect.top + 6))
            tg = self.frame.targets[i] if self.frame else None
            if tg is None or not target_present(tg):
                lines = ["—  no target", "", ""]
            else:
                dist = math.hypot(tg.x, tg.y) / 1000.0
                ang = math.degrees(math.atan2(tg.x, tg.y))
                if tg.speed < 0:
                    motion = "◄ approaching"
                elif tg.speed > 0:
                    motion = "► receding"
                else:
                    motion = "  still"
                lines = [
                    f"dist {dist:5.2f} m    angle {ang:+6.1f}°",
                    f"speed {tg.speed:+4d} cm/s  {motion}",
                    f"x {tg.x:+5d}  y {tg.y:+5d} mm   res {tg.res} mm",
                ]
            for k, txt in enumerate(lines):
                s = self.font.render(txt, True, TEXT if k < 2 else TEXT_DIM)
                self.screen.blit(s, (rect.left + 10, rect.top + 32 + k * 20))

    def draw_status(self):
        w, h = self.screen.get_size()
        top = h - self.STATUS_H
        pygame.draw.rect(self.screen, PANEL_BG, (0, top, w, self.STATUS_H))
        rd = self.reader
        if rd.connected:
            link = f"{rd.port}  {rd.rate_hz():4.1f} Hz"
            link_col = TEXT
        else:
            link = f"{rd.port}  DISCONNECTED  {rd.error}"
            link_col = WARN
        flags = (f"[g]rid {'on ' if self.show_grid else 'off'}  "
                 f"[t]rails {'on ' if self.show_trails else 'off'} ({self.trail_s:.0f}s)  "
                 f"[r]ings {'on ' if self.show_rings else 'off'}  "
                 f"[e]dit {'R' + str(self.sel_region + 1) if self.edit_mode else 'off'}  "
                 f"[p]rint  [space] {'PAUSED' if self.paused else 'run'}   "
                 f"{self.clock.get_fps():3.0f} fps")
        self.screen.blit(self.font.render(link, True, link_col), (12, top + 6))
        self.screen.blit(self.font_small.render(flags, True, TEXT_DIM), (12, top + 27))
        if self.edit_mode:
            hint = "EDIT: 1/2/3 select · drag corner = resize · drag inside = move · drag empty = new · p print · Esc revert"
            s = self.font_small.render(hint, True, WARN)
            self.screen.blit(s, (w - s.get_width() - 12, top + 6))

    # ---- region editing ----
    def print_regions(self):
        print("ld2450_filter_config_t cfg = {")
        print("    .filter_type = 1,  // 0 = off, 1 = detect only inside, 2 = ignore inside (set yourself)")
        for i in range(N_TARGETS):
            rg = normalize_region(self.current_region(i))
            n = i + 1
            print(f"    .x1_filter_{n} = {rg.x1:6d}, .y1_filter_{n} = {rg.y1:6d}, "
                  f".x2_filter_{n} = {rg.x2:6d}, .y2_filter_{n} = {rg.y2:6d},")
        print("};")
        sys.stdout.flush()

    def snap(self, v):
        v = int(round(v / 10.0) * 10)
        return max(-MAX_RANGE_MM, min(MAX_RANGE_MM, v))

    def mouse_down(self, pos):
        if not self.edit_mode or not self.plot_rect.collidepoint(pos):
            return
        i = self.sel_region
        rg = normalize_region(self.current_region(i))
        if region_present(rg):
            for k, (hx, hy) in enumerate(self.region_handles(rg)):
                if abs(pos[0] - hx) <= self.HANDLE_PX and abs(pos[1] - hy) <= self.HANDLE_PX:
                    self.edited[i] = rg
                    self.drag = ("corner", i, k)
                    return
            x0, y0 = self.w2s(rg.x1, rg.y2)
            x1, y1 = self.w2s(rg.x2, rg.y1)
            if pygame.Rect(x0, y0, x1 - x0, y1 - y0).collidepoint(pos):
                wx, wy = self.s2w(*pos)
                self.edited[i] = rg
                self.drag = ("move", i, wx - rg.x1, wy - rg.y1, rg.x2 - rg.x1, rg.y2 - rg.y1)
                return
        wx, wy = self.s2w(*pos)
        self.edited[i] = Region(self.snap(wx), self.snap(wy), self.snap(wx), self.snap(wy))
        self.drag = ("new", i, self.snap(wx), self.snap(wy))

    def mouse_move(self, pos):
        if self.drag is None:
            return
        kind, i = self.drag[0], self.drag[1]
        wx, wy = self.s2w(*pos)
        wx, wy = self.snap(wx), self.snap(wy)
        rg = self.edited[i]
        if kind == "corner":
            k = self.drag[2]
            # corners: 0=(x1,y1) 1=(x2,y1) 2=(x2,y2) 3=(x1,y2)
            x1, y1, x2, y2 = rg
            if k in (0, 3):
                x1 = wx
            else:
                x2 = wx
            if k in (0, 1):
                y1 = wy
            else:
                y2 = wy
            self.edited[i] = Region(x1, y1, x2, y2)
        elif kind == "move":
            _, _, offx, offy, w_mm, h_mm = self.drag
            nx1, ny1 = self.snap(wx - offx), self.snap(wy - offy)
            self.edited[i] = Region(nx1, ny1, nx1 + w_mm, ny1 + h_mm)
        elif kind == "new":
            _, _, x0, y0 = self.drag
            self.edited[i] = Region(x0, y0, wx, wy)

    def mouse_up(self):
        if self.drag is not None:
            i = self.drag[1]
            self.edited[i] = normalize_region(self.edited[i])
            self.drag = None

    # ---- main loop ----
    def run(self):
        running = True
        while running:
            for ev in pygame.event.get():
                if ev.type == pygame.QUIT:
                    running = False
                elif ev.type == pygame.VIDEORESIZE:
                    self.screen = pygame.display.set_mode(ev.size, pygame.RESIZABLE)
                    self._layout()
                elif ev.type == pygame.KEYDOWN:
                    running = self.key(ev.key)
                elif ev.type == pygame.MOUSEBUTTONDOWN and ev.button == 1:
                    self.mouse_down(ev.pos)
                elif ev.type == pygame.MOUSEMOTION:
                    self.mouse_move(ev.pos)
                elif ev.type == pygame.MOUSEBUTTONUP and ev.button == 1:
                    self.mouse_up()
            self.ingest()
            self.draw()
            self.clock.tick(self.fps)
        self.reader.stop()
        pygame.quit()

    def key(self, k):
        if k == pygame.K_q:
            return False
        if k == pygame.K_ESCAPE:
            if self.edit_mode:
                self.edit_mode, self.edited, self.drag = False, {}, None
                return True
            return False
        if k == pygame.K_g:
            self.show_grid = not self.show_grid
        elif k == pygame.K_t:
            self.show_trails = not self.show_trails
        elif k == pygame.K_r:
            self.show_rings = not self.show_rings
        elif k == pygame.K_e:
            self.edit_mode = not self.edit_mode
            if not self.edit_mode:
                self.drag = None
        elif k in (pygame.K_1, pygame.K_2, pygame.K_3):
            self.sel_region = k - pygame.K_1
        elif k == pygame.K_p:
            self.print_regions()
        elif k == pygame.K_SPACE:
            if self.paused:
                self.pause_total += time.monotonic() - self.pause_started
            else:
                self.pause_started = time.monotonic()
            self.paused = not self.paused
        return True


# --------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description="LD2450 live target viewer")
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--trail", type=float, default=3.0, help="trail fade time, seconds")
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--echo", action="store_true", help="print non-$LD2450 lines (ESP logs) to stdout")
    ap.add_argument("--demo", action="store_true", help="synthetic targets, no serial port")
    args = ap.parse_args(argv)

    if args.demo:
        reader = DemoReader()
    else:
        if serial is None:
            sys.exit("pyserial is required:  pip install pyserial   (or use --demo)")
        reader = SerialReader(args.port, args.baud, echo=args.echo)
    reader.start()
    Viewer(reader, args.trail, args.fps).run()


if __name__ == "__main__":
    main()
