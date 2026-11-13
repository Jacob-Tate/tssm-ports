// iAndroidNet.cpp: see iAndroidNet.h. The Java half is CoopNet.java, reached
// through GameActivity's static coopLan and coopNetKind.

#include "iAndroidNet.h"

#include "iAndroid.h"
#include "iAndroidJava.h"
#include "iCoop.h"

#include <stdio.h>

namespace
{
    bool sSent;
    bool sMulticast;
    bool sLowLatency;
    bool sFarewellSet;

    U64 sKindAt;
    iAndroidNetKind sKind = IANDROID_NET_NONE;

    // A fatal box's end skips atexit, so iCoopShutdown's goodbye: the stop goes
    // through the I/O thread, which says goodbye before it reports the role off.
    void Farewell()
    {
        iCoopStatus st;
        iCoopGetStatus(&st);
        if (st.role == ICOOP_ROLE_OFF)
        {
            return;
        }
        iCoopStop();
        const U64 until = SDL_GetTicks() + 500;
        do
        {
            SDL_Delay(5);
            iCoopGetStatus(&st);
        } while (st.role != ICOOP_ROLE_OFF && SDL_GetTicks() < until);
        printf("tssm: coop: android: %s\n", st.role == ICOOP_ROLE_OFF ?
                                                "goodbye sent before the end" :
                                                "no goodbye within 0.5 s; the game ends anyway");
        fflush(stdout);
    }
} // namespace

void iAndroidNetFrame(const iCoopStatus& st)
{
    if (!sFarewellSet)
    {
        sFarewellSet = true;
        iAndroidAtEndProcess(Farewell);
    }

    // A host's responder receives broadcasts; a joiner's answers come unicast.
    const bool multicast = st.role == ICOOP_ROLE_HOST && st.lanAnswering;
    // The radio kept listening whenever another device has to reach this one:
    // in power save it held back nearly everything sent to a phone waiting
    // for a player, or searching, until a session started. Hosting not
    // visibly too: a join by address.
    const bool lowLatency = st.connected || st.phase == ICOOP_PHASE_RECONNECTING ||
                            st.role == ICOOP_ROLE_HOST || st.finding;
    if (sSent && multicast == sMulticast && lowLatency == sLowLatency)
    {
        return;
    }

    iAndroidJava java;
    if (!java.Begin("coopLan", "(ZZ)V"))
    {
        sSent = true; // asked once; a missing method is logged, not retried each frame
        sMulticast = multicast;
        sLowLatency = lowLatency;
        return;
    }
    java.env->CallStaticVoidMethod(java.cls, java.method, (jboolean)multicast,
                                   (jboolean)lowLatency);
    java.Clear();
    sSent = true;
    sMulticast = multicast;
    sLowLatency = lowLatency;
    printf("tssm: coop: android: Wi-Fi locks wanted: multicast %s, low latency %s\n",
           multicast ? "on" : "off", lowLatency ? "on" : "off");
    fflush(stdout);
}

iAndroidNetKind iAndroidNetGetKind()
{
    const U64 now = SDL_GetTicks();
    if (sKindAt != 0 && now - sKindAt < 2000)
    {
        return sKind;
    }
    sKindAt = now;

    iAndroidJava java;
    if (!java.Begin("coopNetKind", "()I"))
    {
        sKind = IANDROID_NET_OTHER; // unknown: no note in the tab
        return sKind;
    }
    const jint k = java.env->CallStaticIntMethod(java.cls, java.method);
    sKind = java.Clear() || k < IANDROID_NET_NONE || k > IANDROID_NET_OTHER ? IANDROID_NET_OTHER :
                                                                              (iAndroidNetKind)k;
    return sKind;
}
