package io.github.tssmport.game;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * Why co-op cannot start with the normal app's saves or game files, and what
 * to do: install or update SpongeBob Movie Port, reinstall co-op, or allow All
 * files access for the normal app's game-files folder. Nothing here changes a
 * save. LauncherActivity decides when it shows.
 */
public class CoopNoticeActivity extends Activity {

    static final String EXTRA_PROBLEM = "problem";
    static final String EXTRA_WHY = "why";
    static final String EXTRA_ASSETS = "assets";

    /** Not a CoopSaves.Problem: the saves are fine, the game files need a permission. */
    static final String FILES_ACCESS = "FILES_ACCESS";

    private String mProblem;
    private String mAssets;
    private boolean mAwaitingPermission;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mProblem = getIntent().getStringExtra(EXTRA_PROBLEM);
        mAssets = getIntent().getStringExtra(EXTRA_ASSETS);
        String why = getIntent().getStringExtra(EXTRA_WHY);
        if (mProblem == null) {
            finish();
            return;
        }

        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText(R.string.app_name);
        title.setTextSize(24);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        TextView text = new TextView(this);
        text.setTextSize(16);
        text.setGravity(Gravity.CENTER);
        text.setPadding(0, pad, 0, pad);
        // The screen's width, so a long folder path wraps.
        layout.addView(text, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        boolean offerNoSaves = true;
        boolean offerRetry = false;
        switch (mProblem) {
            case "NO_APP":
            case "OLD_APP":
                text.setText(R.string.coop_no_app);
                break;
            case "OTHER_KEY":
                text.setText(R.string.coop_other_key);
                break;
            case "NO_PERMISSION":
                text.setText(R.string.coop_no_permission);
                break;
            case FILES_ACCESS:
                text.setText(getString(R.string.coop_files_access, mAssets));
                offerNoSaves = false;
                break;
            default:
                text.setText(getString(R.string.coop_unreachable, why != null ? why : "?"));
                offerRetry = true;
                break;
        }

        if (mProblem.equals(FILES_ACCESS)) {
            Button allow = new Button(this);
            allow.setText(R.string.coop_allow);
            allow.setOnClickListener(v -> askFilesAccess());
            layout.addView(allow);
        }
        if (offerRetry) {
            Button retry = new Button(this);
            retry.setText(R.string.coop_retry);
            retry.setOnClickListener(v -> relaunch());
            layout.addView(retry);
        }
        if (offerNoSaves) {
            Button noSaves = new Button(this);
            noSaves.setText(R.string.coop_play_without_saving);
            noSaves.setOnClickListener(v -> {
                CoopSaves.chooseNoSaves();
                relaunch();
            });
            layout.addView(noSaves);
        }
        Button close = new Button(this);
        close.setText(R.string.coop_close);
        close.setOnClickListener(v -> finish());
        layout.addView(close);

        // Edge to edge (target SDK 35): kept off the system bars and the
        // camera cutout, and scrollable on a short landscape screen.
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.addView(layout, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        scroll.setOnApplyWindowInsetsListener((v, insets) -> {
            int l, t, r, b;
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets i = insets.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                l = i.left;
                t = i.top;
                r = i.right;
                b = i.bottom;
            } else {
                l = insets.getSystemWindowInsetLeft();
                t = insets.getSystemWindowInsetTop();
                r = insets.getSystemWindowInsetRight();
                b = insets.getSystemWindowInsetBottom();
            }
            v.setPadding(l, t, r, b);
            return insets;
        });
        setContentView(scroll);
    }

    private void askFilesAccess() {
        mAwaitingPermission = true;
        if (Build.VERSION.SDK_INT >= 30) {
            try {
                startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                        Uri.parse("package:" + getPackageName())));
            } catch (ActivityNotFoundException e) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        } else {
            requestPermissions(new String[] { android.Manifest.permission.READ_EXTERNAL_STORAGE }, 1);
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!mAwaitingPermission) {
            return;
        }
        mAwaitingPermission = false;
        boolean granted = Build.VERSION.SDK_INT >= 30 ? Environment.isExternalStorageManager()
                : checkSelfPermission(android.Manifest.permission.READ_EXTERNAL_STORAGE)
                        == android.content.pm.PackageManager.PERMISSION_GRANTED;
        if (granted) {
            relaunch();
        }
    }

    /** The launcher again, with the launch's TSSM_ settings (a test launch stays one). */
    private void relaunch() {
        Intent i = new Intent(this, LauncherActivity.class);
        TestLaunch.forward(getIntent(), i);
        startActivity(i);
        finish();
    }

    @Override
    public void onBackPressed() {
        finish();
    }
}
