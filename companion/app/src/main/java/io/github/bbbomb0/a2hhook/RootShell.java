package io.github.bbbomb0.a2hhook;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.concurrent.TimeUnit;

final class RootShell {
    enum AccessStatus {
        GRANTED,
        NOT_AUTHORIZED,
        DENIED,
        UNAVAILABLE,
        TIMEOUT,
        COMMAND_FAILED
    }

    static final class Result {
        final int code;
        final String stdout;
        final String stderr;
        final AccessStatus status;

        Result(int code, String stdout, String stderr, AccessStatus status) {
            this.code = code;
            this.stdout = stdout;
            this.stderr = stderr;
            this.status = status;
        }
    }

    private static final int MAX_OUTPUT = 256 * 1024;

    private RootShell() {}

    static Result probe(long timeoutMs) {
        Result result = run("id -u", timeoutMs);
        if (result.status == AccessStatus.TIMEOUT ||
                result.status == AccessStatus.UNAVAILABLE) {
            return result;
        }
        if (result.code == 0 && "0".equals(result.stdout.trim())) {
            return new Result(result.code, result.stdout, result.stderr,
                    AccessStatus.GRANTED);
        }
        String detail = (result.stdout + "\n" + result.stderr).toLowerCase(Locale.ROOT);
        AccessStatus status = containsDenied(detail)
                ? AccessStatus.DENIED : AccessStatus.NOT_AUTHORIZED;
        return new Result(result.code, result.stdout, result.stderr, status);
    }

    static Result run(String command, long timeoutMs) {
        if (command == null || command.length() > 32768) {
            return new Result(2, "", "command-too-long", AccessStatus.COMMAND_FAILED);
        }
        Process process = null;
        try {
            // ReSukiSU/KernelSU may launch a direct external command in the
            // shell SELinux domain even though su itself succeeded. An
            // explicit nested shell keeps the whole fixed bridge payload in
            // the granted root domain and is also accepted by Magisk su.
            process = new ProcessBuilder("su", "-c",
                    "sh -c " + shellQuote(command)).start();
            process.getOutputStream().close();
            final Process current = process;
            final ByteArrayOutputStream stdout = new ByteArrayOutputStream();
            final ByteArrayOutputStream stderr = new ByteArrayOutputStream();
            Thread outThread = new Thread(() -> drain(current.getInputStream(), stdout), "a2h-root-out");
            Thread errThread = new Thread(() -> drain(current.getErrorStream(), stderr), "a2h-root-err");
            outThread.setDaemon(true);
            errThread.setDaemon(true);
            outThread.start();
            errThread.start();
            boolean finished = process.waitFor(timeoutMs, TimeUnit.MILLISECONDS);
            if (!finished) {
                process.destroy();
                if (!process.waitFor(500, TimeUnit.MILLISECONDS)) process.destroyForcibly();
            }
            outThread.join(1000);
            errThread.join(1000);
            int code = finished ? process.exitValue() : 124;
            return new Result(code,
                    new String(stdout.toByteArray(), StandardCharsets.UTF_8),
                    new String(stderr.toByteArray(), StandardCharsets.UTF_8),
                    finished ? (code == 0 ? AccessStatus.GRANTED : AccessStatus.COMMAND_FAILED)
                            : AccessStatus.TIMEOUT);
        } catch (Exception error) {
            if (process != null) process.destroyForcibly();
            String message = error.getMessage();
            String detail = error.getClass().getSimpleName();
            if (message != null && !message.trim().isEmpty()) {
                detail += ": " + message.trim();
            }
            AccessStatus status = error instanceof IOException
                    ? AccessStatus.UNAVAILABLE : AccessStatus.COMMAND_FAILED;
            return new Result(1, "", detail, status);
        }
    }

    private static boolean containsDenied(String detail) {
        return detail.contains("denied") || detail.contains("not allowed") ||
                detail.contains("not permitted") || detail.contains("permission") ||
                detail.contains("拒绝") || detail.contains("未授权");
    }

    private static String shellQuote(String value) {
        return "'" + value.replace("'", "'\"'\"'") + "'";
    }

    private static void drain(InputStream input, ByteArrayOutputStream output) {
        byte[] buffer = new byte[4096];
        int remaining = MAX_OUTPUT;
        try (InputStream stream = input) {
            while (true) {
                int count = stream.read(buffer);
                if (count < 0) break;
                if (remaining > 0) {
                    int stored = Math.min(count, remaining);
                    output.write(buffer, 0, stored);
                    remaining -= stored;
                }
            }
        } catch (IOException ignored) {
            // The process result remains useful even when a stream closes early.
        }
    }
}
