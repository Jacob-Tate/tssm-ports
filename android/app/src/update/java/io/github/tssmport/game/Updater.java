package io.github.tssmport.game;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Application;
import android.app.PendingIntent;
import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.content.pm.Signature;
import android.net.ConnectivityManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.os.UserManager;
import android.provider.Settings;
import android.util.Log;
import android.widget.Button;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.lang.ref.WeakReference;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.List;
import java.util.TreeSet;
import java.util.concurrent.atomic.AtomicReference;

import org.libsdl.app.SDLActivity;

/**
 * Android's side of in-app updates (src/SB/Core/pc/iUpdate.h; docs/ANDROID.md
 * "Updates"). src/SB/Core/pc/android/iAndroidUpdate.cpp calls the public
 * static methods on the updater's worker thread, never on the UI thread:
 *
 * <ul>
 * <li>{@link #get}: one HTTPS GET (UpdateNet), each body chunk handed to the
 * core as it arrives;</li>
 * <li>{@link #installBlocked}: why this copy must not be replaced by the
 * public file, or null;</li>
 * <li>{@link #prepareDownload}: Android's permission for this app to install
 * apps (the player is sent to the system setting, with a word first, when it
 * is off), and the download's path in the app's private cache folder;</li>
 * <li>{@link #install}: the checked file into a PackageInstaller session.
 * Before it the download is read as an app: the same package, a version code
 * not lower than this copy's, the same signing key. The player is told the
 * game will close, then Android asks in its own window. Replacing the app ends
 * its process, so a successful install is seldom reported back here; the
 * player opens the game again.</li>
 * </ul>
 *
 * <p>Saves, settings and the game's files are not touched: the download lives
 * in the cache folder, and an update keeps the app's data as any update does.
 */
public final class Updater {

    private static final String TAG = "tssm";

    /** The public app. A copy under another package (the co-op app) is not replaced. */
    static final String PUBLIC_PACKAGE = "io.github.tssmport.game";

    /** The broadcast PackageInstaller answers with (UpdateReceiver). */
    static final String ACTION_STATUS = "io.github.tssmport.game.action.UPDATE_STATUS";

    /** How long the game may be in front, after asking Android, with no answer from it. */
    static final long NO_ANSWER_MS = 60_000;

    /** How long Android's settings may take to open. */
    static final long SETTINGS_OPEN_MS = 5_000;

    private static final int WAIT_STEP_MS = 250;

    private static final String UNCHANGED = UpdateText.UNCHANGED;

    // --- native (iAndroidUpdate.cpp) ---------------------------------------

    private static native boolean nativeOnResponse(long ctx, int status, long contentLength,
            String location);

    private static native boolean nativeOnBody(long ctx, byte[] data, int n);

    private static native boolean nativeCancelled(long ctx);

    /** True once the game is ending: every wait here gives up. */
    private static native boolean nativeStopping();

    private static native void nativeLog(String line);

    // --- state, under LOCK ---------------------------------------------------

    private static final Object LOCK = new Object();

    private static Context sApp;
    private static String sNativeVersion = "";
    private static boolean sWatching;

    /** The app's activity in front, from the lifecycle callbacks (UI thread). */
    private static WeakReference<Activity> sResumed = new WeakReference<>(null);
    private static int sResumeCount;
    private static int sPauseCount;

    /** The install under way: its session, Android's window and its answer. */
    private static int sSession = -1;
    private static Intent sConfirm;
    private static boolean sConfirmStarted;
    private static boolean sHaveResult;
    private static int sResultStatus;
    private static String sResultMessage;

    private Updater() {
    }

    // --- start-up ----------------------------------------------------------

    /**
     * Once, from iUpdateCreatePlatform on the game's thread. `version` is the
     * one the native code was built with (TSSM_UPDATE_VERSION).
     */
    public static void init(Activity activity, String version) {
        synchronized (LOCK) {
            sApp = activity.getApplicationContext();
            sNativeVersion = version != null ? version : "";
            sResumed = new WeakReference<>(activity);
            if (!sWatching) {
                activity.getApplication().registerActivityLifecycleCallbacks(new Lifecycle());
                sWatching = true;
            }
        }
        if (!BuildConfig.VERSION_NAME.equals(sNativeVersion)) {
            log("the app is version " + BuildConfig.VERSION_NAME + " but its game code is "
                    + sNativeVersion + "; updates are not installed");
        }
        cleanUp();
    }

    /** What an earlier run left: a download, an install Android never finished. */
    private static void cleanUp() {
        deleteContents(updateDir());
        try {
            PackageInstaller pi = sApp.getPackageManager().getPackageInstaller();
            for (PackageInstaller.SessionInfo info : pi.getMySessions()) {
                if (!info.isActive()) {
                    pi.abandonSession(info.getSessionId());
                    log("removed an unfinished install from an earlier run");
                }
            }
        } catch (RuntimeException e) {
            log("could not look for unfinished installs: " + UpdateNet.text(e));
        }
    }

    // --- iUpdatePlatform::Get ----------------------------------------------

    /** "<iUpdateNet>:<detail for the log>". */
    public static String get(final long ctx, String host, String path, String accept,
            int timeoutMs) {
        if (!allowedHost(host)) {
            return UpdateNet.ERROR + ":" + host + " is not a host the updater uses";
        }
        if (offline()) {
            return UpdateNet.OFFLINE + ":no network";
        }
        UpdateNet.Result r = UpdateNet.get(host, 443, path, accept, timeoutMs,
                new UpdateNet.Sink() {
                    @Override
                    public boolean onResponse(int status, long contentLength, String location) {
                        return nativeOnResponse(ctx, status, contentLength, location);
                    }

                    @Override
                    public boolean onBody(byte[] data, int n) {
                        return nativeOnBody(ctx, data, n);
                    }

                    @Override
                    public boolean cancelled() {
                        return nativeCancelled(ctx);
                    }
                });
        return r.code + ":" + r.detail;
    }

    /** iUpdateHostAllowed's four, again. */
    static boolean allowedHost(String host) {
        return "api.github.com".equals(host) || "github.com".equals(host)
                || "objects.githubusercontent.com".equals(host)
                || "release-assets.githubusercontent.com".equals(host);
    }

    private static boolean offline() {
        try {
            ConnectivityManager cm =
                    (ConnectivityManager) sApp.getSystemService(Context.CONNECTIVITY_SERVICE);
            return cm != null && cm.getActiveNetwork() == null;
        } catch (RuntimeException e) {
            // Not known: the connection says.
            return false;
        }
    }

    // --- iUpdatePlatform::InstallAllowed -------------------------------------

    /** Why this copy is not replaced with the public file, or null. */
    public static String installBlocked() {
        Context app = sApp;
        if (app == null) {
            return "The updater is not ready.";
        }
        String pkg = app.getPackageName();
        if (!PUBLIC_PACKAGE.equals(pkg)) {
            return "This copy is a separate app (" + pkg + "). The public file would install "
                    + "beside it as the other app, so it is not installed from here.";
        }
        if (!BuildConfig.VERSION_NAME.equals(sNativeVersion)) {
            return "This copy's app and game code disagree about its version ("
                    + BuildConfig.VERSION_NAME + ", " + sNativeVersion
                    + "), so it is not updated from here.";
        }
        try {
            UserManager um = (UserManager) app.getSystemService(Context.USER_SERVICE);
            if (um != null && (um.hasUserRestriction(UserManager.DISALLOW_INSTALL_UNKNOWN_SOURCES)
                    || (Build.VERSION.SDK_INT >= 29 && um.hasUserRestriction(
                            UserManager.DISALLOW_INSTALL_UNKNOWN_SOURCES_GLOBALLY)))) {
                return "This device's settings do not allow installing apps from outside an app "
                        + "store, so the update cannot be installed here.";
            }
        } catch (RuntimeException e) {
            log("could not read the device's install restrictions: " + UpdateNet.text(e));
        }
        return null;
    }

    // --- iUpdatePlatform::DownloadPath ---------------------------------------

    /** The download's path, or "!" and why there is none. */
    public static String prepareDownload() {
        try {
            String blocked = installBlocked();
            if (blocked != null) {
                return "!" + blocked;
            }
            if (!canInstallPackages()) {
                String why = askForPermission();
                if (why != null) {
                    return "!" + why;
                }
            }
            File dir = updateDir();
            if (!dir.isDirectory() && !dir.mkdirs()) {
                return "!Could not make the download folder." + UNCHANGED;
            }
            deleteContents(dir);
            return new File(dir, "download.apk").getAbsolutePath();
        } catch (RuntimeException e) {
            log("preparing the download: " + UpdateNet.text(e));
            return "!The download could not be prepared." + UNCHANGED;
        }
    }

    /** Whether Android lets this app install apps (the "install unknown apps" permission). */
    @SuppressWarnings("deprecation")
    static boolean canInstallPackages() {
        if (Build.VERSION.SDK_INT >= 26) {
            return sApp.getPackageManager().canRequestPackageInstalls();
        }
        // Android 7: the one "Unknown sources" switch for every app.
        return Settings.Secure.getInt(sApp.getContentResolver(),
                Settings.Secure.INSTALL_NON_MARKET_APPS, 0) != 0;
    }

    /** Sends the player to the setting and waits for them to come back. Null once it is on. */
    private static String askForPermission() {
        String where = Build.VERSION.SDK_INT >= 26
                ? "turn on \"Allow from this source\" for this app"
                : "turn on \"Unknown sources\"";
        // Android 11 at least ends the app when this permission is granted;
        // the player then opens the game and checks again.
        if (!ask("Allow updates?", "Android must let this app install apps before it can "
                + "install its own update. Press Open settings, " + where
                + ", then come back to the game.\n\nSave first: some Android versions restart "
                + "the game when you allow this. If that happens, check for updates again.",
                "Open settings")) {
            log("the player did not open the install permission's setting");
            return "Not downloaded: updates need Android's permission for this app to install "
                    + "apps." + UNCHANGED;
        }
        int pauses;
        int resumes;
        synchronized (LOCK) {
            pauses = sPauseCount;
            resumes = sResumeCount;
        }
        if (!openInstallSettings()) {
            return "Android's settings did not open. In Settings, find \"Install unknown apps\", "
                    + "allow this app, then try again." + UNCHANGED;
        }
        log("waiting for the player to come back from Android's settings");
        long start = SystemClock.elapsedRealtime();
        synchronized (LOCK) {
            while (true) {
                boolean left = sPauseCount > pauses;
                if (left && sResumeCount > resumes && sResumed.get() != null) {
                    break;
                }
                if (nativeStopping()) {
                    return "The game is closing." + UNCHANGED;
                }
                if (!left && SystemClock.elapsedRealtime() - start > SETTINGS_OPEN_MS) {
                    return "Android's settings did not open. In Settings, find \"Install unknown "
                            + "apps\", allow this app, then try again." + UNCHANGED;
                }
                if (!pause()) {
                    return "Stopped." + UNCHANGED;
                }
            }
        }
        if (!canInstallPackages()) {
            log("back from Android's settings; the permission is still off");
            return "The permission for this app to install apps is still off, so nothing was "
                    + "downloaded." + UNCHANGED;
        }
        log("back from Android's settings; the permission is on");
        return null;
    }

    private static boolean openInstallSettings() {
        final Activity a = frontActivity();
        if (a == null) {
            return false;
        }
        final String pkg = sApp.getPackageName();
        final int[] opened = { -1 };
        a.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                List<Intent> tries = new ArrayList<>();
                if (Build.VERSION.SDK_INT >= 26) {
                    tries.add(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,
                            Uri.parse("package:" + pkg)));
                    tries.add(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES));
                }
                tries.add(new Intent(Settings.ACTION_SECURITY_SETTINGS));
                boolean ok = false;
                for (Intent i : tries) {
                    try {
                        a.startActivity(i);
                        ok = true;
                        break;
                    } catch (ActivityNotFoundException | SecurityException e) {
                        log("no settings page for " + i.getAction() + ": " + UpdateNet.text(e));
                    }
                }
                answer(opened, ok ? 1 : 0);
            }
        });
        return waitForAnswer(opened) == 1;
    }

    // --- iUpdatePlatform::Install --------------------------------------------

    /**
     * Null when Android installed the update (seldom seen: it ends the app
     * first), or why it was not installed. `path` has been checked against
     * the release's SHA256SUMS by the core.
     */
    public static String install(String path, String version) {
        try {
            return installChecked(new File(path), version);
        } catch (RuntimeException e) {
            log("install: " + UpdateNet.text(e));
            synchronized (LOCK) {
                sSession = -1;
            }
            return "Android could not install the update." + UNCHANGED;
        }
    }

    private static String installChecked(File file, String version) {
        String blocked = installBlocked();
        if (blocked != null) {
            return blocked;
        }
        PackageManager pm = sApp.getPackageManager();
        String pkg = sApp.getPackageName();

        // What the download is, before Android is asked.
        PackageInfo archive = archiveInfo(pm, file.getPath());
        if (archive == null) {
            return "The download is not an app Android can read, so it was not installed."
                    + UNCHANGED;
        }
        if (!pkg.equals(archive.packageName)) {
            return "The download is a different app (" + archive.packageName
                    + "), so it was not installed." + UNCHANGED;
        }
        PackageInfo installed;
        try {
            installed = pm.getPackageInfo(pkg, signingFlags());
        } catch (PackageManager.NameNotFoundException e) {
            return "Android does not know this app's own package." + UNCHANGED;
        }
        long have = versionCode(installed);
        long get = versionCode(archive);
        log("the download is " + archive.versionName + " (version code " + get
                + "), this copy " + installed.versionName + " (" + have + ")");
        if (get < have) {
            return UpdateText.downgrade(get, have);
        }
        TreeSet<String> newKeys = archiveSigners(pm, file.getPath(), archive);
        TreeSet<String> ownKeys = signers(installed);
        if (newKeys.isEmpty() || ownKeys.isEmpty()) {
            log("the signing keys could not be compared here; Android compares them");
        } else if (!newKeys.equals(ownKeys)) {
            log("the download is signed by " + newKeys + ", this copy by " + ownKeys);
            return UpdateText.DIFFERENT_KEY;
        }
        if (!canInstallPackages()) {
            return "Android's permission for this app to install apps is off. Turn it on "
                    + "(Settings, Apps, Special app access, Install unknown apps), then check "
                    + "for updates again." + UNCHANGED;
        }

        // The player's go-ahead: Android ends the game to replace it. Where
        // the two apps' names differ (a test copy, a build with a name of its
        // own), the dialog says what replaces what.
        String from = appName(pm, installed.applicationInfo, null);
        String to = appName(pm, archive.applicationInfo, file.getPath());
        log("replacing \"" + from + "\" with \"" + to + "\"");
        String replaces = from != null && to != null && !from.equals(to)
                ? "This replaces \"" + from + "\" with \"" + to + "\". "
                : "";
        if (!ask("Install version " + version + "?", replaces + "Android asks you to confirm "
                + "next. The game closes while the update installs, so progress since your last "
                + "save is lost. Saves and settings stay. Open the game again afterwards.",
                "Install")) {
            log("the player cancelled before Android was asked");
            return "Not installed: you chose Cancel." + UNCHANGED;
        }
        return commitAndWait(pm, pkg, file);
    }

    private static String commitAndWait(PackageManager pm, String pkg, File file) {
        PackageInstaller pi = pm.getPackageInstaller();
        PackageInstaller.SessionParams params =
                new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        params.setAppPackageName(pkg);
        params.setSize(file.length());
        if (Build.VERSION.SDK_INT >= 26) {
            params.setInstallReason(PackageManager.INSTALL_REASON_USER);
        }
        if (Build.VERSION.SDK_INT >= 31) {
            // Always the player's own yes in Android's window.
            params.setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_REQUIRED);
        }
        int id;
        try {
            id = pi.createSession(params);
        } catch (IOException | RuntimeException e) {
            log("createSession: " + UpdateNet.text(e));
            return "Android could not start the install." + UNCHANGED;
        }
        synchronized (LOCK) {
            sSession = id;
            sConfirm = null;
            sConfirmStarted = false;
            sHaveResult = false;
            sResultMessage = null;
        }
        try {
            PackageInstaller.Session s = pi.openSession(id);
            try {
                try (InputStream in = new FileInputStream(file);
                        OutputStream out = s.openWrite("base.apk", 0, file.length())) {
                    byte[] buffer = new byte[64 * 1024];
                    int n;
                    while ((n = in.read(buffer)) > 0) {
                        out.write(buffer, 0, n);
                    }
                    s.fsync(out);
                }
                Intent status = new Intent(ACTION_STATUS).setClass(sApp, UpdateReceiver.class)
                        .setPackage(pkg);
                int flags = PendingIntent.FLAG_UPDATE_CURRENT;
                if (Build.VERSION.SDK_INT >= 31) {
                    // Android adds the status to it.
                    flags |= PendingIntent.FLAG_MUTABLE;
                }
                PendingIntent pending = PendingIntent.getBroadcast(sApp, id, status, flags);
                s.commit(pending.getIntentSender());
            } finally {
                s.close();
            }
        } catch (IOException | RuntimeException e) {
            log("session " + id + ": " + UpdateNet.text(e));
            abandon(pi, id);
            synchronized (LOCK) {
                sSession = -1;
            }
            return "Android could not take the update." + UNCHANGED;
        }
        log("session " + id + " handed to Android");
        return waitForResult(pi, id);
    }

    /**
     * Android's answer: its window first (PENDING_USER_ACTION, opened while
     * the game is in front), then the result. Where Android never answers
     * (its window dismissed on an old version), the game back in front for
     * NO_ANSWER_MS ends the wait.
     */
    private static String waitForResult(PackageInstaller pi, int id) {
        long frontSince = -1;
        synchronized (LOCK) {
            while (true) {
                if (sHaveResult) {
                    int status = sResultStatus;
                    String message = sResultMessage;
                    sSession = -1;
                    log("Android's answer: status " + status
                            + (message != null ? " (" + message + ")" : ""));
                    return status == UpdateText.STATUS_SUCCESS ? null
                            : UpdateText.installFailure(status, message);
                }
                if (nativeStopping()) {
                    sSession = -1;
                    log("the game is closing; no longer waiting for Android");
                    return "The game closed before Android answered." + UNCHANGED;
                }
                long now = SystemClock.elapsedRealtime();
                boolean front = sResumed.get() != null && sConfirm == null;
                if (!front) {
                    frontSince = -1;
                } else if (frontSince < 0) {
                    frontSince = now;
                } else if (now - frontSince > NO_ANSWER_MS) {
                    boolean asked = sConfirmStarted;
                    sSession = -1;
                    log("no answer from Android (its window " + (asked ? "was" : "was not")
                            + " opened)");
                    if (!asked) {
                        abandon(pi, id);
                    }
                    return "Android did not answer about the update. If you closed its window, "
                            + "nothing was changed; check for updates to try again.";
                }
                if (!pause()) {
                    sSession = -1;
                    return "Stopped." + UNCHANGED;
                }
            }
        }
    }

    // --- PackageInstaller's broadcasts (UpdateReceiver, UI thread) ------------

    /** Android's window for a session: opened now if the game is in front, else when it is. */
    static void onPendingUserAction(int session, Intent confirm) {
        synchronized (LOCK) {
            if (session != sSession || sHaveResult) {
                log("Android asked about install " + session + ", which is not waited for");
                return;
            }
            if (confirm == null) {
                setResult(UpdateText.STATUS_FAILURE, "no window from Android to confirm with");
                return;
            }
            sConfirm = confirm;
        }
        openConfirm();
    }

    static void onStatus(int session, int status, String message) {
        synchronized (LOCK) {
            if (session == sSession && !sHaveResult) {
                setResult(status, message);
                return;
            }
        }
        // After a successful update this runs in the new version's process,
        // which has no game running: logcat only.
        Log.i(TAG, "update: Android says status " + status + " for install " + session
                + (message != null ? " (" + message + ")" : ""));
    }

    /** Under LOCK. */
    private static void setResult(int status, String message) {
        sHaveResult = true;
        sResultStatus = status;
        sResultMessage = message;
        sConfirm = null;
        LOCK.notifyAll();
    }

    private static void openConfirm() {
        Activity a;
        Intent confirm;
        synchronized (LOCK) {
            a = sResumed.get();
            confirm = sConfirm;
            if (a == null || confirm == null) {
                return;
            }
            sConfirm = null;
        }
        confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            a.startActivity(confirm);
            synchronized (LOCK) {
                sConfirmStarted = true;
            }
            log("Android's install window opened");
        } catch (ActivityNotFoundException | SecurityException e) {
            synchronized (LOCK) {
                setResult(UpdateText.STATUS_FAILURE,
                        "Android's install window did not open: " + UpdateNet.text(e));
            }
        }
    }

    private static final class Lifecycle implements Application.ActivityLifecycleCallbacks {
        @Override
        public void onActivityResumed(Activity activity) {
            synchronized (LOCK) {
                sResumed = new WeakReference<>(activity);
                sResumeCount++;
                LOCK.notifyAll();
            }
            openConfirm();
        }

        @Override
        public void onActivityPaused(Activity activity) {
            synchronized (LOCK) {
                if (sResumed.get() == activity) {
                    sResumed = new WeakReference<>(null);
                }
                sPauseCount++;
                LOCK.notifyAll();
            }
        }

        @Override
        public void onActivityCreated(Activity activity, Bundle savedInstanceState) {
        }

        @Override
        public void onActivityStarted(Activity activity) {
        }

        @Override
        public void onActivityStopped(Activity activity) {
        }

        @Override
        public void onActivitySaveInstanceState(Activity activity, Bundle outState) {
        }

        @Override
        public void onActivityDestroyed(Activity activity) {
        }
    }

    // --- the download as an app ----------------------------------------------

    @SuppressWarnings("deprecation")
    private static int signingFlags() {
        return Build.VERSION.SDK_INT >= 28 ? PackageManager.GET_SIGNING_CERTIFICATES
                : PackageManager.GET_SIGNATURES;
    }

    @SuppressWarnings("deprecation")
    private static PackageInfo archiveInfo(PackageManager pm, String path) {
        return pm.getPackageArchiveInfo(path, signingFlags());
    }

    /** The download's signers; the older flag where the newer one gave none. */
    @SuppressWarnings("deprecation")
    private static TreeSet<String> archiveSigners(PackageManager pm, String path,
            PackageInfo archive) {
        TreeSet<String> keys = signers(archive);
        if (keys.isEmpty() && Build.VERSION.SDK_INT >= 28) {
            PackageInfo older = pm.getPackageArchiveInfo(path, PackageManager.GET_SIGNATURES);
            if (older != null) {
                keys = signers(older);
            }
        }
        return keys;
    }

    /** SHA-256 of each certificate the package is signed with now. */
    @SuppressWarnings("deprecation")
    private static TreeSet<String> signers(PackageInfo info) {
        Signature[] sigs = null;
        if (Build.VERSION.SDK_INT >= 28 && info.signingInfo != null) {
            sigs = info.signingInfo.getApkContentsSigners();
        }
        if (sigs == null || sigs.length == 0) {
            sigs = info.signatures;
        }
        TreeSet<String> out = new TreeSet<>();
        if (sigs == null) {
            return out;
        }
        try {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            for (Signature s : sigs) {
                if (s == null) {
                    continue;
                }
                byte[] d = md.digest(s.toByteArray());
                StringBuilder hex = new StringBuilder();
                for (byte b : d) {
                    hex.append(String.format(java.util.Locale.ROOT, "%02x", b & 0xff));
                }
                out.add(hex.toString());
            }
        } catch (NoSuchAlgorithmException e) {
            out.clear();
        }
        return out;
    }

    /**
     * An app's name as the launcher shows it, or null. `archivePath`: the
     * name is read from that APK's own resources.
     */
    private static String appName(PackageManager pm, ApplicationInfo info, String archivePath) {
        if (info == null) {
            return null;
        }
        try {
            if (archivePath != null) {
                info.sourceDir = archivePath;
                info.publicSourceDir = archivePath;
            }
            CharSequence label = info.loadLabel(pm);
            String name = label != null ? label.toString().trim() : "";
            return name.isEmpty() || name.length() > 80 ? null : name;
        } catch (RuntimeException e) {
            log("could not read an app's name: " + UpdateNet.text(e));
            return null;
        }
    }

    @SuppressWarnings("deprecation")
    private static long versionCode(PackageInfo info) {
        return Build.VERSION.SDK_INT >= 28 ? info.getLongVersionCode() : info.versionCode;
    }

    private static void abandon(PackageInstaller pi, int id) {
        try {
            pi.abandonSession(id);
        } catch (RuntimeException e) {
            log("abandoning install " + id + ": " + UpdateNet.text(e));
        }
    }

    // --- small things --------------------------------------------------------

    private static File updateDir() {
        return new File(sApp.getCacheDir(), "update");
    }

    private static void deleteContents(File dir) {
        File[] files = dir.listFiles();
        if (files == null) {
            return;
        }
        for (File f : files) {
            if (f.isFile() && !f.delete()) {
                log("could not delete " + f.getName() + " from the download folder");
            }
        }
    }

    private static Activity frontActivity() {
        Activity a;
        synchronized (LOCK) {
            a = sResumed.get();
        }
        if (a == null) {
            a = SDLActivity.getContext();
        }
        return a != null && !a.isFinishing() ? a : null;
    }

    /** One step of a wait, under LOCK. False when interrupted. */
    private static boolean pause() {
        try {
            LOCK.wait(WAIT_STEP_MS);
            return true;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return false;
        }
    }

    /**
     * A question in an Android dialog over the game, Cancel focused (and
     * Back, B or a tap outside cancels). Waits on this thread for the answer.
     */
    private static boolean ask(final String title, final String message, final String yes) {
        final Activity a = frontActivity();
        if (a == null) {
            log("no activity to ask \"" + title + "\" in");
            return false;
        }
        final int[] answer = { -1 };
        final AtomicReference<AlertDialog> shown = new AtomicReference<>();
        a.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    final AlertDialog d = new AlertDialog.Builder(a,
                            android.R.style.Theme_DeviceDefault_Dialog_Alert)
                            .setTitle(title)
                            .setMessage(message)
                            .setNegativeButton("Cancel", new DialogInterface.OnClickListener() {
                                @Override
                                public void onClick(DialogInterface dialog, int which) {
                                    answer(answer, 0);
                                }
                            })
                            .setPositiveButton(yes, new DialogInterface.OnClickListener() {
                                @Override
                                public void onClick(DialogInterface dialog, int which) {
                                    answer(answer, 1);
                                }
                            })
                            .setOnCancelListener(new DialogInterface.OnCancelListener() {
                                @Override
                                public void onCancel(DialogInterface dialog) {
                                    answer(answer, 0);
                                }
                            })
                            .create();
                    d.setOnShowListener(new DialogInterface.OnShowListener() {
                        @Override
                        public void onShow(DialogInterface dialog) {
                            Button cancel = d.getButton(DialogInterface.BUTTON_NEGATIVE);
                            if (cancel != null) {
                                cancel.setFocusable(true);
                                cancel.requestFocus();
                            }
                        }
                    });
                    shown.set(d);
                    d.show();
                } catch (RuntimeException e) {
                    log("could not ask \"" + title + "\": " + UpdateNet.text(e));
                    answer(answer, 0);
                }
            }
        });
        int got = waitForAnswer(answer);
        if (got < 0) {
            a.runOnUiThread(new Runnable() {
                @Override
                public void run() {
                    AlertDialog d = shown.get();
                    if (d != null && d.isShowing()) {
                        d.dismiss();
                    }
                }
            });
        }
        return got == 1;
    }

    private static void answer(int[] slot, int value) {
        synchronized (slot) {
            if (slot[0] < 0) {
                slot[0] = value;
                slot.notifyAll();
            }
        }
    }

    /** The slot's value once set; -1 when the game ends first. */
    private static int waitForAnswer(int[] slot) {
        synchronized (slot) {
            while (slot[0] < 0) {
                if (nativeStopping()) {
                    return -1;
                }
                try {
                    slot.wait(WAIT_STEP_MS);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    return -1;
                }
            }
            return slot[0];
        }
    }

    /** A line in the game's log (tssm-log.txt and logcat). */
    static void log(String line) {
        boolean game;
        synchronized (LOCK) {
            game = sApp != null;
        }
        if (game) {
            try {
                nativeLog(line);
                return;
            } catch (UnsatisfiedLinkError e) {
                // The game's library is not in this process.
            }
        }
        Log.i(TAG, "update: android: " + line);
    }
}
