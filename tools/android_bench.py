#!/usr/bin/env python3
"""Benchmark runs of the port on an Android phone over adb, or on this PC, with
the same scenes, the same scripted play and the same per-frame metrics
(TSSM_PERF; src/SB/Core/pc/iPerf.h).

    python tools/android_bench.py phone --out DIR [--apk APK] [--run SPEC ...]
    python tools/android_bench.py pc --exe tssm.exe --out DIR [--backend gl3] [--run SPEC ...]
    python tools/android_bench.py summary CSV [--skip FRAMES]
    python tools/android_bench.py symbolize SAMPLES --lib libmain.so [--out FILE]
    python tools/android_bench.py scenes

A run SPEC is SCENE[@RATE][:SECONDS][+still][+profile=HZ][+label=NAME][+env=KEY:VALUE]..., e.g.
SC02@120:150, BB01@60:150+label=bb01_60, SC02@120:600+label=heat. RATE is the
frame-rate cap the run's config asks for ("display" when left out, which is
the phone's 120 Hz) and what the scripts' seconds are turned into frames with.
"+still" puts the player at the scene's first spot and holds them there
(TSSM_TEST_PLACE/TSSM_TEST_HOLD) instead of the scripted walk.

What a phone session does, in order (it touches nothing on the phone but the game):

  1. Waits for the phone (adb get-state), for the screen to be on and unlocked
     (it never tries to unlock it), and for the game to be closed: a game
     running that this script did not start is someone playing, and it waits.
  2. Backs up the app's private files (run-as tar) into --out, pulls the APK
     installed now, and records the sha1 of the user's config.ini, of every
     save, of the external mods folder's file list and of tssm-log.txt.
  3. Installs --apk (adb install -r, which keeps the data), if given.
  4. Builds files/perf-test, the test launch's own folder (TestLaunch.java):
     "armed", a config per run with the user's [video] height, msaa,
     framerate, vsync, width and aspect copied (or --config-video), a mod per
     scene whose sb04.ini is the game's with BOOT=<scene> and ShowMenuOnBoot=0,
     and an empty save folder per run. It never writes the user's own files.
  5. Per run: waits until the phone has cooled (the AP sensor within 1 C of
     the session's first reading, or at most 36 C), starts the game with the
     run's extras (am start ... --es TSSM_...), samples the temperatures,
     the charging state and, when readable without changes, the battery's
     current and voltage every 15 s, waits for the game to exit by itself at
     its TSSM_EXIT_AFTER frame (stopping only that process after a timeout),
     and collects the metrics CSV, the threads CSV, the profiler's samples,
     the game's log and logcat --pid of the game.
  6. Cleans up whatever happened: removes files/perf-test, reinstalls the APK
     that was there before, and checks that config.ini, the saves, the mods and
     tssm-log.txt are byte-identical to step 2.

Every run gets a summary (summary.txt, summary.json): frame rate, the frame
intervals (p50, p99, max), frames that missed the paced rate's vsync, the 1%
and 0.1% lows, CPU per phase (update, render, present, swap, pace), GPU time,
passes, draws and programs per frame, the worst frames with their phases, and
the temperature curve. The first --warm seconds (the level loading) are left
out of every number.

The PC mode runs tssm.exe with the same scenes and scripts, each run in its
own folder with its own config, saves, mods and log; it holds the --lock
folder, if given, and waits while a tssm.exe it did not start is running.
"""
import argparse
import bisect
import csv
import hashlib
import io
import json
import math
import os
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PKG = "io.github.tssmport.game"
ACTIVITY = PKG + "/.LauncherActivity"
ADB_DEFAULT = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk", "platform-tools",
                           "adb.exe" if os.name == "nt" else "adb")
PHONE_ASSETS = "/storage/emulated/0/roms/xbox-assets"
EXTERNAL = "/sdcard/Android/data/%s/files" % PKG
PERF_TEST = "files/perf-test"  # relative to the app's data folder (run-as's cwd)

# The [video] keys a test config copies from the user's (unless --config-video).
USER_VIDEO_KEYS = ("width", "height", "aspect", "msaa", "framerate", "vsync")

# --- scenes ---------------------------------------------------------------------
#
# Each scene boots straight into its level (BOOT=<scene>) and visits its spots
# in turn: at each spot the player is put down (TSSM_TEST_PLACE, as the game's
# checkpoint restore does) and walks a loop with the d-pad, jumping and
# attacking, so the camera turns, the player's shadow and the enemies near the
# spot move and the effects play. Spots are "x y z yaw" in world units, found
# on the PC with TSSM_TEST_PLACE captures (the frames are in the bench README).

SCENES = {
    "SC02": {
        "about": "Google-Eyes and Smelly Knick Knacks (Shell City): the level of interest",
        "spots": [(-141.8, 0.8, -254.8, 0), (-194, 5, -204, 0), (-235, 0, -225, 270),
                  (-247, 36, -167, 0), (-264, 48, -78, 0), (-316, 4, -170, 270),
                  (-333, 12, -260, 180), (-146, 3, -280, 180)],
    },
    "DE01": {
        "about": "Dead Eye Gulch: the big outdoor level (the town; its level-start "
                 "fly-through plays about 13 s, so the measured part starts after it)",
        "warm": 16,
        "spots": [(62.7, 2.1, 108.1, 0), (154.9, 8.1, -31.4, 0), (118.1, 9.0, -102.5, 90),
                  (71.2, 15.3, -44.2, 180), (172.4, 5.5, -80.0, 270), (91.0, 15.1, -63.5, 90)],
    },
    "TR01": {
        "about": "the first trench level: the busiest found (most shadow casters, about 20 "
                 "camera passes a frame on the PC, and the most entities and enemies). Not "
                 "the level's start: Mindy talks to the player there, and the measured part "
                 "starts after her talk",
        "warm": 40,
        "spots": [(127.9, 52.0, -9.1, 270),
                  (119.3, 44.5, -15.5, 90), (-23.0, -21.0, 205.0, 0), (-42.0, -9.9, 285.0, 180),
                  (-113.0, -11.0, 209.0, 90), (-190.0, -11.0, 158.0, 270)],
    },
}

# The walk at each spot, in seconds from the spot's start: (start, buttons,
# hold). The d-pad walks relative to the camera, so up, right, down, left
# comes back near the spot.
WALK = [
    (0.6, "up", 2.0), (1.4, "a", 0.15),
    (2.8, "right", 1.6), (3.4, "x", 0.15),
    (4.6, "down", 2.0), (5.6, "a", 0.15),
    (6.8, "left", 1.6), (7.6, "y", 0.15),
    (8.6, "up", 1.4), (9.2, "a+x", 0.15),
]
WALK_LEN = 11.0     # seconds at a spot (the walk, then a short stand)
MAX_PRESSES = 256   # iCapture.cpp kMaxPresses
MAX_PLACES = 64     # iTestEvents.cpp kMaxPlaces


def scene_plan(scene, seconds, rate, warm, still=False):
    """The run's TSSM_* script for a scene: a dict of extras, the frame the
    measured part starts at, and the exit frame."""
    sc = SCENES.get(scene, {"spots": []})
    spots = sc["spots"]
    warm = max(warm, sc.get("warm", 0))
    f = lambda t: int(round(t * rate)) + 1
    warm_frames = f(warm)
    exit_frame = f(warm + seconds)
    env = {}
    if not spots:
        # A level with no spots yet: the player stands where the level starts.
        env["TSSM_INPUT"] = "%d:start" % (exit_frame + 1000)
        return env, warm_frames, exit_frame
    if still:
        x, y, z, yaw = spots[0]
        env["TSSM_TEST_PLACE"] = "%d:%g:%g:%g:%g" % (warm_frames - int(rate), x, y, z, yaw)
        env["TSSM_TEST_HOLD"] = "%d-%d" % (warm_frames - int(rate) + 1, exit_frame)
        # One press after the exit, never reached: a scripted run ignores the
        # touch screen and any controller.
        env["TSSM_INPUT"] = "%d:start" % (exit_frame + 1000)
        return env, warm_frames, exit_frame

    # Longer stays at each spot when the run is long, so the presses fit.
    seg = max(WALK_LEN, math.ceil(seconds * len(WALK) / (MAX_PRESSES - 4 - warm / 1.5)))
    seg = max(seg, seconds / (MAX_PLACES - 2))
    places, presses = [], []
    # While the level starts: A every 1.5 s, which closes a talk or a hint
    # (and skips a fly-through) before the measured part.
    t = 2.0
    while t < warm - 2.0:
        presses.append("%d:a/%d" % (f(t), max(1, int(round(0.15 * rate)))))
        t += 1.5
    t, i = warm - 1.0, 0
    while t < warm + seconds:
        x, y, z, yaw = spots[i % len(spots)]
        places.append("%d:%g:%g:%g:%g" % (f(t), x, y, z, yaw))
        for start, buttons, hold in WALK:
            if t + start < warm + seconds:
                presses.append("%d:%s/%d" % (f(t + start), buttons, max(1, int(round(hold * rate)))))
        t += seg
        i += 1
    presses.append("%d:start" % (exit_frame + 1000))
    env["TSSM_TEST_PLACE"] = ",".join(places)
    env["TSSM_INPUT"] = ",".join(presses)
    return env, warm_frames, exit_frame


def boot_ini(sb04_bytes, scene):
    """The game's sb04.ini with BOOT=<scene> and ShowMenuOnBoot=0 (as
    tools/regress.py's boot_mod)."""
    lines = sb04_bytes.decode("latin-1").splitlines()
    wanted = {"BOOT": "BOOT = %s" % scene, "ShowMenuOnBoot": "ShowMenuOnBoot = 0"}
    out, done = [], set()
    for line in lines:
        key = line.split("=", 1)[0].strip()
        if "=" in line and key in wanted:
            if key in done:
                continue
            done.add(key)
            line = wanted[key]
        out.append(line)
    out[1:1] = [v for k, v in wanted.items() if k not in done]
    return ("\r\n".join(out) + "\r\n").encode("latin-1")


def parse_spec(text, default_seconds):
    m = re.match(r"^([A-Za-z0-9]+)(?:@(\w+))?(?::(\d+(?:\.\d+)?))?((?:\+[^+]+)*)$", text)
    if not m:
        raise SystemExit("bad run spec %r (SCENE[@RATE][:SECONDS][+still][+profile=HZ]"
                         "[+label=NAME])" % text)
    scene = m.group(1).upper()
    if scene not in SCENES:
        print("note: %s is not in SCENES; the player stands where the level starts" % scene)
    spec = {"scene": scene, "rate": m.group(2) or "display",
            "seconds": float(m.group(3) or default_seconds), "still": False, "profile": 0,
            "label": None, "env": {}}
    for opt in filter(None, m.group(4).split("+")):
        k, _, v = opt.partition("=")
        if k == "still":
            spec["still"] = True
        elif k == "profile":
            spec["profile"] = int(v or 1000)
        elif k == "label":
            spec["label"] = v
        elif k == "env" and ":" in v and v.startswith("TSSM_"):
            # An extra switch for the run, e.g. +env=TSSM_GL_VAO:0 (A/B runs of one build).
            ek, _, ev = v.partition(":")
            spec["env"][ek] = ev
        else:
            raise SystemExit("unknown run option +%s" % opt)
    if not spec["label"]:
        spec["label"] = "%s_%s%s" % (scene.lower(), spec["rate"], "_still" if spec["still"] else "")
    return spec


# --- summaries --------------------------------------------------------------------

PHASES = ("update", "render", "present", "swap", "pace")
COUNTS = ("draws", "tris", "passes", "screen_passes", "blits", "clears", "programs", "tex_binds",
          "tex_uploads", "tex_kb", "buf_uploads", "buf_kb", "cameras")


def read_perf_csv(path):
    header, rows = {}, []
    with open(path, newline="") as f:
        lines = []
        for line in f:
            if line.startswith("#"):
                k, _, v = line[1:].strip().partition(" ")
                header[k] = v
            else:
                lines.append(line)
    for r in csv.DictReader(lines):
        try:
            rows.append({k: float(v) if v not in ("", None) else None for k, v in r.items()})
        except ValueError:
            continue  # a row cut off by the process ending
    return header, rows


def pct(sorted_vals, p):
    if not sorted_vals:
        return None
    i = min(len(sorted_vals) - 1, max(0, int(math.ceil(p / 100.0 * len(sorted_vals))) - 1))
    return sorted_vals[i]


def summarize(path, skip_frames=0, thermal=None, exclude=()):
    """The numbers of one run's perf.csv. Frames before skip_frames (the
    loading) and the frames in exclude (captures: writing a PNG is a long
    present phase that is not the game's) are left out."""
    header, rows = read_perf_csv(path)
    exclude = set(exclude)
    rows = [r for r in rows if r["frame"] is not None and r["frame"] >= skip_frames
            and int(r["frame"]) not in exclude]
    out = {"csv": path, "header": header, "frames": len(rows), "excluded": sorted(exclude)}
    if not rows:
        return out
    iv = sorted(r["interval_ms"] for r in rows)
    span = (rows[-1]["t_ms"] - rows[0]["t_ms"]) / 1000.0 + rows[0]["interval_ms"] / 1000.0
    n = len(iv)
    k1, k01 = max(1, n // 100), max(1, n // 1000)
    rate = rows[-1]["rate"] or 0
    out.update({
        "seconds": round(span, 1),
        "fps": round(n / span, 2) if span > 0 else None,
        "paced": rate,
        "interval_ms": {"avg": round(sum(iv) / n, 3), "p50": round(pct(iv, 50), 3),
                        "p99": round(pct(iv, 99), 3), "p99.9": round(pct(iv, 99.9), 3),
                        "max": round(iv[-1], 3)},
        "missed_vsyncs": int(sum(r["missed"] or 0 for r in rows)),
        "frames_missing_vsync": sum(1 for r in rows if (r["missed"] or 0) > 0),
        "low_1pct_fps": round(1000.0 * k1 / sum(iv[-k1:]), 1),
        "low_0.1pct_fps": round(1000.0 * k01 / sum(iv[-k01:]), 1),
    })
    if rate:
        budget = 1000.0 / rate
        out["over_budget_frames"] = sum(1 for v in iv if v > budget * 1.5)
    cpu, wall = {}, {}
    for p in PHASES:
        c = sorted(r[p + "_cpu"] for r in rows)
        w = sorted(r[p + "_wall"] for r in rows)
        cpu[p] = {"avg": round(sum(c) / n, 3), "p99": round(pct(c, 99), 3)}
        wall[p] = {"avg": round(sum(w) / n, 3), "p99": round(pct(w, 99), 3)}
    tot = sorted(sum(r[p + "_cpu"] for p in PHASES) for r in rows)
    work = sorted(sum(r[p + "_cpu"] for p in ("update", "render", "present")) for r in rows)
    out["cpu_ms"] = cpu
    out["wall_ms"] = wall
    out["cpu_total_ms"] = {"avg": round(sum(tot) / n, 3), "p99": round(pct(tot, 99), 3)}
    out["cpu_work_ms"] = {"avg": round(sum(work) / n, 3), "p99": round(pct(work, 99), 3),
                          "max": round(work[-1], 3)}
    out["game_thread_core_pct"] = round(sum(tot) / 10.0 / span, 1) if span > 0 else None
    g = sorted(r["gpu_ms"] for r in rows if r["gpu_ms"] is not None and r["gpu_ms"] >= 0)
    if g:
        out["gpu_ms"] = {"avg": round(sum(g) / len(g), 3), "p50": round(pct(g, 50), 3),
                         "p95": round(pct(g, 95), 3), "p99": round(pct(g, 99), 3),
                         "max": round(g[-1], 3), "frames": len(g)}
    counts = {}
    for c in COUNTS:
        vals = [r.get(c) for r in rows if r.get(c) is not None]
        if vals:
            counts[c] = {"avg": round(sum(vals) / len(vals), 2), "max": max(vals)}
    out["per_frame"] = counts
    worst = sorted(rows, key=lambda r: -r["interval_ms"])[:10]
    out["worst"] = [{
        "frame": int(r["frame"]), "t_s": round(r["t_ms"] / 1000.0, 2),
        "interval_ms": r["interval_ms"],
        "wall": {p: r[p + "_wall"] for p in PHASES}, "cpu": {p: r[p + "_cpu"] for p in PHASES},
        "gpu_ms": r["gpu_ms"], "draws": r.get("draws"), "cameras": r.get("cameras"),
    } for r in worst]
    # What blew the budget: for each frame over 1.5 periods, the phase whose
    # wall time was largest.
    if rate:
        blame = {}
        for r in rows:
            if r["interval_ms"] > 1.5 * 1000.0 / rate:
                p = max(PHASES[:4], key=lambda p: r[p + "_wall"])
                blame[p] = blame.get(p, 0) + 1
        out["over_budget_by_phase"] = blame
    # Per 30 s, for drift with heat.
    buckets = []
    t0 = rows[0]["t_ms"]
    cur = []
    for r in rows:
        if r["t_ms"] - t0 >= 30000.0 and cur:
            buckets.append(cur)
            cur, t0 = [], r["t_ms"]
        cur.append(r)
    if cur:
        buckets.append(cur)
    out["per_30s"] = [{
        "t_s": round(b[0]["t_ms"] / 1000.0, 1), "frames": len(b),
        "fps": round(len(b) / max(1e-6, (b[-1]["t_ms"] - b[0]["t_ms"]) / 1000.0 + b[0]["interval_ms"] / 1000.0), 1),
        "missed": int(sum(r["missed"] or 0 for r in b)),
        "max_ms": round(max(r["interval_ms"] for r in b), 2),
        "cpu_work_ms": round(sum(sum(r[p + "_cpu"] for p in ("update", "render", "present")) for r in b) / len(b), 3),
        "gpu_ms": (lambda g: round(sum(g) / len(g), 3) if g else None)(
            [r["gpu_ms"] for r in b if r["gpu_ms"] is not None and r["gpu_ms"] >= 0]),
    } for b in buckets]
    if thermal:
        out["thermal"] = thermal
    return out


def summary_text(s, title):
    L = ["== %s ==" % title]
    if not s.get("frames"):
        L.append("no frames")
        return "\n".join(L) + "\n"
    h = s["header"]
    L.append("%s; %s; vsync/paced %s" % (h.get("backend", "?"), h.get("device", "?"), h.get("vsync", "?")))
    iv = s["interval_ms"]
    L.append("%d frames in %.1f s: %.2f fps (paced %d); interval avg %.2f p50 %.2f p99 %.2f "
             "p99.9 %.2f max %.2f ms" % (s["frames"], s["seconds"], s["fps"], s["paced"], iv["avg"],
                                         iv["p50"], iv["p99"], iv["p99.9"], iv["max"]))
    L.append("missed vsyncs %d (in %d frames), over 1.5 periods %s; 1%% low %.1f fps, 0.1%% low "
             "%.1f fps" % (s["missed_vsyncs"], s["frames_missing_vsync"], s.get("over_budget_frames"),
                           s["low_1pct_fps"], s["low_0.1pct_fps"]))
    c = s["cpu_ms"]
    L.append("game thread CPU ms/frame: update %.2f render %.2f present %.2f swap %.2f pace %.2f; "
             "work (update+render+present) avg %.2f p99 %.2f max %.2f; %.0f%% of a core" % (
                 c["update"]["avg"], c["render"]["avg"], c["present"]["avg"], c["swap"]["avg"],
                 c["pace"]["avg"], s["cpu_work_ms"]["avg"], s["cpu_work_ms"]["p99"],
                 s["cpu_work_ms"]["max"], s["game_thread_core_pct"] or 0))
    w = s["wall_ms"]
    L.append("wall ms/frame: update %.2f render %.2f present %.2f swap %.2f pace %.2f" % tuple(
        w[p]["avg"] for p in PHASES))
    if "gpu_ms" in s:
        g = s["gpu_ms"]
        L.append("GPU ms/frame: avg %.2f p50 %.2f p95 %.2f p99 %.2f max %.2f (%d frames)" % (
            g["avg"], g["p50"], g["p95"], g["p99"], g["max"], g["frames"]))
    pf = s["per_frame"]
    if "draws" in pf:
        L.append("per frame: draws %.0f (max %d), passes %.1f (screen %.1f), blits %.1f, clears %.1f, "
                 "programs %.1f, texture binds %.0f, uploads tex %.2f buf %.0f (%.0f KB), cameras %.1f" % (
                     pf["draws"]["avg"], pf["draws"]["max"], pf["passes"]["avg"],
                     pf["screen_passes"]["avg"], pf["blits"]["avg"], pf["clears"]["avg"],
                     pf["programs"]["avg"], pf["tex_binds"]["avg"], pf["tex_uploads"]["avg"],
                     pf["buf_uploads"]["avg"], pf["buf_kb"]["avg"], pf["cameras"]["avg"]))
    if s.get("over_budget_by_phase"):
        L.append("frames over 1.5 periods, by their longest phase: %s" % ", ".join(
            "%s %d" % kv for kv in sorted(s["over_budget_by_phase"].items(), key=lambda kv: -kv[1])))
    L.append("worst frames (interval; wall ms update/render/present/swap/pace; gpu):")
    for r in s["worst"][:6]:
        L.append("  frame %d at %.1f s: %.2f ms; %s; gpu %s" % (
            r["frame"], r["t_s"], r["interval_ms"],
            "/".join("%.2f" % r["wall"][p] for p in PHASES), r["gpu_ms"]))
    L.append("per 30 s: " + "; ".join("%.0fs %.1ffps miss %d max %.1f cpu %.2f gpu %s" % (
        b["t_s"], b["fps"], b["missed"], b["max_ms"], b["cpu_work_ms"], b["gpu_ms"]) for b in s["per_30s"]))
    th = s.get("thermal")
    if th:
        L.append("AP C: " + " ".join("%.0fs:%s" % (x["t"], x.get("AP")) for x in th))
        L.append("battery C: " + " ".join("%s" % x.get("battery") for x in th) +
                 "; charging: %s" % sorted(set(str(x.get("charging")) for x in th)))
    return "\n".join(L) + "\n"


def write_summary(run_dir, s, title):
    with open(os.path.join(run_dir, "summary.json"), "w") as f:
        json.dump(s, f, indent=1)
    text = summary_text(s, title)
    with open(os.path.join(run_dir, "summary.txt"), "w") as f:
        f.write(text)
    print(text)


# --- the phone ----------------------------------------------------------------------

class Phone:
    def __init__(self, adb, log):
        self.adb = adb
        self.log = log

    def say(self, msg):
        line = time.strftime("%H:%M:%S ") + msg
        print(line)
        sys.stdout.flush()
        self.log.write(line + "\n")
        self.log.flush()

    def run(self, args, data=None, timeout=120, check=False):
        env = dict(os.environ, MSYS_NO_PATHCONV="1")
        r = subprocess.run([self.adb] + args, input=data, capture_output=True, timeout=timeout,
                           env=env)
        if check and r.returncode != 0:
            raise RuntimeError("adb %s failed: %s" % (" ".join(args)[:200],
                                                      (r.stderr or r.stdout)[-400:]))
        return r

    def sh(self, cmd, timeout=120):
        """adb shell: text output."""
        r = self.run(["shell", cmd], timeout=timeout)
        return r.stdout.decode("utf-8", "replace").replace("\r\n", "\n")

    def runas(self, cmd, timeout=120, binary=False):
        """A command as the game's own user, in its data folder."""
        # adb joins its arguments into one line for the phone's shell: quoted.
        r = self.run(["exec-out", "run-as %s sh -c %s" % (PKG, shlex.quote(cmd))], timeout=timeout)
        return r.stdout if binary else r.stdout.decode("utf-8", "replace").replace("\r\n", "\n")

    def connected(self):
        try:
            r = self.run(["get-state"], timeout=15)
        except subprocess.TimeoutExpired:
            return False
        return r.returncode == 0 and b"device" in r.stdout

    def game_pids(self):
        return self.sh("pidof %s" % PKG).split()

    def screen(self):
        """(awake, locked) from the power and window services, filtered on the phone."""
        p = self.sh("dumpsys power | grep -E 'mWakefulness='")
        w = self.sh("dumpsys window | grep -E 'mDreamingLockscreen=|isKeyguardShowing=|"
                    "mShowingLockscreen=|KeyguardShowing=' | head -6")
        awake = "Awake" in p
        locked = bool(re.search(r"(mDreamingLockscreen|isKeyguardShowing|mShowingLockscreen|"
                                r"KeyguardShowing)=true", w))
        return awake, locked, (p + w).strip().replace("\n", " | ")

    def thermal(self):
        """The device's temperature sensors (thermalservice, the HAL's current
        values) and the battery's state."""
        out = {"t_wall": time.time()}
        t = self.sh("dumpsys thermalservice | grep -E 'Thermal Status|Temperature\\{|"
                    "temperatures from'")
        section = None
        for line in t.splitlines():
            if "temperatures from" in line:
                section = "hal" if "HAL" in line else "cached"
                continue
            m = re.search(r"Thermal Status: (\d+)", line)
            if m:
                out["status"] = int(m.group(1))
            m = re.search(r"mValue=([-\d.]+).*?mName=([^,}]+)", line)
            if m and (section == "hal" or m.group(2) not in out):
                name = m.group(2).strip()
                key = {"AP": "AP", "BAT": "battery_sensor", "SKIN": "skin"}.get(name, name)
                out[key] = float(m.group(1))
        b = self.sh("dumpsys battery | grep -E 'powered|status:|level:|temperature:|voltage:|"
                    "current now|Charge counter'")
        bat = {}
        for line in b.splitlines():
            k, _, v = line.strip().partition(":")
            bat[k.strip()] = v.strip()
        if "temperature" in bat:
            try:
                out["battery"] = int(bat["temperature"]) / 10.0
            except ValueError:
                pass
        out["charging"] = ",".join(k for k in ("AC powered", "USB powered", "Wireless powered",
                                               "Dock powered") if bat.get(k) == "true") or "no"
        out["battery_status"] = bat.get("status")
        out["level"] = bat.get("level")
        cv = self.sh("cat /sys/class/power_supply/battery/current_now "
                     "/sys/class/power_supply/battery/voltage_now 2>/dev/null").split()
        if len(cv) == 2 and all(re.match(r"^-?\d+$", x) for x in cv):
            out["current_uA"] = int(cv[0])
            out["voltage_uV"] = int(cv[1])
        return out

    def sha_user_files(self):
        """sha1s of everything the user owns that a test must not change."""
        cfg = self.runas("sha1sum files/config.ini 2>/dev/null").split()
        saves = self.runas("cd files/tssm/saves 2>/dev/null && find . -type f -exec sha1sum {} + "
                           "| sort")
        mods = self.sh("cd %s/mods 2>/dev/null && find . -type f -exec sha1sum {} + | sort" % EXTERNAL)
        logs = self.sh("cd %s && sha1sum tssm-log.txt tssm-log.prev.txt crash.log 2>/dev/null"
                       % EXTERNAL)
        prefs = self.runas("cd shared_prefs 2>/dev/null && sha1sum * 2>/dev/null")
        h = lambda s: hashlib.sha1(s.encode()).hexdigest()
        return {"config.ini": cfg[0] if cfg else None,
                "saves": h(saves), "saves_files": len(saves.splitlines()),
                "mods": h(mods), "external_logs": h(logs), "shared_prefs": h(prefs)}

    def installed_apk_path(self):
        out = self.sh("pm path %s" % PKG).split()
        paths = [p[len("package:"):] for p in out if p.startswith("package:")]
        return paths[0] if paths else None


def phone_session(a):
    os.makedirs(a.out, exist_ok=True)
    log = open(os.path.join(a.out, "session.log"), "a")
    ph = Phone(a.adb, log)
    specs = [parse_spec(s, a.seconds) for s in (a.run or ["SC02"])]
    lock = a.lock

    # 1. The phone, the screen, the user.
    t0 = time.time()
    while not ph.connected():
        if time.time() - t0 > a.wait_phone:
            ph.say("no phone after %d s; stopping" % a.wait_phone)
            return 5
        ph.say("no phone connected; checking again in 60 s")
        time.sleep(60)
    if lock:
        while True:
            try:
                os.mkdir(lock)
                break
            except FileExistsError:
                ph.say("phone lock %s is held; waiting" % lock)
                time.sleep(30)
    ph.say("phone lock taken")
    try:
        return phone_locked(a, ph, specs)
    finally:
        if lock:
            os.rmdir(lock)
            ph.say("phone lock released")


def wait_ready(a, ph, why):
    """Screen on, unlocked, the game not running. False after --wait-screen."""
    t0 = time.time()
    said = None
    while True:
        if not ph.connected():
            state = "phone not connected"
        else:
            pids = ph.game_pids()
            awake, locked, raw = ph.screen()
            if pids:
                state = "the game is running (pid %s) and this script did not start it: someone " \
                        "is playing; waiting" % " ".join(pids)
            elif not awake:
                state = "the screen is off (%s)" % raw
            elif locked:
                state = "the phone is locked (%s); it has to be unlocked by its owner" % raw
            else:
                return True
        if state != said:
            ph.say("%s: %s" % (why, state))
            said = state
        if time.time() - t0 > a.wait_screen:
            ph.say("%s: still not ready after %d s; stopping" % (why, a.wait_screen))
            return False
        time.sleep(30)


def phone_locked(a, ph, specs):
    if not wait_ready(a, ph, "before the session"):
        return 4
    stamp = time.strftime("%Y%m%d-%H%M%S")

    # 2. Backup, the installed APK, the user's files' hashes.
    backup = os.path.join(a.out, "phone-backup-%s.tar" % stamp)
    with open(backup, "wb") as f:
        r = subprocess.run([a.adb, "exec-out", "run-as", PKG, "tar", "cf", "-", "files",
                            "shared_prefs"], stdout=f, timeout=600)
    size = os.path.getsize(backup)
    if r.returncode != 0 or size < 10240:
        ph.say("backup failed (%d bytes); stopping before anything is changed" % size)
        return 3
    ph.say("backup %s (%d bytes)" % (backup, size))
    apk_path = ph.installed_apk_path()
    pre_apk = os.path.join(a.out, "pre-install-%s.apk" % stamp)
    with open(pre_apk, "wb") as f:
        subprocess.run([a.adb, "exec-out", "cat", apk_path], stdout=f, timeout=600)
    pre_sha = sha1_file(pre_apk)
    ph.say("installed APK %s: %d bytes, sha1 %s" % (apk_path, os.path.getsize(pre_apk), pre_sha))
    if os.path.getsize(pre_apk) < 1 << 20:
        ph.say("could not pull the installed APK; stopping before anything is changed")
        return 3
    before = ph.sha_user_files()
    ph.say("user files before: %s" % json.dumps(before))
    video = {}
    if a.config_video:
        for kv in a.config_video:
            k, _, v = kv.partition("=")
            video[k.strip()] = v.strip()
    else:
        # Only these lines of the user's config are read, and nothing is written there.
        lines = ph.runas("grep -E '^(%s) *=' files/config.ini" % "|".join(USER_VIDEO_KEYS))
        for line in lines.splitlines():
            k, _, v = line.partition("=")
            if k.strip() in USER_VIDEO_KEYS and k.strip() not in video:
                video[k.strip()] = v.split(";")[0].strip()
    ph.say("test config [video]: %s" % video)
    first_t = ph.thermal()
    ph.say("first reading: %s" % json.dumps(first_t))

    installed = False
    results = []
    try:
        # 3. The APK under test.
        if a.apk:
            r = ph.run(["install", "-r", "-d", a.apk], timeout=600)
            ok = r.returncode == 0 and b"Success" in r.stdout
            ph.say("install %s: %s" % (a.apk, (r.stdout + r.stderr).decode("utf-8", "replace").strip()))
            if not ok:
                return 3
            installed = True

        # 4. files/perf-test.
        sb04 = ph.sh("cat %s/sb04.ini" % PHONE_ASSETS).encode("latin-1", "replace")
        if b"BOOT" not in sb04 and a.assets:
            sb04 = open(os.path.join(a.assets, "sb04.ini"), "rb").read()
        if b"BOOT" not in sb04:
            ph.say("no sb04.ini to make the boot mods from")
            return 3
        tree = io.BytesIO()
        with tarfile.open(fileobj=tree, mode="w", format=tarfile.USTAR_FORMAT) as tar:
            def add(name, data=None):
                ti = tarfile.TarInfo("perf-test/" + name)
                ti.mtime = int(time.time())
                if data is None:
                    ti.type = tarfile.DIRTYPE
                    ti.mode = 0o700
                    tar.addfile(ti)
                else:
                    ti.size = len(data)
                    ti.mode = 0o600
                    tar.addfile(ti, io.BytesIO(data))
            add("")
            add("armed", b"tools/android_bench.py\n")
            add("mods")
            add("saves")
            add("runs")
            for scene in sorted(set(s["scene"] for s in specs)):
                add("mod_%s" % scene)
                add("mod_%s/sb04.ini" % scene, boot_ini(sb04, scene))
            for s in specs:
                v = dict(video)
                if s["rate"] != "user":
                    v["framerate"] = s["rate"]
                add("config_%s.ini" % s["label"], test_config(v))
                add("runs/%s" % s["label"])
                add("runs/%s/saves" % s["label"])
        if not push_tree(ph, tree.getvalue()):
            return 3
        ph.say("files/perf-test ready: %s" % ph.runas("cd files/perf-test && find . | sort | tr '\\n' ' '"))

        # 5. The runs.
        for s in specs:
            res = phone_run(a, ph, s, first_t)
            results.append(res)
            if res.get("fatal"):
                break
    finally:
        # 6. Clean up, whatever happened.
        ph.say("cleaning up")
        for pid in getattr(phone_run, "started", []):
            if pid in ph.game_pids():
                ph.say("stopping the test game %s" % pid)
                ph.sh("am force-stop %s" % PKG)
        ph.runas("rm -rf files/perf-test")
        left = ph.runas("ls -d files/perf-test 2>/dev/null").strip()
        ph.say("files/perf-test removed" if not left else "files/perf-test is STILL THERE")
        if installed:
            r = ph.run(["install", "-r", "-d", pre_apk], timeout=600)
            ph.say("reinstalled the APK from before: %s" % (r.stdout + r.stderr).decode(
                "utf-8", "replace").strip())
            path = ph.installed_apk_path()
            now = subprocess.run([a.adb, "exec-out", "cat", path], capture_output=True,
                                 timeout=600).stdout
            ph.say("installed APK sha1 now %s (%s)" % (hashlib.sha1(now).hexdigest(),
                                                       "same as before" if hashlib.sha1(now).hexdigest() == pre_sha else "DIFFERENT"))
        after = ph.sha_user_files()
        same = after == before
        ph.say("user files after: %s -> %s" % (json.dumps(after), "IDENTICAL" if same else "CHANGED"))
        with open(os.path.join(a.out, "session-%s.json" % stamp), "w") as f:
            json.dump({"before": before, "after": after, "identical": same, "pre_apk_sha1": pre_sha,
                       "apk": a.apk, "video": video, "first_thermal": first_t,
                       "runs": [{k: v for k, v in r.items() if k != "summary"} for r in results]},
                      f, indent=1)
    return 0 if same else 2


def sha1_file(path):
    h = hashlib.sha1()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def test_config(video):
    lines = ["; a test launch's own config (tools/android_bench.py); never the player's", "[video]"]
    for k in USER_VIDEO_KEYS:
        if k in video:
            lines.append("%s = %s" % (k, video[k]))
    for k, v in video.items():
        if k not in USER_VIDEO_KEYS:
            lines.append("%s = %s" % (k, v))
    lines += ["[game]", "intro_movies = off", ""]
    return "\r\n".join(lines).encode()


def push_tree(ph, data):
    """Unpacks a tar of perf-test into the app's files folder."""
    ph.runas("rm -rf files/perf-test")
    r = ph.run(["exec-in", "run-as", PKG, "tar", "xf", "-", "-C", "files"], data=data, timeout=300)
    if ph.runas("test -f files/perf-test/armed && echo yes").strip() == "yes":
        return True
    ph.say("exec-in did not unpack (%s); going through /data/local/tmp/tssm-bench.tar"
           % (r.stderr or b"").decode("utf-8", "replace").strip())
    tmp = os.path.join(os.path.dirname(os.path.abspath(ph.log.name)), "tssm-bench.tar")
    with open(tmp, "wb") as f:
        f.write(data)
    ph.run(["push", tmp, "/data/local/tmp/tssm-bench.tar"], timeout=300)
    ph.sh("chmod 644 /data/local/tmp/tssm-bench.tar")
    ph.runas("tar xf /data/local/tmp/tssm-bench.tar -C files")
    ph.sh("rm -f /data/local/tmp/tssm-bench.tar")
    os.remove(tmp)
    if ph.runas("test -f files/perf-test/armed && echo yes").strip() == "yes":
        return True
    ph.say("could not make files/perf-test")
    return False


def wait_cool(a, ph, first, label):
    target = max(first.get("AP", 0) + 1.0, 36.0)
    t0 = time.time()
    while True:
        t = ph.thermal()
        ap = t.get("AP")
        if ap is None or ap <= target:
            ph.say("%s: AP %.1f C (target %.1f C, first %.1f C); battery %.1f C; charging %s" % (
                label, ap or -1, target, first.get("AP", -1), t.get("battery", -1), t["charging"]))
            return t, time.time() - t0
        if time.time() - t0 > a.cool_max:
            ph.say("%s: AP still %.1f C after %d s; running anyway (noted)" % (label, ap, a.cool_max))
            return t, time.time() - t0
        ph.say("%s: cooling, AP %.1f C > %.1f C" % (label, ap, target))
        time.sleep(30)


def phone_run(a, ph, s, first):
    label = s["label"]
    res = {"label": label, "spec": s}
    if not wait_ready(a, ph, label):
        res["fatal"] = "phone not ready"
        return res
    start_t, cooled_s = wait_cool(a, ph, first, label)
    if not wait_ready(a, ph, label):
        res["fatal"] = "phone not ready"
        return res
    rate = a.display_hz if s["rate"] in ("display", "user") else int(s["rate"])
    env, warm_frames, exit_frame = scene_plan(s["scene"], s["seconds"], rate, a.warm, s["still"])
    rd = "runs/%s" % label
    env.update({
        "TSSM_CONFIG": "config_%s.ini" % label,
        "TSSM_SAVE_DIR": rd + "/saves",
        "TSSM_MODS_DIR": "mods",
        "TSSM_MOD": "mod_%s" % s["scene"],
        "TSSM_LOG": rd + "/tssm-log.txt",
        "TSSM_EXIT_AFTER": str(exit_frame),
        "TSSM_PERF": str(a.window),
        "TSSM_PERF_CSV": rd + "/perf.csv",
    })
    env.update(s["env"])
    if s["profile"]:
        env["TSSM_PERF_PROFILE"] = str(s["profile"])
        env["TSSM_PERF_PROFILE_OUT"] = rd + "/perf-samples.txt"
    extras = " ".join("--es %s '%s'" % (k, v) for k, v in env.items())
    cmd = "am start -W -n %s %s" % (ACTIVITY, extras)
    if len(cmd) > 60000:
        res["fatal"] = "the command is too long"
        return res
    local = os.path.join(a.out, label)
    os.makedirs(local, exist_ok=True)
    with open(os.path.join(local, "launch.txt"), "w") as f:
        f.write(cmd + "\n")
    ph.say("%s: starting (%s, %.0f s at %d fps, exit at frame %d)" % (label, s["scene"],
                                                                     s["seconds"], rate, exit_frame))
    t_start = time.time()
    out = ph.sh(cmd, timeout=120)
    pid = None
    for _ in range(60):
        pids = ph.game_pids()
        if pids:
            pid = pids[0]
            break
        time.sleep(1)
    if pid is None:
        ph.say("%s: the game did not start: %s" % (label, out.strip()))
        res["fatal"] = "did not start"
        return res
    phone_run.started = getattr(phone_run, "started", []) + [pid]
    res["pid"] = pid
    thermal = []
    expected = exit_frame / float(rate) + 20
    limit = expected * 1.8 + 60
    next_sample = time.time()
    timed_out = False
    screen_lost = False
    while True:
        if pid not in ph.game_pids():
            break
        now = time.time()
        if now >= next_sample:
            t = ph.thermal()
            t["t"] = round(now - t_start, 1)
            thermal.append(t)
            awake, locked, _ = ph.screen()
            if not awake or locked:
                screen_lost = True
            next_sample += a.sample_every
        if now - t_start > limit:
            ph.say("%s: still running after %d s; stopping the test game (pid %s)" % (label, limit, pid))
            ph.sh("am force-stop %s" % PKG)
            timed_out = True
            break
        time.sleep(2)
    elapsed = time.time() - t_start
    end_t = ph.thermal()
    end_t["t"] = round(elapsed, 1)
    thermal.append(end_t)
    ph.say("%s: exited after %.0f s; AP %s C" % (label, elapsed, end_t.get("AP")))
    if screen_lost:
        ph.say("%s: the screen went off or locked during the run; its numbers are not valid" % label)
    # Collect.
    for name in ("perf.csv", "perf-threads.csv", "perf-samples.txt", "tssm-log.txt", "crash.log"):
        data = ph.runas("cat %s/%s/%s 2>/dev/null" % (PERF_TEST, rd, name), timeout=300, binary=True)
        if data:
            with open(os.path.join(local, name), "wb") as f:
                f.write(data)
    lc = ph.sh("logcat -d -v time --pid=%s" % pid, timeout=120)
    with open(os.path.join(local, "logcat.txt"), "w", encoding="utf-8") as f:
        f.write(lc)
    with open(os.path.join(local, "thermal.json"), "w") as f:
        json.dump({"start": start_t, "cooled_s": cooled_s, "samples": thermal}, f, indent=1)
    res.update(elapsed=round(elapsed, 1), timed_out=timed_out, screen_lost=screen_lost,
               start_AP=start_t.get("AP"), end_AP=end_t.get("AP"),
               charging=sorted(set(t["charging"] for t in thermal)))
    csvp = os.path.join(local, "perf.csv")
    if os.path.exists(csvp):
        th = [{"t": t["t"], "AP": t.get("AP"), "battery": t.get("battery"), "skin": t.get("skin"),
               "charging": t["charging"], "status": t.get("status"),
               "current_mA": round(t["current_uA"] / 1000.0) if "current_uA" in t else None,
               "voltage_V": round(t["voltage_uV"] / 1e6, 3) if "voltage_uV" in t else None}
              for t in [dict(start_t, t=0.0)] + thermal]
        sm = summarize(csvp, warm_frames, th)
        sm["run"] = {k: v for k, v in res.items() if k != "summary"}
        write_summary(local, sm, "%s (phone)" % label)
        res["summary"] = sm
        if s["profile"] and os.path.exists(os.path.join(local, "perf-samples.txt")) and a.lib:
            try:
                rep = symbolize(os.path.join(local, "perf-samples.txt"), a.lib, a.symbolizer)
                with open(os.path.join(local, "profile.txt"), "w") as f:
                    f.write(rep)
            except Exception as e:  # the run's numbers stand without it
                ph.say("%s: symbolizing failed: %s" % (label, e))
    else:
        ph.say("%s: no perf.csv came back" % label)
    return res


# --- the PC -------------------------------------------------------------------------

def other_tssm(own):
    if os.name != "nt":
        return []
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq tssm.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    pids = []
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if parts and parts[0].lower() == "tssm.exe" and len(parts) > 1:
            if int(parts[1]) not in own:
                pids.append(int(parts[1]))
    return pids


def pc_session(a):
    os.makedirs(a.out, exist_ok=True)
    specs = [parse_spec(s, a.seconds) for s in (a.run or ["SC02@120"])]
    sb04 = open(os.path.join(a.assets, "sb04.ini"), "rb").read()
    code = 0
    for s in specs:
        label = s["label"] + ("_" + a.backend if a.backend else "")
        rd = os.path.abspath(os.path.join(a.out, label))
        shutil.rmtree(rd, ignore_errors=True)
        os.makedirs(os.path.join(rd, "saves"))
        os.makedirs(os.path.join(rd, "mod_boot"))
        with open(os.path.join(rd, "mod_boot", "sb04.ini"), "wb") as f:
            f.write(boot_ini(sb04, s["scene"]))
        rate = a.display_hz if s["rate"] in ("display", "user") else int(s["rate"])
        v = {"width": a.size.split("x")[0], "height": a.size.split("x")[1], "aspect": "custom",
             "msaa": str(a.msaa), "framerate": s["rate"], "vsync": a.vsync, "mode": "windowed"}
        if a.backend:
            v["backend"] = a.backend
        for kv in a.config_video or []:
            k, _, val = kv.partition("=")
            v[k.strip()] = val.strip()
        with open(os.path.join(rd, "config.ini"), "wb") as f:
            f.write(test_config(v))
        env, warm_frames, exit_frame = scene_plan(s["scene"], s["seconds"], rate, a.warm, s["still"])
        e = {k: v for k, v in os.environ.items() if not k.upper().startswith("TSSM_")}
        e.update(env)
        e.update({"TSSM_ASSETS": a.assets, "TSSM_CONFIG": os.path.join(rd, "config.ini"),
                  "TSSM_SAVE_DIR": os.path.join(rd, "saves"),
                  "TSSM_MODS_DIR": os.path.join(rd, "mods-none"),
                  "TSSM_MOD": os.path.join(rd, "mod_boot"), "TSSM_LOG": os.path.join(rd, "tssm.log"),
                  "TSSM_EXIT_AFTER": str(exit_frame), "TSSM_PERF": str(a.window),
                  "TSSM_PERF_CSV": os.path.join(rd, "perf.csv")})
        e.update(s["env"])
        if a.capture_at:
            os.makedirs(os.path.join(rd, "frames"), exist_ok=True)
            e["TSSM_CAPTURE_DIR"] = os.path.join(rd, "frames")
            e["TSSM_CAPTURE_FRAMES"] = ",".join(
                str(int(round((a.warm + float(t)) * rate)) + 1) for t in a.capture_at.split(","))
        for kv in a.env or []:
            k, _, val = kv.partition("=")
            e[k] = val
        with open(os.path.join(rd, "launch.json"), "w") as f:
            json.dump({k: e[k] for k in e if k.startswith("TSSM_")}, f, indent=1)
        took = take_lock(a.lock)
        try:
            while other_tssm(set()):
                print("a tssm.exe this script did not start is running; waiting")
                time.sleep(20)
            t0 = time.time()
            with open(os.path.join(rd, "stdout.txt"), "wb") as out:
                p = subprocess.Popen([a.exe], cwd=rd, env=e, stdout=out, stderr=subprocess.STDOUT)
                try:
                    rc = p.wait(exit_frame / float(rate) * 3 + 120)
                except subprocess.TimeoutExpired:
                    p.kill()  # this run's own process, and only it
                    p.wait()
                    rc = None
            elapsed = time.time() - t0
        finally:
            if took:
                os.rmdir(a.lock)
        print("%s: exit %s after %.0f s" % (label, rc, elapsed))
        if rc != 0:
            code = 1
        if os.path.exists(os.path.join(rd, "perf.csv")):
            caps = [int(x) for x in e.get("TSSM_CAPTURE_FRAMES", "").split(",") if x]
            sm = summarize(os.path.join(rd, "perf.csv"), warm_frames, exclude=caps)
            sm["run"] = {"label": label, "spec": s, "exit": rc, "elapsed": round(elapsed, 1)}
            write_summary(rd, sm, "%s (PC)" % label)
    return code


def take_lock(lock):
    if not lock:
        return False
    while True:
        try:
            os.mkdir(lock)
            return True
        except FileExistsError:
            time.sleep(3)


# --- the profile --------------------------------------------------------------------

def classify(path_or_lib, srcfile):
    f = (srcfile or "").replace("\\", "/")
    if path_or_lib == "libmain.so":
        for key, name in (("/third_party/librw/", "librw"), ("/third_party/SDL/", "SDL"),
                          ("/third_party/imgui/", "imgui"), ("/src/SB/Core/pc/", "port (pc)"),
                          ("/src/SB/Game/", "game"), ("/src/SB/Core/x/", "game core (x)"),
                          ("/src/SB/Core/", "game core"), ("/src/", "game other"),
                          ("ndk", "C++ runtime")):
            if key in f.lower() if key == "ndk" else key in f:
                return name
        return "libmain.so (other)"
    return path_or_lib


def symbolize(samples_path, lib, symbolizer=None, top=40):
    """A text report from TSSM_PERF_PROFILE's samples: self and inclusive time by
    function and library, and by frame phase. libmain.so's addresses are named
    from the unstripped copy (lib) with llvm-symbolizer; other libraries' with
    the names the game found with dladdr."""
    head, maps, names, stacks = {}, [], {}, []
    selfname, selfbase = None, 0
    mode = None
    for line in open(samples_path, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if mode is None:
            if line == "maps":
                mode = "maps"
            elif line.startswith("#"):
                continue
            else:
                k, _, v = line.partition(" ")
                head[k] = v
        elif mode == "maps":
            if line == "endmaps":
                mode = "after"
                continue
            m = re.match(r"^([0-9a-f]+)-([0-9a-f]+) (\S+) ([0-9a-f]+) \S+ \d+\s*(.*)$", line)
            if m:
                maps.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(4), 16),
                             m.group(5).strip()))
        elif mode == "after":
            if line.startswith("self "):
                _, selfname, base = line.split(" ", 2)
                selfbase = int(base, 16)
            elif line == "names":
                mode = "names"
            elif line == "stacks":
                mode = "stacks"
        elif mode == "names":
            if line == "endnames":
                mode = "after"
                continue
            parts = line.split(" ")
            if len(parts) >= 3:
                names[int(parts[0], 16)] = (os.path.basename(parts[1]), parts[2])
        elif mode == "stacks":
            if line == "end":
                break
            parts = line.split()
            stacks.append((int(parts[0]), int(parts[1]), [int(x, 16) for x in parts[2:]]))
    maps.sort()
    starts = [m[0] for m in maps]

    def lib_of(addr):
        i = bisect.bisect_right(starts, addr) - 1
        if i >= 0 and maps[i][0] <= addr < maps[i][1]:
            p = maps[i][3]
            if selfname and (p == selfname or "base.apk" in p or p.endswith("libmain.so")):
                return "libmain.so"
            return os.path.basename(p) or "[anon]"
        return "[unknown]"

    own = sorted({a for _, _, st in stacks for a in st if a and lib_of(a) == "libmain.so"})
    sym = {}
    if own:
        tool = symbolizer or find_symbolizer()
        inp = "\n".join("0x%x" % (a - selfbase) for a in own)
        r = subprocess.run([tool, "--obj=" + lib, "--functions=linkage", "--demangle",
                            "--no-inlines", "--relativenames"],
                           input=inp, capture_output=True, text=True)
        blocks = r.stdout.strip().split("\n\n")
        for a, b in zip(own, blocks):
            ls = b.strip().splitlines()
            fn = ls[0] if ls else "??"
            src = ls[1] if len(ls) > 1 else ""
            sym[a] = (fn, src)

    def name_of(addr):
        lb = lib_of(addr)
        if lb == "libmain.so":
            fn, src = sym.get(addr, ("??", ""))
            return lb, fn, src
        n = names.get(addr)
        return lb, (n[1] if n else "?"), ""

    total = sum(c for c, _, _ in stacks)
    self_fn, self_lib, incl_fn, incl_cat, by_phase, phase_fn = {}, {}, {}, {}, {}, {}
    caller = {}
    for count, phase, st in stacks:
        if not st:
            continue
        lb, fn, src = name_of(st[0])
        cat = classify(lb, src)
        key = "%s  [%s]" % (short(fn), cat)
        self_fn[key] = self_fn.get(key, 0) + count
        self_lib[cat] = self_lib.get(cat, 0) + count
        by_phase[phase] = by_phase.get(phase, 0) + count
        pk = (phase, key)
        phase_fn[pk] = phase_fn.get(pk, 0) + count
        # Time outside libmain.so (the driver, libc) charged to the nearest
        # caller inside it: which of the port's or the game's calls cost it.
        for addr in st:
            if addr and lib_of(addr) == "libmain.so":
                _, fnc, srcc = name_of(addr)
                kc = "%s  [%s]" % (short(fnc), classify("libmain.so", srcc))
                if lb != "libmain.so":
                    kc += "  <- " + lb
                caller[kc] = caller.get(kc, 0) + count
                break
        seen, seencat = set(), set()
        for i, addr in enumerate(st):
            if addr == 0:
                continue
            lb2, fn2, src2 = name_of(addr)
            c2 = classify(lb2, src2)
            k2 = "%s  [%s]" % (short(fn2), c2)
            if k2 not in seen:
                seen.add(k2)
                incl_fn[k2] = incl_fn.get(k2, 0) + count
            if c2 not in seencat:
                seencat.add(c2)
                incl_cat[c2] = incl_cat.get(c2, 0) + count
    L = []
    L.append("samples %d (%s Hz of the thread's CPU time; lost %s); thread %s" % (
        total, head.get("hz"), head.get("lost"), head.get("thread")))
    L.append("")
    L.append("by phase (iPerf.h):")
    for ph, c in sorted(by_phase.items()):
        L.append("  %-8s %5.1f%%" % (PHASES[ph] if ph < len(PHASES) else ph, 100.0 * c / total))
    L.append("")
    L.append("self time by library / part:")
    for k, c in sorted(self_lib.items(), key=lambda kv: -kv[1]):
        L.append("  %5.1f%%  %s" % (100.0 * c / total, k))
    L.append("")
    L.append("inclusive time by library / part (a sample counts once for each part on its stack):")
    for k, c in sorted(incl_cat.items(), key=lambda kv: -kv[1]):
        L.append("  %5.1f%%  %s" % (100.0 * c / total, k))
    L.append("")
    L.append("top functions, self time:")
    for k, c in sorted(self_fn.items(), key=lambda kv: -kv[1])[:top]:
        L.append("  %5.1f%%  %s" % (100.0 * c / total, k))
    L.append("")
    L.append("self time with other libraries' time charged to the nearest libmain.so caller:")
    for k, c in sorted(caller.items(), key=lambda kv: -kv[1])[:top]:
        L.append("  %5.1f%%  %s" % (100.0 * c / total, k))
    L.append("")
    L.append("top functions, inclusive time:")
    for k, c in sorted(incl_fn.items(), key=lambda kv: -kv[1])[:top]:
        L.append("  %5.1f%%  %s" % (100.0 * c / total, k))
    for ph in sorted(by_phase):
        L.append("")
        L.append("phase %s, self time (%% of all samples):" % (PHASES[ph] if ph < len(PHASES) else ph))
        items = sorted(((k[1], c) for k, c in phase_fn.items() if k[0] == ph), key=lambda kv: -kv[1])
        for k, c in items[:15]:
            L.append("  %5.1f%%  %s" % (100.0 * c / total, k))
    return "\n".join(L) + "\n"


def short(fn):
    fn = re.sub(r"\(.*\)( const)?$", "", fn)
    return fn if len(fn) < 90 else fn[:87] + "..."


def find_symbolizer():
    base = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Android", "Sdk", "ndk")
    if os.path.isdir(base):
        for v in sorted(os.listdir(base), reverse=True):
            for host in ("windows-x86_64", "linux-x86_64", "darwin-x86_64"):
                p = os.path.join(base, v, "toolchains", "llvm", "prebuilt", host, "bin",
                                 "llvm-symbolizer" + (".exe" if os.name == "nt" else ""))
                if os.path.exists(p):
                    return p
    p = shutil.which("llvm-symbolizer")
    if p:
        return p
    raise RuntimeError("no llvm-symbolizer (the NDK's or on PATH)")


def table(dirs):
    rows = ["| run | fps | p99 / max ms | missed vsyncs | 1% / 0.1% low | CPU work ms (p99) | "
            "update / render / swap ms | GPU ms (p95) | draws | passes (screen) | programs | "
            "cameras | AP C start -> end |",
            "| --- " * 13 + "|"]
    for d in dirs:
        try:
            s = json.load(open(os.path.join(d, "summary.json")))
        except OSError:
            continue
        if not s.get("frames"):
            continue
        pf, c, g = s.get("per_frame", {}), s["cpu_ms"], s.get("gpu_ms")
        th = s.get("thermal") or []
        aps = [t["AP"] for t in th if t.get("AP") is not None]
        f = lambda k: ("%.0f" % pf[k]["avg"]) if k in pf and pf[k]["avg"] else "-"
        rows.append("| %s | %.1f | %.2f / %.1f | %d | %.0f / %.0f | %.2f (%.2f) | %.2f / %.2f / %.2f | %s "
                    "| %s | %s (%s) | %s | %.1f | %s |" % (
                        os.path.basename(os.path.normpath(d)), s["fps"], s["interval_ms"]["p99"],
                        s["interval_ms"]["max"], s["missed_vsyncs"], s["low_1pct_fps"],
                        s["low_0.1pct_fps"], s["cpu_work_ms"]["avg"], s["cpu_work_ms"]["p99"],
                        c["update"]["avg"], c["render"]["avg"], c["swap"]["avg"],
                        "%.2f (%.2f)" % (g["avg"], g["p95"]) if g else "-", f("draws"),
                        ("%.1f" % pf["passes"]["avg"]) if pf.get("passes", {}).get("avg") else "-",
                        ("%.1f" % pf["screen_passes"]["avg"]) if pf.get("screen_passes", {}).get("avg") else "-",
                        ("%.1f" % pf["programs"]["avg"]) if pf.get("programs", {}).get("avg") else "-",
                        pf.get("cameras", {}).get("avg", 0),
                        "%.1f -> %.1f" % (aps[0], aps[-1]) if aps else "-"))
    return "\n".join(rows)


# --- main ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    def common(p):
        p.add_argument("--out", required=True, help="results folder")
        p.add_argument("--run", action="append", help="a run SPEC (see the top); repeat for more")
        p.add_argument("--seconds", type=float, default=150, help="default run length (150)")
        p.add_argument("--warm", type=float, default=10,
                       help="seconds before the measured part (loading, first spot; 10)")
        p.add_argument("--window", type=float, default=5, help="TSSM_PERF summary period (5 s)")
        p.add_argument("--display-hz", type=int, default=120,
                       help="what framerate = display means (120, the phone's panel)")
        p.add_argument("--config-video", action="append",
                       help="key=value for the test config's [video], instead of the user's")
        p.add_argument("--assets", default=os.path.join(os.path.dirname(ROOT), "..", "xbox-assets"),
                       help="the game's files (for sb04.ini)")

    p = sub.add_parser("phone", help="a session on the phone")
    common(p)
    p.add_argument("--adb", default=ADB_DEFAULT)
    p.add_argument("--apk", help="the APK to test (installed with -r; the one before is put back)")
    p.add_argument("--lib", help="the unstripped libmain.so, to name the profile's addresses")
    p.add_argument("--symbolizer", help="llvm-symbolizer (default: the NDK's)")
    p.add_argument("--lock", default=None, help="a mkdir lock held for the whole session")
    p.add_argument("--wait-phone", type=int, default=5400, help="seconds to wait for the phone")
    p.add_argument("--wait-screen", type=int, default=3600,
                   help="seconds to wait for the screen, the lock screen or the user's game")
    p.add_argument("--cool-max", type=int, default=1200, help="most seconds to wait for cooling")
    p.add_argument("--sample-every", type=int, default=15, help="temperature samples (15 s)")

    p = sub.add_parser("pc", help="runs on this PC")
    common(p)
    p.add_argument("--exe", required=True)
    p.add_argument("--backend", default="gl3")
    p.add_argument("--size", default="2340x1080", help="render size (the phone's, 2340x1080)")
    p.add_argument("--msaa", default="4")
    p.add_argument("--vsync", default="off")
    p.add_argument("--lock", default=None, help="a lock folder (made with mkdir) held during each run, so other jobs on "
                   "the machine can wait for it")
    p.add_argument("--env", action="append", help="K=V more environment for the game")
    p.add_argument("--capture-at", help="seconds into the measured part to capture frames at, "
                   "comma separated (frames/ in the run's folder)")

    p = sub.add_parser("summary", help="summarize a perf.csv")
    p.add_argument("csv")
    p.add_argument("--skip", type=int, default=0, help="frames to leave out at the start")
    p.add_argument("--exclude", default="", help="frames to leave out (captures), comma separated")
    p.add_argument("--json", help="also write the summary as JSON here")

    p = sub.add_parser("symbolize", help="a profile report from perf-samples.txt")
    p.add_argument("samples")
    p.add_argument("--lib", required=True)
    p.add_argument("--symbolizer")
    p.add_argument("--out")
    p.add_argument("--top", type=int, default=40)

    sub.add_parser("scenes", help="the scenes and what they show")

    p = sub.add_parser("table", help="a Markdown table of runs' summary.json files")
    p.add_argument("runs", nargs="+", help="run folders (each with summary.json)")

    a = ap.parse_args()
    if a.cmd == "phone":
        a.assets = os.path.abspath(a.assets)
        return phone_session(a)
    if a.cmd == "pc":
        a.assets = os.path.abspath(a.assets)
        a.exe = os.path.abspath(a.exe)
        return pc_session(a)
    if a.cmd == "summary":
        sm = summarize(a.csv, a.skip, exclude=[int(x) for x in a.exclude.split(",") if x])
        if a.json:
            with open(a.json, "w") as f:
                json.dump(sm, f, indent=1)
        print(summary_text(sm, a.csv))
        return 0
    if a.cmd == "symbolize":
        rep = symbolize(a.samples, a.lib, a.symbolizer, a.top)
        if a.out:
            open(a.out, "w").write(rep)
        print(rep)
        return 0
    if a.cmd == "table":
        print(table(a.runs))
        return 0
    if a.cmd == "scenes":
        for k, v in SCENES.items():
            print("%-6s %s; %d spots" % (k, v["about"], len(v["spots"])))
        return 0


if __name__ == "__main__":
    sys.exit(main())
