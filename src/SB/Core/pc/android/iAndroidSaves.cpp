// iAndroidSaves.cpp: see iAndroidSaves.h.

#include "iAndroidSaves.h"

#include "iAndroidJava.h"
#include "iHost.h"
#include "iSaveStore.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

namespace
{
    bool sShared;
    thread_local int sDepth; // iSaveStoreChange nesting on this thread

    void Fail(const char* message)
    {
        printf("tssm: coop saves: %s; closing\n", message);
        fflush(stdout);
        iHostErrorBox("TSSM co-op", message);
        exit(0);
    }

    // `message`, then Play without saving or Close. Playing without saving
    // points the save store at an empty folder of this process's
    // (GameActivity.coopNoSavesFolder) and returns; Close ends the program.
    void PlayWithoutSavingOrClose(const char* message)
    {
        printf("tssm: coop saves: %s\n", message);
        fflush(stdout);
        char text[800];
        snprintf(text, sizeof(text),
                 "%s\n\nYou can play co-op without saving: nothing is kept when it closes.",
                 message);
        const SDL_MessageBoxButtonData buttons[] = {
            { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Close" },
            { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Play without saving" },
        };
        SDL_MessageBoxData box;
        memset(&box, 0, sizeof(box));
        box.flags = SDL_MESSAGEBOX_WARNING;
        box.title = "TSSM co-op";
        box.message = text;
        box.numbuttons = 2;
        box.buttons = buttons;
        int chosen = 0;
        if (SDL_ShowMessageBox(&box, &chosen) && chosen == 1)
        {
            iAndroidJava java;
            if (java.Begin("coopNoSavesFolder", "()Ljava/lang/String;"))
            {
                jstring dir = (jstring)java.env->CallStaticObjectMethod(java.cls, java.method);
                if (!java.Clear() && dir != NULL)
                {
                    const char* utf = java.env->GetStringUTFChars(dir, NULL);
                    if (utf != NULL)
                    {
                        iSaveStoreSetRoot(utf);
                        java.env->ReleaseStringUTFChars(dir, utf);
                        java.env->DeleteLocalRef(dir);
                        printf("tssm: coop saves: playing without saving (the save folder is %s)\n",
                               iSaveStoreRoot());
                        fflush(stdout);
                        return;
                    }
                    java.env->DeleteLocalRef(dir);
                }
            }
            printf("tssm: coop saves: no folder to play without saving in\n");
        }
        printf("tssm: coop saves: closing\n");
        fflush(stdout);
        exit(0);
    }

    void Push()
    {
        iAndroidJava java;
        if (java.Begin("coopSavesPush", "()V"))
        {
            java.env->CallStaticVoidMethod(java.cls, java.method);
            java.Clear();
        }
    }
} // namespace

void iAndroidSavesStart()
{
    const char* internal = getenv("TSSM_ANDROID_INTERNAL");
    char ownRoot[600];
    snprintf(ownRoot, sizeof(ownRoot), "%s/tssm/saves", internal != NULL ? internal : "");
    if (internal == NULL || strcmp(iSaveStoreRoot(), ownRoot) != 0)
    {
        printf("tssm: coop saves: not shared (the save folder is %s)\n", iSaveStoreRoot());
        fflush(stdout);
        return;
    }

    iAndroidJava claim;
    if (!claim.Begin("coopSavesClaim", "()I"))
    {
        PlayWithoutSavingOrClose("Co-op could not reach the saves of SpongeBob Movie Port.");
        return;
    }
    const jint fd = claim.env->CallStaticIntMethod(claim.cls, claim.method);
    if (claim.Clear() || fd < 0)
    {
        PlayWithoutSavingOrClose("Co-op could not reach the saves of SpongeBob Movie Port. If "
                                 "this keeps happening, open SpongeBob Movie Port once, close it, "
                                 "and try again.");
        return;
    }

    // The lock the normal game's iSaveStoreClaimFolder takes on that folder,
    // held for as long as this process lives: never closed once the saves
    // are shared.
    if (flock(fd, LOCK_EX | LOCK_NB) != 0)
    {
        const int err = errno;
        close(fd);
        if (err == EWOULDBLOCK)
        {
            Fail("SpongeBob Movie Port is open, and co-op plays on its saves.\n\nClose it (swipe "
                 "it away in Recent apps), then open TSSM co-op again.");
        }
        printf("tssm: coop saves: the lock was refused (errno %d)\n", err);
        PlayWithoutSavingOrClose("Co-op cannot share the saves of SpongeBob Movie Port on this "
                                 "phone (Android refused the lock). Your saves are unchanged.");
        return;
    }

    // This process's id in the lock file, as iHostClaimInstance writes it,
    // for the normal game's log while co-op holds the lock.
    char pid[32];
    const int n = snprintf(pid, sizeof(pid), "%d\n", (int)getpid());
    if (ftruncate(fd, 0) == 0 && n > 0)
    {
        const ssize_t wrote = pwrite(fd, pid, (size_t)n, 0);
        (void)wrote;
    }

    iAndroidJava mirror;
    jstring why = NULL;
    bool copied = false;
    if (mirror.Begin("coopSavesMirror", "(Ljava/lang/String;)Ljava/lang/String;"))
    {
        jstring root = mirror.env->NewStringUTF(iSaveStoreRoot());
        why = (jstring)mirror.env->CallStaticObjectMethod(mirror.cls, mirror.method, root);
        mirror.env->DeleteLocalRef(root);
        copied = !mirror.Clear() && why == NULL;
    }
    if (!copied)
    {
        char text[400];
        const char* utf = why != NULL ? mirror.env->GetStringUTFChars(why, NULL) : NULL;
        snprintf(text, sizeof(text),
                 "Co-op could not copy your saves from SpongeBob Movie Port (%s). Your saves are "
                 "unchanged.",
                 utf != NULL ? utf : "Android refused it");
        if (utf != NULL)
        {
            mirror.env->ReleaseStringUTFChars(why, utf);
        }
        if (why != NULL)
        {
            mirror.env->DeleteLocalRef(why);
        }
        // Nothing is shared: the lock goes back at once.
        close(fd);
        PlayWithoutSavingOrClose(text);
        return;
    }

    sShared = true;
    printf("tssm: coop saves: playing on the saves of SpongeBob Movie Port (copied into %s)\n",
           iSaveStoreRoot());
    fflush(stdout);
}

void iAndroidSavesChanging()
{
    if (!sShared || sDepth++ > 0)
    {
        return;
    }
    iAndroidJava java;
    if (java.Begin("coopSavesBegin", "()V"))
    {
        java.env->CallStaticVoidMethod(java.cls, java.method);
        java.Clear();
    }
}

void iAndroidSavesChanged()
{
    if (!sShared || (sDepth > 0 && --sDepth > 0))
    {
        return;
    }
    Push();
}

void iAndroidSettingsChanged()
{
    if (sShared && sDepth == 0)
    {
        Push();
    }
}
