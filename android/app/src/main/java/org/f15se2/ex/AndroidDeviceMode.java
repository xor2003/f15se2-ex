package org.f15se2.ex;

import android.app.UiModeManager;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.res.Configuration;

/** TV devices use ordinary controls and an opaque game sky, never phone AR. */
final class AndroidDeviceMode {
    private AndroidDeviceMode() {}

    static boolean isTelevision(Context context) {
        UiModeManager modes = (UiModeManager)context.getSystemService(Context.UI_MODE_SERVICE);
        return (modes != null && modes.getCurrentModeType() == Configuration.UI_MODE_TYPE_TELEVISION)
            || context.getPackageManager().hasSystemFeature(PackageManager.FEATURE_LEANBACK)
            || context.getPackageManager().hasSystemFeature(PackageManager.FEATURE_TELEVISION);
    }
}
