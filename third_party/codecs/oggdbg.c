// Exact OpusPcmSource pattern: one audio packet per fill, full counters.
#include <ogg/ogg.h>

#include <stdio.h>
#include <string.h>

static FILE* f;
static ogg_sync_state oy;
static ogg_stream_state os;
static int serial = -1;
static int osInit = 0;

static int readMore() {
    char* buf = ogg_sync_buffer(&oy, 4096);
    if (!buf) return 0;
    size_t n = fread(buf, 1, 4096, f);
    if (n == 0) return 0;
    ogg_sync_wrote(&oy, (long)n);
    return 1;
}

#define ST (long)os.lacing_fill, (long)os.lacing_packet, (long)os.lacing_returned

int main(int argc, char** argv) {
    f = fopen(argv[1], "rb");
    ogg_sync_init(&oy);
    ogg_page og;
    ogg_packet op;
    long audio = 0;
    for (;;) {
        int r = ogg_sync_pageout(&oy, &og);
        if (r == 0) {
            if (!readMore()) break;
            continue;
        }
        if (r < 0) continue;
        if (!osInit) {
            ogg_stream_init(&os, ogg_page_serialno(&og));
            osInit = 1;
            serial = ogg_page_serialno(&og);
        }
        if (ogg_page_serialno(&og) != serial) continue;
        ogg_stream_pagein(&os, &og);
        printf("IN seq=%ld fill=%ld pkt=%ld ret=%ld\n", (long)ogg_page_pageno(&og), ST);
        int porc = 0;
        while ((porc = ogg_stream_packetout(&os, &op)) != 0) {
            if (porc < 0) {
                printf("  HOLE\n");
                continue;
            }
            printf("  got pkt=%ld bytes=%ld ret->%ld\n", (long)op.packetno, (long)op.bytes,
                   (long)os.lacing_returned);
            if (op.bytes >= 8 && (!memcmp(op.packet, "OpusHead", 8) ||
                                  !memcmp(op.packet, "OpusTags", 8)))
                continue;
            audio++;
            goto nextpage;
        }
        printf("  drained: fill=%ld pkt=%ld ret=%ld\n", ST);
    nextpage:;
        if (audio > 12) break;
    }
    printf("audio=%ld final: fill=%ld pkt=%ld ret=%ld\n", audio, ST);
    return 0;
}
