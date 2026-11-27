#pragma once

#include <windows.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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
inline constexpr int32_t kWglContextMajorVersionArb           = 0x2091;
inline constexpr int32_t kWglContextMinorVersionArb           = 0x2092;
inline constexpr int32_t kWglContextProfileMaskArb            = 0x9126;
inline constexpr int32_t kWglContextCompatibilityProfileBitArb = 0x0002;
inline constexpr int32_t kWglContextEs2ProfileBitExt          = 0x0004;

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

using ChoosePixelFormat_t        = int (WINAPI*)(HDC, const PIXELFORMATDESCRIPTOR*);
using SetPixelFormat_t           = BOOL (WINAPI*)(HDC, int, const PIXELFORMATDESCRIPTOR*);
using GetPixelFormat_t           = int (WINAPI*)(HDC);
using SwapBuffers_t              = BOOL (WINAPI*)(HDC);

using WglCreateContext_t         = HGLRC (WINAPI*)(HDC);
using WglDeleteContext_t         = BOOL (WINAPI*)(HGLRC);
using WglMakeCurrent_t           = BOOL (WINAPI*)(HDC, HGLRC);
using WglGetCurrentContext_t     = HGLRC (WINAPI*)();
using WglGetCurrentDC_t          = HDC (WINAPI*)();
using WglGetProcAddress_t        = PROC (WINAPI*)(LPCSTR);
using WglGetExtensionsStringArb_t = const char* (WINAPI*)(HDC);
using WglCreateContextAttribsArb_t = HGLRC (WINAPI*)(HDC, HGLRC, const int*);
using WglSwapIntervalExt_t       = BOOL (WINAPI*)(int);

using GlGetString_t              = const char* (WINAPI*)(uint32_t);
using GlGetIntegerv_t            = void (WINAPI*)(uint32_t, int32_t*);
using GlGetError_t               = uint32_t (WINAPI*)();
using GlGetFloatv_t              = void (WINAPI*)(uint32_t, float*);
using GlTexParameterf_t          = void (WINAPI*)(uint32_t, uint32_t, float);
using GlFinish_t                 = void (WINAPI*)();

struct WglRuntime {
    HMODULE                 hOpengl32 = nullptr;
    HMODULE                 hGdi32    = nullptr;

    ChoosePixelFormat_t     pChoosePixelFormat = nullptr;
    SetPixelFormat_t        pSetPixelFormat    = nullptr;
    GetPixelFormat_t        pGetPixelFormat    = nullptr;
    SwapBuffers_t           pSwapBuffers       = nullptr;

    WglCreateContext_t      pWglCreateContext     = nullptr;
    WglDeleteContext_t      pWglDeleteContext     = nullptr;
    WglMakeCurrent_t        pWglMakeCurrent       = nullptr;
    WglGetCurrentContext_t  pWglGetCurrentContext = nullptr;
    WglGetCurrentDC_t       pWglGetCurrentDC      = nullptr;
    WglGetProcAddress_t     pWglGetProcAddress    = nullptr;

    WglGetExtensionsStringArb_t  pWglGetExtensionsStringArb  = nullptr;
    WglCreateContextAttribsArb_t pWglCreateContextAttribsArb = nullptr;
    WglSwapIntervalExt_t         pWglSwapIntervalExt         = nullptr;

    GlGetString_t           pGlGetString     = nullptr;
    GlGetIntegerv_t         pGlGetIntegerv   = nullptr;
    GlGetError_t            pGlGetError      = nullptr;
    GlGetFloatv_t           pGlGetFloatv     = nullptr;
    GlTexParameterf_t       pGlTexParameterf = nullptr;
    GlFinish_t              pGlFinish        = nullptr;

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

        pChoosePixelFormat = reinterpret_cast<ChoosePixelFormat_t>(GetProcAddress(hGdi32, "ChoosePixelFormat"));
        pSetPixelFormat    = reinterpret_cast<SetPixelFormat_t>(GetProcAddress(hGdi32, "SetPixelFormat"));
        pGetPixelFormat    = reinterpret_cast<GetPixelFormat_t>(GetProcAddress(hGdi32, "GetPixelFormat"));
        pSwapBuffers       = reinterpret_cast<SwapBuffers_t>(GetProcAddress(hGdi32, "SwapBuffers"));

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
    int         glMajor           = 0;
    int         glMinor           = 0;
    int         resolvedProcs     = 0;
    std::string vendor;
    std::string renderer;
    std::string version;
    std::string reason;
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

inline bool EnsureWindowPixelFormat(WglRuntime& rt, HDC hdc) noexcept {
    if (!hdc || !rt.pGetPixelFormat || !rt.pChoosePixelFormat || !rt.pSetPixelFormat) return false;
    if (rt.pGetPixelFormat(hdc) != 0) return true;

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

inline ProbeResult ProbeNativeEs2Support(BackendMode mode, WglRuntime& rt) noexcept {
    ProbeResult out{};
    if (mode == BackendMode::D3D9) {
        out.reason = "configured_d3d9";
        return out;
    }
    if (!rt.Load()) {
        out.reason = "wgl_load_failed";
        return out;
    }

    HWND hwnd = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
    if (!hwnd) {
        out.reason = "probe_hwnd_failed";
        return out;
    }

    HDC hdc = GetDC(hwnd);
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
            es2Rc = rt.pWglCreateContextAttribsArb(hdc, nullptr, attribs);
            if (es2Rc) break;
        }
    }
    if (es2Rc && rt.pWglMakeCurrent(hdc, es2Rc)) {
        rt.pWglDeleteContext(tmpRc);
        tmpRc = nullptr;
    } else {
        if (es2Rc) {
            rt.pWglDeleteContext(es2Rc);
            es2Rc = nullptr;
        }
        // Fallback to legacy wglCreateContext (which already exposes max Compatibility Profile on Windows)
        es2Rc = tmpRc;
        tmpRc = nullptr;
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

    if (mode == BackendMode::Auto) {
        // Auto mode enables Native OpenGL Compat on verified NVIDIA drivers AND modern AMD Radeon
        // drivers (OpenGL >= 4.5). Legacy AMD (<4.5) and Intel iGPUs default to ANGLE D3D9
        // (users may still opt in via `render_backend=opengl` in dst_hypersmooth.ini).
        const bool isNvidia = ContainsAsciiInsensitive(out.vendor.c_str(), "NVIDIA");
        const bool isModernAmd =
            (ContainsAsciiInsensitive(out.vendor.c_str(), "ATI") ||
             ContainsAsciiInsensitive(out.vendor.c_str(), "AMD")) &&
            (glMaj > 4 || (glMaj == 4 && glMin >= 5));
        if (!isNvidia && !isModernAmd) {
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

    out.supported = true;
    out.reason = "ok";
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
