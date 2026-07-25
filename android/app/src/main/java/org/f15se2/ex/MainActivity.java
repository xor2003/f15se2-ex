package org.f15se2.ex;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import org.libsdl.app.SDLActivity;

/**
 * Supplies SDL with the private external game-data directory.
 *
 * Copyrighted original assets are deliberately not packaged in the APK. They
 * can be installed for development with:
 * adb push GAME_DIRECTORY/. /sdcard/Android/data/org.f15se2.ex/files/game/
 */
public final class MainActivity extends SDLActivity {
    /**
     * Recursively copies an APK asset into ordinary files because the legacy
     * loaders require seekable filesystem paths rather than AssetManager streams.
     */
    private void copyAssetTree(String assetPath, File destination) throws IOException {
        String[] children = getAssets().list(assetPath);
        if (children != null && children.length != 0) {
            if (!destination.isDirectory() && !destination.mkdirs()) {
                throw new IOException("Cannot create " + destination);
            }
            for (String child : children) {
                copyAssetTree(assetPath + "/" + child, new File(destination, child));
            }
            return;
        }

        File parent = destination.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) {
            throw new IOException("Cannot create " + parent);
        }
        try (InputStream input = getAssets().open(assetPath);
             FileOutputStream output = new FileOutputStream(destination)) {
            byte[] buffer = new byte[64 * 1024];
            int count;
            while ((count = input.read(buffer)) >= 0) {
                output.write(buffer, 0, count);
            }
        }
    }

    /**
     * Extracts an optional testing bundle once per installed APK revision.
     * Reinstalling a rebuilt APK refreshes the files without slowing every launch.
     */
    private void extractBundledGame(File gameDirectory) {
        try {
            long installed = getPackageManager()
                .getPackageInfo(getPackageName(), 0).lastUpdateTime;
            File marker = new File(gameDirectory, ".apk-assets-" + installed);
            if (marker.isFile()) {
                return;
            }
            String[] bundled = getAssets().list("game");
            if (bundled == null || bundled.length == 0) {
                return;
            }
            copyAssetTree("game", gameDirectory);
            marker.createNewFile();
        } catch (Exception error) {
            throw new IllegalStateException("Cannot extract bundled F-15 assets", error);
        }
    }

    @Override
    protected String[] getArguments() {
        File storage = getExternalFilesDir(null);
        if (storage == null) {
            storage = getFilesDir();
        }
        File gameDirectory = new File(storage, "game");
        gameDirectory.mkdirs();
        extractBundledGame(gameDirectory);
        return new String[] {"--game", gameDirectory.getAbsolutePath()};
    }
}
