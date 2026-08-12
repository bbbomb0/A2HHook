// a2h_trigger - Synchronized silent AAudio registration stream.
#include <aaudio/AAudio.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <time.h>
#include <unistd.h>

enum {
    STATE_WAIT_STEPS = 8,
    STATE_WAIT_NS = 250000000,
    CALLBACK_WAIT_STEPS = 200,
    CALLBACK_WAIT_NS = 10000000,
    FALLBACK_LEASE_MS = 70000,
};

typedef struct {
    atomic_int callback_count;
    atomic_int error_code;
    int32_t channel_count;
} trigger_state_t;

static aaudio_data_callback_result_t silent_data_callback(
        AAudioStream *stream, void *user_data, void *audio_data,
        int32_t num_frames) {
    (void)stream;
    trigger_state_t *state = user_data;
    if (audio_data == NULL || num_frames <= 0) {
        atomic_store_explicit(&state->error_code, AAUDIO_ERROR_INTERNAL,
                              memory_order_relaxed);
        return AAUDIO_CALLBACK_RESULT_STOP;
    }
    memset(audio_data, 0, (size_t)num_frames *
           (size_t)state->channel_count * sizeof(int16_t));
    atomic_fetch_add_explicit(&state->callback_count, 1, memory_order_relaxed);
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

static void stream_error_callback(AAudioStream *stream, void *user_data,
                                  aaudio_result_t error) {
    (void)stream;
    trigger_state_t *state = user_data;
    atomic_store_explicit(&state->error_code, error, memory_order_relaxed);
}

static int wait_for_state(AAudioStream *stream, aaudio_stream_state_t wanted) {
    for (int step = 0; step < STATE_WAIT_STEPS; ++step) {
        aaudio_stream_state_t current = AAudioStream_getState(stream);
        if (current == wanted) return 0;
        if (current == AAUDIO_STREAM_STATE_CLOSING ||
            current == AAUDIO_STREAM_STATE_CLOSED ||
            current == AAUDIO_STREAM_STATE_DISCONNECTED) {
            return -1;
        }
        aaudio_stream_state_t next = current;
        aaudio_result_t result = AAudioStream_waitForStateChange(
            stream, current, &next, STATE_WAIT_NS);
        if (result != AAUDIO_OK && result != AAUDIO_ERROR_TIMEOUT) return -1;
    }
    return AAudioStream_getState(stream) == wanted ? 0 : -1;
}

static int wait_for_first_callback(AAudioStream *stream,
                                   trigger_state_t *state) {
    for (int step = 0; step < CALLBACK_WAIT_STEPS; ++step) {
        if (atomic_load_explicit(&state->callback_count,
                                 memory_order_relaxed) > 0) {
            return 0;
        }
        if (atomic_load_explicit(&state->error_code,
                                 memory_order_relaxed) != AAUDIO_OK) {
            return -1;
        }
        aaudio_stream_state_t current = AAudioStream_getState(stream);
        if (current == AAUDIO_STREAM_STATE_CLOSING ||
            current == AAUDIO_STREAM_STATE_CLOSED ||
            current == AAUDIO_STREAM_STATE_DISCONNECTED) {
            return -1;
        }
        struct timespec pause = {.tv_sec = 0, .tv_nsec = CALLBACK_WAIT_NS};
        while (nanosleep(&pause, &pause) != 0 && errno == EINTR) {}
    }
    return -1;
}

static int write_session_file(const char *path, AAudioStream *stream,
                              int ready) {
    if (path == NULL) return 0;
    FILE *file = fopen(path, "w");
    if (file == NULL) return -1;
    int session = AAudioStream_getSessionId(stream);
    int ok = session > 0 &&
             fprintf(file, ready ? "%d ready\n" : "%d\n", session) > 0 &&
             fflush(file) == 0;
    if (fclose(file) != 0) ok = 0;
    return ok ? 0 : -1;
}

static int fallback_timeout_ms(const char *path) {
    char value[32] = {0};
    FILE *file = fopen(path, "r");
    if (file == NULL) return -1;
    size_t length = fread(value, 1, sizeof(value) - 1, file);
    fclose(file);
    value[length] = '\0';
    if (strncmp(value, "fallback:", 9) != 0) return -1;
    char *end = NULL;
    long seconds = strtol(value + 9, &end, 10);
    if (end == value + 9 || (*end != '\0' && *end != '\n') ||
        seconds < 5 || seconds > 300) {
        seconds = FALLBACK_LEASE_MS / 1000;
    }
    return (int)(seconds * 1000);
}

static int wait_for_lease_fallback(const char *token) {
    int timeout_ms = fallback_timeout_ms(token);
    int remaining = timeout_ms < 0 ? -1 : (timeout_ms + 999) / 1000;
    while (access(token, F_OK) == 0) {
        if (remaining == 0) return 0;
        struct timespec pause = {.tv_sec = 1, .tv_nsec = 0};
        while (nanosleep(&pause, &pause) != 0 && errno == EINTR) {}
        if (remaining > 0) --remaining;
        timeout_ms = fallback_timeout_ms(token);
        if (timeout_ms < 0) remaining = -1;
    }
    return 0;
}

static int wait_for_lease(const char *token) {
    int notify = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    if (notify < 0) return wait_for_lease_fallback(token);
    int watch = inotify_add_watch(notify, token,
        IN_ATTRIB | IN_CLOSE_WRITE | IN_DELETE_SELF | IN_MOVE_SELF);
    if (watch < 0) {
        close(notify);
        return wait_for_lease_fallback(token);
    }

    char events[512];
    while (access(token, F_OK) == 0) {
        int timeout_ms = fallback_timeout_ms(token);
        struct pollfd descriptor = {.fd = notify, .events = POLLIN};
        int result;
        do {
            result = poll(&descriptor, 1, timeout_ms);
        } while (result < 0 && errno == EINTR);
        if (result == 0) break;
        if (result < 0) {
            inotify_rm_watch(notify, watch);
            close(notify);
            return wait_for_lease_fallback(token);
        }
        while (read(notify, events, sizeof(events)) > 0) {}
    }
    inotify_rm_watch(notify, watch);
    close(notify);
    return 0;
}

static void abort_stream(AAudioStream *stream) {
    if (stream == NULL) return;
    (void)AAudioStream_close(stream);
}

static int stop_and_close_stream(AAudioStream *stream) {
    if (stream == NULL) return -1;
    aaudio_result_t result = AAudioStream_requestStop(stream);
    if (result != AAUDIO_OK ||
        wait_for_state(stream, AAUDIO_STREAM_STATE_STOPPED) != 0) {
        fprintf(stderr, "TRIGGER: stop fail: %d state=%d\n", result,
                AAudioStream_getState(stream));
        abort_stream(stream);
        return -1;
    }
    result = AAudioStream_close(stream);
    if (result != AAUDIO_OK) {
        fprintf(stderr, "TRIGGER: close fail: %d\n", result);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *lease_token = NULL;
    const char *session_file = NULL;
    if (argc == 4 && strcmp(argv[1], "--lease") == 0) {
        lease_token = argv[2];
        session_file = argv[3];
    } else if (argc != 1) {
        fprintf(stderr, "usage: a2h_trigger [--lease TOKEN SESSION_FILE]\n");
        return 2;
    }

    AAudioStreamBuilder *builder = NULL;
    aaudio_result_t r = AAudio_createStreamBuilder(&builder);
    if (r != AAUDIO_OK) {
        fprintf(stderr, "TRIGGER: builder create fail: %d\n", r);
        return 1;
    }

    AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
    AAudioStreamBuilder_setChannelCount(builder, 2);
    AAudioStreamBuilder_setSampleRate(builder, 48000);
    AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
    AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_NONE);
    AAudioStreamBuilder_setSessionId(builder, AAUDIO_SESSION_ID_ALLOCATE);
    trigger_state_t trigger_state;
    atomic_init(&trigger_state.callback_count, 0);
    atomic_init(&trigger_state.error_code, AAUDIO_OK);
    trigger_state.channel_count = 2;
    AAudioStreamBuilder_setDataCallback(
        builder, silent_data_callback, &trigger_state);
    AAudioStreamBuilder_setErrorCallback(
        builder, stream_error_callback, &trigger_state);

    AAudioStream *stream = NULL;
    r = AAudioStreamBuilder_openStream(builder, &stream);
    if (r != AAUDIO_OK) {
        fprintf(stderr, "TRIGGER: openStream fail: %d\n", r);
        AAudioStreamBuilder_delete(builder);
        return 1;
    }
    trigger_state.channel_count = AAudioStream_getChannelCount(stream);
    if (AAudioStream_getFormat(stream) != AAUDIO_FORMAT_PCM_I16 ||
        trigger_state.channel_count <= 0 ||
        trigger_state.channel_count > 8) {
        fprintf(stderr, "TRIGGER: stream format fail: format=%d channels=%d\n",
                AAudioStream_getFormat(stream), trigger_state.channel_count);
        abort_stream(stream);
        AAudioStreamBuilder_delete(builder);
        return 1;
    }

    if (write_session_file(session_file, stream, 0) != 0) {
        fprintf(stderr, "TRIGGER: session file fail\n");
        abort_stream(stream);
        AAudioStreamBuilder_delete(builder);
        return 1;
    }
    r = AAudioStream_requestStart(stream);
    if (r != AAUDIO_OK || wait_for_state(stream, AAUDIO_STREAM_STATE_STARTED) != 0) {
        fprintf(stderr, "TRIGGER: start fail: %d state=%d\n", r,
                AAudioStream_getState(stream));
        abort_stream(stream);
        AAudioStreamBuilder_delete(builder);
        return 1;
    }
    if (wait_for_first_callback(stream, &trigger_state) != 0) {
        fprintf(stderr, "TRIGGER: callback fail: count=%d error=%d state=%d\n",
                atomic_load_explicit(&trigger_state.callback_count,
                                     memory_order_relaxed),
                atomic_load_explicit(&trigger_state.error_code,
                                     memory_order_relaxed),
                AAudioStream_getState(stream));
        abort_stream(stream);
        AAudioStreamBuilder_delete(builder);
        return 1;
    }
    if (write_session_file(session_file, stream, 1) != 0) {
        fprintf(stderr, "TRIGGER: ready file fail\n");
        abort_stream(stream);
        AAudioStreamBuilder_delete(builder);
        return 1;
    }

    if (lease_token != NULL) {
        fprintf(stderr, "TRIGGER: LEASE session=%d\n",
                AAudioStream_getSessionId(stream));
        (void)wait_for_lease(lease_token);
    } else {
        struct timespec settle = {.tv_sec = 0, .tv_nsec = 250000000};
        while (nanosleep(&settle, &settle) != 0 && errno == EINTR) {}
    }

    int callback_error = atomic_load_explicit(
        &trigger_state.error_code, memory_order_relaxed);
    if (callback_error != AAUDIO_OK) {
        fprintf(stderr, "TRIGGER: stream error: %d\n", callback_error);
        abort_stream(stream);
        AAudioStreamBuilder_delete(builder);
        return 1;
    }

    if (stop_and_close_stream(stream) != 0) {
        AAudioStreamBuilder_delete(builder);
        return 1;
    }
    AAudioStreamBuilder_delete(builder);
    fprintf(stderr, "TRIGGER: OK\n");
    return 0;
}
