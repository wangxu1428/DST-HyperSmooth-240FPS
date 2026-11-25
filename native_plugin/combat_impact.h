#pragma once
// =============================================================================
// HyperSmooth v2.9.5-B — Pure Client-Side Combat Hit-Feel & Impact Presentation
//   B1: Render-Only Visual Hit-Stop (wait-free RT anim-time freeze, 0ms input lag!)
//   B2: Directional Micro-Jitter + Underdamped Spring Squash-and-Stretch (WorldMtx)
//   B3: Per-Frame Non-Destructive Additive Hit-Flash (AnimRenderCmd ColourTransform,
//       zero Lua SetAddColour mutation, 100% compatible with freeze/burn/planar!)
//   B5: 240Hz Single-DrawCall Procedural GPU Combat VFX (Weapon Slash Crescents,
//       Directional Sparks / Wood Chips, Impact Shockwave Rings — Dual-Backend
//       Native OpenGL 4.6 #version 100 + ANGLE D3D9 GLES 2.0!)
// =============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include "world_shadow_engine.h"

namespace hs_combat {

inline constexpr uint32_t kMaxImpulses    = 32u;
inline constexpr uint32_t kMaxVfxQueue    = 32u;
inline constexpr uint32_t kMaxSlashArcs   = 16u;
inline constexpr uint32_t kMaxSparks      = 192u;
inline constexpr uint32_t kMaxShockRings  = 16u;
inline constexpr uint32_t kMaxImpactGlows = 16u;
inline constexpr uint32_t kGlDynamicDraw  = 0x88E8u;

enum class ImpactStyle : uint8_t {
    Physical = 0, // Spear, Ham Bat, Fists, Boomerang (crisp white-silver)
    Heavy    = 1, // Heavy weapon / Crit / Boss / Kill (radiant warm gold)
    Shadow   = 2, // Dark Sword, Void Scythe, Ruins Bat (arcane violet)
    Lunar    = 3, // Brightshade Sword, Glass Cutter, Ice (luminous cyan-mint)
    Fire     = 4, // Fire Staff, Torch, Ember (blazing orange)
    Chop     = 5, // Axe on Tree (warm wood chips + subtle flash)
    Mine     = 6, // Pickaxe on Boulder (bright orange-white flint sparks)
};

struct ImpactConfig {
    bool  enabled      = false;
    float hitStopScale = 1.0f;
    float recoilScale  = 1.0f;
    float flashScale   = 1.0f;
    float vfxScale     = 1.0f;
};

struct VfxBurstEvent {
    float       wx = 0.0f, wy = 0.0f, wz = 0.0f;
    float       dirX = 1.0f, dirZ = 0.0f;
    float       intensity = 1.0f;
    float       vfxScale = 1.0f;
    ImpactStyle style = ImpactStyle::Physical;
    uint8_t     swingParity = 0u;
    int64_t     startQpc = 0;
};

struct ImpulseSlot {
    std::atomic<uintptr_t> entityKey{0};
    std::atomic<int64_t>   hitStopEndQpc{0};
    float       bindX = 0.0f, bindY = 0.0f, bindZ = 0.0f;
    float       dirX = 1.0f, dirZ = 0.0f;
    int64_t     startQpc = 0;
    float       hitStopSec = 0.0f;
    float       recoilDurSec = 0.0f;
    float       flashDurSec = 0.0f;
    float       jitterAmp = 0.0f;
    float       squashAmp = 0.0f;
    float       lungeAmp = 0.0f;
    float       flashR = 0.0f, flashG = 0.0f, flashB = 0.0f;
    uint8_t     isAttacker = 0u;
    uint8_t     active = 0u;
};

inline void StyleColors(ImpactStyle style, float outFlash[3], float outCore[3], float outEdge[3]) noexcept {
    switch (style) {
    case ImpactStyle::Heavy:
        outFlash[0] = 0.96f; outFlash[1] = 0.78f; outFlash[2] = 0.28f;
        outCore[0]  = 1.00f; outCore[1]  = 0.96f; outCore[2]  = 0.78f;
        outEdge[0]  = 1.00f; outEdge[1]  = 0.68f; outEdge[2]  = 0.16f;
        break;
    case ImpactStyle::Shadow:
        outFlash[0] = 0.78f; outFlash[1] = 0.34f; outFlash[2] = 0.96f;
        outCore[0]  = 0.95f; outCore[1]  = 0.80f; outCore[2]  = 1.00f;
        outEdge[0]  = 0.62f; outEdge[1]  = 0.18f; outEdge[2]  = 0.95f;
        break;
    case ImpactStyle::Lunar:
        outFlash[0] = 0.38f; outFlash[1] = 0.92f; outFlash[2] = 0.88f;
        outCore[0]  = 0.85f; outCore[1]  = 1.00f; outCore[2]  = 0.98f;
        outEdge[0]  = 0.22f; outEdge[1]  = 0.86f; outEdge[2]  = 0.82f;
        break;
    case ImpactStyle::Fire:
        outFlash[0] = 0.98f; outFlash[1] = 0.54f; outFlash[2] = 0.16f;
        outCore[0]  = 1.00f; outCore[1]  = 0.92f; outCore[2]  = 0.62f;
        outEdge[0]  = 1.00f; outEdge[1]  = 0.36f; outEdge[2]  = 0.08f;
        break;
    case ImpactStyle::Chop:
        outFlash[0] = 0.46f; outFlash[1] = 0.42f; outFlash[2] = 0.34f;
        outCore[0]  = 0.94f; outCore[1]  = 0.76f; outCore[2]  = 0.46f;
        outEdge[0]  = 0.68f; outEdge[1]  = 0.44f; outEdge[2]  = 0.20f;
        break;
    case ImpactStyle::Mine:
        outFlash[0] = 0.62f; outFlash[1] = 0.60f; outFlash[2] = 0.54f;
        outCore[0]  = 1.00f; outCore[1]  = 0.96f; outCore[2]  = 0.76f;
        outEdge[0]  = 1.00f; outEdge[1]  = 0.58f; outEdge[2]  = 0.14f;
        break;
    case ImpactStyle::Physical:
    default:
        outFlash[0] = 0.76f; outFlash[1] = 0.76f; outFlash[2] = 0.74f;
        outCore[0]  = 1.00f; outCore[1]  = 0.99f; outCore[2]  = 0.96f;
        outEdge[0]  = 0.72f; outEdge[1]  = 0.86f; outEdge[2]  = 1.00f;
        break;
    }
}

// =============================================================================
// Main-Thread Impact Impulse Manager + Wait-Free Render-Thread Hit-Stop Reader
// =============================================================================
class CombatImpactEngine {
public:
    void SetConfig(bool enabled, float hitStopScale, float recoilScale, float flashScale, float vfxScale, float lungeScale = 0.85f) noexcept {
        enabled_.store(enabled, std::memory_order_release);
        hitStopScale_.store(std::clamp(hitStopScale, 0.0f, 1.5f), std::memory_order_relaxed);
        recoilScale_.store(std::clamp(recoilScale, 0.0f, 1.5f), std::memory_order_relaxed);
        flashScale_.store(std::clamp(flashScale, 0.0f, 1.5f), std::memory_order_relaxed);
        vfxScale_.store(std::clamp(vfxScale, 0.0f, 1.5f), std::memory_order_relaxed);
        lungeScale_.store(std::clamp(lungeScale, 0.0f, 1.5f), std::memory_order_relaxed);
        if (!enabled) {
            ClearAll();
        }
    }

    bool IsEnabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
    uint32_t ActiveImpulseCount() const noexcept { return activeCount_.load(std::memory_order_acquire); }
    uint32_t VfxQueueSeq() const noexcept { return vfxSeq_.load(std::memory_order_acquire); }

    void ClearAll() noexcept {
        activeHitStopMask_.store(0u, std::memory_order_release);
        activeCount_.store(0u, std::memory_order_release);
        for (uint32_t i = 0; i < kMaxImpulses; ++i) {
            slots_[i].active = 0u;
            slots_[i].entityKey.store(0u, std::memory_order_release);
            slots_[i].hitStopEndQpc.store(0, std::memory_order_relaxed);
        }
        AcquireSRWLockExclusive(&vfxLock_);
        vfxHead_ = 0u;
        vfxTail_ = 0u;
        ReleaseSRWLockExclusive(&vfxLock_);
    }

    // Called from Main Thread (Lua HyperSmooth_TriggerImpact)
    bool TriggerImpact(float wx, float wy, float wz,
                       float dirX, float dirZ,
                       float intensity,
                       ImpactStyle style,
                       bool isAttacker,
                       bool spawnVfx,
                       int64_t nowQpc,
                       int64_t qpcFreq) noexcept {
        if (!enabled_.load(std::memory_order_acquire)) return false;
        if (!std::isfinite(wx) || !std::isfinite(wy) || !std::isfinite(wz)) return false;

        float dx = std::isfinite(dirX) ? dirX : 1.0f;
        float dz = std::isfinite(dirZ) ? dirZ : 0.0f;
        const float dLen = sqrtf(dx * dx + dz * dz);
        if (dLen > 1e-4f) {
            dx /= dLen;
            dz /= dLen;
        } else {
            dx = 1.0f;
            dz = 0.0f;
        }
        const float inten = std::clamp(std::isfinite(intensity) ? intensity : 1.0f, 0.25f, 1.80f);
        const float hsScale = hitStopScale_.load(std::memory_order_relaxed);
        const float rcScale = recoilScale_.load(std::memory_order_relaxed);
        const float flScale = flashScale_.load(std::memory_order_relaxed);
        const float vfScale = vfxScale_.load(std::memory_order_relaxed);
        const float lgScale = lungeScale_.load(std::memory_order_relaxed);

        // Allocate free or oldest slot (also refresh existing impulse on same coords to prevent stacking)
        uint32_t pick = kMaxImpulses;
        int64_t oldestQpc = INT64_MAX;
        uint32_t oldestIdx = 0u;
        for (uint32_t i = 0; i < kMaxImpulses; ++i) {
            if (!slots_[i].active) {
                pick = i;
                break;
            }
            if (slots_[i].isAttacker == (isAttacker ? 1u : 0u)) {
                const float ex = slots_[i].bindX - wx;
                const float ez = slots_[i].bindZ - wz;
                if (ex * ex + ez * ez < 0.09f) {
                    pick = i;
                    break;
                }
            }
            if (slots_[i].startQpc < oldestQpc) {
                oldestQpc = slots_[i].startQpc;
                oldestIdx = i;
            }
        }
        if (pick >= kMaxImpulses) pick = oldestIdx;

        ImpulseSlot& s = slots_[pick];
        // Clear RT hit-stop bit before reinitializing
        activeHitStopMask_.fetch_and(~(1u << pick), std::memory_order_acq_rel);
        s.entityKey.store(0u, std::memory_order_release);

        const float baseHs = isAttacker ? 0.018f : ((style == ImpactStyle::Chop || style == ImpactStyle::Mine) ? 0.022f : 0.032f);
        const float maxHs  = isAttacker ? 0.040f : 0.072f;
        s.hitStopSec   = std::clamp(baseHs * inten * hsScale, 0.0f, maxHs);
        if (isAttacker) {
            s.recoilDurSec = (lgScale > 0.005f) ? 0.115f : 0.0f;
            s.flashDurSec  = 0.0f;
            s.jitterAmp    = 0.0f;
            s.lungeAmp     = (lgScale > 0.005f) ? std::clamp(0.18f * inten * lgScale, 0.0f, 0.30f) : 0.0f;
            s.squashAmp    = (lgScale > 0.005f) ? std::clamp(0.055f * inten * lgScale, 0.0f, 0.11f) : 0.0f;
        } else {
            s.recoilDurSec = (rcScale > 0.005f) ? 0.095f : 0.0f;
            s.flashDurSec  = (flScale > 0.005f) ? ((style == ImpactStyle::Heavy) ? 0.075f : 0.060f) : 0.0f;
            s.jitterAmp    = std::clamp(0.11f * inten * rcScale, 0.0f, 0.24f);
            s.lungeAmp     = 0.0f;
            s.squashAmp    = std::clamp(0.095f * inten * rcScale, 0.0f, 0.22f);
        }

        float flCol[3], coreCol[3], edgeCol[3];
        StyleColors(style, flCol, coreCol, edgeCol);
        s.flashR = std::clamp(flCol[0] * flScale, 0.0f, 1.0f);
        s.flashG = std::clamp(flCol[1] * flScale, 0.0f, 1.0f);
        s.flashB = std::clamp(flCol[2] * flScale, 0.0f, 1.0f);

        s.bindX = wx; s.bindY = wy; s.bindZ = wz;
        s.dirX = dx;  s.dirZ = dz;
        s.startQpc = nowQpc;
        s.isAttacker = isAttacker ? 1u : 0u;

        const bool anyEntityFx = (s.hitStopSec >= 0.004f || s.recoilDurSec >= 0.005f || s.flashDurSec >= 0.005f);
        if (anyEntityFx) {
            s.hitStopEndQpc.store(nowQpc + static_cast<int64_t>(static_cast<double>(s.hitStopSec) * static_cast<double>(qpcFreq)),
                                  std::memory_order_relaxed);
            s.active = 1u;
            RecomputeActiveCount();
        } else {
            s.active = 0u;
            RecomputeActiveCount();
        }

        // Enqueue B5 Procedural 240Hz GPU VFX burst for Render Thread
        if (spawnVfx && vfScale > 0.01f) {
            VfxBurstEvent ev{};
            ev.wx = wx; ev.wy = wy; ev.wz = wz;
            ev.dirX = dx; ev.dirZ = dz;
            ev.intensity = inten;
            ev.vfxScale = vfScale;
            ev.style = style;
            ev.swingParity = (swingParity_++) & 1u;
            ev.startQpc = nowQpc;

            AcquireSRWLockExclusive(&vfxLock_);
            const uint32_t nextHead = (vfxHead_ + 1u) & (kMaxVfxQueue - 1u);
            if (nextHead == vfxTail_) {
                vfxTail_ = (vfxTail_ + 1u) & (kMaxVfxQueue - 1u); // drop oldest if flooded
            }
            vfxQueue_[vfxHead_] = ev;
            vfxHead_ = nextHead;
            ReleaseSRWLockExclusive(&vfxLock_);
            vfxSeq_.fetch_add(1u, std::memory_order_release);
        }

        totalTriggers_.fetch_add(1u, std::memory_order_relaxed);
        return true;
    }

    // Main Thread: Called inside HookAnimRenderCmdCtor AFTER WorldPos interpolation.
    //   1) Binds unbound impulses by exact raw world coords (dist < 0.38 tiles, standing Billboard only!)
    //   2) Applies B2 directional micro-jitter & underdamped spring squash-and-stretch to WorldMtx (+0x18)
    //   3) Applies B3 per-frame saturating additive Hit-Flash to ColourTransform (+0xD4, dirty +0xE2=1)
    bool BindAndApplyOnMainThread(uint8_t* cmd, uintptr_t entityKey,
                                  float rawX, float rawY, float rawZ,
                                  int64_t nowQpc, double qpcInvFreq) noexcept {
        if (!cmd || entityKey == 0 || activeCount_.load(std::memory_order_relaxed) == 0u) return false;
        if (!enabled_.load(std::memory_order_relaxed)) return false;

        // Exclude UI/Debug layers (<3 or >=7) and OnGround decals/rings/shadows (orientMode != 0)
        const uint32_t layer      = *reinterpret_cast<const uint32_t*>(cmd + 0x60);
        const uint32_t orientMode = *reinterpret_cast<const uint32_t*>(cmd + 0x64);
        if (layer < 3u || layer >= 7u || orientMode != 0u) return false;

        int32_t matchedIdx = -1;
        float bestDistSq = 0.38f * 0.38f;
        uint32_t alive = 0u;

        for (uint32_t i = 0; i < kMaxImpulses; ++i) {
            ImpulseSlot& s = slots_[i];
            if (!s.active) continue;
            const float elapsed = static_cast<float>(static_cast<double>(nowQpc - s.startQpc) * qpcInvFreq);
            const float maxDur = std::max({ s.hitStopSec, s.recoilDurSec, s.flashDurSec });
            if (!std::isfinite(elapsed) || elapsed < -0.02f || elapsed >= maxDur) {
                s.active = 0u;
                s.entityKey.store(0u, std::memory_order_release);
                activeHitStopMask_.fetch_and(~(1u << i), std::memory_order_acq_rel);
                continue;
            }
            ++alive;

            const uintptr_t boundKey = s.entityKey.load(std::memory_order_relaxed);
            if (boundKey == entityKey) {
                matchedIdx = static_cast<int32_t>(i);
                bestDistSq = -1.0f; // exact locked key wins unconditionally
            } else if (boundKey == 0u && bestDistSq >= 0.0f && elapsed <= 0.10f) {
                const float dx = rawX - s.bindX;
                const float dz = rawZ - s.bindZ;
                const float d2 = dx * dx + dz * dz;
                if (d2 < bestDistSq) {
                    bestDistSq = d2;
                    matchedIdx = static_cast<int32_t>(i);
                }
            }
        }
        activeCount_.store(alive, std::memory_order_release);

        if (matchedIdx < 0) return false;

        ImpulseSlot& s = slots_[matchedIdx];
        if (s.entityKey.load(std::memory_order_relaxed) == 0u) {
            s.entityKey.store(entityKey, std::memory_order_release);
            if (s.hitStopSec >= 0.004f && nowQpc < s.hitStopEndQpc.load(std::memory_order_relaxed)) {
                activeHitStopMask_.fetch_or(1u << static_cast<uint32_t>(matchedIdx), std::memory_order_acq_rel);
            }
            totalBound_.fetch_add(1u, std::memory_order_relaxed);
        }

        const float elapsed = std::max(0.0f, static_cast<float>(static_cast<double>(nowQpc - s.startQpc) * qpcInvFreq));
        if (elapsed >= s.hitStopSec) {
            activeHitStopMask_.fetch_and(~(1u << static_cast<uint32_t>(matchedIdx)), std::memory_order_acq_rel);
        }
        if (s.isAttacker) {
            // B4 (Zero-Shake Replacement): Attacker Follow-Through Lunge & Elastic Settle on WorldMtx (+0x18)
            // Pure render-only visual commitment along strike direction: 0 physical drift, 0 camera shake!
            if (elapsed < s.recoilDurSec && (s.lungeAmp > 1e-4f || s.squashAmp > 1e-4f)) {
                auto* mtx = reinterpret_cast<float*>(cmd + 0x18);
                const float peakT = std::max(0.018f, s.hitStopSec);
                float w = 0.0f;
                if (elapsed <= peakT) {
                    const float p = std::clamp(elapsed / peakT, 0.0f, 1.0f);
                    w = sinf(p * 1.5707963f);
                } else {
                    const float tailDur = std::max(0.020f, s.recoilDurSec - peakT);
                    const float u = std::clamp((elapsed - peakT) / tailDur, 0.0f, 1.0f);
                    const float inv = 1.0f - u;
                    w = inv * inv * cosf(u * 2.0f);
                }
                if (s.lungeAmp > 1e-4f) {
                    mtx[3]  += s.dirX * (s.lungeAmp * w);
                    mtx[11] += s.dirZ * (s.lungeAmp * w);
                }
                if (s.squashAmp > 1e-4f) {
                    const float sx = std::clamp(1.0f + s.squashAmp * w, 0.90f, 1.12f);
                    const float sy = std::clamp(1.0f - s.squashAmp * 0.55f * w, 0.92f, 1.08f);
                    mtx[0] *= sx; mtx[4] *= sx; mtx[8] *= sx;
                    mtx[1] *= sy; mtx[5] *= sy; mtx[9] *= sy;
                }
            }
            return true;
        }

        // B2: Directional Recoil Micro-Jitter + Underdamped Spring Squash-and-Stretch on WorldMtx (+0x18)
        auto* mtx = reinterpret_cast<float*>(cmd + 0x18);
        if (elapsed < s.recoilDurSec && (s.jitterAmp > 1e-4f || s.squashAmp > 1e-4f)) {
            if (elapsed < s.hitStopSec && s.jitterAmp > 1e-4f && s.hitStopSec > 1e-4f) {
                const float j = sinf(elapsed * 240.0f) * (1.0f - elapsed / s.hitStopSec) * s.jitterAmp;
                mtx[3]  += s.dirX * j;
                mtx[11] += s.dirZ * j;
            }
            if (s.squashAmp > 1e-4f) {
                const float env = expf(-26.0f * elapsed) * cosf(55.0f * elapsed);
                const float sx = std::clamp(1.0f - s.squashAmp * env, 0.78f, 1.22f);
                const float sy = std::clamp(1.0f + s.squashAmp * 1.15f * env, 0.80f, 1.26f);
                // Row-major 4x4 WorldMtx: Column 0 = local X (indices 0,4,8), Column 1 = local Y (indices 1,5,9)
                // Pivot is at entity feet (y=0), so feet stay 100% planted on the ground!
                mtx[0] *= sx; mtx[4] *= sx; mtx[8] *= sx;
                mtx[1] *= sy; mtx[5] *= sy; mtx[9] *= sy;
            }
        }

        // B3: Non-Destructive Per-Frame Additive Hit-Flash on AnimRenderCmd ColourTransform (+0xD4..+0xE2)
        if (elapsed < s.flashDurSec && s.flashDurSec > 1e-4f) {
            uint8_t* ct = cmd + 0xD4;
            const uint8_t multA = ct[7]; // preserve hidden/invisible sprites!
            if (multA >= 10u) {
                const float rem = 1.0f - (elapsed / s.flashDurSec);
                const float f = rem * rem;
                const int addR = static_cast<int>(s.flashR * f * 255.0f + 0.5f);
                const int addG = static_cast<int>(s.flashG * f * 255.0f + 0.5f);
                const int addB = static_cast<int>(s.flashB * f * 255.0f + 0.5f);
                if (addR > 0 || addG > 0 || addB > 0) {
                    ct[0] = static_cast<uint8_t>(std::min<int>(255, static_cast<int>(ct[0]) + addR));
                    ct[1] = static_cast<uint8_t>(std::min<int>(255, static_cast<int>(ct[1]) + addG));
                    ct[2] = static_cast<uint8_t>(std::min<int>(255, static_cast<int>(ct[2]) + addB));
                    ct[0x0E] = 1u; // Trigger Klei ColourTransform::GetMatrix() rebuild for THIS cmd only!
                }
            }
        }
        return true;
    }

    // Render Thread: Wait-free O(popcount) check whether entityKey is currently in B1 Visual Hit-Stop!
    bool IsInHitStop(uintptr_t entityKey, int64_t nowQpc) const noexcept {
        uint32_t mask = activeHitStopMask_.load(std::memory_order_acquire);
        if (mask == 0u || entityKey == 0u) return false;
        while (mask) {
            unsigned long idx = 0;
            _BitScanForward(&idx, mask);
            mask &= (mask - 1u);
            if (slots_[idx].entityKey.load(std::memory_order_acquire) == entityKey &&
                nowQpc < slots_[idx].hitStopEndQpc.load(std::memory_order_relaxed)) {
                return true;
            }
        }
        return false;
    }

    // Render Thread: Drains newly queued B5 VFX bursts into caller's thread-local vector
    void DrainVfxQueue(std::vector<VfxBurstEvent>& out) noexcept {
        out.clear();
        AcquireSRWLockExclusive(&vfxLock_);
        while (vfxTail_ != vfxHead_) {
            out.push_back(vfxQueue_[vfxTail_]);
            vfxTail_ = (vfxTail_ + 1u) & (kMaxVfxQueue - 1u);
        }
        ReleaseSRWLockExclusive(&vfxLock_);
    }

    uint32_t TotalTriggers() const noexcept { return totalTriggers_.load(std::memory_order_relaxed); }
    uint32_t TotalBound() const noexcept { return totalBound_.load(std::memory_order_relaxed); }

private:
    void RecomputeActiveCount() noexcept {
        uint32_t c = 0u;
        for (uint32_t i = 0; i < kMaxImpulses; ++i) {
            if (slots_[i].active) ++c;
        }
        activeCount_.store(c, std::memory_order_release);
    }

    std::atomic<bool>     enabled_{false};
    std::atomic<float>    hitStopScale_{1.0f};
    std::atomic<float>    recoilScale_{1.0f};
    std::atomic<float>    flashScale_{1.0f};
    std::atomic<float>    vfxScale_{1.0f};
    std::atomic<float>    lungeScale_{0.85f};
    std::atomic<uint32_t> activeCount_{0};
    std::atomic<uint32_t> activeHitStopMask_{0};
    std::atomic<uint32_t> vfxSeq_{0};
    std::atomic<uint32_t> totalTriggers_{0};
    std::atomic<uint32_t> totalBound_{0};
    uint32_t              swingParity_ = 0u;
    ImpulseSlot           slots_[kMaxImpulses]{};

    SRWLOCK       vfxLock_ = SRWLOCK_INIT;
    VfxBurstEvent vfxQueue_[kMaxVfxQueue]{};
    uint32_t      vfxHead_ = 0u;
    uint32_t      vfxTail_ = 0u;
};

// =============================================================================
// B5: 240Hz Single-DrawCall Procedural GPU Combat VFX Renderer
//     Draws Weapon Slash Crescents + Directional Sparks / Wood Chips + Impact Rings
//     into PrimaryRT right when World Pass completes (BEFORE Bloom / ColourCube / UI).
//     Dual-Backend verified: Native OpenGL 4.6 (#version 100) + ANGLE D3D9 GLES 2.0!
// =============================================================================
struct VfxVertex {
    float x, y, z;
    float r, g, b, a;
};
static_assert(sizeof(VfxVertex) == 28, "VfxVertex must be 28 bytes");

struct ActiveSlash {
    float   wx, wy, wz;
    float   baseAngle;
    float   sweepDir;   // +1 or -1 alternating combo crescent!
    float   radius;
    float   width;
    float   durSec;
    float   coreRgb[3];
    float   edgeRgb[3];
    int64_t startQpc;
    bool    active;
};

struct ActiveSpark {
    float   x0, y0, z0;
    float   vx, vy, vz;
    float   gravity;
    float   drag;
    float   lenScale;
    float   halfWidth;
    float   durSec;
    float   rgb[3];
    float   alphaMode;  // 0.0 = pure additive glow (sparks), 1.0 = solid premul (wood chips!)
    int64_t startQpc;
    bool    active;
};

struct ActiveRing {
    float   wx, wy, wz;
    float   r0, r1;
    float   thickness;
    float   durSec;
    float   rgb[3];
    int64_t startQpc;
    bool    active;
};

struct ActiveGlow {
    float   wx, wy, wz;
    float   groundRadius;
    float   starLen;
    float   durSec;
    float   groundRgb[3];
    float   starRgb[3];
    int64_t startQpc;
    bool    active;
};

class CombatVfxRenderer {
public:
    void SetOps(const hs_world_shadow::GlShadowOps& ops, bool isNativeGl) noexcept {
        ops_ = ops;
        isNativeGl_ = isNativeGl;
        hasOps_ = (ops_.glCreateShader && ops_.glShaderSource && ops_.glCompileShader &&
                   ops_.glGetShaderiv && ops_.glDeleteShader && ops_.glCreateProgram &&
                   ops_.glAttachShader && ops_.glBindAttribLocation && ops_.glLinkProgram &&
                   ops_.glGetProgramiv && ops_.glDeleteProgram && ops_.glUseProgram &&
                   ops_.glGetUniformLocation && ops_.glUniformMatrix4fv &&
                   ops_.glGenBuffers && ops_.glDeleteBuffers && ops_.glBindBuffer &&
                   ops_.glBufferData && ops_.glEnableVertexAttribArray &&
                   ops_.glDisableVertexAttribArray && ops_.glVertexAttribPointer &&
                   ops_.glDrawArrays && ops_.glEnable && ops_.glDisable &&
                   ops_.glBlendFunc && ops_.glDepthMask && ops_.glColorMask);
    }

    void OnMatrixP(const float* m) noexcept {
        if (!m) return;
        std::memcpy(matP_, m, sizeof(matP_));
        hasP_ = true;
        if (hasV_) RecomputePV();
    }

    void OnMatrixV(const float* m) noexcept {
        if (!m) return;
        std::memcpy(matV_, m, sizeof(matV_));
        hasV_ = true;
        // Column-major OpenGL View matrix: Row 0 = camRight, Row 1 = camUp, Row 2 = camBack
        camRight_[0] = m[0]; camRight_[1] = m[4]; camRight_[2] = m[8];
        camUp_[0]    = m[1]; camUp_[1]    = m[5]; camUp_[2]    = m[9];
        camBack_[0]  = m[2]; camBack_[1]  = m[6]; camBack_[2]  = m[10];
        Norm3(camRight_);
        Norm3(camUp_);
        Norm3(camBack_);
        if (hasP_) RecomputePV();
    }

    void OnMatrixPV(const float* m) noexcept {
        if (!m) return;
        std::memcpy(matPV_, m, sizeof(matPV_));
        hasPV_ = true;
    }

    void OnGroundOrOceanDraw(uint32_t fbo, const int32_t vp[4]) noexcept {
        primaryFbo_ = fbo;
        if (vp) std::memcpy(worldVp_, vp, sizeof(worldVp_));
        seenWorldGround_ = true;
    }

    void OnStandingEntityDraw(uint32_t fbo) noexcept {
        if (seenWorldGround_ && fbo == primaryFbo_) {
            seenWorldStanding_ = true;
        }
    }

    void OnEndFrame() noexcept {
        seenWorldGround_ = false;
        seenWorldStanding_ = false;
        renderedThisFrame_ = false;
    }

    bool ShouldRenderOnLeaveFbo(uint32_t curFbo, uint32_t nextFbo) const noexcept {
        return hasOps_ && !failed_ && hasPV_ && seenWorldGround_ && seenWorldStanding_ &&
               !renderedThisFrame_ && curFbo == primaryFbo_ && nextFbo != curFbo;
    }

    void ResetContext(bool hasCurrentContext) noexcept {
        if (hasCurrentContext && hasOps_) {
            if (prog_ != 0u) ops_.glDeleteProgram(prog_);
            if (vbo_  != 0u) ops_.glDeleteBuffers(1, &vbo_);
        }
        prog_ = 0u;
        vbo_ = 0u;
        locPV_ = -1;
        ready_ = false;
        failed_ = false;
        lastVfxSeq_ = 0u;
        for (auto& s : slashes_) s.active = false;
        for (auto& p : sparks_)  p.active = false;
        for (auto& r : rings_)   r.active = false;
        for (auto& g : glows_)   g.active = false;
        OnEndFrame();
    }

    bool IsReady() const noexcept { return ready_; }
    bool IsFailed() const noexcept { return failed_; }
    uint32_t LastDrawVerts() const noexcept { return lastDrawVerts_; }
    uint32_t TotalDraws() const noexcept { return totalDraws_; }

    // Synchronizes bursts from engine, builds 240Hz geometry, and draws into currently-bound PrimaryRT!
    bool RenderIntoPrimaryRt(CombatImpactEngine& engine,
                             int64_t nowQpc,
                             double qpcInvFreq,
                             const hs_world_shadow::ShadowPassState& saved,
                             hs_gl_cache::ThreadState& glCache) noexcept {
        renderedThisFrame_ = true;
        lastDrawVerts_ = 0u;
        if (!hasOps_ || failed_ || !hasPV_) return false;

        // 1. Drain any newly queued VFX bursts from Main Thread
        const uint32_t curSeq = engine.VfxQueueSeq();
        if (curSeq != lastVfxSeq_) {
            lastVfxSeq_ = curSeq;
            engine.DrainVfxQueue(drainBuf_);
            for (const auto& ev : drainBuf_) {
                SpawnBurst(ev);
            }
        }

        // 2. Build 240Hz triangles; fast-exit with 0 GL calls when no active particles!
        verts_.clear();
        BuildGeometry(nowQpc, qpcInvFreq);
        if (verts_.empty()) return true;

        // 3. Lazy-initialize tiny 1-shader program + streaming VBO
        if (!ready_ && !EnsureResources(saved)) return false;

        // 4. Snapshot host GL state (exact mirror of WorldShadowEngine safety discipline!)
        int32_t hostBlend = 1, hostDepth = 0, hostCull = 0, hostStencil = 0, hostScissor = 0, hostDepthMask = 0;
        int32_t hostProg = static_cast<int32_t>(saved.dstProg);
        int32_t hostColorMask[4] = { 1, 1, 1, 1 };
        int32_t hostBlendSrcRgb = hs_world_shadow::kGlOne, hostBlendDstRgb = hs_world_shadow::kGlOneMinusSrcAlpha;
        int32_t hostBlendSrcA = hs_world_shadow::kGlOne, hostBlendDstA = hs_world_shadow::kGlOneMinusSrcAlpha;
        int32_t hostEqRgb = hs_world_shadow::kGlFuncAdd, hostEqA = hs_world_shadow::kGlFuncAdd;
        if (ops_.glGetIntegerv) {
            ops_.glGetIntegerv(hs_world_shadow::kGlCurrentProgram, &hostProg);
            ops_.glGetIntegerv(hs_world_shadow::kGlBlend, &hostBlend);
            ops_.glGetIntegerv(hs_world_shadow::kGlDepthTest, &hostDepth);
            ops_.glGetIntegerv(hs_world_shadow::kGlCullFace, &hostCull);
            ops_.glGetIntegerv(hs_world_shadow::kGlStencilTest, &hostStencil);
            ops_.glGetIntegerv(hs_world_shadow::kGlScissorTest, &hostScissor);
            ops_.glGetIntegerv(hs_world_shadow::kGlDepthWritemask, &hostDepthMask);
            ops_.glGetIntegerv(hs_world_shadow::kGlColorWritemask, hostColorMask);
            ops_.glGetIntegerv(hs_world_shadow::kGlBlendSrcRgb, &hostBlendSrcRgb);
            ops_.glGetIntegerv(hs_world_shadow::kGlBlendDstRgb, &hostBlendDstRgb);
            ops_.glGetIntegerv(hs_world_shadow::kGlBlendSrcAlpha, &hostBlendSrcA);
            ops_.glGetIntegerv(hs_world_shadow::kGlBlendDstAlpha, &hostBlendDstA);
            ops_.glGetIntegerv(hs_world_shadow::kGlBlendEquationRgb, &hostEqRgb);
            ops_.glGetIntegerv(hs_world_shadow::kGlBlendEquationAlpha, &hostEqA);
        }

        // 5. Render all slashes, sparks, chips, and rings in ONE SINGLE GL DRAW CALL!
        if (worldVp_[2] > 0 && worldVp_[3] > 0) {
            ops_.glViewport(worldVp_[0], worldVp_[1], worldVp_[2], worldVp_[3]);
        }
        ops_.glDisable(hs_world_shadow::kGlDepthTest);
        ops_.glDepthMask(0u);
        ops_.glDisable(hs_world_shadow::kGlCullFace);
        ops_.glDisable(hs_world_shadow::kGlStencilTest);
        ops_.glDisable(hs_world_shadow::kGlScissorTest);
        ops_.glColorMask(1u, 1u, 1u, 1u);
        ops_.glEnable(hs_world_shadow::kGlBlend);
        if (ops_.glBlendEquation) ops_.glBlendEquation(hs_world_shadow::kGlFuncAdd);
        // Hybrid Premul + Additive: a=0 gives pure additive glow, a>0 gives opaque wood chips!
        ops_.glBlendFunc(hs_world_shadow::kGlOne, hs_world_shadow::kGlOneMinusSrcAlpha);

        ops_.glUseProgram(prog_);
        if (locPV_ >= 0) {
            ops_.glUniformMatrix4fv(locPV_, 1, 0u, matPV_);
        }
        ops_.glBindBuffer(hs_world_shadow::kGlArrayBuffer, vbo_);
        ops_.glBufferData(hs_world_shadow::kGlArrayBuffer,
                          static_cast<ptrdiff_t>(verts_.size() * sizeof(VfxVertex)),
                          verts_.data(), kGlDynamicDraw);
        ops_.glEnableVertexAttribArray(0u);
        ops_.glEnableVertexAttribArray(1u);
        ops_.glVertexAttribPointer(0u, 3, hs_world_shadow::kGlFloat, 0u, sizeof(VfxVertex), reinterpret_cast<const void*>(0));
        ops_.glVertexAttribPointer(1u, 4, hs_world_shadow::kGlFloat, 0u, sizeof(VfxVertex), reinterpret_cast<const void*>(12));

        ops_.glDrawArrays(hs_world_shadow::kGlTriangles, 0, static_cast<int32_t>(verts_.size()));
        lastDrawVerts_ = static_cast<uint32_t>(verts_.size());
        ++totalDraws_;

        // 6. Strict 100% Host GL State Restoration (including BOTH attrib 0 & attrib 1 enable bits + pointers!
        //    Klei's RenderDevice::BindVertexFormat caches enabled attribs in a CPU bitmask and XOR-diffs
        //    only changed bits; unconditionally disabling attrib 1 desyncs TEXCOORD0 across PostProcess!)
        if (saved.attr0.enabled) ops_.glEnableVertexAttribArray(0u);
        else                     ops_.glDisableVertexAttribArray(0u);
        if (saved.attr1.enabled) ops_.glEnableVertexAttribArray(1u);
        else                     ops_.glDisableVertexAttribArray(1u);
        auto restoreAttribPtr = [&](uint32_t idx, const hs_world_shadow::SavedAttrib0& a) {
            if (!a.valid) return;
            ops_.glBindBuffer(hs_world_shadow::kGlArrayBuffer, a.buffer);
            ops_.glVertexAttribPointer(idx, a.size, a.type, a.normalized, a.stride, a.pointer);
        };
        restoreAttribPtr(0u, saved.attr0);
        restoreAttribPtr(1u, saved.attr1);
        ops_.glBindBuffer(hs_world_shadow::kGlArrayBuffer, saved.boundArrayBuf);
        ops_.glBindBuffer(hs_world_shadow::kGlElementArrayBuffer, saved.boundElemBuf);
        ops_.glUseProgram(hostProg >= 0 ? static_cast<uint32_t>(hostProg) : saved.dstProg);
        if (ops_.glBlendEquationSeparate) {
            ops_.glBlendEquationSeparate(static_cast<uint32_t>(hostEqRgb), static_cast<uint32_t>(hostEqA));
        } else if (ops_.glBlendEquation) {
            ops_.glBlendEquation(static_cast<uint32_t>(hostEqRgb));
        }
        if (ops_.glBlendFuncSeparate) {
            ops_.glBlendFuncSeparate(static_cast<uint32_t>(hostBlendSrcRgb), static_cast<uint32_t>(hostBlendDstRgb),
                                     static_cast<uint32_t>(hostBlendSrcA),   static_cast<uint32_t>(hostBlendDstA));
        } else {
            ops_.glBlendFunc(static_cast<uint32_t>(hostBlendSrcRgb), static_cast<uint32_t>(hostBlendDstRgb));
        }
        if (hostBlend)   ops_.glEnable(hs_world_shadow::kGlBlend);       else ops_.glDisable(hs_world_shadow::kGlBlend);
        if (hostDepth)   ops_.glEnable(hs_world_shadow::kGlDepthTest);   else ops_.glDisable(hs_world_shadow::kGlDepthTest);
        if (hostCull)    ops_.glEnable(hs_world_shadow::kGlCullFace);    else ops_.glDisable(hs_world_shadow::kGlCullFace);
        if (hostStencil) ops_.glEnable(hs_world_shadow::kGlStencilTest); else ops_.glDisable(hs_world_shadow::kGlStencilTest);
        if (hostScissor) ops_.glEnable(hs_world_shadow::kGlScissorTest); else ops_.glDisable(hs_world_shadow::kGlScissorTest);
        ops_.glDepthMask(hostDepthMask ? 1u : 0u);
        ops_.glColorMask(hostColorMask[0] ? 1u : 0u, hostColorMask[1] ? 1u : 0u,
                         hostColorMask[2] ? 1u : 0u, hostColorMask[3] ? 1u : 0u);
        if (saved.vp[2] > 0 && saved.vp[3] > 0) {
            ops_.glViewport(saved.vp[0], saved.vp[1], saved.vp[2], saved.vp[3]);
        }
        glCache.ResetBindingsOnly();
        return true;
    }

private:
    static void Norm3(float v[3]) noexcept {
        const float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        if (l > 1e-5f) { v[0] /= l; v[1] /= l; v[2] /= l; }
    }

    void RecomputePV() noexcept {
        // Column-major 4x4 matrix multiply: matPV_ = matP_ * matV_
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                matPV_[c * 4 + r] =
                    matP_[0 * 4 + r] * matV_[c * 4 + 0] +
                    matP_[1 * 4 + r] * matV_[c * 4 + 1] +
                    matP_[2 * 4 + r] * matV_[c * 4 + 2] +
                    matP_[3 * 4 + r] * matV_[c * 4 + 3];
            }
        }
        hasPV_ = true;
    }

    uint32_t NextRand() noexcept {
        rngState_ ^= rngState_ << 13;
        rngState_ ^= rngState_ >> 17;
        rngState_ ^= rngState_ << 5;
        return rngState_;
    }

    float Rand01() noexcept {
        return static_cast<float>(NextRand() & 0xFFFFu) * (1.0f / 65535.0f);
    }

    void SpawnBurst(const VfxBurstEvent& ev) noexcept {
        float flCol[3], coreCol[3], edgeCol[3];
        StyleColors(ev.style, flCol, coreCol, edgeCol);
        const float scale = std::clamp(ev.vfxScale, 0.1f, 1.5f);
        const float inten = std::clamp(ev.intensity, 0.35f, 1.6f);
        const float baseAng = atan2f(ev.dirZ, ev.dirX);
        const bool isWork = (ev.style == ImpactStyle::Chop || ev.style == ImpactStyle::Mine);

        // 1. Weapon Slash Crescent Arc (combat strikes!)
        if (!isWork) {
            ActiveSlash& sl = slashes_[slashCursor_++ & (kMaxSlashArcs - 1u)];
            sl.wx = ev.wx - ev.dirX * 0.45f;
            sl.wy = ev.wy + 1.05f;
            sl.wz = ev.wz - ev.dirZ * 0.45f;
            sl.baseAngle = baseAng;
            sl.sweepDir  = ev.swingParity ? 1.0f : -1.0f;
            sl.radius    = (1.05f + 0.32f * inten) * (0.75f + 0.25f * scale);
            sl.width     = (0.28f + 0.12f * inten) * scale;
            sl.durSec    = 0.115f;
            sl.coreRgb[0] = coreCol[0] * scale; sl.coreRgb[1] = coreCol[1] * scale; sl.coreRgb[2] = coreCol[2] * scale;
            sl.edgeRgb[0] = edgeCol[0] * scale; sl.edgeRgb[1] = edgeCol[1] * scale; sl.edgeRgb[2] = edgeCol[2] * scale;
            sl.startQpc  = ev.startQpc;
            sl.active    = true;
        }

        // 2. Directional Impact Sparks / Wood Chips
        const uint32_t nSparks = static_cast<uint32_t>(std::clamp(
            static_cast<int>((isWork ? 9.0f : 11.0f) * inten * (0.65f + 0.35f * scale)), 5, 22));
        for (uint32_t i = 0; i < nSparks; ++i) {
            ActiveSpark& sp = sparks_[sparkCursor_++ & (kMaxSparks - 1u)];
            const float spread = (Rand01() - 0.5f) * (isWork ? 2.4f : 1.55f);
            const float ang = baseAng + spread;
            const float spd = (isWork ? 3.2f : 5.4f) + Rand01() * (isWork ? 3.6f : 6.2f) * inten;
            sp.x0 = ev.wx + (Rand01() - 0.5f) * 0.22f;
            sp.y0 = ev.wy + (isWork ? 0.90f : 1.05f) + (Rand01() - 0.5f) * 0.28f;
            sp.z0 = ev.wz + (Rand01() - 0.5f) * 0.22f;
            sp.vx = cosf(ang) * spd;
            sp.vz = sinf(ang) * spd;
            sp.vy = (isWork ? 2.8f : 2.2f) + Rand01() * 3.8f;
            sp.gravity   = isWork ? 17.5f : 13.5f;
            sp.drag      = isWork ? 2.8f : 4.6f;
            sp.lenScale  = (ev.style == ImpactStyle::Chop) ? 0.030f : (0.045f * (0.7f + 0.3f * scale));
            sp.halfWidth = (ev.style == ImpactStyle::Chop) ? (0.070f * scale) : (0.036f * scale);
            sp.durSec    = (isWork ? 0.22f : 0.17f) + Rand01() * 0.11f;
            const float mixC = Rand01();
            sp.rgb[0] = (coreCol[0] * mixC + edgeCol[0] * (1.0f - mixC)) * scale;
            sp.rgb[1] = (coreCol[1] * mixC + edgeCol[1] * (1.0f - mixC)) * scale;
            sp.rgb[2] = (coreCol[2] * mixC + edgeCol[2] * (1.0f - mixC)) * scale;
            sp.alphaMode = (ev.style == ImpactStyle::Chop) ? 0.92f : 0.0f;
            sp.startQpc  = ev.startQpc;
            sp.active    = true;
        }

        // 3. Impact Shockwave Ring (prominent on Heavy/Crit/Boss/Kill, crisp subtle burst on normal strikes)
        if (!isWork || inten >= 1.15f) {
            ActiveRing& rg = rings_[ringCursor_++ & (kMaxShockRings - 1u)];
            const bool heavy = (ev.style == ImpactStyle::Heavy || inten >= 1.12f);
            rg.wx = ev.wx;
            rg.wy = ev.wy + 1.00f;
            rg.wz = ev.wz;
            rg.r0 = 0.14f;
            rg.r1 = (heavy ? 1.32f : 0.72f) * (0.75f + 0.25f * scale);
            rg.thickness = (heavy ? 0.14f : 0.08f) * scale;
            rg.durSec = heavy ? 0.145f : 0.095f;
            const float gain = (heavy ? 0.95f : 0.55f) * scale;
            rg.rgb[0] = coreCol[0] * gain;
            rg.rgb[1] = coreCol[1] * gain;
            rg.rgb[2] = coreCol[2] * gain;
            rg.startQpc = ev.startQpc;
            rg.active = true;
        }

        // 4. Localized Hit-Point Ground Glow + Crisp 4-Point Cross-Flare (ZERO screen shake / ZERO full-screen flash!)
        {
            ActiveGlow& gl = glows_[glowCursor_++ & (kMaxImpactGlows - 1u)];
            const bool heavy = (ev.style == ImpactStyle::Heavy || inten >= 1.12f);
            gl.wx = ev.wx;
            gl.wy = ev.wy;
            gl.wz = ev.wz;
            gl.groundRadius = (isWork ? 0.72f : (heavy ? 1.30f : 0.95f)) * (0.75f + 0.25f * scale);
            gl.starLen      = (isWork ? 0.34f : (heavy ? 0.68f : 0.46f)) * (0.70f + 0.30f * scale);
            gl.durSec       = isWork ? 0.065f : (heavy ? 0.095f : 0.075f);
            const float gGain = (ev.style == ImpactStyle::Chop ? 0.16f : (heavy ? 0.42f : 0.30f)) * scale;
            gl.groundRgb[0] = edgeCol[0] * gGain;
            gl.groundRgb[1] = edgeCol[1] * gGain;
            gl.groundRgb[2] = edgeCol[2] * gGain;
            const float sGain = (ev.style == ImpactStyle::Chop ? 0.35f : (heavy ? 0.95f : 0.72f)) * scale;
            gl.starRgb[0] = coreCol[0] * sGain;
            gl.starRgb[1] = coreCol[1] * sGain;
            gl.starRgb[2] = coreCol[2] * sGain;
            gl.startQpc = ev.startQpc;
            gl.active = true;
        }
    }

    void PushTri(const VfxVertex& a, const VfxVertex& b, const VfxVertex& c) {
        if (verts_.size() + 3u > 4096u) return;
        verts_.push_back(a);
        verts_.push_back(b);
        verts_.push_back(c);
    }

    void PushQuad(const VfxVertex& p0, const VfxVertex& p1, const VfxVertex& p2, const VfxVertex& p3) {
        PushTri(p0, p1, p2);
        PushTri(p1, p3, p2);
    }

    void BuildGeometry(int64_t nowQpc, double qpcInvFreq) {
        constexpr float kPi = 3.14159265358979323846f;

        // A. Weapon Slash Crescent Arcs (12 curved ribbon segments)
        for (auto& sl : slashes_) {
            if (!sl.active) continue;
            const float t = static_cast<float>(static_cast<double>(nowQpc - sl.startQpc) * qpcInvFreq);
            if (!std::isfinite(t) || t < 0.0f || t >= sl.durSec) { sl.active = false; continue; }
            const float u = t / sl.durSec;
            const float inv = 1.0f - u;
            const float fade = inv * inv;
            const float headProg = 1.0f - inv * inv * inv; // fast ease-out leading blade
            const float tailProg = u * u;                  // smooth chasing trail erase
            const float totalSweep = 2.05f;                // ~117 deg crescent
            const float angStart = sl.baseAngle - sl.sweepDir * (totalSweep * 0.5f);

            constexpr int kSegs = 12;
            const float span = std::max(0.05f, headProg - tailProg);
            for (int i = 0; i < kSegs; ++i) {
                const float f0 = static_cast<float>(i) / static_cast<float>(kSegs);
                const float f1 = static_cast<float>(i + 1) / static_cast<float>(kSegs);
                const float p0 = tailProg + span * f0;
                const float p1 = tailProg + span * f1;
                const float a0 = angStart + sl.sweepDir * (p0 * totalSweep);
                const float a1 = angStart + sl.sweepDir * (p1 * totalSweep);

                // Tapered crescent thickness (sharp tips, broad belly near leading edge)
                const float w0 = sl.width * sinf(f0 * kPi) * (0.45f + 0.55f * f0);
                const float w1 = sl.width * sinf(f1 * kPi) * (0.45f + 0.55f * f1);
                const float b0 = fade * (0.20f + 0.80f * f0);
                const float b1 = fade * (0.20f + 0.80f * f1);

                const float c0 = cosf(a0), s0 = sinf(a0);
                const float c1 = cosf(a1), s1 = sinf(a1);
                // Slight graceful diagonal tilt along the sweep for dynamic 3D sword feel
                const float yOff0 = (p0 - 0.5f) * sl.sweepDir * 0.42f;
                const float yOff1 = (p1 - 0.5f) * sl.sweepDir * 0.42f;

                const float rIn0 = std::max(0.15f, sl.radius - w0);
                const float rIn1 = std::max(0.15f, sl.radius - w1);
                const float rOut0 = sl.radius + w0 * 0.35f;
                const float rOut1 = sl.radius + w1 * 0.35f;

                VfxVertex in0  = { sl.wx + c0 * rIn0,  sl.wy + yOff0, sl.wz + s0 * rIn0,
                                   sl.edgeRgb[0] * b0 * 0.25f, sl.edgeRgb[1] * b0 * 0.25f, sl.edgeRgb[2] * b0 * 0.25f, 0.0f };
                VfxVertex out0 = { sl.wx + c0 * rOut0, sl.wy + yOff0, sl.wz + s0 * rOut0,
                                   sl.coreRgb[0] * b0 * 0.95f, sl.coreRgb[1] * b0 * 0.95f, sl.coreRgb[2] * b0 * 0.95f, 0.0f };
                VfxVertex in1  = { sl.wx + c1 * rIn1,  sl.wy + yOff1, sl.wz + s1 * rIn1,
                                   sl.edgeRgb[0] * b1 * 0.25f, sl.edgeRgb[1] * b1 * 0.25f, sl.edgeRgb[2] * b1 * 0.25f, 0.0f };
                VfxVertex out1 = { sl.wx + c1 * rOut1, sl.wy + yOff1, sl.wz + s1 * rOut1,
                                   sl.coreRgb[0] * b1 * 0.95f, sl.coreRgb[1] * b1 * 0.95f, sl.coreRgb[2] * b1 * 0.95f, 0.0f };
                PushQuad(in0, out0, in1, out1);
            }
        }

        // B. Velocity-Stretched Directional Sparks & Wood Chips (240Hz ballistic arcs!)
        for (auto& sp : sparks_) {
            if (!sp.active) continue;
            const float t = static_cast<float>(static_cast<double>(nowQpc - sp.startQpc) * qpcInvFreq);
            if (!std::isfinite(t) || t < 0.0f || t >= sp.durSec) { sp.active = false; continue; }
            const float u = t / sp.durSec;
            const float fade = (1.0f - u) * (1.0f - u * 0.5f);

            const float eDrag = expf(-sp.drag * t);
            const float tEff  = (1.0f - eDrag) / std::max(sp.drag, 0.1f);
            float px = sp.x0 + sp.vx * tEff;
            float py = sp.y0 + sp.vy * tEff - 0.5f * sp.gravity * t * t;
            float pz = sp.z0 + sp.vz * tEff;
            if (py < 0.04f) py = 0.04f; // light ground bounce clamp

            const float vxNow = sp.vx * eDrag;
            const float vyNow = sp.vy * eDrag - sp.gravity * t;
            const float vzNow = sp.vz * eDrag;
            const float spd = sqrtf(vxNow * vxNow + vyNow * vyNow + vzNow * vzNow);
            const float halfLen = std::max(sp.halfWidth * 1.4f, spd * sp.lenScale);

            float dir[3] = { vxNow, vyNow, vzNow };
            if (spd > 1e-4f) { dir[0] /= spd; dir[1] /= spd; dir[2] /= spd; }
            else { dir[0] = camRight_[0]; dir[1] = camRight_[1]; dir[2] = camRight_[2]; }

            // Side vector perpendicular to BOTH velocity and camera view vector -> never edge-on!
            float side[3] = {
                dir[1] * camBack_[2] - dir[2] * camBack_[1],
                dir[2] * camBack_[0] - dir[0] * camBack_[2],
                dir[0] * camBack_[1] - dir[1] * camBack_[0],
            };
            const float sLen = sqrtf(side[0] * side[0] + side[1] * side[1] + side[2] * side[2]);
            if (sLen > 1e-4f) { side[0] /= sLen; side[1] /= sLen; side[2] /= sLen; }
            else { side[0] = camUp_[0]; side[1] = camUp_[1]; side[2] = camUp_[2]; }

            const float hw = sp.halfWidth * (0.45f + 0.55f * (1.0f - u));
            const float r = sp.rgb[0] * fade;
            const float g = sp.rgb[1] * fade;
            const float b = sp.rgb[2] * fade;
            const float a = sp.alphaMode * fade;

            VfxVertex head = { px + dir[0] * halfLen, py + dir[1] * halfLen, pz + dir[2] * halfLen, r, g, b, a };
            VfxVertex tail = { px - dir[0] * halfLen, py - dir[1] * halfLen, pz - dir[2] * halfLen, r * 0.25f, g * 0.25f, b * 0.25f, a * 0.35f };
            VfxVertex lft  = { px + side[0] * hw,     py + side[1] * hw,     pz + side[2] * hw,     r * 0.85f, g * 0.85f, b * 0.85f, a * 0.85f };
            VfxVertex rgt  = { px - side[0] * hw,     py - side[1] * hw,     pz - side[2] * hw,     r * 0.85f, g * 0.85f, b * 0.85f, a * 0.85f };
            // Diamond spark needle (2 triangles = crisp pointed head & tail!)
            PushTri(head, lft, rgt);
            PushTri(tail, rgt, lft);
        }

        // C. Expanding Impact Shockwave Rings (18 segments facing camera)
        for (auto& rg : rings_) {
            if (!rg.active) continue;
            const float t = static_cast<float>(static_cast<double>(nowQpc - rg.startQpc) * qpcInvFreq);
            if (!std::isfinite(t) || t < 0.0f || t >= rg.durSec) { rg.active = false; continue; }
            const float u = t / rg.durSec;
            const float inv = 1.0f - u;
            const float prog = 1.0f - inv * inv * inv * inv; // Ease-Out Quart punchy expansion!
            const float fade = inv * inv;
            const float rOut = rg.r0 + (rg.r1 - rg.r0) * prog;
            const float rIn  = std::max(0.02f, rOut - rg.thickness * (0.35f + 0.65f * inv));

            constexpr int kSegs = 18;
            for (int i = 0; i < kSegs; ++i) {
                const float a0 = (static_cast<float>(i) / kSegs) * (2.0f * kPi);
                const float a1 = (static_cast<float>(i + 1) / kSegs) * (2.0f * kPi);
                const float c0 = cosf(a0), s0 = sinf(a0);
                const float c1 = cosf(a1), s1 = sinf(a1);
                auto ringPt = [&](float rad, float ca, float sa, float cr, float cg, float cb) -> VfxVertex {
                    return {
                        rg.wx + (camRight_[0] * ca + camUp_[0] * sa) * rad,
                        rg.wy + (camRight_[1] * ca + camUp_[1] * sa) * rad,
                        rg.wz + (camRight_[2] * ca + camUp_[2] * sa) * rad,
                        cr, cg, cb, 0.0f
                    };
                };
                const float br = fade * 0.78f;
                VfxVertex in0  = ringPt(rIn,  c0, s0, 0.0f, 0.0f, 0.0f);
                VfxVertex out0 = ringPt(rOut, c0, s0, rg.rgb[0] * br, rg.rgb[1] * br, rg.rgb[2] * br);
                VfxVertex in1  = ringPt(rIn,  c1, s1, 0.0f, 0.0f, 0.0f);
                VfxVertex out1 = ringPt(rOut, c1, s1, rg.rgb[0] * br, rg.rgb[1] * br, rg.rgb[2] * br);
                PushQuad(in0, out0, in1, out1);
            }
        }

        // D. Localized Hit-Point Ground Glow + Crisp 4-Point Cross-Flare (pure additive, 0 screen shake!)
        for (auto& gl : glows_) {
            if (!gl.active) continue;
            const float t = static_cast<float>(static_cast<double>(nowQpc - gl.startQpc) * qpcInvFreq);
            if (!std::isfinite(t) || t < 0.0f || t >= gl.durSec) { gl.active = false; continue; }
            const float u = t / gl.durSec;
            const float inv = 1.0f - u;
            const float fade = inv * inv;

            // D1. Soft radial ground light splash on XZ plane (y = wy + 0.04)
            const float gr = gl.groundRadius * (0.72f + 0.35f * (1.0f - inv * inv));
            const float gy = gl.wy + 0.04f;
            VfxVertex center = { gl.wx, gy, gl.wz,
                                 gl.groundRgb[0] * fade, gl.groundRgb[1] * fade, gl.groundRgb[2] * fade, 0.0f };
            constexpr int kGSegs = 14;
            for (int i = 0; i < kGSegs; ++i) {
                const float a0 = (static_cast<float>(i) / kGSegs) * (2.0f * kPi);
                const float a1 = (static_cast<float>(i + 1) / kGSegs) * (2.0f * kPi);
                VfxVertex p0 = { gl.wx + cosf(a0) * gr, gy, gl.wz + sinf(a0) * gr, 0.0f, 0.0f, 0.0f, 0.0f };
                VfxVertex p1 = { gl.wx + cosf(a1) * gr, gy, gl.wz + sinf(a1) * gr, 0.0f, 0.0f, 0.0f, 0.0f };
                PushTri(center, p0, p1);
            }

            // D2. Crisp 4-point diagonal cross-flare star glint at impact point (y = wy + 0.96)
            const float sFade = fade * inv; // punchy razor flash
            const float slen = gl.starLen * (0.65f + 0.55f * (1.0f - inv * inv));
            const float swid = slen * 0.14f;
            const float sx = gl.wx, sy = gl.wy + 0.96f, sz = gl.wz;
            const float sr = gl.starRgb[0] * sFade, sg = gl.starRgb[1] * sFade, sb = gl.starRgb[2] * sFade;
            auto starAxis = [&](float ax0, float ay0, float az0, float px0, float py0, float pz0, float lenM) {
                const float L = slen * lenM;
                const float W = swid * lenM;
                VfxVertex c = { sx, sy, sz, sr, sg, sb, 0.0f };
                VfxVertex tipA = { sx + ax0 * L, sy + ay0 * L, sz + az0 * L, 0.0f, 0.0f, 0.0f, 0.0f };
                VfxVertex tipB = { sx - ax0 * L, sy - ay0 * L, sz - az0 * L, 0.0f, 0.0f, 0.0f, 0.0f };
                VfxVertex sideA = { sx + px0 * W, sy + py0 * W, sz + pz0 * W, sr * 0.45f, sg * 0.45f, sb * 0.45f, 0.0f };
                VfxVertex sideB = { sx - px0 * W, sy - py0 * W, sz - pz0 * W, sr * 0.45f, sg * 0.45f, sb * 0.45f, 0.0f };
                PushTri(c, sideA, tipA);
                PushTri(c, tipA, sideB);
                PushTri(c, sideB, tipB);
                PushTri(c, tipB, sideA);
            };
            constexpr float kInvSqrt2 = 0.70710678f;
            const float d1x = (camRight_[0] + camUp_[0]) * kInvSqrt2;
            const float d1y = (camRight_[1] + camUp_[1]) * kInvSqrt2;
            const float d1z = (camRight_[2] + camUp_[2]) * kInvSqrt2;
            const float d2x = (camRight_[0] - camUp_[0]) * kInvSqrt2;
            const float d2y = (camRight_[1] - camUp_[1]) * kInvSqrt2;
            const float d2z = (camRight_[2] - camUp_[2]) * kInvSqrt2;
            starAxis(d1x, d1y, d1z, d2x, d2y, d2z, 1.0f);
            starAxis(d2x, d2y, d2z, d1x, d1y, d1z, 0.78f);
        }
    }

    bool EnsureResources(const hs_world_shadow::ShadowPassState& saved) noexcept {
        if (ready_) return true;
        if (failed_ || !hasOps_) return false;

        static constexpr const char* kVsBody =
            "attribute vec3 aPos;\n"
            "attribute vec4 aCol;\n"
            "uniform mat4 uPV;\n"
            "varying vec4 vCol;\n"
            "void main() {\n"
            "  vCol = aCol;\n"
            "  gl_Position = uPV * vec4(aPos, 1.0);\n"
            "}\n";
        static constexpr const char* kFsBody =
            "#if defined(GL_ES)\n"
            "precision mediump float;\n"
            "#endif\n"
            "varying vec4 vCol;\n"
            "void main() {\n"
            "  gl_FragColor = vCol;\n"
            "}\n";

        std::string vsSrc = isNativeGl_ ? (std::string("#version 100\n") + kVsBody) : std::string(kVsBody);
        std::string fsSrc = isNativeGl_ ? (std::string("#version 100\n") + kFsBody) : std::string(kFsBody);

        auto compileSh = [&](uint32_t kind, const std::string& src) -> uint32_t {
            uint32_t sh = ops_.glCreateShader(kind);
            if (!sh) return 0u;
            const char* p = src.c_str();
            const int32_t len = static_cast<int32_t>(src.size());
            ops_.glShaderSource(sh, 1, &p, &len);
            ops_.glCompileShader(sh);
            int32_t ok = 0;
            ops_.glGetShaderiv(sh, hs_world_shadow::kGlCompileStatus, &ok);
            if (!ok) {
                ops_.glDeleteShader(sh);
                return 0u;
            }
            return sh;
        };

        uint32_t vs = compileSh(hs_world_shadow::kGlVertexShader, vsSrc);
        uint32_t fs = compileSh(hs_world_shadow::kGlFragmentShader, fsSrc);
        if (!vs || !fs) {
            if (vs) ops_.glDeleteShader(vs);
            if (fs) ops_.glDeleteShader(fs);
            failed_ = true;
            return false;
        }
        uint32_t prog = ops_.glCreateProgram();
        ops_.glAttachShader(prog, vs);
        ops_.glAttachShader(prog, fs);
        ops_.glBindAttribLocation(prog, 0u, "aPos");
        ops_.glBindAttribLocation(prog, 1u, "aCol");
        ops_.glLinkProgram(prog);
        ops_.glDeleteShader(vs);
        ops_.glDeleteShader(fs);

        int32_t lok = 0;
        ops_.glGetProgramiv(prog, hs_world_shadow::kGlLinkStatus, &lok);
        if (!lok) {
            ops_.glDeleteProgram(prog);
            ops_.glUseProgram(saved.dstProg);
            failed_ = true;
            return false;
        }

        uint32_t vbo = 0u;
        ops_.glGenBuffers(1, &vbo);
        if (!vbo) {
            ops_.glDeleteProgram(prog);
            ops_.glUseProgram(saved.dstProg);
            failed_ = true;
            return false;
        }

        prog_  = prog;
        vbo_   = vbo;
        locPV_ = ops_.glGetUniformLocation(prog_, "uPV");
        ready_ = true;
        return true;
    }

    hs_world_shadow::GlShadowOps ops_{};
    bool     hasOps_ = false;
    bool     isNativeGl_ = false;
    bool     ready_ = false;
    bool     failed_ = false;

    uint32_t prog_ = 0u;
    uint32_t vbo_  = 0u;
    int32_t  locPV_ = -1;

    float    matP_[16]{};
    float    matV_[16]{};
    float    matPV_[16]{};
    float    camRight_[3] = { 1.0f, 0.0f, 0.0f };
    float    camUp_[3]    = { 0.0f, 1.0f, 0.0f };
    float    camBack_[3]  = { 0.0f, 0.0f, 1.0f };
    bool     hasP_ = false;
    bool     hasV_ = false;
    bool     hasPV_ = false;

    uint32_t primaryFbo_ = 0u;
    int32_t  worldVp_[4] = { 0, 0, 0, 0 };
    bool     seenWorldGround_ = false;
    bool     seenWorldStanding_ = false;
    bool     renderedThisFrame_ = false;

    uint32_t lastVfxSeq_ = 0u;
    uint32_t slashCursor_ = 0u;
    uint32_t sparkCursor_ = 0u;
    uint32_t ringCursor_ = 0u;
    uint32_t glowCursor_ = 0u;
    uint32_t rngState_ = 0xA341316Cu;
    uint32_t lastDrawVerts_ = 0u;
    uint32_t totalDraws_ = 0u;

    ActiveSlash slashes_[kMaxSlashArcs]{};
    ActiveSpark sparks_[kMaxSparks]{};
    ActiveRing  rings_[kMaxShockRings]{};
    ActiveGlow  glows_[kMaxImpactGlows]{};
    std::vector<VfxBurstEvent> drainBuf_;
    std::vector<VfxVertex>     verts_;
};

} // namespace hs_combat
