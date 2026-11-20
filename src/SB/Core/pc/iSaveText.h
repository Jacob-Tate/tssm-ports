#ifndef ISAVETEXT_H
#define ISAVETEXT_H

#include <types.h>

#include <stddef.h>

// PC-only: the load screen's "no saves" message, made true of a folder.
//
// The port runs the GameCube code, whose Load Game first asks which memory
// card slot to read: Memory Card Slot 1 is the save folder, Slot 2 its
// `second` folder (isavegame.cpp). The words come from the Xbox disc, and
// for a slot with no save on it the disc says "No game saves present on the
// Xbox Hard Disk" -- on an Xbox, the only place saves are kept. Picking Slot 2
// when every save is on Slot 1 therefore read as every save being gone. The
// load screen now goes straight to the one folder holding saves
// (iSGOnlyTargetWithGames), so the choice, and this message, come up when
// neither folder has a save; the message then names the slot it looked at.
//
// The message is the TEXT asset "LD NOGAMES TXT" (MN/mnus_US.hip), an
// xTextAsset: a U32 length, then the string, in an asset of 84 bytes. When
// iSGSelectGameDir finds a target empty it calls this (isavegame.h's
// iSGSetNoGamesHook), and the string is rewritten in place to name the slot:
//
//   No game saves on Memory Card Slot 2. Press {i:ui_accept} to continue.
//   No saves on Memory Card Slot 2. Your saves are on Slot 1. Press {i:ui_accept}
//
// the second when the other slot holds a save (77 characters; the asset holds
// 79 and a NUL). Only the US disc's message, or one of these, is rewritten, and
// only when the new one fits the asset; another language's is left as the disc
// has it.
void iSaveTextNoGames(S32 target, S32 otherWithSaves);

// ---------------------------------------------------------------------------
// The save screens' warning, as a setting: [game] save_text.
//
// The Xbox disc's save, load and New Game screens say "Please don't turn off
// your Xbox console"; the GameCube disc's say "Do not touch the Memory Card or
// the POWER Button" (and its formatting and deleting screens the same). Which
// console the words name is a choice:
//
//   auto      this platform's own words (the default): pc, android, switch
//             or 3ds, whichever build this is
//   xbox      the Xbox disc's line, word for word
//   gamecube  the GameCube disc's line, word for word
//   ps2       the PS2 release's memory card warning (iSaveTextTable.cpp says
//             where it comes from: there is no PS2 dump here yet)
//   generic   "Please don't turn off the power or quit the game."
//   pc, android, switch, 3ds   that platform's words, on any build
//
// Whichever disc's files are played, its own lines are the ones rewritten; on
// that disc, its own console's choice leaves them exactly as they are.
//
// **Only the lines themselves.** iSaveTextTable.cpp lists every such line of
// both US discs, exactly. A TEXT asset is rewritten only when its whole string
// is one of them, byte for byte, and then only its warning changes: the rest
// of the string, markup included, stays the disc's. Any other text -- another
// language, a mod's own words, the rest of the save screens -- is never
// touched. (The approach is treedome's text patch, src/SB/Core/pc/iTextPatch.h
// in the BfBB PC port: a readXForm on the TEXT type, with the copy the packer
// takes instead when the new words are longer than the asset.)
//
// **Live.** A line that is rewritten is moved into a copy this file owns for
// the life of the process, one per asset, with room for any choice's words. A
// change of the setting rewrites every such copy at once, so a line already on
// screen changes with it.

enum iSaveTextChoice
{
    ISAVETEXT_AUTO,
    ISAVETEXT_XBOX,
    ISAVETEXT_PS2,
    ISAVETEXT_GAMECUBE,
    ISAVETEXT_GENERIC,
    ISAVETEXT_PC,
    ISAVETEXT_ANDROID,
    ISAVETEXT_SWITCH,
    ISAVETEXT_3DS,
    ISAVETEXT_COUNT
};

// config.ini's word for a choice ("xbox"), and the Settings row's label.
const char* iSaveTextChoiceName(iSaveTextChoice choice);
const char* iSaveTextChoiceLabel(iSaveTextChoice choice);

// A config.ini word, in any case. Empty is auto; anything else that is not a
// choice is auto with *ok false.
iSaveTextChoice iSaveTextParse(const char* value, bool* ok);

// What auto means on this build, and a choice with auto resolved.
iSaveTextChoice iSaveTextPlatform();
iSaveTextChoice iSaveTextResolve(iSaveTextChoice choice);

// The choice's words as a sentence of their own, for the Settings row.
const char* iSaveTextSentence(iSaveTextChoice choice);

// Whether `text` is one of the discs' lines, exactly.
S32 iSaveTextIsLine(const char* text);

// `text` as `choice` words it, into `out`: TRUE when `text` is one of the
// discs' lines (on its own disc's choice, `out` is `text` unchanged), FALSE
// and `out` untouched past its first byte when it is not, or does not fit.
S32 iSaveTextRewrite(const char* text, iSaveTextChoice choice, char* out, size_t outSize);

// The table, for tests/savetexttest.cpp: line `index` as its disc has it, the
// asset(s) it is, and whose disc that is.
S32 iSaveTextLineCount();
S32 iSaveTextLine(S32 index, char* out, size_t outSize, const char** asset, iSaveTextChoice* disc);

// --- the game side (iSaveText.cpp) ---

// The packer's handler table, as zAssetStartup hands it to iAssetTypesInstall:
// the TEXT type gets the readXForm that rewrites the lines as they load.
struct PKRAssetType;
void iSaveTextInstall(PKRAssetType* handlers);

// Reads [game] save_text (a value that is not a choice is auto, said once in
// the log) and rewrites the lines already loaded. At startup and whenever the
// Settings row changes it.
void iSaveTextApplyConfig();

// The choice in force, as set (auto not resolved).
iSaveTextChoice iSaveTextCurrent();

#endif
