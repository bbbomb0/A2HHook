package io.github.bbbomb0.a2hhook;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.view.ViewGroup;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

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

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        webView = new WebView(this);
        webView.setBackgroundColor(Color.TRANSPARENT);
        WebSettings settings = webView.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        settings.setBlockNetworkLoads(true);
        settings.setAllowFileAccess(false);
        settings.setAllowContentAccess(false);
        settings.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);
        WebView.setWebContentsDebuggingEnabled(false);
        webView.addJavascriptInterface(new A2HBridge(webView, executor), "ksu");
        webView.setWebViewClient(new LocalOnlyClient());
        setContentView(webView, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        webView.loadDataWithBaseURL("https://" + LOCAL_HOST + "/", readAsset("index.html"),
                "text/html", "UTF-8", null);
    }

    private String readAsset(String name) {
        try (InputStream stream = getAssets().open(name)) {
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] buffer = new byte[4096];
            int count;
            while ((count = stream.read(buffer)) >= 0) output.write(buffer, 0, count);
            return new String(output.toByteArray(), StandardCharsets.UTF_8);
        } catch (IOException error) {
            return "<!doctype html><meta charset=\"utf-8\"><p>WebUI 资源缺失</p>";
        }
    }

    @Override
    protected void onDestroy() {
        if (webView != null) {
            webView.removeJavascriptInterface("ksu");
            webView.destroy();
        }
        executor.shutdownNow();
        super.onDestroy();
    }

    private final class LocalOnlyClient extends WebViewClient {
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
            if (isAllowedLocalUri(uri) && "/coolapk.png".equals(uri.getPath())) {
                try {
                    return new WebResourceResponse("image/png", null,
                            getAssets().open("coolapk.png"));
                } catch (IOException ignored) {
                    return emptyResponse();
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
            return "/".equals(path) || "/coolapk.png".equals(path);
        }

        private WebResourceResponse emptyResponse() {
            return new WebResourceResponse("text/plain", "UTF-8",
                    new ByteArrayInputStream(new byte[0]));
        }
    }
}
