#pragma once
// =============================================================================
// HyperSmooth v3.5 — Global Elastic Hardware Resource Pool
// =============================================================================
// Dynamically scales CPU worker parallelism, host RAM geometry/shadow caches,
// Lua GC headroom, and persistent GPU ring buffers (VRAM + Bindless/SSBO)
// according to live system RAM headroom, DXGI VRAM pressure, core count,
// OpenGL 4.6 capabilities, and selected hardware tier.
// =============================================================================

#include <windows.h>

#include <algorithm>
#include <cstdint>

#include "hardware_tier.h"

namespace hs_elastic_pool {

inline constexpr uint8_t kPressureAbundant = 0u;
inline constexpr uint8_t kPressureModerate = 1u;
inline constexpr uint8_t kPressureTight    = 2u;

struct ElasticHostSnapshot {
    hs_hw_tier::HardwareTier tier               = hs_hw_tier::HardwareTier::Unleashed;
    uint32_t                 logicalCores       = 1u;
    uint64_t                 totalRamMb         = 0ULL;
    uint64_t                 availRamMb         = 0ULL;
    float                    vramUsedMb         = 0.0f;
    float                    vramBudgetMb       = 0.0f;
    bool                     isNativeGl         = false;
    bool                     hasBufferStorage   = false;
    bool                     hasSsbo            = false;
    bool                     hasBindlessTexture = false;
    bool                     hasBaseVertex      = false;
    uint32_t                 lastAnimEnts       = 0u;
};

struct ElasticPoolPlan {
    uint8_t  pressureLevel             = kPressureAbundant;
    uint32_t maxSkeletalWorkers        = 1u;
    size_t   vbPoolMaxBytes            = 64u * 1024u * 1024u;
    size_t   vbPoolMaxCount            = 1024u;
    uint64_t vbPoolIdleEvictFrames     = 1800u;
    size_t   spriteShadowCacheBytes    = 64u * 1024u * 1024u;
    int      gcHeadroomMinKb           = 262144;
    int      gcHeadroomMaxKb           = 0x320000;
    uint32_t gpuRingMaxVertsPerFrame   = 65536u;
    uint32_t gpuRingMaxIndicesPerFrame = 131072u;
    uint32_t gpuRingMaxInstsPerFrame   = 8192u;
    bool     enableGpuDrivenBindless   = false;
};

inline uint64_t QueryLiveAvailRamMb() noexcept {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return 0ULL;
    return static_cast<uint64_t>(ms.ullAvailPhys / (1024ULL * 1024ULL));
}

inline ElasticPoolPlan ComputeElasticPlan(const ElasticHostSnapshot& snap) noexcept {
    ElasticPoolPlan p{};

    const bool ramTight    = (snap.availRamMb > 0ULL && snap.availRamMb < 4096ULL);
    const bool vramTight   = (snap.vramBudgetMb > 256.0f && snap.vramUsedMb > snap.vramBudgetMb * 0.78f);
    const bool ramMod      = (snap.availRamMb > 0ULL && snap.availRamMb < 8192ULL);
    const bool vramMod     = (snap.vramBudgetMb > 256.0f && snap.vramUsedMb > snap.vramBudgetMb * 0.62f);

    if (ramTight || vramTight) {
        p.pressureLevel = kPressureTight;
    } else if (ramMod || vramMod) {
        p.pressureLevel = kPressureModerate;
    } else {
        p.pressureLevel = kPressureAbundant;
    }

    // CPU elastic worker ceiling
    uint32_t w = 1u;
    if      (snap.logicalCores >= 16u) w = 12u;
    else if (snap.logicalCores >= 12u) w = 8u;
    else if (snap.logicalCores >= 6u)  w = 6u;
    else if (snap.logicalCores >= 4u)  w = 2u;
    if (snap.tier == hs_hw_tier::HardwareTier::Low && w > 4u) {
        w = 4u;
    }
    p.maxSkeletalWorkers = w;

    // Host RAM geometry / shadow caches + Lua GC elastic headroom
    const bool highRam16 = (snap.totalRamMb == 0ULL || snap.totalRamMb >= 16384ULL) &&
                           (snap.availRamMb == 0ULL || snap.availRamMb >= 8192ULL);
    const bool highRam24 = (snap.totalRamMb >= 24576ULL) &&
                           (snap.availRamMb == 0ULL || snap.availRamMb >= 12288ULL);

    switch (snap.tier) {
        case hs_hw_tier::HardwareTier::Low:
            p.vbPoolMaxBytes         = 32u * 1024u * 1024u;
            p.vbPoolMaxCount         = 512u;
            p.vbPoolIdleEvictFrames  = 600u;
            p.spriteShadowCacheBytes = 32u * 1024u * 1024u;
            p.gcHeadroomMinKb        = 131072;
            p.gcHeadroomMaxKb        = 1048576;
            break;

        case hs_hw_tier::HardwareTier::Unleashed:
            if (p.pressureLevel == kPressureAbundant && highRam16) {
                p.vbPoolMaxBytes         = 512u * 1024u * 1024u;
                p.vbPoolMaxCount         = 8192u;
                p.vbPoolIdleEvictFrames  = 14400u;
                p.spriteShadowCacheBytes = 512u * 1024u * 1024u;
            } else if (p.pressureLevel <= kPressureModerate) {
                p.vbPoolMaxBytes         = 256u * 1024u * 1024u;
                p.vbPoolMaxCount         = 4096u;
                p.vbPoolIdleEvictFrames  = 7200u;
                p.spriteShadowCacheBytes = 256u * 1024u * 1024u;
            } else {
                p.vbPoolMaxBytes         = 64u * 1024u * 1024u;
                p.vbPoolMaxCount         = 1024u;
                p.vbPoolIdleEvictFrames  = 1800u;
                p.spriteShadowCacheBytes = 64u * 1024u * 1024u;
            }
            if (p.pressureLevel == kPressureAbundant && highRam24) {
                p.gcHeadroomMinKb = 786432;   // 768 MB
                p.gcHeadroomMaxKb = 6291456;  // 6144 MB
            } else if (p.pressureLevel <= kPressureModerate) {
                p.gcHeadroomMinKb = 524288;   // 512 MB
                p.gcHeadroomMaxKb = 4194304;  // 4096 MB
            } else {
                p.gcHeadroomMinKb = 262144;   // 256 MB
                p.gcHeadroomMaxKb = 2097152;  // 2048 MB
            }
            break;

        case hs_hw_tier::HardwareTier::Balanced:
        default:
            p.vbPoolMaxBytes         = 64u * 1024u * 1024u;
            p.vbPoolMaxCount         = 1024u;
            p.vbPoolIdleEvictFrames  = 1800u;
            p.spriteShadowCacheBytes = 64u * 1024u * 1024u;
            if (p.pressureLevel == kPressureTight) {
                p.gcHeadroomMinKb = 131072;
                p.gcHeadroomMaxKb = 1572864;
            } else {
                p.gcHeadroomMinKb = 262144;
                p.gcHeadroomMaxKb = 0x320000;
            }
            break;
    }

    // Low-RAM host safety clamp (<10 GB total physical RAM)
    if (snap.totalRamMb > 0ULL && snap.totalRamMb < 10240ULL) {
        p.vbPoolMaxBytes         = std::min<size_t>(p.vbPoolMaxBytes, 64u * 1024u * 1024u);
        p.vbPoolMaxCount         = std::min<size_t>(p.vbPoolMaxCount, 1024u);
        p.vbPoolIdleEvictFrames  = std::min<uint64_t>(p.vbPoolIdleEvictFrames, 1800u);
        p.spriteShadowCacheBytes = std::min<size_t>(p.spriteShadowCacheBytes, 64u * 1024u * 1024u);
        p.gcHeadroomMinKb        = std::min<int>(p.gcHeadroomMinKb, 262144);
        p.gcHeadroomMaxKb        = std::min<int>(p.gcHeadroomMaxKb, 0x320000);
    }

    // GPU Persistent Ring Budgets (triple-buffered AZDO VBO/IBO/SSBO per-frame capacities)
    // Mega-bases with 1,400+ entities / 20,000+ bones emit 400K-900K+ sprite vertices/frame;
    // on Unleashed (8GB+ GPU, Abundant VRAM) allocate 1M verts / 2M indices / 64K insts (~100MB VRAM)
    // so GPU-Driven Bindless+SSBO NEVER overflows into slow UploadRingSlice fallback!
    if (snap.tier == hs_hw_tier::HardwareTier::Low || p.pressureLevel == kPressureTight) {
        p.gpuRingMaxVertsPerFrame   = 65536u;
        p.gpuRingMaxIndicesPerFrame = 131072u;
        p.gpuRingMaxInstsPerFrame   = 8192u;
    } else if (snap.tier == hs_hw_tier::HardwareTier::Unleashed && p.pressureLevel == kPressureAbundant) {
        p.gpuRingMaxVertsPerFrame   = 1048576u;
        p.gpuRingMaxIndicesPerFrame = 2097152u;
        p.gpuRingMaxInstsPerFrame   = 65536u;
    } else {
        p.gpuRingMaxVertsPerFrame   = 262144u;
        p.gpuRingMaxIndicesPerFrame = 524288u;
        p.gpuRingMaxInstsPerFrame   = 32768u;
    }

    // GPU-Driven Bindless Texture + Persistent SSBO gating (NVIDIA / Desktop GL 4.6)
    p.enableGpuDrivenBindless = snap.isNativeGl &&
                                snap.hasBufferStorage &&
                                snap.hasSsbo &&
                                snap.hasBindlessTexture &&
                                snap.hasBaseVertex &&
                                (snap.tier != hs_hw_tier::HardwareTier::Low);

    return p;
}

} // namespace hs_elastic_pool
