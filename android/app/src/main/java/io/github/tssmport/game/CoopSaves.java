package io.github.tssmport.game;

import android.content.ContentProviderClient;
import android.content.Context;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ProviderInfo;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.os.RemoteException;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.TreeMap;
import java.util.TreeSet;

/**
 * The co-op app's half of the saves it shares with the normal app
 * (io.github.tssmport.game), whose SaveShareProvider is the other half
 * (docs/ANDROID.md, "The co-op app").
 *
 * <p>The game plays on a copy in this app's own save folder. Before it reads a
 * save, the native side takes the real folder's one-copy lock (claim) and this
 * copies the real saves in (mirror); after each change the save store makes, the
 * changed files go back (push), and the normal app writes them the way the game
 * does, keeping its backups. Each write names the file it replaces by its
 * SHA-256, so nothing the co-op app has not seen is ever replaced.
 */
final class CoopSaves {

    private static final String TAG = "tssm";

    static final String NORMAL_PACKAGE = "io.github.tssmport.game";
    /** The first normal app with SaveShareProvider. */
    static final long NORMAL_MIN_VERSION = 14;

    private static final String AUTHORITY = SaveShareProvider.AUTHORITY;
    private static final String PERMISSION = SaveShareProvider.PERMISSION;
    private static final int API = 1;

    enum Problem { NONE, NO_APP, OLD_APP, OTHER_KEY, NO_PERMISSION, UNREACHABLE }

    /** What probe found; settings and assets only when there is no problem. */
    static final class Probe {
        Problem problem = Problem.NONE;
        String why;
        byte[] config;
        String assets;
    }

    // "Play without saving", chosen on CoopNoticeActivity or in the game's box
    // (iAndroidSaves.cpp): in this process only, so no intent from another app
    // can turn saving off.
    private static volatile boolean sNoSaves;

    static void chooseNoSaves() {
        sNoSaves = true;
    }

    static boolean noSaves() {
        return sNoSaves;
    }

    /** LauncherActivity, when the normal app answered: saving is back on for the next game. */
    static void clearNoSaves() {
        sNoSaves = false;
    }

    private CoopSaves() {
    }

    // --- reaching the normal app -------------------------------------------------

    /**
     * Whether the normal app is there, new enough, signed with this app's key,
     * and answering; with its settings and game-files folder when it is. Binder
     * calls: not on the UI thread.
     */
    static Probe probe(Context context) {
        Probe p = new Probe();
        PackageManager pm = context.getPackageManager();
        PackageInfo normal;
        try {
            normal = pm.getPackageInfo(NORMAL_PACKAGE, 0);
        } catch (PackageManager.NameNotFoundException e) {
            p.problem = Problem.NO_APP;
            return p;
        }
        long version = Build.VERSION.SDK_INT >= 28 ? normal.getLongVersionCode() : normal.versionCode;
        if (pm.checkSignatures(context.getPackageName(), NORMAL_PACKAGE)
                != PackageManager.SIGNATURE_MATCH) {
            p.problem = Problem.OTHER_KEY;
            return p;
        }
        ProviderInfo info = pm.resolveContentProvider(AUTHORITY, 0);
        if (version < NORMAL_MIN_VERSION || info == null) {
            p.problem = Problem.OLD_APP;
            return p;
        }
        if (!NORMAL_PACKAGE.equals(info.packageName)) {
            p.problem = Problem.OTHER_KEY;
            return p;
        }
        if (context.checkSelfPermission(PERMISSION) != PackageManager.PERMISSION_GRANTED) {
            p.problem = Problem.NO_PERMISSION;
            return p;
        }
        try {
            Bundle hello = call(context, "hello", null, null);
            if (hello.getInt("api") != API) {
                p.problem = Problem.OLD_APP;
                return p;
            }
            Bundle settings = call(context, "settings", null, null);
            p.config = settings.getByteArray("config");
            p.assets = settings.getString("assets");
        } catch (IOException e) {
            p.problem = Problem.UNREACHABLE;
            p.why = e.getMessage();
        }
        return p;
    }

    /** One call; IOException for no answer or an answer that is not "ok". */
    private static Bundle call(Context context, String method, String arg, Bundle extras)
            throws IOException {
        ContentProviderClient client =
                context.getContentResolver().acquireUnstableContentProviderClient(AUTHORITY);
        if (client == null) {
            throw new IOException("SpongeBob Movie Port does not answer");
        }
        try {
            Bundle out = client.call(method, arg, extras);
            if (out == null) {
                throw new IOException("no answer to " + method);
            }
            if (!out.getBoolean("ok")) {
                throw new Refused(out.getString("error"), out.getString("message"));
            }
            return out;
        } catch (RemoteException | RuntimeException e) {
            throw new IOException(method + ": " + e, e);
        } finally {
            client.close();
        }
    }

    /** The provider answered, and said no. */
    static final class Refused extends IOException {
        final String error;

        Refused(String error, String message) {
            super((error != null ? error : "?") + ": " + message);
            this.error = error != null ? error : "";
        }
    }

    // --- the session (game thread) ----------------------------------------------

    private static final Object LOCK = new Object();
    private static Context sContext;
    private static File sRoot;
    /** SHA-256 of each writable file in the real folder, as this app last saw it. */
    private static final Map<String, String> sKnown = new HashMap<>();
    private static boolean sStopped;
    private static boolean sToldRetry;
    /** The not-sent mark's text as last written, so an unchanged mark is not written again. */
    private static String sMarked;

    /**
     * The real folder's lock file, opened read-write, as a descriptor the
     * native side flock()s and keeps; -1 when the saves are not shared or
     * cannot be reached.
     */
    static int claim(Context context) {
        if (sNoSaves) {
            return -1;
        }
        sContext = context.getApplicationContext();
        Probe p = probe(sContext);
        if (p.problem != Problem.NONE) {
            Log.w(TAG, "coop saves: " + p.problem + (p.why != null ? " (" + p.why + ")" : ""));
            return -1;
        }
        ContentProviderClient client =
                sContext.getContentResolver().acquireUnstableContentProviderClient(AUTHORITY);
        if (client == null) {
            return -1;
        }
        try {
            ParcelFileDescriptor pfd =
                    client.openFile(Uri.parse("content://" + AUTHORITY + "/claim"), "rw");
            return pfd != null ? pfd.detachFd() : -1;
        } catch (IOException | RemoteException | RuntimeException e) {
            Log.w(TAG, "coop saves: the claim was refused", e);
            return -1;
        } finally {
            client.close();
        }
    }

    /** Folders A, B and C..J, with their backup folders (iSaveStoreTargetDir). */
    private static List<String> folders() {
        List<String> dirs = new ArrayList<>();
        String[] targets = new String[10];
        targets[0] = "";
        targets[1] = "second/";
        for (int i = 2; i <= 9; i++) {
            targets[i] = "more/" + i + "/";
        }
        for (String t : targets) {
            dirs.add(t);
            dirs.add(t + "backup/");
        }
        return dirs;
    }

    /** The save store's leftover temporary of a file co-op may read. */
    private static boolean isTmp(String rel) {
        return rel.endsWith(".tmp") && SaveFolder.readable(rel.substring(0, rel.length() - 4));
    }

    /** Every save-store file in this app's folder, by path (with .tmp leftovers). */
    private static Map<String, File> scan(File root, boolean writableOnly) {
        Map<String, File> out = new TreeMap<>();
        for (String dir : folders()) {
            String[] names = new File(root, dir).list();
            if (names == null) {
                continue;
            }
            for (String name : names) {
                String rel = dir + name;
                File f = new File(root, rel);
                if (!f.isFile()) {
                    continue;
                }
                boolean take = writableOnly ? SaveFolder.writable(rel)
                        : SaveFolder.readable(rel) || isTmp(rel);
                if (take) {
                    out.put(rel, f);
                }
            }
        }
        return out;
    }

    /**
     * The real folder into this app's (`rootPath`). Null when done, else what
     * went wrong (nothing is then shared).
     *
     * <p>When the last session left changes not sent (the mark) and no file
     * changed both here and in the real folder since, the files co-op changed
     * stay and the first push sends them; the rest comes from the real
     * folder. Otherwise that copy is kept aside first and the real folder
     * replaces it, as at any other start.
     */
    static String mirror(String rootPath) {
        synchronized (LOCK) {
            File root = new File(rootPath);
            try {
                Bundle list = call(sContext, "list", null, null);
                String[] paths = list.getStringArray("paths");
                String[] hashes = list.getStringArray("sha256");
                long[] mtimes = list.getLongArray("mtimes");
                if (paths == null || hashes == null || mtimes == null
                        || hashes.length != paths.length || mtimes.length != paths.length) {
                    return "the list of saves was not complete";
                }
                Map<String, Integer> listed = new HashMap<>();
                Map<String, String> real = new HashMap<>();
                for (int i = 0; i < paths.length; i++) {
                    if (!SaveFolder.readable(paths[i]) || hashes[i] == null) {
                        return "a save had a name co-op does not know";
                    }
                    listed.put(paths[i], i);
                    if (SaveFolder.writable(paths[i])) {
                        real.put(paths[i], hashes[i]);
                    }
                }

                Map<String, Boolean> keep = null;
                if (unsentMarker().exists()) {
                    Map<String, String> marked = readMark();
                    keep = marked != null ? keepers(root, real, marked) : null;
                    if (keep == null) {
                        keepUnsent(root);
                    }
                }
                boolean resume = keep != null;

                for (Map.Entry<String, File> e : scan(root, false).entrySet()) {
                    String rel = e.getKey();
                    boolean own = resume && stays(keep, rel);
                    if (!own && !listed.containsKey(rel) && !e.getValue().delete()) {
                        return "could not remove the old copy of " + rel;
                    }
                }

                sKnown.clear();
                int copied = 0;
                for (int i = 0; i < paths.length; i++) {
                    if (SaveFolder.writable(paths[i])) {
                        sKnown.put(paths[i], hashes[i]);
                    }
                    if (resume && stays(keep, paths[i])) {
                        continue;
                    }
                    byte[] data = call(sContext, "read", paths[i], null).getByteArray("data");
                    if (data == null || !SaveFolder.sha256(data).equals(hashes[i])) {
                        return paths[i] + " changed while co-op was copying it";
                    }
                    File f = new File(root, paths[i]);
                    writeFile(f, data);
                    f.setLastModified(mtimes[i]);
                    copied++;
                }
                sRoot = root;
                sMarked = null;
                if (resume) {
                    Log.i(TAG, "coop saves: the last session's changes were not sent; they go at "
                            + "the first save-store change (" + copied + " files copied)");
                } else {
                    Log.i(TAG, "coop saves: copied " + copied + " files from SpongeBob Movie Port");
                }
                return null;
            } catch (IOException e) {
                Log.w(TAG, "coop saves: the copy failed", e);
                return e.getMessage() != null ? e.getMessage() : e.toString();
            }
        }
    }

    /**
     * File by file, which of the copy's writable files stay (true) and which
     * the real folder's replace (false), against what co-op last saw there
     * (`marked`): one co-op changed since stays when the real file is still
     * that one; one it did not change follows the real folder. Null when a
     * file changed on both sides, or a .tmp leftover's file changed there:
     * the copy is then kept aside whole.
     */
    private static Map<String, Boolean> keepers(File root, Map<String, String> real,
            Map<String, String> marked) {
        Map<String, File> local = scan(root, true);
        TreeSet<String> all = new TreeSet<>(real.keySet());
        all.addAll(marked.keySet());
        all.addAll(local.keySet());
        Map<String, Boolean> out = new HashMap<>();
        for (String rel : all) {
            String r = real.containsKey(rel) ? real.get(rel) : "";
            String k = marked.containsKey(rel) ? marked.get(rel) : "";
            String l = "";
            File f = local.get(rel);
            if (f != null) {
                try {
                    l = SaveFolder.sha256(readAll(f));
                } catch (IOException e) {
                    return null;
                }
            }
            if (r.equals(l) || r.equals(k)) {
                out.put(rel, true);
            } else if (l.equals(k)) {
                out.put(rel, false);
            } else {
                Log.w(TAG, "coop saves: " + rel + " changed both here and in SpongeBob Movie Port");
                return null;
            }
        }
        for (String rel : scan(root, false).keySet()) {
            if (isTmp(rel) && Boolean.FALSE.equals(out.get(rel.substring(0, rel.length() - 4)))) {
                Log.w(TAG, "coop saves: " + rel + " was being written here, and its file changed "
                        + "in SpongeBob Movie Port");
                return null;
            }
        }
        return out;
    }

    /** In a resumed copy: whether `rel` stays as it is here. */
    private static boolean stays(Map<String, Boolean> keep, String rel) {
        if (SaveFolder.writable(rel)) {
            return Boolean.TRUE.equals(keep.get(rel));
        }
        if (isTmp(rel)) {
            String base = rel.substring(0, rel.length() - 4);
            return SaveFolder.writable(base) && !Boolean.FALSE.equals(keep.get(base));
        }
        return false;
    }

    /**
     * Before the save store changes anything: the copy is marked as not
     * sent, with what the real folder was last seen to hold, until a push
     * sends everything. A start after co-op ended in between finds the mark.
     */
    static void begin() {
        synchronized (LOCK) {
            if (sRoot != null) {
                markUnsent();
            }
        }
    }

    /**
     * After each change the save store made: config.ini when it changed, and
     * every writable file that differs from what the real folder was last
     * seen to hold, slots first, a slot's old file kept there as backup 1.
     * Something a retry cannot fix (the real file changed) stops sending for
     * the session. The mark goes only once everything was sent.
     */
    static void push() {
        synchronized (LOCK) {
            if (sRoot == null) {
                return;
            }
            pushSettings();
            if (sStopped) {
                return;
            }
            Map<String, File> now = scan(sRoot, true);
            List<String> order = new ArrayList<>();
            for (String rel : now.keySet()) {
                order.add(rel);
            }
            for (String rel : sKnown.keySet()) {
                if (!now.containsKey(rel)) {
                    order.add(rel);
                }
            }
            order.sort((a, b) -> Boolean.compare(!SaveFolder.isSlot(a), !SaveFolder.isSlot(b)));

            int sent = 0;
            boolean complete = true;
            for (String rel : order) {
                String known = sKnown.containsKey(rel) ? sKnown.get(rel) : "";
                File f = now.get(rel);
                Bundle extras = new Bundle();
                extras.putBoolean("backup", SaveFolder.isSlot(rel));
                extras.putString("expect", known);
                String method;
                String sha;
                try {
                    if (f != null) {
                        byte[] data = readAll(f);
                        sha = SaveFolder.sha256(data);
                        if (sha.equals(known)) {
                            continue;
                        }
                        extras.putByteArray("data", data);
                        extras.putLong("mtime", f.lastModified());
                        method = "write";
                    } else {
                        sha = "";
                        method = "remove";
                    }
                } catch (IOException e) {
                    // Not sent: the mark stays, so the next start sends or keeps the copy.
                    Log.w(TAG, "coop saves: cannot read " + rel + " here", e);
                    complete = false;
                    continue;
                }

                try {
                    callRetrying(method, rel, extras);
                } catch (Refused r) {
                    if (r.error.equals("busy")) {
                        unsent(rel, r, true);
                    } else {
                        stop(rel, r);
                    }
                    return;
                } catch (IOException e) {
                    unsent(rel, e, false);
                    return;
                }
                if (sha.isEmpty()) {
                    sKnown.remove(rel);
                } else {
                    sKnown.put(rel, sha);
                }
                sent++;
            }
            if (sent > 0) {
                Log.i(TAG, "coop saves: sent " + sent + " changed file" + (sent == 1 ? "" : "s")
                        + " to SpongeBob Movie Port");
            }
            if (complete) {
                clearUnsent();
                sToldRetry = false;
            } else {
                markUnsent();
            }
        }
    }

    /** The normal app's process may be starting or ending: a few tries. */
    private static void callRetrying(String method, String rel, Bundle extras) throws IOException {
        IOException last = null;
        for (int i = 0; i < 3; i++) {
            try {
                call(sContext, method, rel, extras);
                return;
            } catch (Refused r) {
                if (!r.error.equals("busy")) {
                    throw r;
                }
                last = r;
            } catch (IOException e) {
                last = e;
            }
            try {
                Thread.sleep(300);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
        }
        throw last;
    }

    /** Not sent this time: the next change tries again, the next start sends or keeps it. */
    private static void unsent(String rel, IOException e, boolean busy) {
        Log.w(TAG, "coop saves: " + rel + " not sent yet: " + e.getMessage());
        markUnsent();
        if (!sToldRetry) {
            sToldRetry = true;
            GameActivity.coopToast(busy
                    ? "SpongeBob Movie Port is being opened, so co-op could not save there just "
                            + "now. Close it; co-op tries again at the next save and keeps this "
                            + "save meanwhile."
                    : "Co-op could not reach SpongeBob Movie Port to save there. It tries again "
                            + "at the next save; co-op keeps this save meanwhile.");
        }
    }

    private static void stop(String rel, Refused r) {
        Log.w(TAG, "coop saves: stopped sending: " + rel + ": " + r.getMessage());
        markUnsent();
        sStopped = true;
        GameActivity.coopToast("Co-op could not save into SpongeBob Movie Port: its save had "
                + "changed (" + rel + "). Co-op keeps your save, but sends nothing more until it "
                + "is opened again.");
    }

    // --- what could not be sent ----------------------------------------------------

    private static final String MARK_HEADER = "tssm-coop-unsent/1";

    private static File unsentMarker() {
        return new File(sContext.getFilesDir(), "tssm/coop-unsent");
    }

    /** The mark: its header, then each file co-op last saw in the real folder, with its SHA-256. */
    private static void markUnsent() {
        StringBuilder text = new StringBuilder(MARK_HEADER).append('\n');
        for (Map.Entry<String, String> e : new TreeMap<>(sKnown).entrySet()) {
            text.append(e.getKey()).append('\t').append(e.getValue()).append('\n');
        }
        String t = text.toString();
        if (t.equals(sMarked) && unsentMarker().isFile()) {
            return;
        }
        try {
            writeFile(unsentMarker(), t.getBytes(StandardCharsets.UTF_8));
            sMarked = t;
        } catch (IOException e) {
            Log.w(TAG, "coop saves: cannot mark the copy as not sent", e);
        }
    }

    private static void clearUnsent() {
        File marker = unsentMarker();
        if (marker.exists() && !marker.delete()) {
            Log.w(TAG, "coop saves: cannot clear the not-sent mark");
            return;
        }
        sMarked = null;
    }

    /** The mark's files and hashes; null when it cannot be read. */
    private static Map<String, String> readMark() {
        try {
            String text = new String(readAll(unsentMarker()), StandardCharsets.UTF_8);
            String[] lines = text.split("\n");
            if (lines.length == 0 || !lines[0].equals(MARK_HEADER)) {
                return null;
            }
            Map<String, String> out = new HashMap<>();
            for (int i = 1; i < lines.length; i++) {
                int tab = lines[i].indexOf('\t');
                if (tab < 0 || !SaveFolder.writable(lines[i].substring(0, tab))) {
                    return null;
                }
                out.put(lines[i].substring(0, tab), lines[i].substring(tab + 1));
            }
            return out;
        } catch (IOException e) {
            return null;
        }
    }

    /**
     * Last session's copy, when its changes cannot be sent because
     * SpongeBob Movie Port's saves changed since: into files/tssm/unsent/<time>/.
     */
    private static void keepUnsent(File root) throws IOException {
        String stamp = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.ROOT).format(new Date());
        File keep = new File(sContext.getFilesDir(), "tssm/unsent/" + stamp);
        for (int n = 2; keep.exists(); n++) {
            keep = new File(sContext.getFilesDir(), "tssm/unsent/" + stamp + "-" + n);
        }
        for (Map.Entry<String, File> e : scan(root, false).entrySet()) {
            writeFile(new File(keep, e.getKey()), readAll(e.getValue()));
        }
        clearUnsent();
        Log.w(TAG, "coop saves: last session's saves that were not sent are kept in " + keep);
        GameActivity.coopToast("The last co-op session's newest saves could not be sent: "
                + "SpongeBob Movie Port's saves had changed since. Co-op plays on those, and "
                + "keeps its own copy in its folder (unsent/" + keep.getName() + ").");
    }

    // --- files ---------------------------------------------------------------------

    /** As SaveFolder writes: temporary, flushed, renamed over. */
    private static void writeFile(File f, byte[] data) throws IOException {
        File dir = f.getParentFile();
        if (dir != null && !dir.isDirectory() && !dir.mkdirs()) {
            throw new IOException("cannot make " + dir);
        }
        File tmp = new File(f.getPath() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(data);
            out.flush();
            out.getFD().sync();
        }
        try {
            Os.rename(tmp.getPath(), f.getPath());
        } catch (ErrnoException e) {
            tmp.delete();
            throw new IOException("cannot write " + f, e);
        }
    }

    private static byte[] readAll(File f) throws IOException {
        long n = f.length();
        if (n > SaveFolder.MAX_FILE) {
            throw new IOException(f + " is too large");
        }
        try (InputStream in = new FileInputStream(f)) {
            byte[] data = new byte[(int) n];
            int got = 0;
            int k;
            while (got < data.length && (k = in.read(data, got, data.length - got)) != -1) {
                got += k;
            }
            if (got != data.length) {
                throw new IOException("cannot read " + f);
            }
            return data;
        }
    }

    // --- settings ------------------------------------------------------------------

    /** Co-op's config.ini while it is the normal app's; null when it is not shared. */
    private static File sConfig;
    /** SHA-256 of the normal app's config.ini as co-op last saw it ("" for none). */
    private static String sConfigKnown = "";
    private static boolean sConfigStopped;

    /**
     * At every start that shares the saves: co-op's config.ini becomes the
     * normal app's (none when it has none), and its changes go back with the
     * saves (push), only over the file it started from.
     */
    static void takeSettings(Context context, byte[] normalConfig) {
        File own = new File(context.getFilesDir(), "config.ini");
        synchronized (LOCK) {
            sConfig = null;
            try {
                if (normalConfig != null) {
                    writeFile(own, normalConfig);
                } else if (own.exists() && !own.delete()) {
                    throw new IOException("cannot remove " + own);
                }
                sConfig = own;
                sConfigKnown = normalConfig != null ? SaveFolder.sha256(normalConfig) : "";
                sConfigStopped = false;
                Log.i(TAG, "coop: the settings are SpongeBob Movie Port's config.ini");
            } catch (IOException e) {
                Log.w(TAG, "coop: could not take SpongeBob Movie Port's settings; co-op keeps "
                        + "its own this time", e);
            }
        }
    }

    /** config.ini to the normal app when it changed here (under LOCK). */
    private static void pushSettings() {
        if (sConfig == null || sConfigStopped || !sConfig.isFile()) {
            return;
        }
        byte[] data;
        try {
            data = readAll(sConfig);
        } catch (IOException e) {
            Log.w(TAG, "coop: cannot read config.ini here", e);
            return;
        }
        String sha = SaveFolder.sha256(data);
        if (sha.equals(sConfigKnown)) {
            return;
        }
        Bundle extras = new Bundle();
        extras.putByteArray("data", data);
        extras.putString("expect", sConfigKnown);
        try {
            callRetrying("config", null, extras);
            sConfigKnown = sha;
            Log.i(TAG, "coop: the settings went to SpongeBob Movie Port");
        } catch (Refused r) {
            if (r.error.equals("busy")) {
                Log.w(TAG, "coop: the settings were not sent yet: " + r.getMessage());
                return;
            }
            sConfigStopped = true;
            Log.w(TAG, "coop: stopped sending the settings: " + r.getMessage());
            GameActivity.coopToast("Co-op could not save its settings into SpongeBob Movie Port: "
                    + "they had changed there. Co-op keeps its own until it is opened again.");
        } catch (IOException e) {
            Log.w(TAG, "coop: the settings were not sent yet: " + e.getMessage());
        }
    }
}
