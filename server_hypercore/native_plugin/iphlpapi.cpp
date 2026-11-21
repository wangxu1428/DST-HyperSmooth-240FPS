// =============================================================================
// DST HyperCore Server Native Engine v1.0.0 (IPHLPAPI.DLL x64 Proxy)
// =============================================================================
// First-principles, hermetic-ABI native optimization & profiling engine for
// Don't Starve Together Dedicated Server (dontstarve_dedicated_server_nullrenderer_x64.exe).
//
// Design Axioms:
//   1. Measure First: Built-in microsecond QPC Profiler for every hot subsystem.
//   2. Zero-Divergence: 100% byte-compatible saves & identical gameplay semantics.
//   3. Hermetic ABI: Only cuts self-contained boundaries (lua_newstate alloc,
//      read-only FindEntities SpatialGrid::Query, RFC1950 compress2, 30Hz GC).
//   4. Zero Lock-In: Pure pass-through in client/other processes; safe uninstall.
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "iphlpapi_names.h"
#include "fast_dumper.h"
#include "libdeflate.h"
#include "mimalloc.h"

// =============================================================================
// 297-Export Transparent System32\iphlpapi.dll Proxy (Loader-Lock-Safe Lazy Init)
// =============================================================================
extern "C" {
void* g_iphlp_ptrs[kIphlpExportCount] = {nullptr};

static HMODULE   g_realIphlp = nullptr;
static INIT_ONCE g_iphlpInitOnce = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK LoadRealIphlpCallback(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t sysDir[MAX_PATH] = {};
    UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH - 16) return FALSE;
    std::wstring path(sysDir, len);
    path += L"\\iphlpapi.dll";
    g_realIphlp = LoadLibraryW(path.c_str());
    return g_realIphlp != nullptr ? TRUE : FALSE;
}

void* hc_iphlp_resolve(uint32_t idx) {
    if (idx >= kIphlpExportCount) return nullptr;
    void* cached = InterlockedCompareExchangePointer(&g_iphlp_ptrs[idx], nullptr, nullptr);
    if (cached) return cached;

    InitOnceExecuteOnce(&g_iphlpInitOnce, LoadRealIphlpCallback, nullptr, nullptr);
    if (!g_realIphlp) return nullptr;

    void* fn = reinterpret_cast<void*>(GetProcAddress(g_realIphlp, kIphlpExportNames[idx]));
    if (fn) {
        InterlockedExchangePointer(&g_iphlp_ptrs[idx], fn);
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
};

Config       g_cfg;
HMODULE      g_selfMod = nullptr;
std::wstring g_binDir;
std::wstring g_iniPath;
std::wstring g_logPath;
CRITICAL_SECTION g_logCs;
bool         g_logCsInit = false;
bool         g_isServerProcess = false;
bool         g_timerBoosted = false;
double       g_qpcToUs = 0.0;
HANDLE       g_httpThread = nullptr;
std::atomic<bool> g_stopHttp{false};

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
    std::atomic<int32_t>  gc_last_kb{0};
    std::atomic<int32_t>  gc_last_budget_us{0};

    std::atomic<uint64_t> find_entities_calls{0};
    std::atomic<uint64_t> find_entities_us_sum{0};
    std::atomic<uint64_t> spatial_queries{0};
    std::atomic<uint64_t> spatial_zerocopy_hits{0};
    std::atomic<uint64_t> spatial_cells_visited{0};

    std::atomic<uint64_t> mi_allocs{0};
    std::atomic<uint64_t> mi_reallocs{0};
    std::atomic<uint64_t> mi_frees{0};

    std::atomic<uint64_t> fastdump_calls{0};
    std::atomic<uint64_t> fastdump_us_sum{0};
    std::atomic<uint64_t> fastdump_bytes_sum{0};
    std::atomic<uint64_t> fastdump_fallbacks{0};

    std::atomic<uint64_t> deflate_calls{0};
    std::atomic<uint64_t> deflate_us_sum{0};
    std::atomic<uint64_t> deflate_in_bytes{0};
    std::atomic<uint64_t> deflate_out_bytes{0};

    std::atomic<uint64_t> serialize_world_calls{0};
    std::atomic<uint64_t> serialize_world_us_sum{0};

    // Ring buffer of recent tick durations (us) for P95/P99 calculation
    uint32_t tick_ring[kTickRingSize] = {};
    uint32_t tick_ring_pos = 0;
    uint32_t tick_ring_filled = 0;

    // Live per-tick state for adaptive GC feedback
    double   last_tick_non_gc_ms = 5.0;
    uint64_t cur_tick_gc_us = 0;

    void Reset() {
        ticks_total = 0; tick_us_sum = 0; tick_us_max = 0; tick_overrun_count = 0;
        gc_calls = 0; gc_us_sum = 0; gc_us_max = 0; gc_full_count = 0;
        find_entities_calls = 0; find_entities_us_sum = 0;
        spatial_queries = 0; spatial_zerocopy_hits = 0; spatial_cells_visited = 0;
        mi_allocs = 0; mi_reallocs = 0; mi_frees = 0;
        fastdump_calls = 0; fastdump_us_sum = 0; fastdump_bytes_sum = 0; fastdump_fallbacks = 0;
        deflate_calls = 0; deflate_us_sum = 0; deflate_in_bytes = 0; deflate_out_bytes = 0;
        serialize_world_calls = 0; serialize_world_us_sum = 0;
        tick_ring_pos = 0; tick_ring_filled = 0;
    }

    void ComputePercentiles(double& out_avg, double& out_p95, double& out_p99) const {
        uint32_t n = std::min<uint32_t>(tick_ring_filled, kTickRingSize);
        if (n == 0) { out_avg = out_p95 = out_p99 = 0.0; return; }
        uint32_t tmp[kTickRingSize];
        std::memcpy(tmp, tick_ring, n * sizeof(uint32_t));
        std::sort(tmp, tmp + n);
        uint64_t sum = 0;
        for (uint32_t i = 0; i < n; ++i) sum += tmp[i];
        out_avg = static_cast<double>(sum) / n;
        out_p95 = tmp[std::min<uint32_t>(n - 1, (n * 95) / 100)];
        out_p99 = tmp[std::min<uint32_t>(n - 1, (n * 99) / 100)];
    }
};

Telemetry g_stats;

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

void* InstallHook(uint8_t* target, void* detour, size_t stolenLen,
                  const RelocItem* relocs = nullptr, size_t relocCount = 0) {
    if (!target || !detour || stolenLen < 14 || !g_trampSlab) return nullptr;
    size_t needed = stolenLen + 14 + 16;
    if (g_trampUsed + needed > kTrampSlabSize) return nullptr;

    uint8_t* tramp = g_trampSlab + g_trampUsed;
    // Align next slot to 16 bytes
    g_trampUsed = (g_trampUsed + stolenLen + 14 + 15) & ~size_t(15);

    std::memcpy(tramp, target, stolenLen);

    // Fix up any relocated RIP-relative / E8 call instructions inside stolen bytes
    for (size_t i = 0; i < relocCount; ++i) {
        const RelocItem& r = relocs[i];
        int32_t oldDisp = 0;
        std::memcpy(&oldDisp, target + r.offset + r.dispOff, 4);
        intptr_t absTarget = reinterpret_cast<intptr_t>(target + r.offset + r.insnLen) + oldDisp;
        intptr_t newDisp64 = absTarget - reinterpret_cast<intptr_t>(tramp + r.offset + r.insnLen);
        if (newDisp64 < INT32_MIN || newDisp64 > INT32_MAX) {
            LogMsg("[HOOK] Reloc out of range for target %p!", target);
            return nullptr;
        }
        int32_t newDisp = static_cast<int32_t>(newDisp64);
        std::memcpy(tramp + r.offset + r.dispOff, &newDisp, 4);
    }

    WriteAbsJmp14(tramp + stolenLen, target + stolenLen);

    DWORD oldProt = 0;
    if (!VirtualProtect(target, stolenLen, PAGE_EXECUTE_READWRITE, &oldProt)) {
        return nullptr;
    }
    WriteAbsJmp14(target, detour);
    for (size_t i = 14; i < stolenLen; ++i) target[i] = 0x90;
    VirtualProtect(target, stolenLen, oldProt, &oldProt);
    FlushInstructionCache(GetCurrentProcess(), target, stolenLen);
    FlushInstructionCache(GetCurrentProcess(), tramp, stolenLen + 14);
    return tramp;
}

// =============================================================================
// Resolved Engine Pointers & Lua C API
// =============================================================================
LuaApiTable g_lua;

using SimUpdate_t          = uint8_t (*)(void* sim, float dt, uint8_t flag);
using CollectGarbage_t     = void (*)(void* sim);
using SetGcBudget_t        = void (*)(float sec);
using LuaAlloc_t           = void* (*)(void* ud, void* ptr, size_t osize, size_t nsize);
using LuaNewState_t        = void* (*)(LuaAlloc_t f, void* ud);
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
    // Preserve optional Klei debug allocation tracker if explicitly enabled in Simulation
    if (ud && g_trackLuaMem && reinterpret_cast<const uint8_t*>(ud)[0x119] != 0) {
        g_trackLuaMem(ud, static_cast<uint32_t>(osize), static_cast<uint32_t>(nsize));
    }
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

void HookSpatialGridQuery(void* grid, const float* posXZ, float radius, void* visitor) {
    g_stats.spatial_queries.fetch_add(1, std::memory_order_relaxed);

    // Hermetic safety gate: ONLY take zero-copy path for verified read-only FindEntities!
    if (!g_cfg.enable_spatial_zerocopy || !tl_inReadOnlyFindEntities || !g_entityVisitor || !grid || !posXZ) {
        g_origSpatialGridQuery(grid, posXZ, radius, visitor);
        return;
    }

    const auto* gbytes = reinterpret_cast<const uint8_t*>(grid);
    const auto* cells_begin = *reinterpret_cast<const SpatialCell* const*>(gbytes + 0x08);
    const auto* cells_end   = *reinterpret_cast<const SpatialCell* const*>(gbytes + 0x10);
    const float cell_size   = *reinterpret_cast<const float*>(gbytes + 0x38);

    if (!cells_begin || cells_begin >= cells_end || cell_size <= 0.0f) {
        return;
    }

    g_stats.spatial_zerocopy_hits.fetch_add(1, std::memory_order_relaxed);

    const float x = posXZ[0];
    const float z = posXZ[1];
    const float inv_cs = 1.0f / cell_size;
    const int32_t min_cx = static_cast<int32_t>(std::floor((x - radius) * inv_cs));
    const int32_t max_cx = static_cast<int32_t>(std::floor((x + radius) * inv_cs));
    const int32_t min_cz = static_cast<int32_t>(std::floor((z - radius) * inv_cs));
    const int32_t max_cz = static_cast<int32_t>(std::floor((z + radius) * inv_cs));

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
                    if (!g_entityVisitor(visitor, *p)) {
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

// =============================================================================
// Module 3: 30Hz Adaptive Slack-Based Incremental GC
// =============================================================================
void HookCollectGarbage(void* sim) {
    const int64_t t0 = NowQpc();
    g_stats.gc_calls.fetch_add(1, std::memory_order_relaxed);

    if (!g_cfg.enable_adaptive_gc || !g_lua.gc || !g_setGcBudget || !sim) {
        g_origCollectGarbage(sim);
        const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
        g_stats.gc_us_sum.fetch_add(us, std::memory_order_relaxed);
        g_stats.cur_tick_gc_us += us;
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

    int32_t* pThresholdKb = reinterpret_cast<int32_t*>(simBytes + 0x24C);
    const int32_t curKb = g_lua.gc(L, 3 /*LUA_GCCOUNT*/, 0);
    g_stats.gc_last_kb.store(curKb, std::memory_order_relaxed);

    // Track true post-cycle live working set (updated whenever a GC cycle completes or VM resets)
    static void* s_lastL = nullptr;
    static int32_t s_liveSetKb = 0;
    if (L != s_lastL || s_liveSetKb <= 0 || curKb < s_liveSetKb) {
        s_lastL = L;
        s_liveSetKb = curKb;
    }

    // Ensure emergency threshold is comfortably above live working set so STW full GC
    // only fires on genuine runaway leaks, while incremental GC does the steady work.
    const int32_t emergencyFloor = std::max<int32_t>(
        s_liveSetKb * 2,
        s_liveSetKb + g_cfg.gc_emergency_add_mb * 1024);
    if (*pThresholdKb < emergencyFloor) {
        *pThresholdKb = emergencyFloor;
    } else if (curKb > *pThresholdKb) {
        // Genuine emergency full-GC needed
        g_lua.gc(L, 2 /*LUA_GCCOLLECT*/, 0);
        g_stats.gc_full_count.fetch_add(1, std::memory_order_relaxed);
        const int32_t postKb = g_lua.gc(L, 3 /*LUA_GCCOUNT*/, 0);
        s_liveSetKb = postKb;
        *pThresholdKb = std::max<int32_t>(postKb * 2, postKb + g_cfg.gc_emergency_add_mb * 1024);
        g_stats.gc_last_kb.store(postKb, std::memory_order_relaxed);
    }

    // Pressure-driven 30Hz Adaptive GC:
    // - Never waste CPU re-marking static world tables on calm, low-pressure ticks!
    // - Throttle sharply (0.35ms) on busy combat/boss ticks (>14ms non-GC) to protect p99/overruns.
    // - Ramp smoothly (0.85ms -> 2.5ms) ONLY when live garbage pressure grows (+32MB..+96MB above
    //   post-cycle working set) AND tick slack is available.
    const double nonGcMs = g_stats.last_tick_non_gc_ms;
    const double excessMb = (curKb > s_liveSetKb)
        ? (static_cast<double>(curKb - s_liveSetKb) / 1024.0)
        : 0.0;

    float budgetMs = 0.55f;
    if (nonGcMs >= 14.0) {
        budgetMs = g_cfg.gc_busy_min_ms; // 0.35-0.40ms: protect hot ticks from overruns
    } else if (excessMb >= 96.0) {
        budgetMs = std::min<float>(2.5f, g_cfg.gc_calm_max_ms);
    } else if (excessMb >= 32.0) {
        const double frac = (excessMb - 32.0) / 64.0;
        budgetMs = static_cast<float>(0.85 + frac * 1.15); // 0.85ms .. 2.0ms
    } else {
        budgetMs = 0.55f; // Low garbage pressure: 45% lighter than vanilla's fixed 1.0ms!
    }

    g_stats.gc_last_budget_us.store(static_cast<int32_t>(budgetMs * 1000.0f), std::memory_order_relaxed);
    g_setGcBudget(budgetMs * 0.001f);
    const int cycleDone = g_lua.gc(L, 5 /*LUA_GCSTEP*/, 0);
    if (cycleDone != 0) {
        const int32_t postKb = g_lua.gc(L, 3 /*LUA_GCCOUNT*/, 0);
        if (postKb > 0) {
            s_liveSetKb = postKb;
            g_stats.gc_last_kb.store(postKb, std::memory_order_relaxed);
        }
    }

    const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));
    g_stats.gc_us_sum.fetch_add(us, std::memory_order_relaxed);
    g_stats.cur_tick_gc_us += us;
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
        const int targetLevel = g_cfg.libdeflate_level;
        if (!tl_deflate.comp || tl_deflate.level != targetLevel) {
            if (tl_deflate.comp) libdeflate_free_compressor(tl_deflate.comp);
            tl_deflate.comp = libdeflate_alloc_compressor(targetLevel);
            tl_deflate.level = targetLevel;
        }
        if (tl_deflate.comp) {
            size_t outSz = libdeflate_zlib_compress(tl_deflate.comp, source, sourceLen, dest, *destLen);
            if (outSz > 0 && outSz <= *destLen) {
                *destLen = static_cast<uint32_t>(outSz);
                g_stats.deflate_out_bytes.fetch_add(outSz, std::memory_order_relaxed);
                g_stats.deflate_us_sum.fetch_add(static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc())),
                                                 std::memory_order_relaxed);
                return 0; // Z_OK
            }
        }
    }

    int rc = g_origCompress2(dest, destLen, source, sourceLen, level);
    if (rc == 0 && destLen) {
        g_stats.deflate_out_bytes.fetch_add(*destLen, std::memory_order_relaxed);
    }
    g_stats.deflate_us_sum.fetch_add(static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc())),
                                     std::memory_order_relaxed);
    return rc;
}

// =============================================================================
// Module 4B: C++ Native FastDataDumper + Lua Telemetry Bridge
// =============================================================================
thread_local fast_dumper::GrowBuf tl_dumpBuf;

int Lua_HyperCore_FastDump(void* L) {
    if (!g_cfg.enable_fast_dumper || !g_lua.ReadyForFastDump()) {
        g_lua.pushnil(L);
        g_lua.pushstring(L, "disabled");
        return 2;
    }

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

    auto st = fast_dumper::DumpRoot(L, g_lua, 1, varname, varlen, tl_dumpBuf);
    const uint64_t us = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));

    if (st == fast_dumper::Status::Ok && tl_dumpBuf.data) {
        g_stats.fastdump_calls.fetch_add(1, std::memory_order_relaxed);
        g_stats.fastdump_us_sum.fetch_add(us, std::memory_order_relaxed);
        g_stats.fastdump_bytes_sum.fetch_add(tl_dumpBuf.len, std::memory_order_relaxed);
        g_lua.pushlstring(L, tl_dumpBuf.data, tl_dumpBuf.len);
        return 1;
    }

    g_stats.fastdump_fallbacks.fetch_add(1, std::memory_order_relaxed);
    g_lua.pushnil(L);
    g_lua.pushstring(L, st == fast_dumper::Status::CorruptNumber ? "corrupt_number" : "fallback");
    return 2;
}

int Lua_HyperCore_GetStats(void* L) {
    if (!g_lua.ReadyForRegister()) return 0;
    double avg = 0, p95 = 0, p99 = 0;
    g_stats.ComputePercentiles(avg, p95, p99);

    g_lua.createtable(L, 0, 28);
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
    setNum("gc_last_kb",            static_cast<double>(g_stats.gc_last_kb.load()));
    setNum("gc_last_budget_us",     static_cast<double>(g_stats.gc_last_budget_us.load()));
    setNum("find_entities_calls",   static_cast<double>(g_stats.find_entities_calls.load()));
    setNum("find_entities_us_sum",  static_cast<double>(g_stats.find_entities_us_sum.load()));
    setNum("spatial_queries",       static_cast<double>(g_stats.spatial_queries.load()));
    setNum("spatial_zerocopy_hits", static_cast<double>(g_stats.spatial_zerocopy_hits.load()));
    setNum("spatial_cells_visited", static_cast<double>(g_stats.spatial_cells_visited.load()));
    setNum("mi_allocs",             static_cast<double>(g_stats.mi_allocs.load()));
    setNum("mi_reallocs",           static_cast<double>(g_stats.mi_reallocs.load()));
    setNum("mi_frees",              static_cast<double>(g_stats.mi_frees.load()));
    setNum("fastdump_calls",        static_cast<double>(g_stats.fastdump_calls.load()));
    setNum("fastdump_us_sum",       static_cast<double>(g_stats.fastdump_us_sum.load()));
    setNum("fastdump_bytes_sum",    static_cast<double>(g_stats.fastdump_bytes_sum.load()));
    setNum("fastdump_fallbacks",    static_cast<double>(g_stats.fastdump_fallbacks.load()));
    setNum("deflate_calls",         static_cast<double>(g_stats.deflate_calls.load()));
    setNum("deflate_us_sum",        static_cast<double>(g_stats.deflate_us_sum.load()));
    setNum("deflate_in_bytes",      static_cast<double>(g_stats.deflate_in_bytes.load()));
    setNum("deflate_out_bytes",     static_cast<double>(g_stats.deflate_out_bytes.load()));
    setNum("serialize_world_calls", static_cast<double>(g_stats.serialize_world_calls.load()));
    setNum("serialize_world_us_sum",static_cast<double>(g_stats.serialize_world_us_sum.load()));

    setBool("opt_mimalloc",         g_cfg.enable_mimalloc);
    setBool("opt_spatial_zerocopy", g_cfg.enable_spatial_zerocopy);
    setBool("opt_adaptive_gc",      g_cfg.enable_adaptive_gc);
    setBool("opt_libdeflate",       g_cfg.enable_libdeflate);
    setBool("opt_fast_dumper",      g_cfg.enable_fast_dumper);
    return 1;
}

int Lua_HyperCore_ResetStats(void* /*L*/) {
    g_stats.Reset();
    return 0;
}

int Lua_HyperCore_Version(void* L) {
    g_lua.pushstring(L, "DST-HyperCore-Server/1.0.0");
    return 1;
}

void RegisterHyperCoreLuaGlobals(void* L) {
    if (!L || !g_lua.ReadyForRegister()) return;
    auto regFn = [&](const char* name, int (*fn)(void*)) {
        g_lua.pushcclosure(L, fn, 0);
        g_lua.setfield(L, HC_LUA_GLOBALSINDEX, name);
    };
    regFn("HyperCore_FastDump",   &Lua_HyperCore_FastDump);
    regFn("HyperCore_GetStats",   &Lua_HyperCore_GetStats);
    regFn("HyperCore_ResetStats", &Lua_HyperCore_ResetStats);
    regFn("HyperCore_Version",    &Lua_HyperCore_Version);
    LogMsg("[LUA] Registered HyperCore native APIs into Lua VM %p", L);
}

void HookLuaOpenLibs(void* L) {
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
           "GC(us/t=%.0f max=%llu full=%llu kb=%d) | FindEnts(calls=%llu us/t=%.0f zc=%llu cells=%llu) | "
           "FastDump(calls=%llu ms=%.1f MB=%.2f fb=%llu) | Deflate(calls=%llu ms=%.1f in=%.2fMB out=%.2fMB)",
           tag,
           static_cast<unsigned long long>(ticks),
           avg, p95, p99,
           static_cast<unsigned long long>(g_stats.tick_us_max.load()),
           static_cast<unsigned long long>(g_stats.tick_overrun_count.load()),
           ticks ? static_cast<double>(gcUs) / ticks : 0.0,
           static_cast<unsigned long long>(g_stats.gc_us_max.load()),
           static_cast<unsigned long long>(g_stats.gc_full_count.load()),
           g_stats.gc_last_kb.load(),
           static_cast<unsigned long long>(feCalls),
           ticks ? static_cast<double>(feUs) / ticks : 0.0,
           static_cast<unsigned long long>(g_stats.spatial_zerocopy_hits.load()),
           static_cast<unsigned long long>(g_stats.spatial_cells_visited.load()),
           static_cast<unsigned long long>(g_stats.fastdump_calls.load()),
           g_stats.fastdump_us_sum.load() / 1000.0,
           g_stats.fastdump_bytes_sum.load() / (1024.0 * 1024.0),
           static_cast<unsigned long long>(g_stats.fastdump_fallbacks.load()),
           static_cast<unsigned long long>(g_stats.deflate_calls.load()),
           g_stats.deflate_us_sum.load() / 1000.0,
           g_stats.deflate_in_bytes.load() / (1024.0 * 1024.0),
           g_stats.deflate_out_bytes.load() / (1024.0 * 1024.0));
}

uint8_t HookSimUpdate(void* sim, float dt, uint8_t flag) {
    g_stats.cur_tick_gc_us = 0;
    const int64_t t0 = NowQpc();
    const uint8_t ret = g_origSimUpdate(sim, dt, flag);
    const uint64_t totalUs = static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc()));

    const uint64_t gcUs = g_stats.cur_tick_gc_us;
    const uint64_t nonGcUs = (totalUs > gcUs) ? (totalUs - gcUs) : 0;
    // EMA smoothing (0.65 current + 0.35 prev) for adaptive GC stability
    g_stats.last_tick_non_gc_ms = g_stats.last_tick_non_gc_ms * 0.35 + (static_cast<double>(nonGcUs) * 0.001) * 0.65;

    const uint64_t ticks = g_stats.ticks_total.fetch_add(1, std::memory_order_relaxed) + 1;
    g_stats.tick_us_sum.fetch_add(totalUs, std::memory_order_relaxed);
    if (totalUs > 33333) {
        g_stats.tick_overrun_count.fetch_add(1, std::memory_order_relaxed);
    }
    uint64_t prevMax = g_stats.tick_us_max.load(std::memory_order_relaxed);
    while (totalUs > prevMax && !g_stats.tick_us_max.compare_exchange_weak(prevMax, totalUs)) {}

    g_stats.tick_ring[g_stats.tick_ring_pos] = static_cast<uint32_t>(std::min<uint64_t>(totalUs, UINT32_MAX));
    g_stats.tick_ring_pos = (g_stats.tick_ring_pos + 1) % kTickRingSize;
    if (g_stats.tick_ring_filled < kTickRingSize) g_stats.tick_ring_filled++;

    if (g_cfg.enable_profiler && g_cfg.log_interval_ticks > 0 &&
        (ticks % static_cast<uint64_t>(g_cfg.log_interval_ticks) == 0)) {
        LogTelemetrySummary("PROF");
    }
    return ret;
}

int HookSimFindEntities(void* selfObj, void* L) {
    const int64_t t0 = NowQpc();
    int rc = g_origSimFindEnts(selfObj, L);
    g_stats.find_entities_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.find_entities_us_sum.fetch_add(static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc())),
                                           std::memory_order_relaxed);
    return rc;
}

int HookSimFindEntitiesReg(void* selfObj, void* L) {
    const int64_t t0 = NowQpc();
    int rc = g_origSimFindEntsReg(selfObj, L);
    g_stats.find_entities_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.find_entities_us_sum.fetch_add(static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc())),
                                           std::memory_order_relaxed);
    return rc;
}

int HookSerializeWorldSession(void* selfObj, void* L) {
    const int64_t t0 = NowQpc();
    int rc = g_origSerializeWorld(selfObj, L);
    g_stats.serialize_world_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.serialize_world_us_sum.fetch_add(static_cast<uint64_t>(QpcDeltaUs(t0, NowQpc())),
                                             std::memory_order_relaxed);
    return rc;
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
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 ONLY!
    addr.sin_port = htons(static_cast<u_short>(g_cfg.http_port));

    if (bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(srv, 4) != 0) {
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
        char req[512];
        recv(cli, req, sizeof(req) - 1, 0);

        double avg = 0, p95 = 0, p99 = 0;
        g_stats.ComputePercentiles(avg, p95, p99);

        char body[1024];
        int blen = std::snprintf(body, sizeof(body),
            "{\"version\":\"DST-HyperCore-Server/1.0.0\","
            "\"ticks\":%llu,\"tick_avg_us\":%.1f,\"tick_p95_us\":%.1f,\"tick_p99_us\":%.1f,"
            "\"tick_max_us\":%llu,\"tick_overruns\":%llu,"
            "\"gc_calls\":%llu,\"gc_us_sum\":%llu,\"gc_max_us\":%llu,\"gc_full\":%llu,\"gc_kb\":%d,"
            "\"find_entities_calls\":%llu,\"find_entities_us_sum\":%llu,"
            "\"spatial_queries\":%llu,\"spatial_zerocopy\":%llu,\"spatial_cells\":%llu,"
            "\"fastdump_calls\":%llu,\"fastdump_us_sum\":%llu,\"fastdump_bytes\":%llu,"
            "\"deflate_calls\":%llu,\"deflate_us_sum\":%llu,\"deflate_in\":%llu,\"deflate_out\":%llu}",
            static_cast<unsigned long long>(g_stats.ticks_total.load()), avg, p95, p99,
            static_cast<unsigned long long>(g_stats.tick_us_max.load()),
            static_cast<unsigned long long>(g_stats.tick_overrun_count.load()),
            static_cast<unsigned long long>(g_stats.gc_calls.load()),
            static_cast<unsigned long long>(g_stats.gc_us_sum.load()),
            static_cast<unsigned long long>(g_stats.gc_us_max.load()),
            static_cast<unsigned long long>(g_stats.gc_full_count.load()),
            g_stats.gc_last_kb.load(),
            static_cast<unsigned long long>(g_stats.find_entities_calls.load()),
            static_cast<unsigned long long>(g_stats.find_entities_us_sum.load()),
            static_cast<unsigned long long>(g_stats.spatial_queries.load()),
            static_cast<unsigned long long>(g_stats.spatial_zerocopy_hits.load()),
            static_cast<unsigned long long>(g_stats.spatial_cells_visited.load()),
            static_cast<unsigned long long>(g_stats.fastdump_calls.load()),
            static_cast<unsigned long long>(g_stats.fastdump_us_sum.load()),
            static_cast<unsigned long long>(g_stats.fastdump_bytes_sum.load()),
            static_cast<unsigned long long>(g_stats.deflate_calls.load()),
            static_cast<unsigned long long>(g_stats.deflate_us_sum.load()),
            static_cast<unsigned long long>(g_stats.deflate_in_bytes.load()),
            static_cast<unsigned long long>(g_stats.deflate_out_bytes.load()));

        char hdr[256];
        int hlen = std::snprintf(hdr, sizeof(hdr),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
            blen);
        send(cli, hdr, hlen, 0);
        send(cli, body, blen, 0);
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
constexpr int16_t kAobLuaNext[] = {
    0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, -1, -1, -1, -1, 0x4C, 0x8B,
    0x43, 0x10, 0x48, 0x8B, 0x10, 0x49, 0x83, 0xE8, 0x10, 0x48, 0x8B, 0xCB, 0xE8, -1, -1, -1, -1, 0x85, 0xC0, 0x74
};

#define FIND_AOB(arr) FindUniqueAob(arr, sizeof(arr) / sizeof(arr[0]), #arr)

void InitServerHooks() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    g_qpcToUs = 1000000.0 / static_cast<double>(freq.QuadPart);

    InitializeCriticalSection(&g_logCs);
    g_logCsInit = true;

    wchar_t modPath[MAX_PATH] = {};
    GetModuleFileNameW(g_selfMod, modPath, MAX_PATH);
    std::wstring mp(modPath);
    size_t sl = mp.find_last_of(L"\\/");
    g_binDir  = (sl != std::wstring::npos) ? mp.substr(0, sl + 1) : L"";
    g_iniPath = g_binDir + L"dst_hypercore.ini";
    g_logPath = g_binDir + L"dst_hypercore.log";

    // Truncate old log cleanly on new server boot
    if (FILE* f = _wfopen(g_logPath.c_str(), L"w")) std::fclose(f);

    LoadIniConfig();
    LogMsg("=== DST HyperCore Server Native Engine v1.0.0 Boot ===");
    LogMsg("Config: profiler=%d mimalloc=%d spatial_zc=%d adaptive_gc=%d libdeflate=%d(L%d) fast_dumper=%d http=%d",
           g_cfg.enable_profiler, g_cfg.enable_mimalloc, g_cfg.enable_spatial_zerocopy,
           g_cfg.enable_adaptive_gc, g_cfg.enable_libdeflate, g_cfg.libdeflate_level,
           g_cfg.enable_fast_dumper, g_cfg.enable_http_metrics);

    if (g_cfg.enable_timer_boost) {
        if (timeBeginPeriod(1) == TIMERR_NOERROR) g_timerBoosted = true;
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    }

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
    LogMsg("[LUA] C API resolved: ReadyForRegister=%d ReadyForFastDump=%d",
           g_lua.ReadyForRegister(), g_lua.ReadyForFastDump());

    // 1. Hook lua_newstate -> mimalloc
    g_simLuaAllocAddr = FIND_AOB(kAobSimLuaAlloc);
    g_wgLuaAllocAddr  = FIND_AOB(kAobWgLuaAlloc);
    g_trackLuaMem     = reinterpret_cast<TrackLuaMem_t>(FIND_AOB(kAobTrackLuaMem));
    if (uint8_t* pNewState = FIND_AOB(kAobLuaNewState)) {
        g_origLuaNewState = reinterpret_cast<LuaNewState_t>(
            InstallHook(pNewState, reinterpret_cast<void*>(&HookLuaNewState), 15));
        LogMsg("[HOOK] lua_newstate @ %p -> %s (simAlloc=%p wgAlloc=%p)",
               pNewState, g_origLuaNewState ? "OK" : "FAIL", g_simLuaAllocAddr, g_wgLuaAllocAddr);
    }

    // 2. Hook luaL_openlibs -> auto-register HyperCore_* Lua APIs before any script/mod runs
    if (uint8_t* pOpenLibs = FIND_AOB(kAobLuaOpenLibs)) {
        const RelocItem rel = {10, 7, 3}; // mov rax, [rip + disp32] at +10
        g_origLuaOpenLibs = reinterpret_cast<LuaOpenLibs_t>(
            InstallHook(pOpenLibs, reinterpret_cast<void*>(&HookLuaOpenLibs), 17, &rel, 1));
        LogMsg("[HOOK] luaL_openlibs @ %p -> %s", pOpenLibs, g_origLuaOpenLibs ? "OK" : "FAIL");
    }

    // 3. Hook Simulation::Update (Profiler + Adaptive GC feedback)
    if (uint8_t* pSimUpd = FIND_AOB(kAobSimUpdate)) {
        const RelocItem rel = {11, 5, 1}; // call __chkstk at +11
        g_origSimUpdate = reinterpret_cast<SimUpdate_t>(
            InstallHook(pSimUpd, reinterpret_cast<void*>(&HookSimUpdate), 19, &rel, 1));
        LogMsg("[HOOK] Simulation::Update @ %p -> %s", pSimUpd, g_origSimUpdate ? "OK" : "FAIL");
    }

    // 4. Hook Simulation::CollectGarbage (30Hz Adaptive Slack GC + Profiler)
    if (uint8_t* pGC = FIND_AOB(kAobCollectGarbage)) {
        g_setGcBudget = reinterpret_cast<SetGcBudget_t>(ResolveRel32Call(pGC + 0x8B));
        g_origCollectGarbage = reinterpret_cast<CollectGarbage_t>(
            InstallHook(pGC, reinterpret_cast<void*>(&HookCollectGarbage), 16));
        LogMsg("[HOOK] Simulation::CollectGarbage @ %p (SetGcBudget=%p) -> %s",
               pGC, g_setGcBudget, g_origCollectGarbage ? "OK" : "FAIL");
    }

    // 5. Hook EntityManager::FindEntities + SpatialGrid::Query<FindEntitiesVisitor> (Zero-Copy!)
    if (uint8_t* pEMFind = FIND_AOB(kAobEntityMgrFind)) {
        uint8_t* pSpatialQuery = ResolveRel32Call(pEMFind + 0x10F);
        uint8_t* pVisitor = pSpatialQuery ? ResolveRel32Call(pSpatialQuery + 0x239) : nullptr;
        g_entityVisitor = reinterpret_cast<EntityVisitor_t>(pVisitor);
        g_origEntityMgrFind = reinterpret_cast<EntityMgrFind_t>(
            InstallHook(pEMFind, reinterpret_cast<void*>(&HookEntityMgrFindEntities), 20));
        if (pSpatialQuery && g_entityVisitor) {
            g_origSpatialGridQuery = reinterpret_cast<SpatialGridQuery_t>(
                InstallHook(pSpatialQuery, reinterpret_cast<void*>(&HookSpatialGridQuery), 17));
        }
        LogMsg("[HOOK] SpatialGrid::Query @ %p (Visitor=%p) -> %s",
               pSpatialQuery, pVisitor, (g_origEntityMgrFind && g_origSpatialGridQuery) ? "OK" : "FAIL");
    }

    // 6. Profiler hooks for Sim::FindEntities, Sim::FindEntities_Registered, SerializeWorldSession
    if (uint8_t* pFE = FIND_AOB(kAobSimFindEnts)) {
        g_origSimFindEnts = reinterpret_cast<LuaCFuncObj_t>(
            InstallHook(pFE, reinterpret_cast<void*>(&HookSimFindEntities), 15));
        LogMsg("[HOOK] Sim::FindEntities @ %p -> %s", pFE, g_origSimFindEnts ? "OK" : "FAIL");
    }
    if (uint8_t* pFEReg = FIND_AOB(kAobSimFindEntsReg)) {
        g_origSimFindEntsReg = reinterpret_cast<LuaCFuncObj_t>(
            InstallHook(pFEReg, reinterpret_cast<void*>(&HookSimFindEntitiesReg), 18));
        LogMsg("[HOOK] Sim::FindEntities_Registered @ %p -> %s", pFEReg, g_origSimFindEntsReg ? "OK" : "FAIL");
    }
    if (uint8_t* pSer = FIND_AOB(kAobSerializeWorld)) {
        g_origSerializeWorld = reinterpret_cast<LuaCFuncObj_t>(
            InstallHook(pSer, reinterpret_cast<void*>(&HookSerializeWorldSession), 16));
        LogMsg("[HOOK] SerializeWorldSession @ %p -> %s", pSer, g_origSerializeWorld ? "OK" : "FAIL");
    }

    // 7. Hook zlib compress2 -> SIMD libdeflate
    if (uint8_t* pComp2 = FIND_AOB(kAobCompress2)) {
        g_origCompress2 = reinterpret_cast<Compress2_t>(
            InstallHook(pComp2, reinterpret_cast<void*>(&HookCompress2), 17));
        LogMsg("[HOOK] zlib::compress2 @ %p -> %s", pComp2, g_origCompress2 ? "OK" : "FAIL");
    }

    // Optional read-only HTTP metrics thread (ONLY if explicitly enabled in INI)
    if (g_cfg.enable_http_metrics) {
        g_httpThread = CreateThread(nullptr, 0, HttpMetricsThread, nullptr, 0, nullptr);
    }
}

bool IsDedicatedServerHost() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    const wchar_t* base = std::wcsrchr(exePath, L'\\');
    base = base ? (base + 1) : exePath;
    return _wcsicmp(base, L"dontstarve_dedicated_server_nullrenderer_x64.exe") == 0;
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfMod = hinst;
        DisableThreadLibraryCalls(hinst);
        if (IsDedicatedServerHost()) {
            g_isServerProcess = true;
            InitServerHooks();
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_isServerProcess) {
            g_stopHttp.store(true);
            if (g_httpThread) {
                WaitForSingleObject(g_httpThread, 500);
                CloseHandle(g_httpThread);
                g_httpThread = nullptr;
            }
            LogTelemetrySummary("FINAL");
            if (g_timerBoosted) {
                timeEndPeriod(1);
                g_timerBoosted = false;
            }
            if (g_logCsInit) {
                DeleteCriticalSection(&g_logCs);
                g_logCsInit = false;
            }
        }
    }
    return TRUE;
}
