package org.f15se2.ex;

import java.io.File;
import org.libsdl.app.SDLActivity;

/**
 * Supplies SDL with the private external game-data directory.
 *
 * Copyrighted original assets are deliberately not packaged in the APK. They
 * can be installed for development with:
 * adb push GAME_DIRECTORY/. /sdcard/Android/data/org.f15se2.ex/files/game/
 */
public final class MainActivity extends SDLActivity {
    @Override
    protected String[] getArguments() {
        File gameDirectory = new File(getExternalFilesDir(null), "game");
        gameDirectory.mkdirs();
        return new String[] {"--game", gameDirectory.getAbsolutePath()};
    }
}
