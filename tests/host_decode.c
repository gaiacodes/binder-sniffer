// Host harness: decode a captured Parcel fixture off-device.
//   build/host_decode tests/fixtures/enqueue.bin
#include "bs_decoder.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <capture.bin>\n", argv[0]);
        return 2;
    }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror("fopen"); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fprintf(stderr, "empty file\n"); fclose(f); return 1; }

    uint8_t *buf = malloc((size_t)n);
    if (!buf) { fclose(f); return 1; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        perror("fread"); free(buf); fclose(f); return 1;
    }
    fclose(f);

    bs_notif r;
    if (bs_decode_enqueue(buf, (size_t)n, &r) != 0) {
        printf("decode failed\n");
        free(buf);
        return 1;
    }

    printf("interface : %s\n", r.interface);
    printf("package   : %s\n", r.pkg);
    printf("title     : %s\n", r.have_title ? r.title : "(none)");
    printf("text      : %s\n", r.have_text  ? r.text  : "(none)");

    free(buf);
    return 0;
}
