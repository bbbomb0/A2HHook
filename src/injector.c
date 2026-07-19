// injector.c - arm64 ptrace library injector for A2HHook
// Compile: clang -static -o injector injector.c (via CMake)
// Usage: injector [target_process] [library_path]

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <elf.h>
#include <sys/wait.h>

// arm64 ptrace constants (from Linux uapi)
#define ARM64_NT_PRSTATUS 1
#define PTRACE_ATTACH 16
#define PTRACE_DETACH 17
#define PTRACE_GETREGSET 0x4204
#define PTRACE_SETREGSET 0x4205
#define PTRACE_PEEKDATA 2
#define PTRACE_POKEDATA 5
#define PTRACE_CONT 7

// arm64 register layout (34 x 64-bit: x0-x30 + sp + pc + pstate)
struct arm64_regs {
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

// Syscall wrappers (avoids libc wrapper issues with static builds)
static long _ptrace(int req, pid_t pid, void *addr, void *data) {
    return syscall(117, req, pid, addr, data);  // __NR_ptrace = 117 on arm64
}

// Find PID by process name substring
static int find_pid(const char *name) {
    DIR *d = opendir("/proc");
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_type != DT_DIR) continue;
        char path[256], buf[256];
        snprintf(path, sizeof(path), "/proc/%s/cmdline", de->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;
        ssize_t n = read(fd, buf, sizeof(buf)-1);
        close(fd);
        if (n <= 0) continue;
        buf[n] = 0;
        if (strstr(buf, name)) { closedir(d); return atoi(de->d_name); }
    }
    closedir(d);
    return -1;
}

// Find library base address in target process maps
static uint64_t find_lib_base(int pid, const char *libname) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, libname) && strstr(line, "r-xp")) {
            uint64_t base = strtoull(line, NULL, 16);
            fclose(fp);
            return base;
        }
    }
    fclose(fp);
    return 0;
}

// Find dlopen offset in a local ELF file
static uint64_t elf_find_symbol(const char *elf_path, const char *sym_name) {
    int fd = open(elf_path, O_RDONLY);
    if (fd < 0) return 0;
    
    Elf64_Ehdr ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) { close(fd); return 0; }
    if (ehdr.e_ident[0] != 0x7f) { close(fd); return 0; }
    
    // Read section headers
    Elf64_Shdr *shdrs = malloc(ehdr.e_shentsize * ehdr.e_shnum);
    lseek(fd, ehdr.e_shoff, SEEK_SET);
    if (read(fd, shdrs, ehdr.e_shentsize * ehdr.e_shnum) <= 0) {
        free(shdrs); close(fd); return 0;
    }
    
    // Read section name string table
    Elf64_Shdr *shstr = &shdrs[ehdr.e_shstrndx];
    char *shnames = malloc(shstr->sh_size);
    lseek(fd, shstr->sh_offset, SEEK_SET);
    read(fd, shnames, shstr->sh_size);
    
    // Find .dynsym and .dynstr
    Elf64_Off dynsym_off = 0, dynstr_off = 0;
    size_t dynsym_size = 0, dynstr_size = 0;
    for (int i = 0; i < ehdr.e_shnum; i++) {
        const char *sname = shnames + shdrs[i].sh_name;
        if (strcmp(sname, ".dynsym") == 0) {
            dynsym_off = shdrs[i].sh_offset;
            dynsym_size = shdrs[i].sh_size;
        } else if (strcmp(sname, ".dynstr") == 0) {
            dynstr_off = shdrs[i].sh_offset;
            dynstr_size = shdrs[i].sh_size;
        }
    }
    free(shnames);
    
    if (!dynsym_off || !dynstr_off) { free(shdrs); close(fd); return 0; }
    
    // Read dynamic symbol table
    Elf64_Sym *syms = malloc(dynsym_size);
    lseek(fd, dynsym_off, SEEK_SET);
    read(fd, syms, dynsym_size);
    
    // Read dynamic string table
    char *strs = malloc(dynstr_size);
    lseek(fd, dynstr_off, SEEK_SET);
    read(fd, strs, dynstr_size);
    
    uint64_t result = 0;
    int nsyms = dynsym_size / sizeof(Elf64_Sym);
    for (int i = 0; i < nsyms; i++) {
        if (syms[i].st_name < dynstr_size &&
            strcmp(strs + syms[i].st_name, sym_name) == 0) {
            result = syms[i].st_value;
            break;
        }
    }
    
    free(strs);
    free(syms);
    free(shdrs);
    close(fd);
    return result;
}

int main(int argc, char *argv[]) {
    const char *target = argc > 1 ? argv[1] : "audioserver";
    const char *libpath = argc > 2 ? argv[2] : "/data/adb/modules/a2h_hook/zygisk/arm64-v8a.so";
    
    fprintf(stderr, "[inject] target=%s lib=%s\n", target, libpath);
    
    // 1. Find target PID
    int pid = find_pid(target);
    if (pid < 0) {
        fprintf(stderr, "[inject] ERROR: %s not found\n", target);
        return 1;
    }
    fprintf(stderr, "[inject] %s pid=%d\n", target, pid);
    
    // 2. Find libdl.so base in target
    uint64_t libdl_base = find_lib_base(pid, "libdl.so");
    if (!libdl_base) {
        // Try libc.so (dlopen might be there on newer Android)
        libdl_base = find_lib_base(pid, "libc.so");
        if (!libdl_base) {
            fprintf(stderr, "[inject] ERROR: cannot find libdl/libc in target\n");
            return 1;
        }
        fprintf(stderr, "[inject] libc.so at 0x%lx\n", (unsigned long)libdl_base);
    } else {
        fprintf(stderr, "[inject] libdl.so at 0x%lx\n", (unsigned long)libdl_base);
    }
    
    // 3. Find dlopen offset in local ELF
    uint64_t dlopen_off = elf_find_symbol("/apex/com.android.runtime/lib64/bionic/libdl.so", "dlopen");
    if (!dlopen_off) {
        dlopen_off = elf_find_symbol("/apex/com.android.runtime/lib64/bionic/libc.so", "dlopen");
    }
    if (!dlopen_off) {
        fprintf(stderr, "[inject] ERROR: cannot find dlopen offset\n");
        return 1;
    }
    uint64_t dlopen_addr = libdl_base + dlopen_off;
    fprintf(stderr, "[inject] dlopen at 0x%lx\n", (unsigned long)dlopen_addr);
    
    // 4. Attach
    if (_ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
        perror("[inject] PTRACE_ATTACH");
        return 1;
    }
    waitpid(pid, NULL, 0);
    fprintf(stderr, "[inject] attached\n");
    
    // 5. Save registers
    struct arm64_regs old_regs;
    struct iovec iov = { .iov_base = &old_regs, .iov_len = sizeof(old_regs) };
    if (_ptrace(PTRACE_GETREGSET, pid, (void*)(long)ARM64_NT_PRSTATUS, &iov) < 0) {
        perror("[inject] GETREGSET");
        _ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    
    // 6. Write path to target stack
    size_t pathlen = strlen(libpath) + 1;
    size_t alloc = (pathlen + 15) & ~15UL; // 16-byte aligned
    uint64_t str_addr = old_regs.sp - alloc;
    
    for (size_t i = 0; i < alloc; i += 8) {
        uint64_t word = 0;
        size_t copy = (i + 8 <= pathlen) ? 8 : pathlen - i;
        memcpy(&word, libpath + i, copy);
        if (_ptrace(PTRACE_POKEDATA, pid, (void*)(str_addr + i), (void*)word) < 0) {
            perror("[inject] POKEDATA");
            _ptrace(PTRACE_DETACH, pid, NULL, NULL);
            return 1;
        }
    }
    fprintf(stderr, "[inject] wrote path to 0x%lx\n", (unsigned long)str_addr);
    
    // 7. Set up dlopen call
    struct arm64_regs new_regs = old_regs;
    new_regs.x[0] = str_addr;  // x0 = path
    new_regs.x[1] = 2;         // x1 = RTLD_NOW
    new_regs.x[30] = 0;        // lr = 0 (SIGSEGV on return - we catch it)
    new_regs.pc = dlopen_addr;
    new_regs.sp = str_addr;
    
    iov.iov_base = &new_regs;
    if (_ptrace(PTRACE_SETREGSET, pid, (void*)(long)ARM64_NT_PRSTATUS, &iov) < 0) {
        perror("[inject] SETREGSET");
        _ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return 1;
    }
    
    // 8. Execute
    fprintf(stderr, "[inject] calling dlopen...\n");
    _ptrace(PTRACE_CONT, pid, NULL, NULL);
    int status;
    waitpid(pid, &status, 0);
    
    if (WIFSTOPPED(status)) {
    // Capture x0 (dlopen return value)
    struct arm64_regs ret_regs;
    struct iovec ret_iov = { .iov_base = &ret_regs, .iov_len = sizeof(ret_regs) };
    if (_ptrace(PTRACE_GETREGSET, pid, (void*)(long)ARM64_NT_PRSTATUS, &ret_iov) >= 0) {
        fprintf(stderr, "[inject] dlopen x0=0x%lx (0=FAIL, non-zero=SUCCESS)\n", (unsigned long)ret_regs.x[0]);
    }
    int sig = WSTOPSIG(status);
    fprintf(stderr, "[inject] signal=%d (%s)\n", sig, sig==SIGSEGV?"SEGV":sig==SIGTRAP?"TRAP":"?");
    if (sig == SIGSEGV) {
        fprintf(stderr, "[inject] dlopen returned! (SIGSEGV from lr=0)\n");
    }
}
    
    // 9. Restore & detach
    iov.iov_base = &old_regs;
    _ptrace(PTRACE_SETREGSET, pid, (void*)(long)ARM64_NT_PRSTATUS, &iov);
    _ptrace(PTRACE_DETACH, pid, NULL, NULL);
    fprintf(stderr, "[inject] done\n");
    
    return 0;
}
