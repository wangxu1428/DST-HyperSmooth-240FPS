// =============================================================================
// DST HyperCore Server — Linux x86_64 Engine (v1.3.0)
// =============================================================================
// LD_PRELOAD + PIE-aware System V AMD64 native acceleration engine targeting
// Don't Starve Together Build 762172 (`dontstarve_dedicated_server_nullrenderer_x64`).
//
// Design Guarantees:
//   1. Strict Process Gate: __attribute__((constructor)) checks /proc/self/exe
//      basename; any non-server process is a 100% transparent pass-through.
//   2. Zero Global Heap Hijack: mimalloc is compiled with MI_OVERRIDE=0 and
//      attached ONLY to Klei's Simulation / WorldGen Lua VMs via lua_newstate.
//   3. ASLR-Safe Trampolines + W^X: mmap slab populated while PROT_READ|PROT_WRITE,
//      then permanently locked PROT_READ|PROT_EXEC. All 7-byte RIP-relative
//      `lea reg, [rip + disp32]` instructions in stolen prologues are rewritten
//      to 10-byte `movabs reg, imm64` so arbitrary >2GB PIE/mmap separation works.
//   4. Zero libstdc++.so.6 DT_NEEDED, GLIBC <= 2.17, fail-closed per-feature AOB
//      and barrier checks, immortal singletons (zero teardown crashes on exit).
// =============================================================================

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <link.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "anti_stw_gc.h"
#include "fast_astar.h"
#include "fast_astar_manager.h"
#include "fast_dumper.h"
#include "libdeflate.h"
#include "mimalloc.h"

namespace {

// =============================================================================
// 1. Real libc Function Pointers (Resolved via dlsym(RTLD_NEXT) Once)
// =============================================================================
using OpenFn_t      = int (*)(const char*, int, ...);
using FopenFn_t     = FILE* (*)(const char*, const char*);
using FwriteFn_t    = size_t (*)(const void*, size_t, size_t, FILE*);
using FcloseFn_t    = int (*)(FILE*);
using XstatFn_t     = int (*)(int, const char*, void*);
using StatFn_t      = int (*)(const char*, void*);
using AccessFn_t    = int (*)(const char*, int);
using OpendirFn_t   = DIR* (*)(const char*);
using MkdirFn_t     = int (*)(const char*, mode_t);
using RmdirFn_t     = int (*)(const char*);
using UnlinkFn_t    = int (*)(const char*);
using RemoveFn_t    = int (*)(const char*);
using RenameFn_t    = int (*)(const char*, const char*);
using ExitFn_t      = void (*)(int);

OpenFn_t    g_real_open      = nullptr;
OpenFn_t    g_real_open64    = nullptr;
FopenFn_t   g_real_fopen     = nullptr;
FopenFn_t   g_real_fopen64   = nullptr;
FwriteFn_t  g_real_fwrite    = nullptr;
FcloseFn_t  g_real_fclose    = nullptr;
XstatFn_t   g_real_xstat     = nullptr;
XstatFn_t   g_real_xstat64   = nullptr;
StatFn_t    g_real_stat      = nullptr;
StatFn_t    g_real_stat64    = nullptr;
AccessFn_t  g_real_access    = nullptr;
OpendirFn_t g_real_opendir   = nullptr;
MkdirFn_t   g_real_mkdir     = nullptr;
RmdirFn_t   g_real_rmdir     = nullptr;
UnlinkFn_t  g_real_unlink    = nullptr;
RemoveFn_t  g_real_remove    = nullptr;
RenameFn_t  g_real_rename    = nullptr;
ExitFn_t    g_real_exit      = nullptr;
ExitFn_t    g_real__exit     = nullptr;

pthread_once_t g_symsOnce = PTHREAD_ONCE_INIT;
std::atomic<bool> g_symsReady{false};
std::atomic<bool> g_isServerProcess{false};
std::atomic<bool> g_hooksReady{false};

void ResolveNextSymbolsImpl() {
    g_real_open    = reinterpret_cast<OpenFn_t>(dlsym(RTLD_NEXT, "open"));
    g_real_open64  = reinterpret_cast<OpenFn_t>(dlsym(RTLD_NEXT, "open64"));
    g_real_fopen   = reinterpret_cast<FopenFn_t>(dlsym(RTLD_NEXT, "fopen"));
    g_real_fopen64 = reinterpret_cast<FopenFn_t>(dlsym(RTLD_NEXT, "fopen64"));
    g_real_fwrite  = reinterpret_cast<FwriteFn_t>(dlsym(RTLD_NEXT, "fwrite"));
    g_real_fclose  = reinterpret_cast<FcloseFn_t>(dlsym(RTLD_NEXT, "fclose"));
    g_real_xstat   = reinterpret_cast<XstatFn_t>(dlsym(RTLD_NEXT, "__xstat"));
    g_real_xstat64 = reinterpret_cast<XstatFn_t>(dlsym(RTLD_NEXT, "__xstat64"));
    g_real_stat    = reinterpret_cast<StatFn_t>(dlsym(RTLD_NEXT, "stat"));
    g_real_stat64  = reinterpret_cast<StatFn_t>(dlsym(RTLD_NEXT, "stat64"));
    g_real_access  = reinterpret_cast<AccessFn_t>(dlsym(RTLD_NEXT, "access"));
    g_real_opendir = reinterpret_cast<OpendirFn_t>(dlsym(RTLD_NEXT, "opendir"));
    g_real_mkdir   = reinterpret_cast<MkdirFn_t>(dlsym(RTLD_NEXT, "mkdir"));
    g_real_rmdir   = reinterpret_cast<RmdirFn_t>(dlsym(RTLD_NEXT, "rmdir"));
    g_real_unlink  = reinterpret_cast<UnlinkFn_t>(dlsym(RTLD_NEXT, "unlink"));
    g_real_remove  = reinterpret_cast<RemoveFn_t>(dlsym(RTLD_NEXT, "remove"));
    g_real_rename  = reinterpret_cast<RenameFn_t>(dlsym(RTLD_NEXT, "rename"));
    g_real_exit    = reinterpret_cast<ExitFn_t>(dlsym(RTLD_NEXT, "exit"));
    g_real__exit   = reinterpret_cast<ExitFn_t>(dlsym(RTLD_NEXT, "_exit"));
    g_symsReady.store(true, std::memory_order_release);
}

inline void EnsureRealSyms() noexcept {
    if (!g_symsReady.load(std::memory_order_acquire)) {
        pthread_once(&g_symsOnce, ResolveNextSymbolsImpl);
    }
}

thread_local int tl_shimDepth = 0;
struct ShimGuard {
    bool active = true;
    ShimGuard() noexcept { ++tl_shimDepth; }
    ~ShimGuard() noexcept { --tl_shimDepth; }
};

// =============================================================================
// 2. Timing, Paths, INI Config & Immortal Logger
// =============================================================================
inline int64_t NowNs() noexcept {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + static_cast<int64_t>(ts.tv_nsec);
}

inline double NsDeltaUs(int64_t t0, int64_t t1) noexcept {
    return (t1 > t0) ? static_cast<double>(t1 - t0) * 0.001 : 0.0;
}

struct ServerConfig {
    bool  enable_profiler          = true;
    int   log_interval_ticks       = 300;
    bool  enable_mimalloc          = true;
    bool  enable_spatial_zerocopy  = true;
    bool  enable_adaptive_gc       = true;
    bool  enable_anti_stw_gc       = true;
    float gc_spare_target_ms       = 28.0f;
    float gc_calm_max_ms           = 4.0f;
    float gc_busy_min_ms           = 0.4f;
    int   gc_emergency_add_mb      = 256;
    bool  enable_libdeflate        = true;
    int   libdeflate_level         = 5;
    bool  enable_fastload          = true;
    bool  enable_fast_dumper       = true;
    bool  enable_fast_astar        = true;
    bool  enable_fs_neg_cache      = true;
    bool  enable_async_save        = true;
};

ServerConfig g_cfg;
char g_binDir[1024]  = ".";
char g_iniPath[1088] = "./dst_hypercore.ini";
char g_logPath[1088] = "./dst_hypercore_server.log";
pthread_mutex_t g_logMtx = PTHREAD_MUTEX_INITIALIZER;
bool g_logResetOnce = false;

void LogMsg(const char* fmt, ...) {
    if (!g_real_fopen || !g_real_fwrite || !g_real_fclose) return;
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmLocal{};
    localtime_r(&ts.tv_sec, &tmLocal);
    const int ms = static_cast<int>(ts.tv_nsec / 1000000L);

    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char line[2176];
    const int lineLen = std::snprintf(
        line, sizeof(line), "[%02d:%02d:%02d.%03d] %s\n",
        tmLocal.tm_hour, tmLocal.tm_min, tmLocal.tm_sec, ms, msg);
    if (lineLen <= 0) return;

    pthread_mutex_lock(&g_logMtx);
    const char* mode = g_logResetOnce ? "ab" : "wb";
    FILE* f = g_real_fopen(g_logPath, mode);
    if (f) {
        g_logResetOnce = true;
        g_real_fwrite(line, 1, static_cast<size_t>(lineLen), f);
        g_real_fclose(f);
    }
    pthread_mutex_unlock(&g_logMtx);
}

static char* TrimAscii(char* s) noexcept {
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;
    size_t n = std::strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
    return s;
}

void InitPathsAndLoadConfig() {
    char exePath[1024] = {};
    const ssize_t rlen = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (rlen > 0) {
        exePath[rlen] = '\0';
        const char* slash = std::strrchr(exePath, '/');
        if (slash && slash > exePath) {
            const size_t dlen = static_cast<size_t>(slash - exePath);
            std::memcpy(g_binDir, exePath, dlen);
            g_binDir[dlen] = '\0';
        }
    }

    char shardTag[64] = "server";
    if (g_real_open) {
        const int fd = g_real_open("/proc/self/cmdline", O_RDONLY);
        if (fd >= 0) {
            char cmd[4096];
            const ssize_t n = read(fd, cmd, sizeof(cmd) - 1);
            close(fd);
            if (n > 0) {
                cmd[n] = '\0';
                ssize_t pos = 0;
                bool expectShard = false;
                while (pos < n) {
                    const char* arg = cmd + pos;
                    const size_t alen = std::strlen(arg);
                    if (expectShard && alen > 0 && alen < 48) {
                        size_t j = 0;
                        for (size_t i = 0; i < alen && j < sizeof(shardTag) - 1; ++i) {
                            const char c = arg[i];
                            if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                                (c >= '0' && c <= '9') || c == '_' || c == '-') {
                                shardTag[j++] = c;
                            }
                        }
                        if (j > 0) shardTag[j] = '\0';
                        break;
                    }
                    expectShard = (std::strcmp(arg, "-shard") == 0);
                    pos += static_cast<ssize_t>(alen) + 1;
                }
            }
        }
    }

    std::snprintf(g_iniPath, sizeof(g_iniPath), "%s/dst_hypercore.ini", g_binDir);
    std::snprintf(g_logPath, sizeof(g_logPath), "%s/dst_hypercore_%s.log", g_binDir, shardTag);

    FILE* f = g_real_fopen ? g_real_fopen(g_iniPath, "rb") : nullptr;
    if (!f) return;
    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        char* p = TrimAscii(line);
        if (*p == '\0' || *p == '#' || *p == ';' || *p == '[') continue;
        char* eq = std::strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = TrimAscii(p);
        char* val = TrimAscii(eq + 1);
        char* cmt = std::strchr(val, '#');
        if (cmt) { *cmt = '\0'; val = TrimAscii(val); }
        cmt = std::strchr(val, ';');
        if (cmt) { *cmt = '\0'; val = TrimAscii(val); }

        const int iv = std::atoi(val);
        const bool bv = (iv != 0 || std::strcmp(val, "true") == 0 || std::strcmp(val, "TRUE") == 0);
        if (std::strcmp(key, "enable_profiler") == 0) g_cfg.enable_profiler = bv;
        else if (std::strcmp(key, "log_interval_ticks") == 0) g_cfg.log_interval_ticks = std::clamp(iv, 30, 36000);
        else if (std::strcmp(key, "enable_mimalloc") == 0) g_cfg.enable_mimalloc = bv;
        else if (std::strcmp(key, "enable_spatial_zerocopy") == 0) g_cfg.enable_spatial_zerocopy = bv;
        else if (std::strcmp(key, "enable_adaptive_gc") == 0) g_cfg.enable_adaptive_gc = bv;
        else if (std::strcmp(key, "enable_anti_stw_gc") == 0) g_cfg.enable_anti_stw_gc = bv;
        else if (std::strcmp(key, "gc_spare_target_ms") == 0) g_cfg.gc_spare_target_ms = std::clamp(static_cast<float>(std::atof(val)), 5.0f, 32.0f);
        else if (std::strcmp(key, "gc_calm_max_ms") == 0) g_cfg.gc_calm_max_ms = std::clamp(static_cast<float>(std::atof(val)), 0.5f, 12.0f);
        else if (std::strcmp(key, "gc_busy_min_ms") == 0) g_cfg.gc_busy_min_ms = std::clamp(static_cast<float>(std::atof(val)), 0.1f, 2.0f);
        else if (std::strcmp(key, "gc_emergency_add_mb") == 0) g_cfg.gc_emergency_add_mb = std::clamp(iv, 64, 2048);
        else if (std::strcmp(key, "enable_libdeflate") == 0) g_cfg.enable_libdeflate = bv;
        else if (std::strcmp(key, "libdeflate_level") == 0) g_cfg.libdeflate_level = std::clamp(iv, 1, 9);
        else if (std::strcmp(key, "enable_fast_load") == 0 || std::strcmp(key, "enable_fastload") == 0) g_cfg.enable_fastload = bv;
        else if (std::strcmp(key, "enable_fast_dumper") == 0) g_cfg.enable_fast_dumper = bv;
        else if (std::strcmp(key, "enable_fast_astar") == 0) g_cfg.enable_fast_astar = bv;
        else if (std::strcmp(key, "enable_fs_neg_cache") == 0) g_cfg.enable_fs_neg_cache = bv;
        else if (std::strcmp(key, "enable_async_save") == 0) g_cfg.enable_async_save = bv;
    }
    g_real_fclose(f);
}

// =============================================================================
// 3. Telemetry Counters (Exact Cross-Platform Parity with Windows Build)
// =============================================================================
struct Telemetry {
    static constexpr size_t kRingSize = 512;
    std::atomic<uint64_t> ticks_total{0};
    std::atomic<uint64_t> tick_us_sum{0};
    std::atomic<uint64_t> tick_us_max{0};
    std::atomic<uint64_t> tick_overrun_count{0};
    uint32_t              tick_ring[kRingSize] = {};
    uint32_t              tick_ring_pos = 0;

    std::atomic<uint64_t> gc_calls{0};
    std::atomic<uint64_t> gc_us_sum{0};
    std::atomic<uint64_t> gc_us_max{0};
    std::atomic<uint64_t> gc_full_count{0};
    std::atomic<uint64_t> gc_stw_blocked{0};
    std::atomic<int32_t>  gc_last_kb{0};
    std::atomic<int32_t>  gc_last_budget_us{0};
    uint64_t              cur_tick_gc_us = 0;
    double                last_tick_non_gc_ms = 0.0;

    std::atomic<uint64_t> find_entities_calls{0};
    std::atomic<uint64_t> find_entities_us_sum{0};
    std::atomic<uint64_t> spatial_zerocopy_sites{0};

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

    std::atomic<uint64_t> serialize_world_calls{0};
    std::atomic<uint64_t> serialize_world_us_sum{0};

    std::atomic<uint64_t> unz_hits{0};
    std::atomic<uint64_t> unz_bytes{0};
    std::atomic<uint64_t> unz_us_sum{0};

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

    void PushTickSample(uint32_t us) {
        tick_ring[tick_ring_pos & (kRingSize - 1)] = us;
        ++tick_ring_pos;
    }

    void ComputePercentiles(double& avgUs, double& p95Us, double& p99Us) const {
        const uint64_t total = ticks_total.load(std::memory_order_relaxed);
        avgUs = total ? static_cast<double>(tick_us_sum.load(std::memory_order_relaxed)) / static_cast<double>(total) : 0.0;
        const size_t n = (total < kRingSize) ? static_cast<size_t>(total) : kRingSize;
        if (n == 0) { p95Us = p99Us = 0.0; return; }
        uint32_t tmp[kRingSize];
        std::memcpy(tmp, tick_ring, n * sizeof(uint32_t));
        std::sort(tmp, tmp + n);
        p95Us = tmp[std::min(n - 1, (n * 95) / 100)];
        p99Us = tmp[std::min(n - 1, (n * 99) / 100)];
    }

    void Reset() {
        ticks_total = 0; tick_us_sum = 0; tick_us_max = 0; tick_overrun_count = 0; tick_ring_pos = 0;
        gc_calls = 0; gc_us_sum = 0; gc_us_max = 0; gc_full_count = 0; gc_stw_blocked = 0;
        find_entities_calls = 0; find_entities_us_sum = 0;
        astar_inits = 0; astar_steps = 0; astar_us_sum = 0; astar_completed = 0; astar_failed = 0; astar_fallbacks = 0;
        fastdump_calls = 0; fastdump_us_sum = 0; fastdump_bytes_sum = 0; fastdump_fallbacks = 0;
        deflate_calls = 0; deflate_us_sum = 0; deflate_main_calls = 0; deflate_main_us_sum = 0;
        deflate_in_bytes = 0; deflate_out_bytes = 0;
        serialize_world_calls = 0; serialize_world_us_sum = 0;
        prof_lua_update_calls = 0; prof_lua_update_us_sum = 0; prof_lua_update_us_max = 0;
        prof_net_update_calls = 0; prof_net_update_us_sum = 0; prof_net_update_us_max = 0;
        prof_replica_update_calls = 0; prof_replica_update_us_sum = 0; prof_replica_update_us_max = 0;
        fs_neg_hits = 0; fs_neg_misses = 0; async_save_fallbacks = 0;
    }
};

Telemetry g_stats;

inline void AtomicUpdateMax(std::atomic<uint64_t>& target, uint64_t val) noexcept {
    uint64_t prev = target.load(std::memory_order_relaxed);
    while (val > prev && !target.compare_exchange_weak(prev, val, std::memory_order_relaxed)) {}
}

// =============================================================================
// 4. PIE Main ELF Range Finder, W^X Slab & ASLR-Safe movabs Trampoline Engine
// =============================================================================
uint8_t* g_exeBase    = nullptr;
uint8_t* g_exeTextBeg = nullptr;
uint8_t* g_exeTextEnd = nullptr;
size_t   g_pageSize   = 4096;

constexpr size_t kSlabSize = 65536;
uint8_t* g_trampSlab = nullptr;
size_t   g_trampUsed = 0;
bool     g_slabLocked = false;

int DlPhdrCallback(struct dl_phdr_info* info, size_t /*size*/, void* /*data*/) {
    // On Linux PIE dynamic executables, info->dlpi_name is "" or equals /proc/self/exe for main image
    if (!g_exeBase && info->dlpi_name && info->dlpi_name[0] == '\0') {
        g_exeBase = reinterpret_cast<uint8_t*>(info->dlpi_addr);
        uint8_t* execBeg = nullptr;
        uint8_t* execEnd = nullptr;
        for (int i = 0; i < info->dlpi_phnum; ++i) {
            const auto& ph = info->dlpi_phdr[i];
            if (ph.p_type == PT_LOAD && (ph.p_flags & PF_X) != 0) {
                uint8_t* segBeg = g_exeBase + ph.p_vaddr;
                uint8_t* segEnd = segBeg + ph.p_memsz;
                if (!execBeg || segBeg < execBeg) execBeg = segBeg;
                if (!execEnd || segEnd > execEnd) execEnd = segEnd;
            }
        }
        g_exeTextBeg = execBeg;
        g_exeTextEnd = execEnd;
        return 1;
    }
    return 0;
}

uint8_t* FindUniquePattern(const uint8_t* pat, size_t len) noexcept {
    if (!g_exeTextBeg || !g_exeTextEnd || len == 0 || static_cast<size_t>(g_exeTextEnd - g_exeTextBeg) < len) {
        return nullptr;
    }
    const size_t span = static_cast<size_t>(g_exeTextEnd - g_exeTextBeg);
    uint8_t* hit = nullptr;
    for (size_t i = 0; i <= span - len; ++i) {
        if (g_exeTextBeg[i] == pat[0] && std::memcmp(g_exeTextBeg + i, pat, len) == 0) {
            if (hit) return nullptr; // fail-closed if duplicate!
            hit = g_exeTextBeg + i;
        }
    }
    return hit;
}

template <size_t N>
inline uint8_t* FindUniqueAob(const uint8_t (&pat)[N]) noexcept {
    return FindUniquePattern(pat, N);
}

struct RelocLea {
    uint8_t offset; // offset inside stolen prologue where `48 8D modrm disp32` starts
};

bool WriteAbsJmp14(uint8_t* dst, const void* target) noexcept {
    const uint64_t addr = reinterpret_cast<uint64_t>(target);
    dst[0] = 0xFF;
    dst[1] = 0x25;
    dst[2] = 0x00; dst[3] = 0x00; dst[4] = 0x00; dst[5] = 0x00;
    std::memcpy(dst + 6, &addr, 8);
    return true;
}

bool MprotectRange(void* addr, size_t len, int prot) noexcept {
    const uintptr_t p0 = reinterpret_cast<uintptr_t>(addr) & ~(g_pageSize - 1u);
    const uintptr_t p1 = (reinterpret_cast<uintptr_t>(addr) + len + g_pageSize - 1u) & ~(g_pageSize - 1u);
    return mprotect(reinterpret_cast<void*>(p0), p1 - p0, prot) == 0;
}

bool PatchTextBytesWX(uint8_t* dst, const uint8_t* bytes, size_t len) noexcept {
    if (!dst || !bytes || len == 0) return false;
    if (!MprotectRange(dst, len, PROT_READ | PROT_WRITE | PROT_EXEC)) return false;
    std::memcpy(dst, bytes, len);
    __builtin___clear_cache(reinterpret_cast<char*>(dst), reinterpret_cast<char*>(dst + len));
    MprotectRange(dst, len, PROT_READ | PROT_EXEC);
    return true;
}

// Installs a SysV AMD64 inline detour. Any 7-byte `48 8D modrm disp32` in the
// stolen prologue is rewritten to 10-byte `48 (B8+reg) imm64` in the trampoline
// so >2GB ASLR separation between PIE image and mmap slab never overflows int32.
bool InstallHookSysV(uint8_t* target, void* detour, size_t stolenLen, void** outOrig,
                     const RelocLea* relocs = nullptr, size_t relocCount = 0) noexcept {
    if (!target || !detour || !outOrig || stolenLen < 14 || g_slabLocked) return false;
    if (!g_trampSlab) {
        void* m = mmap(nullptr, kSlabSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m == MAP_FAILED) return false;
        g_trampSlab = static_cast<uint8_t*>(m);
    }
    const size_t alignedOff = (g_trampUsed + 15u) & ~size_t(15u);
    const size_t maxTrampNeed = stolenLen + relocCount * 3u + 16u;
    if (alignedOff + maxTrampNeed > kSlabSize) return false;

    uint8_t* tramp = g_trampSlab + alignedOff;
    size_t tp = 0;
    size_t sp = 0;
    while (sp < stolenLen) {
        bool matchedRel = false;
        for (size_t r = 0; r < relocCount; ++r) {
            if (relocs[r].offset == sp) {
                if (sp + 7 > stolenLen || target[sp] != 0x48 || target[sp + 1] != 0x8D) {
                    return false;
                }
                const uint8_t modrm = target[sp + 2];
                if ((modrm & 0xC7u) != 0x05u) return false; // [rip + disp32] with base RBX/RDX/RAX (REX.R == 0)
                const uint8_t reg = static_cast<uint8_t>((modrm >> 3) & 7u);
                int32_t disp = 0;
                std::memcpy(&disp, target + sp + 3, 4);
                const uint64_t resolved = reinterpret_cast<uint64_t>(target + sp + 7) + static_cast<int64_t>(disp);
                tramp[tp++] = 0x48;
                tramp[tp++] = static_cast<uint8_t>(0xB8u + reg);
                std::memcpy(tramp + tp, &resolved, 8);
                tp += 8;
                sp += 7;
                matchedRel = true;
                break;
            }
        }
        if (!matchedRel) {
            tramp[tp++] = target[sp++];
        }
    }

    WriteAbsJmp14(tramp + tp, target + stolenLen);
    tp += 14;
    g_trampUsed = alignedOff + tp;
    *outOrig = tramp;

    uint8_t patch[64];
    if (stolenLen > sizeof(patch)) return false;
    std::memset(patch, 0x90, stolenLen);
    WriteAbsJmp14(patch, detour);
    return PatchTextBytesWX(target, patch, stolenLen);
}

void LockTrampolineSlabWX() noexcept {
    if (g_trampSlab && !g_slabLocked) {
        mprotect(g_trampSlab, kSlabSize, PROT_READ | PROT_EXEC);
        __builtin___clear_cache(reinterpret_cast<char*>(g_trampSlab),
                                reinterpret_cast<char*>(g_trampSlab + kSlabSize));
        g_slabLocked = true;
    }
}

// =============================================================================
// 5. Verified Build 762172 Linux AOBs (Each 1-of-1 Unique in .text)
// =============================================================================
constexpr uint8_t kAobLuaNewState[] = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x54, 0xB9, 0x78, 0x02, 0x00, 0x00, 0x31, 0xD2, 0x55, 0x48, 0x89, 0xFD, 0x53, 0x48, 0x89, 0xF3 };
constexpr uint8_t kAobLuaClose[]    = { 0xF3, 0x0F, 0x1E, 0xFA, 0x55, 0x53, 0x48, 0x8D, 0x1D, 0x03, 0xFC, 0xFF, 0xFF, 0x48, 0x83, 0xEC, 0x08, 0x48, 0x8B, 0x47, 0x20 };
constexpr uint8_t kAobLuaOpenLibs[] = { 0xF3, 0x0F, 0x1E, 0xFA, 0x55, 0x48, 0x89, 0xFD, 0x53, 0x48, 0x8D, 0x1D, 0xA0, 0x31, 0x0F, 0x00, 0x48, 0x83, 0xEC, 0x08 };
constexpr uint8_t kAobLuaGc[]       = { 0xF3, 0x0F, 0x1E, 0xFA, 0x55, 0x53, 0x48, 0x83, 0xEC, 0x08, 0x48, 0x8B, 0x5F, 0x20, 0x83, 0xFE, 0x08, 0x0F, 0x87, 0x1C, 0x04, 0xC2, 0xFF };
constexpr uint8_t kAobSimUpdate[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0x00, 0x10, 0x00, 0x00, 0x48, 0x83, 0x0C, 0x24, 0x00, 0x48, 0x81, 0xEC, 0x18, 0x01, 0x00, 0x00 };
constexpr uint8_t kAobSimLuaUpd[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x57, 0x41, 0x89, 0xF7, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x55, 0x89, 0xF5, 0x53, 0x48, 0x89, 0xFB };
constexpr uint8_t kAobSimCollGc[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x53, 0x48, 0x89, 0xFB, 0x48, 0x8B, 0xBF, 0xD8, 0x00, 0x00, 0x00, 0x31, 0xD2, 0xBE, 0x03, 0x00, 0x00, 0x00 };
constexpr uint8_t kAobNetMgrUpd[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x57, 0xB9, 0x78, 0x06, 0x00, 0x00, 0x48, 0x8D, 0x15, 0x86, 0xA4, 0x23, 0x00, 0x48, 0x8D, 0x35, 0xA4, 0xAE, 0x23, 0x00 };
constexpr uint8_t kAobRepMgrUpd[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x57, 0xB9, 0x73, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x15, 0xFE, 0xC2, 0x1D, 0x00, 0x48, 0x8D, 0x35, 0x27, 0xC3, 0x1D, 0x00 };
constexpr uint8_t kAobSimFindEnts[] = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x49, 0x89, 0xFC, 0x48, 0x89, 0xF7, 0x55, 0x48, 0x89, 0xF5 };
constexpr uint8_t kAobSimFindReg[]  = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x55, 0x41, 0x54, 0x55, 0x48, 0x89, 0xF5, 0x53, 0x48, 0x89, 0xFB, 0x48, 0x89, 0xF7, 0x48, 0x83, 0xEC, 0x48 };
constexpr uint8_t kAobSerializeW[]  = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x55, 0x48, 0x89, 0xF5, 0x53, 0x48, 0x89, 0xFB, 0x48, 0x81, 0xEC, 0xE8, 0x00, 0x00, 0x00 };
constexpr uint8_t kAobCompress2[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x54, 0x55, 0x53, 0x48, 0x89, 0xF3, 0x48, 0x83, 0xC4, 0x80, 0x64, 0x48, 0x8B, 0x04, 0x25, 0x28, 0x00, 0x00, 0x00 };
constexpr uint8_t kAobUnzRead[]     = { 0xF3, 0x0F, 0x1E, 0xFA, 0x48, 0x85, 0xFF, 0x0F, 0x84, 0x11, 0x02, 0x00, 0x00, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x55 };
constexpr uint8_t kAobInflateFast[] = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x57, 0x41, 0xB9, 0x01, 0x00, 0x00, 0x00, 0x41, 0x56, 0x44, 0x89, 0xCA, 0x41, 0x55, 0x41, 0x54 };
constexpr uint8_t kAobWritePFile[]  = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x57, 0x41, 0x56, 0x45, 0x89, 0xC6, 0x41, 0x55, 0x41, 0x54, 0x49, 0x89, 0xFC, 0x55, 0x53 };
constexpr uint8_t kAobAStarStep[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x55, 0x53, 0x48, 0x83, 0xEC, 0x78, 0x44, 0x8B, 0x67, 0x08, 0x64, 0x48, 0x8B, 0x04, 0x25, 0x28, 0x00, 0x00, 0x00 };
constexpr uint8_t kAobAStarDtor0[]  = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x54, 0x48, 0x8D, 0x05, 0xB3, 0xE0, 0x3D, 0x00, 0x55, 0x48, 0x89, 0xFD, 0x53, 0x4C, 0x8B, 0x67, 0x30 };
constexpr uint8_t kAobAStarDtor1[]  = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x54, 0x48, 0x8D, 0x05, 0xD3, 0xDF, 0x3D, 0x00, 0x55, 0x53, 0x4C, 0x8B, 0x67, 0x30, 0x48, 0x89, 0xFB };
constexpr uint8_t kAobPathDtor0[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x48, 0x85, 0xF6, 0x0F, 0x84, 0x63, 0x01, 0x00, 0x00, 0x41, 0x57, 0x41, 0x56, 0x4C, 0x8D, 0x35, 0xE8, 0xD3, 0x3D, 0x00 };

// Internal helper AOBs (resolved without hooking)
constexpr uint8_t kAobLuaCheckStack[]  = { 0xF3, 0x0F, 0x1E, 0xFA, 0x45, 0x31, 0xC0, 0x81, 0xFE, 0x40, 0x1F, 0x00, 0x00, 0x7F, 0x31, 0x55 };
constexpr uint8_t kAobLuaGetTop[]      = { 0xF3, 0x0F, 0x1E, 0xFA, 0x48, 0x8B, 0x47, 0x10, 0x48, 0x2B, 0x47, 0x18, 0x48, 0xC1, 0xF8, 0x04 };
constexpr uint8_t kAobLuaSetTop[]      = { 0xF3, 0x0F, 0x1E, 0xFA, 0x48, 0x8B, 0x47, 0x10, 0x48, 0x63, 0xCE, 0x85, 0xF6, 0x78, 0x39, 0x48, 0x8B, 0x57, 0x18 };
constexpr uint8_t kAobLuaPushValue[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x49, 0x89, 0xF8, 0xE8, 0x24, 0xFB, 0xFF, 0xFF, 0x48, 0x8B, 0x08, 0x49, 0x8B, 0x50, 0x10 };
constexpr uint8_t kAobLuaType[]        = { 0xF3, 0x0F, 0x1E, 0xFA, 0xE8, 0xF7, 0xFA, 0xFF, 0xFF, 0x48, 0x8D, 0x15, 0x30, 0x17, 0x05, 0x00 };
constexpr uint8_t kAobLuaToNumber[]    = { 0xF3, 0x0F, 0x1E, 0xFA, 0x48, 0x83, 0xEC, 0x28, 0x64, 0x48, 0x8B, 0x04, 0x25, 0x28, 0x00, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x18, 0x31, 0xC0, 0xE8, 0xE3, 0xF8, 0xFF, 0xFF };
constexpr uint8_t kAobLuaToBoolean[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0xE8, 0x37, 0xF8, 0xFF, 0xFF, 0x44, 0x8B, 0x40, 0x08, 0x45, 0x85, 0xC0 };
constexpr uint8_t kAobLuaToLString[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x54, 0x41, 0x89, 0xF4, 0x55, 0x48, 0x89, 0xFD, 0x53, 0x48, 0x89, 0xD3, 0xE8, 0xEA, 0xF7, 0xFF, 0xFF };
constexpr uint8_t kAobLuaToPointer[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x49, 0x89, 0xF8, 0x41, 0x89, 0xF1, 0xE8, 0x61, 0xF6, 0xFF, 0xFF, 0x8B, 0x50, 0x08 };
constexpr uint8_t kAobLuaPushNil[]     = { 0xF3, 0x0F, 0x1E, 0xFA, 0x48, 0x8B, 0x47, 0x10, 0xC7, 0x40, 0x08, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0x47, 0x10, 0x10 };
constexpr uint8_t kAobLuaPushNumber[]  = { 0xF3, 0x0F, 0x1E, 0xFA, 0x48, 0x8B, 0x47, 0x10, 0xC7, 0x40, 0x08, 0x03, 0x00, 0x00, 0x00, 0xF2, 0x0F, 0x11, 0x00 };
constexpr uint8_t kAobLuaPushLString[] = { 0xF3, 0x0F, 0x1E, 0xFA, 0x55, 0x53, 0x48, 0x89, 0xFB, 0x48, 0x83, 0xEC, 0x18, 0x80, 0xBF, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x74, 0x0E, 0x48, 0x8B, 0x47, 0x20, 0x48, 0x8B, 0x48, 0x70, 0x48, 0x39, 0x48, 0x78, 0x73, 0x2C, 0x48, 0x8B, 0x6B, 0x10, 0x48, 0x89, 0xDF, 0xE8, 0x70, 0x95, 0x00, 0x00 };
constexpr uint8_t kAobLuaPushString[]  = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x54, 0x49, 0x89, 0xFC, 0x55, 0x48, 0x83, 0xEC, 0x08, 0x48, 0x85, 0xF6 };
constexpr uint8_t kAobLuaPushCClos[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x54, 0x49, 0x89, 0xF4, 0x55, 0x89, 0xD5, 0x53, 0x80, 0xBF, 0xC0, 0x00, 0x00, 0x00, 0x00 };
constexpr uint8_t kAobLuaPushBool[]    = { 0xF3, 0x0F, 0x1E, 0xFA, 0x48, 0x8B, 0x47, 0x10, 0x31, 0xD2, 0x85, 0xF6, 0x0F, 0x95, 0xC2, 0x89, 0x10 };
constexpr uint8_t kAobLuaGetField[]    = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x54, 0x55, 0x48, 0x89, 0xD5, 0x53, 0x48, 0x89, 0xFB, 0x48, 0x83, 0xEC, 0x20, 0x64, 0x48, 0x8B, 0x04, 0x25, 0x28, 0x00, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x18, 0x31, 0xC0, 0xE8, 0x09, 0xF2, 0xFF, 0xFF };
constexpr uint8_t kAobLuaCreateTbl[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x55, 0x53, 0x48, 0x89, 0xFB, 0x48, 0x83, 0xEC, 0x18, 0x80, 0xBF, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x74, 0x0E, 0x48, 0x8B, 0x47, 0x20, 0x48, 0x8B, 0x48, 0x70, 0x48, 0x39, 0x48, 0x78, 0x73, 0x2C, 0x48, 0x8B, 0x6B, 0x10, 0x48, 0x89, 0xDF, 0xE8, 0x30, 0x98, 0x00, 0x00 };
constexpr uint8_t kAobLuaSetField[]    = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x54, 0x55, 0x48, 0x89, 0xD5, 0x53, 0x48, 0x89, 0xFB, 0x48, 0x83, 0xEC, 0x20, 0x64, 0x48, 0x8B, 0x04, 0x25, 0x28, 0x00, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x18, 0x31, 0xC0, 0xE8, 0x99, 0xEF, 0xFF, 0xFF };
constexpr uint8_t kAobLuaNext[]        = { 0xF3, 0x0F, 0x1E, 0xFA, 0x53, 0x48, 0x89, 0xFB, 0xE8, 0x63, 0xE9, 0xFF, 0xFF, 0x48, 0x8B, 0x4B, 0x10 };
constexpr uint8_t kAobSetGcBudget[]    = { 0xF3, 0x0F, 0x1E, 0xFA, 0xF3, 0x0F, 0x11, 0x05, 0xB8, 0xBC, 0x10, 0x00, 0xC3, 0x0F, 0x1F, 0x00 };
constexpr uint8_t kAobSimLuaAlloc[]    = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x55, 0x49, 0x89, 0xD5, 0x41, 0x54, 0x49, 0x89, 0xFC, 0x55, 0x48, 0x89, 0xF5 };
constexpr uint8_t kAobWgLuaAlloc[]     = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x55, 0x41, 0x54, 0x55, 0x48, 0x89, 0xF5, 0x53, 0x48, 0x83, 0xEC, 0x08, 0x48, 0x85, 0xC9 };
constexpr uint8_t kAobTrackLuaMem[]    = { 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x55, 0x53, 0x48, 0x81, 0xEC, 0xF8, 0x02, 0x00, 0x00, 0x4C, 0x8B, 0xA7, 0xD8, 0x00, 0x00, 0x00 };
constexpr uint8_t kAobAStarHeur[]      = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x54, 0x49, 0x89, 0xF4, 0x55, 0x48, 0x89, 0xFD, 0x48, 0x8D, 0x55, 0x3C };
constexpr uint8_t kAobAStarNbLand[]    = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x57, 0x41, 0x56, 0x49, 0x89, 0xF6, 0x41, 0x55, 0x41, 0x54, 0x55, 0x53, 0x48, 0x89, 0xFB, 0x48, 0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00 };
constexpr uint8_t kAobAStarNbWater[]   = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x57, 0x49, 0x89, 0xFF, 0x41, 0x56, 0x41, 0x55, 0x49, 0x89, 0xD5, 0x41, 0x54, 0x55, 0x48, 0x89, 0xF5 };
constexpr uint8_t kAobKleiNew[]        = { 0xF3, 0x0F, 0x1E, 0xFA, 0x48, 0x83, 0xEC, 0x10, 0x48, 0x89, 0xFE, 0x45, 0x31, 0xC9, 0x41, 0xB8, 0x0B, 0x00, 0x00, 0x00, 0x6A, 0x00, 0x31, 0xC9, 0xBA, 0x08, 0x00, 0x00, 0x00, 0x31, 0xFF, 0xE8, 0x1C, 0xFC, 0xFF, 0xFF };
constexpr uint8_t kAobKleiDelete[]     = { 0xF3, 0x0F, 0x1E, 0xFA, 0x48, 0x89, 0xFE, 0x31, 0xFF, 0xE9, 0x42, 0xFD, 0xFF, 0xFF, 0x66, 0x90 };

// SpatialGrid Zero-Copy Site Anchors + Delete Windows (exact verified bytes)
constexpr uint8_t kAobSpatialAnchorFindEnts[] = {
    0x0F, 0x87, 0xC4, 0x04, 0x00, 0x00,
    0xE8, 0x91, 0x4F, 0x16, 0x00, 0x48, 0x8B, 0x73, 0x08, 0x48, 0x89, 0xC5, 0x48, 0x8B, 0x43, 0x10, 0x48, 0x89, 0xC7, 0x48, 0x29, 0xF7,
    0x48, 0x8D, 0x4C, 0x3D, 0x00, 0x48, 0x89, 0x4C, 0x24, 0x50, 0x48, 0x89, 0xCB, 0x48, 0x39, 0xF0, 0x0F, 0x84, 0xDA, 0x02, 0x00, 0x00,
    0x48, 0x89, 0xFA, 0x48, 0x89, 0xEF, 0xE8, 0xAF, 0x0F, 0xE8, 0xFF
};
constexpr uint8_t kAobSpatialDelFindEnts[] = { 0xFF, 0x48, 0x89, 0xEF, 0x41, 0x83, 0xC6, 0x01, 0xE8, 0xF4, 0x4D, 0x16, 0x00, 0x44, 0x3B, 0x74 };

constexpr uint8_t kAobSpatialAnchorFindReg[] = {
    0x0F, 0x87, 0xCF, 0x02, 0x00, 0x00,
    0xE8, 0xFD, 0x57, 0x16, 0x00, 0x48, 0x8B, 0x73, 0x08, 0x49, 0x89, 0xC5, 0x48, 0x8B, 0x43, 0x10, 0x48, 0x89, 0xC7, 0x48, 0x29, 0xF7,
    0x49, 0x8D, 0x4C, 0x3D, 0x00, 0x48, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0xCB, 0x48, 0x39, 0xF0, 0x0F, 0x84, 0x46, 0x02, 0x00, 0x00,
    0x48, 0x89, 0xFA, 0x4C, 0x89, 0xEF, 0xE8, 0x1B, 0x18, 0xE8, 0xFF
};
constexpr uint8_t kAobSpatialDelFindReg[] = { 0xFF, 0x4C, 0x89, 0xEF, 0x41, 0x83, 0xC4, 0x01, 0xE8, 0xFC, 0x56, 0x16, 0x00, 0x44, 0x3B, 0x64 };

constexpr uint8_t kAobSpatialAnchorLightInt[] = {
    0x0F, 0x87, 0xA2, 0x02, 0x00, 0x00,
    0xE8, 0x26, 0xBB, 0x16, 0x00, 0x48, 0x8B, 0x73, 0x08, 0x49, 0x89, 0xC6, 0x48, 0x8B, 0x43, 0x10, 0x48, 0x89, 0xC7, 0x48, 0x29, 0xF7,
    0x49, 0x8D, 0x0C, 0x3E, 0x48, 0x89, 0x4C, 0x24, 0x20, 0x48, 0x89, 0xCB, 0x48, 0x39, 0xF0, 0x0F, 0x84, 0xF0, 0x01, 0x00, 0x00,
    0x48, 0x89, 0xFA, 0x4C, 0x89, 0xF7, 0xE8, 0x45, 0x7B, 0xE8, 0xFF
};
constexpr uint8_t kAobSpatialDel1LightInt[] = { 0x4D, 0x85, 0xF6, 0x74, 0x08, 0x4C, 0x89, 0xF7, 0xE8, 0x50, 0xBA, 0x16, 0x00, 0x48, 0x8B, 0x44 };
constexpr uint8_t kAobSpatialDel2LightInt[] = { 0xFF, 0x0F, 0x1F, 0x40, 0x00, 0x4C, 0x89, 0xF7, 0xE8, 0x80, 0xB9, 0x16, 0x00, 0xE9, 0x3D, 0xFD };

constexpr uint8_t kAobSpatialAnchorLightRGB[] = {
    0x03, 0x00, 0x00, 0x48, 0x89, 0xD7,
    0xE8, 0x8C, 0xB5, 0x16, 0x00, 0x48, 0x8B, 0x75, 0x08, 0x48, 0x89, 0xC7, 0x48, 0x8B, 0x45, 0x10, 0x48, 0x89, 0xC2, 0x48, 0x29, 0xF2,
    0x4C, 0x8D, 0x04, 0x17, 0x48, 0x39, 0xC6, 0x0F, 0x84, 0xDE, 0x02, 0x00, 0x00, 0x4C, 0x89, 0x44, 0x24, 0x20,
    0xE8, 0xB4, 0x75, 0xE8, 0xFF, 0x4C, 0x8B, 0x44, 0x24, 0x20, 0x48
};
constexpr uint8_t kAobSpatialDelLightRGB[] = { 0x85, 0xFF, 0x0F, 0x84, 0x86, 0xFC, 0xFF, 0xFF, 0xE8, 0x25, 0xB3, 0x16, 0x00, 0xE9, 0x7C, 0xFC };

// =============================================================================
// 6. Module 7: POSIX Read-Only FS Negative Stat Cache (Immortal Singleton)
// =============================================================================
class PosixFsNegCache {
public:
    static constexpr size_t kCap = 16384; // power of 2
    struct Slot {
        uint64_t h1 = 0;
        uint64_t h2 = 0;
        int64_t  expireNs = 0;
        uint32_t epoch = 0;
    };

    static inline uint64_t Mix64(uint64_t k) noexcept {
        k ^= k >> 33; k *= 0xFF51AFD7ED558CCDULL;
        k ^= k >> 33; k *= 0xC4CEB9FE1A85EC53ULL;
        k ^= k >> 33; return k;
    }

    static bool IsCacheableReadOnlyAssetPath(const char* raw, uint64_t* outH1, uint64_t* outH2) noexcept {
        if (!raw || raw[0] == '\0') return false;
        char norm[512];
        size_t len = 0;
        while (raw[len] != '\0') {
            if (len + 1 >= sizeof(norm)) return false;
            char c = raw[len];
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
            norm[len++] = c;
        }
        norm[len] = '\0';

        // Never cache world saves or cluster state paths
        auto hasSub = [&](const char* needle, size_t nlen) {
            if (len < nlen) return false;
            for (size_t i = 0; i <= len - nlen; ++i) {
                if (norm[i] == needle[0] && std::memcmp(norm + i, needle, nlen) == 0) return true;
            }
            return false;
        };
        if (!hasSub("/mods/", 6) && !hasSub("/data/", 6) &&
            std::strncmp(norm, "mods/", 5) != 0 && std::strncmp(norm, "data/", 5) != 0) {
            return false;
        }
        if (hasSub("/save/", 6) || std::strncmp(norm, "save/", 5) == 0) return false;

        auto endsExt = [&](const char* ext, size_t elen) {
            return len >= elen && std::memcmp(norm + len - elen, ext, elen) == 0;
        };
        const bool okExt = endsExt(".tex", 4) || endsExt(".xml", 4) || endsExt(".bin", 4) ||
                           endsExt(".zip", 4) || endsExt(".lua", 4) || endsExt(".dyn", 4) ||
                           endsExt(".ksh", 4) || endsExt(".fsb", 4) || endsExt(".fev", 4);
        if (!okExt) return false;

        uint64_t h1 = 0xCBF29CE484222325ULL;
        uint64_t h2 = 0x9E3779B97F4A7C15ULL;
        for (size_t i = 0; i < len; ++i) {
            const uint8_t b = static_cast<uint8_t>(norm[i]);
            h1 ^= b; h1 *= 0x100000001B3ULL;
            h2 = (h2 * 1315423911ULL) ^ (static_cast<uint64_t>(b) + 0x9E3779B97F4A7C15ULL + (h2 << 6) + (h2 >> 2));
        }
        *outH1 = Mix64(h1) | 1ULL;
        *outH2 = Mix64(h2);
        return true;
    }

    bool Lookup(uint64_t h1, uint64_t h2, int64_t nowNs) noexcept {
        pthread_rwlock_rdlock(&rw_);
        const uint32_t curEp = epoch_.load(std::memory_order_relaxed);
        const size_t mask = kCap - 1u;
        const size_t start = static_cast<size_t>(h1) & mask;
        bool hit = false;
        for (size_t step = 0; step < 8; ++step) {
            const Slot& s = table_[(start + step) & mask];
            if (s.epoch != curEp || s.h1 == 0) break;
            if (s.h1 == h1 && s.h2 == h2) {
                hit = (nowNs < s.expireNs);
                break;
            }
        }
        pthread_rwlock_unlock(&rw_);
        return hit;
    }

    void Insert(uint64_t h1, uint64_t h2, int64_t nowNs, int64_t ttlNs) noexcept {
        pthread_rwlock_wrlock(&rw_);
        const uint32_t curEp = epoch_.load(std::memory_order_relaxed);
        const size_t mask = kCap - 1u;
        const size_t start = static_cast<size_t>(h1) & mask;
        size_t target = start;
        for (size_t step = 0; step < 8; ++step) {
            const size_t idx = (start + step) & mask;
            const Slot& s = table_[idx];
            if (s.epoch != curEp || s.h1 == 0 || (s.h1 == h1 && s.h2 == h2) || nowNs >= s.expireNs) {
                target = idx;
                break;
            }
        }
        table_[target] = Slot{h1, h2, nowNs + ttlNs, curEp};
        pthread_rwlock_unlock(&rw_);
    }

    void InvalidateAll() noexcept {
        uint32_t cur = epoch_.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (cur == 0) {
            pthread_rwlock_wrlock(&rw_);
            std::memset(table_, 0, sizeof(table_));
            epoch_.store(1, std::memory_order_release);
            pthread_rwlock_unlock(&rw_);
        }
    }

private:
    pthread_rwlock_t rw_ = PTHREAD_RWLOCK_INITIALIZER;
    std::atomic<uint32_t> epoch_{1};
    Slot table_[kCap] = {};
};

PosixFsNegCache& GetFsNegCache() noexcept {
    alignas(PosixFsNegCache) static uint8_t buf[sizeof(PosixFsNegCache)];
    static PosixFsNegCache* ptr = new (buf) PosixFsNegCache();
    return *ptr;
}
std::atomic<bool> g_fsNegCacheActive{false};

// =============================================================================
// 7. Module 8: Bounded Strict-FIFO POSIX Async Atomic Save-Commit Queue
// =============================================================================
constexpr uintptr_t kAsyncSentinelAddr = 0x4843534156455054ULL;
inline FILE* GetAsyncSentinelFp() noexcept { return reinterpret_cast<FILE*>(kAsyncSentinelAddr); }
inline bool IsAsyncSentinelFp(FILE* fp) noexcept { return reinterpret_cast<uintptr_t>(fp) == kAsyncSentinelAddr; }

struct AsyncCaptureState {
    bool        active = false;
    const char* path   = nullptr;
    const void* data   = nullptr;
    uint32_t    size   = 0;
};
thread_local AsyncCaptureState tl_asyncCapture;

class PosixAsyncSaveQueue {
public:
    static constexpr size_t kMaxPendingItems = 1024u;
    static constexpr size_t kMaxQueuedBytes  = 64u * 1024u * 1024u;

    struct Item {
        uint64_t seq = 0;
        char*    rawPath = nullptr;
        char*    normPath = nullptr;
        char*    dirPath = nullptr;
        uint8_t* bytes = nullptr;
        size_t   len = 0;
        Item*    next = nullptr;
    };

    static void NormalizePosixPath(const char* in, char* out, size_t cap) noexcept {
        if (!in || !*in || cap < 2) { if (cap) out[0] = '\0'; return; }
        char combined[2048];
        if (in[0] == '/') {
            std::snprintf(combined, sizeof(combined), "%s", in);
        } else {
            char cwd[1024];
            if (!getcwd(cwd, sizeof(cwd))) cwd[0] = '\0';
            std::snprintf(combined, sizeof(combined), "%s/%s", cwd, in);
        }
        // Collapse //, /./, and lexical /.. components
        size_t outLen = 0;
        out[outLen++] = '/';
        const char* p = combined;
        while (*p) {
            while (*p == '/') ++p;
            if (!*p) break;
            const char* seg = p;
            while (*p && *p != '/') ++p;
            const size_t slen = static_cast<size_t>(p - seg);
            if (slen == 1 && seg[0] == '.') continue;
            if (slen == 2 && seg[0] == '.' && seg[1] == '.') {
                if (outLen > 1) {
                    --outLen;
                    while (outLen > 1 && out[outLen - 1] != '/') --outLen;
                }
                continue;
            }
            if (outLen > 1 && outLen + 1 < cap) out[outLen++] = '/';
            if (outLen + slen >= cap) break;
            std::memcpy(out + outLen, seg, slen);
            outLen += slen;
        }
        if (outLen > 1 && out[outLen - 1] == '/') --outLen;
        out[outLen] = '\0';
    }

    static bool WriteAllAndFsync(const char* tmpPath, const uint8_t* data, size_t len) noexcept {
        const int fd = g_real_open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) return false;
        size_t written = 0;
        bool ok = true;
        while (written < len) {
            const ssize_t w = write(fd, data + written, len - written);
            if (w < 0) {
                if (errno == EINTR) continue;
                ok = false;
                break;
            }
            written += static_cast<size_t>(w);
        }
        if (ok) {
            (void)fsync(fd);
        }
        close(fd);
        return ok;
    }

    bool Enqueue(const char* path, const void* data, size_t len) noexcept {
        if (!path || !*path || !data || len == 0 || len > kMaxQueuedBytes) return false;
        const int64_t t0 = NowNs();
        char norm[2048];
        NormalizePosixPath(path, norm, sizeof(norm));
        if (norm[0] == '\0') return false;

        Item* it = static_cast<Item*>(std::calloc(1, sizeof(Item)));
        if (!it) return false;
        it->rawPath  = strdup(path);
        it->normPath = strdup(norm);
        const char* lastSlash = std::strrchr(norm, '/');
        if (lastSlash && lastSlash > norm) {
            const size_t dlen = static_cast<size_t>(lastSlash - norm);
            it->dirPath = static_cast<char*>(std::malloc(dlen + 1));
            if (it->dirPath) {
                std::memcpy(it->dirPath, norm, dlen);
                it->dirPath[dlen] = '\0';
            }
        } else {
            it->dirPath = strdup("/");
        }
        it->bytes = static_cast<uint8_t*>(std::malloc(len));
        if (!it->rawPath || !it->normPath || !it->dirPath || !it->bytes) {
            FreeItem(it);
            return false;
        }
        std::memcpy(it->bytes, data, len);
        it->len = len;

        pthread_mutex_lock(&mtx_);
        if (stopped_) {
            pthread_mutex_unlock(&mtx_);
            FreeItem(it);
            return false;
        }
        EnsureWorkerLocked();
        while ((queuedCount_ >= kMaxPendingItems || queuedBytes_ + len > kMaxQueuedBytes) && !stopped_) {
            pthread_cond_wait(&drainedCv_, &mtx_);
        }
        if (stopped_) {
            pthread_mutex_unlock(&mtx_);
            FreeItem(it);
            return false;
        }
        it->seq = ++nextSeq_;
        if (tail_) tail_->next = it;
        else head_ = it;
        tail_ = it;
        ++queuedCount_;
        queuedBytes_ += len;
        hasPending_.store(true, std::memory_order_release);
        enqueuedCount_.fetch_add(1, std::memory_order_relaxed);
        enqueueUsSum_.fetch_add(static_cast<uint64_t>(NsDeltaUs(t0, NowNs())), std::memory_order_relaxed);
        pthread_cond_signal(&workCv_);
        pthread_mutex_unlock(&mtx_);
        return true;
    }

    inline bool HasPendingFast() const noexcept {
        return hasPending_.load(std::memory_order_acquire);
    }

    void FlushPendingForPath(const char* path) noexcept {
        if (!HasPendingFast() || !path || !*path) return;
        char norm[2048];
        NormalizePosixPath(path, norm, sizeof(norm));
        if (!norm[0]) return;
        pthread_mutex_lock(&mtx_);
        while (true) {
            uint64_t maxSeq = 0;
            if (inFlightNorm_ && std::strcmp(inFlightNorm_, norm) == 0) maxSeq = std::max(maxSeq, inFlightSeq_);
            for (const Item* cur = head_; cur; cur = cur->next) {
                if (std::strcmp(cur->normPath, norm) == 0) maxSeq = std::max(maxSeq, cur->seq);
            }
            if (maxSeq == 0) break;
            flushCount_.fetch_add(1, std::memory_order_relaxed);
            while ((inFlightSeq_ > 0 && inFlightSeq_ <= maxSeq) ||
                   (head_ != nullptr && head_->seq <= maxSeq)) {
                pthread_cond_wait(&drainedCv_, &mtx_);
            }
            break;
        }
        pthread_mutex_unlock(&mtx_);
    }

    void FlushPendingForDir(const char* dir) noexcept {
        if (!HasPendingFast() || !dir || !*dir) return;
        char norm[2048];
        NormalizePosixPath(dir, norm, sizeof(norm));
        const size_t nlen = std::strlen(norm);
        if (nlen == 0) return;
        auto matchesDir = [&](const char* itemNorm, const char* itemDir) {
            if (itemDir && std::strcmp(itemDir, norm) == 0) return true;
            if (itemNorm && std::strncmp(itemNorm, norm, nlen) == 0 &&
                (itemNorm[nlen] == '/' || itemNorm[nlen] == '\0')) return true;
            return false;
        };
        pthread_mutex_lock(&mtx_);
        while (true) {
            uint64_t maxSeq = 0;
            if (inFlightSeq_ > 0 && matchesDir(inFlightNorm_, inFlightDir_)) maxSeq = std::max(maxSeq, inFlightSeq_);
            for (const Item* cur = head_; cur; cur = cur->next) {
                if (matchesDir(cur->normPath, cur->dirPath)) maxSeq = std::max(maxSeq, cur->seq);
            }
            if (maxSeq == 0) break;
            flushCount_.fetch_add(1, std::memory_order_relaxed);
            while ((inFlightSeq_ > 0 && inFlightSeq_ <= maxSeq) ||
                   (head_ != nullptr && head_->seq <= maxSeq)) {
                pthread_cond_wait(&drainedCv_, &mtx_);
            }
            break;
        }
        pthread_mutex_unlock(&mtx_);
    }

    void FlushAll() noexcept {
        if (!HasPendingFast()) return;
        pthread_mutex_lock(&mtx_);
        if (head_ != nullptr || inFlightSeq_ != 0) {
            flushCount_.fetch_add(1, std::memory_order_relaxed);
        }
        while (head_ != nullptr || inFlightSeq_ != 0) {
            pthread_cond_wait(&drainedCv_, &mtx_);
        }
        pthread_mutex_unlock(&mtx_);
    }

    void ShutdownAndFlush() noexcept {
        pthread_mutex_lock(&mtx_);
        while (head_ != nullptr || inFlightSeq_ != 0) {
            pthread_cond_wait(&drainedCv_, &mtx_);
        }
        stopped_ = true;
        pthread_cond_broadcast(&workCv_);
        const bool joinable = workerStarted_;
        pthread_t th = worker_;
        workerStarted_ = false;
        pthread_mutex_unlock(&mtx_);
        if (joinable) pthread_join(th, nullptr);
    }

    uint64_t EnqueuedCount() const noexcept { return enqueuedCount_.load(std::memory_order_relaxed); }
    uint64_t CommittedCount() const noexcept { return committedCount_.load(std::memory_order_relaxed); }
    uint64_t CommittedBytes() const noexcept { return committedBytes_.load(std::memory_order_relaxed); }
    uint64_t FlushCount() const noexcept { return flushCount_.load(std::memory_order_relaxed); }
    uint64_t ErrorCount() const noexcept { return errorCount_.load(std::memory_order_relaxed); }
    uint64_t EnqueueUsSum() const noexcept { return enqueueUsSum_.load(std::memory_order_relaxed); }
    uint64_t IoUsSum() const noexcept { return ioUsSum_.load(std::memory_order_relaxed); }

    void ResetTelemetry() noexcept {
        enqueuedCount_ = 0; committedCount_ = 0; committedBytes_ = 0;
        flushCount_ = 0; errorCount_ = 0; enqueueUsSum_ = 0; ioUsSum_ = 0;
    }

private:
    static void FreeItem(Item* it) noexcept {
        if (!it) return;
        std::free(it->rawPath);
        std::free(it->normPath);
        std::free(it->dirPath);
        std::free(it->bytes);
        std::free(it);
    }

    void EnsureWorkerLocked() noexcept {
        if (!workerStarted_) {
            if (pthread_create(&worker_, nullptr, &PosixAsyncSaveQueue::WorkerEntry, this) == 0) {
                workerStarted_ = true;
            }
        }
    }

    static void* WorkerEntry(void* arg) {
        static_cast<PosixAsyncSaveQueue*>(arg)->RunWorker();
        return nullptr;
    }

    void RunWorker() noexcept {
        while (true) {
            pthread_mutex_lock(&mtx_);
            while (!head_ && !stopped_) {
                pthread_cond_wait(&workCv_, &mtx_);
            }
            if (!head_ && stopped_) {
                pthread_mutex_unlock(&mtx_);
                break;
            }
            Item* it = head_;
            head_ = it->next;
            if (!head_) tail_ = nullptr;
            --queuedCount_;
            queuedBytes_ -= it->len;
            inFlightSeq_  = it->seq;
            inFlightNorm_ = it->normPath;
            inFlightDir_  = it->dirPath;
            pthread_mutex_unlock(&mtx_);

            const int64_t io0 = NowNs();
            char tmpPath[2100];
            std::snprintf(tmpPath, sizeof(tmpPath), "%s.hc_tmp", it->normPath);
            bool ok = WriteAllAndFsync(tmpPath, it->bytes, it->len);
            if (ok && g_real_rename) {
                ok = (g_real_rename(tmpPath, it->normPath) == 0);
            }
            if (!ok && g_real_unlink) {
                g_real_unlink(tmpPath);
            }
            ioUsSum_.fetch_add(static_cast<uint64_t>(NsDeltaUs(io0, NowNs())), std::memory_order_relaxed);
            if (ok) {
                committedCount_.fetch_add(1, std::memory_order_relaxed);
                committedBytes_.fetch_add(it->len, std::memory_order_relaxed);
            } else {
                errorCount_.fetch_add(1, std::memory_order_relaxed);
            }

            pthread_mutex_lock(&mtx_);
            inFlightSeq_  = 0;
            inFlightNorm_ = nullptr;
            inFlightDir_  = nullptr;
            if (!head_) {
                hasPending_.store(false, std::memory_order_release);
            }
            pthread_cond_broadcast(&drainedCv_);
            pthread_mutex_unlock(&mtx_);

            FreeItem(it);
        }
    }

    pthread_mutex_t mtx_ = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t  workCv_ = PTHREAD_COND_INITIALIZER;
    pthread_cond_t  drainedCv_ = PTHREAD_COND_INITIALIZER;
    pthread_t       worker_{};
    bool            workerStarted_ = false;
    bool            stopped_ = false;
    Item*           head_ = nullptr;
    Item*           tail_ = nullptr;
    size_t          queuedCount_ = 0;
    size_t          queuedBytes_ = 0;
    uint64_t        nextSeq_ = 0;
    uint64_t        inFlightSeq_ = 0;
    const char*     inFlightNorm_ = nullptr;
    const char*     inFlightDir_  = nullptr;

    std::atomic<bool>     hasPending_{false};
    std::atomic<uint64_t> enqueuedCount_{0};
    std::atomic<uint64_t> committedCount_{0};
    std::atomic<uint64_t> committedBytes_{0};
    std::atomic<uint64_t> flushCount_{0};
    std::atomic<uint64_t> errorCount_{0};
    std::atomic<uint64_t> enqueueUsSum_{0};
    std::atomic<uint64_t> ioUsSum_{0};
};

std::atomic<PosixAsyncSaveQueue*> g_asyncSavePtr{nullptr};
PosixAsyncSaveQueue& GetAsyncSave() noexcept {
    alignas(PosixAsyncSaveQueue) static uint8_t buf[sizeof(PosixAsyncSaveQueue)];
    static PosixAsyncSaveQueue* ptr = []() {
        auto* p = new (buf) PosixAsyncSaveQueue();
        g_asyncSavePtr.store(p, std::memory_order_release);
        return p;
    }();
    return *ptr;
}
std::atomic<bool> g_asyncSaveActive{false};

// =============================================================================
// 8. Lua API Table & Engine Hook Targets
// =============================================================================
LuaApiTable g_lua;

using SetGcBudget_t     = void (*)(float sec);
using TrackLuaMem_t     = void (*)(void* sim, uint32_t osize, uint32_t nsize);
using LuaAlloc_t        = void* (*)(void* ud, void* ptr, size_t osize, size_t nsize);
using LuaNewState_t     = void* (*)(LuaAlloc_t f, void* ud);
using LuaClose_t        = void (*)(void* L);
using LuaOpenLibs_t     = void (*)(void* L);
using LuaGc_t           = int (*)(void* L, int what, int data);
using SimUpdate_t       = uint8_t (*)(void* sim, float dt, uint8_t flag);
using SimLuaUpdate_t    = uint8_t (*)(void* sim, float dt, uint8_t paused);
using CollectGarbage_t  = void (*)(void* sim);
using NetMgrUpdate_t    = void (*)(void* mgr, float dt);
using ReplicaUpdate_t   = void (*)(void* rm);
using LuaCFuncObj_t     = int (*)(void* selfObj, void* L);
using Compress2_t       = int (*)(uint8_t* dest, unsigned long* destLen,
                                  const uint8_t* source, unsigned long sourceLen, int level);
using UnzRead_t         = int (*)(void* file, void* buf, uint32_t len);
using InflateFast_t     = void (*)(void* strm, uint32_t start);
using WritePFile_t      = uint8_t (*)(const char* path, const void* data, int size,
                                      void* cb, uint32_t arg5, void* arg6);
using AStarStep_t       = uint32_t (*)(void* astar, uint32_t maxSteps);
using AStarDtor_t       = void (*)(void* astar);
using PathNodeDtor_t    = void (*)(void* mgr, void* node);
using PathVecRealloc_t  = void (*)(void* vec, void* pos, const fast_astar::TileCoord* coord);
using KleiNew_t         = void* (*)(size_t bytes);
using KleiDelete_t      = void (*)(void* ptr);

SetGcBudget_t    g_setGcBudget        = nullptr;
TrackLuaMem_t    g_trackLuaMem        = nullptr;
uint8_t*         g_simLuaAllocAddr    = nullptr;
uint8_t*         g_wgLuaAllocAddr     = nullptr;
LuaNewState_t    g_origLuaNewState    = nullptr;
LuaClose_t       g_origLuaClose       = nullptr;
LuaOpenLibs_t    g_origLuaOpenLibs    = nullptr;
LuaGc_t          g_origLuaGc          = nullptr;
SimUpdate_t      g_origSimUpdate      = nullptr;
SimLuaUpdate_t   g_origSimLuaUpdate   = nullptr;
CollectGarbage_t g_origCollectGarbage = nullptr;
NetMgrUpdate_t   g_origNetMgrUpdate   = nullptr;
ReplicaUpdate_t  g_origReplicaUpdate  = nullptr;
LuaCFuncObj_t    g_origSimFindEnts    = nullptr;
LuaCFuncObj_t    g_origSimFindEntsReg = nullptr;
LuaCFuncObj_t    g_origSerializeWorld = nullptr;
Compress2_t      g_origCompress2      = nullptr;
UnzRead_t        g_origUnzRead        = nullptr;
WritePFile_t     g_origWritePFile     = nullptr;
AStarStep_t      g_origAStarStep      = nullptr;
AStarDtor_t      g_origAStarDtor0     = nullptr;
AStarDtor_t      g_origAStarDtor1     = nullptr;
PathNodeDtor_t   g_origPathDtor0      = nullptr;
PathVecRealloc_t g_fnPathVecRealloc   = nullptr;
KleiNew_t        g_fnKleiNew          = nullptr;
KleiDelete_t     g_fnKleiDelete       = nullptr;

fast_astar::EngineCallbacks g_astarCb{};
bool g_astarReady = false;
inline fast_astar::StateManager<64u>& GetAStarMgr() noexcept {
    alignas(fast_astar::StateManager<64u>) static uint8_t buf[sizeof(fast_astar::StateManager<64u>)];
    static fast_astar::StateManager<64u>* ptr = new (buf) fast_astar::StateManager<64u>();
    return *ptr;
}

std::atomic<void*>      g_activeSimLuaState{nullptr};
std::atomic<void*>      g_activeSimGlobalState{nullptr};
std::atomic<bool>       g_simStartedForGc{false};
std::atomic<bool>       g_gcPastWarmup{false};
std::atomic<pthread_t>  g_mainSimThread{};
std::atomic<bool>       g_hasMainSimThread{false};
std::atomic<uint32_t>   g_pendingExternalCatchup{0};

anti_stw_gc::State& GetGcPolicyState() noexcept {
    alignas(anti_stw_gc::State) static uint8_t buf[sizeof(anti_stw_gc::State)];
    static anti_stw_gc::State* ptr = new (buf) anti_stw_gc::State();
    return *ptr;
}

inline void* ExtractLuaGlobalState(void* L) noexcept {
    return L ? *reinterpret_cast<void* const*>(static_cast<const uint8_t*>(L) + 0x20) : nullptr;
}

// =============================================================================
// 9. Module Implementations
// =============================================================================

// --- Module 1: Hermetic Mimalloc Lua Allocators ---
void* HyperSimLuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto trackMem = [&]() {
        if (ud && g_trackLuaMem && reinterpret_cast<const uint8_t*>(ud)[0x139] != 0) {
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
    void* out = ptr ? mi_realloc(ptr, nsize) : mi_malloc(nsize);
    if (out) {
        if (ptr) g_stats.mi_reallocs.fetch_add(1, std::memory_order_relaxed);
        else     g_stats.mi_allocs.fetch_add(1, std::memory_order_relaxed);
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

void HookLuaClose(void* L) {
    void* activeL = g_activeSimLuaState.load(std::memory_order_acquire);
    void* activeG = g_activeSimGlobalState.load(std::memory_order_acquire);
    void* closingG = ExtractLuaGlobalState(L);
    if (!L || L == activeL || (activeG && closingG == activeG)) {
        g_simStartedForGc.store(false, std::memory_order_release);
        g_gcPastWarmup.store(false, std::memory_order_release);
        g_activeSimLuaState.store(nullptr, std::memory_order_release);
        g_activeSimGlobalState.store(nullptr, std::memory_order_release);
        g_pendingExternalCatchup.store(0, std::memory_order_release);
    }
    if (g_origLuaClose) g_origLuaClose(L);
}

// --- Module 4B: Lua Globals & FastDump ---
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
    const int64_t t0 = NowNs();
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
    const auto st = fast_dumper::DumpRoot(L, g_lua, 1, varname, varlen, tl_dumpBuf, &failReason);
    const uint64_t us = static_cast<uint64_t>(NsDeltaUs(t0, NowNs()));

    if (st == fast_dumper::Status::Ok && !tl_dumpBuf.oom && tl_dumpBuf.data) {
        g_stats.fastdump_calls.fetch_add(1, std::memory_order_relaxed);
        g_stats.fastdump_us_sum.fetch_add(us, std::memory_order_relaxed);
        g_stats.fastdump_bytes_sum.fetch_add(tl_dumpBuf.len, std::memory_order_relaxed);
        g_lua.pushlstring(L, tl_dumpBuf.data, tl_dumpBuf.len);
        tl_dumpBuf.ShrinkIfLarge();
        return 1;
    }
    tl_dumpBuf.ShrinkIfLarge();
    g_stats.fastdump_fallbacks.fetch_add(1, std::memory_order_relaxed);
    g_lua.pushnil(L);
    g_lua.pushstring(L, failReason ? failReason : (st == fast_dumper::Status::CorruptNumber ? "corrupt_number" : "fallback"));
    return 2;
}

int Lua_HyperCore_GetStats(void* L) {
    if (!g_lua.ReadyForRegister()) return 0;
    double avg = 0, p95 = 0, p99 = 0;
    g_stats.ComputePercentiles(avg, p95, p99);

    g_lua.createtable(L, 0, 52);
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
    setNum("spatial_queries",       static_cast<double>(g_stats.find_entities_calls.load()));
    setNum("spatial_zerocopy_hits", static_cast<double>(g_stats.spatial_zerocopy_sites.load() > 0 ? g_stats.find_entities_calls.load() : 0));
    setNum("spatial_cells_visited", 0.0);
    setNum("light_spatial_queries", 0.0);
    setNum("spatial_zerocopy_sites",static_cast<double>(g_stats.spatial_zerocopy_sites.load()));
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
    setNum("async_save_flushes",    static_cast<double>(GetAsyncSave().FlushCount()));
    setNum("async_save_fallbacks",  static_cast<double>(g_stats.async_save_fallbacks.load()));
    setNum("async_save_errors",     static_cast<double>(GetAsyncSave().ErrorCount()));
    setNum("async_save_enqueue_us_sum", static_cast<double>(GetAsyncSave().EnqueueUsSum()));
    setNum("async_save_io_us_sum",  static_cast<double>(GetAsyncSave().IoUsSum()));

    setBool("opt_mimalloc",         g_cfg.enable_mimalloc);
    setBool("opt_spatial_zerocopy", g_cfg.enable_spatial_zerocopy && g_stats.spatial_zerocopy_sites.load() > 0);
    setBool("opt_adaptive_gc",      g_cfg.enable_adaptive_gc);
    setBool("opt_anti_stw_gc",      g_cfg.enable_anti_stw_gc);
    setBool("opt_libdeflate",       g_cfg.enable_libdeflate);
    setBool("opt_fast_dumper",      g_cfg.enable_fast_dumper);
    setBool("opt_fast_astar",       g_cfg.enable_fast_astar && g_astarReady);
    setBool("opt_fs_neg_cache",     g_cfg.enable_fs_neg_cache && g_fsNegCacheActive.load(std::memory_order_relaxed));
    setBool("opt_async_save",       g_cfg.enable_async_save && g_asyncSaveActive.load(std::memory_order_relaxed));
    setNum("sim_tick_rate",         30.0);
    return 1;
}

int Lua_HyperCore_ResetStats(void* /*L*/) {
    g_stats.Reset();
    GetAsyncSave().ResetTelemetry();
    return 0;
}

int Lua_HyperCore_Version(void* L) {
    if (!g_lua.pushstring) return 0;
    g_lua.pushstring(L, "DST-HyperCore-Server/1.3.0 (Linux x86_64)");
    return 1;
}

int Lua_HyperCore_GetSimTickRate(void* L) {
    if (!g_lua.pushnumber) return 0;
    g_lua.pushnumber(L, 30.0);
    return 1;
}

int Lua_HyperCore_SetSimTickRate(void* L) {
    if (!g_lua.gettop || !g_lua.tonumber || !g_lua.pushboolean) return 0;
    const int n = g_lua.gettop(L);
    const int req = (n >= 1) ? static_cast<int>(g_lua.tonumber(L, 1) + 0.5) : 0;
    g_lua.pushboolean(L, (req == 30) ? 1 : 0);
    return 1;
}

void RegisterHyperCoreLuaGlobals(void* L) {
    if (!L || !g_lua.pushcclosure || !g_lua.setfield) return;
    auto regFn = [&](const char* name, int (*fn)(void*)) {
        g_lua.pushcclosure(L, fn, 0);
        g_lua.setfield(L, HC_LUA_GLOBALSINDEX, name);
    };
    const bool regFastDump = g_lua.ReadyForFastDump();
    const bool regStats    = g_lua.ReadyForRegister();
    const bool regVersion  = (g_lua.pushstring != nullptr);
    const bool regGetSim   = (g_lua.pushnumber != nullptr);
    const bool regSetSim   = (g_lua.gettop != nullptr && g_lua.tonumber != nullptr && g_lua.pushboolean != nullptr);

    if (regFastDump) regFn("HyperCore_FastDump",       &Lua_HyperCore_FastDump);
    if (regStats)    regFn("HyperCore_GetStats",       &Lua_HyperCore_GetStats);
    regFn("HyperCore_ResetStats",                      &Lua_HyperCore_ResetStats);
    if (regVersion)  regFn("HyperCore_Version",        &Lua_HyperCore_Version);
    if (regGetSim)   regFn("HyperCore_GetSimTickRate", &Lua_HyperCore_GetSimTickRate);
    if (regSetSim)   regFn("HyperCore_SetSimTickRate", &Lua_HyperCore_SetSimTickRate);

    LogMsg("[LUA] Registered HyperCore Linux APIs into Lua VM %p (fastDump=%d stats=%d ver=%d sim=30Hz)",
           L, regFastDump ? 1 : 0, regStats ? 1 : 0, regVersion ? 1 : 0);
}

void HookLuaOpenLibs(void* L) {
    g_simStartedForGc.store(false, std::memory_order_release);
    g_gcPastWarmup.store(false, std::memory_order_release);
    g_origLuaOpenLibs(L);
    RegisterHyperCoreLuaGlobals(L);
}

// --- Module 2: Adaptive Slack-Based Incremental GC + Anti-STW Full-GC Shield ---
// Linux Simulation offsets verified from Build 762172 ELF:
//   +0xD8  lua_State* L
//   +0x118 float      gc_budget_sec
//   +0x2B4 int32_t    gc_threshold_kb
int HookLuaGc(void* L, int what, int data) {
    if (!g_origLuaGc) return 0;
    const bool onMain = g_hasMainSimThread.load(std::memory_order_acquire) &&
                        pthread_equal(pthread_self(), g_mainSimThread.load(std::memory_order_relaxed)) != 0;
    const void* activeG = g_activeSimGlobalState.load(std::memory_order_acquire);
    if (what == 2 /*LUA_GCCOLLECT*/ &&
        g_cfg.enable_anti_stw_gc && g_setGcBudget != nullptr && onMain &&
        g_simStartedForGc.load(std::memory_order_acquire) &&
        g_gcPastWarmup.load(std::memory_order_acquire) &&
        L != nullptr && activeG != nullptr && ExtractLuaGlobalState(L) == activeG) {
        const int64_t t0 = NowNs();
        g_setGcBudget(0.0030f);
        g_origLuaGc(L, 5 /*LUA_GCSTEP*/, 0);
        g_pendingExternalCatchup.store(90, std::memory_order_release);
        g_stats.gc_stw_blocked.fetch_add(1, std::memory_order_relaxed);
        const uint64_t us = static_cast<uint64_t>(NsDeltaUs(t0, NowNs()));
        g_stats.gc_us_sum.fetch_add(us, std::memory_order_relaxed);
        g_stats.cur_tick_gc_us += us;
        return 0;
    }
    return g_origLuaGc(L, what, data);
}

void HookCollectGarbage(void* sim) {
    const int64_t t0 = NowNs();
    g_stats.gc_calls.fetch_add(1, std::memory_order_relaxed);

    if (!g_cfg.enable_adaptive_gc || !g_lua.gc || !g_setGcBudget || !sim) {
        g_origCollectGarbage(sim);
        const uint64_t us = static_cast<uint64_t>(NsDeltaUs(t0, NowNs()));
        g_stats.gc_us_sum.fetch_add(us, std::memory_order_relaxed);
        g_stats.cur_tick_gc_us += us;
        AtomicUpdateMax(g_stats.gc_us_max, us);
        return;
    }

    auto* simBytes = reinterpret_cast<uint8_t*>(sim);
    void* L = *reinterpret_cast<void**>(simBytes + 0xD8);
    if (!L) {
        g_origCollectGarbage(sim);
        return;
    }
    g_activeSimLuaState.store(L, std::memory_order_release);
    g_activeSimGlobalState.store(ExtractLuaGlobalState(L), std::memory_order_release);

    const uint32_t extArm = g_pendingExternalCatchup.exchange(0, std::memory_order_acquire);
    if (extArm > 0) GetGcPolicyState().ArmForcedCatchup(extArm);

    int32_t* pThresholdKb = reinterpret_cast<int32_t*>(simBytes + 0x2B4);
    const int32_t curKb = g_lua.gc(L, 3 /*LUA_GCCOUNT*/, 0);
    g_stats.gc_last_kb.store(curKb, std::memory_order_relaxed);

    anti_stw_gc::Config pcfg{};
    pcfg.gc_spare_target_ms  = g_cfg.gc_spare_target_ms;
    pcfg.gc_calm_max_ms      = g_cfg.gc_calm_max_ms;
    pcfg.gc_busy_min_ms      = g_cfg.gc_busy_min_ms;
    pcfg.gc_emergency_add_mb = g_cfg.gc_emergency_add_mb;
    pcfg.enable_anti_stw_gc  = g_cfg.enable_anti_stw_gc;

    auto& gcState = GetGcPolicyState();
    const anti_stw_gc::Decision dec = gcState.Evaluate(
        L, curKb, *pThresholdKb, g_stats.last_tick_non_gc_ms, false, pcfg);
    g_gcPastWarmup.store(gcState.IsPastWarmup(), std::memory_order_release);
    *pThresholdKb = dec.new_threshold_kb;

    if (dec.run_hard_full_gc) {
        g_lua.gc(L, 2 /*LUA_GCCOLLECT*/, 0);
        g_stats.gc_full_count.fetch_add(1, std::memory_order_relaxed);
        const int32_t postKb = g_lua.gc(L, 3 /*LUA_GCCOUNT*/, 0);
        gcState.OnHardFullGcCompleted(postKb);
        *pThresholdKb = std::max<int32_t>(postKb * 2, postKb + g_cfg.gc_emergency_add_mb * 1024);
        g_stats.gc_last_kb.store(postKb, std::memory_order_relaxed);
    }

    g_stats.gc_last_budget_us.store(static_cast<int32_t>(dec.step_budget_ms * 1000.0f), std::memory_order_relaxed);
    g_setGcBudget(dec.step_budget_ms * 0.001f);
    if (g_lua.gc(L, 5 /*LUA_GCSTEP*/, 0) != 0) {
        const int32_t postKb = g_lua.gc(L, 3 /*LUA_GCCOUNT*/, 0);
        gcState.OnCycleCompleted(postKb);
        if (postKb > 0) g_stats.gc_last_kb.store(postKb, std::memory_order_relaxed);
    }

    const uint64_t us = static_cast<uint64_t>(NsDeltaUs(t0, NowNs()));
    g_stats.gc_us_sum.fetch_add(us, std::memory_order_relaxed);
    g_stats.cur_tick_gc_us += us;
    AtomicUpdateMax(g_stats.gc_us_max, us);
}

// --- Module 4A: SIMD libdeflate Replacement for LP64 zlib::compress2 ---
struct TlsDeflateCtx {
    libdeflate_compressor* comp = nullptr;
    int level = 0;
    ~TlsDeflateCtx() { if (comp) libdeflate_free_compressor(comp); }
};
thread_local TlsDeflateCtx tl_deflate;

int HookCompress2(uint8_t* dest, unsigned long* destLen,
                  const uint8_t* source, unsigned long sourceLen, int level) {
    const int64_t t0 = NowNs();
    g_stats.deflate_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.deflate_in_bytes.fetch_add(sourceLen, std::memory_order_relaxed);

    auto recordTiming = [&](uint64_t us) {
        g_stats.deflate_us_sum.fetch_add(us, std::memory_order_relaxed);
        if (g_hasMainSimThread.load(std::memory_order_acquire) &&
            pthread_equal(pthread_self(), g_mainSimThread.load(std::memory_order_relaxed)) != 0) {
            g_stats.deflate_main_calls.fetch_add(1, std::memory_order_relaxed);
            g_stats.deflate_main_us_sum.fetch_add(us, std::memory_order_relaxed);
        }
    };

    if (g_cfg.enable_libdeflate && dest && destLen && source && *destLen > 0) {
        const int targetLevel = g_cfg.libdeflate_level;
        if (!tl_deflate.comp || tl_deflate.level != targetLevel) {
            if (tl_deflate.comp) libdeflate_free_compressor(tl_deflate.comp);
            tl_deflate.comp = libdeflate_alloc_compressor(targetLevel);
            tl_deflate.level = targetLevel;
        }
        if (tl_deflate.comp) {
            const size_t outSz = libdeflate_zlib_compress(tl_deflate.comp, source, sourceLen, dest, *destLen);
            if (outSz > 0 && outSz <= *destLen) {
                *destLen = static_cast<unsigned long>(outSz);
                g_stats.deflate_out_bytes.fetch_add(outSz, std::memory_order_relaxed);
                recordTiming(static_cast<uint64_t>(NsDeltaUs(t0, NowNs())));
                return 0;
            }
        }
    }

    const int rc = g_origCompress2(dest, destLen, source, sourceLen, level);
    if (rc == 0 && destLen) g_stats.deflate_out_bytes.fetch_add(*destLen, std::memory_order_relaxed);
    recordTiming(static_cast<uint64_t>(NsDeltaUs(t0, NowNs())));
    return rc;
}

// --- Module 4C: Linux LP64 FastLoad (unzReadCurrentFile + 64-bit inflate_fast) ---
using ZReadFileFunc_t = uint64_t (*)(void* opaque, void* stream, void* buf, uint64_t size);
using ZSeekFileFunc_t = long (*)(void* opaque, void* stream, uint64_t offset, int origin);

struct LinuxUnzReadInfo {
    void*           read_buffer;            // +0x000
    const uint8_t*  next_in;                // +0x008
    uint32_t        avail_in;               // +0x010
    uint32_t        pad_ai;                 // +0x014
    uint64_t        total_in;               // +0x018
    uint8_t*        next_out;               // +0x020
    uint32_t        avail_out;              // +0x028
    uint32_t        pad_ao;                 // +0x02C
    uint64_t        total_out;              // +0x030
    uint8_t         pad_stream[0x40];       // +0x038..+0x077
    uint64_t        pos_in_zipfile;         // +0x078
    uint8_t         pad_mid[0x30];          // +0x080..+0x0AF
    uint64_t        rest_read_compressed;   // +0x0B0
    uint64_t        rest_read_uncompressed; // +0x0B8
    uint8_t         pad_io1[0x10];          // +0x0C0..+0x0CF
    ZReadFileFunc_t zread_file;             // +0x0D0
    uint8_t         pad_io2[0x10];          // +0x0D8..+0x0E7
    ZSeekFileFunc_t zseek_file;             // +0x0E8
    uint8_t         pad_io3[0x10];          // +0x0F0..+0x0FF
    void*           opaque;                 // +0x100
    void*           filestream;             // +0x108
    uint64_t        compression_method;     // +0x110
    uint64_t        byte_before_zipfile;    // +0x118
    int32_t         raw;                    // +0x120
};
static_assert(offsetof(LinuxUnzReadInfo, pos_in_zipfile) == 0x78);
static_assert(offsetof(LinuxUnzReadInfo, rest_read_compressed) == 0xB0);
static_assert(offsetof(LinuxUnzReadInfo, rest_read_uncompressed) == 0xB8);
static_assert(offsetof(LinuxUnzReadInfo, zread_file) == 0xD0);
static_assert(offsetof(LinuxUnzReadInfo, zseek_file) == 0xE8);
static_assert(offsetof(LinuxUnzReadInfo, opaque) == 0x100);
static_assert(offsetof(LinuxUnzReadInfo, compression_method) == 0x110);
static_assert(offsetof(LinuxUnzReadInfo, byte_before_zipfile) == 0x118);
static_assert(offsetof(LinuxUnzReadInfo, raw) == 0x120);

struct TlsDecompCtx {
    libdeflate_decompressor* decomp = nullptr;
    uint8_t* compBuf = nullptr;
    size_t   compCap = 0;
    ~TlsDecompCtx() {
        if (decomp) libdeflate_free_decompressor(decomp);
        std::free(compBuf);
    }
    bool Ensure(size_t want) noexcept {
        if (want <= compCap) return true;
        uint8_t* p = static_cast<uint8_t*>(std::realloc(compBuf, want));
        if (!p) return false;
        compBuf = p;
        compCap = want;
        return true;
    }
};
thread_local TlsDecompCtx tl_decomp;

int HookUnzReadCurrentFile(void* file, void* buf, uint32_t len) {
    if (!file) return -102;
    auto* pinfo = *reinterpret_cast<LinuxUnzReadInfo**>(static_cast<uint8_t*>(file) + 0x128);
    if (!pinfo || !pinfo->read_buffer || len == 0 || !buf) {
        return g_origUnzRead(file, buf, len);
    }

    const uint64_t compSize = pinfo->rest_read_compressed;
    const uint64_t uncompSize = pinfo->rest_read_uncompressed;

    if (pinfo->compression_method == 8 && pinfo->raw == 0 &&
        pinfo->avail_in == 0 && pinfo->total_out == 0 &&
        compSize > 0 && compSize <= (64ULL * 1024ULL * 1024ULL) &&
        uncompSize > 0 && uncompSize <= static_cast<uint64_t>(INT32_MAX) &&
        len >= uncompSize && pinfo->zseek_file && pinfo->zread_file) {
        const int64_t t0 = NowNs();
        if (!tl_decomp.decomp) tl_decomp.decomp = libdeflate_alloc_decompressor();
        if (tl_decomp.decomp && tl_decomp.Ensure(static_cast<size_t>(compSize))) {
            const uint64_t offset = pinfo->pos_in_zipfile + pinfo->byte_before_zipfile;
            if (pinfo->zseek_file(pinfo->opaque, pinfo->filestream, offset, 0) == 0 &&
                pinfo->zread_file(pinfo->opaque, pinfo->filestream, tl_decomp.compBuf, compSize) == compSize) {
                size_t actualOut = 0;
                if (libdeflate_deflate_decompress(tl_decomp.decomp, tl_decomp.compBuf, static_cast<size_t>(compSize),
                                                  buf, static_cast<size_t>(uncompSize), &actualOut) == LIBDEFLATE_SUCCESS &&
                    actualOut == static_cast<size_t>(uncompSize)) {
                    pinfo->pos_in_zipfile += compSize;
                    pinfo->rest_read_compressed = 0;
                    pinfo->rest_read_uncompressed = 0;
                    pinfo->next_out = static_cast<uint8_t*>(buf) + uncompSize;
                    pinfo->avail_out = 0;
                    pinfo->total_out += uncompSize;
                    g_stats.unz_hits.fetch_add(1, std::memory_order_relaxed);
                    g_stats.unz_bytes.fetch_add(uncompSize, std::memory_order_relaxed);
                    g_stats.unz_us_sum.fetch_add(static_cast<uint64_t>(NsDeltaUs(t0, NowNs())), std::memory_order_relaxed);
                    return static_cast<int>(uncompSize);
                }
            }
        }
    } else if ((pinfo->compression_method == 0 || pinfo->raw != 0) &&
               pinfo->avail_in == 0 && uncompSize > 0 && uncompSize <= static_cast<uint64_t>(INT32_MAX) &&
               uncompSize == compSize && len >= uncompSize &&
               pinfo->zseek_file && pinfo->zread_file) {
        const uint64_t offset = pinfo->pos_in_zipfile + pinfo->byte_before_zipfile;
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

struct LinuxZlibCode { uint8_t op; uint8_t bits; uint16_t val; };
struct LinuxZlibInflateState {
    int32_t mode;                  // +0x00
    uint8_t pad0[0x30];            // +0x04..+0x33
    uint32_t wsize;                // +0x34
    uint32_t whave;                // +0x38
    uint32_t write;                // +0x3C
    uint8_t* window;               // +0x40
    uint64_t hold;                 // +0x48
    uint32_t bits;                 // +0x50
    uint32_t pad1;                 // +0x54
    void*    pad2;                 // +0x58
    const LinuxZlibCode* lcode;    // +0x60
    const LinuxZlibCode* dcode;    // +0x68
    uint32_t lenbits;              // +0x70
    uint32_t distbits;             // +0x74
};
struct LinuxZlibStream {
    const uint8_t* next_in;        // +0x00
    uint32_t avail_in;             // +0x08
    uint32_t pad0;                 // +0x0C
    uint64_t total_in;             // +0x10
    uint8_t* next_out;             // +0x18
    uint32_t avail_out;            // +0x20
    uint32_t pad1;                 // +0x24
    uint64_t total_out;            // +0x28
    const char* msg;               // +0x30
    LinuxZlibInflateState* state;  // +0x38
};
static_assert(offsetof(LinuxZlibInflateState, wsize) == 0x34);
static_assert(offsetof(LinuxZlibInflateState, hold) == 0x48);
static_assert(offsetof(LinuxZlibInflateState, bits) == 0x50);
static_assert(offsetof(LinuxZlibInflateState, lcode) == 0x60);
static_assert(offsetof(LinuxZlibInflateState, distbits) == 0x74);
static_assert(offsetof(LinuxZlibStream, msg) == 0x30);
static_assert(offsetof(LinuxZlibStream, state) == 0x38);

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

void HookInflateFast(LinuxZlibStream* strm, uint32_t start) {
    LinuxZlibInflateState* state = strm->state;
    const uint8_t* in = strm->next_in;
    const uint8_t* in_end = in + strm->avail_in;
    const uint8_t* last = in + (strm->avail_in - 5);
    uint8_t* out = strm->next_out;
    uint8_t* out_end = out + strm->avail_out;
    uint8_t* beg = out - (start - strm->avail_out);
    uint8_t* end = out + (strm->avail_out - 257);

    const uint32_t wsize = state->wsize, whave = state->whave, write = state->write;
    uint8_t* window = state->window;
    uint64_t hold = state->hold;
    uint32_t bits = state->bits;
    const LinuxZlibCode* lcode = state->lcode;
    const LinuxZlibCode* dcode = state->dcode;
    const uint64_t lmask = (1ULL << state->lenbits) - 1;
    const uint64_t dmask = (1ULL << state->distbits) - 1;

    do {
        if (bits < 15) {
            uint32_t chunk; std::memcpy(&chunk, in, 4);
            hold |= static_cast<uint64_t>(chunk) << bits;
            in += 4; bits += 32;
        }
        LinuxZlibCode here = lcode[hold & lmask];
    dolen:
        uint32_t op = here.bits;
        hold >>= op; bits -= op; op = here.op;
        if (op == 0) {
            *out++ = static_cast<uint8_t>(here.val);
        } else if (op & 16) {
            uint32_t len = here.val;
            op &= 15;
            if (op) {
                if (bits < op && in < in_end) { hold |= static_cast<uint64_t>(*in++) << bits; bits += 8; }
                len += static_cast<uint32_t>(hold & ((1ULL << op) - 1));
                hold >>= op; bits -= op;
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
                const uint32_t avail_back = static_cast<uint32_t>(out - beg);
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

    const uint32_t extra_bytes = bits >> 3;
    in -= extra_bytes;
    bits -= extra_bytes << 3;
    hold &= (1ULL << bits) - 1;
    strm->next_in = in;
    strm->next_out = out;
    strm->avail_in = static_cast<uint32_t>(in_end - in);
    strm->avail_out = static_cast<uint32_t>(out_end - out);
    state->hold = hold;
    state->bits = bits;
}

// --- Module 6: O(N log N) FastAStar Engine ---
// Linux AStar layout (Build 762172):
//   +0x08 int32_t status
//   +0x10..+0x20 open_list (std::vector<Node*>)
//   +0x28..+0x38 closed_list (std::vector<Node*>)
//   +0x40 void* ctx (ctx + 0x3C = TileCoord goal)
//   +0x48..+0x58 path_vec (std::vector<TileCoord>)
//   +0x60 float elapsed_sec
void AStarClearPath(void* pathVec) {
    if (!pathVec) return;
    auto** pv = reinterpret_cast<fast_astar::TileCoord**>(pathVec);
    pv[1] = pv[0]; // end = begin
}

void AStarPushPath(void* pathVec, const fast_astar::TileCoord* pos) {
    if (!pathVec || !pos) return;
    auto** pv = reinterpret_cast<fast_astar::TileCoord**>(pathVec);
    if (pv[1] < pv[2]) {
        *pv[1] = *pos;
        pv[1] += 1;
    } else if (g_fnPathVecRealloc) {
        g_fnPathVecRealloc(pathVec, pv[1], pos);
    }
}

void AStarReserveNeighbors(fast_astar::NeighborVec* vec, uint64_t count) {
    if (!vec || !g_fnKleiNew || count == 0) return;
    auto* buf = static_cast<fast_astar::NeighborEntry*>(
        g_fnKleiNew(static_cast<size_t>(count) * sizeof(fast_astar::NeighborEntry)));
    if (buf) {
        vec->begin = buf;
        vec->end   = buf;
        vec->cap   = buf + count;
    }
}

uint32_t HookAStarStep(void* astar, uint32_t maxSteps) {
    if (!astar) return 0u;
    auto* ab = reinterpret_cast<uint8_t*>(astar);
    if (*reinterpret_cast<int32_t*>(ab + 0x08) != 0) return 0u;

    auto& mgr = GetAStarMgr();
    if (!g_cfg.enable_fast_astar || !g_astarReady) {
        mgr.DetachAndRecycle(astar);
        return g_origAStarStep ? g_origAStarStep(astar, maxSteps) : 0u;
    }

    auto** openBeg   = *reinterpret_cast<uint8_t***>(ab + 0x10);
    auto** openEnd   = *reinterpret_cast<uint8_t***>(ab + 0x18);
    auto** closedBeg = *reinterpret_cast<uint8_t***>(ab + 0x28);
    auto** closedEnd = *reinterpret_cast<uint8_t***>(ab + 0x30);
    const float elapsed = *reinterpret_cast<float*>(ab + 0x60);

    fast_astar::StateManager<64u>::StateHandle handle;
    const bool isFresh = (elapsed == 0.0f && closedBeg == closedEnd &&
                          openBeg != nullptr && openEnd == openBeg + 1 && openBeg[0] != nullptr);
    if (isFresh) {
        handle = mgr.AcquireAndBind(astar);
        if (handle) {
            fast_astar::TileCoord start{};
            std::memcpy(&start, openBeg[0] + 0x08, sizeof(fast_astar::TileCoord));
            void* ctx = *reinterpret_cast<void**>(ab + 0x40);
            if (!ctx) {
                handle.Release();
                mgr.DetachAndRecycle(astar);
                *reinterpret_cast<int32_t*>(ab + 0x08) = 2;
                return 0u;
            }
            if (g_fnKleiDelete) {
                g_fnKleiDelete(openBeg[0]);
                openBeg[0] = nullptr;
                *reinterpret_cast<uint8_t***>(ab + 0x18) = openBeg;
            }
            const float h = g_astarCb.heuristic(ctx, &start);
            handle->Init(start, h);
            g_stats.astar_inits.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        handle = mgr.AcquireRef(astar);
    }

    if (!handle) {
        g_stats.astar_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return g_origAStarStep ? g_origAStarStep(astar, maxSteps) : 0u;
    }

    auto* slotPtr = handle.Slot();
    const uint64_t slotGen = handle.Gen();
    void* ctx = *reinterpret_cast<void**>(ab + 0x40);
    if (!ctx) {
        handle.Release();
        mgr.DetachIfMatch(astar, slotPtr, slotGen);
        *reinterpret_cast<int32_t*>(ab + 0x08) = 2;
        return 0u;
    }

    fast_astar::TileCoord goal{};
    std::memcpy(&goal, static_cast<const uint8_t*>(ctx) + 0x3C, sizeof(fast_astar::TileCoord));

    const int64_t t0 = NowNs();
    int32_t outStatus = 0;
    const uint32_t steps = handle->Step(ctx, goal, maxSteps, g_astarCb, ab + 0x48, outStatus);
    const double us = NsDeltaUs(t0, NowNs());

    *reinterpret_cast<float*>(ab + 0x60) += static_cast<float>(us * 1e-6);
    g_stats.astar_steps.fetch_add(steps, std::memory_order_relaxed);
    g_stats.astar_us_sum.fetch_add(static_cast<uint64_t>(us), std::memory_order_relaxed);

    if (outStatus != 0) {
        *reinterpret_cast<int32_t*>(ab + 0x08) = outStatus;
        handle.Release();
        mgr.DetachIfMatch(astar, slotPtr, slotGen);
        if (outStatus == 1) g_stats.astar_completed.fetch_add(1, std::memory_order_relaxed);
        else                g_stats.astar_failed.fetch_add(1, std::memory_order_relaxed);
    }
    return steps;
}

void HookAStarDtor0(void* astar) {
    if (astar) GetAStarMgr().DetachAndRecycle(astar);
    if (g_origAStarDtor0) g_origAStarDtor0(astar);
}
void HookAStarDtor1(void* astar) {
    if (astar) GetAStarMgr().DetachAndRecycle(astar);
    if (g_origAStarDtor1) g_origAStarDtor1(astar);
}
void HookPathNodeDtor0(void* mgr, void* node) {
    if (node) GetAStarMgr().DetachAndRecycle(static_cast<uint8_t*>(node) + 0x80);
    if (g_origPathDtor0) g_origPathDtor0(mgr, node);
}

// --- Module 0A/0B: Simulation & Network Sub-Phase Profilers ---
void LogTelemetrySummary(const char* tag) {
    double avg = 0, p95 = 0, p99 = 0;
    g_stats.ComputePercentiles(avg, p95, p99);
    const uint64_t ticks = g_stats.ticks_total.load();
    const uint64_t feCalls = g_stats.find_entities_calls.load();
    const uint64_t feUs = g_stats.find_entities_us_sum.load();
    const uint64_t gcUs = g_stats.gc_us_sum.load();

    LogMsg("[%s] ticks=%llu | tick_us(avg=%.0f p95=%.0f p99=%.0f max=%llu overruns=%llu) | "
           "GC(us/t=%.0f max=%llu full=%llu stwBlk=%llu kb=%d) | FindEnts(calls=%llu us/t=%.0f zcSites=%llu) | "
           "AStar(inits=%llu steps=%llu us=%llu ok=%llu fail=%llu) | "
           "FastDump(calls=%llu ms=%.1f MB=%.2f fb=%llu) | Ser(calls=%llu ms=%.1f) | "
           "Deflate(calls=%llu main=%llu ms=%.1f in=%.2fMB out=%.2fMB) | FastLoad(unz=%llu MB=%.1f ms=%.1f)",
           tag, static_cast<unsigned long long>(ticks), avg, p95, p99,
           static_cast<unsigned long long>(g_stats.tick_us_max.load()),
           static_cast<unsigned long long>(g_stats.tick_overrun_count.load()),
           ticks ? static_cast<double>(gcUs) / ticks : 0.0,
           static_cast<unsigned long long>(g_stats.gc_us_max.load()),
           static_cast<unsigned long long>(g_stats.gc_full_count.load()),
           static_cast<unsigned long long>(g_stats.gc_stw_blocked.load()),
           g_stats.gc_last_kb.load(),
           static_cast<unsigned long long>(feCalls),
           ticks ? static_cast<double>(feUs) / ticks : 0.0,
           static_cast<unsigned long long>(g_stats.spatial_zerocopy_sites.load()),
           static_cast<unsigned long long>(g_stats.astar_inits.load()),
           static_cast<unsigned long long>(g_stats.astar_steps.load()),
           static_cast<unsigned long long>(g_stats.astar_us_sum.load()),
           static_cast<unsigned long long>(g_stats.astar_completed.load()),
           static_cast<unsigned long long>(g_stats.astar_failed.load()),
           static_cast<unsigned long long>(g_stats.fastdump_calls.load()),
           g_stats.fastdump_us_sum.load() / 1000.0,
           g_stats.fastdump_bytes_sum.load() / (1024.0 * 1024.0),
           static_cast<unsigned long long>(g_stats.fastdump_fallbacks.load()),
           static_cast<unsigned long long>(g_stats.serialize_world_calls.load()),
           g_stats.serialize_world_us_sum.load() / 1000.0,
           static_cast<unsigned long long>(g_stats.deflate_calls.load()),
           static_cast<unsigned long long>(g_stats.deflate_main_calls.load()),
           g_stats.deflate_us_sum.load() / 1000.0,
           g_stats.deflate_in_bytes.load() / (1024.0 * 1024.0),
           g_stats.deflate_out_bytes.load() / (1024.0 * 1024.0),
           static_cast<unsigned long long>(g_stats.unz_hits.load()),
           g_stats.unz_bytes.load() / (1024.0 * 1024.0),
           g_stats.unz_us_sum.load() / 1000.0);

    const uint64_t luaC = g_stats.prof_lua_update_calls.load();
    const uint64_t netC = g_stats.prof_net_update_calls.load();
    const uint64_t repC = g_stats.prof_replica_update_calls.load();
    auto& asq = GetAsyncSave();
    LogMsg("[%s-SUB] luaUpdate(c=%llu avg=%.0fus max=%llu) | netUpdate(c=%llu avg=%.0fus max=%llu) | "
           "repUpdate(c=%llu avg=%.0fus max=%llu) | FsNeg(hits=%llu misses=%llu) | "
           "AsyncSave(enq=%llu ok=%llu bytes=%llu flush=%llu fb=%llu err=%llu enqUs=%llu ioMs=%.1f)",
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
           static_cast<unsigned long long>(g_stats.fs_neg_hits.load()),
           static_cast<unsigned long long>(g_stats.fs_neg_misses.load()),
           static_cast<unsigned long long>(asq.EnqueuedCount()),
           static_cast<unsigned long long>(asq.CommittedCount()),
           static_cast<unsigned long long>(asq.CommittedBytes()),
           static_cast<unsigned long long>(asq.FlushCount()),
           static_cast<unsigned long long>(g_stats.async_save_fallbacks.load()),
           static_cast<unsigned long long>(asq.ErrorCount()),
           static_cast<unsigned long long>(asq.EnqueueUsSum()),
           asq.IoUsSum() / 1000.0);
}

uint8_t HookSimUpdate(void* sim, float dt, uint8_t flag) {
    if (!g_hasMainSimThread.load(std::memory_order_acquire)) {
        g_mainSimThread.store(pthread_self(), std::memory_order_release);
        g_hasMainSimThread.store(true, std::memory_order_release);
    }
    if (!g_simStartedForGc.load(std::memory_order_relaxed)) {
        g_simStartedForGc.store(true, std::memory_order_release);
    }
    g_stats.cur_tick_gc_us = 0;
    const int64_t t0 = NowNs();
    const uint8_t ret = g_origSimUpdate(sim, dt, flag);
    const uint64_t totalUs = static_cast<uint64_t>(NsDeltaUs(t0, NowNs()));

    const uint64_t gcUs = g_stats.cur_tick_gc_us;
    const uint64_t nonGcUs = (totalUs > gcUs) ? (totalUs - gcUs) : 0;
    g_stats.last_tick_non_gc_ms = g_stats.last_tick_non_gc_ms * 0.35 + (static_cast<double>(nonGcUs) * 0.001) * 0.65;

    const uint64_t ticks = g_stats.ticks_total.fetch_add(1, std::memory_order_relaxed) + 1;
    g_stats.tick_us_sum.fetch_add(totalUs, std::memory_order_relaxed);
    if (totalUs > 33333ULL) g_stats.tick_overrun_count.fetch_add(1, std::memory_order_relaxed);
    AtomicUpdateMax(g_stats.tick_us_max, totalUs);
    g_stats.PushTickSample(static_cast<uint32_t>(std::min<uint64_t>(totalUs, UINT32_MAX)));

    if (g_cfg.enable_profiler && g_cfg.log_interval_ticks > 0 &&
        (ticks % static_cast<uint64_t>(g_cfg.log_interval_ticks) == 0)) {
        LogTelemetrySummary("PROF");
    }
    return ret;
}

uint8_t HookSimLuaUpdate(void* sim, float dt, uint8_t paused) {
    const int64_t t0 = NowNs();
    const uint8_t rc = g_origSimLuaUpdate(sim, dt, paused);
    const uint64_t us = static_cast<uint64_t>(NsDeltaUs(t0, NowNs()));
    g_stats.prof_lua_update_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.prof_lua_update_us_sum.fetch_add(us, std::memory_order_relaxed);
    AtomicUpdateMax(g_stats.prof_lua_update_us_max, us);
    return rc;
}

void HookNetMgrUpdate(void* mgr, float dt) {
    const int64_t t0 = NowNs();
    g_origNetMgrUpdate(mgr, dt);
    const uint64_t us = static_cast<uint64_t>(NsDeltaUs(t0, NowNs()));
    g_stats.prof_net_update_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.prof_net_update_us_sum.fetch_add(us, std::memory_order_relaxed);
    AtomicUpdateMax(g_stats.prof_net_update_us_max, us);
}

void HookReplicaMgr3Update(void* rm) {
    const int64_t t0 = NowNs();
    g_origReplicaUpdate(rm);
    const uint64_t us = static_cast<uint64_t>(NsDeltaUs(t0, NowNs()));
    g_stats.prof_replica_update_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.prof_replica_update_us_sum.fetch_add(us, std::memory_order_relaxed);
    AtomicUpdateMax(g_stats.prof_replica_update_us_max, us);
}

int HookSimFindEntities(void* selfObj, void* L) {
    const int64_t t0 = NowNs();
    const int rc = g_origSimFindEnts(selfObj, L);
    g_stats.find_entities_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.find_entities_us_sum.fetch_add(static_cast<uint64_t>(NsDeltaUs(t0, NowNs())), std::memory_order_relaxed);
    return rc;
}

int HookSimFindEntitiesReg(void* selfObj, void* L) {
    const int64_t t0 = NowNs();
    const int rc = g_origSimFindEntsReg(selfObj, L);
    g_stats.find_entities_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.find_entities_us_sum.fetch_add(static_cast<uint64_t>(NsDeltaUs(t0, NowNs())), std::memory_order_relaxed);
    return rc;
}

int HookSerializeWorldSession(void* selfObj, void* L) {
    const int64_t t0 = NowNs();
    const int rc = g_origSerializeWorld(selfObj, L);
    g_stats.serialize_world_calls.fetch_add(1, std::memory_order_relaxed);
    g_stats.serialize_world_us_sum.fetch_add(static_cast<uint64_t>(NsDeltaUs(t0, NowNs())), std::memory_order_relaxed);
    return rc;
}

// --- Module 3: 4-Site In-Place SpatialGrid Zero-Copy W^X Patches ---
int ApplySpatialGridZeroCopyPatches() noexcept {
    if (!g_cfg.enable_spatial_zerocopy) return 0;
    int patched = 0;

    // Helper: nop out 5-byte `call KleiDelete` cleanly
    const uint8_t nop5[5]  = {0x0F, 0x1F, 0x44, 0x00, 0x00};
    const uint8_t nop11[11]= {0x66, 0x0F, 0x1F, 0x44, 0x00, 0x00, 0x0F, 0x1F, 0x44, 0x00, 0x00};

    // Site 0: EntityManager::FindEntities (rbp = cell->ent_begin, skip KleiNew+memmove+KleiDelete)
    if (uint8_t* a = FindUniqueAob(kAobSpatialAnchorFindEnts)) {
        if (uint8_t* d = FindUniqueAob(kAobSpatialDelFindEnts)) {
            const uint8_t p1[22] = {
                0x48, 0x8B, 0x6B, 0x08,       // mov rbp, [rbx+0x08]
                0x48, 0x89, 0xEE,             // mov rsi, rbp
                0x48, 0x8B, 0x43, 0x10,       // mov rax, [rbx+0x10]
                0x48, 0x89, 0xC7,             // mov rdi, rax
                0x48, 0x29, 0xF7,             // sub rdi, rsi
                0x0F, 0x1F, 0x44, 0x00, 0x00  // nop 5
            };
            if (PatchTextBytesWX(a + 6, p1, sizeof(p1)) &&
                PatchTextBytesWX(a + 50, nop11, sizeof(nop11)) &&
                PatchTextBytesWX(d + 8, nop5, sizeof(nop5))) {
                ++patched;
            }
        }
    }

    // Site 1: EntityManager::FindEntities_Registered (r13 = cell->ent_begin)
    if (uint8_t* a = FindUniqueAob(kAobSpatialAnchorFindReg)) {
        if (uint8_t* d = FindUniqueAob(kAobSpatialDelFindReg)) {
            const uint8_t p1[22] = {
                0x4C, 0x8B, 0x6B, 0x08,       // mov r13, [rbx+0x08]
                0x4C, 0x89, 0xEE,             // mov rsi, r13
                0x48, 0x8B, 0x43, 0x10,       // mov rax, [rbx+0x10]
                0x48, 0x89, 0xC7,             // mov rdi, rax
                0x48, 0x29, 0xF7,             // sub rdi, rsi
                0x0F, 0x1F, 0x44, 0x00, 0x00  // nop 5
            };
            if (PatchTextBytesWX(a + 6, p1, sizeof(p1)) &&
                PatchTextBytesWX(a + 50, nop11, sizeof(nop11)) &&
                PatchTextBytesWX(d + 8, nop5, sizeof(nop5))) {
                ++patched;
            }
        }
    }

    // Site 2: LightWatcher::Intensity (r14 = cell->ent_begin, 2 delete calls)
    if (uint8_t* a = FindUniqueAob(kAobSpatialAnchorLightInt)) {
        uint8_t* d1 = FindUniqueAob(kAobSpatialDel1LightInt);
        uint8_t* d2 = FindUniqueAob(kAobSpatialDel2LightInt);
        if (d1 && d2) {
            const uint8_t p1[22] = {
                0x4C, 0x8B, 0x73, 0x08,       // mov r14, [rbx+0x08]
                0x4C, 0x89, 0xF6,             // mov rsi, r14
                0x48, 0x8B, 0x43, 0x10,       // mov rax, [rbx+0x10]
                0x48, 0x89, 0xC7,             // mov rdi, rax
                0x48, 0x29, 0xF7,             // sub rdi, rsi
                0x0F, 0x1F, 0x44, 0x00, 0x00  // nop 5
            };
            if (PatchTextBytesWX(a + 6, p1, sizeof(p1)) &&
                PatchTextBytesWX(a + 49, nop11, sizeof(nop11)) &&
                PatchTextBytesWX(d1 + 8, nop5, sizeof(nop5)) &&
                PatchTextBytesWX(d2 + 8, nop5, sizeof(nop5))) {
                ++patched;
            }
        }
    }

    // Site 3: LightWatcher::RGB (rdi = cell->ent_begin, replace memmove call with `mov rax, rdi; nop2`)
    if (uint8_t* a = FindUniqueAob(kAobSpatialAnchorLightRGB)) {
        if (uint8_t* d = FindUniqueAob(kAobSpatialDelLightRGB)) {
            const uint8_t p1[22] = {
                0x48, 0x8B, 0x7D, 0x08,       // mov rdi, [rbp+0x08]
                0x48, 0x89, 0xFE,             // mov rsi, rdi
                0x48, 0x8B, 0x45, 0x10,       // mov rax, [rbp+0x10]
                0x48, 0x89, 0xC2,             // mov rdx, rax
                0x48, 0x29, 0xF2,             // sub rdx, rsi
                0x0F, 0x1F, 0x44, 0x00, 0x00  // nop 5
            };
            const uint8_t pMovRax[5] = { 0x48, 0x89, 0xF8, 0x66, 0x90 }; // mov rax, rdi; nop2
            if (PatchTextBytesWX(a + 6, p1, sizeof(p1)) &&
                PatchTextBytesWX(a + 46, pMovRax, sizeof(pMovRax)) &&
                PatchTextBytesWX(d + 8, nop5, sizeof(nop5))) {
                ++patched;
            }
        }
    }

    g_stats.spatial_zerocopy_sites.store(static_cast<uint64_t>(patched), std::memory_order_release);
    return patched;
}

// --- Module 8: WritePersistentFile Interceptor & Shutdown Drain ---
uint8_t HookWritePersistentFile(const char* path, const void* data, int size,
                                void* cb, uint32_t arg5, void* arg6) {
    struct CaptureGuard {
        AsyncCaptureState prev;
        CaptureGuard(const char* p, const void* d, uint32_t sz, bool enable) noexcept : prev(tl_asyncCapture) {
            if (enable) {
                tl_asyncCapture.active = true;
                tl_asyncCapture.path   = p;
                tl_asyncCapture.data   = d;
                tl_asyncCapture.size   = sz;
            }
        }
        ~CaptureGuard() noexcept { tl_asyncCapture = prev; }
    };
    const bool enable = g_cfg.enable_async_save &&
                        g_asyncSaveActive.load(std::memory_order_acquire) &&
                        path != nullptr && data != nullptr && size > 0;
    CaptureGuard guard(path, data, enable ? static_cast<uint32_t>(size) : 0u, enable);
    return g_origWritePFile(path, data, size, cb, arg5, arg6);
}

bool CanCaptureAsyncFopen(const char* path) noexcept {
    if (!path || path[0] == '\0' || !g_real_stat) return false;
    const size_t len = std::strlen(path);
    if (len >= 4 && (std::strcmp(path + len - 4, ".zip") == 0 || std::strcmp(path + len - 4, ".ZIP") == 0)) {
        return false;
    }
    const char* slash = std::strrchr(path, '/');
    if (slash && slash > path) {
        char parent[2048];
        const size_t dlen = std::min(static_cast<size_t>(slash - path), sizeof(parent) - 1);
        std::memcpy(parent, path, dlen);
        parent[dlen] = '\0';
        struct stat pst{};
        if (g_real_stat(parent, &pst) != 0 || !S_ISDIR(pst.st_mode)) return false;
    }
    struct stat tst{};
    if (g_real_stat(path, &tst) == 0) {
        if (S_ISDIR(tst.st_mode) || (tst.st_mode & S_IWUSR) == 0) return false;
    }
    return true;
}

void FlushSavesAndLogOnShutdown() noexcept {
    if (!g_isServerProcess.load(std::memory_order_acquire)) return;
    if (auto* p = g_asyncSavePtr.load(std::memory_order_acquire)) {
        p->ShutdownAndFlush();
    }
    if (g_cfg.enable_profiler) {
        LogTelemetrySummary("EXIT");
    }
}

// =============================================================================
// 10. Process-Gated ELF Constructor Initialization
// =============================================================================
__attribute__((constructor))
void HyperCoreLinuxInit() {
    EnsureRealSyms();
    g_pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
    if (g_pageSize < 4096) g_pageSize = 4096;

    char exePath[1024] = {};
    const ssize_t rlen = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (rlen <= 0) return;
    exePath[rlen] = '\0';
    const char* base = std::strrchr(exePath, '/');
    base = base ? (base + 1) : exePath;
    if (std::strcmp(base, "dontstarve_dedicated_server_nullrenderer_x64") != 0) {
        return; // Strictly inert in non-DST processes (bash/python/steamcmd/etc.)
    }

    g_isServerProcess.store(true, std::memory_order_release);
    ShimGuard guard;
    InitPathsAndLoadConfig();
    LogMsg("=== DST HyperCore Server Linux Engine v1.3.0 Boot (PID=%d) ===", static_cast<int>(getpid()));
    LogMsg("Config: profiler=%d mimalloc=%d spatial_zc=%d adaptive_gc=%d libdeflate=%d(L%d) "
           "fastload=%d fast_dumper=%d fast_astar=%d fs_neg=%d async_save=%d",
           g_cfg.enable_profiler?1:0, g_cfg.enable_mimalloc?1:0, g_cfg.enable_spatial_zerocopy?1:0,
           g_cfg.enable_adaptive_gc?1:0, g_cfg.enable_libdeflate?1:0, g_cfg.libdeflate_level,
           g_cfg.enable_fastload?1:0, g_cfg.enable_fast_dumper?1:0, g_cfg.enable_fast_astar?1:0,
           g_cfg.enable_fs_neg_cache?1:0, g_cfg.enable_async_save?1:0);

    dl_iterate_phdr(DlPhdrCallback, nullptr);
    if (!g_exeTextBeg || !g_exeTextEnd) {
        LogMsg("[ERROR] Failed to locate main PIE executable .text segment via dl_iterate_phdr");
        return;
    }
    LogMsg("[ELF] PIE base=%p .text=%p..%p", g_exeBase, g_exeTextBeg, g_exeTextEnd);

    // 1. Resolve Lua 5.1.4 C API
    g_lua.checkstack   = reinterpret_cast<decltype(g_lua.checkstack)>(FindUniqueAob(kAobLuaCheckStack));
    g_lua.gettop       = reinterpret_cast<decltype(g_lua.gettop)>(FindUniqueAob(kAobLuaGetTop));
    g_lua.settop       = reinterpret_cast<decltype(g_lua.settop)>(FindUniqueAob(kAobLuaSetTop));
    g_lua.pushvalue    = reinterpret_cast<decltype(g_lua.pushvalue)>(FindUniqueAob(kAobLuaPushValue));
    g_lua.type         = reinterpret_cast<decltype(g_lua.type)>(FindUniqueAob(kAobLuaType));
    g_lua.tonumber     = reinterpret_cast<decltype(g_lua.tonumber)>(FindUniqueAob(kAobLuaToNumber));
    g_lua.toboolean    = reinterpret_cast<decltype(g_lua.toboolean)>(FindUniqueAob(kAobLuaToBoolean));
    g_lua.tolstring    = reinterpret_cast<decltype(g_lua.tolstring)>(FindUniqueAob(kAobLuaToLString));
    g_lua.topointer    = reinterpret_cast<decltype(g_lua.topointer)>(FindUniqueAob(kAobLuaToPointer));
    g_lua.pushnil      = reinterpret_cast<decltype(g_lua.pushnil)>(FindUniqueAob(kAobLuaPushNil));
    g_lua.pushnumber   = reinterpret_cast<decltype(g_lua.pushnumber)>(FindUniqueAob(kAobLuaPushNumber));
    g_lua.pushlstring  = reinterpret_cast<decltype(g_lua.pushlstring)>(FindUniqueAob(kAobLuaPushLString));
    g_lua.pushstring   = reinterpret_cast<decltype(g_lua.pushstring)>(FindUniqueAob(kAobLuaPushString));
    g_lua.pushcclosure = reinterpret_cast<decltype(g_lua.pushcclosure)>(FindUniqueAob(kAobLuaPushCClos));
    g_lua.pushboolean  = reinterpret_cast<decltype(g_lua.pushboolean)>(FindUniqueAob(kAobLuaPushBool));
    g_lua.getfield     = reinterpret_cast<decltype(g_lua.getfield)>(FindUniqueAob(kAobLuaGetField));
    g_lua.createtable  = reinterpret_cast<decltype(g_lua.createtable)>(FindUniqueAob(kAobLuaCreateTbl));
    g_lua.setfield     = reinterpret_cast<decltype(g_lua.setfield)>(FindUniqueAob(kAobLuaSetField));
    g_lua.gc           = reinterpret_cast<decltype(g_lua.gc)>(FindUniqueAob(kAobLuaGc));
    g_lua.next         = reinterpret_cast<decltype(g_lua.next)>(FindUniqueAob(kAobLuaNext));
    LogMsg("[LUA] C API resolved: ReadyForRegister=%d ReadyForFastDump=%d",
           g_lua.ReadyForRegister() ? 1 : 0, g_lua.ReadyForFastDump() ? 1 : 0);

    // 2. Materialize immortal singletons BEFORE installing hooks
    (void)GetFsNegCache();
    (void)GetAsyncSave();
    (void)GetAStarMgr();
    (void)GetGcPolicyState();

    // 3. Module 1: Mimalloc + Lua OpenLibs/Close
    g_simLuaAllocAddr = FindUniqueAob(kAobSimLuaAlloc);
    g_wgLuaAllocAddr  = FindUniqueAob(kAobWgLuaAlloc);
    g_trackLuaMem     = reinterpret_cast<TrackLuaMem_t>(FindUniqueAob(kAobTrackLuaMem));
    if (uint8_t* pNewState = FindUniqueAob(kAobLuaNewState)) {
        const bool ok = (g_simLuaAllocAddr && g_wgLuaAllocAddr) &&
                        InstallHookSysV(pNewState, reinterpret_cast<void*>(&HookLuaNewState), 14,
                                        reinterpret_cast<void**>(&g_origLuaNewState));
        LogMsg("[HOOK] lua_newstate @ %p -> %s", pNewState, ok ? "OK" : "FAIL");
    }
    if (uint8_t* pOpenLibs = FindUniqueAob(kAobLuaOpenLibs)) {
        const RelocLea rel{9};
        const bool ok = InstallHookSysV(pOpenLibs, reinterpret_cast<void*>(&HookLuaOpenLibs), 16,
                                        reinterpret_cast<void**>(&g_origLuaOpenLibs), &rel, 1);
        LogMsg("[HOOK] luaL_openlibs @ %p -> %s", pOpenLibs, ok ? "OK" : "FAIL");
    }
    if (uint8_t* pLuaClose = FindUniqueAob(kAobLuaClose)) {
        const RelocLea rel{6};
        const bool ok = InstallHookSysV(pLuaClose, reinterpret_cast<void*>(&HookLuaClose), 17,
                                        reinterpret_cast<void**>(&g_origLuaClose), &rel, 1);
        LogMsg("[HOOK] lua_close @ %p -> %s", pLuaClose, ok ? "OK" : "FAIL");
    }

    // 4. Module 0A/0B + Module 2 (GC)
    g_setGcBudget = reinterpret_cast<SetGcBudget_t>(FindUniqueAob(kAobSetGcBudget));
    if (uint8_t* pSimUpd = FindUniqueAob(kAobSimUpdate)) {
        const bool ok = InstallHookSysV(pSimUpd, reinterpret_cast<void*>(&HookSimUpdate), 14,
                                        reinterpret_cast<void**>(&g_origSimUpdate));
        LogMsg("[HOOK] Simulation::Update @ %p -> %s", pSimUpd, ok ? "OK" : "FAIL");
    }
    if (uint8_t* pSimLua = FindUniqueAob(kAobSimLuaUpd)) {
        const bool ok = InstallHookSysV(pSimLua, reinterpret_cast<void*>(&HookSimLuaUpdate), 15,
                                        reinterpret_cast<void**>(&g_origSimLuaUpdate));
        LogMsg("[HOOK] Simulation::luaUpdate @ %p -> %s", pSimLua, ok ? "OK" : "FAIL");
    }
    if (uint8_t* pCollGc = FindUniqueAob(kAobSimCollGc)) {
        const bool ok = InstallHookSysV(pCollGc, reinterpret_cast<void*>(&HookCollectGarbage), 15,
                                        reinterpret_cast<void**>(&g_origCollectGarbage));
        LogMsg("[HOOK] Simulation::CollectGarbage @ %p -> %s", pCollGc, ok ? "OK" : "FAIL");
    }
    if (uint8_t* pLuaGc = FindUniqueAob(kAobLuaGc)) {
        const bool ok = InstallHookSysV(pLuaGc, reinterpret_cast<void*>(&HookLuaGc), 14,
                                        reinterpret_cast<void**>(&g_origLuaGc));
        LogMsg("[HOOK] lua_gc (Anti-STW Shield) @ %p -> %s", pLuaGc, ok ? "OK" : "FAIL");
    }
    if (uint8_t* pNetUpd = FindUniqueAob(kAobNetMgrUpd)) {
        const RelocLea rel{11};
        const bool ok = InstallHookSysV(pNetUpd, reinterpret_cast<void*>(&HookNetMgrUpdate), 18,
                                        reinterpret_cast<void**>(&g_origNetMgrUpdate), &rel, 1);
        LogMsg("[HOOK] cNetworkManager::Update @ %p -> %s", pNetUpd, ok ? "OK" : "FAIL");
    }
    if (uint8_t* pRepUpd = FindUniqueAob(kAobRepMgrUpd)) {
        const RelocLea rel{11};
        const bool ok = InstallHookSysV(pRepUpd, reinterpret_cast<void*>(&HookReplicaMgr3Update), 18,
                                        reinterpret_cast<void**>(&g_origReplicaUpdate), &rel, 1);
        LogMsg("[HOOK] ReplicaManager3::Update @ %p -> %s", pRepUpd, ok ? "OK" : "FAIL");
    }
    if (uint8_t* pFe = FindUniqueAob(kAobSimFindEnts)) {
        InstallHookSysV(pFe, reinterpret_cast<void*>(&HookSimFindEntities), 15,
                        reinterpret_cast<void**>(&g_origSimFindEnts));
    }
    if (uint8_t* pFer = FindUniqueAob(kAobSimFindReg)) {
        InstallHookSysV(pFer, reinterpret_cast<void*>(&HookSimFindEntitiesReg), 16,
                        reinterpret_cast<void**>(&g_origSimFindEntsReg));
    }
    if (uint8_t* pSer = FindUniqueAob(kAobSerializeW)) {
        InstallHookSysV(pSer, reinterpret_cast<void*>(&HookSerializeWorldSession), 16,
                        reinterpret_cast<void**>(&g_origSerializeWorld));
    }

    // 5. Module 3: 4-Site Zero-Copy SpatialGrid Patches
    const int zcCount = ApplySpatialGridZeroCopyPatches();
    LogMsg("[SPATIAL] Linux Zero-Copy SpatialGrid sites active: %d/4", zcCount);

    // 6. Module 4A & 4C: libdeflate + FastLoad
    if (g_cfg.enable_libdeflate) {
        if (uint8_t* pCmp = FindUniqueAob(kAobCompress2)) {
            const bool ok = InstallHookSysV(pCmp, reinterpret_cast<void*>(&HookCompress2), 15,
                                            reinterpret_cast<void**>(&g_origCompress2));
            LogMsg("[HOOK] zlib::compress2 (LP64) @ %p -> %s", pCmp, ok ? "OK" : "FAIL");
        }
    }
    if (g_cfg.enable_fastload) {
        if (uint8_t* pUnz = FindUniqueAob(kAobUnzRead)) {
            const bool ok = InstallHookSysV(pUnz, reinterpret_cast<void*>(&HookUnzReadCurrentFile), 15,
                                            reinterpret_cast<void**>(&g_origUnzRead));
            LogMsg("[FASTLOAD] minizip unzReadCurrentFile (LP64) @ %p -> %s", pUnz, ok ? "OK" : "FAIL");
        }
        if (uint8_t* pInf = FindUniqueAob(kAobInflateFast)) {
            uint8_t jmp[14]; WriteAbsJmp14(jmp, reinterpret_cast<const void*>(&HookInflateFast));
            const bool ok = PatchTextBytesWX(pInf, jmp, 14);
            LogMsg("[FASTLOAD] zlib 1.2.3 inflate_fast (LP64) @ %p -> %s", pInf, ok ? "OK" : "FAIL");
        }
    }

    // 7. Module 6: O(N log N) FastAStar
    if (g_cfg.enable_fast_astar) {
        uint8_t* pStep = FindUniqueAob(kAobAStarStep);
        uint8_t* pNbL  = FindUniqueAob(kAobAStarNbLand);
        uint8_t* pNbW  = FindUniqueAob(kAobAStarNbWater);
        uint8_t* pHeur = FindUniqueAob(kAobAStarHeur);
        uint8_t* pNew  = FindUniqueAob(kAobKleiNew);
        uint8_t* pDel  = FindUniqueAob(kAobKleiDelete);

        // Decode `call 0x13a410` (PathVec::_M_realloc_insert) at AStar::Step+0x3CC reliably
        if (pStep && pStep[0x3CC] == 0xE8) {
            int32_t disp = 0; std::memcpy(&disp, pStep + 0x3CD, 4);
            uint8_t* cand = pStep + 0x3D1 + static_cast<int64_t>(disp);
            if (cand[0] == 0xF3 && cand[1] == 0x0F && cand[2] == 0x1E && cand[3] == 0xFA) {
                g_fnPathVecRealloc = reinterpret_cast<PathVecRealloc_t>(cand);
            }
        }
        g_fnKleiNew    = reinterpret_cast<KleiNew_t>(pNew);
        g_fnKleiDelete = reinterpret_cast<KleiDelete_t>(pDel);
        if (pStep && pNbL && pNbW && pHeur && g_fnPathVecRealloc && g_fnKleiNew && g_fnKleiDelete) {
            g_astarCb.reserveNeighbors   = &AStarReserveNeighbors;
            g_astarCb.getNeighborsNormal = reinterpret_cast<decltype(g_astarCb.getNeighborsNormal)>(pNbL);
            g_astarCb.getNeighborsType2  = reinterpret_cast<decltype(g_astarCb.getNeighborsType2)>(pNbW);
            g_astarCb.heuristic          = reinterpret_cast<decltype(g_astarCb.heuristic)>(pHeur);
            g_astarCb.clearPath          = &AStarClearPath;
            g_astarCb.pushPathNode       = &AStarPushPath;
            g_astarCb.freeMem            = g_fnKleiDelete;

            uint8_t* pDt0 = FindUniqueAob(kAobAStarDtor0);
            uint8_t* pDt1 = FindUniqueAob(kAobAStarDtor1);
            uint8_t* pPd0 = FindUniqueAob(kAobPathDtor0);
            const RelocLea r0{6}, r1{6};
            const bool okS  = InstallHookSysV(pStep, reinterpret_cast<void*>(&HookAStarStep), 14, reinterpret_cast<void**>(&g_origAStarStep));
            const bool okD0 = pDt0 && InstallHookSysV(pDt0, reinterpret_cast<void*>(&HookAStarDtor0), 14, reinterpret_cast<void**>(&g_origAStarDtor0), &r0, 1);
            const bool okD1 = pDt1 && InstallHookSysV(pDt1, reinterpret_cast<void*>(&HookAStarDtor1), 14, reinterpret_cast<void**>(&g_origAStarDtor1), &r1, 1);
            const bool okP0 = pPd0 && InstallHookSysV(pPd0, reinterpret_cast<void*>(&HookPathNodeDtor0), 15, reinterpret_cast<void**>(&g_origPathDtor0));
            g_astarReady = okS && okD0 && okD1 && okP0;
        }
        LogMsg("[HOOK] Pathfinder FastAStar O(N log N) -> %s", g_astarReady ? "OK" : "FAIL");
    }

    // 8. Module 7 & Module 8: FS Neg Cache + WritePersistentFile Async Commit
    g_fsNegCacheActive.store(g_cfg.enable_fs_neg_cache, std::memory_order_release);
    if (g_cfg.enable_async_save) {
        if (uint8_t* pWpf = FindUniqueAob(kAobWritePFile)) {
            const bool ok = InstallHookSysV(pWpf, reinterpret_cast<void*>(&HookWritePersistentFile), 15,
                                            reinterpret_cast<void**>(&g_origWritePFile));
            g_asyncSaveActive.store(ok, std::memory_order_release);
            LogMsg("[HOOK] WritePersistentFile AsyncCommit @ %p -> %s", pWpf, ok ? "OK" : "FAIL");
        }
    }

    LockTrampolineSlabWX();
    g_hooksReady.store(true, std::memory_order_release);
    LogMsg("[HOOK] Locked trampoline slab W^X -> PROT_READ|PROT_EXEC (%zu/%zu bytes used)", g_trampUsed, kSlabSize);
}

} // namespace

// =============================================================================
// 11. Exported LD_PRELOAD POSIX Interposition Symbols (Strict exports.map)
// =============================================================================
#define HC_EXPORT __attribute__((visibility("default")))

extern "C" {

static inline bool ModeHasWriteA(const char* mode) noexcept {
    return mode && (std::strchr(mode, 'w') || std::strchr(mode, 'a') || std::strchr(mode, '+'));
}

static FILE* CommonFopenShim(FopenFn_t realFn, const char* path, const char* mode) {
    EnsureRealSyms();
    if (!g_isServerProcess.load(std::memory_order_relaxed) || !g_hooksReady.load(std::memory_order_relaxed) || tl_shimDepth > 0) {
        return realFn(path, mode);
    }
    ShimGuard guard;
    if (tl_asyncCapture.active && path && mode && std::strcmp(mode, "wb") == 0 &&
        tl_asyncCapture.path && std::strcmp(path, tl_asyncCapture.path) == 0 &&
        g_asyncSaveActive.load(std::memory_order_acquire)) {
        GetFsNegCache().InvalidateAll();
        if (CanCaptureAsyncFopen(path) &&
            GetAsyncSave().Enqueue(path, tl_asyncCapture.data, tl_asyncCapture.size)) {
            return GetAsyncSentinelFp();
        }
        g_stats.async_save_fallbacks.fetch_add(1, std::memory_order_relaxed);
        GetAsyncSave().FlushPendingForPath(path);
    } else {
        GetAsyncSave().FlushPendingForPath(path);
        if (ModeHasWriteA(mode)) {
            GetFsNegCache().InvalidateAll();
        } else if (g_fsNegCacheActive.load(std::memory_order_acquire) && g_cfg.enable_fs_neg_cache) {
            uint64_t h1 = 0, h2 = 0;
            if (PosixFsNegCache::IsCacheableReadOnlyAssetPath(path, &h1, &h2)) {
                const int64_t now = NowNs();
                if (GetFsNegCache().Lookup(h1, h2, now)) {
                    g_stats.fs_neg_hits.fetch_add(1, std::memory_order_relaxed);
                    errno = ENOENT;
                    return nullptr;
                }
                FILE* fp = realFn(path, mode);
                const int savedErr = errno;
                if (!fp && (savedErr == ENOENT || savedErr == ENOTDIR)) {
                    g_stats.fs_neg_misses.fetch_add(1, std::memory_order_relaxed);
                    GetFsNegCache().Insert(h1, h2, now, 15LL * 1000000000LL);
                }
                errno = savedErr;
                return fp;
            }
        }
    }
    return realFn(path, mode);
}

HC_EXPORT FILE* fopen(const char* path, const char* mode) {
    EnsureRealSyms();
    return CommonFopenShim(g_real_fopen, path, mode);
}

HC_EXPORT FILE* fopen64(const char* path, const char* mode) {
    EnsureRealSyms();
    return CommonFopenShim(g_real_fopen64 ? g_real_fopen64 : g_real_fopen, path, mode);
}

HC_EXPORT size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    EnsureRealSyms();
    if (IsAsyncSentinelFp(stream)) return nmemb;
    return g_real_fwrite(ptr, size, nmemb, stream);
}

HC_EXPORT int fclose(FILE* stream) {
    EnsureRealSyms();
    if (IsAsyncSentinelFp(stream)) return 0;
    return g_real_fclose(stream);
}

static int CommonOpenShim(OpenFn_t realFn, const char* pathname, int flags, bool hasMode, mode_t mode) {
    EnsureRealSyms();
    if (g_isServerProcess.load(std::memory_order_relaxed) && g_hooksReady.load(std::memory_order_relaxed) && tl_shimDepth == 0) {
        ShimGuard guard;
        GetAsyncSave().FlushPendingForPath(pathname);
        if ((flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC | O_APPEND)) != 0) {
            GetFsNegCache().InvalidateAll();
        }
    }
    return hasMode ? realFn(pathname, flags, mode) : realFn(pathname, flags);
}

HC_EXPORT int open(const char* pathname, int flags, ...) {
    mode_t mode = 0;
    bool hasMode = false;
    if ((flags & O_CREAT) != 0) {
        va_list ap; va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
        hasMode = true;
    }
    return CommonOpenShim(g_real_open, pathname, flags, hasMode, mode);
}

HC_EXPORT int open64(const char* pathname, int flags, ...) {
    mode_t mode = 0;
    bool hasMode = false;
    if ((flags & O_CREAT) != 0) {
        va_list ap; va_start(ap, flags);
        mode = static_cast<mode_t>(va_arg(ap, int));
        va_end(ap);
        hasMode = true;
    }
    return CommonOpenShim(g_real_open64 ? g_real_open64 : g_real_open, pathname, flags, hasMode, mode);
}

static int CommonXstatShim(XstatFn_t realFn, int ver, const char* path, void* buf) {
    EnsureRealSyms();
    if (!g_isServerProcess.load(std::memory_order_relaxed) || !g_hooksReady.load(std::memory_order_relaxed) || tl_shimDepth > 0) {
        return realFn(ver, path, buf);
    }
    ShimGuard guard;
    GetAsyncSave().FlushPendingForPath(path);
    if (g_fsNegCacheActive.load(std::memory_order_acquire) && g_cfg.enable_fs_neg_cache) {
        uint64_t h1 = 0, h2 = 0;
        if (PosixFsNegCache::IsCacheableReadOnlyAssetPath(path, &h1, &h2)) {
            const int64_t now = NowNs();
            if (GetFsNegCache().Lookup(h1, h2, now)) {
                g_stats.fs_neg_hits.fetch_add(1, std::memory_order_relaxed);
                errno = ENOENT;
                return -1;
            }
            const int rc = realFn(ver, path, buf);
            const int savedErr = errno;
            if (rc != 0 && (savedErr == ENOENT || savedErr == ENOTDIR)) {
                g_stats.fs_neg_misses.fetch_add(1, std::memory_order_relaxed);
                GetFsNegCache().Insert(h1, h2, now, 15LL * 1000000000LL);
            }
            errno = savedErr;
            return rc;
        }
    }
    return realFn(ver, path, buf);
}

HC_EXPORT int __xstat(int ver, const char* path, struct stat* buf) {
    EnsureRealSyms();
    return CommonXstatShim(g_real_xstat, ver, path, buf);
}

HC_EXPORT int __xstat64(int ver, const char* path, struct stat64* buf) {
    EnsureRealSyms();
    return CommonXstatShim(g_real_xstat64 ? g_real_xstat64 : g_real_xstat, ver, path, buf);
}

static int CommonStatShim(StatFn_t realFn, const char* path, void* buf) {
    EnsureRealSyms();
    if (!g_isServerProcess.load(std::memory_order_relaxed) || !g_hooksReady.load(std::memory_order_relaxed) || tl_shimDepth > 0) {
        return realFn(path, buf);
    }
    ShimGuard guard;
    GetAsyncSave().FlushPendingForPath(path);
    if (g_fsNegCacheActive.load(std::memory_order_acquire) && g_cfg.enable_fs_neg_cache) {
        uint64_t h1 = 0, h2 = 0;
        if (PosixFsNegCache::IsCacheableReadOnlyAssetPath(path, &h1, &h2)) {
            const int64_t now = NowNs();
            if (GetFsNegCache().Lookup(h1, h2, now)) {
                g_stats.fs_neg_hits.fetch_add(1, std::memory_order_relaxed);
                errno = ENOENT;
                return -1;
            }
            const int rc = realFn(path, buf);
            const int savedErr = errno;
            if (rc != 0 && (savedErr == ENOENT || savedErr == ENOTDIR)) {
                g_stats.fs_neg_misses.fetch_add(1, std::memory_order_relaxed);
                GetFsNegCache().Insert(h1, h2, now, 15LL * 1000000000LL);
            }
            errno = savedErr;
            return rc;
        }
    }
    return realFn(path, buf);
}

HC_EXPORT int stat(const char* path, struct stat* buf) {
    EnsureRealSyms();
    return CommonStatShim(g_real_stat, path, buf);
}

HC_EXPORT int stat64(const char* path, struct stat64* buf) {
    EnsureRealSyms();
    return CommonStatShim(g_real_stat64 ? g_real_stat64 : g_real_stat, path, buf);
}

HC_EXPORT int access(const char* pathname, int mode) {
    EnsureRealSyms();
    if (!g_isServerProcess.load(std::memory_order_relaxed) || !g_hooksReady.load(std::memory_order_relaxed) || tl_shimDepth > 0) {
        return g_real_access(pathname, mode);
    }
    ShimGuard guard;
    GetAsyncSave().FlushPendingForPath(pathname);
    if (mode == F_OK && g_fsNegCacheActive.load(std::memory_order_acquire) && g_cfg.enable_fs_neg_cache) {
        uint64_t h1 = 0, h2 = 0;
        if (PosixFsNegCache::IsCacheableReadOnlyAssetPath(pathname, &h1, &h2)) {
            const int64_t now = NowNs();
            if (GetFsNegCache().Lookup(h1, h2, now)) {
                g_stats.fs_neg_hits.fetch_add(1, std::memory_order_relaxed);
                errno = ENOENT;
                return -1;
            }
            const int rc = g_real_access(pathname, mode);
            const int savedErr = errno;
            if (rc != 0 && (savedErr == ENOENT || savedErr == ENOTDIR)) {
                g_stats.fs_neg_misses.fetch_add(1, std::memory_order_relaxed);
                GetFsNegCache().Insert(h1, h2, now, 15LL * 1000000000LL);
            }
            errno = savedErr;
            return rc;
        }
    }
    return g_real_access(pathname, mode);
}

HC_EXPORT DIR* opendir(const char* name) {
    EnsureRealSyms();
    if (g_isServerProcess.load(std::memory_order_relaxed) && g_hooksReady.load(std::memory_order_relaxed) && tl_shimDepth == 0) {
        ShimGuard guard;
        GetAsyncSave().FlushPendingForDir(name);
    }
    return g_real_opendir(name);
}

HC_EXPORT int mkdir(const char* pathname, mode_t mode) {
    EnsureRealSyms();
    if (g_isServerProcess.load(std::memory_order_relaxed) && g_hooksReady.load(std::memory_order_relaxed) && tl_shimDepth == 0) {
        ShimGuard guard;
        GetAsyncSave().FlushPendingForPath(pathname);
        GetFsNegCache().InvalidateAll();
    }
    return g_real_mkdir(pathname, mode);
}

HC_EXPORT int rmdir(const char* pathname) {
    EnsureRealSyms();
    if (g_isServerProcess.load(std::memory_order_relaxed) && g_hooksReady.load(std::memory_order_relaxed) && tl_shimDepth == 0) {
        ShimGuard guard;
        GetAsyncSave().FlushPendingForDir(pathname);
        GetFsNegCache().InvalidateAll();
    }
    return g_real_rmdir(pathname);
}

HC_EXPORT int unlink(const char* pathname) {
    EnsureRealSyms();
    if (g_isServerProcess.load(std::memory_order_relaxed) && g_hooksReady.load(std::memory_order_relaxed) && tl_shimDepth == 0) {
        ShimGuard guard;
        GetAsyncSave().FlushPendingForPath(pathname);
        GetFsNegCache().InvalidateAll();
    }
    return g_real_unlink(pathname);
}

HC_EXPORT int remove(const char* pathname) {
    EnsureRealSyms();
    if (g_isServerProcess.load(std::memory_order_relaxed) && g_hooksReady.load(std::memory_order_relaxed) && tl_shimDepth == 0) {
        ShimGuard guard;
        GetAsyncSave().FlushPendingForPath(pathname);
        GetFsNegCache().InvalidateAll();
    }
    return g_real_remove(pathname);
}

HC_EXPORT int rename(const char* oldpath, const char* newpath) {
    EnsureRealSyms();
    if (g_isServerProcess.load(std::memory_order_relaxed) && g_hooksReady.load(std::memory_order_relaxed) && tl_shimDepth == 0) {
        ShimGuard guard;
        GetAsyncSave().FlushPendingForPath(oldpath);
        GetAsyncSave().FlushPendingForPath(newpath);
        GetFsNegCache().InvalidateAll();
    }
    return g_real_rename(oldpath, newpath);
}

HC_EXPORT __attribute__((noreturn)) void exit(int status) {
    EnsureRealSyms();
    FlushSavesAndLogOnShutdown();
    g_real_exit(status);
    __builtin_unreachable();
}

HC_EXPORT __attribute__((noreturn)) void _exit(int status) {
    EnsureRealSyms();
    FlushSavesAndLogOnShutdown();
    g_real__exit(status);
    __builtin_unreachable();
}

} // extern "C"
