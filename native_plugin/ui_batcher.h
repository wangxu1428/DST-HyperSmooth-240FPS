#pragma once
// =============================================================================
// HyperSmooth Unified UI + Font Multi-Texture Uber-Batcher (Pure GLES2 Layer)
// =============================================================================
// Coalesces consecutive DST UI (`ui.ksh`, `ui_ctransform.ksh`) and Text/Font
// (`font.ksh`, `font_packed.ksh`, `font_packed_outline.ksh`) draw calls into a
// single 8-texture-slot Uber-Program draw call — WITHOUT flushing when `ui.ksh`
// and `font*.ksh` interleave widget-by-widget in DST's `CacheRenderUI` tree!
//
// Key guarantees:
//   - 100% strict back-to-front painter's Z-order, scissor, stencil, blend,
//     viewport, FBO, and alpha premultiplication parity with vanilla GLSL.
//   - Pre-transforms clip_pos = MatrixPVW * vec4(POSITION, 1.0) on CPU (exact
//     for both ortho and perspective UI matrices) and absorbs per-program
//     MatrixPVW / MatrixP / MatrixV / MatrixW / diagonal COLOUR_XFORM uniforms.
//   - Zero shader-source patching of DST's UI/Font shaders: purely observes
//     glShaderSource + glAttachShader + glLinkProgram and renders accumulated
//     batches through one lazily-compiled internal GLES2 Uber-Program.
//   - 100% fail-closed: any unsupported shader/attrib/matrix/non-diagonal
//     COLOUR_XFORM cleanly flushes pending UI quads, syncs deferred program +
//     uniforms, and passes straight through to vanilla GL.
// =============================================================================

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "sprite_batcher.h"

namespace hs_ui_batch {

inline constexpr uint32_t kGlTriangles              = 0x0004u;
inline constexpr uint32_t kGlTriangleStrip          = 0x0005u;
inline constexpr uint32_t kGlUnsignedByte           = 0x1401u;
inline constexpr uint32_t kGlUnsignedShort          = 0x1403u;
inline constexpr uint32_t kGlFloat                  = 0x1406u;
inline constexpr uint32_t kGlTexture2D              = 0x0DE1u;
inline constexpr uint32_t kGlTexture0               = 0x84C0u;
inline constexpr uint32_t kGlArrayBuffer            = 0x8892u;
inline constexpr uint32_t kGlElementArrayBuffer     = 0x8893u;
inline constexpr uint32_t kGlStreamDraw             = 0x88E0u;
inline constexpr uint32_t kGlFragmentShader         = 0x8B30u;
inline constexpr uint32_t kGlVertexShader           = 0x8B31u;
inline constexpr uint32_t kGlCompileStatus          = 0x8B81u;
inline constexpr uint32_t kGlLinkStatus             = 0x8B82u;
inline constexpr uint32_t kGlMaxTextureImageUnits   = 0x8872u;
inline constexpr uint32_t kMapWriteInvalidateBuf    = 0x0002u | 0x0008u;          // WRITE | INVALIDATE_BUFFER (safe orphan on wrap)
inline constexpr uint32_t kMapWriteAppendUnsync     = 0x0002u | 0x0004u | 0x0020u; // WRITE | INVALIDATE_RANGE | UNSYNC (disjoint tail!)

inline constexpr uint32_t kUiTexUnitBase            = 6u; // GL_TEXTURE6 .. GL_TEXTURE13 (unused by DST UI)
inline constexpr uint32_t kMaxUiTexSlots            = 8u;
inline constexpr uint32_t kMaxUiVerts               = 16384u; // max per single flush
inline constexpr uint32_t kMaxUiIndices             = 32768u; // max per single flush (uint16 safe)
inline constexpr uint32_t kUiStreamBufVerts         = 65536u; // 4 MB AZDO linear append VBO (zero race!)
inline constexpr uint32_t kUiStreamBufIndices       = 131072u; // 256 KB AZDO linear append IBO (zero race!)
inline constexpr uint32_t kMaxUiSingleVerts         = 4096u;
inline constexpr uint32_t kMaxUiSingleIndices       = 8192u;
inline constexpr size_t   kMaxUiShadowCaptureBytes  = 256u * 1024u;

enum class UiShaderTag : uint8_t {
    None = 0,
    UiVs,
    FontVs,
    UiBasicFs,
    UiCTransformFs,
    FontBasicFs,
    FontPackedFs,
    FontPackedOutlineFs,
};

enum class UiProgKind : uint8_t {
    None = 0,
    UiBasic,           // mode 0: tex * color
    UiCTransform,      // mode 0: tex * (color * diag(COLOUR_XFORM))
    FontBasic,         // mode 0: tex * color
    FontPacked,        // mode 1: vec4(dot(tex, tc1)) * color
    FontPackedOutline, // mode 2: packed outline glyph * color
};

inline const char* UiProgKindName(UiProgKind k) noexcept {
    switch (k) {
        case UiProgKind::UiBasic:           return "UI";
        case UiProgKind::UiCTransform:      return "UI_CX";
        case UiProgKind::FontBasic:         return "FONT";
        case UiProgKind::FontPacked:        return "FONT_PACK";
        case UiProgKind::FontPackedOutline: return "FONT_OUTL";
        default:                            return "NONE";
    }
}

struct UiBatchedVertex {
    float cx, cy, cz, cw;     // loc 0: HS_UI_CLIP (clip-space gl_Position)
    float u, v, slot, mode;   // loc 1: HS_UI_UVSM (uv, texSlot 0..7, mode 0/1/2)
    float r, g, b, a;         // loc 2: HS_UI_COL  (premultiplied RGBA * diag CX)
    float t1x, t1y, t1z, t1w; // loc 3: HS_UI_TC1  (TEXCOORD1 channel selector)
};
static_assert(sizeof(UiBatchedVertex) == 64, "UiBatchedVertex must be 64 bytes (1 cache line)");

struct UiProgInfo {
    void*      ctx               = nullptr;
    UiProgKind kind              = UiProgKind::None;
    bool       eligible          = false;
    int32_t    locPos            = -1;
    int32_t    locUv             = -1;
    int32_t    locDiffuse        = -1;
    int32_t    locTc1            = -1;
    int32_t    locMatrixPVW      = -1;
    int32_t    locMatrixP        = -1;
    int32_t    locMatrixV        = -1;
    int32_t    locMatrixW        = -1;
    int32_t    locColourXform    = -1;
    int32_t    locSampler0       = -1;
    int32_t    samplerUnit0      = 0;

    float      matPVW[16]        = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float      matP[16]          = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float      matV[16]          = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float      matW[16]          = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float      matPV[16]         = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float      colourXform[16]   = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float      cxDiag[4]         = { 1,1,1,1 };

    bool       hasPVW            = false;
    bool       hasP              = false;
    bool       hasV              = false;
    bool       hasW              = false;
    bool       pvValid           = false;
    bool       pvwCombinedValid  = false;
    bool       cxDiagonal        = true;

    bool       dirtyPVW          = false;
    bool       dirtyP            = false;
    bool       dirtyV            = false;
    bool       dirtyW            = false;
    bool       dirtyCX           = false;
};

struct UiGlOps {
    using CreateShader_t             = uint32_t (WINAPI*)(uint32_t type);
    using ShaderSource_t             = void (WINAPI*)(uint32_t shader, int32_t count, const char* const* str, const int32_t* len);
    using CompileShader_t            = void (WINAPI*)(uint32_t shader);
    using GetShaderiv_t              = void (WINAPI*)(uint32_t shader, uint32_t pname, int32_t* params);
    using DeleteShader_t             = void (WINAPI*)(uint32_t shader);
    using CreateProgram_t            = uint32_t (WINAPI*)();
    using AttachShader_t             = void (WINAPI*)(uint32_t program, uint32_t shader);
    using BindAttribLocation_t       = void (WINAPI*)(uint32_t program, uint32_t index, const char* name);
    using LinkProgram_t              = void (WINAPI*)(uint32_t program);
    using GetProgramiv_t             = void (WINAPI*)(uint32_t program, uint32_t pname, int32_t* params);
    using DeleteProgram_t            = void (WINAPI*)(uint32_t program);
    using UseProgram_t               = void (WINAPI*)(uint32_t program);
    using GetUniformLocation_t       = int32_t (WINAPI*)(uint32_t program, const char* name);
    using GenBuffers_t               = void (WINAPI*)(int32_t n, uint32_t* buffers);
    using DeleteBuffers_t            = void (WINAPI*)(int32_t n, const uint32_t* buffers);
    using BindBuffer_t               = void (WINAPI*)(uint32_t target, uint32_t buffer);
    using BufferData_t               = void (WINAPI*)(uint32_t target, ptrdiff_t size, const void* data, uint32_t usage);
    using BufferSubData_t            = void (WINAPI*)(uint32_t target, ptrdiff_t offset, ptrdiff_t size, const void* data);
    using MapBufferRange_t           = void* (WINAPI*)(uint32_t target, ptrdiff_t offset, ptrdiff_t length, uint32_t access);
    using UnmapBuffer_t              = uint8_t (WINAPI*)(uint32_t target);
    using ActiveTexture_t            = void (WINAPI*)(uint32_t texture);
    using BindTexture_t              = void (WINAPI*)(uint32_t target, uint32_t texture);
    using Uniform1i_t                = void (WINAPI*)(int32_t location, int32_t x);
    using UniformMatrix4fv_t         = void (WINAPI*)(int32_t location, int32_t count, uint8_t transpose, const float* value);
    using VertexAttribPointer_t      = void (WINAPI*)(uint32_t index, int32_t size, uint32_t type, uint8_t normalized, int32_t stride, const void* ptr);
    using EnableVertexAttribArray_t  = void (WINAPI*)(uint32_t index);
    using DisableVertexAttribArray_t = void (WINAPI*)(uint32_t index);
    using DrawElements_t             = void (WINAPI*)(uint32_t mode, int32_t count, uint32_t type, const void* indices);
    using GetIntegerv_t              = void (WINAPI*)(uint32_t pname, int32_t* params);
    using GetBufferParameteriv_t     = void (WINAPI*)(uint32_t target, uint32_t pname, int32_t* params);
    using SyncStateAfterFlush_t      = void (*)(uint32_t dstProg, uint32_t activeUnit, uint32_t arrayBuf, uint32_t elemBuf, uint16_t enabledMask, void* ctx);

    CreateShader_t             glCreateShader             = nullptr;
    ShaderSource_t             glShaderSource             = nullptr;
    CompileShader_t            glCompileShader            = nullptr;
    GetShaderiv_t              glGetShaderiv              = nullptr;
    DeleteShader_t             glDeleteShader             = nullptr;
    CreateProgram_t            glCreateProgram            = nullptr;
    AttachShader_t             glAttachShader             = nullptr;
    BindAttribLocation_t       glBindAttribLocation       = nullptr;
    LinkProgram_t              glLinkProgram              = nullptr;
    GetProgramiv_t             glGetProgramiv             = nullptr;
    DeleteProgram_t            glDeleteProgram            = nullptr;
    UseProgram_t               glUseProgram               = nullptr;
    GetUniformLocation_t       glGetUniformLocation       = nullptr;
    GenBuffers_t               glGenBuffers               = nullptr;
    DeleteBuffers_t            glDeleteBuffers            = nullptr;
    BindBuffer_t               glBindBuffer               = nullptr;
    BufferData_t               glBufferData               = nullptr;
    BufferSubData_t            glBufferSubData            = nullptr;
    MapBufferRange_t           glMapBufferRange           = nullptr;
    UnmapBuffer_t              glUnmapBuffer              = nullptr;
    ActiveTexture_t            glActiveTexture            = nullptr;
    BindTexture_t              glBindTexture              = nullptr;
    Uniform1i_t                glUniform1i                = nullptr;
    UniformMatrix4fv_t         glUniformMatrix4fv         = nullptr;
    VertexAttribPointer_t      glVertexAttribPointer      = nullptr;
    EnableVertexAttribArray_t  glEnableVertexAttribArray  = nullptr;
    DisableVertexAttribArray_t glDisableVertexAttribArray = nullptr;
    DrawElements_t             glDrawElements             = nullptr;
    GetIntegerv_t              glGetIntegerv              = nullptr;
    GetBufferParameteriv_t     glGetBufferParameteriv     = nullptr;
    SyncStateAfterFlush_t      onPostFlushRestore         = nullptr;
    void*                      callbackCtx                = nullptr;
    bool                       preferHighp                = false;
};

struct UiBatchStats {
    int64_t drawsIn      = 0;
    int64_t drawsBatched = 0;
    int64_t flushes      = 0;
    int64_t vertsBatched = 0;
    int64_t bypassDraws  = 0;
    int64_t progSkips    = 0;
    int64_t readbacks    = 0;
};

// Pure read-only classifier for DST's vanilla UI & Font GLSL shaders
inline UiShaderTag ClassifyUiShaderSource(const std::string& src) noexcept {
    // Vertex shaders
    if (src.find("gl_Position") != std::string::npos) {
        if (src.find("uniform mat4 MatrixPVW;") != std::string::npos &&
            src.find("attribute vec3 POSITION;") != std::string::npos &&
            src.find("attribute vec2 TEXCOORD0;") != std::string::npos &&
            src.find("attribute vec4 DIFFUSE;") != std::string::npos &&
            src.find("gl_Position = MatrixPVW * vec4( POSITION.xyz, 1.0 );") != std::string::npos &&
            src.find("ALPHA_RANGE") == std::string::npos) {
            return UiShaderTag::UiVs;
        }
        if (src.find("uniform mat4 MatrixP;") != std::string::npos &&
            src.find("uniform mat4 MatrixV;") != std::string::npos &&
            src.find("uniform mat4 MatrixW;") != std::string::npos &&
            src.find("attribute vec3 POSITION;") != std::string::npos &&
            src.find("attribute vec4 DIFFUSE;") != std::string::npos &&
            src.find("attribute vec2 TEXCOORD0;") != std::string::npos &&
            src.find("attribute vec4 TEXCOORD1;") != std::string::npos &&
            src.find("mat4 mtxPVW = MatrixP * MatrixV * MatrixW;") != std::string::npos) {
            return UiShaderTag::FontVs;
        }
        return UiShaderTag::None;
    }

    // Fragment shaders
    if (src.find("gl_FragColor") == std::string::npos ||
        src.find("uniform sampler2D SAMPLER[1];") == std::string::npos) {
        return UiShaderTag::None;
    }
    if (src.find("ALPHA_RANGE") != std::string::npos ||
        src.find("LIGHTMAP") != std::string::npos ||
        src.find("OCEAN_BLEND_PARAMS") != std::string::npos ||
        src.find("POS2D_UV") != std::string::npos) {
        return UiShaderTag::None;
    }
    if (src.find("float val = dot(pixel, PS_TEXCOORD1);") != std::string::npos &&
        src.find("float glyph = val > 0.5 ? 2.0*val-1.0 : 0.0;") != std::string::npos) {
        return UiShaderTag::FontPackedOutlineFs;
    }
    if (src.find("float glyph = dot(pixel, PS_TEXCOORD1);") != std::string::npos &&
        src.find("gl_FragColor = vec4(glyph, glyph, glyph, glyph) * PS_COLOUR;") != std::string::npos) {
        return UiShaderTag::FontPackedFs;
    }
    if (src.find("varying vec4 PS_TEXCOORD1;") != std::string::npos &&
        src.find("gl_FragColor = texture2D( SAMPLER[0], PS_TEXCOORD ) * PS_COLOUR;") != std::string::npos &&
        src.find("dot(") == std::string::npos) {
        return UiShaderTag::FontBasicFs;
    }
    if (src.find("uniform mat4 COLOUR_XFORM;") != std::string::npos &&
        src.find("colour.rgba = colour.rgba * COLOUR_XFORM;") != std::string::npos &&
        src.find("colour.rgba *= PS_COLOUR.rgba;") != std::string::npos) {
        return UiShaderTag::UiCTransformFs;
    }
    if (src.find("varying vec2 PS_TEXCOORD;") != std::string::npos &&
        src.find("varying vec4 PS_COLOUR;") != std::string::npos &&
        src.find("colour.rgba *= PS_COLOUR.rgba;") != std::string::npos &&
        src.find("COLOUR_XFORM") == std::string::npos &&
        src.find("PS_TEXCOORD1") == std::string::npos) {
        return UiShaderTag::UiBasicFs;
    }
    return UiShaderTag::None;
}

inline UiProgKind MatchUiProgKind(UiShaderTag vsTag, UiShaderTag fsTag) noexcept {
    if (vsTag == UiShaderTag::UiVs) {
        if (fsTag == UiShaderTag::UiBasicFs)      return UiProgKind::UiBasic;
        if (fsTag == UiShaderTag::UiCTransformFs) return UiProgKind::UiCTransform;
    } else if (vsTag == UiShaderTag::FontVs) {
        if (fsTag == UiShaderTag::FontBasicFs)         return UiProgKind::FontBasic;
        if (fsTag == UiShaderTag::FontPackedFs)        return UiProgKind::FontPacked;
        if (fsTag == UiShaderTag::FontPackedOutlineFs) return UiProgKind::FontPackedOutline;
    }
    return UiProgKind::None;
}

inline void Mat4MulColMajor(const float* A, const float* B, float* out) noexcept {
    float r[16];
    for (int c = 0; c < 4; ++c) {
        const float b0 = B[c * 4 + 0];
        const float b1 = B[c * 4 + 1];
        const float b2 = B[c * 4 + 2];
        const float b3 = B[c * 4 + 3];
        r[c * 4 + 0] = A[0] * b0 + A[4] * b1 + A[8]  * b2 + A[12] * b3;
        r[c * 4 + 1] = A[1] * b0 + A[5] * b1 + A[9]  * b2 + A[13] * b3;
        r[c * 4 + 2] = A[2] * b0 + A[6] * b1 + A[10] * b2 + A[14] * b3;
        r[c * 4 + 3] = A[3] * b0 + A[7] * b1 + A[11] * b2 + A[15] * b3;
    }
    std::memcpy(out, r, sizeof(r));
}

class UiBatcher {
public:
    UiBatcher() {
        verts_.resize(kMaxUiVerts);
        indices_.resize(kMaxUiIndices);
        shadowCache_.SetMaxBytes(8u * 1024u * 1024u); // 8 MB cap for UI/font VBOs
    }

    void SetOps(const UiGlOps& ops) { ops_ = ops; }
    void SetEnabled(bool en) {
        if (enabled_ && !en) {
            Flush(hs_sprite_batch::FlushReason::Other);
            FlushDeferredStateForCurrentProg();
        }
        enabled_ = en;
    }
    bool     IsEnabled() const noexcept { return enabled_; }
    bool     HasPending() const noexcept { return indexCount_ > 0u; }
    bool     IsCurrentProgramEligible() const noexcept { return curProg_ != nullptr && curProg_->eligible; }
    uint32_t CurrentProgramId() const noexcept { return dstProgram_; }
    uint32_t LastRejectCode() const noexcept { return lastRejectCode_; }
    uint32_t UberProgramId() const noexcept { return uberProg_; }

    void ResetContextState(bool deleteGpuObjects) {
        if (deleteGpuObjects && gpuReady_) {
            if (ops_.glDeleteBuffers) {
                if (streamVbo_ != 0u) ops_.glDeleteBuffers(1, &streamVbo_);
                if (streamIbo_ != 0u) ops_.glDeleteBuffers(1, &streamIbo_);
            }
            if (ops_.glDeleteProgram && uberProg_ != 0u) {
                ops_.glDeleteProgram(uberProg_);
            }
        }
        streamVbo_        = 0u;
        streamIbo_        = 0u;
        streamVboCursor_  = 0u;
        streamIboCursor_  = 0u;
        streamVboShrunk_  = false;
        streamIboShrunk_  = false;
        gpuReady_         = false;
        gpuInitFailed_    = false;
        uberProg_         = 0u;
        uberFailed_       = false;
        maxTexSlots_      = kMaxUiTexSlots;
        vertCount_        = 0;
        indexCount_       = 0;
        batchTexCount_    = 0;
        dstProgram_       = 0;
        curProg_          = nullptr;
        dstProgDirty_     = false;
        activeTexUnit_    = 0;
        boundArrayBuf_    = 0;
        boundElemBuf_     = 0;
        enabledAttrMask_  = 0;
        std::memset(boundTex_, 0, sizeof(boundTex_));
        for (auto& a : attribs_) a = {};
        shadowCache_.Clear();
    }

    // Invalidates CPU-side binding tracking & pending staging without orphaning live GPU objects
    // (used when a same-context eglMakeCurrent fails while the context remains alive).
    void ResetBindingsOnly() {
        vertCount_        = 0;
        indexCount_       = 0;
        batchTexCount_    = 0;
        dstProgram_       = 0;
        curProg_          = nullptr;
        dstProgDirty_     = false;
        activeTexUnit_    = 0;
        boundArrayBuf_    = 0;
        boundElemBuf_     = 0;
        enabledAttrMask_  = 0;
        std::memset(boundTex_, 0, sizeof(boundTex_));
        for (auto& a : attribs_) a = {};
        shadowCache_.Clear();
    }

    // Returns true if glUseProgram was absorbed/deferred (no driver call needed while interleaving UI & Font!)
    bool OnUseProgram(uint32_t progId, UiProgInfo* info) {
        const bool wasBatchable = (curProg_ != nullptr && curProg_->eligible);
        const bool nowBatchable = (info != nullptr && info->eligible);
        if (progId == dstProgram_ && curProg_ == info) {
            return wasBatchable && dstProgDirty_;
        }
        if (indexCount_ > 0u && !nowBatchable) {
            Flush(hs_sprite_batch::FlushReason::Prog);
        }
        if (wasBatchable && !nowBatchable) {
            // Leaving UI batchable family: ensure no stale deferred prog remains
            dstProgDirty_ = false;
        }
        dstProgram_ = progId;
        curProg_    = nowBatchable ? info : nullptr;
        if (enabled_ && nowBatchable && wasBatchable) {
            // Interleaving UI <-> Font (or Font <-> UI): ZERO flush and ZERO driver glUseProgram!
            dstProgDirty_ = true;
            ++stats_.progSkips;
            return true;
        }
        if (enabled_ && nowBatchable && indexCount_ > 0u) {
            dstProgDirty_ = true;
            ++stats_.progSkips;
            return true;
        }
        dstProgDirty_ = false;
        return false;
    }

    void OnPostSwapBuffers() noexcept {
        if (dstProgram_ != 0u) {
            dstProgDirty_ = true;
        }
    }

    void OnDeleteOrLinkProgram(uint32_t progId) {
        if (progId != 0u && progId == dstProgram_) {
            Flush(hs_sprite_batch::FlushReason::Prog);
            dstProgram_   = 0u;
            curProg_      = nullptr;
            dstProgDirty_ = false;
        }
    }

    void OnActiveTexture(uint32_t texture) noexcept {
        if (texture >= kGlTexture0 && texture < kGlTexture0 + 16u) {
            activeTexUnit_ = texture - kGlTexture0;
        }
    }

    void OnBindTexture(uint32_t target, uint32_t texId) {
        if (target != kGlTexture2D || activeTexUnit_ >= 16u) return;
        boundTex_[activeTexUnit_] = texId;
    }

    void OnDeleteTextures(int32_t n, const uint32_t* textures) {
        if (n <= 0 || !textures) return;
        if (indexCount_ > 0u) {
            for (int32_t i = 0; i < n; ++i) {
                const uint32_t id = textures[i];
                if (id == 0u) continue;
                for (uint32_t s = 0; s < batchTexCount_; ++s) {
                    if (batchTexIds_[s] == id) {
                        Flush(hs_sprite_batch::FlushReason::State);
                        break;
                    }
                }
                if (indexCount_ == 0u) break;
            }
        }
        for (int32_t i = 0; i < n; ++i) {
            const uint32_t id = textures[i];
            if (id == 0u) continue;
            for (uint32_t u = 0; u < 16u; ++u) {
                if (boundTex_[u] == id) boundTex_[u] = 0u;
            }
        }
    }

    void OnBindBuffer(uint32_t target, uint32_t bufId) noexcept {
        if (target == kGlArrayBuffer)        boundArrayBuf_ = bufId;
        else if (target == kGlElementArrayBuffer) boundElemBuf_ = bufId;
    }

    void OnBufferUpload(uint32_t bufId, const void* data, size_t size, bool captureNow) {
        if (bufId == 0u) return;
        if (captureNow && data != nullptr && size > 0u && size <= kMaxUiShadowCaptureBytes) {
            shadowCache_.OnUpload(bufId, data, size);
        } else {
            // Invalidate any stale shadow copy for this buffer ID (zero-churn for non-UI buffers)
            shadowCache_.OnDelete(bufId);
        }
    }

    void OnDeleteBuffers(int32_t n, const uint32_t* buffers) {
        if (n <= 0 || !buffers) return;
        for (int32_t i = 0; i < n; ++i) {
            const uint32_t id = buffers[i];
            if (id == 0u) continue;
            shadowCache_.OnDelete(id);
            if (boundArrayBuf_ == id) boundArrayBuf_ = 0u;
            if (boundElemBuf_  == id) boundElemBuf_  = 0u;
            for (auto& a : attribs_) {
                if (a.buffer == id) {
                    a.buffer = 0u;
                    a.valid  = false;
                }
            }
        }
    }

    void OnVertexAttribPointer(uint32_t index, int32_t size, uint32_t type,
                               uint8_t normalized, int32_t stride, const void* ptr) noexcept {
        if (index >= 16u) return;
        auto& a = attribs_[index];
        a.buffer     = boundArrayBuf_;
        a.size       = size;
        a.type       = type;
        a.normalized = normalized;
        a.stride     = stride;
        a.pointer    = ptr;
        a.valid      = true;
    }

    void OnEnableDisableVertexAttribArray(uint32_t index, bool enable) noexcept {
        if (index >= 16u) return;
        const uint16_t bit = static_cast<uint16_t>(1u << index);
        if (enable) enabledAttrMask_ |= bit;
        else        enabledAttrMask_ &= static_cast<uint16_t>(~bit);
    }

    bool OnUniform1i(int32_t loc, int32_t val) {
        if (!curProg_ || !curProg_->eligible) return false;
        if (loc == curProg_->locSampler0) {
            curProg_->samplerUnit0 = val;
            if (enabled_) return true; // absorbed (Uber-Program uses its own u_tex0..7)
        }
        // Non-sampler int uniform on UI program -> ensure real prog bound
        Flush(hs_sprite_batch::FlushReason::UniOther);
        EnsureDstProgramBound();
        return false;
    }

    bool TryAbsorbMatrix4(int32_t loc, int32_t count, uint8_t transpose, const float* value) {
        if (!enabled_ || !curProg_ || !curProg_->eligible || count != 1 || transpose != 0 || !value) {
            return false;
        }
        UiProgInfo& p = *curProg_;
        if (loc == p.locMatrixPVW && p.locMatrixPVW >= 0) {
            std::memcpy(p.matPVW, value, 16u * sizeof(float));
            p.hasPVW   = true;
            p.dirtyPVW = true;
            return true;
        }
        if (loc == p.locMatrixP && p.locMatrixP >= 0) {
            std::memcpy(p.matP, value, 16u * sizeof(float));
            p.hasP             = true;
            p.dirtyP           = true;
            p.pvValid          = false;
            p.pvwCombinedValid = false;
            return true;
        }
        if (loc == p.locMatrixV && p.locMatrixV >= 0) {
            std::memcpy(p.matV, value, 16u * sizeof(float));
            p.hasV             = true;
            p.dirtyV           = true;
            p.pvValid          = false;
            p.pvwCombinedValid = false;
            return true;
        }
        if (loc == p.locMatrixW && p.locMatrixW >= 0) {
            std::memcpy(p.matW, value, 16u * sizeof(float));
            p.hasW             = true;
            p.dirtyW           = true;
            p.pvwCombinedValid = false;
            return true;
        }
        if (loc == p.locColourXform && p.locColourXform >= 0) {
            std::memcpy(p.colourXform, value, 16u * sizeof(float));
            p.dirtyCX = true;
            // Pure diagonal check (zero off-diagonal terms -> exact fold into premul color!)
            bool isDiag = true;
            for (int c = 0; c < 4 && isDiag; ++c) {
                for (int r = 0; r < 4; ++r) {
                    if (c != r && std::fabs(value[c * 4 + r]) > 1e-6f) {
                        isDiag = false;
                        break;
                    }
                }
            }
            p.cxDiagonal = isDiag;
            if (isDiag) {
                p.cxDiag[0] = value[0];
                p.cxDiag[1] = value[5];
                p.cxDiag[2] = value[10];
                p.cxDiag[3] = value[15];
            }
            return true;
        }
        return false;
    }

    // Must be called before any non-absorbed uniform or vanilla GL draw on the current program!
    // Returns true if a deferred glUseProgram(dstProgram_) was actually emitted to the GL driver.
    bool EnsureDstProgramBound() {
        if (dstProgDirty_ && ops_.glUseProgram) {
            ops_.glUseProgram(dstProgram_);
            dstProgDirty_ = false;
            return true;
        }
        return false;
    }

    void FlushDeferredStateForCurrentProg() {
        EnsureDstProgramBound();
        if (!curProg_ || !ops_.glUniformMatrix4fv) return;
        UiProgInfo& p = *curProg_;
        if (p.dirtyPVW && p.locMatrixPVW >= 0) {
            ops_.glUniformMatrix4fv(p.locMatrixPVW, 1, 0, p.matPVW);
            p.dirtyPVW = false;
        }
        if (p.dirtyP && p.locMatrixP >= 0) {
            ops_.glUniformMatrix4fv(p.locMatrixP, 1, 0, p.matP);
            p.dirtyP = false;
        }
        if (p.dirtyV && p.locMatrixV >= 0) {
            ops_.glUniformMatrix4fv(p.locMatrixV, 1, 0, p.matV);
            p.dirtyV = false;
        }
        if (p.dirtyW && p.locMatrixW >= 0) {
            ops_.glUniformMatrix4fv(p.locMatrixW, 1, 0, p.matW);
            p.dirtyW = false;
        }
        if (p.dirtyCX && p.locColourXform >= 0) {
            ops_.glUniformMatrix4fv(p.locColourXform, 1, 0, p.colourXform);
            p.dirtyCX = false;
        }
        if (p.locSampler0 >= 0 && ops_.glUniform1i) {
            ops_.glUniform1i(p.locSampler0, p.samplerUnit0);
        }
    }

    void PrepareForUnbatchedDraw() {
        if (indexCount_ > 0u) {
            Flush(hs_sprite_batch::FlushReason::Bypass);
        }
        if (curProg_ && curProg_->eligible) {
            ++stats_.bypassDraws;
        }
        FlushDeferredStateForCurrentProg();
    }

    bool TryBatchDrawArrays(uint32_t mode, int32_t first, int32_t count) {
        if (!CanBatchBase(mode) || first < 0 || count < 3 ||
            static_cast<uint32_t>(count) > kMaxUiSingleVerts) {
            return false;
        }
        const bool isStrip = (mode == kGlTriangleStrip);
        if (!isStrip && (count % 3) != 0) return false;
        const uint32_t vCount = static_cast<uint32_t>(count);
        const uint32_t outIdxCount = isStrip ? ((vCount - 2u) * 3u) : vCount;
        if (outIdxCount > kMaxUiSingleIndices) return false;

        ResolvedStreams st{};
        if (!ResolveVertexStreams(static_cast<uint32_t>(first) + vCount, st)) {
            return false;
        }

        const uint32_t texId = boundTex_[curProg_->samplerUnit0];
        if (texId == 0u) { lastRejectCode_ = 15; return false; }

        ++stats_.drawsIn;
        if (vertCount_ + vCount > kMaxUiVerts || indexCount_ + outIdxCount > kMaxUiIndices) {
            Flush(hs_sprite_batch::FlushReason::Full);
        }
        const int32_t texSlot = AcquireTexSlot(texId);
        if (texSlot < 0) { lastRejectCode_ = 16; return false; }

        const float* pvw = ComputeCurrentPVW();
        const float modeF = ModeFloatForKind(curProg_->kind);
        const bool applyCx = (curProg_->kind == UiProgKind::UiCTransform);
        const float* cx = curProg_->cxDiag;

        const uint32_t baseOutVert = vertCount_;
        for (uint32_t k = 0; k < vCount; ++k) {
            EmitVertex(st, static_cast<uint32_t>(first) + k, pvw,
                       static_cast<float>(texSlot), modeF, applyCx, cx);
        }
        if (!isStrip) {
            for (uint32_t k = 0; k < vCount; ++k) {
                indices_[indexCount_++] = static_cast<uint16_t>(baseOutVert + k);
            }
        } else {
            for (uint32_t t = 0; t + 2u < vCount; ++t) {
                const uint16_t a = static_cast<uint16_t>(baseOutVert + t);
                const uint16_t b = static_cast<uint16_t>(baseOutVert + t + 1u);
                const uint16_t c = static_cast<uint16_t>(baseOutVert + t + 2u);
                if ((t & 1u) == 0u) {
                    indices_[indexCount_++] = a;
                    indices_[indexCount_++] = b;
                    indices_[indexCount_++] = c;
                } else {
                    indices_[indexCount_++] = b;
                    indices_[indexCount_++] = a;
                    indices_[indexCount_++] = c;
                }
            }
        }
        ++stats_.drawsBatched;
        stats_.vertsBatched += vCount;
        return true;
    }

    bool TryBatchDrawElements(uint32_t mode, int32_t count, uint32_t type, const void* indicesPtr) {
        if (!CanBatchBase(mode) || type != kGlUnsignedShort || count < 3) {
            return false;
        }
        const bool isStrip = (mode == kGlTriangleStrip);
        if (!isStrip && (count % 3) != 0) return false;
        const uint32_t rawCount = static_cast<uint32_t>(count);
        if (rawCount > kMaxUiSingleIndices) return false;

        const uint16_t* idxSrc = ResolveIndexPtr(indicesPtr, static_cast<size_t>(rawCount) * sizeof(uint16_t));
        if (!idxSrc) { lastRejectCode_ = 22; return false; }

        uint32_t minIdx = 0xFFFFFFFFu, maxIdx = 0u;
        uint32_t outIdxCount = rawCount;
        if (!isStrip) {
            for (uint32_t k = 0; k < rawCount; ++k) {
                const uint32_t ix = idxSrc[k];
                if (ix < minIdx) minIdx = ix;
                if (ix > maxIdx) maxIdx = ix;
            }
        } else {
            outIdxCount = 0u;
            for (uint32_t t = 0; t + 2u < rawCount; ++t) {
                const uint32_t a = idxSrc[t], b = idxSrc[t + 1u], c = idxSrc[t + 2u];
                if (a == b || b == c || a == c) continue;
                outIdxCount += 3u;
                if (a < minIdx) minIdx = a;
                if (b < minIdx) minIdx = b;
                if (c < minIdx) minIdx = c;
                if (a > maxIdx) maxIdx = a;
                if (b > maxIdx) maxIdx = b;
                if (c > maxIdx) maxIdx = c;
            }
            if (outIdxCount == 0u) {
                ++stats_.drawsIn;
                ++stats_.drawsBatched;
                return true;
            }
            if (outIdxCount > kMaxUiSingleIndices) return false;
        }
        if (minIdx > maxIdx) return false;
        const uint32_t spanVerts = (maxIdx - minIdx) + 1u;
        if (spanVerts > kMaxUiSingleVerts) return false;

        ResolvedStreams st{};
        if (!ResolveVertexStreams(maxIdx + 1u, st)) {
            return false;
        }

        const uint32_t texId = boundTex_[curProg_->samplerUnit0];
        if (texId == 0u) { lastRejectCode_ = 15; return false; }

        ++stats_.drawsIn;
        if (vertCount_ + spanVerts > kMaxUiVerts || indexCount_ + outIdxCount > kMaxUiIndices) {
            Flush(hs_sprite_batch::FlushReason::Full);
        }
        const int32_t texSlot = AcquireTexSlot(texId);
        if (texSlot < 0) { lastRejectCode_ = 16; return false; }

        const float* pvw = ComputeCurrentPVW();
        const float modeF = ModeFloatForKind(curProg_->kind);
        const bool applyCx = (curProg_->kind == UiProgKind::UiCTransform);
        const float* cx = curProg_->cxDiag;

        const uint32_t baseOutVert = vertCount_;
        for (uint32_t k = 0; k < spanVerts; ++k) {
            EmitVertex(st, minIdx + k, pvw,
                       static_cast<float>(texSlot), modeF, applyCx, cx);
        }
        if (!isStrip) {
            for (uint32_t k = 0; k < rawCount; ++k) {
                indices_[indexCount_++] = static_cast<uint16_t>(baseOutVert + (idxSrc[k] - minIdx));
            }
        } else {
            for (uint32_t t = 0; t + 2u < rawCount; ++t) {
                const uint32_t a = idxSrc[t], b = idxSrc[t + 1u], c = idxSrc[t + 2u];
                if (a == b || b == c || a == c) continue;
                const uint16_t ia = static_cast<uint16_t>(baseOutVert + (a - minIdx));
                const uint16_t ib = static_cast<uint16_t>(baseOutVert + (b - minIdx));
                const uint16_t ic = static_cast<uint16_t>(baseOutVert + (c - minIdx));
                if ((t & 1u) == 0u) {
                    indices_[indexCount_++] = ia;
                    indices_[indexCount_++] = ib;
                    indices_[indexCount_++] = ic;
                } else {
                    indices_[indexCount_++] = ib;
                    indices_[indexCount_++] = ia;
                    indices_[indexCount_++] = ic;
                }
            }
        }
        ++stats_.drawsBatched;
        stats_.vertsBatched += spanVerts;
        return true;
    }

    void Flush(hs_sprite_batch::FlushReason /*reason*/ = hs_sprite_batch::FlushReason::Other) {
        if (indexCount_ == 0u) return;
        const uint32_t flushVerts   = vertCount_;
        const uint32_t flushIndices = indexCount_;
        const uint32_t flushTexCnt  = batchTexCount_;
        vertCount_     = 0;
        indexCount_    = 0;
        batchTexCount_ = 0;

        if (!EnsureGpuAndUberReady()) return;
        if (!ops_.glUseProgram || !ops_.glBindBuffer || !ops_.glActiveTexture || !ops_.glBindTexture ||
            !ops_.glVertexAttribPointer || !ops_.glEnableVertexAttribArray ||
            !ops_.glDisableVertexAttribArray || !ops_.glDrawElements) {
            return;
        }

        const uint32_t vbo = streamVbo_;
        const uint32_t ibo = streamIbo_;
        const size_t vBytes = static_cast<size_t>(flushVerts) * sizeof(UiBatchedVertex);
        const size_t iBytes = static_cast<size_t>(flushIndices) * sizeof(uint16_t);
        constexpr size_t kVboCap = static_cast<size_t>(kUiStreamBufVerts) * sizeof(UiBatchedVertex);
        constexpr size_t kIboCap = static_cast<size_t>(kUiStreamBufIndices) * sizeof(uint16_t);

        ops_.glUseProgram(uberProg_);

        // Bind up to 8 UI/Font atlas textures onto GL_TEXTURE6..GL_TEXTURE13
        for (uint32_t s = 0; s < flushTexCnt; ++s) {
            ops_.glActiveTexture(kGlTexture0 + kUiTexUnitBase + s);
            ops_.glBindTexture(kGlTexture2D, batchTexIds_[s]);
        }

        // AZDO Linear Streaming Append Ring:
        //   - Disjoint tail append uses WRITE|INVALIDATE_RANGE|UNSYNCHRONIZED (0 GPU overlap, 0 stall!)
        //   - Wrap-to-0 orphan uses WRITE|INVALIDATE_BUFFER (NO UNSYNCHRONIZED -> 0% GPU/CPU race!)
        //   - Fallback upload advances cursor past [0, bytes) (or marks shrunk for full re-orphan),
        //     and glUnmapBuffer() return value is checked so corrupted unmaps safely fall back!
        size_t vBaseOffset = 0;
        ops_.glBindBuffer(kGlArrayBuffer, vbo);
        bool vUploaded = false;
        if (ops_.glMapBufferRange && ops_.glUnmapBuffer && vBytes <= kVboCap) {
            if (streamVboShrunk_ && ops_.glBufferData) {
                ops_.glBufferData(kGlArrayBuffer, static_cast<ptrdiff_t>(kVboCap), nullptr, kGlStreamDraw);
                streamVboShrunk_ = false;
                streamVboCursor_ = 0u;
            }
            if (!streamVboShrunk_) {
                const bool wrap = (streamVboCursor_ + vBytes > kVboCap);
                const size_t off = wrap ? 0u : streamVboCursor_;
                const uint32_t flags = wrap ? kMapWriteInvalidateBuf : kMapWriteAppendUnsync;
                if (void* mapped = ops_.glMapBufferRange(kGlArrayBuffer, static_cast<ptrdiff_t>(off),
                                                         static_cast<ptrdiff_t>(vBytes), flags)) {
                    std::memcpy(mapped, verts_.data(), vBytes);
                    if (ops_.glUnmapBuffer(kGlArrayBuffer) != 0) {
                        vBaseOffset      = off;
                        streamVboCursor_ = off + vBytes;
                        vUploaded        = true;
                    }
                }
            }
        }
        if (!vUploaded && ops_.glBufferData) {
            vBaseOffset = 0u;
            if (ops_.glBufferSubData && vBytes <= kVboCap) {
                ops_.glBufferData(kGlArrayBuffer, static_cast<ptrdiff_t>(kVboCap), nullptr, kGlStreamDraw);
                ops_.glBufferSubData(kGlArrayBuffer, 0, static_cast<ptrdiff_t>(vBytes), verts_.data());
                streamVboShrunk_ = false;
                streamVboCursor_ = vBytes;
            } else {
                ops_.glBufferData(kGlArrayBuffer, static_cast<ptrdiff_t>(vBytes), verts_.data(), kGlStreamDraw);
                streamVboShrunk_ = true;
                streamVboCursor_ = kVboCap;
            }
        }

        size_t iBaseOffset = 0;
        const size_t iAligned = (iBytes + 3u) & ~static_cast<size_t>(3u);
        ops_.glBindBuffer(kGlElementArrayBuffer, ibo);
        bool iUploaded = false;
        if (ops_.glMapBufferRange && ops_.glUnmapBuffer && iAligned <= kIboCap) {
            if (streamIboShrunk_ && ops_.glBufferData) {
                ops_.glBufferData(kGlElementArrayBuffer, static_cast<ptrdiff_t>(kIboCap), nullptr, kGlStreamDraw);
                streamIboShrunk_ = false;
                streamIboCursor_ = 0u;
            }
            if (!streamIboShrunk_) {
                const bool wrap = (streamIboCursor_ + iAligned > kIboCap);
                const size_t off = wrap ? 0u : streamIboCursor_;
                const uint32_t flags = wrap ? kMapWriteInvalidateBuf : kMapWriteAppendUnsync;
                if (void* mapped = ops_.glMapBufferRange(kGlElementArrayBuffer, static_cast<ptrdiff_t>(off),
                                                         static_cast<ptrdiff_t>(iBytes), flags)) {
                    std::memcpy(mapped, indices_.data(), iBytes);
                    if (ops_.glUnmapBuffer(kGlElementArrayBuffer) != 0) {
                        iBaseOffset      = off;
                        streamIboCursor_ = off + iAligned;
                        iUploaded        = true;
                    }
                }
            }
        }
        if (!iUploaded && ops_.glBufferData) {
            iBaseOffset = 0u;
            if (ops_.glBufferSubData && iAligned <= kIboCap) {
                ops_.glBufferData(kGlElementArrayBuffer, static_cast<ptrdiff_t>(kIboCap), nullptr, kGlStreamDraw);
                ops_.glBufferSubData(kGlElementArrayBuffer, 0, static_cast<ptrdiff_t>(iBytes), indices_.data());
                streamIboShrunk_ = false;
                streamIboCursor_ = iAligned;
            } else {
                ops_.glBufferData(kGlElementArrayBuffer, static_cast<ptrdiff_t>(iBytes), indices_.data(), kGlStreamDraw);
                streamIboShrunk_ = true;
                streamIboCursor_ = kIboCap;
            }
        }

        constexpr int32_t kStride = static_cast<int32_t>(sizeof(UiBatchedVertex));
        ops_.glEnableVertexAttribArray(0u);
        ops_.glEnableVertexAttribArray(1u);
        ops_.glEnableVertexAttribArray(2u);
        ops_.glEnableVertexAttribArray(3u);
        ops_.glVertexAttribPointer(0u, 4, kGlFloat, 0, kStride, reinterpret_cast<const void*>(vBaseOffset + offsetof(UiBatchedVertex, cx)));
        ops_.glVertexAttribPointer(1u, 4, kGlFloat, 0, kStride, reinterpret_cast<const void*>(vBaseOffset + offsetof(UiBatchedVertex, u)));
        ops_.glVertexAttribPointer(2u, 4, kGlFloat, 0, kStride, reinterpret_cast<const void*>(vBaseOffset + offsetof(UiBatchedVertex, r)));
        ops_.glVertexAttribPointer(3u, 4, kGlFloat, 0, kStride, reinterpret_cast<const void*>(vBaseOffset + offsetof(UiBatchedVertex, t1x)));

        ops_.glDrawElements(kGlTriangles, static_cast<int32_t>(flushIndices), kGlUnsignedShort, reinterpret_cast<const void*>(iBaseOffset));
        ++stats_.flushes;

        // Restore DST's exact GL state so vanilla draws & SpriteBatcher see zero side effects!
        ops_.glUseProgram(dstProgram_);
        dstProgDirty_ = false;

        uint32_t curBoundVbo = vbo;
        for (uint32_t loc = 0u; loc < 4u; ++loc) {
            const bool wantEn = (enabledAttrMask_ & (1u << loc)) != 0u;
            if (!wantEn) {
                ops_.glDisableVertexAttribArray(loc);
            }
            const auto& a = attribs_[loc];
            if (a.valid) {
                if (curBoundVbo != a.buffer) {
                    ops_.glBindBuffer(kGlArrayBuffer, a.buffer);
                    curBoundVbo = a.buffer;
                }
                ops_.glVertexAttribPointer(loc, a.size, a.type, a.normalized, a.stride, a.pointer);
            }
        }
        if (curBoundVbo != boundArrayBuf_) {
            ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
        }
        ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
        ops_.glActiveTexture(kGlTexture0 + activeTexUnit_);

        if (ops_.onPostFlushRestore) {
            ops_.onPostFlushRestore(dstProgram_, activeTexUnit_, boundArrayBuf_, boundElemBuf_,
                                    enabledAttrMask_, ops_.callbackCtx);
        }
    }

    UiBatchStats TakeStatsDelta() noexcept {
        UiBatchStats out{};
        out.drawsIn      = stats_.drawsIn      - lastFlushed_.drawsIn;
        out.drawsBatched = stats_.drawsBatched - lastFlushed_.drawsBatched;
        out.flushes      = stats_.flushes      - lastFlushed_.flushes;
        out.vertsBatched = stats_.vertsBatched - lastFlushed_.vertsBatched;
        out.bypassDraws  = stats_.bypassDraws  - lastFlushed_.bypassDraws;
        out.progSkips    = stats_.progSkips    - lastFlushed_.progSkips;
        out.readbacks    = stats_.readbacks    - lastFlushed_.readbacks;
        lastFlushed_ = stats_;
        return out;
    }

    const UiBatchStats& CumulativeStats() const noexcept { return stats_; }

private:
    struct ResolvedStreams {
        const uint8_t* posBase = nullptr;
        size_t         posStride = 0;
        int32_t        posSize = 3;

        const uint8_t* uvBase = nullptr;
        size_t         uvStride = 0;

        const uint8_t* colBase = nullptr;
        size_t         colStride = 0;
        bool           colIsUbyte = false;

        const uint8_t* tc1Base = nullptr;
        size_t         tc1Stride = 0;
        bool           tc1IsUbyte = false;
        bool           hasTc1 = false;
    };

    static inline float ModeFloatForKind(UiProgKind k) noexcept {
        switch (k) {
            case UiProgKind::FontPacked:        return 1.0f;
            case UiProgKind::FontPackedOutline: return 2.0f;
            default:                            return 0.0f;
        }
    }

    bool CanBatchBase(uint32_t mode) {
        if (!enabled_) { lastRejectCode_ = 1; return false; }
        if (gpuInitFailed_ || uberFailed_) { lastRejectCode_ = 2; return false; }
        if (!curProg_ || !curProg_->eligible) { lastRejectCode_ = 3; return false; }
        if (mode != kGlTriangles && mode != kGlTriangleStrip) { lastRejectCode_ = 4; return false; }
        if (curProg_->samplerUnit0 < 0 || curProg_->samplerUnit0 >= 16) { lastRejectCode_ = 5; return false; }
        if (curProg_->kind == UiProgKind::UiBasic || curProg_->kind == UiProgKind::UiCTransform) {
            if (!curProg_->hasPVW) { lastRejectCode_ = 6; return false; }
            if (curProg_->kind == UiProgKind::UiCTransform && !curProg_->cxDiagonal) {
                lastRejectCode_ = 7; // non-diagonal colour matrix -> fail-closed to vanilla!
                return false;
            }
        } else {
            if (!curProg_->hasP || !curProg_->hasV || !curProg_->hasW) { lastRejectCode_ = 8; return false; }
        }
        if (!ops_.glUseProgram || !ops_.glBindBuffer || !ops_.glActiveTexture || !ops_.glBindTexture ||
            !ops_.glVertexAttribPointer || !ops_.glEnableVertexAttribArray ||
            !ops_.glDisableVertexAttribArray || !ops_.glDrawElements) {
            gpuInitFailed_ = true;
            lastRejectCode_ = 2;
            return false;
        }
        // Fail-closed BEFORE accepting the very first UI/Font draw into verts_/indices_:
        // if buffer creation or uber-shader compilation/linking fails, return false immediately
        // so the caller executes the vanilla draw path with ZERO dropped UI or text!
        if ((!gpuReady_ || uberProg_ == 0u) && !EnsureGpuAndUberReady()) {
            lastRejectCode_ = 2;
            return false;
        }
        return true;
    }

    const float* ComputeCurrentPVW() noexcept {
        UiProgInfo& p = *curProg_;
        if (p.kind == UiProgKind::UiBasic || p.kind == UiProgKind::UiCTransform) {
            return p.matPVW;
        }
        if (!p.pvValid) {
            Mat4MulColMajor(p.matP, p.matV, p.matPV);
            p.pvValid = true;
            p.pvwCombinedValid = false;
        }
        if (!p.pvwCombinedValid) {
            Mat4MulColMajor(p.matPV, p.matW, p.matPVW);
            p.pvwCombinedValid = true;
        }
        return p.matPVW;
    }

    const uint8_t* ResolveBufferPtr(uint32_t bufId, uint32_t target, const void* ptr, size_t reqBytes) {
        if (bufId == 0u) {
            return reinterpret_cast<const uint8_t*>(ptr);
        }
        const size_t offset = reinterpret_cast<uintptr_t>(ptr);
        size_t sz = 0;
        const uint8_t* base = shadowCache_.Find(bufId, sz);
        if (!base) {
            // Lazy one-time ES 3.0+ readback for static UI VBOs created before batcher activation
            base = FetchFromGpuOnce(bufId, target, sz);
        }
        if (!base || offset > sz || (sz - offset) < reqBytes) {
            return nullptr;
        }
        return base + offset;
    }

    const uint8_t* FetchFromGpuOnce(uint32_t bufId, uint32_t target, size_t& outSize) {
        outSize = 0;
        if (bufId == 0u || !ops_.glBindBuffer || !ops_.glGetBufferParameteriv ||
            !ops_.glMapBufferRange || !ops_.glUnmapBuffer) {
            return nullptr;
        }
        const uint32_t prevBuf = (target == kGlElementArrayBuffer) ? boundElemBuf_ : boundArrayBuf_;
        if (prevBuf != bufId) ops_.glBindBuffer(target, bufId);

        int32_t bufSz = 0;
        ops_.glGetBufferParameteriv(target, hs_sprite_batch::kGlBufferSize, &bufSz);
        const uint8_t* result = nullptr;
        if (bufSz > 0 && static_cast<size_t>(bufSz) <= kMaxUiShadowCaptureBytes) {
            if (void* mapped = ops_.glMapBufferRange(target, 0, static_cast<ptrdiff_t>(bufSz), hs_sprite_batch::kMapReadBit)) {
                ++stats_.readbacks;
                shadowCache_.OnUpload(bufId, mapped, static_cast<size_t>(bufSz));
                ops_.glUnmapBuffer(target);
                result = shadowCache_.Find(bufId, outSize);
            }
        }
        if (prevBuf != bufId) ops_.glBindBuffer(target, prevBuf);
        return result;
    }

    const uint16_t* ResolveIndexPtr(const void* indicesPtr, size_t reqBytes) {
        return reinterpret_cast<const uint16_t*>(
            ResolveBufferPtr(boundElemBuf_, kGlElementArrayBuffer, indicesPtr, reqBytes));
    }

    bool ResolveVertexStreams(uint32_t numVerts, ResolvedStreams& out) {
        const UiProgInfo& p = *curProg_;
        auto checkAttr = [&](int32_t loc, int32_t minSz, int32_t maxSz, bool allowUbyte,
                             const uint8_t*& baseOut, size_t& strideOut, int32_t& sizeOut, bool& isUbyteOut) -> bool {
            if (loc < 0 || loc >= 16) return false;
            if ((enabledAttrMask_ & (1u << loc)) == 0u) return false;
            const auto& a = attribs_[loc];
            if (!a.valid || a.size < minSz || a.size > maxSz) return false;
            size_t elemBytes = 0;
            if (a.type == kGlFloat) {
                elemBytes = static_cast<size_t>(a.size) * sizeof(float);
                isUbyteOut = false;
            } else if (allowUbyte && a.type == kGlUnsignedByte && a.normalized != 0) {
                elemBytes = static_cast<size_t>(a.size);
                isUbyteOut = true;
            } else {
                return false;
            }
            const size_t stride = (a.stride == 0) ? elemBytes : static_cast<size_t>(a.stride);
            if (stride < elemBytes || numVerts == 0u) return false;
            const size_t reqBytes = static_cast<size_t>(numVerts - 1u) * stride + elemBytes;
            const uint8_t* ptr = ResolveBufferPtr(a.buffer, kGlArrayBuffer, a.pointer, reqBytes);
            if (!ptr) return false;
            baseOut   = ptr;
            strideOut = stride;
            sizeOut   = a.size;
            return true;
        };

        bool dummyUbyte = false;
        int32_t dummySz = 0;
        if (!checkAttr(p.locPos, 2, 3, false, out.posBase, out.posStride, out.posSize, dummyUbyte)) {
            lastRejectCode_ = 11;
            return false;
        }
        if (!checkAttr(p.locUv, 2, 4, false, out.uvBase, out.uvStride, dummySz, dummyUbyte)) {
            lastRejectCode_ = 12;
            return false;
        }
        if (!checkAttr(p.locDiffuse, 4, 4, true, out.colBase, out.colStride, dummySz, out.colIsUbyte)) {
            lastRejectCode_ = 13;
            return false;
        }
        const bool needTc1 = (p.kind == UiProgKind::FontPacked || p.kind == UiProgKind::FontPackedOutline);
        if (needTc1) {
            if (!checkAttr(p.locTc1, 4, 4, true, out.tc1Base, out.tc1Stride, dummySz, out.tc1IsUbyte)) {
                lastRejectCode_ = 14;
                return false;
            }
            out.hasTc1 = true;
        } else {
            out.hasTc1 = false;
        }
        return true;
    }

    int32_t AcquireTexSlot(uint32_t texId) {
        for (uint32_t s = 0; s < batchTexCount_; ++s) {
            if (batchTexIds_[s] == texId) return static_cast<int32_t>(s);
        }
        if (batchTexCount_ < maxTexSlots_) {
            const uint32_t s = batchTexCount_++;
            batchTexIds_[s] = texId;
            return static_cast<int32_t>(s);
        }
        Flush(hs_sprite_batch::FlushReason::Tex);
        batchTexIds_[0] = texId;
        batchTexCount_  = 1u;
        return 0;
    }

    inline void EmitVertex(const ResolvedStreams& st, uint32_t idx, const float* m,
                           float texSlotF, float modeF, bool applyCx, const float* cx) noexcept {
        const float* pos = reinterpret_cast<const float*>(st.posBase + static_cast<size_t>(idx) * st.posStride);
        const float x = pos[0];
        const float y = pos[1];
        const float z = (st.posSize >= 3) ? pos[2] : 0.0f;

        const float* uv = reinterpret_cast<const float*>(st.uvBase + static_cast<size_t>(idx) * st.uvStride);

        float dr, dg, db, da;
        if (!st.colIsUbyte) {
            const float* c = reinterpret_cast<const float*>(st.colBase + static_cast<size_t>(idx) * st.colStride);
            dr = c[0]; dg = c[1]; db = c[2]; da = c[3];
        } else {
            const uint8_t* c = st.colBase + static_cast<size_t>(idx) * st.colStride;
            constexpr float kInv255 = 1.0f / 255.0f;
            dr = static_cast<float>(c[0]) * kInv255;
            dg = static_cast<float>(c[1]) * kInv255;
            db = static_cast<float>(c[2]) * kInv255;
            da = static_cast<float>(c[3]) * kInv255;
        }
        // Premultiply alpha (exact match to ui.ksh & font*.ksh)
        float pr = dr * da;
        float pg = dg * da;
        float pb = db * da;
        float pa = da;
        if (applyCx) {
            pr *= cx[0];
            pg *= cx[1];
            pb *= cx[2];
            pa *= cx[3];
        }

        float t1x = 1.0f, t1y = 1.0f, t1z = 1.0f, t1w = 1.0f;
        if (st.hasTc1) {
            if (!st.tc1IsUbyte) {
                const float* t = reinterpret_cast<const float*>(st.tc1Base + static_cast<size_t>(idx) * st.tc1Stride);
                t1x = t[0]; t1y = t[1]; t1z = t[2]; t1w = t[3];
            } else {
                const uint8_t* t = st.tc1Base + static_cast<size_t>(idx) * st.tc1Stride;
                constexpr float kInv255 = 1.0f / 255.0f;
                t1x = static_cast<float>(t[0]) * kInv255;
                t1y = static_cast<float>(t[1]) * kInv255;
                t1z = static_cast<float>(t[2]) * kInv255;
                t1w = static_cast<float>(t[3]) * kInv255;
            }
        }

        UiBatchedVertex& dst = verts_[vertCount_++];
        dst.cx   = m[0] * x + m[4] * y + m[8]  * z + m[12];
        dst.cy   = m[1] * x + m[5] * y + m[9]  * z + m[13];
        dst.cz   = m[2] * x + m[6] * y + m[10] * z + m[14];
        dst.cw   = m[3] * x + m[7] * y + m[11] * z + m[15];
        dst.u    = uv[0];
        dst.v    = uv[1];
        dst.slot = texSlotF;
        dst.mode = modeF;
        dst.r    = pr;
        dst.g    = pg;
        dst.b    = pb;
        dst.a    = pa;
        dst.t1x  = t1x;
        dst.t1y  = t1y;
        dst.t1z  = t1z;
        dst.t1w  = t1w;
    }

    bool EnsureGpuAndUberReady() {
        if (gpuReady_ && uberProg_ != 0u) return true;
        if (gpuInitFailed_ || uberFailed_) return false;

        if (!gpuReady_) {
            if (!ops_.glGenBuffers || !ops_.glBindBuffer || !ops_.glBufferData) {
                gpuInitFailed_ = true;
                return false;
            }
            if (ops_.glGetIntegerv) {
                int32_t maxUnits = 8;
                ops_.glGetIntegerv(kGlMaxTextureImageUnits, &maxUnits);
                if (maxUnits <= static_cast<int32_t>(kUiTexUnitBase) + 1) {
                    gpuInitFailed_ = true;
                    return false;
                }
                const uint32_t avail = static_cast<uint32_t>(maxUnits) - kUiTexUnitBase;
                maxTexSlots_ = (avail < kMaxUiTexSlots) ? avail : kMaxUiTexSlots;
            }
            ops_.glGenBuffers(1, &streamVbo_);
            ops_.glGenBuffers(1, &streamIbo_);
            if (streamVbo_ == 0u || streamIbo_ == 0u) {
                if (ops_.glDeleteBuffers) {
                    if (streamVbo_ != 0u) ops_.glDeleteBuffers(1, &streamVbo_);
                    if (streamIbo_ != 0u) ops_.glDeleteBuffers(1, &streamIbo_);
                }
                streamVbo_ = 0u;
                streamIbo_ = 0u;
                gpuInitFailed_ = true;
                return false;
            }
            constexpr ptrdiff_t kWantVboBytes = static_cast<ptrdiff_t>(kUiStreamBufVerts * sizeof(UiBatchedVertex));
            constexpr ptrdiff_t kWantIboBytes = static_cast<ptrdiff_t>(kUiStreamBufIndices * sizeof(uint16_t));
            ops_.glBindBuffer(kGlArrayBuffer, streamVbo_);
            ops_.glBufferData(kGlArrayBuffer, kWantVboBytes, nullptr, kGlStreamDraw);
            int32_t vAlloc = static_cast<int32_t>(kWantVboBytes);
            if (ops_.glGetBufferParameteriv) {
                vAlloc = 0;
                ops_.glGetBufferParameteriv(kGlArrayBuffer, hs_sprite_batch::kGlBufferSize, &vAlloc);
            }
            ops_.glBindBuffer(kGlElementArrayBuffer, streamIbo_);
            ops_.glBufferData(kGlElementArrayBuffer, kWantIboBytes, nullptr, kGlStreamDraw);
            int32_t iAlloc = static_cast<int32_t>(kWantIboBytes);
            if (ops_.glGetBufferParameteriv) {
                iAlloc = 0;
                ops_.glGetBufferParameteriv(kGlElementArrayBuffer, hs_sprite_batch::kGlBufferSize, &iAlloc);
            }
            ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
            ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
            if (vAlloc < static_cast<int32_t>(kWantVboBytes) || iAlloc < static_cast<int32_t>(kWantIboBytes)) {
                if (ops_.glDeleteBuffers) {
                    ops_.glDeleteBuffers(1, &streamVbo_);
                    ops_.glDeleteBuffers(1, &streamIbo_);
                }
                streamVbo_ = 0u;
                streamIbo_ = 0u;
                gpuInitFailed_ = true;
                return false;
            }
            streamVboCursor_ = 0u;
            streamIboCursor_ = 0u;
            streamVboShrunk_ = false;
            streamIboShrunk_ = false;
            gpuReady_ = true;
        }

        if (uberProg_ == 0u) {
            auto failUber = [&]() -> bool {
                uberFailed_ = true;
                if (ops_.glDeleteBuffers) {
                    if (streamVbo_ != 0u) ops_.glDeleteBuffers(1, &streamVbo_);
                    if (streamIbo_ != 0u) ops_.glDeleteBuffers(1, &streamIbo_);
                }
                streamVbo_ = 0u;
                streamIbo_ = 0u;
                gpuReady_  = false;
                return false;
            };
            if (!ops_.glCreateShader || !ops_.glShaderSource || !ops_.glCompileShader ||
                !ops_.glGetShaderiv || !ops_.glDeleteShader || !ops_.glCreateProgram ||
                !ops_.glAttachShader || !ops_.glBindAttribLocation || !ops_.glLinkProgram ||
                !ops_.glGetProgramiv || !ops_.glDeleteProgram || !ops_.glUseProgram ||
                !ops_.glGetUniformLocation || !ops_.glUniform1i) {
                return failUber();
            }

            static constexpr const char* kUberVs =
                "attribute vec4 HS_UI_CLIP;\n"
                "attribute vec4 HS_UI_UVSM;\n"
                "attribute vec4 HS_UI_COL;\n"
                "attribute vec4 HS_UI_TC1;\n"
                "varying vec4 v_uvsm;\n"
                "varying vec4 v_col;\n"
                "varying vec4 v_tc1;\n"
                "void main() {\n"
                "    gl_Position = HS_UI_CLIP;\n"
                "    v_uvsm = HS_UI_UVSM;\n"
                "    v_col  = HS_UI_COL;\n"
                "    v_tc1  = HS_UI_TC1;\n"
                "}\n";

            const char* kUberFs = ops_.preferHighp
                ? "#if defined( GL_ES )\nprecision highp float;\n#endif\n"
                  "uniform sampler2D u_tex0;\nuniform sampler2D u_tex1;\n"
                  "uniform sampler2D u_tex2;\nuniform sampler2D u_tex3;\n"
                  "uniform sampler2D u_tex4;\nuniform sampler2D u_tex5;\n"
                  "uniform sampler2D u_tex6;\nuniform sampler2D u_tex7;\n"
                  "varying vec4 v_uvsm;\nvarying vec4 v_col;\nvarying vec4 v_tc1;\n"
                  "void main() {\n"
                  "    float s = v_uvsm.z;\n"
                  "    vec4 tex;\n"
                  "    if (s < 0.5)      tex = texture2D(u_tex0, v_uvsm.xy);\n"
                  "    else if (s < 1.5) tex = texture2D(u_tex1, v_uvsm.xy);\n"
                  "    else if (s < 2.5) tex = texture2D(u_tex2, v_uvsm.xy);\n"
                  "    else if (s < 3.5) tex = texture2D(u_tex3, v_uvsm.xy);\n"
                  "    else if (s < 4.5) tex = texture2D(u_tex4, v_uvsm.xy);\n"
                  "    else if (s < 5.5) tex = texture2D(u_tex5, v_uvsm.xy);\n"
                  "    else if (s < 6.5) tex = texture2D(u_tex6, v_uvsm.xy);\n"
                  "    else              tex = texture2D(u_tex7, v_uvsm.xy);\n"
                  "    float m = v_uvsm.w;\n"
                  "    if (m < 0.5) {\n"
                  "        gl_FragColor = tex * v_col;\n"
                  "    } else if (m < 1.5) {\n"
                  "        float g = dot(tex, v_tc1);\n"
                  "        gl_FragColor = vec4(g, g, g, g) * v_col;\n"
                  "    } else {\n"
                  "        float val = dot(tex, v_tc1);\n"
                  "        float glyph = val > 0.5 ? (2.0 * val - 1.0) : 0.0;\n"
                  "        vec4 p = vec4(glyph, glyph, glyph, val > 0.5 ? 1.0 : (2.0 * val));\n"
                  "        gl_FragColor = p * v_col;\n"
                  "    }\n"
                  "}\n"
                : "#if defined( GL_ES )\nprecision mediump float;\n#endif\n"
                  "uniform sampler2D u_tex0;\nuniform sampler2D u_tex1;\n"
                  "uniform sampler2D u_tex2;\nuniform sampler2D u_tex3;\n"
                  "uniform sampler2D u_tex4;\nuniform sampler2D u_tex5;\n"
                  "uniform sampler2D u_tex6;\nuniform sampler2D u_tex7;\n"
                  "varying vec4 v_uvsm;\nvarying vec4 v_col;\nvarying vec4 v_tc1;\n"
                  "void main() {\n"
                  "    float s = v_uvsm.z;\n"
                  "    vec4 tex;\n"
                  "    if (s < 0.5)      tex = texture2D(u_tex0, v_uvsm.xy);\n"
                  "    else if (s < 1.5) tex = texture2D(u_tex1, v_uvsm.xy);\n"
                  "    else if (s < 2.5) tex = texture2D(u_tex2, v_uvsm.xy);\n"
                  "    else if (s < 3.5) tex = texture2D(u_tex3, v_uvsm.xy);\n"
                  "    else if (s < 4.5) tex = texture2D(u_tex4, v_uvsm.xy);\n"
                  "    else if (s < 5.5) tex = texture2D(u_tex5, v_uvsm.xy);\n"
                  "    else if (s < 6.5) tex = texture2D(u_tex6, v_uvsm.xy);\n"
                  "    else              tex = texture2D(u_tex7, v_uvsm.xy);\n"
                  "    float m = v_uvsm.w;\n"
                  "    if (m < 0.5) {\n"
                  "        gl_FragColor = tex * v_col;\n"
                  "    } else if (m < 1.5) {\n"
                  "        float g = dot(tex, v_tc1);\n"
                  "        gl_FragColor = vec4(g, g, g, g) * v_col;\n"
                  "    } else {\n"
                  "        float val = dot(tex, v_tc1);\n"
                  "        float glyph = val > 0.5 ? (2.0 * val - 1.0) : 0.0;\n"
                  "        vec4 p = vec4(glyph, glyph, glyph, val > 0.5 ? 1.0 : (2.0 * val));\n"
                  "        gl_FragColor = p * v_col;\n"
                  "    }\n"
                  "}\n";

            auto compile = [&](uint32_t type, const char* src) -> uint32_t {
                uint32_t sh = ops_.glCreateShader(type);
                if (sh == 0u) return 0u;
                ops_.glShaderSource(sh, 1, &src, nullptr);
                ops_.glCompileShader(sh);
                int32_t ok = 0;
                ops_.glGetShaderiv(sh, kGlCompileStatus, &ok);
                if (!ok) {
                    ops_.glDeleteShader(sh);
                    return 0u;
                }
                return sh;
            };

            const uint32_t vs = compile(kGlVertexShader, kUberVs);
            const uint32_t fs = vs ? compile(kGlFragmentShader, kUberFs) : 0u;
            if (!vs || !fs) {
                if (vs) ops_.glDeleteShader(vs);
                if (fs) ops_.glDeleteShader(fs);
                return failUber();
            }

            const uint32_t prog = ops_.glCreateProgram();
            if (prog == 0u) {
                ops_.glDeleteShader(vs);
                ops_.glDeleteShader(fs);
                return failUber();
            }
            ops_.glAttachShader(prog, vs);
            ops_.glAttachShader(prog, fs);
            ops_.glBindAttribLocation(prog, 0u, "HS_UI_CLIP");
            ops_.glBindAttribLocation(prog, 1u, "HS_UI_UVSM");
            ops_.glBindAttribLocation(prog, 2u, "HS_UI_COL");
            ops_.glBindAttribLocation(prog, 3u, "HS_UI_TC1");
            ops_.glLinkProgram(prog);
            ops_.glDeleteShader(vs);
            ops_.glDeleteShader(fs);

            int32_t linked = 0;
            ops_.glGetProgramiv(prog, kGlLinkStatus, &linked);
            if (!linked) {
                ops_.glDeleteProgram(prog);
                return failUber();
            }

            ops_.glUseProgram(prog);
            for (uint32_t s = 0; s < kMaxUiTexSlots; ++s) {
                char nm[16];
                snprintf(nm, sizeof(nm), "u_tex%u", s);
                const int32_t loc = ops_.glGetUniformLocation(prog, nm);
                if (loc >= 0) {
                    ops_.glUniform1i(loc, static_cast<int32_t>(kUiTexUnitBase + s));
                }
            }
            ops_.glUseProgram(dstProgram_);
            dstProgDirty_ = false;
            uberProg_ = prog;
        }
        return true;
    }

    UiGlOps                      ops_{};
    bool                         enabled_         = false;
    bool                         gpuReady_        = false;
    bool                         gpuInitFailed_   = false;
    uint32_t                     uberProg_        = 0;
    bool                         uberFailed_      = false;
    uint32_t                     maxTexSlots_     = kMaxUiTexSlots;

    uint32_t                     streamVbo_       = 0;
    uint32_t                     streamIbo_       = 0;
    size_t                       streamVboCursor_ = 0;
    size_t                       streamIboCursor_ = 0;
    bool                         streamVboShrunk_ = false;
    bool                         streamIboShrunk_ = false;

    std::vector<UiBatchedVertex> verts_;
    std::vector<uint16_t>        indices_;
    uint32_t                     vertCount_       = 0;
    uint32_t                     indexCount_      = 0;
    uint32_t                     batchTexIds_[kMaxUiTexSlots]{};
    uint32_t                     batchTexCount_   = 0;

    uint32_t                     dstProgram_      = 0;
    UiProgInfo*                  curProg_         = nullptr;
    bool                         dstProgDirty_    = false;

    uint32_t                     activeTexUnit_   = 0;
    uint32_t                     boundTex_[16]{};
    uint32_t                     boundArrayBuf_   = 0;
    uint32_t                     boundElemBuf_    = 0;
    uint16_t                     enabledAttrMask_ = 0;
    hs_sprite_batch::AttribState attribs_[16]{};
    hs_sprite_batch::ShadowBufferCache shadowCache_;

    uint32_t                     lastRejectCode_  = 0;
    UiBatchStats                 stats_{};
    UiBatchStats                 lastFlushed_{};
};

} // namespace hs_ui_batch
