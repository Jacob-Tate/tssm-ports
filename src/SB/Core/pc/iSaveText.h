#ifndef ISAVETEXT_H
#define ISAVETEXT_H

#include <types.h>

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

#endif
