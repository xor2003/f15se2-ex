package org.f15se2.ex;

import android.Manifest;
import android.content.pm.PackageManager;
import android.graphics.PixelFormat;
import android.os.Build;
import android.os.Bundle;
import android.view.ViewGroup;
import android.widget.FrameLayout;
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
    private static final int CAMERA_PERMISSION_REQUEST = 15;
    private ArCameraView arCameraView;

    /**
     * Keeps the Android flight controller in one coordinate system.
     *
     * SDLActivity requests FULL_USER after native startup, overriding both the
     * manifest and onResume and allowing a portrait rotation while banking the
     * handset. Intercept every request at the Activity boundary so SDL, Camera2,
     * touch coordinates and the rotation-vector sensor remain landscape-only.
     */
    @Override
    public void setRequestedOrientation(int requestedOrientation) {
        super.setRequestedOrientation(
            android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
    }

    /** Enables AR by default; an explicit F15_AR=0 keeps the normal GLES sky. */
    private boolean isArRequested() {
        if (!getIntent().hasExtra("F15_AR")) {
            return true;
        }
        String value = getIntent().getStringExtra("F15_AR");
        if (value != null) {
            return "1".equals(value) || "true".equalsIgnoreCase(value) ||
                   "yes".equalsIgnoreCase(value);
        }
        return getIntent().getBooleanExtra("F15_AR", true);
    }

    /** Layers the camera behind SDL and requests an alpha-capable native surface. */
    private void configureAr() {
        if (!isArRequested()) {
            return;
        }
        nativeSetenv("F15_AR", "1");
        FrameLayout layers = new FrameLayout(this);
        ViewGroup parent = (ViewGroup)mLayout.getParent();
        if (parent != null) {
            parent.removeView(mLayout);
        }
        setContentView(layers);
        arCameraView = new ArCameraView(this);
        layers.addView(arCameraView, new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        layers.addView(mLayout, new FrameLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        mSurface.setZOrderOnTop(true);
        mSurface.getHolder().setFormat(PixelFormat.TRANSLUCENT);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M &&
            checkSelfPermission(Manifest.permission.CAMERA) !=
            PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.CAMERA},
                               CAMERA_PERMISSION_REQUEST);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        /*
         * Keep SDL, Camera2 and sensor coordinates in one fixed landscape
         * orientation. SENSOR_LANDSCAPE can briefly or permanently report a
         * portrait display rotation after wake-up on some devices, rotating
         * the camera preview and remapping the flight axes while playing.
         */
        super.onCreate(savedInstanceState);
        /* SDLActivity may apply its own manifest-derived orientation during
         * super.onCreate(), so enforce the fixed orientation afterwards. */
        setRequestedOrientation(
            android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        configureAr();
    }

    @Override
    protected void onResume() {
        super.onResume();
        /* Some Android shells clear a requested orientation while the camera
         * permission dialog or task switcher is in front. Reassert it here. */
        setRequestedOrientation(
            android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);
        if (arCameraView != null && (Build.VERSION.SDK_INT < Build.VERSION_CODES.M ||
            checkSelfPermission(Manifest.permission.CAMERA) ==
            PackageManager.PERMISSION_GRANTED)) {
            arCameraView.resume();
        }
    }

    @Override
    protected void onPause() {
        if (arCameraView != null) {
            arCameraView.pause();
        }
        super.onPause();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions,
                                           int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == CAMERA_PERMISSION_REQUEST && arCameraView != null &&
            grantResults.length > 0 &&
            grantResults[0] == PackageManager.PERMISSION_GRANTED) {
            arCameraView.resume();
        }
    }

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
