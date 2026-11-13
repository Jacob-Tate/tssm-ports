#ifndef ISAVECLI_H
#define ISAVECLI_H

// PC-only: tssm.exe's save tools on the command line.
//
//   tssm --saves [--all]                     list the save slots
//   tssm --import-save <file> [--slot S]     import a .gci/.gcs, a card image,
//        [--pick N] [--force] [--dry-run]    a zip of saves or a save file
//   tssm --export-save <S|all> <file|dir>    export a slot as a .gci, or every
//        [--region GGVE|GGVP|E|P]            save as a zip or into a folder
//   tssm --check-save <file>                 say what a file holds, import nothing
//   tssm --restore-save <S> [N]              put backup N (1 newest) back
//   tssm --delete-save <S>                   empty a slot (it becomes backup 1)
//   tssm --save-dir <dir> ...                use another save folder
//   tssm --saves-help
//
// S is a slot: A1..A3 are the first save folder's three (the console's Memory
// Card Slot A), B1..B3 the second's; a bare 1..3 means folder A. Nothing here
// opens a window or starts the game: the command runs, prints what it did and
// the process exits with 0 (done), 1 (failed) or 2 (not understood).

#include <types.h>

// Runs the save command on the process's command line, if there is one, and
// returns the exit code; -1 when the command line has none (start the game).
// iSystemInit calls it straight after loading config.ini.
S32 iSaveCLIRun();

// The same on an explicit argument list (argv[0] is the program), for tests.
S32 iSaveCLIRunArgs(S32 argc, char** argv);

#endif
