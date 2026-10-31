// injector — runtime .so injection into an ALREADY-RUNNING process (no code baked
// into the app). Uses ptrace purely as a delivery van:
//   1. PTRACE_ATTACH the target
//   2. hijack a thread: set x0=path, pc=dlopen, lr=0, run it
//   3. dlopen() loads bshook.so -> its constructor installs the GOT hook
//   4. PTRACE_DETACH -> ptrace is gone; the hook keeps running natively
//
// dlopen's address in the target is computed from libdl.so's load base in the
// target plus dlopen's offset within libdl in our own process (same file, same
// offset). The .so path should live somewhere the target's linker namespace
// permits (e.g. /system/lib64), otherwise dlopen returns 0.
//
// usage: injector <pid> <absolute-path-to.so>
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <elf.h>

struct a64 { uint64_t regs[31], sp, pc, pstate; };

// load base of the first mapping whose path contains `name`, in /proc/<pid>/maps
static uintptr_t module_base(pid_t pid, const char *name) {
    char path[64]; snprintf(path, sizeof path, "/proc/%d/maps", pid);
    FILE *f = fopen(path, "r"); if (!f) return 0;
    char line[512]; uintptr_t base = 0;
    while (fgets(line, sizeof line, f)) {
        if (strstr(line, name)) { base = (uintptr_t)strtoull(line, 0, 16); break; }
    }
    fclose(f); return base;
}

static int getregs(pid_t pid, struct a64 *r) {
    struct iovec io = { r, sizeof *r };
    return ptrace(PTRACE_GETREGSET, pid, (void *)NT_PRSTATUS, &io);
}
static int setregs(pid_t pid, struct a64 *r) {
    struct iovec io = { r, sizeof *r };
    return ptrace(PTRACE_SETREGSET, pid, (void *)NT_PRSTATUS, &io);
}
static int write_mem(pid_t pid, uintptr_t addr, const void *buf, size_t n) {
    struct iovec l = { (void *)buf, n }, r = { (void *)addr, n };
    return process_vm_writev(pid, &l, 1, &r, 1, 0) == (ssize_t)n ? 0 : -1;
}

// run func(a0,a1) in the target, return x0. lr=0 so the return faults and we regain control.
static uint64_t remote_call(pid_t pid, const struct a64 *saved, uint64_t func,
                            uint64_t a0, uint64_t a1) {
    struct a64 r = *saved;
    r.regs[0] = a0; r.regs[1] = a1;
    r.regs[30] = 0;              // lr -> 0: return branches to 0 => SIGSEGV we catch
    r.pc = func;
    r.sp &= ~15ULL;             // keep sp 16-aligned
    if (setregs(pid, &r)) { perror("setregs"); return 0; }
    for (;;) {
        if (ptrace(PTRACE_CONT, pid, 0, 0)) { perror("CONT"); return 0; }
        int st; if (waitpid(pid, &st, 0) < 0) return 0;
        if (WIFEXITED(st) || WIFSIGNALED(st)) { fprintf(stderr, "target died during call\n"); return 0; }
        if (WIFSTOPPED(st)) {
            int sig = WSTOPSIG(st);
            if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGTRAP) {
                struct a64 out; getregs(pid, &out);
                return out.regs[0];   // x0 = return value
            }
            // some other signal; pass it along and keep waiting
            ptrace(PTRACE_CONT, pid, 0, (void *)(uintptr_t)sig);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <pid> <abs .so path>\n", argv[0]); return 2; }
    pid_t pid = atoi(argv[1]);
    const char *sopath = argv[2];

    // dlopen's offset within libdl.so, measured in OUR process
    uintptr_t my_libdl = module_base(getpid(), "libdl.so");
    void *my_dlopen = dlsym(RTLD_DEFAULT, "dlopen");
    if (!my_libdl || !my_dlopen) { fprintf(stderr, "cannot locate local dlopen/libdl\n"); return 1; }
    uintptr_t dlopen_off = (uintptr_t)my_dlopen - my_libdl;

    uintptr_t tgt_libdl = module_base(pid, "libdl.so");
    if (!tgt_libdl) { fprintf(stderr, "no libdl.so in target %d\n", pid); return 1; }
    uint64_t tgt_dlopen = tgt_libdl + dlopen_off;
    printf("[inj] my libdl=%#lx dlopen=%p off=%#lx | tgt libdl=%#lx dlopen=%#llx\n",
           (unsigned long)my_libdl, my_dlopen, (unsigned long)dlopen_off,
           (unsigned long)tgt_libdl, (unsigned long long)tgt_dlopen);

    if (ptrace(PTRACE_ATTACH, pid, 0, 0)) { perror("ATTACH"); return 1; }
    int st; waitpid(pid, &st, 0);

    struct a64 saved; if (getregs(pid, &saved)) { perror("getregs"); ptrace(PTRACE_DETACH, pid, 0, 0); return 1; }

    // write the .so path into the target's stack red-zone
    uint64_t str_addr = (saved.sp - 1024) & ~15ULL;
    if (write_mem(pid, str_addr, sopath, strlen(sopath) + 1)) {
        perror("write path"); ptrace(PTRACE_DETACH, pid, 0, 0); return 1;
    }

    // RTLD_NOW = 2
    uint64_t handle = remote_call(pid, &saved, tgt_dlopen, str_addr, 2);
    printf("[inj] dlopen(\"%s\") => %#llx %s\n", sopath,
           (unsigned long long)handle, handle ? "(loaded)" : "(FAILED)");

    // restore original state and let the app run on with the hook installed
    setregs(pid, &saved);
    ptrace(PTRACE_DETACH, pid, 0, 0);
    return handle ? 0 : 1;
}
