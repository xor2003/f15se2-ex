package org.f15se2.ex;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.InputStream;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** Keeps document access and installation out of the SDL/native game lifecycle. */
public final class AssetImportActivity extends Activity {
    private static final int PICK_ZIP = 1;
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private TextView status;
    private Button play;
    private Button importZip;
    private File root;
    private Map<String, String> manifest;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        root = getExternalFilesDir(null);
        if (root == null) root = getFilesDir();
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        int padding = (int)(24 * getResources().getDisplayMetrics().density);
        layout.setPadding(padding, padding, padding, padding);
        status = new TextView(this);
        status.setText("Checking game files...");
        play = new Button(this);
        play.setText("Play");
        play.setEnabled(false);
        play.setOnClickListener(view -> {
            startActivity(new Intent(this, MainActivity.class));
            finish();
        });
        importZip = new Button(this);
        importZip.setText("Import game ZIP");
        importZip.setEnabled(false);
        importZip.setOnClickListener(view -> {
            Intent picker = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            picker.addCategory(Intent.CATEGORY_OPENABLE);
            picker.setType("*/*");
            picker.putExtra(Intent.EXTRA_MIME_TYPES,
                new String[] {"application/zip", "application/x-zip-compressed", "application/octet-stream"});
            startActivityForResult(picker, PICK_ZIP);
        });
        layout.addView(status);
        layout.addView(play);
        layout.addView(importZip);
        setContentView(layout);
        worker.execute(() -> {
            try (InputStream input = getAssets().open("game-assets.md5")) {
                manifest = GameAssetInstaller.readManifest(input);
                GameAssetInstaller.recover(root);
                GameAssetInstaller.validate(new File(root, "game"), manifest);
                show("Game files ready.", true);
            } catch (Exception error) {
                show("Import a ZIP of your F-15 Strike Eagle II v451.03 game directory. " +
                     "The APK does not include game data.\n\n" + error.getMessage(), false);
            }
        });
    }

    /** Updates the launcher only while its activity still owns the screen. */
    private void show(String message, boolean ready) {
        runOnUiThread(() -> {
            if (isFinishing() || isDestroyed()) return;
            status.setText(message);
            play.setEnabled(ready);
            importZip.setEnabled(manifest != null);
        });
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_ZIP || result != RESULT_OK || data == null || data.getData() == null) return;
        play.setEnabled(false);
        importZip.setEnabled(false);
        status.setText("Importing and checking game files...");
        worker.execute(() -> {
            try (InputStream input = getContentResolver().openInputStream(data.getData())) {
                if (input == null) throw new java.io.IOException("Cannot open ZIP");
                GameAssetInstaller.install(input, root, manifest);
                show("Game files imported. Tap Play.", true);
            } catch (Exception error) {
                boolean ready = false;
                try {
                    GameAssetInstaller.validate(new File(root, "game"), manifest);
                    ready = true;
                } catch (Exception ignored) { /* A failed first import has no installation. */ }
                show("Import failed: " + error.getMessage(), ready);
            }
        });
    }

    @Override
    protected void onDestroy() {
        worker.shutdownNow();
        super.onDestroy();
    }
}
