#ifndef ISAVESTORE_H
#define ISAVESTORE_H

// PC-only: the save store -- where saves live on a host, and everything done to
// them from outside the game: listing them, importing GameCube saves, exporting
// to the GameCube and to one file for another install, backups. The game's own
// saving goes through isavegame.cpp, which writes through here too, so a save
// made in the game and one imported are the same thing on disk.
//
// The layout (docs/SAVES.md has the whole of it):
//
//   <root>/SpongeBobMovie01..03          folder A: the save files, each a
//                                        canonical payload (iSaveFormat.h)
//   <root>/second/...                    folder B, laid out the same
//   <root>/more/2../...                  folders C..J (the PC save screen)
//   <folder>/SpongeBobMovie01.tga        the still beside a save (PC menus)
//   <folder>/SpongeBobMovie01.gchead     the card entry and head a save was
//                                        imported with, so exporting it gives
//                                        back the same .gci
//   <folder>/backup/SpongeBobMovie01.1   the three versions before the
//                                .2 .3   current one, newest first
//
// A slot is a folder and a game number: "A1".."A3" are folder A's three, which
// is what the console's Memory Card Slot A held; "B1".."B3" are Slot B's.
//
// Every write is atomic (a temporary file renamed over the old one) and every
// overwrite of a save keeps the old one as backup .1.
//
// This is the interface the in-game Saves tab (ImGui) calls; tssm.exe's
// --saves/--import-save/--export-save command line (iSaveCLI.cpp) is the other
// caller.

#include <types.h>

#include <string>
#include <vector>

#define ISS_MAX_TARGETS 10 // isavegame.h's ISG_MAX_TARGETS
#define ISS_GAMES 3
#define ISS_BACKUPS 3

// --- where -----------------------------------------------------------------------

// The save folder. A folder set with iSaveStoreSetRoot (the command line's
// --save-dir) wins, then TSSM_SAVE_DIR, then config.ini's
// game.save_folder, then the host's per-user data folder: tssm/saves, or
// tssm/mods/<name>/saves while a mod is in use (a mod's levels differ, so its
// saves are kept apart).
const char* iSaveStoreRoot();
void iSaveStoreSetRoot(const char* dir);
void iSaveStoreSetConfigRoot(const char* dir);

// Folder `target`'s directory; a slot's save file; the slot's name ("A1").
void iSaveStoreTargetDir(S32 target, char* out, size_t outsize);
void iSaveStoreSlotPath(S32 target, S32 game, char* out, size_t outsize);
void iSaveStoreSlotName(S32 target, S32 game, char* out, size_t outsize);

// "A1".."J3", or "1".."3" for folder A. Case does not matter.
bool iSaveStoreParseSlot(const char* text, S32* target, S32* game);

// --- what is there ---------------------------------------------------------------------

struct iSaveSlotInfo
{
    S32 target;
    S32 game;
    char name[4]; // "A1"
    char path[512];

    bool exists;
    bool valid; // the game will load it
    char problem[160]; // why not, when it exists and is not valid

    char label[64]; // the level, as the game named it
    S32 progress; // percent
    S32 levelIndex; // 0-based world
    char scene[8]; // "HB01"
    S64 mtime; // when it was saved: Unix seconds
    S32 size;

    bool hasThumb;
    char thumbPath[512];
    bool fromCard; // carries the entry it was imported with
    S32 backups;
};

bool iSaveStoreSlot(S32 target, S32 game, iSaveSlotInfo* out);

// Every slot of folders A and B, or of every folder, in order, empty ones
// included.
void iSaveStoreList(std::vector<iSaveSlotInfo>& out, bool allTargets);

// --- reading and writing ------------------------------------------------------------

bool iSaveStoreRead(S32 target, S32 game, std::vector<U8>& payload, char* err, size_t errsize);

// A canonical payload into a slot: checked first, the old save kept as backup
// .1, the still taken away (it was of the old save).
bool iSaveStoreWrite(S32 target, S32 game, const U8* payload, size_t n, char* err, size_t errsize);

// The primitive under both: `n` bytes to `path` through a temporary and a
// rename; with `backup`, what was at `path` becomes backup .1 first.
bool iSaveStoreWriteFile(const char* path, const U8* data, size_t n, bool backup, char* err,
                         size_t errsize);

bool iSaveStoreReadFile(const char* path, std::vector<U8>& out, char* err, size_t errsize);

// --- import --------------------------------------------------------------------------

struct iSaveImportOptions
{
    // The folder the saves go to: -1 for the one the file says (a zip from
    // iSaveStoreExportZip keeps each save's), else A. And the slot: -1 for the
    // number the save carries (SpongeBobMovie02 goes to slot 2).
    S32 target;
    S32 game;

    // Which save of a file holding several (a card, a zip), counted from 1 in
    // the order --check lists them. 0 for all of them.
    S32 pick;

    // Replace a save already in the slot (it is kept as backup .1 either way).
    bool force;

    // Look only: check and report, write nothing.
    bool dryRun;
};

void iSaveImportDefaults(iSaveImportOptions* o);

struct iSaveImportItem
{
    std::string source; // where in the file: "card file 3", "B/78-GGVE-SpongeBobMovie01.gci"
    S32 target;
    S32 game;
    bool ok;
    std::string message; // what happened, or why not

    // The save's own level name and progress, when it could be read (progress
    // -1 when not).
    std::string label;
    S32 progress;
};

// Imports from a .gci (Dolphin's export), a .gcs, a raw card image (.raw,
// .gcp), a zip from iSaveStoreExportZip, or a bare save file from another
// install -- told apart by their contents. TRUE when every save it found was
// imported (or would be, with dryRun); `items` says what happened to each.
bool iSaveStoreImport(const char* file, const iSaveImportOptions* o,
                      std::vector<iSaveImportItem>& items, char* err, size_t errsize);
bool iSaveStoreImportData(const U8* data, size_t n, const char* name, const iSaveImportOptions* o,
                          std::vector<iSaveImportItem>& items, char* err, size_t errsize);

// --- export --------------------------------------------------------------------------

struct iSaveExportOptions
{
    // The game code to write: "GGVE" (North America) unless the save came off
    // a card with another, or this says otherwise. A P region ("GGVP") also
    // writes the date the PAL way.
    char gamecode[5];
};

void iSaveExportDefaults(iSaveExportOptions* o);

// A slot as a .gci, in memory or to a file. A save that came off a card and has
// not been saved over since comes back byte for byte as it was.
bool iSaveStoreExportGCIData(S32 target, S32 game, const iSaveExportOptions* o,
                             std::vector<U8>& out, char* err, size_t errsize);
bool iSaveStoreExportGCI(S32 target, S32 game, const iSaveExportOptions* o, const char* path,
                         char* err, size_t errsize);

// Every save as one zip of .gci files ("A/78-GGVE-SpongeBobMovie01.gci", ...),
// which iSaveStoreImport takes back slot for slot. Returns how many saves went
// in, -1 on failure.
S32 iSaveStoreExportZipData(const iSaveExportOptions* o, std::vector<U8>& out, char* err,
                            size_t errsize);
S32 iSaveStoreExportZip(const iSaveExportOptions* o, const char* path, char* err, size_t errsize);

// Every save as a .gci in a folder, named as Dolphin names them, the way
// Dolphin's GCI folder holds a card: folder A's saves in `dir` itself, the
// other folders' in B/, C/... inside it. Returns how many were written;
// `items` says what happened to each save there is (a damaged one is
// skipped).
struct iSaveExportItem
{
    char name[4]; // the slot: "A1"
    bool ok;
    std::string path; // where it went
    std::string message; // why not
};

S32 iSaveStoreExportFolder(const iSaveExportOptions* o, const char* dir,
                           std::vector<iSaveExportItem>& items);

// --- backups ---------------------------------------------------------------------------

struct iSaveBackupInfo
{
    S32 which; // 1 newest .. ISS_BACKUPS
    char path[512];
    S64 mtime;
    bool valid;
    char label[64];
    S32 progress;
};

void iSaveStoreBackups(S32 target, S32 game, std::vector<iSaveBackupInfo>& out);

// Puts backup `which` back in the slot. What was there becomes backup .1.
bool iSaveStoreRestore(S32 target, S32 game, S32 which, char* err, size_t errsize);

// Empties a slot. The save becomes backup .1, so this can be undone.
bool iSaveStoreDelete(S32 target, S32 game, char* err, size_t errsize);

// --- after a crash ---------------------------------------------------------------------

// Puts a slot right if the game could not use what is in it: a save file that
// is empty, cut short, or fails its checksum (what a system crash or a power
// cut during a save leaves) gets the newest good version back -- the save that
// was being written, if it was whole, else the newest good backup -- and the
// damaged file is kept as backup/<name>.damaged. A slot with no save file stays
// empty unless the save being written into it when the game stopped is whole.
// A save the game can load is never damaged by this: it stays, unless the
// temporary beside it is a whole save no older than it (the game stopped
// between writing the new save and renaming it into place), which goes in with
// the one it replaces as backup 1. TRUE when something was put back; `report`
// says what was done, or what was wrong and could not be put right, and is
// empty when the slot was fine. isavegame.cpp runs it on every slot when the
// game starts, in the one process that holds the folder.
bool iSaveStoreRecover(S32 target, S32 game, char* report, size_t reportsize);

// --- one game at a time ------------------------------------------------------------------

// Claims the save folder for this process, so that a second copy of the game
// started on the same folder can tell (iHostClaimInstance, on a key made from
// iSaveStoreRoot()). Two copies on one folder both take every controller press
// -- input reaches a window that is not in front -- and their saves overwrite
// each other's. 1: claimed. 0: another running copy has it, and `holder` (if
// not NULL) gets its process id, or 0. -1: the host cannot say. The claim lasts
// until the process ends, a crash included.
S32 iSaveStoreClaimFolder(U32* holder);

// Held for the length of a call that changes the folder. On Android, in the
// co-op app, its start marks the shared saves' copy as not yet sent and its
// end sends what changed (android/iAndroidSaves.h); elsewhere it does nothing.
struct iSaveStoreChange
{
    iSaveStoreChange();
    ~iSaveStoreChange();
};

// --- the game in progress ------------------------------------------------------------

// The slot the running game autosaves to: the one it last saved to or loaded
// from, while its autosave is armed (xsavegame's autosave cache). FALSE when
// there is none -- at the title, or before the first save or load. Something
// imported over that slot is saved over again at the game's next autosave.
// Defined in isavegame.cpp, which is where the game's save code is.
bool iSaveStoreAutoSaveSlot(S32* target, S32* game);

#endif
