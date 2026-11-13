package io.github.tssmport.game;

import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Arrays;
import java.util.List;

/**
 * SaveFolder's path checks and writes, on a desktop JVM. No test framework is
 * in the offline Gradle cache, so this is a plain program (docs/ANDROID.md):
 *
 * <pre>
 * javac -d out android/app/src/main/java/io/github/tssmport/game/SaveFolder.java \
 *       android/app/src/test/java/io/github/tssmport/game/SaveFolderTest.java
 * java -cp out io.github.tssmport.game.SaveFolderTest [scratch folder]
 * </pre>
 *
 * Exit status 0 when every check passed.
 */
public final class SaveFolderTest {

    private static int sChecks;
    private static int sFailed;

    private static void check(boolean ok, String what) {
        sChecks++;
        if (!ok) {
            sFailed++;
            System.out.println("FAIL: " + what);
        }
    }

    /** The desktop's atomic replace, which java.io's renameTo is not on Windows. */
    private static final SaveFolder.Fs FS = new SaveFolder.Fs() {
        @Override
        public boolean rename(File from, File to) {
            try {
                Files.move(from.toPath(), to.toPath(), StandardCopyOption.REPLACE_EXISTING,
                        StandardCopyOption.ATOMIC_MOVE);
                return true;
            } catch (IOException e) {
                return false;
            }
        }

        @Override
        public void syncDir(File dir) {
        }

        @Override
        public boolean isLink(File f) {
            return Files.isSymbolicLink(f.toPath());
        }
    };

    private static byte[] bytes(String s) {
        return s.getBytes(StandardCharsets.UTF_8);
    }

    private static String text(File f) throws IOException {
        return new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8);
    }

    private static void deleteTree(File f) {
        File[] kids = f.listFiles();
        if (kids != null && !Files.isSymbolicLink(f.toPath())) {
            for (File k : kids) {
                deleteTree(k);
            }
        }
        f.delete();
    }

    public static void main(String[] args) throws Exception {
        File base = args.length > 0 ? new File(args[0])
                : Files.createTempDirectory("savefoldertest").toFile();
        deleteTree(base);
        File root = new File(base, "files/tssm/saves");
        File outside = new File(base, "files");
        check(root.mkdirs(), "the scratch save folder is made");
        File config = new File(outside, "config.ini");
        Files.write(config.toPath(), bytes("[game]\n"));

        paths();
        escapes(root, base);
        writes(root);
        listing(root);
        claimKeys();

        check(text(config).equals("[game]\n"), "nothing outside the save folder changed");
        String[] top = outside.list();
        Arrays.sort(top);
        check(Arrays.equals(top, new String[] {"config.ini", "tssm"}),
                "nothing was made beside the save folder: " + Arrays.toString(top));

        configWrites(new File(base, "cfg"));

        deleteTree(base);
        System.out.println("SaveFolderTest: " + (sChecks - sFailed) + "/" + sChecks + " passed");
        System.exit(sFailed == 0 ? 0 : 1);
    }

    private static void paths() {
        String[] writable = {
            "SpongeBobMovie01", "SpongeBobMovie02", "SpongeBobMovie03",
            "SpongeBobMovie01.tga", "SpongeBobMovie03.gchead",
            "second/SpongeBobMovie02", "second/SpongeBobMovie02.tga",
            "more/2/SpongeBobMovie01", "more/9/SpongeBobMovie03.gchead",
            "backup/SpongeBobMovie01.damaged", "backup/SpongeBobMovie01.damaged9",
            "more/5/backup/SpongeBobMovie02.damaged3",
        };
        for (String p : writable) {
            check(SaveFolder.writable(p) && SaveFolder.readable(p), "writable: " + p);
        }

        String[] readOnly = {
            "backup/SpongeBobMovie01.1", "backup/SpongeBobMovie03.3",
            "second/backup/SpongeBobMovie02.2", "more/4/backup/SpongeBobMovie01.1",
        };
        for (String p : readOnly) {
            check(SaveFolder.readable(p) && !SaveFolder.writable(p), "read only: " + p);
        }

        String[] refused = {
            null, "", "/", ".", "..", "/SpongeBobMovie01", "../SpongeBobMovie01",
            "../config.ini", "config.ini", "../../config.ini", "SpongeBobMovie01/../../config.ini",
            "second/../SpongeBobMovie01", "./SpongeBobMovie01", "second//SpongeBobMovie01",
            "second\\SpongeBobMovie01", "..\\config.ini", "C:\\SpongeBobMovie01",
            "SpongeBobMovie00", "SpongeBobMovie04", "SpongeBobMovie1", "SpongeBobMovie001",
            "spongebobmovie01", "SPONGEBOBMOVIE01", "SpongeBobMovie01.tmp",
            "SpongeBobMovie01.tga.tmp", "SpongeBobMovie01 ", " SpongeBobMovie01",
            "SpongeBobMovie01\n", "SpongeBobMovie01\u0000", "SpongeBobMovie01/",
            "SpongeBobMovie01.TGA", "SpongeBobMovie01.png", "coop_files.cache",
            "more/1/SpongeBobMovie01", "more/10/SpongeBobMovie01", "more/02/SpongeBobMovie01",
            "more/SpongeBobMovie01", "third/SpongeBobMovie01", "second/second/SpongeBobMovie01",
            "backup/SpongeBobMovie01", "backup/SpongeBobMovie01.0", "backup/SpongeBobMovie01.4",
            "backup/SpongeBobMovie01.tga.1", "backup/SpongeBobMovie01.damaged1",
            "backup/SpongeBobMovie01.damaged10", "backup/backup/SpongeBobMovie01.1",
            "%2e%2e/config.ini", "SpongeBobMovie01%00", "SpongeBob\u2215Movie01",
            "SpongeBobMovie0\uff11",
        };
        for (String p : refused) {
            check(!SaveFolder.readable(p) && !SaveFolder.writable(p), "refused: " + p);
        }

        check(SaveFolder.isSlot("SpongeBobMovie01") && SaveFolder.isSlot("more/3/SpongeBobMovie02"),
                "slots are slots");
        check(!SaveFolder.isSlot("SpongeBobMovie01.tga")
                && !SaveFolder.isSlot("backup/SpongeBobMovie01.damaged"), "a still is not a slot");
    }

    private static void escapes(File root, File base) throws IOException {
        SaveFolder saves = new SaveFolder(root, FS);
        check(saves.resolve("../config.ini", false) == null, "resolve refuses ..");
        check(saves.resolve("SpongeBobMovie01", true) != null, "resolve takes a slot");
        check(saves.write("../config.ini", bytes("x"), false, "", 0).error.equals(SaveFolder.E_PATH),
                "write refuses ..");
        check(saves.remove("../config.ini", false, "").error.equals(SaveFolder.E_PATH),
                "remove refuses ..");
        try {
            saves.read("../config.ini");
            check(false, "read refuses ..");
        } catch (IOException e) {
            check(true, "read refuses ..");
        }

        // A link inside the folder that leads out of it.
        File away = new File(base, "away");
        away.mkdirs();
        File link = new File(root, "second");
        try {
            Files.createSymbolicLink(link.toPath(), away.toPath());
        } catch (IOException | UnsupportedOperationException | SecurityException e) {
            System.out.println("note: no symbolic link here (" + e + "); the link check is skipped");
            deleteTree(away);
            return;
        }
        check(saves.resolve("second/SpongeBobMovie01", true) == null, "resolve refuses a link");
        check(SaveFolder.E_PATH.equals(saves.write("second/SpongeBobMovie01", bytes("x"), false, "", 0)
                .error), "write refuses a link");
        String[] there = away.list();
        check(there != null && there.length == 0, "nothing was written through the link");
        Files.delete(link.toPath());

        // backup/ as a link: a save with a backup is refused before anything moves.
        File slotLinked = new File(root, "SpongeBobMovie02");
        Files.write(slotLinked.toPath(), bytes("keep"));
        Files.createSymbolicLink(new File(root, "backup").toPath(), away.toPath());
        check(SaveFolder.E_PATH.equals(saves.write("SpongeBobMovie02", bytes("y"), true,
                SaveFolder.sha256(bytes("keep")), 0).error), "a linked backup folder is refused");
        check(SaveFolder.E_PATH.equals(saves.remove("SpongeBobMovie02", true,
                SaveFolder.sha256(bytes("keep"))).error), "for a remove too");
        there = away.list();
        check(text(slotLinked).equals("keep") && there != null && there.length == 0,
                "and nothing moved");
        Files.delete(new File(root, "backup").toPath());
        Files.delete(slotLinked.toPath());
        deleteTree(away);
    }

    private static void writes(File root) throws IOException {
        SaveFolder saves = new SaveFolder(root, FS);
        File slot = new File(root, "SpongeBobMovie01");
        File bdir = new File(root, "backup");

        SaveFolder.Result r = saves.write("SpongeBobMovie01", bytes("v1"), true, "", 0);
        check(r.ok && r.sha256.equals(SaveFolder.sha256(bytes("v1"))), "a new slot is written");
        check(text(slot).equals("v1"), "it holds what was sent");
        check(!new File(root, "SpongeBobMovie01.tmp").exists(), "no temporary is left");
        check(!new File(bdir, "SpongeBobMovie01.1").exists(), "a new slot has no backup");

        r = saves.write("SpongeBobMovie01", bytes("v2"), true, "", 0);
        check(!r.ok && r.error.equals(SaveFolder.E_CHANGED), "a write expecting no file is refused");
        check(r.sha256.equals(SaveFolder.sha256(bytes("v1"))), "it is told what is there");
        check(text(slot).equals("v1"), "and nothing changed");

        String sha = SaveFolder.sha256(bytes("v1"));
        for (int v = 2; v <= 5; v++) {
            r = saves.write("SpongeBobMovie01", bytes("v" + v), true, sha, 0);
            check(r.ok, "save over v" + (v - 1));
            sha = r.sha256;
        }
        check(text(slot).equals("v5"), "the slot holds the newest");
        check(text(new File(bdir, "SpongeBobMovie01.1")).equals("v4"), "backup 1 is the one before");
        check(text(new File(bdir, "SpongeBobMovie01.2")).equals("v3"), "backup 2");
        check(text(new File(bdir, "SpongeBobMovie01.3")).equals("v2"), "backup 3");
        check(!new File(bdir, "SpongeBobMovie01.4").exists(), "only three backups are kept");
        String[] leftovers = bdir.list((d, n) -> n.endsWith(".tmp"));
        check(leftovers != null && leftovers.length == 0, "no temporary is left in backup/");

        r = saves.write("SpongeBobMovie01.tga", bytes("still"), false, "", 0);
        check(r.ok && !new File(bdir, "SpongeBobMovie01.tga.1").exists(), "a still has no backup");
        r = saves.write("SpongeBobMovie01.tga", bytes("still2"), true, SaveFolder.sha256(bytes("still")), 0);
        check(!r.ok && r.error.equals(SaveFolder.E_PATH), "backup is for slots only");

        r = saves.write("SpongeBobMovie02", bytes("x"), false, null, 0);
        check(!r.ok && r.error.equals(SaveFolder.E_ARGS), "a write without an expected hash is refused");
        r = saves.write("SpongeBobMovie02", null, false, "", 0);
        check(!r.ok && r.error.equals(SaveFolder.E_ARGS), "a write without data is refused");
        r = saves.write("SpongeBobMovie02", new byte[SaveFolder.MAX_FILE + 1], false, "", 0);
        check(!r.ok && r.error.equals(SaveFolder.E_SIZE), "a file over the limit is refused");
        check(!new File(root, "SpongeBobMovie02").exists(), "and none of those made a file");
        r = saves.write("SpongeBobMovie02", new byte[SaveFolder.MAX_FILE], false, "", 0);
        check(r.ok, "a file at the limit is taken");
        check(saves.remove("SpongeBobMovie02", false, r.sha256).ok, "and removed");

        r = saves.write("backup/SpongeBobMovie01.1", bytes("x"), false,
                SaveFolder.sha256(bytes("v4")), 0);
        check(!r.ok && r.error.equals(SaveFolder.E_PATH), "a backup is not written from outside");

        long when = 1500000000000L;
        r = saves.write("more/3/SpongeBobMovie03", bytes("c"), true, "", when);
        File deep = new File(root, "more/3/SpongeBobMovie03");
        check(r.ok && text(deep).equals("c"), "a slot in folder D is written, its folder made");
        check(Math.abs(deep.lastModified() - when) < 2000, "the time sent is kept");

        // Removing a slot keeps it as backup 1, as the Saves tab's Delete does.
        r = saves.remove("SpongeBobMovie01", true, SaveFolder.sha256(bytes("v4")));
        check(!r.ok && r.error.equals(SaveFolder.E_CHANGED), "a remove of another version is refused");
        check(slot.exists(), "and the slot is still there");
        r = saves.remove("SpongeBobMovie01", true, sha);
        check(r.ok && !slot.exists(), "a slot is removed");
        check(text(new File(bdir, "SpongeBobMovie01.1")).equals("v5"), "and kept as backup 1");
        check(text(new File(bdir, "SpongeBobMovie01.3")).equals("v3"), "the oldest backup goes");
        check(saves.remove("SpongeBobMovie01", true, "").ok, "removing what is gone is no error");
        check(!saves.remove("SpongeBobMovie01", true, sha).ok, "unless something else was expected");

        r = saves.write("SpongeBobMovie01", bytes("v6"), true, "", 0);
        check(r.ok && text(slot).equals("v6"), "a slot is written again");
        check(text(new File(bdir, "SpongeBobMovie01.1")).equals("v5"),
                "a write into an empty slot keeps the backups as they are");

        // A slot's name taken by a folder is never written over.
        File dirSlot = new File(root, "SpongeBobMovie03");
        dirSlot.mkdirs();
        r = saves.write("SpongeBobMovie03", bytes("x"), true, "", 0);
        check(!r.ok, "a folder in a slot's place is refused");
        r = saves.write("SpongeBobMovie03", bytes("x"), true, "?", 0);
        check(!r.ok && dirSlot.isDirectory(), "whatever hash is sent");
        dirSlot.delete();
    }

    private static void listing(File root) throws IOException {
        SaveFolder saves = new SaveFolder(root, FS);
        Files.write(new File(root, "coop_files.cache").toPath(), bytes("cache"));
        Files.write(new File(root, "SpongeBobMovie02.tmp").toPath(), bytes("half"));
        Files.write(new File(root, "notes.txt").toPath(), bytes("n"));
        new File(root, "second").mkdirs();
        Files.write(new File(root, "second/SpongeBobMovie02").toPath(), bytes("b2"));

        List<SaveFolder.Entry> list = saves.list();
        StringBuilder names = new StringBuilder();
        for (SaveFolder.Entry e : list) {
            names.append(e.path).append(' ');
            byte[] data = saves.read(e.path);
            check(data.length == e.size && SaveFolder.sha256(data).equals(e.sha256),
                    "listed size and hash match " + e.path);
        }
        String want = "SpongeBobMovie01 SpongeBobMovie01.tga backup/SpongeBobMovie01.1 "
                + "backup/SpongeBobMovie01.2 backup/SpongeBobMovie01.3 second/SpongeBobMovie02 "
                + "more/3/SpongeBobMovie03 ";
        check(names.toString().equals(want), "the list is the save files only: " + names);

        try {
            saves.read("SpongeBobMovie02.tmp");
            check(false, "a temporary cannot be read");
        } catch (IOException e) {
            check(true, "a temporary cannot be read");
        }
        try {
            saves.read("SpongeBobMovie02");
            check(false, "a missing file cannot be read");
        } catch (IOException e) {
            check(true, "a missing file cannot be read");
        }
    }

    private static void configWrites(File dir) throws IOException {
        check(dir.mkdirs(), "the scratch config folder is made");
        File config = new File(dir, "config.ini");

        SaveFolder.Result r = SaveFolder.writeConfig(config, bytes("[video]\n"), "x", FS);
        check(!r.ok && r.error.equals(SaveFolder.E_CHANGED) && !config.exists(),
                "a config write expecting a file that is not there is refused");
        r = SaveFolder.writeConfig(config, bytes("[video]\n"), "", FS);
        check(r.ok && r.sha256.equals(SaveFolder.sha256(bytes("[video]\n"))),
                "a new config.ini is written");
        check(text(config).equals("[video]\n"), "it holds what was sent");
        check(!new File(dir, "config.ini.tmp").exists(), "no temporary is left beside it");

        String sha = r.sha256;
        r = SaveFolder.writeConfig(config, bytes("[audio]\n"), "", FS);
        check(!r.ok && r.error.equals(SaveFolder.E_CHANGED) && r.sha256.equals(sha),
                "a write expecting no file is refused and told what is there");
        r = SaveFolder.writeConfig(config, bytes("[a]\0"), sha, FS);
        check(!r.ok && r.error.equals(SaveFolder.E_ARGS), "a config.ini with a NUL is refused");
        r = SaveFolder.writeConfig(config, new byte[SaveFolder.MAX_FILE + 1], sha, FS);
        check(!r.ok && r.error.equals(SaveFolder.E_SIZE), "a config.ini over the limit is refused");
        r = SaveFolder.writeConfig(config, null, sha, FS);
        check(!r.ok && r.error.equals(SaveFolder.E_ARGS), "a config write without data is refused");
        r = SaveFolder.writeConfig(config, bytes("[a]\n"), null, FS);
        check(!r.ok && r.error.equals(SaveFolder.E_ARGS), "a config write without a hash is refused");
        check(text(config).equals("[video]\n"), "and none of those changed it");

        r = SaveFolder.writeConfig(config, bytes("[video]\nwidth = 1\n"), sha, FS);
        check(r.ok && text(config).equals("[video]\nwidth = 1\n"),
                "a write over the file last seen replaces it");
        String[] left = dir.list();
        check(left != null && left.length == 1, "only config.ini is in its folder");

        File target = new File(dir, "elsewhere.ini");
        Files.write(target.toPath(), bytes("keep"));
        File linked = new File(dir, "linked.ini");
        try {
            Files.createSymbolicLink(linked.toPath(), target.toPath());
        } catch (IOException | UnsupportedOperationException | SecurityException e) {
            System.out.println("SKIP: no symbolic links here (" + e + "): the config link check");
            return;
        }
        r = SaveFolder.writeConfig(linked, bytes("x"), SaveFolder.sha256(bytes("keep")), FS);
        check(!r.ok && r.error.equals(SaveFolder.E_PATH) && text(target).equals("keep"),
                "a config.ini that is a link is refused");
    }

    private static void claimKeys() {
        // From iSaveStoreClaimFolder's loop, run over the same strings.
        check(SaveFolder.claimKey("/data/user/0/io.github.tssmport.game/files/tssm/saves")
                .equals("tssm-saves-9ebbc52a"), "the claim key of the phone's save folder");
        check(SaveFolder.claimKey("/data/data/io.github.tssmport.game/files/tssm/saves")
                .equals("tssm-saves-4124c84e"), "the claim key of the other spelling");
        check(SaveFolder.claimKey("/Data/USER/0/io.github.tssmport.game/files/tssm/saves/")
                .equals("tssm-saves-9ebbc52a"), "case and a trailing slash do not change it");
    }
}
