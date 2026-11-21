#!/usr/bin/env python3
"""Scripted runs of the Cheats tab's two halves (src/SB/Core/pc/iCheats.h, docs/CHEATS.md).

    python tools/cheat_runs.py --exe build-pc/tssm.exe --assets DIR --out DIR [--runs LIST]

Each run boots tssm.exe straight into PT03 (no menu, no cutscene, SpongeBob standing on solid
ground) with a config.ini, save folder and log of its own in --out, asks for cheats at given
frames (TSSM_TEST_CHEAT, the same requests the tab makes) and reads the game's state from the
log (TSSM_TEST_CHEATLOG, one line every 10 frames). No picture is compared.

  game     every one of the game's nine button codes, entered from the tab: the cheat word's
           bit, and the code's effect in the game (health units, moves, task counters, points
           multiplier, one costume taking the other off); the Extras screen's switches locked
           without the treasure chests and working with the port's unlock_extras; the port's
           no_damage against a real hit (zEntPlayer_Damage), its refill, and its flight started
           and stopped with white and black; a co-op host's state taken through its wire form,
           the joiner's own request refused, and its own settings back after the release.
  buttons  a code entered the original's way: pause, hold LT and RT, eight presses of X and Y.
  off      nothing asked for: the developers' switches stay at the original's values in every
           line, no cheat is in force, and the only "tssm: cheats:" line is the table check.
  config   [cheats] no_damage = on in config.ini from the start: in force from the first frame
           of the level, and a hit takes nothing.

Run it under the GPU lock when other jobs use the GPU. Exits 0 when every row passed. Only the
Python standard library is used.
"""

import argparse
import os
import re
import subprocess
import sys
import time

LOG_EVERY = 10

# TSSM_TEST_CHEAT for the "game" run; the checks below name the same frames.
GAME_EVENTS = [
    (200, "code:max_health"),
    (220, "code:all_moves"),
    (240, "code:all_macho_moves"),
    (260, "code:all_tasks"),
    (280, "code:double_points"),
    (300, "code:double_points"),
    (320, "code:spongebob_caveman"),
    (340, "code:spongebob_ripped_shorts"),
    (360, "code:patrick_caveman"),
    (380, "code:patrick_goofy_goober"),
    (400, "extra:magnetic_pickup"),
    (420, "port:unlock_extras:on"),
    (440, "extra:magnetic_pickup"),
    (460, "extra:mermaid_spongebob"),
    (480, "extra:tiny_foggers"),
    (500, "extra:tiny_foggers"),
    (520, "hit:1"),
    (540, "refill"),
    (560, "port:no_damage:on"),
    (760, "hit:1"),
    (780, "port:no_damage:off"),
    (980, "hit:1"),
    (1000, "port:fly:on"),
    (1160, "port:fly:off"),
    # A host with no damage on, all health upgrades and SpongeBob's caveman costume entered,
    # the Extras list's third row (Long Buckethead Antennas) on, and points not doubled.
    (1260, "host:1:02000100:4:0:1"),
    (1300, "code:all_tasks"),
    (1320, "release"),
    (1340, "port:alloff"),
    # The host's Extras item is still on, and locked again now: it can be switched off, and
    # then not on.
    (1360, "extra:long_antennas"),
    (1380, "extra:long_antennas"),
]
# Flight: white held with black pressed starts it, Y rises, the same again lands.
GAME_INPUT = "1040:white/30,1050:black/4,1080:y/60,1200:white/30,1210:black/4,9000:start"
GAME_EXIT = 1440

# The original's entry: Start pauses, LT and RT held, then X X X X Y X Y X.
BUTTON_CODE = "xxxxyxyx"
BUTTONS_INPUT = "200:start/4,260:lt+rt/160," + ",".join(
    "%d:lt+rt+%s/4" % (290 + 12 * i, b) for i, b in enumerate(BUTTON_CODE)) + ",9000:start"
BUTTONS_EXIT = 480

LINE = re.compile(
    r"tssm: test: frame (\d+): cheats word 0x([0-9A-F]+) multiplier (\d+) health (\d+)/(\d+) "
    r"max sb (\d+) pat (\d+) powerups ([\d,]+) takedamage (\d+) flytoggle (\d+) flying (-?\d+) "
    r"extras 0x([0-9A-F]{8})([0-9A-F]{8}) chests (\d+) tasks (\d+)/(\d+)/(\d+) port 0x([0-9A-F]+) "
    r"following (\d+) y (-?[\d.]+)")


def run_game(exe, assets, folder, config, env_extra, timeout):
    """One process of our own build with everything it writes in `folder`."""
    for sub in ("save", "mods-none", "frames"):
        os.makedirs(os.path.join(folder, sub), exist_ok=True)
    with open(os.path.join(folder, "config.ini"), "w", newline="\r\n") as f:
        f.write("[video]\naspect = 4:3\nwidth = 640\nheight = 480\nbackend = d3d9\n"
                "mode = windowed\n[game]\nintro_movies = off\nboot = PT03\n" + config)
    env = dict(os.environ)
    for k in [k for k in env if k.upper().startswith("TSSM_")]:
        del env[k]
    env.update({
        "TSSM_ASSETS": assets,
        "TSSM_CONFIG": os.path.join(folder, "config.ini"),
        "TSSM_SAVE_DIR": os.path.join(folder, "save"),
        "TSSM_MODS_DIR": os.path.join(folder, "mods-none"),
        "TSSM_LOG": os.path.join(folder, "tssm.log"),
        "TSSM_CAPTURE_DIR": os.path.join(folder, "frames"),
        "TSSM_TEST_CHEATLOG": str(LOG_EVERY),
        "SDL_AUDIO_DRIVER": "dummy",
    })
    env.update(env_extra)
    t0 = time.time()
    with open(os.path.join(folder, "stdout.txt"), "wb") as out:
        p = subprocess.Popen([exe], cwd=folder, env=env, stdout=out, stderr=subprocess.STDOUT)
        try:
            code = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
            code = "timeout"
    log = os.path.join(folder, "tssm.log")
    text = ""
    if os.path.exists(log):
        with open(log, encoding="latin-1") as f:
            text = f.read()
    return code, text, time.time() - t0


def states(text):
    out = {}
    for m in LINE.finditer(text):
        g = m.groups()
        out[int(g[0])] = {
            "word": int(g[1], 16), "multiplier": int(g[2]), "health": int(g[3]),
            "max": int(g[4]), "sbmax": int(g[5]), "patmax": int(g[6]),
            "powerups": [int(x) for x in g[7].split(",")], "takedamage": int(g[8]),
            "flytoggle": int(g[9]), "flying": int(g[10]),
            "extras": (int(g[11], 16) << 32) | int(g[12], 16), "chests": int(g[13]),
            "not_given": int(g[14]), "given": int(g[15]), "done": int(g[16]),
            "port": int(g[17], 16), "following": int(g[18]), "y": float(g[19]),
        }
    return out


class Rows:
    def __init__(self, run):
        self.run = run
        self.rows = []

    def add(self, what, ok, detail=""):
        self.rows.append((self.run, what, bool(ok), detail))


def check_game(text, code, rows):
    s = states(text)
    rows.add("exited by itself", code == 0, "exit %s" % code)
    rows.add("the tab's codes are the game's",
             "tssm: cheats: the tab's 9 button codes are the game's" in text)
    need = [190, 210, 230, 250, 270, 290, 310, 330, 350, 370, 390, 410, 450, 470, 490, 510, 530,
            550, 570, 750, 770, 790, 970, 990, 1010, 1030, 1070, 1140, 1170, 1250, 1270, 1310, 1330, 1350,
            1370, 1390]
    missing = [f for f in need if f not in s]
    rows.add("state logged at every frame checked", not missing, "missing %s" % missing)
    if missing:
        return

    b = s[190]
    rows.add("before: nothing on",
             b["word"] == 0 and b["multiplier"] == 1 and b["takedamage"] == 1 and
             b["flytoggle"] == 0 and b["extras"] == 0 and b["port"] == 0,
             "word 0x%X, health %d/%d, tasks %d not given" % (b["word"], b["health"], b["max"],
                                                               b["not_given"]))

    a = s[210]
    rows.add("code max_health: six units each, bit 0x100",
             a["word"] == 0x100 and a["sbmax"] == 6 and a["patmax"] == 6 and
             a["powerups"][8:] == [3, 3] and a["health"] == b["health"],
             "max sb %d pat %d, health upgrades %s, health still %d" % (
                 a["sbmax"], a["patmax"], a["powerups"][8:], a["health"]))

    a = s[230]
    rows.add("code all_moves: every move at least 1, bit 0x20000",
             a["word"] == 0x20100 and all(x >= 1 for x in a["powerups"][:8]) and
             a["powerups"][:8] == [max(1, x) for x in b["powerups"][:8]],
             "moves %s (were %s)" % (a["powerups"][:8], b["powerups"][:8]))

    a = s[250]
    rows.add("code all_macho_moves: every move 2, bit 0x400000",
             a["word"] == 0x420100 and a["powerups"][:8] == [2] * 8, "moves %s" % a["powerups"][:8])

    a = s[270]
    rows.add("code all_tasks: no task left ungiven, bit 0x800000",
             a["word"] == 0xC20100 and a["not_given"] == 0 and
             a["given"] == b["given"] + b["not_given"] and a["done"] == b["done"],
             "tasks not given/given/done %d/%d/%d (were %d/%d/%d)" % (
                 a["not_given"], a["given"], a["done"], b["not_given"], b["given"], b["done"]))

    a = s[290]
    rows.add("code double_points: multiplier 2, bit 0x40000",
             a["word"] == 0xC60100 and a["multiplier"] == 2, "multiplier %d" % a["multiplier"])
    a = s[310]
    rows.add("code double_points again: bit off, multiplier stays 2 (the original's)",
             a["word"] == 0xC20100 and a["multiplier"] == 2,
             "word 0x%X multiplier %d" % (a["word"], a["multiplier"]))

    a = s[330]
    rows.add("code spongebob_caveman: bit 0x2000000", a["word"] == 0x2C20100, "0x%X" % a["word"])
    a = s[350]
    rows.add("code spongebob_ripped_shorts: takes the caveman costume off",
             a["word"] == 0x4C20100, "0x%X" % a["word"])
    a = s[370]
    rows.add("code patrick_caveman: bit 0x10000000, SpongeBob's kept",
             a["word"] == 0x14C20100, "0x%X" % a["word"])
    a = s[390]
    rows.add("code patrick_goofy_goober: takes Patrick's caveman costume off",
             a["word"] == 0xCC20100, "0x%X" % a["word"])

    rows.add("extra without the treasure chests: stays locked",
             "tssm: cheats: extra magnetic_pickup stays locked" in text and s[410]["extras"] == 0,
             "extras 0x%X with %d chests" % (s[410]["extras"], s[410]["chests"]))
    a = s[450]
    rows.add("port unlock_extras, then the extra: on", a["extras"] != 0 and a["port"] == 0x4 and
             "tssm: cheats: extra magnetic_pickup switched on from the tab" in text,
             "extras 0x%X" % a["extras"])
    magnet = a["extras"]
    a = s[470]
    rows.add("extra mermaid_spongebob: on beside it",
             a["extras"] & magnet and bin(a["extras"]).count("1") == 2 and
             "tssm: cheats: extra mermaid_spongebob switched on from the tab" in text,
             "extras 0x%X" % a["extras"])
    two = a["extras"]
    rows.add("extra tiny_foggers: on, then off again",
             bin(s[490]["extras"]).count("1") == 3 and s[510]["extras"] == two,
             "0x%X then 0x%X" % (s[490]["extras"], s[510]["extras"]))

    rows.add("a hit with no cheat: one health unit gone",
             s[530]["health"] == s[510]["health"] - 1,
             "%d to %d" % (s[510]["health"], s[530]["health"]))
    rows.add("refill: health at the maximum", s[550]["health"] == s[550]["max"] == 6,
             "%d/%d" % (s[550]["health"], s[550]["max"]))
    rows.add("port no_damage on: the developers' switch", s[570]["takedamage"] == 0 and
             s[570]["port"] == 0x5 and "tssm: cheats: no damage on" in text,
             "takedamage %d" % s[570]["takedamage"])
    rows.add("a hit with no_damage: no health gone", s[770]["health"] == s[750]["health"] == 6,
             "%d to %d" % (s[750]["health"], s[770]["health"]))
    rows.add("port no_damage off: the original's value back", s[790]["takedamage"] == 1 and
             "tssm: cheats: no damage off" in text, "takedamage %d" % s[790]["takedamage"])
    rows.add("a hit after it: one health unit gone", s[990]["health"] == s[970]["health"] - 1,
             "%d to %d" % (s[970]["health"], s[990]["health"]))

    rows.add("port fly on: the developers' switch, not flying yet",
             s[1010]["flytoggle"] == 1 and s[1010]["flying"] == 0 and s[1030]["flying"] == 0,
             "flytoggle %d" % s[1010]["flytoggle"])
    rows.add("white held, black pressed: flying", s[1070]["flying"] == 1,
             "flying %d" % s[1070]["flying"])
    rows.add("Y held: rising", s[1140]["y"] > s[1070]["y"] + 1.0,
             "y %.2f to %.2f" % (s[1070]["y"], s[1140]["y"]))
    rows.add("port fly off in mid-air: the buttons stay until landed",
             s[1170]["flytoggle"] == 1 and s[1170]["flying"] == 1 and s[1170]["port"] == 0x4,
             "flytoggle %d flying %d" % (s[1170]["flytoggle"], s[1170]["flying"]))
    rows.add("white and black again: landed, and the switch back at the original's",
             s[1250]["flying"] == 0 and s[1250]["flytoggle"] == 0 and
             "tssm: cheats: flight off" in text,
             "flytoggle %d flying %d" % (s[1250]["flytoggle"], s[1250]["flying"]))

    a = s[1270]
    rows.add("co-op: the host's state taken",
             a["following"] == 1 and a["word"] == 0x02000100 and a["extras"] == 0x4 and
             a["multiplier"] == 1 and a["takedamage"] == 0 and a["port"] == 0x1 and
             "taken from the host" in text,
             "word 0x%X extras 0x%X multiplier %d takedamage %d" % (
                 a["word"], a["extras"], a["multiplier"], a["takedamage"]))
    rows.add("co-op: a joiner's own request refused",
             "cheat code:all_tasks: not understood, or refused" in text and
             s[1310]["word"] == 0x02000100, "word 0x%X" % s[1310]["word"])
    a = s[1330]
    rows.add("co-op: released, its own settings back (unlock_extras only)",
             a["following"] == 0 and a["takedamage"] == 1 and a["port"] == 0x4,
             "port 0x%X takedamage %d" % (a["port"], a["takedamage"]))
    rows.add("all port cheats off", s[1350]["port"] == 0 and s[1350]["takedamage"] == 1 and
             s[1350]["flytoggle"] == 0, "port 0x%X" % s[1350]["port"])
    rows.add("an extra left on, locked again: can be switched off",
             s[1350]["extras"] == 0x4 and s[1370]["extras"] == 0 and
             "tssm: cheats: extra long_antennas switched off from the tab" in text,
             "extras 0x%X to 0x%X" % (s[1350]["extras"], s[1370]["extras"]))
    rows.add("and not on again: stays locked",
             s[1390]["extras"] == 0 and "tssm: cheats: extra long_antennas stays locked" in text,
             "extras 0x%X" % s[1390]["extras"])


def check_buttons(text, code, rows):
    s = states(text)
    rows.add("exited by itself", code == 0, "exit %s" % code)
    if 190 not in s or 470 not in s:
        rows.add("state logged", False, "frames 190 and 470 not both logged")
        return
    b, a = s[190], s[470]
    rows.add("before: nothing on", b["word"] == 0 and b["sbmax"] == 3 and b["patmax"] == 3,
             "word 0x%X max sb %d pat %d" % (b["word"], b["sbmax"], b["patmax"]))
    rows.add("X X X X Y X Y X on the pause screen: all health upgrades",
             a["word"] == 0x100 and a["sbmax"] == 6 and a["patmax"] == 6 and
             a["powerups"][8:] == [3, 3],
             "word 0x%X max sb %d pat %d" % (a["word"], a["sbmax"], a["patmax"]))
    rows.add("entered by the game, not the tab", "entered from the tab" not in text)


def check_off(text, code, rows):
    s = states(text)
    rows.add("exited by itself", code == 0, "exit %s" % code)
    rows.add("state logged", len(s) >= 40, "%d lines" % len(s))
    bad = [f for f, v in sorted(s.items())
           if v["word"] or v["multiplier"] != 1 or v["takedamage"] != 1 or v["flytoggle"] or
           v["flying"] or v["extras"] or v["port"] or v["following"]]
    rows.add("the original's values in every line", not bad and bool(s), "differs at %s" % bad[:5])
    lines = [l for l in text.splitlines() if l.startswith("tssm: cheats:")]
    rows.add("no cheat line but the table check",
             lines == ["tssm: cheats: the tab's 9 button codes are the game's"], "%s" % lines[:3])


def check_config(text, code, rows):
    s = states(text)
    rows.add("exited by itself", code == 0, "exit %s" % code)
    if 250 not in s or 270 not in s:
        rows.add("state logged", False, "frames 250 and 270 not both logged")
        return
    rows.add("[cheats] no_damage = on: in force from the level's start",
             all(v["takedamage"] == 0 and v["port"] == 0x1 for f, v in s.items() if f >= 100),
             "takedamage %d at frame 250" % s[250]["takedamage"])
    rows.add("a hit: no health gone", s[270]["health"] == s[250]["health"],
             "%d to %d" % (s[250]["health"], s[270]["health"]))
    rows.add("the other developers' switch untouched",
             all(v["flytoggle"] == 0 for v in s.values()))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--exe", required=True)
    ap.add_argument("--assets", default=os.environ.get("TSSM_ASSETS"))
    ap.add_argument("--out", required=True)
    ap.add_argument("--runs", default="game,buttons,off,config")
    ap.add_argument("--timeout", type=float, default=120.0)
    args = ap.parse_args()
    if not args.assets:
        sys.exit("--assets (or TSSM_ASSETS) is needed")
    exe = os.path.abspath(args.exe)
    out = os.path.abspath(args.out)

    runs = {
        "game": ("", {"TSSM_TEST_CHEAT": ",".join("%d:%s" % e for e in GAME_EVENTS),
                      "TSSM_INPUT": GAME_INPUT, "TSSM_EXIT_AFTER": str(GAME_EXIT)}, check_game),
        "buttons": ("", {"TSSM_INPUT": BUTTONS_INPUT, "TSSM_EXIT_AFTER": str(BUTTONS_EXIT)},
                    check_buttons),
        "off": ("", {"TSSM_INPUT": "9000:start", "TSSM_EXIT_AFTER": "600"}, check_off),
        "config": ("[cheats]\nno_damage = on\n",
                   {"TSSM_TEST_CHEAT": "260:hit:1", "TSSM_INPUT": "9000:start",
                    "TSSM_EXIT_AFTER": "300"}, check_config),
    }

    all_rows = []
    for name in args.runs.split(","):
        if name not in runs:
            sys.exit("unknown run '%s'" % name)
        config, env, check = runs[name]
        folder = os.path.join(out, name)
        code, text, seconds = run_game(exe, args.assets, folder, config, env, args.timeout)
        rows = Rows(name)
        check(text, code, rows)
        all_rows += rows.rows
        print("%s: exit %s after %.1f s, %d log lines" % (name, code, seconds,
                                                        len(text.splitlines())), flush=True)

    failed = 0
    for run, what, ok, detail in all_rows:
        failed += 0 if ok else 1
        print("%-4s %-8s %s%s" % ("PASS" if ok else "FAIL", run, what,
                                  " [%s]" % detail if detail else ""))
    print("%d of %d rows passed" % (len(all_rows) - failed, len(all_rows)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
