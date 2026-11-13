// Where the game starts, and whether the logos play. What the settings are for
// is in iBoot.h.

#include "iBoot.h"

#include <stdio.h>
#include <string.h>

namespace
{
    // A scene id is four characters and sceneStart is 32, so this is the whole
    // of it. "soak" is four characters too and needs no room of its own.
    char sScene[8];
    S32 sIntroMovies = TRUE;
    F32 sCameraSensitivity = 1.0f;
}

const char* iBootScene()
{
    return sScene;
}

S32 iBootIntroMovies()
{
    return sIntroMovies;
}

void iBootSetScene(const char* scene)
{
    sScene[0] = '\0';

    if (scene == NULL || scene[0] == '\0')
    {
        return;
    }

    // Checked here so that the complaint names the setting: a well-formed but
    // unknown id reaches the missing-file screen (iFileMissingPackage), which
    // blames the game files for what may be a typo in the config.
    if (strlen(scene) != 4)
    {
        printf("tssm: config: game.boot is not a four-character scene id, starting at the "
               "menu: %s\n",
               scene);
        return;
    }

    strcpy(sScene, scene);
}

void iBootSetIntroMovies(S32 play)
{
    sIntroMovies = play;
}

F32 iBootCameraSensitivity()
{
    return sCameraSensitivity;
}

void iBootSetCameraSensitivity(F32 scale)
{
    // Zero is a camera that cannot be turned at all, which reads as a broken
    // controller rather than as a setting.
    if (scale <= 0.0f || scale > 10.0f)
    {
        printf("tssm: config: input.camera_sensitivity is out of range, staying at %g: %g\n",
               (double)sCameraSensitivity, (double)scale);
        return;
    }

    sCameraSensitivity = scale;
}
