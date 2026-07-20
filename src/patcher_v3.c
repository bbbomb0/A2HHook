// a2h_patch v1.5.2-fix - v1.0 universal signature scan + 10-slot whitelist
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
#define A2H_VERSION "1.5.2-fix"

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
    {"os3_0x3e3fc0","HyperOS3/OS3-like",0x3E3FC0,{0xADC9C,0xB19F4,0xB507E,0xBC5CC,0xBC5DB,0xFCFFA},{17,19,22,14,15,14}},
    {"os2_0x3e4280","HyperOS2/OS2-like",0x3E4280,{0xADE45,0xB1BBB,0xB5280,0xBC7CE,0xBC7DD,0xFD0FF},{17,19,22,14,15,14}},
};
static slot_t slots[MAX_SLOTS];
static uintptr_t g_func_off=0x3E3FC0,g_ptr_off=0x437200,g_stub_mark=0x437100,g_rw_start=0,g_rw_end=0,g_rx_start=0,g_rx_end=0;
static char g_libname[64]={0}, g_profile[48]={0}, g_profile_hint[48]={0}, g_locate_method[32]={0}, g_scan_kind[16]={0};
static int g_attached=0;
static uintptr_t slot_off(int i){return upx(slots[i].off_x);} 
static void set_slot_off(int i,uintptr_t off){slots[i].off_x=(uint32_t)off^0xA5C31F77u;} 
static uintptr_t k_func_off(void){return g_func_off;} 
static int load_func_off_hint(uintptr_t *out);
static const unsigned char SIG8[8]={0xe0,0x04,0x00,0xb4,0xfd,0x7b,0xbe,0xa9};
static const unsigned char PATCH[8]={0x20,0x00,0x80,0x52,0xc0,0x03,0x5f,0xd6};
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
static void log_prop(const char *key){
    char cmd[192], val[256]={0};
    snprintf(cmd,sizeof(cmd),"/system/bin/getprop %s 2>/dev/null", key);
    FILE *f=popen(cmd,"r");
    if(f){ if(fgets(val,sizeof(val),f)){ size_t n=strlen(val); while(n&&(val[n-1]=='\n'||val[n-1]=='\n')) val[--n]=0; } pclose(f); }
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
    char line[512]; uintptr_t base=0,rws=0,rwe=0,rxs=0,rxe=0;
    while(fgets(line,sizeof(line),f)) {
        if(!strstr(line,name)) continue;
        uintptr_t s=0,e=0; unsigned off=0; char perms[8]={0};
        if (sscanf(line, "%lx-%lx %7s %x", &s, &e, perms, &off) < 4) continue;
        if (!base && off == 0) base = s;
        if (!base) base = s;
        if (strchr(perms, 'x')) {
            if (!rxs || s < rxs) rxs = s;
            if (e > rxe) rxe = e;
        }
        if (strchr(perms, 'w')) {
            if (!rws || (e - s) >= (rwe - rws)) { rws = s; rwe = e; }
        }
    }
    fclose(f);
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
    if(!valid_pkg(s)) return 0;
    if(slot>=0 && slot<6 && strlen(s)>(size_t)slots[slot].max_len) return 0;
    if(slot>=6 && strlen(s)>63) return 0;
    return 1;
}
static void free_pkgs(char **pkgs) {
    if(!pkgs) return;
    for(int i=0;i<MAX_SLOTS;++i) free(pkgs[i]);
    free(pkgs);
}
static char **read_pkgs(const char *path, int *rejected) {
    char **p = calloc(MAX_SLOTS, sizeof(char*));
    FILE *f = fopen(path, "r");
    if (!p) return NULL;
    if (rejected) *rejected = 0;
    if (!f) {
        for (int i = 0; i < 6; ++i) { char tmp[64]; pkg_default(i, tmp, sizeof(tmp)); p[i] = strdup(tmp); }
        fprintf(stderr, "[a2h_patch] packages: fallback defaults (file missing: %s)\n", path);
        return p;
    }
    char line[256]; int sl=0;
    while(fgets(line,sizeof(line),f)&&sl<MAX_SLOTS) {
        trim(line);
        if(line[0]=='#') continue;
        if(line[0]) {
            if (valid_pkg_for_slot(line, sl)) p[sl] = strdup(line);
            else { fprintf(stderr, "[a2h_patch] slot%d rejected: '%s'\n", sl, line); if (rejected) (*rejected)++; p[sl]=NULL; }
        } else p[sl]=NULL;
        sl++;
    }
    fclose(f);
    fprintf(stderr, "[a2h_patch] packages file=%s lines_read=%d\n", path, sl);
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
static int score_profile(pid_t pid, uintptr_t base, const profile_t *prof) {
    int score = 0; unsigned char head[16] = {0};
    if (mem_r(pid, base + prof->func_off, head, sizeof(head)) != 0) return -1000;
    if (memcmp(head, SIG8, 8) == 0) score += 50;
    if (memcmp(head, PATCH, 8) == 0) score += 40;
    if (is_stub_head(head, sizeof(head))) score += 35;
    for (int i = 0; i < 6; ++i) {
        char got[64]={0}, exp[64]={0}; pkg_default(i, exp, sizeof(exp));
        if (mem_r(pid, base + prof->slot_off[i], got, prof->slot_len[i]) != 0) { score -= 5; continue; }
        if (exp[0] && strncmp(got, exp, prof->slot_len[i]) == 0) score += 10;
        else if (got[0] && strchr(got, '.')) score += 2;
        else score -= 3;
    }
    fprintf(stderr, "[a2h_patch] profile %s (%s) score=%d head=%02x %02x %02x %02x\n",
            prof->name, prof->hint, score, head[0], head[1], head[2], head[3]);
    return score;
}
// Prefer true stock prologue only. Full-text ADRP/global scanning is too noisy after patch.
static int scan_func_by_sig(pid_t pid, uintptr_t base, uintptr_t *out_off) {
    // Locate is_A2H_app even after patching:
    // 1) stock prologue SIG8
    // 2) global patch (mov w0,#1; ret)
    // 3) our whitelist stub head (ADRP x1; ADD x1; MOV w2,#10)
    // Never full-text ADRP scan: too many false positives.
    char mp[64]; snprintf(mp,sizeof(mp),"/proc/%d/maps",pid);
    FILE *f=fopen(mp,"r"); if(!f) return 0;
    char line[512];
    int segs=0, hits_stock=0, hits_global=0, hits_stub=0;
    uintptr_t best_stock=0, best_global=0, best_stub=0;
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
                hits_global++; best_global = rel;
                continue;
            }
            if (is_stub_head(h, 16)) {
                hits_stub++; best_stub = rel;
            }
        }
        free(buf);
    }
    fclose(f);
    uintptr_t best = 0;
    const char *kind = "none";
    if (best_stock) { best = best_stock; kind = "stock"; }
    else if (best_global && hits_global <= 3) { best = best_global; kind = "global"; }
    else if (best_stub && hits_stub <= 3) { best = best_stub; kind = "stub"; }
    else if (best_global) { best = best_global; kind = "global-multi"; }
    else if (best_stub) { best = best_stub; kind = "stub-multi"; }

    if (!best) {
        fprintf(stderr, "[a2h_patch] func scan miss segs=%d stock=%d global=%d stub=%d\n",
                segs, hits_stock, hits_global, hits_stub);
        return 0;
    }
    if (out_off) *out_off = best;
    snprintf(g_scan_kind, sizeof(g_scan_kind), "%s", kind);
    fprintf(stderr, "[a2h_patch] is_A2H_app: 0x%lx kind=%s stock=%d global=%d stub=%d segs=%d\n",
            (unsigned long)best, kind, hits_stock, hits_global, hits_stub, segs);
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
static uintptr_t find_zero_cave(pid_t pid, uintptr_t rw_abs_s, uintptr_t rw_abs_e, uintptr_t need) {
    if (rw_abs_e <= rw_abs_s || need == 0) return 0;
    size_t len = (size_t)(rw_abs_e - rw_abs_s);
    if (len < (size_t)need + 64) return 0;
    if (len > 1024 * 1024) len = 1024 * 1024;
    unsigned char *buf = (unsigned char*)malloc(len);
    if (!buf) return 0;
    uintptr_t found = 0;
    if (mem_r(pid, rw_abs_s, buf, len) == 0) {
        size_t limit = len - (size_t)need;
        for (size_t pos = limit; pos > 0; ) {
            pos &= ~(size_t)0xF;
            int zero = 1;
            for (size_t j = 0; j < (size_t)need; ++j) {
                if (buf[pos + j] != 0) { zero = 0; break; }
            }
            if (zero) { found = rw_abs_s + pos; break; }
            if (pos == 0) break;
            pos -= 16;
        }
    }
    free(buf);
    return found;
}
static int locate_targets(pid_t pid, uintptr_t base) {
    g_scan_kind[0] = 0;

    // 0) If a previously saved func offset still looks valid, use it first.
    // This is the cheapest path on repeat runs for known OS2/OS3 builds.
    uintptr_t hint = 0;
    if (load_func_off_hint(&hint)) {
        unsigned char hh[16] = {0};
        if (mem_r(pid, base + hint, hh, 16) == 0) {
            const profile_t *hit = profile_for_func_off(hint);
            int hint_ok = hit ? head_is_known(hh, 16) : (memcmp(hh, SIG8, 8) == 0 || is_stub_head(hh, 16));
            if (!hint_ok) {
                fprintf(stderr, "[a2h_patch] func_off hint ignored: 0x%lx head=%02x %02x %02x %02x\n",
                        (unsigned long)hint, hh[0], hh[1], hh[2], hh[3]);
            } else {
                g_func_off = hint;
                if (hit) {
                    apply_profile(hit);
                    snprintf(g_locate_method, sizeof(g_locate_method), "hint+profile");
                    fprintf(stderr, "[a2h_patch] selected method=%s profile=%s hint=%s func=0x%lx\n",
                            g_locate_method, g_profile, g_profile_hint, (unsigned long)g_func_off);
                } else {
                    snprintf(g_locate_method, sizeof(g_locate_method), "hint");
                    snprintf(g_profile, sizeof(g_profile), "hint");
                    snprintf(g_profile_hint, sizeof(g_profile_hint), "saved");
                    fprintf(stderr, "[a2h_patch] selected method=hint func=0x%lx\n", (unsigned long)g_func_off);
                }
                return 1;
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
    if (fast_best >= 0 && fast_best_score >= 40) {
        unsigned char ph[16]={0};
        if (mem_r(pid, base + PROFILES[fast_best].func_off, ph, 16) == 0 && head_is_known(ph, 16)) {
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

    if (sig_ok && strcmp(g_scan_kind, "stock") == 0) {
        g_func_off = sig_off;
        snprintf(g_locate_method, sizeof(g_locate_method), "scan-stock");
        snprintf(g_profile, sizeof(g_profile), "universal");
        snprintf(g_profile_hint, sizeof(g_profile_hint), "v1.0-style");
        fprintf(stderr, "[a2h_patch] selected method=%s func=0x%lx kind=%s\n",
                g_locate_method, (unsigned long)g_func_off, g_scan_kind);
        return 1;
    }

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
            if (memcmp(ph, SIG8, 8)==0 || memcmp(ph, PATCH, 8)==0) profile_head_ok = 1;
            else if (is_stub_head(ph, 16)) profile_head_ok = 1;
        }
    }

    if (best >= 0 && best_score >= 40 && profile_head_ok) {
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

    if (best >= 0 && best_score >= 20 && profile_head_ok) {
        g_func_off = PROFILES[best].func_off;
        snprintf(g_locate_method, sizeof(g_locate_method), "profile-weak");
        fprintf(stderr, "[a2h_patch] selected method=%s profile=%s hint=%s func=0x%lx score=%d str_found=%d\n",
                g_locate_method, g_profile, g_profile_hint, (unsigned long)g_func_off, best_score, str_found);
        return 1;
    }

    fprintf(stderr, "[a2h_patch] ERROR: cannot locate is_A2H_app (scan=%d best_score=%d profile_head_ok=%d)\n",
            sig_ok, best_score, profile_head_ok);
    return 0;
}

static int setup_custom_cave(pid_t pid, uintptr_t base, uintptr_t rw_abs_s, uintptr_t rw_abs_e) {
    uintptr_t need = MAX_SLOTS * 64 + 16 + (MAX_SLOTS * 8) + 32;
    uintptr_t rws = rw_abs_s ? (rw_abs_s - base) : 0x434000;
    uintptr_t rwe = rw_abs_e ? (rw_abs_e - base) : 0x43A000;
    g_rw_start=rws; g_rw_end=rwe;
    if (rwe <= rws || (rwe - rws) < need + 64) {
        fprintf(stderr, "[a2h_patch] WARN: RW segment small/unknown, use fallback cave 0x437000\n");
        uintptr_t cave = 0x437000;
        for (int i = 0; i < MAX_SLOTS; ++i) {
            set_slot_off(i, cave + (uintptr_t)i * 64);
            slots[i].max_len = 63;
            slots[i].label = "x";
        }
        g_stub_mark = cave + MAX_SLOTS * 64;
        g_ptr_off = (g_stub_mark + 16 + 7) & ~((uintptr_t)7);
        return 0;
    }
    uintptr_t cave_abs = find_zero_cave(pid, rw_abs_s, rw_abs_e, need);
    uintptr_t cave = cave_abs ? (cave_abs - base) : ((rwe - need - 32) & ~((uintptr_t)0xF));
    const char *source = cave_abs ? "zero-scan" : "rw-tail";
    if (cave < rws) cave = rws;
    for (int i = 0; i < MAX_SLOTS; ++i) {
        set_slot_off(i, cave + (uintptr_t)i * 64);
        slots[i].max_len = 63;
        slots[i].label = "x";
    }
    g_stub_mark = cave + MAX_SLOTS * 64;
    g_ptr_off = (g_stub_mark + 16 + 7) & ~((uintptr_t)7);
    fprintf(stderr, "[a2h_patch] RW rel=0x%lx-0x%lx cave=0x%lx source=%s ptr=0x%lx mark=0x%lx need=0x%lx\n",
            (unsigned long)rws,(unsigned long)rwe,(unsigned long)cave,source,(unsigned long)g_ptr_off,(unsigned long)g_stub_mark,(unsigned long)need);
    return 1;
}
static int apply_strings(pid_t pid, uintptr_t base, char **pkgs) {
    int ok=1;
    for(int i=0;i<MAX_SLOTS;i++) {
        char buf[64]; memset(buf,0,sizeof(buf));
        size_t maxlen=(size_t)slots[i].max_len; if(maxlen>=sizeof(buf)) maxlen=sizeof(buf)-1;
        if(pkgs[i]&&pkgs[i][0]){ strncpy(buf,pkgs[i],maxlen); buf[maxlen]=0; }
        uintptr_t addr=base+slot_off(i);
        // always clear then write, avoid stale garbage in custom cave
        char z[64]; memset(z,0,sizeof(z));
        mem_w(pid,addr,z,maxlen+1);
        int wrc=mem_w(pid,addr,buf,maxlen+1);
        char verify[64]; memset(verify,0,sizeof(verify));
        int rrc=mem_r(pid,addr,verify,maxlen);
        int same=(rrc==0 && strncmp(verify,buf,maxlen)==0);
        fprintf(stderr,"[a2h_patch] slot%d off=0x%lx write=%s verify=%s value='%s'\n",
                i,(unsigned long)slot_off(i), wrc==0?"OK":"FAIL", same?"OK":"FAIL", buf[0]?buf:"(empty)");
        if(wrc!=0 || !same) ok=0;
    }
    return ok;
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
    uint64_t ptrs[MAX_SLOTS];
    for (int i=0;i<MAX_SLOTS;++i) {
        char first=0;
        if (mem_r(pid, base + slot_off(i), &first, 1) != 0) {
            fprintf(stderr, "[a2h_patch] stub prep read slot%d FAIL\n", i); return 0;
        }
        ptrs[i] = first ? (uint64_t)(base + slot_off(i)) : 0;
        fprintf(stderr, "[a2h_patch] ptr[%d]=%s\n", i, ptrs[i]?"set":"null");
    }
    if (mem_w(pid, table, ptrs, sizeof(ptrs)) != 0) {
        fprintf(stderr, "[a2h_patch] ptr table write FAIL @0x%lx\n", (unsigned long)g_ptr_off); return 0;
    }
    uint64_t chk=0; mem_r(pid, table, &chk, sizeof(chk));
    fprintf(stderr, "[a2h_patch] ptr table write OK first=0x%llx\n", (unsigned long long)chk);
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
    if (mem_w(pid, func, code, n*4) != 0) { fprintf(stderr,"[a2h_patch] stub code write FAIL\n"); return 0; }
    uint32_t magic=0x31483241; mem_w(pid, base + g_stub_mark, &magic, sizeof(magic));
    return 1;
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
            else t=find_audio_primary_base_and_maps(pid,&rw_s,&rw_e,&rx_s,&rx_e);
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
    if (!locate_targets(pid, base)) {
        // Last chance: previously saved function offset (helps OS2.0.220 after global when patterns are ambiguous).
        uintptr_t hint=0;
        if (load_func_off_hint(&hint)) {
            unsigned char hh[16]={0};
            if (mem_r(pid, base + hint, hh, 16) == 0) {
                int ok = 0;
                if (memcmp(hh, SIG8, 8)==0 || memcmp(hh, PATCH, 8)==0) ok = 1;
                else if (is_stub_head(hh, 16)) ok = 1;
                if (ok) {
                    g_func_off = hint;
                    snprintf(g_locate_method, sizeof(g_locate_method), "hint");
                    fprintf(stderr, "[a2h_patch] selected method=hint func=0x%lx\n", (unsigned long)hint);
                } else {
                    fprintf(stderr, "[a2h_patch] func_off hint invalid head\n");
                    if(g_attached)trace_detach(pid); return 2;
                }
            } else {
                if(g_attached)trace_detach(pid); return 2;
            }
        } else {
            if(g_attached)trace_detach(pid); return 2;
        }
    }
    save_func_off_hint(k_func_off());
    setup_custom_cave(pid, base, rw_s, rw_e);
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
        int ok = (check_want_global==1) ? already_global : already_stub;
        fprintf(stderr,"[a2h_patch] live: want=%s cur=%s head=%02x %02x %02x %02x method=%s func=0x%lx\n",
                check_want_global==1?"global":"whitelist", cur, vfy8[0],vfy8[1],vfy8[2],vfy8[3],
                g_locate_method[0]?g_locate_method:"?", (unsigned long)k_func_off());
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
        return ok?0:1;
    }
    if(mode==1){
        fprintf(stderr,"applying whitelist...\n");
        int rejected=0; char **pkgs=read_pkgs(pkgfile,&rejected);
        if(!pkgs){ fprintf(stderr,"[a2h_patch] ERROR: config\n"); if(g_attached)trace_detach(pid); return 1; }
        for(int i=0;i<MAX_SLOTS;i++) fprintf(stderr,"[a2h_patch] cfg[%d]=%s\n", i, (pkgs[i]&&pkgs[i][0])?pkgs[i]:"(empty)");
        int rc=apply_strings(pid,base,pkgs);
        int stub_ok=0;
        if(rc) stub_ok=install_whitelist_stub(pid,base);
        else fprintf(stderr,"[a2h_patch] skip stub because string apply failed\n");
        free_pkgs(pkgs);
        unsigned char vf[16]={0}; mem_r(pid,func_addr,vf,16);
        int stubbed=is_stub_head(vf,16);
        fprintf(stderr,"whitelist: %s\n", (rc && stub_ok && stubbed)?"OK":"FAIL");
        fprintf(stderr,"[a2h_patch] summary method=%s profile=%s hint=%s write=%s stub=%s final=%s rejected=%d icache=ivau\n",
                g_locate_method,g_profile,g_profile_hint, rc?"OK":"FAIL", stub_ok?"OK":"FAIL", stubbed?"whitelist":"not-whitelist", rejected);
        if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
        return (rc && stub_ok && stubbed)?0:1;
    }
    if(already_global){fprintf(stderr,"already enabled\n"); if(g_attached)trace_detach(pid); fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0); return 0;}
    fprintf(stderr,"enabling global...\n");
    if (mem_w(pid,func_addr,PATCH,8) != 0) {
        fprintf(stderr,"enable: FAIL\n"); if(g_attached)trace_detach(pid);
        fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0); return 1;
    }
    remote_icache_flush(pid, func_addr, 16);
    mem_w(pid,func_addr,PATCH,8);
    remote_icache_flush(pid, func_addr, 16);
    syscall(__NR_membarrier, MEMBARRIER_CMD_GLOBAL, 0);
    unsigned char vf[8]={0}; mem_r(pid,func_addr,vf,8);
    int ok = memcmp(vf,PATCH,8)==0;
    fprintf(stderr,"enable: %s\n",ok?"OK":"FAIL");
    fprintf(stderr,"[a2h_patch] global verify head=%02x %02x %02x %02x method=%s profile=%s hint=%s icache=ivau\n",
            vf[0],vf[1],vf[2],vf[3], g_locate_method, g_profile, g_profile_hint);
    if(g_attached)trace_detach(pid);
    fprintf(stderr,"[a2h_patch] elapsed_ms=%ld\n", now_ms()-t0);
    return ok?0:1;
}
