#ifndef ISAVEFORMAT_H
#define ISAVEFORMAT_H

// PC-only: the save file formats, as data. Nothing here touches the disk or
// the game; iSaveStore.h is the store built on it, and docs/SAVES.md describes
// every format below with offsets.
//
// THE PAYLOAD. What xsavegame.cpp builds and hands to iSGSaveFile: a 16-byte
// header ('GDAT', version 1, size, CRC) and a run of client blocks (tag, room,
// bytes used, then `room` bytes). It is the same game logic on every platform,
// but the game writes it with memcpy, so its integers are in the host's byte
// order. The console is big-endian. The port keeps every payload on disk in the
// console's order -- the CANONICAL payload -- and converts at the edge
// (isavegame.cpp), so a PC save file IS a GameCube payload and the other way
// round. Only the wrapper differs:
//
//   GameCube   .gci: a 64-byte directory entry, then the card file: an 0x4000
//              head (title, comment, banner, icon, a footer the game checks)
//              and the payload, padded to the card's 8 KB blocks.
//   PC         the payload alone, SpongeBobMovie01..03 in the save folder.
//
// The bit stream the serializer clients carry needed one change in shared code
// to be host-independent (xserializer.cpp, XSER_ELEMENT_BYTE); with it the
// payload a PC build writes is byte-for-byte the one the console would.

#include <types.h>

#include <stddef.h>
#include <string>
#include <vector>

// --- the card ---------------------------------------------------------------

#define ISF_BLOCK_SIZE 0x2000 // a memory card block, and the file size unit
#define ISF_DENTRY_SIZE 0x40 // a directory entry, the head of a .gci
#define ISF_HEAD_SIZE 0x4000 // isavegame's banner/icon area, rounded to blocks
#define ISF_ICONDATA_SIZE 0x2440 // gc/isavegame.cpp's IconData
#define ISF_BANNER_OFFSET 0x40 // 96x32 RGB5A3
#define ISF_BANNER_SIZE 0x1800
#define ISF_ICON_OFFSET 0x1840 // 32x32 RGB5A3, one frame
#define ISF_ICON_SIZE 0x800
#define ISF_FOOTER_OFFSET 0x2240
#define ISF_COMMENT_SIZE 0x20 // each of the comment's two lines

// The strings gc/isavegame.cpp writes into every file's head, the first and
// the last of which iSG_curKosher checks on the console before it will load.
#define ISF_GAME_NAME "The SpongeBob Movie"
#define ISF_FOOTER "SPONGEBOB2:PATRICK'SREVENGE::RyanNeilDan"
#define ISF_FILE_BASE "SpongeBobMovie" // + "01".."03"
#define ISF_MAKER "78" // the disc header's maker code
#define ISF_GAMECODE_NTSC "GGVE" // gc/isavegame.cpp's ISG_GC_GAMENAME
#define ISF_NUM_GAMES 3

// --- the payload ---------------------------------------------------------------

#define ISF_TAG(a, b, c, d) (((U32)(a) << 24) | ((U32)(b) << 16) | ((U32)(c) << 8) | (U32)(d))

#define ISF_PAYLOAD_HEADER 0x10
#define ISF_CLIENT_HEADER 0x0C
#define ISF_LEADER_SIZE 0x58 // st_XSAVEGAME_LEADER on the console
#define ISF_LEADER_FUN_SIZE 0x16 // "--TakeMeToYourLeader--", after it
#define ISF_LEADER_READ 0x74 // xSG_grab_leaders' read: header, block header, leader

// One client block, as the walk found it.
struct iSaveBlock
{
    U32 tag;
    U32 offset; // of the block's data, past its 12-byte header
    S32 room; // "maxamt": bytes the block occupies
    S32 used; // "realamt": bytes of it the client wrote
};

// What a canonical payload says about itself.
struct iSavePayloadInfo
{
    U32 size; // the header's size field
    U32 crcStored;
    U32 crcComputed;
    bool crcOK;

    S32 version; // the header's, 1
    S32 serialVersion; // the SVID block's, 1; -1 if there is none

    char label[64]; // the leader: the level's name, as the game showed it
    S32 progress; // percent
    S64 gametime; // always 0 in TSSM
    S32 levelIndex; // thumbIconIdx: world - 1
    char scene[8]; // the ROOM block: the scene the save resumes in, "HB01"

    S32 serialClients; // PLYR, CNTR and one block per scene visited
    std::vector<iSaveBlock> blocks;
};

// Walks and checks a canonical payload: magic, version, size, every block's
// bounds, the CRC, and the blocks the game cannot load without. `n` may be
// longer than the payload (a card file's padding); the header's size is the
// payload. TRUE with `info` filled for a payload the game will load; FALSE
// with `err` saying why (info is still filled as far as the walk got).
bool iSavePayloadInspect(const U8* p, size_t n, iSavePayloadInfo* info, char* err, size_t errsize);

// xUtil_crc_update's CRC, reimplemented so the store does not need the game:
// CRC-32 with polynomial 0x04C11DB7, MSB first, no reflection, no final xor.
// Start from 0xFFFFFFFF. The payload's covers bytes 0x10..size.
U32 iSaveCRC(U32 accum, const U8* p, size_t n);

// --- canonical <-> host --------------------------------------------------------

// Where the host's st_XSAVEGAME_LEADER keeps its fields (offsetof/sizeof on
// the host; the console's is 0x58 bytes with the S64 at 0x48). Filled in by
// isavegame.cpp, which can see the struct.
struct iSaveHostLayout
{
    U32 leaderSize;
    U32 offProgress;
    U32 offGametime;
    U32 offThumb;
};

enum iSaveConvertDir
{
    ISF_TO_HOST,
    ISF_TO_CANONICAL
};

// Converts a payload between the console's byte order and the host's, block by
// block: the header and block headers, the leader's fields, PREF and SVID's
// words, and every serializer block's words (the bit stream is stored as
// S32s). ROOM and SFIL are bytes and are copied.
//
// `n` may be shorter than the payload -- iSGReadLeader reads only the first
// 0x74 bytes -- and what is there is converted. For a whole payload the CRC is
// rewritten for the converted bytes when the source's own CRC held, and made
// to fail when it did not, so the game's check (xSG_ld_validate) reaches the
// same verdict on either side. The two buffers must not overlap. FALSE when the
// block walk broke off (the rest is copied as it was).
bool iSavePayloadConvert(iSaveConvertDir dir, const U8* src, U8* dst, size_t n,
                         const iSaveHostLayout* host);

// --- .gci ------------------------------------------------------------------------

// A card file: its directory entry, decoded, and its blocks.
struct iSaveGCI
{
    char gamecode[5];
    char maker[3];
    char filename[33];
    U8 bannerFormat;
    U32 time; // seconds since 2000-01-01, the console's local clock
    U32 iconAddr;
    U16 iconFormat;
    U16 iconSpeed;
    U8 permission;
    U8 copyTimes;
    U16 startBlock;
    U16 length; // blocks
    U32 commentAddr;

    const U8* dentry; // ISF_DENTRY_SIZE bytes
    const U8* data; // length * ISF_BLOCK_SIZE bytes
    size_t dataSize;
};

// Any game's: a 0x40 entry and `length` blocks. Also takes a GameShark .gcs
// (an 0x110-byte "GCSAVE" header first). The result points into `file`.
bool iSaveGCIParse(const U8* file, size_t n, iSaveGCI* out, char* err, size_t errsize);

// This game's file: maker 78, a GGV* game code, named SpongeBobMovie01..03.
// Returns the slot (0..2) the name says, or -1.
S32 iSaveGCIGameIndex(const iSaveGCI* gci);

// The payload out of a TSSM card file, with the head's strings and the payload
// checked. `payload` gets exactly the header's size (the block padding is
// dropped).
bool iSaveGCIPayload(const iSaveGCI* gci, std::vector<U8>& payload, iSavePayloadInfo* info,
                     char* err, size_t errsize);

struct iSaveGCIBuild
{
    char gamecode[5]; // "GGVE"; a P region ("GGVP") also switches the date to D.M.Y
    S32 game; // 0..2: SpongeBobMovie01..03
    U32 time; // the entry's time, seconds since 2000 (iSaveGCTimeFromUnix)
    char comment[ISF_COMMENT_SIZE]; // the comment's second line: the console writes the save's date

    // Banner (ISF_BANNER_SIZE) and icon (ISF_ICON_SIZE) pixels, in the card's
    // tiled RGB5A3. NULL draws the port's own (iSaveDefaultBanner/Icon).
    const U8* banner;
    const U8* icon;

    // An original entry and head to reproduce byte for byte instead -- for a
    // payload that has not changed since it came off a card. Both or neither.
    const U8* verbatimDentry;
    const U8* verbatimHead;

    U16 startBlock; // as the card had it; 5, the first data block, otherwise
    U8 copyTimes;
};

void iSaveGCIBuildDefaults(iSaveGCIBuild* b);

// A .gci for `payload`, laid out as gc/isavegame.cpp's iSGSaveFile and the SDK's
// CARDCreate/CARDSetStatus would have put it on a card.
bool iSaveGCIMake(const U8* payload, size_t n, const iSaveGCIBuild* b, std::vector<U8>& out);

// Dolphin's name for an exported file: "78-GGVE-SpongeBobMovie01.gci".
void iSaveGCIDolphinName(const char* gamecode, S32 game, char* out, size_t outsize);

// The 0x4000-byte head: title, comment, banner, icon, footer.
void iSaveComposeHead(U8* head, const char* comment, const U8* banner, const U8* icon);

// The port's own banner and icon, drawn (not taken from the disc, which the
// port does not have): a sponge-yellow icon and a title banner.
void iSaveDefaultBanner(U8* out); // ISF_BANNER_SIZE
void iSaveDefaultIcon(U8* out); // ISF_ICON_SIZE

// The first image of a TPL (the console's texture palette: SBGCBanner.tpl and
// SBGCIcon.tpl on the disc), as gc/isavegame.cpp's iSG_tpl_unpack reads it.
bool iSaveTPLImage(const U8* tpl, size_t n, const U8** data, size_t* size, U16* width, U16* height,
                   U32* format);

// --- raw memory card images -------------------------------------------------------

// Every file on a raw card image (Dolphin's .raw, GCMM's .gcp: the card's
// bytes, nothing around them), each as a .gci (entry + blocks). The directory
// and block map copies are checked and the newer good one used.
bool iSaveCardFiles(const U8* img, size_t n, std::vector<std::vector<U8> >& gcis, char* err,
                    size_t errsize);

// A freshly formatted card image holding `gcis`, the way CARDFormat and
// CARDCreate lay one out. For tests: its ID block's serial is not tied to any
// console, so real hardware would not mount it.
bool iSaveCardCreate(S32 sizeMbits, const std::vector<std::vector<U8> >& gcis, std::vector<U8>& img,
                     char* err, size_t errsize);

// --- clocks ------------------------------------------------------------------------

// The console counts seconds since 2000-01-01 on its local wall clock.
U32 iSaveGCTimeFromUnix(S64 unixSeconds);
S64 iSaveUnixFromGCTime(U32 gcSeconds);

// iSGMakeTimeStamp's format: "MM/DD/YYYY HH:MM:SS", or "DD.MM.YYYY HH:MM:SS"
// for PAL.
void iSaveFormatStamp(U32 gcSeconds, bool pal, char* out, size_t outsize);

// --- small helpers ------------------------------------------------------------

U16 iSaveGetU16(const U8* p);
U32 iSaveGetU32(const U8* p);
void iSavePutU16(U8* p, U16 v);
void iSavePutU32(U8* p, U32 v);

// A tag as four printable characters ("GDAT"), for messages.
const char* iSaveTagName(U32 tag, char* out5);

#endif
