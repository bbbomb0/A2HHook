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
    g_auxiliary.update_flags_state = OVERLAY_STOCK;
    g_auxiliary.update_app_policy_state = OVERLAY_STOCK;
    g_auxiliary.policy_relaxed = 0;
    g_auxiliary.stream_ref_state = OVERLAY_STOCK;
    g_auxiliary.output_pool_state = OVERLAY_STOCK;
    g_auxiliary.stream_events_patched = 0;
    g_auxiliary.valid = 1;
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
            g_auxiliary.stream_ref_handoff_legacy,
            g_auxiliary.stream_ref_legacy,
            g_auxiliary.output_pool_tail_patched,
            &g_auxiliary.stream_ref_delete_target,
            &g_auxiliary.output_pool_stack_fail_target) ||
        g_auxiliary.stream_ref_delete_target != delete_target) {
        fprintf(stderr, "FAIL stream reference fixture generation\n");
        exit(2);
    }
    memcpy(fake_memory + g_auxiliary.update_off + UPDATE_FLAGS_PATCH_OFF,
           UPDATE_FLAGS_STOCK, sizeof(UPDATE_FLAGS_STOCK));
    memcpy(fake_memory + g_auxiliary.update_off +
           UPDATE_APP_POLICY_PATCH_OFF, g_auxiliary.app_policy_stock,
           UPDATE_APP_POLICY_BYTES);
    memcpy(fake_memory + g_auxiliary.policy_off + GAME_POLICY_PATCH_OFF,
           GAME_POLICY_STOCK, sizeof(GAME_POLICY_STOCK));
    memcpy(fake_memory + g_auxiliary.stream_event_off +
           STREAM_REF_PATCH_OFF, g_auxiliary.stream_ref_stock,
           STREAM_REF_PATCH_BYTES);
    memcpy(fake_memory + g_auxiliary.output_pool_off +
           OUTPUT_POOL_TAIL_PATCH_OFF, OUTPUT_POOL_TAIL_STOCK,
           OUTPUT_POOL_TAIL_PATCH_BYTES);
    for (size_t i = 0; i < STREAM_EVENT_PATCH_COUNT; ++i) {
        memcpy(fake_memory + g_auxiliary.stream_event_off +
               STREAM_EVENT_PATCH_OFFSETS[i], STREAM_EVENT_STOCK[i],
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
        unsigned char handoff_legacy[STREAM_REF_PATCH_BYTES];
        unsigned char legacy[STREAM_REF_PATCH_BYTES];
        unsigned char output_patched[OUTPUT_POOL_TAIL_PATCH_BYTES];
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
                outputs[i], output_tail, patched, handoff_legacy, legacy,
                output_patched,
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
        uint32_t expected_update = 0;
        uint32_t expected_output = 0;
        if (!encode_aarch64_bl(
                streams[i] + STREAM_REF_PATCH_OFF +
                STREAM_REF_ALLOWED_BL_OFF,
                allowed_target, &expected_allowed) ||
            !encode_aarch64_b(
                streams[i] + STREAM_REF_PATCH_OFF +
                STREAM_REF_UPDATE_B_OFF,
                update_target, &expected_update) ||
            !encode_aarch64_b(
                outputs[i] + OUTPUT_POOL_TAIL_PATCH_OFF,
                streams[i] + STREAM_REF_HELPER_OFF,
                &expected_output) ||
            load_u32le(patched) != 0xAA0003F5u ||
            load_u32le(patched + 0x20u) != 0xF9429EC8u ||
            load_u32le(patched + STREAM_REF_ALLOWED_BL_OFF) !=
                expected_allowed ||
            load_u32le(patched + 0x48u) != 0x391466C8u ||
            load_u32le(patched + 0x4Cu) != 0x14000012u ||
            load_u32le(patched + 0x64u) != 0xD503201Fu ||
            load_u32le(patched + 0x68u) != 0xD503201Fu ||
            load_u32le(handoff_legacy + 0x64u) != 0x34000054u ||
            load_u32le(handoff_legacy + 0x68u) != 0x391466DFu ||
            load_u32le(patched + STREAM_REF_UPDATE_B_OFF) !=
                expected_update ||
            load_u32le(output_patched) != expected_output ||
            load_u32le(patched + 0x90u) != 0xD503201Fu ||
            STREAM_EVENT_PATCH_SIZES[0] != 8u ||
            load_u32le(STREAM_EVENT_RECOMPUTE[0]) != 0x3914671Fu ||
            load_u32le(STREAM_EVENT_RECOMPUTE[0] + 4) != 0x14000118u) {
            fprintf(stderr,
                    "FAIL stream ref fixture=%lu handoff/helper instructions\n",
                    (unsigned long)i);
            return 0;
        }
    }
    unsigned char corrupt[STREAM_REF_PATCH_BYTES];
    unsigned char patched[STREAM_REF_PATCH_BYTES];
    unsigned char handoff_legacy[STREAM_REF_PATCH_BYTES];
    unsigned char legacy[STREAM_REF_PATCH_BYTES];
    unsigned char output_patched[OUTPUT_POOL_TAIL_PATCH_BYTES];
    unsigned char output_tail[sizeof(OUTPUT_POOL_TAIL_STOCK_TEMPLATE)];
    uintptr_t target = 0;
    uintptr_t stack_target = 0;
    memcpy(output_tail, OUTPUT_POOL_TAIL_STOCK_TEMPLATE,
           sizeof(output_tail));
    memcpy(corrupt, STREAM_REF_STOCK_TEMPLATE, sizeof(corrupt));
    corrupt[0] ^= 1u;
    if (build_stream_ref_overlay(
            0x361B20u, corrupt, 0x41F000u, 0x41F100u, 0x3A4950u,
            output_tail, patched, handoff_legacy, legacy, output_patched,
            &target,
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

static int model_handoff_on_decrement(int references, size_t app_count,
                                      int mode_active, int allowed) {
    return references == 0 && app_count == 1 && mode_active && allowed;
}

static int model_policy_decision(size_t app_count, int handoff,
                                 int relaxed, int any_match) {
    if (app_count != 0) {
        if (relaxed) return any_match;
        return app_count == 1 && any_match;
    }
    return handoff != 0;
}

static int test_stream_ref_handoff_semantics(void) {
    static const struct {
        int references;
        size_t apps;
        int mode_active;
        int allowed;
        int expected_handoff;
        const char *label;
    } decrements[] = {
        {1, 1, 1, 1, 0, "nonzero-reference"},
        {0, 1, 1, 1, 1, "same-app-stream-handoff"},
        {0, 1, 1, 0, 0, "foreign-route"},
        {0, 1, 0, 1, 0, "inactive-audio"},
        {0, 2, 1, 1, 0, "multiple-packages"},
    };
    for (size_t i = 0; i < sizeof(decrements) / sizeof(decrements[0]); ++i) {
        int actual = model_handoff_on_decrement(
            decrements[i].references, decrements[i].apps,
            decrements[i].mode_active, decrements[i].allowed);
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
    int handoff = 1;
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

    for (int fault = 1; fault <= 14; ++fault) {
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

    for (int fault = 1; fault <= 14; ++fault) {
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
    g_test_icache_fail_call = 16;
    if (install_whitelist_stub(42, FAKE_BASE) != 0) {
        fprintf(stderr, "FAIL coordinated whitelist cache fault not observed\n");
        return 0;
    }
    if (!whitelist_transaction_restore(42, FAKE_BASE, &main_transaction) ||
        !auxiliary_transaction_restore(42, FAKE_BASE, &auxiliary_transaction)) {
        fprintf(stderr, "FAIL coordinated whitelist rollback\n");
        return 0;
    }
    return expect_equal(before, "coordinated-whitelist", 14);
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
    g_test_icache_fail_call = 16;
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
    return expect_equal(before, "coordinated-global", 14);
}

int main(void) {
    int ok = test_stub_transaction() && test_string_transaction() &&
             test_owned_stale_stub_shapes() &&
             test_ten_slot_matcher_semantics() &&
             test_outer_second_cache_failure() &&
             test_app_policy_overlay_generation() &&
             test_stream_ref_overlay_generation() &&
             test_stream_ref_handoff_semantics() &&
             test_auxiliary_transactions() &&
             test_coordinated_whitelist_rollback() &&
             test_coordinated_global_rollback();
    if (!ok) return 1;
    printf("PASS coordinated transactions: stub faults=3 string faults=20 "
           "aux-write faults=14 aux-cache faults=14 legacy-migration=1 "
           "policy-generations=4 stream-ref-generations=4 "
           "stream-ref-semantics=11 stale overlays=2 matcher cases=9 "
           "whitelist/global outer rollback=3\n");
    return 0;
}
