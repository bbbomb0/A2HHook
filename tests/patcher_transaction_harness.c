/* Isolated transaction tests. This file never touches another process. */

#define A2H_TEST_FAULT_INJECTION 1
#define main a2h_embedded_main
#define syscall a2h_test_syscall
#define open a2h_test_open
#define fopen a2h_test_fopen
#include "../src/patcher_v3.c"
#undef fopen
#undef open
#undef syscall
#undef main

#include <stdarg.h>

#define FAKE_SIZE 0x10000u
#define FAKE_BASE ((uintptr_t)0x10000000u)
#define AUXILIARY_NON_EVENT_REGION_COUNT 9u
#define AUXILIARY_CODE_REGION_COUNT \
    (AUXILIARY_NON_EVENT_REGION_COUNT + STREAM_EVENT_PATCH_COUNT)
#define AUXILIARY_FAULT_POINT_COUNT (AUXILIARY_CODE_REGION_COUNT * 2u)

_Static_assert(AUXILIARY_FAULT_POINT_COUNT == 24u,
               "update auxiliary fault-point accounting");

static unsigned char fake_memory[FAKE_SIZE];
static int write_call_count;
static int fail_write_call;

static int fake_range(uintptr_t address, size_t size, size_t *offset) {
    if (address < FAKE_BASE || size > FAKE_SIZE) return 0;
    uintptr_t relative = address - FAKE_BASE;
    if (relative > FAKE_SIZE - size) return 0;
    *offset = (size_t)relative;
    return 1;
}

long a2h_test_syscall(long number, ...) {
    va_list args;
    va_start(args, number);
    if (number == 270 || number == 271) {
        (void)va_arg(args, int);
        struct iovec *local = va_arg(args, struct iovec *);
        unsigned long local_count = va_arg(args, unsigned long);
        struct iovec *remote = va_arg(args, struct iovec *);
        unsigned long remote_count = va_arg(args, unsigned long);
        (void)va_arg(args, unsigned long);
        va_end(args);
        if (local_count != 1 || remote_count != 1 || local[0].iov_len != remote[0].iov_len) {
            errno = EINVAL;
            return -1;
        }
        size_t offset = 0;
        size_t size = local[0].iov_len;
        if (!fake_range((uintptr_t)remote[0].iov_base, size, &offset)) {
            errno = EFAULT;
            return -1;
        }
        if (number == 271) {
            write_call_count++;
            if (fail_write_call > 0 && write_call_count == fail_write_call) {
                errno = EIO;
                return -1;
            }
            memcpy(fake_memory + offset, local[0].iov_base, size);
        } else {
            memcpy(local[0].iov_base, fake_memory + offset, size);
        }
        return (long)size;
    }
    va_end(args);
    if (number == __NR_membarrier) return 0;
    errno = ENOSYS;
    return -1;
}

int a2h_test_open(const char *path, int flags, ...) {
    (void)path;
    (void)flags;
    errno = EACCES;
    return -1;
}

FILE *a2h_test_fopen(const char *path, const char *mode) {
    (void)path;
    (void)mode;
    errno = EACCES;
    return NULL;
}

static void reset_fake(void) {
    for (size_t i = 0; i < sizeof(fake_memory); ++i) fake_memory[i] = (unsigned char)(i * 37u + 11u);
    g_func_off = 0x1000;
    g_ptr_off = 0x2000;
    g_stub_mark = 0x2100;
    g_rw_start = 0;
    g_rw_end = FAKE_SIZE;
    g_rx_start = 0;
    g_rx_end = 0;
    g_attached = 0;
    g_icache_methods = 0;
    g_test_icache_override = -1;
    g_test_icache_fail_call = 0;
    g_test_icache_calls = 0;
    memset(&g_auxiliary, 0, sizeof(g_auxiliary));
    for (int i = 0; i < MAX_SLOTS; ++i) {
        set_slot_off(i, 0x3000u + (uintptr_t)i * 64u);
        slots[i].max_len = 63;
        char value[64];
        snprintf(value, sizeof(value), "com.example.slot%d", i + 1);
        memset(fake_memory + slot_off(i), 0, 64);
        memcpy(fake_memory + slot_off(i), value, strlen(value));
    }
    write_call_count = 0;
    fail_write_call = 0;
}

static void configure_auxiliary_stock(void) {
    g_auxiliary.update_off = 0x5000u;
    g_auxiliary.policy_off = 0x6000u;
    g_auxiliary.stream_event_off = 0x7000u;
    g_auxiliary.output_pool_off = 0xA000u;
    g_auxiliary.concurrent_helper_off = 0xB000u;
    g_auxiliary.stream_open_off = 0xD000u;
    g_auxiliary.update_flags_patch_off = UPDATE_FLAGS_PATCH_OFF;
    g_auxiliary.update_app_policy_patch_off = UPDATE_APP_POLICY_PATCH_OFF;
    g_auxiliary.policy_idle_count_cbz_off = POLICY_IDLE_COUNT_CBZ_OFF;
    g_auxiliary.policy_idle_clear_off = IDLE_CLEAR_BRANCH_PATCH_OFF;
    g_auxiliary.policy_game_off = GAME_POLICY_PATCH_OFF;
    g_auxiliary.stream_ref_patch_off = STREAM_REF_PATCH_OFF;
    memcpy(g_auxiliary.stream_event_patch_offsets,
           STREAM_EVENT_PATCH_OFFSETS,
           sizeof(g_auxiliary.stream_event_patch_offsets));
    g_auxiliary.stream_update_call_off = STREAM_UPDATE_CALL_OFF;
    g_auxiliary.output_pool_tail_patch_off = OUTPUT_POOL_TAIL_PATCH_OFF;
    g_auxiliary.stream_open_patch_off = STREAM_OPEN_EPILOGUE_EXPECTED_OFF;
    g_auxiliary.update_flags_state = OVERLAY_STOCK;
    g_auxiliary.update_app_policy_state = OVERLAY_STOCK;
    g_auxiliary.concurrent_helper_state = OVERLAY_STOCK;
    g_auxiliary.idle_clear_state = OVERLAY_STOCK;
    g_auxiliary.policy_relaxed = 0;
    g_auxiliary.stream_ref_state = OVERLAY_STOCK;
    g_auxiliary.stream_open_state = OVERLAY_STOCK;
    g_auxiliary.output_pool_state = OVERLAY_STOCK;
    g_auxiliary.stream_events_patched = 0;
    g_auxiliary.valid = 1;
    if (!build_idle_clear_overlay(
            g_auxiliary.update_off, g_auxiliary.policy_off,
            g_auxiliary.concurrent_helper_off,
            g_auxiliary.update_flags_patched,
            g_auxiliary.update_flags_guarded_legacy,
            g_auxiliary.idle_count_branch,
            g_auxiliary.idle_clear_branch)) {
        fprintf(stderr, "FAIL auxiliary fixture idle clear generation\n");
        exit(2);
    }
    memcpy(g_auxiliary.app_policy_disk,
           UPDATE_APP_POLICY_DISK_TEMPLATE, UPDATE_APP_POLICY_BYTES);
    uint32_t stock_call = 0;
    uintptr_t call_target = g_auxiliary.update_off + 0x9000u;
    if (!encode_aarch64_bl(
            g_auxiliary.update_off + UPDATE_APP_POLICY_PATCH_OFF +
            UPDATE_APP_POLICY_DISK_BL_OFF,
            call_target, &stock_call)) {
        fprintf(stderr, "FAIL auxiliary fixture BL encode\n");
        exit(2);
    }
    store_u32le(g_auxiliary.app_policy_disk +
                UPDATE_APP_POLICY_DISK_BL_OFF, stock_call);
    if (!build_update_app_policy_overlay(
            g_auxiliary.update_off, g_auxiliary.app_policy_disk,
            g_auxiliary.app_policy_stock,
            g_auxiliary.app_policy_relaxed,
            g_auxiliary.app_policy_legacy,
            &g_auxiliary.app_policy_call_target) ||
        g_auxiliary.app_policy_call_target != call_target) {
        fprintf(stderr, "FAIL auxiliary fixture policy generation\n");
        exit(2);
    }
    memcpy(g_auxiliary.stream_ref_stock, STREAM_REF_STOCK_TEMPLATE,
           STREAM_REF_PATCH_BYTES);
    uint32_t delete_call = 0;
    uintptr_t delete_target = g_auxiliary.stream_event_off + 0x9000u;
    if (!encode_aarch64_bl(
            g_auxiliary.stream_event_off + STREAM_REF_PATCH_OFF +
            STREAM_REF_DELETE_BL_OFF,
            delete_target, &delete_call)) {
        fprintf(stderr, "FAIL stream reference fixture BL encode\n");
        exit(2);
    }
    store_u32le(g_auxiliary.stream_ref_stock +
                STREAM_REF_DELETE_BL_OFF, delete_call);
    uintptr_t allowed_target = g_auxiliary.stream_event_off + 0x4000u;
    uintptr_t update_target = g_auxiliary.stream_event_off + 0x5000u;
    unsigned char output_tail[sizeof(OUTPUT_POOL_TAIL_STOCK_TEMPLATE)];
    memcpy(output_tail, OUTPUT_POOL_TAIL_STOCK_TEMPLATE,
           sizeof(output_tail));
    if (!build_stream_ref_overlay(
            g_auxiliary.stream_event_off, g_auxiliary.stream_ref_stock,
            allowed_target, update_target, g_auxiliary.output_pool_off,
            output_tail,
            g_auxiliary.stream_ref_patched,
            g_auxiliary.stream_ref_persistent_legacy,
            g_auxiliary.stream_ref_failed_idle_guard,
            g_auxiliary.stream_ref_legacy,
            g_auxiliary.output_pool_tail_patched,
            g_auxiliary.output_pool_tail_persistent_legacy,
            &g_auxiliary.stream_ref_delete_target,
            &g_auxiliary.output_pool_stack_fail_target) ||
        g_auxiliary.stream_ref_delete_target != delete_target) {
        fprintf(stderr, "FAIL stream reference fixture generation\n");
        exit(2);
    }
    memset(g_auxiliary.concurrent_helper_disk, 0,
           UPDATE_CONCURRENT_HELPER_BYTES);
    memcpy(g_auxiliary.stream_event_stock,
           STREAM_EVENT_STOCK_TEMPLATE,
           sizeof(g_auxiliary.stream_event_stock));
    uint32_t strlen_call = 0;
    if (!encode_aarch64_bl(
            g_auxiliary.stream_event_off + STREAM_EVENT_PATCH_OFFSETS[0] +
            sizeof(uint32_t) * 4u,
            g_auxiliary.stream_event_off + 0x9000u, &strlen_call)) {
        fprintf(stderr, "FAIL stream event fixture BL encode\n");
        exit(2);
    }
    store_u32le(g_auxiliary.stream_event_stock[0] +
                sizeof(uint32_t) * 4u, strlen_call);
    memcpy(g_auxiliary.stream_open_stock,
           STREAM_OPEN_EPILOGUE_STOCK, STREAM_OPEN_EVENT_BYTES);
    if (!build_concurrent_helpers(
            g_auxiliary.concurrent_helper_off, g_auxiliary.policy_off,
            g_auxiliary.stream_event_off, g_auxiliary.stream_open_off,
            g_auxiliary.stream_open_patch_off, update_target,
            update_target,
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
            g_auxiliary.stream_open_patched)) {
        fprintf(stderr, "FAIL concurrent helper fixture generation\n");
        exit(2);
    }
    memcpy(fake_memory + g_auxiliary.update_off + UPDATE_FLAGS_PATCH_OFF,
           UPDATE_FLAGS_STOCK, sizeof(UPDATE_FLAGS_STOCK));
    memcpy(fake_memory + g_auxiliary.update_off +
           UPDATE_APP_POLICY_PATCH_OFF, g_auxiliary.app_policy_stock,
           UPDATE_APP_POLICY_BYTES);
    memcpy(fake_memory + g_auxiliary.concurrent_helper_off,
           g_auxiliary.concurrent_helper_disk,
           UPDATE_CONCURRENT_HELPER_BYTES);
    memcpy(fake_memory + g_auxiliary.policy_off + POLICY_IDLE_COUNT_CBZ_OFF,
           POLICY_IDLE_COUNT_CBZ_STOCK,
           sizeof(POLICY_IDLE_COUNT_CBZ_STOCK));
    memcpy(fake_memory + g_auxiliary.policy_off + GAME_POLICY_PATCH_OFF,
           GAME_POLICY_STOCK, sizeof(GAME_POLICY_STOCK));
    memcpy(fake_memory + g_auxiliary.policy_off +
           IDLE_CLEAR_BRANCH_PATCH_OFF, IDLE_CLEAR_BRANCH_STOCK,
           IDLE_CLEAR_BRANCH_BYTES);
    memcpy(fake_memory + g_auxiliary.stream_event_off +
           STREAM_REF_PATCH_OFF, g_auxiliary.stream_ref_stock,
           STREAM_REF_PATCH_BYTES);
    memcpy(fake_memory + g_auxiliary.output_pool_off +
           OUTPUT_POOL_TAIL_PATCH_OFF, OUTPUT_POOL_TAIL_STOCK,
           OUTPUT_POOL_TAIL_PATCH_BYTES);
    memcpy(fake_memory + g_auxiliary.stream_open_off +
           g_auxiliary.stream_open_patch_off,
           g_auxiliary.stream_open_stock, STREAM_OPEN_EVENT_BYTES);
    for (size_t i = 0; i < STREAM_EVENT_PATCH_COUNT; ++i) {
        memcpy(fake_memory + g_auxiliary.stream_event_off +
               STREAM_EVENT_PATCH_OFFSETS[i],
               g_auxiliary.stream_event_stock[i],
               STREAM_EVENT_PATCH_SIZES[i]);
    }
}

static void configure_owned_cave(void) {
    uintptr_t cave = 0x3000u;
    for (int i = 0; i < MAX_SLOTS; ++i) {
        set_slot_off(i, cave + (uintptr_t)i * 64u);
        slots[i].max_len = 63;
    }
    g_stub_mark = cave + MAX_SLOTS * 64u;
    g_ptr_off = (g_stub_mark + sizeof(WHITELIST_MARKER) + 7u) & ~(uintptr_t)7u;
    g_elf_tail_start = cave;
    g_elf_tail_end = cave + WHITELIST_CAVE_BYTES;
}

static int expect_equal(const unsigned char *before, const char *name, int fault) {
    if (memcmp(before, fake_memory, sizeof(fake_memory)) == 0) return 1;
    fprintf(stderr, "FAIL %s fault=%d did not roll back byte-for-byte\n", name, fault);
    return 0;
}

static int test_stub_transaction(void) {
    int ok = 1;
    reset_fake();
    unsigned char before[FAKE_SIZE];
    memcpy(before, fake_memory, sizeof(before));
    if (!write_whitelist_stub_code(42, FAKE_BASE) || memcmp(before, fake_memory, sizeof(before)) == 0) {
        fprintf(stderr, "FAIL stub success path\n");
        ok = 0;
    }
    for (int fault = 1; fault <= 3; ++fault) {
        reset_fake();
        memcpy(before, fake_memory, sizeof(before));
        fail_write_call = fault;
        if (write_whitelist_stub_code(42, FAKE_BASE) != 0) {
            fprintf(stderr, "FAIL stub fault=%d unexpectedly succeeded\n", fault);
            ok = 0;
        }
        ok &= expect_equal(before, "stub", fault);
    }
    return ok;
}

static int test_string_transaction(void) {
    int ok = 1;
    char *packages[MAX_SLOTS];
    char values[MAX_SLOTS][64];
    for (int i = 0; i < MAX_SLOTS; ++i) {
        snprintf(values[i], sizeof(values[i]), "org.example.changed%d", i + 1);
        packages[i] = values[i];
    }
    unsigned char before[FAKE_SIZE];
    for (int fault = 1; fault <= MAX_SLOTS * 2; ++fault) {
        reset_fake();
        memcpy(before, fake_memory, sizeof(before));
        fail_write_call = fault;
        if (apply_strings(42, FAKE_BASE, packages) != 0) {
            fprintf(stderr, "FAIL strings fault=%d unexpectedly succeeded\n", fault);
            ok = 0;
        }
        ok &= expect_equal(before, "strings", fault);
    }
    reset_fake();
    memcpy(before, fake_memory, sizeof(before));
    if (!apply_strings(42, FAKE_BASE, packages) || memcmp(before, fake_memory, sizeof(before)) == 0) {
        fprintf(stderr, "FAIL strings success path\n");
        ok = 0;
    }
    return ok;
}

static int test_owned_stale_stub_shapes(void) {
    int ok = 1;
    reset_fake();
    configure_owned_cave();
    if (!write_whitelist_stub_code(42, FAKE_BASE)) {
        fprintf(stderr, "FAIL stale stub setup\n");
        return 0;
    }
    unsigned char *function = fake_memory + g_func_off;
    if (!marked_cave_ok(42, FAKE_BASE, slot_off(0))) {
        fprintf(stderr, "FAIL stale stub cave ownership\n");
        ok = 0;
    }

    unsigned char stub[WHITELIST_STUB_BYTES];
    memcpy(stub, function, sizeof(stub));
    uintptr_t recovered_cave = 0;
    if (!exact_stub_cave_from_code(42, FAKE_BASE, g_func_off, &recovered_cave) ||
        recovered_cave != slot_off(0)) {
        fprintf(stderr, "FAIL exact stub cave recovery without hint\n");
        ok = 0;
    }
    memcpy(function, PATCH, sizeof(PATCH));
    if (global_stub_suffix_overlay(function, WHITELIST_STUB_BYTES) != 8) {
        fprintf(stderr, "FAIL stale 8-byte overlay fingerprint\n");
        ok = 0;
    }
    recovered_cave = 0;
    if (!owned_global_stub_suffix_ok(42, FAKE_BASE, function,
                                     WHITELIST_STUB_BYTES) ||
        !find_unique_marked_cave(42, FAKE_BASE, &recovered_cave) ||
        recovered_cave != slot_off(0)) {
        fprintf(stderr, "FAIL stale overlay cave recovery without hint\n");
        ok = 0;
    }

    memcpy(function, stub, sizeof(stub));
    memcpy(function, GLOBAL_PATCH, sizeof(GLOBAL_PATCH));
    if (global_stub_suffix_overlay(function, WHITELIST_STUB_BYTES) != 16) {
        fprintf(stderr, "FAIL stale 16-byte overlay fingerprint\n");
        ok = 0;
    }
    function[5 * sizeof(uint32_t)] ^= 1u;
    if (global_stub_suffix_overlay(function, WHITELIST_STUB_BYTES) != 0) {
        fprintf(stderr, "FAIL corrupted stale suffix accepted\n");
        ok = 0;
    }
    function[5 * sizeof(uint32_t)] ^= 1u;
    fake_memory[g_stub_mark] ^= 1u;
    if (marked_cave_ok(42, FAKE_BASE, slot_off(0))) {
        fprintf(stderr, "FAIL corrupted cave marker accepted\n");
        ok = 0;
    }
    return ok;
}

static int32_t sign_extend_branch(uint32_t value, unsigned int bits) {
    uint32_t sign = 1u << (bits - 1u);
    return (int32_t)((value ^ sign) - sign);
}

/* Execute the emitted 19-word matcher with a deliberately small instruction
 * model. This validates the encoded branches and all ten table entries without
 * executing writable memory or depending on an emulator package. */
static int model_whitelist_stub(const char *input) {
    const unsigned char *code = fake_memory + g_func_off;
    uintptr_t table = 0;
    if (!exact_whitelist_stub_shape(code, WHITELIST_STUB_BYTES) ||
        !decode_stub_table(FAKE_BASE + g_func_off, code, &table)) {
        return -1;
    }

    uintptr_t x0 = (uintptr_t)input;
    uintptr_t x1 = table;
    uintptr_t x3 = 0;
    uintptr_t x4 = 0;
    uint32_t w2 = 0, w5 = 0, w6 = 0;
    int not_equal = 0;
    int pc = 2;
    for (int steps = 0; steps < 4096; ++steps) {
        uint32_t word = load_u32le(code + (size_t)pc * sizeof(uint32_t));
        switch (pc) {
            case 2:
                pc = x0 ? pc + 1 :
                     pc + sign_extend_branch((word >> 5) & 0x7ffffu, 19);
                break;
            case 3:
                w2 = (word >> 5) & 0xffffu;
                pc++;
                break;
            case 4:
                pc = w2 ? pc + 1 :
                     pc + sign_extend_branch((word >> 5) & 0x7ffffu, 19);
                break;
            case 5: {
                size_t offset = 0;
                if (!fake_range(x1, sizeof(x3), &offset)) return -1;
                memcpy(&x3, fake_memory + offset, sizeof(x3));
                x1 += sizeof(x3);
                pc++;
                break;
            }
            case 6:
                pc = x3 ? pc + 1 :
                     pc + sign_extend_branch((word >> 5) & 0x7ffffu, 19);
                break;
            case 7:
                x4 = x0;
                pc++;
                break;
            case 8:
                w5 = *(const unsigned char *)x4;
                x4++;
                pc++;
                break;
            case 9: {
                size_t offset = 0;
                if (!fake_range(x3, 1, &offset)) return -1;
                w6 = fake_memory[offset];
                x3++;
                pc++;
                break;
            }
            case 10:
                not_equal = w5 != w6;
                pc++;
                break;
            case 11:
                pc = not_equal ?
                     pc + sign_extend_branch((word >> 5) & 0x7ffffu, 19) : pc + 1;
                break;
            case 12:
                pc = w5 ?
                     pc + sign_extend_branch((word >> 5) & 0x7ffffu, 19) : pc + 1;
                break;
            case 13:
                x0 = 1;
                pc++;
                break;
            case 14:
                return (int)(uint32_t)x0;
            case 15:
                w2--;
                pc++;
                break;
            case 16:
                pc += sign_extend_branch(word & 0x03ffffffu, 26);
                break;
            case 17:
                x0 = 0;
                pc++;
                break;
            case 18:
                return (int)(uint32_t)x0;
            default:
                return -1;
        }
    }
    return -1;
}

static int test_ten_slot_matcher_semantics(void) {
    reset_fake();
    memset(fake_memory + slot_off(4), 0, 64);
    if (!write_whitelist_stub_code(42, FAKE_BASE)) {
        fprintf(stderr, "FAIL matcher semantic setup\n");
        return 0;
    }

    static const struct {
        const char *package_name;
        int expected;
        const char *label;
    } cases[] = {
        {"com.example.slot1", 1, "slot-1"},
        {"com.example.slot7", 1, "slot-7"},
        {"com.example.slot10", 1, "slot-10"},
        {"com.example.slot5", 0, "disabled-slot"},
        {"com.example.slot1.extra", 0, "longer-suffix"},
        {"com.example.slot", 0, "shorter-prefix"},
        {"org.example.missing", 0, "missing"},
        {"", 0, "empty-input"},
        {NULL, 0, "null-input"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        int actual = model_whitelist_stub(cases[i].package_name);
        if (actual != cases[i].expected) {
            fprintf(stderr, "FAIL matcher %s expected=%d actual=%d\n",
                    cases[i].label, cases[i].expected, actual);
            return 0;
        }
    }
    return 1;
}

static int test_outer_second_cache_failure(void) {
    reset_fake();
    configure_owned_cave();
    unsigned char before[FAKE_SIZE];
    memcpy(before, fake_memory, sizeof(before));

    whitelist_transaction_t transaction;
    if (!whitelist_transaction_begin(42, FAKE_BASE, &transaction)) {
        fprintf(stderr, "FAIL outer transaction snapshot\n");
        return 0;
    }

    char *packages[MAX_SLOTS];
    char values[MAX_SLOTS][64];
    for (int i = 0; i < MAX_SLOTS; ++i) {
        snprintf(values[i], sizeof(values[i]), "org.example.outer%d", i + 1);
        packages[i] = values[i];
    }
    if (!apply_strings(42, FAKE_BASE, packages)) {
        fprintf(stderr, "FAIL outer transaction string setup\n");
        return 0;
    }

    g_test_icache_override = ICACHE_REMOTE_IVAU;
    g_test_icache_fail_call = 2;
    g_test_icache_calls = 0;
    if (install_whitelist_stub(42, FAKE_BASE) != 0 || g_test_icache_calls != 2) {
        fprintf(stderr, "FAIL second cache fault was not observed\n");
        return 0;
    }
    if (!whitelist_transaction_restore(42, FAKE_BASE, &transaction)) {
        fprintf(stderr, "FAIL outer rollback after second cache fault\n");
        return 0;
    }
    return expect_equal(before, "outer-second-cache", 2);
}

static int test_executable_tail_layout(void) {
    static const uintptr_t rx_ends[] = {
        0x41FC30u, 0x420330u, 0x420070u, 0x4200D0u
    };
    static const uintptr_t next_loads[] = {
        0x420000u, 0x424000u, 0x424000u, 0x424000u
    };
    static const uintptr_t expected_concurrent[] = {
        0x41FEC0u, 0x420EC0u, 0x420EC0u, 0x420EC0u
    };
    static const uintptr_t expected_cache[] = {
        0x41FF90u, 0x420F90u, 0x420F90u, 0x420F90u
    };
    for (size_t i = 0; i < sizeof(rx_ends) / sizeof(rx_ends[0]); ++i) {
        Elf64_Phdr ph[2] = {{0}};
        ph[0].p_type = PT_LOAD;
        ph[0].p_flags = PF_R | PF_X;
        ph[0].p_offset = 0x180000u;
        ph[0].p_vaddr = 0x180000u;
        ph[0].p_filesz = rx_ends[i] - 0x180000u;
        ph[0].p_memsz = ph[0].p_filesz;
        ph[1].p_type = PT_LOAD;
        ph[1].p_flags = PF_R | PF_W;
        ph[1].p_offset = next_loads[i];
        ph[1].p_vaddr = next_loads[i];
        ph[1].p_filesz = 0x1000u;
        ph[1].p_memsz = 0x1000u;
        executable_tail_layout_t layout;
        if (!derive_executable_tail_layout(ph, 2, 4096u, &layout) ||
            layout.segment_file_end != rx_ends[i] ||
            layout.segment_mem_end != rx_ends[i] ||
            layout.concurrent_off != expected_concurrent[i] ||
            layout.cache_off != expected_cache[i] ||
            layout.concurrent_off + UPDATE_CONCURRENT_HELPER_BYTES >
                layout.cache_off ||
            layout.cache_off + CACHE_HELPER_TOTAL_BYTES > layout.tail_end) {
            fprintf(stderr, "FAIL executable tail layout fixture=%lu\n",
                    (unsigned long)i);
            return 0;
        }
        ph[1].p_vaddr = layout.concurrent_off;
        if (derive_executable_tail_layout(ph, 2, 4096u, &layout)) {
            fprintf(stderr,
                    "FAIL executable tail accepted PT_LOAD overlap fixture=%lu\n",
                    (unsigned long)i);
            return 0;
        }
    }
    return 1;
}

static int test_checked_absolute_ranges(void) {
    uintptr_t start = 0, end = 0;
    if (!checked_absolute_range(0x1000u, 0x200u, 0x30u,
                                &start, &end) ||
        start != 0x1200u || end != 0x1230u) {
        fprintf(stderr, "FAIL checked absolute range valid case\n");
        return 0;
    }
    if (checked_absolute_range(UINTPTR_MAX - 0x10u, 0x11u, 1u,
                               &start, &end)) {
        fprintf(stderr, "FAIL checked absolute range base wrap accepted\n");
        return 0;
    }
    if (checked_absolute_range(UINTPTR_MAX - 0x20u, 0x10u, 0x11u,
                               &start, &end)) {
        fprintf(stderr, "FAIL checked absolute range length wrap accepted\n");
        return 0;
    }
    return 1;
}

static int write_temp_blob(const unsigned char *data, size_t size) {
    char path[] = "/data/local/tmp/a2h_layout_XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    unlink(path);
    size_t written = 0;
    while (written < size) {
        ssize_t rc = write(fd, data + written, size - written);
        if (rc <= 0) {
            close(fd);
            return -1;
        }
        written += (size_t)rc;
    }
    return fd;
}

static void install_fake_plt(uintptr_t target) {
    static const uint32_t words[] = {
        0x90000010u, 0xF9400211u, 0x91000210u, 0xD61F0220u
    };
    memcpy(fake_memory + target, words, sizeof(words));
}

static void place_policy_layout(unsigned char *blob, size_t size,
                                intptr_t delta) {
    size_t count_load = 0;
    size_t count_cbz = 0;
    size_t manager_init = 0;
    size_t slot_index_init = 0;
    size_t slot_count_reload = 0;
    size_t slot_index_step = 0;
    size_t slot_table_load = 0;
    size_t slot_stream_load = 0;
    size_t device_begin_load = 0;
    size_t device_end_load = 0;
    size_t idle = 0;
    size_t game = 0;
    if (!shifted_local_offset(POLICY_IDLE_COUNT_LOAD_OFF, delta, size,
                              sizeof(uint32_t), &count_load) ||
        !shifted_local_offset(POLICY_IDLE_COUNT_CBZ_OFF, delta, size,
                              sizeof(uint32_t) * 2u, &count_cbz) ||
        !shifted_local_offset(POLICY_MANAGER_INIT_OFF, delta, size,
                              sizeof(uint32_t), &manager_init) ||
        !shifted_local_offset(POLICY_SLOT_INDEX_INIT_OFF, delta, size,
                              sizeof(uint32_t), &slot_index_init) ||
        !shifted_local_offset(POLICY_SLOT_COUNT_RELOAD_OFF, delta, size,
                              sizeof(uint32_t), &slot_count_reload) ||
        !shifted_local_offset(POLICY_SLOT_INDEX_STEP_OFF, delta, size,
                              sizeof(uint32_t), &slot_index_step) ||
        !shifted_local_offset(POLICY_SLOT_TABLE_LOAD_OFF, delta, size,
                              sizeof(uint32_t), &slot_table_load) ||
        !shifted_local_offset(POLICY_SLOT_STREAM_LOAD_OFF, delta, size,
                              sizeof(uint32_t), &slot_stream_load) ||
        !shifted_local_offset(POLICY_DEVICE_BEGIN_LOAD_OFF, delta, size,
                              sizeof(uint32_t), &device_begin_load) ||
        !shifted_local_offset(POLICY_DEVICE_END_LOAD_OFF, delta, size,
                              sizeof(uint32_t), &device_end_load) ||
        !shifted_local_offset(IDLE_CLEAR_BRANCH_PATCH_OFF, delta, size,
                              sizeof(IDLE_CLEAR_DISK_CONTEXT), &idle) ||
        !shifted_local_offset(GAME_POLICY_PATCH_OFF, delta, size,
                              GAME_POLICY_PATCH_BYTES, &game)) {
        return;
    }
    store_u32le(blob + count_load, POLICY_IDLE_COUNT_LOAD);
    store_u32le(blob + count_cbz, POLICY_IDLE_COUNT_CBZ);
    store_u32le(blob + manager_init, POLICY_MANAGER_INIT);
    store_u32le(blob + slot_index_init, POLICY_SLOT_INDEX_INIT);
    store_u32le(blob + slot_count_reload, POLICY_SLOT_COUNT_RELOAD);
    store_u32le(blob + slot_index_step, POLICY_SLOT_INDEX_STEP);
    store_u32le(blob + slot_table_load, POLICY_SLOT_TABLE_LOAD);
    store_u32le(blob + slot_stream_load, POLICY_SLOT_STREAM_LOAD);
    store_u32le(blob + device_begin_load, POLICY_DEVICE_BEGIN_LOAD);
    store_u32le(blob + device_end_load, POLICY_DEVICE_END_LOAD);
    memcpy(blob + idle, IDLE_CLEAR_DISK_CONTEXT,
           sizeof(IDLE_CLEAR_DISK_CONTEXT));
    memcpy(blob + game, GAME_POLICY_STOCK, GAME_POLICY_PATCH_BYTES);
}

static int test_dynamic_semantic_layouts(void) {
    enum { SHIFT = 0x40 };
    reset_fake();
    g_rx_start = FAKE_BASE;
    g_rx_end = FAKE_BASE + FAKE_SIZE;
    install_fake_plt(0x800u);
    install_fake_plt(0x900u);
    install_fake_plt(0xA00u);
    install_fake_plt(0xB00u);

    size_t update_size = UPDATE_A2H_MODE_FUNC_BYTES + SHIFT;
    unsigned char *update = (unsigned char *)calloc(1, update_size);
    if (!update) return 0;
    size_t update_flags = UPDATE_FLAGS_PATCH_OFF + SHIFT;
    size_t update_policy = UPDATE_APP_POLICY_PATCH_OFF + SHIFT;
    size_t update_allowed = UPDATE_A2H_ALLOWED_BL_OFF + SHIFT;
    memcpy(update + update_flags, UPDATE_FLAGS_STOCK,
           sizeof(UPDATE_FLAGS_STOCK));
    memcpy(update + update_policy, UPDATE_APP_POLICY_DISK_TEMPLATE,
           UPDATE_APP_POLICY_BYTES);
    uint32_t branch = 0;
    if (!encode_aarch64_bl(0x2000u + update_policy +
                           UPDATE_APP_POLICY_DISK_BL_OFF,
                           0x800u, &branch)) {
        free(update);
        return 0;
    }
    store_u32le(update + update_policy + UPDATE_APP_POLICY_DISK_BL_OFF,
                branch);
    if (!encode_aarch64_bl(0x2000u + update_allowed, 0x900u, &branch)) {
        free(update);
        return 0;
    }
    store_u32le(update + update_allowed, branch);
    int fd = write_temp_blob(update, update_size);
    free(update);
    elf_a2h_symbol_t update_sym = {0x2000u, 0u, update_size};
    intptr_t delta = 0;
    int update_ok = fd >= 0 && locate_update_layout(
        fd, 42, FAKE_BASE, &update_sym, &delta) && delta == SHIFT;
    if (fd >= 0) close(fd);

    size_t policy_size = IS_A2H_ALLOWED_FUNC_BYTES + SHIFT;
    unsigned char *policy = (unsigned char *)calloc(1, policy_size);
    if (!policy) return 0;
    place_policy_layout(policy, policy_size, SHIFT);
    fd = write_temp_blob(policy, policy_size);
    elf_a2h_symbol_t policy_sym = {0x3000u, 0u, policy_size};
    delta = 0;
    int policy_ok = fd >= 0 && locate_policy_layout(
        fd, &policy_sym, &delta) && delta == SHIFT;
    if (fd >= 0) close(fd);
    free(policy);

    size_t stream_size = STREAM_SET_PARAMETERS_FUNC_BYTES + SHIFT;
    unsigned char *stream = (unsigned char *)calloc(1, stream_size);
    if (!stream) return 0;
    store_u32le(stream + STREAM_APP_MAP_MANAGER_OFF + SHIFT,
                STREAM_APP_MAP_MANAGER_ADD);
    store_u32le(stream + STREAM_NEW_NODE_MANAGER_OFF + SHIFT,
                STREAM_APP_MAP_MANAGER_ADD);
    memcpy(stream + STREAM_UPDATE_CALL_OFF + SHIFT,
           STREAM_UPDATE_CALL_TEMPLATE, STREAM_UPDATE_CALL_BYTES);
    if (!encode_aarch64_bl(0x4000u + STREAM_UPDATE_BL_OFF + SHIFT,
                           0xA00u, &branch)) {
        free(stream);
        return 0;
    }
    store_u32le(stream + STREAM_UPDATE_BL_OFF + SHIFT, branch);
    for (size_t i = 0; i < STREAM_EVENT_PATCH_COUNT; ++i) {
        memcpy(stream + STREAM_EVENT_PATCH_OFFSETS[i] + SHIFT,
               STREAM_EVENT_STOCK_TEMPLATE[i],
               STREAM_EVENT_PATCH_SIZES[i]);
    }
    if (!encode_aarch64_bl(
            0x4000u + STREAM_EVENT_PATCH_OFFSETS[0] + SHIFT + 16u,
            0xB00u, &branch)) {
        free(stream);
        return 0;
    }
    store_u32le(stream + STREAM_EVENT_PATCH_OFFSETS[0] + SHIFT + 16u,
                branch);
    fd = write_temp_blob(stream, stream_size);
    free(stream);
    elf_a2h_symbol_t stream_sym = {0x4000u, 0u, stream_size};
    delta = 0;
    int stream_ok = fd >= 0 && locate_stream_layout(
        fd, 42, FAKE_BASE, &stream_sym, &delta) && delta == SHIFT;
    if (fd >= 0) close(fd);

    size_t output_size = UPDATE_OUTPUT_POOL_FUNC_BYTES + SHIFT;
    unsigned char *output = (unsigned char *)calloc(1, output_size);
    if (!output) return 0;
    store_u32le(output + OUTPUT_POOL_MANAGER_ARG_OFF + SHIFT,
                OUTPUT_POOL_MANAGER_ARG_MOV);
    memcpy(output + OUTPUT_POOL_TAIL_PATCH_OFF + SHIFT,
           OUTPUT_POOL_TAIL_STOCK_TEMPLATE,
           sizeof(OUTPUT_POOL_TAIL_STOCK_TEMPLATE));
    fd = write_temp_blob(output, output_size);
    elf_a2h_symbol_t output_sym = {0x5000u, 0u, output_size};
    delta = 0;
    int output_ok = fd >= 0 && locate_output_layout(
        fd, &output_sym, &delta) && delta == SHIFT;
    if (fd >= 0) close(fd);
    free(output);

    size_t duplicate_size = IS_A2H_ALLOWED_FUNC_BYTES + 0x240u;
    unsigned char *duplicate = (unsigned char *)calloc(1, duplicate_size);
    if (!duplicate) return 0;
    place_policy_layout(duplicate, duplicate_size, 0);
    place_policy_layout(duplicate, duplicate_size, 0x200);
    fd = write_temp_blob(duplicate, duplicate_size);
    free(duplicate);
    elf_a2h_symbol_t duplicate_sym = {0x6000u, 0u, duplicate_size};
    delta = 0;
    int duplicate_rejected = fd >= 0 &&
        !locate_policy_layout(fd, &duplicate_sym, &delta);
    if (fd >= 0) close(fd);

    if (!update_ok || !policy_ok || !stream_ok || !output_ok ||
        !duplicate_rejected) {
        fprintf(stderr,
                "FAIL dynamic layouts update=%d policy=%d stream=%d output=%d duplicate=%d\n",
                update_ok, policy_ok, stream_ok, output_ok,
                duplicate_rejected);
        return 0;
    }
    return 1;
}

static int test_update_flags_overlay(void) {
    static const uintptr_t updates[] = {
        0x3974A0u, 0x397B20u, 0x397800u, 0x397860u
    };
    static const uintptr_t policies[] = {
        0x396C60u, 0x3972E0u, 0x396FC0u, 0x397020u
    };
    static const uintptr_t helpers[] = {
        0x41FEC0u, 0x420EC0u, 0x420EC0u, 0x420EC0u
    };
    unsigned char patched[sizeof(UPDATE_FLAGS_STOCK)];
    unsigned char guarded_legacy[sizeof(UPDATE_FLAGS_STOCK)];
    unsigned char idle_count_branch[sizeof(POLICY_IDLE_COUNT_CBZ_STOCK)];
    unsigned char idle_branch[IDLE_CLEAR_BRANCH_BYTES];
    for (size_t i = 0; i < sizeof(updates) / sizeof(updates[0]); ++i) {
        uint32_t expected_to_helper = 0;
        uint32_t expected_to_idle = 0;
        uint32_t expected_count_to_zero_init = 0;
        uint32_t expected_legacy_resume = 0;
        if (!build_idle_clear_overlay(updates[i], policies[i], helpers[i],
                                      patched,
                                      guarded_legacy,
                                      idle_count_branch,
                                      idle_branch) ||
            !encode_aarch64_cbz_like(
                policies[i] + POLICY_IDLE_COUNT_CBZ_OFF,
                helpers[i] + CONCURRENT_ZERO_INIT_OFF,
                POLICY_IDLE_COUNT_CBZ,
                &expected_count_to_zero_init) ||
            !encode_aarch64_b(
                policies[i] + IDLE_CLEAR_BRANCH_PATCH_OFF,
                updates[i] + UPDATE_IDLE_HELPER_OFF,
                &expected_to_helper) ||
            !encode_aarch64_b(
                updates[i] + UPDATE_IDLE_HELPER_BRANCH_OFF,
                helpers[i] + CONCURRENT_IDLE_HELPER_OFF,
                &expected_to_idle) ||
            !encode_aarch64_b(
                updates[i] + UPDATE_FLAGS_PATCH_OFF + 28u,
                policies[i] + IDLE_CLEAR_RESUME_OFF,
                &expected_legacy_resume) ||
            load_u32le(patched) != 0xB9400C08u ||
            load_u32le(patched + 4u) != 0x721E091Fu ||
            load_u32le(patched + 8u) != 0x54000181u ||
            load_u32le(patched + 12u) != 0x37900168u ||
            load_u32le(patched + 16u) != 0x14000004u ||
            load_u32le(patched + 20u) != 0x2A1F03F3u ||
            load_u32le(patched + 24u) != expected_to_idle ||
            load_u32le(idle_count_branch) !=
                expected_count_to_zero_init ||
            load_u32le(patched + 28u) != 0xD503201Fu ||
            load_u32le(guarded_legacy + 24u) != 0x3914641Fu ||
            load_u32le(guarded_legacy + 28u) !=
                expected_legacy_resume ||
            load_u32le(idle_branch) != expected_to_helper) {
            fprintf(stderr,
                    "FAIL idle clear overlay fixture=%lu generation\n",
                    (unsigned long)i);
            return 0;
        }
    }
    if (build_idle_clear_overlay(
            UINTPTR_MAX - UPDATE_FLAGS_PATCH_OFF,
            policies[0], helpers[0], patched, guarded_legacy,
            idle_count_branch, idle_branch)) {
        fprintf(stderr, "FAIL idle clear overlay address wrap accepted\n");
        return 0;
    }
    if (memcmp(patched, UPDATE_FLAGS_GAME_HANDOFF_LEGACY,
               sizeof(patched)) == 0 ||
        memcmp(patched, guarded_legacy, sizeof(patched)) == 0 ||
        load_u32le(UPDATE_FLAGS_LEGACY) != 0x721E091Fu) {
        fprintf(stderr, "FAIL output overlay migration shapes\n");
        return 0;
    }

    unsigned char disk[UPDATE_A2H_MODE_FUNC_BYTES] = {0};
    unsigned char live[UPDATE_A2H_MODE_FUNC_BYTES] = {0};
    memcpy(disk + UPDATE_FLAGS_PATCH_OFF, UPDATE_FLAGS_STOCK,
           sizeof(UPDATE_FLAGS_STOCK));
    const owned_overlay_region_t region = {
        UPDATE_FLAGS_PATCH_OFF, UPDATE_FLAGS_STOCK, patched,
        UPDATE_FLAGS_GAME_HANDOFF_LEGACY,
        guarded_legacy, UPDATE_FLAGS_LEGACY,
        sizeof(UPDATE_FLAGS_STOCK)
    };
    int state = -1;
    memcpy(live, disk, sizeof(live));
    memcpy(live + UPDATE_FLAGS_PATCH_OFF,
           UPDATE_FLAGS_GAME_HANDOFF_LEGACY,
           sizeof(UPDATE_FLAGS_GAME_HANDOFF_LEGACY));
    if (!exact_multi_overlay(disk, live, sizeof(disk), &region, 1, &state) ||
        state != OVERLAY_ALTERNATE) {
        fprintf(stderr, "FAIL public game-spatial candidate ownership\n");
        return 0;
    }
    memcpy(live + UPDATE_FLAGS_PATCH_OFF, guarded_legacy,
           sizeof(guarded_legacy));
    state = -1;
    if (!exact_multi_overlay(disk, live, sizeof(disk), &region, 1, &state) ||
        state != OVERLAY_ALTERNATE2) {
        fprintf(stderr, "FAIL guarded idle candidate ownership\n");
        return 0;
    }
    memcpy(live + UPDATE_FLAGS_PATCH_OFF, patched, sizeof(patched));
    state = -1;
    if (!exact_multi_overlay(disk, live, sizeof(disk), &region, 1, &state) ||
        state != OVERLAY_PATCHED) {
        fprintf(stderr, "FAIL eligible output overlay ownership\n");
        return 0;
    }
    memcpy(live + UPDATE_FLAGS_PATCH_OFF, UPDATE_FLAGS_LEGACY,
           sizeof(UPDATE_FLAGS_LEGACY));
    state = -1;
    if (!exact_multi_overlay(disk, live, sizeof(disk), &region, 1, &state) ||
        state != OVERLAY_LEGACY) {
        fprintf(stderr, "FAIL low-byte output overlay ownership\n");
        return 0;
    }
    live[UPDATE_FLAGS_PATCH_OFF + 17u] ^= 1u;
    if (exact_multi_overlay(disk, live, sizeof(disk), &region, 1, &state)) {
        fprintf(stderr, "FAIL foreign output overlay accepted\n");
        return 0;
    }

    unsigned char policy_disk[IS_A2H_ALLOWED_FUNC_BYTES] = {0};
    unsigned char policy_live[IS_A2H_ALLOWED_FUNC_BYTES] = {0};
    memcpy(policy_disk + IDLE_CLEAR_BRANCH_PATCH_OFF,
           IDLE_CLEAR_BRANCH_STOCK, IDLE_CLEAR_BRANCH_BYTES);
    memcpy(policy_disk + GAME_POLICY_PATCH_OFF, GAME_POLICY_STOCK,
           sizeof(GAME_POLICY_STOCK));
    unsigned char policy_concurrent[GAME_POLICY_PATCH_BYTES];
    memcpy(policy_concurrent, GAME_POLICY_CONCURRENT_TEMPLATE,
           sizeof(policy_concurrent));
    uint32_t policy_to_helper = 0;
    if (!encode_aarch64_b(
            policies[3] + GAME_POLICY_PATCH_OFF,
            helpers[3],
            &policy_to_helper)) {
        fprintf(stderr, "FAIL concurrent policy branch generation\n");
        return 0;
    }
    store_u32le(policy_concurrent + GAME_POLICY_HELPER_BRANCH_OFF,
                policy_to_helper);
    if (memcmp(policy_concurrent + GAME_POLICY_ENTRY_BYTES,
               GAME_POLICY_STOCK + GAME_POLICY_ENTRY_BYTES,
               GAME_POLICY_PATCH_BYTES - GAME_POLICY_ENTRY_BYTES) != 0 ||
        memcmp(GAME_POLICY_RELAXED + GAME_POLICY_ENTRY_BYTES,
               GAME_POLICY_STOCK + GAME_POLICY_ENTRY_BYTES,
               GAME_POLICY_PATCH_BYTES - GAME_POLICY_ENTRY_BYTES) != 0 ||
        load_u32le(policy_concurrent + 24u) != 0xF9401529u) {
        fprintf(stderr, "FAIL concurrent policy canary tail preservation\n");
        return 0;
    }
    const owned_overlay_region_t policy_regions[] = {
        {IDLE_CLEAR_BRANCH_PATCH_OFF, IDLE_CLEAR_BRANCH_STOCK,
         idle_branch, NULL, NULL, NULL, IDLE_CLEAR_BRANCH_BYTES},
        {GAME_POLICY_PATCH_OFF, GAME_POLICY_STOCK, policy_concurrent,
         GAME_POLICY_RELAXED, NULL, GAME_POLICY_PUBLIC_RELAXED,
         sizeof(GAME_POLICY_STOCK)}
    };
    int policy_states[2] = {-1, -1};
    memcpy(policy_live, policy_disk, sizeof(policy_live));
    memcpy(policy_live + IDLE_CLEAR_BRANCH_PATCH_OFF,
           idle_branch, IDLE_CLEAR_BRANCH_BYTES);
    memcpy(policy_live + GAME_POLICY_PATCH_OFF, policy_concurrent,
           sizeof(policy_concurrent));
    if (!exact_multi_overlay(
            policy_disk, policy_live, sizeof(policy_disk), policy_regions,
            sizeof(policy_regions) / sizeof(policy_regions[0]),
            policy_states) ||
        policy_states[0] != OVERLAY_PATCHED ||
        policy_states[1] != OVERLAY_PATCHED) {
        fprintf(stderr, "FAIL policy idle-clear ownership\n");
        return 0;
    }
    memcpy(policy_live + GAME_POLICY_PATCH_OFF, GAME_POLICY_RELAXED,
           sizeof(GAME_POLICY_RELAXED));
    if (!exact_multi_overlay(
            policy_disk, policy_live, sizeof(policy_disk), policy_regions,
            sizeof(policy_regions) / sizeof(policy_regions[0]),
            policy_states) || policy_states[1] != OVERLAY_ALTERNATE) {
        fprintf(stderr, "FAIL relaxed policy ownership\n");
        return 0;
    }
    memcpy(policy_live + GAME_POLICY_PATCH_OFF,
           GAME_POLICY_PUBLIC_RELAXED,
           sizeof(GAME_POLICY_PUBLIC_RELAXED));
    if (!exact_multi_overlay(
            policy_disk, policy_live, sizeof(policy_disk), policy_regions,
            sizeof(policy_regions) / sizeof(policy_regions[0]),
            policy_states) || policy_states[1] != OVERLAY_LEGACY) {
        fprintf(stderr, "FAIL public relaxed policy migration ownership\n");
        return 0;
    }
    policy_live[IDLE_CLEAR_BRANCH_PATCH_OFF + 1u] ^= 1u;
    if (exact_multi_overlay(
            policy_disk, policy_live, sizeof(policy_disk), policy_regions,
            sizeof(policy_regions) / sizeof(policy_regions[0]),
            policy_states)) {
        fprintf(stderr, "FAIL foreign idle-clear branch accepted\n");
        return 0;
    }
    return 1;
}

static int test_concurrent_helper_generation(void) {
    static const uintptr_t updates[] = {
        0x3974A0u, 0x397B20u, 0x397800u, 0x397860u
    };
    static const uintptr_t policies[] = {
        0x396C60u, 0x3972E0u, 0x396FC0u, 0x397020u
    };
    static const uintptr_t streams[] = {
        0x361870u, 0x361F00u, 0x361B20u, 0x361B80u
    };
    static const uintptr_t helpers[] = {
        0x41FEC0u, 0x420EC0u, 0x420EC0u, 0x420EC0u
    };
    static const uintptr_t opens[] = {
        0x366B50u, 0x3671D0u, 0x366DF0u, 0x366E50u
    };
    static const unsigned char new_node_stock[][28] = {
        {0x38,0x06,0x00,0xB0,0x18,0xC7,0x46,0xF9,
         0x08,0x03,0x40,0x39,0x88,0x01,0x08,0x36,
         0xE6,0xCB,0x40,0xF9,0xC3,0xEC,0xFF,0xF0,
         0x63,0xA8,0x1D,0x91},
        {0x58,0x06,0x00,0x90,0x18,0xC7,0x46,0xF9,
         0x08,0x03,0x40,0x39,0x88,0x01,0x08,0x36,
         0xE6,0xCB,0x40,0xF9,0xC3,0xEC,0xFF,0xD0,
         0x63,0xB0,0x29,0x91},
        {0x58,0x06,0x00,0xB0,0x18,0xC7,0x46,0xF9,
         0x08,0x03,0x40,0x39,0x88,0x01,0x08,0x36,
         0xE6,0xCB,0x40,0xF9,0xC3,0xEC,0xFF,0xF0,
         0x63,0x9C,0x25,0x91},
        {0x58,0x06,0x00,0xB0,0x18,0xC7,0x46,0xF9,
         0x08,0x03,0x40,0x39,0x88,0x01,0x08,0x36,
         0xE6,0xCB,0x40,0xF9,0xC3,0xEC,0xFF,0xF0,
         0x63,0x54,0x26,0x91}
    };
    for (size_t i = 0; i < sizeof(updates) / sizeof(updates[0]); ++i) {
        unsigned char stock[STREAM_EVENT_PATCH_COUNT]
                           [STREAM_EVENT_PATCH_MAX_BYTES] = {{0}};
        unsigned char helper[UPDATE_CONCURRENT_HELPER_BYTES];
        unsigned char helper_previous[UPDATE_CONCURRENT_HELPER_BYTES];
        unsigned char helper_previous2[UPDATE_CONCURRENT_HELPER_BYTES];
        unsigned char helper_legacy[UPDATE_CONCURRENT_HELPER_BYTES];
        unsigned char helper_legacy2[UPDATE_CONCURRENT_HELPER_BYTES];
        unsigned char policy[GAME_POLICY_PATCH_BYTES];
        unsigned char events[STREAM_EVENT_PATCH_COUNT]
                            [STREAM_EVENT_PATCH_MAX_BYTES];
        unsigned char public_events[STREAM_EVENT_PATCH_COUNT]
                                   [STREAM_EVENT_PATCH_MAX_BYTES];
        unsigned char handoff_event[STREAM_EVENT_PATCH_MAX_BYTES];
        unsigned char open_event[STREAM_OPEN_EVENT_BYTES];
        memcpy(stock, STREAM_EVENT_STOCK_TEMPLATE, sizeof(stock));
        memcpy(stock[1], new_node_stock[i], sizeof(new_node_stock[i]));
        uint32_t strlen_call = 0;
        uintptr_t update_target = streams[i] + STREAM_UPDATE_CALL_OFF;
        uintptr_t open_update_target = streams[i] + 0x6000u;
        if (!encode_aarch64_bl(
                streams[i] + STREAM_EVENT_PATCH_OFFSETS[0] + 16u,
                streams[i] + 0x9000u, &strlen_call)) {
            fprintf(stderr, "FAIL stream fixture BL=%lu\n",
                    (unsigned long)i);
            return 0;
        }
        store_u32le(stock[0] + 16u, strlen_call);
        if (!build_concurrent_helpers(
                helpers[i], policies[i], streams[i], opens[i],
                STREAM_OPEN_EPILOGUE_EXPECTED_OFF,
                update_target, open_update_target, stock,
                helper, helper_previous, helper_previous2,
                helper_legacy, helper_legacy2,
                policy, events,
                public_events,
                handoff_event, open_event)) {
            fprintf(stderr, "FAIL concurrent helper fixture=%lu\n",
                    (unsigned long)i);
            return 0;
        }
        uint32_t expected_policy_entry = 0;
        uint32_t expected_pending_decay = 0;
        uint32_t expected_helper_entry = 0;
        uint32_t expected_policy_return = 0;
        uint32_t expected_legacy_policy_return = 0;
        uint32_t expected_idle_return = 0;
        uint32_t expected_zero_init_return = 0;
        uint32_t expected_event_return[2] = {0};
        uint32_t expected_final_event = 0;
        uint32_t expected_public_return = 0;
        uint32_t expected_open_call = 0;
        uint32_t expected_open_update = 0;
        uint32_t expected_open_return = 0;
        if (!encode_aarch64_b(
                policies[i] + GAME_POLICY_PATCH_OFF,
                helpers[i] + CONCURRENT_ENTRY_B_OFF,
                &expected_policy_entry) ||
            !encode_aarch64_b(
                helpers[i] + CONCURRENT_PENDING_DECAY_B_OFF,
                helpers[i] + CONCURRENT_CORE_OFF,
                &expected_pending_decay) ||
            !encode_aarch64_b(
                helpers[i] + CONCURRENT_ENTRY_B_OFF,
                helpers[i] + CONCURRENT_MAIN_OFF,
                &expected_helper_entry) ||
            !encode_aarch64_b(
                helpers[i] + CONCURRENT_POLICY_RETURN_B_OFF,
                policies[i] + GAME_POLICY_PATCH_OFF + 4u,
                &expected_policy_return) ||
            !encode_aarch64_b(
                helpers[i] + CONCURRENT_LEGACY_SHIFT +
                    CONCURRENT_LEGACY_POLICY_RETURN_B_OFF,
                policies[i] + GAME_POLICY_PATCH_OFF + 4u,
                &expected_legacy_policy_return) ||
            !encode_aarch64_b(
                helpers[i] + CONCURRENT_IDLE_RETURN_B_OFF,
                policies[i] + IDLE_CLEAR_RESUME_OFF,
                &expected_idle_return) ||
            !encode_aarch64_b(
                helpers[i] + CONCURRENT_ZERO_INIT_RETURN_B_OFF,
                policies[i] + IDLE_CLEAR_BRANCH_PATCH_OFF,
                &expected_zero_init_return) ||
            !encode_aarch64_b(
                streams[i] + STREAM_EVENT_PATCH_OFFSETS[0] + 24u,
                update_target, &expected_event_return[0]) ||
            !encode_aarch64_b(
                streams[i] + STREAM_EVENT_PATCH_OFFSETS[1] + 24u,
                update_target, &expected_event_return[1]) ||
            !encode_aarch64_b(
                streams[i] + STREAM_EVENT_PATCH_OFFSETS[2],
                update_target, &expected_final_event) ||
            !encode_aarch64_b(
                streams[i] + STREAM_EVENT_PATCH_OFFSETS[0] + 4u,
                update_target, &expected_public_return) ||
            !encode_aarch64_bl(
                opens[i] + STREAM_OPEN_EPILOGUE_EXPECTED_OFF,
                helpers[i] + CONCURRENT_OPEN_HELPER_OFF,
                &expected_open_call) ||
            !encode_aarch64_bl(
                helpers[i] + CONCURRENT_OPEN_UPDATE_BL_OFF,
                open_update_target, &expected_open_update) ||
            !encode_aarch64_b(
                helpers[i] + CONCURRENT_OPEN_RETURN_B_OFF,
                opens[i] + STREAM_OPEN_EPILOGUE_EXPECTED_OFF +
                    STREAM_OPEN_EVENT_BYTES,
                &expected_open_return) ||
            load_u32le(policy) != expected_policy_entry ||
            load_u32le(helper + CONCURRENT_PENDING_DECAY_B_OFF) !=
                expected_pending_decay ||
            load_u32le(helper + CONCURRENT_ENTRY_B_OFF) !=
                expected_helper_entry ||
            load_u32le(helper + CONCURRENT_POLICY_RETURN_B_OFF) !=
                expected_policy_return ||
            load_u32le(helper + CONCURRENT_IDLE_RETURN_B_OFF) !=
                expected_idle_return ||
            load_u32le(helper + CONCURRENT_ZERO_INIT_OFF) !=
                0xAA0003F4u ||
            load_u32le(helper + CONCURRENT_ZERO_INIT_RETURN_B_OFF) !=
                expected_zero_init_return ||
            load_u32le(helper + CONCURRENT_OPEN_HELPER_OFF) !=
                0x35000074u ||
            load_u32le(helper + 4u) != STREAM_OPEN_MANAGER_LOAD ||
            load_u32le(helper + CONCURRENT_OPEN_UPDATE_BL_OFF) !=
                expected_open_update ||
            load_u32le(helper + 12u) != 0x2A1403E0u ||
            load_u32le(helper + CONCURRENT_OPEN_RETURN_B_OFF) !=
                expected_open_return ||
            !bytes_are_zero(helper_previous, CONCURRENT_PREVIOUS_SHIFT) ||
            memcmp(helper_previous + CONCURRENT_PREVIOUS_SHIFT,
                   UPDATE_CONCURRENT_HELPER_V3_TEMPLATE,
                   CONCURRENT_PENDING_DECAY_B_OFF -
                       CONCURRENT_PREVIOUS_SHIFT) != 0 ||
            load_u32le(helper_previous + CONCURRENT_PENDING_DECAY_B_OFF) !=
                expected_pending_decay ||
            load_u32le(helper_previous + CONCURRENT_ENTRY_B_OFF) !=
                expected_helper_entry ||
            load_u32le(helper_previous + CONCURRENT_POLICY_RETURN_B_OFF) !=
                expected_policy_return ||
            load_u32le(helper_previous + CONCURRENT_IDLE_RETURN_B_OFF) !=
                expected_idle_return ||
            load_u32le(helper_previous + CONCURRENT_ZERO_INIT_RETURN_B_OFF) !=
                expected_zero_init_return ||
            !bytes_are_zero(helper_previous2,
                            CONCURRENT_PREVIOUS2_SHIFT) ||
            memcmp(helper_previous2 + CONCURRENT_PREVIOUS2_SHIFT,
                   UPDATE_CONCURRENT_HELPER_V2_TEMPLATE,
                   CONCURRENT_ENTRY_B_OFF -
                       CONCURRENT_PREVIOUS2_SHIFT) != 0 ||
            load_u32le(helper_previous2 + CONCURRENT_ENTRY_B_OFF) !=
                expected_helper_entry ||
            load_u32le(helper_previous2 + CONCURRENT_POLICY_RETURN_B_OFF) !=
                expected_policy_return ||
            load_u32le(helper_previous2 + CONCURRENT_IDLE_RETURN_B_OFF) !=
                expected_idle_return ||
            load_u32le(helper_previous2 +
                       CONCURRENT_ZERO_INIT_RETURN_B_OFF) !=
                expected_zero_init_return ||
            load_u32le(helper_legacy + CONCURRENT_LEGACY_SHIFT +
                       CONCURRENT_LEGACY_POLICY_RETURN_B_OFF) !=
                expected_legacy_policy_return ||
            load_u32le(helper_legacy + CONCURRENT_ZERO_INIT_OFF) !=
                0xAA0003F4u ||
            load_u32le(helper_legacy +
                       CONCURRENT_ZERO_INIT_RETURN_B_OFF) !=
                expected_zero_init_return ||
            load_u32le(helper_legacy2 + CONCURRENT_ZERO_INIT_OFF) != 0u ||
            load_u32le(helper_legacy2 +
                       CONCURRENT_ZERO_INIT_RETURN_B_OFF) != 0u ||
            load_u32le(events[0] + 24u) != expected_event_return[0] ||
            load_u32le(events[1] + 24u) != expected_event_return[1] ||
            load_u32le(events[2]) != expected_final_event ||
            memcmp(events[0], events[1], 24u) != 0 ||
            load_u32le(public_events[0]) != 0x3914671Fu ||
            load_u32le(public_events[0] + 4u) !=
                expected_public_return ||
            load_u32le(public_events[0] + 4u) != 0x14000118u ||
            memcmp(public_events[1], stock[1],
                   STREAM_EVENT_PATCH_SIZES[1]) != 0 ||
            load_u32le(public_events[2]) != 0x14000051u ||
            memcmp(public_events[0] + 8u, stock[0] + 8u,
                   STREAM_EVENT_PATCH_SIZES[0] - 8u) != 0 ||
            memcmp(handoff_event, STREAM_EVENT_HANDOFF_LEGACY,
                   sizeof(STREAM_EVENT_HANDOFF_LEGACY)) != 0 ||
            memcmp(handoff_event + sizeof(STREAM_EVENT_HANDOFF_LEGACY),
                   stock[0] + sizeof(STREAM_EVENT_HANDOFF_LEGACY),
                   STREAM_EVENT_PATCH_SIZES[0] -
                   sizeof(STREAM_EVENT_HANDOFF_LEGACY)) != 0 ||
            load_u32le(open_event) != expected_open_call) {
            fprintf(stderr, "FAIL concurrent branch fixture=%lu\n",
                    (unsigned long)i);
            return 0;
        }
        unsigned char expected_helper[UPDATE_CONCURRENT_HELPER_BYTES];
        unsigned char expected_previous[UPDATE_CONCURRENT_HELPER_BYTES];
        unsigned char expected_previous2[UPDATE_CONCURRENT_HELPER_BYTES];
        memset(expected_helper, 0, sizeof(expected_helper));
        memcpy(expected_helper, STREAM_OPEN_HELPER_TEMPLATE,
               sizeof(STREAM_OPEN_HELPER_TEMPLATE));
        for (size_t off = sizeof(STREAM_OPEN_HELPER_TEMPLATE);
             off < CONCURRENT_CORE_OFF; off += sizeof(uint32_t)) {
            store_u32le(expected_helper + off, 0xD503201Fu);
        }
        memcpy(expected_helper + CONCURRENT_CORE_OFF,
               UPDATE_CONCURRENT_HELPER_V3_TEMPLATE,
               UPDATE_CONCURRENT_PREVIOUS_BYTES);
        store_u32le(expected_helper + CONCURRENT_OPEN_UPDATE_BL_OFF,
                    expected_open_update);
        store_u32le(expected_helper + CONCURRENT_OPEN_RETURN_B_OFF,
                    expected_open_return);
        store_u32le(expected_helper + CONCURRENT_PENDING_DECAY_B_OFF,
                    expected_pending_decay);
        store_u32le(expected_helper + CONCURRENT_ENTRY_B_OFF,
                    expected_helper_entry);
        store_u32le(expected_helper + CONCURRENT_POLICY_RETURN_B_OFF,
                    expected_policy_return);
        store_u32le(expected_helper + CONCURRENT_IDLE_RETURN_B_OFF,
                    expected_idle_return);
        store_u32le(expected_helper + CONCURRENT_ZERO_INIT_RETURN_B_OFF,
                    expected_zero_init_return);
        memset(expected_previous, 0, sizeof(expected_previous));
        memcpy(expected_previous + CONCURRENT_PREVIOUS_SHIFT,
               UPDATE_CONCURRENT_HELPER_V3_TEMPLATE,
               UPDATE_CONCURRENT_PREVIOUS_BYTES);
        store_u32le(expected_previous + CONCURRENT_PENDING_DECAY_B_OFF,
                    expected_pending_decay);
        store_u32le(expected_previous + CONCURRENT_ENTRY_B_OFF,
                    expected_helper_entry);
        store_u32le(expected_previous + CONCURRENT_POLICY_RETURN_B_OFF,
                    expected_policy_return);
        store_u32le(expected_previous + CONCURRENT_IDLE_RETURN_B_OFF,
                    expected_idle_return);
        store_u32le(expected_previous + CONCURRENT_ZERO_INIT_RETURN_B_OFF,
                    expected_zero_init_return);
        memset(expected_previous2, 0, sizeof(expected_previous2));
        memcpy(expected_previous2 + CONCURRENT_PREVIOUS2_SHIFT,
               UPDATE_CONCURRENT_HELPER_V2_TEMPLATE,
               UPDATE_CONCURRENT_PREVIOUS2_BYTES);
        store_u32le(expected_previous2 + CONCURRENT_ENTRY_B_OFF,
                    expected_helper_entry);
        store_u32le(expected_previous2 + CONCURRENT_POLICY_RETURN_B_OFF,
                    expected_policy_return);
        store_u32le(expected_previous2 + CONCURRENT_IDLE_RETURN_B_OFF,
                    expected_idle_return);
        store_u32le(expected_previous2 +
                    CONCURRENT_ZERO_INIT_RETURN_B_OFF,
                    expected_zero_init_return);
        if (memcmp(helper, expected_helper, sizeof(expected_helper)) != 0 ||
            memcmp(helper_previous, expected_previous,
                   sizeof(expected_previous)) != 0 ||
            memcmp(helper_previous2, expected_previous2,
                   sizeof(expected_previous2)) != 0) {
            fprintf(stderr,
                    "FAIL exact concurrent helper bytes fixture=%lu\n",
                    (unsigned long)i);
            return 0;
        }
    }
    unsigned char corrupt[STREAM_EVENT_PATCH_COUNT]
                         [STREAM_EVENT_PATCH_MAX_BYTES] = {{0}};
    memcpy(corrupt, STREAM_EVENT_STOCK_TEMPLATE, sizeof(corrupt));
    corrupt[0][8] ^= 1u;
    unsigned char helper[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char helper_previous[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char helper_previous2[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char helper_legacy[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char helper_legacy2[UPDATE_CONCURRENT_HELPER_BYTES];
    unsigned char policy[GAME_POLICY_PATCH_BYTES];
    unsigned char events[STREAM_EVENT_PATCH_COUNT]
                        [STREAM_EVENT_PATCH_MAX_BYTES];
    unsigned char public_events[STREAM_EVENT_PATCH_COUNT]
                               [STREAM_EVENT_PATCH_MAX_BYTES];
    unsigned char handoff_event[STREAM_EVENT_PATCH_MAX_BYTES];
    unsigned char open_event[STREAM_OPEN_EVENT_BYTES];
    if (build_concurrent_helpers(
            helpers[0], policies[0], streams[0], opens[0],
            STREAM_OPEN_EPILOGUE_EXPECTED_OFF,
            streams[0] + 0x5000u, streams[0] + 0x6000u,
            corrupt, helper, helper_previous, helper_previous2,
            helper_legacy, helper_legacy2,
            policy, events,
            public_events, handoff_event, open_event)) {
        fprintf(stderr, "FAIL foreign stream app-event shape accepted\n");
        return 0;
    }
    memcpy(corrupt, STREAM_EVENT_STOCK_TEMPLATE, sizeof(corrupt));
    corrupt[1][sizeof(uint32_t)] ^= 1u;
    if (build_concurrent_helpers(
            helpers[0], policies[0], streams[0], opens[0],
            STREAM_OPEN_EPILOGUE_EXPECTED_OFF,
            streams[0] + 0x5000u, streams[0] + 0x6000u,
            corrupt, helper, helper_previous, helper_previous2,
            helper_legacy, helper_legacy2,
            policy, events,
            public_events, handoff_event, open_event)) {
        fprintf(stderr,
                "FAIL foreign stream new-node event shape accepted\n");
        return 0;
    }
    memcpy(corrupt, STREAM_EVENT_STOCK_TEMPLATE, sizeof(corrupt));
    if (build_concurrent_helpers(
            helpers[0], UINTPTR_MAX - GAME_POLICY_PATCH_OFF + 1u,
            streams[0], opens[0], STREAM_OPEN_EPILOGUE_EXPECTED_OFF,
            streams[0] + 0x5000u, streams[0] + 0x6000u, corrupt,
            helper, helper_previous, helper_previous2,
            helper_legacy, helper_legacy2,
            policy, events,
            public_events, handoff_event, open_event)) {
        fprintf(stderr, "FAIL concurrent policy address wrap accepted\n");
        return 0;
    }
    if (build_concurrent_helpers(
            helpers[0], policies[0],
            UINTPTR_MAX - STREAM_EVENT_PATCH_OFFSETS[0] + 1u,
            opens[0], STREAM_OPEN_EPILOGUE_EXPECTED_OFF,
            streams[0] + 0x5000u, streams[0] + 0x6000u, corrupt,
            helper, helper_previous, helper_previous2,
            helper_legacy, helper_legacy2,
            policy, events,
            public_events, handoff_event, open_event)) {
        fprintf(stderr, "FAIL concurrent stream address wrap accepted\n");
        return 0;
    }
    if (build_concurrent_helpers(
            helpers[0], policies[0], streams[0],
            UINTPTR_MAX - STREAM_OPEN_EPILOGUE_EXPECTED_OFF + 1u,
            STREAM_OPEN_EPILOGUE_EXPECTED_OFF,
            streams[0] + 0x5000u, streams[0] + 0x6000u, corrupt,
            helper, helper_previous, helper_previous2,
            helper_legacy, helper_legacy2, policy, events,
            public_events, handoff_event, open_event)) {
        fprintf(stderr, "FAIL stream open address wrap accepted\n");
        return 0;
    }
    return 1;
}

static int test_concurrent_generation_ownership(void) {
    int update_states[2] = {OVERLAY_PATCHED, OVERLAY_PATCHED};
    int policy_states[POLICY_OVERLAY_REGION_COUNT] = {
        OVERLAY_PATCHED, OVERLAY_PATCHED, OVERLAY_PATCHED
    };
    int stream_states[STREAM_OVERLAY_REGION_COUNT] = {
        OVERLAY_PATCHED, OVERLAY_PATCHED,
        OVERLAY_PATCHED, OVERLAY_PATCHED
    };
    if (!coherent_concurrent_generation(
            update_states, OVERLAY_PATCHED,
            policy_states, stream_states, OVERLAY_PATCHED)) {
        fprintf(stderr, "FAIL final concurrent generation ownership\n");
        return 0;
    }
    if (!coherent_concurrent_generation(
            update_states, OVERLAY_LEGACY,
            policy_states, stream_states, OVERLAY_STOCK)) {
        fprintf(stderr, "FAIL previous 176-byte RX-tail ownership\n");
        return 0;
    }
    if (!coherent_concurrent_generation(
            update_states, OVERLAY_ALTERNATE3,
            policy_states, stream_states, OVERLAY_STOCK)) {
        fprintf(stderr, "FAIL previous 160-byte RX-tail ownership\n");
        return 0;
    }

    if (!coherent_concurrent_generation(
            update_states, OVERLAY_ALTERNATE,
            policy_states, stream_states, OVERLAY_STOCK)) {
        fprintf(stderr, "FAIL relocated 64-byte RX-tail ownership\n");
        return 0;
    }
    policy_states[POLICY_REGION_IDLE_COUNT] = OVERLAY_STOCK;
    stream_states[STREAM_REGION_EVENT1] = OVERLAY_STOCK;
    if (!coherent_concurrent_generation(
            update_states, OVERLAY_ALTERNATE2,
            policy_states, stream_states, OVERLAY_STOCK)) {
        fprintf(stderr, "FAIL relocated 56-byte RX-tail ownership\n");
        return 0;
    }
    policy_states[POLICY_REGION_IDLE_COUNT] = OVERLAY_PATCHED;
    if (coherent_concurrent_generation(
            update_states, OVERLAY_ALTERNATE2,
            policy_states, stream_states, OVERLAY_STOCK)) {
        fprintf(stderr, "FAIL 56-byte helper with new idle branch accepted\n");
        return 0;
    }
    if (coherent_concurrent_generation(
            update_states, OVERLAY_PATCHED,
            policy_states, stream_states, OVERLAY_PATCHED)) {
        fprintf(stderr, "FAIL final helper with missing event accepted\n");
        return 0;
    }

    update_states[0] = OVERLAY_ALTERNATE2;
    update_states[1] = OVERLAY_STOCK;
    policy_states[POLICY_REGION_IDLE_COUNT] = OVERLAY_STOCK;
    policy_states[POLICY_REGION_IDLE_CLEAR] = OVERLAY_PATCHED;
    policy_states[POLICY_REGION_GAME] = OVERLAY_LEGACY;
    stream_states[STREAM_REGION_EVENT0] = OVERLAY_ALTERNATE;
    stream_states[STREAM_REGION_EVENT1] = OVERLAY_STOCK;
    stream_states[STREAM_REGION_EVENT2] = OVERLAY_PATCHED;
    if (!coherent_concurrent_generation(
            update_states, OVERLAY_STOCK,
            policy_states, stream_states, OVERLAY_STOCK)) {
        fprintf(stderr, "FAIL historical generation ownership\n");
        return 0;
    }
    stream_states[STREAM_REGION_EVENT1] = OVERLAY_PATCHED;
    if (coherent_concurrent_generation(
            update_states, OVERLAY_STOCK,
            policy_states, stream_states, OVERLAY_STOCK)) {
        fprintf(stderr, "FAIL foreign historical generation accepted\n");
        return 0;
    }
    update_states[0] = OVERLAY_PATCHED;
    policy_states[POLICY_REGION_IDLE_COUNT] = OVERLAY_PATCHED;
    policy_states[POLICY_REGION_GAME] = OVERLAY_PATCHED;
    stream_states[STREAM_REGION_EVENT0] = OVERLAY_PATCHED;
    stream_states[STREAM_REGION_EVENT1] = OVERLAY_PATCHED;
    stream_states[STREAM_REGION_EVENT2] = OVERLAY_PATCHED;
    if (coherent_concurrent_generation(
            update_states, OVERLAY_PATCHED,
            policy_states, stream_states, OVERLAY_STOCK)) {
        fprintf(stderr, "FAIL final helper with stock open event accepted\n");
        return 0;
    }
    return 1;
}

static int test_app_policy_overlay_generation(void) {
    static const uintptr_t updates[] = {
        0x3974A0u, 0x397B20u, 0x397800u, 0x397860u
    };
    static const uintptr_t targets[] = {
        0x41ED60u, 0x41F460u, 0x41F1B0u, 0x41F210u
    };
    for (size_t i = 0; i < sizeof(updates) / sizeof(updates[0]); ++i) {
        unsigned char disk[UPDATE_APP_POLICY_BYTES];
        unsigned char stock[UPDATE_APP_POLICY_BYTES];
        unsigned char relaxed[UPDATE_APP_POLICY_BYTES];
        unsigned char legacy[UPDATE_APP_POLICY_BYTES];
        memcpy(disk, UPDATE_APP_POLICY_DISK_TEMPLATE, sizeof(disk));
        uint32_t stock_call = 0;
        uintptr_t stock_site = updates[i] + UPDATE_APP_POLICY_PATCH_OFF +
                               UPDATE_APP_POLICY_DISK_BL_OFF;
        if (!encode_aarch64_bl(stock_site, targets[i], &stock_call)) {
            fprintf(stderr, "FAIL policy fixture=%lu stock BL encode\n",
                    (unsigned long)i);
            return 0;
        }
        store_u32le(disk + UPDATE_APP_POLICY_DISK_BL_OFF, stock_call);
        uintptr_t generated_target = 0;
        if (!build_update_app_policy_overlay(updates[i], disk, stock,
                                             relaxed, legacy,
                                             &generated_target) ||
            generated_target != targets[i]) {
            fprintf(stderr, "FAIL policy fixture=%lu generation\n",
                    (unsigned long)i);
            return 0;
        }
        uintptr_t relaxed_target = 0;
        uintptr_t relaxed_site = updates[i] + UPDATE_APP_POLICY_PATCH_OFF +
                                 UPDATE_APP_POLICY_RELAXED_BL_OFF;
        if (!decode_aarch64_bl(
                relaxed_site,
                load_u32le(relaxed + UPDATE_APP_POLICY_RELAXED_BL_OFF),
                &relaxed_target) || relaxed_target != targets[i]) {
            fprintf(stderr, "FAIL policy fixture=%lu relaxed BL target\n",
                    (unsigned long)i);
            return 0;
        }
        uintptr_t stock_target = 0;
        uintptr_t stock_handoff_site = updates[i] +
            UPDATE_APP_POLICY_PATCH_OFF + UPDATE_APP_POLICY_STOCK_BL_OFF;
        if (!decode_aarch64_bl(
                stock_handoff_site,
                load_u32le(stock + UPDATE_APP_POLICY_STOCK_BL_OFF),
                &stock_target) || stock_target != targets[i] ||
            load_u32le(stock + 0x0Cu) != 0xB50001A8u ||
            load_u32le(stock + 0x10u) != 0x39546668u ||
            load_u32le(relaxed) != 0xF9429A77u ||
            load_u32le(relaxed + 0x08u) != 0x39546668u ||
            load_u32le(relaxed + 0x30u) != 0xF94002F7u ||
            load_u32le(relaxed + 0x34u) != 0xB5FFFF17u ||
            load_u32le(relaxed + 0x38u) != 0x52800037u ||
            memcmp(legacy + sizeof(UPDATE_APP_POLICY_LEGACY_TEMPLATE),
                   disk + sizeof(UPDATE_APP_POLICY_LEGACY_TEMPLATE),
                   UPDATE_APP_POLICY_BYTES -
                   sizeof(UPDATE_APP_POLICY_LEGACY_TEMPLATE)) != 0) {
            fprintf(stderr,
                    "FAIL policy fixture=%lu handoff/loop/legacy shape "
                    "stock=%08x/%08x/%08x relaxed=%08x/%08x/%08x legacy_tail=%02x\n",
                    (unsigned long)i, load_u32le(stock),
                    load_u32le(stock + 0x0Cu), load_u32le(stock + 0x10u),
                    load_u32le(relaxed), load_u32le(relaxed + 0x30u),
                    load_u32le(relaxed + 0x34u),
                    legacy[sizeof(UPDATE_APP_POLICY_LEGACY_TEMPLATE)]);
            return 0;
        }
    }

    unsigned char corrupt[UPDATE_APP_POLICY_BYTES];
    unsigned char stock[UPDATE_APP_POLICY_BYTES];
    unsigned char relaxed[UPDATE_APP_POLICY_BYTES];
    unsigned char legacy[UPDATE_APP_POLICY_BYTES];
    uintptr_t target = 0;
    memcpy(corrupt, UPDATE_APP_POLICY_DISK_TEMPLATE, sizeof(corrupt));
    corrupt[0] ^= 1u;
    if (build_update_app_policy_overlay(0x397800u, corrupt, stock,
                                        relaxed, legacy, &target)) {
        fprintf(stderr, "FAIL policy foreign stock shape accepted\n");
        return 0;
    }
    return 1;
}

static int test_stream_ref_overlay_generation(void) {
    static const uintptr_t streams[] = {
        0x361870u, 0x361F00u, 0x361B20u, 0x361B80u
    };
    static const uintptr_t targets[] = {
        0x415BB0u, 0x4162A0u, 0x415FE0u, 0x416040u
    };
    static const uintptr_t outputs[] = {
        0x3A45C0u, 0x3A4C70u, 0x3A4950u, 0x3A49B0u
    };
    for (size_t i = 0; i < sizeof(streams) / sizeof(streams[0]); ++i) {
        unsigned char stock[STREAM_REF_PATCH_BYTES];
        unsigned char patched[STREAM_REF_PATCH_BYTES];
        unsigned char persistent_legacy[STREAM_REF_PATCH_BYTES];
        unsigned char failed_idle_guard[STREAM_REF_PATCH_BYTES];
        unsigned char legacy[STREAM_REF_PATCH_BYTES];
        unsigned char output_patched[OUTPUT_POOL_TAIL_PATCH_BYTES];
        unsigned char output_persistent_legacy
            [OUTPUT_POOL_TAIL_PATCH_BYTES];
        unsigned char output_tail[sizeof(OUTPUT_POOL_TAIL_STOCK_TEMPLATE)];
        memcpy(output_tail, OUTPUT_POOL_TAIL_STOCK_TEMPLATE,
               sizeof(output_tail));
        memcpy(stock, STREAM_REF_STOCK_TEMPLATE, sizeof(stock));
        uint32_t call = 0;
        uintptr_t site = streams[i] + STREAM_REF_PATCH_OFF +
                         STREAM_REF_DELETE_BL_OFF;
        if (!encode_aarch64_bl(site, targets[i], &call)) {
            fprintf(stderr, "FAIL stream ref fixture=%lu BL encode\n",
                    (unsigned long)i);
            return 0;
        }
        store_u32le(stock + STREAM_REF_DELETE_BL_OFF, call);
        uintptr_t allowed_target = targets[i] + 0x100u;
        uintptr_t update_target = targets[i] + 0x200u;
        uintptr_t generated_target = 0;
        uintptr_t stack_target = 0;
        if (!build_stream_ref_overlay(
                streams[i], stock, allowed_target, update_target,
                outputs[i], output_tail, patched, persistent_legacy,
                failed_idle_guard, legacy, output_patched,
                output_persistent_legacy,
                &generated_target, &stack_target) ||
            generated_target != targets[i] ||
            stack_target != outputs[i] + OUTPUT_POOL_STACK_FAIL_OFF ||
            load_u32le(patched + STREAM_REF_DELETE_BL_OFF) != call ||
            memcmp(legacy + STREAM_REF_LEGACY_EVENT_OFF,
                   STREAM_REF_LEGACY_EVENT_RECOMPUTE,
                   sizeof(STREAM_REF_LEGACY_EVENT_RECOMPUTE)) != 0) {
            fprintf(stderr, "FAIL stream ref fixture=%lu generation\n",
                    (unsigned long)i);
            return 0;
        }
        uint32_t expected_allowed = 0;
        uint32_t expected_count = 0;
        uint32_t expected_stack = 0;
        uint32_t expected_legacy_stack = 0;
        uint32_t expected_update = 0;
        uint32_t expected_output = 0;
        uint32_t expected_output_persistent = 0;
        if (!encode_aarch64_bl(
                streams[i] + STREAM_REF_PATCH_OFF +
                STREAM_REF_ALLOWED_BL_OFF,
                allowed_target, &expected_allowed) ||
            !encode_aarch64_cbz_like(
                streams[i] + STREAM_REF_PATCH_OFF +
                STREAM_REF_COUNT_CBNZ_OFF,
                streams[i] + STREAM_UPDATE_CALL_OFF,
                load_u32le(STREAM_REF_PATCH_TEMPLATE +
                           STREAM_REF_COUNT_CBNZ_OFF),
                &expected_count) ||
            !encode_aarch64_b_cond(
                streams[i] + STREAM_REF_PATCH_OFF +
                STREAM_REF_STACK_COND_OFF,
                stack_target, 1u, &expected_stack) ||
            !encode_aarch64_b_cond(
                streams[i] + STREAM_REF_PATCH_OFF +
                STREAM_REF_LEGACY_STACK_COND_OFF,
                stack_target, 1u, &expected_legacy_stack) ||
            !encode_aarch64_b(
                streams[i] + STREAM_REF_PATCH_OFF +
                STREAM_REF_UPDATE_B_OFF,
                update_target, &expected_update) ||
            !encode_aarch64_b(
                outputs[i] + OUTPUT_POOL_TAIL_PATCH_OFF,
                streams[i] + STREAM_REF_HELPER_OFF,
                &expected_output) ||
            !encode_aarch64_b(
                outputs[i] + OUTPUT_POOL_TAIL_PATCH_OFF,
                streams[i] + STREAM_REF_LEGACY_HELPER_OFF,
                &expected_output_persistent) ||
            load_u32le(patched) != 0xAA0003F5u ||
            load_u32le(patched + 0x20u) != 0xF9429EC8u ||
            load_u32le(patched + STREAM_REF_ALLOWED_BL_OFF) !=
                expected_allowed ||
            load_u32le(patched + STREAM_REF_COUNT_CBNZ_OFF) !=
                expected_count ||
            load_u32le(patched + STREAM_REF_FLAGS_LOAD_OFF) !=
                0xB940B668u ||
            load_u32le(patched + 0x48u) != 0x721E091Fu ||
            load_u32le(patched + 0x4Cu) != 0x54000221u ||
            load_u32le(patched + 0x50u) != 0x37900208u ||
            load_u32le(patched + 0x54u) != 0x14000010u ||
            load_u32le(patched + 0x58u) != 0xF9401728u ||
            load_u32le(patched + STREAM_REF_STACK_COND_OFF) !=
                expected_stack ||
            load_u32le(patched + 0x68u) != 0xD503201Fu ||
            load_u32le(patched + STREAM_REF_STORE_OFF) !=
                0x391466C0u ||
            load_u32le(persistent_legacy +
                       STREAM_REF_LEGACY_IDLE_GUARD_CBNZ_OFF) !=
                0xD503201Fu ||
            load_u32le(persistent_legacy +
                       STREAM_REF_LEGACY_IDLE_GUARD_CLEAR_OFF) !=
                0xD503201Fu ||
            load_u32le(failed_idle_guard +
                       STREAM_REF_LEGACY_IDLE_GUARD_CBNZ_OFF) !=
                0x35000055u ||
            load_u32le(failed_idle_guard +
                       STREAM_REF_LEGACY_IDLE_GUARD_CLEAR_OFF) !=
                0x391466DFu ||
            load_u32le(failed_idle_guard +
                       STREAM_REF_LEGACY_STACK_COND_OFF) !=
                expected_legacy_stack ||
            load_u32le(patched + STREAM_REF_UPDATE_B_OFF) !=
                expected_update ||
            load_u32le(output_patched) != expected_output ||
            load_u32le(output_persistent_legacy) !=
                expected_output_persistent ||
            STREAM_EVENT_PATCH_SIZES[0] != 28u ||
            load_u32le(STREAM_EVENT_INLINE_TEMPLATE) != 0x3914671Fu ||
            load_u32le(STREAM_EVENT_INLINE_TEMPLATE + 4u) != 0xF9429F08u ||
            load_u32le(STREAM_EVENT_INLINE_TEMPLATE + 8u) != 0xF100051Fu ||
            load_u32le(STREAM_EVENT_INLINE_TEMPLATE + 12u) != 0x54000069u ||
            load_u32le(STREAM_EVENT_INLINE_TEMPLATE + 16u) != 0x52800028u ||
            load_u32le(STREAM_EVENT_INLINE_TEMPLATE + 20u) != 0x39146B08u) {
            fprintf(stderr,
                    "FAIL stream ref fixture=%lu handoff/helper instructions\n",
                    (unsigned long)i);
            return 0;
        }
    }
    unsigned char corrupt[STREAM_REF_PATCH_BYTES];
    unsigned char patched[STREAM_REF_PATCH_BYTES];
    unsigned char persistent_legacy[STREAM_REF_PATCH_BYTES];
    unsigned char failed_idle_guard[STREAM_REF_PATCH_BYTES];
    unsigned char legacy[STREAM_REF_PATCH_BYTES];
    unsigned char output_patched[OUTPUT_POOL_TAIL_PATCH_BYTES];
    unsigned char output_persistent_legacy[OUTPUT_POOL_TAIL_PATCH_BYTES];
    unsigned char output_tail[sizeof(OUTPUT_POOL_TAIL_STOCK_TEMPLATE)];
    uintptr_t target = 0;
    uintptr_t stack_target = 0;
    memcpy(output_tail, OUTPUT_POOL_TAIL_STOCK_TEMPLATE,
           sizeof(output_tail));
    memcpy(corrupt, STREAM_REF_STOCK_TEMPLATE, sizeof(corrupt));
    corrupt[0] ^= 1u;
    if (build_stream_ref_overlay(
            0x361B20u, corrupt, 0x41F000u, 0x41F100u, 0x3A4950u,
            output_tail, patched, persistent_legacy, failed_idle_guard,
            legacy, output_patched, output_persistent_legacy, &target,
            &stack_target)) {
        fprintf(stderr, "FAIL stream reference foreign shape accepted\n");
        return 0;
    }
    return 1;
}

typedef struct {
    int present;
    size_t device_count;
    uint32_t first_device;
} model_output_t;

static int model_flags_eligible(uint32_t flags) {
    const uint32_t low_flags = 0x4u | 0x8u | 0x10u;
    const uint32_t spatializer = 1u << 18;
    return (flags & low_flags) != 0 || (flags & spatializer) != 0;
}

static int model_handoff_on_decrement(int references, size_t app_count,
                                      int mode_active, int allowed,
                                      uint32_t flags) {
    return references == 0 && app_count == 1 && mode_active && allowed &&
           model_flags_eligible(flags);
}

static int model_policy_decision(size_t app_count, int handoff,
                                 int relaxed, int any_match) {
    if (app_count != 0) {
        if (relaxed) return any_match;
        return app_count == 1 && any_match;
    }
    return handoff != 0;
}

static int model_app_latch_commit(int app_count, int state) {
    return app_count > 1 ? 1 : state;
}

static int model_concurrent_policy(size_t active, size_t app_count,
                                   int *state, int game_auto_pause,
                                   int speaker) {
    if (!state) return -1;
    if (active == 0) {
        *state = 0;
        return 2;
    }
    if (!game_auto_pause) {
        *state = 0;
    } else if (*state != 0) {
        *state = active > 1 ? 2 : (app_count > 1 ? 1 : 0);
    }
    if (*state == 2) return 0;
    return speaker ? 1 : 0;
}

static int test_concurrent_latch_semantics(void) {
    int state = model_app_latch_commit(2, 0);
    if (state != 1 || model_concurrent_policy(1, 2, &state, 1, 1) != 1 ||
        state != 1 || model_concurrent_policy(2, 1, &state, 1, 1) != 0 ||
        state != 2 || model_concurrent_policy(1, 1, &state, 1, 1) != 1 ||
        state != 0) {
        fprintf(stderr, "FAIL pending/concurrent/clear state machine\n");
        return 0;
    }
    state = model_app_latch_commit(2, 0);
    if (model_concurrent_policy(2, 2, &state, 0, 1) != 1 || state != 0) {
        fprintf(stderr, "FAIL relaxed policy clears concurrent latch\n");
        return 0;
    }
    state = model_app_latch_commit(1, 0);
    if (model_concurrent_policy(2, 1, &state, 1, 1) != 1 || state != 0 ||
        model_concurrent_policy(2, 1, &state, 1, 0) != 0) {
        fprintf(stderr, "FAIL single-app multi-stream policy semantics\n");
        return 0;
    }
    state = model_app_latch_commit(2, 0);
    if (model_concurrent_policy(0, 2, &state, 1, 1) != 2 || state != 0) {
        fprintf(stderr, "FAIL zero-output return semantics\n");
        return 0;
    }
    state = 2;
    if (model_concurrent_policy(0, 1, &state, 1, 1) != 2 || state != 0) {
        fprintf(stderr, "FAIL idle helper clear semantics\n");
        return 0;
    }
    state = 2;
    if (model_concurrent_policy(2, 1, &state, 1, 1) != 0 ||
        model_concurrent_policy(1, 1, &state, 1, 1) != 1 || state != 0) {
        fprintf(stderr, "FAIL game exit resume semantics\n");
        return 0;
    }
    return 1;
}

static int model_idle_clear(size_t active_outputs, int handoff) {
    return active_outputs == 0 ? 0 : handoff;
}

static int test_stream_ref_handoff_semantics(void) {
    static const struct {
        int references;
        size_t apps;
        int mode_active;
        int allowed;
        uint32_t flags;
        int expected_handoff;
        const char *label;
    } decrements[] = {
        {1, 1, 1, 1, 0x4u, 0, "nonzero-reference"},
        {0, 1, 1, 1, 0x4u, 1, "fast-stream-handoff"},
        {0, 1, 1, 1, 0x8u, 1, "deep-stream-handoff"},
        {0, 1, 1, 1, 0x10u, 1, "offload-stream-handoff"},
        {0, 1, 1, 1, 1u << 18, 1, "spatial-stream-handoff"},
        {0, 1, 1, 1, 0u, 0, "system-ui-flags-zero"},
        {0, 1, 1, 1, 0x2u, 0, "unsupported-output"},
        {0, 1, 1, 0, 0x4u, 0, "foreign-route"},
        {0, 1, 0, 1, 0x4u, 0, "inactive-audio"},
        {0, 2, 1, 1, 0x4u, 0, "multiple-packages"},
    };
    for (size_t i = 0; i < sizeof(decrements) / sizeof(decrements[0]); ++i) {
        int actual = model_handoff_on_decrement(
            decrements[i].references, decrements[i].apps,
            decrements[i].mode_active, decrements[i].allowed,
            decrements[i].flags);
        if (actual != decrements[i].expected_handoff) {
            fprintf(stderr,
                    "FAIL handoff decrement %s expected=%d actual=%d\n",
                    decrements[i].label, decrements[i].expected_handoff,
                    actual);
            return 0;
        }
    }
    if (!model_policy_decision(0, 1, 0, 0) ||
        model_policy_decision(1, 1, 0, 0) ||
        model_policy_decision(2, 1, 0, 1) ||
        !model_policy_decision(2, 0, 1, 1) ||
        model_policy_decision(2, 0, 1, 0)) {
        fprintf(stderr, "FAIL handoff map precedence/relaxed semantics\n");
        return 0;
    }
    int handoff = model_idle_clear(1, 1);
    /* Output activation precedes +appname on observed game transitions. */
    if (!model_policy_decision(0, handoff, 0, 0)) {
        fprintf(stderr, "FAIL active output cleared handoff before app commit\n");
        return 0;
    }
    /* The committed +appname map supersedes and clears the transient flag. */
    handoff = 0;
    if (!model_policy_decision(1, handoff, 0, 1) ||
        model_policy_decision(1, handoff, 0, 0)) {
        fprintf(stderr, "FAIL committed app did not supersede handoff\n");
        return 0;
    }
    handoff = model_idle_clear(0, 1);
    if (handoff != 0 || model_policy_decision(0, handoff, 0, 0)) {
        fprintf(stderr, "FAIL zero-output handoff was not cleared\n");
        return 0;
    }
    static const struct {
        uint32_t flags;
        int expected;
    } outputs[] = {
        {0, 0},
        {0x4u, 1},
        {0x8u, 1},
        {0x10u, 1},
        {1u << 18, 1},
        {0x2u, 0},
    };
    for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        if (model_flags_eligible(outputs[i].flags) != outputs[i].expected) {
            fprintf(stderr,
                    "FAIL eligible output flags index=%lu\n",
                    (unsigned long)i);
            return 0;
        }
    }
    return 1;
}

static int test_auxiliary_transactions(void) {
    int ok = 1;
    unsigned char before[FAKE_SIZE];

    reset_fake();
    configure_auxiliary_stock();
    memcpy(before, fake_memory, sizeof(before));
    auxiliary_transaction_t transaction;
    if (!auxiliary_transaction_begin(42, FAKE_BASE, &transaction)) {
        fprintf(stderr, "FAIL auxiliary success snapshot\n");
        return 0;
    }
    g_test_icache_override = ICACHE_REMOTE_IVAU;
    if (!apply_auxiliary_targets(42, FAKE_BASE, 1) ||
        !verify_auxiliary_targets(42, FAKE_BASE, 1, 0) ||
        !auxiliary_transaction_restore(42, FAKE_BASE, &transaction) ||
        !expect_equal(before, "auxiliary-success-restore", 0)) {
        fprintf(stderr, "FAIL auxiliary success/restore path\n");
        ok = 0;
    }

    for (int fault = 1; fault <= (int)AUXILIARY_FAULT_POINT_COUNT; ++fault) {
        reset_fake();
        configure_auxiliary_stock();
        memcpy(before, fake_memory, sizeof(before));
        if (!auxiliary_transaction_begin(42, FAKE_BASE, &transaction)) return 0;
        g_test_icache_override = ICACHE_REMOTE_IVAU;
        fail_write_call = fault;
        if (apply_auxiliary_targets(42, FAKE_BASE, 1) != 0) {
            fprintf(stderr, "FAIL auxiliary write fault=%d unexpectedly succeeded\n", fault);
            ok = 0;
        }
        if (!auxiliary_transaction_restore(42, FAKE_BASE, &transaction)) {
            fprintf(stderr, "FAIL auxiliary write fault=%d rollback failed\n", fault);
            ok = 0;
        }
        ok &= expect_equal(before, "auxiliary-write", fault);
    }

    for (int fault = 1; fault <= (int)AUXILIARY_FAULT_POINT_COUNT; ++fault) {
        reset_fake();
        configure_auxiliary_stock();
        memcpy(before, fake_memory, sizeof(before));
        if (!auxiliary_transaction_begin(42, FAKE_BASE, &transaction)) return 0;
        g_test_icache_override = ICACHE_REMOTE_IVAU;
        g_test_icache_fail_call = fault;
        if (apply_auxiliary_targets(42, FAKE_BASE, 1) != 0) {
            fprintf(stderr, "FAIL auxiliary cache fault=%d unexpectedly succeeded\n", fault);
            ok = 0;
        }
        if (!auxiliary_transaction_restore(42, FAKE_BASE, &transaction)) {
            fprintf(stderr, "FAIL auxiliary cache fault=%d rollback failed\n", fault);
            ok = 0;
        }
        ok &= expect_equal(before, "auxiliary-cache", fault);
    }

    reset_fake();
    configure_auxiliary_stock();
    memcpy(fake_memory + g_auxiliary.update_off + UPDATE_FLAGS_PATCH_OFF,
           UPDATE_FLAGS_LEGACY, sizeof(UPDATE_FLAGS_LEGACY));
    memcpy(fake_memory + g_auxiliary.stream_event_off +
           STREAM_REF_PATCH_OFF, g_auxiliary.stream_ref_legacy,
           STREAM_REF_PATCH_BYTES);
    g_test_icache_override = ICACHE_REMOTE_IVAU;
    if (!apply_auxiliary_targets(42, FAKE_BASE, 0) ||
        !verify_auxiliary_targets(42, FAKE_BASE, 0, 0)) {
        fprintf(stderr, "FAIL legacy v1.5.6 lifecycle migration\n");
        ok = 0;
    }
    reset_fake();
    configure_auxiliary_stock();
    memcpy(fake_memory + g_auxiliary.update_off + UPDATE_FLAGS_PATCH_OFF,
           g_auxiliary.update_flags_guarded_legacy,
           sizeof(g_auxiliary.update_flags_guarded_legacy));
    g_test_icache_override = ICACHE_REMOTE_IVAU;
    if (!apply_auxiliary_targets(42, FAKE_BASE, 0) ||
        !verify_auxiliary_targets(42, FAKE_BASE, 0, 0)) {
        fprintf(stderr, "FAIL guarded idle candidate migration\n");
        ok = 0;
    }
    reset_fake();
    configure_auxiliary_stock();
    memcpy(fake_memory + g_auxiliary.update_off + UPDATE_FLAGS_PATCH_OFF,
           UPDATE_FLAGS_GAME_HANDOFF_LEGACY,
           sizeof(UPDATE_FLAGS_GAME_HANDOFF_LEGACY));
    memcpy(fake_memory + g_auxiliary.stream_event_off +
           STREAM_REF_PATCH_OFF,
           g_auxiliary.stream_ref_persistent_legacy,
           STREAM_REF_PATCH_BYTES);
    memcpy(fake_memory + g_auxiliary.output_pool_off +
           OUTPUT_POOL_TAIL_PATCH_OFF,
           g_auxiliary.output_pool_tail_persistent_legacy,
           OUTPUT_POOL_TAIL_PATCH_BYTES);
    g_test_icache_override = ICACHE_REMOTE_IVAU;
    if (!apply_auxiliary_targets(42, FAKE_BASE, 0) ||
        !verify_auxiliary_targets(42, FAKE_BASE, 0, 0)) {
        fprintf(stderr, "FAIL public v1.5.6 persistent handoff migration\n");
        ok = 0;
    }
    reset_fake();
    configure_auxiliary_stock();
    memcpy(fake_memory + g_auxiliary.update_off + UPDATE_FLAGS_PATCH_OFF,
           UPDATE_FLAGS_GAME_HANDOFF_LEGACY,
           sizeof(UPDATE_FLAGS_GAME_HANDOFF_LEGACY));
    memcpy(fake_memory + g_auxiliary.stream_event_off +
           STREAM_REF_PATCH_OFF,
           g_auxiliary.stream_ref_failed_idle_guard,
           STREAM_REF_PATCH_BYTES);
    memcpy(fake_memory + g_auxiliary.output_pool_off +
           OUTPUT_POOL_TAIL_PATCH_OFF,
           g_auxiliary.output_pool_tail_persistent_legacy,
           OUTPUT_POOL_TAIL_PATCH_BYTES);
    g_test_icache_override = ICACHE_REMOTE_IVAU;
    if (!apply_auxiliary_targets(42, FAKE_BASE, 0) ||
        !verify_auxiliary_targets(42, FAKE_BASE, 0, 0)) {
        fprintf(stderr, "FAIL failed idle-guard candidate migration\n");
        ok = 0;
    }
    reset_fake();
    configure_auxiliary_stock();
    memcpy(fake_memory + g_auxiliary.concurrent_helper_off,
           g_auxiliary.concurrent_helper_previous,
           UPDATE_CONCURRENT_HELPER_BYTES);
    if (!resolve_owned_executable_helper(42, FAKE_BASE) ||
        g_auxiliary.concurrent_helper_state != OVERLAY_LEGACY) {
        fprintf(stderr, "FAIL previous 176-byte helper ownership\n");
        ok = 0;
    }
    g_test_icache_override = ICACHE_REMOTE_IVAU;
    if (!apply_auxiliary_targets(42, FAKE_BASE, 0) ||
        !verify_auxiliary_targets(42, FAKE_BASE, 0, 0)) {
        fprintf(stderr, "FAIL previous 176-byte helper migration\n");
        ok = 0;
    }
    reset_fake();
    configure_auxiliary_stock();
    memcpy(fake_memory + g_auxiliary.concurrent_helper_off,
           g_auxiliary.concurrent_helper_previous2,
           UPDATE_CONCURRENT_HELPER_BYTES);
    if (!resolve_owned_executable_helper(42, FAKE_BASE) ||
        g_auxiliary.concurrent_helper_state != OVERLAY_ALTERNATE3) {
        fprintf(stderr, "FAIL previous 160-byte helper ownership\n");
        ok = 0;
    }
    g_test_icache_override = ICACHE_REMOTE_IVAU;
    if (!apply_auxiliary_targets(42, FAKE_BASE, 0) ||
        !verify_auxiliary_targets(42, FAKE_BASE, 0, 0)) {
        fprintf(stderr, "FAIL previous 160-byte helper migration\n");
        ok = 0;
    }
    reset_fake();
    configure_auxiliary_stock();
    memcpy(fake_memory + g_auxiliary.update_off + UPDATE_FLAGS_PATCH_OFF,
           g_auxiliary.update_flags_patched,
           sizeof(g_auxiliary.update_flags_patched));
    memcpy(fake_memory + g_auxiliary.concurrent_helper_off,
           g_auxiliary.concurrent_helper_legacy,
           UPDATE_CONCURRENT_HELPER_BYTES);
    memcpy(fake_memory + g_auxiliary.policy_off +
           IDLE_CLEAR_BRANCH_PATCH_OFF,
           g_auxiliary.idle_clear_branch, IDLE_CLEAR_BRANCH_BYTES);
    memcpy(fake_memory + g_auxiliary.policy_off + GAME_POLICY_PATCH_OFF,
           g_auxiliary.policy_concurrent, GAME_POLICY_PATCH_BYTES);
    for (size_t i = 0; i < STREAM_EVENT_PATCH_COUNT; ++i) {
        memcpy(fake_memory + g_auxiliary.stream_event_off +
               STREAM_EVENT_PATCH_OFFSETS[i],
               g_auxiliary.stream_event_patched[i],
               STREAM_EVENT_PATCH_SIZES[i]);
    }
    g_test_icache_override = ICACHE_REMOTE_IVAU;
    if (!apply_auxiliary_targets(42, FAKE_BASE, 0) ||
        !verify_auxiliary_targets(42, FAKE_BASE, 0, 0)) {
        fprintf(stderr, "FAIL missing-zero-init helper migration\n");
        ok = 0;
    }
    return ok;
}

static int test_coordinated_whitelist_rollback(void) {
    reset_fake();
    configure_owned_cave();
    configure_auxiliary_stock();
    unsigned char before[FAKE_SIZE];
    memcpy(before, fake_memory, sizeof(before));

    whitelist_transaction_t main_transaction;
    auxiliary_transaction_t auxiliary_transaction;
    if (!whitelist_transaction_begin(42, FAKE_BASE, &main_transaction) ||
        !auxiliary_transaction_begin(42, FAKE_BASE, &auxiliary_transaction)) {
        fprintf(stderr, "FAIL coordinated whitelist snapshots\n");
        return 0;
    }

    char *packages[MAX_SLOTS];
    char values[MAX_SLOTS][64];
    for (int i = 0; i < MAX_SLOTS; ++i) {
        snprintf(values[i], sizeof(values[i]), "org.example.coordinated%d", i + 1);
        packages[i] = values[i];
    }
    g_test_icache_override = ICACHE_REMOTE_IVAU;
    if (!apply_auxiliary_targets(42, FAKE_BASE, 1) ||
        !apply_strings(42, FAKE_BASE, packages)) {
        fprintf(stderr, "FAIL coordinated whitelist setup\n");
        return 0;
    }
    if (g_test_icache_calls != (int)AUXILIARY_FAULT_POINT_COUNT) {
        fprintf(stderr,
                "FAIL coordinated whitelist auxiliary cache count=%d expected=%u\n",
                g_test_icache_calls,
                (unsigned)AUXILIARY_FAULT_POINT_COUNT);
        return 0;
    }
    const int fault_call = g_test_icache_calls + 2;
    g_test_icache_fail_call = fault_call;
    if (install_whitelist_stub(42, FAKE_BASE) != 0) {
        fprintf(stderr, "FAIL coordinated whitelist cache fault not observed\n");
        return 0;
    }
    if (!whitelist_transaction_restore(42, FAKE_BASE, &main_transaction) ||
        !auxiliary_transaction_restore(42, FAKE_BASE, &auxiliary_transaction)) {
        fprintf(stderr, "FAIL coordinated whitelist rollback\n");
        return 0;
    }
    return expect_equal(before, "coordinated-whitelist", fault_call);
}

static int test_coordinated_global_rollback(void) {
    reset_fake();
    configure_auxiliary_stock();
    unsigned char before[FAKE_SIZE];
    unsigned char global_before[sizeof(GLOBAL_PATCH)];
    memcpy(before, fake_memory, sizeof(before));
    memcpy(global_before, fake_memory + g_func_off, sizeof(global_before));

    auxiliary_transaction_t auxiliary_transaction;
    if (!auxiliary_transaction_begin(42, FAKE_BASE, &auxiliary_transaction)) {
        fprintf(stderr, "FAIL coordinated global snapshot\n");
        return 0;
    }
    g_test_icache_override = ICACHE_REMOTE_IVAU;
    if (!apply_auxiliary_targets(42, FAKE_BASE, 1)) {
        fprintf(stderr, "FAIL coordinated global auxiliary setup\n");
        return 0;
    }
    if (g_test_icache_calls != (int)AUXILIARY_FAULT_POINT_COUNT) {
        fprintf(stderr,
                "FAIL coordinated global auxiliary cache count=%d expected=%u\n",
                g_test_icache_calls,
                (unsigned)AUXILIARY_FAULT_POINT_COUNT);
        return 0;
    }
    const int fault_call = g_test_icache_calls + 2;
    g_test_icache_fail_call = fault_call;
    if (write_code_twice(42, FAKE_BASE, FAKE_BASE + g_func_off,
                         GLOBAL_PATCH, sizeof(GLOBAL_PATCH),
                         "is_A2H_app.global-test") != 0) {
        fprintf(stderr, "FAIL coordinated global cache fault not observed\n");
        return 0;
    }
    if (!restore_global_snapshot(42, FAKE_BASE, FAKE_BASE + g_func_off,
                                 global_before) ||
        !auxiliary_transaction_restore(42, FAKE_BASE, &auxiliary_transaction)) {
        fprintf(stderr, "FAIL coordinated global rollback\n");
        return 0;
    }
    return expect_equal(before, "coordinated-global", fault_call);
}

int main(void) {
    int ok = test_stub_transaction() && test_string_transaction() &&
             test_owned_stale_stub_shapes() &&
             test_ten_slot_matcher_semantics() &&
              test_outer_second_cache_failure() &&
              test_executable_tail_layout() &&
              test_checked_absolute_ranges() &&
              test_dynamic_semantic_layouts() &&
              test_update_flags_overlay() &&
              test_concurrent_helper_generation() &&
              test_concurrent_generation_ownership() &&
              test_app_policy_overlay_generation() &&
              test_stream_ref_overlay_generation() &&
              test_concurrent_latch_semantics() &&
              test_stream_ref_handoff_semantics() &&
             test_auxiliary_transactions() &&
             test_coordinated_whitelist_rollback() &&
             test_coordinated_global_rollback();
    if (!ok) return 1;
    printf("PASS coordinated transactions: stub faults=3 string faults=20 "
           "aux-write faults=%u aux-cache faults=%u legacy-migration=6 "
           "policy-generations=4 stream-ref-generations=4 "
           "output-flag-shapes=4 stream-ref-semantics=24 "
           "dynamic-layouts=5 stale overlays=2 matcher cases=9 "
           "whitelist/global outer rollback=3\n",
           (unsigned)AUXILIARY_FAULT_POINT_COUNT,
           (unsigned)AUXILIARY_FAULT_POINT_COUNT);
    return 0;
}
