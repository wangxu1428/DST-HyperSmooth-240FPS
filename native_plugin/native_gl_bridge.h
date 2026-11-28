#pragma once

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "gpu_driver_compat.h"

namespace hs_native_gl {

enum class BackendMode : int {
    D3D9 = 0,
    OpenGL = 1,
    Auto = 2,
};

inline BackendMode ParseBackendMode(const wchar_t* s) noexcept {
    if (!s || !*s) return BackendMode::Auto;
    wchar_t buf[32]{};
    size_t j = 0;
    for (size_t i = 0; s[i] && j < 31; ++i) {
        wchar_t c = s[i];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') continue;
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        buf[j++] = c;
    }
    buf[j] = L'\0';
    if (wcscmp(buf, L"d3d9") == 0 || wcscmp(buf, L"angle") == 0 || wcscmp(buf, L"0") == 0) {
        return BackendMode::D3D9;
    }
    if (wcscmp(buf, L"opengl") == 0 || wcscmp(buf, L"gl") == 0 || wcscmp(buf, L"es2") == 0 || wcscmp(buf, L"1") == 0) {
        return BackendMode::OpenGL;
    }
    return BackendMode::Auto;
}

inline const char* BackendModeName(BackendMode m) noexcept {
    switch (m) {
        case BackendMode::D3D9:   return "d3d9";
        case BackendMode::OpenGL: return "opengl";
        default:                  return "auto";
    }
}

struct GlesImportEntry {
    uint16_t    ordinal;
    const char* name;
};

// Exact 76 libGLESv2.dll imports used by dontstarve_steam_x64.exe (Build 762172).
inline constexpr GlesImportEntry kDstGles2Imports[76] = {
    {   1, "glActiveTexture" },
    {   2, "glAttachShader" },
    {   3, "glBindAttribLocation" },
    {   4, "glBindBuffer" },
    {   5, "glBindFramebuffer" },
    {   6, "glBindRenderbuffer" },
    {   7, "glBindTexture" },
    {   9, "glBlendEquation" },
    {  11, "glBlendFunc" },
    {  13, "glBufferData" },
    {  15, "glCheckFramebufferStatus" },
    {  16, "glClear" },
    {  17, "glClearColor" },
    {  19, "glClearStencil" },
    {  20, "glColorMask" },
    {  21, "glCompileShader" },
    {  22, "glCompressedTexImage2D" },
    {  26, "glCreateProgram" },
    {  27, "glCreateShader" },
    {  28, "glCullFace" },
    {  29, "glDeleteBuffers" },
    {  30, "glDeleteFramebuffers" },
    {  31, "glDeleteTextures" },
    {  32, "glDeleteProgram" },
    {  33, "glDeleteRenderbuffers" },
    {  34, "glDeleteShader" },
    {  36, "glDepthFunc" },
    {  37, "glDepthMask" },
    {  39, "glDisable" },
    {  40, "glDisableVertexAttribArray" },
    {  41, "glDrawArrays" },
    {  42, "glDrawElements" },
    {  43, "glEnable" },
    {  44, "glEnableVertexAttribArray" },
    {  45, "glFinish" },
    {  47, "glFramebufferRenderbuffer" },
    {  48, "glFramebufferTexture2D" },
    {  49, "glFrontFace" },
    {  50, "glGenBuffers" },
    {  52, "glGenFramebuffers" },
    {  53, "glGenRenderbuffers" },
    {  54, "glGenTextures" },
    {  55, "glGetActiveAttrib" },
    {  58, "glGetAttribLocation" },
    {  61, "glGetError" },
    {  64, "glGetIntegerv" },
    {  65, "glGetProgramiv" },
    {  66, "glGetProgramInfoLog" },
    {  68, "glGetShaderiv" },
    {  69, "glGetShaderInfoLog" },
    {  72, "glGetString" },
    {  77, "glGetUniformLocation" },
    {  90, "glLinkProgram" },
    {  91, "glPixelStorei" },
    {  92, "glPolygonOffset" },
    {  93, "glReadPixels" },
    {  95, "glRenderbufferStorage" },
    {  97, "glScissor" },
    {  99, "glShaderSource" },
    { 100, "glStencilFunc" },
    { 102, "glStencilMask" },
    { 104, "glStencilOp" },
    { 106, "glTexImage2D" },
    { 109, "glTexParameteri" },
    { 112, "glUniform1f" },
    { 113, "glUniform1fv" },
    { 114, "glUniform1i" },
    { 117, "glUniform2fv" },
    { 121, "glUniform3fv" },
    { 125, "glUniform4fv" },
    { 128, "glUniformMatrix2fv" },
    { 129, "glUniformMatrix3fv" },
    { 130, "glUniformMatrix4fv" },
    { 131, "glUseProgram" },
    { 141, "glVertexAttribPointer" },
    { 142, "glViewport" },
};

// Explicit WGL/GL/EGL constants (avoid <GL/gl.h> SDK drift under Zig/MinGW).
inline constexpr int32_t kWglContextMajorVersionArb            = 0x2091;
inline constexpr int32_t kWglContextMinorVersionArb            = 0x2092;
inline constexpr int32_t kWglContextProfileMaskArb             = 0x9126;
inline constexpr int32_t kWglContextCompatibilityProfileBitArb = 0x0002;
inline constexpr int32_t kWglContextEs2ProfileBitExt           = 0x0004;

inline constexpr int     kWglDrawToWindowArb                   = 0x2001;
inline constexpr int     kWglAccelerationArb                   = 0x2003;
inline constexpr int     kWglSupportOpenglArb                  = 0x2010;
inline constexpr int     kWglDoubleBufferArb                   = 0x2011;
inline constexpr int     kWglPixelTypeArb                      = 0x2013;
inline constexpr int     kWglColorBitsArb                      = 0x2014;
inline constexpr int     kWglRedBitsArb                        = 0x2015;
inline constexpr int     kWglGreenBitsArb                      = 0x2017;
inline constexpr int     kWglBlueBitsArb                       = 0x2019;
inline constexpr int     kWglAlphaBitsArb                      = 0x201B;
inline constexpr int     kWglDepthBitsArb                      = 0x2022;
inline constexpr int     kWglStencilBitsArb                    = 0x2023;
inline constexpr int     kWglFullAccelerationArb               = 0x2027;
inline constexpr int     kWglTypeRgbaArb                       = 0x202B;
inline constexpr int     kWglTypeRgbaFloatArb                  = 0x21A0;

inline constexpr uint32_t kGlVendor   = 0x1F00u;
inline constexpr uint32_t kGlRenderer = 0x1F01u;
inline constexpr uint32_t kGlVersion  = 0x1F02u;
inline constexpr uint32_t kGlExts     = 0x1F03u;

inline constexpr uint32_t kEglSuccess          = 0x3000u;
inline constexpr uint32_t kEglBadAccess        = 0x3002u;
inline constexpr uint32_t kEglBadAttribute     = 0x3004u;
inline constexpr uint32_t kEglBadConfig        = 0x3005u;
inline constexpr uint32_t kEglBadContext       = 0x3006u;
inline constexpr uint32_t kEglBadDisplay       = 0x3008u;
inline constexpr uint32_t kEglBadMatch         = 0x3009u;
inline constexpr uint32_t kEglBadNativeWindow  = 0x300Bu;
inline constexpr uint32_t kEglNone             = 0x3038u;

using ChoosePixelFormat_t            = int (WINAPI*)(HDC, const PIXELFORMATDESCRIPTOR*);
using SetPixelFormat_t               = BOOL (WINAPI*)(HDC, int, const PIXELFORMATDESCRIPTOR*);
using GetPixelFormat_t               = int (WINAPI*)(HDC);
using DescribePixelFormat_t          = int (WINAPI*)(HDC, int, UINT, LPPIXELFORMATDESCRIPTOR);
using SwapBuffers_t                  = BOOL (WINAPI*)(HDC);

using WglCreateContext_t             = HGLRC (WINAPI*)(HDC);
using WglDeleteContext_t             = BOOL (WINAPI*)(HGLRC);
using WglMakeCurrent_t               = BOOL (WINAPI*)(HDC, HGLRC);
using WglGetCurrentContext_t         = HGLRC (WINAPI*)();
using WglGetCurrentDC_t              = HDC (WINAPI*)();
using WglGetProcAddress_t            = PROC (WINAPI*)(LPCSTR);
using WglGetExtensionsStringArb_t    = const char* (WINAPI*)(HDC);
using WglCreateContextAttribsArb_t   = HGLRC (WINAPI*)(HDC, HGLRC, const int*);
using WglSwapIntervalExt_t           = BOOL (WINAPI*)(int);
using WglChoosePixelFormatArb_t      = BOOL (WINAPI*)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
using WglGetPixelFormatAttribivArb_t = BOOL (WINAPI*)(HDC, int, int, UINT, const int*, int*);

using GlGetString_t              = const char* (WINAPI*)(uint32_t);
using GlGetIntegerv_t            = void (WINAPI*)(uint32_t, int32_t*);
using GlGetError_t               = uint32_t (WINAPI*)();
using GlGetFloatv_t              = void (WINAPI*)(uint32_t, float*);
using GlTexParameterf_t          = void (WINAPI*)(uint32_t, uint32_t, float);
using GlFinish_t                 = void (WINAPI*)();

struct WglRuntime {
    HMODULE                 hOpengl32 = nullptr;
    HMODULE                 hGdi32    = nullptr;

    ChoosePixelFormat_t     pChoosePixelFormat   = nullptr;
    SetPixelFormat_t        pSetPixelFormat      = nullptr;
    GetPixelFormat_t        pGetPixelFormat      = nullptr;
    DescribePixelFormat_t   pDescribePixelFormat = nullptr;
    SwapBuffers_t           pSwapBuffers         = nullptr;

    WglCreateContext_t      pWglCreateContext     = nullptr;
    WglDeleteContext_t      pWglDeleteContext     = nullptr;
    WglMakeCurrent_t        pWglMakeCurrent       = nullptr;
    WglGetCurrentContext_t  pWglGetCurrentContext = nullptr;
    WglGetCurrentDC_t       pWglGetCurrentDC      = nullptr;
    WglGetProcAddress_t     pWglGetProcAddress    = nullptr;

    WglGetExtensionsStringArb_t    pWglGetExtensionsStringArb    = nullptr;
    WglCreateContextAttribsArb_t   pWglCreateContextAttribsArb   = nullptr;
    WglSwapIntervalExt_t           pWglSwapIntervalExt           = nullptr;
    WglChoosePixelFormatArb_t      pWglChoosePixelFormatArb      = nullptr;
    WglGetPixelFormatAttribivArb_t pWglGetPixelFormatAttribivArb = nullptr;

    GlGetString_t           pGlGetString     = nullptr;
    GlGetIntegerv_t         pGlGetIntegerv   = nullptr;
    GlGetError_t            pGlGetError      = nullptr;
    GlGetFloatv_t           pGlGetFloatv     = nullptr;
    GlTexParameterf_t       pGlTexParameterf = nullptr;
    GlFinish_t              pGlFinish        = nullptr;

    int                            chosenArbPixelFormat = 0;
    PIXELFORMATDESCRIPTOR          chosenArbPfd{};
    hs_gpu_compat::PixelFormatInfo chosenPfdInfo{};
    bool                           enableWglExactPfd = true;

    bool Load() noexcept {
        if (hOpengl32 && hGdi32 && pWglCreateContext && pSetPixelFormat && pSwapBuffers) {
            return true;
        }
        if (!hGdi32) {
            hGdi32 = LoadLibraryW(L"gdi32.dll");
        }
        if (!hOpengl32) {
            hOpengl32 = LoadLibraryW(L"opengl32.dll");
        }
        if (!hGdi32 || !hOpengl32) return false;

        pChoosePixelFormat   = reinterpret_cast<ChoosePixelFormat_t>(GetProcAddress(hGdi32, "ChoosePixelFormat"));
        pSetPixelFormat      = reinterpret_cast<SetPixelFormat_t>(GetProcAddress(hGdi32, "SetPixelFormat"));
        pGetPixelFormat      = reinterpret_cast<GetPixelFormat_t>(GetProcAddress(hGdi32, "GetPixelFormat"));
        pDescribePixelFormat = reinterpret_cast<DescribePixelFormat_t>(GetProcAddress(hGdi32, "DescribePixelFormat"));
        pSwapBuffers         = reinterpret_cast<SwapBuffers_t>(GetProcAddress(hGdi32, "SwapBuffers"));

        pWglCreateContext     = reinterpret_cast<WglCreateContext_t>(GetProcAddress(hOpengl32, "wglCreateContext"));
        pWglDeleteContext     = reinterpret_cast<WglDeleteContext_t>(GetProcAddress(hOpengl32, "wglDeleteContext"));
        pWglMakeCurrent       = reinterpret_cast<WglMakeCurrent_t>(GetProcAddress(hOpengl32, "wglMakeCurrent"));
        pWglGetCurrentContext = reinterpret_cast<WglGetCurrentContext_t>(GetProcAddress(hOpengl32, "wglGetCurrentContext"));
        pWglGetCurrentDC      = reinterpret_cast<WglGetCurrentDC_t>(GetProcAddress(hOpengl32, "wglGetCurrentDC"));
        pWglGetProcAddress    = reinterpret_cast<WglGetProcAddress_t>(GetProcAddress(hOpengl32, "wglGetProcAddress"));

        pGlGetString     = reinterpret_cast<GlGetString_t>(GetProcAddress(hOpengl32, "glGetString"));
        pGlGetIntegerv   = reinterpret_cast<GlGetIntegerv_t>(GetProcAddress(hOpengl32, "glGetIntegerv"));
        pGlGetError      = reinterpret_cast<GlGetError_t>(GetProcAddress(hOpengl32, "glGetError"));
        pGlGetFloatv     = reinterpret_cast<GlGetFloatv_t>(GetProcAddress(hOpengl32, "glGetFloatv"));
        pGlTexParameterf = reinterpret_cast<GlTexParameterf_t>(GetProcAddress(hOpengl32, "glTexParameterf"));
        pGlFinish        = reinterpret_cast<GlFinish_t>(GetProcAddress(hOpengl32, "glFinish"));

        return pChoosePixelFormat && pSetPixelFormat && pGetPixelFormat && pSwapBuffers &&
               pWglCreateContext && pWglDeleteContext && pWglMakeCurrent &&
               pWglGetCurrentContext && pWglGetCurrentDC && pWglGetProcAddress &&
               pGlGetString && pGlGetError;
    }

    void* ResolveGlProc(const char* name) const noexcept {
        if (!name || !*name) return nullptr;
        void* p = pWglGetProcAddress ? reinterpret_cast<void*>(pWglGetProcAddress(name)) : nullptr;
        const uintptr_t u = reinterpret_cast<uintptr_t>(p);
        if (p == nullptr || u <= 3u || u == static_cast<uintptr_t>(-1)) {
            p = hOpengl32 ? reinterpret_cast<void*>(GetProcAddress(hOpengl32, name)) : nullptr;
        }
        return p;
    }

    void ResolveWglArbProcsOnCurrent() noexcept {
        if (!pWglGetExtensionsStringArb) {
            pWglGetExtensionsStringArb = reinterpret_cast<WglGetExtensionsStringArb_t>(
                ResolveGlProc("wglGetExtensionsStringARB"));
        }
        if (!pWglCreateContextAttribsArb) {
            pWglCreateContextAttribsArb = reinterpret_cast<WglCreateContextAttribsArb_t>(
                ResolveGlProc("wglCreateContextAttribsARB"));
        }
        if (!pWglSwapIntervalExt) {
            pWglSwapIntervalExt = reinterpret_cast<WglSwapIntervalExt_t>(
                ResolveGlProc("wglSwapIntervalEXT"));
        }
        if (!pWglChoosePixelFormatArb) {
            pWglChoosePixelFormatArb = reinterpret_cast<WglChoosePixelFormatArb_t>(
                ResolveGlProc("wglChoosePixelFormatARB"));
        }
        if (!pWglGetPixelFormatAttribivArb) {
            pWglGetPixelFormatAttribivArb = reinterpret_cast<WglGetPixelFormatAttribivArb_t>(
                ResolveGlProc("wglGetPixelFormatAttribivARB"));
        }
    }
};

struct OptionalGlProcs {
    void* glTexParameterf    = nullptr;
    void* glGetFloatv        = nullptr;
    void* glBufferSubData    = nullptr;
    void* glMapBufferRange   = nullptr;
    void* glUnmapBuffer      = nullptr;
    void* glBufferStorage    = nullptr;
    void* glBufferStorageExt = nullptr;
    void* glFenceSync            = nullptr;
    void* glClientWaitSync       = nullptr;
    void* glDeleteSync           = nullptr;
    void* glFinish               = nullptr;
    void* glGetBufferParameteriv  = nullptr;
    void* glGetBufferSubData      = nullptr;
    void* glBlendFuncSeparate     = nullptr;
    void* glBlendEquationSeparate = nullptr;
    void* glBlitFramebuffer       = nullptr;
    // v3.0 Unleashed: OpenGL 4.3+ Compute / SSBO / Image Load-Store / Instanced Rendering
    void* glDispatchCompute       = nullptr;
    void* glMemoryBarrier         = nullptr;
    void* glBindBufferBase        = nullptr;
    void* glBindImageTexture       = nullptr;
    void* glDrawArraysInstanced    = nullptr;
    void* glDrawElementsInstanced  = nullptr;
    void* glDrawElementsBaseVertex = nullptr;
    void* glVertexAttribDivisor    = nullptr;
    void* glGetTextureHandleARB          = nullptr;
    void* glMakeTextureHandleResidentARB = nullptr;
    void* glMakeTextureHandleNonResidentARB = nullptr;
    void* glTexStorage2D          = nullptr;
    void* glIsEnabled             = nullptr;
    void* glGenQueries            = nullptr;
    void* glDeleteQueries         = nullptr;
    void* glBeginQuery            = nullptr;
    void* glEndQuery              = nullptr;
    void* glGetQueryObjectiv      = nullptr;
    void* glGetQueryObjectui64v   = nullptr;
    void* glUniform4f             = nullptr;
};

struct ProbeResult {
    bool        supported         = false;
    bool        hasAniso          = false;
    bool        hasBufferStorage  = false;
    bool        hasMapBufferRange = false;
    bool        hasSwapControl    = false;
    bool        hasComputeShader  = false;
    bool        hasSsbo           = false;
    bool        hasImageLoadStore = false;
    bool        hasInstancedDraw  = false;
    bool        hasBaseVertex     = false;
    bool        hasBindlessTexture = false;
    bool        isNvidiaVendor    = false;
    bool        hasNonUniformBindless = false;
    bool        ccTexelFetchReady = false;
    int         glMajor           = 0;
    int         glMinor           = 0;
    int         resolvedProcs     = 0;
    std::string vendor;
    std::string renderer;
    std::string version;
    std::string reason;
    hs_gpu_compat::PixelFormatInfo pfdInfo{};
    hs_gpu_compat::CanaryReport    canary{};
};

inline bool ContainsAsciiInsensitive(const char* haystack, const char* needle) noexcept {
    if (!haystack || !needle || !*needle) return false;
    const size_t nlen = std::strlen(needle);
    for (const char* p = haystack; *p; ++p) {
        size_t i = 0;
        while (i < nlen && p[i] &&
               std::tolower(static_cast<unsigned char>(p[i])) ==
               std::tolower(static_cast<unsigned char>(needle[i]))) {
            ++i;
        }
        if (i == nlen) return true;
    }
    return false;
}

inline bool ContainsExtensionToken(const char* extList, const char* token) noexcept {
    if (!extList || !token || !*token) return false;
    const size_t tlen = std::strlen(token);
    const char* p = extList;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
        if (!*p) break;
        const char* start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') ++p;
        if (static_cast<size_t>(p - start) == tlen && std::memcmp(start, token, tlen) == 0) {
            return true;
        }
    }
    return false;
}

inline bool ParseEsVersion(const char* verStr, int& outMajor, int& outMinor) noexcept {
    outMajor = 0;
    outMinor = 0;
    if (!verStr) return false;
    for (const char* p = verStr; *p; ++p) {
        if (*p >= '0' && *p <= '9') {
            outMajor = *p - '0';
            ++p;
            while (*p >= '0' && *p <= '9') {
                outMajor = outMajor * 10 + (*p - '0');
                ++p;
            }
            if (*p == '.') {
                ++p;
                while (*p >= '0' && *p <= '9') {
                    outMinor = outMinor * 10 + (*p - '0');
                    ++p;
                }
            }
            return outMajor > 0;
        }
    }
    return false;
}

inline bool QueryPixelFormatInfo(WglRuntime& rt,
                                 HDC hdc,
                                 int pf,
                                 bool usedArbChoose,
                                 hs_gpu_compat::PixelFormatInfo& outInfo) noexcept {
    outInfo = hs_gpu_compat::PixelFormatInfo{};
    if (!hdc || pf <= 0) return false;
    outInfo.pixelFormat = pf;
    outInfo.usedArbChoose = usedArbChoose;

    bool gotPfd = false;
    PIXELFORMATDESCRIPTOR pfd{};
    if (rt.pDescribePixelFormat) {
        pfd.nSize = sizeof(pfd);
        if (rt.pDescribePixelFormat(hdc, pf, sizeof(pfd), &pfd) > 0) {
            gotPfd = true;
            outInfo.isRgba                = (pfd.iPixelType == PFD_TYPE_RGBA);
            outInfo.isHardwareAccelerated = ((pfd.dwFlags & PFD_GENERIC_FORMAT) == 0);
            outInfo.redBits               = pfd.cRedBits;
            outInfo.greenBits             = pfd.cGreenBits;
            outInfo.blueBits              = pfd.cBlueBits;
            outInfo.alphaBits             = pfd.cAlphaBits;
            outInfo.depthBits             = pfd.cDepthBits;
            outInfo.stencilBits           = pfd.cStencilBits;
        }
    }

    if (rt.pWglGetPixelFormatAttribivArb &&
        rt.pWglGetCurrentContext && rt.pWglGetCurrentContext() != nullptr) {
        const int attrs[8] = {
            kWglPixelTypeArb,
            kWglAccelerationArb,
            kWglRedBitsArb,
            kWglGreenBitsArb,
            kWglBlueBitsArb,
            kWglAlphaBitsArb,
            kWglDepthBitsArb,
            kWglStencilBitsArb,
        };
        int vals[8]{};
        if (rt.pWglGetPixelFormatAttribivArb(hdc, pf, 0, 8, attrs, vals) != FALSE) {
            gotPfd = true;
            outInfo.isRgba = (vals[0] == kWglTypeRgbaArb);
            outInfo.isFloat = (vals[0] == kWglTypeRgbaFloatArb);
            outInfo.isHardwareAccelerated = (vals[1] == kWglFullAccelerationArb);
            outInfo.redBits     = vals[2];
            outInfo.greenBits   = vals[3];
            outInfo.blueBits    = vals[4];
            outInfo.alphaBits   = vals[5];
            outInfo.depthBits   = vals[6];
            outInfo.stencilBits = vals[7];
        }
    }

    if (!gotPfd) return false;
    outInfo.is10BitRgb = (outInfo.redBits == 10 && outInfo.greenBits == 10 && outInfo.blueBits == 10);
    outInfo.isExact8888 = outInfo.isRgba && !outInfo.isFloat &&
                          (outInfo.redBits == 8 && outInfo.greenBits == 8 &&
                           outInfo.blueBits == 8 && outInfo.alphaBits == 8);
    // 10-bit RGBA is non-destructively valid for live Canary verification; only float / palette / <8bit are invalid.
    outInfo.isValidForProbe = outInfo.isRgba && !outInfo.isFloat &&
                              (outInfo.redBits >= 8 && outInfo.greenBits >= 8 && outInfo.blueBits >= 8);
    return true;
}

inline bool SelectBestWindowPixelFormatArb(WglRuntime& rt,
                                           HDC hdc,
                                           int& outPf,
                                           PIXELFORMATDESCRIPTOR& outPfd,
                                           hs_gpu_compat::PixelFormatInfo& outInfo) noexcept {
    outPf = 0;
    std::memset(&outPfd, 0, sizeof(outPfd));
    outInfo = hs_gpu_compat::PixelFormatInfo{};
    if (!hdc || !rt.pWglChoosePixelFormatArb || !rt.pDescribePixelFormat) return false;

    const int iAttribs[] = {
        kWglDrawToWindowArb,  1,
        kWglSupportOpenglArb, 1,
        kWglDoubleBufferArb,  1,
        kWglAccelerationArb,  kWglFullAccelerationArb,
        kWglPixelTypeArb,     kWglTypeRgbaArb,
        kWglColorBitsArb,     32,
        kWglRedBitsArb,       8,
        kWglGreenBitsArb,     8,
        kWglBlueBitsArb,      8,
        kWglAlphaBitsArb,     8,
        kWglDepthBitsArb,     24,
        kWglStencilBitsArb,   8,
        0
    };
    int  candidates[32]{};
    UINT numFormats = 0u;
    if (rt.pWglChoosePixelFormatArb(hdc, iAttribs, nullptr, 32u, candidates, &numFormats) == FALSE ||
        numFormats == 0u) {
        return false;
    }

    const UINT n = (numFormats > 32u) ? 32u : numFormats;
    int bestPf = 0;
    int bestTier = -1; // 3=exact 8-8-8-8 HW, 2=8-8-8-* HW, 1=any valid >=8bit non-float HW RGBA (incl 10-10-10-2)
    hs_gpu_compat::PixelFormatInfo bestInfo{};

    for (UINT i = 0u; i < n; ++i) {
        const int pf = candidates[i];
        if (pf <= 0) continue;
        hs_gpu_compat::PixelFormatInfo cur{};
        if (!QueryPixelFormatInfo(rt, hdc, pf, true, cur)) continue;
        if (!cur.isValidForProbe || !cur.isHardwareAccelerated) continue;

        int tier = 1;
        if (cur.isExact8888 && cur.depthBits >= 24 && cur.stencilBits >= 8) {
            tier = 4;
        } else if (cur.isExact8888) {
            tier = 3;
        } else if (cur.redBits == 8 && cur.greenBits == 8 && cur.blueBits == 8) {
            tier = 2;
        }
        if (tier > bestTier) {
            bestTier = tier;
            bestPf   = pf;
            bestInfo = cur;
            if (bestTier == 4) break;
        }
    }

    if (bestPf <= 0) return false;
    PIXELFORMATDESCRIPTOR desc{};
    desc.nSize = sizeof(desc);
    if (rt.pDescribePixelFormat(hdc, bestPf, sizeof(desc), &desc) <= 0) return false;
    if (desc.iPixelType != PFD_TYPE_RGBA || (desc.dwFlags & PFD_SUPPORT_OPENGL) == 0 ||
        (desc.dwFlags & PFD_DOUBLEBUFFER) == 0) {
        return false;
    }
    outPf   = bestPf;
    outPfd  = desc;
    outInfo = bestInfo;
    return true;
}

inline bool EnsureWindowPixelFormat(WglRuntime& rt, HDC hdc) noexcept {
    if (!hdc || !rt.pGetPixelFormat || !rt.pChoosePixelFormat || !rt.pSetPixelFormat) return false;
    if (rt.pGetPixelFormat(hdc) != 0) return true;

    if (rt.enableWglExactPfd && rt.chosenArbPixelFormat > 0 && rt.pDescribePixelFormat) {
        PIXELFORMATDESCRIPTOR arbPfd{};
        arbPfd.nSize = sizeof(arbPfd);
        if (rt.pDescribePixelFormat(hdc, rt.chosenArbPixelFormat, sizeof(arbPfd), &arbPfd) > 0 &&
            arbPfd.iPixelType == PFD_TYPE_RGBA &&
            (arbPfd.dwFlags & (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER)) ==
                (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER) &&
            arbPfd.cRedBits >= 8 && arbPfd.cGreenBits >= 8 && arbPfd.cBlueBits >= 8) {
            if (rt.pSetPixelFormat(hdc, rt.chosenArbPixelFormat, &arbPfd) != FALSE) {
                return true;
            }
        }
    }

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cAlphaBits   = 8;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType   = PFD_MAIN_PLANE;

    const int pf = rt.pChoosePixelFormat(hdc, &pfd);
    if (pf == 0) return false;
    return rt.pSetPixelFormat(hdc, pf, &pfd) != FALSE;
}

inline hs_gpu_compat::CanaryGlOps BuildCanaryGlOps(const WglRuntime& rt) noexcept {
    hs_gpu_compat::CanaryGlOps gl{};
    auto load = [&](auto& fn, const char* nm) {
        fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(rt.ResolveGlProc(nm));
    };
    load(gl.glGenFramebuffers,          "glGenFramebuffers");
    load(gl.glDeleteFramebuffers,       "glDeleteFramebuffers");
    load(gl.glBindFramebuffer,          "glBindFramebuffer");
    load(gl.glFramebufferTexture2D,     "glFramebufferTexture2D");
    load(gl.glCheckFramebufferStatus,   "glCheckFramebufferStatus");
    load(gl.glGenTextures,              "glGenTextures");
    load(gl.glDeleteTextures,           "glDeleteTextures");
    load(gl.glActiveTexture,            "glActiveTexture");
    load(gl.glBindTexture,              "glBindTexture");
    load(gl.glTexImage2D,               "glTexImage2D");
    load(gl.glTexParameteri,            "glTexParameteri");
    load(gl.glTexParameterf,            "glTexParameterf");
    load(gl.glCreateShader,             "glCreateShader");
    load(gl.glShaderSource,             "glShaderSource");
    load(gl.glCompileShader,            "glCompileShader");
    load(gl.glGetShaderiv,              "glGetShaderiv");
    load(gl.glGetShaderInfoLog,         "glGetShaderInfoLog");
    load(gl.glDeleteShader,             "glDeleteShader");
    load(gl.glCreateProgram,            "glCreateProgram");
    load(gl.glAttachShader,             "glAttachShader");
    load(gl.glBindAttribLocation,       "glBindAttribLocation");
    load(gl.glLinkProgram,              "glLinkProgram");
    load(gl.glGetProgramiv,             "glGetProgramiv");
    load(gl.glGetProgramInfoLog,        "glGetProgramInfoLog");
    load(gl.glDeleteProgram,            "glDeleteProgram");
    load(gl.glUseProgram,               "glUseProgram");
    load(gl.glGetUniformLocation,       "glGetUniformLocation");
    load(gl.glUniform1f,                "glUniform1f");
    load(gl.glUniform1i,                "glUniform1i");
    load(gl.glUniform2fv,               "glUniform2fv");
    load(gl.glUniform3fv,               "glUniform3fv");
    load(gl.glUniform4fv,               "glUniform4fv");
    load(gl.glUniformMatrix4fv,         "glUniformMatrix4fv");
    load(gl.glGenBuffers,               "glGenBuffers");
    load(gl.glDeleteBuffers,            "glDeleteBuffers");
    load(gl.glBindBuffer,               "glBindBuffer");
    load(gl.glBufferData,               "glBufferData");
    load(gl.glEnableVertexAttribArray,  "glEnableVertexAttribArray");
    load(gl.glDisableVertexAttribArray, "glDisableVertexAttribArray");
    load(gl.glVertexAttribPointer,      "glVertexAttribPointer");
    load(gl.glDrawArrays,               "glDrawArrays");
    load(gl.glViewport,                 "glViewport");
    load(gl.glEnable,                   "glEnable");
    load(gl.glDisable,                  "glDisable");
    load(gl.glBlendFunc,                "glBlendFunc");
    load(gl.glClearColor,               "glClearColor");
    load(gl.glClear,                    "glClear");
    load(gl.glPixelStorei,              "glPixelStorei");
    load(gl.glReadPixels,               "glReadPixels");
    load(gl.glFinish,                   "glFinish");
    load(gl.glGetError,                 "glGetError");
    load(gl.glGetIntegerv,              "glGetIntegerv");
    load(gl.glGetFloatv,                "glGetFloatv");
    load(gl.glIsEnabled,                "glIsEnabled");
    return gl;
}

inline ProbeResult ProbeNativeEs2Support(BackendMode mode,
                                         WglRuntime& rt,
                                         const hs_gpu_compat::CompatConfig& compatCfg = hs_gpu_compat::CompatConfig{}) noexcept {
    ProbeResult out{};
    if (mode == BackendMode::D3D9) {
        out.reason = "configured_d3d9";
        return out;
    }
    if (!rt.Load()) {
        out.reason = "wgl_load_failed";
        return out;
    }

    rt.enableWglExactPfd    = compatCfg.enableWglExactPfd;
    rt.chosenArbPixelFormat = 0;
    std::memset(&rt.chosenArbPfd, 0, sizeof(rt.chosenArbPfd));
    rt.chosenPfdInfo        = hs_gpu_compat::PixelFormatInfo{};

    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
    if (!hwnd) {
        out.reason = "probe_hwnd_failed";
        return out;
    }

    HDC hdc = GetDC(hwnd);
    HWND probeHwnd2 = nullptr;
    HDC  probeHdc2  = nullptr;
    HGLRC prevRc = rt.pWglGetCurrentContext();
    HDC   prevDc = rt.pWglGetCurrentDC();
    HGLRC tmpRc = nullptr;
    HGLRC es2Rc = nullptr;

    auto cleanup = [&]() {
        if (rt.pWglGetCurrentContext() != prevRc || rt.pWglGetCurrentDC() != prevDc) {
            rt.pWglMakeCurrent(prevDc, prevRc);
        }
        if (es2Rc) {
            rt.pWglDeleteContext(es2Rc);
            es2Rc = nullptr;
        }
        if (tmpRc) {
            rt.pWglDeleteContext(tmpRc);
            tmpRc = nullptr;
        }
        if (probeHdc2 && probeHwnd2) {
            ReleaseDC(probeHwnd2, probeHdc2);
            probeHdc2 = nullptr;
        }
        if (probeHwnd2) {
            DestroyWindow(probeHwnd2);
            probeHwnd2 = nullptr;
        }
        if (hdc) {
            ReleaseDC(hwnd, hdc);
            hdc = nullptr;
        }
        if (hwnd) {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    };

    if (!hdc || !EnsureWindowPixelFormat(rt, hdc)) {
        out.reason = "pixel_format_failed";
        cleanup();
        return out;
    }

    tmpRc = rt.pWglCreateContext(hdc);
    if (!tmpRc || !rt.pWglMakeCurrent(hdc, tmpRc)) {
        out.reason = "wgl_bootstrap_context_failed";
        cleanup();
        return out;
    }

    rt.ResolveWglArbProcsOnCurrent();
    const char* wglExts = rt.pWglGetExtensionsStringArb ? rt.pWglGetExtensionsStringArb(hdc) : nullptr;
    out.hasSwapControl = ContainsExtensionToken(wglExts, "WGL_EXT_swap_control") ||
                         ContainsExtensionToken(wglExts, "WGL_EXT_swap_control_tear");

    // Layer 3: Prefer exact wglChoosePixelFormatARB (with attribute readback) on a fresh probe HWND so
    // the startup canary exercises the EXACT pixel format that gameHwnd receives!
    HDC   targetHdc = hdc;
    if (compatCfg.enableWglExactPfd && rt.pWglChoosePixelFormatArb) {
        int arbPf = 0;
        PIXELFORMATDESCRIPTOR arbPfd{};
        hs_gpu_compat::PixelFormatInfo arbInfo{};
        if (SelectBestWindowPixelFormatArb(rt, hdc, arbPf, arbPfd, arbInfo)) {
            probeHwnd2 = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
            if (probeHwnd2) {
                probeHdc2 = GetDC(probeHwnd2);
                if (probeHdc2 && rt.pSetPixelFormat(probeHdc2, arbPf, &arbPfd) != FALSE) {
                    const int verifiedPf = rt.pGetPixelFormat(probeHdc2);
                    hs_gpu_compat::PixelFormatInfo readbackInfo{};
                    if (verifiedPf > 0 && QueryPixelFormatInfo(rt, probeHdc2, verifiedPf, true, readbackInfo) &&
                        readbackInfo.isValidForProbe) {
                        rt.chosenArbPixelFormat = verifiedPf;
                        rt.chosenArbPfd         = arbPfd;
                        rt.chosenPfdInfo        = readbackInfo;
                        out.pfdInfo             = readbackInfo;
                        targetHdc               = probeHdc2;
                    }
                }
            }
            if (targetHdc != probeHdc2) {
                if (probeHdc2 && probeHwnd2) { ReleaseDC(probeHwnd2, probeHdc2); probeHdc2 = nullptr; }
                if (probeHwnd2) { DestroyWindow(probeHwnd2); probeHwnd2 = nullptr; }
            }
        }
    }
    if (out.pfdInfo.pixelFormat == 0) {
        const int curPf = rt.pGetPixelFormat(hdc);
        QueryPixelFormatInfo(rt, hdc, curPf, false, out.pfdInfo);
        rt.chosenPfdInfo = out.pfdInfo;
    }

    // Use Desktop OpenGL 4.1+ Compatibility Profile (GL_ARB_ES2_compatibility) instead of
    // WGL_CONTEXT_ES2_PROFILE_BIT_EXT:
    //   - 100% native GLSL ES 1.00 (`#version 100`) + all 76 DST GLES2 entry points + S3TC/FBO/VAO=0
    //   - Steam GameOverlayRenderer64.dll explicitly rejects OpenGL ES profiles ("overlay is not supported")
    //     while still capturing mouse/keyboard input on Shift+Tab / VisitURL (Workshop / Mod Languages),
    //     causing an invisible input-lock freeze AND per-frame GL_INVALID_OPERATION (0x502) errors!
    //   - Desktop OpenGL Compatibility Profile gives 100% native Steam Overlay rendering & 0x0 GL errors.
    if (rt.pWglCreateContextAttribsArb && ContainsExtensionToken(wglExts, "WGL_ARB_create_context_profile")) {
        // v3.0 Unleashed: Prefer highest Desktop Compatibility Profile (4.6 -> 4.3 -> 4.1)
        // so modern GPUs unlock Compute Shaders, SSBOs, Image Load/Store & Instanced Rendering
        // while keeping 100% GLES2 + Steam Overlay compatibility!
        const int kVersions[][2] = { {4, 6}, {4, 3}, {4, 1} };
        for (const auto& ver : kVersions) {
            const int attribs[] = {
                kWglContextMajorVersionArb, ver[0],
                kWglContextMinorVersionArb, ver[1],
                kWglContextProfileMaskArb,  kWglContextCompatibilityProfileBitArb,
                0
            };
            es2Rc = rt.pWglCreateContextAttribsArb(targetHdc, nullptr, attribs);
            if (es2Rc) break;
        }
    }
    if (es2Rc && rt.pWglMakeCurrent(targetHdc, es2Rc)) {
        rt.pWglDeleteContext(tmpRc);
        tmpRc = nullptr;
    } else {
        if (es2Rc) {
            rt.pWglDeleteContext(es2Rc);
            es2Rc = nullptr;
        }
        if (targetHdc != hdc) {
            es2Rc = rt.pWglCreateContext(targetHdc);
            if (es2Rc && rt.pWglMakeCurrent(targetHdc, es2Rc)) {
                rt.pWglDeleteContext(tmpRc);
                tmpRc = nullptr;
            } else {
                if (es2Rc) {
                    rt.pWglDeleteContext(es2Rc);
                    es2Rc = nullptr;
                }
                // Fall back to bootstrap context on hdc
                rt.chosenArbPixelFormat = 0;
                QueryPixelFormatInfo(rt, hdc, rt.pGetPixelFormat(hdc), false, out.pfdInfo);
                rt.chosenPfdInfo = out.pfdInfo;
                es2Rc = tmpRc;
                tmpRc = nullptr;
                targetHdc = hdc;
            }
        } else {
            // Fallback to legacy wglCreateContext (which already exposes max Compatibility Profile on Windows)
            es2Rc = tmpRc;
            tmpRc = nullptr;
        }
    }

    const char* sVendor   = rt.pGlGetString(kGlVendor);
    const char* sRenderer = rt.pGlGetString(kGlRenderer);
    const char* sVersion  = rt.pGlGetString(kGlVersion);
    const char* sExts     = rt.pGlGetString(kGlExts);

    out.vendor   = sVendor   ? sVendor   : "";
    out.renderer = sRenderer ? sRenderer : "";
    out.version  = sVersion  ? sVersion  : "";

    if (ContainsAsciiInsensitive(out.vendor.c_str(), "Microsoft") ||
        ContainsAsciiInsensitive(out.renderer.c_str(), "GDI Generic")) {
        out.reason = "software_gdi_generic";
        cleanup();
        return out;
    }

    int glMaj = 0, glMin = 0;
    ParseEsVersion(out.version.c_str(), glMaj, glMin);
    const bool isEsCtx = ContainsAsciiInsensitive(out.version.c_str(), "OpenGL ES");
    if (isEsCtx) {
        out.reason = "unexpected_es_profile";
        cleanup();
        return out;
    }

    out.isNvidiaVendor = ContainsAsciiInsensitive(out.vendor.c_str(), "NVIDIA");

    if (mode == BackendMode::Auto) {
        // Auto mode enables Native OpenGL Compat on verified NVIDIA drivers AND modern AMD Radeon
        // drivers (OpenGL >= 4.5). Legacy AMD (<4.5) and Intel iGPUs default to ANGLE D3D9
        // (users may still opt in via `render_backend=opengl` in dst_hypersmooth.ini).
        const bool isModernAmd =
            (ContainsAsciiInsensitive(out.vendor.c_str(), "ATI") ||
             ContainsAsciiInsensitive(out.vendor.c_str(), "AMD")) &&
            (glMaj > 4 || (glMaj == 4 && glMin >= 5));
        if (!out.isNvidiaVendor && !isModernAmd) {
            out.reason = "auto_prefer_d3d9_for_gpu_vendor";
            cleanup();
            return out;
        }
    }

    const bool hasEs2Compat = ContainsExtensionToken(sExts, "GL_ARB_ES2_compatibility") ||
                              (glMaj > 4 || (glMaj == 4 && glMin >= 1));
    const bool hasS3tc = ContainsExtensionToken(sExts, "GL_EXT_texture_compression_s3tc") ||
                         ContainsExtensionToken(sExts, "GL_EXT_texture_compression_dxt1");
    const bool hasFbo = ContainsExtensionToken(sExts, "GL_ARB_framebuffer_object") || (glMaj >= 3);
    const bool hasPackedDs = ContainsExtensionToken(sExts, "GL_EXT_packed_depth_stencil") ||
                             ContainsExtensionToken(sExts, "GL_ARB_framebuffer_object") || (glMaj >= 3);

    if (!hasEs2Compat) {
        out.reason = "missing_arb_es2_compatibility";
        cleanup();
        return out;
    }
    if (!hasS3tc) {
        out.reason = "missing_s3tc_dxt";
        cleanup();
        return out;
    }
    if (!hasFbo) {
        out.reason = "missing_fbo";
        cleanup();
        return out;
    }
    if (!hasPackedDs) {
        out.reason = "missing_packed_depth_stencil";
        cleanup();
        return out;
    }

    out.glMajor = glMaj;
    out.glMinor = glMin;
    out.hasAniso = ContainsExtensionToken(sExts, "GL_EXT_texture_filter_anisotropic") ||
                   ContainsExtensionToken(sExts, "GL_ARB_texture_filter_anisotropic");
    out.hasMapBufferRange = ContainsExtensionToken(sExts, "GL_ARB_map_buffer_range") ||
                            (glMaj >= 3) ||
                            (rt.ResolveGlProc("glMapBufferRange") && rt.ResolveGlProc("glUnmapBuffer"));
    out.hasBufferStorage = ContainsExtensionToken(sExts, "GL_ARB_buffer_storage") ||
                           ContainsExtensionToken(sExts, "GL_EXT_buffer_storage") ||
                           (glMaj > 4 || (glMaj == 4 && glMin >= 4));
    const bool isGl43 = (glMaj > 4 || (glMaj == 4 && glMin >= 3));
    out.hasComputeShader = (isGl43 || ContainsExtensionToken(sExts, "GL_ARB_compute_shader")) &&
                           (rt.ResolveGlProc("glDispatchCompute") != nullptr) &&
                           (rt.ResolveGlProc("glMemoryBarrier") != nullptr);
    out.hasSsbo = (isGl43 || ContainsExtensionToken(sExts, "GL_ARB_shader_storage_buffer_object")) &&
                  (rt.ResolveGlProc("glBindBufferBase") != nullptr);
    out.hasImageLoadStore = (glMaj > 4 || (glMaj == 4 && glMin >= 2) ||
                             ContainsExtensionToken(sExts, "GL_ARB_shader_image_load_store")) &&
                            (rt.ResolveGlProc("glBindImageTexture") != nullptr);
    out.hasInstancedDraw = (glMaj > 3 || (glMaj == 3 && glMin >= 3) ||
                            ContainsExtensionToken(sExts, "GL_ARB_draw_instanced")) &&
                           (rt.ResolveGlProc("glDrawArraysInstanced") != nullptr);
    out.hasBaseVertex    = (glMaj > 3 || (glMaj == 3 && glMin >= 2) ||
                            ContainsExtensionToken(sExts, "GL_ARB_draw_elements_base_vertex")) &&
                           (rt.ResolveGlProc("glDrawElementsBaseVertex") != nullptr);
    out.hasBindlessTexture = ContainsExtensionToken(sExts, "GL_ARB_bindless_texture") &&
                             (rt.ResolveGlProc("glGetTextureHandleARB") != nullptr) &&
                             (rt.ResolveGlProc("glMakeTextureHandleResidentARB") != nullptr) &&
                             (rt.ResolveGlProc("glMakeTextureHandleNonResidentARB") != nullptr);
    // Shader SpriteBatcher samples dynamically non-uniform sampler2D(HS_V_BTEX) without
    // GL_EXT_nonuniform_qualifier; only safe on NVIDIA drivers, whereas AMD GCN/RDNA
    // requires nonuniformEXT to avoid SGPR undefined texture reads (black sprites/buildings).
    out.hasNonUniformBindless = out.hasBindlessTexture && out.isNvidiaVendor;

    int okCount = 0;
    const char* firstMissing = nullptr;
    for (const auto& imp : kDstGles2Imports) {
        if (rt.ResolveGlProc(imp.name)) {
            ++okCount;
        } else if (!firstMissing) {
            firstMissing = imp.name;
        }
    }
    out.resolvedProcs = okCount;
    if (okCount != 76) {
        out.reason = std::string("missing_gles2_proc:") + (firstMissing ? firstMissing : "?");
        cleanup();
        return out;
    }

    // Layer 4: Real-Path Startup ColourCube Canary (verifies actual PFD + shaders + LUTs + FBO + blend)
    const hs_gpu_compat::CanaryGlOps cGl = BuildCanaryGlOps(rt);
    const bool canaryOk = hs_gpu_compat::RunLiveColourCubeCanary(
        cGl, out.glMajor, out.glMinor, out.hasAniso, compatCfg,
        out.vendor, out.renderer, out.version, out.pfdInfo,
        false, out.canary);
    out.ccTexelFetchReady = out.canary.texelFetchReady;
    if (!canaryOk && mode == BackendMode::Auto) {
        out.supported = false;
        out.reason = !out.canary.fallbackReason.empty()
            ? out.canary.fallbackReason
            : (std::string("canary_failed:") + out.canary.stage);
        cleanup();
        return out;
    }

    out.supported = true;
    out.reason = canaryOk ? "ok" : (std::string("forced_opengl_canary_warn:") + out.canary.stage);
    cleanup();
    return out;
}

inline bool CreateGameEs2Context(WglRuntime& rt, HWND hwnd, HDC& inOutHdc, HGLRC& outRc, HGLRC shareRc = nullptr) noexcept {
    outRc = nullptr;
    if (!rt.Load() || !hwnd) return false;
    if (!inOutHdc) {
        inOutHdc = GetDC(hwnd);
    }
    if (!inOutHdc || !EnsureWindowPixelFormat(rt, inOutHdc)) return false;

    const HGLRC prevRc = rt.pWglGetCurrentContext();
    const HDC   prevDc = rt.pWglGetCurrentDC();

    if (!rt.pWglCreateContextAttribsArb) {
        HGLRC tmpRc = rt.pWglCreateContext(inOutHdc);
        if (!tmpRc || !rt.pWglMakeCurrent(inOutHdc, tmpRc)) {
            if (tmpRc) rt.pWglDeleteContext(tmpRc);
            rt.pWglMakeCurrent(prevDc, prevRc);
            return false;
        }
        rt.ResolveWglArbProcsOnCurrent();
        rt.pWglMakeCurrent(prevDc, prevRc);
        rt.pWglDeleteContext(tmpRc);
    }

    if (rt.pWglCreateContextAttribsArb) {
        const int kVersions[][2] = { {4, 6}, {4, 3}, {4, 1} };
        for (const auto& ver : kVersions) {
            const int attribs[] = {
                kWglContextMajorVersionArb, ver[0],
                kWglContextMinorVersionArb, ver[1],
                kWglContextProfileMaskArb,  kWglContextCompatibilityProfileBitArb,
                0
            };
            outRc = rt.pWglCreateContextAttribsArb(inOutHdc, shareRc, attribs);
            if (outRc) break;
        }
    }
    if (!outRc) {
        // Fallback to legacy wglCreateContext (creates max Desktop Compatibility Profile)
        outRc = rt.pWglCreateContext(inOutHdc);
    }
    return outRc != nullptr;
}

inline int ResolveLiveGlesTable(WglRuntime& rt, void* outProcs[76], OptionalGlProcs& outOpt) noexcept {
    int count = 0;
    for (size_t i = 0; i < 76; ++i) {
        outProcs[i] = rt.ResolveGlProc(kDstGles2Imports[i].name);
        if (outProcs[i]) ++count;
    }
    rt.ResolveWglArbProcsOnCurrent();
    outOpt.glTexParameterf    = rt.ResolveGlProc("glTexParameterf");
    outOpt.glGetFloatv        = rt.ResolveGlProc("glGetFloatv");
    outOpt.glBufferSubData    = rt.ResolveGlProc("glBufferSubData");
    outOpt.glMapBufferRange   = rt.ResolveGlProc("glMapBufferRange");
    outOpt.glUnmapBuffer      = rt.ResolveGlProc("glUnmapBuffer");
    outOpt.glBufferStorage    = rt.ResolveGlProc("glBufferStorage");
    outOpt.glBufferStorageExt = rt.ResolveGlProc("glBufferStorageEXT");
    outOpt.glFenceSync            = rt.ResolveGlProc("glFenceSync");
    outOpt.glClientWaitSync       = rt.ResolveGlProc("glClientWaitSync");
    outOpt.glDeleteSync           = rt.ResolveGlProc("glDeleteSync");
    outOpt.glFinish               = rt.ResolveGlProc("glFinish");
    outOpt.glGetBufferParameteriv  = rt.ResolveGlProc("glGetBufferParameteriv");
    outOpt.glGetBufferSubData      = rt.ResolveGlProc("glGetBufferSubData");
    outOpt.glBlendFuncSeparate     = rt.ResolveGlProc("glBlendFuncSeparate");
    outOpt.glBlendEquationSeparate = rt.ResolveGlProc("glBlendEquationSeparate");
    outOpt.glBlitFramebuffer       = rt.ResolveGlProc("glBlitFramebuffer");
    outOpt.glDispatchCompute       = rt.ResolveGlProc("glDispatchCompute");
    outOpt.glMemoryBarrier         = rt.ResolveGlProc("glMemoryBarrier");
    outOpt.glBindBufferBase        = rt.ResolveGlProc("glBindBufferBase");
    outOpt.glBindImageTexture       = rt.ResolveGlProc("glBindImageTexture");
    outOpt.glDrawArraysInstanced    = rt.ResolveGlProc("glDrawArraysInstanced");
    outOpt.glDrawElementsInstanced  = rt.ResolveGlProc("glDrawElementsInstanced");
    outOpt.glDrawElementsBaseVertex = rt.ResolveGlProc("glDrawElementsBaseVertex");
    outOpt.glVertexAttribDivisor    = rt.ResolveGlProc("glVertexAttribDivisor");
    outOpt.glGetTextureHandleARB             = rt.ResolveGlProc("glGetTextureHandleARB");
    outOpt.glMakeTextureHandleResidentARB    = rt.ResolveGlProc("glMakeTextureHandleResidentARB");
    outOpt.glMakeTextureHandleNonResidentARB = rt.ResolveGlProc("glMakeTextureHandleNonResidentARB");
    outOpt.glTexStorage2D          = rt.ResolveGlProc("glTexStorage2D");
    outOpt.glIsEnabled             = rt.ResolveGlProc("glIsEnabled");
    outOpt.glGenQueries            = rt.ResolveGlProc("glGenQueries");
    outOpt.glDeleteQueries         = rt.ResolveGlProc("glDeleteQueries");
    outOpt.glBeginQuery            = rt.ResolveGlProc("glBeginQuery");
    outOpt.glEndQuery              = rt.ResolveGlProc("glEndQuery");
    outOpt.glGetQueryObjectiv      = rt.ResolveGlProc("glGetQueryObjectiv");
    outOpt.glGetQueryObjectui64v   = rt.ResolveGlProc("glGetQueryObjectui64v");
    outOpt.glUniform4f             = rt.ResolveGlProc("glUniform4f");
    return count;
}

inline bool GetEglConfigAttribValue(int32_t attr, int32_t* outVal) noexcept {
    if (!outVal) return false;
    switch (attr) {
        case 0x3020: *outVal = 32;     return true; // EGL_BUFFER_SIZE
        case 0x3021: *outVal = 8;      return true; // EGL_ALPHA_SIZE
        case 0x3022: *outVal = 8;      return true; // EGL_BLUE_SIZE
        case 0x3023: *outVal = 8;      return true; // EGL_GREEN_SIZE
        case 0x3024: *outVal = 8;      return true; // EGL_RED_SIZE
        case 0x3025: *outVal = 24;     return true; // EGL_DEPTH_SIZE
        case 0x3026: *outVal = 8;      return true; // EGL_STENCIL_SIZE
        case 0x3027: *outVal = 0x3038; return true; // EGL_CONFIG_CAVEAT = EGL_NONE
        case 0x3028: *outVal = 1;      return true; // EGL_CONFIG_ID
        case 0x3029: *outVal = 0;      return true; // EGL_LEVEL
        case 0x302A: *outVal = 16384;  return true; // EGL_MAX_PBUFFER_HEIGHT
        case 0x302B: *outVal = 268435456; return true; // EGL_MAX_PBUFFER_PIXELS
        case 0x302C: *outVal = 16384;  return true; // EGL_MAX_PBUFFER_WIDTH
        case 0x302D: *outVal = 1;      return true; // EGL_NATIVE_RENDERABLE
        case 0x302E: *outVal = 0;      return true; // EGL_NATIVE_VISUAL_ID
        case 0x302F: *outVal = 0;      return true; // EGL_NATIVE_VISUAL_TYPE
        case 0x3031: *outVal = 0;      return true; // EGL_SAMPLE_BUFFERS
        case 0x3032: *outVal = 0;      return true; // EGL_SAMPLES
        case 0x3033: *outVal = 0x0004; return true; // EGL_SURFACE_TYPE = EGL_WINDOW_BIT
        case 0x3034: *outVal = 0x3038; return true; // EGL_TRANSPARENT_TYPE = EGL_NONE
        case 0x3039: *outVal = 1;      return true; // EGL_BIND_TO_TEXTURE_RGB
        case 0x303A: *outVal = 1;      return true; // EGL_BIND_TO_TEXTURE_RGBA
        case 0x303B: *outVal = 0;      return true; // EGL_MIN_SWAP_INTERVAL
        case 0x303C: *outVal = 4;      return true; // EGL_MAX_SWAP_INTERVAL
        case 0x3040: *outVal = 0x0004; return true; // EGL_RENDERABLE_TYPE = EGL_OPENGL_ES2_BIT
        case 0x3042: *outVal = 0x0004; return true; // EGL_CONFORMANT = EGL_OPENGL_ES2_BIT
        default:     return false;
    }
}

} // namespace hs_native_gl
