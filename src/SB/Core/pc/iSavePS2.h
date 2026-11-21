#ifndef ISAVEPS2_H
#define ISAVEPS2_H

// PC-only: the PlayStation 2 release's save files, as data. Nothing here
// touches the disk; iSaveStore.h is the store built on it and docs/SAVES.md
// section 8 describes the layout.
//
// On a memory card the game keeps one folder:
//
//   BASLUS-20904SBMovie /          it really ends in a space (PAL:
//                                  BESLES-52895SBMovie )
//       icon.sys                   964 bytes: the title and how the browser
//                                  lights the icon
//       SpongeIcon01.ico           the icon the console's browser shows
//       BASLUS-20904SBMovie        992 bytes of filler, named like the folder
//       SpongeBobMovie01..03       the saves: the payload in little-endian
//                                  form (iSaveLE.h) and nothing else
//
// The same folder travels in several wrappers, all read here:
//
//   .psu           uLaunchELF's (EMS) file: the card's own directory entries,
//                  each file's bytes after its entry. Written too.
//   .max           Action Replay Max: the files, LZARI-packed. Read only.
//   .psv           what a PlayStation 3 exports from a virtual memory card.
//                  Read and written, with one of two signatures (below).
//   a card image   PCSX2's .ps2 file (or any raw card dump): the card's file
//                  system, with or without the 16 spare bytes after each page
//                  that hold its error-correcting code. Read and written.
//
// PCSX2's folder-type card is plain files on the host and is the store's
// business (iSaveStore.h).
//
// A PS3 takes a .psv only with its own signature (iSavePS2PsvSign), made from
// key values that are in the PS3, not in this port. So a .psv written here
// carries one of two:
//
//   stand-in   the same computation with every key value zero. A valid file
//              that other copies of this port and PC tools that read .psv
//              files take; a PS3 needs it re-signed first (a save tool on a
//              modded PS3 does that).
//   real       the key values read from a file the player provides
//              (iSavePS2PsvKeysParse: 32 bytes, the key, then its IV). Made as
//              the public description of the PS3's signing says; not yet
//              confirmed on a PS3.
//
// No key is in this code, none is asked for as text, and nothing here prints
// one. Reading a .psv never needs one.
//
// A PS3's virtual memory card for PS2 games (.VM2) is a card image without the
// spare bytes, which the card functions below read and write as they are.
//
// Only <types.h>, the C library and std::vector/std::string.

#include <types.h>

#include <stddef.h>
#include <string>
#include <vector>

#define IPS2_FOLDER_NTSC "BASLUS-20904SBMovie "
#define IPS2_FOLDER_PAL "BESLES-52895SBMovie "
#define IPS2_ICON_SYS "icon.sys"
#define IPS2_ICON_FILE "SpongeIcon01.ico"
#define IPS2_ICON_SYS_SIZE 964
#define IPS2_FILLER_SIZE 992
#define IPS2_ICON_ASSET 0xF18ABAEFu // xStrHash("SpongeIcon"), a RAW asset of the PS2's PLAT.HIP

// A directory entry's mode, as the game's files and folder have it.
#define IPS2_MODE_FILE 0x8497u
#define IPS2_MODE_DIR 0x8427u
#define IPS2_MODE_ISFILE 0x0010u
#define IPS2_MODE_ISDIR 0x0020u
#define IPS2_MODE_EXISTS 0x8000u

#define IPS2_ENTRY_SIZE 512
#define IPS2_PAGE_SIZE 512
#define IPS2_SPARE_SIZE 16
#define IPS2_CLUSTER_SIZE 1024
#define IPS2_CARD_CLUSTERS 8192 // an 8 MB card

struct iSavePS2File
{
    std::string name;
    std::vector<U8> data;
    U32 mode;
    U32 attr;
    S64 created; // Unix seconds; 0 for unknown
    S64 modified;
};

struct iSavePS2Folder
{
    std::string name;
    U32 mode;
    U32 attr;
    S64 created;
    S64 modified;
    std::vector<iSavePS2File> files;
};

void iSavePS2FolderClear(iSavePS2Folder* f);

// The file of `name` in a folder, or NULL.
iSavePS2File* iSavePS2Find(iSavePS2Folder* f, const char* name);
const iSavePS2File* iSavePS2Find(const iSavePS2Folder* f, const char* name);

// --- names -----------------------------------------------------------------------------

// Whether `name` is the game's save folder, NTSC or PAL. A host that cannot
// keep a name ending in a space has '_' there (PCSX2's folder cards), and some
// tools drop it: all three are taken.
bool iSavePS2IsGameFolder(const char* name, bool* pal);

// "SpongeBobMovie02" -> 1; -1 for anything else.
S32 iSavePS2GameOfName(const char* name);
void iSavePS2SaveName(S32 game, char* out, size_t outsize);

// A name as a host can hold it, the way PCSX2 cleans one: \ % : | " < > and
// any dots or spaces at its end become '_'.
std::string iSavePS2HostName(const char* name);

// --- time ------------------------------------------------------------------------------

// A directory entry's eight time bytes (0, second, minute, hour, day, month,
// year as two bytes). The console's clock is Japan's, nine hours ahead of UTC.
void iSavePS2TimePack(S64 unixSeconds, U8 out[8]);
S64 iSavePS2TimeUnpack(const U8 in[8]);

// --- the files beside the saves ----------------------------------------------------------

// icon.sys as the game builds it: "PS2D", the title "SpongeBob - The Movie"
// in Shift-JIS with its line break, the game's colours and lights, and
// SpongeIcon01.ico named for all three icons.
void iSavePS2IconSys(std::vector<U8>& out);

// The filler file: 992 bytes. The game's own carries twenty lines of its
// programmers' text, which is not this port's to ship; here the lines are
// empty. Nothing reads them: the game only looks for the file.
void iSavePS2Filler(std::vector<U8>& out);

// An icon made by the port from its own art (the sponge block of the app
// icon): a slab with the block drawn on it, in the console's icon format.
void iSavePS2OwnIcon(std::vector<U8>& out);

// Whether `p` is a well-formed icon file: its header, vertices, animation and
// texture add up to exactly `n` bytes.
bool iSavePS2IconCheck(const U8* p, size_t n, char* err, size_t errsize);

// The game's own icon out of a package (the PlayStation 2 disc's PLAT.HIP has
// it; the other consoles' packages do not). FALSE when it is not there.
bool iSavePS2IconFromPackage(const U8* hip, size_t n, std::vector<U8>& icon);

// --- .psu ------------------------------------------------------------------------------

bool iSavePS2PsuIs(const U8* p, size_t n);
bool iSavePS2PsuParse(const U8* p, size_t n, iSavePS2Folder* out, char* err, size_t errsize);
void iSavePS2PsuBuild(const iSavePS2Folder* folder, std::vector<U8>& out);

// --- .max (read only) ----------------------------------------------------------------------

bool iSavePS2MaxIs(const U8* p, size_t n);
bool iSavePS2MaxParse(const U8* p, size_t n, iSavePS2Folder* out, char* err, size_t errsize);

// --- .psv --------------------------------------------------------------------------------

#define IPS2_PSV_SEED 0x08 // where the seed is in the file
#define IPS2_PSV_SEED_SIZE 20
#define IPS2_PSV_SIGNATURE 0x1C // and the signature
#define IPS2_PSV_SIGNATURE_SIZE 20
#define IPS2_PSV_KEY_SIZE 16
#define IPS2_PSV_KEYS_FILE_SIZE 32 // the key, then its IV

bool iSavePS2PsvIs(const U8* p, size_t n);
bool iSavePS2PsvParse(const U8* p, size_t n, iSavePS2Folder* out, char* err, size_t errsize);

// The name a PS3 gives a folder's .psv: the first 12 characters, the rest as
// hex, then ".PSV" ("BASLUS-20904SBMovie " -> "BASLUS-2090453424D6F76696520.PSV").
std::string iSavePS2PsvName(const char* folder);

// The two key values a real signature is made with, out of the player's file:
// 32 bytes, the key the PS3 signs PlayStation 2 saves with (AES-128), then its
// 16-byte IV. FALSE with `err` (which never holds any of the file's bytes)
// when the file is another size or all zeros (that would only be the stand-in
// signature again).
struct iSavePS2PsvKeys
{
    U8 key[IPS2_PSV_KEY_SIZE];
    U8 iv[IPS2_PSV_KEY_SIZE];
};
bool iSavePS2PsvKeysParse(const U8* file, size_t n, iSavePS2PsvKeys* out, char* err,
                          size_t errsize);
void iSavePS2PsvKeysClear(iSavePS2PsvKeys* k);

// A .psv of `folder`, its files in the folder's order. `seed` (20 bytes) NULL
// is 00 01 .. 13, as PC tools write it; `keys` NULL is the stand-in signature.
void iSavePS2PsvBuild(const iSavePS2Folder* folder, const U8* seed, const iSavePS2PsvKeys* keys,
                      std::vector<U8>& out);

// The signature at 0x1C, made (over the whole file, those 20 bytes counted as
// zero) or checked. `keys` NULL is the stand-in.
void iSavePS2PsvSign(U8* psv, size_t n, const iSavePS2PsvKeys* keys);
bool iSavePS2PsvVerify(const U8* psv, size_t n, const iSavePS2PsvKeys* keys);

// --- a card image ------------------------------------------------------------------------

// The three code bytes of one 128-byte quarter of a page.
void iSavePS2Ecc(const U8* chunk, U8 ecc[3]);

// Whether `p` starts like a formatted card.
bool iSavePS2CardIs(const U8* p, size_t n);

// A blank formatted 8 MB card: with the spare bytes (PCSX2's .ps2,
// 8,650,752 bytes) or without (8,388,608).
void iSavePS2CardFormat(std::vector<U8>& card, bool withEcc, S64 now);

// The names of the folders in the card's root.
bool iSavePS2CardList(const U8* card, size_t n, std::vector<std::string>& names, char* err,
                      size_t errsize);

// One of them, with its files' contents. FALSE with `err` when it is not there
// or the card's tables are damaged.
bool iSavePS2CardReadFolder(const U8* card, size_t n, const char* name, iSavePS2Folder* out,
                            char* err, size_t errsize);

// Writes `folder`'s files into the card's folder of that name, which is made
// if it is not there. A file already there is replaced; every other file and
// folder on the card stays as it is. The pages written get their code bytes.
// On failure (a full card, damaged tables) `card` is left exactly as it was.
bool iSavePS2CardWriteFolder(std::vector<U8>& card, const iSavePS2Folder* folder, S64 now,
                             char* err, size_t errsize);

// Looks the whole card over: every written page's code bytes (a card with
// them), and every folder's and file's chain through the allocation table.
// `freeClusters` (if not NULL) gets the free space in KB.
bool iSavePS2CardCheck(const U8* card, size_t n, U32* freeClusters, char* err, size_t errsize);

#endif
