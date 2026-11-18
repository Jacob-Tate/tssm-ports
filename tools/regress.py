#!/usr/bin/env python3
"""Regression run for the PC port: both widths to gameplay, frames against references, unit tests.

    python tools/regress.py --assets DIR --reference DIR --out DIR [options]

What it does, in order:

  1. (--build) builds each width's existing build directory with `cmake --build`
     inside that width's MSVC environment, or, for a directory configured with
     MinGW's gcc, with the CMake it was configured with and the MinGW folder
     first on PATH. It never configures: a build directory is made once with
     build-debug.bat (or the cmake lines in src/SB/Core/pc/README.md), and its
     cache keeps the compiler and the FFmpeg prefix it was made with.
  2. runs tssm.exe of each width with the same scripted controller (TSSM_INPUT)
     from the legal screen through the title, New Game, the autosave prompt, the
     first save, story1.bik and BB02's opening cutscene into gameplay, capturing
     a fixed list of frames (TSSM_CAPTURE_FRAMES) and exiting at a fixed frame
     (TSSM_EXIT_AFTER). Each run is one process of our own build, stopped only by
     its own exit or, after --timeout seconds (88 by default), by this script,
     which stops only that process. Before a run starts the script waits until
     no other tssm.exe is running (a second Direct3D device has been seen to
     fail to open while one is), and a run whose device failed to open is tried
     again.
  3. checks each run: it exited by itself at the exit frame, every frame was
     captured, the game's own log (TSSM_LOG, here tssm.log in the run's folder)
     holds every line the run printed, and (64-bit) the asset widening was
     installed and widened something, with no asset it could not widen.
  4. compares each captured frame with the stored reference frame of the same
     width, perceptually: both pictures are reduced to 16x16-pixel block
     averages, and a frame passes when the mean difference of the blocks and the
     share of blocks that differ by more than 24 levels are both within the
     frame's tolerance (stills tight, menus a little looser, the movie and
     gameplay loose enough for timing and particles; TOLERANCE says what was
     measured). A frame may also match a stored variant of its reference
     (frame_NNNNN.altK.png), or the other width's reference or variants: some
     pictures come out one of two ways from run to run at either width (in
     BB02 a lighter patch on the ground in front of SpongeBob is there in some
     runs and not in others), and x64 is meant to look like x86. The x64
     frames are also compared with this run's x86 frames (or x86's stored
     pictures).
  5. runs two short level runs per width that the run to gameplay never shows
     (RUNS says why each is there). Each boots straight into its level through
     a mod of sb04.ini (BOOT=<scene>, ShowMenuOnBoot=0; TSSM_MOD), with no
     button pressed, and is checked as in 3:
     - de01_fly: DE01's level-start fly-through. Its frames are compared with
       references as in 4, and the camera, which the game logs every frame
       (TSSM_TEST_CAMLOG), has to stay finite, inside the level and moving
       smoothly while the fly-through plays (check_camera; no reference
       needed).
     - pt03_belt: a conveyor belt over PT03's lava, with the player put down in
       front of it and held still (TSSM_TEST_PLACE, TSSM_TEST_HOLD). Its frames
       are compared with references as in 4, and the belt has to move between
       two frames 12 apart (check_scroll; no reference needed).
     - bb02_crowd_xbox, bb02_crowd_gamecube: No Cheese!'s fish crowd, with the
       player put down facing it and held still, once with [extras] crowds =
       xbox and once with gamecube. Checked by themselves (check_crowd): the
       crowd has to move between two frames 40 apart with xbox and stand still
       with gamecube. No reference frames.
     - bb02_xbox: the run to gameplay again with every [extras] key at xbox:
       the title with the Xbox menu glow, and BB02 with the Xbox glow (0.55),
       its spotlights and its crowd. Its frames are compared with references
       of their own.
     - tt01_spot_xbox, tt01_spot_gamecube: TT01's disco, its script started
       (TSSM_TEST_SEND) with the player held there, once with [extras]
       spotlights = xbox and once with gamecube. The scene's spotlights are
       logged (TSSM_TEST_SPOTLOG) and checked (check_spotlights): the 12 disco
       lights, which only the Xbox files have, come on with xbox and never
       with gamecube. No reference frames (the disco moves over the whole
       picture).
     - bb01_crowd_xbox, bb01_crowd_gamecube: Sandwich Driving 101's crowd
       with the car held by it, its objects logged (TSSM_TEST_CROWDLOG) and
       checked (check_crowd_log): their cycle runs with xbox, and with
       gamecube all 22 stand at speed 0.
     - sc02_water_xbox, sc02_water_gamecube, b301_water_xbox,
       b301_water_gamecube: the two levels with water, once with [extras]
       water = xbox and once with gamecube. The water's own log lines are
       checked (check_water): with xbox the Xbox water is set up for every
       body and its surface drawn, with gamecube the GameCube water is used
       because the setting says so, and the level's extras line agrees. No
       reference frames.
     --quick leaves them out.

     Every run's config.ini sets the five [extras] keys (the Xbox release's
     features, src/SB/Core/pc/iExtras.h) to gamecube, except the one a run is
     about, so the references show the GameCube versions and a change to an
     xbox version cannot move them.
  6. runs every unit test of each width: save_selftest, tssm_input_test,
     tssm_modtest, tssm_sndtest (boot.HIP and MN/mnuc.HIP) and tssm_widentest.
  7. prints a pass/fail table, writes report.json and report.html (the frames
     side by side with their references) into --out, and exits 0 when every row
     passed.

References are made by a run with --update-reference, after the frames have
been looked at: it copies this run's frames over the references (per width and
run, dropping their variants) instead of comparing. Only the runs made are
updated: `--update-reference --runs de01_fly,pt03_belt` leaves the run to
gameplay's references as they are. --add-variants stores each frame of this
run that matched nothing as a new variant of its reference, for a picture that
has been looked at and found to be a second right answer. References and
captures are game frames: keep them out of the repository (--reference and
--out are outside it).

Options:
  --assets DIR        the Xbox asset folder (else TSSM_ASSETS)
  --reference DIR     where the reference frames are: <width>/frame_NNNNN.png
                      for the run to gameplay, <width>/<run>/frame_NNNNN.png for
                      the level runs (else TSSM_REGRESS_REFERENCE)
  --out DIR           where this run's folders and the report go; a run folder is
                      made in it per invocation (else TSSM_REGRESS_OUT)
  --widths x86,x64    which widths (both by default)
  --runs LIST         which game runs: bb02 (the run to gameplay), de01_fly,
                      pt03_belt (all three by default)
  --quick             the run to gameplay only, no level runs (--runs bb02)
  --build             build first
  --build-dir-x64 D   default build-pc;  --build-dir-x86 D  default build-pc-x86
  --skip-runs         unit tests only;  --skip-tests  runs only
  --from-run DIR      score the runs in an earlier run folder (DIR/run_x86,
                      DIR/run_de01_fly_x64, ...) instead of running the game again
  --update-reference  store this run's frames as the references
  --add-variants      store this run's unmatched frames as variants
  --backend NAME      the renderer for the runs (d3d9 by default); gl3 with
                      TSSM_GL_NO_S3TC=1 in the environment widens the still
                      frame's limit (TOLERANCE_NO_S3TC)
  --size WxH          render at exactly this size (video.aspect = custom): the
                      widescreen pass, against references of its own (e.g.
                      --size 1280x720 --reference .../reference_wide). The run
                      to gameplay also pauses at 3050 and captures the pause
                      menu at 3150; the belt crop is placed where the belt is
                      in a Hor+/Vert+ picture of that size. Without it every
                      run is pinned to 4:3 at 640x480, which is what the
                      default references are.
  --timeout S         seconds the run to gameplay may take (88); the level runs
                      get their own limits, scaled by S/88
  --wait S            seconds to wait for another tssm.exe to finish (600)

Only the Python standard library is used (the PNG reader is here: captures are
written uncompressed by iFrameGrabWrite, any PNG of 8-bit RGB or RGBA reads).
"""

import argparse
import datetime
import html
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# The scripted path to gameplay (TSSM_INPUT, port 0; frame numbers are frames
# presented, intro movies off). Start at the title, A on New Game, down and A
# on the autosave prompt (YES), A on the first memory card slot (the first save
# is written), A through story1.bik and BB02's opening cutscene, then gameplay.
SCRIPT = ("400:start,700:a,1000:down,1100:a,1350:a,1650:a,1950:a,2150:a,2350:a,"
          "2600:x,2800:b")
EXIT_FRAME = 3300

# The [video] lines every run's config.ini starts with. The port's default
# aspect is the display's shape, so the runs pin 4:3 at 640x480: that is what
# the references are. --size replaces this (main).
VIDEO = "aspect = 4:3\nwidth = 640\nheight = 480\n"

# Frames compared, each with its tolerance class.
FRAMES = [
    (4, "still"),      # the legal screen
    (100, "menu"),     # the title (its surf moves)
    (400, "menu"),     # the title, as Start is pressed
    (700, "menu"),     # the main menu
    (1000, "menu"),    # the autosave prompt
    (1300, "menu"),    # the memory card choice
    (1800, "movie"),   # story1.bik
    (2200, "game"),    # BB02, gameplay, with the HUD
    (2500, "game"),
    (2700, "game"),
    (3000, "game"),
    (3200, "game"),
]

# The level runs (step 5). Each catches a bug that reached players because the
# run to gameplay cannot show it:
#
# de01_fly: the level-start fly-throughs ('FLY ' assets, zCameraFlyUpdate in
# zCamera.cpp). The console byte-swaps their keys, which are little-endian in
# the files; on the port the swap made every key garbage and put the camera
# into the floor or far outside the world for the whole fly-through. BB02 opens
# with an xCutscene, not a fly-through. DE01's (de01_flythrough_gary, 380 keys,
# about 12.7 s) starts on its own when the level begins; the frames are taken
# while it plays, and TSSM_TEST_CAMLOG=1 logs the camera every frame for
# check_camera.
#
# pt03_belt: scrolling textures (surface UV effects drawn as MatFX UV
# transforms; tools/librw/matfx_uvtransform.patch). Without them the belts and
# the lava stood still while the game still moved the player along. The player
# is put down in front of CONV_BELT_02 (CONVEYOR_SURF_01 scrolls one texture
# length a second) and held there, so the camera is still and frames 300 and
# 312 differ only by what animates; check_scroll compares BELT_CROP.
#
# Neither run presses a button, but each sets TSSM_INPUT (one press after its
# exit frame, never reached) so a controller someone has plugged in cannot
# skip the fly-through or move the player.
RUN_BB02 = {
    "name": "bb02", "item": "run to gameplay", "boot": None, "script": SCRIPT,
    "exit": EXIT_FRAME, "frames": FRAMES, "env": {}, "timeout": None,
    "about": "to frame %d, about a minute" % EXIT_FRAME,
}
RUN_DE01_FLY = {
    "name": "de01_fly", "item": "DE01 fly-through", "boot": "DE01",
    "script": "1460:start", "exit": 460,
    "frames": [(120, "game"), (200, "game"), (320, "game"), (420, "game")],
    "env": {"TSSM_TEST_CAMLOG": "1"}, "timeout": 60,
    "about": "boot DE01, its fly-through, to frame 460",
}
RUN_PT03_BELT = {
    "name": "pt03_belt", "item": "PT03 conveyor belt", "boot": "PT03",
    "script": "1340:start", "exit": 340,
    "frames": [(300, "game"), (312, "game")],
    "env": {"TSSM_TEST_PLACE": "150:-39.3:21.5:-337.1:90", "TSSM_TEST_HOLD": "151-340"},
    "timeout": 60,
    "about": "boot PT03, held in front of a belt, to frame 340",
}
# The Xbox extras ([extras] in config.ini, src/SB/Core/pc/iExtras.h). Every run
# above pins all five to gamecube (EXTRAS), so the references show the GameCube
# release's versions and a change to an xbox version cannot move them. These
# runs are the xbox ones, each checked by itself (no reference frames: the
# crowd's cycle is not at the same point from run to run).
#
# bb02_crowd_xbox, bb02_crowd_gamecube: No Cheese!'s fish crowd behind the
# barrier, with the player put down facing it and held still (as pt03_belt).
# check_crowd compares CROWD_CROP of frames 2500 and 2540: with crowds = xbox
# it has to move, with gamecube it has to stand still.
EXTRAS = ("glow", "spotlights", "crowds", "water", "frame_lock")
# A taps every 100 frames up to 1500 skip story1.bik and BB02's opening cutscene
# (left to play, the cutscene was still on at 1840 in one run of three).
RUN_BB02_CROWD_XBOX = {
    "name": "bb02_crowd_xbox", "item": "BB02 crowd (xbox)", "boot": "BB02",
    "script": ",".join("%d:a/4" % f for f in range(300, 1501, 100)), "exit": 2560,
    "frames": [(2500, "game"), (2540, "game")],
    "env": {"TSSM_TEST_PLACE": "2400:-41.0:7.0:7.0:320", "TSSM_TEST_HOLD": "2401-2560"},
    "timeout": 100, "extras": {"crowds": "xbox"}, "compare": False,
    "about": "boot BB02, held facing the crowd, to frame 2560, crowds xbox",
}
RUN_BB02_CROWD_GC = dict(RUN_BB02_CROWD_XBOX, name="bb02_crowd_gamecube", item="BB02 crowd (gamecube)",
                         extras={"crowds": "gamecube"},
                         about="boot BB02, held facing the crowd, to frame 2560, crowds gamecube")
# bb02_xbox: the run to gameplay with every extra at xbox, against references
# of its own: the title (the menu glow), the main menu, and BB02 (the Xbox
# glow at 0.55, the spotlights, the crowd).
RUN_BB02_XBOX = {
    "name": "bb02_xbox", "item": "run to gameplay (xbox)", "boot": None, "script": SCRIPT,
    "exit": EXIT_FRAME,
    "frames": [(100, "menu"), (400, "menu"), (700, "menu"), (2200, "game"), (2500, "game"),
               (2700, "game"), (3000, "game")],
    "env": {}, "timeout": None, "extras": dict((k, "xbox") for k in EXTRAS),
    "about": "to frame %d with every [extras] key at xbox" % EXIT_FRAME,
}
# tt01_spot_xbox, tt01_spot_gamecube: Bubble Blowing Baby Hunt's disco lights,
# which only the Xbox files have and which the disco machine's script
# (DISCO_SCRIPT_01) switches on 5 s after it starts. The script is started at
# TT01_SPOT_SEND, the player put down in the disco and held there, and the
# scene's spotlights logged every 10 frames (TSSM_TEST_SPOTLOG). The only
# buttons are two B presses that close Mindy's tokens message, which opens
# after the player is put down (an A by the level's start opens the warp menu
# instead). check_spotlights: with
# spotlights = xbox the 12 disco lights have to come on; with gamecube none of
# them may, through the whole script. TT01 is one of the GameCube's 30 fps
# levels, so both runs set frame_lock = xbox and run at 60. No reference
# frames: the disco's floor dots and lasers move over the whole picture, and two
# runs differ by a mean of 10 at the same frame; the frames are kept for the
# report.
TT01_SPOT_SEND = 700
RUN_TT01_SPOT_XBOX = {
    "name": "tt01_spot_xbox", "item": "TT01 disco lights (xbox)", "boot": "TT01",
    "script": "1050:b,1150:b", "exit": 1710,
    "frames": [(1300, "game"), (1500, "game")],
    "env": {"TSSM_TEST_SEND": "%d:DISCO_SCRIPT_01" % TT01_SPOT_SEND, "TSSM_TEST_SPOTLOG": "10",
            "TSSM_TEST_PLACE": "750:160:12:-150:127", "TSSM_TEST_HOLD": "751-1710"},
    "timeout": 80, "extras": {"spotlights": "xbox", "frame_lock": "xbox"}, "compare": False,
    "about": "boot TT01, the disco started at %d, to frame 1710, spotlights xbox" % TT01_SPOT_SEND,
}
RUN_TT01_SPOT_GC = dict(RUN_TT01_SPOT_XBOX, name="tt01_spot_gamecube",
                        item="TT01 disco lights (gamecube)",
                        extras={"spotlights": "gamecube", "frame_lock": "xbox"},
                        about="boot TT01, the disco started at %d, to frame 1710, spotlights "
                              "gamecube" % TT01_SPOT_SEND)
# bb01_crowd_xbox, bb01_crowd_gamecube: Sandwich Driving 101's fish crowd,
# with the Patty Wagon put down by one of its groups and held (BB01 is a
# GameCube 30 fps level: frame_lock = xbox in both). The held car's camera
# still sways, so frames cannot show the crowd's own motion; the crowd objects
# are logged every 60 frames instead (TSSM_TEST_CROWDLOG) and checked
# (check_crowd_log): with xbox the cycle runs at speed 1 and the crowd in view
# moves on in it, with gamecube every one of the 22 is at speed 0 and none
# moves.
RUN_BB01_CROWD_XBOX = {
    "name": "bb01_crowd_xbox", "item": "BB01 crowd (xbox)", "boot": "BB01",
    "script": "99999:start", "exit": 2100, "frames": [(2000, "game")],
    "env": {"TSSM_TEST_CROWDLOG": "60", "TSSM_TEST_PLACE": "1500:-128:10:185:180",
            "TSSM_TEST_HOLD": "1501-2100"},
    "timeout": 100, "extras": {"crowds": "xbox", "frame_lock": "xbox"}, "compare": False,
    "about": "boot BB01, the car held by the crowd, to frame 2100, crowds xbox",
}
RUN_BB01_CROWD_GC = dict(RUN_BB01_CROWD_XBOX, name="bb01_crowd_gamecube", item="BB01 crowd (gamecube)",
                         extras={"crowds": "gamecube", "frame_lock": "xbox"},
                         about="boot BB01, the car held by the crowd, to frame 2100, crowds gamecube")
# sc02_water_xbox, sc02_water_gamecube, b301_water_xbox, b301_water_gamecube:
# the Xbox water (iXboxWater.cpp), on by default, in the two levels that have
# water bodies, with no button pressed. check_water reads the water's lines:
# with water = xbox, "Xbox water in <scene>: <n> bodies" with the level's
# number of bodies (WATER_BODIES) and "<scene>: the surface is drawn"; with
# gamecube, "GameCube water in <scene> -- extras.water is gamecube" and no
# Xbox water; and the level's "extras:" line names the same water. SC02 is a
# GameCube 30 fps level, so both SC02 runs set frame_lock = xbox. Measured
# (2026-11-18, D3D9, both widths): both xbox lines come before SC02's frame 300
# and before B301's 600 (its opening cutscene), and every run passes in 13 to
# 18 s. No reference frames: one frame of each is kept for the report.
WATER_BODIES = {"SC02": 2, "B301": 1}
RUN_SC02_WATER_XBOX = {
    "name": "sc02_water_xbox", "item": "SC02 water (xbox)", "boot": "SC02",
    "script": "99999:start", "exit": 400, "frames": [(300, "game")],
    "env": {}, "timeout": 60, "extras": {"water": "xbox", "frame_lock": "xbox"},
    "compare": False, "about": "boot SC02 to frame 400, water xbox",
}
RUN_SC02_WATER_GC = dict(RUN_SC02_WATER_XBOX, name="sc02_water_gamecube", item="SC02 water (gamecube)",
                         extras={"water": "gamecube", "frame_lock": "xbox"},
                         about="boot SC02 to frame 400, water gamecube")
RUN_B301_WATER_XBOX = dict(RUN_SC02_WATER_XBOX, name="b301_water_xbox", item="B301 water (xbox)",
                           boot="B301", exit=700, frames=[(600, "game")], timeout=70,
                           extras={"water": "xbox"}, about="boot B301 to frame 700, water xbox")
RUN_B301_WATER_GC = dict(RUN_B301_WATER_XBOX, name="b301_water_gamecube", item="B301 water (gamecube)",
                         extras={"water": "gamecube"},
                         about="boot B301 to frame 700, water gamecube")
WATER_RUNS = (RUN_SC02_WATER_XBOX, RUN_SC02_WATER_GC, RUN_B301_WATER_XBOX, RUN_B301_WATER_GC)
RUNS = [RUN_BB02, RUN_DE01_FLY, RUN_PT03_BELT, RUN_BB02_CROWD_XBOX, RUN_BB02_CROWD_GC,
        RUN_BB02_XBOX, RUN_TT01_SPOT_XBOX, RUN_TT01_SPOT_GC, RUN_BB01_CROWD_XBOX,
        RUN_BB01_CROWD_GC] + list(WATER_RUNS)

# check_camera: while the fly-through plays (the log says "fly-through on"), the
# camera must be finite and within CAMERA_LIMIT of the origin on every axis; it
# may jump by more than CAMERA_STEP from one frame to the next (a cut) at most
# CAMERA_MAX_CUTS times; and the fly-through must have played for at least
# CAMERA_MIN_FLY of the run's frames. Measured (2026-10-27, three runs per
# width): DE01's fly-through starts at frame 15 and plays past the exit frame,
# within 161 of the origin, never more than 0.12 a frame apart except for its
# one cut (frame 255 or 256; the next is at 492). The frames are taken away
# from the cut, and came out within a mean of 0.71 of the references (0.0% of
# blocks). With the keys byte-swapped the camera sat between 1e-37 and 1e+37
# or at NaN.
CAMERA_LIMIT = 1000.0
CAMERA_STEP = 2.0
CAMERA_MAX_CUTS = 5
CAMERA_MIN_FLY = 300

# check_scroll: the belt's part of the 640x480 frame (x0, y0, x1, y1) and the
# least mean per-pixel difference (crop_change, 0-255) between frames 300 and
# 312 that counts as moving. Measured (2026-10-27, three runs per width): 8.64
# to 8.67 with the belt scrolling, about 0.05 without (the belt texture still,
# only noise); the frames themselves came out within 0.04 of the references.
BELT_CROP = (180, 360, 420, 480)
BELT_FRAMES = (300, 312)
BELT_MIN_CHANGE = 2.0

# check_crowd: the crowd's part of the 640x480 frame, the least mean change
# (as crop_change) between CROWD_FRAMES that counts as moving (xbox) and the
# most that counts as standing still (gamecube). Measured (2026-11-16, D3D9,
# x64): 2.18 and 2.24 moving, 0.13 still (SpongeBob's idle and the seaweed).
CROWD_CROP = (190, 165, 420, 255)
CROWD_FRAMES = (2500, 2540)
CROWD_MOVING = 1.0
CROWD_STILL = 0.5

# check_spotlights: TT01's disco lights (DISCO_BALL_SPOTLIGHT_01-08 and
# DISCO_SPOTLIGHT_01-04, their xStrHash IDs), and the frames after
# TT01_SPOT_SEND by which the script has switched them on (5 s, at 60 fps)
# and the log has to have reached.
DISCO_LIGHTS = frozenset(["715F7AA%s" % c for c in "BCDEF"] + ["715F7AB%d" % d for d in (0, 1, 2)] +
                         ["7F57F41%d" % d for d in (5, 6, 7, 8)])
DISCO_ON_BY = 300 + 60
DISCO_LOGGED_TO = 900

# check_crowd_log: BB01's crowd objects (CROWD_SIMP_01-22), and the frame from
# which the log is read (the car is held from 1500).
BB01_CROWD = 22
BB01_CROWD_FROM = 1600

# Mean block difference (0-255) and share of blocks differing by more than
# BLOCK_LEVEL, per class. Measured on one machine (2026-10-23, three runs per
# width, and x64 against x86): at most 1.14 and 0.8% (the title as Start is
# pressed) and 0.59 and 1.1% (gameplay); the legal screen and the movie frame
# identical. The limits leave room for timing under load; the black 128x128
# shadow corner appearing or going away moves a gameplay frame by about 8 and
# 5%, and a missing line of menu text by more than the menu limits.
TOLERANCE = {
    "still": (0.5, 0.005),
    "menu": (3.0, 0.03),
    "movie": (8.0, 0.15),
    "game": (4.0, 0.05),
}
# The phone's texture path (backend gl3 with TSSM_GL_NO_S3TC=1, as on the
# OpenGL ES desktop check) decodes the S3TC textures on the CPU, whose
# rounding moves the legal screen by a mean of 1.05 (no block over the level)
# against the D3D9 references. Only the still's mean limit is widened for it.
TOLERANCE_NO_S3TC = {
    "still": (1.5, 0.005),
}
BLOCK = 16
BLOCK_LEVEL = 24

UNIT_TESTS = [
    # name, executable, arguments ({assets}, {scratch}), extra environment
    ("save_selftest", "save_selftest", ["{scratch}"], {}),
    ("tssm_input_test", "tssm_input_test", ["{scratch}"], {}),
    ("tssm_modtest", "tssm_modtest", ["{assets}", "{scratch}"], {}),
    ("tssm_sndtest boot.HIP", "tssm_sndtest", ["{assets}/boot.HIP", "{scratch}"], {"TSSM_AUDIO": "0"}),
    ("tssm_sndtest mnuc.HIP", "tssm_sndtest", ["{assets}/MN/mnuc.HIP", "{scratch}"], {"TSSM_AUDIO": "0"}),
    ("tssm_widentest", "tssm_widentest", ["{assets}"], {}),
    ("tssm_extrastest", "tssm_extrastest", ["{scratch}"], {}),
    ("tssm_watertest", "tssm_watertest", [], {}),
]

EXE = ".exe" if os.name == "nt" else ""

DEVICE_FAILED = ("CreateDevice() failed", "D3D11CreateDeviceAndSwapChain failed")

# iAssetHooks.cpp lines that mean an asset was not widened as it should be.
WIDEN_PROBLEMS = ("read as it is", "may be misread", "handler in the game's table",
                  "has no readXForm", "already keeps a copy", "off (TSSM_WIDEN=0)")


# ---------------------------------------------------------------------------
# PNG

def read_png(path):
    """(width, height, bytes per pixel, rows) of an 8-bit RGB or RGBA PNG."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos = 8
    width = height = depth = ctype = interlace = 0
    idat = []
    while pos + 8 <= len(data):
        n, = struct.unpack(">I", data[pos:pos + 4])
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body[:13])
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
        pos += 12 + n
    if depth != 8 or ctype not in (2, 6) or interlace:
        raise ValueError("only 8-bit RGB/RGBA, not interlaced")
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(b"".join(idat))
    stride = width * bpp
    rows = []
    prev = bytearray(stride)
    for y in range(height):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(bytes(line))
        prev = line
    return width, height, bpp, rows


def blocks(path):
    """The picture as BLOCK x BLOCK averages of R, G and B."""
    width, height, bpp, rows = read_png(path)
    bw, bh = width // BLOCK, height // BLOCK
    acc = [[0, 0, 0] for _ in range(bw * bh)]
    for y in range(bh * BLOCK):
        row = rows[y]
        base = (y // BLOCK) * bw
        for bx in range(bw):
            seg = row[bx * BLOCK * bpp:(bx + 1) * BLOCK * bpp]
            a = acc[base + bx]
            a[0] += sum(seg[0::bpp])
            a[1] += sum(seg[1::bpp])
            a[2] += sum(seg[2::bpp])
    n = float(BLOCK * BLOCK)
    return (width, height), [(r / n, g / n, b / n) for r, g, b in acc]


def compare(path_a, path_b):
    """(mean block difference, share of blocks over BLOCK_LEVEL), or raises."""
    size_a, a = blocks(path_a)
    size_b, b = blocks(path_b)
    if size_a != size_b:
        raise ValueError("sizes differ: %dx%d vs %dx%d" % (size_a + size_b))
    diffs = [max(abs(x[0] - y[0]), abs(x[1] - y[1]), abs(x[2] - y[2])) for x, y in zip(a, b)]
    return sum(diffs) / len(diffs), sum(1 for d in diffs if d > BLOCK_LEVEL) / float(len(diffs))


# ---------------------------------------------------------------------------
# Processes

def other_tssm_running():
    if os.name != "nt":
        return False
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq tssm.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True).stdout
    return any(line.lower().startswith('"tssm.exe"') for line in out.splitlines())


def wait_for_device(limit):
    t0 = time.time()
    said = False
    while other_tssm_running():
        if not said:
            print("  another tssm.exe is running; waiting for it to finish (a second device "
                  "can fail to open)")
            said = True
        if time.time() - t0 > limit:
            return False
        time.sleep(3)
    return True


def find_vcvarsall():
    vswhere = os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                           "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.exists(vswhere):
        return None
    path = subprocess.run([vswhere, "-latest", "-products", "*", "-property", "installationPath"],
                          capture_output=True, text=True).stdout.strip()
    vcvars = os.path.join(path, "VC", "Auxiliary", "Build", "vcvarsall.bat")
    return vcvars if os.path.exists(vcvars) else None


def cache_value(build_dir, name):
    """A CMakeCache.txt entry's value, or None."""
    with open(os.path.join(build_dir, "CMakeCache.txt"), encoding="utf-8", errors="replace") as f:
        for line in f:
            key, sep, value = line.rstrip("\r\n").partition("=")
            if sep and key.split(":", 1)[0] == name:
                return value
    return None


def mingw_folder(build_dir):
    """The MinGW bin folder a build directory was configured with (its C++
    compiler's, when gcc is beside it), or None."""
    cxx = cache_value(build_dir, "CMAKE_CXX_COMPILER")
    if cxx and os.path.isabs(cxx):
        folder = os.path.dirname(cxx)
        if os.path.exists(os.path.join(folder, "gcc" + EXE)):
            return folder
    return None


def build(width, build_dir, log_path):
    """cmake --build in the width's MSVC environment, or a MinGW build
    directory's own; never configures."""
    if not os.path.exists(os.path.join(build_dir, "CMakeCache.txt")):
        return False, "%s is not configured (make it with build-debug.bat first)" % build_dir
    mingw = mingw_folder(build_dir)
    if mingw:
        # gcc, its assembler and linker load DLLs from their own folder, which
        # has to be on PATH outside the MSYS2 shell.
        env = dict(os.environ)
        env["PATH"] = mingw + os.pathsep + env.get("PATH", "")
        cmake = cache_value(build_dir, "CMAKE_COMMAND") or "cmake"
        with open(log_path, "w") as log:
            r = subprocess.run([cmake, "--build", build_dir], stdout=log,
                               stderr=subprocess.STDOUT, env=env)
        if r.returncode != 0:
            return False, "build failed (%s)" % log_path
        return True, "built (MinGW)"
    vcvars = find_vcvarsall()
    if vcvars is None:
        return False, "no Visual Studio (vswhere/vcvarsall) found"
    llvm = os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "LLVM", "bin")
    fd, bat = tempfile.mkstemp(suffix=".bat")
    with os.fdopen(fd, "w") as f:
        f.write("@echo off\r\n")
        f.write('call "%s" %s >nul || exit /b 1\r\n' % (vcvars, width))
        if os.path.isdir(llvm):
            f.write('set "PATH=%s;%%PATH%%"\r\n' % llvm)
        f.write('cmake --build "%s"\r\n' % build_dir)
        f.write("exit /b %errorlevel%\r\n")
    try:
        with open(log_path, "w") as log:
            r = subprocess.run(["cmd", "/c", bat], stdout=log, stderr=subprocess.STDOUT)
    finally:
        os.remove(bat)
    if r.returncode != 0:
        return False, "build failed (%s)" % log_path
    return True, "built"


def boot_mod(assets, run_dir, scene):
    """A mod folder (TSSM_MOD) whose sb04.ini is the game's with BOOT=<scene>
    and ShowMenuOnBoot=0: the game starts in that scene, past the menus."""
    lines = open(os.path.join(assets, "sb04.ini"), "rb").read().decode("latin-1").splitlines()
    wanted = {"BOOT": "BOOT = %s" % scene, "ShowMenuOnBoot": "ShowMenuOnBoot = 0"}
    out = []
    done = set()
    for line in lines:
        key = line.split("=", 1)[0].strip()
        if "=" in line and key in wanted:
            if key in done:
                continue  # the first setting of each is replaced, any later one dropped
            done.add(key)
            line = wanted[key]
        out.append(line)
    out[1:1] = [v for k, v in wanted.items() if k not in done]  # settings the file lacked
    mod = os.path.join(run_dir, "mod_boot")
    os.makedirs(mod, exist_ok=True)
    with open(os.path.join(mod, "sb04.ini"), "wb") as f:
        f.write(("\r\n".join(out) + "\r\n").encode("latin-1"))
    return mod


def run_game(width, exe, assets, run_dir, backend, timeout, wait, spec=RUN_BB02):
    """One run of our own tssm.exe. Returns a dict describing it."""
    os.makedirs(os.path.join(run_dir, "frames"), exist_ok=True)
    os.makedirs(os.path.join(run_dir, "save"), exist_ok=True)
    with open(os.path.join(run_dir, "config.ini"), "w", newline="\r\n") as f:
        f.write("[video]\n%sbackend = %s\n[game]\nintro_movies = off\n" % (VIDEO, backend))
        # Every Xbox extra at gamecube unless the run is about one (EXTRAS).
        extras = dict((k, "gamecube") for k in EXTRAS)
        extras.update(spec.get("extras", {}))
        f.write("[extras]\n" + "".join("%s = %s\n" % (k, extras[k]) for k in EXTRAS))
    env = dict(os.environ)
    # Nothing from the caller's environment that changes what a run does.
    for k in [k for k in env if k.upper().startswith("TSSM_TEST_")] + [
            "TSSM_WIDEN", "TSSM_GUI_SELFTEST", "TSSM_GUI_OPEN_AT", "TSSM_INPUT_TEST", "TSSM_MOD",
            "TSSM_MODS"]:
        env.pop(k, None)
    env.update({
        "TSSM_ASSETS": assets,
        "TSSM_CONFIG": os.path.join(run_dir, "config.ini"),
        "TSSM_SAVE_DIR": os.path.join(run_dir, "save"),
        "TSSM_MODS_DIR": os.path.join(run_dir, "mods-none"),
        "TSSM_CAPTURE_DIR": os.path.join(run_dir, "frames"),
        "TSSM_CAPTURE_FRAMES": ",".join(str(n) for n, _ in spec["frames"]),
        "TSSM_EXIT_AFTER": str(spec["exit"]),
        "TSSM_INPUT": spec["script"],
        # The game's own log (iLog.h) in the run's folder: checked below, and
        # never the player's last_run.log.
        "TSSM_LOG": os.path.join(run_dir, "tssm.log"),
    })
    if spec["boot"]:
        env["TSSM_MOD"] = boot_mod(assets, run_dir, spec["boot"])
    env.update(spec["env"])

    result = {"width": width, "dir": run_dir, "attempts": 0}
    for attempt in range(3):
        if not wait_for_device(wait):
            result.update(ok=False, why="another tssm.exe kept running")
            return result
        result["attempts"] = attempt + 1
        t0 = time.time()
        with open(os.path.join(run_dir, "log.txt"), "wb") as out, \
                open(os.path.join(run_dir, "stderr.txt"), "wb") as err:
            p = subprocess.Popen([exe], cwd=run_dir, env=env, stdout=out, stderr=err)
            try:
                code = p.wait(timeout)
                timed_out = False
            except subprocess.TimeoutExpired:
                p.kill()  # this run's own process, and only it
                p.wait()
                code = None
                timed_out = True
        elapsed = time.time() - t0
        log = open(os.path.join(run_dir, "log.txt"), "rb").read().decode("utf-8", "replace")
        errlog = open(os.path.join(run_dir, "stderr.txt"), "rb").read().decode("utf-8", "replace")
        if any(s in log + errlog for s in DEVICE_FAILED) and elapsed < 20:
            print("  %s: the device did not open (another program has the GPU?); trying again" % width)
            time.sleep(10)
            continue
        break

    with open(os.path.join(run_dir, "run.txt"), "w") as f:
        f.write("pid=%d exit=%s elapsed=%.1f timed_out=%d exe=%s\n" % (p.pid, code, elapsed,
                                                                      timed_out, exe))
    result.update(pid=p.pid, exit=code, elapsed=round(elapsed, 1), timed_out=timed_out, log=log)
    return check_run(width, run_dir, result, timeout, spec)


def load_run(width, run_dir, spec=RUN_BB02):
    """A run made earlier (--from-run), read back from its folder."""
    result = {"width": width, "dir": run_dir, "attempts": 1}
    try:
        info = dict(kv.split("=", 1) for kv in open(os.path.join(run_dir, "run.txt")).read().split()
                    if "=" in kv)
        log = open(os.path.join(run_dir, "log.txt"), "rb").read().decode("utf-8", "replace")
    except OSError as e:
        result.update(ok=False, why="no run in %s (%s)" % (run_dir, e))
        return result
    code = None if info.get("exit") in (None, "None") else int(info["exit"])
    result.update(pid=int(info.get("pid", 0)), exit=code, elapsed=float(info.get("elapsed", 0)),
                  timed_out=info.get("timed_out") == "1", log=log)
    return check_run(width, run_dir, result, 0, spec)


def check_run(width, run_dir, result, timeout, spec=RUN_BB02):
    """What a run has to show: its own exit at the exit frame, every frame, and
    (x64) the widening."""
    log = result["log"]
    code = result["exit"]
    timed_out = result["timed_out"]
    problems = []
    if timed_out:
        problems.append("did not exit within %d s (stopped pid %d)" % (timeout, result["pid"]))
    elif code != 0:
        problems.append("exit code %s" % code)
        crash = [l for l in log.splitlines() if "CRASH" in l or l.startswith("tssm:   #0")]
        if crash:
            problems.append(" / ".join(c.strip() for c in crash[:2]))
    if "TSSM_EXIT_AFTER reached" not in log:
        problems.append("never reached frame %d" % spec["exit"])
    own = os.path.join(run_dir, "tssm.log")
    if not os.path.exists(own):
        problems.append("the game kept no log of its own (tssm.log)")
    else:
        kept = open(own, "rb").read().decode("utf-8", "replace")
        printed = [l for l in log.splitlines() if l.strip()]
        lost = [l for l in printed if l not in kept]
        if lost:
            problems.append("its own log lacks %d of the %d lines it printed (first: %s)" % (
                len(lost), len(printed), lost[0].strip()[:80]))
    missing = [n for n, _ in spec["frames"]
               if not os.path.exists(os.path.join(run_dir, "frames", "frame_%05d.png" % n))]
    if missing:
        problems.append("frames not captured: %s" % ", ".join(map(str, missing)))
    result["missing"] = missing

    if width == "x64":
        # iAssetHooks.cpp's log lines: installed, what could not be widened,
        # and the summary at exit.
        widen = [l.split("tssm: widen: ", 1)[1] for l in log.splitlines() if "tssm: widen: " in l]
        widen_problems = []
        if not any(l.startswith("copy-at-load for") for l in widen):
            widen_problems.append("the asset widening was not installed")
        for l in widen:
            if any(s in l for s in WIDEN_PROBLEMS):
                widen_problems.append(l)
        summary = [l for l in widen if l.startswith("at exit:")]
        result["widen"] = summary[0][len("at exit:"):].strip() if summary else ""
        if not summary and not timed_out and code == 0:
            widen_problems.append("no summary at exit")
        result["widen_ok"] = not widen_problems
        problems += ["widening: " + w for w in widen_problems[:3]]
    result["ok"] = not problems
    result["why"] = "; ".join(problems)
    return result


def run_test(name, exe, args, extra_env, scratch, timeout=600):
    os.makedirs(scratch, exist_ok=True)
    env = dict(os.environ)
    env.update(extra_env)
    t0 = time.time()
    with open(os.path.join(scratch, "test.log"), "wb") as out:
        try:
            r = subprocess.run([exe] + args, cwd=scratch, env=env, stdout=out,
                               stderr=subprocess.STDOUT, timeout=timeout)
            code = r.returncode
        except subprocess.TimeoutExpired:
            code = None
    text = open(os.path.join(scratch, "test.log"), "rb").read().decode("utf-8", "replace")
    last = [l for l in text.splitlines() if l.strip()]
    tail = last[-1].strip() if last else ""
    return {"name": name, "exit": code, "ok": code == 0, "elapsed": round(time.time() - t0, 1),
            "detail": tail if code == 0 else "exit %s: %s" % (code, tail)}


# ---------------------------------------------------------------------------
# The level runs' own checks (no reference needed)

CAMLOG = re.compile(r"tssm: test: frame (\d+): camera at \((.*)\), fly-through (on|off)")


def check_camera(log):
    """(ok, detail): the fly-through camera as TSSM_TEST_CAMLOG logged it."""
    on = []
    for line in log.splitlines():
        m = CAMLOG.search(line)
        if not m or m.group(3) != "on":
            continue
        try:
            pos = tuple(float(x) for x in m.group(2).split(", "))
        except ValueError:  # a NaN printed as -nan(ind)
            pos = ()
        if len(pos) != 3:
            pos = (float("nan"),) * 3
        on.append((int(m.group(1)), pos))

    def sane(p):
        return all(math.isfinite(c) and abs(c) <= CAMERA_LIMIT for c in p)

    problems = []
    if len(on) < CAMERA_MIN_FLY:
        problems.append("the fly-through played for %d logged frames, not at least %d" % (
            len(on), CAMERA_MIN_FLY))
    bad = [(f, p) for f, p in on if not sane(p)]
    if bad:
        problems.append("%d of %d frames outside +-%g or not finite, the first at frame %d: "
                        "(%s)" % (len(bad), len(on), CAMERA_LIMIT, bad[0][0],
                                  ", ".join("%g" % c for c in bad[0][1])))
    cuts = [fb for (fa, pa), (fb, pb) in zip(on, on[1:])
            if fb == fa + 1 and sane(pa) and sane(pb) and math.dist(pa, pb) > CAMERA_STEP]
    if len(cuts) > CAMERA_MAX_CUTS:
        problems.append("the camera jumped more than %g in one frame %d times (at most %d), "
                        "first at frames %s" % (CAMERA_STEP, len(cuts), CAMERA_MAX_CUTS,
                                                ", ".join(map(str, cuts[:5]))))
    if problems:
        return False, "; ".join(problems)
    largest = max(max(abs(c) for c in p) for _, p in on)
    return True, "%d frames of fly-through (%d-%d), within %.0f of the origin, %d cut%s%s" % (
        len(on), on[0][0], on[-1][0], largest, len(cuts), "" if len(cuts) == 1 else "s",
        " (frames %s)" % ", ".join(map(str, cuts)) if cuts else "")


def crop_change(path_a, path_b, box):
    """Mean per-pixel difference (the mean of R, G and B, 0-255) of two frames
    inside box, (x0, y0, x1, y1) on a 640x480 frame. On another size the box is
    placed where the same part of the scene is: the port keeps the 4:3 view and
    widens it (Hor+) or heightens it (Vert+), so the 640x480 picture is the
    centre of the frame scaled by min(w/640, h/480)."""
    wa, ha, bpa, ra = read_png(path_a)
    wb, hb, bpb, rb = read_png(path_b)
    if (wa, ha) != (wb, hb):
        raise ValueError("sizes differ: %dx%d vs %dx%d" % (wa, ha, wb, hb))
    k = min(wa / 640.0, ha / 480.0)
    x0, x1 = (int(round(wa / 2.0 + (x - 320) * k)) for x in (box[0], box[2]))
    y0, y1 = (int(round(ha / 2.0 + (y - 240) * k)) for y in (box[1], box[3]))
    total = 0
    for y in range(y0, y1):
        la, lb = ra[y], rb[y]
        for x in range(x0, x1):
            i, j = x * bpa, x * bpb
            total += abs(la[i] - lb[j]) + abs(la[i + 1] - lb[j + 1]) + abs(la[i + 2] - lb[j + 2])
    return total / (3.0 * (x1 - x0) * (y1 - y0))


def check_scroll(frames_dir):
    """(ok, detail): the belt moved between BELT_FRAMES."""
    a, b = (os.path.join(frames_dir, "frame_%05d.png" % f) for f in BELT_FRAMES)
    for p in (a, b):
        if not os.path.exists(p):
            return False, "%s not captured" % os.path.basename(p)
    try:
        change = crop_change(a, b, BELT_CROP)
    except ValueError as e:
        return False, str(e)
    detail = "belt crop %s, frame %d vs %d: mean change %.2f (moving from %.1f)" % (
        "x".join(map(str, BELT_CROP)), BELT_FRAMES[0], BELT_FRAMES[1], change, BELT_MIN_CHANGE)
    if change < BELT_MIN_CHANGE:
        return False, detail + "; the belt texture stands still"
    return True, detail


def check_crowd(frames_dir, moving):
    """(ok, detail): the crowd moved between CROWD_FRAMES (crowds = xbox), or
    stood still (gamecube)."""
    a, b = (os.path.join(frames_dir, "frame_%05d.png" % f) for f in CROWD_FRAMES)
    for p in (a, b):
        if not os.path.exists(p):
            return False, "%s not captured" % os.path.basename(p)
    try:
        change = crop_change(a, b, CROWD_CROP)
    except ValueError as e:
        return False, str(e)
    detail = "crowd crop %s, frame %d vs %d: mean change %.2f (%s)" % (
        "x".join(map(str, CROWD_CROP)), CROWD_FRAMES[0], CROWD_FRAMES[1], change,
        "moving from %.1f" % CROWD_MOVING if moving else "still below %.1f" % CROWD_STILL)
    if moving and change < CROWD_MOVING:
        return False, detail + "; the crowd stands still"
    if not moving and change > CROWD_STILL:
        return False, detail + "; the crowd moves"
    return True, detail


SPOTLOG = re.compile(r"tssm: test: frame (\d+): spotlights (\d+) on of (\d+), (\d+) drawn, "
                     r"(?:(\d+) flares, )?camera inside (\d+)(?:, nearest beam ([-0-9.]+))?;(.*)")


def check_spotlights(log, xbox):
    """(ok, detail): TT01's disco lights came on (spotlights = xbox) or never did
    (gamecube), as TSSM_TEST_SPOTLOG logged the scene's spotlights."""
    seen = []
    for line in log.splitlines():
        m = SPOTLOG.search(line)
        if m:
            ids = set(t.rstrip("+") for t in m.group(8).split() if t != "none")
            seen.append((int(m.group(1)), ids & DISCO_LIGHTS, int(m.group(6))))
    after = [x for x in seen if x[0] > TT01_SPOT_SEND]
    if not after or after[-1][0] < TT01_SPOT_SEND + DISCO_LOGGED_TO:
        return False, "the spotlights were logged to frame %d, not to %d" % (
            after[-1][0] if after else 0, TT01_SPOT_SEND + DISCO_LOGGED_TO)
    most = max(len(x[1]) for x in seen)
    first = [f for f, ids, _ in after if len(ids) == len(DISCO_LIGHTS)]
    inside = sum(1 for x in after if x[2] > 0)
    detail = "%d frames logged to %d; disco lights on: at most %d of %d%s; camera in a beam in %d" % (
        len(seen), seen[-1][0], most, len(DISCO_LIGHTS),
        ", all from frame %d" % first[0] if first else "", inside)
    if xbox and not (first and first[0] <= TT01_SPOT_SEND + DISCO_ON_BY):
        return False, detail + "; the disco lights did not all come on by frame %d" % (
            TT01_SPOT_SEND + DISCO_ON_BY)
    if not xbox and most > 0:
        return False, detail + "; a disco light came on"
    return True, detail


CROWDLOG = re.compile(r"tssm: test: frame (\d+): crowd (\d+) objects, speed ([-0-9.]+), (\d+) moved")


def check_crowd_log(log, moving):
    """(ok, detail): BB01's crowd objects, as TSSM_TEST_CROWDLOG logged them, ran their cycle
    (crowds = xbox) or stood at speed 0 (gamecube)."""
    seen = [(int(m.group(1)), int(m.group(2)), float(m.group(3)), int(m.group(4)))
            for m in CROWDLOG.finditer(log) if int(m.group(1)) >= BB01_CROWD_FROM]
    if not seen:
        return False, "no crowd lines logged from frame %d" % BB01_CROWD_FROM
    counts = sorted(set(x[1] for x in seen))
    speeds = sorted(set(x[2] for x in seen))
    moved = [x[3] for x in seen]
    detail = "%d lines from frame %d: %s objects, speed %s, %d-%d moved a line" % (
        len(seen), seen[0][0], "/".join(map(str, counts)), "/".join("%.2f" % v for v in speeds),
        min(moved), max(moved))
    if counts != [BB01_CROWD]:
        return False, detail + "; not the %d crowd objects" % BB01_CROWD
    if moving and (speeds != [1.0] or min(moved) < 1):
        return False, detail + "; the crowd stands still"
    if not moving and (speeds != [0.0] or max(moved) > 0):
        return False, detail + "; the crowd moves"
    return True, detail


def check_water(log, scene, xbox):
    """(ok, detail): the level's water as its log lines say: the Xbox water set up for
    every body and its surface drawn (water = xbox), or the GameCube water because the
    setting says so (gamecube); and the level's extras line names the same."""
    lines = [l.strip() for l in log.splitlines()]
    bodies = WATER_BODIES[scene]
    xb = [l for l in lines if l.startswith("tssm: water: Xbox water in %s:" % scene)]
    gc = [l for l in lines if l.startswith("tssm: water: GameCube water in %s --" % scene)]
    drawn = any(l.startswith("tssm: water: %s: the surface is drawn" % scene) for l in lines)
    ex = [l for l in lines if l.startswith("tssm: extras: %s " % scene)]
    want = "water %s" % ("xbox" if xbox else "gamecube")
    detail = "%d Xbox water line(s), %d GameCube water line(s), surface %s, extras line %s" % (
        len(xb), len(gc), "drawn" if drawn else "not drawn",
        "names %s" % want if any(want + "," in l for l in ex) else "missing or other")
    if not ex or not all(want + "," in l for l in ex):
        return False, detail + "; the extras line does not say %s" % want
    if xbox:
        body = "%d bod%s" % (bodies, "y" if bodies == 1 else "ies")
        if not xb or not all((": %s;" % body) in l for l in xb):
            return False, detail + "; not 'Xbox water in %s: %s'" % (scene, body)
        if gc or not drawn:
            return False, detail + "; the Xbox water fell back or was not drawn" + (
                ": " + gc[0] if gc else "")
    else:
        if xb or not gc or not all("extras.water is gamecube" in l for l in gc):
            return False, detail + "; not the GameCube water by the setting"
    return True, detail


# ---------------------------------------------------------------------------
# References

def ref_sub(spec):
    """The run's folder under <reference>/<width>: none for the run to gameplay."""
    return "" if spec is RUN_BB02 else spec["name"]


def run_folder(spec, width):
    return "run_" + width if spec is RUN_BB02 else "run_%s_%s" % (spec["name"], width)


def run_label(spec, width):
    return width if spec is RUN_BB02 else "%s %s" % (spec["name"], width)


def variants(reference, width, frame, own_only=False, sub=""):
    """(label, path) of every stored picture a frame may match: the width's
    reference, its variants (frame_NNNNN.altK.png), then the other width's.
    x64 is meant to look like x86, so each width's pictures count for both."""
    out = []
    order = [width] + ([] if own_only else [w for w in ("x86", "x64") if w != width])
    for w in order:
        d = os.path.join(reference, w, sub)
        main = os.path.join(d, "frame_%05d.png" % frame)
        if os.path.exists(main):
            out.append((w, main))
        k = 1
        while os.path.exists(os.path.join(d, "frame_%05d.alt%d.png" % (frame, k))):
            out.append(("%s alt%d" % (w, k), os.path.join(d, "frame_%05d.alt%d.png" % (frame, k))))
            k += 1
    return out


def add_variant(reference, width, frame, src, sub=""):
    d = os.path.join(reference, width, sub)
    os.makedirs(d, exist_ok=True)
    k = 1
    while os.path.exists(os.path.join(d, "frame_%05d.alt%d.png" % (frame, k))):
        k += 1
    dst = os.path.join(d, "frame_%05d.alt%d.png" % (frame, k))
    shutil.copyfile(src, dst)
    return dst


def best_match(path, candidates, cls):
    """(mean, share, ok, note) against the closest candidate."""
    if not candidates:
        return (None, None, False, "no reference")
    tol_mean, tol_share = TOLERANCE[cls]
    best = None
    for label, cand in candidates:
        try:
            mean, share = compare(path, cand)
        except Exception as e:  # a broken PNG fails the frame, not the run
            return (None, None, False, str(e))
        ok = mean <= tol_mean and share <= tol_share
        score = max(mean / tol_mean, share / tol_share)
        if best is None or (ok, -score) > (best[2], -best[4]):
            best = (mean, share, ok, label, score)
    mean, share, ok, label, _ = best
    if not ok:
        return (mean, share, ok, "closest: " + label)
    return (mean, share, ok, "" if label == candidates[0][0] else "matches " + label)


def frames_detail(results):
    bad = [x for x in results if not x[4]]
    measured = [x for x in results if x[2] is not None]
    worst = max(measured, key=lambda x: max(x[2] / TOLERANCE[x[1]][0], x[3] / TOLERANCE[x[1]][1]),
                default=None)
    detail = "%d/%d within tolerance" % (len(results) - len(bad), len(results))
    if worst:
        detail += "; closest to the limit %d (%s) mean %.2f, %.1f%% of blocks" % (
            worst[0], worst[1], worst[2], 100 * worst[3])
    others = [x for x in results if x[4] and x[5]]
    if others:
        detail += "; " + ", ".join("%d %s" % (x[0], x[5]) for x in others)
    if bad:
        detail += "; failed: " + ", ".join("%d%s" % (x[0], " (%s)" % x[5] if x[5] else "")
                                           for x in bad)
    return detail


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--assets", default=os.environ.get("TSSM_ASSETS"))
    ap.add_argument("--reference", default=os.environ.get("TSSM_REGRESS_REFERENCE"))
    ap.add_argument("--out", default=os.environ.get("TSSM_REGRESS_OUT"))
    ap.add_argument("--widths", default="x86,x64")
    ap.add_argument("--runs", default=",".join(spec["name"] for spec in RUNS))
    ap.add_argument("--quick", action="store_true", help="the run to gameplay only")
    ap.add_argument("--build", action="store_true")
    ap.add_argument("--build-dir-x64", default=os.path.join(ROOT, "build-pc"))
    ap.add_argument("--build-dir-x86", default=os.path.join(ROOT, "build-pc-x86"))
    ap.add_argument("--skip-runs", action="store_true")
    ap.add_argument("--skip-tests", action="store_true")
    ap.add_argument("--update-reference", action="store_true")
    ap.add_argument("--add-variants", action="store_true")
    ap.add_argument("--from-run", help="score an earlier run's folder instead of running")
    ap.add_argument("--backend", default="d3d9")
    ap.add_argument("--size", help="WxH: render at exactly this size (the widescreen pass)")
    ap.add_argument("--timeout", type=int, default=88)
    ap.add_argument("--wait", type=int, default=600)
    a = ap.parse_args()

    if a.size:
        global VIDEO, SCRIPT, FRAMES
        try:
            sw, sh = (int(v) for v in a.size.lower().split("x"))
        except ValueError:
            ap.error("--size is WxH, e.g. 1280x720")
        VIDEO = "aspect = custom\nwidth = %d\nheight = %d\n" % (sw, sh)
        # Gameplay is over by 3050; the pause menu is the boxed panel over the
        # live scene.
        SCRIPT += ",3050:start"
        FRAMES = sorted(FRAMES + [(3150, "menu")])
        RUN_BB02["script"] = SCRIPT
        RUN_BB02["frames"] = FRAMES

    for name, value in (("--assets (or TSSM_ASSETS)", a.assets), ("--out (or TSSM_REGRESS_OUT)", a.out)):
        if not value:
            ap.error("%s is required" % name)
    if not a.skip_runs and not a.reference:
        ap.error("--reference (or TSSM_REGRESS_REFERENCE) is required for the runs")
    widths = [w.strip() for w in a.widths.split(",") if w.strip()]
    dirs = {"x64": os.path.abspath(a.build_dir_x64), "x86": os.path.abspath(a.build_dir_x86)}
    for w in widths:
        if w not in dirs:
            ap.error("unknown width %s" % w)
    names = [n.strip() for n in ("bb02" if a.quick else a.runs).split(",") if n.strip()]
    for n in names:
        if n not in [spec["name"] for spec in RUNS]:
            ap.error("unknown run %s (runs: %s)" % (n, ", ".join(spec["name"] for spec in RUNS)))
    specs = [spec for spec in RUNS if spec["name"] in names]
    for path in (a.out, a.reference):
        if path and os.path.abspath(path).startswith(ROOT + os.sep):
            ap.error("%s is inside the repository; game frames stay out of it" % path)

    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    out = os.path.join(os.path.abspath(a.out), "regress_" + stamp)
    os.makedirs(out, exist_ok=True)
    head = subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"],
                          capture_output=True, text=True).stdout.strip()
    dirty = subprocess.run(["git", "-C", ROOT, "status", "--porcelain", "--untracked-files=no"],
                           capture_output=True, text=True).stdout.strip()
    print("regress: %s%s, widths %s, runs %s, into %s" % (
        head, " (+ local changes)" if dirty else "", ",".join(widths),
        ",".join(spec["name"] for spec in specs), out))
    if a.backend == "gl3" and os.environ.get("TSSM_GL_NO_S3TC") == "1":
        TOLERANCE.update(TOLERANCE_NO_S3TC)
        print("TSSM_GL_NO_S3TC=1: CPU-decoded textures, still frames within mean %.2f" %
              TOLERANCE["still"][0])

    rows = []  # (item, width, ok, detail)
    report = {"head": head, "dirty": bool(dirty), "when": stamp, "script": SCRIPT,
              "exit_frame": EXIT_FRAME, "frames": FRAMES, "tolerance": TOLERANCE,
              "level_runs": {spec["name"]: {k: spec[k] for k in ("boot", "exit", "frames", "env")}
                             for spec in specs if spec is not RUN_BB02},
              "runs": {}, "frames_result": {}, "tests": {}}

    if a.build:
        for w in widths:
            print("build %s ..." % w)
            ok, why = build(w, dirs[w], os.path.join(out, "build_%s.log" % w))
            rows.append(("build", w, ok, why))
            if not ok:
                print("  " + why)

    runs = {}  # (run name, width) -> the run
    if not a.skip_runs:
        for spec in specs:
            for w in widths:
                exe = os.path.join(dirs[w], "tssm" + EXE)
                if not os.path.exists(exe):
                    rows.append((spec["item"], w, False, "no %s" % exe))
                    continue
                folder = run_folder(spec, w)
                if a.from_run:
                    r = load_run(w, os.path.join(os.path.abspath(a.from_run), folder), spec)
                else:
                    print("run %s %s (%s) ..." % (spec["name"], w, spec["about"]))
                    timeout = a.timeout if spec["timeout"] is None else \
                        int(math.ceil(spec["timeout"] * a.timeout / 88.0))
                    r = run_game(w, exe, a.assets, os.path.join(out, folder), a.backend, timeout,
                                 a.wait, spec)
                runs[(spec["name"], w)] = r
                detail = "exit %s at %.1f s" % (r.get("exit"), r.get("elapsed", 0)) if r["ok"] else r["why"]
                if r.get("attempts", 1) > 1:
                    detail += " (attempt %d)" % r["attempts"]
                rows.append((spec["item"], w, r["ok"], detail))
                if spec is RUN_BB02 and w == "x64" and "widen_ok" in r:
                    rows.append(("asset widening", w, r["widen_ok"], r["widen"] or "no summary"))
                report["runs"][run_label(spec, w)] = {k: v for k, v in r.items() if k != "log"}
                # The checks that need no reference.
                if spec is RUN_DE01_FLY:
                    ok, detail = check_camera(r.get("log", ""))
                    rows.append(("DE01 fly-through camera", w, ok, detail))
                elif spec is RUN_PT03_BELT:
                    ok, detail = check_scroll(os.path.join(r["dir"], "frames"))
                    rows.append(("PT03 belt scrolls", w, ok, detail))
                elif spec is RUN_BB02_CROWD_XBOX or spec is RUN_BB02_CROWD_GC:
                    moving = spec is RUN_BB02_CROWD_XBOX
                    ok, detail = check_crowd(os.path.join(r["dir"], "frames"), moving)
                    rows.append(("BB02 crowd %s" % ("moves" if moving else "stands still"), w, ok,
                                 detail))
                elif spec is RUN_TT01_SPOT_XBOX or spec is RUN_TT01_SPOT_GC:
                    xbox = spec is RUN_TT01_SPOT_XBOX
                    ok, detail = check_spotlights(r.get("log", ""), xbox)
                    rows.append(("TT01 disco lights %s" % ("on" if xbox else "stay off"), w, ok,
                                 detail))
                elif spec is RUN_BB01_CROWD_XBOX or spec is RUN_BB01_CROWD_GC:
                    moving = spec is RUN_BB01_CROWD_XBOX
                    ok, detail = check_crowd_log(r.get("log", ""), moving)
                    rows.append(("BB01 crowd %s" % ("moves" if moving else "stands still"), w, ok,
                                 detail))
                elif spec in WATER_RUNS:
                    xbox = spec["extras"]["water"] == "xbox"
                    ok, detail = check_water(r.get("log", ""), spec["boot"], xbox)
                    rows.append(("%s water %s" % (spec["boot"], "xbox" if xbox else "gamecube"), w,
                                 ok, detail))

        # Frames: against the references (and their variants), and x64 against
        # this run's x86.
        for (name, w), r in runs.items():
            spec = [s for s in RUNS if s["name"] == name][0]
            if not spec.get("compare", True):
                continue  # checked by itself above, no references
            sub = ref_sub(spec)
            item = "frames vs reference" if spec is RUN_BB02 else spec["item"] + " frames"
            ref_dir = os.path.join(a.reference, w, sub)
            frames_dir = os.path.join(r["dir"], "frames")
            if a.update_reference:
                os.makedirs(ref_dir, exist_ok=True)
                n = 0
                for f, _ in spec["frames"]:
                    for label, old in variants(a.reference, w, f, own_only=True, sub=sub):
                        if " alt" in label:
                            os.remove(old)  # the old reference's variants go with it
                    src = os.path.join(frames_dir, "frame_%05d.png" % f)
                    if os.path.exists(src):
                        shutil.copyfile(src, os.path.join(ref_dir, "frame_%05d.png" % f))
                        n += 1
                with open(os.path.join(ref_dir, "reference.json"), "w") as fh:
                    json.dump({"head": head, "dirty": bool(dirty), "when": stamp,
                               "run": spec["name"], "boot": spec["boot"], "script": spec["script"],
                               "env": spec["env"], "frames": [f for f, _ in spec["frames"]],
                               "backend": a.backend}, fh, indent=1)
                rows.append(("references updated" if spec is RUN_BB02 else
                             "%s references updated" % spec["item"], w,
                             n == len(spec["frames"]), "%d frames -> %s" % (n, ref_dir)))
                continue
            results = []
            added = []
            for f, cls in spec["frames"]:
                mine = os.path.join(frames_dir, "frame_%05d.png" % f)
                if not os.path.exists(mine):
                    results.append((f, cls, None, None, False, "not captured"))
                    continue
                res = best_match(mine, variants(a.reference, w, f, sub=sub), cls)
                results.append((f, cls) + res)
                if not res[2] and a.add_variants and res[0] is not None:
                    added.append(add_variant(a.reference, w, f, mine, sub))
            report["frames_result"][run_label(spec, w)] = results
            rows.append((item, w, not [x for x in results if not x[4]], frames_detail(results)))
            for path in added:
                rows.append(("variant added", w, True, path))

        if ("bb02", "x64") in runs and ("bb02", "x86") in runs:
            results = []
            for f, cls in RUN_BB02["frames"]:
                p64 = os.path.join(runs[("bb02", "x64")]["dir"], "frames", "frame_%05d.png" % f)
                p86 = os.path.join(runs[("bb02", "x86")]["dir"], "frames", "frame_%05d.png" % f)
                if not os.path.exists(p64):
                    results.append((f, cls, None, None, False, "missing"))
                    continue
                # This run's x86 frame, or a stored picture known to be right
                # (the references and their variants: the two-way pictures can
                # come out differently at the two widths in one run).
                candidates = ([("this run's x86", p86)] if os.path.exists(p86) else [])
                if a.reference:
                    candidates += variants(a.reference, "x86", f)
                results.append((f, cls) + best_match(p64, candidates, cls))
            report["frames_result"]["x64_vs_x86"] = results
            rows.append(("x64 frames vs x86", "both", not [x for x in results if not x[4]],
                         frames_detail(results)))

    if not a.skip_tests:
        for w in widths:
            for name, exe_name, args, extra in UNIT_TESTS:
                exe = os.path.join(dirs[w], exe_name + EXE)
                if not os.path.exists(exe):
                    rows.append((name, w, False, "not built (%s)" % exe))
                    continue
                scratch = os.path.join(out, "test_%s_%s" % (w, name.replace(" ", "_").replace(".", "_")))
                fmt = {"assets": os.path.abspath(a.assets).replace("\\", "/"),
                       "scratch": scratch.replace("\\", "/")}
                print("test %s %s ..." % (w, name))
                t = run_test(name, exe, [x.format(**fmt) for x in args], extra, scratch)
                rows.append((name, w, t["ok"], t["detail"]))
                report["tests"]["%s %s" % (w, name)] = t

    # The table.
    width_item = max(len(r[0]) for r in rows) if rows else 10
    print()
    print("%-*s  %-5s  %-4s  %s" % (width_item, "item", "width", "", "detail"))
    print("%s  %s  %s  %s" % ("-" * width_item, "-" * 5, "-" * 4, "-" * 40))
    for item, w, ok, detail in rows:
        print("%-*s  %-5s  %-4s  %s" % (width_item, item, w, "PASS" if ok else "FAIL", detail))
    passed = all(r[2] for r in rows) and bool(rows)
    print()
    print("%s: %d of %d rows passed (%s)" % ("PASS" if passed else "FAIL",
                                            sum(1 for r in rows if r[2]), len(rows), out))

    report["rows"] = [{"item": r[0], "width": r[1], "ok": r[2], "detail": r[3]} for r in rows]
    report["passed"] = passed
    with open(os.path.join(out, "report.json"), "w") as f:
        json.dump(report, f, indent=1)
    write_html(out, report, a.reference, runs)
    return 0 if passed else 1


def write_html(out, report, reference, runs):
    """report.html: the table, and every frame beside its reference."""
    def rel(p):
        # A frame on another drive than --out has no relative path (relpath
        # raises); it is linked by its absolute file: URL instead.
        try:
            return os.path.relpath(p, out).replace("\\", "/")
        except ValueError:
            return "file:///" + os.path.abspath(p).replace("\\", "/").lstrip("/")

    parts = ["<!doctype html><meta charset=utf-8><title>regress %s</title>" % html.escape(report["when"]),
             "<style>body{font:14px sans-serif;margin:16px}td,th{padding:2px 8px;text-align:left;"
             "vertical-align:top}.pass{color:#060}.fail{color:#a00;font-weight:bold}"
             "img{width:320px;height:auto;border:1px solid #888}</style>",
             "<h1>regress %s</h1><p>%s%s</p><table><tr><th>item<th>width<th><th>detail" % (
                 html.escape(report["when"]), html.escape(report["head"]),
                 " (+ local changes)" if report["dirty"] else "")]
    for r in report["rows"]:
        parts.append("<tr><td>%s<td>%s<td class=%s>%s<td>%s" % (
            html.escape(r["item"]), r["width"], "pass" if r["ok"] else "fail",
            "PASS" if r["ok"] else "FAIL", html.escape(r["detail"])))
    parts.append("</table>")
    for (name, w), r in runs.items():
        spec = [s for s in RUNS if s["name"] == name][0]
        label = run_label(spec, w)
        results = {x[0]: x for x in report["frames_result"].get(label, [])}
        parts.append("<h2>%s</h2><table><tr><th>frame<th>this run<th>reference<th>difference" %
                     html.escape(label if spec is RUN_BB02 else "%s (%s)" % (spec["item"], w)))
        for f, cls in spec["frames"]:
            mine = os.path.join(r["dir"], "frames", "frame_%05d.png" % f)
            ref = os.path.join(reference, w, ref_sub(spec), "frame_%05d.png" % f) if reference else ""
            x = results.get(f)
            diff = ""
            if x and x[2] is not None:
                diff = "mean %.2f, %.1f%% of blocks" % (x[2], 100 * x[3])
            ok = x[4] if x else True
            parts.append("<tr><td>%d<br>%s<td>%s<td>%s<td class=%s>%s" % (
                f, cls,
                '<img src="%s">' % rel(mine) if os.path.exists(mine) else "-",
                '<img src="%s">' % rel(ref) if ref and os.path.exists(ref) else "-",
                "pass" if ok else "fail", html.escape(diff or (x[5] if x else ""))))
        parts.append("</table>")
    with open(os.path.join(out, "report.html"), "w", encoding="utf-8") as f:
        f.write("\n".join(parts))


if __name__ == "__main__":
    sys.exit(main())
