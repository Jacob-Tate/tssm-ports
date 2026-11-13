#ifndef ISPIKE_H
#define ISPIKE_H

#include <types.h>

// The 3DS speed test (docs/3DS.md,
// "The speed test"). A run that needs nobody at the controls: it
// measures the console (model, clock, memory, threads, SD card speed), then
// boots a list of levels one after the other with librw's NULL renderer and
// plays a scripted controller in each, timing every frame's work
// with the measurement counters (iD0Stats.h), and writes what it found to
// spike-report.txt. The same run on a PC build is the calibration: the ratio
// of the two is how much slower the console is at this game's own code.
//
// On:
//   3DS   built with -DTSSM_3DS_SPIKE=ON (tssm-spike.3dsx). Folder:
//         sdmc:/3ds/tssm/spike, report sdmc:/3ds/tssm/spike-report.txt.
//   PC    TSSM_SPIKE=1 (folder TSSM_SPIKE_DIR, default ./spike; the report
//         goes into it). For the calibration line.
//
// Settings, all optional, in <folder>/../spike.ini on the 3DS
// (sdmc:/3ds/tssm/spike.ini) or TSSM_SPIKE_LEVELS / TSSM_SPIKE_SECONDS on a PC:
//   levels  = BB02,TR01,GG02,JK01   scene ids, in order
//   seconds = 60                    game time per level (30 frames a second)
//   sdmb    = 64                    MB read for the SD card speed test
//
// The run's own settings replace the player's: its config.ini, an empty save
// and mods folder, and a mod folder whose sb04.ini turns the level-intro
// movies off -- all under the folder above, so nothing of the player's is
// read or written.

S32 iSpikeOn();

// iSystemInit, before anything reads a setting: points TSSM_CONFIG,
// TSSM_SAVE_DIR, TSSM_MODS_DIR, TSSM_MOD, TSSM_D0, TSSM_RWHEAP and TSSM_INPUT
// at the run's own, and writes them.
void iSpikeEarly();

// iCapture's frame hook, once per presented frame, after iD0Stats has measured it
// and before the script's buttons for the next frame are set.
void iSpikeFrame();

// iFile, for every game file opened for reading: the run lists them, which is
// how the smallest set of files to copy to a console is known.
void iSpikeFileOpened(const char* path, long length);

#endif
