// =============================================================================
// DST HyperCore Server Native Engine v1.2.0 (IPHLPAPI.DLL x64 Proxy)
// =============================================================================
// First-principles, hermetic-ABI native optimization & profiling engine for
// Don't Starve Together Dedicated Server (dontstarve_dedicated_server_nullrenderer_x64.exe).
//
// Security Hardening (v1.1.0):
//   - Lock-free LoadLibraryW proxy resolution (eliminates AB-BA Loader Lock cycle)
//   - Full x64 SEH .pdata/.xdata unwind info + per-API fail return contract
//   - Module Pinning (GET_MODULE_HANDLE_EX_FLAG_PIN) + lpvReserved-compliant DllMain
//   - W^X compliant trampoline slab (PAGE_EXECUTE_READ after init), ordered publish,
//     paired rollback on partial hook failure, independent SpatialGrid/Visitor AOB checks
//   - Memory-safe FastDataDumper (O(1) active-table cycle detection, DAG node cap,
//     checked GrowBuf realloc + 256MB cap + post-dump shrink, locale-independent to_chars)
//   - Finite/clamped SpatialGrid::Query bounds (exact math.huge/Inf/NaN vanilla parity)
//   - Per-shard log isolation (Master/Caves), SO_EXCLUSIVEADDRUSE + SRWLOCK telemetry
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>
#include <immintrin.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "iphlpapi_names.h"
#include "fast_dumper.h"
#include "fast_json.h"
#include "fast_astar.h"
#include "fast_astar_manager.h"
#include "anti_stw_gc.h"
#include "fs_neg_cache.h"
#include "async_save_commit.h"
#include "rpc_qos.h"
#include "klei_save_codec.h"
#include "replica_fastpath.h"
#include "spike_blackbox.h"
#include "server_hardware_tier.h"
#include "libdeflate.h"
#include "mimalloc.h"

// =============================================================================
// 297-Export Transparent System32\iphlpapi.dll Proxy (Lock-Free Lazy Resolution)
// =============================================================================
// Never holds any user lock across LoadLibraryW, making AB-BA Loader Lock cycles
// and same-thread DllMain re-entrancy deadlocks mathematically impossible.
extern "C" {
void* g_iphlp_ptrs[kIphlpExportCount] = {nullptr};

static std::atomic<HMODULE> g_realIphlp{nullptr};

static HMODULE LoadRealIphlpLockFree() {
    HMODULE h = g_realIphlp.load(std::memory_order_acquire);
    if (h) return h;

    wchar_t sysDir[MAX_PATH] = {};
    UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH - 16) return nullptr;
    wchar_t path[MAX_PATH] = {};
    std::memcpy(path, sysDir, len * sizeof(wchar_t));
    static constexpr wchar_t kSuffix[] = L"\\iphlpapi.dll";
    std::memcpy(path + len, kSuffix, sizeof(kSuffix));

    // NT LdrpLoaderLock internally serializes and supports recursion on the same thread.
    HMODULE loaded = LoadLibraryW(path);
    if (!loaded) return nullptr;

    HMODULE expected = nullptr;
    if (!g_realIphlp.compare_exchange_strong(expected, loaded, std::memory_order_acq_rel)) {
        // Another thread raced and published first; do NOT call FreeLibrary here
        // because we may be executing inside another DLL's DllMain.
        return expected;
    }
    return loaded;
}

void* hc_iphlp_resolve(uint32_t idx) {
    if (idx >= kIphlpExportCount) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return nullptr;
    }
    void* cached = InterlockedCompareExchangePointer(&g_iphlp_ptrs[idx], nullptr, nullptr);
    if (cached) return cached;

    HMODULE hReal = LoadRealIphlpLockFree();
    if (!hReal) {
        SetLastError(ERROR_PROC_NOT_FOUND);
        return nullptr;
    }

    void* fn = reinterpret_cast<void*>(GetProcAddress(hReal, kIphlpExportNames[idx]));
    if (fn) {
        InterlockedExchangePointer(&g_iphlp_ptrs[idx], fn);
    } else {
        SetLastError(ERROR_PROC_NOT_FOUND);
    }
    return fn;
}
} // extern "C"

namespace {

// =============================================================================
// Configuration (dst_hypercore.ini next to iphlpapi.dll)
// =============================================================================
struct Config {
    bool   enable_profiler         = true;
    bool   enable_mimalloc         = true;
    bool   enable_spatial_zerocopy = true;
    bool   enable_adaptive_gc      = true;
    bool   enable_libdeflate       = true;
    bool   enable_fast_dumper      = true;
    bool   enable_timer_boost      = true;
    bool   enable_http_metrics     = false;
    int    http_port               = 7869;
    int    libdeflate_level        = 5;
    float  gc_spare_target_ms      = 28.0f;
    float  gc_calm_max_ms          = 4.0f;
    float  gc_busy_min_ms          = 0.4f;
    int    gc_emergency_add_mb     = 256;
    int    log_interval_ticks      = 300; // log summary every 10s (300 ticks) when profiler active
    int    sim_tick_rate           = 30;  // 30 (stock vanilla) or 60 (opt-in HyperCore/HyperSmooth dual-side 60Hz)
    bool   enable_raknet_instant_wake = true; // 0ms event-driven RakNet UDP wakeup (eliminates 10ms WaitOnEvent stall)
    bool   enable_fast_astar       = true; // O(N log N) flat-hash + binary-heap A* Pathfinder engine (v1.3.0)
    bool   enable_anti_stw_gc      = true; // Smooths startup/mod LUA_GCCOLLECT nukes into bounded incremental drain
    bool   enable_fast_load        = true; // SIMD libdeflate unzReadCurrentFile + 64-bit inflate_fast + rep stosb VecFill
    bool   enable_fs_neg_cache     = true; // Read-only mod/data negative stat cache
    bool   enable_async_save       = true; // Async atomic save-commit queue with strict FIFO & barriers
    bool   enable_rpc_qos          = true; // Causal 3-channel RakNet::RPC4 QoS classifier
    bool   enable_replica_fastpath = true; // O(N) QueryReplicaList + zero-scan PreSerialize engine
    int    replica_construct_budget = -1;  // -1=auto (tier-managed: 64/96/128), 0=off, [16..1024]=explicit cap
    bool   replica_budget_ini_explicit = false;
    bool   enable_blackbox         = true; // Low-overhead spike blackbox ring buffer
    int    blackbox_threshold_ms   = 0;    // 0 = auto (33.3ms@30Hz / 16.7ms@60Hz), or explicit ms
    int    hardware_tier           = 1;    // 0=low, 1=balanced (default), 2=unleashed
};

Config       g_cfg;
HMODULE      g_selfMod = nullptr;
std::wstring g_binDir;
std::wstring g_iniPath;
std::wstring g_logPath;
char         g_shardName[48] = "server";
CRITICAL_SECTION g_logCs;
bool         g_logCsInit = false;
bool         g_isServerProcess = false;
bool         g_timerBoosted = false;
int64_t      g_qpcFreq = 10000000;
double       g_qpcToUs = 0.0;
HANDLE       g_httpThread = nullptr;
std::atomic<bool> g_stopHttp{false};

hc_server_tier::HostCaps   g_hostCaps{};
std::atomic<int>           g_activeServerTier{1};
std::atomic<int>           g_idealCoreApplied{-1};
std::atomic<bool>          g_ecoQosDisabled{false};
std::atomic<bool>          g_prewarmRan{false};
std::atomic<bool>          g_simSchedDirty{true};
std::atomic<uint64_t>      g_fsNegTtlQpc{150000000ULL};
std::atomic<uint32_t>      g_fastDumpMaxNodes{fast_dumper::kMaxDumpNodes};
std::atomic<size_t>        g_fastDumpKeepBytes{fast_dumper::kShrinkKeepBytes};
std::atomic<int>           g_effectiveDeflateLevel{5};
std::atomic<uint32_t>      g_effectiveReplicaBudget{96u};
std::atomic<int>           g_luaReplicaBudgetOverride{-1}; // -1 = auto, 0 = off, [16..1024] = explicit Lua override
std::atomic<float>         g_gcSpareTargetMs{28.0f};
std::atomic<float>         g_gcCalmMaxMs{4.0f};
std::atomic<float>         g_gcBusyMinMs{0.35f};
std::atomic<int>           g_gcEmergencyAddMb{256};
std::atomic<float>         g_gcExcessRampMb{32.0f};
std::atomic<bool>          g_runtimeInitDone{false};
float*       g_simTickFloatPtr = nullptr;
double*      g_simTickDoublePtr = nullptr;
float*       g_simAnimShaderTickPtr = nullptr;
float*       g_simPathfinderTickPtr = nullptr;
float*       g_simSpatialTickPtr = nullptr;
float*       g_simTimeDiffTickPtr = nullptr;
float*       g_netTickScalePtr = nullptr;
void**       g_netMgrGlobalPtr = nullptr;
std::atomic<int> g_activeSimTickRate{30};
std::atomic<bool> g_netTickSynced60{false};

using SetNetworkTickRate_t     = void (*)(void* netMgr, uint32_t tickRate);
using ServerFrameLimiterWait_t = void (*)(void* fl, uint8_t bSleep);
using CreateWaitableTimerExW_t = HANDLE (WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
constexpr DWORD kCreateWaitableTimerHighRes = 0x00000002u;

SetNetworkTickRate_t     g_fnSetNetTickRate           = nullptr;
ServerFrameLimiterWait_t g_origServerFrameLimiterWait = nullptr;

void LogMsg(const char* fmt, ...) {
    if (!g_logCsInit) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;

    EnterCriticalSection(&g_logCs);
    FILE* f = _wfopen(g_logPath.c_str(), L"a");
    if (f) {
        SYSTEMTIME st;
        GetLocalTime(&st);
        std::fprintf(f, "[%02u:%02u:%02u.%03u] %s\n",
                     st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, buf);
        std::fclose(f);
    }
    LeaveCriticalSection(&g_logCs);
}

inline int64_t NowQpc() {
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

inline double QpcDeltaUs(int64_t start, int64_t end) {
    return static_cast<double>(end - start) * g_qpcToUs;
}

void LoadIniConfig() {
    FILE* f = _wfopen(g_iniPath.c_str(), L"r");
    if (!f) return;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == ';' || *p == '[' || *p == '\r' || *p == '\n' || *p == '\0') continue;
        char* eq = std::strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char* k = p;
        char* v = eq + 1;
        auto trim = [](char* s) {
            while (*s == ' ' || *s == '\t') ++s;
            size_t l = std::strlen(s);
            while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '\t' || s[l - 1] == '\r' || s[l - 1] == '\n'))
                s[--l] = '\0';
            return s;
        };
        k = trim(k);
        v = trim(v);
        if      (std::strcmp(k, "enable_profiler") == 0)         g_cfg.enable_profiler = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_mimalloc") == 0)         g_cfg.enable_mimalloc = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_spatial_zerocopy") == 0) g_cfg.enable_spatial_zerocopy = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_adaptive_gc") == 0)      g_cfg.enable_adaptive_gc = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_libdeflate") == 0)       g_cfg.enable_libdeflate = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_fast_dumper") == 0)      g_cfg.enable_fast_dumper = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_timer_boost") == 0)      g_cfg.enable_timer_boost = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_http_metrics") == 0)     g_cfg.enable_http_metrics = std::atoi(v) != 0;
        else if (std::strcmp(k, "http_port") == 0)               g_cfg.http_port = std::atoi(v);
        else if (std::strcmp(k, "libdeflate_level") == 0)        g_cfg.libdeflate_level = std::clamp(std::atoi(v), 1, 12);
        else if (std::strcmp(k, "gc_spare_target_ms") == 0)      g_cfg.gc_spare_target_ms = static_cast<float>(std::atof(v));
        else if (std::strcmp(k, "gc_calm_max_ms") == 0)          g_cfg.gc_calm_max_ms = static_cast<float>(std::atof(v));
        else if (std::strcmp(k, "gc_busy_min_ms") == 0)          g_cfg.gc_busy_min_ms = static_cast<float>(std::atof(v));
        else if (std::strcmp(k, "gc_emergency_add_mb") == 0)     g_cfg.gc_emergency_add_mb = std::clamp(std::atoi(v), 64, 2048);
        else if (std::strcmp(k, "log_interval_ticks") == 0)      g_cfg.log_interval_ticks = std::max(0, std::atoi(v));
        else if (std::strcmp(k, "sim_tick_rate") == 0)           g_cfg.sim_tick_rate = (std::atoi(v) == 60) ? 60 : 30;
        else if (std::strcmp(k, "enable_raknet_instant_wake") == 0) g_cfg.enable_raknet_instant_wake = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_fast_astar") == 0)       g_cfg.enable_fast_astar = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_anti_stw_gc") == 0)      g_cfg.enable_anti_stw_gc = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_fast_load") == 0)        g_cfg.enable_fast_load = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_fs_neg_cache") == 0)     g_cfg.enable_fs_neg_cache = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_async_save") == 0)       g_cfg.enable_async_save = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_rpc_qos") == 0)          g_cfg.enable_rpc_qos = std::atoi(v) != 0;
        else if (std::strcmp(k, "enable_replica_fastpath") == 0) g_cfg.enable_replica_fastpath = std::atoi(v) != 0;
        else if (std::strcmp(k, "replica_construct_budget") == 0) {
            if (hc_server_tier::EqualsAsciiInsensitive(v, "auto")) {
                g_cfg.replica_construct_budget = -1;
                g_cfg.replica_budget_ini_explicit = false;
            } else {
                const int iv = std::atoi(v);
                if (iv < 0) {
                    g_cfg.replica_construct_budget = -1;
                    g_cfg.replica_budget_ini_explicit = false;
                } else {
                    g_cfg.replica_construct_budget = static_cast<int>(hc_replica::ClampReplicaConstructBudget(iv));
                    g_cfg.replica_budget_ini_explicit = true;
                }
            }
        }
        else if (std::strcmp(k, "enable_blackbox") == 0)         g_cfg.enable_blackbox = std::atoi(v) != 0;
        else if (std::strcmp(k, "blackbox_threshold_ms") == 0) {
            const int iv = std::atoi(v);
            g_cfg.blackbox_threshold_ms = (iv <= 0) ? 0 : std::clamp(iv, 5, 500);
        }
        else if (std::strcmp(k, "hardware_tier") == 0)           g_cfg.hardware_tier = static_cast<int>(hc_server_tier::ParseTier(v));
    }
    std::fclose(f);
}

// =============================================================================
// Microsecond Telemetry & Ring Buffer (Lock-Free / Single Sim-Thread Writer)
// =============================================================================
static constexpr size_t kTickRingSize = 300;

struct Telemetry {
    std::atomic<uint64_t> ticks_total{0};
    std::atomic<uint64_t> tick_us_sum{0};
    std::atomic<uint64_t> tick_us_max{0};
    std::atomic<uint64_t> tick_overrun_count{0}; // ticks > 33,333 us

    std::atomic<uint64_t> gc_calls{0};
    std::atomic<uint64_t> gc_us_sum{0};
    std::atomic<uint64_t> gc_us_max{0};
    std::atomic<uint64_t> gc_full_count{0};
    std::atomic<uint64_t> gc_stw_blocked{0};
    std::atomic<int32_t>  gc_last_kb{0};
    std::atomic<int32_t>  gc_last_budget_us{0};

    std::atomic<uint64_t> find_entities_calls{0};
    std::atomic<uint64_t> find_entities_us_sum{0};
    std::atomic<uint64_t> spatial_queries{0};
    std::atomic<uint64_t> spatial_zerocopy_hits{0};
    std::atomic<uint64_t> spatial_cells_visited{0};
    std::atomic<uint64_t> light_spatial_queries{0};

    std::atomic<uint64_t> astar_inits{0};
    std::atomic<uint64_t> astar_steps{0};
    std::atomic<uint64_t> astar_us_sum{0};
    std::atomic<uint64_t> astar_completed{0};
    std::atomic<uint64_t> astar_failed{0};
    std::atomic<uint64_t> astar_fallbacks{0};

    std::atomic<uint64_t> mi_allocs{0};
    std::atomic<uint64_t> mi_reallocs{0};
    std::atomic<uint64_t> mi_frees{0};

    std::atomic<uint64_t> fastdump_calls{0};
    std::atomic<uint64_t> fastdump_us_sum{0};
    std::atomic<uint64_t> fastdump_bytes_sum{0};
    std::atomic<uint64_t> fastdump_fallbacks{0};

    std::atomic<uint64_t> deflate_calls{0};
    std::atomic<uint64_t> deflate_us_sum{0};
    std::atomic<uint64_t> deflate_main_calls{0};
    std::atomic<uint64_t> deflate_main_us_sum{0};
    std::atomic<uint64_t> deflate_in_bytes{0};
    std::atomic<uint64_t> deflate_out_bytes{0};

    std::atomic<uint64_t> unz_hits{0};
    std::atomic<uint64_t> unz_bytes{0};
    std::atomic<uint64_t> unz_us_sum{0};
    std::atomic<uint64_t> uncomp_calls{0};
    std::atomic<uint64_t> uncomp_out_bytes{0};
    std::atomic<uint64_t> uncomp_us_sum{0};
    std::atomic<uint64_t> fastjson_enc_calls{0};
    std::atomic<uint64_t> fastjson_dec_calls{0};
    std::atomic<uint64_t> fastjson_fallbacks{0};

    std::atomic<uint64_t> serialize_world_calls{0};
    std::atomic<uint64_t> serialize_world_us_sum{0};

    std::atomic<uint64_t> prof_lua_update_calls{0};
    std::atomic<uint64_t> prof_lua_update_us_sum{0};
    std::atomic<uint64_t> prof_lua_update_us_max{0};
    std::atomic<uint64_t> prof_net_update_calls{0};
    std::atomic<uint64_t> prof_net_update_us_sum{0};
    std::atomic<uint64_t> prof_net_update_us_max{0};
    std::atomic<uint64_t> prof_replica_update_calls{0};
    std::atomic<uint64_t> prof_replica_update_us_sum{0};
    std::atomic<uint64_t> prof_replica_update_us_max{0};

    std::atomic<uint64_t> fs_neg_hits{0};
    std::atomic<uint64_t> fs_neg_misses{0};

    std::atomic<uint64_t> async_save_fallbacks{0};

    // Ring buffer of recent tick durations (us) for P95/P99 calculation (SRWLOCK-protected)
    mutable SRWLOCK tick_ring_lock = SRWLOCK_INIT;
    uint32_t tick_ring[kTickRingSize] = {};
    uint32_t tick_ring_pos = 0;
    uint32_t tick_ring_filled = 0;

    // Live per-tick state for adaptive GC feedback
    double   last_tick_non_gc_ms = 5.0;
    uint64_t cur_tick_gc_us = 0;

    void Reset() {
        ticks_total = 0; tick_us_sum = 0; tick_us_max = 0; tick_overrun_count = 0;
        gc_calls = 0; gc_us_sum = 0; gc_us_max = 0; gc_full_count = 0; gc_stw_blocked = 0;
        find_entities_calls = 0; find_entities_us_sum = 0;
        spatial_queries = 0; spatial_zerocopy_hits = 0; spatial_cells_visited = 0; light_spatial_queries = 0;
        astar_inits = 0; astar_steps = 0; astar_us_sum = 0; astar_completed = 0; astar_failed = 0; astar_fallbacks = 0;
        mi_allocs = 0; mi_reallocs = 0; mi_frees = 0;
        fastdump_calls = 0; fastdump_us_sum = 0; fastdump_bytes_sum = 0; fastdump_fallbacks = 0;
        deflate_calls = 0; deflate_us_sum = 0; deflate_main_calls = 0; deflate_main_us_sum = 0; deflate_in_bytes = 0; deflate_out_bytes = 0;
        unz_hits = 0; unz_bytes = 0; unz_us_sum = 0;
        uncomp_calls = 0; uncomp_out_bytes = 0; uncomp_us_sum = 0;
        fastjson_enc_calls = 0; fastjson_dec_calls = 0; fastjson_fallbacks = 0;
        serialize_world_calls = 0; serialize_world_us_sum = 0;
        prof_lua_update_calls = 0; prof_lua_update_us_sum = 0; prof_lua_update_us_max = 0;
        prof_net_update_calls = 0; prof_net_update_us_sum = 0; prof_net_update_us_max = 0;
        prof_replica_update_calls = 0; prof_replica_update_us_sum = 0; prof_replica_update_us_max = 0;
        fs_neg_hits = 0; fs_neg_misses = 0;
        async_save_fallbacks = 0;
        AcquireSRWLockExclusive(&tick_ring_lock);
        tick_ring_pos = 0; tick_ring_filled = 0;
        ReleaseSRWLockExclusive(&tick_ring_lock);
    }

    void PushTickSample(uint32_t us) {
        AcquireSRWLockExclusive(&tick_ring_lock);
        tick_ring[tick_ring_pos] = us;
        tick_ring_pos = (tick_ring_pos + 1) % kTickRingSize;
        if (tick_ring_filled < kTickRingSize) tick_ring_filled++;
        ReleaseSRWLockExclusive(&tick_ring_lock);
    }

    void ComputePercentiles(double& out_avg, double& out_p95, double& out_p99) const {
        uint32_t tmp[kTickRingSize];
        uint32_t n = 0;
        AcquireSRWLockShared(&tick_ring_lock);
        n = std::min<uint32_t>(tick_ring_filled, kTickRingSize);
        if (n > 0) std::memcpy(tmp, tick_ring, n * sizeof(uint32_t));
        ReleaseSRWLockShared(&tick_ring_lock);

        if (n == 0) { out_avg = out_p95 = out_p99 = 0.0; return; }
        std::sort(tmp, tmp + n);
        uint64_t sum = 0;
        for (uint32_t i = 0; i < n; ++i) sum += tmp[i];
        out_avg = static_cast<double>(sum) / n;
        out_p95 = tmp[std::min<uint32_t>(n - 1, (n * 95) / 100)];
        out_p99 = tmp[std::min<uint32_t>(n - 1, (n * 99) / 100)];
    }
};

Telemetry g_stats;

struct TickSubphaseTls {
    uint32_t luaUs     = 0;
    uint32_t gcUs      = 0;
    uint32_t netUs     = 0;
    uint32_t replicaUs = 0;
    uint32_t findEntUs = 0;
    uint32_t astarUs   = 0;
    uint32_t saveUs    = 0;
    uint8_t  gcFull    = 0;
    int      saveDepth = 0;
};
thread_local TickSubphaseTls tl_tickProf{};

struct SaveSubphaseGuard {
    bool top = false;
    explicit SaveSubphaseGuard() noexcept : top(tl_tickProf.saveDepth++ == 0) {}
    void Add(uint64_t us) const noexcept {
        if (top && us > 0) {
            tl_tickProf.saveUs += static_cast<uint32_t>(std::min<uint64_t>(us, 60000000ULL));
        }
    }
    ~SaveSubphaseGuard() noexcept { --tl_tickProf.saveDepth; }
};

inline uint32_t ClampUsU32(uint64_t us) noexcept {
    return static_cast<uint32_t>(std::min<uint64_t>(us, 60000000ULL));
}

inline uint64_t NowEpochMs() noexcept {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    const uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
                       static_cast<uint64_t>(ft.dwLowDateTime);
    constexpr uint64_t kEpochDiff = 116444736000000000ULL;
    return (t > kEpochDiff) ? ((t - kEpochDiff) / 10000ULL) : 0ULL;
}

SRWLOCK g_bbLock = SRWLOCK_INIT;
hs_blackbox::Ring<hs_blackbox::ServerEvent> g_serverBlackbox;
std::atomic<uint64_t> g_blackboxTotalSpikes{0};
std::atomic<uint32_t> g_blackboxLastThreshMs{33};

// =============================================================================
// Cross-Version AOB Scanner & Near-Module Relocating x64 Hook Engine
// =============================================================================
uint8_t* g_textStart = nullptr;
size_t   g_textSize  = 0;

bool InitTextSection() {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(sec[i].Name, ".text", 5) == 0) {
            g_textStart = base + sec[i].VirtualAddress;
            g_textSize  = sec[i].Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

// Wildcard byte is -1
uint8_t* FindUniqueAob(const int16_t* pat, size_t len, const char* name) {
    if (!g_textStart || g_textSize < len) return nullptr;
    size_t firstFixed = 0;
    while (firstFixed < len && pat[firstFixed] < 0) ++firstFixed;
    if (firstFixed >= len) return nullptr;
    uint8_t fb = static_cast<uint8_t>(pat[firstFixed]);

    uint8_t* found = nullptr;
    size_t hits = 0;
    const size_t limit = g_textSize - len;
    for (size_t i = 0; i <= limit; ++i) {
        if (g_textStart[i + firstFixed] != fb) continue;
        bool ok = true;
        for (size_t k = 0; k < len; ++k) {
            if (pat[k] >= 0 && g_textStart[i + k] != static_cast<uint8_t>(pat[k])) {
                ok = false;
                break;
            }
        }
        if (ok) {
            found = g_textStart + i;
            ++hits;
            if (hits > 1) break;
        }
    }
    if (hits != 1) {
        LogMsg("[AOB] WARN: %s matched %zu times (expected 1) - skipped safely", name, hits);
        return nullptr;
    }
    return found;
}

uint8_t* ResolveRel32Call(uint8_t* callSite) {
    if (!callSite || callSite[0] != 0xE8) return nullptr;
    int32_t disp = 0;
    std::memcpy(&disp, callSite + 1, 4);
    return callSite + 5 + disp;
}

// Near-module trampoline slab (within +-1.5 GB of exe so rel32/RIP-disp32 always fits!)
uint8_t* g_trampSlab = nullptr;
size_t   g_trampUsed = 0;
static constexpr size_t kTrampSlabSize = 65536;

bool InitTrampolineSlab() {
    uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    // Search downward then upward in 64KB increments within +-1.5 GB
    for (int dir = 0; dir < 2; ++dir) {
        for (uintptr_t off = 0x1000000; off < 0x58000000ULL; off += 0x10000) {
            uintptr_t cand = (dir == 0) ? (base + off) : (base > off ? base - off : 0);
            if (cand < 0x10000000ULL) continue;
            void* p = VirtualAlloc(reinterpret_cast<void*>(cand), kTrampSlabSize,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (p) {
                g_trampSlab = static_cast<uint8_t*>(p);
                g_trampUsed = 0;
                return true;
            }
        }
    }
    return false;
}

void WriteAbsJmp14(uint8_t* dst, const void* target) {
    dst[0] = 0xFF;
    dst[1] = 0x25;
    uint32_t zero = 0;
    std::memcpy(dst + 2, &zero, 4);
    uint64_t addr = reinterpret_cast<uint64_t>(target);
    std::memcpy(dst + 6, &addr, 8);
}

// Relocates E8 rel32 or [48/4C] [8B/8D] ModRM(mod=0,rm=5) disp32 at explicit instruction offsets
struct RelocItem {
    size_t offset;  // offset inside stolen prologue
    size_t insnLen; // 5 for E8 rel32, 7 for REX + op + modrm + disp32
    size_t dispOff; // 1 for E8, 3 for 7-byte RIP-relative
};

bool VerifyAobAt(const uint8_t* addr, const int16_t* pat, size_t len) {
    if (!addr || !g_textStart) return false;
    if (addr < g_textStart || static_cast<size_t>(addr - g_textStart) + len > g_textSize) return false;
    for (size_t k = 0; k < len; ++k) {
        if (pat[k] >= 0 && addr[k] != static_cast<uint8_t>(pat[k])) return false;
    }
    return true;
}

bool RestoreHookBytes(uint8_t* target, const uint8_t* savedBytes, size_t len) {
    if (!target || !savedBytes || len == 0) return false;
    DWORD oldProt = 0;
    if (!VirtualProtect(target, len, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    std::memcpy(target, savedBytes, len);
    DWORD dummy = 0;
    if (!VirtualProtect(target, len, oldProt, &dummy)) {
        LogMsg("[HOOK] WARN: RestoreHookBytes failed restoring page protection 0x%lX @ %p (err=%lu)",
               static_cast<unsigned long>(oldProt), target, static_cast<unsigned long>(GetLastError()));
    }
    FlushInstructionCache(GetCurrentProcess(), target, len);
    return true;
}

// Strict ordered install:
//   1. Build trampoline + relocate RIP-relative instructions
//   2. FlushInstructionCache(tramp)
//   3. Publish *outOrigPtr = tramp with release fence BEFORE modifying target
//   4. Patch target entry -> FlushInstructionCache(target) -> restore page protection
bool InstallHookOrdered(uint8_t* target, void* detour, size_t stolenLen, void** outOrigPtr,
                        const RelocItem* relocs = nullptr, size_t relocCount = 0,
                        uint8_t* outSavedPrologue = nullptr) {
    if (!target || !detour || !outOrigPtr || stolenLen < 14 || !g_trampSlab) return false;
    size_t needed = stolenLen + 14 + 16;
    if (g_trampUsed + needed > kTrampSlabSize) return false;

    const size_t prevUsed = g_trampUsed;
    uint8_t* tramp = g_trampSlab + g_trampUsed;
    g_trampUsed = (g_trampUsed + stolenLen + 14 + 15) & ~size_t(15);

    std::memcpy(tramp, target, stolenLen);
    if (outSavedPrologue) {
        std::memcpy(outSavedPrologue, target, stolenLen);
    }

    for (size_t i = 0; i < relocCount; ++i) {
        const RelocItem& r = relocs[i];
        int32_t oldDisp = 0;
        std::memcpy(&oldDisp, target + r.offset + r.dispOff, 4);
        intptr_t absTarget = reinterpret_cast<intptr_t>(target + r.offset + r.insnLen) + oldDisp;
        intptr_t newDisp64 = absTarget - reinterpret_cast<intptr_t>(tramp + r.offset + r.insnLen);
        if (newDisp64 < INT32_MIN || newDisp64 > INT32_MAX) {
            LogMsg("[HOOK] Reloc out of range for target %p!", target);
            g_trampUsed = prevUsed;
            return false;
        }
        int32_t newDisp = static_cast<int32_t>(newDisp64);
        std::memcpy(tramp + r.offset + r.dispOff, &newDisp, 4);
    }

    WriteAbsJmp14(tramp + stolenLen, target + stolenLen);
    FlushInstructionCache(GetCurrentProcess(), tramp, stolenLen + 14);

    // Publish trampoline pointer BEFORE target entry becomes live
    *outOrigPtr = tramp;
    std::atomic_thread_fence(std::memory_order_release);

    DWORD oldProt = 0;
    if (!VirtualProtect(target, stolenLen, PAGE_EXECUTE_READWRITE, &oldProt)) {
        *outOrigPtr = nullptr;
        g_trampUsed = prevUsed;
        return false;
    }
    WriteAbsJmp14(target, detour);
    for (size_t i = 14; i < stolenLen; ++i) target[i] = 0x90;
    DWORD dummy = 0;
    if (!VirtualProtect(target, stolenLen, oldProt, &dummy)) {
        LogMsg("[HOOK] WARN: VirtualProtect restore failed for target %p", target);
    }
    FlushInstructionCache(GetCurrentProcess(), target, stolenLen);
    return true;
}

uint8_t* FindUniqueMaskedCallsite(const uint8_t* pre, size_t preLen,
                                  const uint8_t* post, size_t postLen,
                                  const char* name) {
    const size_t total = preLen + 4 + postLen;
    if (!g_textStart || g_textSize < total) return nullptr;
    uint8_t* hit = nullptr;
    size_t count = 0;
    for (size_t off = 0; off <= g_textSize - total; ++off) {
        if (g_textStart[off] == pre[0] &&
            std::memcmp(g_textStart + off, pre, preLen) == 0 &&
            std::memcmp(g_textStart + off + preLen + 4, post, postLen) == 0) {
            hit = g_textStart + off + preLen - 1; // points to 0xE8
            ++count;
            if (count > 1) break;
        }
    }
    if (count != 1) {
        LogMsg("[AOB] WARN: callsite %s matched %zu times (expected 1) - skipped safely", name, count);
        return nullptr;
    }
    return hit;
}

bool InstallRel32CallRelay(uint8_t* callSite, void* detour) {
    if (!callSite || callSite[0] != 0xE8 || !detour || !g_trampSlab) return false;
    const size_t needed = 16;
    if (g_trampUsed + needed > kTrampSlabSize) return false;
    const size_t prevUsed = g_trampUsed;
    uint8_t* relay = g_trampSlab + g_trampUsed;
    g_trampUsed = (g_trampUsed + 14 + 15) & ~size_t(15);

    const intptr_t diff = reinterpret_cast<intptr_t>(relay) - reinterpret_cast<intptr_t>(callSite + 5);
    if (diff < INT32_MIN || diff > INT32_MAX) {
        g_trampUsed = prevUsed;
        return false;
    }
    WriteAbsJmp14(relay, detour);
    FlushInstructionCache(GetCurrentProcess(), relay, 14);
    std::atomic_thread_fence(std::memory_order_release);

    const int32_t rel = static_cast<int32_t>(diff);
    if (!RestoreHookBytes(callSite + 1, reinterpret_cast<const uint8_t*>(&rel), 4)) {
        g_trampUsed = prevUsed;
        return false;
    }
    return true;
}

bool IsInTextSection(const uint8_t* p) {
    return p && g_textStart && p >= g_textStart && static_cast<size_t>(p - g_textStart) < g_textSize;
}

bool FinalizeTrampolineSlabWX() {
    if (!g_trampSlab) return false;
    DWORD oldProt = 0;
    if (VirtualProtect(g_trampSlab, kTrampSlabSize, PAGE_EXECUTE_READ, &oldProt)) {
        LogMsg("[HOOK] Locked trampoline slab W^X -> PAGE_EXECUTE_READ (%zu/%zu bytes used)",
               g_trampUsed, kTrampSlabSize);
        return true;
    }
    LogMsg("[ERROR][HOOK] Failed locking trampoline slab W^X -> PAGE_EXECUTE_READ (err=%lu)!",
           static_cast<unsigned long>(GetLastError()));
    return false;
}

// =============================================================================
// Resolved Engine Pointers & Lua C API
// =============================================================================
// =============================================================================
// Module 7 & 8: Read-Only FS Negative Stat Cache & Async Atomic Save-Commit
// (Immortal placement singletons: ZERO destructors in DLL_PROCESS_DETACH!)
// =============================================================================
alignas(hs_fs_neg::NegativeStatCache) static uint8_t g_fsNegCacheStorage[sizeof(hs_fs_neg::NegativeStatCache)];
alignas(hc_async_save::AsyncSaveQueue) static uint8_t g_asyncSaveStorage[sizeof(hc_async_save::AsyncSaveQueue)];
static std::atomic<hs_fs_neg::NegativeStatCache*> g_fsNegCachePtr{nullptr};
static std::atomic<hc_async_save::AsyncSaveQueue*> g_asyncSavePtr{nullptr};

inline hs_fs_neg::NegativeStatCache& GetFsNegCache() noexcept {
    auto* p = g_fsNegCachePtr.load(std::memory_order_acquire);
    if (!p) [[unlikely]] {
        static std::atomic<bool> init{false};
        if (!init.exchange(true, std::memory_order_acq_rel)) {
            p = new (g_fsNegCacheStorage) hs_fs_neg::NegativeStatCache();
            g_fsNegCachePtr.store(p, std::memory_order_release);
        } else {
            while (!(p = g_fsNegCachePtr.load(std::memory_order_acquire))) {
                _mm_pause();
            }
        }
    }
    return *p;
}

inline hc_async_save::AsyncSaveQueue& GetAsyncSave() noexcept {
    auto* p = g_asyncSavePtr.load(std::memory_order_acquire);
    if (!p) [[unlikely]] {
        static std::atomic<bool> init{false};
        if (!init.exchange(true, std::memory_order_acq_rel)) {
            p = new (g_asyncSaveStorage) hc_async_save::AsyncSaveQueue();
            g_asyncSavePtr.store(p, std::memory_order_release);
        } else {
            while (!(p = g_asyncSavePtr.load(std::memory_order_acquire))) {
                _mm_pause();
            }
        }
    }
    return *p;
}
std::atomic<bool> g_fsNegCacheActive{false};
std::atomic<bool> g_asyncSaveActive{false};

using WritePersistentFile_t = uint8_t (*)(const char* utf8Path, const void* data, uint32_t size,
                                          void* delegate, uint32_t arg5, void* arg6);
using Rpc4Call_t = void (*)(void* thisPtr, const char* uniqueID, void* bitStream,
                            int priority, int reliability, uint8_t orderingChannel,
                            const void* systemIdentifier, uint8_t broadcast, uint8_t callLocally);
using GetFileAttributesW_t  = DWORD (WINAPI*)(LPCWSTR);
using CreateFileW_t         = HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
using DeleteFileW_t         = BOOL (WINAPI*)(LPCWSTR);
using MoveFileExW_t         = BOOL (WINAPI*)(LPCWSTR, LPCWSTR, DWORD);
using CopyFileW_t           = BOOL (WINAPI*)(LPCWSTR, LPCWSTR, BOOL);
using CreateDirectoryW_t    = BOOL (WINAPI*)(LPCWSTR, LPSECURITY_ATTRIBUTES);
using RemoveDirectoryW_t    = BOOL (WINAPI*)(LPCWSTR);
using FindFirstFileW_t      = HANDLE (WINAPI*)(LPCWSTR, LPWIN32_FIND_DATAW);
using SetCurrentDirectoryW_t = BOOL (WINAPI*)(LPCWSTR);
using Wfopen_t              = FILE* (__cdecl*)(const wchar_t*, const wchar_t*);
using WfopenS_t             = int (__cdecl*)(FILE**, const wchar_t*, const wchar_t*);
using Fopen_t               = FILE* (__cdecl*)(const char*, const char*);
using Fwrite_t              = size_t (__cdecl*)(const void*, size_t, size_t, FILE*);
using Fclose_t              = int (__cdecl*)(FILE*);
using Wstat_t               = int (__cdecl*)(const wchar_t*, void*);
using StatA_t               = int (__cdecl*)(const char*, void*);
using OpenA_t               = int (__cdecl*)(const char*, int, int);
using MsvcrErrnoFn_t        = int* (__cdecl*)();
using Uncompress_t          = int (*)(uint8_t*, uint32_t*, const uint8_t*, uint32_t);
using ExeExit_t             = void (__cdecl*)(int);
using ExeCexit_t            = void (__cdecl*)();

WritePersistentFile_t  g_origWritePersistentFile = nullptr;
GetFileAttributesW_t   g_origGetFileAttributesW  = nullptr;
CreateFileW_t          g_origCreateFileW         = nullptr;
DeleteFileW_t          g_origDeleteFileW         = nullptr;
MoveFileExW_t          g_origMoveFileExW         = nullptr;
CopyFileW_t            g_origCopyFileW           = nullptr;
CreateDirectoryW_t     g_origCreateDirectoryW    = nullptr;
RemoveDirectoryW_t     g_origRemoveDirectoryW    = nullptr;
FindFirstFileW_t       g_origFindFirstFileW      = nullptr;
SetCurrentDirectoryW_t g_origSetCurrentDirW      = nullptr;
Wfopen_t               g_origWfopen              = nullptr;
WfopenS_t              g_origWfopen_s            = nullptr;
Fopen_t                g_origFopen               = nullptr;
Fwrite_t               g_origFwrite              = nullptr;
Fclose_t               g_origFclose              = nullptr;
Wstat_t                g_origWstat64i32          = nullptr;
StatA_t                g_origStat64              = nullptr;
StatA_t                g_origStat64i32           = nullptr;
OpenA_t                g_origOpenA               = nullptr;
MsvcrErrnoFn_t         g_msvcr90Errno            = nullptr;
Uncompress_t           g_origUncompress          = nullptr;
ExeExit_t              g_origExeExit             = nullptr;
ExeExit_t              g_origExe_exit            = nullptr;
ExeCexit_t             g_origExeCexit            = nullptr;

// Unaligned low bit pointer impossible as a real CRT FILE* struct; intercepted before MSVCR90!
FILE* const kAsyncSentinelFile = reinterpret_cast<FILE*>(static_cast<uintptr_t>(0x1ULL));

struct AsyncCaptureState {
    bool        active = false;
    const void* data   = nullptr;
    uint32_t    size   = 0;
};
thread_local AsyncCaptureState tl_asyncCapture;

hs_rpc_qos::Classifier g_rpcQos;
Rpc4Call_t             g_origRpc4Call = nullptr;
std::atomic<bool>      g_rpcQosActive{false};

// Module 0B: Verified Sub-Phase Profiler & ReplicaManager3 Fast-Path Pointers
using SimLuaUpdate_t      = uint8_t (*)(void* sim, float dt, uint8_t paused);
using NetMgrUpdate_t      = void (*)(void* netMgr, float dt);
using ReplicaMgr3Update_t = void (*)(void* rm3);
using QueryReplicaList_t  = void (*)(void* conn, void* outConstruct, void* outDestroy);

SimLuaUpdate_t      g_origSimLuaUpdate      = nullptr;
NetMgrUpdate_t      g_origNetMgrUpdate      = nullptr;
ReplicaMgr3Update_t g_origReplicaMgr3Update = nullptr;
QueryReplicaList_t  g_origQueryReplicaList  = nullptr;

hc_replica::ReplicaAbiConfig g_replicaCfg;
hc_replica::ReplicaFastStats g_replicaStats;
std::atomic<bool>            g_replicaFastActive{false};
std::atomic<bool>            g_replicaPreSerPatched{false};

inline void UpdateAtomicMax(std::atomic<uint64_t>& target, uint64_t val) noexcept {
    uint64_t prev = target.load(std::memory_order_relaxed);
    while (val > prev && !target.compare_exchange_weak(prev, val, std::memory_order_relaxed)) {}
}

LuaApiTable g_lua;

using SimUpdate_t          = uint8_t (*)(void* sim, float dt, uint8_t flag);
using CollectGarbage_t     = void (*)(void* sim);
using SetGcBudget_t        = void (*)(float sec);
using LuaAlloc_t           = void* (*)(void* ud, void* ptr, size_t osize, size_t nsize);
using LuaNewState_t        = void* (*)(LuaAlloc_t f, void* ud);
using LuaClose_t           = void (*)(void* L);
using LuaOpenLibs_t        = void (*)(void* L);
using TrackLuaMem_t        = void (*)(void* sim, uint32_t osize, uint32_t nsize);
using EntityMgrFind_t      = void (*)(void* mgr, const float* pos3, float radius, void* mustTags,
                                      void* cantTags, void* oneOfTags, void* outVec);
using SpatialGridQuery_t   = void (*)(void* grid, const float* posXZ, float radius, void* visitor);
using EntityVisitor_t      = uint8_t (*)(void* visitor, void* entity);
using LuaCFuncObj_t        = int (*)(void* selfObj, void* L);
using Compress2_t          = int (*)(uint8_t* dest, uint32_t* destLen,
                                     const uint8_t* source, uint32_t sourceLen, int level);

SimUpdate_t        g_origSimUpdate         = nullptr;
CollectGarbage_t   g_origCollectGarbage    = nullptr;
SetGcBudget_t      g_setGcBudget           = nullptr;
LuaNewState_t      g_origLuaNewState       = nullptr;
LuaClose_t         g_origLuaClose          = nullptr;
LuaOpenLibs_t      g_origLuaOpenLibs       = nullptr;
TrackLuaMem_t      g_trackLuaMem           = nullptr;
uint8_t*           g_simLuaAllocAddr       = nullptr;
uint8_t*           g_wgLuaAllocAddr        = nullptr;
EntityMgrFind_t    g_origEntityMgrFind     = nullptr;
SpatialGridQuery_t g_origSpatialGridQuery  = nullptr;
EntityVisitor_t    g_entityVisitor         = nullptr;
LuaCFuncObj_t      g_origSimFindEnts       = nullptr;
LuaCFuncObj_t      g_origSimFindEntsReg    = nullptr;
LuaCFuncObj_t      g_origSerializeWorld    = nullptr;
Compress2_t        g_origCompress2         = nullptr;

// =============================================================================
// Module 1: Hermetic lua_newstate -> Lockless mimalloc (Zero Ring-0 Syscalls!)
// =============================================================================
void* HyperSimLuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto trackMem = [&]() {
        if (ud && g_trackLuaMem && reinterpret_cast<const uint8_t*>(ud)[0x119] != 0) {
            g_trackLuaMem(ud, static_cast<uint32_t>(osize), static_cast<uint32_t>(nsize));
        }
    };
    if (nsize == 0) {
        if (ptr) {
            mi_free(ptr);
            g_stats.mi_frees.fetch_add(1, std::memory_order_relaxed);
            trackMem();
        }
        return nullptr;
    }
    void* out = nullptr;
    if (!ptr) {
        out = mi_malloc(nsize);
        if (out) g_stats.mi_allocs.fetch_add(1, std::memory_order_relaxed);
    } else {
        out = mi_realloc(ptr, nsize);
        if (out) g_stats.mi_reallocs.fetch_add(1, std::memory_order_relaxed);
    }
    if (out) {
        trackMem();
    }
    return out;
}

void* HyperWgLuaAlloc(void* /*ud*/, void* ptr, size_t /*osize*/, size_t nsize) {
    if (nsize == 0) {
        if (ptr) {
            mi_free(ptr);
            g_stats.mi_frees.fetch_add(1, std::memory_order_relaxed);
        }
        return nullptr;
    }
    if (!ptr) {
        g_stats.mi_allocs.fetch_add(1, std::memory_order_relaxed);
        return mi_malloc(nsize);
    }
    g_stats.mi_reallocs.fetch_add(1, std::memory_order_relaxed);
    return mi_realloc(ptr, nsize);
}

void* HookLuaNewState(LuaAlloc_t f, void* ud) {
    if (g_cfg.enable_mimalloc) {
        if (reinterpret_cast<uint8_t*>(f) == g_simLuaAllocAddr) {
            LogMsg("[MIMALLOC] Attached lockless mimalloc allocator to Simulation Lua VM (ud=%p)", ud);
            return g_origLuaNewState(&HyperSimLuaAlloc, ud);
        }
        if (reinterpret_cast<uint8_t*>(f) == g_wgLuaAllocAddr) {
            LogMsg("[MIMALLOC] Attached lockless mimalloc allocator to WorldGen/Aux Lua VM");
            return g_origLuaNewState(&HyperWgLuaAlloc, ud);
        }
    }
    return g_origLuaNewState(f, ud);
}

// =============================================================================
// Module 2: SpatialGrid::Query Zero-Copy Read-Only Fast Path
// =============================================================================
// Exact MSVC x64 layout verified from SpatialGrid::Query @ 0x140114970 & 0x1403DAAB0:
//   SpatialCell (size 0x28 = 40 bytes):
//     +0x00 int32_t cx
//     +0x04 int32_t cz
//     +0x08 void*   std::vector allocator/debug proxy (MSVC90 _Vector_val)
//     +0x10 void**  ent_begin
//     +0x18 void**  ent_end
//     +0x20 void**  ent_cap
//   SpatialGrid:
//     +0x08 SpatialCell* cells_begin
//     +0x10 SpatialCell* cells_end
//     +0x38 float        cell_size
struct SpatialCell {
    int32_t cx;
    int32_t cz;
    void*   vec_proxy;
    void**  ent_begin;
    void**  ent_end;
    void**  ent_cap;
};
static_assert(sizeof(SpatialCell) == 0x28, "SpatialCell size must be 0x28");

thread_local bool tl_inReadOnlyFindEntities = false;

void HookEntityMgrFindEntities(void* mgr, const float* pos3, float radius,
                               void* mustTags, void* cantTags, void* oneOfTags, void* outVec) {
    struct Guard {
        bool prev;
        Guard() : prev(tl_inReadOnlyFindEntities) { tl_inReadOnlyFindEntities = true; }
        ~Guard() { tl_inReadOnlyFindEntities = prev; }
    } guard;
    g_origEntityMgrFind(mgr, pos3, radius, mustTags, cantTags, oneOfTags, outVec);
}

void RunZeroCopySpatialQuery(void* grid, const float* posXZ, float radius, void* visitor,
                             EntityVisitor_t vis, SpatialGridQuery_t orig, bool isLightQuery) {
    g_stats.spatial_queries.fetch_add(1, std::memory_order_relaxed);
    if (isLightQuery) {
        g_stats.light_spatial_queries.fetch_add(1, std::memory_order_relaxed);
    }

    if (!g_cfg.enable_spatial_zerocopy || !vis || !grid || !posXZ) {
        if (orig) orig(grid, posXZ, radius, visitor);
        return;
    }

    const auto* gbytes = reinterpret_cast<const uint8_t*>(grid);
    const auto* cells_begin = *reinterpret_cast<const SpatialCell* const*>(gbytes + 0x08);
    const auto* cells_end   = *reinterpret_cast<const SpatialCell* const*>(gbytes + 0x10);
    const float cell_size   = *reinterpret_cast<const float*>(gbytes + 0x38);

    if (!cells_begin || cells_begin >= cells_end || !std::isfinite(cell_size) || cell_size <= 0.0f) {
        return;
    }

    const float x = posXZ[0];
    const float z = posXZ[1];
    // Guard against math.huge / Inf / NaN / extreme radii (prevents float->int32_t UB!)
    // Exact vanilla parity: fall back cleanly to Klei's original SpatialGrid::Query.
    if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(radius) ||
        radius < 0.0f || radius > 100000.0f) {
        if (orig) orig(grid, posXZ, radius, visitor);
        return;
    }

    g_stats.spatial_zerocopy_hits.fetch_add(1, std::memory_order_relaxed);

    const double inv_cs = 1.0 / static_cast<double>(cell_size);
    const double d_min_cx = std::floor((static_cast<double>(x) - static_cast<double>(radius)) * inv_cs);
    const double d_max_cx = std::floor((static_cast<double>(x) + static_cast<double>(radius)) * inv_cs);
    const double d_min_cz = std::floor((static_cast<double>(z) - static_cast<double>(radius)) * inv_cs);
    const double d_max_cz = std::floor((static_cast<double>(z) + static_cast<double>(radius)) * inv_cs);

    const double grid_lo_x = static_cast<double>(cells_begin->cx);
    const double grid_hi_x = static_cast<double>((cells_end - 1)->cx);
    if (d_max_cx < grid_lo_x || d_min_cx > grid_hi_x) return;

    auto clamp_i32 = [](double v) -> int32_t {
        if (v < -1000000000.0) return -1000000000;
        if (v >  1000000000.0) return  1000000000;
        return static_cast<int32_t>(v);
    };
    const int32_t min_cx = clamp_i32(std::max(d_min_cx, grid_lo_x));
    const int32_t max_cx = clamp_i32(std::min(d_max_cx, grid_hi_x));
    const int32_t min_cz = clamp_i32(d_min_cz);
    const int32_t max_cz = clamp_i32(d_max_cz);

    if (min_cx > max_cx || min_cz > max_cz) return;

    const bool skip_box_check = (cell_size > radius * 2.0f);
    // Tiny epsilon guarantees zero false-negative cell skips on boundary; EntityVisitor enforces exact r^2.
    const float r_sq_eps = radius * radius + 1e-3f;

    uint64_t visited_cells = 0;

    // Initial binary search for first cell >= (min_cx, min_cz)
    auto lower_bound_cell = [](const SpatialCell* lo, const SpatialCell* hi, int32_t cx, int32_t cz) {
        while (lo < hi) {
            const SpatialCell* mid = lo + ((hi - lo) >> 1);
            if (mid->cx < cx || (mid->cx == cx && mid->cz < cz)) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        return lo;
    };

    const SpatialCell* cur = lower_bound_cell(cells_begin, cells_end, min_cx, min_cz);

    for (int32_t cx = min_cx; cx <= max_cx && cur < cells_end; ++cx) {
        if (cur->cx > max_cx) break;
        if (cur->cx > cx) {
            cx = cur->cx; // Jump directly to next populated column!
            if (cx > max_cx) break;
        }
        if (cur->cx < cx || cur->cz < min_cz) {
            cur = lower_bound_cell(cur, cells_end, cx, min_cz);
        }
        while (cur < cells_end && cur->cx == cx && cur->cz <= max_cz) {
            bool overlap = true;
            if (!skip_box_check) {
                const float bx0 = static_cast<float>(cur->cx) * cell_size;
                const float bx1 = bx0 + cell_size;
                const float bz0 = static_cast<float>(cur->cz) * cell_size;
                const float bz1 = bz0 + cell_size;
                const float dx = (x < bx0) ? (bx0 - x) : (x > bx1 ? (x - bx1) : 0.0f);
                const float dz = (z < bz0) ? (bz0 - z) : (z > bz1 ? (z - bz1) : 0.0f);
                overlap = (dx * dx + dz * dz <= r_sq_eps);
            }
            if (overlap) {
                ++visited_cells;
                void** p = cur->ent_begin;
                void** e = cur->ent_end;
                while (p < e) {
                    if (!vis(visitor, *p)) {
                        g_stats.spatial_cells_visited.fetch_add(visited_cells, std::memory_order_relaxed);
                        return;
                    }
                    ++p;
                }
            }
            ++cur;
        }
    }
    g_stats.spatial_cells_visited.fetch_add(visited_cells, std::memory_order_relaxed);
}

void HookSpatialGridQuery(void* grid, const float* posXZ, float radius, void* visitor) {
    if (!tl_inReadOnlyFindEntities) {
        g_stats.spatial_queries.fetch_add(1, std::memory_order_relaxed);
        g_origSpatialGridQuery(grid, posXZ, radius, visitor);
        return;
    }
    RunZeroCopySpatialQuery(grid, posXZ, radius, visitor, g_entityVisitor, g_origSpatialGridQuery, false);
}

// 4 additional hot read-only server SpatialGrid::Query template instantiations (v1.3.0)
// complementing Module 5's FindEntities_CachedTag (5/5 total read-only server sites!):
//   [0] FindEntities / CountEntities (direct-tag)
//   [1] FindEntities_Registered      (combat / mobs / farms / boats)
//   [2] LightWatcher::GetLightAtPoint Intensity (mega-base multi-light hot path!)
//   [3] LightWatcher::GetLightAtPoint RGB
static constexpr size_t kNumDirectSqSites = 4;
SpatialGridQuery_t g_origDirectSq[kNumDirectSqSites] = {};
EntityVisitor_t    g_directSqVisitor[kNumDirectSqSites] = {};

template <size_t Slot>
void HookDirectSpatialQuery(void* grid, const float* posXZ, float radius, void* visitor) {
    RunZeroCopySpatialQuery(grid, posXZ, radius, visitor,
                            g_directSqVisitor[Slot], g_origDirectSq[Slot], Slot >= 2);
}

// =============================================================================
// Module 6 (v1.3.0): O(N log N) Flat-Hash + Binary-Heap Pathfinder AStar Engine
// =============================================================================
using AStarReset_t = void (*)(void* astar);
using AStarInit_t  = void (*)(void* astar, void* ctx);
using AStarStep_t  = uint32_t (*)(void* astar, uint32_t maxSteps);

AStarReset_t g_origAStarReset = nullptr;
AStarInit_t  g_origAStarInit  = nullptr;
AStarStep_t  g_origAStarStep  = nullptr;
fast_astar::EngineCallbacks g_astarCb{};
bool g_astarReady = false;

static void ClearKleiPathVec(void* pathVec) {
    if (!pathVec) return;
    auto* pv = reinterpret_cast<uint8_t*>(pathVec);
    // MSVC90 std::vector<TileCoord>: +0x08=begin, +0x10=end (exact match with Klei 0x1400b2adc..0x1400b2b1e)
    *reinterpret_cast<void**>(pv + 0x10) = *reinterpret_cast<void**>(pv + 0x08);
}

// Lock-guarded O(1) flat pointer table (AStar* -> SearchState*) + warm object pool (0 heap churn)
fast_astar::StateManager<64u> g_astarMgr;

void HookAStarReset(void* astar) {
    if (astar) {
        g_astarMgr.DetachAndRecycle(astar);
    }
    if (g_origAStarReset) {
        g_origAStarReset(astar);
    }
}

void HookAStarInit(void* astar, void* ctx) {
    if (!g_cfg.enable_fast_astar || !g_astarReady || !astar || !ctx) {
        if (astar) g_astarMgr.DetachAndRecycle(astar);
        if (g_origAStarInit) g_origAStarInit(astar, ctx);
        return;
    }

    // Clear any legacy Klei open/closed nodes and reset AStar fields cleanly
    if (g_origAStarReset) g_origAStarReset(astar);

    auto* ab = reinterpret_cast<uint8_t*>(astar);
    *reinterpret_cast<void**>(ab + 0x50)   = ctx;
    *reinterpret_cast<float*>(ab + 0x78)   = 0.0f;
    *reinterpret_cast<int32_t*>(ab + 0x08) = 0;

    auto handle = g_astarMgr.AcquireAndBind(astar);
    if (!handle) {
        g_stats.astar_fallbacks.fetch_add(1, std::memory_order_relaxed);
        if (g_origAStarInit) g_origAStarInit(astar, ctx);
        return;
    }

    const auto* cb = reinterpret_cast<const uint8_t*>(ctx);
    fast_astar::TileCoord start{};
    std::memcpy(&start, cb + 0x30, sizeof(fast_astar::TileCoord));
    const float h = g_astarCb.heuristic(ctx, &start);
    handle->Init(start, h);
    g_stats.astar_inits.fetch_add(1, std::memory_order_relaxed);
}

uint32_t HookAStarStep(void* astar, uint32_t maxSteps) {
    if (!astar) return 0u;
    auto* ab = reinterpret_cast<uint8_t*>(astar);
    if (*reinterpret_cast<int32_t*>(ab + 0x08) != 0) {
        return 0u;
    }

    auto handle = g_astarMgr.AcquireRef(astar);
    if (!handle || !g_cfg.enable_fast_astar || !g_astarReady) {
        handle.Release();
        g_stats.astar_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return g_origAStarStep ? g_origAStarStep(astar, maxSteps) : 0u;
    }

    auto* slotPtr = handle.Slot();
    const uint64_t slotGen = handle.Gen();
    void* ctx = *reinterpret_cast<void**>(ab + 0x50);
    if (!ctx) {
        handle.Release();
        g_astarMgr.DetachIfMatch(astar, slotPtr, slotGen);
        *reinterpret_cast<int32_t*>(ab + 0x08) = 2;
        return 0u;
    }

    fast_astar::TileCoord goal{};
    std::memcpy(&goal, reinterpret_cast<const uint8_t*>(ctx) + 0x3C, sizeof(fast_astar::TileCoord));

    const int64_t t0 = NowQpc();
    int32_t outStatus = 0;
    const uint32_t steps = handle->Step(ctx, goal, maxSteps, g_astarCb, ab + 0x58, outStatus);
    const double us = QpcDeltaUs(t0, NowQpc());

    *reinterpret_cast<float*>(ab + 0x78) += static_cast<float>(us * 1e-6);
    const uint64_t u64 = static_cast<uint64_t>(us);
    g_stats.astar_steps.fetch_add(steps, std::memory_order_relaxed);
    g_stats.astar_us_sum.fetch_add(u64, std::memory_order_relaxed);
    tl_tickProf.astarUs += ClampUsU32(u64);

    if (outStatus != 0) {
        *reinterpret_cast<int32_t*>(ab + 0x08) = outStatus;
        handle.Release();
        g_astarMgr.DetachIfMatch(astar, slotPtr, slotGen);
        if (outStatus == 1) {
            g_stats.astar_completed.fetch_add(1, std::memory_order_relaxed);
        } else {
            g_stats.astar_failed.fetch_add(1, std::memory_order_relaxed);
        }
    }
    return steps;
}

// =============================================================================
// Module 3: Adaptive Slack-Based Incremental GC + Anti-STW Full-GC Shield
// =============================================================================
using LuaGcFn_t = int (*)(void* L, int what, int data);
LuaGcFn_t                 g_origLuaGc = nullptr;
std::atomic<void*>        g_activeSimLuaState{nullptr};
std::atomic<void*>        g_activeSimGlobalState{nullptr}; // L->l_G (offset +0x20 in Lua 5.1.4 x64)
std::atomic<bool>         g_simStartedForGc{false};
std::atomic<bool>         g_gcPastWarmup{false};
std::atomic<DWORD>        g_mainSimThreadId{0};
anti_stw_gc::State        g_gcPolicyState;
std::atomic<uint32_t>     g_pendingExternalCatchup{0};

static inline void* ExtractLuaGlobalState(void* L) noexcept {
    if (!L) return nullptr;
    return *reinterpret_cast<void* const*>(static_cast<const uint8_t*>(L) + 0x20);
}

void ResetGcLifecycleForVmTeardown(void* closingL) {
    void* activeL = g_activeSimLuaState.load(std::memory_order_acquire);
    void* activeG = g_activeSimGlobalState.load(std::memory_order_acquire);
    void* closingG = ExtractLuaGlobalState(closingL);
    if (!closingL || closingL == activeL || (activeG && closingG == activeG)) {
        g_simStartedForGc.store(false, std::memory_order_release);
        g_gcPastWarmup.store(false, std::memory_order_release);
        g_activeSimLuaState.store(nullptr, std::memory_order_release);
        g_activeSimGlobalState.store(nullptr, std::memory_order_release);
        g_pendingExternalCatchup.store(0, std::memory_order_release);
    }
}

void HookLuaClose(void* L) {
    ResetGcLifecycleForVmTeardown(L);
    if (g_origLuaClose) {
        g_origLuaClose(L);
    }
}

int HookLuaGc(void* L, int what, int data) {
    if (!g_origLuaGc) return 0;
    // Intercept external Lua/Mod explicit collectgarbage("collect") nukes ONLY during steady-state
    // active Simulation gameplay on the main sim thread, past startup warmup, on the exact Sim VM!
    const DWORD mainTid = g_mainSimThreadId.load(std::memory_order_acquire);
    const void* activeG = g_activeSimGlobalState.load(std::memory_order_acquire);
    if (what == 2 /*LUA_GCCOLLECT*/ &&
        g_cfg.enable_anti_stw_gc &&
        g_setGcBudget != nullptr &&
        mainTid != 0 && GetCurrentThreadId() == mainTid &&
        g_simStartedForGc.load(std::memory_order_acquire) &&
        g_gcPastWarmup.load(std::memory_order_acquire) &&
        L != nullptr &&
        activeG != nullptr &&
        ExtractLuaGlobalState(L) == activeG) {
        const int64_t t0 = NowQpc();
        const bool is60Hz = (g_activeSimTickRate.load(std::memory_order_relaxed) == 60);
        g_setGcBudget(is60Hz ? 0.0020f : 0.0030f);
        g_origLuaGc(L, 5 /*LUA_GCSTEP*/, 0);
        g_pendingExternalCatchup.store(90, std::memory_order_release);
        g_stats.gc_stw_blocked.fetch_add(1, std::memory_order_relaxed);
        const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
        g_stats.gc_us_sum.fetch_add(us, std::memory_order_relaxed);
        g_stats.cur_tick_gc_us += us;
        tl_tickProf.gcUs += ClampUsU32(us);
        return 0;
    }
    return g_origLuaGc(L, what, data);
}

void HookCollectGarbage(void* sim) {
    const int64_t t0 = NowQpc();
    g_stats.gc_calls.fetch_add(1, std::memory_order_relaxed);

    if (!g_cfg.enable_adaptive_gc || !g_lua.gc || !g_setGcBudget || !sim) {
        g_origCollectGarbage(sim);
        const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
        g_stats.gc_us_sum.fetch_add(us, std::memory_order_relaxed);
        g_stats.cur_tick_gc_us += us;
        tl_tickProf.gcUs += ClampUsU32(us);
        uint64_t prev = g_stats.gc_us_max.load(std::memory_order_relaxed);
        while (us > prev && !g_stats.gc_us_max.compare_exchange_weak(prev, us)) {}
        return;
    }

    auto* simBytes = reinterpret_cast<uint8_t*>(sim);
    void* L = *reinterpret_cast<void**>(simBytes + 0xB0);
    if (!L) {
        g_origCollectGarbage(sim);
        return;
    }
    g_activeSimLuaState.store(L, std::memory_order_release);
    g_activeSimGlobalState.store(ExtractLuaGlobalState(L), std::memory_order_release);

    const uint32_t extArm = g_pendingExternalCatchup.exchange(0, std::memory_order_acquire);
    if (extArm > 0) {
        g_gcPolicyState.ArmForcedCatchup(extArm);
    }

    int32_t* pThresholdKb = reinterpret_cast<int32_t*>(simBytes + 0x24C);
    const int32_t curKb = g_lua.gc(L, 3 /*LUA_GCCOUNT*/, 0);
    g_stats.gc_last_kb.store(curKb, std::memory_order_relaxed);

    anti_stw_gc::Config pcfg{};
    pcfg.gc_spare_target_ms  = g_gcSpareTargetMs.load(std::memory_order_relaxed);
    pcfg.gc_calm_max_ms      = g_gcCalmMaxMs.load(std::memory_order_relaxed);
    pcfg.gc_busy_min_ms      = g_gcBusyMinMs.load(std::memory_order_relaxed);
    pcfg.gc_emergency_add_mb = g_gcEmergencyAddMb.load(std::memory_order_relaxed);
    pcfg.gc_excess_ramp_mb   = g_gcExcessRampMb.load(std::memory_order_relaxed);
    pcfg.enable_anti_stw_gc  = g_cfg.enable_anti_stw_gc;

    const bool is60Hz = (g_activeSimTickRate.load(std::memory_order_relaxed) == 60);
    const anti_stw_gc::Decision dec = g_gcPolicyState.Evaluate(
        L, curKb, *pThresholdKb, g_stats.last_tick_non_gc_ms, is60Hz, pcfg);
    g_gcPastWarmup.store(g_gcPolicyState.IsPastWarmup(), std::memory_order_release);
    *pThresholdKb = dec.new_threshold_kb;

    if (dec.run_hard_full_gc) {
        // Last-resort runaway OOM circuit-breaker (calls direct unhooked g_lua.gc)
        g_lua.gc(L, 2 /*LUA_GCCOLLECT*/, 0);
        g_stats.gc_full_count.fetch_add(1, std::memory_order_relaxed);
        tl_tickProf.gcFull = 1;
        const int32_t postKb = g_lua.gc(L, 3 /*LUA_GCCOUNT*/, 0);
        g_gcPolicyState.OnHardFullGcCompleted(postKb);
        *pThresholdKb = std::max<int32_t>(postKb * 2, postKb + g_cfg.gc_emergency_add_mb * 1024);
        g_stats.gc_last_kb.store(postKb, std::memory_order_relaxed);
    }

    g_stats.gc_last_budget_us.store(static_cast<int32_t>(dec.step_budget_ms * 1000.0f), std::memory_order_relaxed);
    g_setGcBudget(dec.step_budget_ms * 0.001f);
    const int cycleDone = g_lua.gc(L, 5 /*LUA_GCSTEP*/, 0);
    if (cycleDone != 0) {
        const int32_t postKb = g_lua.gc(L, 3 /*LUA_GCCOUNT*/, 0);
        g_gcPolicyState.OnCycleCompleted(postKb);
        if (postKb > 0) {
            g_stats.gc_last_kb.store(postKb, std::memory_order_relaxed);
        }
    }

    const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
    g_stats.gc_us_sum.fetch_add(us, std::memory_order_relaxed);
    g_stats.cur_tick_gc_us += us;
    tl_tickProf.gcUs += ClampUsU32(us);
    uint64_t prev = g_stats.gc_us_max.load(std::memory_order_relaxed);
    while (us > prev && !g_stats.gc_us_max.compare_exchange_weak(prev, us)) {}
}

// =============================================================================
// Module 4A: SIMD libdeflate Replacement for Scalar zlib compress2 (Level 9 -> Fast SIMD!)
// =============================================================================
struct TlsDeflateCtx {
    libdeflate_compressor* comp = nullptr;
    int level = 0;
    ~TlsDeflateCtx() {
        if (comp) libdeflate_free_compressor(comp);
    }
};
thread_local TlsDeflateCtx tl_deflate;

int HookCompress2(uint8_t* dest, uint32_t* destLen,
                  const uint8_t* source, uint32_t sourceLen, int level) {
    const int64_t t0 = NowQpc();
    g_stats.deflate_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.deflate_in_bytes.fetch_add(sourceLen, std::memory_order_relaxed);

    if (g_cfg.enable_libdeflate && dest && destLen && source && *destLen > 0) {
        const int targetLevel = g_effectiveDeflateLevel.load(std::memory_order_relaxed);
        if (!tl_deflate.comp || tl_deflate.level != targetLevel) {
            if (tl_deflate.comp) libdeflate_free_compressor(tl_deflate.comp);
            tl_deflate.comp = libdeflate_alloc_compressor(targetLevel);
            tl_deflate.level = targetLevel;
        }
        if (tl_deflate.comp) {
            size_t outSz = libdeflate_zlib_compress(tl_deflate.comp, source, sourceLen, dest, *destLen);
            if (outSz > 0 && outSz <= *destLen) {
                *destLen = static_cast<uint32_t>(outSz);
                const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
                g_stats.deflate_out_bytes.fetch_add(outSz, std::memory_order_relaxed);
                g_stats.deflate_us_sum.fetch_add(us, std::memory_order_relaxed);
                const DWORD mainTid = g_mainSimThreadId.load(std::memory_order_relaxed);
                if (mainTid != 0 && GetCurrentThreadId() == mainTid) {
                    g_stats.deflate_main_calls.fetch_add(1, std::memory_order_relaxed);
                    g_stats.deflate_main_us_sum.fetch_add(us, std::memory_order_relaxed);
                    if (tl_tickProf.saveDepth == 0) tl_tickProf.saveUs += ClampUsU32(us);
                }
                return 0; // Z_OK
            }
        }
    }

    int rc = g_origCompress2(dest, destLen, source, sourceLen, level);
    if (rc == 0 && destLen) {
        g_stats.deflate_out_bytes.fetch_add(*destLen, std::memory_order_relaxed);
    }
    const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
    g_stats.deflate_us_sum.fetch_add(us, std::memory_order_relaxed);
    const DWORD mainTid = g_mainSimThreadId.load(std::memory_order_relaxed);
    if (mainTid != 0 && GetCurrentThreadId() == mainTid) {
        g_stats.deflate_main_calls.fetch_add(1, std::memory_order_relaxed);
        g_stats.deflate_main_us_sum.fetch_add(us, std::memory_order_relaxed);
        if (tl_tickProf.saveDepth == 0) tl_tickProf.saveUs += ClampUsU32(us);
    }
    return rc;
}

int HookUncompress(uint8_t* dest, uint32_t* destLen, const uint8_t* source, uint32_t sourceLen) {
    if (g_cfg.enable_libdeflate && dest && destLen && source && sourceLen > 0 && *destLen > 0) {
        static thread_local struct TlsZDecomp {
            libdeflate_decompressor* d = nullptr;
            ~TlsZDecomp() { if (d) libdeflate_free_decompressor(d); }
        } tl_dec;
        if (!tl_dec.d) tl_dec.d = libdeflate_alloc_decompressor();
        if (tl_dec.d) {
            const int64_t t0 = NowQpc();
            size_t actualOut = 0;
            if (libdeflate_zlib_decompress(tl_dec.d, source, static_cast<size_t>(sourceLen),
                                           dest, static_cast<size_t>(*destLen), &actualOut) == LIBDEFLATE_SUCCESS &&
                actualOut <= 0xFFFFFFFFULL) {
                const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
                *destLen = static_cast<uint32_t>(actualOut);
                g_stats.uncomp_calls.fetch_add(1, std::memory_order_relaxed);
                g_stats.uncomp_out_bytes.fetch_add(actualOut, std::memory_order_relaxed);
                g_stats.uncomp_us_sum.fetch_add(us, std::memory_order_relaxed);
                return 0; // Z_OK
            }
        }
    }
    return g_origUncompress ? g_origUncompress(dest, destLen, source, sourceLen) : -3;
}

// =============================================================================
// Module 4C: Server FastLoad Engine (libdeflate v1.23 unzReadCurrentFile + 64-bit inflate_fast)
// Exact binary parity verified against dontstarve_dedicated_server_nullrenderer_x64.exe (0/309 insn diffs).
// =============================================================================
using UnzReadCurrentFile_t = int (*)(void* file, void* buf, uint32_t len);
using ZReadFileFunc_t      = uint32_t (*)(void* opaque, void* stream, void* buf, uint32_t size);
using ZSeekFileFunc_t      = int32_t (*)(void* opaque, void* stream, uint32_t offset, int origin);

UnzReadCurrentFile_t g_origUnzRead = nullptr;

struct ServerUnzReadInfo {
    void* read_buffer;              // +0x00
    const uint8_t* next_in;         // +0x08
    uint32_t avail_in;              // +0x10
    uint32_t total_in;              // +0x14
    uint8_t* next_out;              // +0x18
    uint32_t avail_out;             // +0x20
    uint32_t total_out;             // +0x24
    uint8_t pad_stream[0x38];       // +0x28..+0x5F
    uint32_t pos_in_zipfile;        // +0x60
    uint8_t pad_mid[0x18];          // +0x64..+0x7B
    uint32_t rest_read_compressed;  // +0x7C
    uint32_t rest_read_uncompressed;// +0x80
    uint8_t pad_io[0x14];           // +0x84..+0x97
    ZReadFileFunc_t zread_file;     // +0x98
    uint8_t pad_io2[0x10];          // +0xA0..+0xAF
    ZSeekFileFunc_t zseek_file;     // +0xB0
    uint8_t pad_io3[0x10];          // +0xB8..+0xC7
    void* opaque;                   // +0xC8
    void* filestream;               // +0xD0
    int32_t compression_method;     // +0xD8
    uint32_t byte_before_zipfile;   // +0xDC
    int32_t raw;                    // +0xE0
};

struct TlsDecompCtx {
    libdeflate_decompressor* decomp = nullptr;
    std::vector<uint8_t> compBuf;
    ~TlsDecompCtx() {
        if (decomp) { libdeflate_free_decompressor(decomp); decomp = nullptr; }
    }
};

extern "C" int HookUnzReadCurrentFile(void* file, void* buf, uint32_t len) {
    if (!file) return -102; // UNZ_PARAMERROR (mirrors Klei prologue check before trampoline +0x16)
    auto* pinfo = *reinterpret_cast<ServerUnzReadInfo**>(static_cast<uint8_t*>(file) + 0xC8);
    if (!pinfo || !pinfo->read_buffer || len == 0 || !buf) {
        return g_origUnzRead(file, buf, len);
    }

    const uint32_t compSize   = pinfo->rest_read_compressed;
    const uint32_t uncompSize = pinfo->rest_read_uncompressed;

    if (pinfo->compression_method == 8 && pinfo->raw == 0 &&
        pinfo->avail_in == 0 && pinfo->total_out == 0 &&
        compSize > 0 && compSize <= (64u * 1024u * 1024u) &&
        uncompSize > 0 && uncompSize <= static_cast<uint32_t>(INT32_MAX) && len >= uncompSize &&
        pinfo->zseek_file && pinfo->zread_file) {

        const int64_t t0 = NowQpc();
        thread_local TlsDecompCtx tls;
        if (!tls.decomp) tls.decomp = libdeflate_alloc_decompressor();
        if (tls.decomp) {
            if (tls.compBuf.size() < compSize) tls.compBuf.resize(compSize);
            const uint32_t offset = pinfo->pos_in_zipfile + pinfo->byte_before_zipfile;
            if (pinfo->zseek_file(pinfo->opaque, pinfo->filestream, offset, 0) == 0 &&
                pinfo->zread_file(pinfo->opaque, pinfo->filestream, tls.compBuf.data(), compSize) == compSize) {
                size_t actualOut = 0;
                if (libdeflate_deflate_decompress(tls.decomp, tls.compBuf.data(), compSize,
                                                  buf, uncompSize, &actualOut) == LIBDEFLATE_SUCCESS &&
                    actualOut == uncompSize) {
                    pinfo->pos_in_zipfile += compSize;
                    pinfo->rest_read_compressed = 0;
                    pinfo->rest_read_uncompressed = 0;
                    pinfo->next_out = static_cast<uint8_t*>(buf) + uncompSize;
                    pinfo->avail_out = 0;
                    pinfo->total_out += uncompSize;
                    g_stats.unz_hits.fetch_add(1, std::memory_order_relaxed);
                    g_stats.unz_bytes.fetch_add(uncompSize, std::memory_order_relaxed);
                    g_stats.unz_us_sum.fetch_add(static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc())), std::memory_order_relaxed);
                    return static_cast<int>(uncompSize);
                }
            }
        }
    } else if ((pinfo->compression_method == 0 || pinfo->raw != 0) &&
               pinfo->avail_in == 0 && uncompSize > 0 && uncompSize <= static_cast<uint32_t>(INT32_MAX) &&
               uncompSize == compSize && len >= uncompSize &&
               pinfo->zseek_file && pinfo->zread_file) {
        const uint32_t offset = pinfo->pos_in_zipfile + pinfo->byte_before_zipfile;
        if (pinfo->zseek_file(pinfo->opaque, pinfo->filestream, offset, 0) == 0 &&
            pinfo->zread_file(pinfo->opaque, pinfo->filestream, buf, uncompSize) == uncompSize) {
            pinfo->pos_in_zipfile += uncompSize;
            pinfo->rest_read_compressed = 0;
            pinfo->rest_read_uncompressed = 0;
            pinfo->next_out = static_cast<uint8_t*>(buf) + uncompSize;
            pinfo->avail_out = 0;
            pinfo->total_out += uncompSize;
            return static_cast<int>(uncompSize);
        }
    }

    return g_origUnzRead(file, buf, len);
}

struct ZlibCode { uint8_t op; uint8_t bits; uint16_t val; };
struct ZlibInflateState {
    int32_t mode;
    uint8_t pad0[0x28];
    uint32_t wsize;
    uint32_t whave;
    uint32_t write;
    uint8_t* window;
    uint32_t hold;
    uint32_t bits;
    uint8_t pad1[0x10];
    const ZlibCode* lcode;
    const ZlibCode* dcode;
    uint32_t lenbits;
    uint32_t distbits;
};
struct ZlibStream {
    const uint8_t* next_in;
    uint32_t avail_in;
    uint32_t pad0;
    uint8_t* next_out;
    uint32_t avail_out;
    uint32_t pad1;
    const char* msg;
    ZlibInflateState* state;
};

static inline void FastCopyMatch(uint8_t* out, const uint8_t* from, uint32_t len, uint32_t dist) {
    if (dist >= 16) {
        while (len >= 16) { std::memcpy(out, from, 16); out += 16; from += 16; len -= 16; }
        if (len >= 8) { std::memcpy(out, from, 8); out += 8; from += 8; len -= 8; }
        while (len--) *out++ = *from++;
    } else if (dist >= 8) {
        while (len >= 8) { std::memcpy(out, from, 8); out += 8; from += 8; len -= 8; }
        while (len--) *out++ = *from++;
    } else if (dist == 1) {
        uint8_t b = *from;
        uint64_t v = static_cast<uint64_t>(b) * 0x0101010101010101ULL;
        while (len >= 8) { std::memcpy(out, &v, 8); out += 8; len -= 8; }
        while (len--) *out++ = b;
    } else {
        while (len--) *out++ = *from++;
    }
}

extern "C" void HookInflateFast(ZlibStream* strm, uint32_t start) {
    ZlibInflateState* state = strm->state;
    const uint8_t* in = strm->next_in;
    const uint8_t* in_end = in + strm->avail_in;
    const uint8_t* last = in + (strm->avail_in - 5);
    uint8_t* out = strm->next_out;
    uint8_t* out_end = out + strm->avail_out;
    uint8_t* beg = out - (start - strm->avail_out);
    uint8_t* end = out + (strm->avail_out - 257);

    uint32_t wsize = state->wsize, whave = state->whave, write = state->write;
    uint8_t* window = state->window;
    uint64_t hold = static_cast<uint64_t>(state->hold);
    uint32_t bits = state->bits;
    const ZlibCode* lcode = state->lcode;
    const ZlibCode* dcode = state->dcode;
    uint64_t lmask = (1ULL << state->lenbits) - 1;
    uint64_t dmask = (1ULL << state->distbits) - 1;

    do {
        if (bits < 15) {
            uint32_t chunk;
            std::memcpy(&chunk, in, 4);
            hold |= static_cast<uint64_t>(chunk) << bits;
            in += 4;
            bits += 32;
        }
        ZlibCode here = lcode[hold & lmask];
    dolen:
        uint32_t op = here.bits;
        hold >>= op;
        bits -= op;
        op = here.op;
        if (op == 0) {
            *out++ = static_cast<uint8_t>(here.val);
        } else if (op & 16) {
            uint32_t len = here.val;
            op &= 15;
            if (op) {
                if (bits < op && in < in_end) { hold |= static_cast<uint64_t>(*in++) << bits; bits += 8; }
                len += static_cast<uint32_t>(hold & ((1ULL << op) - 1));
                hold >>= op;
                bits -= op;
            }
            if (bits < 15) {
                if (in + 4 <= in_end) {
                    uint32_t chunk; std::memcpy(&chunk, in, 4);
                    hold |= static_cast<uint64_t>(chunk) << bits; in += 4; bits += 32;
                } else {
                    while (bits < 15 && in < in_end) { hold |= static_cast<uint64_t>(*in++) << bits; bits += 8; }
                }
            }
            here = dcode[hold & dmask];
        dodist:
            op = here.bits; hold >>= op; bits -= op; op = here.op;
            if (op & 16) {
                uint32_t dist = here.val;
                op &= 15;
                if (bits < op) {
                    if (in < in_end) { hold |= static_cast<uint64_t>(*in++) << bits; bits += 8; }
                    if (bits < op && in < in_end) { hold |= static_cast<uint64_t>(*in++) << bits; bits += 8; }
                }
                dist += static_cast<uint32_t>(hold & ((1ULL << op) - 1));
                hold >>= op; bits -= op;
                uint32_t avail_back = static_cast<uint32_t>(out - beg);
                if (dist > avail_back) {
                    uint32_t back = dist - avail_back;
                    if (back > whave) { strm->msg = "invalid distance too far back"; state->mode = 27; break; }
                    const uint8_t* from = window;
                    if (write == 0) {
                        from += wsize - back;
                        if (back < len) { len -= back; std::memcpy(out, from, back); out += back; from = out - dist; }
                    } else if (write < back) {
                        from += wsize + write - back; back -= write;
                        if (back < len) {
                            len -= back; std::memcpy(out, from, back); out += back; from = window;
                            if (write < len) { uint32_t wlen = write; len -= wlen; std::memcpy(out, from, wlen); out += wlen; from = out - dist; }
                        }
                    } else {
                        from += write - back;
                        if (back < len) { len -= back; std::memcpy(out, from, back); out += back; from = out - dist; }
                    }
                    if (from >= window && from < window + wsize) { std::memcpy(out, from, len); out += len; }
                    else { FastCopyMatch(out, from, len, dist); out += len; }
                } else {
                    FastCopyMatch(out, out - dist, len, dist);
                    out += len;
                }
            } else if ((op & 64) == 0) {
                here = dcode[here.val + (hold & ((1ULL << op) - 1))]; goto dodist;
            } else { strm->msg = "invalid distance code"; state->mode = 27; break; }
        } else if ((op & 64) == 0) {
            here = lcode[here.val + (hold & ((1ULL << op) - 1))]; goto dolen;
        } else if (op & 32) {
            state->mode = 11; break;
        } else { strm->msg = "invalid literal/length code"; state->mode = 27; break; }
    } while (in < last && out < end);

    uint32_t extra_bytes = bits >> 3;
    in -= extra_bytes;
    bits -= extra_bytes << 3;
    hold &= (1ULL << bits) - 1;
    strm->next_in = in;
    strm->next_out = out;
    strm->avail_in = static_cast<uint32_t>(in_end - in);
    strm->avail_out = static_cast<uint32_t>(out_end - out);
    state->hold = static_cast<uint32_t>(hold);
    state->bits = bits;
}

// =============================================================================
// Module 4B: C++ Native FastDataDumper + Lua Telemetry Bridge
// =============================================================================
thread_local fast_dumper::GrowBuf tl_dumpBuf;

int Lua_HyperCore_FastDump(void* L) {
    if (!g_cfg.enable_fast_dumper || !g_lua.ReadyForFastDump()) {
        if (g_lua.pushnil && g_lua.pushstring) {
            g_lua.pushnil(L);
            g_lua.pushstring(L, "disabled");
            return 2;
        }
        return 0;
    }

    SaveSubphaseGuard saveGuard;
    const int64_t t0 = NowQpc();
    const int nargs = g_lua.gettop(L);
    if (nargs < 1) {
        g_lua.pushnil(L);
        g_lua.pushstring(L, "no_arg");
        return 2;
    }

    const char* varname = nullptr;
    size_t varlen = 0;
    if (nargs >= 2 && g_lua.type(L, 2) == HC_LUA_TSTRING) {
        varname = g_lua.tolstring(L, 2, &varlen);
    }

    const char* failReason = nullptr;
    const uint32_t dumpMaxNodes = g_fastDumpMaxNodes.load(std::memory_order_relaxed);
    const size_t dumpKeepBytes = g_fastDumpKeepBytes.load(std::memory_order_relaxed);
    auto st = fast_dumper::DumpRoot(L, g_lua, 1, varname, varlen, tl_dumpBuf, &failReason, dumpMaxNodes);
    const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));

    if (st == fast_dumper::Status::Ok && !tl_dumpBuf.oom && tl_dumpBuf.data) {
        saveGuard.Add(us);
        g_stats.fastdump_calls.fetch_add(1, std::memory_order_relaxed);
        g_stats.fastdump_us_sum.fetch_add(us, std::memory_order_relaxed);
        g_stats.fastdump_bytes_sum.fetch_add(tl_dumpBuf.len, std::memory_order_relaxed);
        g_lua.pushlstring(L, tl_dumpBuf.data, tl_dumpBuf.len);
        tl_dumpBuf.ShrinkIfLarge(dumpKeepBytes);
        return 1;
    }

    tl_dumpBuf.ShrinkIfLarge(dumpKeepBytes);
    g_stats.fastdump_fallbacks.fetch_add(1, std::memory_order_relaxed);
    g_lua.pushnil(L);
    g_lua.pushstring(L, failReason ? failReason : (st == fast_dumper::Status::CorruptNumber ? "corrupt_number" : "fallback"));
    return 2;
}

int Lua_HyperCore_GetStats(void* L) {
    if (!g_lua.ReadyForRegister()) return 0;
    double avg = 0, p95 = 0, p99 = 0;
    g_stats.ComputePercentiles(avg, p95, p99);

    g_lua.createtable(L, 0, 72);
    auto setNum = [&](const char* k, double v) {
        g_lua.pushnumber(L, v);
        g_lua.setfield(L, -2, k);
    };
    auto setBool = [&](const char* k, bool v) {
        g_lua.pushboolean(L, v ? 1 : 0);
        g_lua.setfield(L, -2, k);
    };

    setNum("ticks_total",           static_cast<double>(g_stats.ticks_total.load()));
    setNum("tick_avg_us",           avg);
    setNum("tick_p95_us",           p95);
    setNum("tick_p99_us",           p99);
    setNum("tick_max_us",           static_cast<double>(g_stats.tick_us_max.load()));
    setNum("tick_overruns",         static_cast<double>(g_stats.tick_overrun_count.load()));
    setNum("gc_calls",              static_cast<double>(g_stats.gc_calls.load()));
    setNum("gc_us_sum",             static_cast<double>(g_stats.gc_us_sum.load()));
    setNum("gc_max_us",             static_cast<double>(g_stats.gc_us_max.load()));
    setNum("gc_full_count",         static_cast<double>(g_stats.gc_full_count.load()));
    setNum("gc_stw_blocked",        static_cast<double>(g_stats.gc_stw_blocked.load()));
    setNum("gc_last_kb",            static_cast<double>(g_stats.gc_last_kb.load()));
    setNum("gc_last_budget_us",     static_cast<double>(g_stats.gc_last_budget_us.load()));
    setNum("find_entities_calls",   static_cast<double>(g_stats.find_entities_calls.load()));
    setNum("find_entities_us_sum",  static_cast<double>(g_stats.find_entities_us_sum.load()));
    setNum("spatial_queries",       static_cast<double>(g_stats.spatial_queries.load()));
    setNum("spatial_zerocopy_hits", static_cast<double>(g_stats.spatial_zerocopy_hits.load()));
    setNum("spatial_cells_visited", static_cast<double>(g_stats.spatial_cells_visited.load()));
    setNum("light_spatial_queries", static_cast<double>(g_stats.light_spatial_queries.load()));
    setNum("astar_inits",           static_cast<double>(g_stats.astar_inits.load()));
    setNum("astar_steps",           static_cast<double>(g_stats.astar_steps.load()));
    setNum("astar_us_sum",          static_cast<double>(g_stats.astar_us_sum.load()));
    setNum("astar_completed",       static_cast<double>(g_stats.astar_completed.load()));
    setNum("astar_failed",          static_cast<double>(g_stats.astar_failed.load()));
    setNum("astar_fallbacks",       static_cast<double>(g_stats.astar_fallbacks.load()));
    setNum("mi_allocs",             static_cast<double>(g_stats.mi_allocs.load()));
    setNum("mi_reallocs",           static_cast<double>(g_stats.mi_reallocs.load()));
    setNum("mi_frees",              static_cast<double>(g_stats.mi_frees.load()));
    setNum("fastdump_calls",        static_cast<double>(g_stats.fastdump_calls.load()));
    setNum("fastdump_us_sum",       static_cast<double>(g_stats.fastdump_us_sum.load()));
    setNum("fastdump_bytes_sum",    static_cast<double>(g_stats.fastdump_bytes_sum.load()));
    setNum("fastdump_fallbacks",    static_cast<double>(g_stats.fastdump_fallbacks.load()));
    setNum("deflate_calls",         static_cast<double>(g_stats.deflate_calls.load()));
    setNum("deflate_us_sum",        static_cast<double>(g_stats.deflate_us_sum.load()));
    setNum("deflate_main_calls",    static_cast<double>(g_stats.deflate_main_calls.load()));
    setNum("deflate_main_us_sum",   static_cast<double>(g_stats.deflate_main_us_sum.load()));
    setNum("deflate_in_bytes",      static_cast<double>(g_stats.deflate_in_bytes.load()));
    setNum("deflate_out_bytes",     static_cast<double>(g_stats.deflate_out_bytes.load()));
    setNum("uncomp_calls",          static_cast<double>(g_stats.uncomp_calls.load()));
    setNum("uncomp_out_bytes",      static_cast<double>(g_stats.uncomp_out_bytes.load()));
    setNum("uncomp_us_sum",         static_cast<double>(g_stats.uncomp_us_sum.load()));
    setNum("fastjson_enc_calls",    static_cast<double>(g_stats.fastjson_enc_calls.load()));
    setNum("fastjson_dec_calls",    static_cast<double>(g_stats.fastjson_dec_calls.load()));
    setNum("fastjson_fallbacks",    static_cast<double>(g_stats.fastjson_fallbacks.load()));
    setNum("serialize_world_calls", static_cast<double>(g_stats.serialize_world_calls.load()));
    setNum("serialize_world_us_sum",static_cast<double>(g_stats.serialize_world_us_sum.load()));
    setNum("prof_lua_update_calls", static_cast<double>(g_stats.prof_lua_update_calls.load()));
    setNum("prof_lua_update_us_sum",static_cast<double>(g_stats.prof_lua_update_us_sum.load()));
    setNum("prof_lua_update_us_max",static_cast<double>(g_stats.prof_lua_update_us_max.load()));
    setNum("prof_net_update_calls", static_cast<double>(g_stats.prof_net_update_calls.load()));
    setNum("prof_net_update_us_sum",static_cast<double>(g_stats.prof_net_update_us_sum.load()));
    setNum("prof_net_update_us_max",static_cast<double>(g_stats.prof_net_update_us_max.load()));
    setNum("prof_replica_update_calls",  static_cast<double>(g_stats.prof_replica_update_calls.load()));
    setNum("prof_replica_update_us_sum", static_cast<double>(g_stats.prof_replica_update_us_sum.load()));
    setNum("prof_replica_update_us_max", static_cast<double>(g_stats.prof_replica_update_us_max.load()));
    setNum("fs_neg_hits",           static_cast<double>(g_stats.fs_neg_hits.load()));
    setNum("fs_neg_misses",         static_cast<double>(g_stats.fs_neg_misses.load()));
    setNum("async_save_enqueued",   static_cast<double>(GetAsyncSave().EnqueuedCount()));
    setNum("async_save_committed",  static_cast<double>(GetAsyncSave().CommittedCount()));
    setNum("async_save_bytes",      static_cast<double>(GetAsyncSave().CommittedBytes()));
    setNum("async_save_compressed", static_cast<double>(GetAsyncSave().CompressedCount()));
    setNum("async_save_compress_us_sum", static_cast<double>(GetAsyncSave().CompressUsSum()));
    setNum("async_save_flushes",    static_cast<double>(GetAsyncSave().FlushCount()));
    setNum("async_save_fallbacks",  static_cast<double>(g_stats.async_save_fallbacks.load()));
    setNum("async_save_errors",     static_cast<double>(GetAsyncSave().ErrorCount()));
    setNum("async_save_enqueue_us_sum", static_cast<double>(GetAsyncSave().EnqueueUsSum()));
    setNum("async_save_io_us_sum",  static_cast<double>(GetAsyncSave().IoUsSum()));
    setNum("rpc_qos_imm",           static_cast<double>(g_rpcQos.ImmCount()));
    setNum("rpc_qos_off_ch0",       static_cast<double>(g_rpcQos.OffCh0Count()));
    setNum("rpc_qos_total",         static_cast<double>(g_rpcQos.TotalCalls()));

    setBool("opt_mimalloc",         g_cfg.enable_mimalloc);
    setBool("opt_spatial_zerocopy", g_cfg.enable_spatial_zerocopy);
    setBool("opt_adaptive_gc",      g_cfg.enable_adaptive_gc);
    setBool("opt_anti_stw_gc",      g_cfg.enable_anti_stw_gc);
    setBool("opt_libdeflate",       g_cfg.enable_libdeflate);
    setBool("opt_fast_dumper",      g_cfg.enable_fast_dumper);
    setBool("opt_fast_astar",       g_cfg.enable_fast_astar && g_astarReady);
    setBool("opt_fs_neg_cache",     g_cfg.enable_fs_neg_cache && g_fsNegCacheActive.load(std::memory_order_relaxed));
    setBool("opt_async_save",       g_cfg.enable_async_save && g_asyncSaveActive.load(std::memory_order_relaxed));
    setBool("opt_rpc_qos",          g_cfg.enable_rpc_qos && g_rpcQosActive.load(std::memory_order_relaxed));
    setBool("opt_replica_fastpath", g_cfg.enable_replica_fastpath && g_replicaFastActive.load(std::memory_order_relaxed));
    setNum("replica_construct_budget",       static_cast<double>(g_effectiveReplicaBudget.load(std::memory_order_relaxed)));
    setBool("replica_proximity_active",      g_replicaCfg.proximityValid);
    setNum("replica_qrl_calls",              static_cast<double>(g_replicaStats.qrl_calls.load()));
    setNum("replica_constructed_total",      static_cast<double>(g_replicaStats.constructed_total.load()));
    setNum("replica_destroyed_total",        static_cast<double>(g_replicaStats.destroyed_total.load()));
    setNum("replica_paced_deferred_total",   static_cast<double>(g_replicaStats.paced_deferred_total.load()));
    setNum("replica_paced_burst_ticks",      static_cast<double>(g_replicaStats.paced_burst_ticks.load()));
    setNum("replica_paced_parent_promotes",  static_cast<double>(g_replicaStats.paced_parent_promotes.load()));
    setNum("replica_paced_near_bypasses",    static_cast<double>(g_replicaStats.paced_near_bypasses.load()));
    setNum("sim_tick_rate",         static_cast<double>(g_activeSimTickRate.load(std::memory_order_relaxed)));
    setNum("hardware_tier",         static_cast<double>(g_activeServerTier.load(std::memory_order_relaxed)));
    setNum("hw_cores",              static_cast<double>(g_hostCaps.logicalCores));
    setNum("hw_ram_mb",             static_cast<double>(g_hostCaps.totalRamMb));
    setNum("hw_ideal_core",         static_cast<double>(g_idealCoreApplied.load(std::memory_order_relaxed)));
    return 1;
}

int Lua_HyperCore_ResetStats(void* /*L*/) {
    g_stats.Reset();
    GetAsyncSave().ResetTelemetry();
    return 0;
}

SRWLOCK  g_simTickLock         = SRWLOCK_INIT;
uint32_t g_savedPre60NetRate   = 0; // saved before first 30->60 transition (0 = unset)
bool     g_hasSavedPre60NetRate = false;

bool PatchFloatProtected(float* ptr, float val) {
    if (!ptr) return false;
    DWORD oldProt = 0, dummy = 0;
    if (!VirtualProtect(ptr, sizeof(float), PAGE_READWRITE, &oldProt)) return false;
    *ptr = val;
    if (!VirtualProtect(ptr, sizeof(float), oldProt, &dummy)) {
        LogMsg("[WARN] PatchFloatProtected @ %p failed restoring prot 0x%lX",
               static_cast<void*>(ptr), static_cast<unsigned long>(oldProt));
    }
    return true;
}

bool PatchDoubleProtected(double* ptr, double val) {
    if (!ptr) return false;
    DWORD oldProt = 0, dummy = 0;
    if (!VirtualProtect(ptr, sizeof(double), PAGE_READWRITE, &oldProt)) return false;
    *ptr = val;
    if (!VirtualProtect(ptr, sizeof(double), oldProt, &dummy)) {
        LogMsg("[WARN] PatchDoubleProtected @ %p failed restoring prot 0x%lX",
               static_cast<void*>(ptr), static_cast<unsigned long>(oldProt));
    }
    return true;
}

void SyncServerNetworkTickRateForSimHz(int hz) {
    if (!g_netMgrGlobalPtr || !g_fnSetNetTickRate) return;
    auto* netMgr = static_cast<uint8_t*>(*g_netMgrGlobalPtr);
    if (!netMgr) return;
    void* replicaMgr = *reinterpret_cast<void**>(netMgr + 0x288);
    if (!replicaMgr) return;

    const uint32_t curRate = *reinterpret_cast<uint32_t*>(netMgr + 0xD8);
    uint32_t desiredRate = curRate;
    if (hz == 60) {
        // Snapshot pre-60Hz rate ONCE so switching 60 -> 30 restores exact vanilla/custom rate!
        if (!g_hasSavedPre60NetRate) {
            g_savedPre60NetRate = (curRate != 0) ? curRate : 15u;
            g_hasSavedPre60NetRate = true;
        }
        // Default 15Hz -> upgrade to native 60Hz (U=60(1)) so server snapshots match 60Hz SimTick
        // AND joining clients automatically disable RakNet download throttling (1000/60 <= 16.67ms -> D=0)!
        // Respect explicit admin cluster.ini overrides (e.g., 20/30/60).
        desiredRate = (curRate == 0 || curRate == 15) ? 60u : curRate;
        g_netTickSynced60.store(true, std::memory_order_release);
    } else {
        desiredRate = g_hasSavedPre60NetRate ? g_savedPre60NetRate : ((curRate != 0) ? curRate : 15u);
        g_hasSavedPre60NetRate = false;
        g_savedPre60NetRate = 0;
        g_netTickSynced60.store(false, std::memory_order_release);
    }
    g_fnSetNetTickRate(netMgr, desiredRate);
    const uint8_t ticksPerSnap = *(static_cast<uint8_t*>(replicaMgr) + 0x89D);
    LogMsg("[SIMTICK] Synced Server NetworkTickRate -> U=%u(%u) for %dHz SimTick",
           desiredRate, static_cast<unsigned>(ticksPerSnap), hz);
}

bool ApplySimTickRate(int hz) {
    if (hz != 30 && hz != 60) return false;

    AcquireSRWLockExclusive(&g_simTickLock);
    struct UnlockGuard {
        SRWLOCK* lk;
        ~UnlockGuard() { ReleaseSRWLockExclusive(lk); }
    } guard{&g_simTickLock};

    if (!g_simTickFloatPtr || !g_simTickDoublePtr) return false;
    // Fail-closed: activating 60Hz requires ALL 4 subsystem constants + netTickScale verified!
    if (hz == 60 &&
        (!g_simAnimShaderTickPtr || !g_simPathfinderTickPtr ||
         !g_simSpatialTickPtr || !g_simTimeDiffTickPtr || !g_netTickScalePtr)) {
        LogMsg("[SIMTICK] Refused Server 60Hz (incomplete subsystem pointers: anim=%d path=%d spat=%d diff=%d netScale=%d)",
               g_simAnimShaderTickPtr != nullptr, g_simPathfinderTickPtr != nullptr,
               g_simSpatialTickPtr != nullptr, g_simTimeDiffTickPtr != nullptr,
               g_netTickScalePtr != nullptr);
        return false;
    }

    const int prevHz = g_activeSimTickRate.load(std::memory_order_acquire);
    if (prevHz == hz) {
        if (hz == 60 && !g_netTickSynced60.load(std::memory_order_acquire)) {
            SyncServerNetworkTickRateForSimHz(60);
        }
        return true;
    }

    const float newF = 1.0f / static_cast<float>(hz);
    const double newD = 1.0 / static_cast<double>(hz);
    const float newNetScale = (hz == 60) ? 60.499998f : 30.499998f;

    // Transactional snapshot before mutating ANY constant
    struct FloatPatchEntry {
        float* ptr;
        float  prevVal;
        float  newVal;
        bool   written;
    };
    FloatPatchEntry fEntries[6] = {
        { g_simTickFloatPtr,      g_simTickFloatPtr      ? *g_simTickFloatPtr      : 0.0f, newF,        false },
        { g_simAnimShaderTickPtr, g_simAnimShaderTickPtr ? *g_simAnimShaderTickPtr : 0.0f, newF,        false },
        { g_simPathfinderTickPtr, g_simPathfinderTickPtr ? *g_simPathfinderTickPtr : 0.0f, newF,        false },
        { g_simSpatialTickPtr,    g_simSpatialTickPtr    ? *g_simSpatialTickPtr    : 0.0f, newF,        false },
        { g_simTimeDiffTickPtr,   g_simTimeDiffTickPtr   ? *g_simTimeDiffTickPtr   : 0.0f, newF,        false },
        { g_netTickScalePtr,      g_netTickScalePtr      ? *g_netTickScalePtr      : 0.0f, newNetScale, false },
    };
    const double savedD = *g_simTickDoublePtr;
    bool dWritten = false;

    auto rollbackAll = [&]() {
        if (dWritten) {
            PatchDoubleProtected(g_simTickDoublePtr, savedD);
        }
        for (int i = 5; i >= 0; --i) {
            if (fEntries[i].written && fEntries[i].ptr) {
                PatchFloatProtected(fEntries[i].ptr, fEntries[i].prevVal);
            }
        }
    };

    if (!PatchDoubleProtected(g_simTickDoublePtr, newD)) {
        rollbackAll();
        return false;
    }
    dWritten = true;

    int extras = 0;
    for (int i = 0; i < 6; ++i) {
        if (!fEntries[i].ptr) continue;
        if (!PatchFloatProtected(fEntries[i].ptr, fEntries[i].newVal)) {
            rollbackAll();
            LogMsg("[SIMTICK] Server %dHz patch failed at slot %d -> rolled back atomically to %dHz", hz, i, prevHz);
            return false;
        }
        fEntries[i].written = true;
        if (i >= 1 && i <= 4) ++extras;
    }
    const bool netScaleOk = fEntries[5].written;

    g_activeSimTickRate.store(hz, std::memory_order_release);
    LogMsg("[SIMTICK] Server Simulation tick rate -> %d Hz (dt=%.6fs, subsys=%d/4, netScale=%s)",
           hz, newD, extras, netScaleOk ? "OK" : "SKIP");

    SyncServerNetworkTickRateForSimHz(hz);
    return true;
}

// High-Resolution 120Hz Waitable-Timer Server Frame Pacer in 60Hz SimTick mode:
// Eliminates 60fps Sleep(1) vs 60Hz SimTick beat-frequency aliasing (0/2/0/2 bursts -> exact 1/0/1/0)!
struct ServerFrameLimiterObj {
    int64_t startQpc;
    float   targetSec;
};

struct ServerPacerTls {
    HANDLE  hTimer = nullptr;
    bool    checked = false;
    int64_t nextDeadlineQpc = 0;
    double  spinMarginUs = 850.0;

    HANDLE GetTimer() {
        if (!checked) {
            checked = true;
            if (HMODULE k32 = GetModuleHandleW(L"kernel32.dll")) {
                auto pCreateEx = reinterpret_cast<CreateWaitableTimerExW_t>(
                    GetProcAddress(k32, "CreateWaitableTimerExW"));
                if (pCreateEx) {
                    hTimer = pCreateEx(nullptr, nullptr, kCreateWaitableTimerHighRes, TIMER_ALL_ACCESS);
                }
            }
        }
        return hTimer;
    }
};

void HookServerFrameLimiterWait(void* flPtr, uint8_t bSleep) {
    if (g_activeSimTickRate.load(std::memory_order_relaxed) != 60) {
        if (g_origServerFrameLimiterWait) g_origServerFrameLimiterWait(flPtr, bSleep);
        return;
    }

    thread_local ServerPacerTls tls;
    auto* fl = static_cast<ServerFrameLimiterObj*>(flPtr);
    // 120Hz outer loop (8.333ms = exact 2x Nyquist oversampling of 60Hz SimTick)
    const int64_t targetTicks = static_cast<int64_t>(static_cast<double>(g_qpcFreq) / 120.0 + 0.5);
    int64_t nowQpc = NowQpc();
    const int64_t baseStart = (fl && fl->startQpc > 0 && fl->startQpc <= nowQpc) ? fl->startQpc : nowQpc;

    if (tls.nextDeadlineQpc == 0) {
        tls.nextDeadlineQpc = baseStart + targetTicks;
    } else {
        tls.nextDeadlineQpc += targetTicks;
    }
    if (nowQpc >= tls.nextDeadlineQpc) {
        tls.nextDeadlineQpc = nowQpc;
    } else if ((tls.nextDeadlineQpc - nowQpc) > (targetTicks + (targetTicks >> 1))) {
        tls.nextDeadlineQpc = nowQpc + targetTicks;
    }

    double remUs = static_cast<double>(tls.nextDeadlineQpc - nowQpc) * g_qpcToUs;
    if (remUs > tls.spinMarginUs + 450.0) {
        HANDLE hTimer = tls.GetTimer();
        if (hTimer) {
            while (remUs > tls.spinMarginUs + 450.0) {
                double wantSleepUs = remUs - tls.spinMarginUs;
                if (wantSleepUs < 450.0) break;
                if (wantSleepUs > 15000.0) wantSleepUs = 15000.0;
                LARGE_INTEGER due{};
                due.QuadPart = -static_cast<LONGLONG>(wantSleepUs * 10.0);
                int64_t t0 = nowQpc;
                if (SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE)) {
                    WaitForSingleObject(hTimer, 30);
                } else {
                    Sleep(1);
                }
                int64_t wakeQpc = NowQpc();
                double overshootUs = static_cast<double>(wakeQpc - t0) * g_qpcToUs - wantSleepUs;
                if (overshootUs > -50.0 && overshootUs < 4000.0) {
                    double targetMargin = std::max(450.0, overshootUs * 1.40 + 250.0);
                    tls.spinMarginUs = tls.spinMarginUs * 0.88 + targetMargin * 0.12;
                    tls.spinMarginUs = std::clamp(tls.spinMarginUs, 450.0, 1850.0);
                }
                nowQpc = wakeQpc;
                remUs = static_cast<double>(tls.nextDeadlineQpc - nowQpc) * g_qpcToUs;
            }
        } else {
            while (remUs > 2000.0) {
                Sleep(1);
                nowQpc = NowQpc();
                remUs = static_cast<double>(tls.nextDeadlineQpc - nowQpc) * g_qpcToUs;
            }
        }
    }
    while ((nowQpc = NowQpc()) < tls.nextDeadlineQpc) {
        _mm_pause();
    }
}

int Lua_HyperCore_GetSimTickRate(void* L) {
    if (!g_lua.pushnumber) return 0;
    g_lua.pushnumber(L, static_cast<double>(g_activeSimTickRate.load(std::memory_order_acquire)));
    return 1;
}

int Lua_HyperCore_SetSimTickRate(void* L) {
    if (!g_lua.gettop || !g_lua.tonumber || !g_lua.pushboolean) return 0;
    int hz = (g_lua.gettop(L) >= 1) ? static_cast<int>(g_lua.tonumber(L, 1)) : 30;
    bool ok = ApplySimTickRate(hz);
    g_lua.pushboolean(L, ok ? 1 : 0);
    return 1;
}

int Lua_HyperCore_SetRpcChannelMasks(void* L) {
    if (!g_lua.tonumber || !g_lua.pushboolean) return 0;
    const int nargs = g_lua.gettop ? g_lua.gettop(L) : 4;
    const uint32_t srv0 = static_cast<uint32_t>(static_cast<int64_t>(g_lua.tonumber(L, 1)));
    const uint32_t srv1 = static_cast<uint32_t>(static_cast<int64_t>(g_lua.tonumber(L, 2)));
    const uint32_t srv2 = static_cast<uint32_t>(static_cast<int64_t>(g_lua.tonumber(L, 3)));
    const uint32_t cli0 = static_cast<uint32_t>(static_cast<int64_t>(g_lua.tonumber(L, 4)));
    const uint32_t cli1 = (nargs >= 5)
        ? static_cast<uint32_t>(static_cast<int64_t>(g_lua.tonumber(L, 5)))
        : 0u;
    const uint64_t srvLo = static_cast<uint64_t>(srv0) | (static_cast<uint64_t>(srv1) << 32);
    const uint64_t srvHi = static_cast<uint64_t>(srv2);
    const uint64_t cliLo = static_cast<uint64_t>(cli0) | (static_cast<uint64_t>(cli1) << 32);
    g_rpcQos.SetMasks(srvLo, srvHi, cliLo, 0ULL);
    g_lua.pushboolean(L, 1);
    return 1;
}

int Lua_HyperCore_Version(void* L) {
    if (!g_lua.pushstring) return 0;
    g_lua.pushstring(L, "DST-HyperCore-Server/1.6.0");
    return 1;
}

int Lua_HyperCore_GetBlackbox(void* L) {
    if (!g_lua.pushnumber || !g_lua.pushstring) return 0;
    int limit = 20;
    if (g_lua.tonumber && (!g_lua.gettop || g_lua.gettop(L) >= 1)) {
        const int req = static_cast<int>(g_lua.tonumber(L, 1));
        if (req > 0) limit = std::clamp(req, 1, static_cast<int>(hs_blackbox::kCap));
    }
    hs_blackbox::ServerEvent evs[hs_blackbox::kCap]{};
    uint64_t total = 0;
    AcquireSRWLockShared(&g_bbLock);
    const size_t n = g_serverBlackbox.SnapshotLatest(evs, static_cast<size_t>(limit), total);
    ReleaseSRWLockShared(&g_bbLock);

    const uint32_t threshMs = g_blackboxLastThreshMs.load(std::memory_order_relaxed);
    constexpr size_t kJsonCap = 32768;
    char* jsonBuf = static_cast<char*>(std::malloc(kJsonCap));
    size_t written = 0;
    if (jsonBuf) {
        hs_blackbox::FormatServerJson(jsonBuf, kJsonCap, evs, n, total, threshMs, &written);
        g_lua.pushnumber(L, static_cast<double>(total));
        g_lua.pushnumber(L, static_cast<double>(written));
        g_lua.pushstring(L, jsonBuf);
        std::free(jsonBuf);
    } else {
        char emptyBuf[128]{};
        std::snprintf(emptyBuf, sizeof(emptyBuf),
                      "{\"role\":\"server\",\"total\":%llu,\"count\":0,\"threshold_ms\":%u,\"events\":[]}",
                      static_cast<unsigned long long>(total), static_cast<unsigned>(threshMs));
        g_lua.pushnumber(L, static_cast<double>(total));
        g_lua.pushnumber(L, 0.0);
        g_lua.pushstring(L, emptyBuf);
    }
    return 3;
}

int Lua_HyperCore_ClearBlackbox(void* L) {
    AcquireSRWLockExclusive(&g_bbLock);
    g_serverBlackbox.Clear();
    ReleaseSRWLockExclusive(&g_bbLock);
    g_blackboxTotalSpikes.store(0, std::memory_order_relaxed);
    if (g_lua.pushboolean) {
        g_lua.pushboolean(L, 1);
        return 1;
    }
    return 0;
}

int Lua_HyperCore_ExportBlackbox(void* L) {
    if (!g_lua.pushboolean || !g_lua.pushstring) return 0;
    std::wstring outWPath;
    if (!g_logPath.empty()) {
        const size_t sep = g_logPath.find_last_of(L"\\/");
        outWPath = (sep != std::wstring::npos) ? g_logPath.substr(0, sep + 1) : L"";
    } else {
        outWPath = g_binDir;
    }
    outWPath += L"dst_hypercore_blackbox.json";

    char pathUtf8[MAX_PATH * 4]{};
    const int u8Len = WideCharToMultiByte(
        CP_UTF8, 0, outWPath.c_str(), -1, pathUtf8, static_cast<int>(sizeof(pathUtf8)), nullptr, nullptr);
    if (u8Len <= 0) {
        std::snprintf(pathUtf8, sizeof(pathUtf8), "dst_hypercore_blackbox.json");
    }

    hs_blackbox::ServerEvent evs[hs_blackbox::kCap]{};
    uint64_t total = 0;
    AcquireSRWLockShared(&g_bbLock);
    const size_t n = g_serverBlackbox.SnapshotLatest(evs, hs_blackbox::kCap, total);
    ReleaseSRWLockShared(&g_bbLock);

    const uint32_t threshMs = g_blackboxLastThreshMs.load(std::memory_order_relaxed);
    constexpr size_t kJsonCap = 32768;
    char* jsonBuf = static_cast<char*>(std::malloc(kJsonCap));
    bool ok = false;
    if (jsonBuf) {
        size_t written = 0;
        const size_t jsonLen = hs_blackbox::FormatServerJson(jsonBuf, kJsonCap, evs, n, total, threshMs, &written);
        if (jsonLen > 0) {
            if (FILE* f = _wfopen(outWPath.c_str(), L"wb")) {
                ok = (std::fwrite(jsonBuf, 1, jsonLen, f) == jsonLen);
                std::fclose(f);
            }
        }
        std::free(jsonBuf);
    }
    g_lua.pushboolean(L, ok ? 1 : 0);
    g_lua.pushstring(L, pathUtf8);
    return 2;
}

DWORD WINAPI ServerPrewarmBundlesProc(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    constexpr size_t kChunkBytes = 1024u * 1024u;
    constexpr size_t kMaxPrewarmBytes = 256u * 1024u * 1024u;
    uint8_t* buf = static_cast<uint8_t*>(std::malloc(kChunkBytes));
    if (!buf) return 0;

    static const wchar_t* kRelFiles[] = {
        L"..\\data\\databundles\\scripts.zip",
        L"..\\data\\databundles\\anim_dynamic.zip",
    };
    size_t totalRead = 0;
    for (const wchar_t* rel : kRelFiles) {
        if (g_stopHttp.load(std::memory_order_relaxed) || totalRead >= kMaxPrewarmBytes) break;
        std::wstring fullPath = g_binDir + rel;
        HANDLE h = CreateFileW(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;
        while (!g_stopHttp.load(std::memory_order_relaxed) && totalRead < kMaxPrewarmBytes) {
            DWORD got = 0;
            if (!ReadFile(h, buf, static_cast<DWORD>(kChunkBytes), &got, nullptr) || got == 0) {
                break;
            }
            totalRead += got;
        }
        CloseHandle(h);
    }
    std::free(buf);
    LogMsg("[TIER] Background bundle page-cache prewarm finished (%zu MB)", totalRead / (1024u * 1024u));
    return 0;
}

uint32_t ResolveEffectiveReplicaBudget(uint32_t tierBudget) noexcept {
    const int luaOv = g_luaReplicaBudgetOverride.load(std::memory_order_acquire);
    if (luaOv >= 0) {
        return hc_replica::ClampReplicaConstructBudget(luaOv);
    }
    if (g_cfg.replica_budget_ini_explicit) {
        return hc_replica::ClampReplicaConstructBudget(g_cfg.replica_construct_budget);
    }
    return hc_replica::ClampReplicaConstructBudget(static_cast<int>(tierBudget));
}

void ApplyServerHardwareTierLive(int rawTier, bool applyOsSchedNow) {
    const auto tier = hc_server_tier::NormalizeTier(rawTier);
    const auto b    = hc_server_tier::ComputeServerTierBudget(tier, g_hostCaps);
    g_activeServerTier.store(static_cast<int>(tier), std::memory_order_release);

    if (g_cfg.enable_mimalloc) {
        mi_option_set(mi_option_purge_delay, b.miPurgeDelayMs);
        mi_option_set(mi_option_eager_commit, b.miEagerCommit ? 1 : 0);
        mi_option_set(mi_option_purge_decommits, b.miPurgeDecommits ? 1 : 0);
    }
    g_astarMgr.SetMaxWarmPool(b.astarWarmPoolCap);
    GetAsyncSave().SetLimits(b.asyncSaveMaxQueueBytes, b.asyncSaveKeepEncBytes);
    g_fsNegTtlQpc.store(static_cast<uint64_t>(g_qpcFreq) * b.fsNegTtlSec, std::memory_order_relaxed);
    g_fastDumpMaxNodes.store(b.fastDumpMaxNodes, std::memory_order_relaxed);
    g_fastDumpKeepBytes.store(b.fastDumpKeepBytes, std::memory_order_relaxed);
    g_effectiveDeflateLevel.store(b.libdeflateLevel, std::memory_order_relaxed);
    const uint32_t effRepBudget = ResolveEffectiveReplicaBudget(b.replicaConstructBudget);
    g_effectiveReplicaBudget.store(effRepBudget, std::memory_order_relaxed);
    g_gcSpareTargetMs.store(b.gcSpareTargetMs, std::memory_order_relaxed);
    g_gcCalmMaxMs.store(b.gcCalmMaxMs, std::memory_order_relaxed);
    g_gcBusyMinMs.store(b.gcBusyMinMs, std::memory_order_relaxed);
    g_gcEmergencyAddMb.store(b.gcEmergencyAddMb, std::memory_order_relaxed);
    g_gcExcessRampMb.store(b.gcExcessRampMb, std::memory_order_relaxed);
    g_simSchedDirty.store(true, std::memory_order_release);

    if (applyOsSchedNow) {
        const bool ecoOk = hc_server_tier::ApplyWin11EcoQosDisable(b.disableEcoQos);
        g_ecoQosDisabled.store(b.disableEcoQos && ecoOk, std::memory_order_relaxed);

        if (b.highPriority) {
            SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
        } else if (b.aboveNormalPriority) {
            SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
        } else {
            SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        }

        if (g_cfg.enable_timer_boost) {
            if (b.timerBoost1ms && !g_timerBoosted) {
                if (timeBeginPeriod(1) == TIMERR_NOERROR) g_timerBoosted = true;
            } else if (!b.timerBoost1ms && g_timerBoosted) {
                timeEndPeriod(1);
                g_timerBoosted = false;
            }
        }

        if (b.prewarmBundles && !g_prewarmRan.exchange(true, std::memory_order_acq_rel)) {
            if (HANDLE th = CreateThread(nullptr, 0, ServerPrewarmBundlesProc, nullptr, 0, nullptr)) {
                CloseHandle(th);
            }
        }
    }

    LogMsg("[TIER] Server Hardware Tier=%s(%d) | shard=%s cores=%u(P=%u) ram=%lluMB | miPurge=%dms eager=%d decommit=%d | deflate=L%d dump=%u/%zuMB asyncQ=%zuMB astarPool=%zu fsNeg=%llus repPace=%u | gc(emerg=%dMB busy=%.2fms calm=%.1fms ramp=%.0fMB) | prewarm=%d",
           hc_server_tier::TierName(tier), static_cast<int>(tier),
           g_shardName, g_hostCaps.logicalCores, g_hostCaps.perfCoreCount,
           static_cast<unsigned long long>(g_hostCaps.totalRamMb),
           b.miPurgeDelayMs, b.miEagerCommit ? 1 : 0, b.miPurgeDecommits ? 1 : 0,
           b.libdeflateLevel, b.fastDumpMaxNodes, b.fastDumpKeepBytes / (1024u * 1024u),
           b.asyncSaveMaxQueueBytes / (1024u * 1024u), b.astarWarmPoolCap,
           static_cast<unsigned long long>(b.fsNegTtlSec), effRepBudget,
           b.gcEmergencyAddMb, b.gcBusyMinMs, b.gcCalmMaxMs, b.gcExcessRampMb,
           b.prewarmBundles ? 1 : 0);
}

int Lua_HyperCore_SetReplicaPacing(void* L) {
    if (!g_lua.gettop || !g_lua.tonumber || !g_lua.pushboolean || !g_lua.pushnumber) return 0;
    int req = -1;
    if (g_lua.gettop(L) >= 1) {
        const int t = g_lua.type ? g_lua.type(L, 1) : HC_LUA_TNUMBER;
        if (t == HC_LUA_TSTRING && g_lua.tolstring) {
            size_t len = 0;
            const char* s = g_lua.tolstring(L, 1, &len);
            if (s && hc_server_tier::EqualsAsciiInsensitive(s, "auto")) {
                req = -1;
            } else if (s && (hc_server_tier::EqualsAsciiInsensitive(s, "off") ||
                             hc_server_tier::EqualsAsciiInsensitive(s, "false"))) {
                req = 0;
            } else if (s) {
                req = std::atoi(s);
            }
        } else {
            req = static_cast<int>(g_lua.tonumber(L, 1));
        }
    }
    if (req < 0) {
        g_luaReplicaBudgetOverride.store(-1, std::memory_order_release);
    } else {
        g_luaReplicaBudgetOverride.store(static_cast<int>(hc_replica::ClampReplicaConstructBudget(req)),
                                         std::memory_order_release);
    }
    const auto tier = hc_server_tier::NormalizeTier(g_activeServerTier.load(std::memory_order_acquire));
    const auto b    = hc_server_tier::ComputeServerTierBudget(tier, g_hostCaps);
    const uint32_t eff = ResolveEffectiveReplicaBudget(b.replicaConstructBudget);
    g_effectiveReplicaBudget.store(eff, std::memory_order_relaxed);
    LogMsg("[REPLICA] Runtime pacing budget -> %u (req=%d, iniOverride=%d, tier=%s)",
           eff, req, g_cfg.replica_budget_ini_explicit ? 1 : 0, hc_server_tier::TierName(tier));
    g_lua.pushboolean(L, 1);
    g_lua.pushnumber(L, static_cast<double>(eff));
    return 2;
}

int Lua_HyperCore_SetHardwareTier(void* L) {
    if (!g_lua.gettop || !g_lua.tonumber || !g_lua.pushboolean || !g_lua.pushnumber) return 0;
    int reqTier = 1;
    if (g_lua.gettop(L) >= 1) {
        const int t = g_lua.type ? g_lua.type(L, 1) : HC_LUA_TNUMBER;
        if (t == HC_LUA_TSTRING && g_lua.tolstring) {
            size_t len = 0;
            const char* s = g_lua.tolstring(L, 1, &len);
            reqTier = static_cast<int>(hc_server_tier::ParseTier(s));
        } else {
            reqTier = static_cast<int>(g_lua.tonumber(L, 1));
        }
    }
    const auto applied = hc_server_tier::NormalizeTier(reqTier);
    ApplyServerHardwareTierLive(static_cast<int>(applied), g_runtimeInitDone.load(std::memory_order_acquire));
    g_lua.pushboolean(L, 1);
    g_lua.pushnumber(L, static_cast<double>(applied));
    return 2;
}

int Lua_HyperCore_GetHardwareInfo(void* L) {
    if (!g_lua.ReadyForRegister()) return 0;
    const auto tier = hc_server_tier::NormalizeTier(g_activeServerTier.load(std::memory_order_acquire));
    const auto b    = hc_server_tier::ComputeServerTierBudget(tier, g_hostCaps);

    g_lua.createtable(L, 0, 24);
    auto setNum = [&](const char* k, double v) {
        g_lua.pushnumber(L, v);
        g_lua.setfield(L, -2, k);
    };
    auto setBool = [&](const char* k, bool v) {
        g_lua.pushboolean(L, v ? 1 : 0);
        g_lua.setfield(L, -2, k);
    };
    auto setStr = [&](const char* k, const char* v) {
        g_lua.pushstring(L, v ? v : "");
        g_lua.setfield(L, -2, k);
    };

    setNum("tier",              static_cast<double>(tier));
    setStr("tier_name",         hc_server_tier::TierName(tier));
    setStr("shard",             g_shardName);
    setNum("cores",             static_cast<double>(g_hostCaps.logicalCores));
    setNum("perf_cores",        static_cast<double>(g_hostCaps.perfCoreCount));
    setNum("ideal_core",        static_cast<double>(g_idealCoreApplied.load(std::memory_order_relaxed)));
    setNum("ram_mb",            static_cast<double>(g_hostCaps.totalRamMb));
    setBool("eco_qos_disabled", g_ecoQosDisabled.load(std::memory_order_relaxed));
    setNum("mi_purge_ms",       static_cast<double>(b.miPurgeDelayMs));
    setNum("deflate_level",     static_cast<double>(g_effectiveDeflateLevel.load(std::memory_order_relaxed)));
    setNum("fastdump_nodes",    static_cast<double>(g_fastDumpMaxNodes.load(std::memory_order_relaxed)));
    setNum("fastdump_keep_mb",  static_cast<double>(g_fastDumpKeepBytes.load(std::memory_order_relaxed) / (1024u * 1024u)));
    setNum("async_queue_mb",    static_cast<double>(GetAsyncSave().MaxQueueBytes() / (1024u * 1024u)));
    setNum("astar_pool",        static_cast<double>(g_astarMgr.MaxWarmPool()));
    setNum("fs_neg_ttl_s",      static_cast<double>(b.fsNegTtlSec));
    setNum("gc_emerg_mb",       static_cast<double>(g_gcEmergencyAddMb.load(std::memory_order_relaxed)));
    setNum("gc_busy_ms",        static_cast<double>(g_gcBusyMinMs.load(std::memory_order_relaxed)));
    setNum("gc_calm_ms",        static_cast<double>(g_gcCalmMaxMs.load(std::memory_order_relaxed)));
    setNum("gc_ramp_mb",        static_cast<double>(g_gcExcessRampMb.load(std::memory_order_relaxed)));
    setNum("replica_construct_budget", static_cast<double>(g_effectiveReplicaBudget.load(std::memory_order_relaxed)));
    setBool("replica_budget_explicit", g_cfg.replica_budget_ini_explicit || (g_luaReplicaBudgetOverride.load(std::memory_order_relaxed) >= 0));
    setBool("prewarm_ran",      g_prewarmRan.load(std::memory_order_relaxed));
    return 1;
}

int Lua_HyperCore_FastJsonEncode(void* L) {
    if (!g_lua.ReadyForFastJson()) {
        if (g_lua.pushnil) { g_lua.pushnil(L); return 1; }
        return 0;
    }
    const int nargs = g_lua.gettop(L);
    if (nargs < 1) {
        g_lua.pushnil(L);
        return 1;
    }
    const bool compliant = (nargs >= 2 && g_lua.type(L, 2) == HC_LUA_TBOOLEAN)
        ? (g_lua.toboolean(L, 2) != 0) : false;
    fast_json::JsonOutBuf out;
    if (fast_json::EncodeRoot(L, g_lua, 1, out, compliant) == fast_dumper::Status::Ok &&
        !out.oom && out.data) {
        g_stats.fastjson_enc_calls.fetch_add(1, std::memory_order_relaxed);
        g_lua.pushlstring(L, out.data, out.len);
        return 1;
    }
    g_stats.fastjson_fallbacks.fetch_add(1, std::memory_order_relaxed);
    g_lua.pushnil(L);
    return 1;
}

int Lua_HyperCore_FastJsonDecode(void* L) {
    if (!g_lua.ReadyForFastJson()) {
        if (g_lua.pushboolean) { g_lua.pushboolean(L, 0); return 1; }
        return 0;
    }
    const int baseTop = g_lua.gettop(L);
    if (baseTop < 1 || g_lua.type(L, 1) != HC_LUA_TSTRING) {
        g_stats.fastjson_fallbacks.fetch_add(1, std::memory_order_relaxed);
        g_lua.pushboolean(L, 0);
        return 1;
    }
    size_t slen = 0;
    const char* s = g_lua.tolstring(L, 1, &slen);
    size_t nextPos = 1;
    g_lua.pushboolean(L, 1);
    if (fast_json::DecodeRoot(L, g_lua, s, slen, &nextPos) == fast_dumper::Status::Ok) {
        g_stats.fastjson_dec_calls.fetch_add(1, std::memory_order_relaxed);
        g_lua.pushnumber(L, static_cast<double>(nextPos));
        return 3;
    }
    g_lua.settop(L, baseTop);
    g_stats.fastjson_fallbacks.fetch_add(1, std::memory_order_relaxed);
    g_lua.pushboolean(L, 0);
    return 1;
}

void RegisterHyperCoreLuaGlobals(void* L) {
    if (!L || !g_lua.pushcclosure || !g_lua.setfield) return;
    auto regFn = [&](const char* name, int (*fn)(void*)) {
        g_lua.pushcclosure(L, fn, 0);
        g_lua.setfield(L, HC_LUA_GLOBALSINDEX, name);
    };
    // Gate EVERY exported Lua function individually by its exact required C API pointers!
    const bool regFastDump = g_lua.ReadyForFastDump();
    const bool regFastJson = g_lua.ReadyForFastJson();
    const bool regStats    = g_lua.ReadyForRegister();
    const bool regVersion  = (g_lua.pushstring != nullptr);
    const bool regGetSim   = (g_lua.pushnumber != nullptr);
    const bool regSetSim   = (g_lua.gettop != nullptr && g_lua.tonumber != nullptr && g_lua.pushboolean != nullptr);
    const bool regBlackbox = (g_lua.pushnumber != nullptr && g_lua.pushstring != nullptr);

    if (regFastDump) regFn("HyperCore_FastDump",       &Lua_HyperCore_FastDump);
    if (regFastJson) {
        regFn("HyperCore_FastJsonEncode",              &Lua_HyperCore_FastJsonEncode);
        regFn("HyperCore_FastJsonDecode",              &Lua_HyperCore_FastJsonDecode);
    }
    if (regStats)    regFn("HyperCore_GetStats",       &Lua_HyperCore_GetStats);
    regFn("HyperCore_ResetStats",                      &Lua_HyperCore_ResetStats);
    if (regVersion)  regFn("HyperCore_Version",        &Lua_HyperCore_Version);
    if (regGetSim)   regFn("HyperCore_GetSimTickRate", &Lua_HyperCore_GetSimTickRate);
    if (regSetSim) {
        regFn("HyperCore_SetSimTickRate", &Lua_HyperCore_SetSimTickRate);
        regFn("HyperCore_SetRpcChannelMasks", &Lua_HyperCore_SetRpcChannelMasks);
        if (g_lua.pushnumber) {
            regFn("HyperCore_SetHardwareTier", &Lua_HyperCore_SetHardwareTier);
            regFn("HyperCore_SetReplicaPacing", &Lua_HyperCore_SetReplicaPacing);
        }
    }
    if (regStats) {
        regFn("HyperCore_GetHardwareInfo", &Lua_HyperCore_GetHardwareInfo);
    }
    if (regBlackbox) {
        regFn("HyperCore_GetBlackbox",    &Lua_HyperCore_GetBlackbox);
        regFn("HyperCore_ExportBlackbox", &Lua_HyperCore_ExportBlackbox);
    }
    regFn("HyperCore_ClearBlackbox", &Lua_HyperCore_ClearBlackbox);

    LogMsg("[LUA] Registered HyperCore APIs into Lua VM %p (fastDump=%d fastJson=%d stats=%d ver=%d getSim=%d setSim=%d sim=%dHz)",
           L, regFastDump ? 1 : 0, regFastJson ? 1 : 0, regStats ? 1 : 0, regVersion ? 1 : 0,
           regGetSim ? 1 : 0, regSetSim ? 1 : 0, g_activeSimTickRate.load(std::memory_order_relaxed));
}

void HookLuaOpenLibs(void* L) {
    // New Lua VM initializing: pause anti-STW external GC interception until Simulation ticks past warmup!
    g_simStartedForGc.store(false, std::memory_order_release);
    g_gcPastWarmup.store(false, std::memory_order_release);
    g_origLuaOpenLibs(L);
    RegisterHyperCoreLuaGlobals(L);
}

// =============================================================================
// Module 0: Profiler Hooks (Simulation::Update, FindEntities, SerializeWorldSession)
// =============================================================================
void LogTelemetrySummary(const char* tag) {
    double avg = 0, p95 = 0, p99 = 0;
    g_stats.ComputePercentiles(avg, p95, p99);
    const uint64_t ticks = g_stats.ticks_total.load();
    const uint64_t feCalls = g_stats.find_entities_calls.load();
    const uint64_t feUs = g_stats.find_entities_us_sum.load();
    const uint64_t gcUs = g_stats.gc_us_sum.load();
    LogMsg("[%s] ticks=%llu | tick_us(avg=%.0f p95=%.0f p99=%.0f max=%llu overruns=%llu) | "
           "GC(us/t=%.0f max=%llu full=%llu stwBlk=%llu kb=%d) | FindEnts(calls=%llu us/t=%.0f zc=%llu light=%llu cells=%llu) | "
           "AStar(inits=%llu steps=%llu us=%llu ok=%llu fail=%llu) | "
           "FastDump(calls=%llu ms=%.1f MB=%.2f fb=%llu) | FastJson(enc=%llu dec=%llu fb=%llu) | Ser(calls=%llu ms=%.1f) | Deflate(calls=%llu main=%llu ms=%.1f in=%.2fMB out=%.2fMB) | FastLoad(unz=%llu MB=%.1f ms=%.1f uncomp=%llu outMB=%.1f)",
           tag,
           static_cast<unsigned long long>(ticks),
           avg, p95, p99,
           static_cast<unsigned long long>(g_stats.tick_us_max.load()),
           static_cast<unsigned long long>(g_stats.tick_overrun_count.load()),
           ticks ? static_cast<double>(gcUs) / ticks : 0.0,
           static_cast<unsigned long long>(g_stats.gc_us_max.load()),
           static_cast<unsigned long long>(g_stats.gc_full_count.load()),
           static_cast<unsigned long long>(g_stats.gc_stw_blocked.load()),
           g_stats.gc_last_kb.load(),
           static_cast<unsigned long long>(feCalls),
           ticks ? static_cast<double>(feUs) / ticks : 0.0,
           static_cast<unsigned long long>(g_stats.spatial_zerocopy_hits.load()),
           static_cast<unsigned long long>(g_stats.light_spatial_queries.load()),
           static_cast<unsigned long long>(g_stats.spatial_cells_visited.load()),
           static_cast<unsigned long long>(g_stats.astar_inits.load()),
           static_cast<unsigned long long>(g_stats.astar_steps.load()),
           static_cast<unsigned long long>(g_stats.astar_us_sum.load()),
           static_cast<unsigned long long>(g_stats.astar_completed.load()),
           static_cast<unsigned long long>(g_stats.astar_failed.load()),
           static_cast<unsigned long long>(g_stats.fastdump_calls.load()),
           g_stats.fastdump_us_sum.load() / 1000.0,
           g_stats.fastdump_bytes_sum.load() / (1024.0 * 1024.0),
           static_cast<unsigned long long>(g_stats.fastdump_fallbacks.load()),
           static_cast<unsigned long long>(g_stats.fastjson_enc_calls.load()),
           static_cast<unsigned long long>(g_stats.fastjson_dec_calls.load()),
           static_cast<unsigned long long>(g_stats.fastjson_fallbacks.load()),
           static_cast<unsigned long long>(g_stats.serialize_world_calls.load()),
           g_stats.serialize_world_us_sum.load() / 1000.0,
           static_cast<unsigned long long>(g_stats.deflate_calls.load()),
           static_cast<unsigned long long>(g_stats.deflate_main_calls.load()),
           g_stats.deflate_us_sum.load() / 1000.0,
           g_stats.deflate_in_bytes.load() / (1024.0 * 1024.0),
           g_stats.deflate_out_bytes.load() / (1024.0 * 1024.0),
           static_cast<unsigned long long>(g_stats.unz_hits.load()),
           g_stats.unz_bytes.load() / (1024.0 * 1024.0),
           g_stats.unz_us_sum.load() / 1000.0,
           static_cast<unsigned long long>(g_stats.uncomp_calls.load()),
           g_stats.uncomp_out_bytes.load() / (1024.0 * 1024.0));

    const uint64_t luaC = g_stats.prof_lua_update_calls.load();
    const uint64_t netC = g_stats.prof_net_update_calls.load();
    const uint64_t repC = g_stats.prof_replica_update_calls.load();
    const uint64_t qrlC = g_replicaStats.qrl_calls.load();
    LogMsg("[%s-SUB] luaUpdate(c=%llu avg=%.0fus max=%llu) | netUpdate(c=%llu avg=%.0fus max=%llu) | "
           "repUpdate(c=%llu avg=%.0fus max=%llu) | RepFast(on=%d qrlC=%llu qrlAvg=%.0fus qrlMax=%llu hits=%llu fb=%llu con=%llu dst=%llu cmp=%llu preClr=%llu zSkip=%llu paceB=%u prox=%d def=%llu brst=%llu pPro=%llu nByp=%llu) | "
           "FsNeg(hits=%llu misses=%llu) | "
           "AsyncSave(enq=%llu ok=%llu comp=%llu bytes=%llu flush=%llu fb=%llu err=%llu enqUs=%llu compMs=%.1f ioMs=%.1f) | "
           "RpcQoS(imm=%lld offCh0=%lld tot=%lld)",
           tag,
           static_cast<unsigned long long>(luaC),
           luaC ? static_cast<double>(g_stats.prof_lua_update_us_sum.load()) / luaC : 0.0,
           static_cast<unsigned long long>(g_stats.prof_lua_update_us_max.load()),
           static_cast<unsigned long long>(netC),
           netC ? static_cast<double>(g_stats.prof_net_update_us_sum.load()) / netC : 0.0,
           static_cast<unsigned long long>(g_stats.prof_net_update_us_max.load()),
           static_cast<unsigned long long>(repC),
           repC ? static_cast<double>(g_stats.prof_replica_update_us_sum.load()) / repC : 0.0,
           static_cast<unsigned long long>(g_stats.prof_replica_update_us_max.load()),
           g_replicaFastActive.load(std::memory_order_relaxed) ? 1 : 0,
           static_cast<unsigned long long>(qrlC),
           qrlC ? static_cast<double>(g_replicaStats.qrl_us_sum.load()) / qrlC : 0.0,
           static_cast<unsigned long long>(g_replicaStats.qrl_us_max.load()),
           static_cast<unsigned long long>(g_replicaStats.fast_proxy_hits.load()),
           static_cast<unsigned long long>(g_replicaStats.slow_rel_fallbacks.load() + g_replicaStats.non_proxy_fallbacks.load()),
           static_cast<unsigned long long>(g_replicaStats.constructed_total.load()),
           static_cast<unsigned long long>(g_replicaStats.destroyed_total.load()),
           static_cast<unsigned long long>(g_replicaStats.bulk_conn_compacts.load()),
           static_cast<unsigned long long>(g_replicaStats.preser_cleared.load()),
           static_cast<unsigned long long>(g_replicaStats.preser_zero_conn_skips.load()),
           g_effectiveReplicaBudget.load(std::memory_order_relaxed),
           g_replicaCfg.proximityValid ? 1 : 0,
           static_cast<unsigned long long>(g_replicaStats.paced_deferred_total.load()),
           static_cast<unsigned long long>(g_replicaStats.paced_burst_ticks.load()),
           static_cast<unsigned long long>(g_replicaStats.paced_parent_promotes.load()),
           static_cast<unsigned long long>(g_replicaStats.paced_near_bypasses.load()),
           static_cast<unsigned long long>(g_stats.fs_neg_hits.load()),
           static_cast<unsigned long long>(g_stats.fs_neg_misses.load()),
           static_cast<unsigned long long>(GetAsyncSave().EnqueuedCount()),
           static_cast<unsigned long long>(GetAsyncSave().CommittedCount()),
           static_cast<unsigned long long>(GetAsyncSave().CompressedCount()),
           static_cast<unsigned long long>(GetAsyncSave().CommittedBytes()),
           static_cast<unsigned long long>(GetAsyncSave().FlushCount()),
           static_cast<unsigned long long>(g_stats.async_save_fallbacks.load()),
           static_cast<unsigned long long>(GetAsyncSave().ErrorCount()),
           static_cast<unsigned long long>(GetAsyncSave().EnqueueUsSum()),
           GetAsyncSave().CompressUsSum() / 1000.0,
           GetAsyncSave().IoUsSum() / 1000.0,
           static_cast<long long>(g_rpcQos.ImmCount()),
           static_cast<long long>(g_rpcQos.OffCh0Count()),
           static_cast<long long>(g_rpcQos.TotalCalls()));
}

DWORD WINAPI HttpMetricsThread(LPVOID);

// Runs ONCE on first Simulation::Update tick — completely OUTSIDE DllMain / Loader Lock!
void InitRuntimeServicesOnce() {
    if (g_runtimeInitDone.exchange(true, std::memory_order_acq_rel)) return;
    ApplyServerHardwareTierLive(g_activeServerTier.load(std::memory_order_acquire), true);
    if (g_cfg.enable_http_metrics) {
        g_httpThread = CreateThread(nullptr, 0, HttpMetricsThread, nullptr, 0, nullptr);
    }
}

uint8_t HookSimUpdate(void* sim, float dt, uint8_t flag) {
    if (!g_runtimeInitDone.load(std::memory_order_relaxed)) {
        InitRuntimeServicesOnce();
    }
    if (g_simSchedDirty.exchange(false, std::memory_order_acq_rel)) {
        const auto tier = hc_server_tier::NormalizeTier(g_activeServerTier.load(std::memory_order_acquire));
        const auto b    = hc_server_tier::ComputeServerTierBudget(tier, g_hostCaps);
        SetThreadPriority(GetCurrentThread(), b.highPriority ? THREAD_PRIORITY_ABOVE_NORMAL : THREAD_PRIORITY_NORMAL);
        if (b.pinShardIdealCore) {
            const int want = hc_server_tier::SelectIdealShardCore(g_shardName, g_hostCaps);
            const int applied = (want >= 0) ? hc_server_tier::ApplyShardIdealProcessor(want) : -1;
            g_idealCoreApplied.store(applied, std::memory_order_relaxed);
            if (applied >= 0) {
                LogMsg("[TIER] Pinned shard=%s main thread ideal P-core -> CPU%d", g_shardName, applied);
            }
        } else {
            g_idealCoreApplied.store(-1, std::memory_order_relaxed);
        }
    }
    if (g_mainSimThreadId.load(std::memory_order_relaxed) == 0) {
        g_mainSimThreadId.store(GetCurrentThreadId(), std::memory_order_release);
    }
    if (!g_simStartedForGc.load(std::memory_order_relaxed)) {
        g_simStartedForGc.store(true, std::memory_order_release);
    }
    if (g_activeSimTickRate.load(std::memory_order_relaxed) == 60 &&
        !g_netTickSynced60.load(std::memory_order_relaxed)) {
        SyncServerNetworkTickRateForSimHz(60);
    }
    g_stats.cur_tick_gc_us = 0;
    tl_tickProf = {};
    const int64_t t0 = NowQpc();
    const uint8_t ret = g_origSimUpdate(sim, dt, flag);
    const uint64_t totalUs = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));

    const uint64_t gcUs = g_stats.cur_tick_gc_us;
    const uint64_t nonGcUs = (totalUs > gcUs) ? (totalUs - gcUs) : 0;
    // EMA smoothing (0.65 current + 0.35 prev) for adaptive GC stability
    g_stats.last_tick_non_gc_ms = g_stats.last_tick_non_gc_ms * 0.35 + (static_cast<double>(nonGcUs) * 0.001) * 0.65;

    const uint64_t ticks = g_stats.ticks_total.fetch_add(1, std::memory_order_relaxed) + 1;
    g_stats.tick_us_sum.fetch_add(totalUs, std::memory_order_relaxed);
    const int simHz = g_activeSimTickRate.load(std::memory_order_relaxed);
    const uint64_t budgetLimitUs = (simHz == 60) ? 16667ULL : 33333ULL;
    if (totalUs > budgetLimitUs) {
        g_stats.tick_overrun_count.fetch_add(1, std::memory_order_relaxed);
    }
    uint64_t prevMax = g_stats.tick_us_max.load(std::memory_order_relaxed);
    while (totalUs > prevMax && !g_stats.tick_us_max.compare_exchange_weak(prevMax, totalUs)) {}

    g_stats.PushTickSample(static_cast<uint32_t>(std::min<uint64_t>(totalUs, UINT32_MAX)));

    const uint64_t bbThreshUs = (g_cfg.blackbox_threshold_ms > 0)
        ? (static_cast<uint64_t>(g_cfg.blackbox_threshold_ms) * 1000ULL)
        : budgetLimitUs;
    g_blackboxLastThreshMs.store(static_cast<uint32_t>((bbThreshUs + 500ULL) / 1000ULL), std::memory_order_relaxed);
    if (g_cfg.enable_blackbox && totalUs > bbThreshUs) {
        hs_blackbox::ServerEvent ev{};
        ev.epochMs   = NowEpochMs();
        ev.tickSeq   = ticks;
        ev.totalUs   = ClampUsU32(totalUs);
        ev.budgetUs  = static_cast<uint32_t>(budgetLimitUs);
        ev.luaUs     = tl_tickProf.luaUs;
        ev.gcUs      = (tl_tickProf.gcUs > 0) ? tl_tickProf.gcUs : ClampUsU32(gcUs);
        ev.netUs     = tl_tickProf.netUs;
        ev.replicaUs = tl_tickProf.replicaUs;
        ev.findEntUs = tl_tickProf.findEntUs;
        ev.astarUs   = tl_tickProf.astarUs;
        ev.saveUs    = tl_tickProf.saveUs;
        ev.gcKb      = g_stats.gc_last_kb.load(std::memory_order_relaxed);
        ev.gcFull    = tl_tickProf.gcFull;
        ev.simHz     = static_cast<uint8_t>((simHz == 60) ? 60 : 30);
        hs_blackbox::ComputeServerDerived(ev);
        AcquireSRWLockExclusive(&g_bbLock);
        g_serverBlackbox.Push(ev);
        ReleaseSRWLockExclusive(&g_bbLock);
        g_blackboxTotalSpikes.fetch_add(1, std::memory_order_relaxed);
    }

    if (g_cfg.enable_profiler && g_cfg.log_interval_ticks > 0 &&
        (ticks % static_cast<uint64_t>(g_cfg.log_interval_ticks) == 0)) {
        LogTelemetrySummary("PROF");
    }
    return ret;
}

int HookSimFindEntities(void* selfObj, void* L) {
    const int64_t t0 = NowQpc();
    int rc = g_origSimFindEnts(selfObj, L);
    const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
    g_stats.find_entities_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.find_entities_us_sum.fetch_add(us, std::memory_order_relaxed);
    tl_tickProf.findEntUs += ClampUsU32(us);
    return rc;
}

int HookSimFindEntitiesReg(void* selfObj, void* L) {
    const int64_t t0 = NowQpc();
    int rc = g_origSimFindEntsReg(selfObj, L);
    const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
    g_stats.find_entities_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.find_entities_us_sum.fetch_add(us, std::memory_order_relaxed);
    tl_tickProf.findEntUs += ClampUsU32(us);
    return rc;
}

int HookSerializeWorldSession(void* selfObj, void* L) {
    SaveSubphaseGuard saveGuard;
    const int64_t t0 = NowQpc();
    int rc = g_origSerializeWorld(selfObj, L);
    const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
    g_stats.serialize_world_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.serialize_world_us_sum.fetch_add(us, std::memory_order_relaxed);
    saveGuard.Add(us);
    return rc;
}

uint8_t HookSimLuaUpdate(void* sim, float dt, uint8_t paused) {
    const int64_t t0 = NowQpc();
    const uint8_t ret = g_origSimLuaUpdate(sim, dt, paused);
    const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
    g_stats.prof_lua_update_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.prof_lua_update_us_sum.fetch_add(us, std::memory_order_relaxed);
    UpdateAtomicMax(g_stats.prof_lua_update_us_max, us);
    tl_tickProf.luaUs += ClampUsU32(us);
    return ret;
}

void HookNetMgrUpdate(void* netMgr, float dt) {
    const int64_t t0 = NowQpc();
    g_origNetMgrUpdate(netMgr, dt);
    const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
    g_stats.prof_net_update_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.prof_net_update_us_sum.fetch_add(us, std::memory_order_relaxed);
    UpdateAtomicMax(g_stats.prof_net_update_us_max, us);
    tl_tickProf.netUs += ClampUsU32(us);
}

void HookReplicaMgr3Update(void* rm3) {
    const int64_t t0 = NowQpc();
    const bool preSerPatched = g_replicaPreSerPatched.load(std::memory_order_acquire) && (rm3 != nullptr);
    const uint32_t snapBefore = preSerPatched
        ? *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(rm3) + 0x8a0)
        : 0u;
    g_origReplicaMgr3Update(rm3);
    if (preSerPatched) {
        const uint32_t snapAfter = *reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(rm3) + 0x8a0);
        if (snapAfter != snapBefore) {
            hc_replica::ClearSerializedPreFlags(rm3, g_replicaCfg, &g_replicaStats);
        }
    }
    const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
    g_stats.prof_replica_update_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.prof_replica_update_us_sum.fetch_add(us, std::memory_order_relaxed);
    UpdateAtomicMax(g_stats.prof_replica_update_us_max, us);
    tl_tickProf.replicaUs += ClampUsU32(us);
}

void HookQueryReplicaList(void* conn, void* outConstruct, void* outDestroy) {
    const int64_t t0 = NowQpc();
    if (g_replicaFastActive.load(std::memory_order_acquire) && g_replicaCfg.IsValid()) {
        hc_replica::ReplicaAbiConfig runCfg = g_replicaCfg;
        runCfg.maxConstructPerTick = g_effectiveReplicaBudget.load(std::memory_order_relaxed);
        hc_replica::FastQueryReplicaList(
            conn,
            static_cast<hc_replica::DsListPtr*>(outConstruct),
            static_cast<hc_replica::DsListPtr*>(outDestroy),
            runCfg,
            &g_replicaStats);
    } else if (g_origQueryReplicaList) {
        g_origQueryReplicaList(conn, outConstruct, outDestroy);
    }
    const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
    g_replicaStats.qrl_us_sum.fetch_add(us, std::memory_order_relaxed);
    UpdateAtomicMax(g_replicaStats.qrl_us_max, us);
}

// =============================================================================
// Unified Server EXE IAT Hook Layer (Modules 7 & 8 + Safe Exit Flush)
// =============================================================================
void** FindExeIatSlot(const char* dllName, uint16_t ordinal, const char* funcName) {
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) return nullptr;
    auto* base = reinterpret_cast<uint8_t*>(exe);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress || !dir.Size) return nullptr;

    auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
    for (; desc->Name != 0; ++desc) {
        const char* curDll = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(curDll, dllName) != 0) continue;
        const DWORD origRva = desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk;
        if (!origRva || !desc->FirstThunk) continue;
        auto* origThunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + origRva);
        auto* iatThunk  = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
        for (; origThunk->u1.AddressOfData != 0; ++origThunk, ++iatThunk) {
            if (IMAGE_SNAP_BY_ORDINAL64(origThunk->u1.Ordinal)) {
                if (ordinal != 0 && IMAGE_ORDINAL64(origThunk->u1.Ordinal) == ordinal) {
                    return reinterpret_cast<void**>(&iatThunk->u1.Function);
                }
            } else if (funcName) {
                auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + origThunk->u1.AddressOfData);
                if (std::strcmp(reinterpret_cast<const char*>(ibn->Name), funcName) == 0) {
                    return reinterpret_cast<void**>(&iatThunk->u1.Function);
                }
            }
        }
    }
    return nullptr;
}

template <typename FnPtr>
bool PatchExeIatEntry(const char* dllName, const char* funcName, void* detour, FnPtr& outOrig) {
    void** slot = FindExeIatSlot(dllName, 0, funcName);
    if (!slot) return false;
    void* orig = *slot;
    DWORD oldProt = 0, dummy = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt)) {
        return false;
    }
    outOrig = reinterpret_cast<FnPtr>(orig);
    std::atomic_thread_fence(std::memory_order_release);
    *slot = detour;
    VirtualProtect(slot, sizeof(void*), oldProt, &dummy);
    return true;
}

static bool ModeHasWriteIntentW(const wchar_t* mode) noexcept {
    if (!mode) return false;
    return std::wcschr(mode, L'w') || std::wcschr(mode, L'a') || std::wcschr(mode, L'+');
}

static bool ModeHasWriteIntentA(const char* mode) noexcept {
    if (!mode) return false;
    return std::strchr(mode, 'w') || std::strchr(mode, 'a') || std::strchr(mode, '+');
}

static void SetMsvcrErrno(int e) noexcept {
    if (g_msvcr90Errno) {
        if (int* ep = g_msvcr90Errno()) *ep = e;
    } else {
        errno = e;
    }
}

static bool IsMsvcrEnoent(DWORD lastErr) noexcept {
    if (g_msvcr90Errno) {
        int* ep = g_msvcr90Errno();
        if (ep) return *ep == ENOENT;
    }
    return lastErr == ERROR_FILE_NOT_FOUND || lastErr == ERROR_PATH_NOT_FOUND;
}

static bool CanCaptureAsyncWfopen(const wchar_t* path) noexcept {
    if (!path || path[0] == L'\0' || !g_origGetFileAttributesW) return false;
    const size_t plen = std::wcslen(path);
    if (plen >= 4 && _wcsicmp(path + plen - 4, L".zip") == 0) {
        return false;
    }
    const wchar_t* lastFwd = std::wcsrchr(path, L'/');
    const wchar_t* lastBck = std::wcsrchr(path, L'\\');
    const wchar_t* lastSep = (lastFwd && (!lastBck || lastFwd > lastBck)) ? lastFwd : lastBck;
    if (lastSep && lastSep > path) {
        std::wstring parentDir(path, static_cast<size_t>(lastSep - path));
        if (parentDir.size() == 2 && parentDir[1] == L':') {
            parentDir.push_back(L'\\');
        }
        const DWORD pattrs = g_origGetFileAttributesW(parentDir.c_str());
        if (pattrs == INVALID_FILE_ATTRIBUTES || (pattrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            return false;
        }
    }
    const DWORD tattrs = g_origGetFileAttributesW(path);
    if (tattrs != INVALID_FILE_ATTRIBUTES &&
        (tattrs & (FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_DIRECTORY)) != 0) {
        return false;
    }
    return true;
}

static void HookRpc4Call(void* thisPtr, const char* uniqueID, void* bitStream,
                         int priority, int reliability, uint8_t orderingChannel,
                         const void* systemIdentifier, uint8_t broadcast, uint8_t callLocally) {
    if (!g_origRpc4Call) return;
    if (g_cfg.enable_rpc_qos &&
        g_rpcQosActive.load(std::memory_order_acquire) &&
        bitStream != nullptr) {
        const uint8_t* bs = static_cast<const uint8_t*>(bitStream);
        uint32_t bsBits = 0;
        const uint8_t* bsData = nullptr;
        std::memcpy(&bsBits, bs + 0x00, sizeof(bsBits));
        std::memcpy(&bsData, bs + 0x10, sizeof(bsData));
        const auto cls = g_rpcQos.Classify(uniqueID, bsData, bsBits, priority, reliability, orderingChannel);
        priority = cls.priority;
        orderingChannel = cls.orderingChannel;
    }
    g_origRpc4Call(thisPtr, uniqueID, bitStream, priority, reliability, orderingChannel,
                   systemIdentifier, broadcast, callLocally);
}

uint8_t HookWritePersistentFile(const char* utf8Path, const void* data, uint32_t size,
                                void* delegate, uint32_t arg5, void* arg6) {
    struct CaptureGuard {
        AsyncCaptureState prev;
        CaptureGuard(bool enable, const void* d, uint32_t sz) noexcept
            : prev(tl_asyncCapture) {
            if (enable) {
                tl_asyncCapture.active = true;
                tl_asyncCapture.data   = d;
                tl_asyncCapture.size   = sz;
            }
        }
        ~CaptureGuard() noexcept { tl_asyncCapture = prev; }
    };

    if (!g_origWritePersistentFile) return 0;

    // EncodePersistentString is never substituted with placeholder tokens: data is ALWAYS complete,
    // independently decodable save bytes (and already SIMD-accelerated via libdeflate compress2 hook).
    // HookExeWfopen commits it atomically with per-path serialization, unique temp file, and verified
    // FlushFileBuffers + MoveFileExW BEFORE reporting success to DST!
    const bool enablePlain = g_cfg.enable_async_save &&
                             g_asyncSaveActive.load(std::memory_order_acquire) &&
                             data != nullptr && size > 0;
    CaptureGuard guard(enablePlain, data, size);
    return g_origWritePersistentFile(utf8Path, data, size, delegate, arg5, arg6);
}

static DWORD WINAPI HookExeGetFileAttributesW(LPCWSTR lpFileName) {
    if (!g_origGetFileAttributesW) return INVALID_FILE_ATTRIBUTES;
    GetAsyncSave().FlushPendingForPath(lpFileName);

    if (g_fsNegCacheActive.load(std::memory_order_acquire) && g_cfg.enable_fs_neg_cache) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPath(lpFileName, &h1, &h2)) {
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (GetFsNegCache().Lookup(h1, h2, now)) {
                g_stats.fs_neg_hits.fetch_add(1, std::memory_order_relaxed);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return INVALID_FILE_ATTRIBUTES;
            }
            const DWORD res = g_origGetFileAttributesW(lpFileName);
            const DWORD err = GetLastError();
            if (res == INVALID_FILE_ATTRIBUTES &&
                (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)) {
                g_stats.fs_neg_misses.fetch_add(1, std::memory_order_relaxed);
                GetFsNegCache().Insert(h1, h2, now, g_fsNegTtlQpc.load(std::memory_order_relaxed));
            }
            SetLastError(err);
            return res;
        }
    }
    return g_origGetFileAttributesW(lpFileName);
}

static HANDLE WINAPI HookExeCreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                        LPSECURITY_ATTRIBUTES lpSec, DWORD dwCreationDisposition,
                                        DWORD dwFlagsAndAttrs, HANDLE hTemplate) {
    GetAsyncSave().FlushPendingForPath(lpFileName);
    if ((dwDesiredAccess & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE)) != 0 ||
        dwCreationDisposition != OPEN_EXISTING) {
        GetFsNegCache().InvalidateAll();
    }
    return g_origCreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSec,
                             dwCreationDisposition, dwFlagsAndAttrs, hTemplate);
}

static BOOL WINAPI HookExeDeleteFileW(LPCWSTR lpFileName) {
    GetAsyncSave().FlushPendingForPath(lpFileName);
    GetFsNegCache().InvalidateAll();
    return g_origDeleteFileW(lpFileName);
}

static BOOL WINAPI HookExeMoveFileExW(LPCWSTR lpExisting, LPCWSTR lpNew, DWORD dwFlags) {
    GetAsyncSave().FlushPendingForPath(lpExisting);
    GetAsyncSave().FlushPendingForPath(lpNew);
    GetFsNegCache().InvalidateAll();
    return g_origMoveFileExW(lpExisting, lpNew, dwFlags);
}

static BOOL WINAPI HookExeCopyFileW(LPCWSTR lpExisting, LPCWSTR lpNew, BOOL bFailIfExists) {
    GetAsyncSave().FlushPendingForPath(lpExisting);
    GetAsyncSave().FlushPendingForPath(lpNew);
    GetFsNegCache().InvalidateAll();
    return g_origCopyFileW(lpExisting, lpNew, bFailIfExists);
}

static BOOL WINAPI HookExeCreateDirectoryW(LPCWSTR lpPath, LPSECURITY_ATTRIBUTES lpSec) {
    GetAsyncSave().FlushPendingForPath(lpPath);
    GetFsNegCache().InvalidateAll();
    return g_origCreateDirectoryW(lpPath, lpSec);
}

static BOOL WINAPI HookExeRemoveDirectoryW(LPCWSTR lpPath) {
    GetAsyncSave().FlushPendingForDirPattern(lpPath);
    GetFsNegCache().InvalidateAll();
    return g_origRemoveDirectoryW(lpPath);
}

static HANDLE WINAPI HookExeFindFirstFileW(LPCWSTR lpPattern, LPWIN32_FIND_DATAW lpData) {
    GetAsyncSave().FlushPendingForDirPattern(lpPattern);
    return g_origFindFirstFileW(lpPattern, lpData);
}

static BOOL WINAPI HookExeSetCurrentDirectoryW(LPCWSTR lpPath) {
    GetAsyncSave().FlushAll();
    GetFsNegCache().InvalidateAll();
    return g_origSetCurrentDirW(lpPath);
}

static FILE* __cdecl HookExeWfopen(const wchar_t* path, const wchar_t* mode) {
    if (tl_asyncCapture.active && path && mode && std::wcscmp(mode, L"wb") == 0) {
        const void* capData = tl_asyncCapture.data;
        const uint32_t capSize = tl_asyncCapture.size;
        tl_asyncCapture.active = false;
        if (CanCaptureAsyncWfopen(path) && capData && capSize > 0) {
            DWORD winErr = 0;
            const int64_t tCommit0 = NowQpc();
            // Synchronous verified atomic commit (per-path lock + unique temp file +
            // checked FlushFileBuffers + MoveFileExW): NEVER fabricate save success before disk durability!
            if (GetAsyncSave().CommitSyncAtomic(path, capData, capSize, &winErr)) {
                const uint64_t commitUs = static_cast<uint64_t>(QpcDeltaUs(tCommit0, NowQpc()));
                const DWORD mainTid = g_mainSimThreadId.load(std::memory_order_relaxed);
                if (mainTid != 0 && GetCurrentThreadId() == mainTid && tl_tickProf.saveDepth == 0) {
                    tl_tickProf.saveUs += ClampUsU32(commitUs);
                }
                GetFsNegCache().InvalidateAll();
                return kAsyncSentinelFile;
            }
            g_stats.async_save_fallbacks.fetch_add(1, std::memory_order_relaxed);
            const bool isLockErr = (winErr == ERROR_SHARING_VIOLATION ||
                                    winErr == ERROR_ACCESS_DENIED ||
                                    winErr == ERROR_LOCK_VIOLATION);
            SetMsvcrErrno(isLockErr ? EACCES : EIO);
            SetLastError(winErr ? winErr : ERROR_WRITE_FAULT);
            return nullptr;
        }
        g_stats.async_save_fallbacks.fetch_add(1, std::memory_order_relaxed);
    }
    GetAsyncSave().FlushPendingForPath(path);
    if (ModeHasWriteIntentW(mode)) {
        GetFsNegCache().InvalidateAll();
        return g_origWfopen(path, mode);
    }
    if (g_fsNegCacheActive.load(std::memory_order_acquire) && g_cfg.enable_fs_neg_cache &&
        mode && (std::wcscmp(mode, L"rb") == 0 || std::wcscmp(mode, L"r") == 0)) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPath(path, &h1, &h2)) {
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (GetFsNegCache().Lookup(h1, h2, now)) {
                g_stats.fs_neg_hits.fetch_add(1, std::memory_order_relaxed);
                SetMsvcrErrno(ENOENT);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return nullptr;
            }
            FILE* f = g_origWfopen(path, mode);
            if (!f && IsMsvcrEnoent(GetLastError())) {
                g_stats.fs_neg_misses.fetch_add(1, std::memory_order_relaxed);
                GetFsNegCache().Insert(h1, h2, now, g_fsNegTtlQpc.load(std::memory_order_relaxed));
            }
            return f;
        }
    }
    return g_origWfopen(path, mode);
}

static int __cdecl HookExeWfopenS(FILE** pFile, const wchar_t* path, const wchar_t* mode) {
    GetAsyncSave().FlushPendingForPath(path);
    if (ModeHasWriteIntentW(mode)) {
        GetFsNegCache().InvalidateAll();
        return g_origWfopen_s(pFile, path, mode);
    }
    if (g_fsNegCacheActive.load(std::memory_order_acquire) && g_cfg.enable_fs_neg_cache &&
        pFile && mode && (std::wcscmp(mode, L"rb") == 0 || std::wcscmp(mode, L"r") == 0)) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPath(path, &h1, &h2)) {
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (GetFsNegCache().Lookup(h1, h2, now)) {
                *pFile = nullptr;
                g_stats.fs_neg_hits.fetch_add(1, std::memory_order_relaxed);
                SetMsvcrErrno(ENOENT);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return ENOENT;
            }
            const int rc = g_origWfopen_s(pFile, path, mode);
            if (rc == ENOENT) {
                g_stats.fs_neg_misses.fetch_add(1, std::memory_order_relaxed);
                GetFsNegCache().Insert(h1, h2, now, g_fsNegTtlQpc.load(std::memory_order_relaxed));
            }
            return rc;
        }
    }
    return g_origWfopen_s(pFile, path, mode);
}

static FILE* __cdecl HookExeFopen(const char* path, const char* mode) {
    GetAsyncSave().FlushPendingForAnsiPath(path);
    if (ModeHasWriteIntentA(mode)) {
        GetFsNegCache().InvalidateAll();
        return g_origFopen(path, mode);
    }
    if (g_fsNegCacheActive.load(std::memory_order_acquire) && g_cfg.enable_fs_neg_cache &&
        mode && (std::strcmp(mode, "rb") == 0 || std::strcmp(mode, "r") == 0)) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPathA(path, &h1, &h2)) {
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (GetFsNegCache().Lookup(h1, h2, now)) {
                g_stats.fs_neg_hits.fetch_add(1, std::memory_order_relaxed);
                SetMsvcrErrno(ENOENT);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return nullptr;
            }
            FILE* f = g_origFopen(path, mode);
            if (!f && IsMsvcrEnoent(GetLastError())) {
                g_stats.fs_neg_misses.fetch_add(1, std::memory_order_relaxed);
                GetFsNegCache().Insert(h1, h2, now, g_fsNegTtlQpc.load(std::memory_order_relaxed));
            }
            return f;
        }
    }
    return g_origFopen(path, mode);
}

static size_t __cdecl HookExeFwrite(const void* buf, size_t size, size_t count, FILE* stream) {
    if (stream == kAsyncSentinelFile) {
        return count;
    }
    return g_origFwrite(buf, size, count, stream);
}

static int __cdecl HookExeFclose(FILE* stream) {
    if (stream == kAsyncSentinelFile) {
        return 0;
    }
    return g_origFclose(stream);
}

static int __cdecl HookExeWstat64i32(const wchar_t* path, void* buf) {
    GetAsyncSave().FlushPendingForPath(path);
    if (g_fsNegCacheActive.load(std::memory_order_acquire) && g_cfg.enable_fs_neg_cache) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPath(path, &h1, &h2)) {
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (GetFsNegCache().Lookup(h1, h2, now)) {
                g_stats.fs_neg_hits.fetch_add(1, std::memory_order_relaxed);
                SetMsvcrErrno(ENOENT);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return -1;
            }
            const int rc = g_origWstat64i32(path, buf);
            if (rc == -1 && IsMsvcrEnoent(GetLastError())) {
                g_stats.fs_neg_misses.fetch_add(1, std::memory_order_relaxed);
                GetFsNegCache().Insert(h1, h2, now, g_fsNegTtlQpc.load(std::memory_order_relaxed));
            }
            return rc;
        }
    }
    return g_origWstat64i32(path, buf);
}

static int __cdecl HookExeStat64(const char* path, void* buf) {
    GetAsyncSave().FlushPendingForAnsiPath(path);
    if (g_fsNegCacheActive.load(std::memory_order_acquire) && g_cfg.enable_fs_neg_cache) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPathA(path, &h1, &h2)) {
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (GetFsNegCache().Lookup(h1, h2, now)) {
                g_stats.fs_neg_hits.fetch_add(1, std::memory_order_relaxed);
                SetMsvcrErrno(ENOENT);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return -1;
            }
            const int rc = g_origStat64(path, buf);
            if (rc == -1 && IsMsvcrEnoent(GetLastError())) {
                g_stats.fs_neg_misses.fetch_add(1, std::memory_order_relaxed);
                GetFsNegCache().Insert(h1, h2, now, g_fsNegTtlQpc.load(std::memory_order_relaxed));
            }
            return rc;
        }
    }
    return g_origStat64(path, buf);
}

static int __cdecl HookExeStat64i32(const char* path, void* buf) {
    GetAsyncSave().FlushPendingForAnsiPath(path);
    if (g_fsNegCacheActive.load(std::memory_order_acquire) && g_cfg.enable_fs_neg_cache) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPathA(path, &h1, &h2)) {
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (GetFsNegCache().Lookup(h1, h2, now)) {
                g_stats.fs_neg_hits.fetch_add(1, std::memory_order_relaxed);
                SetMsvcrErrno(ENOENT);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return -1;
            }
            const int rc = g_origStat64i32(path, buf);
            if (rc == -1 && IsMsvcrEnoent(GetLastError())) {
                g_stats.fs_neg_misses.fetch_add(1, std::memory_order_relaxed);
                GetFsNegCache().Insert(h1, h2, now, g_fsNegTtlQpc.load(std::memory_order_relaxed));
            }
            return rc;
        }
    }
    return g_origStat64i32(path, buf);
}

static int __cdecl HookExeOpenA(const char* path, int oflag, int pmode) {
    GetAsyncSave().FlushPendingForAnsiPath(path);
    if ((oflag & 0x0303) != 0) {
        GetFsNegCache().InvalidateAll();
    }
    return g_origOpenA(path, oflag, pmode);
}

static void FlushSavesAndLogOnShutdown() {
    if (auto* p = g_asyncSavePtr.load(std::memory_order_acquire)) {
        p->ShutdownAndFlush();
    }
    g_stopHttp.store(true, std::memory_order_release);
}

static void __cdecl HookExeExit(int code) {
    FlushSavesAndLogOnShutdown();
    LogTelemetrySummary("EXIT");
    g_origExeExit(code);
}

static void __cdecl HookExe_exit(int code) {
    FlushSavesAndLogOnShutdown();
    g_origExe_exit(code);
}

static void __cdecl HookExeCexit() {
    FlushSavesAndLogOnShutdown();
    g_origExeCexit();
}

static BOOL WINAPI HyperCoreConsoleCtrlHandler(DWORD) {
    FlushSavesAndLogOnShutdown();
    return FALSE; // chain to CRT / OS default handler cleanly
}

static bool InstallUnifiedFsAndAsyncSaveIatHooks() {
    // Materialize immortal singletons on the single startup main thread before hooks go live
    (void)GetFsNegCache();
    (void)GetAsyncSave();

    // Kernel32 path/dir IAT hooks (shared cleanly between Module 7 & Module 8 without double patching!)
    bool okGa = PatchExeIatEntry("KERNEL32.dll", "GetFileAttributesW",  reinterpret_cast<void*>(&HookExeGetFileAttributesW), g_origGetFileAttributesW);
    bool okMv = PatchExeIatEntry("KERNEL32.dll", "MoveFileExW",         reinterpret_cast<void*>(&HookExeMoveFileExW),        g_origMoveFileExW);
    bool okCp = PatchExeIatEntry("KERNEL32.dll", "CopyFileW",           reinterpret_cast<void*>(&HookExeCopyFileW),          g_origCopyFileW);
    bool okDl = PatchExeIatEntry("KERNEL32.dll", "DeleteFileW",         reinterpret_cast<void*>(&HookExeDeleteFileW),        g_origDeleteFileW);
    bool okMd = PatchExeIatEntry("KERNEL32.dll", "CreateDirectoryW",    reinterpret_cast<void*>(&HookExeCreateDirectoryW),   g_origCreateDirectoryW);
    bool okRd = PatchExeIatEntry("KERNEL32.dll", "RemoveDirectoryW",    reinterpret_cast<void*>(&HookExeRemoveDirectoryW),   g_origRemoveDirectoryW);
    bool okCf = PatchExeIatEntry("KERNEL32.dll", "CreateFileW",         reinterpret_cast<void*>(&HookExeCreateFileW),        g_origCreateFileW);
    bool okFf = PatchExeIatEntry("KERNEL32.dll", "FindFirstFileW",      reinterpret_cast<void*>(&HookExeFindFirstFileW),     g_origFindFirstFileW);
    bool okScd = PatchExeIatEntry("KERNEL32.dll", "SetCurrentDirectoryW", reinterpret_cast<void*>(&HookExeSetCurrentDirectoryW), g_origSetCurrentDirW);

    if (HMODULE hCrt = GetModuleHandleA("MSVCR90.dll")) {
        g_msvcr90Errno = reinterpret_cast<MsvcrErrnoFn_t>(GetProcAddress(hCrt, "_errno"));
    }

    // MSVCR90 CRT stream + stat + clean-exit hooks (required fail-closed for AsyncSave!)
    bool okWf  = PatchExeIatEntry("MSVCR90.dll", "_wfopen",    reinterpret_cast<void*>(&HookExeWfopen),     g_origWfopen);
    bool okFw  = PatchExeIatEntry("MSVCR90.dll", "fwrite",     reinterpret_cast<void*>(&HookExeFwrite),     g_origFwrite);
    bool okFc  = PatchExeIatEntry("MSVCR90.dll", "fclose",     reinterpret_cast<void*>(&HookExeFclose),     g_origFclose);
    bool okWfs = PatchExeIatEntry("MSVCR90.dll", "_wfopen_s",  reinterpret_cast<void*>(&HookExeWfopenS),    g_origWfopen_s);
    bool okFo  = PatchExeIatEntry("MSVCR90.dll", "fopen",      reinterpret_cast<void*>(&HookExeFopen),      g_origFopen);
    bool okWs  = PatchExeIatEntry("MSVCR90.dll", "_wstat64i32",reinterpret_cast<void*>(&HookExeWstat64i32),g_origWstat64i32);
    bool okS64 = PatchExeIatEntry("MSVCR90.dll", "_stat64",    reinterpret_cast<void*>(&HookExeStat64),     g_origStat64);
    bool okS32 = PatchExeIatEntry("MSVCR90.dll", "_stat64i32", reinterpret_cast<void*>(&HookExeStat64i32),  g_origStat64i32);
    bool okOpen = PatchExeIatEntry("MSVCR90.dll", "_open",     reinterpret_cast<void*>(&HookExeOpenA),      g_origOpenA);
    bool okEx  = PatchExeIatEntry("MSVCR90.dll", "exit",       reinterpret_cast<void*>(&HookExeExit),       g_origExeExit);
    bool okEx2 = PatchExeIatEntry("MSVCR90.dll", "_exit",      reinterpret_cast<void*>(&HookExe_exit),      g_origExe_exit);
    bool okCex = PatchExeIatEntry("MSVCR90.dll", "_cexit",     reinterpret_cast<void*>(&HookExeCexit),      g_origExeCexit);

    SetConsoleCtrlHandler(&HyperCoreConsoleCtrlHandler, TRUE);

    const bool fsNegReady = g_cfg.enable_fs_neg_cache && okGa && okMv && okCp && okDl && okMd &&
                            okRd && okScd && okCf && okOpen && okWs && okWf && okWfs && okFo && okS64 && okS32;
    g_fsNegCacheActive.store(fsNegReady, std::memory_order_release);
    if (g_cfg.enable_fs_neg_cache && !fsNegReady) {
        g_cfg.enable_fs_neg_cache = false;
    }
    LogMsg("[PERF][FSNEG] KERNEL32/CRT Mod/Data Negative Stat Cache (ga=%d fo=%d wf=%d wfs=%d ws=%d s64=%d s32=%d mv=%d cp=%d dl=%d md=%d rd=%d scd=%d errno=%d) -> active=%d",
           okGa?1:0, okFo?1:0, okWf?1:0, okWfs?1:0, okWs?1:0, okS64?1:0, okS32?1:0,
           okMv?1:0, okCp?1:0, okDl?1:0, okMd?1:0, okRd?1:0, okScd?1:0, g_msvcr90Errno?1:0, fsNegReady?1:0);

    const bool asyncBarriersOk = okGa && okMv && okCp && okDl && okMd && okRd && okCf && okFf &&
                                 okWf && okFw && okFc && okWfs && okFo && okWs && okS64 && okS32 && okEx && okCex;
    if (!g_cfg.enable_async_save || !asyncBarriersOk) {
        g_asyncSaveActive.store(false, std::memory_order_release);
    }
    LogMsg("[SAVE] Async Save Barriers (k32=%d%d%d%d crt=%d%d%d stat=%d%d%d exit=%d%d%d) -> barriersOk=%d",
           okCf?1:0, okFf?1:0, okRd?1:0, okGa?1:0,
           okWf?1:0, okFw?1:0, okFc?1:0,
           okWs?1:0, okS64?1:0, okS32?1:0,
           okEx?1:0, okEx2?1:0, okCex?1:0,
           asyncBarriersOk ? 1 : 0);
    return asyncBarriersOk;
}

// =============================================================================
// Optional Zero-Dependency 127.0.0.1 Read-Only JSON Metrics Thread (Default OFF)
// =============================================================================
DWORD WINAPI HttpMetricsThread(LPVOID) {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 0;

    SOCKET srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (srv == INVALID_SOCKET) { WSACleanup(); return 0; }

    int opt = 1;
    // Exclusive port binding prevents Master & Caves shards from hijacking each other's port
    setsockopt(srv, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 ONLY!
    addr.sin_port = htons(static_cast<u_short>(g_cfg.http_port));

    if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(srv, 4) != 0) {
        LogMsg("[HTTP] Port %d already bound by another shard/process; endpoint disabled on this shard.", g_cfg.http_port);
        closesocket(srv);
        WSACleanup();
        return 0;
    }

    LogMsg("[HTTP] Read-only metrics endpoint listening on http://127.0.0.1:%d/metrics", g_cfg.http_port);

    while (!g_stopHttp.load(std::memory_order_relaxed)) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(srv, &rfds);
        timeval tv = {0, 250000};
        if (select(0, &rfds, nullptr, nullptr, &tv) <= 0) continue;

        SOCKET cli = accept(srv, nullptr, nullptr);
        if (cli == INVALID_SOCKET) continue;

        // Bound recv/send timeout so idle or malicious local connections can never hang this thread
        DWORD ioTimeoutMs = 2000;
        setsockopt(cli, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&ioTimeoutMs), sizeof(ioTimeoutMs));
        setsockopt(cli, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&ioTimeoutMs), sizeof(ioTimeoutMs));

        char req[512];
        int rlen = recv(cli, req, sizeof(req) - 1, 0);
        if (rlen <= 0) {
            closesocket(cli);
            continue;
        }

        double avg = 0, p95 = 0, p99 = 0;
        g_stats.ComputePercentiles(avg, p95, p99);

        char body[3072];
        int blen = std::snprintf(body, sizeof(body),
            "{\"version\":\"DST-HyperCore-Server/1.6.0\","
            "\"sim_tick_rate\":%d,"
            "\"ticks\":%llu,\"tick_avg_us\":%.1f,\"tick_p95_us\":%.1f,\"tick_p99_us\":%.1f,"
            "\"tick_max_us\":%llu,\"tick_overruns\":%llu,"
            "\"gc_calls\":%llu,\"gc_us_sum\":%llu,\"gc_max_us\":%llu,\"gc_full\":%llu,\"gc_stw_blocked\":%llu,\"gc_kb\":%d,"
            "\"find_entities_calls\":%llu,\"find_entities_us_sum\":%llu,"
            "\"spatial_queries\":%llu,\"spatial_zerocopy\":%llu,\"light_spatial_queries\":%llu,\"spatial_cells\":%llu,"
            "\"astar_inits\":%llu,\"astar_steps\":%llu,\"astar_us_sum\":%llu,\"astar_completed\":%llu,\"astar_failed\":%llu,"
            "\"fastdump_calls\":%llu,\"fastdump_us_sum\":%llu,\"fastdump_bytes\":%llu,"
            "\"fastjson_enc_calls\":%llu,\"fastjson_dec_calls\":%llu,\"fastjson_fallbacks\":%llu,"
            "\"serialize_calls\":%llu,\"serialize_us_sum\":%llu,"
            "\"lua_update_calls\":%llu,\"lua_update_us_sum\":%llu,\"lua_update_max\":%llu,"
            "\"net_update_calls\":%llu,\"net_update_us_sum\":%llu,\"net_update_max\":%llu,"
            "\"replica_update_calls\":%llu,\"replica_update_us_sum\":%llu,\"replica_update_max\":%llu,"
            "\"replica_construct_budget\":%u,\"replica_paced_deferred\":%llu,\"replica_paced_bursts\":%llu,"
            "\"replica_paced_parent_promotes\":%llu,\"replica_paced_near_bypasses\":%llu,"
            "\"fs_neg_hits\":%llu,\"fs_neg_misses\":%llu,"
            "\"async_save_enqueued\":%llu,\"async_save_committed\":%llu,\"async_save_bytes\":%llu,\"async_save_flushes\":%llu,\"async_save_errors\":%llu,"
            "\"deflate_calls\":%llu,\"deflate_main_calls\":%llu,\"deflate_us_sum\":%llu,\"deflate_in\":%llu,\"deflate_out\":%llu,"
            "\"uncomp_calls\":%llu,\"uncomp_out_bytes\":%llu,\"uncomp_us_sum\":%llu,\"blackbox_spikes\":%llu}",
            g_activeSimTickRate.load(std::memory_order_relaxed),
            static_cast<unsigned long long>(g_stats.ticks_total.load()), avg, p95, p99,
            static_cast<unsigned long long>(g_stats.tick_us_max.load()),
            static_cast<unsigned long long>(g_stats.tick_overrun_count.load()),
            static_cast<unsigned long long>(g_stats.gc_calls.load()),
            static_cast<unsigned long long>(g_stats.gc_us_sum.load()),
            static_cast<unsigned long long>(g_stats.gc_us_max.load()),
            static_cast<unsigned long long>(g_stats.gc_full_count.load()),
            static_cast<unsigned long long>(g_stats.gc_stw_blocked.load()),
            g_stats.gc_last_kb.load(),
            static_cast<unsigned long long>(g_stats.find_entities_calls.load()),
            static_cast<unsigned long long>(g_stats.find_entities_us_sum.load()),
            static_cast<unsigned long long>(g_stats.spatial_queries.load()),
            static_cast<unsigned long long>(g_stats.spatial_zerocopy_hits.load()),
            static_cast<unsigned long long>(g_stats.light_spatial_queries.load()),
            static_cast<unsigned long long>(g_stats.spatial_cells_visited.load()),
            static_cast<unsigned long long>(g_stats.astar_inits.load()),
            static_cast<unsigned long long>(g_stats.astar_steps.load()),
            static_cast<unsigned long long>(g_stats.astar_us_sum.load()),
            static_cast<unsigned long long>(g_stats.astar_completed.load()),
            static_cast<unsigned long long>(g_stats.astar_failed.load()),
            static_cast<unsigned long long>(g_stats.fastdump_calls.load()),
            static_cast<unsigned long long>(g_stats.fastdump_us_sum.load()),
            static_cast<unsigned long long>(g_stats.fastdump_bytes_sum.load()),
            static_cast<unsigned long long>(g_stats.fastjson_enc_calls.load()),
            static_cast<unsigned long long>(g_stats.fastjson_dec_calls.load()),
            static_cast<unsigned long long>(g_stats.fastjson_fallbacks.load()),
            static_cast<unsigned long long>(g_stats.serialize_world_calls.load()),
            static_cast<unsigned long long>(g_stats.serialize_world_us_sum.load()),
            static_cast<unsigned long long>(g_stats.prof_lua_update_calls.load()),
            static_cast<unsigned long long>(g_stats.prof_lua_update_us_sum.load()),
            static_cast<unsigned long long>(g_stats.prof_lua_update_us_max.load()),
            static_cast<unsigned long long>(g_stats.prof_net_update_calls.load()),
            static_cast<unsigned long long>(g_stats.prof_net_update_us_sum.load()),
            static_cast<unsigned long long>(g_stats.prof_net_update_us_max.load()),
            static_cast<unsigned long long>(g_stats.prof_replica_update_calls.load()),
            static_cast<unsigned long long>(g_stats.prof_replica_update_us_sum.load()),
            static_cast<unsigned long long>(g_stats.prof_replica_update_us_max.load()),
            g_effectiveReplicaBudget.load(std::memory_order_relaxed),
            static_cast<unsigned long long>(g_replicaStats.paced_deferred_total.load()),
            static_cast<unsigned long long>(g_replicaStats.paced_burst_ticks.load()),
            static_cast<unsigned long long>(g_replicaStats.paced_parent_promotes.load()),
            static_cast<unsigned long long>(g_replicaStats.paced_near_bypasses.load()),
            static_cast<unsigned long long>(g_stats.fs_neg_hits.load()),
            static_cast<unsigned long long>(g_stats.fs_neg_misses.load()),
            static_cast<unsigned long long>(GetAsyncSave().EnqueuedCount()),
            static_cast<unsigned long long>(GetAsyncSave().CommittedCount()),
            static_cast<unsigned long long>(GetAsyncSave().CommittedBytes()),
            static_cast<unsigned long long>(GetAsyncSave().FlushCount()),
            static_cast<unsigned long long>(GetAsyncSave().ErrorCount()),
            static_cast<unsigned long long>(g_stats.deflate_calls.load()),
            static_cast<unsigned long long>(g_stats.deflate_main_calls.load()),
            static_cast<unsigned long long>(g_stats.deflate_us_sum.load()),
            static_cast<unsigned long long>(g_stats.deflate_in_bytes.load()),
            static_cast<unsigned long long>(g_stats.deflate_out_bytes.load()),
            static_cast<unsigned long long>(g_stats.uncomp_calls.load()),
            static_cast<unsigned long long>(g_stats.uncomp_out_bytes.load()),
            static_cast<unsigned long long>(g_stats.uncomp_us_sum.load()),
            static_cast<unsigned long long>(g_blackboxTotalSpikes.load(std::memory_order_relaxed)));
        if (blen < 0) blen = 0;
        else if (blen >= static_cast<int>(sizeof(body))) blen = static_cast<int>(sizeof(body)) - 1;

        char hdr[256];
        int hlen = std::snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Content-Length: %d\r\nConnection: close\r\n\r\n",
            blen);
        if (hlen < 0) hlen = 0;
        else if (hlen >= static_cast<int>(sizeof(hdr))) hlen = static_cast<int>(sizeof(hdr)) - 1;
        send(cli, hdr, hlen, 0);
        if (blen > 0) send(cli, body, blen, 0);
        closesocket(cli);
    }

    closesocket(srv);
    WSACleanup();
    return 0;
}

// =============================================================================
// Verified Unique Cross-Version AOB Table (33/33 Verified Unique in Server Exe)
// =============================================================================
constexpr int16_t kAobSimUpdate[] = {
    0x40, 0x57, 0x41, 0x55, 0x41, 0x56, 0xB8, 0x10, 0x13, 0x00, 0x00, 0xE8, -1, -1, -1, -1,
    0x48, 0x2B, 0xE0, 0x44, 0x0F, 0x29, 0x8C, 0x24, 0xC0, 0x12, 0x00, 0x00, 0x44, 0x0F, 0x29, 0x94
};
constexpr int16_t kAobCollectGarbage[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x45, 0x33, 0xC0, 0x48, 0x8B, 0xF9,
    0x48, 0x8B, 0x89, 0xB0, 0x00, 0x00, 0x00, 0x41, 0x8D, 0x50, 0x03, 0xE8, -1, -1, -1, -1,
    0x3B, 0x87, 0x4C, 0x02, 0x00, 0x00, 0x7E, 0x54, 0x48, 0x8B, 0x8F, 0xB0
};
constexpr int16_t kAobSimLuaAlloc[] = {
    0x40, 0x53, 0x55, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x78, 0x48, 0x8B, 0x05, -1, -1, -1, -1,
    0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x68, 0x80, 0xB9, 0x18, 0x01, 0x00, 0x00, 0x00, 0x48, 0x89, 0x74, 0x24
};
constexpr int16_t kAobWgLuaAlloc[] = {
    0x4C, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC,
    0x20, 0x49, 0x8B, 0xF9, 0x49, 0x8B, 0xE8, 0x48, 0x8B, 0xF2, 0x4D, 0x85, 0xC9, 0x75, 0x12, 0x48, 0x8B, 0xCA, 0xE8, -1
};
constexpr int16_t kAobTrackLuaMem[] = {
    0x4C, 0x8B, 0xDC, 0x41, 0x54, 0x41, 0x55, 0x48, 0x81, 0xEC, 0xC8, 0x02, 0x00, 0x00, 0x48, 0x8B,
    0x05, -1, -1, -1, -1, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xB0, 0x02, 0x00, 0x00, 0x80, 0xB9, 0x19, 0x01
};
constexpr int16_t kAobEntityMgrFind[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0x41, 0x10, 0x48,
    0x8B, 0xAC, 0x24, 0xD0, 0x00, 0x00, 0x00, 0x48
};
constexpr int16_t kAobSimFindEnts[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48,
    0x89, 0x78, 0x20, 0x41, 0x54, 0x48, 0x81, 0xEC, 0xA0, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xE9, 0x0F,
    0x29, 0x70, 0xE8, 0x48, 0x8B, 0xCA, 0x0F, 0x29
};
constexpr int16_t kAobSimFindEntsReg[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00,
    0x00, 0x00, 0x48, 0x8B, 0xF1, 0x48, 0x8B, 0xCA, 0x0F, 0x29, 0x74, 0x24, 0x70, 0x0F, 0x29, 0x7C,
    0x24, 0x60, 0x48, 0x8B, 0xFA, 0xE8, -1, -1
};
constexpr int16_t kAobSerializeWorld[] = {
    0x40, 0x53, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00,
    0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xF0, 0x00, 0x00, 0x00,
    0x4C, 0x8B, 0xF9, 0x48, 0x8B, 0x0D, -1, -1, -1, -1
};
constexpr int16_t kAobCompress2[] = {
    0x4C, 0x8B, 0xDC, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x8B, 0x02, 0x4D, 0x89, 0x43,
    0x98, 0x45, 0x89, 0x4B, 0xA0, 0x49, 0x89, 0x4B, 0xA8, 0x89, 0x44, 0x24, 0x38, 0x33, 0xC0, 0x48,
    0x8B, 0xFA, 0x8B, 0x94, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x4C, 0x8D, 0x05
};
constexpr int16_t kAobLuaNewState[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0xFA, 0x48, 0x8B, 0xF1, 0x41, 0xB9, 0x78, 0x02, 0x00, 0x00, 0x45, 0x33, 0xC0, 0x33, 0xD2,
    0x48, 0x8B, 0xCF, 0xFF
};
constexpr int16_t kAobLuaOpenLibs[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x05, -1, -1, -1, -1,
    0x48, 0x8B, 0xF9, 0x48, 0x8D, 0x1D, -1, -1, -1, -1, 0x48, 0x85, 0xC0, 0x74, 0x35, 0x45, 0x33, 0xC0, 0x48
};
constexpr int16_t kAobLuaGc[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0x59, 0x20, 0x33, 0xFF, 0x48, 0x8B, 0xF1, 0x83, 0xFA, 0x08, 0x0F, 0x87, -1, -1, -1, -1, 0x48, 0x8D, 0x0D
};
constexpr int16_t kAobLuaCheckStack[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x81, 0xFA, 0x40, 0x1F, 0x00, 0x00,
    0x48, 0x8B, 0xD9, 0x7F, 0x75, 0x4C, 0x8B, 0x41, 0x10, 0x48, 0x63, 0xFA, 0x49, 0x8B, 0xC0, 0x48
};
constexpr int16_t kAobLuaGetTop[] = {
    0x48, 0x8B, 0x41, 0x10, 0x48, 0x2B, 0x41, 0x18, 0x48, 0xC1, 0xF8, 0x04, 0xC3
};
constexpr int16_t kAobLuaSetTop[] = {
    0x4C, 0x8B, 0xC1, 0x85, 0xD2, 0x78, 0x40, 0x48, 0x8B, 0x41, 0x18, 0x4C, 0x63, 0xCA, 0x4D, 0x03,
    0xC9, 0x4A, 0x8D, 0x14, 0xC8, 0x48, 0x39, 0x51, 0x10, 0x73, 0x1F, 0x33, 0xD2, 0x0F, 0x1F, 0x00
};
constexpr int16_t kAobLuaPushValue[] = {
    0x48, 0x83, 0xEC, 0x28, 0x4C, 0x8B, 0xD1, 0xE8, -1, -1, -1, -1, 0x4D, 0x8B, 0x42, 0x10,
    0x48, 0x8B, 0x10, 0x49, 0x89, 0x10, 0x8B, 0x40, 0x08, 0x41, 0x89, 0x40, 0x08, 0x49, 0x83, 0x42
};
constexpr int16_t kAobLuaType[] = {
    0x48, 0x83, 0xEC, 0x28, 0xE8, -1, -1, -1, -1, 0x48, 0x8D, 0x0D, -1, -1, -1, -1,
    0x48, 0x3B, 0xC1, 0x75, 0x08, 0x83, 0xC8, 0xFF, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x8B, 0x40, 0x08
};
constexpr int16_t kAobLuaToNumber[] = {
    0x48, 0x83, 0xEC, 0x38, 0xE8, -1, -1, -1, -1, 0x83, 0x78, 0x08, 0x03, 0x74, 0x1B, 0x48,
    0x8D, 0x54, 0x24, 0x20, 0x48, 0x8B, 0xC8, 0xE8, -1, -1, -1, -1, 0x48, 0x85, 0xC0, 0x75
};
constexpr int16_t kAobLuaToBoolean[] = {
    0x48, 0x83, 0xEC, 0x28, 0xE8, -1, -1, -1, -1, 0x8B, 0x48, 0x08, 0x85, 0xC9, 0x74, 0x14,
    0x83, 0xF9, 0x01, 0x75, 0x05, 0x83, 0x38, 0x00, 0x74, 0x0A, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x48
};
constexpr int16_t kAobLuaToLString[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x49,
    0x8B, 0xD8, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0xE8, -1, -1, -1, -1, 0x4C, 0x8B, 0xD0, 0x83, 0x78, 0x08, 0x04, 0x74
};
constexpr int16_t kAobLuaPushNil[] = {
    0x48, 0x8B, 0x41, 0x10, 0xC7, 0x40, 0x08, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0x41, 0x10, 0x10, 0xC3
};
constexpr int16_t kAobLuaPushNumber[] = {
    0x48, 0x8B, 0x41, 0x10, 0xF2, 0x0F, 0x11, 0x08, 0xC7, 0x40, 0x08, 0x03, 0x00, 0x00, 0x00, 0x48, 0x83, 0x41, 0x10, 0x10, 0xC3
};
constexpr int16_t kAobLuaPushLString[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x80, 0xB9, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xF0, 0x48, 0x8B, 0xEA, 0x48, 0x8B, 0xF9
};
constexpr int16_t kAobLuaPushString[] = {
    0x48, 0x83, 0xEC, 0x28, 0x4C, 0x8B, 0xC9, 0x48, 0x85, 0xD2, 0x75, 0x11, 0x48, 0x8B, 0x41, 0x10,
    0x89, 0x50, 0x08, 0x48, 0x83, 0x41, 0x10, 0x10, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x48, 0x83, 0xC9
};
constexpr int16_t kAobLuaPushCClosure[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80,
    0xB9, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x63, 0xF8, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xD9, 0x74, 0x13, 0x4C, 0x8B, 0x49
};
constexpr int16_t kAobLuaPushBoolean[] = {
    0x4C, 0x8B, 0x41, 0x10, 0x33, 0xC0, 0x85, 0xD2, 0x0F, 0x95, 0xC0, 0x41, 0xC7, 0x40, 0x08, 0x01,
    0x00, 0x00, 0x00, 0x41, 0x89, 0x00, 0x48, 0x83, 0x41, 0x10, 0x10, 0xC3
};
constexpr int16_t kAobLuaGetField[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x4D,
    0x8B, 0xD0, 0x48, 0x8B, 0xF1, 0xE8, -1, -1, -1, -1, 0x48, 0x83, 0xC9, 0xFF, 0x48, 0x8B, 0xD8,
    0x49, 0x8B, 0xFA, 0x33, 0xC0, 0x49, 0x8B, 0xD2, 0xF2, 0xAE, 0x48, 0xF7, 0xD1, 0x4C, 0x8D, 0x41,
    0xFF, 0x48, 0x8B, 0xCE, 0xE8, -1, -1, -1, -1, 0x4C, 0x8B, 0x4E, 0x10, 0x4C, 0x8D, 0x44, 0x24,
    0x20, 0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCE, 0x48, 0x89, 0x44
};
constexpr int16_t kAobLuaCreateTable[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x80, 0xB9, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x41, 0x8B, 0xF0, 0x8B, 0xEA, 0x48, 0x8B, 0xF9, 0x74
};
constexpr int16_t kAobLuaSetField[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x4D,
    0x8B, 0xD0, 0x48, 0x8B, 0xF1, 0xE8, -1, -1, -1, -1, 0x48, 0x83, 0xC9, 0xFF, 0x48, 0x8B, 0xD8,
    0x49, 0x8B, 0xFA, 0x33, 0xC0, 0x49, 0x8B, 0xD2, 0xF2, 0xAE, 0x48, 0xF7, 0xD1, 0x4C, 0x8D, 0x41,
    0xFF, 0x48, 0x8B, 0xCE, 0xE8, -1, -1, -1, -1, 0x4C, 0x8B, 0x4E, 0x10, 0x4C, 0x8D, 0x44, 0x24,
    0x20, 0x49, 0x83, 0xE9, 0x10, 0x48, 0x8B, 0xD3, 0x48, 0x8B
};
constexpr int16_t kAobLuaToPointer[] = {
    0x48, 0x83, 0xEC, 0x28, 0x44, 0x8B, 0xD2, 0x4C, 0x8B, 0xD9, 0xE8, -1, -1, -1, -1, 0x44,
    0x8B, 0x48, 0x08, 0x41, 0x83, 0xE9, 0x02, 0x74, 0x1F, 0x41, 0x83, 0xE9, 0x03, 0x74, 0x3D,
    0x41, 0x83, 0xE9, 0x01, 0x74, 0x37, 0x41, 0x83, 0xE9, 0x01, 0x74, 0x0D
};
constexpr int16_t kAobSpatialGridQueryPrologue[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x53, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56,
    0x41, 0x57, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00, 0x44, 0x0F, 0x29, 0x54, 0x24, 0x70,
    0x0F, 0x29, 0x70, 0xB8, 0x44, 0x0F, 0x28, 0xD2
};
constexpr int16_t kAobEntityVisitor[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x48, 0x89, 0x54, 0x24, 0x10,
    0x57, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xF6, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x89, 0x74,
    0x24, 0x40
};
constexpr int16_t kAobLuaNext[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, -1, -1, -1, -1, 0x4C, 0x8B,
    0x43, 0x10, 0x48, 0x8B, 0x10, 0x49, 0x83, 0xE8, 0x10, 0x48, 0x8B, 0xCB, 0xE8, -1, -1, -1, -1, 0x85, 0xC0, 0x74
};
constexpr int16_t kAobLuaRawSet[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0xF9, 0xE8, -1, -1, -1, -1, 0x48, 0x8B, 0x5F, 0x10, 0x48, 0x8B, 0x10, 0x4C, 0x8D,
    0x43, 0xE0, 0x48, 0x8B, 0xCF, 0x48, 0x8B, 0xF0
};
constexpr int16_t kAobLuaRawSetI[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x45,
    0x8B, 0xD0, 0x48, 0x8B, 0xF9, 0xE8, -1, -1, -1, -1, 0x48, 0x8B, 0x10, 0x48, 0x8B, 0x5F,
    0x10, 0x45, 0x8B, 0xC2
};
constexpr int16_t kAobUncompress[] = {
    0x40, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x8B, 0x02, 0x48, 0x8B, 0xFA,
    0x4C, 0x89, 0x44, 0x24, 0x20, 0x89, 0x44, 0x24, 0x38, 0x33, 0xC0, 0x48, 0x89, 0x4C, 0x24, 0x30
};
// Lua 5.1.4 lua_close (21-byte RIP-free prologue, verified unique 1-match at RVA 0x422d50)
constexpr int16_t kAobLuaClose[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x20,
    0x48, 0x8B, 0x98, 0xB0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCB, 0x48, 0x8B, 0x53, 0x40
};
// Exclusive Simulation::Update fixed-timestep loader (verified unique 1-match on Server & Client!)
constexpr int16_t kAobSimUpdateTick[] = {
    0xF3, 0x0F, 0x10, 0x05, -1, -1, -1, -1, 0xF3, 0x0F, 0x10, 0x35, -1, -1, -1, -1,
    0x48, 0x8D, 0x84, 0x24, 0xA0, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0xC6, 0x44, 0x0F, 0x2F, 0xD0,
    0xF3, 0x0F, 0x11, 0x84, 0x24, 0xA0, 0x00, 0x00, 0x00, 0x77, 0x08, 0x48, 0x8D, 0x84, 0x24, 0xA8,
    0x00, 0x00, 0x00, 0x80, 0x7B, 0x48, 0x00, 0xF3, 0x44, 0x0F, 0x10, 0x43, 0x44, 0xF3, 0x44, 0x0F,
    0x59, 0x00, 0x75, 0x0C, 0x80, 0x7B, 0x49, 0x00, 0x75, 0x06, 0x80, 0x7B, 0x4A, 0x00, 0x74, 0x0B,
    0x80, 0x7B, 0x4E, 0x00, 0x75, 0x05, 0x40, 0xB7, 0x01, 0xEB, 0x03, 0x40, 0x32, 0xFF, 0xF2, 0x44,
    0x0F, 0x10, 0x1D, -1, -1, -1, -1
};
// Exclusive secondary mTick*(1/30) subsystems & SetNetworkTickRate + Server FrameLimiter::Wait (verified unique!)
constexpr int16_t kAobAnimShaderTick[] = {
    0x66, 0x0F, 0xEF, 0xF6, 0x48, 0x8B, 0x51, 0x28, 0x48, 0x8D, 0x8C, 0x24, 0x30, 0x02, 0x00, 0x00,
    0x8B, 0x82, 0x90, 0x00, 0x00, 0x00, 0xF3, 0x48, 0x0F, 0x2A, 0xF0,
    0x48, 0x8D, 0x84, 0x24, 0xC0, 0x01, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x28,
    0xC7, 0x44, 0x24, 0x20, 0x04, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x35, -1, -1, -1, -1,
    0xF3, 0x0F, 0x58, 0xB2, 0x94, 0x00, 0x00, 0x00
};
constexpr int16_t kAobPathfinderTick[] = {
    0x49, 0x8B, 0x4D, 0x18, 0x66, 0x0F, 0xEF, 0xC0, 0x48, 0x8B, 0x91, 0xF0, 0x00, 0x00, 0x00,
    0x8B, 0x82, 0x90, 0x00, 0x00, 0x00, 0xF3, 0x48, 0x0F, 0x2A, 0xC0,
    0xF3, 0x0F, 0x59, 0x05, -1, -1, -1, -1, 0xF3, 0x0F, 0x58, 0x82, 0x94, 0x00, 0x00, 0x00
};
constexpr int16_t kAobSpatialTick[] = {
    0x41, 0x8B, 0x80, 0x90, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xEA, 0x4C, 0x8B, 0xE1, 0x49, 0x8B, 0xF1,
    0xF3, 0x48, 0x0F, 0x2A, 0xC0, 0xB8, 0x89, 0x88, 0x88, 0x88,
    0xF3, 0x0F, 0x59, 0x05, -1, -1, -1, -1, 0xF3, 0x41, 0x0F, 0x58, 0x80, 0x94, 0x00, 0x00, 0x00
};
constexpr int16_t kAobSimTimeDiffTick[] = {
    0x8B, 0x41, 0x08, 0x66, 0x0F, 0xEF, 0xC0, 0x2B, 0x42, 0x08, 0xF3, 0x48, 0x0F, 0x2A, 0xC0,
    0xF3, 0x0F, 0x59, 0x05, -1, -1, -1, -1, 0xF3, 0x0F, 0x58, 0x41, 0x0C, 0xF3, 0x0F, 0x5C, 0x42, 0x0C, 0xC3
};
constexpr int16_t kAobSetNetTickRate[] = {
    0x89, 0x54, 0x24, 0x10, 0x4C, 0x8B, 0xDC, 0x53, 0x55, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x50,
    0x48, 0x8B, 0xE9, 0x41, 0xBC, 0x0A, 0x00, 0x00, 0x00, 0x85, 0xD2, 0x74, 0x2B
};
constexpr int16_t kAobNetTickScale[] = {
    0xF3, 0x0F, 0x10, 0x05, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1,
    0x48, 0x8B, 0x8D, 0x88, 0x02, 0x00, 0x00, 0x33, 0xD2, 0xF3, 0x48, 0x0F, 0x2C, 0xC0,
    0x41, 0xF7, 0xF4, 0x85, 0xD2, 0x49, 0x8B, 0xD6, 0x0F, 0x44, 0xF0, 0xE8, -1, -1, -1, -1,
    0x48, 0x8B, 0x8D, 0x88, 0x02, 0x00, 0x00, 0x40, 0x0F, 0xB6, 0xD6, 0xE8, -1, -1, -1, -1,
    0x48, 0x8B, 0x8D, 0x88, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xD7, 0xE8, -1, -1, -1, -1
};
constexpr int16_t kAobSimUpdateNetMgr[] = {
    0x48, 0x8B, 0x0D, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1, 0x84, 0xC0, 0x74, 0x0A,
    0xF3, 0x0F, 0x10, 0x05, -1, -1, -1, -1, 0xEB, 0x08, 0xF3, 0x0F, 0x10, 0x05, -1, -1, -1, -1,
    0xF3, 0x0F, 0x10, 0x35, -1, -1, -1, -1
};
constexpr int16_t kAobServerFrameLimiterWait[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x50,
    0x0F, 0x29, 0x74, 0x24, 0x40, 0x0F, 0x29, 0x7C, 0x24, 0x30,
    0x0F, 0x57, 0xFF, 0x0F, 0xB6, 0xDA, 0x48, 0x8B, 0xF9, 0x44, 0x0F, 0x29, 0x44, 0x24, 0x20,
    0xF3, 0x44, 0x0F, 0x10, 0x05, -1, -1, -1, -1
};
// Verified unique 1-match RakNet SendBuffered & SendBufferedList SetEvent guard AOBs
// Vanilla RakNet skips quitAndDataEvents.SetEvent() when priority != IMMEDIATE_PRIORITY (0),
// causing all HIGH/MEDIUM priority world snapshots & RPCs to stall 0-10ms in WaitOnEvent(10).
constexpr int16_t kAobRakNetSendBufWake[] = {
    0x45, 0x85, 0xF6, 0x75, 0x0C, 0x48, 0x8D, 0x8D, 0xD0, 0x06, 0x00, 0x00, 0xE8, -1, -1, -1, -1
};
constexpr int16_t kAobRakNetSendBufListWake[] = {
    0x85, 0xFF, 0x75, 0x0C, 0x49, 0x8D, 0x8D, 0xD0, 0x06, 0x00, 0x00, 0xE8, -1, -1, -1, -1
};
// Verified unique 1-match Pathfinder AStar::Reset, AStar::Init, AStar::Step AOBs (v1.3.0)
constexpr int16_t kAobAStarReset[] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x79, 0x40, 0x48, 0x8B, 0x59, 0x38, 0x33, 0xED, 0x48, 0x8B
};
constexpr int16_t kAobAStarInit[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
    0x8B, 0xDA, 0x48, 0x8B, 0xF1, 0xE8, -1, -1, -1, -1, 0x33, 0xFF, 0x48, 0x89, 0x5E, 0x50
};
constexpr int16_t kAobAStarStep[] = {
    0x89, 0x54, 0x24, 0x10, 0x57, 0x41, 0x54, 0x48, 0x81, 0xEC, 0xB8, 0x00, 0x00, 0x00, 0x83, 0x79,
    0x08, 0x00, 0x8B, 0xFA, 0x4C, 0x8B, 0xE1, 0x74, 0x0D, 0x33, 0xC0, 0x48, 0x81, 0xC4, 0xB8, 0x00
};
// Verified unique 1-match Server FastLoad AOBs (100% instruction parity with client)
constexpr int16_t kAobUnzReadCurrentFile[] = {
    0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xF6, 0x48, 0x85, 0xC9, 0x75, 0x09, 0x8D, 0x46, 0x9A,
    0x48, 0x83, 0xC4, 0x20, 0x5E, 0xC3, 0x48, 0x89, 0x7C, 0x24, 0x38, 0x48, 0x8B, 0xB9, 0xC8, 0x00,
    0x00, 0x00, 0x48, 0x85
};
constexpr int16_t kAobInflateFast[] = {
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41,
    0x57, 0x48, 0x83, 0xEC, 0x38, 0x4C, 0x8B, 0x51, 0x28, 0x44, 0x8B, 0x71, 0x08, 0x48, 0x8B, 0x31,
    0x4C, 0x8B, 0x41, 0x10
};
constexpr int16_t kAobVecFillA[] = {
    0x48, 0x8B, 0xCB, 0x48, 0x89, 0x5C, 0x24, 0x20, 0x48, 0x8B, 0xD6, 0x48, 0x89, 0x54, 0x24, 0x28,
    0x48, 0x85, 0xC9, 0x74, 0x19, 0x41, 0x0F, 0xB6, 0x45, 0x00, 0x88, 0x02, 0x48, 0xFF, 0xC9, 0x48,
    0x89, 0x4C, 0x24, 0x20, 0x48, 0xFF, 0xC2, 0x48, 0x89, 0x54, 0x24, 0x28, 0xEB, 0xE2
};
constexpr int16_t kAobVecFillB[] = {
    0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0x89, 0x4C, 0x24, 0x40, 0x0F, 0x1F, 0x40, 0x00, 0x48, 0x85,
    0xC0, 0x74, 0x15, 0x44, 0x88, 0x21, 0x48, 0xFF, 0xC8, 0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0xFF,
    0xC1, 0x48, 0x89, 0x4C, 0x24, 0x40, 0xEB, 0xE6
};
// Verified 1-of-1 AOBs for Verified Atomic Save-Commit (WritePersistentFile), RPC QoS & Tick Sub-Phase Profiler
constexpr int16_t kAobWritePersistentFile[] = {
    0x40, 0x53, 0x55, 0x56, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xD8, 0x00, 0x00,
    0x00, 0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xB8
};
constexpr int16_t kAobRpc4Call[] = {
    0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x48, 0x81, 0xEC, 0x30, 0x03, 0x00, 0x00,
    0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24,
    0x20, 0x03, 0x00, 0x00, 0x48, 0x8B, 0x9C, 0x24
};
constexpr int16_t kAobSimLuaUpdate[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x48, 0x89, 0x70, 0x20, 0xF3, 0x0F, 0x11, 0x48, 0x10,
    0x57, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x44, 0x0F, 0xB6, 0x49, 0x4A
};
constexpr int16_t kAobNetMgrUpdate[] = {
    0x40, 0x55, 0x41, 0x55, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x30, 0x0A, 0x00, 0x00, 0x0F, 0x29, 0xB4,
    0x24, 0x00, 0x0A, 0x00, 0x00, 0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x33, 0xC4
};
constexpr int16_t kAobReplicaMgr3Update[] = {
    0x40, 0x56, 0x48, 0x81, 0xEC, 0x00, 0x02, 0x00, 0x00, 0x48, 0x8B, 0x05, -1, -1, -1, -1,
    0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xD0, 0x01, 0x00, 0x00, 0x48, 0x8B, 0xF1, 0x4C, 0x8D, 0x05
};
constexpr int16_t kAobQueryReplicaList[] = {
    0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x55, 0x57, 0x48, 0x81, 0xEC,
    0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9, 0x4C, 0x8D, 0x05, -1, -1, -1, -1
};
constexpr int16_t kAobNetworkProxyIsRelevant[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x49,
    0x8B, 0xE8, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xD9, 0x4C, 0x85, 0x81, 0xC0, 0x01, 0x00, 0x00
};
constexpr int16_t kAobNetworkProxyQueryDestr[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x0D, -1, -1, -1, -1,
    0x80, 0xB9, 0x08, 0x03, 0x00, 0x00, 0x00, 0x74, 0x0B, 0xB8, 0x01, 0x00, 0x00, 0x00
};
// cNetworkClient::SetPlayerProxy (1-of-1 AOB): verifies client+0x1b8 (playerProxy) & playerProxy+0x18 (Entity*)
constexpr int16_t kAobClientSetPlayerProxy[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x89, 0x91, 0xB8, 0x01, 0x00, 0x00,
    0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x8B, 0xD9, 0x80, 0xB8, 0x09, 0x03, 0x00, 0x00, 0x00,
    0x0F, 0x84
};

#define FIND_AOB(arr) FindUniqueAob(arr, sizeof(arr) / sizeof(arr[0]), #arr)

std::wstring DetectShardLogSuffix() {
    const wchar_t* cmd = GetCommandLineW();
    if (cmd) {
        const wchar_t* p = std::wcsstr(cmd, L"-shard");
        if (p) {
            p += 6;
            while (*p == L' ' || *p == L'\t' || *p == L'"') ++p;
            std::wstring shard;
            while (*p && *p != L' ' && *p != L'\t' && *p != L'"' && shard.size() < 32) {
                wchar_t c = *p++;
                if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                    (c >= L'0' && c <= L'9') || c == L'_' || c == L'-') {
                    shard.push_back(c);
                }
            }
            if (!shard.empty()) {
                size_t n = 0;
                for (wchar_t c : shard) {
                    if (n + 1 >= sizeof(g_shardName)) break;
                    g_shardName[n++] = static_cast<char>(c & 0x7F);
                }
                g_shardName[n] = '\0';
                return L"_" + shard;
            }
        }
    }
    std::snprintf(g_shardName, sizeof(g_shardName), "server");
    wchar_t pidBuf[32];
    std::swprintf(pidBuf, 32, L"_pid%lu", static_cast<unsigned long>(GetCurrentProcessId()));
    return pidBuf;
}

bool g_initOutsideLoaderLock = false;

void InitServerHooks() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcFreq = (freq.QuadPart > 0) ? freq.QuadPart : 10000000LL;
    g_qpcToUs = 1000000.0 / static_cast<double>(g_qpcFreq);

    InitializeCriticalSection(&g_logCs);
    g_logCsInit = true;

    wchar_t modPath[MAX_PATH] = {};
    GetModuleFileNameW(g_selfMod, modPath, MAX_PATH);
    std::wstring mp(modPath);
    size_t sl = mp.find_last_of(L"\\/");
    g_binDir  = (sl != std::wstring::npos) ? mp.substr(0, sl + 1) : L"";
    g_iniPath = g_binDir + L"dst_hypercore.ini";
    g_logPath = g_binDir + L"dst_hypercore" + DetectShardLogSuffix() + L".log";

    // Truncate only this shard's dedicated log cleanly on new server boot
    if (FILE* f = _wfopen(g_logPath.c_str(), L"w")) std::fclose(f);

    LoadIniConfig();
    g_hostCaps = hc_server_tier::ProbeHostCaps();
    ApplyServerHardwareTierLive(g_cfg.hardware_tier, false);
    LogMsg("=== DST HyperCore Server Native Engine v1.6.0 Boot (PID=%lu, outsideLoaderLock=%d) ===",
           static_cast<unsigned long>(GetCurrentProcessId()), g_initOutsideLoaderLock ? 1 : 0);
    LogMsg("Config: profiler=%d mimalloc=%d spatial_zc=%d adaptive_gc=%d libdeflate=%d(L%d) fast_load=%d fast_dumper=%d fast_astar=%d fs_neg=%d async_save=%d rpc_qos=%d rep_fast=%d(pace=%u) sim_tick=%dHz http=%d",
           g_cfg.enable_profiler, g_cfg.enable_mimalloc, g_cfg.enable_spatial_zerocopy,
           g_cfg.enable_adaptive_gc, g_cfg.enable_libdeflate, g_cfg.libdeflate_level,
           g_cfg.enable_fast_load, g_cfg.enable_fast_dumper, g_cfg.enable_fast_astar,
           g_cfg.enable_fs_neg_cache, g_cfg.enable_async_save, g_cfg.enable_rpc_qos,
           g_cfg.enable_replica_fastpath, g_effectiveReplicaBudget.load(std::memory_order_relaxed),
           g_cfg.sim_tick_rate, g_cfg.enable_http_metrics);

    if (!InitTextSection() || !InitTrampolineSlab()) {
        LogMsg("[ERROR] Failed to initialize .text scanner or near-module trampoline slab!");
        return;
    }

    // Resolve Lua 5.1 C API functions
    g_lua.checkstack   = reinterpret_cast<decltype(g_lua.checkstack)>(FIND_AOB(kAobLuaCheckStack));
    g_lua.gettop       = reinterpret_cast<decltype(g_lua.gettop)>(FIND_AOB(kAobLuaGetTop));
    g_lua.settop       = reinterpret_cast<decltype(g_lua.settop)>(FIND_AOB(kAobLuaSetTop));
    g_lua.pushvalue    = reinterpret_cast<decltype(g_lua.pushvalue)>(FIND_AOB(kAobLuaPushValue));
    g_lua.type         = reinterpret_cast<decltype(g_lua.type)>(FIND_AOB(kAobLuaType));
    g_lua.tonumber     = reinterpret_cast<decltype(g_lua.tonumber)>(FIND_AOB(kAobLuaToNumber));
    g_lua.toboolean    = reinterpret_cast<decltype(g_lua.toboolean)>(FIND_AOB(kAobLuaToBoolean));
    g_lua.tolstring    = reinterpret_cast<decltype(g_lua.tolstring)>(FIND_AOB(kAobLuaToLString));
    g_lua.topointer    = reinterpret_cast<decltype(g_lua.topointer)>(FIND_AOB(kAobLuaToPointer));
    g_lua.pushnil      = reinterpret_cast<decltype(g_lua.pushnil)>(FIND_AOB(kAobLuaPushNil));
    g_lua.pushnumber   = reinterpret_cast<decltype(g_lua.pushnumber)>(FIND_AOB(kAobLuaPushNumber));
    g_lua.pushlstring  = reinterpret_cast<decltype(g_lua.pushlstring)>(FIND_AOB(kAobLuaPushLString));
    g_lua.pushstring   = reinterpret_cast<decltype(g_lua.pushstring)>(FIND_AOB(kAobLuaPushString));
    g_lua.pushcclosure = reinterpret_cast<decltype(g_lua.pushcclosure)>(FIND_AOB(kAobLuaPushCClosure));
    g_lua.pushboolean  = reinterpret_cast<decltype(g_lua.pushboolean)>(FIND_AOB(kAobLuaPushBoolean));
    g_lua.getfield     = reinterpret_cast<decltype(g_lua.getfield)>(FIND_AOB(kAobLuaGetField));
    g_lua.createtable  = reinterpret_cast<decltype(g_lua.createtable)>(FIND_AOB(kAobLuaCreateTable));
    g_lua.setfield     = reinterpret_cast<decltype(g_lua.setfield)>(FIND_AOB(kAobLuaSetField));
    g_lua.gc           = reinterpret_cast<decltype(g_lua.gc)>(FIND_AOB(kAobLuaGc));
    g_lua.next         = reinterpret_cast<decltype(g_lua.next)>(FIND_AOB(kAobLuaNext));
    g_lua.rawset       = reinterpret_cast<decltype(g_lua.rawset)>(FIND_AOB(kAobLuaRawSet));
    g_lua.rawseti      = reinterpret_cast<decltype(g_lua.rawseti)>(FIND_AOB(kAobLuaRawSetI));
    LogMsg("[LUA] C API resolved: ReadyForRegister=%d ReadyForFastDump=%d ReadyForFastJson=%d",
           g_lua.ReadyForRegister(), g_lua.ReadyForFastDump(), g_lua.ReadyForFastJson());

    // 1. Hook lua_newstate -> mimalloc
    g_simLuaAllocAddr = FIND_AOB(kAobSimLuaAlloc);
    g_wgLuaAllocAddr  = FIND_AOB(kAobWgLuaAlloc);
    g_trackLuaMem     = reinterpret_cast<TrackLuaMem_t>(FIND_AOB(kAobTrackLuaMem));
    if (uint8_t* pNewState = FIND_AOB(kAobLuaNewState)) {
        bool ok = InstallHookOrdered(pNewState, reinterpret_cast<void*>(&HookLuaNewState), 15,
                                     reinterpret_cast<void**>(&g_origLuaNewState));
        LogMsg("[HOOK] lua_newstate @ %p -> %s (simAlloc=%p wgAlloc=%p)",
               pNewState, ok ? "OK" : "FAIL", g_simLuaAllocAddr, g_wgLuaAllocAddr);
    }

    // 2. Hook luaL_openlibs -> auto-register HyperCore_* Lua APIs before any script/mod runs
    if (uint8_t* pOpenLibs = FIND_AOB(kAobLuaOpenLibs)) {
        const RelocItem rel = {10, 7, 3}; // mov rax, [rip + disp32] at +10
        bool ok = InstallHookOrdered(pOpenLibs, reinterpret_cast<void*>(&HookLuaOpenLibs), 17,
                                     reinterpret_cast<void**>(&g_origLuaOpenLibs), &rel, 1);
        LogMsg("[HOOK] luaL_openlibs @ %p -> %s", pOpenLibs, ok ? "OK" : "FAIL");
    }

    // 2B. Hook lua_close (21-byte RIP-free prologue) -> reset Anti-STW GC lifecycle on VM teardown
    if (uint8_t* pLuaClose = FIND_AOB(kAobLuaClose)) {
        bool ok = InstallHookOrdered(pLuaClose, reinterpret_cast<void*>(&HookLuaClose), 21,
                                     reinterpret_cast<void**>(&g_origLuaClose));
        LogMsg("[HOOK] lua_close (GC lifecycle reset) @ %p -> %s", pLuaClose, ok ? "OK" : "FAIL");
    }

    // 3. Hook Simulation::Update (Profiler + Adaptive GC feedback + outside-DllMain runtime init)
    if (uint8_t* pSimUpd = FIND_AOB(kAobSimUpdate)) {
        const RelocItem rel = {11, 5, 1}; // call __chkstk at +11
        bool ok = InstallHookOrdered(pSimUpd, reinterpret_cast<void*>(&HookSimUpdate), 19,
                                     reinterpret_cast<void**>(&g_origSimUpdate), &rel, 1);
        LogMsg("[HOOK] Simulation::Update @ %p -> %s", pSimUpd, ok ? "OK" : "FAIL");
    }

    // 4. Hook Simulation::CollectGarbage (30Hz Pressure-Driven Adaptive GC + Profiler)
    if (uint8_t* pGC = FIND_AOB(kAobCollectGarbage)) {
        g_setGcBudget = reinterpret_cast<SetGcBudget_t>(ResolveRel32Call(pGC + 0x8B));
        bool ok = InstallHookOrdered(pGC, reinterpret_cast<void*>(&HookCollectGarbage), 16,
                                     reinterpret_cast<void**>(&g_origCollectGarbage));
        LogMsg("[HOOK] Simulation::CollectGarbage @ %p (SetGcBudget=%p) -> %s",
               pGC, g_setGcBudget, ok ? "OK" : "FAIL");
    }

    // 4B. Hook lua_gc (15-byte RIP-free prologue) -> Anti-STW Full-GC Shield for external Lua/Mod collectgarbage("collect")
    if (g_cfg.enable_anti_stw_gc && g_lua.gc) {
        auto* pLuaGc = reinterpret_cast<uint8_t*>(g_lua.gc);
        bool ok = InstallHookOrdered(pLuaGc, reinterpret_cast<void*>(&HookLuaGc), 15,
                                     reinterpret_cast<void**>(&g_origLuaGc));
        if (ok && g_origLuaGc) {
            // Internal calls inside HookCollectGarbage bypass HookLuaGc via the unhooked trampoline
            g_lua.gc = g_origLuaGc;
        }
        LogMsg("[HOOK] lua_gc (Anti-STW Shield) @ %p -> %s", pLuaGc, ok ? "OK" : "FAIL");
    }

    // 5. Paired, Fail-Closed, Independently AOB-Verified SpatialGrid::Query Hook with Rollback!
    if (uint8_t* pEMFind = FIND_AOB(kAobEntityMgrFind)) {
        uint8_t* pSpatialQuery = ResolveRel32Call(pEMFind + 0x10F);
        uint8_t* pVisitor = pSpatialQuery ? ResolveRel32Call(pSpatialQuery + 0x239) : nullptr;
        const bool sqOk = VerifyAobAt(pSpatialQuery, kAobSpatialGridQueryPrologue,
                                      sizeof(kAobSpatialGridQueryPrologue) / sizeof(kAobSpatialGridQueryPrologue[0]));
        const bool evOk = VerifyAobAt(pVisitor, kAobEntityVisitor,
                                      sizeof(kAobEntityVisitor) / sizeof(kAobEntityVisitor[0]));
        if (sqOk && evOk) {
            g_entityVisitor = reinterpret_cast<EntityVisitor_t>(pVisitor);
            uint8_t savedEM[20] = {};
            bool ok1 = InstallHookOrdered(pEMFind, reinterpret_cast<void*>(&HookEntityMgrFindEntities), 20,
                                          reinterpret_cast<void**>(&g_origEntityMgrFind), nullptr, 0, savedEM);
            bool ok2 = false;
            if (ok1) {
                ok2 = InstallHookOrdered(pSpatialQuery, reinterpret_cast<void*>(&HookSpatialGridQuery), 17,
                                         reinterpret_cast<void**>(&g_origSpatialGridQuery));
                if (!ok2) {
                    // Paired rollback: keep g_origEntityMgrFind valid if restore ever fails so pass-through never null-derefs!
                    const bool restored = RestoreHookBytes(pEMFind, savedEM, 20);
                    if (restored) {
                        g_origEntityMgrFind = nullptr;
                    } else {
                        LogMsg("[CRITICAL][HOOK] SpatialGrid rollback RestoreHookBytes failed @ %p; retaining pass-through trampoline", pEMFind);
                    }
                }
            }
            LogMsg("[HOOK] SpatialGrid::Query @ %p (Visitor=%p, AOB=VERIFIED) -> %s",
                   pSpatialQuery, pVisitor, (ok1 && ok2) ? "OK" : "FAIL (rolled back)");
        } else {
            LogMsg("[HOOK] SpatialGrid::Query skipped (Fail-Closed AOB verify: sq=%d ev=%d)", sqOk, evOk);
        }
    }

    // 5B. Remaining 4 Hot Read-Only Server SpatialGrid::Query Callsites (5/5 total with Module 5!)
    if (g_cfg.enable_spatial_zerocopy) {
        constexpr int16_t kSqPrologue17[17] = {
            0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x53, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
        };
        struct DirectSqSite {
            const char* name;
            uint8_t pre[17];
            uint8_t post[12];
            void*   detour;
        };
        static const DirectSqSite kDirectSites[kNumDirectSqSites] = {
            {
                "FindEntities_DirectTag",
                {0x8B, 0xFA, 0x4D, 0x8D, 0x4B, 0xC8, 0xF3, 0x41, 0x0F, 0x11, 0x43, 0x38, 0x49, 0x8D, 0x53, 0x38, 0xE8},
                {0x48, 0x8B, 0x53, 0x10, 0x48, 0x8B, 0x4B, 0x08, 0x4C, 0x8B, 0xC2, 0x4C},
                reinterpret_cast<void*>(&HookDirectSpatialQuery<0>)
            },
            {
                "FindEntities_Registered",
                {0x03, 0xC2, 0x0F, 0xB6, 0xC0, 0x2B, 0xC2, 0x48, 0x8D, 0x54, 0x24, 0x30, 0x88, 0x44, 0x24, 0x40, 0xE8},
                {0x48, 0x8B, 0x53, 0x10, 0x48, 0x8B, 0x4B, 0x08, 0x4C, 0x8B, 0xC2, 0x4C},
                reinterpret_cast<void*>(&HookDirectSpatialQuery<1>)
            },
            {
                "LightWatcher_Intensity",
                {0xF3, 0x0F, 0x10, 0x10, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x60, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x64, 0xE8},
                {0x48, 0x83, 0xC4, 0x48, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC},
                reinterpret_cast<void*>(&HookDirectSpatialQuery<2>)
            },
            {
                "LightWatcher_RGB",
                {0xF3, 0x0F, 0x10, 0x10, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x78, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x7C, 0xE8},
                {0xF3, 0x0F, 0x10, 0x6C, 0x24, 0x60, 0x0F, 0x2F, 0xEE, 0x76, 0x28, 0xF3},
                reinterpret_cast<void*>(&HookDirectSpatialQuery<3>)
            },
        };
        int sqDirectOk = 0;
        for (size_t i = 0; i < kNumDirectSqSites; ++i) {
            uint8_t* cs = FindUniqueMaskedCallsite(kDirectSites[i].pre, 17, kDirectSites[i].post, 12, kDirectSites[i].name);
            uint8_t* sq = cs ? ResolveRel32Call(cs) : nullptr;
            const bool sqProlOk = sq && VerifyAobAt(sq, kSqPrologue17, 17);
            uint8_t* vis = sqProlOk ? ResolveRel32Call(sq + 0x239) : nullptr;
            if (cs && sqProlOk && IsInTextSection(vis)) {
                g_origDirectSq[i]    = reinterpret_cast<SpatialGridQuery_t>(sq);
                g_directSqVisitor[i] = reinterpret_cast<EntityVisitor_t>(vis);
                if (InstallRel32CallRelay(cs, kDirectSites[i].detour)) {
                    ++sqDirectOk;
                    LogMsg("[HOOK] SpatialGrid::Query[%zu] %s @ %p (SQ=%p Vis=%p) -> OK",
                           i, kDirectSites[i].name, cs, sq, vis);
                } else {
                    g_origDirectSq[i] = nullptr;
                    g_directSqVisitor[i] = nullptr;
                    LogMsg("[HOOK] SpatialGrid::Query[%zu] %s relay patch FAILED", i, kDirectSites[i].name);
                }
            } else {
                LogMsg("[HOOK] SpatialGrid::Query[%zu] %s skipped (Fail-Closed: cs=%p prol=%d vis=%p)",
                       i, kDirectSites[i].name, cs, sqProlOk, vis);
            }
        }
        LogMsg("[SPATIAL] Direct Zero-Copy SpatialGrid sites active: %d/%zu (+1 CachedTag = %d/5 total)",
               sqDirectOk, kNumDirectSqSites, sqDirectOk + (g_origSpatialGridQuery ? 1 : 0));
    }

    // 5C. O(N log N) Flat-Hash + Binary-Heap Pathfinder AStar Engine (paired rollback on partial failure!)
    if (g_cfg.enable_fast_astar) {
        uint8_t* pReset = FIND_AOB(kAobAStarReset);
        uint8_t* pInit  = FIND_AOB(kAobAStarInit);
        uint8_t* pStep  = FIND_AOB(kAobAStarStep);
        if (pReset && pInit && pStep) {
            uint8_t* initReset = ResolveRel32Call(pInit + 21);
            uint8_t* stepReset = ResolveRel32Call(pStep + 0x479);
            uint8_t* pReserve  = ResolveRel32Call(pStep + 0x068);
            uint8_t* pNbType2  = ResolveRel32Call(pStep + 0x1C9);
            uint8_t* pNbNorm   = ResolveRel32Call(pStep + 0x1D9);
            uint8_t* pHeur1    = ResolveRel32Call(pStep + 0x32F);
            uint8_t* pHeur2    = ResolveRel32Call(pStep + 0x3B6);
            uint8_t* pPushPath = ResolveRel32Call(pStep + 0x469);
            uint8_t* pFreeMem  = ResolveRel32Call(pStep + 0x4E5);

            const bool xcheck = (initReset == pReset) && (stepReset == pReset) &&
                                (pHeur1 != nullptr && pHeur1 == pHeur2) &&
                                IsInTextSection(pReserve) && IsInTextSection(pNbType2) &&
                                IsInTextSection(pNbNorm)  && IsInTextSection(pHeur1) &&
                                IsInTextSection(pPushPath) && IsInTextSection(pFreeMem);
            if (xcheck) {
                g_astarCb.reserveNeighbors   = reinterpret_cast<decltype(g_astarCb.reserveNeighbors)>(pReserve);
                g_astarCb.getNeighborsNormal = reinterpret_cast<decltype(g_astarCb.getNeighborsNormal)>(pNbNorm);
                g_astarCb.getNeighborsType2  = reinterpret_cast<decltype(g_astarCb.getNeighborsType2)>(pNbType2);
                g_astarCb.heuristic          = reinterpret_cast<decltype(g_astarCb.heuristic)>(pHeur1);
                g_astarCb.clearPath          = &ClearKleiPathVec;
                g_astarCb.pushPathNode       = reinterpret_cast<decltype(g_astarCb.pushPathNode)>(pPushPath);
                g_astarCb.freeMem            = reinterpret_cast<decltype(g_astarCb.freeMem)>(pFreeMem);

                uint8_t savedR[15] = {}, savedI[15] = {};
                bool okR = InstallHookOrdered(pReset, reinterpret_cast<void*>(&HookAStarReset), 15,
                                              reinterpret_cast<void**>(&g_origAStarReset), nullptr, 0, savedR);
                bool okI = okR && InstallHookOrdered(pInit, reinterpret_cast<void*>(&HookAStarInit), 15,
                                                     reinterpret_cast<void**>(&g_origAStarInit), nullptr, 0, savedI);
                bool okS = okI && InstallHookOrdered(pStep, reinterpret_cast<void*>(&HookAStarStep), 14,
                                                     reinterpret_cast<void**>(&g_origAStarStep));
                if (okR && okI && okS) {
                    g_astarReady = true;
                    LogMsg("[HOOK] Pathfinder FastAStar O(N log N) @ (Reset=%p Init=%p Step=%p Nb=%p/%p Heur=%p) -> OK",
                           pReset, pInit, pStep, pNbNorm, pNbType2, pHeur1);
                } else {
                    const bool restI = !okI || RestoreHookBytes(pInit, savedI, 15);
                    const bool restR = !okR || RestoreHookBytes(pReset, savedR, 15);
                    // Only clear g_orig* trampolines whose patched prologues were verified restored;
                    // if restore failed, keep g_orig* intact with g_astarReady=false so hooks safely pass through!
                    if (restR) g_origAStarReset = nullptr;
                    if (restI) g_origAStarInit  = nullptr;
                    if (!okS)  g_origAStarStep  = nullptr;
                    LogMsg("[HOOK] Pathfinder FastAStar partial install failed (okR=%d okI=%d okS=%d, restR=%d restI=%d) -> %s",
                           okR, okI, okS, restR, restI, (restR && restI) ? "rolled back cleanly" : "PASS-THROUGH RETAINED");
                }
            } else {
                LogMsg("[HOOK] Pathfinder FastAStar skipped (Fail-Closed cross-check failed: ir=%p sr=%p h1=%p h2=%p)",
                       initReset, stepReset, pHeur1, pHeur2);
            }
        }
    }

    // 6. Profiler hooks for Sim::FindEntities, Sim::FindEntities_Registered, SerializeWorldSession
    if (uint8_t* pFE = FIND_AOB(kAobSimFindEnts)) {
        bool ok = InstallHookOrdered(pFE, reinterpret_cast<void*>(&HookSimFindEntities), 15,
                                     reinterpret_cast<void**>(&g_origSimFindEnts));
        LogMsg("[HOOK] Sim::FindEntities @ %p -> %s", pFE, ok ? "OK" : "FAIL");
    }
    if (uint8_t* pFEReg = FIND_AOB(kAobSimFindEntsReg)) {
        bool ok = InstallHookOrdered(pFEReg, reinterpret_cast<void*>(&HookSimFindEntitiesReg), 18,
                                     reinterpret_cast<void**>(&g_origSimFindEntsReg));
        LogMsg("[HOOK] Sim::FindEntities_Registered @ %p -> %s", pFEReg, ok ? "OK" : "FAIL");
    }
    if (uint8_t* pSer = FIND_AOB(kAobSerializeWorld)) {
        bool ok = InstallHookOrdered(pSer, reinterpret_cast<void*>(&HookSerializeWorldSession), 16,
                                     reinterpret_cast<void**>(&g_origSerializeWorld));
        LogMsg("[HOOK] SerializeWorldSession @ %p -> %s", pSer, ok ? "OK" : "FAIL");
    }

    // 7. Hook zlib compress2 & zlib::uncompress -> SIMD libdeflate
    if (uint8_t* pComp2 = FIND_AOB(kAobCompress2)) {
        bool ok = InstallHookOrdered(pComp2, reinterpret_cast<void*>(&HookCompress2), 17,
                                     reinterpret_cast<void**>(&g_origCompress2));
        LogMsg("[HOOK] zlib::compress2 @ %p -> %s", pComp2, ok ? "OK" : "FAIL");
    }
    if (g_cfg.enable_libdeflate) {
        if (uint8_t* pUncomp = FIND_AOB(kAobUncompress)) {
            bool ok = InstallHookOrdered(pUncomp, reinterpret_cast<void*>(&HookUncompress), 14,
                                         reinterpret_cast<void**>(&g_origUncompress));
            LogMsg("[FASTLOAD] zlib::uncompress @ %p -> libdeflate -> %s", pUncomp, ok ? "OK" : "FAIL");
        }
    }

    // 8. Hook Server FrameLimiter::Wait -> 120Hz Waitable Timer in 60Hz SimTick mode (eliminates 0/2 beat aliasing)
    if (uint8_t* pFlWait = FIND_AOB(kAobServerFrameLimiterWait)) {
        bool ok = InstallHookOrdered(pFlWait, reinterpret_cast<void*>(&HookServerFrameLimiterWait), 15,
                                     reinterpret_cast<void**>(&g_origServerFrameLimiterWait));
        LogMsg("[HOOK] Server FrameLimiter::Wait @ %p -> %s", pFlWait, ok ? "OK" : "FAIL");
    }

    // 9. Resolve exclusive Simulation fixed-timestep + secondary 1/30 + NetworkTickRate pointers (Fail-Closed verify)
    auto resolveCheckedFloat = [](uint8_t* site, size_t dispOff, size_t nextIpOff, float expected, float tol) -> float* {
        if (!site) return nullptr;
        int32_t disp = 0;
        std::memcpy(&disp, site + dispOff, 4);
        auto* p = reinterpret_cast<float*>(site + nextIpOff + disp);
        return (std::fabs(*p - expected) < tol) ? p : nullptr;
    };
    g_simAnimShaderTickPtr = resolveCheckedFloat(FIND_AOB(kAobAnimShaderTick),  52, 56, 1.0f / 30.0f, 1e-5f);
    g_simPathfinderTickPtr = resolveCheckedFloat(FIND_AOB(kAobPathfinderTick),  30, 34, 1.0f / 30.0f, 1e-5f);
    g_simSpatialTickPtr    = resolveCheckedFloat(FIND_AOB(kAobSpatialTick),     30, 34, 1.0f / 30.0f, 1e-5f);
    g_simTimeDiffTickPtr   = resolveCheckedFloat(FIND_AOB(kAobSimTimeDiffTick), 19, 23, 1.0f / 30.0f, 1e-5f);
    g_netTickScalePtr      = resolveCheckedFloat(FIND_AOB(kAobNetTickScale),     4,  8, 30.5f,        0.01f);
    g_fnSetNetTickRate     = reinterpret_cast<SetNetworkTickRate_t>(FIND_AOB(kAobSetNetTickRate));
    if (uint8_t* pNetRef = FIND_AOB(kAobSimUpdateNetMgr)) {
        int32_t disp = 0;
        std::memcpy(&disp, pNetRef + 3, 4);
        g_netMgrGlobalPtr = reinterpret_cast<void**>(pNetRef + 7 + disp);
    }

    if (uint8_t* pTickLoad = FIND_AOB(kAobSimUpdateTick)) {
        int32_t dispF = 0, dispD = 0;
        std::memcpy(&dispF, pTickLoad + 12, 4);
        std::memcpy(&dispD, pTickLoad + 99, 4);
        auto* pf = reinterpret_cast<float*>(pTickLoad + 16 + dispF);
        auto* pd = reinterpret_cast<double*>(pTickLoad + 103 + dispD);
        if (std::fabs(*pf - (1.0f / 30.0f)) < 1e-5f && std::fabs(*pd - (1.0 / 30.0)) < 1e-5) {
            g_simTickFloatPtr = pf;
            g_simTickDoublePtr = pd;
            LogMsg("[SIMTICK] Verified exclusive Server tick constants @ f=%p d=%p sub=(%d%d%d%d) netScale=%p netMgr=%p setNet=%p",
                   static_cast<void*>(pf), static_cast<void*>(pd),
                   g_simAnimShaderTickPtr != nullptr, g_simPathfinderTickPtr != nullptr,
                   g_simSpatialTickPtr != nullptr, g_simTimeDiffTickPtr != nullptr,
                   static_cast<void*>(g_netTickScalePtr), static_cast<void*>(g_netMgrGlobalPtr),
                   reinterpret_cast<void*>(g_fnSetNetTickRate));
            if (g_cfg.sim_tick_rate == 60) {
                ApplySimTickRate(60);
            }
        } else {
            LogMsg("[SIMTICK] Skipped (Fail-Closed value check: f=%.6f d=%.6f)", *pf, *pd);
        }
    }

    // 10. RakNet Event-Driven Zero-Stall Wakeup (eliminates 0-10ms Server snapshot WaitOnEvent stall, 0% idle CPU)
    if (g_cfg.enable_raknet_instant_wake) {
        uint8_t* pWake1 = FIND_AOB(kAobRakNetSendBufWake);
        uint8_t* pWake2 = FIND_AOB(kAobRakNetSendBufListWake);
        uint8_t* setEv1 = pWake1 ? ResolveRel32Call(pWake1 + 12) : nullptr;
        uint8_t* setEv2 = pWake2 ? ResolveRel32Call(pWake2 + 11) : nullptr;
        if (pWake1 && pWake2 && setEv1 && setEv1 == setEv2) {
            constexpr uint8_t kNop2[2]  = { 0x90, 0x90 };
            constexpr uint8_t kOrig2[2] = { 0x75, 0x0C };
            bool ok1 = RestoreHookBytes(pWake1 + 3, kNop2, 2);
            bool ok2 = false;
            bool rolledBack = true;
            if (ok1) {
                ok2 = RestoreHookBytes(pWake2 + 2, kNop2, 2);
                if (!ok2) {
                    // Paired rollback: never leave partial RakNet patch installed
                    rolledBack = RestoreHookBytes(pWake1 + 3, kOrig2, 2);
                }
            }
            LogMsg("[NET] RakNet Zero-Stall Event Wakeup @ (%p, %p, SetEvent=%p) -> %s",
                   pWake1 + 3, pWake2 + 2, setEv1,
                   (ok1 && ok2) ? "OK (0ms snapshot dispatch)"
                                : (rolledBack ? "FAIL (rolled back)" : "FAIL (CRITICAL: rollback failed!)"));
        } else {
            LogMsg("[NET] RakNet Zero-Stall skipped (Fail-Closed cross-check: p1=%p p2=%p ev1=%p ev2=%p)",
                   pWake1, pWake2, setEv1, setEv2);
        }
    }

    // 11. Server FastLoad Engine (libdeflate v1.23 unzReadCurrentFile + 64-bit inflate_fast + HW rep stosb VecFill)
    //     Uses the shared W^X near-module trampoline slab locked to PAGE_EXECUTE_READ right below!
    if (g_cfg.enable_fast_load) {
        if (uint8_t* pUnz = FIND_AOB(kAobUnzReadCurrentFile)) {
            g_trampUsed = (g_trampUsed + 15u) & ~static_cast<size_t>(15u);
            if (g_trampSlab && g_trampUsed + 32u <= kTrampSlabSize) {
                uint8_t* tramp = g_trampSlab + g_trampUsed;
                g_trampUsed += 32u;
                constexpr uint8_t kUnzPrefix[8] = { 0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xF6 };
                std::memcpy(tramp, kUnzPrefix, 8);
                WriteAbsJmp14(tramp + 8, pUnz + 0x16);
                FlushInstructionCache(GetCurrentProcess(), tramp, 22);
                std::atomic_thread_fence(std::memory_order_release);
                g_origUnzRead = reinterpret_cast<UnzReadCurrentFile_t>(tramp);
                uint8_t jmp[14];
                WriteAbsJmp14(jmp, reinterpret_cast<const void*>(&HookUnzReadCurrentFile));
                bool ok = RestoreHookBytes(pUnz, jmp, 14);
                LogMsg("[FASTLOAD] minizip unzReadCurrentFile @ %p -> libdeflate v1.23 (W^X slab) -> %s",
                       pUnz, ok ? "OK" : "FAIL");
            }
        }
        if (uint8_t* pInf = FIND_AOB(kAobInflateFast)) {
            uint8_t jmp[14];
            WriteAbsJmp14(jmp, reinterpret_cast<const void*>(&HookInflateFast));
            bool ok = RestoreHookBytes(pInf, jmp, 14);
            LogMsg("[FASTLOAD] zlib 1.2.3 inflate_fast @ %p -> 64-bit HookInflateFast -> %s",
                   pInf, ok ? "OK" : "FAIL");
        }
        if (uint8_t* pA = FIND_AOB(kAobVecFillA)) {
            constexpr size_t kLenA = sizeof(kAobVecFillA) / sizeof(kAobVecFillA[0]);
            uint8_t patchA[kLenA];
            std::memset(patchA, 0x90, kLenA);
            constexpr uint8_t codeA[] = { 0x57, 0x48, 0x89, 0xF7, 0x48, 0x89, 0xD9, 0x41, 0x0F, 0xB6, 0x45, 0x00, 0xF3, 0xAA, 0x5F };
            std::memcpy(patchA, codeA, sizeof(codeA));
            bool ok = RestoreHookBytes(pA, patchA, kLenA);
            LogMsg("[FASTLOAD] std::vector::_Insert_n Loop A @ %p -> HW rep stosb (%zuB) -> %s", pA, kLenA, ok ? "OK" : "FAIL");
        }
        if (uint8_t* pB = FIND_AOB(kAobVecFillB)) {
            constexpr size_t kLenB = sizeof(kAobVecFillB) / sizeof(kAobVecFillB[0]);
            uint8_t patchB[kLenB];
            std::memset(patchB, 0x90, kLenB);
            constexpr uint8_t codeB[] = { 0x57, 0x48, 0x89, 0xCF, 0x48, 0x89, 0xC1, 0x44, 0x89, 0xE0, 0xF3, 0xAA, 0x5F };
            std::memcpy(patchB, codeB, sizeof(codeB));
            bool ok = RestoreHookBytes(pB, patchB, kLenB);
            LogMsg("[FASTLOAD] std::vector::_Insert_n Loop B @ %p -> HW rep stosb (%zuB) -> %s", pB, kLenB, ok ? "OK" : "FAIL");
        }
    }

    // 12. Unified FS Negative Stat Cache & Verified Atomic Save-Commit (fail-closed)
    const bool asyncBarriersOk = InstallUnifiedFsAndAsyncSaveIatHooks();
    if (g_cfg.enable_async_save && asyncBarriersOk) {
        if (uint8_t* pWriteFile = FIND_AOB(kAobWritePersistentFile)) {
            bool ok = InstallHookOrdered(pWriteFile, reinterpret_cast<void*>(&HookWritePersistentFile), 17,
                                         reinterpret_cast<void**>(&g_origWritePersistentFile));
            g_asyncSaveActive.store(ok, std::memory_order_release);
            LogMsg("[HOOK] WritePersistentFile VerifiedAtomicCommit @ %p -> %s", pWriteFile, ok ? "OK" : "FAIL");
        }
    }

    // 13. Causal 3-Channel RakNet::RPC4 QoS Classifier
    if (g_cfg.enable_rpc_qos) {
        if (uint8_t* pRpc = FIND_AOB(kAobRpc4Call)) {
            bool ok = InstallHookOrdered(pRpc, reinterpret_cast<void*>(&HookRpc4Call), 14,
                                         reinterpret_cast<void**>(&g_origRpc4Call));
            g_rpcQosActive.store(ok, std::memory_order_release);
            LogMsg("[HOOK] RakNet::RPC4::Call CausalQoS @ %p -> %s", pRpc, ok ? "OK" : "FAIL");
        }
    }

    // 13. Verified C++ Tick Sub-Phase Profiler Hooks
    if (uint8_t* pLuaUpd = FIND_AOB(kAobSimLuaUpdate)) {
        bool ok = InstallHookOrdered(pLuaUpd, reinterpret_cast<void*>(&HookSimLuaUpdate), 24,
                                     reinterpret_cast<void**>(&g_origSimLuaUpdate));
        LogMsg("[HOOK] Simulation::luaUpdate @ %p -> %s", pLuaUpd, ok ? "OK" : "FAIL");
    }
    if (uint8_t* pNetUpd = FIND_AOB(kAobNetMgrUpdate)) {
        bool ok = InstallHookOrdered(pNetUpd, reinterpret_cast<void*>(&HookNetMgrUpdate), 21,
                                     reinterpret_cast<void**>(&g_origNetMgrUpdate));
        LogMsg("[HOOK] cNetworkManager::Update @ %p -> %s", pNetUpd, ok ? "OK" : "FAIL");
    }
    if (uint8_t* pRepUpd = FIND_AOB(kAobReplicaMgr3Update)) {
        const RelocItem rel = {9, 7, 3}; // mov rax, [rip + disp32] (__security_cookie) at +9
        bool ok = InstallHookOrdered(pRepUpd, reinterpret_cast<void*>(&HookReplicaMgr3Update), 16,
                                     reinterpret_cast<void**>(&g_origReplicaMgr3Update), &rel, 1);
        LogMsg("[HOOK] ReplicaManager3::Update @ %p -> %s", pRepUpd, ok ? "OK" : "FAIL");

        // 14. O(N) QueryReplicaList + Zero-Scan PreSerialize Fast-Path Engine
        if (ok && g_cfg.enable_replica_fastpath) {
            auto findUniqueRdataPtr = [](const void* target) -> const uint8_t* {
                HMODULE hExe = GetModuleHandleW(nullptr);
                if (!hExe || !target) return nullptr;
                auto* base = reinterpret_cast<const uint8_t*>(hExe);
                auto* dos  = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
                if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
                auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
                if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;
                const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
                const uint8_t* hit = nullptr;
                for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
                    if (std::memcmp(sec[i].Name, ".rdata", 6) == 0) {
                        const uint8_t* rBeg = base + sec[i].VirtualAddress;
                        const size_t   rLen = sec[i].Misc.VirtualSize;
                        for (size_t off = 0; off + sizeof(void*) <= rLen; off += sizeof(void*)) {
                            const void* val = nullptr;
                            std::memcpy(&val, rBeg + off, sizeof(void*));
                            if (val == target) {
                                if (hit) return nullptr; // fail-closed if duplicate
                                hit = rBeg + off;
                            }
                        }
                    }
                }
                return hit;
            };
            auto decodeRel32Call = [](const uint8_t* site) -> uint8_t* {
                if (!site || site[0] != 0xE8) return nullptr;
                int32_t disp = 0;
                std::memcpy(&disp, site + 1, 4);
                return const_cast<uint8_t*>(site + 5 + static_cast<int64_t>(disp));
            };

            uint8_t* pQrl = FIND_AOB(kAobQueryReplicaList);
            uint8_t* pRel = FIND_AOB(kAobNetworkProxyIsRelevant);
            uint8_t* pQd  = FIND_AOB(kAobNetworkProxyQueryDestr);
            bool cfgOk = false;
            if (pQrl && pRel && pQd &&
                pQrl[0x3b] == 0x48 && pQrl[0x3c] == 0x8B && pQrl[0x3d] == 0x0D &&
                pQrl[0x52] == 0xE8 && pQrl[0x2d6] == 0xE8 && pQrl[0x30d] == 0xE8 &&
                pRel[0xed] == 0xE8) {
                int32_t dispNet = 0;
                std::memcpy(&dispNet, pQrl + 0x3e, 4);
                auto** ppNetMgr = reinterpret_cast<void**>(pQrl + 0x3b + 7 + static_cast<int64_t>(dispNet));
                uint8_t* pGetCli   = decodeRel32Call(pQrl + 0x52);
                uint8_t* pHostNew  = decodeRel32Call(pQrl + 0x2d6);
                uint8_t* pHostDel  = decodeRel32Call(pQrl + 0x30d);
                uint8_t* pGetInvFn = decodeRel32Call(pRel + 0xed);
                uint8_t* pInvIdFn  = (pGetInvFn && pGetInvFn[0x19] == 0xE8) ? decodeRel32Call(pGetInvFn + 0x19) : nullptr;
                const uint8_t* qdSlot    = findUniqueRdataPtr(pQd);
                const uint8_t* invIdSlot = (pInvIdFn && pInvIdFn[0] == 0xB8 &&
                                            pInvIdFn[1] == 0x1E && pInvIdFn[2] == 0x28 &&
                                            pInvIdFn[3] == 0x8E && pInvIdFn[4] == 0x3F)
                                           ? findUniqueRdataPtr(pInvIdFn) : nullptr;
                if (ppNetMgr && pGetCli && pHostNew && pHostDel && qdSlot && invIdSlot) {
                    hc_replica::ReplicaAbiConfig cfg{};
                    cfg.ppNetMgr          = ppNetMgr;
                    cfg.fnGetClientByGuid = reinterpret_cast<hc_replica::GetClientByGuidFn_t>(pGetCli);
                    cfg.fnIsRelevant      = reinterpret_cast<hc_replica::IsRelevantFn_t>(pRel);
                    cfg.fnHostNew         = reinterpret_cast<hc_replica::HostNewFn_t>(pHostNew);
                    cfg.fnHostDelete      = reinterpret_cast<hc_replica::HostDeleteFn_t>(pHostDel);
                    cfg.networkProxyVtbl  = qdSlot - 0x48;
                    cfg.invItemCompVtbl   = invIdSlot - 0x30;
                    // Windows x64 default offsets already match verified Build 762172 PE.
                    // Fail-closed proximity offset verification:
                    //   1) kAobClientSetPlayerProxy: mov [rcx+0x1b8], rdx; mov rax, [ppNetMgr]; mov rcx, [rdx+0x18]
                    //   2) kAobEntityVisitor: cmp [rdx+0x20c], sil; subss xmm1, [rdx+0x200]; subss xmm0, [rdx+0x208]
                    uint8_t* pSetPly = FIND_AOB(kAobClientSetPlayerProxy);
                    uint8_t* pEntVis = FIND_AOB(kAobEntityVisitor);
                    if (pSetPly && pEntVis) {
                        int32_t dispNetPly = 0;
                        std::memcpy(&dispNetPly, pSetPly + 0x10, 4);
                        auto** ppNetPly = reinterpret_cast<void**>(pSetPly + 0x0d + 7 + static_cast<int64_t>(dispNetPly));
                        const bool plyOk = (ppNetPly == ppNetMgr) &&
                            pSetPly[0x06] == 0x48 && pSetPly[0x07] == 0x89 && pSetPly[0x08] == 0x91 &&
                            pSetPly[0x09] == 0xB8 && pSetPly[0x0a] == 0x01 && pSetPly[0x0b] == 0x00 && pSetPly[0x0c] == 0x00 &&
                            pSetPly[0x2e] == 0x48 && pSetPly[0x2f] == 0x8B && pSetPly[0x30] == 0x4A && pSetPly[0x31] == 0x18;
                        const bool posOk =
                            pEntVis[0x20] == 0x40 && pEntVis[0x21] == 0x38 && pEntVis[0x22] == 0xB2 &&
                            pEntVis[0x23] == 0x0C && pEntVis[0x24] == 0x02 && pEntVis[0x25] == 0x00 && pEntVis[0x26] == 0x00 &&
                            pEntVis[0x47] == 0xF3 && pEntVis[0x48] == 0x0F && pEntVis[0x49] == 0x5C && pEntVis[0x4a] == 0x8A &&
                            pEntVis[0x4b] == 0x00 && pEntVis[0x4c] == 0x02 && pEntVis[0x4d] == 0x00 && pEntVis[0x4e] == 0x00 &&
                            pEntVis[0x4f] == 0xF3 && pEntVis[0x50] == 0x0F && pEntVis[0x51] == 0x5C && pEntVis[0x52] == 0x82 &&
                            pEntVis[0x53] == 0x08 && pEntVis[0x54] == 0x02 && pEntVis[0x55] == 0x00 && pEntVis[0x56] == 0x00;
                        if (plyOk && posOk) {
                            cfg.proximityValid       = true;
                            cfg.offClientPlayerProxy = 0x1b8;
                            cfg.offEntPosX           = 0x200;
                            cfg.offEntPosZ           = 0x208;
                        }
                    }
                    g_replicaCfg = cfg;
                    cfgOk = g_replicaCfg.IsValid();
                }
            }
            if (cfgOk && pQrl) {
                bool okQrl = InstallHookOrdered(pQrl, reinterpret_cast<void*>(&HookQueryReplicaList), 20,
                                                reinterpret_cast<void**>(&g_origQueryReplicaList));
                g_replicaFastActive.store(okQrl, std::memory_order_release);
                LogMsg("[HOOK] cNetworkConnection::QueryReplicaList FastPath @ %p -> %s (proxyVtbl=%p invVtbl=%p prox=%d paceBudget=%u)",
                       pQrl, okQrl ? "OK" : "FAIL", g_replicaCfg.networkProxyVtbl, g_replicaCfg.invItemCompVtbl,
                       g_replicaCfg.proximityValid ? 1 : 0, g_effectiveReplicaBudget.load(std::memory_order_relaxed));

                // Patch PreSerialize 17k-entity full-world scan (`cmp [r14+18h], r13d; jbe +24h` -> `jmp +24h`)
                // Safe because HookReplicaMgr3Update clears +0x178 on active serializedReplicaList after every snapshot.
                if (okQrl &&
                    pRepUpd[0x1c3] == 0x45 && pRepUpd[0x1c4] == 0x39 &&
                    pRepUpd[0x1c5] == 0x6E && pRepUpd[0x1c6] == 0x18 &&
                    pRepUpd[0x1c7] == 0x76 && pRepUpd[0x1c8] == 0x24) {
                    const uint8_t jmpByte = 0xEB;
                    if (RestoreHookBytes(pRepUpd + 0x1c7, &jmpByte, 1)) {
                        g_replicaPreSerPatched.store(true, std::memory_order_release);
                        LogMsg("[PERF][REPLICA] Patched PreSerialize full-world scan @ %p -> Zero-Scan active",
                               pRepUpd + 0x1c7);
                    }
                }
            } else {
                LogMsg("[WARN][REPLICA] FastPath symbol verification incomplete (pQrl=%p pRel=%p pQd=%p) -> vanilla passthrough",
                       pQrl, pRel, pQd);
            }
        }
    }

    // Lock trampoline slab to PAGE_EXECUTE_READ (100% W^X compliant, zero permanent RWX pages!)
    if (!FinalizeTrampolineSlabWX()) {
        LogMsg("[WARN][HOOK] Continuing with non-RX trampoline slab (W^X lock failed)");
    }
}

// =============================================================================
// Outside-Loader-Lock Entry-Point Deferred Initialization
// =============================================================================
// Avoids ALL file I/O, INI parsing, CRT stdio, and .text scanning inside DllMain!
// Patches only the 5-byte tail `jmp __scrt_common_main_seh` at mainCRTStartup+0x0d
// to a W^X RX near-module stub that executes InitServerHooks() on the single main
// thread AFTER Windows releases the Loader Lock and BEFORE main()/lua_newstate()!
uint8_t* g_epJmpSite = nullptr;
uint8_t  g_epSaved5[5] = {};
std::atomic<bool> g_epInitRan{false};

extern "C" void DeferredEntryPointInit() {
    if (g_epInitRan.exchange(true, std::memory_order_acq_rel)) return;
    if (g_epJmpSite) {
        DWORD oldProt = 0, dummy = 0;
        if (VirtualProtect(g_epJmpSite, 5, PAGE_EXECUTE_READWRITE, &oldProt)) {
            std::memcpy(g_epJmpSite, g_epSaved5, 5);
            VirtualProtect(g_epJmpSite, 5, oldProt, &dummy);
            FlushInstructionCache(GetCurrentProcess(), g_epJmpSite, 5);
        }
    }
    g_initOutsideLoaderLock = true;
    InitServerHooks();
}

bool TryInstallDeferredEntryPointInit() {
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    if (!base) return false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const uint32_t epRva = nt->OptionalHeader.AddressOfEntryPoint;
    if (epRva == 0) return false;

    uint8_t* ep = base + epRva;
    // Strict fail-closed verification of MSVC x64 mainCRTStartup:
    //   +0x00: 48 83 EC 28        sub rsp, 0x28
    //   +0x04: E8 xx xx xx xx     call __security_init_cookie
    //   +0x09: 48 83 C4 28        add rsp, 0x28
    //   +0x0d: E9 yy yy yy yy     jmp __scrt_common_main_seh
    if (!(ep[0] == 0x48 && ep[1] == 0x83 && ep[2] == 0xEC && ep[3] == 0x28 &&
          ep[4] == 0xE8 &&
          ep[9] == 0x48 && ep[10] == 0x83 && ep[11] == 0xC4 && ep[12] == 0x28 &&
          ep[13] == 0xE9)) {
        return false;
    }

    uint8_t* jmpSite = ep + 13;
    int32_t origDisp = 0;
    std::memcpy(&origDisp, jmpSite + 1, 4);
    uint8_t* origTarget = jmpSite + 5 + origDisp;

    // Allocate a single near-module page within +-1.5GB of jmpSite (pure VirtualAlloc, safe in DllMain!)
    uintptr_t anchor = reinterpret_cast<uintptr_t>(jmpSite) & ~uintptr_t(0xFFFF);
    uint8_t* stub = nullptr;
    for (int dir = 0; dir < 2 && !stub; ++dir) {
        for (uintptr_t off = 0x100000; off < 0x58000000ULL; off += 0x10000) {
            uintptr_t cand = (dir == 0) ? (anchor + off) : (anchor > off ? anchor - off : 0);
            if (cand < 0x10000000ULL) continue;
            intptr_t relCheck = static_cast<intptr_t>(cand) - reinterpret_cast<intptr_t>(jmpSite + 5);
            if (relCheck < INT32_MIN || relCheck > INT32_MAX) continue;
            void* p = VirtualAlloc(reinterpret_cast<void*>(cand), 4096,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (p) {
                stub = static_cast<uint8_t*>(p);
                break;
            }
        }
    }
    if (!stub) return false;

    const intptr_t newDisp64 = reinterpret_cast<intptr_t>(stub) - reinterpret_cast<intptr_t>(jmpSite + 5);
    if (newDisp64 < INT32_MIN || newDisp64 > INT32_MAX) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }

    // Build 34-byte Win64 ABI-compliant stub:
    //   sub rsp, 0x28 ; movabs rax, &DeferredEntryPointInit ; call rax ; add rsp, 0x28 ; jmp [rip+0] ; dq origTarget
    uint8_t code[34] = {
        0x48, 0x83, 0xEC, 0x28,
        0x48, 0xB8, 0,0,0,0,0,0,0,0,
        0xFF, 0xD0,
        0x48, 0x83, 0xC4, 0x28,
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,
        0,0,0,0,0,0,0,0
    };
    const uint64_t fnAddr = reinterpret_cast<uint64_t>(&DeferredEntryPointInit);
    const uint64_t tgtAddr = reinterpret_cast<uint64_t>(origTarget);
    std::memcpy(code + 6, &fnAddr, 8);
    std::memcpy(code + 26, &tgtAddr, 8);
    std::memcpy(stub, code, sizeof(code));
    FlushInstructionCache(GetCurrentProcess(), stub, sizeof(code));

    DWORD stubOldProt = 0;
    if (!VirtualProtect(stub, 4096, PAGE_EXECUTE_READ, &stubOldProt)) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }

    g_epJmpSite = jmpSite;
    std::memcpy(g_epSaved5, jmpSite, 5);
    std::atomic_thread_fence(std::memory_order_release);

    DWORD epOldProt = 0, dummy = 0;
    if (!VirtualProtect(jmpSite, 5, PAGE_EXECUTE_READWRITE, &epOldProt)) {
        g_epJmpSite = nullptr;
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    const int32_t newDisp = static_cast<int32_t>(newDisp64);
    jmpSite[0] = 0xE9;
    std::memcpy(jmpSite + 1, &newDisp, 4);
    VirtualProtect(jmpSite, 5, epOldProt, &dummy);
    FlushInstructionCache(GetCurrentProcess(), jmpSite, 5);
    return true;
}

bool IsDedicatedServerHost() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const wchar_t* base = std::wcsrchr(exePath, L'\\');
    const wchar_t* base2 = std::wcsrchr(exePath, L'/');
    if (base2 && (!base || base2 > base)) base = base2;
    base = base ? (base + 1) : exePath;
    return _wcsicmp(base, L"dontstarve_dedicated_server_nullrenderer_x64.exe") == 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfMod = hinst;
        DisableThreadLibraryCalls(hinst);
        if (IsDedicatedServerHost()) {
            // Pin module permanently in process address space so runtime FreeLibrary
            // can NEVER unmap hooked trampolines, Lua C closures, or mimalloc allocators.
            HMODULE pinned = nullptr;
            GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&DllMain), &pinned);
            g_isServerProcess = true;
            // Defer ALL file I/O, INI parsing, .text scanning, and hook installation until
            // mainCRTStartup runs OUTSIDE the Windows Loader Lock (before main/lua_newstate)!
            if (!TryInstallDeferredEntryPointInit()) {
                // Fail-safe fallback only if a future compiler alters mainCRTStartup prologue
                InitServerHooks();
            }
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        // Strict Win32 DllMain contract: on process termination (lpReserved != nullptr),
        // other threads have already been forcefully terminated by the kernel;
        // NEVER acquire locks, wait on threads, or call WinMM/WinSock/FreeLibrary here!
        if (lpReserved != nullptr) {
            return TRUE;
        }
        g_stopHttp.store(true);
    }
    return TRUE;
}
