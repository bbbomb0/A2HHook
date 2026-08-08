package io.github.bbbomb0.a2hhook;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;

final class RootShell {
    static final class Result {
        final int code;
        final String stdout;
        final String stderr;

        Result(int code, String stdout, String stderr) {
            this.code = code;
            this.stdout = stdout;
            this.stderr = stderr;
        }
    }

    private static final int MAX_OUTPUT = 256 * 1024;

    private RootShell() {}

    static Result run(String command, long timeoutMs) {
        if (command == null || command.length() > 32768) {
            return new Result(2, "", "command-too-long");
        }
        Process process = null;
        try {
            process = new ProcessBuilder("su", "-c", command).start();
            final Process current = process;
            final ByteArrayOutputStream stdout = new ByteArrayOutputStream();
            final ByteArrayOutputStream stderr = new ByteArrayOutputStream();
            Thread outThread = new Thread(() -> drain(current.getInputStream(), stdout), "a2h-root-out");
            Thread errThread = new Thread(() -> drain(current.getErrorStream(), stderr), "a2h-root-err");
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
                    new String(stderr.toByteArray(), StandardCharsets.UTF_8));
        } catch (Exception error) {
            if (process != null) process.destroyForcibly();
            return new Result(1, "", error.getClass().getSimpleName());
        }
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
