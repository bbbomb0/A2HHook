// a2h_patch v1.5.4 - v1.0 universal signature scan + 10-slot whitelist
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
#include <time.h>
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

#define PTRACE_ATTACH 16
#define PTRACE_DETACH 17
#define PTRACE_CONT 7
#define PTRACE_PEEKDATA 2
#define PTRACE_POKEDATA 5
#define MAX_SLOTS 10
#define A2H_VERSION "1.5.4"
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
    const char *name; const char *hint; uintptr_t func_off; uintptr_t slot_off[6]; int slot_len[6];
} profile_t;
static const profile_t PROFILES[] = {
    {"os3_0_302_0x3e3fc0","HyperOS3.0.302 verified",0x3E3FC0,{0xADC9C,0xB19F4,0xB507E,0xBC5CC,0xBC5DB,0xFCFFA},{17,19,22,14,15,14}},
    {"os2_0_218_0x3e4280","HyperOS2.0.218 static",0x3E4280,{0xADE45,0xB1BBB,0xB5280,0xBC7CE,0xBC7DD,0xFD0FF},{17,19,22,14,15,14}},
};
static slot_t slots[MAX_SLOTS];
static uintptr_t g_func_off=0x3E3FC0,g_ptr_off=0x437200,g_stub_mark=0x437100,g_rw_start=0,g_rw_end=0,g_rx_start=0,g_rx_end=0;
static uintptr_t g_elf_tail_start=0,g_elf_tail_end=0,g_elf_tail_candidate=0;
static char g_libname[64]={0}, g_profile[48]={0}, g_profile_hint[48]={0}, g_locate_method[32]={0}, g_scan_kind[24]={0};
static int g_attached=0;
static uintptr_t slot_off(int i){return upx(slots[i].off_x);} 
static void set_slot_off(int i,uintptr_t off){slots[i].off_x=(uint32_t)off^0xA5C31F77u;} 
static uintptr_t k_func_off(void){return g_func_off;} 
static int load_func_off_hint(uintptr_t *out);
static int load_cave_hint(uintptr_t *out);
static void save_cave_hint(uintptr_t off);
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
static int trace_attach(pid_t pid){if(ptrace_call(PTRACE_ATTACH,pid,NULL,NULL)<0)return -1;int st=0;waitpid(pid,&st,0);return 0;} 
static void trace_detach(pid_t pid){ptrace_call(PTRACE_DETACH,pid,NULL,NULL);} 
static int mem_w(pid_t pid,uintptr_t addr,const void *d,size_t len){
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
    static const uint32_t tail[WHITELIST_STUB_WORDS - 2] = {
        0xB40001E0u, 0x52800142u, 0x340001A2u, 0xF8408423u,
        0xB4000123u, 0xAA0003E4u, 0x38401485u, 0x38401466u,
        0x6B0600BFu, 0x54000081u, 0x35FFFF85u, 0x52800020u,
        0xD65F03C0u, 0x51000442u, 0x17FFFFF4u, 0x52800000u,
        0xD65F03C0u
    };
    if (!h || n < WHITELIST_STUB_BYTES || !is_stub_head(h, n)) return 0;
    for (size_t i = 0; i < sizeof(tail) / sizeof(tail[0]); ++i) {
        if (load_u32le(h + (i + 2) * sizeof(uint32_t)) != tail[i]) return 0;
    }
    return 1;
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

// EL0-legal remote I-cache maintenance.
// Old path used "ic ialluis" (EL1-only) and treated SIGILL as success, so memory
// looked patched while instruction cache still executed stock code on some ROMs.
static int remote_icache_flush(pid_t pid, uintptr_t func_abs, size_t nbytes) {
    int methods = 0;
    if (syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL, 0) == 0) {
        fprintf(stderr, "[a2h_patch] icache: membarrier GLOBAL ok\n"); methods++;
    } else if (syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL_EXPEDITED, 0) == 0) {
        fprintf(stderr, "[a2h_patch] icache: membarrier GLOBAL_EXPEDITED ok\n"); methods++;
    } else {
        fprintf(stderr, "[a2h_patch] icache: membarrier unavailable errno=%d\n", errno);
    }
    if (!g_attached) {
        fprintf(stderr, "[a2h_patch] icache: ptrace not attached, skip remote ivau\n");
        return methods > 0 ? 1 : 0;
    }
    if (nbytes < 64) nbytes = 64;
    if (nbytes > 4096) nbytes = 4096;
    uint32_t lines = (uint32_t)((nbytes + 63) / 64);
    if (lines < 1) lines = 1;
    if (lines > 64) lines = 64;

    // x0=start VA, x1=line count
    // loop: ic ivau,x0; add x0,#64; subs x1,#1; b.ne; dsb ish; isb; brk #0
    uint32_t flush_code[8];
    int cn = 0;
    flush_code[cn++] = 0xD50B7520; // ic ivau, x0
    flush_code[cn++] = 0x91010000; // add x0, x0, #0x40
    flush_code[cn++] = 0xF1000421; // subs x1, x1, #1
    flush_code[cn++] = 0x54FFFFA1; // b.ne loop
    flush_code[cn++] = 0xD5033B9F; // dsb ish
    flush_code[cn++] = 0xD5033FDF; // isb
    flush_code[cn++] = 0xD4200000; // brk #0

    uintptr_t scratch = func_abs;
    int used_scratch = 0;
    if (g_rx_end > g_rx_start + 64) {
        uintptr_t cand = (g_rx_end - 64) & ~((uintptr_t)0xF);
        if (cand >= g_rx_start && cand + 64 <= g_rx_end &&
            (cand + 64 <= func_abs || cand >= func_abs + 128)) {
            scratch = cand;
            used_scratch = 1;
        }
    }

    size_t save_n = (size_t)cn * 4;
    unsigned char saved[64];
    if (save_n > sizeof(saved)) save_n = sizeof(saved);
    if (mem_r(pid, scratch, saved, save_n) != 0) {
        fprintf(stderr, "[a2h_patch] icache: save scratch FAIL @0x%lx\n", (unsigned long)scratch);
        return methods > 0 ? 1 : 0;
    }
    if (mem_w(pid, scratch, flush_code, save_n) != 0) {
        fprintf(stderr, "[a2h_patch] icache: write flush stub FAIL\n");
        return methods > 0 ? 1 : 0;
    }

    struct user_pt_regs_a64 regs, backup;
    struct iovec iov;
    memset(&regs, 0, sizeof(regs));
    iov.iov_base = &regs; iov.iov_len = sizeof(regs);
    if (ptrace_call(PTRACE_GETREGSET, pid, (void *)(uintptr_t)NT_PRSTATUS, &iov) < 0) {
        fprintf(stderr, "[a2h_patch] icache: GETREGSET FAIL errno=%d\n", errno);
        mem_w(pid, scratch, saved, save_n);
        return methods > 0 ? 1 : 0;
    }
    backup = regs;
    regs.pc = scratch;
    regs.regs[0] = func_abs & ~((uintptr_t)63);
    regs.regs[1] = lines + 2;
    iov.iov_base = &regs; iov.iov_len = sizeof(regs);
    if (ptrace_call(PTRACE_SETREGSET, pid, (void *)(uintptr_t)NT_PRSTATUS, &iov) < 0) {
        fprintf(stderr, "[a2h_patch] icache: SETREGSET FAIL errno=%d\n", errno);
        mem_w(pid, scratch, saved, save_n);
        return methods > 0 ? 1 : 0;
    }
    if (ptrace_call(PTRACE_CONT, pid, NULL, NULL) < 0) {
        fprintf(stderr, "[a2h_patch] icache: CONT FAIL errno=%d\n", errno);
        iov.iov_base = &backup; iov.iov_len = sizeof(backup);
        ptrace_call(PTRACE_SETREGSET, pid, (void *)(uintptr_t)NT_PRSTATUS, &iov);
        mem_w(pid, scratch, saved, save_n);
        return methods > 0 ? 1 : 0;
    }
    int st = 0;
    pid_t w = waitpid(pid, &st, 0);
    int ok_trap = 0;
    if (w == pid && WIFSTOPPED(st)) {
        int sig = WSTOPSIG(st);
        if (sig == 5) ok_trap = 1; // only SIGTRAP counts as success
        fprintf(stderr, "[a2h_patch] icache: remote ivau stopped sig=%d scratch=%s\n",
                sig, used_scratch ? "rx-tail" : "func");
    } else {
        fprintf(stderr, "[a2h_patch] icache: wait status=0x%x w=%d errno=%d\n", st, (int)w, errno);
    }
    iov.iov_base = &backup; iov.iov_len = sizeof(backup);
    ptrace_call(PTRACE_SETREGSET, pid, (void *)(uintptr_t)NT_PRSTATUS, &iov);
    mem_w(pid, scratch, saved, save_n);
    if (ok_trap) { fprintf(stderr, "[a2h_patch] icache: remote ivau EXEC ok\n"); methods++; }
    else fprintf(stderr, "[a2h_patch] icache: remote ivau EXEC uncertain\n");
    return methods > 0 ? 1 : 0;
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
    g_func_off = prof->func_off;
    snprintf(g_profile, sizeof(g_profile), "%s", prof->name);
    snprintf(g_profile_hint, sizeof(g_profile_hint), "%s", prof->hint);
    for (int i = 0; i < 6; ++i) { set_slot_off(i, prof->slot_off[i]); slots[i].max_len = prof->slot_len[i]; slots[i].label = "s"; }
    for (int i = 6; i < MAX_SLOTS; ++i) { slots[i].max_len = 63; slots[i].label = "x"; }
}
static const profile_t *profile_for_func_off(uintptr_t off) {
    for (size_t i = 0; i < sizeof(PROFILES) / sizeof(PROFILES[0]); ++i) {
        if (PROFILES[i].func_off == off) return &PROFILES[i];
    }
    return NULL;
}
static int head_is_known(const unsigned char *head, size_t n) {
    return (head && n >= 8) && (
        memcmp(head, SIG8, 8) == 0 ||
        memcmp(head, PATCH, 8) == 0 ||
        is_stub_head(head, n)
    );
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
    if (!decode_stub_table(base + func_off, head, &table)) {
        if (verbose) fprintf(stderr, "[a2h_patch] stub table decode FAIL\n");
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

/* A legacy whitelist->global transition leaves the old stub words after the
 * eight-byte global return sequence.  Accept that shape only when the cave
 * marker and pointer table still belong to this module instance. */
static int patched_global_candidate_ok(pid_t pid, uintptr_t base,
                                       const unsigned char *head, size_t n) {
    if (!patched_global_tail_ok(head, n)) return 0;
    if (!patched_global_stub_tail(head, n)) return 1;

    uintptr_t cave = 0;
    return load_cave_hint(&cave) && marked_cave_ok(pid, base, cave);
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

/* A v1.5.2-era stub can outlive its unmarked or overwritten RW table until
 * the HAL restarts.  For a known function offset, the stub opcode shape plus
 * all six exact official strings identifies the target strongly enough to
 * let whitelist apply rebuild the cave and table.  Unknown offsets still
 * require a fully valid table. */
static int known_profile_stub_state_ok(pid_t pid, uintptr_t base,
                                       const profile_t *prof,
                                       const unsigned char *head, int score,
                                       const char *context) {
    if (!prof || !is_stub_head(head, 16)) return 0;
    stub_info_t si;
    if (inspect_stub_table(pid, base, prof->func_off, head, NULL, 0, &si)) {
        return 1;
    }
    if (score >= 95 && profile_official_strings_exact(pid, base, prof) &&
        exact_whitelist_stub_at(pid, base + prof->func_off)) {
        /* Emit the concrete table failure only on the recovery path. */
        inspect_stub_table(pid, base, prof->func_off, head, NULL, 1, &si);
        fprintf(stderr,
                "[a2h_patch] WARN: known profile stale stub table; allow repair context=%s profile=%s func=0x%lx score=%d\n",
                context ? context : "?", prof->name,
                (unsigned long)prof->func_off, score);
        return 1;
    }
    return 0;
}
// Prefer a unique stock prologue. Patched candidates must also be unambiguous
// and, for a legacy stub tail, prove ownership of the recorded cave marker.
static int scan_func_by_sig(pid_t pid, uintptr_t base, uintptr_t *out_off) {
    // Locate is_A2H_app even after patching:
    // 1) stock prologue SIG8
    // 2) global patch (mov w0,#1; ret)
    // 3) our whitelist stub head (ADRP x1; ADD x1; MOV w2,#10)
    // Never full-text ADRP scan: too many false positives.
    char mp[64]; snprintf(mp,sizeof(mp),"/proc/%d/maps",pid);
    FILE *f=fopen(mp,"r"); if(!f) return 0;
    char line[512];
    int segs=0, hits_stock=0, hits_global_raw=0, hits_global=0;
    int hits_stub_raw=0, hits_stub=0, hits_stub_stale_exact=0;
    uintptr_t best_stock=0, best_global=0, best_stub=0, best_stub_stale=0;
    fprintf(stderr, "[a2h_patch] Scanning exec for stock/global/stub is_A2H_app...\n");
    while (fgets(line,sizeof(line),f)) {
        if (!strstr(line, g_libname[0]?g_libname:"audio.primary")) continue;
        uintptr_t s=0,e=0; unsigned off=0; char perms[8]={0};
        if (sscanf(line, "%lx-%lx %7s %x", &s, &e, perms, &off) < 4) continue;
        if (!strchr(perms,'x')) continue;
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
            if (memcmp(h, SIG8, 8) == 0) {
                hits_stock++; best_stock = rel;
                fprintf(stderr, "[a2h_patch] FOUND stock at rel=0x%lx\n", (unsigned long)rel);
                continue;
            }
            if (memcmp(h, PATCH, 8) == 0) {
                hits_global_raw++;
                if (patched_global_candidate_ok(pid, base, h, 16)) {
                    hits_global++;
                    best_global = rel;
                }
                continue;
            }
            if (is_stub_head(h, 16)) {
                hits_stub_raw++;
                stub_info_t si;
                if (inspect_stub_table(pid, base, rel, h, NULL, 0, &si)) {
                    hits_stub++;
                    best_stub = rel;
                } else if (i + WHITELIST_STUB_BYTES <= len &&
                           exact_whitelist_stub_shape(h, len - i)) {
                    hits_stub_stale_exact++;
                    best_stub_stale = rel;
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
    if (hits_stock == 1) {
        best = best_stock;
        kind = "stock";
    } else if (hits_stock > 1) {
        snprintf(g_scan_kind, sizeof(g_scan_kind), "ambiguous");
        fprintf(stderr, "[a2h_patch] ERROR: ambiguous stock signature candidates=%d\n", hits_stock);
        return 0;
    } else if (hits_global + hits_stub + hits_stub_stale_exact == 1) {
        if (hits_global == 1) { best = best_global; kind = "global"; }
        else if (hits_stub == 1) { best = best_stub; kind = "stub"; }
        else { best = best_stub_stale; kind = "stub-stale-exact"; }
    } else if (hits_global + hits_stub + hits_stub_stale_exact > 1) {
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
    char mp[64]; snprintf(mp,sizeof(mp),"/proc/%d/maps",pid);
    FILE *f=fopen(mp,"r"); if(!f) return 0;
    typedef struct { uintptr_t s,e; unsigned off; char perms[8]; } seg_t;
    seg_t segs[32]; int nseg=0;
    char line[512];
    uintptr_t rw_rel_s=0, rw_rel_e=0;
    while (fgets(line,sizeof(line),f) && nseg < 32) {
        if (!strstr(line, g_libname[0]?g_libname:"audio.primary")) continue;
        uintptr_t s=0,e=0; unsigned off=0; char perms[8]={0};
        if (sscanf(line, "%lx-%lx %7s %x", &s, &e, perms, &off) < 4) continue;
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
            for (size_t off=0; off + n + 1 < len; ++off) {
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

    // 0) A known profile hint may use the fast path. Unknown stock hints are
    // only clues: the eight-byte stock prologue is not unique enough by itself.
    uintptr_t hint = 0;
    int patched_hint_ok = 0;
    const char *patched_hint_state = NULL;
    if (load_func_off_hint(&hint)) {
        unsigned char hh[16] = {0};
        uintptr_t hint_abs = base + hint;
        int hint_in_rx = (g_rx_end > g_rx_start) ?
                         (hint_abs >= g_rx_start && hint_abs + sizeof(hh) <= g_rx_end) : 1;
        if (!hint_in_rx) {
            fprintf(stderr, "[a2h_patch] func_off hint ignored: 0x%lx outside RX map\n",
                    (unsigned long)hint);
        } else if (mem_r(pid, hint_abs, hh, sizeof(hh)) == 0) {
            const profile_t *hit = profile_for_func_off(hint);
            if (hit && head_is_known(hh, sizeof(hh))) {
                int hint_score = score_profile(pid, base, hit);
                int state_ok = 1;
                if (memcmp(hh, PATCH, sizeof(PATCH)) == 0 &&
                    !patched_global_candidate_ok(pid, base, hh, sizeof(hh))) {
                    state_ok = 0;
                    fprintf(stderr,
                            "[a2h_patch] known hint global tail mismatch; defer scan: 0x%lx\n",
                            (unsigned long)hint);
                } else if (is_stub_head(hh, sizeof(hh))) {
                    state_ok = known_profile_stub_state_ok(pid, base, hit, hh,
                                                           hint_score, "hint");
                    if (!state_ok) {
                        fprintf(stderr,
                                "[a2h_patch] known hint stub table invalid; defer scan: 0x%lx\n",
                                (unsigned long)hint);
                    }
                }
                if (hint_score >= 90 && state_ok) {
                    apply_profile(hit);
                    snprintf(g_locate_method, sizeof(g_locate_method), "hint+profile");
                    fprintf(stderr,
                            "[a2h_patch] selected method=%s profile=%s hint=%s func=0x%lx score=%d\n",
                            g_locate_method, g_profile, g_profile_hint,
                            (unsigned long)g_func_off, hint_score);
                    return 1;
                }
                fprintf(stderr,
                        "[a2h_patch] known hint confidence too low; defer scan func=0x%lx score=%d\n",
                        (unsigned long)hint, hint_score);
            }
            if (memcmp(hh, SIG8, sizeof(SIG8)) == 0) {
                fprintf(stderr,
                        "[a2h_patch] unknown stock hint deferred pending unique scan: 0x%lx\n",
                        (unsigned long)hint);
            } else if (patched_global_candidate_ok(pid, base, hh, sizeof(hh))) {
                patched_hint_ok = 1;
                patched_hint_state = "global";
                fprintf(stderr,
                        "[a2h_patch] validated patched hint state=global func=0x%lx (stock tail intact)\n",
                        (unsigned long)hint);
            } else if (is_stub_head(hh, sizeof(hh))) {
                stub_info_t si;
                if (inspect_stub_table(pid, base, hint, hh, NULL, 0, &si)) {
                    patched_hint_ok = 1;
                    patched_hint_state = "whitelist";
                    fprintf(stderr,
                            "[a2h_patch] validated patched hint state=whitelist func=0x%lx active_ptrs=%d magic=%d\n",
                            (unsigned long)hint, si.active_ptrs, si.magic_ok);
                } else if (exact_whitelist_stub_at(pid, hint_abs)) {
                    patched_hint_ok = 1;
                    patched_hint_state = "whitelist-stale-exact";
                    fprintf(stderr,
                            "[a2h_patch] validated stale exact whitelist stub hint func=0x%lx\n",
                            (unsigned long)hint);
                } else {
                    fprintf(stderr,
                            "[a2h_patch] func_off stub hint ignored: 0x%lx table validation failed\n",
                            (unsigned long)hint);
                }
            } else {
                fprintf(stderr, "[a2h_patch] func_off hint ignored: 0x%lx head=%02x %02x %02x %02x\n",
                        (unsigned long)hint, hh[0], hh[1], hh[2], hh[3]);
            }
        }
    }

    // 1) Try known profiles first; if the head is already stock/global/stub and
    // the profile score is strong enough, we can skip the broader exec scan.
    int fast_best = -1, fast_best_score = -100000;
    for (size_t i=0;i<sizeof(PROFILES)/sizeof(PROFILES[0]);++i) {
        int sc = score_profile(pid, base, &PROFILES[i]);
        if (sc > fast_best_score) { fast_best_score = sc; fast_best = (int)i; }
    }
    /* A fixed profile is accepted only when all six official strings and the
     * function head agree.  A loose score here can re-use an offset from a
     * different OTA and patch an unrelated function. */
    if (fast_best >= 0 && fast_best_score >= 90) {
        unsigned char ph[16]={0};
        int fast_state_ok = 0;
        if (mem_r(pid, base + PROFILES[fast_best].func_off, ph, 16) == 0) {
            if (memcmp(ph, SIG8, sizeof(SIG8)) == 0 ||
                patched_global_candidate_ok(pid, base, ph, sizeof(ph))) {
                fast_state_ok = 1;
            } else if (is_stub_head(ph, sizeof(ph))) {
                fast_state_ok = known_profile_stub_state_ok(pid, base,
                                                            &PROFILES[fast_best], ph,
                                                            fast_best_score,
                                                            "profile-fast");
            }
        }
        if (fast_state_ok) {
            apply_profile(&PROFILES[fast_best]);
            snprintf(g_locate_method, sizeof(g_locate_method), "profile-fast");
            fprintf(stderr, "[a2h_patch] selected method=%s profile=%s hint=%s func=0x%lx score=%d\n",
                    g_locate_method, g_profile, g_profile_hint, (unsigned long)g_func_off, fast_best_score);
            return 1;
        }
    }

    // 2) Prefer the universal v1.0-style executable scan when it finds a real stock prologue.
    // This is the least ROM-specific path and should carry the main compatibility load.
    uintptr_t sig_off = 0;
    int sig_ok = scan_func_by_sig(pid, base, &sig_off);

    /* Do not return immediately on a unique stock hit.  main() seeds slots
     * from the OS3 profile; OS2/unknown builds must still select or scan their
     * package-string layout before any whitelist data is written. */

    // 3) Prefer known profiles when already patched / stock matches them.
    int best=-1, best_score=-100000;
    for (size_t i=0;i<sizeof(PROFILES)/sizeof(PROFILES[0]);++i) {
        int sc=score_profile(pid, base, &PROFILES[i]);
        if (sc>best_score){best_score=sc; best=(int)i;}
    }

    // Seed package slots from best profile first.
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

    // Decision priority:
    // 1) v1.0-style stock executable scan
    // 2) high-confidence profile when its function head looks valid (stock/global/stub)
    // 3) runtime scan (stock/global/unique stub) for unknown builds like OS2.0.220 @0x3e4360
    // 4) weak profile last resort
    int profile_head_ok = 0;
    if (best >= 0) {
        unsigned char ph[16]={0};
        if (mem_r(pid, base + PROFILES[best].func_off, ph, 16) == 0) {
            if (memcmp(ph, SIG8, 8)==0 ||
                patched_global_candidate_ok(pid, base, ph, sizeof(ph))) profile_head_ok = 1;
            else if (is_stub_head(ph, 16)) {
                profile_head_ok = known_profile_stub_state_ok(pid, base,
                                                              &PROFILES[best], ph,
                                                              best_score,
                                                              "profile-final");
            }
        }
    }

    if (best >= 0 && best_score >= 80 && profile_head_ok &&
        strcmp(g_scan_kind, "ambiguous") != 0) {
        g_func_off = PROFILES[best].func_off;
        if (sig_ok && sig_off == g_func_off)
            snprintf(g_locate_method, sizeof(g_locate_method), "profile+scan");
        else
            snprintf(g_locate_method, sizeof(g_locate_method), "profile");
        fprintf(stderr, "[a2h_patch] selected method=%s profile=%s hint=%s func=0x%lx score=%d str_found=%d\n",
                g_locate_method, g_profile, g_profile_hint, (unsigned long)g_func_off, best_score, str_found);
        return 1;
    }

    if (sig_ok) {
        g_func_off = sig_off;
        snprintf(g_locate_method, sizeof(g_locate_method), "scan");
        if (best >= 0) {
            // Keep package profile offsets if useful, even when function offset differs (OS2.0.220).
            snprintf(g_profile, sizeof(g_profile), "scan+%s", PROFILES[best].name);
            snprintf(g_profile_hint, sizeof(g_profile_hint), "%s", PROFILES[best].hint);
        } else {
            snprintf(g_profile, sizeof(g_profile), "scan");
            snprintf(g_profile_hint, sizeof(g_profile_hint), "universal");
        }
        fprintf(stderr, "[a2h_patch] selected method=%s func=0x%lx str_found=%d profile=%s\n",
                g_locate_method, (unsigned long)g_func_off, str_found, g_profile);
        return 1;
    }

    // The full scan recorded ambiguity/misses above. A saved patched hint is
    // accepted only after state-specific validation (marker, stock tail, or
    // a cave-owned legacy stub tail).
    if (patched_hint_ok && strcmp(g_scan_kind, "ambiguous") != 0) {
        g_func_off = hint;
        snprintf(g_locate_method, sizeof(g_locate_method), "hint-patched-validated");
        snprintf(g_profile, sizeof(g_profile), "hint");
        snprintf(g_profile_hint, sizeof(g_profile_hint), "%s", patched_hint_state);
        fprintf(stderr, "[a2h_patch] selected method=%s state=%s func=0x%lx\n",
                g_locate_method, patched_hint_state, (unsigned long)g_func_off);
        return 1;
    }

    fprintf(stderr, "[a2h_patch] ERROR: cannot locate is_A2H_app (scan=%d best_score=%d profile_head_ok=%d)\n",
            sig_ok, best_score, profile_head_ok);
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
    if (load_cave_hint(&hinted_cave) && hinted_cave >= g_elf_tail_start &&
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
                         (reused == 2 ? "elf-tail-stub-repair" : "elf-tail-zero");
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
    save_cave_hint(slot_off(0));
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
        remote_icache_flush(pid, func, code_bytes);
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
    remote_icache_flush(pid, func, 96);
    if (!write_whitelist_stub_code(pid, base)) return 0;
    remote_icache_flush(pid, func, 96);
    syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL, 0);
    syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL_EXPEDITED, 0);
    unsigned char head[16]={0};
    if (mem_r(pid, func, head, 16) != 0) return 0;
    int stubbed = is_stub_head(head, 16);
    fprintf(stderr, "[a2h_patch] stub head=%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x (%s)\n",
            head[0],head[1],head[2],head[3],head[4],head[5],head[6],head[7],head[8],head[9],head[10],head[11],head[12],head[13],head[14],head[15],
            stubbed?"stub-ok":"BAD");
    return stubbed ? 1 : 0;
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
        int stubbed = is_stub_head(head, 16);
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

static int load_func_off_hint(uintptr_t *out) {
    const char *paths[] = {
        "/data/adb/modules/a2h_hook/config/func_off",
        "/data/local/tmp/a2h_func_off",
        NULL
    };
    for (int i=0; paths[i]; ++i) {
        FILE *f=fopen(paths[i], "r");
        if (!f) continue;
        unsigned long v=0;
        if (fscanf(f, "%lx", &v) == 1 && v >= 0x10000ul && v <= 0x2000000ul) {
            fclose(f);
            if (out) *out = (uintptr_t)v;
            fprintf(stderr, "[a2h_patch] func_off hint from %s: 0x%lx\n", paths[i], v);
            return 1;
        }
        fclose(f);
    }
    return 0;
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
        uintptr_t t=find_lib_base_and_maps(pid,n1,&rw_s,&rw_e,&rx_s,&rx_e);
        if(t) snprintf(g_libname,sizeof(g_libname),"%s",n1);
        else {
            t=find_lib_base_and_maps(pid,n2,&rw_s,&rw_e,&rx_s,&rx_e);
            if(t) snprintf(g_libname,sizeof(g_libname),"%s",n2);
            else (void)find_audio_primary_base_and_maps(pid,&rw_s,&rw_e,&rx_s,&rx_e);
        }
        base=base_override;
    }
    if(!base){fprintf(stderr,"[a2h_patch] ERROR: target map missing\n"); if(g_attached)trace_detach(pid); return 2;}
    g_rx_start = rx_s; g_rx_end = rx_e;
    if (base && rw_s >= base) { g_rw_start = rw_s - base; g_rw_end = rw_e ? (rw_e - base) : 0; }
    fprintf(stderr,"[a2h_patch] lib=%s base=0x%lx rw_abs=0x%lx-0x%lx rx_abs=0x%lx-0x%lx\n",
            g_libname[0]?g_libname:"?", (unsigned long)base, (unsigned long)rw_s, (unsigned long)rw_e,
            (unsigned long)rx_s, (unsigned long)rx_e);
    g_attached = (trace_attach(pid) == 0);
    fprintf(stderr,"[a2h_patch] ptrace=%s\n", g_attached?"ok":"unavailable");
    if (!discover_elf_tail_region(pid, base, WHITELIST_CAVE_BYTES)) {
        fprintf(stderr,
                "[a2h_patch] WARN: whitelist ELF tail unavailable; global mode remains eligible\n");
    }
#ifdef A2H_DIAGNOSTIC_SCAN_ONLY
    uintptr_t diagnostic_off = 0;
    int diagnostic_ok = scan_func_by_sig(pid, base, &diagnostic_off);
    fprintf(stderr,
            "[a2h_patch] diagnostic universal scan=%s func=0x%lx kind=%s\n",
            diagnostic_ok ? "OK" : "FAIL", (unsigned long)diagnostic_off,
            g_scan_kind[0] ? g_scan_kind : "none");
    if (g_attached) trace_detach(pid);
    return diagnostic_ok ? 0 : 2;
#endif
    if (!locate_targets(pid, base)) {
        fprintf(stderr, "[a2h_patch] ERROR: target location unresolved; no unsafe hint fallback\n");
        if(g_attached)trace_detach(pid);
        return 2;
    }
    if (mode == 0 || mode == 1) save_func_off_hint(k_func_off());
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
    int already_stub = is_stub_head(vfy8, 16);
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
        int rc=apply_strings(pid,base,pkgs);
        int stub_ok=0;
        if(rc) stub_ok=install_whitelist_stub(pid,base);
        else fprintf(stderr,"[a2h_patch] skip stub because string apply failed\n");
        unsigned char vf[16]={0}; mem_r(pid,func_addr,vf,16);
        int stubbed=is_stub_head(vf,16);
        stub_info_t si;
        memset(&si, 0, sizeof(si));
        int table_ok = stubbed && inspect_stub_table(pid, base, k_func_off(), vf, pkgs, 1, &si);
        int final_ok = rc && stub_ok && stubbed && table_ok && si.content_mismatch == 0;
        free_pkgs(pkgs);
        fprintf(stderr,"whitelist: %s\n", final_ok?"OK":"FAIL");
        fprintf(stderr,"[a2h_patch] summary method=%s profile=%s hint=%s write=%s stub=%s table=%s final=%s active=%d active_ptrs=%d lines=%d rejected=%d fallback=%d all_off=%d mismatch=%d icache=ivau\n",
                g_locate_method,g_profile,g_profile_hint, rc?"OK":"FAIL", stub_ok?"OK":"FAIL",
                table_ok?"OK":"FAIL", stubbed?"whitelist":"not-whitelist",
                stats.active, si.active_ptrs, stats.lines_read, stats.rejected,
                stats.fallback_defaults, stats.explicit_all_off, si.content_mismatch);
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
        return final_ok?0:1;
    }
    int global_marked = memcmp(vfy8, GLOBAL_PATCH, sizeof(GLOBAL_PATCH)) == 0;
    if (already_global && global_marked){fprintf(stderr,"already enabled\n"); if(g_attached)trace_detach(pid); fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0); return 0;}
    fprintf(stderr, already_global ? "upgrading global marker...\n" : "enabling global...\n");
    if (mem_w(pid,func_addr,GLOBAL_PATCH,sizeof(GLOBAL_PATCH)) != 0) {
        fprintf(stderr,"enable: FAIL\n"); if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0); return 1;
    }
    remote_icache_flush(pid, func_addr, 16);
    mem_w(pid,func_addr,GLOBAL_PATCH,sizeof(GLOBAL_PATCH));
    remote_icache_flush(pid, func_addr, 16);
    syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL, 0);
    unsigned char vf[16]={0}; mem_r(pid,func_addr,vf,sizeof(vf));
    int ok = memcmp(vf,GLOBAL_PATCH,sizeof(GLOBAL_PATCH))==0;
    fprintf(stderr,"enable: %s\n",ok?"OK":"FAIL");
    fprintf(stderr,"[a2h_patch] global verify head=%02x %02x %02x %02x method=%s profile=%s hint=%s icache=ivau\n",
            vf[0],vf[1],vf[2],vf[3], g_locate_method, g_profile, g_profile_hint);
    if(g_attached)trace_detach(pid);
    fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
    return ok?0:1;
}
