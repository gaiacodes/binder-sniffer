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
    struct dl_phdr_info libbinder_info = {0};
    __android_log_print(ANDROID_LOG_INFO, "HOOK", "loaded");
    dl_iterate_phdr(my_callback, &libbinder_info);
    const Elf64_Dyn *dynamic = NULL;
    for (int i=0; i < libbinder_info.dlpi_phnum; i++) {
        const Elf64_Phdr * ph = &libbinder_info.dlpi_phdr[i];
        if (ph->p_type == PT_DYNAMIC) {
            dynamic = (const Elf64_Dyn * ) (libbinder_info.dlpi_addr + ph->p_vaddr);
        }
    }
    if (dynamic == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "HOOK", "no PT_DYNAMIC found");
        return;
    }
    Elf64_Rela * relocation_table_addr = NULL;
    Elf64_Xword relocation_table_size = 0;
    Elf64_Sym * symbol_table_addr = NULL;
    const char * string_blob_addr = NULL;

    for(int i = 0; dynamic[i].d_tag != DT_NULL; i++) {
        if (dynamic[i].d_tag == DT_JMPREL) {
            relocation_table_addr = (Elf64_Rela *)(libbinder_info.dlpi_addr + dynamic[i].d_un.d_ptr);
        }
        if (dynamic[i].d_tag == DT_PLTRELSZ) {
            relocation_table_size = dynamic[i].d_un.d_val;
        }
        if (dynamic[i].d_tag == DT_SYMTAB) {
            symbol_table_addr = (Elf64_Sym *)(libbinder_info.dlpi_addr + dynamic[i].d_un.d_ptr);
        }
        if (dynamic[i].d_tag == DT_STRTAB) {
            string_blob_addr = (const char *)(libbinder_info.dlpi_addr + dynamic[i].d_un.d_ptr);
        }
    }
}


