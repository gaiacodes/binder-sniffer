#include <link.h>        // dl_iterate_phdr, struct dl_phdr_info
#include <elf.h>         // Elf64_Phdr/Dyn/Rela/Sym, DT_*, PT_*, ELF64_R_*, R_AARCH64_JUMP_SLOT
#include <sys/mman.h>    // mprotect, PROT_*
#include <unistd.h>      // sysconf, _SC_PAGESIZE
#include <string.h>      // strstr, strcmp
#include <stdint.h>      // uintptr_t
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
    void **got_slot = NULL;

    for (int i = 0; i < relocation_table_size / sizeof(Elf64_Rela); i++) {
        Elf64_Rela relocation = relocation_table_addr[i];
        Elf64_Xword sym_index = ELF64_R_SYM(relocation.r_info);
        const char *name = string_blob_addr + symbol_table_addr[sym_index].st_name;
        if (strcmp(name, "ioctl") == 0) {
            got_slot = (void **)(libbinder_info.dlpi_addr + relocation.r_offset);
            break;
        }
    }
    if (got_slot == NULL) {
        __android_log_print(ANDROID_LOG_ERROR, "HOOK", "no ioctl got slot found");
        return;
    }
    __android_log_print(ANDROID_LOG_INFO, "HOOK", "ioctl got_slot=%p  *got_slot=%p",
                        (void *)got_slot, *got_slot);

    size_t pagesize = sysconf(_SC_PAGESIZE);
    uintptr_t page_start = (uintptr_t)got_slot & ~((uintptr_t)pagesize - 1);
    if (mprotect((void *)page_start, pagesize, PROT_READ | PROT_WRITE) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, "HOOK", "mprotect failed");
        return;
    }
    __android_log_print(ANDROID_LOG_INFO, "HOOK", "got_slot page now writable");
}


