// TSSM_TEST_SEND: an event to a scene object by name at a given frame, for
// unattended runs (iTestSend.h). Port-only; compiled with the game units
// because it calls game code. Nothing happens unless the variable is set.

#include "iTestSend.h"

#include "xEvent.h"
#include "xString.h"
#include "zGlobals.h"
#include "zScene.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace
{
    struct Send
    {
        S32 frame;
        char name[64];
        U32 event;
    };

    const S32 kMaxSends = 8;

    Send sSends[kMaxSends];
    S32 sSendCount;
    bool sParsed;

    void Parse(const char* list)
    {
        char buf[512];
        snprintf(buf, sizeof(buf), "%s", list);

        for (char* tok = strtok(buf, ","); tok != NULL; tok = strtok(NULL, ","))
        {
            if (sSendCount == kMaxSends)
            {
                printf("tssm: test: TSSM_TEST_SEND has more than %d entries; the rest are "
                       "ignored\n",
                       (int)kMaxSends);
                break;
            }
            Send& s = sSends[sSendCount];
            unsigned event = eEventRun;
            int frame = 0;
            if (sscanf(tok, "%d:%63[^:]:%u", &frame, s.name, &event) >= 2 && frame > 1)
            {
                s.frame = frame;
                s.event = event;
                sSendCount++;
            }
            else
            {
                printf("tssm: test: TSSM_TEST_SEND: '%s' is not frame:name[:event]; ignored\n",
                       tok);
            }
        }
        fflush(stdout);
    }
} // namespace

void iTestSendFrame(S32 frame)
{
    if (!sParsed)
    {
        sParsed = true;
        const char* list = getenv("TSSM_TEST_SEND");
        if (list != NULL && list[0] != '\0')
        {
            Parse(list);
        }
    }

    for (S32 i = 0; i < sSendCount; i++)
    {
        if (sSends[i].frame != frame)
        {
            continue;
        }
        xBase* b = globals.sceneCur != NULL ? zSceneFindObject(xStrHash(sSends[i].name)) : NULL;
        printf("tssm: test: frame %d: event %u to %s%s (TSSM_TEST_SEND)\n", (int)frame,
               (unsigned)sSends[i].event, sSends[i].name,
               b != NULL ? "" : ": no such object in this scene");
        fflush(stdout);
        if (b != NULL)
        {
            zEntEvent(b, sSends[i].event);
        }
    }
}
