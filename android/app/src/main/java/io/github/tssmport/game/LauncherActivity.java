package io.github.tssmport.game;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.util.Log;

import java.io.File;

/**
 * The launcher entry of the co-op app. It plays on the saves and game files of
 * the normal app (SpongeBob Movie Port, io.github.tssmport.game): it asks that
 * app first (CoopSaves.probe), takes its game-files folder and its settings
 * (config.ini, which goes back with the saves), and says what is missing when it
 * cannot (CoopNoticeActivity). Then the import screen when the game's files
 * are not in place (or the "Import game files" shortcut was used), and the
 * game otherwise.
 *
 * <p>Based on treedome's ProfileActivity (CC0). TSSM has no video profile to
 * ask about: resolution and the rest are config.ini's, edited from the
 * in-game overlay.
 */
public class LauncherActivity extends Activity {

    private static final String TAG = "tssm";

    static final String PREFS = "tssm";
    static final String EXTRA_IMPORT = "import";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (getIntent().getBooleanExtra(EXTRA_IMPORT, false)) {
            startActivity(new Intent(this, ImportActivity.class));
            finish();
            return;
        }

        // Binder calls to the normal app, which may have to start: off the UI
        // thread.
        final Intent launch = getIntent();
        new Thread(() -> {
            final CoopSaves.Probe p = CoopSaves.probe(getApplicationContext());
            runOnUiThread(() -> decide(launch, p));
        }, "tssm-coop-probe").start();
    }

    private void decide(Intent launch, CoopSaves.Probe p) {
        if (isFinishing() || isDestroyed()) {
            return;
        }
        // A test launch plays on its own folder (TestLaunch) and never on the
        // shared saves, so nothing the normal app says stops it.
        final boolean test = !TestLaunch.settings(this, launch).isEmpty();
        if (p.problem != CoopSaves.Problem.NONE && !CoopSaves.noSaves() && !test) {
            Log.w(TAG, "coop: " + p.problem + (p.why != null ? " (" + p.why + ")" : ""));
            notice(launch, p.problem.name(), p.why, null);
            return;
        }

        if (p.problem == CoopSaves.Problem.NONE) {
            if (!test) {
                CoopSaves.clearNoSaves();
                CoopSaves.takeSettings(this, p.config);
            }

            // The normal app's game-files folder, read where it is: co-op never
            // keeps a copy of its own.
            if (p.assets != null && !p.assets.equals(ImportActivity.linkedPath(this))) {
                if (!ImportActivity.canReadStorage(this)) {
                    notice(launch, CoopNoticeActivity.FILES_ACCESS, null, p.assets);
                    return;
                }
                if (ImportActivity.containsGame(new File(p.assets))) {
                    getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                            .putString(ImportActivity.KEY_ASSETS_PATH, p.assets).apply();
                    Log.i(TAG, "coop: the game files of SpongeBob Movie Port: " + p.assets);
                }
            }
        }

        if (!ImportActivity.assetsPresent(this)) {
            startActivity(new Intent(this, ImportActivity.class));
            finish();
            return;
        }

        // A test launch's settings go on to the game (TestLaunch, which decides
        // whether they are taken); a normal launch has none.
        Intent game = new Intent(this, GameActivity.class);
        TestLaunch.forward(launch, game);
        startActivity(game);
        finish();
        overridePendingTransition(0, 0);
    }

    private void notice(Intent launch, String problem, String why, String assets) {
        Intent i = new Intent(this, CoopNoticeActivity.class);
        // Carried through the notice, so its relaunch is the same launch.
        TestLaunch.forward(launch, i);
        i.putExtra(CoopNoticeActivity.EXTRA_PROBLEM, problem);
        if (why != null) {
            i.putExtra(CoopNoticeActivity.EXTRA_WHY, why);
        }
        if (assets != null) {
            i.putExtra(CoopNoticeActivity.EXTRA_ASSETS, assets);
        }
        startActivity(i);
        finish();
    }
}
