package io.github.bbbomb0.a2hhook;

import android.content.Context;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.view.HapticFeedbackConstants;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;

import org.json.JSONObject;

import java.lang.reflect.Constructor;
import java.lang.reflect.Method;
import java.util.concurrent.ExecutorService;
import java.util.regex.Pattern;

final class A2HBridge {
    private static final Pattern HAPTIC_KIND = Pattern.compile("toggle|button|tick|confirm|error");
    private static final Pattern CALLBACK_NAME = Pattern.compile("__a2h_exec_[0-9]+_[0-9]+");

    private final WebUiActivity activity;
    private final WebView webView;
    private final ExecutorService executor;
    private final HapticEngine hapticEngine;

    A2HBridge(WebUiActivity context, WebView webView, ExecutorService executor) {
        this.activity = context;
        this.webView = webView;
        this.executor = executor;
        this.hapticEngine = new HapticEngine(context.getApplicationContext(), webView);
    }

    @JavascriptInterface
    public void exec(String command, String options, String callbackName) {
        if (callbackName == null || !CALLBACK_NAME.matcher(callbackName).matches()) return;
        executor.execute(() -> {
            RootShell.Result result = RootShell.run(command, 15000);
            String script = "window[" + JSONObject.quote(callbackName) + "](" +
                    result.code + "," + JSONObject.quote(result.stdout) + "," +
                    JSONObject.quote(result.stderr) + ");";
            webView.post(() -> webView.evaluateJavascript(script, null));
        });
    }

    @JavascriptInterface
    public void haptic(String kind, int strength) {
        if (kind == null || !HAPTIC_KIND.matcher(kind).matches()) return;
        int level = Math.max(0, Math.min(3, strength));
        if (level == 0) return;
        webView.post(() -> hapticEngine.perform(kind, level));
    }

    @JavascriptInterface
    public void setSystemBarsDark(boolean dark) {
        webView.post(() -> {
            if (!activity.isFinishing() && !activity.isDestroyed()) {
                activity.setSystemBarsDark(dark);
            }
        });
    }

    private static final class HapticEngine {
        private final Context context;
        private final WebView webView;
        private boolean miuiResolved;
        private Object miuiUtil;
        private Method miuiPerform;

        HapticEngine(Context context, WebView webView) {
            this.context = context;
            this.webView = webView;
        }

        void perform(String kind, int strength) {
            int feedback = feedbackConstant(kind, strength);
            if (performMiui(feedback)) return;
            try {
                if (webView.performHapticFeedback(feedback)) return;
            } catch (RuntimeException ignored) {
                // Fall through to the public vibrator API.
            }
            performVibrator(kind, strength);
        }

        private int feedbackConstant(String kind, int strength) {
            if ("confirm".equals(kind) || "error".equals(kind) || strength >= 3) {
                return HapticFeedbackConstants.LONG_PRESS;
            }
            if ("tick".equals(kind) || strength == 1) {
                return HapticFeedbackConstants.KEYBOARD_TAP;
            }
            return HapticFeedbackConstants.VIRTUAL_KEY;
        }

        private boolean performMiui(int feedback) {
            resolveMiui();
            if (miuiUtil == null || miuiPerform == null) return false;
            try {
                Object result = miuiPerform.invoke(miuiUtil, feedback, false);
                return !(result instanceof Boolean) || (Boolean) result;
            } catch (ReflectiveOperationException | RuntimeException ignored) {
                miuiUtil = null;
                miuiPerform = null;
                return false;
            }
        }

        private void resolveMiui() {
            if (miuiResolved) return;
            miuiResolved = true;
            try {
                Class<?> type = Class.forName("miui.util.HapticFeedbackUtil");
                Constructor<?> constructor = type.getConstructor(Context.class, boolean.class);
                miuiUtil = constructor.newInstance(context, false);
                miuiPerform = type.getMethod("performHapticFeedback", int.class, boolean.class);
            } catch (ReflectiveOperationException | RuntimeException ignored) {
                miuiUtil = null;
                miuiPerform = null;
            }
        }

        @SuppressWarnings("deprecation")
        private void performVibrator(String kind, int strength) {
            try {
                Vibrator vibrator;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    VibratorManager manager = context.getSystemService(VibratorManager.class);
                    vibrator = manager == null ? null : manager.getDefaultVibrator();
                } else {
                    vibrator = (Vibrator) context.getSystemService(Context.VIBRATOR_SERVICE);
                }
                if (vibrator == null || !vibrator.hasVibrator()) return;
                int effect;
                if ("error".equals(kind)) effect = VibrationEffect.EFFECT_DOUBLE_CLICK;
                else if ("confirm".equals(kind) || strength >= 3) effect = VibrationEffect.EFFECT_HEAVY_CLICK;
                else if ("tick".equals(kind) || strength == 1) effect = VibrationEffect.EFFECT_TICK;
                else effect = VibrationEffect.EFFECT_CLICK;
                vibrator.vibrate(VibrationEffect.createPredefined(effect));
            } catch (RuntimeException ignored) {
                // Haptics are optional and must never block configuration changes.
            }
        }
    }
}
