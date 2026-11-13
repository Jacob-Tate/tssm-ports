// iAndroidDocument.h: the documents the system's file picker hands the game
// (content:// URIs), saved and read through the app's own Java
// (GameActivity.java) instead of SDL's.
//
// SDL 3.4.14 opens a content:// URI through SDLActivity.openFileDescriptor,
// which catches only FileNotFoundException. Any other refusal (Android throws
// SecurityException for a URI the app was not granted) comes back to C as fd 0
// with the exception still pending: SDL writes the file to /dev/null (the
// game's fd 0), reports success, closes fd 0, and the next JNI call on the
// thread aborts the app. These functions clear every Java exception and say
// what it was. SDL's save dialog also drops the suggested file name and asks
// for "*/*" when Android does not know the extension (.gci).
//
// The Saves tab is the user (gui/iGuiUtil.cpp: PickSaveFile, ReadWholeFile,
// WriteWholeFile). docs/ANDROID.md, "Saves".

#ifndef IANDROIDDOCUMENT_H
#define IANDROIDDOCUMENT_H

#include <SDL3/SDL_dialog.h>

#include <stddef.h>

// TRUE for a path that is a content:// URI.
bool iAndroidIsDocument(const char* path);

// The system's "save as" picker (ACTION_CREATE_DOCUMENT), with `name` filled
// in and the type that goes with `extension` (the first of a filter's
// "zip;..." list; one Android does not know is sent as plain bytes, so the
// name typed keeps its extension). `callback` is answered as SDL's dialogs
// answer: the document's URI, or no file when cancelled. It runs on Android's
// UI thread. FALSE when the picker could not be shown; nothing is answered then.
bool iAndroidShowSaveDocument(SDL_DialogFileCallback callback, void* userdata, const char* name,
                              const char* extension);

// Replaces what the document holds with `n` bytes. FALSE, with the reason in
// `why`, when anything refused or fell short.
bool iAndroidWriteDocument(const char* uri, const void* data, size_t n, char* why, size_t whySize);

// The whole document, in memory from SDL_malloc (free it with SDL_free), its
// size in `*n`. NULL, with the reason in `why`, when it could not be read.
void* iAndroidReadDocument(const char* uri, size_t* n, char* why, size_t whySize);

#endif
