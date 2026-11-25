#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_5.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "native_gl_bridge.h"

namespace hs_dxgi_flip {

enum class FlipMode : int {
    Off  = 0,
    On   = 1,
    Auto = 2,
};

inline FlipMode ParseFlipMode(const wchar_t* s) noexcept {
    if (!s || !*s) return FlipMode::Auto;
    wchar_t buf[32]{};
    size_t j = 0;
    for (size_t i = 0; s[i] && j < 31; ++i) {
        wchar_t c = s[i];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') continue;
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        buf[j++] = c;
    }
    buf[j] = L'\0';
    if (wcscmp(buf, L"0") == 0 || wcscmp(buf, L"off") == 0 || wcscmp(buf, L"false") == 0 || wcscmp(buf, L"no") == 0) {
        return FlipMode::Off;
    }
    if (wcscmp(buf, L"1") == 0 || wcscmp(buf, L"on") == 0 || wcscmp(buf, L"true") == 0 || wcscmp(buf, L"yes") == 0) {
        return FlipMode::On;
    }
    return FlipMode::Auto;
}

inline const char* FlipModeName(FlipMode m) noexcept {
    switch (m) {
        case FlipMode::Off: return "off";
        case FlipMode::On:  return "on";
        default:            return "auto";
    }
}

// Explicit COM IIDs (zero dependency on -ldxguid / -luuid).
inline constexpr GUID kIidD3d11Texture2D = {
    0x6f15aaf2, 0xd208, 0x4e89, {0x9a, 0xb4, 0x48, 0x95, 0x35, 0xd3, 0x4f, 0x9c}
};
inline constexpr GUID kIidDxgiDevice1 = {
    0x77db970f, 0x6276, 0x48ba, {0xba, 0x28, 0x07, 0x01, 0x43, 0xb4, 0x39, 0x2c}
};
inline constexpr GUID kIidDxgiAdapter = {
    0x2411e7e1, 0x12ac, 0x4ccf, {0xbd, 0x14, 0x97, 0x98, 0xe8, 0x53, 0x4d, 0xc0}
};
inline constexpr GUID kIidDxgiFactory2 = {
    0x50c83a1c, 0xe072, 0x4c48, {0x87, 0xb0, 0x36, 0x30, 0xfa, 0x36, 0xa6, 0xd0}
};
inline constexpr GUID kIidDxgiFactory5 = {
    0x7632e1f5, 0xee65, 0x4dca, {0x87, 0xfd, 0x84, 0xcd, 0x75, 0xf8, 0x83, 0x8d}
};
inline constexpr GUID kIidDxgiSwapChain1 = {
    0x790a45f7, 0x0d42, 0x4876, {0x98, 0x3a, 0x0a, 0x55, 0xcf, 0xe6, 0xf4, 0xaa}
};

// WGL_NV_DX_interop2 constants & entry points.
inline constexpr uint32_t kWglAccessWriteDiscardNv = 0x0002u;

inline constexpr uint32_t kGlTexture2D                 = 0x0DE1u;
inline constexpr uint32_t kGlTextureBinding2D          = 0x8069u;
inline constexpr uint32_t kGlTextureMinFilter          = 0x2801u;
inline constexpr uint32_t kGlTextureMagFilter          = 0x2800u;
inline constexpr uint32_t kGlTextureWrapS              = 0x2802u;
inline constexpr uint32_t kGlTextureWrapT              = 0x2803u;
inline constexpr uint32_t kGlNearest                   = 0x2600u;
inline constexpr uint32_t kGlClampToEdge               = 0x812Fu;
inline constexpr uint32_t kGlRgba                      = 0x1908u;
inline constexpr uint32_t kGlRgba8                     = 0x8058u;
inline constexpr uint32_t kGlUnsignedByte              = 0x1401u;
inline constexpr uint32_t kGlDepth24Stencil8           = 0x88F0u;
inline constexpr uint32_t kGlColorBufferBit            = 0x00004000u;
inline constexpr uint32_t kGlScissorTest               = 0x0C11u;
inline constexpr uint32_t kGlFramebuffer               = 0x8D40u;
inline constexpr uint32_t kGlReadFramebuffer           = 0x8CA8u;
inline constexpr uint32_t kGlDrawFramebuffer           = 0x8CA9u;
inline constexpr uint32_t kGlDrawFramebufferBinding    = 0x8CA6u;
inline constexpr uint32_t kGlReadFramebufferBinding    = 0x8CAAu;
inline constexpr uint32_t kGlRenderbuffer              = 0x8D41u;
inline constexpr uint32_t kGlRenderbufferBinding       = 0x8CA7u;
inline constexpr uint32_t kGlColorAttachment0          = 0x8CE0u;
inline constexpr uint32_t kGlDepthStencilAttachment    = 0x821Au;
inline constexpr uint32_t kGlFramebufferComplete       = 0x8CD5u;
inline constexpr uint32_t kGlViewport                  = 0x0BA2u;

using PfnD3D11CreateDevice_t = HRESULT (WINAPI*)(
    IDXGIAdapter*, D3D_DRIVER_TYPE, HMODULE, UINT,
    const D3D_FEATURE_LEVEL*, UINT, UINT,
    ID3D11Device**, D3D_FEATURE_LEVEL*, ID3D11DeviceContext**);

using PfnWglDXOpenDeviceNV_t       = HANDLE (WINAPI*)(void* dxDevice);
using PfnWglDXCloseDeviceNV_t      = BOOL   (WINAPI*)(HANDLE hDevice);
using PfnWglDXRegisterObjectNV_t   = HANDLE (WINAPI*)(HANDLE hDevice, void* dxObject, uint32_t name, uint32_t type, uint32_t access);
using PfnWglDXUnregisterObjectNV_t = BOOL   (WINAPI*)(HANDLE hDevice, HANDLE hObject);
using PfnWglDXLockObjectsNV_t      = BOOL   (WINAPI*)(HANDLE hDevice, int32_t count, HANDLE* hObjects);
using PfnWglDXUnlockObjectsNV_t    = BOOL   (WINAPI*)(HANDLE hDevice, int32_t count, HANDLE* hObjects);

struct RawGlProcs {
    void     (WINAPI* glGenFramebuffers)(int32_t, uint32_t*)                                                                  = nullptr;
    void     (WINAPI* glDeleteFramebuffers)(int32_t, const uint32_t*)                                                         = nullptr;
    void     (WINAPI* glBindFramebuffer)(uint32_t, uint32_t)                                                                  = nullptr;
    void     (WINAPI* glFramebufferTexture2D)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t)                                = nullptr;
    void     (WINAPI* glFramebufferRenderbuffer)(uint32_t, uint32_t, uint32_t, uint32_t)                                      = nullptr;
    uint32_t (WINAPI* glCheckFramebufferStatus)(uint32_t)                                                                     = nullptr;
    void     (WINAPI* glBlitFramebuffer)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t) = nullptr;

    void     (WINAPI* glGenTextures)(int32_t, uint32_t*)                                                                      = nullptr;
    void     (WINAPI* glDeleteTextures)(int32_t, const uint32_t*)                                                             = nullptr;
    void     (WINAPI* glBindTexture)(uint32_t, uint32_t)                                                                      = nullptr;
    void     (WINAPI* glTexImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*)   = nullptr;
    void     (WINAPI* glTexParameteri)(uint32_t, uint32_t, int32_t)                                                           = nullptr;

    void     (WINAPI* glGenRenderbuffers)(int32_t, uint32_t*)                                                                 = nullptr;
    void     (WINAPI* glDeleteRenderbuffers)(int32_t, const uint32_t*)                                                        = nullptr;
    void     (WINAPI* glBindRenderbuffer)(uint32_t, uint32_t)                                                                 = nullptr;
    void     (WINAPI* glRenderbufferStorage)(uint32_t, uint32_t, int32_t, int32_t)                                            = nullptr;

    void     (WINAPI* glGetIntegerv)(uint32_t, int32_t*)                                                                      = nullptr;
    void     (WINAPI* glViewport)(int32_t, int32_t, int32_t, int32_t)                                                         = nullptr;
    uint8_t  (WINAPI* glIsEnabled)(uint32_t)                                                                                  = nullptr;
    void     (WINAPI* glEnable)(uint32_t)                                                                                     = nullptr;
    void     (WINAPI* glDisable)(uint32_t)                                                                                    = nullptr;
    uint32_t (WINAPI* glGetError)()                                                                                           = nullptr;
    void     (WINAPI* glFlush)()                                                                                              = nullptr;

    PfnWglDXOpenDeviceNV_t       wglDXOpenDeviceNV       = nullptr;
    PfnWglDXCloseDeviceNV_t      wglDXCloseDeviceNV      = nullptr;
    PfnWglDXRegisterObjectNV_t   wglDXRegisterObjectNV   = nullptr;
    PfnWglDXUnregisterObjectNV_t wglDXUnregisterObjectNV = nullptr;
    PfnWglDXLockObjectsNV_t      wglDXLockObjectsNV      = nullptr;
    PfnWglDXUnlockObjectsNV_t    wglDXUnlockObjectsNV    = nullptr;

    bool Resolve(const hs_native_gl::WglRuntime& rt) noexcept {
        auto load = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(rt.ResolveGlProc(name));
        };
        load(glGenFramebuffers,         "glGenFramebuffers");
        load(glDeleteFramebuffers,      "glDeleteFramebuffers");
        load(glBindFramebuffer,         "glBindFramebuffer");
        load(glFramebufferTexture2D,    "glFramebufferTexture2D");
        load(glFramebufferRenderbuffer, "glFramebufferRenderbuffer");
        load(glCheckFramebufferStatus,  "glCheckFramebufferStatus");
        load(glBlitFramebuffer,         "glBlitFramebuffer");

        load(glGenTextures,             "glGenTextures");
        load(glDeleteTextures,          "glDeleteTextures");
        load(glBindTexture,             "glBindTexture");
        load(glTexImage2D,              "glTexImage2D");
        load(glTexParameteri,           "glTexParameteri");

        load(glGenRenderbuffers,        "glGenRenderbuffers");
        load(glDeleteRenderbuffers,     "glDeleteRenderbuffers");
        load(glBindRenderbuffer,        "glBindRenderbuffer");
        load(glRenderbufferStorage,     "glRenderbufferStorage");

        load(glGetIntegerv,             "glGetIntegerv");
        load(glViewport,                "glViewport");
        load(glIsEnabled,               "glIsEnabled");
        load(glEnable,                  "glEnable");
        load(glDisable,                 "glDisable");
        load(glGetError,                "glGetError");
        load(glFlush,                   "glFlush");

        load(wglDXOpenDeviceNV,         "wglDXOpenDeviceNV");
        load(wglDXCloseDeviceNV,        "wglDXCloseDeviceNV");
        load(wglDXRegisterObjectNV,     "wglDXRegisterObjectNV");
        load(wglDXUnregisterObjectNV,   "wglDXUnregisterObjectNV");
        load(wglDXLockObjectsNV,        "wglDXLockObjectsNV");
        load(wglDXUnlockObjectsNV,      "wglDXUnlockObjectsNV");

        return glGenFramebuffers && glDeleteFramebuffers && glBindFramebuffer &&
               glFramebufferTexture2D && glFramebufferRenderbuffer &&
               glCheckFramebufferStatus && glBlitFramebuffer &&
               glGenTextures && glDeleteTextures && glBindTexture &&
               glTexImage2D && glTexParameteri &&
               glGenRenderbuffers && glDeleteRenderbuffers &&
               glBindRenderbuffer && glRenderbufferStorage &&
               glGetIntegerv && glIsEnabled && glEnable && glDisable &&
               wglDXOpenDeviceNV && wglDXCloseDeviceNV &&
               wglDXRegisterObjectNV && wglDXUnregisterObjectNV &&
               wglDXLockObjectsNV && wglDXUnlockObjectsNV;
    }
};

struct DxgiProbeResult {
    bool        supported    = false;
    bool        allowTearing = false;
    std::string reason;
};

inline PfnD3D11CreateDevice_t LoadD3D11CreateDevice(HMODULE& outMod) noexcept {
    if (!outMod) {
        outMod = LoadLibraryW(L"d3d11.dll");
    }
    if (!outMod) return nullptr;
    return reinterpret_cast<PfnD3D11CreateDevice_t>(GetProcAddress(outMod, "D3D11CreateDevice"));
}

// Live-verifies WGL_NV_DX_interop2 + D3D11 interop lock/unlock on a tiny 16x16 texture
// BEFORE gameHwnd is ever touched. Requires a valid WGL context to be current on the calling thread,
// OR creates a temporary 1x1 WGL probe context if none is current.
inline DxgiProbeResult ProbeDxgiFlipSupport(FlipMode mode, hs_native_gl::WglRuntime& rt) noexcept {
    DxgiProbeResult out{};
    if (mode == FlipMode::Off) {
        out.reason = "configured_off";
        return out;
    }
    if (!rt.Load()) {
        out.reason = "wgl_load_failed";
        return out;
    }

    HMODULE hD3d11 = nullptr;
    auto fnCreateDev = LoadD3D11CreateDevice(hD3d11);
    if (!fnCreateDev) {
        out.reason = "d3d11_dll_missing";
        return out;
    }

    HWND  tmpHwnd = nullptr;
    HDC   tmpHdc  = nullptr;
    HGLRC tmpRc   = nullptr;
    HGLRC prevRc  = rt.pWglGetCurrentContext();
    HDC   prevDc  = rt.pWglGetCurrentDC();
    const bool needTmpCtx = (prevRc == nullptr || prevDc == nullptr);

    ID3D11Device*        dev   = nullptr;
    ID3D11DeviceContext* ctx   = nullptr;
    ID3D11Texture2D*     tex   = nullptr;
    HANDLE               hDev  = nullptr;
    HANDLE               hObj  = nullptr;
    uint32_t             rbo   = 0u;
    RawGlProcs           gl{};

    auto cleanup = [&]() {
        if (hObj && hDev && gl.wglDXUnregisterObjectNV) {
            gl.wglDXUnregisterObjectNV(hDev, hObj);
            hObj = nullptr;
        }
        if (rbo && gl.glDeleteRenderbuffers) {
            gl.glDeleteRenderbuffers(1, &rbo);
            rbo = 0u;
        }
        if (hDev && gl.wglDXCloseDeviceNV) {
            gl.wglDXCloseDeviceNV(hDev);
            hDev = nullptr;
        }
        if (tex) { tex->Release(); tex = nullptr; }
        if (ctx) { ctx->Release(); ctx = nullptr; }
        if (dev) { dev->Release(); dev = nullptr; }
        if (needTmpCtx) {
            rt.pWglMakeCurrent(prevDc, prevRc);
            if (tmpRc) { rt.pWglDeleteContext(tmpRc); tmpRc = nullptr; }
            if (tmpHdc && tmpHwnd) { ReleaseDC(tmpHwnd, tmpHdc); tmpHdc = nullptr; }
            if (tmpHwnd) { DestroyWindow(tmpHwnd); tmpHwnd = nullptr; }
        }
    };

    HDC activeDc = prevDc;
    if (needTmpCtx) {
        tmpHwnd = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
        if (!tmpHwnd) { out.reason = "probe_hwnd_failed"; cleanup(); return out; }
        tmpHdc = GetDC(tmpHwnd);
        if (!tmpHdc || !hs_native_gl::EnsureWindowPixelFormat(rt, tmpHdc)) {
            out.reason = "probe_pfd_failed";
            cleanup();
            return out;
        }
        tmpRc = rt.pWglCreateContext(tmpHdc);
        if (!tmpRc || !rt.pWglMakeCurrent(tmpHdc, tmpRc)) {
            out.reason = "probe_wgl_ctx_failed";
            cleanup();
            return out;
        }
        rt.ResolveWglArbProcsOnCurrent();
        activeDc = tmpHdc;
    }

    const char* wglExts = (rt.pWglGetExtensionsStringArb && activeDc) ? rt.pWglGetExtensionsStringArb(activeDc) : nullptr;
    if (!hs_native_gl::ContainsExtensionToken(wglExts, "WGL_NV_DX_interop2")) {
        out.reason = "no_wgl_nv_dx_interop2";
        cleanup();
        return out;
    }

    if (!gl.Resolve(rt)) {
        out.reason = "missing_interop_or_fbo_procs";
        cleanup();
        return out;
    }

    const D3D_FEATURE_LEVEL reqLevels[2] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL gotLevel = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = fnCreateDev(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                             D3D11_CREATE_DEVICE_BGRA_SUPPORT, reqLevels, 2, D3D11_SDK_VERSION,
                             &dev, &gotLevel, &ctx);
    if (FAILED(hr) || !dev || !ctx) {
        hr = fnCreateDev(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                         0, reqLevels + 1, 1, D3D11_SDK_VERSION,
                         &dev, &gotLevel, &ctx);
    }
    if (FAILED(hr) || !dev || !ctx) {
        out.reason = "d3d11_create_device_failed";
        cleanup();
        return out;
    }

    // Check DXGI_FEATURE_PRESENT_ALLOW_TEARING via IDXGIFactory5.
    IDXGIDevice1* dxgiDev = nullptr;
    if (SUCCEEDED(dev->QueryInterface(kIidDxgiDevice1, reinterpret_cast<void**>(&dxgiDev))) && dxgiDev) {
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(dxgiDev->GetParent(kIidDxgiAdapter, reinterpret_cast<void**>(&adapter))) && adapter) {
            IDXGIFactory5* fac5 = nullptr;
            if (SUCCEEDED(adapter->GetParent(kIidDxgiFactory5, reinterpret_cast<void**>(&fac5))) && fac5) {
                BOOL tearing = FALSE;
                if (SUCCEEDED(fac5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing) {
                    out.allowTearing = true;
                }
                fac5->Release();
            }
            adapter->Release();
        }
        dxgiDev->Release();
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width              = 16;
    td.Height             = 16;
    td.MipLevels          = 1;
    td.ArraySize          = 1;
    td.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count   = 1;
    td.SampleDesc.Quality = 0;
    td.Usage              = D3D11_USAGE_DEFAULT;
    td.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(dev->CreateTexture2D(&td, nullptr, &tex)) || !tex) {
        out.reason = "d3d11_probe_tex_failed";
        cleanup();
        return out;
    }

    hDev = gl.wglDXOpenDeviceNV(dev);
    if (!hDev) {
        out.reason = "wglDXOpenDeviceNV_failed";
        cleanup();
        return out;
    }

    gl.glGenRenderbuffers(1, &rbo);
    if (!rbo) {
        out.reason = "glGenRenderbuffers_failed";
        cleanup();
        return out;
    }

    hObj = gl.wglDXRegisterObjectNV(hDev, tex, rbo, kGlRenderbuffer, kWglAccessWriteDiscardNv);
    if (!hObj) {
        out.reason = "wglDXRegisterObjectNV_failed";
        cleanup();
        return out;
    }

    if (!gl.wglDXLockObjectsNV(hDev, 1, &hObj)) {
        out.reason = "wglDXLockObjectsNV_failed";
        cleanup();
        return out;
    }
    gl.wglDXUnlockObjectsNV(hDev, 1, &hObj);

    out.supported = true;
    out.reason = "ok";
    cleanup();
    return out;
}

class DxgiFlipPresenter {
public:
    DxgiFlipPresenter() = default;
    ~DxgiFlipPresenter() {
        Shutdown(false);
    }

    DxgiFlipPresenter(const DxgiFlipPresenter&) = delete;
    DxgiFlipPresenter& operator=(const DxgiFlipPresenter&) = delete;

    // Stage 1: Create hidden 1x1 dummy WGL surface + D3D11 device + DXGI Flip-Model swapchain
    // on real gameHwnd WITHOUT ever calling SetPixelFormat on gameHwnd.
    // Fail-closed: if anything fails, cleans up completely and leaves gameHwnd untouched.
    bool InitSwapChain(hs_native_gl::WglRuntime& rt, HWND gameHwnd, std::string& outReason) noexcept {
        Shutdown(false);
        outReason.clear();
        if (!gameHwnd || !rt.Load()) {
            outReason = "invalid_hwnd_or_wgl";
            return false;
        }

        auto fnCreateDev = LoadD3D11CreateDevice(hD3d11_);
        if (!fnCreateDev) {
            outReason = "d3d11_dll_missing";
            return false;
        }

        dummyHwnd_ = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
        if (!dummyHwnd_) {
            outReason = "dummy_hwnd_failed";
            Shutdown(false);
            return false;
        }
        dummyHdc_ = GetDC(dummyHwnd_);
        if (!dummyHdc_ || !hs_native_gl::EnsureWindowPixelFormat(rt, dummyHdc_)) {
            outReason = "dummy_pfd_failed";
            Shutdown(false);
            return false;
        }

        const D3D_FEATURE_LEVEL reqLevels[2] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        D3D_FEATURE_LEVEL gotLevel = D3D_FEATURE_LEVEL_11_0;
        HRESULT hr = fnCreateDev(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, reqLevels, 2, D3D11_SDK_VERSION,
                                 &d3dDevice_, &gotLevel, &d3dCtx_);
        if (FAILED(hr) || !d3dDevice_ || !d3dCtx_) {
            hr = fnCreateDev(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                             0, reqLevels + 1, 1, D3D11_SDK_VERSION,
                             &d3dDevice_, &gotLevel, &d3dCtx_);
        }
        if (FAILED(hr) || !d3dDevice_ || !d3dCtx_) {
            outReason = "d3d11_create_device_failed";
            Shutdown(false);
            return false;
        }

        IDXGIDevice1*  dxgiDev = nullptr;
        IDXGIAdapter*  adapter = nullptr;
        IDXGIFactory2* fac2    = nullptr;
        IDXGIFactory5* fac5    = nullptr;

        auto releaseTmp = [&]() {
            if (fac5)    { fac5->Release();    fac5    = nullptr; }
            if (fac2)    { fac2->Release();    fac2    = nullptr; }
            if (adapter) { adapter->Release(); adapter = nullptr; }
            if (dxgiDev) { dxgiDev->Release(); dxgiDev = nullptr; }
        };

        if (FAILED(d3dDevice_->QueryInterface(kIidDxgiDevice1, reinterpret_cast<void**>(&dxgiDev))) || !dxgiDev) {
            releaseTmp();
            outReason = "qi_dxgi_device1_failed";
            Shutdown(false);
            return false;
        }
        dxgiDev->SetMaximumFrameLatency(1);

        if (FAILED(dxgiDev->GetParent(kIidDxgiAdapter, reinterpret_cast<void**>(&adapter))) || !adapter) {
            releaseTmp();
            outReason = "get_dxgi_adapter_failed";
            Shutdown(false);
            return false;
        }
        if (FAILED(adapter->GetParent(kIidDxgiFactory2, reinterpret_cast<void**>(&fac2))) || !fac2) {
            releaseTmp();
            outReason = "get_dxgi_factory2_failed";
            Shutdown(false);
            return false;
        }

        allowTearing_ = false;
        if (SUCCEEDED(fac2->QueryInterface(kIidDxgiFactory5, reinterpret_cast<void**>(&fac5))) && fac5) {
            BOOL tearing = FALSE;
            if (SUCCEEDED(fac5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearing, sizeof(tearing))) && tearing) {
                allowTearing_ = true;
            }
        }

        uint32_t w = 1280u, h = 720u;
        QueryClientSize(gameHwnd, w, h);

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width              = w;
        desc.Height             = h;
        desc.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.Stereo             = FALSE;
        desc.SampleDesc.Count   = 1;
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount        = 3;
        desc.Scaling            = DXGI_SCALING_STRETCH;
        desc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.AlphaMode          = DXGI_ALPHA_MODE_IGNORE;
        desc.Flags              = allowTearing_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

        hr = fac2->CreateSwapChainForHwnd(d3dDevice_, gameHwnd, &desc, nullptr, nullptr, &swapChain_);
        if (FAILED(hr) && allowTearing_) {
            allowTearing_ = false;
            desc.Flags    = 0u;
            hr = fac2->CreateSwapChainForHwnd(d3dDevice_, gameHwnd, &desc, nullptr, nullptr, &swapChain_);
        }
        if (FAILED(hr)) {
            desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
            hr = fac2->CreateSwapChainForHwnd(d3dDevice_, gameHwnd, &desc, nullptr, nullptr, &swapChain_);
        }
        if (FAILED(hr) || !swapChain_) {
            char buf[64];
            snprintf(buf, sizeof(buf), "create_swapchain_failed_0x%08lX", static_cast<unsigned long>(hr));
            outReason = buf;
            releaseTmp();
            Shutdown(false);
            return false;
        }

        // Never let DXGI intercept Alt+Enter behind SDL2 / DST.
        fac2->MakeWindowAssociation(gameHwnd, DXGI_MWA_NO_ALT_ENTER);
        releaseTmp();

        gameHwnd_       = gameHwnd;
        width_          = w;
        height_         = h;
        bufferCount_    = desc.BufferCount;
        swapEffect_     = desc.SwapEffect;
        swapChainFlags_ = desc.Flags;
        swapChainReady_.store(true, std::memory_order_release);
        outReason = "ok";
        return true;
    }

    // Stage 2: Call on the render thread right after wglMakeCurrent(DummyHdc(), hglrc) succeeds.
    // Creates the persistent GL offscreen RGBA8+D24S8 FBO and the WGL_NV_DX_interop2 shared D3D11 surface.
    bool InitGlResources(const hs_native_gl::WglRuntime& rt, std::string& outReason) noexcept {
        outReason.clear();
        if (!swapChainReady_.load(std::memory_order_acquire) || !d3dDevice_ || !swapChain_) {
            outReason = "swapchain_not_ready";
            return false;
        }
        if (glReady_.load(std::memory_order_acquire) && offscreenFbo_.load(std::memory_order_acquire) != 0u) {
            outReason = "already_initialized";
            return true;
        }
        if (!gl_.Resolve(rt)) {
            outReason = "gl_interop_procs_missing";
            return false;
        }

        hInteropDev_ = gl_.wglDXOpenDeviceNV(d3dDevice_);
        if (!hInteropDev_) {
            outReason = "wglDXOpenDeviceNV_failed";
            return false;
        }

        // Refresh client rect in case window resized between CreateWindowSurface and MakeCurrent.
        uint32_t curW = width_, curH = height_;
        if (QueryClientSize(gameHwnd_, curW, curH) && (curW != width_ || curH != height_)) {
            if (SUCCEEDED(swapChain_->ResizeBuffers(bufferCount_, curW, curH, DXGI_FORMAT_R8G8B8A8_UNORM, swapChainFlags_))) {
                width_  = curW;
                height_ = curH;
            }
        }

        int32_t prevTex = 0, prevRbo = 0, prevDrawFbo = 0, prevReadFbo = 0;
        gl_.glGetIntegerv(kGlTextureBinding2D,       &prevTex);
        gl_.glGetIntegerv(kGlRenderbufferBinding,    &prevRbo);
        gl_.glGetIntegerv(kGlDrawFramebufferBinding, &prevDrawFbo);
        gl_.glGetIntegerv(kGlReadFramebufferBinding, &prevReadFbo);

        // 1. Create persistent GL offscreen FBO (standard GL Y-up RGBA8 + D24S8).
        uint32_t fbo = 0u, ctex = 0u, dsRbo = 0u;
        gl_.glGenFramebuffers(1, &fbo);
        gl_.glGenTextures(1, &ctex);
        gl_.glGenRenderbuffers(1, &dsRbo);
        if (!fbo || !ctex || !dsRbo) {
            if (fbo)   gl_.glDeleteFramebuffers(1, &fbo);
            if (ctex)  gl_.glDeleteTextures(1, &ctex);
            if (dsRbo) gl_.glDeleteRenderbuffers(1, &dsRbo);
            ReleaseGlResources(true);
            outReason = "gen_offscreen_fbo_failed";
            return false;
        }

        gl_.glBindTexture(kGlTexture2D, ctex);
        gl_.glTexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<int32_t>(kGlNearest));
        gl_.glTexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<int32_t>(kGlNearest));
        gl_.glTexParameteri(kGlTexture2D, kGlTextureWrapS,     static_cast<int32_t>(kGlClampToEdge));
        gl_.glTexParameteri(kGlTexture2D, kGlTextureWrapT,     static_cast<int32_t>(kGlClampToEdge));
        gl_.glTexImage2D(kGlTexture2D, 0, static_cast<int32_t>(kGlRgba8),
                         static_cast<int32_t>(width_), static_cast<int32_t>(height_), 0,
                         kGlRgba, kGlUnsignedByte, nullptr);

        gl_.glBindRenderbuffer(kGlRenderbuffer, dsRbo);
        gl_.glRenderbufferStorage(kGlRenderbuffer, kGlDepth24Stencil8,
                                  static_cast<int32_t>(width_), static_cast<int32_t>(height_));

        gl_.glBindFramebuffer(kGlFramebuffer, fbo);
        gl_.glFramebufferTexture2D(kGlFramebuffer, kGlColorAttachment0, kGlTexture2D, ctex, 0);
        gl_.glFramebufferRenderbuffer(kGlFramebuffer, kGlDepthStencilAttachment, kGlRenderbuffer, dsRbo);
        const uint32_t offStatus = gl_.glCheckFramebufferStatus(kGlFramebuffer);
        if (offStatus != kGlFramebufferComplete) {
            char buf[64];
            snprintf(buf, sizeof(buf), "offscreen_fbo_incomplete_0x%X", offStatus);
            outReason = buf;
            gl_.glBindTexture(kGlTexture2D, static_cast<uint32_t>(prevTex));
            gl_.glBindRenderbuffer(kGlRenderbuffer, static_cast<uint32_t>(prevRbo));
            gl_.glBindFramebuffer(kGlDrawFramebuffer, static_cast<uint32_t>(prevDrawFbo));
            gl_.glBindFramebuffer(kGlReadFramebuffer, static_cast<uint32_t>(prevReadFbo));
            gl_.glDeleteFramebuffers(1, &fbo);
            gl_.glDeleteTextures(1, &ctex);
            gl_.glDeleteRenderbuffers(1, &dsRbo);
            ReleaseGlResources(true);
            return false;
        }

        offscreenFbo_.store(fbo, std::memory_order_release);
        offscreenColorTex_       = ctex;
        offscreenDepthStencilRbo_ = dsRbo;

        // 2. Create static VRAM D3D11 shared texture + WGL_NV_DX_interop2 renderbuffer + FBO.
        if (!CreateInteropTarget(width_, height_, outReason)) {
            gl_.glBindTexture(kGlTexture2D, static_cast<uint32_t>(prevTex));
            gl_.glBindRenderbuffer(kGlRenderbuffer, static_cast<uint32_t>(prevRbo));
            gl_.glBindFramebuffer(kGlDrawFramebuffer, static_cast<uint32_t>(prevDrawFbo));
            gl_.glBindFramebuffer(kGlReadFramebuffer, static_cast<uint32_t>(prevReadFbo));
            ReleaseGlResources(true);
            return false;
        }

        // Restore previous texture/renderbuffer bindings, and bind offscreenFbo as active default FBO!
        gl_.glBindTexture(kGlTexture2D, static_cast<uint32_t>(prevTex));
        gl_.glBindRenderbuffer(kGlRenderbuffer, static_cast<uint32_t>(prevRbo));
        gl_.glBindFramebuffer(kGlDrawFramebuffer, (prevDrawFbo == 0) ? fbo : static_cast<uint32_t>(prevDrawFbo));
        gl_.glBindFramebuffer(kGlReadFramebuffer, (prevReadFbo == 0) ? fbo : static_cast<uint32_t>(prevReadFbo));
        if (gl_.glViewport && width_ > 1u && height_ > 1u) {
            int32_t vp[4]{};
            gl_.glGetIntegerv(kGlViewport, vp);
            if (vp[2] <= 1 && vp[3] <= 1) {
                gl_.glViewport(0, 0, static_cast<int32_t>(width_), static_cast<int32_t>(height_));
            }
        }

        glReady_.store(true, std::memory_order_release);
        outReason = "ok";
        return true;
    }

    // Call before glClear on default FBO (or after Present) to track window resize smoothly.
    bool EnsureSizeForCurrentWindow() noexcept {
        if (!IsActive() || !gameHwnd_) return false;
        uint32_t newW = 0u, newH = 0u;
        if (!QueryClientSize(gameHwnd_, newW, newH)) {
            return true; // Minimized (0x0) — keep current backing buffers alive.
        }
        if (newW == width_ && newH == height_) {
            return true;
        }
        return ResizeInternal(newW, newH);
    }

    // Hardware VRAM Y-flip blit (offscreenFbo -> interopFbo) -> D3D11 CopyResource -> DXGI Flip Present!
    bool Present(int32_t swapInterval) noexcept {
        if (!IsActive() || !swapChain_ || !d3dCtx_ || !hInteropDev_ || !hInteropObj_) {
            return false;
        }

        // Skip presenting while window is minimized (0x0 client rect).
        uint32_t curW = 0u, curH = 0u;
        if (!QueryClientSize(gameHwnd_, curW, curH)) {
            return true;
        }

        const uint32_t offFbo = offscreenFbo_.load(std::memory_order_acquire);
        if (!offFbo || !interopFbo_) return false;

        int32_t prevReadFbo = 0, prevDrawFbo = 0;
        gl_.glGetIntegerv(kGlReadFramebufferBinding, &prevReadFbo);
        gl_.glGetIntegerv(kGlDrawFramebufferBinding, &prevDrawFbo);
        const bool wasScissor = (gl_.glIsEnabled(kGlScissorTest) != 0u);
        if (wasScissor) {
            gl_.glDisable(kGlScissorTest);
        }

        bool blitOk = false;
        if (gl_.wglDXLockObjectsNV(hInteropDev_, 1, &hInteropObj_)) {
            gl_.glBindFramebuffer(kGlReadFramebuffer, offFbo);
            gl_.glBindFramebuffer(kGlDrawFramebuffer, interopFbo_);
            // Y-flip during VRAM blit: GL bottom-up (0..h) -> D3D11 top-down (h..0).
            gl_.glBlitFramebuffer(0, 0, static_cast<int32_t>(width_), static_cast<int32_t>(height_),
                                  0, static_cast<int32_t>(height_), static_cast<int32_t>(width_), 0,
                                  kGlColorBufferBit, kGlNearest);
            gl_.glBindFramebuffer(kGlReadFramebuffer, static_cast<uint32_t>(prevReadFbo));
            gl_.glBindFramebuffer(kGlDrawFramebuffer, static_cast<uint32_t>(prevDrawFbo));
            gl_.wglDXUnlockObjectsNV(hInteropDev_, 1, &hInteropObj_);
            blitOk = true;
        } else {
            gl_.glBindFramebuffer(kGlReadFramebuffer, static_cast<uint32_t>(prevReadFbo));
            gl_.glBindFramebuffer(kGlDrawFramebuffer, static_cast<uint32_t>(prevDrawFbo));
        }

        if (wasScissor) {
            gl_.glEnable(kGlScissorTest);
        }
        if (!blitOk) {
            return false;
        }

        ID3D11Texture2D* backBuf = nullptr;
        HRESULT hr = swapChain_->GetBuffer(0, kIidD3d11Texture2D, reinterpret_cast<void**>(&backBuf));
        if (FAILED(hr) || !backBuf) {
            return false;
        }
        d3dCtx_->CopyResource(backBuf, sharedTex_);
        backBuf->Release();

        const uint32_t syncInt = (swapInterval > 0)
            ? static_cast<uint32_t>(std::min<int32_t>(swapInterval, 4))
            : 0u;
        uint32_t flags = (syncInt == 0u && allowTearing_) ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        hr = swapChain_->Present(syncInt, flags);
        if (hr == DXGI_ERROR_INVALID_CALL && (flags & DXGI_PRESENT_ALLOW_TEARING) != 0u) {
            hr = swapChain_->Present(0u, 0u);
        }

        presentCount_.fetch_add(1u, std::memory_order_relaxed);

        // If window resized during this frame, resize offscreen/swapchain cleanly for the next frame.
        if (curW != width_ || curH != height_) {
            ResizeInternal(curW, curH);
        }

        return SUCCEEDED(hr);
    }

    void ReleaseGlResources(bool glContextCurrent) noexcept {
        glReady_.store(false, std::memory_order_release);
        const uint32_t offFbo = offscreenFbo_.exchange(0u, std::memory_order_acq_rel);

        if (glContextCurrent) {
            if (hInteropObj_ && hInteropDev_ && gl_.wglDXUnregisterObjectNV) {
                gl_.wglDXUnregisterObjectNV(hInteropDev_, hInteropObj_);
            }
            if (interopFbo_ && gl_.glDeleteFramebuffers) {
                gl_.glDeleteFramebuffers(1, &interopFbo_);
            }
            if (interopRbo_ && gl_.glDeleteRenderbuffers) {
                gl_.glDeleteRenderbuffers(1, &interopRbo_);
            }
            if (offFbo && gl_.glDeleteFramebuffers) {
                gl_.glDeleteFramebuffers(1, &offFbo);
            }
            if (offscreenColorTex_ && gl_.glDeleteTextures) {
                gl_.glDeleteTextures(1, &offscreenColorTex_);
            }
            if (offscreenDepthStencilRbo_ && gl_.glDeleteRenderbuffers) {
                gl_.glDeleteRenderbuffers(1, &offscreenDepthStencilRbo_);
            }
            if (hInteropDev_ && gl_.wglDXCloseDeviceNV) {
                gl_.wglDXCloseDeviceNV(hInteropDev_);
            }
        }
        hInteropObj_             = nullptr;
        interopFbo_              = 0u;
        interopRbo_              = 0u;
        offscreenColorTex_       = 0u;
        offscreenDepthStencilRbo_ = 0u;
        hInteropDev_             = nullptr;
        if (sharedTex_) {
            sharedTex_->Release();
            sharedTex_ = nullptr;
        }
    }

    void Shutdown(bool glContextCurrent) noexcept {
        ReleaseGlResources(glContextCurrent);
        swapChainReady_.store(false, std::memory_order_release);
        if (swapChain_) {
            swapChain_->Release();
            swapChain_ = nullptr;
        }
        if (d3dCtx_) {
            d3dCtx_->ClearState();
            d3dCtx_->Flush();
            d3dCtx_->Release();
            d3dCtx_ = nullptr;
        }
        if (d3dDevice_) {
            d3dDevice_->Release();
            d3dDevice_ = nullptr;
        }
        if (dummyHdc_ && dummyHwnd_) {
            ReleaseDC(dummyHwnd_, dummyHdc_);
            dummyHdc_ = nullptr;
        }
        if (dummyHwnd_) {
            DestroyWindow(dummyHwnd_);
            dummyHwnd_ = nullptr;
        }
        gameHwnd_       = nullptr;
        width_          = 0u;
        height_         = 0u;
        allowTearing_   = false;
        swapChainFlags_ = 0u;
    }

    // Diagnostic readback of the D3D11 shared surface (top-down RGBA8) for live GPU tests.
    bool ReadbackSharedTextureRgba(std::vector<uint8_t>& outRgba, uint32_t& outW, uint32_t& outH) noexcept {
        outRgba.clear();
        outW = 0u;
        outH = 0u;
        if (!d3dDevice_ || !d3dCtx_ || !sharedTex_ || width_ == 0u || height_ == 0u) return false;

        D3D11_TEXTURE2D_DESC sd{};
        sd.Width              = width_;
        sd.Height             = height_;
        sd.MipLevels          = 1;
        sd.ArraySize          = 1;
        sd.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.SampleDesc.Count   = 1;
        sd.SampleDesc.Quality = 0;
        sd.Usage              = D3D11_USAGE_STAGING;
        sd.CPUAccessFlags     = D3D11_CPU_ACCESS_READ;

        ID3D11Texture2D* staging = nullptr;
        if (FAILED(d3dDevice_->CreateTexture2D(&sd, nullptr, &staging)) || !staging) {
            return false;
        }
        d3dCtx_->CopyResource(staging, sharedTex_);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr = d3dCtx_->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr) || !mapped.pData) {
            staging->Release();
            return false;
        }

        outW = width_;
        outH = height_;
        const size_t rowBytes = static_cast<size_t>(width_) * 4u;
        outRgba.resize(rowBytes * height_);
        const auto* src = static_cast<const uint8_t*>(mapped.pData);
        for (uint32_t y = 0; y < height_; ++y) {
            std::memcpy(outRgba.data() + static_cast<size_t>(y) * rowBytes,
                        src + static_cast<size_t>(y) * mapped.RowPitch,
                        rowBytes);
        }
        d3dCtx_->Unmap(staging, 0);
        staging->Release();
        return true;
    }

    bool     IsSwapChainReady() const noexcept { return swapChainReady_.load(std::memory_order_acquire); }
    bool     IsActive()         const noexcept { return swapChainReady_.load(std::memory_order_acquire) && glReady_.load(std::memory_order_acquire) && offscreenFbo_.load(std::memory_order_acquire) != 0u; }
    uint32_t OffscreenFbo()     const noexcept { return offscreenFbo_.load(std::memory_order_acquire); }
    HWND     GameHwnd()         const noexcept { return gameHwnd_; }
    HWND     DummyHwnd()        const noexcept { return dummyHwnd_; }
    HDC      DummyHdc()         const noexcept { return dummyHdc_; }
    uint32_t Width()            const noexcept { return width_; }
    uint32_t Height()           const noexcept { return height_; }
    bool     AllowTearing()     const noexcept { return allowTearing_; }
    uint32_t BufferCount()      const noexcept { return bufferCount_; }
    uint32_t ResizeCount()      const noexcept { return resizeCount_.load(std::memory_order_relaxed); }
    uint64_t PresentCount()     const noexcept { return presentCount_.load(std::memory_order_relaxed); }
    const char* SwapEffectName() const noexcept {
        return (swapEffect_ == DXGI_SWAP_EFFECT_FLIP_DISCARD) ? "FLIP_DISCARD" : "FLIP_SEQUENTIAL";
    }

private:
    static bool QueryClientSize(HWND hwnd, uint32_t& outW, uint32_t& outH) noexcept {
        if (!hwnd) return false;
        RECT rc{};
        if (!GetClientRect(hwnd, &rc)) return false;
        const int32_t w = rc.right - rc.left;
        const int32_t h = rc.bottom - rc.top;
        if (w <= 0 || h <= 0) return false;
        outW = static_cast<uint32_t>(std::clamp<int32_t>(w, 1, 16384));
        outH = static_cast<uint32_t>(std::clamp<int32_t>(h, 1, 16384));
        return true;
    }

    bool CreateInteropTarget(uint32_t w, uint32_t h, std::string& outReason) noexcept {
        D3D11_TEXTURE2D_DESC td{};
        td.Width              = w;
        td.Height             = h;
        td.MipLevels          = 1;
        td.ArraySize          = 1;
        td.Format             = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count   = 1;
        td.SampleDesc.Quality = 0;
        td.Usage              = D3D11_USAGE_DEFAULT;
        td.BindFlags          = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(d3dDevice_->CreateTexture2D(&td, nullptr, &sharedTex_)) || !sharedTex_) {
            outReason = "create_shared_d3d11_tex_failed";
            return false;
        }

        gl_.glGenRenderbuffers(1, &interopRbo_);
        gl_.glGenFramebuffers(1, &interopFbo_);
        if (!interopRbo_ || !interopFbo_) {
            outReason = "gen_interop_gl_objs_failed";
            return false;
        }

        hInteropObj_ = gl_.wglDXRegisterObjectNV(hInteropDev_, sharedTex_, interopRbo_,
                                                 kGlRenderbuffer, kWglAccessWriteDiscardNv);
        if (!hInteropObj_) {
            outReason = "wglDXRegisterObjectNV_failed";
            return false;
        }

        if (!gl_.wglDXLockObjectsNV(hInteropDev_, 1, &hInteropObj_)) {
            outReason = "wglDXLockObjectsNV_init_failed";
            return false;
        }
        gl_.glBindFramebuffer(kGlFramebuffer, interopFbo_);
        gl_.glFramebufferRenderbuffer(kGlFramebuffer, kGlColorAttachment0, kGlRenderbuffer, interopRbo_);
        const uint32_t status = gl_.glCheckFramebufferStatus(kGlFramebuffer);
        gl_.wglDXUnlockObjectsNV(hInteropDev_, 1, &hInteropObj_);

        if (status != kGlFramebufferComplete) {
            char buf[64];
            snprintf(buf, sizeof(buf), "interop_fbo_incomplete_0x%X", status);
            outReason = buf;
            return false;
        }
        return true;
    }

    bool ResizeInternal(uint32_t newW, uint32_t newH) noexcept {
        if (!IsActive() || newW == 0u || newH == 0u) return false;

        int32_t prevTex = 0, prevRbo = 0, prevDrawFbo = 0, prevReadFbo = 0;
        gl_.glGetIntegerv(kGlTextureBinding2D,       &prevTex);
        gl_.glGetIntegerv(kGlRenderbufferBinding,    &prevRbo);
        gl_.glGetIntegerv(kGlDrawFramebufferBinding, &prevDrawFbo);
        gl_.glGetIntegerv(kGlReadFramebufferBinding, &prevReadFbo);

        // Tear down old interop objects first.
        if (hInteropObj_ && hInteropDev_) {
            gl_.wglDXUnregisterObjectNV(hInteropDev_, hInteropObj_);
            hInteropObj_ = nullptr;
        }
        if (interopFbo_) {
            gl_.glDeleteFramebuffers(1, &interopFbo_);
            interopFbo_ = 0u;
        }
        if (interopRbo_) {
            gl_.glDeleteRenderbuffers(1, &interopRbo_);
            interopRbo_ = 0u;
        }
        if (sharedTex_) {
            sharedTex_->Release();
            sharedTex_ = nullptr;
        }

        d3dCtx_->ClearState();
        d3dCtx_->Flush();

        HRESULT hr = swapChain_->ResizeBuffers(bufferCount_, newW, newH, DXGI_FORMAT_R8G8B8A8_UNORM, swapChainFlags_);
        if (FAILED(hr)) {
            // Attempt to restore interop target at previous size so rendering can continue.
            std::string ignore;
            CreateInteropTarget(width_, height_, ignore);
            gl_.glBindTexture(kGlTexture2D, static_cast<uint32_t>(prevTex));
            gl_.glBindRenderbuffer(kGlRenderbuffer, static_cast<uint32_t>(prevRbo));
            gl_.glBindFramebuffer(kGlDrawFramebuffer, static_cast<uint32_t>(prevDrawFbo));
            gl_.glBindFramebuffer(kGlReadFramebuffer, static_cast<uint32_t>(prevReadFbo));
            return false;
        }

        // Reallocate offscreen FBO backing storage IN-PLACE (offscreenFbo_ handle ID stays identical!).
        gl_.glBindTexture(kGlTexture2D, offscreenColorTex_);
        gl_.glTexImage2D(kGlTexture2D, 0, static_cast<int32_t>(kGlRgba8),
                         static_cast<int32_t>(newW), static_cast<int32_t>(newH), 0,
                         kGlRgba, kGlUnsignedByte, nullptr);

        gl_.glBindRenderbuffer(kGlRenderbuffer, offscreenDepthStencilRbo_);
        gl_.glRenderbufferStorage(kGlRenderbuffer, kGlDepth24Stencil8,
                                  static_cast<int32_t>(newW), static_cast<int32_t>(newH));

        std::string reason;
        const bool interopOk = CreateInteropTarget(newW, newH, reason);

        const uint32_t offFbo = offscreenFbo_.load(std::memory_order_acquire);
        gl_.glBindTexture(kGlTexture2D, static_cast<uint32_t>(prevTex));
        gl_.glBindRenderbuffer(kGlRenderbuffer, static_cast<uint32_t>(prevRbo));
        gl_.glBindFramebuffer(kGlDrawFramebuffer, (prevDrawFbo == 0) ? offFbo : static_cast<uint32_t>(prevDrawFbo));
        gl_.glBindFramebuffer(kGlReadFramebuffer, (prevReadFbo == 0) ? offFbo : static_cast<uint32_t>(prevReadFbo));

        if (!interopOk) return false;

        width_  = newW;
        height_ = newH;
        resizeCount_.fetch_add(1u, std::memory_order_relaxed);
        return true;
    }

    HMODULE              hD3d11_       = nullptr;
    HWND                 gameHwnd_     = nullptr;
    HWND                 dummyHwnd_    = nullptr;
    HDC                  dummyHdc_     = nullptr;

    ID3D11Device*        d3dDevice_    = nullptr;
    ID3D11DeviceContext* d3dCtx_       = nullptr;
    IDXGISwapChain1*     swapChain_    = nullptr;
    ID3D11Texture2D*     sharedTex_    = nullptr;

    RawGlProcs           gl_{};
    HANDLE               hInteropDev_  = nullptr;
    HANDLE               hInteropObj_  = nullptr;

    std::atomic<uint32_t> offscreenFbo_{0u};
    uint32_t              offscreenColorTex_        = 0u;
    uint32_t              offscreenDepthStencilRbo_ = 0u;
    uint32_t              interopRbo_               = 0u;
    uint32_t              interopFbo_               = 0u;

    uint32_t              width_          = 0u;
    uint32_t              height_         = 0u;
    uint32_t              bufferCount_    = 3u;
    DXGI_SWAP_EFFECT      swapEffect_     = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    uint32_t              swapChainFlags_ = 0u;
    bool                  allowTearing_   = false;

    std::atomic<bool>     swapChainReady_{false};
    std::atomic<bool>     glReady_{false};
    std::atomic<uint32_t> resizeCount_{0u};
    std::atomic<uint64_t> presentCount_{0u};
};

} // namespace hs_dxgi_flip
