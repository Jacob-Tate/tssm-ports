package io.github.tssmport.game;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Binder;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.os.Process;
import android.system.ErrnoException;
import android.system.Os;
import android.system.OsConstants;

import java.io.File;
import java.io.FileDescriptor;
import java.io.FileNotFoundException;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;

/**
 * The saves, settings and one-copy lock this app shares with the co-op app,
 * which installs beside it under its own package and the same signing key
 * (docs/ANDROID.md, "Saves shared with the co-op app").
 *
 * <p>Only an app signed with this app's key gets in. The manifest guards
 * openFile with a signature permission; call() is not guarded by the manifest
 * at all, so every entry point checks the caller itself.
 *
 * <p>The saves stay where they are. The co-op app plays on a copy and sends
 * each change back through write and remove, which change the real folder the
 * way the game does (SaveFolder), and only when the file there is the one the
 * co-op app last saw. config.ini goes back the same way, through config.
 */
public final class SaveShareProvider extends ContentProvider {

    static final String AUTHORITY = "io.github.tssmport.game.saves";
    static final String PERMISSION = "io.github.tssmport.permission.SHARED_SAVES";

    /** Raised when what call() takes or gives changes. */
    static final int API = 1;

    private static final Object LOCK = new Object();
    private static boolean sGameStarted;

    /**
     * GameActivity, before the game starts. From then on this process belongs
     * to the game, whose claim on the saves lasts until the process ends, so
     * write and remove are refused here. The claim lock is the real guard;
     * this one holds even if the co-op app did not take the lock first.
     */
    static void gameStarting() {
        synchronized (LOCK) {
            sGameStarted = true;
        }
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    private void checkCaller() {
        Context context = getContext();
        int uid = Binder.getCallingUid();
        if (context == null
                || context.checkCallingPermission(PERMISSION) != PackageManager.PERMISSION_GRANTED
                || context.getPackageManager().checkSignatures(Process.myUid(), uid)
                        != PackageManager.SIGNATURE_MATCH) {
            throw new SecurityException("uid " + uid + " is not signed with this app's key");
        }
    }

    private File filesDir() throws IOException {
        // Canonical, as SDL_GetAndroidInternalStoragePath gives the game, so
        // that both name the same lock file.
        return getContext().getFilesDir().getCanonicalFile();
    }

    private File savesDir() throws IOException {
        return new File(filesDir(), "tssm/saves");
    }

    private static final SaveFolder.Fs FS = new SaveFolder.Fs() {
        @Override
        public boolean rename(File from, File to) {
            try {
                Os.rename(from.getPath(), to.getPath());
                return true;
            } catch (ErrnoException e) {
                return false;
            }
        }

        @Override
        public void syncDir(File dir) {
            try {
                FileDescriptor fd = Os.open(dir.getPath(), OsConstants.O_RDONLY, 0);
                try {
                    Os.fsync(fd);
                } finally {
                    Os.close(fd);
                }
            } catch (ErrnoException e) {
                // As iHostFlushDir: the write itself has been made.
            }
        }

        @Override
        public boolean isLink(File f) {
            try {
                return OsConstants.S_ISLNK(Os.lstat(f.getPath()).st_mode);
            } catch (ErrnoException e) {
                return false; // not there yet
            }
        }
    };

    @Override
    public Bundle call(String method, String arg, Bundle extras) {
        checkCaller();
        Bundle out = new Bundle();
        out.putInt("api", API);
        Bundle in = extras != null ? extras : Bundle.EMPTY;
        try {
            SaveFolder saves = new SaveFolder(savesDir(), FS);
            switch (method != null ? method : "") {
                case "hello":
                    synchronized (LOCK) {
                        out.putBoolean("gameStarted", sGameStarted);
                    }
                    out.putBoolean("ok", true);
                    break;
                case "list":
                    putList(out, saves.list());
                    out.putBoolean("ok", true);
                    break;
                case "read":
                    out.putByteArray("data", saves.read(arg));
                    out.putBoolean("ok", true);
                    break;
                case "write":
                case "remove":
                    synchronized (LOCK) {
                        if (sGameStarted) {
                            fail(out, "busy", "the game is running");
                            break;
                        }
                        SaveFolder.Result r = method.equals("write")
                                ? saves.write(arg, in.getByteArray("data"), in.getBoolean("backup"),
                                        in.getString("expect"), in.getLong("mtime"))
                                : saves.remove(arg, in.getBoolean("backup"), in.getString("expect"));
                        out.putBoolean("ok", r.ok);
                        if (!r.ok) {
                            fail(out, r.error, r.message);
                        }
                        if (r.sha256 != null) {
                            out.putString("sha256", r.sha256);
                        }
                    }
                    break;
                case "config":
                    synchronized (LOCK) {
                        if (sGameStarted) {
                            fail(out, "busy", "the game is running");
                            break;
                        }
                        SaveFolder.Result r = SaveFolder.writeConfig(
                                new File(filesDir(), "config.ini"), in.getByteArray("data"),
                                in.getString("expect"), FS);
                        out.putBoolean("ok", r.ok);
                        if (!r.ok) {
                            fail(out, r.error, r.message);
                        }
                        if (r.sha256 != null) {
                            out.putString("sha256", r.sha256);
                        }
                    }
                    break;
                case "settings":
                    File config = new File(filesDir(), "config.ini");
                    if (config.isFile() && config.length() <= SaveFolder.MAX_FILE) {
                        out.putByteArray("config", readAll(config));
                    }
                    String linked = ImportActivity.linkedPath(getContext());
                    if (linked != null) {
                        out.putString("assets", linked);
                    }
                    out.putBoolean("ok", true);
                    break;
                default:
                    fail(out, SaveFolder.E_ARGS, "no such call");
                    break;
            }
        } catch (IOException | RuntimeException e) {
            out = new Bundle();
            out.putInt("api", API);
            fail(out, SaveFolder.E_IO, e.getMessage() != null ? e.getMessage() : e.toString());
        }
        return out;
    }

    private static void putList(Bundle out, List<SaveFolder.Entry> entries) {
        int n = entries.size();
        String[] paths = new String[n];
        long[] sizes = new long[n];
        long[] mtimes = new long[n];
        String[] hashes = new String[n];
        for (int i = 0; i < n; i++) {
            SaveFolder.Entry e = entries.get(i);
            paths[i] = e.path;
            sizes[i] = e.size;
            mtimes[i] = e.mtime;
            hashes[i] = e.sha256;
        }
        out.putStringArray("paths", paths);
        out.putLongArray("sizes", sizes);
        out.putLongArray("mtimes", mtimes);
        out.putStringArray("sha256", hashes);
    }

    private static void fail(Bundle out, String error, String message) {
        out.putBoolean("ok", false);
        out.putString("error", error);
        out.putString("message", message);
    }

    private static byte[] readAll(File f) throws IOException {
        try (InputStream in = new FileInputStream(f)) {
            byte[] data = new byte[(int) f.length()];
            int got = 0;
            int n;
            while (got < data.length && (n = in.read(data, got, data.length - got)) != -1) {
                got += n;
            }
            if (got != data.length) {
                throw new IOException("cannot read " + f.getName());
            }
            return data;
        }
    }

    /**
     * content://AUTHORITY/claim, "rw": the save folder's one-copy lock file,
     * the one the game's iSaveStoreClaimFolder takes. The co-op app flock()s
     * the descriptor and keeps it while it runs, so the lock ends with the
     * co-op process, whatever happens to this one. Nothing else is opened.
     */
    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        checkCaller();
        List<String> segments = uri.getPathSegments();
        if (!"rw".equals(mode) || segments.size() != 1 || !"claim".equals(segments.get(0))) {
            throw new FileNotFoundException("only the claim can be opened, read-write");
        }
        File lock;
        try {
            File locks = new File(filesDir(), "locks");
            if (!locks.isDirectory() && !locks.mkdirs()) {
                throw new FileNotFoundException("cannot make " + locks);
            }
            lock = new File(locks, SaveFolder.claimKey(savesDir().getPath()) + ".lock");
        } catch (IOException e) {
            throw new FileNotFoundException(e.toString());
        }
        return ParcelFileDescriptor.open(lock,
                ParcelFileDescriptor.MODE_READ_WRITE | ParcelFileDescriptor.MODE_CREATE);
    }

    // Nothing is offered as rows.

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs,
            String sortOrder) {
        checkCaller();
        return null;
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        checkCaller();
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        checkCaller();
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        checkCaller();
        return 0;
    }
}
