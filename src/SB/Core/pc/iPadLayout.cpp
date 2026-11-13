// The move layout. The table and the evidence for it are in iPadLayout.h.

#include "iPadLayout.h"

#include "iHost.h"
#include "iPadBind.h"
#include "xPad.h"

#include <stdio.h>

namespace
{
    enum Choice
    {
        CHOICE_AUTO,
        CHOICE_XBOX,
        CHOICE_GAMECUBE
    };

    Choice sChoice = CHOICE_AUTO;
    iPadLayoutKind sActive = IPAD_LAYOUT_XBOX;
} // namespace

void iPadLayoutSetChoice(const char* choice)
{
    if (choice == NULL || choice[0] == '\0' || iHostStrCaseCmp(choice, "auto") == 0)
    {
        sChoice = CHOICE_AUTO;
    }
    else if (iHostStrCaseCmp(choice, "xbox") == 0)
    {
        sChoice = CHOICE_XBOX;
    }
    else if (iHostStrCaseCmp(choice, "gamecube") == 0)
    {
        sChoice = CHOICE_GAMECUBE;
    }
    else
    {
        printf("tssm: input.layout is '%s', which is not auto, xbox or gamecube; using auto\n",
               choice);
        fflush(stdout);
        sChoice = CHOICE_AUTO;
    }

    iPadLayoutRefresh();
}

const char* iPadLayoutChoice()
{
    switch (sChoice)
    {
    case CHOICE_XBOX:
        return "xbox";
    case CHOICE_GAMECUBE:
        return "gamecube";
    default:
        return "auto";
    }
}

void iPadLayoutRefresh()
{
    switch (sChoice)
    {
    case CHOICE_XBOX:
        sActive = IPAD_LAYOUT_XBOX;
        break;
    case CHOICE_GAMECUBE:
        sActive = IPAD_LAYOUT_GAMECUBE;
        break;
    default:
        sActive = (iHostStrCaseCmp(iPadBindActivePreset(), "gamecube") == 0) ?
                      IPAD_LAYOUT_GAMECUBE :
                      IPAD_LAYOUT_XBOX;
        break;
    }
}

iPadLayoutKind iPadLayoutActive()
{
    return sActive;
}

const char* iPadLayoutName(iPadLayoutKind kind)
{
    return kind == IPAD_LAYOUT_GAMECUBE ? "gamecube" : "xbox";
}

U32 iPadLayoutButton(U32 mask)
{
    if (sActive != IPAD_LAYOUT_XBOX)
    {
        return mask;
    }

    const U32 pair = XPAD_BUTTON_TRIANGLE | XPAD_BUTTON_O;
    U32 out = mask & ~pair;

    if (mask & XPAD_BUTTON_TRIANGLE)
    {
        out |= XPAD_BUTTON_O;
    }
    if (mask & XPAD_BUTTON_O)
    {
        out |= XPAD_BUTTON_TRIANGLE;
    }

    return out;
}
