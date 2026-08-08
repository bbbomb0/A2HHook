package io.github.bbbomb0.a2hhook;

import android.webkit.JavascriptInterface;
import android.webkit.WebView;

import org.json.JSONObject;

import java.util.concurrent.ExecutorService;

final class A2HBridge {
    private final WebView webView;
    private final ExecutorService executor;

    A2HBridge(WebView webView, ExecutorService executor) {
        this.webView = webView;
        this.executor = executor;
    }

    @JavascriptInterface
    public void exec(String command, String options, String callbackName) {
        if (callbackName == null || !callbackName.matches("__a2h_exec_[0-9]+_[0-9]+")) return;
        executor.execute(() -> {
            RootShell.Result result = RootShell.run(command, 15000);
            String script = "window[" + JSONObject.quote(callbackName) + "](" +
                    result.code + "," + JSONObject.quote(result.stdout) + "," +
                    JSONObject.quote(result.stderr) + ");";
            webView.post(() -> webView.evaluateJavascript(script, null));
        });
    }
}
