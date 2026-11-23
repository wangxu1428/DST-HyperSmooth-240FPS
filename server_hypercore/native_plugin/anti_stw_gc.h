#pragma once

#include <algorithm>
#include <cstdint>

namespace anti_stw_gc {

struct Config {
    float gc_spare_target_ms = 28.0f;
    float gc_calm_max_ms     = 4.0f;
    float gc_busy_min_ms     = 0.4f;
    int   gc_emergency_add_mb = 256;
    int   gc_hard_abs_mb      = 0; // 0 = auto-anchored from post-warmup baseline (max(4x, +1024MB, >=512MB))
    bool  enable_anti_stw_gc  = true;
};

struct Decision {
    bool    run_hard_full_gc = false;
    float   step_budget_ms   = 0.55f;
    int32_t new_threshold_kb = 0;
    bool    in_catchup_mode  = false;
};

// Pure deterministic state machine for Server Adaptive + Anti-STW GC.
// Eliminates startup population false Full-GC (329ms+) and smoothly drains
// allocation spikes via capped incremental steps while keeping a hard OOM
// circuit-breaker (both floating AND absolute post-warmup ceiling) so sustained
// memory leaks fire the breaker even when incremental cycles keep completing.
struct State {
    void*    last_vm            = nullptr;
    int32_t  live_set_kb        = 0;
    int32_t  anchor_live_kb     = 0;
    int32_t  last_hard_cliff_kb = 0;
    uint32_t vm_ticks           = 0;
    uint32_t forced_catchup     = 0;
    uint32_t over_cliff_ticks   = 0;

    static constexpr uint32_t kWarmupTicks        = 120;
    static constexpr uint32_t kOomConfirmTicks    = 120;

    bool IsPastWarmup() const noexcept {
        return vm_ticks > kWarmupTicks;
    }

    void ResetForVm(void* vm, int32_t curKb) {
        last_vm            = vm;
        live_set_kb        = (curKb > 0) ? curKb : 0;
        anchor_live_kb     = live_set_kb;
        last_hard_cliff_kb = 0;
        vm_ticks           = 0;
        forced_catchup     = 0;
        over_cliff_ticks   = 0;
    }

    void ArmForcedCatchup(uint32_t ticks = 90) {
        if (ticks > forced_catchup) forced_catchup = ticks;
    }

    void OnCycleCompleted(int32_t postKb) {
        if (postKb > 0) {
            live_set_kb = postKb;
            if (postKb < anchor_live_kb || anchor_live_kb <= 0) {
                anchor_live_kb = postKb;
            }
        }
        forced_catchup = 0;
        // CRITICAL OOM BREAKER INVARIANT: NEVER reset over_cliff_ticks if heap is STILL
        // breaching the absolute/hard ceiling after an incremental cycle completes!
        if (last_hard_cliff_kb <= 0 || postKb <= last_hard_cliff_kb) {
            over_cliff_ticks = 0;
        }
    }

    void OnHardFullGcCompleted(int32_t postKb) {
        if (postKb > 0) {
            live_set_kb = postKb;
            if (postKb < anchor_live_kb || anchor_live_kb <= 0) {
                anchor_live_kb = postKb;
            }
        }
        forced_catchup   = 0;
        over_cliff_ticks = 0;
    }

    Decision Evaluate(void* vm, int32_t curKb, int32_t curThresholdKb,
                      double nonGcMs, bool is60Hz, const Config& cfg) {
        Decision d{};
        if (vm != last_vm || live_set_kb <= 0) {
            ResetForVm(vm, curKb);
        }
        ++vm_ticks;

        // During initial world population warmup, track rising working set so
        // loading 18k+ entities never triggers a spurious 300ms+ startup Full-GC.
        if (vm_ticks <= kWarmupTicks) {
            if (curKb > live_set_kb) live_set_kb = curKb;
            if (live_set_kb > anchor_live_kb) anchor_live_kb = live_set_kb;
        } else if (curKb > 0 && curKb < live_set_kb) {
            live_set_kb = curKb;
            if (curKb < anchor_live_kb) anchor_live_kb = curKb;
        }

        const int32_t addKb = std::max(64, cfg.gc_emergency_add_mb) * 1024;
        const int32_t softFloorKb = std::max<int32_t>(live_set_kb * 2, live_set_kb + addKb);
        const int32_t floatingCliffKb = std::max<int32_t>(
            live_set_kb * 3,
            live_set_kb + std::max<int32_t>(768 * 1024, addKb * 2));
        const int32_t absCliffKb = (cfg.gc_hard_abs_mb > 0)
            ? (cfg.gc_hard_abs_mb * 1024)
            : std::max<int32_t>(
                  512 * 1024,
                  std::max<int32_t>(anchor_live_kb * 4, anchor_live_kb + 1024 * 1024));
        const int32_t hardCliffKb = std::min(floatingCliffKb, absCliffKb);
        last_hard_cliff_kb = hardCliffKb;

        d.new_threshold_kb = (curThresholdKb < softFloorKb) ? softFloorKb : curThresholdKb;

        const bool pastWarmup = (vm_ticks > kWarmupTicks);
        const bool overSoft   = pastWarmup && (curKb > d.new_threshold_kb);
        const bool overCliff  = pastWarmup && (curKb > hardCliffKb);

        if (!cfg.enable_anti_stw_gc) {
            // Legacy behavior when anti-STW shield is explicitly disabled
            if (curThresholdKb >= softFloorKb && curKb > curThresholdKb) {
                d.run_hard_full_gc = true;
            }
        } else {
            if (overSoft) {
                if (forced_catchup < 30) forced_catchup = 30;
            }
            if (overCliff) {
                ++over_cliff_ticks;
                if (over_cliff_ticks >= kOomConfirmTicks) {
                    d.run_hard_full_gc = true;
                    over_cliff_ticks = 0;
                }
            } else {
                over_cliff_ticks = 0;
            }
        }

        const double effectiveTargetMs = is60Hz
            ? std::min<double>(13.5, static_cast<double>(cfg.gc_spare_target_ms) * 0.5)
            : static_cast<double>(cfg.gc_spare_target_ms);
        const double spareMs = std::max(0.0, effectiveTargetMs - nonGcMs);
        const double excessMb = (curKb > live_set_kb)
            ? (static_cast<double>(curKb - live_set_kb) / 1024.0)
            : 0.0;

        const float idleBaseMs = is60Hz ? 0.30f : 0.55f;
        const float busyMinMs  = is60Hz ? std::min(cfg.gc_busy_min_ms, 0.25f) : cfg.gc_busy_min_ms;
        float budgetMs = idleBaseMs;

        if (forced_catchup > 0 && cfg.enable_anti_stw_gc) {
            --forced_catchup;
            d.in_catchup_mode = true;
            // Smoothly drain backlog across ticks using available headroom without exceeding tick budget
            const double maxDrainMs = is60Hz
                ? std::min<double>(3.0, std::max<double>(cfg.gc_calm_max_ms * 0.5, 1.8))
                : std::max<double>(cfg.gc_calm_max_ms, 5.5);
            const double share = is60Hz ? 0.40 : 0.45;
            if (spareMs < (is60Hz ? 2.0 : 3.5)) {
                budgetMs = busyMinMs;
            } else {
                budgetMs = static_cast<float>(std::clamp(spareMs * share, static_cast<double>(busyMinMs), maxDrainMs));
            }
        } else if (spareMs < (is60Hz ? 2.5 : 4.0)) {
            budgetMs = busyMinMs;
        } else if (excessMb >= 96.0) {
            const double allowed = std::min<double>(cfg.gc_calm_max_ms, std::max(is60Hz ? 0.6 : 1.0, spareMs * 0.25));
            budgetMs = static_cast<float>(std::min(is60Hz ? 1.5 : 2.5, allowed));
        } else if (excessMb >= 32.0) {
            const double frac = (excessMb - 32.0) / 64.0;
            const double want = (is60Hz ? 0.45 : 0.85) + frac * (is60Hz ? 0.65 : 1.15);
            const double capBySpare = std::max<double>(busyMinMs, spareMs * 0.20);
            budgetMs = static_cast<float>(std::min(want, capBySpare));
        } else if (nonGcMs >= (is60Hz ? 9.5 : 18.0)) {
            budgetMs = busyMinMs;
        } else {
            budgetMs = idleBaseMs;
        }

        d.step_budget_ms = budgetMs;
        return d;
    }
};

} // namespace anti_stw_gc
