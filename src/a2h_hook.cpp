// A2HHook runtime extension for the six-entry vendor is_A2H_app function.
// The library is loaded into the MediaTek audio service by LD_PRELOAD.  It
// interposes strcmp only for return addresses belonging to is_A2H_app, so the
// extra four whitelist entries cannot change unrelated HAL comparisons.

#include <dlfcn.h>
#include <fcntl.h>
#include <android/log.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdarg>

#include "dobby.h"

#include <cstdint>
#include <cstring>

namespace {

using strcmp_fn = int (*)(const char *, const char *);

constexpr const char *kPackagesPath = "/data/local/tmp/a2h_packages.txt";
constexpr const char *kStatePath = "/data/local/tmp/a2h_state";
constexpr size_t kMaxExtra = 4;
constexpr size_t kPackageSize = 64;
constexpr char kTag[] = "A2HHook";

pthread_once_t g_resolve_once = PTHREAD_ONCE_INIT;
strcmp_fn g_real_strcmp = nullptr;
strcmp_fn g_original_strcmp = nullptr;
pthread_mutex_t g_config_lock = PTHREAD_MUTEX_INITIALIZER;
char g_extra[kMaxExtra][kPackageSize];
size_t g_extra_count = 0;
time_t g_config_mtime = 0;
thread_local bool g_in_hook = false;
thread_local bool g_loading = false;
thread_local bool g_logging = false;

void logi(const char *fmt, ...) {
    if (g_logging) return;
    g_logging = true;
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_INFO, kTag, fmt, ap);
    va_end(ap);
    g_logging = false;
}

void logw(const char *fmt, ...) {
    if (g_logging) return;
    g_logging = true;
    va_list ap;
    va_start(ap, fmt);
    __android_log_vprint(ANDROID_LOG_WARN, kTag, fmt, ap);
    va_end(ap);
    g_logging = false;
}

int raw_compare(const char *a, const char *b) {
    const unsigned char *lhs = reinterpret_cast<const unsigned char *>(a);
    const unsigned char *rhs = reinterpret_cast<const unsigned char *>(b);
    if (!lhs || !rhs) return lhs == rhs ? 0 : (lhs ? 1 : -1);
    while (*lhs && *lhs == *rhs) {
        ++lhs;
        ++rhs;
    }
    return static_cast<int>(*lhs) - static_cast<int>(*rhs);
}

void resolve_real_strcmp() {
    g_loading = true;
    g_real_strcmp = reinterpret_cast<strcmp_fn>(dlsym(RTLD_NEXT, "strcmp"));
    g_loading = false;
    if (g_real_strcmp) logi("resolved strcmp hook");
}

bool valid_package(const char *value) {
    if (!value || !*value) return false;
    size_t length = 0;
    bool dot = false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
        if (++length >= kPackageSize) return false;
        if (*p == '.') dot = true;
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '.')) return false;
    }
    return dot;
}

bool whitelist_mode() {
    char state[16] = {};
    int fd = open(kStatePath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return true;
    ssize_t n = read(fd, state, sizeof(state) - 1);
    close(fd);
    return n <= 0 || std::strncmp(state, "enabled", 7) != 0;
}

void reload_extra_locked(time_t mtime) {
    g_extra_count = 0;
    int fd = open(kPackagesPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        g_config_mtime = mtime;
        return;
    }
    char data[4096] = {};
    ssize_t size = read(fd, data, sizeof(data) - 1);
    close(fd);
    if (size <= 0) {
        g_config_mtime = mtime;
        return;
    }
    size_t line = 0;
    char value[kPackageSize] = {};
    size_t value_len = 0;
    for (ssize_t i = 0; i <= size && g_extra_count < kMaxExtra; ++i) {
        char ch = (i == size) ? '\n' : data[i];
        if (ch == '\r') continue;
        if (ch != '\n') {
            if (value_len + 1 < sizeof(value)) value[value_len++] = ch;
            continue;
        }
        value[value_len] = '\0';
        if (line >= 6 && valid_package(value)) {
            std::strncpy(g_extra[g_extra_count], value, kPackageSize - 1);
            g_extra[g_extra_count][kPackageSize - 1] = '\0';
            ++g_extra_count;
        }
        value_len = 0;
        ++line;
    }
    g_config_mtime = mtime;
}

void reload_extra_if_needed() {
    struct stat st = {};
    time_t mtime = (stat(kPackagesPath, &st) == 0) ? st.st_mtime : 0;
    if (mtime == g_config_mtime) return;
    pthread_mutex_lock(&g_config_lock);
    if (mtime != g_config_mtime) reload_extra_locked(mtime);
    pthread_mutex_unlock(&g_config_lock);
}

bool from_a2h_function(void *return_address) {
    Dl_info info = {};
    if (!return_address || dladdr(return_address, &info) == 0 || !info.dli_fbase || !info.dli_fname)
        return false;
    if (std::strstr(info.dli_fname, "audio.primary.") == nullptr) return false;
    uintptr_t offset = reinterpret_cast<uintptr_t>(return_address) -
                       reinterpret_cast<uintptr_t>(info.dli_fbase);
    // Return addresses immediately follow the six BL strcmp instructions.
    return offset >= 0x3e3fe0 && offset <= 0x3e4048;
}

bool extra_match(const char *package_name) {
    if (!package_name || !whitelist_mode()) return false;
    reload_extra_if_needed();
    pthread_mutex_lock(&g_config_lock);
    for (size_t i = 0; i < g_extra_count; ++i) {
        if (raw_compare(package_name, g_extra[i]) == 0) {
            logi("extra hit: %s", package_name);
            pthread_mutex_unlock(&g_config_lock);
            return true;
        }
    }
    pthread_mutex_unlock(&g_config_lock);
    return false;
}

}  // namespace

int hooked_strcmp(const char *lhs, const char *rhs) {
    if (g_in_hook || g_loading) return raw_compare(lhs, rhs);
    void *caller = __builtin_return_address(0);
    g_in_hook = true;
    pthread_once(&g_resolve_once, resolve_real_strcmp);
    int result = g_original_strcmp ? g_original_strcmp(lhs, rhs) :
                 (g_real_strcmp ? g_real_strcmp(lhs, rhs) : raw_compare(lhs, rhs));
    if (result != 0 && from_a2h_function(caller) && extra_match(lhs)) {
        logi("cmp overridden for %s", lhs ? lhs : "(null)");
        result = 0;
    }
    g_in_hook = false;
    return result;
}

__attribute__((constructor)) static void install_hook() {
    pthread_once(&g_resolve_once, resolve_real_strcmp);
    if (!g_real_strcmp) {
        logw("failed to resolve strcmp");
        return;
    }
    logi("installing strcmp hook");
    int rc = DobbyHook(reinterpret_cast<void *>(g_real_strcmp),
                    reinterpret_cast<void *>(hooked_strcmp),
                    reinterpret_cast<void **>(&g_original_strcmp));
    if (rc == 0) logi("hook installed");
    else logw("hook install failed: %d", rc);
}

// Keep the Zygisk Next export available for installations that also load the
// module through Zygisk.  The runtime extension itself is driven by preload.
extern "C" __attribute__((visibility("default"))) void zygisk_module_entry(void *) {}
