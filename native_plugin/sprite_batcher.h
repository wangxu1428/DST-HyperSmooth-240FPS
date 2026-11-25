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
inline constexpr uint32_t kGlBufferSize             = 0x8764u;
inline constexpr uint32_t kGlStreamDraw             = 0x88E0u;
inline constexpr uint32_t kGlMaxTextureImageUnits   = 0x8872u;
inline constexpr uint32_t kMapReadBit               = 0x0001u;
inline constexpr uint32_t kMapWriteBit              = 0x0002u;
inline constexpr uint32_t kMapInvalidateRangeBit    = 0x0004u;
inline constexpr uint32_t kMapInvalidateBufferBit   = 0x0008u;
inline constexpr uint32_t kMapUnsynchronizedBit     = 0x0020u;
// NEVER combine GL_MAP_UNSYNCHRONIZED_BIT with offset-0 reuse: per GLES3 spec §6.3.2 it permits
// overwriting in-flight GPU VRAM when >kRingSlots flushes occur in one frame! Pure WRITE|INVALIDATE_BUFFER
// triggers lockless driver page orphaning with 0ms stall and 0% CPU/GPU data race.
inline constexpr uint32_t kMapWriteInvalidateBuffer = kMapWriteBit | kMapInvalidateBufferBit;
inline constexpr uint32_t kMapWriteInvalidateUnsync = kMapWriteInvalidateBuffer;

inline constexpr uint32_t kBatchTexUnitBase         = 6u;     // GL_TEXTURE6 .. GL_TEXTURE13 (unused by DST 0..5)
inline constexpr uint32_t kMaxBatchTexSlots         = 8u;
inline constexpr uint32_t kMaxBatchVerts            = 16384u; // 1 MB per ring slot
inline constexpr uint32_t kMaxBatchIndices          = 32768u; // 64 KB per ring slot (uint16_t safe <= 65535)
inline constexpr uint32_t kRingSlots                = 8u;
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
    void*    ctx                = nullptr;
    bool     checked            = false;
    bool     eligible           = false;
    bool     samplersInit       = false;
    int32_t  locBatchActive     = -1;
    int32_t  locMatrixW         = -1;
    int32_t  locColourXform     = -1;
    int32_t  locFloatParams     = -1;
    int32_t  locParams          = -1;
    int32_t  locPos2dUv         = -1;
    int32_t  locBatchWpos       = -1;
    int32_t  locBatchCMult      = -1;
    int32_t  locBatchCAdd       = -1;
    int32_t  locBatchTex[kMaxBatchTexSlots] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    int32_t  locSampler0        = -1;
    int32_t  locSampler1        = -1;
    int32_t  locSampler5        = -1;
    int32_t  samplerUnit0       = 0;
    int32_t  samplerUnit1       = 1;
    int32_t  samplerUnit5       = 5;
    float    lastFloatParamsZ   = 0.0f;
    float    lastParamsY        = 0.0f;
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
    using UniformMatrix4fv_t         = void (WINAPI*)(int32_t location, int32_t count, uint8_t transpose, const float* value);
    using VertexAttribPointer_t      = void (WINAPI*)(uint32_t index, int32_t size, uint32_t type, uint8_t normalized, int32_t stride, const void* ptr);
    using EnableVertexAttribArray_t  = void (WINAPI*)(uint32_t index);
    using DisableVertexAttribArray_t = void (WINAPI*)(uint32_t index);
    using DrawElements_t             = void (WINAPI*)(uint32_t mode, int32_t count, uint32_t type, const void* indices);
    using GetIntegerv_t              = void (WINAPI*)(uint32_t pname, int32_t* params);
    using GetBufferParameteriv_t     = void (WINAPI*)(uint32_t target, uint32_t pname, int32_t* params);
    using GetBufferSubData_t         = void (WINAPI*)(uint32_t target, ptrdiff_t offset, ptrdiff_t size, void* data);
    using SyncStateAfterFlush_t      = void (*)(uint32_t activeUnit, uint32_t arrayBuf, uint32_t elemBuf, uint32_t batchWposLoc, void* ctx);
    using ShadowMirrorBatch_t        = void (*)(uint32_t indexCount, void* ctx);

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
    UniformMatrix4fv_t         glUniformMatrix4fv         = nullptr;
    VertexAttribPointer_t      glVertexAttribPointer      = nullptr;
    EnableVertexAttribArray_t  glEnableVertexAttribArray  = nullptr;
    DisableVertexAttribArray_t glDisableVertexAttribArray = nullptr;
    DrawElements_t             glDrawElements             = nullptr;
    GetIntegerv_t              glGetIntegerv              = nullptr;
    GetBufferParameteriv_t     glGetBufferParameteriv     = nullptr;
    GetBufferSubData_t         glGetBufferSubData         = nullptr;
    SyncStateAfterFlush_t      onPostFlushRestore         = nullptr;
    ShadowMirrorBatch_t        onShadowMirrorBatch        = nullptr;
    void*                      callbackCtx                = nullptr;
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

// =============================================================================
// Shader Source Transformer (strictly whitelisted to vanilla world anim.ksh)
// Composes cleanly with HD-2D Contact AO transform in either order!
// =============================================================================
inline size_t FindDeclInsertOffset(const std::string& src) {
    // Match dinput8.cpp FindPrecisionEndOffset: if `precision ...;` is wrapped inside
    // `#if defined( GL_ES ) ... #endif`, insert AFTER `#endif` so Desktop OpenGL sees the decls!
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
                    return (nl != std::string::npos) ? (nl + 1) : (endifPos + 6);
                }
            }
            size_t nl = src.find('\n', semi);
            return (nl != std::string::npos) ? (nl + 1) : (semi + 1);
        }
    }
    size_t ver = src.find("#version");
    if (ver != std::string::npos) {
        size_t nl = src.find('\n', ver);
        return (nl != std::string::npos) ? (nl + 1) : 0;
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

inline bool TransformAnimBatchShaderSource(const std::string& src, std::string& outSrc, const char*& outTag) {
    if (src.find("HS_BATCH_ACTIVE") != std::string::npos) {
        return false; // already transformed
    }
    if (HasDefineLine(src, "SKINNED") || HasDefineLine(src, "FADE_OUT") ||
        HasDefineLine(src, "HAUNT") || HasDefineLine(src, "HOLO") ||
        HasDefineLine(src, "UI_HOLO") || HasDefineLine(src, "UI_CC") ||
        HasDefineLine(src, "BLOOM")) {
        return false;
    }

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
            const size_t posAttr = s.find("attribute vec4 POS2D_UV;");
            if (posAttr != std::string::npos) {
                s.insert(posAttr,
                         "uniform float HS_BATCH_ACTIVE;\n"
                         "attribute vec4 HS_BATCH_WPOS;\n"
                         "attribute vec4 HS_BATCH_CMULT;\n"
                         "attribute vec4 HS_BATCH_CADD;\n"
                         "varying vec4 HS_V_CMULT;\n"
                         "varying vec3 HS_V_CADD;\n");
                outSrc = std::move(s);
                outTag = "ANIM_BATCH_VS";
                return true;
            }
        }
    }

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
            std::string wrapped =
                "if (HS_BATCH_ACTIVE > 0.5) {\n"
                "        float hs_bs = PS_TEXCOORD.z;\n"
                "        if (hs_bs < 0.5)      colour.rgba = texture2D(HS_BATCH_TEX0, PS_TEXCOORD.xy);\n"
                "        else if (hs_bs < 1.5) colour.rgba = texture2D(HS_BATCH_TEX1, PS_TEXCOORD.xy);\n"
                "        else if (hs_bs < 2.5) colour.rgba = texture2D(HS_BATCH_TEX2, PS_TEXCOORD.xy);\n"
                "        else if (hs_bs < 3.5) colour.rgba = texture2D(HS_BATCH_TEX3, PS_TEXCOORD.xy);\n"
                "        else if (hs_bs < 4.5) colour.rgba = texture2D(HS_BATCH_TEX4, PS_TEXCOORD.xy);\n"
                "        else if (hs_bs < 5.5) colour.rgba = texture2D(HS_BATCH_TEX5, PS_TEXCOORD.xy);\n"
                "        else if (hs_bs < 6.5) colour.rgba = texture2D(HS_BATCH_TEX6, PS_TEXCOORD.xy);\n"
                "        else                  colour.rgba = texture2D(HS_BATCH_TEX7, PS_TEXCOORD.xy);\n"
                "    } else {\n" +
                origSampleBlock +
                "\n    }";
            s.replace(posTriple, (posEndif + 6) - posTriple, wrapped);
            static constexpr const char* kColXformOrig = "gl_FragColor.rgba = colour.rgba * COLOUR_XFORM;";
            const size_t posCol = s.find(kColXformOrig);
            if (posCol != std::string::npos) {
                s.replace(posCol, std::strlen(kColXformOrig),
                          "gl_FragColor.rgba = (HS_BATCH_ACTIVE > 0.5) ? "
                          "vec4(colour.rgb * HS_V_CMULT.rgb + colour.a * HS_V_CADD, colour.a * HS_V_CMULT.a) : "
                          "(colour.rgba * COLOUR_XFORM);");
            }
            static constexpr const char* kBatchFsDecls =
                "uniform float HS_BATCH_ACTIVE;\n"
                "varying vec4 HS_V_CMULT;\n"
                "varying vec3 HS_V_CADD;\n"
                "uniform sampler2D HS_BATCH_TEX0;\n"
                "uniform sampler2D HS_BATCH_TEX1;\n"
                "uniform sampler2D HS_BATCH_TEX2;\n"
                "uniform sampler2D HS_BATCH_TEX3;\n"
                "uniform sampler2D HS_BATCH_TEX4;\n"
                "uniform sampler2D HS_BATCH_TEX5;\n"
                "uniform sampler2D HS_BATCH_TEX6;\n"
                "uniform sampler2D HS_BATCH_TEX7;\n";
            s.insert(FindDeclInsertOffset(s), kBatchFsDecls);
            outSrc = std::move(s);
            outTag = "ANIM_BATCH_FS";
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
        const bool needRebind = (curBoundBuf != bufId);
        if (needRebind) ops.glBindBuffer(target, bufId);
        int32_t bufSz = 0;
        ops.glGetBufferParameteriv(target, kGlBufferSize, &bufSz);
        if (bufSz <= 0 || static_cast<size_t>(bufSz) > kMaxShadowBufBytes) {
            if (needRebind) ops.glBindBuffer(target, curBoundBuf);
            if (static_cast<size_t>(bufSz) > kMaxShadowBufBytes) MarkDynamic(bufId); // skip future queries
            return nullptr;
        }
        const size_t size = static_cast<size_t>(bufSz);
        void* mapped = ops.glMapBufferRange(target, 0, static_cast<ptrdiff_t>(size), kMapReadBit);
        if (!mapped) {
            if (needRebind) ops.glBindBuffer(target, curBoundBuf);
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
        if (needRebind) ops.glBindBuffer(target, curBoundBuf);
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
    }

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
    SpriteBatcher() = default;
    ~SpriteBatcher() = default;
    SpriteBatcher(const SpriteBatcher&) = delete;
    SpriteBatcher& operator=(const SpriteBatcher&) = delete;

    void SetOps(const GlBatchOps& ops) noexcept {
        ops_ = ops;
    }

    void SetEnabled(bool en) noexcept {
        if (!en) {
            if (HasPending()) Flush();
            CommitDeferredMatrixW();
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

    void ReleaseGpuResources() noexcept {
        if (ops_.glDeleteBuffers) {
            for (uint32_t i = 0; i < kRingSlots; ++i) {
                if (vboIds_[i] != 0u) {
                    ops_.glDeleteBuffers(1, &vboIds_[i]);
                    vboIds_[i] = 0u;
                }
                if (iboIds_[i] != 0u) {
                    ops_.glDeleteBuffers(1, &iboIds_[i]);
                    iboIds_[i] = 0u;
                }
            }
        } else {
            for (uint32_t i = 0; i < kRingSlots; ++i) {
                vboIds_[i] = 0u;
                iboIds_[i] = 0u;
            }
        }
        ringReady_  = false;
        ringFailed_ = false;
        ringIdx_    = 0u;
    }

    void ResetContext(void* newCtx, bool deleteGpuOnCurrentCtx = false) {
        if (deleteGpuOnCurrentCtx) {
            ReleaseGpuResources();
        } else if (newCtx != curCtx_ || newCtx == nullptr) {
            ringReady_  = false;
            ringFailed_ = false;
            ringIdx_    = 0u;
            for (uint32_t i = 0; i < kRingSlots; ++i) {
                vboIds_[i] = 0u;
                iboIds_[i] = 0u;
            }
        }
        // When newCtx == curCtx_ && newCtx != nullptr && !deleteGpuOnCurrentCtx (e.g., failed
        // same-context eglMakeCurrent where pre-switch cleanup was skipped), preserve live
        // vboIds_/iboIds_/ringReady_ so handles are NEVER orphaned and ReleaseGpuResources()
        // still frees every allocated buffer cleanly!
        curCtx_          = newCtx;
        maxTexSlots_     = 0;
        texUnitsProbed_  = false;
        vertCount_       = 0;
        indexCount_      = 0;
        batchTexCount_   = 0;
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
        attribEnMask_    = 0;
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
            for (int32_t i = 0; i < n && !hit; ++i) {
                for (uint32_t s = 0; s < batchTexCount_; ++s) {
                    if (batchTexIds_[s] == textures[i]) { hit = true; break; }
                }
            }
            if (hit) Flush();
        }
        for (int32_t i = 0; i < n; ++i) {
            for (uint32_t u = 0; u < 16u; ++u) {
                if (boundTex2D_[u] == textures[i]) boundTex2D_[u] = 0;
            }
        }
    }

    void OnUseProgram(uint32_t progId, BatchProgInfo* info) {
        if (progId != curProgId_ || info != curProg_) {
            Flush(FlushReason::Prog);
            CommitDeferredMatrixW();
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

    void OnUniform1i(int32_t location, int32_t val) {
        if (curProg_ && curProg_->eligible) {
            if (location == curProg_->locSampler0) { curProg_->samplerUnit0 = val; return; }
            if (location == curProg_->locSampler1) { curProg_->samplerUnit1 = val; return; }
            if (location == curProg_->locSampler5) { curProg_->samplerUnit5 = val; return; }
        }
        if (HasPending()) Flush(FlushReason::UniOther);
    }

    void OnUniform3fv(int32_t location, int32_t count, const float* v) {
        if (curProg_ && curProg_->eligible && location >= 0 && count == 1 && v != nullptr) {
            if (location == curProg_->locFloatParams) {
                curProg_->lastFloatParamsZ = v[2];
            } else if (location == curProg_->locParams) {
                curProg_->lastParamsY = v[1];
            }
        }
        if (HasPending()) Flush(FlushReason::Uni3);
    }

    void OnAnyStateOrUniformChange(FlushReason reason = FlushReason::State) {
        if (HasPending()) Flush(reason);
    }

    // Must be called immediately before any unbatched glDrawArrays / glDrawElements
    void PrepareForUnbatchedDraw() {
        if (HasPending()) Flush(FlushReason::Bypass);
        CommitDeferredMatrixW();
        ++stats_.bypassDraws;
    }

    void CommitDeferredMatrixW() {
        if (!curProg_ || !ops_.glUniformMatrix4fv) return;
        if (matrixWDirty_ && curProg_->locMatrixW >= 0) {
            ops_.glUniformMatrix4fv(curProg_->locMatrixW, 1, 0, curMatrixW_);
            matrixWDirty_ = false;
        }
        if (colourXformDirty_ && curProg_->locColourXform >= 0) {
            ops_.glUniformMatrix4fv(curProg_->locColourXform, 1, 0, curColourXform_);
            colourXformDirty_ = false;
        }
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
        const uint8_t* vBase = ResolveVertexPtr(attr, static_cast<size_t>(first + count) * stride);
        if (!vBase) { lastRejectCode_ = 20; return false; }

        ++stats_.drawsIn;
        if (vertCount_ + vCount > kMaxBatchVerts || indexCount_ + outIdxCount > kMaxBatchIndices) {
            Flush(FlushReason::Full);
        }
        if (!EnsureSamplerSlotsForVertices(vBase, stride, static_cast<uint32_t>(first), vCount, nullptr, 0)) {
            lastRejectCode_ = 21;
            return false;
        }

        const uint32_t baseOutVert = vertCount_;
        const uint32_t baseOutIdx  = indexCount_;
        const float* m = curMatrixW_;
        const float rLen = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        const float invR = (rLen > 1e-4f) ? (1.0f / rLen) : 1.0f;
        const float rux = m[0] * invR, ruy = m[1] * invR, ruz = m[2] * invR;
        const bool isStanding = IsCurrentMatrixWStanding();
        const bool canShadow  = isStanding &&
                                (!skipTranslucentShadows_ || curCMult_[3] >= 0.72f) &&
                                (curProg_->lastParamsY <= 0.35f);
        const float shadowFlag = canShadow ? 1.0f : 0.0f;
        const bool doFoliage = isStanding &&
            ((foliageClass == 1u && (std::fabs(foliageSwayBase) > 1e-4f || std::fabs(foliageWakeBase) > 1e-4f)) ||
             (foliageClass == 2u && std::fabs(foliageSwayBase) > 1e-4f));
        float swayAxisX = 0.70710678f, swayAxisZ = -0.70710678f;
        if (doFoliage) {
            const float ax = hasCamRight_ ? camRightX_ : rux;
            const float az = hasCamRight_ ? camRightZ_ : ruz;
            const float aLen = std::sqrt(ax * ax + az * az);
            if (aLen > 1e-4f) {
                swayAxisX = ax / aLen;
                swayAxisZ = az / aLen;
            }
        }
        float maxWy = -1e9f;
        for (uint32_t k = 0; k < vCount; ++k) {
            const float* src = reinterpret_cast<const float*>(vBase + (static_cast<size_t>(first) + k) * stride);
            const float x = src[0], y = src[1], uEnc = src[2], v = src[3];
            const int32_t slot = ResolveVertexTexSlot(uEnc);
            if (slot < 0) {
                vertCount_  = baseOutVert;
                indexCount_ = baseOutIdx;
                return false;
            }
            const float dx = m[0] * x + m[4] * y;
            const float dy = m[1] * x + m[5] * y;
            const float dz = m[2] * x + m[6] * y;
            float wx = dx + m[12];
            const float wy = dy + m[13];
            if (wy > maxWy) maxWy = wy;
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
            dst.slot = static_cast<float>(slot);
            dst.mr   = curCMult_[0];
            dst.mg   = curCMult_[1];
            dst.mb   = curCMult_[2];
            dst.ma   = curCMult_[3];
            dst.ar   = curCAdd_[0];
            dst.ag   = curCAdd_[1];
            dst.ab   = curCAdd_[2];
            dst.pad  = (hasCamRight_ ? (dx * camRightX_ + dz * camRightZ_)
                                     : (dx * rux + dy * ruy + dz * ruz)) + horizOff;
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
        const uint8_t* vBase = ResolveVertexPtr(attr, static_cast<size_t>(maxIdx + 1u) * stride);
        if (!vBase) { lastRejectCode_ = 23; return false; }

        ++stats_.drawsIn;
        if (vertCount_ + spanVerts > kMaxBatchVerts || indexCount_ + outIdxCount > kMaxBatchIndices) {
            Flush(FlushReason::Full);
        }
        if (!EnsureSamplerSlotsForVertices(vBase, stride, minIdx, spanVerts, idxSrc, rawCount)) {
            lastRejectCode_ = 24;
            return false;
        }

        const uint32_t baseOutVert = vertCount_;
        const float* m = curMatrixW_;
        const float rLen = std::sqrt(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        const float invR = (rLen > 1e-4f) ? (1.0f / rLen) : 1.0f;
        const float rux = m[0] * invR, ruy = m[1] * invR, ruz = m[2] * invR;
        const bool isStanding = IsCurrentMatrixWStanding();
        const bool canShadow  = isStanding &&
                                (!skipTranslucentShadows_ || curCMult_[3] >= 0.72f) &&
                                (curProg_->lastParamsY <= 0.35f);
        const float shadowFlag = canShadow ? 1.0f : 0.0f;
        const bool doFoliage = isStanding &&
            ((foliageClass == 1u && (std::fabs(foliageSwayBase) > 1e-4f || std::fabs(foliageWakeBase) > 1e-4f)) ||
             (foliageClass == 2u && std::fabs(foliageSwayBase) > 1e-4f));
        float swayAxisX = 0.70710678f, swayAxisZ = -0.70710678f;
        if (doFoliage) {
            const float ax = hasCamRight_ ? camRightX_ : rux;
            const float az = hasCamRight_ ? camRightZ_ : ruz;
            const float aLen = std::sqrt(ax * ax + az * az);
            if (aLen > 1e-4f) {
                swayAxisX = ax / aLen;
                swayAxisZ = az / aLen;
            }
        }
        // Copy only the referenced vertex span [minIdx .. maxIdx] into our batch VBO!
        float maxWy = -1e9f;
        for (uint32_t k = 0; k < spanVerts; ++k) {
            const float* src = reinterpret_cast<const float*>(vBase + (static_cast<size_t>(minIdx) + k) * stride);
            const float x = src[0], y = src[1], uEnc = src[2], v = src[3];
            const int32_t slot = ResolveVertexTexSlot(uEnc);
            const float dx = m[0] * x + m[4] * y;
            const float dy = m[1] * x + m[5] * y;
            const float dz = m[2] * x + m[6] * y;
            float wx = dx + m[12];
            const float wy = dy + m[13];
            if (wy > maxWy) maxWy = wy;
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
            dst.slot = static_cast<float>(slot >= 0 ? slot : 0);
            dst.mr   = curCMult_[0];
            dst.mg   = curCMult_[1];
            dst.mb   = curCMult_[2];
            dst.ma   = curCMult_[3];
            dst.ar   = curCAdd_[0];
            dst.ag   = curCAdd_[1];
            dst.ab   = curCAdd_[2];
            dst.pad  = (hasCamRight_ ? (dx * camRightX_ + dz * camRightZ_)
                                     : (dx * rux + dy * ruy + dz * ruz)) + horizOff;
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
        const uint32_t flushVerts   = vertCount_;
        const uint32_t flushIndices = indexCount_;
        const uint32_t flushTexCnt  = batchTexCount_;
        const bool     mirrorShadow = batchHasShadowCaster_;
        lastFlushHasShadowBounds_ = mirrorShadow && (shadowMinX_ <= shadowMaxX_) && (shadowMinZ_ <= shadowMaxZ_);
        if (lastFlushHasShadowBounds_) {
            lastFlushShadowBounds_[0] = shadowMinX_;
            lastFlushShadowBounds_[1] = shadowMaxX_;
            lastFlushShadowBounds_[2] = shadowMinZ_;
            lastFlushShadowBounds_[3] = shadowMaxZ_;
        }
        vertCount_     = 0;
        indexCount_    = 0;
        batchTexCount_ = 0;
        batchHasShadowCaster_ = false;
        shadowMinX_ = 1e30f;
        shadowMaxX_ = -1e30f;
        shadowMinZ_ = 1e30f;
        shadowMaxZ_ = -1e30f;

        if (!curProg_ || !curProg_->eligible || !EnsureGpuRingReady()) {
            return;
        }
        if (!ops_.glBindBuffer || !ops_.glActiveTexture || !ops_.glBindTexture ||
            !ops_.glUniform1f || !ops_.glVertexAttribPointer ||
            !ops_.glEnableVertexAttribArray || !ops_.glDisableVertexAttribArray ||
            !ops_.glDrawElements) {
            return;
        }

        // One-time sampler unit binding (HS_BATCH_TEX0..7 -> GL_TEXTURE6..13) on this program
        if (!curProg_->samplersInit && ops_.glUniform1i) {
            for (uint32_t s = 0; s < maxTexSlots_; ++s) {
                if (curProg_->locBatchTex[s] >= 0) {
                    ops_.glUniform1i(curProg_->locBatchTex[s], static_cast<int32_t>(kBatchTexUnitBase + s));
                }
            }
            curProg_->samplersInit = true;
        }

        const uint32_t slot = ringIdx_;
        ringIdx_ = (ringIdx_ + 1u) % kRingSlots;
        const uint32_t vbo = vboIds_[slot];
        const uint32_t ibo = iboIds_[slot];
        const size_t vBytes = static_cast<size_t>(flushVerts) * sizeof(BatchedVertex);
        const size_t iBytes = static_cast<size_t>(flushIndices) * sizeof(uint16_t);

        UploadRingSlice(kGlArrayBuffer, vbo, verts_.data(), vBytes,
                        static_cast<size_t>(kMaxBatchVerts) * sizeof(BatchedVertex));
        UploadRingSlice(kGlElementArrayBuffer, ibo, indices_.data(), iBytes,
                        static_cast<size_t>(kMaxBatchIndices) * sizeof(uint16_t));

        // Bind up to 8 entity atlas textures onto unused units GL_TEXTURE6..13
        for (uint32_t s = 0; s < flushTexCnt; ++s) {
            ops_.glActiveTexture(kGlTexture0 + kBatchTexUnitBase + s);
            ops_.glBindTexture(kGlTexture2D, batchTexIds_[s]);
        }

        ops_.glUniform1f(curProg_->locBatchActive, 1.0f);

        const uint32_t posLoc  = static_cast<uint32_t>(curProg_->locPos2dUv);
        const uint32_t wposLoc = static_cast<uint32_t>(curProg_->locBatchWpos);
        const bool     useCol  = (curProg_->locBatchCMult >= 0 && curProg_->locBatchCAdd >= 0);
        ops_.glVertexAttribPointer(posLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                   reinterpret_cast<const void*>(offsetof(BatchedVertex, x)));
        ops_.glEnableVertexAttribArray(wposLoc);
        ops_.glVertexAttribPointer(wposLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                   reinterpret_cast<const void*>(offsetof(BatchedVertex, wx)));
        if (useCol) {
            const uint32_t cmLoc = static_cast<uint32_t>(curProg_->locBatchCMult);
            const uint32_t caLoc = static_cast<uint32_t>(curProg_->locBatchCAdd);
            ops_.glEnableVertexAttribArray(cmLoc);
            ops_.glVertexAttribPointer(cmLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                       reinterpret_cast<const void*>(offsetof(BatchedVertex, mr)));
            ops_.glEnableVertexAttribArray(caLoc);
            ops_.glVertexAttribPointer(caLoc, 4, kGlFloat, 0, sizeof(BatchedVertex),
                                       reinterpret_cast<const void*>(offsetof(BatchedVertex, ar)));
        }

        ops_.glDrawElements(kGlTriangles, static_cast<int32_t>(flushIndices), kGlUnsignedShort, nullptr);
        ++stats_.flushes;

        // Zero-copy World-Space ShadowMask FBO mirror: reuses the ALREADY-UPLOADED Ring VBO/IBO,
        // ALREADY-BOUND GL_TEXTURE6..13 atlases, and ALREADY-ENABLED attribs 0/5/6/7 in 1 draw call!
        if (mirrorShadow && ops_.onShadowMirrorBatch) {
            ops_.onShadowMirrorBatch(flushIndices, ops_.callbackCtx);
        }

        // Restore GL state cleanly so subsequent unbatched DST draws & gl_state_cache stay 100% exact!
        ops_.glUniform1f(curProg_->locBatchActive, 0.0f);
        ops_.glDisableVertexAttribArray(wposLoc);
        if (useCol) {
            ops_.glDisableVertexAttribArray(static_cast<uint32_t>(curProg_->locBatchCMult));
            ops_.glDisableVertexAttribArray(static_cast<uint32_t>(curProg_->locBatchCAdd));
        }
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

        if (ops_.onPostFlushRestore) {
            ops_.onPostFlushRestore(activeUnit_, boundArrayBuf_, boundElemBuf_, wposLoc, ops_.callbackCtx);
        }
    }

private:
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
        if (curProg_->lastFloatParamsZ > 0.0f) { lastRejectCode_ = 5; return false; } // Ocean-bobbing items stay on exact vanilla path
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
        if (maxUnits >= static_cast<int32_t>(kBatchTexUnitBase + kMaxBatchTexSlots)) {
            maxTexSlots_ = kMaxBatchTexSlots;
        } else if (maxUnits > static_cast<int32_t>(kBatchTexUnitBase)) {
            maxTexSlots_ = static_cast<uint32_t>(maxUnits - static_cast<int32_t>(kBatchTexUnitBase));
        } else {
            maxTexSlots_ = 0;
        }
        return maxTexSlots_ >= 2u;
    }

    bool EnsureGpuRingReady() {
        if (ringReady_) return true;
        if (ringFailed_) return false;
        if (!ops_.glGenBuffers || !ops_.glBindBuffer || !ops_.glBufferData || !ops_.glGetBufferParameteriv) {
            ringFailed_ = true;
            return false;
        }
        if (verts_.empty()) verts_.resize(kMaxBatchVerts);
        if (indices_.empty()) indices_.resize(kMaxBatchIndices);
        for (uint32_t i = 0; i < kRingSlots; ++i) {
            vboIds_[i] = 0u;
            iboIds_[i] = 0u;
        }
        ops_.glGenBuffers(static_cast<int32_t>(kRingSlots), vboIds_);
        ops_.glGenBuffers(static_cast<int32_t>(kRingSlots), iboIds_);
        constexpr ptrdiff_t kVboSlotBytes = static_cast<ptrdiff_t>(kMaxBatchVerts * sizeof(BatchedVertex));
        constexpr ptrdiff_t kIboSlotBytes = static_cast<ptrdiff_t>(kMaxBatchIndices * sizeof(uint16_t));
        for (uint32_t i = 0; i < kRingSlots; ++i) {
            if (vboIds_[i] == 0u || iboIds_[i] == 0u) {
                ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
                ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
                ReleaseGpuResources();
                ringFailed_ = true;
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
                ReleaseGpuResources();
                ringFailed_ = true;
                return false;
            }
        }
        ops_.glBindBuffer(kGlArrayBuffer, boundArrayBuf_);
        ops_.glBindBuffer(kGlElementArrayBuffer, boundElemBuf_);
        ringReady_ = true;
        return true;
    }

    void UploadRingSlice(uint32_t target, uint32_t bufId, const void* data, size_t bytes, size_t maxSlotBytes) {
        ops_.glBindBuffer(target, bufId);
        if (ops_.glMapBufferRange && ops_.glUnmapBuffer && bytes <= maxSlotBytes) {
            void* mapped = ops_.glMapBufferRange(target, 0, static_cast<ptrdiff_t>(bytes), kMapWriteInvalidateUnsync);
            if (mapped) {
                std::memcpy(mapped, data, bytes);
                if (ops_.glUnmapBuffer(target) != 0) {
                    return;
                }
            }
        }
        // Portable orphan + upload fallback (ANGLE D3D9 & drivers without MapBufferRange).
        // Preserve full maxSlotBytes allocation if MapBufferRange exists so future slices never fail.
        if (ops_.glBufferSubData && ops_.glMapBufferRange && bytes <= maxSlotBytes) {
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
        if (batchTexCount_ < maxTexSlots_) {
            batchTexIds_[batchTexCount_] = texId;
            return static_cast<int32_t>(batchTexCount_++);
        }
        return -1;
    }

    int32_t ResolveVertexTexSlot(float uEnc) const noexcept {
        const uint32_t texId = SamplerIndexToTexId(uEnc);
        if (texId == 0u) return -1;
        for (uint32_t s = 0; s < batchTexCount_; ++s) {
            if (batchTexIds_[s] == texId) return static_cast<int32_t>(s);
        }
        return -1;
    }

    bool EnsureSamplerSlotsForVertices(const uint8_t* vBase, size_t stride,
                                       uint32_t startVert, uint32_t vertCount,
                                       const uint16_t* idxSrc, uint32_t idxCount) {
        // Collect the 1..3 distinct textures referenced by this sprite (fast O(N) bitmask)
        uint32_t needed[3] = { 0, 0, 0 };
        uint32_t neededCnt = 0;
        uint8_t seenSi = 0;
        auto visit = [&](uint32_t vIdx) -> bool {
            const float* src = reinterpret_cast<const float*>(vBase + static_cast<size_t>(vIdx) * stride);
            const int si = static_cast<int>(floorf(src[2] * 0.5f));
            const uint8_t bit = (si <= 0) ? 1u : ((si == 1) ? 2u : 4u);
            if (seenSi & bit) return true;
            seenSi |= bit;
            const uint32_t texId = SamplerIndexToTexId(src[2]);
            if (texId == 0u) return false;
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
        if (neededCnt == 0u) return false;

        // Check how many NEW texture slots this sprite needs
        uint32_t newNeeded = 0;
        for (uint32_t k = 0; k < neededCnt; ++k) {
            bool exists = false;
            for (uint32_t s = 0; s < batchTexCount_; ++s) {
                if (batchTexIds_[s] == needed[k]) { exists = true; break; }
            }
            if (!exists) ++newNeeded;
        }
        if (batchTexCount_ + newNeeded > maxTexSlots_) {
            if (newNeeded > maxTexSlots_) return false;
            Flush(FlushReason::Tex);
        }
        for (uint32_t k = 0; k < neededCnt; ++k) {
            if (FindOrAddTexSlot(needed[k]) < 0) return false;
        }
        return true;
    }

    GlBatchOps        ops_{};
    ShadowBufferCache shadowCache_;

    void*          curCtx_          = nullptr;
    bool           enabled_         = false;
    bool           ringReady_       = false;
    bool           ringFailed_      = false;
    bool           texUnitsProbed_  = false;
    uint32_t       maxTexSlots_     = 0;
    uint32_t       ringIdx_         = 0;
    uint32_t       vboIds_[kRingSlots] = {};
    uint32_t       iboIds_[kRingSlots] = {};

    std::vector<BatchedVertex> verts_;
    std::vector<uint16_t>      indices_;
    uint32_t                   vertCount_ = 0;
    uint32_t                   indexCount_ = 0;
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
