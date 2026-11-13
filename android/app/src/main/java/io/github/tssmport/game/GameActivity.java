package io.github.tssmport.game;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ContentResolver;
import android.content.Context;
import android.content.Intent;
import android.hardware.Sensor;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.webkit.MimeTypeMap;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

/**
 * The activity that runs the game. Everything the game does is native;
 * SDLActivity stands between it and the framework, and this subclass changes
 * three things about it (the libraries, the start-up environment and a
 * surface without the accelerometer), and saves and reads the documents the
 * system's file picker hands the Saves tab. Based on treedome's BfbbActivity (CC0).
 */
public class GameActivity extends SDLActivity {

    private static final String TAG = "tssm";

    /**
     * One native library. SDL is linked statically into libmain.so
     * (CMakeLists.txt uses the vendored, static SDL on Android), so there is
     * no libSDL3.so to load first; SDL's JNI_OnLoad is inside libmain.so.
     */
    @Override
    protected String[] getLibraries() {
        return new String[] { "main" };
    }

    /**
     * Makes the assets directory the game reads by default
     * (Android/data/&lt;package&gt;/files/assets, which iAndroidStartup in
     * src/SB/Core/pc/android/iAndroid.cpp hands the game as TSSM_ASSETS), and
     * passes a folder used in place as TSSM_ASSETS instead.
     */
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // The co-op app's writes to the saves stop here (SaveShareProvider).
        SaveShareProvider.gameStarting();

        File external = getExternalFilesDir(null);

        if (external != null) {
            File assets = new File(external, "assets");

            if (!assets.exists() && !assets.mkdirs()) {
                // Not fatal: the game reports a missing folder with its path.
                Log.w(TAG, "could not create " + assets.getAbsolutePath());
            }
        } else {
            Log.w(TAG, "no external files directory; assets must come from config.ini");
        }

        super.onCreate(savedInstanceState);

        // Before SDL_main, which starts once the surface exists.
        try {
            String linked = ImportActivity.linkedPath(this);
            if (linked != null) {
                nativeSetenv("TSSM_ASSETS", linked);
            }

            // A test launch from adb (TestLaunch): its settings, for this
            // process only. Empty for a normal launch.
            java.util.Map<String, String> test = TestLaunch.settings(this, getIntent());

            // "Play without saving" (CoopNoticeActivity): a folder emptied
            // at each start, so nothing is kept and no other save is touched.
            // A test launch has its own folder and never shares the saves.
            if (test.isEmpty() && CoopSaves.noSaves()) {
                File unsaved = new File(getCacheDir(), "no-saves");
                deleteTree(unsaved);
                unsaved.mkdirs();
                nativeSetenv("TSSM_SAVE_DIR", unsaved.getPath());
            }

            for (java.util.Map.Entry<String, String> e : test.entrySet()) {
                // A code or password never goes to logcat.
                boolean secret = e.getKey().equals("TSSM_COOP_PASSWORD")
                        || e.getKey().equals("TSSM_TEST_COOP_CODE");
                Log.i(TAG, "test launch: " + e.getKey() + "="
                        + (secret ? "(hidden)" : e.getValue()));
                nativeSetenv(e.getKey(), e.getValue());
                if (e.getKey().equals("TSSM_ANDROID_ACCEL")) {
                    sAccelerometer = e.getValue().equals("1");
                }
            }
        } catch (UnsatisfiedLinkError e) {
            // libmain.so did not load; SDLActivity has already said so.
            Log.w(TAG, "could not pass the assets folder", e);
        }
    }

    private static void deleteTree(File f) {
        File[] children = f.listFiles();
        if (children != null) {
            for (File c : children) {
                deleteTree(c);
            }
        }
        f.delete();
    }

    @Override
    protected void onResume() {
        super.onResume();
        CoopNet.resumed(this, true);
    }

    @Override
    protected void onPause() {
        CoopNet.resumed(this, false);
        super.onPause();
    }

    // --- co-op: called by native code on the game's thread -------------------
    //
    // src/SB/Core/pc/android/iAndroidNet.cpp and iAndroidSaves.cpp.

    /** The Wi-Fi locks co-op wants now (CoopNet). */
    public static void coopLan(boolean multicast, boolean lowLatency) {
        Activity activity = mSingleton;
        if (activity != null) {
            CoopNet.want(activity, multicast, lowLatency);
        }
    }

    /** The network Android sends through (CoopNet.kind). */
    public static int coopNetKind() {
        Activity activity = mSingleton;
        return activity != null ? CoopNet.kind(activity) : 4;
    }

    /** The normal app's save-folder lock as a descriptor, or -1 (CoopSaves.claim). */
    public static int coopSavesClaim() {
        Activity activity = mSingleton;
        return activity != null ? CoopSaves.claim(activity) : -1;
    }

    /** The real saves copied into `root`; null when done, else why not. */
    public static String coopSavesMirror(String root) {
        return CoopSaves.mirror(root);
    }

    /** Before the save store changes its copy: marked as not sent (CoopSaves.begin). */
    public static void coopSavesBegin() {
        CoopSaves.begin();
    }

    /** What the save store changed, to the real saves. */
    public static void coopSavesPush() {
        CoopSaves.push();
    }

    /**
     * "Play without saving", chosen in the game when the saves could not be
     * shared: the same empty folder as CoopNoticeActivity's, or null.
     */
    public static String coopNoSavesFolder() {
        Activity activity = mSingleton;
        if (activity == null) {
            return null;
        }
        CoopSaves.chooseNoSaves();
        File unsaved = new File(activity.getCacheDir(), "no-saves");
        deleteTree(unsaved);
        return unsaved.mkdirs() ? unsaved.getPath() : null;
    }

    /** A line for the player, over the game. */
    static void coopToast(final String text) {
        final Activity activity = mSingleton;
        if (activity != null) {
            activity.runOnUiThread(() -> Toast.makeText(activity, text, Toast.LENGTH_LONG).show());
        }
    }

    /**
     * The game has ended (Quit in its menu, or the activity finished and SDL's
     * thread with it): the process ends too. Until it does, it holds the save
     * folder's lock, and the other app (co-op, or the normal game) would find
     * the saves in use. SDL would end it anyway before starting the game in
     * it again.
     */
    @Override
    protected void onDestroy() {
        super.onDestroy();
        Thread sdl = mSDLThread;
        if (isFinishing() && (mSDLMainFinished || sdl == null || !sdl.isAlive())) {
            Log.i(TAG, "the game has ended; ending the app's process");
            System.exit(0);
        }
    }

    // --- the surface, without the accelerometer ------------------------------

    /** TSSM_ANDROID_ACCEL=1 on a test launch keeps SDL's listener, for comparing. */
    private static volatile boolean sAccelerometer;

    /**
     * SDL's surface listens to the accelerometer for as long as the game is in
     * front, at SensorManager.SENSOR_DELAY_GAME: a sensor event on the UI
     * thread and a call into native code 50 or more times a second, waking a
     * core each time. The game reads no accelerometer (the touch controls and
     * gamepads are its input), and SDL uses the events for nothing else but to
     * notice the phone turned from one landscape to the other, which Android
     * already handles for the surface. So it is never turned on here.
     */
    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new GameSurface(context);
    }

    private static final class GameSurface extends SDLSurface {
        GameSurface(Context context) {
            super(context);
        }

        @Override
        protected void enableSensor(int sensortype, boolean enabled) {
            if (sensortype == Sensor.TYPE_ACCELEROMETER && !sAccelerometer) {
                return;
            }
            super.enableSensor(sensortype, enabled);
        }
    }

    // --- documents from the system's file picker -----------------------------
    //
    // The Saves tab's export and import (src/SB/Core/pc/android/iAndroidDocument.h).
    // SDL's own route opens a content:// URI in SDLActivity.openFileDescriptor,
    // which catches only FileNotFoundException: any other refusal reaches C as
    // fd 0 with the exception still pending, the save goes to /dev/null, the
    // game says it was exported, and the next JNI call aborts the app. These
    // throw instead, and the native side reports what they threw.

    /** The save picker's request; SDL's own requests count up from 0. */
    private static final int REQUEST_SAVE_DOCUMENT = 0x7553;

    /** The picker's answer, a URI or null (cancelled), to the native side. */
    private static native void nativeSaveDocumentPicked(String uri);

    /**
     * The system's "save as" picker, with the name filled in. SDL's sends no
     * name, and asks for any type when Android does not know the extension
     * (.gci), so the name typed got no extension. A type Android does not know
     * goes as plain bytes, which storage apps save under the name as typed.
     * Called by native code on the game's thread.
     */
    public static boolean showSaveDocument(String name, String extension) {
        final Activity activity = mSingleton;
        if (activity == null) {
            return false;
        }
        String mime = extension.isEmpty() ? null
                : MimeTypeMap.getSingleton().getMimeTypeFromExtension(extension);
        Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType(mime != null ? mime : "application/octet-stream");
        if (!name.isEmpty()) {
            intent.putExtra(Intent.EXTRA_TITLE, name);
        }
        try {
            activity.startActivityForResult(intent, REQUEST_SAVE_DOCUMENT);
        } catch (ActivityNotFoundException e) {
            Log.w(TAG, "no app saves documents", e);
            return false;
        }
        return true;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_SAVE_DOCUMENT) {
            Uri uri = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
            nativeSaveDocumentPicked(uri != null ? uri.toString() : null);
        }
    }

    /**
     * Replaces what a document holds with `data`. Closed here, after the
     * data is in, so the storage app sees the whole file when it is told the
     * document was closed (SDL hands the descriptor to C with detachFd).
     * Called by native code on the game's thread.
     */
    public static void writeDocument(String uri, byte[] data) throws IOException {
        ContentResolver resolver = resolver();
        Uri u = Uri.parse(uri);
        ParcelFileDescriptor pfd;
        try {
            pfd = resolver.openFileDescriptor(u, "wt");
        } catch (IllegalArgumentException | UnsupportedOperationException
                | FileNotFoundException e) {
            // Not every storage app takes "wt". The picker's document is new
            // and empty, so "w" leaves nothing of an older file behind; the
            // size check below would say so if it did.
            Log.i(TAG, "writeDocument: \"wt\" refused (" + e + "); trying \"w\"");
            pfd = resolver.openFileDescriptor(u, "w");
        }
        if (pfd == null) {
            throw new FileNotFoundException("the storage app gave no file for " + uri);
        }
        try (FileOutputStream out = new ParcelFileDescriptor.AutoCloseOutputStream(pfd)) {
            out.write(data);
            out.flush();
            try {
                out.getFD().sync();
            } catch (IOException e) {
                // A pipe (a cloud storage app) cannot sync; closing ends it.
            }
            long size = pfd.getStatSize(); // -1 for a pipe
            if (size >= 0 && size != data.length) {
                throw new IOException("wrote " + data.length + " bytes, but the document holds "
                        + size);
            }
        }
    }

    /** A whole document. Called by native code on the game's thread. */
    public static byte[] readDocument(String uri) throws IOException {
        try (InputStream in = resolver().openInputStream(Uri.parse(uri))) {
            if (in == null) {
                throw new FileNotFoundException("the storage app gave nothing for " + uri);
            }
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[65536];
            int n;
            while ((n = in.read(buffer)) != -1) {
                out.write(buffer, 0, n);
            }
            return out.toByteArray();
        }
    }

    private static ContentResolver resolver() {
        final Activity activity = mSingleton;
        if (activity == null) {
            throw new IllegalStateException("the game's activity is gone");
        }
        return activity.getContentResolver();
    }
}
