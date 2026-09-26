#define A2H_TRIGGER_LEASE_WATCH_TEST
#include "../src/trigger.c"

#include <signal.h>
#include <sys/wait.h>

static const char *test_binary_path;
static uid_t test_app_uid;
static volatile sig_atomic_t stop_session_reader;

static void request_session_reader_stop(int signal_number) {
    (void)signal_number;
    stop_session_reader = 1;
}

static int write_file(const char *path, const char *value, mode_t mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
    if (fd < 0) return -1;
    size_t length = strlen(value);
    size_t written = 0;
    while (written < length) {
        ssize_t result = write(fd, value + written, length - written);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) {
            close(fd);
            return -1;
        }
        written += (size_t)result;
    }
    int mode_result = fchmod(fd, mode);
    int close_result = close(fd);
    return mode_result == 0 && close_result == 0 ? 0 : -1;
}

static int atomic_replace(const char *path, const char *value) {
    char temporary[512];
    int length = snprintf(temporary, sizeof(temporary), "%s.next", path);
    if (length < 0 || (size_t)length >= sizeof(temporary)) return -1;
    unlink(temporary);
    if (write_file(temporary, value, 0444) != 0) return -1;
    return rename(temporary, path);
}

static int sleep_ms(int milliseconds) {
    struct timespec pause = {
        .tv_sec = milliseconds / 1000,
        .tv_nsec = (long)(milliseconds % 1000) * 1000000L,
    };
    while (nanosleep(&pause, &pause) != 0) {
        if (errno != EINTR) return -1;
    }
    return 0;
}

static pid_t start_waiter(const char *token, const char *ready, int force_poll) {
    pid_t child = fork();
    if (child != 0) return child;
    char uid[32];
    char command[1536];
    int uid_length = snprintf(uid, sizeof(uid), "%lu",
                              (unsigned long)test_app_uid);
    int command_length = snprintf(
        command, sizeof(command), "exec %s --waiter %s %s %s %s",
        test_binary_path, force_poll ? "poll" : "inotify", token, ready, uid);
    if (uid_length < 0 || (size_t)uid_length >= sizeof(uid) ||
        command_length < 0 || (size_t)command_length >= sizeof(command)) {
        _exit(90);
    }
    execl("/system/bin/su", "su", uid, "-c", command, (char *)NULL);
    {
        int fd = open(ready, O_WRONLY | O_TRUNC | O_CLOEXEC);
        if (fd >= 0) {
            char mode = 'X';
            (void)write(fd, &mode, sizeof(mode));
            close(fd);
        }
    }
    _exit(91);
}

static int wait_for_marker(const char *path, char expected, int timeout_ms) {
    int elapsed = 0;
    while (elapsed <= timeout_ms) {
        char actual = '\0';
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            (void)read(fd, &actual, sizeof(actual));
            close(fd);
        }
        if (actual == expected) return 0;
        if (sleep_ms(20) != 0) return -1;
        elapsed += 20;
    }
    return -1;
}

static char read_marker(const char *path) {
    char actual = '\0';
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        (void)read(fd, &actual, sizeof(actual));
        close(fd);
    }
    return actual;
}

static int child_is_alive(pid_t child) {
    int status = 0;
    pid_t result = waitpid(child, &status, WNOHANG);
    return result == 0;
}

static int wait_for_child(pid_t child, int timeout_ms) {
    int elapsed = 0;
    int status = 0;
    while (elapsed <= timeout_ms) {
        pid_t result = waitpid(child, &status, WNOHANG);
        if (result == child) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (result < 0 && errno != EINTR) return 0;
        if (sleep_ms(50) != 0) return 0;
        elapsed += 50;
    }
    return 0;
}

static int session_contents_valid(const char *path) {
    char contents[64];
    ssize_t length;
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    do {
        length = read(fd, contents, sizeof(contents));
    } while (length < 0 && errno == EINTR);
    close(fd);
    return (length == 4 && memcmp(contents, "100\n", 4) == 0) ||
           (length == 10 && memcmp(contents, "101 ready\n", 10) == 0);
}

static int run_session_file_atomicity(const char *base) {
    char path[512];
    char temporary[512];
    int length = snprintf(path, sizeof(path), "%s/a2h_session_atomic_%ld",
                          base, (long)getpid());
    if (length < 0 || (size_t)length >= sizeof(path) ||
        snprintf(temporary, sizeof(temporary), "%s.tmp", path) < 0 ||
        write_session_contents(path, 100, 0) != 0) {
        fprintf(stderr, "FAIL session atomicity setup errno=%d\n", errno);
        return -1;
    }
    pid_t reader = fork();
    if (reader < 0) {
        unlink(path);
        fprintf(stderr, "FAIL session atomicity reader fork errno=%d\n", errno);
        return -1;
    }
    if (reader == 0) {
        stop_session_reader = 0;
        (void)signal(SIGTERM, request_session_reader_stop);
        while (!stop_session_reader) {
            if (!session_contents_valid(path)) _exit(1);
            (void)sleep_ms(1);
        }
        _exit(0);
    }

    int write_ok = 1;
    for (int step = 0; step < 1000; ++step) {
        int ready = step & 1;
        int session = ready ? 101 : 100;
        if (write_session_contents(path, session, ready) != 0) {
            write_ok = 0;
            break;
        }
        (void)sleep_ms(1);
    }
    (void)kill(reader, SIGTERM);
    int reader_ok = wait_for_child(reader, 3000);
    int final_ok = session_contents_valid(path);
    int temporary_absent = access(temporary, F_OK) != 0 && errno == ENOENT;
    unlink(path);
    if (!write_ok || !reader_ok || !final_ok || !temporary_absent) {
        fprintf(stderr,
                "FAIL session atomicity writes=%d reader=%d final=%d temp_absent=%d\n",
                write_ok, reader_ok, final_ok, temporary_absent);
        return -1;
    }
    return 0;
}

static void stop_child(pid_t child) {
    if (child <= 0) return;
    kill(child, SIGKILL);
    (void)waitpid(child, NULL, 0);
}

static int make_case(const char *base, const char *name, pid_t owner,
                     char *directory, size_t directory_size,
                     char *token, size_t token_size, mode_t leases_mode) {
    int length = snprintf(directory, directory_size, "%s/a2h_watch_%s_%ld",
                          base, name, (long)owner);
    if (length < 0 || (size_t)length >= directory_size) return -1;
    if (mkdir(directory, 0711) != 0) return -1;
    char leases[512];
    length = snprintf(leases, sizeof(leases), "%s/leases", directory);
    if (length < 0 || (size_t)length >= sizeof(leases) ||
        mkdir(leases, leases_mode) != 0) return -1;
    length = snprintf(token, token_size, "%s/lease", leases);
    if (length < 0 || (size_t)length >= token_size) return -1;
    return 0;
}

static void remove_case(const char *directory, const char *token) {
    char leases[512];
    char temporary[512];
    if (snprintf(leases, sizeof(leases), "%s/leases", directory) < 0 ||
        snprintf(temporary, sizeof(temporary), "%s.next", token) < 0) return;
    unlink(token);
    unlink(temporary);
    rmdir(leases);
    rmdir(directory);
}

static int run_inotify_replace_delete(const char *base, const char *ready,
                                      pid_t owner) {
    char directory[512];
    char token[512];
    if (make_case(base, "inotify", owner, directory, sizeof(directory),
                  token, sizeof(token), 0755) != 0 ||
        write_file(token, "policy\n", 0444) != 0 ||
        write_file(ready, "", 0666) != 0) {
        fprintf(stderr, "FAIL inotify case setup errno=%d\n", errno);
        return -1;
    }

    pid_t child = start_waiter(token, ready, 0);
    if (child <= 0) {
        fprintf(stderr, "FAIL inotify waiter fork errno=%d\n", errno);
        stop_child(child);
        remove_case(directory, token);
        return -1;
    }
    if (wait_for_marker(ready, 'I', 3000) != 0) {
        fprintf(stderr, "FAIL inotify ready marker=%d alive=%d\n",
                (int)read_marker(ready), child_is_alive(child));
        stop_child(child);
        remove_case(directory, token);
        return -1;
    }
    if (atomic_replace(token, "policy\n") != 0 || sleep_ms(150) != 0 ||
        !child_is_alive(child)) {
        fprintf(stderr, "FAIL inotify atomic-replace survival errno=%d\n", errno);
        stop_child(child);
        remove_case(directory, token);
        return -1;
    }
    if (unlink(token) != 0 || !wait_for_child(child, 2000)) {
        fprintf(stderr, "FAIL inotify delete release errno=%d\n", errno);
        stop_child(child);
        remove_case(directory, token);
        return -1;
    }
    remove_case(directory, token);
    return 0;
}

static int run_poll_renewal_and_transitions(const char *base, const char *ready,
                                            pid_t owner) {
    char directory[512];
    char token[512];
    if (make_case(base, "poll", owner, directory, sizeof(directory),
                  token, sizeof(token), 0711) != 0 ||
        write_file(token, "fallback:5\n", 0444) != 0 ||
        write_file(ready, "", 0666) != 0) {
        fprintf(stderr, "FAIL polling case setup errno=%d\n", errno);
        return -1;
    }

    pid_t child = start_waiter(token, ready, 1);
    if (child <= 0 || wait_for_marker(ready, 'P', 3000) != 0) {
        fprintf(stderr, "FAIL polling ready marker=%d alive=%d errno=%d\n",
                (int)read_marker(ready), child_is_alive(child), errno);
        stop_child(child);
        remove_case(directory, token);
        return -1;
    }
    if (sleep_ms(2500) != 0 || atomic_replace(token, "fallback:5\n") != 0 ||
        sleep_ms(3000) != 0 || !child_is_alive(child)) {
        fprintf(stderr, "FAIL polling same-value renewal\n");
        stop_child(child);
        remove_case(directory, token);
        return -1;
    }
    if (atomic_replace(token, "policy\n") != 0 || sleep_ms(3000) != 0 ||
        !child_is_alive(child)) {
        fprintf(stderr, "FAIL polling fallback-to-policy transition\n");
        stop_child(child);
        remove_case(directory, token);
        return -1;
    }
    if (atomic_replace(token, "fallback:5\n") != 0 ||
        !wait_for_child(child, 7000)) {
        fprintf(stderr, "FAIL polling policy-to-fallback expiry\n");
        stop_child(child);
        remove_case(directory, token);
        return -1;
    }
    remove_case(directory, token);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 6 && strcmp(argv[1], "--waiter") == 0) {
        lease_test_force_poll = strcmp(argv[2], "poll") == 0;
        lease_test_ready_path = argv[4];
        char *end = NULL;
        unsigned long expected_uid = strtoul(argv[5], &end, 10);
        if (end == argv[5] || *end != '\0' || getuid() != (uid_t)expected_uid) {
            int fd = open(lease_test_ready_path, O_WRONLY | O_TRUNC | O_CLOEXEC);
            if (fd >= 0) {
                char mode = 'U';
                (void)write(fd, &mode, sizeof(mode));
                close(fd);
            }
            return 90;
        }
        return wait_for_lease(argv[3]);
    }
    if (argc != 3) {
        fprintf(stderr, "usage: trigger_lease_watch_harness TEST_BASE APP_UID\n");
        return 2;
    }
    char *uid_end = NULL;
    unsigned long parsed_uid = strtoul(argv[2], &uid_end, 10);
    if (uid_end == argv[2] || *uid_end != '\0' || parsed_uid < 10000 ||
        parsed_uid > UINT32_MAX) {
        fprintf(stderr, "FAIL invalid app UID\n");
        return 1;
    }
    char ready[512];
    int length = snprintf(ready, sizeof(ready), "%s/a2h_watch_ready_%ld",
                          argv[1], (long)getpid());
    if (length < 0 || (size_t)length >= sizeof(ready) ||
        write_file(ready, "", 0666) != 0) {
        fprintf(stderr, "FAIL trigger-lease-ready-file\n");
        return 1;
    }

    test_binary_path = argv[0];
    test_app_uid = (uid_t)parsed_uid;
    pid_t owner = getpid();
    int ok = run_session_file_atomicity(argv[1]) == 0 &&
             run_inotify_replace_delete(argv[1], ready, owner) == 0 &&
             run_poll_renewal_and_transitions(argv[1], ready, owner) == 0;
    unlink(ready);
    if (!ok) {
        fprintf(stderr, "FAIL trigger lease directory watch / polling renewal\n");
        return 1;
    }
    puts("PASS trigger lease atomic replacement and polling renewal");
    return 0;
}
