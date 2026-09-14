// Cover-art decoding via WIC. See win_art.h.
#include "win_art.h"

#include <wincodec.h>

HBITMAP DecodeCoverArt(const unsigned char* data, size_t len, int maxDim) {
    if (!data || len < 16 || len > 64u * 1024u * 1024u) return nullptr;

    HBITMAP hbmp = nullptr;
    IWICImagingFactory* factory = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* conv = nullptr;

    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return nullptr;

    UINT w = 0, h = 0, dw = 0, dh = 0;
    IWICBitmapSource* src = nullptr;
    bool haveSrc = false;

    if (SUCCEEDED(factory->CreateStream(&stream)) &&
        SUCCEEDED(stream->InitializeFromMemory((BYTE*)data, (DWORD)len)) && // WIC never writes
        SUCCEEDED(factory->CreateDecoderFromStream(stream, nullptr,
                                                   WICDecodeMetadataCacheOnDemand, &decoder))) {
        UINT frames = 0;
        if (SUCCEEDED(decoder->GetFrameCount(&frames)) && frames > 0 &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) &&
            SUCCEEDED(frame->GetSize(&w, &h)) &&
            w > 0 && h > 0 && w <= 20000 && h <= 20000) {
            dw = w;
            dh = h;
            src = frame;
            haveSrc = true;
            if (maxDim > 0 && (dw > (UINT)maxDim || dh > (UINT)maxDim)) {
                UINT sw = dw, sh = dh;
                if (sw >= sh) {
                    sh = (UINT)(sh * (double)maxDim / sw);
                    sw = (UINT)maxDim;
                } else {
                    sw = (UINT)(sw * (double)maxDim / sh);
                    sh = (UINT)maxDim;
                }
                if (sw < 1) sw = 1;
                if (sh < 1) sh = 1;
                if (SUCCEEDED(factory->CreateBitmapScaler(&scaler)) &&
                    SUCCEEDED(scaler->Initialize(frame, sw, sh,
                                                 WICBitmapInterpolationModeFant))) {
                    src = scaler;
                    dw = sw;
                    dh = sh;
                }
            }
        }
    }

    if (haveSrc && SUCCEEDED(factory->CreateFormatConverter(&conv)) &&
        SUCCEEDED(conv->Initialize(src, GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom))) {
        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
        bi.bmiHeader.biWidth = (LONG)dw;
        bi.bmiHeader.biHeight = -(LONG)dh; // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        hbmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (hbmp && bits) {
            if (FAILED(conv->CopyPixels(nullptr, dw * 4, dw * dh * 4, (BYTE*)bits))) {
                DeleteObject(hbmp);
                hbmp = nullptr;
            }
        }
    }

    if (conv) conv->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    if (stream) stream->Release();
    if (factory) factory->Release();
    return hbmp;
}
