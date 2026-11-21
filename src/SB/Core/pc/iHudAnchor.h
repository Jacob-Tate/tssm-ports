#ifndef IHUDANCHOR_H
#define IHUDANCHOR_H

#include <types.h>

// PC-only: the HUD at the real edges of a screen that is not 4:3 (config.ini's
// [video] ui = native on a wide screen, and always on a screen taller than 4:3;
// iScreen.h has the setting and the arithmetic).
//
// The game's HUD is sixteen xhud widgets (mnui.HIP), in groups, each drawn for
// a corner of a 4:3 television:
//
//   top left      the health (Krabby Patties)
//   top right     the manliness meter: its bar and notch, the points, the
//                 upgrade icon and text, the combo counter and its words
//   bottom left   the treasure chest and its count
//   bottom right  the Goofy Goober Token and its count; in the Patty Wagon, the
//                 nitro and its count
//
// Two more pieces of HUD are not xhud widgets and are placed the same way:
//
//   bottom right   the Sonic Wave Guitar's countdown while the wave flies
//   bottom centre  the bosses' health meter: it stays centred, so it only moves
//                  on a screen taller than 4:3, down to the bottom edge, level
//                  with the chest and the token as on a 4:3 screen
//
// A group goes to its corner of the real screen as one piece, by the distance
// the screen reaches past the 4:3 box, so every widget keeps the margin and the
// neighbours it was drawn with, and a widget sliding in or out keeps its
// motion. On a screen taller than 4:3 each group is also cut at the top and
// bottom of the 4:3 screen it is moved with (iScreen2DClipY), so a piece that
// hides goes past the edge as on a 4:3 screen and does not wait in the margin.
// Which group a widget belongs to is a table of their names
// (iHudAnchor.cpp): the rectangles alone do not say (the upgrade icon sits
// nearer the middle of the screen than the right). A widget the table does not
// know (a mod's) goes by where its middle was drawn: the left or the right 40%
// of the box to that edge, the middle fifth stays.
//
// What stays in the box, whatever the setting:
//   - everything else: the menus and the pause screen (zUI), talk boxes and
//     prompts, subtitles, the guitar's sights (they sit on what they aim at);
//   - the HUD itself while the game is paused: the pause screen is drawn around
//     it (the chest and the token either side of the chest count's box). It
//     glides between the two places in a fifth of a second.
//
// Nothing here is game state: it changes where a widget is drawn and nothing
// the game reads, so each player of a co-op session keeps their own setting.

namespace xhud
{
    struct widget;
}

struct RwMatrixTag;

// Around one xhud widget's draw (xhud::widget::render_all).
void iHudAnchorBegin(const xhud::widget& w);

// Around a piece of HUD that is not an xhud widget, drawn at (x, y) of the 4:3
// screen (0 to 1 across and down).
void iHudAnchorBegin(F32 x, F32 y);

void iHudAnchorEnd();

struct iHudAnchorScope
{
    iHudAnchorScope(const xhud::widget& w)
    {
        iHudAnchorBegin(w);
    }
    iHudAnchorScope(F32 x, F32 y)
    {
        iHudAnchorBegin(x, y);
    }
    ~iHudAnchorScope()
    {
        iHudAnchorEnd();
    }
};

// For the bosses' health meter (zNMEHealthMeter.cpp), drawn in the HUD camera
// as RwIm3D: the world matrix that carries it down to the bottom edge, or NULL
// (the console's) when it stays where it is, which is always except on a screen
// taller than 4:3.
RwMatrixTag* iHudAnchorBossMeter();

// For unattended runs (TSSM_TEST_HUDLOG=<n>, iTestEvents.h): the widgets drawn
// in the next frame are logged under that frame number, each with its name, the
// rectangle it was authored at and is at now (640x480 units), its edges and
// where its top left corner lands on the screen in pixels; so are the guitar's
// countdown ("hud point") and the boss meter's offset ("hud boss meter"). 0
// stops it.
void iHudAnchorLog(S32 frame);

#endif
