// =============================================================================
// DST HyperSmooth & FastLoad Native Companion Plugin v2.4.0 (dinput8.dll x64)
// =============================================================================
// Production-grade, security-hardened universal x86-64 native plugin for DST:
//   1. Lock-free full 6/6 System32\dinput8.dll proxy + Module Pinning + Safe DllMain
//   2. ZERO runtime .text writes: one-shot permanent FrameLimiter::SetTargetFrameTime
//      hook + pure atomic FPS switching (0 runtime VirtualProtect, 0 tearing risk)
//   3. Cold-start ThreadFreezeGuard (thread suspend + RIP collision check) + strict W^X
//      (PAGE_EXECUTE_READ on all trampolines/stubs)
//   4. [v2.4.0] Zero-Contention RTSS-Grade Frame Pacer (FrameLimiter::Wait hook):
//      - Win10/11 CREATE_WAITABLE_TIMER_HIGH_RESOLUTION + self-calibrating wakeup EMA
//      - Absolute deadline scheduling (lag-spike snap, zero catch-up bursts)
//      - Yielding _mm_pause() precision tail -> 0% CPU contention with GPU/ANGLE/Audio!
//      - Lock-free 512-frame ring buffer telemetry (avgFPS, 1% Low FPS, P99 ms, jitter)
//   5. [v2.4.0] 30Hz-Heartbeat-Dodging Slack-Aware Client GC (defers LUA_GCSTEP off
//      heavy 30Hz SimTick frames into idle ~1ms render frames, anti-starvation ceiling)
//   6. [v2.4.0] Hermetic mimalloc v2.1.7 Lockless Client Lua Heap (Sim + WorldGen VMs)
//   7. [v2.4.0] Auto-Negotiated Opt-In 60Hz SimTick Receiver (strictly 30Hz on all
//      vanilla/public servers; auto-resets to 30Hz on every VM/menu/server transition!)
//   8. Hardware `rep stosb` fix + libdeflate v1.23 (AVX-512/AVX2/SSE2) zip hook
//   9. True 240fps Full-World 2D Skeletal Animation Interpolation (Main + Overlay + UI)
//  10. 0ms Direct C++ <-> Lua VM Native Bridge (zero disk polling once connected)
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <immintrin.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "libdeflate.h"
#include "mimalloc.h"

namespace {

using DirectInput8Create_t  = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using DllCanUnloadNow_t     = HRESULT(WINAPI*)();
using DllGetClassObject_t   = HRESULT(WINAPI*)(REFCLSID, REFIID, LPVOID*);
using DllRegisterServer_t   = HRESULT(WINAPI*)();
using DllUnregisterServer_t = HRESULT(WINAPI*)();
using GetdfDIJoystick_t     = const void*(WINAPI*)();

using UnzReadCurrentFile_t = int (*)(void* file, void* buf, uint32_t len);
using ZReadFileFunc_t = uint32_t (*)(void* opaque, void* stream, void* buf, uint32_t size);
using ZSeekFileFunc_t = int32_t (*)(void* opaque, void* stream, uint32_t offset, int origin);
using AnimGetFrame_t = const void* (*)(const void* anim, int32_t playMode, float time);
using LuaGc_t = int (*)(void* L, int what, int data);
using SetGcBudget_t = void (*)(float sec);
using LuaOpenLibs_t = void (*)(void* L);
using LuaCFunction_t = int (*)(void* L);
using LuaPushCClosure_t = void (*)(void* L, LuaCFunction_t fn, int n);
using LuaSetField_t = void (*)(void* L, int idx, const char* k);
using LuaPushNumber_t = void (*)(void* L, double n);
using LuaPushBoolean_t = void (*)(void* L, int b);
using LuaPushString_t = void (*)(void* L, const char* s);
using LuaToNumber_t = double (*)(void* L, int idx);
using LuaToBoolean_t = int (*)(void* L, int idx);
using LuaAlloc_t = void* (*)(void* ud, void* ptr, size_t osize, size_t nsize);
using LuaNewState_t = void* (*)(LuaAlloc_t f, void* ud);
using TrackLuaMem_t = void (*)(void* ud, uint32_t osize, uint32_t nsize);
using CreateWaitableTimerExW_t = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, LPCWSTR, DWORD, DWORD);
using EglGetCurrentContext_t = void* (WINAPI*)();
using GlFinish_t = void (WINAPI*)();
using ClientSpatialQuery_t = void (*)(void* grid, const float* posXZ, float radius, void* visitor);
using ClientSpatialVisitor_t = uint8_t (*)(void* visitor, void* elem);
using ProcessAsyncTexQueue_t = void (*)(void* loader);

constexpr int kLuaGlobalsIndex = -10002;
constexpr DWORD kCreateWaitableTimerHighRes = 0x00000002; // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION (Win10 1803+)
constexpr size_t kClientSpatialSlots = 5;

HMODULE g_selfModule = nullptr;
std::atomic<HMODULE> g_realDInput8{nullptr};
std::atomic<FARPROC> g_pDirectInput8Create{nullptr};
std::atomic<FARPROC> g_pDllCanUnloadNow{nullptr};
std::atomic<FARPROC> g_pDllGetClassObject{nullptr};
std::atomic<FARPROC> g_pDllRegisterServer{nullptr};
std::atomic<FARPROC> g_pDllUnregisterServer{nullptr};
std::atomic<FARPROC> g_pGetdfDIJoystick{nullptr};

SRWLOCK g_logLock = SRWLOCK_INIT;
HANDLE g_workerThread = nullptr;
HANDLE g_prewarmThread = nullptr;
std::atomic<LONG> g_stopFlag{0};
bool g_timerBoosted = false;

uint8_t* g_patchAddr = nullptr;
std::atomic<bool> g_frameLimiterHooked{false};
std::atomic<bool> g_optRtssPacer{true};
std::atomic<bool> g_optMimallocLua{true};
std::atomic<bool> g_optRakNetZeroStall{true};
std::atomic<bool> g_optLowLatencyGpuSync{true};
std::atomic<bool> g_optFastSpatialQuery{true};
std::atomic<bool> g_optTexTimeSlice{true};
ClientSpatialQuery_t   g_origClientSpatialQuery[kClientSpatialSlots]{};
ClientSpatialVisitor_t g_clientSpatialVisitor[kClientSpatialSlots]{};
ProcessAsyncTexQueue_t g_origProcessAsyncTexQueue = nullptr;
UnzReadCurrentFile_t g_origUnzRead = nullptr;
uint8_t* g_unzTrampoline = nullptr;
AnimGetFrame_t g_origAnimGetFrame = nullptr;
uint8_t* g_animRelayStubMain = nullptr;
uint8_t* g_animRelayStubOverlay = nullptr;
uint8_t* g_animRelayStubUI = nullptr;
LuaGc_t g_luaGc = nullptr;
SetGcBudget_t g_setGcBudget = nullptr;
LuaOpenLibs_t g_origLuaOpenLibs = nullptr;
uint8_t* g_luaOpenLibsTramp = nullptr;
LuaPushCClosure_t g_luaPushCClosure = nullptr;
LuaSetField_t g_luaSetField = nullptr;
LuaPushNumber_t g_luaPushNumber = nullptr;
LuaPushBoolean_t g_luaPushBoolean = nullptr;
LuaPushString_t g_luaPushString = nullptr;
LuaToNumber_t g_luaToNumber = nullptr;
LuaToBoolean_t g_luaToBoolean = nullptr;

// v2.4.0 Hermetic mimalloc Client Lua Heap pointers
LuaNewState_t g_origClientLuaNewState = nullptr;
uint8_t* g_clientLuaNewStateTramp = nullptr;
uint8_t* g_clientSimLuaAllocAddr = nullptr;
uint8_t* g_clientWgLuaAllocAddr = nullptr;
TrackLuaMem_t g_clientTrackLuaMem = nullptr;

// v2.4.0 Auto-Negotiated Opt-In 60Hz SimTick (default 30Hz; resets to 30Hz on every VM init!)
float* g_clientSimTickFloatPtr = nullptr;
double* g_clientSimTickDoublePtr = nullptr;
float* g_clientAnimShaderTickPtr = nullptr;
float* g_clientPathfinderTickPtr = nullptr;
float* g_clientSpatialTickPtr = nullptr;
float* g_clientSimTimeDiffPtr = nullptr;
float* g_clientNetTickScalePtr = nullptr;
void** g_clientNetMgrGlobalPtr = nullptr;
using ClientSetNetworkTickRate_t = void (*)(void* netMgr, uint32_t tickRate);
using ClientSetDownloadMs_t      = void (*)(void* replicaMgr, uint64_t intervalMs);
ClientSetNetworkTickRate_t g_fnClientSetNetTickRate = nullptr;
ClientSetDownloadMs_t      g_fnClientSetDownloadMs  = nullptr;
std::atomic<int> g_clientSimTickRate{30};
std::atomic<uint32_t> g_clientSimTickGen{0};

uint32_t g_cmdBuildOffset = 0xB8;
uint32_t g_buildAltOffset = 0xB0;
bool g_altRenderCheckVerified = false;
int64_t g_qpcFreq = 10000000;
double g_qpcInvFreq = 1.0 / 10000000.0;
double g_qpcToUs = 0.1;

std::atomic<LONG> g_currentFps{-1};
std::atomic<LONG> g_skeletalTweenEnabled{1};
std::atomic<LONG> g_worldPosInterpEnabled{1};
std::atomic<LONG> g_luaBridgeActive{0};
std::atomic<int64_t> g_frameStartQpc{0};
std::atomic<uint32_t> g_frameSeq{0};
std::atomic<int64_t> g_lastFrameUs{0};
std::atomic<LONG> g_inGameplay{0};
std::atomic<int64_t> g_vmBaseInterpFrames{0};
std::atomic<int64_t> g_vmBaseWorldPos{0};
std::atomic<int64_t> g_vmBaseSpatialQueries{0};
std::wstring g_binDir;
std::wstring g_iniPath;
std::wstring g_logPath;

volatile LONG64 g_libdeflateHits = 0;
volatile LONG64 g_libdeflateBytes = 0;
volatile LONG64 g_animInterpFrames = 0;
volatile LONG64 g_animElemsTweened = 0;
volatile LONG64 g_animPass2Swaps = 0;
volatile LONG64 g_animSwarmShedFrames = 0;
volatile LONG64 g_worldPosInterpCount = 0;
volatile LONG64 g_worldPosBypassCount = 0;
volatile LONG64 g_gpuSyncDrains = 0;
volatile LONG64 g_gpuSyncSkips = 0;
volatile LONG64 g_gcEmergencyFulls = 0;
volatile LONG64 g_gcDeferredCount = 0;
volatile LONG64 g_miLuaAllocs = 0;
volatile LONG64 g_miLuaReallocs = 0;
volatile LONG64 g_miLuaFrees = 0;
volatile LONG64 g_clientSpatialQueries = 0;
volatile LONG64 g_clientSpatialCells = 0;
volatile LONG64 g_texUploadsTotal = 0;
volatile LONG64 g_texUploadSlices = 0;

// Lock-free per-frame subsystem timing & Hitch breakdown telemetry (0 game-thread I/O)
std::atomic<uint32_t> g_lastAnimHookUs{0};
std::atomic<uint32_t> g_lastAnimEnts{0};
std::atomic<uint32_t> g_lastAnimBones{0};
std::atomic<uint32_t> g_lastAnimShed{0};
std::atomic<uint32_t> g_lastGcUs{0};
std::atomic<int32_t>  g_lastGcKb{0};
std::atomic<uint32_t> g_lastTexUploadUs{0};
std::atomic<uint32_t> g_hitchPendingCount{0};
std::atomic<uint32_t> g_hitchPeakUs{0};
std::atomic<uint32_t> g_hitchAnimUs{0};
std::atomic<uint32_t> g_hitchAnimEnts{0};
std::atomic<uint32_t> g_hitchAnimBones{0};
std::atomic<uint32_t> g_hitchAnimShed{0};
std::atomic<uint32_t> g_hitchGcUs{0};
std::atomic<int32_t>  g_hitchGcKb{0};
std::atomic<uint32_t> g_hitchTexUs{0};

// v2.4.0 512-Frame Ring Buffer for Lock-Free 1% Low / P99 / Jitter Telemetry
struct FrameTimeRing {
    static constexpr size_t kCap = 512;
    SRWLOCK lock = SRWLOCK_INIT;
    float samplesUs[kCap]{};
    size_t writeIdx = 0;
    size_t count = 0;

    void Push(float us) {
        AcquireSRWLockExclusive(&lock);
        samplesUs[writeIdx] = us;
        writeIdx = (writeIdx + 1) & (kCap - 1);
        if (count < kCap) ++count;
        ReleaseSRWLockExclusive(&lock);
    }

    void ComputeStats(double& avgFps, double& low1Fps, double& p99Ms, double& jitterMs) {
        float buf[kCap];
        size_t n = 0;
        size_t startIdx = 0;
        AcquireSRWLockShared(&lock);
        n = count;
        if (n > 0) {
            startIdx = (count < kCap) ? 0 : writeIdx;
            for (size_t i = 0; i < n; ++i) {
                buf[i] = samplesUs[(startIdx + i) & (kCap - 1)];
            }
        }
        ReleaseSRWLockShared(&lock);

        if (n < 4) {
            avgFps = 0.0; low1Fps = 0.0; p99Ms = 0.0; jitterMs = 0.0;
            return;
        }
        // Keep ~2-second rolling tail (120..512 samples) so 1% Low responds swiftly after scene changes
        double totalUs = 0.0;
        size_t win = 0;
        for (size_t i = n; i > 0; --i) {
            totalUs += static_cast<double>(buf[i - 1]);
            ++win;
            if (win >= 120 && totalUs >= 2000000.0) break;
        }
        size_t offset = n - win;
        double jitterSum = 0.0;
        for (size_t i = offset + 1; i < n; ++i) {
            jitterSum += std::fabs(static_cast<double>(buf[i]) - static_cast<double>(buf[i - 1]));
        }
        jitterMs = (win > 1) ? ((jitterSum / static_cast<double>(win - 1)) * 0.001) : 0.0;
        double meanUs = totalUs / static_cast<double>(win);
        avgFps = (meanUs > 1.0) ? (1000000.0 / meanUs) : 0.0;

        float* slice = buf + offset;
        std::sort(slice, slice + win);
        size_t idx99 = static_cast<size_t>(static_cast<double>(win - 1) * 0.99);
        p99Ms = static_cast<double>(slice[idx99]) * 0.001;

        size_t slowCount = std::max<size_t>(1, (win + 99) / 100);
        double slowSum = 0.0;
        for (size_t i = win - slowCount; i < win; ++i) {
            slowSum += static_cast<double>(slice[i]);
        }
        double slowMeanUs = slowSum / static_cast<double>(slowCount);
        low1Fps = (slowMeanUs > 1.0) ? (1000000.0 / slowMeanUs) : 0.0;
    }
};
FrameTimeRing g_frameRing;

static inline int64_t NowQpc() {
    LARGE_INTEGER li{};
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

//------------------------------------------------------------------------------
// AOB Signatures (verified unique 1-match in dontstarve_steam_x64.exe)
//------------------------------------------------------------------------------
constexpr uint8_t kSigFrameLimiter[] = {
    0xF3, 0x0F, 0x11, 0x49, 0x08, 0xC3,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x50,
    0x0F, 0x29, 0x74, 0x24, 0x40, 0x0F, 0x29, 0x7C, 0x24, 0x30,
    0x0F, 0x57, 0xFF, 0x0F, 0xB6, 0xDA
};

constexpr uint8_t kSigVecFillA[] = {
    0x48, 0x8B, 0xCB, 0x48, 0x89, 0x5C, 0x24, 0x20, 0x48, 0x8B, 0xD6, 0x48, 0x89, 0x54, 0x24, 0x28,
    0x48, 0x85, 0xC9, 0x74, 0x19, 0x41, 0x0F, 0xB6, 0x45, 0x00, 0x88, 0x02, 0x48, 0xFF, 0xC9, 0x48,
    0x89, 0x4C, 0x24, 0x20, 0x48, 0xFF, 0xC2, 0x48, 0x89, 0x54, 0x24, 0x28, 0xEB, 0xE2
};

constexpr uint8_t kSigVecFillB[] = {
    0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0x89, 0x4C, 0x24, 0x40, 0x0F, 0x1F, 0x40, 0x00, 0x48, 0x85,
    0xC0, 0x74, 0x15, 0x44, 0x88, 0x21, 0x48, 0xFF, 0xC8, 0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0xFF,
    0xC1, 0x48, 0x89, 0x4C, 0x24, 0x40, 0xEB, 0xE6
};

constexpr uint8_t kSigUnzReadCurrentFile[] = {
    0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xF6, 0x48, 0x85, 0xC9, 0x75, 0x09, 0x8D, 0x46, 0x9A,
    0x48, 0x83, 0xC4, 0x20, 0x5E, 0xC3, 0x48, 0x89, 0x7C, 0x24, 0x38, 0x48, 0x8B, 0xB9, 0xC8, 0x00,
    0x00, 0x00, 0x48, 0x85
};

constexpr uint8_t kSigInflateFast[] = {
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41,
    0x57, 0x48, 0x83, 0xEC, 0x38, 0x4C, 0x8B, 0x51, 0x28, 0x44, 0x8B, 0x71, 0x08, 0x48, 0x8B, 0x31,
    0x4C, 0x8B, 0x41, 0x10
};

// v2.4.0 Verified Unique Client AOBs (1 match each in dontstarve_steam_x64.exe)
constexpr uint8_t kSigLuaNewState[] = {
    0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0xFA, 0x48, 0x8B, 0xF1, 0x41, 0xB9, 0x78, 0x02, 0x00, 0x00, 0x45, 0x33, 0xC0, 0x33, 0xD2,
    0x48, 0x8B, 0xCF, 0xFF
};

constexpr uint8_t kSigSimLuaAlloc[] = {
    0x40, 0x53, 0x55, 0x57, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x78, 0x48, 0x8B, 0x05, 0, 0, 0, 0,
    0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x68, 0x80, 0xB9, 0x18, 0x01, 0x00, 0x00, 0x00, 0x48, 0x89, 0x74, 0x24
};
constexpr char kMaskSimLuaAlloc[] = "xxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxx";

constexpr uint8_t kSigWgLuaAlloc[] = {
    0x4C, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC,
    0x20, 0x49, 0x8B, 0xF9, 0x49, 0x8B, 0xE8, 0x48, 0x8B, 0xF2, 0x4D, 0x85, 0xC9, 0x75, 0x12, 0x48, 0x8B, 0xCA, 0xE8
};

constexpr uint8_t kSigTrackLuaMem[] = {
    0x4C, 0x8B, 0xDC, 0x41, 0x54, 0x41, 0x55, 0x48, 0x81, 0xEC, 0xC8, 0x02, 0x00, 0x00, 0x48, 0x8B,
    0x05, 0, 0, 0, 0, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xB0, 0x02, 0x00, 0x00, 0x80, 0xB9, 0x19, 0x01
};
constexpr char kMaskTrackLuaMem[] = "xxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxx";

constexpr uint8_t kSigSimUpdateTick[] = {
    0xF3, 0x0F, 0x10, 0x05, 0, 0, 0, 0, 0xF3, 0x0F, 0x10, 0x35, 0, 0, 0, 0,
    0x48, 0x8D, 0x84, 0x24, 0xA0, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0xC6, 0x44, 0x0F, 0x2F, 0xD0,
    0xF3, 0x0F, 0x11, 0x84, 0x24, 0xA0, 0x00, 0x00, 0x00, 0x77, 0x08, 0x48, 0x8D, 0x84, 0x24, 0xA8,
    0x00, 0x00, 0x00, 0x80, 0x7B, 0x48, 0x00, 0xF3, 0x44, 0x0F, 0x10, 0x43, 0x44, 0xF3, 0x44, 0x0F,
    0x59, 0x00, 0x75, 0x0C, 0x80, 0x7B, 0x49, 0x00, 0x75, 0x06, 0x80, 0x7B, 0x4A, 0x00, 0x74, 0x0B,
    0x80, 0x7B, 0x4E, 0x00, 0x75, 0x05, 0x40, 0xB7, 0x01, 0xEB, 0x03, 0x40, 0x32, 0xFF, 0xF2, 0x44,
    0x0F, 0x10, 0x1D, 0, 0, 0, 0
};
constexpr char kMaskSimUpdateTick[] =
    "xxxx????xxxx????"
    "xxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxx"
    "xxx????";

// Verified unique secondary 1/30 & SetNetworkTickRate AOBs (1 match each in dontstarve_steam_x64.exe)
constexpr uint8_t kSigClientAnimShaderTick[] = {
    0x66, 0x0F, 0xEF, 0xF6, 0x48, 0x8B, 0x51, 0x28, 0x48, 0x8D, 0x8C, 0x24, 0x30, 0x02, 0x00, 0x00,
    0x8B, 0x82, 0x90, 0x00, 0x00, 0x00, 0xF3, 0x48, 0x0F, 0x2A, 0xF0,
    0x48, 0x8D, 0x84, 0x24, 0xC0, 0x01, 0x00, 0x00, 0x48, 0x89, 0x44, 0x24, 0x28,
    0xC7, 0x44, 0x24, 0x20, 0x04, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x35, 0, 0, 0, 0,
    0xF3, 0x0F, 0x58, 0xB2, 0x94, 0x00, 0x00, 0x00
};
constexpr char kMaskClientAnimShaderTick[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxx";

constexpr uint8_t kSigClientPathfinderTick[] = {
    0x49, 0x8B, 0x4D, 0x18, 0x66, 0x0F, 0xEF, 0xC0, 0x48, 0x8B, 0x91, 0xF0, 0x00, 0x00, 0x00,
    0x8B, 0x82, 0x90, 0x00, 0x00, 0x00, 0xF3, 0x48, 0x0F, 0x2A, 0xC0,
    0xF3, 0x0F, 0x59, 0x05, 0, 0, 0, 0, 0xF3, 0x0F, 0x58, 0x82, 0x94, 0x00, 0x00, 0x00
};
constexpr char kMaskClientPathfinderTick[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxx";

constexpr uint8_t kSigClientSpatialTick[] = {
    0x41, 0x8B, 0x80, 0x90, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0xEA, 0x4C, 0x8B, 0xE1, 0x49, 0x8B, 0xF1,
    0xF3, 0x48, 0x0F, 0x2A, 0xC0, 0xB8, 0x89, 0x88, 0x88, 0x88,
    0xF3, 0x0F, 0x59, 0x05, 0, 0, 0, 0, 0xF3, 0x41, 0x0F, 0x58, 0x80, 0x94, 0x00, 0x00, 0x00
};
constexpr char kMaskClientSpatialTick[] =
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxx";

constexpr uint8_t kSigClientSimTimeDiff[] = {
    0x8B, 0x41, 0x08, 0x66, 0x0F, 0xEF, 0xC0, 0x2B, 0x42, 0x08, 0xF3, 0x48, 0x0F, 0x2A, 0xC0,
    0xF3, 0x0F, 0x59, 0x05, 0, 0, 0, 0, 0xF3, 0x0F, 0x58, 0x41, 0x0C, 0xF3, 0x0F, 0x5C, 0x42, 0x0C, 0xC3
};
constexpr char kMaskClientSimTimeDiff[] =
    "xxxxxxxxxxxxxxxxxxx????xxxxxxxxxxx";

constexpr uint8_t kSigClientSetNetTickRate[] = {
    0x89, 0x54, 0x24, 0x10, 0x4C, 0x8B, 0xDC, 0x53, 0x55, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x50,
    0x48, 0x8B, 0xE9, 0x41, 0xBC, 0x0A, 0x00, 0x00, 0x00, 0x85, 0xD2, 0x74, 0x2B
};

constexpr uint8_t kSigClientNetTickScale[] = {
    0xF3, 0x0F, 0x10, 0x05, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0,
    0x48, 0x8B, 0x8D, 0x88, 0x02, 0x00, 0x00, 0x33, 0xD2, 0xF3, 0x48, 0x0F, 0x2C, 0xC0,
    0x41, 0xF7, 0xF4, 0x85, 0xD2, 0x49, 0x8B, 0xD6, 0x0F, 0x44, 0xF0, 0xE8, 0, 0, 0, 0,
    0x48, 0x8B, 0x8D, 0x88, 0x02, 0x00, 0x00, 0x40, 0x0F, 0xB6, 0xD6, 0xE8, 0, 0, 0, 0,
    0x48, 0x8B, 0x8D, 0x88, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xD7, 0xE8, 0, 0, 0, 0
};
constexpr char kMaskClientNetTickScale[] =
    "xxxx????x????"
    "xxxxxxxxxxxxxxxxxxxxxxxxxx????"
    "xxxxxxxxxxxx????"
    "xxxxxxxxxxx????";

constexpr uint8_t kSigClientSimUpdateNetMgr[] = {
    0x48, 0x8B, 0x0D, 0, 0, 0, 0, 0xE8, 0, 0, 0, 0, 0x84, 0xC0, 0x74, 0x0A,
    0xF3, 0x0F, 0x10, 0x05, 0, 0, 0, 0, 0xEB, 0x08, 0xF3, 0x0F, 0x10, 0x05, 0, 0, 0, 0,
    0xF3, 0x0F, 0x10, 0x35, 0, 0, 0, 0
};
constexpr char kMaskClientSimUpdateNetMgr[] =
    "xxx????x????xxxxxxxx????xxxxxx????xxxx????";

// Verified unique 1-match RakNet SendBuffered & SendBufferedList SetEvent guard AOBs
// Vanilla RakNet skips quitAndDataEvents.SetEvent() when priority != IMMEDIATE_PRIORITY (0),
// causing ALL HIGH_PRIORITY gameplay RPCs to stall 0-10ms in UpdateNetworkLoop::WaitOnEvent(10).
constexpr uint8_t kSigClientRakSendBufWake[] = {
    0x45, 0x85, 0xF6, 0x75, 0x0C, 0x48, 0x8D, 0x8D, 0xD0, 0x06, 0x00, 0x00, 0xE8
};
constexpr uint8_t kSigClientRakSendBufListWake[] = {
    0x85, 0xFF, 0x75, 0x0C, 0x49, 0x8D, 0x8D, 0xD0, 0x06, 0x00, 0x00, 0xE8
};

bool IsTargetDstClientProcess() {
    wchar_t exePath[MAX_PATH]{};
    DWORD n = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    const wchar_t* base = exePath;
    for (const wchar_t* p = exePath; *p; ++p) {
        if (*p == L'\\' || *p == L'/') base = p + 1;
    }
    return _wcsicmp(base, L"dontstarve_steam_x64.exe") == 0 ||
           _wcsicmp(base, L"dontstarve_steam.exe") == 0 ||
           _wcsicmp(base, L"dontstarve_rail_x64.exe") == 0 ||
           _wcsicmp(base, L"dontstarve_rail.exe") == 0;
}

void LogMsg(const char* fmt, ...) {
    if (g_logPath.empty()) return;
    AcquireSRWLockExclusive(&g_logLock);
    FILE* f = _wfopen(g_logPath.c_str(), L"a");
    if (f) {
        SYSTEMTIME st{};
        GetLocalTime(&st);
        fprintf(f, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fprintf(f, "\n");
        fclose(f);
    }
    ReleaseSRWLockExclusive(&g_logLock);
}

// Lock-free System32\dinput8.dll loader: eliminates InitOnceExecuteOnce <-> Loader Lock AB-BA deadlock
HMODULE LoadRealDInput8LockFree() {
    HMODULE h = g_realDInput8.load(std::memory_order_acquire);
    if (h) return h;
    wchar_t sysDir[MAX_PATH]{};
    UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH - 16) return nullptr;
    std::wstring path(sysDir, len);
    path += L"\\dinput8.dll";
    HMODULE loaded = LoadLibraryW(path.c_str());
    if (!loaded) return nullptr;
    HMODULE expected = nullptr;
    if (!g_realDInput8.compare_exchange_strong(expected, loaded,
                                               std::memory_order_release,
                                               std::memory_order_acquire)) {
        // Another thread raced and published first; avoid FreeLibrary here in case
        // this proxy export is invoked while another DLL holds the OS Loader Lock.
        return expected;
    }
    return loaded;
}

template <typename FnT>
FnT ResolveRealExport(std::atomic<FARPROC>& slot, const char* name) {
    FARPROC p = slot.load(std::memory_order_acquire);
    if (!p) {
        HMODULE h = LoadRealDInput8LockFree();
        if (!h) return nullptr;
        p = GetProcAddress(h, name);
        if (p) slot.store(p, std::memory_order_release);
    }
    return reinterpret_cast<FnT>(p);
}

bool GetTextSection(uint8_t*& outStart, size_t& outSize) {
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) return false;
    auto* base = reinterpret_cast<uint8_t*>(exe);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (memcmp(sec->Name, ".text", 5) == 0) {
            outStart = base + sec->VirtualAddress;
            outSize = sec->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

uint8_t* FindPatternInText(uint8_t* start, size_t size, const uint8_t* sig, size_t sigLen) {
    if (!start || size < sigLen) return nullptr;
    for (size_t off = 0; off <= size - sigLen; ++off) {
        if (start[off] == sig[0] && memcmp(start + off, sig, sigLen) == 0) {
            return start + off;
        }
    }
    return nullptr;
}

uint8_t* FindMaskedPatternInText(uint8_t* start, size_t size, const uint8_t* sig, const char* mask, size_t sigLen) {
    if (!start || size < sigLen) return nullptr;
    for (size_t off = 0; off <= size - sigLen; ++off) {
        if (mask[0] == 'x' && start[off] != sig[0]) continue;
        bool ok = true;
        for (size_t j = 1; j < sigLen; ++j) {
            if (mask[j] == 'x' && start[off + j] != sig[j]) { ok = false; break; }
        }
        if (ok) return start + off;
    }
    return nullptr;
}

uint8_t* ResolveRel32Call(uint8_t* callInstr) {
    if (!callInstr || (callInstr[0] != 0xE8 && callInstr[0] != 0xE9)) return nullptr;
    int32_t rel = 0;
    memcpy(&rel, callInstr + 1, 4);
    return callInstr + 5 + rel;
}

bool WriteCodeBytes(uint8_t* addr, const uint8_t* bytes, size_t len) {
    DWORD oldProt = 0;
    if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    memcpy(addr, bytes, len);
    FlushInstructionCache(GetCurrentProcess(), addr, len);
    DWORD dummy = 0;
    VirtualProtect(addr, len, oldProt, &dummy);
    return true;
}

// Strict W^X finalizer: flips newly written trampoline/stub from RWX -> RX
bool FinalizeExecPageWX(void* page, size_t len) {
    if (!page || len == 0) return false;
    FlushInstructionCache(GetCurrentProcess(), page, len);
    DWORD oldProt = 0;
    return VirtualProtect(page, len, PAGE_EXECUTE_READ, &oldProt) != 0;
}

struct PendingCodePatch {
    uint8_t*    addr = nullptr;
    uint8_t     bytes[64]{};
    size_t      len = 0;
    std::string logMsg;
    void*       ownedExecPage = nullptr;
    bool        applied = false;
};

struct SuspendedThreadInfo {
    HANDLE handle = nullptr;
    uintptr_t rip = 0;
};

// Cold-start RAII thread freezer: suspends sibling threads & captures RIP so .text patching
// can NEVER tear an instruction currently being executed by the main game thread.
class ThreadFreezeGuard {
public:
    ThreadFreezeGuard() {
        DWORD selfPid = GetCurrentProcessId();
        DWORD selfTid = GetCurrentThreadId();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) return;
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        if (Thread32First(snap, &te)) {
            do {
                if (te.th32OwnerProcessID == selfPid && te.th32ThreadID != selfTid) {
                    HANDLE ht = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                           FALSE, te.th32ThreadID);
                    if (ht) {
                        if (SuspendThread(ht) != static_cast<DWORD>(-1)) {
                            CONTEXT ctx{};
                            ctx.ContextFlags = CONTEXT_CONTROL;
                            uintptr_t rip = 0;
                            if (GetThreadContext(ht, &ctx)) {
                                rip = static_cast<uintptr_t>(ctx.Rip);
                            }
                            threads_.push_back({ht, rip});
                        } else {
                            CloseHandle(ht);
                        }
                    }
                }
            } while (Thread32Next(snap, &te));
        }
        CloseHandle(snap);
    }

    ~ThreadFreezeGuard() {
        for (auto& t : threads_) {
            ResumeThread(t.handle);
            CloseHandle(t.handle);
        }
    }

    bool AnyRipNear(uint8_t* addr, size_t len) const {
        uintptr_t lo = reinterpret_cast<uintptr_t>(addr) - 16;
        uintptr_t hi = reinterpret_cast<uintptr_t>(addr) + len + 16;
        for (const auto& t : threads_) {
            if (t.rip >= lo && t.rip <= hi) return true;
        }
        return false;
    }

private:
    std::vector<SuspendedThreadInfo> threads_;
};

void ApplyPendingPatchesSafely(std::vector<PendingCodePatch>& patches) {
    if (patches.empty()) return;
    // Publish all g_orig* function pointers & RX trampolines globally BEFORE patching any .text site
    std::atomic_thread_fence(std::memory_order_release);

    for (int attempt = 0; attempt < 5; ++attempt) {
        bool anyRemaining = false;
        {
            ThreadFreezeGuard freeze;
            for (auto& p : patches) {
                if (p.applied || !p.addr || p.len == 0) continue;
                if (freeze.AnyRipNear(p.addr, p.len)) {
                    anyRemaining = true;
                    continue;
                }
                if (WriteCodeBytes(p.addr, p.bytes, p.len)) {
                    p.applied = true;
                }
            }
        }
        if (!anyRemaining) break;
        Sleep(1);
    }

    for (auto& p : patches) {
        if (p.applied) {
            if (!p.logMsg.empty()) LogMsg("%s", p.logMsg.c_str());
        } else {
            LogMsg("SKIPPED patch @ %p (RIP collision or VirtualProtect failed; fail-closed)", static_cast<void*>(p.addr));
            if (p.ownedExecPage) {
                VirtualFree(p.ownedExecPage, 0, MEM_RELEASE);
                p.ownedExecPage = nullptr;
            }
        }
    }
}

//------------------------------------------------------------------------------
// libdeflate v1.23 Whole-Entry Accelerator for minizip unzReadCurrentFile()
//------------------------------------------------------------------------------
struct UnzReadInfo {
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

struct ThreadLocalDecomp {
    libdeflate_decompressor* decomp = nullptr;
    std::vector<uint8_t> compBuf;
    ~ThreadLocalDecomp() {
        if (decomp) { libdeflate_free_decompressor(decomp); decomp = nullptr; }
    }
};

extern "C" int FastUnzReadCurrentFile(void* file, void* buf, uint32_t len) {
    if (!file) return -102;
    auto* pinfo = *reinterpret_cast<UnzReadInfo**>(static_cast<uint8_t*>(file) + 0xC8);
    if (!pinfo || !pinfo->read_buffer || len == 0 || !buf) {
        return g_origUnzRead(file, buf, len);
    }

    uint32_t compSize = pinfo->rest_read_compressed;
    uint32_t uncompSize = pinfo->rest_read_uncompressed;

    if (pinfo->compression_method == 8 && pinfo->raw == 0 &&
        pinfo->avail_in == 0 && pinfo->total_out == 0 &&
        compSize > 0 && compSize <= (64u * 1024u * 1024u) &&
        uncompSize > 0 && uncompSize <= static_cast<uint32_t>(INT32_MAX) && len >= uncompSize &&
        pinfo->zseek_file && pinfo->zread_file) {

        thread_local ThreadLocalDecomp tls;
        if (!tls.decomp) tls.decomp = libdeflate_alloc_decompressor();
        if (tls.decomp) {
            if (tls.compBuf.size() < compSize) tls.compBuf.resize(compSize);
            uint32_t offset = pinfo->pos_in_zipfile + pinfo->byte_before_zipfile;
            if (pinfo->zseek_file(pinfo->opaque, pinfo->filestream, offset, 0) == 0) {
                if (pinfo->zread_file(pinfo->opaque, pinfo->filestream, tls.compBuf.data(), compSize) == compSize) {
                    size_t actualOut = 0;
                    if (libdeflate_deflate_decompress(tls.decomp, tls.compBuf.data(), compSize, buf, uncompSize, &actualOut) == LIBDEFLATE_SUCCESS &&
                        actualOut == uncompSize) {
                        pinfo->pos_in_zipfile += compSize;
                        pinfo->rest_read_compressed = 0;
                        pinfo->rest_read_uncompressed = 0;
                        pinfo->next_out = static_cast<uint8_t*>(buf) + uncompSize;
                        pinfo->avail_out = 0;
                        pinfo->total_out += uncompSize;
                        InterlockedIncrement64(&g_libdeflateHits);
                        InterlockedAdd64(&g_libdeflateBytes, static_cast<LONG64>(uncompSize));
                        return static_cast<int>(uncompSize);
                    }
                }
            }
        }
    } else if ((pinfo->compression_method == 0 || pinfo->raw != 0) &&
               pinfo->avail_in == 0 && uncompSize > 0 && uncompSize <= static_cast<uint32_t>(INT32_MAX) &&
               uncompSize == compSize && len >= uncompSize &&
               pinfo->zseek_file && pinfo->zread_file) {
        uint32_t offset = pinfo->pos_in_zipfile + pinfo->byte_before_zipfile;
        if (pinfo->zseek_file(pinfo->opaque, pinfo->filestream, offset, 0) == 0) {
            if (pinfo->zread_file(pinfo->opaque, pinfo->filestream, buf, uncompSize) == uncompSize) {
                pinfo->pos_in_zipfile += uncompSize;
                pinfo->rest_read_compressed = 0;
                pinfo->rest_read_uncompressed = 0;
                pinfo->next_out = static_cast<uint8_t*>(buf) + uncompSize;
                pinfo->avail_out = 0;
                pinfo->total_out += uncompSize;
                return static_cast<int>(uncompSize);
            }
        }
    }

    return g_origUnzRead(file, buf, len);
}

//------------------------------------------------------------------------------
// 64-bit Fast inflate_fast fallback (for non-zip / chunked callers)
//------------------------------------------------------------------------------
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
        while (len >= 16) { memcpy(out, from, 16); out += 16; from += 16; len -= 16; }
        if (len >= 8) { memcpy(out, from, 8); out += 8; from += 8; len -= 8; }
        while (len--) *out++ = *from++;
    } else if (dist >= 8) {
        while (len >= 8) { memcpy(out, from, 8); out += 8; from += 8; len -= 8; }
        while (len--) *out++ = *from++;
    } else if (dist == 1) {
        uint8_t b = *from;
        uint64_t v = static_cast<uint64_t>(b) * 0x0101010101010101ULL;
        while (len >= 8) { memcpy(out, &v, 8); out += 8; len -= 8; }
        while (len--) *out++ = b;
    } else {
        while (len--) *out++ = *from++;
    }
}

extern "C" void FastInflateFast(ZlibStream* strm, uint32_t start) {
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
            memcpy(&chunk, in, 4);
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
                    uint32_t chunk; memcpy(&chunk, in, 4);
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
                        if (back < len) { len -= back; memcpy(out, from, back); out += back; from = out - dist; }
                    } else if (write < back) {
                        from += wsize + write - back; back -= write;
                        if (back < len) {
                            len -= back; memcpy(out, from, back); out += back; from = window;
                            if (write < len) { uint32_t wlen = write; len -= wlen; memcpy(out, from, wlen); out += wlen; from = out - dist; }
                        }
                    } else {
                        from += write - back;
                        if (back < len) { len -= back; memcpy(out, from, back); out += back; from = out - dist; }
                    }
                    if (from >= window && from < window + wsize) { memcpy(out, from, len); out += len; }
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

//------------------------------------------------------------------------------
// Universal x86-64 True 240fps 2D Skeletal Animation Interpolation
// Covers ALL 3 channels: Main World + Overlay + UI AnimNode!
//------------------------------------------------------------------------------
struct AnimElement {
    uint32_t layer_hash;   // +0x00
    uint8_t  meta0[12];    // +0x04..+0x0F
    uint32_t symbol_hash;  // +0x10
    uint8_t  meta1[12];    // +0x14..+0x1F
    uint32_t frame_num;    // +0x20
    uint16_t m[6];         // +0x24..+0x2F: 6 IEEE-754 float16 [a, b, c, d, tx, ty]
};
static_assert(sizeof(AnimElement) == 48, "AnimElement must be 48 bytes");

struct AnimFrame {
    float        bbox[6];      // +0x00..+0x17
    AnimElement* elements;     // +0x18
    uint32_t     num_elements; // +0x20
    uint32_t     pad;          // +0x24
};
static_assert(sizeof(AnimFrame) == 40, "AnimFrame must be 40 bytes");

struct Animation {
    uint8_t    pad0[0x08];
    AnimFrame* frames;     // +0x08
    float      framerate;  // +0x10
    uint8_t    pad1[0x24];
    uint32_t   num_frames; // +0x38
    uint32_t   pad2;
    float      duration;   // +0x40
};

// Portable branchless IEEE-754 float16 <-> float32 (runs on 100% of x86-64 CPUs, zero F16C requirement!)
// Exact match with Klei's own half_to_float decoder @ 0x1400F3A8D.
static inline float HalfToFloat(uint16_t h) {
    uint32_t exp_mant = (h & 0x7FFFu);
    uint32_t bits = (h & 0x7C00u) ? ((exp_mant + 0x1C000u) << 13) : 0u;
    bits |= (static_cast<uint32_t>(h & 0x8000u) << 16);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static inline uint16_t FloatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 112;
    uint32_t mant = x & 0x007FFFFFu;
    if (exp <= 0) return static_cast<uint16_t>(sign);
    if (exp >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
    uint32_t rounded = (mant + 0x1000u) >> 13;
    if (rounded >= 0x400u) {
        rounded = 0;
        ++exp;
        if (exp >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | rounded);
}

static inline float WrapAngleDiff(float d) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    if (d > kPi) d -= kTwoPi;
    else if (d < -kPi) d += kTwoPi;
    return d;
}

// 3-Tier Ultra-Fast Float16 Matrix Interpolator (benchmarked 4.3x-5.1x faster on boss swarms):
//   Tier 0: Exact static bone (memcmp 12B == 0) -> 0 float ops, 0 transcendentals
//   Tier 1: Pure translation (memcmp 8B == 0)   -> 2 float lerps, 0 transcendentals
//   Tier 2: Small-angle 2x2 (<3.5 deg, err < f16 ULP) or fastOnly guard -> linear lerp, 0 trig
//   Tier 3: Large swings / boss slams           -> full polar decomposition (sqrt/atan2/cos/sin)
static inline bool TweenElementMatrix(const uint16_t m0h[6], const uint16_t m1h[6], float t,
                                      uint16_t outh[6], bool fastOnly = false) {
    // Tier 0: exact identical matrix (outh already copied from e0.m by caller)
    if (memcmp(m0h, m1h, 12) == 0) {
        return true;
    }

    const float tx0 = HalfToFloat(m0h[4]), ty0 = HalfToFloat(m0h[5]);
    const float tx1 = HalfToFloat(m1h[4]), ty1 = HalfToFloat(m1h[5]);
    const float dtx = tx1 - tx0;
    const float dty = ty1 - ty0;
    // Boss-scaled guard (420px): allows giant boss slams/lunges (Daywalker/Deerclops/Dragonfly)
    // while still blocking cross-screen teleports.
    if (fabsf(dtx) > 420.0f || fabsf(dty) > 420.0f) return false;

    // Tier 1: pure translation (2x2 linear part bit-identical)
    if (memcmp(m0h, m1h, 8) == 0) {
        outh[4] = FloatToHalf(tx0 + dtx * t);
        outh[5] = FloatToHalf(ty0 + dty * t);
        return true;
    }

    const float a0 = HalfToFloat(m0h[0]), b0 = HalfToFloat(m0h[1]);
    const float c0 = HalfToFloat(m0h[2]), d0 = HalfToFloat(m0h[3]);
    const float a1 = HalfToFloat(m1h[0]), b1 = HalfToFloat(m1h[1]);
    const float c1 = HalfToFloat(m1h[2]), d1 = HalfToFloat(m1h[3]);

    const float det0 = a0 * d0 - b0 * c0;
    const float det1 = a1 * d1 - b1 * c1;
    if (det0 * det1 < 0.0f) return false;

    const float da = a1 - a0, db = b1 - b0, dc = c1 - c0, dd = d1 - d0;

    // Tier 2: small-angle 2x2 delta (<= ~3.5 deg: linear error < 0.0005 < f16 ULP),
    // or graceful swarm fastOnly mode (< ~20 deg linear, skip heavy trig above)
    const float linThresh = fastOnly ? 0.35f : 0.06f;
    if (fabsf(da) < linThresh && fabsf(db) < linThresh && fabsf(dc) < linThresh && fabsf(dd) < linThresh) {
        outh[0] = FloatToHalf(a0 + da * t);
        outh[1] = FloatToHalf(b0 + db * t);
        outh[2] = FloatToHalf(c0 + dc * t);
        outh[3] = FloatToHalf(d0 + dd * t);
        outh[4] = FloatToHalf(tx0 + dtx * t);
        outh[5] = FloatToHalf(ty0 + dty * t);
        return true;
    }
    if (fastOnly) {
        return false; // Under extreme swarm load, keep f0 pose for huge swing instead of burning trig
    }

    // Tier 3: large weapon/boss rotation -> exact polar decomposition
    const float su0 = sqrtf(a0 * a0 + c0 * c0);
    const float su1 = sqrtf(a1 * a1 + c1 * c1);
    const float sv0 = sqrtf(b0 * b0 + d0 * d0);
    const float sv1 = sqrtf(b1 * b1 + d1 * d1);

    float a, b, c, d;
    if (su0 > 1e-4f && su1 > 1e-4f && sv0 > 1e-4f && sv1 > 1e-4f) {
        const float au0 = atan2f(c0, a0), au1 = atan2f(c1, a1);
        const float du = WrapAngleDiff(au1 - au0);
        const float av0 = atan2f(d0, b0), av1 = atan2f(d1, b1);
        const float dv = WrapAngleDiff(av1 - av0);
        // Allow fast weapon swings & boss attacks up to ~135 deg/tick (2.35 rad)
        if (fabsf(du) > 2.35f || fabsf(dv) > 2.35f) return false;

        const float su = su0 + (su1 - su0) * t, au = au0 + du * t;
        const float sv = sv0 + (sv1 - sv0) * t, av = av0 + dv * t;
        a = su * cosf(au); c = su * sinf(au);
        b = sv * cosf(av); d = sv * sinf(av);
    } else {
        a = a0 + da * t; b = b0 + db * t;
        c = c0 + dc * t; d = d0 + dd * t;
    }

    outh[0] = FloatToHalf(a);
    outh[1] = FloatToHalf(b);
    outh[2] = FloatToHalf(c);
    outh[3] = FloatToHalf(d);
    outh[4] = FloatToHalf(tx0 + dtx * t);
    outh[5] = FloatToHalf(ty0 + dty * t);
    return true;
}

constexpr uint32_t kMaxAnimElements = 512;

struct AnimBufferSlot {
    AnimFrame frame{};
    AnimElement elems[kMaxAnimElements];
};

struct EntityAnimTrack {
    uintptr_t   entity_key = 0;
    const void* anim = nullptr;
    float       last_time = -1.0f;
    float       prev_f = 0.0f;
    float       curr_f = 0.0f;
    float       last_rendered_f = 0.0f;
    int64_t     tick_qpc = 0;       // exact wall timestamp of last time change (for EMA period)
    int64_t     seg_start_qpc = 0;  // prev render frame timestamp -> eliminates dt=0 duplicate pose!
    int64_t     seen_qpc = 0;
    float       tick_period = 1.0f / 30.0f;
    uint32_t    sim_gen = 0;
    uint8_t     valid = 0;
};

// Render-only 240FPS Entity World-Position Interpolation Track (per persistentAnimNode)
// Eliminates discrete 15Hz/30Hz/60Hz sawtooth/judder for Lag-Comp-OFF player, ALL monsters,
// remote players, and projectiles — while auto-bypassing continuous 240FPS Bullet physics
// (Lag-Comp-ON local player) with 0.0ms added latency!
struct EntityPosTrack {
    uintptr_t entity_key = 0;
    float     last_raw_x = 0.0f, last_raw_y = 0.0f, last_raw_z = 0.0f;
    float     prev_x = 0.0f, prev_y = 0.0f, prev_z = 0.0f;
    float     curr_x = 0.0f, curr_y = 0.0f, curr_z = 0.0f;
    float     rend_x = 0.0f, rend_y = 0.0f, rend_z = 0.0f;
    int64_t   update_qpc = 0;       // exact wall timestamp of last raw pos change (for EMA & fast_streak)
    int64_t   seg_start_qpc = 0;    // prev render frame timestamp -> eliminates dt=0 duplicate frame!
    int64_t   seen_qpc = 0;
    float     period = 1.0f / 30.0f;
    uint32_t  sim_gen = 0;
    uint8_t   fast_streak = 0;
    uint8_t   settled = 1;
    uint8_t   valid = 0;
};

constexpr size_t kTrackSlots = 8192;

enum class AnimChannel : uint8_t { Main = 0, Overlay = 1, UI = 2 };

struct ThreadLocalAnimState {
    // Compact L1-hot fixed-array ring pools (verified AnimRenderer::Render @ 0x1400F4920..0x1400F59C6
    // consumes mainSlots completely before next entity -> shrinking 16->2 eliminates 393KB stride thrash!):
    AnimBufferSlot mainSlots[2];
    uint32_t mainRing = 0;
    AnimBufferSlot overlaySlots[8];
    uint32_t overlayRing = 0;
    AnimBufferSlot uiSlots[2];
    uint32_t uiRing = 0;
    std::vector<EntityAnimTrack> tracks;
    std::vector<EntityPosTrack> posTracks;

    // Frame-aware QPC caching, zero-LOCK batched counters & swarm hitch-proof load guard
    uint32_t lastFrameSeq = 0xFFFFFFFFu;
    int64_t  cachedQpc = 0;
    int64_t  frameAnimStartQpc = 0;
    int64_t  frameHookAccumQpc = 0;
    uint32_t qpcCountdown = 0;
    uint32_t frameTweenEnts = 0;
    uint32_t frameTweenBones = 0;
    uint32_t frameShedCount = 0;
    uint8_t  frameShedLevel = 0; // 0=full quality, 1=fast linear only, 2=pos240-only skeletal shed

    int64_t  batchInterpFrames = 0;
    int64_t  batchElemsTweened = 0;
    int64_t  batchPass2Swaps = 0;
    int64_t  batchPosInterp = 0;
    int64_t  batchPosBypass = 0;
    int64_t  batchShedFrames = 0;

    ThreadLocalAnimState() : tracks(kTrackSlots), posTracks(kTrackSlots) {}

    AnimBufferSlot& NextSlot(AnimChannel ch) {
        if (ch == AnimChannel::Main) return mainSlots[(mainRing++) & 1u];
        if (ch == AnimChannel::Overlay) return overlaySlots[(overlayRing++) & 7u];
        return uiSlots[(uiRing++) & 1u];
    }

    void FlushCounters() {
        if (batchInterpFrames) { InterlockedAdd64(&g_animInterpFrames, batchInterpFrames); batchInterpFrames = 0; }
        if (batchElemsTweened) { InterlockedAdd64(&g_animElemsTweened, batchElemsTweened); batchElemsTweened = 0; }
        if (batchPass2Swaps)   { InterlockedAdd64(&g_animPass2Swaps, batchPass2Swaps);     batchPass2Swaps = 0; }
        if (batchPosInterp)    { InterlockedAdd64(&g_worldPosInterpCount, batchPosInterp); batchPosInterp = 0; }
        if (batchPosBypass)    { InterlockedAdd64(&g_worldPosBypassCount, batchPosBypass); batchPosBypass = 0; }
        if (batchShedFrames)   { InterlockedAdd64(&g_animSwarmShedFrames, batchShedFrames); batchShedFrames = 0; }
    }

    int64_t GetFrameQpc() {
        const uint32_t curSeq = g_frameSeq.load(std::memory_order_relaxed);
        if (curSeq != lastFrameSeq || cachedQpc == 0) {
            const int64_t now = NowQpc();
            if (frameAnimStartQpc != 0) {
                const uint32_t hookUs = static_cast<uint32_t>(
                    std::clamp(static_cast<double>(frameHookAccumQpc) * g_qpcToUs, 0.0, 500000.0));
                g_lastAnimHookUs.store(hookUs, std::memory_order_relaxed);
                g_lastAnimEnts.store(frameTweenEnts, std::memory_order_relaxed);
                g_lastAnimBones.store(frameTweenBones, std::memory_order_relaxed);
                g_lastAnimShed.store(frameShedCount, std::memory_order_relaxed);
            }
            FlushCounters();
            lastFrameSeq = curSeq;
            cachedQpc = now;
            frameAnimStartQpc = now;
            frameHookAccumQpc = 0;
            qpcCountdown = 16;
            frameTweenEnts = 0;
            frameTweenBones = 0;
            frameShedCount = 0;
            frameShedLevel = 0;
            return now;
        }
        if (qpcCountdown == 0) {
            const int64_t now = NowQpc();
            cachedQpc = now;
            qpcCountdown = 16;
            // Safety reset if g_frameSeq hasn't ticked for >10ms (e.g. loading / paused screen)
            const double spanUs = static_cast<double>(now - frameAnimStartQpc) * g_qpcToUs;
            if (spanUs > 10000.0) {
                FlushCounters();
                frameAnimStartQpc = now;
                frameHookAccumQpc = 0;
                frameTweenEnts = 0;
                frameTweenBones = 0;
                frameShedCount = 0;
                frameShedLevel = 0;
            } else {
                const double workUs = static_cast<double>(frameHookAccumQpc) * g_qpcToUs;
                if (workUs > 1500.0 || frameTweenEnts >= 480) {
                    frameShedLevel = 2;
                } else if (workUs > 850.0 || frameTweenEnts >= 240) {
                    frameShedLevel = 1;
                }
            }
        } else {
            --qpcCountdown;
        }
        return cachedQpc;
    }
};

thread_local uintptr_t g_tlsLastParentKey = 0;
thread_local std::unique_ptr<ThreadLocalAnimState> g_tlsAnimState;

static inline ThreadLocalAnimState& GetTlsAnimState() {
    if (!g_tlsAnimState) g_tlsAnimState.reset(new ThreadLocalAnimState());
    return *g_tlsAnimState;
}

// Render-only 240FPS world-position interpolator operating on freshly allocated per-frame
// AnimRenderCmd (verified @ 0x1400FADE0) BEFORE AnimRenderer::Render reads matrix [cmd+0x18].
// Zero gameplay/hitbox/physics mutation; 100% safe for Hats/FollowSymbol/Boats/Teleports.
static void InterpolateAnimCmdWorldPos(uint8_t* cmd, uintptr_t key, ThreadLocalAnimState& tls, int64_t nowQpc) {
    if (!cmd || key == 0) return;
    if (g_worldPosInterpEnabled.load(std::memory_order_relaxed) == 0 ||
        g_skeletalTweenEnabled.load(std::memory_order_relaxed) == 0) {
        return;
    }
    // Skip screen-space / HUD / background layer commands ([cmd+0x60] == 8 @ 0x1400F49C9)
    const uint32_t orientMode = *reinterpret_cast<const uint32_t*>(cmd + 0x60);
    if (orientMode == 8u) return;

    // 4x4 row-major World Transform Matrix at [cmd + 0x18]:
    // Translation column: X = +0x24 (+0x0C), Y = +0x34 (+0x1C), Z = +0x44 (+0x2C)
    float* pX = reinterpret_cast<float*>(cmd + 0x24);
    float* pY = reinterpret_cast<float*>(cmd + 0x34);
    float* pZ = reinterpret_cast<float*>(cmd + 0x44);
    const float rawX = *pX;
    const float rawY = *pY;
    const float rawZ = *pZ;
    if (!std::isfinite(rawX) || !std::isfinite(rawY) || !std::isfinite(rawZ)) return;

    const uint32_t curSimGen = g_clientSimTickGen.load(std::memory_order_relaxed);
    const int curSimHz = g_clientSimTickRate.load(std::memory_order_relaxed);

    // 4-probe open-addressing lookup with LRU eviction
    const size_t baseSlot = ((key >> 4) ^ (key >> 15) ^ (key >> 27)) & (kTrackSlots - 1);
    EntityPosTrack* trPtr = nullptr;
    EntityPosTrack* lruPtr = &tls.posTracks[baseSlot];
    for (size_t p = 0; p < 4; ++p) {
        EntityPosTrack& cand = tls.posTracks[(baseSlot + p) & (kTrackSlots - 1)];
        if (cand.valid && cand.entity_key == key) { trPtr = &cand; break; }
        if (!cand.valid) { trPtr = &cand; break; }
        if (cand.seen_qpc < lruPtr->seen_qpc) lruPtr = &cand;
    }
    if (!trPtr) trPtr = lruPtr;
    EntityPosTrack& tr = *trPtr;

    const int64_t prevSeenQpc = tr.seen_qpc;
    const double unseenSec = (tr.valid && tr.entity_key == key)
        ? (static_cast<double>(nowQpc - prevSeenQpc) * g_qpcInvFreq)
        : 999.0;
    tr.seen_qpc = nowQpc;

    auto snapToRaw = [&]() {
        tr.entity_key = key;
        tr.last_raw_x = tr.prev_x = tr.curr_x = tr.rend_x = rawX;
        tr.last_raw_y = tr.prev_y = tr.curr_y = tr.rend_y = rawY;
        tr.last_raw_z = tr.prev_z = tr.curr_z = tr.rend_z = rawZ;
        tr.update_qpc = nowQpc;
        tr.seg_start_qpc = nowQpc;
        tr.settled = 1;
        tr.valid = 1;
    };

    if (!tr.valid || tr.entity_key != key || unseenSec > 0.16) {
        tr.period = (curSimHz == 60) ? (1.0f / 60.0f) : (1.0f / 30.0f);
        tr.sim_gen = curSimGen;
        tr.fast_streak = 0;
        snapToRaw();
        return;
    }
    if (tr.sim_gen != curSimGen) {
        tr.period = (curSimHz == 60) ? (1.0f / 60.0f) : (1.0f / 30.0f);
        tr.sim_gen = curSimGen;
        tr.fast_streak = 0;
    }

    const bool rawChanged = (rawX != tr.last_raw_x || rawY != tr.last_raw_y || rawZ != tr.last_raw_z);
    if (!rawChanged && tr.settled) {
        return; // Ultra-fast static entity path (trees, walls, chests, idle entities)
    }

    if (rawChanged) {
        const float dx = rawX - tr.last_raw_x;
        const float dy = rawY - tr.last_raw_y;
        const float dz = rawZ - tr.last_raw_z;
        const float distSq = dx * dx + dy * dy + dz * dz;
        const float elapsed = static_cast<float>(static_cast<double>(nowQpc - tr.update_qpc) * g_qpcInvFreq);

        // Auto-detect continuous 240FPS local Bullet physics (Lag Comp ON):
        // Updates arriving faster than 9ms (>=111Hz) bypass interpolation with 0.0ms added latency!
        if (elapsed > 0.0f && elapsed < 0.009f) {
            if (tr.fast_streak < 8) ++tr.fast_streak;
        } else {
            tr.fast_streak = 0;
        }

        // Teleport / wormhole / Lazy Explorer / respawn guard (> 5.0 tiles/step) or Lag-Comp-ON bypass
        if (!std::isfinite(distSq) || distSq > 25.0f || distSq < 1e-10f || tr.fast_streak >= 2) {
            if (tr.fast_streak >= 2) {
                ++tls.batchPosBypass;
            }
            snapToRaw();
            return;
        }

        // Adaptive snapshot cadence EMA (auto-locks onto 16.7ms / 33.3ms / 66.7ms per entity!)
        if (elapsed >= 0.011f && elapsed <= 0.110f) {
            tr.period = tr.period * 0.72f + elapsed * 0.28f;
            if (tr.period < 0.015f) tr.period = 0.015f;
            if (tr.period > 0.095f) tr.period = 0.095f;
        }

        // C0-continuous handoff from exact last rendered pixel (eliminates early/late arrival pops)
        // with anti-lag-runaway clamp (never falls > 1.5 steps behind during packet bursts)
        float startX = tr.rend_x;
        float startY = tr.rend_y;
        float startZ = tr.rend_z;
        const float lx = tr.last_raw_x - startX;
        const float ly = tr.last_raw_y - startY;
        const float lz = tr.last_raw_z - startZ;
        const float lagSq = lx * lx + ly * ly + lz * lz;
        if (lagSq > distSq * 2.25f) {
            startX = (startX + tr.last_raw_x) * 0.5f;
            startY = (startY + tr.last_raw_y) * 0.5f;
            startZ = (startZ + tr.last_raw_z) * 0.5f;
        }

        tr.prev_x = startX; tr.prev_y = startY; tr.prev_z = startZ;
        tr.curr_x = rawX;   tr.curr_y = rawY;   tr.curr_z = rawZ;
        tr.last_raw_x = rawX; tr.last_raw_y = rawY; tr.last_raw_z = rawZ;
        tr.update_qpc = nowQpc;
        // Anchor segment start to prev render frame (widened to 24ms & clamped to 0.55*period so
        // brief 150fps / 15ms swarm dips NEVER amplify into a 2-frame double-pose judder!)
        if (prevSeenQpc > 0 && unseenSec > 0.0 && unseenSec <= 0.024) {
            const double maxBackSec = static_cast<double>(tr.period) * 0.55;
            const double backSec = (unseenSec < maxBackSec) ? unseenSec : maxBackSec;
            tr.seg_start_qpc = nowQpc - static_cast<int64_t>(backSec * static_cast<double>(g_qpcFreq));
        } else {
            tr.seg_start_qpc = nowQpc;
        }
        tr.settled = 0;
    }

    float effPeriod = tr.period * 1.03f; // +3% jitter cushion avoids 1-frame micro-stalls on +/-0.5ms net jitter
    if (effPeriod < 0.015f) effPeriod = 0.015f;
    if (effPeriod > 0.098f) effPeriod = 0.098f;

    const float dt = static_cast<float>(static_cast<double>(nowQpc - tr.seg_start_qpc) * g_qpcInvFreq);
    float alpha = dt / effPeriod;
    if (!std::isfinite(alpha) || alpha >= 1.0f) {
        tr.rend_x = tr.curr_x;
        tr.rend_y = tr.curr_y;
        tr.rend_z = tr.curr_z;
        tr.settled = 1;
        return; // Exact raw coordinate reached -> zero drift
    }
    if (alpha < 0.0f) alpha = 0.0f;

    tr.rend_x = tr.prev_x + (tr.curr_x - tr.prev_x) * alpha;
    tr.rend_y = tr.prev_y + (tr.curr_y - tr.prev_y) * alpha;
    tr.rend_z = tr.prev_z + (tr.curr_z - tr.prev_z) * alpha;
    *pX = tr.rend_x;
    *pY = tr.rend_y;
    *pZ = tr.rend_z;
    ++tls.batchPosInterp;
}

static const void* InterpGetFrameCore(const void* animPtr, int32_t playMode, float time, uintptr_t key,
                                      AnimChannel channel, ThreadLocalAnimState* optTls = nullptr,
                                      int64_t optNowQpc = 0) {
    // Always refresh parent key BEFORE any early return (prevents stale overlay keys on 1-frame / disabled entities)
    if (channel == AnimChannel::Main && key != 0) {
        g_tlsLastParentKey = key;
    }
    if (!animPtr || !g_origAnimGetFrame) {
        return g_origAnimGetFrame ? g_origAnimGetFrame(animPtr, playMode, time) : nullptr;
    }
    if (g_skeletalTweenEnabled.load(std::memory_order_relaxed) == 0) {
        return g_origAnimGetFrame(animPtr, playMode, time);
    }

    const auto* anim = static_cast<const Animation*>(animPtr);
    uint32_t nframes = anim->num_frames;
    float fps = anim->framerate;
    if (nframes <= 1 || !anim->frames || !std::isfinite(fps) || !std::isfinite(time) || fps <= 0.0f || fps > 240.0f) {
        return g_origAnimGetFrame(animPtr, playMode, time);
    }

    if (key == 0) {
        return g_origAnimGetFrame(animPtr, playMode, time);
    }

    ThreadLocalAnimState& tls = optTls ? *optTls : GetTlsAnimState();
    const int64_t nowQpc = (optNowQpc != 0) ? optNowQpc : tls.GetFrameQpc();

    // Compute continuous floating-point frame coordinate (works for 30fps, 40fps,
    // SetDeltaTimeMultiplier speed scaling, looping, one-shot, and percentage animations).
    float nf = static_cast<float>(nframes);
    float curF = 0.0f;
    if (playMode == 1) {
        float t = (time < 0.0f) ? 0.0f : time;
        curF = fmodf(t * fps, nf);
        if (curF < 0.0f) curF += nf;
    } else if (playMode == 0) {
        float t = (time < 0.0f) ? 0.0f : time;
        curF = t * fps;
        if (curF > nf - 1.0f) curF = nf - 1.0f;
    } else if (playMode == 2) {
        float c = (time < 0.0f) ? 0.0f : ((time > 1.0f) ? 1.0f : time);
        curF = c * (nf - 1.0f);
    } else {
        return g_origAnimGetFrame(animPtr, playMode, time);
    }
    if (!std::isfinite(curF)) {
        return g_origAnimGetFrame(animPtr, playMode, time);
    }

    // 4-probe open-addressing lookup with LRU eviction (0% hash-collision eviction in megabases)
    size_t baseSlot = ((key >> 4) ^ (key >> 15) ^ (key >> 27)) & (kTrackSlots - 1);
    EntityAnimTrack* trPtr = nullptr;
    EntityAnimTrack* lruPtr = &tls.tracks[baseSlot];
    for (size_t p = 0; p < 4; ++p) {
        EntityAnimTrack& cand = tls.tracks[(baseSlot + p) & (kTrackSlots - 1)];
        if (cand.valid && cand.entity_key == key) { trPtr = &cand; break; }
        if (!cand.valid) { trPtr = &cand; break; }
        if (cand.seen_qpc < lruPtr->seen_qpc) lruPtr = &cand;
    }
    if (!trPtr) trPtr = lruPtr;
    EntityAnimTrack& tr = *trPtr;
    const int64_t prevAnimSeenQpc = tr.seen_qpc;
    const double animUnseenSec = (tr.valid && tr.entity_key == key)
        ? (static_cast<double>(nowQpc - prevAnimSeenQpc) * g_qpcInvFreq)
        : 999.0;
    tr.seen_qpc = nowQpc;

    auto discreteFrame = [&]() -> const void* {
        float safeF = (curF >= 0.0f && std::isfinite(curF)) ? curF : 0.0f;
        uint32_t fi = static_cast<uint32_t>(safeF + 0.001f);
        if (fi >= nframes) fi = (playMode == 1) ? (fi % nframes) : (nframes - 1);
        return &anim->frames[fi];
    };

    const uint32_t curSimGen = g_clientSimTickGen.load(std::memory_order_relaxed);
    const int curSimHz = g_clientSimTickRate.load(std::memory_order_relaxed);
    if (!tr.valid || tr.entity_key != key || tr.anim != animPtr) {
        tr.entity_key = key;
        tr.anim = animPtr;
        tr.last_time = time;
        tr.prev_f = curF;
        tr.curr_f = curF;
        tr.last_rendered_f = curF;
        tr.tick_qpc = nowQpc;
        tr.seg_start_qpc = nowQpc;
        tr.tick_period = (curSimHz == 60) ? (1.0f / 60.0f) : (1.0f / 30.0f);
        tr.sim_gen = curSimGen;
        tr.valid = 1;
        return discreteFrame();
    }
    if (tr.sim_gen != curSimGen) {
        tr.tick_period = (curSimHz == 60) ? (1.0f / 60.0f) : (1.0f / 30.0f);
        tr.sim_gen = curSimGen;
    }

    if (time != tr.last_time) {
        float oldF = tr.curr_f;
        float df = curF - oldF;
        if (playMode == 1 && df < 0.0f) {
            df += nf;
        }
        float elapsed = static_cast<float>(static_cast<double>(nowQpc - tr.tick_qpc) * g_qpcInvFreq);
        bool smoothStep = (df > 0.01f && df <= 4.5f) ||
                          (playMode == 2 && fabsf(df) > 0.01f && fabsf(df) <= 4.5f);
        if (smoothStep) {
            if (elapsed > 0.005f && elapsed < 0.120f) {
                tr.tick_period = tr.tick_period * 0.7f + elapsed * 0.3f;
            }
            // C0-continuous skeletal handoff from last_rendered_f (prevents micro-pops on jitter)
            float handoffF = oldF;
            float dfRend = curF - tr.last_rendered_f;
            if (playMode == 1 && dfRend < 0.0f) dfRend += nf;
            if ((dfRend > 0.01f && dfRend <= 4.5f) ||
                (playMode == 2 && fabsf(dfRend) > 0.01f && fabsf(dfRend) <= 4.5f)) {
                handoffF = tr.last_rendered_f;
            }
            tr.prev_f = handoffF;
            tr.curr_f = curF;
            // Anchor segment to previous render frame timestamp (widened to 24ms & clamped to 0.55*period)
            if (prevAnimSeenQpc > 0 && animUnseenSec > 0.0 && animUnseenSec <= 0.024) {
                const double maxBackSec = static_cast<double>(tr.tick_period) * 0.55;
                const double backSec = (animUnseenSec < maxBackSec) ? animUnseenSec : maxBackSec;
                tr.seg_start_qpc = nowQpc - static_cast<int64_t>(backSec * static_cast<double>(g_qpcFreq));
            } else {
                tr.seg_start_qpc = nowQpc;
            }
        } else {
            tr.prev_f = curF;
            tr.curr_f = curF;
            tr.last_rendered_f = curF;
            tr.seg_start_qpc = nowQpc;
        }
        tr.last_time = time;
        tr.tick_qpc = nowQpc;
    }

    float stepF = tr.curr_f - tr.prev_f;
    if (playMode == 1 && stepF < 0.0f) stepF += nf;
    if (!std::isfinite(stepF) || fabsf(stepF) < 0.001f) {
        tr.last_rendered_f = curF;
        return discreteFrame();
    }

    float dtSinceTick = static_cast<float>(static_cast<double>(nowQpc - tr.seg_start_qpc) * g_qpcInvFreq);
    float alpha = (tr.tick_period > 1e-4f) ? (dtSinceTick / tr.tick_period) : 1.0f;
    if (!std::isfinite(alpha) || alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    float fNow = tr.prev_f + stepF * alpha;
    if (!std::isfinite(fNow)) {
        tr.last_rendered_f = curF;
        return discreteFrame();
    }
    if (playMode == 1) {
        fNow = fmodf(fNow, nf);
        if (fNow < 0.0f) fNow += nf;
    } else if (fNow < 0.0f) {
        fNow = 0.0f;
    } else if (fNow > nf - 1.0f) {
        fNow = nf - 1.0f;
    }
    tr.last_rendered_f = fNow;

    // Level-2 Swarm Hitch Guard: under extreme 500+ entity storms, gracefully shed excess skeletal
    // bone blending for this frame while keeping 100% 240FPS world-position interpolation active!
    if (tls.frameShedLevel >= 2 && channel != AnimChannel::UI) {
        ++tls.frameShedCount;
        ++tls.batchShedFrames;
        return discreteFrame();
    }

    uint32_t idx0 = 0, idx1 = 0;
    float frac = 0.0f;

    if (playMode == 1) {
        if (!std::isfinite(fNow) || fNow < 0.0f) fNow = 0.0f;
        float fl = floorf(fNow);
        idx0 = static_cast<uint32_t>(fl) % nframes;
        idx1 = (idx0 + 1) % nframes;
        frac = fNow - fl;
    } else {
        if (fNow < 0.0f) fNow = 0.0f;
        if (fNow >= nf - 1.0f) return &anim->frames[nframes - 1];
        float fl = floorf(fNow);
        idx0 = static_cast<uint32_t>(fl);
        idx1 = idx0 + 1;
        frac = fNow - fl;
    }

    if (idx0 >= nframes) idx0 = nframes - 1;
    if (idx1 >= nframes) idx1 = nframes - 1;
    if (frac <= 0.015f) return &anim->frames[idx0];
    if (frac >= 0.985f) return &anim->frames[idx1];
    if (idx0 == idx1) {
        return &anim->frames[idx0];
    }

    const AnimFrame& f0 = anim->frames[idx0];
    const AnimFrame& f1 = anim->frames[idx1];
    uint32_t n0 = f0.num_elements, n1 = f1.num_elements;
    if (n0 == 0 || !f0.elements || n1 == 0 || !f1.elements || n0 > kMaxAnimElements) return &f0;

    const int64_t workStartQpc = ((tls.frameTweenEnts & 15u) == 0) ? NowQpc() : 0;

    AnimBufferSlot& bufSlot = tls.NextSlot(channel);
    bufSlot.frame = f0;
    bufSlot.frame.elements = bufSlot.elems;
    memcpy(bufSlot.elems, f0.elements, n0 * sizeof(AnimElement));

    const bool fastOnly = (tls.frameShedLevel >= 1 && channel != AnimChannel::UI);
    const bool useHash = (n0 > 4 && n1 <= kMaxAnimElements);

    // L1-resident O(N) stack hash table + duplicate-bucket spatial nearest-neighbor guard.
    // Unique bones (99% of characters/Bosses) match in O(1); multi-instance duplicate-key
    // elements (e.g. fireflies, spores, sparks, petals) match by nearest spatial distance
    // (<=48px) with exclusive usedF1 bitset so dying/spawning particles NEVER streak in a line!
    constexpr uint32_t kHashSize = 128;
    constexpr uint32_t kHashMask = kHashSize - 1;
    constexpr float kDupMaxDistSq = 48.0f * 48.0f;
    int16_t headLS[kHashSize];
    int16_t nextLS[kMaxAnimElements];
    uint8_t f0BucketCnt[kHashSize];
    uint64_t usedF1[4] = { 0, 0, 0, 0 };

    if (useHash) {
        memset(headLS, -1, sizeof(headLS));
        memset(f0BucketCnt, 0, sizeof(f0BucketCnt));
        for (uint32_t k = 0; k < n0; ++k) {
            const uint32_t h = ((f0.elements[k].layer_hash * 2654435761u) ^ f0.elements[k].symbol_hash) & kHashMask;
            if (f0BucketCnt[h] < 255) ++f0BucketCnt[h];
        }
        // Reverse-insert f1 so bucket chains traverse in strict ascending index order (0, 1, 2, ...)
        for (int32_t j = static_cast<int32_t>(n1) - 1; j >= 0; --j) {
            const uint32_t h = ((f1.elements[j].layer_hash * 2654435761u) ^ f1.elements[j].symbol_hash) & kHashMask;
            nextLS[j] = headLS[h];
            headLS[h] = static_cast<int16_t>(j);
        }
    }

    auto isUsedF1 = [&](uint32_t j) -> bool {
        return j < kMaxAnimElements && ((usedF1[j >> 6] >> (j & 63)) & 1ULL) != 0;
    };
    auto markUsedF1 = [&](uint32_t j) {
        if (j < kMaxAnimElements) usedF1[j >> 6] |= (1ULL << (j & 63));
    };

    uint32_t tweenedCount = 0;
    uint32_t pass2Count = 0;
    for (uint32_t i = 0; i < n0; ++i) {
        const AnimElement& e0 = f0.elements[i];
        const uint32_t lhash = e0.layer_hash, shash = e0.symbol_hash, fnum = e0.frame_num;

        const AnimElement* match = nullptr;
        bool isPass2 = false;

        if (useHash) {
            const uint32_t h = ((lhash * 2654435761u) ^ shash) & kHashMask;
            uint32_t cnt0LS = 0, cnt0Exact = 0;
            if (f0BucketCnt[h] > 1) {
                for (uint32_t k = 0; k < n0; ++k) {
                    const AnimElement& ek = f0.elements[k];
                    if (ek.symbol_hash == shash && ek.layer_hash == lhash) {
                        ++cnt0LS;
                        if (ek.frame_num == fnum) ++cnt0Exact;
                    }
                }
            } else {
                cnt0LS = 1;
                cnt0Exact = 1;
            }

            int32_t firstJ = -1, exactJ = -1;
            uint32_t cnt1LS = 0, cnt1Exact = 0;
            for (int16_t j = headLS[h]; j >= 0; j = nextLS[j]) {
                const AnimElement& e1 = f1.elements[j];
                if (e1.symbol_hash == shash && e1.layer_hash == lhash) {
                    if (firstJ < 0) firstJ = j;
                    ++cnt1LS;
                    if (e1.frame_num == fnum) {
                        if (exactJ < 0) exactJ = j;
                        ++cnt1Exact;
                    }
                }
            }

            if (firstJ >= 0) {
                if (cnt0LS == 1 && cnt1LS == 1) {
                    // Fast O(1) unique (layer, symbol) 1-to-1 bone match (full 420px Boss sweep range)
                    const AnimElement& e1 = f1.elements[firstJ];
                    if (e1.frame_num == fnum) {
                        match = &e1;
                    } else if (!fastOnly) {
                        match = &e1;
                        isPass2 = true;
                    }
                } else if (cnt0Exact == 1 && cnt1Exact == 1 && !isUsedF1(static_cast<uint32_t>(exactJ))) {
                    // Unique (layer, symbol, frame_num) inside shared-symbol rig (e.g. evergreen tree
                    // pieces fn=15/16/17/18 or co-located trunk fn=0/1): exact 1-to-1 match, never swap!
                    markUsedF1(static_cast<uint32_t>(exactJ));
                    match = &f1.elements[exactJ];
                } else {
                    // Multi-instance duplicate-key particle/rig bucket (fireflies/sparks/snow/spores):
                    // Decode FP16 HalfToFloat(m[4..5]) for true Euclidean nearest-neighbor matching!
                    const float tx0 = HalfToFloat(e0.m[4]);
                    const float ty0 = HalfToFloat(e0.m[5]);
                    int32_t bestJ = -1;
                    float bestScore = 1e30f;
                    for (int16_t j = headLS[h]; j >= 0; j = nextLS[j]) {
                        if (isUsedF1(static_cast<uint32_t>(j))) continue;
                        const AnimElement& e1 = f1.elements[j];
                        if (e1.symbol_hash == shash && e1.layer_hash == lhash) {
                            const float dx = HalfToFloat(e1.m[4]) - tx0;
                            const float dy = HalfToFloat(e1.m[5]) - ty0;
                            const float d2 = dx * dx + dy * dy;
                            if (d2 <= kDupMaxDistSq) {
                                const float score = d2 + ((e1.frame_num == fnum) ? 0.0f : 4.0f);
                                if (score < bestScore) {
                                    bestScore = score;
                                    bestJ = j;
                                }
                            }
                        }
                    }
                    if (bestJ >= 0) {
                        markUsedF1(static_cast<uint32_t>(bestJ));
                        const AnimElement& e1 = f1.elements[bestJ];
                        if (e1.frame_num == fnum) {
                            match = &e1;
                        } else if (!fastOnly) {
                            match = &e1;
                            isPass2 = true;
                        }
                    }
                }
            }
        } else {
            uint32_t cnt0LS = 0, cnt0Exact = 0;
            for (uint32_t k = 0; k < n0; ++k) {
                const AnimElement& ek = f0.elements[k];
                if (ek.symbol_hash == shash && ek.layer_hash == lhash) {
                    ++cnt0LS;
                    if (ek.frame_num == fnum) ++cnt0Exact;
                }
            }
            int32_t firstJ = -1, exactJ = -1;
            uint32_t cnt1LS = 0, cnt1Exact = 0;
            for (uint32_t j = 0; j < n1; ++j) {
                const AnimElement& e1 = f1.elements[j];
                if (e1.symbol_hash == shash && e1.layer_hash == lhash) {
                    if (firstJ < 0) firstJ = static_cast<int32_t>(j);
                    ++cnt1LS;
                    if (e1.frame_num == fnum) {
                        if (exactJ < 0) exactJ = static_cast<int32_t>(j);
                        ++cnt1Exact;
                    }
                }
            }
            if (firstJ >= 0) {
                if (cnt0LS == 1 && cnt1LS == 1) {
                    const AnimElement& e1 = f1.elements[firstJ];
                    if (e1.frame_num == fnum) {
                        match = &e1;
                    } else if (!fastOnly) {
                        match = &e1;
                        isPass2 = true;
                    }
                } else if (cnt0Exact == 1 && cnt1Exact == 1 && !isUsedF1(static_cast<uint32_t>(exactJ))) {
                    markUsedF1(static_cast<uint32_t>(exactJ));
                    match = &f1.elements[exactJ];
                } else {
                    const float tx0 = HalfToFloat(e0.m[4]);
                    const float ty0 = HalfToFloat(e0.m[5]);
                    int32_t bestJ = -1;
                    float bestScore = 1e30f;
                    for (uint32_t j = 0; j < n1; ++j) {
                        if (isUsedF1(j)) continue;
                        const AnimElement& e1 = f1.elements[j];
                        if (e1.symbol_hash == shash && e1.layer_hash == lhash) {
                            const float dx = HalfToFloat(e1.m[4]) - tx0;
                            const float dy = HalfToFloat(e1.m[5]) - ty0;
                            const float d2 = dx * dx + dy * dy;
                            if (d2 <= kDupMaxDistSq) {
                                const float score = d2 + ((e1.frame_num == fnum) ? 0.0f : 4.0f);
                                if (score < bestScore) {
                                    bestScore = score;
                                    bestJ = static_cast<int32_t>(j);
                                }
                            }
                        }
                    }
                    if (bestJ >= 0) {
                        markUsedF1(static_cast<uint32_t>(bestJ));
                        const AnimElement& e1 = f1.elements[bestJ];
                        if (e1.frame_num == fnum) {
                            match = &e1;
                        } else if (!fastOnly) {
                            match = &e1;
                            isPass2 = true;
                        }
                    }
                }
            }
        }

        if (match && TweenElementMatrix(e0.m, match->m, frac, bufSlot.elems[i].m, fastOnly)) {
            ++tweenedCount;
            if (isPass2) {
                ++pass2Count;
                if (frac >= 0.5f) {
                    bufSlot.elems[i].frame_num = match->frame_num;
                }
            }
        }
    }

    if (workStartQpc != 0) {
        // Sampled per-16-entity work estimator (avoids per-entity QPC overhead while bounding total hook time)
        tls.frameHookAccumQpc += (NowQpc() - workStartQpc) * 16;
    }

    if (tweenedCount > 0) {
        ++tls.frameTweenEnts;
        tls.frameTweenBones += tweenedCount;
        ++tls.batchInterpFrames;
        tls.batchElemsTweened += static_cast<int64_t>(tweenedCount);
        if (pass2Count > 0) tls.batchPass2Swaps += static_cast<int64_t>(pass2Count);
    }
    return &bufSlot.frame;
}

extern "C" const void* FastInterpGetFrameMain(const void* animPtr, int32_t playMode, float time, const void* animRenderCmd) {
    if (!animRenderCmd) {
        return g_origAnimGetFrame ? g_origAnimGetFrame(animPtr, playMode, time) : nullptr;
    }
    auto* cmd = const_cast<uint8_t*>(static_cast<const uint8_t*>(animRenderCmd));
    const void* persistentAnimNode = *reinterpret_cast<const void* const*>(cmd + 8);
    ThreadLocalAnimState& tls = GetTlsAnimState();
    const int64_t nowQpc = tls.GetFrameQpc();
    if (persistentAnimNode) {
        const uintptr_t entityKey = reinterpret_cast<uintptr_t>(persistentAnimNode);
        g_tlsLastParentKey = entityKey;
        // Render-only 240FPS Entity World-Position Interpolation:
        // Runs BEFORE AnimRenderer::Render reads [cmd+0x18..0x57] (fresh per-frame AnimRenderCmd).
        InterpolateAnimCmdWorldPos(cmd, entityKey, tls, nowQpc);
    }
    // Critical ALT_RENDERPATH safety bypass (exact match with Klei @ 0x1400F5775..0x1400F5786):
    // ALT_RENDERPATH assets (Maxwell shadow_trap/shadow_pillar -> shadow_pillar_base_fx,
    // Wormwood trap_vines, lunarhaildrop, wagboss_fissure) compute
    //   frame_idx = (AnimFrame* - anim->frames) / 40
    // inside RenderFrame (0x1400F3C59). They MUST receive a real pointer inside anim->frames[].
    if (g_altRenderCheckVerified) {
        const auto* buildPtr = *reinterpret_cast<const uint8_t* const*>(cmd + g_cmdBuildOffset);
        if (buildPtr) {
            const void* altMesh = *reinterpret_cast<const void* const*>(buildPtr + g_buildAltOffset);
            if (altMesh != nullptr) {
                return g_origAnimGetFrame ? g_origAnimGetFrame(animPtr, playMode, time) : nullptr;
            }
        }
    }
    return InterpGetFrameCore(animPtr, playMode, time, reinterpret_cast<uintptr_t>(persistentAnimNode),
                              AnimChannel::Main, &tls, nowQpc);
}

extern "C" const void* FastInterpGetFrameUI(const void* animPtr, int32_t playMode, float time, const void* persistentAnimNode) {
    return InterpGetFrameCore(animPtr, playMode, time, reinterpret_cast<uintptr_t>(persistentAnimNode), AnimChannel::UI);
}

extern "C" const void* FastInterpGetFrameOverlay(const void* animPtr, int32_t playMode, float time, const void* overlayItem) {
    uintptr_t baseKey = (g_tlsLastParentKey != 0) ? g_tlsLastParentKey : reinterpret_cast<uintptr_t>(overlayItem);
    uintptr_t childKey = (baseKey ^ (reinterpret_cast<uintptr_t>(animPtr) * 0x9E3779B97F4A7C15ULL)) | 1ULL;
    return InterpGetFrameCore(animPtr, playMode, time, childKey, AnimChannel::Overlay);
}

uint8_t* AllocNearExecutable(uint8_t* nearAddr, size_t size) {
    uintptr_t base = reinterpret_cast<uintptr_t>(nearAddr) & ~static_cast<uintptr_t>(0xFFFF);
    constexpr uintptr_t kStep = 0x10000;
    constexpr uintptr_t kMaxDist = 0x60000000;
    for (uintptr_t delta = kStep; delta < kMaxDist; delta += kStep) {
        if (base > delta) {
            void* p = VirtualAlloc(reinterpret_cast<void*>(base - delta), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (p) return static_cast<uint8_t*>(p);
        }
        void* p = VirtualAlloc(reinterpret_cast<void*>(base + delta), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return static_cast<uint8_t*>(p);
    }
    return nullptr;
}

static bool QueueAnimCallsiteHook(std::vector<PendingCodePatch>& patches, uint8_t* callSite, const char* label,
                                  uint8_t*& outStub, const void* targetFn, bool derefRbxPlus8) {
    int32_t origRel = 0;
    memcpy(&origRel, callSite + 1, 4);
    uint8_t* origTarget = callSite + 5 + origRel;
    if (!g_origAnimGetFrame) g_origAnimGetFrame = reinterpret_cast<AnimGetFrame_t>(origTarget);

    uint8_t* stubMem = AllocNearExecutable(callSite, 64);
    if (!stubMem) return false;

    uint64_t dst = reinterpret_cast<uint64_t>(targetFn);
    if (derefRbxPlus8) {
        uint8_t stub[18] = { 0x4C, 0x8B, 0x4B, 0x08, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
        memcpy(stub + 10, &dst, sizeof(dst));
        memcpy(stubMem, stub, sizeof(stub));
        FinalizeExecPageWX(stubMem, sizeof(stub));
    } else {
        uint8_t stub[17] = { 0x49, 0x89, 0xD9, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
        memcpy(stub + 9, &dst, sizeof(dst));
        memcpy(stubMem, stub, sizeof(stub));
        FinalizeExecPageWX(stubMem, sizeof(stub));
    }

    int64_t diff = reinterpret_cast<intptr_t>(stubMem) - reinterpret_cast<intptr_t>(callSite + 5);
    if (diff < INT32_MIN || diff > INT32_MAX) {
        VirtualFree(stubMem, 0, MEM_RELEASE);
        return false;
    }
    int32_t newRel = static_cast<int32_t>(diff);
    outStub = stubMem;

    PendingCodePatch patch{};
    patch.addr = callSite + 1;
    memcpy(patch.bytes, &newRel, 4);
    patch.len = 4;
    patch.ownedExecPage = stubMem;
    char msg[256];
    snprintf(msg, sizeof(msg), "Hooked %s @ %p -> 240fps Skeletal Tween (W^X RX stub, %s)",
             label, static_cast<void*>(callSite), derefRbxPlus8 ? "persistent [rbx+8]" : "direct rbx");
    patch.logMsg = msg;
    patches.push_back(std::move(patch));
    return true;
}

static uint8_t* FindMaskedCallsite(uint8_t* textStart, size_t textSize, const uint8_t* pre, size_t preLen, const uint8_t* post, size_t postLen) {
    size_t total = preLen + 4 + postLen;
    if (!textStart || textSize < total) return nullptr;
    for (size_t off = 0; off <= textSize - total; ++off) {
        if (textStart[off] == pre[0] &&
            memcmp(textStart + off, pre, preLen) == 0 &&
            memcmp(textStart + off + preLen + 4, post, postLen) == 0) {
            return textStart + off + preLen - 1; // points to 0xE8
        }
    }
    return nullptr;
}

void PrepareSkeletalAnimTweenHooks(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    g_qpcInvFreq = (freq.QuadPart > 0) ? (1.0 / static_cast<double>(freq.QuadPart)) : (1.0 / 10000000.0);

    // Masked ALT_RENDERPATH offset signature inside Main World AnimRenderer (0x1400F5775):
    //   mov rcx, [rbx + disp32] ; mov r13, r15 ; cmp [rcx + disp32], r15 ; je rel8
    // Verified 1 unique match across entire .text even with all 3 wildcards!
    constexpr uint8_t kSigAltRender[] = {
        0x48, 0x8B, 0x8B, 0x00, 0x00, 0x00, 0x00,
        0x4D, 0x8B, 0xEF,
        0x4C, 0x39, 0xB9, 0x00, 0x00, 0x00, 0x00,
        0x74, 0x00
    };
    constexpr char kMaskAltRender[] = "xxx????xxxxxx????x?";
    if (uint8_t* pAlt = FindMaskedPatternInText(textStart, textSize, kSigAltRender, kMaskAltRender, sizeof(kSigAltRender))) {
        uint32_t offCmd = 0, offAlt = 0;
        memcpy(&offCmd, pAlt + 3, 4);
        memcpy(&offAlt, pAlt + 13, 4);
        if (offCmd >= 0x40 && offCmd <= 0x400 && offAlt >= 0x40 && offAlt <= 0x400) {
            g_cmdBuildOffset = offCmd;
            g_buildAltOffset = offAlt;
            g_altRenderCheckVerified = true;
            LogMsg("Verified ALT_RENDERPATH guard @ %p (cmd+0x%X -> build+0x%X)",
                   static_cast<void*>(pAlt), g_cmdBuildOffset, g_buildAltOffset);
        }
    }

    // 1. Main World AnimRenderer::Render (FAIL-CLOSED: skip if ALT_RENDERPATH guard is unverified!)
    if (g_altRenderCheckVerified) {
        constexpr uint8_t kPreMain[]  = { 0x8B, 0x93, 0xC0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x8B, 0xB0, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x93, 0xC4, 0x00, 0x00, 0x00, 0xE8 };
        constexpr uint8_t kPostMain[] = { 0x4C, 0x8B, 0xF0, 0x48, 0x89, 0x84, 0x24 };
        if (uint8_t* cs = FindMaskedCallsite(textStart, textSize, kPreMain, sizeof(kPreMain), kPostMain, sizeof(kPostMain))) {
            QueueAnimCallsiteHook(patches, cs, "Main World AnimRenderer", g_animRelayStubMain, reinterpret_cast<const void*>(&FastInterpGetFrameMain), false);
        }
    } else {
        LogMsg("WARNING: ALT_RENDERPATH AOB unverified -> Main World Skeletal Tween disabled (fail-closed safety)");
    }

    // 2. Overlay / FollowSymbol AnimRenderer (0x1400F4493)
    constexpr uint8_t kPreOvl[]  = { 0x8B, 0x53, 0x10, 0x48, 0x8B, 0x0B, 0xF3, 0x0F, 0x10, 0x53, 0x14, 0xE8 };
    constexpr uint8_t kPostOvl[] = { 0x48, 0x8B, 0xF0, 0x48, 0x85, 0xC0, 0x0F, 0x84 };
    if (uint8_t* cs = FindMaskedCallsite(textStart, textSize, kPreOvl, sizeof(kPreOvl), kPostOvl, sizeof(kPostOvl))) {
        QueueAnimCallsiteHook(patches, cs, "Overlay AnimRenderer", g_animRelayStubOverlay, reinterpret_cast<const void*>(&FastInterpGetFrameOverlay), false);
    }

    // 3. UI / AnimNode Render (0x1400F3135): rbx is persistent AnimNode* directly!
    constexpr uint8_t kPreUI[]  = { 0x8B, 0x93, 0x14, 0x01, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x93, 0x18, 0x01, 0x00, 0x00, 0xE8 };
    constexpr uint8_t kPostUI[] = { 0x48, 0x8B, 0xE8, 0x48, 0x85, 0xC0, 0x0F, 0x84 };
    if (uint8_t* cs = FindMaskedCallsite(textStart, textSize, kPreUI, sizeof(kPreUI), kPostUI, sizeof(kPostUI))) {
        QueueAnimCallsiteHook(patches, cs, "UI/AnimNode Renderer", g_animRelayStubUI, reinterpret_cast<const void*>(&FastInterpGetFrameUI), false);
    }
}

void PrepareVectorMemsetPatch(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    if (uint8_t* pA = FindPatternInText(textStart, textSize, kSigVecFillA, sizeof(kSigVecFillA))) {
        PendingCodePatch p{};
        p.addr = pA;
        p.len = sizeof(kSigVecFillA);
        memset(p.bytes, 0x90, p.len);
        constexpr uint8_t codeA[] = { 0x57, 0x48, 0x89, 0xF7, 0x48, 0x89, 0xD9, 0x41, 0x0F, 0xB6, 0x45, 0x00, 0xF3, 0xAA, 0x5F };
        memcpy(p.bytes, codeA, sizeof(codeA));
        char msg[160];
        snprintf(msg, sizeof(msg), "Patched std::vector::_Insert_n Loop A -> HW rep stosb @ %p", static_cast<void*>(pA));
        p.logMsg = msg;
        patches.push_back(std::move(p));
    }
    if (uint8_t* pB = FindPatternInText(textStart, textSize, kSigVecFillB, sizeof(kSigVecFillB))) {
        PendingCodePatch p{};
        p.addr = pB;
        p.len = sizeof(kSigVecFillB);
        memset(p.bytes, 0x90, p.len);
        constexpr uint8_t codeB[] = { 0x57, 0x48, 0x89, 0xCF, 0x48, 0x89, 0xC1, 0x44, 0x89, 0xE0, 0xF3, 0xAA, 0x5F };
        memcpy(p.bytes, codeB, sizeof(codeB));
        char msg[160];
        snprintf(msg, sizeof(msg), "Patched std::vector::_Insert_n Loop B -> HW rep stosb @ %p", static_cast<void*>(pB));
        p.logMsg = msg;
        patches.push_back(std::move(p));
    }
}

void PrepareLibdeflateUnzHook(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    uint8_t* pUnz = FindPatternInText(textStart, textSize, kSigUnzReadCurrentFile, sizeof(kSigUnzReadCurrentFile));
    if (!pUnz) return;

    uint8_t* tramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return;

    constexpr uint8_t prefix[8] = { 0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xF6 };
    memcpy(tramp, prefix, 8);
    uint8_t jmpBack[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint64_t resumeAddr = reinterpret_cast<uint64_t>(pUnz + 0x16);
    memcpy(jmpBack + 6, &resumeAddr, sizeof(resumeAddr));
    memcpy(tramp + 8, jmpBack, 14);
    FinalizeExecPageWX(tramp, 22);
    g_unzTrampoline = tramp;
    g_origUnzRead = reinterpret_cast<UnzReadCurrentFile_t>(tramp);

    PendingCodePatch p{};
    p.addr = pUnz;
    p.len = 14;
    p.ownedExecPage = tramp;
    uint8_t jmpHook[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint64_t hookTarget = reinterpret_cast<uint64_t>(&FastUnzReadCurrentFile);
    memcpy(jmpHook + 6, &hookTarget, sizeof(hookTarget));
    memcpy(p.bytes, jmpHook, 14);
    char msg[192];
    snprintf(msg, sizeof(msg), "Hooked minizip unzReadCurrentFile @ %p -> libdeflate v1.23 (W^X RX tramp)", static_cast<void*>(pUnz));
    p.logMsg = msg;
    patches.push_back(std::move(p));
}

void PrepareFastInflatePatch(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    uint8_t* pInf = FindPatternInText(textStart, textSize, kSigInflateFast, sizeof(kSigInflateFast));
    if (!pInf) return;
    PendingCodePatch p{};
    p.addr = pInf;
    p.len = 14;
    uint8_t jmp[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint64_t target = reinterpret_cast<uint64_t>(&FastInflateFast);
    memcpy(jmp + 6, &target, sizeof(target));
    memcpy(p.bytes, jmp, 14);
    char msg[160];
    snprintf(msg, sizeof(msg), "Hooked zlib 1.2.3 inflate_fast @ %p -> 64-bit FastInflateFast", static_cast<void*>(pInf));
    p.logMsg = msg;
    patches.push_back(std::move(p));
}

//------------------------------------------------------------------------------
// [v2.4.0] Swarm-Flat Heartbeat-Dodging Slack-Aware Client GC Pacer
//   - At smooth target FPS (240Hz): dodges isolated heavy 30Hz/60Hz SimTick frames onto
//     the next lightweight ~1ms render frame (zero tick-spike stacking!).
//   - Under sustained FPS dips / Boss Swarms (e.g. 240->150 FPS where consecutive frames
//     are busy): switches seamlessly to a FLAT 0.28-0.45ms micro-slice every frame instead
//     of rhythmic "defer 2 -> slam 0.90ms" sawtooth hitches, preventing STW memory cliffs!
//------------------------------------------------------------------------------
extern "C" void FastSmoothCollectGarbage(uint8_t* sim) {
    if (!sim || !g_luaGc || !g_setGcBudget) return;
    void* L = *reinterpret_cast<void**>(sim + 0xB0);
    if (!L) return;

    static thread_local int64_t tl_lastGcQpc = 0;
    static thread_local int tl_gcDeferred = 0;
    static thread_local int tl_heavyStreak = 0;
    const int64_t nowQpc = NowQpc();
    float actualDt = 1.0f / 60.0f;
    if (tl_lastGcQpc != 0 && g_qpcInvFreq > 0.0) {
        double dt = static_cast<double>(nowQpc - tl_lastGcQpc) * g_qpcInvFreq;
        if (dt < 0.002) dt = 0.002;
        if (dt > 0.050) dt = 0.050;
        actualDt = static_cast<float>(dt);
    }
    tl_lastGcQpc = nowQpc;

    auto computeFloor = [](int liveKB) -> int {
        int floorKB = liveKB + 262144; // +256 MB emergency headroom
        int scaleKB = static_cast<int>(static_cast<double>(liveKB) * 2.5);
        int next = (scaleKB > floorKB) ? scaleKB : floorKB;
        if (next - liveKB > 0x320000) next = liveKB + 0x320000;
        return next;
    };

    // 1. Emergency-only STW safety floor ALWAYS executes first (never bypassed by deferral)
    int memKB = g_luaGc(L, 3 /*LUA_GCCOUNT*/, 0);
    g_lastGcKb.store(memKB, std::memory_order_relaxed);
    int32_t* pThresh = reinterpret_cast<int32_t*>(sim + 0x24C);
    if (*pThresh <= 0) {
        *pThresh = computeFloor(memKB);
    } else if (memKB > *pThresh) {
        g_luaGc(L, 2 /*LUA_GCCOLLECT*/, 0);
        int afterKB = g_luaGc(L, 3 /*LUA_GCCOUNT*/, 0);
        *pThresh = computeFloor(afterKB);
        g_lastGcKb.store(afterKB, std::memory_order_relaxed);
        InterlockedIncrement64(&g_gcEmergencyFulls);
    }

    // 2. Real-dt base budget (~8.5% frame-time, [0.35ms, 1.60ms])
    float budgetSec = actualDt * 0.085f;
    if (budgetSec < 0.00035f) budgetSec = 0.00035f;
    if (budgetSec > 0.00160f) budgetSec = 0.00160f;

    // 3. Heartbeat-Dodging (isolated ticks) + Swarm-Flat Micro-Slicing (sustained FPS dips)
    const LONG curFps = g_currentFps.load(std::memory_order_relaxed);
    const int64_t frameStart = g_frameStartQpc.load(std::memory_order_acquire);
    if (curFps > 0 && frameStart > 0 && nowQpc >= frameStart) {
        const double targetUs = 1000000.0 / static_cast<double>(curFps);
        const double workUs = static_cast<double>(nowQpc - frameStart) * g_qpcToUs;
        if (workUs > targetUs * 0.60) {
            if (tl_heavyStreak == 0 && tl_gcDeferred == 0) {
                // Isolated heavy 30Hz/60Hz SimTick frame inside smooth 240Hz -> defer once to idle frame!
                ++tl_heavyStreak;
                ++tl_gcDeferred;
                InterlockedIncrement64(&g_gcDeferredCount);
                g_lastGcUs.store(0, std::memory_order_relaxed);
                return;
            }
            // Sustained busy / Boss Swarm dip (e.g. 240->150 FPS): NEVER sawtooth defer->0.90ms!
            // Run a flat, ultra-smooth micro-slice every frame so frame-time stays level.
            if (tl_heavyStreak < 16) ++tl_heavyStreak;
            tl_gcDeferred = 0;
            float flatCapSec = (actualDt <= 0.010f) ? 0.00030f : ((actualDt <= 0.018f) ? 0.00045f : 0.00065f);
            if (budgetSec > flatCapSec) budgetSec = flatCapSec;
        } else {
            tl_heavyStreak = 0;
            const double slackUs = targetUs - workUs;
            if (tl_gcDeferred == 0 && slackUs > 500.0) {
                // Fit cleanly inside remaining idle slack BEFORE the Frame Pacer sleeps
                const double maxSlackSec = (slackUs - 350.0) * 1e-6 * 0.70;
                if (maxSlackSec < static_cast<double>(budgetSec)) {
                    budgetSec = static_cast<float>(std::max(0.00030, maxSlackSec));
                }
            }
            tl_gcDeferred = 0;
        }
    } else {
        tl_heavyStreak = 0;
        tl_gcDeferred = 0;
    }

    // 4. Execute Klei's wall-clock time-budgeted incremental GC step;
    //    when a full incremental cycle completes (ret == 1), relax *pThresh downward to live set!
    g_setGcBudget(budgetSec);
    int cycleDone = g_luaGc(L, 5 /*LUA_GCSTEP*/, 0);
    if (cycleDone == 1) {
        int liveKB = g_luaGc(L, 3 /*LUA_GCCOUNT*/, 0);
        g_lastGcKb.store(liveKB, std::memory_order_relaxed);
        int relaxed = computeFloor(liveKB);
        if (relaxed < *pThresh) {
            *pThresh = relaxed;
        }
    }
    const uint32_t gcUs = static_cast<uint32_t>(
        std::clamp(static_cast<double>(NowQpc() - nowQpc) * g_qpcToUs, 0.0, 1000000.0));
    g_lastGcUs.store(gcUs, std::memory_order_relaxed);
}

void PrepareSmoothLuaGcHook(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    // Simulation::CollectGarbage (0x140102070) masked AOB (verified unique 1-match)
    constexpr uint8_t kPreGc[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x45, 0x33, 0xC0,
        0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x89, 0xB0, 0x00, 0x00, 0x00, 0x41, 0x8D, 0x50, 0x03, 0xE8
    };
    constexpr uint8_t kPostGc[] = {
        0x3B, 0x87, 0x4C, 0x02, 0x00, 0x00, 0x7E, 0x54, 0x48, 0x8B, 0x8F, 0xB0, 0x00, 0x00, 0x00, 0x45
    };
    uint8_t* cs = FindMaskedCallsite(textStart, textSize, kPreGc, sizeof(kPreGc), kPostGc, sizeof(kPostGc));
    if (!cs) return;
    uint8_t* fnStart = cs - (sizeof(kPreGc) - 1);
    if (fnStart[0x8B] != 0xE8) return;

    int32_t relGc = 0, relBudget = 0;
    memcpy(&relGc, fnStart + 0x1C, 4);
    memcpy(&relBudget, fnStart + 0x8C, 4);
    g_luaGc = reinterpret_cast<LuaGc_t>(fnStart + 0x20 + relGc);
    g_setGcBudget = reinterpret_cast<SetGcBudget_t>(fnStart + 0x90 + relBudget);

    PendingCodePatch p{};
    p.addr = fnStart;
    p.len = 14;
    uint8_t jmp[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint64_t target = reinterpret_cast<uint64_t>(&FastSmoothCollectGarbage);
    memcpy(jmp + 6, &target, sizeof(target));
    memcpy(p.bytes, jmp, 14);
    char msg[224];
    snprintf(msg, sizeof(msg), "Hooked Simulation::CollectGarbage @ %p -> Swarm-Flat Heartbeat-Dodging GC (0.30-1.60ms)",
             static_cast<void*>(fnStart));
    p.logMsg = msg;
    patches.push_back(std::move(p));
}

//------------------------------------------------------------------------------
// [v2.4.0] Hermetic mimalloc v2.1.7 Lockless Client Lua Heap (Sim + WorldGen)
// Replaces Klei's global WaitForSingleObject heap lock & malloc+memcpy+free realloc.
// Uses thread_local batched stat counters (0 LOCK XADD per allocation!).
//------------------------------------------------------------------------------
void* HyperClientSimLuaAlloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    static thread_local uint32_t tl_a = 0, tl_r = 0, tl_f = 0;
    auto trackMem = [&]() {
        if (ud && g_clientTrackLuaMem && reinterpret_cast<const uint8_t*>(ud)[0x119] != 0) {
            g_clientTrackLuaMem(ud, static_cast<uint32_t>(osize), static_cast<uint32_t>(nsize));
        }
    };
    if (nsize == 0) {
        if (ptr) {
            mi_free(ptr);
            if (++tl_f >= 1024u) { InterlockedAdd64(&g_miLuaFrees, tl_f); tl_f = 0; }
            trackMem();
        }
        return nullptr;
    }
    void* out = nullptr;
    if (!ptr) {
        out = mi_malloc(nsize);
        if (out && ++tl_a >= 1024u) { InterlockedAdd64(&g_miLuaAllocs, tl_a); tl_a = 0; }
    } else {
        out = mi_realloc(ptr, nsize);
        if (out && ++tl_r >= 1024u) { InterlockedAdd64(&g_miLuaReallocs, tl_r); tl_r = 0; }
    }
    if (out) {
        trackMem();
    }
    return out;
}

void* HyperClientWgLuaAlloc(void* /*ud*/, void* ptr, size_t /*osize*/, size_t nsize) {
    static thread_local uint32_t tl_a = 0, tl_r = 0, tl_f = 0;
    if (nsize == 0) {
        if (ptr) {
            mi_free(ptr);
            if (++tl_f >= 1024u) { InterlockedAdd64(&g_miLuaFrees, tl_f); tl_f = 0; }
        }
        return nullptr;
    }
    if (!ptr) {
        void* out = mi_malloc(nsize);
        if (out && ++tl_a >= 1024u) { InterlockedAdd64(&g_miLuaAllocs, tl_a); tl_a = 0; }
        return out;
    }
    void* out = mi_realloc(ptr, nsize);
    if (out && ++tl_r >= 1024u) { InterlockedAdd64(&g_miLuaReallocs, tl_r); tl_r = 0; }
    return out;
}

void* HookClientLuaNewState(LuaAlloc_t f, void* ud) {
    if (g_optMimallocLua.load(std::memory_order_relaxed)) {
        if (reinterpret_cast<uint8_t*>(f) == g_clientSimLuaAllocAddr) {
            return g_origClientLuaNewState(&HyperClientSimLuaAlloc, ud);
        }
        if (reinterpret_cast<uint8_t*>(f) == g_clientWgLuaAllocAddr) {
            return g_origClientLuaNewState(&HyperClientWgLuaAlloc, ud);
        }
    }
    return g_origClientLuaNewState(f, ud);
}

void PrepareClientMimallocHook(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    uint8_t* pNewState = FindPatternInText(textStart, textSize, kSigLuaNewState, sizeof(kSigLuaNewState));
    uint8_t* pSimAlloc = FindMaskedPatternInText(textStart, textSize, kSigSimLuaAlloc, kMaskSimLuaAlloc, sizeof(kSigSimLuaAlloc));
    uint8_t* pWgAlloc  = FindPatternInText(textStart, textSize, kSigWgLuaAlloc, sizeof(kSigWgLuaAlloc));
    uint8_t* pTrack    = FindMaskedPatternInText(textStart, textSize, kSigTrackLuaMem, kMaskTrackLuaMem, sizeof(kSigTrackLuaMem));
    uint8_t* pTrackRef = pSimAlloc ? ResolveRel32Call(pSimAlloc + 0xA1) : nullptr;

    if (!pNewState || !pSimAlloc || !pWgAlloc || !pTrack || pTrack != pTrackRef) {
        LogMsg("mimalloc Lua Heap skipped (Fail-Closed AOB verify: ns=%p sim=%p wg=%p trk=%p ref=%p)",
               pNewState, pSimAlloc, pWgAlloc, pTrack, pTrackRef);
        return;
    }

    // 15-byte RIP-independent prologue trampoline (W^X PAGE_EXECUTE_READ)
    uint8_t* tramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return;
    memcpy(tramp, pNewState, 15);
    uint8_t jmpBack[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint64_t resumeAddr = reinterpret_cast<uint64_t>(pNewState + 15);
    memcpy(jmpBack + 6, &resumeAddr, sizeof(resumeAddr));
    memcpy(tramp + 15, jmpBack, 14);
    FinalizeExecPageWX(tramp, 29);

    g_clientLuaNewStateTramp = tramp;
    g_origClientLuaNewState  = reinterpret_cast<LuaNewState_t>(tramp);
    g_clientSimLuaAllocAddr  = pSimAlloc;
    g_clientWgLuaAllocAddr   = pWgAlloc;
    g_clientTrackLuaMem      = reinterpret_cast<TrackLuaMem_t>(pTrack);

    PendingCodePatch p{};
    p.addr = pNewState;
    p.len = 15;
    p.ownedExecPage = tramp;
    uint8_t jmpHook[15] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0,0,0,0,0,0,0,0, 0x90 };
    uint64_t hookTarget = reinterpret_cast<uint64_t>(&HookClientLuaNewState);
    memcpy(jmpHook + 6, &hookTarget, sizeof(hookTarget));
    memcpy(p.bytes, jmpHook, 15);
    char msg[224];
    snprintf(msg, sizeof(msg), "Hooked Client lua_newstate @ %p -> Hermetic mimalloc v2.1.7 (W^X RX tramp)",
             static_cast<void*>(pNewState));
    p.logMsg = msg;
    patches.push_back(std::move(p));
}

//------------------------------------------------------------------------------
// [v2.4.0] Zero-Contention RTSS-Grade Frame Pacer & Permanent Atomic FPS Hook
//   - Replaces FrameLimiter::SetTargetFrameTime (14B permanent jump, 0 .text writes)
//   - Replaces FrameLimiter::Wait with High-Resolution Waitable Timer + self-calibrating
//     overshoot EMA + yielding _mm_pause() tail (0% CPU during ~85-90% of wait -> ZERO
//     CPU/GPU contention with ANGLE/OpenGL/Audio threads!) + 1% Low / P99 Telemetry.
//------------------------------------------------------------------------------
struct FrameLimiterObj {
    int64_t startQpc;  // +0x00 (reset by Klei right after Wait returns)
    float   targetSec; // +0x08 (written by HookSetTargetFrameTime)
};

struct FramePacerTls {
    HANDLE  hTimer = nullptr;
    bool    timerChecked = false;
    bool    glChecked = false;
    EglGetCurrentContext_t pEglGetCurrentContext = nullptr;
    GlFinish_t             pGlFinish = nullptr;
    int     gpuSyncCooldown = 0;
    int64_t lastEndQpc = 0;
    int64_t nextDeadlineQpc = 0;
    double  spinMarginUs = 950.0;

    bool HasGlSync() {
        if (!glChecked) {
            glChecked = true;
            HMODULE hEgl = GetModuleHandleW(L"libEGL.dll");
            HMODULE hGles = GetModuleHandleW(L"libGLESv2.dll");
            if (hEgl && hGles) {
                pEglGetCurrentContext = reinterpret_cast<EglGetCurrentContext_t>(
                    GetProcAddress(hEgl, "eglGetCurrentContext"));
                pGlFinish = reinterpret_cast<GlFinish_t>(
                    GetProcAddress(hGles, "glFinish"));
            }
        }
        return (pEglGetCurrentContext != nullptr && pGlFinish != nullptr);
    }

    HANDLE GetHighResTimer() {
        if (!timerChecked) {
            timerChecked = true;
            HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
            if (k32) {
                auto pCreateEx = reinterpret_cast<CreateWaitableTimerExW_t>(
                    GetProcAddress(k32, "CreateWaitableTimerExW"));
                if (pCreateEx) {
                    hTimer = pCreateEx(nullptr, nullptr, kCreateWaitableTimerHighRes, TIMER_ALL_ACCESS);
                }
            }
        }
        return hTimer;
    }

    ~FramePacerTls() {
        if (hTimer && g_stopFlag.load(std::memory_order_relaxed) == 0) {
            CloseHandle(hTimer);
            hTimer = nullptr;
        }
    }
};

extern "C" void HookSetTargetFrameTime(void* frameLimiter, float engineDt) {
    if (!frameLimiter) return;
    LONG fps = g_currentFps.load(std::memory_order_relaxed);
    float dt = engineDt;
    if (fps == 0) {
        dt = 0.0f;
    } else if (fps > 0 && fps != 60) {
        dt = 1.0f / static_cast<float>(fps);
    }
    *reinterpret_cast<float*>(static_cast<uint8_t*>(frameLimiter) + 8) = dt;
}

extern "C" void HookFrameLimiterWait(FrameLimiterObj* fl, uint8_t /*bSleep*/) {
    thread_local FramePacerTls tls;

    int64_t nowQpc = NowQpc();
    LONG fps = g_currentFps.load(std::memory_order_relaxed);
    float targetSec = fl ? fl->targetSec : 0.0f;
    if (fps == 0) {
        targetSec = 0.0f;
    } else if (fps > 0 && fps != 60) {
        targetSec = 1.0f / static_cast<float>(fps);
    }

    if (targetSec > 0.0005f && targetSec < 0.5f) {
        const int64_t targetTicks = static_cast<int64_t>(
            static_cast<double>(targetSec) * static_cast<double>(g_qpcFreq) + 0.5);
        const int64_t baseStart = (fl && fl->startQpc > 0 && fl->startQpc <= nowQpc) ? fl->startQpc : nowQpc;

        if (g_optRtssPacer.load(std::memory_order_relaxed)) {
            // Absolute deadline scheduling (RTSS-grade): zero drift, lag-spike snap, zero catch-up burst!
            if (tls.nextDeadlineQpc == 0) {
                tls.nextDeadlineQpc = baseStart + targetTicks;
            } else {
                tls.nextDeadlineQpc += targetTicks;
            }
            if (nowQpc >= tls.nextDeadlineQpc) {
                tls.nextDeadlineQpc = nowQpc; // Lag spike / VSync cap -> snap cleanly, no burst!
            } else if ((tls.nextDeadlineQpc - nowQpc) > (targetTicks + (targetTicks >> 2))) {
                // FPS target raised or resumed from pause -> clamp cleanly to 1 frame
                int64_t clamped = baseStart + targetTicks;
                tls.nextDeadlineQpc = (clamped > nowQpc + targetTicks) ? (nowQpc + targetTicks) : clamped;
            }

            double remUs = static_cast<double>(tls.nextDeadlineQpc - nowQpc) * g_qpcToUs;

            // [Reflex-Style Zero-FPS-Loss GPU Queue Drain]:
            // When CPU finishes early (>1.1ms free slack before frame deadline), drain ANGLE's
            // 2-3 frame render-ahead queue inside the idle wait window we would sleep away anyway!
            // Self-adaptive brake: if GPU is saturated (remUs drops <150us), cool down 60 frames
            // so GPU-bound FPS is NEVER reduced.
            if (g_optLowLatencyGpuSync.load(std::memory_order_relaxed)) {
                if (tls.gpuSyncCooldown > 0) {
                    --tls.gpuSyncCooldown;
                } else if (remUs > 1100.0 && tls.HasGlSync() && tls.pEglGetCurrentContext() != nullptr) {
                    tls.pGlFinish();
                    nowQpc = NowQpc();
                    remUs = static_cast<double>(tls.nextDeadlineQpc - nowQpc) * g_qpcToUs;
                    InterlockedIncrement64(&g_gpuSyncDrains);
                    if (remUs < 150.0) {
                        tls.gpuSyncCooldown = 60;
                        InterlockedIncrement64(&g_gpuSyncSkips);
                    }
                }
            }

            if (remUs > tls.spinMarginUs + 450.0) {
                HANDLE hTimer = tls.GetHighResTimer();
                if (hTimer) {
                    // Zero-Contention OS Waitable Timer sleep (~70-85% of idle wait at 0% CPU!)
                    // Never invoke kernel timer for micro-sleeps (<450us) whose Win10 wakeup overshoot
                    // degrades 240Hz 1% Low FPS.
                    while (remUs > tls.spinMarginUs + 450.0) {
                        double wantSleepUs = remUs - tls.spinMarginUs;
                        if (wantSleepUs < 450.0) break;
                        if (wantSleepUs > 25000.0) wantSleepUs = 25000.0;

                        LARGE_INTEGER due{};
                        due.QuadPart = -static_cast<LONGLONG>(wantSleepUs * 10.0);
                        int64_t sleepStart = nowQpc;
                        if (SetWaitableTimer(hTimer, &due, 0, nullptr, nullptr, FALSE)) {
                            WaitForSingleObject(hTimer, 50);
                        } else {
                            Sleep(1);
                        }
                        int64_t wakeQpc = NowQpc();
                        double actualUs = static_cast<double>(wakeQpc - sleepStart) * g_qpcToUs;
                        double overshootUs = actualUs - wantSleepUs;
                        if (overshootUs > -50.0 && overshootUs < 6000.0) {
                            if (overshootUs > tls.spinMarginUs * 0.65) {
                                tls.spinMarginUs = tls.spinMarginUs * 0.45 + (overshootUs * 1.40 + 200.0) * 0.55;
                            } else {
                                double targetMargin = std::max(550.0, overshootUs * 1.45 + 250.0);
                                tls.spinMarginUs = tls.spinMarginUs * 0.96 + targetMargin * 0.04;
                            }
                            if (tls.spinMarginUs < 500.0) tls.spinMarginUs = 500.0;
                            if (tls.spinMarginUs > 1950.0) tls.spinMarginUs = 1950.0;
                        }
                        nowQpc = wakeQpc;
                        remUs = static_cast<double>(tls.nextDeadlineQpc - nowQpc) * g_qpcToUs;
                    }
                } else {
                    // Win7/8 graceful fallback
                    while (remUs > 2000.0) {
                        Sleep(1);
                        nowQpc = NowQpc();
                        remUs = static_cast<double>(tls.nextDeadlineQpc - nowQpc) * g_qpcToUs;
                    }
                }
            }
            // Yielding precision tail (~0.4ms): _mm_pause() eliminates HT/GPU pipeline contention
            while ((nowQpc = NowQpc()) < tls.nextDeadlineQpc) {
                _mm_pause();
            }
            if ((nowQpc - tls.nextDeadlineQpc) > (targetTicks >> 1)) {
                tls.nextDeadlineQpc = nowQpc;
            }
        } else {
            // Simple Sleep(1) + _mm_pause() fallback when rtss_frame_pacer=0
            const int64_t deadline = baseStart + targetTicks;
            while ((nowQpc = NowQpc()) < deadline) {
                double remUs = static_cast<double>(deadline - nowQpc) * g_qpcToUs;
                if (remUs > 2000.0) Sleep(1);
                else _mm_pause();
            }
            tls.nextDeadlineQpc = 0;
        }
    } else {
        tls.nextDeadlineQpc = 0;
    }

    // Publish exact frame start & sequence for RenderThread/GC/TexStream + 1% Low ring & Hitch breakdown
    g_frameStartQpc.store(nowQpc, std::memory_order_release);
    g_frameSeq.fetch_add(1, std::memory_order_release);
    if (tls.lastEndQpc != 0) {
        double frameUs = static_cast<double>(nowQpc - tls.lastEndQpc) * g_qpcToUs;
        if (frameUs >= 100.0 && frameUs <= 1000000.0) {
            g_lastFrameUs.store(static_cast<int64_t>(frameUs), std::memory_order_relaxed);
            g_frameRing.Push(static_cast<float>(frameUs));

            // Promote to active gameplay ONLY on genuine 3D world activity (prevents Main Menu animated
            // puppet — orientMode=8, pos240=0, spatial=0 — from triggering gameplay throttling/hitch logs).
            if (g_inGameplay.load(std::memory_order_relaxed) == 0) {
                const int64_t dPos = InterlockedCompareExchange64(&g_worldPosInterpCount, 0, 0) -
                                     g_vmBaseWorldPos.load(std::memory_order_relaxed);
                const int64_t dSp  = InterlockedCompareExchange64(&g_clientSpatialQueries, 0, 0) -
                                     g_vmBaseSpatialQueries.load(std::memory_order_relaxed);
                const int64_t dAnim = InterlockedCompareExchange64(&g_animInterpFrames, 0, 0) -
                                      g_vmBaseInterpFrames.load(std::memory_order_relaxed);
                const bool worldActive = (dPos > 64) || (dSp > 256) ||
                                         (dAnim > 1500 && InterlockedCompareExchange64(&g_libdeflateBytes, 0, 0) > (1024LL * 1024LL * 1024LL) &&
                                          !g_optFastSpatialQuery.load(std::memory_order_relaxed) &&
                                          g_worldPosInterpEnabled.load(std::memory_order_relaxed) == 0);
                if (worldActive && frameUs < 25000.0) {
                    g_inGameplay.store(1, std::memory_order_release);
                }
            } else if (frameUs > 25000.0 && frameUs < 500000.0) {
                // Lock-free Hitch breakdown capture (0 MainThread I/O; background WorkerProc logs max 1x/10s)
                const uint32_t fUs = static_cast<uint32_t>(frameUs);
                g_hitchPendingCount.fetch_add(1, std::memory_order_relaxed);
                uint32_t prevPeak = g_hitchPeakUs.load(std::memory_order_relaxed);
                if (fUs >= prevPeak) {
                    g_hitchPeakUs.store(fUs, std::memory_order_relaxed);
                    g_hitchAnimUs.store(g_lastAnimHookUs.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    g_hitchAnimEnts.store(g_lastAnimEnts.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    g_hitchAnimBones.store(g_lastAnimBones.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    g_hitchAnimShed.store(g_lastAnimShed.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    g_hitchGcUs.store(g_lastGcUs.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    g_hitchGcKb.store(g_lastGcKb.load(std::memory_order_relaxed), std::memory_order_relaxed);
                    g_hitchTexUs.store(g_lastTexUploadUs.load(std::memory_order_relaxed), std::memory_order_relaxed);
                }
            }
        }
    }
    tls.lastEndQpc = nowQpc;
}

bool ApplyFpsPatch(int fps) {
    if (!g_frameLimiterHooked.load(std::memory_order_acquire)) return false;
    if (fps < 0) fps = 0;
    if (fps > 1000) fps = 1000;
    LONG prev = g_currentFps.exchange(fps, std::memory_order_relaxed);
    if (prev == fps) return true;

    if (fps == 60) LogMsg("FPS -> 60 (RTSS Waitable-Timer Pacer, 0 .text writes)");
    else if (fps == 0) LogMsg("FPS -> UNLIMITED (0 .text writes)");
    else LogMsg("FPS -> %d (dt=%.6fs, RTSS Pacer, 0 .text writes)", fps, 1.0f / static_cast<float>(fps));
    return true;
}

//------------------------------------------------------------------------------
// [v2.4.0] Route B: Auto-Negotiated Opt-In 60Hz SimTick Receiver
// Strictly 30Hz stock by default; unconditionally reset to 30Hz on every VM init!
//------------------------------------------------------------------------------
static bool PatchClientFloatProtected(float* ptr, float val) {
    if (!ptr) return false;
    DWORD oldProt = 0, dummy = 0;
    if (!VirtualProtect(ptr, sizeof(float), PAGE_READWRITE, &oldProt)) return false;
    *ptr = val;
    VirtualProtect(ptr, sizeof(float), oldProt, &dummy);
    return true;
}

bool SetClientSimTickRate(int hz) {
    if (hz != 30 && hz != 60) return false;
    if (!g_clientSimTickFloatPtr || !g_clientSimTickDoublePtr) return false;
    if (g_clientSimTickRate.load(std::memory_order_acquire) == hz) return true;

    const float newF = 1.0f / static_cast<float>(hz);
    const double newD = 1.0 / static_cast<double>(hz);

    if (!PatchClientFloatProtected(g_clientSimTickFloatPtr, newF)) return false;

    DWORD oldD = 0, dummy = 0;
    if (!VirtualProtect(g_clientSimTickDoublePtr, sizeof(double), PAGE_READWRITE, &oldD)) return false;
    *g_clientSimTickDoublePtr = newD;
    VirtualProtect(g_clientSimTickDoublePtr, sizeof(double), oldD, &dummy);

    // Patch ALL secondary exclusive mTick*(1/30) subsystems (AnimRenderer shader time, PathFinder, EntityMgr, LightWatcher)
    int extras = 0;
    if (PatchClientFloatProtected(g_clientAnimShaderTickPtr, newF)) ++extras;
    if (PatchClientFloatProtected(g_clientPathfinderTickPtr, newF)) ++extras;
    if (PatchClientFloatProtected(g_clientSpatialTickPtr, newF))    ++extras;
    if (PatchClientFloatProtected(g_clientSimTimeDiffPtr, newF))    ++extras;

    // Patch SetNetworkTickRate 30.5f -> 60.5f so upload ticksPerSnapshot stays exact (U=10(6) on Client, U=60(1) on Local Host)
    bool netScaleOk = PatchClientFloatProtected(g_clientNetTickScalePtr, (hz == 60) ? 60.499998f : 30.499998f);

    g_clientSimTickRate.store(hz, std::memory_order_release);
    g_clientSimTickGen.fetch_add(1, std::memory_order_release);

    // Re-sync NetworkManager & disable RakNet ReplicaManager3 download throttle (D=0) in 60Hz mode!
    // Prevents RakPeer::PushBackPacket sawtooth when Server sends >15Hz snapshots to a D=15 client!
    const char* netSyncDesc = "none";
    if (g_clientNetMgrGlobalPtr && *g_clientNetMgrGlobalPtr) {
        auto* netMgr = static_cast<uint8_t*>(*g_clientNetMgrGlobalPtr);
        void* replicaMgr = *reinterpret_cast<void**>(netMgr + 0x288);
        uint8_t isClient = netMgr[0x308];
        uint32_t curRate = *reinterpret_cast<uint32_t*>(netMgr + 0xD8);
        if (hz == 60 && replicaMgr) {
            if (isClient == 0) {
                // No-Caves Local Host running inside Client binary: upgrade default 15Hz -> 60Hz (U=60(1))
                uint32_t hostRate = (curRate == 0 || curRate == 15) ? 60u : curRate;
                if (g_fnClientSetNetTickRate) {
                    g_fnClientSetNetTickRate(netMgr, hostRate);
                    netSyncDesc = "local-host U=60(1)";
                }
            } else {
                // Dedicated-Server Client: recalc upload ticksPerSnapshot=6 (10Hz) AND set D=0 (unthrottled receive!)
                if (g_fnClientSetNetTickRate) {
                    g_fnClientSetNetTickRate(netMgr, curRate ? curRate : 15u);
                }
                if (g_fnClientSetDownloadMs) {
                    g_fnClientSetDownloadMs(replicaMgr, 0);
                    netSyncDesc = "client U=10(6) D=0(unthrottled)";
                } else {
                    netSyncDesc = "client U=10(6)";
                }
            }
        } else if (hz == 30) {
            netSyncDesc = "restored 30Hz vanilla";
        }
    }

    LogMsg("[SIMTICK] Client Simulation tick rate -> %d Hz (dt=%.6fs, subsys=%d/4, netScale=%s, net=%s)",
           hz, newD, extras, netScaleOk ? "OK" : "SKIP", netSyncDesc);
    return true;
}

void InitClientSimTickPointers(uint8_t* textStart, size_t textSize) {
    auto resolveCheckedFloat = [](uint8_t* site, size_t dispOff, size_t nextIpOff, float expected, float tol) -> float* {
        if (!site) return nullptr;
        int32_t disp = 0;
        memcpy(&disp, site + dispOff, 4);
        auto* p = reinterpret_cast<float*>(site + nextIpOff + disp);
        return (fabsf(*p - expected) < tol) ? p : nullptr;
    };

    g_clientAnimShaderTickPtr = resolveCheckedFloat(
        FindMaskedPatternInText(textStart, textSize, kSigClientAnimShaderTick, kMaskClientAnimShaderTick, sizeof(kSigClientAnimShaderTick)),
        52, 56, 1.0f / 30.0f, 1e-5f);
    g_clientPathfinderTickPtr = resolveCheckedFloat(
        FindMaskedPatternInText(textStart, textSize, kSigClientPathfinderTick, kMaskClientPathfinderTick, sizeof(kSigClientPathfinderTick)),
        30, 34, 1.0f / 30.0f, 1e-5f);
    g_clientSpatialTickPtr = resolveCheckedFloat(
        FindMaskedPatternInText(textStart, textSize, kSigClientSpatialTick, kMaskClientSpatialTick, sizeof(kSigClientSpatialTick)),
        30, 34, 1.0f / 30.0f, 1e-5f);
    g_clientSimTimeDiffPtr = resolveCheckedFloat(
        FindMaskedPatternInText(textStart, textSize, kSigClientSimTimeDiff, kMaskClientSimTimeDiff, sizeof(kSigClientSimTimeDiff)),
        19, 23, 1.0f / 30.0f, 1e-5f);

    if (uint8_t* pSetNet = FindPatternInText(textStart, textSize, kSigClientSetNetTickRate, sizeof(kSigClientSetNetTickRate))) {
        g_fnClientSetNetTickRate = reinterpret_cast<ClientSetNetworkTickRate_t>(pSetNet);
    }
    if (uint8_t* pNetScale = FindMaskedPatternInText(textStart, textSize, kSigClientNetTickScale, kMaskClientNetTickScale, sizeof(kSigClientNetTickScale))) {
        g_clientNetTickScalePtr = resolveCheckedFloat(pNetScale, 4, 8, 30.5f, 0.01f);
        if (uint8_t* pSetDl = ResolveRel32Call(pNetScale + 69)) {
            g_fnClientSetDownloadMs = reinterpret_cast<ClientSetDownloadMs_t>(pSetDl);
        }
    }
    if (uint8_t* pNetRef = FindMaskedPatternInText(textStart, textSize, kSigClientSimUpdateNetMgr, kMaskClientSimUpdateNetMgr, sizeof(kSigClientSimUpdateNetMgr))) {
        int32_t disp = 0;
        memcpy(&disp, pNetRef + 3, 4);
        g_clientNetMgrGlobalPtr = reinterpret_cast<void**>(pNetRef + 7 + disp);
    }

    uint8_t* pTickLoad = FindMaskedPatternInText(textStart, textSize, kSigSimUpdateTick, kMaskSimUpdateTick, sizeof(kSigSimUpdateTick));
    if (!pTickLoad) {
        LogMsg("[SIMTICK] Client SimUpdate 1/30 AOB not found (locked at 30Hz).");
        return;
    }
    int32_t dispF = 0, dispD = 0;
    memcpy(&dispF, pTickLoad + 12, 4);
    memcpy(&dispD, pTickLoad + 99, 4);
    auto* pf = reinterpret_cast<float*>(pTickLoad + 16 + dispF);
    auto* pd = reinterpret_cast<double*>(pTickLoad + 103 + dispD);
    if (fabsf(*pf - (1.0f / 30.0f)) < 1e-5f && fabs(*pd - (1.0 / 30.0)) < 1e-5) {
        g_clientSimTickFloatPtr = pf;
        g_clientSimTickDoublePtr = pd;
        LogMsg("[SIMTICK] Verified exclusive Client tick constants @ f=%p d=%p sub=(%d%d%d%d) netScale=%p netMgr=%p setNet=%p setDl=%p",
               static_cast<void*>(pf), static_cast<void*>(pd),
               g_clientAnimShaderTickPtr != nullptr, g_clientPathfinderTickPtr != nullptr,
               g_clientSpatialTickPtr != nullptr, g_clientSimTimeDiffPtr != nullptr,
               static_cast<void*>(g_clientNetTickScalePtr), static_cast<void*>(g_clientNetMgrGlobalPtr),
               reinterpret_cast<void*>(g_fnClientSetNetTickRate), reinterpret_cast<void*>(g_fnClientSetDownloadMs));
    } else {
        LogMsg("[SIMTICK] Skipped (Fail-Closed value check: f=%.6f d=%.6f)", *pf, *pd);
    }
}

//------------------------------------------------------------------------------
// 0ms Direct C++ <-> Lua VM Native Bridge (luaL_openlibs hook)
//------------------------------------------------------------------------------
int LuaHyperSmoothSetFps(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    int fps = static_cast<int>(g_luaToNumber(L, 1));
    bool ok = ApplyFpsPatch(fps);
    g_luaPushBoolean(L, ok ? 1 : 0);
    return 1;
}

int LuaHyperSmoothSetSkeletal(void* L) {
    if (!g_luaToBoolean || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    LONG newVal = g_luaToBoolean(L, 1) ? 1 : 0;
    LONG prev = g_skeletalTweenEnabled.exchange(newVal, std::memory_order_relaxed);
    if (prev != newVal) {
        LogMsg("Lua Bridge -> Skeletal 240fps=%ld", newVal);
    }
    g_luaPushBoolean(L, 1);
    return 1;
}

int LuaHyperSmoothSetWorldInterp(void* L) {
    if (!g_luaToBoolean || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    LONG newVal = g_luaToBoolean(L, 1) ? 1 : 0;
    LONG prev = g_worldPosInterpEnabled.exchange(newVal, std::memory_order_relaxed);
    if (prev != newVal) {
        LogMsg("Lua Bridge -> WorldPos240=%ld", newVal);
    }
    g_luaPushBoolean(L, 1);
    return 1;
}

static uint8_t* FindUniquePatternInText(uint8_t* start, size_t size, const uint8_t* sig, size_t sigLen) {
    if (!start || size < sigLen) return nullptr;
    uint8_t* hit = nullptr;
    for (size_t off = 0; off <= size - sigLen; ++off) {
        if (start[off] == sig[0] && memcmp(start + off, sig, sigLen) == 0) {
            if (hit) return nullptr; // ambiguous (>1 match) -> fail-closed
            hit = start + off;
        }
    }
    return hit;
}

void PrepareRakNetZeroStallPatch(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    uint8_t* p1 = FindUniquePatternInText(textStart, textSize, kSigClientRakSendBufWake, sizeof(kSigClientRakSendBufWake));
    uint8_t* p2 = FindUniquePatternInText(textStart, textSize, kSigClientRakSendBufListWake, sizeof(kSigClientRakSendBufListWake));
    uint8_t* ev1 = p1 ? ResolveRel32Call(p1 + 12) : nullptr;
    uint8_t* ev2 = p2 ? ResolveRel32Call(p2 + 11) : nullptr;
    if (!p1 || !p2 || !ev1 || ev1 != ev2) {
        LogMsg("[NET] RakNet Zero-Stall skipped (Fail-Closed cross-check: p1=%p p2=%p ev1=%p ev2=%p)",
               p1, p2, ev1, ev2);
        return;
    }
    constexpr uint8_t kNop2[2] = { 0x90, 0x90 };
    {
        PendingCodePatch patch{};
        patch.addr = p1 + 3;
        patch.len = 2;
        memcpy(patch.bytes, kNop2, 2);
        char msg[192];
        snprintf(msg, sizeof(msg), "Patched RakNet SendBuffered @ %p -> 0ms event-driven RPC wakeup (SetEvent=%p)",
                 static_cast<void*>(p1 + 3), static_cast<void*>(ev1));
        patch.logMsg = msg;
        patches.push_back(std::move(patch));
    }
    {
        PendingCodePatch patch{};
        patch.addr = p2 + 2;
        patch.len = 2;
        memcpy(patch.bytes, kNop2, 2);
        char msg[192];
        snprintf(msg, sizeof(msg), "Patched RakNet SendBufferedList @ %p -> 0ms event-driven RPC wakeup (SetEvent=%p)",
                 static_cast<void*>(p2 + 2), static_cast<void*>(ev2));
        patch.logMsg = msg;
        patches.push_back(std::move(patch));
    }
}

//------------------------------------------------------------------------------
// [v2.4.0] Zero-Copy Client SpatialGrid::Query Accelerator (All 5 Read-Only Sites)
// Eliminates Klei's per-cell MSVCR90!malloc + memcpy + free deep-copy storm inside:
//   [0] FindEntities / CountEntities (cached-tag path @ 0x140123BEF -> 0x140121690)
//   [1] FindEntities / CountEntities (direct-tag path @ 0x140123AAC -> 0x1401213B0)
//   [2] FindEntities_Registered      (@ 0x140123D2C -> 0x140121970)
//   [3] LightWatcher::GetLightAtPoint Intensity (@ 0x14011F6A8 -> 0x14011EDC0)
//   [4] LightWatcher::GetLightAtPoint RGB       (@ 0x14011F740 -> 0x14011F0A0)
// Huge anti-hitch win across ALL concurrent multi-unit fights (CC3, Dragonfly lavae,
// Bee Queen bees, Fuelweaver shadows, Toadstool, Crab King, Frog Rain, Megabases).
//------------------------------------------------------------------------------
struct ClientSpatialCell {
    int32_t cx;
    int32_t cz;
    void*   vec_proxy;
    void**  ent_begin;
    void**  ent_end;
    void**  ent_cap;
};
static_assert(sizeof(ClientSpatialCell) == 0x28, "ClientSpatialCell size must be 0x28");

static uint8_t* FindUniqueMaskedCallsite(uint8_t* textStart, size_t textSize,
                                         const uint8_t* pre, size_t preLen,
                                         const uint8_t* post, size_t postLen) {
    const size_t total = preLen + 4 + postLen;
    if (!textStart || textSize < total) return nullptr;
    uint8_t* hit = nullptr;
    for (size_t off = 0; off <= textSize - total; ++off) {
        if (textStart[off] == pre[0] &&
            memcmp(textStart + off, pre, preLen) == 0 &&
            memcmp(textStart + off + preLen + 4, post, postLen) == 0) {
            if (hit) return nullptr; // ambiguous (>1 match) -> fail-closed
            hit = textStart + off + preLen - 1; // points to 0xE8 / 0xE9
        }
    }
    return hit;
}

template <size_t Slot>
static void HookClientSpatialQuery(void* grid, const float* posXZ, float radius, void* visitor) {
    static thread_local uint32_t tl_q = 0;
    static thread_local uint32_t tl_c = 0;

    const auto vis = g_clientSpatialVisitor[Slot];
    const auto orig = g_origClientSpatialQuery[Slot];
    if (!g_optFastSpatialQuery.load(std::memory_order_relaxed) || !vis || !grid || !posXZ) {
        if (orig) orig(grid, posXZ, radius, visitor);
        return;
    }

    const auto* gbytes = reinterpret_cast<const uint8_t*>(grid);
    const auto* cells_begin = *reinterpret_cast<const ClientSpatialCell* const*>(gbytes + 0x08);
    const auto* cells_end   = *reinterpret_cast<const ClientSpatialCell* const*>(gbytes + 0x10);
    const float cell_size   = *reinterpret_cast<const float*>(gbytes + 0x38);
    if (!cells_begin || cells_begin >= cells_end || !std::isfinite(cell_size) || cell_size <= 0.0f) {
        return;
    }

    const float x = posXZ[0];
    const float z = posXZ[1];
    if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(radius) ||
        radius < 0.0f || radius > 100000.0f) {
        if (orig) orig(grid, posXZ, radius, visitor);
        return;
    }

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
    const float r_sq_eps = radius * radius + 1e-3f;
    uint32_t visited_cells = 0;

    auto lower_bound_cell = [](const ClientSpatialCell* lo, const ClientSpatialCell* hi, int32_t cx, int32_t cz) {
        while (lo < hi) {
            const ClientSpatialCell* mid = lo + ((hi - lo) >> 1);
            if (mid->cx < cx || (mid->cx == cx && mid->cz < cz)) lo = mid + 1;
            else hi = mid;
        }
        return lo;
    };

    const ClientSpatialCell* cur = lower_bound_cell(cells_begin, cells_end, min_cx, min_cz);
    for (int32_t cx = min_cx; cx <= max_cx && cur < cells_end; ++cx) {
        if (cur->cx > max_cx) break;
        if (cur->cx > cx) {
            cx = cur->cx;
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
                        tl_c += visited_cells;
                        if (++tl_q >= 256u) {
                            InterlockedAdd64(&g_clientSpatialQueries, tl_q);
                            InterlockedAdd64(&g_clientSpatialCells, tl_c);
                            tl_q = 0; tl_c = 0;
                        }
                        return;
                    }
                    ++p;
                }
            }
            ++cur;
        }
    }

    tl_c += visited_cells;
    if (++tl_q >= 256u) {
        InterlockedAdd64(&g_clientSpatialQueries, tl_q);
        InterlockedAdd64(&g_clientSpatialCells, tl_c);
        tl_q = 0; tl_c = 0;
    }
}

static bool QueueRel32NearRelayHook(std::vector<PendingCodePatch>& patches, uint8_t* callSite,
                                    const void* targetFn, const char* logMsg) {
    uint8_t* stub = AllocNearExecutable(callSite, 64);
    if (!stub) return false;
    uint8_t code[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint64_t dst = reinterpret_cast<uint64_t>(targetFn);
    memcpy(code + 6, &dst, sizeof(dst));
    memcpy(stub, code, sizeof(code));
    FinalizeExecPageWX(stub, sizeof(code));

    const int64_t diff = reinterpret_cast<intptr_t>(stub) - reinterpret_cast<intptr_t>(callSite + 5);
    if (diff < INT32_MIN || diff > INT32_MAX) {
        VirtualFree(stub, 0, MEM_RELEASE);
        return false;
    }
    const int32_t rel = static_cast<int32_t>(diff);
    PendingCodePatch patch{};
    patch.addr = callSite + 1;
    memcpy(patch.bytes, &rel, 4);
    patch.len = 4;
    patch.ownedExecPage = stub;
    patch.logMsg = logMsg;
    patches.push_back(std::move(patch));
    return true;
}

void PrepareClientSpatialZeroCopyHooks(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    constexpr uint8_t kExpectedSgPrologue[17] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x53, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57
    };
    struct SiteSpec {
        const char* name;
        uint8_t pre[17];
        uint8_t post[12];
        const void* hookFn;
    };
    static const SiteSpec kSites[kClientSpatialSlots] = {
        {
            "FindEntities(Cached)",
            { 0x4C, 0x24, 0x30, 0x48, 0x8D, 0x94, 0x24, 0xD0, 0x00, 0x00, 0x00, 0x66, 0x89, 0x44, 0x24, 0x6A, 0xE8 },
            { 0x48, 0x8B, 0x55, 0x10, 0x48, 0x8B, 0x4D, 0x08, 0x4C, 0x8B, 0xC2, 0x4D },
            reinterpret_cast<const void*>(&HookClientSpatialQuery<0>)
        },
        {
            "FindEntities(Direct)",
            { 0x8B, 0xFA, 0x4D, 0x8D, 0x4B, 0xC8, 0xF3, 0x41, 0x0F, 0x11, 0x43, 0x38, 0x49, 0x8D, 0x53, 0x38, 0xE8 },
            { 0x48, 0x8B, 0x53, 0x10, 0x48, 0x8B, 0x4B, 0x08, 0x4C, 0x8B, 0xC2, 0x4C },
            reinterpret_cast<const void*>(&HookClientSpatialQuery<1>)
        },
        {
            "FindEntities(Registered)",
            { 0x03, 0xC2, 0x0F, 0xB6, 0xC0, 0x2B, 0xC2, 0x48, 0x8D, 0x54, 0x24, 0x30, 0x88, 0x44, 0x24, 0x40, 0xE8 },
            { 0x48, 0x8B, 0x53, 0x10, 0x48, 0x8B, 0x4B, 0x08, 0x4C, 0x8B, 0xC2, 0x4C },
            reinterpret_cast<const void*>(&HookClientSpatialQuery<2>)
        },
        {
            "LightWatcher(Intensity)",
            { 0xF3, 0x0F, 0x10, 0x10, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x60, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x64, 0xE8 },
            { 0x48, 0x83, 0xC4, 0x48, 0xC3, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC },
            reinterpret_cast<const void*>(&HookClientSpatialQuery<3>)
        },
        {
            "LightWatcher(RGB)",
            { 0xF3, 0x0F, 0x10, 0x10, 0xF3, 0x0F, 0x11, 0x44, 0x24, 0x78, 0xF3, 0x0F, 0x11, 0x4C, 0x24, 0x7C, 0xE8 },
            { 0xF3, 0x0F, 0x10, 0x6C, 0x24, 0x60, 0x0F, 0x2F, 0xEE, 0x76, 0x28, 0xF3 },
            reinterpret_cast<const void*>(&HookClientSpatialQuery<4>)
        }
    };

    const uintptr_t textLo = reinterpret_cast<uintptr_t>(textStart);
    const uintptr_t textHi = textLo + textSize;
    int hooked = 0;

    for (size_t i = 0; i < kClientSpatialSlots; ++i) {
        uint8_t* cs = FindUniqueMaskedCallsite(textStart, textSize, kSites[i].pre, 17, kSites[i].post, 12);
        if (!cs) continue;
        uint8_t* fn = ResolveRel32Call(cs);
        const uintptr_t fnAddr = reinterpret_cast<uintptr_t>(fn);
        if (!fn || fnAddr < textLo || fnAddr + 0x250 > textHi) continue;
        // Strict fail-closed verification of SpatialGrid::Query<V> prologue & +0x239 visitor call
        if (memcmp(fn, kExpectedSgPrologue, sizeof(kExpectedSgPrologue)) != 0) continue;
        if (fn[0x239] != 0xE8 || fn[0x23E] != 0x84 || fn[0x23F] != 0xC0) continue;
        uint8_t* vis = ResolveRel32Call(fn + 0x239);
        const uintptr_t visAddr = reinterpret_cast<uintptr_t>(vis);
        if (!vis || visAddr < textLo || visAddr >= textHi) continue;

        g_origClientSpatialQuery[i] = reinterpret_cast<ClientSpatialQuery_t>(fn);
        g_clientSpatialVisitor[i] = reinterpret_cast<ClientSpatialVisitor_t>(vis);

        char msg[224];
        snprintf(msg, sizeof(msg), "Hooked Client SpatialGrid %s @ %p (fn=%p vis=%p) -> Zero-Copy O(cells)",
                 kSites[i].name, static_cast<void*>(cs), static_cast<void*>(fn), static_cast<void*>(vis));
        if (QueueRel32NearRelayHook(patches, cs, kSites[i].hookFn, msg)) {
            ++hooked;
        }
    }
    if (hooked == 0) {
        LogMsg("[SPATIAL] Client Zero-Copy SpatialGrid skipped (Fail-Closed AOB verify)");
    }
}

//------------------------------------------------------------------------------
// [v2.4.0] Time-Sliced RenderThread Async Texture Upload (ProcessAsyncQueueResults)
// Prevents Boss/Swarm/Event on-demand DXT5 texture bursts (e.g. CC3 meteors,
// Dragonfly lavae, Bee Queen bees, Fuelweaver minions, Frog Rain) from stalling
// ANGLE RenderThread for 20-60ms in a single frame:
//   - Active gameplay: uploads >=1 texture and caps at 1.2ms/frame (remaining jobs
//     finish smoothly 4ms later on the next frame).
//   - Loading screens / VM resets / bulk >48 queue: unlimited full speed!
//------------------------------------------------------------------------------
static void HookProcessAsyncTextureQueue(void* loader) {
    if (!loader || !g_optTexTimeSlice.load(std::memory_order_relaxed)) {
        if (g_origProcessAsyncTexQueue) g_origProcessAsyncTexQueue(loader);
        return;
    }

    auto* bytes = static_cast<uint8_t*>(loader);
    auto* cs = reinterpret_cast<CRITICAL_SECTION*>(bytes + 0x88);
    auto** pBegin = reinterpret_cast<void***>(bytes + 0xB8);
    auto** pEnd   = reinterpret_cast<void***>(bytes + 0xC0);

    static thread_local uint32_t tl_lastTexSeq = 0xFFFFFFFFu;
    static thread_local uint32_t tl_frameTexUs = 0;
    const uint32_t curSeq = g_frameSeq.load(std::memory_order_relaxed);
    if (curSeq != tl_lastTexSeq) {
        g_lastTexUploadUs.store(tl_frameTexUs, std::memory_order_relaxed);
        tl_lastTexSeq = curSeq;
        tl_frameTexUs = 0;
    }

    EnterCriticalSection(cs);
    void** begin = *pBegin;
    void** end   = *pEnd;
    if (!begin || end <= begin) {
        LeaveCriticalSection(cs);
        return;
    }

    const size_t initCount = static_cast<size_t>(end - begin);
    const int64_t lastFrameUs = g_lastFrameUs.load(std::memory_order_relaxed);
    const bool limitActive = (g_inGameplay.load(std::memory_order_relaxed) != 0) &&
                             (lastFrameUs > 0 && lastFrameUs < 35000) &&
                             (initCount <= 48);
    constexpr double kTexSliceBudgetUs = 1200.0;

    const int64_t t0 = NowQpc();
    size_t processed = 0;
    bool sliced = false;

    while (true) {
        begin = *pBegin;
        end   = *pEnd;
        const size_t curCount = (begin && end > begin) ? static_cast<size_t>(end - begin) : 0;
        if (processed >= curCount) break;

        void* job = begin[processed];
        if (job) {
            auto* vtbl = *reinterpret_cast<void***>(job);
            if (vtbl) {
                if (vtbl[1]) reinterpret_cast<void (*)(void*)>(vtbl[1])(job);         // Upload texture (+0x08)
                if (vtbl[2]) reinterpret_cast<void (*)(void*, int)>(vtbl[2])(job, 1); // Deleting dtor (+0x10)
            }
        }
        ++processed;

        if (limitActive && processed < curCount) {
            const double elapsedUs = static_cast<double>(NowQpc() - t0) * g_qpcToUs;
            if (elapsedUs + static_cast<double>(tl_frameTexUs) >= kTexSliceBudgetUs) {
                sliced = true;
                break;
            }
        }
    }

    begin = *pBegin;
    end   = *pEnd;
    const size_t finalCount = (begin && end > begin) ? static_cast<size_t>(end - begin) : 0;
    if (processed < finalCount) {
        const size_t rem = finalCount - processed;
        memmove(begin, begin + processed, rem * sizeof(void*));
        *pEnd = begin + rem;
    } else if (begin) {
        *pEnd = begin;
    }
    LeaveCriticalSection(cs);

    const uint32_t usedUs = static_cast<uint32_t>(
        std::clamp(static_cast<double>(NowQpc() - t0) * g_qpcToUs, 0.0, 500000.0));
    tl_frameTexUs += usedUs;
    g_lastTexUploadUs.store(tl_frameTexUs, std::memory_order_relaxed);
    if (processed > 0) InterlockedAdd64(&g_texUploadsTotal, static_cast<LONG64>(processed));
    if (sliced) InterlockedIncrement64(&g_texUploadSlices);
}

void PrepareTexStreamTimeSliceHook(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    // Unique 1-match dual-callsite inside TexStreamTick (0x140384855..0x14038488B):
    //   call ProcessAsyncQueueResults (loader1) ; mov rcx, [rdi] ; ... ; jmp ProcessAsyncQueueResults (loader0)
    constexpr uint8_t kPre[] = {
        0x49, 0x83, 0x7B, 0x08, 0x00, 0x74, 0x0B, 0x41, 0x80, 0x7B, 0x48, 0x00, 0x74, 0x04,
        0x41, 0xFF, 0x43, 0x50, 0x49, 0x83, 0xC3, 0x70, 0x4C, 0x3B, 0x5B, 0x40, 0x75, 0xE4,
        0x48, 0x8B, 0x4F, 0x08, 0xE8
    };
    constexpr uint8_t kMid[] = {
        0x48, 0x8B, 0x0F, 0x48, 0x8B, 0x5C, 0x24, 0x30, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xE9
    };
    uint8_t* cs1 = FindUniqueMaskedCallsite(textStart, textSize, kPre, sizeof(kPre), kMid, sizeof(kMid));
    if (!cs1) {
        LogMsg("[TEX] RenderThread Time-Sliced Texture Upload skipped (callsite AOB miss)");
        return;
    }
    uint8_t* cs2 = cs1 + 5 + (sizeof(kMid) - 1); // points to 0xE9 tail-call
    uint8_t* fn1 = ResolveRel32Call(cs1);
    uint8_t* fn2 = ResolveRel32Call(cs2);
    const uintptr_t textLo = reinterpret_cast<uintptr_t>(textStart);
    const uintptr_t textHi = textLo + textSize;
    if (!fn1 || fn1 != fn2 ||
        reinterpret_cast<uintptr_t>(fn1) < textLo ||
        reinterpret_cast<uintptr_t>(fn1) + 0x90 > textHi) {
        LogMsg("[TEX] Time-Sliced Texture Upload skipped (cross-check failed: fn1=%p fn2=%p)", fn1, fn2);
        return;
    }

    // Strict layout verification of ProcessAsyncQueueResults (CS @ +0x88, vector @ +0xB8/+0xC0, vtbl[1]/[2])
    constexpr uint8_t kLockBytes[8] = { 0x48, 0x8D, 0x8F, 0x88, 0x00, 0x00, 0x00, 0xE8 };
    constexpr uint8_t kVecBytes[25] = {
        0x48, 0x8B, 0x87, 0xC0, 0x00, 0x00, 0x00, 0x33, 0xF6, 0x48, 0x2B, 0x87, 0xB8, 0x00, 0x00, 0x00,
        0x48, 0xC1, 0xF8, 0x03, 0x48, 0x85, 0xC0, 0x74, 0x53
    };
    if (memcmp(fn1 + 0x2F, kLockBytes, sizeof(kLockBytes)) != 0 ||
        memcmp(fn1 + 0x3B, kVecBytes, sizeof(kVecBytes)) != 0 ||
        fn1[0x71] != 0xFF || fn1[0x72] != 0x50 || fn1[0x73] != 0x08 ||
        fn1[0x7F] != 0x41 || fn1[0x80] != 0xFF || fn1[0x81] != 0x53 || fn1[0x82] != 0x10) {
        LogMsg("[TEX] Time-Sliced Texture Upload skipped (Fail-Closed body layout mismatch @ %p)", fn1);
        return;
    }

    g_origProcessAsyncTexQueue = reinterpret_cast<ProcessAsyncTexQueue_t>(fn1);

    // Allocate 2 independent W^X RX relay stubs (1 per callsite) so partial-rollback on
    // ThreadFreezeGuard RIP collision can NEVER free a page still referenced by the sibling callsite.
    char msg1[224], msg2[224];
    snprintf(msg1, sizeof(msg1), "Hooked RenderThread AsyncTextureUpload[1] @ %p (fn=%p) -> 1.2ms/frame Time-Slice",
             static_cast<void*>(cs1), static_cast<void*>(fn1));
    snprintf(msg2, sizeof(msg2), "Hooked RenderThread AsyncTextureUpload[0] @ %p (fn=%p) -> 1.2ms/frame Time-Slice",
             static_cast<void*>(cs2), static_cast<void*>(fn1));
    QueueRel32NearRelayHook(patches, cs1, reinterpret_cast<const void*>(&HookProcessAsyncTextureQueue), msg1);
    QueueRel32NearRelayHook(patches, cs2, reinterpret_cast<const void*>(&HookProcessAsyncTextureQueue), msg2);
}

int LuaHyperSmoothGetStatus(void* L) {
    if (!g_luaPushNumber || !g_luaPushBoolean) return 0;
    g_luaPushNumber(L, static_cast<double>(g_currentFps.load(std::memory_order_relaxed)));
    g_luaPushBoolean(L, g_skeletalTweenEnabled.load(std::memory_order_relaxed) != 0 ? 1 : 0);
    return 2;
}

int LuaHyperSmoothGetFrameStats(void* L) {
    if (!g_luaPushNumber) return 0;
    double avgFps = 0.0, low1Fps = 0.0, p99Ms = 0.0, jitterMs = 0.0;
    g_frameRing.ComputeStats(avgFps, low1Fps, p99Ms, jitterMs);
    g_luaPushNumber(L, avgFps);
    g_luaPushNumber(L, low1Fps);
    g_luaPushNumber(L, p99Ms);
    g_luaPushNumber(L, jitterMs);
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_gcDeferredCount, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(g_clientSimTickRate.load(std::memory_order_relaxed)));
    return 6;
}

int LuaHyperSmoothSetSimTickRate(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    int hz = static_cast<int>(g_luaToNumber(L, 1));
    bool ok = SetClientSimTickRate(hz);
    g_luaPushBoolean(L, ok ? 1 : 0);
    return 1;
}

int LuaHyperSmoothGetSimTickRate(void* L) {
    if (!g_luaPushNumber) return 0;
    g_luaPushNumber(L, static_cast<double>(g_clientSimTickRate.load(std::memory_order_acquire)));
    return 1;
}

int LuaHyperSmoothVersion(void* L) {
    if (!g_luaPushString) return 0;
    g_luaPushString(L, "DST-HyperSmooth/2.4.1");
    return 1;
}

void RegisterHyperSmoothLuaApi(void* L) {
    if (!L || !g_luaPushCClosure || !g_luaSetField) return;
    auto reg = [&](const char* name, LuaCFunction_t fn) {
        g_luaPushCClosure(L, fn, 0);
        g_luaSetField(L, kLuaGlobalsIndex, name);
    };
    reg("HyperSmooth_SetFps", &LuaHyperSmoothSetFps);
    reg("HyperSmooth_SetSkeletal", &LuaHyperSmoothSetSkeletal);
    reg("HyperSmooth_SetWorldInterp", &LuaHyperSmoothSetWorldInterp);
    reg("HyperSmooth_GetStatus", &LuaHyperSmoothGetStatus);
    reg("HyperSmooth_GetFrameStats", &LuaHyperSmoothGetFrameStats);
    reg("HyperSmooth_SetSimTickRate", &LuaHyperSmoothSetSimTickRate);
    reg("HyperSmooth_GetSimTickRate", &LuaHyperSmoothGetSimTickRate);
    if (g_luaPushString) {
        reg("HyperSmooth_Version", &LuaHyperSmoothVersion);
    }
}

void HookLuaOpenLibs(void* L) {
    // CRITICAL SAFETY INVARIANT: every fresh Lua VM (Main Menu, Public Server, Shard Switch)
    // unconditionally resets Client SimTick back to vanilla 30Hz so 60Hz can NEVER leak,
    // and temporarily unlocks unlimited Texture Streaming upload speed during loading screens!
    g_inGameplay.store(0, std::memory_order_release);
    g_vmBaseInterpFrames.store(InterlockedCompareExchange64(&g_animInterpFrames, 0, 0), std::memory_order_relaxed);
    g_vmBaseWorldPos.store(InterlockedCompareExchange64(&g_worldPosInterpCount, 0, 0), std::memory_order_relaxed);
    g_vmBaseSpatialQueries.store(InterlockedCompareExchange64(&g_clientSpatialQueries, 0, 0), std::memory_order_relaxed);
    SetClientSimTickRate(30);
    if (g_origLuaOpenLibs) {
        g_origLuaOpenLibs(L);
    }
    RegisterHyperSmoothLuaApi(L);
}

void PrepareLuaBridgeHook(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    constexpr uint8_t kSigOpenLibs[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20,
        0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x8B, 0xF9,
        0x48, 0x8D, 0x1D, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x85, 0xC0, 0x74, 0x35,
        0x45, 0x33, 0xC0, 0x48, 0x8B, 0xD0, 0x48, 0x8B, 0xCF,
        0xE8, 0x00, 0x00, 0x00, 0x00
    };
    constexpr char kMaskOpenLibs[] = "xxxxxxxxxxxxx????xxxxxx????xxxxxxxxxxxxxxx????";

    constexpr uint8_t kSigSetField[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30,
        0x4D, 0x8B, 0xD0, 0x48, 0x8B, 0xF1, 0xE8, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x83, 0xC9, 0xFF, 0x48, 0x8B, 0xD8, 0x49, 0x8B, 0xFA, 0x33, 0xC0, 0x49, 0x8B, 0xD2,
        0xF2, 0xAE, 0x48, 0xF7, 0xD1, 0x4C, 0x8D, 0x41, 0xFF, 0x48, 0x8B, 0xCE, 0xE8, 0x00, 0x00, 0x00, 0x00,
        0x4C, 0x8B, 0x4E, 0x10, 0x4C, 0x8D, 0x44, 0x24, 0x20, 0x49, 0x83, 0xE9, 0x10,
        0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCE, 0x48, 0x89, 0x44, 0x24, 0x20,
        0xC7, 0x44, 0x24, 0x28, 0x04, 0x00, 0x00, 0x00, 0xE8, 0x00, 0x00, 0x00, 0x00
    };
    constexpr char kMaskSetField[] = "xxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????";

    constexpr uint8_t kSigPushNumber[] = {
        0x48, 0x8B, 0x41, 0x10, 0xF2, 0x0F, 0x11, 0x08, 0xC7, 0x40, 0x08, 0x03, 0x00, 0x00, 0x00
    };
    constexpr uint8_t kSigPushBoolean[] = {
        0x4C, 0x8B, 0x41, 0x10, 0x33, 0xC0, 0x85, 0xD2, 0x0F, 0x95, 0xC0,
        0x41, 0xC7, 0x40, 0x08, 0x01, 0x00, 0x00, 0x00, 0x41, 0x89, 0x00
    };
    constexpr uint8_t kSigPushString[] = {
        0x48, 0x83, 0xEC, 0x28, 0x4C, 0x8B, 0xC9, 0x48, 0x85, 0xD2, 0x75, 0x11, 0x48, 0x8B, 0x41, 0x10,
        0x89, 0x50, 0x08, 0x48, 0x83, 0x41, 0x10, 0x10, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x48, 0x83, 0xC9
    };
    constexpr uint8_t kSigToNumber[] = {
        0x48, 0x83, 0xEC, 0x38, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x83, 0x78, 0x08, 0x03, 0x74, 0x1B,
        0x48, 0x8D, 0x54, 0x24, 0x20, 0x48, 0x8B, 0xC8, 0xE8, 0x00, 0x00, 0x00, 0x00,
        0x48, 0x85, 0xC0, 0x75, 0x09, 0x66, 0x0F, 0x57, 0xC0, 0x48, 0x83, 0xC4, 0x38, 0xC3
    };
    constexpr char kMaskToNumber[] = "xxxxx????xxxxxxxxxxxxxxx????xxxxxxxxxxxxxx";
    constexpr uint8_t kSigToBoolean[] = {
        0x48, 0x83, 0xEC, 0x28, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x8B, 0x48, 0x08, 0x85, 0xC9, 0x74, 0x14,
        0x83, 0xF9, 0x01, 0x75, 0x05, 0x83, 0x38, 0x00, 0x74, 0x0A, 0xB8, 0x01, 0x00, 0x00, 0x00,
        0x48, 0x83, 0xC4, 0x28, 0xC3, 0x33, 0xC0
    };
    constexpr char kMaskToBoolean[] = "xxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

    uint8_t* pOpenLibs = FindMaskedPatternInText(textStart, textSize, kSigOpenLibs, kMaskOpenLibs, sizeof(kSigOpenLibs));
    uint8_t* pSetField = FindMaskedPatternInText(textStart, textSize, kSigSetField, kMaskSetField, sizeof(kSigSetField));
    uint8_t* pPushNum  = FindPatternInText(textStart, textSize, kSigPushNumber, sizeof(kSigPushNumber));
    uint8_t* pPushBool = FindPatternInText(textStart, textSize, kSigPushBoolean, sizeof(kSigPushBoolean));
    uint8_t* pPushStr  = FindPatternInText(textStart, textSize, kSigPushString, sizeof(kSigPushString));
    uint8_t* pToNum    = FindMaskedPatternInText(textStart, textSize, kSigToNumber, kMaskToNumber, sizeof(kSigToNumber));
    uint8_t* pToBool   = FindMaskedPatternInText(textStart, textSize, kSigToBoolean, kMaskToBoolean, sizeof(kSigToBoolean));
    uint8_t* pPushC    = pOpenLibs ? ResolveRel32Call(pOpenLibs + 0x29) : nullptr;

    if (!pOpenLibs || !pPushC || !pSetField || !pPushNum || !pPushBool || !pToNum || !pToBool) {
        LogMsg("Lua Bridge skipped (AOB miss: open=%p pushC=%p setF=%p pNum=%p pBool=%p tNum=%p tBool=%p)",
               pOpenLibs, pPushC, pSetField, pPushNum, pPushBool, pToNum, pToBool);
        return;
    }

    g_luaPushCClosure = reinterpret_cast<LuaPushCClosure_t>(pPushC);
    g_luaSetField     = reinterpret_cast<LuaSetField_t>(pSetField);
    g_luaPushNumber   = reinterpret_cast<LuaPushNumber_t>(pPushNum);
    g_luaPushBoolean  = reinterpret_cast<LuaPushBoolean_t>(pPushBool);
    g_luaPushString   = reinterpret_cast<LuaPushString_t>(pPushStr);
    g_luaToNumber     = reinterpret_cast<LuaToNumber_t>(pToNum);
    g_luaToBoolean    = reinterpret_cast<LuaToBoolean_t>(pToBool);

    // 17-byte trampoline for luaL_openlibs (relocates mov rax, [rip+disp32] at +10/+13)
    uint8_t* tramp = AllocNearExecutable(pOpenLibs, 64);
    if (!tramp) return;
    memcpy(tramp, pOpenLibs, 17);
    int32_t origDisp = 0;
    memcpy(&origDisp, pOpenLibs + 13, 4);
    intptr_t absTarget = reinterpret_cast<intptr_t>(pOpenLibs + 17) + origDisp;
    intptr_t newDisp = absTarget - reinterpret_cast<intptr_t>(tramp + 17);
    if (newDisp < INT32_MIN || newDisp > INT32_MAX) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return;
    }
    int32_t newDisp32 = static_cast<int32_t>(newDisp);
    memcpy(tramp + 13, &newDisp32, 4);

    uint8_t jmpBack[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint64_t resumeAddr = reinterpret_cast<uint64_t>(pOpenLibs + 17);
    memcpy(jmpBack + 6, &resumeAddr, sizeof(resumeAddr));
    memcpy(tramp + 17, jmpBack, 14);
    FinalizeExecPageWX(tramp, 31);
    g_luaOpenLibsTramp = tramp;
    g_origLuaOpenLibs = reinterpret_cast<LuaOpenLibs_t>(tramp);

    PendingCodePatch p{};
    p.addr = pOpenLibs;
    p.len = 17;
    p.ownedExecPage = tramp;
    uint8_t jmpHook[17] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0,0,0,0,0,0,0,0, 0x90, 0x90, 0x90 };
    uint64_t hookTarget = reinterpret_cast<uint64_t>(&HookLuaOpenLibs);
    memcpy(jmpHook + 6, &hookTarget, sizeof(hookTarget));
    memcpy(p.bytes, jmpHook, 17);
    char msg[224];
    snprintf(msg, sizeof(msg), "Hooked luaL_openlibs @ %p -> 0ms Direct Lua VM Bridge (W^X RX tramp)", static_cast<void*>(pOpenLibs));
    p.logMsg = msg;
    patches.push_back(std::move(p));
}

void EnsureDefaultIni() {
    if (GetFileAttributesW(g_iniPath.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    FILE* f = _wfopen(g_iniPath.c_str(), L"w");
    if (!f) return;
    fprintf(f,
        "; DST HyperSmooth & FastLoad Native Companion Plugin Configuration (v2.4.0)\n"
        "; Note: If you also use the Steam Workshop Companion Mod, in-game ESC->Settings->Graphics overrides these live.\n"
        "[FPS]\n"
        "target_fps=240\n"
        "\n"
        "[OPTIMIZE]\n"
        "high_res_timer=1\n"
        "boost_priority=1\n"
        "prewarm_bundles=1\n"
        "rtss_frame_pacer=1\n"
        "hybrid_frame_pacer=1\n"
        "fast_mimalloc_lua=1\n"
        "fast_vector_fill=1\n"
        "fast_libdeflate_zip=1\n"
        "fast_zlib_inflate=1\n"
        "smooth_skeletal_anim=1\n"
        "world_pos_interp=1\n"
        "raknet_zero_stall=1\n"
        "low_latency_gpu_sync=1\n"
        "smooth_lua_gc=1\n"
        "fast_spatial_query=1\n"
        "timeslice_tex_upload=1\n");
    fclose(f);
}

int ReadIniInt(const wchar_t* sec, const wchar_t* key, int defVal) {
    return static_cast<int>(GetPrivateProfileIntW(sec, key, defVal, g_iniPath.c_str()));
}

uint64_t GetFileModTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
           static_cast<uint64_t>(data.ftLastWriteTime.dwLowDateTime);
}

bool FindNewestModBridgeFile(std::wstring& outPath, uint64_t& outMtime) {
    wchar_t docsPath[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docsPath))) return false;
    const wchar_t* roots[] = { L"\\Klei\\DoNotStarveTogether\\", L"\\Klei\\DoNotStarveTogetherRail\\" };

    uint64_t bestTime = 0;
    std::wstring bestPath;
    for (const wchar_t* relRoot : roots) {
        std::wstring root = std::wstring(docsPath) + relRoot;
        // Direct client_save (non-ID layouts)
        {
            std::wstring direct = root + L"client_save\\dst_smooth_highfps_cfg";
            uint64_t mt = GetFileModTime(direct);
            if (mt > bestTime) { bestTime = mt; bestPath = direct; }
        }
        // Per-account <id>\client_save layouts
        std::wstring search = root + L"*";
        WIN32_FIND_DATAW fd{};
        HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) continue;
        do {
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
                std::wstring candidate = root + fd.cFileName + L"\\client_save\\dst_smooth_highfps_cfg";
                uint64_t mt = GetFileModTime(candidate);
                if (mt > bestTime) { bestTime = mt; bestPath = candidate; }
            }
        } while (FindNextFileW(hFind, &fd));
        FindClose(hFind);
    }

    if (bestTime > 0) { outPath = bestPath; outMtime = bestTime; return true; }
    return false;
}

// Supports both legacy "<fps>" and v2.0.0 "fps=<N>;skeletal=<0|1>" formats
bool ParseModBridgeFile(const std::wstring& path, int& outFps, bool& hasFps, int& outSkeletal, bool& hasSkeletal) {
    hasFps = false;
    hasSkeletal = false;
    FILE* f = _wfopen(path.c_str(), L"r");
    if (!f) return false;
    char buf[128]{};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return false;

    if (const char* p = strstr(buf, "fps=")) {
        outFps = static_cast<int>(strtol(p + 4, nullptr, 10));
        hasFps = true;
    }
    if (const char* p = strstr(buf, "skeletal=")) {
        outSkeletal = static_cast<int>(strtol(p + 9, nullptr, 10));
        hasSkeletal = true;
    }
    if (!hasFps && !hasSkeletal) {
        char* endp = nullptr;
        long v = strtol(buf, &endp, 10);
        if (endp != buf) {
            outFps = static_cast<int>(v);
            hasFps = true;
        }
    }
    return hasFps || hasSkeletal;
}

DWORD WINAPI PrewarmBundlesProc(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    std::wstring bundlesDir = g_binDir + L"..\\data\\databundles\\";
    std::wstring search = bundlesDir + L"*.zip";
    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    constexpr size_t kBufSize = 1024 * 1024;
    auto* buf = new uint8_t[kBufSize];
    size_t totalBytes = 0;
    DWORD t0 = GetTickCount();

    do {
        if (g_stopFlag.load(std::memory_order_relaxed) != 0) break;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring fpath = bundlesDir + fd.cFileName;
            HANDLE hf = CreateFileW(fpath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (hf != INVALID_HANDLE_VALUE) {
                DWORD nread = 0;
                while (g_stopFlag.load(std::memory_order_relaxed) == 0 &&
                       ReadFile(hf, buf, static_cast<DWORD>(kBufSize), &nread, nullptr) && nread > 0) {
                    totalBytes += nread;
                }
                CloseHandle(hf);
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    delete[] buf;
    LogMsg("Pre-warmed databundles: %.1f MB in %lu ms",
           static_cast<double>(totalBytes) / (1024.0 * 1024.0), GetTickCount() - t0);
    return 0;
}

DWORD WINAPI WorkerProc(LPVOID) {
    if (!IsTargetDstClientProcess()) {
        return 0;
    }

    wchar_t modPath[MAX_PATH]{};
    GetModuleFileNameW(g_selfModule, modPath, MAX_PATH);
    std::wstring mp(modPath);
    size_t slash = mp.find_last_of(L"\\/");
    g_binDir = (slash != std::wstring::npos) ? mp.substr(0, slash + 1) : L"";
    g_iniPath = g_binDir + L"dst_fps.ini";
    g_logPath = g_binDir + L"dst_fps_plugin.log";

    AcquireSRWLockExclusive(&g_logLock);
    if (FILE* f = _wfopen(g_logPath.c_str(), L"w")) fclose(f);
    ReleaseSRWLockExclusive(&g_logLock);

    LogMsg("DST HyperSmooth & FastLoad Native Companion Plugin v2.4.1 (RTSS Pacer + mimalloc + Boss Anti-Hitch + Firefly Spatial Matcher) loaded.");

    // Initialize QPC frequency scale BEFORE installing any timing/pacer/GC/anim hooks
    {
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        g_qpcFreq = (freq.QuadPart > 0) ? freq.QuadPart : 10000000;
        g_qpcInvFreq = 1.0 / static_cast<double>(g_qpcFreq);
        g_qpcToUs = 1000000.0 / static_cast<double>(g_qpcFreq);
    }

    EnsureDefaultIni();

    bool optTimer = ReadIniInt(L"OPTIMIZE", L"high_res_timer", 1) != 0;
    bool optPriority = ReadIniInt(L"OPTIMIZE", L"boost_priority", 1) != 0;
    bool optPrewarm = ReadIniInt(L"OPTIMIZE", L"prewarm_bundles", 1) != 0;
    bool optHybridPacer = ReadIniInt(L"OPTIMIZE", L"hybrid_frame_pacer", 1) != 0;
    bool optRtssPacer = ReadIniInt(L"OPTIMIZE", L"rtss_frame_pacer", optHybridPacer ? 1 : 0) != 0;
    bool optMimallocLua = ReadIniInt(L"OPTIMIZE", L"fast_mimalloc_lua", 1) != 0;
    bool optFastVec = ReadIniInt(L"OPTIMIZE", L"fast_vector_fill", 1) != 0;
    bool optLibdeflate = ReadIniInt(L"OPTIMIZE", L"fast_libdeflate_zip", 1) != 0;
    bool optFastZlib = ReadIniInt(L"OPTIMIZE", L"fast_zlib_inflate", 1) != 0;
    bool optSkeletal = ReadIniInt(L"OPTIMIZE", L"smooth_skeletal_anim", 1) != 0;
    bool optWorldPos = ReadIniInt(L"OPTIMIZE", L"world_pos_interp", 1) != 0;
    bool optRakZeroStall = ReadIniInt(L"OPTIMIZE", L"raknet_zero_stall", 1) != 0;
    bool optLowLatGpu = ReadIniInt(L"OPTIMIZE", L"low_latency_gpu_sync", 1) != 0;
    bool optSmoothGc = ReadIniInt(L"OPTIMIZE", L"smooth_lua_gc", 1) != 0;
    bool optFastSpatial = ReadIniInt(L"OPTIMIZE", L"fast_spatial_query", 1) != 0;
    bool optTexTimeSlice = ReadIniInt(L"OPTIMIZE", L"timeslice_tex_upload", 1) != 0;
    int initFps = ReadIniInt(L"FPS", L"target_fps", 240);
    if (initFps < 0) initFps = 0;
    if (initFps > 1000) initFps = 1000;
    g_currentFps.store(initFps, std::memory_order_relaxed);
    g_skeletalTweenEnabled.store(optSkeletal ? 1 : 0, std::memory_order_relaxed);
    g_worldPosInterpEnabled.store(optWorldPos ? 1 : 0, std::memory_order_relaxed);
    g_optRtssPacer.store(optRtssPacer, std::memory_order_relaxed);
    g_optMimallocLua.store(optMimallocLua, std::memory_order_relaxed);
    g_optRakNetZeroStall.store(optRakZeroStall, std::memory_order_relaxed);
    g_optLowLatencyGpuSync.store(optLowLatGpu, std::memory_order_relaxed);
    g_optFastSpatialQuery.store(optFastSpatial, std::memory_order_relaxed);
    g_optTexTimeSlice.store(optTexTimeSlice, std::memory_order_relaxed);

    if (optTimer && timeBeginPeriod(1) == TIMERR_NOERROR) {
        g_timerBoosted = true;
        LogMsg("Enabled 1ms high-resolution Windows timer.");
    }
    // Use ABOVE_NORMAL (not HIGH) for client so 240Hz UI spinning never starves
    // local dedicated server / DSA child worldgen processes on low-core CPUs!
    if (optPriority && SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS)) {
        LogMsg("Boosted client process -> ABOVE_NORMAL_PRIORITY_CLASS.");
    }

    uint8_t* textStart = nullptr;
    size_t textSize = 0;
    if (GetTextSection(textStart, textSize)) {
        // Phase 1: Read-only AOB scanning & W^X trampoline preparation (0 threads suspended)
        std::vector<PendingCodePatch> patches;
        patches.reserve(24);

        if (optMimallocLua) PrepareClientMimallocHook(patches, textStart, textSize);
        if (optFastVec) PrepareVectorMemsetPatch(patches, textStart, textSize);
        if (optLibdeflate) PrepareLibdeflateUnzHook(patches, textStart, textSize);
        if (optFastZlib) PrepareFastInflatePatch(patches, textStart, textSize);
        if (optSmoothGc) PrepareSmoothLuaGcHook(patches, textStart, textSize);
        if (optRakZeroStall) PrepareRakNetZeroStallPatch(patches, textStart, textSize);
        if (optFastSpatial) PrepareClientSpatialZeroCopyHooks(patches, textStart, textSize);
        if (optTexTimeSlice) PrepareTexStreamTimeSliceHook(patches, textStart, textSize);
        PrepareSkeletalAnimTweenHooks(patches, textStart, textSize);
        InitClientSimTickPointers(textStart, textSize);

        size_t flPatchIdx = static_cast<size_t>(-1);
        g_patchAddr = FindPatternInText(textStart, textSize, kSigFrameLimiter, sizeof(kSigFrameLimiter));
        if (g_patchAddr) {
            LogMsg("Found FrameLimiter::SetTargetFrameTime @ %p & FrameLimiter::Wait @ %p",
                   static_cast<void*>(g_patchAddr), static_cast<void*>(g_patchAddr + 0x10));

            // 1. Hook FrameLimiter::Wait (g_patchAddr + 0x10, 117B function) -> Zero-Contention RTSS Frame Pacer + 1% Low Ring!
            {
                PendingCodePatch wp{};
                wp.addr = g_patchAddr + 0x10;
                wp.len = 14;
                uint8_t jmpW[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
                uint64_t wTarget = reinterpret_cast<uint64_t>(&HookFrameLimiterWait);
                memcpy(jmpW + 6, &wTarget, sizeof(wTarget));
                memcpy(wp.bytes, jmpW, 14);
                char msg[224];
                snprintf(msg, sizeof(msg),
                         "Hooked FrameLimiter::Wait @ %p -> Zero-Contention RTSS Waitable-Timer Pacer (rtss=%d, 1%%Low telemetry)",
                         static_cast<void*>(g_patchAddr + 0x10), optRtssPacer ? 1 : 0);
                wp.logMsg = msg;
                patches.push_back(std::move(wp));
            }

            // 2. Permanent one-shot 14-byte jump into C++ HookSetTargetFrameTime (fits inside 16-byte padded slot):
            //    Runtime ESC->Graphics / Hotkey / INI FPS switching is 100% pure atomic data store (0 .text writes ever again!)
            PendingCodePatch flp{};
            flp.addr = g_patchAddr;
            flp.len = 14;
            uint8_t jmpFl[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
            uint64_t flTarget = reinterpret_cast<uint64_t>(&HookSetTargetFrameTime);
            memcpy(jmpFl + 6, &flTarget, sizeof(flTarget));
            memcpy(flp.bytes, jmpFl, 14);
            char msg[192];
            snprintf(msg, sizeof(msg), "Hooked FrameLimiter::SetTargetFrameTime @ %p -> atomic FPS=%d (0 runtime .text writes)",
                     static_cast<void*>(g_patchAddr), initFps);
            flp.logMsg = msg;
            flPatchIdx = patches.size();
            patches.push_back(std::move(flp));
        }

        PrepareLuaBridgeHook(patches, textStart, textSize);

        // Phase 2: Atomic cold-start patch application under ThreadFreezeGuard (RIP-safe!)
        ApplyPendingPatchesSafely(patches);
        if (flPatchIdx != static_cast<size_t>(-1) && patches[flPatchIdx].applied) {
            g_frameLimiterHooked.store(true, std::memory_order_release);
        }
    }

    if (optPrewarm) {
        g_prewarmThread = CreateThread(nullptr, 0, PrewarmBundlesProc, nullptr, 0, nullptr);
    }

    uint64_t lastIniTime = GetFileModTime(g_iniPath);
    uint64_t lastModTime = 0;
    std::wstring modBridgePath;
    if (FindNewestModBridgeFile(modBridgePath, lastModTime)) {
        int mFps = 0, mSkel = 1;
        bool hFps = false, hSkel = false;
        if (ParseModBridgeFile(modBridgePath, mFps, hFps, mSkel, hSkel)) {
            if (hFps) ApplyFpsPatch(mFps);
            if (hSkel) {
                g_skeletalTweenEnabled.store(mSkel ? 1 : 0, std::memory_order_relaxed);
                LogMsg("Mod bridge -> Skeletal 240fps=%d", mSkel ? 1 : 0);
            }
        }
    }

    int tickCounter = 0;
    int lastHitchLogTick = -100;
    bool loggedInitialSummary = false;
    bool loggedBridgeQuiet = false;
    while (g_stopFlag.load(std::memory_order_relaxed) == 0) {
        Sleep(500);
        ++tickCounter;

        // Clean production summary once after entering active 3D gameplay (zero continuous log spam!)
        if (!loggedInitialSummary && (tickCounter % 6 == 0) && g_inGameplay.load(std::memory_order_acquire) != 0) {
            LONG64 bytes = InterlockedCompareExchange64(&g_libdeflateBytes, 0, 0);
            LONG64 interp = InterlockedCompareExchange64(&g_animInterpFrames, 0, 0);
            if (bytes > (1024LL * 1024LL * 1024LL) && interp > 1000) {
                loggedInitialSummary = true;
                LONG64 hits = InterlockedCompareExchange64(&g_libdeflateHits, 0, 0);
                LONG64 bones = InterlockedCompareExchange64(&g_animElemsTweened, 0, 0);
                LONG64 p2 = InterlockedCompareExchange64(&g_animPass2Swaps, 0, 0);
                LONG64 shed = InterlockedCompareExchange64(&g_animSwarmShedFrames, 0, 0);
                LONG64 gcFull = InterlockedCompareExchange64(&g_gcEmergencyFulls, 0, 0);
                LONG64 gcDef = InterlockedCompareExchange64(&g_gcDeferredCount, 0, 0);
                LONG64 miAlloc = InterlockedCompareExchange64(&g_miLuaAllocs, 0, 0);
                LONG64 posInterp = InterlockedCompareExchange64(&g_worldPosInterpCount, 0, 0);
                LONG64 posBypass = InterlockedCompareExchange64(&g_worldPosBypassCount, 0, 0);
                LONG64 spQ = InterlockedCompareExchange64(&g_clientSpatialQueries, 0, 0);
                LONG64 spC = InterlockedCompareExchange64(&g_clientSpatialCells, 0, 0);
                LONG64 texUp = InterlockedCompareExchange64(&g_texUploadsTotal, 0, 0);
                LONG64 texSl = InterlockedCompareExchange64(&g_texUploadSlices, 0, 0);
                double avgFps = 0.0, low1Fps = 0.0, p99Ms = 0.0, jitterMs = 0.0;
                g_frameRing.ComputeStats(avgFps, low1Fps, p99Ms, jitterMs);
                LogMsg("[Summary] avg=%.0f 1%%L=%.0f p99=%.2fms jit=%.2fms sim=%dHz | libdeflate=%lld (%.1fMB) | skeletal240=%lld (%lld bones, %lld swap, %lld shed) | pos240=%lld (byp=%lld) | spatial=%lld (%lld cells) | tex=%lld (slice=%lld) | gc(full=%lld defer=%lld) | mi_allocs=%lld.",
                       avgFps, low1Fps, p99Ms, jitterMs, g_clientSimTickRate.load(std::memory_order_relaxed),
                       hits, static_cast<double>(bytes) / (1024.0 * 1024.0), interp, bones, p2, shed,
                       posInterp, posBypass, spQ, spC, texUp, texSl, gcFull, gcDef, miAlloc);
            }
        }

        // Zero-spam Hitch breakdown telemetry: logs ONLY when >=1 frame >25ms occurs in active gameplay
        // (rate-limited to at most 1 line every 10 seconds, 0 MainThread/RenderThread disk I/O!)
        if (tickCounter - lastHitchLogTick >= 20) {
            const uint32_t hCnt = g_hitchPendingCount.exchange(0, std::memory_order_acq_rel);
            if (hCnt > 0) {
                lastHitchLogTick = tickCounter;
                const uint32_t peakUs = g_hitchPeakUs.exchange(0, std::memory_order_relaxed);
                const uint32_t aUs = g_hitchAnimUs.load(std::memory_order_relaxed);
                const uint32_t aEnts = g_hitchAnimEnts.load(std::memory_order_relaxed);
                const uint32_t aBones = g_hitchAnimBones.load(std::memory_order_relaxed);
                const uint32_t aShed = g_hitchAnimShed.load(std::memory_order_relaxed);
                const uint32_t gcUs = g_hitchGcUs.load(std::memory_order_relaxed);
                const int32_t  gcKb = g_hitchGcKb.load(std::memory_order_relaxed);
                const uint32_t texUs = g_hitchTexUs.load(std::memory_order_relaxed);
                const LONG64 gcFull = InterlockedCompareExchange64(&g_gcEmergencyFulls, 0, 0);
                const LONG64 spQ = InterlockedCompareExchange64(&g_clientSpatialQueries, 0, 0);
                const LONG64 texUp = InterlockedCompareExchange64(&g_texUploadsTotal, 0, 0);
                const LONG64 texSl = InterlockedCompareExchange64(&g_texUploadSlices, 0, 0);
                double avgFps = 0.0, low1Fps = 0.0, p99Ms = 0.0, jitterMs = 0.0;
                g_frameRing.ComputeStats(avgFps, low1Fps, p99Ms, jitterMs);
                LogMsg("[Hitch] peak=%.1fms (cnt=%u | avg=%.0f 1%%L=%.0f p99=%.1fms) | anim=%.2fms (ents=%u bones=%u shed=%u) | gc=%.2fms (kb=%d full=%lld) | tex=%.2fms (up=%lld slice=%lld) | spatial=%lld",
                       static_cast<double>(peakUs) * 0.001, hCnt, avgFps, low1Fps, p99Ms,
                       static_cast<double>(aUs) * 0.001, aEnts, aBones, aShed,
                       static_cast<double>(gcUs) * 0.001, gcKb, gcFull,
                       static_cast<double>(texUs) * 0.001, texUp, texSl, spQ);
            }
        }

        // Poll dst_fps.ini once per second for manual user edits
        if (tickCounter % 2 == 0) {
            uint64_t curIniTime = GetFileModTime(g_iniPath);
            if (curIniTime != 0 && curIniTime != lastIniTime) {
                lastIniTime = curIniTime;
                int fps = ReadIniInt(L"FPS", L"target_fps", 240);
                int skel = ReadIniInt(L"OPTIMIZE", L"smooth_skeletal_anim", 1);
                int wpos = ReadIniInt(L"OPTIMIZE", L"world_pos_interp", 1);
                int gsync = ReadIniInt(L"OPTIMIZE", L"low_latency_gpu_sync", 1);
                int spq = ReadIniInt(L"OPTIMIZE", L"fast_spatial_query", 1);
                int texs = ReadIniInt(L"OPTIMIZE", L"timeslice_tex_upload", 1);
                g_skeletalTweenEnabled.store(skel ? 1 : 0, std::memory_order_relaxed);
                g_worldPosInterpEnabled.store(wpos ? 1 : 0, std::memory_order_relaxed);
                g_optLowLatencyGpuSync.store(gsync != 0, std::memory_order_relaxed);
                g_optFastSpatialQuery.store(spq != 0, std::memory_order_relaxed);
                g_optTexTimeSlice.store(texs != 0, std::memory_order_relaxed);
                LogMsg("Hot-reloaded dst_fps.ini -> FPS=%d, Skeletal240=%d, WorldPos240=%d, GpuSync=%d, SpatialZC=%d, TexSlice=%d",
                       fps, skel ? 1 : 0, wpos ? 1 : 0, gsync ? 1 : 0, spq ? 1 : 0, texs ? 1 : 0);
                ApplyFpsPatch(fps);
            }
        }

        // Stop scanning Documents\Klei once 0ms direct Lua VM bridge is active!
        if (g_luaBridgeActive.load(std::memory_order_acquire) != 0) {
            if (!loggedBridgeQuiet) {
                loggedBridgeQuiet = true;
                LogMsg("0ms Direct Lua VM Bridge active -> disabled legacy Documents file polling.");
            }
            continue;
        }

        std::wstring curModPath;
        uint64_t curModTime = 0;
        if (FindNewestModBridgeFile(curModPath, curModTime) && curModTime != lastModTime) {
            lastModTime = curModTime;
            int mFps = 0, mSkel = 1;
            bool hFps = false, hSkel = false;
            if (ParseModBridgeFile(curModPath, mFps, hFps, mSkel, hSkel)) {
                if (hFps) ApplyFpsPatch(mFps);
                if (hSkel) {
                    LONG newVal = mSkel ? 1 : 0;
                    LONG prevVal = g_skeletalTweenEnabled.exchange(newVal, std::memory_order_relaxed);
                    if (prevVal != newVal) {
                        LogMsg("Hot-reloaded Mod bridge -> Skeletal240=%ld", newVal);
                    }
                }
            }
        }
    }
    return 0;
}

} // namespace

//------------------------------------------------------------------------------
// Complete 6/6 Lock-Free System32\dinput8.dll Export Pass-Through
//------------------------------------------------------------------------------
extern "C" HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
    auto fn = ResolveRealExport<DirectInput8Create_t>(g_pDirectInput8Create, "DirectInput8Create");
    if (!fn) return E_FAIL;
    return fn(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

extern "C" HRESULT WINAPI DllCanUnloadNow() {
    auto fn = ResolveRealExport<DllCanUnloadNow_t>(g_pDllCanUnloadNow, "DllCanUnloadNow");
    if (!fn) return S_FALSE;
    return fn();
}

extern "C" HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv) {
    auto fn = ResolveRealExport<DllGetClassObject_t>(g_pDllGetClassObject, "DllGetClassObject");
    if (!fn) return CLASS_E_CLASSNOTAVAILABLE;
    return fn(rclsid, riid, ppv);
}

extern "C" HRESULT WINAPI DllRegisterServer() {
    auto fn = ResolveRealExport<DllRegisterServer_t>(g_pDllRegisterServer, "DllRegisterServer");
    if (!fn) return E_FAIL;
    return fn();
}

extern "C" HRESULT WINAPI DllUnregisterServer() {
    auto fn = ResolveRealExport<DllUnregisterServer_t>(g_pDllUnregisterServer, "DllUnregisterServer");
    if (!fn) return E_FAIL;
    return fn();
}

extern "C" const void* WINAPI GetdfDIJoystick() {
    auto fn = ResolveRealExport<GetdfDIJoystick_t>(g_pGetdfDIJoystick, "GetdfDIJoystick");
    if (!fn) return nullptr;
    return fn();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfModule = hModule;
        DisableThreadLibraryCalls(hModule);
        // Non-DST processes get pure zero-overhead pass-through (0 threads, 0 hooks, 0 logs)
        if (!IsTargetDstClientProcess()) {
            return TRUE;
        }
        // Pin DLL in DST client process so hooked callsites can NEVER jump into unmapped memory
        HMODULE pinned = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(&DllMain), &pinned);
        g_workerThread = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_stopFlag.store(1, std::memory_order_relaxed);
        // Process terminating (lpReserved != nullptr): OS reclaims threads, timers, and DLLs atomically.
        // NEVER WaitForSingleObject / FreeLibrary / timeEndPeriod under Loader Lock!
        if (lpReserved != nullptr) {
            return TRUE;
        }
    }
    return TRUE;
}
