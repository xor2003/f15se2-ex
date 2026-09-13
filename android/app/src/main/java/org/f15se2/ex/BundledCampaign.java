package org.f15se2.ex;

import android.content.Context;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

/** Installs the APK's own immutable campaign, without touching imported data or saves. */
final class BundledCampaign {
    static File directory(Context context) {
        return new File(context.getFilesDir(), "bundled-20260913/campaigns/SVN");
    }

    static void prepare(Context context) throws IOException {
        File destination = directory(context);
        File ready = new File(destination, ".ready");
        if (ready.isFile()) return;
        copy(context, "campaigns/SVN", destination);
        if (!ready.createNewFile()) throw new IOException("Cannot activate bundled campaign");
    }

    private static void copy(Context context, String source, File destination) throws IOException {
        String[] children = context.getAssets().list(source);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs())
                throw new IOException("Cannot create " + destination);
            for (String child : children) copy(context, source + "/" + child, new File(destination, child));
            return;
        }
        try (InputStream input = context.getAssets().open(source);
             FileOutputStream output = new FileOutputStream(destination)) {
            byte[] buffer = new byte[16384];
            int count;
            while ((count = input.read(buffer)) != -1) output.write(buffer, 0, count);
        }
    }
}
