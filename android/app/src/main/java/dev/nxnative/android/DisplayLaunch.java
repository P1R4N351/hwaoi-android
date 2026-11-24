// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import android.app.Activity;
import android.app.ActivityOptions;
import android.content.Context;
import android.content.Intent;
import android.hardware.display.DeviceProductInfo;
import android.hardware.display.DisplayManager;
import android.os.Build;
import android.os.Bundle;
import android.view.Display;

/**
 * Routes launcher and game launches onto the primary top screen ({@link Display#DEFAULT_DISPLAY})
 * on dual-screen handhelds such as AYN Thor when invoked from a built-in bottom launcher shell.
 */
final class DisplayLaunch {
    static final String EXTRA_MOVED_MAIN = "_moved_main_display";

    private DisplayLaunch() {}

    static boolean hasBuiltInSecondScreen(Context ctx) {
        if (ctx == null) return false;
        DisplayManager dm = (DisplayManager) ctx.getSystemService(Context.DISPLAY_SERVICE);
        if (dm == null) return false;
        Display main = dm.getDisplay(Display.DEFAULT_DISPLAY);
        DeviceProductInfo mainInfo = main != null && Build.VERSION.SDK_INT >= 31 ? main.getDeviceProductInfo() : null;
        String mainPnp = mainInfo == null ? null : mainInfo.getManufacturerPnpId();
        Display[] all = dm.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION);
        if (all == null || all.length == 0) all = dm.getDisplays();
        if (all == null || all.length == 0) return false;
        CompanionDisplay.Screen[] screens = new CompanionDisplay.Screen[all.length];
        for (int i = 0; i < all.length; i++) {
            Display d = all[i];
            if (d == null || d.getDisplayId() == Display.DEFAULT_DISPLAY) continue;
            DeviceProductInfo info = Build.VERSION.SDK_INT >= 31 ? d.getDeviceProductInfo() : null;
            boolean usable = d.isValid() && (d.getFlags() & Display.FLAG_PRIVATE) == 0;
            screens[i] = new CompanionDisplay.Screen(
                    d.getDisplayId(), usable,
                    info == null ? CompanionDisplay.NO_INFO : info.getConnectionToSinkType(),
                    info == null ? null : info.getManufacturerPnpId());
        }
        return CompanionDisplay.classify(screens, mainPnp) == CompanionDisplay.BUILT_IN;
    }

    static Bundle mainDisplayOptions(Context ctx) {
        if (!hasBuiltInSecondScreen(ctx)) return null;
        return ActivityOptions.makeBasic().setLaunchDisplayId(Display.DEFAULT_DISPLAY).toBundle();
    }

    static void startOnMainDisplay(Context ctx, Intent intent) {
        Bundle opts = mainDisplayOptions(ctx);
        if (opts == null) {
            ctx.startActivity(intent);
            return;
        }
        Intent copy = new Intent(intent);
        if (!(ctx instanceof Activity) || currentDisplayId((Activity) ctx) != Display.DEFAULT_DISPLAY) {
            copy.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        }
        ctx.startActivity(copy, opts);
    }

    static boolean ensureMainDisplay(Activity activity) {
        if (activity == null) return false;
        Intent src = activity.getIntent();
        if (src != null && src.getBooleanExtra(EXTRA_MOVED_MAIN, false)) return false;
        if (currentDisplayId(activity) == Display.DEFAULT_DISPLAY) return false;
        if (!hasBuiltInSecondScreen(activity)) return false;
        Intent relaunch = new Intent(src != null ? src : new Intent(activity, activity.getClass()));
        relaunch.setClass(activity, activity.getClass());
        relaunch.putExtra(EXTRA_MOVED_MAIN, true);
        relaunch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP);
        Bundle opts = ActivityOptions.makeBasic().setLaunchDisplayId(Display.DEFAULT_DISPLAY).toBundle();
        activity.startActivity(relaunch, opts);
        activity.finish();
        activity.overridePendingTransition(0, 0);
        return true;
    }

    private static int currentDisplayId(Activity a) {
        Display d = Build.VERSION.SDK_INT >= 30 ? a.getDisplay() : null;
        return d != null ? d.getDisplayId() : Display.DEFAULT_DISPLAY;
    }
}
