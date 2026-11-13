// iAndroidNet.h: what co-op on the home network needs from Android, through
// GameActivity.java and CoopNet.java. docs/ANDROID.md, "The co-op app".

#ifndef IANDROIDNET_H
#define IANDROIDNET_H

struct iCoopStatus;

// Every frame, on the game's thread (gui/iGuiCoop.cpp, iGui::CoopFrame): asks
// Java for the Wi-Fi locks co-op needs now, when that changes. The multicast
// lock while hosting visibly (many phones' Wi-Fi drops broadcasts without it);
// the low-latency lock while hosting, searching or in a session (a phone
// saving power holds back what is sent to it). Java holds them only while the
// game is in front. The first call also sets the goodbye a fatal box's end
// sends a partner (iAndroidAtEndProcess), within half a second.
void iAndroidNetFrame(const iCoopStatus& st);

enum iAndroidNetKind
{
    IANDROID_NET_NONE,
    IANDROID_NET_WIFI,
    IANDROID_NET_ETHERNET,
    IANDROID_NET_MOBILE,
    IANDROID_NET_OTHER,
};

// The network Android sends through now, for the Co-op tab. A local query,
// at most every two seconds (cached).
iAndroidNetKind iAndroidNetGetKind();

#endif
