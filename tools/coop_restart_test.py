#!/usr/bin/env python3
"""Online co-op: the guest's game ends without a goodbye (killed, as a crash)
and is started again; its new join must take the silent session's place on
the host (docs/MULTIPLAYER.md 8.3, kReplaceSilence) before the host's 10 s
timeout, and the shared world must come back. Two copies of our own build on
127.0.0.1, DE01, through tools/coop_test.py's helpers.

    python tools/coop_restart_test.py --exe build-x/tssm.exe --assets DIR --out DIR
                                      [--gap SECONDS] [--guest-exe ...] [--port N]

--gap is the time between the kill and the new start (4 by default). With a
short gap (0 or 0.5) the new game's first join reaches the host while its old
session has been silent for less than 3 s: the host still answers FULL, and
the joining game must ask again each second for a few seconds (iCoop.cpp,
kFirstFullFor) instead of giving up.

--third does not kill anyone: while the guest plays, a third copy joins with
the right password. A game that really is full: the third copy asks for its
6 seconds, is then told so in words (captured in its Co-op tab), and the two
players' session never notices.

Each copy gets its own folder, config.ini and save folder under --out; the
script stops only its own copies.
"""
import argparse
import os
import re
import secrets
import subprocess
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import coop_test as ct  # noqa: E402

KILL_AT = 1800  # the host's frame at which the first guest is killed


def wait_for(path, text, limit):
    t0 = time.time()
    while time.time() - t0 < limit:
        try:
            if text in open(path, "rb").read().decode("utf-8", "replace"):
                return True
        except OSError:
            pass
        time.sleep(0.05)
    return False


def third_rows(row, rows, procs, logs, out, frames):
    """--third: the third copy is told the game is full; the session goes on."""
    h, l1, l3 = logs["host"], logs["g1"], logs["g2"]
    asks = len(re.findall(r"the host's game has a player; asking again", l3))
    row("the third copy asked again for its seconds (%d times)" % asks, 3 <= asks <= 8)
    m = re.search(r"coop: the join was refused: (.*)", l3)
    row("then it was told the game is full", m is not None and "already has" in m.group(1), m.group(1) if m else "")
    row("it never connected", "coop: connected (joined)" not in l3)
    row("it did not ask on after being told", "asking again" not in l3[m.end():] if m else False)
    row("its Co-op tab was captured", os.path.exists(os.path.join(frames, "coop_tab.png")))
    row("the host kept its player (no replacing, no timeout)",
        "was replaced by a new join" not in h and "joined again" not in h and "nothing heard for 10 seconds" not in h)
    row("the first guest noticed nothing", "connection is interrupted" not in l1 and "joining again" not in l1)
    moments, pairs, worst, where = ct.compare_syncs(h, l1)
    row("same places at the same moments, host and first guest (<= 0.25)", moments > 3 and worst <= 0.25,
        "%d moments, %d positions, worst %.3f %s" % (moments, pairs, worst, where))
    for k, log in (("host", h), ("g1", l1), ("third", l3)):
        bad = [x for x in ("protocol error", "level mismatch", "unknown animation", "Assertion") if x in log]
        row("%s: no errors" % k, not bad, str(bad))
    for k in ("host", "g1", "g2"):
        row("%s exited by itself" % k, procs[k].returncode == 0, str(procs[k].returncode))
    for what, good, detail in rows:
        print("%-90s %s  %s" % (what, "PASS" if good else "FAIL", detail))
    n = sum(1 for r in rows if r[1])
    print("%s: %d of %d (%s)" % ("PASS" if n == len(rows) else "FAIL", n, len(rows), out))
    return 0 if n == len(rows) else 1


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--exe", required=True, help="our own tssm.exe (never a player's build)")
    p.add_argument("--guest-exe")
    p.add_argument("--assets", default=os.environ.get("TSSM_ASSETS"))
    p.add_argument("--out", required=True)
    p.add_argument("--port", type=int, default=24660)
    p.add_argument("--gap", type=float, default=4.0, help="seconds between the kill and the new start")
    p.add_argument("--third", action="store_true", help="no kill: a third copy joins a game that really is full")
    a = p.parse_args()
    if not a.assets or not os.path.exists(os.path.join(os.path.abspath(a.assets), "sb04.ini")):
        sys.exit("--assets: no sb04.ini in %s" % a.assets)
    exe = os.path.abspath(a.exe)
    gexe = os.path.abspath(a.guest_exe) if a.guest_exe else exe
    assets = os.path.abspath(a.assets)
    name = "full_third" if a.third else "restart_gap%g" % a.gap
    out = os.path.join(os.path.abspath(a.out), "%s_%s" % (name, time.strftime("%Y%m%d_%H%M%S")))
    hd, g1, g2 = (os.path.join(out, n) for n in ("host", "guest1", "guest2"))
    for d in (hd, g1, g2):
        os.makedirs(d)
    sc = ct.SCENES["DE01"]
    mod = ct.boot_mod(assets, out, "DE01")
    pw = secrets.token_hex(8)
    host_t = {"exit": 5200, "frames": [KILL_AT, 4600, 4800], "hud": KILL_AT + 170, "gui": 5100}
    g1_t = {"exit": 4000, "frames": [1400], "hud": 1300, "gui": 3900}
    g2_t = {"exit": 2100, "frames": [1700, 1850], "hud": 1750, "gui": 2000}
    if a.third:
        # The third copy joins at its start; by this frame it has been told.
        g2_t = {"exit": 1500, "frames": [1100], "hud": 1000, "gui": 1400}
    extra = {"TSSM_COOP_SYNC_CAPTURE": "1000:150"}
    procs, files = {}, []

    def start(role, d, script, place, timing, which):
        env = ct.env_for(role, d, assets, mod, a.port, pw, script, dict(extra), timing, place)
        o = open(os.path.join(d, "log.txt"), "wb")
        e = open(os.path.join(d, "stderr.txt"), "wb")
        files.extend([o, e])
        return subprocess.Popen([which], cwd=d, env=env, stdout=o, stderr=e)

    procs["host"] = start("host", hd, sc["host_script"], sc["host_place"], host_t, exe)
    time.sleep(1.0)
    procs["g1"] = start("join", g1, sc["guest_script"], sc["guest_place"], g1_t, gexe)
    seen = wait_for(os.path.join(hd, "log.txt"), "capture: frame %d " % KILL_AT, 240)
    t_kill = time.time()
    if not a.third:
        procs["g1"].kill()  # our own copy: as a crash, no goodbye
        procs["g1"].wait()
        print("the host reached frame %d: %s; the first guest was killed at %s" % (KILL_AT, seen,
                                                                                time.strftime("%H:%M:%S")))
        time.sleep(a.gap)
    procs["g2"] = start("join", g2, sc["guest_script"], sc["guest_place"], g2_t, gexe)
    print("the second guest started %.1f s after the kill" % (time.time() - t_kill))
    t0 = time.time()
    for k in ("g2", "g1", "host") if a.third else ("g2", "host"):
        try:
            procs[k].wait(max(1, 400 - (time.time() - t0)))
        except subprocess.TimeoutExpired:
            procs[k].kill()
            procs[k].wait()
            print("TIMEOUT", k)
    for f in files:
        f.close()
    logs = {k: open(os.path.join(d, "log.txt"), "rb").read().decode("utf-8", "replace")
            for k, d in (("host", hd), ("g1", g1), ("g2", g2))}
    h, l2 = logs["host"], logs["g2"]
    rows = []

    def row(what, good, detail=""):
        rows.append((what, bool(good), detail))

    row("the first guest connected", "coop: connected (joined)" in logs["g1"])
    if a.third:
        return third_rows(row, rows, procs, logs, out, os.path.join(g2, "frames"))
    replaced = h.find("a silent player's session was replaced by a new join")
    row("the host let the silent session go for the new join", replaced >= 0)
    row("the host: \"the other player joined again\"", "the other player joined again" in h)
    timeout = h.find("nothing heard for 10 seconds")
    row("no timeout on the host before that", replaced >= 0 and (timeout < 0 or timeout > replaced))
    full = len(re.findall(r"the host's game has a player; asking again", l2))
    row("the second guest connected (asking again after FULL: %d times)" % full,
        "coop: connected (joined)" in l2, "")
    row("the second guest was never refused", "join was refused" not in l2)
    m = re.findall(r"tssm: coop: level DE01 shares (\d+) things", l2)
    row("the second guest built DE01's table", bool(m), str(m[:1]))
    row("the second guest showed the host's world", "coop: the host's world is shown" in l2)
    row("both draw the other", "partner frame" in l2 and h.count("partner frame") > 50)
    for k in ("host", "g2"):
        bad = [x for x in ("protocol error", "level mismatch", "unknown animation", "Assertion") if x in logs[k]]
        row("%s: no errors" % k, not bad, str(bad))
    moments, pairs, worst, where = ct.compare_syncs(h, l2)
    row("same places at the same moments, host and second guest (<= 0.25)", moments > 3 and worst <= 0.25,
        "%d moments, %d positions, worst %.3f %s" % (moments, pairs, worst, where))
    chk, w2, wh2, _ = ct.compare_shown(h, l2)
    row("everything the second guest drew is where the host had it (<= 0.25)", chk > 100 and w2 <= 0.25,
        "%d positions, worst %.3f %s" % (chk, w2, wh2))
    row("the host exited by itself", procs["host"].returncode == 0, str(procs["host"].returncode))
    row("the second guest exited by itself", procs["g2"].returncode == 0, str(procs["g2"].returncode))
    for what, good, detail in rows:
        print("%-90s %s  %s" % (what, "PASS" if good else "FAIL", detail))
    n = sum(1 for r in rows if r[1])
    print("%s: %d of %d (%s)" % ("PASS" if n == len(rows) else "FAIL", n, len(rows), out))
    return 0 if n == len(rows) else 1


if __name__ == "__main__":
    sys.exit(main())
