package io.github.tssmport.game;

import java.util.Locale;

/**
 * The player's words for what Android said about an update's install, and
 * for the checks made before it is asked. Updater shows them in the settings
 * window's Updates section (through the core, iUpdate.h).
 *
 * <p>No Android classes, so that a desktop JVM runs its test
 * (src/updateTest/java/.../UpdateNetTest.java). The status values are
 * PackageInstaller's public constants.
 */
final class UpdateText {

    static final int STATUS_PENDING_USER_ACTION = -1;
    static final int STATUS_SUCCESS = 0;
    static final int STATUS_FAILURE = 1;
    static final int STATUS_FAILURE_BLOCKED = 2;
    static final int STATUS_FAILURE_ABORTED = 3;
    static final int STATUS_FAILURE_INVALID = 4;
    static final int STATUS_FAILURE_CONFLICT = 5;
    static final int STATUS_FAILURE_STORAGE = 6;
    static final int STATUS_FAILURE_INCOMPATIBLE = 7;
    static final int STATUS_FAILURE_TIMEOUT = 8;

    static final String UNCHANGED = " Nothing was changed.";

    static final String DIFFERENT_KEY = "The update is signed with a different key from this "
            + "copy, so Android cannot install it over this copy." + UNCHANGED;

    private UpdateText() {
    }

    /** A failed install's status (anything but SUCCESS and PENDING_USER_ACTION). */
    static String installFailure(int status, String message) {
        String m = message != null ? message.toUpperCase(Locale.ROOT) : "";
        if (m.contains("INSTALL_FAILED_UPDATE_INCOMPATIBLE") || m.contains("SIGNATURES DO NOT MATCH")
                || m.contains("INCONSISTENT_CERTIFICATES")) {
            return DIFFERENT_KEY;
        }
        if (m.contains("INSTALL_FAILED_VERSION_DOWNGRADE")) {
            return downgrade(-1, -1);
        }
        switch (status) {
            case STATUS_FAILURE_ABORTED:
                return "The update was not installed: it was cancelled in Android's window."
                        + UNCHANGED;
            case STATUS_FAILURE_BLOCKED:
                return "Android blocked the update (a device policy or a security check)."
                        + UNCHANGED;
            case STATUS_FAILURE_CONFLICT:
                return "Android refused the update because it conflicts with the installed app."
                        + UNCHANGED;
            case STATUS_FAILURE_INCOMPATIBLE:
                return "Android says the update does not work on this device." + UNCHANGED;
            case STATUS_FAILURE_INVALID:
                return "Android says the downloaded app is damaged." + UNCHANGED;
            case STATUS_FAILURE_STORAGE:
                return "There is not enough free storage to install the update. Free some space, "
                        + "then try again." + UNCHANGED;
            case STATUS_FAILURE_TIMEOUT:
                return "Android took too long to install the update." + UNCHANGED;
            default:
                return "Android could not install the update." + UNCHANGED;
        }
    }

    /**
     * The download's build number (Android's version code) is lower than the
     * installed app's. -1: not known.
     */
    static String downgrade(long download, long installed) {
        if (download < 0 || installed < 0) {
            return "The update's build number is lower than this copy's, so Android refuses it "
                    + "as an older version." + UNCHANGED;
        }
        return "The update's build number (" + download + ") is lower than this copy's ("
                + installed + "), so Android refuses it as an older version." + UNCHANGED;
    }
}
