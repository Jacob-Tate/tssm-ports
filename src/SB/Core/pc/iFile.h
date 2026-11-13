#ifndef IFILE_H
#define IFILE_H

#include <types.h>

#include <stddef.h>

enum IFILE_READSECTOR_STATUS
{
    IFILE_RDSTAT_NOOP,
    IFILE_RDSTAT_INPROG,
    IFILE_RDSTAT_DONE,
    IFILE_RDSTAT_FAIL,
    IFILE_RDSTAT_QUEUED,
    IFILE_RDSTAT_EXPIRED
};

// PC iFile.h: treedome's pc/iFile.h (host files, asset root and mod folder)
// with TSSM's interface: const names in iFileLoad/iFileSetPath, iFileFind's
// flags argument, and iFileExists/iFileGetSectorSize/xFileDriveType. The
// asset-override hook (BfBB's code-built menus) is not carried.

// The GameCube and PS2 shapes of this struct are both in the gc header, chosen
// by #ifdef; this is the third. It is embedded by value in tag_xFile, which is
// a runtime type and never overlaid on asset data, so the layout is ours to
// choose.
struct iModPkg;

struct tag_iFile
{
    U32 flags;
    char path[512];
    void* handle;
    S32 offset;
    S32 length;
    S32 asynckey;

    // A package some mod replaces assets in (iMod.h): reads are served from
    // this view of the file at `handle`, and `length` is the view's.
    iModPkg* view;
};

enum xFileDriveType
{
    XFILE_DRIVE_CDVD,
    XFILE_DRIVE_CONSOLE_HD
};

#define IFILE_OPEN_READ 0x1
#define IFILE_OPEN_WRITE 0x2
#define IFILE_OPEN_ABSPATH 0x4

#define IFILE_SEEK_SET 0
#define IFILE_SEEK_CUR 1
#define IFILE_SEEK_END 2

struct tag_xFile;

void iFileInit();
void iFileExit();
U32* iFileLoad(const char* name, U32* buffer, U32* size);
U32 iFileOpen(const char* name, S32 flags, tag_xFile* file);

// PC-only. Where the game's own files are: `[assets] path` from config.ini,
// or TSSM_ASSETS when that is set. "" when neither says, which means the
// working directory. Separators are slashes whichever was typed.
const char* iFileAssetRoot();

// PC-only. The mod folder: `[assets] mod` from config.ini, or TSSM_MOD when
// that is set. "" when there is none. A file under it is read in place of the
// asset root's file at the same relative path; the asset root is never
// written to.
//
// This is the single-folder form. The mods/ folder of iMod.h (many mods, a
// load order, per-asset, texture, sound, movie and INI overrides) is applied
// under it: a file this folder has wins over every mod's.
const char* iFileModRoot();

// PC-only. The last component of the mod folder iFileInit accepted, like
// "BFBBMix", or "" when no mod is in use. isavegame keeps a mod's saves apart
// under this name.
const char* iFileModName();

// PC-only. Whether the mod folder supplies the file at `rel`, a path relative
// to the asset root like "mn/mnu3.HIP". Asks the same question a read does, so
// a package from another console's release is not counted. After iFileInit.
bool iFileModReplaces(const char* rel);

// PC-only. The full path of the first file the game cannot run without that
// is not under the asset root, or NULL when they are all there. Asked once by
// iSystemInit, because the alternative is a hang: zMainLoadFontHIP spins on
// `while (xSTLoadStep('FONT') < 1.0f)` with no exit and no caller to fail to.
const char* iFileMissingAssetPath();

// PC-only. Which console's packer wrote the assets. Returns the two-letter id
// out of boot.HIP's PLAT chunk -- "XB", "GC", "PS2" -- and sets *longName to
// the spelling beside it ("Xbox", "GameCube"). NULL when the file cannot be
// read or carries no PLAT, which is not treated as an answer either way.
const char* iFileAssetPlatform(const char** longName);

// PC-only. A scene package (xSTPreLoadScene's sid, flg_hiphop and file name, or
// NULL) the game could not load: names the file and where it should be, shows
// it, and stops the process. Called from zSceneLoadPackFile.
void iFileMissingPackage(U32 sid, S32 options, const char* filename);


S32 iFileSeek(tag_xFile* file, S32 offset, S32 whence);
U32 iFileRead(tag_xFile* file, void* buf, U32 size);
S32 iFileReadAsync(tag_xFile* file, void* buf, U32 aSize, void (*callback)(tag_xFile*),
                   S32 priority);
IFILE_READSECTOR_STATUS iFileReadAsyncStatus(S32 key, S32* amtToFar);
U32 iFileClose(tag_xFile* file);
U32 iFileGetSize(tag_xFile* file);
void iFileReadStop();
void iFileFullPath(const char* relname, char* fullname);
void iFileSetPath(const char* path);
U32 iFileFind(const char* name, int fileflags, tag_xFile* file);
void iFileGetInfo(tag_xFile* file, U32* starting_sector, U32* size_in_bytes);
void iFileAsyncService();

// TSSM additions (gc/iFile.h).
U8 iFileExists(const char* name, S32 flags);

// PC: a host path ('/' separators) whose components may be spelled in the wrong
// case, rewritten in place to the spelling on disk. false when some component has
// no match.
bool iFileResolveCase(char* path, size_t pathsize);
U32 iFileGetSectorSize(xFileDriveType drive);

#endif
