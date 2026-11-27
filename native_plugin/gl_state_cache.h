#pragma once

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace hs_gl_cache {

static constexpr uint32_t kMaxTexUnits          = 24;
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
    UNI_MAT2 = 8,
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
static constexpr uint32_t kGlStencilAlways      = 0x0207u;
static constexpr uint32_t kGlStencilKeep        = 0x1E00u;
static constexpr uint32_t kGlStencilFuncPname   = 0x0B92u;
static constexpr uint32_t kGlStencilValueMaskPname = 0x0B93u;
static constexpr uint32_t kGlStencilFailPname   = 0x0B94u;
static constexpr uint32_t kGlStencilPassDepthFailPname = 0x0B95u;
static constexpr uint32_t kGlStencilPassDepthPassPname = 0x0B96u;
static constexpr uint32_t kGlStencilRefPname    = 0x0B97u;
static constexpr uint32_t kGlStencilWritemaskPname = 0x0B98u;
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
    uint32_t stencilFunc     = kInvalidId;
    int32_t  stencilRef      = 0;
    uint32_t stencilFuncMask = 0;
    bool     stencilFuncValid = false;
    uint32_t drvStencilFunc  = kInvalidId;
    int32_t  drvStencilRef   = 0;
    uint32_t drvStencilFuncMask = 0;
    bool     drvStencilFuncValid = false;
    bool     stencilFuncDirty = false;
    uint32_t stencilWriteMask = 0;
    bool     stencilWriteMaskValid = false;
    uint32_t drvStencilWriteMask = 0;
    bool     drvStencilWriteMaskValid = false;
    bool     stencilWriteMaskDirty = false;
    uint32_t stencilOpFail   = kInvalidId;
    uint32_t stencilOpZfail  = kInvalidId;
    uint32_t stencilOpZpass  = kInvalidId;
    bool     stencilOpValid  = false;
    uint32_t drvStencilOpFail  = kInvalidId;
    uint32_t drvStencilOpZfail = kInvalidId;
    uint32_t drvStencilOpZpass = kInvalidId;
    bool     drvStencilOpValid = false;
    bool     stencilOpDirty    = false;
    float    polyOffFactor   = 0.0f;
    float    polyOffUnits    = 0.0f;
    bool     polyOffValid    = false;
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
        depthMask             = -1;
        colorMask             = -1;
        stencilFunc           = kInvalidId;
        stencilRef            = 0;
        stencilFuncMask       = 0;
        stencilFuncValid      = false;
        drvStencilFunc        = kInvalidId;
        drvStencilRef         = 0;
        drvStencilFuncMask    = 0;
        drvStencilFuncValid   = false;
        stencilFuncDirty      = false;
        stencilWriteMask      = 0;
        stencilWriteMaskValid = false;
        drvStencilWriteMask   = 0;
        drvStencilWriteMaskValid = false;
        stencilWriteMaskDirty = false;
        stencilOpFail         = kInvalidId;
        stencilOpZfail        = kInvalidId;
        stencilOpZpass        = kInvalidId;
        stencilOpValid        = false;
        drvStencilOpFail      = kInvalidId;
        drvStencilOpZfail     = kInvalidId;
        drvStencilOpZpass     = kInvalidId;
        drvStencilOpValid     = false;
        stencilOpDirty        = false;
        polyOffFactor         = 0.0f;
        polyOffUnits          = 0.0f;
        polyOffValid          = false;
        attribKnownMask       = 0;
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

    // Split peek + commit helpers for raster/viewport state hooks:
    //   1) Call Is*Redundant(...) BEFORE flushing pending Sprite/UI batches. On cache HIT it
    //      records the hit and returns true so the hook skips the redundant GL call without
    //      touching the batch. On cache MISS it counts the call WITHOUT mutating the cache!
    //   2) Flush pending Sprite/UI batches (ShadowV2 / WaterRefl capture the TRUE pre-change
    //      GPU state from glCache!).
    //   3) Call Record*(...) and invoke the real GL driver function.
    bool IsBlendFuncRedundant(uint32_t sfactor, uint32_t dfactor) {
        ++batchCalls;
        if (blendSrc == sfactor && blendDst == dfactor) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void RecordBlendFunc(uint32_t sfactor, uint32_t dfactor) noexcept {
        blendSrc = sfactor;
        blendDst = dfactor;
    }
    bool ShouldCallBlendFunc(uint32_t sfactor, uint32_t dfactor) {
        if (IsBlendFuncRedundant(sfactor, dfactor)) return false;
        RecordBlendFunc(sfactor, dfactor);
        return true;
    }

    bool IsBlendEquationRedundant(uint32_t mode) {
        ++batchCalls;
        if (blendEq == mode) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void RecordBlendEquation(uint32_t mode) noexcept {
        blendEq = mode;
    }
    bool ShouldCallBlendEquation(uint32_t mode) {
        if (IsBlendEquationRedundant(mode)) return false;
        RecordBlendEquation(mode);
        return true;
    }

    bool IsCullFaceRedundant(uint32_t mode) {
        ++batchCalls;
        if (cullFace == mode) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void RecordCullFace(uint32_t mode) noexcept {
        cullFace = mode;
    }
    bool ShouldCallCullFace(uint32_t mode) {
        if (IsCullFaceRedundant(mode)) return false;
        RecordCullFace(mode);
        return true;
    }

    bool IsDepthFuncRedundant(uint32_t func) {
        ++batchCalls;
        if (depthFunc == func) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void RecordDepthFunc(uint32_t func) noexcept {
        depthFunc = func;
    }
    bool ShouldCallDepthFunc(uint32_t func) {
        if (IsDepthFuncRedundant(func)) return false;
        RecordDepthFunc(func);
        return true;
    }

    bool IsFrontFaceRedundant(uint32_t mode) {
        ++batchCalls;
        if (frontFace == mode) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void RecordFrontFace(uint32_t mode) noexcept {
        frontFace = mode;
    }
    bool ShouldCallFrontFace(uint32_t mode) {
        if (IsFrontFaceRedundant(mode)) return false;
        RecordFrontFace(mode);
        return true;
    }

    bool IsColorMaskRedundant(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        ++batchCalls;
        const int8_t packed = static_cast<int8_t>((r ? 1 : 0) | (g ? 2 : 0) | (b ? 4 : 0) | (a ? 8 : 0));
        if (colorMask == packed) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void RecordColorMask(uint8_t r, uint8_t g, uint8_t b, uint8_t a) noexcept {
        colorMask = static_cast<int8_t>((r ? 1 : 0) | (g ? 2 : 0) | (b ? 4 : 0) | (a ? 8 : 0));
    }
    bool ShouldCallColorMask(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        if (IsColorMaskRedundant(r, g, b, a)) return false;
        RecordColorMask(r, g, b, a);
        return true;
    }

    bool IsDepthMaskRedundant(uint8_t flag) {
        ++batchCalls;
        const int8_t v = (flag != 0) ? 1 : 0;
        if (depthMask == v) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void RecordDepthMask(uint8_t flag) noexcept {
        depthMask = (flag != 0) ? 1 : 0;
    }
    bool ShouldCallDepthMask(uint8_t flag) {
        if (IsDepthMaskRedundant(flag)) return false;
        RecordDepthMask(flag);
        return true;
    }

    bool IsStencilTestKnownDisabled() const noexcept {
        return caps[4] == 0;
    }
    bool HasDeferredStencilState() const noexcept {
        return stencilFuncDirty || stencilWriteMaskDirty || stencilOpDirty;
    }
    // OpenGL ES 2.0 / 4.6 Spec §4.1.5 exact no-op proofs while GL_STENCIL_TEST == ENABLED:
    //   1. If stencilOp can never write (all KEEP, or func==ALWAYS with zfail==KEEP && zpass==KEEP),
    //      changing glStencilMask has ZERO effect on any draw call (only affects glClear(STENCIL)).
    bool CanDeferStencilMaskWhenEnabled() const noexcept {
        if (!stencilOpValid) return false;
        if (stencilOpZfail != kGlStencilKeep || stencilOpZpass != kGlStencilKeep) return false;
        return (stencilOpFail == kGlStencilKeep) ||
               (stencilFuncValid && stencilFunc == kGlStencilAlways);
    }
    //   2. If logical glStencilMask == 0, NO stencil bit can ever be modified regardless of stencilOp.
    bool CanDeferStencilOpWhenEnabled() const noexcept {
        return stencilWriteMaskValid && stencilWriteMask == 0u;
    }
    //   3. If old and new stencilFunc are both GL_ALWAYS AND stencil writes are provably disabled
    //      (wmask == 0 OR zfail/zpass == KEEP), changing ref/fmask has ZERO effect on any draw call.
    bool CanDeferStencilFuncWhenEnabled(uint32_t newFunc) const noexcept {
        if (newFunc != kGlStencilAlways || !stencilFuncValid || stencilFunc != kGlStencilAlways) {
            return false;
        }
        return CanDeferStencilOpWhenEnabled() || CanDeferStencilMaskWhenEnabled();
    }

    bool IsStencilFuncRedundant(uint32_t func, int32_t ref, uint32_t mask) {
        ++batchCalls;
        if (stencilFuncValid && stencilFunc == func && stencilRef == ref && stencilFuncMask == mask) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void DeferStencilFunc(uint32_t func, int32_t ref, uint32_t mask) noexcept {
        stencilFuncValid = true;
        stencilFunc      = func;
        stencilRef       = ref;
        stencilFuncMask  = mask;
        stencilFuncDirty = (!drvStencilFuncValid || drvStencilFunc != func ||
                            drvStencilRef != ref || drvStencilFuncMask != mask);
    }
    void RecordStencilFunc(uint32_t func, int32_t ref, uint32_t mask) noexcept {
        stencilFuncValid    = true;
        stencilFunc         = func;
        stencilRef          = ref;
        stencilFuncMask     = mask;
        drvStencilFuncValid = true;
        drvStencilFunc      = func;
        drvStencilRef       = ref;
        drvStencilFuncMask  = mask;
        stencilFuncDirty    = false;
    }
    bool TakeDeferredStencilFunc(uint32_t& outFunc, int32_t& outRef, uint32_t& outMask) noexcept {
        if (!stencilFuncDirty || !stencilFuncValid) return false;
        outFunc             = stencilFunc;
        outRef              = stencilRef;
        outMask             = stencilFuncMask;
        drvStencilFuncValid = true;
        drvStencilFunc      = stencilFunc;
        drvStencilRef       = stencilRef;
        drvStencilFuncMask  = stencilFuncMask;
        stencilFuncDirty    = false;
        return true;
    }
    bool ShouldCallStencilFunc(uint32_t func, int32_t ref, uint32_t mask) {
        if (IsStencilFuncRedundant(func, ref, mask)) return false;
        RecordStencilFunc(func, ref, mask);
        return true;
    }

    bool IsStencilMaskRedundant(uint32_t mask) {
        ++batchCalls;
        if (stencilWriteMaskValid && stencilWriteMask == mask) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void DeferStencilMask(uint32_t mask) noexcept {
        stencilWriteMaskValid = true;
        stencilWriteMask      = mask;
        stencilWriteMaskDirty = (!drvStencilWriteMaskValid || drvStencilWriteMask != mask);
    }
    void RecordStencilMask(uint32_t mask) noexcept {
        stencilWriteMaskValid    = true;
        stencilWriteMask         = mask;
        drvStencilWriteMaskValid = true;
        drvStencilWriteMask      = mask;
        stencilWriteMaskDirty    = false;
    }
    bool TakeDeferredStencilMask(uint32_t& outMask) noexcept {
        if (!stencilWriteMaskDirty || !stencilWriteMaskValid) return false;
        outMask                  = stencilWriteMask;
        drvStencilWriteMaskValid = true;
        drvStencilWriteMask      = stencilWriteMask;
        stencilWriteMaskDirty    = false;
        return true;
    }
    bool ShouldCallStencilMask(uint32_t mask) {
        if (IsStencilMaskRedundant(mask)) return false;
        RecordStencilMask(mask);
        return true;
    }

    bool IsStencilOpRedundant(uint32_t fail, uint32_t zfail, uint32_t zpass) {
        ++batchCalls;
        if (stencilOpValid && stencilOpFail == fail && stencilOpZfail == zfail && stencilOpZpass == zpass) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void DeferStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass) noexcept {
        stencilOpValid = true;
        stencilOpFail  = fail;
        stencilOpZfail = zfail;
        stencilOpZpass = zpass;
        stencilOpDirty = (!drvStencilOpValid || drvStencilOpFail != fail ||
                          drvStencilOpZfail != zfail || drvStencilOpZpass != zpass);
    }
    void RecordStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass) noexcept {
        stencilOpValid    = true;
        stencilOpFail     = fail;
        stencilOpZfail    = zfail;
        stencilOpZpass    = zpass;
        drvStencilOpValid = true;
        drvStencilOpFail  = fail;
        drvStencilOpZfail = zfail;
        drvStencilOpZpass = zpass;
        stencilOpDirty    = false;
    }
    bool TakeDeferredStencilOp(uint32_t& outFail, uint32_t& outZfail, uint32_t& outZpass) noexcept {
        if (!stencilOpDirty || !stencilOpValid) return false;
        outFail           = stencilOpFail;
        outZfail          = stencilOpZfail;
        outZpass          = stencilOpZpass;
        drvStencilOpValid = true;
        drvStencilOpFail  = stencilOpFail;
        drvStencilOpZfail = stencilOpZfail;
        drvStencilOpZpass = stencilOpZpass;
        stencilOpDirty    = false;
        return true;
    }
    bool ShouldCallStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass) {
        if (IsStencilOpRedundant(fail, zfail, zpass)) return false;
        RecordStencilOp(fail, zfail, zpass);
        return true;
    }

    bool IsPolygonOffsetRedundant(float factor, float units) {
        ++batchCalls;
        if (!std::isfinite(factor) || !std::isfinite(units)) {
            return false;
        }
        if (polyOffValid && polyOffFactor == factor && polyOffUnits == units) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void RecordPolygonOffset(float factor, float units) noexcept {
        if (!std::isfinite(factor) || !std::isfinite(units)) {
            polyOffValid = false;
            return;
        }
        polyOffValid  = true;
        polyOffFactor = factor;
        polyOffUnits  = units;
    }
    bool ShouldCallPolygonOffset(float factor, float units) {
        if (IsPolygonOffsetRedundant(factor, units)) return false;
        RecordPolygonOffset(factor, units);
        return true;
    }

    bool IsViewportRedundant(int32_t x, int32_t y, int32_t w, int32_t h) {
        ++batchCalls;
        if (w < 0 || h < 0) return false;
        if (vpValid && vp[0] == x && vp[1] == y && vp[2] == w && vp[3] == h) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void RecordViewport(int32_t x, int32_t y, int32_t w, int32_t h) noexcept {
        if (w < 0 || h < 0) {
            vpValid = false;
            return;
        }
        vpValid = true;
        vp[0] = x; vp[1] = y; vp[2] = w; vp[3] = h;
    }
    bool ShouldCallViewport(int32_t x, int32_t y, int32_t w, int32_t h) {
        if (IsViewportRedundant(x, y, w, h)) return false;
        RecordViewport(x, y, w, h);
        return true;
    }

    bool IsScissorRedundant(int32_t x, int32_t y, int32_t w, int32_t h) {
        ++batchCalls;
        if (w < 0 || h < 0) return false;
        if (scValid && sc[0] == x && sc[1] == y && sc[2] == w && sc[3] == h) {
            ++batchHits;
            return true;
        }
        return false;
    }
    void RecordScissor(int32_t x, int32_t y, int32_t w, int32_t h) noexcept {
        if (w < 0 || h < 0) {
            scValid = false;
            return;
        }
        scValid = true;
        sc[0] = x; sc[1] = y; sc[2] = w; sc[3] = h;
    }
    bool ShouldCallScissor(int32_t x, int32_t y, int32_t w, int32_t h) {
        if (IsScissorRedundant(x, y, w, h)) return false;
        RecordScissor(x, y, w, h);
        return true;
    }

    bool IsEnableDisableRedundant(uint32_t cap, bool enable) {
        ++batchCalls;
        const int idx = CapToIndex(cap);
        if (idx >= 0) {
            const int8_t v = enable ? 1 : 0;
            if (caps[idx] == v) {
                ++batchHits;
                return true;
            }
        }
        return false;
    }
    void RecordEnableDisable(uint32_t cap, bool enable) noexcept {
        const int idx = CapToIndex(cap);
        if (idx >= 0) {
            caps[idx] = enable ? 1 : 0;
        }
    }
    bool ShouldCallEnableDisable(uint32_t cap, bool enable) {
        if (IsEnableDisableRedundant(cap, enable)) return false;
        RecordEnableDisable(cap, enable);
        return true;
    }

    bool ShouldCallUniform(int32_t loc, int32_t count, uint8_t transpose,
                           uint8_t tag, const void* data, size_t byteLen) {
        ++batchUniCalls;
        if (curProgram == 0u || curProgram == kInvalidId ||
            loc < 0 || loc >= static_cast<int32_t>(kMaxUniLocs)) {
            return true;
        }
        if (count != 1) {
            if (count > 1) {
                if (UniformProgBucket* b = FindUniBucket(curProgram)) {
                    const int32_t maxLoc = static_cast<int32_t>(kMaxUniLocs);
                    const int32_t end = (count > maxLoc - loc) ? maxLoc : (loc + count);
                    for (int32_t i = loc; i < end; ++i) {
                        b->slots[i].tag = UNI_NONE;
                    }
                }
            }
            return true;
        }
        if (transpose != 0 || !data || byteLen == 0 || byteLen > 64) {
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
