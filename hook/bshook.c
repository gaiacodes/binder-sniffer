#include <android/log.h>
#include <unistd.h>

__attribute__((constructor))
static void bshook_init(void) {
    __android_log_print(ANDROID_LOG_INFO, "HOOK", "loaded");
}
