package io.github.tssmport.game;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;

/**
 * The save folder as SaveShareProvider hands it to the co-op app: only the
 * save store's own file names, and every write made the way the game makes it
 * (iSaveStoreWriteFile and iSS_backup in src/SB/Core/pc/iSaveStore.cpp), so a
 * write cut short leaves what the game already repairs at its next start.
 *
 * <p>No Android classes, so that it runs as a plain JVM test
 * (src/test/java/.../SaveFolderTest.java).
 */
final class SaveFolder {

    /** What java.io cannot do on API 24: replace a file atomically, flush a folder, see a link. */
    interface Fs {
        boolean rename(File from, File to);

        void syncDir(File dir);

        boolean isLink(File f);
    }

    /** Larger than any save, still or card header (a still is 192 KB at most). */
    static final int MAX_FILE = 256 * 1024;

    /** ISS_BACKUPS. */
    static final int BACKUPS = 3;

    static final String E_ARGS = "args";
    static final String E_PATH = "path";
    static final String E_SIZE = "size";
    static final String E_CHANGED = "changed";
    static final String E_IO = "io";

    // Folders A, B and C..J (iSaveStoreTargetDir), and a slot's file name.
    private static final String DIR = "(?:|second/|more/[2-9]/)";
    private static final String NAME = "SpongeBobMovie0[1-3]";

    // What the co-op app may write: a slot, its still and card header, and the
    // copy of a damaged save its repair keeps (iSS_keepDamaged).
    private static final Pattern WRITABLE = Pattern.compile(
            DIR + "(?:" + NAME + "(?:\\.tga|\\.gchead)?|backup/" + NAME + "\\.damaged[2-9]?)");

    // What it may read: the same and the backups, which only this side writes.
    private static final Pattern READABLE = Pattern.compile(
            DIR + "(?:" + NAME + "(?:\\.tga|\\.gchead)?|backup/" + NAME
                    + "\\.(?:[1-3]|damaged[2-9]?))");

    private static final Pattern SLOT = Pattern.compile(DIR + NAME);

    static final class Entry {
        final String path;
        final long size;
        final long mtime;
        final String sha256;

        Entry(String path, long size, long mtime, String sha256) {
            this.path = path;
            this.size = size;
            this.mtime = mtime;
            this.sha256 = sha256;
        }
    }

    /** ok, or an error code (E_*) and what went wrong; sha256 is the file's after the call. */
    static final class Result {
        final boolean ok;
        final String error;
        final String message;
        final String sha256;

        private Result(boolean ok, String error, String message, String sha256) {
            this.ok = ok;
            this.error = error;
            this.message = message;
            this.sha256 = sha256;
        }

        static Result done(String sha256) {
            return new Result(true, null, null, sha256);
        }

        static Result fail(String error, String message, String sha256) {
            return new Result(false, error, message, sha256);
        }
    }

    private final File root;
    private final Fs fs;

    SaveFolder(File root, Fs fs) {
        this.root = root;
        this.fs = fs;
    }

    static boolean readable(String rel) {
        return rel != null && READABLE.matcher(rel).matches();
    }

    static boolean writable(String rel) {
        return rel != null && WRITABLE.matcher(rel).matches();
    }

    static boolean isSlot(String rel) {
        return rel != null && SLOT.matcher(rel).matches();
    }

    /**
     * `rel` in the folder, or null when it is not one of the names above or
     * does not resolve to exactly that place: a link anywhere below the
     * folder is refused, whether or not getCanonicalFile follows it.
     */
    File resolve(String rel, boolean write) {
        if (!(write ? writable(rel) : readable(rel))) {
            return null;
        }
        try {
            File base = root.getCanonicalFile();
            File plain = new File(base, rel);
            File canonical = plain.getCanonicalFile();
            String inside = base.getPath() + File.separator;
            if (!canonical.getPath().equals(plain.getPath())
                    || !canonical.getPath().startsWith(inside)) {
                return null;
            }
            File step = base;
            for (String part : rel.split("/")) {
                step = new File(step, part);
                if (fs.isLink(step)) {
                    return null;
                }
            }
            return canonical;
        } catch (IOException e) {
            return null;
        }
    }

    /** Every file the co-op app may read, with its size, time and SHA-256. */
    List<Entry> list() {
        List<Entry> out = new ArrayList<>();
        List<String> dirs = new ArrayList<>();
        dirs.add("");
        dirs.add("second/");
        for (int i = 2; i <= 9; i++) {
            dirs.add("more/" + i + "/");
        }
        for (String dir : dirs) {
            listInto(out, dir);
            listInto(out, dir + "backup/");
        }
        return out;
    }

    private void listInto(List<Entry> out, String dir) {
        String[] names = new File(root, dir).list();
        if (names == null) {
            return;
        }
        Arrays.sort(names);
        for (String name : names) {
            String rel = dir + name;
            File f = resolve(rel, false);
            if (f == null || !f.isFile() || f.length() > MAX_FILE) {
                continue;
            }
            try {
                out.add(new Entry(rel, f.length(), f.lastModified(), sha256(readAll(f))));
            } catch (IOException e) {
                // Gone or unreadable between list() and here: not listed.
            }
        }
    }

    byte[] read(String rel) throws IOException {
        File f = resolve(rel, false);
        if (f == null) {
            throw new IOException(E_PATH);
        }
        if (!f.isFile() || f.length() > MAX_FILE) {
            throw new IOException("no file " + rel);
        }
        return readAll(f);
    }

    /**
     * iSaveStoreWriteFile: `data` to `rel` through `rel`.tmp and a rename;
     * with `backup` (a slot only), what was there becomes backup .1 first.
     * Nothing is written unless the file now there has the SHA-256 `expect`
     * ("" for no file). `mtime` (ms, 0 for now) is set afterwards, as
     * iSaveStoreRestore keeps the backup's time.
     */
    Result write(String rel, byte[] data, boolean backup, String expect, long mtime) {
        if (data == null || expect == null) {
            return Result.fail(E_ARGS, "no data or no expected hash", null);
        }
        File f = resolve(rel, true);
        if (f == null || (backup && (!isSlot(rel) || fs.isLink(backupDir(f))))) {
            return Result.fail(E_PATH, "not a save file: " + rel, null);
        }
        if (data.length > MAX_FILE) {
            return Result.fail(E_SIZE, data.length + " bytes is too large", null);
        }
        String current = current(f);
        if (current.equals(UNREADABLE)) {
            return Result.fail(E_IO, "cannot read " + rel, null);
        }
        if (!current.equals(expect)) {
            return Result.fail(E_CHANGED, rel + " is not the file the co-op app last saw", current);
        }

        File dir = f.getParentFile();
        if (!dir.isDirectory() && !dir.mkdirs()) {
            return Result.fail(E_IO, "cannot make the folder " + dir, current);
        }
        if (backup) {
            backup(f);
        }
        if (!writeFile(f, data)) {
            return Result.fail(E_IO, "cannot write " + rel, current(f));
        }
        if (mtime > 0) {
            f.setLastModified(mtime);
        }
        return Result.done(sha256(data));
    }

    /**
     * Removes `rel`, as iSaveStoreDelete does with `backup` (a slot only: the
     * save becomes backup .1). Only when the file there has the SHA-256
     * `expect`; "" when the file is already gone is not an error.
     */
    Result remove(String rel, boolean backup, String expect) {
        if (expect == null) {
            return Result.fail(E_ARGS, "no expected hash", null);
        }
        File f = resolve(rel, true);
        if (f == null || (backup && (!isSlot(rel) || fs.isLink(backupDir(f))))) {
            return Result.fail(E_PATH, "not a save file: " + rel, null);
        }
        String current = current(f);
        if (current.equals(UNREADABLE)) {
            return Result.fail(E_IO, "cannot read " + rel, null);
        }
        if (!current.equals(expect)) {
            return Result.fail(E_CHANGED, rel + " is not the file the co-op app last saw", current);
        }
        if (current.isEmpty()) {
            return Result.done("");
        }
        if (backup) {
            backup(f);
        }
        if (!f.delete()) {
            return Result.fail(E_IO, "cannot remove " + rel, current(f));
        }
        fs.syncDir(f.getParentFile());
        return Result.done("");
    }

    /**
     * config.ini replaced whole (the "config" call), as iConfigEditSave
     * writes it: through a temporary and a rename, with the saves' flushes.
     * Only text, and only over the file with the SHA-256 `expect` ("" for
     * none). No backup: the game keeps none of its settings.
     */
    static Result writeConfig(File config, byte[] data, String expect, Fs fs) {
        if (data == null || expect == null) {
            return Result.fail(E_ARGS, "no data or no expected hash", null);
        }
        if (data.length > MAX_FILE) {
            return Result.fail(E_SIZE, data.length + " bytes is too large", null);
        }
        for (byte b : data) {
            if (b == 0) {
                return Result.fail(E_ARGS, "config.ini is text", null);
            }
        }
        if (fs.isLink(config)) {
            return Result.fail(E_PATH, "config.ini is a link", null);
        }
        String current = current(config);
        if (current.equals(UNREADABLE)) {
            return Result.fail(E_IO, "cannot read config.ini", null);
        }
        if (!current.equals(expect)) {
            return Result.fail(E_CHANGED, "config.ini is not the file the co-op app last saw",
                    current);
        }
        if (!writeFile(config, data, fs)) {
            return Result.fail(E_IO, "cannot write config.ini", current(config));
        }
        return Result.done(sha256(data));
    }

    private static File backupDir(File f) {
        return new File(f.getParentFile(), "backup");
    }

    /** iSS_backup: .2 to .3, .1 to .2, then a copy (not a move) of the file to .1. */
    private void backup(File f) {
        if (!f.isFile()) {
            return;
        }
        File bdir = backupDir(f);
        bdir.mkdirs();
        String name = f.getName();

        new File(bdir, name + "." + BACKUPS).delete();
        for (int i = BACKUPS - 1; i >= 1; i--) {
            File newer = new File(bdir, name + "." + i);
            if (newer.exists()) {
                fs.rename(newer, new File(bdir, name + "." + (i + 1)));
            }
        }

        try {
            writeFile(new File(bdir, name + ".1"), readAll(f));
        } catch (IOException e) {
            // As iSS_backup: the save itself is still written.
        }
    }

    private boolean writeFile(File f, byte[] data) {
        return writeFile(f, data, fs);
    }

    /** iSaveStoreWriteFile without the backup: temporary, flush to disk, rename, flush the folder. */
    private static boolean writeFile(File f, byte[] data, Fs fs) {
        File tmp = new File(f.getPath() + ".tmp");
        boolean ok;
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(data);
            out.flush();
            out.getFD().sync();
            ok = true;
        } catch (IOException e) {
            ok = false;
        }
        if (!ok || !fs.rename(tmp, f)) {
            tmp.delete();
            return false;
        }
        fs.syncDir(f.getParentFile());
        return true;
    }

    private static final String UNREADABLE = "?";

    /** The SHA-256 of what is at `f`, "" for nothing, UNREADABLE for something that is not a readable file. */
    private static String current(File f) {
        if (!f.exists()) {
            return "";
        }
        if (!f.isFile()) {
            return UNREADABLE;
        }
        try (InputStream in = new FileInputStream(f)) {
            MessageDigest md = MessageDigest.getInstance("SHA-256");
            byte[] buffer = new byte[65536];
            int n;
            while ((n = in.read(buffer)) != -1) {
                md.update(buffer, 0, n);
            }
            return hex(md.digest());
        } catch (IOException | NoSuchAlgorithmException e) {
            return UNREADABLE;
        }
    }

    private static byte[] readAll(File f) throws IOException {
        try (InputStream in = new FileInputStream(f)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[65536];
            int n;
            while ((n = in.read(buffer)) != -1) {
                out.write(buffer, 0, n);
                if (out.size() > MAX_FILE) {
                    throw new IOException(f + " is too large");
                }
            }
            return out.toByteArray();
        }
    }

    static String sha256(byte[] data) {
        try {
            return hex(MessageDigest.getInstance("SHA-256").digest(data));
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }

    private static String hex(byte[] b) {
        StringBuilder s = new StringBuilder(b.length * 2);
        for (byte x : b) {
            s.append(Character.forDigit((x >> 4) & 0xf, 16)).append(Character.forDigit(x & 0xf, 16));
        }
        return s.toString();
    }

    /**
     * The one-copy lock's name for the save folder at `path`, as
     * iSaveStoreClaimFolder makes it: FNV-1a over the path with A-Z folded to
     * lower case. The game's claim and the co-op app's must name one file.
     */
    static String claimKey(String path) {
        String p = path.replace('\\', '/');
        while (p.length() > 1 && p.endsWith("/")) {
            p = p.substring(0, p.length() - 1);
        }
        int hash = 0x811c9dc5;
        for (byte b : p.getBytes(StandardCharsets.UTF_8)) {
            int c = b & 0xff;
            if (c >= 'A' && c <= 'Z') {
                c += 'a' - 'A';
            }
            hash = (hash ^ c) * 16777619;
        }
        return String.format(Locale.ROOT, "tssm-saves-%08x", hash);
    }
}
