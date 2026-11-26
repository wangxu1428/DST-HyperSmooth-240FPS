#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hs_world_pos {

struct SnapPoint {
    double t = 0.0;
    float  x = 0.0f;
    float  y = 0.0f;
    float  z = 0.0f;
};

struct SmoothStats {
    bool   interpolated = false;
    bool   bypassed     = false;
    bool   deJittered   = false;
    uint32_t jitterUs   = 0u;
};

// Unified Main-Thread Snapshot-Timeline World-Position Smoother:
//   - Used identically by AnimRenderCmd (Main-Thread ctor), DynamicShadow, LightMap, and FollowCamera!
//   - Reconstructs uniform virtual server timestamps via a symmetric-bound Phase-Locked Loop (PLL),
//     eliminating 200Hz/60Hz (15/15/20ms) frame-quantization ripples & UDP burst jumps.
//   - Strict same-frame lockstep keyed by monotonic frameSeq (immune to mid-frame clock drift).
//   - Auto-bypasses continuous local Bullet physics (Lag Comp ON) with 0.0ms added latency
//     only after >=5 consecutive single-frame updates (frameSeq delta == 1), with >=3 discrete
//     updates required to exit bypass (immune to 60/75/90/120/144/165/240 FPS jitter!).
class SmoothTimelineTable {
public:
    static constexpr size_t kSlots    = 4096u; // power-of-2 open addressing
    static constexpr uint8_t kMaxSnaps = 8u;

    struct Track {
        uintptr_t   key              = 0u;
        const void* lastAnimPtr      = nullptr;
        uint64_t    lastSeenSeq      = 0u;
        uint64_t    lastChangeSeq    = 0u;
        double      lastSeenSec      = 0.0;
        double      lastArrSec       = 0.0;
        double      lastPlaySec      = 0.0;
        double      period           = 1.0 / 30.0;
        double      jitter           = 0.0;
        double      curDelay         = (1.0 / 30.0) * 1.18;
        float       lastRawX         = 0.0f;
        float       lastRawY         = 0.0f;
        float       lastRawZ         = 0.0f;
        float       rendX            = 0.0f;
        float       rendY            = 0.0f;
        float       rendZ            = 0.0f;
        float       bypassOffX       = 0.0f;
        float       bypassOffY       = 0.0f;
        float       bypassOffZ       = 0.0f;
        float       avgStep          = 0.0f;
        uint32_t    simGen           = 0u;
        uint8_t     fastStreak       = 0u;
        uint8_t     discreteStreak   = 0u;
        uint8_t     continuousBypass = 0u;
        uint8_t     warmupTicks      = 0u;
        uint8_t     settled          = 1u;
        uint8_t     valid            = 0u;
        uint8_t     count            = 0u;
        uint8_t     head             = 0u;
        SnapPoint   snaps[kMaxSnaps]{};

        const SnapPoint& At(uint8_t idx) const noexcept {
            return snaps[(head + idx) & (kMaxSnaps - 1u)];
        }
        const SnapPoint& Front() const noexcept { return At(0u); }
        const SnapPoint& Back()  const noexcept { return At(static_cast<uint8_t>(count - 1u)); }

        void ResetSnaps(double t, float x, float y, float z) noexcept {
            head = 0u;
            count = 1u;
            snaps[0] = { t, x, y, z };
        }

        void PushSnap(double t, float x, float y, float z) noexcept {
            if (count < kMaxSnaps) {
                snaps[(head + count) & (kMaxSnaps - 1u)] = { t, x, y, z };
                ++count;
            } else {
                snaps[head] = { t, x, y, z };
                head = static_cast<uint8_t>((head + 1u) & (kMaxSnaps - 1u));
            }
        }
    };

    SmoothTimelineTable() : tracks_(kSlots) {}

    void Clear() noexcept {
        for (auto& t : tracks_) {
            t.valid = 0u;
            t.key   = 0u;
        }
    }

    void ResetKey(uintptr_t key) noexcept {
        if (key == 0u) return;
        const size_t base = HashKey(key);
        for (size_t p = 0; p < 4u; ++p) {
            Track& cand = tracks_[(base + p) & (kSlots - 1u)];
            if (cand.valid && cand.key == key) {
                cand.valid = 0u;
                cand.key   = 0u;
                return;
            }
        }
    }

    // Evaluates the unified PLL snapshot timeline for `key` at exact `(frameSeq, nowSec, frameDt)`.
    // Returns true if output differs from raw (or is actively interpolated), false on raw pass-through / static.
    bool Evaluate(uintptr_t key,
                  float rawX, float rawY, float rawZ,
                  uint64_t frameSeq, double nowSec, double frameDt,
                  int curSimHz, uint32_t curSimGen,
                  const void* animPtr, bool optDeJitter,
                  float& outX, float& outY, float& outZ,
                  SmoothStats* outStats = nullptr) noexcept {
        outX = rawX;
        outY = rawY;
        outZ = rawZ;
        if (outStats) *outStats = {};
        if (key == 0u || !std::isfinite(rawX) || !std::isfinite(rawY) || !std::isfinite(rawZ)) {
            return false;
        }

        const size_t baseSlot = HashKey(key);
        Track* trPtr = nullptr;
        Track* lruPtr = &tracks_[baseSlot];
        for (size_t p = 0; p < 4u; ++p) {
            Track& cand = tracks_[(baseSlot + p) & (kSlots - 1u)];
            if (cand.valid && cand.key == key) { trPtr = &cand; break; }
            if (!cand.valid) { trPtr = &cand; break; }
            if (cand.lastSeenSec < lruPtr->lastSeenSec) lruPtr = &cand;
        }
        if (!trPtr) trPtr = lruPtr;
        Track& tr = *trPtr;

        // Same-frame multi-pass lockstep guard (exact frameSeq match — zero mid-frame drift!)
        if (tr.valid && tr.key == key && frameSeq != 0u && tr.lastSeenSeq == frameSeq) {
            if (outStats && tr.continuousBypass != 0u) outStats->bypassed = true;
            if (!tr.settled) {
                outX = tr.rendX;
                outY = tr.rendY;
                outZ = tr.rendZ;
                if (outStats) outStats->interpolated = (tr.continuousBypass == 0u);
                return true;
            }
            return false;
        }

        const double prevSeenSec = tr.lastSeenSec;
        const double unseenSec   = (tr.valid && tr.key == key) ? (nowSec - prevSeenSec) : 999.0;
        tr.lastSeenSeq = frameSeq;
        tr.lastSeenSec = nowSec;

        const double nomPeriod = (curSimHz == 60) ? (1.0 / 60.0) : (1.0 / 30.0);
        const double dt = std::clamp((frameDt > 0.0 && frameDt <= 0.10) ? frameDt : 0.005, 0.0005, 0.050);

        auto snapToRaw = [&]() {
            tr.key         = key;
            tr.lastAnimPtr = animPtr;
            tr.lastRawX    = tr.rendX = rawX;
            tr.lastRawY    = tr.rendY = rawY;
            tr.lastRawZ    = tr.rendZ = rawZ;
            tr.bypassOffX  = tr.bypassOffY = tr.bypassOffZ = 0.0f;
            tr.lastArrSec  = nowSec;
            tr.lastPlaySec = nowSec - tr.curDelay;
            tr.jitter      = 0.0;
            tr.avgStep     = 0.0f;
            tr.settled     = 1u;
            tr.valid       = 1u;
            tr.ResetSnaps(nowSec, rawX, rawY, rawZ);
        };

        if (!tr.valid || tr.key != key || unseenSec > 0.16 || unseenSec < -0.05) {
            tr.period           = nomPeriod;
            tr.curDelay         = nomPeriod * 1.18;
            tr.simGen           = curSimGen;
            tr.lastChangeSeq    = 0u;
            tr.fastStreak       = 0u;
            tr.discreteStreak   = 0u;
            tr.continuousBypass = 0u;
            tr.warmupTicks      = 0u;
            snapToRaw();
            return false;
        }

        if (tr.simGen != curSimGen) {
            tr.period           = nomPeriod;
            tr.curDelay         = nomPeriod * 1.18;
            tr.jitter           = 0.0;
            tr.simGen           = curSimGen;
            tr.lastChangeSeq    = 0u;
            tr.fastStreak       = 0u;
            tr.discreteStreak   = 0u;
            tr.continuousBypass = 0u;
            tr.warmupTicks      = 0u;
            tr.avgStep          = 0.0f;
            tr.bypassOffX       = tr.bypassOffY = tr.bypassOffZ = 0.0f;
        }

        const bool animChanged = (animPtr != nullptr && tr.lastAnimPtr != nullptr && animPtr != tr.lastAnimPtr);
        if (animPtr != nullptr) {
            tr.lastAnimPtr = animPtr;
        }

        const bool rawChanged = (rawX != tr.lastRawX || rawY != tr.lastRawY || rawZ != tr.lastRawZ);
        if (!rawChanged && tr.settled) {
            if (outStats && tr.continuousBypass != 0u) outStats->bypassed = true;
            return false; // Ultra-fast O(1) static entity path
        }

        if (rawChanged) {
            const float dx = rawX - tr.lastRawX;
            const float dy = rawY - tr.lastRawY;
            const float dz = rawZ - tr.lastRawZ;
            const float distSq = dx * dx + dy * dy + dz * dz;
            const double elapsed = nowSec - tr.lastArrSec;
            tr.lastArrSec = nowSec;

            const uint64_t dSeq = (frameSeq > tr.lastChangeSeq && tr.lastChangeSeq != 0u)
                ? (frameSeq - tr.lastChangeSeq)
                : 999u;
            tr.lastChangeSeq = frameSeq;

            // Teleport / discrete-hop guards (wormhole, Lazy Explorer, lunarthrall vine hops, blink/emerge)
            const bool stationaryTeleport = ((tr.settled != 0u || elapsed >= 0.095) && distSq > 1.8225f);
            const bool animSwitchTeleport = (animChanged && distSq > 1.0f);
            if (!std::isfinite(distSq) || distSq > 25.0f || distSq < 1e-10f ||
                stationaryTeleport || animSwitchTeleport) {
                tr.fastStreak = 0u;
                tr.discreteStreak = 0u;
                if (outStats && tr.continuousBypass != 0u) {
                    outStats->bypassed = true;
                }
                snapToRaw();
                return false;
            }

            // Continuous local physics detection (Lag Comp ON):
            // Refresh-rate-invariant frame-step ratio check with hysteresis exit.
            if (dSeq == 1u) {
                if (tr.fastStreak < 16u) ++tr.fastStreak;
                tr.discreteStreak = 0u;
                if (tr.continuousBypass == 0u && tr.fastStreak >= 5u) {
                    tr.continuousBypass = 1u;
                    if (!tr.settled) {
                        const float ox = tr.rendX - rawX;
                        const float oy = tr.rendY - rawY;
                        const float oz = tr.rendZ - rawZ;
                        if ((ox * ox + oy * oy + oz * oz) <= 4.0f) {
                            tr.bypassOffX = ox;
                            tr.bypassOffY = oy;
                            tr.bypassOffZ = oz;
                        } else {
                            tr.bypassOffX = tr.bypassOffY = tr.bypassOffZ = 0.0f;
                        }
                    } else {
                        tr.bypassOffX = tr.bypassOffY = tr.bypassOffZ = 0.0f;
                    }
                }
            } else {
                tr.fastStreak = 0u;
                if (tr.continuousBypass != 0u) {
                    if (elapsed >= 0.011 && elapsed <= 0.16) {
                        if (tr.discreteStreak < 16u) ++tr.discreteStreak;
                    }
                    if (tr.discreteStreak >= 3u) {
                        // Exiting continuous bypass back to discrete PLL interpolation cleanly
                        tr.continuousBypass = 0u;
                        tr.discreteStreak = 0u;
                        tr.bypassOffX = tr.bypassOffY = tr.bypassOffZ = 0.0f;
                        tr.period = nomPeriod;
                        tr.curDelay = nomPeriod * 1.18;
                        tr.warmupTicks = 0u;
                        tr.settled = 1u;
                    }
                }
            }

            if (tr.continuousBypass != 0u) {
                tr.lastRawX = rawX;
                tr.lastRawY = rawY;
                tr.lastRawZ = rawZ;
            } else {
                const float stepDist = std::sqrt(distSq);
                int coalescedK = 1;
                if (!tr.settled && tr.avgStep > 1e-3f &&
                    stepDist > tr.avgStep * 1.65f && stepDist < tr.avgStep * 3.60f &&
                    elapsed >= tr.period * 1.30) {
                    coalescedK = std::clamp(static_cast<int>(std::lround(stepDist / tr.avgStep)), 2, 3);
                } else {
                    tr.avgStep = (tr.avgStep <= 1e-3f) ? stepDist : (tr.avgStep * 0.85f + stepDist * 0.15f);
                }

                // Symmetric bounds [4ms .. 95ms] so sub-15ms updates never bias period above arrival rate!
                const double normElapsed = elapsed / static_cast<double>(coalescedK);
                if (normElapsed >= 0.003 && normElapsed <= 0.120) {
                    const bool inWarmup = (tr.warmupTicks < 6u);
                    if (inWarmup) {
                        ++tr.warmupTicks;
                        tr.period = std::clamp(tr.period * 0.50 + normElapsed * 0.50, 0.004, 0.095);
                        if (tr.warmupTicks >= 2u) {
                            const double dev = std::min(std::fabs(normElapsed - tr.period), 0.050);
                            tr.jitter = tr.jitter * 0.80 + dev * 0.20;
                        }
                    } else {
                        const double dev = std::min(std::fabs(normElapsed - tr.period), 0.050);
                        tr.jitter = tr.jitter * 0.88 + dev * 0.12;
                        tr.period = std::clamp(tr.period * 0.92 + normElapsed * 0.08, 0.004, 0.095);
                    }
                }

                if (tr.settled || tr.count == 0u) {
                    // Smooth immediate start from rest: anchor synthetic depart sample 1 tick back
                    const double tPrev = nowSec - tr.period * static_cast<double>(coalescedK);
                    tr.ResetSnaps(tPrev, tr.lastRawX, tr.lastRawY, tr.lastRawZ);
                    tr.PushSnap(nowSec, rawX, rawY, rawZ);
                    tr.lastPlaySec = tPrev;
                } else {
                    const double lastT = tr.Back().t;
                    const double tPred = lastT + tr.period * static_cast<double>(coalescedK);
                    const double err   = nowSec - tPred;
                    double tSnap = nowSec;
                    if (std::fabs(err) <= tr.period * 1.40) {
                        // Normal cadence / 200Hz quantization / UDP jitter -> gentle 5% PLL pull!
                        tSnap = tPred + 0.05 * err;
                    } else if (err > tr.period * 1.40 && err <= tr.period * 4.0) {
                        // Brief packet gap (1..4 dropped ticks) -> preserve phase cleanly
                        const int k = std::max(coalescedK, static_cast<int>(std::lround((nowSec - lastT) / tr.period)));
                        tSnap = lastT + static_cast<double>(k) * tr.period +
                                0.08 * ((nowSec - lastT) - static_cast<double>(k) * tr.period);
                    } else {
                        tSnap = nowSec;
                    }
                    // Prevent PLL timestamp runaway into the future when updates arrive faster than tr.period
                    const double maxFutureT = nowSec + tr.period * 0.65;
                    if (tSnap > maxFutureT) {
                        tSnap = maxFutureT;
                    }
                    if (tSnap <= lastT + 0.001) {
                        tSnap = lastT + 0.001;
                    }
                    tr.PushSnap(tSnap, rawX, rawY, rawZ);
                }

                tr.lastRawX = rawX;
                tr.lastRawY = rawY;
                tr.lastRawZ = rawZ;
                tr.settled  = 0u;
            }
        }

        if (tr.continuousBypass != 0u) {
            if (outStats) outStats->bypassed = true;
            const float offSq = tr.bypassOffX * tr.bypassOffX +
                                tr.bypassOffY * tr.bypassOffY +
                                tr.bypassOffZ * tr.bypassOffZ;
            if (offSq > 1e-10f) {
                const float decay = static_cast<float>(std::exp(-dt * 95.0));
                tr.bypassOffX *= decay;
                tr.bypassOffY *= decay;
                tr.bypassOffZ *= decay;
                const float remSq = tr.bypassOffX * tr.bypassOffX +
                                    tr.bypassOffY * tr.bypassOffY +
                                    tr.bypassOffZ * tr.bypassOffZ;
                if (remSq <= 1e-10f) {
                    snapToRaw();
                    return false;
                }
                tr.rendX = rawX + tr.bypassOffX;
                tr.rendY = rawY + tr.bypassOffY;
                tr.rendZ = rawZ + tr.bypassOffZ;
                tr.settled = 0u;
                outX = tr.rendX;
                outY = tr.rendY;
                outZ = tr.rendZ;
                return true;
            }
            snapToRaw();
            return false;
        }

        if (tr.settled || tr.count == 0u) {
            return false;
        }

        // Smoothly slewed interpolation delay (1.15x tick + online jitter cushion + half-frame quant margin)
        double jitPad = 0.0;
        if (optDeJitter && tr.jitter > 0.0015) {
            jitPad = std::min((tr.jitter - 0.0015) * 2.0, tr.period * 0.85);
            if (outStats) {
                outStats->deJittered = true;
                outStats->jitterUs   = static_cast<uint32_t>(tr.jitter * 1000000.0);
            }
        }
        const double targetDelay = std::clamp(tr.period * 1.15 + jitPad + dt * 0.5, std::max(0.006, dt * 1.15), 0.150);
        const double maxSlew     = dt * ((tr.warmupTicks < 6u) ? 0.35 : 0.08);
        tr.curDelay += std::clamp(targetDelay - tr.curDelay, -maxSlew, maxSlew);

        // Strictly monotonic virtual playback clock (immune to backward steps)
        const double desiredPlay = nowSec - tr.curDelay;
        double playT = tr.lastPlaySec + dt;
        const double playErr = desiredPlay - playT;
        if (std::fabs(playErr) > tr.period * 2.5) {
            playT = desiredPlay;
        } else {
            playT += playErr * 0.05;
        }
        if (playT < tr.lastPlaySec) {
            playT = tr.lastPlaySec;
        }
        tr.lastPlaySec = playT;

        const SnapPoint& front = tr.Front();
        const SnapPoint& back  = tr.Back();
        if (playT <= front.t) {
            tr.rendX = front.x;
            tr.rendY = front.y;
            tr.rendZ = front.z;
        } else if (playT >= back.t) {
            tr.rendX = back.x;
            tr.rendY = back.y;
            tr.rendZ = back.z;
            if ((nowSec - tr.lastArrSec) > tr.period * 1.8) {
                tr.settled = 1u;
                outX = tr.rendX;
                outY = tr.rendY;
                outZ = tr.rendZ;
                return false; // Exact resting coordinate reached -> zero drift!
            }
        } else {
            // Find bracketing snapshot segment [s0, s1]
            for (uint8_t i = 0u; i + 1u < tr.count; ++i) {
                const SnapPoint& s0 = tr.At(i);
                const SnapPoint& s1 = tr.At(static_cast<uint8_t>(i + 1u));
                if (playT >= s0.t && playT <= s1.t) {
                    const double span = s1.t - s0.t;
                    const float u = (span > 1e-6)
                        ? static_cast<float>(std::clamp((playT - s0.t) / span, 0.0, 1.0))
                        : 1.0f;
                    tr.rendX = s0.x + (s1.x - s0.x) * u;
                    tr.rendY = s0.y + (s1.y - s0.y) * u;
                    tr.rendZ = s0.z + (s1.z - s0.z) * u;
                    break;
                }
            }
        }

        outX = tr.rendX;
        outY = tr.rendY;
        outZ = tr.rendZ;
        if (outStats) outStats->interpolated = true;
        return true;
    }

private:
    static inline size_t HashKey(uintptr_t key) noexcept {
        return ((key >> 4) ^ (key >> 15) ^ (key >> 27)) & (kSlots - 1u);
    }

    std::vector<Track> tracks_;
};

} // namespace hs_world_pos
