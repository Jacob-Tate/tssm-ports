#ifndef ISAVEXBOX_H
#define ISAVEXBOX_H

// PC-only: the Xbox release's save files, as data. Nothing here touches the
// disk; iSaveStore.h is the store built on it and docs/SAVES.md section 7
// describes the layout.
//
// On the Xbox's hard disk a save is a folder:
//
//   UDATA\54510027\                     the game's own folder (its title ID)
//       TitleMeta.xbx                   "TitleName=The SpongeBob SquarePants Movie"
//       TitleImage.xbx, SaveImage.xbx   the dashboard's pictures, out of the XBE
//       7B317197822D\                   one save: 12 hex digits worked out from
//           SaveMeta.xbx                its name, "Name=SpongeBobMovie01"
//           GameData.xsv                the payload (iSaveLE.h's little-endian
//                                       form) and a 20-byte signature
//
// THE SIGNATURE is HMAC-SHA1 over the payload, keyed with the title's
// signature key. A console works that key out when the game starts:
// HMAC-SHA1(the console's certificate key, the 16-byte signature-key field of
// the XBE's certificate), first 16 bytes. The certificate key is the same in
// every retail console and is in none of the game's files. So there are two
// signatures this can write:
//
//   emulator   the certificate key is 16 zero bytes, which is what
//              Cxbx-Reloaded uses when its data folder has no keys.bin. Needs
//              only the player's own DEFAULT.XBE. Cxbx-Reloaded and other
//              copies of this port take it; a real Xbox does not.
//   real       the certificate key comes from a keys.bin the player made from
//              their own Xbox (32 bytes: the EEPROM key, then the certificate
//              key -- the file Cxbx-Reloaded reads). A real Xbox and xemu take
//              it.
//
// No key is in this code, none is ever asked for as text, and no function here
// prints one. Reading a save never needs one: an import takes any signature.
//
// Only <types.h>, the C library and std::vector/std::string.

#include <types.h>

#include <stddef.h>
#include <string>
#include <vector>

#define ISX_TITLE_ID 0x54510027u
#define ISX_KEY_SIZE 16
#define ISX_SIGNATURE_SIZE 20
#define ISX_KEYS_FILE_SIZE 32 // keys.bin: the EEPROM key, then the certificate key
#define ISX_SAVE_FILE "GameData.xsv"
#define ISX_SAVE_META "SaveMeta.xbx"
#define ISX_TITLE_META "TitleMeta.xbx"
#define ISX_TITLE_IMAGE "TitleImage.xbx"
#define ISX_SAVE_IMAGE "SaveImage.xbx"

// --- names -----------------------------------------------------------------------------

// The game's name for save `game` (0..2): "SpongeBobMovie01". This is what the
// dashboard shows and what the folder's name is worked out from.
void iSaveXboxSaveName(S32 game, char* out, size_t outsize);

// The slot (0..2) a save name is, or -1.
S32 iSaveXboxGameOfName(const char* name);

// XCreateSaveGame's folder for a save name: its UTF-16 units folded as
// H = (H * 65536 + c) mod (2^48 - 59), as 12 upper-case hex digits.
// "SpongeBobMovie01" gives "7B317197822D". `out` holds 13 characters.
void iSaveXboxFolderName(const char* saveName, char* out);

// A .xbx text file: a byte-order mark, "<key>=<value>" and CR LF, in UTF-16LE.
// SaveMeta.xbx is ("Name", the save's name); TitleMeta.xbx ("TitleName", the
// game's).
void iSaveXboxMeta(const char* key, const char* value, std::vector<U8>& out);

// The value of `key` in a .xbx file, as ASCII (other characters become '?').
// FALSE when the key is not there.
bool iSaveXboxMetaValue(const U8* file, size_t n, const char* key, char* out, size_t outsize);

// --- the player's XBE ------------------------------------------------------------------

// What an export needs out of the Xbox program. The certificate's
// signature-key field is kept in here, in memory, for iSaveXboxTitleKey and is
// never shown.
struct iSaveXboxTitle
{
    U32 titleId;
    char titleName[48]; // ASCII
    std::vector<U8> titleImage; // the $$XTIMAGE section: TitleImage.xbx
    std::vector<U8> saveImage; // the $$XSIMAGE section: SaveImage.xbx
    U8 certificateField[ISX_KEY_SIZE];
};

// Reads an XBE (DEFAULT.XBE) held in memory. FALSE with `err` when it is not
// an XBE, is cut short, or is another game's.
bool iSaveXbeParse(const U8* file, size_t n, iSaveXboxTitle* out, char* err, size_t errsize);

// The certificate key out of a keys.bin held in memory. FALSE with `err` when
// the file is not 32 bytes or its certificate key is all zeros (which would
// only repeat the emulator signature).
bool iSaveXboxKeysParse(const U8* file, size_t n, U8 certificateKey[ISX_KEY_SIZE], char* err,
                        size_t errsize);

// --- the signature ----------------------------------------------------------------------

// The title's signature key. `certificateKey` NULL is the emulator's: 16 zero
// bytes.
void iSaveXboxTitleKey(const iSaveXboxTitle* title, const U8* certificateKey,
                       U8 out[ISX_KEY_SIZE]);

// The 20 bytes that follow the payload in GameData.xsv.
void iSaveXboxSign(const U8 titleKey[ISX_KEY_SIZE], const U8* payload, size_t n,
                   U8 out[ISX_SIGNATURE_SIZE]);

// GameData.xsv for a little-endian payload: the payload, then its signature.
void iSaveXboxFile(const U8 titleKey[ISX_KEY_SIZE], const U8* payload, size_t n,
                   std::vector<U8>& out);

// Whether a GameData.xsv's last 20 bytes are `titleKey`'s signature of the
// rest.
bool iSaveXboxVerify(const U8 titleKey[ISX_KEY_SIZE], const U8* file, size_t n);

#endif
