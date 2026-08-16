// a2h_patch v1.5.8 - universal signature/ELF scan + active audio lifecycle
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
#include <sys/resource.h>
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
#define A2H_VERSION "1.5.8"
#define WHITELIST_CAVE_BYTES (MAX_SLOTS * 64 + 16 + MAX_SLOTS * 8 + 32)
#define WHITELIST_STUB_WORDS 19
#define WHITELIST_STUB_BYTES (WHITELIST_STUB_WORDS * sizeof(uint32_t))
#define A2H_APP_FUNC_BYTES 160u
#define UPDATE_A2H_MODE_FUNC_BYTES 568u
#define IS_A2H_ALLOWED_FUNC_BYTES 700u
#define STREAM_SET_PARAMETERS_FUNC_BYTES 12040u
#define UPDATE_OUTPUT_POOL_FUNC_BYTES 1128u
#define UPDATE_FLAGS_PATCH_OFF 0xDCu
#define UPDATE_IDLE_GUARD_OFF 0xECu
#define UPDATE_IDLE_HELPER_OFF 0xF0u
#define UPDATE_IDLE_HELPER_BRANCH_OFF 0xF4u
#define UPDATE_APP_POLICY_PATCH_OFF 0x130u
#define UPDATE_APP_POLICY_BYTES 72u
#define UPDATE_APP_POLICY_DISK_BL_OFF 0x24u
#define UPDATE_APP_POLICY_STOCK_BL_OFF 0x34u
#define UPDATE_APP_POLICY_RELAXED_BL_OFF 0x28u
#define UPDATE_APP_POLICY_LEGACY_BL_OFF 0x1Cu
#define UPDATE_A2H_ALLOWED_BL_OFF 0x1D4u
#define UPDATE_CONCURRENT_HELPER_BYTES 208u
#define UPDATE_CONCURRENT_PREVIOUS_BYTES 176u
#define UPDATE_CONCURRENT_PREVIOUS2_BYTES 160u
#define UPDATE_CONCURRENT_LEGACY_BYTES 64u
#define CONCURRENT_PREVIOUS_SHIFT \
    (UPDATE_CONCURRENT_HELPER_BYTES - UPDATE_CONCURRENT_PREVIOUS_BYTES)
#define CONCURRENT_PREVIOUS2_SHIFT \
    (UPDATE_CONCURRENT_HELPER_BYTES - UPDATE_CONCURRENT_PREVIOUS2_BYTES)
#define CONCURRENT_LEGACY_SHIFT \
    (UPDATE_CONCURRENT_HELPER_BYTES - UPDATE_CONCURRENT_LEGACY_BYTES)
#define CONCURRENT_OPEN_HELPER_OFF 0u
#define CONCURRENT_OPEN_UPDATE_BL_OFF 8u
#define CONCURRENT_OPEN_RETURN_B_OFF 16u
#define CONCURRENT_CORE_OFF 0x20u
#define CONCURRENT_MAIN_OFF 0x30u
#define CONCURRENT_PENDING_DECAY_B_OFF 0x88u
#define CONCURRENT_ENTRY_B_OFF 0x90u
#define CONCURRENT_POLICY_RETURN_B_OFF 0xACu
#define CONCURRENT_IDLE_HELPER_OFF 0xBCu
#define CONCURRENT_IDLE_RETURN_B_OFF 0xC4u
#define CONCURRENT_ZERO_INIT_OFF 0xC8u
#define CONCURRENT_ZERO_INIT_RETURN_B_OFF 0xCCu
#define CONCURRENT_LEGACY_POLICY_RETURN_B_OFF 0x28u
#define CONCURRENT_LEGACY_IDLE_RETURN_B_OFF 0x34u
#define CONCURRENT_LEGACY_ZERO_INIT_RETURN_B_OFF 0x3Cu
#define GAME_POLICY_PATCH_OFF 0x1C8u
#define GAME_POLICY_PATCH_BYTES 40u
#define GAME_POLICY_ENTRY_BYTES 20u
#define GAME_POLICY_HELPER_BRANCH_OFF 0u
#define POLICY_IDLE_COUNT_LOAD_OFF 0x30u
#define POLICY_IDLE_COUNT_CBZ_OFF 0x3Cu
#define POLICY_MANAGER_INIT_OFF 0x40u
#define POLICY_SLOT_INDEX_INIT_OFF 0x50u
#define POLICY_SLOT_COUNT_RELOAD_OFF 0x64u
#define POLICY_SLOT_INDEX_STEP_OFF 0x6Cu
#define POLICY_SLOT_TABLE_LOAD_OFF 0x78u
#define POLICY_SLOT_STREAM_LOAD_OFF 0x7Cu
#define POLICY_DEVICE_BEGIN_LOAD_OFF 0x84u
#define POLICY_DEVICE_END_LOAD_OFF 0x8Cu
#define IDLE_CLEAR_BRANCH_PATCH_OFF 0x188u
#define IDLE_CLEAR_RESUME_OFF 0x18Cu
#define IDLE_CLEAR_BRANCH_BYTES 4u
#define STREAM_EVENT_PATCH_COUNT 3u
#define STREAM_EVENT_PATCH_MAX_BYTES 28u
#define STREAM_REGION_EVENT0 0u
#define STREAM_REGION_REF 1u
#define STREAM_REGION_EVENT1 2u
#define STREAM_REGION_EVENT2 3u
#define POLICY_REGION_IDLE_COUNT 0u
#define POLICY_REGION_IDLE_CLEAR 1u
#define POLICY_REGION_GAME 2u
#define POLICY_OVERLAY_REGION_COUNT 3u
#define STREAM_REF_PATCH_OFF 0x2030u
#define STREAM_REF_PATCH_BYTES 148u
#define STREAM_REF_DELETE_BL_OFF 0x10u
#define STREAM_REF_COUNT_CBNZ_OFF 0x18u
#define STREAM_REF_ALLOWED_BL_OFF 0x38u
#define STREAM_REF_FLAGS_LOAD_OFF 0x44u
#define STREAM_REF_STACK_COND_OFF 0x64u
#define STREAM_REF_STORE_OFF 0x90u
#define STREAM_REF_LEGACY_STACK_COND_OFF 0x60u
#define STREAM_REF_LEGACY_IDLE_GUARD_CBNZ_OFF 0x64u
#define STREAM_REF_LEGACY_IDLE_GUARD_CLEAR_OFF 0x68u
#define STREAM_REF_UPDATE_B_OFF 0x8Cu
#define STREAM_REF_HELPER_OFF 0x2088u
#define STREAM_REF_LEGACY_HELPER_OFF 0x2084u
#define STREAM_APP_MAP_MANAGER_OFF 0x1F58u
#define STREAM_NEW_NODE_MANAGER_OFF 0x2144u
#define STREAM_UPDATE_CALL_OFF 0x23F8u
#define STREAM_UPDATE_BL_OFF 0x23FCu
#define STREAM_UPDATE_CALL_BYTES 12u
#define STREAM_REF_LEGACY_EVENT_OFF 0x24u
#define OUTPUT_POOL_TAIL_PATCH_OFF 0x278u
#define OUTPUT_POOL_TAIL_PATCH_BYTES 4u
#define OUTPUT_POOL_MANAGER_ARG_OFF 0x38u
#define OUTPUT_POOL_STACK_COND_OFF 0x0Cu
#define OUTPUT_POOL_STACK_FAIL_OFF 0x464u
#define STREAM_OPEN_EPILOGUE_EXPECTED_OFF 0x9ECu
#define STREAM_OPEN_EPILOGUE_BYTES 36u
#define STREAM_OPEN_EVENT_BYTES 4u
#define STREAM_OPEN_THIS_MOV_OFF 0x30u
#define STREAM_OPEN_THIS_MOV 0xAA0003F3u
#define STREAM_OPEN_MANAGER_LOAD 0xF9400A60u
#define HANDOFF_FLAG_OFF 0x519u
#define CONCURRENT_APP_FLAG_OFF 0x51Au
#define STREAM_OVERLAY_REGION_COUNT (STREAM_EVENT_PATCH_COUNT + 1u)
#define AUXILIARY_PATCH_MAX_BYTES UPDATE_CONCURRENT_HELPER_BYTES

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
    {"os2_0_208_0x3e3b90","HyperOS2.0.208 static",0x3E3B90,160,{0xADD72,0xB1ACA,0xB4FED,0xBC53B,0xBC54A,0xFCDFD},{17,19,22,14,15,14}},
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
static const unsigned char UPDATE_FLAGS_STOCK[32]={
    0xC8,0x01,0x18,0x37,0x68,0x0E,0x41,0xF9,
    0x00,0x69,0x77,0xF8,0x08,0x00,0x40,0xF9,
    0x08,0x89,0x40,0xF9,0x00,0x01,0x3F,0xD6,
    0x08,0x30,0x40,0x39,0xE8,0x00,0x20,0x37
};
/* Early v1.5.6 only covered low-byte FAST/DEEP/OFFLOAD flags. */
static const unsigned char UPDATE_FLAGS_LEGACY[32]={
    0x1F,0x09,0x1E,0x72,0xA1,0x01,0x00,0x54,
    0x1F,0x20,0x03,0xD5,0x1F,0x20,0x03,0xD5,
    0x1F,0x20,0x03,0xD5,0x1F,0x20,0x03,0xD5,
    0x1F,0x20,0x03,0xD5,0x1F,0x20,0x03,0xD5
};
/* Eligible output objects are intentionally independent from the active
 * stream slots inspected by isA2HAllowed(). */
static const unsigned char UPDATE_FLAGS_GAME_HANDOFF_LEGACY[32]={
    0x08,0x0C,0x40,0xB9,0x1F,0x09,0x1E,0x72,
    0x81,0x01,0x00,0x54,0x68,0x01,0x90,0x37,
    0x1F,0x20,0x03,0xD5,0x1F,0x20,0x03,0xD5,
    0x1F,0x20,0x03,0xD5,0x1F,0x20,0x03,0xD5
};
static const unsigned char UPDATE_IDLE_HELPER_TEMPLATE[16]={
    0x04,0x00,0x00,0x14, /* non-eligible output: b update+0xfc */
    0xF3,0x03,0x1F,0x2A, /* helper: mov w19, wzr */
    0x00,0x00,0x00,0x14, /* b update concurrent idle helper */
    0x1F,0x20,0x03,0xD5  /* nop */
};
static const unsigned char UPDATE_IDLE_HELPER_GUARDED_LEGACY[16]={
    0x04,0x00,0x00,0x14,
    0xF3,0x03,0x1F,0x2A,
    0x1F,0x64,0x14,0x39,
    0x00,0x00,0x00,0x14
};
static const unsigned char IDLE_CLEAR_BRANCH_STOCK
        [IDLE_CLEAR_BRANCH_BYTES]={
    0xF3,0x03,0x1F,0x2A /* mov w19, wzr */
};
static const unsigned char IDLE_CLEAR_DISK_CONTEXT[12]={
    0xF3,0x03,0x1F,0x2A, /* mov w19, wzr */
    0x3A,0x00,0x80,0x52, /* mov w26, #1 */
    0x88,0x03,0x40,0x39  /* ldrb w8, [x28] */
};
static const uint32_t POLICY_IDLE_COUNT_LOAD=0xF9413C08u;
static const uint32_t POLICY_IDLE_COUNT_CBZ=0xB4000A68u;
static const uint32_t POLICY_MANAGER_INIT=0xAA0003F4u;
static const uint32_t POLICY_SLOT_INDEX_INIT=0x5280011Bu;
static const uint32_t POLICY_SLOT_COUNT_RELOAD=0xF9413E88u;
static const uint32_t POLICY_SLOT_INDEX_STEP=0x9100437Bu;
static const uint32_t POLICY_SLOT_TABLE_LOAD=0xF9413A88u;
static const uint32_t POLICY_SLOT_STREAM_LOAD=0xF87B6908u;
static const uint32_t POLICY_DEVICE_BEGIN_LOAD=0xF9412517u;
static const uint32_t POLICY_DEVICE_END_LOAD=0xF9412919u;
static const unsigned char POLICY_IDLE_COUNT_CBZ_STOCK[4]={
    0x68,0x0A,0x00,0xB4
};
/* The stock BL displacement differs by ROM.  Its four bytes are generated
 * from the mapped ELF after the rest of this block is matched exactly. */
static const unsigned char UPDATE_APP_POLICY_DISK_TEMPLATE[UPDATE_APP_POLICY_BYTES]={
    0x68,0x9E,0x42,0xF9,0x1F,0x05,0x00,0xF1,
    0x81,0x01,0x00,0x54,0x68,0x9A,0x42,0xF9,
    0x09,0x41,0x40,0x39,0x0A,0x11,0x40,0xF9,
    0x08,0x45,0x00,0x91,0x3F,0x01,0x00,0x72,
    0x00,0x01,0x8A,0x9A,0x00,0x00,0x00,0x00,
    0x17,0x00,0x00,0x52,0xC8,0x02,0x40,0x39,
    0xA8,0x00,0x08,0x37,0x14,0x00,0x00,0x14,
    0x37,0x00,0x80,0x52,0xC8,0x02,0x40,0x39,
    0x28,0x02,0x08,0x36,0xA3,0xE8,0xFF,0xD0
};
static const unsigned char UPDATE_APP_POLICY_STOCK_TEMPLATE[UPDATE_APP_POLICY_BYTES]={
    0x68,0x9E,0x42,0xF9,0x1F,0x05,0x00,0xF1,
    0xA0,0x00,0x00,0x54,0xA8,0x01,0x00,0xB5,
    0x68,0x66,0x54,0x39,0x17,0x01,0x00,0x52,
    0x1B,0x00,0x00,0x14,0x68,0x9A,0x42,0xF9,
    0x09,0x41,0x40,0x39,0x0A,0x45,0x00,0x91,
    0x08,0x11,0x40,0xF9,0x3F,0x01,0x00,0x72,
    0x40,0x01,0x88,0x9A,0x00,0x00,0x00,0x00,
    0x17,0x00,0x00,0x52,0x12,0x00,0x00,0x14,
    0x37,0x00,0x80,0x52,0x10,0x00,0x00,0x14
};
static const unsigned char UPDATE_APP_POLICY_RELAXED_TEMPLATE[UPDATE_APP_POLICY_BYTES]={
    /* x23 is callee-saved and keeps the current node across is_A2H_app. */
    0x77,0x9A,0x42,0xF9,0x97,0x00,0x00,0xB5,
    0x68,0x66,0x54,0x39,0x17,0x01,0x00,0x52,
    0x1D,0x00,0x00,0x14,0xE9,0x42,0x40,0x39,
    0xEA,0x46,0x00,0x91,0xE0,0x12,0x40,0xF9,
    0x3F,0x01,0x00,0x72,0x40,0x01,0x80,0x9A,
    0x00,0x00,0x00,0x00,0xA0,0x00,0x00,0x35,
    0xF7,0x02,0x40,0xF9,0x17,0xFF,0xFF,0xB5,
    0x37,0x00,0x80,0x52,0x12,0x00,0x00,0x14,
    0x17,0x00,0x80,0x52,0x10,0x00,0x00,0x14
};
static const unsigned char UPDATE_APP_POLICY_LEGACY_TEMPLATE[56]={
    0x77,0x9A,0x42,0xF9,0xB7,0x01,0x00,0xB4,
    0xE9,0x42,0x40,0x39,0xEA,0x46,0x00,0x91,
    0xE0,0x12,0x40,0xF9,0x3F,0x01,0x00,0x72,
    0x40,0x01,0x80,0x9A,0x00,0x00,0x00,0x00,
    0x80,0x00,0x00,0x35,0xF7,0x02,0x40,0xF9,
    0x17,0xFF,0xFF,0xB5,0x03,0x00,0x00,0x14,
    0xF7,0x03,0x1F,0x2A,0x02,0x00,0x00,0x14
};
static const unsigned char GAME_POLICY_STOCK[GAME_POLICY_PATCH_BYTES]={
    0x7F,0x06,0x00,0x71,0xE9,0x07,0x40,0xF9,
    0xE8,0x17,0x9F,0x1A,0x7F,0x02,0x00,0x71,
    0xEA,0x17,0x9F,0x1A,0x1F,0x01,0x1A,0x6A,
    0x29,0x15,0x40,0xF9,0x4A,0x79,0x1F,0x53,
    0xAB,0x83,0x5F,0xF8,0x40,0x05,0x9F,0x1A
};
static const unsigned char GAME_POLICY_CONCURRENT_TEMPLATE
        [GAME_POLICY_PATCH_BYTES]={
    0x00,0x00,0x00,0x14,0x7F,0x02,0x00,0x71,
    0xE8,0x07,0x9F,0x1A,0xEA,0x17,0x9F,0x1A,
    0xE9,0x07,0x40,0xF9,0x1F,0x01,0x1A,0x6A,
    0x29,0x15,0x40,0xF9,0x4A,0x79,0x1F,0x53,
    0xAB,0x83,0x5F,0xF8,0x40,0x05,0x9F,0x1A
};
static const unsigned char GAME_POLICY_RELAXED[GAME_POLICY_PATCH_BYTES]={
    0x9F,0x6A,0x14,0x39,0x7F,0x02,0x00,0x71,
    0xE8,0x07,0x9F,0x1A,0xEA,0x17,0x9F,0x1A,
    0xE9,0x07,0x40,0xF9,0x1F,0x01,0x1A,0x6A,
    0x29,0x15,0x40,0xF9,0x4A,0x79,0x1F,0x53,
    0xAB,0x83,0x5F,0xF8,0x40,0x05,0x9F,0x1A
};
static const unsigned char GAME_POLICY_PUBLIC_RELAXED
        [GAME_POLICY_PATCH_BYTES]={
    0x7F,0x06,0x00,0x71,0xE9,0x07,0x40,0xF9,
    0xE8,0x17,0x9F,0x1A,0x7F,0x02,0x00,0x71,
    0xEA,0x17,0x9F,0x1A,0x7F,0x02,0x00,0x71,
    0x29,0x15,0x40,0xF9,0x4A,0x79,0x1F,0x53,
    0xAB,0x83,0x5F,0xF8,0x40,0x01,0x9A,0x1A
};
static const unsigned char UPDATE_CONCURRENT_HELPER_V3_TEMPLATE
        [UPDATE_CONCURRENT_PREVIOUS_BYTES]={
    0x08,0x01,0x00,0x12,0x88,0x6A,0x14,0x39,
    0x21,0x00,0x00,0x14,0x1F,0x20,0x03,0xD5,
    0x88,0x6A,0x54,0x39,0xC8,0x03,0x00,0x34,
    0x95,0x3A,0x41,0xF9,0x96,0x3E,0x41,0xF9,
    0xF6,0x01,0x00,0xB4,0x17,0x01,0x80,0xD2,
    0x18,0x00,0x80,0xD2,0xA9,0x6A,0x77,0xF8,
    0x09,0x01,0x00,0xB4,0x2A,0x25,0x41,0xF9,
    0x2B,0x29,0x41,0xF9,0x7F,0x01,0x0A,0xEB,
    0x89,0x00,0x00,0x54,0x18,0x07,0x00,0x11,
    0x1F,0x0B,0x00,0x71,0xA2,0x01,0x00,0x54,
    0xD6,0x06,0x00,0xF1,0xF7,0x42,0x00,0x91,
    0xA1,0xFE,0xFF,0x54,0x89,0x9E,0x42,0xF9,
    0x3F,0x05,0x00,0xF1,0x88,0x00,0x00,0x54,
    0x00,0x00,0x00,0x14,0x08,0x00,0x00,0x14,
    0x00,0x00,0x00,0x14,0x28,0x00,0x80,0x52,
    0x88,0x6A,0x14,0x39,0x04,0x00,0x00,0x14,
    0x48,0x00,0x80,0x52,0x88,0x6A,0x14,0x39,
    0xFA,0x03,0x1F,0x2A,0x00,0x00,0x00,0x14,
    0x1F,0x20,0x03,0xD5,0x1F,0x20,0x03,0xD5,
    0x1F,0x20,0x03,0xD5,0x1F,0x64,0x14,0x39,
    0x1F,0x68,0x14,0x39,0x00,0x00,0x00,0x14,
    0xF4,0x03,0x00,0xAA,0x00,0x00,0x00,0x14
};
static const unsigned char STREAM_OPEN_EPILOGUE_STOCK
        [STREAM_OPEN_EPILOGUE_BYTES]={
    0xE0,0x03,0x14,0x2A,0xF4,0x4F,0x54,0xA9,
    0xF6,0x57,0x53,0xA9,0xF8,0x5F,0x52,0xA9,
    0xFA,0x67,0x51,0xA9,0xFD,0x7B,0x4F,0xA9,
    0xFC,0x83,0x40,0xF9,0xFF,0x43,0x05,0x91,
    0xC0,0x03,0x5F,0xD6
};
static const unsigned char STREAM_OPEN_HELPER_TEMPLATE[20]={
    0x74,0x00,0x00,0x35,0x60,0x0A,0x40,0xF9,
    0x00,0x00,0x00,0x94,0xE0,0x03,0x14,0x2A,
    0x00,0x00,0x00,0x14
};
static const unsigned char UPDATE_CONCURRENT_HELPER_V2_TEMPLATE
        [UPDATE_CONCURRENT_PREVIOUS_BYTES]={
    0x88,0x6A,0x54,0x39,0xC8,0x03,0x00,0x34,
    0x95,0x3A,0x41,0xF9,0x96,0x3E,0x41,0xF9,
    0xF6,0x01,0x00,0xB4,0x17,0x01,0x80,0xD2,
    0x18,0x00,0x80,0xD2,0xA9,0x6A,0x77,0xF8,
    0x09,0x01,0x00,0xB4,0x2A,0x25,0x41,0xF9,
    0x2B,0x29,0x41,0xF9,0x7F,0x01,0x0A,0xEB,
    0x89,0x00,0x00,0x54,0x18,0x07,0x00,0x11,
    0x1F,0x0B,0x00,0x71,0xA2,0x01,0x00,0x54,
    0xD6,0x06,0x00,0xF1,0xF7,0x42,0x00,0x91,
    0xA1,0xFE,0xFF,0x54,0x89,0x9E,0x42,0xF9,
    0x3F,0x05,0x00,0xF1,0x88,0x00,0x00,0x54,
    0x9F,0x6A,0x14,0x39,0x08,0x00,0x00,0x14,
    0x00,0x00,0x00,0x14,0x28,0x00,0x80,0x52,
    0x88,0x6A,0x14,0x39,0x04,0x00,0x00,0x14,
    0x48,0x00,0x80,0x52,0x88,0x6A,0x14,0x39,
    0xFA,0x03,0x1F,0x2A,0x00,0x00,0x00,0x14,
    0x1F,0x20,0x03,0xD5,0x1F,0x20,0x03,0xD5,
    0x1F,0x20,0x03,0xD5,0x1F,0x64,0x14,0x39,
    0x1F,0x68,0x14,0x39,0x00,0x00,0x00,0x14,
    0xF4,0x03,0x00,0xAA,0x00,0x00,0x00,0x14
};
static const unsigned char UPDATE_CONCURRENT_HELPER_V1_TEMPLATE
        [UPDATE_CONCURRENT_LEGACY_BYTES]={
    0x88,0x6A,0x54,0x39,0x7F,0x06,0x00,0x71,
    0xC9,0x00,0x00,0x54,0xE8,0x00,0x00,0x34,
    0x48,0x00,0x80,0x52,0x88,0x6A,0x14,0x39,
    0xFA,0x03,0x1F,0x2A,0x03,0x00,0x00,0x14,
    0x48,0x00,0x08,0x36,0x9F,0x6A,0x14,0x39,
    0x00,0x00,0x00,0x14,0x9F,0x66,0x14,0x39,
    0x9F,0x6A,0x14,0x39,0x00,0x00,0x00,0x14,
    0xF4,0x03,0x00,0xAA,0x00,0x00,0x00,0x14
};
static const unsigned char UPDATE_CONCURRENT_HELPER_V0_TEMPLATE
        [UPDATE_CONCURRENT_LEGACY_BYTES]={
    0x88,0x6A,0x54,0x39,0x7F,0x06,0x00,0x71,
    0xC9,0x00,0x00,0x54,0xE8,0x00,0x00,0x34,
    0x48,0x00,0x80,0x52,0x88,0x6A,0x14,0x39,
    0xFA,0x03,0x1F,0x2A,0x03,0x00,0x00,0x14,
    0x48,0x00,0x08,0x36,0x9F,0x6A,0x14,0x39,
    0x00,0x00,0x00,0x14,0x1F,0x64,0x14,0x39,
    0x1F,0x68,0x14,0x39,0x00,0x00,0x00,0x14,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
static const size_t STREAM_EVENT_PATCH_OFFSETS[STREAM_EVENT_PATCH_COUNT]={
    0x1F94u,0x2170u,0x22B4u
};
static const size_t STREAM_EVENT_PATCH_SIZES[STREAM_EVENT_PATCH_COUNT]={
    28u,28u,4u
};
static const unsigned char STREAM_EVENT_STOCK_TEMPLATE
        [STREAM_EVENT_PATCH_COUNT][STREAM_EVENT_PATCH_MAX_BYTES]={
    {0x08,0xD4,0x0F,0x36,0x40,0x07,0x00,0x91,
     0xF5,0xCB,0x40,0xF9,0x7B,0x0A,0x40,0xF9,
     0x00,0x00,0x00,0x00,0x1F,0x40,0x00,0xB1,
     0xA2,0x50,0x00,0x54},
    {0x58,0x06,0x00,0xB0,0x18,0xC7,0x46,0xF9,
     0x08,0x03,0x40,0x39,0x88,0x01,0x08,0x36,
     0xE6,0xCB,0x40,0xF9,0xC3,0xEC,0xFF,0xF0,
     0x63,0x9C,0x25,0x91},
    {0xD8,0xFD,0xFF,0x17,0x00,0x00,0x00,0x00}
};
static const unsigned char STREAM_EVENT_RECOMPUTE_LEGACY
        [STREAM_EVENT_PATCH_COUNT][STREAM_EVENT_PATCH_MAX_BYTES]={
    /* A committed +appname supersedes the transient handoff. */
    {0x1F,0x67,0x14,0x39,0x18,0x01,0x00,0x14},
    {0},
    {0x51,0x00,0x00,0x14}
};
static const unsigned char STREAM_EVENT_INLINE_TEMPLATE
        [STREAM_EVENT_PATCH_MAX_BYTES]={
    0x1F,0x67,0x14,0x39,0x08,0x9F,0x42,0xF9,
    0x1F,0x05,0x00,0xF1,0x69,0x00,0x00,0x54,
    0x28,0x00,0x80,0x52,0x08,0x6B,0x14,0x39,
    0x00,0x00,0x00,0x14
};
static const unsigned char STREAM_EVENT_HANDOFF_LEGACY
        [STREAM_EVENT_PATCH_MAX_BYTES]={
    0x19,0x01,0x00,0x14,0x40,0x07,0x00,0x91
};
static const unsigned char STREAM_REF_LEGACY_EVENT_RECOMPUTE[4]={
    0xE9,0x00,0x00,0x14
};
static const unsigned char STREAM_REF_STOCK_TEMPLATE[STREAM_REF_PATCH_BYTES]={
    0xE8,0x03,0x49,0x39,0x15,0x28,0x40,0xB9,0x68,0x00,0x00,0x36,0xE0,0x2B,0x41,0xF9,
    0x20,0xC9,0x02,0x94,0x15,0x04,0x00,0x34,0x48,0x06,0x00,0xB0,0x08,0xC5,0x46,0xF9,
    0x08,0x01,0x40,0x39,0x08,0xCE,0x0F,0x36,0xF5,0xCB,0x40,0xF9,0x76,0x0A,0x40,0xF9,
    0xE0,0x03,0x09,0x91,0x41,0x07,0x00,0x91,0xF7,0x03,0x09,0x91,0x7D,0x1E,0xFC,0x97,
    0xF7,0x37,0x00,0xF9,0x42,0x06,0x00,0xB0,0x42,0x80,0x46,0xF9,0xC0,0x82,0x14,0x91,
    0xE1,0x03,0x09,0x91,0xE3,0xA3,0x01,0x91,0xE4,0xFF,0x08,0x91,0xD5,0xEB,0x02,0x94,
    0x07,0x28,0x40,0xB9,0xC3,0xEC,0xFF,0xF0,0x63,0x9C,0x25,0x91,0x41,0xED,0xFF,0x90,
    0x21,0xC0,0x33,0x91,0x22,0xEC,0xFF,0xF0,0x42,0xAC,0x0B,0x91,0x60,0x00,0x80,0x52,
    0x04,0xCF,0x80,0x52,0xE5,0x03,0x03,0xAA,0xE6,0x03,0x15,0xAA,0x25,0xC9,0x02,0x94,
    0x79,0x00,0x00,0x14
};
static const unsigned char STREAM_REF_FAILED_IDLE_GUARD_TEMPLATE
        [STREAM_REF_PATCH_BYTES]={
    0xF5,0x03,0x00,0xAA,0xE8,0x03,0x49,0x39,0x68,0x00,0x00,0x36,0xE0,0x2B,0x41,0xF9,
    0x00,0x00,0x00,0x94,0xA8,0x2A,0x40,0xB9,0xC8,0x01,0x00,0x35,0x76,0x0A,0x40,0xF9,
    0xC8,0x9E,0x42,0xF9,0x1F,0x05,0x00,0xF1,0x61,0x03,0x00,0x54,0xC8,0x4E,0x54,0x39,
    0x28,0x03,0x00,0x34,0xE0,0x03,0x16,0xAA,0x00,0x00,0x00,0x94,0x1F,0x04,0x00,0x71,
    0xA1,0x02,0x00,0x54,0x28,0x00,0x80,0x52,0xC8,0x66,0x14,0x39,0x12,0x00,0x00,0x14,
    0xDE,0x00,0x00,0x14,0x28,0x17,0x40,0xF9,0xA9,0x83,0x5F,0xF8,0x1F,0x01,0x09,0xEB,
    0x01,0x00,0x00,0x54,0x55,0x00,0x00,0x35,0xDF,0x66,0x14,0x39,0xE0,0x03,0x16,0xAA,
    0xF4,0x4F,0x5B,0xA9,0xF6,0x57,0x5A,0xA9,0xF8,0x5F,0x59,0xA9,0xFA,0x67,0x58,0xA9,
    0xFC,0x6F,0x57,0xA9,0xFD,0x7B,0x56,0xA9,0xFF,0x03,0x07,0x91,0x00,0x00,0x00,0x14,
    0x1F,0x20,0x03,0xD5
};
static const unsigned char STREAM_REF_PATCH_TEMPLATE[STREAM_REF_PATCH_BYTES]={
    0xF5,0x03,0x00,0xAA,0xE8,0x03,0x49,0x39,0x68,0x00,0x00,0x36,0xE0,0x2B,0x41,0xF9,
    0x00,0x00,0x00,0x94,0xA8,0x2A,0x40,0xB9,0x88,0x1D,0x00,0x35,0x76,0x0A,0x40,0xF9,
    0xC8,0x9E,0x42,0xF9,0x1F,0x05,0x00,0xF1,0x61,0x03,0x00,0x54,0xC8,0x4E,0x54,0x39,
    0x28,0x03,0x00,0x34,0xE0,0x03,0x16,0xAA,0x00,0x00,0x00,0x94,0x1F,0x04,0x00,0x71,
    0xA1,0x02,0x00,0x54,0x68,0xB6,0x40,0xB9,0x1F,0x09,0x1E,0x72,0x21,0x02,0x00,0x54,
    0x08,0x02,0x90,0x37,0x10,0x00,0x00,0x14,0x28,0x17,0x40,0xF9,0xA9,0x83,0x5F,0xF8,
    0x1F,0x01,0x09,0xEB,0x01,0x00,0x00,0x54,0x1F,0x20,0x03,0xD5,0xE0,0x03,0x16,0xAA,
    0xF4,0x4F,0x5B,0xA9,0xF6,0x57,0x5A,0xA9,0xF8,0x5F,0x59,0xA9,0xFA,0x67,0x58,0xA9,
    0xFC,0x6F,0x57,0xA9,0xFD,0x7B,0x56,0xA9,0xFF,0x03,0x07,0x91,0x00,0x00,0x00,0x14,
    0xC0,0x66,0x14,0x39
};
static const unsigned char OUTPUT_POOL_TAIL_STOCK[OUTPUT_POOL_TAIL_PATCH_BYTES]={
    0x28,0x17,0x40,0xF9
};
static const unsigned char OUTPUT_POOL_TAIL_STOCK_TEMPLATE[48]={
    0x28,0x17,0x40,0xF9,0xA9,0x83,0x5F,0xF8,
    0x1F,0x01,0x09,0xEB,0x01,0x0F,0x00,0x54,
    0xF4,0x4F,0x5B,0xA9,0xF6,0x57,0x5A,0xA9,
    0xF8,0x5F,0x59,0xA9,0xFA,0x67,0x58,0xA9,
    0xFC,0x6F,0x57,0xA9,0xFD,0x7B,0x56,0xA9,
    0xFF,0x03,0x07,0x91,0xC0,0x03,0x5F,0xD6
};
static const unsigned char STREAM_UPDATE_CALL_TEMPLATE
        [STREAM_UPDATE_CALL_BYTES]={
    0x60,0x0A,0x40,0xF9,0x00,0x00,0x00,0x00,
    0x85,0xFD,0xFF,0x17
};
static const uint32_t STREAM_APP_MAP_MANAGER_ADD=0x91148300u;
static const uint32_t OUTPUT_POOL_MANAGER_ARG_MOV=0xAA0003F6u;
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
static void store_u32le(unsigned char *h, uint32_t value) {
    h[0] = (unsigned char)value;
    h[1] = (unsigned char)(value >> 8);
    h[2] = (unsigned char)(value >> 16);
    h[3] = (unsigned char)(value >> 24);
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

#define EXECUTABLE_TAIL_ALIGN 16u

typedef struct {
    uintptr_t segment_file_end;
    uintptr_t segment_mem_end;
    uintptr_t tail_start;
    uintptr_t tail_end;
    uintptr_t concurrent_off;
    uintptr_t cache_off;
    uint64_t concurrent_file_off;
    uint64_t cache_file_off;
} executable_tail_layout_t;

static int checked_absolute_range(uintptr_t base, uintptr_t offset,
                                  size_t length, uintptr_t *start,
                                  uintptr_t *end) {
    if (!start || !end || base > UINTPTR_MAX - offset) return 0;
    uintptr_t absolute = base + offset;
    if (length > (size_t)(UINTPTR_MAX - absolute)) return 0;
    *start = absolute;
    *end = absolute + (uintptr_t)length;
    return 1;
}

/* Reserve the highest RX page-tail slot for the cache helper and place the
 * lifecycle helper immediately below it. Both slots are outside every
 * PT_LOAD, so no function body or ELF object is repurposed as a code cave. */
static int derive_executable_tail_layout(
        const Elf64_Phdr *ph, size_t phnum, uintptr_t page,
        executable_tail_layout_t *layout) {
    if (!ph || !phnum || !layout || !page || (page & (page - 1)) != 0)
        return 0;
    memset(layout, 0, sizeof(*layout));
    uintptr_t segment_mem_end = 0;
    uintptr_t segment_file_end = 0;
    int executable_index = -1;
    for (size_t i = 0; i < phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_filesz > ph[i].p_memsz ||
            ph[i].p_vaddr > UINTPTR_MAX - ph[i].p_memsz ||
            ph[i].p_vaddr > UINTPTR_MAX - ph[i].p_filesz) {
            return 0;
        }
        if (!(ph[i].p_flags & PF_X) || ph[i].p_memsz == 0) continue;
        uintptr_t current_mem_end =
            (uintptr_t)(ph[i].p_vaddr + ph[i].p_memsz);
        if (current_mem_end > segment_mem_end) {
            segment_mem_end = current_mem_end;
            segment_file_end =
                (uintptr_t)(ph[i].p_vaddr + ph[i].p_filesz);
            executable_index = (int)i;
        }
    }
    if (executable_index < 0 || !segment_mem_end ||
        segment_mem_end > UINTPTR_MAX - (page - 1) ||
        segment_mem_end > UINTPTR_MAX - (EXECUTABLE_TAIL_ALIGN - 1)) {
        return 0;
    }
    uintptr_t occupied_end = segment_mem_end;
    if (segment_file_end > occupied_end) occupied_end = segment_file_end;
    uintptr_t tail_start =
        (occupied_end + EXECUTABLE_TAIL_ALIGN - 1) &
        ~(uintptr_t)(EXECUTABLE_TAIL_ALIGN - 1);
    uintptr_t tail_end =
        (segment_mem_end + page - 1) & ~(uintptr_t)(page - 1);
    if (tail_end <= tail_start ||
        tail_end - tail_start <
            UPDATE_CONCURRENT_HELPER_BYTES + CACHE_HELPER_TOTAL_BYTES) {
        return 0;
    }
    uintptr_t cache_off =
        (tail_end - CACHE_HELPER_TOTAL_BYTES) &
        ~(uintptr_t)(EXECUTABLE_TAIL_ALIGN - 1);
    if (cache_off < UPDATE_CONCURRENT_HELPER_BYTES) return 0;
    uintptr_t concurrent_off =
        (cache_off - UPDATE_CONCURRENT_HELPER_BYTES) &
        ~(uintptr_t)(EXECUTABLE_TAIL_ALIGN - 1);
    if (concurrent_off < tail_start ||
        concurrent_off > UINTPTR_MAX - UPDATE_CONCURRENT_HELPER_BYTES ||
        concurrent_off + UPDATE_CONCURRENT_HELPER_BYTES > cache_off ||
        cache_off > UINTPTR_MAX - CACHE_HELPER_TOTAL_BYTES ||
        cache_off + CACHE_HELPER_TOTAL_BYTES > tail_end) {
        return 0;
    }
    uintptr_t claimed_end = cache_off + CACHE_HELPER_TOTAL_BYTES;
    for (size_t i = 0; i < phnum; ++i) {
        if (ph[i].p_type != PT_LOAD) continue;
        if (ph[i].p_vaddr > UINTPTR_MAX - ph[i].p_memsz) return 0;
        if (ph[i].p_memsz == 0) continue;
        uintptr_t other_start = (uintptr_t)ph[i].p_vaddr;
        uintptr_t other_end =
            (uintptr_t)(ph[i].p_vaddr + ph[i].p_memsz);
        if (other_start < claimed_end && other_end > concurrent_off)
            return 0;
    }
    const Elf64_Phdr *selected = &ph[executable_index];
    uint64_t concurrent_delta =
        (uint64_t)concurrent_off - selected->p_vaddr;
    uint64_t cache_delta = (uint64_t)cache_off - selected->p_vaddr;
    if (concurrent_delta > UINT64_MAX - selected->p_offset ||
        cache_delta > UINT64_MAX - selected->p_offset) {
        return 0;
    }
    layout->segment_file_end = segment_file_end;
    layout->segment_mem_end = segment_mem_end;
    layout->tail_start = tail_start;
    layout->tail_end = tail_end;
    layout->concurrent_off = concurrent_off;
    layout->cache_off = cache_off;
    layout->concurrent_file_off = selected->p_offset + concurrent_delta;
    layout->cache_file_off = selected->p_offset + cache_delta;
    return 1;
}

static uintptr_t discover_cache_helper(pid_t pid, uintptr_t base, size_t need) {
    if (!base || need != CACHE_HELPER_TOTAL_BYTES ||
        !g_libpath[0] || !g_lib_inode) return 0;
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
    long page_long = sysconf(_SC_PAGESIZE);
    uintptr_t page = page_long > 0 ? (uintptr_t)page_long : 4096;
    executable_tail_layout_t layout;
    if (!derive_executable_tail_layout(ph, eh.e_phnum, page, &layout)) {
        free(ph);
        fprintf(stderr, "[a2h_patch] icache helper: executable PT_LOAD tail unavailable\n");
        return 0;
    }
    free(ph);
    uintptr_t concurrent_start = 0, concurrent_end = 0;
    uintptr_t absolute = 0, cache_end = 0;
    if (!checked_absolute_range(base, layout.concurrent_off,
                                UPDATE_CONCURRENT_HELPER_BYTES,
                                &concurrent_start, &concurrent_end) ||
        !checked_absolute_range(base, layout.cache_off, need,
                                &absolute, &cache_end)) {
        return 0;
    }
    uintptr_t map_start = 0, map_end = 0;
    if (!executable_hal_map_contains(pid, concurrent_start, cache_end,
                                     &map_start, &map_end)) {
        fprintf(stderr,
                "[a2h_patch] icache helper: candidate lacks exact private HAL RX map\n");
        return 0;
    }
    fprintf(stderr,
            "[a2h_patch] icache helper: ELF RX tail file_end=0x%lx mem_end=0x%lx concurrent=0x%lx cache=0x%lx abs=0x%lx bytes=%lu map=0x%lx-0x%lx\n",
            (unsigned long)layout.segment_file_end,
            (unsigned long)layout.segment_mem_end,
            (unsigned long)layout.concurrent_off,
            (unsigned long)layout.cache_off,
            (unsigned long)absolute, (unsigned long)need,
            (unsigned long)map_start, (unsigned long)map_end);
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

#define ELF_SYMBOL_MAX_BYTES 16384u
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

#define ELF_SYMBOL_CACHE_MAX 12
#define ELF_SYMBOL_CACHE_NAME_MAX 160

typedef struct {
    dev_t dev;
    ino_t ino;
    uint64_t file_size;
    size_t expected_size;
    char name[ELF_SYMBOL_CACHE_NAME_MAX];
    elf_a2h_symbol_t symbol;
    int valid;
} elf_symbol_cache_entry_t;

static elf_symbol_cache_entry_t g_symbol_cache[ELF_SYMBOL_CACHE_MAX];
static size_t g_symbol_cache_next;

static int elf_symbol_cache_lookup(int fd, uint64_t file_size,
                                   const char *name, size_t expected_size,
                                   elf_a2h_symbol_t *out) {
    struct stat st;
    if (!name || !out || fstat(fd, &st) != 0) return 0;
    for (size_t i = 0; i < ELF_SYMBOL_CACHE_MAX; ++i) {
        const elf_symbol_cache_entry_t *entry = &g_symbol_cache[i];
        if (entry->valid && entry->dev == st.st_dev && entry->ino == st.st_ino &&
            entry->file_size == file_size &&
            entry->expected_size == expected_size &&
            strcmp(entry->name, name) == 0) {
            *out = entry->symbol;
            fprintf(stderr,
                    "[a2h_patch] ELF symbol cache hit %s vaddr=0x%lx size=%lu\n",
                    name, (unsigned long)out->vaddr,
                    (unsigned long)out->size);
            return 1;
        }
    }
    return 0;
}

static void elf_symbol_cache_store(int fd, uint64_t file_size,
                                   const char *name, size_t expected_size,
                                   const elf_a2h_symbol_t *symbol) {
    struct stat st;
    size_t name_len = name ? strlen(name) : 0;
    if (!symbol || name_len == 0 || name_len >= ELF_SYMBOL_CACHE_NAME_MAX ||
        fstat(fd, &st) != 0) return;
    elf_symbol_cache_entry_t *entry =
        &g_symbol_cache[g_symbol_cache_next++ % ELF_SYMBOL_CACHE_MAX];
    memset(entry, 0, sizeof(*entry));
    entry->dev = st.st_dev;
    entry->ino = st.st_ino;
    entry->file_size = file_size;
    entry->expected_size = expected_size;
    memcpy(entry->name, name, name_len + 1);
    entry->symbol = *symbol;
    entry->valid = 1;
}

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

static int parse_unique_func_symbol(int fd, uint64_t file_size,
                                    const char *symbol_name,
                                    size_t expected_size,
                                    elf_a2h_symbol_t *out) {
    Elf64_Ehdr eh;
    if (!out || !symbol_name || !symbol_name[0]) {
        return ELF_RESOLVE_REJECTED;
    }
    if (elf_symbol_cache_lookup(fd, file_size, symbol_name, expected_size, out)) {
        return ELF_RESOLVE_VERIFIED;
    }
    if (!pread_exact(fd, &eh, sizeof(eh), 0) ||
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
    size_t symbol_name_len = strlen(symbol_name);
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
            if (!nul || (size_t)(nul - name) != symbol_name_len ||
                memcmp(name, symbol_name, symbol_name_len + 1) != 0) continue;
            named++;
            if (ELF64_ST_TYPE(sym->st_info) != STT_FUNC ||
                ELF64_ST_BIND(sym->st_info) != STB_GLOBAL ||
                sym->st_shndx == SHN_UNDEF || sym->st_shndx >= eh.e_shnum ||
                sh[sym->st_shndx].sh_type != SHT_PROGBITS ||
                !(sh[sym->st_shndx].sh_flags & SHF_ALLOC) ||
                !(sh[sym->st_shndx].sh_flags & SHF_EXECINSTR) ||
                sym->st_size == 0 ||
                (expected_size && sym->st_size != expected_size) ||
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
                "[a2h_patch] ELF symbol rejected name=%s named=%d unique=%d invalid=%d expected_size=%s%lu\n",
                symbol_name, named, unique, invalid,
                expected_size ? "" : "any<=",
                (unsigned long)(expected_size ? expected_size :
                                ELF_SYMBOL_MAX_BYTES));
        return ELF_RESOLVE_REJECTED;
    }
    if (named == 0) {
        fprintf(stderr, "[a2h_patch] ELF symbol unavailable: %s stripped\n",
                symbol_name);
        return ELF_RESOLVE_UNAVAILABLE;
    }
    *out = candidate;
    elf_symbol_cache_store(fd, file_size, symbol_name, expected_size, out);
    fprintf(stderr,
            "[a2h_patch] ELF symbol %s vaddr=0x%lx size=%lu file_off=0x%lx entries=%d\n",
            symbol_name, (unsigned long)out->vaddr,
            (unsigned long)out->size, (unsigned long)out->file_off, named);
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
    int parsed = parse_unique_func_symbol(fd, file_size, "is_A2H_app",
                                          0, &sym);
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

typedef struct {
    uintptr_t update_off;
    uintptr_t policy_off;
    uintptr_t stream_event_off;
    uintptr_t stream_open_off;
    uintptr_t output_pool_off;
    uintptr_t concurrent_helper_off;
    size_t update_flags_patch_off;
    size_t update_app_policy_patch_off;
    size_t policy_idle_count_cbz_off;
    size_t policy_idle_clear_off;
    size_t policy_game_off;
    size_t stream_ref_patch_off;
    size_t stream_event_patch_offsets[STREAM_EVENT_PATCH_COUNT];
    size_t stream_open_patch_off;
    size_t stream_update_call_off;
    size_t output_pool_tail_patch_off;
    unsigned char app_policy_disk[UPDATE_APP_POLICY_BYTES];
    unsigned char app_policy_stock[UPDATE_APP_POLICY_BYTES];
    unsigned char app_policy_relaxed[UPDATE_APP_POLICY_BYTES];
    unsigned char app_policy_legacy[UPDATE_APP_POLICY_BYTES];
    uintptr_t app_policy_call_target;
    uintptr_t allowed_call_target;
    unsigned char stream_ref_stock[STREAM_REF_PATCH_BYTES];
    unsigned char stream_ref_patched[STREAM_REF_PATCH_BYTES];
    unsigned char stream_ref_persistent_legacy[STREAM_REF_PATCH_BYTES];
    unsigned char stream_ref_failed_idle_guard[STREAM_REF_PATCH_BYTES];
    unsigned char stream_ref_legacy[STREAM_REF_PATCH_BYTES];
    uintptr_t stream_ref_delete_target;
    uintptr_t stream_ref_update_target;
    uintptr_t stream_event_strlen_target;
    uintptr_t output_pool_stack_fail_target;
    unsigned char output_pool_tail_patched[OUTPUT_POOL_TAIL_PATCH_BYTES];
    unsigned char output_pool_tail_persistent_legacy
        [OUTPUT_POOL_TAIL_PATCH_BYTES];
    unsigned char update_flags_patched[sizeof(UPDATE_FLAGS_STOCK)];
    unsigned char update_flags_guarded_legacy[sizeof(UPDATE_FLAGS_STOCK)];
    unsigned char concurrent_helper_disk[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char concurrent_helper_patched[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char concurrent_helper_previous[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char concurrent_helper_previous2[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char concurrent_helper_legacy[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char concurrent_helper_legacy2[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char policy_concurrent[GAME_POLICY_PATCH_BYTES];
    unsigned char stream_event_stock[STREAM_EVENT_PATCH_COUNT]
                                    [STREAM_EVENT_PATCH_MAX_BYTES];
    unsigned char stream_event_patched[STREAM_EVENT_PATCH_COUNT]
                                      [STREAM_EVENT_PATCH_MAX_BYTES];
    unsigned char stream_event_recompute_legacy[STREAM_EVENT_PATCH_COUNT]
                                               [STREAM_EVENT_PATCH_MAX_BYTES];
    unsigned char stream_event_handoff_legacy[STREAM_EVENT_PATCH_MAX_BYTES];
    unsigned char stream_open_stock[STREAM_OPEN_EVENT_BYTES];
    unsigned char stream_open_patched[STREAM_OPEN_EVENT_BYTES];
    unsigned char idle_count_branch[sizeof(POLICY_IDLE_COUNT_CBZ_STOCK)];
    unsigned char idle_clear_branch[IDLE_CLEAR_BRANCH_BYTES];
    int update_flags_state;
    int concurrent_helper_state;
    int update_app_policy_state;
    int idle_count_state;
    int idle_clear_state;
    int policy_relaxed;
    int stream_ref_state;
    int stream_open_state;
    int output_pool_state;
    unsigned int stream_events_patched;
    int valid;
} auxiliary_targets_t;

static auxiliary_targets_t g_auxiliary={0};

typedef struct {
    size_t patch_off;
    const unsigned char *stock;
    const unsigned char *patched;
    const unsigned char *alternate;
    const unsigned char *alternate2;
    const unsigned char *legacy;
    size_t patch_size;
} owned_overlay_region_t;

enum {
    OVERLAY_STOCK = 0,
    OVERLAY_PATCHED = 1,
    OVERLAY_ALTERNATE = 2,
    OVERLAY_ALTERNATE2 = 3,
    OVERLAY_ALTERNATE3 = 4,
    OVERLAY_LEGACY = 5
};

static int bytes_are_zero(const unsigned char *bytes, size_t count) {
    if (!bytes) return 0;
    for (size_t i = 0; i < count; ++i) {
        if (bytes[i] != 0) return 0;
    }
    return 1;
}

static int prepare_executable_concurrent_helper(
        int fd, uint64_t file_size, pid_t pid, uintptr_t base) {
    Elf64_Ehdr eh;
    if (fd < 0 || !base ||
        !pread_exact(fd, &eh, sizeof(eh), 0) ||
        memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS64 ||
        eh.e_ident[EI_DATA] != ELFDATA2LSB ||
        eh.e_phentsize != sizeof(Elf64_Phdr) ||
        eh.e_phnum == 0 || eh.e_phnum > 128 ||
        !file_range_ok(eh.e_phoff,
                       (uint64_t)eh.e_phnum * sizeof(Elf64_Phdr),
                       file_size)) {
        fprintf(stderr,
                "[a2h_patch] concurrent helper rejected: invalid ELF headers\n");
        return 0;
    }
    size_t ph_bytes = (size_t)eh.e_phnum * sizeof(Elf64_Phdr);
    Elf64_Phdr *ph = (Elf64_Phdr *)malloc(ph_bytes);
    if (!ph || !pread_exact(fd, ph, ph_bytes, eh.e_phoff)) {
        free(ph);
        fprintf(stderr,
                "[a2h_patch] concurrent helper rejected: program headers unreadable\n");
        return 0;
    }
    long page_long = sysconf(_SC_PAGESIZE);
    uintptr_t page = page_long > 0 ? (uintptr_t)page_long : 4096;
    executable_tail_layout_t layout;
    int derived = derive_executable_tail_layout(
        ph, eh.e_phnum, page, &layout);
    free(ph);
    if (!derived || layout.cache_off < layout.concurrent_off ||
        layout.cache_off > UINTPTR_MAX - CACHE_HELPER_TOTAL_BYTES) {
        fprintf(stderr,
                "[a2h_patch] concurrent helper rejected: RX tail layout unavailable\n");
        return 0;
    }
    size_t span = (size_t)(layout.cache_off + CACHE_HELPER_TOTAL_BYTES -
                           layout.concurrent_off);
    if (span < UPDATE_CONCURRENT_HELPER_BYTES || span > 4096 ||
        layout.cache_file_off < layout.concurrent_file_off ||
        layout.cache_file_off - layout.concurrent_file_off !=
            layout.cache_off - layout.concurrent_off ||
        !file_range_ok(layout.concurrent_file_off, span, file_size)) {
        fprintf(stderr,
                "[a2h_patch] concurrent helper rejected: disk tail range invalid\n");
        return 0;
    }
    unsigned char *disk_span = (unsigned char *)malloc(span);
    if (!disk_span ||
        !pread_exact(fd, disk_span, span, layout.concurrent_file_off) ||
        !bytes_are_zero(disk_span, span)) {
        free(disk_span);
        fprintf(stderr,
                "[a2h_patch] concurrent helper rejected: disk RX tail is not zero\n");
        return 0;
    }
    memcpy(g_auxiliary.concurrent_helper_disk, disk_span,
           UPDATE_CONCURRENT_HELPER_BYTES);
    free(disk_span);
    uintptr_t concurrent_start = 0, concurrent_end = 0;
    uintptr_t cache_start = 0, cache_end = 0;
    if (!checked_absolute_range(base, layout.concurrent_off,
                                UPDATE_CONCURRENT_HELPER_BYTES,
                                &concurrent_start, &concurrent_end) ||
        !checked_absolute_range(base, layout.cache_off,
                                CACHE_HELPER_TOTAL_BYTES,
                                &cache_start, &cache_end) ||
        concurrent_end > cache_start ||
        !executable_hal_map_contains(pid, concurrent_start, cache_end,
                                     NULL, NULL)) {
        fprintf(stderr,
                "[a2h_patch] concurrent helper rejected: exact HAL RX map missing\n");
        return 0;
    }
    g_auxiliary.concurrent_helper_off = layout.concurrent_off;
    fprintf(stderr,
            "[a2h_patch] concurrent helper RX slot file_end=0x%lx mem_end=0x%lx slot=0x%lx cache=0x%lx bytes=%u disk=zero\n",
            (unsigned long)layout.segment_file_end,
            (unsigned long)layout.segment_mem_end,
            (unsigned long)layout.concurrent_off,
            (unsigned long)layout.cache_off,
            UPDATE_CONCURRENT_HELPER_BYTES);
    return 1;
}

static int resolve_owned_executable_helper(pid_t pid, uintptr_t base) {
    unsigned char live[UPDATE_CONCURRENT_HELPER_BYTES];
    if (!g_auxiliary.concurrent_helper_off ||
        base > UINTPTR_MAX - g_auxiliary.concurrent_helper_off ||
        mem_r(pid, base + g_auxiliary.concurrent_helper_off,
              live, sizeof(live)) != 0) {
        return 0;
    }
    if (memcmp(live, g_auxiliary.concurrent_helper_disk,
               sizeof(live)) == 0) {
        g_auxiliary.concurrent_helper_state = OVERLAY_STOCK;
    } else if (memcmp(live, g_auxiliary.concurrent_helper_patched,
                      sizeof(live)) == 0) {
        g_auxiliary.concurrent_helper_state = OVERLAY_PATCHED;
    } else if (memcmp(live, g_auxiliary.concurrent_helper_previous,
                      sizeof(live)) == 0) {
        g_auxiliary.concurrent_helper_state = OVERLAY_LEGACY;
    } else if (memcmp(live, g_auxiliary.concurrent_helper_previous2,
                      sizeof(live)) == 0) {
        g_auxiliary.concurrent_helper_state = OVERLAY_ALTERNATE3;
    } else if (memcmp(live, g_auxiliary.concurrent_helper_legacy,
                      sizeof(live)) == 0) {
        g_auxiliary.concurrent_helper_state = OVERLAY_ALTERNATE;
    } else if (memcmp(live, g_auxiliary.concurrent_helper_legacy2,
                      sizeof(live)) == 0) {
        g_auxiliary.concurrent_helper_state = OVERLAY_ALTERNATE2;
    } else {
        fprintf(stderr,
                "[a2h_patch] concurrent helper rejected: RX slot is foreign or partial\n");
        return 0;
    }
    fprintf(stderr,
            "[a2h_patch] concurrent helper ownership slot=0x%lx state=%s\n",
            (unsigned long)g_auxiliary.concurrent_helper_off,
            g_auxiliary.concurrent_helper_state == OVERLAY_PATCHED ?
                "owned" :
            (g_auxiliary.concurrent_helper_state == OVERLAY_LEGACY ?
                "previous-176" :
            (g_auxiliary.concurrent_helper_state == OVERLAY_ALTERNATE3 ?
                "previous-160" :
            (g_auxiliary.concurrent_helper_state == OVERLAY_ALTERNATE ?
                "legacy-64" :
            (g_auxiliary.concurrent_helper_state == OVERLAY_ALTERNATE2 ?
                "legacy-56" : "stock-zero")))));
    return 1;
}

static int decode_aarch64_bl(uintptr_t site, uint32_t instruction,
                             uintptr_t *target) {
    if (!target || (instruction & 0xFC000000u) != 0x94000000u ||
        site > (uintptr_t)INT64_MAX) {
        return 0;
    }
    int64_t immediate = (int64_t)(instruction & 0x03FFFFFFu);
    if (immediate & 0x02000000ll) immediate -= 0x04000000ll;
    int64_t resolved = (int64_t)site + immediate * 4;
    if (resolved < 0 || (uint64_t)resolved > (uint64_t)UINTPTR_MAX ||
        ((uintptr_t)resolved & 3u) != 0) {
        return 0;
    }
    *target = (uintptr_t)resolved;
    return 1;
}

static int encode_aarch64_bl(uintptr_t site, uintptr_t target,
                             uint32_t *instruction) {
    if (!instruction || site > (uintptr_t)INT64_MAX ||
        target > (uintptr_t)INT64_MAX) {
        return 0;
    }
    int64_t delta = (int64_t)target - (int64_t)site;
    int64_t immediate = delta / 4;
    if ((delta & 3ll) != 0 || immediate < -0x02000000ll ||
        immediate > 0x01FFFFFFll) {
        return 0;
    }
    *instruction = 0x94000000u |
                   ((uint32_t)immediate & 0x03FFFFFFu);
    return 1;
}

static int encode_aarch64_b(uintptr_t site, uintptr_t target,
                            uint32_t *instruction) {
    if (!instruction || site > (uintptr_t)INT64_MAX ||
        target > (uintptr_t)INT64_MAX) {
        return 0;
    }
    int64_t delta = (int64_t)target - (int64_t)site;
    int64_t immediate = delta / 4;
    if ((delta & 3ll) != 0 || immediate < -0x02000000ll ||
        immediate > 0x01FFFFFFll) {
        return 0;
    }
    *instruction = 0x14000000u |
                   ((uint32_t)immediate & 0x03FFFFFFu);
    return 1;
}

static int decode_aarch64_b_cond(uintptr_t site, uint32_t instruction,
                                 uintptr_t *target, uint32_t *condition) {
    if (!target || (instruction & 0xFF000010u) != 0x54000000u ||
        site > (uintptr_t)INT64_MAX) {
        return 0;
    }
    int64_t immediate = (int64_t)((instruction >> 5) & 0x7FFFFu);
    if (immediate & 0x40000ll) immediate -= 0x80000ll;
    int64_t resolved = (int64_t)site + immediate * 4;
    if (resolved < 0 || (uint64_t)resolved > (uint64_t)UINTPTR_MAX ||
        ((uintptr_t)resolved & 3u) != 0) {
        return 0;
    }
    *target = (uintptr_t)resolved;
    if (condition) *condition = instruction & 0xFu;
    return 1;
}

static int encode_aarch64_b_cond(uintptr_t site, uintptr_t target,
                                 uint32_t condition,
                                 uint32_t *instruction) {
    if (!instruction || condition > 0xFu ||
        site > (uintptr_t)INT64_MAX || target > (uintptr_t)INT64_MAX) {
        return 0;
    }
    int64_t delta = (int64_t)target - (int64_t)site;
    int64_t immediate = delta / 4;
    if ((delta & 3ll) != 0 || immediate < -0x40000ll ||
        immediate > 0x3FFFFll) {
        return 0;
    }
    *instruction = 0x54000000u |
                   (((uint32_t)immediate & 0x7FFFFu) << 5) |
                   condition;
    return 1;
}

static int encode_aarch64_cbz_like(uintptr_t site, uintptr_t target,
                                   uint32_t shape,
                                   uint32_t *instruction) {
    if (!instruction || (shape & 0x7E000000u) != 0x34000000u ||
        site > (uintptr_t)INT64_MAX || target > (uintptr_t)INT64_MAX) {
        return 0;
    }
    int64_t delta = (int64_t)target - (int64_t)site;
    int64_t immediate = delta / 4;
    if ((delta & 3ll) != 0 || immediate < -0x40000ll ||
        immediate > 0x3FFFFll) {
        return 0;
    }
    *instruction = (shape & 0xFF00001Fu) |
                   (((uint32_t)immediate & 0x7FFFFu) << 5);
    return 1;
}

static int build_idle_clear_overlay(
        uintptr_t update_vaddr, uintptr_t policy_vaddr,
        uintptr_t concurrent_helper_vaddr,
        unsigned char *update_patched,
        unsigned char *update_guarded_legacy,
        unsigned char *idle_count_branch,
        unsigned char *idle_clear_branch) {
    if (!update_vaddr || !policy_vaddr || !concurrent_helper_vaddr ||
        !update_patched ||
        !update_guarded_legacy || !idle_count_branch ||
        !idle_clear_branch ||
        UPDATE_IDLE_GUARD_OFF != UPDATE_FLAGS_PATCH_OFF + 16u ||
        UPDATE_IDLE_HELPER_OFF != UPDATE_IDLE_GUARD_OFF + 4u ||
        UPDATE_IDLE_HELPER_BRANCH_OFF != UPDATE_IDLE_HELPER_OFF + 4u ||
        CONCURRENT_IDLE_HELPER_OFF >= UPDATE_CONCURRENT_HELPER_BYTES ||
        update_vaddr > UINTPTR_MAX -
            (UPDATE_FLAGS_PATCH_OFF + sizeof(UPDATE_FLAGS_STOCK) -
             sizeof(uint32_t)) ||
        policy_vaddr > UINTPTR_MAX - IDLE_CLEAR_RESUME_OFF ||
        concurrent_helper_vaddr >
            UINTPTR_MAX - CONCURRENT_ZERO_INIT_RETURN_B_OFF) {
        return 0;
    }
    uintptr_t branch_site = policy_vaddr + IDLE_CLEAR_BRANCH_PATCH_OFF;
    uintptr_t count_branch_site = policy_vaddr + POLICY_IDLE_COUNT_CBZ_OFF;
    uintptr_t helper_site = update_vaddr + UPDATE_IDLE_HELPER_OFF;
    uintptr_t helper_branch_site = update_vaddr +
                                   UPDATE_IDLE_HELPER_BRANCH_OFF;
    uintptr_t legacy_return_site = update_vaddr +
                                   UPDATE_FLAGS_PATCH_OFF + 28u;
    uintptr_t idle_helper_target = concurrent_helper_vaddr +
                                   CONCURRENT_IDLE_HELPER_OFF;
    uintptr_t zero_init_target = concurrent_helper_vaddr +
                                 CONCURRENT_ZERO_INIT_OFF;
    uintptr_t resume_target = policy_vaddr + IDLE_CLEAR_RESUME_OFF;
    uint32_t branch_to_helper = 0;
    uint32_t count_to_zero_init = 0;
    uint32_t branch_to_idle_helper = 0;
    uint32_t legacy_branch_to_resume = 0;
    if (!encode_aarch64_cbz_like(
            count_branch_site, zero_init_target, POLICY_IDLE_COUNT_CBZ,
            &count_to_zero_init) ||
        !encode_aarch64_b(branch_site, helper_site, &branch_to_helper) ||
        !encode_aarch64_b(helper_branch_site, idle_helper_target,
                          &branch_to_idle_helper) ||
        !encode_aarch64_b(legacy_return_site, resume_target,
                          &legacy_branch_to_resume)) {
        return 0;
    }
    memcpy(update_patched, UPDATE_FLAGS_GAME_HANDOFF_LEGACY,
           sizeof(UPDATE_FLAGS_GAME_HANDOFF_LEGACY));
    memcpy(update_patched + UPDATE_IDLE_GUARD_OFF -
           UPDATE_FLAGS_PATCH_OFF, UPDATE_IDLE_HELPER_TEMPLATE,
           sizeof(UPDATE_IDLE_HELPER_TEMPLATE));
    store_u32le(update_patched + UPDATE_IDLE_HELPER_BRANCH_OFF -
                UPDATE_FLAGS_PATCH_OFF, branch_to_idle_helper);
    memcpy(update_guarded_legacy, UPDATE_FLAGS_GAME_HANDOFF_LEGACY,
           sizeof(UPDATE_FLAGS_GAME_HANDOFF_LEGACY));
    memcpy(update_guarded_legacy + UPDATE_IDLE_GUARD_OFF -
           UPDATE_FLAGS_PATCH_OFF, UPDATE_IDLE_HELPER_GUARDED_LEGACY,
           sizeof(UPDATE_IDLE_HELPER_GUARDED_LEGACY));
    store_u32le(update_guarded_legacy + 28u,
                 legacy_branch_to_resume);
    store_u32le(idle_count_branch, count_to_zero_init);
    store_u32le(idle_clear_branch, branch_to_helper);
    return 1;
}

static int stream_event_stock_shape(
        const unsigned char stream_events[STREAM_EVENT_PATCH_COUNT]
                                         [STREAM_EVENT_PATCH_MAX_BYTES]) {
    if (!stream_events ||
        memcmp(stream_events[0], STREAM_EVENT_STOCK_TEMPLATE[0],
               sizeof(uint32_t) * 4u) != 0 ||
        (load_u32le(stream_events[0] + sizeof(uint32_t) * 4u) &
         0xFC000000u) != 0x94000000u ||
        memcmp(stream_events[0] + sizeof(uint32_t) * 5u,
               STREAM_EVENT_STOCK_TEMPLATE[0] + sizeof(uint32_t) * 5u,
               STREAM_EVENT_PATCH_SIZES[0] - sizeof(uint32_t) * 5u) != 0 ||
        (load_u32le(stream_events[1]) & 0x9F00001Fu) != 0x90000018u ||
        memcmp(stream_events[1] + sizeof(uint32_t),
               STREAM_EVENT_STOCK_TEMPLATE[1] + sizeof(uint32_t),
               sizeof(uint32_t) * 4u) != 0 ||
        (load_u32le(stream_events[1] + sizeof(uint32_t) * 5u) &
         0x9F00001Fu) != 0x90000003u ||
        (load_u32le(stream_events[1] + sizeof(uint32_t) * 6u) &
         0xFFC003FFu) != 0x91000063u ||
        memcmp(stream_events[2], STREAM_EVENT_STOCK_TEMPLATE[2],
               STREAM_EVENT_PATCH_SIZES[2]) != 0) {
        return 0;
    }
    return 1;
}

static int build_concurrent_helpers(
        uintptr_t helper_vaddr, uintptr_t policy_vaddr,
        uintptr_t stream_vaddr, uintptr_t stream_open_vaddr,
        size_t stream_open_patch_off,
        uintptr_t stream_recompute_target,
        uintptr_t update_a2h_target,
        const unsigned char stream_event_stock[STREAM_EVENT_PATCH_COUNT]
                                                [STREAM_EVENT_PATCH_MAX_BYTES],
        unsigned char *helper_patched,
        unsigned char *helper_previous,
        unsigned char *helper_previous2,
        unsigned char *helper_legacy,
        unsigned char *helper_legacy2,
        unsigned char *policy_concurrent,
        unsigned char stream_events[STREAM_EVENT_PATCH_COUNT]
                                   [STREAM_EVENT_PATCH_MAX_BYTES],
        unsigned char stream_event_recompute_legacy
                [STREAM_EVENT_PATCH_COUNT][STREAM_EVENT_PATCH_MAX_BYTES],
        unsigned char *stream_event_handoff_legacy,
        unsigned char *stream_open_patched) {
    if (!helper_vaddr || !policy_vaddr || !stream_vaddr ||
        !stream_open_vaddr || !stream_recompute_target ||
        !update_a2h_target || !stream_event_stock || !helper_patched ||
        !helper_previous || !helper_previous2 ||
        !helper_legacy || !helper_legacy2 ||
        !policy_concurrent ||
        !stream_events ||
        !stream_event_recompute_legacy || !stream_event_handoff_legacy ||
        !stream_open_patched ||
        !stream_event_stock_shape(stream_event_stock) ||
        helper_vaddr >
            UINTPTR_MAX - CONCURRENT_ZERO_INIT_RETURN_B_OFF ||
        policy_vaddr > UINTPTR_MAX -
            (GAME_POLICY_PATCH_OFF + sizeof(uint32_t)) ||
        stream_vaddr > UINTPTR_MAX - STREAM_EVENT_PATCH_OFFSETS[2] ||
        stream_open_patch_off > UINTPTR_MAX - stream_open_vaddr ||
        stream_open_vaddr + stream_open_patch_off >
            UINTPTR_MAX - STREAM_OPEN_EVENT_BYTES) {
        return 0;
    }
    uintptr_t policy_return_site = helper_vaddr +
                                    CONCURRENT_POLICY_RETURN_B_OFF;
    uintptr_t helper_core = helper_vaddr + CONCURRENT_CORE_OFF;
    uintptr_t helper_main = helper_vaddr + CONCURRENT_MAIN_OFF;
    uintptr_t pending_decay_site = helper_vaddr +
                                     CONCURRENT_PENDING_DECAY_B_OFF;
    uintptr_t helper_entry_site = helper_vaddr + CONCURRENT_ENTRY_B_OFF;
    uintptr_t legacy_helper_vaddr = helper_vaddr + CONCURRENT_LEGACY_SHIFT;
    uintptr_t legacy_policy_return_site = legacy_helper_vaddr +
        CONCURRENT_LEGACY_POLICY_RETURN_B_OFF;
    uintptr_t idle_return_site = helper_vaddr +
                                  CONCURRENT_IDLE_RETURN_B_OFF;
    uintptr_t zero_init_return_site = helper_vaddr +
                                      CONCURRENT_ZERO_INIT_RETURN_B_OFF;
    uintptr_t policy_branch_site = policy_vaddr + GAME_POLICY_PATCH_OFF +
                                   GAME_POLICY_HELPER_BRANCH_OFF;
    uintptr_t policy_helper_entry = helper_vaddr + CONCURRENT_ENTRY_B_OFF;
    uintptr_t stream_event_sites[2] = {
        stream_vaddr + STREAM_EVENT_PATCH_OFFSETS[0],
        stream_vaddr + STREAM_EVENT_PATCH_OFFSETS[1]
    };
    uintptr_t stream_recompute_site = stream_vaddr +
                                      STREAM_EVENT_PATCH_OFFSETS[2];
    uintptr_t stream_open_site = stream_open_vaddr +
                                 stream_open_patch_off;
    uintptr_t stream_open_helper = helper_vaddr +
                                   CONCURRENT_OPEN_HELPER_OFF;
    uintptr_t stream_open_update_site = helper_vaddr +
                                        CONCURRENT_OPEN_UPDATE_BL_OFF;
    uintptr_t stream_open_return_site = helper_vaddr +
                                        CONCURRENT_OPEN_RETURN_B_OFF;
    uint32_t policy_return = 0;
    uint32_t pending_decay = 0;
    uint32_t helper_entry = 0;
    uint32_t legacy_policy_return = 0;
    uint32_t idle_return = 0;
    uint32_t zero_init_return = 0;
    uint32_t policy_to_helper = 0;
    uint32_t stream_to_update[2] = {0};
    uint32_t stream_recompute = 0;
    uint32_t stream_legacy_recompute = 0;
    uint32_t stream_open_call = 0;
    uint32_t stream_open_update = 0;
    uint32_t stream_open_return = 0;
    if (!encode_aarch64_b(pending_decay_site, helper_core,
                           &pending_decay) ||
        !encode_aarch64_b(helper_entry_site, helper_main,
                           &helper_entry) ||
        !encode_aarch64_b(
            policy_return_site,
            policy_vaddr + GAME_POLICY_PATCH_OFF + sizeof(uint32_t),
            &policy_return) ||
        !encode_aarch64_b(
            legacy_policy_return_site,
            policy_vaddr + GAME_POLICY_PATCH_OFF + sizeof(uint32_t),
            &legacy_policy_return) ||
        !encode_aarch64_b(idle_return_site,
                           policy_vaddr + IDLE_CLEAR_RESUME_OFF,
                           &idle_return) ||
        !encode_aarch64_b(zero_init_return_site,
                           policy_vaddr + IDLE_CLEAR_BRANCH_PATCH_OFF,
                           &zero_init_return) ||
        !encode_aarch64_b(policy_branch_site, policy_helper_entry,
                           &policy_to_helper) ||
        !encode_aarch64_b(
            stream_event_sites[0] + STREAM_EVENT_PATCH_SIZES[0] -
                sizeof(uint32_t),
            stream_recompute_target, &stream_to_update[0]) ||
        !encode_aarch64_b(
            stream_event_sites[1] + STREAM_EVENT_PATCH_SIZES[1] -
                sizeof(uint32_t),
            stream_recompute_target, &stream_to_update[1]) ||
        !encode_aarch64_b(stream_recompute_site, stream_recompute_target,
                           &stream_recompute) ||
        !encode_aarch64_b(stream_event_sites[0] + sizeof(uint32_t),
                           stream_recompute_target,
                           &stream_legacy_recompute) ||
        !encode_aarch64_bl(stream_open_site, stream_open_helper,
                           &stream_open_call) ||
        !encode_aarch64_bl(stream_open_update_site, update_a2h_target,
                           &stream_open_update) ||
        !encode_aarch64_b(stream_open_return_site,
                          stream_open_site + STREAM_OPEN_EVENT_BYTES,
                          &stream_open_return)) {
        return 0;
    }
    memset(helper_patched, 0, UPDATE_CONCURRENT_HELPER_BYTES);
    memcpy(helper_patched, STREAM_OPEN_HELPER_TEMPLATE,
           sizeof(STREAM_OPEN_HELPER_TEMPLATE));
    for (size_t off = sizeof(STREAM_OPEN_HELPER_TEMPLATE);
         off < CONCURRENT_CORE_OFF; off += sizeof(uint32_t)) {
        store_u32le(helper_patched + off, 0xD503201Fu);
    }
    memcpy(helper_patched + CONCURRENT_CORE_OFF,
           UPDATE_CONCURRENT_HELPER_V3_TEMPLATE,
           UPDATE_CONCURRENT_PREVIOUS_BYTES);
    store_u32le(helper_patched + CONCURRENT_OPEN_UPDATE_BL_OFF,
                 stream_open_update);
    store_u32le(helper_patched + CONCURRENT_OPEN_RETURN_B_OFF,
                 stream_open_return);
    store_u32le(helper_patched + CONCURRENT_PENDING_DECAY_B_OFF,
                 pending_decay);
    store_u32le(helper_patched + CONCURRENT_ENTRY_B_OFF, helper_entry);
    store_u32le(helper_patched + CONCURRENT_POLICY_RETURN_B_OFF,
                 policy_return);
    store_u32le(helper_patched + CONCURRENT_IDLE_RETURN_B_OFF,
                 idle_return);
    store_u32le(helper_patched + CONCURRENT_ZERO_INIT_RETURN_B_OFF,
                 zero_init_return);
    memset(helper_previous, 0, UPDATE_CONCURRENT_HELPER_BYTES);
    memset(helper_previous2, 0, UPDATE_CONCURRENT_HELPER_BYTES);
    memset(helper_legacy, 0, UPDATE_CONCURRENT_HELPER_BYTES);
    memset(helper_legacy2, 0, UPDATE_CONCURRENT_HELPER_BYTES);
    memcpy(helper_previous + CONCURRENT_PREVIOUS_SHIFT,
           UPDATE_CONCURRENT_HELPER_V3_TEMPLATE,
           UPDATE_CONCURRENT_PREVIOUS_BYTES);
    store_u32le(helper_previous + CONCURRENT_PENDING_DECAY_B_OFF,
                 pending_decay);
    store_u32le(helper_previous + CONCURRENT_ENTRY_B_OFF, helper_entry);
    store_u32le(helper_previous + CONCURRENT_POLICY_RETURN_B_OFF,
                 policy_return);
    store_u32le(helper_previous + CONCURRENT_IDLE_RETURN_B_OFF,
                 idle_return);
    store_u32le(helper_previous + CONCURRENT_ZERO_INIT_RETURN_B_OFF,
                 zero_init_return);
    memcpy(helper_previous2 + CONCURRENT_PREVIOUS2_SHIFT,
           UPDATE_CONCURRENT_HELPER_V2_TEMPLATE,
           UPDATE_CONCURRENT_PREVIOUS2_BYTES);
    store_u32le(helper_previous2 + CONCURRENT_ENTRY_B_OFF, helper_entry);
    store_u32le(helper_previous2 + CONCURRENT_POLICY_RETURN_B_OFF,
                 policy_return);
    store_u32le(helper_previous2 + CONCURRENT_IDLE_RETURN_B_OFF,
                 idle_return);
    store_u32le(helper_previous2 + CONCURRENT_ZERO_INIT_RETURN_B_OFF,
                 zero_init_return);
    memcpy(helper_legacy + CONCURRENT_LEGACY_SHIFT,
           UPDATE_CONCURRENT_HELPER_V1_TEMPLATE,
           UPDATE_CONCURRENT_LEGACY_BYTES);
    memcpy(helper_legacy2 + CONCURRENT_LEGACY_SHIFT,
           UPDATE_CONCURRENT_HELPER_V0_TEMPLATE,
           UPDATE_CONCURRENT_LEGACY_BYTES);
    store_u32le(helper_legacy + CONCURRENT_LEGACY_SHIFT +
                CONCURRENT_LEGACY_POLICY_RETURN_B_OFF,
                legacy_policy_return);
    store_u32le(helper_legacy + CONCURRENT_LEGACY_SHIFT +
                CONCURRENT_LEGACY_IDLE_RETURN_B_OFF,
                idle_return);
    store_u32le(helper_legacy + CONCURRENT_LEGACY_SHIFT +
                CONCURRENT_LEGACY_ZERO_INIT_RETURN_B_OFF,
                zero_init_return);
    store_u32le(helper_legacy2 + CONCURRENT_LEGACY_SHIFT +
                CONCURRENT_LEGACY_POLICY_RETURN_B_OFF,
                legacy_policy_return);
    store_u32le(helper_legacy2 + CONCURRENT_LEGACY_SHIFT +
                CONCURRENT_LEGACY_IDLE_RETURN_B_OFF,
                idle_return);
    memcpy(policy_concurrent, GAME_POLICY_CONCURRENT_TEMPLATE,
           GAME_POLICY_PATCH_BYTES);
    store_u32le(policy_concurrent + GAME_POLICY_HELPER_BRANCH_OFF,
                 policy_to_helper);
    memset(stream_events, 0, STREAM_EVENT_PATCH_COUNT *
           STREAM_EVENT_PATCH_MAX_BYTES);
    for (size_t i = 0; i < 2u; ++i) {
        memcpy(stream_events[i], STREAM_EVENT_INLINE_TEMPLATE,
               STREAM_EVENT_PATCH_SIZES[i]);
        store_u32le(stream_events[i] + STREAM_EVENT_PATCH_SIZES[i] -
                    sizeof(uint32_t), stream_to_update[i]);
    }
    store_u32le(stream_events[2], stream_recompute);
    store_u32le(stream_open_patched, stream_open_call);
    memcpy(stream_event_recompute_legacy, stream_event_stock,
           STREAM_EVENT_PATCH_COUNT * STREAM_EVENT_PATCH_MAX_BYTES);
    memcpy(stream_event_recompute_legacy[0],
           STREAM_EVENT_RECOMPUTE_LEGACY[0], sizeof(uint32_t));
    store_u32le(stream_event_recompute_legacy[0] + sizeof(uint32_t),
                 stream_legacy_recompute);
    store_u32le(stream_event_recompute_legacy[2], stream_recompute);
    memcpy(stream_event_handoff_legacy, stream_event_stock[0],
           STREAM_EVENT_PATCH_SIZES[0]);
    memcpy(stream_event_handoff_legacy, STREAM_EVENT_HANDOFF_LEGACY,
           sizeof(STREAM_EVENT_HANDOFF_LEGACY));
    return 1;
}

static int build_update_app_policy_overlay(
        uintptr_t update_vaddr, const unsigned char *stock,
        unsigned char *stock_handoff, unsigned char *relaxed_handoff,
        unsigned char *legacy, uintptr_t *call_target) {
    if (!stock || !stock_handoff || !relaxed_handoff || !legacy ||
        !call_target ||
        memcmp(stock, UPDATE_APP_POLICY_DISK_TEMPLATE,
               UPDATE_APP_POLICY_DISK_BL_OFF) != 0 ||
        memcmp(stock + UPDATE_APP_POLICY_DISK_BL_OFF + sizeof(uint32_t),
               UPDATE_APP_POLICY_DISK_TEMPLATE +
               UPDATE_APP_POLICY_DISK_BL_OFF + sizeof(uint32_t),
               UPDATE_APP_POLICY_BYTES - UPDATE_APP_POLICY_DISK_BL_OFF -
               sizeof(uint32_t)) != 0) {
        return 0;
    }
    uintptr_t target = 0;
    uintptr_t stock_call = update_vaddr + UPDATE_APP_POLICY_PATCH_OFF +
                           UPDATE_APP_POLICY_DISK_BL_OFF;
    uintptr_t stock_handoff_call = update_vaddr +
        UPDATE_APP_POLICY_PATCH_OFF + UPDATE_APP_POLICY_STOCK_BL_OFF;
    uintptr_t relaxed_call = update_vaddr + UPDATE_APP_POLICY_PATCH_OFF +
                             UPDATE_APP_POLICY_RELAXED_BL_OFF;
    uintptr_t legacy_call = update_vaddr + UPDATE_APP_POLICY_PATCH_OFF +
                            UPDATE_APP_POLICY_LEGACY_BL_OFF;
    uint32_t stock_replacement = 0;
    uint32_t relaxed_replacement = 0;
    uint32_t legacy_replacement = 0;
    if (!decode_aarch64_bl(stock_call,
                           load_u32le(stock +
                                      UPDATE_APP_POLICY_DISK_BL_OFF),
                           &target) ||
        !encode_aarch64_bl(stock_handoff_call, target,
                           &stock_replacement) ||
        !encode_aarch64_bl(relaxed_call, target,
                           &relaxed_replacement) ||
        !encode_aarch64_bl(legacy_call, target,
                           &legacy_replacement)) {
        return 0;
    }
    memcpy(stock_handoff, UPDATE_APP_POLICY_STOCK_TEMPLATE,
           UPDATE_APP_POLICY_BYTES);
    store_u32le(stock_handoff + UPDATE_APP_POLICY_STOCK_BL_OFF,
                stock_replacement);
    memcpy(relaxed_handoff, UPDATE_APP_POLICY_RELAXED_TEMPLATE,
           UPDATE_APP_POLICY_BYTES);
    store_u32le(relaxed_handoff + UPDATE_APP_POLICY_RELAXED_BL_OFF,
                relaxed_replacement);
    memcpy(legacy, stock, UPDATE_APP_POLICY_BYTES);
    memcpy(legacy, UPDATE_APP_POLICY_LEGACY_TEMPLATE,
           sizeof(UPDATE_APP_POLICY_LEGACY_TEMPLATE));
    store_u32le(legacy + UPDATE_APP_POLICY_LEGACY_BL_OFF,
                legacy_replacement);
    *call_target = target;
    return 1;
}

static int exact_aarch64_plt_entry(pid_t pid, uintptr_t base,
                                   uintptr_t target_vaddr) {
    if (base > UINTPTR_MAX - target_vaddr) return 0;
    uintptr_t address = base + target_vaddr;
    if (address < g_rx_start || address > g_rx_end ||
        sizeof(uint32_t) * 4u > g_rx_end - address) {
        return 0;
    }
    unsigned char bytes[sizeof(uint32_t) * 4u];
    if (mem_r(pid, address, bytes, sizeof(bytes)) != 0) return 0;
    uint32_t adrp = load_u32le(bytes);
    uint32_t ldr = load_u32le(bytes + 4);
    uint32_t add = load_u32le(bytes + 8);
    uint32_t branch = load_u32le(bytes + 12);
    uint32_t ldr_offset = ((ldr >> 10) & 0xFFFu) << 3;
    uint32_t add_offset = (add >> 10) & 0xFFFu;
    return (adrp & 0x9F00001Fu) == 0x90000010u &&
           (ldr & 0xFFC003FFu) == 0xF9400211u &&
           (add & 0xFFC003FFu) == 0x91000210u &&
           branch == 0xD61F0220u && ldr_offset == add_offset;
}

static int stream_ref_stock_shape(const unsigned char *stock) {
    if (!stock) return 0;
    for (size_t offset = 0; offset < STREAM_REF_PATCH_BYTES;
         offset += sizeof(uint32_t)) {
        uint32_t current = load_u32le(stock + offset);
        uint32_t expected = load_u32le(STREAM_REF_STOCK_TEMPLATE + offset);
        if (offset == 0x10u || offset == 0x3Cu || offset == 0x5Cu ||
            offset == 0x8Cu) {
            if ((current & 0xFC000000u) != 0x94000000u) return 0;
        } else if (offset == 0x18u || offset == 0x44u ||
                   offset == 0x74u) {
            if ((current & 0x9F00001Fu) !=
                (expected & 0x9F00001Fu)) return 0;
        } else if (offset == 0x68u || offset == 0x70u ||
                   offset == 0x78u) {
            if ((current & 0xFFC003FFu) !=
                (expected & 0xFFC003FFu)) return 0;
        } else if (current != expected) {
            return 0;
        }
    }
    return 1;
}

static int build_stream_ref_overlay(
        uintptr_t stream_vaddr, const unsigned char *stock,
        uintptr_t allowed_target, uintptr_t update_target,
        uintptr_t output_vaddr, const unsigned char *output_tail,
        unsigned char *patched, unsigned char *persistent_legacy,
        unsigned char *failed_idle_guard,
        unsigned char *legacy,
        unsigned char *output_patched,
        unsigned char *output_persistent_legacy,
        uintptr_t *delete_target,
        uintptr_t *stack_fail_target) {
    if (!stock || !output_tail || !patched || !persistent_legacy ||
        !failed_idle_guard || !legacy || !output_patched ||
        !output_persistent_legacy || !delete_target || !stack_fail_target ||
        !allowed_target || !update_target ||
        stream_vaddr > UINTPTR_MAX - STREAM_REF_PATCH_OFF -
                       STREAM_REF_DELETE_BL_OFF ||
        output_vaddr > UINTPTR_MAX - OUTPUT_POOL_TAIL_PATCH_OFF -
                       sizeof(OUTPUT_POOL_TAIL_STOCK_TEMPLATE) ||
        memcmp(output_tail, OUTPUT_POOL_TAIL_STOCK_TEMPLATE,
               sizeof(OUTPUT_POOL_TAIL_STOCK_TEMPLATE)) != 0 ||
        !stream_ref_stock_shape(stock)) {
        return 0;
    }
    uintptr_t delete_site = stream_vaddr + STREAM_REF_PATCH_OFF +
                            STREAM_REF_DELETE_BL_OFF;
    uintptr_t allowed_site = stream_vaddr + STREAM_REF_PATCH_OFF +
                             STREAM_REF_ALLOWED_BL_OFF;
    uintptr_t count_site = stream_vaddr + STREAM_REF_PATCH_OFF +
                           STREAM_REF_COUNT_CBNZ_OFF;
    uintptr_t stack_site = stream_vaddr + STREAM_REF_PATCH_OFF +
                           STREAM_REF_STACK_COND_OFF;
    uintptr_t legacy_stack_site = stream_vaddr + STREAM_REF_PATCH_OFF +
                                  STREAM_REF_LEGACY_STACK_COND_OFF;
    uintptr_t update_site = stream_vaddr + STREAM_REF_PATCH_OFF +
                            STREAM_REF_UPDATE_B_OFF;
    uintptr_t output_site = output_vaddr + OUTPUT_POOL_TAIL_PATCH_OFF;
    uintptr_t stack_target = 0;
    uintptr_t target = 0;
    uint32_t stack_condition = 0;
    uint32_t delete_replacement = 0;
    uint32_t allowed_replacement = 0;
    uint32_t count_replacement = 0;
    uint32_t stack_replacement = 0;
    uint32_t legacy_stack_replacement = 0;
    uint32_t update_replacement = 0;
    uint32_t output_replacement = 0;
    uint32_t output_persistent_replacement = 0;
    if (!decode_aarch64_bl(
            delete_site,
            load_u32le(stock + STREAM_REF_DELETE_BL_OFF), &target) ||
        !decode_aarch64_b_cond(
            output_site + OUTPUT_POOL_STACK_COND_OFF,
            load_u32le(output_tail + OUTPUT_POOL_STACK_COND_OFF),
            &stack_target, &stack_condition) || stack_condition != 1u ||
        stack_target != output_vaddr + OUTPUT_POOL_STACK_FAIL_OFF ||
        !encode_aarch64_bl(delete_site, target, &delete_replacement) ||
        !encode_aarch64_bl(allowed_site, allowed_target,
                           &allowed_replacement) ||
        !encode_aarch64_cbz_like(
            count_site, stream_vaddr + STREAM_UPDATE_CALL_OFF,
            load_u32le(STREAM_REF_PATCH_TEMPLATE +
                       STREAM_REF_COUNT_CBNZ_OFF),
            &count_replacement) ||
        !encode_aarch64_b_cond(stack_site, stack_target, stack_condition,
                               &stack_replacement) ||
        !encode_aarch64_b_cond(
            legacy_stack_site, stack_target, stack_condition,
            &legacy_stack_replacement) ||
        !encode_aarch64_b(update_site, update_target,
                          &update_replacement) ||
        !encode_aarch64_b(output_site,
                          stream_vaddr + STREAM_REF_HELPER_OFF,
                          &output_replacement) ||
        !encode_aarch64_b(output_site,
                          stream_vaddr + STREAM_REF_LEGACY_HELPER_OFF,
                          &output_persistent_replacement)) {
        return 0;
    }
    memcpy(patched, STREAM_REF_PATCH_TEMPLATE, STREAM_REF_PATCH_BYTES);
    store_u32le(patched + STREAM_REF_DELETE_BL_OFF, delete_replacement);
    store_u32le(patched + STREAM_REF_ALLOWED_BL_OFF, allowed_replacement);
    store_u32le(patched + STREAM_REF_COUNT_CBNZ_OFF, count_replacement);
    store_u32le(patched + STREAM_REF_STACK_COND_OFF, stack_replacement);
    store_u32le(patched + STREAM_REF_UPDATE_B_OFF, update_replacement);
    store_u32le(output_patched, output_replacement);
    store_u32le(output_persistent_legacy, output_persistent_replacement);

    memcpy(failed_idle_guard, STREAM_REF_FAILED_IDLE_GUARD_TEMPLATE,
           STREAM_REF_PATCH_BYTES);
    store_u32le(failed_idle_guard + STREAM_REF_DELETE_BL_OFF,
                delete_replacement);
    store_u32le(failed_idle_guard + STREAM_REF_ALLOWED_BL_OFF,
                allowed_replacement);
    store_u32le(failed_idle_guard + STREAM_REF_LEGACY_STACK_COND_OFF,
                legacy_stack_replacement);
    store_u32le(failed_idle_guard + STREAM_REF_UPDATE_B_OFF,
                update_replacement);
    memcpy(persistent_legacy, failed_idle_guard, STREAM_REF_PATCH_BYTES);
    store_u32le(persistent_legacy +
                STREAM_REF_LEGACY_IDLE_GUARD_CBNZ_OFF,
                0xD503201Fu);
    store_u32le(persistent_legacy +
                STREAM_REF_LEGACY_IDLE_GUARD_CLEAR_OFF,
                0xD503201Fu);
    memcpy(legacy, stock, STREAM_REF_PATCH_BYTES);
    memcpy(legacy + STREAM_REF_LEGACY_EVENT_OFF,
           STREAM_REF_LEGACY_EVENT_RECOMPUTE,
           sizeof(STREAM_REF_LEGACY_EVENT_RECOMPUTE));
    *delete_target = target;
    *stack_fail_target = stack_target;
    return 1;
}

static int shifted_local_offset(size_t expected, intptr_t delta,
                                size_t function_size, size_t span,
                                size_t *out) {
    if (!out) return 0;
    size_t value = 0;
    if (delta < 0) {
        size_t magnitude = (size_t)(-delta);
        if (expected < magnitude) return 0;
        value = expected - magnitude;
    } else {
        size_t magnitude = (size_t)delta;
        if (expected > SIZE_MAX - magnitude) return 0;
        value = expected + magnitude;
    }
    if (value > function_size || span > function_size - value) return 0;
    *out = value;
    return 1;
}

static int shifted_symbol_vaddr(const elf_a2h_symbol_t *sym,
                                intptr_t delta, uintptr_t *out) {
    if (!sym || !out) return 0;
    if (delta < 0) {
        uintptr_t magnitude = (uintptr_t)(-delta);
        if (sym->vaddr < magnitude) return 0;
        *out = sym->vaddr - magnitude;
    } else {
        uintptr_t magnitude = (uintptr_t)delta;
        if (sym->vaddr > UINTPTR_MAX - magnitude) return 0;
        *out = sym->vaddr + magnitude;
    }
    return 1;
}

static unsigned char *read_symbol_bytes(int fd,
                                        const elf_a2h_symbol_t *sym) {
    if (!sym || !sym->size || sym->size > ELF_SYMBOL_MAX_BYTES) return NULL;
    unsigned char *bytes = (unsigned char *)malloc(sym->size);
    if (!bytes || !pread_exact(fd, bytes, sym->size, sym->file_off)) {
        free(bytes);
        return NULL;
    }
    return bytes;
}

static int decode_aarch64_cbz_like(uintptr_t site, uint32_t instruction,
                                   uintptr_t *target) {
    if (!target || (instruction & 0x7E000000u) != 0x34000000u ||
        site > (uintptr_t)INT64_MAX) {
        return 0;
    }
    int64_t immediate = (int64_t)((instruction >> 5) & 0x7FFFFu);
    if (immediate & 0x40000ll) immediate -= 0x80000ll;
    int64_t resolved = (int64_t)site + immediate * 4;
    if (resolved < 0 || (uint64_t)resolved > (uint64_t)UINTPTR_MAX ||
        ((uintptr_t)resolved & 3u) != 0) {
        return 0;
    }
    *target = (uintptr_t)resolved;
    return 1;
}

static int locate_update_layout(int fd, pid_t pid, uintptr_t base,
                                const elf_a2h_symbol_t *sym,
                                intptr_t *out_delta) {
    unsigned char *bytes = read_symbol_bytes(fd, sym);
    if (!bytes || !out_delta) {
        free(bytes);
        return 0;
    }
    unsigned int hits = 0;
    intptr_t selected = 0;
    for (size_t app_off = 0;
         app_off + UPDATE_APP_POLICY_BYTES <= sym->size;
         app_off += sizeof(uint32_t)) {
        intptr_t delta = (intptr_t)app_off -
                         (intptr_t)UPDATE_APP_POLICY_PATCH_OFF;
        size_t flags_off = 0;
        size_t allowed_off = 0;
        uintptr_t semantic_vaddr = 0;
        unsigned char stock[UPDATE_APP_POLICY_BYTES];
        unsigned char relaxed[UPDATE_APP_POLICY_BYTES];
        unsigned char legacy[UPDATE_APP_POLICY_BYTES];
        uintptr_t app_target = 0;
        uintptr_t allowed_target = 0;
        if (!shifted_local_offset(UPDATE_FLAGS_PATCH_OFF, delta,
                                  sym->size, sizeof(UPDATE_FLAGS_STOCK),
                                  &flags_off) ||
            !shifted_local_offset(UPDATE_A2H_ALLOWED_BL_OFF, delta,
                                  sym->size, sizeof(uint32_t),
                                  &allowed_off) ||
            !shifted_symbol_vaddr(sym, delta, &semantic_vaddr) ||
            memcmp(bytes + flags_off, UPDATE_FLAGS_STOCK,
                   sizeof(UPDATE_FLAGS_STOCK)) != 0 ||
            !build_update_app_policy_overlay(
                semantic_vaddr, bytes + app_off, stock, relaxed, legacy,
                &app_target) ||
            !decode_aarch64_bl(sym->vaddr + allowed_off,
                               load_u32le(bytes + allowed_off),
                               &allowed_target) ||
            !exact_aarch64_plt_entry(pid, base, app_target) ||
            !exact_aarch64_plt_entry(pid, base, allowed_target)) {
            continue;
        }
        selected = delta;
        hits++;
    }
    free(bytes);
    if (hits != 1u) {
        fprintf(stderr,
                "[a2h_patch] update layout rejected: semantic_hits=%u size=%lu\n",
                hits, (unsigned long)sym->size);
        return 0;
    }
    *out_delta = selected;
    return 1;
}

static int locate_policy_layout(int fd, const elf_a2h_symbol_t *sym,
                                intptr_t *out_delta) {
    unsigned char *bytes = read_symbol_bytes(fd, sym);
    if (!bytes || !out_delta) {
        free(bytes);
        return 0;
    }
    unsigned int hits = 0;
    intptr_t selected = 0;
    for (size_t game_off = 0;
         game_off + GAME_POLICY_PATCH_BYTES <= sym->size;
         game_off += sizeof(uint32_t)) {
        if (memcmp(bytes + game_off, GAME_POLICY_STOCK,
                   GAME_POLICY_PATCH_BYTES) != 0) {
            continue;
        }
        intptr_t delta = (intptr_t)game_off -
                         (intptr_t)GAME_POLICY_PATCH_OFF;
        size_t count_load_off = 0;
        size_t count_cbz_off = 0;
        size_t manager_init_off = 0;
        size_t slot_index_init_off = 0;
        size_t slot_count_reload_off = 0;
        size_t slot_index_step_off = 0;
        size_t slot_table_load_off = 0;
        size_t slot_stream_load_off = 0;
        size_t device_begin_load_off = 0;
        size_t device_end_load_off = 0;
        size_t idle_off = 0;
        uintptr_t cbz_target = 0;
        if (!shifted_local_offset(POLICY_IDLE_COUNT_LOAD_OFF, delta,
                                  sym->size, sizeof(uint32_t) * 5u,
                                  &count_load_off) ||
            !shifted_local_offset(POLICY_IDLE_COUNT_CBZ_OFF, delta,
                                  sym->size, sizeof(uint32_t) * 2u,
                                  &count_cbz_off) ||
            !shifted_local_offset(POLICY_MANAGER_INIT_OFF, delta,
                                  sym->size, sizeof(uint32_t),
                                  &manager_init_off) ||
            !shifted_local_offset(POLICY_SLOT_INDEX_INIT_OFF, delta,
                                  sym->size, sizeof(uint32_t),
                                  &slot_index_init_off) ||
            !shifted_local_offset(POLICY_SLOT_COUNT_RELOAD_OFF, delta,
                                  sym->size, sizeof(uint32_t),
                                  &slot_count_reload_off) ||
            !shifted_local_offset(POLICY_SLOT_INDEX_STEP_OFF, delta,
                                  sym->size, sizeof(uint32_t),
                                  &slot_index_step_off) ||
            !shifted_local_offset(POLICY_SLOT_TABLE_LOAD_OFF, delta,
                                  sym->size, sizeof(uint32_t),
                                  &slot_table_load_off) ||
            !shifted_local_offset(POLICY_SLOT_STREAM_LOAD_OFF, delta,
                                  sym->size, sizeof(uint32_t),
                                  &slot_stream_load_off) ||
            !shifted_local_offset(POLICY_DEVICE_BEGIN_LOAD_OFF, delta,
                                  sym->size, sizeof(uint32_t),
                                  &device_begin_load_off) ||
            !shifted_local_offset(POLICY_DEVICE_END_LOAD_OFF, delta,
                                  sym->size, sizeof(uint32_t),
                                  &device_end_load_off) ||
            !shifted_local_offset(IDLE_CLEAR_BRANCH_PATCH_OFF, delta,
                                  sym->size,
                                  sizeof(IDLE_CLEAR_DISK_CONTEXT),
                                  &idle_off) ||
            load_u32le(bytes + count_load_off) != POLICY_IDLE_COUNT_LOAD ||
            load_u32le(bytes + count_cbz_off) != POLICY_IDLE_COUNT_CBZ ||
            load_u32le(bytes + manager_init_off) != POLICY_MANAGER_INIT ||
            load_u32le(bytes + slot_index_init_off) !=
                POLICY_SLOT_INDEX_INIT ||
            load_u32le(bytes + slot_count_reload_off) !=
                POLICY_SLOT_COUNT_RELOAD ||
            load_u32le(bytes + slot_index_step_off) !=
                POLICY_SLOT_INDEX_STEP ||
            load_u32le(bytes + slot_table_load_off) !=
                POLICY_SLOT_TABLE_LOAD ||
            load_u32le(bytes + slot_stream_load_off) !=
                POLICY_SLOT_STREAM_LOAD ||
            load_u32le(bytes + device_begin_load_off) !=
                POLICY_DEVICE_BEGIN_LOAD ||
            load_u32le(bytes + device_end_load_off) !=
                POLICY_DEVICE_END_LOAD ||
            memcmp(bytes + idle_off, IDLE_CLEAR_DISK_CONTEXT,
                   sizeof(IDLE_CLEAR_DISK_CONTEXT)) != 0 ||
            !decode_aarch64_cbz_like(
                sym->vaddr + count_cbz_off,
                load_u32le(bytes + count_cbz_off), &cbz_target) ||
            cbz_target != sym->vaddr + idle_off) {
            continue;
        }
        selected = delta;
        hits++;
    }
    free(bytes);
    if (hits != 1u) {
        fprintf(stderr,
                "[a2h_patch] policy layout rejected: semantic_hits=%u size=%lu\n",
                hits, (unsigned long)sym->size);
        return 0;
    }
    *out_delta = selected;
    return 1;
}

static int locate_stream_layout(int fd, pid_t pid, uintptr_t base,
                                const elf_a2h_symbol_t *sym,
                                intptr_t *out_delta) {
    unsigned char *bytes = read_symbol_bytes(fd, sym);
    if (!bytes || !out_delta) {
        free(bytes);
        return 0;
    }
    unsigned int hits = 0;
    intptr_t selected = 0;
    for (size_t update_call_off = 0;
         update_call_off + STREAM_UPDATE_CALL_BYTES <= sym->size;
         update_call_off += sizeof(uint32_t)) {
        if (memcmp(bytes + update_call_off, STREAM_UPDATE_CALL_TEMPLATE,
                   sizeof(uint32_t)) != 0 ||
            memcmp(bytes + update_call_off + sizeof(uint32_t) * 2u,
                   STREAM_UPDATE_CALL_TEMPLATE + sizeof(uint32_t) * 2u,
                   sizeof(uint32_t)) != 0) {
            continue;
        }
        intptr_t delta = (intptr_t)update_call_off -
                         (intptr_t)STREAM_UPDATE_CALL_OFF;
        size_t manager0_off = 0;
        size_t manager1_off = 0;
        size_t event_offsets[STREAM_EVENT_PATCH_COUNT] = {0};
        unsigned char events[STREAM_EVENT_PATCH_COUNT]
                            [STREAM_EVENT_PATCH_MAX_BYTES] = {{0}};
        uintptr_t update_target = 0;
        uintptr_t strlen_target = 0;
        int valid = shifted_local_offset(
            STREAM_APP_MAP_MANAGER_OFF, delta, sym->size,
            sizeof(uint32_t), &manager0_off) &&
            shifted_local_offset(
                STREAM_NEW_NODE_MANAGER_OFF, delta, sym->size,
                sizeof(uint32_t), &manager1_off) &&
            load_u32le(bytes + manager0_off) ==
                STREAM_APP_MAP_MANAGER_ADD &&
            load_u32le(bytes + manager1_off) ==
                STREAM_APP_MAP_MANAGER_ADD &&
            decode_aarch64_bl(
                sym->vaddr + update_call_off + sizeof(uint32_t),
                load_u32le(bytes + update_call_off + sizeof(uint32_t)),
                &update_target) &&
            exact_aarch64_plt_entry(pid, base, update_target);
        for (size_t i = 0; valid && i < STREAM_EVENT_PATCH_COUNT; ++i) {
            valid = shifted_local_offset(
                STREAM_EVENT_PATCH_OFFSETS[i], delta, sym->size,
                STREAM_EVENT_PATCH_SIZES[i], &event_offsets[i]);
            if (valid) {
                memcpy(events[i], bytes + event_offsets[i],
                       STREAM_EVENT_PATCH_SIZES[i]);
            }
        }
        valid = valid && stream_event_stock_shape(events) &&
            decode_aarch64_bl(
                sym->vaddr + event_offsets[0] + sizeof(uint32_t) * 4u,
                load_u32le(events[0] + sizeof(uint32_t) * 4u),
                &strlen_target) &&
            exact_aarch64_plt_entry(pid, base, strlen_target);
        if (!valid) continue;
        selected = delta;
        hits++;
    }
    free(bytes);
    if (hits != 1u) {
        fprintf(stderr,
                "[a2h_patch] stream layout rejected: semantic_hits=%u size=%lu\n",
                hits, (unsigned long)sym->size);
        return 0;
    }
    *out_delta = selected;
    return 1;
}

static int locate_output_layout(int fd, const elf_a2h_symbol_t *sym,
                                intptr_t *out_delta) {
    unsigned char *bytes = read_symbol_bytes(fd, sym);
    if (!bytes || !out_delta) {
        free(bytes);
        return 0;
    }
    unsigned int hits = 0;
    intptr_t selected = 0;
    for (size_t tail_off = 0;
         tail_off + sizeof(OUTPUT_POOL_TAIL_STOCK_TEMPLATE) <= sym->size;
         tail_off += sizeof(uint32_t)) {
        if (memcmp(bytes + tail_off, OUTPUT_POOL_TAIL_STOCK_TEMPLATE,
                   sizeof(OUTPUT_POOL_TAIL_STOCK_TEMPLATE)) != 0) {
            continue;
        }
        intptr_t delta = (intptr_t)tail_off -
                         (intptr_t)OUTPUT_POOL_TAIL_PATCH_OFF;
        size_t manager_off = 0;
        if (!shifted_local_offset(OUTPUT_POOL_MANAGER_ARG_OFF, delta,
                                  sym->size, sizeof(uint32_t),
                                  &manager_off) ||
            load_u32le(bytes + manager_off) != OUTPUT_POOL_MANAGER_ARG_MOV) {
            continue;
        }
        selected = delta;
        hits++;
    }
    free(bytes);
    if (hits != 1u) {
        fprintf(stderr,
                "[a2h_patch] output layout rejected: semantic_hits=%u size=%lu\n",
                hits, (unsigned long)sym->size);
        return 0;
    }
    *out_delta = selected;
    return 1;
}

static int locate_stream_open_layout(int fd, const elf_a2h_symbol_t *sym,
                                     size_t *out_patch_off) {
    if (fd < 0 || !sym || !out_patch_off ||
        sym->size < STREAM_OPEN_EPILOGUE_BYTES ||
        STREAM_OPEN_THIS_MOV_OFF > sym->size - sizeof(uint32_t)) {
        return 0;
    }
    unsigned char *bytes = (unsigned char *)malloc(sym->size);
    if (!bytes || !pread_exact(fd, bytes, sym->size, sym->file_off)) {
        free(bytes);
        return 0;
    }
    unsigned int epilogue_hits = 0;
    unsigned int manager_load_hits = 0;
    size_t selected = 0;
    for (size_t off = 0; off + sizeof(uint32_t) <= sym->size;
         off += sizeof(uint32_t)) {
        if (load_u32le(bytes + off) == STREAM_OPEN_MANAGER_LOAD) {
            manager_load_hits++;
        }
        if (off + STREAM_OPEN_EPILOGUE_BYTES <= sym->size &&
            memcmp(bytes + off, STREAM_OPEN_EPILOGUE_STOCK,
                   STREAM_OPEN_EPILOGUE_BYTES) == 0) {
            selected = off;
            epilogue_hits++;
        }
    }
    int valid =
        load_u32le(bytes + STREAM_OPEN_THIS_MOV_OFF) ==
            STREAM_OPEN_THIS_MOV &&
        epilogue_hits == 1u && manager_load_hits != 0u;
    free(bytes);
    if (!valid) {
        fprintf(stderr,
                "[a2h_patch] stream open layout rejected: epilogue_hits=%u manager_load_hits=%u size=%lu\n",
                epilogue_hits, manager_load_hits,
                (unsigned long)sym->size);
        return 0;
    }
    *out_patch_off = selected;
    fprintf(stderr,
            "[a2h_patch] stream open layout derived patch=0x%lx delta=%ld manager_load_hits=%u\n",
            (unsigned long)selected,
            (long)((intptr_t)selected -
                   (intptr_t)STREAM_OPEN_EPILOGUE_EXPECTED_OFF),
            manager_load_hits);
    return 1;
}

static int prepare_stream_ref_overlay(
        int fd, uint64_t file_size, pid_t pid, uintptr_t base,
        const char *stream_name, const char *stream_open_name,
        const char *output_name,
        const char *update_name, const char *policy_name) {
    elf_a2h_symbol_t sym = {0};
    elf_a2h_symbol_t open_sym = {0};
    elf_a2h_symbol_t output_sym = {0};
    elf_a2h_symbol_t update_sym = {0};
    elf_a2h_symbol_t policy_sym = {0};
    unsigned char output_tail[sizeof(OUTPUT_POOL_TAIL_STOCK_TEMPLATE)];
    unsigned char update_context[STREAM_UPDATE_CALL_BYTES];
    uint32_t stream_app_manager = 0;
    uint32_t output_manager_arg = 0;
    uintptr_t stream_event_strlen_target = 0;
    intptr_t stream_delta = 0;
    intptr_t output_delta = 0;
    intptr_t update_delta = 0;
    intptr_t policy_delta = 0;
    size_t stream_ref_off = 0;
    size_t stream_update_call_off = 0;
    size_t stream_manager_off = 0;
    size_t output_manager_off = 0;
    size_t output_tail_off = 0;
    size_t stream_open_patch_off = 0;
    size_t stream_event_offsets[STREAM_EVENT_PATCH_COUNT] = {0};
    uintptr_t stream_semantic_vaddr = 0;
    uintptr_t output_semantic_vaddr = 0;
    uintptr_t update_semantic_vaddr = 0;
    uintptr_t policy_semantic_vaddr = 0;
    int parsed = parse_unique_func_symbol(fd, file_size, stream_name,
                                          0,
                                          &sym);
    int output_parsed = parse_unique_func_symbol(
        fd, file_size, output_name, 0,
        &output_sym);
    int open_parsed = parse_unique_func_symbol(
        fd, file_size, stream_open_name, 0,
        &open_sym);
    int update_parsed = parse_unique_func_symbol(
        fd, file_size, update_name, 0,
        &update_sym);
    int policy_parsed = parse_unique_func_symbol(
        fd, file_size, policy_name, 0,
        &policy_sym);
    if (parsed != ELF_RESOLVE_VERIFIED ||
        open_parsed != ELF_RESOLVE_VERIFIED ||
        output_parsed != ELF_RESOLVE_VERIFIED ||
        update_parsed != ELF_RESOLVE_VERIFIED ||
        policy_parsed != ELF_RESOLVE_VERIFIED ||
        !locate_stream_layout(fd, pid, base, &sym, &stream_delta) ||
        !locate_stream_open_layout(fd, &open_sym,
                                   &stream_open_patch_off) ||
        !locate_output_layout(fd, &output_sym, &output_delta) ||
        !locate_update_layout(fd, pid, base, &update_sym,
                              &update_delta) ||
        !locate_policy_layout(fd, &policy_sym, &policy_delta) ||
        update_sym.vaddr != g_auxiliary.update_off ||
        policy_sym.vaddr != g_auxiliary.policy_off ||
        !g_auxiliary.concurrent_helper_off ||
        !shifted_local_offset(STREAM_REF_PATCH_OFF, stream_delta,
                              sym.size, STREAM_REF_PATCH_BYTES,
                              &stream_ref_off) ||
        !shifted_local_offset(STREAM_UPDATE_CALL_OFF, stream_delta,
                              sym.size, sizeof(update_context),
                              &stream_update_call_off) ||
        !shifted_local_offset(STREAM_APP_MAP_MANAGER_OFF, stream_delta,
                              sym.size, sizeof(stream_app_manager),
                              &stream_manager_off) ||
        !shifted_local_offset(OUTPUT_POOL_MANAGER_ARG_OFF, output_delta,
                              output_sym.size, sizeof(output_manager_arg),
                              &output_manager_off) ||
        !shifted_local_offset(OUTPUT_POOL_TAIL_PATCH_OFF, output_delta,
                              output_sym.size, sizeof(output_tail),
                              &output_tail_off) ||
        !shifted_symbol_vaddr(&sym, stream_delta,
                              &stream_semantic_vaddr) ||
        !shifted_symbol_vaddr(&output_sym, output_delta,
                              &output_semantic_vaddr) ||
        !shifted_symbol_vaddr(&update_sym, update_delta,
                              &update_semantic_vaddr) ||
        !shifted_symbol_vaddr(&policy_sym, policy_delta,
                              &policy_semantic_vaddr) ||
        update_semantic_vaddr + UPDATE_FLAGS_PATCH_OFF !=
            update_sym.vaddr + g_auxiliary.update_flags_patch_off ||
        policy_semantic_vaddr + POLICY_IDLE_COUNT_CBZ_OFF !=
            policy_sym.vaddr + g_auxiliary.policy_idle_count_cbz_off ||
        !pread_exact(fd, g_auxiliary.stream_ref_stock,
                     STREAM_REF_PATCH_BYTES,
                     sym.file_off + stream_ref_off) ||
        !pread_exact(fd, update_context, sizeof(update_context),
                     sym.file_off + stream_update_call_off) ||
        memcmp(update_context, STREAM_UPDATE_CALL_TEMPLATE,
               sizeof(uint32_t)) != 0 ||
        memcmp(update_context + sizeof(uint32_t) * 2u,
               STREAM_UPDATE_CALL_TEMPLATE + sizeof(uint32_t) * 2u,
               sizeof(uint32_t)) != 0 ||
        !decode_aarch64_bl(sym.vaddr + stream_update_call_off +
                           sizeof(uint32_t),
                           load_u32le(update_context + sizeof(uint32_t)),
                           &g_auxiliary.stream_ref_update_target) ||
        !pread_exact(fd, &stream_app_manager, sizeof(stream_app_manager),
                     sym.file_off + stream_manager_off) ||
        stream_app_manager != STREAM_APP_MAP_MANAGER_ADD ||
        !pread_exact(fd, &output_manager_arg, sizeof(output_manager_arg),
                     output_sym.file_off + output_manager_off) ||
        output_manager_arg != OUTPUT_POOL_MANAGER_ARG_MOV ||
        !pread_exact(fd, output_tail, sizeof(output_tail),
                     output_sym.file_off + output_tail_off)) {
        fprintf(stderr,
                "[a2h_patch] stream handoff overlay rejected: symbol/layout mismatch\n");
        return 0;
    }
    if (!pread_exact(fd, g_auxiliary.stream_open_stock,
                     STREAM_OPEN_EVENT_BYTES,
                     open_sym.file_off + stream_open_patch_off) ||
        memcmp(g_auxiliary.stream_open_stock,
               STREAM_OPEN_EPILOGUE_STOCK,
               STREAM_OPEN_EVENT_BYTES) != 0) {
        fprintf(stderr,
                "[a2h_patch] stream open overlay rejected: event bytes mismatch\n");
        return 0;
    }
    for (size_t i = 0; i < STREAM_EVENT_PATCH_COUNT; ++i) {
        if (!shifted_local_offset(
                STREAM_EVENT_PATCH_OFFSETS[i], stream_delta, sym.size,
                STREAM_EVENT_PATCH_SIZES[i], &stream_event_offsets[i]) ||
            !pread_exact(fd, g_auxiliary.stream_event_stock[i],
                         STREAM_EVENT_PATCH_SIZES[i],
                         sym.file_off + stream_event_offsets[i])) {
            fprintf(stderr,
                    "[a2h_patch] stream handoff overlay rejected: event layout mismatch\n");
            return 0;
        }
    }
    if (!decode_aarch64_bl(
            sym.vaddr + stream_event_offsets[0] +
                sizeof(uint32_t) * 4u,
            load_u32le(g_auxiliary.stream_event_stock[0] +
                       sizeof(uint32_t) * 4u),
            &stream_event_strlen_target) ||
         !build_stream_ref_overlay(
            stream_semantic_vaddr, g_auxiliary.stream_ref_stock,
            g_auxiliary.allowed_call_target,
            g_auxiliary.stream_ref_update_target,
            output_semantic_vaddr, output_tail,
            g_auxiliary.stream_ref_patched,
            g_auxiliary.stream_ref_persistent_legacy,
            g_auxiliary.stream_ref_failed_idle_guard,
            g_auxiliary.stream_ref_legacy,
            g_auxiliary.output_pool_tail_patched,
            g_auxiliary.output_pool_tail_persistent_legacy,
            &g_auxiliary.stream_ref_delete_target,
            &g_auxiliary.output_pool_stack_fail_target) ||
        !build_concurrent_helpers(
            g_auxiliary.concurrent_helper_off, policy_semantic_vaddr,
            stream_semantic_vaddr,
            open_sym.vaddr, stream_open_patch_off,
            sym.vaddr + stream_update_call_off,
            g_auxiliary.stream_ref_update_target,
             g_auxiliary.stream_event_stock,
             g_auxiliary.concurrent_helper_patched,
             g_auxiliary.concurrent_helper_previous,
             g_auxiliary.concurrent_helper_previous2,
             g_auxiliary.concurrent_helper_legacy,
            g_auxiliary.concurrent_helper_legacy2,
            g_auxiliary.policy_concurrent,
            g_auxiliary.stream_event_patched,
            g_auxiliary.stream_event_recompute_legacy,
            g_auxiliary.stream_event_handoff_legacy,
            g_auxiliary.stream_open_patched) ||
         !exact_aarch64_plt_entry(pid, base,
                                  g_auxiliary.stream_ref_delete_target) ||
         !exact_aarch64_plt_entry(pid, base,
                                  g_auxiliary.stream_ref_update_target) ||
        !exact_aarch64_plt_entry(pid, base,
                                 stream_event_strlen_target)) {
        fprintf(stderr,
                "[a2h_patch] stream handoff overlay rejected: stock shape/branch/event-BL/PLT mismatch\n");
        return 0;
    }
    g_auxiliary.stream_event_strlen_target = stream_event_strlen_target;
    g_auxiliary.stream_open_off = open_sym.vaddr;
    g_auxiliary.stream_open_patch_off = stream_open_patch_off;
    g_auxiliary.output_pool_off = output_sym.vaddr;
    g_auxiliary.stream_ref_patch_off = stream_ref_off;
    memcpy(g_auxiliary.stream_event_patch_offsets, stream_event_offsets,
           sizeof(stream_event_offsets));
    g_auxiliary.stream_update_call_off = stream_update_call_off;
    g_auxiliary.output_pool_tail_patch_off = output_tail_off;
    fprintf(stderr,
            "[a2h_patch] stream handoff overlay generated bytes=%u stream_delta=%ld output_delta=%ld open=0x%lx+0x%lx delete_target=0x%lx update_target=0x%lx event_strlen_target=0x%lx stack_fail=0x%lx output_pool=0x%lx\n",
            STREAM_REF_PATCH_BYTES,
            (long)stream_delta, (long)output_delta,
            (unsigned long)g_auxiliary.stream_open_off,
            (unsigned long)g_auxiliary.stream_open_patch_off,
            (unsigned long)g_auxiliary.stream_ref_delete_target,
            (unsigned long)g_auxiliary.stream_ref_update_target,
            (unsigned long)g_auxiliary.stream_event_strlen_target,
            (unsigned long)g_auxiliary.output_pool_stack_fail_target,
            (unsigned long)g_auxiliary.output_pool_off);
    return 1;
}

static int prepare_update_app_policy_overlay(
        int fd, uint64_t file_size, pid_t pid, uintptr_t base,
        const char *update_name) {
    elf_a2h_symbol_t sym = {0};
    uint32_t allowed_call = 0;
    intptr_t layout_delta = 0;
    size_t flags_off = 0;
    size_t app_policy_off = 0;
    size_t allowed_bl_off = 0;
    uintptr_t semantic_vaddr = 0;
    int parsed = parse_unique_func_symbol(fd, file_size, update_name,
                                          0, &sym);
    if (parsed != ELF_RESOLVE_VERIFIED ||
        !locate_update_layout(fd, pid, base, &sym, &layout_delta) ||
        !shifted_local_offset(UPDATE_FLAGS_PATCH_OFF, layout_delta,
                              sym.size, sizeof(UPDATE_FLAGS_STOCK),
                              &flags_off) ||
        !shifted_local_offset(UPDATE_APP_POLICY_PATCH_OFF, layout_delta,
                              sym.size, UPDATE_APP_POLICY_BYTES,
                              &app_policy_off) ||
        !shifted_local_offset(UPDATE_A2H_ALLOWED_BL_OFF, layout_delta,
                              sym.size, sizeof(allowed_call),
                              &allowed_bl_off) ||
        !shifted_symbol_vaddr(&sym, layout_delta, &semantic_vaddr) ||
        !pread_exact(fd, g_auxiliary.app_policy_disk,
                     UPDATE_APP_POLICY_BYTES,
                     sym.file_off + app_policy_off) ||
        !pread_exact(fd, &allowed_call, sizeof(allowed_call),
                     sym.file_off + allowed_bl_off) ||
        !decode_aarch64_bl(sym.vaddr + allowed_bl_off,
                           allowed_call,
                           &g_auxiliary.allowed_call_target) ||
        !build_update_app_policy_overlay(
            semantic_vaddr, g_auxiliary.app_policy_disk,
            g_auxiliary.app_policy_stock,
            g_auxiliary.app_policy_relaxed,
            g_auxiliary.app_policy_legacy,
            &g_auxiliary.app_policy_call_target) ||
        !exact_aarch64_plt_entry(pid, base,
                                 g_auxiliary.app_policy_call_target) ||
        !exact_aarch64_plt_entry(pid, base,
                                 g_auxiliary.allowed_call_target)) {
        fprintf(stderr,
                "[a2h_patch] auxiliary app policy rejected: stock shape/BL/PLT mismatch\n");
        return 0;
    }
    fprintf(stderr,
            "[a2h_patch] auxiliary app policy generated bytes=%u delta=%ld call_target=0x%lx allowed_target=0x%lx\n",
            UPDATE_APP_POLICY_BYTES,
            (long)layout_delta,
            (unsigned long)g_auxiliary.app_policy_call_target,
            (unsigned long)g_auxiliary.allowed_call_target);
    g_auxiliary.update_off = sym.vaddr;
    g_auxiliary.update_flags_patch_off = flags_off;
    g_auxiliary.update_app_policy_patch_off = app_policy_off;
    return 1;
}

static int prepare_idle_clear_overlay(
        int fd, uint64_t file_size, pid_t pid, uintptr_t base,
        const char *update_name,
        const char *policy_name) {
    elf_a2h_symbol_t update_sym = {0};
    elf_a2h_symbol_t policy_sym = {0};
    uint32_t count_load = 0;
    uint32_t count_cbz = 0;
    unsigned char idle_context[sizeof(IDLE_CLEAR_DISK_CONTEXT)];
    intptr_t update_delta = 0;
    intptr_t policy_delta = 0;
    size_t update_flags_off = 0;
    size_t count_load_off = 0;
    size_t count_cbz_off = 0;
    size_t idle_clear_off = 0;
    size_t game_policy_off = 0;
    uintptr_t update_semantic_vaddr = 0;
    uintptr_t policy_semantic_vaddr = 0;
    int update_parsed = parse_unique_func_symbol(
        fd, file_size, update_name, 0,
        &update_sym);
    int policy_parsed = parse_unique_func_symbol(
        fd, file_size, policy_name, 0,
        &policy_sym);
    if (update_parsed != ELF_RESOLVE_VERIFIED ||
        policy_parsed != ELF_RESOLVE_VERIFIED ||
        !locate_update_layout(fd, pid, base, &update_sym,
                              &update_delta) ||
        !locate_policy_layout(fd, &policy_sym, &policy_delta) ||
        !shifted_local_offset(UPDATE_FLAGS_PATCH_OFF, update_delta,
                              update_sym.size, sizeof(UPDATE_FLAGS_STOCK),
                              &update_flags_off) ||
        update_flags_off != g_auxiliary.update_flags_patch_off ||
        !shifted_local_offset(POLICY_IDLE_COUNT_LOAD_OFF, policy_delta,
                              policy_sym.size, sizeof(count_load),
                              &count_load_off) ||
        !shifted_local_offset(POLICY_IDLE_COUNT_CBZ_OFF, policy_delta,
                              policy_sym.size, sizeof(count_cbz),
                              &count_cbz_off) ||
        !shifted_local_offset(IDLE_CLEAR_BRANCH_PATCH_OFF, policy_delta,
                              policy_sym.size, sizeof(idle_context),
                              &idle_clear_off) ||
        !shifted_local_offset(GAME_POLICY_PATCH_OFF, policy_delta,
                              policy_sym.size, GAME_POLICY_PATCH_BYTES,
                              &game_policy_off) ||
        !shifted_symbol_vaddr(&update_sym, update_delta,
                              &update_semantic_vaddr) ||
        !shifted_symbol_vaddr(&policy_sym, policy_delta,
                              &policy_semantic_vaddr) ||
        !pread_exact(fd, &count_load, sizeof(count_load),
                     policy_sym.file_off + count_load_off) ||
        !pread_exact(fd, &count_cbz, sizeof(count_cbz),
                     policy_sym.file_off + count_cbz_off) ||
        !pread_exact(fd, idle_context, sizeof(idle_context),
                     policy_sym.file_off + idle_clear_off) ||
        count_load != POLICY_IDLE_COUNT_LOAD ||
        count_cbz != POLICY_IDLE_COUNT_CBZ ||
        memcmp(idle_context, IDLE_CLEAR_DISK_CONTEXT,
               sizeof(idle_context)) != 0 ||
        !build_idle_clear_overlay(
            update_semantic_vaddr, policy_semantic_vaddr,
            g_auxiliary.concurrent_helper_off,
            g_auxiliary.update_flags_patched,
            g_auxiliary.update_flags_guarded_legacy,
            g_auxiliary.idle_count_branch,
            g_auxiliary.idle_clear_branch)) {
        fprintf(stderr,
                "[a2h_patch] idle clear overlay rejected: zero-output control flow mismatch\n");
        return 0;
    }
    fprintf(stderr,
            "[a2h_patch] idle clear overlay generated update_delta=%ld policy_delta=%ld helper=0x%lx branch=0x%lx resume=0x%lx bytes=%u/%u\n",
            (long)update_delta, (long)policy_delta,
            (unsigned long)(update_semantic_vaddr +
                            UPDATE_IDLE_HELPER_OFF),
            (unsigned long)(policy_sym.vaddr + idle_clear_off),
            (unsigned long)(policy_semantic_vaddr + IDLE_CLEAR_RESUME_OFF),
            (unsigned int)sizeof(UPDATE_IDLE_HELPER_TEMPLATE),
            IDLE_CLEAR_BRANCH_BYTES);
    if (update_sym.vaddr != g_auxiliary.update_off) return 0;
    g_auxiliary.policy_off = policy_sym.vaddr;
    g_auxiliary.policy_idle_count_cbz_off = count_cbz_off;
    g_auxiliary.policy_idle_clear_off = idle_clear_off;
    g_auxiliary.policy_game_off = game_policy_off;
    return 1;
}

static int exact_multi_overlay(const unsigned char *disk,
                               const unsigned char *live,
                               size_t func_size,
                               const owned_overlay_region_t *regions,
                               size_t region_count, int *states) {
    if (!disk || !live || !regions || !region_count || !states) return 0;
    size_t cursor = 0;
    for (size_t i = 0; i < region_count; ++i) {
        const owned_overlay_region_t *region = &regions[i];
        if (!region->stock || !region->patched || !region->patch_size ||
            region->patch_off < cursor || region->patch_off > func_size ||
            region->patch_size > func_size - region->patch_off ||
            memcmp(disk + cursor, live + cursor,
                   region->patch_off - cursor) != 0 ||
            memcmp(disk + region->patch_off, region->stock,
                   region->patch_size) != 0) {
            return 0;
        }
        const unsigned char *live_region = live + region->patch_off;
        if (memcmp(live_region, region->stock, region->patch_size) == 0) {
            states[i] = OVERLAY_STOCK;
        } else if (memcmp(live_region, region->patched,
                          region->patch_size) == 0) {
            states[i] = OVERLAY_PATCHED;
        } else if (region->alternate &&
                   memcmp(live_region, region->alternate,
                          region->patch_size) == 0) {
            states[i] = OVERLAY_ALTERNATE;
        } else if (region->alternate2 &&
                   memcmp(live_region, region->alternate2,
                          region->patch_size) == 0) {
            states[i] = OVERLAY_ALTERNATE2;
        } else if (region->legacy &&
                   memcmp(live_region, region->legacy,
                          region->patch_size) == 0) {
            states[i] = OVERLAY_LEGACY;
        } else {
            return 0;
        }
        cursor = region->patch_off + region->patch_size;
    }
    return memcmp(disk + cursor, live + cursor, func_size - cursor) == 0;
}

static int coherent_concurrent_generation(
        const int update_states[2], int helper_state,
        const int policy_states[POLICY_OVERLAY_REGION_COUNT],
        const int stream_states[STREAM_OVERLAY_REGION_COUNT],
        int stream_open_state) {
    if (!update_states || !policy_states || !stream_states) return 0;
    int policy_owned =
        policy_states[POLICY_REGION_GAME] == OVERLAY_PATCHED ||
        policy_states[POLICY_REGION_GAME] == OVERLAY_ALTERNATE;
    int final = update_states[0] == OVERLAY_PATCHED &&
        helper_state == OVERLAY_PATCHED &&
        stream_open_state == OVERLAY_PATCHED &&
        policy_states[POLICY_REGION_IDLE_COUNT] == OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT0] == OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT1] == OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT2] == OVERLAY_PATCHED &&
        policy_owned;
    int relocated_64 = update_states[0] == OVERLAY_PATCHED &&
        helper_state == OVERLAY_ALTERNATE &&
        stream_open_state == OVERLAY_STOCK &&
        policy_states[POLICY_REGION_IDLE_COUNT] == OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT0] == OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT1] == OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT2] == OVERLAY_PATCHED &&
        policy_owned;
    int previous_176 = update_states[0] == OVERLAY_PATCHED &&
        helper_state == OVERLAY_LEGACY &&
        stream_open_state == OVERLAY_STOCK &&
        policy_states[POLICY_REGION_IDLE_COUNT] == OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT0] == OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT1] == OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT2] == OVERLAY_PATCHED &&
        policy_owned;
    int previous_160 = update_states[0] == OVERLAY_PATCHED &&
        helper_state == OVERLAY_ALTERNATE3 &&
        stream_open_state == OVERLAY_STOCK &&
        policy_states[POLICY_REGION_IDLE_COUNT] == OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT0] == OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT1] == OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT2] == OVERLAY_PATCHED &&
        policy_owned;
    int relocated_56 = update_states[0] == OVERLAY_PATCHED &&
        helper_state == OVERLAY_ALTERNATE2 &&
        stream_open_state == OVERLAY_STOCK &&
        policy_states[POLICY_REGION_IDLE_COUNT] == OVERLAY_STOCK &&
        stream_states[STREAM_REGION_EVENT0] == OVERLAY_PATCHED &&
        (stream_states[STREAM_REGION_EVENT1] == OVERLAY_STOCK ||
         stream_states[STREAM_REGION_EVENT1] == OVERLAY_PATCHED) &&
        stream_states[STREAM_REGION_EVENT2] == OVERLAY_PATCHED &&
        policy_owned;
    int historical = update_states[0] != OVERLAY_PATCHED &&
        helper_state == OVERLAY_STOCK &&
        stream_open_state == OVERLAY_STOCK &&
        policy_states[POLICY_REGION_IDLE_COUNT] == OVERLAY_STOCK &&
        stream_states[STREAM_REGION_EVENT0] != OVERLAY_PATCHED &&
        stream_states[STREAM_REGION_EVENT1] == OVERLAY_STOCK &&
        (stream_states[STREAM_REGION_EVENT2] == OVERLAY_STOCK ||
         stream_states[STREAM_REGION_EVENT2] == OVERLAY_PATCHED) &&
        (policy_states[POLICY_REGION_GAME] == OVERLAY_STOCK ||
         policy_states[POLICY_REGION_GAME] == OVERLAY_LEGACY);
    return final || previous_176 || previous_160 || relocated_64 || relocated_56 ||
           historical;
}

static int resolve_owned_multi_symbol(int fd, uint64_t file_size,
                                      pid_t pid, uintptr_t base,
                                      const char *name,
                                      size_t expected_size,
                                      const owned_overlay_region_t *regions,
                                      size_t region_count,
                                      uintptr_t *out_off, int *states) {
    elf_a2h_symbol_t sym={0};
    int parsed = parse_unique_func_symbol(fd, file_size, name,
                                          expected_size, &sym);
    if (parsed != ELF_RESOLVE_VERIFIED || base > UINTPTR_MAX - sym.vaddr ||
        sym.size > UINTPTR_MAX - (base + sym.vaddr)) {
        fprintf(stderr,
                "[a2h_patch] auxiliary symbol rejected name=%s parse=%d\n",
                name, parsed);
        return 0;
    }
    uintptr_t absolute = base + sym.vaddr;
    if (absolute < g_rx_start || absolute + sym.size > g_rx_end) {
        fprintf(stderr,
                "[a2h_patch] auxiliary symbol rejected name=%s reason=RX-range\n",
                name);
        return 0;
    }
    unsigned char *disk = (unsigned char *)malloc(sym.size);
    unsigned char *live = (unsigned char *)malloc(sym.size);
    int owned = disk && live &&
                pread_exact(fd, disk, sym.size, sym.file_off) &&
                mem_r(pid, absolute, live, sym.size) == 0 &&
                exact_multi_overlay(disk, live, sym.size, regions,
                                    region_count, states);
    free(disk);
    free(live);
    if (!owned) {
        fprintf(stderr,
                "[a2h_patch] auxiliary symbol rejected name=%s reason=foreign-or-unknown-bytes\n",
                name);
        return 0;
    }
    *out_off = sym.vaddr;
    fprintf(stderr,
            "[a2h_patch] auxiliary symbol verified name=%s func=0x%lx size=%lu regions=%lu\n",
            name, (unsigned long)sym.vaddr, (unsigned long)sym.size,
            (unsigned long)region_count);
    return 1;
}

static int resolve_auxiliary_targets(pid_t pid, uintptr_t base) {
    static const char update_name[] =
        "_ZN7android22AudioALSAStreamManager13updateA2HModeEv";
    static const char policy_name[] =
        "_ZN7android22AudioALSAStreamManager12isA2HAllowedEv";
    static const char stream_event_name[] =
        "_ZN7android18AudioALSAStreamOut13setParametersERKNS_7String8E";
    static const char stream_open_name[] =
        "_ZN7android18AudioALSAStreamOut4openEv";
    static const char output_pool_name[] =
        "_ZN7android22AudioALSAStreamManager22updateOutputPoolActiveE20audio_output_flags_tb";
    memset(&g_auxiliary, 0, sizeof(g_auxiliary));
    uint64_t file_size = 0;
    int fd = open_mapped_hal(pid, &file_size);
    if (fd < 0) return 0;
    if (!prepare_update_app_policy_overlay(fd, file_size, pid, base,
                                           update_name) ||
        !prepare_executable_concurrent_helper(fd, file_size, pid, base) ||
        !prepare_idle_clear_overlay(fd, file_size, pid, base, update_name,
                                    policy_name) ||
        !prepare_stream_ref_overlay(fd, file_size, pid, base,
                                    stream_event_name,
                                    stream_open_name,
                                    output_pool_name, update_name,
                                    policy_name)) {
        close(fd);
        memset(&g_auxiliary, 0, sizeof(g_auxiliary));
        return 0;
    }
    const owned_overlay_region_t update_regions[] = {
        {g_auxiliary.update_flags_patch_off, UPDATE_FLAGS_STOCK,
         g_auxiliary.update_flags_patched,
         UPDATE_FLAGS_GAME_HANDOFF_LEGACY,
         g_auxiliary.update_flags_guarded_legacy, UPDATE_FLAGS_LEGACY,
         sizeof(UPDATE_FLAGS_STOCK)},
        {g_auxiliary.update_app_policy_patch_off,
         g_auxiliary.app_policy_disk,
         g_auxiliary.app_policy_stock, g_auxiliary.app_policy_relaxed,
         NULL, g_auxiliary.app_policy_legacy, UPDATE_APP_POLICY_BYTES}
    };
    const owned_overlay_region_t stream_regions[STREAM_OVERLAY_REGION_COUNT] = {
        {g_auxiliary.stream_event_patch_offsets[0],
         g_auxiliary.stream_event_stock[0],
         g_auxiliary.stream_event_patched[0],
         g_auxiliary.stream_event_recompute_legacy[0], NULL,
         g_auxiliary.stream_event_handoff_legacy,
         STREAM_EVENT_PATCH_SIZES[0]},
        {g_auxiliary.stream_ref_patch_off, g_auxiliary.stream_ref_stock,
         g_auxiliary.stream_ref_patched,
         g_auxiliary.stream_ref_persistent_legacy,
         g_auxiliary.stream_ref_failed_idle_guard,
         g_auxiliary.stream_ref_legacy,
         STREAM_REF_PATCH_BYTES},
        {g_auxiliary.stream_event_patch_offsets[1],
         g_auxiliary.stream_event_stock[1],
         g_auxiliary.stream_event_patched[1],
         g_auxiliary.stream_event_recompute_legacy[1], NULL, NULL,
         STREAM_EVENT_PATCH_SIZES[1]},
        {g_auxiliary.stream_event_patch_offsets[2],
         g_auxiliary.stream_event_stock[2],
         g_auxiliary.stream_event_patched[2],
         g_auxiliary.stream_event_recompute_legacy[2], NULL, NULL,
         STREAM_EVENT_PATCH_SIZES[2]}
    };
    const owned_overlay_region_t stream_open_region = {
        g_auxiliary.stream_open_patch_off,
        g_auxiliary.stream_open_stock,
        g_auxiliary.stream_open_patched,
        NULL, NULL, NULL, STREAM_OPEN_EVENT_BYTES
    };
    int update_states[2]={0};
    int stream_states[STREAM_OVERLAY_REGION_COUNT]={0};
    int update_ok = resolve_owned_multi_symbol(
        fd, file_size, pid, base, update_name,
        0, update_regions,
        sizeof(update_regions) / sizeof(update_regions[0]),
        &g_auxiliary.update_off, update_states);
    int stream_ok = update_ok && resolve_owned_multi_symbol(
        fd, file_size, pid, base, stream_event_name,
        0, stream_regions,
        STREAM_OVERLAY_REGION_COUNT, &g_auxiliary.stream_event_off,
        stream_states);
    int stream_open_states[1] = {0};
    int stream_open_ok = stream_ok && resolve_owned_multi_symbol(
        fd, file_size, pid, base, stream_open_name, 0,
        &stream_open_region, 1, &g_auxiliary.stream_open_off,
        stream_open_states);
    const owned_overlay_region_t policy_regions[] = {
        {g_auxiliary.policy_idle_count_cbz_off,
         POLICY_IDLE_COUNT_CBZ_STOCK,
         g_auxiliary.idle_count_branch, NULL, NULL, NULL,
         sizeof(POLICY_IDLE_COUNT_CBZ_STOCK)},
        {g_auxiliary.policy_idle_clear_off, IDLE_CLEAR_BRANCH_STOCK,
         g_auxiliary.idle_clear_branch, NULL, NULL, NULL,
         IDLE_CLEAR_BRANCH_BYTES},
        {g_auxiliary.policy_game_off, GAME_POLICY_STOCK,
         g_auxiliary.policy_concurrent, GAME_POLICY_RELAXED,
         NULL, GAME_POLICY_PUBLIC_RELAXED, GAME_POLICY_PATCH_BYTES}
    };
    int policy_states[POLICY_OVERLAY_REGION_COUNT]={0};
    int policy_ok = stream_open_ok && resolve_owned_multi_symbol(
        fd, file_size, pid, base, policy_name,
        0, policy_regions,
        sizeof(policy_regions) / sizeof(policy_regions[0]),
        &g_auxiliary.policy_off, policy_states);
    const owned_overlay_region_t output_pool_region = {
        g_auxiliary.output_pool_tail_patch_off, OUTPUT_POOL_TAIL_STOCK,
        g_auxiliary.output_pool_tail_patched,
        g_auxiliary.output_pool_tail_persistent_legacy,
        NULL, NULL, OUTPUT_POOL_TAIL_PATCH_BYTES
    };
    int output_pool_states[1]={0};
    uintptr_t output_pool_off = 0;
    int output_pool_ok = policy_ok && resolve_owned_multi_symbol(
        fd, file_size, pid, base, output_pool_name,
        0, &output_pool_region, 1,
        &output_pool_off, output_pool_states) &&
        output_pool_off == g_auxiliary.output_pool_off;
    int concurrent_helper_ok = output_pool_ok &&
        resolve_owned_executable_helper(pid, base);
    close(fd);
    g_auxiliary.update_flags_state = update_states[0];
    g_auxiliary.update_app_policy_state = update_states[1];
    g_auxiliary.idle_count_state =
        policy_states[POLICY_REGION_IDLE_COUNT];
    g_auxiliary.idle_clear_state =
        policy_states[POLICY_REGION_IDLE_CLEAR];
    g_auxiliary.policy_relaxed =
        policy_states[POLICY_REGION_GAME] == OVERLAY_ALTERNATE ||
        policy_states[POLICY_REGION_GAME] == OVERLAY_LEGACY;
    g_auxiliary.stream_ref_state = stream_states[1];
    g_auxiliary.stream_open_state = stream_open_states[0];
    g_auxiliary.output_pool_state = output_pool_states[0];
    static const size_t stream_event_region_indices
            [STREAM_EVENT_PATCH_COUNT] = {
        STREAM_REGION_EVENT0, STREAM_REGION_EVENT1, STREAM_REGION_EVENT2
    };
    for (size_t i = 0; i < STREAM_EVENT_PATCH_COUNT; ++i) {
        if (stream_states[stream_event_region_indices[i]] == OVERLAY_PATCHED)
            g_auxiliary.stream_events_patched++;
    }
    int handoff_owned = stream_states[1] == OVERLAY_PATCHED ||
                        stream_states[1] == OVERLAY_ALTERNATE ||
                        stream_states[1] == OVERLAY_ALTERNATE2;
    int output_owned = g_auxiliary.output_pool_state == OVERLAY_PATCHED ||
                       g_auxiliary.output_pool_state == OVERLAY_ALTERNATE;
    int app_policy_new = update_states[1] == OVERLAY_PATCHED ||
                         update_states[1] == OVERLAY_ALTERNATE;
    int app_policy_old = update_states[1] == OVERLAY_STOCK ||
                          update_states[1] == OVERLAY_LEGACY;
    int idle_helper_owned = update_states[0] == OVERLAY_PATCHED ||
                            update_states[0] == OVERLAY_ALTERNATE2;
    int idle_branch_owned =
        policy_states[POLICY_REGION_IDLE_CLEAR] == OVERLAY_PATCHED;
    int concurrent_generation = coherent_concurrent_generation(
        update_states, g_auxiliary.concurrent_helper_state,
        policy_states, stream_states, g_auxiliary.stream_open_state);
    int ownership_coherent = handoff_owned == output_owned &&
        idle_helper_owned == idle_branch_owned &&
        concurrent_generation &&
        ((handoff_owned && app_policy_new) ||
         (!handoff_owned && app_policy_old));
    g_auxiliary.valid = update_ok && stream_ok && stream_open_ok && policy_ok &&
                        output_pool_ok && concurrent_helper_ok &&
                        ownership_coherent;
    if (!g_auxiliary.valid) {
        memset(&g_auxiliary, 0, sizeof(g_auxiliary));
        fprintf(stderr,
                "[a2h_patch] ERROR: auxiliary lifecycle targets unresolved; no patch data written\n");
        return 0;
    }
    fprintf(stderr,
            "[a2h_patch] auxiliary ownership output_flags=%s idle_init=%s idle_clear=%s concurrent_slot=%s@0x%lx app_policy=%s stream_handoff=%s output_pool=%s stream_events=%u/%u stream_open=%s output_policy=%s\n",
            g_auxiliary.update_flags_state == OVERLAY_LEGACY ?
                "game-lowbyte-v1.5.6" :
            (g_auxiliary.update_flags_state == OVERLAY_ALTERNATE2 ?
                "game+spatial+guarded-idle-v1" :
            (g_auxiliary.update_flags_state == OVERLAY_ALTERNATE ?
                "game+spatial-v1.5.6" :
            (g_auxiliary.update_flags_state == OVERLAY_PATCHED ?
                "game+spatial+idle-helper" : "stock"))),
            g_auxiliary.idle_count_state == OVERLAY_PATCHED ?
                "manager-x20" : "stock-skip",
            g_auxiliary.idle_clear_state == OVERLAY_PATCHED ?
                "zero-output" : "stock",
            g_auxiliary.concurrent_helper_state == OVERLAY_PATCHED ?
                "owned" :
            (g_auxiliary.concurrent_helper_state == OVERLAY_LEGACY ?
                "previous-176" :
            (g_auxiliary.concurrent_helper_state == OVERLAY_ALTERNATE3 ?
                "previous-160" :
            (g_auxiliary.concurrent_helper_state == OVERLAY_ALTERNATE ?
                "legacy-64" :
            (g_auxiliary.concurrent_helper_state == OVERLAY_ALTERNATE2 ?
                "legacy-56" : "stock-zero")))),
            (unsigned long)g_auxiliary.concurrent_helper_off,
            g_auxiliary.update_app_policy_state == OVERLAY_ALTERNATE ?
                "relaxed-handoff" :
            (g_auxiliary.update_app_policy_state == OVERLAY_PATCHED ?
                "stock-handoff" :
            (g_auxiliary.update_app_policy_state == OVERLAY_LEGACY ?
                "legacy-relaxed" : "stock")),
            g_auxiliary.stream_ref_state == OVERLAY_PATCHED ? "handoff" :
            g_auxiliary.stream_ref_state == OVERLAY_ALTERNATE ?
                "handoff-persistent-v1.5.6" :
            g_auxiliary.stream_ref_state == OVERLAY_ALTERNATE2 ?
                "handoff-failed-idle-guard" :
            (g_auxiliary.stream_ref_state == OVERLAY_LEGACY ?
             "legacy-event" : "stock"),
            g_auxiliary.output_pool_state == OVERLAY_PATCHED ? "event-tail" :
            (g_auxiliary.output_pool_state == OVERLAY_ALTERNATE ?
                "event-tail-v1.5.6" : "stock"),
            g_auxiliary.stream_events_patched, STREAM_EVENT_PATCH_COUNT,
            g_auxiliary.stream_open_state == OVERLAY_PATCHED ?
                "post-commit" : "stock",
            g_auxiliary.policy_relaxed ? "relaxed" : "stock");
    return 1;
}

static int verify_auxiliary_targets(pid_t pid, uintptr_t base,
                                    int want_app_policy_relaxed,
                                    int verbose) {
    if (!g_auxiliary.valid) return 0;
    unsigned char update[sizeof(UPDATE_FLAGS_STOCK)];
    unsigned char app_policy[UPDATE_APP_POLICY_BYTES];
    unsigned char concurrent_helper[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char idle_count_branch[sizeof(POLICY_IDLE_COUNT_CBZ_STOCK)];
    unsigned char idle_clear_branch[IDLE_CLEAR_BRANCH_BYTES];
    unsigned char policy[sizeof(GAME_POLICY_STOCK)];
    unsigned char stream_ref[STREAM_REF_PATCH_BYTES];
    unsigned char output_pool_tail[OUTPUT_POOL_TAIL_PATCH_BYTES];
    unsigned char stream_event[STREAM_EVENT_PATCH_MAX_BYTES];
    unsigned char stream_open_event[STREAM_OPEN_EVENT_BYTES];
    int update_ok = mem_r(pid, base + g_auxiliary.update_off +
                          g_auxiliary.update_flags_patch_off,
                          update, sizeof(update)) == 0 &&
                    memcmp(update, g_auxiliary.update_flags_patched,
                           sizeof(update)) == 0;
    const unsigned char *wanted_app_policy = want_app_policy_relaxed ?
        g_auxiliary.app_policy_relaxed : g_auxiliary.app_policy_stock;
    int app_policy_ok = mem_r(pid, base + g_auxiliary.update_off +
                              g_auxiliary.update_app_policy_patch_off,
                              app_policy, sizeof(app_policy)) == 0 &&
                        memcmp(app_policy, wanted_app_policy,
                               UPDATE_APP_POLICY_BYTES) == 0;
    int concurrent_helper_ok = mem_r(
        pid, base + g_auxiliary.concurrent_helper_off,
        concurrent_helper, sizeof(concurrent_helper)) == 0 &&
        memcmp(concurrent_helper, g_auxiliary.concurrent_helper_patched,
               sizeof(concurrent_helper)) == 0;
    int idle_count_ok = mem_r(
        pid, base + g_auxiliary.policy_off +
             g_auxiliary.policy_idle_count_cbz_off,
        idle_count_branch, sizeof(idle_count_branch)) == 0 &&
        memcmp(idle_count_branch, g_auxiliary.idle_count_branch,
               sizeof(idle_count_branch)) == 0;
    int idle_clear_ok = mem_r(
        pid, base + g_auxiliary.policy_off +
             g_auxiliary.policy_idle_clear_off,
        idle_clear_branch, sizeof(idle_clear_branch)) == 0 &&
        memcmp(idle_clear_branch, g_auxiliary.idle_clear_branch,
               sizeof(idle_clear_branch)) == 0;
    const unsigned char *wanted_policy = want_app_policy_relaxed ?
        GAME_POLICY_RELAXED : g_auxiliary.policy_concurrent;
    int policy_ok = mem_r(pid, base + g_auxiliary.policy_off +
                          g_auxiliary.policy_game_off,
                          policy, sizeof(policy)) == 0 &&
                    memcmp(policy, wanted_policy, sizeof(policy)) == 0;
    int stream_ref_ok = mem_r(
        pid, base + g_auxiliary.stream_event_off +
             g_auxiliary.stream_ref_patch_off,
        stream_ref, sizeof(stream_ref)) == 0 &&
        memcmp(stream_ref, g_auxiliary.stream_ref_patched,
               sizeof(stream_ref)) == 0;
    int output_pool_ok = mem_r(
        pid, base + g_auxiliary.output_pool_off +
             g_auxiliary.output_pool_tail_patch_off,
        output_pool_tail, sizeof(output_pool_tail)) == 0 &&
        memcmp(output_pool_tail, g_auxiliary.output_pool_tail_patched,
               sizeof(output_pool_tail)) == 0;
    int stream_open_ok = mem_r(
        pid, base + g_auxiliary.stream_open_off +
             g_auxiliary.stream_open_patch_off,
        stream_open_event, sizeof(stream_open_event)) == 0 &&
        memcmp(stream_open_event, g_auxiliary.stream_open_patched,
               sizeof(stream_open_event)) == 0;
    unsigned int stream_events_ok = 0;
    for (size_t i = 0; i < STREAM_EVENT_PATCH_COUNT; ++i) {
        uintptr_t address = base + g_auxiliary.stream_event_off +
                            g_auxiliary.stream_event_patch_offsets[i];
        size_t event_size = STREAM_EVENT_PATCH_SIZES[i];
        if (mem_r(pid, address, stream_event, event_size) == 0 &&
            memcmp(stream_event, g_auxiliary.stream_event_patched[i],
                   event_size) == 0) {
            stream_events_ok++;
        }
    }
    int final_ok = update_ok && app_policy_ok && concurrent_helper_ok &&
                   idle_count_ok && idle_clear_ok && policy_ok &&
                   stream_ref_ok && output_pool_ok && stream_open_ok &&
                   stream_events_ok == STREAM_EVENT_PATCH_COUNT;
    if (verbose || !final_ok) {
        fprintf(stderr,
                "[a2h_patch] auxiliary live output_flags=%s idle_init=%s idle_clear=%s concurrent_latch=%s stream_handoff=%s output_pool=%s stream_events=%u/%u stream_open=%s game_auto_pause=%s app_policy=%s output_policy=%s final=%s\n",
                update_ok ? "game+spatial+idle-helper" : "mismatch",
                idle_count_ok ? "manager-x20" : "mismatch",
                idle_clear_ok ? "zero-output" : "mismatch",
                concurrent_helper_ok ? "event-driven" : "mismatch",
                stream_ref_ok ? "handoff" : "mismatch",
                output_pool_ok ? "event-tail" : "mismatch",
                stream_events_ok, STREAM_EVENT_PATCH_COUNT,
                stream_open_ok ? "post-commit" : "mismatch",
                want_app_policy_relaxed ? "disabled" : "enabled",
                app_policy_ok ? (want_app_policy_relaxed ?
                                 "relaxed" : "stock") :
                                "mismatch",
                policy_ok ? (want_app_policy_relaxed ?
                             "multi-speaker" :
                             "multi-speaker+concurrent-pause") :
                            "mismatch",
                final_ok ? "OK" : "FAIL");
    }
    return final_ok;
}

typedef struct {
    uintptr_t update_addr;
    uintptr_t app_policy_addr;
    uintptr_t concurrent_helper_addr;
    uintptr_t idle_count_addr;
    uintptr_t idle_clear_addr;
    uintptr_t policy_addr;
    uintptr_t stream_ref_addr;
    uintptr_t output_pool_addr;
    uintptr_t stream_open_addr;
    uintptr_t stream_event_addr[STREAM_EVENT_PATCH_COUNT];
    unsigned char update_before[sizeof(UPDATE_FLAGS_STOCK)];
    unsigned char app_policy_before[UPDATE_APP_POLICY_BYTES];
    unsigned char concurrent_helper_before[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char idle_count_before[sizeof(POLICY_IDLE_COUNT_CBZ_STOCK)];
    unsigned char idle_clear_before[IDLE_CLEAR_BRANCH_BYTES];
    unsigned char policy_before[sizeof(GAME_POLICY_STOCK)];
    unsigned char stream_ref_before[STREAM_REF_PATCH_BYTES];
    unsigned char output_pool_before[OUTPUT_POOL_TAIL_PATCH_BYTES];
    unsigned char stream_open_before[STREAM_OPEN_EVENT_BYTES];
    unsigned char stream_event_before[STREAM_EVENT_PATCH_COUNT]
                                     [STREAM_EVENT_PATCH_MAX_BYTES];
    int valid;
} auxiliary_transaction_t;

static int auxiliary_transaction_begin(pid_t pid, uintptr_t base,
                                        auxiliary_transaction_t *tx) {
    if (!tx || !g_auxiliary.valid) return 0;
    memset(tx, 0, sizeof(*tx));
    tx->update_addr = base + g_auxiliary.update_off +
                      g_auxiliary.update_flags_patch_off;
    tx->app_policy_addr = base + g_auxiliary.update_off +
                          g_auxiliary.update_app_policy_patch_off;
    tx->concurrent_helper_addr = base +
                                 g_auxiliary.concurrent_helper_off;
    tx->idle_count_addr = base + g_auxiliary.policy_off +
                          g_auxiliary.policy_idle_count_cbz_off;
    tx->idle_clear_addr = base + g_auxiliary.policy_off +
                          g_auxiliary.policy_idle_clear_off;
    tx->policy_addr = base + g_auxiliary.policy_off +
                      g_auxiliary.policy_game_off;
    tx->stream_ref_addr = base + g_auxiliary.stream_event_off +
                          g_auxiliary.stream_ref_patch_off;
    tx->output_pool_addr = base + g_auxiliary.output_pool_off +
                           g_auxiliary.output_pool_tail_patch_off;
    tx->stream_open_addr = base + g_auxiliary.stream_open_off +
                           g_auxiliary.stream_open_patch_off;
    if (mem_r(pid, tx->update_addr, tx->update_before,
              sizeof(tx->update_before)) != 0 ||
        mem_r(pid, tx->app_policy_addr, tx->app_policy_before,
              sizeof(tx->app_policy_before)) != 0 ||
        mem_r(pid, tx->concurrent_helper_addr,
              tx->concurrent_helper_before,
              sizeof(tx->concurrent_helper_before)) != 0 ||
        mem_r(pid, tx->idle_count_addr, tx->idle_count_before,
              sizeof(tx->idle_count_before)) != 0 ||
        mem_r(pid, tx->idle_clear_addr, tx->idle_clear_before,
              sizeof(tx->idle_clear_before)) != 0 ||
        mem_r(pid, tx->policy_addr, tx->policy_before,
              sizeof(tx->policy_before)) != 0 ||
        mem_r(pid, tx->stream_ref_addr, tx->stream_ref_before,
              sizeof(tx->stream_ref_before)) != 0 ||
        mem_r(pid, tx->output_pool_addr, tx->output_pool_before,
              sizeof(tx->output_pool_before)) != 0 ||
        mem_r(pid, tx->stream_open_addr, tx->stream_open_before,
              sizeof(tx->stream_open_before)) != 0) {
        fprintf(stderr, "[a2h_patch] auxiliary transaction snapshot FAIL\n");
        return 0;
    }
    for (size_t i = 0; i < STREAM_EVENT_PATCH_COUNT; ++i) {
        tx->stream_event_addr[i] = base + g_auxiliary.stream_event_off +
                                   g_auxiliary.stream_event_patch_offsets[i];
        if (mem_r(pid, tx->stream_event_addr[i],
                  tx->stream_event_before[i],
                  STREAM_EVENT_PATCH_SIZES[i]) != 0) {
            fprintf(stderr,
                    "[a2h_patch] auxiliary transaction snapshot FAIL stream_event=%lu\n",
                    (unsigned long)i);
            return 0;
        }
    }
    tx->valid = 1;
    fprintf(stderr,
            "[a2h_patch] auxiliary transaction snapshot OK update_bytes=%lu app_policy_bytes=%lu concurrent_helper_bytes=%lu idle_init_bytes=%lu idle_clear_bytes=%lu output_policy_bytes=%lu stream_handoff_bytes=%lu output_pool_bytes=%lu stream_events=%u stream_open_bytes=%lu\n",
            (unsigned long)sizeof(tx->update_before),
            (unsigned long)sizeof(tx->app_policy_before),
            (unsigned long)sizeof(tx->concurrent_helper_before),
            (unsigned long)sizeof(tx->idle_count_before),
            (unsigned long)sizeof(tx->idle_clear_before),
            (unsigned long)sizeof(tx->policy_before),
            (unsigned long)sizeof(tx->stream_ref_before),
            (unsigned long)sizeof(tx->output_pool_before),
            STREAM_EVENT_PATCH_COUNT,
            (unsigned long)sizeof(tx->stream_open_before));
    return 1;
}

static int write_code_twice(pid_t pid, uintptr_t base, uintptr_t addr,
                            const unsigned char *wanted, size_t len,
                            const char *label) {
    unsigned char verify[AUXILIARY_PATCH_MAX_BYTES];
    if (!wanted || !label || !len || len > sizeof(verify)) return 0;
    int first_write = mem_w(pid, addr, wanted, len) == 0 &&
                      mem_r(pid, addr, verify, len) == 0 &&
                      memcmp(verify, wanted, len) == 0;
    int first_cache = first_write &&
                      ((remote_icache_flush(pid, base, addr, len) &
                        ICACHE_REMOTE_IVAU) != 0);
    int second_write = first_cache && mem_w(pid, addr, wanted, len) == 0 &&
                       mem_r(pid, addr, verify, len) == 0 &&
                       memcmp(verify, wanted, len) == 0;
    int second_cache = second_write &&
                       ((remote_icache_flush(pid, base, addr, len) &
                         ICACHE_REMOTE_IVAU) != 0);
    syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL, 0);
    syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL_EXPEDITED, 0);
    int final_ok = second_cache && mem_r(pid, addr, verify, len) == 0 &&
                   memcmp(verify, wanted, len) == 0;
    fprintf(stderr,
            "[a2h_patch] code write label=%s first=%s first_cache=%s second=%s second_cache=%s final=%s\n",
            label, first_write ? "OK" : "FAIL",
            first_cache ? "OK" : "FAIL",
            second_write ? "OK" : "FAIL",
            second_cache ? "OK" : "FAIL",
            final_ok ? "OK" : "FAIL");
    return final_ok;
}

static int restore_code_snapshot(pid_t pid, uintptr_t base, uintptr_t addr,
                                 const unsigned char *before, size_t len,
                                 const char *label) {
    unsigned char verify[AUXILIARY_PATCH_MAX_BYTES];
    if (!before || !label || !len || len > sizeof(verify)) return 0;
    int write_ok = mem_w(pid, addr, before, len) == 0 &&
                   mem_r(pid, addr, verify, len) == 0 &&
                   memcmp(verify, before, len) == 0;
    int cache_ok = write_ok &&
                   ((remote_icache_flush(pid, base, addr, len) &
                     ICACHE_REMOTE_IVAU) != 0);
    fprintf(stderr,
            "[a2h_patch] code rollback label=%s bytes=%s cache=%s\n",
            label, write_ok ? "OK" : "FAIL",
            cache_ok ? "OK" : "FAIL");
    return write_ok && cache_ok;
}

static int auxiliary_transaction_restore(pid_t pid, uintptr_t base,
                                          const auxiliary_transaction_t *tx) {
    if (!tx || !tx->valid) return 0;
    /* Disconnect every branch into the RX-tail helper before its bytes are
     * restored. A failed disconnect intentionally leaves the owned helper in
     * place rather than exposing zero/stock bytes through a live branch. */
    int stream_open_ok = restore_code_snapshot(
        pid, base, tx->stream_open_addr, tx->stream_open_before,
        sizeof(tx->stream_open_before), "stream-open-post-commit");
    int policy_ok = restore_code_snapshot(
        pid, base, tx->policy_addr, tx->policy_before,
        sizeof(tx->policy_before), "isA2HAllowed");
    int update_ok = restore_code_snapshot(
        pid, base, tx->update_addr, tx->update_before,
        sizeof(tx->update_before), "updateA2HMode");
    int idle_clear_ok = restore_code_snapshot(
        pid, base, tx->idle_clear_addr, tx->idle_clear_before,
        sizeof(tx->idle_clear_before), "isA2HAllowed.idle-clear");
    int idle_count_ok = restore_code_snapshot(
        pid, base, tx->idle_count_addr, tx->idle_count_before,
        sizeof(tx->idle_count_before), "isA2HAllowed.idle-init");
    int output_pool_ok = restore_code_snapshot(
        pid, base, tx->output_pool_addr, tx->output_pool_before,
        sizeof(tx->output_pool_before), "output-pool-event-tail");
    int stream_ok = 1;
    for (size_t i = 0; i < STREAM_EVENT_PATCH_COUNT; ++i) {
        char label[48];
        snprintf(label, sizeof(label), "stream-app-event.%lu",
                 (unsigned long)i);
        int item_ok = restore_code_snapshot(
            pid, base, tx->stream_event_addr[i],
            tx->stream_event_before[i], STREAM_EVENT_PATCH_SIZES[i],
            label);
        stream_ok = item_ok && stream_ok;
    }
    int stream_ref_ok = restore_code_snapshot(
        pid, base, tx->stream_ref_addr, tx->stream_ref_before,
        sizeof(tx->stream_ref_before), "stream-handoff-helper");
    int concurrent_helper_ok = 0;
    if (stream_open_ok && policy_ok && update_ok &&
        idle_clear_ok && idle_count_ok) {
        concurrent_helper_ok = restore_code_snapshot(
            pid, base, tx->concurrent_helper_addr,
            tx->concurrent_helper_before,
            sizeof(tx->concurrent_helper_before),
            "rx-tail.concurrent-helper");
    } else {
        fprintf(stderr,
                "[a2h_patch] code rollback label=rx-tail.concurrent-helper skipped=live-branch\n");
    }
    int app_policy_ok = restore_code_snapshot(
        pid, base, tx->app_policy_addr, tx->app_policy_before,
        sizeof(tx->app_policy_before), "updateA2HMode.app-policy");
    return stream_open_ok && idle_count_ok && idle_clear_ok &&
           output_pool_ok && stream_ok &&
           stream_ref_ok && policy_ok && concurrent_helper_ok &&
           app_policy_ok && update_ok;
}

static int auxiliary_cache_preflight(pid_t pid, uintptr_t base) {
    if (!g_auxiliary.valid) return 0;
    int update = remote_icache_flush(
        pid, base, base + g_auxiliary.update_off +
        g_auxiliary.update_flags_patch_off,
        g_auxiliary.update_app_policy_patch_off + UPDATE_APP_POLICY_BYTES -
        g_auxiliary.update_flags_patch_off);
    int concurrent_helper = remote_icache_flush(
        pid, base, base + g_auxiliary.concurrent_helper_off,
        UPDATE_CONCURRENT_HELPER_BYTES);
    int policy = remote_icache_flush(
        pid, base, base + g_auxiliary.policy_off +
        g_auxiliary.policy_idle_count_cbz_off,
        g_auxiliary.policy_game_off + sizeof(GAME_POLICY_STOCK) -
        g_auxiliary.policy_idle_count_cbz_off);
    int stream_events = remote_icache_flush(
        pid, base, base + g_auxiliary.stream_event_off +
        g_auxiliary.stream_event_patch_offsets[0],
        g_auxiliary.stream_event_patch_offsets[STREAM_EVENT_PATCH_COUNT - 1] +
        STREAM_EVENT_PATCH_SIZES[STREAM_EVENT_PATCH_COUNT - 1] -
        g_auxiliary.stream_event_patch_offsets[0]);
    int output_pool = remote_icache_flush(
        pid, base, base + g_auxiliary.output_pool_off +
        g_auxiliary.output_pool_tail_patch_off,
        OUTPUT_POOL_TAIL_PATCH_BYTES);
    int stream_open = remote_icache_flush(
        pid, base, base + g_auxiliary.stream_open_off +
        g_auxiliary.stream_open_patch_off, STREAM_OPEN_EVENT_BYTES);
    int ok = (update & ICACHE_REMOTE_IVAU) != 0 &&
             (concurrent_helper & ICACHE_REMOTE_IVAU) != 0 &&
             (policy & ICACHE_REMOTE_IVAU) != 0 &&
             (stream_events & ICACHE_REMOTE_IVAU) != 0 &&
             (output_pool & ICACHE_REMOTE_IVAU) != 0 &&
             (stream_open & ICACHE_REMOTE_IVAU) != 0;
    fprintf(stderr,
            "[a2h_patch] auxiliary cache preflight update=%s concurrent_helper=%s policy=%s stream_events=%s output_pool=%s stream_open=%s\n",
            (update & ICACHE_REMOTE_IVAU) ? "OK" : "FAIL",
            (concurrent_helper & ICACHE_REMOTE_IVAU) ? "OK" : "FAIL",
            (policy & ICACHE_REMOTE_IVAU) ? "OK" : "FAIL",
            (stream_events & ICACHE_REMOTE_IVAU) ? "OK" : "FAIL",
            (output_pool & ICACHE_REMOTE_IVAU) ? "OK" : "FAIL",
            (stream_open & ICACHE_REMOTE_IVAU) ? "OK" : "FAIL");
    return ok;
}

static int apply_auxiliary_targets(pid_t pid, uintptr_t base,
                                   int want_app_policy_relaxed) {
    if (!g_auxiliary.valid) return 0;
    uintptr_t update_addr = base + g_auxiliary.update_off +
                            g_auxiliary.update_flags_patch_off;
    uintptr_t app_policy_addr = base + g_auxiliary.update_off +
                                g_auxiliary.update_app_policy_patch_off;
    uintptr_t concurrent_helper_addr = base +
                                        g_auxiliary.concurrent_helper_off;
    uintptr_t policy_addr = base + g_auxiliary.policy_off +
                            g_auxiliary.policy_game_off;
    uintptr_t idle_count_addr = base + g_auxiliary.policy_off +
                                g_auxiliary.policy_idle_count_cbz_off;
    uintptr_t idle_clear_addr = base + g_auxiliary.policy_off +
                                g_auxiliary.policy_idle_clear_off;
    unsigned char current[AUXILIARY_PATCH_MAX_BYTES];
    int concurrent_helper_ready =
        mem_r(pid, concurrent_helper_addr, current,
              UPDATE_CONCURRENT_HELPER_BYTES) == 0 &&
        memcmp(current, g_auxiliary.concurrent_helper_patched,
               UPDATE_CONCURRENT_HELPER_BYTES) == 0;
    if (!concurrent_helper_ready) {
        concurrent_helper_ready = write_code_twice(
            pid, base, concurrent_helper_addr,
            g_auxiliary.concurrent_helper_patched,
            UPDATE_CONCURRENT_HELPER_BYTES,
            "rx-tail.concurrent-helper");
    }
    int idle_count_ready = concurrent_helper_ready &&
        mem_r(pid, idle_count_addr, current,
              sizeof(g_auxiliary.idle_count_branch)) == 0 &&
        memcmp(current, g_auxiliary.idle_count_branch,
               sizeof(g_auxiliary.idle_count_branch)) == 0;
    if (concurrent_helper_ready && !idle_count_ready) {
        idle_count_ready = write_code_twice(
            pid, base, idle_count_addr, g_auxiliary.idle_count_branch,
            sizeof(g_auxiliary.idle_count_branch),
            "isA2HAllowed.idle-init");
    }
    int update_ready = idle_count_ready &&
                       mem_r(pid, update_addr, current,
                             sizeof(g_auxiliary.update_flags_patched)) == 0 &&
                       memcmp(current, g_auxiliary.update_flags_patched,
                              sizeof(g_auxiliary.update_flags_patched)) == 0;
    if (concurrent_helper_ready && !update_ready) {
        update_ready = write_code_twice(pid, base, update_addr,
                                        g_auxiliary.update_flags_patched,
                                        sizeof(g_auxiliary.update_flags_patched),
                                        "updateA2HMode.game-spatial-idle-helper");
    }
    const unsigned char *app_policy = want_app_policy_relaxed ?
        g_auxiliary.app_policy_relaxed : g_auxiliary.app_policy_stock;
    int app_policy_ready = update_ready &&
                           mem_r(pid, app_policy_addr, current,
                                  UPDATE_APP_POLICY_BYTES) == 0 &&
                           memcmp(current, app_policy,
                                  UPDATE_APP_POLICY_BYTES) == 0;
    if (update_ready && !app_policy_ready) {
        app_policy_ready = write_code_twice(
            pid, base, app_policy_addr, app_policy,
            UPDATE_APP_POLICY_BYTES,
            want_app_policy_relaxed ?
                "updateA2HMode.app-policy.relaxed" :
                "updateA2HMode.app-policy.stock");
    }
    const unsigned char *policy = want_app_policy_relaxed ?
        GAME_POLICY_RELAXED : g_auxiliary.policy_concurrent;
    int policy_ready = concurrent_helper_ready && app_policy_ready &&
                       mem_r(pid, policy_addr, current,
                             sizeof(GAME_POLICY_STOCK)) == 0 &&
                       memcmp(current, policy,
                              sizeof(GAME_POLICY_STOCK)) == 0;
    if (concurrent_helper_ready && !policy_ready) {
        policy_ready = write_code_twice(
            pid, base, policy_addr, policy, sizeof(GAME_POLICY_STOCK),
            want_app_policy_relaxed ?
                "isA2HAllowed.multi-speaker" :
                "isA2HAllowed.concurrent-pause");
    }
    int idle_clear_ready = concurrent_helper_ready && policy_ready &&
        mem_r(pid, idle_clear_addr, current,
              IDLE_CLEAR_BRANCH_BYTES) == 0 &&
        memcmp(current, g_auxiliary.idle_clear_branch,
               IDLE_CLEAR_BRANCH_BYTES) == 0;
    if (concurrent_helper_ready && policy_ready &&
        !idle_clear_ready) {
        idle_clear_ready = write_code_twice(
            pid, base, idle_clear_addr, g_auxiliary.idle_clear_branch,
            IDLE_CLEAR_BRANCH_BYTES, "isA2HAllowed.idle-clear");
    }
    uintptr_t stream_ref_addr = base + g_auxiliary.stream_event_off +
                                g_auxiliary.stream_ref_patch_off;
    int stream_ref_ready = concurrent_helper_ready && policy_ready &&
        idle_clear_ready &&
        mem_r(pid, stream_ref_addr, current, STREAM_REF_PATCH_BYTES) == 0 &&
        memcmp(current, g_auxiliary.stream_ref_patched,
               STREAM_REF_PATCH_BYTES) == 0;
    if (concurrent_helper_ready && policy_ready &&
        idle_clear_ready &&
        !stream_ref_ready) {
        stream_ref_ready = write_code_twice(
            pid, base, stream_ref_addr, g_auxiliary.stream_ref_patched,
            STREAM_REF_PATCH_BYTES, "stream-handoff-helper");
    }
    uintptr_t output_pool_addr = base + g_auxiliary.output_pool_off +
                                 g_auxiliary.output_pool_tail_patch_off;
    int output_pool_ready = stream_ref_ready &&
        mem_r(pid, output_pool_addr, current,
              OUTPUT_POOL_TAIL_PATCH_BYTES) == 0 &&
        memcmp(current, g_auxiliary.output_pool_tail_patched,
               OUTPUT_POOL_TAIL_PATCH_BYTES) == 0;
    if (stream_ref_ready && !output_pool_ready) {
        output_pool_ready = write_code_twice(
            pid, base, output_pool_addr,
            g_auxiliary.output_pool_tail_patched,
            OUTPUT_POOL_TAIL_PATCH_BYTES, "output-pool-event-tail");
    }
    int stream_ready = concurrent_helper_ready && policy_ready &&
                       idle_clear_ready && stream_ref_ready &&
                       output_pool_ready;
    for (size_t i = 0; stream_ready && i < STREAM_EVENT_PATCH_COUNT; ++i) {
        uintptr_t address = base + g_auxiliary.stream_event_off +
                            g_auxiliary.stream_event_patch_offsets[i];
        size_t event_size = STREAM_EVENT_PATCH_SIZES[i];
        int item_ready = mem_r(pid, address, current, event_size) == 0 &&
                         memcmp(current, g_auxiliary.stream_event_patched[i],
                                event_size) == 0;
        if (!item_ready) {
            char label[48];
            snprintf(label, sizeof(label), "stream-app-event.%lu",
                     (unsigned long)i);
            item_ready = write_code_twice(
                pid, base, address, g_auxiliary.stream_event_patched[i],
                event_size, label);
        }
        stream_ready = item_ready;
    }
    uintptr_t stream_open_addr = base + g_auxiliary.stream_open_off +
                                 g_auxiliary.stream_open_patch_off;
    int stream_open_ready = stream_ready &&
        mem_r(pid, stream_open_addr, current,
              STREAM_OPEN_EVENT_BYTES) == 0 &&
        memcmp(current, g_auxiliary.stream_open_patched,
               STREAM_OPEN_EVENT_BYTES) == 0;
    if (stream_ready && !stream_open_ready) {
        stream_open_ready = write_code_twice(
            pid, base, stream_open_addr,
            g_auxiliary.stream_open_patched,
            STREAM_OPEN_EVENT_BYTES, "stream-open-post-commit");
    }
    return update_ready && app_policy_ready && concurrent_helper_ready &&
           idle_count_ready &&
           policy_ready &&
           idle_clear_ready && stream_ref_ready && output_pool_ready &&
           stream_ready && stream_open_ready &&
           verify_auxiliary_targets(pid, base,
                                    want_app_policy_relaxed, 1);
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
    int mode=0,pid=-1; char *pkgfile=NULL; uintptr_t base_override=0;
    int check_want_global=-1, game_auto_pause=1;
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
            printf("       %s --game-auto-pause enabled|disabled\n",argv[0]);
            printf("       %s --base 0x...\n",argv[0]);
            printf("       capabilities: apply-final-verified\n");
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
        else if(strcmp(argv[i],"--game-auto-pause")==0 && i+1<argc) {
            const char *policy=argv[++i];
            if(strcmp(policy,"enabled")==0) game_auto_pause=1;
            else if(strcmp(policy,"disabled")==0) game_auto_pause=0;
            else {
                fprintf(stderr,
                        "[a2h_patch] ERROR: invalid --game-auto-pause value=%s\n",
                        policy);
                return 2;
            }
        }
        else if(strcmp(argv[i],"--base")==0 && i+1<argc) base_override=(uintptr_t)strtoull(argv[++i],NULL,0);
        else if(argv[i][0] != '-') pid=atoi(argv[i]);
    }
    if ((mode == 0 || mode == 1) &&
        setpriority(PRIO_PROCESS, 0, -10) == 0) {
        fprintf(stderr, "[a2h_patch] priority=boosted nice=-10\n");
    }
    if(!pkgfile) pkgfile="/data/adb/modules/a2h_hook/config/packages.txt";
    // Identity props are relatively expensive; only print on real apply.
    if(mode==0 || mode==1) log_system_identity();
    if(pid<=0){ for(int i=0;i<30;i++){ pid=find_pid(); if(pid>0)break; sleep(1);} }
    if(pid<=0){fprintf(stderr,"[a2h_patch] ERROR: service not found\n");return 2;}
    fprintf(stderr,"[a2h_patch] pid=%d mode=%s game_auto_pause=%s\n", pid,
            mode==1?"whitelist":(mode==2?"show":(mode==3?"status":(mode==4?"check":"global"))),
            game_auto_pause?"enabled":"disabled");
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
    if (!resolve_auxiliary_targets(pid, base)) {
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
    if(mode==2){
        int rc=show_strings(pid,base);
        verify_auxiliary_targets(pid, base, game_auto_pause ? 0 : 1, 1);
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0); return rc?1:0; }
    if(mode==3){
        int status_ok = already_global &&
                        verify_auxiliary_targets(pid, base,
                                                 game_auto_pause ? 0 : 1, 1);
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
        return status_ok?0:1;
    }
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
        ok = ok && verify_auxiliary_targets(pid, base,
                                             game_auto_pause ? 0 : 1, 1);
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
        int auxiliary_preflight = auxiliary_cache_preflight(pid, base);
        if ((preflight & ICACHE_REMOTE_IVAU) == 0 || !auxiliary_preflight) {
            fprintf(stderr,
                    "[a2h_patch] ERROR: coordinated cache preflight failed; no patch data written\n");
            free_pkgs(pkgs);
            if(g_attached)trace_detach(pid);
            fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
            return 1;
        }
        whitelist_transaction_t tx;
        auxiliary_transaction_t auxiliary_tx;
        if (!whitelist_transaction_begin(pid, base, &tx) ||
            !auxiliary_transaction_begin(pid, base, &auxiliary_tx)) {
            free_pkgs(pkgs);
            if(g_attached)trace_detach(pid);
            fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
            return 1;
        }
        int auxiliary_ok=apply_auxiliary_targets(
            pid, base, game_auto_pause ? 0 : 1);
        int rc=auxiliary_ok && apply_strings(pid,base,pkgs);
        int stub_ok=0;
        if(rc) stub_ok=install_whitelist_stub(pid,base);
        else fprintf(stderr,"[a2h_patch] skip stub because string apply failed\n");
        unsigned char vf[16]={0}; mem_r(pid,func_addr,vf,16);
        int stubbed=exact_whitelist_stub_at(pid,func_addr);
        stub_info_t si;
        memset(&si, 0, sizeof(si));
        int table_ok = stubbed && inspect_stub_table(pid, base, k_func_off(), vf, pkgs, 1, &si);
        int lifecycle_ok = verify_auxiliary_targets(
            pid, base, game_auto_pause ? 0 : 1, 1);
        int final_ok = auxiliary_ok && lifecycle_ok && rc && stub_ok &&
                       stubbed && table_ok && si.content_mismatch == 0;
        int rollback_ok = 1;
        if (!final_ok) {
            int main_rollback = whitelist_transaction_restore(pid, base, &tx);
            int auxiliary_rollback = auxiliary_transaction_restore(
                pid, base, &auxiliary_tx);
            rollback_ok = main_rollback && auxiliary_rollback;
        }
        if (final_ok) {
            save_func_off_hint(k_func_off());
            save_cave_hint(slot_off(0));
        }
        free_pkgs(pkgs);
        fprintf(stderr,"whitelist: %s\n", final_ok?"OK":"FAIL");
        fprintf(stderr,"[a2h_patch] summary method=%s profile=%s hint=%s lifecycle=%s policy=%s write=%s stub=%s table=%s final=%s rollback=%s active=%d active_ptrs=%d lines=%d rejected=%d fallback=%d all_off=%d mismatch=%d icache=%s\n",
                g_locate_method,g_profile,g_profile_hint,
                lifecycle_ok?"OK":"FAIL",game_auto_pause?"stock":"relaxed",
                rc?"OK":"FAIL", stub_ok?"OK":"FAIL",
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
    int main_write_needed = !(already_global && global_marked);
    fprintf(stderr, main_write_needed ?
            (already_global ? "upgrading global marker...\n" : "enabling global...\n") :
            "global entry already enabled; verifying lifecycle overlays...\n");
    int preflight = main_write_needed ?
                    remote_icache_flush(pid, base, func_addr,
                                        sizeof(GLOBAL_PATCH)) :
                    ICACHE_REMOTE_IVAU;
    int auxiliary_preflight = auxiliary_cache_preflight(pid, base);
    if ((preflight & ICACHE_REMOTE_IVAU) == 0 || !auxiliary_preflight) {
        fprintf(stderr,
                "[a2h_patch] ERROR: coordinated cache preflight failed; no function bytes written\n");
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
        return 1;
    }
    unsigned char global_before[sizeof(GLOBAL_PATCH)];
    auxiliary_transaction_t auxiliary_tx;
    if (mem_r(pid, func_addr, global_before, sizeof(global_before)) != 0 ||
        !auxiliary_transaction_begin(pid, base, &auxiliary_tx)) {
        fprintf(stderr,"[a2h_patch] ERROR: global transaction snapshot failed\n");
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
        return 1;
    }
    int auxiliary_ok = apply_auxiliary_targets(
        pid, base, game_auto_pause ? 0 : 1);
    if (!main_write_needed) {
        int ok = auxiliary_ok && verify_auxiliary_targets(
            pid, base, game_auto_pause ? 0 : 1, 1);
        int rollback_ok = ok ? 1 :
                          auxiliary_transaction_restore(pid, base,
                                                        &auxiliary_tx);
        if (ok) save_func_off_hint(k_func_off());
        fprintf(stderr,"enable: %s\n",ok?"OK":"FAIL");
        fprintf(stderr,
                "[a2h_patch] global lifecycle=%s policy=%s rollback=%s\n",
                ok?"OK":"FAIL",game_auto_pause?"stock":"relaxed",
                ok?"not-needed":(rollback_ok?"OK":"FAIL"));
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
        return ok?0:1;
    }
    unsigned char first_verify[sizeof(GLOBAL_PATCH)]={0};
    int first_write_ok = auxiliary_ok &&
                         mem_w(pid,func_addr,GLOBAL_PATCH,sizeof(GLOBAL_PATCH)) == 0 &&
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
    int lifecycle_ok = verify_auxiliary_targets(
        pid, base, game_auto_pause ? 0 : 1, 1);
    int ok = auxiliary_ok && lifecycle_ok && first_write_ok &&
             second_write_ok && verify_ok && cache_ok;
    int rollback_ok = 1;
    if (!ok) {
        int main_rollback = restore_global_snapshot(pid, base, func_addr,
                                                    global_before);
        int auxiliary_rollback = auxiliary_transaction_restore(
            pid, base, &auxiliary_tx);
        rollback_ok = main_rollback && auxiliary_rollback;
    }
    if (ok) save_func_off_hint(k_func_off());
    if (!first_write_ok) fprintf(stderr, "[a2h_patch] ERROR: global first write failed\n");
    if (first_write_ok && !first_cache_ok) fprintf(stderr, "[a2h_patch] ERROR: global first I-cache synchronization unverified\n");
    if (first_write_ok && first_cache_ok && !second_write_ok)
        fprintf(stderr, "[a2h_patch] ERROR: global second write/verify failed\n");
    if (second_write_ok && !second_cache_ok) fprintf(stderr, "[a2h_patch] ERROR: global second I-cache synchronization unverified\n");
    if (!verify_ok) fprintf(stderr, "[a2h_patch] ERROR: global final byte verification failed\n");
    fprintf(stderr,"enable: %s\n",ok?"OK":"FAIL");
    fprintf(stderr,"[a2h_patch] global verify head=%02x %02x %02x %02x method=%s profile=%s hint=%s lifecycle=%s policy=%s icache=%s rollback=%s\n",
            vf[0],vf[1],vf[2],vf[3], g_locate_method, g_profile, g_profile_hint,
            lifecycle_ok?"OK":"FAIL",game_auto_pause?"stock":"relaxed",
            icache_status(), ok?"not-needed":(rollback_ok?"OK":"FAIL"));
    if(g_attached)trace_detach(pid);
    fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
    return ok?0:1;
}
