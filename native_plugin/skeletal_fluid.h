#pragma once
// =============================================================================
// HyperSmooth v2.9.5-A — Fluid Skeletal Animation Engine (A1 + A2)
//   A1: Cross-Clip Pose Crossfade (Bone-Level Ease-Out Cubic Polar Blending)
//       Eliminates 0ms hard cuts on idle<->run, run<->atk, chop/mine start/stop
//       while auto-guarding against Left<->Right mirror flips, teleports, UI,
//       particle swarms, and radical form transformations.
//   A2: 4-Keyframe C1 Monotone PCHIP Spline Bone Interpolation
//       Eliminates 30Hz linear velocity corners on limb swings & weapon arcs
//       with mathematically guaranteed zero-overshoot monotonicity!
// =============================================================================
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace hs_skeletal {

struct AnimElement {
    uint32_t layer_hash;   // +0x00
    uint8_t  meta0[12];    // +0x04..+0x0F
    uint32_t symbol_hash;  // +0x10
    uint8_t  meta1[12];    // +0x14..+0x1F
    uint32_t frame_num;    // +0x20
    uint16_t m[6];         // +0x24..+0x2F: 6 IEEE-754 float16 [a, b, c, d, tx, ty]
};
static_assert(sizeof(AnimElement) == 48, "AnimElement must be 48 bytes");

struct AnimFrame {
    float        bbox[6];      // +0x00..+0x17
    AnimElement* elements;     // +0x18
    uint32_t     num_elements; // +0x20
    uint32_t     pad;          // +0x24
};
static_assert(sizeof(AnimFrame) == 40, "AnimFrame must be 40 bytes");

struct Animation {
    uint8_t    pad0[0x08];
    AnimFrame* frames;     // +0x08
    float      framerate;  // +0x10
    uint8_t    pad1[0x24];
    uint32_t   num_frames; // +0x38
    uint32_t   pad2;
    float      duration;   // +0x40
};

// Portable branchless IEEE-754 float16 <-> float32 (exact match with Klei @ 0x1400F3A8D)
inline float HalfToFloat(uint16_t h) {
    uint32_t exp_mant = (h & 0x7FFFu);
    uint32_t bits = (h & 0x7C00u) ? ((exp_mant + 0x1C000u) << 13) : 0u;
    bits |= (static_cast<uint32_t>(h & 0x8000u) << 16);
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

inline uint16_t FloatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, sizeof(x));
    uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 112;
    uint32_t mant = x & 0x007FFFFFu;
    if (exp <= 0) return static_cast<uint16_t>(sign);
    if (exp >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
    uint32_t rounded = (mant + 0x1000u) >> 13;
    if (rounded >= 0x400u) {
        rounded = 0;
        ++exp;
        if (exp >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | rounded);
}

inline float WrapAngleDiff(float d) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    if (d > kPi) d -= kTwoPi;
    else if (d < -kPi) d += kTwoPi;
    return d;
}

inline float ElemDet(const uint16_t m[6]) {
    return HalfToFloat(m[0]) * HalfToFloat(m[3]) - HalfToFloat(m[1]) * HalfToFloat(m[2]);
}

// -----------------------------------------------------------------------------
// A2 Core: Fritsch-Carlson Monotone Cubic Hermite (PCHIP) Scalar & Angle Spline
//   - Guaranteed C1 continuous across keyframes
//   - Mathematically ZERO overshoot past [min(v0, v1), max(v0, v1)]
//   - Smooth zero-slope turnaround at local peaks/reversals (walk swing apex!)
//   - Reduces bit-exact to linear interpolation when splineStr == 0.0
// -----------------------------------------------------------------------------
inline float PchipFromDeltas(float v0, float d0, float d1, float d2, float t, float splineStr) {
    if (fabsf(d1) < 1e-7f) return v0;
    const float s = (splineStr < 0.0f) ? 0.0f : ((splineStr > 1.0f) ? 1.0f : splineStr);
    if (s <= 1e-4f) return v0 + d1 * t;

    const float m0p = (d0 * d1 > 0.0f) ? ((2.0f * d0 * d1) / (d0 + d1)) : 0.0f;
    const float m1p = (d1 * d2 > 0.0f) ? ((2.0f * d1 * d2) / (d1 + d2)) : 0.0f;
    const float m0 = d1 + (m0p - d1) * s;
    const float m1 = d1 + (m1p - d1) * s;

    const float t2 = t * t;
    const float t3 = t2 * t;
    return v0 + m0 * t + (3.0f * d1 - 2.0f * m0 - m1) * t2 + (m0 + m1 - 2.0f * d1) * t3;
}

inline float PchipScalar(float vM1, float v0, float v1, float v2,
                         bool hasM1, bool hasP2, float t, float splineStr) {
    const float d1 = v1 - v0;
    const float d0 = hasM1 ? (v0 - vM1) : d1;
    const float d2 = hasP2 ? (v2 - v1) : d1;
    return PchipFromDeltas(v0, d0, d1, d2, t, splineStr);
}

inline float PchipAngle(float aM1, float a0, float a1, float a2,
                        bool hasM1, bool hasP2, float t, float splineStr, float maxAng = 2.35f) {
    const float d1 = WrapAngleDiff(a1 - a0);
    float d0 = hasM1 ? WrapAngleDiff(a0 - aM1) : d1;
    float d2 = hasP2 ? WrapAngleDiff(a2 - a1) : d1;
    if (fabsf(d0) > maxAng) d0 = d1;
    if (fabsf(d2) > maxAng) d2 = d1;
    return PchipFromDeltas(a0, d0, d1, d2, t, splineStr);
}

// -----------------------------------------------------------------------------
// 3-Tier Ultra-Fast Float16 Matrix Interpolator (2-Key Linear / Polar)
// -----------------------------------------------------------------------------
inline bool TweenElementMatrix(const uint16_t m0h[6], const uint16_t m1h[6], float t,
                               uint16_t outh[6], bool fastOnly = false,
                               float maxDist = 420.0f, float maxAng = 2.35f) {
    // Tier 0: exact identical matrix (copy to outh in case caller didn't pre-copy)
    if (std::memcmp(m0h, m1h, 12) == 0) {
        if (outh != m0h) std::memcpy(outh, m0h, 12);
        return true;
    }

    const float tx0 = HalfToFloat(m0h[4]), ty0 = HalfToFloat(m0h[5]);
    const float tx1 = HalfToFloat(m1h[4]), ty1 = HalfToFloat(m1h[5]);
    const float dtx = tx1 - tx0;
    const float dty = ty1 - ty0;
    if (fabsf(dtx) > maxDist || fabsf(dty) > maxDist) return false;

    // Tier 1: pure translation (2x2 linear part bit-identical)
    if (std::memcmp(m0h, m1h, 8) == 0) {
        if (outh != m0h) std::memcpy(outh, m0h, 8);
        outh[4] = FloatToHalf(tx0 + dtx * t);
        outh[5] = FloatToHalf(ty0 + dty * t);
        return true;
    }

    const float a0 = HalfToFloat(m0h[0]), b0 = HalfToFloat(m0h[1]);
    const float c0 = HalfToFloat(m0h[2]), d0 = HalfToFloat(m0h[3]);
    const float a1 = HalfToFloat(m1h[0]), b1 = HalfToFloat(m1h[1]);
    const float c1 = HalfToFloat(m1h[2]), d1 = HalfToFloat(m1h[3]);

    const float det0 = a0 * d0 - b0 * c0;
    const float det1 = a1 * d1 - b1 * c1;
    if (det0 * det1 < 0.0f) return false;

    const float da = a1 - a0, db = b1 - b0, dc = c1 - c0, dd = d1 - d0;

    // Tier 2: small-angle 2x2 delta (<= ~3.5 deg) or swarm fastOnly mode
    const float linThresh = fastOnly ? 0.35f : 0.06f;
    if (fabsf(da) < linThresh && fabsf(db) < linThresh && fabsf(dc) < linThresh && fabsf(dd) < linThresh) {
        outh[0] = FloatToHalf(a0 + da * t);
        outh[1] = FloatToHalf(b0 + db * t);
        outh[2] = FloatToHalf(c0 + dc * t);
        outh[3] = FloatToHalf(d0 + dd * t);
        outh[4] = FloatToHalf(tx0 + dtx * t);
        outh[5] = FloatToHalf(ty0 + dty * t);
        return true;
    }
    if (fastOnly) {
        return false;
    }

    // Tier 3: large weapon/boss rotation -> exact polar decomposition
    const float su0 = sqrtf(a0 * a0 + c0 * c0);
    const float su1 = sqrtf(a1 * a1 + c1 * c1);
    const float sv0 = sqrtf(b0 * b0 + d0 * d0);
    const float sv1 = sqrtf(b1 * b1 + d1 * d1);

    float a, b, c, d;
    if (su0 > 1e-4f && su1 > 1e-4f && sv0 > 1e-4f && sv1 > 1e-4f) {
        const float au0 = atan2f(c0, a0), au1 = atan2f(c1, a1);
        const float du = WrapAngleDiff(au1 - au0);
        const float av0 = atan2f(d0, b0), av1 = atan2f(d1, b1);
        const float dv = WrapAngleDiff(av1 - av0);
        if (fabsf(du) > maxAng || fabsf(dv) > maxAng) return false;

        const float su = su0 + (su1 - su0) * t, au = au0 + du * t;
        const float sv = sv0 + (sv1 - sv0) * t, av = av0 + dv * t;
        a = su * cosf(au); c = su * sinf(au);
        b = sv * cosf(av); d = sv * sinf(av);
    } else {
        a = a0 + da * t; b = b0 + db * t;
        c = c0 + dc * t; d = d0 + dd * t;
    }

    outh[0] = FloatToHalf(a);
    outh[1] = FloatToHalf(b);
    outh[2] = FloatToHalf(c);
    outh[3] = FloatToHalf(d);
    outh[4] = FloatToHalf(tx0 + dtx * t);
    outh[5] = FloatToHalf(ty0 + dty * t);
    return true;
}

// -----------------------------------------------------------------------------
// A2: 4-Keyframe C1 Monotone PCHIP Matrix Interpolator (mM1, m0, m1, mP2)
//   - Falls back cleanly to 2-key TweenElementMatrix if splineStr <= 0 or fastOnly
//   - Validates mM1 / mP2 independently (det sign & jump cap) so missing or
//     teleporting neighbor keys degrade smoothly to one-sided tangents.
// -----------------------------------------------------------------------------
inline bool TweenElementMatrixSpline(const uint16_t* mM1h,
                                     const uint16_t m0h[6],
                                     const uint16_t m1h[6],
                                     const uint16_t* mP2h,
                                     float t,
                                     float splineStr,
                                     uint16_t outh[6],
                                     bool fastOnly = false,
                                     bool* outUsedSpline = nullptr) {
    if (outUsedSpline) *outUsedSpline = false;
    if (splineStr <= 0.001f || fastOnly || (!mM1h && !mP2h)) {
        return TweenElementMatrix(m0h, m1h, t, outh, fastOnly);
    }
    if (std::memcmp(m0h, m1h, 12) == 0) {
        if (outh != m0h) std::memcpy(outh, m0h, 12);
        return true;
    }

    constexpr float kMaxDist = 420.0f;
    constexpr float kMaxAng  = 2.35f;

    const float tx0 = HalfToFloat(m0h[4]), ty0 = HalfToFloat(m0h[5]);
    const float tx1 = HalfToFloat(m1h[4]), ty1 = HalfToFloat(m1h[5]);
    if (fabsf(tx1 - tx0) > kMaxDist || fabsf(ty1 - ty0) > kMaxDist) return false;

    const float a0 = HalfToFloat(m0h[0]), b0 = HalfToFloat(m0h[1]);
    const float c0 = HalfToFloat(m0h[2]), d0 = HalfToFloat(m0h[3]);
    const float a1 = HalfToFloat(m1h[0]), b1 = HalfToFloat(m1h[1]);
    const float c1 = HalfToFloat(m1h[2]), d1 = HalfToFloat(m1h[3]);
    const float det0 = a0 * d0 - b0 * c0;
    const float det1 = a1 * d1 - b1 * c1;
    if (det0 * det1 < 0.0f) return false;

    bool hasM1 = false;
    float aM1 = a0, bM1 = b0, cM1 = c0, dM1 = d0, txM1 = tx0, tyM1 = ty0;
    if (mM1h) {
        txM1 = HalfToFloat(mM1h[4]); tyM1 = HalfToFloat(mM1h[5]);
        aM1 = HalfToFloat(mM1h[0]);  bM1 = HalfToFloat(mM1h[1]);
        cM1 = HalfToFloat(mM1h[2]);  dM1 = HalfToFloat(mM1h[3]);
        const float detM1 = aM1 * dM1 - bM1 * cM1;
        if (detM1 * det0 >= 0.0f && fabsf(tx0 - txM1) <= kMaxDist && fabsf(ty0 - tyM1) <= kMaxDist) {
            hasM1 = true;
        }
    }

    bool hasP2 = false;
    float aP2 = a1, bP2 = b1, cP2 = c1, dP2 = d1, txP2 = tx1, tyP2 = ty1;
    if (mP2h) {
        txP2 = HalfToFloat(mP2h[4]); tyP2 = HalfToFloat(mP2h[5]);
        aP2 = HalfToFloat(mP2h[0]);  bP2 = HalfToFloat(mP2h[1]);
        cP2 = HalfToFloat(mP2h[2]);  dP2 = HalfToFloat(mP2h[3]);
        const float detP2 = aP2 * dP2 - bP2 * cP2;
        if (detP2 * det1 >= 0.0f && fabsf(txP2 - tx1) <= kMaxDist && fabsf(tyP2 - ty1) <= kMaxDist) {
            hasP2 = true;
        }
    }

    if (!hasM1 && !hasP2) {
        return TweenElementMatrix(m0h, m1h, t, outh, fastOnly);
    }

    const float tx = PchipScalar(txM1, tx0, tx1, txP2, hasM1, hasP2, t, splineStr);
    const float ty = PchipScalar(tyM1, ty0, ty1, tyP2, hasM1, hasP2, t, splineStr);

    // Tier 1: Pure translation across f0..f1 -> zero trig!
    if (std::memcmp(m0h, m1h, 8) == 0) {
        if (outh != m0h) std::memcpy(outh, m0h, 8);
        outh[4] = FloatToHalf(tx);
        outh[5] = FloatToHalf(ty);
        if (outUsedSpline) *outUsedSpline = true;
        return true;
    }

    const float da = a1 - a0, db = b1 - b0, dc = c1 - c0, dd = d1 - d0;
    const bool smallMain = (fabsf(da) < 0.06f && fabsf(db) < 0.06f && fabsf(dc) < 0.06f && fabsf(dd) < 0.06f);
    const bool smallM1 = !hasM1 || (fabsf(a0 - aM1) < 0.12f && fabsf(b0 - bM1) < 0.12f &&
                                    fabsf(c0 - cM1) < 0.12f && fabsf(d0 - dM1) < 0.12f);
    const bool smallP2 = !hasP2 || (fabsf(aP2 - a1) < 0.12f && fabsf(bP2 - b1) < 0.12f &&
                                    fabsf(cP2 - c1) < 0.12f && fabsf(dP2 - d1) < 0.12f);

    // Tier 2: Small-angle 2x2 PCHIP directly on matrix entries (zero trig!)
    if (smallMain && smallM1 && smallP2) {
        outh[0] = FloatToHalf(PchipScalar(aM1, a0, a1, aP2, hasM1, hasP2, t, splineStr));
        outh[1] = FloatToHalf(PchipScalar(bM1, b0, b1, bP2, hasM1, hasP2, t, splineStr));
        outh[2] = FloatToHalf(PchipScalar(cM1, c0, c1, cP2, hasM1, hasP2, t, splineStr));
        outh[3] = FloatToHalf(PchipScalar(dM1, d0, d1, dP2, hasM1, hasP2, t, splineStr));
        outh[4] = FloatToHalf(tx);
        outh[5] = FloatToHalf(ty);
        if (outUsedSpline) *outUsedSpline = true;
        return true;
    }

    // Tier 3: Large swing / boss slam -> Polar-Decomposed C1 Monotone PCHIP!
    const float su0 = sqrtf(a0 * a0 + c0 * c0);
    const float su1 = sqrtf(a1 * a1 + c1 * c1);
    const float sv0 = sqrtf(b0 * b0 + d0 * d0);
    const float sv1 = sqrtf(b1 * b1 + d1 * d1);

    float a, b, c, d;
    if (su0 > 1e-4f && su1 > 1e-4f && sv0 > 1e-4f && sv1 > 1e-4f) {
        const float au0 = atan2f(c0, a0), au1 = atan2f(c1, a1);
        const float du = WrapAngleDiff(au1 - au0);
        const float av0 = atan2f(d0, b0), av1 = atan2f(d1, b1);
        const float dv = WrapAngleDiff(av1 - av0);
        if (fabsf(du) > kMaxAng || fabsf(dv) > kMaxAng) return false;

        float suM1 = su0, svM1 = sv0, auM1 = au0, avM1 = av0;
        bool pHasM1 = false;
        if (hasM1) {
            suM1 = sqrtf(aM1 * aM1 + cM1 * cM1);
            svM1 = sqrtf(bM1 * bM1 + dM1 * dM1);
            if (suM1 > 1e-4f && svM1 > 1e-4f) {
                auM1 = atan2f(cM1, aM1);
                avM1 = atan2f(dM1, bM1);
                pHasM1 = true;
            }
        }
        float suP2 = su1, svP2 = sv1, auP2 = au1, avP2 = av1;
        bool pHasP2 = false;
        if (hasP2) {
            suP2 = sqrtf(aP2 * aP2 + cP2 * cP2);
            svP2 = sqrtf(bP2 * bP2 + dP2 * dP2);
            if (suP2 > 1e-4f && svP2 > 1e-4f) {
                auP2 = atan2f(cP2, aP2);
                avP2 = atan2f(dP2, bP2);
                pHasP2 = true;
            }
        }

        const float su = PchipScalar(suM1, su0, su1, suP2, pHasM1, pHasP2, t, splineStr);
        const float sv = PchipScalar(svM1, sv0, sv1, svP2, pHasM1, pHasP2, t, splineStr);
        const float au = PchipAngle(auM1, au0, au1, auP2, pHasM1, pHasP2, t, splineStr, kMaxAng);
        const float av = PchipAngle(avM1, av0, av1, avP2, pHasM1, pHasP2, t, splineStr, kMaxAng);
        a = su * cosf(au); c = su * sinf(au);
        b = sv * cosf(av); d = sv * sinf(av);
    } else {
        a = PchipScalar(aM1, a0, a1, aP2, hasM1, hasP2, t, splineStr);
        b = PchipScalar(bM1, b0, b1, bP2, hasM1, hasP2, t, splineStr);
        c = PchipScalar(cM1, c0, c1, cP2, hasM1, hasP2, t, splineStr);
        d = PchipScalar(dM1, d0, d1, dP2, hasM1, hasP2, t, splineStr);
    }

    outh[0] = FloatToHalf(a);
    outh[1] = FloatToHalf(b);
    outh[2] = FloatToHalf(c);
    outh[3] = FloatToHalf(d);
    outh[4] = FloatToHalf(tx);
    outh[5] = FloatToHalf(ty);
    if (outUsedSpline) *outUsedSpline = true;
    return true;
}

// =============================================================================
// A1: Cross-Clip Pose Crossfade Pool (Compact L2-Resident 128-Slot LRU Pool)
//   - Only tracks Main-channel animated rigs (1 <= num_elements <= 96).
//   - Always renders on top of the NEW clip's AnimFrame (preserving 100% of
//     new frame_num sprites, layer ordering, and attack/FX spawns from t=0),
//     blending only matched rigid limb matrices from prevPose -> targetPose
//     via Cubic Ease-Out (fast responsive snap + silky deceleration!).
//   - Automatically cancels on:
//       * Left <-> Right 180-deg facing mirror flip (WorldMtx Basis-X dot < -0.25)
//       * Off-screen / respawn / teleport gap (> 100ms unseen)
//       * Low structural bone overlap (< 30% limbs matching, e.g. Werebeaver)
//       * Particle / duplicate-symbol layers
//       * Swarm load shedding (fastOnly)
// =============================================================================
static constexpr uint32_t kPosePoolSlots = 512u;
static constexpr uint32_t kMaxPoseBones  = 96u;

struct BoneSnap {
    uint32_t layer_hash;
    uint32_t symbol_hash;
    uint16_t m[6];
};

struct PoseCrossfadeSlot {
    uintptr_t entity_key = 0;
    int64_t   seen_qpc = 0;
    int64_t   blend_start_qpc = 0;
    float     blend_dur_sec = 0.0f;
    float     last_axis_x[3] = { 1.0f, 0.0f, 0.0f };
    uint16_t  last_count = 0;
    uint16_t  blend_count = 0;
    uint8_t   valid = 0;
    uint8_t   blending = 0;
    uint8_t   has_axis_x = 0;
    uint8_t   pad = 0;
    BoneSnap  last_pose[kMaxPoseBones];
    BoneSnap  blend_from[kMaxPoseBones];
};

class PoseCrossfadePool {
public:
    PoseCrossfadePool() {
        for (uint32_t i = 0; i < kPosePoolSlots; ++i) {
            slots_[i].valid = 0;
            slots_[i].blending = 0;
            slots_[i].entity_key = 0;
        }
    }

    PoseCrossfadeSlot* Find(uintptr_t key) {
        if (key == 0) return nullptr;
        const uint32_t base = HashKey(key);
        for (uint32_t p = 0; p < 4u; ++p) {
            PoseCrossfadeSlot& s = slots_[(base + p) & (kPosePoolSlots - 1u)];
            if (s.valid && s.entity_key == key) return &s;
        }
        return nullptr;
    }

    PoseCrossfadeSlot* FindOrAlloc(uintptr_t key, int64_t nowQpc) {
        if (key == 0) return nullptr;
        const uint32_t base = HashKey(key);
        PoseCrossfadeSlot* emptySlot = nullptr;
        PoseCrossfadeSlot* lruSlot = &slots_[base];
        for (uint32_t p = 0; p < 4u; ++p) {
            PoseCrossfadeSlot& s = slots_[(base + p) & (kPosePoolSlots - 1u)];
            if (s.valid && s.entity_key == key) return &s;
            if (!s.valid && !emptySlot) emptySlot = &s;
            if (s.seen_qpc < lruSlot->seen_qpc) lruSlot = &s;
        }
        PoseCrossfadeSlot* dst = emptySlot ? emptySlot : lruSlot;
        dst->entity_key = key;
        dst->seen_qpc = nowQpc;
        dst->blend_start_qpc = 0;
        dst->blend_dur_sec = 0.0f;
        dst->last_count = 0;
        dst->blend_count = 0;
        dst->valid = 1;
        dst->blending = 0;
        dst->has_axis_x = 0;
        return dst;
    }

    // Detects camera-rotation-invariant 180-deg Left<->Right entity facing flip
    static bool IsFacingFlipped(const PoseCrossfadeSlot& slot, const float* curAxisX) {
        if (!curAxisX || !slot.has_axis_x) return false;
        const float len2A = slot.last_axis_x[0] * slot.last_axis_x[0] +
                            slot.last_axis_x[1] * slot.last_axis_x[1] +
                            slot.last_axis_x[2] * slot.last_axis_x[2];
        const float len2B = curAxisX[0] * curAxisX[0] +
                            curAxisX[1] * curAxisX[1] +
                            curAxisX[2] * curAxisX[2];
        if (len2A < 1e-6f || len2B < 1e-6f) return false;
        const float dot = slot.last_axis_x[0] * curAxisX[0] +
                          slot.last_axis_x[1] * curAxisX[1] +
                          slot.last_axis_x[2] * curAxisX[2];
        // cos(theta) < -0.25 => >104 deg horizontal flip (normal camera spin is <5 deg/frame)
        return dot < -0.25f * sqrtf(len2A * len2B);
    }

    // Attempts to initiate a crossfade from slot.last_pose -> targetFrame on animPtr switch.
    // Returns true if crossfade started cleanly; false if rejected by safety guards.
    static bool TryStartCrossfade(PoseCrossfadeSlot& slot,
                                  const AnimFrame& targetFrame,
                                  int64_t nowQpc,
                                  float durationSec,
                                  double animUnseenSec,
                                  const float* curAxisX) {
        slot.blending = 0;
        if (durationSec < 0.005f || animUnseenSec > 0.10) return false;
        if (IsFacingFlipped(slot, curAxisX)) return false;
        if (slot.last_count == 0 || slot.last_count > kMaxPoseBones) return false;
        if (!targetFrame.elements || targetFrame.num_elements == 0 || targetFrame.num_elements > kMaxPoseBones) {
            return false;
        }

        // Pose similarity pre-check: ensure the new clip shares genuine skeletal limbs
        // with the old clip (aborts cleanly on total form transformations or unrelated builds).
        const uint32_t nT = targetFrame.num_elements;
        uint32_t matched = 0;
        for (uint32_t i = 0; i < nT; ++i) {
            const AnimElement& et = targetFrame.elements[i];
            if (FindUniqueBoneMatch(slot.last_pose, slot.last_count, et) >= 0) {
                ++matched;
            }
        }
        const bool smallRig = (nT <= 4u);
        const bool okOverlap = smallRig
            ? (matched >= 1u && matched * 100u >= nT * 50u)
            : (matched >= 3u && matched * 100u >= nT * 30u);
        if (!okOverlap) return false;

        std::memcpy(slot.blend_from, slot.last_pose, slot.last_count * sizeof(BoneSnap));
        slot.blend_count = slot.last_count;
        slot.blend_start_qpc = nowQpc;
        slot.blend_dur_sec = durationSec;
        slot.blending = 1;
        return true;
    }

    // Applies active crossfade in-place onto writable targetElems[0..nT-1].
    // Returns number of blended bones (0 if crossfade finished/cancelled).
    static uint32_t ApplyCrossfade(PoseCrossfadeSlot& slot,
                                   AnimElement* targetElems,
                                   uint32_t nT,
                                   int64_t nowQpc,
                                   double qpcInvFreq,
                                   const float* curAxisX,
                                   bool fastOnly) {
        if (!slot.blending) return 0;
        if (fastOnly || !targetElems || nT == 0 || nT > kMaxPoseBones ||
            slot.blend_count == 0 || IsFacingFlipped(slot, curAxisX)) {
            slot.blending = 0;
            return 0;
        }
        const float elapsed = static_cast<float>(static_cast<double>(nowQpc - slot.blend_start_qpc) * qpcInvFreq);
        if (!std::isfinite(elapsed) || elapsed < 0.0f || slot.blend_dur_sec <= 1e-4f || elapsed >= slot.blend_dur_sec) {
            slot.blending = 0;
            return 0;
        }
        const float u = elapsed / slot.blend_dur_sec;
        // Ease-Out Cubic: snappy responsive start (w=0.45 at 18% time), silky zero-jerk settle!
        const float inv = 1.0f - u;
        const float w = 1.0f - inv * inv * inv;
        if (w >= 0.992f) {
            slot.blending = 0;
            return 0;
        }

        uint32_t blended = 0;
        for (uint32_t i = 0; i < nT; ++i) {
            AnimElement& et = targetElems[i];
            // Skip duplicate (layer, symbol) particle swarms inside targetFrame
            if (CountLayerSym(targetElems, nT, et.layer_hash, et.symbol_hash) != 1u) continue;
            const int32_t bi = FindUniqueBoneMatch(slot.blend_from, slot.blend_count, et);
            if (bi < 0) continue;
            uint16_t outM[6];
            if (TweenElementMatrix(slot.blend_from[bi].m, et.m, w, outM, false, 180.0f, 1.85f)) {
                std::memcpy(et.m, outM, sizeof(outM));
                ++blended;
            }
        }
        return blended;
    }

    // Records final rendered pose for future clip-switch crossfade continuity
    static void RecordPose(PoseCrossfadeSlot& slot,
                           const AnimFrame& renderedFrame,
                           int64_t nowQpc,
                           const float* curAxisX) {
        slot.seen_qpc = nowQpc;
        if (curAxisX) {
            slot.last_axis_x[0] = curAxisX[0];
            slot.last_axis_x[1] = curAxisX[1];
            slot.last_axis_x[2] = curAxisX[2];
            slot.has_axis_x = 1;
        }
        if (!renderedFrame.elements || renderedFrame.num_elements == 0 || renderedFrame.num_elements > kMaxPoseBones) {
            slot.last_count = 0;
            return;
        }
        const uint32_t n = renderedFrame.num_elements;
        for (uint32_t i = 0; i < n; ++i) {
            const AnimElement& e = renderedFrame.elements[i];
            BoneSnap& dst = slot.last_pose[i];
            dst.layer_hash = e.layer_hash;
            dst.symbol_hash = e.symbol_hash;
            std::memcpy(dst.m, e.m, sizeof(dst.m));
        }
        slot.last_count = static_cast<uint16_t>(n);
    }

private:
    PoseCrossfadeSlot slots_[kPosePoolSlots];

    static uint32_t HashKey(uintptr_t key) {
        return static_cast<uint32_t>(((key >> 4) ^ (key >> 13) ^ (key >> 23)) & (kPosePoolSlots - 1u));
    }

    static uint32_t CountLayerSym(const AnimElement* elems, uint32_t n, uint32_t lhash, uint32_t shash) {
        uint32_t c = 0;
        for (uint32_t i = 0; i < n; ++i) {
            if (elems[i].symbol_hash == shash && elems[i].layer_hash == lhash) ++c;
        }
        return c;
    }

    // Finds a unique skeletal match in prevPose for target element et:
    //   Pass 1: Exact unique (layer_hash, symbol_hash) with consistent det & <=180px jump
    //   Pass 2: Fallback unique symbol_hash within 48px (handles facing layer-order swaps!)
    static int32_t FindUniqueBoneMatch(const BoneSnap* prev, uint32_t nPrev, const AnimElement& et) {
        constexpr float kCrossMaxDist = 180.0f;
        constexpr float kSymFallbackDistSq = 48.0f * 48.0f;
        const float txT = HalfToFloat(et.m[4]), tyT = HalfToFloat(et.m[5]);
        const float detT = ElemDet(et.m);

        int32_t exactIdx = -1;
        uint32_t exactCnt = 0;
        int32_t symIdx = -1;
        uint32_t symCnt = 0;
        for (uint32_t j = 0; j < nPrev; ++j) {
            if (prev[j].symbol_hash != et.symbol_hash) continue;
            ++symCnt;
            symIdx = static_cast<int32_t>(j);
            if (prev[j].layer_hash == et.layer_hash) {
                ++exactCnt;
                exactIdx = static_cast<int32_t>(j);
            }
        }
        if (exactCnt == 1u && exactIdx >= 0) {
            const BoneSnap& b = prev[exactIdx];
            if (ElemDet(b.m) * detT >= 0.0f &&
                fabsf(txT - HalfToFloat(b.m[4])) <= kCrossMaxDist &&
                fabsf(tyT - HalfToFloat(b.m[5])) <= kCrossMaxDist) {
                return exactIdx;
            }
            return -1;
        }
        if (exactCnt == 0u && symCnt == 1u && symIdx >= 0) {
            const BoneSnap& b = prev[symIdx];
            const float dx = txT - HalfToFloat(b.m[4]);
            const float dy = tyT - HalfToFloat(b.m[5]);
            if (ElemDet(b.m) * detT >= 0.0f && (dx * dx + dy * dy) <= kSymFallbackDistSq) {
                return symIdx;
            }
        }
        return -1;
    }
};

} // namespace hs_skeletal
