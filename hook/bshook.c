#include <link.h>        // dl_iterate_phdr, struct dl_phdr_info
#include <elf.h>         // Elf64_Phdr/Dyn/Rela/Sym, DT_*, PT_*, ELF64_R_*, R_AARCH64_JUMP_SLOT
#include <sys/mman.h>    // mprotect, PROT_*
#include <unistd.h>      // sysconf, _SC_PAGESIZE
#include <string.h>      // strstr, strcmp
#include <android/log.h> // __android_log_print


static int my_callback(struct dl_phdr_info *info, size_t size, void *data) {
    if (strstr(info->dlpi_name, "libbinder.so")) {
        __android_log_print(ANDROID_LOG_INFO, "HOOK", "found libbinder");
        *(struct dl_phdr_info *)data = *info;
        return 1;
    }
    return 0;
}
__attribute__((constructor))
static void bshook_init(void) {
    struct dl_phdr_info libbinder = {0};
    __android_log_print(ANDROID_LOG_INFO, "HOOK", "loaded");
    dl_iterate_phdr(my_callback, &libbinder);
}


