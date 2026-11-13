package io.github.tssmport.game;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * The test launch hook: settings for ONE launch from adb, for benchmarks and
 * test runs (tools/android_bench.py; docs/ANDROID.md, "Test launches").
 *
 * <pre>
 * adb shell am start -n io.github.tssmport.game.coop/io.github.tssmport.game.LauncherActivity \
 *     --es TSSM_PERF 5 --es TSSM_MOD mods/boot --es TSSM_EXIT_AFTER 7200
 * </pre>
 *
 * <p>String extras whose names are on {@link #KEYS} become environment
 * variables of the game's process before the native side starts, and only
 * when files/perf-test/armed exists in the app's private storage: only the
 * app itself and {@code adb shell run-as} (a debuggable build) can create it,
 * so no other app can steer a launch, and a launch without it is a normal one
 * whatever extras it carries.
 *
 * <p>A test launch never uses the player's own files. Every path key must
 * resolve inside files/perf-test (a relative value is taken from there), and
 * the config, save folder, mods folder and log are always pointed there, at
 * perf-test/config.ini, saves/, mods/ and tssm-log.txt unless the launch names
 * others. Nothing is stored: the next launch without extras is unchanged.
 */
final class TestLaunch {

    private static final String TAG = "tssm";

    static final String DIR = "perf-test";
    static final String ARMED = "armed";

    /** Keys that name files or folders: each must resolve inside perf-test. */
    private static final Set<String> PATH_KEYS = new HashSet<>(Arrays.asList(
            "TSSM_MOD", "TSSM_MODS_DIR", "TSSM_SAVE_DIR", "TSSM_CONFIG", "TSSM_LOG",
            "TSSM_CAPTURE_DIR", "TSSM_PERF_CSV", "TSSM_PERF_PROFILE_OUT", "TSSM_GUI_CAPTURE"));

    /** Every key a test launch takes. */
    static final Set<String> KEYS = new HashSet<>(Arrays.asList(
            "TSSM_MOD", "TSSM_MODS_DIR", "TSSM_SAVE_DIR", "TSSM_CONFIG", "TSSM_LOG",
            "TSSM_INPUT", "TSSM_TEST_PLACE", "TSSM_TEST_HOLD", "TSSM_EXIT_AFTER",
            "TSSM_CAPTURE_FRAMES", "TSSM_CAPTURE_DIR",
            "TSSM_PERF", "TSSM_PERF_CSV", "TSSM_PERF_PROFILE", "TSSM_PERF_PROFILE_OUT",
            // Switches that put back the paths before the CPU work of 0.9, for
            // comparing runs of one build (docs/ANDROID.md, "Heat, battery and
            // frame drops").
            "TSSM_GL_VAO", "TSSM_GL_STREAM", "TSSM_GL_SWAP_CACHE", "TSSM_VSYNC_PACE",
            "TSSM_PERF_HINT", "TSSM_ANDROID_ACCEL",
            // The same for the GPU work of 0.9 (docs/RENDERING.md, "What a
            // frame costs the GPU").
            "TSSM_GPU_OPT", "TSSM_GPU_POST", "TSSM_GPU_FIXEDSIZE", "TSSM_GPU_LOG",
            "TSSM_GPU_VERIFY", "TSSM_GPU_MSRTT", "TSSM_GPU_MSDISCARD",
            // Co-op runs between this phone and a PC (docs/MULTIPLAYER.md;
            // design N4): the session, the home-network finder's loopback
            // hooks, and the test actions both sides drive. Not
            // TSSM_LOCAL_COOP: local co-op is Windows only.
            "TSSM_COOP", "TSSM_COOP_PORT", "TSSM_COOP_NAME", "TSSM_COOP_PASSWORD",
            "TSSM_COOP_ADDR", "TSSM_COOP_BIND", "TSSM_COOP_LOG", "TSSM_COOP_FOLLOW",
            "TSSM_COOP_RECONNECT", "TSSM_COOP_SYNC_CAPTURE", "TSSM_COOP_LAN_PORT",
            "TSSM_COOP_LAN_TARGET",
            "TSSM_TEST_COOP_CODE", "TSSM_TEST_COOP_GO", "TSSM_TEST_COOP_KICK",
            "TSSM_TEST_COOP_LOOK", "TSSM_TEST_COOP_TAB", "TSSM_TEST_COOP_BUILD_SALT",
            "TSSM_TEST_COOP_ASSET_SALT",
            "TSSM_TEST_SCENE", "TSSM_TEST_WARP", "TSSM_TEST_EVENT", "TSSM_TEST_STALL",
            "TSSM_TEST_QUIT", "TSSM_TEST_SAVE", "TSSM_TEST_PLACE_ON", "TSSM_TEST_HEALTH",
            "TSSM_GUI_OPEN", "TSSM_GUI_OPEN_AT", "TSSM_GUI_TAB", "TSSM_GUI_CAPTURE",
            "TSSM_GUI_CAPTURE_FRAMES", "TSSM_GUI_CLOSE"));

    /** Pointed into perf-test on every test launch, unless the launch names them. */
    private static final String[][] DEFAULTS = {
            { "TSSM_CONFIG", "config.ini" },
            { "TSSM_SAVE_DIR", "saves" },
            { "TSSM_MODS_DIR", "mods" },
            { "TSSM_LOG", "tssm-log.txt" },
    };

    private static final int MAX_VALUE = 2048;

    private TestLaunch() {
    }

    /** The TSSM_ string extras of an intent, for passing on to GameActivity. */
    static void forward(Intent from, Intent to) {
        Bundle extras = from.getExtras();
        if (extras == null) {
            return;
        }
        for (String key : extras.keySet()) {
            if (key.startsWith("TSSM_")) {
                Object v = extras.get(key);
                if (v instanceof String) {
                    to.putExtra(key, (String) v);
                }
            }
        }
    }

    /**
     * The environment a launch asks for: empty for a normal launch, or every
     * accepted key with its value (paths made absolute). Anything refused is
     * logged and left out; a launch whose extras are refused as a whole (not
     * armed) is a normal launch.
     */
    static Map<String, String> settings(Context context, Intent intent) {
        Map<String, String> out = new LinkedHashMap<>();
        Bundle extras = intent != null ? intent.getExtras() : null;
        if (extras == null) {
            return out;
        }
        List<String> asked = new ArrayList<>();
        for (String key : extras.keySet()) {
            if (key.startsWith("TSSM_")) {
                asked.add(key);
            }
        }
        if (asked.isEmpty()) {
            return out;
        }

        File base;
        try {
            base = new File(context.getFilesDir(), DIR).getCanonicalFile();
        } catch (IOException e) {
            Log.w(TAG, "test launch: no private files folder; a normal launch", e);
            return out;
        }
        if (!new File(base, ARMED).isFile()) {
            Log.w(TAG, "test launch: " + asked + " given but " + base + "/" + ARMED
                    + " is not there; a normal launch");
            return out;
        }

        for (String key : asked) {
            Object raw = extras.get(key);
            if (!KEYS.contains(key)) {
                Log.w(TAG, "test launch: " + key + " is not a test key; ignored");
                continue;
            }
            if (!(raw instanceof String)) {
                Log.w(TAG, "test launch: " + key + " is not a string (--es); ignored");
                continue;
            }
            String value = (String) raw;
            if (value.length() > MAX_VALUE || hasControl(value)) {
                Log.w(TAG, "test launch: " + key + "'s value is too long or has control "
                        + "characters; ignored");
                continue;
            }
            if (PATH_KEYS.contains(key)) {
                String path = inside(base, value);
                if (path == null) {
                    Log.w(TAG, "test launch: " + key + "=" + value + " is not inside " + base
                            + "; ignored");
                    continue;
                }
                value = path;
            }
            out.put(key, value);
        }

        // The player's own config, saves, mods and log are never a test's.
        for (String[] d : DEFAULTS) {
            if (!out.containsKey(d[0])) {
                out.put(d[0], new File(base, d[1]).getPath());
            }
        }
        return out;
    }

    /** value as an absolute path inside base, or null when it would leave it. */
    private static String inside(File base, String value) {
        if (value.isEmpty()) {
            return null;
        }
        File f = value.startsWith("/") ? new File(value) : new File(base, value);
        try {
            File c = f.getCanonicalFile();
            String b = base.getPath() + File.separator;
            if (!c.getPath().startsWith(b)) {
                return null;
            }
            return c.getPath();
        } catch (IOException e) {
            return null;
        }
    }

    private static boolean hasControl(String s) {
        for (int i = 0; i < s.length(); i++) {
            if (s.charAt(i) < 0x20 || s.charAt(i) == 0x7f) {
                return true;
            }
        }
        return false;
    }
}
