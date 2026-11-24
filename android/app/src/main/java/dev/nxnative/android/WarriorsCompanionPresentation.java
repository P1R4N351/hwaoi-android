// SPDX-License-Identifier: MIT
package dev.nxnative.android;

import android.app.Presentation;
import android.content.Context;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.drawable.ColorDrawable;
import android.os.Bundle;
import android.view.Display;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;

/**
 * Non-focusable Presentation hosting the converged Warriors second-screen Canvas view.
 * Keeps controller and input focus on Display 0 (GameActivity).
 */
final class WarriorsCompanionPresentation extends Presentation {
    private final WarriorsCompanionModel model;
    private final WarriorsAssetHarvester art;
    private final WarriorsCompanionView.Actions actions;
    private WarriorsCompanionView contentView;

    WarriorsCompanionPresentation(
        Context outerContext, Display display, WarriorsCompanionModel model,
        WarriorsAssetHarvester art, WarriorsCompanionView.Actions actions) {
        super(outerContext, display);
        this.model = model;
        this.art = art;
        this.actions = actions;
    }

    WarriorsCompanionView companionView() { return contentView; }

    @Override protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        Window w = getWindow();
        if (w != null) {
            w.setFormat(PixelFormat.OPAQUE);
            w.setBackgroundDrawable(new ColorDrawable(Color.BLACK));
            w.clearFlags(WindowManager.LayoutParams.FLAG_DIM_BEHIND);
            w.addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN
                    | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                    | WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
                    | WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL);
            w.setWindowAnimations(0);
            View decor = w.getDecorView();
            @SuppressWarnings("deprecation")
            int vis = View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE;
            @SuppressWarnings("deprecation")
            Runnable apply = () -> decor.setSystemUiVisibility(vis);
            apply.run();
        }
        contentView = new WarriorsCompanionView(getContext(), model, art, actions);
        setContentView(contentView);
    }
}
