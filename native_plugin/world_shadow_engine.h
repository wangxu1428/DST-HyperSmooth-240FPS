#pragma once
// =============================================================================
// HyperSmooth World-Space ShadowMask FBO Engine (v2.9.0 — Pure GLES2 IAT Layer)
// =============================================================================
// Replaces the legacy v2.8.0 BloomSampler blue-channel + full-screen postprocess
// shadow hack with a dedicated World-Space Top-Down Orthographic RGBA8 ShadowMask
// pipeline that eliminates all 5 structural limitations:
//   1. True World-Space Occlusion & ColourCube LUT Integration:
//      Composites a single world-space ground quad (y = 0.04, GL_ZERO/GL_SRC_COLOR)
//      onto PrimaryRT AFTER Ground/Ocean/Roads/Creep/OnGround/DynamicShadow and
//      strictly BEFORE the first standing world entity — foreground characters,
//      trees, and structures naturally draw OVER ground shadows with 0% face/body
//      sticker clipping and 100% native ColourCube day/dusk/night tinting!
//   2. Full 0..255 Coverage + PCSS Contact-Hardening + Zero Alpha-Stacking Blobs:
//      - Channel R: full 8-bit coverage (16.6x precision over 0.06 blue encoding)
//      - Channel G: ground-contact proximity (1.0 = foot/root, 0.0 = canopy tip)
//        driving height-adaptive 9-tap IGN-rotated Vogel-disk penumbra softening.
//      - Native OpenGL: hardware glBlendEquation(GL_MAX) -> zero overlap blobs.
//      - ANGLE D3D9 (no GL_EXT_blend_minmax): core GLES2 GL_DEPTH_COMPONENT16
//        depth test (GL_LESS, smallest height wins) + alpha cutoff -> zero blobs
//        and zero GL_INVALID_ENUM errors!
//   3. 360-Degree Side-Light Billboard Geometry Fix:
//      Smoothly reorients horizontal width axis from camRight at ground contact
//      (y=0) to perpendicular-to-light hs_perp above y=0.06..0.85, keeping feet
//      pixel-locked while preventing razor-thin side-light collapse at any Q/E angle.
//   4. 240FPS Batched Mirroring & Zero BloomSampler Pass Overhead:
//      - Mirrors SpriteBatcher::Flush() batches in 1 draw call per batch directly
//        from the already-uploaded Ring VBO & bound GL_TEXTURE6..13 atlases!
//      - Mirrors anim_skinned.ksh (trees/grass/mobs) and unbatched standing anim
//        via in-shader HS_SHADOW_PASS toggle with zero bone/VBO re-uploads!
//      - Texel-snapped world-space ping-pong FBO -> 0.00px camera/Q-E pan lag!
//   5. 100% Fail-Closed: if FBO or shaders ever fail to initialize, active stays
//      false and callers fall back cleanly with zero visual corruption.
// =============================================================================

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "gl_state_cache.h"
#include "blue_noise_64.h"

namespace hs_world_shadow {

inline constexpr uint32_t kGlFramebuffer            = 0x8D40u;
inline constexpr uint32_t kGlRenderbuffer           = 0x8D41u;
inline constexpr uint32_t kGlColorAttachment0       = 0x8CE0u;
inline constexpr uint32_t kGlDepthAttachment        = 0x8D00u;
inline constexpr uint32_t kGlFramebufferComplete    = 0x8CD5u;
inline constexpr uint32_t kGlDepthComponent16       = 0x81A5u;
inline constexpr uint32_t kGlTexture2D              = 0x0DE1u;
inline constexpr uint32_t kGlTexture0               = 0x84C0u;
inline constexpr uint32_t kGlRgba                   = 0x1908u;
inline constexpr uint32_t kGlUnsignedByte           = 0x1401u;
inline constexpr uint32_t kGlUnsignedShort          = 0x1403u;
inline constexpr uint32_t kGlFloat                  = 0x1406u;
inline constexpr uint32_t kGlNearest                = 0x2600u;
inline constexpr uint32_t kGlLinear                 = 0x2601u;
inline constexpr uint32_t kGlRepeat                 = 0x2901u;
inline constexpr uint32_t kGlClampToEdge            = 0x812Fu;
inline constexpr uint32_t kGlMaxTextureImageUnits   = 0x8872u;
inline constexpr uint32_t kGlTextureMinFilter       = 0x2801u;
inline constexpr uint32_t kGlTextureMagFilter       = 0x2800u;
inline constexpr uint32_t kGlTextureWrapS           = 0x2802u;
inline constexpr uint32_t kGlTextureWrapT           = 0x2803u;
inline constexpr uint32_t kGlArrayBuffer            = 0x8892u;
inline constexpr uint32_t kGlElementArrayBuffer     = 0x8893u;
inline constexpr uint32_t kGlShaderStorageBuffer    = 0x90D2u;
inline constexpr uint32_t kGpuInstBinding           = 0u;
inline constexpr uint32_t kGlStaticDraw             = 0x88E4u;
inline constexpr uint32_t kGlTriangles              = 0x0004u;
inline constexpr uint32_t kGlTriangleStrip          = 0x0005u;
inline constexpr uint32_t kGlColorBufferBit         = 0x00004000u;
inline constexpr uint32_t kGlDepthBufferBit         = 0x00000100u;
inline constexpr uint32_t kGlBlend                  = 0x0BE2u;
inline constexpr uint32_t kGlDepthTest              = 0x0B71u;
inline constexpr uint32_t kGlCullFace               = 0x0B44u;
inline constexpr uint32_t kGlScissorTest            = 0x0C11u;
inline constexpr uint32_t kGlStencilTest            = 0x0B90u;
inline constexpr uint32_t kGlLess                   = 0x0201u;
inline constexpr uint32_t kGlZero                   = 0x0000u;
inline constexpr uint32_t kGlOne                    = 0x0001u;
inline constexpr uint32_t kGlSrcColor               = 0x0300u;
inline constexpr uint32_t kGlSrcAlpha               = 0x0302u;
inline constexpr uint32_t kGlOneMinusSrcAlpha       = 0x0303u;
inline constexpr uint32_t kGlFuncAdd                = 0x8006u;
inline constexpr uint32_t kGlMax                    = 0x8008u;
inline constexpr uint32_t kGlVertexShader           = 0x8B31u;
inline constexpr uint32_t kGlFragmentShader         = 0x8B30u;
inline constexpr uint32_t kGlCompileStatus          = 0x8B81u;
inline constexpr uint32_t kGlLinkStatus             = 0x8B82u;
inline constexpr uint32_t kGlNoError                = 0u;
inline constexpr uint32_t kGlCurrentProgram         = 0x8B8Du;
inline constexpr uint32_t kGlRenderbufferBinding    = 0x8CA7u;
inline constexpr uint32_t kGlDepthWritemask         = 0x0B72u;
inline constexpr uint32_t kGlColorWritemask         = 0x0C23u;
inline constexpr uint32_t kGlDepthFuncEnum          = 0x0B74u;
inline constexpr uint32_t kGlBlendEquationRgb       = 0x8009u;
inline constexpr uint32_t kGlBlendEquationAlpha     = 0x883Du;
inline constexpr uint32_t kGlBlendSrcRgb            = 0x80C9u;
inline constexpr uint32_t kGlBlendDstRgb            = 0x80C8u;
inline constexpr uint32_t kGlBlendSrcAlpha          = 0x80CBu;
inline constexpr uint32_t kGlBlendDstAlpha          = 0x80CAu;

inline constexpr uint32_t kBatchTexUnitBase         = 6u;
inline constexpr uint32_t kMaxBatchTexSlots         = 16u;
inline constexpr uint32_t kLegacyBatchTexSlots      = 8u;
inline constexpr uint32_t BatchSlotToTexUnit(uint32_t slot) noexcept {
    return (slot < 8u) ? (kBatchTexUnitBase + slot) : (16u + (slot - 8u));
}
inline constexpr uint32_t kCompositeTexUnit         = 14u; // Unused by DST (0..5) & SpriteBatch (6..13)
inline constexpr uint32_t kBlueNoiseTexUnit         = 15u; // 64x64 Void-and-Cluster Blue Noise (GL_TEXTURE15)
inline constexpr float    kShadowHalfExtent         = 48.0f; // 96x96 world units = 24x24 DST tiles
inline constexpr float    kWorldUnitsPerTile        = 4.0f;  // 1 DST turf tile = 4.0 world units
inline constexpr uint32_t kMaxPointShadowLights     = 3u;    // Top-3 near-field campfire/lantern point shadows
inline constexpr size_t   kMaxDeferredBatches       = 2048u; // Covers 800+ stencil-flush mega-bases in 1 FBO bind!

struct PointShadowLight {
    float wx       = 0.0f;
    float wz       = 0.0f;
    float radius   = 0.0f; // <= 0.5f disables this slot
    float strength = 0.0f; // [0, 1] shadow contribution weight
};

struct ShadowConfig {
    float dirWx             = -0.707f;
    float dirWz             = -0.707f;
    float stretch           = 1.35f;
    float projOn            = 0.0f;
    float dirStrength       = 1.0f;  // [0, 1] directional pass weight (0 at pure point-light night)
    float pointStretchScale = 1.0f;  // [0.5, 1.8] user shadow_length applied to point-light stretch
    float windTime          = 0.0f;
    float swayAmp           = 0.45f;
    float heightFade        = 0.55f;
    float penumbraBias      = 1.0f;
    float density           = 0.0f;
    float soft              = 1.15f;
    float tintR             = 0.10f;
    float tintG             = 0.14f;
    float tintB             = 0.38f;
    float entityRecv        = 0.68f;
    float ovalKeep          = 0.24f;
    float minHeight         = 0.0f;  // [0, 1.5] world-unit sprite height cutoff (0 = all standing sprites)
    uint32_t maxHdRes       = 2048u; // Native GL can scale up to 4096 when BluRay clarity is active
    uint32_t temporalStride = 1u;    // Opt-in temporal shadow reuse cadence (1=every frame, 2=every 2nd, 3=every 3rd; zero FBO realloc!)
    bool  fastEntityReceive1Tap = false; // Opt-in 1-tap fast entity shadow receive (HS_RECV_BOUNDS.w = 0.65f, cuts 80% 4K shadow fetches)
    bool  hdMode            = true;
    bool  skipNightmare     = true;
    bool  hasLuaCam         = false;
    float luaCamX           = 0.0f;
    float luaCamZ           = 0.0f;
    float luaCamRightX      = 0.707f;
    float luaCamRightZ      = -0.707f;
    uint32_t         pointLightCount = 0u;
    PointShadowLight pointLights[kMaxPointShadowLights]{};
};

inline bool IsPointLightValid(const PointShadowLight& pl) noexcept {
    return std::isfinite(pl.wx) && std::isfinite(pl.wz) &&
           std::isfinite(pl.radius) && std::isfinite(pl.strength) &&
           pl.radius > 0.5f && pl.strength > 0.008f;
}

inline bool HasActivePointLights(const ShadowConfig& cfg) noexcept {
    const uint32_t cnt = (cfg.pointLightCount < kMaxPointShadowLights)
        ? cfg.pointLightCount : kMaxPointShadowLights;
    for (uint32_t i = 0u; i < cnt; ++i) {
        if (IsPointLightValid(cfg.pointLights[i])) return true;
    }
    return false;
}

inline bool HasActiveDirectionalShadow(const ShadowConfig& cfg) noexcept {
    return cfg.projOn > 0.5f && std::isfinite(cfg.dirStrength) && cfg.dirStrength > 0.005f;
}

inline bool HasAnyShadowLight(const ShadowConfig& cfg) noexcept {
    return std::isfinite(cfg.density) && cfg.density > 0.001f &&
           (HasActiveDirectionalShadow(cfg) || HasActivePointLights(cfg));
}

inline bool PointLightTouchesAabbXZ(const PointShadowLight& pl, const float boundsXZ[4]) noexcept {
    // Generous 3.0-unit sprite half-width margin around entity origins in the batch
    const float cx = std::clamp(pl.wx, boundsXZ[0] - 3.0f, boundsXZ[1] + 3.0f);
    const float cz = std::clamp(pl.wz, boundsXZ[2] - 3.0f, boundsXZ[3] + 3.0f);
    const float dx = pl.wx - cx;
    const float dz = pl.wz - cz;
    const float reach = pl.radius + 1.0f;
    return (dx * dx + dz * dz) < (reach * reach);
}

inline bool PointLightTouchesEntityXZ(const PointShadowLight& pl, float wx, float wz) noexcept {
    const float dx = wx - pl.wx;
    const float dz = wz - pl.wz;
    const float d2 = dx * dx + dz * dz;
    const float reach = pl.radius + 1.5f;
    // Exclude entities strictly inside the flame origin (<= 0.40) and beyond reach
    return d2 > 0.16f && d2 < (reach * reach);
}

struct GlShadowOps {
    using GenFramebuffers_t          = void (WINAPI*)(int32_t, uint32_t*);
    using DeleteFramebuffers_t       = void (WINAPI*)(int32_t, const uint32_t*);
    using BindFramebuffer_t          = void (WINAPI*)(uint32_t, uint32_t);
    using FramebufferTexture2D_t     = void (WINAPI*)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t);
    using CheckFramebufferStatus_t   = uint32_t (WINAPI*)(uint32_t);
    using GenRenderbuffers_t         = void (WINAPI*)(int32_t, uint32_t*);
    using DeleteRenderbuffers_t      = void (WINAPI*)(int32_t, const uint32_t*);
    using BindRenderbuffer_t         = void (WINAPI*)(uint32_t, uint32_t);
    using RenderbufferStorage_t      = void (WINAPI*)(uint32_t, uint32_t, int32_t, int32_t);
    using FramebufferRenderbuffer_t  = void (WINAPI*)(uint32_t, uint32_t, uint32_t, uint32_t);
    using GenTextures_t              = void (WINAPI*)(int32_t, uint32_t*);
    using DeleteTextures_t           = void (WINAPI*)(int32_t, const uint32_t*);
    using ActiveTexture_t            = void (WINAPI*)(uint32_t);
    using BindTexture_t              = void (WINAPI*)(uint32_t, uint32_t);
    using TexImage2D_t               = void (WINAPI*)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*);
    using TexParameteri_t            = void (WINAPI*)(uint32_t, uint32_t, int32_t);
    using CreateShader_t             = uint32_t (WINAPI*)(uint32_t);
    using ShaderSource_t             = void (WINAPI*)(uint32_t, int32_t, const char* const*, const int32_t*);
    using CompileShader_t            = void (WINAPI*)(uint32_t);
    using GetShaderiv_t              = void (WINAPI*)(uint32_t, uint32_t, int32_t*);
    using GetShaderInfoLog_t         = void (WINAPI*)(uint32_t, int32_t, int32_t*, char*);
    using DeleteShader_t             = void (WINAPI*)(uint32_t);
    using CreateProgram_t            = uint32_t (WINAPI*)();
    using AttachShader_t             = void (WINAPI*)(uint32_t, uint32_t);
    using BindAttribLocation_t       = void (WINAPI*)(uint32_t, uint32_t, const char*);
    using LinkProgram_t              = void (WINAPI*)(uint32_t);
    using GetProgramiv_t             = void (WINAPI*)(uint32_t, uint32_t, int32_t*);
    using GetProgramInfoLog_t        = void (WINAPI*)(uint32_t, int32_t, int32_t*, char*);
    using DeleteProgram_t            = void (WINAPI*)(uint32_t);
    using UseProgram_t               = void (WINAPI*)(uint32_t);
    using GetUniformLocation_t       = int32_t (WINAPI*)(uint32_t, const char*);
    using Uniform1f_t                = void (WINAPI*)(int32_t, float);
    using Uniform1i_t                = void (WINAPI*)(int32_t, int32_t);
    using Uniform4fv_t               = void (WINAPI*)(int32_t, int32_t, const float*);
    using UniformMatrix4fv_t         = void (WINAPI*)(int32_t, int32_t, uint8_t, const float*);
    using GenBuffers_t               = void (WINAPI*)(int32_t, uint32_t*);
    using DeleteBuffers_t            = void (WINAPI*)(int32_t, const uint32_t*);
    using BindBuffer_t               = void (WINAPI*)(uint32_t, uint32_t);
    using BufferData_t               = void (WINAPI*)(uint32_t, ptrdiff_t, const void*, uint32_t);
    using EnableVertexAttribArray_t  = void (WINAPI*)(uint32_t);
    using DisableVertexAttribArray_t = void (WINAPI*)(uint32_t);
    using VertexAttribPointer_t      = void (WINAPI*)(uint32_t, int32_t, uint32_t, uint8_t, int32_t, const void*);
    using DrawArrays_t               = void (WINAPI*)(uint32_t, int32_t, int32_t);
    using DrawElements_t                = void (WINAPI*)(uint32_t, int32_t, uint32_t, const void*);
    using DrawElementsBaseVertex_t      = void (WINAPI*)(uint32_t, int32_t, uint32_t, const void*, int32_t);
    using MultiDrawElementsBaseVertex_t = void (WINAPI*)(uint32_t, const int32_t*, uint32_t, const void* const*, int32_t, const int32_t*);
    using BindBufferBase_t              = void (WINAPI*)(uint32_t, uint32_t, uint32_t);
    using Viewport_t                 = void (WINAPI*)(int32_t, int32_t, int32_t, int32_t);
    using ClearColor_t               = void (WINAPI*)(float, float, float, float);
    using Clear_t                    = void (WINAPI*)(uint32_t);
    using Enable_t                   = void (WINAPI*)(uint32_t);
    using Disable_t                  = void (WINAPI*)(uint32_t);
    using BlendFunc_t                = void (WINAPI*)(uint32_t, uint32_t);
    using BlendFuncSeparate_t        = void (WINAPI*)(uint32_t, uint32_t, uint32_t, uint32_t);
    using BlendEquation_t            = void (WINAPI*)(uint32_t);
    using BlendEquationSeparate_t    = void (WINAPI*)(uint32_t, uint32_t);
    using DepthFunc_t                = void (WINAPI*)(uint32_t);
    using DepthMask_t                = void (WINAPI*)(uint8_t);
    using ColorMask_t                = void (WINAPI*)(uint8_t, uint8_t, uint8_t, uint8_t);
    using GetIntegerv_t              = void (WINAPI*)(uint32_t, int32_t*);
    using GetError_t                 = uint32_t (WINAPI*)();
    using LogFn_t                    = void (*)(const char* msg);

    GenFramebuffers_t          glGenFramebuffers          = nullptr;
    DeleteFramebuffers_t       glDeleteFramebuffers       = nullptr;
    BindFramebuffer_t          glBindFramebuffer          = nullptr;
    FramebufferTexture2D_t     glFramebufferTexture2D     = nullptr;
    CheckFramebufferStatus_t   glCheckFramebufferStatus   = nullptr;
    GenRenderbuffers_t         glGenRenderbuffers         = nullptr;
    DeleteRenderbuffers_t      glDeleteRenderbuffers      = nullptr;
    BindRenderbuffer_t         glBindRenderbuffer         = nullptr;
    RenderbufferStorage_t      glRenderbufferStorage      = nullptr;
    FramebufferRenderbuffer_t  glFramebufferRenderbuffer  = nullptr;
    GenTextures_t              glGenTextures              = nullptr;
    DeleteTextures_t           glDeleteTextures           = nullptr;
    ActiveTexture_t            glActiveTexture            = nullptr;
    BindTexture_t              glBindTexture              = nullptr;
    TexImage2D_t               glTexImage2D               = nullptr;
    TexParameteri_t            glTexParameteri            = nullptr;
    CreateShader_t             glCreateShader             = nullptr;
    ShaderSource_t             glShaderSource             = nullptr;
    CompileShader_t            glCompileShader            = nullptr;
    GetShaderiv_t              glGetShaderiv              = nullptr;
    GetShaderInfoLog_t         glGetShaderInfoLog         = nullptr;
    DeleteShader_t             glDeleteShader             = nullptr;
    CreateProgram_t            glCreateProgram            = nullptr;
    AttachShader_t             glAttachShader             = nullptr;
    BindAttribLocation_t       glBindAttribLocation       = nullptr;
    LinkProgram_t              glLinkProgram              = nullptr;
    GetProgramiv_t             glGetProgramiv             = nullptr;
    GetProgramInfoLog_t        glGetProgramInfoLog        = nullptr;
    DeleteProgram_t            glDeleteProgram            = nullptr;
    UseProgram_t               glUseProgram               = nullptr;
    GetUniformLocation_t       glGetUniformLocation       = nullptr;
    Uniform1f_t                glUniform1f                = nullptr;
    Uniform1i_t                glUniform1i                = nullptr;
    Uniform4fv_t               glUniform4fv               = nullptr;
    UniformMatrix4fv_t         glUniformMatrix4fv         = nullptr;
    GenBuffers_t               glGenBuffers               = nullptr;
    DeleteBuffers_t            glDeleteBuffers            = nullptr;
    BindBuffer_t               glBindBuffer               = nullptr;
    BufferData_t               glBufferData               = nullptr;
    EnableVertexAttribArray_t  glEnableVertexAttribArray  = nullptr;
    DisableVertexAttribArray_t glDisableVertexAttribArray = nullptr;
    VertexAttribPointer_t      glVertexAttribPointer      = nullptr;
    DrawArrays_t               glDrawArrays               = nullptr;
    DrawElements_t             glDrawElements             = nullptr;
    Viewport_t                 glViewport                 = nullptr;
    ClearColor_t               glClearColor               = nullptr;
    Clear_t                    glClear                    = nullptr;
    Enable_t                   glEnable                   = nullptr;
    Disable_t                  glDisable                  = nullptr;
    BlendFunc_t                glBlendFunc                = nullptr;
    BlendFuncSeparate_t        glBlendFuncSeparate        = nullptr;
    BlendEquation_t            glBlendEquation            = nullptr;
    BlendEquationSeparate_t    glBlendEquationSeparate    = nullptr;
    DepthFunc_t                glDepthFunc                = nullptr;
    DepthMask_t                glDepthMask                = nullptr;
    ColorMask_t                glColorMask                = nullptr;
    GetIntegerv_t              glGetIntegerv              = nullptr;
    GetError_t                 glGetError                 = nullptr;
    LogFn_t                    logFn                      = nullptr;
    DrawElementsBaseVertex_t      glDrawElementsBaseVertex      = nullptr;
    BindBufferBase_t              glBindBufferBase              = nullptr;
    MultiDrawElementsBaseVertex_t glMultiDrawElementsBaseVertex = nullptr; // Optional GL 3.2+ core (coalesces multi-baseVertex GPU shadow batches)

    bool IsComplete() const noexcept {
        return glGenFramebuffers && glDeleteFramebuffers && glBindFramebuffer &&
               glFramebufferTexture2D && glCheckFramebufferStatus &&
               glGenRenderbuffers && glDeleteRenderbuffers && glBindRenderbuffer &&
               glRenderbufferStorage && glFramebufferRenderbuffer &&
               glGenTextures && glDeleteTextures && glActiveTexture && glBindTexture &&
               glTexImage2D && glTexParameteri &&
               glCreateShader && glShaderSource && glCompileShader && glGetShaderiv && glDeleteShader &&
               glCreateProgram && glAttachShader && glBindAttribLocation && glLinkProgram &&
               glGetProgramiv && glDeleteProgram && glUseProgram && glGetUniformLocation &&
               glUniform1f && glUniform1i && glUniform4fv && glUniformMatrix4fv &&
               glGenBuffers && glDeleteBuffers && glBindBuffer && glBufferData &&
               glEnableVertexAttribArray && glDisableVertexAttribArray && glVertexAttribPointer &&
               glDrawArrays && glDrawElements && glViewport && glClearColor && glClear &&
               glEnable && glDisable && glBlendFunc && glBlendEquation &&
               glDepthFunc && glDepthMask && glColorMask;
    }
};

struct SavedAttrib0 {
    uint32_t    buffer     = 0;
    int32_t     size       = 4;
    uint32_t    type       = kGlFloat;
    uint8_t     normalized = 0;
    int32_t     stride     = 0;
    const void* pointer    = nullptr;
    bool        valid      = false;
    bool        enabled    = false;
};

struct ShadowPassState {
    uint32_t     dstFbo            = 0;
    uint32_t     dstProg           = 0;
    int32_t      vp[4]             = { 0, 0, 1280, 720 };
    uint32_t     activeUnit        = 0;
    uint32_t     boundArrayBuf     = 0;
    uint32_t     boundElemBuf      = 0;
    float        clearColor[4]     = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool         hasClearColor     = false;
    bool         glCacheTrusted    = true;
    bool         blendCacheTrusted = false; // Opt-in: true when host only calls tracked symmetric glBlendFunc/glBlendEquation (0 blend glGetIntegerv!)
    SavedAttrib0 attr0{};
    SavedAttrib0 attr1{};
};

// =============================================================================
// Internal GLSL ES 1.00 Shaders (verified on ANGLE 1.0.0.2249 + Desktop GL 4.6)
// =============================================================================
inline constexpr const char* kShadowBatchVs =
    "uniform vec4 u_bounds;\n"
    "uniform vec4 u_shadProj;\n"
    "uniform vec4 u_shadDyn;\n"
    "uniform vec4 u_camRight;\n"
    "uniform vec4 u_pointLight;\n"
    "attribute vec4 POS2D_UV;\n"
    "attribute vec4 HS_BATCH_WPOS;\n"
    "attribute vec4 HS_BATCH_CMULT;\n"
    "attribute vec4 HS_BATCH_CADD;\n"
    "varying vec3 v_tc;\n"
    "varying vec3 v_meta;\n"
    "void main() {\n"
    "\tif ((POS2D_UV.x - 2.0 * floor(POS2D_UV.x * 0.5)) < 0.5) {\n"
    "\t\tgl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "\t\tv_tc = vec3(0.0);\n"
    "\t\tv_meta = vec3(0.0);\n"
    "\t\treturn;\n"
    "\t}\n"
    "\tfloat hs_si = floor(POS2D_UV.z * 0.5);\n"
    "\tv_tc = vec3(POS2D_UV.z - 2.0 * hs_si, POS2D_UV.w, HS_BATCH_WPOS.w);\n"
    "\tfloat hs_h = clamp(HS_BATCH_WPOS.y, 0.0, 7.5);\n"
    "\tvec2 hs_cr = normalize(u_camRight.xy + vec2(1e-6, 0.0));\n"
    "\tvec2 hs_cf = vec2(-hs_cr.y, hs_cr.x);\n"
    "\tfloat hs_lx = HS_BATCH_CADD.w;\n"
    "\tvec2 hs_anch = HS_BATCH_WPOS.xz - hs_cr * hs_lx;\n"
    "\tvec2 hs_dir = normalize(u_shadProj.xy + vec2(1e-6, 0.0));\n"
    "\tfloat hs_stretch = u_shadProj.z;\n"
    "\tfloat hs_lw = clamp(u_pointLight.w, 0.0, 1.0);\n"
    "\tfloat hs_airFade = clamp(1.0 - max(max(POS2D_UV.y, 0.0) - 0.45, 0.0) / 8.5, 0.08, 1.0);\n"
    "\ths_lw *= hs_airFade;\n"
    "\tif (u_pointLight.z > 0.5) {\n"
    "\t\tvec2 hs_d = hs_anch - u_pointLight.xy;\n"
    "\t\tfloat hs_dist = length(hs_d);\n"
    "\t\tfloat hs_rad = max(u_pointLight.z, 1.0);\n"
    "\t\tfloat hs_near = smoothstep(0.42, 1.15, hs_dist);\n"
    "\t\tfloat hs_far = smoothstep(hs_rad, hs_rad * 0.25, hs_dist);\n"
    "\t\ths_lw = hs_near * hs_far * hs_lw;\n"
    "\t\tif (hs_lw <= 0.008) {\n"
    "\t\t\tgl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "\t\t\tv_meta = vec3(0.0);\n"
    "\t\t\treturn;\n"
    "\t\t}\n"
    "\t\ths_dir = hs_d / max(hs_dist, 0.42);\n"
    "\t\tfloat hs_plen = clamp(u_camRight.w, 0.50, 1.80);\n"
    "\t\ths_stretch = (0.55 + clamp(hs_dist / hs_rad, 0.0, 1.0) * 1.70) * hs_plen;\n"
    "\t} else if (hs_lw <= 0.005) {\n"
    "\t\tgl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "\t\tv_meta = vec3(0.0);\n"
    "\t\treturn;\n"
    "\t}\n"
    "\tv_meta = vec3(hs_h, HS_BATCH_CMULT.a, hs_lw);\n"
    "\tvec2 hs_perp = vec2(-hs_dir.y, hs_dir.x);\n"
    "\tif (dot(hs_perp, hs_cr) + 0.05 * dot(hs_perp, hs_cf) < 0.0) {\n"
    "\t\ths_perp = -hs_perp;\n"
    "\t}\n"
    "\tvec2 hs_flat = hs_anch + hs_perp * hs_lx;\n"
    "\tvec2 hs_wvec = vec2(0.0);\n"
    "\tif (hs_h > 0.60 && u_shadDyn.y > 0.0) {\n"
    "\t\tfloat hs_wt = u_shadDyn.x;\n"
    "\t\tfloat hs_ph = hs_anch.x * 0.29 + hs_anch.y * 0.21;\n"
    "\t\tfloat hs_bz = sin(hs_wt * 1.15 + hs_ph + hs_h * 0.14) * 0.65\n"
    "\t\t            + sin(hs_wt * 2.03 + hs_ph * 1.70 - hs_h * 0.22) * 0.35;\n"
    "\t\tfloat hs_sw0 = smoothstep(0.60, 3.60, hs_h);\n"
    "\t\tfloat hs_soff = hs_bz * (hs_sw0 * hs_sw0) * clamp(u_shadDyn.y, 0.0, 1.5) * (0.28 + 0.06 * hs_h);\n"
    "\t\ths_wvec = hs_perp * hs_soff + hs_dir * (hs_soff * 0.35);\n"
    "\t}\n"
    "\tvec2 hs_pxz = hs_flat + hs_dir * (hs_h * hs_stretch) + hs_wvec;\n"
    "\tvec2 hs_ndc = (hs_pxz - u_bounds.xy) * u_bounds.z;\n"
    "\tfloat hs_hn = hs_h / 7.5;\n"
    "\tfloat hs_zKey = (u_bounds.w > 0.5) ? ((1.0 - hs_lw) * 0.92 + hs_hn * 0.08) : hs_hn;\n"
    "\tfloat hs_z = hs_zKey * 1.98 - 0.99;\n"
    "\tgl_Position = vec4(hs_ndc.x, hs_ndc.y, hs_z, 1.0);\n"
    "}\n";

inline constexpr const char* kShadowBatchFs =
    "#if defined(GL_ES)\n"
    "precision highp float;\n"
    "#endif\n"
    "uniform sampler2D HS_BATCH_TEX0;\n"
    "uniform sampler2D HS_BATCH_TEX1;\n"
    "uniform sampler2D HS_BATCH_TEX2;\n"
    "uniform sampler2D HS_BATCH_TEX3;\n"
    "uniform sampler2D HS_BATCH_TEX4;\n"
    "uniform sampler2D HS_BATCH_TEX5;\n"
    "uniform sampler2D HS_BATCH_TEX6;\n"
    "uniform sampler2D HS_BATCH_TEX7;\n"
    "uniform vec4 u_bounds;\n"
    "uniform vec4 u_shadDyn;\n"
    "varying vec3 v_tc;\n"
    "varying vec3 v_meta;\n"
    "void main() {\n"
    "\tfloat s = v_tc.z;\n"
    "\tvec4 c;\n"
    "\tif (s < 0.5)      c = texture2D(HS_BATCH_TEX0, v_tc.xy);\n"
    "\telse if (s < 1.5) c = texture2D(HS_BATCH_TEX1, v_tc.xy);\n"
    "\telse if (s < 2.5) c = texture2D(HS_BATCH_TEX2, v_tc.xy);\n"
    "\telse if (s < 3.5) c = texture2D(HS_BATCH_TEX3, v_tc.xy);\n"
    "\telse if (s < 4.5) c = texture2D(HS_BATCH_TEX4, v_tc.xy);\n"
    "\telse if (s < 5.5) c = texture2D(HS_BATCH_TEX5, v_tc.xy);\n"
    "\telse if (s < 6.5) c = texture2D(HS_BATCH_TEX6, v_tc.xy);\n"
    "\telse              c = texture2D(HS_BATCH_TEX7, v_tc.xy);\n"
    "\tfloat a = c.a * v_meta.y;\n"
    "\tfloat h = v_meta.x;\n"
    "\tfloat hf = clamp(u_shadDyn.z, 0.0, 1.0);\n"
    "\tfloat hfade = mix(1.0, 1.0 - 0.58 * smoothstep(0.60, 5.80, h), hf);\n"
    "\tfloat root = smoothstep(0.01, 0.14, h);\n"
    "\tfloat cov = smoothstep(0.16, 0.66, a) * root * hfade * clamp(v_meta.z, 0.0, 1.0);\n"
    "\tif (u_bounds.w > 0.5 && cov < 0.08) {\n"
    "\t\tdiscard;\n"
    "\t}\n"
    "\tfloat hNorm = clamp(h / 7.5, 0.0, 1.0);\n"
    "\tfloat prox = (1.0 - hNorm) * step(0.05, cov);\n"
    "\tfloat hMax = hNorm * step(0.12, cov);\n"
    "\tgl_FragColor = vec4(cov, prox, hMax, 1.0);\n"
    "}\n";

inline constexpr const char* kShadowBatchFs16 =
    "#if defined(GL_ES)\n"
    "precision highp float;\n"
    "#endif\n"
    "uniform sampler2D HS_BATCH_TEX0;\n"
    "uniform sampler2D HS_BATCH_TEX1;\n"
    "uniform sampler2D HS_BATCH_TEX2;\n"
    "uniform sampler2D HS_BATCH_TEX3;\n"
    "uniform sampler2D HS_BATCH_TEX4;\n"
    "uniform sampler2D HS_BATCH_TEX5;\n"
    "uniform sampler2D HS_BATCH_TEX6;\n"
    "uniform sampler2D HS_BATCH_TEX7;\n"
    "uniform sampler2D HS_BATCH_TEX8;\n"
    "uniform sampler2D HS_BATCH_TEX9;\n"
    "uniform sampler2D HS_BATCH_TEX10;\n"
    "uniform sampler2D HS_BATCH_TEX11;\n"
    "uniform sampler2D HS_BATCH_TEX12;\n"
    "uniform sampler2D HS_BATCH_TEX13;\n"
    "uniform sampler2D HS_BATCH_TEX14;\n"
    "uniform sampler2D HS_BATCH_TEX15;\n"
    "uniform vec4 u_bounds;\n"
    "uniform vec4 u_shadDyn;\n"
    "varying vec3 v_tc;\n"
    "varying vec3 v_meta;\n"
    "void main() {\n"
    "\tfloat s = v_tc.z;\n"
    "\tvec4 c;\n"
    "\tif (s < 7.5) {\n"
    "\t\tif (s < 3.5) {\n"
    "\t\t\tif (s < 1.5)      c = (s < 0.5) ? texture2D(HS_BATCH_TEX0, v_tc.xy) : texture2D(HS_BATCH_TEX1, v_tc.xy);\n"
    "\t\t\telse              c = (s < 2.5) ? texture2D(HS_BATCH_TEX2, v_tc.xy) : texture2D(HS_BATCH_TEX3, v_tc.xy);\n"
    "\t\t} else {\n"
    "\t\t\tif (s < 5.5)      c = (s < 4.5) ? texture2D(HS_BATCH_TEX4, v_tc.xy) : texture2D(HS_BATCH_TEX5, v_tc.xy);\n"
    "\t\t\telse              c = (s < 6.5) ? texture2D(HS_BATCH_TEX6, v_tc.xy) : texture2D(HS_BATCH_TEX7, v_tc.xy);\n"
    "\t\t}\n"
    "\t} else {\n"
    "\t\tif (s < 11.5) {\n"
    "\t\t\tif (s < 9.5)      c = (s < 8.5) ? texture2D(HS_BATCH_TEX8, v_tc.xy) : texture2D(HS_BATCH_TEX9, v_tc.xy);\n"
    "\t\t\telse              c = (s < 10.5) ? texture2D(HS_BATCH_TEX10, v_tc.xy) : texture2D(HS_BATCH_TEX11, v_tc.xy);\n"
    "\t\t} else {\n"
    "\t\t\tif (s < 13.5)     c = (s < 12.5) ? texture2D(HS_BATCH_TEX12, v_tc.xy) : texture2D(HS_BATCH_TEX13, v_tc.xy);\n"
    "\t\t\telse              c = (s < 14.5) ? texture2D(HS_BATCH_TEX14, v_tc.xy) : texture2D(HS_BATCH_TEX15, v_tc.xy);\n"
    "\t\t}\n"
    "\t}\n"
    "\tfloat a = c.a * v_meta.y;\n"
    "\tfloat h = v_meta.x;\n"
    "\tfloat hf = clamp(u_shadDyn.z, 0.0, 1.0);\n"
    "\tfloat hfade = mix(1.0, 1.0 - 0.58 * smoothstep(0.60, 5.80, h), hf);\n"
    "\tfloat root = smoothstep(0.01, 0.14, h);\n"
    "\tfloat cov = smoothstep(0.16, 0.66, a) * root * hfade * clamp(v_meta.z, 0.0, 1.0);\n"
    "\tif (u_bounds.w > 0.5 && cov < 0.08) {\n"
    "\t\tdiscard;\n"
    "\t}\n"
    "\tfloat hNorm = clamp(h / 7.5, 0.0, 1.0);\n"
    "\tfloat prox = (1.0 - hNorm) * step(0.05, cov);\n"
    "\tfloat hMax = hNorm * step(0.12, cov);\n"
    "\tgl_FragColor = vec4(cov, prox, hMax, 1.0);\n"
    "}\n";

inline constexpr const char* kShadowBatchGpuVs =
    "#version 430 compatibility\n"
    "#extension GL_ARB_bindless_texture : require\n"
    "struct HsGpuInst { vec4 m0; vec4 m1; vec4 m2; vec4 cmult; vec4 cadd; vec4 meta; vec4 meta2; uvec2 tex0; uvec2 tex1; uvec2 tex2; uvec2 flags; };\n"
    "layout(std430, binding = 0) readonly buffer HsGpuInstBuf { HsGpuInst hs_gpu_insts[]; };\n"
    "uniform vec4 u_bounds;\n"
    "uniform vec4 u_shadProj;\n"
    "uniform vec4 u_shadDyn;\n"
    "uniform vec4 u_camRight;\n"
    "uniform vec4 u_pointLight;\n"
    "attribute vec4 POS2D_UV;\n"
    "attribute vec4 HS_BATCH_WPOS;\n"
    "varying vec2 v_tc;\n"
    "varying vec3 v_meta;\n"
    "flat out uvec2 v_btex;\n"
    "void main() {\n"
    "\tint hs_iid = int(HS_BATCH_WPOS.x + 0.5);\n"
    "\tHsGpuInst hs_ins = hs_gpu_insts[hs_iid];\n"
    "\tif (hs_ins.cadd.w < 0.5) {\n"
    "\t\tgl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "\t\tv_tc = vec2(0.0);\n"
    "\t\tv_meta = vec3(0.0);\n"
    "\t\tv_btex = uvec2(0u, 0u);\n"
    "\t\treturn;\n"
    "\t}\n"
    "\tvec2 hs_p = POS2D_UV.xy;\n"
    "\tfloat hs_dx = hs_ins.m0.x * hs_p.x + hs_ins.m1.x * hs_p.y;\n"
    "\tfloat hs_dy = hs_ins.m0.y * hs_p.x + hs_ins.m1.y * hs_p.y;\n"
    "\tfloat hs_dz = hs_ins.m0.z * hs_p.x + hs_ins.m1.z * hs_p.y;\n"
    "\tfloat hs_wx = hs_dx + hs_ins.m2.x;\n"
    "\tfloat hs_wy = hs_dy + hs_ins.m2.y;\n"
    "\tfloat hs_wz = hs_dz + hs_ins.m2.z;\n"
    "\tfloat hs_ho = 0.0;\n"
    "\tif (hs_ins.meta2.z > 0.5 && hs_dy > 0.06) {\n"
    "\t\tif (hs_ins.meta2.z < 1.5) {\n"
    "\t\t\ths_ho = hs_ins.meta2.x * smoothstep(0.08, 1.35, hs_dy)\n"
    "\t\t\t      + hs_ins.meta2.y * smoothstep(0.06, 1.10, hs_dy);\n"
    "\t\t} else {\n"
    "\t\t\tfloat hs_b2 = smoothstep(1.00, 3.80, hs_dy);\n"
    "\t\t\tfloat hs_hc = min(hs_dy, 8.0);\n"
    "\t\t\ths_ho = hs_ins.meta2.x * (hs_b2 * hs_b2) * (0.18 + 0.025 * hs_hc);\n"
    "\t\t}\n"
    "\t\ths_wx += hs_ins.meta.w * hs_ho;\n"
    "\t\ths_wz += hs_ins.meta2.w * hs_ho;\n"
    "\t}\n"
    "\tfloat hs_si = floor(POS2D_UV.z * 0.5);\n"
    "\tv_tc = vec2(POS2D_UV.z - 2.0 * hs_si, POS2D_UV.w);\n"
    "\tv_btex = (hs_si < 0.5) ? hs_ins.tex0 : ((hs_si < 1.5) ? hs_ins.tex1 : hs_ins.tex2);\n"
    "\tfloat hs_h = clamp(hs_wy, 0.0, 7.5);\n"
    "\tvec2 hs_cr = normalize(u_camRight.xy + vec2(1e-6, 0.0));\n"
    "\tvec2 hs_cf = vec2(-hs_cr.y, hs_cr.x);\n"
    "\tfloat hs_lx = dot(vec3(hs_dx, hs_dy, hs_dz), hs_ins.meta.xyz) + hs_ho;\n"
    "\tvec2 hs_anch = vec2(hs_wx, hs_wz) - hs_cr * hs_lx;\n"
    "\tvec2 hs_dir = normalize(u_shadProj.xy + vec2(1e-6, 0.0));\n"
    "\tfloat hs_stretch = u_shadProj.z;\n"
    "\tfloat hs_lw = clamp(u_pointLight.w, 0.0, 1.0);\n"
    "\tfloat hs_airFade = clamp(1.0 - max(max(hs_ins.m0.w, 0.0) - 0.45, 0.0) / 8.5, 0.08, 1.0);\n"
    "\ths_lw *= hs_airFade;\n"
    "\tif (u_pointLight.z > 0.5) {\n"
    "\t\tvec2 hs_d = hs_anch - u_pointLight.xy;\n"
    "\t\tfloat hs_dist = length(hs_d);\n"
    "\t\tfloat hs_rad = max(u_pointLight.z, 1.0);\n"
    "\t\tfloat hs_near = smoothstep(0.42, 1.15, hs_dist);\n"
    "\t\tfloat hs_far = smoothstep(hs_rad, hs_rad * 0.25, hs_dist);\n"
    "\t\ths_lw = hs_near * hs_far * hs_lw;\n"
    "\t\tif (hs_lw <= 0.008) {\n"
    "\t\t\tgl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "\t\t\tv_meta = vec3(0.0);\n"
    "\t\t\treturn;\n"
    "\t\t}\n"
    "\t\ths_dir = hs_d / max(hs_dist, 0.42);\n"
    "\t\tfloat hs_plen = clamp(u_camRight.w, 0.50, 1.80);\n"
    "\t\ths_stretch = (0.55 + clamp(hs_dist / hs_rad, 0.0, 1.0) * 1.70) * hs_plen;\n"
    "\t} else if (hs_lw <= 0.005) {\n"
    "\t\tgl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "\t\tv_meta = vec3(0.0);\n"
    "\t\treturn;\n"
    "\t}\n"
    "\tv_meta = vec3(hs_h, hs_ins.cmult.a, hs_lw);\n"
    "\tvec2 hs_perp = vec2(-hs_dir.y, hs_dir.x);\n"
    "\tif (dot(hs_perp, hs_cr) + 0.05 * dot(hs_perp, hs_cf) < 0.0) {\n"
    "\t\ths_perp = -hs_perp;\n"
    "\t}\n"
    "\tvec2 hs_flat = hs_anch + hs_perp * hs_lx;\n"
    "\tvec2 hs_wvec = vec2(0.0);\n"
    "\tif (hs_h > 0.60 && u_shadDyn.y > 0.0) {\n"
    "\t\tfloat hs_wt = u_shadDyn.x;\n"
    "\t\tfloat hs_ph = hs_anch.x * 0.29 + hs_anch.y * 0.21;\n"
    "\t\tfloat hs_bz = sin(hs_wt * 1.15 + hs_ph + hs_h * 0.14) * 0.65\n"
    "\t\t            + sin(hs_wt * 2.03 + hs_ph * 1.70 - hs_h * 0.22) * 0.35;\n"
    "\t\tfloat hs_sw0 = smoothstep(0.60, 3.60, hs_h);\n"
    "\t\tfloat hs_soff = hs_bz * (hs_sw0 * hs_sw0) * clamp(u_shadDyn.y, 0.0, 1.5) * (0.28 + 0.06 * hs_h);\n"
    "\t\ths_wvec = hs_perp * hs_soff + hs_dir * (hs_soff * 0.35);\n"
    "\t}\n"
    "\tvec2 hs_pxz = hs_flat + hs_dir * (hs_h * hs_stretch) + hs_wvec;\n"
    "\tvec2 hs_ndc = (hs_pxz - u_bounds.xy) * u_bounds.z;\n"
    "\tfloat hs_hn = hs_h / 7.5;\n"
    "\tfloat hs_zKey = (u_bounds.w > 0.5) ? ((1.0 - hs_lw) * 0.92 + hs_hn * 0.08) : hs_hn;\n"
    "\tfloat hs_z = hs_zKey * 1.98 - 0.99;\n"
    "\tgl_Position = vec4(hs_ndc.x, hs_ndc.y, hs_z, 1.0);\n"
    "}\n";

inline constexpr const char* kShadowBatchGpuFs =
    "#version 430 compatibility\n"
    "#extension GL_ARB_bindless_texture : require\n"
    "uniform vec4 u_shadDyn;\n"
    "varying vec2 v_tc;\n"
    "varying vec3 v_meta;\n"
    "flat in uvec2 v_btex;\n"
    "void main() {\n"
    "\tvec4 c = texture2D(sampler2D(v_btex), v_tc);\n"
    "\tfloat a = c.a * v_meta.y;\n"
    "\tfloat h = v_meta.x;\n"
    "\tfloat hf = clamp(u_shadDyn.z, 0.0, 1.0);\n"
    "\tfloat hfade = mix(1.0, 1.0 - 0.58 * smoothstep(0.60, 5.80, h), hf);\n"
    "\tfloat root = smoothstep(0.01, 0.14, h);\n"
    "\tfloat cov = smoothstep(0.16, 0.66, a) * root * hfade * clamp(v_meta.z, 0.0, 1.0);\n"
    "\tfloat hNorm = clamp(h / 7.5, 0.0, 1.0);\n"
    "\tfloat prox = (1.0 - hNorm) * step(0.05, cov);\n"
    "\tfloat hMax = hNorm * step(0.12, cov);\n"
    "\tgl_FragColor = vec4(cov, prox, hMax, 1.0);\n"
    "}\n";

inline constexpr const char* kShadowCompositeVs =
    "uniform mat4 u_curPV;\n"
    "uniform vec4 u_prevBounds;\n"
    "attribute vec2 a_corner;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "\tvec2 wxz = u_prevBounds.xy + a_corner * u_prevBounds.z;\n"
    "\tv_uv = a_corner * 0.5 + 0.5;\n"
    "\tgl_Position = u_curPV * vec4(wxz.x, 0.04, wxz.y, 1.0);\n"
    "}\n";

inline constexpr const char* kShadowCompositeFs =
    "#if defined(GL_ES)\n"
    "precision highp float;\n"
    "#endif\n"
    "uniform sampler2D u_shadowMap;\n"
    "uniform sampler2D u_blueNoise;\n"
    "uniform vec4 u_shadowCfg;\n"
    "uniform vec4 u_filterCfg;\n"
    "varying vec2 v_uv;\n"
    "void main() {\n"
    "\tvec2 edge = abs(v_uv * 2.0 - 1.0);\n"
    "\tfloat boxFade = 1.0 - smoothstep(0.90, 1.0, max(edge.x, edge.y));\n"
    "\tif (boxFade <= 0.001) {\n"
    "\t\tgl_FragColor = vec4(1.0);\n"
    "\t\treturn;\n"
    "\t}\n"
    "\tvec4 c0 = texture2D(u_shadowMap, v_uv);\n"
    "\tfloat softVal = clamp(u_filterCfg.y, 0.0, 2.0);\n"
    "\tfloat cov;\n"
    "\tif (softVal <= 0.03) {\n"
    "\t\tcov = clamp(c0.r, 0.0, 1.0) * boxFade;\n"
    "\t} else {\n"
    "\t\tfloat hNorm = mix(0.65, clamp(1.0 - c0.g, 0.0, 1.0), step(0.02, c0.r));\n"
    "\t\tfloat softRamp = smoothstep(0.03, 0.40, softVal);\n"
    "\t\tfloat basePx = u_filterCfg.x * (0.95 * softRamp + 1.85 * softVal) * clamp(u_filterCfg.z, 0.50, 2.20);\n"
    "\t\tfloat ang;\n"
    "\t\tfloat radJit = 1.0;\n"
    "\t\tif (u_filterCfg.w > 0.5) {\n"
    "\t\t\tvec2 bn = texture2D(u_blueNoise, gl_FragCoord.xy * 0.015625).rg;\n"
    "\t\t\tang = 6.2831853 * bn.r;\n"
    "\t\t\tradJit = 0.86 + 0.28 * bn.g;\n"
    "\t\t} else {\n"
    "\t\t\tang = 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
    "\t\t}\n"
    "\t\tfloat rad = basePx * mix(0.32, 1.45, hNorm) * radJit;\n"
    "\t\tfloat ca = cos(ang);\n"
    "\t\tfloat sa = sin(ang);\n"
    "\t\tvec2 rx = vec2(ca, sa) * rad;\n"
    "\t\tvec2 ry = vec2(-sa, ca) * rad;\n"
    "\t\tfloat s0 = c0.r;\n"
    "\t\tfloat s1 = texture2D(u_shadowMap, v_uv + rx *  0.3536               ).r;\n"
    "\t\tfloat s2 = texture2D(u_shadowMap, v_uv + rx * -0.3688 + ry *  0.3375).r;\n"
    "\t\tfloat s3 = texture2D(u_shadowMap, v_uv + rx *  0.0437 + ry * -0.6108).r;\n"
    "\t\tfloat s4 = texture2D(u_shadowMap, v_uv + rx *  0.4535 + ry *  0.5513).r;\n"
    "\t\tfloat s5 = texture2D(u_shadowMap, v_uv + rx * -0.7900 + ry * -0.0566).r;\n"
    "\t\tfloat s6 = texture2D(u_shadowMap, v_uv + rx *  0.6250 + ry * -0.6020).r;\n"
    "\t\tfloat s7 = texture2D(u_shadowMap, v_uv + rx * -0.0728 + ry *  0.9325).r;\n"
    "\t\tfloat s8 = texture2D(u_shadowMap, v_uv + rx * -0.6644 + ry * -0.7474).r;\n"
    "\t\tcov = (s0 * 2.2 + (s1 + s2 + s3 + s4) * 1.15 + (s5 + s6 + s7 + s8) * 0.80) * 0.10;\n"
    "\t\tcov = clamp(cov, 0.0, 1.0) * boxFade;\n"
    "\t}\n"
    "\tfloat eff = cov * clamp(u_shadowCfg.w, 0.0, 1.0);\n"
    "\tvec3 mulCol = mix(vec3(1.0), clamp(u_shadowCfg.rgb, vec3(0.0), vec3(1.0)), eff);\n"
    "\tgl_FragColor = vec4(mulCol, 1.0);\n"
    "}\n";

inline bool IsStandingMatrix(const float* m) noexcept {
    if (!m) return false;
    const float c1LenSq = m[4] * m[4] + m[5] * m[5] + m[6] * m[6];
    return (c1LenSq > 1e-8f) && ((m[5] * m[5]) > 0.1225f * c1LenSq);
}

inline void MultiplyMat4ColMajor(const float* a, const float* b, float* out) noexcept {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0]
                           + a[1 * 4 + r] * b[c * 4 + 1]
                           + a[2 * 4 + r] * b[c * 4 + 2]
                           + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
}

class WorldShadowEngine {
public:
    WorldShadowEngine() = default;

    void SetOps(const GlShadowOps& ops, bool isNativeGl) noexcept {
        ops_ = ops;
        isNativeGl_ = isNativeGl;
        if (hdResCap_ == 2048u && isNativeGl) {
            hdResCap_ = 4096u;
        }
    }

    bool IsReady() const noexcept { return ready_ && !failed_; }
    bool IsFailed() const noexcept { return failed_; }
    bool HasBlueNoise() const noexcept { return texBlueNoise_ != 0u; }
    uint32_t Resolution() const noexcept { return res_; }
    bool UsesGlMax() const noexcept { return isNativeGl_; }
    uint32_t FrameRecvGen() const noexcept { return frameRecvGen_; }
    uint32_t LastFrameShadowDraws() const noexcept { return lastFrameDraws_; }
    uint32_t LastFrameBatchedDraws() const noexcept { return lastFrameBatchDraws_; }
    uint32_t LastFrameUnbatchedDraws() const noexcept { return lastFrameUnbatchedDraws_; }
    uint32_t LastFrameDeferredBatches() const noexcept { return lastFrameDeferredBatches_; }
    // Number of deferred shadow draw calls eliminated (merged) via contiguous / MultiDraw coalescing last frame
    // (e.g. N batches coalesced into 1 GL draw -> LastFrameDeferredBatches()==N, LastFrameShadowDraws()==1, LastFrameCoalescedDraws()==N-1).
    uint32_t LastFrameCoalescedDraws() const noexcept { return lastFrameCoalescedDraws_; }
    uint64_t ShadowReuseSkips() const noexcept { return totalShadowReuseSkips_; }
    bool     LastFrameReused() const noexcept { return lastFrameReused_; }
    bool     LastFrameShadowReused() const noexcept { return lastFrameReused_; }
    bool     HasDeferredBatches() const noexcept { return !deferredBatches_.empty(); }
    bool     HasGpuBatchProgram() const noexcept { return progBatchGpu_ != 0u; }
    bool     IsFastEntityReceive1Tap() const noexcept { return fastEntityReceive1Tap_; }

    void GetCachedBounds(float outBounds[4]) const noexcept {
        if (!outBounds) return;
        outBounds[0] = prevCenterX_;
        outBounds[1] = prevCenterZ_;
        outBounds[2] = 1.0f / kShadowHalfExtent;
        outBounds[3] = hasPrevMap_ ? 1.0f : 0.0f;
    }

    bool HasEntityReceiveData(const ShadowConfig& cfg) const noexcept {
        return ready_ && hasPrevMap_ && compositedThisFrame_ &&
               HasActiveDirectionalShadow(cfg) && cfg.dirStrength > 0.05f &&
               cfg.density > 0.001f && cfg.entityRecv > 0.001f;
    }

    void GetEntityReceiveUniforms(const ShadowConfig& cfg, float outBounds[4], float outTint[4]) const noexcept {
        if (HasEntityReceiveData(cfg)) {
            const float effRecv = std::clamp(cfg.density, 0.0f, 1.0f) *
                                  std::clamp(cfg.entityRecv, 0.0f, 1.0f) *
                                  std::clamp(cfg.dirStrength, 0.0f, 1.0f);
            const bool fast1Tap = cfg.fastEntityReceive1Tap;
            fastEntityReceive1Tap_ = fast1Tap;
            outBounds[0] = prevCenterX_;
            outBounds[1] = prevCenterZ_;
            outBounds[2] = 1.0f / kShadowHalfExtent;
            outBounds[3] = fast1Tap ? 0.65f : (isNativeGl_ ? 1.0f : 2.0f); // 0.65 = 1-tap fast receive, 1.0 = exact GL_MAX 5-tap, 2.0 = ANGLE DEPTH16 wide-tap
            outTint[0]   = cfg.tintR;
            outTint[1]   = cfg.tintG;
            outTint[2]   = cfg.tintB;
            outTint[3]   = effRecv;
        } else {
            outBounds[0] = 0.0f;
            outBounds[1] = 0.0f;
            outBounds[2] = 1.0f / kShadowHalfExtent;
            outBounds[3] = 0.0f;
            outTint[0]   = 1.0f;
            outTint[1]   = 1.0f;
            outTint[2]   = 1.0f;
            outTint[3]   = 0.0f;
        }
    }

    void ResetContext(bool hasAliveCtx) noexcept {
        if (hasAliveCtx && ops_.IsComplete()) {
            DestroyResources();
        } else {
            fbo_[0] = fbo_[1] = 0;
            tex_[0] = tex_[1] = 0;
            depthRbo_[0] = depthRbo_[1] = 0;
            texBlueNoise_ = 0;
            progBatch_ = 0;
            progBatchGpu_ = 0;
            progComposite_ = 0;
            quadVbo_ = 0;
        }
        ready_ = false;
        failed_ = false;
        res_ = 0;
        hdResCap_ = isNativeGl_ ? 4096u : 2048u;
        hasPrevMap_ = false;
        hasCachedSnapshot_ = false;
        reuseStreak_ = 0u;
        lastFrameReused_ = false;
        fastEntityReceive1Tap_ = false;
        lastFrameCoalescedDraws_ = 0u;
        totalShadowReuseSkips_ = 0ULL;
        ++frameRecvGen_;
        ResetFrameFlags();
        ++frameRecvGen_;
    }

    void OnGroundOrOceanDraw(uint32_t curFbo, const int32_t vp[4]) noexcept {
        worldFbo_ = curFbo;
        worldVp_[0] = vp[0];
        worldVp_[1] = vp[1];
        worldVp_[2] = vp[2];
        worldVp_[3] = vp[3];
        sawGroundThisFrame_ = true;
    }

    void OnMatrixP(const float* m) noexcept {
        if (!m) return;
        std::memcpy(curMatrixP_, m, sizeof(curMatrixP_));
        hasMatrixP_ = true;
        if (hasMatrixV_) {
            MultiplyMat4ColMajor(curMatrixP_, curMatrixV_, curPV_);
            hasPV_ = true;
        }
    }

    void OnMatrixV(const float* m) noexcept {
        if (!m) return;
        std::memcpy(curMatrixV_, m, sizeof(curMatrixV_));
        hasMatrixV_ = true;
        if (hasMatrixP_) {
            MultiplyMat4ColMajor(curMatrixP_, curMatrixV_, curPV_);
            hasPV_ = true;
        }
        ExtractCameraBasisFromMatrixV(m);
    }

    void OnMatrixPV(const float* pv) noexcept {
        if (!pv) return;
        std::memcpy(curPV_, pv, sizeof(curPV_));
        hasPV_ = true;
    }

    bool HasCamRight() const noexcept { return hasCamRight_; }
    float CamRightX() const noexcept { return camRightX_; }
    float CamRightZ() const noexcept { return camRightZ_; }
    uint32_t WorldFbo() const noexcept { return worldFbo_; }
    bool SawGroundThisFrame() const noexcept { return sawGroundThisFrame_; }
    bool BeganWriteThisFrame() const noexcept { return beganWriteThisFrame_; }
    bool IsInsideWorldPass(uint32_t curFbo) const noexcept {
        return sawGroundThisFrame_ && curFbo == worldFbo_ && worldVp_[2] > 0 && worldVp_[3] > 0;
    }

    bool NeedsCompositeOrBeginWrite(uint32_t curFbo, const ShadowConfig& cfg) const noexcept {
        if (failed_ || !HasAnyShadowLight(cfg)) return false;
        if (!IsInsideWorldPass(curFbo)) return false;
        return !frameShadowHandled_;
    }

    bool IsEntityInsideShadowBox(float wx, float wz) const noexcept {
        const float margin = kShadowHalfExtent + 10.0f;
        return std::fabs(wx - writeCenterX_) <= margin && std::fabs(wz - writeCenterZ_) <= margin;
    }

    // Called right BEFORE the first standing world entity draws onto PrimaryRT:
    //   1. Lazily creates/resizes ping-pong RGBA8 ShadowMask FBOs & internal shaders.
    //   2. Composites previous completed ping-pong shadow map onto PrimaryRT (world quad y=0.04).
    //   3. Either reuses the cached shadow map (opt-in temporalStride > 1, 0 FBO clear / 0 shadow draws,
    //      keeping ground & entity bounds strictly locked to cached prevCenterX_/Z_), OR clears this
    //      frame's write ShadowMask FBO ready to accept standing entity mirrors!
    bool OnBeforeFirstStandingEntity(const ShadowConfig& cfg,
                                     const ShadowPassState& saved,
                                     hs_gl_cache::ThreadState& glCache) noexcept {
        if (frameShadowHandled_) return ready_;
        if (!HasAnyShadowLight(cfg)) {
            fastEntityReceive1Tap_ = false;
            return false;
        }
        if (!ops_.IsComplete() || !IsInsideWorldPass(saved.dstFbo)) return false;

        const uint32_t maxTarget = cfg.maxHdRes > 0u ? std::min(cfg.maxHdRes, hdResCap_) : std::min(2048u, hdResCap_);
        const uint32_t wantRes = cfg.hdMode ? maxTarget : 512u;
        if (!EnsureResources(wantRes, saved, glCache)) {
            return false;
        }

        // Update camera basis from Lua fallback if MatrixV hasn't populated it yet
        if (!hasCamRight_ && cfg.hasLuaCam) {
            const float len = std::sqrt(cfg.luaCamRightX * cfg.luaCamRightX + cfg.luaCamRightZ * cfg.luaCamRightZ);
            if (len > 1e-4f) {
                camRightX_ = cfg.luaCamRightX / len;
                camRightZ_ = cfg.luaCamRightZ / len;
                hasCamRight_ = true;
            }
        }

        const float cx = hasCamCenter_ ? camCenterX_ : (cfg.hasLuaCam ? cfg.luaCamX : 0.0f);
        const float cz = hasCamCenter_ ? camCenterZ_ : (cfg.hasLuaCam ? cfg.luaCamZ : 0.0f);

        const bool didComposite = (hasPrevMap_ && hasPV_ && !compositedThisFrame_);
        if (CanReuseCachedShadowMap(cfg, cx, cz)) {
            if (didComposite) {
                const SavedRasterState savedRaster = CaptureRasterState(glCache, saved.glCacheTrusted, saved.blendCacheTrusted);
                CompositeGroundQuad(cfg, saved, glCache);
                compositedThisFrame_ = true;
                ++frameRecvGen_;
                RestoreRasterState(savedRaster, glCache, saved.vp, saved.glCacheTrusted,
                                   true /*restoreBlend*/, false /*restoreDepthFunc*/);
            }
            const bool wantFast1Tap = cfg.fastEntityReceive1Tap && HasEntityReceiveData(cfg);
            if (wantFast1Tap != fastEntityReceive1Tap_) {
                fastEntityReceive1Tap_ = wantFast1Tap;
                ++frameRecvGen_;
            }
            frameShadowHandled_  = true;
            beganWriteThisFrame_ = false;
            reusedThisFrame_     = true;
            ++reuseStreak_;
            ++totalShadowReuseSkips_;
            return true;
        }

        // Capture exact host raster state BEFORE any mutation (zero GL queries when glCache + blendCache are trusted;
        // falls back to glGetIntegerv whenever any glCache field is unknown or untrusted!).
        const SavedRasterState savedRaster = CaptureRasterState(glCache, saved.glCacheTrusted, saved.blendCacheTrusted);

        // 1. Composite previous frame's world-space shadow map onto PrimaryRT BEFORE standing entities!
        //    Defer raster restore until after step 3 clears the write FBO (eliminates redundant double-restore!).
        if (didComposite) {
            CompositeGroundQuad(cfg, saved, glCache);
            compositedThisFrame_ = true;
            ++frameRecvGen_;
        }
        const bool wantFast1Tap = cfg.fastEntityReceive1Tap && HasEntityReceiveData(cfg);
        if (wantFast1Tap != fastEntityReceive1Tap_) {
            fastEntityReceive1Tap_ = wantFast1Tap;
            ++frameRecvGen_;
        }

        // 2. Snap current write ortho bounds to exact shadow-texel grid (0 sub-texel crawl!)
        const float texelWorld = (2.0f * kShadowHalfExtent) / static_cast<float>(res_);
        const float safeCx = std::isfinite(cx) ? cx : 0.0f;
        const float safeCz = std::isfinite(cz) ? cz : 0.0f;
        writeCenterX_ = std::floor(safeCx / texelWorld + 0.5f) * texelWorld;
        writeCenterZ_ = std::floor(safeCz / texelWorld + 0.5f) * texelWorld;
        writeBounds_[0] = writeCenterX_;
        writeBounds_[1] = writeCenterZ_;
        writeBounds_[2] = 1.0f / kShadowHalfExtent;
        writeBounds_[3] = isNativeGl_ ? 0.0f : 1.0f; // 1.0 = ANGLE D3D9 depth-discard anti-overlap
        CaptureShadowSnapshot(cfg, res_, pendingSnapshot_);

        // 3. Clear current write ShadowMask FBO
        ops_.glBindFramebuffer(kGlFramebuffer, fbo_[writeSlot_]);
        ops_.glViewport(0, 0, static_cast<int32_t>(res_), static_cast<int32_t>(res_));
        if (!didComposite) {
            ops_.glDisable(kGlScissorTest);
            ops_.glDisable(kGlStencilTest);
        }
        ops_.glColorMask(1, 1, 1, 1);
        ops_.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        if (isNativeGl_) {
            ops_.glClear(kGlColorBufferBit);
        } else {
            ops_.glDepthMask(1);
            ops_.glClear(kGlColorBufferBit | kGlDepthBufferBit);
        }

        // Restore DST FBO, viewport, clearColor, and host raster state ONCE!
        ops_.glBindFramebuffer(kGlFramebuffer, saved.dstFbo);
        ops_.glViewport(saved.vp[0], saved.vp[1], saved.vp[2], saved.vp[3]);
        if (saved.hasClearColor) {
            ops_.glClearColor(saved.clearColor[0], saved.clearColor[1], saved.clearColor[2], saved.clearColor[3]);
        }
        RestoreRasterState(savedRaster, glCache, saved.vp, saved.glCacheTrusted,
                           didComposite /*restoreBlend*/, false /*restoreDepthFunc*/);

        frameShadowHandled_ = true;
        beganWriteThisFrame_ = true;
        reusedThisFrame_ = false;
        batchUniUploadedFrame_ = false;
        batchGpuUniUploadedFrame_ = false;
        return true;
    }

    // Mirrors a SpriteBatcher flush directly from the ALREADY-BOUND Ring VBO/IBO & GL_TEXTURE6..13!
    // Optional batchBoundsXZ[4] = {minX, maxX, minZ, maxZ} enables O(1) CPU per-light & shadow-box culling!
    void MirrorBatchedSprites(uint32_t indexCount,
                              const ShadowConfig& cfg,
                              const ShadowPassState& saved,
                              hs_gl_cache::ThreadState& glCache,
                              const float* batchBoundsXZ = nullptr,
                              uintptr_t indexByteOffset = 0u) noexcept {
        if (!ready_ || !beganWriteThisFrame_ || indexCount == 0u || !ops_.IsComplete()) return;
        if (!IsInsideWorldPass(saved.dstFbo)) return;

        bool drawDir = HasActiveDirectionalShadow(cfg);
        if (drawDir && batchBoundsXZ) {
            DirShadowCullBox cbox{};
            BuildDirShadowCullBox(cfg, cbox);
            if (!cbox.TouchesAabbXZ(batchBoundsXZ)) drawDir = false;
        }
        uint32_t activePtIdx[kMaxPointShadowLights]{};
        uint32_t activePtCnt = 0u;
        const uint32_t ptCnt = (cfg.pointLightCount < kMaxPointShadowLights)
            ? cfg.pointLightCount : kMaxPointShadowLights;
        if (locBatchPointLight_ >= 0) {
            for (uint32_t li = 0u; li < ptCnt; ++li) {
                const auto& pl = cfg.pointLights[li];
                if (!IsPointLightValid(pl)) continue;
                if (batchBoundsXZ && !PointLightTouchesAabbXZ(pl, batchBoundsXZ)) continue;
                activePtIdx[activePtCnt++] = li;
            }
        }
        if (!drawDir && activePtCnt == 0u) return;

        const SavedRasterState savedRaster = CaptureRasterState(glCache, saved.glCacheTrusted, saved.blendCacheTrusted);
        const uint32_t restoreProg = ResolveHostProgram(saved, glCache);

        ops_.glBindFramebuffer(kGlFramebuffer, fbo_[writeSlot_]);
        ops_.glViewport(0, 0, static_cast<int32_t>(res_), static_cast<int32_t>(res_));
        ApplyShadowWriteRasterState();

        ops_.glUseProgram(progBatch_);
        if (!batchUniUploadedFrame_) {
            UploadShadowBatchUniforms(locBatchBounds_, locBatchShadProj_,
                                      locBatchShadDyn_, locBatchCamRight_, cfg);
            batchUniUploadedFrame_ = true;
        }

        const void* const idxPtr = reinterpret_cast<const void*>(indexByteOffset);
        if (drawDir) {
            const float dirU[4] = { 0.0f, 0.0f, 0.0f, std::clamp(cfg.dirStrength, 0.0f, 1.0f) };
            if (locBatchPointLight_ >= 0) ops_.glUniform4fv(locBatchPointLight_, 1, dirU);
            ops_.glDrawElements(kGlTriangles, static_cast<int32_t>(indexCount), kGlUnsignedShort, idxPtr);
            ++curFrameDraws_;
            ++curFrameBatchDraws_;
            wroteAnyThisFrame_ = true;
        }
        for (uint32_t pi = 0u; pi < activePtCnt; ++pi) {
            const auto& pl = cfg.pointLights[activePtIdx[pi]];
            const float ptU[4] = { pl.wx, pl.wz, pl.radius, std::clamp(pl.strength, 0.0f, 1.0f) };
            ops_.glUniform4fv(locBatchPointLight_, 1, ptU);
            ops_.glDrawElements(kGlTriangles, static_cast<int32_t>(indexCount), kGlUnsignedShort, idxPtr);
            ++curFrameDraws_;
            ++curFrameBatchDraws_;
            wroteAnyThisFrame_ = true;
        }
        if (activePtCnt > 0u && locBatchPointLight_ >= 0) {
            const float resetU[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            ops_.glUniform4fv(locBatchPointLight_, 1, resetU);
        }

        // Restore DST program, FBO, viewport, raster state (VBO/IBO/attribs/textures untouched!)
        ops_.glUseProgram(restoreProg);
        ops_.glBindFramebuffer(kGlFramebuffer, saved.dstFbo);
        ops_.glViewport(saved.vp[0], saved.vp[1], saved.vp[2], saved.vp[3]);
        RestoreRasterState(savedRaster, glCache, saved.vp, saved.glCacheTrusted,
                           isNativeGl_ /*restoreBlend*/, !isNativeGl_ /*restoreDepthFunc*/);
        if (saved.glCacheTrusted) {
            glCache.curProgram = restoreProg;
        }
    }

    void MirrorBatchedSpritesGpu(uint32_t ssboId,
                                 uint32_t indexCount,
                                 int32_t baseVertex,
                                 uintptr_t indexByteOffset,
                                 const ShadowConfig& cfg,
                                 const ShadowPassState& saved,
                                 hs_gl_cache::ThreadState& glCache,
                                 const float* batchBoundsXZ = nullptr) noexcept {
        if (!ready_ || !beganWriteThisFrame_ || !isNativeGl_ || progBatchGpu_ == 0u ||
            indexCount == 0u || ssboId == 0u || !ops_.IsComplete() ||
            !ops_.glBindBufferBase || !ops_.glDrawElementsBaseVertex) {
            return;
        }
        if (!IsInsideWorldPass(saved.dstFbo)) return;

        bool drawDir = HasActiveDirectionalShadow(cfg);
        if (drawDir && batchBoundsXZ) {
            DirShadowCullBox cbox{};
            BuildDirShadowCullBox(cfg, cbox);
            if (!cbox.TouchesAabbXZ(batchBoundsXZ)) drawDir = false;
        }
        uint32_t activePtIdx[kMaxPointShadowLights]{};
        uint32_t activePtCnt = 0u;
        const uint32_t ptCnt = (locBatchGpuPointLight_ >= 0)
            ? ((cfg.pointLightCount < kMaxPointShadowLights) ? cfg.pointLightCount : kMaxPointShadowLights)
            : 0u;
        for (uint32_t li = 0u; li < ptCnt; ++li) {
            const auto& pl = cfg.pointLights[li];
            if (!IsPointLightValid(pl)) continue;
            if (batchBoundsXZ && !PointLightTouchesAabbXZ(pl, batchBoundsXZ)) continue;
            activePtIdx[activePtCnt++] = li;
        }
        if (!drawDir && activePtCnt == 0u) return;

        const SavedRasterState savedRaster = CaptureRasterState(glCache, saved.glCacheTrusted, saved.blendCacheTrusted);
        const uint32_t restoreProg = ResolveHostProgram(saved, glCache);

        ops_.glBindFramebuffer(kGlFramebuffer, fbo_[writeSlot_]);
        ops_.glViewport(0, 0, static_cast<int32_t>(res_), static_cast<int32_t>(res_));
        ApplyShadowWriteRasterState();

        ops_.glUseProgram(progBatchGpu_);
        ops_.glBindBufferBase(kGlShaderStorageBuffer, kGpuInstBinding, ssboId);
        if (!batchGpuUniUploadedFrame_) {
            UploadShadowBatchUniforms(locBatchGpuBounds_, locBatchGpuShadProj_,
                                      locBatchGpuShadDyn_, locBatchGpuCamRight_, cfg);
            batchGpuUniUploadedFrame_ = true;
        }

        const void* const idxPtr = reinterpret_cast<const void*>(indexByteOffset);
        if (drawDir) {
            const float dirU[4] = { 0.0f, 0.0f, 0.0f, std::clamp(cfg.dirStrength, 0.0f, 1.0f) };
            if (locBatchGpuPointLight_ >= 0) ops_.glUniform4fv(locBatchGpuPointLight_, 1, dirU);
            ops_.glDrawElementsBaseVertex(kGlTriangles, static_cast<int32_t>(indexCount),
                                          kGlUnsignedShort, idxPtr, baseVertex);
            ++curFrameDraws_;
            ++curFrameBatchDraws_;
            wroteAnyThisFrame_ = true;
        }
        for (uint32_t pi = 0u; pi < activePtCnt; ++pi) {
            const auto& pl = cfg.pointLights[activePtIdx[pi]];
            const float ptU[4] = { pl.wx, pl.wz, pl.radius, std::clamp(pl.strength, 0.0f, 1.0f) };
            ops_.glUniform4fv(locBatchGpuPointLight_, 1, ptU);
            ops_.glDrawElementsBaseVertex(kGlTriangles, static_cast<int32_t>(indexCount),
                                          kGlUnsignedShort, idxPtr, baseVertex);
            ++curFrameDraws_;
            ++curFrameBatchDraws_;
            wroteAnyThisFrame_ = true;
        }
        if (activePtCnt > 0u && locBatchGpuPointLight_ >= 0) {
            const float resetU[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            ops_.glUniform4fv(locBatchGpuPointLight_, 1, resetU);
        }

        ops_.glUseProgram(restoreProg);
        ops_.glBindFramebuffer(kGlFramebuffer, saved.dstFbo);
        ops_.glViewport(saved.vp[0], saved.vp[1], saved.vp[2], saved.vp[3]);
        RestoreRasterState(savedRaster, glCache, saved.vp, saved.glCacheTrusted,
                           true /*restoreBlend*/, false /*restoreDepthFunc*/);
        if (saved.glCacheTrusted) {
            glCache.curProgram = restoreProg;
        }
    }

    // Deferred Shadow Replay (Native GL 4.6 AZDO Persistent Ring — Zero Per-Flush FBO Ping-Pong!):
    //   - Records a lightweight ~100B descriptor for each SpriteBatcher flush whose vertices/indices
    //     live untouched inside the frame's linear-bump AZDO Persistent Ring.
    //   - Because Native GL ShadowMask uses commutative/associative GL_MAX with depth test OFF,
    //     replaying all recorded batches in ONE single FBO bind at end-of-world produces 100%
    //     bit-exact identical shadow pixels while eliminating up to 390 FBO switches/frame!
    bool RecordBatchedShadow(uint32_t vboId,
                             uint32_t iboId,
                             uint32_t indexCount,
                             uintptr_t vertByteOffset,
                             uintptr_t indexByteOffset,
                             uint32_t texCount,
                             const uint32_t* texIds,
                             const float* batchBoundsXZ,
                             uint32_t curFbo) noexcept {
        if (!ready_ || !beganWriteThisFrame_ || !isNativeGl_ || !ops_.IsComplete()) return false;
        if (indexCount == 0u || vboId == 0u || iboId == 0u || texCount == 0u || !texIds) return false;
        if (!IsInsideWorldPass(curFbo)) return false;
        if (deferredBatches_.size() >= kMaxDeferredBatches) return false;
        if (deferredBatches_.capacity() < 128u) {
            deferredBatches_.reserve(128u);
        }
        DeferredShadowBatch b{};
        b.vboId           = vboId;
        b.iboId           = iboId;
        b.indexCount      = indexCount;
        b.vertByteOffset  = vertByteOffset;
        b.indexByteOffset = indexByteOffset;
        b.texCount        = (texCount <= kMaxBatchTexSlots) ? texCount : kMaxBatchTexSlots;
        std::memcpy(b.texIds, texIds, static_cast<size_t>(b.texCount) * sizeof(uint32_t));
        if (batchBoundsXZ) {
            std::memcpy(b.boundsXZ, batchBoundsXZ, sizeof(b.boundsXZ));
            b.hasBounds = true;
        }
        deferredBatches_.push_back(b);
        return true;
    }

    bool RecordBatchedShadowGpu(uint32_t vboId,
                                uint32_t iboId,
                                uint32_t ssboId,
                                uint32_t indexCount,
                                int32_t baseVertex,
                                uintptr_t indexByteOffset,
                                const float* batchBoundsXZ,
                                uint32_t curFbo) noexcept {
        if (!ready_ || !beganWriteThisFrame_ || !isNativeGl_ || progBatchGpu_ == 0u ||
            !ops_.IsComplete() || !ops_.glBindBufferBase || !ops_.glDrawElementsBaseVertex) {
            return false;
        }
        if (indexCount == 0u || vboId == 0u || iboId == 0u || ssboId == 0u) return false;
        if (!IsInsideWorldPass(curFbo)) return false;
        if (deferredBatches_.size() >= kMaxDeferredBatches) return false;
        if (deferredBatches_.capacity() < 128u) {
            deferredBatches_.reserve(128u);
        }
        DeferredShadowBatch b{};
        b.vboId           = vboId;
        b.iboId           = iboId;
        b.ssboId          = ssboId;
        b.indexCount      = indexCount;
        b.baseVertex      = baseVertex;
        b.indexByteOffset = indexByteOffset;
        b.isGpuDriven     = true;
        if (batchBoundsXZ) {
            std::memcpy(b.boundsXZ, batchBoundsXZ, sizeof(b.boundsXZ));
            b.hasBounds = true;
        }
        deferredBatches_.push_back(b);
        return true;
    }

    void FlushDeferredBatchedShadows(const ShadowConfig& cfg,
                                     const ShadowPassState& saved,
                                     hs_gl_cache::ThreadState& glCache) noexcept {
        if (deferredBatches_.empty()) return;
        if (!ready_ || !beganWriteThisFrame_ || !isNativeGl_ || !ops_.IsComplete()) {
            deferredBatches_.clear();
            return;
        }

        const bool drawDir = HasActiveDirectionalShadow(cfg);
        DirShadowCullBox dirCullBox{};
        if (drawDir) {
            BuildDirShadowCullBox(cfg, dirCullBox);
        }
        const uint32_t ptCnt = (locBatchPointLight_ >= 0 || locBatchGpuPointLight_ >= 0)
            ? ((cfg.pointLightCount < kMaxPointShadowLights) ? cfg.pointLightCount : kMaxPointShadowLights)
            : 0u;
        bool ptValid[kMaxPointShadowLights]{};
        bool anyPtValid = false;
        for (uint32_t li = 0u; li < ptCnt; ++li) {
            ptValid[li] = IsPointLightValid(cfg.pointLights[li]);
            if (ptValid[li]) anyPtValid = true;
        }
        if (!drawDir && !anyPtValid) {
            deferredBatches_.clear();
            return;
        }

        // Pre-pass: compute per-batch active light bitmask (bit 0 = directional, bits 1..3 = point lights).
        // Because Native GL ShadowMask uses commutative/associative GL_MAX with depth test OFF, grouping by
        // light pass and coalescing contiguous IBO / same-buffer GPU batches is 100% bit-exact identical!
        const size_t n = std::min(deferredBatches_.size(), kMaxDeferredBatches);
        if (batchLightMask_.size() < n) {
            batchLightMask_.resize(n);
            batchIsGpu_.resize(n);
            mdCounts_.resize(n);
            mdPtrs_.resize(n);
            mdBaseVerts_.resize(n);
        }
        std::memset(batchLightMask_.data(), 0, n);
        std::memset(batchIsGpu_.data(), 0, n);
        uint8_t* const batchLightMask = batchLightMask_.data();
        uint8_t* const batchIsGpu     = batchIsGpu_.data();
        uint8_t anyPassMask = 0u;
        for (size_t i = 0u; i < n; ++i) {
            const auto& b = deferredBatches_[i];
            const bool wantGpu = b.isGpuDriven && (progBatchGpu_ != 0u) && (b.ssboId != 0u) &&
                                 (ops_.glBindBufferBase != nullptr) && (ops_.glDrawElementsBaseVertex != nullptr);
            if (b.isGpuDriven && !wantGpu) continue;
            batchIsGpu[i] = wantGpu ? 1u : 0u;

            uint8_t mask = 0u;
            if (drawDir && (!b.hasBounds || dirCullBox.TouchesAabbXZ(b.boundsXZ))) {
                mask |= 1u;
            }
            const int32_t ptLoc = wantGpu ? locBatchGpuPointLight_ : locBatchPointLight_;
            if (anyPtValid && ptLoc >= 0) {
                for (uint32_t li = 0u; li < ptCnt; ++li) {
                    if (!ptValid[li]) continue;
                    if (b.hasBounds && !PointLightTouchesAabbXZ(cfg.pointLights[li], b.boundsXZ)) continue;
                    mask |= static_cast<uint8_t>(1u << (li + 1u));
                }
            }
            if (mask != 0u) {
                batchLightMask[i] = mask;
                anyPassMask |= mask;
                ++curFrameDeferredBatches_;
            }
        }

        // Lazy FBO/state enter: if every deferred batch is culled by AABB bounds, emit ZERO GL calls!
        if (anyPassMask == 0u) {
            deferredBatches_.clear();
            return;
        }

        CaptureShadowSnapshot(cfg, res_, pendingSnapshot_);
        const SavedRasterState savedRaster = CaptureRasterState(glCache, saved.glCacheTrusted, saved.blendCacheTrusted);
        const uint32_t         restoreProg = ResolveHostProgram(saved, glCache);
        ops_.glBindFramebuffer(kGlFramebuffer, fbo_[writeSlot_]);
        ops_.glViewport(0, 0, static_cast<int32_t>(res_), static_cast<int32_t>(res_));
        ApplyShadowWriteRasterState();
        ops_.glEnableVertexAttribArray(0u);
        ops_.glEnableVertexAttribArray(7u);

        bool     touchedActiveTex  = false;
        uint32_t progMode          = 0u; // 1 = CPU, 2 = GPU
        bool     cpuAttribsEnabled = false;
        uint32_t boundVbo          = 0xFFFFFFFFu;
        uint32_t boundIbo          = 0xFFFFFFFFu;
        uint32_t boundSsbo         = 0xFFFFFFFFu;
        uint32_t gpuAttribVbo      = 0xFFFFFFFFu;
        uint32_t cpuAttribVbo      = 0xFFFFFFFFu;
        uintptr_t cpuAttribOff     = 0u;
        uint32_t boundSlotTex[kMaxBatchTexSlots];
        for (uint32_t s = 0u; s < kMaxBatchTexSlots; ++s) boundSlotTex[s] = 0xFFFFFFFFu;
        int32_t  lastLightKey      = -2;
        bool     usedCpuPt         = false;
        bool     usedGpuPt         = false;
        const float dirStrengthClamped = std::clamp(cfg.dirStrength, 0.0f, 1.0f);

        int32_t*     const mdCounts    = mdCounts_.data();
        const void** const mdPtrs      = mdPtrs_.data();
        int32_t*     const mdBaseVerts = mdBaseVerts_.data();

        for (uint32_t pass = 0u; pass <= ptCnt; ++pass) {
            const uint8_t passBit = static_cast<uint8_t>(1u << pass);
            if ((anyPassMask & passBit) == 0u) continue;

            const int32_t curLightKey = (pass == 0u) ? -1 : static_cast<int32_t>(pass - 1u);
            float lightU[4];
            if (pass == 0u) {
                lightU[0] = 0.0f; lightU[1] = 0.0f; lightU[2] = 0.0f; lightU[3] = dirStrengthClamped;
            } else {
                const auto& pl = cfg.pointLights[pass - 1u];
                lightU[0] = pl.wx; lightU[1] = pl.wz; lightU[2] = pl.radius; lightU[3] = std::clamp(pl.strength, 0.0f, 1.0f);
            }

            size_t i = 0u;
            while (i < n) {
                if ((batchLightMask[i] & passBit) == 0u) {
                    ++i;
                    continue;
                }
                const auto& b0 = deferredBatches_[i];
                const bool wantGpu = batchIsGpu[i];

                if (wantGpu) {
                    if (progMode != 2u) {
                        if (cpuAttribsEnabled) {
                            ops_.glDisableVertexAttribArray(5u);
                            ops_.glDisableVertexAttribArray(6u);
                            cpuAttribsEnabled = false;
                        }
                        ops_.glUseProgram(progBatchGpu_);
                        if (!batchGpuUniUploadedFrame_) {
                            UploadShadowBatchUniforms(locBatchGpuBounds_, locBatchGpuShadProj_,
                                                      locBatchGpuShadDyn_, locBatchGpuCamRight_, cfg);
                            batchGpuUniUploadedFrame_ = true;
                        }
                        progMode     = 2u;
                        lastLightKey = -2;
                        gpuAttribVbo = 0xFFFFFFFFu;
                        cpuAttribVbo = 0xFFFFFFFFu;
                    }
                    if (boundSsbo != b0.ssboId) {
                        ops_.glBindBufferBase(kGlShaderStorageBuffer, kGpuInstBinding, b0.ssboId);
                        boundSsbo = b0.ssboId;
                    }
                    if (boundVbo != b0.vboId) {
                        ops_.glBindBuffer(kGlArrayBuffer, b0.vboId);
                        boundVbo = b0.vboId;
                    }
                    if (boundIbo != b0.iboId) {
                        ops_.glBindBuffer(kGlElementArrayBuffer, b0.iboId);
                        boundIbo = b0.iboId;
                    }
                    if (gpuAttribVbo != b0.vboId) {
                        ops_.glVertexAttribPointer(0u, 4, kGlFloat, 0, 20, reinterpret_cast<const void*>(0u));
                        ops_.glVertexAttribPointer(7u, 1, kGlFloat, 0, 20, reinterpret_cast<const void*>(16u));
                        gpuAttribVbo = b0.vboId;
                    }
                    if (locBatchGpuPointLight_ >= 0 && lastLightKey != curLightKey) {
                        ops_.glUniform4fv(locBatchGpuPointLight_, 1, lightU);
                        lastLightKey = curLightKey;
                    }

                    // Coalesce consecutive active GPU batches sharing (vbo, ibo, ssbo):
                    //   Level 1: merge contiguous index ranges with identical baseVertex into one draw.
                    //   Level 2: when glMultiDrawElementsBaseVertex is available, issue ALL distinct
                    //            baseVertex ranges in ONE single driver call (363 batches -> 1 draw)!
                    uint32_t  subCnt       = 0u;
                    uint32_t  mergedInRun  = 0u;
                    uintptr_t lastEndOff   = 0u;
                    int32_t   lastBaseVert = 0;
                    size_t j = i;
                    while (j < n) {
                        if ((batchLightMask[j] & passBit) == 0u) {
                            ++j;
                            continue;
                        }
                        if (!batchIsGpu[j]) break;
                        const auto& bj = deferredBatches_[j];
                        if (bj.vboId != b0.vboId || bj.iboId != b0.iboId || bj.ssboId != b0.ssboId) break;

                        if (subCnt > 0u &&
                            bj.baseVertex == lastBaseVert &&
                            bj.indexByteOffset == lastEndOff &&
                            static_cast<uint64_t>(mdCounts[subCnt - 1u]) + static_cast<uint64_t>(bj.indexCount) <= 0x3FFFFFFFULL) {
                            mdCounts[subCnt - 1u] += static_cast<int32_t>(bj.indexCount);
                            lastEndOff += static_cast<uintptr_t>(bj.indexCount) * sizeof(uint16_t);
                            ++mergedInRun;
                        } else {
                            mdCounts[subCnt]    = static_cast<int32_t>(bj.indexCount);
                            mdPtrs[subCnt]      = reinterpret_cast<const void*>(bj.indexByteOffset);
                            mdBaseVerts[subCnt] = bj.baseVertex;
                            lastBaseVert        = bj.baseVertex;
                            lastEndOff          = bj.indexByteOffset + static_cast<uintptr_t>(bj.indexCount) * sizeof(uint16_t);
                            ++subCnt;
                        }
                        ++j;
                    }
                    i = j;

                    if (subCnt == 1u) {
                        ops_.glDrawElementsBaseVertex(kGlTriangles, mdCounts[0], kGlUnsignedShort, mdPtrs[0], mdBaseVerts[0]);
                        ++curFrameDraws_;
                        ++curFrameBatchDraws_;
                        curFrameCoalescedDraws_ += mergedInRun;
                    } else if (ops_.glMultiDrawElementsBaseVertex != nullptr) {
                        ops_.glMultiDrawElementsBaseVertex(kGlTriangles, mdCounts, kGlUnsignedShort, mdPtrs,
                                                           static_cast<int32_t>(subCnt), mdBaseVerts);
                        ++curFrameDraws_;
                        ++curFrameBatchDraws_;
                        curFrameCoalescedDraws_ += mergedInRun + (subCnt - 1u);
                    } else {
                        for (uint32_t k = 0u; k < subCnt; ++k) {
                            ops_.glDrawElementsBaseVertex(kGlTriangles, mdCounts[k], kGlUnsignedShort, mdPtrs[k], mdBaseVerts[k]);
                            ++curFrameDraws_;
                            ++curFrameBatchDraws_;
                        }
                        curFrameCoalescedDraws_ += mergedInRun;
                    }
                    wroteAnyThisFrame_ = true;
                    if (pass > 0u) usedGpuPt = true;
                } else {
                    if (progMode != 1u) {
                        ops_.glUseProgram(progBatch_);
                        if (!batchUniUploadedFrame_) {
                            UploadShadowBatchUniforms(locBatchBounds_, locBatchShadProj_,
                                                      locBatchShadDyn_, locBatchCamRight_, cfg);
                            batchUniUploadedFrame_ = true;
                        }
                        if (!cpuAttribsEnabled) {
                            ops_.glEnableVertexAttribArray(5u);
                            ops_.glEnableVertexAttribArray(6u);
                            cpuAttribsEnabled = true;
                        }
                        progMode     = 1u;
                        lastLightKey = -2;
                        gpuAttribVbo = 0xFFFFFFFFu;
                        cpuAttribVbo = 0xFFFFFFFFu;
                    }
                    if (boundVbo != b0.vboId) {
                        ops_.glBindBuffer(kGlArrayBuffer, b0.vboId);
                        boundVbo = b0.vboId;
                    }
                    if (boundIbo != b0.iboId) {
                        ops_.glBindBuffer(kGlElementArrayBuffer, b0.iboId);
                        boundIbo = b0.iboId;
                    }
                    if (cpuAttribVbo != b0.vboId || cpuAttribOff != b0.vertByteOffset) {
                        constexpr int32_t kStride = 64;
                        ops_.glVertexAttribPointer(0u, 4, kGlFloat, 0, kStride, reinterpret_cast<const void*>(b0.vertByteOffset + 0u));
                        ops_.glVertexAttribPointer(7u, 4, kGlFloat, 0, kStride, reinterpret_cast<const void*>(b0.vertByteOffset + 16u));
                        ops_.glVertexAttribPointer(5u, 4, kGlFloat, 0, kStride, reinterpret_cast<const void*>(b0.vertByteOffset + 32u));
                        ops_.glVertexAttribPointer(6u, 4, kGlFloat, 0, kStride, reinterpret_cast<const void*>(b0.vertByteOffset + 48u));
                        cpuAttribVbo = b0.vboId;
                        cpuAttribOff = b0.vertByteOffset;
                    }
                    for (uint32_t s = 0u; s < b0.texCount; ++s) {
                        if (boundSlotTex[s] != b0.texIds[s]) {
                            ops_.glActiveTexture(kGlTexture0 + BatchSlotToTexUnit(s));
                            ops_.glBindTexture(kGlTexture2D, b0.texIds[s]);
                            boundSlotTex[s] = b0.texIds[s];
                            touchedActiveTex = true;
                        }
                    }
                    if (locBatchPointLight_ >= 0 && lastLightKey != curLightKey) {
                        ops_.glUniform4fv(locBatchPointLight_, 1, lightU);
                        lastLightKey = curLightKey;
                    }

                    // Coalesce consecutive contiguous CPU shadow batches sharing identical VBO/IBO/offset/textures
                    uint32_t  accCount    = b0.indexCount;
                    uintptr_t endByteOff  = b0.indexByteOffset + static_cast<uintptr_t>(b0.indexCount) * sizeof(uint16_t);
                    uint32_t  mergedInRun = 0u;
                    size_t j = i + 1u;
                    while (j < n) {
                        if ((batchLightMask[j] & passBit) == 0u) {
                            ++j;
                            continue;
                        }
                        if (batchIsGpu[j]) break;
                        const auto& bj = deferredBatches_[j];
                        if (bj.vboId != b0.vboId || bj.iboId != b0.iboId ||
                            bj.vertByteOffset != b0.vertByteOffset || bj.texCount != b0.texCount ||
                            bj.indexByteOffset != endByteOff ||
                            static_cast<uint64_t>(accCount) + static_cast<uint64_t>(bj.indexCount) > 0x3FFFFFFFULL) {
                            break;
                        }
                        bool sameTex = true;
                        for (uint32_t s = 0u; s < b0.texCount; ++s) {
                            if (bj.texIds[s] != b0.texIds[s]) { sameTex = false; break; }
                        }
                        if (!sameTex) break;
                        accCount   += bj.indexCount;
                        endByteOff += static_cast<uintptr_t>(bj.indexCount) * sizeof(uint16_t);
                        ++mergedInRun;
                        ++j;
                    }
                    i = j;

                    ops_.glDrawElements(kGlTriangles, static_cast<int32_t>(accCount), kGlUnsignedShort,
                                        reinterpret_cast<const void*>(b0.indexByteOffset));
                    ++curFrameDraws_;
                    ++curFrameBatchDraws_;
                    curFrameCoalescedDraws_ += mergedInRun;
                    wroteAnyThisFrame_ = true;
                    if (pass > 0u) usedCpuPt = true;
                }
            }
        }

        const float resetU[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        if (usedCpuPt && locBatchPointLight_ >= 0) {
            if (progMode != 1u) ops_.glUseProgram(progBatch_);
            ops_.glUniform4fv(locBatchPointLight_, 1, resetU);
        }
        if (usedGpuPt && locBatchGpuPointLight_ >= 0 && progBatchGpu_ != 0u) {
            ops_.glUseProgram(progBatchGpu_);
            ops_.glUniform4fv(locBatchGpuPointLight_, 1, resetU);
        }

        // Restore host attribs, VBO/IBO, texture unit, program, FBO, viewport & raster state ONCE!
        if (cpuAttribsEnabled) {
            ops_.glDisableVertexAttribArray(5u);
            ops_.glDisableVertexAttribArray(6u);
        }
        ops_.glDisableVertexAttribArray(7u);
        if (!saved.attr0.enabled) {
            ops_.glDisableVertexAttribArray(0u);
        }
        ops_.glBindBuffer(kGlArrayBuffer, saved.boundArrayBuf);
        if (saved.attr0.valid) {
            if (saved.attr0.buffer != saved.boundArrayBuf) {
                ops_.glBindBuffer(kGlArrayBuffer, saved.attr0.buffer);
                ops_.glVertexAttribPointer(0u, saved.attr0.size, saved.attr0.type,
                                           saved.attr0.normalized, saved.attr0.stride, saved.attr0.pointer);
                ops_.glBindBuffer(kGlArrayBuffer, saved.boundArrayBuf);
            } else {
                ops_.glVertexAttribPointer(0u, saved.attr0.size, saved.attr0.type,
                                           saved.attr0.normalized, saved.attr0.stride, saved.attr0.pointer);
            }
        }
        ops_.glBindBuffer(kGlElementArrayBuffer, saved.boundElemBuf);
        if (touchedActiveTex) {
            ops_.glActiveTexture(kGlTexture0 + saved.activeUnit);
        }
        ops_.glUseProgram(restoreProg);
        ops_.glBindFramebuffer(kGlFramebuffer, saved.dstFbo);
        ops_.glViewport(saved.vp[0], saved.vp[1], saved.vp[2], saved.vp[3]);
        RestoreRasterState(savedRaster, glCache, saved.vp, saved.glCacheTrusted,
                           true /*restoreBlend*/, false /*restoreDepthFunc*/);

        for (uint32_t s = 0u; s < kMaxBatchTexSlots; ++s) {
            const uint32_t u = BatchSlotToTexUnit(s);
            if (u < hs_gl_cache::kMaxTexUnits && boundSlotTex[s] != 0xFFFFFFFFu) {
                glCache.boundTex2D[u] = hs_gl_cache::kInvalidId;
            }
        }
        auto syncAttr = [&](uint32_t loc, bool en) {
            if (loc < hs_gl_cache::kMaxVertexAttribs) {
                const uint16_t bit = static_cast<uint16_t>(1u << loc);
                glCache.attribKnownMask |= bit;
                if (en) glCache.attribEnabledMask |= bit;
                else    glCache.attribEnabledMask &= static_cast<uint16_t>(~bit);
            }
        };
        syncAttr(0u, saved.attr0.enabled);
        syncAttr(5u, false);
        syncAttr(6u, false);
        syncAttr(7u, false);
        if (saved.glCacheTrusted) {
            glCache.curProgram      = restoreProg;
            if (touchedActiveTex) {
                glCache.activeUnit  = static_cast<int32_t>(saved.activeUnit);
            }
            glCache.boundArrayBuf   = saved.boundArrayBuf;
            glCache.boundElementBuf = saved.boundElemBuf;
        } else if (touchedActiveTex) {
            glCache.activeUnit = -1;
        }

        deferredBatches_.clear();
    }

    // Mirrors an unbatched standing draw (anim_skinned.ksh trees/grass/mobs OR unbatched anim*.ksh)
    // using the ALREADY-BOUND program, VBO/IBO, fastanim_bones[64], and textures!
    template <typename DrawFn>
    void MirrorCurrentProgramDraw(int32_t locShadowPass,
                                  int32_t locShadowBounds,
                                  int32_t locShadowCamR,
                                  const ShadowConfig& cfg,
                                  const ShadowPassState& saved,
                                  hs_gl_cache::ThreadState& glCache,
                                  DrawFn&& issueDraw,
                                  int32_t locShadowPt = -1,
                                  bool hasEntityPos = false,
                                  float entityWx = 0.0f,
                                  float entityWz = 0.0f) noexcept {
        if (!ready_ || !beganWriteThisFrame_ || locShadowPass < 0 || !ops_.IsComplete()) return;
        if (!IsInsideWorldPass(saved.dstFbo)) return;

        const bool drawDir = HasActiveDirectionalShadow(cfg);
        uint32_t activePtIdx[kMaxPointShadowLights]{};
        uint32_t activePtCnt = 0u;
        const uint32_t ptCnt = (cfg.pointLightCount < kMaxPointShadowLights)
            ? cfg.pointLightCount : kMaxPointShadowLights;
        if (locShadowPt >= 0) {
            for (uint32_t li = 0u; li < ptCnt; ++li) {
                const auto& pl = cfg.pointLights[li];
                if (!IsPointLightValid(pl)) continue;
                if (hasEntityPos && !PointLightTouchesEntityXZ(pl, entityWx, entityWz)) continue;
                activePtIdx[activePtCnt++] = li;
            }
        }
        if (!drawDir && activePtCnt == 0u) return;

        const SavedRasterState savedRaster = CaptureRasterState(glCache, saved.glCacheTrusted, saved.blendCacheTrusted);

        ops_.glBindFramebuffer(kGlFramebuffer, fbo_[writeSlot_]);
        ops_.glViewport(0, 0, static_cast<int32_t>(res_), static_cast<int32_t>(res_));
        ApplyShadowWriteRasterState();

        const float ptScale = std::isfinite(cfg.pointStretchScale)
            ? std::clamp(cfg.pointStretchScale, 0.50f, 1.80f) : 1.0f;
        const float camR[4] = { camRightX_, camRightZ_, cfg.skipNightmare ? 1.0f : 0.0f, ptScale };
        if (locShadowBounds >= 0) ops_.glUniform4fv(locShadowBounds, 1, writeBounds_);
        if (locShadowCamR >= 0)   ops_.glUniform4fv(locShadowCamR, 1, camR);
        ops_.glUniform1f(locShadowPass, 1.0f);

        if (drawDir) {
            if (locShadowPt >= 0) {
                const float dirU[4] = { 0.0f, 0.0f, 0.0f, std::clamp(cfg.dirStrength, 0.0f, 1.0f) };
                ops_.glUniform4fv(locShadowPt, 1, dirU);
            }
            issueDraw();
            ++curFrameDraws_;
            ++curFrameUnbatchedDraws_;
            wroteAnyThisFrame_ = true;
        }
        for (uint32_t pi = 0u; pi < activePtCnt; ++pi) {
            const auto& pl = cfg.pointLights[activePtIdx[pi]];
            const float ptU[4] = { pl.wx, pl.wz, pl.radius, std::clamp(pl.strength, 0.0f, 1.0f) };
            ops_.glUniform4fv(locShadowPt, 1, ptU);
            issueDraw();
            ++curFrameDraws_;
            ++curFrameUnbatchedDraws_;
            wroteAnyThisFrame_ = true;
        }
        if (locShadowPt >= 0) {
            const float resetU[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            ops_.glUniform4fv(locShadowPt, 1, resetU);
        }
        ops_.glUniform1f(locShadowPass, 0.0f);
        if (saved.glCacheTrusted) {
            const uint32_t prog = saved.dstProg;
            glCache.InvalidateUniform(prog, locShadowPass);
            if (locShadowBounds >= 0) glCache.InvalidateUniform(prog, locShadowBounds);
            if (locShadowCamR >= 0)   glCache.InvalidateUniform(prog, locShadowCamR);
            if (locShadowPt >= 0)     glCache.InvalidateUniform(prog, locShadowPt);
        }

        ops_.glBindFramebuffer(kGlFramebuffer, saved.dstFbo);
        ops_.glViewport(saved.vp[0], saved.vp[1], saved.vp[2], saved.vp[3]);
        RestoreRasterState(savedRaster, glCache, saved.vp, saved.glCacheTrusted,
                           isNativeGl_ /*restoreBlend*/, !isNativeGl_ /*restoreDepthFunc*/);
    }

    void OnEndFrame() noexcept {
        if (sawGroundThisFrame_) {
            if (beganWriteThisFrame_ && wroteAnyThisFrame_) {
                prevCenterX_ = writeCenterX_;
                prevCenterZ_ = writeCenterZ_;
                readSlot_ = writeSlot_;
                writeSlot_ ^= 1u;
                hasPrevMap_ = true;
                cachedSnapshot_ = pendingSnapshot_;
                hasCachedSnapshot_ = true;
                reuseStreak_ = 0u;
                lastFrameReused_ = false;
            } else if (reusedThisFrame_ && hasPrevMap_) {
                // Keep completed shadow map in readSlot_ & prevCenterX_/Z_ strictly unchanged!
                lastFrameReused_ = true;
            } else {
                hasPrevMap_ = false;
                hasCachedSnapshot_ = false;
                reuseStreak_ = 0u;
                lastFrameReused_ = false;
            }
            lastFrameDraws_           = curFrameDraws_;
            lastFrameBatchDraws_      = curFrameBatchDraws_;
            lastFrameUnbatchedDraws_  = curFrameUnbatchedDraws_;
            lastFrameDeferredBatches_ = curFrameDeferredBatches_;
            lastFrameCoalescedDraws_  = curFrameCoalescedDraws_;
        }
        const bool hadShadowActivity = beganWriteThisFrame_ || compositedThisFrame_ || reusedThisFrame_ || hasPrevMap_;
        ResetFrameFlags();
        if (hadShadowActivity) {
            ++frameRecvGen_;
        }
    }

private:
    struct ShadowRenderSnapshot {
        uint32_t         res               = 0u;
        float            halfExtent        = kShadowHalfExtent;
        bool             hasDir            = false;
        float            sunX              = 0.0f;
        float            sunZ              = 0.0f;
        float            stretch           = 0.0f;
        float            dirStrength       = 0.0f;
        float            heightFade        = 0.0f;
        float            minHeight         = 0.0f;
        float            pointStretchScale = 1.0f;
        float            windTime          = 0.0f;
        bool             skipNightmare     = true;
        uint32_t         ptCount           = 0u;
        PointShadowLight pts[kMaxPointShadowLights]{};
    };

    static void CaptureShadowSnapshot(const ShadowConfig& cfg, uint32_t res, ShadowRenderSnapshot& out) noexcept {
        out = ShadowRenderSnapshot{};
        out.res        = res;
        out.halfExtent = kShadowHalfExtent;
        out.hasDir     = HasActiveDirectionalShadow(cfg);
        if (out.hasDir) {
            const float lenSq = cfg.dirWx * cfg.dirWx + cfg.dirWz * cfg.dirWz;
            if (std::isfinite(lenSq) && lenSq > 1e-8f) {
                const float invL = 1.0f / std::sqrt(lenSq);
                out.sunX = cfg.dirWx * invL;
                out.sunZ = cfg.dirWz * invL;
            }
            out.stretch     = std::isfinite(cfg.stretch) ? cfg.stretch : 1.35f;
            out.dirStrength = std::clamp(cfg.dirStrength, 0.0f, 1.0f);
        }
        out.heightFade        = std::isfinite(cfg.heightFade) ? cfg.heightFade : 0.55f;
        out.minHeight         = std::isfinite(cfg.minHeight) ? cfg.minHeight : 0.0f;
        out.pointStretchScale = std::isfinite(cfg.pointStretchScale)
            ? std::clamp(cfg.pointStretchScale, 0.50f, 1.80f) : 1.0f;
        out.windTime          = std::isfinite(cfg.windTime) ? cfg.windTime : 0.0f;
        out.skipNightmare     = cfg.skipNightmare;
        const uint32_t cnt = (cfg.pointLightCount < kMaxPointShadowLights)
            ? cfg.pointLightCount : kMaxPointShadowLights;
        for (uint32_t i = 0u; i < cnt; ++i) {
            if (IsPointLightValid(cfg.pointLights[i])) {
                out.pts[out.ptCount++] = cfg.pointLights[i];
            }
        }
    }

    bool CanReuseCachedShadowMap(const ShadowConfig& cfg, float camX, float camZ) const noexcept {
        const uint32_t stride = (cfg.temporalStride <= 1u) ? 1u : std::min(cfg.temporalStride, 8u);
        if (stride <= 1u) return false;
        if (!ready_ || !hasPrevMap_ || !hasCachedSnapshot_) return false;
        if (reuseStreak_ >= (stride - 1u)) return false;
        if (!std::isfinite(camX) || !std::isfinite(camZ)) return false;

        // 1. Resolution & world-space half-extent must match exactly
        if (res_ != cachedSnapshot_.res || kShadowHalfExtent != cachedSnapshot_.halfExtent) return false;

        // 2. Camera center drift relative to the CACHED shadow map origin must stay strictly inside safety margin
        //    (< 1.2 DST tiles = 4.8 world units, leaving > 7+ tiles of shadow-box border; teleport > 1.2 tiles refreshes immediately!)
        const float dcam = std::fabs(camX - prevCenterX_) + std::fabs(camZ - prevCenterZ_);
        if (dcam >= (1.2f * kWorldUnitsPerTile)) return false;

        // 3. Directional sun vector & shadow projection parameters must match closely
        const bool curHasDir = HasActiveDirectionalShadow(cfg);
        if (curHasDir != cachedSnapshot_.hasDir) return false;
        if (curHasDir) {
            const float lenSq = cfg.dirWx * cfg.dirWx + cfg.dirWz * cfg.dirWz;
            if (!std::isfinite(lenSq) || lenSq <= 1e-8f) return false;
            const float invL = 1.0f / std::sqrt(lenSq);
            const float sunX = cfg.dirWx * invL;
            const float sunZ = cfg.dirWz * invL;
            if (std::fabs(sunX - cachedSnapshot_.sunX) + std::fabs(sunZ - cachedSnapshot_.sunZ) >= 0.004f) {
                return false;
            }
            const float stretch = std::isfinite(cfg.stretch) ? cfg.stretch : 1.35f;
            if (std::fabs(stretch - cachedSnapshot_.stretch) >= 0.015f) return false;
            const float dirStr = std::clamp(cfg.dirStrength, 0.0f, 1.0f);
            if (std::fabs(dirStr - cachedSnapshot_.dirStrength) >= 0.04f) return false;
        }

        // 4. Filtering/height/wind/nightmare caster invariants
        const float heightFade = std::isfinite(cfg.heightFade) ? cfg.heightFade : 0.55f;
        const float minHeight  = std::isfinite(cfg.minHeight) ? cfg.minHeight : 0.0f;
        const float ptScale    = std::isfinite(cfg.pointStretchScale)
            ? std::clamp(cfg.pointStretchScale, 0.50f, 1.80f) : 1.0f;
        if (std::fabs(heightFade - cachedSnapshot_.heightFade) >= 0.02f) return false;
        if (std::fabs(minHeight - cachedSnapshot_.minHeight) >= 0.01f) return false;
        if (cfg.skipNightmare != cachedSnapshot_.skipNightmare) return false;
        if (std::fabs(ptScale - cachedSnapshot_.pointStretchScale) >= 0.02f) return false;
        if (std::isfinite(cfg.swayAmp) && cfg.swayAmp > 0.01f) {
            if (!std::isfinite(cfg.windTime) || std::fabs(cfg.windTime - cachedSnapshot_.windTime) > 0.25f) {
                return false;
            }
        }

        // 5. Active point-light count & positions must stay within 0.15 tiles (0.60 world units)
        PointShadowLight curPts[kMaxPointShadowLights]{};
        uint32_t curPtCount = 0u;
        const uint32_t cnt = (cfg.pointLightCount < kMaxPointShadowLights)
            ? cfg.pointLightCount : kMaxPointShadowLights;
        for (uint32_t i = 0u; i < cnt; ++i) {
            if (IsPointLightValid(cfg.pointLights[i])) {
                curPts[curPtCount++] = cfg.pointLights[i];
            }
        }
        if (curPtCount != cachedSnapshot_.ptCount) return false;
        for (uint32_t i = 0u; i < curPtCount; ++i) {
            const float dpos = std::fabs(curPts[i].wx - cachedSnapshot_.pts[i].wx) +
                               std::fabs(curPts[i].wz - cachedSnapshot_.pts[i].wz);
            if (dpos >= (0.15f * kWorldUnitsPerTile)) return false;
            if (std::fabs(curPts[i].radius - cachedSnapshot_.pts[i].radius) >= 0.50f) return false;
            if (std::fabs(curPts[i].strength - cachedSnapshot_.pts[i].strength) >= 0.04f) return false;
        }
        return true;
    }

    struct DeferredShadowBatch {
        uint32_t  vboId           = 0;
        uint32_t  iboId           = 0;
        uint32_t  ssboId          = 0;
        uint32_t  indexCount      = 0;
        int32_t   baseVertex      = 0;
        uintptr_t vertByteOffset  = 0;
        uintptr_t indexByteOffset = 0;
        uint32_t  texCount        = 0;
        uint32_t  texIds[kMaxBatchTexSlots] = {};
        float     boundsXZ[4]     = {};
        bool      hasBounds       = false;
        bool      isGpuDriven     = false;
    };

    void UploadShadowBatchUniforms(int32_t locB, int32_t locP, int32_t locD, int32_t locR,
                                   const ShadowConfig& cfg) noexcept {
        const float proj[4] = { cfg.dirWx, cfg.dirWz, cfg.stretch, cfg.projOn };
        const float dyn[4]  = { cfg.windTime, cfg.swayAmp, cfg.heightFade, cfg.penumbraBias };
        const float ptScale = std::isfinite(cfg.pointStretchScale)
            ? std::clamp(cfg.pointStretchScale, 0.50f, 1.80f) : 1.0f;
        const float camR[4] = { camRightX_, camRightZ_, cfg.skipNightmare ? 1.0f : 0.0f, ptScale };
        if (locB >= 0) ops_.glUniform4fv(locB, 1, writeBounds_);
        if (locP >= 0) ops_.glUniform4fv(locP, 1, proj);
        if (locD >= 0) ops_.glUniform4fv(locD, 1, dyn);
        if (locR >= 0) ops_.glUniform4fv(locR, 1, camR);
    }

    void ResetFrameFlags() noexcept {
        sawGroundThisFrame_       = false;
        compositedThisFrame_      = false;
        frameShadowHandled_       = false;
        beganWriteThisFrame_      = false;
        reusedThisFrame_          = false;
        wroteAnyThisFrame_        = false;
        batchUniUploadedFrame_    = false;
        batchGpuUniUploadedFrame_ = false;
        curFrameDraws_            = 0;
        curFrameBatchDraws_       = 0;
        curFrameUnbatchedDraws_   = 0;
        curFrameDeferredBatches_  = 0;
        curFrameCoalescedDraws_   = 0;
        deferredBatches_.clear();
    }

    void ExtractCameraBasisFromMatrixV(const float* v) noexcept {
        // Column-major view matrix rows:
        //   row 0 = camRight = (v[0], v[4], v[8])
        //   row 1 = camUp    = (v[1], v[5], v[9])
        //   row 2 = camBack  = (v[2], v[6], v[10])
        const float rx = v[0], ry = v[4], rz = v[8];
        const float ux = v[1], uy = v[5], uz = v[9];
        const float bx = v[2], by = v[6], bz = v[10];
        const float rLen = std::sqrt(rx * rx + rz * rz);
        if (rLen > 1e-4f) {
            camRightX_ = rx / rLen;
            camRightZ_ = rz / rLen;
            hasCamRight_ = true;
        }
        const float tx = v[12], ty = v[13], tz = v[14];
        const float eyeX = -(rx * tx + ux * ty + bx * tz);
        const float eyeY = -(ry * tx + uy * ty + by * tz);
        const float eyeZ = -(rz * tx + uz * ty + bz * tz);
        if (std::fabs(by) > 0.08f && eyeY > 0.5f && eyeY < 250.0f) {
            const float t = eyeY / by;
            if (std::fabs(t) < 250.0f) {
                camCenterX_ = eyeX - t * bx;
                camCenterZ_ = eyeZ - t * bz;
                hasCamCenter_ = true;
            }
        }
    }

    struct DirShadowCullBox {
        float minAnchorX = -1e30f;
        float maxAnchorX =  1e30f;
        float minAnchorZ = -1e30f;
        float maxAnchorZ =  1e30f;

        bool TouchesAabbXZ(const float boundsXZ[4]) const noexcept {
            return boundsXZ[1] >= minAnchorX && boundsXZ[0] <= maxAnchorX &&
                   boundsXZ[3] >= minAnchorZ && boundsXZ[2] <= maxAnchorZ;
        }
    };

    void BuildDirShadowCullBox(const ShadowConfig& cfg, DirShadowCullBox& out) const noexcept {
        const float dLenSq = cfg.dirWx * cfg.dirWx + cfg.dirWz * cfg.dirWz;
        float dx = 0.0f, dz = 0.0f;
        if (dLenSq > 1e-8f) {
            const float invD = 1.0f / std::sqrt(dLenSq);
            dx = cfg.dirWx * invD;
            dz = cfg.dirWz * invD;
        }
        const float stretch = std::isfinite(cfg.stretch) ? std::clamp(cfg.stretch, 0.0f, 4.0f) : 1.35f;
        const float projX = dx * (7.5f * stretch);
        const float projZ = dz * (7.5f * stretch);
        constexpr float kPad = 6.5f; // generous sprite half-width + wind sway margin
        out.minAnchorX = (writeCenterX_ - kShadowHalfExtent) - kPad - (projX > 0.0f ? projX : 0.0f);
        out.maxAnchorX = (writeCenterX_ + kShadowHalfExtent) + kPad - (projX < 0.0f ? projX : 0.0f);
        out.minAnchorZ = (writeCenterZ_ - kShadowHalfExtent) - kPad - (projZ > 0.0f ? projZ : 0.0f);
        out.maxAnchorZ = (writeCenterZ_ + kShadowHalfExtent) + kPad - (projZ < 0.0f ? projZ : 0.0f);
    }

    struct SavedRasterState {
        bool     caps[5]          = { true, false, false, false, false };
        bool     capsKnown[5]     = { false, false, false, false, false };
        uint32_t blendEqRgb       = kGlFuncAdd;
        uint32_t blendEqAlpha     = kGlFuncAdd;
        bool     blendEqKnown     = false;
        uint32_t blendSrcRgb      = kGlSrcAlpha;
        uint32_t blendDstRgb      = kGlOneMinusSrcAlpha;
        uint32_t blendSrcAlpha    = kGlSrcAlpha;
        uint32_t blendDstAlpha    = kGlOneMinusSrcAlpha;
        bool     blendFnKnown     = false;
        uint32_t depthFunc        = kGlLess;
        bool     depthFuncKnown   = false;
        uint8_t  depthMask        = 0u;
        bool     depthMaskKnown   = false;
        uint8_t  colorMask[4]     = { 1u, 1u, 1u, 1u };
        bool     colorMaskKnown   = false;
    };

    void ApplyShadowWriteRasterState() noexcept {
        ops_.glDisable(kGlCullFace);
        ops_.glDisable(kGlScissorTest);
        ops_.glDisable(kGlStencilTest);
        ops_.glColorMask(1, 1, 1, 1);
        if (isNativeGl_) {
            ops_.glDisable(kGlDepthTest);
            ops_.glDepthMask(0);
            ops_.glEnable(kGlBlend);
            ops_.glBlendEquation(kGlMax);
            ops_.glBlendFunc(kGlOne, kGlOne);
        } else {
            ops_.glDisable(kGlBlend);
            ops_.glEnable(kGlDepthTest);
            ops_.glDepthMask(1);
            ops_.glDepthFunc(kGlLess);
        }
    }

    uint32_t ResolveHostProgram(const ShadowPassState& saved,
                                const hs_gl_cache::ThreadState& glCache) const noexcept {
        if ((!saved.glCacheTrusted || glCache.curProgram == hs_gl_cache::kInvalidId) && ops_.glGetIntegerv) {
            int32_t drvProg = -1;
            ops_.glGetIntegerv(kGlCurrentProgram, &drvProg);
            if (drvProg >= 0) {
                return static_cast<uint32_t>(drvProg);
            }
        }
        return saved.dstProg;
    }

    SavedRasterState CaptureRasterState(const hs_gl_cache::ThreadState& glCache,
                                        bool glCacheTrusted,
                                        bool blendCacheTrusted) const noexcept {
        SavedRasterState rs{};
        static constexpr uint32_t kCapEnums[5] = {
            kGlBlend, kGlDepthTest, kGlCullFace, kGlScissorTest, kGlStencilTest
        };
        for (int i = 0; i < 5; ++i) {
            if (glCacheTrusted && glCache.caps[i] >= 0) {
                rs.caps[i]      = (glCache.caps[i] != 0);
                rs.capsKnown[i] = true;
            } else if (ops_.glGetIntegerv) {
                int32_t v = -1;
                ops_.glGetIntegerv(kCapEnums[i], &v);
                if (v >= 0) {
                    rs.caps[i]      = (v != 0);
                    rs.capsKnown[i] = true;
                }
            }
        }

        // Fast zero-stall path when caller certifies symmetric-only host blend (e.g. DST IAT only imports
        // glBlendEquation/glBlendFunc) AND glCache holds valid values. Falls back to live glGetIntegerv
        // whenever blendCacheTrusted==false (e.g. separate-alpha hosts/tests) or cache is unknown!
        if (glCacheTrusted && blendCacheTrusted && glCache.blendEq != hs_gl_cache::kInvalidId) {
            rs.blendEqRgb   = glCache.blendEq;
            rs.blendEqAlpha = glCache.blendEq;
            rs.blendEqKnown = true;
        } else if (ops_.glGetIntegerv) {
            int32_t eqRgb = -1, eqAlpha = -1;
            ops_.glGetIntegerv(kGlBlendEquationRgb, &eqRgb);
            ops_.glGetIntegerv(kGlBlendEquationAlpha, &eqAlpha);
            if (eqRgb > 0) {
                rs.blendEqRgb   = static_cast<uint32_t>(eqRgb);
                rs.blendEqAlpha = (eqAlpha > 0) ? static_cast<uint32_t>(eqAlpha) : rs.blendEqRgb;
                rs.blendEqKnown = true;
            }
        }

        if (glCacheTrusted && blendCacheTrusted &&
            glCache.blendSrc != hs_gl_cache::kInvalidId &&
            glCache.blendDst != hs_gl_cache::kInvalidId) {
            rs.blendSrcRgb   = glCache.blendSrc;
            rs.blendDstRgb   = glCache.blendDst;
            rs.blendSrcAlpha = glCache.blendSrc;
            rs.blendDstAlpha = glCache.blendDst;
            rs.blendFnKnown  = true;
        } else if (ops_.glGetIntegerv) {
            int32_t sRgb = -1, dRgb = -1, sA = -1, dA = -1;
            ops_.glGetIntegerv(kGlBlendSrcRgb, &sRgb);
            ops_.glGetIntegerv(kGlBlendDstRgb, &dRgb);
            ops_.glGetIntegerv(kGlBlendSrcAlpha, &sA);
            ops_.glGetIntegerv(kGlBlendDstAlpha, &dA);
            if (sRgb >= 0 && dRgb >= 0) {
                rs.blendSrcRgb   = static_cast<uint32_t>(sRgb);
                rs.blendDstRgb   = static_cast<uint32_t>(dRgb);
                rs.blendSrcAlpha = (sA >= 0) ? static_cast<uint32_t>(sA) : rs.blendSrcRgb;
                rs.blendDstAlpha = (dA >= 0) ? static_cast<uint32_t>(dA) : rs.blendDstRgb;
                rs.blendFnKnown  = true;
            }
        }

        if (glCacheTrusted && glCache.depthFunc != hs_gl_cache::kInvalidId) {
            rs.depthFunc      = glCache.depthFunc;
            rs.depthFuncKnown = true;
        } else if (ops_.glGetIntegerv) {
            int32_t v = -1;
            ops_.glGetIntegerv(kGlDepthFuncEnum, &v);
            if (v > 0) {
                rs.depthFunc      = static_cast<uint32_t>(v);
                rs.depthFuncKnown = true;
            }
        }

        if (glCacheTrusted && glCache.depthMask >= 0) {
            rs.depthMask      = static_cast<uint8_t>(glCache.depthMask ? 1u : 0u);
            rs.depthMaskKnown = true;
        } else if (ops_.glGetIntegerv) {
            int32_t v = -1;
            ops_.glGetIntegerv(kGlDepthWritemask, &v);
            if (v >= 0) {
                rs.depthMask      = static_cast<uint8_t>(v ? 1u : 0u);
                rs.depthMaskKnown = true;
            }
        }

        if (glCacheTrusted && glCache.colorMask >= 0) {
            const int8_t cm = glCache.colorMask;
            rs.colorMask[0]   = (cm & 1) ? 1u : 0u;
            rs.colorMask[1]   = (cm & 2) ? 1u : 0u;
            rs.colorMask[2]   = (cm & 4) ? 1u : 0u;
            rs.colorMask[3]   = (cm & 8) ? 1u : 0u;
            rs.colorMaskKnown = true;
        } else if (ops_.glGetIntegerv) {
            int32_t cm[4] = { -1, -1, -1, -1 };
            ops_.glGetIntegerv(kGlColorWritemask, cm);
            if (cm[0] >= 0 && cm[1] >= 0 && cm[2] >= 0 && cm[3] >= 0) {
                rs.colorMask[0]   = cm[0] ? 1u : 0u;
                rs.colorMask[1]   = cm[1] ? 1u : 0u;
                rs.colorMask[2]   = cm[2] ? 1u : 0u;
                rs.colorMask[3]   = cm[3] ? 1u : 0u;
                rs.colorMaskKnown = true;
            }
        }
        return rs;
    }

    void RestoreRasterState(const SavedRasterState& rs,
                            hs_gl_cache::ThreadState& glCache,
                            const int32_t dstVp[4],
                            bool glCacheTrusted,
                            bool restoreBlend = true,
                            bool restoreDepthFunc = true) noexcept {
        static constexpr uint32_t kCapEnums[5] = {
            kGlBlend, kGlDepthTest, kGlCullFace, kGlScissorTest, kGlStencilTest
        };
        for (int i = 0; i < 5; ++i) {
            if (rs.caps[i]) ops_.glEnable(kCapEnums[i]);
            else            ops_.glDisable(kCapEnums[i]);
            if (glCacheTrusted) {
                glCache.caps[i] = rs.capsKnown[i] ? static_cast<int8_t>(rs.caps[i] ? 1 : 0) : -1;
            }
        }

        const bool splitEq = (rs.blendEqAlpha != rs.blendEqRgb);
        if (restoreBlend) {
            if (splitEq && ops_.glBlendEquationSeparate) {
                ops_.glBlendEquationSeparate(rs.blendEqRgb, rs.blendEqAlpha);
            } else {
                ops_.glBlendEquation(rs.blendEqRgb);
            }
        }
        if (glCacheTrusted) {
            glCache.blendEq = (rs.blendEqKnown && !splitEq) ? rs.blendEqRgb : hs_gl_cache::kInvalidId;
        }

        const bool splitFn = (rs.blendSrcAlpha != rs.blendSrcRgb || rs.blendDstAlpha != rs.blendDstRgb);
        if (restoreBlend) {
            if (splitFn && ops_.glBlendFuncSeparate) {
                ops_.glBlendFuncSeparate(rs.blendSrcRgb, rs.blendDstRgb, rs.blendSrcAlpha, rs.blendDstAlpha);
            } else {
                ops_.glBlendFunc(rs.blendSrcRgb, rs.blendDstRgb);
            }
        }
        if (glCacheTrusted) {
            glCache.blendSrc = (rs.blendFnKnown && !splitFn) ? rs.blendSrcRgb : hs_gl_cache::kInvalidId;
            glCache.blendDst = (rs.blendFnKnown && !splitFn) ? rs.blendDstRgb : hs_gl_cache::kInvalidId;
        }

        if (restoreDepthFunc) {
            ops_.glDepthFunc(rs.depthFunc);
        }
        if (glCacheTrusted) {
            glCache.depthFunc = rs.depthFuncKnown ? rs.depthFunc : hs_gl_cache::kInvalidId;
        }

        ops_.glDepthMask(rs.depthMask);
        if (glCacheTrusted) {
            glCache.depthMask = rs.depthMaskKnown ? static_cast<int8_t>(rs.depthMask ? 1 : 0) : -1;
        }

        ops_.glColorMask(rs.colorMask[0], rs.colorMask[1], rs.colorMask[2], rs.colorMask[3]);
        if (glCacheTrusted) {
            if (rs.colorMaskKnown) {
                glCache.colorMask = static_cast<int8_t>(
                    (rs.colorMask[0] ? 1 : 0) | (rs.colorMask[1] ? 2 : 0) |
                    (rs.colorMask[2] ? 4 : 0) | (rs.colorMask[3] ? 8 : 0));
            } else {
                glCache.colorMask = -1;
            }
            glCache.vpValid = true;
            glCache.vp[0] = dstVp[0];
            glCache.vp[1] = dstVp[1];
            glCache.vp[2] = dstVp[2];
            glCache.vp[3] = dstVp[3];
        }
    }

    void CompositeGroundQuad(const ShadowConfig& cfg,
                             const ShadowPassState& saved,
                             hs_gl_cache::ThreadState& glCache) noexcept {
        const uint32_t restoreProg = ResolveHostProgram(saved, glCache);
        ops_.glBindFramebuffer(kGlFramebuffer, saved.dstFbo);
        ops_.glViewport(saved.vp[0], saved.vp[1], saved.vp[2], saved.vp[3]);
        ops_.glDisable(kGlDepthTest);
        ops_.glDepthMask(0);
        ops_.glDisable(kGlCullFace);
        ops_.glDisable(kGlScissorTest);
        ops_.glDisable(kGlStencilTest);
        ops_.glColorMask(1, 1, 1, 0); // Multiply RGB only; preserve PrimaryRT alpha!
        ops_.glEnable(kGlBlend);
        ops_.glBlendEquation(kGlFuncAdd);
        ops_.glBlendFunc(kGlZero, kGlSrcColor);

        ops_.glUseProgram(progComposite_);
        ops_.glActiveTexture(kGlTexture0 + kCompositeTexUnit);
        ops_.glBindTexture(kGlTexture2D, tex_[readSlot_]);
        if (texBlueNoise_ != 0u) {
            ops_.glActiveTexture(kGlTexture0 + kBlueNoiseTexUnit);
            ops_.glBindTexture(kGlTexture2D, texBlueNoise_);
        }

        const float prevBounds[4] = { prevCenterX_, prevCenterZ_, kShadowHalfExtent, 0.0f };
        const float shadCfg[4]    = { cfg.tintR, cfg.tintG, cfg.tintB, cfg.density };
        const float filterUnit    = (res_ >= 1024u) ? (1.0f / 1024.0f) : (1.0f / static_cast<float>(res_));
        const float filterCfg[4]  = {
            filterUnit,
            std::clamp(cfg.soft, 0.0f, 2.0f),
            cfg.penumbraBias,
            (texBlueNoise_ != 0u) ? 1.0f : 0.0f
        };
        if (locCompPV_ >= 0)         ops_.glUniformMatrix4fv(locCompPV_, 1, 0, curPV_);
        if (locCompPrevBounds_ >= 0) ops_.glUniform4fv(locCompPrevBounds_, 1, prevBounds);
        if (locCompShadowCfg_ >= 0)  ops_.glUniform4fv(locCompShadowCfg_, 1, shadCfg);
        if (locCompFilterCfg_ >= 0)  ops_.glUniform4fv(locCompFilterCfg_, 1, filterCfg);

        ops_.glBindBuffer(kGlArrayBuffer, quadVbo_);
        ops_.glEnableVertexAttribArray(0u);
        ops_.glVertexAttribPointer(0u, 2, kGlFloat, 0, 8, nullptr);

        ops_.glDrawArrays(kGlTriangleStrip, 0, 4);

        // Restore exact DST vertex attrib 0, buffers, texture unit, program (raster state restored ONCE by caller!)
        if (!saved.attr0.enabled) {
            ops_.glDisableVertexAttribArray(0u);
        }
        ops_.glBindBuffer(kGlArrayBuffer, saved.boundArrayBuf);
        if (saved.attr0.valid) {
            if (saved.attr0.buffer != saved.boundArrayBuf) {
                ops_.glBindBuffer(kGlArrayBuffer, saved.attr0.buffer);
                ops_.glVertexAttribPointer(0u, saved.attr0.size, saved.attr0.type,
                                           saved.attr0.normalized, saved.attr0.stride, saved.attr0.pointer);
                ops_.glBindBuffer(kGlArrayBuffer, saved.boundArrayBuf);
            } else {
                ops_.glVertexAttribPointer(0u, saved.attr0.size, saved.attr0.type,
                                           saved.attr0.normalized, saved.attr0.stride, saved.attr0.pointer);
            }
        }
        ops_.glActiveTexture(kGlTexture0 + saved.activeUnit);
        ops_.glUseProgram(restoreProg);
        if (kCompositeTexUnit < hs_gl_cache::kMaxTexUnits) {
            glCache.boundTex2D[kCompositeTexUnit] = hs_gl_cache::kInvalidId;
        }
        if (kBlueNoiseTexUnit < hs_gl_cache::kMaxTexUnits) {
            glCache.boundTex2D[kBlueNoiseTexUnit] = hs_gl_cache::kInvalidId;
        }
        if (saved.glCacheTrusted) {
            glCache.curProgram = restoreProg;
            glCache.activeUnit = static_cast<int32_t>(saved.activeUnit);
            glCache.boundArrayBuf = saved.boundArrayBuf;
        } else {
            glCache.activeUnit = -1;
        }
    }

    uint32_t CompileShader(uint32_t type, const char* rawSrc, const char* tag) noexcept {
        std::string src;
        if (isNativeGl_ && std::strncmp(rawSrc, "#version", 8) != 0) {
            src = "#version 100\n";
            src += rawSrc;
        } else {
            src = rawSrc;
        }
        const uint32_t sh = ops_.glCreateShader(type);
        if (sh == 0u) return 0u;
        const char* p = src.c_str();
        const int32_t len = static_cast<int32_t>(src.size());
        ops_.glShaderSource(sh, 1, &p, &len);
        ops_.glCompileShader(sh);
        int32_t ok = 0;
        ops_.glGetShaderiv(sh, kGlCompileStatus, &ok);
        if (!ok) {
            if (ops_.logFn && ops_.glGetShaderInfoLog) {
                char info[512]{};
                int32_t outLen = 0;
                ops_.glGetShaderInfoLog(sh, static_cast<int32_t>(sizeof(info) - 1), &outLen, info);
                char msg[640]{};
                std::snprintf(msg, sizeof(msg), "[SHADOW][ERR] Shader %s compile failed: %s", tag, info);
                ops_.logFn(msg);
            }
            ops_.glDeleteShader(sh);
            return 0u;
        }
        return sh;
    }

    uint32_t BuildProgram(const char* vsSrc, const char* fsSrc, const char* tag, bool isBatch) noexcept {
        const uint32_t vs = CompileShader(kGlVertexShader, vsSrc, tag);
        const uint32_t fs = CompileShader(kGlFragmentShader, fsSrc, tag);
        if (vs == 0u || fs == 0u) {
            if (vs) ops_.glDeleteShader(vs);
            if (fs) ops_.glDeleteShader(fs);
            return 0u;
        }
        const uint32_t prog = ops_.glCreateProgram();
        if (prog == 0u) {
            ops_.glDeleteShader(vs);
            ops_.glDeleteShader(fs);
            return 0u;
        }
        ops_.glAttachShader(prog, vs);
        ops_.glAttachShader(prog, fs);
        if (isBatch) {
            ops_.glBindAttribLocation(prog, 0u, "POS2D_UV");
            ops_.glBindAttribLocation(prog, 5u, "HS_BATCH_CMULT");
            ops_.glBindAttribLocation(prog, 6u, "HS_BATCH_CADD");
            ops_.glBindAttribLocation(prog, 7u, "HS_BATCH_WPOS");
        } else {
            ops_.glBindAttribLocation(prog, 0u, "a_corner");
        }
        ops_.glLinkProgram(prog);
        ops_.glDeleteShader(vs);
        ops_.glDeleteShader(fs);
        int32_t ok = 0;
        ops_.glGetProgramiv(prog, kGlLinkStatus, &ok);
        if (!ok) {
            if (ops_.logFn && ops_.glGetProgramInfoLog) {
                char info[512]{};
                int32_t outLen = 0;
                ops_.glGetProgramInfoLog(prog, static_cast<int32_t>(sizeof(info) - 1), &outLen, info);
                char msg[640]{};
                std::snprintf(msg, sizeof(msg), "[SHADOW][ERR] Program %s link failed: %s", tag, info);
                ops_.logFn(msg);
            }
            ops_.glDeleteProgram(prog);
            return 0u;
        }
        return prog;
    }

    bool EnsureResources(uint32_t wantRes,
                         const ShadowPassState& saved,
                         hs_gl_cache::ThreadState& glCache) noexcept {
        if (failed_) return false;
        if (ready_ && res_ == wantRes) return true;
        if (ready_ && res_ != wantRes) {
            DestroyFbosOnly();
            hasPrevMap_ = false;
        }

        // Unconditional RAII restoration guard covering EVERY success & failure exit path:
        // restores host program, FBO, renderbuffer, array buffer, active texture unit, and glCache!
        struct EnsureResourcesGuard {
            const GlShadowOps&        ops;
            const ShadowPassState&    saved;
            hs_gl_cache::ThreadState& glCache;
            uint32_t                  restoreProg      = 0u;
            uint32_t                  prevRenderbuffer = 0u;
            bool                      touchedProg      = false;
            bool                      touchedFbo       = false;
            bool                      touchedRbo       = false;
            bool                      touchedArrayBuf  = false;
            bool                      touchedActiveTex = false;
            bool                      touchedUnit14    = false;
            bool                      touchedUnit15    = false;
            bool                      restored         = false;

            void Restore() noexcept {
                if (restored) return;
                restored = true;
                if (touchedUnit14) {
                    ops.glActiveTexture(kGlTexture0 + kCompositeTexUnit);
                    ops.glBindTexture(kGlTexture2D, 0u);
                    if (saved.glCacheTrusted && kCompositeTexUnit < hs_gl_cache::kMaxTexUnits) {
                        glCache.boundTex2D[kCompositeTexUnit] = hs_gl_cache::kInvalidId;
                    }
                }
                if (touchedUnit15 && saved.glCacheTrusted && kBlueNoiseTexUnit < hs_gl_cache::kMaxTexUnits) {
                    glCache.boundTex2D[kBlueNoiseTexUnit] = hs_gl_cache::kInvalidId;
                }
                if (touchedFbo) {
                    ops.glBindFramebuffer(kGlFramebuffer, saved.dstFbo);
                }
                if (touchedRbo) {
                    ops.glBindRenderbuffer(kGlRenderbuffer, prevRenderbuffer);
                }
                if (touchedArrayBuf) {
                    ops.glBindBuffer(kGlArrayBuffer, saved.boundArrayBuf);
                    if (saved.glCacheTrusted) {
                        glCache.boundArrayBuf = saved.boundArrayBuf;
                    }
                }
                if (touchedActiveTex) {
                    ops.glActiveTexture(kGlTexture0 + saved.activeUnit);
                    if (saved.glCacheTrusted) {
                        glCache.activeUnit = static_cast<int32_t>(saved.activeUnit);
                    }
                }
                if (touchedProg) {
                    ops.glUseProgram(restoreProg);
                    if (saved.glCacheTrusted) {
                        glCache.curProgram = restoreProg;
                    }
                }
            }

            ~EnsureResourcesGuard() noexcept { Restore(); }
        };

        EnsureResourcesGuard guard{ ops_, saved, glCache };
        guard.restoreProg = ResolveHostProgram(saved, glCache);
        int32_t maxUnits = 0;
        if (ops_.glGetIntegerv) {
            ops_.glGetIntegerv(kGlMaxTextureImageUnits, &maxUnits);
            if (maxUnits > 0 && maxUnits <= static_cast<int32_t>(kCompositeTexUnit)) {
                failed_ = true;
                return false;
            }
        }
        const bool use16BatchSlots = isNativeGl_ && (maxUnits >= 24);
        if (!isNativeGl_ && ops_.glGetIntegerv) {
            int32_t rbo = 0;
            ops_.glGetIntegerv(kGlRenderbufferBinding, &rbo);
            if (rbo > 0) {
                guard.prevRenderbuffer = static_cast<uint32_t>(rbo);
            }
        }

        // 1. Build internal programs & static quad VBO once per context
        if (progBatch_ == 0u) {
            progBatch_ = BuildProgram(kShadowBatchVs, use16BatchSlots ? kShadowBatchFs16 : kShadowBatchFs, "ShadowBatch", true);
            if (progBatch_ == 0u) {
                guard.Restore();
                DestroyResources();
                failed_ = true;
                return false;
            }
            locBatchBounds_     = ops_.glGetUniformLocation(progBatch_, "u_bounds");
            locBatchShadProj_   = ops_.glGetUniformLocation(progBatch_, "u_shadProj");
            locBatchShadDyn_    = ops_.glGetUniformLocation(progBatch_, "u_shadDyn");
            locBatchCamRight_   = ops_.glGetUniformLocation(progBatch_, "u_camRight");
            locBatchPointLight_ = ops_.glGetUniformLocation(progBatch_, "u_pointLight");
            guard.touchedProg = true;
            ops_.glUseProgram(progBatch_);
            const uint32_t slotCnt = use16BatchSlots ? kMaxBatchTexSlots : kLegacyBatchTexSlots;
            for (uint32_t s = 0; s < slotCnt; ++s) {
                char uName[24];
                std::snprintf(uName, sizeof(uName), "HS_BATCH_TEX%u", s);
                const int32_t loc = ops_.glGetUniformLocation(progBatch_, uName);
                if (loc >= 0) {
                    ops_.glUniform1i(loc, static_cast<int32_t>(BatchSlotToTexUnit(s)));
                }
            }
        }
        if (progBatchGpu_ == 0u && isNativeGl_ && ops_.glBindBufferBase && ops_.glDrawElementsBaseVertex) {
            progBatchGpu_ = BuildProgram(kShadowBatchGpuVs, kShadowBatchGpuFs, "ShadowBatchGpu", true);
            if (progBatchGpu_ != 0u) {
                locBatchGpuBounds_     = ops_.glGetUniformLocation(progBatchGpu_, "u_bounds");
                locBatchGpuShadProj_   = ops_.glGetUniformLocation(progBatchGpu_, "u_shadProj");
                locBatchGpuShadDyn_    = ops_.glGetUniformLocation(progBatchGpu_, "u_shadDyn");
                locBatchGpuCamRight_   = ops_.glGetUniformLocation(progBatchGpu_, "u_camRight");
                locBatchGpuPointLight_ = ops_.glGetUniformLocation(progBatchGpu_, "u_pointLight");
            }
        }
        if (progComposite_ == 0u) {
            progComposite_ = BuildProgram(kShadowCompositeVs, kShadowCompositeFs, "ShadowComposite", false);
            if (progComposite_ == 0u) {
                guard.Restore();
                DestroyResources();
                failed_ = true;
                return false;
            }
            locCompPV_         = ops_.glGetUniformLocation(progComposite_, "u_curPV");
            locCompPrevBounds_ = ops_.glGetUniformLocation(progComposite_, "u_prevBounds");
            locCompShadowCfg_  = ops_.glGetUniformLocation(progComposite_, "u_shadowCfg");
            locCompFilterCfg_  = ops_.glGetUniformLocation(progComposite_, "u_filterCfg");
            const int32_t locMap = ops_.glGetUniformLocation(progComposite_, "u_shadowMap");
            const int32_t locBn  = ops_.glGetUniformLocation(progComposite_, "u_blueNoise");
            guard.touchedProg = true;
            ops_.glUseProgram(progComposite_);
            if (locMap >= 0) {
                ops_.glUniform1i(locMap, static_cast<int32_t>(kCompositeTexUnit));
            }
            if (locBn >= 0) {
                ops_.glUniform1i(locBn, static_cast<int32_t>(kBlueNoiseTexUnit));
            }
        }

        // Upload precomputed 64x64 Void-and-Cluster Blue Noise texture once per context (16KB RGBA8)
        if (texBlueNoise_ == 0u) {
            const int32_t bnUnits = (maxUnits > 0) ? maxUnits : 16;
            if (bnUnits >= static_cast<int32_t>(kBlueNoiseTexUnit + 1u)) {
                ops_.glGenTextures(1, &texBlueNoise_);
                if (texBlueNoise_ != 0u) {
                    uint8_t rgba[kBlueNoiseSize * kBlueNoiseSize * 4u];
                    for (uint32_t i = 0; i < kBlueNoiseSize * kBlueNoiseSize; ++i) {
                        rgba[i * 4u + 0u] = kBlueNoise64R[i];
                        rgba[i * 4u + 1u] = kBlueNoise64G[i];
                        rgba[i * 4u + 2u] = 0u;
                        rgba[i * 4u + 3u] = 255u;
                    }
                    guard.touchedActiveTex = true;
                    guard.touchedUnit15 = true;
                    ops_.glActiveTexture(kGlTexture0 + kBlueNoiseTexUnit);
                    ops_.glBindTexture(kGlTexture2D, texBlueNoise_);
                    ops_.glTexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<int32_t>(kGlNearest));
                    ops_.glTexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<int32_t>(kGlNearest));
                    ops_.glTexParameteri(kGlTexture2D, kGlTextureWrapS, static_cast<int32_t>(kGlRepeat));
                    ops_.glTexParameteri(kGlTexture2D, kGlTextureWrapT, static_cast<int32_t>(kGlRepeat));
                    ops_.glTexImage2D(kGlTexture2D, 0, static_cast<int32_t>(kGlRgba),
                                      static_cast<int32_t>(kBlueNoiseSize), static_cast<int32_t>(kBlueNoiseSize),
                                      0, kGlRgba, kGlUnsignedByte, rgba);
                }
            }
        }

        if (quadVbo_ == 0u) {
            static constexpr float kQuadVerts[8] = {
                -1.0f, -1.0f,
                 1.0f, -1.0f,
                -1.0f,  1.0f,
                 1.0f,  1.0f,
            };
            ops_.glGenBuffers(1, &quadVbo_);
            if (quadVbo_ == 0u) {
                guard.Restore();
                DestroyResources();
                failed_ = true;
                return false;
            }
            guard.touchedArrayBuf = true;
            ops_.glBindBuffer(kGlArrayBuffer, quadVbo_);
            ops_.glBufferData(kGlArrayBuffer, sizeof(kQuadVerts), kQuadVerts, kGlStaticDraw);
        }

        // 2. Build 2-slot ping-pong RGBA8 ShadowMask FBOs (+ DEPTH16 RBO on ANGLE D3D9)
        //    Gracefully degrades 4096 -> 2048 -> 1024 if a low-VRAM / legacy driver cannot allocate high-res FBOs!
        guard.touchedActiveTex = true;
        guard.touchedUnit14 = true;
        ops_.glActiveTexture(kGlTexture0 + kCompositeTexUnit);
        uint32_t tryRes = wantRes;
        if (ops_.glGetIntegerv) {
            int32_t maxTexSz = 0;
            ops_.glGetIntegerv(0x0D33u /*GL_MAX_TEXTURE_SIZE*/, &maxTexSz);
            if (maxTexSz >= 1024 && tryRes > static_cast<uint32_t>(maxTexSz)) {
                tryRes = static_cast<uint32_t>(maxTexSz);
                if (hdResCap_ > tryRes) hdResCap_ = tryRes;
            }
        }
        bool allOk = false;
        for (int attempt = 0; attempt < 3; ++attempt) {
            ops_.glGenFramebuffers(2, fbo_);
            ops_.glGenTextures(2, tex_);
            if (!isNativeGl_) {
                ops_.glGenRenderbuffers(2, depthRbo_);
            }
            allOk = true;
            for (int i = 0; i < 2; ++i) {
                if (fbo_[i] == 0u || tex_[i] == 0u || (!isNativeGl_ && depthRbo_[i] == 0u)) {
                    allOk = false;
                    break;
                }
                ops_.glBindTexture(kGlTexture2D, tex_[i]);
                ops_.glTexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<int32_t>(kGlLinear));
                ops_.glTexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<int32_t>(kGlLinear));
                ops_.glTexParameteri(kGlTexture2D, kGlTextureWrapS, static_cast<int32_t>(kGlClampToEdge));
                ops_.glTexParameteri(kGlTexture2D, kGlTextureWrapT, static_cast<int32_t>(kGlClampToEdge));
                ops_.glTexImage2D(kGlTexture2D, 0, static_cast<int32_t>(kGlRgba),
                                  static_cast<int32_t>(tryRes), static_cast<int32_t>(tryRes),
                                  0, kGlRgba, kGlUnsignedByte, nullptr);

                guard.touchedFbo = true;
                ops_.glBindFramebuffer(kGlFramebuffer, fbo_[i]);
                ops_.glFramebufferTexture2D(kGlFramebuffer, kGlColorAttachment0, kGlTexture2D, tex_[i], 0);
                if (!isNativeGl_) {
                    guard.touchedRbo = true;
                    ops_.glBindRenderbuffer(kGlRenderbuffer, depthRbo_[i]);
                    ops_.glRenderbufferStorage(kGlRenderbuffer, kGlDepthComponent16,
                                               static_cast<int32_t>(tryRes), static_cast<int32_t>(tryRes));
                    ops_.glFramebufferRenderbuffer(kGlFramebuffer, kGlDepthAttachment, kGlRenderbuffer, depthRbo_[i]);
                }
                const uint32_t status = ops_.glCheckFramebufferStatus(kGlFramebuffer);
                if (status != kGlFramebufferComplete) {
                    if (ops_.logFn) {
                        char msg[192]{};
                        std::snprintf(msg, sizeof(msg), "[SHADOW][ERR] FBO[%d] incomplete: status=0x%X (res=%u nativeGl=%d)",
                                      i, status, tryRes, isNativeGl_ ? 1 : 0);
                        ops_.logFn(msg);
                    }
                    allOk = false;
                    break;
                }
            }
            if (allOk) break;
            DestroyFbosOnly();
            if (tryRes > 2048u) {
                tryRes = 2048u;
                hdResCap_ = 2048u;
            } else if (tryRes > 1024u) {
                tryRes = 1024u;
                hdResCap_ = 1024u;
            } else {
                break;
            }
        }

        // Unconditionally restore host program, FBO, previous renderbuffer, VBO, and active texture unit!
        guard.Restore();

        if (!allOk) {
            DestroyResources();
            failed_ = true;
            return false;
        }

        res_ = tryRes;
        ready_ = true;
        if (ops_.logFn) {
            char msg[224]{};
            std::snprintf(msg, sizeof(msg),
                          "[VISUAL][SHADOW_V2] World-Space ShadowMask FBO ready (%ux%u RGBA8, mode=%s, halfExtent=%.0f, blueNoise64=%d, entityRecv3D=1)",
                          res_, res_, isNativeGl_ ? "GL_MAX" : "DEPTH16", static_cast<double>(kShadowHalfExtent),
                          texBlueNoise_ != 0u ? 1 : 0);
            ops_.logFn(msg);
        }
        return true;
    }

    void DestroyFbosOnly() noexcept {
        if (fbo_[0] || fbo_[1]) {
            ops_.glDeleteFramebuffers(2, fbo_);
            fbo_[0] = fbo_[1] = 0;
        }
        if (tex_[0] || tex_[1]) {
            ops_.glDeleteTextures(2, tex_);
            tex_[0] = tex_[1] = 0;
        }
        if (depthRbo_[0] || depthRbo_[1]) {
            ops_.glDeleteRenderbuffers(2, depthRbo_);
            depthRbo_[0] = depthRbo_[1] = 0;
        }
        res_ = 0;
        ready_ = false;
        hasPrevMap_ = false;
        hasCachedSnapshot_ = false;
        reuseStreak_ = 0u;
    }

    void DestroyResources() noexcept {
        DestroyFbosOnly();
        if (texBlueNoise_)  { ops_.glDeleteTextures(1, &texBlueNoise_); texBlueNoise_ = 0; }
        if (progBatch_)     { ops_.glDeleteProgram(progBatch_);         progBatch_ = 0; }
        if (progBatchGpu_)  { ops_.glDeleteProgram(progBatchGpu_);      progBatchGpu_ = 0; }
        if (progComposite_) { ops_.glDeleteProgram(progComposite_);     progComposite_ = 0; }
        if (quadVbo_)       { ops_.glDeleteBuffers(1, &quadVbo_);       quadVbo_ = 0; }
    }

    GlShadowOps ops_{};
    bool        isNativeGl_ = false;
    bool        ready_      = false;
    bool        failed_     = false;
    uint32_t    res_        = 0;
    uint32_t    hdResCap_   = 2048u;

    uint32_t    fbo_[2]       = {};
    uint32_t    tex_[2]       = {};
    uint32_t    depthRbo_[2]  = {};
    uint32_t    texBlueNoise_ = 0;
    uint32_t    readSlot_     = 0;
    uint32_t    writeSlot_    = 0;
    bool        hasPrevMap_   = false;
    uint32_t    frameRecvGen_ = 1;

    uint32_t    progBatch_          = 0;
    int32_t     locBatchBounds_     = -1;
    int32_t     locBatchShadProj_   = -1;
    int32_t     locBatchShadDyn_    = -1;
    int32_t     locBatchCamRight_   = -1;
    int32_t     locBatchPointLight_ = -1;

    uint32_t    progBatchGpu_          = 0;
    int32_t     locBatchGpuBounds_     = -1;
    int32_t     locBatchGpuShadProj_   = -1;
    int32_t     locBatchGpuShadDyn_    = -1;
    int32_t     locBatchGpuCamRight_   = -1;
    int32_t     locBatchGpuPointLight_ = -1;

    uint32_t    progComposite_    = 0;
    int32_t     locCompPV_        = -1;
    int32_t     locCompPrevBounds_ = -1;
    int32_t     locCompShadowCfg_ = -1;
    int32_t     locCompFilterCfg_ = -1;

    uint32_t    quadVbo_ = 0;

    uint32_t    worldFbo_    = 0;
    int32_t     worldVp_[4]  = { 0, 0, 1280, 720 };
    float       curMatrixP_[16] = {};
    float       curMatrixV_[16] = {};
    float       curPV_[16]      = {};
    bool        hasMatrixP_  = false;
    bool        hasMatrixV_  = false;
    bool        hasPV_       = false;

    float       camRightX_   = 0.707f;
    float       camRightZ_   = -0.707f;
    bool        hasCamRight_ = false;
    float       camCenterX_  = 0.0f;
    float       camCenterZ_  = 0.0f;
    bool        hasCamCenter_ = false;

    float       prevCenterX_ = 0.0f;
    float       prevCenterZ_ = 0.0f;
    float       writeCenterX_ = 0.0f;
    float       writeCenterZ_ = 0.0f;
    float       writeBounds_[4] = { 0.0f, 0.0f, 1.0f / kShadowHalfExtent, 0.0f };

    bool        sawGroundThisFrame_       = false;
    bool        compositedThisFrame_      = false;
    bool        frameShadowHandled_       = false;
    bool        beganWriteThisFrame_      = false;
    bool        reusedThisFrame_          = false;
    bool        wroteAnyThisFrame_        = false;
    bool        batchUniUploadedFrame_    = false;
    bool        batchGpuUniUploadedFrame_ = false;

    ShadowRenderSnapshot pendingSnapshot_{};
    ShadowRenderSnapshot cachedSnapshot_{};
    bool                 hasCachedSnapshot_     = false;
    uint32_t             reuseStreak_           = 0u;
    bool                 lastFrameReused_       = false;
    uint64_t             totalShadowReuseSkips_ = 0ULL;

    uint32_t    curFrameDraws_            = 0;
    uint32_t    curFrameBatchDraws_       = 0;
    uint32_t    curFrameUnbatchedDraws_   = 0;
    uint32_t    curFrameDeferredBatches_  = 0;
    uint32_t    curFrameCoalescedDraws_   = 0;
    uint32_t    lastFrameDraws_           = 0;
    uint32_t    lastFrameBatchDraws_      = 0;
    uint32_t    lastFrameUnbatchedDraws_  = 0;
    uint32_t    lastFrameDeferredBatches_ = 0;
    uint32_t    lastFrameCoalescedDraws_  = 0;
    mutable bool fastEntityReceive1Tap_   = false;
    std::vector<DeferredShadowBatch> deferredBatches_;
    std::vector<uint8_t>             batchLightMask_;
    std::vector<uint8_t>             batchIsGpu_;
    std::vector<int32_t>             mdCounts_;
    std::vector<const void*>         mdPtrs_;
    std::vector<int32_t>             mdBaseVerts_;
};

} // namespace hs_world_shadow
