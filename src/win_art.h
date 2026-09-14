// Cover-art decoding (WIC - inbox on Windows, no extra DLLs to ship).
#pragma once

#include <windows.h>

// Decodes image bytes (JPEG/PNG/GIF/BMP/TIFF) into a 32bpp top-down HBITMAP.
// Longest side is capped at maxDim (0 = native size). Returns null on any
// failure. Caller must DeleteObject() the result.
HBITMAP DecodeCoverArt(const unsigned char* data, size_t len, int maxDim);
