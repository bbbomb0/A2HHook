// a2h_patch v1.5.5-fix - universal signature/ELF scan + 10-slot whitelist
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/uio.h>
#include <sys/wait.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <linux/elf.h>

#ifndef PTRACE_GETREGSET
#define PTRACE_GETREGSET 0x4204
#endif
#ifndef PTRACE_SETREGSET
#define PTRACE_SETREGSET 0x4205
#endif
#ifndef PTRACE_GETSIGINFO
#define PTRACE_GETSIGINFO 0x4202
#endif
#ifndef NT_PRSTATUS
#define NT_PRSTATUS 1
#endif
#ifndef __NR_membarrier
#define __NR_membarrier 283
#endif
#ifndef MEMBARRIER_CMD_GLOBAL
#define MEMBARRIER_CMD_GLOBAL (1 << 0)
#endif
#ifndef MEMBARRIER_CMD_GLOBAL_EXPEDITED
#define MEMBARRIER_CMD_GLOBAL_EXPEDITED (1 << 1)
#endif
#ifndef PTRACE_SEIZE
#define PTRACE_SEIZE 0x4206
#endif
#ifndef PTRACE_INTERRUPT
#define PTRACE_INTERRUPT 0x4207
#endif
#ifndef PTRACE_EVENT_STOP
#define PTRACE_EVENT_STOP 128
#endif
#ifndef __WALL
#define __WALL 0x40000000
#endif

#define PTRACE_DETACH 17
#define PTRACE_CONT 7
#define PTRACE_PEEKDATA 2
#define PTRACE_POKETEXT 4
#define PTRACE_POKEDATA 5
#define MAX_SLOTS 10
#define A2H_VERSION "1.5.5-fix"
#define WHITELIST_CAVE_BYTES (MAX_SLOTS * 64 + 16 + MAX_SLOTS * 8 + 32)
#define WHITELIST_STUB_WORDS 19
#define WHITELIST_STUB_BYTES (WHITELIST_STUB_WORDS * sizeof(uint32_t))

struct user_pt_regs_a64 {
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

static uint32_t u32x(uint32_t v) { return v ^ 0xA5C31F77u; }
static uintptr_t upx(uint32_t cloaked) { return (uintptr_t)u32x(cloaked); }
static void unmix(char *dst, const unsigned char *src, size_t n, unsigned char key) {
    for (size_t i = 0; i < n; ++i) dst[i] = (char)(src[i] ^ (unsigned char)(key + (unsigned char)i));
    dst[n] = 0;
}
static void name_hal_primary(char *dst) {
    static const unsigned char e[] = {0x3b,0x2e,0x38,0x34,0x31,0x71,0x10,0x13,0x0b,0x0e,0x05,0x17,0x1f,0x49,0x05,0x0c,0x0e,0x02,0x0d,0x19,0x0b,0x04,0x5e,0x02,0x1d};
    unmix(dst, e, sizeof(e), 0x5A);
}
static void name_hal_mt(char *dst) {
    static const unsigned char e[] = {0x3b,0x2e,0x38,0x34,0x31,0x71,0x10,0x13,0x0b,0x0e,0x05,0x17,0x1f,0x49,0x05,0x1d,0x5c,0x52,0x55,0x5c,0x40,0x1c,0x1f};
    unmix(dst, e, sizeof(e), 0x5A);
}
static void name_svc_short(char *dst) {
    snprintf(dst, 64, "%s", "android.hardware.audio.service-aidl.mediatek");
}
static void pkg_default(int idx, char *dst, size_t cap) {
    static const unsigned char enc[6][32] = {
        {0x5a,0x55,0x56,0x12,0x56,0x4b,0x58,0x2f,0x34,0x6c,0x22,0x2a,0x21,0x34,0x28,0x21,0x2d},
        {0x5a,0x55,0x56,0x12,0x49,0x5b,0x51,0x23,0x24,0x2c,0x37,0x6a,0x34,0x37,0x2a,0x3d,0x3a,0x23,0x28},
        {0x5a,0x55,0x56,0x12,0x53,0x5b,0x4b,0x25,0x20,0x31,0x26,0x6a,0x26,0x2a,0x28,0x3d,0x2d,0x27,0x3e,0x3f,0x24,0x2d},
        {0x5a,0x54,0x15,0x57,0x48,0x49,0x50,0x6e,0x31,0x2e,0x22,0x3d,0x20,0x34},
        {0x5a,0x55,0x56,0x12,0x50,0x57,0x4a,0x29,0x6f,0x32,0x2f,0x25,0x3c,0x23,0x35},
        {0x5a,0x55,0x56,0x12,0x51,0x4b,0x51,0x21,0x6f,0x2f,0x36,0x37,0x2c,0x25}
    };
    static const unsigned char lens[6] = {17,19,22,14,15,14};
    if (!dst || !cap || idx < 0 || idx >= 6) return;
    char tmp[64];
    unmix(tmp, enc[idx], lens[idx], 0x39);
    snprintf(dst, cap, "%s", tmp);
}

typedef struct { uint32_t off_x; int max_len; const char *label; } slot_t;
typedef struct {
    const char *name; const char *hint; uintptr_t func_off; size_t func_size;
    uintptr_t slot_off[6]; int slot_len[6];
} profile_t;
static const profile_t PROFILES[] = {
    {"os3_0_302_0x3e3fc0","HyperOS3.0.302 verified",0x3E3FC0,160,{0xADC9C,0xB19F4,0xB507E,0xBC5CC,0xBC5DB,0xFCFFA},{17,19,22,14,15,14}},
    {"os3_0_305_0x3e4020","HyperOS3.0.305 static",0x3E4020,160,{0xADC8C,0xB19E4,0xB506E,0xBC5BC,0xBC5CB,0xFD028},{17,19,22,14,15,14}},
    {"os2_0_218_0x3e4280","HyperOS2.0.218 static",0x3E4280,160,{0xADE45,0xB1BBB,0xB5280,0xBC7CE,0xBC7DD,0xFD0FF},{17,19,22,14,15,14}},
};
static slot_t slots[MAX_SLOTS];
static uintptr_t g_func_off=0x3E3FC0,g_ptr_off=0x437200,g_stub_mark=0x437100,g_rw_start=0,g_rw_end=0,g_rx_start=0,g_rx_end=0;
static size_t g_func_capacity=0;
static uintptr_t g_elf_tail_start=0,g_elf_tail_end=0,g_elf_tail_candidate=0;
static char g_libname[64]={0}, g_libpath[512]={0}, g_profile[48]={0}, g_profile_hint[48]={0}, g_locate_method[32]={0}, g_scan_kind[24]={0};
static unsigned int g_lib_dev_major=0,g_lib_dev_minor=0;
static unsigned long long g_lib_inode=0;
static int g_attached=0;
typedef struct {
    pid_t tid;
    int stopped;
    int resume_signal;
    int regs_restore_pending;
    struct user_pt_regs_a64 saved_regs;
    int affinity_restore_pending;
    cpu_set_t saved_affinity;
} trace_thread_t;
typedef struct {
    pid_t tgid;
    pid_t control_tid;
    trace_thread_t *threads;
    size_t count;
    size_t capacity;
} trace_group_t;
static trace_group_t g_trace_group={0};
static uintptr_t g_cache_helper_addr=0;
static pid_t g_cache_helper_pid=-1;
static uintptr_t g_cache_helper_base=0;
static int g_cache_helper_ready=0;
static int g_trace_compromised=0;
enum { ICACHE_MEMBARRIER = 1, ICACHE_REMOTE_IVAU = 2 };
static int g_icache_methods=0;
static int g_icache_failures=0;
#ifdef A2H_TEST_FAULT_INJECTION
static int g_test_icache_override=-1;
static int g_test_icache_fail_call=0;
static int g_test_icache_calls=0;
#endif
static uintptr_t slot_off(int i){return upx(slots[i].off_x);} 
static void set_slot_off(int i,uintptr_t off){slots[i].off_x=(uint32_t)off^0xA5C31F77u;} 
static uintptr_t k_func_off(void){return g_func_off;} 
static int load_cave_hint(uintptr_t *out);
static void save_cave_hint(uintptr_t off);
static long now_ms(void);
static int restore_trace_regs(pid_t tid, struct user_pt_regs_a64 *backup);
static int restore_thread_affinity(pid_t tid, const cpu_set_t *original);
static const unsigned char SIG8[8]={0xe0,0x04,0x00,0xb4,0xfd,0x7b,0xbe,0xa9};
static const unsigned char PATCH[8]={0x20,0x00,0x80,0x52,0xc0,0x03,0x5f,0xd6};
static const unsigned char STOCK_TAIL8[8]={0xf3,0x0b,0x00,0xf9,0xfd,0x03,0x00,0x91};
static const unsigned char GLOBAL_TAIL8[8]={0x1f,0x20,0x03,0xd5,0x1f,0x20,0x03,0xd5};
static const unsigned char GLOBAL_PATCH[16]={
    0x20,0x00,0x80,0x52,0xc0,0x03,0x5f,0xd6,
    0x1f,0x20,0x03,0xd5,0x1f,0x20,0x03,0xd5
};
static const unsigned char WHITELIST_MARKER[16]={
    'A','2','H','1','W','L','S','T','0','0','0','2',0,0,0,0
};
static const uint32_t WHITELIST_STUB_FIXED_TAIL[WHITELIST_STUB_WORDS - 2]={
    0xB40001E0u, 0x52800142u, 0x340001A2u, 0xF8408423u,
    0xB4000123u, 0xAA0003E4u, 0x38401485u, 0x38401466u,
    0x6B0600BFu, 0x54000081u, 0x35FFFF85u, 0x52800020u,
    0xD65F03C0u, 0x51000442u, 0x17FFFFF4u, 0x52800000u,
    0xD65F03C0u
};
static int patched_global_stub_tail(const unsigned char *head, size_t n) {
    if (!head || n < 16 || memcmp(head, PATCH, sizeof(PATCH)) != 0) return 0;
    uint32_t w2 = (uint32_t)head[8] | ((uint32_t)head[9] << 8) |
                  ((uint32_t)head[10] << 16) | ((uint32_t)head[11] << 24);
    uint32_t w3 = (uint32_t)head[12] | ((uint32_t)head[13] << 8) |
                  ((uint32_t)head[14] << 16) | ((uint32_t)head[15] << 24);
    return ((w2 & 0xFF00001Fu) == 0xB4000000u) && w3 == 0x52800142u;
}
static int patched_global_tail_ok(const unsigned char *head, size_t n) {
    if (!head || n < 16 || memcmp(head, PATCH, sizeof(PATCH)) != 0) return 0;
    return memcmp(head + sizeof(PATCH), STOCK_TAIL8, sizeof(STOCK_TAIL8)) == 0 ||
           memcmp(head + sizeof(PATCH), GLOBAL_TAIL8, sizeof(GLOBAL_TAIL8)) == 0 ||
           patched_global_stub_tail(head, n);
}
static uint32_t load_u32le(const unsigned char *h) {
    return (uint32_t)h[0] | ((uint32_t)h[1] << 8) | ((uint32_t)h[2] << 16) | ((uint32_t)h[3] << 24);
}
static int exact_whitelist_stub_suffix(const unsigned char *h, size_t n,
                                       size_t first_word) {
    if (!h || first_word < 2 || first_word >= WHITELIST_STUB_WORDS ||
        n < WHITELIST_STUB_BYTES) return 0;
    for (size_t word = first_word; word < WHITELIST_STUB_WORDS; ++word) {
        if (load_u32le(h + word * sizeof(uint32_t)) !=
            WHITELIST_STUB_FIXED_TAIL[word - 2]) return 0;
    }
    return 1;
}
static int is_stub_head(const unsigned char *h, size_t n) {
    if (!h || n < 12) return 0;
    uint32_t w0 = load_u32le(h);
    uint32_t w1 = load_u32le(h + 4);
    uint32_t w2 = load_u32le(h + 8);
    if (((w0 & 0x9F00001Fu) != 0x90000001u) || ((w1 & 0xFFC003FFu) != 0x91000021u)) return 0;
    if (w2 == 0x52800142u) return 1; // v1.5.x: ADRP; ADD; MOV w2,#10
    if (n >= 16) {
        uint32_t w3 = load_u32le(h + 12);
        if (((w2 & 0xFF00001Fu) == 0xB4000000u) && w3 == 0x52800142u) return 1; // v1.6+: CBZ x0; MOV w2,#10
    }
    return 0;
}
static long ptrace_call(int req,pid_t pid,void *addr,void *data){return syscall(117,req,pid,addr,data);} 
static ssize_t vm_read(pid_t pid,uintptr_t addr,void *buf,size_t len){struct iovec l={buf,len},r={(void*)addr,len};return syscall(270,pid,&l,1,&r,1,0);} 
static ssize_t vm_write(pid_t pid,uintptr_t addr,const void *buf,size_t len){struct iovec l={(void*)buf,len},r={(void*)addr,len};return syscall(271,pid,&l,1,&r,1,0);} 

static int trace_group_index(pid_t tid) {
    for (size_t i = 0; i < g_trace_group.count; ++i) {
        if (g_trace_group.threads[i].tid == tid) return (int)i;
    }
    return -1;
}

static int trace_wait_stop(pid_t tid, long timeout_ms, int *status_out) {
    long deadline = now_ms() + timeout_ms;
    for (;;) {
        int status = 0;
        errno = 0;
        pid_t waited = waitpid(tid, &status, __WALL | WNOHANG);
        if (waited == tid) {
            if (status_out) *status_out = status;
            if (WIFSTOPPED(status)) return 1;
            errno = ESRCH;
            return 0;
        }
        if (waited < 0 && errno != EINTR) return 0;
        if (now_ms() >= deadline) {
            errno = ETIMEDOUT;
            return 0;
        }
        struct timespec pause = {0, 2 * 1000 * 1000};
        while (nanosleep(&pause, &pause) != 0 && errno == EINTR) {}
    }
}

static int trace_tid_exists(pid_t tid) {
    if (g_trace_group.tgid <= 0 || tid <= 0) return 0;
    char path[96];
    snprintf(path, sizeof(path), "/proc/%d/task/%d", g_trace_group.tgid, tid);
    return access(path, F_OK) == 0;
}

static int trace_resume_signal(pid_t tid, int status, int helper_context) {
    if (!WIFSTOPPED(status)) return 0;
    int signal = WSTOPSIG(status);
    unsigned int event = (unsigned int)status >> 16;
    if (event == PTRACE_EVENT_STOP) {
        return signal == SIGTRAP ? 0 : signal;
    }
    if (!helper_context) return signal;
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    int has_info = ptrace_call(PTRACE_GETSIGINFO, tid, NULL, &info) == 0;
    if (has_info && info.si_code <= 0) return signal;
    switch (signal) {
        case SIGILL:
        case SIGFPE:
        case SIGSEGV:
        case SIGBUS:
        case SIGTRAP:
        case SIGSYS:
            return 0;
        default:
            return signal;
    }
}

static void trace_capture_resume_signal(trace_thread_t *thread, int status,
                                        int helper_context) {
    if (!thread) return;
    int signal = trace_resume_signal(thread->tid, status, helper_context);
    if (!signal) return;
    if (!thread->resume_signal) {
        thread->resume_signal = signal;
    } else if (thread->resume_signal != signal) {
        fprintf(stderr,
                "[a2h_patch] ptrace pending signal collision tid=%d keep=%d drop=%d\n",
                thread->tid, thread->resume_signal, signal);
    }
}

static int collect_task_tids(pid_t pid, pid_t **out, size_t *count_out) {
    if (!out || !count_out) return 0;
    *out = NULL;
    *count_out = 0;
    char task_path[64];
    snprintf(task_path, sizeof(task_path), "/proc/%d/task", pid);
    DIR *dir = opendir(task_path);
    if (!dir) return 0;
    size_t count = 0, capacity = 0;
    pid_t *tids = NULL;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        char *end = NULL;
        errno = 0;
        long value = strtol(entry->d_name, &end, 10);
        if (errno || !end || *end || value <= 0 || value > INT_MAX) continue;
        if (count == capacity) {
            size_t next = capacity ? capacity * 2 : 32;
            pid_t *grown = (pid_t *)realloc(tids, next * sizeof(*tids));
            if (!grown) {
                free(tids);
                closedir(dir);
                return 0;
            }
            tids = grown;
            capacity = next;
        }
        tids[count++] = (pid_t)value;
    }
    closedir(dir);
    if (!count) {
        free(tids);
        return 0;
    }
    for (size_t i = 0; i < count; ++i) {
        if (tids[i] == pid) {
            pid_t first = tids[0];
            tids[0] = tids[i];
            tids[i] = first;
            break;
        }
    }
    *out = tids;
    *count_out = count;
    return 1;
}

static int trace_group_reserve(size_t need) {
    if (need <= g_trace_group.capacity) return 1;
    size_t capacity = g_trace_group.capacity ? g_trace_group.capacity * 2 : 32;
    while (capacity < need) capacity *= 2;
    trace_thread_t *grown = (trace_thread_t *)realloc(
        g_trace_group.threads, capacity * sizeof(*grown));
    if (!grown) return 0;
    g_trace_group.threads = grown;
    g_trace_group.capacity = capacity;
    return 1;
}

static void trace_group_release(void) {
    free(g_trace_group.threads);
    memset(&g_trace_group, 0, sizeof(g_trace_group));
}

static int trace_force_stop(trace_thread_t *thread) {
    if (!thread) return 0;
    if (!trace_tid_exists(thread->tid)) return -1;
    if (thread->stopped) return 1;
    for (int attempt = 0; attempt < 3; ++attempt) {
        (void)ptrace_call(PTRACE_INTERRUPT, thread->tid, NULL, NULL);
        int status = 0;
        if (trace_wait_stop(thread->tid, 250, &status)) {
            thread->stopped = 1;
            trace_capture_resume_signal(thread, status,
                                        thread->regs_restore_pending);
            return 1;
        }
        if (!trace_tid_exists(thread->tid)) return -1;
    }
    return 0;
}

static int trace_group_detach_all(void) {
    int all_detached = 1;
    for (size_t i = g_trace_group.count; i > 0; --i) {
        trace_thread_t *thread = &g_trace_group.threads[i - 1];
        if (!trace_tid_exists(thread->tid)) {
            thread->regs_restore_pending = 0;
            thread->affinity_restore_pending = 0;
            continue;
        }
        if (thread->affinity_restore_pending) {
            if (restore_thread_affinity(thread->tid, &thread->saved_affinity)) {
                thread->affinity_restore_pending = 0;
            } else if (!trace_tid_exists(thread->tid)) {
                thread->affinity_restore_pending = 0;
            } else {
                fprintf(stderr,
                        "[a2h_patch] ptrace detach affinity restore tid=%d retry-after-stop errno=%d\n",
                        thread->tid, errno);
            }
        }
        int stop_state = trace_force_stop(thread);
        if (stop_state < 0) continue;
        if (stop_state == 0) {
            fprintf(stderr,
                    "[a2h_patch] ptrace detach cannot stop tid=%d\n", thread->tid);
            all_detached = 0;
            g_trace_compromised = 1;
            continue;
        }
        if (thread->affinity_restore_pending) {
            if (restore_thread_affinity(thread->tid, &thread->saved_affinity)) {
                thread->affinity_restore_pending = 0;
            } else {
                fprintf(stderr,
                        "[a2h_patch] ptrace detach stopped affinity restore tid=%d FAIL errno=%d\n",
                        thread->tid, errno);
                all_detached = 0;
                g_trace_compromised = 1;
                continue;
            }
        }
        if (thread->regs_restore_pending) {
            if (restore_trace_regs(thread->tid, &thread->saved_regs)) {
                thread->regs_restore_pending = 0;
            } else {
                fprintf(stderr,
                        "[a2h_patch] ptrace detach regs restore tid=%d FAIL errno=%d\n",
                        thread->tid, errno);
                all_detached = 0;
                g_trace_compromised = 1;
                continue;
            }
        }
        int detached = 0;
        errno = 0;
        for (int attempt = 0; attempt < 3 && !detached; ++attempt) {
            long detached_rc = ptrace_call(
                PTRACE_DETACH, thread->tid, NULL,
                (void *)(uintptr_t)thread->resume_signal);
            if (detached_rc == 0 || !trace_tid_exists(thread->tid)) {
                detached = 1;
            }
        }
        if (!detached) {
            fprintf(stderr, "[a2h_patch] ptrace detach tid=%d FAIL errno=%d stopped=%d\n",
                    thread->tid, errno, thread->stopped);
            all_detached = 0;
            g_trace_compromised = 1;
        }
    }
    trace_group_release();
    g_attached = 0;
    return all_detached;
}

static int trace_seize_and_stop(pid_t tid, long deadline) {
    if (!trace_group_reserve(g_trace_group.count + 1)) return -1;
    trace_thread_t *thread = &g_trace_group.threads[g_trace_group.count++];
    memset(thread, 0, sizeof(*thread));
    thread->tid = tid;
    if (ptrace_call(PTRACE_SEIZE, tid, NULL, NULL) < 0) {
        int saved = errno;
        g_trace_group.count--;
        if (saved == ESRCH) return 0;
        errno = saved;
        return -1;
    }
    if (ptrace_call(PTRACE_INTERRUPT, tid, NULL, NULL) < 0) {
        if (errno == ESRCH) {
            g_trace_group.count--;
            return 0;
        }
        return -1;
    }
    long remaining = deadline - now_ms();
    if (remaining <= 0) {
        errno = ETIMEDOUT;
        return -1;
    }
    int status = 0;
    if (!trace_wait_stop(tid, remaining, &status)) {
        if (errno == ESRCH) {
            g_trace_group.count--;
            return 0;
        }
        return -1;
    }
    thread->stopped = 1;
    trace_capture_resume_signal(thread, status, 0);
    return 1;
}

static int trace_attach(pid_t pid) {
    if (!trace_group_detach_all() || g_trace_compromised) return -1;
    g_trace_group.tgid = pid;
    long deadline = now_ms() + 3000;
    int stable_passes = 0;
    for (int pass = 0; pass < 16 && now_ms() < deadline; ++pass) {
        pid_t *tids = NULL;
        size_t count = 0;
        if (!collect_task_tids(pid, &tids, &count)) goto fail;
        int added = 0;
        for (size_t i = 0; i < count; ++i) {
            if (trace_group_index(tids[i]) >= 0) continue;
            int seized = trace_seize_and_stop(tids[i], deadline);
            if (seized < 0) {
                fprintf(stderr, "[a2h_patch] ptrace freeze tid=%d FAIL errno=%d\n",
                        tids[i], errno);
                free(tids);
                goto fail;
            }
            if (seized > 0) added++;
        }
        free(tids);

        pid_t *verify = NULL;
        size_t verify_count = 0;
        if (!collect_task_tids(pid, &verify, &verify_count)) goto fail;
        int missing = 0;
        for (size_t i = 0; i < verify_count; ++i) {
            int index = trace_group_index(verify[i]);
            if (index < 0 || !g_trace_group.threads[index].stopped) {
                missing = 1;
                break;
            }
        }
        free(verify);
        if (!missing && !added) {
            if (++stable_passes >= 2) {
                int leader = trace_group_index(pid);
                if (leader >= 0 && g_trace_group.threads[leader].stopped) {
                    g_trace_group.control_tid = pid;
                } else {
                    for (size_t i = 0; i < g_trace_group.count; ++i) {
                        if (g_trace_group.threads[i].stopped) {
                            g_trace_group.control_tid = g_trace_group.threads[i].tid;
                            break;
                        }
                    }
                }
                if (!g_trace_group.control_tid) goto fail;
                fprintf(stderr,
                        "[a2h_patch] ptrace freeze OK tids=%lu control=%d passes=%d\n",
                        (unsigned long)g_trace_group.count,
                        g_trace_group.control_tid, pass + 1);
                return 0;
            }
        } else {
            stable_passes = 0;
        }
    }
    errno = ETIMEDOUT;
fail:
    fprintf(stderr, "[a2h_patch] ptrace freeze rollback tids=%lu errno=%d\n",
            (unsigned long)g_trace_group.count, errno);
    (void)trace_group_detach_all();
    return -1;
}

static void trace_detach(pid_t pid) {
    if (g_trace_group.tgid && g_trace_group.tgid != pid) {
        fprintf(stderr, "[a2h_patch] ptrace detach tgid mismatch expected=%d got=%d\n",
                g_trace_group.tgid, pid);
    }
    (void)trace_group_detach_all();
}
static int mem_w(pid_t pid,uintptr_t addr,const void *d,size_t len){
    if (g_trace_compromised) {
        errno = EIO;
        return -1;
    }
    if(vm_write(pid,addr,d,len)==(ssize_t)len) return 0;
    char p[64]; snprintf(p,sizeof(p),"/proc/%d/mem",pid);
    int fd=open(p,O_RDWR); if(fd<0){
        const unsigned char *src=(const unsigned char*)d;
        for(size_t off=0; off<len; off+=sizeof(long)){
            long word=0; size_t n=len-off; if(n>sizeof(word)) n=sizeof(word);
            if(n<sizeof(word)){errno=0; long old=ptrace_call(PTRACE_PEEKDATA,pid,(void*)(addr+off),NULL); if(old==-1&&errno) return -1; word=old;}
            memcpy(&word,src+off,n);
            if(ptrace_call(PTRACE_POKEDATA,pid,(void*)(addr+off),(void*)word)<0) return -1;
        }
        return 0;
    }
    if(lseek(fd,(off_t)addr,SEEK_SET)!=(off_t)addr){close(fd);return -1;}
    ssize_t n=write(fd,d,len); close(fd); return n==(ssize_t)len?0:-1;
}
static int mem_r(pid_t pid,uintptr_t addr,void *buf,size_t len){
    if(vm_read(pid,addr,buf,len)==(ssize_t)len) return 0;
    char p[64]; snprintf(p,sizeof(p),"/proc/%d/mem",pid);
    int fd=open(p,O_RDONLY); if(fd<0){
        unsigned char *dst=(unsigned char*)buf;
        for(size_t off=0; off<len; off+=sizeof(long)){
            errno=0; long word=ptrace_call(PTRACE_PEEKDATA,pid,(void*)(addr+off),NULL);
            if(word==-1&&errno) return -1;
            size_t n=len-off; if(n>sizeof(word)) n=sizeof(word);
            memcpy(dst+off,&word,n);
        }
        return 0;
    }
    if(lseek(fd,(off_t)addr,SEEK_SET)!=(off_t)addr){close(fd);return -1;}
    ssize_t n=read(fd,buf,len); close(fd); return n==(ssize_t)len?0:-1;
}

static int exact_whitelist_stub_shape(const unsigned char *h, size_t n) {
    if (!h || n < WHITELIST_STUB_BYTES || !is_stub_head(h, n)) return 0;
    return exact_whitelist_stub_suffix(h, n, 2);
}

static int exact_whitelist_stub_at(pid_t pid, uintptr_t addr) {
    unsigned char code[WHITELIST_STUB_BYTES];
    return mem_r(pid, addr, code, sizeof(code)) == 0 &&
           exact_whitelist_stub_shape(code, sizeof(code));
}

static int writable_map_contains(pid_t pid, uintptr_t start, uintptr_t end,
                                 uintptr_t *map_start, uintptr_t *map_end) {
    if (!start || end <= start) return 0;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        uintptr_t s = 0, e = 0;
        char perms[8] = {0};
        if (sscanf(line, "%lx-%lx %7s", &s, &e, perms) != 3) continue;
        if (start >= s && end <= e && perms[0] == 'r' && perms[1] == 'w') {
            if (map_start) *map_start = s;
            if (map_end) *map_end = e;
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

/* Derive storage only from bytes outside the final writable PT_LOAD's
 * declared p_memsz but inside its loader-rounded writable page.  Unlike a
 * zero scan inside .bss, this area is not owned by any ELF section/object. */
static int discover_elf_tail_region(pid_t pid, uintptr_t base, uintptr_t need) {
    g_elf_tail_start = 0;
    g_elf_tail_end = 0;
    g_elf_tail_candidate = 0;
    if (!base || !need) return 0;

    Elf64_Ehdr eh;
    memset(&eh, 0, sizeof(eh));
    if (mem_r(pid, base, &eh, sizeof(eh)) != 0 ||
        memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS64 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB ||
        eh.e_phentsize != sizeof(Elf64_Phdr) ||
        eh.e_phnum == 0 || eh.e_phnum > 128 ||
        eh.e_phoff > 1024 * 1024) {
        fprintf(stderr, "[a2h_patch] ELF tail unavailable: invalid ELF64 header\n");
        return 0;
    }

    size_t ph_bytes = (size_t)eh.e_phnum * sizeof(Elf64_Phdr);
    Elf64_Phdr *ph = (Elf64_Phdr *)malloc(ph_bytes);
    if (!ph) return 0;
    if (mem_r(pid, base + (uintptr_t)eh.e_phoff, ph, ph_bytes) != 0) {
        free(ph);
        fprintf(stderr, "[a2h_patch] ELF tail unavailable: program headers unreadable\n");
        return 0;
    }

    uintptr_t segment_end = 0;
    int writable_index = -1;
    for (size_t i = 0; i < eh.e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_filesz > ph[i].p_memsz ||
            ph[i].p_vaddr > UINTPTR_MAX - ph[i].p_memsz) {
            fprintf(stderr, "[a2h_patch] ELF tail unavailable: malformed PT_LOAD index=%lu\n",
                    (unsigned long)i);
            free(ph);
            return 0;
        }
        if (!(ph[i].p_flags & PF_W) || ph[i].p_memsz == 0) continue;
        uintptr_t end = (uintptr_t)(ph[i].p_vaddr + ph[i].p_memsz);
        if (end > segment_end) {
            segment_end = end;
            writable_index = (int)i;
        }
    }
    if (writable_index < 0 || !segment_end || segment_end > UINTPTR_MAX - 15) {
        free(ph);
        fprintf(stderr, "[a2h_patch] ELF tail unavailable: writable PT_LOAD missing\n");
        return 0;
    }

    long page_long = sysconf(_SC_PAGESIZE);
    uintptr_t page = page_long > 0 ? (uintptr_t)page_long : 4096;
    if ((page & (page - 1)) != 0 || segment_end > UINTPTR_MAX - (page - 1)) {
        free(ph);
        fprintf(stderr, "[a2h_patch] ELF tail unavailable: invalid page size\n");
        return 0;
    }
    uintptr_t tail_start = (segment_end + 15) & ~(uintptr_t)15;
    uintptr_t tail_end = (segment_end + page - 1) & ~(page - 1);
    if (tail_end <= tail_start || tail_end - tail_start < need) {
        free(ph);
        fprintf(stderr,
                "[a2h_patch] ELF tail too small segment_end=0x%lx tail=0x%lx-0x%lx need=0x%lx\n",
                (unsigned long)segment_end, (unsigned long)tail_start,
                (unsigned long)tail_end, (unsigned long)need);
        return 0;
    }
    for (size_t i = 0; i < eh.e_phnum; ++i) {
        if ((int)i == writable_index || ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0)
            continue;
        uintptr_t other_start = (uintptr_t)ph[i].p_vaddr;
        uintptr_t other_end = (uintptr_t)(ph[i].p_vaddr + ph[i].p_memsz);
        if (other_start < tail_end && other_end > tail_start) {
            fprintf(stderr,
                    "[a2h_patch] ELF tail unavailable: PT_LOAD overlap index=%lu rel=0x%lx-0x%lx\n",
                    (unsigned long)i, (unsigned long)other_start,
                    (unsigned long)other_end);
            free(ph);
            return 0;
        }
    }
    free(ph);
    uintptr_t candidate = (tail_end - need) & ~(uintptr_t)15;
    if (candidate < tail_start || base > UINTPTR_MAX - tail_end) return 0;

    uintptr_t map_start = 0, map_end = 0;
    if (!writable_map_contains(pid, base + candidate, base + tail_end,
                               &map_start, &map_end)) {
        fprintf(stderr,
                "[a2h_patch] ELF tail unavailable: candidate not in one writable map rel=0x%lx-0x%lx\n",
                (unsigned long)candidate, (unsigned long)tail_end);
        return 0;
    }

    g_elf_tail_start = tail_start;
    g_elf_tail_end = tail_end;
    g_elf_tail_candidate = candidate;
    if (!g_rw_start || tail_start < g_rw_start) g_rw_start = tail_start;
    if (tail_end > g_rw_end) g_rw_end = tail_end;
    fprintf(stderr,
            "[a2h_patch] ELF tail ready segment_end=0x%lx safe=0x%lx-0x%lx candidate=0x%lx need=0x%lx map=0x%lx-0x%lx\n",
            (unsigned long)segment_end, (unsigned long)tail_start,
            (unsigned long)tail_end, (unsigned long)candidate,
            (unsigned long)need, (unsigned long)(map_start - base),
            (unsigned long)(map_end - base));
    return 1;
}
#define CACHE_HELPER_WORDS 24u
#define CACHE_HELPER_CODE_BYTES (CACHE_HELPER_WORDS * sizeof(uint32_t))
#define CACHE_HELPER_OWNER_BYTES 16u
#define CACHE_HELPER_TOTAL_BYTES (CACHE_HELPER_CODE_BYTES + CACHE_HELPER_OWNER_BYTES)
#define CACHE_HELPER_BRK_OFFSET 0x58u

static const uint32_t CACHE_HELPER_CODE[CACHE_HELPER_WORDS] = {
    0xD53B0022u, /* mrs x2, ctr_el0 */
    0xD3504C43u, /* ubfx x3, x2, #16, #4 */
    0xD2800084u, /* mov x4, #4 */
    0x9AC32083u, /* lsl x3, x4, x3 */
    0xD1000464u, /* sub x4, x3, #1 */
    0x8A240005u, /* bic x5, x0, x4 */
    0xD50B7B25u, /* dc cvau, x5 */
    0x8B0300A5u, /* add x5, x5, x3 */
    0xEB0100BFu, /* cmp x5, x1 */
    0x54FFFFA3u, /* b.lo dcache_loop */
    0xD5033B9Fu, /* dsb ish */
    0xD3400C43u, /* ubfx x3, x2, #0, #4 */
    0xD2800084u, /* mov x4, #4 */
    0x9AC32083u, /* lsl x3, x4, x3 */
    0xD1000464u, /* sub x4, x3, #1 */
    0x8A240005u, /* bic x5, x0, x4 */
    0xD50B7525u, /* ic ivau, x5 */
    0x8B0300A5u, /* add x5, x5, x3 */
    0xEB0100BFu, /* cmp x5, x1 */
    0x54FFFFA3u, /* b.lo icache_loop */
    0xD5033B9Fu, /* dsb ish */
    0xD5033FDFu, /* isb */
    0xD4200000u, /* brk #0 */
    0xD503201Fu  /* nop */
};
static const unsigned char CACHE_HELPER_OWNER[CACHE_HELPER_OWNER_BYTES] = {
    'A','2','H','-','I','C','A','C','H','E','-','V','1',0,0,0
};

static void build_cache_helper(unsigned char out[CACHE_HELPER_TOTAL_BYTES]) {
    memcpy(out, CACHE_HELPER_CODE, CACHE_HELPER_CODE_BYTES);
    memcpy(out + CACHE_HELPER_CODE_BYTES, CACHE_HELPER_OWNER,
           CACHE_HELPER_OWNER_BYTES);
}

static int executable_hal_map_contains(pid_t pid, uintptr_t start, uintptr_t end,
                                       uintptr_t *map_start, uintptr_t *map_end) {
    if (!start || end <= start || !g_libpath[0] || !g_lib_inode) return 0;
    char maps_path[64];
    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    FILE *maps = fopen(maps_path, "r");
    if (!maps) return 0;
    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), maps)) {
        uintptr_t current_start = 0, current_end = 0;
        unsigned long map_offset = 0;
        unsigned int dev_major = 0, dev_minor = 0;
        unsigned long long inode = 0;
        char perms[8] = {0};
        int path_pos = 0;
        int fields = sscanf(line, "%lx-%lx %7s %lx %x:%x %llu %n",
                            &current_start, &current_end, perms, &map_offset,
                            &dev_major, &dev_minor, &inode, &path_pos);
        (void)map_offset;
        if (fields != 7 || start < current_start || end > current_end ||
            perms[0] != 'r' || perms[1] != '-' || perms[2] != 'x' ||
            perms[3] != 'p' || dev_major != g_lib_dev_major ||
            dev_minor != g_lib_dev_minor || inode != g_lib_inode || path_pos <= 0) {
            continue;
        }
        char *path = line + path_pos;
        while (*path == ' ' || *path == '\t') path++;
        path[strcspn(path, "\r\n")] = 0;
        if (strcmp(path, g_libpath) != 0) continue;
        if (map_start) *map_start = current_start;
        if (map_end) *map_end = current_end;
        found = 1;
        break;
    }
    fclose(maps);
    return found;
}

/* The helper may live only after the final executable PT_LOAD's p_memsz and
 * before the loader-rounded page end.  Those bytes are outside every ELF load
 * segment, yet remain executable in the target's private file mapping. */
static uintptr_t discover_cache_helper(pid_t pid, uintptr_t base, size_t need) {
    if (!base || !need || need > 4096 || !g_libpath[0] || !g_lib_inode) return 0;
    Elf64_Ehdr eh;
    if (mem_r(pid, base, &eh, sizeof(eh)) != 0 ||
        memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS64 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB ||
        eh.e_phentsize != sizeof(Elf64_Phdr) ||
        eh.e_phnum == 0 || eh.e_phnum > 128 || eh.e_phoff > 1024 * 1024) {
        fprintf(stderr, "[a2h_patch] icache helper: invalid ELF header\n");
        return 0;
    }
    size_t ph_bytes = (size_t)eh.e_phnum * sizeof(Elf64_Phdr);
    Elf64_Phdr *ph = (Elf64_Phdr *)malloc(ph_bytes);
    if (!ph || mem_r(pid, base + (uintptr_t)eh.e_phoff, ph, ph_bytes) != 0) {
        free(ph);
        fprintf(stderr, "[a2h_patch] icache helper: program headers unreadable\n");
        return 0;
    }
    uintptr_t segment_end = 0;
    int executable_index = -1;
    for (size_t i = 0; i < eh.e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_filesz > ph[i].p_memsz ||
            ph[i].p_vaddr > UINTPTR_MAX - ph[i].p_memsz) {
            free(ph);
            fprintf(stderr, "[a2h_patch] icache helper: malformed PT_LOAD index=%lu\n",
                    (unsigned long)i);
            return 0;
        }
        if (!(ph[i].p_flags & PF_X) || ph[i].p_memsz == 0) continue;
        uintptr_t current_end = (uintptr_t)(ph[i].p_vaddr + ph[i].p_memsz);
        if (current_end > segment_end) {
            segment_end = current_end;
            executable_index = (int)i;
        }
    }
    long page_long = sysconf(_SC_PAGESIZE);
    uintptr_t page = page_long > 0 ? (uintptr_t)page_long : 4096;
    if (executable_index < 0 || !segment_end || (page & (page - 1)) != 0 ||
        segment_end > UINTPTR_MAX - (page - 1) ||
        segment_end > UINTPTR_MAX - 15) {
        free(ph);
        fprintf(stderr, "[a2h_patch] icache helper: executable PT_LOAD tail unavailable\n");
        return 0;
    }
    uintptr_t tail_start = (segment_end + 15) & ~(uintptr_t)15;
    uintptr_t tail_end = (segment_end + page - 1) & ~(page - 1);
    if (tail_end <= tail_start || tail_end - tail_start < need) {
        free(ph);
        fprintf(stderr,
                "[a2h_patch] icache helper: RX tail too small rel=0x%lx-0x%lx need=%lu\n",
                (unsigned long)tail_start, (unsigned long)tail_end,
                (unsigned long)need);
        return 0;
    }
    uintptr_t candidate = (tail_end - need) & ~(uintptr_t)15;
    if (candidate < tail_start || candidate > UINTPTR_MAX - need) {
        free(ph);
        return 0;
    }
    for (size_t i = 0; i < eh.e_phnum; ++i) {
        if (ph[i].p_type != PT_LOAD || ph[i].p_memsz == 0) continue;
        uintptr_t other_start = (uintptr_t)ph[i].p_vaddr;
        uintptr_t other_end = (uintptr_t)(ph[i].p_vaddr + ph[i].p_memsz);
        if (other_start < candidate + need && other_end > candidate) {
            free(ph);
            fprintf(stderr,
                    "[a2h_patch] icache helper: PT_LOAD overlap index=%lu\n",
                    (unsigned long)i);
            return 0;
        }
    }
    free(ph);
    if (base > UINTPTR_MAX - candidate || base + candidate > UINTPTR_MAX - need) return 0;
    uintptr_t absolute = base + candidate;
    uintptr_t map_start = 0, map_end = 0;
    if (!executable_hal_map_contains(pid, absolute, absolute + need,
                                     &map_start, &map_end)) {
        fprintf(stderr,
                "[a2h_patch] icache helper: candidate lacks exact private HAL RX map\n");
        return 0;
    }
    fprintf(stderr,
            "[a2h_patch] icache helper: ELF RX tail rel=0x%lx abs=0x%lx bytes=%lu map=0x%lx-0x%lx\n",
            (unsigned long)candidate, (unsigned long)absolute,
            (unsigned long)need, (unsigned long)map_start,
            (unsigned long)map_end);
    return absolute;
}

/* PTRACE_POKETEXT is the architecture-supported debugger code-write path.  On
 * arm64 the kernel performs the required ptrace-access cache maintenance for
 * each executable word; the resident helper then supplies the explicit full
 * D-cache clean and I-cache invalidate sequence for patched target ranges. */
static int trace_write_code(pid_t tid, uintptr_t addr, const void *data, size_t len) {
    int index = trace_group_index(tid);
    if (index < 0 || !g_trace_group.threads[index].stopped || !data || !len) {
        errno = EINVAL;
        return 0;
    }
    const unsigned char *source = (const unsigned char *)data;
    for (size_t offset = 0; offset < len; offset += sizeof(long)) {
        long word = 0;
        size_t chunk = len - offset;
        if (chunk > sizeof(word)) chunk = sizeof(word);
        if (chunk < sizeof(word)) {
            errno = 0;
            word = ptrace_call(PTRACE_PEEKDATA, tid,
                               (void *)(addr + offset), NULL);
            if (word == -1 && errno) return 0;
        }
        memcpy(&word, source + offset, chunk);
        if (ptrace_call(PTRACE_POKETEXT, tid, (void *)(addr + offset),
                        (void *)(uintptr_t)(unsigned long)word) < 0) {
            return 0;
        }
    }
    return 1;
}

static int ensure_cache_helper(pid_t pid, uintptr_t base, int *fresh_out) {
    if (fresh_out) *fresh_out = 0;
    unsigned char expected[CACHE_HELPER_TOTAL_BYTES];
    unsigned char current[CACHE_HELPER_TOTAL_BYTES];
    build_cache_helper(expected);
    uintptr_t helper = 0;
    if (g_cache_helper_addr && g_cache_helper_pid == pid &&
        g_cache_helper_base == base) {
        helper = g_cache_helper_addr;
    } else {
        helper = discover_cache_helper(pid, base, sizeof(expected));
    }
    if (!helper || mem_r(pid, helper, current, sizeof(current)) != 0) return 0;
    if (memcmp(current, expected, sizeof(expected)) == 0) {
        g_cache_helper_addr = helper;
        g_cache_helper_pid = pid;
        g_cache_helper_base = base;
        fprintf(stderr, "[a2h_patch] icache helper: owned image verified\n");
        return 1;
    }
    for (size_t i = 0; i < sizeof(current); ++i) {
        if (current[i] != 0) {
            fprintf(stderr,
                    "[a2h_patch] icache helper: RX tail is not zero/owned; refusing overwrite\n");
            return 0;
        }
    }
    pid_t control = g_trace_group.control_tid;
    int installed = control &&
                    trace_write_code(control, helper, expected, sizeof(expected));
    int verified = installed &&
                   mem_r(pid, helper, current, sizeof(current)) == 0 &&
                   memcmp(current, expected, sizeof(expected)) == 0;
    if (!verified) {
        unsigned char zeros[CACHE_HELPER_TOTAL_BYTES] = {0};
        int rolled_back = control &&
                          trace_write_code(control, helper, zeros, sizeof(zeros)) &&
                          mem_r(pid, helper, current, sizeof(current)) == 0 &&
                          memcmp(current, zeros, sizeof(zeros)) == 0;
        fprintf(stderr,
                "[a2h_patch] icache helper: install/verify FAIL rollback=%s\n",
                rolled_back ? "OK" : "FAIL");
        return 0;
    }
    g_cache_helper_addr = helper;
    g_cache_helper_pid = pid;
    g_cache_helper_base = base;
    g_cache_helper_ready = 0;
    if (fresh_out) *fresh_out = 1;
    fprintf(stderr, "[a2h_patch] icache helper: claimed with owner marker\n");
    return 1;
}

static int restore_trace_regs(pid_t tid, struct user_pt_regs_a64 *backup) {
    if (!backup) return 0;
    struct iovec iov = {backup, sizeof(*backup)};
    for (int attempt = 0; attempt < 3; ++attempt) {
        iov.iov_base = backup;
        iov.iov_len = sizeof(*backup);
        if (ptrace_call(PTRACE_SETREGSET, tid,
                        (void *)(uintptr_t)NT_PRSTATUS, &iov) == 0) {
            return 1;
        }
    }
    return 0;
}

static int execute_cache_helper_on_tid(pid_t pid, pid_t control,
                                       uintptr_t start, size_t nbytes,
                                       const char *purpose) {
    if (!g_cache_helper_addr || !nbytes || start > UINTPTR_MAX - nbytes) return 0;
    int index = trace_group_index(control);
    if (control <= 0 || index < 0 || !g_trace_group.threads[index].stopped) {
        fprintf(stderr, "[a2h_patch] icache helper: control TID is not stopped\n");
        return 0;
    }
    unsigned char expected[CACHE_HELPER_TOTAL_BYTES];
    unsigned char current[CACHE_HELPER_TOTAL_BYTES];
    build_cache_helper(expected);
    if (mem_r(pid, g_cache_helper_addr, current, sizeof(current)) != 0 ||
        memcmp(current, expected, sizeof(expected)) != 0) {
        fprintf(stderr, "[a2h_patch] icache helper: ownership changed before exec\n");
        return 0;
    }

    struct user_pt_regs_a64 regs, backup, observed;
    struct iovec iov = {&regs, sizeof(regs)};
    memset(&regs, 0, sizeof(regs));
    if (ptrace_call(PTRACE_GETREGSET, control,
                    (void *)(uintptr_t)NT_PRSTATUS, &iov) < 0) {
        fprintf(stderr, "[a2h_patch] icache helper: GETREGSET FAIL errno=%d\n", errno);
        return 0;
    }
    backup = regs;
    regs.pc = g_cache_helper_addr;
    regs.regs[0] = start;
    regs.regs[1] = start + nbytes;
    iov.iov_base = &regs;
    iov.iov_len = sizeof(regs);
    if (ptrace_call(PTRACE_SETREGSET, control,
                    (void *)(uintptr_t)NT_PRSTATUS, &iov) < 0) {
        fprintf(stderr, "[a2h_patch] icache helper: SETREGSET FAIL errno=%d\n", errno);
        return 0;
    }
    g_trace_group.threads[index].saved_regs = backup;
    g_trace_group.threads[index].regs_restore_pending = 1;
    if (ptrace_call(PTRACE_CONT, control, NULL, NULL) < 0) {
        fprintf(stderr, "[a2h_patch] icache helper: CONT FAIL errno=%d\n", errno);
        if (restore_trace_regs(control, &backup)) {
            g_trace_group.threads[index].regs_restore_pending = 0;
        } else if (trace_tid_exists(control)) {
            fprintf(stderr,
                    "[a2h_patch] icache helper: regs restore after CONT failure FAIL errno=%d\n",
                    errno);
            g_trace_compromised = 1;
        } else {
            g_trace_group.threads[index].regs_restore_pending = 0;
        }
        return 0;
    }
    g_trace_group.threads[index].stopped = 0;
    int status = 0;
    if (!trace_wait_stop(control, 1000, &status)) {
        fprintf(stderr,
                "[a2h_patch] icache helper: bounded wait timeout; interrupting tid=%d\n",
                control);
        int recovered = 0;
        for (int attempt = 0; attempt < 3 && !recovered; ++attempt) {
            (void)ptrace_call(PTRACE_INTERRUPT, control, NULL, NULL);
            recovered = trace_wait_stop(control, 500, &status);
        }
        if (!recovered) {
            fprintf(stderr,
                    "[a2h_patch] icache helper: cannot recover control tid errno=%d\n",
                    errno);
            if (trace_tid_exists(control)) {
                g_trace_compromised = 1;
            } else {
                g_trace_group.threads[index].regs_restore_pending = 0;
            }
            return 0;
        }
    }
    g_trace_group.threads[index].stopped = 1;
    trace_capture_resume_signal(&g_trace_group.threads[index], status, 1);
    memset(&observed, 0, sizeof(observed));
    iov.iov_base = &observed;
    iov.iov_len = sizeof(observed);
    int observed_ok = ptrace_call(PTRACE_GETREGSET, control,
                                  (void *)(uintptr_t)NT_PRSTATUS, &iov) == 0;
    int signal = WIFSTOPPED(status) ? WSTOPSIG(status) : 0;
    uintptr_t brk = g_cache_helper_addr + CACHE_HELPER_BRK_OFFSET;
    int trap_ok = observed_ok && signal == SIGTRAP &&
                  (observed.pc == brk || observed.pc == brk + sizeof(uint32_t));

    int restored = restore_trace_regs(control, &backup);
    if (restored) {
        g_trace_group.threads[index].regs_restore_pending = 0;
    } else if (trace_tid_exists(control)) {
        g_trace_compromised = 1;
    } else {
        g_trace_group.threads[index].regs_restore_pending = 0;
    }
    fprintf(stderr,
            "[a2h_patch] icache helper: %s sig=%d pc=0x%lx brk=0x%lx trap=%s regs_restore=%s\n",
            purpose ? purpose : "target", signal,
            (unsigned long)(observed_ok ? observed.pc : 0),
            (unsigned long)brk, trap_ok ? "OK" : "FAIL",
            restored ? "OK" : "FAIL");
    return trap_ok && restored;
}

static int restore_thread_affinity(pid_t tid, const cpu_set_t *original) {
    if (!original) return 0;
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (sched_setaffinity(tid, sizeof(*original), original) != 0) continue;
        cpu_set_t observed;
        CPU_ZERO(&observed);
        if (sched_getaffinity(tid, sizeof(observed), &observed) == 0 &&
            CPU_EQUAL(&observed, original)) {
            return 1;
        }
    }
    return 0;
}

/* IC IVAU is performed by a PE, so execute the helper once on every CPU on
 * which any frozen target thread could have populated a stale I-cache line.
 * A singleton affinity proves which CPU ran each instance; all affinities are
 * restored before the process is detached. */
static int execute_cache_helper_all_cpus(pid_t pid, uintptr_t start,
                                         size_t nbytes, const char *purpose) {
    size_t thread_count = g_trace_group.count;
    if (!thread_count) return 0;
    cpu_set_t *affinities = (cpu_set_t *)calloc(thread_count, sizeof(*affinities));
    if (!affinities) return 0;
    cpu_set_t union_set;
    CPU_ZERO(&union_set);
    for (size_t i = 0; i < thread_count; ++i) {
        trace_thread_t *thread = &g_trace_group.threads[i];
        if (!thread->stopped ||
            sched_getaffinity(thread->tid, sizeof(affinities[i]),
                              &affinities[i]) != 0) {
            fprintf(stderr,
                    "[a2h_patch] icache helper: affinity read FAIL tid=%d errno=%d\n",
                    thread->tid, errno);
            free(affinities);
            return 0;
        }
        CPU_OR(&union_set, &union_set, &affinities[i]);
    }

    int cpu_total = 0;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (CPU_ISSET(cpu, &union_set)) cpu_total++;
    }
    if (!cpu_total) {
        free(affinities);
        return 0;
    }

    int completed = 0;
    for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
        if (!CPU_ISSET(cpu, &union_set)) continue;
        size_t selected = thread_count;
        for (size_t i = 0; i < thread_count; ++i) {
            if (CPU_ISSET(cpu, &affinities[i])) {
                selected = i;
                break;
            }
        }
        if (selected == thread_count) break;
        pid_t tid = g_trace_group.threads[selected].tid;
        cpu_set_t singleton;
        CPU_ZERO(&singleton);
        CPU_SET(cpu, &singleton);
        if (sched_setaffinity(tid, sizeof(singleton), &singleton) != 0) {
            fprintf(stderr,
                    "[a2h_patch] icache helper: bind tid=%d cpu=%d FAIL errno=%d\n",
                    tid, cpu, errno);
            break;
        }
        g_trace_group.threads[selected].saved_affinity = affinities[selected];
        g_trace_group.threads[selected].affinity_restore_pending = 1;
        cpu_set_t observed_affinity;
        CPU_ZERO(&observed_affinity);
        int bound = sched_getaffinity(tid, sizeof(observed_affinity),
                                      &observed_affinity) == 0 &&
                    CPU_ISSET(cpu, &observed_affinity);
        for (int other = 0; bound && other < CPU_SETSIZE; ++other) {
            if (other != cpu && CPU_ISSET(other, &observed_affinity)) bound = 0;
        }
        char label[64];
        snprintf(label, sizeof(label), "%s cpu=%d",
                 purpose ? purpose : "target", cpu);
        int executed = bound && execute_cache_helper_on_tid(
            pid, tid, start, nbytes, label);
        int restored = restore_thread_affinity(tid, &affinities[selected]);
        if (restored || !trace_tid_exists(tid)) {
            g_trace_group.threads[selected].affinity_restore_pending = 0;
        } else {
            g_trace_compromised = 1;
        }
        if (!bound) {
            fprintf(stderr,
                    "[a2h_patch] icache helper: singleton affinity verify FAIL tid=%d cpu=%d\n",
                    tid, cpu);
        }
        if (!restored) {
            fprintf(stderr,
                    "[a2h_patch] icache helper: affinity restore FAIL tid=%d errno=%d\n",
                    tid, errno);
        }
        if (!executed || !restored) break;
        completed++;
    }
    free(affinities);
    fprintf(stderr,
            "[a2h_patch] icache helper: %s cpu_coverage=%d/%d\n",
            purpose ? purpose : "target", completed, cpu_total);
    return completed == cpu_total;
}

static void log_prop(const char *key){
    char cmd[192], val[256]={0};
    snprintf(cmd,sizeof(cmd),"/system/bin/getprop %s 2>/dev/null", key);
    FILE *f=popen(cmd,"r");
    if(f){ if(fgets(val,sizeof(val),f)){ size_t n=strlen(val); while(n&&(val[n-1]=='\n'||val[n-1]=='\r')) val[--n]=0; } pclose(f); }
    fprintf(stderr,"[a2h_patch] prop %s=%s\n", key, val[0]?val:"(empty)");
}
static void log_system_identity(void){
    fprintf(stderr,"[a2h_patch] --- system identity ---\n");
    log_prop("ro.product.model");
    log_prop("ro.product.device");
    log_prop("ro.build.version.release");
    log_prop("ro.build.version.sdk");
    log_prop("ro.mi.os.version.incremental");
    log_prop("ro.mi.os.version.name");
    log_prop("ro.system.build.version.incremental");
    log_prop("ro.vendor.build.version.incremental");
    log_prop("ro.build.version.incremental");
    fprintf(stderr,"[a2h_patch] --- end identity ---\n");
}

/* Stop-the-world EL0 cache maintenance.  The resident helper dynamically reads
 * CTR_EL0 and performs dc cvau/dsb ish/ic ivau/dsb ish/isb over the range. */
static int remote_icache_flush(pid_t pid, uintptr_t base, uintptr_t func_abs,
                               size_t nbytes) {
#ifdef A2H_TEST_FAULT_INJECTION
    if (g_test_icache_override >= 0) {
        int call = ++g_test_icache_calls;
        int result = call == g_test_icache_fail_call ? 0 : g_test_icache_override;
        g_icache_methods |= result;
        if (!(result & ICACHE_REMOTE_IVAU)) g_icache_failures++;
        return result;
    }
#endif
    int methods = 0;
    if (syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL, 0) == 0) {
        fprintf(stderr, "[a2h_patch] icache: membarrier GLOBAL ok\n");
        methods |= ICACHE_MEMBARRIER;
    } else if (syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL_EXPEDITED, 0) == 0) {
        fprintf(stderr, "[a2h_patch] icache: membarrier GLOBAL_EXPEDITED ok\n");
        methods |= ICACHE_MEMBARRIER;
    } else {
        fprintf(stderr, "[a2h_patch] icache: membarrier unavailable errno=%d\n", errno);
    }
    if (!g_attached || g_trace_compromised) {
        fprintf(stderr, "[a2h_patch] icache: thread group unavailable/compromised\n");
        g_icache_methods |= methods;
        g_icache_failures++;
        return methods;
    }
    if (!base || !func_abs || !nbytes || nbytes > 4096 ||
        func_abs > UINTPTR_MAX - nbytes) {
        fprintf(stderr, "[a2h_patch] icache: invalid target range\n");
        g_icache_methods |= methods;
        g_icache_failures++;
        return methods;
    }
    int fresh = 0;
    if (!ensure_cache_helper(pid, base, &fresh)) {
        fprintf(stderr, "[a2h_patch] icache: no proven-safe helper\n");
        g_icache_methods |= methods;
        g_icache_failures++;
        return methods;
    }
    if (!g_cache_helper_ready) {
        if (!execute_cache_helper_all_cpus(pid, g_cache_helper_addr,
                                           CACHE_HELPER_TOTAL_BYTES,
                                           "bootstrap-self")) {
            fprintf(stderr, "[a2h_patch] icache: helper bootstrap FAIL fresh=%d\n",
                    fresh);
            g_icache_methods |= methods;
            g_icache_failures++;
            return methods;
        }
        g_cache_helper_ready = 1;
    }
    if (execute_cache_helper_all_cpus(pid, func_abs, nbytes, "target")) {
        fprintf(stderr, "[a2h_patch] icache: remote D/I EXEC ok\n");
        methods |= ICACHE_REMOTE_IVAU;
    } else {
        fprintf(stderr, "[a2h_patch] icache: remote D/I EXEC failed\n");
        g_icache_failures++;
    }
    (void)syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL, 0);
    g_icache_methods |= methods;
    return methods;
}

static const char *icache_status(void) {
    if (g_icache_failures) return "failed";
    if (g_icache_methods & ICACHE_REMOTE_IVAU) return "remote-dc-ic";
    if (g_icache_methods & ICACHE_MEMBARRIER) return "membarrier-only";
    return "none";
}

static int cmdline_has(const char *buf, int n, const char *needle) {
    int j = 0;
    while (j < n) {
        if (buf[j] && strstr(buf + j, needle)) return 1;
        j += (int)strlen(buf + j) + 1;
    }
    return 0;
}
static int proc_maps_has_audio_primary(const char *pid_name) {
    char mp[256], line[512];
    snprintf(mp, sizeof(mp), "/proc/%s/maps", pid_name);
    FILE *f = fopen(mp, "r");
    if (!f) return 0;
    int hit = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, "audio.primary.") && strstr(line, ".so")) { hit = 1; break; }
    }
    fclose(f);
    return hit;
}
static int find_pid(void) {
    char needle[80]; name_svc_short(needle);
    DIR *d=opendir("/proc"); if(!d) return -1;
    struct dirent *de;
    // First pass: exact/near service names. This is the common HyperOS path.
    // Generic audio service names are only accepted when the process already maps audio.primary.*.so.
    while((de=readdir(d))) {
        if(de->d_name[0]<'0'||de->d_name[0]>'9') continue;
        char pp[256],buf[512];
        snprintf(pp,sizeof(pp),"/proc/%s/cmdline",de->d_name);
        int fd=open(pp,O_RDONLY); if(fd<0) continue;
        ssize_t n=read(fd,buf,sizeof(buf)-1); close(fd);
        if(n<=0) continue; buf[n]=0;
        if (cmdline_has(buf,(int)n,needle) ||
            cmdline_has(buf,(int)n,"android.hardware.audio.service-aidl.mediatek") ||
            cmdline_has(buf,(int)n,"audio.service-aidl.mediatek")) {
            closedir(d); return atoi(de->d_name);
        }
        if ((cmdline_has(buf,(int)n,"android.hardware.audio.service") ||
             cmdline_has(buf,(int)n,"audio.service-aidl")) &&
            proc_maps_has_audio_primary(de->d_name)) {
            closedir(d); return atoi(de->d_name);
        }
    }
    rewinddir(d);
    // Second pass: universal fallback inherited from v1.0 style discovery.
    // Pick whichever audio process actually maps an audio.primary.* HAL.
    while((de=readdir(d))) {
        if(de->d_name[0]<'0'||de->d_name[0]>'9') continue;
        char mp[256], line[512];
        snprintf(mp,sizeof(mp),"/proc/%s/maps",de->d_name);
        FILE *f=fopen(mp,"r"); if(!f) continue;
        int hit=0;
        while(fgets(line,sizeof(line),f)) {
            if (strstr(line,"audio.primary.") && strstr(line,".so")) { hit=1; break; }
        }
        fclose(f);
        if(hit){ closedir(d); return atoi(de->d_name); }
    }
    closedir(d); return -1;
}
static uintptr_t find_lib_base_and_maps(pid_t pid, const char *name, uintptr_t *rw_s, uintptr_t *rw_e, uintptr_t *rx_s, uintptr_t *rx_e) {
    char mp[64]; snprintf(mp,sizeof(mp),"/proc/%d/maps",pid);
    FILE *f=fopen(mp,"r"); if(!f) return 0;
    char line[512]; uintptr_t base=0,first_s=0,rws=0,rwe=0,rxs=0,rxe=0;
    while(fgets(line,sizeof(line),f)) {
        if(!strstr(line,name)) continue;
        uintptr_t s=0,e=0; unsigned off=0; char perms[8]={0};
        if (sscanf(line, "%lx-%lx %7s %x", &s, &e, perms, &off) < 4) continue;
        if (!first_s) first_s = s;
        if (!base && off == 0) base = s;
        if (strchr(perms, 'x')) {
            if (!rxs || s < rxs) rxs = s;
            if (e > rxe) rxe = e;
        }
        if (strchr(perms, 'w')) {
            if (!rws || (e - s) >= (rwe - rws)) { rws = s; rwe = e; }
        }
    }
    fclose(f);
    if (!base) base = first_s;
    if (rw_s) *rw_s = rws;
    if (rw_e) *rw_e = rwe;
    if (rx_s) *rx_s = rxs;
    if (rx_e) *rx_e = rxe;
    return base;
}

/* Preserve the exact pathname and file identity used by the target process.
 * The ELF symbol fallback must never inspect a similarly named local copy. */
static int capture_hal_map_identity(pid_t pid, uintptr_t base) {
    g_libpath[0] = 0;
    g_lib_dev_major = 0;
    g_lib_dev_minor = 0;
    g_lib_inode = 0;
    if (!base || !g_libname[0]) return 0;

    char mp[64];
    snprintf(mp, sizeof(mp), "/proc/%d/maps", pid);
    FILE *f = fopen(mp, "r");
    if (!f) return 0;
    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        uintptr_t s = 0, e = 0;
        unsigned long map_off = 0;
        unsigned int dev_major = 0, dev_minor = 0;
        unsigned long long inode = 0;
        char perms[8] = {0};
        int path_pos = 0;
        int fields = sscanf(line, "%lx-%lx %7s %lx %x:%x %llu %n",
                            &s, &e, perms, &map_off, &dev_major, &dev_minor,
                            &inode, &path_pos);
        if (fields != 7 || s != base || map_off != 0 || path_pos <= 0) continue;
        char *path = line + path_pos;
        while (*path == ' ' || *path == '\t') path++;
        size_t n = strcspn(path, "\r\n");
        path[n] = 0;
        const char *base_name = strrchr(path, '/');
        base_name = base_name ? base_name + 1 : path;
        if (path[0] != '/' || strcmp(base_name, g_libname) != 0) continue;
        snprintf(g_libpath, sizeof(g_libpath), "%s", path);
        g_lib_dev_major = dev_major;
        g_lib_dev_minor = dev_minor;
        g_lib_inode = inode;
        found = 1;
        break;
    }
    fclose(f);
    if (found) {
        fprintf(stderr,
                "[a2h_patch] HAL map path=%s dev=%x:%x inode=%llu\n",
                g_libpath, g_lib_dev_major, g_lib_dev_minor, g_lib_inode);
    } else {
        fprintf(stderr, "[a2h_patch] WARN: exact offset-0 HAL map path unavailable\n");
    }
    return found;
}

static uintptr_t find_audio_primary_base_and_maps(pid_t pid, uintptr_t *rw_s, uintptr_t *rw_e, uintptr_t *rx_s, uintptr_t *rx_e) {
    char mp[64]; snprintf(mp,sizeof(mp),"/proc/%d/maps",pid);
    FILE *f=fopen(mp,"r"); if(!f) return 0;
    char line[512], best_name[96]={0};
    int best_score = -1;
    while(fgets(line,sizeof(line),f)) {
        if(!strstr(line,"audio.primary.") || !strstr(line,".so")) continue;
        char *path = strchr(line, '/');
        if(!path) continue;
        char *nl = strchr(path, '\n'); if(nl) *nl = 0;
        char *base_name = strrchr(path, '/'); base_name = base_name ? base_name + 1 : path;
        int sc = 10;
        if(strstr(base_name,"audio.primary.mediatek.so")) sc += 50;
        if(strstr(base_name,"audio.primary.mt6991.so")) sc += 45;
        if(strstr(path,"/vendor/lib64/hw/")) sc += 10;
        if(strstr(line," r-xp ")) sc += 3;
        if(sc > best_score) {
            best_score = sc;
            snprintf(best_name,sizeof(best_name),"%s",base_name);
        }
    }
    fclose(f);
    if(!best_name[0]) return 0;
    uintptr_t base = find_lib_base_and_maps(pid, best_name, rw_s, rw_e, rx_s, rx_e);
    if(base) {
        snprintf(g_libname,sizeof(g_libname),"%s",best_name);
        fprintf(stderr,"[a2h_patch] auto HAL lib=%s score=%d\n", best_name, best_score);
    }
    return base;
}

static uintptr_t find_audio_primary_by_exact_base(pid_t pid, uintptr_t wanted,
                                                   uintptr_t *rw_s, uintptr_t *rw_e,
                                                   uintptr_t *rx_s, uintptr_t *rx_e) {
    if (!wanted) return 0;
    char mp[64];
    snprintf(mp, sizeof(mp), "/proc/%d/maps", pid);
    FILE *f = fopen(mp, "r");
    if (!f) return 0;
    char line[1024], selected[96] = {0};
    while (fgets(line, sizeof(line), f)) {
        uintptr_t start = 0;
        unsigned long map_off = 0;
        if (sscanf(line, "%lx-%*lx %*7s %lx", &start, &map_off) != 2 ||
            start != wanted || map_off != 0 ||
            !strstr(line, "audio.primary.") || !strstr(line, ".so")) {
            continue;
        }
        char *path = strchr(line, '/');
        if (!path) continue;
        path[strcspn(path, "\r\n")] = 0;
        const char *base_name = strrchr(path, '/');
        base_name = base_name ? base_name + 1 : path;
        if (strlen(base_name) >= sizeof(selected)) continue;
        snprintf(selected, sizeof(selected), "%s", base_name);
        break;
    }
    fclose(f);
    if (!selected[0]) return 0;
    uintptr_t actual = find_lib_base_and_maps(pid, selected, rw_s, rw_e, rx_s, rx_e);
    if (actual != wanted) return 0;
    snprintf(g_libname, sizeof(g_libname), "%s", selected);
    return actual;
}

static void trim(char *s) {
    char *p=s; while(*p && isspace((unsigned char)*p)) p++;
    if(p!=s) memmove(s,p,strlen(p)+1);
    size_t n=strlen(s); while(n && isspace((unsigned char)s[n-1])) s[--n]=0;
}
static int valid_pkg(const char *s) {
    if(!s||!*s||strlen(s)>=64) return 0;
    if(!isalpha((unsigned char)s[0])) return 0;
    for(const char *p=s; *p; ++p) if(!(isalnum((unsigned char)*p)||*p=='_'||*p=='.')) return 0;
    return strchr(s,'.')!=NULL;
}
static int valid_pkg_for_slot(const char *s, int slot) {
    (void)slot;
    if(!valid_pkg(s)) return 0;
    /* All ten configured slots are copied into the 64-byte custom cave. */
    return 1;
}
static void free_pkgs(char **pkgs) {
    if(!pkgs) return;
    for(int i=0;i<MAX_SLOTS;++i) free(pkgs[i]);
    free(pkgs);
}
typedef struct {
    int lines_read;
    int active;
    int rejected;
    int extra_lines;
    int padded_lines;
    int fallback_defaults;
    int explicit_all_off;
} pkg_stats_t;

static int fill_default_pkgs(char **pkgs) {
    int active = 0;
    for (int i = 0; i < 6; ++i) {
        char tmp[64];
        pkg_default(i, tmp, sizeof(tmp));
        free(pkgs[i]);
        pkgs[i] = strdup(tmp);
        if (pkgs[i]) active++;
    }
    for (int i = 6; i < MAX_SLOTS; ++i) {
        free(pkgs[i]);
        pkgs[i] = NULL;
    }
    return active;
}

static char **read_pkgs(const char *path, pkg_stats_t *stats) {
    char **p = calloc(MAX_SLOTS, sizeof(char*));
    FILE *f = fopen(path, "r");
    if (!p) return NULL;
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!f) {
        int active = fill_default_pkgs(p);
        if (stats) { stats->active = active; stats->fallback_defaults = 1; }
        fprintf(stderr, "[a2h_patch] packages: fallback defaults (file missing: %s) active=%d\n", path, active);
        return p;
    }
    char line[256]; int sl=0, active=0, rejected=0, extra=0;
    while(fgets(line,sizeof(line),f)) {
        trim(line);
        if(line[0]=='#') continue;
        if(sl>=MAX_SLOTS) { extra++; continue; }
        if(line[0]) {
            if (valid_pkg_for_slot(line, sl)) {
                p[sl] = strdup(line);
                if (p[sl]) active++;
            } else {
                fprintf(stderr, "[a2h_patch] slot%d rejected: '%s'\n", sl, line);
                rejected++;
                p[sl]=NULL;
            }
        } else p[sl]=NULL;
        sl++;
    }
    fclose(f);
    int padded = sl < MAX_SLOTS ? MAX_SLOTS - sl : 0;
    int fallback = 0;
    int explicit_all_off = (sl == MAX_SLOTS && active == 0 && rejected == 0);
    if(active == 0 && !explicit_all_off) {
        fprintf(stderr,
                "[a2h_patch] WARN: packages structurally invalid/empty lines=%d rejected=%d; restoring official defaults\n",
                sl, rejected);
        active = fill_default_pkgs(p);
        fallback = 1;
    } else if (sl < MAX_SLOTS) {
        fprintf(stderr, "[a2h_patch] WARN: packages short table lines=%d padded=%d active=%d\n",
                sl, padded, active);
    }
    if(extra > 0) {
        fprintf(stderr, "[a2h_patch] WARN: packages ignored extra lines=%d\n", extra);
    }
    if(explicit_all_off) {
        fprintf(stderr, "[a2h_patch] packages: explicit all-off table accepted\n");
    }
    if(stats) {
        stats->lines_read = sl;
        stats->active = active;
        stats->rejected = rejected;
        stats->extra_lines = extra;
        stats->padded_lines = padded;
        stats->fallback_defaults = fallback;
        stats->explicit_all_off = explicit_all_off;
    }
    fprintf(stderr,
            "[a2h_patch] packages file=%s lines_read=%d active=%d rejected=%d extra=%d padded=%d fallback=%d all_off=%d\n",
            path, sl, active, rejected, extra, padded, fallback, explicit_all_off);
    return p;
}

static void apply_profile(const profile_t *prof) {
    snprintf(g_profile, sizeof(g_profile), "%s", prof->name);
    snprintf(g_profile_hint, sizeof(g_profile_hint), "%s", prof->hint);
    for (int i = 0; i < 6; ++i) { set_slot_off(i, prof->slot_off[i]); slots[i].max_len = prof->slot_len[i]; slots[i].label = "s"; }
    for (int i = 6; i < MAX_SLOTS; ++i) { slots[i].max_len = 63; slots[i].label = "x"; }
}
typedef struct {
    int table_ok;
    int magic_ok;
    int active_ptrs;
    int invalid_ptrs;
    int content_mismatch;
    uintptr_t table_abs;
} stub_info_t;

/* Decode the ADRP/ADD pair emitted by write_whitelist_stub_code(). */
static int decode_stub_table(uintptr_t func_abs, const unsigned char *head, uintptr_t *table_abs) {
    if (!head || !table_abs) return 0;
    uint32_t adrp = load_u32le(head);
    uint32_t add = load_u32le(head + 4);
    if (((adrp & 0x9F00001Fu) != 0x90000001u) ||
        ((add & 0xFFC003FFu) != 0x91000021u)) return 0;
    int64_t imm21 = (int64_t)((((adrp >> 5) & 0x7FFFFu) << 2) | ((adrp >> 29) & 0x3u));
    if (imm21 & (1LL << 20)) imm21 |= ~((1LL << 21) - 1);
    int64_t page_signed = (int64_t)(func_abs & ~(uintptr_t)0xFFFu) + imm21 * 4096;
    if (page_signed < 0) return 0;
    uintptr_t page = (uintptr_t)page_signed;
    uint32_t imm12 = (add >> 10) & 0xFFFu;
    if ((add >> 22) & 1u) imm12 <<= 12;
    *table_abs = page + (uintptr_t)imm12;
    return 1;
}

static int inspect_stub_table(pid_t pid, uintptr_t base, uintptr_t func_off,
                              const unsigned char *head, char **expected,
                              int verbose, stub_info_t *out) {
    stub_info_t info;
    memset(&info, 0, sizeof(info));
    uintptr_t table = 0;
    unsigned char code[WHITELIST_STUB_BYTES];
    if (!head || mem_r(pid, base + func_off, code, sizeof(code)) != 0 ||
        memcmp(code, head, 16) != 0 ||
        !exact_whitelist_stub_shape(code, sizeof(code)) ||
        !decode_stub_table(base + func_off, code, &table)) {
        if (verbose) fprintf(stderr, "[a2h_patch] complete whitelist stub verify FAIL\n");
        if (out) *out = info;
        return 0;
    }
    info.table_abs = table;
    uintptr_t rw_lo = base + g_rw_start;
    uintptr_t rw_hi = base + g_rw_end;
    uintptr_t table_bytes = MAX_SLOTS * sizeof(uint64_t);
    uintptr_t table_delta = MAX_SLOTS * 64 + 16;
    int tail_ok = (g_elf_tail_end > g_elf_tail_start &&
                   table >= base + g_elf_tail_start &&
                   table <= base + g_elf_tail_end - table_bytes);
    int range_ok = (g_rw_end > g_rw_start &&
                    g_rw_end - g_rw_start >= table_bytes &&
                    table >= rw_lo && table <= rw_hi - table_bytes && tail_ok);
    if (!range_ok || table < base + 16) {
        if (verbose) fprintf(stderr, "[a2h_patch] stub table out of range @0x%lx\n",
                             (unsigned long)table);
        if (out) *out = info;
        return 0;
    }
    if (table < base + table_delta) {
        if (verbose) fprintf(stderr, "[a2h_patch] stub table has invalid cave layout\n");
        if (out) *out = info;
        return 0;
    }
    uintptr_t cave_abs = table - table_delta;
    if (((cave_abs - base) & 15u) != 0 ||
        cave_abs < base + g_elf_tail_start ||
        cave_abs > base + g_elf_tail_end - WHITELIST_CAVE_BYTES) {
        if (verbose) fprintf(stderr, "[a2h_patch] stub cave outside ELF tail\n");
        if (out) *out = info;
        return 0;
    }
    unsigned char marker[sizeof(WHITELIST_MARKER)];
    if (mem_r(pid, table - sizeof(marker), marker, sizeof(marker)) == 0 &&
        memcmp(marker, WHITELIST_MARKER, sizeof(marker)) == 0) {
        info.magic_ok = 1;
    }
    uint64_t ptrs[MAX_SLOTS];
    if (mem_r(pid, table, ptrs, sizeof(ptrs)) != 0) {
        if (verbose) fprintf(stderr, "[a2h_patch] stub pointer table read FAIL @0x%lx\n",
                             (unsigned long)(table - base));
        if (out) *out = info;
        return 0;
    }
    uintptr_t lib_hi = base + g_rw_end;
    for (int i = 0; i < MAX_SLOTS; ++i) {
        const char *want = expected && expected[i] ? expected[i] : NULL;
        if (!ptrs[i]) {
            if (expected && want && want[0]) info.content_mismatch++;
            continue;
        }
        info.active_ptrs++;
        if (expected && (!want || !want[0])) {
            /* A disabled slot must not retain a pointer to an old string. */
            info.content_mismatch++;
        }
        uintptr_t p = (uintptr_t)ptrs[i];
        char got[64];
        memset(got, 0, sizeof(got));
        uintptr_t expected_ptr = cave_abs + (uintptr_t)i * 64;
        if (p != expected_ptr || p < base || p >= lib_hi ||
            mem_r(pid, p, got, sizeof(got) - 1) != 0 ||
            !valid_pkg(got)) {
            info.invalid_ptrs++;
            continue;
        }
        if (want && strcmp(got, want) != 0) info.content_mismatch++;
        if (verbose) fprintf(stderr, "[a2h_patch] active_ptr[%d]=0x%lx value='%s'\n",
                             i, (unsigned long)(p - base), got);
    }
    info.table_ok = range_ok && info.magic_ok && info.invalid_ptrs == 0;
    if (verbose) {
        fprintf(stderr,
                "[a2h_patch] stub table=%s magic=%s active_ptrs=%d invalid_ptrs=%d content_mismatch=%d rel=0x%lx\n",
                info.table_ok ? "OK" : "BAD", info.magic_ok ? "OK" : "missing",
                info.active_ptrs, info.invalid_ptrs, info.content_mismatch,
                (unsigned long)(table - base));
    }
    if (out) *out = info;
    return info.table_ok;
}

static int marked_cave_ok(pid_t pid, uintptr_t base, uintptr_t cave) {
    uintptr_t marker_span = MAX_SLOTS * 64 + 16;
    if ((cave & 15u) != 0 ||
        g_rw_end <= g_rw_start || g_elf_tail_end <= g_elf_tail_start ||
        g_elf_tail_end - g_elf_tail_start < WHITELIST_CAVE_BYTES ||
        g_rw_end - g_rw_start < marker_span ||
        cave < g_rw_start || cave > g_rw_end - marker_span ||
        cave < g_elf_tail_start || cave > g_elf_tail_end - WHITELIST_CAVE_BYTES) {
        return 0;
    }
    uintptr_t marker = base + cave + MAX_SLOTS * 64;
    unsigned char marker_value[sizeof(WHITELIST_MARKER)];
    if (mem_r(pid, marker, marker_value, sizeof(marker_value)) != 0 ||
        memcmp(marker_value, WHITELIST_MARKER, sizeof(marker_value)) != 0) {
        return 0;
    }
    uintptr_t table = cave + MAX_SLOTS * 64;
    table = (table + 16 + 7) & ~((uintptr_t)7);
    uint64_t ptrs[MAX_SLOTS];
    if (mem_r(pid, base + table, ptrs, sizeof(ptrs)) != 0) return 0;
    uintptr_t rw_lo = base + g_rw_start;
    uintptr_t rw_hi = base + g_rw_end;
    for (int i = 0; i < MAX_SLOTS; ++i) {
        uintptr_t expected = base + cave + (uintptr_t)i * 64;
        if (ptrs[i] && (ptrs[i] != expected || ptrs[i] < rw_lo || ptrs[i] >= rw_hi))
            return 0;
    }
    return 1;
}

static int find_unique_marked_cave(pid_t pid, uintptr_t base, uintptr_t *out) {
    if (out) *out = 0;
    if (g_elf_tail_end <= g_elf_tail_start ||
        g_elf_tail_end - g_elf_tail_start < WHITELIST_CAVE_BYTES) {
        return 0;
    }
    uintptr_t first = (g_elf_tail_start + 15u) & ~(uintptr_t)15u;
    uintptr_t last = g_elf_tail_end - WHITELIST_CAVE_BYTES;
    uintptr_t found = 0;
    int hits = 0;
    for (uintptr_t cave = first; cave <= last; cave += 16u) {
        if (!marked_cave_ok(pid, base, cave)) continue;
        found = cave;
        hits++;
        if (hits > 1) break;
    }
    if (hits != 0) {
        fprintf(stderr,
                "[a2h_patch] marked cave scan hits=%d rel=0x%lx safe=0x%lx-0x%lx\n",
                hits, (unsigned long)found, (unsigned long)g_elf_tail_start,
                (unsigned long)g_elf_tail_end);
    }
    if (hits != 1) return 0;
    if (out) *out = found;
    return 1;
}

static int exact_stub_targets_cave(pid_t pid, uintptr_t base,
                                   uintptr_t func_off, uintptr_t cave) {
    unsigned char code[WHITELIST_STUB_BYTES];
    uintptr_t table_abs = 0;
    uintptr_t expected_rel = cave + MAX_SLOTS * 64 + sizeof(WHITELIST_MARKER);
    expected_rel = (expected_rel + 7) & ~(uintptr_t)7;
    uintptr_t expected_table = base + expected_rel;
    if ((cave & 15u) != 0) return 0;
    if (mem_r(pid, base + func_off, code, sizeof(code)) != 0 ||
        !exact_whitelist_stub_shape(code, sizeof(code)) ||
        !decode_stub_table(base + func_off, code, &table_abs)) {
        return 0;
    }
    return table_abs == expected_table;
}

static int exact_stub_cave_from_code(pid_t pid, uintptr_t base,
                                     uintptr_t func_off, uintptr_t *out) {
    if (out) *out = 0;
    unsigned char code[WHITELIST_STUB_BYTES];
    uintptr_t table_abs = 0;
    if (mem_r(pid, base + func_off, code, sizeof(code)) != 0 ||
        !exact_whitelist_stub_shape(code, sizeof(code)) ||
        !decode_stub_table(base + func_off, code, &table_abs) ||
        table_abs < base) {
        return 0;
    }
    uintptr_t table_rel = table_abs - base;
    uintptr_t table_delta = MAX_SLOTS * 64u + sizeof(WHITELIST_MARKER);
    table_delta = (table_delta + 7u) & ~(uintptr_t)7u;
    if (table_rel < table_delta) return 0;
    uintptr_t cave = table_rel - table_delta;
    if ((cave & 15u) != 0 || g_elf_tail_end <= g_elf_tail_start ||
        g_elf_tail_end - g_elf_tail_start < WHITELIST_CAVE_BYTES ||
        cave < g_elf_tail_start ||
        cave > g_elf_tail_end - WHITELIST_CAVE_BYTES ||
        !exact_stub_targets_cave(pid, base, func_off, cave)) {
        return 0;
    }

    uint64_t ptrs[MAX_SLOTS];
    if (mem_r(pid, table_abs, ptrs, sizeof(ptrs)) != 0) return 0;
    for (int i = 0; i < MAX_SLOTS; ++i) {
        uintptr_t expected = base + cave + (uintptr_t)i * 64u;
        if (ptrs[i] != 0 && ptrs[i] != expected) return 0;
    }
    fprintf(stderr,
            "[a2h_patch] exact stub recovered safe cave rel=0x%lx table=0x%lx\n",
            (unsigned long)cave, (unsigned long)table_rel);
    if (out) *out = cave;
    return 1;
}

/* A whitelist->global transition intentionally needs only a short return
 * sequence. The previous matcher suffix can therefore remain after either an
 * old 8-byte or current 16-byte global overlay. Treat that suffix as ours only
 * when every remaining matcher word and the marked cave/pointer table agree. */
static int global_stub_suffix_overlay(const unsigned char *head, size_t n) {
    if (patched_global_stub_tail(head, n) &&
        exact_whitelist_stub_suffix(head, n, 2)) return 8;
    if (n >= sizeof(GLOBAL_PATCH) &&
        memcmp(head, GLOBAL_PATCH, sizeof(GLOBAL_PATCH)) == 0 &&
        exact_whitelist_stub_suffix(head, n, 4)) return 16;
    return 0;
}

static int owned_global_stub_suffix_ok(pid_t pid, uintptr_t base,
                                       const unsigned char *head, size_t n) {
    int overlay = global_stub_suffix_overlay(head, n);
    if (!overlay) return 0;

    uintptr_t cave = 0;
    const char *source = "none";
    int owned = load_cave_hint(&cave) && marked_cave_ok(pid, base, cave);
    if (owned) {
        source = "hint";
    } else if (find_unique_marked_cave(pid, base, &cave)) {
        owned = 1;
        source = "safe-tail-scan";
    }
    fprintf(stderr,
            "[a2h_patch] global stale-stub overlay=%s suffix=exact cave=%s source=%s rel=0x%lx\n",
            overlay == 8 ? "8-byte" : "16-byte", owned ? "owned" : "rejected",
            source, (unsigned long)cave);
    return owned;
}

static int patched_global_candidate_ok(pid_t pid, uintptr_t base,
                                       const unsigned char *head, size_t n) {
    if (!patched_global_tail_ok(head, n)) return 0;
    if (patched_global_stub_tail(head, n) ||
        (n >= WHITELIST_STUB_BYTES &&
         exact_whitelist_stub_suffix(head, n, 4))) {
        return owned_global_stub_suffix_ok(pid, base, head, n);
    }
    return 1;
}

static int score_profile(pid_t pid, uintptr_t base, const profile_t *prof) {
    int score = 0; unsigned char head[16] = {0};
    if (mem_r(pid, base + prof->func_off, head, sizeof(head)) != 0) return -1000;
    if (memcmp(head, SIG8, 8) == 0) score += 50;
    if (memcmp(head, PATCH, 8) == 0) {
        score += patched_global_tail_ok(head, sizeof(head)) ? 40 : 10;
    }
    if (is_stub_head(head, sizeof(head))) score += 35;
    for (int i = 0; i < 6; ++i) {
        char got[64]={0}, exp[64]={0}; pkg_default(i, exp, sizeof(exp));
        size_t slot_len = prof->slot_len[i] > 0 ? (size_t)prof->slot_len[i] : 0;
        if (!slot_len || mem_r(pid, base + prof->slot_off[i], got, slot_len) != 0) {
            score -= 5;
            continue;
        }
        if (exp[0] && strncmp(got, exp, slot_len) == 0) score += 10;
        else if (got[0] && strchr(got, '.')) score += 2;
        else score -= 3;
    }
    fprintf(stderr, "[a2h_patch] profile %s (%s) score=%d head=%02x %02x %02x %02x\n",
            prof->name, prof->hint, score, head[0], head[1], head[2], head[3]);
    return score;
}

static int profile_official_strings_exact(pid_t pid, uintptr_t base,
                                          const profile_t *prof) {
    if (!prof) return 0;
    for (int i = 0; i < 6; ++i) {
        char got[64] = {0};
        char exp[64] = {0};
        pkg_default(i, exp, sizeof(exp));
        size_t n = strlen(exp);
        if (n != (size_t)prof->slot_len[i] ||
            mem_r(pid, base + prof->slot_off[i], got, n + 1) != 0 ||
            memcmp(got, exp, n + 1) != 0) {
            return 0;
        }
    }
    return 1;
}

#define ELF_SYMBOL_MAX_BYTES 4096u
#define ELF_TABLE_MAX_BYTES (32u * 1024u * 1024u)

typedef enum {
    ELF_A2H_NONE = 0,
    ELF_A2H_STOCK,
    ELF_A2H_GLOBAL,
    ELF_A2H_WHITELIST,
    ELF_A2H_OWNED_ABS_JUMP
} elf_a2h_state_t;

enum {
    ELF_RESOLVE_UNAVAILABLE = 0,
    ELF_RESOLVE_VERIFIED = 1,
    ELF_RESOLVE_REJECTED = -1
};

typedef struct {
    uintptr_t vaddr;
    uintptr_t file_off;
    size_t size;
} elf_a2h_symbol_t;

static const char *elf_a2h_state_name(elf_a2h_state_t state) {
    switch (state) {
        case ELF_A2H_STOCK: return "stock";
        case ELF_A2H_GLOBAL: return "global";
        case ELF_A2H_WHITELIST: return "whitelist-exact";
        case ELF_A2H_OWNED_ABS_JUMP: return "owned-abs-jump";
        default: return "none";
    }
}

static int file_range_ok(uint64_t off, uint64_t len, uint64_t file_size) {
    return off <= file_size && len <= file_size - off;
}

static int pread_exact(int fd, void *buf, size_t len, uint64_t off) {
    unsigned char *dst = (unsigned char *)buf;
    size_t done = 0;
    while (done < len) {
        ssize_t n = pread(fd, dst + done, len - done, (off_t)(off + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        done += (size_t)n;
    }
    return 1;
}

static int open_mapped_hal(pid_t pid, uint64_t *file_size) {
    if (!g_libpath[0] || g_libpath[0] != '/' || !g_lib_inode ||
        strstr(g_libpath, " (deleted)")) {
        fprintf(stderr, "[a2h_patch] ELF symbol unavailable: live HAL path is missing/deleted\n");
        return -1;
    }
    char rooted[640];
    int nr = snprintf(rooted, sizeof(rooted), "/proc/%d/root%s", pid, g_libpath);
    int fd = (nr > 0 && (size_t)nr < sizeof(rooted)) ?
             open(rooted, O_RDONLY | O_CLOEXEC) : -1;
    if (fd < 0) fd = open(g_libpath, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        fprintf(stderr, "[a2h_patch] ELF symbol unavailable: open %s failed errno=%d\n",
                g_libpath, errno);
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 ||
        (unsigned long long)st.st_ino != g_lib_inode ||
        (unsigned int)major(st.st_dev) != g_lib_dev_major ||
        (unsigned int)minor(st.st_dev) != g_lib_dev_minor) {
        fprintf(stderr,
                "[a2h_patch] ELF symbol unavailable: mapped file identity mismatch path=%s\n",
                g_libpath);
        close(fd);
        return -1;
    }
    if (file_size) *file_size = (uint64_t)st.st_size;
    return fd;
}

static int path_is_owned_a2h_hook(const char *path) {
    if (!path || !path[0]) return 0;
    return strstr(path, "/data/adb/modules/a2h_hook/") != NULL ||
           strstr(path, "/data/adb/modules_update/a2h_hook/") != NULL;
}

static int exec_map_for_addr(pid_t pid, uintptr_t addr, char *path, size_t cap) {
    if (path && cap) path[0] = 0;
    char mp[64];
    snprintf(mp, sizeof(mp), "/proc/%d/maps", pid);
    FILE *f = fopen(mp, "r");
    if (!f) return 0;
    char line[1024];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        uintptr_t s = 0, e = 0;
        unsigned long map_off = 0;
        unsigned int dev_major = 0, dev_minor = 0;
        unsigned long long inode = 0;
        char perms[8] = {0};
        int path_pos = 0;
        int fields = sscanf(line, "%lx-%lx %7s %lx %x:%x %llu %n",
                            &s, &e, perms, &map_off, &dev_major, &dev_minor,
                            &inode, &path_pos);
        if (fields != 7 || addr < s || addr >= e || !strchr(perms, 'x')) continue;
        if (path && cap && path_pos > 0) {
            char *src = line + path_pos;
            while (*src == ' ' || *src == '\t') src++;
            src[strcspn(src, "\r\n")] = 0;
            snprintf(path, cap, "%s", src[0] ? src : "[anonymous]");
        }
        found = 1;
        break;
    }
    fclose(f);
    return found;
}

static int parse_unique_a2h_symbol(int fd, uint64_t file_size,
                                   elf_a2h_symbol_t *out) {
    Elf64_Ehdr eh;
    if (!out || !pread_exact(fd, &eh, sizeof(eh), 0) ||
        memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS64 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB ||
        eh.e_ident[EI_VERSION] != EV_CURRENT || eh.e_version != EV_CURRENT ||
        eh.e_type != ET_DYN || eh.e_machine != EM_AARCH64 ||
        eh.e_ehsize != sizeof(Elf64_Ehdr) ||
        eh.e_phentsize != sizeof(Elf64_Phdr) || eh.e_phnum == 0 || eh.e_phnum > 128) {
        fprintf(stderr, "[a2h_patch] ELF symbol rejected: unsupported ELF64 header\n");
        return ELF_RESOLVE_REJECTED;
    }
    if (eh.e_shnum == 0) {
        fprintf(stderr,
                "[a2h_patch] ELF symbol unavailable: section table stripped\n");
        return ELF_RESOLVE_UNAVAILABLE;
    }
    if (eh.e_shentsize != sizeof(Elf64_Shdr) || eh.e_shnum > 4096) {
        fprintf(stderr, "[a2h_patch] ELF symbol rejected: invalid section table\n");
        return ELF_RESOLVE_REJECTED;
    }
    uint64_t ph_bytes = (uint64_t)eh.e_phnum * sizeof(Elf64_Phdr);
    uint64_t sh_bytes = (uint64_t)eh.e_shnum * sizeof(Elf64_Shdr);
    if (!file_range_ok(eh.e_phoff, ph_bytes, file_size) ||
        !file_range_ok(eh.e_shoff, sh_bytes, file_size)) {
        fprintf(stderr, "[a2h_patch] ELF symbol unavailable: header tables outside file\n");
        return ELF_RESOLVE_REJECTED;
    }
    Elf64_Phdr *ph = (Elf64_Phdr *)malloc((size_t)ph_bytes);
    Elf64_Shdr *sh = (Elf64_Shdr *)malloc((size_t)sh_bytes);
    if (!ph || !sh || !pread_exact(fd, ph, (size_t)ph_bytes, eh.e_phoff) ||
        !pread_exact(fd, sh, (size_t)sh_bytes, eh.e_shoff)) {
        free(ph); free(sh);
        return ELF_RESOLVE_REJECTED;
    }

    int named = 0, invalid = 0, unique = 0;
    elf_a2h_symbol_t candidate = {0};
    static const char symbol_name[] = "is_A2H_app";
    for (size_t i = 0; i < eh.e_shnum; ++i) {
        if (sh[i].sh_type != SHT_DYNSYM && sh[i].sh_type != SHT_SYMTAB) continue;
        if (sh[i].sh_entsize != sizeof(Elf64_Sym) ||
            sh[i].sh_size == 0 || sh[i].sh_size > ELF_TABLE_MAX_BYTES ||
            sh[i].sh_size % sizeof(Elf64_Sym) != 0 || sh[i].sh_link >= eh.e_shnum ||
            sh[sh[i].sh_link].sh_type != SHT_STRTAB ||
            sh[sh[i].sh_link].sh_size == 0 ||
            sh[sh[i].sh_link].sh_size > ELF_TABLE_MAX_BYTES ||
            !file_range_ok(sh[i].sh_offset, sh[i].sh_size, file_size) ||
            !file_range_ok(sh[sh[i].sh_link].sh_offset,
                           sh[sh[i].sh_link].sh_size, file_size)) {
            continue;
        }
        size_t sym_bytes = (size_t)sh[i].sh_size;
        size_t str_bytes = (size_t)sh[sh[i].sh_link].sh_size;
        Elf64_Sym *syms = (Elf64_Sym *)malloc(sym_bytes);
        char *strings = (char *)malloc(str_bytes);
        if (!syms || !strings ||
            !pread_exact(fd, syms, sym_bytes, sh[i].sh_offset) ||
            !pread_exact(fd, strings, str_bytes, sh[sh[i].sh_link].sh_offset)) {
            free(syms); free(strings);
            invalid = 1;
            break;
        }
        size_t count = sym_bytes / sizeof(Elf64_Sym);
        for (size_t j = 0; j < count; ++j) {
            Elf64_Sym *sym = &syms[j];
            if (sym->st_name >= str_bytes) continue;
            size_t remain = str_bytes - sym->st_name;
            const char *name = strings + sym->st_name;
            const char *nul = (const char *)memchr(name, 0, remain);
            if (!nul || (size_t)(nul - name) != sizeof(symbol_name) - 1 ||
                memcmp(name, symbol_name, sizeof(symbol_name)) != 0) continue;
            named++;
            if (ELF64_ST_TYPE(sym->st_info) != STT_FUNC ||
                ELF64_ST_BIND(sym->st_info) != STB_GLOBAL ||
                sym->st_shndx == SHN_UNDEF || sym->st_shndx >= eh.e_shnum ||
                sh[sym->st_shndx].sh_type != SHT_PROGBITS ||
                !(sh[sym->st_shndx].sh_flags & SHF_ALLOC) ||
                !(sh[sym->st_shndx].sh_flags & SHF_EXECINSTR) ||
                sym->st_size < sizeof(GLOBAL_PATCH) ||
                sym->st_size > ELF_SYMBOL_MAX_BYTES ||
                sym->st_value > UINTPTR_MAX || sym->st_size > UINTPTR_MAX - sym->st_value ||
                sh[sym->st_shndx].sh_size >
                    UINT64_MAX - sh[sym->st_shndx].sh_addr ||
                sym->st_value < sh[sym->st_shndx].sh_addr ||
                sym->st_value + sym->st_size >
                    sh[sym->st_shndx].sh_addr + sh[sym->st_shndx].sh_size) {
                invalid = 1;
                continue;
            }
            int load_hits = 0;
            uint64_t file_off = 0;
            for (size_t k = 0; k < eh.e_phnum; ++k) {
                if (ph[k].p_type != PT_LOAD || !(ph[k].p_flags & PF_X) ||
                    ph[k].p_filesz > UINT64_MAX - ph[k].p_vaddr ||
                    sym->st_value < ph[k].p_vaddr ||
                    sym->st_value + sym->st_size > ph[k].p_vaddr + ph[k].p_filesz)
                    continue;
                uint64_t delta = sym->st_value - ph[k].p_vaddr;
                if (delta > UINT64_MAX - ph[k].p_offset ||
                    !file_range_ok(ph[k].p_offset + delta, sym->st_size, file_size))
                    continue;
                file_off = ph[k].p_offset + delta;
                load_hits++;
            }
            uint64_t section_delta = sym->st_value - sh[sym->st_shndx].sh_addr;
            int section_file_ok =
                section_delta <= UINT64_MAX - sh[sym->st_shndx].sh_offset &&
                file_range_ok(sh[sym->st_shndx].sh_offset + section_delta,
                              sym->st_size, file_size) &&
                sh[sym->st_shndx].sh_offset + section_delta == file_off;
            if (load_hits != 1 || file_off > UINTPTR_MAX || !section_file_ok) {
                invalid = 1;
                continue;
            }
            elf_a2h_symbol_t next = {
                (uintptr_t)sym->st_value, (uintptr_t)file_off, (size_t)sym->st_size
            };
            if (!unique) {
                candidate = next;
                unique = 1;
            } else if (candidate.vaddr != next.vaddr ||
                       candidate.file_off != next.file_off || candidate.size != next.size) {
                unique++;
            }
        }
        free(syms); free(strings);
    }
    free(ph); free(sh);
    if (invalid || (named > 0 && unique != 1)) {
        fprintf(stderr,
                "[a2h_patch] ELF symbol rejected: named=%d unique=%d invalid=%d\n",
                named, unique, invalid);
        return ELF_RESOLVE_REJECTED;
    }
    if (named == 0) {
        fprintf(stderr, "[a2h_patch] ELF symbol unavailable: is_A2H_app stripped\n");
        return ELF_RESOLVE_UNAVAILABLE;
    }
    *out = candidate;
    fprintf(stderr,
            "[a2h_patch] ELF symbol is_A2H_app vaddr=0x%lx size=%lu file_off=0x%lx entries=%d\n",
            (unsigned long)out->vaddr, (unsigned long)out->size,
            (unsigned long)out->file_off, named);
    return ELF_RESOLVE_VERIFIED;
}

/* A symbol identifies the function, not ownership of a runtime hook.  A 16-byte
 * absolute jump is accepted only when its target maps to this module; an exact
 * stock tail alone is deliberately insufficient to overwrite a foreign hook. */
static int resolve_func_by_elf_symbol(pid_t pid, uintptr_t base,
                                      uintptr_t *out_off,
                                      elf_a2h_state_t *out_state) {
    if (out_state) *out_state = ELF_A2H_NONE;
    uint64_t file_size = 0;
    int fd = open_mapped_hal(pid, &file_size);
    if (fd < 0) return 0;
    elf_a2h_symbol_t sym;
    int parsed = parse_unique_a2h_symbol(fd, file_size, &sym);
    if (parsed != ELF_RESOLVE_VERIFIED) {
        close(fd);
        return parsed;
    }
    if (sym.size < sizeof(GLOBAL_PATCH) || base > UINTPTR_MAX - sym.vaddr) {
        fprintf(stderr,
                "[a2h_patch] ELF symbol rejected: capacity=%lu required_global=%lu\n",
                (unsigned long)sym.size, (unsigned long)sizeof(GLOBAL_PATCH));
        close(fd);
        return ELF_RESOLVE_REJECTED;
    }
    unsigned char *disk = (unsigned char *)malloc(sym.size);
    unsigned char *live = (unsigned char *)malloc(sym.size);
    if (!disk || !live || !pread_exact(fd, disk, sym.size, sym.file_off) ||
        mem_r(pid, base + sym.vaddr, live, sym.size) != 0) {
        free(disk); free(live); close(fd);
        fprintf(stderr, "[a2h_patch] ELF symbol unavailable: function bytes unreadable\n");
        return ELF_RESOLVE_REJECTED;
    }
    close(fd);
    uintptr_t func_abs = base + sym.vaddr;
    if (sym.size > UINTPTR_MAX - func_abs ||
        memcmp(disk, SIG8, sizeof(SIG8)) != 0 ||
        memcmp(disk + sizeof(SIG8), STOCK_TAIL8, sizeof(STOCK_TAIL8)) != 0 ||
        func_abs < g_rx_start || func_abs + sym.size > g_rx_end) {
        fprintf(stderr,
                "[a2h_patch] ELF symbol rejected: disk stock entry/RX range mismatch\n");
        free(disk); free(live);
        return ELF_RESOLVE_REJECTED;
    }

    int stale_overlay = sym.size >= WHITELIST_STUB_BYTES ?
                        global_stub_suffix_overlay(live, sym.size) : 0;
    int stale_owned = stale_overlay ?
                      owned_global_stub_suffix_ok(pid, base, live, sym.size) : 0;
    elf_a2h_state_t state = ELF_A2H_NONE;
    if (memcmp(live, disk, sym.size) == 0) {
        state = ELF_A2H_STOCK;
    } else if (stale_owned &&
               memcmp(live + WHITELIST_STUB_BYTES,
                      disk + WHITELIST_STUB_BYTES,
                      sym.size - WHITELIST_STUB_BYTES) == 0) {
        state = ELF_A2H_GLOBAL;
    } else if (!stale_overlay &&
               patched_global_candidate_ok(pid, base, live, sym.size) &&
               memcmp(live + 16, disk + 16, sym.size - 16) == 0) {
        state = ELF_A2H_GLOBAL;
    } else if (sym.size >= WHITELIST_STUB_BYTES &&
               exact_whitelist_stub_shape(live, sym.size) &&
               memcmp(live + WHITELIST_STUB_BYTES,
                      disk + WHITELIST_STUB_BYTES,
                      sym.size - WHITELIST_STUB_BYTES) == 0) {
        state = ELF_A2H_WHITELIST;
    } else if (load_u32le(live) == 0x58000051u &&
               load_u32le(live + 4) == 0xD61F0220u &&
               memcmp(live + 16, disk + 16, sym.size - 16) == 0) {
        uintptr_t target = 0;
        memcpy(&target, live + 8, sizeof(target));
        char owner[512] = {0};
        int mapped = target && !(target & 3u) &&
                     exec_map_for_addr(pid, target, owner, sizeof(owner));
        int owned = mapped && path_is_owned_a2h_hook(owner);
        fprintf(stderr,
                "[a2h_patch] ELF symbol absolute-jump target=0x%lx owner=%s owned=%d tail=%lu\n",
                (unsigned long)target, mapped ? owner : "(unmapped)", owned,
                (unsigned long)(sym.size - 16));
        if (owned) state = ELF_A2H_OWNED_ABS_JUMP;
        else fprintf(stderr,
                     "[a2h_patch] ERROR: foreign/unowned 16-byte hook; refusing overwrite\n");
    } else {
        fprintf(stderr,
                "[a2h_patch] ELF symbol rejected: runtime bytes are neither stock nor an owned A2H state\n");
    }
    free(disk); free(live);
    if (state == ELF_A2H_NONE) return ELF_RESOLVE_REJECTED;
    if (out_off) *out_off = sym.vaddr;
    if (out_state) *out_state = state;
    g_func_capacity = sym.size;
    fprintf(stderr,
            "[a2h_patch] ELF symbol verified func=0x%lx capacity=%lu state=%s\n",
            (unsigned long)sym.vaddr, (unsigned long)g_func_capacity,
            elf_a2h_state_name(state));
    return ELF_RESOLVE_VERIFIED;
}

// Prefer a unique stock prologue. Patched candidates must also be unambiguous
// and, for a legacy stub tail, prove ownership of the recorded cave marker.
static int scan_func_by_sig(pid_t pid, uintptr_t base, uintptr_t *out_off,
                            size_t *out_capacity) {
    // Locate is_A2H_app even after patching:
    // 1) stock prologue SIG8
    // 2) global patch (mov w0,#1; ret)
    // 3) our whitelist stub head (ADRP x1; ADD x1; MOV w2,#10)
    // Never full-text ADRP scan: too many false positives.
    if (out_capacity) *out_capacity = 0;
    if (!g_libpath[0] || !g_lib_inode) {
        fprintf(stderr,
                "[a2h_patch] ERROR: executable scan requires exact HAL map identity\n");
        return 0;
    }
    char mp[64]; snprintf(mp,sizeof(mp),"/proc/%d/maps",pid);
    FILE *f=fopen(mp,"r"); if(!f) return 0;
    char line[512];
    int segs=0, hits_stock=0, hits_global_raw=0, hits_global=0;
    int hits_stub_raw=0, hits_stub=0, hits_stub_stale_exact=0;
    uintptr_t best_stock=0, best_global=0, best_stub=0, best_stub_stale=0;
    size_t best_global_capacity = 0;
    fprintf(stderr, "[a2h_patch] Scanning exec for stock/global/stub is_A2H_app...\n");
    while (fgets(line,sizeof(line),f)) {
        uintptr_t s=0,e=0; unsigned long off=0; char perms[8]={0};
        unsigned int dev_major=0,dev_minor=0;
        unsigned long long inode=0;
        int path_pos=0;
        if (sscanf(line, "%lx-%lx %7s %lx %x:%x %llu %n", &s, &e,
                   perms, &off, &dev_major, &dev_minor, &inode, &path_pos) != 7 ||
            !strchr(perms,'x') || dev_major != g_lib_dev_major ||
            dev_minor != g_lib_dev_minor || inode != g_lib_inode || path_pos <= 0) {
            continue;
        }
        char *mapped_path=line+path_pos;
        while (*mapped_path==' ' || *mapped_path=='\t') mapped_path++;
        mapped_path[strcspn(mapped_path,"\r\n")]=0;
        if (strcmp(mapped_path,g_libpath)!=0) continue;
        segs++;
        size_t len = (size_t)(e - s);
        if (len < 16 || len > 16*1024*1024) continue;
        unsigned char *buf = (unsigned char*)malloc(len);
        if (!buf) continue;
        if (mem_r(pid, s, buf, len) != 0) { free(buf); continue; }
        for (size_t i=0; i+16<=len; i+=4) {
            uintptr_t abs = s + i;
            if (abs < base) continue;
            uintptr_t rel = abs - base;
            // v1.0 succeeded by treating the executable segment as the source of truth.
            // Keep a broad but sane relative range so OS2/OS3 minor rebuilds do not depend on profiles.
            if (rel < 0x10000 || rel > 0x2000000) continue;
            const unsigned char *h = buf + i;
            if (memcmp(h, SIG8, sizeof(SIG8)) == 0 &&
                memcmp(h + sizeof(SIG8), STOCK_TAIL8,
                       sizeof(STOCK_TAIL8)) == 0) {
                hits_stock++; best_stock = rel;
                fprintf(stderr, "[a2h_patch] FOUND stock at rel=0x%lx\n", (unsigned long)rel);
                continue;
            }
            if (memcmp(h, PATCH, 8) == 0) {
                hits_global_raw++;
                if (patched_global_candidate_ok(pid, base, h, len - i)) {
                    hits_global++;
                    best_global = rel;
                    best_global_capacity =
                        global_stub_suffix_overlay(h, len - i) ?
                        WHITELIST_STUB_BYTES : sizeof(GLOBAL_PATCH);
                }
                continue;
            }
            if (is_stub_head(h, 16)) {
                hits_stub_raw++;
                if (i + WHITELIST_STUB_BYTES <= len) {
                    stub_info_t si;
                    if (inspect_stub_table(pid, base, rel, h, NULL, 0, &si)) {
                        hits_stub++;
                        best_stub = rel;
                    } else if (exact_whitelist_stub_shape(h, len - i)) {
                        hits_stub_stale_exact++;
                        best_stub_stale = rel;
                    }
                }
            }
        }
        free(buf);
    }
    fclose(f);
    fprintf(stderr,
            "[a2h_patch] func scan candidates segs=%d stock=%d global_raw=%d global_tail=%d stub_raw=%d stub_valid=%d stub_stale_exact=%d\n",
            segs, hits_stock, hits_global_raw, hits_global, hits_stub_raw,
            hits_stub, hits_stub_stale_exact);
    uintptr_t best = 0;
    const char *kind = "none";
    int patched_hits = hits_global + hits_stub + hits_stub_stale_exact;
    if (hits_stock == 1 && patched_hits == 0) {
        best = best_stock;
        kind = "stock";
    } else if (hits_stock > 1 || (hits_stock > 0 && patched_hits > 0)) {
        snprintf(g_scan_kind, sizeof(g_scan_kind), "ambiguous");
        fprintf(stderr,
                "[a2h_patch] ERROR: ambiguous cross-state candidates stock=%d global=%d stub=%d stale=%d\n",
                hits_stock, hits_global, hits_stub, hits_stub_stale_exact);
        return 0;
    } else if (patched_hits == 1) {
        if (hits_global == 1) { best = best_global; kind = "global"; }
        else if (hits_stub == 1) { best = best_stub; kind = "stub"; }
        else { best = best_stub_stale; kind = "stub-stale-exact"; }
    } else if (patched_hits > 1) {
        snprintf(g_scan_kind, sizeof(g_scan_kind), "ambiguous");
        fprintf(stderr,
                "[a2h_patch] ERROR: ambiguous patched candidates global=%d stub_valid=%d stub_stale_exact=%d\n",
                hits_global, hits_stub, hits_stub_stale_exact);
        return 0;
    } else if (hits_global_raw > 0 || hits_stub_raw > 0) {
        snprintf(g_scan_kind, sizeof(g_scan_kind), "ambiguous");
        fprintf(stderr,
                "[a2h_patch] ERROR: unverified raw patched candidates global=%d stub=%d; refusing unsafe fallback\n",
                hits_global_raw, hits_stub_raw);
        return 0;
    }
    if (!best) {
        snprintf(g_scan_kind, sizeof(g_scan_kind), "miss");
        fprintf(stderr, "[a2h_patch] func scan miss\n");
        return 0;
    }
    if (out_off) *out_off = best;
    if (out_capacity) {
        if (strcmp(kind, "global") == 0) {
            *out_capacity = best_global_capacity;
        } else if (strcmp(kind,"stub")==0 ||
                   strcmp(kind,"stub-stale-exact")==0) {
            *out_capacity = WHITELIST_STUB_BYTES;
        } else {
            *out_capacity = sizeof(GLOBAL_PATCH);
        }
    }
    snprintf(g_scan_kind, sizeof(g_scan_kind), "%s", kind);
    fprintf(stderr, "[a2h_patch] is_A2H_app: 0x%lx kind=%s stock=%d global_raw=%d global_tail=%d stub_valid=%d stub_stale_exact=%d segs=%d\n",
            (unsigned long)best, kind, hits_stock, hits_global_raw, hits_global,
            hits_stub, hits_stub_stale_exact, segs);
    return 1;
}
static int score_string_rel(uintptr_t rel, uintptr_t rw_rel_s, uintptr_t rw_rel_e) {
    // Reject RW segment copies (e.g. 0x435xxx). Prefer real rodata around 0xA0000-0x120000.
    if (rw_rel_e > rw_rel_s && rel >= rw_rel_s && rel < rw_rel_e) return -1000;
    if (rel >= 0xA0000 && rel <= 0x120000) return 100;
    if (rel >= 0x80000 && rel <= 0x200000) return 80;
    if (rel >= 0x20000 && rel < 0x80000) return 40;
    if (rel > 0x200000 && rel < 0x400000) return 10;
    return 1;
}
static int scan_official_strings(pid_t pid, uintptr_t base) {
    if (!g_libpath[0] || !g_lib_inode) return 0;
    char mp[64]; snprintf(mp,sizeof(mp),"/proc/%d/maps",pid);
    FILE *f=fopen(mp,"r"); if(!f) return 0;
    typedef struct { uintptr_t s,e; unsigned long off; char perms[8]; } seg_t;
    seg_t segs[32]; int nseg=0;
    char line[1024];
    uintptr_t rw_rel_s=0, rw_rel_e=0;
    while (fgets(line,sizeof(line),f) && nseg < 32) {
        uintptr_t s=0,e=0; unsigned long off=0; char perms[8]={0};
        unsigned int dev_major=0,dev_minor=0;
        unsigned long long inode=0;
        int path_pos=0;
        if (sscanf(line, "%lx-%lx %7s %lx %x:%x %llu %n", &s, &e,
                   perms, &off, &dev_major, &dev_minor, &inode, &path_pos) != 7 ||
            dev_major != g_lib_dev_major || dev_minor != g_lib_dev_minor ||
            inode != g_lib_inode || path_pos <= 0) {
            continue;
        }
        char *mapped_path=line+path_pos;
        while (*mapped_path==' ' || *mapped_path=='\t') mapped_path++;
        mapped_path[strcspn(mapped_path,"\r\n")]=0;
        if (strcmp(mapped_path,g_libpath)!=0) continue;
        segs[nseg].s=s; segs[nseg].e=e; segs[nseg].off=off; snprintf(segs[nseg].perms,sizeof(segs[nseg].perms),"%s",perms);
        nseg++;
        if (strchr(perms,'w') && s >= base) {
            uintptr_t rs = s - base, re = e - base;
            if (!rw_rel_s || rs < rw_rel_s) rw_rel_s = rs;
            if (re > rw_rel_e) rw_rel_e = re;
        }
    }
    fclose(f);
    if (!nseg) return 0;

    int found = 0;
    for (int i=0;i<6;i++) {
        char exp[64]; pkg_default(i, exp, sizeof(exp));
        size_t n = strlen(exp);
        int best_score = -100000;
        uintptr_t chosen = 0;
        int candidates = 0;
        for (int si=0; si<nseg; ++si) {
            if (strchr(segs[si].perms,'x') && !strchr(segs[si].perms,'r')) continue;
            // Prefer non-writable segments first for package strings.
            size_t len = (size_t)(segs[si].e - segs[si].s);
            if (len < n+1 || len > 8*1024*1024) continue;
            unsigned char *buf = (unsigned char*)malloc(len);
            if (!buf) continue;
            if (mem_r(pid, segs[si].s, buf, len) != 0) { free(buf); continue; }
            for (size_t off=0; off + n + 1 <= len; ++off) {
                if (memcmp(buf+off, exp, n) != 0 || buf[off+n] != 0) continue;
                uintptr_t abs = segs[si].s + off;
                if (abs < base) continue;
                uintptr_t rel = abs - base;
                candidates++;
                int sc = score_string_rel(rel, rw_rel_s, rw_rel_e);
                if (strchr(segs[si].perms,'w')) sc -= 50;
                if (sc > best_score || (sc == best_score && (chosen == 0 || rel < chosen))) {
                    best_score = sc;
                    chosen = rel;
                }
            }
            free(buf);
        }
        if (best_score > 0 && chosen) {
            set_slot_off(i, chosen);
            found++;
            fprintf(stderr, "[a2h_patch] strscan slot%d '%s' -> 0x%lx score=%d cand=%d maxlen=%d\n",
                    i, exp, (unsigned long)slot_off(i), best_score, candidates, slots[i].max_len);
        } else {
            fprintf(stderr, "[a2h_patch] strscan miss slot%d '%s' cand=%d\n", i, exp, candidates);
        }
    }
    return found;
}
static int memory_is_zero(pid_t pid, uintptr_t start, uintptr_t len) {
    unsigned char buf[256];
    while (len) {
        size_t n = len < sizeof(buf) ? (size_t)len : sizeof(buf);
        if (mem_r(pid, start, buf, n) != 0) return 0;
        for (size_t i = 0; i < n; ++i) {
            if (buf[i] != 0) return 0;
        }
        start += n;
        len -= n;
    }
    return 1;
}
static int locate_targets(pid_t pid, uintptr_t base) {
    g_scan_kind[0] = 0;

    /* The mapped-file symbol is authoritative when present.  A malformed
     * symbol or unowned runtime hook is a hard rejection, not permission to
     * search for a more convenient function elsewhere in the executable. */
    uintptr_t elf_off = 0;
    elf_a2h_state_t elf_state = ELF_A2H_NONE;
    int elf_result = resolve_func_by_elf_symbol(pid, base, &elf_off, &elf_state);
    if (elf_result == ELF_RESOLVE_REJECTED) {
        fprintf(stderr,
                "[a2h_patch] ERROR: authoritative ELF target rejected; fallback disabled\n");
        return 0;
    }

    /* Stripped HALs retain a conservative compatibility path.  Stock/global
     * scans prove only 16 writable code bytes; an exact module matcher proves
     * all 76 bytes and can therefore be repaired safely. */
    uintptr_t sig_off = 0;
    size_t sig_capacity = 0;
    int sig_ok = 0;
    if (elf_result == ELF_RESOLVE_UNAVAILABLE) {
        sig_ok = scan_func_by_sig(pid, base, &sig_off, &sig_capacity);
    }
    else snprintf(g_scan_kind, sizeof(g_scan_kind), "elf-symbol");

    /* Profiles seed diagnostic string offsets only.  They never select the
     * function target, so an OTA cannot become writable merely by reusing old
     * offsets and package literals. */
    int best=-1, best_score=-100000;
    for (size_t i=0;i<sizeof(PROFILES)/sizeof(PROFILES[0]);++i) {
        int sc=score_profile(pid, base, &PROFILES[i]);
        if (profile_official_strings_exact(pid, base, &PROFILES[i]) &&
            sc>best_score) { best_score=sc; best=(int)i; }
    }

    if (best >= 0) apply_profile(&PROFILES[best]);
    for (int i = 0; i < MAX_SLOTS; ++i) { slots[i].max_len = 63; slots[i].label = "x"; }
    int str_found = scan_official_strings(pid, base);
    if (best >= 0) {
        for (int i=0;i<6;i++) {
            uintptr_t cur = slot_off(i);
            int sc = score_string_rel(cur, g_rw_start, g_rw_end ? g_rw_end : 0x43A000);
            if (sc < 40) {
                set_slot_off(i, PROFILES[best].slot_off[i]);
                fprintf(stderr, "[a2h_patch] slot%d prefer profile off=0x%lx (strscan weak)\n",
                        i, (unsigned long)PROFILES[best].slot_off[i]);
            }
        }
    }

    if (elf_result == ELF_RESOLVE_VERIFIED) {
        g_func_off = elf_off;
        snprintf(g_locate_method, sizeof(g_locate_method), "%s",
                 elf_state == ELF_A2H_OWNED_ABS_JUMP ?
                 "elf-symbol+owned-jump" : "elf-symbol");
        snprintf(g_profile, sizeof(g_profile), "elf-symbol");
        snprintf(g_profile_hint, sizeof(g_profile_hint), "%s",
                 elf_a2h_state_name(elf_state));
        fprintf(stderr,
                "[a2h_patch] selected method=%s func=0x%lx capacity=%lu state=%s str_found=%d\n",
                g_locate_method, (unsigned long)g_func_off,
                (unsigned long)g_func_capacity,
                elf_a2h_state_name(elf_state), str_found);
        return 1;
    }

    if (sig_ok) {
        g_func_off = sig_off;
        g_func_capacity = sig_capacity;
        snprintf(g_locate_method, sizeof(g_locate_method), "scan");
        if (best >= 0) {
            // Keep package profile offsets if useful, even when function offset differs (OS2.0.220).
            snprintf(g_profile, sizeof(g_profile), "scan+%s", PROFILES[best].name);
            snprintf(g_profile_hint, sizeof(g_profile_hint), "%s", PROFILES[best].hint);
        } else {
            snprintf(g_profile, sizeof(g_profile), "scan");
            snprintf(g_profile_hint, sizeof(g_profile_hint), "universal");
        }
        fprintf(stderr,
                "[a2h_patch] selected method=%s func=0x%lx capacity=%lu kind=%s str_found=%d profile=%s\n",
                g_locate_method, (unsigned long)g_func_off,
                (unsigned long)g_func_capacity, g_scan_kind, str_found, g_profile);
        return 1;
    }

    fprintf(stderr,
            "[a2h_patch] ERROR: cannot locate is_A2H_app (elf=%d scan=%d best_score=%d)\n",
            elf_result, sig_ok, best_score);
    return 0;
}

static int setup_custom_cave(pid_t pid, uintptr_t base, uintptr_t rw_abs_s, uintptr_t rw_abs_e) {
    uintptr_t need = WHITELIST_CAVE_BYTES;
    if (!rw_abs_s || !rw_abs_e || rw_abs_s < base || rw_abs_e <= rw_abs_s) {
        fprintf(stderr, "[a2h_patch] ERROR: writable RW map unavailable; whitelist apply aborted\n");
        return 0;
    }
    uintptr_t rws = rw_abs_s - base;
    uintptr_t rwe = rw_abs_e - base;
    g_rw_start=rws; g_rw_end=rwe;
    if (!discover_elf_tail_region(pid, base, need)) {
        fprintf(stderr, "[a2h_patch] ERROR: no verified ELF tail for whitelist cave\n");
        return 0;
    }
    uintptr_t cave_abs = 0;
    uintptr_t hinted_cave = 0;
    int reused = 0;
    uintptr_t recovered_cave = 0;
    if (exact_stub_cave_from_code(pid, base, k_func_off(), &recovered_cave)) {
        cave_abs = base + recovered_cave;
        reused = 3;
    }
    if (!cave_abs && load_cave_hint(&hinted_cave) && hinted_cave >= g_elf_tail_start &&
        hinted_cave <= g_elf_tail_end - need) {
        /* A marker proves ownership only inside the unallocated ELF tail. */
        if (marked_cave_ok(pid, base, hinted_cave)) {
            cave_abs = base + hinted_cave;
            reused = 1;
            fprintf(stderr, "[a2h_patch] reuse marked cave rel=0x%lx\n",
                    (unsigned long)hinted_cave);
        } else if (exact_stub_targets_cave(pid, base, k_func_off(), hinted_cave)) {
            cave_abs = base + hinted_cave;
            reused = 2;
            fprintf(stderr,
                    "[a2h_patch] repair exact-stub-owned cave rel=0x%lx\n",
                    (unsigned long)hinted_cave);
        }
    }
    if (!cave_abs && find_unique_marked_cave(pid, base, &recovered_cave)) {
        cave_abs = base + recovered_cave;
        reused = 4;
    }
    if (!cave_abs) {
        cave_abs = base + g_elf_tail_candidate;
        if (!memory_is_zero(pid, cave_abs, need)) {
            fprintf(stderr,
                    "[a2h_patch] ERROR: ELF tail candidate is not zero rel=0x%lx need=0x%lx\n",
                    (unsigned long)g_elf_tail_candidate, (unsigned long)need);
            return 0;
        }
    }
    uintptr_t cave = cave_abs - base;
    const char *source = reused == 1 ? "elf-tail-reuse" :
                         (reused == 2 ? "elf-tail-stub-repair" :
                          (reused == 3 ? "elf-tail-stub-decode" :
                           (reused == 4 ? "elf-tail-marker-scan" : "elf-tail-zero")));
    for (int i = 0; i < MAX_SLOTS; ++i) {
        set_slot_off(i, cave + (uintptr_t)i * 64);
        slots[i].max_len = 63;
        slots[i].label = "x";
    }
    g_stub_mark = cave + MAX_SLOTS * 64;
    g_ptr_off = (g_stub_mark + 16 + 7) & ~((uintptr_t)7);
    fprintf(stderr, "[a2h_patch] RW rel=0x%lx-0x%lx cave=0x%lx source=%s ptr=0x%lx mark=0x%lx need=0x%lx\n",
            (unsigned long)g_rw_start,(unsigned long)g_rw_end,(unsigned long)cave,source,(unsigned long)g_ptr_off,(unsigned long)g_stub_mark,(unsigned long)need);
    return 1;
}
static int apply_strings(pid_t pid, uintptr_t base, char **pkgs) {
    unsigned char saved[MAX_SLOTS][64];
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (mem_r(pid, base + slot_off(i), saved[i], sizeof(saved[i])) != 0) {
            fprintf(stderr, "[a2h_patch] slot%d snapshot FAIL\n", i);
            return 0;
        }
    }
    for(int i=0;i<MAX_SLOTS;i++) {
        char buf[64]; memset(buf,0,sizeof(buf));
        size_t maxlen=(size_t)slots[i].max_len; if(maxlen>=sizeof(buf)) maxlen=sizeof(buf)-1;
        if(pkgs[i]&&pkgs[i][0]){ strncpy(buf,pkgs[i],maxlen); buf[maxlen]=0; }
        uintptr_t addr=base+slot_off(i);
        // always clear then write, avoid stale garbage in custom cave
        char z[64]; memset(z,0,sizeof(z));
        int crc=mem_w(pid,addr,z,maxlen+1);
        int wrc=crc==0 ? mem_w(pid,addr,buf,maxlen+1) : -1;
        char verify[64]; memset(verify,0,sizeof(verify));
        int rrc=wrc==0 ? mem_r(pid,addr,verify,maxlen+1) : -1;
        int same=(rrc==0 && memcmp(verify,buf,maxlen+1)==0);
        fprintf(stderr,"[a2h_patch] slot%d off=0x%lx clear=%s write=%s verify=%s value='%s'\n",
                i,(unsigned long)slot_off(i), crc==0?"OK":"FAIL", wrc==0?"OK":"FAIL",
                same?"OK":"FAIL", buf[0]?buf:"(empty)");
        if(crc!=0 || wrc!=0 || !same) {
            int restore_ok = 1;
            for (int j = 0; j < MAX_SLOTS; ++j) {
                unsigned char restored[64];
                uintptr_t restore_addr = base + slot_off(j);
                if (mem_w(pid, restore_addr, saved[j], sizeof(saved[j])) != 0 ||
                    mem_r(pid, restore_addr, restored, sizeof(restored)) != 0 ||
                    memcmp(restored, saved[j], sizeof(saved[j])) != 0)
                    restore_ok = 0;
            }
            fprintf(stderr, "[a2h_patch] string transaction rollback=%s after slot%d failure\n",
                    restore_ok ? "OK" : "FAIL", i);
            return 0;
        }
    }
    return 1;
}

typedef struct {
    uintptr_t func;
    uintptr_t cave;
    unsigned char func_before[WHITELIST_STUB_BYTES];
    unsigned char cave_before[WHITELIST_CAVE_BYTES];
    int valid;
} whitelist_transaction_t;

static int whitelist_transaction_begin(pid_t pid, uintptr_t base,
                                       whitelist_transaction_t *tx) {
    if (!tx || !slot_off(0) || g_ptr_off < slot_off(0) ||
        g_stub_mark < slot_off(0)) return 0;
    memset(tx, 0, sizeof(*tx));
    tx->func = base + k_func_off();
    tx->cave = base + slot_off(0);
    uintptr_t cave_end = slot_off(0) + WHITELIST_CAVE_BYTES;
    for (int i = 0; i < MAX_SLOTS; ++i) {
        if (slot_off(i) != slot_off(0) + (uintptr_t)i * 64) {
            fprintf(stderr,
                    "[a2h_patch] transaction layout rejected slot=%d off=0x%lx\n",
                    i, (unsigned long)slot_off(i));
            return 0;
        }
    }
    if (g_stub_mark + sizeof(WHITELIST_MARKER) > cave_end ||
        g_ptr_off + MAX_SLOTS * sizeof(uint64_t) > cave_end ||
        mem_r(pid, tx->func, tx->func_before, sizeof(tx->func_before)) != 0 ||
        mem_r(pid, tx->cave, tx->cave_before, sizeof(tx->cave_before)) != 0) {
        fprintf(stderr, "[a2h_patch] whitelist outer transaction snapshot FAIL\n");
        return 0;
    }
    tx->valid = 1;
    fprintf(stderr,
            "[a2h_patch] whitelist outer transaction snapshot OK func_bytes=%lu cave_bytes=%lu\n",
            (unsigned long)sizeof(tx->func_before),
            (unsigned long)sizeof(tx->cave_before));
    return 1;
}

static int whitelist_transaction_restore(pid_t pid, uintptr_t base,
                                         const whitelist_transaction_t *tx) {
    if (!tx || !tx->valid) return 0;
    unsigned char cave_verify[WHITELIST_CAVE_BYTES];
    unsigned char func_verify[WHITELIST_STUB_BYTES];
    int func_ok = mem_w(pid, tx->func, tx->func_before,
                        sizeof(tx->func_before)) == 0 &&
                  mem_r(pid, tx->func, func_verify, sizeof(func_verify)) == 0 &&
                  memcmp(func_verify, tx->func_before, sizeof(func_verify)) == 0;
    int cache_ok = 0;
    if (func_ok) {
        cache_ok = (remote_icache_flush(pid, base, tx->func,
                                        sizeof(tx->func_before)) &
                     ICACHE_REMOTE_IVAU) != 0;
    }
    int cave_ok = mem_w(pid, tx->cave, tx->cave_before,
                        sizeof(tx->cave_before)) == 0 &&
                  mem_r(pid, tx->cave, cave_verify, sizeof(cave_verify)) == 0 &&
                  memcmp(cave_verify, tx->cave_before, sizeof(cave_verify)) == 0;
    fprintf(stderr,
            "[a2h_patch] whitelist outer transaction rollback cave=%s func=%s cache=%s\n",
            cave_ok ? "OK" : "FAIL", func_ok ? "OK" : "FAIL",
            cache_ok ? "OK" : "FAIL");
    return cave_ok && func_ok && cache_ok;
}

static int restore_global_snapshot(pid_t pid, uintptr_t base, uintptr_t func,
                                   const unsigned char before[sizeof(GLOBAL_PATCH)]) {
    unsigned char verify[sizeof(GLOBAL_PATCH)];
    int write_ok = mem_w(pid, func, before, sizeof(GLOBAL_PATCH)) == 0 &&
                   mem_r(pid, func, verify, sizeof(verify)) == 0 &&
                   memcmp(verify, before, sizeof(verify)) == 0;
    int cache_ok = write_ok &&
                   ((remote_icache_flush(pid, base, func,
                                         sizeof(GLOBAL_PATCH)) &
                     ICACHE_REMOTE_IVAU) != 0);
    fprintf(stderr,
            "[a2h_patch] global transaction rollback bytes=%s cache=%s\n",
            write_ok ? "OK" : "FAIL", cache_ok ? "OK" : "FAIL");
    return write_ok && cache_ok;
}

static int encode_adrp_add(uint32_t *adrp, uint32_t *add, int rd, uintptr_t pc, uintptr_t target) {
    int64_t page_delta = ((int64_t)(target & ~0xFFFull) - (int64_t)(pc & ~0xFFFull)) >> 12;
    if (page_delta < -(1LL<<20) || page_delta > ((1LL<<20)-1)) return -1;
    uint32_t immlo=(uint32_t)(page_delta & 0x3);
    uint32_t immhi=(uint32_t)((page_delta >> 2) & 0x7FFFF);
    *adrp = 0x90000000u | (immlo << 29) | (immhi << 5) | (uint32_t)rd;
    uint32_t imm12=(uint32_t)(target & 0xFFF);
    *add = 0x91000000u | (imm12 << 10) | ((uint32_t)rd << 5) | (uint32_t)rd;
    return 0;
}
static int write_whitelist_stub_code(pid_t pid, uintptr_t base) {
    uintptr_t func = base + k_func_off();
    uintptr_t table = base + g_ptr_off;
    uintptr_t marker_addr = base + g_stub_mark;
    uint64_t ptrs[MAX_SLOTS];
    for (int i=0;i<MAX_SLOTS;++i) {
        char first=0;
        if (mem_r(pid, base + slot_off(i), &first, 1) != 0) {
            fprintf(stderr, "[a2h_patch] stub prep read slot%d FAIL\n", i); return 0;
        }
        ptrs[i] = first ? (uint64_t)(base + slot_off(i)) : 0;
        fprintf(stderr, "[a2h_patch] ptr[%d]=%s\n", i, ptrs[i]?"set":"null");
    }
    uint32_t code[24]; int n=0; uint32_t adrp=0, add=0;
    if (encode_adrp_add(&adrp,&add,1,func,table)!=0) {
        fprintf(stderr,"[a2h_patch] ADRP range FAIL func=0x%lx table=0x%lx\n",(unsigned long)func,(unsigned long)table);
        return 0;
    }
    code[n++]=adrp; code[n++]=add;
    code[n++]=0xB4000000; // cbz x0,false; exact offset patched below
    code[n++]=0x52800142; // mov w2,#10
    code[n++]=0x34000002; // cbz w2,false; exact offset patched below
    code[n++]=0xF8408423; // ldr x3,[x1],#8
    code[n++]=0xB4000003; // cbz x3,next_slot
    code[n++]=0xAA0003E4; // mov x4,x0
    code[n++]=0x38401485; // ldrb w5,[x4],#1
    code[n++]=0x38401466; // ldrb w6,[x3],#1
    code[n++]=0x6B0600BF; // cmp w5,w6
    code[n++]=0x54000001; // b.ne next_slot
    code[n++]=0x35000005; // cbnz w5,compare_loop
    code[n++]=0x52800020; code[n++]=0xD65F03C0; // true; ret
    code[n++]=0x51000442; // sub w2,w2,#1
    code[n++]=0x14000000; // b loop_count
    code[n++]=0x52800000; code[n++]=0xD65F03C0; // false; ret
    code[2]=0xB4000000|(15u<<5); code[4]=0x34000002|(13u<<5); code[6]=0xB4000003|(9u<<5);
    code[11]=0x54000001|(4u<<5); code[12]=0x35000005|((uint32_t)(-4 & 0x7FFFF)<<5);
    code[16]=0x14000000|((uint32_t)(-12)&0x3FFFFFF);
    const size_t code_bytes = (size_t)n * sizeof(code[0]);
    unsigned char old_code[sizeof(code)];
    unsigned char old_table[sizeof(ptrs)];
    unsigned char old_marker[16];
    if (mem_r(pid, func, old_code, code_bytes) != 0 ||
        mem_r(pid, table, old_table, sizeof(old_table)) != 0 ||
        mem_r(pid, marker_addr, old_marker, sizeof(old_marker)) != 0) {
        fprintf(stderr, "[a2h_patch] stub transaction snapshot FAIL\n");
        return 0;
    }

    int table_started = 0, marker_started = 0, code_started = 0;
    unsigned char table_verify[sizeof(ptrs)];
    unsigned char marker_verify[16];
    unsigned char code_verify[sizeof(code)];
    if (mem_w(pid, table, ptrs, sizeof(ptrs)) != 0) {
        table_started = 1;
        fprintf(stderr, "[a2h_patch] ptr table write FAIL @0x%lx\n", (unsigned long)g_ptr_off);
        goto rollback;
    }
    table_started = 1;
    if (mem_r(pid, table, table_verify, sizeof(table_verify)) != 0 ||
        memcmp(table_verify, ptrs, sizeof(ptrs)) != 0) {
        fprintf(stderr, "[a2h_patch] ptr table verify FAIL @0x%lx\n", (unsigned long)g_ptr_off);
        goto rollback;
    }
    fprintf(stderr, "[a2h_patch] ptr table write OK first=0x%llx\n",
            (unsigned long long)ptrs[0]);

    marker_started = 1;
    if (mem_w(pid, marker_addr, WHITELIST_MARKER, sizeof(WHITELIST_MARKER)) != 0 ||
        mem_r(pid, marker_addr, marker_verify, sizeof(marker_verify)) != 0 ||
        memcmp(marker_verify, WHITELIST_MARKER, sizeof(marker_verify)) != 0) {
        fprintf(stderr, "[a2h_patch] cave marker write FAIL\n");
        goto rollback;
    }

    code_started = 1;
    if (mem_w(pid, func, code, code_bytes) != 0 ||
        mem_r(pid, func, code_verify, code_bytes) != 0 ||
        memcmp(code_verify, code, code_bytes) != 0) {
        fprintf(stderr, "[a2h_patch] stub code write/verify FAIL\n");
        goto rollback;
    }
    return 1;

rollback:
    /* Restore the executable bytes first so an old stub never observes a
     * partially restored table or marker. */
    ;
    int restore_code = 1, restore_table = 1, restore_marker = 1;
    if (code_started) {
        restore_code = mem_w(pid, func, old_code, code_bytes) == 0 &&
                       mem_r(pid, func, code_verify, code_bytes) == 0 &&
                       memcmp(code_verify, old_code, code_bytes) == 0;
        remote_icache_flush(pid, base, func, code_bytes);
        syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL, 0);
    }
    if (table_started) {
        restore_table = mem_w(pid, table, old_table, sizeof(old_table)) == 0 &&
                        mem_r(pid, table, table_verify, sizeof(table_verify)) == 0 &&
                        memcmp(table_verify, old_table, sizeof(old_table)) == 0;
    }
    if (marker_started) {
        restore_marker = mem_w(pid, marker_addr, old_marker, sizeof(old_marker)) == 0 &&
                         mem_r(pid, marker_addr, marker_verify, sizeof(marker_verify)) == 0 &&
                         memcmp(marker_verify, old_marker, sizeof(old_marker)) == 0;
    }
    fprintf(stderr, "[a2h_patch] stub transaction rollback code=%s table=%s marker=%s\n",
            restore_code ? "OK" : "FAIL", restore_table ? "OK" : "FAIL",
            restore_marker ? "OK" : "FAIL");
    return 0;
}
static int install_whitelist_stub(pid_t pid, uintptr_t base) {
    uintptr_t func = base + k_func_off();
    if (!write_whitelist_stub_code(pid, base)) return 0;
    int flush_first = remote_icache_flush(pid, base, func,
                                          WHITELIST_STUB_BYTES);
    if ((flush_first & ICACHE_REMOTE_IVAU) == 0) {
        fprintf(stderr,
                "[a2h_patch] ERROR: whitelist first I-cache synchronization unverified\n");
        return 0;
    }
    if (!write_whitelist_stub_code(pid, base)) return 0;
    int flush_second = remote_icache_flush(pid, base, func,
                                           WHITELIST_STUB_BYTES);
    syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL, 0);
    syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL_EXPEDITED, 0);
    unsigned char head[16]={0};
    if (mem_r(pid, func, head, 16) != 0) return 0;
    int stubbed = exact_whitelist_stub_at(pid, func);
    int cache_ok = (flush_second & ICACHE_REMOTE_IVAU) != 0;
    fprintf(stderr, "[a2h_patch] stub head=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x (%s)\n",
            head[0],head[1],head[2],head[3],head[4],head[5],head[6],head[7],head[8],head[9],head[10],head[11],head[12],head[13],head[14],head[15],
            stubbed?"stub-exact":"BAD");
    if (!cache_ok) fprintf(stderr, "[a2h_patch] ERROR: whitelist I-cache synchronization unverified\n");
    return stubbed && cache_ok;
}

static int show_strings(pid_t pid, uintptr_t base) {
    int rc=0;
    for(int i=0;i<MAX_SLOTS;i++) {
        if (!slot_off(i)) {
            fprintf(stderr,"  [%d] off=unavailable\n", i);
            continue;
        }
        char buf[64]; memset(buf,0,sizeof(buf));
        size_t n=(size_t)slots[i].max_len; if(n>=sizeof(buf)) n=sizeof(buf)-1;
        if(mem_r(pid,base+slot_off(i),buf,n)==0) {
            int printable=1;
            for(size_t k=0;k<n && buf[k];++k){
                unsigned char c=(unsigned char)buf[k];
                if(!(isalnum(c)||c=='.'||c=='_')) { printable=0; break; }
            }
            fprintf(stderr,"  [%d] off=0x%lx %s\n", i, (unsigned long)slot_off(i),
                    (printable && buf[0])?buf:"(disabled)");
        } else { fprintf(stderr,"  [%d] off=0x%lx ERR\n", i, (unsigned long)slot_off(i)); rc=1; }
    }
    unsigned char head[16]={0};
    if (mem_r(pid, base + k_func_off(), head, 16) == 0) {
        int stubbed = exact_whitelist_stub_at(pid, base + k_func_off());
        int global = (head[0]==0x20 && head[1]==0x00 && head[2]==0x80 && head[3]==0x52);
        fprintf(stderr, "  mode: %s\n", global?"global":(stubbed?"whitelist":"stock"));
        fprintf(stderr, "  func_head: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
                head[0],head[1],head[2],head[3],head[4],head[5],head[6],head[7],head[8],head[9],head[10],head[11],head[12],head[13],head[14],head[15]);
    } else { fprintf(stderr, "  mode: unreadable\n"); rc=1; }
    fprintf(stderr, "  method=%s profile=%s hint=%s lib=%s base=0x%lx rw=0x%lx-0x%lx ptr=0x%lx\n",
            g_locate_method[0]?g_locate_method:"?", g_profile[0]?g_profile:"?", g_profile_hint[0]?g_profile_hint:"?",
            g_libname[0]?g_libname:"?", (unsigned long)base,
            (unsigned long)g_rw_start, (unsigned long)g_rw_end, (unsigned long)g_ptr_off);
    return rc;
}
static long now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static void save_func_off_hint(uintptr_t off) {
    FILE *f=fopen("/data/adb/modules/a2h_hook/config/func_off", "w");
    if (f) { fprintf(f, "%lx\n", (unsigned long)off); fclose(f); }
    f=fopen("/data/local/tmp/a2h_func_off", "w");
    if (f) { fprintf(f, "%lx\n", (unsigned long)off); fclose(f); }
}
static int load_cave_hint(uintptr_t *out) {
    const char *paths[] = {
        "/data/adb/modules/a2h_hook/config/cave_off",
        "/data/local/tmp/a2h_cave_off",
        NULL
    };
    for (int i = 0; paths[i]; ++i) {
        FILE *f = fopen(paths[i], "r");
        if (!f) continue;
        unsigned long v = 0;
        if (fscanf(f, "%lx", &v) == 1 && v >= 0x10000ul && v <= 0x2000000ul) {
            fclose(f);
            if (out) *out = (uintptr_t)v;
            fprintf(stderr, "[a2h_patch] cave hint from %s: 0x%lx\n", paths[i], v);
            return 1;
        }
        fclose(f);
    }
    return 0;
}
static void save_cave_hint(uintptr_t off) {
    FILE *f = fopen("/data/adb/modules/a2h_hook/config/cave_off", "w");
    if (f) { fprintf(f, "%lx\n", (unsigned long)off); fclose(f); }
    f = fopen("/data/local/tmp/a2h_cave_off", "w");
    if (f) { fprintf(f, "%lx\n", (unsigned long)off); fclose(f); }
}
int main(int argc,char **argv) {
    int mode=0,pid=-1; char *pkgfile=NULL; uintptr_t base_override=0; int check_want_global=-1;
    long t0=now_ms();
    apply_profile(&PROFILES[0]);
    fprintf(stderr, "[a2h_patch] version=%s\n", A2H_VERSION);
    for(int i=1;i<argc;i++) {
        if(strcmp(argv[i],"--help")==0||strcmp(argv[i],"-h")==0){
            printf("a2h_patch %s\n", A2H_VERSION);
            printf("Usage: %s [PID]\n",argv[0]);
            printf("       %s --disable [PID] [FILE]\n",argv[0]);
            printf("       %s --show [PID]\n",argv[0]);
            printf("       %s --status [PID]\n",argv[0]);
            printf("       %s --check global|whitelist [PID]\n",argv[0]);
            printf("       %s --packages FILE\n",argv[0]);
            printf("       %s --base 0x...\n",argv[0]);
            return 0;
        } else if(strcmp(argv[i],"--disable")==0){
            mode=1;
            if (i + 1 < argc && argv[i + 1][0] != '-') pid = atoi(argv[++i]);
            if (i + 1 < argc && argv[i + 1][0] != '-') pkgfile = argv[++i];
        }
        else if(strcmp(argv[i],"--show")==0) mode=2;
        else if(strcmp(argv[i],"--status")==0) mode=3;
        else if(strcmp(argv[i],"--check")==0 && i+1<argc){
            mode=4;
            check_want_global = (strcmp(argv[++i], "global")==0) ? 1 : 0;
        }
        else if(strcmp(argv[i],"--packages")==0 && i+1<argc) pkgfile=argv[++i];
        else if(strcmp(argv[i],"--base")==0 && i+1<argc) base_override=(uintptr_t)strtoull(argv[++i],NULL,0);
        else if(argv[i][0] != '-') pid=atoi(argv[i]);
    }
    if(!pkgfile) pkgfile="/data/adb/modules/a2h_hook/config/packages.txt";
    // Identity props are relatively expensive; only print on real apply.
    if(mode==0 || mode==1) log_system_identity();
    if(pid<=0){ for(int i=0;i<30;i++){ pid=find_pid(); if(pid>0)break; sleep(1);} }
    if(pid<=0){fprintf(stderr,"[a2h_patch] ERROR: service not found\n");return 2;}
    fprintf(stderr,"[a2h_patch] pid=%d mode=%s\n", pid, mode==1?"whitelist":(mode==2?"show":(mode==3?"status":(mode==4?"check":"global"))));
    char n1[48], n2[48]; name_hal_primary(n1); name_hal_mt(n2);
    uintptr_t base=base_override, rw_s=0, rw_e=0, rx_s=0, rx_e=0;
    for(int i=0;i<10&&!base;i++) {
        base=find_lib_base_and_maps(pid,n1,&rw_s,&rw_e,&rx_s,&rx_e);
        if(base){ snprintf(g_libname,sizeof(g_libname),"%s",n1); break; }
        base=find_lib_base_and_maps(pid,n2,&rw_s,&rw_e,&rx_s,&rx_e);
        if(base){ snprintf(g_libname,sizeof(g_libname),"%s",n2); break; }
        base=find_audio_primary_base_and_maps(pid,&rw_s,&rw_e,&rx_s,&rx_e);
        if(base) break;
        sleep(1); pid=find_pid();
    }
    if(base_override) {
        base=find_audio_primary_by_exact_base(pid,base_override,&rw_s,&rw_e,&rx_s,&rx_e);
        if(!base) {
            fprintf(stderr,"[a2h_patch] ERROR: --base 0x%lx does not identify an offset-0 audio.primary HAL map\n",
                    (unsigned long)base_override);
        }
    }
    if(!base){fprintf(stderr,"[a2h_patch] ERROR: target map missing\n"); if(g_attached)trace_detach(pid); return 2;}
    (void)capture_hal_map_identity(pid, base);
    g_rx_start = rx_s; g_rx_end = rx_e;
    if (base && rw_s >= base) { g_rw_start = rw_s - base; g_rw_end = rw_e ? (rw_e - base) : 0; }
    fprintf(stderr,"[a2h_patch] lib=%s base=0x%lx rw_abs=0x%lx-0x%lx rx_abs=0x%lx-0x%lx\n",
            g_libname[0]?g_libname:"?", (unsigned long)base, (unsigned long)rw_s, (unsigned long)rw_e,
            (unsigned long)rx_s, (unsigned long)rx_e);
    g_attached = (trace_attach(pid) == 0);
    fprintf(stderr,"[a2h_patch] ptrace=%s\n", g_attached?"ok":"unavailable");
    if (!g_attached && (mode == 0 || mode == 1)) {
        fprintf(stderr,
                "[a2h_patch] ERROR: writable modes require a fully frozen thread group\n");
        return 2;
    }
    if (!discover_elf_tail_region(pid, base, WHITELIST_CAVE_BYTES)) {
        fprintf(stderr,
                "[a2h_patch] WARN: whitelist ELF tail unavailable; global mode remains eligible\n");
    }
#ifdef A2H_DIAGNOSTIC_SCAN_ONLY
    uintptr_t diagnostic_off = 0;
    size_t diagnostic_capacity = 0;
    int diagnostic_ok = scan_func_by_sig(pid, base, &diagnostic_off,
                                         &diagnostic_capacity);
    fprintf(stderr,
            "[a2h_patch] diagnostic universal scan=%s func=0x%lx capacity=%lu kind=%s\n",
            diagnostic_ok ? "OK" : "FAIL", (unsigned long)diagnostic_off,
            (unsigned long)diagnostic_capacity,
            g_scan_kind[0] ? g_scan_kind : "none");
    if (g_attached) trace_detach(pid);
    return diagnostic_ok ? 0 : 2;
#endif
    if (!locate_targets(pid, base)) {
        fprintf(stderr, "[a2h_patch] ERROR: target location unresolved; no unsafe hint fallback\n");
        if(g_attached)trace_detach(pid);
        return 2;
    }
    size_t required_capacity = mode == 1 ? WHITELIST_STUB_BYTES :
                               (mode == 0 ? sizeof(GLOBAL_PATCH) : 0);
    if (required_capacity && g_func_capacity < required_capacity) {
        fprintf(stderr,
                "[a2h_patch] ERROR: function capacity insufficient mode=%s proven=%lu required=%lu; no write performed\n",
                mode == 1 ? "whitelist" : "global",
                (unsigned long)g_func_capacity,
                (unsigned long)required_capacity);
        if(g_attached)trace_detach(pid);
        return 2;
    }
    if (mode == 1 && !setup_custom_cave(pid, base, rw_s, rw_e)) {
        fprintf(stderr, "[a2h_patch] ERROR: whitelist cave setup failed; no memory written\n");
        if (g_attached) trace_detach(pid);
        return 2;
    }
    uintptr_t func_addr = base + k_func_off();
    unsigned char vfy8[16]={0};
    if (mem_r(pid,func_addr,vfy8,16)!=0) {
        fprintf(stderr,"[a2h_patch] ERROR: cannot read is_A2H_app @0x%lx\n", (unsigned long)k_func_off());
        if(g_attached)trace_detach(pid); return 2;
    }
    fprintf(stderr,"[a2h_patch] func@0x%lx head=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
            (unsigned long)k_func_off(), vfy8[0],vfy8[1],vfy8[2],vfy8[3],vfy8[4],vfy8[5],vfy8[6],vfy8[7],vfy8[8],vfy8[9],vfy8[10],vfy8[11],vfy8[12],vfy8[13],vfy8[14],vfy8[15]);
    int sig_ok = (memcmp(vfy8,SIG8,8)==0);
    int already_global = (memcmp(vfy8,PATCH,8)==0);
    int already_stub = exact_whitelist_stub_at(pid, func_addr);
    fprintf(stderr,"[a2h_patch] sig=%s state=%s method=%s profile=%s hint=%s\n",
            sig_ok?"stock-match":(already_global?"global":(already_stub?"whitelist":"unknown")),
            already_global?"global":(already_stub?"whitelist":(sig_ok?"stock":"unknown")),
            g_locate_method[0]?g_locate_method:"?", g_profile, g_profile_hint);
    if(mode==2){ int rc=show_strings(pid,base); if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0); return rc?1:0; }
    if(mode==3){ if(g_attached)trace_detach(pid); fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0); return already_global?0:1; }
    if(mode==4){
        const char *cur = already_global?"global":(already_stub?"whitelist":(sig_ok?"stock":"unknown"));
        int ok = already_global;
        int config_active = -1;
        stub_info_t si;
        memset(&si, 0, sizeof(si));
        if(check_want_global != 1) {
            pkg_stats_t stats;
            char **pkgs = read_pkgs(pkgfile, &stats);
            config_active = pkgs ? stats.active : -1;
            int table_ok = already_stub && pkgs &&
                           inspect_stub_table(pid, base, k_func_off(), vfy8, pkgs, 1, &si);
            ok = table_ok && si.content_mismatch == 0;
            free_pkgs(pkgs);
        }
        fprintf(stderr,"[a2h_patch] live: want=%s cur=%s head=%02x %02x %02x %02x method=%s func=0x%lx active_ptrs=%d config_active=%d mismatch=%d\n",
                check_want_global==1?"global":"whitelist", cur, vfy8[0],vfy8[1],vfy8[2],vfy8[3],
                g_locate_method[0]?g_locate_method:"?", (unsigned long)k_func_off(),
                check_want_global==1?-1:si.active_ptrs, config_active,
                check_want_global==1?0:si.content_mismatch);
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
        return ok?0:1;
    }
    if(mode==1){
        fprintf(stderr,"applying whitelist...\n");
        pkg_stats_t stats;
        char **pkgs=read_pkgs(pkgfile,&stats);
        if(!pkgs){ fprintf(stderr,"[a2h_patch] ERROR: config\n"); if(g_attached)trace_detach(pid); return 1; }
        for(int i=0;i<MAX_SLOTS;i++) fprintf(stderr,"[a2h_patch] cfg[%d]=%s\n", i, (pkgs[i]&&pkgs[i][0])?pkgs[i]:"(empty)");
        int preflight = remote_icache_flush(pid, base, func_addr,
                                            WHITELIST_STUB_BYTES);
        if ((preflight & ICACHE_REMOTE_IVAU) == 0) {
            fprintf(stderr,
                    "[a2h_patch] ERROR: whitelist cache preflight failed; no patch data written\n");
            free_pkgs(pkgs);
            if(g_attached)trace_detach(pid);
            fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
            return 1;
        }
        whitelist_transaction_t tx;
        if (!whitelist_transaction_begin(pid, base, &tx)) {
            free_pkgs(pkgs);
            if(g_attached)trace_detach(pid);
            fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
            return 1;
        }
        int rc=apply_strings(pid,base,pkgs);
        int stub_ok=0;
        if(rc) stub_ok=install_whitelist_stub(pid,base);
        else fprintf(stderr,"[a2h_patch] skip stub because string apply failed\n");
        unsigned char vf[16]={0}; mem_r(pid,func_addr,vf,16);
        int stubbed=exact_whitelist_stub_at(pid,func_addr);
        stub_info_t si;
        memset(&si, 0, sizeof(si));
        int table_ok = stubbed && inspect_stub_table(pid, base, k_func_off(), vf, pkgs, 1, &si);
        int final_ok = rc && stub_ok && stubbed && table_ok && si.content_mismatch == 0;
        int rollback_ok = 1;
        if (!final_ok) rollback_ok = whitelist_transaction_restore(pid, base, &tx);
        if (final_ok) {
            save_func_off_hint(k_func_off());
            save_cave_hint(slot_off(0));
        }
        free_pkgs(pkgs);
        fprintf(stderr,"whitelist: %s\n", final_ok?"OK":"FAIL");
        fprintf(stderr,"[a2h_patch] summary method=%s profile=%s hint=%s write=%s stub=%s table=%s final=%s rollback=%s active=%d active_ptrs=%d lines=%d rejected=%d fallback=%d all_off=%d mismatch=%d icache=%s\n",
                g_locate_method,g_profile,g_profile_hint, rc?"OK":"FAIL", stub_ok?"OK":"FAIL",
                table_ok?"OK":"FAIL", stubbed?"whitelist":"not-whitelist",
                final_ok?"not-needed":(rollback_ok?"OK":"FAIL"),
                stats.active, si.active_ptrs, stats.lines_read, stats.rejected,
                stats.fallback_defaults, stats.explicit_all_off, si.content_mismatch,
                icache_status());
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
        return final_ok?0:1;
    }
    int global_marked = memcmp(vfy8, GLOBAL_PATCH, sizeof(GLOBAL_PATCH)) == 0;
    if (already_global && global_marked){
        save_func_off_hint(k_func_off());
        fprintf(stderr,"already enabled\n");
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
        return 0;
    }
    fprintf(stderr, already_global ? "upgrading global marker...\n" : "enabling global...\n");
    int preflight = remote_icache_flush(pid, base, func_addr,
                                        sizeof(GLOBAL_PATCH));
    if ((preflight & ICACHE_REMOTE_IVAU) == 0) {
        fprintf(stderr,
                "[a2h_patch] ERROR: global cache preflight failed; no function bytes written\n");
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
        return 1;
    }
    unsigned char global_before[sizeof(GLOBAL_PATCH)];
    if (mem_r(pid, func_addr, global_before, sizeof(global_before)) != 0) {
        fprintf(stderr,"[a2h_patch] ERROR: global transaction snapshot failed\n");
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
        return 1;
    }
    unsigned char first_verify[sizeof(GLOBAL_PATCH)]={0};
    int first_write_ok = mem_w(pid,func_addr,GLOBAL_PATCH,sizeof(GLOBAL_PATCH)) == 0 &&
                         mem_r(pid,func_addr,first_verify,sizeof(first_verify)) == 0 &&
                         memcmp(first_verify,GLOBAL_PATCH,sizeof(GLOBAL_PATCH)) == 0;
    int flush_first = first_write_ok ?
                      remote_icache_flush(pid, base, func_addr,
                                          sizeof(GLOBAL_PATCH)) : 0;
    int first_cache_ok = (flush_first & ICACHE_REMOTE_IVAU) != 0;
    unsigned char second_verify[sizeof(GLOBAL_PATCH)]={0};
    int second_write_ok = first_write_ok && first_cache_ok &&
                          mem_w(pid,func_addr,GLOBAL_PATCH,sizeof(GLOBAL_PATCH)) == 0 &&
                          mem_r(pid,func_addr,second_verify,sizeof(second_verify)) == 0 &&
                          memcmp(second_verify,GLOBAL_PATCH,sizeof(GLOBAL_PATCH)) == 0;
    int flush_second = second_write_ok ?
                       remote_icache_flush(pid, base, func_addr,
                                           sizeof(GLOBAL_PATCH)) : 0;
    int second_cache_ok = (flush_second & ICACHE_REMOTE_IVAU) != 0;
    syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL, 0);
    syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL_EXPEDITED, 0);
    unsigned char vf[sizeof(GLOBAL_PATCH)]={0};
    int verify_ok = mem_r(pid,func_addr,vf,sizeof(vf)) == 0 &&
                    memcmp(vf,GLOBAL_PATCH,sizeof(GLOBAL_PATCH)) == 0;
    int cache_ok = first_cache_ok && second_cache_ok;
    int ok = first_write_ok && second_write_ok && verify_ok && cache_ok;
    int rollback_ok = 1;
    if (!ok) rollback_ok = restore_global_snapshot(pid, base, func_addr,
                                                    global_before);
    if (ok) save_func_off_hint(k_func_off());
    if (!first_write_ok) fprintf(stderr, "[a2h_patch] ERROR: global first write failed\n");
    if (first_write_ok && !first_cache_ok) fprintf(stderr, "[a2h_patch] ERROR: global first I-cache synchronization unverified\n");
    if (first_write_ok && first_cache_ok && !second_write_ok)
        fprintf(stderr, "[a2h_patch] ERROR: global second write/verify failed\n");
    if (second_write_ok && !second_cache_ok) fprintf(stderr, "[a2h_patch] ERROR: global second I-cache synchronization unverified\n");
    if (!verify_ok) fprintf(stderr, "[a2h_patch] ERROR: global final byte verification failed\n");
    fprintf(stderr,"enable: %s\n",ok?"OK":"FAIL");
    fprintf(stderr,"[a2h_patch] global verify head=%02x %02x %02x %02x method=%s profile=%s hint=%s icache=%s rollback=%s\n",
            vf[0],vf[1],vf[2],vf[3], g_locate_method, g_profile, g_profile_hint,
            icache_status(), ok?"not-needed":(rollback_ok?"OK":"FAIL"));
    if(g_attached)trace_detach(pid);
    fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
    return ok?0:1;
}
