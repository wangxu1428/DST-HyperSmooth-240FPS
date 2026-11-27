#pragma once

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cwchar>

namespace hs_hw_tier {

enum class HardwareTier : int {
    Low       = 0,
    Balanced  = 1,
    Unleashed = 2,
};

struct HardwareCaps {
    uint32_t logicalCores       = 0;
    uint64_t totalRamMb         = 0;
    uint64_t availRamMb         = 0;
    int      glMajor            = 0;
    int      glMinor            = 0;
    bool     hasBufferStorage   = false;
    bool     hasSsbo            = false;
    bool     hasBindlessTexture = false;
    bool     hasBaseVertex      = false;
    bool     isNativeGl         = false;
};

struct TierBudget {
    size_t   vbPoolMaxBytes         = 64u * 1024u * 1024u;
    size_t   vbPoolMaxCount         = 1024u;
    uint64_t vbPoolIdleEvictFrames  = 1800u;
    size_t   spriteShadowCacheBytes = 64u * 1024u * 1024u;
    double   animShedL1Us           = 2200.0;
    double   animShedL2Us           = 4000.0;
    uint32_t animShedL1Ents         = 900u;
    uint32_t animShedL2Ents         = 1800u;
    double   texSliceBudgetUs       = 1200.0;
    int      gcHeadroomMinKb        = 262144;   // 256 MB
    int      gcHeadroomMaxKb        = 0x320000; // ~3276 MB
    float    gcBusyCapShortSec      = 0.00030f;
    int      gcMaxDeferStreak       = 1;
    bool     prewarmMods            = false;
    bool     disableEcoQos          = true;
    bool     highPriority           = false;
    bool     persistentSpriteRing   = true;
};

inline HardwareCaps ProbeHostCaps() noexcept {
    HardwareCaps caps{};
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    caps.logicalCores = (si.dwNumberOfProcessors > 0) ? static_cast<uint32_t>(si.dwNumberOfProcessors) : 1u;

    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        caps.totalRamMb = static_cast<uint64_t>(ms.ullTotalPhys / (1024ULL * 1024ULL));
        caps.availRamMb = static_cast<uint64_t>(ms.ullAvailPhys / (1024ULL * 1024ULL));
    }
    return caps;
}

inline HardwareTier ParseTier(const wchar_t* s) noexcept {
    if (!s || !*s) return HardwareTier::Balanced;
    wchar_t buf[32]{};
    size_t j = 0;
    for (size_t i = 0; s[i] && j < 31; ++i) {
        wchar_t c = s[i];
        if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') continue;
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');
        buf[j++] = c;
    }
    buf[j] = L'\0';
    if (wcscmp(buf, L"low") == 0 || wcscmp(buf, L"0") == 0) {
        return HardwareTier::Low;
    }
    if (wcscmp(buf, L"unleashed") == 0 || wcscmp(buf, L"high") == 0 || wcscmp(buf, L"2") == 0) {
        return HardwareTier::Unleashed;
    }
    return HardwareTier::Balanced;
}

inline const char* TierName(HardwareTier t) noexcept {
    switch (t) {
        case HardwareTier::Low:       return "low";
        case HardwareTier::Unleashed: return "unleashed";
        default:                      return "balanced";
    }
}

inline const wchar_t* TierNameW(HardwareTier t) noexcept {
    switch (t) {
        case HardwareTier::Low:       return L"low";
        case HardwareTier::Unleashed: return L"unleashed";
        default:                      return L"balanced";
    }
}

inline TierBudget ComputeTierBudget(HardwareTier tier, const HardwareCaps& caps) noexcept {
    TierBudget b{};
    switch (tier) {
        case HardwareTier::Low:
            b.vbPoolMaxBytes         = 32u * 1024u * 1024u;
            b.vbPoolMaxCount         = 512u;
            b.vbPoolIdleEvictFrames  = 600u;
            b.spriteShadowCacheBytes = 32u * 1024u * 1024u;
            b.animShedL1Us           = 1400.0;
            b.animShedL2Us           = 2600.0;
            b.animShedL1Ents         = 500u;
            b.animShedL2Ents         = 1000u;
            b.texSliceBudgetUs       = 800.0;
            b.gcHeadroomMinKb        = 131072;   // 128 MB
            b.gcHeadroomMaxKb        = 1048576;  // 1024 MB
            b.gcBusyCapShortSec      = 0.00035f;
            b.gcMaxDeferStreak       = 1;
            b.prewarmMods            = false;
            b.disableEcoQos          = false;
            b.highPriority           = false;
            b.persistentSpriteRing   = false;
            break;

        case HardwareTier::Unleashed:
            b.vbPoolMaxBytes         = 256u * 1024u * 1024u;
            b.vbPoolMaxCount         = 4096u;
            b.vbPoolIdleEvictFrames  = 7200u;
            b.spriteShadowCacheBytes = 256u * 1024u * 1024u;
            b.animShedL1Us           = 6000.0;
            b.animShedL2Us           = 12000.0;
            b.animShedL1Ents         = 3500u;
            b.animShedL2Ents         = 7000u;
            b.texSliceBudgetUs       = 1200.0;
            b.gcHeadroomMinKb        = 524288;   // 512 MB
            b.gcHeadroomMaxKb        = 4194304;  // 4096 MB
            b.gcBusyCapShortSec      = 0.00020f;
            b.gcMaxDeferStreak       = 2;
            b.prewarmMods            = (caps.totalRamMb >= 12288ULL);
            b.disableEcoQos          = true;
            b.highPriority           = true;
            b.persistentSpriteRing   = true;
            break;

        case HardwareTier::Balanced:
        default:
            b.vbPoolMaxBytes         = 64u * 1024u * 1024u;
            b.vbPoolMaxCount         = 1024u;
            b.vbPoolIdleEvictFrames  = 1800u;
            b.spriteShadowCacheBytes = 64u * 1024u * 1024u;
            b.animShedL1Us           = 2200.0;
            b.animShedL2Us           = 4000.0;
            b.animShedL1Ents         = 900u;
            b.animShedL2Ents         = 1800u;
            b.texSliceBudgetUs       = 1200.0;
            b.gcHeadroomMinKb        = 262144;   // 256 MB
            b.gcHeadroomMaxKb        = 0x320000; // ~3276 MB
            b.gcBusyCapShortSec      = 0.00030f;
            b.gcMaxDeferStreak       = 1;
            b.prewarmMods            = false;
            b.disableEcoQos          = true;
            b.highPriority           = false;
            b.persistentSpriteRing   = true;
            break;
    }

    // Low-RAM safety clamp: even if an <10GB PC selects Unleashed, bound caches to avoid OOM/paging
    if (caps.totalRamMb > 0ULL && caps.totalRamMb < 10240ULL) {
        b.vbPoolMaxBytes         = std::min<size_t>(b.vbPoolMaxBytes, 64u * 1024u * 1024u);
        b.spriteShadowCacheBytes = std::min<size_t>(b.spriteShadowCacheBytes, 64u * 1024u * 1024u);
        b.gcHeadroomMinKb        = std::min<int>(b.gcHeadroomMinKb, 262144);
        b.gcHeadroomMaxKb        = std::min<int>(b.gcHeadroomMaxKb, 0x320000);
        b.prewarmMods            = false;
    }
    return b;
}

inline bool ApplyWin11EcoQosDisable(bool disable) noexcept {
    // Win11 ProcessPowerThrottling (4) via kernel32!SetProcessInformation (safe no-op on Win7/Win10)
    struct HsPowerThrottlingState {
        ULONG Version;
        ULONG ControlMask;
        ULONG StateMask;
    };
    using SetProcessInformation_t = BOOL (WINAPI*)(HANDLE, int, void*, DWORD);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) return false;
    auto fn = reinterpret_cast<SetProcessInformation_t>(
        reinterpret_cast<void*>(GetProcAddress(k32, "SetProcessInformation")));
    if (!fn) return false;

    HsPowerThrottlingState state{};
    state.Version     = 1u;
    state.ControlMask = 1u; // PROCESS_POWER_THROTTLING_EXECUTION_SPEED
    state.StateMask   = disable ? 0u : 1u;
    return fn(GetCurrentProcess(), 4 /*ProcessPowerThrottling*/, &state, sizeof(state)) != FALSE;
}

} // namespace hs_hw_tier
