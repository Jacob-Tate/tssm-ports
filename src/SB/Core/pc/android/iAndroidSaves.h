// iAndroidSaves.h: the co-op app plays on the saves of the normal app
// (io.github.tssmport.game), through its SaveShareProvider. The Java half is
// CoopSaves.java; docs/ANDROID.md, "The co-op app".
//
// The co-op app copies the real save folder into its own (the save store's
// root) once it holds the real folder's one-copy lock, runs the unchanged save
// code on that copy, and sends each change back, which the normal app writes
// the way the game writes a save.

#ifndef IANDROIDSAVES_H
#define IANDROIDSAVES_H

// iSystem.cpp's ClaimSaveFolder, before the save store's own claim and before
// any save is read. With the default save folder (no mod, no test launch, not
// "Play without saving"): takes the real folder's lock and copies the saves
// in. While the normal game holds that lock, it says so and the program ends;
// when the saves cannot be reached, the player chooses between playing
// without saving and closing. Nothing is changed either way.
void iAndroidSavesStart();

// iSaveStoreChange's start: before the save store changes the folder, the
// copy is marked as not yet sent. Nested changes mark once.
void iAndroidSavesChanging();

// iSaveStoreChange's end (a save, a restore, a delete, an import, the
// start-up repair): when the outermost change ends, the changes go to the real
// folder. Nothing when the saves are not shared.
void iAndroidSavesChanged();

// iConfigSave, after config.ini was written: it goes to the normal app with
// the saves, now, or when the change around it ends.
void iAndroidSettingsChanged();

#endif
