#pragma once

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace hs_gl_cache {

static constexpr uint32_t kMaxTexUnits          = 16;
static constexpr uint32_t kMaxVertexAttribs     = 16;
static constexpr uint32_t kMaxUniProgs          = 64;
static constexpr uint32_t kMaxUniLocs           = 64;
static constexpr uint32_t kInvalidId            = 0xFFFFFFFFu;
static constexpr uint32_t kGlTexture0           = 0x84C0u;
static constexpr uint32_t kGlTexture2D          = 0x0DE1u;
static constexpr uint32_t kGlArrayBuffer        = 0x8892u;
static constexpr uint32_t kGlElementArrayBuffer = 0x8893u;

enum UniformTag : uint8_t {
    UNI_NONE = 0,
    UNI_1I   = 1,
    UNI_1F   = 2,
    UNI_2FV  = 3,
    UNI_3FV  = 4,
    UNI_4FV  = 5,
    UNI_MAT3 = 6,
    UNI_MAT4 = 7,
};

struct UniformSlot {
    uint8_t  tag = UNI_NONE;
    uint32_t words[16] = {};
};

struct UniformProgBucket {
    uint32_t    progId = 0;
    uint32_t    stamp  = 0;
    UniformSlot slots[kMaxUniLocs];
};

struct UniformTable {
    uint32_t          clock = 1;
    UniformProgBucket buckets[kMaxUniProgs];
};

// Safe GLES2 boolean capabilities (strictly whitelisted; unknown enums always pass through!)
static constexpr uint32_t kGlBlend              = 0x0BE2u;
static constexpr uint32_t kGlDepthTest          = 0x0B71u;
static constexpr uint32_t kGlCullFace           = 0x0B44u;
static constexpr uint32_t kGlScissorTest        = 0x0C11u;
static constexpr uint32_t kGlStencilTest        = 0x0B90u;
static constexpr uint32_t kGlPolygonOffsetFill  = 0x8037u;
static constexpr size_t   kNumCaps              = 6;

inline int CapToIndex(uint32_t cap) {
    switch (cap) {
        case kGlBlend:             return 0;
        case kGlDepthTest:         return 1;
        case kGlCullFace:          return 2;
        case kGlScissorTest:       return 3;
        case kGlStencilTest:       return 4;
        case kGlPolygonOffsetFill: return 5;
        default:                   return -1;
    }
}

// Per-thread GLES2 state mirror for eliminating redundant ANGLE 1.0 (D3D9) translation calls.
// Invariants:
//   - Initial state is UNKNOWN so the first call of every category ALWAYS forwards to ANGLE.
//   - Reset() is called on every eglMakeCurrent (context switch / recreation / detach).
//   - OnDeleteTextures / OnDeleteProgram / OnLinkProgram invalidate affected slots immediately
//     to guarantee zero handle-reuse aliasing.
struct ThreadState {
    void*    curContext      = nullptr;
    int32_t  activeUnit      = -1;
    uint32_t curProgram      = kInvalidId;
    uint32_t boundArrayBuf   = kInvalidId;
    uint32_t boundElementBuf = kInvalidId;
    uint32_t blendSrc        = kInvalidId;
    uint32_t blendDst        = kInvalidId;
    uint32_t blendEq         = kInvalidId;
    uint32_t cullFace        = kInvalidId;
    uint32_t depthFunc       = kInvalidId;
    uint32_t frontFace       = kInvalidId;
    int8_t   depthMask       = -1;
    int8_t   colorMask       = -1;
    uint16_t attribKnownMask   = 0;
    uint16_t attribEnabledMask = 0;
    bool     vpValid         = false;
    int32_t  vp[4]           = {};
    bool     scValid         = false;
    int32_t  sc[4]           = {};
    int8_t   caps[kNumCaps] = { -1, -1, -1, -1, -1, -1 };
    uint32_t boundTex2D[kMaxTexUnits] = {
        kInvalidId, kInvalidId, kInvalidId, kInvalidId,
        kInvalidId, kInvalidId, kInvalidId, kInvalidId,
        kInvalidId, kInvalidId, kInvalidId, kInvalidId,
        kInvalidId, kInvalidId, kInvalidId, kInvalidId
    };

    UniformTable* uniTable = nullptr;

    uint32_t batchCalls    = 0;
    uint32_t batchHits     = 0;
    uint32_t batchUniCalls = 0;
    uint32_t batchUniHits  = 0;

    ThreadState() = default;
    ~ThreadState() {
        std::free(uniTable);
        uniTable = nullptr;
    }
    ThreadState(const ThreadState&) = delete;
    ThreadState& operator=(const ThreadState&) = delete;

    static inline uint32_t UniHash(uint32_t prog) {
        return ((prog * 2654435761u) ^ (prog >> 7)) & (kMaxUniProgs - 1);
    }

    UniformProgBucket* FindUniBucket(uint32_t program) {
        if (!uniTable || program == 0u || program == kInvalidId) return nullptr;
        const uint32_t base = UniHash(program);
        for (uint32_t p = 0; p < 8u; ++p) {
            UniformProgBucket& b = uniTable->buckets[(base + p) & (kMaxUniProgs - 1)];
            if (b.progId == program) return &b;
        }
        return nullptr;
    }

    UniformProgBucket* FindOrAllocUniBucket(uint32_t program) {
        if (program == 0u || program == kInvalidId) return nullptr;
        if (!uniTable) {
            uniTable = static_cast<UniformTable*>(std::calloc(1, sizeof(UniformTable)));
            if (!uniTable) return nullptr;
            uniTable->clock = 1;
        }
        const uint32_t base = UniHash(program);
        UniformProgBucket* emptySlot = nullptr;
        UniformProgBucket* lruSlot   = &uniTable->buckets[base];
        for (uint32_t p = 0; p < 8u; ++p) {
            UniformProgBucket& b = uniTable->buckets[(base + p) & (kMaxUniProgs - 1)];
            if (b.progId == program) {
                b.stamp = ++uniTable->clock;
                return &b;
            }
            if (b.progId == 0u && !emptySlot) emptySlot = &b;
            if (b.stamp < lruSlot->stamp) lruSlot = &b;
        }
        UniformProgBucket* dst = emptySlot ? emptySlot : lruSlot;
        std::memset(dst, 0, sizeof(*dst)); // Wipe ALL previous slots on eviction -> zero stale leaks!
        dst->progId = program;
        dst->stamp  = ++uniTable->clock;
        return dst;
    }

    // Lightweight post-SwapBuffers barrier: clears cached GL bindings/caps/program/buffers/textures
    // while preserving curContext and per-program uniform shadow values (immune to Steam/RTSS/GPU overlays!).
    void ResetBindingsOnly() noexcept {
        activeUnit        = -1;
        curProgram        = kInvalidId;
        boundArrayBuf     = kInvalidId;
        boundElementBuf   = kInvalidId;
        blendSrc          = kInvalidId;
        blendDst          = kInvalidId;
        blendEq           = kInvalidId;
        cullFace          = kInvalidId;
        depthFunc         = kInvalidId;
        frontFace         = kInvalidId;
        depthMask         = -1;
        colorMask         = -1;
        attribKnownMask   = 0;
        attribEnabledMask = 0;
        vpValid           = false;
        scValid           = false;
        for (size_t i = 0; i < kNumCaps; ++i) caps[i] = -1;
        for (uint32_t i = 0; i < kMaxTexUnits; ++i) boundTex2D[i] = kInvalidId;
    }

    void Reset() {
        ResetBindingsOnly();
        if (uniTable) {
            std::memset(uniTable, 0, sizeof(UniformTable));
            uniTable->clock = 1;
        }
    }

    void OnMakeCurrent(void* ctx) {
        Reset();
        curContext = ctx;
    }

    // Returns true if the real GL call MUST be invoked; false if redundant (skipped).
    bool ShouldCallActiveTexture(uint32_t textureEnum) {
        ++batchCalls;
        if (textureEnum >= kGlTexture0 && textureEnum < (kGlTexture0 + kMaxTexUnits)) {
            const int32_t unit = static_cast<int32_t>(textureEnum - kGlTexture0);
            if (activeUnit == unit) {
                ++batchHits;
                return false;
            }
            activeUnit = unit;
            return true;
        }
        activeUnit = -1;
        return true;
    }

    bool ShouldCallBindTexture(uint32_t target, uint32_t texture) {
        ++batchCalls;
        if (target == kGlTexture2D && activeUnit >= 0 && activeUnit < static_cast<int32_t>(kMaxTexUnits)) {
            if (boundTex2D[activeUnit] == texture) {
                ++batchHits;
                return false;
            }
            boundTex2D[activeUnit] = texture;
            return true;
        }
        return true;
    }

    void OnDeleteTextures(int32_t n, const uint32_t* textures) {
        if (n <= 0 || !textures) return;
        for (int32_t i = 0; i < n; ++i) {
            const uint32_t id = textures[i];
            if (id == 0) continue;
            for (uint32_t u = 0; u < kMaxTexUnits; ++u) {
                if (boundTex2D[u] == id) {
                    boundTex2D[u] = kInvalidId;
                }
            }
        }
    }

    bool ShouldCallBindBuffer(uint32_t target, uint32_t buffer) {
        ++batchCalls;
        uint32_t* slot = nullptr;
        if (target == kGlArrayBuffer) slot = &boundArrayBuf;
        else if (target == kGlElementArrayBuffer) slot = &boundElementBuf;
        else return true;
        if (*slot == buffer) {
            ++batchHits;
            return false;
        }
        *slot = buffer;
        return true;
    }

    void OnDeleteBuffers(int32_t n, const uint32_t* buffers) {
        if (n <= 0 || !buffers) return;
        for (int32_t i = 0; i < n; ++i) {
            const uint32_t id = buffers[i];
            if (id == 0) continue;
            if (boundArrayBuf == id) boundArrayBuf = kInvalidId;
            if (boundElementBuf == id) boundElementBuf = kInvalidId;
        }
    }

    bool ShouldCallEnableDisableVertexAttribArray(uint32_t index, bool enable) {
        ++batchCalls;
        if (index >= kMaxVertexAttribs) return true;
        const uint16_t bit = static_cast<uint16_t>(1u << index);
        if ((attribKnownMask & bit) != 0 && (((attribEnabledMask & bit) != 0) == enable)) {
            ++batchHits;
            return false;
        }
        attribKnownMask |= bit;
        if (enable) attribEnabledMask |= bit;
        else attribEnabledMask &= static_cast<uint16_t>(~bit);
        return true;
    }

    // Fail-closed glUseProgram mirror: only cache program==0 or a program verified
    // linked (GL_LINK_STATUS != 0) in the active context. Unverified/failed programs
    // always pass through and invalidate curProgram so ANGLE rejection never desyncs state.
    bool ShouldCallUseProgramChecked(uint32_t program, bool programVerifiedLinked) {
        ++batchCalls;
        if (program != 0u && !programVerifiedLinked) {
            curProgram = kInvalidId;
            return true;
        }
        if (curProgram == program) {
            ++batchHits;
            return false;
        }
        curProgram = program;
        return true;
    }

    bool ShouldCallUseProgram(uint32_t program) {
        return ShouldCallUseProgramChecked(program, true);
    }

    void OnDeleteOrLinkProgram(uint32_t program) {
        if (curProgram == program) {
            curProgram = kInvalidId;
        }
        if (UniformProgBucket* b = FindUniBucket(program)) {
            std::memset(b, 0, sizeof(*b));
        }
    }

    bool ShouldCallBlendFunc(uint32_t sfactor, uint32_t dfactor) {
        ++batchCalls;
        if (blendSrc == sfactor && blendDst == dfactor) {
            ++batchHits;
            return false;
        }
        blendSrc = sfactor;
        blendDst = dfactor;
        return true;
    }

    bool ShouldCallBlendEquation(uint32_t mode) {
        ++batchCalls;
        if (blendEq == mode) {
            ++batchHits;
            return false;
        }
        blendEq = mode;
        return true;
    }

    bool ShouldCallCullFace(uint32_t mode) {
        ++batchCalls;
        if (cullFace == mode) {
            ++batchHits;
            return false;
        }
        cullFace = mode;
        return true;
    }

    bool ShouldCallDepthFunc(uint32_t func) {
        ++batchCalls;
        if (depthFunc == func) {
            ++batchHits;
            return false;
        }
        depthFunc = func;
        return true;
    }

    bool ShouldCallFrontFace(uint32_t mode) {
        ++batchCalls;
        if (frontFace == mode) {
            ++batchHits;
            return false;
        }
        frontFace = mode;
        return true;
    }

    bool ShouldCallColorMask(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        ++batchCalls;
        const int8_t packed = static_cast<int8_t>((r ? 1 : 0) | (g ? 2 : 0) | (b ? 4 : 0) | (a ? 8 : 0));
        if (colorMask == packed) {
            ++batchHits;
            return false;
        }
        colorMask = packed;
        return true;
    }

    bool ShouldCallDepthMask(uint8_t flag) {
        ++batchCalls;
        const int8_t v = (flag != 0) ? 1 : 0;
        if (depthMask == v) {
            ++batchHits;
            return false;
        }
        depthMask = v;
        return true;
    }

    bool ShouldCallViewport(int32_t x, int32_t y, int32_t w, int32_t h) {
        ++batchCalls;
        if (w < 0 || h < 0) {
            vpValid = false;
            return true;
        }
        if (vpValid && vp[0] == x && vp[1] == y && vp[2] == w && vp[3] == h) {
            ++batchHits;
            return false;
        }
        vpValid = true;
        vp[0] = x; vp[1] = y; vp[2] = w; vp[3] = h;
        return true;
    }

    bool ShouldCallScissor(int32_t x, int32_t y, int32_t w, int32_t h) {
        ++batchCalls;
        if (w < 0 || h < 0) {
            scValid = false;
            return true;
        }
        if (scValid && sc[0] == x && sc[1] == y && sc[2] == w && sc[3] == h) {
            ++batchHits;
            return false;
        }
        scValid = true;
        sc[0] = x; sc[1] = y; sc[2] = w; sc[3] = h;
        return true;
    }

    bool ShouldCallEnableDisable(uint32_t cap, bool enable) {
        ++batchCalls;
        const int idx = CapToIndex(cap);
        if (idx >= 0) {
            const int8_t v = enable ? 1 : 0;
            if (caps[idx] == v) {
                ++batchHits;
                return false;
            }
            caps[idx] = v;
            return true;
        }
        return true;
    }

    bool ShouldCallUniform(int32_t loc, int32_t count, uint8_t transpose,
                           uint8_t tag, const void* data, size_t byteLen) {
        ++batchUniCalls;
        if (curProgram == 0u || curProgram == kInvalidId ||
            loc < 0 || loc >= static_cast<int32_t>(kMaxUniLocs) ||
            count != 1 || transpose != 0 || !data || byteLen == 0 || byteLen > 64) {
            return true;
        }
        UniformProgBucket* b = FindOrAllocUniBucket(curProgram);
        if (!b) return true;
        UniformSlot& s = b->slots[loc];
        if (s.tag == tag && std::memcmp(s.words, data, byteLen) == 0) {
            ++batchUniHits;
            return false;
        }
        s.tag = tag;
        std::memcpy(s.words, data, byteLen);
        return true;
    }

    void InvalidateUniform(uint32_t program, int32_t loc) {
        if (loc < 0 || loc >= static_cast<int32_t>(kMaxUniLocs)) return;
        if (UniformProgBucket* b = FindUniBucket(program)) {
            b->slots[loc].tag = UNI_NONE;
        }
    }
};

} // namespace hs_gl_cache
