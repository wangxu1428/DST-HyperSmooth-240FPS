#pragma once
// =============================================================================
// Low-Overhead Spike Black Box (Shared Portable Header-Only Module)
// =============================================================================
// Pure C++20 POD + libc snprintf formatting:
//   - Zero heap allocations on Push()
//   - Zero std::string / std::vector / iostream / exceptions / RTTI
//   - 100% compatible with Win x64 Pure-UCRT proxy DLLs and Linux GLIBC <= 2.17
// =============================================================================

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace hs_blackbox {

static constexpr size_t kCap = 64;

// Trigger bitflags
static constexpr uint8_t kTrigFrame = 1u << 0;
static constexpr uint8_t kTrigGpu   = 1u << 1;
static constexpr uint8_t kTrigNet   = 1u << 2;
static constexpr uint8_t kTrigTick  = 1u << 3;

enum class ClientCulprit : uint8_t {
    Mixed = 0,
    ClientLua,
    ClientGc,
    ClientRenderSubmit,
    GpuWaitOrSwap,
    ClientIoHost,
    ClientNetCpu,
    ClientCpuOther,
    NetSpike,
};

enum class ServerCulprit : uint8_t {
    Mixed = 0,
    SrvLua,
    SrvGc,
    SrvNet,
    SrvReplica,
    SrvSpatial,
    SrvAStar,
    SrvSaveIo,
    SrvOtherOrOs,
};

inline const char* ClientCulpritName(uint8_t c) noexcept {
    switch (static_cast<ClientCulprit>(c)) {
        case ClientCulprit::ClientLua:          return "CLIENT_LUA";
        case ClientCulprit::ClientGc:           return "CLIENT_GC";
        case ClientCulprit::ClientRenderSubmit: return "CLIENT_RENDER_SUBMIT";
        case ClientCulprit::GpuWaitOrSwap:      return "GPU_WAIT_OR_SWAP";
        case ClientCulprit::ClientIoHost:       return "CLIENT_IO_HOST";
        case ClientCulprit::ClientNetCpu:       return "CLIENT_NET_CPU";
        case ClientCulprit::ClientCpuOther:     return "CLIENT_CPU_OTHER";
        case ClientCulprit::NetSpike:           return "NET_SPIKE";
        case ClientCulprit::Mixed:
        default:                                return "MIXED";
    }
}

inline const char* ServerCulpritName(uint8_t c) noexcept {
    switch (static_cast<ServerCulprit>(c)) {
        case ServerCulprit::SrvLua:       return "SRV_LUA";
        case ServerCulprit::SrvGc:        return "SRV_GC";
        case ServerCulprit::SrvNet:       return "SRV_NET";
        case ServerCulprit::SrvReplica:   return "SRV_REPLICA";
        case ServerCulprit::SrvSpatial:   return "SRV_SPATIAL";
        case ServerCulprit::SrvAStar:     return "SRV_ASTAR";
        case ServerCulprit::SrvSaveIo:    return "SRV_SAVE_IO";
        case ServerCulprit::SrvOtherOrOs: return "SRV_OTHER_OR_OS";
        case ServerCulprit::Mixed:
        default:                          return "SRV_MIXED";
    }
}

struct ClientEvent {
    uint64_t epochMs    = 0;
    uint32_t frameSeq   = 0;
    uint32_t frameUs    = 0;
    uint32_t cpuBusyUs  = 0;
    uint32_t gpuDrainUs = 0;
    uint32_t swapUs     = 0;
    uint32_t worldUs    = 0;
    uint32_t postUs     = 0;
    uint32_t luaUs      = 0;
    uint32_t netCpuUs   = 0;
    uint32_t gcUs       = 0;
    int32_t  gcKb       = 0;
    uint32_t animUs     = 0;
    uint32_t animEnts   = 0;
    uint32_t texUs      = 0;
    uint32_t unzUs      = 0;
    uint32_t shaderUs   = 0;
    uint32_t hostUs     = 0;
    uint32_t otherUs    = 0;
    int32_t  pingAvgMs  = -1;
    int32_t  pingLastMs = -1;
    uint16_t lossX10    = 0;
    uint32_t resendMsgs = 0;
    uint32_t rxKbps     = 0;
    uint8_t  gcFull     = 0;
    uint8_t  triggers   = 0;
    uint8_t  culprit    = 0;
    uint8_t  isNativeGl = 0;
};

struct ServerEvent {
    uint64_t epochMs   = 0;
    uint64_t tickSeq   = 0;
    uint32_t totalUs   = 0;
    uint32_t budgetUs  = 33333u;
    uint32_t luaUs     = 0;
    uint32_t gcUs      = 0;
    uint32_t netUs     = 0;
    uint32_t replicaUs = 0;
    uint32_t findEntUs = 0;
    uint32_t astarUs   = 0;
    uint32_t saveUs    = 0;
    uint32_t otherUs   = 0;
    int32_t  gcKb      = 0;
    uint8_t  gcFull    = 0;
    uint8_t  simHz     = 30u;
    uint8_t  culprit   = 0;
    uint8_t  pad       = 0;
};

inline void ComputeClientDerived(ClientEvent& ev) noexcept {
    // Do NOT subtract animUs (runs inside GameRender/worldUs) or hostUs (often inside luaUs) to prevent double-counting.
    const uint64_t topUs =
        static_cast<uint64_t>(ev.luaUs) +
        static_cast<uint64_t>(ev.netCpuUs) +
        static_cast<uint64_t>(ev.gcUs) +
        static_cast<uint64_t>(ev.worldUs) +
        static_cast<uint64_t>(ev.postUs) +
        static_cast<uint64_t>(ev.swapUs) +
        static_cast<uint64_t>(ev.texUs) +
        static_cast<uint64_t>(ev.unzUs) +
        static_cast<uint64_t>(ev.shaderUs);
    ev.otherUs = (static_cast<uint64_t>(ev.cpuBusyUs) > topUs)
        ? static_cast<uint32_t>(static_cast<uint64_t>(ev.cpuBusyUs) - topUs)
        : 0u;

    if (ev.triggers == kTrigNet) {
        ev.culprit = static_cast<uint8_t>(ClientCulprit::NetSpike);
        return;
    }

    const uint64_t refUs = std::max<uint64_t>({
        static_cast<uint64_t>(ev.frameUs),
        static_cast<uint64_t>(ev.cpuBusyUs) + static_cast<uint64_t>(ev.gpuDrainUs),
        1ULL,
    });
    const uint64_t gpuSum = static_cast<uint64_t>(ev.swapUs) + static_cast<uint64_t>(ev.gpuDrainUs);
    const uint64_t ioSum  = static_cast<uint64_t>(ev.texUs) + static_cast<uint64_t>(ev.unzUs) +
                            static_cast<uint64_t>(ev.shaderUs) + static_cast<uint64_t>(ev.hostUs);
    const uint64_t renSum = static_cast<uint64_t>(ev.worldUs) + static_cast<uint64_t>(ev.postUs);

    if (gpuSum * 100ULL >= refUs * 40ULL && gpuSum >= 8000ULL) {
        ev.culprit = static_cast<uint8_t>(ClientCulprit::GpuWaitOrSwap);
    } else if (ev.gcFull != 0 || (static_cast<uint64_t>(ev.gcUs) * 100ULL >= refUs * 35ULL && ev.gcUs >= 5000u)) {
        ev.culprit = static_cast<uint8_t>(ClientCulprit::ClientGc);
    } else if (ioSum * 100ULL >= refUs * 35ULL && ioSum >= 5000ULL) {
        ev.culprit = static_cast<uint8_t>(ClientCulprit::ClientIoHost);
    } else if (static_cast<uint64_t>(ev.luaUs) * 100ULL >= refUs * 35ULL && ev.luaUs >= 5000u) {
        ev.culprit = static_cast<uint8_t>(ClientCulprit::ClientLua);
    } else if (renSum * 100ULL >= refUs * 40ULL && renSum >= 8000ULL) {
        ev.culprit = static_cast<uint8_t>(ClientCulprit::ClientRenderSubmit);
    } else if (static_cast<uint64_t>(ev.netCpuUs) * 100ULL >= refUs * 35ULL && ev.netCpuUs >= 5000u) {
        ev.culprit = static_cast<uint8_t>(ClientCulprit::ClientNetCpu);
    } else if (static_cast<uint64_t>(ev.otherUs) * 100ULL >= refUs * 50ULL && ev.otherUs >= 8000u) {
        ev.culprit = static_cast<uint8_t>(ClientCulprit::ClientCpuOther);
    } else {
        ev.culprit = static_cast<uint8_t>(ClientCulprit::Mixed);
    }
}

inline void ComputeServerDerived(ServerEvent& ev) noexcept {
    const uint64_t netTop = std::max<uint64_t>(ev.netUs, ev.replicaUs);
    const uint64_t accounted =
        static_cast<uint64_t>(ev.luaUs) +
        static_cast<uint64_t>(ev.gcUs) +
        netTop;
    ev.otherUs = (static_cast<uint64_t>(ev.totalUs) > accounted)
        ? static_cast<uint32_t>(static_cast<uint64_t>(ev.totalUs) - accounted)
        : 0u;

    const uint64_t tot = std::max<uint64_t>(static_cast<uint64_t>(ev.totalUs), 1ULL);
    if (static_cast<uint64_t>(ev.saveUs) * 100ULL >= tot * 35ULL && ev.saveUs >= 5000u) {
        ev.culprit = static_cast<uint8_t>(ServerCulprit::SrvSaveIo);
    } else if (ev.gcFull != 0 || (static_cast<uint64_t>(ev.gcUs) * 100ULL >= tot * 35ULL && ev.gcUs >= 5000u)) {
        ev.culprit = static_cast<uint8_t>(ServerCulprit::SrvGc);
    } else if (static_cast<uint64_t>(ev.astarUs) * 100ULL >= tot * 35ULL && ev.astarUs >= 5000u) {
        ev.culprit = static_cast<uint8_t>(ServerCulprit::SrvAStar);
    } else if (static_cast<uint64_t>(ev.findEntUs) * 100ULL >= tot * 35ULL && ev.findEntUs >= 5000u) {
        ev.culprit = static_cast<uint8_t>(ServerCulprit::SrvSpatial);
    } else if (static_cast<uint64_t>(ev.replicaUs) * 100ULL >= tot * 35ULL && ev.replicaUs >= 5000u) {
        ev.culprit = static_cast<uint8_t>(ServerCulprit::SrvReplica);
    } else if (static_cast<uint64_t>(ev.netUs) * 100ULL >= tot * 35ULL && ev.netUs >= 5000u) {
        ev.culprit = static_cast<uint8_t>(ServerCulprit::SrvNet);
    } else if (static_cast<uint64_t>(ev.luaUs) * 100ULL >= tot * 35ULL && ev.luaUs >= 5000u) {
        ev.culprit = static_cast<uint8_t>(ServerCulprit::SrvLua);
    } else if (static_cast<uint64_t>(ev.otherUs) * 100ULL >= tot * 50ULL && ev.otherUs >= 8000u) {
        ev.culprit = static_cast<uint8_t>(ServerCulprit::SrvOtherOrOs);
    } else {
        ev.culprit = static_cast<uint8_t>(ServerCulprit::Mixed);
    }
}

template <typename T, size_t Cap = kCap>
struct Ring {
    T        items[Cap]{};
    size_t   writeIdx = 0;
    size_t   count = 0;
    uint64_t totalPushed = 0;

    void Push(const T& v) noexcept {
        items[writeIdx] = v;
        writeIdx = (writeIdx + 1) % Cap;
        if (count < Cap) ++count;
        ++totalPushed;
    }

    void Clear() noexcept {
        writeIdx = 0;
        count = 0;
        totalPushed = 0;
    }

    size_t SnapshotLatest(T* out, size_t maxOut, uint64_t& outTotal) const noexcept {
        outTotal = totalPushed;
        if (!out || maxOut == 0 || count == 0) return 0;
        const size_t n = std::min(count, maxOut);
        for (size_t i = 0; i < n; ++i) {
            const size_t idx = (writeIdx + Cap - 1u - i) % Cap;
            out[i] = items[idx];
        }
        return n;
    }
};

inline int FormatClientEntry(char* dst, size_t dstCap, const ClientEvent& e) noexcept {
    return std::snprintf(
        dst, dstCap,
        "{\"epoch_ms\":%llu,\"frame_seq\":%u,\"frame_us\":%u,\"cpu_busy_us\":%u,"
        "\"gpu_drain_us\":%u,\"swap_us\":%u,\"world_us\":%u,\"post_us\":%u,"
        "\"lua_us\":%u,\"net_cpu_us\":%u,\"gc_us\":%u,\"gc_kb\":%d,\"gc_full\":%u,"
        "\"anim_us\":%u,\"anim_ents\":%u,\"tex_us\":%u,\"unz_us\":%u,\"shader_us\":%u,"
        "\"host_us\":%u,\"other_us\":%u,\"ping_avg_ms\":%d,\"ping_last_ms\":%d,"
        "\"loss_pct\":%u.%u,\"resend\":%u,\"rx_kbps\":%u,\"triggers\":%u,"
        "\"culprit\":\"%s\",\"gl\":%u}",
        static_cast<unsigned long long>(e.epochMs),
        static_cast<unsigned>(e.frameSeq),
        static_cast<unsigned>(e.frameUs),
        static_cast<unsigned>(e.cpuBusyUs),
        static_cast<unsigned>(e.gpuDrainUs),
        static_cast<unsigned>(e.swapUs),
        static_cast<unsigned>(e.worldUs),
        static_cast<unsigned>(e.postUs),
        static_cast<unsigned>(e.luaUs),
        static_cast<unsigned>(e.netCpuUs),
        static_cast<unsigned>(e.gcUs),
        static_cast<int>(e.gcKb),
        static_cast<unsigned>(e.gcFull),
        static_cast<unsigned>(e.animUs),
        static_cast<unsigned>(e.animEnts),
        static_cast<unsigned>(e.texUs),
        static_cast<unsigned>(e.unzUs),
        static_cast<unsigned>(e.shaderUs),
        static_cast<unsigned>(e.hostUs),
        static_cast<unsigned>(e.otherUs),
        static_cast<int>(e.pingAvgMs),
        static_cast<int>(e.pingLastMs),
        static_cast<unsigned>(e.lossX10 / 10u),
        static_cast<unsigned>(e.lossX10 % 10u),
        static_cast<unsigned>(e.resendMsgs),
        static_cast<unsigned>(e.rxKbps),
        static_cast<unsigned>(e.triggers),
        ClientCulpritName(e.culprit),
        static_cast<unsigned>(e.isNativeGl ? 1u : 0u));
}

inline int FormatServerEntry(char* dst, size_t dstCap, const ServerEvent& e) noexcept {
    return std::snprintf(
        dst, dstCap,
        "{\"epoch_ms\":%llu,\"tick_seq\":%llu,\"total_us\":%u,\"budget_us\":%u,"
        "\"lua_us\":%u,\"gc_us\":%u,\"net_us\":%u,\"replica_us\":%u,"
        "\"find_ent_us\":%u,\"astar_us\":%u,\"save_us\":%u,\"other_us\":%u,"
        "\"gc_kb\":%d,\"gc_full\":%u,\"sim_hz\":%u,\"culprit\":\"%s\"}",
        static_cast<unsigned long long>(e.epochMs),
        static_cast<unsigned long long>(e.tickSeq),
        static_cast<unsigned>(e.totalUs),
        static_cast<unsigned>(e.budgetUs),
        static_cast<unsigned>(e.luaUs),
        static_cast<unsigned>(e.gcUs),
        static_cast<unsigned>(e.netUs),
        static_cast<unsigned>(e.replicaUs),
        static_cast<unsigned>(e.findEntUs),
        static_cast<unsigned>(e.astarUs),
        static_cast<unsigned>(e.saveUs),
        static_cast<unsigned>(e.otherUs),
        static_cast<int>(e.gcKb),
        static_cast<unsigned>(e.gcFull),
        static_cast<unsigned>(e.simHz),
        ServerCulpritName(e.culprit));
}

template <typename T, typename EntryFn>
inline size_t FormatJsonCommon(const char* role, char* outBuf, size_t outCap,
                               const T* events, size_t n, uint64_t total,
                               uint32_t thresholdMs, EntryFn&& fmtEntry,
                               size_t* outWrittenCount = nullptr) noexcept {
    if (outWrittenCount) *outWrittenCount = 0;
    if (!outBuf || outCap < 128) return 0;

    // Pass 1: measure worst-case header size so we can compute exact emitCount that always fits with closing `]}`.
    const int hdrWorst = std::snprintf(
        nullptr, 0,
        "{\"role\":\"%s\",\"total\":%llu,\"count\":%zu,\"threshold_ms\":%u,\"events\":[",
        role, static_cast<unsigned long long>(total), n, static_cast<unsigned>(thresholdMs));
    if (hdrWorst <= 0 || static_cast<size_t>(hdrWorst) + 3u > outCap) {
        const int fallbackLen = std::snprintf(
            outBuf, outCap,
            "{\"role\":\"%s\",\"total\":%llu,\"count\":0,\"threshold_ms\":%u,\"events\":[]}",
            role, static_cast<unsigned long long>(total), static_cast<unsigned>(thresholdMs));
        return (fallbackLen > 0 && static_cast<size_t>(fallbackLen) < outCap)
            ? static_cast<size_t>(fallbackLen) : 0u;
    }

    size_t emitCount = 0;
    size_t dryPos = static_cast<size_t>(hdrWorst);
    for (size_t i = 0; i < n && events != nullptr; ++i) {
        const int entryLen = fmtEntry(nullptr, 0, events[i]);
        if (entryLen <= 0) break;
        const size_t sep = (emitCount > 0) ? 1u : 0u;
        if (dryPos + sep + static_cast<size_t>(entryLen) + 3u > outCap) break;
        dryPos += sep + static_cast<size_t>(entryLen);
        ++emitCount;
    }

    // Pass 2: emit exact header matching emitCount, then append entries.
    int hdrLen = std::snprintf(
        outBuf, outCap,
        "{\"role\":\"%s\",\"total\":%llu,\"count\":%zu,\"threshold_ms\":%u,\"events\":[",
        role, static_cast<unsigned long long>(total), emitCount, static_cast<unsigned>(thresholdMs));
    if (hdrLen <= 0 || static_cast<size_t>(hdrLen) + 3u > outCap) {
        return 0u;
    }

    size_t pos = static_cast<size_t>(hdrLen);
    for (size_t i = 0; i < emitCount; ++i) {
        if (i > 0) {
            outBuf[pos++] = ',';
        }
        const int w = fmtEntry(outBuf + pos, outCap - pos - 2u, events[i]);
        if (w <= 0 || static_cast<size_t>(w) >= outCap - pos - 2u) {
            if (i > 0 && outBuf[pos - 1] == ',') --pos;
            break;
        }
        pos += static_cast<size_t>(w);
    }

    outBuf[pos++] = ']';
    outBuf[pos++] = '}';
    outBuf[pos] = '\0';
    if (outWrittenCount) *outWrittenCount = emitCount;
    return pos;
}

inline size_t FormatClientJson(char* outBuf, size_t outCap,
                               const ClientEvent* events, size_t n,
                               uint64_t total, uint32_t thresholdMs,
                               size_t* outWrittenCount = nullptr) noexcept {
    return FormatJsonCommon("client", outBuf, outCap, events, n, total, thresholdMs,
                            FormatClientEntry, outWrittenCount);
}

inline size_t FormatServerJson(char* outBuf, size_t outCap,
                               const ServerEvent* events, size_t n,
                               uint64_t total, uint32_t thresholdMs,
                               size_t* outWrittenCount = nullptr) noexcept {
    return FormatJsonCommon("server", outBuf, outCap, events, n, total, thresholdMs,
                            FormatServerEntry, outWrittenCount);
}

} // namespace hs_blackbox
