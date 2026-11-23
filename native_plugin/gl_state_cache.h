#pragma once

#include <cstdint>

namespace hs_gl_cache {

static constexpr uint32_t kMaxTexUnits    = 16;
static constexpr uint32_t kInvalidId      = 0xFFFFFFFFu;
static constexpr uint32_t kGlTexture0     = 0x84C0u;
static constexpr uint32_t kGlTexture2D    = 0x0DE1u;

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
    void*    curContext  = nullptr;
    int32_t  activeUnit  = -1;
    uint32_t curProgram  = kInvalidId;
    uint32_t blendSrc    = kInvalidId;
    uint32_t blendDst    = kInvalidId;
    int8_t   depthMask   = -1;
    int8_t   caps[kNumCaps] = { -1, -1, -1, -1, -1, -1 };
    uint32_t boundTex2D[kMaxTexUnits] = {
        kInvalidId, kInvalidId, kInvalidId, kInvalidId,
        kInvalidId, kInvalidId, kInvalidId, kInvalidId,
        kInvalidId, kInvalidId, kInvalidId, kInvalidId,
        kInvalidId, kInvalidId, kInvalidId, kInvalidId
    };

    uint32_t batchCalls = 0;
    uint32_t batchHits  = 0;

    void Reset() {
        activeUnit = -1;
        curProgram = kInvalidId;
        blendSrc   = kInvalidId;
        blendDst   = kInvalidId;
        depthMask  = -1;
        for (size_t i = 0; i < kNumCaps; ++i) caps[i] = -1;
        for (uint32_t i = 0; i < kMaxTexUnits; ++i) boundTex2D[i] = kInvalidId;
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
};

} // namespace hs_gl_cache
