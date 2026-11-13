#ifndef IMOD_H
#define IMOD_H

#include <types.h>

#include <stddef.h>

// PC-only: mod support. New code; docs/MODDING.md is the modder's guide.
//
// A mods folder holds one folder per mod:
//
//   mods/
//     mods.ini              the load order and on/off switches (written by the
//                           mod manager; hand-editable)
//     mods.log              what the last run replaced (written at startup)
//     <Mod>/
//       mod.ini             [mod] name, author, version, description, enabled,
//                           priority
//       files/<path>        loose files: replace <asset folder>/<path> whole
//                           (a .HIP, sb04.ini, FMV/x.bik, ...)
//       assets/<key>        one asset of any package, by ID or by name
//       assets/<pkg>/<key>  the same, only in package <pkg> (file name, no
//                           extension: boot, font_US, bb01, ...)
//       textures/**/<name>.png|.dds|.tga|.bmp
//                           a texture, by its RenderWare name
//       sounds/<key>.wav|.ogg|...   (and sounds/<pkg>/<key>.*)
//                           a sound asset (SND/SNDS), by ID or by name
//       fmv/**/<movie>.*    a movie, by the name the game asks for
//       ini/<file>.ini      an overlay on one of the game's INI files
//
// Mods later in the load order win. Two enabled mods that replace the same
// thing are a conflict: both are listed (iModGetConflict) and the later one is
// used. Every override applied is logged to stdout, to mods.log and to
// iModLogLine.
//
// With no mods folder, no mods, or `[assets] mods = off`, nothing here changes
// what the game reads.
//
// Where the hooks are: iFile.cpp (loose files, packages, INI overlays),
// iModTexture.cpp + rw/texture.cpp (textures), iFMV.cpp (movies). Packages
// are replaced at the file level by iModPackage.cpp, so every reader of a
// package -- the packer, streamed textures, sounds, cutscenes -- sees the
// same, modded, table of contents.
//
// Changes to the list (enable, disable, reorder, rescan) apply to what is
// opened afterwards. Packages already loaded keep their assets until they are
// loaded again; iModRestartAdvised says when that matters.
//
// The legacy single mod folder, `[assets] mod` / TSSM_MOD (a copy of the asset
// folder's layout), still works and is applied over every mod here.

// --- life cycle ---------------------------------------------------------------------

// Finds the mods folder, reads every mod and mods.ini, builds the override
// index. Called by iFileInit; calling it again is iModRescan.
void iModInit();

// Reads the folder again (mods added, removed or edited on disk) and rebuilds
// the index. Returns the number of mods found.
S32 iModRescan();

// The folder mods are read from, without a trailing slash, or "" when there
// is none. iModDefaultFolder is where one should be created: beside the
// executable.
const char* iModFolder();
const char* iModDefaultFolder();

// `[assets] mods` (or TSSM_MODS): whether mods are applied at all.
S32 iModsEnabled();

// --- the list, for the mod manager --------------------------------------------------

enum iModKind
{
    IMOD_KIND_FILE,
    IMOD_KIND_ASSET,
    IMOD_KIND_TEXTURE,
    IMOD_KIND_SOUND,
    IMOD_KIND_MOVIE,
    IMOD_KIND_INI,
    IMOD_KIND_COUNT
};

// "file", "asset", "texture", "sound", "movie", "ini".
const char* iModKindName(S32 kind);

struct iModInfo
{
    char folder[64]; // the folder's name: the mod's identity in mods.ini
    char path[512]; // its full path, ending in '/'
    char name[128]; // mod.ini name, or the folder's
    char author[128];
    char version[32];
    char description[512];
    S32 enabled;
    S32 priority; // mod.ini priority; places a mod mods.ini does not list
    S32 counts[IMOD_KIND_COUNT]; // what it replaces, by kind
    S32 conflicts; // overrides it shares with another enabled mod
    char problem[256]; // why something in it was ignored, or ""
};

// In load order: index 0 is applied first, the last one wins.
S32 iModCount();
const iModInfo* iModGet(S32 index);

// The index of the mod in `folder` (case-insensitive), or -1.
S32 iModFind(const char* folder);

// Switch a mod on or off / move it to `newIndex`. The index is rebuilt at
// once; mods.ini is written only by iModSaveList. False for a bad index.
bool iModSetEnabled(S32 index, S32 enabled);
bool iModMove(S32 index, S32 newIndex);

// Writes the load order and switches to mods.ini, creating the mods folder if
// there is none. False if it cannot be written.
bool iModSaveList();

// True once the list has changed since the first package was opened: what is
// already loaded predates the change, so a restart shows it everywhere.
bool iModRestartAdvised();

// --- conflicts ---------------------------------------------------------------------------

struct iModConflict
{
    S32 kind; // iModKind
    char key[160]; // what both replace: a path, "boot: 0x1D327E54", a texture name
    S32 winner; // mod indices
    S32 loser;
};

// Found when the list is indexed (same file, same key) and when a package is
// opened (two mods' keys naming the same asset, e.g. one by ID, one by name).
S32 iModConflictCount();
const iModConflict* iModGetConflict(S32 index);

// --- the log -----------------------------------------------------------------------------

// What was found and applied, oldest first; also printed with a "tssm: mod:"
// prefix and written to mods.log.
S32 iModLogCount();
const char* iModLogLine(S32 index);
void iModLogf(const char* fmt, ...);

// --- for the platform layer --------------------------------------------------------

// A loose file replacing `rel` (a path under the asset folder, any case, '/'
// separated). Writes its full path to `out`.
bool iModResolveFile(const char* rel, char* out, size_t outsize);

// The modded view of the package at `path` (iModPackage.h), with a reference
// for the caller, or NULL when no enabled mod replaces anything in it.
struct iModPkg;
iModPkg* iModOpenPackage(const char* path);

// The replacement image for the texture called `name`, or NULL.
const char* iModFindTexture(const char* name);

// A replacement for the movie the game names `gameName` ("FMV\HILogo.bik"),
// from fmv/ or files/FMV/. Writes its full path to `out`.
bool iModFindMovie(const char* gameName, char* out, size_t outsize);

// Applies every enabled overlay for the INI file `rel` to `text` (`len` bytes,
// as loaded). Returns a new malloc'd buffer padded with zeros to a multiple of
// 32 bytes, with its padded size in *outSize, or NULL when there is no
// overlay for the file.
void* iModApplyIniOverlays(const char* rel, const char* text, U32 len, U32* outSize);

// Sets up the texture hook (iModTexture.cpp). Called by iModInit.
void iModTextureInstall();

#endif
