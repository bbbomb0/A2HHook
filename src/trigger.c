// a2h_trigger - Synchronized silent AAudio registration stream.
#ifndef A2H_TRIGGER_LEASE_WATCH_TEST
#include <aaudio/AAudio.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum {
    STATE_WAIT_STEPS = 8,
    STATE_WAIT_NS = 250000000,
    CALLBACK_WAIT_STEPS = 200,
    CALLBACK_WAIT_NS = 10000000,
    FALLBACK_LEASE_MS = 70000,
};

static int write_session_contents(const char *path, int session, int ready) {
    if (path == NULL || session <= 0) return -1;

    size_t path_length = strlen(path);
    if (path_length > SIZE_MAX - sizeof(".tmp")) return -1;
    char *temporary_path = malloc(path_length + sizeof(".tmp"));
    if (temporary_path == NULL) return -1;
    memcpy(temporary_path, path, path_length);
    memcpy(temporary_path + path_length, ".tmp", sizeof(".tmp"));

    int descriptor = open(temporary_path,
                          O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (descriptor < 0) {
        free(temporary_path);
        return -1;
    }
    char contents[32];
    int content_length = snprintf(contents, sizeof(contents),
                                  ready ? "%d ready\n" : "%d\n", session);
    int ok = content_length > 0 && (size_t)content_length < sizeof(contents);
    size_t written = 0;
    while (ok && written < (size_t)content_length) {
        ssize_t count = write(descriptor, contents + written,
                              (size_t)content_length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            ok = 0;
            break;
        }
        written += (size_t)count;
    }
    if (close(descriptor) != 0) ok = 0;
    if (ok && rename(temporary_path, path) == 0) {
        free(temporary_path);
        return 0;
    }
    (void)unlink(temporary_path);
    free(temporary_path);
    return -1;
}

#ifndef A2H_TRIGGER_LEASE_WATCH_TEST
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
    return write_session_contents(path, AAudioStream_getSessionId(stream), ready);
}
#endif

typedef struct {
    struct stat metadata;
    int present;
    int timeout_ms;
} lease_snapshot_t;

#ifdef A2H_TRIGGER_LEASE_WATCH_TEST
static int lease_test_force_poll;
static const char *lease_test_ready_path;
#endif

static int fallback_timeout_ms(const char *value) {
    if (strncmp(value, "fallback:", 9) != 0) return -1;
    char *end = NULL;
    long seconds = strtol(value + 9, &end, 10);
    if (end == value + 9 || (*end != '\0' && *end != '\n') ||
        seconds < 1 || seconds > 300) {
        seconds = FALLBACK_LEASE_MS / 1000;
    }
    return (int)(seconds * 1000);
}

static int read_lease_snapshot(const char *path, lease_snapshot_t *snapshot) {
    memset(snapshot, 0, sizeof(*snapshot));
    int fd;
    do {
        fd = open(path, O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0) {
        if (errno == ENOENT) return 0;
        if (access(path, F_OK) == 0) {
            snapshot->present = 1;
            snapshot->timeout_ms = -1;
        }
        return 0;
    }
    if (fstat(fd, &snapshot->metadata) != 0) {
        close(fd);
        snapshot->present = 1;
        snapshot->timeout_ms = -1;
        return 0;
    }

    char value[32] = {0};
    ssize_t length;
    do {
        length = read(fd, value, sizeof(value) - 1);
    } while (length < 0 && errno == EINTR);
    int close_result = close(fd);
    if (length < 0 || close_result != 0) {
        snapshot->present = 1;
        snapshot->timeout_ms = -1;
        return 0;
    }
    value[(size_t)length] = '\0';
    snapshot->present = 1;
    snapshot->timeout_ms = fallback_timeout_ms(value);
    return 0;
}

static int same_lease_snapshot(const lease_snapshot_t *left,
                               const lease_snapshot_t *right) {
    if (left->present != right->present) return 0;
    if (!left->present) return 1;
    return left->metadata.st_dev == right->metadata.st_dev &&
           left->metadata.st_ino == right->metadata.st_ino &&
           left->metadata.st_size == right->metadata.st_size &&
           left->metadata.st_mtime == right->metadata.st_mtime &&
           left->metadata.st_ctime == right->metadata.st_ctime;
}

static int add_lease_directory_watch(int notify, const char *token) {
    const char *slash = strrchr(token, '/');
    size_t directory_length = slash == NULL ? 1u :
        (slash == token ? 1u : (size_t)(slash - token));
    char *directory = malloc(directory_length + 1u);
    if (directory == NULL) return -1;
    if (slash == NULL) {
        directory[0] = '.';
    } else if (slash == token) {
        directory[0] = '/';
    } else {
        memcpy(directory, token, directory_length);
    }
    directory[directory_length] = '\0';
    int watch = inotify_add_watch(notify, directory,
        IN_ATTRIB | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_FROM |
        IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF);
    free(directory);
    return watch;
}

static int64_t monotonic_time_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int lease_remaining_ms(int64_t deadline_ms) {
    if (deadline_ms < 0) return -1;
    int64_t remaining = deadline_ms - monotonic_time_ms();
    if (remaining <= 0) return 0;
    return remaining > INT32_MAX ? INT32_MAX : (int)remaining;
}

static int64_t lease_deadline_ms(const lease_snapshot_t *snapshot) {
    return snapshot->timeout_ms < 0 ? -1 :
        monotonic_time_ms() + snapshot->timeout_ms;
}

static void signal_lease_test_ready(char mode) {
#ifdef A2H_TRIGGER_LEASE_WATCH_TEST
    if (lease_test_ready_path != NULL) {
        int fd = open(lease_test_ready_path,
                      O_WRONLY | O_TRUNC | O_CLOEXEC);
        if (fd >= 0) {
            (void)write(fd, &mode, sizeof(mode));
            close(fd);
        }
    }
#else
    (void)mode;
#endif
}

static int wait_for_lease(const char *token) {
    lease_snapshot_t current;
    if (read_lease_snapshot(token, &current) != 0 || !current.present) {
        signal_lease_test_ready('E');
        return 0;
    }
    int64_t deadline_ms = lease_deadline_ms(&current);

    int notify = -1;
    int watch = -1;
#ifdef A2H_TRIGGER_LEASE_WATCH_TEST
    if (!lease_test_force_poll)
#endif
    {
        notify = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
        if (notify >= 0) {
            watch = add_lease_directory_watch(notify, token);
            if (watch < 0) {
                close(notify);
                notify = -1;
            }
        }
    }
    signal_lease_test_ready(notify >= 0 ? 'I' : 'P');

    char events[512];
    for (;;) {
        lease_snapshot_t latest;
        int snapshot_result = read_lease_snapshot(token, &latest);
        if (snapshot_result == 0 && !latest.present) break;
        if (snapshot_result == 0 && !same_lease_snapshot(&current, &latest)) {
            current = latest;
            deadline_ms = lease_deadline_ms(&current);
        }

        int timeout_ms = lease_remaining_ms(deadline_ms);
        if (timeout_ms == 0) break;
        if (notify < 0) {
            int pause_ms = timeout_ms < 0 || timeout_ms > 1000 ? 1000 : timeout_ms;
            struct timespec pause = {
                .tv_sec = pause_ms / 1000,
                .tv_nsec = (long)(pause_ms % 1000) * 1000000L,
            };
            while (nanosleep(&pause, &pause) != 0 && errno == EINTR) {}
            continue;
        }

        struct pollfd descriptor = {.fd = notify, .events = POLLIN};
        int result;
        do {
            result = poll(&descriptor, 1, timeout_ms);
        } while (result < 0 && errno == EINTR);
        if (result == 0) continue;
        if (result < 0) {
            inotify_rm_watch(notify, watch);
            close(notify);
            notify = -1;
            watch = -1;
            continue;
        }
        if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            inotify_rm_watch(notify, watch);
            close(notify);
            notify = -1;
            watch = -1;
            continue;
        }
        while (read(notify, events, sizeof(events)) > 0) {}
    }
    if (watch >= 0) inotify_rm_watch(notify, watch);
    if (notify >= 0) close(notify);
    return 0;
}

#ifndef A2H_TRIGGER_LEASE_WATCH_TEST
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
#endif
