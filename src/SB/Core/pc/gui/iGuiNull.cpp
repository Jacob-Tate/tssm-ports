// iGui.h with TSSM_GUI=OFF: no launcher, no overlay, the game's input untouched.

#include "gui/iGui.h"

#include "iConfig.h"

void iGuiLauncherRun()
{
}

void iGuiInit()
{
}

void iGuiShutdown()
{
}

bool iGuiIsOpen()
{
    return false;
}

void iGuiSetOpen(bool)
{
}

const iPadHostState* iGuiFilterPad(S32, const iPadHostState* host)
{
    return host;
}

void iGuiSetPanel(iGuiPanel, iGuiPanelFn, void*)
{
}

// No sliders to move them, but config.ini's volumes count all the same.
void iGuiSetAudioListener(iGuiAudioFn fn)
{
    if (fn != NULL)
    {
        fn(iConfigGetInt("audio.master_volume", 100) / 100.0f,
           iConfigGetInt("audio.music_volume", 100) / 100.0f,
           iConfigGetInt("audio.sfx_volume", 100) / 100.0f,
           iConfigGetInt("audio.movie_volume", 100) / 100.0f);
    }
}
