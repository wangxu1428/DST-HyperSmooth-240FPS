#pragma once

// =============================================================================
// HyperShade Visual Closed-Loop Engine (look_matcher.h)
//
// Zero-external-component, 4-layer architecture inside dinput8.dll:
//   [1] Generic Input Layer:
//       - Win32 Clipboard CF_DIBV5 / CF_DIB (strict BMP header/stride/sign
//         normalization) & CF_HDROP image file paths -> Windows OS WIC decode,
//         RGBA32 conversion, and pre-analysis downscaling on a background thread.
//       - Non-blocking OpenGL 3.2+ live frame sampler (glBlitFramebuffer to
//         160x90 RGBA8 FBO + GL_PIXEL_PACK_BUFFER + glFenceSync non-blocking
//         poll -> zero RenderThread stall on Native GL; honest sync fallback on
//         legacy ANGLE D3D9 GLES 2.0).
//   [2] Dual-Mode Visual Feature Analysis Layer (Oklab, Björn Ottosson 2020):
//       - Mode 1 (General Look Matching): scene-agnostic statistical transfer
//         (17-point Oklab L quantile CDF, 3-zone shadow/mid/highlight chroma,
//         9-point chroma CDF, highlight roll-off, 4-corner symmetry-gated
//         vignette probe). Never assumes pixel-to-pixel correspondence.
//       - Mode 2 (Same-Scene Exact Matching): ZNCC + gradient correlation
//         verification, trimmed Robust Huber pixel-pair Oklab error + quantile
//         hybrid, spatial radial vignette & local micro-contrast estimation.
//       - Explicit per-feature & overall confidence scores on every result.
//   [3] Core Asset — Capability Whitelist & Regularized Parameter Optimizer:
//       - Queries ONLY parameters stably owned & controlled by HyperShade's
//         active rendering pipeline (HS_VISUAL_FINAL). Future pipeline modules
//         register new whitelist entries without rewriting visual analysis.
//       - Bounded multi-scale deterministic coordinate-pattern optimization
//         with L2 Tikhonov damping and zero-mean ASC CDL residual isolation.
//   [4] Standard HS1- Share Code Integration (handled in Lua HS_SHARE):
//       - HS1- guarantees deterministic parameter-state reproduction (final
//         rendered pixels vary with GPU driver, resolution, scene & backend).
//       - Extracts statistical visual features strictly from user-supplied
//         images/frames to generate HyperShade's own parameter presets; does
//         not read, parse, or copy third-party shaders, assets, or code.
// =============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace hs_look_match {

// -----------------------------------------------------------------------------
// Explicit COM GUIDs (zero -luuid / -lwindowscodecs link dependency under Zig)
// -----------------------------------------------------------------------------
inline constexpr GUID kClsidWicImagingFactory = {
    0xcacaf262u, 0x9370u, 0x4615u, { 0xa1u, 0x3bu, 0x9fu, 0x55u, 0x39u, 0xdau, 0x4cu, 0x0au }
};
inline constexpr GUID kIidWicImagingFactory = {
    0xec5ec8a9u, 0xc395u, 0x4314u, { 0x9cu, 0x77u, 0x54u, 0xd7u, 0xa9u, 0x35u, 0xffu, 0x70u }
};
inline constexpr GUID kGuidWicPixelFormat32bppRGBA = {
    0xf5c7ad2du, 0x6a8du, 0x43ddu, { 0xa7u, 0xa8u, 0xa2u, 0x99u, 0x35u, 0x26u, 0x1au, 0xe9u }
};

// Analysis dimensions (160x90 = 14,400 px = 57.6 KB RGBA8; fast decode & solve)
inline constexpr int32_t  kAnalysisW          = 160;
inline constexpr int32_t  kAnalysisH          = 90;
inline constexpr size_t   kMaxClipboardBytes  = 64u * 1024u * 1024u;
inline constexpr int32_t  kMaxImageDim        = 16384;
inline constexpr uint64_t kMaxDecodedRawBytes = 256ull * 1024ull * 1024ull;

// GL constants (self-contained, zero <GL/gl.h> SDK drift)
inline constexpr uint32_t kGlTexture2D              = 0x0DE1u;
inline constexpr uint32_t kGlTextureBinding2D       = 0x8069u;
inline constexpr uint32_t kGlRgba                   = 0x1908u;
inline constexpr uint32_t kGlRgba8                  = 0x8058u;
inline constexpr uint32_t kGlUnsignedByte           = 0x1401u;
inline constexpr uint32_t kGlLinear                 = 0x2601u;
inline constexpr uint32_t kGlClampToEdge            = 0x812Fu;
inline constexpr uint32_t kGlTextureMinFilter       = 0x2801u;
inline constexpr uint32_t kGlTextureMagFilter       = 0x2800u;
inline constexpr uint32_t kGlTextureWrapS           = 0x2802u;
inline constexpr uint32_t kGlTextureWrapT           = 0x2803u;
inline constexpr uint32_t kGlFramebuffer            = 0x8D40u;
inline constexpr uint32_t kGlReadFramebuffer        = 0x8CA8u;
inline constexpr uint32_t kGlDrawFramebuffer        = 0x8CA9u;
inline constexpr uint32_t kGlDrawFramebufferBinding = 0x8CA6u;
inline constexpr uint32_t kGlReadFramebufferBinding = 0x8CAAu;
inline constexpr uint32_t kGlColorAttachment0       = 0x8CE0u;
inline constexpr uint32_t kGlFramebufferComplete    = 0x8CD5u;
inline constexpr uint32_t kGlColorBufferBit         = 0x00004000u;
inline constexpr uint32_t kGlScissorTest            = 0x0C11u;
inline constexpr uint32_t kGlPackAlignment          = 0x0D05u;
inline constexpr uint32_t kGlPixelPackBuffer        = 0x88EBu;
inline constexpr uint32_t kGlPixelPackBufferBinding = 0x88EDu;
inline constexpr uint32_t kGlStreamRead             = 0x88E1u;
inline constexpr uint32_t kGlMapReadBit             = 0x0001u;
inline constexpr uint32_t kGlSyncGpuCommandsComplete = 0x9117u;
inline constexpr uint32_t kGlAlreadySignaled        = 0x911Au;
inline constexpr uint32_t kGlConditionSatisfied     = 0x911Cu;

// -----------------------------------------------------------------------------
// Normalized Top-Down Linear/sRGB Image Container
// -----------------------------------------------------------------------------
struct ImageRgb {
    int32_t            width        = 0;
    int32_t            height       = 0;
    int32_t            origWidth    = 0;
    int32_t            origHeight   = 0;
    std::vector<float> rgb; // width * height * 3, sRGB [0, 1], row 0 = top

    bool Valid() const noexcept {
        return width > 0 && height > 0 &&
               rgb.size() == static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    }
};

// -----------------------------------------------------------------------------
// Color Math: Exact IEC 61966-2-1 sRGB <-> Linear & Oklab (Björn Ottosson 2020)
// -----------------------------------------------------------------------------
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline float ClampF(float v, float lo, float hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float SmoothstepF(float e0, float e1, float x) noexcept {
    if (e0 == e1) return x < e0 ? 0.0f : 1.0f;
    const float t = ClampF((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline float SrgbToLinearScalar(float c) noexcept {
    c = ClampF(c, 0.0f, 1.0f);
    if (c < 0.04045f) return c / 12.92f;
    return std::pow((c + 0.055f) / 1.055f, 2.4f);
}

inline float SrgbToLinearFast(float c) noexcept {
    static const std::array<float, 257> kLut = []() {
        std::array<float, 257> t{};
        for (int i = 0; i <= 256; ++i) {
            t[static_cast<size_t>(i)] = SrgbToLinearScalar(static_cast<float>(i) / 256.0f);
        }
        return t;
    }();
    c = ClampF(c, 0.0f, 1.0f) * 256.0f;
    const int i = static_cast<int>(c);
    if (i >= 256) return 1.0f;
    const float f = c - static_cast<float>(i);
    return kLut[static_cast<size_t>(i)] + (kLut[static_cast<size_t>(i + 1)] - kLut[static_cast<size_t>(i)]) * f;
}

inline float LinearToSrgbScalar(float c) noexcept {
    c = ClampF(c, 0.0f, 1.0f);
    if (c < 0.0031308f) return c * 12.92f;
    return 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

inline float FastCbrtf(float x) noexcept {
    if (x <= 1e-12f) return 0.0f;
    uint32_t u = 0u;
    std::memcpy(&u, &x, sizeof(u));
    u = u / 3u + 0x2a517d47u;
    float y = 0.0f;
    std::memcpy(&y, &u, sizeof(y));
    y = (2.0f * y + x / (y * y)) * (1.0f / 3.0f);
    y = (2.0f * y + x / (y * y)) * (1.0f / 3.0f);
    return y;
}

inline Vec3 SrgbToOklab(float r, float g, float b) noexcept {
    const float lr = SrgbToLinearFast(r);
    const float lg = SrgbToLinearFast(g);
    const float lb = SrgbToLinearFast(b);

    const float l = 0.4122214708f * lr + 0.5363325363f * lg + 0.0514459929f * lb;
    const float m = 0.2119034982f * lr + 0.6806995451f * lg + 0.1073969566f * lb;
    const float s = 0.0883024619f * lr + 0.2817188376f * lg + 0.6299787005f * lb;

    const float l_ = FastCbrtf(l);
    const float m_ = FastCbrtf(m);
    const float s_ = FastCbrtf(s);

    return {
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_
    };
}

// -----------------------------------------------------------------------------
// Layer 1A: Strict CF_DIB / CF_DIBV5 -> Validated BMP Stream Normalization
// -----------------------------------------------------------------------------
inline uint16_t ReadLe16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8u);
}

inline uint32_t ReadLe32(const uint8_t* p) noexcept {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8u) |
           (static_cast<uint32_t>(p[2]) << 16u) |
           (static_cast<uint32_t>(p[3]) << 24u);
}

inline int32_t ReadLeS32(const uint8_t* p) noexcept {
    return static_cast<int32_t>(ReadLe32(p));
}

inline void WriteLe16(uint8_t* p, uint16_t v) noexcept {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8u) & 0xFFu);
}

inline void WriteLe32(uint8_t* p, uint32_t v) noexcept {
    p[0] = static_cast<uint8_t>(v & 0xFFu);
    p[1] = static_cast<uint8_t>((v >> 8u) & 0xFFu);
    p[2] = static_cast<uint8_t>((v >> 16u) & 0xFFu);
    p[3] = static_cast<uint8_t>((v >> 24u) & 0xFFu);
}

struct DibHeaderInfo {
    uint32_t biSize          = 0u;
    int32_t  biWidth         = 0;
    int32_t  biHeight        = 0; // >0 bottom-up, <0 top-down
    uint16_t biPlanes        = 0u;
    uint16_t biBitCount      = 0u;
    uint32_t biCompression   = 0u;
    uint32_t biClrUsed       = 0u;
    uint32_t bfOffBits       = 0u;
    uint64_t rowStride       = 0ull;
    bool     isTopDown       = false;
};

// Converts raw Windows CF_DIB / CF_DIBV5 memory payload (header + optional
// bitfields/palette + pixel bits, WITHOUT 14-byte BITMAPFILEHEADER) into a
// strictly validated in-memory BMP file suitable for WIC CreateDecoderFromStream.
inline bool BuildValidatedBmpFromDib(const uint8_t* dib, size_t dibSize,
                                     std::vector<uint8_t>& outBmp,
                                     std::string& outErr,
                                     DibHeaderInfo* outInfo = nullptr) noexcept {
    outBmp.clear();
    outErr.clear();

    if (!dib || dibSize < 40u) {
        outErr = "dib_too_small";
        return false;
    }
    if (dibSize > kMaxClipboardBytes) {
        outErr = "dib_exceeds_size_limit";
        return false;
    }

    const uint32_t biSize        = ReadLe32(dib + 0);
    const int32_t  biWidth       = ReadLeS32(dib + 4);
    const int32_t  biHeight      = ReadLeS32(dib + 8);
    const uint16_t biPlanes      = ReadLe16(dib + 12);
    const uint16_t biBitCount    = ReadLe16(dib + 14);
    const uint32_t biCompression = ReadLe32(dib + 16);
    const uint32_t biClrUsed     = ReadLe32(dib + 32);

    if (biSize < 40u || biSize > dibSize || biSize > 4096u) {
        outErr = "dib_invalid_header_size";
        return false;
    }
    const int64_t absHeight64 = (biHeight < 0) ? -static_cast<int64_t>(biHeight) : static_cast<int64_t>(biHeight);
    if (biWidth <= 0 || biWidth > kMaxImageDim || absHeight64 <= 0 || absHeight64 > kMaxImageDim) {
        outErr = "dib_invalid_dimensions";
        return false;
    }
    if (biPlanes != 1u) {
        outErr = "dib_invalid_planes";
        return false;
    }
    if (biBitCount != 1u && biBitCount != 4u && biBitCount != 8u &&
        biBitCount != 16u && biBitCount != 24u && biBitCount != 32u) {
        outErr = "dib_unsupported_bitcount";
        return false;
    }
    // BI_RGB(0), BI_RLE8(1), BI_RLE4(2), BI_BITFIELDS(3)
    if (biCompression > 3u) {
        outErr = "dib_unsupported_compression";
        return false;
    }
    // Decoded RGBA32 memory safety guard
    if (static_cast<uint64_t>(biWidth) * static_cast<uint64_t>(absHeight64) * 4ull > kMaxDecodedRawBytes) {
        outErr = "dib_decoded_pixels_too_large";
        return false;
    }

    // BI_BITFIELDS appends 3 DWORD channel masks (12 bytes) ONLY for 40-byte BITMAPINFOHEADER.
    // BITMAPV2(52)/V3(56)/V4(108)/V5(124) already include the masks inside biSize!
    const uint32_t bitfieldsExtra = (biSize == 40u && biCompression == 3u) ? 12u : 0u;

    uint32_t paletteEntries = 0u;
    if (biBitCount <= 8u) {
        const uint32_t maxPal = 1u << biBitCount;
        paletteEntries = (biClrUsed > 0u && biClrUsed <= maxPal) ? biClrUsed : maxPal;
    } else if (biClrUsed > 0u) {
        if (biClrUsed > 256u) {
            outErr = "dib_invalid_palette_count";
            return false;
        }
        paletteEntries = biClrUsed;
    }
    const uint64_t paletteBytes   = static_cast<uint64_t>(paletteEntries) * 4ull;
    const uint64_t headerAndTable = static_cast<uint64_t>(biSize) + bitfieldsExtra + paletteBytes;
    if (headerAndTable >= dibSize) {
        outErr = "dib_header_exceeds_buffer";
        return false;
    }

    const uint64_t rowStride = ((static_cast<uint64_t>(biWidth) * biBitCount + 31ull) / 32ull) * 4ull;
    const uint64_t availPixels = dibSize - headerAndTable;
    size_t tailPadBytes = 0u;
    if (biCompression == 0u || biCompression == 3u) {
        const uint64_t needPixels = rowStride * static_cast<uint64_t>(absHeight64);
        if (availPixels < needPixels) {
            // Some clipboard producers omit up to 3 trailing alignment bytes on the final row.
            if (needPixels - availPixels <= 3ull) {
                tailPadBytes = static_cast<size_t>(needPixels - availPixels);
            } else {
                outErr = "dib_truncated_pixels";
                return false;
            }
        }
    }

    const uint64_t bfOffBits64 = 14ull + headerAndTable;
    const uint64_t bfSize64    = 14ull + dibSize + tailPadBytes;
    if (bfOffBits64 > 0xFFFFFFFFull || bfSize64 > 0xFFFFFFFFull) {
        outErr = "dib_size_overflow";
        return false;
    }

    outBmp.resize(static_cast<size_t>(bfSize64), 0u);
    outBmp[0] = 'B';
    outBmp[1] = 'M';
    WriteLe32(outBmp.data() + 2,  static_cast<uint32_t>(bfSize64));
    WriteLe16(outBmp.data() + 6,  0u);
    WriteLe16(outBmp.data() + 8,  0u);
    WriteLe32(outBmp.data() + 10, static_cast<uint32_t>(bfOffBits64));
    std::memcpy(outBmp.data() + 14, dib, dibSize);

    if (outInfo) {
        outInfo->biSize        = biSize;
        outInfo->biWidth       = biWidth;
        outInfo->biHeight      = biHeight;
        outInfo->biPlanes      = biPlanes;
        outInfo->biBitCount    = biBitCount;
        outInfo->biCompression = biCompression;
        outInfo->biClrUsed     = biClrUsed;
        outInfo->bfOffBits     = static_cast<uint32_t>(bfOffBits64);
        outInfo->rowStride     = rowStride;
        outInfo->isTopDown     = (biHeight < 0);
    }
    return true;
}

// -----------------------------------------------------------------------------
// Layer 1B: Windows OS WIC Decoder & Pre-Analysis Downscaler (Background Thread)
// -----------------------------------------------------------------------------
inline bool IsSupportedImageExtension(const std::wstring& path) noexcept {
    const size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1u >= path.size()) return false;
    std::wstring ext = path.substr(dot);
    for (wchar_t& c : ext) c = static_cast<wchar_t>(towlower(c));
    return ext == L".png"  || ext == L".jpg"  || ext == L".jpeg" ||
           ext == L".bmp"  || ext == L".dib"  || ext == L".webp" ||
           ext == L".tif"  || ext == L".tiff" || ext == L".gif";
}

inline void ComputeTargetAnalysisSize(int32_t srcW, int32_t srcH, bool forceExactGrid,
                                      int32_t& outW, int32_t& outH) noexcept {
    if (forceExactGrid || srcW <= 0 || srcH <= 0) {
        outW = kAnalysisW;
        outH = kAnalysisH;
        return;
    }
    // Preserve aspect ratio inside kAnalysisW x kAnalysisH bounding box (min 32x18)
    const double sx = static_cast<double>(kAnalysisW) / static_cast<double>(srcW);
    const double sy = static_cast<double>(kAnalysisH) / static_cast<double>(srcH);
    const double s  = std::min(1.0, std::min(sx, sy));
    outW = std::clamp(static_cast<int32_t>(std::lround(srcW * s)), 32, kAnalysisW);
    outH = std::clamp(static_cast<int32_t>(std::lround(srcH * s)), 18, kAnalysisH);
}

inline bool DecodeWicFrameToImageRgb(IWICImagingFactory* fac,
                                     IWICBitmapFrameDecode* frame,
                                     bool forceExactGrid,
                                     ImageRgb& outImg,
                                     std::string& outErr) noexcept {
    UINT srcW = 0, srcH = 0;
    if (FAILED(frame->GetSize(&srcW, &srcH)) || srcW == 0u || srcH == 0u ||
        srcW > static_cast<UINT>(kMaxImageDim) || srcH > static_cast<UINT>(kMaxImageDim)) {
        outErr = "wic_invalid_dimensions";
        return false;
    }

    int32_t dstW = kAnalysisW, dstH = kAnalysisH;
    ComputeTargetAnalysisSize(static_cast<int32_t>(srcW), static_cast<int32_t>(srcH),
                              forceExactGrid, dstW, dstH);

    IWICBitmapSource* source = frame;
    IWICBitmapScaler* scaler = nullptr;
    if (static_cast<int32_t>(srcW) != dstW || static_cast<int32_t>(srcH) != dstH) {
        if (SUCCEEDED(fac->CreateBitmapScaler(&scaler)) && scaler) {
            if (SUCCEEDED(scaler->Initialize(frame, static_cast<UINT>(dstW), static_cast<UINT>(dstH),
                                             WICBitmapInterpolationModeFant))) {
                source = scaler;
            } else {
                scaler->Release();
                scaler = nullptr;
                dstW = static_cast<int32_t>(srcW);
                dstH = static_cast<int32_t>(srcH);
            }
        }
    }

    IWICFormatConverter* conv = nullptr;
    HRESULT hr = fac->CreateFormatConverter(&conv);
    if (FAILED(hr) || !conv) {
        if (scaler) scaler->Release();
        outErr = "wic_converter_create_failed";
        return false;
    }

    hr = conv->Initialize(source, kGuidWicPixelFormat32bppRGBA,
                          WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        conv->Release();
        if (scaler) scaler->Release();
        outErr = "wic_format_convert_failed";
        return false;
    }

    const UINT stride = static_cast<UINT>(dstW) * 4u;
    const UINT bufBytes = stride * static_cast<UINT>(dstH);
    std::vector<uint8_t> rgba(bufBytes);
    hr = conv->CopyPixels(nullptr, stride, bufBytes, rgba.data());

    conv->Release();
    if (scaler) scaler->Release();
    if (FAILED(hr)) {
        outErr = "wic_copy_pixels_failed";
        return false;
    }

    outImg.width      = dstW;
    outImg.height     = dstH;
    outImg.origWidth  = static_cast<int32_t>(srcW);
    outImg.origHeight = static_cast<int32_t>(srcH);
    outImg.rgb.resize(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 3u);
    constexpr float kInv255 = 1.0f / 255.0f;
    const size_t nPix = static_cast<size_t>(dstW) * static_cast<size_t>(dstH);
    for (size_t i = 0; i < nPix; ++i) {
        outImg.rgb[i * 3u + 0u] = rgba[i * 4u + 0u] * kInv255;
        outImg.rgb[i * 3u + 1u] = rgba[i * 4u + 1u] * kInv255;
        outImg.rgb[i * 3u + 2u] = rgba[i * 4u + 2u] * kInv255;
    }
    return true;
}

inline bool DecodeImageBytesWithWic(const uint8_t* bytes, size_t len, bool forceExactGrid,
                                    ImageRgb& outImg, std::string& outErr) noexcept {
    if (!bytes || len < 16u || len > kMaxClipboardBytes + 64u) {
        outErr = "wic_invalid_stream_bytes";
        return false;
    }
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!hMem) {
        outErr = "wic_oom_hglobal";
        return false;
    }
    void* dst = GlobalLock(hMem);
    if (!dst) {
        GlobalFree(hMem);
        outErr = "wic_lock_failed";
        return false;
    }
    std::memcpy(dst, bytes, len);
    GlobalUnlock(hMem);

    IStream* stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &stream)) || !stream) {
        GlobalFree(hMem);
        outErr = "wic_create_stream_failed";
        return false;
    }

    IWICImagingFactory* fac = nullptr;
    HRESULT hr = CoCreateInstance(kClsidWicImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  kIidWicImagingFactory, reinterpret_cast<void**>(&fac));
    if (FAILED(hr) || !fac) {
        stream->Release();
        outErr = "wic_factory_failed";
        return false;
    }

    IWICBitmapDecoder* dec = nullptr;
    hr = fac->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr) || !dec) {
        fac->Release();
        stream->Release();
        outErr = "wic_decode_stream_failed";
        return false;
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = dec->GetFrame(0, &frame);
    bool ok = false;
    if (SUCCEEDED(hr) && frame) {
        ok = DecodeWicFrameToImageRgb(fac, frame, forceExactGrid, outImg, outErr);
        frame->Release();
    } else {
        outErr = "wic_get_frame_failed";
    }

    dec->Release();
    fac->Release();
    stream->Release();
    return ok;
}

inline bool DecodeImageFileWithWic(const std::wstring& path, bool forceExactGrid,
                                   ImageRgb& outImg, std::string& outErr) noexcept {
    if (path.empty()) {
        outErr = "wic_empty_path";
        return false;
    }
    IWICImagingFactory* fac = nullptr;
    HRESULT hr = CoCreateInstance(kClsidWicImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  kIidWicImagingFactory, reinterpret_cast<void**>(&fac));
    if (FAILED(hr) || !fac) {
        outErr = "wic_factory_failed";
        return false;
    }
    IWICBitmapDecoder* dec = nullptr;
    hr = fac->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                        WICDecodeMetadataCacheOnDemand, &dec);
    if (FAILED(hr) || !dec) {
        fac->Release();
        outErr = "wic_decode_file_failed";
        return false;
    }
    IWICBitmapFrameDecode* frame = nullptr;
    hr = dec->GetFrame(0, &frame);
    bool ok = false;
    if (SUCCEEDED(hr) && frame) {
        ok = DecodeWicFrameToImageRgb(fac, frame, forceExactGrid, outImg, outErr);
        frame->Release();
    } else {
        outErr = "wic_get_frame_failed";
    }
    dec->Release();
    fac->Release();
    return ok;
}

// -----------------------------------------------------------------------------
// Layer 1C: Fast Non-Blocking Clipboard Payload Acquisition (< 0.5 ms)
// -----------------------------------------------------------------------------
enum class ClipboardKind : uint8_t {
    None     = 0,
    Dib      = 1,
    FilePath = 2,
};

struct ClipboardPayload {
    ClipboardKind        kind = ClipboardKind::None;
    std::vector<uint8_t> rawDib;
    std::wstring         filePath;
    std::string          sourceTag;
};

// Copies raw clipboard payload immediately and closes clipboard; all decoding
// and downscaling runs strictly on the background worker thread.
inline bool GrabClipboardImagePayload(ClipboardPayload& out, std::string& outErr) noexcept {
    out = ClipboardPayload{};
    outErr.clear();

    if (!OpenClipboard(nullptr)) {
        outErr = "clipboard_open_failed";
        return false;
    }

    auto copyDib = [&](UINT fmt, const char* tag) -> bool {
        if (!IsClipboardFormatAvailable(fmt)) return false;
        HANDLE h = GetClipboardData(fmt);
        if (!h) return false;
        const size_t sz = GlobalSize(h);
        if (sz < 40u || sz > kMaxClipboardBytes) return false;
        const void* p = GlobalLock(h);
        if (!p) return false;
        out.kind = ClipboardKind::Dib;
        out.rawDib.resize(sz);
        std::memcpy(out.rawDib.data(), p, sz);
        out.sourceTag = tag;
        GlobalUnlock(h);
        return true;
    };

    bool ok = copyDib(CF_DIBV5, "clipboard_dibv5");
    if (!ok) ok = copyDib(CF_DIB, "clipboard_dib");
    if (!ok && IsClipboardFormatAvailable(CF_HDROP)) {
        HANDLE h = GetClipboardData(CF_HDROP);
        if (h) {
            HDROP hDrop = static_cast<HDROP>(h);
            const UINT count = DragQueryFileW(hDrop, 0xFFFFFFFFu, nullptr, 0u);
            for (UINT i = 0; i < count && i < 16u; ++i) {
                const UINT len = DragQueryFileW(hDrop, i, nullptr, 0u);
                if (len == 0u || len > 32760u) continue;
                std::wstring path(len, L'\0');
                DragQueryFileW(hDrop, i, path.data(), len + 1u);
                if (IsSupportedImageExtension(path)) {
                    out.kind      = ClipboardKind::FilePath;
                    out.filePath  = std::move(path);
                    out.sourceTag = "clipboard_file";
                    ok = true;
                    break;
                }
            }
            if (!ok) {
                outErr = "clipboard_hdrop_not_image";
            }
        }
    }

    if (!ok && outErr.empty()) {
        if (IsClipboardFormatAvailable(CF_UNICODETEXT) || IsClipboardFormatAvailable(CF_TEXT)) {
            outErr = "clipboard_has_text_not_image";
        } else {
            outErr = "clipboard_no_image";
        }
    }

    CloseClipboard();
    return ok;
}

// -----------------------------------------------------------------------------
// Helper: Resample ImageRgb (for Same-Scene comparison when aspect differs)
// -----------------------------------------------------------------------------
inline ImageRgb ResampleImageBilinear(const ImageRgb& src, int32_t dstW, int32_t dstH) noexcept {
    ImageRgb out;
    if (!src.Valid() || dstW <= 0 || dstH <= 0) return out;
    if (src.width == dstW && src.height == dstH) return src;
    out.width      = dstW;
    out.height     = dstH;
    out.origWidth  = src.origWidth;
    out.origHeight = src.origHeight;
    out.rgb.resize(static_cast<size_t>(dstW) * static_cast<size_t>(dstH) * 3u);

    const float sx = static_cast<float>(src.width)  / static_cast<float>(dstW);
    const float sy = static_cast<float>(src.height) / static_cast<float>(dstH);
    for (int32_t y = 0; y < dstH; ++y) {
        const float fy = ClampF((y + 0.5f) * sy - 0.5f, 0.0f, static_cast<float>(src.height - 1));
        const int32_t y0 = static_cast<int32_t>(fy);
        const int32_t y1 = std::min(y0 + 1, src.height - 1);
        const float ty = fy - static_cast<float>(y0);
        for (int32_t x = 0; x < dstW; ++x) {
            const float fx = ClampF((x + 0.5f) * sx - 0.5f, 0.0f, static_cast<float>(src.width - 1));
            const int32_t x0 = static_cast<int32_t>(fx);
            const int32_t x1 = std::min(x0 + 1, src.width - 1);
            const float tx = fx - static_cast<float>(x0);
            const size_t i00 = (static_cast<size_t>(y0) * src.width + x0) * 3u;
            const size_t i10 = (static_cast<size_t>(y0) * src.width + x1) * 3u;
            const size_t i01 = (static_cast<size_t>(y1) * src.width + x0) * 3u;
            const size_t i11 = (static_cast<size_t>(y1) * src.width + x1) * 3u;
            const size_t o   = (static_cast<size_t>(y) * dstW + x) * 3u;
            for (int c = 0; c < 3; ++c) {
                const float a = src.rgb[i00 + c] + (src.rgb[i10 + c] - src.rgb[i00 + c]) * tx;
                const float b = src.rgb[i01 + c] + (src.rgb[i11 + c] - src.rgb[i01 + c]) * tx;
                out.rgb[o + c] = a + (b - a) * ty;
            }
        }
    }
    return out;
}

// Neutral DST-balanced daylight synthetic source fallback (used ONLY when user
// runs clipboard Look Matching outside gameplay, e.g. on Main Menu).
inline ImageRgb BuildNeutralDstReferenceImage() noexcept {
    ImageRgb img;
    img.width      = kAnalysisW;
    img.height     = kAnalysisH;
    img.origWidth  = kAnalysisW;
    img.origHeight = kAnalysisH;
    img.rgb.resize(static_cast<size_t>(kAnalysisW) * static_cast<size_t>(kAnalysisH) * 3u);
    for (int32_t y = 0; y < kAnalysisH; ++y) {
        const float v = (y + 0.5f) / static_cast<float>(kAnalysisH);
        for (int32_t x = 0; x < kAnalysisW; ++x) {
            const float u = (x + 0.5f) / static_cast<float>(kAnalysisW);
            // Smooth tonal & natural muted-chroma ramp spanning DST foliage/stone/earth/warmth
            const float luma = 0.08f + 0.80f * (0.65f * v + 0.35f * (0.5f + 0.5f * std::sin(u * 6.2831853f)));
            const float h    = u * 6.2831853f;
            const float chr  = 0.14f * std::sin(v * 3.1415926f);
            const float r    = ClampF(luma + chr * std::cos(h), 0.02f, 0.96f);
            const float g    = ClampF(luma + chr * std::cos(h - 2.0943951f), 0.02f, 0.96f);
            const float b    = ClampF(luma + chr * std::cos(h + 2.0943951f), 0.02f, 0.96f);
            const size_t idx = (static_cast<size_t>(y) * kAnalysisW + x) * 3u;
            img.rgb[idx + 0u] = r;
            img.rgb[idx + 1u] = g;
            img.rgb[idx + 2u] = b;
        }
    }
    return img;
}

// -----------------------------------------------------------------------------
// Layer 2: Dual-Mode Visual Feature Analysis (Oklab, CDF, 3-Zone, Symmetry)
// -----------------------------------------------------------------------------
inline constexpr int kLumaQuantiles   = 17;
inline constexpr int kChromaQuantiles = 9;

inline constexpr float kLumaQPoints[kLumaQuantiles] = {
    0.02f, 0.05f, 0.10f, 0.16f, 0.22f, 0.29f, 0.36f, 0.43f, 0.50f,
    0.57f, 0.64f, 0.71f, 0.78f, 0.84f, 0.90f, 0.95f, 0.98f
};
inline constexpr float kChromaQPoints[kChromaQuantiles] = {
    0.10f, 0.20f, 0.30f, 0.40f, 0.50f, 0.60f, 0.70f, 0.80f, 0.90f
};

struct VisualFeatures {
    float lumaQ[kLumaQuantiles]{};
    float chromaQ[kChromaQuantiles]{};
    // 3 luminance bands: 0=shadow [0.08,0.36], 1=mid [0.36,0.68], 2=highlight [0.68,0.96]
    float zoneA[3]{};
    float zoneB[3]{};
    float zoneC[3]{};
    float zoneFrac[3]{};
    float meanL            = 0.5f;
    float stdL             = 0.15f;
    float meanC            = 0.08f;
    float hlDesatRatio     = 1.0f; // zoneC[2] / max(zoneC[1], 0.02)
    float radialCornerDrop = 0.0f; // 1 - cornerLuma / centerLuma
    float cornerSymmetry   = 0.0f; // [0..1] 1 = all 4 corners darken symmetrically
    float microContrast    = 0.0f; // mean local Laplacian normalized by stdL
    float validFraction    = 0.0f; // non-black/non-clipped sample ratio
};

inline float PickQuantileSorted(const std::vector<float>& sorted, float q) noexcept {
    if (sorted.empty()) return 0.0f;
    if (sorted.size() == 1u) return sorted[0];
    const float pos = ClampF(q, 0.0f, 1.0f) * static_cast<float>(sorted.size() - 1u);
    const size_t i0 = static_cast<size_t>(pos);
    const size_t i1 = std::min(i0 + 1u, sorted.size() - 1u);
    const float t   = pos - static_cast<float>(i0);
    return sorted[i0] + (sorted[i1] - sorted[i0]) * t;
}

inline VisualFeatures ExtractVisualFeatures(const ImageRgb& img) noexcept {
    VisualFeatures f{};
    if (!img.Valid()) return f;

    const int32_t W = img.width;
    const int32_t H = img.height;
    const size_t N  = static_cast<size_t>(W) * static_cast<size_t>(H);

    std::vector<float> ls;
    std::vector<float> cs;
    ls.reserve(N);
    cs.reserve(N);
    std::vector<float> lumaGrid(N, 0.0f);

    double sumL = 0.0, sumL2 = 0.0, sumC = 0.0;
    double zA[3]{}, zB[3]{}, zC[3]{};
    size_t zN[3]{};

    double centerSum = 0.0;
    size_t centerN   = 0u;
    double cornerSum[4]{};
    size_t cornerN[4]{};

    for (int32_t y = 0; y < H; ++y) {
        const float v  = (y + 0.5f) / static_cast<float>(H);
        const float dy = v - 0.5f;
        for (int32_t x = 0; x < W; ++x) {
            const float u  = (x + 0.5f) / static_cast<float>(W);
            const float dx = u - 0.5f;
            const size_t i = static_cast<size_t>(y) * static_cast<size_t>(W) + static_cast<size_t>(x);
            const float r  = img.rgb[i * 3u + 0u];
            const float g  = img.rgb[i * 3u + 1u];
            const float b  = img.rgb[i * 3u + 2u];
            const Vec3 lab = SrgbToOklab(r, g, b);
            const float c  = std::sqrt(lab.y * lab.y + lab.z * lab.z);
            lumaGrid[i] = lab.x;

            // Exclude pure pitch-black letterbox / fog-of-war pixels from tonal stats
            if (lab.x >= 0.035f) {
                ls.push_back(lab.x);
                cs.push_back(c);
                sumL  += lab.x;
                sumL2 += static_cast<double>(lab.x) * lab.x;
                sumC  += c;

                int band = -1;
                if (lab.x >= 0.08f && lab.x < 0.36f)      band = 0;
                else if (lab.x >= 0.36f && lab.x < 0.68f) band = 1;
                else if (lab.x >= 0.68f && lab.x <= 0.98f) band = 2;
                if (band >= 0) {
                    zA[band] += lab.y;
                    zB[band] += lab.z;
                    zC[band] += c;
                    ++zN[band];
                }
            }

            const float r2 = (dx * dx + dy * dy) * 2.4f;
            if (r2 < 0.25f && lab.x >= 0.035f) {
                centerSum += lab.x;
                ++centerN;
            } else if (r2 > 0.55f && lab.x >= 0.02f) {
                const int qIdx = (dy >= 0.0f ? 2 : 0) + (dx >= 0.0f ? 1 : 0);
                cornerSum[qIdx] += lab.x;
                ++cornerN[qIdx];
            }
        }
    }

    if (ls.empty()) {
        // Completely dark image fallback
        for (int k = 0; k < kLumaQuantiles; ++k) f.lumaQ[k] = 0.02f;
        return f;
    }

    f.validFraction = static_cast<float>(ls.size()) / static_cast<float>(N);
    f.meanL = static_cast<float>(sumL / static_cast<double>(ls.size()));
    const double varL = std::max(0.0, sumL2 / static_cast<double>(ls.size()) - static_cast<double>(f.meanL) * f.meanL);
    f.stdL  = static_cast<float>(std::sqrt(varL));
    f.meanC = static_cast<float>(sumC / static_cast<double>(cs.size()));

    std::sort(ls.begin(), ls.end());
    std::sort(cs.begin(), cs.end());
    for (int k = 0; k < kLumaQuantiles; ++k) {
        f.lumaQ[k] = PickQuantileSorted(ls, kLumaQPoints[k]);
    }
    for (int k = 0; k < kChromaQuantiles; ++k) {
        f.chromaQ[k] = PickQuantileSorted(cs, kChromaQPoints[k]);
    }

    for (int b = 0; b < 3; ++b) {
        f.zoneFrac[b] = static_cast<float>(zN[b]) / static_cast<float>(ls.size());
        if (zN[b] > 0u) {
            const double inv = 1.0 / static_cast<double>(zN[b]);
            f.zoneA[b] = static_cast<float>(zA[b] * inv);
            f.zoneB[b] = static_cast<float>(zB[b] * inv);
            f.zoneC[b] = static_cast<float>(zC[b] * inv);
        }
    }
    f.hlDesatRatio = (zN[2] >= 16u && zN[1] >= 16u)
        ? ClampF(f.zoneC[2] / std::max(f.zoneC[1], 0.025f), 0.0f, 2.5f)
        : 1.0f;

    // 4-corner symmetry-aware radial vignette probe
    if (centerN >= 32u && cornerN[0] >= 8u && cornerN[1] >= 8u && cornerN[2] >= 8u && cornerN[3] >= 8u) {
        const float cMean = static_cast<float>(centerSum / static_cast<double>(centerN));
        float qm[4]{};
        float qAvg = 0.0f;
        for (int q = 0; q < 4; ++q) {
            qm[q] = static_cast<float>(cornerSum[q] / static_cast<double>(cornerN[q]));
            qAvg += qm[q] * 0.25f;
        }
        float qVar = 0.0f;
        for (int q = 0; q < 4; ++q) {
            const float d = qm[q] - qAvg;
            qVar += d * d * 0.25f;
        }
        const float qStd = std::sqrt(qVar);
        if (cMean > 0.08f) {
            f.radialCornerDrop = ClampF(1.0f - (qAvg / cMean), -0.5f, 0.75f);
            // High symmetry ONLY when all 4 corners have closely matching brightness
            f.cornerSymmetry = ClampF(1.0f - (qStd / std::max(cMean * 0.22f, 0.03f)), 0.0f, 1.0f);
        }
    }

    // Center-region local micro-contrast (Laplacian energy)
    if (W >= 8 && H >= 8) {
        const int32_t x0 = W / 4, x1 = (W * 3) / 4;
        const int32_t y0 = H / 4, y1 = (H * 3) / 4;
        double lapSum = 0.0;
        size_t lapN   = 0u;
        for (int32_t y = y0; y < y1; ++y) {
            for (int32_t x = x0; x < x1; ++x) {
                const float c = lumaGrid[static_cast<size_t>(y) * W + x];
                if (c < 0.04f) continue;
                const float n4 = 0.25f * (
                    lumaGrid[static_cast<size_t>(y - 1) * W + x] +
                    lumaGrid[static_cast<size_t>(y + 1) * W + x] +
                    lumaGrid[static_cast<size_t>(y) * W + (x - 1)] +
                    lumaGrid[static_cast<size_t>(y) * W + (x + 1)]);
                lapSum += std::fabs(c - n4);
                ++lapN;
            }
        }
        if (lapN > 0u) {
            f.microContrast = static_cast<float>(lapSum / static_cast<double>(lapN));
        }
    }
    return f;
}

struct SceneSimilarity {
    float zncc     = 0.0f; // [-1, 1] luminance structural correlation
    float gradCorr = 0.0f; // [-1, 1] edge/gradient correlation
    bool  sameScene = false;
};

inline SceneSimilarity ComputeSceneSimilarity(const ImageRgb& a, const ImageRgb& b) noexcept {
    SceneSimilarity out{};
    if (!a.Valid() || !b.Valid()) return out;

    constexpr int32_t kGW = 32;
    constexpr int32_t kGH = 18;
    const ImageRgb ga = ResampleImageBilinear(a, kGW, kGH);
    const ImageRgb gb = ResampleImageBilinear(b, kGW, kGH);
    constexpr size_t M = static_cast<size_t>(kGW) * static_cast<size_t>(kGH);
    float la[M]{}, lb[M]{};
    double ma = 0.0, mb = 0.0;
    for (size_t i = 0; i < M; ++i) {
        la[i] = 0.2126f * ga.rgb[i * 3u] + 0.7152f * ga.rgb[i * 3u + 1u] + 0.0722f * ga.rgb[i * 3u + 2u];
        lb[i] = 0.2126f * gb.rgb[i * 3u] + 0.7152f * gb.rgb[i * 3u + 1u] + 0.0722f * gb.rgb[i * 3u + 2u];
        ma += la[i];
        mb += lb[i];
    }
    ma /= static_cast<double>(M);
    mb /= static_cast<double>(M);
    double num = 0.0, da = 0.0, db = 0.0;
    for (size_t i = 0; i < M; ++i) {
        const double xa = la[i] - ma;
        const double xb = lb[i] - mb;
        num += xa * xb;
        da  += xa * xa;
        db  += xb * xb;
    }
    const double denom = std::sqrt(da * db);
    out.zncc = (denom > 1e-8) ? static_cast<float>(num / denom) : 0.0f;

    // Gradient magnitude correlation (robust against monotonic tone/LUT shifts!)
    std::vector<float> ea, eb;
    ea.reserve(M);
    eb.reserve(M);
    double mea = 0.0, meb = 0.0;
    for (int32_t y = 1; y < kGH - 1; ++y) {
        for (int32_t x = 1; x < kGW - 1; ++x) {
            const auto gmag = [&](const float* g) -> float {
                const float gx = g[(y) * kGW + (x + 1)] - g[(y) * kGW + (x - 1)];
                const float gy = g[(y + 1) * kGW + (x)] - g[(y - 1) * kGW + (x)];
                return std::sqrt(gx * gx + gy * gy);
            };
            const float va = gmag(la);
            const float vb = gmag(lb);
            ea.push_back(va);
            eb.push_back(vb);
            mea += va;
            meb += vb;
        }
    }
    if (!ea.empty()) {
        mea /= static_cast<double>(ea.size());
        meb /= static_cast<double>(eb.size());
        double gn = 0.0, gda = 0.0, gdb = 0.0;
        for (size_t i = 0; i < ea.size(); ++i) {
            const double xa = ea[i] - mea;
            const double xb = eb[i] - meb;
            gn  += xa * xb;
            gda += xa * xa;
            gdb += xb * xb;
        }
        const double gd = std::sqrt(gda * gdb);
        out.gradCorr = (gd > 1e-8) ? static_cast<float>(gn / gd) : 0.0f;
    }
    out.sameScene = (out.zncc >= 0.80f && out.gradCorr >= 0.65f);
    return out;
}

// -----------------------------------------------------------------------------
// Layer 3: Core Asset — Capability Whitelist & Owned Forward Model
// -----------------------------------------------------------------------------
enum ParamIndex : int {
    kP_Exposure = 0,
    kP_Temp,
    kP_Tint,
    kP_Contrast,
    kP_Lightness,
    kP_Gamma,
    kP_Sat,
    kP_Vibrance,
    kP_Filmic,
    kP_HlDesat,
    kP_SlopeR,
    kP_SlopeG,
    kP_SlopeB,
    kP_OffsetR,
    kP_OffsetG,
    kP_OffsetB,
    kP_PowerR,
    kP_PowerG,
    kP_PowerB,
    kP_Vignette,
    kP_Sharp,
    kParamCount
};

struct CapabilitySpec {
    const char* key;
    float       minVal;
    float       maxVal;
    float       defVal;
    float       regWeight;
    float       initStep;
    bool        observableGeneral;
    bool        observableSameScene;
    uint8_t     stage; // 1=global tone/color, 2=zero-mean CDL split-tone, 3=spatial/detail
};

// Single source of truth for parameters stably owned & controlled by HyperShade's
// active post-process grading pipeline (exact bounds matching modmain.lua TUNE_BOUNDS).
// Future owned pipeline capabilities extend this table without rewriting Layer 1/2.
inline constexpr CapabilitySpec kCapabilityWhitelist[kParamCount] = {
    { "exposure",  -0.80f, 0.80f, 0.00f, 0.0008f, 0.16f, true,  true,  1 },
    { "temp",      -1.00f, 1.00f, 0.00f, 0.0005f, 0.20f, true,  true,  1 },
    { "tint",      -1.00f, 1.00f, 0.00f, 0.0008f, 0.20f, true,  true,  1 },
    { "contrast",  -0.40f, 0.60f, 0.00f, 0.0012f, 0.12f, true,  true,  1 },
    { "lightness", -0.30f, 0.30f, 0.00f, 0.0025f, 0.06f, true,  true,  1 },
    { "gamma",      0.60f, 1.60f, 1.00f, 0.0012f, 0.12f, true,  true,  1 },
    { "sat",        0.30f, 1.80f, 1.00f, 0.0008f, 0.16f, true,  true,  1 },
    { "vibrance",  -0.60f, 0.80f, 0.00f, 0.0014f, 0.14f, true,  true,  1 },
    { "filmic",     0.00f, 1.00f, 0.00f, 0.0008f, 0.20f, true,  true,  1 },
    { "hl_desat",   0.00f, 1.00f, 0.00f, 0.0012f, 0.20f, true,  true,  1 },
    { "slope_r",    0.50f, 1.60f, 1.00f, 0.0060f, 0.08f, true,  true,  2 },
    { "slope_g",    0.50f, 1.60f, 1.00f, 0.0060f, 0.08f, true,  true,  2 },
    { "slope_b",    0.50f, 1.60f, 1.00f, 0.0060f, 0.08f, true,  true,  2 },
    { "offset_r",  -0.25f, 0.25f, 0.00f, 0.0150f, 0.03f, true,  true,  2 },
    { "offset_g",  -0.25f, 0.25f, 0.00f, 0.0150f, 0.03f, true,  true,  2 },
    { "offset_b",  -0.25f, 0.25f, 0.00f, 0.0150f, 0.03f, true,  true,  2 },
    { "power_r",    0.50f, 1.60f, 1.00f, 0.0070f, 0.08f, true,  true,  2 },
    { "power_g",    0.50f, 1.60f, 1.00f, 0.0070f, 0.08f, true,  true,  2 },
    { "power_b",    0.50f, 1.60f, 1.00f, 0.0070f, 0.08f, true,  true,  2 },
    { "vignette",   0.00f, 0.50f, 0.00f, 0.0015f, 0.08f, false, true,  3 },
    { "sharp",      0.00f, 1.60f, 0.00f, 0.0020f, 0.15f, false, true,  3 },
};

struct ParamVector {
    float v[kParamCount]{};

    static ParamVector Identity() noexcept {
        ParamVector p{};
        for (int i = 0; i < kParamCount; ++i) p.v[i] = kCapabilityWhitelist[i].defVal;
        return p;
    }

    void ClampAll() noexcept {
        for (int i = 0; i < kParamCount; ++i) {
            v[i] = ClampF(v[i], kCapabilityWhitelist[i].minVal, kCapabilityWhitelist[i].maxVal);
        }
    }
};

// Precomputed per-candidate constants (hoists exp2/branches out of pixel loop!)
struct PreparedGrade {
    float mulR, mulG, mulB;
    float slopeR, slopeG, slopeB;
    float offR, offG, offB;
    float powR, powG, powB;
    float lightO, conP1, invGamma;
    float filmic, sat, vib, hlDesat, vig;
    bool  doCdlAffine;
    bool  doCdlPow;
    bool  doGamma;
    bool  doFilmic;
    bool  doVibOrHl;
    bool  doVig;

    static PreparedGrade From(const ParamVector& p, bool applyVignette) noexcept {
        PreparedGrade g{};
        const float expScale = std::exp2(p.v[kP_Exposure]);
        const float wbr = std::exp2(0.20f * p.v[kP_Temp]);
        const float wbg = std::exp2(0.12f * p.v[kP_Tint]);
        const float wbb = std::exp2(-0.20f * p.v[kP_Temp]);
        const float wbn = (3.0f / (wbr + wbg + wbb + 1e-6f)) * expScale;
        g.mulR = wbr * wbn;
        g.mulG = wbg * wbn;
        g.mulB = wbb * wbn;
        g.slopeR = p.v[kP_SlopeR]; g.slopeG = p.v[kP_SlopeG]; g.slopeB = p.v[kP_SlopeB];
        g.offR   = p.v[kP_OffsetR]; g.offG  = p.v[kP_OffsetG]; g.offB  = p.v[kP_OffsetB];
        g.powR   = std::max(p.v[kP_PowerR], 0.10f);
        g.powG   = std::max(p.v[kP_PowerG], 0.10f);
        g.powB   = std::max(p.v[kP_PowerB], 0.10f);
        g.lightO   = p.v[kP_Lightness];
        g.conP1    = 1.0f + p.v[kP_Contrast];
        g.invGamma = 1.0f / std::max(p.v[kP_Gamma], 0.10f);
        g.filmic   = ClampF(p.v[kP_Filmic], 0.0f, 1.0f);
        g.sat      = std::max(p.v[kP_Sat], 0.0f);
        g.vib      = p.v[kP_Vibrance];
        g.hlDesat  = ClampF(p.v[kP_HlDesat], 0.0f, 1.0f);
        g.vig      = applyVignette ? ClampF(p.v[kP_Vignette], 0.0f, 0.50f) : 0.0f;
        g.doCdlAffine = (std::fabs(g.slopeR - 1.0f) + std::fabs(g.slopeG - 1.0f) + std::fabs(g.slopeB - 1.0f) +
                         std::fabs(g.offR) + std::fabs(g.offG) + std::fabs(g.offB)) > 1e-4f;
        g.doCdlPow    = (std::fabs(g.powR - 1.0f) + std::fabs(g.powG - 1.0f) + std::fabs(g.powB - 1.0f)) > 1e-4f;
        g.doGamma     = std::fabs(g.invGamma - 1.0f) > 1e-4f;
        g.doFilmic    = g.filmic > 0.001f;
        g.doVibOrHl   = (std::fabs(g.vib) > 1e-4f) || (g.hlDesat > 1e-4f);
        g.doVig       = g.vig > 0.001f;
        return g;
    }
};

// Exact 1:1 C++ forward model matching tools/build_shaders.py GradePixel() + vignette
inline Vec3 EvaluateGradePixelPrepared(Vec3 color, float u, float v, const PreparedGrade& g) noexcept {
    color.x *= g.mulR;
    color.y *= g.mulG;
    color.z *= g.mulB;

    if (g.doCdlAffine) {
        color.x = ClampF(color.x * g.slopeR + g.offR, 0.0f, 1.0f);
        color.y = ClampF(color.y * g.slopeG + g.offG, 0.0f, 1.0f);
        color.z = ClampF(color.z * g.slopeB + g.offB, 0.0f, 1.0f);
    } else {
        color.x = ClampF(color.x, 0.0f, 1.0f);
        color.y = ClampF(color.y, 0.0f, 1.0f);
        color.z = ClampF(color.z, 0.0f, 1.0f);
    }
    if (g.doCdlPow) {
        color.x = std::pow(std::max(color.x, 1e-6f), g.powR);
        color.y = std::pow(std::max(color.y, 1e-6f), g.powG);
        color.z = std::pow(std::max(color.z, 1e-6f), g.powB);
    }

    color.x = std::max((color.x + g.lightO - 0.5f) * g.conP1 + 0.5f, 0.0f);
    color.y = std::max((color.y + g.lightO - 0.5f) * g.conP1 + 0.5f, 0.0f);
    color.z = std::max((color.z + g.lightO - 0.5f) * g.conP1 + 0.5f, 0.0f);
    if (g.doGamma) {
        color.x = std::pow(color.x, g.invGamma);
        color.y = std::pow(color.y, g.invGamma);
        color.z = std::pow(color.z, g.invGamma);
    }

    if (g.doFilmic) {
        auto acesCh = [](float c) -> float {
            const float lin = SrgbToLinearFast(c);
            const float ac  = (lin * (2.51f * lin + 0.03f)) / (lin * (2.43f * lin + 0.59f) + 0.14f);
            return LinearToSrgbScalar(ac);
        };
        color.x += (acesCh(color.x) - color.x) * g.filmic;
        color.y += (acesCh(color.y) - color.y) * g.filmic;
        color.z += (acesCh(color.z) - color.z) * g.filmic;
    }

    const float yt = 0.25f * color.x + 0.50f * color.y + 0.25f * color.z;
    float sf = g.sat;
    if (g.doVibOrHl) {
        const float ct    = color.y - 0.50f * (color.x + color.z);
        const float co    = color.x - color.z;
        const float vibM  = ClampF(1.0f - std::sqrt(co * co + ct * ct) / 0.6f, 0.0f, 1.0f);
        const float wl    = std::pow(ClampF(4.0f * yt * (1.0f - yt), 0.0f, 1.0f), 1.1f);
        const float luma  = 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
        const float hlM   = SmoothstepF(0.6f, 1.0f, luma);
        sf = (g.sat + g.vib * vibM * wl) * (1.0f - hlM * g.hlDesat);
    }
    color.x = yt + (color.x - yt) * sf;
    color.y = yt + (color.y - yt) * sf;
    color.z = yt + (color.z - yt) * sf;

    if (g.doVig) {
        const float dx = u - 0.5f;
        const float dy = v - 0.5f;
        const float vig = 1.0f - g.vig * SmoothstepF(0.35f, 0.95f, (dx * dx + dy * dy) * 2.4f);
        color.x *= vig;
        color.y *= vig;
        color.z *= vig;
    }

    color.x = ClampF(color.x, 0.0f, 1.0f);
    color.y = ClampF(color.y, 0.0f, 1.0f);
    color.z = ClampF(color.z, 0.0f, 1.0f);
    return color;
}

inline Vec3 EvaluateGradePixel(Vec3 color, float u, float v, const ParamVector& p, bool applyVignette) noexcept {
    return EvaluateGradePixelPrepared(color, u, v, PreparedGrade::From(p, applyVignette));
}

// -----------------------------------------------------------------------------
// Fitting Modes & Structured Output (With Honest Per-Feature Confidence)
// -----------------------------------------------------------------------------
enum class FitModeRequest : uint8_t {
    Auto      = 0, // Auto-select General vs Same-Scene via ZNCC + gradCorr
    General   = 1, // Force Mode 1: statistical Look Matching (cross-game / arbitrary ref)
    SameScene = 2, // Force Mode 2: spatial pixel-pair exact matching (fallback if misaligned)
    SameFrame = 3, // In-game post-ColourCube vs pre-UI same-frame capture
};

struct LookMatchResult {
    bool        ok                = false;
    std::string errorCode;
    std::string warningCode;
    std::string usedMode;          // "general" | "same_scene" | "same_frame"
    std::string inputSource;       // "clipboard_dibv5" | "clipboard_dib" | "clipboard_file" | "file" | "same_frame_gl"
    std::string readbackPath;      // "async_pbo" | "sync_fallback" | "neutral_ref"
    int32_t     refWidth          = 0;
    int32_t     refHeight         = 0;
    float       elapsedMs         = 0.0f;
    float       zncc              = 0.0f;
    float       gradCorr          = 0.0f;
    float       deltaEBefore      = 0.0f; // Oklab distance * 100
    float       deltaEAfter       = 0.0f;
    float       overallConfidence = 0.0f; // [0..1]
    float       colorConfidence   = 0.0f; // [0..1]
    float       spatialConfidence = 0.0f; // [0..1]
    bool        sharpFitted       = false;
    bool        vignetteFitted    = false;
    ParamVector params            = ParamVector::Identity();
};

// Regularization penalty (keeps CDL zero-mean so it never fights exposure/gamma/lightness)
inline float ComputeRegularizationLoss(const ParamVector& p, bool generalMode) noexcept {
    float reg = 0.0f;
    const float modeMul = generalMode ? 1.4f : 1.0f;
    for (int i = 0; i < kParamCount; ++i) {
        const float d = p.v[i] - kCapabilityWhitelist[i].defVal;
        reg += kCapabilityWhitelist[i].regWeight * modeMul * d * d;
    }
    // Strong zero-mean penalty on CDL triplets so CDL captures ONLY chroma split-tone
    const float sMean = (p.v[kP_SlopeR]  + p.v[kP_SlopeG]  + p.v[kP_SlopeB])  / 3.0f - 1.0f;
    const float oMean = (p.v[kP_OffsetR] + p.v[kP_OffsetG] + p.v[kP_OffsetB]) / 3.0f;
    const float pMean = (p.v[kP_PowerR]  + p.v[kP_PowerG]  + p.v[kP_PowerB])  / 3.0f - 1.0f;
    reg += 0.08f * (sMean * sMean + pMean * pMean) + 0.20f * (oMean * oMean);
    return reg;
}

struct SamplePixel {
    Vec3  rgb;
    float u = 0.5f;
    float v = 0.5f;
};

struct SamplePair {
    Vec3  srcRgb;
    Vec3  refLab;
    float u = 0.5f;
    float v = 0.5f;
};

// Deterministic stratified pixel sampling (~768..1024 samples -> ~0.015 ms per objective eval!)
inline std::vector<SamplePixel> BuildStratifiedSamples(const ImageRgb& img, size_t maxSamples) noexcept {
    std::vector<SamplePixel> out;
    if (!img.Valid()) return out;
    const int32_t W = img.width;
    const int32_t H = img.height;
    const size_t N  = static_cast<size_t>(W) * static_cast<size_t>(H);
    const size_t stride = std::max<size_t>(1u, N / std::max<size_t>(maxSamples, 64u));
    out.reserve(std::min(N, maxSamples + 64u));
    for (size_t i = 0; i < N; i += stride) {
        const float r = img.rgb[i * 3u + 0u];
        const float g = img.rgb[i * 3u + 1u];
        const float b = img.rgb[i * 3u + 2u];
        const float y = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        if (y < 0.015f) continue;
        const int32_t py = static_cast<int32_t>(i / static_cast<size_t>(W));
        const int32_t px = static_cast<int32_t>(i % static_cast<size_t>(W));
        out.push_back({ { r, g, b }, (px + 0.5f) / W, (py + 0.5f) / H });
    }
    return out;
}

inline std::vector<SamplePair> BuildAlignedPairs(const ImageRgb& src, const ImageRgb& ref, size_t maxPairs) noexcept {
    std::vector<SamplePair> out;
    if (!src.Valid() || !ref.Valid()) return out;
    const ImageRgb refAligned = (ref.width == src.width && ref.height == src.height)
        ? ref
        : ResampleImageBilinear(ref, src.width, src.height);
    const int32_t W = src.width;
    const int32_t H = src.height;
    const size_t N  = static_cast<size_t>(W) * static_cast<size_t>(H);
    const size_t stride = std::max<size_t>(1u, N / std::max<size_t>(maxPairs, 64u));
    out.reserve(std::min(N, maxPairs + 64u));
    for (size_t i = 0; i < N; i += stride) {
        const float sr = src.rgb[i * 3u + 0u];
        const float sg = src.rgb[i * 3u + 1u];
        const float sb = src.rgb[i * 3u + 2u];
        const float sy = 0.2126f * sr + 0.7152f * sg + 0.0722f * sb;
        if (sy < 0.02f || (sr > 0.985f && sg > 0.985f && sb > 0.985f)) continue;
        const float rr = refAligned.rgb[i * 3u + 0u];
        const float rg = refAligned.rgb[i * 3u + 1u];
        const float rb = refAligned.rgb[i * 3u + 2u];
        const int32_t py = static_cast<int32_t>(i / static_cast<size_t>(W));
        const int32_t px = static_cast<int32_t>(i % static_cast<size_t>(W));
        out.push_back({ { sr, sg, sb }, SrgbToOklab(rr, rg, rb), (px + 0.5f) / W, (py + 0.5f) / H });
    }
    return out;
}

// O(N + B) linear-interpolated histogram quantile extractor (replaces O(N log N) sort in hot loop!)
inline void ExtractHistogramQuantiles(const uint16_t* hist, int bins, float rangeMax,
                                      uint32_t total, const float* qPts, int qCount,
                                      float* outQ) noexcept {
    if (total == 0u) {
        for (int k = 0; k < qCount; ++k) outQ[k] = 0.0f;
        return;
    }
    const float binW = rangeMax / static_cast<float>(bins);
    int b = 0;
    uint32_t cum = hist[0];
    for (int k = 0; k < qCount; ++k) {
        const float target = ClampF(qPts[k], 0.0f, 1.0f) * static_cast<float>(total);
        while (b + 1 < bins && static_cast<float>(cum) < target) {
            ++b;
            cum += hist[b];
        }
        const uint32_t prev = cum - hist[b];
        const float frac = (hist[b] > 0u) ? ClampF((target - static_cast<float>(prev)) / static_cast<float>(hist[b]), 0.0f, 1.0f) : 0.5f;
        outQ[k] = (static_cast<float>(b) + frac) * binW;
    }
}

// Mode 1 Loss: Scene-agnostic Oklab statistical distribution matching (O(N), zero sorts!)
inline float EvaluateGeneralLoss(const std::vector<SamplePixel>& srcSamples,
                                 const VisualFeatures& refF,
                                 const ParamVector& p,
                                 float* outRawDeltaE = nullptr) noexcept {
    const size_t n = srcSamples.size();
    if (n == 0u) return 1.0f;
    const PreparedGrade pg = PreparedGrade::From(p, false);

    constexpr int kBins = 64;
    uint16_t histL[kBins]{};
    uint16_t histC[kBins]{};
    float zA[3]{}, zB[3]{}, zC[3]{};
    uint32_t zN[3]{};

    for (size_t i = 0; i < n; ++i) {
        const Vec3 graded = EvaluateGradePixelPrepared(srcSamples[i].rgb, srcSamples[i].u, srcSamples[i].v, pg);
        const Vec3 lab    = SrgbToOklab(graded.x, graded.y, graded.z);
        const float c     = std::sqrt(lab.y * lab.y + lab.z * lab.z);
        const int bL = std::clamp(static_cast<int>(lab.x * (kBins - 1)), 0, kBins - 1);
        const int bC = std::clamp(static_cast<int>((c / 0.40f) * (kBins - 1)), 0, kBins - 1);
        ++histL[bL];
        ++histC[bC];
        int band = -1;
        if (lab.x >= 0.08f && lab.x < 0.36f)       band = 0;
        else if (lab.x >= 0.36f && lab.x < 0.68f)  band = 1;
        else if (lab.x >= 0.68f && lab.x <= 0.98f) band = 2;
        if (band >= 0) {
            zA[band] += lab.y;
            zB[band] += lab.z;
            zC[band] += c;
            ++zN[band];
        }
    }

    float outLQ[kLumaQuantiles]{};
    float outCQ[kChromaQuantiles]{};
    ExtractHistogramQuantiles(histL, kBins, 1.0f,  static_cast<uint32_t>(n), kLumaQPoints,   kLumaQuantiles,   outLQ);
    ExtractHistogramQuantiles(histC, kBins, 0.40f, static_cast<uint32_t>(n), kChromaQPoints, kChromaQuantiles, outCQ);

    float lumaErr = 0.0f;
    for (int k = 0; k < kLumaQuantiles; ++k) {
        const float d = outLQ[k] - refF.lumaQ[k];
        lumaErr += d * d;
    }
    lumaErr /= static_cast<float>(kLumaQuantiles);

    float chromaErr = 0.0f;
    for (int k = 0; k < kChromaQuantiles; ++k) {
        const float d = outCQ[k] - refF.chromaQ[k];
        chromaErr += d * d;
    }
    chromaErr /= static_cast<float>(kChromaQuantiles);

    float zoneErr = 0.0f;
    float zoneW   = 0.0f;
    for (int b = 0; b < 3; ++b) {
        if (zN[b] < 4u || refF.zoneFrac[b] < 0.02f) continue;
        const float inv = 1.0f / static_cast<float>(zN[b]);
        const float da  = zA[b] * inv - refF.zoneA[b];
        const float db  = zB[b] * inv - refF.zoneB[b];
        const float w   = (b == 1) ? 1.5f : 1.0f;
        zoneErr += w * (da * da + db * db);
        zoneW   += w;
    }
    if (zoneW > 0.0f) zoneErr /= zoneW;

    float hlErr = 0.0f;
    if (zN[2] >= 6u && zN[1] >= 6u && refF.zoneFrac[2] >= 0.03f) {
        const float cMid = zC[1] / static_cast<float>(zN[1]);
        const float cHl  = zC[2] / static_cast<float>(zN[2]);
        const float rat  = ClampF(cHl / std::max(cMid, 0.025f), 0.0f, 2.5f);
        const float dr   = rat - refF.hlDesatRatio;
        hlErr = 0.015f * dr * dr;
    }

    const float rawDistSq = 1.15f * lumaErr + 2.20f * zoneErr + 1.40f * chromaErr + hlErr;
    if (outRawDeltaE) {
        *outRawDeltaE = std::sqrt(std::max(rawDistSq, 0.0f)) * 100.0f;
    }
    return rawDistSq + ComputeRegularizationLoss(p, true);
}

// Mode 2 Loss: Robust trimmed Huber spatial pixel-pair Oklab + O(N) quantile hybrid
inline float EvaluateSameSceneLoss(const std::vector<SamplePair>& pairs,
                                   const VisualFeatures& refF,
                                   const ParamVector& p,
                                   bool withVignette,
                                   float* outRawDeltaE = nullptr) noexcept {
    const size_t n = pairs.size();
    if (n == 0u) return 1.0f;
    const PreparedGrade pg = PreparedGrade::From(p, withVignette);

    constexpr int kBins = 64;
    uint16_t histL[kBins]{};
    constexpr float kHuberDelta = 0.09f;
    float huberSum   = 0.0f;
    float rawDistSum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        const Vec3 graded = EvaluateGradePixelPrepared(pairs[i].srcRgb, pairs[i].u, pairs[i].v, pg);
        const Vec3 lab    = SrgbToOklab(graded.x, graded.y, graded.z);
        const int bL = std::clamp(static_cast<int>(lab.x * (kBins - 1)), 0, kBins - 1);
        ++histL[bL];
        const float dL = lab.x - pairs[i].refLab.x;
        const float da = lab.y - pairs[i].refLab.y;
        const float db = lab.z - pairs[i].refLab.z;
        const float dist = std::sqrt(dL * dL + 1.5f * (da * da + db * db));
        rawDistSum += dist;
        if (dist <= kHuberDelta) {
            huberSum += 0.5f * dist * dist;
        } else {
            huberSum += kHuberDelta * (dist - 0.5f * kHuberDelta);
        }
    }

    float outLQ[kLumaQuantiles]{};
    ExtractHistogramQuantiles(histL, kBins, 1.0f, static_cast<uint32_t>(n), kLumaQPoints, kLumaQuantiles, outLQ);
    float qErr = 0.0f;
    for (int k = 0; k < kLumaQuantiles; ++k) {
        const float d = outLQ[k] - refF.lumaQ[k];
        qErr += d * d;
    }
    qErr /= static_cast<float>(kLumaQuantiles);

    const float meanHuber = huberSum / static_cast<float>(n);
    if (outRawDeltaE) {
        *outRawDeltaE = (rawDistSum / static_cast<float>(n)) * 100.0f;
    }
    return 1.60f * meanHuber + 0.35f * qErr + ComputeRegularizationLoss(p, false);
}

// Deterministic multi-scale bounded coordinate-pattern optimizer
template <typename EvalFn>
inline float OptimizeParamSubset(ParamVector& best, const int* indices, int count,
                                 int maxRounds, EvalFn&& evalFn) noexcept {
    float bestLoss = evalFn(best);
    float steps[kParamCount]{};
    for (int i = 0; i < count; ++i) {
        steps[indices[i]] = kCapabilityWhitelist[indices[i]].initStep;
    }

    for (int round = 0; round < maxRounds; ++round) {
        bool anyImproved = false;
        ParamVector startOfRound = best;

        for (int k = 0; k < count; ++k) {
            const int idx = indices[k];
            const auto& spec = kCapabilityWhitelist[idx];
            float step = steps[idx];
            if (step < 0.002f) continue;

            const float orig = best.v[idx];
            float candVal = orig;
            float candLoss = bestLoss;

            const float plusV = ClampF(orig + step, spec.minVal, spec.maxVal);
            if (std::fabs(plusV - orig) > 1e-5f) {
                best.v[idx] = plusV;
                const float l = evalFn(best);
                if (l + 1e-7f < candLoss) {
                    candLoss = l;
                    candVal  = plusV;
                }
            }

            const float minusV = ClampF(orig - step, spec.minVal, spec.maxVal);
            if (std::fabs(minusV - orig) > 1e-5f) {
                best.v[idx] = minusV;
                const float l = evalFn(best);
                if (l + 1e-7f < candLoss) {
                    candLoss = l;
                    candVal  = minusV;
                }
            }

            if (candVal != orig) {
                best.v[idx] = candVal;
                bestLoss    = candLoss;
                anyImproved = true;
            } else {
                best.v[idx] = orig;
                steps[idx] *= 0.50f;
            }
        }

        // Hooke-Jeeves pattern extrapolation step along round improvement vector
        if (anyImproved) {
            ParamVector extrap = best;
            for (int k = 0; k < count; ++k) {
                const int idx = indices[k];
                const auto& spec = kCapabilityWhitelist[idx];
                extrap.v[idx] = ClampF(best.v[idx] + 0.65f * (best.v[idx] - startOfRound.v[idx]),
                                       spec.minVal, spec.maxVal);
            }
            const float el = evalFn(extrap);
            if (el + 1e-7f < bestLoss) {
                best     = extrap;
                bestLoss = el;
            }
        } else {
            bool anyActive = false;
            for (int k = 0; k < count; ++k) {
                if (steps[indices[k]] >= 0.003f) { anyActive = true; break; }
            }
            if (!anyActive) break;
        }
    }
    return bestLoss;
}

// Re-center CDL triplets strictly around neutral mean so CDL never duplicates
// global exposure / lightness / gamma.
inline void EnforceZeroMeanCdl(ParamVector& p) noexcept {
    auto recenter = [&](int r, int g, int b, float target) {
        const float m = (p.v[r] + p.v[g] + p.v[b]) / 3.0f;
        const float d = m - target;
        p.v[r] = ClampF(p.v[r] - d, kCapabilityWhitelist[r].minVal, kCapabilityWhitelist[r].maxVal);
        p.v[g] = ClampF(p.v[g] - d, kCapabilityWhitelist[g].minVal, kCapabilityWhitelist[g].maxVal);
        p.v[b] = ClampF(p.v[b] - d, kCapabilityWhitelist[b].minVal, kCapabilityWhitelist[b].maxVal);
    };
    recenter(kP_SlopeR,  kP_SlopeG,  kP_SlopeB,  1.0f);
    recenter(kP_OffsetR, kP_OffsetG, kP_OffsetB, 0.0f);
    recenter(kP_PowerR,  kP_PowerG,  kP_PowerB,  1.0f);
}

// Main Pure-CPU Dual-Mode Solver (runs on background worker thread; zero GL/UI deps!)
inline LookMatchResult SolveLookMatch(const ImageRgb& srcImg,
                                      const ImageRgb& refImg,
                                      FitModeRequest reqMode,
                                      const std::string& inputSource,
                                      const std::string& readbackPath) noexcept {
    LookMatchResult res{};
    res.inputSource  = inputSource;
    res.readbackPath = readbackPath;

    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);

    if (!refImg.Valid()) {
        res.errorCode = "invalid_reference_image";
        return res;
    }
    const ImageRgb effSrc = srcImg.Valid() ? srcImg : BuildNeutralDstReferenceImage();
    res.refWidth  = refImg.origWidth  > 0 ? refImg.origWidth  : refImg.width;
    res.refHeight = refImg.origHeight > 0 ? refImg.origHeight : refImg.height;

    const VisualFeatures srcF = ExtractVisualFeatures(effSrc);
    const VisualFeatures refF = ExtractVisualFeatures(refImg);
    if (refF.validFraction < 0.03f) {
        res.errorCode = "reference_image_too_dark";
        return res;
    }

    const SceneSimilarity sim = ComputeSceneSimilarity(effSrc, refImg);
    res.zncc     = sim.zncc;
    res.gradCorr = sim.gradCorr;

    bool useSameScene = false;
    if (reqMode == FitModeRequest::SameFrame) {
        useSameScene = true;
        res.usedMode = "same_frame";
    } else if (reqMode == FitModeRequest::SameScene) {
        if (sim.zncc >= 0.55f) {
            useSameScene = true;
            res.usedMode = "same_scene";
            if (sim.zncc < 0.78f) res.warningCode = "partial_scene_alignment";
        } else {
            useSameScene = false;
            res.usedMode = "general";
            res.warningCode = "misaligned_fallback_general";
        }
    } else if (reqMode == FitModeRequest::General) {
        useSameScene = false;
        res.usedMode = "general";
    } else {
        // Auto: promote to Same-Scene ONLY when structural similarity is high
        useSameScene = srcImg.Valid() && sim.sameScene;
        res.usedMode = useSameScene ? "same_scene" : "general";
    }

    static constexpr int kStage1Idx[10] = {
        kP_Exposure, kP_Temp, kP_Tint, kP_Contrast, kP_Lightness,
        kP_Gamma, kP_Sat, kP_Vibrance, kP_Filmic, kP_HlDesat
    };
    static constexpr int kStage2CdlIdx[9] = {
        kP_SlopeR, kP_SlopeG, kP_SlopeB,
        kP_OffsetR, kP_OffsetG, kP_OffsetB,
        kP_PowerR, kP_PowerG, kP_PowerB
    };
    static constexpr int kStage3VigIdx[1] = { kP_Vignette };

    ParamVector best = ParamVector::Identity();

    if (useSameScene) {
        const auto pairs = BuildAlignedPairs(effSrc, refImg, 384u);
        if (pairs.size() < 48u) {
            res.errorCode = "insufficient_valid_pixels";
            return res;
        }
        float dBefore = 0.0f, dAfter = 0.0f;
        EvaluateSameSceneLoss(pairs, refF, best, false, &dBefore);
        res.deltaEBefore = dBefore;

        if (reqMode == FitModeRequest::SameFrame && dBefore < 0.35f) {
            // No post-ColourCube external shader change detected!
            res.ok                = true;
            res.warningCode       = "no_post_colourcube_delta";
            res.deltaEAfter       = dBefore;
            res.colorConfidence   = 0.95f;
            res.spatialConfidence = 0.95f;
            res.overallConfidence = 0.95f;
            QueryPerformanceCounter(&t1);
            res.elapsedMs = (freq.QuadPart > 0)
                ? static_cast<float>(double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(freq.QuadPart))
                : 0.0f;
            return res;
        }

        // Stage 1: Global color & tone (10 params)
        OptimizeParamSubset(best, kStage1Idx, 10, 12, [&](const ParamVector& pv) {
            return EvaluateSameSceneLoss(pairs, refF, pv, false, nullptr);
        });

        // Stage 3a: Spatial Vignette (observable in Same-Scene!)
        OptimizeParamSubset(best, kStage3VigIdx, 1, 7, [&](const ParamVector& pv) {
            return EvaluateSameSceneLoss(pairs, refF, pv, true, nullptr);
        });
        if (best.v[kP_Vignette] < 0.03f) best.v[kP_Vignette] = 0.0f;
        res.vignetteFitted = (best.v[kP_Vignette] > 0.025f);

        // Stage 2: Zero-mean CDL split-tone polish if residual warrants it
        float dAfterStage1 = 0.0f;
        EvaluateSameSceneLoss(pairs, refF, best, true, &dAfterStage1);
        if (dAfterStage1 > 1.8f) {
            ParamVector cdlCand = best;
            OptimizeParamSubset(cdlCand, kStage2CdlIdx, 9, 8, [&](const ParamVector& pv) {
                return EvaluateSameSceneLoss(pairs, refF, pv, true, nullptr);
            });
            EnforceZeroMeanCdl(cdlCand);
            float dCdl = 0.0f;
            const float lBefore = EvaluateSameSceneLoss(pairs, refF, best, true, nullptr);
            const float lAfter  = EvaluateSameSceneLoss(pairs, refF, cdlCand, true, &dCdl);
            if (lAfter + 1e-5f < lBefore) {
                best = cdlCand;
            }
        }

        EvaluateSameSceneLoss(pairs, refF, best, true, &dAfter);
        res.deltaEAfter = dAfter;

        // Stage 3b: Micro-contrast / sharpness ratio (observable ONLY in Same-Scene!)
        if (srcF.microContrast > 0.004f && refF.microContrast > srcF.microContrast * 1.12f) {
            const float ratio = refF.microContrast / srcF.microContrast;
            const float estSharp = ClampF((ratio - 1.0f) * 1.65f, 0.0f, 1.40f);
            if (estSharp >= 0.15f) {
                best.v[kP_Sharp] = estSharp;
                res.sharpFitted  = true;
            }
        }

        const float impRatio  = (dBefore > 0.5f) ? ClampF(1.0f - (dAfter / dBefore), 0.0f, 1.0f) : 1.0f;
        const float absQual   = ClampF(1.0f - (dAfter / 14.0f), 0.0f, 1.0f);
        res.colorConfidence   = ClampF(0.60f * absQual + 0.40f * impRatio, 0.05f, 0.99f);
        res.spatialConfidence = ClampF((sim.zncc - 0.40f) / 0.58f, 0.25f, 0.98f);
        res.overallConfidence = ClampF(0.70f * res.colorConfidence + 0.30f * res.spatialConfidence, 0.10f, 0.98f);
    } else {
        // Mode 1: Scene-Agnostic General Look Matching (Cross-Game / Movie / Reference)
        const auto samples = BuildStratifiedSamples(effSrc, 384u);
        if (samples.size() < 48u) {
            res.errorCode = "insufficient_source_samples";
            return res;
        }
        float dBefore = 0.0f, dAfter = 0.0f;
        EvaluateGeneralLoss(samples, refF, best, &dBefore);
        res.deltaEBefore = dBefore;

        // Stage 1: Global tone & color distribution transfer (10 params)
        OptimizeParamSubset(best, kStage1Idx, 10, 12, [&](const ParamVector& pv) {
            return EvaluateGeneralLoss(samples, refF, pv, nullptr);
        });

        // Stage 2: Damped zero-mean split-tone CDL ONLY if 3-zone chromaticity residual remains
        float dStage1 = 0.0f;
        EvaluateGeneralLoss(samples, refF, best, &dStage1);
        if (dStage1 > 2.2f) {
            ParamVector cdlCand = best;
            OptimizeParamSubset(cdlCand, kStage2CdlIdx, 9, 7, [&](const ParamVector& pv) {
                return EvaluateGeneralLoss(samples, refF, pv, nullptr);
            });
            EnforceZeroMeanCdl(cdlCand);
            const float lBefore = EvaluateGeneralLoss(samples, refF, best, nullptr);
            const float lAfter  = EvaluateGeneralLoss(samples, refF, cdlCand, nullptr);
            if (lAfter + 1e-5f < lBefore) {
                best = cdlCand;
            }
        }

        // Stage 3 (Conservative Heuristic): Single-image vignette ONLY when all 4 corners
        // darken symmetrically AND strongly; sharp is NEVER guessed in General mode!
        if (refF.cornerSymmetry >= 0.78f && refF.radialCornerDrop > 0.14f) {
            const float vigEst = ClampF((refF.radialCornerDrop - 0.10f) * 0.75f, 0.0f, 0.22f);
            if (vigEst >= 0.05f) {
                best.v[kP_Vignette] = vigEst;
                res.vignetteFitted  = true;
            }
        }

        EvaluateGeneralLoss(samples, refF, best, &dAfter);
        res.deltaEAfter = dAfter;

        const float impRatio  = (dBefore > 0.5f) ? ClampF(1.0f - (dAfter / dBefore), 0.0f, 1.0f) : 1.0f;
        const float absQual   = ClampF(1.0f - (dAfter / 16.0f), 0.0f, 1.0f);
        const float richness  = ClampF((refF.stdL / 0.12f) * 0.6f + (refF.validFraction * 0.4f), 0.35f, 1.0f);
        res.colorConfidence   = ClampF((0.55f * absQual + 0.45f * impRatio) * richness, 0.15f, 0.94f);
        // Explicitly honest: cross-scene single image cannot reliably observe Bloom/Sharp/Vignette
        res.spatialConfidence = res.vignetteFitted ? (0.35f * refF.cornerSymmetry) : 0.15f;
        res.overallConfidence = ClampF(0.80f * res.colorConfidence + 0.20f * res.spatialConfidence, 0.15f, 0.88f);
    }

    // Snap tiny noise near default back to exact default -> ultra-short HS1- share codes!
    best.ClampAll();
    for (int i = 0; i < kParamCount; ++i) {
        if (std::fabs(best.v[i] - kCapabilityWhitelist[i].defVal) < 0.006f) {
            best.v[i] = kCapabilityWhitelist[i].defVal;
        }
    }

    res.params = best;
    res.ok     = true;
    QueryPerformanceCounter(&t1);
    res.elapsedMs = (freq.QuadPart > 0)
        ? static_cast<float>(double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(freq.QuadPart))
        : 0.0f;
    return res;
}

// -----------------------------------------------------------------------------
// Layer 1D: Non-Blocking OpenGL 3.2+ Async PBO Frame Sampler (RenderThread)
// -----------------------------------------------------------------------------
struct GlSamplerOps {
    void     (WINAPI* glGenFramebuffers)(int32_t, uint32_t*)                                  = nullptr;
    void     (WINAPI* glDeleteFramebuffers)(int32_t, const uint32_t*)                         = nullptr;
    void     (WINAPI* glBindFramebuffer)(uint32_t, uint32_t)                                  = nullptr;
    void     (WINAPI* glFramebufferTexture2D)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t)= nullptr;
    uint32_t (WINAPI* glCheckFramebufferStatus)(uint32_t)                                     = nullptr;
    void     (WINAPI* glGenTextures)(int32_t, uint32_t*)                                      = nullptr;
    void     (WINAPI* glDeleteTextures)(int32_t, const uint32_t*)                             = nullptr;
    void     (WINAPI* glBindTexture)(uint32_t, uint32_t)                                      = nullptr;
    void     (WINAPI* glTexImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*) = nullptr;
    void     (WINAPI* glTexParameteri)(uint32_t, uint32_t, int32_t)                           = nullptr;
    void     (WINAPI* glGenBuffers)(int32_t, uint32_t*)                                       = nullptr;
    void     (WINAPI* glDeleteBuffers)(int32_t, const uint32_t*)                              = nullptr;
    void     (WINAPI* glBindBuffer)(uint32_t, uint32_t)                                       = nullptr;
    void     (WINAPI* glBufferData)(uint32_t, ptrdiff_t, const void*, uint32_t)               = nullptr;
    void*    (WINAPI* glMapBufferRange)(uint32_t, ptrdiff_t, ptrdiff_t, uint32_t)             = nullptr;
    uint8_t  (WINAPI* glUnmapBuffer)(uint32_t)                                                = nullptr;
    void     (WINAPI* glReadPixels)(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void*) = nullptr;
    void     (WINAPI* glPixelStorei)(uint32_t, int32_t)                                       = nullptr;
    void     (WINAPI* glGetIntegerv)(uint32_t, int32_t*)                                      = nullptr;
    void     (WINAPI* glEnable)(uint32_t)                                                     = nullptr;
    void     (WINAPI* glDisable)(uint32_t)                                                    = nullptr;
    void     (WINAPI* glBlitFramebuffer)(int32_t, int32_t, int32_t, int32_t,
                                         int32_t, int32_t, int32_t, int32_t,
                                         uint32_t, uint32_t)                                  = nullptr;
    void*    (WINAPI* glFenceSync)(uint32_t, uint32_t)                                        = nullptr;
    uint32_t (WINAPI* glClientWaitSync)(void*, uint32_t, uint64_t)                            = nullptr;
    void     (WINAPI* glDeleteSync)(void*)                                                    = nullptr;

    bool CanAsyncPbo() const noexcept {
        return glGenFramebuffers && glBindFramebuffer && glFramebufferTexture2D && glCheckFramebufferStatus &&
               glGenTextures && glBindTexture && glTexImage2D && glTexParameteri &&
               glGenBuffers && glBindBuffer && glBufferData && glMapBufferRange && glUnmapBuffer &&
               glReadPixels && glPixelStorei && glGetIntegerv && glEnable && glDisable &&
               glBlitFramebuffer && glFenceSync && glClientWaitSync && glDeleteSync;
    }

    bool CanSyncFallback() const noexcept {
        return glReadPixels && glPixelStorei && glGetIntegerv;
    }
};

// Box-filter RGBA8 bottom-up GL readback -> top-down ImageRgb (kAnalysisW x kAnalysisH)
inline ImageRgb DownsampleGlRgbaBottomUp(const uint8_t* rgba, int32_t srcW, int32_t srcH) noexcept {
    ImageRgb out;
    if (!rgba || srcW <= 0 || srcH <= 0) return out;
    out.width      = kAnalysisW;
    out.height     = kAnalysisH;
    out.origWidth  = srcW;
    out.origHeight = srcH;
    out.rgb.resize(static_cast<size_t>(kAnalysisW) * static_cast<size_t>(kAnalysisH) * 3u);
    constexpr float kInv255 = 1.0f / 255.0f;

    for (int32_t dy = 0; dy < kAnalysisH; ++dy) {
        // Flip Y: dy=0 is top of image -> bottom-up row near srcH-1
        const int32_t topY0 = (dy * srcH) / kAnalysisH;
        const int32_t topY1 = std::max(topY0 + 1, ((dy + 1) * srcH) / kAnalysisH);
        for (int32_t dx = 0; dx < kAnalysisW; ++dx) {
            const int32_t x0 = (dx * srcW) / kAnalysisW;
            const int32_t x1 = std::max(x0 + 1, ((dx + 1) * srcW) / kAnalysisW);
            // Stride within large cells (e.g. 4K -> max 4x4 taps per cell = ultra fast)
            const int32_t stepX = std::max(1, (x1 - x0) / 4);
            const int32_t stepY = std::max(1, (topY1 - topY0) / 4);
            uint32_t sr = 0, sg = 0, sb = 0, cnt = 0;
            for (int32_t ty = topY0; ty < topY1; ty += stepY) {
                const int32_t glY = (srcH - 1) - ty;
                const uint8_t* row = rgba + static_cast<size_t>(glY) * static_cast<size_t>(srcW) * 4u;
                for (int32_t tx = x0; tx < x1; tx += stepX) {
                    const uint8_t* px = row + static_cast<size_t>(tx) * 4u;
                    sr += px[0];
                    sg += px[1];
                    sb += px[2];
                    ++cnt;
                }
            }
            const size_t o = (static_cast<size_t>(dy) * kAnalysisW + dx) * 3u;
            const float inv = (cnt > 0u) ? (kInv255 / static_cast<float>(cnt)) : 0.0f;
            out.rgb[o + 0u] = sr * inv;
            out.rgb[o + 1u] = sg * inv;
            out.rgb[o + 2u] = sb * inv;
        }
    }
    return out;
}

inline ImageRgb ConvertExactGlRgbaBottomUp(const uint8_t* rgba, int32_t w, int32_t h,
                                           int32_t origW, int32_t origH) noexcept {
    ImageRgb out;
    if (!rgba || w <= 0 || h <= 0) return out;
    out.width      = w;
    out.height     = h;
    out.origWidth  = origW > 0 ? origW : w;
    out.origHeight = origH > 0 ? origH : h;
    out.rgb.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 3u);
    constexpr float kInv255 = 1.0f / 255.0f;
    for (int32_t y = 0; y < h; ++y) {
        const uint8_t* srcRow = rgba + static_cast<size_t>(h - 1 - y) * static_cast<size_t>(w) * 4u;
        float* dstRow = out.rgb.data() + static_cast<size_t>(y) * static_cast<size_t>(w) * 3u;
        for (int32_t x = 0; x < w; ++x) {
            dstRow[x * 3u + 0u] = srcRow[x * 4u + 0u] * kInv255;
            dstRow[x * 3u + 1u] = srcRow[x * 4u + 1u] * kInv255;
            dstRow[x * 3u + 2u] = srcRow[x * 4u + 2u] * kInv255;
        }
    }
    return out;
}

class GlAsyncFrameSampler {
public:
    static constexpr size_t kPboBytes = static_cast<size_t>(kAnalysisW) * static_cast<size_t>(kAnalysisH) * 4u;

    void SetOps(const GlSamplerOps& ops, void* glCtx) noexcept {
        if (ctx_ != glCtx) {
            ResetHandlesOnly();
            ctx_ = glCtx;
        }
        ops_ = ops;
    }

    void OnContextReset() noexcept {
        ResetHandlesOnly();
        ctx_ = nullptr;
    }

    bool HasPendingSlot(int slot) const noexcept {
        return slot >= 0 && slot < 2 && pending_[slot];
    }

    // Issue non-blocking GPU downsample + PBO async DMA readback (Native GL)
    // or honest one-shot fallback readback (ANGLE D3D9 GLES 2.0).
    // Returns true if capture succeeded (async queued OR sync completed).
    bool CaptureCurrentFbo(int slot, uint32_t actualSrcFbo, const int32_t vp[4],
                           ImageRgb& outSyncImg, bool& outCompletedNow,
                           std::string& outReadbackPath) noexcept {
        outCompletedNow = false;
        if (slot < 0 || slot >= 2) return false;
        const int32_t vw = vp[2];
        const int32_t vh = vp[3];
        if (vw < 16 || vh < 16 || vw > kMaxImageDim || vh > kMaxImageDim) return false;

        if (ops_.CanAsyncPbo() && EnsureAsyncResources()) {
            // Cancel any stale pending fence on this slot first
            if (fence_[slot]) {
                ops_.glDeleteSync(fence_[slot]);
                fence_[slot] = nullptr;
            }

            int32_t prevReadFbo = 0, prevDrawFbo = 0, prevPbo = 0, prevPack = 4, prevScissor = 0;
            ops_.glGetIntegerv(kGlReadFramebufferBinding, &prevReadFbo);
            ops_.glGetIntegerv(kGlDrawFramebufferBinding, &prevDrawFbo);
            ops_.glGetIntegerv(kGlPixelPackBufferBinding, &prevPbo);
            ops_.glGetIntegerv(kGlPackAlignment,          &prevPack);
            ops_.glGetIntegerv(kGlScissorTest,            &prevScissor);

            if (prevScissor) ops_.glDisable(kGlScissorTest);

            ops_.glBindFramebuffer(kGlReadFramebuffer, actualSrcFbo);
            ops_.glBindFramebuffer(kGlDrawFramebuffer, downFbo_[slot]);
            ops_.glBlitFramebuffer(vp[0], vp[1], vp[0] + vw, vp[1] + vh,
                                   0, 0, kAnalysisW, kAnalysisH,
                                   kGlColorBufferBit, kGlLinear);

            ops_.glBindFramebuffer(kGlReadFramebuffer, downFbo_[slot]);
            ops_.glBindBuffer(kGlPixelPackBuffer, pbo_[slot]);
            ops_.glPixelStorei(kGlPackAlignment, 4);
            ops_.glReadPixels(0, 0, kAnalysisW, kAnalysisH, kGlRgba, kGlUnsignedByte, nullptr);
            fence_[slot] = ops_.glFenceSync(kGlSyncGpuCommandsComplete, 0u);

            // Restore exact prior GL state
            ops_.glBindBuffer(kGlPixelPackBuffer, static_cast<uint32_t>(prevPbo));
            ops_.glPixelStorei(kGlPackAlignment, prevPack);
            ops_.glBindFramebuffer(kGlReadFramebuffer, static_cast<uint32_t>(prevReadFbo));
            ops_.glBindFramebuffer(kGlDrawFramebuffer, static_cast<uint32_t>(prevDrawFbo));
            if (prevScissor) ops_.glEnable(kGlScissorTest);

            if (fence_[slot]) {
                pending_[slot]  = true;
                origW_[slot]    = vw;
                origH_[slot]    = vh;
                outReadbackPath = "async_pbo";
                return true;
            }
        }

        // Legacy ANGLE D3D9 GLES 2.0 fallback (one-shot on user click only)
        if (ops_.CanSyncFallback()) {
            const uint64_t bytes = static_cast<uint64_t>(vw) * static_cast<uint64_t>(vh) * 4ull;
            if (bytes > kMaxDecodedRawBytes) return false;
            int32_t prevPack = 4;
            ops_.glGetIntegerv(kGlPackAlignment, &prevPack);
            ops_.glPixelStorei(kGlPackAlignment, 4);
            std::vector<uint8_t> tmp(static_cast<size_t>(bytes));
            ops_.glReadPixels(vp[0], vp[1], vw, vh, kGlRgba, kGlUnsignedByte, tmp.data());
            ops_.glPixelStorei(kGlPackAlignment, prevPack);
            outSyncImg      = DownsampleGlRgbaBottomUp(tmp.data(), vw, vh);
            outCompletedNow = outSyncImg.Valid();
            outReadbackPath = "sync_fallback";
            return outCompletedNow;
        }
        return false;
    }

    // Zero-stall non-blocking poll (timeout = 0 ns) called at SwapBuffers
    bool TryHarvestAsync(int slot, ImageRgb& outImg) noexcept {
        if (slot < 0 || slot >= 2 || !pending_[slot] || !fence_[slot]) return false;
        const uint32_t st = ops_.glClientWaitSync(fence_[slot], 0u, 0ull);
        if (st != kGlAlreadySignaled && st != kGlConditionSatisfied) {
            return false; // GPU DMA still in flight; check next frame with 0 stall!
        }

        int32_t prevPbo = 0;
        ops_.glGetIntegerv(kGlPixelPackBufferBinding, &prevPbo);
        ops_.glBindBuffer(kGlPixelPackBuffer, pbo_[slot]);
        const void* ptr = ops_.glMapBufferRange(kGlPixelPackBuffer, 0,
                                                static_cast<ptrdiff_t>(kPboBytes), kGlMapReadBit);
        bool ok = false;
        if (ptr) {
            outImg = ConvertExactGlRgbaBottomUp(static_cast<const uint8_t*>(ptr),
                                                kAnalysisW, kAnalysisH, origW_[slot], origH_[slot]);
            ops_.glUnmapBuffer(kGlPixelPackBuffer);
            ok = outImg.Valid();
        }
        ops_.glBindBuffer(kGlPixelPackBuffer, static_cast<uint32_t>(prevPbo));

        ops_.glDeleteSync(fence_[slot]);
        fence_[slot]   = nullptr;
        pending_[slot] = false;
        return ok;
    }

    void CancelPending() noexcept {
        for (int s = 0; s < 2; ++s) {
            if (fence_[s] && ops_.glDeleteSync) {
                ops_.glDeleteSync(fence_[s]);
            }
            fence_[s]   = nullptr;
            pending_[s] = false;
        }
    }

private:
    void ResetHandlesOnly() noexcept {
        for (int s = 0; s < 2; ++s) {
            downFbo_[s] = 0u;
            downTex_[s] = 0u;
            pbo_[s]     = 0u;
            fence_[s]   = nullptr;
            pending_[s] = false;
        }
        ready_  = false;
        failed_ = false;
    }

    bool EnsureAsyncResources() noexcept {
        if (ready_) return true;
        if (failed_) return false;

        int32_t prevReadFbo = 0, prevDrawFbo = 0, prevTex = 0, prevPbo = 0;
        ops_.glGetIntegerv(kGlReadFramebufferBinding, &prevReadFbo);
        ops_.glGetIntegerv(kGlDrawFramebufferBinding, &prevDrawFbo);
        ops_.glGetIntegerv(kGlTextureBinding2D,       &prevTex);
        ops_.glGetIntegerv(kGlPixelPackBufferBinding, &prevPbo);

        bool ok = true;
        ops_.glGenFramebuffers(2, downFbo_);
        ops_.glGenTextures(2, downTex_);
        ops_.glGenBuffers(2, pbo_);
        for (int s = 0; s < 2 && ok; ++s) {
            if (!downFbo_[s] || !downTex_[s] || !pbo_[s]) { ok = false; break; }
            ops_.glBindTexture(kGlTexture2D, downTex_[s]);
            ops_.glTexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<int32_t>(kGlLinear));
            ops_.glTexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<int32_t>(kGlLinear));
            ops_.glTexParameteri(kGlTexture2D, kGlTextureWrapS, static_cast<int32_t>(kGlClampToEdge));
            ops_.glTexParameteri(kGlTexture2D, kGlTextureWrapT, static_cast<int32_t>(kGlClampToEdge));
            ops_.glTexImage2D(kGlTexture2D, 0, static_cast<int32_t>(kGlRgba8),
                              kAnalysisW, kAnalysisH, 0, kGlRgba, kGlUnsignedByte, nullptr);

            ops_.glBindFramebuffer(kGlFramebuffer, downFbo_[s]);
            ops_.glFramebufferTexture2D(kGlFramebuffer, kGlColorAttachment0, kGlTexture2D, downTex_[s], 0);
            if (ops_.glCheckFramebufferStatus(kGlFramebuffer) != kGlFramebufferComplete) {
                ok = false;
                break;
            }
            ops_.glBindBuffer(kGlPixelPackBuffer, pbo_[s]);
            ops_.glBufferData(kGlPixelPackBuffer, static_cast<ptrdiff_t>(kPboBytes), nullptr, kGlStreamRead);
        }

        ops_.glBindBuffer(kGlPixelPackBuffer, static_cast<uint32_t>(prevPbo));
        ops_.glBindTexture(kGlTexture2D, static_cast<uint32_t>(prevTex));
        ops_.glBindFramebuffer(kGlReadFramebuffer, static_cast<uint32_t>(prevReadFbo));
        ops_.glBindFramebuffer(kGlDrawFramebuffer, static_cast<uint32_t>(prevDrawFbo));

        if (!ok) {
            failed_ = true;
            return false;
        }
        ready_ = true;
        return true;
    }

    GlSamplerOps ops_{};
    void*        ctx_        = nullptr;
    uint32_t     downFbo_[2] = { 0u, 0u };
    uint32_t     downTex_[2] = { 0u, 0u };
    uint32_t     pbo_[2]     = { 0u, 0u };
    void*        fence_[2]   = { nullptr, nullptr };
    bool         pending_[2] = { false, false };
    int32_t      origW_[2]   = { 0, 0 };
    int32_t      origH_[2]   = { 0, 0 };
    bool         ready_      = false;
    bool         failed_     = false;
};

// -----------------------------------------------------------------------------
// Coordinator: Bridges MainThread Lua API, RenderThread Async Capture, & Worker
// -----------------------------------------------------------------------------
enum class JobState : uint32_t {
    Idle      = 0u,
    Capturing = 1u,
    Solving   = 2u,
    Done      = 3u,
    Error     = 4u,
};

class LookMatchManager {
public:
    LookMatchManager() noexcept {
        InitializeCriticalSection(&cs_);
    }

    ~LookMatchManager() noexcept {
        DeleteCriticalSection(&cs_);
    }

    // Ultra-fast single atomic check for RenderThread hot path (0.000 ms when idle!)
    bool IsCaptureArmed() const noexcept {
        return armedJobId_.load(std::memory_order_acquire) != 0u;
    }

    JobState State() const noexcept {
        return static_cast<JobState>(state_.load(std::memory_order_acquire));
    }

    // Called from Lua MainThread: grabs clipboard immediately (< 0.5 ms), arms
    // non-blocking GL capture if needed, and kicks off background solve thread.
    bool StartClipboardOrFileJob(FitModeRequest reqMode, const std::wstring* optFilePath,
                                 bool allowLiveSourceCapture, std::string& outErr, uint32_t& outJobId) noexcept {
        outErr.clear();
        ClipboardPayload payload{};
        if (optFilePath && !optFilePath->empty()) {
            payload.kind      = ClipboardKind::FilePath;
            payload.filePath  = *optFilePath;
            payload.sourceTag = "file";
        } else {
            if (!GrabClipboardImagePayload(payload, outErr)) {
                return false;
            }
        }

        const uint32_t jobId = nextJobId_.fetch_add(1u, std::memory_order_relaxed) + 1u;
        EnterCriticalSection(&cs_);
        activeJobId_         = jobId;
        activeReqMode_       = reqMode;
        activePayload_       = std::move(payload);
        activeSrcImg_        = ImageRgb{};
        activeRefImg_        = ImageRgb{};
        activeReadbackPath_  = allowLiveSourceCapture ? "" : "neutral_ref";
        activeNeedDualSlot_  = false;
        activeStartTickMs_   = GetTickCount64();
        lastResult_          = LookMatchResult{};
        lastError_.clear();
        LeaveCriticalSection(&cs_);

        outJobId = jobId;
        if (allowLiveSourceCapture) {
            state_.store(static_cast<uint32_t>(JobState::Capturing), std::memory_order_release);
            armedDualSlot_.store(false, std::memory_order_release);
            armedJobId_.store(jobId, std::memory_order_release);
        } else {
            armedJobId_.store(0u, std::memory_order_release);
            LaunchWorkerLockedOut(jobId);
        }
        return true;
    }

    bool StartSameFrameJob(std::string& outErr, uint32_t& outJobId) noexcept {
        outErr.clear();
        const uint32_t jobId = nextJobId_.fetch_add(1u, std::memory_order_relaxed) + 1u;
        EnterCriticalSection(&cs_);
        activeJobId_         = jobId;
        activeReqMode_       = FitModeRequest::SameFrame;
        activePayload_       = ClipboardPayload{};
        activePayload_.sourceTag = "same_frame_gl";
        activeSrcImg_        = ImageRgb{};
        activeRefImg_        = ImageRgb{};
        activeReadbackPath_.clear();
        activeNeedDualSlot_  = true;
        activeStartTickMs_   = GetTickCount64();
        lastResult_          = LookMatchResult{};
        lastError_.clear();
        LeaveCriticalSection(&cs_);

        outJobId = jobId;
        state_.store(static_cast<uint32_t>(JobState::Capturing), std::memory_order_release);
        armedDualSlot_.store(true, std::memory_order_release);
        armedJobId_.store(jobId, std::memory_order_release);
        return true;
    }

    // Called from RenderThread once slots have been harvested non-blockingly!
    void OnRenderCaptureComplete(uint32_t jobId, ImageRgb srcImg, ImageRgb refImg,
                                 const std::string& readbackPath) noexcept {
        bool launch = false;
        EnterCriticalSection(&cs_);
        if (activeJobId_ == jobId &&
            state_.load(std::memory_order_acquire) == static_cast<uint32_t>(JobState::Capturing)) {
            activeSrcImg_       = std::move(srcImg);
            if (activeNeedDualSlot_) {
                activeRefImg_   = std::move(refImg);
            }
            activeReadbackPath_ = readbackPath;
            launch = true;
        }
        LeaveCriticalSection(&cs_);

        if (launch) {
            LaunchWorkerLockedOut(jobId);
        }
    }

    uint32_t ArmedJobId(bool& outDualSlot) const noexcept {
        outDualSlot = armedDualSlot_.load(std::memory_order_acquire);
        return armedJobId_.load(std::memory_order_acquire);
    }

    void DisarmCapture(uint32_t jobId) noexcept {
        uint32_t expected = jobId;
        armedJobId_.compare_exchange_strong(expected, 0u, std::memory_order_acq_rel);
    }

    // Polled from Lua MainThread; automatically falls back to neutral reference
    // if player clicked Clipboard Match on Main Menu (no COLOURCUBE frame after 350 ms).
    JobState Poll(LookMatchResult& outRes, std::string& outErr) noexcept {
        const JobState st = State();
        if (st == JobState::Capturing) {
            bool timedOut = false;
            bool dual     = false;
            uint32_t jid  = 0u;
            EnterCriticalSection(&cs_);
            const uint64_t elapsed = GetTickCount64() - activeStartTickMs_;
            if (elapsed > 350ull) {
                timedOut = true;
                dual     = activeNeedDualSlot_;
                jid      = activeJobId_;
                if (dual) {
                    lastError_ = "no_world_frame_for_same_frame";
                } else {
                    activeReadbackPath_ = "neutral_ref";
                }
            }
            LeaveCriticalSection(&cs_);
            if (timedOut) {
                DisarmCapture(jid);
                if (dual) {
                    state_.store(static_cast<uint32_t>(JobState::Error), std::memory_order_release);
                    outErr = "no_world_frame_for_same_frame";
                    return JobState::Error;
                }
                LaunchWorkerLockedOut(jid);
                return JobState::Solving;
            }
            return JobState::Capturing;
        }

        if (st == JobState::Done) {
            EnterCriticalSection(&cs_);
            outRes = lastResult_;
            LeaveCriticalSection(&cs_);
            return JobState::Done;
        }
        if (st == JobState::Error) {
            EnterCriticalSection(&cs_);
            outErr = lastError_;
            LeaveCriticalSection(&cs_);
            return JobState::Error;
        }
        return st;
    }

    void ResetToIdle() noexcept {
        armedJobId_.store(0u, std::memory_order_release);
        state_.store(static_cast<uint32_t>(JobState::Idle), std::memory_order_release);
    }

private:
    struct WorkerCtx {
        LookMatchManager* self = nullptr;
        uint32_t          jobId = 0u;
    };

    void LaunchWorkerLockedOut(uint32_t jobId) noexcept {
        state_.store(static_cast<uint32_t>(JobState::Solving), std::memory_order_release);
        auto* ctx = new (std::nothrow) WorkerCtx{ this, jobId };
        if (!ctx) {
            EnterCriticalSection(&cs_);
            if (activeJobId_ == jobId) lastError_ = "worker_oom";
            LeaveCriticalSection(&cs_);
            state_.store(static_cast<uint32_t>(JobState::Error), std::memory_order_release);
            return;
        }
        HANDLE h = CreateThread(nullptr, 0, &LookMatchManager::WorkerProc, ctx, 0, nullptr);
        if (h) {
            CloseHandle(h);
        } else {
            delete ctx;
            EnterCriticalSection(&cs_);
            if (activeJobId_ == jobId) lastError_ = "worker_thread_failed";
            LeaveCriticalSection(&cs_);
            state_.store(static_cast<uint32_t>(JobState::Error), std::memory_order_release);
        }
    }

    static DWORD WINAPI WorkerProc(void* param) noexcept {
        WorkerCtx* ctx = static_cast<WorkerCtx*>(param);
        if (!ctx || !ctx->self) {
            delete ctx;
            return 0u;
        }
        LookMatchManager* self = ctx->self;
        const uint32_t jobId   = ctx->jobId;
        delete ctx;

        const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        FitModeRequest   reqMode = FitModeRequest::Auto;
        ClipboardPayload payload{};
        ImageRgb         srcImg{};
        ImageRgb         refImg{};
        std::string      readbackPath;
        bool             needDual = false;

        EnterCriticalSection(&self->cs_);
        if (self->activeJobId_ != jobId) {
            LeaveCriticalSection(&self->cs_);
            if (SUCCEEDED(comHr)) CoUninitialize();
            return 0u;
        }
        reqMode      = self->activeReqMode_;
        payload      = self->activePayload_;
        srcImg       = self->activeSrcImg_;
        refImg       = self->activeRefImg_;
        readbackPath = self->activeReadbackPath_;
        needDual     = self->activeNeedDualSlot_;
        LeaveCriticalSection(&self->cs_);

        std::string decErr;
        bool haveRef = needDual && refImg.Valid();
        if (!needDual) {
            const bool forceExact = (reqMode == FitModeRequest::SameScene);
            if (payload.kind == ClipboardKind::Dib) {
                std::vector<uint8_t> bmp;
                if (BuildValidatedBmpFromDib(payload.rawDib.data(), payload.rawDib.size(), bmp, decErr)) {
                    haveRef = DecodeImageBytesWithWic(bmp.data(), bmp.size(), forceExact, refImg, decErr);
                }
            } else if (payload.kind == ClipboardKind::FilePath) {
                haveRef = DecodeImageFileWithWic(payload.filePath, forceExact, refImg, decErr);
            } else {
                decErr = "missing_input_payload";
            }
        }

        if (!haveRef) {
            EnterCriticalSection(&self->cs_);
            if (self->activeJobId_ == jobId) {
                self->lastError_ = decErr.empty() ? "decode_failed" : decErr;
                self->state_.store(static_cast<uint32_t>(JobState::Error), std::memory_order_release);
            }
            LeaveCriticalSection(&self->cs_);
            if (SUCCEEDED(comHr)) CoUninitialize();
            return 0u;
        }

        LookMatchResult res = SolveLookMatch(srcImg, refImg, reqMode, payload.sourceTag, readbackPath);

        EnterCriticalSection(&self->cs_);
        if (self->activeJobId_ == jobId) {
            if (res.ok) {
                self->lastResult_ = std::move(res);
                self->state_.store(static_cast<uint32_t>(JobState::Done), std::memory_order_release);
            } else {
                self->lastError_ = res.errorCode.empty() ? "solve_failed" : res.errorCode;
                self->state_.store(static_cast<uint32_t>(JobState::Error), std::memory_order_release);
            }
        }
        LeaveCriticalSection(&self->cs_);

        if (SUCCEEDED(comHr)) CoUninitialize();
        return 0u;
    }

    CRITICAL_SECTION      cs_{};
    std::atomic<uint32_t> nextJobId_{ 0u };
    std::atomic<uint32_t> armedJobId_{ 0u };
    std::atomic<bool>     armedDualSlot_{ false };
    std::atomic<uint32_t> state_{ static_cast<uint32_t>(JobState::Idle) };

    uint32_t              activeJobId_        = 0u;
    FitModeRequest        activeReqMode_      = FitModeRequest::Auto;
    ClipboardPayload      activePayload_{};
    ImageRgb              activeSrcImg_{};
    ImageRgb              activeRefImg_{};
    std::string           activeReadbackPath_;
    bool                  activeNeedDualSlot_ = false;
    uint64_t              activeStartTickMs_  = 0ull;
    LookMatchResult       lastResult_{};
    std::string           lastError_;
};

} // namespace hs_look_match
