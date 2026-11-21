#ifndef IEXTRASGC_H
#define IEXTRASGC_H

#include <types.h>

// PC only, compiled with the game units: the Xbox extras in a level whose package is GameCube
// files (the GameCube disc, or a mod's GameCube level among the Xbox files; iExtras.h decides
// whose files a level is).
//
// Two of BB02's and B401's extras are in their GameCube files but switched off or left unused
// there, by data that differs from the Xbox files' (iExtras.h has the tables):
//
//   - spotlights: BB02's SPOTLIGHT_DISP_GC_ONLY switches SPOTLIGHT_01 and _02 off at every
//     SceneBegin, and B401's intro and restart switch SPOTLIGHT_GROUP off where the Xbox files
//     switch it on and run SPOTLIGHT_SCRIPT. With spotlights = xbox, those objects get the Xbox
//     files' links instead, so the lights behave as on the Xbox.
//   - crowds: BB02's crowd objects have the rigid models crowd01-crowd03 and no animation list,
//     though the files ship the skinned models and lists of the Xbox files' crowd. With crowds =
//     xbox, each gets the skinned model and its list, so the game's own code animates it as on
//     the Xbox. The Xbox files' 7 other crowd members are not in the GameCube files, and how far
//     the crowd is drawn stays the files' (75, the Xbox files' 150).
//
// With gamecube, or the Xbox files, nothing is changed. Every asset and object is checked
// before it is changed: a GameCube file that is not what the disc has there (a mod's) is left as
// it is, and the feature falls back to the GameCube version for the level with one sentence
// (iExtrasFallBack), in the level's line and the overlay.
//
// BB01's GameCube files have no skinned crowd and TT01's no disco lights: iExtrasResolve gives
// those the GameCube version and says why.

// zSceneInit, with the level's package read and before its pipe tables are read and its objects
// are made, after iExtrasSceneEnter: points the crowd objects' assets at the skinned crowd, and
// checks the assets of the objects whose links change.
void iExtrasGCScenePrepare(U32 sceneID);

// zSceneInit, once the objects exist (before zSceneSetup's resets and SceneBegin): gives those
// objects the Xbox files' links, from memory of the scene's own (a dispatcher re-reads its links
// from its asset at every reset, so it is given a copy of its asset with them).
void iExtrasGCSceneObjects(U32 sceneID);

#endif
