package io.github.bbbomb0.a2hhook;

import android.app.Activity;
import android.content.Intent;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Insets;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.Toast;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class WebUiActivity extends Activity {
    private static final String LOCAL_HOST = "a2h.local";
    private final ExecutorService executor = Executors.newSingleThreadExecutor(r -> {
        Thread thread = new Thread(r, "a2h-web-root");
        thread.setDaemon(true);
        return thread;
    });
    private WebView webView;
    private FrameLayout rootView;
    private boolean backDispatching;
    private OnBackInvokedCallback predictiveBackCallback;
    private int insetTop;
    private int insetRight;
    private int insetBottom;
    private int insetLeft;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        boolean dark = isDarkMode(getResources().getConfiguration());
        int backgroundColor = backgroundColor(dark);
        configureEdgeToEdge(dark);
        webView = new WebView(this);
        webView.setBackgroundColor(backgroundColor);
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setBlockNetworkLoads(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            settings.setAlgorithmicDarkeningAllowed(false);
        }
        WebView.setWebContentsDebuggingEnabled(false);
        webView.addJavascriptInterface(new A2HBridge(this, webView, executor), "ksu");
        webView.setWebViewClient(new LocalOnlyClient());
        rootView = new FrameLayout(this);
        rootView.setBackgroundColor(backgroundColor);
        rootView.addView(webView, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        rootView.setOnApplyWindowInsetsListener((view, insets) -> {
            updateSafeInsets(insets);
            return insets;
        });
        setContentView(rootView, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        rootView.requestApplyInsets();
        webView.loadDataWithBaseURL("https://" + LOCAL_HOST + "/", readWebUiAsset(dark),
                "text/html", "UTF-8", null);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            predictiveBackCallback = this::dispatchBackToWebUi;
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(
                    OnBackInvokedDispatcher.PRIORITY_DEFAULT, predictiveBackCallback);
        }
    }

    @Override
    public void onConfigurationChanged(Configuration configuration) {
        super.onConfigurationChanged(configuration);
        applySystemTheme(isDarkMode(configuration));
    }

    private boolean isDarkMode(Configuration configuration) {
        return (configuration.uiMode & Configuration.UI_MODE_NIGHT_MASK)
                == Configuration.UI_MODE_NIGHT_YES;
    }

    private int backgroundColor(boolean dark) {
        return dark ? Color.BLACK : Color.rgb(247, 247, 247);
    }

    private void applySystemTheme(boolean dark) {
        int color = backgroundColor(dark);
        if (rootView != null) rootView.setBackgroundColor(color);
        if (webView != null) {
            webView.setBackgroundColor(color);
            webView.evaluateJavascript(
                    "window.A2HNativeTheme&&window.A2HNativeTheme(" + dark + ");", null);
        }
        setSystemBarsDark(dark);
    }

    @Override
    @SuppressWarnings("deprecation")
    public void onBackPressed() {
        dispatchBackToWebUi();
    }

    private void dispatchBackToWebUi() {
        if (webView == null || backDispatching) return;
        backDispatching = true;
        webView.evaluateJavascript(
                "(function(){return !!(window.A2HWebUIBack&&window.A2HWebUIBack());})()",
                result -> {
                    backDispatching = false;
                    if (!"true".equals(result)) finishActivityBack();
                });
    }

    private void finishActivityBack() {
        finishAfterTransition();
    }

    @SuppressWarnings("deprecation")
    private void configureEdgeToEdge(boolean dark) {
        Window window = getWindow();
        window.setStatusBarColor(Color.TRANSPARENT);
        window.setNavigationBarColor(Color.TRANSPARENT);
        window.setNavigationBarDividerColor(Color.TRANSPARENT);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            window.setStatusBarContrastEnforced(false);
            window.setNavigationBarContrastEnforced(false);
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
        } else {
            View decor = window.getDecorView();
            decor.setSystemUiVisibility(decor.getSystemUiVisibility()
                    | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
        setSystemBarsDark(dark);
    }

    @SuppressWarnings("deprecation")
    void setSystemBarsDark(boolean dark) {
        Window window = getWindow();
        View decor = window.getDecorView();
        boolean lightBackground = !dark;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            WindowInsetsController controller = window.getInsetsController();
            if (controller == null) return;
            int mask = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                    | WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
            controller.setSystemBarsAppearance(lightBackground ? mask : 0, mask);
            return;
        }
        int visibility = decor.getSystemUiVisibility()
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION;
        int lightFlags = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
                | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
        visibility = lightBackground ? visibility | lightFlags : visibility & ~lightFlags;
        decor.setSystemUiVisibility(visibility);
    }

    @SuppressWarnings("deprecation")
    private void updateSafeInsets(WindowInsets insets) {
        int top;
        int right;
        int bottom;
        int left;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            int types = WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout();
            Insets safe = insets.getInsetsIgnoringVisibility(types);
            top = safe.top;
            right = safe.right;
            bottom = safe.bottom;
            left = safe.left;
        } else {
            top = insets.getStableInsetTop();
            right = insets.getStableInsetRight();
            bottom = insets.getStableInsetBottom();
            left = insets.getStableInsetLeft();
            if (insets.getDisplayCutout() != null) {
                top = Math.max(top, insets.getDisplayCutout().getSafeInsetTop());
                right = Math.max(right, insets.getDisplayCutout().getSafeInsetRight());
                bottom = Math.max(bottom, insets.getDisplayCutout().getSafeInsetBottom());
                left = Math.max(left, insets.getDisplayCutout().getSafeInsetLeft());
            }
        }
        top = toCssPixels(top);
        right = toCssPixels(right);
        bottom = toCssPixels(bottom);
        left = toCssPixels(left);
        if (insetTop == top && insetRight == right && insetBottom == bottom && insetLeft == left) {
            return;
        }
        insetTop = top;
        insetRight = right;
        insetBottom = bottom;
        insetLeft = left;
        dispatchInsetsToWebUi();
    }

    private int toCssPixels(int physicalPixels) {
        float density = getResources().getDisplayMetrics().density;
        if (!(density > 0f)) density = 1f;
        return Math.max(0, Math.round(physicalPixels / density));
    }

    private void dispatchInsetsToWebUi() {
        if (webView == null) return;
        String script = "window.A2HSystemInsets&&window.A2HSystemInsets(" + insetTop + ","
                + insetRight + "," + insetBottom + "," + insetLeft + ");";
        webView.post(() -> {
            if (webView != null) webView.evaluateJavascript(script, null);
        });
    }

    private String readAsset(String name) {
        try (InputStream stream = getAssets().open(name)) {
            ByteArrayOutputStream output = new ByteArrayOutputStream(Math.max(4096, stream.available()));
            byte[] buffer = new byte[4096];
            int count;
            while ((count = stream.read(buffer)) >= 0) output.write(buffer, 0, count);
            return new String(output.toByteArray(), StandardCharsets.UTF_8);
        } catch (IOException error) {
            return "<!doctype html><meta charset=\"utf-8\"><p>WebUI 资源缺失</p>";
        }
    }

    private String readWebUiAsset(boolean dark) {
        String html = readAsset("index.html");
        String marker = "<head>";
        int index = html.indexOf(marker);
        if (index < 0) return html;
        String bootstrap = "<script>window.__A2H_NATIVE_DARK__=" + dark + ";</script>";
        return html.substring(0, index + marker.length()) + bootstrap
                + html.substring(index + marker.length());
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (webView != null) {
            webView.onResume();
            webView.resumeTimers();
        }
    }

    @Override
    protected void onPause() {
        if (webView != null) {
            webView.onPause();
            webView.pauseTimers();
        }
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
                && predictiveBackCallback != null) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(predictiveBackCallback);
            predictiveBackCallback = null;
        }
        executor.shutdownNow();
        if (webView != null) {
            webView.stopLoading();
            webView.removeJavascriptInterface("ksu");
            if (webView.getParent() instanceof ViewGroup) {
                ((ViewGroup) webView.getParent()).removeView(webView);
            }
            webView.removeAllViews();
            webView.destroy();
            webView = null;
        }
        rootView = null;
        super.onDestroy();
    }

    private final class LocalOnlyClient extends WebViewClient {
        @Override
        public void onPageFinished(WebView view, String url) {
            dispatchInsetsToWebUi();
            applySystemTheme(isDarkMode(getResources().getConfiguration()));
        }

        @Override
        public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
            Uri uri = request.getUrl();
            if (isAllowedLocalUri(uri)) return false;
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, uri));
            } catch (Exception error) {
                Toast.makeText(WebUiActivity.this, "无法打开链接", Toast.LENGTH_SHORT).show();
            }
            return true;
        }

        @Override
        public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request) {
            Uri uri = request.getUrl();
            if (isAllowedLocalUri(uri)) {
                if ("/".equals(uri.getPath())) {
                    return null;
                }
                String asset = assetName(uri.getPath());
                if (asset != null) {
                    try {
                        String mime = asset.endsWith(".webp") ? "image/webp" :
                                asset.endsWith(".svg") ? "image/svg+xml" : "image/png";
                        return new WebResourceResponse(mime, null, getAssets().open(asset));
                    } catch (IOException ignored) {
                        return emptyResponse();
                    }
                }
            }
            return emptyResponse();
        }

        private boolean isAllowedLocalUri(Uri uri) {
            if (!"https".equals(uri.getScheme()) || !LOCAL_HOST.equals(uri.getHost()) ||
                    uri.getPort() != -1 || uri.getUserInfo() != null ||
                    uri.getQuery() != null) {
                return false;
            }
            String path = uri.getPath();
            return "/".equals(path) || assetName(path) != null;
        }

        private String assetName(String path) {
            if ("/coolapk.webp".equals(path)) return "coolapk.webp";
            if ("/donate-wechat-pay.webp".equals(path)) return "donate-wechat-pay.webp";
            if ("/donate-wechat.webp".equals(path)) return "donate-wechat.webp";
            if ("/donate-alipay.webp".equals(path)) return "donate-alipay.webp";
            if ("/payment-wechat-pay.webp".equals(path)) return "payment-wechat-pay.webp";
            if ("/payment-wechat-reward.webp".equals(path)) return "payment-wechat-reward.webp";
            if ("/payment-alipay.webp".equals(path)) return "payment-alipay.webp";
            return null;
        }

        private WebResourceResponse emptyResponse() {
            return new WebResourceResponse("text/plain", "UTF-8",
                    new ByteArrayInputStream(new byte[0]));
        }
    }
}
