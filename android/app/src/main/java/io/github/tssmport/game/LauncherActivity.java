package io.github.tssmport.game;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/**
 * The launcher entry. Sends the player to the import screen when the game's
 * files are not in place (or the "Import game files" shortcut was used), and
 * starts the game otherwise.
 *
 * <p>Based on treedome's ProfileActivity (CC0). TSSM has no video profile to
 * ask about: resolution and the rest are config.ini's, edited from the
 * in-game overlay.
 */
public class LauncherActivity extends Activity {

    static final String PREFS = "tssm";
    static final String EXTRA_IMPORT = "import";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        if (getIntent().getBooleanExtra(EXTRA_IMPORT, false) || !ImportActivity.assetsPresent(this)) {
            startActivity(new Intent(this, ImportActivity.class));
            finish();
            return;
        }

        // A test launch's settings go on to the game (TestLaunch, which decides
        // whether they are taken); a normal launch has none.
        Intent game = new Intent(this, GameActivity.class);
        TestLaunch.forward(getIntent(), game);
        startActivity(game);
        finish();
        overridePendingTransition(0, 0);
    }
}
