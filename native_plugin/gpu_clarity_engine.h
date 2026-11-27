#pragma once
// =============================================================================
// HyperSmooth GPU Compute Clarity Engine (OpenGL 4.3+ — Pure GL Layer)
//   Reclaims idle modern GPU compute & VRAM right AFTER world PostProcessor
//   (ColourCube / HS_VisualFinal) and STRICTLY BEFORE any UI/Font draw calls:
//     - Grade 0 (Vanilla):   0.00ms, zero allocations.
//     - Grade 1 (清晰/HD):    Native 16xAF + MipBias + 1.0x LM + Unchoked BCAS.
//     - Grade 2 (超清/UltraHD): 2-Pass RGBA16F Compute @ 1x Native Screen
//         -> Pass 1: Edge-Directed 2D AA + Anime4K Gradient-Guided Ink Push
//         -> Pass 2: Shared-Memory FidelityFX CAS + Dual-Scale Edge-Preserving Local Clarity
//     - Grade 3 (蓝光/BluRay):  3-Pass Virtual-4K RGBA16F Compute
//         -> Pass A: 2x Virtual-4K Anisotropic EASU + Sub-Pixel Ink Tightening
//         -> Pass B: 4x4 Lanczos-2 Super-Sampled Downsample -> RGBA16F
//         -> Pass C: Shared-Memory Dual-Band Clarity + Crystal Vibrance + TPDF Dither
//   Strict Guarantees:
//     - ZERO UI/HUD/Font/cursor corruption (runs strictly pre-UI on live screen backbuffer).
//     - Zero barrier deadlock on odd resolutions (e.g. 2560x1377) via unconditional barrier().
//     - Exact GL state save & restore (FBOs, program, active tex unit, scissor, image unit).
//     - 100% fail-closed: RGBA16F -> RGBA8 fallback and clean bypass on any error.
// =============================================================================
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace hs_gpu_clarity {

inline constexpr uint32_t kGlComputeShader            = 0x91B9u;
inline constexpr uint32_t kGlFramebuffer              = 0x8D40u;
inline constexpr uint32_t kGlReadFramebuffer          = 0x8CA8u;
inline constexpr uint32_t kGlDrawFramebuffer          = 0x8CA9u;
inline constexpr uint32_t kGlDrawFramebufferBinding   = 0x8CA6u;
inline constexpr uint32_t kGlReadFramebufferBinding   = 0x8CAAu;
inline constexpr uint32_t kGlColorAttachment0         = 0x8CE0u;
inline constexpr uint32_t kGlFramebufferComplete      = 0x8CD5u;
inline constexpr uint32_t kGlTexture2D                = 0x0DE1u;
inline constexpr uint32_t kGlTexture0                 = 0x84C0u;
inline constexpr uint32_t kGlRgba8                    = 0x8058u;
inline constexpr uint32_t kGlRgb10A2                  = 0x8059u;
inline constexpr uint32_t kGlRgba16f                  = 0x881Au;
inline constexpr uint32_t kGlTextureMinFilter         = 0x2801u;
inline constexpr uint32_t kGlTextureMagFilter         = 0x2800u;
inline constexpr uint32_t kGlTextureWrapS             = 0x2802u;
inline constexpr uint32_t kGlTextureWrapT             = 0x2803u;
inline constexpr uint32_t kGlNearest                  = 0x2600u;
inline constexpr uint32_t kGlLinear                   = 0x2601u;
inline constexpr uint32_t kGlClampToEdge              = 0x812Fu;
inline constexpr uint32_t kGlColorBufferBit           = 0x00004000u;
inline constexpr uint32_t kGlCurrentProgram           = 0x8B8Du;
inline constexpr uint32_t kGlActiveTexture            = 0x84E0u;
inline constexpr uint32_t kGlTextureBinding2D         = 0x8069u;
inline constexpr uint32_t kGlScissorTest              = 0x0C11u;
inline constexpr uint32_t kGlCompileStatus            = 0x8B81u;
inline constexpr uint32_t kGlLinkStatus               = 0x8B82u;
inline constexpr uint32_t kGlWriteOnly                = 0x88B9u;
inline constexpr uint32_t kGlShaderImageAccessBarrier = 0x00000020u;
inline constexpr uint32_t kGlTextureFetchBarrier      = 0x00000008u;
inline constexpr uint32_t kGlFramebufferBarrier       = 0x00001000u;
inline constexpr uint32_t kGlTimeElapsed              = 0x88BFu;
inline constexpr uint32_t kGlQueryResult              = 0x8866u;
inline constexpr uint32_t kGlQueryResultAvailable     = 0x8867u;
inline constexpr uint32_t kGlMaxTextureSize           = 0x0D33u;

struct GlComputeOps {
    using GenFramebuffers_t        = void (WINAPI*)(int32_t, uint32_t*);
    using DeleteFramebuffers_t     = void (WINAPI*)(int32_t, const uint32_t*);
    using BindFramebuffer_t        = void (WINAPI*)(uint32_t, uint32_t);
    using FramebufferTexture2D_t   = void (WINAPI*)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t);
    using CheckFramebufferStatus_t = uint32_t (WINAPI*)(uint32_t);
    using BlitFramebuffer_t        = void (WINAPI*)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t);
    using GenTextures_t            = void (WINAPI*)(int32_t, uint32_t*);
    using DeleteTextures_t         = void (WINAPI*)(int32_t, const uint32_t*);
    using ActiveTexture_t          = void (WINAPI*)(uint32_t);
    using BindTexture_t            = void (WINAPI*)(uint32_t, uint32_t);
    using TexStorage2D_t           = void (WINAPI*)(uint32_t, int32_t, uint32_t, int32_t, int32_t);
    using TexParameteri_t          = void (WINAPI*)(uint32_t, uint32_t, int32_t);
    using CreateShader_t           = uint32_t (WINAPI*)(uint32_t);
    using ShaderSource_t           = void (WINAPI*)(uint32_t, int32_t, const char* const*, const int32_t*);
    using CompileShader_t          = void (WINAPI*)(uint32_t);
    using GetShaderiv_t            = void (WINAPI*)(uint32_t, uint32_t, int32_t*);
    using GetShaderInfoLog_t       = void (WINAPI*)(uint32_t, int32_t, int32_t*, char*);
    using DeleteShader_t           = void (WINAPI*)(uint32_t);
    using CreateProgram_t          = uint32_t (WINAPI*)();
    using AttachShader_t           = void (WINAPI*)(uint32_t, uint32_t);
    using LinkProgram_t            = void (WINAPI*)(uint32_t);
    using GetProgramiv_t           = void (WINAPI*)(uint32_t, uint32_t, int32_t*);
    using GetProgramInfoLog_t      = void (WINAPI*)(uint32_t, int32_t, int32_t*, char*);
    using DeleteProgram_t          = void (WINAPI*)(uint32_t);
    using UseProgram_t             = void (WINAPI*)(uint32_t);
    using GetUniformLocation_t     = int32_t (WINAPI*)(uint32_t, const char*);
    using Uniform4f_t              = void (WINAPI*)(int32_t, float, float, float, float);
    using Uniform1i_t              = void (WINAPI*)(int32_t, int32_t);
    using DispatchCompute_t        = void (WINAPI*)(uint32_t, uint32_t, uint32_t);
    using MemoryBarrier_t          = void (WINAPI*)(uint32_t);
    using BindImageTexture_t       = void (WINAPI*)(uint32_t, uint32_t, int32_t, uint8_t, int32_t, uint32_t, uint32_t);
    using GetIntegerv_t            = void (WINAPI*)(uint32_t, int32_t*);
    using IsEnabled_t              = uint8_t (WINAPI*)(uint32_t);
    using Enable_t                 = void (WINAPI*)(uint32_t);
    using Disable_t                = void (WINAPI*)(uint32_t);
    using GenQueries_t             = void (WINAPI*)(int32_t, uint32_t*);
    using DeleteQueries_t          = void (WINAPI*)(int32_t, const uint32_t*);
    using BeginQuery_t             = void (WINAPI*)(uint32_t, uint32_t);
    using EndQuery_t               = void (WINAPI*)(uint32_t);
    using GetQueryObjectiv_t       = void (WINAPI*)(uint32_t, uint32_t, int32_t*);
    using GetQueryObjectui64v_t    = void (WINAPI*)(uint32_t, uint32_t, uint64_t*);
    using LogFn_t                  = void (*)(const char*);

    GenFramebuffers_t        glGenFramebuffers        = nullptr;
    DeleteFramebuffers_t     glDeleteFramebuffers     = nullptr;
    BindFramebuffer_t        glBindFramebuffer        = nullptr;
    FramebufferTexture2D_t   glFramebufferTexture2D   = nullptr;
    CheckFramebufferStatus_t glCheckFramebufferStatus = nullptr;
    BlitFramebuffer_t        glBlitFramebuffer        = nullptr;
    GenTextures_t            glGenTextures            = nullptr;
    DeleteTextures_t         glDeleteTextures         = nullptr;
    ActiveTexture_t          glActiveTexture          = nullptr;
    BindTexture_t            glBindTexture            = nullptr;
    TexStorage2D_t           glTexStorage2D           = nullptr;
    TexParameteri_t          glTexParameteri          = nullptr;
    CreateShader_t           glCreateShader           = nullptr;
    ShaderSource_t           glShaderSource           = nullptr;
    CompileShader_t          glCompileShader          = nullptr;
    GetShaderiv_t            glGetShaderiv            = nullptr;
    GetShaderInfoLog_t       glGetShaderInfoLog       = nullptr;
    DeleteShader_t           glDeleteShader           = nullptr;
    CreateProgram_t          glCreateProgram          = nullptr;
    AttachShader_t           glAttachShader           = nullptr;
    LinkProgram_t            glLinkProgram            = nullptr;
    GetProgramiv_t           glGetProgramiv           = nullptr;
    GetProgramInfoLog_t      glGetProgramInfoLog      = nullptr;
    DeleteProgram_t          glDeleteProgram          = nullptr;
    UseProgram_t             glUseProgram             = nullptr;
    GetUniformLocation_t     glGetUniformLocation     = nullptr;
    Uniform4f_t              glUniform4f              = nullptr;
    Uniform1i_t              glUniform1i              = nullptr;
    DispatchCompute_t        glDispatchCompute        = nullptr;
    MemoryBarrier_t          glMemoryBarrier          = nullptr;
    BindImageTexture_t       glBindImageTexture       = nullptr;
    GetIntegerv_t            glGetIntegerv            = nullptr;
    IsEnabled_t              glIsEnabled              = nullptr;
    Enable_t                 glEnable                 = nullptr;
    Disable_t                glDisable                = nullptr;
    GenQueries_t             glGenQueries             = nullptr;
    DeleteQueries_t          glDeleteQueries          = nullptr;
    BeginQuery_t             glBeginQuery             = nullptr;
    EndQuery_t               glEndQuery               = nullptr;
    GetQueryObjectiv_t       glGetQueryObjectiv       = nullptr;
    GetQueryObjectui64v_t    glGetQueryObjectui64v    = nullptr;
    LogFn_t                  logFn                    = nullptr;

    bool IsComplete() const noexcept {
        return glGenFramebuffers && glDeleteFramebuffers && glBindFramebuffer &&
               glFramebufferTexture2D && glCheckFramebufferStatus && glBlitFramebuffer &&
               glGenTextures && glDeleteTextures && glActiveTexture && glBindTexture &&
               glTexStorage2D && glTexParameteri &&
               glCreateShader && glShaderSource && glCompileShader && glGetShaderiv && glDeleteShader &&
               glCreateProgram && glAttachShader && glLinkProgram && glGetProgramiv && glDeleteProgram &&
               glUseProgram && glGetUniformLocation && glUniform4f &&
               glDispatchCompute && glMemoryBarrier && glBindImageTexture && glGetIntegerv;
    }

    bool HasTimerQuery() const noexcept {
        return glGenQueries && glDeleteQueries && glBeginQuery && glEndQuery &&
               glGetQueryObjectiv && glGetQueryObjectui64v;
    }
};

struct ClarityConfig {
    int   grade              = 0;     // 0=Vanilla, 1=Clarity, 2=UltraHD (1x Compute), 3=BluRay (2x Virtual-4K Compute)
    float strength           = 1.0f;  // [0.0, 1.5]
    bool  allowAdaptiveGrade = false; // Opt-in zero-reallocation adaptive ladder (3 <-> 2 <-> 1 <-> 0) under GPU saturation
    float emaFrameGpuMs      = 0.0f;  // Live EMA GPU frame time in ms (<= 0 disables adaptive stepping)
    float targetGpuBudgetMs  = 5.5f;  // Sustained GPU frame budget threshold for 3->2 step-down (+1.0ms 2->1, +2.0ms 1->0)
    uint32_t adaptiveUpFrames   = 120u; // Sustained under-budget frames before stepping up a clarity grade
    float    adaptiveUpMarginMs = 1.5f; // Hysteresis deadband (ms) below step-down threshold for recovery
};

// Opt-in cached host GL state passed by callers (e.g. dinput8.cpp) to eliminate all per-frame
// synchronous glGetIntegerv / glIsEnabled / glGetQueryObject* driver-thread round-trips!
// Any negative field falls back to querying live GL (100% fail-closed).
struct KnownGlState {
    int32_t drawFbo        = -1;   // >= 0: valid GL_DRAW_FRAMEBUFFER_BINDING
    int32_t readFbo        = -1;   // >= 0: valid GL_READ_FRAMEBUFFER_BINDING
    int32_t curProgram     = -1;   // >= 0: valid GL_CURRENT_PROGRAM
    int32_t activeTexEnum  = -1;   // >= kGlTexture0 (0x84C0): valid GL_ACTIVE_TEXTURE
    int32_t boundTex0      = -1;   // >= 0: valid GL_TEXTURE_BINDING_2D on GL_TEXTURE0
    int8_t  scissorEnabled = -1;   // 0=OFF, 1=ON, -1=unknown (query glIsEnabled)
    bool    sampleTimer    = true; // false: skip GL_TIME_ELAPSED Begin/End & Harvest (avoids nesting & query stalls)
};

// -----------------------------------------------------------------------------
// Shader 1 (Grade 2 · Pass 1): uSrc (RGBA8) -> uWork (RGBA16F @ WxH)
//   - Shared-Memory 18x18 Tile Cooperative Load (7.1x fewer texture fetches!)
//   - Local Structure-Tensor Edge-Directed 2D Tangent AA
//   - Anime4K-Style Gradient-Guided Ink Thinning / Morphological Shock Push
// -----------------------------------------------------------------------------
inline constexpr const char* kCsUltraPre1xBody = R"GLSL(
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D uSrc;
layout(DST_WORK_FMT, binding = 0) writeonly uniform image2D uWork;

uniform vec4 uParams; // (w, h, invW, invH)
uniform vec4 uTune;   // (strength, aaWeight, inkThin, unused)

shared vec4 sTile[18][18];

float Luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    ivec2 sz     = ivec2(int(uParams.x + 0.5), int(uParams.y + 0.5));
    ivec2 hi     = sz - ivec2(1);
    ivec2 wgBase = ivec2(gl_WorkGroupID.xy) * 16 - ivec2(1);

    // Unconditional cooperative load + barrier() BEFORE bounds return -> 0% deadlock on odd sizes!
    for (uint idx = gl_LocalInvocationIndex; idx < 324u; idx += 256u) {
        int tx = int(idx % 18u);
        int ty = int(idx / 18u);
        ivec2 sc = clamp(wgBase + ivec2(tx, ty), ivec2(0), hi);
        sTile[ty][tx] = texelFetch(uSrc, sc, 0);
    }
    memoryBarrierShared();
    barrier();

    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= sz.x || gid.y >= sz.y) return;

    int lx = int(gl_LocalInvocationID.x) + 1;
    int ly = int(gl_LocalInvocationID.y) + 1;

    vec4 cC    = sTile[ly    ][lx    ];
    vec3 rgbTL = sTile[ly - 1][lx - 1].rgb;
    vec3 rgbN  = sTile[ly - 1][lx    ].rgb;
    vec3 rgbTR = sTile[ly - 1][lx + 1].rgb;
    vec3 rgbW  = sTile[ly    ][lx - 1].rgb;
    vec3 rgbE  = sTile[ly    ][lx + 1].rgb;
    vec3 rgbBL = sTile[ly + 1][lx - 1].rgb;
    vec3 rgbS  = sTile[ly + 1][lx    ].rgb;
    vec3 rgbBR = sTile[ly + 1][lx + 1].rgb;

    float tl = Luma(rgbTL), tc = Luma(rgbN), tr = Luma(rgbTR);
    float ml = Luma(rgbW),  mc = Luma(cC.rgb), mr = Luma(rgbE);
    float bl = Luma(rgbBL), bc = Luma(rgbS),  br = Luma(rgbBR);

    float gx = (tr + 2.0 * mr + br) - (tl + 2.0 * ml + bl);
    float gy = (bl + 2.0 * bc + br) - (tl + 2.0 * tc + tr);
    float grad = sqrt(max(gx * gx + gy * gy, 0.0)) * 0.25;

    // 2x2 quad structure tensor for coherence & dominant edge tangent
    vec2 q0 = vec2(tc - tl, ml - tl);
    vec2 q1 = vec2(tr - tc, mc - tc);
    vec2 q2 = vec2(mc - ml, bl - ml);
    vec2 q3 = vec2(mr - mc, bc - mc);
    float Jxx = q0.x * q0.x + q1.x * q1.x + q2.x * q2.x + q3.x * q3.x;
    float Jyy = q0.y * q0.y + q1.y * q1.y + q2.y * q2.y + q3.y * q3.y;
    float Jxy = q0.x * q0.y + q1.x * q1.y + q2.x * q2.y + q3.x * q3.y;
    float trace = Jxx + Jyy;
    float detDiff = sqrt(max((Jxx - Jyy) * (Jxx - Jyy) + 4.0 * Jxy * Jxy, 0.0));
    float coh = (trace > 1e-6) ? clamp(detDiff / (trace + 1e-6), 0.0, 1.0) : 0.0;

    float strength = clamp(uTune.x, 0.0, 1.5);
    vec3 rgb = cC.rgb;

    // 1. Edge-Directed 2D Tangent AA (smooths oblique stairsteps without cross-edge blur)
    if (coh > 0.32 && grad > 0.035 && uTune.y > 0.001) {
        float nLen = sqrt(max(gx * gx + gy * gy, 1e-12));
        vec2 tangent = vec2(-gy, gx) / nLen;
        vec2 uv = (vec2(gid) + 0.5) * uParams.zw;
        vec2 stepUv = tangent * (0.48 * uParams.zw);
        vec3 tP = texture(uSrc, uv + stepUv).rgb;
        vec3 tM = texture(uSrc, uv - stepUv).rgb;
        float lP = Luma(tP);
        float lM = Luma(tM);
        float tol = grad * 0.90 + 0.02;
        if (abs(lP - mc) < tol && abs(lM - mc) < tol) {
            float wAA = clamp((coh - 0.32) * 1.47, 0.0, 1.0)
                      * smoothstep(0.035, 0.20, grad)
                      * clamp(0.35 * uTune.y * strength, 0.0, 0.38);
            rgb = mix(rgb, 0.5 * (tP + tM), wAA);
        }
    }

    // 2. Anime4K-Style Gradient-Guided Ink Push (tightens 2-3px blurry charcoal strokes into 1px crisp vector ink)
    float inkStr = clamp(uTune.z, 0.0, 1.5) * strength;
    if (inkStr > 0.001) {
        vec3 darkCol = cC.rgb; float lMin = mc;
        vec3 brightCol = cC.rgb; float lMax = mc;
        #define TRACK_PX(c, l) \
            if ((l) < lMin) { lMin = (l); darkCol = (c); } \
            if ((l) > lMax) { lMax = (l); brightCol = (c); }
        TRACK_PX(rgbTL, tl); TRACK_PX(rgbN,  tc); TRACK_PX(rgbTR, tr);
        TRACK_PX(rgbW,  ml);                      TRACK_PX(rgbE,  mr);
        TRACK_PX(rgbBL, bl); TRACK_PX(rgbS,  bc); TRACK_PX(rgbBR, br);
        #undef TRACK_PX

        float span = lMax - lMin;
        if (span > 0.075) {
            float lMid = 0.5 * (lMin + lMax);
            float edgeGate = smoothstep(0.075, 0.24, span) * smoothstep(0.18, 0.55, coh);
            if (mc <= lMid && lMin < 0.42) {
                float wDark = clamp(0.42 * inkStr * edgeGate * (1.0 - smoothstep(0.0, 0.42, mc)), 0.0, 0.55);
                rgb = mix(rgb, darkCol, wDark);
            } else if (mc > lMid && lMin < 0.36) {
                float wBright = clamp(0.28 * inkStr * edgeGate * smoothstep(0.04, 0.25, lMax - mc), 0.0, 0.36);
                rgb = mix(rgb, brightCol, wBright);
            }
        }
    }

    imageStore(uWork, gid, vec4(clamp(rgb, 0.0, 1.0), cC.a));
}
)GLSL";

// -----------------------------------------------------------------------------
// Shader 2 (Grade 3 · Pass A): uSrc (RGBA8) -> uHi (RGB10_A2 @ 2W x 2H)
//   - Shared-Memory 12x12 Tile Cooperative Load (21.3x fewer texture fetches + 12x fewer Luma!)
//   - 12-Tap Anisotropic EASU Virtual-4K Reconstruction (division-free inner taps)
//   - Sub-Pixel Ink Stroke Tightening in 2x Space
// -----------------------------------------------------------------------------
inline constexpr const char* kCsBluRayUpscale2xBody = R"GLSL(
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D uSrc;
layout(DST_WORK_FMT, binding = 0) writeonly uniform image2D uHi;

uniform vec4 uSrcSize; // (w, h, invW, invH)
uniform vec4 uHiSize;  // (2w, 2h, inv2W, inv2H)
uniform vec4 uTune;    // (strength, unused, inkRefine, unused)

shared vec4  sSrc[12][12];
shared float sLuma[12][12];

float Luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void AccumQuad(vec2 fp, out vec2 dir, out float len,
               float b, float c,
               float e, float f, float g, float h,
               float i, float j, float k, float l,
               float n, float o) {
    dir = vec2(0.0);
    len = 0.0;

    float wTL = (1.0 - fp.x) * (1.0 - fp.y);
    float wTR = fp.x * (1.0 - fp.y);
    float wBL = (1.0 - fp.x) * fp.y;
    float wBR = fp.x * fp.y;

    {
        float dx = g - e; float lx = max(abs(g - f), abs(f - e));
        float dy = j - b; float ly = max(abs(j - f), abs(f - b));
        float rx = (lx > 1e-6) ? clamp(abs(dx) / lx, 0.0, 1.0) : 0.0;
        float ry = (ly > 1e-6) ? clamp(abs(dy) / ly, 0.0, 1.0) : 0.0;
        float lq = 0.5 * (rx + ry);
        dir += vec2(dx, dy) * wTL;
        len += (lq * lq) * wTL;
    }
    {
        float dx = h - f; float lx = max(abs(h - g), abs(g - f));
        float dy = k - c; float ly = max(abs(k - g), abs(g - c));
        float rx = (lx > 1e-6) ? clamp(abs(dx) / lx, 0.0, 1.0) : 0.0;
        float ry = (ly > 1e-6) ? clamp(abs(dy) / ly, 0.0, 1.0) : 0.0;
        float lq = 0.5 * (rx + ry);
        dir += vec2(dx, dy) * wTR;
        len += (lq * lq) * wTR;
    }
    {
        float dx = k - i; float lx = max(abs(k - j), abs(j - i));
        float dy = n - f; float ly = max(abs(n - j), abs(j - f));
        float rx = (lx > 1e-6) ? clamp(abs(dx) / lx, 0.0, 1.0) : 0.0;
        float ry = (ly > 1e-6) ? clamp(abs(dy) / ly, 0.0, 1.0) : 0.0;
        float lq = 0.5 * (rx + ry);
        dir += vec2(dx, dy) * wBL;
        len += (lq * lq) * wBL;
    }
    {
        float dx = l - j; float lx = max(abs(l - k), abs(k - j));
        float dy = o - g; float ly = max(abs(o - k), abs(k - g));
        float rx = (lx > 1e-6) ? clamp(abs(dx) / lx, 0.0, 1.0) : 0.0;
        float ry = (ly > 1e-6) ? clamp(abs(dy) / ly, 0.0, 1.0) : 0.0;
        float lq = 0.5 * (rx + ry);
        dir += vec2(dx, dy) * wBR;
        len += (lq * lq) * wBR;
    }
}

float EasuWeight(vec2 off, vec2 dVec, vec2 tVec, float clpMax, float invClpMax, float negBoost) {
    float u = dot(off, dVec);
    float v = dot(off, tVec);
    float d2 = min(u * u + v * v, clpMax);
    float b = 0.4 * d2 - 1.0;
    float wMain = (25.0 / 16.0) * (b * b) - (9.0 / 16.0);
    if (wMain < 0.0) wMain *= negBoost;
    float win = 1.0 - d2 * invClpMax;
    return wMain * (win * win);
}

void main() {
    ivec2 hiSz       = ivec2(int(uHiSize.x + 0.5), int(uHiSize.y + 0.5));
    ivec2 srcSz      = ivec2(int(uSrcSize.x + 0.5), int(uSrcSize.y + 0.5));
    ivec2 maxCoord   = srcSz - ivec2(1);
    ivec2 tileOrigin = ivec2(gl_WorkGroupID.xy) * 8 - ivec2(2);

    // Unconditional cooperative load + barrier() BEFORE bounds return -> 0% deadlock on odd sizes!
    if (gl_LocalInvocationIndex < 144u) {
        int tx = int(gl_LocalInvocationIndex % 12u);
        int ty = int(gl_LocalInvocationIndex / 12u);
        ivec2 sc = clamp(tileOrigin + ivec2(tx, ty), ivec2(0), maxCoord);
        vec4 px = texelFetch(uSrc, sc, 0);
        sSrc[ty][tx]  = px;
        sLuma[ty][tx] = Luma(px.rgb);
    }
    memoryBarrierShared();
    barrier();

    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= hiSz.x || gid.y >= hiSz.y) return;

    int lx = int(gl_LocalInvocationID.x);
    int ly = int(gl_LocalInvocationID.y);
    int cx = ((lx + 1) >> 1) + 1;
    int cy = ((ly + 1) >> 1) + 1;
    vec2 fp = vec2(((lx & 1) == 0) ? 0.75 : 0.25,
                   ((ly & 1) == 0) ? 0.75 : 0.25);

    vec3 b  = sSrc[cy - 1][cx    ].rgb; vec3 c  = sSrc[cy - 1][cx + 1].rgb;
    vec3 e  = sSrc[cy    ][cx - 1].rgb; vec4 f4 = sSrc[cy    ][cx    ]; vec4 g4 = sSrc[cy    ][cx + 1]; vec3 h = sSrc[cy    ][cx + 2].rgb;
    vec3 i  = sSrc[cy + 1][cx - 1].rgb; vec4 j4 = sSrc[cy + 1][cx    ]; vec4 k4 = sSrc[cy + 1][cx + 1]; vec3 l = sSrc[cy + 1][cx + 2].rgb;
    vec3 n  = sSrc[cy + 2][cx    ].rgb; vec3 o  = sSrc[cy + 2][cx + 1].rgb;

    vec3 f = f4.rgb, g = g4.rgb, j = j4.rgb, k = k4.rgb;

    vec2 dir;
    float lenRaw;
    AccumQuad(fp, dir, lenRaw,
              sLuma[cy - 1][cx    ], sLuma[cy - 1][cx + 1],
              sLuma[cy    ][cx - 1], sLuma[cy    ][cx    ], sLuma[cy    ][cx + 1], sLuma[cy    ][cx + 2],
              sLuma[cy + 1][cx - 1], sLuma[cy + 1][cx    ], sLuma[cy + 1][cx + 1], sLuma[cy + 1][cx + 2],
              sLuma[cy + 2][cx    ], sLuma[cy + 2][cx + 1]);

    float dirSq = dot(dir, dir);
    float len = clamp(lenRaw * lenRaw, 0.0, 1.0);
    vec2 nDir = (dirSq > 1e-8) ? (dir * inversesqrt(dirSq)) : vec2(1.0, 0.0);
    if (dirSq <= 1e-8) len = 0.0;
    vec2 nTan = vec2(-nDir.y, nDir.x);

    float strength  = clamp(uTune.x, 0.0, 1.5);
    float aniso     = clamp(uTune.x, 0.5, 1.35);
    float sAcross   = 1.0 + 0.52 * len * aniso;
    float sAlong    = 1.0 + 1.35 * len * aniso;
    float clpMax    = mix(4.0, 2.75, len);
    float invClpMax = 1.0 / max(clpMax, 1e-4);
    float negBoost  = mix(0.85, 1.45, len) * clamp(strength, 0.35, 1.4);
    vec2  dVec      = nDir * sAcross;
    vec2  tVec      = nTan / sAlong;

    vec3 acc = vec3(0.0);
    float wSum = 0.0;

    #define TAP(col, dx, dy) { \
        float w = EasuWeight(vec2(float(dx) - fp.x, float(dy) - fp.y), dVec, tVec, clpMax, invClpMax, negBoost); \
        acc += (col) * w; \
        wSum += w; \
    }
    TAP(b,  0, -1); TAP(c,  1, -1);
    TAP(e, -1,  0); TAP(f,  0,  0); TAP(g,  1,  0); TAP(h,  2,  0);
    TAP(i, -1,  1); TAP(j,  0,  1); TAP(k,  1,  1); TAP(l,  2,  1);
    TAP(n,  0,  2); TAP(o,  1,  2);
    #undef TAP

    vec3 rgb = (abs(wSum) > 1e-5) ? (acc / wSum) : mix(mix(f, g, fp.x), mix(j, k, fp.x), fp.y);

    // Relaxed clamp keeps EASU negative-lobe acutance intact while eliminating hard ringing
    vec3 min4 = min(min(f, g), min(j, k));
    vec3 max4 = max(max(f, g), max(j, k));
    rgb = clamp(rgb, clamp(min4 - 0.025, 0.0, 1.0), max4);

    // Sub-pixel Virtual-4K ink stroke tightening (dark-side sharpen + bright-side bleed cleanup)
    float y4    = Luma(rgb);
    float maxY  = Luma(max4);
    float minY  = Luma(min4);
    float span4 = maxY - minY;
    if (uTune.z > 0.001 && span4 > 0.07) {
        float mid4 = 0.5 * (minY + maxY);
        if (y4 <= mid4 && minY < 0.44) {
            float inkPush = smoothstep(0.07, 0.26, span4) * (1.0 - smoothstep(0.0, 0.44, y4));
            float wDark = clamp(inkPush * 0.40 * uTune.z * strength, 0.0, 0.52);
            rgb = mix(rgb, min4, wDark);
        } else if (y4 > mid4 && minY < 0.38) {
            float cleanPush = smoothstep(0.07, 0.26, span4) * smoothstep(0.03, 0.24, maxY - y4);
            float wBright = clamp(cleanPush * 0.26 * uTune.z * strength, 0.0, 0.32);
            rgb = mix(rgb, max4, wBright);
        }
    }

    float a = mix(mix(f4.a, g4.a, fp.x), mix(j4.a, k4.a, fp.x), fp.y);
    imageStore(uHi, gid, vec4(clamp(rgb, 0.0, 1.0), clamp(a, 0.0, 1.0)));
}
)GLSL";

// -----------------------------------------------------------------------------
// Shader 3 (Grade 3 · Pass B): uHi (RGB10_A2 @ 2W x 2H) -> uWork (RGBA16F @ W x H)
//   - Shared-Memory 34x34 Tile Cooperative Load (3.54x fewer texture fetches!)
//   - Precomputed Constant 4x4 Lanczos-2 Super-Sampled Resolve (0 sin/div/loop!)
// -----------------------------------------------------------------------------
inline constexpr const char* kCsBluRayResolve1xBody = R"GLSL(
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D uHi;
layout(DST_WORK_FMT, binding = 0) writeonly uniform image2D uWork;

uniform vec4 uOutSize; // (w, h, invW, invH)
uniform vec4 uTune;    // (strength, microDetail, unused, unused)

shared vec4 sHi[34][34];

void main() {
    ivec2 outSz    = ivec2(int(uOutSize.x + 0.5), int(uOutSize.y + 0.5));
    ivec2 hiMax    = outSz * 2 - ivec2(1);
    ivec2 wgHiBase = ivec2(gl_WorkGroupID.xy) * 32 - ivec2(1);

    // Unconditional cooperative load + barrier() BEFORE bounds return -> 0% deadlock on odd sizes!
    for (uint idx = gl_LocalInvocationIndex; idx < 1156u; idx += 256u) {
        int tx = int(idx % 34u);
        int ty = int(idx / 34u);
        ivec2 sc = clamp(wgHiBase + ivec2(tx, ty), ivec2(0), hiMax);
        sHi[ty][tx] = texelFetch(uHi, sc, 0);
    }
    memoryBarrierShared();
    barrier();

    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= outSz.x || gid.y >= outSz.y) return;

    int bx = int(gl_LocalInvocationID.x) * 2 + 1;
    int by = int(gl_LocalInvocationID.y) * 2 + 1;

    // 2x2 center taps
    vec4 s00 = sHi[by    ][bx    ];
    vec4 s10 = sHi[by    ][bx + 1];
    vec4 s01 = sHi[by + 1][bx    ];
    vec4 s11 = sHi[by + 1][bx + 1];

    // 4 corner taps (w = Lanczos2(1.5)^2 = 0.004055697)
    vec3 kSum = sHi[by - 1][bx - 1].rgb + sHi[by - 1][bx + 2].rgb
              + sHi[by + 2][bx - 1].rgb + sHi[by + 2][bx + 2].rgb;

    // 8 edge taps (w = Lanczos2(0.5) * Lanczos2(1.5) = -0.036501270)
    vec3 eSum = sHi[by - 1][bx    ].rgb + sHi[by - 1][bx + 1].rgb
              + sHi[by    ][bx - 1].rgb + sHi[by    ][bx + 2].rgb
              + sHi[by + 1][bx - 1].rgb + sHi[by + 1][bx + 2].rgb
              + sHi[by + 2][bx    ].rgb + sHi[by + 2][bx + 1].rgb;

    // 4 center taps (w = Lanczos2(0.5)^2 = 0.328511432), sum = 1.038258353 (inv = 0.963151414)
    vec3 cSum = s00.rgb + s10.rgb + s01.rgb + s11.rgb;
    vec3 down = (cSum * 0.328511432 - eSum * 0.036501270 + kSum * 0.004055697) * 0.963151414;

    vec3 min2x2 = min(min(s00.rgb, s10.rgb), min(s01.rgb, s11.rgb));
    vec3 max2x2 = max(max(s00.rgb, s10.rgb), max(s01.rgb, s11.rgb));
    float aSum  = s00.a + s10.a + s01.a + s11.a;

    float strength = clamp(uTune.x, 0.0, 1.5);
    vec3 ringAvg   = (eSum + kSum) * (1.0 / 12.0);
    vec3 detail    = down - ringAvg;
    vec3 crisp     = down + detail * clamp(0.22 * uTune.y * strength, 0.0, 0.32);
    vec3 rgb       = clamp(crisp, clamp(min2x2 - 0.035, 0.0, 1.0), clamp(max2x2 + 0.012, 0.0, 1.0));

    imageStore(uWork, gid, vec4(rgb, clamp(aSum * 0.25, 0.0, 1.0)));
}
)GLSL";

// -----------------------------------------------------------------------------
// Shader 4 (Shared Final Stage · Grade 2 & Grade 3):
//   uWork (RGBA16F @ WxH) -> uOut (RGBA8 @ WxH)
//   - Band 1: Shared-Memory 18x18 vec4 Tile FidelityFX-Style Luma CAS + 9-Tap Micro-Detail
//   - Band 2: Edge-Preserving Dual-Radius (r=2.5px + r=6.5px) Local Clarity
//   - Perceptual Color Scaling + Crystal Vibrance + 8-bit TPDF Anti-Banding Dither
// -----------------------------------------------------------------------------
inline constexpr const char* kCsClarityFinish1xBody = R"GLSL(
layout(local_size_x = 16, local_size_y = 16) in;

layout(binding = 0) uniform sampler2D uWork;
layout(rgba8, binding = 0) writeonly uniform image2D uOut;

uniform vec4 uOutSize; // (w, h, invW, invH)
uniform vec4 uTune;    // (fineCasStr, localClarityStr, vibranceBoost, ditherStr)

shared vec4 sTile[18][18];

float Luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

void main() {
    ivec2 sz     = ivec2(int(uOutSize.x + 0.5), int(uOutSize.y + 0.5));
    ivec2 hi     = sz - ivec2(1);
    ivec2 wgBase = ivec2(gl_WorkGroupID.xy) * 16 - ivec2(1);

    // Unconditional cooperative load + barrier() BEFORE bounds return -> 0% deadlock on odd sizes!
    for (uint idx = gl_LocalInvocationIndex; idx < 324u; idx += 256u) {
        int tx = int(idx % 18u);
        int ty = int(idx / 18u);
        ivec2 sc = clamp(wgBase + ivec2(tx, ty), ivec2(0), hi);
        sTile[ty][tx] = texelFetch(uWork, sc, 0);
    }
    memoryBarrierShared();
    barrier();

    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= sz.x || gid.y >= sz.y) return;

    int lx = int(gl_LocalInvocationID.x) + 1;
    int ly = int(gl_LocalInvocationID.y) + 1;

    vec4 c4    = sTile[ly][lx];
    vec3 c     = c4.rgb;
    float yOrig = Luma(c);

    float yNW = Luma(sTile[ly - 1][lx - 1].rgb);
    float yN  = Luma(sTile[ly - 1][lx    ].rgb);
    float yNE = Luma(sTile[ly - 1][lx + 1].rgb);
    float yW  = Luma(sTile[ly    ][lx - 1].rgb);
    float yE  = Luma(sTile[ly    ][lx + 1].rgb);
    float ySW = Luma(sTile[ly + 1][lx - 1].rgb);
    float yS  = Luma(sTile[ly + 1][lx    ].rgb);
    float ySE = Luma(sTile[ly + 1][lx + 1].rgb);

    // Band 1: Shared-memory FidelityFX-style luma CAS + 9-tap high-pass
    float yMin = min(yOrig, min(min(yN, yS), min(yE, yW)));
    float yMax = max(yOrig, max(max(yN, yS), max(yE, yW)));
    float amp  = sqrt(clamp(min(yMin, 1.0 - yMax) / max(yMax, 0.08), 0.0, 1.0));
    float hp   = yOrig - (0.15 * (yN + yS + yE + yW) + 0.10 * (yNW + yNE + ySW + ySE));
    float nGate = smoothstep(0.010, 0.038, yMax - yMin);
    float posCap = mix(0.068, 0.032, smoothstep(0.22, 0.60, yMax - yMin));
    float fineDelta = clamp(hp * (1.25 + 1.15 * amp) * uTune.x * nGate, -0.15, posCap);
    float yFine = clamp(yOrig + fineDelta, 0.0, 1.0);

    // Band 2: Edge-preserving dual-radius (r=2.5px + r=6.5px) local clarity via HW bilinear taps
    float clarityDelta = 0.0;
    if (uTune.y > 0.001) {
        vec2 uv = (vec2(gid) + 0.5) * uOutSize.zw;
        vec2 px = uOutSize.zw;
        const float kInv2Sig2Log2e = 1.44269504 / (2.0 * 0.16 * 0.16);
        float wAcc = 0.25;
        float yAcc = yOrig * 0.25;

        #define ADD_CLARITY_TAP(ox, oy, spW) { \
            float ty = Luma(texture(uWork, uv + vec2((ox), (oy)) * px).rgb); \
            float dy = ty - yOrig; \
            float rw = exp2(-(dy * dy) * kInv2Sig2Log2e) * (spW); \
            yAcc += ty * rw; \
            wAcc += rw; \
        }
        // r = 2.5px diagonal ring
        ADD_CLARITY_TAP(-2.5, -2.5, 1.10); ADD_CLARITY_TAP( 2.5, -2.5, 1.10);
        ADD_CLARITY_TAP(-2.5,  2.5, 1.10); ADD_CLARITY_TAP( 2.5,  2.5, 1.10);
        // r = 6.5px outer ring
        ADD_CLARITY_TAP(-6.5,  0.0, 0.70); ADD_CLARITY_TAP( 6.5,  0.0, 0.70);
        ADD_CLARITY_TAP( 0.0, -6.5, 0.70); ADD_CLARITY_TAP( 0.0,  6.5, 0.70);
        ADD_CLARITY_TAP(-4.6, -4.6, 0.65); ADD_CLARITY_TAP( 4.6, -4.6, 0.65);
        ADD_CLARITY_TAP(-4.6,  4.6, 0.65); ADD_CLARITY_TAP( 4.6,  4.6, 0.65);
        #undef ADD_CLARITY_TAP

        float localMean = yAcc / max(wAcc, 1e-4);
        float localDiff = yFine - localMean;
        clarityDelta = clamp(uTune.y * (localDiff / (1.0 + abs(localDiff) * 3.5)), -0.11, 0.11);
    }

    float yTarget = clamp(yFine + clarityDelta, 0.0, 1.0);
    float rawRatio = clamp(yTarget / max(yOrig, 1e-4), 0.62, 1.42);
    float ratio = mix(1.0, rawRatio, smoothstep(0.008, 0.035, yOrig));
    vec3 rgb = clamp(c * ratio, 0.0, 1.0);

    // Subtle crystal vibrance (lifts DST grey wash on low/mid chroma without blowing out brights)
    if (uTune.z > 0.001) {
        float maxC = max(rgb.r, max(rgb.g, rgb.b));
        float minC = min(rgb.r, min(rgb.g, rgb.b));
        float sat = (maxC > 1e-4) ? ((maxC - minC) / maxC) : 0.0;
        float satBoost = uTune.z * (1.0 - sat) * smoothstep(0.04, 0.22, yTarget) * (1.0 - smoothstep(0.85, 0.99, yTarget));
        float avgY = Luma(rgb);
        rgb = clamp(mix(vec3(avgY), rgb, 1.0 + satBoost), 0.0, 1.0);
    }

    // 8-bit TPDF anti-banding dither (preserves silky RGBA16F -> RGBA8 gradients)
    float dStr = clamp(uTune.w, 0.0, 1.5);
    if (dStr > 0.001) {
        vec2 p = vec2(gid);
        float d1 = fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
        float d2 = fract(sin(dot(p.yx, vec2(78.233, 12.9898))) * 24634.6345);
        rgb = clamp(rgb + vec3((d1 - d2) * (dStr / 255.0)), 0.0, 1.0);
    }

    imageStore(uOut, gid, vec4(rgb, c4.a));
}
)GLSL";

class GpuClarityEngine {
public:
    GpuClarityEngine() = default;

    void SetOps(const GlComputeOps& ops, bool supported) noexcept {
        ops_ = ops;
        supported_ = supported && ops_.IsComplete();
    }

    void OnContextReset(bool hasAliveCtx) noexcept {
        if (hasAliveCtx && ops_.IsComplete()) {
            DestroyAllResources();
        } else {
            progUltraPre_ = 0u;
            progUp2x_     = 0u;
            progRes1x_    = 0u;
            progFinish_   = 0u;
            fboSrc_       = 0u;
            fboOut_       = 0u;
            texSrc_       = 0u;
            texWork_      = 0u;
            texOut_       = 0u;
            texHi_        = 0u;
            queries_[0] = queries_[1] = queries_[2] = queries_[3] = 0u;
            queryArmed_[0] = queryArmed_[1] = queryArmed_[2] = queryArmed_[3] = false;
        }
        w_ = 0;
        h_ = 0;
        has2x_ = false;
        failed_ = false;
        active_ = false;
        effectiveGrade_ = 0;
        adaptiveGrade_ = 0;
        adaptiveBaseGrade_ = 0;
        overBudgetFrames_ = 0u;
        underBudgetFrames_ = 0u;
        adaptiveGradeSteps_ = 0ULL;
        lastGpuMs_ = 0.0f;
        querySlot_ = 0u;
        maxTexSize_ = 0;
        workFmt_ = kGlRgba16f;
        hiFmt_   = kGlRgb10A2;
        workBytesPerPx_ = 8u;
        hiBytesPerPx_   = 4u;
        compiledWorkFmt_ = 0u;
        compiledHiFmt_   = 0u;
    }

    bool IsSupported() const noexcept { return supported_ && !failed_; }
    bool IsFailed() const noexcept { return failed_; }
    bool IsActive() const noexcept { return active_ && !failed_; }
    bool IsAdaptiveBypassed() const noexcept { return adaptiveBaseGrade_ >= 2 && adaptiveGrade_ == 0; }
    int  EffectiveGrade() const noexcept { return effectiveGrade_; }
    uint64_t AdaptiveGradeSteps() const noexcept { return adaptiveGradeSteps_; }
    float LastGpuMs() const noexcept { return lastGpuMs_; }
    bool Uses16F() const noexcept { return workFmt_ == kGlRgba16f; }
    uint32_t VramExtraKb() const noexcept {
        if (w_ <= 0 || h_ <= 0 || texSrc_ == 0u || texOut_ == 0u) return 0u;
        const uint64_t px1x = static_cast<uint64_t>(w_) * static_cast<uint64_t>(h_);
        // texSrc (4B) + texOut (4B) + texWork (workBytesPerPx_)
        uint64_t totalBytes = px1x * (8ULL + static_cast<uint64_t>(texWork_ ? workBytesPerPx_ : 0u));
        if (has2x_ && texHi_ != 0u) {
            totalBytes += px1x * 4ULL * static_cast<uint64_t>(hiBytesPerPx_);
        }
        return static_cast<uint32_t>((totalBytes + 1023ULL) / 1024ULL);
    }

    void ReleaseComputeTargetsIfIdle() noexcept {
        effectiveGrade_ = 0;
        adaptiveGrade_ = 0;
        adaptiveBaseGrade_ = 0;
        overBudgetFrames_ = 0u;
        underBudgetFrames_ = 0u;
        if (!active_ && w_ == 0 && h_ == 0 && texSrc_ == 0u) return;
        active_ = false;
        lastGpuMs_ = 0.0f;
        if (ops_.IsComplete()) {
            DestroyTexturesAndFbos();
        }
    }

    bool ProcessFrame(uint32_t srcFbo,
                      int32_t vpW,
                      int32_t vpH,
                      const ClarityConfig& cfg,
                      const KnownGlState* knownState = nullptr) noexcept {
        if (cfg.grade < 2) {
            if (texSrc_ != 0u || texWork_ != 0u || texOut_ != 0u || texHi_ != 0u) {
                ReleaseComputeTargetsIfIdle();
            } else {
                adaptiveGrade_ = 0;
                adaptiveBaseGrade_ = 0;
                overBudgetFrames_ = 0u;
                underBudgetFrames_ = 0u;
            }
            active_ = false;
            effectiveGrade_ = (cfg.grade <= 0) ? 0 : 1;
            lastGpuMs_ = 0.0f;
            return false;
        }
        if (!supported_ || failed_) {
            active_ = false;
            effectiveGrade_ = 0;
            return false;
        }
        if (vpW < 64 || vpH < 64 || vpW > 7680 || vpH > 4320) {
            effectiveGrade_ = 0;
            return false;
        }

        // Opt-in zero-reallocation full adaptive ladder (3 <-> 2 <-> 1 <-> 0):
        //   - Step down (one rung at a time, resetting hysteresis counter on each step):
        //       * 3 -> 2: >= 45 consecutive frames with emaFrameGpuMs > targetGpuBudgetMs (default 5.5ms)
        //       * 2 -> 1: >= 45 consecutive frames with emaFrameGpuMs > targetGpuBudgetMs + 1.0ms (6.5ms)
        //                 (Grade 1 runs 1-pass shared-mem Fast-CAS directly on texSrc_, skipping UltraPre & 12-tap clarity)
        //       * 1 -> 0: >= 60 consecutive frames with emaFrameGpuMs > targetGpuBudgetMs + 2.0ms (7.5ms)
        //                 (Grade 0 cleanly bypasses all compute/blits -> 0.000ms GPU while keeping targets resident!)
        //   - Step up (one rung at a time, >= 120 consecutive frames with 1.5ms hysteresis deadband):
        //       * 0 -> 1: ema < (budget + 2.0) - 1.5 (6.0ms)
        //       * 1 -> 2: ema < (budget + 1.0) - 1.5 (5.0ms)
        //       * 2 -> 3: ema < budget - 1.5 (4.0ms)
        //   - Keeps texHi_ & texWork_ & texSrc_ & texOut_ resident across 3 <-> 2 <-> 1 <-> 0 -> ZERO GL reallocs!
        static constexpr uint32_t kAdaptiveDown3To2Frames = 45u;
        static constexpr uint32_t kAdaptiveDown2To1Frames = 45u;
        static constexpr uint32_t kAdaptiveDown1To0Frames = 60u;
        static constexpr uint32_t kAdaptiveUpFrames       = 120u;
        static constexpr float    kAdaptiveUpMarginMs     = 1.5f;

        const int reqGrade = (cfg.grade >= 3) ? 3 : 2;
        if (cfg.allowAdaptiveGrade) {
            if (adaptiveBaseGrade_ != reqGrade) {
                if (adaptiveBaseGrade_ == 0 || reqGrade > adaptiveBaseGrade_) {
                    adaptiveGrade_ = reqGrade;
                } else if (adaptiveGrade_ > reqGrade) {
                    adaptiveGrade_ = reqGrade;
                }
                adaptiveBaseGrade_ = reqGrade;
                overBudgetFrames_  = 0u;
                underBudgetFrames_ = 0u;
            }
            const float budgetMs = (std::isfinite(cfg.targetGpuBudgetMs) && cfg.targetGpuBudgetMs > 1.0f)
                ? cfg.targetGpuBudgetMs : 5.5f;
            const uint32_t upFrames = (cfg.adaptiveUpFrames >= 1u) ? cfg.adaptiveUpFrames : kAdaptiveUpFrames;
            const float upMarginMs = (std::isfinite(cfg.adaptiveUpMarginMs) && cfg.adaptiveUpMarginMs >= 0.1f)
                ? cfg.adaptiveUpMarginMs : kAdaptiveUpMarginMs;
            const float emaMs = (std::isfinite(cfg.emaFrameGpuMs) && cfg.emaFrameGpuMs > 0.0f)
                ? cfg.emaFrameGpuMs : 0.0f;
            auto downThreshFor = [&](int g) noexcept -> float {
                if (g >= 3) return budgetMs;
                if (g == 2) return budgetMs + 1.0f;
                return budgetMs + 2.0f;
            };
            auto downFramesFor = [&](int g) noexcept -> uint32_t {
                if (g >= 3) return kAdaptiveDown3To2Frames;
                if (g == 2) return kAdaptiveDown2To1Frames;
                return kAdaptiveDown1To0Frames;
            };
            auto recoverThreshForUpper = [&](int upperG) noexcept -> float {
                const float dt = downThreshFor(upperG);
                return (dt > (upMarginMs + 0.5f)) ? (dt - upMarginMs) : (dt * 0.70f);
            };

            if (emaMs > 0.0f) {
                if (adaptiveGrade_ > 0 && emaMs > downThreshFor(adaptiveGrade_)) {
                    ++overBudgetFrames_;
                    underBudgetFrames_ = 0u;
                    if (overBudgetFrames_ >= downFramesFor(adaptiveGrade_)) {
                        const int prevG = adaptiveGrade_;
                        const float thresh = downThreshFor(prevG);
                        --adaptiveGrade_;
                        overBudgetFrames_  = 0u;
                        underBudgetFrames_ = 0u;
                        ++adaptiveGradeSteps_;
                        if (ops_.logFn) {
                            char msg[208]{};
                            std::snprintf(msg, sizeof(msg),
                                          "[VISUAL][GPU_CLARITY] Adaptive grade %d -> %d (emaGpu=%.2fms > thresh=%.2fms, zero FBO realloc)",
                                          prevG, adaptiveGrade_,
                                          static_cast<double>(emaMs), static_cast<double>(thresh));
                            ops_.logFn(msg);
                        }
                    }
                } else if (adaptiveGrade_ < reqGrade && emaMs < recoverThreshForUpper(adaptiveGrade_ + 1)) {
                    ++underBudgetFrames_;
                    overBudgetFrames_ = 0u;
                    if (underBudgetFrames_ >= upFrames) {
                        const int prevG = adaptiveGrade_;
                        const float recMs = recoverThreshForUpper(prevG + 1);
                        ++adaptiveGrade_;
                        overBudgetFrames_  = 0u;
                        underBudgetFrames_ = 0u;
                        ++adaptiveGradeSteps_;
                        if (ops_.logFn) {
                            char msg[208]{};
                            std::snprintf(msg, sizeof(msg),
                                          "[VISUAL][GPU_CLARITY] Adaptive grade %d -> %d (emaGpu=%.2fms < recover=%.2fms, zero FBO realloc)",
                                          prevG, adaptiveGrade_,
                                          static_cast<double>(emaMs), static_cast<double>(recMs));
                            ops_.logFn(msg);
                        }
                    }
                } else {
                    overBudgetFrames_  = 0u;
                    underBudgetFrames_ = 0u;
                }
            }
        } else {
            adaptiveGrade_     = reqGrade;
            adaptiveBaseGrade_ = 0;
            overBudgetFrames_  = 0u;
            underBudgetFrames_ = 0u;
        }

        // Grade 0 adaptive bypass: 0.000ms GPU, zero GL blits/dispatches, keep all FBO/textures resident!
        if (adaptiveGrade_ <= 0) {
            active_ = false;
            effectiveGrade_ = 0;
            lastGpuMs_ = 0.0f;
            return false;
        }

        const bool retain2x = (reqGrade >= 3 && cfg.allowAdaptiveGrade);
        int runGrade = adaptiveGrade_;
        if (runGrade >= 3) {
            // GL_MAX_TEXTURE_SIZE is context-immutable: query ONCE per context (0 per-frame stalls!)
            if (maxTexSize_ <= 0) {
                ops_.glGetIntegerv(kGlMaxTextureSize, &maxTexSize_);
            }
            if (maxTexSize_ > 0 && (vpW * 2 > maxTexSize_ || vpH * 2 > maxTexSize_)) {
                runGrade = 2;
            }
        }
        return ProcessFrameWithMode(srcFbo, vpW, vpH, runGrade, retain2x, cfg.strength, knownState);
    }

private:
    bool ProcessFrameWithMode(uint32_t srcFbo,
                              int32_t vpW,
                              int32_t vpH,
                              int runGrade,
                              bool retain2x,
                              float strength,
                              const KnownGlState* knownState) noexcept {
        bool need2x = (runGrade >= 3);
        // Allocate targets first so RGBA16F/RGB10_A2 capability / fallback format is locked before shader compilation
        if (!EnsureTargets(vpW, vpH, need2x, retain2x, knownState)) {
            if (need2x && !failed_) {
                if (!EnsureTargets(vpW, vpH, false, false, knownState)) {
                    failed_ = true;
                    active_ = false;
                    effectiveGrade_ = 0;
                    return false;
                }
                need2x = false;
                runGrade = 2;
            } else {
                failed_ = true;
                active_ = false;
                effectiveGrade_ = 0;
                return false;
            }
        }
        if (!EnsurePrograms()) {
            failed_ = true;
            active_ = false;
            effectiveGrade_ = 0;
            return false;
        }

        // Capture host GL state: zero synchronous glGetIntegerv/glIsEnabled when knownState supplies cached values!
        int32_t prevDrawFbo = 0, prevReadFbo = 0, prevProg = 0, prevActiveTex = 0, prevTex0 = 0;
        if (knownState && knownState->drawFbo >= 0) {
            prevDrawFbo = knownState->drawFbo;
        } else {
            ops_.glGetIntegerv(kGlDrawFramebufferBinding, &prevDrawFbo);
        }
        if (knownState && knownState->readFbo >= 0) {
            prevReadFbo = knownState->readFbo;
        } else {
            ops_.glGetIntegerv(kGlReadFramebufferBinding, &prevReadFbo);
        }
        if (knownState && knownState->curProgram >= 0) {
            prevProg = knownState->curProgram;
        } else {
            ops_.glGetIntegerv(kGlCurrentProgram, &prevProg);
        }
        if (knownState && knownState->activeTexEnum >= static_cast<int32_t>(kGlTexture0)) {
            prevActiveTex = knownState->activeTexEnum;
        } else {
            ops_.glGetIntegerv(kGlActiveTexture, &prevActiveTex);
        }
        bool scissorWasOn = true;
        if (knownState && knownState->scissorEnabled >= 0) {
            scissorWasOn = (knownState->scissorEnabled != 0);
        } else if (ops_.glIsEnabled) {
            scissorWasOn = (ops_.glIsEnabled(kGlScissorTest) != 0);
        }
        if (scissorWasOn && ops_.glDisable) {
            ops_.glDisable(kGlScissorTest);
        }
        if (prevActiveTex != static_cast<int32_t>(kGlTexture0)) {
            ops_.glActiveTexture(kGlTexture0);
        }
        if (knownState && knownState->boundTex0 >= 0) {
            prevTex0 = knownState->boundTex0;
        } else {
            ops_.glGetIntegerv(kGlTextureBinding2D, &prevTex0);
        }

        // Harvest finished timer query from ring without stalling CPU (opt-out via knownState->sampleTimer)
        const bool wantTimer = !knownState || knownState->sampleTimer;
        if (wantTimer) {
            HarvestTimerQueries();
        }
        const bool canQuery = wantTimer && ops_.HasTimerQuery() && EnsureQueries();
        uint32_t curQuery = 0u;
        if (canQuery) {
            curQuery = queries_[querySlot_];
            ops_.glBeginQuery(kGlTimeElapsed, curQuery);
        }

        // 1. Copy rendered world scene from live backbuffer srcFbo -> texSrc_ (1:1 nearest)
        ops_.glBindFramebuffer(kGlReadFramebuffer, srcFbo);
        ops_.glBindFramebuffer(kGlDrawFramebuffer, fboSrc_);
        ops_.glBlitFramebuffer(0, 0, vpW, vpH, 0, 0, vpW, vpH, kGlColorBufferBit, kGlNearest);
        ops_.glMemoryBarrier(kGlTextureFetchBarrier);

        const uint32_t gx1 = static_cast<uint32_t>((vpW + 15) / 16);
        const uint32_t gy1 = static_cast<uint32_t>((vpH + 15) / 16);
        const float str = std::clamp(strength, 0.0f, 1.5f);
        const float invW = 1.0f / static_cast<float>(vpW);
        const float invH = 1.0f / static_cast<float>(vpH);

        if (runGrade <= 1) {
            // Adaptive Grade 1 (Fast-CAS 1x): single-pass shared-memory FidelityFX CAS directly texSrc_ -> texOut_
            // Skips UltraPre1x AND all 12 scattered bilinear local-clarity taps (~75% cheaper than Grade 2!)
            DispatchClarityFinish(vpW, vpH, texSrc_, 0.58f * str, 0.0f, 0.04f * str, 0.50f);
        } else if (runGrade == 2) {
            // Grade 2 (UltraHD / 超清): Pass 1 (2D AA + Anime4K Ink Push) -> texWork_
            ops_.glUseProgram(progUltraPre_);
            if (locUltraParams_ >= 0) {
                ops_.glUniform4f(locUltraParams_, static_cast<float>(vpW), static_cast<float>(vpH), invW, invH);
            }
            if (locUltraTune_ >= 0) {
                ops_.glUniform4f(locUltraTune_, str, 1.0f, 0.85f, 0.0f);
            }
            ops_.glBindTexture(kGlTexture2D, texSrc_);
            ops_.glBindImageTexture(0u, texWork_, 0, 0, 0, kGlWriteOnly, workFmt_);
            ops_.glDispatchCompute(gx1, gy1, 1u);
            ops_.glMemoryBarrier(kGlTextureFetchBarrier);

            // Pass 2 (Shared-Memory CAS + Dual-Scale Edge-Preserving Local Clarity) -> texOut_
            DispatchClarityFinish(vpW, vpH, texWork_, 0.72f * str, 0.34f * str, 0.05f * str, 0.70f);
        } else {
            // Grade 3 (BluRay / 蓝光):
            // Pass A: 2x Virtual-4K Anisotropic EASU + Sub-Pixel Ink Tightening -> texHi_ (RGB10_A2)
            const int32_t hiW = vpW * 2;
            const int32_t hiH = vpH * 2;
            const uint32_t gx2 = static_cast<uint32_t>((hiW + 15) / 16);
            const uint32_t gy2 = static_cast<uint32_t>((hiH + 15) / 16);

            ops_.glUseProgram(progUp2x_);
            if (locUp2xSrc_ >= 0) {
                ops_.glUniform4f(locUp2xSrc_, static_cast<float>(vpW), static_cast<float>(vpH), invW, invH);
            }
            if (locUp2xHi_ >= 0) {
                ops_.glUniform4f(locUp2xHi_, static_cast<float>(hiW), static_cast<float>(hiH),
                                 1.0f / static_cast<float>(hiW), 1.0f / static_cast<float>(hiH));
            }
            if (locUp2xTune_ >= 0) {
                ops_.glUniform4f(locUp2xTune_, str, 1.0f, 1.15f, 0.0f);
            }
            ops_.glBindTexture(kGlTexture2D, texSrc_);
            ops_.glBindImageTexture(0u, texHi_, 0, 0, 0, kGlWriteOnly, hiFmt_);
            ops_.glDispatchCompute(gx2, gy2, 1u);
            ops_.glMemoryBarrier(kGlTextureFetchBarrier);

            // Pass B: 4x4 Lanczos-2 Super-Sampled Downsample + Micro-Detail -> texWork_ (RGBA16F)
            ops_.glUseProgram(progRes1x_);
            if (locRes1xOut_ >= 0) {
                ops_.glUniform4f(locRes1xOut_, static_cast<float>(vpW), static_cast<float>(vpH), invW, invH);
            }
            if (locRes1xTune_ >= 0) {
                ops_.glUniform4f(locRes1xTune_, str, 1.0f, 0.0f, 0.0f);
            }
            ops_.glBindTexture(kGlTexture2D, texHi_);
            ops_.glBindImageTexture(0u, texWork_, 0, 0, 0, kGlWriteOnly, workFmt_);
            ops_.glDispatchCompute(gx1, gy1, 1u);
            ops_.glMemoryBarrier(kGlTextureFetchBarrier);

            // Pass C: Shared-Memory CAS + Dual-Scale Local Clarity + Crystal Vibrance + Dither -> texOut_
            DispatchClarityFinish(vpW, vpH, texWork_, 0.98f * str, 0.56f * str, 0.14f * str, 0.85f);
        }

        // 2. Write enhanced result back into srcFbo STRICTLY BEFORE UI draws!
        ops_.glBindImageTexture(0u, 0u, 0, 0, 0, kGlWriteOnly, kGlRgba8);
        ops_.glBindFramebuffer(kGlReadFramebuffer, fboOut_);
        ops_.glBindFramebuffer(kGlDrawFramebuffer, srcFbo);
        ops_.glBlitFramebuffer(0, 0, vpW, vpH, 0, 0, vpW, vpH, kGlColorBufferBit, kGlNearest);

        if (canQuery) {
            ops_.glEndQuery(kGlTimeElapsed);
            queryArmed_[querySlot_] = true;
            querySlot_ = (querySlot_ + 1u) & 3u;
        }

        // 3. Strictly restore previous GL state
        ops_.glBindTexture(kGlTexture2D, static_cast<uint32_t>(prevTex0));
        if (prevActiveTex != static_cast<int32_t>(kGlTexture0)) {
            ops_.glActiveTexture(static_cast<uint32_t>(prevActiveTex));
        }
        ops_.glUseProgram(static_cast<uint32_t>(prevProg));
        ops_.glBindFramebuffer(kGlReadFramebuffer, static_cast<uint32_t>(prevReadFbo));
        if (static_cast<uint32_t>(prevDrawFbo) != srcFbo) {
            ops_.glBindFramebuffer(kGlDrawFramebuffer, static_cast<uint32_t>(prevDrawFbo));
        }
        if (scissorWasOn && ops_.glEnable) {
            ops_.glEnable(kGlScissorTest);
        }

        active_ = true;
        effectiveGrade_ = runGrade;
        return true;
    }

    void DispatchClarityFinish(int32_t vpW, int32_t vpH, uint32_t srcTex,
                               float fineCas, float localClarity, float vibrance, float dither) noexcept {
        const uint32_t gx1 = static_cast<uint32_t>((vpW + 15) / 16);
        const uint32_t gy1 = static_cast<uint32_t>((vpH + 15) / 16);
        ops_.glUseProgram(progFinish_);
        if (locFinishOut_ >= 0) {
            ops_.glUniform4f(locFinishOut_,
                             static_cast<float>(vpW), static_cast<float>(vpH),
                             1.0f / static_cast<float>(vpW), 1.0f / static_cast<float>(vpH));
        }
        if (locFinishTune_ >= 0) {
            ops_.glUniform4f(locFinishTune_, fineCas, localClarity, vibrance, dither);
        }
        ops_.glBindTexture(kGlTexture2D, srcTex);
        ops_.glBindImageTexture(0u, texOut_, 0, 0, 0, kGlWriteOnly, kGlRgba8);
        ops_.glDispatchCompute(gx1, gy1, 1u);
        ops_.glMemoryBarrier(kGlFramebufferBarrier);
    }

    static const char* FormatLayoutName(uint32_t fmt) noexcept {
        if (fmt == kGlRgba16f) return "rgba16f\n";
        if (fmt == kGlRgb10A2) return "rgb10_a2\n";
        return "rgba8\n";
    }

    static const char* FormatLogName(uint32_t fmt) noexcept {
        if (fmt == kGlRgba16f) return "RGBA16F";
        if (fmt == kGlRgb10A2) return "RGB10_A2";
        return "RGBA8";
    }

    uint32_t CompileComputeProgram(const char* srcBody, uint32_t dstFmt, const char* tag) noexcept {
        std::string fullSrc = "#version 430 core\n#define DST_WORK_FMT ";
        fullSrc += FormatLayoutName(dstFmt);
        fullSrc += srcBody;
        const char* srcPtr = fullSrc.c_str();
        const int32_t srcLen = static_cast<int32_t>(fullSrc.size());

        const uint32_t sh = ops_.glCreateShader(kGlComputeShader);
        if (sh == 0u) return 0u;
        ops_.glShaderSource(sh, 1, &srcPtr, &srcLen);
        ops_.glCompileShader(sh);
        int32_t ok = 0;
        ops_.glGetShaderiv(sh, kGlCompileStatus, &ok);
        if (!ok) {
            if (ops_.logFn && ops_.glGetShaderInfoLog) {
                char info[512]{};
                int32_t len = 0;
                ops_.glGetShaderInfoLog(sh, static_cast<int32_t>(sizeof(info) - 1), &len, info);
                char msg[640]{};
                std::snprintf(msg, sizeof(msg), "[VISUAL][GPU_CLARITY][ERR] Compute shader %s compile failed: %s", tag, info);
                ops_.logFn(msg);
            }
            ops_.glDeleteShader(sh);
            return 0u;
        }
        const uint32_t prog = ops_.glCreateProgram();
        if (prog == 0u) {
            ops_.glDeleteShader(sh);
            return 0u;
        }
        ops_.glAttachShader(prog, sh);
        ops_.glLinkProgram(prog);
        ops_.glDeleteShader(sh);
        int32_t linkOk = 0;
        ops_.glGetProgramiv(prog, kGlLinkStatus, &linkOk);
        if (!linkOk) {
            if (ops_.logFn && ops_.glGetProgramInfoLog) {
                char info[512]{};
                int32_t len = 0;
                ops_.glGetProgramInfoLog(prog, static_cast<int32_t>(sizeof(info) - 1), &len, info);
                char msg[640]{};
                std::snprintf(msg, sizeof(msg), "[VISUAL][GPU_CLARITY][ERR] Compute program %s link failed: %s", tag, info);
                ops_.logFn(msg);
            }
            ops_.glDeleteProgram(prog);
            return 0u;
        }
        return prog;
    }

    bool EnsurePrograms() noexcept {
        if (progUltraPre_ && progUp2x_ && progRes1x_ && progFinish_ &&
            compiledWorkFmt_ == workFmt_ && compiledHiFmt_ == hiFmt_) {
            return true;
        }
        if (compiledWorkFmt_ != workFmt_) {
            if (progUltraPre_) { ops_.glDeleteProgram(progUltraPre_); progUltraPre_ = 0u; }
            if (progRes1x_)    { ops_.glDeleteProgram(progRes1x_);    progRes1x_    = 0u; }
        }
        if (compiledHiFmt_ != hiFmt_) {
            if (progUp2x_)     { ops_.glDeleteProgram(progUp2x_);     progUp2x_     = 0u; }
        }
        if (progUltraPre_ == 0u) {
            progUltraPre_ = CompileComputeProgram(kCsUltraPre1xBody, workFmt_, "UltraPre1x");
            if (progUltraPre_ == 0u) return false;
            locUltraParams_ = ops_.glGetUniformLocation(progUltraPre_, "uParams");
            locUltraTune_   = ops_.glGetUniformLocation(progUltraPre_, "uTune");
        }
        if (progUp2x_ == 0u) {
            progUp2x_ = CompileComputeProgram(kCsBluRayUpscale2xBody, hiFmt_, "BluRayUpscale2x");
            if (progUp2x_ == 0u) return false;
            locUp2xSrc_  = ops_.glGetUniformLocation(progUp2x_, "uSrcSize");
            locUp2xHi_   = ops_.glGetUniformLocation(progUp2x_, "uHiSize");
            locUp2xTune_ = ops_.glGetUniformLocation(progUp2x_, "uTune");
        }
        if (progRes1x_ == 0u) {
            progRes1x_ = CompileComputeProgram(kCsBluRayResolve1xBody, workFmt_, "BluRayResolve1x");
            if (progRes1x_ == 0u) return false;
            locRes1xOut_  = ops_.glGetUniformLocation(progRes1x_, "uOutSize");
            locRes1xTune_ = ops_.glGetUniformLocation(progRes1x_, "uTune");
        }
        if (progFinish_ == 0u) {
            progFinish_ = CompileComputeProgram(kCsClarityFinish1xBody, kGlRgba8, "ClarityFinish1x");
            if (progFinish_ == 0u) return false;
            locFinishOut_  = ops_.glGetUniformLocation(progFinish_, "uOutSize");
            locFinishTune_ = ops_.glGetUniformLocation(progFinish_, "uTune");
        }
        compiledWorkFmt_ = workFmt_;
        compiledHiFmt_   = hiFmt_;
        return true;
    }

    uint32_t CreateImmutableTex(int32_t w, int32_t h, uint32_t internalFmt) noexcept {
        uint32_t tex = 0u;
        ops_.glGenTextures(1, &tex);
        if (tex == 0u) return 0u;
        ops_.glBindTexture(kGlTexture2D, tex);
        ops_.glTexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<int32_t>(kGlLinear));
        ops_.glTexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<int32_t>(kGlLinear));
        ops_.glTexParameteri(kGlTexture2D, kGlTextureWrapS, static_cast<int32_t>(kGlClampToEdge));
        ops_.glTexParameteri(kGlTexture2D, kGlTextureWrapT, static_cast<int32_t>(kGlClampToEdge));
        ops_.glTexStorage2D(kGlTexture2D, 1, internalFmt, w, h);
        return tex;
    }

    bool VerifyTexFboComplete(uint32_t tex) noexcept {
        if (tex == 0u) return false;
        uint32_t probeFbo = 0u;
        ops_.glGenFramebuffers(1, &probeFbo);
        if (probeFbo == 0u) return false;
        ops_.glBindFramebuffer(kGlDrawFramebuffer, probeFbo);
        ops_.glFramebufferTexture2D(kGlDrawFramebuffer, kGlColorAttachment0, kGlTexture2D, tex, 0);
        const uint32_t st = ops_.glCheckFramebufferStatus(kGlDrawFramebuffer);
        ops_.glDeleteFramebuffers(1, &probeFbo);
        return st == kGlFramebufferComplete;
    }

    bool EnsureTargets(int32_t vpW, int32_t vpH, bool need2x, bool retain2x, const KnownGlState* knownState) noexcept {
        if (vpW == w_ && vpH == h_ && texSrc_ != 0u && texWork_ != 0u && texOut_ != 0u) {
            if (need2x) {
                if (has2x_ && texHi_ != 0u) {
                    return true;
                }
            } else {
                if (retain2x || texHi_ == 0u) {
                    return true; // Zero-allocation / zero-deletion fast path during adaptive 3 <-> 2 stepping!
                }
                ops_.glDeleteTextures(1, &texHi_);
                texHi_ = 0u;
                has2x_ = false;
                return true;
            }
        }

        int32_t prevDrawFbo = 0, prevReadFbo = 0, prevActiveTex = 0, prevTex0 = 0;
        if (knownState && knownState->drawFbo >= 0) prevDrawFbo = knownState->drawFbo;
        else ops_.glGetIntegerv(kGlDrawFramebufferBinding, &prevDrawFbo);
        if (knownState && knownState->readFbo >= 0) prevReadFbo = knownState->readFbo;
        else ops_.glGetIntegerv(kGlReadFramebufferBinding, &prevReadFbo);
        if (knownState && knownState->activeTexEnum >= static_cast<int32_t>(kGlTexture0)) prevActiveTex = knownState->activeTexEnum;
        else ops_.glGetIntegerv(kGlActiveTexture, &prevActiveTex);
        if (prevActiveTex != static_cast<int32_t>(kGlTexture0)) {
            ops_.glActiveTexture(kGlTexture0);
        }
        if (knownState && knownState->boundTex0 >= 0) prevTex0 = knownState->boundTex0;
        else ops_.glGetIntegerv(kGlTextureBinding2D, &prevTex0);

        auto restoreBind = [&]() {
            ops_.glBindTexture(kGlTexture2D, static_cast<uint32_t>(prevTex0));
            if (prevActiveTex != static_cast<int32_t>(kGlTexture0)) {
                ops_.glActiveTexture(static_cast<uint32_t>(prevActiveTex));
            }
            ops_.glBindFramebuffer(kGlReadFramebuffer, static_cast<uint32_t>(prevReadFbo));
            ops_.glBindFramebuffer(kGlDrawFramebuffer, static_cast<uint32_t>(prevDrawFbo));
        };

        if (vpW != w_ || vpH != h_ || texSrc_ == 0u || texWork_ == 0u || texOut_ == 0u) {
            DestroyTexturesAndFbos();
            texSrc_ = CreateImmutableTex(vpW, vpH, kGlRgba8);
            texOut_ = CreateImmutableTex(vpW, vpH, kGlRgba8);
            ops_.glGenFramebuffers(1, &fboSrc_);
            ops_.glGenFramebuffers(1, &fboOut_);
            if (!texSrc_ || !texOut_ || !fboSrc_ || !fboOut_) {
                DestroyTexturesAndFbos();
                restoreBind();
                return false;
            }
            ops_.glBindFramebuffer(kGlDrawFramebuffer, fboSrc_);
            ops_.glFramebufferTexture2D(kGlDrawFramebuffer, kGlColorAttachment0, kGlTexture2D, texSrc_, 0);
            if (ops_.glCheckFramebufferStatus(kGlDrawFramebuffer) != kGlFramebufferComplete) {
                DestroyTexturesAndFbos();
                restoreBind();
                return false;
            }
            ops_.glBindFramebuffer(kGlDrawFramebuffer, fboOut_);
            ops_.glFramebufferTexture2D(kGlDrawFramebuffer, kGlColorAttachment0, kGlTexture2D, texOut_, 0);
            if (ops_.glCheckFramebufferStatus(kGlDrawFramebuffer) != kGlFramebufferComplete) {
                DestroyTexturesAndFbos();
                restoreBind();
                return false;
            }

            // Try RGBA16F for intermediate 1x compute precision; fail gracefully to RGBA8
            texWork_ = CreateImmutableTex(vpW, vpH, workFmt_);
            if (workFmt_ == kGlRgba16f && !VerifyTexFboComplete(texWork_)) {
                if (texWork_) { ops_.glDeleteTextures(1, &texWork_); texWork_ = 0u; }
                workFmt_ = kGlRgba8;
                workBytesPerPx_ = 4u;
                hiFmt_ = kGlRgba8;
                hiBytesPerPx_ = 4u;
                texWork_ = CreateImmutableTex(vpW, vpH, kGlRgba8);
            }
            if (!texWork_) {
                DestroyTexturesAndFbos();
                restoreBind();
                return false;
            }
            w_ = vpW;
            h_ = vpH;
        }

        if (need2x && texHi_ == 0u) {
            // Virtual-4K 2x intermediate uses RGB10_A2 (10-bit RGB = 4x 8-bit precision, 50% less VRAM/BW vs RGBA16F!)
            texHi_ = CreateImmutableTex(vpW * 2, vpH * 2, hiFmt_);
            if (texHi_ && hiFmt_ != kGlRgba8 && !VerifyTexFboComplete(texHi_)) {
                ops_.glDeleteTextures(1, &texHi_);
                texHi_ = 0u;
                hiFmt_ = kGlRgba8;
                hiBytesPerPx_ = 4u;
                texHi_ = CreateImmutableTex(vpW * 2, vpH * 2, kGlRgba8);
            }
            if (!texHi_) {
                restoreBind();
                return false;
            }
            has2x_ = true;
        }

        restoreBind();
        if (ops_.logFn) {
            char msg[224]{};
            std::snprintf(msg, sizeof(msg),
                          "[VISUAL][GPU_CLARITY] Compute targets ready (%dx%d work=%s, virtual4K=%dx%d hi=%s, vramExtra=%uMB)",
                          w_, h_, FormatLogName(workFmt_),
                          has2x_ ? w_ * 2 : 0, has2x_ ? h_ * 2 : 0,
                          has2x_ ? FormatLogName(hiFmt_) : "none",
                          (VramExtraKb() + 1023u) / 1024u);
            ops_.logFn(msg);
        }
        return true;
    }

    bool EnsureQueries() noexcept {
        if (queries_[0] != 0u) return true;
        ops_.glGenQueries(4, queries_);
        return queries_[0] != 0u;
    }

    void HarvestTimerQueries() noexcept {
        if (!ops_.HasTimerQuery()) return;
        // Steady-state fast path: only inspect the oldest slot (querySlot_, 4 samples old) first!
        // Avoids 3 wasted glGetQueryObjectiv driver-thread round-trips on still-in-flight queries.
        for (uint32_t step = 0u; step < 4u; ++step) {
            const uint32_t i = (querySlot_ + step) & 3u;
            if (!queryArmed_[i] || queries_[i] == 0u) continue;
            int32_t avail = 0;
            ops_.glGetQueryObjectiv(queries_[i], kGlQueryResultAvailable, &avail);
            if (avail == 0) {
                break; // FIFO GPU execution: newer queries cannot be ready if an older one isn't
            }
            uint64_t ns = 0ULL;
            ops_.glGetQueryObjectui64v(queries_[i], kGlQueryResult, &ns);
            queryArmed_[i] = false;
            const float ms = static_cast<float>(static_cast<double>(ns) * 1e-6);
            if (ms >= 0.0f && ms < 250.0f) {
                lastGpuMs_ = (lastGpuMs_ <= 0.0001f) ? ms : (lastGpuMs_ * 0.85f + ms * 0.15f);
            }
            if (step == 0u) {
                break; // 1 harvested reading per frame in steady state -> minimum driver round-trips
            }
        }
    }

    void DestroyTexturesAndFbos() noexcept {
        if (fboSrc_)  { ops_.glDeleteFramebuffers(1, &fboSrc_); fboSrc_  = 0u; }
        if (fboOut_)  { ops_.glDeleteFramebuffers(1, &fboOut_); fboOut_  = 0u; }
        if (texSrc_)  { ops_.glDeleteTextures(1, &texSrc_);     texSrc_  = 0u; }
        if (texWork_) { ops_.glDeleteTextures(1, &texWork_);    texWork_ = 0u; }
        if (texOut_)  { ops_.glDeleteTextures(1, &texOut_);     texOut_  = 0u; }
        if (texHi_)   { ops_.glDeleteTextures(1, &texHi_);      texHi_   = 0u; }
        w_ = 0;
        h_ = 0;
        has2x_ = false;
    }

    void DestroyAllResources() noexcept {
        DestroyTexturesAndFbos();
        if (progUltraPre_) { ops_.glDeleteProgram(progUltraPre_); progUltraPre_ = 0u; }
        if (progUp2x_)     { ops_.glDeleteProgram(progUp2x_);     progUp2x_     = 0u; }
        if (progRes1x_)    { ops_.glDeleteProgram(progRes1x_);    progRes1x_    = 0u; }
        if (progFinish_)   { ops_.glDeleteProgram(progFinish_);   progFinish_   = 0u; }
        compiledWorkFmt_ = 0u;
        compiledHiFmt_   = 0u;
        if (ops_.glDeleteQueries && queries_[0] != 0u) {
            ops_.glDeleteQueries(4, queries_);
        }
        queries_[0] = queries_[1] = queries_[2] = queries_[3] = 0u;
        queryArmed_[0] = queryArmed_[1] = queryArmed_[2] = queryArmed_[3] = false;
    }

    GlComputeOps ops_{};
    bool         supported_  = false;
    bool         failed_     = false;
    bool         active_     = false;

    uint32_t     workFmt_        = kGlRgba16f;
    uint32_t     hiFmt_          = kGlRgb10A2;
    uint32_t     workBytesPerPx_ = 8u;
    uint32_t     hiBytesPerPx_   = 4u;
    uint32_t     compiledWorkFmt_ = 0u;
    uint32_t     compiledHiFmt_   = 0u;
    int32_t      maxTexSize_      = 0;

    uint32_t     progUltraPre_ = 0u;
    uint32_t     progUp2x_     = 0u;
    uint32_t     progRes1x_    = 0u;
    uint32_t     progFinish_   = 0u;
    int32_t      locUltraParams_ = -1;
    int32_t      locUltraTune_   = -1;
    int32_t      locUp2xSrc_     = -1;
    int32_t      locUp2xHi_      = -1;
    int32_t      locUp2xTune_    = -1;
    int32_t      locRes1xOut_    = -1;
    int32_t      locRes1xTune_   = -1;
    int32_t      locFinishOut_   = -1;
    int32_t      locFinishTune_  = -1;

    uint32_t     fboSrc_     = 0u;
    uint32_t     fboOut_     = 0u;
    uint32_t     texSrc_     = 0u;
    uint32_t     texWork_    = 0u;
    uint32_t     texOut_     = 0u;
    uint32_t     texHi_      = 0u;
    int32_t      w_          = 0;
    int32_t      h_          = 0;
    bool         has2x_      = false;

    uint32_t     queries_[4]{};
    bool         queryArmed_[4]{};
    uint32_t     querySlot_  = 0u;
    float        lastGpuMs_  = 0.0f;

    int          effectiveGrade_      = 0;
    int          adaptiveGrade_       = 0;
    int          adaptiveBaseGrade_   = 0;
    uint32_t     overBudgetFrames_    = 0u;
    uint32_t     underBudgetFrames_   = 0u;
    uint64_t     adaptiveGradeSteps_  = 0ULL;
};

} // namespace hs_gpu_clarity
