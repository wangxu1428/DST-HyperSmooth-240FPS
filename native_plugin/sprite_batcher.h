#pragma once
// =============================================================================
// HyperSmooth Multi-Texture 2D Sprite Draw-Call Batcher (Pure GLES2 IAT Layer)
// =============================================================================
// Coalesces consecutive world `anim.ksh` 2D entity draw calls (trees, grass,
// chests, walls, bees, bosses, players — even when interleaved in strict Z
// order!) into a single multi-texture ring-buffered `glDrawElements` call:
//   - Pre-transforms `world_pos = MatrixW * vec4(POS2D_UV.xy, 0, 1)` on CPU
//     (6 mul + 6 add per vertex, zero projective distortion).
//   - Maps up to 8 distinct entity atlas textures simultaneously across unused
//     DST texture units GL_TEXTURE6 .. GL_TEXTURE13 (`HS_BATCH_TEX0..7`).
//   - Preserves 100% strict back-to-front painter's algorithm Z-order, alpha
//     blending, lighting, ocean tint, COLOUR_XFORM, and HD-2D Contact AO.
//   - Works on BOTH Native OpenGL (zero-stall glMapBufferRange) AND ANGLE D3D9
//     (driver buffer orphaning), with zero extra binary AOBs.
//   - 100% fail-closed: any unsupported shader/uniform/state/buffer cleanly
//     flushes pending sprites and passes straight through to vanilla GL!
// =============================================================================

#include <windows.h>
#include <emmintrin.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace hs_sprite_batch {

inline constexpr uint32_t kGlTriangles              = 0x0004u;
inline constexpr uint32_t kGlTriangleStrip          = 0x0005u;
inline constexpr uint32_t kGlUnsignedShort          = 0x1403u;
inline constexpr uint32_t kGlFloat                  = 0x1406u;
inline constexpr uint32_t kGlTexture2D              = 0x0DE1u;
inline constexpr uint32_t kGlTexture0               = 0x84C0u;
inline constexpr uint32_t kGlArrayBuffer            = 0x8892u;
inline constexpr uint32_t kGlElementArrayBuffer     = 0x8893u;
inline constexpr uint32_t kGlShaderStorageBuffer    = 0x90D2u;
inline constexpr uint32_t kGlBufferSize             = 0x8764u;
inline constexpr uint32_t kGpuInstBinding           = 0u;
inline constexpr uint32_t kGlStreamDraw             = 0x88E0u;
inline constexpr uint32_t kGlMaxTextureImageUnits   = 0x8872u;
inline constexpr uint32_t kMapReadBit               = 0x0001u;
inline constexpr uint32_t kMapWriteBit              = 0x0002u;
inline constexpr uint32_t kMapInvalidateRangeBit    = 0x0004u;
inline constexpr uint32_t kMapInvalidateBufferBit   = 0x0008u;
inline constexpr uint32_t kMapUnsynchronizedBit     = 0x0020u;
inline constexpr uint32_t kMapPersistentBit         = 0x0040u;
inline constexpr uint32_t kMapCoherentBit           = 0x0080u;
inline constexpr uint32_t kGlSyncGpuCommandsComplete = 0x9117u;
inline constexpr uint32_t kGlSyncFlushCommandsBit   = 0x00000001u;
inline constexpr uint64_t kGlTimeoutIgnored         = 0xFFFFFFFFFFFFFFFFULL;
// NEVER combine GL_MAP_UNSYNCHRONIZED_BIT with offset-0 reuse: per GLES3 spec §6.3.2 it permits
// overwriting in-flight GPU VRAM when >kRingSlots flushes occur in one frame! Pure WRITE|INVALIDATE_BUFFER
// triggers lockless driver page orphaning with 0ms stall and 0% CPU/GPU data race.
inline constexpr uint32_t kMapWriteInvalidateBuffer = kMapWriteBit | kMapInvalidateBufferBit;
inline constexpr uint32_t kMapWriteInvalidateUnsync = kMapWriteInvalidateBuffer;
inline constexpr uint32_t kMapWritePersistentCoherent = kMapWriteBit | kMapPersistentBit | kMapCoherentBit;

inline constexpr uint32_t kBatchTexUnitBase         = 6u;     // Slots 0..7 -> GL_TEXTURE6..13; Slots 8..15 -> GL_TEXTURE16..23 (skips 14..15 Shadow/BlueNoise!)
inline constexpr uint32_t kMaxBatchTexSlots         = 16u;
inline constexpr uint32_t kLegacyBatchTexSlots      = 8u;
inline constexpr uint32_t BatchSlotToTexUnit(uint32_t slot) noexcept {
    return (slot < 8u) ? (kBatchTexUnitBase + slot) : (16u + (slot - 8u));
}
inline constexpr uint32_t kMaxBatchVerts            = 32768u; // CPU/ANGLE 64B BatchedVertex limit (2 MiB/slot fallback ring)
inline constexpr uint32_t kMaxGpuBatchVerts         = 65520u; // GPU-Driven 20B GpuBatchVertex limit (uint16_t safe <= 65534, avoids 0xFFFF primitive restart & fits <= kAzdoFrameMaxVerts)
inline constexpr uint32_t kMaxBatchIndices          = 131072u;// uint16_t index values <= 65519 (<= 43690 tris, safe on D3D9 & Native GL)
inline constexpr uint32_t kMaxGpuBatchInsts         = 8192u;  // max GPU-Driven instances per flush (also tight-VRAM SSBO floor)
inline constexpr uint32_t kRingSlots                = 8u;
inline constexpr uint32_t kAzdoFramesInFlight       = 3u;     // Triple-buffered frame-level fences (0 intra-frame stalls!)
inline constexpr uint32_t kAzdoFrameMaxVerts        = 65536u; // 4 MB/frame linear bump arena (12 MB total VBO)
inline constexpr uint32_t kAzdoFrameMaxIndices      = 131072u;// 256 KB/frame default linear bump IBO
inline constexpr uint32_t kMaxSingleDrawVerts       = 4096u;
inline constexpr uint32_t kMaxSingleDrawIndices     = 8192u;
inline constexpr size_t   kMaxShadowBufBytes        = 4u * 1024u * 1024u;  // 4 MB max per cached static anim VBO/IBO
inline constexpr size_t   kMaxShadowCacheBytes      = 48u * 1024u * 1024u; // 48 MB hard cap (only visible anim.ksh VBOs cached!)

struct BatchedVertex {
    float x, y, u, v;       // +0x00..+0x0F -> loc 0 (POS2D_UV)
    float wx, wy, wz, slot; // +0x10..+0x1F -> loc 7 (HS_BATCH_WPOS: world_pos.xyz, texSlot 0..7)
    float mr, mg, mb, ma;   // +0x20..+0x2F -> loc 5 (HS_BATCH_CMULT: diag of affine COLOUR_XFORM)
    float ar, ag, ab, pad;  // +0x30..+0x3F -> loc 6 (HS_BATCH_CADD: alpha-premul add of COLOUR_XFORM)
};
static_assert(sizeof(BatchedVertex) == 64, "BatchedVertex must be 64 bytes (1 cache line)");

struct alignas(16) GpuSpriteInstance {
    float    m0[4];    // +0x00: (m[0], m[1], m[2], anchorY=m[13])
    float    m1[4];    // +0x10: (m[4], m[5], m[6], lastOceanBlend[0])
    float    m2[4];    // +0x20: (m[12], m[13], m[14], exactInt: b0=fpClip, b1=bloom, b2=useInstParams, b3..19=alphaTestUnorm16, b20=fpBob)
    float    cmult[4]; // +0x30: curCMult_[0..3]
    float    cadd[4];  // +0x40: (curCAdd_[0..2], shadowFlag)
    float    meta[4];  // +0x50: (pax, pay, paz, swayAxisX)
    float    meta2[4]; // +0x60: (foliageSwayBase, foliageWakeBase, foliageClassF, swayAxisZ)
    uint32_t tex0[2];  // +0x70: 64-bit ARB bindless handle for SAMPLER[0]
    uint32_t tex1[2];  // +0x78: 64-bit ARB bindless handle for SAMPLER[1]
    uint32_t tex2[2];  // +0x80: 64-bit ARB bindless handle for SAMPLER[5]
    uint32_t flags[2]; // +0x88: flags[0]=floatBits(lastFloatParams[0]), flags[1]=floatBits(PARAMS.y LIGHT_OVERRIDE)
};
static_assert(sizeof(GpuSpriteInstance) == 144, "GpuSpriteInstance must be 144 bytes std430");

// Packs (fpClip, bloomToggle, useInstParams, ALPHA_TEST, fpBob) into a 21-bit exact IEEE-754 integer float (< 2^24)
// so world_shadow_engine.h / water_reflection_engine.h keep identical 144-byte std430 SSBO layout while
// PARAMS.y (LIGHT_OVERRIDE) keeps full bit-exact FP32 precision in flags[1]!
inline float PackM2WParams(bool fpClip, bool bloomToggle, bool useInstParams, float alphaTest,
                           bool fpBob = false) noexcept {
    uint32_t bits = (fpClip ? 1u : 0u) |
                    (bloomToggle ? 2u : 0u) |
                    (useInstParams ? 4u : 0u) |
                    (fpBob ? (1u << 20u) : 0u);
    if (useInstParams && alphaTest > 0.0f) {
        uint32_t at16 = 0u;
        if (alphaTest > 1.0f) {
            at16 = 65536u; // 65536/65535 > 1.0 -> discards all [0,1] alpha
        } else {
            at16 = static_cast<uint32_t>(alphaTest * 65535.0f + 0.5f);
            if (at16 == 0u) at16 = 1u; // Preserve strict (ALPHA_TEST > 0.0) for tiny positive thresholds
            if (at16 > 65535u) at16 = 65535u;
        }
        bits |= (at16 << 3u);
    }
    return static_cast<float>(bits);
}

struct GpuBatchVertex {
    float x, y, u, v; // +0x00..+0x0F -> loc 0 (POS2D_UV)
    float instId;     // +0x10..+0x13 -> loc 7 (HS_BATCH_WPOS.x = global SSBO index)
};
static_assert(sizeof(GpuBatchVertex) == 20, "GpuBatchVertex must be 20 bytes");

struct AttribState {
    uint32_t    buffer     = 0;
    int32_t     size       = 0;
    uint32_t    type       = 0;
    uint8_t     normalized = 0;
    int32_t     stride     = 0;
    const void* pointer    = nullptr;
    bool        valid      = false;
};

struct BatchProgInfo {
    void*    ctx                 = nullptr;
    bool     checked             = false;
    bool     eligible            = false;
    bool     hasGpuDriven        = false;
    bool     isBloomPass         = false;
    bool     samplersInit        = false;
    bool     floatParamsDirty    = false;
    bool     hasFloatParamsValue = false; // True once FLOAT_PARAMS observed via TryAbsorbFloatParams or OnUniform3fv
    bool     oceanBlendDirty     = false;
    bool     paramsDirty         = false;
    bool     hasParamsAbsorption = false; // Link-time capability: hasGpuDriven && !isBloomPass && locParams >= 0
    bool     hasParamsValue      = false; // True once PARAMS observed via TryAbsorbParams or OnUniform3fv
    int32_t  locBatchActive      = -1;
    int32_t  locMatrixW          = -1;
    int32_t  locColourXform      = -1;
    int32_t  locFloatParams      = -1;
    int32_t  locParams           = -1;
    int32_t  locOceanBlend       = -1;
    int32_t  locPos2dUv          = -1;
    int32_t  locBatchWpos        = -1;
    int32_t  locBatchCMult       = -1;
    int32_t  locBatchCAdd        = -1;
    int32_t  locBatchTex[kMaxBatchTexSlots] = {
        -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1
    };
    uint32_t progMaxTexSlots     = kLegacyBatchTexSlots;
    int32_t  locSampler0         = -1;
    int32_t  locSampler1         = -1;
    int32_t  locSampler5         = -1;
    int32_t  samplerUnit0        = 0;
    int32_t  samplerUnit1        = 1;
    int32_t  samplerUnit5        = 5;
    float    lastFloatParamsZ    = 0.0f;
    float    lastParamsY         = 0.0f;
    float    lastFloatParams[3]  = { 0.0f, 0.0f, 0.0f };
    float    lastParams[3]       = { 0.0f, 0.0f, 0.0f };
    float    lastOceanBlend[4]   = { 0.0f, 0.0f, 0.0f, 0.0f };
};

struct GlBatchOps {
    using GenBuffers_t               = void (WINAPI*)(int32_t n, uint32_t* buffers);
    using DeleteBuffers_t            = void (WINAPI*)(int32_t n, const uint32_t* buffers);
    using BindBuffer_t               = void (WINAPI*)(uint32_t target, uint32_t buffer);
    using BufferData_t               = void (WINAPI*)(uint32_t target, ptrdiff_t size, const void* data, uint32_t usage);
    using BufferSubData_t            = void (WINAPI*)(uint32_t target, ptrdiff_t offset, ptrdiff_t size, const void* data);
    using MapBufferRange_t           = void* (WINAPI*)(uint32_t target, ptrdiff_t offset, ptrdiff_t length, uint32_t access);
    using UnmapBuffer_t              = uint8_t (WINAPI*)(uint32_t target);
    using ActiveTexture_t            = void (WINAPI*)(uint32_t texture);
    using BindTexture_t              = void (WINAPI*)(uint32_t target, uint32_t texture);
    using Uniform1f_t                = void (WINAPI*)(int32_t location, float x);
    using Uniform1i_t                = void (WINAPI*)(int32_t location, int32_t x);
    using Uniform3fv_t               = void (WINAPI*)(int32_t location, int32_t count, const float* value);
    using Uniform4fv_t               = void (WINAPI*)(int32_t location, int32_t count, const float* value);
    using UniformMatrix4fv_t         = void (WINAPI*)(int32_t location, int32_t count, uint8_t transpose, const float* value);
    using VertexAttribPointer_t      = void (WINAPI*)(uint32_t index, int32_t size, uint32_t type, uint8_t normalized, int32_t stride, const void* ptr);
    using EnableVertexAttribArray_t  = void (WINAPI*)(uint32_t index);
    using DisableVertexAttribArray_t = void (WINAPI*)(uint32_t index);
    using DrawElements_t             = void (WINAPI*)(uint32_t mode, int32_t count, uint32_t type, const void* indices);
    using DrawElementsBaseVertex_t   = void (WINAPI*)(uint32_t mode, int32_t count, uint32_t type, const void* indices, int32_t basevertex);
    using GetIntegerv_t              = void (WINAPI*)(uint32_t pname, int32_t* params);
    using GetBufferParameteriv_t     = void (WINAPI*)(uint32_t target, uint32_t pname, int32_t* params);
    using GetBufferSubData_t         = void (WINAPI*)(uint32_t target, ptrdiff_t offset, ptrdiff_t size, void* data);
    using SyncStateAfterFlush_t      = void (*)(uint32_t activeUnit, uint32_t arrayBuf, uint32_t elemBuf, uint32_t batchWposLoc, void* ctx);
    using SyncStickyFlush_t          = void (*)(bool touchedTexUnits, bool enteredSticky, uint32_t batchWposLoc, void* ctx);
    using ShadowMirrorBatch_t        = void (*)(uint32_t indexCount, void* ctx);
    using BufferStorage_t            = void (WINAPI*)(uint32_t target, ptrdiff_t size, const void* data, uint32_t flags);
    using FenceSync_t                      = void* (WINAPI*)(uint32_t condition, uint32_t flags);
    using ClientWaitSync_t                 = uint32_t (WINAPI*)(void* sync, uint32_t flags, uint64_t timeout);
    using DeleteSync_t                     = void (WINAPI*)(void* sync);
    using BindBufferBase_t                 = void (WINAPI*)(uint32_t target, uint32_t index, uint32_t buffer);
    using GetTextureHandleArb_t            = uint64_t (WINAPI*)(uint32_t texture);
    using MakeTextureHandleResidentArb_t   = void (WINAPI*)(uint64_t handle);
    using MakeTextureHandleNonResidentArb_t = void (WINAPI*)(uint64_t handle);
    using NowQpc_t                         = int64_t (*)();

    GenBuffers_t               glGenBuffers               = nullptr;
    DeleteBuffers_t            glDeleteBuffers            = nullptr;
    BindBuffer_t               glBindBuffer               = nullptr;
    BufferData_t               glBufferData               = nullptr;
    BufferSubData_t            glBufferSubData            = nullptr;
    MapBufferRange_t           glMapBufferRange           = nullptr;
    UnmapBuffer_t              glUnmapBuffer              = nullptr;
    ActiveTexture_t            glActiveTexture            = nullptr;
    BindTexture_t              glBindTexture              = nullptr;
    Uniform1f_t                glUniform1f                = nullptr;
    Uniform1i_t                glUniform1i                = nullptr;
    Uniform3fv_t               glUniform3fv               = nullptr;
    Uniform4fv_t               glUniform4fv               = nullptr;
    UniformMatrix4fv_t         glUniformMatrix4fv         = nullptr;
    VertexAttribPointer_t      glVertexAttribPointer      = nullptr;
    EnableVertexAttribArray_t  glEnableVertexAttribArray  = nullptr;
    DisableVertexAttribArray_t glDisableVertexAttribArray = nullptr;
    DrawElements_t             glDrawElements             = nullptr;
    DrawElementsBaseVertex_t   glDrawElementsBaseVertex   = nullptr;
    GetIntegerv_t              glGetIntegerv              = nullptr;
    GetBufferParameteriv_t     glGetBufferParameteriv     = nullptr;
    GetBufferSubData_t         glGetBufferSubData         = nullptr;
    SyncStateAfterFlush_t      onPostFlushRestore         = nullptr;
    SyncStickyFlush_t          onStickyFlushSync          = nullptr;
    ShadowMirrorBatch_t        onShadowMirrorBatch        = nullptr;
    void*                      callbackCtx                = nullptr;
    BufferStorage_t                   glBufferStorage                  = nullptr;
    FenceSync_t                       glFenceSync                      = nullptr;
    ClientWaitSync_t                  glClientWaitSync                 = nullptr;
    DeleteSync_t                      glDeleteSync                     = nullptr;
    BindBufferBase_t                  glBindBufferBase                 = nullptr;
    GetTextureHandleArb_t             glGetTextureHandleARB            = nullptr;
    MakeTextureHandleResidentArb_t    glMakeTextureHandleResidentARB   = nullptr;
    MakeTextureHandleNonResidentArb_t glMakeTextureHandleNonResidentARB = nullptr;
    NowQpc_t                          nowQpc                           = nullptr;
    bool                              preferOrphanSubData              = false;
};

enum class FlushReason : uint8_t {
    Other = 0,
    Prog,
    Uni4,
    Uni3,
    UniMat,
    UniOther,
    State,
    Tex,
    Full,
    End,
    Bypass,
    Count
};

struct Stats {
    int64_t drawsIn        = 0; // Total eligible anim draw calls entering batcher
    int64_t drawsBatched   = 0; // Draw calls coalesced into batches
    int64_t flushes        = 0; // Actual GPU glDrawElements issued by batcher
    int64_t vertsBatched   = 0;
    int64_t bypassDraws    = 0; // Draws falling back to vanilla path
    int64_t shadowHits     = 0;
    int64_t shadowMisses   = 0;
    int64_t flReason[static_cast<size_t>(FlushReason::Count)]{};
};

struct FrameProfile {
    int64_t tryResolveQpc  = 0; // ResolveVertexPtr + ResolveIndexPtr (incl. FetchFromGpu)
    int64_t tryInFlushQpc  = 0; // Flush(...) invoked inside TryBatch* (Other/Full/Tex)
    int64_t flushWaitQpc   = 0; // glClientWaitSync inside Flush
    int64_t flushUploadQpc = 0; // Persistent memcpy or UploadRingSlice inside Flush
    int64_t flushDrawQpc   = 0; // GL state setup + glDrawElements* + shadow mirror + restore
};

struct DiagCounters {
    uint64_t gpuModeSwitches  = 0;
    uint64_t gpuLatchFrames   = 0;
    uint64_t gpuDenyHits      = 0;
    uint64_t gpuDenyMutated   = 0;
    uint64_t gpuDenyNullAlloc = 0;
    uint64_t gpuDenyHandleZero = 0;
    uint64_t gpuRingOverflow  = 0;
    uint64_t cpuFallbackRing  = 0;
    uint32_t peakFrameVerts   = 0;
    uint32_t peakFrameIndices = 0;
    uint32_t peakFrameInsts   = 0;
};

// =============================================================================
// Shader Source Transformer (strictly whitelisted to vanilla world anim.ksh)
// Composes cleanly with HD-2D Contact AO transform in either order!
// =============================================================================
inline size_t SkipExtensionLines(const std::string& src, size_t off) noexcept {
    while (off < src.size()) {
        size_t p = off;
        while (p < src.size() && (src[p] == ' ' || src[p] == '\t' || src[p] == '\r' || src[p] == '\n')) ++p;
        if (p + 10 <= src.size() && src.compare(p, 10, "#extension") == 0) {
            size_t nl = src.find('\n', p);
            off = (nl != std::string::npos) ? (nl + 1) : src.size();
        } else {
            break;
        }
    }
    return off;
}

inline size_t FindDeclInsertOffset(const std::string& src) {
    // Match dinput8.cpp FindPrecisionEndOffset: if `precision ...;` is wrapped inside
    // `#if defined( GL_ES ) ... #endif`, insert AFTER `#endif` (and after any #extension lines)!
    size_t pos = src.find("precision ");
    if (pos != std::string::npos) {
        size_t semi = src.find(';', pos);
        if (semi != std::string::npos) {
            size_t ifPos = src.rfind("#if", pos);
            size_t prevEndif = src.rfind("#endif", pos);
            if (ifPos != std::string::npos && (prevEndif == std::string::npos || prevEndif < ifPos)) {
                size_t endifPos = src.find("#endif", semi);
                if (endifPos != std::string::npos) {
                    size_t nl = src.find('\n', endifPos);
                    return SkipExtensionLines(src, (nl != std::string::npos) ? (nl + 1) : (endifPos + 6));
                }
            }
            size_t nl = src.find('\n', semi);
            return SkipExtensionLines(src, (nl != std::string::npos) ? (nl + 1) : (semi + 1));
        }
    }
    size_t ver = src.find("#version");
    if (ver != std::string::npos) {
        size_t nl = src.find('\n', ver);
        return SkipExtensionLines(src, (nl != std::string::npos) ? (nl + 1) : 0);
    }
    return 0;
}

inline bool HasDefineLine(const std::string& src, const char* macro) {
    std::string p1 = std::string("#define ") + macro + "\r";
    std::string p2 = std::string("#define ") + macro + "\n";
    std::string p3 = std::string("#define ") + macro + " ";
    return src.find(p1) != std::string::npos ||
           src.find(p2) != std::string::npos ||
           src.find(p3) != std::string::npos;
}

inline bool TransformAnimBatchShaderSource(const std::string& src, std::string& outSrc, const char*& outTag,
                                           bool allow16Slots = false, bool allowGpuDriven = false) {
    if (src.find("HS_BATCH_ACTIVE") != std::string::npos) {
        return false; // already transformed
    }
    if (HasDefineLine(src, "SKINNED") || HasDefineLine(src, "FADE_OUT") ||
        HasDefineLine(src, "HAUNT") || HasDefineLine(src, "HOLO") ||
        HasDefineLine(src, "UI_HOLO") || HasDefineLine(src, "UI_CC") ||
        HasDefineLine(src, "BLOOM")) {
        return false;
    }

    static constexpr const char* kGpuVersionHeader =
        "#version 430 compatibility\n"
        "#extension GL_ARB_bindless_texture : require\n";

    static constexpr const char* kGpuInstSwayBody =
        "\t\tint hs_iid = int(HS_BATCH_WPOS.x + 0.5);\n"
        "\t\tHsGpuInst hs_ins = hs_gpu_insts[hs_iid];\n"
        "\t\tvec2 hs_p = POS2D_UV.xy;\n"
        "\t\tfloat hs_dx = hs_ins.m0.x * hs_p.x + hs_ins.m1.x * hs_p.y;\n"
        "\t\tfloat hs_dy = hs_ins.m0.y * hs_p.x + hs_ins.m1.y * hs_p.y;\n"
        "\t\tfloat hs_dz = hs_ins.m0.z * hs_p.x + hs_ins.m1.z * hs_p.y;\n"
        "\t\tfloat hs_wx = hs_dx + hs_ins.m2.x;\n"
        "\t\tfloat hs_wy = hs_dy + hs_ins.m2.y;\n"
        "\t\tfloat hs_wz = hs_dz + hs_ins.m2.z;\n"
        "\t\tfloat hs_ho = 0.0;\n"
        "\t\tif (hs_ins.meta2.z > 0.5 && hs_dy > 0.06) {\n"
        "\t\t\tif (hs_ins.meta2.z < 1.5) {\n"
        "\t\t\t\ths_ho = hs_ins.meta2.x * smoothstep(0.08, 1.35, hs_dy)\n"
        "\t\t\t\t      + hs_ins.meta2.y * smoothstep(0.06, 1.10, hs_dy);\n"
        "\t\t\t} else {\n"
        "\t\t\t\tfloat hs_b2 = smoothstep(1.00, 3.80, hs_dy);\n"
        "\t\t\t\tfloat hs_hc = min(hs_dy, 8.0);\n"
        "\t\t\t\ths_ho = hs_ins.meta2.x * (hs_b2 * hs_b2) * (0.18 + 0.025 * hs_hc);\n"
        "\t\t\t}\n"
        "\t\t\ths_wx += hs_ins.meta.w * hs_ho;\n"
        "\t\t\ths_wz += hs_ins.meta2.w * hs_ho;\n"
        "\t\t}\n"
        "\t\tvec4 world_pos_tmp = vec4(hs_wx, hs_wy, hs_wz, 1.0);\n"
        "\t\tfloat hs_si = floor(POS2D_UV.z * 0.5);\n"
        "\t\tTEXCOORD0 = vec3(POS2D_UV.z - 2.0 * hs_si, POS2D_UV.w, 0.0);\n"
        "\t\tHS_V_BTEX = (hs_si < 0.5) ? hs_ins.tex0 : ((hs_si < 1.5) ? hs_ins.tex1 : hs_ins.tex2);\n";

    // 1. Vanilla world anim.ksh VERTEX SHADER
    if (src.find("attribute vec4 POS2D_UV;") != std::string::npos &&
        src.find("uniform mat4 MatrixP;") != std::string::npos &&
        src.find("uniform mat4 MatrixV;") != std::string::npos &&
        src.find("uniform mat4 MatrixW;") != std::string::npos &&
        src.find("PS_TEXCOORD = TEXCOORD0;") != std::string::npos &&
        src.find("PS_POS = world_pos.xyz;") != std::string::npos) {
        const size_t posWorld = src.find("vec4 world_pos = MatrixW * vec4( object_pos, 1.0 );");
        const size_t posElse  = (posWorld != std::string::npos) ? src.rfind("#else", posWorld) : std::string::npos;
        const size_t posEndif = (posWorld != std::string::npos) ? src.find("#endif", posWorld) : std::string::npos;
        if (posWorld != std::string::npos && posElse != std::string::npos && posEndif != std::string::npos) {
            std::string s = src;
            if (allowGpuDriven) {
                const std::string gpuVsBody =
                    std::string("#else\n"
                                "\tvec4 world_pos;\n"
                                "\tvec3 TEXCOORD0;\n"
                                "\tfloat hs_gpu_localX = 0.0;\n"
                                "\tif (HS_BATCH_ACTIVE > 1.5) {\n") +
                    kGpuInstSwayBody +
                    "\t\tworld_pos = world_pos_tmp;\n"
                    "\t\tHS_V_CMULT = hs_ins.cmult;\n"
                    "\t\tHS_V_CADD  = hs_ins.cadd.rgb;\n"
                    "\t\tuint  hs_m2u   = uint(hs_ins.m2.w + 0.5);\n"
                    "\t\tif ((hs_m2u & 0x100000u) != 0u) {\n"
                    "\t\t\tworld_pos.y += sin(hs_ins.m2.x + hs_ins.m2.z + TIMEPARAMS.x * 3.0) * 0.025;\n"
                    "\t\t}\n"
                    "\t\tfloat hs_fpEn  = float(hs_m2u & 1u);\n"
                    "\t\tfloat hs_bloom = float((hs_m2u >> 1u) & 1u);\n"
                    "\t\tfloat hs_useP  = float((hs_m2u >> 2u) & 1u);\n"
                    "\t\tfloat hs_at    = float((hs_m2u >> 3u) & 0x1FFFFu) * (1.0 / 65535.0);\n"
                    "\t\tHS_V_FOB       = vec3(uintBitsToFloat(hs_ins.flags.x), hs_fpEn, hs_ins.m1.w);\n"
                    "\t\tHS_V_PARAMS    = vec4(hs_at, uintBitsToFloat(hs_ins.flags.y), hs_bloom, hs_useP);\n"
                    "\t\ths_gpu_localX  = dot(vec3(hs_dx, hs_dy, hs_dz), hs_ins.meta.xyz) + hs_ho;\n"
                    "\t} else if (HS_BATCH_ACTIVE > 0.5) {\n"
                    "\t\tfloat hs_si = floor(POS2D_UV.z / 2.0);\n"
                    "\t\tTEXCOORD0 = vec3(POS2D_UV.z - 2.0 * hs_si, POS2D_UV.w, HS_BATCH_WPOS.w);\n"
                    "\t\tworld_pos = vec4(HS_BATCH_WPOS.xyz, 1.0);\n"
                    "\t\tHS_V_CMULT  = HS_BATCH_CMULT;\n"
                    "\t\tHS_V_CADD   = HS_BATCH_CADD.rgb;\n"
                    "\t\tHS_V_FOB    = vec3(0.0);\n"
                    "\t\tHS_V_PARAMS = vec4(0.0);\n"
                    "\t\tHS_V_BTEX   = uvec2(0u, 0u);\n"
                    "\t} else {\n"
                    "\t\tvec3 POSITION = vec3(POS2D_UV.xy, 0.0);\n"
                    "\t\tfloat samplerIndex = floor(POS2D_UV.z / 2.0);\n"
                    "\t\tTEXCOORD0 = vec3(POS2D_UV.z - 2.0 * samplerIndex, POS2D_UV.w, samplerIndex);\n"
                    "\t\tvec3 object_pos = POSITION.xyz;\n"
                    "\t\tworld_pos = MatrixW * vec4(object_pos, 1.0);\n"
                    "\t\tif (FLOAT_PARAMS.z > 0.0) {\n"
                    "\t\t\tfloat world_x = MatrixW[3][0];\n"
                    "\t\t\tfloat world_z = MatrixW[3][2];\n"
                    "\t\t\tworld_pos.y += sin(world_x + world_z + TIMEPARAMS.x * 3.0) * 0.025;\n"
                    "\t\t}\n"
                    "\t\tHS_V_CMULT  = vec4(1.0);\n"
                    "\t\tHS_V_CADD   = vec3(0.0);\n"
                    "\t\tHS_V_FOB    = vec3(0.0);\n"
                    "\t\tHS_V_PARAMS = vec4(0.0);\n"
                    "\t\tHS_V_BTEX   = uvec2(0u, 0u);\n"
                    "\t}\n"
                    "\tmat4 mtxPV = MatrixP * MatrixV;\n"
                    "\tgl_Position = mtxPV * world_pos;\n";
                s.replace(posElse, posEndif - posElse, gpuVsBody);
            } else {
                static constexpr const char* kBatchVsBody =
                    "#else\n"
                    "\tvec4 world_pos;\n"
                    "\tvec3 TEXCOORD0;\n"
                    "\tif (HS_BATCH_ACTIVE > 0.5) {\n"
                    "\t\tfloat hs_si = floor(POS2D_UV.z / 2.0);\n"
                    "\t\tTEXCOORD0 = vec3(POS2D_UV.z - 2.0 * hs_si, POS2D_UV.w, HS_BATCH_WPOS.w);\n"
                    "\t\tworld_pos = vec4(HS_BATCH_WPOS.xyz, 1.0);\n"
                    "\t\tHS_V_CMULT = HS_BATCH_CMULT;\n"
                    "\t\tHS_V_CADD  = HS_BATCH_CADD.rgb;\n"
                    "\t} else {\n"
                    "\t\tvec3 POSITION = vec3(POS2D_UV.xy, 0.0);\n"
                    "\t\tfloat samplerIndex = floor(POS2D_UV.z / 2.0);\n"
                    "\t\tTEXCOORD0 = vec3(POS2D_UV.z - 2.0 * samplerIndex, POS2D_UV.w, samplerIndex);\n"
                    "\t\tvec3 object_pos = POSITION.xyz;\n"
                    "\t\tworld_pos = MatrixW * vec4(object_pos, 1.0);\n"
                    "\t\tif (FLOAT_PARAMS.z > 0.0) {\n"
                    "\t\t\tfloat world_x = MatrixW[3][0];\n"
                    "\t\t\tfloat world_z = MatrixW[3][2];\n"
                    "\t\t\tworld_pos.y += sin(world_x + world_z + TIMEPARAMS.x * 3.0) * 0.025;\n"
                    "\t\t}\n"
                    "\t\tHS_V_CMULT = vec4(1.0);\n"
                    "\t\tHS_V_CADD  = vec3(0.0);\n"
                    "\t}\n"
                    "\tmat4 mtxPV = MatrixP * MatrixV;\n"
                    "\tgl_Position = mtxPV * world_pos;\n";
                s.replace(posElse, posEndif - posElse, kBatchVsBody);
            }
            const size_t posAttr = s.find("attribute vec4 POS2D_UV;");
            if (posAttr != std::string::npos) {
                static constexpr const char* kGpuVsDecls =
                    "struct HsGpuInst { vec4 m0; vec4 m1; vec4 m2; vec4 cmult; vec4 cadd; vec4 meta; vec4 meta2; uvec2 tex0; uvec2 tex1; uvec2 tex2; uvec2 flags; };\n"
                    "layout(std430, binding = 0) readonly buffer HsGpuInstBuf { HsGpuInst hs_gpu_insts[]; };\n"
                    "uniform float HS_BATCH_ACTIVE;\n"
                    "attribute vec4 HS_BATCH_WPOS;\n"
                    "attribute vec4 HS_BATCH_CMULT;\n"
                    "attribute vec4 HS_BATCH_CADD;\n"
                    "varying vec4 HS_V_CMULT;\n"
                    "varying vec3 HS_V_CADD;\n"
                    "varying vec3 HS_V_FOB;\n"
                    "flat out vec4 HS_V_PARAMS;\n"
                    "flat out uvec2 HS_V_BTEX;\n";
                static constexpr const char* kCpuVsDecls =
                    "uniform float HS_BATCH_ACTIVE;\n"
                    "attribute vec4 HS_BATCH_WPOS;\n"
                    "attribute vec4 HS_BATCH_CMULT;\n"
                    "attribute vec4 HS_BATCH_CADD;\n"
                    "varying vec4 HS_V_CMULT;\n"
                    "varying vec3 HS_V_CADD;\n";
                s.insert(posAttr, allowGpuDriven ? kGpuVsDecls : kCpuVsDecls);
                if (allowGpuDriven && s.find("#version") == std::string::npos) {
                    s.insert(0, kGpuVersionHeader);
                }
                outSrc = std::move(s);
                outTag = "ANIM_BATCH_VS";
                return true;
            }
        }
    }

    auto buildWrappedSampleBlock = [&](const std::string& origSampleBlock) {
        const char* cpuSlotTree = allow16Slots
            ? "        float hs_bs = PS_TEXCOORD.z;\n"
              "        if (hs_bs < 7.5) {\n"
              "            if (hs_bs < 3.5) {\n"
              "                if (hs_bs < 1.5)      colour.rgba = (hs_bs < 0.5) ? texture2D(HS_BATCH_TEX0, PS_TEXCOORD.xy) : texture2D(HS_BATCH_TEX1, PS_TEXCOORD.xy);\n"
              "                else                  colour.rgba = (hs_bs < 2.5) ? texture2D(HS_BATCH_TEX2, PS_TEXCOORD.xy) : texture2D(HS_BATCH_TEX3, PS_TEXCOORD.xy);\n"
              "            } else {\n"
              "                if (hs_bs < 5.5)      colour.rgba = (hs_bs < 4.5) ? texture2D(HS_BATCH_TEX4, PS_TEXCOORD.xy) : texture2D(HS_BATCH_TEX5, PS_TEXCOORD.xy);\n"
              "                else                  colour.rgba = (hs_bs < 6.5) ? texture2D(HS_BATCH_TEX6, PS_TEXCOORD.xy) : texture2D(HS_BATCH_TEX7, PS_TEXCOORD.xy);\n"
              "            }\n"
              "        } else {\n"
              "            if (hs_bs < 11.5) {\n"
              "                if (hs_bs < 9.5)      colour.rgba = (hs_bs < 8.5) ? texture2D(HS_BATCH_TEX8, PS_TEXCOORD.xy) : texture2D(HS_BATCH_TEX9, PS_TEXCOORD.xy);\n"
              "                else                  colour.rgba = (hs_bs < 10.5) ? texture2D(HS_BATCH_TEX10, PS_TEXCOORD.xy) : texture2D(HS_BATCH_TEX11, PS_TEXCOORD.xy);\n"
              "            } else {\n"
              "                if (hs_bs < 13.5)     colour.rgba = (hs_bs < 12.5) ? texture2D(HS_BATCH_TEX12, PS_TEXCOORD.xy) : texture2D(HS_BATCH_TEX13, PS_TEXCOORD.xy);\n"
              "                else                  colour.rgba = (hs_bs < 14.5) ? texture2D(HS_BATCH_TEX14, PS_TEXCOORD.xy) : texture2D(HS_BATCH_TEX15, PS_TEXCOORD.xy);\n"
              "            }\n"
              "        }\n"
            : "        float hs_bs = PS_TEXCOORD.z;\n"
              "        if (hs_bs < 0.5)      colour.rgba = texture2D(HS_BATCH_TEX0, PS_TEXCOORD.xy);\n"
              "        else if (hs_bs < 1.5) colour.rgba = texture2D(HS_BATCH_TEX1, PS_TEXCOORD.xy);\n"
              "        else if (hs_bs < 2.5) colour.rgba = texture2D(HS_BATCH_TEX2, PS_TEXCOORD.xy);\n"
              "        else if (hs_bs < 3.5) colour.rgba = texture2D(HS_BATCH_TEX3, PS_TEXCOORD.xy);\n"
              "        else if (hs_bs < 4.5) colour.rgba = texture2D(HS_BATCH_TEX4, PS_TEXCOORD.xy);\n"
              "        else if (hs_bs < 5.5) colour.rgba = texture2D(HS_BATCH_TEX5, PS_TEXCOORD.xy);\n"
              "        else if (hs_bs < 6.5) colour.rgba = texture2D(HS_BATCH_TEX6, PS_TEXCOORD.xy);\n"
              "        else                  colour.rgba = texture2D(HS_BATCH_TEX7, PS_TEXCOORD.xy);\n";
        if (allowGpuDriven) {
            return std::string("if (HS_BATCH_ACTIVE > 1.5) {\n"
                               "        colour.rgba = texture2D(sampler2D(HS_V_BTEX), PS_TEXCOORD.xy);\n"
                               "    } else if (HS_BATCH_ACTIVE > 0.5) {\n") +
                   cpuSlotTree +
                   "    } else {\n" +
                   origSampleBlock +
                   "\n    }";
        }
        return std::string("if (HS_BATCH_ACTIVE > 0.5) {\n") +
               cpuSlotTree +
               "    } else {\n" +
               origSampleBlock +
               "\n    }";
    };

    auto appendBatchSamplerDecls = [&](std::string& fsDecls) {
        if (allowGpuDriven) fsDecls += "flat in uvec2 HS_V_BTEX;\n";
        const uint32_t texDeclCnt = allow16Slots ? 16u : 8u;
        for (uint32_t i = 0; i < texDeclCnt; ++i) {
            char line[48];
            snprintf(line, sizeof(line), "uniform sampler2D HS_BATCH_TEX%u;\n", i);
            fsDecls += line;
        }
    };

    // 2. Vanilla world anim.ksh FRAGMENT SHADER
    if (src.find("uniform sampler2D SAMPLER[") != std::string::npos &&
        src.find("gl_FragColor.rgba = colour.rgba * COLOUR_XFORM;") != std::string::npos &&
        src.find("OCEAN_BLEND_PARAMS") != std::string::npos &&
        src.find("CalculateLightingContribution()") != std::string::npos) {
        const size_t posVec4   = src.find("vec4 colour;");
        const size_t posTriple = (posVec4 != std::string::npos) ? src.find("#if defined( TRIPLE_ATLAS )", posVec4) : std::string::npos;
        const size_t posBloom  = (posTriple != std::string::npos) ? src.find("if (BLOOM_TOGGLE == 1.0)", posTriple) : std::string::npos;
        const size_t posEndif  = (posBloom != std::string::npos) ? src.rfind("#endif", posBloom) : std::string::npos;
        if (posTriple != std::string::npos && posEndif != std::string::npos && posEndif > posTriple) {
            std::string s = src;
            const std::string origSampleBlock = s.substr(posTriple, (posEndif + 6) - posTriple);
            s.replace(posTriple, (posEndif + 6) - posTriple, buildWrappedSampleBlock(origSampleBlock));
            static constexpr const char* kColXformOrig = "gl_FragColor.rgba = colour.rgba * COLOUR_XFORM;";
            const size_t posCol = s.find(kColXformOrig);
            if (posCol != std::string::npos) {
                s.replace(posCol, std::strlen(kColXformOrig),
                          "gl_FragColor.rgba = (HS_BATCH_ACTIVE > 0.5) ? "
                          "vec4(colour.rgb * HS_V_CMULT.rgb + colour.a * HS_V_CADD, colour.a * HS_V_CMULT.a) : "
                          "(colour.rgba * COLOUR_XFORM);");
            }
            if (allowGpuDriven) {
                size_t posFp = s.find("if(FLOAT_PARAMS.y > 0.0)");
                if (posFp == std::string::npos) posFp = s.find("if (FLOAT_PARAMS.y > 0.0)");
                const size_t posDisc = (posFp != std::string::npos) ? s.find("discard;", posFp) : std::string::npos;
                const size_t close1  = (posDisc != std::string::npos) ? s.find('}', posDisc) : std::string::npos;
                const size_t close2  = (close1 != std::string::npos) ? s.find('}', close1 + 1) : std::string::npos;
                if (close2 != std::string::npos && (close2 - posFp) < 160u) {
                    s.replace(posFp, (close2 + 1) - posFp,
                              "vec2 hs_fp = (HS_BATCH_ACTIVE > 1.5) ? HS_V_FOB.xy : FLOAT_PARAMS.xy;\n"
                              "    if (hs_fp.y > 0.0 && PS_POS.y < hs_fp.x) { discard; }");
                }
                static constexpr const char* kOceanBlendOrig =
                    "gl_FragColor.rgb = mix(gl_FragColor.rgb, gl_FragColor.rgb * world_tint.rgb, OCEAN_BLEND_PARAMS.x);";
                const size_t posOb = s.find(kOceanBlendOrig);
                if (posOb != std::string::npos) {
                    s.replace(posOb, std::strlen(kOceanBlendOrig),
                              "gl_FragColor.rgb = mix(gl_FragColor.rgb, gl_FragColor.rgb * world_tint.rgb, (HS_BATCH_ACTIVE > 1.5) ? HS_V_FOB.z : OCEAN_BLEND_PARAMS.x);");
                }
                if (s.find("uniform vec3 PARAMS;") != std::string::npos) {
                    auto replaceParamsMacro = [&](const char* macroName, const char* comp, const char* repl) -> bool {
                        size_t pos = 0;
                        const size_t mLen = std::strlen(macroName);
                        const std::string rhs = std::string("PARAMS.") + comp;
                        while ((pos = s.find("#define", pos)) != std::string::npos) {
                            size_t lineStart = s.rfind('\n', pos);
                            lineStart = (lineStart == std::string::npos) ? 0u : (lineStart + 1u);
                            bool onlyWsBefore = true;
                            for (size_t i = lineStart; i < pos; ++i) {
                                if (s[i] != ' ' && s[i] != '\t' && s[i] != '\r') { onlyWsBefore = false; break; }
                            }
                            if (onlyWsBefore) {
                                size_t p = pos + 7;
                                while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
                                if (s.compare(p, mLen, macroName) == 0) {
                                    p += mLen;
                                    if (p < s.size() && (s[p] == ' ' || s[p] == '\t')) {
                                        while (p < s.size() && (s[p] == ' ' || s[p] == '\t')) ++p;
                                        if (s.compare(p, rhs.size(), rhs) == 0) {
                                            const size_t endP = p + rhs.size();
                                            if (endP >= s.size() || s[endP] == '\r' || s[endP] == '\n' ||
                                                s[endP] == ' ' || s[endP] == '\t' || s[endP] == '/') {
                                                s.replace(p, rhs.size(), repl);
                                                return true;
                                            }
                                        }
                                    }
                                }
                            }
                            pos += 7;
                        }
                        return false;
                    };
                    replaceParamsMacro("ALPHA_TEST",     "x", "hs_params.x");
                    replaceParamsMacro("LIGHT_OVERRIDE", "y", "hs_params.y");
                    replaceParamsMacro("BLOOM_TOGGLE",   "z", "hs_params.z");
                    const size_t posVec4Cur = s.find("vec4 colour;");
                    if (posVec4Cur != std::string::npos) {
                        s.insert(posVec4Cur + std::strlen("vec4 colour;"),
                                 "\n    vec3 hs_params = (HS_BATCH_ACTIVE > 1.5 && HS_V_PARAMS.w > 0.5) ? HS_V_PARAMS.xyz : PARAMS;");
                    }
                }
            }
            if (allowGpuDriven && s.find("#version") == std::string::npos) {
                s.insert(0, kGpuVersionHeader);
            }
            std::string fsDecls;
            fsDecls.reserve(768);
            fsDecls += "uniform float HS_BATCH_ACTIVE;\nvarying vec4 HS_V_CMULT;\nvarying vec3 HS_V_CADD;\n";
            if (allowGpuDriven) fsDecls += "varying vec3 HS_V_FOB;\nflat in vec4 HS_V_PARAMS;\n";
            appendBatchSamplerDecls(fsDecls);
            s.insert(FindDeclInsertOffset(s), fsDecls);
            outSrc = std::move(s);
            outTag = "ANIM_BATCH_FS";
            return true;
        }
    }

    // 3. Vanilla world anim_bloom.ksh VERTEX SHADER (preserves else branch for ANIM_BLOOM_VS)
    if (src.find("attribute vec4 POS2D_UV;") != std::string::npos &&
        src.find("uniform mat4 MatrixP;") != std::string::npos &&
        src.find("uniform mat4 MatrixV;") != std::string::npos &&
        src.find("uniform mat4 MatrixW;") != std::string::npos &&
        src.find("PS_TEXCOORD = TEXCOORD0;") != std::string::npos &&
        src.find("mat4 mtxPVW = MatrixP * MatrixV * MatrixW;") != std::string::npos &&
        src.find("PS_POS") == std::string::npos) {
        const size_t posPvw   = src.find("mat4 mtxPVW = MatrixP * MatrixV * MatrixW;");
        const size_t posElse  = (posPvw != std::string::npos) ? src.rfind("#else", posPvw) : std::string::npos;
        const size_t posEndif = (posPvw != std::string::npos) ? src.find("#endif", posPvw) : std::string::npos;
        if (posPvw != std::string::npos && posElse != std::string::npos && posEndif != std::string::npos) {
            std::string s = src;
            if (allowGpuDriven) {
                const std::string gpuBloomVsBody =
                    std::string("#else\n"
                                "\tvec3 TEXCOORD0;\n"
                                "\tif (HS_BATCH_ACTIVE > 1.5) {\n") +
                    kGpuInstSwayBody +
                    "\t\tgl_Position = (MatrixP * MatrixV) * world_pos_tmp;\n"
                    "\t} else if (HS_BATCH_ACTIVE > 0.5) {\n"
                    "\t\tfloat hs_si = floor(POS2D_UV.z / 2.0);\n"
                    "\t\tTEXCOORD0 = vec3(POS2D_UV.z - 2.0 * hs_si, POS2D_UV.w, HS_BATCH_WPOS.w);\n"
                    "\t\tHS_V_BTEX = uvec2(0u, 0u);\n"
                    "\t\tgl_Position = (MatrixP * MatrixV) * vec4(HS_BATCH_WPOS.xyz, 1.0);\n"
                    "\t} else {\n"
                    "\t\tvec3 POSITION = vec3(POS2D_UV.xy, 0.0);\n"
                    "\t\tfloat samplerIndex = floor(POS2D_UV.z/2.0);\n"
                    "\t\tTEXCOORD0 = vec3(POS2D_UV.z - 2.0*samplerIndex, POS2D_UV.w, samplerIndex);\n"
                    "\t\tHS_V_BTEX = uvec2(0u, 0u);\n"
                    "\t\tmat4 mtxPVW = MatrixP * MatrixV * MatrixW;\n"
                    "\t\tgl_Position = mtxPVW * vec4( POSITION.xyz, 1.0 );\n"
                    "\t}\n";
                s.replace(posElse, posEndif - posElse, gpuBloomVsBody);
            } else {
                static constexpr const char* kCpuBloomVsBody =
                    "#else\n"
                    "\tvec3 TEXCOORD0;\n"
                    "\tif (HS_BATCH_ACTIVE > 0.5) {\n"
                    "\t\tfloat hs_si = floor(POS2D_UV.z / 2.0);\n"
                    "\t\tTEXCOORD0 = vec3(POS2D_UV.z - 2.0 * hs_si, POS2D_UV.w, HS_BATCH_WPOS.w);\n"
                    "\t\tgl_Position = (MatrixP * MatrixV) * vec4(HS_BATCH_WPOS.xyz, 1.0);\n"
                    "\t} else {\n"
                    "\t\tvec3 POSITION = vec3(POS2D_UV.xy, 0.0);\n"
                    "\t\tfloat samplerIndex = floor(POS2D_UV.z/2.0);\n"
                    "\t\tTEXCOORD0 = vec3(POS2D_UV.z - 2.0*samplerIndex, POS2D_UV.w, samplerIndex);\n"
                    "\t\tmat4 mtxPVW = MatrixP * MatrixV * MatrixW;\n"
                    "\t\tgl_Position = mtxPVW * vec4( POSITION.xyz, 1.0 );\n"
                    "\t}\n";
                s.replace(posElse, posEndif - posElse, kCpuBloomVsBody);
            }
            const size_t posAttr = s.find("attribute vec4 POS2D_UV;");
            if (posAttr != std::string::npos) {
                static constexpr const char* kGpuBloomVsDecls =
                    "struct HsGpuInst { vec4 m0; vec4 m1; vec4 m2; vec4 cmult; vec4 cadd; vec4 meta; vec4 meta2; uvec2 tex0; uvec2 tex1; uvec2 tex2; uvec2 flags; };\n"
                    "layout(std430, binding = 0) readonly buffer HsGpuInstBuf { HsGpuInst hs_gpu_insts[]; };\n"
                    "uniform float HS_BATCH_ACTIVE;\n"
                    "attribute vec4 HS_BATCH_WPOS;\n"
                    "flat out uvec2 HS_V_BTEX;\n";
                static constexpr const char* kCpuBloomVsDecls =
                    "uniform float HS_BATCH_ACTIVE;\n"
                    "attribute vec4 HS_BATCH_WPOS;\n";
                s.insert(posAttr, allowGpuDriven ? kGpuBloomVsDecls : kCpuBloomVsDecls);
                if (allowGpuDriven && s.find("#version") == std::string::npos) {
                    s.insert(0, kGpuVersionHeader);
                }
                outSrc = std::move(s);
                outTag = "ANIM_BLOOM_BATCH_VS";
                return true;
            }
        }
    }

    // 4. Vanilla world anim_bloom.ksh FRAGMENT SHADER (preserves output line for ANIM_BLOOM_FS)
    if (src.find("uniform sampler2D SAMPLER[") != std::string::npos &&
        src.find("gl_FragColor.rgba = vec4( 0, 0, 0, colour.a );") != std::string::npos &&
        src.find("LIGHTMAP_TEXTURE") == std::string::npos) {
        const size_t posVec4   = src.find("vec4 colour;");
        const size_t posTriple = (posVec4 != std::string::npos) ? src.find("#if defined( TRIPLE_ATLAS )", posVec4) : std::string::npos;
        const size_t posFrag   = (posTriple != std::string::npos) ? src.find("gl_FragColor.rgba = vec4( 0, 0, 0, colour.a );", posTriple) : std::string::npos;
        const size_t posEndif  = (posFrag != std::string::npos) ? src.rfind("#endif", posFrag) : std::string::npos;
        if (posTriple != std::string::npos && posEndif != std::string::npos && posEndif > posTriple) {
            std::string s = src;
            const std::string origSampleBlock = s.substr(posTriple, (posEndif + 6) - posTriple);
            s.replace(posTriple, (posEndif + 6) - posTriple, buildWrappedSampleBlock(origSampleBlock));
            if (allowGpuDriven && s.find("#version") == std::string::npos) {
                s.insert(0, kGpuVersionHeader);
            }
            std::string fsDecls = "uniform float HS_BATCH_ACTIVE;\n";
            appendBatchSamplerDecls(fsDecls);
            s.insert(FindDeclInsertOffset(s), fsDecls);
            outSrc = std::move(s);
            outTag = "ANIM_BLOOM_BATCH_FS";
            return true;
        }
    }

    return false;
}

// =============================================================================
// O(1) Zero-Churn CPU Shadow Buffer Cache for VBO/IBO Geometry
//   - Direct O(1) table lookup for bufId < 65536 (+ fallback map for high IDs)
//   - Intrusive O(1) doubly-linked LRU eviction (ZERO O(N) hash-map scans!)
//   - Retains vector capacity across VBPool recycle/reupload (ZERO malloc/free churn!)
// =============================================================================
class ShadowBufferCache {
public:
    static constexpr uint32_t kInvalidSlot  = 0xFFFFFFFFu;
    static constexpr uint32_t kDirectCap    = 65536u;
    static constexpr uint32_t kMaxEntries   = 32768u;

    struct Slot {
        uint32_t             bufId = 0;
        uint32_t             prev  = kInvalidSlot;
        uint32_t             next  = kInvalidSlot;
        uint32_t             used  = 0;     // active byte length (0 = deleted/invalid, capacity retained)
        bool                 inLru = false;
        std::vector<uint8_t> bytes;
    };

    void OnUpload(uint32_t bufId, const void* data, size_t size) {
        if (bufId == 0u) return;
        if (bufId > maxBufId_) maxBufId_ = bufId;
        ++uploads_;
        if (!data || size == 0u || size > kMaxShadowBufBytes) {
            OnDelete(bufId);
            return;
        }
        uint32_t idx = LookupSlot(bufId);
        if (idx != kInvalidSlot) {
            Slot& s = pool_[idx];
            totalBytes_ -= std::min(totalBytes_, static_cast<size_t>(s.used));
            if (s.bytes.size() < size) s.bytes.resize(size);
            std::memcpy(s.bytes.data(), data, size);
            s.used = static_cast<uint32_t>(size);
            totalBytes_ += size;
            TouchLru(idx);
            EvictIfNeeded(idx);
            return;
        }
        if (pool_.size() >= kMaxEntries && freeSlots_.empty()) {
            EvictTail(kInvalidSlot);
        }
        if (!freeSlots_.empty()) {
            idx = freeSlots_.back();
            freeSlots_.pop_back();
        } else {
            idx = static_cast<uint32_t>(pool_.size());
            pool_.emplace_back();
        }
        Slot& s = pool_[idx];
        s.bufId = bufId;
        if (s.bytes.size() < size) s.bytes.resize(size);
        std::memcpy(s.bytes.data(), data, size);
        s.used = static_cast<uint32_t>(size);
        totalBytes_ += size;
        BindSlot(bufId, idx);
        TouchLru(idx);
        EvictIfNeeded(idx);
    }

    void OnDelete(uint32_t bufId) {
        if (bufId == 0u) return;
        ClearDynamic(bufId);
        InvalidateIfPresent(bufId);
    }

    void InvalidateIfPresent(uint32_t bufId) noexcept {
        if (bufId == 0u) return;
        const uint32_t idx = LookupSlot(bufId);
        if (idx == kInvalidSlot) return;
        Slot& s = pool_[idx];
        if (s.used != 0u) {
            totalBytes_ -= std::min(totalBytes_, static_cast<size_t>(s.used));
            s.used = 0u;
        }
        MoveToTail(idx);
    }

    void MarkDynamic(uint32_t bufId) {
        if (bufId == 0u || bufId >= kDirectCap) return;
        if (bufId >= dynFlags_.size()) {
            size_t nextSz = std::max<size_t>(256u, dynFlags_.size() * 2u);
            while (nextSz <= bufId) nextSz *= 2u;
            if (nextSz > kDirectCap) nextSz = kDirectCap;
            dynFlags_.resize(nextSz, 0u);
        }
        dynFlags_[bufId] = 1u;
    }

    void ClearDynamic(uint32_t bufId) noexcept {
        if (bufId < dynFlags_.size()) {
            dynFlags_[bufId] = 0u;
        }
    }

    bool IsDynamic(uint32_t bufId) const noexcept {
        return (bufId < dynFlags_.size()) ? (dynFlags_[bufId] != 0u) : false;
    }

    // One-time lazy GPU->CPU readback ONLY for static anim.ksh VBOs actually drawn on screen!
    // Uses core OpenGL ES 3.0+ glMapBufferRange(GL_MAP_READ_BIT) + glUnmapBuffer (100% ES-spec compliant!).
    // Eliminates 100% of upload-time shadow memcpy across millions of VBPool/terrain/UI buffers.
    const uint8_t* FetchFromGpu(uint32_t target, uint32_t bufId, const GlBatchOps& ops,
                                uint32_t curBoundBuf, size_t& outSize) {
        outSize = 0;
        if (bufId == 0u || IsDynamic(bufId) ||
            !ops.glBindBuffer || !ops.glGetBufferParameteriv ||
            !ops.glMapBufferRange || !ops.glUnmapBuffer) {
            return nullptr;
        }
        ops.glBindBuffer(target, bufId);
        int32_t bufSz = 0;
        ops.glGetBufferParameteriv(target, kGlBufferSize, &bufSz);
        if (bufSz <= 0 || static_cast<size_t>(bufSz) > kMaxShadowBufBytes) {
            if (curBoundBuf != bufId) ops.glBindBuffer(target, curBoundBuf);
            if (static_cast<size_t>(bufSz) > kMaxShadowBufBytes) MarkDynamic(bufId); // skip future queries
            return nullptr;
        }
        const size_t size = static_cast<size_t>(bufSz);
        void* mapped = ops.glMapBufferRange(target, 0, static_cast<ptrdiff_t>(size), kMapReadBit);
        if (!mapped) {
            if (curBoundBuf != bufId) ops.glBindBuffer(target, curBoundBuf);
            return nullptr;
        }
        if (bufId > maxBufId_) maxBufId_ = bufId;
        uint32_t idx = LookupSlot(bufId);
        if (idx == kInvalidSlot) {
            if (pool_.size() >= kMaxEntries && freeSlots_.empty()) {
                EvictTail(kInvalidSlot);
            }
            if (!freeSlots_.empty()) {
                idx = freeSlots_.back();
                freeSlots_.pop_back();
            } else {
                idx = static_cast<uint32_t>(pool_.size());
                pool_.emplace_back();
            }
            pool_[idx].bufId = bufId;
            BindSlot(bufId, idx);
        }
        Slot& s = pool_[idx];
        totalBytes_ -= std::min(totalBytes_, static_cast<size_t>(s.used));
        if (s.bytes.size() < size) s.bytes.resize(size);
        std::memcpy(s.bytes.data(), mapped, size);
        ops.glUnmapBuffer(target);
        if (curBoundBuf != bufId) ops.glBindBuffer(target, curBoundBuf);
        s.used = static_cast<uint32_t>(size);
        totalBytes_ += size;
        ++gpuFetches_;
        TouchLru(idx);
        EvictIfNeeded(idx);
        outSize = size;
        return s.bytes.data();
    }

    void Clear() {
        for (auto& s : pool_) {
            s.bufId = 0;
            s.prev  = kInvalidSlot;
            s.next  = kInvalidSlot;
            s.used  = 0;
            s.inLru = false;
            s.bytes.clear();
            s.bytes.shrink_to_fit();
        }
        pool_.clear();
        freeSlots_.clear();
        direct_.clear();
        dynFlags_.clear();
        overflow_.clear();
        head_       = kInvalidSlot;
        tail_       = kInvalidSlot;
        totalBytes_ = 0;
        uploads_    = 0;
        evicts_     = 0;
        gpuFetches_ = 0;
        maxBufId_   = 0;
    }

    const uint8_t* Find(uint32_t bufId, size_t& outSize) noexcept {
        const uint32_t idx = LookupSlot(bufId);
        if (idx == kInvalidSlot) {
            outSize = 0;
            return nullptr;
        }
        const Slot& s = pool_[idx];
        if (s.used == 0u || s.bytes.size() < s.used) {
            outSize = 0;
            return nullptr;
        }
        TouchLru(idx);
        outSize = static_cast<size_t>(s.used);
        return s.bytes.data();
    }

    size_t   PeekSize(uint32_t bufId) const noexcept {
        const uint32_t idx = LookupSlot(bufId);
        if (idx == kInvalidSlot) return 0u;
        return static_cast<size_t>(pool_[idx].used);
    }

    void SetMaxBytes(size_t maxBytes) noexcept {
        maxCacheBytes_ = (maxBytes >= 65536u) ? maxBytes : 65536u;
        EvictIfNeeded(kInvalidSlot);
    }

    size_t   MaxBytes() const noexcept { return maxCacheBytes_; }
    size_t   TotalBytes() const noexcept { return totalBytes_; }
    size_t   Count() const noexcept { return pool_.size() - freeSlots_.size(); }
    uint64_t Uploads() const noexcept { return uploads_; }
    uint64_t Evicts() const noexcept { return evicts_; }
    uint64_t GpuFetches() const noexcept { return gpuFetches_; }
    uint32_t MaxBufId() const noexcept { return maxBufId_; }

private:
    uint32_t LookupSlot(uint32_t bufId) const noexcept {
        if (bufId < direct_.size()) {
            return direct_[bufId];
        }
        if (bufId >= kDirectCap) {
            auto it = overflow_.find(bufId);
            return (it != overflow_.end()) ? it->second : kInvalidSlot;
        }
        return kInvalidSlot;
    }

    void BindSlot(uint32_t bufId, uint32_t idx) {
        if (bufId < kDirectCap) {
            if (bufId >= direct_.size()) {
                size_t nextSz = std::max<size_t>(256u, direct_.size() * 2u);
                while (nextSz <= bufId) nextSz *= 2u;
                if (nextSz > kDirectCap) nextSz = kDirectCap;
                direct_.resize(nextSz, kInvalidSlot);
            }
            direct_[bufId] = idx;
        } else {
            overflow_[bufId] = idx;
        }
    }

    void UnbindSlot(uint32_t bufId) noexcept {
        if (bufId < direct_.size()) {
            direct_[bufId] = kInvalidSlot;
        } else if (bufId >= kDirectCap) {
            overflow_.erase(bufId);
        }
    }

    void UnlinkLru(uint32_t idx) noexcept {
        Slot& s = pool_[idx];
        if (!s.inLru) return;
        if (s.prev != kInvalidSlot) pool_[s.prev].next = s.next;
        else head_ = s.next;
        if (s.next != kInvalidSlot) pool_[s.next].prev = s.prev;
        else tail_ = s.prev;
        s.prev  = kInvalidSlot;
        s.next  = kInvalidSlot;
        s.inLru = false;
    }

    void TouchLru(uint32_t idx) noexcept {
        if (head_ == idx) return;
        UnlinkLru(idx);
        Slot& s = pool_[idx];
        s.prev  = kInvalidSlot;
        s.next  = head_;
        s.inLru = true;
        if (head_ != kInvalidSlot) pool_[head_].prev = idx;
        else tail_ = idx;
        head_ = idx;
    }

    void MoveToTail(uint32_t idx) noexcept {
        if (tail_ == idx) return;
        UnlinkLru(idx);
        Slot& s = pool_[idx];
        s.prev  = tail_;
        s.next  = kInvalidSlot;
        s.inLru = true;
        if (tail_ != kInvalidSlot) pool_[tail_].next = idx;
        else head_ = idx;
        tail_ = idx;
    }

    bool EvictTail(uint32_t keepIdx) noexcept {
        uint32_t victim = tail_;
        if (victim == keepIdx) {
            victim = (victim != kInvalidSlot) ? pool_[victim].prev : kInvalidSlot;
        }
        if (victim == kInvalidSlot) return false;
        UnlinkLru(victim);
        Slot& s = pool_[victim];
        totalBytes_ -= std::min(totalBytes_, static_cast<size_t>(s.used));
        UnbindSlot(s.bufId);
        s.bufId = 0;
        s.used  = 0;
        // Release unusually large (>64KB) backing vector on eviction so memory stays bounded
        if (s.bytes.capacity() > 65536u) {
            std::vector<uint8_t>().swap(s.bytes);
        }
        freeSlots_.push_back(victim);
        ++evicts_;
        return true;
    }

    void EvictIfNeeded(uint32_t keepIdx) noexcept {
        while (totalBytes_ > maxCacheBytes_) {
            if (!EvictTail(keepIdx)) break;
        }
    }

    std::vector<Slot>                     pool_;
    std::vector<uint32_t>                 freeSlots_;
    std::vector<uint32_t>                 direct_;
    std::vector<uint8_t>                  dynFlags_;
    std::unordered_map<uint32_t, uint32_t> overflow_;
    uint32_t                              head_          = kInvalidSlot;
    uint32_t                              tail_          = kInvalidSlot;
    size_t                                maxCacheBytes_ = kMaxShadowCacheBytes;
    size_t                                totalBytes_    = 0;
    uint64_t                              uploads_    = 0;
    uint64_t                              evicts_     = 0;
    uint64_t                              gpuFetches_ = 0;
    uint32_t                              maxBufId_   = 0;
};

// =============================================================================
// Per-Thread Multi-Texture Sprite Batcher State Machine
// =============================================================================
class SpriteBatcher {
public:
    static constexpr uint32_t kBindlessDirectCap        = 65536u;
    static constexpr uint32_t kMaxBindlessResident      = 4096u;
    static constexpr uint32_t kMaxFrameGpuModeSwitches  = 4u;
    static constexpr uint32_t kGpuPingPongCooldownFrames = 120u;

    struct BindlessSlot {
        uint64_t handle     = 0ULL;
        bool     resident   = false;
        bool     denied     = false;
        uint8_t  denyReason = 0u; // 1=mutatedResident, 2=nullAlloc, 3=handleZero
    };

    struct DeniedTexSample {
        uint32_t texId  = 0u;
        uint8_t  reason = 0u;
    };

    SpriteBatcher() = default;
    ~SpriteBatcher() = default;
    SpriteBatcher(const SpriteBatcher&) = delete;
    SpriteBatcher& operator=(const SpriteBatcher&) = delete;

    void SetOps(const GlBatchOps& ops) noexcept {
        ops_ = ops;
    }

    void SetShadowCacheMaxBytes(size_t maxBytes) noexcept {
        shadowCache_.SetMaxBytes(maxBytes);
    }

    size_t ShadowCacheMaxBytes() const noexcept {
        return shadowCache_.MaxBytes();
    }

    size_t ShadowCacheTotalBytes() const noexcept {
        return shadowCache_.TotalBytes();
    }

    void SetPersistentRingPreferred(bool en) noexcept {
        if (persistentRingPreferred_ == en) return;
        const bool oldWant = CanUseAzdoPersistent();
        persistentRingPreferred_ = en;
        const bool newWant = CanUseAzdoPersistent();
        if (ringReady_ && !HasPending() && oldWant != newWant) {
            if (ops_.glBindBuffer) {
                ReleaseGpuBuffersOnly(true);
                ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
                ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
            }
        }
    }

    void SetGpuDrivenPreferred(bool en) noexcept {
        if (gpuDrivenPreferred_ == en) return;
        gpuDrivenPreferred_ = en;
        if (en && ringReady_ && !gpuDrivenRingActive_ && !gpuDrivenRingFailed_ &&
            CanAllocateGpuDrivenRing()) {
            gpuRingResizePending_ = true;
        }
    }

    void SetElasticRingBudget(uint32_t maxVertsPerFrame,
                              uint32_t maxIndicesPerFrame,
                              uint32_t maxInstsPerFrame) noexcept {
        const uint32_t v = std::max<uint32_t>(kAzdoFrameMaxVerts, std::min<uint32_t>(maxVertsPerFrame, 2097152u));
        const uint32_t i = std::max<uint32_t>(kAzdoFrameMaxIndices, std::min<uint32_t>(maxIndicesPerFrame, 4194304u));
        const uint32_t s = std::max<uint32_t>(kMaxGpuBatchInsts, std::min<uint32_t>(maxInstsPerFrame, 131072u));
        const bool shrinking = (v < baseWantGpuVerts_) || (i < baseWantGpuIndices_) || (s < baseWantGpuInsts_);
        baseWantGpuVerts_   = v;
        baseWantGpuIndices_ = i;
        baseWantGpuInsts_   = s;
        const uint32_t nv = shrinking ? v : std::max<uint32_t>(wantGpuFrameMaxVerts_, v);
        const uint32_t ni = shrinking ? i : std::max<uint32_t>(wantGpuFrameMaxIndices_, i);
        const uint32_t ns = shrinking ? s : std::max<uint32_t>(wantGpuFrameMaxInsts_, s);
        if (nv == wantGpuFrameMaxVerts_ && ni == wantGpuFrameMaxIndices_ && ns == wantGpuFrameMaxInsts_) {
            return;
        }
        wantGpuFrameMaxVerts_   = nv;
        wantGpuFrameMaxIndices_ = ni;
        wantGpuFrameMaxInsts_   = ns;
        if (ringReady_ && persistentRingActive_) {
            gpuRingResizePending_ = true;
        }
    }

    bool IsPersistentRingActive() const noexcept {
        return persistentRingActive_;
    }

    bool IsGpuDrivenActive() const noexcept {
        return gpuDrivenPreferred_ && gpuDrivenRingActive_ && !gpuDrivenRingFailed_;
    }

    uint32_t BindlessResidentCount() const noexcept {
        return bindlessResidentCnt_;
    }

    uint64_t GpuDrivenDraws() const noexcept {
        return gpuDrivenDraws_;
    }

    bool IsTextureBindless(uint32_t texId) const noexcept {
        const BindlessSlot* sl = PeekBindlessSlot(texId);
        return sl != nullptr && (sl->handle != 0ULL || sl->resident);
    }

    void OnTexImageMutated(uint32_t texId, bool denyIfNew = false) noexcept {
        if (texId == 0u) return;
        const BindlessSlot* sl = PeekBindlessSlot(texId);
        if (sl != nullptr && (sl->handle != 0ULL || sl->resident)) {
            if (HasPending() && curBatchGpuMode_) {
                Flush(FlushReason::Tex);
            }
            InvalidateBindlessTex(texId, true, 1u);
        } else if (denyIfNew) {
            BindlessSlot& s = TouchBindlessSlot(texId);
            s.denied = true;
            if (s.denyReason == 0u) s.denyReason = 2u;
            mruBindlessValid_ = false;
        }
    }

    void BeginFrameProfile(bool enable) noexcept {
        profEnabled_ = enable && (ops_.nowQpc != nullptr);
        frameProf_   = {};
    }

    FrameProfile TakeFrameProfile() noexcept {
        const FrameProfile out = frameProf_;
        frameProf_ = {};
        return out;
    }

    const DiagCounters& GetDiagCounters() const noexcept { return diag_; }
    uint32_t ActiveAzdoFrameMaxVerts() const noexcept { return activeAzdoFrameMaxVerts_; }
    uint64_t ShadowCacheGpuFetches() const noexcept { return shadowCache_.GpuFetches(); }
    bool IsGpuLatchedOff() const noexcept { return frameGpuLatchedOff_ || (gpuCooldownFrames_ > 0u); }

    bool PopDeniedTexSample(DeniedTexSample& out) noexcept {
        if (deniedSampleRead_ >= deniedSampleCount_) return false;
        out = deniedSamples_[deniedSampleRead_++];
        return true;
    }

    void SetEnabled(bool en) noexcept {
        if (!en) {
            if (HasPending()) Flush();
            EndStickyBatchSession();
            CommitDeferredMatrixW();
            boundSsboId_ = 0u;
        }
        enabled_ = en;
    }

    bool IsEnabled() const noexcept { return enabled_; }
    bool HasPending() const noexcept { return indexCount_ > 0u; }
    bool HasDeferredMatrixW() const noexcept { return matrixWDirty_; }
    const Stats& GetStats() const noexcept { return stats_; }
    void ResetStats() noexcept { stats_ = {}; }
    Stats TakeStatsDelta() noexcept {
        Stats d = stats_;
        stats_ = {};
        return d;
    }

    uint32_t BoundBufferForTarget(uint32_t target) const noexcept {
        if (target == kGlArrayBuffer) return boundArrayBuf_;
        if (target == kGlElementArrayBuffer) return boundElemBuf_;
        return 0u;
    }

    uint32_t CurrentProgramId() const noexcept { return curProgId_; }
    bool IsCurrentProgramEligible() const noexcept { return curProg_ && curProg_->eligible; }
    bool IsCurrentProgramBloomPass() const noexcept { return curProg_ && curProg_->isBloomPass; }
    int32_t CurrentBatchActiveLoc() const noexcept { return curProg_ ? curProg_->locBatchActive : -1; }
    uint32_t ActiveUnit() const noexcept { return activeUnit_; }
    uint32_t BoundArrayBuf() const noexcept { return boundArrayBuf_; }
    uint32_t BoundElemBuf() const noexcept { return boundElemBuf_; }
    void GetAttribState(uint32_t index, AttribState& out, bool& enabled) const noexcept {
        if (index < 16u) {
            out = attribs_[index];
            enabled = (attribEnMask_ & static_cast<uint16_t>(1u << index)) != 0u;
        } else {
            out = {};
            enabled = false;
        }
    }
    void GetAttrib0State(AttribState& out, bool& enabled) const noexcept {
        GetAttribState(0u, out, enabled);
    }
    void SetShadowActive(bool active) noexcept {
        shadowPassActive_ = active;
    }
    void SetDeferHostRestore(bool defer) noexcept {
        const bool wasDefer = deferHostRestore_;
        deferHostRestore_ = defer;
        if (wasDefer && !defer) {
            EndStickyBatchSession();
        }
    }
    bool IsStickySessionActive() const noexcept {
        return stickySessionActive_;
    }
    void InvalidateStickyTex(uint32_t texId) noexcept {
        if (texId == 0u) return;
        for (uint32_t s = 0; s < kMaxBatchTexSlots; ++s) {
            if (stickySlotTex_[s] == texId) {
                stickySlotTex_[s] = 0u;
            }
        }
    }
    void InvalidateStickyTexUnit(uint32_t unit) noexcept {
        if (!stickySessionActive_ || unit < kBatchTexUnitBase) return;
        for (uint32_t s = 0; s < kMaxBatchTexSlots; ++s) {
            if (BatchSlotToTexUnit(s) == unit) {
                stickySlotTex_[s] = 0u;
            }
        }
    }
    void PrepareForImmediateShadowMirror() noexcept {
        if (!stickySessionActive_ || !stickyAttribsZeroOffset_ || !curProg_ ||
            !ops_.glVertexAttribPointer) {
            return;
        }
        const uintptr_t baseOff = lastFlushVertByteOffset_;
        const uint32_t  posLoc  = static_cast<uint32_t>(curProg_->locPos2dUv);
        const uint32_t  wposLoc = static_cast<uint32_t>(curProg_->locBatchWpos);
        if (stickyMode_ == 2u) {
            ops_.glVertexAttribPointer(posLoc, 4, kGlFloat, 0, sizeof(GpuBatchVertex),
                                       reinterpret_cast<const void*>(baseOff + offsetof(GpuBatchVertex, x)));
            ops_.glVertexAttribPointer(wposLoc, 1, kGlFloat, 0, sizeof(GpuBatchVertex),
                                       reinterpret_cast<const void*>(baseOff + offsetof(GpuBatchVertex, instId)));
        } else {
            ops_.glVertexAttribPointer(posLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                       reinterpret_cast<const void*>(baseOff + offsetof(BatchedVertex, x)));
            ops_.glVertexAttribPointer(wposLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                       reinterpret_cast<const void*>(baseOff + offsetof(BatchedVertex, wx)));
            if (stickyCMultLoc_ >= 0 && stickyCAddLoc_ >= 0) {
                ops_.glVertexAttribPointer(static_cast<uint32_t>(stickyCMultLoc_), 4, kGlFloat, 0, sizeof(BatchedVertex),
                                           reinterpret_cast<const void*>(baseOff + offsetof(BatchedVertex, mr)));
                ops_.glVertexAttribPointer(static_cast<uint32_t>(stickyCAddLoc_), 4, kGlFloat, 0, sizeof(BatchedVertex),
                                           reinterpret_cast<const void*>(baseOff + offsetof(BatchedVertex, ar)));
            }
        }
        stickyAttribsZeroOffset_ = false;
    }
    void EndStickyBatchSession() noexcept {
        if (!stickySessionActive_) return;
        stickySessionActive_     = false;
        stickyAttribsZeroOffset_ = false;
        stickyMode_              = 0u;
        for (uint32_t s = 0; s < kMaxBatchTexSlots; ++s) {
            stickySlotTex_[s] = 0u;
        }
        if (stickyBatchActiveLoc_ >= 0 && ops_.glUniform1f) {
            ops_.glUniform1f(stickyBatchActiveLoc_, 0.0f);
        }
        if (ops_.glDisableVertexAttribArray) {
            if (stickyWposLoc_ >= 0) ops_.glDisableVertexAttribArray(static_cast<uint32_t>(stickyWposLoc_));
            if (stickyCMultLoc_ >= 0 && stickyCAddLoc_ >= 0) {
                ops_.glDisableVertexAttribArray(static_cast<uint32_t>(stickyCMultLoc_));
                ops_.glDisableVertexAttribArray(static_cast<uint32_t>(stickyCAddLoc_));
            }
        }
        if (!deferHostRestore_) {
            if (ops_.glBindBuffer) {
                ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
                ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
            }
            const uint32_t posLoc = (curProg_ && curProg_->locPos2dUv >= 0)
                                        ? static_cast<uint32_t>(curProg_->locPos2dUv)
                                        : 0u;
            if (posLoc < 16u && attribs_[posLoc].valid && ops_.glVertexAttribPointer) {
                const AttribState& a = attribs_[posLoc];
                if (a.buffer != boundArrayBuf_ && ops_.glBindBuffer) {
                    ops_.glBindBuffer(kGlArrayBuffer, a.buffer);
                    ops_.glVertexAttribPointer(posLoc, a.size, a.type, a.normalized, a.stride, a.pointer);
                    ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
                } else {
                    ops_.glVertexAttribPointer(posLoc, a.size, a.type, a.normalized, a.stride, a.pointer);
                }
            }
            if (ops_.glActiveTexture) {
                ops_.glActiveTexture(kGlTexture0 + activeUnit_);
            }
        }
        const uint32_t wposNotify = (stickyWposLoc_ >= 0) ? static_cast<uint32_t>(stickyWposLoc_) : 7u;
        stickyProgId_         = 0u;
        stickyBatchActiveLoc_ = -1;
        stickyWposLoc_        = -1;
        stickyCMultLoc_       = -1;
        stickyCAddLoc_        = -1;
        if (ops_.onPostFlushRestore) {
            ops_.onPostFlushRestore(activeUnit_, boundArrayBuf_, boundElemBuf_, wposNotify, ops_.callbackCtx);
        }
    }
    bool IsUnitEntityAtlas(uint32_t unit) const noexcept {
        if (!curProg_ || !curProg_->eligible) return false;
        const int32_t u = static_cast<int32_t>(unit);
        return (u == curProg_->samplerUnit0 || u == curProg_->samplerUnit1 ||
                (curProg_->locSampler5 >= 0 && u == curProg_->samplerUnit5));
    }
    uint32_t EffectiveMaxTexSlots() const noexcept {
        const uint32_t progCap = curProg_ ? curProg_->progMaxTexSlots : kLegacyBatchTexSlots;
        return (maxTexSlots_ < progCap) ? maxTexSlots_ : progCap;
    }
    void SetShadowHints(bool skipTranslucent, bool hasCamRight, float camRightX, float camRightZ,
                        float minShadowHeight = 0.0f) noexcept {
        skipTranslucentShadows_ = skipTranslucent;
        hasCamRight_            = hasCamRight;
        camRightX_              = camRightX;
        camRightZ_              = camRightZ;
        minShadowHeight_        = (minShadowHeight > 0.0f) ? minShadowHeight : 0.0f;
    }
    const float* LastFlushShadowBoundsXZ() const noexcept {
        return lastFlushHasShadowBounds_ ? lastFlushShadowBounds_ : nullptr;
    }
    uintptr_t LastFlushIndexByteOffset() const noexcept {
        return lastFlushIndexByteOffset_;
    }
    uintptr_t LastFlushVertByteOffset() const noexcept {
        return lastFlushVertByteOffset_;
    }
    bool LastFlushUsedPersistentRing() const noexcept {
        return lastFlushUsedPersistent_;
    }
    uint32_t LastFlushVboId() const noexcept {
        return lastFlushVboId_;
    }
    uint32_t LastFlushIboId() const noexcept {
        return lastFlushIboId_;
    }
    uint32_t LastFlushTexCount() const noexcept {
        return lastFlushTexCount_;
    }
    bool LastFlushIsGpuDriven() const noexcept {
        return lastFlushIsGpuDriven_;
    }
    uint32_t LastFlushSsboId() const noexcept {
        return lastFlushSsboId_;
    }
    int32_t LastFlushBaseVertex() const noexcept {
        return lastFlushBaseVertex_;
    }
    const uint32_t* LastFlushTexIds() const noexcept {
        return lastFlushTexIds_;
    }

    // Frame boundary hook (called once per SwapBuffers after Flush(End)):
    // Places ONE fence for the completed frame's persistent bump-allocator region and advances
    // to the next triple-buffered frame slice — ZERO intra-frame glClientWaitSync stalls!
    void OnEndFrame() noexcept {
        EndStickyBatchSession();
        boundSsboId_ = 0u;
        const uint32_t frameV = std::max(gpuVertCursor_, azdoVertCursor_);
        if (frameV > diag_.peakFrameVerts) diag_.peakFrameVerts = frameV;
        if (azdoIdxCursor_ > diag_.peakFrameIndices) diag_.peakFrameIndices = azdoIdxCursor_;
        if (gpuInstCursor_ > diag_.peakFrameInsts) diag_.peakFrameInsts = gpuInstCursor_;
        // Auto-grow GPU-Driven ring on mega-base overflow / >85% saturation (non-Low tiers only!)
        if (gpuDrivenRingActive_ && baseWantGpuVerts_ >= 262144u) {
            uint32_t growV = wantGpuFrameMaxVerts_;
            uint32_t growI = wantGpuFrameMaxIndices_;
            uint32_t growS = wantGpuFrameMaxInsts_;
            if (frameHadGpuRingOverflow_ || (gpuVertCursor_ >= (activeGpuFrameMaxVerts_ - (activeGpuFrameMaxVerts_ >> 3)))) {
                growV = std::min<uint32_t>(2097152u, std::max<uint32_t>(growV, activeGpuFrameMaxVerts_ * 2u));
            }
            if (frameHadGpuRingOverflow_ || (azdoIdxCursor_ >= (activeAzdoFrameMaxIndices_ - (activeAzdoFrameMaxIndices_ >> 3)))) {
                growI = std::min<uint32_t>(4194304u, std::max<uint32_t>(growI, activeAzdoFrameMaxIndices_ * 2u));
            }
            if (frameHadGpuRingOverflow_ || (gpuInstCursor_ >= (activeGpuFrameMaxInsts_ - (activeGpuFrameMaxInsts_ >> 3)))) {
                growS = std::min<uint32_t>(131072u, std::max<uint32_t>(growS, activeGpuFrameMaxInsts_ * 2u));
            }
            if (growV > wantGpuFrameMaxVerts_ || growI > wantGpuFrameMaxIndices_ || growS > wantGpuFrameMaxInsts_) {
                wantGpuFrameMaxVerts_   = growV;
                wantGpuFrameMaxIndices_ = growI;
                wantGpuFrameMaxInsts_   = growS;
                gpuRingResizePending_   = true;
            }
        }
        if (gpuCooldownFrames_ > 0u) {
            --gpuCooldownFrames_;
            if (gpuCooldownFrames_ == 0u) {
                consecLatchFrames_ = 0u;
            }
        } else if (!frameGpuLatchedOff_) {
            consecLatchFrames_ = 0u;
        }
        frameModeSwitches_       = 0u;
        frameGpuLatchedOff_      = false;
        frameGpuRingFull_        = false;
        frameHadGpuRingOverflow_ = false;
        if (persistentRingActive_ && azdoFrameTouched_) {
            if (ops_.glFenceSync) {
                if (azdoFrameFences_[azdoFrameIdx_] && ops_.glDeleteSync) {
                    ops_.glDeleteSync(azdoFrameFences_[azdoFrameIdx_]);
                }
                azdoFrameFences_[azdoFrameIdx_] = ops_.glFenceSync(kGlSyncGpuCommandsComplete, 0u);
            }
            azdoFrameIdx_     = (azdoFrameIdx_ + 1u) % kAzdoFramesInFlight;
            azdoVertCursor_   = 0u;
            azdoIdxCursor_    = 0u;
            gpuVertCursor_    = 0u;
            gpuInstCursor_    = 0u;
            azdoFrameWaited_  = (azdoFrameFences_[azdoFrameIdx_] == nullptr);
            azdoFrameTouched_ = false;
        }
        if (gpuRingResizePending_ && !HasPending() && ops_.glBindBuffer) {
            if (ops_.glClientWaitSync && ops_.glDeleteSync) {
                for (uint32_t i = 0; i < kAzdoFramesInFlight; ++i) {
                    if (azdoFrameFences_[i]) {
                        ops_.glClientWaitSync(azdoFrameFences_[i], kGlSyncFlushCommandsBit, 50000000ULL);
                        ops_.glDeleteSync(azdoFrameFences_[i]);
                        azdoFrameFences_[i] = nullptr;
                    }
                }
            }
            ReleaseGpuBuffersOnly(true);
            ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
            ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
            gpuRingResizePending_ = false;
            gpuDrivenRingFailed_  = false;
        }
    }
    const float* CurrentMatrixW() const noexcept {
        return hasMatrixW_ ? curMatrixW_ : nullptr;
    }
    bool IsCurrentMatrixWStanding() const noexcept {
        if (!hasMatrixW_) return false;
        const float* m = curMatrixW_;
        const float c1LenSq = m[4] * m[4] + m[5] * m[5] + m[6] * m[6];
        return (c1LenSq > 1e-8f) && ((m[5] * m[5]) > 0.1225f * c1LenSq);
    }
    uint32_t LastRejectCode() const noexcept { return lastRejectCode_; }
    void ClearLastRejectCode() noexcept { lastRejectCode_ = 0; }
    void GetDiagState(bool& hasMatW, float& fpZ, uint16_t& enMask, AttribState& posAttr,
                      uint32_t& elemBuf, uint32_t& tex0, uint32_t& tex1, uint32_t& maxSlots) const noexcept {
        hasMatW  = hasMatrixW_;
        fpZ      = curProg_ ? curProg_->lastFloatParamsZ : -1.0f;
        enMask   = attribEnMask_;
        const int32_t pl = (curProg_ && curProg_->locPos2dUv >= 0 && curProg_->locPos2dUv < 16) ? curProg_->locPos2dUv : 0;
        posAttr  = attribs_[pl];
        elemBuf  = boundElemBuf_;
        tex0     = curProg_ && curProg_->samplerUnit0 >= 0 && curProg_->samplerUnit0 < 16 ? boundTex2D_[curProg_->samplerUnit0] : 0u;
        tex1     = curProg_ && curProg_->samplerUnit1 >= 0 && curProg_->samplerUnit1 < 16 ? boundTex2D_[curProg_->samplerUnit1] : 0u;
        maxSlots = maxTexSlots_;
    }
    void GetShadowDiag(uint32_t queryBuf, size_t& cnt, size_t& bytes, uint64_t& evicts,
                       uint64_t& uploads, uint32_t& maxId, size_t& foundSz) const noexcept {
        cnt     = shadowCache_.Count();
        bytes   = shadowCache_.TotalBytes();
        evicts  = shadowCache_.Evicts();
        uploads = shadowCache_.Uploads();
        maxId   = shadowCache_.MaxBufId();
        foundSz = shadowCache_.PeekSize(queryBuf);
    }

    void ReleaseGpuBuffersOnly(bool waitAndDeleteOnGl) noexcept {
        if (waitAndDeleteOnGl) {
            if (ops_.glDeleteSync) {
                for (uint32_t i = 0; i < kAzdoFramesInFlight; ++i) {
                    if (azdoFrameFences_[i] != nullptr) {
                        ops_.glDeleteSync(azdoFrameFences_[i]);
                        azdoFrameFences_[i] = nullptr;
                    }
                }
            } else {
                for (uint32_t i = 0; i < kAzdoFramesInFlight; ++i) azdoFrameFences_[i] = nullptr;
            }
            bool anyUnmapped = false;
            if (ops_.glBindBuffer && ops_.glUnmapBuffer) {
                if (azdoVboId_ != 0u && azdoVboMapped_ != nullptr) {
                    ops_.glBindBuffer(kGlArrayBuffer, azdoVboId_);
                    ops_.glUnmapBuffer(kGlArrayBuffer);
                    azdoVboMapped_ = nullptr;
                    anyUnmapped = true;
                }
                if (azdoIboId_ != 0u && azdoIboMapped_ != nullptr) {
                    ops_.glBindBuffer(kGlElementArrayBuffer, azdoIboId_);
                    ops_.glUnmapBuffer(kGlElementArrayBuffer);
                    azdoIboMapped_ = nullptr;
                    anyUnmapped = true;
                }
                if (gpuVboId_ != 0u && gpuVboMapped_ != nullptr) {
                    ops_.glBindBuffer(kGlArrayBuffer, gpuVboId_);
                    ops_.glUnmapBuffer(kGlArrayBuffer);
                    gpuVboMapped_ = nullptr;
                    anyUnmapped = true;
                }
                if (gpuSsboId_ != 0u && gpuSsboMapped_ != nullptr) {
                    ops_.glBindBuffer(kGlShaderStorageBuffer, gpuSsboId_);
                    ops_.glUnmapBuffer(kGlShaderStorageBuffer);
                    gpuSsboMapped_ = nullptr;
                }
                if (anyUnmapped) {
                    ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
                    ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
                }
            }
            if (ops_.glDeleteBuffers) {
                if (azdoVboId_ != 0u) { ops_.glDeleteBuffers(1, &azdoVboId_); azdoVboId_ = 0u; }
                if (azdoIboId_ != 0u) { ops_.glDeleteBuffers(1, &azdoIboId_); azdoIboId_ = 0u; }
                if (gpuVboId_  != 0u) { ops_.glDeleteBuffers(1, &gpuVboId_);  gpuVboId_  = 0u; }
                if (gpuSsboId_ != 0u) { ops_.glDeleteBuffers(1, &gpuSsboId_); gpuSsboId_ = 0u; }
                for (uint32_t i = 0; i < kRingSlots; ++i) {
                    if (vboIds_[i] != 0u) { ops_.glDeleteBuffers(1, &vboIds_[i]); vboIds_[i] = 0u; }
                    if (iboIds_[i] != 0u) { ops_.glDeleteBuffers(1, &iboIds_[i]); iboIds_[i] = 0u; }
                }
            }
        } else {
            for (uint32_t i = 0; i < kAzdoFramesInFlight; ++i) azdoFrameFences_[i] = nullptr;
        }
        azdoVboId_            = 0u;
        azdoIboId_            = 0u;
        gpuVboId_             = 0u;
        gpuSsboId_            = 0u;
        azdoVboMapped_        = nullptr;
        azdoIboMapped_        = nullptr;
        gpuVboMapped_         = nullptr;
        gpuSsboMapped_        = nullptr;
        for (uint32_t i = 0; i < kRingSlots; ++i) {
            vboIds_[i] = 0u;
            iboIds_[i] = 0u;
        }
        azdoFrameIdx_         = 0u;
        azdoVertCursor_       = 0u;
        azdoIdxCursor_        = 0u;
        gpuVertCursor_        = 0u;
        gpuInstCursor_        = 0u;
        azdoFrameWaited_      = true;
        azdoFrameTouched_     = false;
        persistentRingActive_ = false;
        gpuDrivenRingActive_  = false;
        fallbackRingReady_    = false;
        ringReady_            = false;
        ringFailed_           = false;
        ringIdx_              = 0u;
        lastFlushIndexByteOffset_ = 0u;
        lastFlushVertByteOffset_  = 0u;
        lastFlushIsGpuDriven_     = false;
        lastFlushSsboId_          = 0u;
        lastFlushBaseVertex_      = 0;
        boundSsboId_              = 0u;
    }

    void ReleaseGpuResources() noexcept {
        EndStickyBatchSession();
        ReleaseGpuBuffersOnly(true);
        ReleaseAllBindless(true);
    }

    void ResetContext(void* newCtx, bool deleteGpuOnCurrentCtx = false) {
        if (deleteGpuOnCurrentCtx) {
            ReleaseGpuResources();
        } else if (newCtx != curCtx_ || newCtx == nullptr) {
            ReleaseGpuBuffersOnly(false);
            ReleaseAllBindless(false);
        }
        azdoFailed_          = false;
        gpuDrivenRingFailed_ = false;
        gpuRingResizePending_ = false;
        frameModeSwitches_       = 0u;
        frameGpuLatchedOff_      = false;
        frameGpuRingFull_        = false;
        frameHadGpuRingOverflow_ = false;
        consecLatchFrames_       = 0u;
        gpuCooldownFrames_       = 0u;
        activeAzdoFrameMaxVerts_ = kAzdoFrameMaxVerts;
        // When newCtx == curCtx_ && newCtx != nullptr && !deleteGpuOnCurrentCtx (e.g., failed
        // same-context eglMakeCurrent where pre-switch cleanup was skipped), preserve live
        // vboIds_/iboIds_/ringReady_ so handles are NEVER orphaned and ReleaseGpuResources()
        // still frees every allocated buffer cleanly!
        curCtx_          = newCtx;
        maxTexSlots_     = 0;
        texUnitsProbed_  = false;
        vertCount_       = 0;
        gpuVertCount_    = 0;
        gpuInstCount_    = 0;
        indexCount_      = 0;
        batchTexCount_   = 0;
        curBatchGpuMode_ = false;
        batchHasShadowCaster_ = false;
        shadowMinX_ = 1e30f;
        shadowMaxX_ = -1e30f;
        shadowMinZ_ = 1e30f;
        shadowMaxZ_ = -1e30f;
        lastFlushHasShadowBounds_ = false;
        curProg_          = nullptr;
        curProgId_        = 0;
        hasMatrixW_       = false;
        matrixWDirty_     = false;
        hasColourXform_   = false;
        colourXformDirty_ = false;
        curCMult_[0] = curCMult_[1] = curCMult_[2] = curCMult_[3] = 1.0f;
        curCAdd_[0]  = curCAdd_[1]  = curCAdd_[2]  = curCAdd_[3]  = 0.0f;
        activeUnit_      = 0;
        boundArrayBuf_   = 0;
        boundElemBuf_    = 0;
        boundSsboId_     = 0;
        mruBindlessValid_ = false;
        attribEnMask_    = 0;
        stickySessionActive_     = false;
        stickyAttribsZeroOffset_ = false;
        stickyMode_              = 0u;
        stickyProgId_            = 0u;
        stickyBatchActiveLoc_    = -1;
        stickyWposLoc_           = -1;
        stickyCMultLoc_          = -1;
        stickyCAddLoc_           = -1;
        for (auto& stTex : stickySlotTex_) stTex = 0u;
        for (auto& a : attribs_) a = {};
        for (auto& t : boundTex2D_) t = 0;
        shadowCache_.Clear();
    }

    bool HasLazyGpuReadback() const noexcept {
        return ops_.glBindBuffer != nullptr &&
               ops_.glGetBufferParameteriv != nullptr &&
               ops_.glMapBufferRange != nullptr &&
               ops_.glUnmapBuffer != nullptr;
    }

    void OnBufferUpload(uint32_t bufId, const void* data, size_t size, bool isDynamic = false) {
        if (bufId == 0u) return;
        if (isDynamic) {
            shadowCache_.MarkDynamic(bufId);
        } else {
            shadowCache_.ClearDynamic(bufId);
        }
        // Native OpenGL zero-copy fast path: NEVER memcpy millions of VBPool/terrain/UI uploads!
        // Just O(1) invalidate if this ID happened to be cached; visible anim.ksh static VBOs
        // are lazily fetched ONCE on first draw via glGetBufferSubData.
        if (HasLazyGpuReadback()) {
            shadowCache_.InvalidateIfPresent(bufId);
            return;
        }
        // ANGLE D3D9 fallback (no glGetBufferSubData): skip dynamic VBPool churn & giant terrain VBOs
        if (isDynamic || size > 262144u) {
            shadowCache_.InvalidateIfPresent(bufId);
            return;
        }
        shadowCache_.OnUpload(bufId, data, size);
    }

    void OnDeleteBuffers(int32_t n, const uint32_t* buffers) {
        if (!buffers || n <= 0) return;
        for (int32_t i = 0; i < n; ++i) {
            if (buffers[i] != 0u) {
                shadowCache_.OnDelete(buffers[i]);
                if (buffers[i] == boundArrayBuf_) boundArrayBuf_ = 0;
                if (buffers[i] == boundElemBuf_) boundElemBuf_ = 0;
                for (uint32_t a = 0; a < 16u; ++a) {
                    if (attribs_[a].buffer == buffers[i]) {
                        attribs_[a].buffer = 0;
                        attribs_[a].valid  = false;
                    }
                }
            }
        }
    }

    void OnDeleteTextures(int32_t n, const uint32_t* textures) {
        if (!textures || n <= 0) return;
        if (HasPending()) {
            bool hit = false;
            if (curBatchGpuMode_) {
                for (int32_t i = 0; i < n && !hit; ++i) {
                    if (IsTextureBindless(textures[i])) { hit = true; }
                }
            } else {
                for (int32_t i = 0; i < n && !hit; ++i) {
                    for (uint32_t s = 0; s < batchTexCount_; ++s) {
                        if (batchTexIds_[s] == textures[i]) { hit = true; break; }
                    }
                }
            }
            if (hit) Flush(FlushReason::Tex);
        }
        for (int32_t i = 0; i < n; ++i) {
            InvalidateStickyTex(textures[i]);
            InvalidateBindlessTex(textures[i], false);
            for (uint32_t u = 0; u < 16u; ++u) {
                if (boundTex2D_[u] == textures[i]) boundTex2D_[u] = 0;
            }
        }
    }

    void OnUseProgram(uint32_t progId, BatchProgInfo* info) {
        if (progId != curProgId_ || info != curProg_) {
            Flush(FlushReason::Prog);
            EndStickyBatchSession();
            CommitDeferredMatrixW();
            boundSsboId_      = 0u;
            curProgId_        = progId;
            curProg_          = info;
            hasMatrixW_       = false;
            matrixWDirty_     = false;
            hasColourXform_   = false;
            colourXformDirty_ = false;
            curCMult_[0] = curCMult_[1] = curCMult_[2] = curCMult_[3] = 1.0f;
            curCAdd_[0]  = curCAdd_[1]  = curCAdd_[2]  = curCAdd_[3]  = 0.0f;
        }
    }

    void OnDeleteOrLinkProgram(uint32_t progId) {
        if (progId == curProgId_) {
            Flush(FlushReason::Prog);
            EndStickyBatchSession();
            curProg_          = nullptr;
            curProgId_        = 0;
            hasMatrixW_       = false;
            matrixWDirty_     = false;
            hasColourXform_   = false;
            colourXformDirty_ = false;
        }
    }

    void OnActiveTexture(uint32_t textureUnitEnum) noexcept {
        if (textureUnitEnum >= kGlTexture0 && textureUnitEnum < kGlTexture0 + 16u) {
            activeUnit_ = textureUnitEnum - kGlTexture0;
        }
    }

    void OnBindTexture(uint32_t target, uint32_t texId) {
        if (target != kGlTexture2D || activeUnit_ >= 16u) {
            if (HasPending()) Flush(FlushReason::Tex);
            return;
        }
        const uint32_t prev = boundTex2D_[activeUnit_];
        boundTex2D_[activeUnit_] = texId;
        if (!HasPending() || prev == texId) return;
        // Atlas units (SAMPLER[0], SAMPLER[1], and SAMPLER[5] if TRIPLE_ATLAS) do NOT flush!
        // Shared scene units (e.g. LightMap=3, Ocean=4, Erode=2) MUST flush when changed!
        if (curProg_ &&
            (static_cast<int32_t>(activeUnit_) == curProg_->samplerUnit0 ||
             static_cast<int32_t>(activeUnit_) == curProg_->samplerUnit1 ||
             (curProg_->locSampler5 >= 0 && static_cast<int32_t>(activeUnit_) == curProg_->samplerUnit5))) {
            return;
        }
        Flush(FlushReason::Tex);
    }

    void OnBindBuffer(uint32_t target, uint32_t buffer) noexcept {
        if (target == kGlArrayBuffer) {
            boundArrayBuf_ = buffer;
        } else if (target == kGlElementArrayBuffer) {
            boundElemBuf_ = buffer;
        }
    }

    void OnVertexAttribPointer(uint32_t index, int32_t size, uint32_t type, uint8_t normalized,
                               int32_t stride, const void* ptr) noexcept {
        if (index < 16u) {
            attribs_[index] = AttribState{ boundArrayBuf_, size, type, normalized, stride, ptr, true };
        }
    }

    void OnEnableDisableVertexAttribArray(uint32_t index, bool enable) {
        if (index < 16u) {
            const uint16_t bit = static_cast<uint16_t>(1u << index);
            const bool wasEnabled = (attribEnMask_ & bit) != 0;
            if (wasEnabled != enable) {
                if (HasPending()) Flush(FlushReason::State);
                EndStickyBatchSession();
                if (enable) attribEnMask_ |= bit;
                else        attribEnMask_ &= static_cast<uint16_t>(~bit);
            }
        }
    }

    // Absorbs MatrixW without flushing other mat4 uniforms (caller runs uniform-cache dedup first!)
    bool TryAbsorbMatrixW(int32_t location, int32_t count, uint8_t transpose, const float* value) noexcept {
        if (enabled_ && curProg_ && curProg_->eligible &&
            location >= 0 && location == curProg_->locMatrixW &&
            count == 1 && transpose == 0 && value != nullptr) {
            std::memcpy(curMatrixW_, value, sizeof(curMatrixW_));
            hasMatrixW_   = true;
            matrixWDirty_ = true;
            return true;
        }
        return false;
    }

    // Absorbs affine diagonal+alpha-add COLOUR_XFORM (99.9% of DST entity tints: SetMultColour + SetAddColour!)
    // so differently-tinted trees/mobs/props coalesce into ONE batch instead of flushing 94% of batches!
    // Non-affine color matrices (hue rotation/grayscale with non-zero off-diagonals) fail-closed to vanilla GL!
    bool TryAbsorbColourXform(int32_t location, int32_t count, uint8_t transpose, const float* value) noexcept {
        if (!curProg_ || !curProg_->eligible ||
            location < 0 || location != curProg_->locColourXform ||
            curProg_->locBatchCMult < 0 || curProg_->locBatchCAdd < 0) {
            return false;
        }
        if (enabled_ && count == 1 && transpose == 0 && value != nullptr) {
            // Check 9 off-diagonal non-alpha-add entries of column-major mat4 for `colour.rgba * COLOUR_XFORM`:
            // res.r = r*m[0] + g*m[1] + b*m[2] + a*m[3]
            // res.g = r*m[4] + g*m[5] + b*m[6] + a*m[7]
            // res.b = r*m[8] + g*m[9] + b*m[10] + a*m[11]
            // res.a = r*m[12] + g*m[13] + b*m[14] + a*m[15]
            constexpr float kEps = 1e-6f;
            if (fabsf(value[1]) <= kEps && fabsf(value[2]) <= kEps &&
                fabsf(value[4]) <= kEps && fabsf(value[6]) <= kEps &&
                fabsf(value[8]) <= kEps && fabsf(value[9]) <= kEps &&
                fabsf(value[12]) <= kEps && fabsf(value[13]) <= kEps && fabsf(value[14]) <= kEps) {
                std::memcpy(curColourXform_, value, sizeof(curColourXform_));
                curCMult_[0] = value[0];
                curCMult_[1] = value[5];
                curCMult_[2] = value[10];
                curCMult_[3] = value[15];
                curCAdd_[0]  = value[3];
                curCAdd_[1]  = value[7];
                curCAdd_[2]  = value[11];
                curCAdd_[3]  = 0.0f;
                hasColourXform_   = true;
                colourXformDirty_ = true;
                return true;
            }
        }
        // Non-affine COLOUR_XFORM or disabled: fail-closed -> caller flushes & uploads exact mat4 to GL!
        hasColourXform_   = false;
        colourXformDirty_ = false;
        return false;
    }

    // Returns true if MatrixW or affine COLOUR_XFORM upload was absorbed/deferred (caller skips GL call!)
    bool OnUniformMatrix4fv(int32_t location, int32_t count, uint8_t transpose, const float* value) {
        if (TryAbsorbMatrixW(location, count, transpose, value) ||
            TryAbsorbColourXform(location, count, transpose, value)) {
            return true; // Absorbed! Zero GL uniform call while batching!
        }
        if (HasPending()) Flush(FlushReason::UniMat);
        return false;
    }

    void OnUniform1i(int32_t location, int32_t val, bool flushIfPending = true) {
        if (curProg_ && curProg_->eligible) {
            if (location == curProg_->locSampler0) { curProg_->samplerUnit0 = val; return; }
            if (location == curProg_->locSampler1) { curProg_->samplerUnit1 = val; return; }
            if (location == curProg_->locSampler5) { curProg_->samplerUnit5 = val; return; }
        }
        if (flushIfPending && HasPending()) Flush(FlushReason::UniOther);
    }

    bool TryAbsorbFloatParams(int32_t location, int32_t count, const float* v) {
        if (!enabled_ || !curProg_ || !curProg_->eligible || !curProg_->hasGpuDriven ||
            location < 0 || location != curProg_->locFloatParams ||
            count != 1 || v == nullptr || !ops_.glUniform3fv ||
            !CanUseGpuDrivenDraw()) {
            return false;
        }
        if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) {
            return false;
        }
        if (curProg_->hasFloatParamsValue &&
            curProg_->lastFloatParams[0] == v[0] &&
            curProg_->lastFloatParams[1] == v[1] &&
            curProg_->lastFloatParams[2] == v[2]) {
            return true;
        }
        if (HasPending() && !curBatchGpuMode_) {
            Flush(FlushReason::Uni3);
        }
        std::memcpy(curProg_->lastFloatParams, v, 3u * sizeof(float));
        curProg_->lastFloatParamsZ    = v[2];
        curProg_->hasFloatParamsValue = true;
        curProg_->floatParamsDirty    = true;
        return true;
    }

    bool TryAbsorbParams(int32_t location, int32_t count, const float* v) {
        if (!enabled_ || !curProg_ || !curProg_->eligible || !curProg_->hasGpuDriven ||
            curProg_->isBloomPass || !curProg_->hasParamsAbsorption ||
            location < 0 || location != curProg_->locParams ||
            count != 1 || v == nullptr || !ops_.glUniform3fv ||
            !CanUseGpuDrivenDraw()) {
            return false;
        }
        if (!std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2])) {
            return false;
        }
        const bool changed = !curProg_->hasParamsValue ||
                             curProg_->lastParams[0] != v[0] ||
                             curProg_->lastParams[1] != v[1] ||
                             curProg_->lastParams[2] != v[2];
        if (!changed) {
            return true;
        }
        if (HasPending() && !curBatchGpuMode_) {
            Flush(FlushReason::Uni3);
        }
        std::memcpy(curProg_->lastParams, v, 3u * sizeof(float));
        curProg_->lastParamsY    = v[1];
        curProg_->hasParamsValue = true;
        curProg_->paramsDirty    = true;
        return true;
    }

    bool TryAbsorbOceanBlend(int32_t location, int32_t count, const float* v) {
        if (!enabled_ || !curProg_ || !curProg_->eligible || !curProg_->hasGpuDriven ||
            location < 0 || location != curProg_->locOceanBlend ||
            count != 1 || v == nullptr || !ops_.glUniform4fv ||
            !CanUseGpuDrivenDraw()) {
            return false;
        }
        if (HasPending() && !curBatchGpuMode_) {
            Flush(FlushReason::Uni4);
        }
        std::memcpy(curProg_->lastOceanBlend, v, 4u * sizeof(float));
        curProg_->oceanBlendDirty = true;
        return true;
    }

    void OnUniform3fv(int32_t location, int32_t count, const float* v, bool flushIfPending = true) {
        if (flushIfPending && HasPending()) Flush(FlushReason::Uni3);
        if (curProg_ && curProg_->eligible && location >= 0 && count == 1 && v != nullptr) {
            if (location == curProg_->locFloatParams) {
                std::memcpy(curProg_->lastFloatParams, v, 3u * sizeof(float));
                curProg_->lastFloatParamsZ    = v[2];
                curProg_->hasFloatParamsValue = true;
                curProg_->floatParamsDirty    = false;
            } else if (location == curProg_->locParams) {
                std::memcpy(curProg_->lastParams, v, 3u * sizeof(float));
                curProg_->lastParamsY    = v[1];
                curProg_->hasParamsValue = true;
                curProg_->paramsDirty    = false;
            }
        }
    }

    void OnUniform4fv(int32_t location, int32_t count, const float* v, bool flushIfPending = true) {
        if (flushIfPending && HasPending()) Flush(FlushReason::Uni4);
        if (curProg_ && curProg_->eligible && location >= 0 && count == 1 && v != nullptr) {
            if (location == curProg_->locOceanBlend) {
                std::memcpy(curProg_->lastOceanBlend, v, 4u * sizeof(float));
                curProg_->oceanBlendDirty = false;
            }
        }
    }

    void OnAnyStateOrUniformChange(FlushReason reason = FlushReason::State) {
        if (HasPending()) Flush(reason);
    }

    // Must be called immediately before any unbatched glDrawArrays / glDrawElements
    void PrepareForUnbatchedDraw() {
        if (HasPending()) Flush(FlushReason::Bypass);
        EndStickyBatchSession();
        CommitDeferredMatrixW();
        boundSsboId_ = 0u;
        ++stats_.bypassDraws;
    }

    void CommitDeferredPerEntityUniforms() {
        if (!curProg_) return;
        if (curProg_->floatParamsDirty && curProg_->locFloatParams >= 0 && ops_.glUniform3fv) {
            ops_.glUniform3fv(curProg_->locFloatParams, 1, curProg_->lastFloatParams);
            curProg_->floatParamsDirty = false;
        }
        if (curProg_->paramsDirty && curProg_->locParams >= 0 && ops_.glUniform3fv) {
            ops_.glUniform3fv(curProg_->locParams, 1, curProg_->lastParams);
            curProg_->paramsDirty = false;
        }
        if (curProg_->oceanBlendDirty && curProg_->locOceanBlend >= 0 && ops_.glUniform4fv) {
            ops_.glUniform4fv(curProg_->locOceanBlend, 1, curProg_->lastOceanBlend);
            curProg_->oceanBlendDirty = false;
        }
    }

    void CommitDeferredMatrixW() {
        if (!curProg_) return;
        if (ops_.glUniformMatrix4fv) {
            if (matrixWDirty_ && curProg_->locMatrixW >= 0) {
                ops_.glUniformMatrix4fv(curProg_->locMatrixW, 1, 0, curMatrixW_);
                matrixWDirty_ = false;
            }
            if (colourXformDirty_ && curProg_->locColourXform >= 0) {
                ops_.glUniformMatrix4fv(curProg_->locColourXform, 1, 0, curColourXform_);
                colourXformDirty_ = false;
            }
        }
        CommitDeferredPerEntityUniforms();
    }

    static inline float Smoothstep01(float e0, float e1, float x) noexcept {
        float t = (x - e0) / (e1 - e0);
        if (t <= 0.0f) return 0.0f;
        if (t >= 1.0f) return 1.0f;
        return t * t * (3.0f - 2.0f * t);
    }

    // Returns true if draw call was absorbed into the sprite batch!
    bool TryBatchDrawArrays(uint32_t mode, int32_t first, int32_t count,
                            uint8_t foliageClass = 0u,
                            float foliageSwayBase = 0.0f,
                            float foliageWakeBase = 0.0f) {
        if (!CanBatchBase(mode) || first < 0 || count < 3 ||
            static_cast<uint32_t>(count) > kMaxSingleDrawVerts) {
            return false;
        }
        const bool isStrip = (mode == kGlTriangleStrip);
        if (!isStrip && (count % 3) != 0) return false;
        const uint32_t vCount = static_cast<uint32_t>(count);
        const uint32_t outIdxCount = isStrip ? ((vCount - 2u) * 3u) : vCount;
        if (outIdxCount > kMaxSingleDrawIndices) return false;

        const int32_t posLoc = curProg_->locPos2dUv;
        const AttribState& attr = attribs_[posLoc];
        const size_t stride = (attr.stride == 0) ? 16u : static_cast<size_t>(attr.stride);
        const int64_t rT0 = ProfNow();
        const uint8_t* vBase = ResolveVertexPtr(attr, static_cast<size_t>(first + count) * stride);
        if (rT0 != 0) {
            const int64_t rT1 = ops_.nowQpc();
            if (rT1 > rT0) frameProf_.tryResolveQpc += (rT1 - rT0);
        }
        if (!vBase) { lastRejectCode_ = 20; return false; }

        ++stats_.drawsIn;
        if (TryBatchGpuDrivenInternal(vBase, stride, static_cast<uint32_t>(first), vCount,
                                      nullptr, 0u, outIdxCount, isStrip,
                                      foliageClass, foliageSwayBase, foliageWakeBase)) {
            return true;
        }
        if (curProg_->lastFloatParamsZ > 0.0f) {
            lastRejectCode_ = 5;
            return false; // Ocean-bobbing sprites without GPU-Driven stay on exact vanilla path
        }
        if (HasPending() && curBatchGpuMode_) {
            FlushFromTryBatch(FlushReason::Other);
            if (!frameGpuRingFull_) {
                NoteGpuModeSwitch();
            }
        }
        CommitDeferredPerEntityUniforms();
        if (vertCount_ + vCount > kMaxBatchVerts || indexCount_ + outIdxCount > kMaxBatchIndices) {
            FlushFromTryBatch(FlushReason::Full);
        }
        if (!EnsureSamplerSlotsForVertices(vBase, stride, static_cast<uint32_t>(first), vCount, nullptr, 0)) {
            lastRejectCode_ = 21;
            return false;
        }
        curBatchGpuMode_ = false;

        const uint32_t baseOutVert = vertCount_;
        const float* m = curMatrixW_;
        const bool needMeta = shadowPassActive_ || (foliageClass != 0u);
        float rux = 0.0f, ruy = 0.0f, ruz = 0.0f;
        bool canShadow = false;
        bool doFoliage = false;
        float swayAxisX = 0.70710678f, swayAxisZ = -0.70710678f;
        if (needMeta) {
            if (!hasCamRight_) {
                const float rLen = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
                const float invR = (rLen > 1e-4f) ? (1.0f / rLen) : 1.0f;
                rux = m[0] * invR; ruy = m[1] * invR; ruz = m[2] * invR;
            }
            const bool isStanding = IsCurrentMatrixWStanding();
            canShadow = shadowPassActive_ && isStanding &&
                        (!skipTranslucentShadows_ || curCMult_[3] >= 0.72f) &&
                        (curProg_->lastParamsY <= 0.35f);
            doFoliage = isStanding &&
                ((foliageClass == 1u && (std::fabs(foliageSwayBase) > 1e-4f || std::fabs(foliageWakeBase) > 1e-4f)) ||
                 (foliageClass == 2u && std::fabs(foliageSwayBase) > 1e-4f));
            if (doFoliage) {
                const float ax = hasCamRight_ ? camRightX_ : rux;
                const float az = hasCamRight_ ? camRightZ_ : ruz;
                const float aLen = std::sqrt(ax * ax + az * az);
                if (aLen > 1e-4f) {
                    swayAxisX = ax / aLen;
                    swayAxisZ = az / aLen;
                }
            }
        }
        const float shadowFlag = canShadow ? 1.0f : 0.0f;
        const bool singleSlot  = curSingleSlot_;
        const float singleSlotF = curSiSlots_[0];
        float maxWy = -1e9f;
        for (uint32_t k = 0; k < vCount; ++k) {
            const float* src = reinterpret_cast<const float*>(vBase + (static_cast<size_t>(first) + k) * stride);
            const float x = src[0], y = src[1], uEnc = src[2], v = src[3];
            const float slotF = singleSlot ? singleSlotF : ResolvePreSlot(uEnc);
            const float dx = m[0] * x + m[4] * y;
            const float dy = m[1] * x + m[5] * y;
            const float dz = m[2] * x + m[6] * y;
            float wx = dx + m[12];
            const float wy = dy + m[13];
            if (canShadow && wy > maxWy) maxWy = wy;
            float wz = dz + m[14];
            float horizOff = 0.0f;
            if (doFoliage && dy > 0.06f) {
                if (foliageClass == 1u) {
                    horizOff = foliageSwayBase * Smoothstep01(0.08f, 1.35f, dy) +
                               foliageWakeBase * Smoothstep01(0.06f, 1.10f, dy);
                } else {
                    const float b2 = Smoothstep01(1.00f, 3.80f, dy);
                    const float hc = (dy < 8.0f) ? dy : 8.0f;
                    horizOff = foliageSwayBase * (b2 * b2) * (0.18f + 0.025f * hc);
                }
                wx += swayAxisX * horizOff;
                wz += swayAxisZ * horizOff;
            }
            BatchedVertex& dst = verts_[vertCount_++];
            dst.x    = shadowFlag;
            dst.y    = m[13];
            dst.u    = uEnc;
            dst.v    = v;
            dst.wx   = wx;
            dst.wy   = wy;
            dst.wz   = wz;
            dst.slot = slotF;
            dst.mr   = curCMult_[0];
            dst.mg   = curCMult_[1];
            dst.mb   = curCMult_[2];
            dst.ma   = curCMult_[3];
            dst.ar   = curCAdd_[0];
            dst.ag   = curCAdd_[1];
            dst.ab   = curCAdd_[2];
            dst.pad  = needMeta
                ? ((hasCamRight_ ? (dx * camRightX_ + dz * camRightZ_)
                                 : (dx * rux + dy * ruy + dz * ruz)) + horizOff)
                : 0.0f;
        }
        if (canShadow) {
            if (minShadowHeight_ <= 0.01f || maxWy >= minShadowHeight_) {
                batchHasShadowCaster_ = true;
                if (m[12] < shadowMinX_) shadowMinX_ = m[12];
                if (m[12] > shadowMaxX_) shadowMaxX_ = m[12];
                if (m[14] < shadowMinZ_) shadowMinZ_ = m[14];
                if (m[14] > shadowMaxZ_) shadowMaxZ_ = m[14];
            } else {
                for (uint32_t k = 0; k < vCount; ++k) {
                    verts_[baseOutVert + k].x = 0.0f;
                }
            }
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

    // Returns true if draw call was absorbed into the sprite batch!
    bool TryBatchDrawElements(uint32_t mode, int32_t count, uint32_t type, const void* indicesPtr,
                              uint8_t foliageClass = 0u,
                              float foliageSwayBase = 0.0f,
                              float foliageWakeBase = 0.0f) {
        if (!CanBatchBase(mode) || type != kGlUnsignedShort || count < 3) {
            return false;
        }
        const bool isStrip = (mode == kGlTriangleStrip);
        if (!isStrip && (count % 3) != 0) return false;
        const uint32_t rawCount = static_cast<uint32_t>(count);
        if (rawCount > kMaxSingleDrawIndices) return false;

        const int64_t riT0 = ProfNow();
        const uint16_t* idxSrc = ResolveIndexPtr(indicesPtr, static_cast<size_t>(rawCount) * sizeof(uint16_t));
        if (riT0 != 0) {
            const int64_t riT1 = ops_.nowQpc();
            if (riT1 > riT0) frameProf_.tryResolveQpc += (riT1 - riT0);
        }
        if (!idxSrc) { lastRejectCode_ = 22; return false; }

        uint32_t minIdx = 0xFFFFFFFFu, maxIdx = 0u;
        uint32_t outIdxCount = rawCount;
        if (!isStrip) {
            uint32_t k = 0u;
            if (rawCount >= 8u) {
                const __m128i kBias = _mm_set1_epi16(static_cast<short>(0x8000u));
                __m128i vMin = _mm_set1_epi16(0x7FFF);
                __m128i vMax = _mm_set1_epi16(static_cast<short>(0x8000u));
                for (; k + 7u < rawCount; k += 8u) {
                    const __m128i v = _mm_xor_si128(
                        _mm_loadu_si128(reinterpret_cast<const __m128i*>(idxSrc + k)), kBias);
                    vMin = _mm_min_epi16(vMin, v);
                    vMax = _mm_max_epi16(vMax, v);
                }
                vMin = _mm_min_epi16(vMin, _mm_srli_si128(vMin, 8));
                vMin = _mm_min_epi16(vMin, _mm_srli_si128(vMin, 4));
                vMin = _mm_min_epi16(vMin, _mm_srli_si128(vMin, 2));
                vMax = _mm_max_epi16(vMax, _mm_srli_si128(vMax, 8));
                vMax = _mm_max_epi16(vMax, _mm_srli_si128(vMax, 4));
                vMax = _mm_max_epi16(vMax, _mm_srli_si128(vMax, 2));
                minIdx = static_cast<uint32_t>(static_cast<uint16_t>(_mm_cvtsi128_si32(vMin) ^ 0x8000));
                maxIdx = static_cast<uint32_t>(static_cast<uint16_t>(_mm_cvtsi128_si32(vMax) ^ 0x8000));
            }
            for (; k < rawCount; ++k) {
                const uint32_t ix = idxSrc[k];
                if (ix < minIdx) minIdx = ix;
                if (ix > maxIdx) maxIdx = ix;
            }
        } else {
            outIdxCount = 0u;
            for (uint32_t t = 0; t + 2u < rawCount; ++t) {
                const uint32_t a = idxSrc[t], b = idxSrc[t + 1u], c = idxSrc[t + 2u];
                if (a == b || b == c || a == c) continue; // skip degenerate stitch triangles!
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
                return true; // fully degenerate strip -> clean 0-tri absorb
            }
            if (outIdxCount > kMaxSingleDrawIndices) return false;
        }
        if (minIdx > maxIdx) return false;
        const uint32_t spanVerts = (maxIdx - minIdx) + 1u;
        if (spanVerts > kMaxSingleDrawVerts) return false;

        const int32_t posLoc = curProg_->locPos2dUv;
        const AttribState& attr = attribs_[posLoc];
        const size_t stride = (attr.stride == 0) ? 16u : static_cast<size_t>(attr.stride);
        const int64_t rvT0 = ProfNow();
        const uint8_t* vBase = ResolveVertexPtr(attr, static_cast<size_t>(maxIdx + 1u) * stride);
        if (rvT0 != 0) {
            const int64_t rvT1 = ops_.nowQpc();
            if (rvT1 > rvT0) frameProf_.tryResolveQpc += (rvT1 - rvT0);
        }
        if (!vBase) { lastRejectCode_ = 23; return false; }

        ++stats_.drawsIn;
        if (TryBatchGpuDrivenInternal(vBase, stride, minIdx, spanVerts,
                                      idxSrc, rawCount, outIdxCount, isStrip,
                                      foliageClass, foliageSwayBase, foliageWakeBase)) {
            return true;
        }
        if (curProg_->lastFloatParamsZ > 0.0f) {
            lastRejectCode_ = 5;
            return false; // Ocean-bobbing sprites without GPU-Driven stay on exact vanilla path
        }
        if (HasPending() && curBatchGpuMode_) {
            FlushFromTryBatch(FlushReason::Other);
            if (!frameGpuRingFull_) {
                NoteGpuModeSwitch();
            }
        }
        CommitDeferredPerEntityUniforms();
        if (vertCount_ + spanVerts > kMaxBatchVerts || indexCount_ + outIdxCount > kMaxBatchIndices) {
            FlushFromTryBatch(FlushReason::Full);
        }
        if (!EnsureSamplerSlotsForVertices(vBase, stride, minIdx, spanVerts, idxSrc, rawCount)) {
            lastRejectCode_ = 24;
            return false;
        }
        curBatchGpuMode_ = false;

        const uint32_t baseOutVert = vertCount_;
        const float* m = curMatrixW_;
        const bool needMeta = shadowPassActive_ || (foliageClass != 0u);
        float rux = 0.0f, ruy = 0.0f, ruz = 0.0f;
        bool canShadow = false;
        bool doFoliage = false;
        float swayAxisX = 0.70710678f, swayAxisZ = -0.70710678f;
        if (needMeta) {
            if (!hasCamRight_) {
                const float rLen = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
                const float invR = (rLen > 1e-4f) ? (1.0f / rLen) : 1.0f;
                rux = m[0] * invR; ruy = m[1] * invR; ruz = m[2] * invR;
            }
            const bool isStanding = IsCurrentMatrixWStanding();
            canShadow = shadowPassActive_ && isStanding &&
                        (!skipTranslucentShadows_ || curCMult_[3] >= 0.72f) &&
                        (curProg_->lastParamsY <= 0.35f);
            doFoliage = isStanding &&
                ((foliageClass == 1u && (std::fabs(foliageSwayBase) > 1e-4f || std::fabs(foliageWakeBase) > 1e-4f)) ||
                 (foliageClass == 2u && std::fabs(foliageSwayBase) > 1e-4f));
            if (doFoliage) {
                const float ax = hasCamRight_ ? camRightX_ : rux;
                const float az = hasCamRight_ ? camRightZ_ : ruz;
                const float aLen = std::sqrt(ax * ax + az * az);
                if (aLen > 1e-4f) {
                    swayAxisX = ax / aLen;
                    swayAxisZ = az / aLen;
                }
            }
        }
        const float shadowFlag  = canShadow ? 1.0f : 0.0f;
        const bool  singleSlot  = curSingleSlot_;
        const float singleSlotF = curSiSlots_[0];
        // Copy only the referenced vertex span [minIdx .. maxIdx] into our batch VBO!
        float maxWy = -1e9f;
        uint32_t k = 0;
        if (!doFoliage && spanVerts >= 4u && stride == 16u) {
            const __m128 vm0  = _mm_set1_ps(m[0]);
            const __m128 vm1  = _mm_set1_ps(m[1]);
            const __m128 vm2  = _mm_set1_ps(m[2]);
            const __m128 vm4  = _mm_set1_ps(m[4]);
            const __m128 vm5  = _mm_set1_ps(m[5]);
            const __m128 vm6  = _mm_set1_ps(m[6]);
            const __m128 vm12 = _mm_set1_ps(m[12]);
            const __m128 vm13 = _mm_set1_ps(m[13]);
            const __m128 vm14 = _mm_set1_ps(m[14]);
            const float* srcBaseF = reinterpret_cast<const float*>(vBase + static_cast<size_t>(minIdx) * 16u);
            if (!needMeta) {
                for (; k + 3u < spanVerts; k += 4u) {
                    const float* sf = srcBaseF + static_cast<size_t>(k) * 4u;
                    __m128 vx = _mm_loadu_ps(sf);
                    __m128 vy = _mm_loadu_ps(sf + 4);
                    __m128 vu = _mm_loadu_ps(sf + 8);
                    __m128 vv = _mm_loadu_ps(sf + 12);
                    _MM_TRANSPOSE4_PS(vx, vy, vu, vv);

                    const __m128 wxv = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vm0, vx), _mm_mul_ps(vm4, vy)), vm12);
                    const __m128 wyv = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vm1, vx), _mm_mul_ps(vm5, vy)), vm13);
                    const __m128 wzv = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vm2, vx), _mm_mul_ps(vm6, vy)), vm14);

                    alignas(16) float wxArr[4], wyArr[4], wzArr[4], uArr[4], vArr[4];
                    _mm_store_ps(wxArr, wxv);
                    _mm_store_ps(wyArr, wyv);
                    _mm_store_ps(wzArr, wzv);
                    _mm_store_ps(uArr, vu);
                    _mm_store_ps(vArr, vv);
                    for (uint32_t lane = 0; lane < 4u; ++lane) {
                        BatchedVertex& dst = verts_[vertCount_++];
                        dst.x    = 0.0f;
                        dst.y    = m[13];
                        dst.u    = uArr[lane];
                        dst.v    = vArr[lane];
                        dst.wx   = wxArr[lane];
                        dst.wy   = wyArr[lane];
                        dst.wz   = wzArr[lane];
                        dst.slot = singleSlot ? singleSlotF : ResolvePreSlot(uArr[lane]);
                        dst.mr   = curCMult_[0];
                        dst.mg   = curCMult_[1];
                        dst.mb   = curCMult_[2];
                        dst.ma   = curCMult_[3];
                        dst.ar   = curCAdd_[0];
                        dst.ag   = curCAdd_[1];
                        dst.ab   = curCAdd_[2];
                        dst.pad  = 0.0f;
                    }
                }
            } else {
                const __m128 vpax = _mm_set1_ps(hasCamRight_ ? camRightX_ : rux);
                const __m128 vpay = _mm_set1_ps(hasCamRight_ ? 0.0f : ruy);
                const __m128 vpaz = _mm_set1_ps(hasCamRight_ ? camRightZ_ : ruz);
                __m128 vMaxWy     = _mm_set1_ps(maxWy);
                for (; k + 3u < spanVerts; k += 4u) {
                    const float* sf = srcBaseF + static_cast<size_t>(k) * 4u;
                    __m128 vx = _mm_loadu_ps(sf);
                    __m128 vy = _mm_loadu_ps(sf + 4);
                    __m128 vu = _mm_loadu_ps(sf + 8);
                    __m128 vv = _mm_loadu_ps(sf + 12);
                    _MM_TRANSPOSE4_PS(vx, vy, vu, vv);

                    const __m128 dxv = _mm_add_ps(_mm_mul_ps(vm0, vx), _mm_mul_ps(vm4, vy));
                    const __m128 dyv = _mm_add_ps(_mm_mul_ps(vm1, vx), _mm_mul_ps(vm5, vy));
                    const __m128 dzv = _mm_add_ps(_mm_mul_ps(vm2, vx), _mm_mul_ps(vm6, vy));

                    const __m128 wxv = _mm_add_ps(dxv, vm12);
                    const __m128 wyv = _mm_add_ps(dyv, vm13);
                    const __m128 wzv = _mm_add_ps(dzv, vm14);
                    const __m128 padv = _mm_add_ps(
                        _mm_add_ps(_mm_mul_ps(dxv, vpax), _mm_mul_ps(dyv, vpay)),
                        _mm_mul_ps(dzv, vpaz));
                    vMaxWy = _mm_max_ps(vMaxWy, wyv);

                    alignas(16) float wxArr[4], wyArr[4], wzArr[4], padArr[4], uArr[4], vArr[4];
                    _mm_store_ps(wxArr, wxv);
                    _mm_store_ps(wyArr, wyv);
                    _mm_store_ps(wzArr, wzv);
                    _mm_store_ps(padArr, padv);
                    _mm_store_ps(uArr, vu);
                    _mm_store_ps(vArr, vv);
                    for (uint32_t lane = 0; lane < 4u; ++lane) {
                        BatchedVertex& dst = verts_[vertCount_++];
                        dst.x    = shadowFlag;
                        dst.y    = m[13];
                        dst.u    = uArr[lane];
                        dst.v    = vArr[lane];
                        dst.wx   = wxArr[lane];
                        dst.wy   = wyArr[lane];
                        dst.wz   = wzArr[lane];
                        dst.slot = singleSlot ? singleSlotF : ResolvePreSlot(uArr[lane]);
                        dst.mr   = curCMult_[0];
                        dst.mg   = curCMult_[1];
                        dst.mb   = curCMult_[2];
                        dst.ma   = curCMult_[3];
                        dst.ar   = curCAdd_[0];
                        dst.ag   = curCAdd_[1];
                        dst.ab   = curCAdd_[2];
                        dst.pad  = padArr[lane];
                    }
                }
                alignas(16) float maxArr[4];
                _mm_store_ps(maxArr, vMaxWy);
                for (int lane = 0; lane < 4; ++lane) {
                    if (maxArr[lane] > maxWy) maxWy = maxArr[lane];
                }
            }
        }
        for (; k < spanVerts; ++k) {
            const float* src = reinterpret_cast<const float*>(vBase + (static_cast<size_t>(minIdx) + k) * stride);
            const float x = src[0], y = src[1], uEnc = src[2], v = src[3];
            const float slotF = singleSlot ? singleSlotF : ResolvePreSlot(uEnc);
            const float dx = m[0] * x + m[4] * y;
            const float dy = m[1] * x + m[5] * y;
            const float dz = m[2] * x + m[6] * y;
            float wx = dx + m[12];
            const float wy = dy + m[13];
            if (canShadow && wy > maxWy) maxWy = wy;
            float wz = dz + m[14];
            float horizOff = 0.0f;
            if (doFoliage && dy > 0.06f) {
                if (foliageClass == 1u) {
                    horizOff = foliageSwayBase * Smoothstep01(0.08f, 1.35f, dy) +
                               foliageWakeBase * Smoothstep01(0.06f, 1.10f, dy);
                } else {
                    const float b2 = Smoothstep01(1.00f, 3.80f, dy);
                    const float hc = (dy < 8.0f) ? dy : 8.0f;
                    horizOff = foliageSwayBase * (b2 * b2) * (0.18f + 0.025f * hc);
                }
                wx += swayAxisX * horizOff;
                wz += swayAxisZ * horizOff;
            }
            BatchedVertex& dst = verts_[vertCount_++];
            dst.x    = shadowFlag;
            dst.y    = m[13];
            dst.u    = uEnc;
            dst.v    = v;
            dst.wx   = wx;
            dst.wy   = wy;
            dst.wz   = wz;
            dst.slot = slotF;
            dst.mr   = curCMult_[0];
            dst.mg   = curCMult_[1];
            dst.mb   = curCMult_[2];
            dst.ma   = curCMult_[3];
            dst.ar   = curCAdd_[0];
            dst.ag   = curCAdd_[1];
            dst.ab   = curCAdd_[2];
            dst.pad  = needMeta
                ? ((hasCamRight_ ? (dx * camRightX_ + dz * camRightZ_)
                                 : (dx * rux + dy * ruy + dz * ruz)) + horizOff)
                : 0.0f;
        }
        if (canShadow) {
            if (minShadowHeight_ <= 0.01f || maxWy >= minShadowHeight_) {
                batchHasShadowCaster_ = true;
                if (m[12] < shadowMinX_) shadowMinX_ = m[12];
                if (m[12] > shadowMaxX_) shadowMaxX_ = m[12];
                if (m[14] < shadowMinZ_) shadowMinZ_ = m[14];
                if (m[14] > shadowMaxZ_) shadowMaxZ_ = m[14];
            } else {
                for (uint32_t k = 0; k < spanVerts; ++k) {
                    verts_[baseOutVert + k].x = 0.0f;
                }
            }
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

    void Flush(FlushReason reason = FlushReason::Other) {
        if (indexCount_ == 0u) return;
        const size_t rIdx = static_cast<size_t>(reason);
        if (rIdx < static_cast<size_t>(FlushReason::Count)) {
            ++stats_.flReason[rIdx];
        }
        const bool     isGpuBatch   = curBatchGpuMode_;
        const uint32_t flushVerts   = isGpuBatch ? gpuVertCount_ : vertCount_;
        const uint32_t flushIndices = indexCount_;
        const uint32_t flushInsts   = gpuInstCount_;
        const uint32_t flushTexCnt  = batchTexCount_;
        const bool     mirrorShadow = batchHasShadowCaster_;
        lastFlushHasShadowBounds_ = mirrorShadow && (shadowMinX_ <= shadowMaxX_) && (shadowMinZ_ <= shadowMaxZ_);
        if (lastFlushHasShadowBounds_) {
            lastFlushShadowBounds_[0] = shadowMinX_;
            lastFlushShadowBounds_[1] = shadowMaxX_;
            lastFlushShadowBounds_[2] = shadowMinZ_;
            lastFlushShadowBounds_[3] = shadowMaxZ_;
        }
        vertCount_       = 0;
        gpuVertCount_    = 0;
        gpuInstCount_    = 0;
        indexCount_      = 0;
        batchTexCount_   = 0;
        curBatchGpuMode_ = false;
        batchHasShadowCaster_ = false;
        shadowMinX_ = 1e30f;
        shadowMaxX_ = -1e30f;
        shadowMinZ_ = 1e30f;
        shadowMaxZ_ = -1e30f;

        if (!curProg_ || !curProg_->eligible || !EnsureGpuRingReady()) {
            EndStickyBatchSession();
            return;
        }
        if (!ops_.glBindBuffer || !ops_.glActiveTexture || !ops_.glBindTexture ||
            !ops_.glUniform1f || !ops_.glVertexAttribPointer ||
            !ops_.glEnableVertexAttribArray || !ops_.glDisableVertexAttribArray ||
            !ops_.glDrawElements) {
            EndStickyBatchSession();
            return;
        }

        const uint32_t posLoc  = static_cast<uint32_t>(curProg_->locPos2dUv);
        const uint32_t wposLoc = static_cast<uint32_t>(curProg_->locBatchWpos);

        if (isGpuBatch) {
            if (!gpuDrivenRingActive_ || !gpuVboMapped_ || !gpuSsboMapped_ || !azdoIboMapped_ ||
                !ops_.glBindBufferBase || !ops_.glDrawElementsBaseVertex) {
                EndStickyBatchSession();
                return;
            }
            WaitForAzdoFrameIfNeeded();
            if ((gpuVertCursor_ + flushVerts) > activeGpuFrameMaxVerts_ ||
                (azdoIdxCursor_ + flushIndices) > activeAzdoFrameMaxIndices_ ||
                (gpuInstCursor_ + flushInsts) > activeGpuFrameMaxInsts_) {
                EndStickyBatchSession();
                return;
            }

            // Vertices, indices & SSBO instances were streamed directly into the lockless persistent
            // mapped ring during TryBatchGpuDrivenInternal() -> ZERO per-flush std::memcpy!
            const size_t vBaseVert = static_cast<size_t>(azdoFrameIdx_) * activeGpuFrameMaxVerts_ + gpuVertCursor_;
            const size_t iBaseIdx  = static_cast<size_t>(azdoFrameIdx_) * activeAzdoFrameMaxIndices_ + azdoIdxCursor_;
            const uintptr_t vByteOff = static_cast<uintptr_t>(vBaseVert * sizeof(GpuBatchVertex));
            const uintptr_t iByteOff = static_cast<uintptr_t>(iBaseIdx * sizeof(uint16_t));

            gpuVertCursor_   += flushVerts;
            azdoIdxCursor_   += flushIndices;
            gpuInstCursor_   += flushInsts;
            azdoFrameTouched_ = true;

            lastFlushIsGpuDriven_     = true;
            lastFlushUsedPersistent_  = true;
            lastFlushVboId_           = gpuVboId_;
            lastFlushIboId_           = azdoIboId_;
            lastFlushSsboId_          = gpuSsboId_;
            lastFlushBaseVertex_      = static_cast<int32_t>(vBaseVert);
            lastFlushVertByteOffset_  = vByteOff;
            lastFlushIndexByteOffset_ = iByteOff;
            lastFlushTexCount_        = 0u;

            const int64_t dT0 = ProfNow();
            if (stickySessionActive_ && (stickyMode_ != 2u || stickyProgId_ != curProgId_)) {
                EndStickyBatchSession();
            }

            if (deferHostRestore_) {
                const bool enteredSticky = !stickySessionActive_;
                if (!stickySessionActive_) {
                    ops_.glBindBuffer(kGlArrayBuffer, gpuVboId_);
                    ops_.glBindBuffer(kGlElementArrayBuffer, azdoIboId_);
                    if (boundSsboId_ != gpuSsboId_) {
                        ops_.glBindBufferBase(kGlShaderStorageBuffer, kGpuInstBinding, gpuSsboId_);
                        boundSsboId_ = gpuSsboId_;
                    }
                    ops_.glUniform1f(curProg_->locBatchActive, 2.0f);
                    ops_.glEnableVertexAttribArray(wposLoc);
                    stickySessionActive_     = true;
                    stickyAttribsZeroOffset_ = false;
                    stickyMode_              = 2u;
                    stickyProgId_            = curProgId_;
                    stickyBatchActiveLoc_    = curProg_->locBatchActive;
                    stickyWposLoc_           = static_cast<int32_t>(wposLoc);
                    stickyCMultLoc_          = -1;
                    stickyCAddLoc_           = -1;
                }
                if (!stickyAttribsZeroOffset_) {
                    ops_.glVertexAttribPointer(posLoc, 4, kGlFloat, 0, sizeof(GpuBatchVertex),
                                               reinterpret_cast<const void*>(offsetof(GpuBatchVertex, x)));
                    ops_.glVertexAttribPointer(wposLoc, 1, kGlFloat, 0, sizeof(GpuBatchVertex),
                                               reinterpret_cast<const void*>(offsetof(GpuBatchVertex, instId)));
                    stickyAttribsZeroOffset_ = true;
                }
                ops_.glDrawElementsBaseVertex(kGlTriangles, static_cast<int32_t>(flushIndices), kGlUnsignedShort,
                                              reinterpret_cast<const void*>(iByteOff), static_cast<int32_t>(vBaseVert));
                ++stats_.flushes;
                if (mirrorShadow && ops_.onShadowMirrorBatch) {
                    ops_.onShadowMirrorBatch(flushIndices, ops_.callbackCtx);
                }
                if (enteredSticky && ops_.onStickyFlushSync) {
                    ops_.onStickyFlushSync(false, true, wposLoc, ops_.callbackCtx);
                }
                if (dT0 != 0) {
                    const int64_t dT1 = ops_.nowQpc();
                    if (dT1 > dT0) frameProf_.flushDrawQpc += (dT1 - dT0);
                }
                return;
            }

            ops_.glBindBuffer(kGlArrayBuffer, gpuVboId_);
            ops_.glBindBuffer(kGlElementArrayBuffer, azdoIboId_);
            if (boundSsboId_ != gpuSsboId_) {
                ops_.glBindBufferBase(kGlShaderStorageBuffer, kGpuInstBinding, gpuSsboId_);
                boundSsboId_ = gpuSsboId_;
            }
            ops_.glUniform1f(curProg_->locBatchActive, 2.0f);
            ops_.glVertexAttribPointer(posLoc, 4, kGlFloat, 0, sizeof(GpuBatchVertex),
                                       reinterpret_cast<const void*>(offsetof(GpuBatchVertex, x)));
            ops_.glEnableVertexAttribArray(wposLoc);
            ops_.glVertexAttribPointer(wposLoc, 1, kGlFloat, 0, sizeof(GpuBatchVertex),
                                       reinterpret_cast<const void*>(offsetof(GpuBatchVertex, instId)));
            ops_.glDrawElementsBaseVertex(kGlTriangles, static_cast<int32_t>(flushIndices), kGlUnsignedShort,
                                          reinterpret_cast<const void*>(iByteOff), static_cast<int32_t>(vBaseVert));
            ++stats_.flushes;
            if (mirrorShadow && ops_.onShadowMirrorBatch) {
                ops_.onShadowMirrorBatch(flushIndices, ops_.callbackCtx);
            }
            ops_.glUniform1f(curProg_->locBatchActive, 0.0f);
            ops_.glDisableVertexAttribArray(wposLoc);
            ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
            ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
            if (posLoc < 16u && attribs_[posLoc].valid) {
                const AttribState& a = attribs_[posLoc];
                if (a.buffer != boundArrayBuf_) {
                    ops_.glBindBuffer(kGlArrayBuffer, a.buffer);
                    ops_.glVertexAttribPointer(posLoc, a.size, a.type, a.normalized, a.stride, a.pointer);
                    ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
                } else {
                    ops_.glVertexAttribPointer(posLoc, a.size, a.type, a.normalized, a.stride, a.pointer);
                }
            }
            if (ops_.onPostFlushRestore) {
                ops_.onPostFlushRestore(activeUnit_, boundArrayBuf_, boundElemBuf_, wposLoc, ops_.callbackCtx);
            }
            if (dT0 != 0) {
                const int64_t dT1 = ops_.nowQpc();
                if (dT1 > dT0) frameProf_.flushDrawQpc += (dT1 - dT0);
            }
            return;
        }

        if (stickySessionActive_ && (stickyMode_ != 1u || stickyProgId_ != curProgId_)) {
            EndStickyBatchSession();
        }
        lastFlushIsGpuDriven_ = false;
        lastFlushSsboId_      = 0u;

        // One-time sampler unit binding (HS_BATCH_TEX0..7 -> TEXTURE6..13, 8..15 -> TEXTURE16..23)
        if (!curProg_->samplersInit && ops_.glUniform1i) {
            const uint32_t effSlots = EffectiveMaxTexSlots();
            for (uint32_t s = 0; s < effSlots; ++s) {
                if (curProg_->locBatchTex[s] >= 0) {
                    ops_.glUniform1i(curProg_->locBatchTex[s], static_cast<int32_t>(BatchSlotToTexUnit(s)));
                }
            }
            curProg_->samplersInit = true;
        }

        const size_t vBytes = static_cast<size_t>(flushVerts) * sizeof(BatchedVertex);
        const size_t iBytes = static_cast<size_t>(flushIndices) * sizeof(uint16_t);
        uintptr_t vByteOff  = 0u;
        uintptr_t iByteOff  = 0u;
        size_t    vBaseVert = 0u;
        bool usedPersistent = false;

        if (persistentRingActive_ && azdoVboMapped_ && azdoIboMapped_) {
            WaitForAzdoFrameIfNeeded();
            if ((azdoVertCursor_ + flushVerts) <= activeAzdoFrameMaxVerts_ &&
                (azdoIdxCursor_ + flushIndices) <= activeAzdoFrameMaxIndices_) {
                vBaseVert = static_cast<size_t>(azdoFrameIdx_) * activeAzdoFrameMaxVerts_ + azdoVertCursor_;
                const size_t iBaseIdx  = static_cast<size_t>(azdoFrameIdx_) * activeAzdoFrameMaxIndices_ + azdoIdxCursor_;
                vByteOff = static_cast<uintptr_t>(vBaseVert * sizeof(BatchedVertex));
                iByteOff = static_cast<uintptr_t>(iBaseIdx * sizeof(uint16_t));
                const int64_t uT0 = ProfNow();
                std::memcpy(azdoVboMapped_ + vByteOff, verts_.data(), vBytes);
                std::memcpy(azdoIboMapped_ + iByteOff, indices_.data(), iBytes);
                if (uT0 != 0) {
                    const int64_t uT1 = ops_.nowQpc();
                    if (uT1 > uT0) frameProf_.flushUploadQpc += (uT1 - uT0);
                }
                azdoVertCursor_   += flushVerts;
                azdoIdxCursor_    += flushIndices;
                azdoFrameTouched_  = true;
                usedPersistent = true;
            }
        }
        const bool wantSticky = usedPersistent && deferHostRestore_ && (ops_.glDrawElementsBaseVertex != nullptr);
        if (!wantSticky && stickySessionActive_) {
            EndStickyBatchSession();
        }
        if (usedPersistent && !stickySessionActive_) {
            ops_.glBindBuffer(kGlArrayBuffer, azdoVboId_);
            ops_.glBindBuffer(kGlElementArrayBuffer, azdoIboId_);
        }
        uint32_t usedVboId = azdoVboId_;
        uint32_t usedIboId = azdoIboId_;
        if (!usedPersistent) {
            ++diag_.cpuFallbackRing;
            if (!EnsureFallbackRingReady()) {
                return;
            }
            const uint32_t slot = ringIdx_;
            ringIdx_ = (ringIdx_ + 1u) % kRingSlots;
            usedVboId = vboIds_[slot];
            usedIboId = iboIds_[slot];
            const int64_t uT0 = ProfNow();
            UploadRingSlice(kGlArrayBuffer, usedVboId, verts_.data(), vBytes,
                            static_cast<size_t>(kMaxBatchVerts) * sizeof(BatchedVertex));
            UploadRingSlice(kGlElementArrayBuffer, usedIboId, indices_.data(), iBytes,
                            static_cast<size_t>(kMaxBatchIndices) * sizeof(uint16_t));
            if (uT0 != 0) {
                const int64_t uT1 = ops_.nowQpc();
                if (uT1 > uT0) frameProf_.flushUploadQpc += (uT1 - uT0);
            }
        }
        const int64_t cpuDrawT0 = ProfNow();
        lastFlushUsedPersistent_  = usedPersistent;
        lastFlushVboId_           = usedVboId;
        lastFlushIboId_           = usedIboId;
        lastFlushBaseVertex_      = static_cast<int32_t>(vBaseVert);
        lastFlushVertByteOffset_  = vByteOff;
        lastFlushIndexByteOffset_ = iByteOff;
        lastFlushTexCount_        = flushTexCnt;
        if (flushTexCnt > 0u) {
            std::memcpy(lastFlushTexIds_, batchTexIds_, static_cast<size_t>(flushTexCnt) * sizeof(uint32_t));
        }

        const bool useCol = (curProg_->locBatchCMult >= 0 && curProg_->locBatchCAdd >= 0);

        bool enteredSticky = false;
        bool touchedTexUnits = false;
        if (wantSticky) {
            enteredSticky = !stickySessionActive_;
            if (!stickySessionActive_) {
                for (uint32_t s = 0; s < kMaxBatchTexSlots; ++s) stickySlotTex_[s] = 0u;
                ops_.glUniform1f(curProg_->locBatchActive, 1.0f);
                ops_.glEnableVertexAttribArray(wposLoc);
                if (useCol) {
                    ops_.glEnableVertexAttribArray(static_cast<uint32_t>(curProg_->locBatchCMult));
                    ops_.glEnableVertexAttribArray(static_cast<uint32_t>(curProg_->locBatchCAdd));
                }
                stickySessionActive_     = true;
                stickyMode_              = 1u;
                stickyAttribsZeroOffset_ = false;
                stickyProgId_            = curProgId_;
                stickyBatchActiveLoc_    = curProg_->locBatchActive;
                stickyWposLoc_           = static_cast<int32_t>(wposLoc);
                stickyCMultLoc_          = useCol ? curProg_->locBatchCMult : -1;
                stickyCAddLoc_           = useCol ? curProg_->locBatchCAdd  : -1;
            }
            for (uint32_t s = 0; s < flushTexCnt; ++s) {
                const uint32_t texId = batchTexIds_[s];
                if (stickySlotTex_[s] == texId && texId != 0u) continue;
                ops_.glActiveTexture(kGlTexture0 + BatchSlotToTexUnit(s));
                ops_.glBindTexture(kGlTexture2D, texId);
                stickySlotTex_[s] = texId;
                touchedTexUnits = true;
            }
            if (!stickyAttribsZeroOffset_) {
                ops_.glVertexAttribPointer(posLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                           reinterpret_cast<const void*>(offsetof(BatchedVertex, x)));
                ops_.glVertexAttribPointer(wposLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                           reinterpret_cast<const void*>(offsetof(BatchedVertex, wx)));
                if (useCol) {
                    const uint32_t cmLoc = static_cast<uint32_t>(curProg_->locBatchCMult);
                    const uint32_t caLoc = static_cast<uint32_t>(curProg_->locBatchCAdd);
                    ops_.glVertexAttribPointer(cmLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                               reinterpret_cast<const void*>(offsetof(BatchedVertex, mr)));
                    ops_.glVertexAttribPointer(caLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                               reinterpret_cast<const void*>(offsetof(BatchedVertex, ar)));
                }
                stickyAttribsZeroOffset_ = true;
            }
            ops_.glDrawElementsBaseVertex(kGlTriangles, static_cast<int32_t>(flushIndices), kGlUnsignedShort,
                                          reinterpret_cast<const void*>(iByteOff), static_cast<int32_t>(vBaseVert));
        } else {
            // Bind up to 16 entity atlas textures onto unused units (6..13, 16..23)
            for (uint32_t s = 0; s < flushTexCnt; ++s) {
                ops_.glActiveTexture(kGlTexture0 + BatchSlotToTexUnit(s));
                ops_.glBindTexture(kGlTexture2D, batchTexIds_[s]);
            }

            ops_.glUniform1f(curProg_->locBatchActive, 1.0f);

            ops_.glVertexAttribPointer(posLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                       reinterpret_cast<const void*>(vByteOff + offsetof(BatchedVertex, x)));
            ops_.glEnableVertexAttribArray(wposLoc);
            ops_.glVertexAttribPointer(wposLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                       reinterpret_cast<const void*>(vByteOff + offsetof(BatchedVertex, wx)));
            if (useCol) {
                const uint32_t cmLoc = static_cast<uint32_t>(curProg_->locBatchCMult);
                const uint32_t caLoc = static_cast<uint32_t>(curProg_->locBatchCAdd);
                ops_.glEnableVertexAttribArray(cmLoc);
                ops_.glVertexAttribPointer(cmLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                           reinterpret_cast<const void*>(vByteOff + offsetof(BatchedVertex, mr)));
                ops_.glEnableVertexAttribArray(caLoc);
                ops_.glVertexAttribPointer(caLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                           reinterpret_cast<const void*>(vByteOff + offsetof(BatchedVertex, ar)));
            }

            ops_.glDrawElements(kGlTriangles, static_cast<int32_t>(flushIndices), kGlUnsignedShort,
                                reinterpret_cast<const void*>(iByteOff));
        }
        ++stats_.flushes;

        // Zero-copy World-Space ShadowMask FBO mirror: reuses the ALREADY-UPLOADED Ring VBO/IBO,
        // ALREADY-BOUND GL_TEXTURE6..13 atlases, and ALREADY-ENABLED attribs 0/5/6/7 in 1 draw call!
        if (mirrorShadow && ops_.onShadowMirrorBatch) {
            ops_.onShadowMirrorBatch(flushIndices, ops_.callbackCtx);
        }

        if (wantSticky && (enteredSticky || touchedTexUnits) && ops_.onStickyFlushSync) {
            ops_.onStickyFlushSync(touchedTexUnits, enteredSticky, wposLoc, ops_.callbackCtx);
        }

        if (!wantSticky) {
            // Restore GL state cleanly so subsequent unbatched DST draws & gl_state_cache stay 100% exact!
            // When deferHostRestore_ is active (IAT-sealed lazy bindings in dinput8.cpp), skip restoring
            // Klei's VBO/IBO/attrib0/activeUnit back to the driver; onPostFlushRestore marks them dirty!
            ops_.glUniform1f(curProg_->locBatchActive, 0.0f);
            ops_.glDisableVertexAttribArray(wposLoc);
            if (useCol) {
                ops_.glDisableVertexAttribArray(static_cast<uint32_t>(curProg_->locBatchCMult));
                ops_.glDisableVertexAttribArray(static_cast<uint32_t>(curProg_->locBatchCAdd));
            }
            if (!deferHostRestore_) {
                ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
                ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
                if (posLoc < 16u && attribs_[posLoc].valid) {
                    const AttribState& a = attribs_[posLoc];
                    if (a.buffer != boundArrayBuf_) {
                        ops_.glBindBuffer(kGlArrayBuffer, a.buffer);
                        ops_.glVertexAttribPointer(posLoc, a.size, a.type, a.normalized, a.stride, a.pointer);
                        ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
                    } else {
                        ops_.glVertexAttribPointer(posLoc, a.size, a.type, a.normalized, a.stride, a.pointer);
                    }
                }
                ops_.glActiveTexture(kGlTexture0 + activeUnit_);
            }

            if (ops_.onPostFlushRestore) {
                ops_.onPostFlushRestore(activeUnit_, boundArrayBuf_, boundElemBuf_, wposLoc, ops_.callbackCtx);
            }
        }
        if (cpuDrawT0 != 0) {
            const int64_t cpuDrawT1 = ops_.nowQpc();
            if (cpuDrawT1 > cpuDrawT0) frameProf_.flushDrawQpc += (cpuDrawT1 - cpuDrawT0);
        }
    }

private:
    void WaitForAzdoFrameIfNeeded() noexcept {
        if (azdoFrameWaited_) return;
        if (azdoFrameFences_[azdoFrameIdx_] && ops_.glClientWaitSync && ops_.glDeleteSync) {
            const int64_t wT0 = ProfNow();
            ops_.glClientWaitSync(azdoFrameFences_[azdoFrameIdx_], kGlSyncFlushCommandsBit, 50000000ULL);
            ops_.glDeleteSync(azdoFrameFences_[azdoFrameIdx_]);
            if (wT0 != 0) {
                const int64_t wT1 = ops_.nowQpc();
                if (wT1 > wT0) frameProf_.flushWaitQpc += (wT1 - wT0);
            }
            azdoFrameFences_[azdoFrameIdx_] = nullptr;
        }
        azdoFrameWaited_ = true;
    }

    void EnsureCpuStagingReady() {
        if (verts_.size() < kMaxBatchVerts) verts_.resize(kMaxBatchVerts);
        if (indices_.size() < kMaxBatchIndices) indices_.resize(kMaxBatchIndices);
    }

    bool CanBatchBase(uint32_t mode) {
        if (!enabled_) { lastRejectCode_ = 1; return false; }
        if (ringFailed_) { lastRejectCode_ = 13; return false; }
        if (mode != kGlTriangles && mode != kGlTriangleStrip) { lastRejectCode_ = 2; return false; }
        if (!curProg_ || !curProg_->eligible) { lastRejectCode_ = 3; return false; }
        if (!hasMatrixW_) { lastRejectCode_ = 4; return false; }
        if (curProg_->locColourXform >= 0 && curProg_->locBatchCMult >= 0 &&
            curProg_->locBatchCAdd >= 0 && !hasColourXform_) {
            lastRejectCode_ = 12;
            return false;
        }
        // Ensure strictly affine MatrixW (no projective W row)
        if (fabsf(curMatrixW_[3]) > 1e-5f || fabsf(curMatrixW_[7]) > 1e-5f ||
            fabsf(curMatrixW_[15] - 1.0f) > 1e-5f) {
            lastRejectCode_ = 6;
            return false;
        }
        if (!EnsureTexUnitCaps()) { lastRejectCode_ = 7; return false; }
        const int32_t posLoc = curProg_->locPos2dUv;
        if (posLoc < 0 || posLoc >= 16) { lastRejectCode_ = 8; return false; }
        if (attribEnMask_ != static_cast<uint16_t>(1u << posLoc)) { lastRejectCode_ = 9; return false; }
        const AttribState& attr = attribs_[posLoc];
        if (!attr.valid || attr.size != 4 || attr.type != kGlFloat || attr.normalized != 0) { lastRejectCode_ = 10; return false; }
        if (attr.stride != 0 && attr.stride != 16) { lastRejectCode_ = 11; return false; }
        if (!ops_.glBindBuffer || !ops_.glActiveTexture || !ops_.glBindTexture ||
            !ops_.glUniform1f || !ops_.glVertexAttribPointer ||
            !ops_.glEnableVertexAttribArray || !ops_.glDisableVertexAttribArray ||
            !ops_.glDrawElements) {
            ringFailed_ = true;
            lastRejectCode_ = 13;
            return false;
        }
        // Fail-closed BEFORE accepting the very first sprite draw: if GPU ring allocation fails,
        // reject immediately (and permanently for this context) so ALL draws run on vanilla path!
        if (!ringReady_ && !EnsureGpuRingReady()) {
            lastRejectCode_ = 13;
            return false;
        }
        // Ocean-bobbing sprites (FLOAT_PARAMS.z > 0) are handled bit-exact on GPU-Driven SSBO path;
        // on CPU/ANGLE fallback (no TIMEPARAMS in CPU pre-transform), fail closed to vanilla path!
        if (curProg_->lastFloatParamsZ > 0.0f && !CanUseGpuDrivenDraw()) {
            lastRejectCode_ = 5;
            return false;
        }
        EnsureCpuStagingReady();
        lastRejectCode_ = 0;
        return true;
    }

    bool EnsureTexUnitCaps() noexcept {
        if (texUnitsProbed_) return maxTexSlots_ >= 2u;
        texUnitsProbed_ = true;
        int32_t maxUnits = 16;
        if (ops_.glGetIntegerv) {
            ops_.glGetIntegerv(kGlMaxTextureImageUnits, &maxUnits);
        }
        if (maxUnits >= 24) {
            maxTexSlots_ = kMaxBatchTexSlots; // 16 slots: TEXTURE6..13 + TEXTURE16..23
        } else if (maxUnits >= static_cast<int32_t>(kBatchTexUnitBase + kLegacyBatchTexSlots)) {
            maxTexSlots_ = kLegacyBatchTexSlots; // 8 slots: TEXTURE6..13 (ANGLE D3D9)
        } else if (maxUnits > static_cast<int32_t>(kBatchTexUnitBase)) {
            maxTexSlots_ = static_cast<uint32_t>(maxUnits - static_cast<int32_t>(kBatchTexUnitBase));
        } else {
            maxTexSlots_ = 0;
        }
        return maxTexSlots_ >= 2u;
    }

    bool CanUseAzdoPersistent() const noexcept {
        return persistentRingPreferred_ &&
               ops_.glBufferStorage != nullptr &&
               ops_.glMapBufferRange != nullptr &&
               ops_.glUnmapBuffer != nullptr &&
               ops_.glFenceSync != nullptr &&
               ops_.glClientWaitSync != nullptr &&
               ops_.glDeleteSync != nullptr;
    }

    bool CanAllocateGpuDrivenRing() const noexcept {
        return CanUseAzdoPersistent() &&
               gpuDrivenPreferred_ &&
               ops_.glBindBufferBase != nullptr &&
               ops_.glDrawElementsBaseVertex != nullptr &&
               ops_.glGetTextureHandleARB != nullptr &&
               ops_.glMakeTextureHandleResidentARB != nullptr &&
               ops_.glMakeTextureHandleNonResidentARB != nullptr;
    }

    bool CanUseGpuDrivenDraw() const noexcept {
        return gpuDrivenPreferred_ &&
               gpuDrivenRingActive_ &&
               !gpuDrivenRingFailed_ &&
               !frameGpuLatchedOff_ &&
               !frameGpuRingFull_ &&
               gpuCooldownFrames_ == 0u &&
               curProg_ != nullptr &&
               curProg_->hasGpuDriven &&
               gpuVboMapped_ != nullptr &&
               gpuSsboMapped_ != nullptr &&
               azdoIboMapped_ != nullptr &&
               ops_.glBindBufferBase != nullptr &&
               ops_.glDrawElementsBaseVertex != nullptr &&
               ops_.glGetTextureHandleARB != nullptr &&
               ops_.glMakeTextureHandleResidentARB != nullptr;
    }

    bool EnsureFallbackRingReady() {
        if (fallbackRingReady_) return true;
        if (!ops_.glGenBuffers || !ops_.glBindBuffer || !ops_.glBufferData || !ops_.glGetBufferParameteriv) {
            return false;
        }
        constexpr ptrdiff_t kVboSlotBytes = static_cast<ptrdiff_t>(kMaxBatchVerts * sizeof(BatchedVertex));
        constexpr ptrdiff_t kIboSlotBytes = static_cast<ptrdiff_t>(kMaxBatchIndices * sizeof(uint16_t));
        for (uint32_t i = 0; i < kRingSlots; ++i) {
            vboIds_[i] = 0u;
            iboIds_[i] = 0u;
        }
        ops_.glGenBuffers(static_cast<int32_t>(kRingSlots), vboIds_);
        ops_.glGenBuffers(static_cast<int32_t>(kRingSlots), iboIds_);
        for (uint32_t i = 0; i < kRingSlots; ++i) {
            if (vboIds_[i] == 0u || iboIds_[i] == 0u) {
                ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
                ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
                if (ops_.glDeleteBuffers) {
                    for (uint32_t k = 0; k < kRingSlots; ++k) {
                        if (vboIds_[k] != 0u) { ops_.glDeleteBuffers(1, &vboIds_[k]); vboIds_[k] = 0u; }
                        if (iboIds_[k] != 0u) { ops_.glDeleteBuffers(1, &iboIds_[k]); iboIds_[k] = 0u; }
                    }
                }
                return false;
            }
            ops_.glBindBuffer(kGlArrayBuffer, vboIds_[i]);
            ops_.glBufferData(kGlArrayBuffer, kVboSlotBytes, nullptr, kGlStreamDraw);
            int32_t vAllocBytes = 0;
            ops_.glGetBufferParameteriv(kGlArrayBuffer, kGlBufferSize, &vAllocBytes);

            ops_.glBindBuffer(kGlElementArrayBuffer, iboIds_[i]);
            ops_.glBufferData(kGlElementArrayBuffer, kIboSlotBytes, nullptr, kGlStreamDraw);
            int32_t iAllocBytes = 0;
            ops_.glGetBufferParameteriv(kGlElementArrayBuffer, kGlBufferSize, &iAllocBytes);

            if (vAllocBytes < static_cast<int32_t>(kVboSlotBytes) ||
                iAllocBytes < static_cast<int32_t>(kIboSlotBytes)) {
                ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
                ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
                if (ops_.glDeleteBuffers) {
                    for (uint32_t k = 0; k < kRingSlots; ++k) {
                        if (vboIds_[k] != 0u) { ops_.glDeleteBuffers(1, &vboIds_[k]); vboIds_[k] = 0u; }
                        if (iboIds_[k] != 0u) { ops_.glDeleteBuffers(1, &iboIds_[k]); iboIds_[k] = 0u; }
                    }
                }
                return false;
            }
        }
        ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
        ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
        fallbackRingReady_ = true;
        return true;
    }

    bool EnsureGpuRingReady() {
        if (ringReady_) return true;
        if (ringFailed_) return false;
        if (!ops_.glGenBuffers || !ops_.glBindBuffer || !ops_.glBufferData || !ops_.glGetBufferParameteriv) {
            ringFailed_ = true;
            return false;
        }
        EnsureCpuStagingReady();

        // 1. Lockless 3-Frame-In-Flight Bump-Allocated Persistent-Coherent Ring (GL_ARB_buffer_storage)
        //    Elastic VBO/IBO (and optional GPU-Driven VBO + SSBO), 1 fence per SwapBuffers -> ZERO intra-frame stalls!
        if (!azdoFailed_ && CanUseAzdoPersistent()) {
            uint32_t wantCpuV = std::max<uint32_t>(kAzdoFrameMaxVerts, std::min<uint32_t>(wantGpuFrameMaxVerts_, 262144u));
            const uint32_t wantIdx = std::max<uint32_t>(kAzdoFrameMaxIndices, wantGpuFrameMaxIndices_);
            ptrdiff_t azdoVboBytes = static_cast<ptrdiff_t>(
                static_cast<size_t>(kAzdoFramesInFlight) * wantCpuV * sizeof(BatchedVertex));
            const ptrdiff_t azdoIboBytes = static_cast<ptrdiff_t>(
                static_cast<size_t>(kAzdoFramesInFlight) * wantIdx * sizeof(uint16_t));
            azdoVboId_ = 0u;
            azdoIboId_ = 0u;
            azdoVboMapped_ = nullptr;
            azdoIboMapped_ = nullptr;
            for (uint32_t i = 0; i < kAzdoFramesInFlight; ++i) azdoFrameFences_[i] = nullptr;

            ops_.glGenBuffers(1, &azdoVboId_);
            ops_.glGenBuffers(1, &azdoIboId_);
            bool azdoOk = (azdoVboId_ != 0u && azdoIboId_ != 0u);
            if (azdoOk) {
                ops_.glBindBuffer(kGlArrayBuffer, azdoVboId_);
                ops_.glBufferStorage(kGlArrayBuffer, azdoVboBytes, nullptr, kMapWritePersistentCoherent);
                int32_t vAllocBytes = 0;
                ops_.glGetBufferParameteriv(kGlArrayBuffer, kGlBufferSize, &vAllocBytes);
                if (vAllocBytes < static_cast<int32_t>(azdoVboBytes) && wantCpuV > kAzdoFrameMaxVerts) {
                    if (ops_.glDeleteBuffers) {
                        ops_.glDeleteBuffers(1, &azdoVboId_);
                        azdoVboId_ = 0u;
                        ops_.glGenBuffers(1, &azdoVboId_);
                    }
                    wantCpuV = kAzdoFrameMaxVerts;
                    azdoVboBytes = static_cast<ptrdiff_t>(
                        static_cast<size_t>(kAzdoFramesInFlight) * wantCpuV * sizeof(BatchedVertex));
                    if (azdoVboId_ != 0u) {
                        ops_.glBindBuffer(kGlArrayBuffer, azdoVboId_);
                        ops_.glBufferStorage(kGlArrayBuffer, azdoVboBytes, nullptr, kMapWritePersistentCoherent);
                        vAllocBytes = 0;
                        ops_.glGetBufferParameteriv(kGlArrayBuffer, kGlBufferSize, &vAllocBytes);
                    }
                }
                if (vAllocBytes >= static_cast<int32_t>(azdoVboBytes)) {
                    azdoVboMapped_ = static_cast<uint8_t*>(
                        ops_.glMapBufferRange(kGlArrayBuffer, 0, azdoVboBytes, kMapWritePersistentCoherent));
                }

                ops_.glBindBuffer(kGlElementArrayBuffer, azdoIboId_);
                ops_.glBufferStorage(kGlElementArrayBuffer, azdoIboBytes, nullptr, kMapWritePersistentCoherent);
                int32_t iAllocBytes = 0;
                ops_.glGetBufferParameteriv(kGlElementArrayBuffer, kGlBufferSize, &iAllocBytes);
                if (iAllocBytes >= static_cast<int32_t>(azdoIboBytes)) {
                    azdoIboMapped_ = static_cast<uint8_t*>(
                        ops_.glMapBufferRange(kGlElementArrayBuffer, 0, azdoIboBytes, kMapWritePersistentCoherent));
                }

                if (!azdoVboMapped_ || !azdoIboMapped_ ||
                    vAllocBytes < static_cast<int32_t>(azdoVboBytes) ||
                    iAllocBytes < static_cast<int32_t>(azdoIboBytes)) {
                    azdoOk = false;
                }
            }
            if (azdoOk) {
                activeAzdoFrameMaxVerts_   = wantCpuV;
                activeAzdoFrameMaxIndices_ = wantIdx;
                gpuDrivenRingActive_ = false;
                if (!gpuDrivenRingFailed_ && CanAllocateGpuDrivenRing()) {
                    const uint32_t wantGv = std::max<uint32_t>(kAzdoFrameMaxVerts, wantGpuFrameMaxVerts_);
                    const uint32_t wantGs = std::max<uint32_t>(kMaxGpuBatchInsts, wantGpuFrameMaxInsts_);
                    const ptrdiff_t gpuVboBytes = static_cast<ptrdiff_t>(
                        static_cast<size_t>(kAzdoFramesInFlight) * wantGv * sizeof(GpuBatchVertex));
                    const ptrdiff_t gpuSsboBytes = static_cast<ptrdiff_t>(
                        static_cast<size_t>(kAzdoFramesInFlight) * wantGs * sizeof(GpuSpriteInstance));
                    gpuVboId_ = 0u;
                    gpuSsboId_ = 0u;
                    gpuVboMapped_ = nullptr;
                    gpuSsboMapped_ = nullptr;
                    ops_.glGenBuffers(1, &gpuVboId_);
                    ops_.glGenBuffers(1, &gpuSsboId_);
                    bool gpuOk = (gpuVboId_ != 0u && gpuSsboId_ != 0u);
                    if (gpuOk) {
                        ops_.glBindBuffer(kGlArrayBuffer, gpuVboId_);
                        ops_.glBufferStorage(kGlArrayBuffer, gpuVboBytes, nullptr, kMapWritePersistentCoherent);
                        int32_t gvAlloc = 0;
                        ops_.glGetBufferParameteriv(kGlArrayBuffer, kGlBufferSize, &gvAlloc);
                        if (gvAlloc >= static_cast<int32_t>(gpuVboBytes)) {
                            gpuVboMapped_ = static_cast<uint8_t*>(
                                ops_.glMapBufferRange(kGlArrayBuffer, 0, gpuVboBytes, kMapWritePersistentCoherent));
                        }

                        ops_.glBindBuffer(kGlShaderStorageBuffer, gpuSsboId_);
                        ops_.glBufferStorage(kGlShaderStorageBuffer, gpuSsboBytes, nullptr, kMapWritePersistentCoherent);
                        int32_t gsAlloc = 0;
                        ops_.glGetBufferParameteriv(kGlShaderStorageBuffer, kGlBufferSize, &gsAlloc);
                        if (gsAlloc >= static_cast<int32_t>(gpuSsboBytes)) {
                            gpuSsboMapped_ = static_cast<uint8_t*>(
                                ops_.glMapBufferRange(kGlShaderStorageBuffer, 0, gpuSsboBytes, kMapWritePersistentCoherent));
                        }
                        if (!gpuVboMapped_ || !gpuSsboMapped_ ||
                            gvAlloc < static_cast<int32_t>(gpuVboBytes) ||
                            gsAlloc < static_cast<int32_t>(gpuSsboBytes)) {
                            gpuOk = false;
                        }
                    }
                    if (gpuOk) {
                        activeGpuFrameMaxVerts_ = wantGv;
                        activeGpuFrameMaxInsts_ = wantGs;
                        gpuDrivenRingActive_ = true;
                        ops_.glBindBuffer(kGlShaderStorageBuffer, 0u);
                    } else {
                        if (ops_.glUnmapBuffer) {
                            if (gpuVboId_ != 0u && gpuVboMapped_ != nullptr) {
                                ops_.glBindBuffer(kGlArrayBuffer, gpuVboId_);
                                ops_.glUnmapBuffer(kGlArrayBuffer);
                            }
                            if (gpuSsboId_ != 0u && gpuSsboMapped_ != nullptr) {
                                ops_.glBindBuffer(kGlShaderStorageBuffer, gpuSsboId_);
                                ops_.glUnmapBuffer(kGlShaderStorageBuffer);
                            }
                        }
                        ops_.glBindBuffer(kGlShaderStorageBuffer, 0u);
                        if (ops_.glDeleteBuffers) {
                            if (gpuVboId_ != 0u) ops_.glDeleteBuffers(1, &gpuVboId_);
                            if (gpuSsboId_ != 0u) ops_.glDeleteBuffers(1, &gpuSsboId_);
                        }
                        gpuVboId_ = 0u;
                        gpuSsboId_ = 0u;
                        gpuVboMapped_ = nullptr;
                        gpuSsboMapped_ = nullptr;
                        gpuDrivenRingActive_ = false;
                        gpuDrivenRingFailed_ = true;
                    }
                }
                ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
                ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
                azdoFrameIdx_         = 0u;
                azdoVertCursor_       = 0u;
                azdoIdxCursor_        = 0u;
                gpuVertCursor_        = 0u;
                gpuInstCursor_        = 0u;
                azdoFrameWaited_      = true;
                azdoFrameTouched_     = false;
                persistentRingActive_ = true;
                ringReady_            = true;
                return true;
            }
            ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
            ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
            ReleaseGpuResources();
            azdoFailed_ = true;
        }

        // 2. Portable Standard Ring (glBufferData GL_STREAM_DRAW + per-flush orphan)
        if (!EnsureFallbackRingReady()) {
            ringFailed_ = true;
            return false;
        }
        persistentRingActive_ = false;
        gpuDrivenRingActive_  = false;
        ringReady_ = true;
        return true;
    }

    void UploadRingSlice(uint32_t target, uint32_t bufId, const void* data, size_t bytes, size_t maxSlotBytes) {
        ops_.glBindBuffer(target, bufId);
        // Mode-4 orphan + glBufferSubData (0.15us/call on RTX 4060; eliminates glMapBufferRange 20ms stalls).
        if (ops_.preferOrphanSubData && ops_.glBufferSubData && bytes < maxSlotBytes) {
            ops_.glBufferData(target, static_cast<ptrdiff_t>(maxSlotBytes), nullptr, kGlStreamDraw);
            ops_.glBufferSubData(target, 0, static_cast<ptrdiff_t>(bytes), data);
        } else {
            ops_.glBufferData(target, static_cast<ptrdiff_t>(bytes), data, kGlStreamDraw);
        }
    }

    const uint8_t* ResolveVertexPtr(const AttribState& attr, size_t requiredBytes) {
        const uintptr_t offset = reinterpret_cast<uintptr_t>(attr.pointer);
        if (attr.buffer == 0u) {
            return (attr.pointer != nullptr) ? static_cast<const uint8_t*>(attr.pointer) : nullptr;
        }
        size_t bufSize = 0;
        const uint8_t* base = shadowCache_.Find(attr.buffer, bufSize);
        if (!base && HasLazyGpuReadback()) {
            base = shadowCache_.FetchFromGpu(kGlArrayBuffer, attr.buffer, ops_, boundArrayBuf_, bufSize);
        }
        if (!base || offset > bufSize || requiredBytes > (bufSize - offset)) {
            ++stats_.shadowMisses;
            return nullptr;
        }
        ++stats_.shadowHits;
        return base + offset;
    }

    const uint16_t* ResolveIndexPtr(const void* indicesPtr, size_t requiredBytes) {
        const uintptr_t offset = reinterpret_cast<uintptr_t>(indicesPtr);
        if (boundElemBuf_ == 0u) {
            return (indicesPtr != nullptr) ? static_cast<const uint16_t*>(indicesPtr) : nullptr;
        }
        size_t bufSize = 0;
        const uint8_t* base = shadowCache_.Find(boundElemBuf_, bufSize);
        if (!base && HasLazyGpuReadback()) {
            base = shadowCache_.FetchFromGpu(kGlElementArrayBuffer, boundElemBuf_, ops_, boundElemBuf_, bufSize);
        }
        if (!base || offset > bufSize || requiredBytes > (bufSize - offset)) {
            ++stats_.shadowMisses;
            return nullptr;
        }
        ++stats_.shadowHits;
        return reinterpret_cast<const uint16_t*>(base + offset);
    }

    uint32_t SamplerIndexToTexId(float uEnc) const noexcept {
        const int si = static_cast<int>(floorf(uEnc * 0.5f));
        int32_t unit = curProg_->samplerUnit0;
        if (si == 1 || (si >= 2 && curProg_->locSampler5 < 0)) {
            unit = curProg_->samplerUnit1;
        } else if (si >= 2) {
            unit = curProg_->samplerUnit5;
        }
        if (unit < 0 || unit >= 16) return 0u;
        return boundTex2D_[unit];
    }

    int32_t FindOrAddTexSlot(uint32_t texId) noexcept {
        if (texId == 0u) return -1;
        for (uint32_t s = 0; s < batchTexCount_; ++s) {
            if (batchTexIds_[s] == texId) return static_cast<int32_t>(s);
        }
        const uint32_t effSlots = EffectiveMaxTexSlots();
        if (batchTexCount_ < effSlots) {
            batchTexIds_[batchTexCount_] = texId;
            return static_cast<int32_t>(batchTexCount_++);
        }
        return -1;
    }

    inline float ResolvePreSlot(float uEnc) const noexcept {
        const int si = (uEnc < 2.0f) ? 0 : ((uEnc < 4.0f) ? 1 : 2);
        return curSiSlots_[si];
    }

    int32_t ResolveVertexTexSlot(float uEnc) const noexcept {
        const uint32_t texId = SamplerIndexToTexId(uEnc);
        if (texId == 0u) return -1;
        for (uint32_t s = 0; s < batchTexCount_; ++s) {
            if (batchTexIds_[s] == texId) return static_cast<int32_t>(s);
        }
        return -1;
    }

    bool CollectSpriteSamplerTextures(const uint8_t* vBase, size_t stride,
                                      uint32_t startVert, uint32_t vertCount,
                                      const uint16_t* idxSrc, uint32_t idxCount,
                                      uint32_t siTexId[3], uint32_t needed[3],
                                      uint32_t& neededCnt) const noexcept {
        neededCnt = 0u;
        siTexId[0] = siTexId[1] = siTexId[2] = 0u;
        uint8_t seenSi = 0u;
        auto visit = [&](uint32_t vIdx) noexcept -> bool {
            const float* src = reinterpret_cast<const float*>(vBase + static_cast<size_t>(vIdx) * stride);
            const float uEnc = src[2];
            const int si = (uEnc < 2.0f) ? 0 : ((uEnc < 4.0f) ? 1 : 2);
            const uint8_t bit = static_cast<uint8_t>(1u << si);
            if (seenSi & bit) return true;
            seenSi |= bit;
            const uint32_t texId = SamplerIndexToTexId(uEnc);
            if (texId == 0u) return false;
            siTexId[si] = texId;
            for (uint32_t k = 0; k < neededCnt; ++k) {
                if (needed[k] == texId) return true;
            }
            if (neededCnt < 3u) needed[neededCnt++] = texId;
            return true;
        };
        if (idxSrc && idxCount > 0u) {
            for (uint32_t k = 0; k < idxCount; ++k) {
                if (!visit(idxSrc[k])) return false;
                if (seenSi == 7u) break;
            }
        } else {
            for (uint32_t k = 0; k < vertCount; ++k) {
                if (!visit(startVert + k)) return false;
                if (seenSi == 7u) break;
            }
        }
        return neededCnt > 0u;
    }

    bool EnsureSamplerSlotsForVertices(const uint8_t* vBase, size_t stride,
                                       uint32_t startVert, uint32_t vertCount,
                                       const uint16_t* idxSrc, uint32_t idxCount) {
        uint32_t needed[3] = { 0, 0, 0 };
        uint32_t neededCnt = 0;
        uint32_t siTexId[3] = { 0, 0, 0 };
        if (!CollectSpriteSamplerTextures(vBase, stride, startVert, vertCount,
                                          idxSrc, idxCount, siTexId, needed, neededCnt)) {
            return false;
        }

        // Check how many NEW texture slots this sprite needs
        uint32_t newNeeded = 0;
        for (uint32_t k = 0; k < neededCnt; ++k) {
            bool exists = false;
            for (uint32_t s = 0; s < batchTexCount_; ++s) {
                if (batchTexIds_[s] == needed[k]) { exists = true; break; }
            }
            if (!exists) ++newNeeded;
        }
        const uint32_t effSlots = EffectiveMaxTexSlots();
        if (batchTexCount_ + newNeeded > effSlots) {
            if (newNeeded > effSlots) return false;
            FlushFromTryBatch(FlushReason::Tex);
        }
        int32_t neededSlots[3] = { 0, 0, 0 };
        for (uint32_t k = 0; k < neededCnt; ++k) {
            neededSlots[k] = FindOrAddTexSlot(needed[k]);
            if (neededSlots[k] < 0) return false;
        }
        curSingleSlot_ = (neededCnt == 1u);
        if (curSingleSlot_) {
            const float sf = static_cast<float>(neededSlots[0]);
            curSiSlots_[0] = sf;
            curSiSlots_[1] = sf;
            curSiSlots_[2] = sf;
        } else {
            for (int si = 0; si < 3; ++si) {
                float sf = 0.0f;
                const uint32_t tid = siTexId[si];
                if (tid != 0u) {
                    for (uint32_t k = 0; k < neededCnt; ++k) {
                        if (needed[k] == tid) { sf = static_cast<float>(neededSlots[k]); break; }
                    }
                }
                curSiSlots_[si] = sf;
            }
        }
        return true;
    }

    const BindlessSlot* PeekBindlessSlot(uint32_t texId) const noexcept {
        if (texId == 0u) return nullptr;
        if (texId < kBindlessDirectCap) {
            return (texId < bindlessDirect_.size()) ? &bindlessDirect_[texId] : nullptr;
        }
        const auto it = bindlessOverflow_.find(texId);
        return (it != bindlessOverflow_.end()) ? &it->second : nullptr;
    }

    BindlessSlot& TouchBindlessSlot(uint32_t texId) {
        if (texId < kBindlessDirectCap) {
            if (texId >= bindlessDirect_.size()) {
                bindlessDirect_.resize(static_cast<size_t>(texId) + 1u);
            }
            return bindlessDirect_[texId];
        }
        return bindlessOverflow_[texId];
    }

    uint64_t GetOrMakeResident(uint32_t texId) noexcept {
        if (texId == 0u || !ops_.glGetTextureHandleARB || !ops_.glMakeTextureHandleResidentARB) {
            return 0ULL;
        }
        BindlessSlot& sl = TouchBindlessSlot(texId);
        if (sl.denied) {
            ++diag_.gpuDenyHits;
            if (sl.denyReason == 1u)      ++diag_.gpuDenyMutated;
            else if (sl.denyReason == 2u) ++diag_.gpuDenyNullAlloc;
            else if (sl.denyReason == 3u) ++diag_.gpuDenyHandleZero;
            RecordDeniedSample(texId, sl.denyReason);
            return 0ULL;
        }
        if (sl.resident && sl.handle != 0ULL) return sl.handle;
        if (bindlessResidentCnt_ >= kMaxBindlessResident) return 0ULL;
        if (sl.handle == 0ULL) {
            sl.handle = ops_.glGetTextureHandleARB(texId);
            if (sl.handle == 0ULL) {
                sl.denied = true;
                sl.denyReason = 3u;
                ++diag_.gpuDenyHits;
                ++diag_.gpuDenyHandleZero;
                RecordDeniedSample(texId, 3u);
                return 0ULL;
            }
        }
        ops_.glMakeTextureHandleResidentARB(sl.handle);
        sl.resident = true;
        ++bindlessResidentCnt_;
        return sl.handle;
    }

    void InvalidateBindlessTex(uint32_t texId, bool denyFuture, uint8_t denyReason = 0u) noexcept {
        if (texId == 0u) return;
        mruBindlessValid_ = false;
        BindlessSlot* sl = nullptr;
        if (texId < kBindlessDirectCap) {
            if (texId < bindlessDirect_.size()) sl = &bindlessDirect_[texId];
        } else {
            const auto it = bindlessOverflow_.find(texId);
            if (it != bindlessOverflow_.end()) sl = &it->second;
        }
        if (!sl) {
            if (denyFuture) {
                BindlessSlot& s = TouchBindlessSlot(texId);
                s.denied = true;
                if (denyReason != 0u) s.denyReason = denyReason;
            }
            return;
        }
        if (sl->resident && sl->handle != 0ULL && ops_.glMakeTextureHandleNonResidentARB) {
            ops_.glMakeTextureHandleNonResidentARB(sl->handle);
        }
        if (sl->resident && bindlessResidentCnt_ > 0u) {
            --bindlessResidentCnt_;
        }
        sl->resident = false;
        sl->handle   = 0ULL;
        if (denyFuture) {
            sl->denied = true;
            if (denyReason != 0u) sl->denyReason = denyReason;
        } else {
            sl->denied = false;
            sl->denyReason = 0u;
        }
    }

    void ReleaseAllBindless(bool deleteOnGl) noexcept {
        if (deleteOnGl && ops_.glMakeTextureHandleNonResidentARB) {
            for (auto& sl : bindlessDirect_) {
                if (sl.resident && sl.handle != 0ULL) {
                    ops_.glMakeTextureHandleNonResidentARB(sl.handle);
                }
            }
            for (auto& kv : bindlessOverflow_) {
                if (kv.second.resident && kv.second.handle != 0ULL) {
                    ops_.glMakeTextureHandleNonResidentARB(kv.second.handle);
                }
            }
        }
        bindlessDirect_.clear();
        bindlessOverflow_.clear();
        bindlessResidentCnt_ = 0u;
        mruBindlessValid_ = false;
    }

    void EmitBatchIndicesTo(uint16_t* __restrict dst, const uint16_t* __restrict idxSrc,
                            uint32_t rawCount, uint32_t startVert,
                            uint32_t baseOutVert, uint32_t spanVerts, bool isStrip) noexcept {
        if (idxSrc && rawCount > 0u) {
            if (!isStrip) {
                const uint16_t delta = static_cast<uint16_t>(baseOutVert - startVert);
                uint32_t k = 0u;
                if (rawCount >= 8u) {
                    const __m128i vDelta = _mm_set1_epi16(static_cast<short>(delta));
                    for (; k + 7u < rawCount; k += 8u) {
                        const __m128i vIdx = _mm_loadu_si128(reinterpret_cast<const __m128i*>(idxSrc + k));
                        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + k), _mm_add_epi16(vIdx, vDelta));
                    }
                }
                for (; k < rawCount; ++k) {
                    dst[k] = static_cast<uint16_t>(idxSrc[k] + delta);
                }
                indexCount_ += rawCount;
            } else {
                const uint16_t delta = static_cast<uint16_t>(baseOutVert - startVert);
                uint32_t out = 0u;
                for (uint32_t t = 0; t + 2u < rawCount; ++t) {
                    const uint32_t a = idxSrc[t], b = idxSrc[t + 1u], c = idxSrc[t + 2u];
                    if (a == b || b == c || a == c) continue;
                    const uint16_t ia = static_cast<uint16_t>(a + delta);
                    const uint16_t ib = static_cast<uint16_t>(b + delta);
                    const uint16_t ic = static_cast<uint16_t>(c + delta);
                    if ((t & 1u) == 0u) {
                        dst[out++] = ia;
                        dst[out++] = ib;
                        dst[out++] = ic;
                    } else {
                        dst[out++] = ib;
                        dst[out++] = ia;
                        dst[out++] = ic;
                    }
                }
                indexCount_ += out;
            }
        } else {
            if (!isStrip) {
                uint32_t k = 0u;
                if (spanVerts >= 8u) {
                    __m128i vCur = _mm_setr_epi16(
                        static_cast<short>(baseOutVert + 0u), static_cast<short>(baseOutVert + 1u),
                        static_cast<short>(baseOutVert + 2u), static_cast<short>(baseOutVert + 3u),
                        static_cast<short>(baseOutVert + 4u), static_cast<short>(baseOutVert + 5u),
                        static_cast<short>(baseOutVert + 6u), static_cast<short>(baseOutVert + 7u));
                    const __m128i vStep = _mm_set1_epi16(8);
                    for (; k + 7u < spanVerts; k += 8u) {
                        _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + k), vCur);
                        vCur = _mm_add_epi16(vCur, vStep);
                    }
                }
                for (; k < spanVerts; ++k) {
                    dst[k] = static_cast<uint16_t>(baseOutVert + k);
                }
                indexCount_ += spanVerts;
            } else {
                uint32_t out = 0u;
                for (uint32_t t = 0; t + 2u < spanVerts; ++t) {
                    const uint16_t a = static_cast<uint16_t>(baseOutVert + t);
                    const uint16_t b = static_cast<uint16_t>(baseOutVert + t + 1u);
                    const uint16_t c = static_cast<uint16_t>(baseOutVert + t + 2u);
                    if ((t & 1u) == 0u) {
                        dst[out++] = a;
                        dst[out++] = b;
                        dst[out++] = c;
                    } else {
                        dst[out++] = b;
                        dst[out++] = a;
                        dst[out++] = c;
                    }
                }
                indexCount_ += out;
            }
        }
    }

    bool TryBatchGpuDrivenInternal(const uint8_t* vBase, size_t stride,
                                   uint32_t startVert, uint32_t spanVerts,
                                   const uint16_t* idxSrc, uint32_t rawCount,
                                   uint32_t outIdxCount, bool isStrip,
                                   uint8_t foliageClass,
                                   float foliageSwayBase,
                                   float foliageWakeBase) {
        if (!CanUseGpuDrivenDraw()) return false;

        // O(1) Coherent 1-Entry MRU + Single-Lookup Bindless Sampler Path:
        // skips hash/vector lookups completely on consecutive same-atlas sprites!
        uint64_t h0 = 0ULL, h1 = 0ULL, h2 = 0ULL;
        bool fastBindlessOk = false;
        {
            const int32_t u0 = curProg_->samplerUnit0;
            const int32_t u1 = curProg_->samplerUnit1;
            const int32_t u5 = curProg_->samplerUnit5;
            const uint32_t t0 = (u0 >= 0 && u0 < 16) ? boundTex2D_[u0] : 0u;
            const bool need1 = (curProg_->locSampler1 >= 0);
            const bool need2 = (curProg_->locSampler5 >= 0);
            const uint32_t t1 = (need1 && u1 >= 0 && u1 < 16) ? boundTex2D_[u1] : 0u;
            const uint32_t t2 = (need2 && u5 >= 0 && u5 < 16) ? boundTex2D_[u5] : 0u;
            const uint32_t q1 = need1 ? t1 : t0;
            const uint32_t q2 = need2 ? t2 : q1;
            if (mruBindlessValid_ &&
                t0 == mruBindlessTex_[0] &&
                q1 == mruBindlessTex_[1] &&
                q2 == mruBindlessTex_[2]) {
                h0 = mruBindlessHandle_[0];
                h1 = mruBindlessHandle_[1];
                h2 = mruBindlessHandle_[2];
                fastBindlessOk = true;
            } else if (t0 != 0u && (!need1 || t1 != 0u) && (!need2 || t2 != 0u)) {
                auto resolveFast = [&](uint32_t tid) noexcept -> uint64_t {
                    if (tid < bindlessDirect_.size()) {
                        const BindlessSlot& sl = bindlessDirect_[tid];
                        if (sl.denied) return 0ULL;
                        if (sl.resident && sl.handle != 0ULL) return sl.handle;
                    } else if (tid >= kBindlessDirectCap) {
                        const auto it = bindlessOverflow_.find(tid);
                        if (it != bindlessOverflow_.end()) {
                            if (it->second.denied) return 0ULL;
                            if (it->second.resident && it->second.handle != 0ULL) return it->second.handle;
                        }
                    }
                    return GetOrMakeResident(tid);
                };
                h0 = resolveFast(t0);
                if (h0 != 0ULL) {
                    h1 = need1 ? ((t1 == t0) ? h0 : resolveFast(t1)) : h0;
                    h2 = need2 ? ((t2 == t0) ? h0 : ((t2 == t1) ? h1 : resolveFast(t2))) : h1;
                    if (h1 != 0ULL && h2 != 0ULL) {
                        mruBindlessTex_[0]    = t0;
                        mruBindlessTex_[1]    = q1;
                        mruBindlessTex_[2]    = q2;
                        mruBindlessHandle_[0] = h0;
                        mruBindlessHandle_[1] = h1;
                        mruBindlessHandle_[2] = h2;
                        mruBindlessValid_     = true;
                        fastBindlessOk        = true;
                    }
                }
            }
        }
        if (!fastBindlessOk) {
            uint32_t needed[3] = { 0, 0, 0 };
            uint32_t neededCnt = 0u;
            uint32_t siTexId[3] = { 0, 0, 0 };
            if (!CollectSpriteSamplerTextures(vBase, stride, startVert, spanVerts,
                                              idxSrc, rawCount, siTexId, needed, neededCnt)) {
                return false;
            }
            uint64_t handles[3] = { 0ULL, 0ULL, 0ULL };
            uint64_t fallbackH  = 0ULL;
            for (int si = 0; si < 3; ++si) {
                if (siTexId[si] != 0u) {
                    handles[si] = GetOrMakeResident(siTexId[si]);
                    if (handles[si] == 0ULL) return false;
                    if (fallbackH == 0ULL) fallbackH = handles[si];
                }
            }
            if (fallbackH == 0ULL) return false;
            h0 = handles[0] ? handles[0] : fallbackH;
            h1 = handles[1] ? handles[1] : fallbackH;
            h2 = handles[2] ? handles[2] : fallbackH;
        }

        if (HasPending() && !curBatchGpuMode_) {
            if (frameModeSwitches_ >= kMaxFrameGpuModeSwitches) {
                NoteGpuModeSwitch();
                return false;
            }
            FlushFromTryBatch(FlushReason::Other);
            NoteGpuModeSwitch();
            if (frameGpuLatchedOff_) {
                return false;
            }
        }
        if (gpuVertCount_ + spanVerts > kMaxGpuBatchVerts ||
            indexCount_ + outIdxCount > kMaxBatchIndices ||
            gpuInstCount_ + 1u > kMaxGpuBatchInsts) {
            FlushFromTryBatch(FlushReason::Full);
        }
        if ((gpuVertCursor_ + gpuVertCount_ + spanVerts) > activeGpuFrameMaxVerts_ ||
            (azdoIdxCursor_ + indexCount_ + outIdxCount) > activeAzdoFrameMaxIndices_ ||
            (gpuInstCursor_ + gpuInstCount_ + 1u) > activeGpuFrameMaxInsts_) {
            ++diag_.gpuRingOverflow;
            frameGpuRingFull_        = true;
            frameHadGpuRingOverflow_ = true;
            return false;
        }
        WaitForAzdoFrameIfNeeded();

        const float* m = curMatrixW_;
        const bool needMeta = shadowPassActive_ || (foliageClass != 0u);
        float rux = 0.0f, ruy = 0.0f, ruz = 0.0f;
        bool canShadow = false;
        bool doFoliage = false;
        float swayAxisX = 0.70710678f, swayAxisZ = -0.70710678f;
        if (needMeta) {
            if (!hasCamRight_) {
                const float rLen = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
                const float invR = (rLen > 1e-4f) ? (1.0f / rLen) : 1.0f;
                rux = m[0] * invR; ruy = m[1] * invR; ruz = m[2] * invR;
            }
            const bool isStanding = IsCurrentMatrixWStanding();
            canShadow = shadowPassActive_ && isStanding &&
                        (!skipTranslucentShadows_ || curCMult_[3] >= 0.72f) &&
                        (curProg_->lastParamsY <= 0.35f);
            doFoliage = isStanding &&
                ((foliageClass == 1u && (std::fabs(foliageSwayBase) > 1e-4f || std::fabs(foliageWakeBase) > 1e-4f)) ||
                 (foliageClass == 2u && std::fabs(foliageSwayBase) > 1e-4f));
            if (doFoliage) {
                const float ax = hasCamRight_ ? camRightX_ : rux;
                const float az = hasCamRight_ ? camRightZ_ : ruz;
                const float aLen = std::sqrt(ax * ax + az * az);
                if (aLen > 1e-4f) {
                    swayAxisX = ax / aLen;
                    swayAxisZ = az / aLen;
                }
            }
        }

        const uint32_t baseOutVert = gpuVertCount_;
        const size_t globalVertIdx = static_cast<size_t>(azdoFrameIdx_) * activeGpuFrameMaxVerts_ +
                                     gpuVertCursor_ + gpuVertCount_;
        const size_t globalIdxOff  = static_cast<size_t>(azdoFrameIdx_) * activeAzdoFrameMaxIndices_ +
                                     azdoIdxCursor_ + indexCount_;
        const size_t globalInstIdx = static_cast<size_t>(azdoFrameIdx_) * activeGpuFrameMaxInsts_ +
                                     gpuInstCursor_ + gpuInstCount_;
        const float globalInstId = static_cast<float>(globalInstIdx);

        float maxWy = -1e9f;
        const bool trackMaxWy = canShadow && (minShadowHeight_ > 0.01f);
        const int64_t uT0 = ProfNow();
        GpuBatchVertex* __restrict dstV =
            reinterpret_cast<GpuBatchVertex*>(gpuVboMapped_) + globalVertIdx;
        const uint8_t* __restrict srcPtr = vBase + static_cast<size_t>(startVert) * stride;
        if (!trackMaxWy) {
            uint32_t k = 0u;
            if (stride == 16u) {
                const float* __restrict sf = reinterpret_cast<const float*>(srcPtr);
                for (; k + 3u < spanVerts; k += 4u) {
                    const __m128 v0 = _mm_loadu_ps(sf);
                    const __m128 v1 = _mm_loadu_ps(sf + 4);
                    const __m128 v2 = _mm_loadu_ps(sf + 8);
                    const __m128 v3 = _mm_loadu_ps(sf + 12);
                    _mm_storeu_ps(&dstV[k + 0u].x, v0);
                    dstV[k + 0u].instId = globalInstId;
                    _mm_storeu_ps(&dstV[k + 1u].x, v1);
                    dstV[k + 1u].instId = globalInstId;
                    _mm_storeu_ps(&dstV[k + 2u].x, v2);
                    dstV[k + 2u].instId = globalInstId;
                    _mm_storeu_ps(&dstV[k + 3u].x, v3);
                    dstV[k + 3u].instId = globalInstId;
                    sf += 16;
                }
                for (; k < spanVerts; ++k) {
                    _mm_storeu_ps(&dstV[k].x, _mm_loadu_ps(sf));
                    dstV[k].instId = globalInstId;
                    sf += 4;
                }
            } else {
                for (; k < spanVerts; ++k) {
                    _mm_storeu_ps(&dstV[k].x, _mm_loadu_ps(reinterpret_cast<const float*>(srcPtr)));
                    dstV[k].instId = globalInstId;
                    srcPtr += stride;
                }
            }
        } else {
            const float m1 = m[1], m5 = m[5], m13 = m[13];
            uint32_t k = 0u;
            if (stride == 16u) {
                const float* __restrict sf = reinterpret_cast<const float*>(srcPtr);
                if (spanVerts >= 4u) {
                    const __m128 vm1  = _mm_set1_ps(m1);
                    const __m128 vm5  = _mm_set1_ps(m5);
                    const __m128 vm13 = _mm_set1_ps(m13);
                    __m128 vMaxWy     = _mm_set1_ps(maxWy);
                    for (; k + 3u < spanVerts; k += 4u) {
                        const __m128 v0 = _mm_loadu_ps(sf);
                        const __m128 v1 = _mm_loadu_ps(sf + 4);
                        const __m128 v2 = _mm_loadu_ps(sf + 8);
                        const __m128 v3 = _mm_loadu_ps(sf + 12);
                        _mm_storeu_ps(&dstV[k + 0u].x, v0);
                        dstV[k + 0u].instId = globalInstId;
                        _mm_storeu_ps(&dstV[k + 1u].x, v1);
                        dstV[k + 1u].instId = globalInstId;
                        _mm_storeu_ps(&dstV[k + 2u].x, v2);
                        dstV[k + 2u].instId = globalInstId;
                        _mm_storeu_ps(&dstV[k + 3u].x, v3);
                        dstV[k + 3u].instId = globalInstId;
                        const __m128 v01 = _mm_movelh_ps(v0, v1);
                        const __m128 v23 = _mm_movelh_ps(v2, v3);
                        const __m128 vx  = _mm_shuffle_ps(v01, v23, _MM_SHUFFLE(2, 0, 2, 0));
                        const __m128 vy  = _mm_shuffle_ps(v01, v23, _MM_SHUFFLE(3, 1, 3, 1));
                        const __m128 wyv = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vm1, vx), _mm_mul_ps(vm5, vy)), vm13);
                        // Keep vMaxWy as 2nd operand so NaN in wyv never overwrites vMaxWy (matches scalar `if (wy > maxWy)`!)
                        vMaxWy = _mm_max_ps(wyv, vMaxWy);
                        sf += 16;
                    }
                    const __m128 hi  = _mm_movehl_ps(vMaxWy, vMaxWy);
                    const __m128 m2  = _mm_max_ps(hi, vMaxWy);
                    const __m128 m1v = _mm_max_ss(_mm_shuffle_ps(m2, m2, _MM_SHUFFLE(1, 1, 1, 1)), m2);
                    const float vecMax = _mm_cvtss_f32(m1v);
                    if (vecMax > maxWy) maxWy = vecMax;
                }
                for (; k < spanVerts; ++k) {
                    const __m128 v0 = _mm_loadu_ps(sf);
                    _mm_storeu_ps(&dstV[k].x, v0);
                    dstV[k].instId = globalInstId;
                    const float wy = m1 * sf[0] + m5 * sf[1] + m13;
                    if (wy > maxWy) maxWy = wy;
                    sf += 4;
                }
            } else {
                for (; k < spanVerts; ++k) {
                    const float* __restrict sf = reinterpret_cast<const float*>(srcPtr);
                    const __m128 v0 = _mm_loadu_ps(sf);
                    _mm_storeu_ps(&dstV[k].x, v0);
                    dstV[k].instId = globalInstId;
                    const float wy = m1 * sf[0] + m5 * sf[1] + m13;
                    if (wy > maxWy) maxWy = wy;
                    srcPtr += stride;
                }
            }
        }
        gpuVertCount_ += spanVerts;

        float shadowFlag = 0.0f;
        if (canShadow && (minShadowHeight_ <= 0.01f || maxWy >= minShadowHeight_)) {
            shadowFlag = 1.0f;
            batchHasShadowCaster_ = true;
            if (m[12] < shadowMinX_) shadowMinX_ = m[12];
            if (m[12] > shadowMaxX_) shadowMaxX_ = m[12];
            if (m[14] < shadowMinZ_) shadowMinZ_ = m[14];
            if (m[14] > shadowMaxZ_) shadowMaxZ_ = m[14];
        }

        const bool useInstParams = curProg_->hasParamsAbsorption && curProg_->hasParamsValue && !curProg_->isBloomPass;
        const bool fpClipOn      = (curProg_->lastFloatParams[1] > 0.0f);
        const bool fpBobOn       = (curProg_->lastFloatParams[2] > 0.0f);
        const bool bloomOn       = useInstParams && (curProg_->lastParams[2] == 1.0f);

        alignas(16) GpuSpriteInstance inst;
        inst.m0[0]    = m[0];  inst.m0[1] = m[1];  inst.m0[2] = m[2];  inst.m0[3] = m[13];
        inst.m1[0]    = m[4];  inst.m1[1] = m[5];  inst.m1[2] = m[6];  inst.m1[3] = curProg_->lastOceanBlend[0];
        inst.m2[0]    = m[12]; inst.m2[1] = m[13]; inst.m2[2] = m[14];
        inst.m2[3]    = PackM2WParams(fpClipOn, bloomOn, useInstParams, curProg_->lastParams[0], fpBobOn);
        inst.cmult[0] = curCMult_[0];
        inst.cmult[1] = curCMult_[1];
        inst.cmult[2] = curCMult_[2];
        inst.cmult[3] = curCMult_[3];
        inst.cadd[0]  = curCAdd_[0];
        inst.cadd[1]  = curCAdd_[1];
        inst.cadd[2]  = curCAdd_[2];
        inst.cadd[3]  = shadowFlag;
        inst.meta[0]  = needMeta ? (hasCamRight_ ? camRightX_ : rux) : 0.0f;
        inst.meta[1]  = needMeta ? (hasCamRight_ ? 0.0f : ruy) : 0.0f;
        inst.meta[2]  = needMeta ? (hasCamRight_ ? camRightZ_ : ruz) : 0.0f;
        inst.meta[3]  = swayAxisX;
        inst.meta2[0] = foliageSwayBase;
        inst.meta2[1] = foliageWakeBase;
        inst.meta2[2] = doFoliage ? static_cast<float>(foliageClass) : 0.0f;
        inst.meta2[3] = swayAxisZ;
        std::memcpy(inst.tex0, &h0, sizeof(uint64_t));
        std::memcpy(inst.tex1, &h1, sizeof(uint64_t));
        std::memcpy(inst.tex2, &h2, sizeof(uint64_t));
        std::memcpy(&inst.flags[0], &curProg_->lastFloatParams[0], sizeof(float));
        if (useInstParams) {
            std::memcpy(&inst.flags[1], &curProg_->lastParams[1], sizeof(float));
        } else {
            inst.flags[1] = 0u;
        }
        std::memcpy(reinterpret_cast<GpuSpriteInstance*>(gpuSsboMapped_) + globalInstIdx,
                    &inst, sizeof(GpuSpriteInstance));
        ++gpuInstCount_;

        uint16_t* __restrict dstI = reinterpret_cast<uint16_t*>(azdoIboMapped_) + globalIdxOff;
        EmitBatchIndicesTo(dstI, idxSrc, rawCount, startVert, baseOutVert, spanVerts, isStrip);
        if (uT0 != 0) {
            const int64_t uT1 = ops_.nowQpc();
            if (uT1 > uT0) frameProf_.flushUploadQpc += (uT1 - uT0);
        }
        curBatchGpuMode_ = true;
        ++stats_.drawsBatched;
        ++gpuDrivenDraws_;
        stats_.vertsBatched += spanVerts;
        return true;
    }

    int64_t ProfNow() const noexcept {
        return (profEnabled_ && ops_.nowQpc) ? ops_.nowQpc() : 0;
    }

    void FlushFromTryBatch(FlushReason reason) {
        const int64_t t0 = ProfNow();
        Flush(reason);
        if (t0 != 0) {
            const int64_t t1 = ops_.nowQpc();
            if (t1 > t0) frameProf_.tryInFlushQpc += (t1 - t0);
        }
    }

    void NoteGpuModeSwitch() noexcept {
        ++frameModeSwitches_;
        ++diag_.gpuModeSwitches;
        if (frameModeSwitches_ >= kMaxFrameGpuModeSwitches && !frameGpuLatchedOff_) {
            frameGpuLatchedOff_ = true;
            ++diag_.gpuLatchFrames;
            ++consecLatchFrames_;
            if (consecLatchFrames_ >= 3u) {
                gpuCooldownFrames_ = kGpuPingPongCooldownFrames;
            }
        }
    }

    void RecordDeniedSample(uint32_t texId, uint8_t reason) noexcept {
        if (deniedSampleCount_ >= kMaxDeniedSamples) return;
        for (uint32_t i = 0; i < deniedSampleCount_; ++i) {
            if (deniedSamples_[i].texId == texId) return;
        }
        deniedSamples_[deniedSampleCount_++] = DeniedTexSample{ texId, reason };
    }

    GlBatchOps        ops_{};
    ShadowBufferCache shadowCache_;

    static constexpr uint32_t kMaxDeniedSamples = 8u;
    bool           profEnabled_              = false;
    FrameProfile   frameProf_{};
    DiagCounters   diag_{};
    uint32_t       frameModeSwitches_        = 0u;
    bool           frameGpuLatchedOff_       = false;
    bool           frameGpuRingFull_         = false;
    bool           frameHadGpuRingOverflow_  = false;
    uint32_t       consecLatchFrames_        = 0u;
    uint32_t       gpuCooldownFrames_        = 0u;
    uint32_t       baseWantGpuVerts_         = kAzdoFrameMaxVerts;
    uint32_t       baseWantGpuIndices_       = kAzdoFrameMaxIndices;
    uint32_t       baseWantGpuInsts_         = kMaxGpuBatchInsts;
    DeniedTexSample deniedSamples_[kMaxDeniedSamples]{};
    uint32_t       deniedSampleCount_        = 0u;
    uint32_t       deniedSampleRead_         = 0u;

    void*          curCtx_                   = nullptr;
    bool           enabled_                  = false;
    bool           persistentRingPreferred_  = true;
    bool           persistentRingActive_     = false;
    bool           gpuDrivenPreferred_       = false;
    bool           gpuDrivenRingActive_      = false;
    bool           gpuDrivenRingFailed_      = false;
    bool           gpuRingResizePending_     = false;
    bool           fallbackRingReady_        = false;
    bool           azdoFailed_               = false;
    bool           ringReady_                = false;
    bool           ringFailed_               = false;
    bool           texUnitsProbed_           = false;
    uint32_t       maxTexSlots_              = 0;
    uint32_t       ringIdx_                  = 0;
    uint32_t       wantGpuFrameMaxVerts_     = kAzdoFrameMaxVerts;
    uint32_t       wantGpuFrameMaxIndices_   = kAzdoFrameMaxIndices;
    uint32_t       wantGpuFrameMaxInsts_     = kMaxGpuBatchInsts;
    uint32_t       activeAzdoFrameMaxVerts_  = kAzdoFrameMaxVerts;
    uint32_t       activeAzdoFrameMaxIndices_ = kAzdoFrameMaxIndices;
    uint32_t       activeGpuFrameMaxVerts_   = kAzdoFrameMaxVerts;
    uint32_t       activeGpuFrameMaxInsts_   = kMaxGpuBatchInsts;
    uint32_t       vboIds_[kRingSlots]       = {};
    uint32_t       iboIds_[kRingSlots]       = {};
    uint32_t       azdoVboId_                = 0;
    uint32_t       azdoIboId_                = 0;
    uint32_t       gpuVboId_                 = 0;
    uint32_t       gpuSsboId_                = 0;
    uint8_t*       azdoVboMapped_            = nullptr;
    uint8_t*       azdoIboMapped_            = nullptr;
    uint8_t*       gpuVboMapped_             = nullptr;
    uint8_t*       gpuSsboMapped_            = nullptr;
    void*          azdoFrameFences_[kAzdoFramesInFlight] = {};
    uint32_t       azdoFrameIdx_             = 0;
    uint32_t       azdoVertCursor_           = 0;
    uint32_t       azdoIdxCursor_            = 0;
    uint32_t       gpuVertCursor_            = 0;
    uint32_t       gpuInstCursor_            = 0;
    bool           azdoFrameWaited_          = true;
    bool           azdoFrameTouched_         = false;
    uintptr_t      lastFlushIndexByteOffset_ = 0u;
    uintptr_t      lastFlushVertByteOffset_  = 0u;
    bool           lastFlushUsedPersistent_  = false;
    bool           lastFlushIsGpuDriven_     = false;
    uint32_t       lastFlushVboId_           = 0u;
    uint32_t       lastFlushIboId_           = 0u;
    uint32_t       lastFlushSsboId_          = 0u;
    int32_t        lastFlushBaseVertex_      = 0;
    uint32_t       lastFlushTexCount_        = 0u;
    uint32_t       lastFlushTexIds_[kMaxBatchTexSlots] = {};
    bool           shadowPassActive_         = true;
    bool           deferHostRestore_         = false;
    bool           stickySessionActive_      = false;
    bool           stickyAttribsZeroOffset_  = false;
    uint8_t        stickyMode_               = 0u;
    uint32_t       stickyProgId_             = 0u;
    int32_t        stickyBatchActiveLoc_     = -1;
    int32_t        stickyWposLoc_            = -1;
    int32_t        stickyCMultLoc_           = -1;
    int32_t        stickyCAddLoc_            = -1;
    uint32_t       stickySlotTex_[kMaxBatchTexSlots] = {};
    float          curSiSlots_[3]          = { 0.0f, 0.0f, 0.0f };
    bool           curSingleSlot_          = false;

    std::vector<BatchedVertex>     verts_;
    std::vector<uint16_t>          indices_;
    uint32_t                       vertCount_       = 0;
    uint32_t                       gpuVertCount_    = 0;
    uint32_t                       gpuInstCount_    = 0;
    uint32_t                       indexCount_      = 0;
    bool                           curBatchGpuMode_ = false;
    uint64_t                       gpuDrivenDraws_  = 0ULL;
    std::vector<BindlessSlot>      bindlessDirect_;
    std::unordered_map<uint32_t, BindlessSlot> bindlessOverflow_;
    uint32_t                       bindlessResidentCnt_ = 0u;
    uint32_t                       mruBindlessTex_[3]    = {};
    uint64_t                       mruBindlessHandle_[3] = {};
    bool                           mruBindlessValid_     = false;
    uint32_t                       boundSsboId_          = 0u;
    uint32_t                   batchTexIds_[kMaxBatchTexSlots] = {};
    uint32_t                   batchTexCount_ = 0;
    bool                       batchHasShadowCaster_   = false;
    float                      shadowMinX_             = 1e30f;
    float                      shadowMaxX_             = -1e30f;
    float                      shadowMinZ_             = 1e30f;
    float                      shadowMaxZ_             = -1e30f;
    float                      lastFlushShadowBounds_[4] = { 1e30f, -1e30f, 1e30f, -1e30f };
    bool                       lastFlushHasShadowBounds_ = false;
    bool                       skipTranslucentShadows_ = true;
    bool                       hasCamRight_            = false;
    float                      camRightX_              = 0.707f;
    float                      camRightZ_              = -0.707f;
    float                      minShadowHeight_        = 0.0f;

    BatchProgInfo* curProg_          = nullptr;
    uint32_t       curProgId_        = 0;
    float          curMatrixW_[16]   = {};
    bool           hasMatrixW_       = false;
    bool           matrixWDirty_     = false;
    float          curColourXform_[16] = {};
    float          curCMult_[4]      = { 1.0f, 1.0f, 1.0f, 1.0f };
    float          curCAdd_[4]       = { 0.0f, 0.0f, 0.0f, 0.0f };
    bool           hasColourXform_   = false;
    bool           colourXformDirty_ = false;

    uint32_t       activeUnit_      = 0;
    uint32_t       boundArrayBuf_   = 0;
    uint32_t       boundElemBuf_    = 0;
    uint16_t       attribEnMask_    = 0;
    AttribState    attribs_[16]{};
    uint32_t       boundTex2D_[16]  = {};

    Stats          stats_{};
    uint32_t       lastRejectCode_  = 0;
};

} // namespace hs_sprite_batch
