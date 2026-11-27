#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "native_gl_bridge.h"
#include "gpu_clarity_engine.h"

static void ClarityTestLog(const char* msg) {
    printf("  [LOG] %s\n", msg);
}

static void FillTestImage(std::vector<uint8_t>& rgba, int32_t w, int32_t h) {
    rgba.resize(static_cast<size_t>(w) * static_cast<size_t>(h) * 4u);
    for (int32_t y = 0; y < h; ++y) {
        for (int32_t x = 0; x < w; ++x) {
            const float fx = static_cast<float>(x) / static_cast<float>(w);
            const float fy = static_cast<float>(y) / static_cast<float>(h);
            // Smooth background gradient + parchment patch + soft diagonal hand-drawn ink stroke
            float r = 205.0f + 20.0f * fx - 12.0f * fy;
            float g = 190.0f + 16.0f * fx - 10.0f * fy;
            float b = 158.0f + 10.0f * fy;

            // Soft hazy mid-frequency texture patch in [x: 600..900, y: 60..220] (away from diagonal stroke)
            if (x >= 600 && x < 900 && y >= 60 && y < 220) {
                const float wave = 8.0f * std::sin(static_cast<float>(x) * 0.42f) * std::cos(static_cast<float>(y) * 0.42f)
                                 + 5.0f * std::sin(static_cast<float>(x) * 1.15f + static_cast<float>(y) * 0.65f);
                r += wave;
                g += wave;
                b += wave;
            }

            // Diagonal ink stroke along y = 0.52*x + 120 with 1.4px soft blur width
            const float dy = static_cast<float>(y) - (0.52f * static_cast<float>(x) + 120.0f);
            const float d = std::fabs(dy);
            if (d < 2.8f) {
                const float t = std::clamp((d - 0.8f) / 2.0f, 0.0f, 1.0f);
                const float s = t * t * (3.0f - 2.0f * t);
                r = 22.0f + (r - 22.0f) * s;
                g = 20.0f + (g - 20.0f) * s;
                b = 24.0f + (b - 24.0f) * s;
            }
            const size_t idx = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
            rgba[idx + 0] = static_cast<uint8_t>(std::clamp(static_cast<int>(r + 0.5f), 0, 255));
            rgba[idx + 1] = static_cast<uint8_t>(std::clamp(static_cast<int>(g + 0.5f), 0, 255));
            rgba[idx + 2] = static_cast<uint8_t>(std::clamp(static_cast<int>(b + 0.5f), 0, 255));
            rgba[idx + 3] = 255u;
        }
    }
}

static double MeasureLocalPatchContrast(const std::vector<uint8_t>& rgba, int32_t w, int32_t h) {
    const int32_t x0 = 620, x1 = std::min(w - 1, 880);
    const int32_t y0 = 80,  y1 = std::min(h - 1, 200);
    double sum = 0.0;
    int cnt = 0;
    for (int32_t y = y0; y < y1; ++y) {
        for (int32_t x = x0; x < x1; ++x) {
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
            sum += 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
            ++cnt;
        }
    }
    if (cnt == 0) return 0.0;
    const double mean = sum / static_cast<double>(cnt);
    double mad = 0.0;
    for (int32_t y = y0; y < y1; ++y) {
        for (int32_t x = x0; x < x1; ++x) {
            const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
            const double yv = 0.2126 * rgba[i] + 0.7152 * rgba[i + 1] + 0.0722 * rgba[i + 2];
            mad += std::fabs(yv - mean);
        }
    }
    return mad / static_cast<double>(cnt);
}

static double MeasureDiagonalEdgeContrast(const std::vector<uint8_t>& rgba, int32_t w, int32_t h) {
    // Average max local vertical luminance transition across the stroke in [400..1200]
    double sumMaxDiff = 0.0;
    int cnt = 0;
    for (int32_t x = 400; x < std::min(w - 2, 1200); x += 4) {
        const int32_t cy = static_cast<int32_t>(0.52f * static_cast<float>(x) + 120.0f);
        if (cy < 4 || cy >= h - 4) continue;
        double maxStep = 0.0;
        for (int32_t y = cy - 3; y < cy + 3; ++y) {
            const size_t i0 = (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
            const size_t i1 = (static_cast<size_t>(y + 1) * static_cast<size_t>(w) + static_cast<size_t>(x)) * 4u;
            const double y0 = 0.2126 * rgba[i0] + 0.7152 * rgba[i0 + 1] + 0.0722 * rgba[i0 + 2];
            const double y1 = 0.2126 * rgba[i1] + 0.7152 * rgba[i1 + 1] + 0.0722 * rgba[i1 + 2];
            maxStep = std::max(maxStep, std::fabs(y1 - y0));
        }
        sumMaxDiff += maxStep;
        ++cnt;
    }
    return (cnt > 0) ? (sumMaxDiff / static_cast<double>(cnt)) : 0.0;
}

int main() {
    printf("=== Live RTX GPU Test: OpenGL 4.3+ Compute Clarity Engine + RGBA16F LightMap ===\n");

    hs_native_gl::WglRuntime rt{};
    const auto probe = hs_native_gl::ProbeNativeEs2Support(hs_native_gl::BackendMode::Auto, rt);
    if (!probe.supported) {
        printf("  [FAIL] Native OpenGL probe unsupported: %s\n", probe.reason.c_str());
        return 1;
    }
    printf("  GPU: %s | GL %d.%d | compute=%d imageLoadStore=%d\n",
           probe.renderer.c_str(), probe.glMajor, probe.glMinor,
           probe.hasComputeShader ? 1 : 0, probe.hasImageLoadStore ? 1 : 0);

    HWND hwnd = CreateWindowExW(0, L"STATIC", L"HS_CLARITY_TEST", WS_POPUP, 0, 0, 128, 128, nullptr, nullptr, nullptr, nullptr);
    HDC hdc = nullptr;
    HGLRC rc = nullptr;
    if (!hwnd || !hs_native_gl::CreateGameEs2Context(rt, hwnd, hdc, rc) || !rt.pWglMakeCurrent(hdc, rc)) {
        if (hwnd) DestroyWindow(hwnd);
        printf("  [FAIL] CreateGameEs2Context / wglMakeCurrent failed\n");
        return 1;
    }

    void* procs[76]{};
    hs_native_gl::OptionalGlProcs opt{};
    hs_native_gl::ResolveLiveGlesTable(rt, procs, opt);
    auto findP = [&](const char* name) -> void* {
        for (size_t i = 0; i < 76; ++i) {
            if (std::strcmp(hs_native_gl::kDstGles2Imports[i].name, name) == 0) return procs[i];
        }
        return nullptr;
    };

    using GlTexImage2D_t = void (WINAPI*)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*);
    using GlReadPixels_t = void (WINAPI*)(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void*);
    using GlGetError_t   = uint32_t (WINAPI*)();
    using GlFinish_t     = void (WINAPI*)();

    auto pTexImg   = reinterpret_cast<GlTexImage2D_t>(findP("glTexImage2D"));
    auto pReadPix  = reinterpret_cast<GlReadPixels_t>(findP("glReadPixels"));
    auto pGetErr   = reinterpret_cast<GlGetError_t>(findP("glGetError"));
    auto pFinish   = reinterpret_cast<GlFinish_t>(findP("glFinish"));

    hs_gpu_clarity::GlComputeOps ops{};
    ops.glGenFramebuffers        = reinterpret_cast<hs_gpu_clarity::GlComputeOps::GenFramebuffers_t>(findP("glGenFramebuffers"));
    ops.glDeleteFramebuffers     = reinterpret_cast<hs_gpu_clarity::GlComputeOps::DeleteFramebuffers_t>(findP("glDeleteFramebuffers"));
    ops.glBindFramebuffer        = reinterpret_cast<hs_gpu_clarity::GlComputeOps::BindFramebuffer_t>(findP("glBindFramebuffer"));
    ops.glFramebufferTexture2D   = reinterpret_cast<hs_gpu_clarity::GlComputeOps::FramebufferTexture2D_t>(findP("glFramebufferTexture2D"));
    ops.glCheckFramebufferStatus = reinterpret_cast<hs_gpu_clarity::GlComputeOps::CheckFramebufferStatus_t>(findP("glCheckFramebufferStatus"));
    ops.glBlitFramebuffer        = reinterpret_cast<hs_gpu_clarity::GlComputeOps::BlitFramebuffer_t>(opt.glBlitFramebuffer);
    ops.glGenTextures            = reinterpret_cast<hs_gpu_clarity::GlComputeOps::GenTextures_t>(findP("glGenTextures"));
    ops.glDeleteTextures         = reinterpret_cast<hs_gpu_clarity::GlComputeOps::DeleteTextures_t>(findP("glDeleteTextures"));
    ops.glActiveTexture          = reinterpret_cast<hs_gpu_clarity::GlComputeOps::ActiveTexture_t>(findP("glActiveTexture"));
    ops.glBindTexture            = reinterpret_cast<hs_gpu_clarity::GlComputeOps::BindTexture_t>(findP("glBindTexture"));
    ops.glTexStorage2D           = reinterpret_cast<hs_gpu_clarity::GlComputeOps::TexStorage2D_t>(opt.glTexStorage2D);
    ops.glTexParameteri          = reinterpret_cast<hs_gpu_clarity::GlComputeOps::TexParameteri_t>(findP("glTexParameteri"));
    ops.glCreateShader           = reinterpret_cast<hs_gpu_clarity::GlComputeOps::CreateShader_t>(findP("glCreateShader"));
    ops.glShaderSource           = reinterpret_cast<hs_gpu_clarity::GlComputeOps::ShaderSource_t>(findP("glShaderSource"));
    ops.glCompileShader          = reinterpret_cast<hs_gpu_clarity::GlComputeOps::CompileShader_t>(findP("glCompileShader"));
    ops.glGetShaderiv            = reinterpret_cast<hs_gpu_clarity::GlComputeOps::GetShaderiv_t>(findP("glGetShaderiv"));
    ops.glGetShaderInfoLog       = reinterpret_cast<hs_gpu_clarity::GlComputeOps::GetShaderInfoLog_t>(findP("glGetShaderInfoLog"));
    ops.glDeleteShader           = reinterpret_cast<hs_gpu_clarity::GlComputeOps::DeleteShader_t>(findP("glDeleteShader"));
    ops.glCreateProgram          = reinterpret_cast<hs_gpu_clarity::GlComputeOps::CreateProgram_t>(findP("glCreateProgram"));
    ops.glAttachShader           = reinterpret_cast<hs_gpu_clarity::GlComputeOps::AttachShader_t>(findP("glAttachShader"));
    ops.glLinkProgram            = reinterpret_cast<hs_gpu_clarity::GlComputeOps::LinkProgram_t>(findP("glLinkProgram"));
    ops.glGetProgramiv           = reinterpret_cast<hs_gpu_clarity::GlComputeOps::GetProgramiv_t>(findP("glGetProgramiv"));
    ops.glGetProgramInfoLog      = reinterpret_cast<hs_gpu_clarity::GlComputeOps::GetProgramInfoLog_t>(findP("glGetProgramInfoLog"));
    ops.glDeleteProgram          = reinterpret_cast<hs_gpu_clarity::GlComputeOps::DeleteProgram_t>(findP("glDeleteProgram"));
    ops.glUseProgram             = reinterpret_cast<hs_gpu_clarity::GlComputeOps::UseProgram_t>(findP("glUseProgram"));
    ops.glGetUniformLocation     = reinterpret_cast<hs_gpu_clarity::GlComputeOps::GetUniformLocation_t>(findP("glGetUniformLocation"));
    ops.glUniform4f              = reinterpret_cast<hs_gpu_clarity::GlComputeOps::Uniform4f_t>(opt.glUniform4f);
    ops.glDispatchCompute        = reinterpret_cast<hs_gpu_clarity::GlComputeOps::DispatchCompute_t>(opt.glDispatchCompute);
    ops.glMemoryBarrier          = reinterpret_cast<hs_gpu_clarity::GlComputeOps::MemoryBarrier_t>(opt.glMemoryBarrier);
    ops.glBindImageTexture       = reinterpret_cast<hs_gpu_clarity::GlComputeOps::BindImageTexture_t>(opt.glBindImageTexture);
    ops.glGetIntegerv            = reinterpret_cast<hs_gpu_clarity::GlComputeOps::GetIntegerv_t>(findP("glGetIntegerv"));
    ops.glIsEnabled              = reinterpret_cast<hs_gpu_clarity::GlComputeOps::IsEnabled_t>(opt.glIsEnabled);
    ops.glEnable                 = reinterpret_cast<hs_gpu_clarity::GlComputeOps::Enable_t>(findP("glEnable"));
    ops.glDisable                = reinterpret_cast<hs_gpu_clarity::GlComputeOps::Disable_t>(findP("glDisable"));
    ops.glGenQueries             = reinterpret_cast<hs_gpu_clarity::GlComputeOps::GenQueries_t>(opt.glGenQueries);
    ops.glDeleteQueries          = reinterpret_cast<hs_gpu_clarity::GlComputeOps::DeleteQueries_t>(opt.glDeleteQueries);
    ops.glBeginQuery             = reinterpret_cast<hs_gpu_clarity::GlComputeOps::BeginQuery_t>(opt.glBeginQuery);
    ops.glEndQuery               = reinterpret_cast<hs_gpu_clarity::GlComputeOps::EndQuery_t>(opt.glEndQuery);
    ops.glGetQueryObjectiv       = reinterpret_cast<hs_gpu_clarity::GlComputeOps::GetQueryObjectiv_t>(opt.glGetQueryObjectiv);
    ops.glGetQueryObjectui64v    = reinterpret_cast<hs_gpu_clarity::GlComputeOps::GetQueryObjectui64v_t>(opt.glGetQueryObjectui64v);
    ops.logFn                    = &ClarityTestLog;

    if (!ops.IsComplete() || !ops.HasTimerQuery() || !pTexImg || !pReadPix || !pGetErr || !pFinish) {
        printf("  [FAIL] Missing GL4.3 compute/timer procs!\n");
        return 1;
    }

    // Drain any prior state
    while (pGetErr() != 0u) {}

    // 1. Test RGBA16F HDR LightMap RT FBO completeness
    uint32_t hdrFbo = 0u, hdrTex = 0u;
    ops.glGenFramebuffers(1, &hdrFbo);
    ops.glGenTextures(1, &hdrTex);
    ops.glActiveTexture(hs_gpu_clarity::kGlTexture0);
    ops.glBindTexture(hs_gpu_clarity::kGlTexture2D, hdrTex);
    ops.glTexParameteri(hs_gpu_clarity::kGlTexture2D, hs_gpu_clarity::kGlTextureMinFilter, static_cast<int32_t>(hs_gpu_clarity::kGlLinear));
    ops.glTexParameteri(hs_gpu_clarity::kGlTexture2D, hs_gpu_clarity::kGlTextureMagFilter, static_cast<int32_t>(hs_gpu_clarity::kGlLinear));
    pTexImg(hs_gpu_clarity::kGlTexture2D, 0, 0x881A /*GL_RGBA16F*/, 1920, 1080, 0, 0x1908u, 0x140Bu /*GL_HALF_FLOAT*/, nullptr);
    ops.glBindFramebuffer(hs_gpu_clarity::kGlFramebuffer, hdrFbo);
    ops.glFramebufferTexture2D(hs_gpu_clarity::kGlFramebuffer, hs_gpu_clarity::kGlColorAttachment0, hs_gpu_clarity::kGlTexture2D, hdrTex, 0);
    const uint32_t hdrStatus = ops.glCheckFramebufferStatus(hs_gpu_clarity::kGlFramebuffer);
    const uint32_t hdrErr = pGetErr();
    const bool hdrOk = (hdrStatus == hs_gpu_clarity::kGlFramebufferComplete && hdrErr == 0u);
    ops.glDeleteFramebuffers(1, &hdrFbo);
    ops.glDeleteTextures(1, &hdrTex);
    printf("  1. RGBA16F 1920x1080 HDR LightMap RT: status=0x%X err=0x%X -> %s\n", hdrStatus, hdrErr, hdrOk ? "PASS" : "FAIL");
    if (!hdrOk) return 1;

    // 2. Test Compute Engine Grade 2 (UltraHD) & Grade 3 (BluRay) @ 1920x1080
    hs_gpu_clarity::GpuClarityEngine eng;
    eng.SetOps(ops, true);

    auto makeTestFbo = [&](int32_t w, int32_t h, const std::vector<uint8_t>& pxData, uint32_t& outFbo, uint32_t& outTex) {
        ops.glGenTextures(1, &outTex);
        ops.glGenFramebuffers(1, &outFbo);
        ops.glActiveTexture(hs_gpu_clarity::kGlTexture0);
        ops.glBindTexture(hs_gpu_clarity::kGlTexture2D, outTex);
        ops.glTexParameteri(hs_gpu_clarity::kGlTexture2D, hs_gpu_clarity::kGlTextureMinFilter, static_cast<int32_t>(hs_gpu_clarity::kGlLinear));
        ops.glTexParameteri(hs_gpu_clarity::kGlTexture2D, hs_gpu_clarity::kGlTextureMagFilter, static_cast<int32_t>(hs_gpu_clarity::kGlLinear));
        pTexImg(hs_gpu_clarity::kGlTexture2D, 0, hs_gpu_clarity::kGlRgba8, w, h, 0, 0x1908u, 0x1401u, pxData.data());
        ops.glBindFramebuffer(hs_gpu_clarity::kGlFramebuffer, outFbo);
        ops.glFramebufferTexture2D(hs_gpu_clarity::kGlFramebuffer, hs_gpu_clarity::kGlColorAttachment0, hs_gpu_clarity::kGlTexture2D, outTex, 0);
    };

    const int32_t W = 1920, H = 1080;
    std::vector<uint8_t> baseRgba, outUhd, outBd;
    FillTestImage(baseRgba, W, H);
    const double baseEdge = MeasureDiagonalEdgeContrast(baseRgba, W, H);
    const double basePatch = MeasureLocalPatchContrast(baseRgba, W, H);

    uint32_t fbo = 0u, tex = 0u;
    makeTestFbo(W, H, baseRgba, fbo, tex);

    // Preserve state probe
    ops.glEnable(hs_gpu_clarity::kGlScissorTest);
    ops.glBindFramebuffer(hs_gpu_clarity::kGlFramebuffer, fbo);

    // Run Grade 2 (12 iterations, measure GPU timer & image quality)
    for (int it = 0; it < 12; ++it) {
        ops.glBindTexture(hs_gpu_clarity::kGlTexture2D, tex);
        pTexImg(hs_gpu_clarity::kGlTexture2D, 0, hs_gpu_clarity::kGlRgba8, W, H, 0, 0x1908u, 0x1401u, baseRgba.data());
        hs_gpu_clarity::ClarityConfig cfg{ 2, 1.0f };
        if (!eng.ProcessFrame(fbo, W, H, cfg)) {
            printf("  [FAIL] Grade 2 ProcessFrame returned false!\n");
            return 1;
        }
    }
    pFinish();
    // Run one more pass so timer query harvests final reading
    ops.glBindTexture(hs_gpu_clarity::kGlTexture2D, tex);
    pTexImg(hs_gpu_clarity::kGlTexture2D, 0, hs_gpu_clarity::kGlRgba8, W, H, 0, 0x1908u, 0x1401u, baseRgba.data());
    eng.ProcessFrame(fbo, W, H, { 2, 1.0f });
    pFinish();

    int32_t chkDrawFbo = 0;
    ops.glGetIntegerv(hs_gpu_clarity::kGlDrawFramebufferBinding, &chkDrawFbo);
    const bool scissorPreserved = (ops.glIsEnabled(hs_gpu_clarity::kGlScissorTest) != 0);
    const float uhdMs = eng.LastGpuMs();
    const uint32_t uhdVramKb = eng.VramExtraKb();
    outUhd.resize(static_cast<size_t>(W) * static_cast<size_t>(H) * 4u);
    ops.glBindFramebuffer(hs_gpu_clarity::kGlReadFramebuffer, fbo);
    pReadPix(0, 0, W, H, 0x1908u, 0x1401u, outUhd.data());
    const uint32_t uhdErr = pGetErr();
    const double uhdEdge = MeasureDiagonalEdgeContrast(outUhd, W, H);
    const double uhdPatch = MeasureLocalPatchContrast(outUhd, W, H);
    uint8_t uhdMaxNearEdge = 0u;
    for (int32_t y = 300; y < 350; ++y) {
        for (int32_t x = 400; x < 450; ++x) {
            uhdMaxNearEdge = std::max(uhdMaxNearEdge, outUhd[(static_cast<size_t>(y) * W + x) * 4u]);
        }
    }
    const bool uhdOk = (uhdErr == 0u && eng.IsActive() && eng.Uses16F() && uhdMs > 0.0f && uhdMs < 3.0f &&
                        uhdEdge > baseEdge * 1.15 && uhdPatch > basePatch && uhdMaxNearEdge <= 240u &&
                        static_cast<uint32_t>(chkDrawFbo) == fbo && scissorPreserved);
    printf("  2. Grade 2 (UltraHD 1x @ 1080p, 16F=%d): gpu=%.3fms vram=%uKB edge=%.1f->%.1f patch=%.2f->%.2f maxR=%u stateOk=%d err=0x%X -> %s\n",
           eng.Uses16F() ? 1 : 0, uhdMs, uhdVramKb, baseEdge, uhdEdge, basePatch, uhdPatch, uhdMaxNearEdge,
           (static_cast<uint32_t>(chkDrawFbo) == fbo && scissorPreserved) ? 1 : 0, uhdErr, uhdOk ? "PASS" : "FAIL");
    if (!uhdOk) return 1;

    // Run Grade 3 (BluRay Virtual-4K 2x EASU + 4x4 Lanczos-2 Resolve @ 1080p)
    for (int it = 0; it < 12; ++it) {
        ops.glBindTexture(hs_gpu_clarity::kGlTexture2D, tex);
        pTexImg(hs_gpu_clarity::kGlTexture2D, 0, hs_gpu_clarity::kGlRgba8, W, H, 0, 0x1908u, 0x1401u, baseRgba.data());
        if (!eng.ProcessFrame(fbo, W, H, { 3, 1.0f })) {
            printf("  [FAIL] Grade 3 ProcessFrame returned false!\n");
            return 1;
        }
    }
    pFinish();
    ops.glBindTexture(hs_gpu_clarity::kGlTexture2D, tex);
    pTexImg(hs_gpu_clarity::kGlTexture2D, 0, hs_gpu_clarity::kGlRgba8, W, H, 0, 0x1908u, 0x1401u, baseRgba.data());
    eng.ProcessFrame(fbo, W, H, { 3, 1.0f });
    pFinish();

    const float bdMs = eng.LastGpuMs();
    const uint32_t bdVramKb = eng.VramExtraKb();
    outBd.resize(static_cast<size_t>(W) * static_cast<size_t>(H) * 4u);
    ops.glBindFramebuffer(hs_gpu_clarity::kGlReadFramebuffer, fbo);
    pReadPix(0, 0, W, H, 0x1908u, 0x1401u, outBd.data());
    const uint32_t bdErr = pGetErr();
    const double bdEdge = MeasureDiagonalEdgeContrast(outBd, W, H);
    const double bdPatch = MeasureLocalPatchContrast(outBd, W, H);
    uint8_t bdMaxNearEdge = 0u;
    for (int32_t y = 300; y < 350; ++y) {
        for (int32_t x = 400; x < 450; ++x) {
            bdMaxNearEdge = std::max(bdMaxNearEdge, outBd[(static_cast<size_t>(y) * W + x) * 4u]);
        }
    }
    const bool bdOk = (bdErr == 0u && eng.IsActive() && bdMs > 0.0f && bdMs < 4.0f &&
                       bdVramKb > 30000u && bdEdge > uhdEdge && bdPatch > uhdPatch && bdMaxNearEdge <= 242u);
    printf("  3. Grade 3 (BluRay Virtual-4K @ 1080p): gpu=%.3fms vram=%uKB edge=%.1f->%.1f patch=%.2f->%.2f maxR=%u err=0x%X -> %s\n",
           bdMs, bdVramKb, baseEdge, bdEdge, basePatch, bdPatch, bdMaxNearEdge, bdErr, bdOk ? "PASS" : "FAIL");
    if (!bdOk) return 1;

    // 4. Odd-resolution live screen stress (2560x1377 non-multiple of 16 -> 5120x2754 Virtual-4K) + Resize + Grade0 VRAM Free
    ops.glDeleteFramebuffers(1, &fbo);
    ops.glDeleteTextures(1, &tex);

    std::vector<uint8_t> rOdd, r1280;
    const int32_t oddW = 2560, oddH = 1377;
    FillTestImage(rOdd, oddW, oddH);
    FillTestImage(r1280, 1280, 720);
    uint32_t fboOdd = 0u, texOdd = 0u, fbo720 = 0u, tex720 = 0u;
    makeTestFbo(oddW, oddH, rOdd, fboOdd, texOdd);

    hs_gpu_clarity::GpuClarityEngine engOdd;
    engOdd.SetOps(ops, true);
    for (int it = 0; it < 12; ++it) {
        ops.glBindTexture(hs_gpu_clarity::kGlTexture2D, texOdd);
        pTexImg(hs_gpu_clarity::kGlTexture2D, 0, hs_gpu_clarity::kGlRgba8, oddW, oddH, 0, 0x1908u, 0x1401u, rOdd.data());
        if (!engOdd.ProcessFrame(fboOdd, oddW, oddH, { 2, 1.0f })) {
            printf("  [FAIL] Odd 2560x1377 Grade 2 failed!\n");
            return 1;
        }
    }
    pFinish();
    engOdd.ProcessFrame(fboOdd, oddW, oddH, { 2, 1.0f });
    pFinish();
    const float oddUhdMs = engOdd.LastGpuMs();
    const uint32_t oddUhdVram = engOdd.VramExtraKb();

    for (int it = 0; it < 12; ++it) {
        ops.glBindTexture(hs_gpu_clarity::kGlTexture2D, texOdd);
        pTexImg(hs_gpu_clarity::kGlTexture2D, 0, hs_gpu_clarity::kGlRgba8, oddW, oddH, 0, 0x1908u, 0x1401u, rOdd.data());
        if (!engOdd.ProcessFrame(fboOdd, oddW, oddH, { 3, 1.0f })) {
            printf("  [FAIL] Odd 2560x1377 Grade 3 failed!\n");
            return 1;
        }
    }
    pFinish();
    engOdd.ProcessFrame(fboOdd, oddW, oddH, { 3, 1.0f });
    pFinish();
    const float oddBdMs = engOdd.LastGpuMs();
    const uint32_t oddBdVram = engOdd.VramExtraKb();
    const bool okOdd = (oddUhdMs > 0.0f && oddBdMs > 0.0f && oddBdMs < 3.5f);

    makeTestFbo(1280, 720, r1280, fbo720, tex720);
    const bool ok720 = engOdd.ProcessFrame(fbo720, 1280, 720, { 2, 1.0f });
    engOdd.ProcessFrame(fbo720, 1280, 720, { 0, 1.0f });
    const bool offClean = !engOdd.IsActive() && engOdd.VramExtraKb() == 0u;
    const uint32_t resErr = pGetErr();

    eng.OnContextReset(true);
    engOdd.OnContextReset(true);
    ops.glDeleteFramebuffers(1, &fboOdd);
    ops.glDeleteTextures(1, &texOdd);
    ops.glDeleteFramebuffers(1, &fbo720);
    ops.glDeleteTextures(1, &tex720);

    const bool allResizeOk = okOdd && ok720 && offClean && (resErr == 0u);
    printf("  4. Odd 2560x1377 (UHD %.3fms/%uKB, BD-5K %.3fms/%uKB) -> 720p + Grade0 VRAM Free: okOdd=%d ok720=%d offClean=%d err=0x%X -> %s\n",
           oddUhdMs, oddUhdVram, oddBdMs, oddBdVram, okOdd ? 1 : 0, ok720 ? 1 : 0, offClean ? 1 : 0, resErr, allResizeOk ? "PASS" : "FAIL");
    if (!allResizeOk) return 1;

    rt.pWglMakeCurrent(nullptr, nullptr);
    rt.pWglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    printf("\n[PASS] ALL LIVE RTX GPU CLARITY COMPUTE & RGBA16F TESTS PASSED!\n");
    return 0;
}
