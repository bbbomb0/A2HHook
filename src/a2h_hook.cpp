// A2HHook runtime extension for vendor is_A2H_app.
// The library is loaded into the MediaTek audio service by LD_PRELOAD / Zygisk.
// It interposes strcmp only for return addresses belonging to the active
// is_A2H_app window, so extra whitelist entries can join the same call chain
// without affecting unrelated HAL comparisons.

#include <dlfcn.h>
#include <fcntl.h>
#include <android/log.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstdarg>

#include "dobby.h"

#include <cstdint>
#include <cstring>

namespace {

using strcmp_fn = int (*)(const char *, const char *);

constexpr const char *kPackagesPath = "/data/local/tmp/a2h_packages.txt";
constexpr const char *kFuncHintPath = "/data/adb/modules/a2h_hook/config/func_off";
constexpr const char *kStatePath = "/data/local/tmp/a2h_state";
constexpr const char *kStateFallbackPath = "/data/adb/modules/a2h_hook/config/state";
constexpr size_t kMaxExtra = 4;
constexpr size_t kPackageSize = 64;
// Used only before the patcher has written config/func_off.  It covers the
// observed OS2/OS3 function locations long enough to load the real hint; once
// loaded, from_a2h_function() returns to the narrow per-build window.
constexpr uintptr_t kBootstrapFuncStart = 0x3e3f00;
constexpr uintptr_t kBootstrapFuncEnd = 0x3e4600;
constexpr char kTag[] = "A2HHook";

pthread_once_t g_resolve_once = PTHREAD_ONCE_INIT;
strcmp_fn g_real_strcmp = nullptr;
strcmp_fn g_original_strcmp = nullptr;
pthread_mutex_t g_config_lock = PTHREAD_MUTEX_INITIALIZER;
char g_extra[kMaxExtra][kPackageSize];
size_t g_extra_count = 0;
uint64_t g_config_stamp = 0;
uintptr_t g_func_off = 0;
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

uint64_t file_stamp(const char *path) {
    struct stat st = {};
    if (stat(path, &st) != 0) return 0;
    uint64_t stamp = static_cast<uint64_t>(st.st_mtime);
    stamp = stamp * 1315423911u + static_cast<uint64_t>(st.st_size);
#if defined(__BIONIC__) || defined(__linux__)
    stamp = stamp * 1315423911u + static_cast<uint64_t>(st.st_mtim.tv_nsec);
#endif
    return stamp;
}

uint64_t combined_stamp(uint64_t package_stamp, uint64_t hint_stamp) {
    if (package_stamp == 0 && hint_stamp == 0) return 0;
    // Do not use max(package_stamp, hint_stamp): a changed file can hash to a
    // smaller value and then fail to invalidate the cached package list.
    uint64_t mixed = package_stamp ^ (hint_stamp + 0x9e3779b97f4a7c15ULL +
                                      (package_stamp << 6) + (package_stamp >> 2));
    return mixed ? mixed : 1;
}

uintptr_t read_func_hint() {
    int fd = open(kFuncHintPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    char buf[64] = {};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return 0;
    char *end = nullptr;
    uintptr_t value = static_cast<uintptr_t>(std::strtoull(buf, &end, 16));
    if (end == buf || value < 0x10000 || value > 0x2000000) return 0;
    return value;
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
    if (fd < 0) fd = open(kStateFallbackPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return true;
    ssize_t n = read(fd, state, sizeof(state) - 1);
    close(fd);
    return n <= 0 || std::strncmp(state, "enabled", 7) != 0;
}

void reload_extra_locked(uint64_t stamp) {
    g_extra_count = 0;
    int fd = open(kPackagesPath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        g_func_off = read_func_hint();
        g_config_stamp = stamp;
        return;
    }
    char data[4096] = {};
    ssize_t size = read(fd, data, sizeof(data) - 1);
    close(fd);
    if (size <= 0) {
        g_func_off = read_func_hint();
        g_config_stamp = stamp;
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
    g_func_off = read_func_hint();
    if (g_func_off) {
        logi("loaded func_off hint: 0x%lx", static_cast<unsigned long>(g_func_off));
    } else {
        logw("func_off hint missing, using fallback window");
    }
    g_config_stamp = stamp;
}

void reload_extra_if_needed() {
    uint64_t pm = file_stamp(kPackagesPath);
    uint64_t fm = file_stamp(kFuncHintPath);
    uint64_t stamp = combined_stamp(pm, fm);
    if (stamp == g_config_stamp) return;
    pthread_mutex_lock(&g_config_lock);
    if (stamp != g_config_stamp) reload_extra_locked(stamp);
    pthread_mutex_unlock(&g_config_lock);
}

bool from_a2h_function(void *return_address) {
    Dl_info info = {};
    if (!return_address || dladdr(return_address, &info) == 0 || !info.dli_fbase || !info.dli_fname)
        return false;
    if (std::strstr(info.dli_fname, "audio.primary.") == nullptr) return false;
    uintptr_t offset = reinterpret_cast<uintptr_t>(return_address) -
                       reinterpret_cast<uintptr_t>(info.dli_fbase);
    uintptr_t start = g_func_off ? (g_func_off > 0x80 ? g_func_off - 0x80 : 0) : kBootstrapFuncStart;
    uintptr_t end = g_func_off ? (g_func_off + 0x180) : kBootstrapFuncEnd;
    return offset >= start && offset <= end;
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
    // Load any persisted package list and function hint before the first
    // strcmp call.  On a first boot the hint may not exist yet; the bootstrap
    // window above allows a later call to reload it after the patcher runs.
    reload_extra_if_needed();
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
