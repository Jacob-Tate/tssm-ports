package io.github.tssmport.game;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.Network;
import android.net.NetworkCapabilities;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.util.Log;

/**
 * Co-op on the home network: the two Wi-Fi locks and the kind of network, for
 * the native side (src/SB/Core/pc/android/iAndroidNet.cpp, through
 * GameActivity). docs/ANDROID.md, "The co-op app".
 *
 * <p>The multicast lock while hosting visibly: without it many phones' Wi-Fi
 * drops the broadcast questions of the devices looking for games. The
 * low-latency lock during a session, while hosting and while the list
 * searches: a phone saving power otherwise holds incoming packets back for
 * 100 ms or more, and while waiting for a player it lost most of what a PC
 * sent it. Both only while the game is in front (Android also drops the
 * low-latency lock with the screen off), so a phone in a pocket is not kept
 * awake, and neither in single player.
 */
final class CoopNet {

    private static final String TAG = "tssm";

    private static boolean sWantMulticast;
    private static boolean sWantLowLatency;
    private static boolean sResumed;
    private static WifiManager.MulticastLock sMulticast;
    private static WifiManager.WifiLock sLowLatency;

    private CoopNet() {
    }

    /** What co-op needs now (the game's thread). */
    static synchronized void want(Context context, boolean multicast, boolean lowLatency) {
        sWantMulticast = multicast;
        sWantLowLatency = lowLatency;
        apply(context);
    }

    /** The game in front or not (the UI thread). */
    static synchronized void resumed(Context context, boolean resumed) {
        sResumed = resumed;
        apply(context);
    }

    private static void apply(Context context) {
        WifiManager wifi = (WifiManager) context.getApplicationContext()
                .getSystemService(Context.WIFI_SERVICE);
        if (wifi == null) {
            return;
        }
        try {
            boolean multicast = sWantMulticast && sResumed;
            if (multicast && sMulticast == null) {
                sMulticast = wifi.createMulticastLock("tssm-coop");
                sMulticast.setReferenceCounted(false);
                sMulticast.acquire();
                Log.i(TAG, "coop: multicast lock taken");
            } else if (!multicast && sMulticast != null) {
                sMulticast.release();
                sMulticast = null;
                Log.i(TAG, "coop: multicast lock released");
            }

            boolean low = sWantLowLatency && sResumed;
            if (low && sLowLatency == null) {
                int mode = Build.VERSION.SDK_INT >= 29 ? WifiManager.WIFI_MODE_FULL_LOW_LATENCY
                        : WifiManager.WIFI_MODE_FULL_HIGH_PERF;
                sLowLatency = wifi.createWifiLock(mode, "tssm-coop");
                sLowLatency.setReferenceCounted(false);
                sLowLatency.acquire();
                Log.i(TAG, "coop: low-latency Wi-Fi lock taken");
            } else if (!low && sLowLatency != null) {
                sLowLatency.release();
                sLowLatency = null;
                Log.i(TAG, "coop: low-latency Wi-Fi lock released");
            }
        } catch (RuntimeException e) {
            // A refused lock costs latency or broadcasts, never the game.
            Log.w(TAG, "coop: a Wi-Fi lock was refused", e);
        }
    }

    /** As iAndroidNetKind: 0 none, 1 Wi-Fi, 2 Ethernet, 3 mobile data, 4 other. */
    static int kind(Context context) {
        ConnectivityManager cm = (ConnectivityManager) context.getApplicationContext()
                .getSystemService(Context.CONNECTIVITY_SERVICE);
        if (cm == null) {
            return 4;
        }
        try {
            Network active = cm.getActiveNetwork();
            NetworkCapabilities c = active != null ? cm.getNetworkCapabilities(active) : null;
            if (c == null) {
                return 0;
            }
            if (c.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) {
                return 1;
            }
            if (c.hasTransport(NetworkCapabilities.TRANSPORT_ETHERNET)) {
                return 2;
            }
            if (c.hasTransport(NetworkCapabilities.TRANSPORT_CELLULAR)) {
                return 3;
            }
            return 4;
        } catch (RuntimeException e) {
            return 4;
        }
    }
}
