package io.github.tssmport.game;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.os.Build;

/**
 * PackageInstaller's answers about an update's install (Updater.commitAndWait
 * names this receiver in its PendingIntent). Not exported: only this app's
 * own PendingIntent reaches it. In the manifest only of builds with updates
 * (src/update/AndroidManifest.xml).
 */
public final class UpdateReceiver extends BroadcastReceiver {

    @Override
    @SuppressWarnings("deprecation")
    public void onReceive(Context context, Intent intent) {
        if (!Updater.ACTION_STATUS.equals(intent.getAction())) {
            return;
        }
        int session = intent.getIntExtra(PackageInstaller.EXTRA_SESSION_ID, -1);
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS,
                PackageInstaller.STATUS_FAILURE);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            // The typed form only from Android 14: on 13 it can miss an extra
            // the untyped form finds (AndroidX's IntentCompat waits for 34 too).
            Intent confirm = Build.VERSION.SDK_INT >= 34
                    ? intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent.class)
                    : (Intent) intent.getParcelableExtra(Intent.EXTRA_INTENT);
            Updater.onPendingUserAction(session, confirm);
            return;
        }
        Updater.onStatus(session, status,
                intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE));
    }
}
