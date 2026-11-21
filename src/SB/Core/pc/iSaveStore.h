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

// The largest file read as a save, a card or a zip of saves. Card images are
// the largest: 2043 blocks of 8 KB, 16 MB.
#define ISS_MAX_INPUT (64u * 1024u * 1024u)

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

    // For saying which signature an Xbox save carries (it is imported either
    // way): the Xbox program and the keys.bin to check with, ahead of
    // config.ini's `[game] xbox_xbe` and `xbox_keys`. NULL or "" for those.
    const char* xboxXbe;
    const char* xboxKeys;

    // The same for a PlayStation 3's .psv: the PS3 key file to check its
    // signature with, ahead of config.ini's `[game] psv_keys`.
    const char* psvKeys;
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
// .gcp), a zip from iSaveStoreExportZip, a bare save file from another
// install, an Xbox save folder or a PlayStation 2 save in any of its forms
// (below) -- told apart by their contents. TRUE when every save it found was
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

// --- the other consoles ------------------------------------------------------------------
//
// The PlayStation 2 and Xbox releases keep the same save in little-endian
// order (iSaveLE.h), and converting one needs to know what each level saves,
// which is read out of the game's own files: the level packages under the
// asset folder (`[assets] path`, or TSSM_ASSETS), any release's. A save that
// has visited a level whose package is not there cannot be converted, and the
// message says which file is missing.

// The folder the level packages are read from, for a caller that is not the
// game (the tests). NULL or "" goes back to the game's asset folder.
void iSaveStoreSetAssetRoot(const char* dir);

// A canonical payload as the little-endian consoles store it, and back. Byte
// for byte each other's inverse. `notes` (if not NULL) gets a line for each
// block that holds more than this copy of its level saves (iSaveLE.h): it is
// converted all the same.
bool iSaveStoreToLE(const U8* canonical, size_t n, std::vector<U8>& le, std::string* notes,
                    char* err, size_t errsize);
bool iSaveStoreFromLE(const U8* le, size_t n, std::vector<U8>& canonical, std::string* notes,
                      char* err, size_t errsize);

// --- Xbox --------------------------------------------------------------------------------
//
// An Xbox save is a folder (iSaveXbox.h): UDATA/54510027/<12 hex digits>/ with
// SaveMeta.xbx and GameData.xsv, the payload and a signature. iSaveStoreImport
// takes one as a folder -- the save's own, the game's (54510027), a whole
// UDATA, or Cxbx-Reloaded's data folder -- or as a zip of any of those, and
// takes it whatever its signature.
//
// Exporting has to sign, and there are two signatures (the player's choice):
//
//   ISS_XBOX_EMULATOR  what Cxbx-Reloaded computes with no keys.bin. Needs the
//                      player's DEFAULT.XBE and nothing else. Cxbx-Reloaded and
//                      other copies of this port load it; a real Xbox does not.
//   ISS_XBOX_REAL      also needs the keys.bin the player made from their own
//                      Xbox (the 32-byte file Cxbx-Reloaded reads). A real Xbox
//                      and xemu load it.
//
// The port holds no key, never takes one as text and never prints one.

enum
{
    ISS_XBOX_EMULATOR,
    ISS_XBOX_REAL
};

struct iSaveXboxExportOptions
{
    S32 signature; // ISS_XBOX_EMULATOR or ISS_XBOX_REAL

    // The Xbox program. "": config.ini's `[game] xbox_xbe`, else DEFAULT.XBE
    // in the game's asset folder (where it is when the Xbox files are the ones
    // in use).
    char xbe[512];

    // keys.bin, for ISS_XBOX_REAL. "": config.ini's `[game] xbox_keys`.
    char keys[512];

    // Which of the Xbox's three saves it becomes: 0..2, or -1 for the slot's
    // own number.
    S32 number;

    // Write over a save of that name already in the folder.
    bool replace;
};

void iSaveXboxExportDefaults(iSaveXboxExportOptions* o);

// Where the Xbox program and the keys file would be read from, as an export
// with these settings would find them. FALSE with `err` saying what is needed
// when there is none. `given` may be NULL.
bool iSaveStoreXboxProgram(const char* given, char* path, size_t pathsize, char* err,
                           size_t errsize);
bool iSaveStoreXboxKeys(const char* given, char* path, size_t pathsize, char* err, size_t errsize);

// The same, and the file is read and checked as an export would: the program
// has to be this game's Xbox program, the keys file 32 bytes holding a
// certificate key. `path` is filled when a file was found, usable or not, so
// a caller can show which file the reason is about. Nothing of the files'
// contents is kept or goes into `err`.
bool iSaveStoreXboxProgramUsable(const char* given, char* path, size_t pathsize, char* err,
                                 size_t errsize);
bool iSaveStoreXboxKeysUsable(const char* given, char* path, size_t pathsize, char* err,
                              size_t errsize);

// The files iSaveStoreXboxDecode checks signatures with, ahead of config.ini's
// (iSaveImportOptions; the import sets and clears them). NULL or "" for none.
void iSaveStoreXboxLookWith(const char* xbe, const char* keys);

// A slot as an Xbox save under `dir`, laid out as the game writes it:
// <title folder>/<12 hex>/SaveMeta.xbx and GameData.xsv, and the title's own
// three files when they are not there yet (made from the player's XBE). The
// title folder is found from what `dir` is: Cxbx-Reloaded's data folder (one
// holding EmuDisk/Partition1) gets EmuDisk/Partition1/UDATA/54510027; a folder
// named UDATA gets 54510027 inside it; one named 54510027 is used as it is;
// anything else gets UDATA/54510027 inside it, ready to copy to an Xbox's E:
// drive. `where` (if not NULL) gets the save's folder.
bool iSaveStoreExportXbox(S32 target, S32 game, const iSaveXboxExportOptions* o, const char* dir,
                          std::string* where, char* err, size_t errsize);

// Every save of folder `target` (an Xbox holds three, as one folder does).
// Returns how many were written.
S32 iSaveStoreExportXboxFolder(S32 target, const iSaveXboxExportOptions* o, const char* dir,
                               std::vector<iSaveExportItem>& items);

// GameData.xsv's bytes as a canonical payload. `signature` (if not NULL) says
// what its signature is, as far as can be told: "emulator signature", "real
// signature (matches your keys.bin)", "signature not recognised" or
// "signature not checked". Information only: the save is read either way.
// `notes` as iSaveStoreFromLE's.
bool iSaveStoreXboxDecode(const U8* xsv, size_t n, std::vector<U8>& canonical,
                          std::string* signature, std::string* notes, char* err, size_t errsize);

// The saves under a folder, gathered into a zip in memory (every GameData.xsv
// with its SaveMeta.xbx, and every PlayStation 2 save file -- a
// SpongeBobMovie01..03 in little-endian form -- by relative path), which
// iSaveStoreImportData takes.
// For a caller that imports from bytes (the Saves tab). FALSE with `err` when
// the folder holds none. Without `deep` only the folder's own files are read,
// not the folders in it: for a GameData.xsv that was picked itself, whose
// folder may be anything (a downloads folder with other saves below it).
bool iSaveStoreReadFolder(const char* dir, std::vector<U8>& zip, char* err, size_t errsize,
                          bool deep = true);

// --- PlayStation 2 -----------------------------------------------------------------------
//
// On a PlayStation 2 memory card the game keeps one folder,
// "BASLUS-20904SBMovie " (iSavePS2.h), holding up to three saves and three
// files beside them. iSaveStoreImport takes it in any of the forms it travels
// in, told apart by their contents: a .psu file (uLaunchELF), an Action Replay
// Max .max file, a PlayStation 3's .psv, a memory card image (PCSX2's .ps2, with
// or without the spare bytes), and as a folder: PCSX2's folder-type card, the
// game's folder inside one, or any folder holding SpongeBobMovie01..03.
// Nothing is ever written to what is imported from.
//
// Exporting writes what the game writes: the save, and when the folder is new
// icon.sys, the icon and the filler file. The icon is the game's own when the
// player's files have it (the PlayStation 2 disc's PLAT.HIP, or a save the
// game made), else one the port makes from its own art.
//
// A PlayStation 3 holds PS2 saves on virtual memory cards and exports one
// game's folder as a .psv, which has to be signed. There are two signatures
// (the player's choice), with the same shape as the Xbox's:
//
//   ISS_PSV_STANDIN  the PS3's computation with every key value zero: a valid
//                    file for other copies of this port and PC tools that read
//                    .psv files. A PS3 needs it re-signed first (for example
//                    by a save tool on a modded PS3).
//   ISS_PSV_REAL     the PS3's own, made with the key values in a file the
//                    player provides (iSavePS2.h: 32 bytes). Made as publicly
//                    described; not yet confirmed on a PS3.
//
// The port holds no key, never takes one as text and never prints one. A
// PS3's virtual memory card file (.VM2) is a memory card file without the
// spare bytes, written to as a card.

enum
{
    ISS_PS2_AUTO, // from what the destination is (below)
    ISS_PS2_PSU, // a .psu file, for uLaunchELF on a console, or mymc
    ISS_PS2_FOLDER_CARD, // PCSX2's folder-type memory card
    ISS_PS2_CARD, // a memory card file (PCSX2's .ps2, a PS3's .VM2)
    ISS_PS2_PSV // a PlayStation 3's export (.psv)
};

enum
{
    ISS_PSV_STANDIN,
    ISS_PSV_REAL
};

struct iSavePS2ExportOptions
{
    // ISS_PS2_AUTO: an existing card file is a card, an existing .psv a .psv;
    // a folder holding _pcsx2_superblock is a folder card; a new name ending
    // ".ps2" becomes a new formatted card (".vm2": one without the spare
    // bytes, as a PS3 keeps it); a new name ending ".psv" a .psv; anything
    // else is a .psu (in a folder, one named BASLUS-20904SBMovie.psu). For
    // ISS_PS2_PSV a folder gets the name a PS3 gives it
    // (BASLUS-2090453424D6F76696520.PSV).
    S32 kind;

    // The PAL game's folder, BESLES-52895SBMovie , instead of the NTSC one.
    bool pal;

    // Where the game's own icon is: PLAT.HIP from the player's PlayStation 2
    // disc, a folder holding it, a SpongeIcon01.ico, or a save the game made
    // (.psu, .max, .psv, a card). "": config.ini's `[game] ps2_icon`, else the
    // game's files in use if they are the PlayStation 2's, else the port's own
    // icon.
    char icon[512];

    // Which of the card's three saves it becomes: 0..2, or -1 for the slot's
    // own number.
    S32 number;

    // Write over a save of that number already there.
    bool replace;

    // A .psv's signature: ISS_PSV_STANDIN or ISS_PSV_REAL.
    S32 psvSignature;

    // The PS3 key file, for ISS_PSV_REAL. "": config.ini's `[game] psv_keys`.
    char psvKeys[512];
};

void iSavePS2ExportDefaults(iSavePS2ExportOptions* o);

// A slot into `dest`. The game's folder there is added to: its other saves,
// and everything else on a card, stay as they are, and a save already there
// under the same number is refused unless `replace`. Before a card file is
// changed, a copy of it as it was is made beside it (<name>.bak, .bak2, ...),
// and without that copy nothing is written. `where` (if not NULL) says where
// it went, and where the copy is.
bool iSaveStoreExportPS2(S32 target, S32 game, const iSavePS2ExportOptions* o, const char* dest,
                         std::string* where, char* err, size_t errsize);

// Every save of folder `target` (a card's folder holds three, as one folder
// here does), in one go. Returns how many were written; 0 with `err` when the
// write failed.
S32 iSaveStoreExportPS2Folder(S32 target, const iSavePS2ExportOptions* o, const char* dest,
                              std::vector<iSaveExportItem>& items, std::string* where, char* err,
                              size_t errsize);

// The icon an export with these settings would write. `from` (if not NULL)
// gets the file it came out of, or "" for the port's own. FALSE with `err`
// when a place was named and holds no icon.
bool iSaveStorePS2Icon(const char* given, std::vector<U8>& icon, std::string* from, char* err,
                       size_t errsize);

// Where the PS3 key file would be read from, as an export with these settings
// would find it (`given`, else config.ini's `[game] psv_keys`). FALSE with
// `err` saying what is needed when there is none. The Usable form also reads
// and checks it (32 bytes, not all zeros); `path` is filled when a file was
// found, usable or not. Nothing of the file's contents is kept or goes into
// `err`.
bool iSaveStorePsvKeys(const char* given, char* path, size_t pathsize, char* err, size_t errsize);
bool iSaveStorePsvKeysUsable(const char* given, char* path, size_t pathsize, char* err,
                             size_t errsize);

// The key file a .psv's signature is checked with on import, ahead of
// config.ini's (iSaveImportOptions; the import sets and clears it).
void iSaveStorePsvLookWith(const char* keys);

// Which signature a .psv's bytes carry, as far as can be told: "stand-in
// signature (a PS3 needs it re-signed)", "real signature (matches your PS3
// key file)", "signature not recognised" (it is neither, by the key file at
// hand) or "signature unknown (...)" (no usable key file to tell by).
// Information only: the save is read either way.
std::string iSaveStorePsvSignature(const U8* psv, size_t n);

// The game's save folders in a file's bytes: 1 when the bytes are one of the
// PlayStation 2 forms (`out` may still be empty: a card with no save of this
// game), 0 when they are none of them, -1 with `err` when they are one and
// damaged. `kind` (if not NULL) says which form.
struct iSavePS2Folder;
S32 iSaveStorePS2Folders(const U8* p, size_t n, std::vector<iSavePS2Folder>& out,
                         std::string* kind, char* err, size_t errsize);

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

// Whether another running copy has claimed the save folder, without claiming
// it: the launcher asks before the game does (iHostInstanceHeld). `holder` as
// above.
bool iSaveStoreFolderHolder(U32* holder);

// --- the game in progress ------------------------------------------------------------

// The slot the running game autosaves to: the one it last saved to or loaded
// from, while its autosave is armed (xsavegame's autosave cache). FALSE when
// there is none -- at the title, or before the first save or load. Something
// imported over that slot is saved over again at the game's next autosave.
// Defined in isavegame.cpp, which is where the game's save code is.
bool iSaveStoreAutoSaveSlot(S32* target, S32* game);

#endif
