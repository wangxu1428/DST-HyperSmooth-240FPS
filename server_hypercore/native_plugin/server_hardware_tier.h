#pragma once
// =============================================================================
// DST HyperCore Server — Tiered Hardware Liberation & Shard P-Core Topology
// =============================================================================
// Hermetic header (ZERO STL containers/strings, ZERO exceptions, GLIBC <= 2.17):
//   - 3 user-selectable server hardware tiers:
//       0 = Low       (省电低配 · 低占用小内存保守策略)
//       1 = Balanced  (均衡标准 · 默认推荐配置)
//       2 = Unleashed (高端解放 · 大内存高核数全功率释放)
//   - Probes logical CPUs, physical P-cores (EfficiencyClass on hybrid CPUs),
//     and host RAM.
//   - Maps Master / Caves shards onto distinct non-Core-0 physical P-cores via
//     soft ideal-processor hint (never hard-pins worker threads).
//   - Disables Windows 11 EcoQoS throttling for Balanced / Unleashed tiers.
// =============================================================================

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace hc_server_tier {

enum class ServerTier : int {
    Low       = 0,
    Balanced  = 1,
    Unleashed = 2,
};

inline ServerTier NormalizeTier(int raw) noexcept {
    if (raw <= 0) return ServerTier::Low;
    if (raw >= 2) return ServerTier::Unleashed;
    return ServerTier::Balanced;
}

inline ServerTier ParseTier(const char* s) noexcept {
    if (!s || !*s) return ServerTier::Balanced;
    while (*s == ' ' || *s == '\t') ++s;
    if (std::isdigit(static_cast<unsigned char>(*s))) {
        return NormalizeTier(std::atoi(s));
    }
    char buf[32]{};
    size_t n = 0;
    while (s[n] && n + 1 < sizeof(buf)) {
        const char c = s[n];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '#' || c == ';') break;
        buf[n++] = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    buf[n] = '\0';
    if (std::strcmp(buf, "low") == 0 || std::strcmp(buf, "eco") == 0) {
        return ServerTier::Low;
    }
    if (std::strcmp(buf, "unleashed") == 0 || std::strcmp(buf, "high") == 0 || std::strcmp(buf, "max") == 0) {
        return ServerTier::Unleashed;
    }
    return ServerTier::Balanced;
}

inline const char* TierName(ServerTier t) noexcept {
    switch (t) {
        case ServerTier::Low:       return "low";
        case ServerTier::Unleashed: return "unleashed";
        case ServerTier::Balanced:
        default:                    return "balanced";
    }
}

struct HostCaps {
    uint32_t logicalCores  = 4;
    uint32_t perfCoreCount = 0;
    uint32_t perfCoreIds[64] = {};
    uint64_t totalRamMb    = 0;
};

inline HostCaps ProbeHostCaps() noexcept {
    HostCaps caps{};
#ifdef _WIN32
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        caps.totalRamMb = ms.ullTotalPhys / (1024ULL * 1024ULL);
    }
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint32_t logical = (si.dwNumberOfProcessors > 0) ? static_cast<uint32_t>(si.dwNumberOfProcessors) : 4u;
    using GetActiveProcCnt_t = DWORD (WINAPI*)(WORD);
    if (HMODULE hK32 = GetModuleHandleW(L"kernel32.dll")) {
        auto pAct = reinterpret_cast<GetActiveProcCnt_t>(GetProcAddress(hK32, "GetActiveProcessorCount"));
        if (pAct) {
            const DWORD cnt = pAct(0xFFFFu /*ALL_PROCESSOR_GROUPS*/);
            if (cnt > logical) logical = static_cast<uint32_t>(cnt);
        }
        using GetLpiEx_t = BOOL (WINAPI*)(LOGICAL_PROCESSOR_RELATIONSHIP, PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX, PDWORD);
        auto pLpiEx = reinterpret_cast<GetLpiEx_t>(GetProcAddress(hK32, "GetLogicalProcessorInformationEx"));
        if (pLpiEx) {
            DWORD bytes = 0;
            pLpiEx(RelationProcessorCore, nullptr, &bytes);
            if (bytes > 0 && bytes <= 1048576u) {
                uint8_t* raw = static_cast<uint8_t*>(std::malloc(bytes));
                if (raw) {
                    if (pLpiEx(RelationProcessorCore,
                               reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(raw), &bytes)) {
                        uint8_t maxEff = 0;
                        for (DWORD off = 0; off + sizeof(DWORD) * 2u <= bytes; ) {
                            auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(raw + off);
                            if (info->Size == 0 || off + info->Size > bytes) break;
                            if (info->Relationship == RelationProcessorCore) {
                                if (info->Processor.EfficiencyClass > maxEff) {
                                    maxEff = info->Processor.EfficiencyClass;
                                }
                            }
                            off += info->Size;
                        }
                        for (DWORD off = 0; off + sizeof(DWORD) * 2u <= bytes && caps.perfCoreCount < 64u; ) {
                            auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(raw + off);
                            if (info->Size == 0 || off + info->Size > bytes) break;
                            if (info->Relationship == RelationProcessorCore &&
                                info->Processor.EfficiencyClass == maxEff &&
                                info->Processor.GroupCount > 0) {
                                const KAFFINITY mask = info->Processor.GroupMask[0].Mask;
                                if (mask != 0) {
                                    uint32_t bit = 0;
                                    KAFFINITY m = mask;
                                    while ((m & 1u) == 0 && bit < 63u) {
                                        m >>= 1u;
                                        ++bit;
                                    }
                                    if (info->Processor.GroupMask[0].Group == 0) {
                                        caps.perfCoreIds[caps.perfCoreCount++] = bit;
                                    }
                                }
                            }
                            off += info->Size;
                        }
                    }
                    std::free(raw);
                }
            }
        }
    }
    caps.logicalCores = logical;
#else
    const long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    caps.logicalCores = (cpus > 0) ? static_cast<uint32_t>(cpus) : 4u;
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSz = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSz > 0) {
        caps.totalRamMb = (static_cast<uint64_t>(pages) * static_cast<uint64_t>(pageSz)) / (1024ULL * 1024ULL);
    }
#endif
    if (caps.perfCoreCount == 0) {
        const uint32_t n = std::min<uint32_t>(caps.logicalCores, 64u);
        for (uint32_t i = 0; i < n; ++i) {
            caps.perfCoreIds[i] = i;
        }
        caps.perfCoreCount = n;
    }
    return caps;
}

struct ServerTierBudget {
    int32_t  miPurgeDelayMs         = 500;
    bool     miEagerCommit          = true;
    bool     miPurgeDecommits       = true;
    float    gcSpareTargetMs        = 28.0f;
    float    gcCalmMaxMs            = 4.0f;
    float    gcBusyMinMs            = 0.35f;
    int      gcEmergencyAddMb       = 256;
    float    gcExcessRampMb         = 32.0f;
    int      libdeflateLevel        = 5;
    uint32_t fastDumpMaxNodes       = 4000000u;
    size_t   fastDumpKeepBytes      = 16u * 1024u * 1024u;
    size_t   asyncSaveMaxQueueBytes = 64u * 1024u * 1024u;
    size_t   asyncSaveKeepEncBytes  = 8u * 1024u * 1024u;
    size_t   astarWarmPoolCap       = 64u;
    uint64_t fsNegTtlSec            = 30ULL;
    uint32_t replicaConstructBudget = 96u;
    bool     disableEcoQos          = true;
    bool     highPriority           = false;
    bool     aboveNormalPriority    = true;
    bool     timerBoost1ms          = true;
    bool     pinShardIdealCore      = false;
    bool     prewarmBundles         = false;
};

inline ServerTierBudget ComputeServerTierBudget(ServerTier tier, const HostCaps& caps) noexcept {
    ServerTierBudget b{};
    switch (tier) {
        case ServerTier::Low:
            b.miPurgeDelayMs         = 50;
            b.miEagerCommit          = false;
            b.miPurgeDecommits       = true;
            b.gcSpareTargetMs        = 26.0f;
            b.gcCalmMaxMs            = 3.0f;
            b.gcBusyMinMs            = 0.50f;
            b.gcEmergencyAddMb       = 128;
            b.gcExcessRampMb         = 32.0f;
            b.libdeflateLevel        = 6;
            b.fastDumpMaxNodes       = 2500000u;
            b.fastDumpKeepBytes      = 4u * 1024u * 1024u;
            b.asyncSaveMaxQueueBytes = 32u * 1024u * 1024u;
            b.asyncSaveKeepEncBytes  = 4u * 1024u * 1024u;
            b.astarWarmPoolCap       = 32u;
            b.fsNegTtlSec            = 15ULL;
            b.replicaConstructBudget = 64u;
            b.disableEcoQos          = false;
            b.highPriority           = false;
            b.aboveNormalPriority    = false;
            b.timerBoost1ms          = false;
            b.pinShardIdealCore      = false;
            b.prewarmBundles         = false;
            break;

        case ServerTier::Unleashed:
            b.miPurgeDelayMs         = 5000;
            b.miEagerCommit          = true;
            b.miPurgeDecommits       = false;
            b.gcSpareTargetMs        = 29.0f;
            b.gcCalmMaxMs            = 6.5f;
            b.gcBusyMinMs            = 0.15f;
            b.gcEmergencyAddMb       = 768;
            b.gcExcessRampMb         = 96.0f;
            b.libdeflateLevel        = 2;
            b.fastDumpMaxNodes       = 12000000u;
            b.fastDumpKeepBytes      = 64u * 1024u * 1024u;
            b.asyncSaveMaxQueueBytes = 256u * 1024u * 1024u;
            b.asyncSaveKeepEncBytes  = 32u * 1024u * 1024u;
            b.astarWarmPoolCap       = 256u;
            b.fsNegTtlSec            = 120ULL;
            b.replicaConstructBudget = 128u;
            b.disableEcoQos          = true;
            b.highPriority           = true;
            b.aboveNormalPriority    = false;
            b.timerBoost1ms          = true;
            b.pinShardIdealCore      = (caps.logicalCores >= 8u);
            b.prewarmBundles         = (caps.totalRamMb >= 12288ULL);
            break;

        case ServerTier::Balanced:
        default:
            b.miPurgeDelayMs         = 500;
            b.miEagerCommit          = true;
            b.miPurgeDecommits       = true;
            b.gcSpareTargetMs        = 28.0f;
            b.gcCalmMaxMs            = 4.0f;
            b.gcBusyMinMs            = 0.35f;
            b.gcEmergencyAddMb       = 256;
            b.gcExcessRampMb         = 32.0f;
            b.libdeflateLevel        = 5;
            b.fastDumpMaxNodes       = 4000000u;
            b.fastDumpKeepBytes      = 16u * 1024u * 1024u;
            b.asyncSaveMaxQueueBytes = 64u * 1024u * 1024u;
            b.asyncSaveKeepEncBytes  = 8u * 1024u * 1024u;
            b.astarWarmPoolCap       = 64u;
            b.fsNegTtlSec            = 30ULL;
            b.replicaConstructBudget = 96u;
            b.disableEcoQos          = true;
            b.highPriority           = false;
            b.aboveNormalPriority    = true;
            b.timerBoost1ms          = true;
            b.pinShardIdealCore      = false;
            b.prewarmBundles         = false;
            break;
    }

    // Low-RAM safety clamp: on <8GB hosts, keep retained arenas/buffers bounded to prevent swap/OOM
    if (caps.totalRamMb > 0ULL && caps.totalRamMb < 8192ULL) {
        b.gcEmergencyAddMb       = std::min<int>(b.gcEmergencyAddMb, 256);
        b.fastDumpKeepBytes      = std::min<size_t>(b.fastDumpKeepBytes, 16u * 1024u * 1024u);
        b.asyncSaveMaxQueueBytes = std::min<size_t>(b.asyncSaveMaxQueueBytes, 64u * 1024u * 1024u);
        b.asyncSaveKeepEncBytes  = std::min<size_t>(b.asyncSaveKeepEncBytes, 8u * 1024u * 1024u);
        b.miPurgeDelayMs         = std::min<int32_t>(b.miPurgeDelayMs, 500);
        b.miPurgeDecommits       = true;
        b.prewarmBundles         = false;
    }
    return b;
}

inline bool EqualsAsciiInsensitive(const char* a, const char* b) noexcept {
    if (!a || !b) return false;
    while (*a && *b) {
        if (std::tolower(static_cast<unsigned char>(*a)) !=
            std::tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

inline int SelectIdealShardCore(const char* shardName, const HostCaps& caps) noexcept {
    if (caps.logicalCores < 8u || caps.perfCoreCount < 3u) {
        return -1;
    }
    uint32_t cands[64]{};
    uint32_t nCands = 0;
    const uint32_t limit = std::min<uint32_t>(caps.perfCoreCount, 64u);
    for (uint32_t i = 0; i < limit; ++i) {
        if (caps.perfCoreIds[i] != 0u) {
            cands[nCands++] = caps.perfCoreIds[i];
        }
    }
    if (nCands == 0u) {
        for (uint32_t i = 0; i < limit; ++i) {
            cands[nCands++] = caps.perfCoreIds[i];
        }
    }
    if (nCands == 0u) return -1;
    if (EqualsAsciiInsensitive(shardName, "Master")) {
        return static_cast<int>(cands[0]);
    }
    if (EqualsAsciiInsensitive(shardName, "Caves")) {
        return static_cast<int>(cands[1u % nCands]);
    }
    uint32_t h = 2166136261u;
    if (shardName) {
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(shardName); *p; ++p) {
            h ^= static_cast<uint32_t>(std::tolower(*p));
            h *= 16777619u;
        }
    }
    if (nCands > 2u) {
        return static_cast<int>(cands[2u + (h % (nCands - 2u))]);
    }
    return static_cast<int>(cands[h % nCands]);
}

#ifdef _WIN32
inline bool ApplyWin11EcoQosDisable(bool disable) noexcept {
    using SetProcInfo_t = BOOL (WINAPI*)(HANDLE, int, void*, DWORD);
    static SetProcInfo_t pSetProcInfo = reinterpret_cast<SetProcInfo_t>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation"));
    if (!pSetProcInfo) return false;
    struct ProcPowerThrottleState {
        ULONG Version;
        ULONG ControlMask;
        ULONG StateMask;
    } state{};
    state.Version     = 1;
    state.ControlMask = 0x1u; // PROCESS_POWER_THROTTLING_EXECUTION_SPEED
    state.StateMask   = disable ? 0u : 0x1u;
    return pSetProcInfo(GetCurrentProcess(), 4 /*ProcessPowerThrottling*/, &state, sizeof(state)) != FALSE;
}

inline int ApplyShardIdealProcessor(int coreIdx) noexcept {
    if (coreIdx < 0) return -1;
    using SetIdealEx_t = BOOL (WINAPI*)(HANDLE, PPROCESSOR_NUMBER, PPROCESSOR_NUMBER);
    static SetIdealEx_t pSetIdealEx = reinterpret_cast<SetIdealEx_t>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadIdealProcessorEx"));
    if (pSetIdealEx) {
        PROCESSOR_NUMBER pn{};
        pn.Group  = 0;
        pn.Number = static_cast<BYTE>(coreIdx & 0x3F);
        if (pSetIdealEx(GetCurrentThread(), &pn, nullptr)) {
            return coreIdx;
        }
    }
    const DWORD prev = SetThreadIdealProcessor(GetCurrentThread(), static_cast<DWORD>(coreIdx));
    return (prev != static_cast<DWORD>(-1)) ? coreIdx : -1;
}
#endif

} // namespace hc_server_tier
