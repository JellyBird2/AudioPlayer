// Headless test: extraction + WIC decode of embedded cover art.
// (NOT shipped - build only for verification.)
#include "win_player.h"
#include "win_art.h"

#include <windows.h>
#include <objbase.h>
#include <stdio.h>

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc < 2) {
        printf("usage: test_art file.ape\n");
        return 1;
    }
    AudioPlayer p;
    std::wstring err;
    if (!p.Open(argv[1], err)) {
        printf("open failed\n");
        return 1;
    }
    std::vector<unsigned char> img = p.CoverImage();
    printf("cover bytes: %u\n", (unsigned)img.size());
    int rc = 0;
    if (!img.empty()) {
        printf("magic: %02X %02X %02X %02X\n", img[0], img[1], img[2], img[3]);
        HBITMAP b = DecodeCoverArt(img.data(), img.size(), 512);
        if (b) {
            BITMAP bm = {};
            GetObjectW(b, sizeof(bm), &bm);
            printf("bitmap: %dx%d\n", (int)bm.bmWidth, (int)bm.bmHeight);
            DeleteObject(b);
        } else {
            printf("decode FAILED\n");
            rc = 2;
        }
    }
    CoUninitialize();
    return rc;
}
