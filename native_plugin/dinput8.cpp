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
#include <dxgi1_4.h>
#include <d3d9.h>
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
#include "gl_state_cache.h"
#include "fs_neg_cache.h"
#include "fast_astar.h"
#include "fast_astar_manager.h"
#include "fast_dumper.h"

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
using GlTexParameteri_t = void (WINAPI*)(uint32_t target, uint32_t pname, int32_t param);
using GlTexParameterf_t = void (WINAPI*)(uint32_t target, uint32_t pname, float param);
using GlGetFloatv_t = void (WINAPI*)(uint32_t pname, float* params);
using GlGetString_t = const char* (WINAPI*)(uint32_t name);
using GlShaderSource_t = void (WINAPI*)(uint32_t shader, int32_t count, const char* const* string, const int32_t* length);
using GlLinkProgram_t = void (WINAPI*)(uint32_t program);
using GlUseProgram_t = void (WINAPI*)(uint32_t program);
using GlDeleteProgram_t = void (WINAPI*)(uint32_t program);
using GlGetUniformLocation_t = int32_t (WINAPI*)(uint32_t program, const char* name);
using GlGetProgramiv_t = void (WINAPI*)(uint32_t program, uint32_t pname, int32_t* params);
using GlUniform4fv_t = void (WINAPI*)(int32_t location, int32_t count, const float* value);
using EglMakeCurrent_t = uint32_t (WINAPI*)(void* dpy, void* draw, void* read, void* ctx);
using EglSwapBuffers_t = uint32_t (WINAPI*)(void* dpy, void* surface);
using GlActiveTexture_t = void (WINAPI*)(uint32_t texture);
using GlBindTexture_t = void (WINAPI*)(uint32_t target, uint32_t texture);
using GlDeleteTextures_t = void (WINAPI*)(int32_t n, const uint32_t* textures);
using GlBlendFunc_t = void (WINAPI*)(uint32_t sfactor, uint32_t dfactor);
using GlDepthMask_t = void (WINAPI*)(uint8_t flag);
using GlEnable_t = void (WINAPI*)(uint32_t cap);
using GlDisable_t = void (WINAPI*)(uint32_t cap);
using GlBindBuffer_t = void (WINAPI*)(uint32_t target, uint32_t buffer);
using GlDeleteBuffers_t = void (WINAPI*)(int32_t n, const uint32_t* buffers);
using GlBlendEquation_t = void (WINAPI*)(uint32_t mode);
using GlColorMask_t = void (WINAPI*)(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
using GlCullFace_t = void (WINAPI*)(uint32_t mode);
using GlDepthFunc_t = void (WINAPI*)(uint32_t func);
using GlEnableVertexAttribArray_t = void (WINAPI*)(uint32_t index);
using GlDisableVertexAttribArray_t = void (WINAPI*)(uint32_t index);
using GlFrontFace_t = void (WINAPI*)(uint32_t mode);
using GlScissor_t = void (WINAPI*)(int32_t x, int32_t y, int32_t width, int32_t height);
using GlViewport_t = void (WINAPI*)(int32_t x, int32_t y, int32_t width, int32_t height);
using GlUniform1f_t = void (WINAPI*)(int32_t location, float x);
using GlUniform1i_t = void (WINAPI*)(int32_t location, int32_t x);
using GlUniform2fv_t = void (WINAPI*)(int32_t location, int32_t count, const float* v);
using GlUniform3fv_t = void (WINAPI*)(int32_t location, int32_t count, const float* v);
using GlUniformMatrix3fv_t = void (WINAPI*)(int32_t location, int32_t count, uint8_t transpose, const float* value);
using GlUniformMatrix4fv_t = void (WINAPI*)(int32_t location, int32_t count, uint8_t transpose, const float* value);
using GetFileAttributesW_t = DWORD (WINAPI*)(LPCWSTR lpFileName);
using MoveFileExW_t = BOOL (WINAPI*)(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags);
using CopyFileW_t = BOOL (WINAPI*)(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists);
using DeleteFileW_t = BOOL (WINAPI*)(LPCWSTR lpFileName);
using CreateDirectoryW_t = BOOL (WINAPI*)(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
using GameRender_t = void (*)(void* self, void* renderData);
using PostProcessorRender_t = void (*)(void* pp, void* renderData, uint32_t primaryTexHandle);
using CreateDXGIFactory1_t = HRESULT (WINAPI*)(REFIID riid, void** ppFactory);
using ClientSpatialQuery_t = void (*)(void* grid, const float* posXZ, float radius, void* visitor);
using ClientSpatialVisitor_t = uint8_t (*)(void* visitor, void* elem);
using ProcessAsyncTexQueue_t = void (*)(void* loader);
using ParticleMgrUpdate_t = void (*)(void* mgr, float dt);
using ParticleRenderCmdCtor_t = void* (*)(void* cmd, void* renderNode, void* mtx);
using TransformGetWorldMatrix_t = const float* (*)(void* transform);
using LightMapRender_t = void (*)(void* lightMap, uint8_t* renderData);

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
std::atomic<bool> g_optD3d9ExLowLatency{true};
std::atomic<bool> g_optFastSpatialQuery{true};
std::atomic<bool> g_optTexTimeSlice{true};
std::atomic<bool> g_optGlStateCache{true};
std::atomic<bool> g_optGlUniformCache{true};
std::atomic<bool> g_optFsNegCache{true};
std::atomic<bool> g_optAdaptiveDeJitter{true};
std::atomic<uint32_t> g_glCacheResetGen{1};
std::atomic<uint32_t> g_displayRefreshHz{0};
std::atomic<int>  g_visualAnisoLevel{1};
std::atomic<bool> g_visualFullresLightmap{false};
std::atomic<float> g_visualMipLodBias{0.0f}; // Player-First default: 0.0f (100% vanilla)
std::atomic<uint32_t> g_d3d9ExLatGen{1};
std::atomic<bool> g_angleSamplerHooked{false};
std::atomic<bool> g_d3d9ExActive{false};
std::atomic<uint32_t> g_d3d9ExOrigLatency{3};
std::atomic<uint32_t> g_d3d9ExCurLatency{3};
volatile LONG64   g_mipBiasAppliedHits = 0;

// [v2.6.0] 240Hz Render-Only VFX & Particle Interpolation (VFXEffect + ParticleEmitter)
std::atomic<LONG> g_vfxSmoothEnabled{1};
std::atomic<bool> g_vfxSmoothHooked{false};
std::atomic<int64_t> g_vfxLastTickQpc{0};
std::atomic<float> g_vfxTickPeriodSec{1.0f / 30.0f};
std::atomic<float> g_vfxLastSimDt{1.0f / 30.0f};
ParticleMgrUpdate_t g_origParticleMgrUpdate = nullptr;
ParticleRenderCmdCtor_t g_origVfxRenderCmdCtor = nullptr;
ParticleRenderCmdCtor_t g_origEmitterRenderCmdCtor = nullptr;
volatile LONG64   g_vfxCmdsSmoothed = 0;
volatile LONG64   g_vfxParticlesSmoothed = 0;

// [v2.6.0] 240Hz Moving Dynamic Light Interpolation + 0ms Live LightMap RT Hot-Swap
TransformGetWorldMatrix_t g_origTransformGetWorldMatrix = nullptr;
LightMapRender_t          g_origLightMapRender = nullptr;
std::atomic<bool>         g_lightPos240Hooked{false};
std::atomic<bool>         g_lightmapHotSwapHooked{false};
std::atomic<uint32_t>     g_lightmapRebuildGen{0};
volatile LONG64           g_lightPos240Count = 0;
GlTexParameteri_t g_origGlTexParameteri = nullptr;
GlTexParameterf_t g_fnGlTexParameterf   = nullptr;
GlGetFloatv_t     g_fnGlGetFloatv       = nullptr;
GlGetString_t     g_fnGlGetString       = nullptr;
std::atomic<int>  g_afCapsProbeState{0}; // 0=unprobed, 1=ready(supported), -1=unsupported
std::atomic<float> g_hwMaxAniso{1.0f};
bool              g_angleAfUnlocked = false;
bool              g_afIatHooked     = false;
bool              g_lightmapPatched = false;
volatile float*   g_lightmapScaleSlot = nullptr;
volatile LONG64   g_afAppliedHits = 0;
volatile LONG64   g_afTrilinearUpgrades = 0;

// Visual Fidelity Calm World Shaders (Player-First Default OFF: params={contactAo, calmEnv, 0, sunElev})
struct Hd2dUniformState {
    float params[4]; // (contactAoStr, calmEnvStr, reserved, sunElev)
    float sun[4];    // (sunWorldX, sunWorldZ, camViewX, camViewZ)
    float tint[4];   // (tintR, tintG, tintB, rainWetness)
};
struct Hd2dProgSlot {
    void*    ctx;
    bool     checked;
    bool     linkedOk;
    bool     hasAny;
    int32_t  locParams;
    int32_t  locSun;
    int32_t  locTint;
    uint32_t lastGen;
};
constexpr uint32_t kMaxHd2dProgs = 4096u;
SRWLOCK               g_hd2dStateLock = SRWLOCK_INIT;
SRWLOCK               g_hd2dProgLock  = SRWLOCK_INIT;
Hd2dUniformState      g_hd2dState = {
    { 0.0f, 0.0f, 0.0f, 0.70f },
    { 0.707f, 0.707f, 0.0f, -1.0f },
    { 1.0f, 0.94f, 0.82f, 0.0f }
};
std::atomic<uint32_t> g_hd2dStateGen{1};
std::atomic<bool>     g_optHd2dWorldShaders{true};
std::atomic<float>    g_hd2dMasterStr{0.0f};
std::atomic<uint32_t> g_hd2dShadersInjected{0};
std::atomic<uint32_t> g_hd2dProgsLinked{0};
Hd2dProgSlot          g_hd2dProgs[kMaxHd2dProgs]{};
// Multi-context overflow map when two distinct non-shared EGL contexts use the same program ID
struct Hd2dCtxProgEntry {
    void*        ctx;
    uint32_t     program;
    Hd2dProgSlot slot;
};
std::vector<Hd2dCtxProgEntry> g_hd2dOverflowProgs;
GlShaderSource_t      g_origGlShaderSource = nullptr;
GlLinkProgram_t       g_origGlLinkProgram = nullptr;
GlUseProgram_t        g_origGlUseProgram = nullptr;
GlDeleteProgram_t     g_origGlDeleteProgram = nullptr;
GlGetUniformLocation_t g_fnGlGetUniformLocation = nullptr;
GlGetProgramiv_t      g_fnGlGetProgramiv = nullptr;
GlUniform4fv_t        g_fnGlUniform4fv = nullptr;
bool                  g_hd2dIatHooked = false;
bool                  g_glCacheIatHooked = false;
std::atomic<bool>     g_glCacheActive{false};
std::atomic<bool>     g_glUniformCacheActive{false};
bool                  g_eglSwapIatHooked = false;
bool                  g_renderPerfHooked = false;

hs_fs_neg::NegativeStatCache g_fsNegCache;
GetFileAttributesW_t  g_origGetFileAttributesW = nullptr;
MoveFileExW_t         g_origMoveFileExW = nullptr;
CopyFileW_t           g_origCopyFileW = nullptr;
DeleteFileW_t         g_origDeleteFileW = nullptr;
CreateDirectoryW_t    g_origCreateDirectoryW = nullptr;
std::atomic<bool>     g_fsNegCacheActive{false};
volatile LONG64       g_fsNegQueries = 0;
volatile LONG64       g_fsNegHits = 0;
volatile LONG64       g_deJitterHits = 0;
std::atomic<uint32_t> g_deJitterPeakUs{0};

// Cold-start patch indices (status flags flip ONLY after ApplyPendingPatchesSafely confirms applied!)
size_t g_vfxPmPatchIdx         = static_cast<size_t>(-1);
size_t g_vfxCmdPatchIdx        = static_cast<size_t>(-1);
size_t g_vfxEmitPatchIdx       = static_cast<size_t>(-1);
size_t g_angleAfPatchIdx       = static_cast<size_t>(-1);
size_t g_angleSamplerPatchIdx  = static_cast<size_t>(-1);
size_t g_lightmapScalePatchIdx = static_cast<size_t>(-1);
size_t g_lightmapHotSwapIdx    = static_cast<size_t>(-1);
size_t g_light240PatchIdx      = static_cast<size_t>(-1);
size_t g_gameRenderPatchIdx    = static_cast<size_t>(-1);
size_t g_postProcPatchIdx      = static_cast<size_t>(-1);
EglMakeCurrent_t      g_origEglMakeCurrent = nullptr;
EglSwapBuffers_t      g_origEglSwapBuffers = nullptr;
GlActiveTexture_t     g_origGlActiveTexture = nullptr;
GlBindTexture_t       g_origGlBindTexture = nullptr;
GlDeleteTextures_t    g_origGlDeleteTextures = nullptr;
GlBlendFunc_t         g_origGlBlendFunc = nullptr;
GlDepthMask_t         g_origGlDepthMask = nullptr;
GlEnable_t                   g_origGlEnable = nullptr;
GlDisable_t                  g_origGlDisable = nullptr;
GlBindBuffer_t               g_origGlBindBuffer = nullptr;
GlDeleteBuffers_t            g_origGlDeleteBuffers = nullptr;
GlBlendEquation_t            g_origGlBlendEquation = nullptr;
GlColorMask_t                g_origGlColorMask = nullptr;
GlCullFace_t                 g_origGlCullFace = nullptr;
GlDepthFunc_t                g_origGlDepthFunc = nullptr;
GlEnableVertexAttribArray_t  g_origGlEnableVertexAttribArray = nullptr;
GlDisableVertexAttribArray_t g_origGlDisableVertexAttribArray = nullptr;
GlFrontFace_t                g_origGlFrontFace = nullptr;
GlScissor_t                  g_origGlScissor = nullptr;
GlViewport_t                 g_origGlViewport = nullptr;
GlUniform1f_t                g_origGlUniform1f = nullptr;
GlUniform1i_t                g_origGlUniform1i = nullptr;
GlUniform2fv_t               g_origGlUniform2fv = nullptr;
GlUniform3fv_t               g_origGlUniform3fv = nullptr;
GlUniform4fv_t               g_origGlUniform4fv = nullptr;
GlUniformMatrix3fv_t         g_origGlUniformMatrix3fv = nullptr;
GlUniformMatrix4fv_t         g_origGlUniformMatrix4fv = nullptr;
volatile LONG64              g_glCacheCalls = 0;
volatile LONG64              g_glCacheHits = 0;
volatile LONG64              g_glUniCalls = 0;
volatile LONG64              g_glUniHits = 0;
GameRender_t          g_origGameRender = nullptr;
PostProcessorRender_t g_origPostProcessorRender = nullptr;
std::atomic<float>    g_emaWorldRenderUs{0.0f};
std::atomic<float>    g_emaPostProcUs{0.0f};
std::atomic<float>    g_emaFrameBusyUs{0.0f};
std::atomic<float>    g_emaGpuDrainUs{0.0f};
std::atomic<float>    g_emaSwapUs{0.0f};
std::atomic<float>    g_processVramMb{0.0f};
std::atomic<float>    g_processVramBudgetMb{0.0f};
std::atomic<float>    g_peakVramMb{0.0f};

static inline void UpdateAtomicEma(std::atomic<float>& slot, float sample, float alpha) {
    if (!(sample >= 0.0f) || sample > 500000.0f) return;
    float prev = slot.load(std::memory_order_relaxed);
    float next = (prev <= 0.001f) ? sample : (prev * (1.0f - alpha) + sample * alpha);
    slot.store(next, std::memory_order_relaxed);
}
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
SRWLOCK g_clientSimTickLock = SRWLOCK_INIT;
uint32_t g_clientSavedPre60NetRate = 0; // 0 = unset/default 15Hz; preserved across 30->60->30 transitions!
bool g_clientHasSavedPre60NetRate = false;

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
std::atomic<uint32_t> g_lastFrameBusyUs{0};
std::atomic<uint32_t> g_lastSwapUs{0};
std::atomic<uint32_t> g_lastWorldRenderUs{0};
std::atomic<uint32_t> g_lastPostProcUs{0};
std::atomic<uint32_t> g_frameUnzUs{0};
std::atomic<uint32_t> g_frameShaderUs{0};

// Tear-free Hitch & Severe-Stall telemetry snapshot (protected by lightweight SRWLOCK only on >25ms frames & 10s flush)
struct HitchSnapshot {
    uint32_t pendingCount = 0;
    uint32_t severeCount  = 0; // frames >= 500ms (up to 60s)
    uint32_t severeMaxUs  = 0;
    uint32_t peakUs       = 0;
    uint32_t busyUs       = 0;
    uint32_t swapUs       = 0;
    uint32_t worldUs      = 0;
    uint32_t postUs       = 0;
    uint32_t unzUs        = 0;
    uint32_t shaderUs     = 0;
    uint32_t animUs       = 0;
    uint32_t animEnts     = 0;
    uint32_t animBones    = 0;
    uint32_t animShed     = 0;
    uint32_t gcUs         = 0;
    int32_t  gcKb         = 0;
    uint32_t texUs        = 0;
};
SRWLOCK       g_hitchLock = SRWLOCK_INIT;
HitchSnapshot g_hitchSnap{};

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

// Strict fail-closed unique AOB scanner: returns match ONLY if exactly 1 occurrence exists.
uint8_t* FindPatternInText(uint8_t* start, size_t size, const uint8_t* sig, size_t sigLen) {
    if (!start || sigLen == 0 || size < sigLen) return nullptr;
    uint8_t* hit = nullptr;
    for (size_t off = 0; off <= size - sigLen; ++off) {
        if (start[off] == sig[0] && memcmp(start + off, sig, sigLen) == 0) {
            if (hit) return nullptr; // ambiguous (>1 match) -> fail-closed
            hit = start + off;
        }
    }
    return hit;
}

// Strict fail-closed unique masked AOB scanner: returns match ONLY if exactly 1 occurrence exists.
uint8_t* FindMaskedPatternInText(uint8_t* start, size_t size, const uint8_t* sig, const char* mask, size_t sigLen) {
    if (!start || sigLen == 0 || size < sigLen) return nullptr;
    uint8_t* hit = nullptr;
    for (size_t off = 0; off <= size - sigLen; ++off) {
        if (mask[0] == 'x' && start[off] != sig[0]) continue;
        bool ok = true;
        for (size_t j = 1; j < sigLen; ++j) {
            if (mask[j] == 'x' && start[off + j] != sig[j]) { ok = false; break; }
        }
        if (ok) {
            if (hit) return nullptr; // ambiguous (>1 match) -> fail-closed
            hit = start + off;
        }
    }
    return hit;
}

static uint8_t* FindUniqueMaskedPatternInText(uint8_t* start, size_t size, const uint8_t* sig, const char* mask, size_t sigLen) {
    return FindMaskedPatternInText(start, size, sig, mask, sigLen);
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
    if (!VirtualProtect(addr, len, oldProt, &dummy)) {
        LogMsg("[WARN] WriteCodeBytes @ %p failed restoring page protection 0x%lX (err=%lu)",
               static_cast<void*>(addr), static_cast<unsigned long>(oldProt),
               static_cast<unsigned long>(GetLastError()));
    }
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

// Cold-start Fail-Closed RAII thread freezer: suspends sibling threads & captures RIP so .text
// patching can NEVER tear an executing instruction. If ANY sibling thread cannot be snapshotted,
// opened (other than already-exited ERROR_INVALID_PARAMETER), suspended, or inspected for RIP,
// complete_ becomes false and ApplyPendingPatchesSafely aborts patching for that pass!
class ThreadFreezeGuard {
public:
    ThreadFreezeGuard() {
        DWORD selfPid = GetCurrentProcessId();
        DWORD selfTid = GetCurrentThreadId();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap == INVALID_HANDLE_VALUE) {
            complete_ = false;
            return;
        }
        THREADENTRY32 te{};
        te.dwSize = sizeof(te);
        if (!Thread32First(snap, &te)) {
            complete_ = false;
            CloseHandle(snap);
            return;
        }
        do {
            if (te.th32OwnerProcessID == selfPid && te.th32ThreadID != selfTid) {
                HANDLE ht = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                                       FALSE, te.th32ThreadID);
                if (!ht) {
                    const DWORD err = GetLastError();
                    // ERROR_INVALID_PARAMETER means the thread exited between snapshot and OpenThread.
                    if (err != ERROR_INVALID_PARAMETER) {
                        complete_ = false;
                        break;
                    }
                    continue;
                }
                if (SuspendThread(ht) == static_cast<DWORD>(-1)) {
                    DWORD exitCode = 0;
                    const bool confirmedExited = (GetExitCodeThread(ht, &exitCode) && exitCode != STILL_ACTIVE);
                    CloseHandle(ht);
                    if (!confirmedExited) {
                        complete_ = false;
                        break;
                    }
                    continue;
                }
                CONTEXT ctx{};
                ctx.ContextFlags = CONTEXT_CONTROL;
                if (!GetThreadContext(ht, &ctx)) {
                    // Thread was suspended; push so destructor resumes it, and mark freeze incomplete!
                    threads_.push_back({ht, 0});
                    complete_ = false;
                    break;
                }
                threads_.push_back({ht, static_cast<uintptr_t>(ctx.Rip)});
            }
        } while (Thread32Next(snap, &te));
        CloseHandle(snap);
    }

    ~ThreadFreezeGuard() {
        for (auto& t : threads_) {
            ResumeThread(t.handle);
            CloseHandle(t.handle);
        }
    }

    bool IsComplete() const noexcept { return complete_; }

    bool AnyRipNear(uint8_t* addr, size_t len) const {
        if (!complete_) return true; // fail-closed!
        uintptr_t lo = reinterpret_cast<uintptr_t>(addr) - 16;
        uintptr_t hi = reinterpret_cast<uintptr_t>(addr) + len + 16;
        for (const auto& t : threads_) {
            if (t.rip == 0 || (t.rip >= lo && t.rip <= hi)) return true;
        }
        return false;
    }

private:
    bool complete_ = true;
    std::vector<SuspendedThreadInfo> threads_;
};

void ApplyPendingPatchesSafely(std::vector<PendingCodePatch>& patches) {
    if (patches.empty()) return;
    // Publish all g_orig* function pointers & RX trampolines globally BEFORE patching any .text site
    std::atomic_thread_fence(std::memory_order_release);

    int incompleteFreezes = 0;
    for (int attempt = 0; attempt < 5; ++attempt) {
        bool anyRemaining = false;
        {
            ThreadFreezeGuard freeze;
            if (!freeze.IsComplete()) {
                ++incompleteFreezes;
                anyRemaining = true;
            } else {
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
        }
        if (!anyRemaining) break;
        Sleep(1);
    }
    if (incompleteFreezes > 0) {
        LogMsg("[SAFETY] ThreadFreezeGuard incomplete on %d/5 pass(es); unapplied patches skipped fail-closed",
               incompleteFreezes);
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
    struct ScopedUnzTimer {
        int64_t t0 = 0;
        ScopedUnzTimer() {
            if (g_inGameplay.load(std::memory_order_relaxed) != 0) t0 = NowQpc();
        }
        ~ScopedUnzTimer() {
            if (t0 != 0) {
                const double us = static_cast<double>(NowQpc() - t0) * g_qpcToUs;
                if (us > 0.0 && us < 1000000.0) {
                    g_frameUnzUs.fetch_add(static_cast<uint32_t>(us), std::memory_order_relaxed);
                }
            }
        }
    } unzTimer;
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
    float     jitter = 0.0f;
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
    int64_t  batchDeJitterHits = 0;

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
        if (batchDeJitterHits) { InterlockedAdd64(&g_deJitterHits, batchDeJitterHits);     batchDeJitterHits = 0; }
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

// Shared 240FPS C0 world-position interpolator used by BOTH:
//   1) AnimRenderCmd (entities, players, mobs, projectiles)
//   2) LightMap::GenerateRenderData (moving torches, lanterns, miner hats, fireflies, spell lights)
// Zero gameplay/hitbox/physics mutation; auto-bypasses continuous 240Hz Bullet physics (Lag Comp ON).
static bool InterpolateWorldPosCore(float* pX, float* pY, float* pZ, uintptr_t key,
                                    ThreadLocalAnimState& tls, int64_t nowQpc, bool countAsAnim) {
    if (!pX || !pY || !pZ || key == 0) return false;
    if (g_worldPosInterpEnabled.load(std::memory_order_relaxed) == 0 ||
        g_skeletalTweenEnabled.load(std::memory_order_relaxed) == 0) {
        return false;
    }
    const float rawX = *pX;
    const float rawY = *pY;
    const float rawZ = *pZ;
    if (!std::isfinite(rawX) || !std::isfinite(rawY) || !std::isfinite(rawZ)) return false;

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
        tr.jitter = 0.0f;
        tr.settled = 1;
        tr.valid = 1;
    };

    if (!tr.valid || tr.entity_key != key || unseenSec > 0.16) {
        tr.period = (curSimHz == 60) ? (1.0f / 60.0f) : (1.0f / 30.0f);
        tr.sim_gen = curSimGen;
        tr.fast_streak = 0;
        snapToRaw();
        return false;
    }
    if (tr.sim_gen != curSimGen) {
        tr.period = (curSimHz == 60) ? (1.0f / 60.0f) : (1.0f / 30.0f);
        tr.jitter = 0.0f;
        tr.sim_gen = curSimGen;
        tr.fast_streak = 0;
    }

    const bool rawChanged = (rawX != tr.last_raw_x || rawY != tr.last_raw_y || rawZ != tr.last_raw_z);
    if (!rawChanged && tr.settled) {
        return false; // Ultra-fast static entity path (trees, walls, chests, idle entities)
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
            if (countAsAnim && tr.fast_streak >= 2) {
                ++tls.batchPosBypass;
            }
            snapToRaw();
            return false;
        }

        // Adaptive snapshot cadence EMA + De-Jitter EWMA (auto-locks onto 16.7ms / 33.3ms / 66.7ms!)
        if (elapsed >= 0.011f && elapsed <= 0.110f) {
            const float dev = std::min(fabsf(elapsed - tr.period), 0.060f);
            tr.jitter = tr.jitter * 0.85f + dev * 0.15f;
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

    float effPeriod = tr.period * 1.03f; // +3% base cushion (+ adaptive online jitter buffer when jitter > 2.5ms)
    if (g_optAdaptiveDeJitter.load(std::memory_order_relaxed) && tr.jitter > 0.0025f) {
        const float jitterPad = std::min((tr.jitter - 0.0025f) * 1.25f, tr.period * 0.45f);
        effPeriod += jitterPad;
        ++tls.batchDeJitterHits;
        const uint32_t jUs = static_cast<uint32_t>(tr.jitter * 1000000.0f);
        uint32_t prevPeak = g_deJitterPeakUs.load(std::memory_order_relaxed);
        while (jUs > prevPeak && !g_deJitterPeakUs.compare_exchange_weak(prevPeak, jUs, std::memory_order_relaxed)) {}
    }
    if (effPeriod < 0.015f) effPeriod = 0.015f;
    if (effPeriod > 0.125f) effPeriod = 0.125f;

    const float dt = static_cast<float>(static_cast<double>(nowQpc - tr.seg_start_qpc) * g_qpcInvFreq);
    float alpha = dt / effPeriod;
    if (!std::isfinite(alpha) || alpha >= 1.0f) {
        tr.rend_x = tr.curr_x;
        tr.rend_y = tr.curr_y;
        tr.rend_z = tr.curr_z;
        tr.settled = 1;
        return false; // Exact raw coordinate reached -> zero drift
    }
    if (alpha < 0.0f) alpha = 0.0f;

    tr.rend_x = tr.prev_x + (tr.curr_x - tr.prev_x) * alpha;
    tr.rend_y = tr.prev_y + (tr.curr_y - tr.prev_y) * alpha;
    tr.rend_z = tr.prev_z + (tr.curr_z - tr.prev_z) * alpha;
    *pX = tr.rend_x;
    *pY = tr.rend_y;
    *pZ = tr.rend_z;
    if (countAsAnim) {
        ++tls.batchPosInterp;
    }
    return true;
}

static void InterpolateAnimCmdWorldPos(uint8_t* cmd, uintptr_t key, ThreadLocalAnimState& tls, int64_t nowQpc) {
    if (!cmd || key == 0) return;
    // Skip screen-space / HUD / background layer commands ([cmd+0x60] == 8 @ 0x1400F49C9)
    const uint32_t orientMode = *reinterpret_cast<const uint32_t*>(cmd + 0x60);
    if (orientMode == 8u) return;

    // 4x4 row-major World Transform Matrix at [cmd + 0x18]:
    // Translation column: X = +0x24 (+0x0C), Y = +0x34 (+0x1C), Z = +0x44 (+0x2C)
    InterpolateWorldPosCore(
        reinterpret_cast<float*>(cmd + 0x24),
        reinterpret_cast<float*>(cmd + 0x34),
        reinterpret_cast<float*>(cmd + 0x44),
        key, tls, nowQpc, true);
}

static const float* HookLightGetWorldMatrix(void* transform) {
    const float* orig = g_origTransformGetWorldMatrix ? g_origTransformGetWorldMatrix(transform) : nullptr;
    if (!orig || !transform) return orig;
    if (g_worldPosInterpEnabled.load(std::memory_order_relaxed) == 0 ||
        g_skeletalTweenEnabled.load(std::memory_order_relaxed) == 0) {
        return orig;
    }
    static thread_local float tl_lightMtx[16];
    memcpy(tl_lightMtx, orig, sizeof(tl_lightMtx));
    ThreadLocalAnimState& tls = GetTlsAnimState();
    const int64_t nowQpc = tls.GetFrameQpc();
    const uintptr_t lightKey = reinterpret_cast<uintptr_t>(transform) | 2ULL;
    if (InterpolateWorldPosCore(&tl_lightMtx[3], &tl_lightMtx[7], &tl_lightMtx[11], lightKey, tls, nowQpc, false)) {
        static thread_local uint32_t tl_lightHits = 0;
        if (++tl_lightHits >= 64u) {
            InterlockedAdd64(&g_lightPos240Count, static_cast<LONG64>(tl_lightHits));
            tl_lightHits = 0;
        }
    }
    return tl_lightMtx;
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
        float elapsed = static_cast<float>(static_cast<double>(nowQpc - tr.tick_qpc) * g_qpcInvFreq);
        // Seamless one-shot self-replay wrap (e.g. Wolfgang `use_dumbbell_loop`, `chop_loop`, `mine_loop`):
        // Klei plays these looping actions with loop=false (playMode==0) and restarts on animover!
        // Only wrap when the exact same anim immediately restarts right after finishing its final frame
        // (elapsed <= 0.120s prevents idle rested one-shot anims from ever falsely wrapping).
        const bool oneShotWrap = (playMode == 0 && oldF >= nf - 2.5f && curF <= 2.0f && elapsed > 0.0f && elapsed <= 0.120f);
        if ((playMode == 1 || oneShotWrap) && df < 0.0f) {
            df += nf;
        }
        bool smoothStep = (df > 0.01f && df <= 4.5f) ||
                          (playMode == 2 && fabsf(df) > 0.01f && fabsf(df) <= 4.5f);
        if (smoothStep) {
            if (elapsed > 0.005f && elapsed < 0.120f) {
                tr.tick_period = tr.tick_period * 0.7f + elapsed * 0.3f;
            }
            // C0-continuous skeletal handoff from last_rendered_f (prevents micro-pops on jitter)
            float handoffF = oldF;
            float dfRend = curF - tr.last_rendered_f;
            const bool rendWrap = (playMode == 1) || (playMode == 0 && tr.last_rendered_f >= nf - 2.5f && curF <= 2.0f);
            if (rendWrap && dfRend < 0.0f) dfRend += nf;
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

    const bool isWrappingSeg = (playMode == 1) || (playMode == 0 && tr.prev_f >= nf - 2.5f && tr.curr_f <= 2.0f);
    float stepF = tr.curr_f - tr.prev_f;
    if (isWrappingSeg && stepF < 0.0f) stepF += nf;
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
    if (isWrappingSeg) {
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

    if (isWrappingSeg) {
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
    if (n0 == 0 || !f0.elements || n1 == 0 || !f1.elements || n0 > kMaxAnimElements || n1 > kMaxAnimElements) return &f0;

    const int64_t workStartQpc = ((tls.frameTweenEnts & 15u) == 0) ? NowQpc() : 0;

    AnimBufferSlot& bufSlot = tls.NextSlot(channel);
    bufSlot.frame = f0;
    bufSlot.frame.elements = bufSlot.elems;
    memcpy(bufSlot.elems, f0.elements, n0 * sizeof(AnimElement));

    const bool fastOnly = (tls.frameShedLevel >= 1 && channel != AnimChannel::UI);

    // L1-resident O(N) stack hash table + exact spatial/2-opt multi-instance particle matcher:
    //   1) Unique (layer, symbol) bones: fast O(1) path; caps jump at 48px ONLY if the layer_hash
    //      itself hosts multiple particles (fixes autumn tree_leaf_fx fade-out cross-tree streaks
    //      while preserving full 420px Boss slams on unique limb layers).
    //   2) Multi-instance duplicate (layer, symbol) buckets (fireflies, pond wave_still reflections,
    //      oasis/lava pools, falling leaves, evergreen tiers):
    //      - Safe exact-(layer,sym,fn) lock ONLY when <=48px, det-sign consistent, AND within
    //        +16px^2 of the closest same-(layer,sym) candidate (fixes 689/2810 DST anim zips
    //        including 48/48 pond reflection frames & firefly 153-234px streaks!).
    //      - Global closest-pair matching (det-sign consistent, zero penalty for fn+1 flipbooks)
    //        + 2-opt minimum-squared-distance uncrossing (eliminates crossing firefly swaps!).
    constexpr uint32_t kHashSize = 128;
    constexpr uint32_t kHashMask = kHashSize - 1;
    constexpr float kDupMaxDistSq = 48.0f * 48.0f;
    constexpr uint32_t kMaxDupBucket = 64;
    int16_t headLS[kHashSize];
    int16_t nextLS[kMaxAnimElements];
    uint8_t f0BucketCnt[kHashSize];
    int16_t matchedF1[kMaxAnimElements];
    uint8_t f0Processed[kMaxAnimElements];
    uint64_t usedF1[kMaxAnimElements / 64] = { 0 }; // 512 bits (matches kMaxAnimElements)

    memset(headLS, -1, sizeof(headLS));
    memset(f0BucketCnt, 0, sizeof(f0BucketCnt));
    memset(matchedF1, -1, n0 * sizeof(int16_t));
    memset(f0Processed, 0, n0 * sizeof(uint8_t));

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

    auto isUsedF1 = [&](uint32_t j) -> bool {
        return j < kMaxAnimElements && ((usedF1[j >> 6] >> (j & 63)) & 1ULL) != 0;
    };
    auto markUsedF1 = [&](uint32_t j) {
        if (j < kMaxAnimElements) usedF1[j >> 6] |= (1ULL << (j & 63));
    };
    auto elemDet = [](const uint16_t m[6]) -> float {
        return HalfToFloat(m[0]) * HalfToFloat(m[3]) - HalfToFloat(m[1]) * HalfToFloat(m[2]);
    };
    // Distinguishes dynamic particle layers (e.g. autumn `tree_leaf_fx` where leaves duplicate or
    // spawn/despawn across frames) from rigid composite character rig layers (e.g. player `arm_carry`
    // holding 6 distinct 1-to-1 limb/weapon symbols: arm_upper, arm_lower, hand, swap_object).
    // Only dynamic particle layers receive the 48px jump cap; rigid character limbs retain full 420px!
    auto isParticleLayer = [&](uint32_t lhash) -> bool {
        uint32_t c0 = 0, c1 = 0;
        for (uint32_t k = 0; k < n0; ++k) {
            if (f0.elements[k].layer_hash == lhash) ++c0;
        }
        for (uint32_t j = 0; j < n1; ++j) {
            if (f1.elements[j].layer_hash == lhash) ++c1;
        }
        if (c0 <= 1 && c1 <= 1) return false;
        if (c0 != c1) return true; // particle spawn/despawn on this layer -> cap 48px
        for (uint32_t k = 0; k < n0; ++k) {
            if (f0.elements[k].layer_hash != lhash) continue;
            const uint32_t sym = f0.elements[k].symbol_hash;
            uint32_t m0 = 0, m1 = 0;
            for (uint32_t a = 0; a < n0; ++a) {
                if (f0.elements[a].layer_hash == lhash && f0.elements[a].symbol_hash == sym) ++m0;
            }
            if (m0 != 1) return true; // duplicate symbol on layer -> particle layer
            for (uint32_t b = 0; b < n1; ++b) {
                if (f1.elements[b].layer_hash == lhash && f1.elements[b].symbol_hash == sym) ++m1;
            }
            if (m1 != 1) return true; // symbol churn (vanished/replaced) -> particle layer
        }
        return false; // strict 1-to-1 symbol bijection -> rigid composite limb layer!
    };

    for (uint32_t i = 0; i < n0; ++i) {
        if (f0Processed[i]) continue;
        const AnimElement& e0 = f0.elements[i];
        const uint32_t lhash = e0.layer_hash, shash = e0.symbol_hash;
        const uint32_t h = ((lhash * 2654435761u) ^ shash) & kHashMask;

        uint32_t cnt0LS = 1;
        if (f0BucketCnt[h] > 1) {
            cnt0LS = 0;
            for (uint32_t k = i; k < n0; ++k) {
                if (f0.elements[k].symbol_hash == shash && f0.elements[k].layer_hash == lhash) ++cnt0LS;
            }
        }
        int32_t firstJ = -1;
        uint32_t cnt1LS = 0;
        for (int16_t j = headLS[h]; j >= 0; j = nextLS[j]) {
            const AnimElement& e1 = f1.elements[j];
            if (e1.symbol_hash == shash && e1.layer_hash == lhash) {
                if (firstJ < 0) firstJ = j;
                ++cnt1LS;
            }
        }
        if (cnt1LS == 0) {
            f0Processed[i] = 1;
            continue;
        }

        if (cnt0LS == 1 && cnt1LS == 1) {
            // Fast O(1) unique (layer, symbol) 1-to-1 bone match
            const AnimElement& e1 = f1.elements[firstJ];
            const float dx = HalfToFloat(e1.m[4]) - HalfToFloat(e0.m[4]);
            const float dy = HalfToFloat(e1.m[5]) - HalfToFloat(e0.m[5]);
            const float d2 = dx * dx + dy * dy;
            if (d2 <= kDupMaxDistSq || !isParticleLayer(lhash)) {
                matchedF1[i] = static_cast<int16_t>(firstJ);
                markUsedF1(static_cast<uint32_t>(firstJ));
            }
            f0Processed[i] = 1;
            continue;
        }

        // Multi-instance duplicate (layer, symbol) bucket: process the entire bucket together!
        uint16_t b0[kMaxDupBucket], b1[kMaxDupBucket];
        uint32_t m0 = 0, m1 = 0;
        for (uint32_t k = i; k < n0; ++k) {
            if (f0.elements[k].symbol_hash == shash && f0.elements[k].layer_hash == lhash) {
                f0Processed[k] = 1;
                if (m0 < kMaxDupBucket) b0[m0++] = static_cast<uint16_t>(k);
            }
        }
        for (int16_t j = headLS[h]; j >= 0; j = nextLS[j]) {
            if (isUsedF1(static_cast<uint32_t>(j))) continue;
            const AnimElement& e1 = f1.elements[j];
            if (e1.symbol_hash == shash && e1.layer_hash == lhash) {
                if (m1 < kMaxDupBucket) b1[m1++] = static_cast<uint16_t>(j);
            }
        }

        if (cnt0LS <= kMaxDupBucket && cnt1LS <= kMaxDupBucket && m0 > 0 && m1 > 0) {
            float x0[kMaxDupBucket], y0[kMaxDupBucket], d0[kMaxDupBucket];
            float x1[kMaxDupBucket], y1[kMaxDupBucket], d1[kMaxDupBucket];
            uint32_t fn0[kMaxDupBucket], fn1[kMaxDupBucket];
            int16_t locMatch[kMaxDupBucket];
            uint8_t locUsed1[kMaxDupBucket];

            for (uint32_t a = 0; a < m0; ++a) {
                const AnimElement& ea = f0.elements[b0[a]];
                x0[a] = HalfToFloat(ea.m[4]); y0[a] = HalfToFloat(ea.m[5]);
                d0[a] = elemDet(ea.m); fn0[a] = ea.frame_num;
                locMatch[a] = -1;
            }
            for (uint32_t b = 0; b < m1; ++b) {
                const AnimElement& eb = f1.elements[b1[b]];
                x1[b] = HalfToFloat(eb.m[4]); y1[b] = HalfToFloat(eb.m[5]);
                d1[b] = elemDet(eb.m); fn1[b] = eb.frame_num;
                locUsed1[b] = 0;
            }

            // Step B1: Safe exact-(layer,sym,fn) lock (e.g. evergreen co-located tiers fn=15..18):
            // ONLY allowed if <=48px, det-sign consistent, AND within +16px^2 of closest same-LS!
            for (uint32_t a = 0; a < m0; ++a) {
                uint32_t c0Ex = 0, c1Ex = 0;
                int32_t exB = -1;
                const uint32_t fa = fn0[a];
                for (uint32_t k = 0; k < m0; ++k) {
                    if (fn0[k] == fa) ++c0Ex;
                }
                if (c0Ex != 1) continue;
                for (uint32_t b = 0; b < m1; ++b) {
                    if (fn1[b] == fa) { exB = static_cast<int32_t>(b); ++c1Ex; }
                }
                if (c1Ex != 1 || locUsed1[exB] || d0[a] * d1[exB] < 0.0f) continue;
                const float dxEx = x1[exB] - x0[a], dyEx = y1[exB] - y0[a];
                const float d2Ex = dxEx * dxEx + dyEx * dyEx;
                if (d2Ex > kDupMaxDistSq) continue;
                float minD2 = 1e30f;
                for (uint32_t b = 0; b < m1; ++b) {
                    if (locUsed1[b] || d0[a] * d1[b] < 0.0f) continue;
                    const float dx = x1[b] - x0[a], dy = y1[b] - y0[a];
                    const float dd = dx * dx + dy * dy;
                    if (dd < minD2) minD2 = dd;
                }
                if (d2Ex <= minD2 + 16.0f) {
                    locMatch[a] = static_cast<int16_t>(exB);
                    locUsed1[exB] = 1;
                }
            }

            // Step B2: Global closest-pair greedy (det-consistent, 0 penalty for fn+1 flipbooks!)
            uint8_t b2List[kMaxDupBucket];
            uint32_t b2Cnt = 0;
            for (;;) {
                float bestSc = 1e30f;
                int32_t bestA = -1, bestB = -1;
                for (uint32_t a = 0; a < m0; ++a) {
                    if (locMatch[a] >= 0) continue;
                    for (uint32_t b = 0; b < m1; ++b) {
                        if (locUsed1[b] || d0[a] * d1[b] < 0.0f) continue;
                        const float dx = x1[b] - x0[a], dy = y1[b] - y0[a];
                        const float dd = dx * dx + dy * dy;
                        if (dd <= kDupMaxDistSq) {
                            const float tie = (fn1[b] == fn0[a] || fn1[b] == fn0[a] + 1u) ? 0.0f : 0.25f;
                            const float sc = dd + tie;
                            if (sc < bestSc) {
                                bestSc = sc;
                                bestA = static_cast<int32_t>(a);
                                bestB = static_cast<int32_t>(b);
                            }
                        }
                    }
                }
                if (bestA < 0) break;
                locMatch[bestA] = static_cast<int16_t>(bestB);
                locUsed1[bestB] = 1;
                b2List[b2Cnt++] = static_cast<uint8_t>(bestA);
            }

            // Step B3: 2-opt minimum-squared-distance uncrossing (fixes crossing firefly/leaf swaps!)
            for (int sweep = 0; sweep < 2 && b2Cnt >= 2; ++sweep) {
                bool improved = false;
                for (uint32_t p = 0; p < b2Cnt; ++p) {
                    const uint32_t a1 = b2List[p];
                    int16_t b1i = locMatch[a1];
                    for (uint32_t q = p + 1; q < b2Cnt; ++q) {
                        const uint32_t a2 = b2List[q];
                        const int16_t b2i = locMatch[a2];
                        if (d0[a1] * d1[b2i] < 0.0f || d0[a2] * d1[b1i] < 0.0f) continue;
                        const float dx11 = x1[b1i] - x0[a1], dy11 = y1[b1i] - y0[a1];
                        const float dx22 = x1[b2i] - x0[a2], dy22 = y1[b2i] - y0[a2];
                        const float dx12 = x1[b2i] - x0[a1], dy12 = y1[b2i] - y0[a1];
                        const float dx21 = x1[b1i] - x0[a2], dy21 = y1[b1i] - y0[a2];
                        const float d12 = dx12 * dx12 + dy12 * dy12;
                        const float d21 = dx21 * dx21 + dy21 * dy21;
                        if (d12 <= kDupMaxDistSq && d21 <= kDupMaxDistSq &&
                            (d12 + d21 + 1e-3f) < (dx11 * dx11 + dy11 * dy11 + dx22 * dx22 + dy22 * dy22)) {
                            locMatch[a1] = b2i;
                            locMatch[a2] = b1i;
                            b1i = b2i;
                            improved = true;
                        }
                    }
                }
                if (!improved) break;
            }

            for (uint32_t a = 0; a < m0; ++a) {
                if (locMatch[a] >= 0) {
                    const uint16_t fj = b1[locMatch[a]];
                    matchedF1[b0[a]] = static_cast<int16_t>(fj);
                    markUsedF1(fj);
                }
            }
        } else {
            // Overflow fallback (>64 identical particles in one bucket): det-consistent bounded NN
            for (uint32_t k = i; k < n0; ++k) {
                const AnimElement& ek = f0.elements[k];
                if (ek.symbol_hash != shash || ek.layer_hash != lhash) continue;
                const float tx0 = HalfToFloat(ek.m[4]), ty0 = HalfToFloat(ek.m[5]);
                const float det0k = elemDet(ek.m);
                int32_t bestJ = -1;
                float bestSc = 1e30f;
                for (int16_t j = headLS[h]; j >= 0; j = nextLS[j]) {
                    if (isUsedF1(static_cast<uint32_t>(j))) continue;
                    const AnimElement& e1 = f1.elements[j];
                    if (e1.symbol_hash != shash || e1.layer_hash != lhash) continue;
                    if (det0k * elemDet(e1.m) < 0.0f) continue;
                    const float dx = HalfToFloat(e1.m[4]) - tx0, dy = HalfToFloat(e1.m[5]) - ty0;
                    const float dd = dx * dx + dy * dy;
                    if (dd <= kDupMaxDistSq) {
                        const float tie = (e1.frame_num == ek.frame_num || e1.frame_num == ek.frame_num + 1u) ? 0.0f : 0.25f;
                        if (dd + tie < bestSc) { bestSc = dd + tie; bestJ = j; }
                    }
                }
                if (bestJ >= 0) {
                    matchedF1[k] = static_cast<int16_t>(bestJ);
                    markUsedF1(static_cast<uint32_t>(bestJ));
                }
            }
        }
    }

    uint32_t tweenedCount = 0;
    uint32_t pass2Count = 0;
    for (uint32_t i = 0; i < n0; ++i) {
        const int16_t mj = matchedF1[i];
        if (mj < 0) continue;
        const AnimElement& e0 = f0.elements[i];
        const AnimElement& e1 = f1.elements[mj];
        const bool isPass2 = (e1.frame_num != e0.frame_num);
        if (isPass2 && fastOnly) continue;
        if (TweenElementMatrix(e0.m, e1.m, frac, bufSlot.elems[i].m, fastOnly)) {
            ++tweenedCount;
            if (isPass2) {
                ++pass2Count;
                if (frac >= 0.5f) {
                    bufSlot.elems[i].frame_num = e1.frame_num;
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
    if (!textStart || preLen == 0 || textSize < total) return nullptr;
    uint8_t* hit = nullptr;
    for (size_t off = 0; off <= textSize - total; ++off) {
        if (textStart[off] == pre[0] &&
            memcmp(textStart + off, pre, preLen) == 0 &&
            memcmp(textStart + off + preLen + 4, post, postLen) == 0) {
            if (hit) return nullptr; // ambiguous (>1 match) -> fail-closed
            hit = textStart + off + preLen - 1; // points to 0xE8
        }
    }
    return hit;
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

static bool IsLuaJitEnvironmentPresent() {
    if (GetModuleHandleW(L"lua51DS.dll") != nullptr ||
        GetModuleHandleW(L"lua51.dll") != nullptr ||
        GetModuleHandleW(L"Injector.dll") != nullptr ||
        GetModuleHandleW(L"plugin_core_vm.dll") != nullptr ||
        GetModuleHandleW(L"frida-gum.dll") != nullptr) {
        return true;
    }
    // DontStarveLuaJIT2 (workshop-3444078585) injects via a local bin64\Winmm.dll proxy that loads
    // before dinput8.dll (prior to Injector.dll / lua51DS.dll). Distinguish local proxy from System32!
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        if (wchar_t* slash = wcsrchr(exePath, L'\\')) {
            *(slash + 1) = L'\0';
            std::wstring winmmProxy = std::wstring(exePath) + L"Winmm.dll";
            HMODULE hWinmm = GetModuleHandleW(L"Winmm.dll");
            if (hWinmm) {
                wchar_t modPath[MAX_PATH] = {};
                if (GetModuleFileNameW(hWinmm, modPath, MAX_PATH) > 0 &&
                    _wcsicmp(modPath, winmmProxy.c_str()) == 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

void* HookClientLuaNewState(LuaAlloc_t f, void* ud) {
    if (g_optMimallocLua.load(std::memory_order_relaxed) &&
        GetModuleHandleW(L"lua51DS.dll") == nullptr) {
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
    if (IsLuaJitEnvironmentPresent()) {
        g_optMimallocLua.store(false, std::memory_order_relaxed);
        LogMsg("[COMPAT] Detected LuaJIT2 / local Winmm.dll proxy -> auto-skipped mimalloc lua_newstate hook for 100%% LuaJIT compatibility.");
        return;
    }
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
                    const int64_t tGpu0 = nowQpc;
                    tls.pGlFinish();
                    nowQpc = NowQpc();
                    UpdateAtomicEma(g_emaGpuDrainUs, static_cast<float>((nowQpc - tGpu0) * g_qpcToUs), 0.06f);
                    remUs = static_cast<double>(tls.nextDeadlineQpc - nowQpc) * g_qpcToUs;
                    InterlockedIncrement64(&g_gpuSyncDrains);
                    if (remUs < 150.0) {
                        tls.gpuSyncCooldown = 60;
                        InterlockedIncrement64(&g_gpuSyncSkips);
                    }
                }
            }
            if (baseStart > 0 && nowQpc >= baseStart) {
                const float busyUs = static_cast<float>((nowQpc - baseStart) * g_qpcToUs);
                UpdateAtomicEma(g_emaFrameBusyUs, busyUs, 0.06f);
                g_lastFrameBusyUs.store(static_cast<uint32_t>(std::clamp(busyUs, 0.0f, 1000000.0f)), std::memory_order_relaxed);
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
        if (fl && fl->startQpc > 0 && nowQpc >= fl->startQpc) {
            const float busyUs = static_cast<float>((nowQpc - fl->startQpc) * g_qpcToUs);
            UpdateAtomicEma(g_emaFrameBusyUs, busyUs, 0.06f);
            g_lastFrameBusyUs.store(static_cast<uint32_t>(std::clamp(busyUs, 0.0f, 1000000.0f)), std::memory_order_relaxed);
        }
        tls.nextDeadlineQpc = 0;
    }

    // Publish exact frame start & sequence for RenderThread/GC/TexStream + 1% Low ring & Hitch breakdown
    g_frameStartQpc.store(nowQpc, std::memory_order_release);
    g_frameSeq.fetch_add(1, std::memory_order_release);
    const uint32_t frameUnzUs    = g_frameUnzUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameShaderUs = g_frameShaderUs.exchange(0, std::memory_order_acq_rel);
    if (tls.lastEndQpc != 0) {
        double frameUs = static_cast<double>(nowQpc - tls.lastEndQpc) * g_qpcToUs;
        if (frameUs >= 100.0 && frameUs <= 60000000.0) {
            if (frameUs <= 1000000.0) {
                g_lastFrameUs.store(static_cast<int64_t>(frameUs), std::memory_order_relaxed);
                g_frameRing.Push(static_cast<float>(frameUs));
            }

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
            } else if (frameUs > 25000.0) {
                // Tear-free Hitch & Severe-Stall (>=500ms) capture (0 MainThread I/O; WorkerProc logs max 1x/10s)
                const uint32_t fUs = static_cast<uint32_t>(std::min(frameUs, 60000000.0));
                const bool isSevere = (frameUs >= 500000.0);
                AcquireSRWLockExclusive(&g_hitchLock);
                if (isSevere) {
                    ++g_hitchSnap.severeCount;
                    if (fUs > g_hitchSnap.severeMaxUs) g_hitchSnap.severeMaxUs = fUs;
                } else {
                    ++g_hitchSnap.pendingCount;
                }
                if (fUs >= g_hitchSnap.peakUs) {
                    g_hitchSnap.peakUs    = fUs;
                    g_hitchSnap.busyUs    = g_lastFrameBusyUs.load(std::memory_order_relaxed);
                    g_hitchSnap.swapUs    = g_lastSwapUs.load(std::memory_order_relaxed);
                    g_hitchSnap.worldUs   = g_lastWorldRenderUs.load(std::memory_order_relaxed);
                    g_hitchSnap.postUs    = g_lastPostProcUs.load(std::memory_order_relaxed);
                    g_hitchSnap.unzUs     = frameUnzUs;
                    g_hitchSnap.shaderUs  = frameShaderUs;
                    g_hitchSnap.animUs    = g_lastAnimHookUs.load(std::memory_order_relaxed);
                    g_hitchSnap.animEnts  = g_lastAnimEnts.load(std::memory_order_relaxed);
                    g_hitchSnap.animBones = g_lastAnimBones.load(std::memory_order_relaxed);
                    g_hitchSnap.animShed  = g_lastAnimShed.load(std::memory_order_relaxed);
                    g_hitchSnap.gcUs      = g_lastGcUs.load(std::memory_order_relaxed);
                    g_hitchSnap.gcKb      = g_lastGcKb.load(std::memory_order_relaxed);
                    g_hitchSnap.texUs     = g_lastTexUploadUs.load(std::memory_order_relaxed);
                }
                ReleaseSRWLockExclusive(&g_hitchLock);
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
    if (!VirtualProtect(ptr, sizeof(float), oldProt, &dummy)) {
        LogMsg("[WARN] PatchClientFloatProtected @ %p failed restoring prot 0x%lX",
               static_cast<void*>(ptr), static_cast<unsigned long>(oldProt));
    }
    return true;
}

static bool PatchClientDoubleProtected(double* ptr, double val) {
    if (!ptr) return false;
    DWORD oldProt = 0, dummy = 0;
    if (!VirtualProtect(ptr, sizeof(double), PAGE_READWRITE, &oldProt)) return false;
    *ptr = val;
    if (!VirtualProtect(ptr, sizeof(double), oldProt, &dummy)) {
        LogMsg("[WARN] PatchClientDoubleProtected @ %p failed restoring prot 0x%lX",
               static_cast<void*>(ptr), static_cast<unsigned long>(oldProt));
    }
    return true;
}

bool SetClientSimTickRate(int hz) {
    if (hz != 30 && hz != 60) return false;

    AcquireSRWLockExclusive(&g_clientSimTickLock);
    struct UnlockGuard {
        SRWLOCK* lk;
        ~UnlockGuard() { ReleaseSRWLockExclusive(lk); }
    } guard{&g_clientSimTickLock};

    if (!g_clientSimTickFloatPtr || !g_clientSimTickDoublePtr) return false;
    // Fail-closed: activating 60Hz requires ALL 4 subsystem constants + netTickScale verified!
    if (hz == 60 &&
        (!g_clientAnimShaderTickPtr || !g_clientPathfinderTickPtr ||
         !g_clientSpatialTickPtr || !g_clientSimTimeDiffPtr || !g_clientNetTickScalePtr)) {
        LogMsg("[SIMTICK] Refused Client 60Hz (incomplete subsystem pointers: anim=%d path=%d spat=%d diff=%d netScale=%d)",
               g_clientAnimShaderTickPtr != nullptr, g_clientPathfinderTickPtr != nullptr,
               g_clientSpatialTickPtr != nullptr, g_clientSimTimeDiffPtr != nullptr,
               g_clientNetTickScalePtr != nullptr);
        return false;
    }

    const int prevHz = g_clientSimTickRate.load(std::memory_order_acquire);
    if (prevHz == hz) return true;

    const float newF = 1.0f / static_cast<float>(hz);
    const double newD = 1.0 / static_cast<double>(hz);
    const float newNetScale = (hz == 60) ? 60.499998f : 30.499998f;

    // Transactional snapshot of every mutable constant before any write
    struct FloatPatchEntry {
        float* ptr;
        float  prevVal;
        float  newVal;
        bool   written;
    };
    FloatPatchEntry fEntries[6] = {
        { g_clientSimTickFloatPtr,   g_clientSimTickFloatPtr   ? *g_clientSimTickFloatPtr   : 0.0f, newF,        false },
        { g_clientAnimShaderTickPtr, g_clientAnimShaderTickPtr ? *g_clientAnimShaderTickPtr : 0.0f, newF,        false },
        { g_clientPathfinderTickPtr, g_clientPathfinderTickPtr ? *g_clientPathfinderTickPtr : 0.0f, newF,        false },
        { g_clientSpatialTickPtr,    g_clientSpatialTickPtr    ? *g_clientSpatialTickPtr    : 0.0f, newF,        false },
        { g_clientSimTimeDiffPtr,    g_clientSimTimeDiffPtr    ? *g_clientSimTimeDiffPtr    : 0.0f, newF,        false },
        { g_clientNetTickScalePtr,   g_clientNetTickScalePtr   ? *g_clientNetTickScalePtr   : 0.0f, newNetScale, false },
    };
    const double savedD = *g_clientSimTickDoublePtr;
    bool dWritten = false;

    auto rollbackAll = [&]() {
        if (dWritten) {
            PatchClientDoubleProtected(g_clientSimTickDoublePtr, savedD);
        }
        for (int i = 5; i >= 0; --i) {
            if (fEntries[i].written && fEntries[i].ptr) {
                PatchClientFloatProtected(fEntries[i].ptr, fEntries[i].prevVal);
            }
        }
    };

    // 1. Primary double + float
    if (!PatchClientDoubleProtected(g_clientSimTickDoublePtr, newD)) {
        rollbackAll();
        return false;
    }
    dWritten = true;

    int extras = 0;
    for (int i = 0; i < 6; ++i) {
        if (!fEntries[i].ptr) continue;
        if (!PatchClientFloatProtected(fEntries[i].ptr, fEntries[i].newVal)) {
            rollbackAll();
            LogMsg("[SIMTICK] Client %dHz patch failed at slot %d -> rolled back atomically to %dHz", hz, i, prevHz);
            return false;
        }
        fEntries[i].written = true;
        if (i >= 1 && i <= 4) ++extras;
    }
    const bool netScaleOk = fEntries[5].written;

    g_clientSimTickRate.store(hz, std::memory_order_release);
    g_clientSimTickGen.fetch_add(1, std::memory_order_release);

    // Re-sync NetworkManager (and restore pre-60Hz rate + vanilla download throttle on 60->30!)
    char netSyncBuf[96] = "none";
    if (g_clientNetMgrGlobalPtr && *g_clientNetMgrGlobalPtr) {
        auto* netMgr = static_cast<uint8_t*>(*g_clientNetMgrGlobalPtr);
        void* replicaMgr = *reinterpret_cast<void**>(netMgr + 0x288);
        const uint8_t isClient = netMgr[0x308];
        const uint32_t curRate = *reinterpret_cast<uint32_t*>(netMgr + 0xD8);
        if (hz == 60 && replicaMgr) {
            if (!g_clientHasSavedPre60NetRate) {
                g_clientSavedPre60NetRate = (curRate != 0) ? curRate : 15u;
                g_clientHasSavedPre60NetRate = true;
            }
            if (isClient == 0) {
                // No-Caves Local Host running inside Client binary: upgrade default 15Hz -> 60Hz (U=60(1))
                const uint32_t hostRate = (curRate == 0 || curRate == 15) ? 60u : curRate;
                if (g_fnClientSetNetTickRate) {
                    g_fnClientSetNetTickRate(netMgr, hostRate);
                    snprintf(netSyncBuf, sizeof(netSyncBuf), "local-host U=%u (saved=%u)", hostRate, g_clientSavedPre60NetRate);
                }
            } else {
                // Dedicated-Server Client: recalc upload ticksPerSnapshot=6 (10Hz) AND set D=0 (unthrottled receive!)
                if (g_fnClientSetNetTickRate) {
                    g_fnClientSetNetTickRate(netMgr, curRate ? curRate : 15u);
                }
                if (g_fnClientSetDownloadMs) {
                    g_fnClientSetDownloadMs(replicaMgr, 0);
                    snprintf(netSyncBuf, sizeof(netSyncBuf), "client U=10(6) D=0 (saved=%u)", g_clientSavedPre60NetRate);
                } else {
                    snprintf(netSyncBuf, sizeof(netSyncBuf), "client U=10(6) (saved=%u)", g_clientSavedPre60NetRate);
                }
            }
        } else if (hz == 30) {
            const uint32_t restoreRate = g_clientHasSavedPre60NetRate ? g_clientSavedPre60NetRate : (curRate ? curRate : 15u);
            g_clientHasSavedPre60NetRate = false;
            g_clientSavedPre60NetRate = 0;
            if (replicaMgr && g_fnClientSetNetTickRate) {
                // SetNetworkTickRate with restored 30.5f recalculates ALL 4 fields:
                // m_netTickRate, uploadMs, ticksPerSnapshot, AND downloadMs (100% vanilla)!
                g_fnClientSetNetTickRate(netMgr, restoreRate);
                snprintf(netSyncBuf, sizeof(netSyncBuf), "restored 30Hz vanilla (rate=%u)", restoreRate);
            } else {
                snprintf(netSyncBuf, sizeof(netSyncBuf), "restored 30Hz vanilla (no replicaMgr)");
            }
        }
    }

    LogMsg("[SIMTICK] Client Simulation tick rate -> %d Hz (dt=%.6fs, subsys=%d/4, netScale=%s, net=%s)",
           hz, newD, extras, netScaleOk ? "OK" : "SKIP", netSyncBuf);
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

//------------------------------------------------------------------------------
// [v2.6.0] Pillar 1: True 240Hz Render-Only VFX & Particle Interpolation
//   - Vanilla DST steps BOTH modern VFXEffect (rain/snow/pollen/fire/sparks/boss
//     magic) and legacy ParticleEmitter strictly inside 30Hz Simulation::Update.
//   - Intercepts ParticleManager::Update to measure exact simulation tick phase
//     (auto-adapts to 30Hz vanilla AND 60Hz HyperCore), then smoothly interpolates
//     positions, rotation angles, and envelope ages IN-PLACE inside Klei's fresh
//     per-frame ParticleRenderCmd / EmitterRenderCmd snapshots:
//       pos(alpha) = pos_curr - (1 - alpha) * vel_curr == lerp(pos_prev, pos_curr, alpha)
//   - Zero mutation of simulation ParticleBuffer, zero allocation, zero overshoot,
//     fresh-spawn & ground-collision guarded, auto-freezes cleanly on pause!
//------------------------------------------------------------------------------
static void HookParticleMgrUpdate(void* mgr, float dt) {
    const int64_t nowQpc = NowQpc();
    const int64_t prevQpc = g_vfxLastTickQpc.exchange(nowQpc, std::memory_order_acq_rel);
    if (dt > 0.004f && dt < 0.10f) {
        g_vfxLastSimDt.store(dt, std::memory_order_relaxed);
    }
    if (prevQpc > 0 && nowQpc > prevQpc && g_qpcInvFreq > 0.0) {
        const double elapsed = static_cast<double>(nowQpc - prevQpc) * g_qpcInvFreq;
        if (elapsed >= 0.010 && elapsed <= 0.080) {
            const float prevPer = g_vfxTickPeriodSec.load(std::memory_order_relaxed);
            const float nextPer = (prevPer > 0.008f && prevPer < 0.080f)
                ? (prevPer * 0.85f + static_cast<float>(elapsed) * 0.15f)
                : static_cast<float>(elapsed);
            g_vfxTickPeriodSec.store(std::clamp(nextPer, 0.012f, 0.060f), std::memory_order_relaxed);
        } else if (dt > 0.010f && dt < 0.060f) {
            g_vfxTickPeriodSec.store(dt, std::memory_order_relaxed);
        }
    }
    if (g_origParticleMgrUpdate) {
        g_origParticleMgrUpdate(mgr, dt);
    }
}

static inline void SmoothParticleCmdInPlace(
    uint32_t count, const uint8_t* pool,
    float* cmdPos, float* cmdAges, float* cmdAngles,
    bool hasGroundCollision) {
    if (!pool || !cmdPos || count == 0 || count > 65535u) return;
    if (g_vfxSmoothEnabled.load(std::memory_order_relaxed) == 0) return;

    const uint16_t poolCount = *reinterpret_cast<const uint16_t*>(pool + 0x0A);
    if (poolCount != count) return;
    const float* poolVel = *reinterpret_cast<const float* const*>(pool + 0x18);
    if (!poolVel) return;

    const int64_t lastTickQpc = g_vfxLastTickQpc.load(std::memory_order_acquire);
    if (lastTickQpc <= 0 || g_qpcInvFreq <= 0.0) return;

    const int64_t nowQpc = NowQpc();
    if (nowQpc < lastTickQpc) return;
    const float period = g_vfxTickPeriodSec.load(std::memory_order_relaxed);
    if (!(period > 0.008f)) return;

    const float elapsed = static_cast<float>(static_cast<double>(nowQpc - lastTickQpc) * g_qpcInvFreq);
    if (elapsed >= period) return; // alpha >= 1.0 (or paused) -> back == 0, exact current pose
    const float alpha = std::clamp(elapsed / period, 0.0f, 1.0f);
    const float back = 1.0f - alpha;
    if (back <= 0.002f) return;

    const float* poolAngVel = cmdAngles ? *reinterpret_cast<const float* const*>(pool + 0x30) : nullptr;
    const float simDtBack = back * g_vfxLastSimDt.load(std::memory_order_relaxed);

    uint32_t smoothed = 0;
    for (uint32_t i = 0; i < count; ++i) {
        // Freshly spawned particles (age == 0 before first ParticleBuffer::Update) must NOT step backward
        if (cmdAges) {
            const float age = cmdAges[i];
            if (!(age > 0.0005f)) continue;
            const float interpAge = age - simDtBack;
            cmdAges[i] = (interpAge > 0.0f) ? interpAge : 0.0f;
        }
        const size_t base = static_cast<size_t>(i) * 3u;
        const float vx = poolVel[base + 0];
        const float vy = poolVel[base + 1];
        const float vz = poolVel[base + 2];
        if (!std::isfinite(vx) || !std::isfinite(vy) || !std::isfinite(vz)) continue;
        if (fabsf(vx) > 20.0f || fabsf(vy) > 20.0f || fabsf(vz) > 20.0f) continue;

        // Match Klei ParticleBuffer::Update ground-collision halt (pos.y <= 0 skips pos += vel)
        const float py = cmdPos[base + 1];
        if (hasGroundCollision && py <= 0.0001f) continue;

        cmdPos[base + 0] -= back * vx;
        float ny = py - back * vy;
        if (hasGroundCollision && ny < 0.0f) ny = 0.0f;
        cmdPos[base + 1] = ny;
        cmdPos[base + 2] -= back * vz;

        if (cmdAngles && poolAngVel) {
            const float av = poolAngVel[i];
            if (std::isfinite(av) && fabsf(av) <= 6.283185f) {
                cmdAngles[i] -= back * av;
            }
        }
        ++smoothed;
    }

    if (smoothed > 0) {
        static thread_local uint32_t tl_cmds = 0;
        static thread_local uint64_t tl_parts = 0;
        ++tl_cmds;
        tl_parts += smoothed;
        if (tl_cmds >= 64u) {
            InterlockedAdd64(&g_vfxCmdsSmoothed, static_cast<LONG64>(tl_cmds));
            InterlockedAdd64(&g_vfxParticlesSmoothed, static_cast<LONG64>(tl_parts));
            tl_cmds = 0;
            tl_parts = 0;
        }
    }
}

static void* HookVfxRenderCmdCtor(void* cmd, void* renderNode, void* mtx) {
    void* ret = g_origVfxRenderCmdCtor ? g_origVfxRenderCmdCtor(cmd, renderNode, mtx) : cmd;
    if (ret && g_vfxSmoothHooked.load(std::memory_order_acquire)) {
        auto* c = static_cast<uint8_t*>(ret);
        const auto* vfx = *reinterpret_cast<const uint8_t* const*>(c + 0x18);
        if (vfx) {
            const auto* pool = *reinterpret_cast<const uint8_t* const*>(vfx + 0x98);
            const bool groundCol = (vfx[0x3B] != 0);
            const uint32_t count = *reinterpret_cast<const uint32_t*>(c + 0x60);
            float* ages   = *reinterpret_cast<float**>(c + 0x70);
            float* pos    = *reinterpret_cast<float**>(c + 0x78);
            float* angles = *reinterpret_cast<float**>(c + 0x88);
            SmoothParticleCmdInPlace(count, pool, pos, ages, angles, groundCol);
        }
    }
    return ret;
}

static void* HookEmitterRenderCmdCtor(void* cmd, void* renderNode, void* mtx) {
    void* ret = g_origEmitterRenderCmdCtor ? g_origEmitterRenderCmdCtor(cmd, renderNode, mtx) : cmd;
    if (ret && g_vfxSmoothHooked.load(std::memory_order_acquire)) {
        auto* c = static_cast<uint8_t*>(ret);
        const auto* emitter = *reinterpret_cast<const uint8_t* const*>(c + 0x60);
        if (emitter) {
            const auto* pool = *reinterpret_cast<const uint8_t* const*>(emitter + 0xA0);
            const uint32_t count = *reinterpret_cast<const uint32_t*>(c + 0x6C);
            float* ages   = *reinterpret_cast<float**>(c + 0x78);
            float* pos    = *reinterpret_cast<float**>(c + 0x80);
            float* angles = *reinterpret_cast<float**>(c + 0x90);
            SmoothParticleCmdInPlace(count, pool, pos, ages, angles, false);
        }
    }
    return ret;
}

static void PrepareVfxParticleSmoothHooks(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    // 1. Unique callsite of ParticleManager::Update(dt) inside Simulation::Update (0x140108ede)
    constexpr uint8_t kSigPmCall[] = {
        0x48, 0x8B, 0x8B, 0xB8, 0x00, 0x00, 0x00,
        0xF3, 0x0F, 0x10, 0x0D, 0, 0, 0, 0,
        0x48, 0x8B, 0x49, 0x50,
        0xE8, 0, 0, 0, 0,
        0xB2, 0x01, 0x48, 0x8B, 0xCB, 0xE8
    };
    constexpr char kMaskPmCall[] = "xxxxxxxxxxx????xxxxx????xxxxxx";

    // 2. Unique callsite of VFXRenderCmd::VFXRenderCmd inside ParticleRenderNode::GenerateRenderCommand (0x1400e4fbf)
    constexpr uint8_t kSigVfxCmdCall[] = {
        0x48, 0x8B, 0x81, 0xD0, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xF8, 0x48, 0x8B, 0xEA,
        0x4C, 0x8B, 0x88, 0x98, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF1,
        0x66, 0x41, 0x83, 0x79, 0x0A, 0x00, 0x0F, 0x84, 0, 0, 0, 0,
        0xB9, 0x98, 0x00, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x38, 0xE8, 0, 0, 0, 0,
        0x33, 0xDB, 0x48, 0x85, 0xC0, 0x74, 0x13, 0x4C, 0x8B, 0xC5, 0x48, 0x8B, 0xD6, 0x48, 0x8B, 0xC8,
        0xE8, 0, 0, 0, 0, 0x4C, 0x8B, 0xD8
    };
    constexpr char kMaskVfxCmdCall[] =
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxxxxx????xxxxxxxxxxxxxxxxx????xxx";

    // 3. Unique callsite of EmitterRenderCmd::EmitterRenderCmd (0x1400b465f)
    constexpr uint8_t kSigEmitCmdCall[] = {
        0x48, 0x8B, 0x81, 0xD8, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xF8, 0x48, 0x8B, 0xEA,
        0x4C, 0x8B, 0x88, 0xA0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xF1,
        0x66, 0x41, 0x83, 0x79, 0x0A, 0x00, 0x0F, 0x84, 0, 0, 0, 0,
        0xB9, 0x98, 0x00, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x38, 0xE8, 0, 0, 0, 0,
        0x33, 0xDB, 0x48, 0x85, 0xC0, 0x74, 0x13, 0x4C, 0x8B, 0xC5, 0x48, 0x8B, 0xD6, 0x48, 0x8B, 0xC8,
        0xE8, 0, 0, 0, 0, 0x4C, 0x8B, 0xD8
    };

    const uintptr_t textLo = reinterpret_cast<uintptr_t>(textStart);
    const uintptr_t textHi = textLo + textSize;

    uint8_t* pPmSite = FindUniqueMaskedPatternInText(textStart, textSize, kSigPmCall, kMaskPmCall, sizeof(kSigPmCall));
    uint8_t* pVfxSite = FindUniqueMaskedPatternInText(textStart, textSize, kSigVfxCmdCall, kMaskVfxCmdCall, sizeof(kSigVfxCmdCall));
    uint8_t* pEmitSite = FindUniqueMaskedPatternInText(textStart, textSize, kSigEmitCmdCall, kMaskVfxCmdCall, sizeof(kSigEmitCmdCall));
    if (!pPmSite || !pVfxSite) {
        LogMsg("[VFX240] Skipped 240Hz Particle Smoothing (Fail-Closed AOB miss: pm=%p vfx=%p emit=%p)",
               static_cast<void*>(pPmSite), static_cast<void*>(pVfxSite), static_cast<void*>(pEmitSite));
        return;
    }

    uint8_t* pmCs = pPmSite + 19;
    uint8_t* pmFn = ResolveRel32Call(pmCs);
    uint8_t* vfxCs = pVfxSite + 66;
    uint8_t* vfxFn = ResolveRel32Call(vfxCs);
    if (!pmFn || !vfxFn ||
        reinterpret_cast<uintptr_t>(pmFn) < textLo || reinterpret_cast<uintptr_t>(pmFn) >= textHi ||
        reinterpret_cast<uintptr_t>(vfxFn) < textLo || reinterpret_cast<uintptr_t>(vfxFn) + 0x180 > textHi) {
        LogMsg("[VFX240] Skipped 240Hz Particle Smoothing (target bounds check failed)");
        return;
    }

    // Strict fail-closed struct layout check inside VFXRenderCmd ctor (+0x153: pool +0x98, count +0x0A->+0x60, +0x38/+0x40/+0x10/+0x20/+0x28)
    constexpr uint8_t kExpectedVfxBody[] = {
        0x48, 0x8B, 0xB8, 0x98, 0x00, 0x00, 0x00, 0x0F, 0xB7, 0x4F, 0x0A, 0x89, 0x4B, 0x60,
        0x48, 0x8B, 0x82, 0xD0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x77, 0x38, 0x48, 0x8B, 0x6F, 0x40,
        0x4C, 0x8B, 0x6F, 0x10, 0x4C, 0x8B, 0x67, 0x20, 0x4C, 0x8B, 0x77, 0x28
    };
    if (memcmp(vfxFn + 0x153, kExpectedVfxBody, sizeof(kExpectedVfxBody)) != 0) {
        LogMsg("[VFX240] Skipped 240Hz Particle Smoothing (VFXRenderCmd layout mismatch @ %p)", static_cast<void*>(vfxFn));
        return;
    }

    g_origParticleMgrUpdate = reinterpret_cast<ParticleMgrUpdate_t>(pmFn);
    g_origVfxRenderCmdCtor  = reinterpret_cast<ParticleRenderCmdCtor_t>(vfxFn);

    char msgPm[224], msgVfx[224];
    snprintf(msgPm, sizeof(msgPm), "[VFX240] Hooked ParticleManager::Update @ %p (fn=%p) -> exact SimTick phase clock",
             static_cast<void*>(pmCs), static_cast<void*>(pmFn));
    snprintf(msgVfx, sizeof(msgVfx), "[VFX240] Hooked VFXRenderCmd @ %p (fn=%p) -> True 240Hz Render-Only Particle Interp",
             static_cast<void*>(vfxCs), static_cast<void*>(vfxFn));
    const size_t pmIdx = patches.size();
    const bool okPm  = QueueRel32NearRelayHook(patches, pmCs, reinterpret_cast<const void*>(&HookParticleMgrUpdate), msgPm);
    const size_t vfxIdx = patches.size();
    const bool okVfx = okPm && QueueRel32NearRelayHook(patches, vfxCs, reinterpret_cast<const void*>(&HookVfxRenderCmdCtor), msgVfx);

    if (okPm && okVfx) {
        g_vfxPmPatchIdx  = pmIdx;
        g_vfxCmdPatchIdx = vfxIdx;
    } else {
        // Never leave orphaned clock patch if VFX render hook failed to queue
        while (patches.size() > pmIdx) {
            if (patches.back().ownedExecPage) VirtualFree(patches.back().ownedExecPage, 0, MEM_RELEASE);
            patches.pop_back();
        }
        return;
    }

    bool okEmit = false;
    if (pEmitSite) {
        uint8_t* emitCs = pEmitSite + 66;
        uint8_t* emitFn = ResolveRel32Call(emitCs);
        constexpr uint8_t kExpectedEmitBody[] = {
            0x48, 0x8B, 0x88, 0xA0, 0x00, 0x00, 0x00, 0x0F, 0xB7, 0x41, 0x0A, 0x89, 0x43, 0x6C,
            0x4C, 0x8B, 0x69, 0x38, 0x48, 0x8B, 0x79, 0x40, 0x4C, 0x8B, 0x61, 0x10, 0x48, 0x8B, 0x69, 0x20,
            0x48, 0x8B, 0x71, 0x28
        };
        if (emitFn &&
            reinterpret_cast<uintptr_t>(emitFn) >= textLo &&
            reinterpret_cast<uintptr_t>(emitFn) + 0x120 <= textHi &&
            memcmp(emitFn + 0xEB, kExpectedEmitBody, sizeof(kExpectedEmitBody)) == 0) {
            g_origEmitterRenderCmdCtor = reinterpret_cast<ParticleRenderCmdCtor_t>(emitFn);
            char msgEm[224];
            snprintf(msgEm, sizeof(msgEm), "[VFX240] Hooked Legacy EmitterRenderCmd @ %p (fn=%p) -> True 240Hz Particle Interp",
                     static_cast<void*>(emitCs), static_cast<void*>(emitFn));
            const size_t emIdx = patches.size();
            okEmit = QueueRel32NearRelayHook(patches, emitCs, reinterpret_cast<const void*>(&HookEmitterRenderCmdCtor), msgEm);
            if (okEmit) g_vfxEmitPatchIdx = emIdx;
        }
    }
    if (!okEmit) {
        LogMsg("[VFX240] Modern VFXEffect 240Hz queued (legacy ParticleEmitter skipped)");
    }
}

//------------------------------------------------------------------------------
// Visual Fidelity Tier-1 Native Engine (v2.5.0-alpha):
//   V1. Unlock ANGLE 1.0.0.2249 hardware Anisotropic Filtering (0x8E3 cap bit)
//       + PE IAT hook on libGLESv2!glTexParameteri -> true 16x AF + trilinear
//       exclusively on mipmapped GL_TEXTURE_2D world/terrain/entity atlases
//       (0% impact on UI/LUT/FBO textures; 0 GL errors on older GPUs).
//   V2. Full-Resolution LightMap (0.5x -> 1.0x native screen resolution) via
//       single-instruction RIP-relative float slot (never mutates shared 0.5f!).
//------------------------------------------------------------------------------
static uint8_t* AllocNearReadWriteData(uint8_t* nearAddr, size_t size) {
    uintptr_t base = reinterpret_cast<uintptr_t>(nearAddr) & ~static_cast<uintptr_t>(0xFFFF);
    constexpr uintptr_t kStep = 0x10000;
    constexpr uintptr_t kMaxDist = 0x60000000;
    for (uintptr_t delta = kStep; delta < kMaxDist; delta += kStep) {
        if (base > delta) {
            void* p = VirtualAlloc(reinterpret_cast<void*>(base - delta), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (p) return static_cast<uint8_t*>(p);
        }
        void* p = VirtualAlloc(reinterpret_cast<void*>(base + delta), size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (p) return static_cast<uint8_t*>(p);
    }
    return nullptr;
}

static bool GetModuleTextSection(HMODULE mod, uint8_t*& outStart, size_t& outSize) {
    if (!mod) return false;
    auto* base = reinterpret_cast<uint8_t*>(mod);
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

static void** FindExeIatSlot(const char* dllName, uint16_t ordinal, const char* funcName) {
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) return nullptr;
    auto* base = reinterpret_cast<uint8_t*>(exe);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
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
                if (strcmp(reinterpret_cast<const char*>(ibn->Name), funcName) == 0) {
                    return reinterpret_cast<void**>(&iatThunk->u1.Function);
                }
            }
        }
    }
    return nullptr;
}

static void ProbeAnisoCapsOnGlThread() {
    if (g_afCapsProbeState.load(std::memory_order_acquire) != 0) return;
    if (!g_fnGlGetString || !g_fnGlGetFloatv || !g_fnGlTexParameterf) {
        g_afCapsProbeState.store(-1, std::memory_order_release);
        return;
    }
    const char* exts = g_fnGlGetString(0x1F03u); // GL_EXTENSIONS
    if (!exts || !strstr(exts, "GL_EXT_texture_filter_anisotropic")) {
        g_afCapsProbeState.store(-1, std::memory_order_release);
        return;
    }
    float maxAf = 1.0f;
    g_fnGlGetFloatv(0x84FFu, &maxAf); // GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT
    if (!(maxAf > 1.0f) || maxAf > 64.0f) {
        g_afCapsProbeState.store(-1, std::memory_order_release);
        return;
    }
    g_hwMaxAniso.store(maxAf, std::memory_order_release);
    g_afCapsProbeState.store(1, std::memory_order_release);
    LogMsg("[VISUAL] GL_EXT_texture_filter_anisotropic active on GPU (hwMax=%.0fx)", static_cast<double>(maxAf));
}

void WINAPI HookGlTexParameteri(uint32_t target, uint32_t pname, int32_t param) {
    // Strictly target mipmapped GL_TEXTURE_2D (terrain/entity/tree atlases);
    // leaves ColourCube LUTs (POINT), UI sprites (no-mip LINEAR) and FBOs untouched!
    constexpr uint32_t kGlTexture2D = 0x0DE1u;
    constexpr uint32_t kGlTextureMinFilter = 0x2801u;
    constexpr int32_t  kGlLinearMipmapNearest = 0x2701;
    constexpr int32_t  kGlLinearMipmapLinear  = 0x2703;
    constexpr uint32_t kGlTextureMaxAnisotropyExt = 0x84FEu;

    int32_t effectiveParam = param;
    const bool isMipLinear = (target == kGlTexture2D) && (pname == kGlTextureMinFilter) &&
                             (param == kGlLinearMipmapNearest || param == kGlLinearMipmapLinear);
    const int desiredAf = g_visualAnisoLevel.load(std::memory_order_relaxed);
    if (isMipLinear && desiredAf > 1 && param == kGlLinearMipmapNearest) {
        effectiveParam = kGlLinearMipmapLinear;
        InterlockedIncrement64(&g_afTrilinearUpgrades);
    }

    if (g_origGlTexParameteri) {
        g_origGlTexParameteri(target, pname, effectiveParam);
    }

    if (isMipLinear && g_fnGlTexParameterf) {
        if (g_afCapsProbeState.load(std::memory_order_acquire) == 0) {
            ProbeAnisoCapsOnGlThread();
        }
        if (g_afCapsProbeState.load(std::memory_order_acquire) == 1) {
            const float hwMax = g_hwMaxAniso.load(std::memory_order_relaxed);
            const float level = (desiredAf > 1) ? std::min(static_cast<float>(desiredAf), hwMax) : 1.0f;
            g_fnGlTexParameterf(kGlTexture2D, kGlTextureMaxAnisotropyExt, level);
            if (level > 1.0f) {
                InterlockedIncrement64(&g_afAppliedHits);
            }
        }
    }
}

//------------------------------------------------------------------------------
// [v2.6.0] Pillar 2 + Pillar 3 Unified ANGLE D3D9Ex Hook (libGLESv2!applyTextures):
//   1. Hardware Reflex: IDirect3DDevice9Ex::SetMaximumFrameLatency(1) (cuts up to
//      2 queued GPU frames without stalling CPU/GPU parallelism; restores original
//      latency cleanly when disabled). Zero D3D9 vtable hooks!
//   2. Native Mipmap LOD Bias (D3DSAMP_MIPMAPLODBIAS): strictly applied ONLY to
//      mipmapped world textures (mipFilter != 0), 0.0f on UI/LUTs/FBOs, 0 extra
//      D3D9 API calls in Vanilla Pure default, 0ms instant restore to 0.0f!
//------------------------------------------------------------------------------
extern "C" void OnAngleApplySampler(void* d3d9DevRaw, uint32_t sampler, uint32_t mipAndAfPacked, float maxAniso) {
    if (!d3d9DevRaw) return;
    auto* dev = static_cast<IDirect3DDevice9*>(d3d9DevRaw);
    const uint32_t mipFilter = mipAndAfPacked & 0xFFFFu;
    const bool afSupported = ((mipAndAfPacked >> 16) != 0);

    // 1. Hardware D3D9Ex SetMaximumFrameLatency(1) — runs ONLY on device init or mode toggle (~1ns fast path)
    static void* s_lastDev = nullptr;
    static uint32_t s_lastLatGen = 0;
    static uint32_t s_lastBiasBits[16] = {};
    const uint32_t curLatGen = g_d3d9ExLatGen.load(std::memory_order_acquire);
    if (d3d9DevRaw != s_lastDev || curLatGen != s_lastLatGen) {
        if (d3d9DevRaw != s_lastDev) {
            for (int i = 0; i < 16; ++i) s_lastBiasBits[i] = 0xFFFFFFFFu;
        }
        s_lastDev = d3d9DevRaw;
        s_lastLatGen = curLatGen;

        static constexpr GUID kIidD3d9DevEx = {
            0xb18b10ce, 0x2649, 0x405a, { 0x87, 0x0f, 0x95, 0xf7, 0x77, 0xd4, 0x31, 0x3a }
        };
        void* pExRaw = nullptr;
        if (SUCCEEDED(dev->QueryInterface(kIidD3d9DevEx, &pExRaw)) && pExRaw) {
            auto* devEx = static_cast<IDirect3DDevice9Ex*>(pExRaw);
            static bool s_capturedOrig = false;
            if (!s_capturedOrig) {
                UINT origLat = 3;
                if (SUCCEEDED(devEx->GetMaximumFrameLatency(&origLat)) && origLat >= 1 && origLat <= 16) {
                    g_d3d9ExOrigLatency.store(origLat, std::memory_order_relaxed);
                }
                s_capturedOrig = true;
            }
            const UINT origLat = g_d3d9ExOrigLatency.load(std::memory_order_relaxed);
            const bool wantLow = g_optD3d9ExLowLatency.load(std::memory_order_relaxed);
            const UINT targetLat = wantLow ? 1u : origLat;
            HRESULT hr = devEx->SetMaximumFrameLatency(targetLat);
            UINT readback = targetLat;
            devEx->GetMaximumFrameLatency(&readback);
            devEx->Release();
            if (SUCCEEDED(hr)) {
                g_d3d9ExCurLatency.store(readback, std::memory_order_relaxed);
                g_d3d9ExActive.store(wantLow && readback == 1u, std::memory_order_release);
                LogMsg("[LATENCY] D3D9Ex SetMaximumFrameLatency: orig=%u -> target=%u (readback=%u, ReflexQueue=%s)",
                       origLat, targetLat, readback, (wantLow && readback == 1u) ? "ON" : "OFF");
            }
        }
    }

    // 2. Native Hardware Mipmap LOD Bias (D3DSAMP_MIPMAPLODBIAS = 8)
    //    Strictly mipmapped world textures (mipFilter != 0); UI/LUT/FBO stay 0.0f!
    //    Zero extra D3D9 calls when Vanilla Pure (0.0f) and already clean.
    if (sampler < 16u) {
        const float rawBias = g_visualMipLodBias.load(std::memory_order_relaxed);
        const float desiredBias = (mipFilter != 0) ? rawBias : 0.0f;
        uint32_t biasBits = 0;
        memcpy(&biasBits, &desiredBias, sizeof(biasBits));
        if (biasBits != 0u || s_lastBiasBits[sampler] != 0u) {
            dev->SetSamplerState(sampler, D3DSAMP_MIPMAPLODBIAS, static_cast<DWORD>(biasBits));
            s_lastBiasBits[sampler] = biasBits;
            if (biasBits != 0u) {
                InterlockedIncrement64(&g_mipBiasAppliedHits);
            }
        }
    }

    // 3. Original ANGLE D3DSAMP_MAXANISOTROPY (10) behavior
    if (afSupported) {
        const DWORD aniso = static_cast<DWORD>(static_cast<int64_t>(maxAniso));
        dev->SetSamplerState(sampler, D3DSAMP_MAXANISOTROPY, aniso);
    }
}

static void PrepareVisualFidelityHooks(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    // V1a: Unlock ANGLE 1.0.0.2249 hardcoded mSupportsTextureFilterAnisotropy = false (+0x8E3)
    //      inside libGLESv2.dll Context::initCaps so D3D9 SetSamplerState(D3DSAMP_MAXANISOTROPY) activates!
    HMODULE hGles = GetModuleHandleW(L"libGLESv2.dll");
    if (hGles) {
        g_fnGlTexParameterf = reinterpret_cast<GlTexParameterf_t>(GetProcAddress(hGles, "glTexParameterf"));
        g_fnGlGetFloatv     = reinterpret_cast<GlGetFloatv_t>(GetProcAddress(hGles, "glGetFloatv"));
        g_fnGlGetString     = reinterpret_cast<GlGetString_t>(GetProcAddress(hGles, "glGetString"));

        uint8_t* glText = nullptr;
        size_t glTextSz = 0;
        if (GetModuleTextSection(hGles, glText, glTextSz)) {
            // Unique 38-byte signature in ANGLE 1.0.0.2249 libGLESv2.dll Context::initCaps:
            //   +0:  mov [rsi+0x8e1], al
            //   +6:  mov word ptr [rsi+0x8e2], r12w  (hardcodes 0x8e2=0 & 0x8e3=0!)
            //   +14: xor r13d, r13d ; mov eax, r13d
            constexpr uint8_t kSigAngleCaps[] = {
                0x88, 0x86, 0xE1, 0x08, 0x00, 0x00,
                0x66, 0x44, 0x89, 0xA6, 0xE2, 0x08, 0x00, 0x00,
                0x45, 0x33, 0xED,
                0x41, 0x8B, 0xC5,
                0x44, 0x38, 0xAE, 0xD6, 0x08, 0x00, 0x00,
                0xB9, 0x02, 0x00, 0x00, 0x00,
                0x0F, 0x45, 0xC1,
                0x89, 0x86, 0xE4, 0x08, 0x00, 0x00
            };
            uint8_t* hit = FindUniquePatternInText(glText, glTextSz, kSigAngleCaps, sizeof(kSigAngleCaps));
            if (hit) {
                uint8_t* patchSite = hit + 6; // exact 14 bytes: mov [rsi+0x8e2],r12w + xor r13d,r13d + mov eax,r13d
                uint8_t* stub = AllocNearExecutable(patchSite, 64);
                if (stub) {
                    const uint64_t retVa = reinterpret_cast<uint64_t>(patchSite + 14);
                    const float oneF = 1.0f;
                    uint8_t code[53] = {
                        0x44, 0x88, 0xA6, 0xE2, 0x08, 0x00, 0x00,       // mov byte ptr [rsi+0x8e2], r12b (0)
                        0xF3, 0x0F, 0x10, 0x86, 0xBC, 0x08, 0x00, 0x00, // movss xmm0, [rsi+0x8bc] (hwMaxAniso)
                        0x0F, 0x2E, 0x05, 0x1B, 0x00, 0x00, 0x00,       // ucomiss xmm0, [rip+0x1B] (1.0f @ +49)
                        0x0F, 0x97, 0x86, 0xE3, 0x08, 0x00, 0x00,       // seta byte ptr [rsi+0x8e3]
                        0x45, 0x33, 0xED, 0x41, 0x8B, 0xC5,             // xor r13d,r13d ; mov eax,r13d
                        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,             // jmp [rip+0] -> retVa
                        0,0,0,0,0,0,0,0,
                        0,0,0,0                                         // float 1.0f @ offset 49
                    };
                    memcpy(code + 41, &retVa, 8);
                    memcpy(code + 49, &oneF, 4);
                    memcpy(stub, code, sizeof(code));
                    FinalizeExecPageWX(stub, sizeof(code));

                    PendingCodePatch p{};
                    p.addr = patchSite;
                    p.len = 14;
                    p.ownedExecPage = stub;
                    uint8_t jmp[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
                    uint64_t dst = reinterpret_cast<uint64_t>(stub);
                    memcpy(jmp + 6, &dst, 8);
                    memcpy(p.bytes, jmp, 14);
                    char msg[192];
                    snprintf(msg, sizeof(msg), "[VISUAL] Unlocked ANGLE 1.0 hardware AF cap @ %p (W^X RX stub)", static_cast<void*>(patchSite));
                    p.logMsg = msg;
                    g_angleAfPatchIdx = patches.size();
                    patches.push_back(std::move(p));
                }
            } else {
                LogMsg("[VISUAL] ANGLE AF cap unlock skipped (libGLESv2 AOB miss; fail-closed)");
            }

            // V1b + Pillar 2: Unified ANGLE gl::Context::applyTextures hook (0x18000fdf3..0x18000fe17, 36B, 0 RIP-rel)
            //   Enables BOTH hardware D3D9Ex SetMaximumFrameLatency(1) AND native D3DSAMP_MIPMAPLODBIAS!
            constexpr uint8_t kSigAngleApplySampler[36] = {
                0x41, 0x80, 0xBD, 0xE3, 0x08, 0x00, 0x00, 0x00,
                0x74, 0x1A,
                0x49, 0x8B, 0x4D, 0x18,
                0x41, 0xB8, 0x0A, 0x00, 0x00, 0x00,
                0xF3, 0x4C, 0x0F, 0x2C, 0xCE,
                0x8B, 0xD7,
                0x48, 0x8B, 0x01,
                0xFF, 0x90, 0x28, 0x02, 0x00, 0x00
            };
            uint8_t* pSamp = FindUniquePatternInText(glText, glTextSz, kSigAngleApplySampler, sizeof(kSigAngleApplySampler));
            if (pSamp) {
                uint8_t* stub = AllocNearExecutable(pSamp, 64);
                if (stub) {
                    const uint64_t fnVa  = reinterpret_cast<uint64_t>(&OnAngleApplySampler);
                    const uint64_t retVa = reinterpret_cast<uint64_t>(pSamp + sizeof(kSigAngleApplySampler));
                    uint8_t code[62] = {
                        0x49, 0x8B, 0x4D, 0x18,                         // mov rcx, [r13 + 0x18]
                        0x8B, 0xD7,                                     // mov edx, edi
                        0x44, 0x8B, 0x44, 0x24, 0x24,                   // mov r8d, [rsp + 0x24]
                        0x41, 0x0F, 0xB6, 0x85, 0xE3, 0x08, 0x00, 0x00, // movzx eax, byte ptr [r13 + 0x8e3]
                        0xC1, 0xE0, 0x10,                               // shl eax, 16
                        0x41, 0x09, 0xC0,                               // or r8d, eax
                        0x0F, 0x28, 0xDE,                               // movaps xmm3, xmm6
                        0x48, 0x83, 0xEC, 0x20,                         // sub rsp, 0x20
                        0x48, 0xB8, 0,0,0,0,0,0,0,0,                    // movabs rax, &OnAngleApplySampler
                        0xFF, 0xD0,                                     // call rax
                        0x48, 0x83, 0xC4, 0x20,                         // add rsp, 0x20
                        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,             // jmp [rip + 0] -> retVa
                        0,0,0,0,0,0,0,0
                    };
                    memcpy(code + 34, &fnVa, 8);
                    memcpy(code + 54, &retVa, 8);
                    memcpy(stub, code, sizeof(code));
                    FinalizeExecPageWX(stub, sizeof(code));

                    PendingCodePatch p{};
                    p.addr = pSamp;
                    p.len = sizeof(kSigAngleApplySampler);
                    p.ownedExecPage = stub;
                    memset(p.bytes, 0x90, p.len);
                    uint8_t jmp[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
                    const uint64_t dst = reinterpret_cast<uint64_t>(stub);
                    memcpy(jmp + 6, &dst, 8);
                    memcpy(p.bytes, jmp, 14);
                    char msg[224];
                    snprintf(msg, sizeof(msg),
                             "[LATENCY+VISUAL] Hooked ANGLE applyTextures @ %p -> D3D9Ex MaxLatency=1 + Native MipLodBias (W^X RX stub)",
                             static_cast<void*>(pSamp));
                    p.logMsg = msg;
                    g_angleSamplerPatchIdx = patches.size();
                    patches.push_back(std::move(p));
                }
            } else {
                LogMsg("[LATENCY+VISUAL] ANGLE applyTextures hook skipped (libGLESv2 AOB miss; fail-closed)");
            }
        }
    }

    // V2: Full-Resolution LightMap (0.5x -> 1.0x) inside LightMap::InitRenderTarget (unique 1-match AOB)
    //     Redirects ONLY this instruction's RIP-relative float to a private non-executable RW float slot
    //     so the 170 other sites sharing .rdata 0.5f are 100% untouched!
    constexpr uint8_t kSigLightMapInit[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x80, 0x79, 0x14, 0x00,
        0x48, 0x8B, 0xD9, 0x74, 0x08, 0xB0, 0x01, 0x48, 0x83, 0xC4, 0x40, 0x5B, 0xC3,
        0x48, 0x8B, 0x09, 0xF3, 0x0F, 0x10, 0x0D
    };
    uint8_t* lmFn = FindUniquePatternInText(textStart, textSize, kSigLightMapInit, sizeof(kSigLightMapInit));
    if (lmFn && memcmp(lmFn + sizeof(kSigLightMapInit) + 4, "\x66\x0F\xEF\xC0", 4) == 0) {
        uint8_t* dispAddr = lmFn + sizeof(kSigLightMapInit);
        uint8_t* nextIp = dispAddr + 4;
        int32_t origDisp = 0;
        memcpy(&origDisp, dispAddr, 4);
        const float* origScalePtr = reinterpret_cast<const float*>(nextIp + origDisp);
        if (fabsf(*origScalePtr - 0.5f) < 1e-5f) {
            uint8_t* rwPage = AllocNearReadWriteData(nextIp, 4096);
            if (rwPage) {
                intptr_t newDisp = reinterpret_cast<intptr_t>(rwPage) - reinterpret_cast<intptr_t>(nextIp);
                if (newDisp >= INT32_MIN && newDisp <= INT32_MAX) {
                    auto* slot = reinterpret_cast<volatile float*>(rwPage);
                    *slot = g_visualFullresLightmap.load(std::memory_order_relaxed) ? 1.0f : 0.5f;
                    g_lightmapScaleSlot = slot;

                    PendingCodePatch p{};
                    p.addr = dispAddr;
                    p.len = 4;
                    int32_t d32 = static_cast<int32_t>(newDisp);
                    memcpy(p.bytes, &d32, 4);
                    char msg[224];
                    snprintf(msg, sizeof(msg),
                             "[VISUAL] Patched LightMap::InitRenderTarget @ %p -> Full-Res %.1fx (private RW slot %p)",
                             static_cast<void*>(lmFn), static_cast<double>(*slot), static_cast<void*>(rwPage));
                    p.logMsg = msg;
                    g_lightmapScalePatchIdx = patches.size();
                    patches.push_back(std::move(p));
                } else {
                    VirtualFree(rwPage, 0, MEM_RELEASE);
                }
            }
        } else {
            LogMsg("[VISUAL] Full-Res LightMap skipped (expected 0.5f, got %.3f; fail-closed)", static_cast<double>(*origScalePtr));
        }
    } else {
        LogMsg("[VISUAL] Full-Res LightMap skipped (AOB miss; fail-closed)");
    }
}

//------------------------------------------------------------------------------
// Visual Fidelity HD-2D World Shaders (v2.5.0-hd2d) & Render Performance Telemetry:
//   - Zero-Extra-Pass GLSL ES 1.00 injection via libGLESv2.dll PE IAT hooks
//     (@99 glShaderSource, @90 glLinkProgram, @131 glUseProgram, @32 glDeleteProgram).
//   - Targets strictly 10 World Pixel Shaders (ZERO UI, Bloom, Minimap, Font, Waterfall!):
//       1) OCEAN_COMB (ocean_combined.ksh): 3-wave chain-rule micro-normals, directional
//          sun/moon wave diffuse + specular sparkles, coastal scatter, campfire reflections.
//       2) OCEAN_DEC  (ocean.ksh): 0-tap soft-knee specular headroom lift (0.50 -> ~0.88).
//       3) GROUND     (ground.ksh, ground_underground.ksh): world-space turf stroke relief,
//          campfire/torch normal-mapping, rain wetness darkening + specular sheen.
//       4) ANIM       (anim*.ksh 6 world variants): 0-tap ground contact AO, crown height
//          lift, quad-coherent screen-space directional Rim Light + firelight glow.
//   - Strictly NO fake Z-buffer DoF (PrimaryRT uses unsampleable Depth Renderbuffer).
//   - Live World Render / PostProcess / Busy / GPU Drain ms + DXGI VRAM telemetry.
//------------------------------------------------------------------------------
static size_t FindPrecisionEndOffset(const std::string& src) {
    size_t p = src.find("precision mediump float;");
    if (p == std::string::npos) {
        p = src.find("precision highp float;");
    }
    if (p == std::string::npos) return 0;
    size_t e = src.find("#endif", p);
    if (e == std::string::npos) return 0;
    size_t nl = src.find('\n', e);
    return (nl == std::string::npos) ? (e + 6) : (nl + 1);
}

static bool TransformHd2dShaderSource(const std::string& src, std::string& outSrc, const char*& outTag) {
    outTag = nullptr;
    if (src.find("gl_FragColor") == std::string::npos) return false;
    if (src.find("HS_HD2D_PARAMS") != std::string::npos) return false;

    static constexpr const char* kUniformDecls =
        "\nuniform vec4 HS_HD2D_PARAMS;\n"
        "uniform vec4 HS_HD2D_SUN;\n"
        "uniform vec4 HS_HD2D_TINT;\n";

    // 1. OCEAN_COMB (ocean_combined.ksh): calm vanilla dark-ink water + whisper-soft shoreline transition (0 extra taps)
    if (src.find("OCEAN_UV0_PARAMS") != std::string::npos &&
        src.find("LIGHTMAP_TEXTURE") != std::string::npos &&
        src.find("gl_FragColor = vec4( colour.rgb, alpha );") != std::string::npos) {
        static constexpr const char* kLmCall = "colour.rgb *= CalculateLightingContribution();";
        static constexpr const char* kEncLine = "float encoding_factor = 2.0;";
        size_t posLm = src.find(kLmCall);
        size_t posEnc = src.find(kEncLine);
        if (posLm != std::string::npos && posEnc != std::string::npos && posEnc > posLm) {
            std::string s = src;
            size_t uOff = FindPrecisionEndOffset(s);
            s.insert(uOff, kUniformDecls);
            posLm = s.find(kLmCall);
            s.replace(posLm, strlen(kLmCall), "vec3 hs_lm_c = CalculateLightingContribution();\n\tcolour.rgb *= hs_lm_c;");
            posEnc = s.find(kEncLine);
            static constexpr const char* kOceanBody =
                "\tif (HS_HD2D_PARAMS.y > 0.001) {\n"
                "\t\tfloat hs_s = min(HS_HD2D_PARAMS.y, 1.25);\n"
                "\t\tfloat hs_lmLuma = dot(hs_lm_c, vec3(0.299, 0.587, 0.114));\n"
                "\t\tfloat hs_shallow = clamp(1.0 - noise_opacity, 0.0, 1.0);\n"
                "\t\tvec3 hs_coastal = vec3(0.008, 0.022, 0.026) * (hs_shallow * 0.85 * hs_s * hs_lmLuma);\n"
                "\t\tcolour.rgb += hs_coastal;\n"
                "\t}\n\t";
            s.insert(posEnc, kOceanBody);
            outSrc = std::move(s);
            outTag = "OCEAN_COMB";
            return true;
        }
    }

    // 2. GROUND (ground.ksh, ground_underground.ksh): 100% dry matte hand-drawn parchment on clear days;
    //    subtle matte rain-dampening ONLY while raining (0 extra taps, zero ink-line emboss!)
    if (src.find("NOISE_REPEAT_SIZE") != std::string::npos &&
        src.find("BASE_TEXTURE") != std::string::npos &&
        src.find("NOISE_TEXTURE") != std::string::npos &&
        src.find("base_colour.rgb *= CalculateLightingContribution();") != std::string::npos) {
        static constexpr const char* kLmLine = "base_colour.rgb *= CalculateLightingContribution();";
        std::string s = src;
        size_t uOff = FindPrecisionEndOffset(s);
        s.insert(uOff, kUniformDecls);
        size_t posL = s.find(kLmLine);
        if (posL != std::string::npos) {
            static constexpr const char* kGroundBody =
                "base_colour.rgb *= CalculateLightingContribution();\n"
                "\tif (HS_HD2D_PARAMS.y > 0.001 && HS_HD2D_TINT.a > 0.005) {\n"
                "\t\tfloat hs_wet = clamp(HS_HD2D_TINT.a * min(HS_HD2D_PARAMS.y, 1.2), 0.0, 1.0);\n"
                "\t\tfloat hs_edge = smoothstep(0.15, 0.85, base_colour.a);\n"
                "\t\tbase_colour.rgb *= mix(1.0, 1.0 - 0.09 * hs_wet, hs_edge);\n"
                "\t}";
            s.replace(posL, strlen(kLmLine), kGroundBody);
            outSrc = std::move(s);
            outTag = "GROUND";
            return true;
        }
    }

    // 3. ANIM (anim, anim_skinned, anim_fade, anim_haunted, anim_fade_haunted, anim_holo;
    //    strictly excludes BLOOM & UI_CC shaders!):
    //    Calm ground Contact AO (~13% at base y=0..0.45, ink-protected) + eye-comfort soft highlight
    //    shoulder (>0.86) preventing white-face/marble glare. 0 extra taps, 0 dFdx/dFdy artifacts!
    static constexpr const char* kAnimAnchor =
        "gl_FragColor.rgb *= max( light.rgb, vec3( LIGHT_OVERRIDE, LIGHT_OVERRIDE, LIGHT_OVERRIDE ) );";
    if (src.find(kAnimAnchor) != std::string::npos &&
        src.find("OCEAN_BLEND_PARAMS") != std::string::npos &&
        src.find("UI_CC") != std::string::npos &&
        src.find("#define BLOOM\r") == std::string::npos &&
        src.find("#define BLOOM\n") == std::string::npos &&
        src.find("#define UI_CC\r") == std::string::npos &&
        src.find("#define UI_CC\n") == std::string::npos) {
        std::string s = src;
        size_t uOff = FindPrecisionEndOffset(s);
        s.insert(uOff, kUniformDecls);
        size_t pos = s.find(kAnimAnchor);
        if (pos != std::string::npos) {
            static constexpr const char* kAnimBody =
                "\n\t\tif (HS_HD2D_PARAMS.x > 0.001) {\n"
                "\t\t\tfloat hs_s = min(HS_HD2D_PARAMS.x, 1.15);\n"
                "\t\t\tfloat hs_a = gl_FragColor.a;\n"
                "\t\t\tfloat hs_effMask = step(0.08, hs_a) * (1.0 - smoothstep(0.05, 0.35, LIGHT_OVERRIDE));\n"
                "\t\t\tfloat hs_aSafe = max(hs_a, 0.001);\n"
                "\t\t\tvec3 hs_col = gl_FragColor.rgb / hs_aSafe;\n"
                "\t\t\tfloat hs_luma = dot(hs_col, vec3(0.299, 0.587, 0.114));\n"
                "\t\t\tfloat hs_ink = smoothstep(0.05, 0.22, hs_luma);\n"
                "\t\t\tfloat hs_y = max(PS_POS.y, 0.0);\n"
                "\t\t\tfloat hs_ao = mix(1.0 - 0.13 * hs_s, 1.0, smoothstep(0.0, 0.45, hs_y));\n"
                "\t\t\tvec3 hs_res = hs_col * mix(1.0, hs_ao, hs_ink);\n"
                "\t\t\tvec3 hs_over = max(hs_res - vec3(0.86), vec3(0.0));\n"
                "\t\t\ths_res -= hs_over * (0.35 * min(hs_s, 1.0));\n"
                "\t\t\tgl_FragColor.rgb = mix(gl_FragColor.rgb, min(max(hs_res, vec3(0.0)) * hs_a, vec3(hs_a)), hs_effMask);\n"
                "\t\t}";
            s.insert(pos + strlen(kAnimAnchor), kAnimBody);
            outSrc = std::move(s);
            outTag = "ANIM";
            return true;
        }
    }

    // 4. COLOURCUBE (postprocess_colourcube.ksh & postprocess_colourcube_linedistort.ksh):
    //    Runs on EVERY frame even with Game Bloom OFF!
    //    When HS_HD2D_PARAMS.z == 0.0 (Player-First default): 100% bit-for-bit identical vanilla.
    //    When HS_HD2D_PARAMS.z > 0.001 (opt-in Dark-Scene Eye-Comfort Clarity):
    //      a) Hue-preserving crushed dark-toe lift strictly in dim visible shadows (0.006..0.24),
    //         keeping pitch-black fog-of-war (<0.006) pure black and daylight (>0.24) 100% untouched!
    //      b) Fixes Klei's 31.0 -> 31.5 LUT texel-center clamp on R/G channels (0 extra taps!).
    static constexpr const char* kCcAnchor =
        "vec3 intermediate = colour.rgb * vec3( CUBE_DIMENSION - 1.0, CUBE_DIMENSION - 1.0, CUBE_DIMENSION - 1.0 );";
    if (src.find("#define SRC_IMAGE SAMPLER[0]") != std::string::npos &&
        src.find("#define COLOUR_CUBE SAMPLER[1]") != std::string::npos &&
        src.find("UI_CC") == std::string::npos &&
        src.find(kCcAnchor) != std::string::npos &&
        src.find("min( intermediate.r + 0.5, 31.0 )") != std::string::npos) {
        std::string s = src;
        size_t uOff = FindPrecisionEndOffset(s);
        s.insert(uOff, kUniformDecls);
        size_t posCc = s.find(kCcAnchor);
        if (posCc != std::string::npos) {
            static constexpr const char* kCcBody =
                "vec3 hs_cc_in = colour.rgb;\n"
                "\tfloat hs_cc_max = 31.0;\n"
                "\tif (HS_HD2D_PARAMS.z > 0.001) {\n"
                "\t\tfloat hs_dc = min(HS_HD2D_PARAMS.z, 1.0);\n"
                "\t\tfloat hs_l = dot(hs_cc_in, vec3(0.299, 0.587, 0.114));\n"
                "\t\tfloat hs_toe = smoothstep(0.006, 0.032, hs_l) * (1.0 - smoothstep(0.09, 0.24, hs_l));\n"
                "\t\ths_cc_in = min(hs_cc_in * (1.0 + 0.26 * hs_dc * hs_toe), vec3(1.0));\n"
                "\t\ths_cc_max = 31.5;\n"
                "\t}\n"
                "\tvec3 intermediate = hs_cc_in * vec3( CUBE_DIMENSION - 1.0, CUBE_DIMENSION - 1.0, CUBE_DIMENSION - 1.0 );";
            s.replace(posCc, strlen(kCcAnchor), kCcBody);
            // Replace all 4 occurrences of `, 31.0 )` inside ApplyColourCube with `, hs_cc_max )`
            size_t endCc = s.find("return mix(floor_col", posCc);
            if (endCc != std::string::npos) {
                size_t scan = posCc;
                while ((scan = s.find(", 31.0 )", scan)) != std::string::npos && scan < endCc) {
                    s.replace(scan, 8, ", hs_cc_max )");
                    endCc += 5;
                    scan += 13;
                }
            }
            outSrc = std::move(s);
            outTag = "COLOURCUBE";
            return true;
        }
    }

    // 5. LIGHTING (shaders/lighting.ksh): Zero-tap screen-space IGN triangular dithering on
    //    campfire / torch / lantern / firefly 8-bit LightMap falloff gradients (gated by HS_HD2D_SUN.z).
    //    Eliminates concentric night ring banding without lifting pitch-black darkness (t <= 0.002 untouched).
    //    When HS_HD2D_SUN.z == 0.0 (Vanilla Pure default): 100% bit-for-bit identical vanilla!
    static constexpr const char* kLightAnchor = "gl_FragColor = vec4( colour, 1 );";
    if (src.find("LIGHT_POS") != std::string::npos &&
        src.find("LIGHT_PARAMETERS") != std::string::npos &&
        src.find("LIGHT_CONSTANTS") != std::string::npos &&
        src.find("exp(K0* pow((dist/RC), -K1))") != std::string::npos &&
        src.find(kLightAnchor) != std::string::npos) {
        std::string s = src;
        size_t uOff = FindPrecisionEndOffset(s);
        s.insert(uOff, kUniformDecls);
        size_t posL = s.find(kLightAnchor);
        if (posL != std::string::npos) {
            static constexpr const char* kLightBody =
                "if (HS_HD2D_SUN.z > 0.001 && t > 0.002) {\n"
                "\t\tfloat hs_ign = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n"
                "\t\tcolour = clamp(colour + vec3((hs_ign - 0.5) * (1.35 / 255.0)), vec3(0.0), vec3(1.0));\n"
                "\t}\n"
                "\tgl_FragColor = vec4( colour, 1 );";
            s.replace(posL, strlen(kLightAnchor), kLightBody);
            outSrc = std::move(s);
            outTag = "LIGHTING";
            return true;
        }
    }

    return false;
}

static void WINAPI HookGlShaderSource(uint32_t shader, int32_t count, const char* const* string, const int32_t* length) {
    if (!g_origGlShaderSource) return;
    if (g_optHd2dWorldShaders.load(std::memory_order_relaxed) && count > 0 && string != nullptr) {
        std::string combined;
        for (int32_t i = 0; i < count; ++i) {
            if (!string[i]) continue;
            if (length && length[i] >= 0) {
                combined.append(string[i], static_cast<size_t>(length[i]));
            } else {
                combined.append(string[i]);
            }
        }
        std::string transformed;
        const char* tag = nullptr;
        if (TransformHd2dShaderSource(combined, transformed, tag)) {
            const char* ptr = transformed.c_str();
            const int32_t len = static_cast<int32_t>(transformed.size());
            g_origGlShaderSource(shader, 1, &ptr, &len);
            const uint32_t n = g_hd2dShadersInjected.fetch_add(1, std::memory_order_relaxed) + 1;
            LogMsg("[VISUAL][HD2D] Injected world shader #%u (%s, shader=%u, %zu -> %zu bytes)",
                   n, tag ? tag : "?", shader, combined.size(), transformed.size());
            return;
        }
    }
    g_origGlShaderSource(shader, count, string, length);
}

// =============================================================================
// Per-Thread GLES2 State-Mirror Cache (cuts redundant 2015 ANGLE D3D9 calls)
// + eglSwapBuffers Present-Latency Telemetry & Context-Switch Invalidation
// =============================================================================
static thread_local hs_gl_cache::ThreadState tl_glCache;
static thread_local uint32_t                 tl_glCacheGen = 0;

static inline hs_gl_cache::ThreadState& GetSyncedGlCache() {
    const uint32_t curGen = g_glCacheResetGen.load(std::memory_order_acquire);
    if (tl_glCacheGen != curGen) {
        tl_glCache.Reset();
        tl_glCacheGen = curGen;
    }
    return tl_glCache;
}

static inline void FlushGlCacheStatsIfReady(hs_gl_cache::ThreadState& st) {
    if (st.batchCalls >= 256u || st.batchUniCalls >= 256u) {
        if (st.batchCalls) {
            InterlockedAdd64(&g_glCacheCalls, static_cast<LONG64>(st.batchCalls));
            InterlockedAdd64(&g_glCacheHits,  static_cast<LONG64>(st.batchHits));
            st.batchCalls = 0;
            st.batchHits  = 0;
        }
        if (st.batchUniCalls) {
            InterlockedAdd64(&g_glUniCalls, static_cast<LONG64>(st.batchUniCalls));
            InterlockedAdd64(&g_glUniHits,  static_cast<LONG64>(st.batchUniHits));
            st.batchUniCalls = 0;
            st.batchUniHits  = 0;
        }
    }
}

static inline bool IsGlCacheOperational() {
    return g_glCacheActive.load(std::memory_order_acquire) &&
           g_optGlStateCache.load(std::memory_order_relaxed);
}

static inline bool IsGlUniformCacheOperational() {
    return IsGlCacheOperational() &&
           g_glUniformCacheActive.load(std::memory_order_acquire) &&
           g_optGlUniformCache.load(std::memory_order_relaxed);
}

static uint32_t WINAPI HookEglMakeCurrent(void* dpy, void* draw, void* read, void* ctx) {
    const uint32_t ok = g_origEglMakeCurrent ? g_origEglMakeCurrent(dpy, draw, read, ctx) : 0u;
    if (ok) {
        tl_glCache.OnMakeCurrent(ctx);
    } else {
        tl_glCache.Reset();
    }
    return ok;
}

static uint32_t WINAPI HookEglSwapBuffers(void* dpy, void* surface) {
    if (!g_origEglSwapBuffers) return 0u;
    const int64_t t0 = NowQpc();
    const uint32_t rc = g_origEglSwapBuffers(dpy, surface);
    const double swapUs = static_cast<double>(NowQpc() - t0) * g_qpcToUs;
    if (swapUs >= 0.0 && swapUs < 1000000.0) {
        g_lastSwapUs.store(static_cast<uint32_t>(swapUs), std::memory_order_relaxed);
        UpdateAtomicEma(g_emaSwapUs, static_cast<float>(swapUs), 0.06f);
    }
    return rc;
}

static void WINAPI HookGlActiveTexture(uint32_t texture) {
    if (!g_origGlActiveTexture) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallActiveTexture(texture);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlActiveTexture(texture);
}

static void WINAPI HookGlBindTexture(uint32_t target, uint32_t texture) {
    if (!g_origGlBindTexture) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallBindTexture(target, texture);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlBindTexture(target, texture);
}

static void WINAPI HookGlDeleteTextures(int32_t n, const uint32_t* textures) {
    tl_glCache.OnDeleteTextures(n, textures);
    if (g_origGlDeleteTextures) {
        g_origGlDeleteTextures(n, textures);
    }
}

static void WINAPI HookGlBlendFunc(uint32_t sfactor, uint32_t dfactor) {
    if (!g_origGlBlendFunc) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallBlendFunc(sfactor, dfactor);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlBlendFunc(sfactor, dfactor);
}

static void WINAPI HookGlDepthMask(uint8_t flag) {
    if (!g_origGlDepthMask) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallDepthMask(flag);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlDepthMask(flag);
}

static void WINAPI HookGlEnable(uint32_t cap) {
    if (!g_origGlEnable) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallEnableDisable(cap, true);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlEnable(cap);
}

static void WINAPI HookGlDisable(uint32_t cap) {
    if (!g_origGlDisable) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallEnableDisable(cap, false);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlDisable(cap);
}

static void WINAPI HookGlBindBuffer(uint32_t target, uint32_t buffer) {
    if (!g_origGlBindBuffer) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallBindBuffer(target, buffer);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlBindBuffer(target, buffer);
}

static void WINAPI HookGlDeleteBuffers(int32_t n, const uint32_t* buffers) {
    tl_glCache.OnDeleteBuffers(n, buffers);
    if (g_origGlDeleteBuffers) g_origGlDeleteBuffers(n, buffers);
}

static void WINAPI HookGlBlendEquation(uint32_t mode) {
    if (!g_origGlBlendEquation) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallBlendEquation(mode);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlBlendEquation(mode);
}

static void WINAPI HookGlColorMask(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!g_origGlColorMask) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallColorMask(r, g, b, a);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlColorMask(r, g, b, a);
}

static void WINAPI HookGlCullFace(uint32_t mode) {
    if (!g_origGlCullFace) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallCullFace(mode);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlCullFace(mode);
}

static void WINAPI HookGlDepthFunc(uint32_t func) {
    if (!g_origGlDepthFunc) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallDepthFunc(func);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlDepthFunc(func);
}

static void WINAPI HookGlEnableVertexAttribArray(uint32_t index) {
    if (!g_origGlEnableVertexAttribArray) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallEnableDisableVertexAttribArray(index, true);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlEnableVertexAttribArray(index);
}

static void WINAPI HookGlDisableVertexAttribArray(uint32_t index) {
    if (!g_origGlDisableVertexAttribArray) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallEnableDisableVertexAttribArray(index, false);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlDisableVertexAttribArray(index);
}

static void WINAPI HookGlFrontFace(uint32_t mode) {
    if (!g_origGlFrontFace) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallFrontFace(mode);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlFrontFace(mode);
}

static void WINAPI HookGlScissor(int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!g_origGlScissor) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallScissor(x, y, width, height);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlScissor(x, y, width, height);
}

static void WINAPI HookGlViewport(int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!g_origGlViewport) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallViewport(x, y, width, height);
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlViewport(x, y, width, height);
}

static void WINAPI HookGlUniform1f(int32_t location, float x) {
    if (!g_origGlUniform1f) return;
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, 1, 0, hs_gl_cache::UNI_1F, &x, sizeof(float));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlUniform1f(location, x);
}

static void WINAPI HookGlUniform1i(int32_t location, int32_t x) {
    if (!g_origGlUniform1i) return;
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, 1, 0, hs_gl_cache::UNI_1I, &x, sizeof(int32_t));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlUniform1i(location, x);
}

static void WINAPI HookGlUniform2fv(int32_t location, int32_t count, const float* v) {
    if (!g_origGlUniform2fv) return;
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, count, 0, hs_gl_cache::UNI_2FV, v, 2 * sizeof(float));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlUniform2fv(location, count, v);
}

static void WINAPI HookGlUniform3fv(int32_t location, int32_t count, const float* v) {
    if (!g_origGlUniform3fv) return;
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, count, 0, hs_gl_cache::UNI_3FV, v, 3 * sizeof(float));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlUniform3fv(location, count, v);
}

static void WINAPI HookGlUniform4fv(int32_t location, int32_t count, const float* v) {
    if (!g_origGlUniform4fv) return;
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, count, 0, hs_gl_cache::UNI_4FV, v, 4 * sizeof(float));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlUniform4fv(location, count, v);
}

static void WINAPI HookGlUniformMatrix3fv(int32_t location, int32_t count, uint8_t transpose, const float* value) {
    if (!g_origGlUniformMatrix3fv) return;
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, count, transpose, hs_gl_cache::UNI_MAT3, value, 9 * sizeof(float));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlUniformMatrix3fv(location, count, transpose, value);
}

static void WINAPI HookGlUniformMatrix4fv(int32_t location, int32_t count, uint8_t transpose, const float* value) {
    if (!g_origGlUniformMatrix4fv) return;
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, count, transpose, hs_gl_cache::UNI_MAT4, value, 16 * sizeof(float));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    g_origGlUniformMatrix4fv(location, count, transpose, value);
}

// Context-scoped program slot helpers (fast O(1) array for primary context + overflow vector for multi-context)
static Hd2dProgSlot* FindProgSlotLocked(void* ctx, uint32_t program) {
    if (program < kMaxHd2dProgs && g_hd2dProgs[program].checked && g_hd2dProgs[program].ctx == ctx) {
        return &g_hd2dProgs[program];
    }
    for (auto& e : g_hd2dOverflowProgs) {
        if (e.ctx == ctx && e.program == program) {
            return &e.slot;
        }
    }
    return nullptr;
}

static Hd2dProgSlot& UpsertProgSlotLocked(void* ctx, uint32_t program) {
    // 1. Exact match in primary array first
    if (program < kMaxHd2dProgs && g_hd2dProgs[program].checked && g_hd2dProgs[program].ctx == ctx) {
        return g_hd2dProgs[program];
    }
    // 2. Exact match in multi-context overflow vector (prevents duplicate slots when primary was erased!)
    for (auto& e : g_hd2dOverflowProgs) {
        if (e.ctx == ctx && e.program == program) {
            return e.slot;
        }
    }
    // 3. Occupy empty primary array slot only if not already present in overflow
    if (program < kMaxHd2dProgs && !g_hd2dProgs[program].checked) {
        return g_hd2dProgs[program];
    }
    // 4. Allocate new overflow entry
    g_hd2dOverflowProgs.push_back(Hd2dCtxProgEntry{ctx, program, Hd2dProgSlot{}});
    return g_hd2dOverflowProgs.back().slot;
}

static void EraseProgSlotLocked(void* ctx, uint32_t program) {
    auto decLinked = []() {
        uint32_t cur = g_hd2dProgsLinked.load(std::memory_order_relaxed);
        while (cur > 0 && !g_hd2dProgsLinked.compare_exchange_weak(cur, cur - 1, std::memory_order_relaxed)) {}
    };
    if (program < kMaxHd2dProgs && g_hd2dProgs[program].checked && g_hd2dProgs[program].ctx == ctx) {
        if (g_hd2dProgs[program].hasAny) decLinked();
        g_hd2dProgs[program] = {};
        return;
    }
    for (size_t i = 0; i < g_hd2dOverflowProgs.size(); ++i) {
        if (g_hd2dOverflowProgs[i].ctx == ctx && g_hd2dOverflowProgs[i].program == program) {
            if (g_hd2dOverflowProgs[i].slot.hasAny) decLinked();
            g_hd2dOverflowProgs[i] = g_hd2dOverflowProgs.back();
            g_hd2dOverflowProgs.pop_back();
            return;
        }
    }
}

static void WINAPI HookGlLinkProgram(uint32_t program) {
    tl_glCache.OnDeleteOrLinkProgram(program);
    const int64_t t0 = (g_inGameplay.load(std::memory_order_relaxed) != 0) ? NowQpc() : 0;
    if (g_origGlLinkProgram) {
        g_origGlLinkProgram(program);
    }
    if (t0 != 0) {
        const double us = static_cast<double>(NowQpc() - t0) * g_qpcToUs;
        if (us > 0.0 && us < 1000000.0) {
            g_frameShaderUs.fetch_add(static_cast<uint32_t>(us), std::memory_order_relaxed);
        }
    }

    constexpr uint32_t kGlLinkStatus = 0x8B82u;
    int32_t linkStatus = 0;
    bool linkedOk = false;
    if (program != 0 && g_fnGlGetProgramiv) {
        g_fnGlGetProgramiv(program, kGlLinkStatus, &linkStatus);
        linkedOk = (linkStatus != 0);
    }

    int32_t locP = -1, locS = -1, locT = -1;
    if (linkedOk && g_fnGlGetUniformLocation) {
        locP = g_fnGlGetUniformLocation(program, "HS_HD2D_PARAMS");
        locS = g_fnGlGetUniformLocation(program, "HS_HD2D_SUN");
        locT = g_fnGlGetUniformLocation(program, "HS_HD2D_TINT");
    }

    void* const curCtx = tl_glCache.curContext;
    bool newlyLinkedHd2d = false;
    AcquireSRWLockExclusive(&g_hd2dProgLock);
    Hd2dProgSlot& slot = UpsertProgSlotLocked(curCtx, program);
    const bool hadAny = slot.hasAny;
    slot.ctx       = curCtx;
    slot.checked   = true;
    slot.linkedOk  = linkedOk;
    slot.locParams = locP;
    slot.locSun    = locS;
    slot.locTint   = locT;
    slot.hasAny    = linkedOk && (locP >= 0 || locS >= 0 || locT >= 0);
    slot.lastGen   = 0;
    if (slot.hasAny && !hadAny) {
        newlyLinkedHd2d = true;
    } else if (!slot.hasAny && hadAny) {
        uint32_t cur = g_hd2dProgsLinked.load(std::memory_order_relaxed);
        while (cur > 0 && !g_hd2dProgsLinked.compare_exchange_weak(cur, cur - 1, std::memory_order_relaxed)) {}
    }
    ReleaseSRWLockExclusive(&g_hd2dProgLock);

    if (newlyLinkedHd2d) {
        const uint32_t total = g_hd2dProgsLinked.fetch_add(1, std::memory_order_relaxed) + 1;
        LogMsg("[VISUAL][HD2D] Linked HD-2D program #%u (ctx=%p prog=%u, locs=%d/%d/%d)",
               total, curCtx, program, locP, locS, locT);
    }
}

static void WINAPI HookGlUseProgram(uint32_t program) {
    void* const curCtx = tl_glCache.curContext;
    bool progLinkedOk = false;
    bool hasHd2d = false;
    int32_t locP = -1, locS = -1, locT = -1;
    uint32_t slotLastGen = 0;

    if (program > 0) {
        AcquireSRWLockShared(&g_hd2dProgLock);
        if (Hd2dProgSlot* slot = FindProgSlotLocked(curCtx, program)) {
            progLinkedOk = slot->linkedOk;
            hasHd2d      = slot->hasAny && slot->linkedOk;
            locP         = slot->locParams;
            locS         = slot->locSun;
            locT         = slot->locTint;
            slotLastGen  = slot->lastGen;
        }
        ReleaseSRWLockShared(&g_hd2dProgLock);
    }

    bool shouldCall = true;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        shouldCall = st.ShouldCallUseProgramChecked(program, progLinkedOk);
        FlushGlCacheStatsIfReady(st);
    }
    if (shouldCall && g_origGlUseProgram) {
        g_origGlUseProgram(program);
    }

    if (hasHd2d && g_fnGlUniform4fv) {
        const uint32_t curGen = g_hd2dStateGen.load(std::memory_order_acquire);
        if (slotLastGen != curGen) {
            Hd2dUniformState st{};
            AcquireSRWLockShared(&g_hd2dStateLock);
            st = g_hd2dState;
            ReleaseSRWLockShared(&g_hd2dStateLock);
            if (locP >= 0) {
                g_fnGlUniform4fv(locP, 1, st.params);
                tl_glCache.InvalidateUniform(program, locP);
            }
            if (locS >= 0) {
                g_fnGlUniform4fv(locS, 1, st.sun);
                tl_glCache.InvalidateUniform(program, locS);
            }
            if (locT >= 0) {
                g_fnGlUniform4fv(locT, 1, st.tint);
                tl_glCache.InvalidateUniform(program, locT);
            }
            AcquireSRWLockExclusive(&g_hd2dProgLock);
            if (Hd2dProgSlot* slot = FindProgSlotLocked(curCtx, program)) {
                slot->lastGen = curGen;
            }
            ReleaseSRWLockExclusive(&g_hd2dProgLock);
        }
    }
}

static void WINAPI HookGlDeleteProgram(uint32_t program) {
    tl_glCache.OnDeleteOrLinkProgram(program);
    AcquireSRWLockExclusive(&g_hd2dProgLock);
    EraseProgSlotLocked(tl_glCache.curContext, program);
    ReleaseSRWLockExclusive(&g_hd2dProgLock);
    if (g_origGlDeleteProgram) {
        g_origGlDeleteProgram(program);
    }
}

static thread_local double g_tlsLastPostProcUs = 0.0;

static void HookGameRender(void* self, void* renderData) {
    g_tlsLastPostProcUs = 0.0;
    const int64_t t0 = NowQpc();
    if (g_origGameRender) {
        g_origGameRender(self, renderData);
    }
    const double totalGameUs = static_cast<double>(NowQpc() - t0) * g_qpcToUs;
    const double worldUs = std::max(0.0, totalGameUs - g_tlsLastPostProcUs);
    g_lastWorldRenderUs.store(static_cast<uint32_t>(std::clamp(worldUs, 0.0, 1000000.0)), std::memory_order_relaxed);
    UpdateAtomicEma(g_emaWorldRenderUs, static_cast<float>(worldUs), 0.06f);
}

static void HookPostProcessorRender(void* pp, void* renderData, uint32_t primaryTexHandle) {
    const int64_t t0 = NowQpc();
    if (g_origPostProcessorRender) {
        g_origPostProcessorRender(pp, renderData, primaryTexHandle);
    }
    const double postUs = static_cast<double>(NowQpc() - t0) * g_qpcToUs;
    g_tlsLastPostProcUs = postUs;
    g_lastPostProcUs.store(static_cast<uint32_t>(std::clamp(postUs, 0.0, 1000000.0)), std::memory_order_relaxed);
    UpdateAtomicEma(g_emaPostProcUs, static_cast<float>(postUs), 0.06f);
}

static bool QueuePrologueTrampolineHook(
    std::vector<PendingCodePatch>& patches, uint8_t* fnAddr, size_t stealLen,
    const void* hookFn, void*& outOrigFn, const char* logText) {
    if (!fnAddr || stealLen < 14 || stealLen > 32) return false;
    uint8_t* tramp = AllocNearExecutable(fnAddr, 64);
    if (!tramp) return false;
    memcpy(tramp, fnAddr, stealLen);
    uint8_t jmpBack[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    const uint64_t resumeVa = reinterpret_cast<uint64_t>(fnAddr + stealLen);
    memcpy(jmpBack + 6, &resumeVa, 8);
    memcpy(tramp + stealLen, jmpBack, 14);
    FinalizeExecPageWX(tramp, stealLen + 14);
    outOrigFn = reinterpret_cast<void*>(tramp);

    PendingCodePatch p{};
    p.addr = fnAddr;
    p.len = stealLen;
    p.ownedExecPage = tramp;
    memset(p.bytes, 0x90, stealLen);
    uint8_t jmpHook[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    const uint64_t dst = reinterpret_cast<uint64_t>(hookFn);
    memcpy(jmpHook + 6, &dst, 8);
    memcpy(p.bytes, jmpHook, 14);
    p.logMsg = logText;
    patches.push_back(std::move(p));
    return true;
}

static void PrepareRenderPerfHooks(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    // 1. GameRender (unique 1-match masked AOB @ 0x140018890) -> World RenderThread ms telemetry
    constexpr uint8_t kSigGameRender[] = {
        0x48, 0x89, 0x54, 0x24, 0x10, 0x56, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x58, 0x02, 0x00, 0x00,
        0x48, 0x8B, 0xF1, 0x48, 0x8B, 0x89, 0x70, 0x01, 0x00, 0x00, 0x4C, 0x8B, 0xF2,
        0x48, 0x81, 0xC2, 0x88, 0x00, 0x00, 0x00, 0xE8, 0, 0, 0, 0, 0x48, 0x8B, 0x4E, 0x48
    };
    constexpr char kMaskGameRender[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxx";

    // 2. PostProcessor::Render (unique 1-match masked AOB @ 0x14001FB50) -> PostProcess ms telemetry
    constexpr uint8_t kSigPostProcRender[] = {
        0x4C, 0x8B, 0xDC, 0x45, 0x89, 0x43, 0x18, 0x49, 0x89, 0x53, 0x10, 0x56, 0x41, 0x54, 0x41, 0x55,
        0x48, 0x83, 0xEC, 0x70, 0x80, 0x3A, 0x00, 0x41, 0x8B, 0xF0, 0x4C, 0x8B, 0xEA, 0x4C, 0x8B, 0xE1,
        0x0F, 0x84, 0, 0, 0, 0, 0x48, 0x8B, 0x42, 0x38, 0x48, 0x2B, 0x42, 0x30
    };
    constexpr char kMaskPostProcRender[] = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx????xxxxxxxx";

    uint8_t* fnGR = FindUniqueMaskedPatternInText(textStart, textSize, kSigGameRender, kMaskGameRender, sizeof(kSigGameRender));
    uint8_t* fnPP = FindUniqueMaskedPatternInText(textStart, textSize, kSigPostProcRender, kMaskPostProcRender, sizeof(kSigPostProcRender));
    if (!fnGR || !fnPP) {
        LogMsg("[VISUAL][PERF] Render telemetry hooks skipped (Fail-Closed AOB miss: gr=%p pp=%p)",
               static_cast<void*>(fnGR), static_cast<void*>(fnPP));
        return;
    }

    void* origGR = nullptr;
    void* origPP = nullptr;
    char msgGR[224], msgPP[224];
    snprintf(msgGR, sizeof(msgGR), "[VISUAL][PERF] Hooked GameRender @ %p -> Live WorldRender ms telemetry", static_cast<void*>(fnGR));
    snprintf(msgPP, sizeof(msgPP), "[VISUAL][PERF] Hooked PostProcessor::Render @ %p -> Live PostProcess ms telemetry", static_cast<void*>(fnPP));

    const size_t grIdx = patches.size();
    const bool okGR = QueuePrologueTrampolineHook(patches, fnGR, 15, reinterpret_cast<const void*>(&HookGameRender), origGR, msgGR);
    const size_t ppIdx = patches.size();
    const bool okPP = okGR && QueuePrologueTrampolineHook(patches, fnPP, 16, reinterpret_cast<const void*>(&HookPostProcessorRender), origPP, msgPP);
    if (!okGR || !okPP) {
        while (patches.size() > grIdx) {
            if (patches.back().ownedExecPage) VirtualFree(patches.back().ownedExecPage, 0, MEM_RELEASE);
            patches.pop_back();
        }
        LogMsg("[VISUAL][PERF] Render telemetry hooks skipped (trampoline alloc failed)");
        return;
    }
    g_origGameRender          = reinterpret_cast<GameRender_t>(origGR);
    g_origPostProcessorRender = reinterpret_cast<PostProcessorRender_t>(origPP);
    g_gameRenderPatchIdx      = grIdx;
    g_postProcPatchIdx        = ppIdx;
}

static void HookLightMapRender(void* lightMap, uint8_t* renderData) {
    if (!g_origLightMapRender) return;
    static uint32_t s_appliedRebuildGen = 0;
    const uint32_t wantGen = g_lightmapRebuildGen.load(std::memory_order_acquire);
    if (renderData && wantGen != s_appliedRebuildGen) {
        s_appliedRebuildGen = wantGen;
        const uint8_t savedFlag = renderData[0];
        renderData[0] = 1; // Triggers Klei's native LightMap RT destroy + InitRenderTarget() rebuild!
        g_origLightMapRender(lightMap, renderData);
        renderData[0] = savedFlag;
        LogMsg("[VISUAL] Live 0ms LightMap RT hot-swapped -> %.1fx (no reload needed!)",
               g_lightmapScaleSlot ? static_cast<double>(*g_lightmapScaleSlot) : 0.5);
        return;
    }
    g_origLightMapRender(lightMap, renderData);
}

static void PrepareLightMap240AndHotSwapHooks(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    const uintptr_t textLo = reinterpret_cast<uintptr_t>(textStart);
    const uintptr_t textHi = textLo + textSize;

    // 1. Live 0ms 0.5x <-> 1.0x LightMap RT Hot-Swap (LightMap::Render @ 0x140026ca0, 18B RIP-free prologue)
    constexpr uint8_t kSigLightMapRender[44] = {
        0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x80, 0x3A, 0x00,
        0x48, 0x89, 0x5C, 0x24, 0x50, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xF9,
        0x0F, 0x84, 0x91, 0x00, 0x00, 0x00, 0x80, 0x79, 0x14, 0x00, 0x74, 0x40,
        0x8B, 0x51, 0x10, 0x83, 0xFA, 0xFF, 0x74, 0x16
    };
    if (uint8_t* fnLmR = FindUniquePatternInText(textStart, textSize, kSigLightMapRender, sizeof(kSigLightMapRender))) {
        void* origLmR = nullptr;
        char msg[224];
        snprintf(msg, sizeof(msg), "[VISUAL] Hooked LightMap::Render @ %p -> 0ms Live 0.5x<->1.0x LightMap RT Hot-Swap",
                 static_cast<void*>(fnLmR));
        const size_t hsIdx = patches.size();
        if (QueuePrologueTrampolineHook(patches, fnLmR, 18, reinterpret_cast<const void*>(&HookLightMapRender), origLmR, msg)) {
            g_origLightMapRender = reinterpret_cast<LightMapRender_t>(origLmR);
            g_lightmapHotSwapIdx = hsIdx;
        }
    } else {
        LogMsg("[VISUAL] Live LightMap Hot-Swap skipped (AOB miss; fail-closed)");
    }

    // 2. Pillar 1: 240Hz Dynamic Light Interpolation (LightMap::GenerateRenderData @ 0x140026640 -> call +17)
    constexpr uint8_t kSigLightWorldMtx[58] = {
        0x48, 0x8B, 0x2B, 0x80, 0x7D, 0x34, 0x00, 0x0F, 0x84, 0, 0, 0, 0,
        0x48, 0x8B, 0x4D, 0x18, 0xE8, 0, 0, 0, 0,
        0x48, 0x8D, 0x8C, 0x24, 0x88, 0x00, 0x00, 0x00,
        0x48, 0x8D, 0x54, 0x24, 0x70,
        0xF3, 0x0F, 0x10, 0x7D, 0x20,
        0xF3, 0x44, 0x0F, 0x10, 0x40, 0x0C,
        0xF3, 0x44, 0x0F, 0x10, 0x48, 0x1C,
        0xF3, 0x44, 0x0F, 0x10, 0x50, 0x2C
    };
    constexpr char kMaskLightWorldMtx[] = "xxxxxxxxx????xxxxx????xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    if (uint8_t* pLight = FindUniqueMaskedPatternInText(textStart, textSize, kSigLightWorldMtx, kMaskLightWorldMtx, sizeof(kSigLightWorldMtx))) {
        uint8_t* cs = pLight + 17;
        uint8_t* fn = ResolveRel32Call(cs);
        if (fn && reinterpret_cast<uintptr_t>(fn) >= textLo && reinterpret_cast<uintptr_t>(fn) < textHi) {
            g_origTransformGetWorldMatrix = reinterpret_cast<TransformGetWorldMatrix_t>(fn);
            char msg[224];
            snprintf(msg, sizeof(msg), "[LIGHT240] Hooked LightMap Dynamic Light Transform @ %p (fn=%p) -> True 240Hz Moving Light Halos",
                     static_cast<void*>(cs), static_cast<void*>(fn));
            const size_t lIdx = patches.size();
            if (QueueRel32NearRelayHook(patches, cs, reinterpret_cast<const void*>(&HookLightGetWorldMatrix), msg)) {
                g_light240PatchIdx = lIdx;
            }
        }
    } else {
        LogMsg("[LIGHT240] 240Hz Dynamic Light Interp skipped (AOB miss; fail-closed)");
    }
}

// Background-only DXGI Local VRAM Telemetry (runs 1x/sec on WorkerProc; 0 GameThread/RenderThread overhead)
static void SampleProcessVramDXGI() {
    static constexpr GUID kIidDxgiFactory1 = { 0x770aae78, 0xf26f, 0x4dba, { 0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87 } };
    static constexpr GUID kIidDxgiAdapter3 = { 0x645967a4, 0x1392, 0x4310, { 0xa7, 0x98, 0x80, 0x53, 0xce, 0x3e, 0x93, 0xfd } };
    static bool s_inited = false;
    static IDXGIFactory1* s_factory = nullptr;
    if (!s_inited) {
        s_inited = true;
        HMODULE hDxgi = LoadLibraryW(L"dxgi.dll");
        if (hDxgi) {
            auto fnCreate = reinterpret_cast<CreateDXGIFactory1_t>(GetProcAddress(hDxgi, "CreateDXGIFactory1"));
            if (fnCreate) {
                void* pFac = nullptr;
                if (SUCCEEDED(fnCreate(kIidDxgiFactory1, &pFac)) && pFac) {
                    s_factory = static_cast<IDXGIFactory1*>(pFac);
                }
            }
        }
    }
    if (!s_factory) return;

    UINT64 bestUsage = 0;
    UINT64 bestBudget = 0;
    for (UINT idx = 0; idx < 8; ++idx) {
        IDXGIAdapter1* adp1 = nullptr;
        if (FAILED(s_factory->EnumAdapters1(idx, &adp1)) || !adp1) break;
        void* pAdp3 = nullptr;
        if (SUCCEEDED(adp1->QueryInterface(kIidDxgiAdapter3, &pAdp3)) && pAdp3) {
            auto* adp3 = static_cast<IDXGIAdapter3*>(pAdp3);
            DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            if (SUCCEEDED(adp3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
                if (info.CurrentUsage > bestUsage || (bestUsage == 0 && info.Budget > bestBudget)) {
                    bestUsage = info.CurrentUsage;
                    bestBudget = info.Budget;
                }
            }
            adp3->Release();
        }
        adp1->Release();
    }
    if (bestUsage > 0 || bestBudget > 0) {
        const float curMb = static_cast<float>(static_cast<double>(bestUsage) / (1024.0 * 1024.0));
        const float budMb = static_cast<float>(static_cast<double>(bestBudget) / (1024.0 * 1024.0));
        g_processVramMb.store(curMb, std::memory_order_relaxed);
        g_processVramBudgetMb.store(budMb, std::memory_order_relaxed);
        float pk = g_peakVramMb.load(std::memory_order_relaxed);
        if (curMb > pk) g_peakVramMb.store(curMb, std::memory_order_relaxed);
    }
}

// Race-free IAT hook helper: publishes the original function pointer directly into the
// caller's global g_orig* variable with a release fence BEFORE atomically swapping the IAT slot!
template <typename FnT>
static bool PatchExeIatEntry(const char* dllName, uint16_t ordinal, const char* funcName, void* hookFn, FnT& outOrigGlobal) {
    void** iatSlot = FindExeIatSlot(dllName, ordinal, funcName);
    if (!iatSlot || !*iatSlot) return false;
    void* origVal = *iatSlot;
    if (origVal == hookFn) return true; // already hooked
    outOrigGlobal = reinterpret_cast<FnT>(origVal);
    std::atomic_thread_fence(std::memory_order_release);
    DWORD oldProt = 0;
    if (!VirtualProtect(iatSlot, sizeof(void*), PAGE_READWRITE, &oldProt)) {
        outOrigGlobal = nullptr;
        return false;
    }
    InterlockedExchangePointer(iatSlot, hookFn);
    DWORD dummy = 0;
    if (!VirtualProtect(iatSlot, sizeof(void*), oldProt, &dummy)) {
        LogMsg("[WARN] PatchExeIatEntry(%s!%s) failed restoring IAT protection 0x%lX",
               dllName, funcName ? funcName : "?", static_cast<unsigned long>(oldProt));
    }
    return true;
}

static void InstallGlIatHooks() {
    HMODULE hGles = GetModuleHandleW(L"libGLESv2.dll");
    if (hGles) {
        g_fnGlGetUniformLocation = reinterpret_cast<GlGetUniformLocation_t>(GetProcAddress(hGles, "glGetUniformLocation"));
        g_fnGlGetProgramiv       = reinterpret_cast<GlGetProgramiv_t>(GetProcAddress(hGles, "glGetProgramiv"));
        g_fnGlUniform4fv         = reinterpret_cast<GlUniform4fv_t>(GetProcAddress(hGles, "glUniform4fv"));
    }

    if (PatchExeIatEntry("libGLESv2.dll", 109, "glTexParameteri", reinterpret_cast<void*>(&HookGlTexParameteri), g_origGlTexParameteri)) {
        g_afIatHooked = true;
        LogMsg("[VISUAL] Hooked libGLESv2!glTexParameteri (@109) IAT -> 16x AF + Trilinear (target=%dx)",
               g_visualAnisoLevel.load(std::memory_order_relaxed));
    } else {
        LogMsg("[VISUAL] glTexParameteri (@109) IAT hook skipped (slot not found)");
    }

    bool okLnk = false, okUse = false, okDel = false;
    if (g_optHd2dWorldShaders.load(std::memory_order_relaxed) || g_optGlStateCache.load(std::memory_order_relaxed)) {
        bool okSrc = g_optHd2dWorldShaders.load(std::memory_order_relaxed)
            ? PatchExeIatEntry("libGLESv2.dll", 99,  "glShaderSource",  reinterpret_cast<void*>(&HookGlShaderSource),  g_origGlShaderSource)
            : false;
        okLnk = PatchExeIatEntry("libGLESv2.dll", 90,  "glLinkProgram",   reinterpret_cast<void*>(&HookGlLinkProgram),   g_origGlLinkProgram);
        okUse = PatchExeIatEntry("libGLESv2.dll", 131, "glUseProgram",    reinterpret_cast<void*>(&HookGlUseProgram),    g_origGlUseProgram);
        okDel = PatchExeIatEntry("libGLESv2.dll", 32,  "glDeleteProgram", reinterpret_cast<void*>(&HookGlDeleteProgram), g_origGlDeleteProgram);
        g_hd2dIatHooked = (okSrc && okLnk && okUse && okDel && g_fnGlGetUniformLocation && g_fnGlGetProgramiv && g_fnGlUniform4fv);
        LogMsg("[VISUAL][HD2D] libGLESv2 IAT hooks (src=%d lnk=%d use=%d del=%d loc=%d piv=%d uni=%d) -> active=%d",
               okSrc ? 1 : 0, okLnk ? 1 : 0, okUse ? 1 : 0, okDel ? 1 : 0,
               g_fnGlGetUniformLocation ? 1 : 0, g_fnGlGetProgramiv ? 1 : 0, g_fnGlUniform4fv ? 1 : 0,
               g_hd2dIatHooked ? 1 : 0);
    }

    // EGL Present Telemetry (eglSwapBuffers @32) + Context Reset Invalidation (eglMakeCurrent @25)
    {
        bool okMc = PatchExeIatEntry("libEGL.dll", 25, "eglMakeCurrent", reinterpret_cast<void*>(&HookEglMakeCurrent), g_origEglMakeCurrent);
        bool okSw = PatchExeIatEntry("libEGL.dll", 32, "eglSwapBuffers", reinterpret_cast<void*>(&HookEglSwapBuffers), g_origEglSwapBuffers);
        g_eglSwapIatHooked = okSw;
        LogMsg("[PERF][EGL] libEGL IAT hooks (makeCurrent=%d swapBuffers=%d)", okMc ? 1 : 0, okSw ? 1 : 0);

        // ANGLE 1.0 (D3D9) Redundant GL State-Mirror & Uniform Shadow Cache Deduplication
        // Fail-closed: ALL 11 core hooks (eglMakeCurrent + link/use/deleteProgram + 7 state hooks) MUST succeed!
        if (g_optGlStateCache.load(std::memory_order_relaxed) && okMc && okLnk && okUse && okDel) {
            bool okAt = PatchExeIatEntry("libGLESv2.dll",  1, "glActiveTexture",  reinterpret_cast<void*>(&HookGlActiveTexture),  g_origGlActiveTexture);
            bool okBt = PatchExeIatEntry("libGLESv2.dll",  7, "glBindTexture",    reinterpret_cast<void*>(&HookGlBindTexture),    g_origGlBindTexture);
            bool okDt = PatchExeIatEntry("libGLESv2.dll", 31, "glDeleteTextures", reinterpret_cast<void*>(&HookGlDeleteTextures), g_origGlDeleteTextures);
            bool okBf = PatchExeIatEntry("libGLESv2.dll", 11, "glBlendFunc",      reinterpret_cast<void*>(&HookGlBlendFunc),      g_origGlBlendFunc);
            bool okDm = PatchExeIatEntry("libGLESv2.dll", 37, "glDepthMask",      reinterpret_cast<void*>(&HookGlDepthMask),      g_origGlDepthMask);
            bool okEn = PatchExeIatEntry("libGLESv2.dll", 43, "glEnable",         reinterpret_cast<void*>(&HookGlEnable),         g_origGlEnable);
            bool okDi = PatchExeIatEntry("libGLESv2.dll", 39, "glDisable",        reinterpret_cast<void*>(&HookGlDisable),        g_origGlDisable);
            g_glCacheIatHooked = (okMc && okLnk && okUse && okDel &&
                                  okAt && okBt && okDt && okBf && okDm && okEn && okDi);
            g_glCacheActive.store(g_glCacheIatHooked, std::memory_order_release);
            if (!g_glCacheIatHooked) {
                g_optGlStateCache.store(false, std::memory_order_release);
            } else {
                // Extended buffer / vertex-attrib / raster state deduplication (fail-closed pairs)
                bool okBb = false, okDb = false;
                if (FindExeIatSlot("libGLESv2.dll", 4, "glBindBuffer") &&
                    FindExeIatSlot("libGLESv2.dll", 29, "glDeleteBuffers")) {
                    okBb = PatchExeIatEntry("libGLESv2.dll",  4, "glBindBuffer",    reinterpret_cast<void*>(&HookGlBindBuffer),    g_origGlBindBuffer);
                    okDb = PatchExeIatEntry("libGLESv2.dll", 29, "glDeleteBuffers", reinterpret_cast<void*>(&HookGlDeleteBuffers), g_origGlDeleteBuffers);
                }
                bool okEv = false, okDv = false;
                if (FindExeIatSlot("libGLESv2.dll", 44, "glEnableVertexAttribArray") &&
                    FindExeIatSlot("libGLESv2.dll", 40, "glDisableVertexAttribArray")) {
                    okEv = PatchExeIatEntry("libGLESv2.dll", 44, "glEnableVertexAttribArray",  reinterpret_cast<void*>(&HookGlEnableVertexAttribArray),  g_origGlEnableVertexAttribArray);
                    okDv = PatchExeIatEntry("libGLESv2.dll", 40, "glDisableVertexAttribArray", reinterpret_cast<void*>(&HookGlDisableVertexAttribArray), g_origGlDisableVertexAttribArray);
                }
                bool okBe = PatchExeIatEntry("libGLESv2.dll",  9, "glBlendEquation", reinterpret_cast<void*>(&HookGlBlendEquation), g_origGlBlendEquation);
                bool okCm = PatchExeIatEntry("libGLESv2.dll", 20, "glColorMask",     reinterpret_cast<void*>(&HookGlColorMask),     g_origGlColorMask);
                bool okCf = PatchExeIatEntry("libGLESv2.dll", 28, "glCullFace",      reinterpret_cast<void*>(&HookGlCullFace),      g_origGlCullFace);
                bool okDf = PatchExeIatEntry("libGLESv2.dll", 36, "glDepthFunc",     reinterpret_cast<void*>(&HookGlDepthFunc),     g_origGlDepthFunc);
                bool okFf = PatchExeIatEntry("libGLESv2.dll", 49, "glFrontFace",     reinterpret_cast<void*>(&HookGlFrontFace),     g_origGlFrontFace);
                bool okSc = PatchExeIatEntry("libGLESv2.dll", 97, "glScissor",       reinterpret_cast<void*>(&HookGlScissor),       g_origGlScissor);
                bool okVp = PatchExeIatEntry("libGLESv2.dll", 142,"glViewport",      reinterpret_cast<void*>(&HookGlViewport),      g_origGlViewport);

                bool okUni = false;
                if (g_optGlUniformCache.load(std::memory_order_relaxed)) {
                    bool u1f = PatchExeIatEntry("libGLESv2.dll", 112, "glUniform1f",        reinterpret_cast<void*>(&HookGlUniform1f),        g_origGlUniform1f);
                    bool u1i = PatchExeIatEntry("libGLESv2.dll", 114, "glUniform1i",        reinterpret_cast<void*>(&HookGlUniform1i),        g_origGlUniform1i);
                    bool u2  = PatchExeIatEntry("libGLESv2.dll", 117, "glUniform2fv",       reinterpret_cast<void*>(&HookGlUniform2fv),       g_origGlUniform2fv);
                    bool u3  = PatchExeIatEntry("libGLESv2.dll", 121, "glUniform3fv",       reinterpret_cast<void*>(&HookGlUniform3fv),       g_origGlUniform3fv);
                    bool u4  = PatchExeIatEntry("libGLESv2.dll", 125, "glUniform4fv",       reinterpret_cast<void*>(&HookGlUniform4fv),       g_origGlUniform4fv);
                    bool um3 = PatchExeIatEntry("libGLESv2.dll", 129, "glUniformMatrix3fv", reinterpret_cast<void*>(&HookGlUniformMatrix3fv), g_origGlUniformMatrix3fv);
                    bool um4 = PatchExeIatEntry("libGLESv2.dll", 130, "glUniformMatrix4fv", reinterpret_cast<void*>(&HookGlUniformMatrix4fv), g_origGlUniformMatrix4fv);
                    okUni = (u1f && u1i && u2 && u3 && u4 && um3 && um4);
                    g_glUniformCacheActive.store(okUni, std::memory_order_release);
                    if (!okUni) g_optGlUniformCache.store(false, std::memory_order_release);
                }
                LogMsg("[PERF][GLCACHE+] Extended state(buf=%d attr=%d eq=%d cm=%d cf=%d df=%d ff=%d sc=%d vp=%d) | UniformShadow=%d",
                       (okBb && okDb)?1:0, (okEv && okDv)?1:0, okBe?1:0, okCm?1:0, okCf?1:0, okDf?1:0, okFf?1:0, okSc?1:0, okVp?1:0, okUni?1:0);
            }
            LogMsg("[PERF][GLCACHE] ANGLE State-Mirror IAT hooks (mc=%d lnk=%d use=%d del=%d at=%d bt=%d dt=%d bf=%d dm=%d en=%d di=%d) -> active=%d",
                   okMc?1:0, okLnk?1:0, okUse?1:0, okDel?1:0,
                   okAt?1:0, okBt?1:0, okDt?1:0, okBf?1:0, okDm?1:0, okEn?1:0, okDi?1:0,
                   g_glCacheIatHooked?1:0);
        } else if (g_optGlStateCache.load(std::memory_order_relaxed)) {
            g_glCacheIatHooked = false;
            g_glCacheActive.store(false, std::memory_order_release);
            g_optGlStateCache.store(false, std::memory_order_release);
            LogMsg("[PERF][GLCACHE] Disabled fail-closed (missing prerequisite hooks: mc=%d lnk=%d use=%d del=%d)",
                   okMc?1:0, okLnk?1:0, okUse?1:0, okDel?1:0);
        }
    }
}

static DWORD WINAPI HookFsGetFileAttributesW(LPCWSTR lpFileName) {
    if (!g_origGetFileAttributesW) return INVALID_FILE_ATTRIBUTES;
    if (g_fsNegCacheActive.load(std::memory_order_acquire) &&
        g_optFsNegCache.load(std::memory_order_relaxed)) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPath(lpFileName, &h1, &h2)) {
            InterlockedIncrement64(&g_fsNegQueries);
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (g_fsNegCache.Lookup(h1, h2, now)) {
                InterlockedIncrement64(&g_fsNegHits);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return INVALID_FILE_ATTRIBUTES;
            }
            const DWORD res = g_origGetFileAttributesW(lpFileName);
            const DWORD err = GetLastError();
            if (res == INVALID_FILE_ATTRIBUTES &&
                (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)) {
                g_fsNegCache.Insert(h1, h2, now, static_cast<uint64_t>(g_qpcFreq) * 15ULL);
            }
            SetLastError(err);
            return res;
        }
    }
    return g_origGetFileAttributesW(lpFileName);
}

static BOOL WINAPI HookFsMoveFileExW(LPCWSTR a, LPCWSTR b, DWORD f) {
    g_fsNegCache.InvalidateAll();
    return g_origMoveFileExW ? g_origMoveFileExW(a, b, f) : FALSE;
}

static BOOL WINAPI HookFsCopyFileW(LPCWSTR a, LPCWSTR b, BOOL f) {
    g_fsNegCache.InvalidateAll();
    return g_origCopyFileW ? g_origCopyFileW(a, b, f) : FALSE;
}

static BOOL WINAPI HookFsDeleteFileW(LPCWSTR a) {
    g_fsNegCache.InvalidateAll();
    return g_origDeleteFileW ? g_origDeleteFileW(a) : FALSE;
}

static BOOL WINAPI HookFsCreateDirectoryW(LPCWSTR a, LPSECURITY_ATTRIBUTES sa) {
    g_fsNegCache.InvalidateAll();
    return g_origCreateDirectoryW ? g_origCreateDirectoryW(a, sa) : FALSE;
}

static void InstallFsNegCacheHooks() {
    if (!g_optFsNegCache.load(std::memory_order_relaxed)) return;
    // Fail-closed: require ALL 4 write-invalidation hooks before enabling negative stat cache!
    bool okMv = PatchExeIatEntry("KERNEL32.dll", 0, "MoveFileExW",      reinterpret_cast<void*>(&HookFsMoveFileExW),      g_origMoveFileExW);
    bool okCp = PatchExeIatEntry("KERNEL32.dll", 0, "CopyFileW",        reinterpret_cast<void*>(&HookFsCopyFileW),        g_origCopyFileW);
    bool okDl = PatchExeIatEntry("KERNEL32.dll", 0, "DeleteFileW",      reinterpret_cast<void*>(&HookFsDeleteFileW),      g_origDeleteFileW);
    bool okMd = PatchExeIatEntry("KERNEL32.dll", 0, "CreateDirectoryW", reinterpret_cast<void*>(&HookFsCreateDirectoryW), g_origCreateDirectoryW);
    bool okGa = false;
    if (okMv && okCp && okDl && okMd) {
        okGa = PatchExeIatEntry("KERNEL32.dll", 0, "GetFileAttributesW", reinterpret_cast<void*>(&HookFsGetFileAttributesW), g_origGetFileAttributesW);
    }
    const bool active = (okMv && okCp && okDl && okMd && okGa);
    g_fsNegCacheActive.store(active, std::memory_order_release);
    if (!active) g_optFsNegCache.store(false, std::memory_order_release);
    LogMsg("[PERF][FSNEG] KERNEL32 Mod/Data Negative Stat Cache (ga=%d mv=%d cp=%d dl=%d md=%d) -> active=%d",
           okGa?1:0, okMv?1:0, okCp?1:0, okDl?1:0, okMd?1:0, active?1:0);
}

int LuaHyperSmoothSetVisual(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    int aniso = static_cast<int>(g_luaToNumber(L, 1));
    if (aniso < 1) aniso = 1;
    if (aniso > 16) aniso = 16;
    bool fullLm = g_luaToBoolean ? (g_luaToBoolean(L, 2) != 0) : (aniso > 1);
    float mipBias = static_cast<float>(g_luaToNumber(L, 3)); // optional 3rd arg; nil/omitted -> 0.0f
    if (!(mipBias >= -0.75f) || mipBias > 0.0f) mipBias = 0.0f;
    int prevAf = g_visualAnisoLevel.exchange(aniso, std::memory_order_relaxed);
    bool prevLm = g_visualFullresLightmap.exchange(fullLm, std::memory_order_relaxed);
    float prevMip = g_visualMipLodBias.exchange(mipBias, std::memory_order_relaxed);
    if (g_lightmapScaleSlot) {
        *g_lightmapScaleSlot = fullLm ? 1.0f : 0.5f;
    }
    if (prevLm != fullLm) {
        g_lightmapRebuildGen.fetch_add(1, std::memory_order_release);
    }
    if (prevAf != aniso || prevLm != fullLm || fabsf(prevMip - mipBias) > 1e-4f) {
        LogMsg("[VISUAL] Lua Bridge -> AF=%dx, FullResLightmap=%d (liveSwap=%d), MipLodBias=%.2f",
               aniso, fullLm ? 1 : 0, g_lightmapHotSwapHooked.load(std::memory_order_relaxed) ? 1 : 0,
               static_cast<double>(mipBias));
    }
    g_luaPushBoolean(L, 1);
    return 1;
}

int LuaHyperSmoothGetVisualStats(void* L) {
    if (!g_luaPushNumber || !g_luaPushBoolean) return 0;
    g_luaPushNumber(L, static_cast<double>(g_visualAnisoLevel.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_hwMaxAniso.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_afAppliedHits, 0, 0)));
    g_luaPushBoolean(L, g_lightmapPatched ? 1 : 0);
    g_luaPushNumber(L, g_lightmapScaleSlot ? static_cast<double>(*g_lightmapScaleSlot) : 0.5);
    g_luaPushBoolean(L, g_angleSamplerHooked.load(std::memory_order_relaxed) ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(g_visualMipLodBias.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_mipBiasAppliedHits, 0, 0)));
    return 8;
}

int LuaHyperSmoothSetLowLatency(void* L) {
    if (!g_luaToBoolean || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    const bool on = (g_luaToBoolean(L, 1) != 0);
    const bool prevEx = g_optD3d9ExLowLatency.exchange(on, std::memory_order_relaxed);
    g_optLowLatencyGpuSync.store(on, std::memory_order_relaxed);
    if (prevEx != on) {
        g_d3d9ExLatGen.fetch_add(1, std::memory_order_release);
        LogMsg("[LATENCY] Lua Bridge -> LowLatency=%d (D3D9Ex MaxLatency=%s + GpuDrain=%d)",
               on ? 1 : 0, on ? "1" : "orig", on ? 1 : 0);
    }
    g_luaPushBoolean(L, g_angleSamplerHooked.load(std::memory_order_relaxed) ? 1 : 0);
    return 1;
}

int LuaHyperSmoothSetVfxSmooth(void* L) {
    if (!g_luaToBoolean || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    const LONG newVal = g_luaToBoolean(L, 1) ? 1 : 0;
    const LONG prev = g_vfxSmoothEnabled.exchange(newVal, std::memory_order_relaxed);
    if (prev != newVal) {
        LogMsg("[VFX240] Lua Bridge -> SmoothVfxParticles=%ld", newVal);
    }
    g_luaPushBoolean(L, g_vfxSmoothHooked.load(std::memory_order_relaxed) ? 1 : 0);
    return 1;
}

int LuaHyperSmoothGetLatencyStats(void* L) {
    if (!g_luaPushNumber || !g_luaPushBoolean) return 0;
    g_luaPushBoolean(L, g_d3d9ExActive.load(std::memory_order_relaxed) ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(g_d3d9ExCurLatency.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_d3d9ExOrigLatency.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_emaGpuDrainUs.load(std::memory_order_relaxed)) * 0.001);
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_gpuSyncDrains, 0, 0)));
    g_luaPushBoolean(L, g_vfxSmoothHooked.load(std::memory_order_relaxed) ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_vfxCmdsSmoothed, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_vfxParticlesSmoothed, 0, 0)));
    g_luaPushBoolean(L, g_angleSamplerHooked.load(std::memory_order_relaxed) ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(g_visualMipLodBias.load(std::memory_order_relaxed)));
    g_luaPushBoolean(L, g_lightPos240Hooked.load(std::memory_order_relaxed) ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_lightPos240Count, 0, 0)));
    g_luaPushBoolean(L, g_lightmapHotSwapHooked.load(std::memory_order_relaxed) ? 1 : 0);
    return 13;
}

int LuaHyperSmoothSetHd2d(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    auto clampF = [](float v, float lo, float hi) {
        if (!(v >= lo)) return lo;
        if (v > hi) return hi;
        return v;
    };
    Hd2dUniformState next{};
    next.params[0] = clampF(static_cast<float>(g_luaToNumber(L, 1)), 0.0f, 1.5f);  // contactAoStr (ANIM)
    next.params[1] = clampF(static_cast<float>(g_luaToNumber(L, 2)), 0.0f, 1.5f);  // calmEnvStr (OCEAN/GROUND)
    next.params[2] = clampF(static_cast<float>(g_luaToNumber(L, 3)), 0.0f, 1.5f);  // darkClarityStr (COLOURCUBE)
    next.params[3] = clampF(static_cast<float>(g_luaToNumber(L, 4)), 0.05f, 1.0f); // sunElev
    next.sun[0]    = clampF(static_cast<float>(g_luaToNumber(L, 5)), -1.0f, 1.0f); // sunWorldX
    next.sun[1]    = clampF(static_cast<float>(g_luaToNumber(L, 6)), -1.0f, 1.0f); // sunWorldZ
    next.sun[2]    = clampF(static_cast<float>(g_luaToNumber(L, 7)), -1.0f, 1.0f); // camViewX
    next.sun[3]    = clampF(static_cast<float>(g_luaToNumber(L, 8)), -1.0f, 1.0f); // camViewZ
    next.tint[0]   = clampF(static_cast<float>(g_luaToNumber(L, 9)),  0.0f, 2.0f); // tintR
    next.tint[1]   = clampF(static_cast<float>(g_luaToNumber(L, 10)), 0.0f, 2.0f); // tintG
    next.tint[2]   = clampF(static_cast<float>(g_luaToNumber(L, 11)), 0.0f, 2.0f); // tintB
    next.tint[3]   = clampF(static_cast<float>(g_luaToNumber(L, 12)), 0.0f, 1.0f); // rainWetness

    bool changed = false;
    AcquireSRWLockExclusive(&g_hd2dStateLock);
    for (int i = 0; i < 4; ++i) {
        if (fabsf(g_hd2dState.params[i] - next.params[i]) > 1e-4f ||
            fabsf(g_hd2dState.sun[i]    - next.sun[i])    > 1e-4f ||
            fabsf(g_hd2dState.tint[i]   - next.tint[i])   > 1e-4f) {
            changed = true;
            break;
        }
    }
    if (changed) {
        g_hd2dState = next;
    }
    ReleaseSRWLockExclusive(&g_hd2dStateLock);

    if (changed) {
        g_hd2dMasterStr.store(std::max({ next.params[0], next.params[1], next.params[2], next.sun[2] }), std::memory_order_relaxed);
        g_hd2dStateGen.fetch_add(1, std::memory_order_release);
    }
    g_luaPushBoolean(L, g_hd2dIatHooked ? 1 : 0);
    return 1;
}

int LuaHyperSmoothGetVisualPerf(void* L) {
    if (!g_luaPushNumber || !g_luaPushBoolean) return 0;
    const bool active = g_hd2dIatHooked && g_hd2dProgsLinked.load(std::memory_order_relaxed) > 0;
    g_luaPushBoolean(L, active ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(g_hd2dProgsLinked.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_hd2dMasterStr.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_emaWorldRenderUs.load(std::memory_order_relaxed)) * 0.001);
    g_luaPushNumber(L, static_cast<double>(g_emaPostProcUs.load(std::memory_order_relaxed)) * 0.001);
    g_luaPushNumber(L, static_cast<double>(g_emaFrameBusyUs.load(std::memory_order_relaxed)) * 0.001);
    g_luaPushNumber(L, static_cast<double>(g_emaGpuDrainUs.load(std::memory_order_relaxed)) * 0.001);
    g_luaPushNumber(L, static_cast<double>(g_processVramMb.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_peakVramMb.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_processVramBudgetMb.load(std::memory_order_relaxed)));
    return 10;
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

// =============================================================================
// [v2.6.0] Solo / Non-Caves Local Host Smoothness Engine:
//   1. SIMD libdeflate v1.23 zlib::compress2 (morning auto-save / minimap)
//   2. C++ Native FastDataDumper (155 MB/s cycle-safe save serializer)
//   3. O(N log N) Flat-Hash + Binary-Heap FastAStar PathFinder (25x faster walled bases)
// Zero overhead on multiplayer clients (only fires when client hosts Master locally!)
// =============================================================================
static uint8_t* FindUniqueAobInt16(uint8_t* textStart, size_t textSize, const int16_t* pat, size_t len) {
    if (!textStart || textSize < len) return nullptr;
    size_t firstFixed = 0;
    while (firstFixed < len && pat[firstFixed] < 0) ++firstFixed;
    if (firstFixed >= len) return nullptr;
    const uint8_t fb = static_cast<uint8_t>(pat[firstFixed]);
    uint8_t* hit = nullptr;
    const size_t limit = textSize - len;
    for (size_t i = 0; i <= limit; ++i) {
        if (textStart[i + firstFixed] != fb) continue;
        bool ok = true;
        for (size_t k = 0; k < len; ++k) {
            if (pat[k] >= 0 && textStart[i + k] != static_cast<uint8_t>(pat[k])) {
                ok = false;
                break;
            }
        }
        if (ok) {
            if (hit) return nullptr; // ambiguous (>1 match) -> fail-closed
            hit = textStart + i;
        }
    }
    return hit;
}

static uint8_t* BuildPlainRelocTrampoline(uint8_t* target, size_t stolenLen) {
    if (!target || stolenLen < 14) return nullptr;
    uint8_t* tramp = AllocNearExecutable(target, 64);
    if (!tramp) return nullptr;
    memcpy(tramp, target, stolenLen);
    uint8_t jmpBack[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint64_t resumeAddr = reinterpret_cast<uint64_t>(target + stolenLen);
    memcpy(jmpBack + 6, &resumeAddr, sizeof(resumeAddr));
    memcpy(tramp + stolenLen, jmpBack, 14);
    if (!FinalizeExecPageWX(tramp, stolenLen + 14)) {
        VirtualFree(tramp, 0, MEM_RELEASE);
        return nullptr;
    }
    return tramp;
}

// --- 1. Client libdeflate zlib::compress2 Hook ---
using ClientCompress2_t = int (*)(uint8_t* dest, uint32_t* destLen,
                                  const uint8_t* source, uint32_t sourceLen, int level);
ClientCompress2_t g_origClientCompress2 = nullptr;
std::atomic<bool> g_clientCompress2Hooked{false};
size_t g_clientComp2PatchIdx = static_cast<size_t>(-1);
volatile LONG64 g_clientDeflateCompCalls = 0;
volatile LONG64 g_clientDeflateCompInBytes = 0;
volatile LONG64 g_clientDeflateCompOutBytes = 0;
volatile LONG64 g_clientDeflateCompUs = 0;

struct ClientTlsDeflateComp {
    libdeflate_compressor* comp = nullptr;
    ~ClientTlsDeflateComp() {
        if (comp) libdeflate_free_compressor(comp);
    }
};
thread_local ClientTlsDeflateComp tl_clientDeflateComp;

static int HookClientCompress2(uint8_t* dest, uint32_t* destLen,
                               const uint8_t* source, uint32_t sourceLen, int level) {
    LARGE_INTEGER t0{}, t1{};
    QueryPerformanceCounter(&t0);
    InterlockedIncrement64(&g_clientDeflateCompCalls);
    InterlockedAdd64(&g_clientDeflateCompInBytes, static_cast<LONG64>(sourceLen));

    if (dest && destLen && source && *destLen > 0) {
        if (!tl_clientDeflateComp.comp) {
            tl_clientDeflateComp.comp = libdeflate_alloc_compressor(6);
        }
        if (tl_clientDeflateComp.comp) {
            const size_t outSz = libdeflate_zlib_compress(
                tl_clientDeflateComp.comp, source, sourceLen, dest, *destLen);
            if (outSz > 0 && outSz <= *destLen) {
                *destLen = static_cast<uint32_t>(outSz);
                InterlockedAdd64(&g_clientDeflateCompOutBytes, static_cast<LONG64>(outSz));
                QueryPerformanceCounter(&t1);
                const LONG64 us = static_cast<LONG64>(
                    static_cast<double>(t1.QuadPart - t0.QuadPart) * g_qpcInvFreq * 1000000.0);
                InterlockedAdd64(&g_clientDeflateCompUs, us);
                return 0; // Z_OK
            }
        }
    }

    int rc = g_origClientCompress2 ? g_origClientCompress2(dest, destLen, source, sourceLen, level) : -1;
    if (rc == 0 && destLen) {
        InterlockedAdd64(&g_clientDeflateCompOutBytes, static_cast<LONG64>(*destLen));
    }
    QueryPerformanceCounter(&t1);
    const LONG64 us = static_cast<LONG64>(
        static_cast<double>(t1.QuadPart - t0.QuadPart) * g_qpcInvFreq * 1000000.0);
    InterlockedAdd64(&g_clientDeflateCompUs, us);
    return rc;
}

static void PrepareClientCompress2Hook(std::vector<PendingCodePatch>& patches,
                                       uint8_t* textStart, size_t textSize) {
    static constexpr int16_t kAobClientCompress2[] = {
        0x4C, 0x8B, 0xDC, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x8B, 0x02, 0x4D, 0x89, 0x43,
        0x98, 0x45, 0x89, 0x4B, 0xA0, 0x49, 0x89, 0x4B, 0xA8, 0x89, 0x44, 0x24, 0x38, 0x33, 0xC0, 0x48,
        0x8B, 0xFA, 0x8B, 0x94, 0x24, 0xB0, 0x00, 0x00, 0x00, 0x4C, 0x8D, 0x05
    };
    uint8_t* pComp2 = FindUniqueAobInt16(textStart, textSize, kAobClientCompress2,
                                         sizeof(kAobClientCompress2) / sizeof(kAobClientCompress2[0]));
    if (!pComp2) {
        LogMsg("[HOST] Client zlib::compress2 skipped (AOB miss; fail-closed)");
        return;
    }
    uint8_t* tramp = BuildPlainRelocTrampoline(pComp2, 17);
    if (!tramp) return;
    g_origClientCompress2 = reinterpret_cast<ClientCompress2_t>(tramp);

    PendingCodePatch p{};
    p.addr = pComp2;
    p.len = 17;
    p.ownedExecPage = tramp;
    uint8_t jmpHook[17] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0,0,0,0,0,0,0,0, 0x90, 0x90, 0x90 };
    uint64_t dst = reinterpret_cast<uint64_t>(&HookClientCompress2);
    memcpy(jmpHook + 6, &dst, sizeof(dst));
    memcpy(p.bytes, jmpHook, 17);
    char msg[200];
    snprintf(msg, sizeof(msg), "[HOST] Hooked Client zlib::compress2 @ %p -> SIMD libdeflate v1.23 (L6)",
             static_cast<void*>(pComp2));
    p.logMsg = msg;
    g_clientComp2PatchIdx = patches.size();
    patches.push_back(std::move(p));
}

// --- 2. Client O(N log N) FastAStar PathFinder Hook (Solo / Local Host) ---
using ClientAStarReset_t = void (*)(void* astar);
using ClientAStarInit_t  = void (*)(void* astar, void* ctx);
using ClientAStarStep_t  = uint32_t (*)(void* astar, uint32_t maxSteps);

ClientAStarReset_t g_origClientAStarReset = nullptr;
ClientAStarInit_t  g_origClientAStarInit  = nullptr;
ClientAStarStep_t  g_origClientAStarStep  = nullptr;
fast_astar::EngineCallbacks g_clientAStarCb{};
std::atomic<bool> g_clientAStarReady{false};
size_t g_clientAStarPatchIdx[3] = {
    static_cast<size_t>(-1), static_cast<size_t>(-1), static_cast<size_t>(-1)
};

volatile LONG64 g_clientAStarInits = 0;
volatile LONG64 g_clientAStarSteps = 0;
volatile LONG64 g_clientAStarCompleted = 0;
volatile LONG64 g_clientAStarFailed = 0;
volatile LONG64 g_clientAStarFallbacks = 0;
volatile LONG64 g_clientAStarUs = 0;

static void ClearClientKleiPathVec(void* pathVec) {
    if (!pathVec) return;
    auto* pv = reinterpret_cast<uint8_t*>(pathVec);
    *reinterpret_cast<void**>(pv + 0x10) = *reinterpret_cast<void**>(pv + 0x08);
}

fast_astar::StateManager<48u> g_clientAStarMgr;

static void HookClientAStarReset(void* astar) {
    if (astar) {
        g_clientAStarMgr.DetachAndRecycle(astar);
    }
    if (g_origClientAStarReset) {
        g_origClientAStarReset(astar);
    }
}

static void HookClientAStarInit(void* astar, void* ctx) {
    if (!g_clientAStarReady.load(std::memory_order_acquire) || !astar || !ctx) {
        if (astar) g_clientAStarMgr.DetachAndRecycle(astar);
        if (g_origClientAStarInit) g_origClientAStarInit(astar, ctx);
        return;
    }

    if (g_origClientAStarReset) g_origClientAStarReset(astar);
    auto* ab = reinterpret_cast<uint8_t*>(astar);
    *reinterpret_cast<void**>(ab + 0x50)   = ctx;
    *reinterpret_cast<float*>(ab + 0x78)   = 0.0f;
    *reinterpret_cast<int32_t*>(ab + 0x08) = 0;

    auto handle = g_clientAStarMgr.AcquireAndBind(astar);
    if (!handle) {
        InterlockedIncrement64(&g_clientAStarFallbacks);
        if (g_origClientAStarInit) g_origClientAStarInit(astar, ctx);
        return;
    }

    const auto* cb = reinterpret_cast<const uint8_t*>(ctx);
    fast_astar::TileCoord start{};
    memcpy(&start, cb + 0x30, sizeof(fast_astar::TileCoord));
    const float h = g_clientAStarCb.heuristic(ctx, &start);
    handle->Init(start, h);
    InterlockedIncrement64(&g_clientAStarInits);
}

static uint32_t HookClientAStarStep(void* astar, uint32_t maxSteps) {
    if (!astar) return 0u;
    auto* ab = reinterpret_cast<uint8_t*>(astar);
    if (*reinterpret_cast<int32_t*>(ab + 0x08) != 0) {
        return 0u;
    }

    auto handle = g_clientAStarMgr.AcquireRef(astar);
    if (!handle || !g_clientAStarReady.load(std::memory_order_acquire)) {
        handle.Release();
        InterlockedIncrement64(&g_clientAStarFallbacks);
        return g_origClientAStarStep ? g_origClientAStarStep(astar, maxSteps) : 0u;
    }

    auto* slotPtr = handle.Slot();
    const uint64_t slotGen = handle.Gen();
    void* ctx = *reinterpret_cast<void**>(ab + 0x50);
    if (!ctx) {
        handle.Release();
        g_clientAStarMgr.DetachIfMatch(astar, slotPtr, slotGen);
        *reinterpret_cast<int32_t*>(ab + 0x08) = 2;
        return 0u;
    }

    fast_astar::TileCoord goal{};
    memcpy(&goal, reinterpret_cast<const uint8_t*>(ctx) + 0x3C, sizeof(fast_astar::TileCoord));

    LARGE_INTEGER t0{}, t1{};
    QueryPerformanceCounter(&t0);
    int32_t outStatus = 0;
    const uint32_t steps = handle->Step(ctx, goal, maxSteps, g_clientAStarCb, ab + 0x58, outStatus);
    QueryPerformanceCounter(&t1);
    const double us = static_cast<double>(t1.QuadPart - t0.QuadPart) * g_qpcInvFreq * 1000000.0;

    *reinterpret_cast<float*>(ab + 0x78) += static_cast<float>(us * 1e-6);
    InterlockedAdd64(&g_clientAStarSteps, static_cast<LONG64>(steps));
    InterlockedAdd64(&g_clientAStarUs, static_cast<LONG64>(us));

    if (outStatus != 0) {
        *reinterpret_cast<int32_t*>(ab + 0x08) = outStatus;
        handle.Release();
        g_clientAStarMgr.DetachIfMatch(astar, slotPtr, slotGen);
        if (outStatus == 1) {
            InterlockedIncrement64(&g_clientAStarCompleted);
        } else {
            InterlockedIncrement64(&g_clientAStarFailed);
        }
    }
    return steps;
}

static void PrepareClientFastAStarHooks(std::vector<PendingCodePatch>& patches,
                                        uint8_t* textStart, size_t textSize) {
    static constexpr int16_t kAobClientAStarReset[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x79, 0x40, 0x48, 0x8B, 0x59, 0x38, 0x33, 0xED, 0x48, 0x8B
    };
    static constexpr int16_t kAobClientAStarInit[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48,
        0x8B, 0xDA, 0x48, 0x8B, 0xF1, 0xE8, -1, -1, -1, -1, 0x33, 0xFF, 0x48, 0x89, 0x5E, 0x50
    };
    static constexpr int16_t kAobClientAStarStep[] = {
        0x89, 0x54, 0x24, 0x10, 0x57, 0x41, 0x54, 0x48, 0x81, 0xEC, 0xB8, 0x00, 0x00, 0x00, 0x83, 0x79,
        0x08, 0x00, 0x8B, 0xFA, 0x4C, 0x8B, 0xE1, 0x74, 0x0D, 0x33, 0xC0, 0x48, 0x81, 0xC4, 0xB8, 0x00
    };

    uint8_t* pReset = FindUniqueAobInt16(textStart, textSize, kAobClientAStarReset,
                                         sizeof(kAobClientAStarReset) / sizeof(kAobClientAStarReset[0]));
    uint8_t* pInit  = FindUniqueAobInt16(textStart, textSize, kAobClientAStarInit,
                                         sizeof(kAobClientAStarInit) / sizeof(kAobClientAStarInit[0]));
    uint8_t* pStep  = FindUniqueAobInt16(textStart, textSize, kAobClientAStarStep,
                                         sizeof(kAobClientAStarStep) / sizeof(kAobClientAStarStep[0]));
    if (!pReset || !pInit || !pStep) {
        LogMsg("[HOST] Client FastAStar skipped (AOB miss: r=%p i=%p s=%p)", pReset, pInit, pStep);
        return;
    }

    auto inText = [&](uint8_t* ptr) -> bool {
        return ptr && ptr >= textStart && ptr < (textStart + textSize);
    };
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
                        inText(pReserve) && inText(pNbType2) && inText(pNbNorm) &&
                        inText(pHeur1) && inText(pPushPath) && inText(pFreeMem);
    if (!xcheck) {
        LogMsg("[HOST] Client FastAStar skipped (Fail-Closed cross-check failed: ir=%p sr=%p h1=%p h2=%p)",
               initReset, stepReset, pHeur1, pHeur2);
        return;
    }

    uint8_t* trReset = BuildPlainRelocTrampoline(pReset, 15);
    uint8_t* trInit  = trReset ? BuildPlainRelocTrampoline(pInit, 15) : nullptr;
    uint8_t* trStep  = trInit  ? BuildPlainRelocTrampoline(pStep, 14) : nullptr;
    if (!trReset || !trInit || !trStep) {
        if (trReset) VirtualFree(trReset, 0, MEM_RELEASE);
        if (trInit)  VirtualFree(trInit, 0, MEM_RELEASE);
        if (trStep)  VirtualFree(trStep, 0, MEM_RELEASE);
        return;
    }

    g_clientAStarCb.reserveNeighbors   = reinterpret_cast<decltype(g_clientAStarCb.reserveNeighbors)>(pReserve);
    g_clientAStarCb.getNeighborsNormal = reinterpret_cast<decltype(g_clientAStarCb.getNeighborsNormal)>(pNbNorm);
    g_clientAStarCb.getNeighborsType2  = reinterpret_cast<decltype(g_clientAStarCb.getNeighborsType2)>(pNbType2);
    g_clientAStarCb.heuristic          = reinterpret_cast<decltype(g_clientAStarCb.heuristic)>(pHeur1);
    g_clientAStarCb.clearPath          = &ClearClientKleiPathVec;
    g_clientAStarCb.pushPathNode       = reinterpret_cast<decltype(g_clientAStarCb.pushPathNode)>(pPushPath);
    g_clientAStarCb.freeMem            = reinterpret_cast<decltype(g_clientAStarCb.freeMem)>(pFreeMem);

    g_origClientAStarReset = reinterpret_cast<ClientAStarReset_t>(trReset);
    g_origClientAStarInit  = reinterpret_cast<ClientAStarInit_t>(trInit);
    g_origClientAStarStep  = reinterpret_cast<ClientAStarStep_t>(trStep);

    auto pushHook = [&](int slot, uint8_t* addr, size_t len, void* detour, uint8_t* tramp, const char* msg) {
        PendingCodePatch p{};
        p.addr = addr;
        p.len = len;
        p.ownedExecPage = tramp;
        memset(p.bytes, 0x90, len);
        p.bytes[0] = 0xFF; p.bytes[1] = 0x25;
        uint32_t zero = 0; memcpy(p.bytes + 2, &zero, 4);
        uint64_t dst = reinterpret_cast<uint64_t>(detour);
        memcpy(p.bytes + 6, &dst, sizeof(dst));
        p.logMsg = msg;
        g_clientAStarPatchIdx[slot] = patches.size();
        patches.push_back(std::move(p));
    };

    char msg[240];
    snprintf(msg, sizeof(msg), "[HOST] Hooked Client FastAStar O(N log N) @ (Reset=%p Init=%p Step=%p)",
             static_cast<void*>(pReset), static_cast<void*>(pInit), static_cast<void*>(pStep));
    pushHook(0, pReset, 15, reinterpret_cast<void*>(&HookClientAStarReset), trReset, "");
    pushHook(1, pInit,  15, reinterpret_cast<void*>(&HookClientAStarInit),  trInit,  "");
    pushHook(2, pStep,  14, reinterpret_cast<void*>(&HookClientAStarStep),  trStep,  msg);
}

// --- 3. Client C++ Native FastDataDumper (Solo / Non-Caves Morning Auto-Save) ---
LuaApiTable g_clientLuaApi{};
thread_local fast_dumper::GrowBuf tl_clientDumpBuf;
volatile LONG64 g_clientFastDumpCalls = 0;
volatile LONG64 g_clientFastDumpFallbacks = 0;
volatile LONG64 g_clientFastDumpBytes = 0;
volatile LONG64 g_clientFastDumpUs = 0;

static int LuaHyperSmoothFastDump(void* L) {
    if (!g_clientLuaApi.ReadyForFastDump()) {
        if (g_clientLuaApi.pushnil && g_clientLuaApi.pushstring) {
            g_clientLuaApi.pushnil(L);
            g_clientLuaApi.pushstring(L, "unavailable");
            return 2;
        }
        return 0;
    }
    LARGE_INTEGER t0{}, t1{};
    QueryPerformanceCounter(&t0);
    const int nargs = g_clientLuaApi.gettop(L);
    if (nargs < 1) {
        g_clientLuaApi.pushnil(L);
        g_clientLuaApi.pushstring(L, "no_arg");
        return 2;
    }

    const char* varname = nullptr;
    size_t varlen = 0;
    if (nargs >= 2 && g_clientLuaApi.type(L, 2) == HC_LUA_TSTRING) {
        varname = g_clientLuaApi.tolstring(L, 2, &varlen);
    }

    const char* failReason = nullptr;
    const auto st = fast_dumper::DumpRoot(L, g_clientLuaApi, 1, varname, varlen, tl_clientDumpBuf, &failReason);
    QueryPerformanceCounter(&t1);
    const LONG64 us = static_cast<LONG64>(
        static_cast<double>(t1.QuadPart - t0.QuadPart) * g_qpcInvFreq * 1000000.0);

    if (st == fast_dumper::Status::Ok && !tl_clientDumpBuf.oom && tl_clientDumpBuf.data) {
        InterlockedIncrement64(&g_clientFastDumpCalls);
        InterlockedAdd64(&g_clientFastDumpUs, us);
        InterlockedAdd64(&g_clientFastDumpBytes, static_cast<LONG64>(tl_clientDumpBuf.len));
        g_clientLuaApi.pushlstring(L, tl_clientDumpBuf.data, tl_clientDumpBuf.len);
        tl_clientDumpBuf.ShrinkIfLarge();
        return 1;
    }

    tl_clientDumpBuf.ShrinkIfLarge();
    InterlockedIncrement64(&g_clientFastDumpFallbacks);
    g_clientLuaApi.pushnil(L);
    g_clientLuaApi.pushstring(L, failReason ? failReason : "fallback");
    return 2;
}

static int LuaHyperSmoothGetHostPerf(void* L) {
    if (!g_luaPushNumber || !g_luaPushBoolean) return 0;
    g_luaPushBoolean(L, g_clientLuaApi.ReadyForFastDump() ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_clientFastDumpCalls, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_clientFastDumpUs, 0, 0)) * 0.001);
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_clientFastDumpBytes, 0, 0)) / (1024.0 * 1024.0));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_clientFastDumpFallbacks, 0, 0)));
    g_luaPushBoolean(L, g_clientCompress2Hooked.load(std::memory_order_relaxed) ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_clientDeflateCompCalls, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_clientDeflateCompInBytes, 0, 0)) / (1024.0 * 1024.0));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_clientDeflateCompUs, 0, 0)) * 0.001);
    g_luaPushBoolean(L, g_clientAStarReady.load(std::memory_order_relaxed) ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_clientAStarInits, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_clientAStarSteps, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_clientAStarUs, 0, 0)) * 0.001);
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_clientAStarFallbacks, 0, 0)));
    return 14;
}

int LuaHyperSmoothGetRenderOptStats(void* L) {
    if (!g_luaPushNumber) return 0;
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_glCacheCalls, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_glCacheHits, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_glUniCalls, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_glUniHits, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_fsNegQueries, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_fsNegHits, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(InterlockedCompareExchange64(&g_deJitterHits, 0, 0)));
    g_luaPushNumber(L, static_cast<double>(g_deJitterPeakUs.load(std::memory_order_relaxed)) * 0.001);
    return 8;
}

int LuaHyperSmoothVersion(void* L) {
    if (!g_luaPushString) return 0;
    g_luaPushString(L, "DST-HyperSmooth/2.6.0");
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
    reg("HyperSmooth_SetVfxSmooth", &LuaHyperSmoothSetVfxSmooth);
    reg("HyperSmooth_SetLowLatency", &LuaHyperSmoothSetLowLatency);
    reg("HyperSmooth_GetStatus", &LuaHyperSmoothGetStatus);
    reg("HyperSmooth_GetFrameStats", &LuaHyperSmoothGetFrameStats);
    reg("HyperSmooth_GetLatencyStats", &LuaHyperSmoothGetLatencyStats);
    reg("HyperSmooth_GetRenderOptStats", &LuaHyperSmoothGetRenderOptStats);
    reg("HyperSmooth_SetSimTickRate", &LuaHyperSmoothSetSimTickRate);
    reg("HyperSmooth_GetSimTickRate", &LuaHyperSmoothGetSimTickRate);
    reg("HyperSmooth_SetVisual", &LuaHyperSmoothSetVisual);
    reg("HyperSmooth_GetVisualStats", &LuaHyperSmoothGetVisualStats);
    reg("HyperSmooth_SetHd2d", &LuaHyperSmoothSetHd2d);
    reg("HyperSmooth_GetVisualPerf", &LuaHyperSmoothGetVisualPerf);
    reg("HyperSmooth_GetHostPerf", &LuaHyperSmoothGetHostPerf);
    if (g_clientLuaApi.ReadyForFastDump()) {
        reg("HyperSmooth_FastDump", &LuaHyperSmoothFastDump);
    }
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
    if (g_clientSimTickRate.load(std::memory_order_acquire) != 30) {
        if (!SetClientSimTickRate(30)) {
            LogMsg("[ERROR][SIMTICK] Failed restoring Client SimTick to 30Hz on new Lua VM init (cur=%dHz)!",
                   g_clientSimTickRate.load(std::memory_order_acquire));
        }
    }
    // Player-First safety: reset world-shader strengths & Mip LOD Bias to 0.0 (Vanilla Pure) on fresh Lua VM
    // so disabling the Workshop Mod never leaks visual state!
    {
        AcquireSRWLockExclusive(&g_hd2dStateLock);
        g_hd2dState.params[0] = 0.0f;
        g_hd2dState.params[1] = 0.0f;
        g_hd2dState.params[2] = 0.0f;
        g_hd2dState.sun[2]    = 0.0f;
        ReleaseSRWLockExclusive(&g_hd2dStateLock);
        g_hd2dMasterStr.store(0.0f, std::memory_order_relaxed);
        g_hd2dStateGen.fetch_add(1, std::memory_order_release);
        g_visualMipLodBias.store(0.0f, std::memory_order_relaxed);
    }
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

    // Populate LuaApiTable for C++ Native FastDataDumper (all verified unique 1-match AOBs on Client EXE!)
    static constexpr int16_t kAobCLuaCheckStack[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x81, 0xFA, 0x40, 0x1F, 0x00, 0x00,
        0x48, 0x8B, 0xD9, 0x7F, 0x75, 0x4C, 0x8B, 0x41, 0x10, 0x48, 0x63, 0xFA, 0x49, 0x8B, 0xC0, 0x48
    };
    static constexpr int16_t kAobCLuaGetTop[] = {
        0x48, 0x8B, 0x41, 0x10, 0x48, 0x2B, 0x41, 0x18, 0x48, 0xC1, 0xF8, 0x04, 0xC3
    };
    static constexpr int16_t kAobCLuaSetTop[] = {
        0x4C, 0x8B, 0xC1, 0x85, 0xD2, 0x78, 0x40, 0x48, 0x8B, 0x41, 0x18, 0x4C, 0x63, 0xCA, 0x4D, 0x03,
        0xC9, 0x4A, 0x8D, 0x14, 0xC8, 0x48, 0x39, 0x51, 0x10, 0x73, 0x1F, 0x33, 0xD2, 0x0F, 0x1F, 0x00
    };
    static constexpr int16_t kAobCLuaPushValue[] = {
        0x48, 0x83, 0xEC, 0x28, 0x4C, 0x8B, 0xD1, 0xE8, -1, -1, -1, -1, 0x4D, 0x8B, 0x42, 0x10,
        0x48, 0x8B, 0x10, 0x49, 0x89, 0x10, 0x8B, 0x40, 0x08, 0x41, 0x89, 0x40, 0x08, 0x49, 0x83, 0x42
    };
    static constexpr int16_t kAobCLuaType[] = {
        0x48, 0x83, 0xEC, 0x28, 0xE8, -1, -1, -1, -1, 0x48, 0x8D, 0x0D, -1, -1, -1, -1,
        0x48, 0x3B, 0xC1, 0x75, 0x08, 0x83, 0xC8, 0xFF, 0x48, 0x83, 0xC4, 0x28, 0xC3, 0x8B, 0x40, 0x08
    };
    static constexpr int16_t kAobCLuaToLString[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x49,
        0x8B, 0xD8, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0xE8, -1, -1, -1, -1, 0x4C, 0x8B, 0xD0, 0x83, 0x78, 0x08, 0x04, 0x74
    };
    static constexpr int16_t kAobCLuaToPointer[] = {
        0x48, 0x83, 0xEC, 0x28, 0x44, 0x8B, 0xD2, 0x4C, 0x8B, 0xD9, 0xE8, -1, -1, -1, -1, 0x44,
        0x8B, 0x48, 0x08, 0x41, 0x83, 0xE9, 0x02, 0x74, 0x1F, 0x41, 0x83, 0xE9, 0x03, 0x74, 0x3D,
        0x41, 0x83, 0xE9, 0x01, 0x74, 0x37, 0x41, 0x83, 0xE9, 0x01, 0x74, 0x0D
    };
    static constexpr int16_t kAobCLuaPushNil[] = {
        0x48, 0x8B, 0x41, 0x10, 0xC7, 0x40, 0x08, 0x00, 0x00, 0x00, 0x00, 0x48, 0x83, 0x41, 0x10, 0x10, 0xC3
    };
    static constexpr int16_t kAobCLuaPushLString[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x48, 0x83, 0xEC, 0x20, 0x80, 0xB9, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xF0, 0x48, 0x8B, 0xEA, 0x48, 0x8B, 0xF9
    };
    static constexpr int16_t kAobCLuaNext[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, -1, -1, -1, -1, 0x4C, 0x8B,
        0x43, 0x10, 0x48, 0x8B, 0x10, 0x49, 0x83, 0xE8, 0x10, 0x48, 0x8B, 0xCB, 0xE8, -1, -1, -1, -1, 0x85, 0xC0, 0x74
    };
    auto findU16 = [&](const auto& arr) -> uint8_t* {
        return FindUniqueAobInt16(textStart, textSize, arr, sizeof(arr) / sizeof(arr[0]));
    };
    g_clientLuaApi.checkstack   = reinterpret_cast<decltype(g_clientLuaApi.checkstack)>(findU16(kAobCLuaCheckStack));
    g_clientLuaApi.gettop       = reinterpret_cast<decltype(g_clientLuaApi.gettop)>(findU16(kAobCLuaGetTop));
    g_clientLuaApi.settop       = reinterpret_cast<decltype(g_clientLuaApi.settop)>(findU16(kAobCLuaSetTop));
    g_clientLuaApi.pushvalue    = reinterpret_cast<decltype(g_clientLuaApi.pushvalue)>(findU16(kAobCLuaPushValue));
    g_clientLuaApi.type         = reinterpret_cast<decltype(g_clientLuaApi.type)>(findU16(kAobCLuaType));
    g_clientLuaApi.tonumber     = g_luaToNumber;
    g_clientLuaApi.toboolean    = g_luaToBoolean;
    g_clientLuaApi.tolstring    = reinterpret_cast<decltype(g_clientLuaApi.tolstring)>(findU16(kAobCLuaToLString));
    g_clientLuaApi.topointer    = reinterpret_cast<decltype(g_clientLuaApi.topointer)>(findU16(kAobCLuaToPointer));
    g_clientLuaApi.pushnil      = reinterpret_cast<decltype(g_clientLuaApi.pushnil)>(findU16(kAobCLuaPushNil));
    g_clientLuaApi.pushnumber   = g_luaPushNumber;
    g_clientLuaApi.pushlstring  = reinterpret_cast<decltype(g_clientLuaApi.pushlstring)>(findU16(kAobCLuaPushLString));
    g_clientLuaApi.pushstring   = g_luaPushString;
    g_clientLuaApi.pushcclosure = g_luaPushCClosure;
    g_clientLuaApi.pushboolean  = g_luaPushBoolean;
    g_clientLuaApi.setfield     = g_luaSetField;
    g_clientLuaApi.next         = reinterpret_cast<decltype(g_clientLuaApi.next)>(findU16(kAobCLuaNext));
    LogMsg("[HOST] Client FastDataDumper C++ Lua API ready=%d", g_clientLuaApi.ReadyForFastDump() ? 1 : 0);

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
        "; DST HyperSmooth & FastLoad Native Companion Plugin Configuration (v2.6.0)\n"
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
        "smooth_vfx_particles=1\n"
        "world_pos_interp=1\n"
        "raknet_zero_stall=1\n"
        "d3d9ex_low_latency=1\n"
        "low_latency_gpu_sync=1\n"
        "smooth_lua_gc=1\n"
        "fast_spatial_query=1\n"
        "timeslice_tex_upload=1\n"
        "gl_state_cache=1\n"
        "gl_uniform_cache=1\n"
        "enable_fs_neg_cache=1\n"
        "adaptive_dejitter=1\n"
        "\n"
        "[VISUAL]\n"
        "; Player-First Default: 100%% Vanilla Pure (all visual enhancements strictly opt-in)\n"
        "anisotropic_filter=1\n"
        "fullres_lightmap=0\n"
        "mip_lod_sharp=0\n"
        "hd2d_world_shaders=1\n");
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

    LogMsg("DST HyperSmooth & FastLoad Native Companion Plugin v2.6.0 (240Hz Skeletal+VFX + D3D9Ex Reflex Queue + RTSS Pacer + mimalloc + Player-First Visual Fidelity) loaded.");

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
    bool optVfxSmooth = ReadIniInt(L"OPTIMIZE", L"smooth_vfx_particles", 1) != 0;
    bool optWorldPos = ReadIniInt(L"OPTIMIZE", L"world_pos_interp", 1) != 0;
    bool optRakZeroStall = ReadIniInt(L"OPTIMIZE", L"raknet_zero_stall", 1) != 0;
    bool optD3d9ExLowLat = ReadIniInt(L"OPTIMIZE", L"d3d9ex_low_latency", 1) != 0;
    bool optLowLatGpu = ReadIniInt(L"OPTIMIZE", L"low_latency_gpu_sync", 1) != 0;
    bool optSmoothGc = ReadIniInt(L"OPTIMIZE", L"smooth_lua_gc", 1) != 0;
    bool optFastSpatial = ReadIniInt(L"OPTIMIZE", L"fast_spatial_query", 1) != 0;
    bool optTexTimeSlice = ReadIniInt(L"OPTIMIZE", L"timeslice_tex_upload", 1) != 0;
    bool optGlStateCache = ReadIniInt(L"OPTIMIZE", L"gl_state_cache", 1) != 0;
    bool optGlUniformCache = ReadIniInt(L"OPTIMIZE", L"gl_uniform_cache", 1) != 0;
    bool optFsNegCache = ReadIniInt(L"OPTIMIZE", L"enable_fs_neg_cache", 1) != 0;
    bool optDeJitter = ReadIniInt(L"OPTIMIZE", L"adaptive_dejitter", 1) != 0;
    {
        DEVMODEW dm{};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 0) {
            g_displayRefreshHz.store(dm.dmDisplayFrequency, std::memory_order_relaxed);
            LogMsg("[DISPLAY] Primary monitor refresh rate detected: %lu Hz", static_cast<unsigned long>(dm.dmDisplayFrequency));
        }
    }
    int  optAniso = ReadIniInt(L"VISUAL", L"anisotropic_filter", 1);
    if (optAniso < 1) optAniso = 1;
    if (optAniso > 16) optAniso = 16;
    bool optFullresLm = ReadIniInt(L"VISUAL", L"fullres_lightmap", 0) != 0;
    bool optMipSharp = ReadIniInt(L"VISUAL", L"mip_lod_sharp", 0) != 0;
    bool optHd2dWorld = ReadIniInt(L"VISUAL", L"hd2d_world_shaders", 1) != 0;
    int initFps = ReadIniInt(L"FPS", L"target_fps", 240);
    if (initFps < 0) initFps = 0;
    if (initFps > 1000) initFps = 1000;
    g_currentFps.store(initFps, std::memory_order_relaxed);
    g_skeletalTweenEnabled.store(optSkeletal ? 1 : 0, std::memory_order_relaxed);
    g_vfxSmoothEnabled.store(optVfxSmooth ? 1 : 0, std::memory_order_relaxed);
    g_worldPosInterpEnabled.store(optWorldPos ? 1 : 0, std::memory_order_relaxed);
    g_optRtssPacer.store(optRtssPacer, std::memory_order_relaxed);
    g_optMimallocLua.store(optMimallocLua, std::memory_order_relaxed);
    g_optRakNetZeroStall.store(optRakZeroStall, std::memory_order_relaxed);
    g_optD3d9ExLowLatency.store(optD3d9ExLowLat, std::memory_order_relaxed);
    g_optLowLatencyGpuSync.store(optLowLatGpu, std::memory_order_relaxed);
    g_optFastSpatialQuery.store(optFastSpatial, std::memory_order_relaxed);
    g_optTexTimeSlice.store(optTexTimeSlice, std::memory_order_relaxed);
    g_optGlStateCache.store(optGlStateCache, std::memory_order_relaxed);
    g_optGlUniformCache.store(optGlUniformCache, std::memory_order_relaxed);
    g_optFsNegCache.store(optFsNegCache, std::memory_order_relaxed);
    g_optAdaptiveDeJitter.store(optDeJitter, std::memory_order_relaxed);
    g_visualAnisoLevel.store(optAniso, std::memory_order_relaxed);
    g_visualFullresLightmap.store(optFullresLm, std::memory_order_relaxed);
    g_visualMipLodBias.store(optMipSharp ? -0.45f : 0.0f, std::memory_order_relaxed);
    g_optHd2dWorldShaders.store(optHd2dWorld, std::memory_order_release);

    // Install libGLESv2 + KERNEL32 PE IAT hooks FIRST (at ~t=+1ms, BEFORE AOB scan) so glShaderSource
    // / glLinkProgram / glUseProgram and Mod Negative Stat Cache are live before DST loads bundles/shaders!
    InstallGlIatHooks();
    InstallFsNegCacheHooks();

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
        patches.reserve(38);

        PrepareVisualFidelityHooks(patches, textStart, textSize);
        PrepareRenderPerfHooks(patches, textStart, textSize);
        PrepareLightMap240AndHotSwapHooks(patches, textStart, textSize);
        if (optMimallocLua) PrepareClientMimallocHook(patches, textStart, textSize);
        if (optFastVec) PrepareVectorMemsetPatch(patches, textStart, textSize);
        if (optLibdeflate) {
            PrepareLibdeflateUnzHook(patches, textStart, textSize);
            PrepareClientCompress2Hook(patches, textStart, textSize);
        }
        if (optFastZlib) PrepareFastInflatePatch(patches, textStart, textSize);
        if (optSmoothGc) PrepareSmoothLuaGcHook(patches, textStart, textSize);
        if (optRakZeroStall) PrepareRakNetZeroStallPatch(patches, textStart, textSize);
        if (optFastSpatial) PrepareClientSpatialZeroCopyHooks(patches, textStart, textSize);
        if (optTexTimeSlice) PrepareTexStreamTimeSliceHook(patches, textStart, textSize);
        PrepareClientFastAStarHooks(patches, textStart, textSize);
        PrepareVfxParticleSmoothHooks(patches, textStart, textSize);
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

        // Phase 2: Atomic cold-start patch application under ThreadFreezeGuard (RIP-safe, fail-closed!)
        ApplyPendingPatchesSafely(patches);
        auto isApplied = [&](size_t idx) -> bool {
            return idx != static_cast<size_t>(-1) && idx < patches.size() && patches[idx].applied;
        };
        if (isApplied(flPatchIdx)) {
            g_frameLimiterHooked.store(true, std::memory_order_release);
        }
        if (isApplied(g_clientComp2PatchIdx)) {
            g_clientCompress2Hooked.store(true, std::memory_order_release);
        }
        if (isApplied(g_clientAStarPatchIdx[0]) &&
            isApplied(g_clientAStarPatchIdx[1]) &&
            isApplied(g_clientAStarPatchIdx[2])) {
            g_clientAStarReady.store(true, std::memory_order_release);
        }
        if (isApplied(g_vfxPmPatchIdx) && isApplied(g_vfxCmdPatchIdx)) {
            g_vfxSmoothHooked.store(true, std::memory_order_release);
        }
        if (isApplied(g_angleAfPatchIdx)) {
            g_angleAfUnlocked = true;
        }
        if (isApplied(g_angleSamplerPatchIdx)) {
            g_angleSamplerHooked.store(true, std::memory_order_release);
        }
        if (isApplied(g_lightmapScalePatchIdx)) {
            g_lightmapPatched = true;
        }
        if (isApplied(g_lightmapHotSwapIdx)) {
            g_lightmapHotSwapHooked.store(true, std::memory_order_release);
        }
        if (isApplied(g_light240PatchIdx)) {
            g_lightPos240Hooked.store(true, std::memory_order_release);
        }
        if (isApplied(g_gameRenderPatchIdx) && isApplied(g_postProcPatchIdx)) {
            g_renderPerfHooked = true;
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

        // Sample active GPU process VRAM usage 1x/sec purely on background WorkerProc (0 Render/Game thread cost)
        if (tickCounter % 2 == 0) {
            SampleProcessVramDXGI();
        }

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
                LONG64 afHits = InterlockedCompareExchange64(&g_afAppliedHits, 0, 0);
                LONG64 afTri  = InterlockedCompareExchange64(&g_afTrilinearUpgrades, 0, 0);
                LONG64 vfxCmds = InterlockedCompareExchange64(&g_vfxCmdsSmoothed, 0, 0);
                LONG64 vfxPts  = InterlockedCompareExchange64(&g_vfxParticlesSmoothed, 0, 0);
                LONG64 light240 = InterlockedCompareExchange64(&g_lightPos240Count, 0, 0);
                LONG64 glCalls = InterlockedCompareExchange64(&g_glCacheCalls, 0, 0);
                LONG64 glHits  = InterlockedCompareExchange64(&g_glCacheHits, 0, 0);
                double glHitPct = (glCalls > 0) ? (100.0 * static_cast<double>(glHits) / static_cast<double>(glCalls)) : 0.0;
                LONG64 uniCalls = InterlockedCompareExchange64(&g_glUniCalls, 0, 0);
                LONG64 uniHits  = InterlockedCompareExchange64(&g_glUniHits, 0, 0);
                double uniPct = (uniCalls > 0) ? (100.0 * static_cast<double>(uniHits) / static_cast<double>(uniCalls)) : 0.0;
                LONG64 fsQ = InterlockedCompareExchange64(&g_fsNegQueries, 0, 0);
                LONG64 fsH = InterlockedCompareExchange64(&g_fsNegHits, 0, 0);
                LONG64 djH = InterlockedCompareExchange64(&g_deJitterHits, 0, 0);
                double djPk = static_cast<double>(g_deJitterPeakUs.load(std::memory_order_relaxed)) * 0.001;
                double avgFps = 0.0, low1Fps = 0.0, p99Ms = 0.0, jitterMs = 0.0;
                g_frameRing.ComputeStats(avgFps, low1Fps, p99Ms, jitterMs);
                LogMsg("[Summary] avg=%.0f 1%%L=%.0f p99=%.2fms jit=%.2fms sim=%dHz disp=%uHz | libdeflate=%lld (%.1fMB) | skeletal240=%lld (%lld bones, %lld swap, %lld shed) | vfx240=%lld (%lld pts) | light240=%lld | pos240=%lld (byp=%lld deJit=%lld/%.2fms) | d3d9ex_lat=%u/%u | glCache=%.1f%% (%lld/%lld) glUni=%.1f%% (%lld/%lld) fsNeg=%lld/%lld swap=%.2fms | spatial=%lld (%lld cells) | tex=%lld (slice=%lld) | visual(af=%dx/hw=%.0fx hits=%lld tri=%lld lm=%.1fx mip=%.2f hd2d(inj=%u progs=%u str=%.2f) busy=%.2fms world=%.2fms post=%.2fms gpu=%.2fms vram=%.0fMB/pk=%.0fMB) | gc(full=%lld defer=%lld) | mi_allocs=%lld.",
                       avgFps, low1Fps, p99Ms, jitterMs, g_clientSimTickRate.load(std::memory_order_relaxed),
                       g_displayRefreshHz.load(std::memory_order_relaxed),
                       hits, static_cast<double>(bytes) / (1024.0 * 1024.0), interp, bones, p2, shed,
                       vfxCmds, vfxPts, light240,
                       posInterp, posBypass, djH, djPk,
                       g_d3d9ExCurLatency.load(std::memory_order_relaxed),
                       g_d3d9ExOrigLatency.load(std::memory_order_relaxed),
                       glHitPct, glHits, glCalls,
                       uniPct, uniHits, uniCalls,
                       fsH, fsQ,
                       static_cast<double>(g_emaSwapUs.load(std::memory_order_relaxed)) * 0.001,
                       spQ, spC, texUp, texSl,
                       g_visualAnisoLevel.load(std::memory_order_relaxed),
                       static_cast<double>(g_hwMaxAniso.load(std::memory_order_relaxed)),
                       afHits, afTri, g_lightmapScaleSlot ? static_cast<double>(*g_lightmapScaleSlot) : 0.5,
                       static_cast<double>(g_visualMipLodBias.load(std::memory_order_relaxed)),
                       g_hd2dShadersInjected.load(std::memory_order_relaxed),
                       g_hd2dProgsLinked.load(std::memory_order_relaxed),
                       static_cast<double>(g_hd2dMasterStr.load(std::memory_order_relaxed)),
                       static_cast<double>(g_emaFrameBusyUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaWorldRenderUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaPostProcUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaGpuDrainUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_processVramMb.load(std::memory_order_relaxed)),
                       static_cast<double>(g_peakVramMb.load(std::memory_order_relaxed)),
                       gcFull, gcDef, miAlloc);
            }
        }

        // Zero-spam Hitch & Severe-Stall breakdown telemetry: logs ONLY when >=1 frame >25ms occurs in gameplay
        // (rate-limited to at most 1 line every 10 seconds, tear-free snapshot, 0 MainThread/RenderThread disk I/O!)
        if (tickCounter - lastHitchLogTick >= 20) {
            HitchSnapshot snap{};
            AcquireSRWLockExclusive(&g_hitchLock);
            snap = g_hitchSnap;
            g_hitchSnap = {};
            ReleaseSRWLockExclusive(&g_hitchLock);
            if (snap.pendingCount > 0 || snap.severeCount > 0) {
                lastHitchLogTick = tickCounter;
                const LONG64 gcFull = InterlockedCompareExchange64(&g_gcEmergencyFulls, 0, 0);
                const LONG64 spQ = InterlockedCompareExchange64(&g_clientSpatialQueries, 0, 0);
                const LONG64 texUp = InterlockedCompareExchange64(&g_texUploadsTotal, 0, 0);
                const LONG64 texSl = InterlockedCompareExchange64(&g_texUploadSlices, 0, 0);
                double avgFps = 0.0, low1Fps = 0.0, p99Ms = 0.0, jitterMs = 0.0;
                g_frameRing.ComputeStats(avgFps, low1Fps, p99Ms, jitterMs);
                LogMsg("[Hitch] peak=%.1fms (cnt=%u severe=%u[max=%.1fms] | avg=%.0f 1%%L=%.0f p99=%.1fms) | busy=%.1fms swap=%.1fms world=%.1fms post=%.1fms | anim=%.2fms (ents=%u bones=%u shed=%u) | gc=%.2fms (kb=%d full=%lld) | tex=%.2fms (up=%lld slice=%lld) | zip=%.2fms shdr=%.2fms | spatial=%lld",
                       static_cast<double>(snap.peakUs) * 0.001, snap.pendingCount,
                       snap.severeCount, static_cast<double>(snap.severeMaxUs) * 0.001,
                       avgFps, low1Fps, p99Ms,
                       static_cast<double>(snap.busyUs) * 0.001, static_cast<double>(snap.swapUs) * 0.001,
                       static_cast<double>(snap.worldUs) * 0.001, static_cast<double>(snap.postUs) * 0.001,
                       static_cast<double>(snap.animUs) * 0.001, snap.animEnts, snap.animBones, snap.animShed,
                       static_cast<double>(snap.gcUs) * 0.001, snap.gcKb, gcFull,
                       static_cast<double>(snap.texUs) * 0.001, texUp, texSl,
                       static_cast<double>(snap.unzUs) * 0.001, static_cast<double>(snap.shaderUs) * 0.001, spQ);
            }
        }

        // Poll dst_fps.ini once per second for manual user edits
        if (tickCounter % 2 == 0) {
            uint64_t curIniTime = GetFileModTime(g_iniPath);
            if (curIniTime != 0 && curIniTime != lastIniTime) {
                lastIniTime = curIniTime;
                int fps = ReadIniInt(L"FPS", L"target_fps", 240);
                int skel = ReadIniInt(L"OPTIMIZE", L"smooth_skeletal_anim", 1);
                int vfx = ReadIniInt(L"OPTIMIZE", L"smooth_vfx_particles", 1);
                int wpos = ReadIniInt(L"OPTIMIZE", L"world_pos_interp", 1);
                int d3d9lat = ReadIniInt(L"OPTIMIZE", L"d3d9ex_low_latency", 1);
                int gsync = ReadIniInt(L"OPTIMIZE", L"low_latency_gpu_sync", 1);
                int spq = ReadIniInt(L"OPTIMIZE", L"fast_spatial_query", 1);
                int texs = ReadIniInt(L"OPTIMIZE", L"timeslice_tex_upload", 1);
                int glsc = ReadIniInt(L"OPTIMIZE", L"gl_state_cache", 1);
                int gluc = ReadIniInt(L"OPTIMIZE", L"gl_uniform_cache", 1);
                int fsnc = ReadIniInt(L"OPTIMIZE", L"enable_fs_neg_cache", 1);
                int adjt = ReadIniInt(L"OPTIMIZE", L"adaptive_dejitter", 1);
                int af = ReadIniInt(L"VISUAL", L"anisotropic_filter", 1);
                if (af < 1) af = 1;
                if (af > 16) af = 16;
                int flm = ReadIniInt(L"VISUAL", L"fullres_lightmap", 0);
                int mips = ReadIniInt(L"VISUAL", L"mip_lod_sharp", 0);
                int hd2d = ReadIniInt(L"VISUAL", L"hd2d_world_shaders", 1);
                g_skeletalTweenEnabled.store(skel ? 1 : 0, std::memory_order_relaxed);
                g_vfxSmoothEnabled.store(vfx ? 1 : 0, std::memory_order_relaxed);
                g_worldPosInterpEnabled.store(wpos ? 1 : 0, std::memory_order_relaxed);
                if (g_optD3d9ExLowLatency.exchange(d3d9lat != 0, std::memory_order_relaxed) != (d3d9lat != 0)) {
                    g_d3d9ExLatGen.fetch_add(1, std::memory_order_release);
                }
                g_optLowLatencyGpuSync.store(gsync != 0, std::memory_order_relaxed);
                g_optFastSpatialQuery.store(spq != 0, std::memory_order_relaxed);
                g_optTexTimeSlice.store(texs != 0, std::memory_order_relaxed);
                const bool wantGlsc = (glsc != 0) && g_glCacheActive.load(std::memory_order_acquire);
                const bool wantGluc = (gluc != 0) && g_glUniformCacheActive.load(std::memory_order_acquire);
                if (g_optGlStateCache.exchange(wantGlsc, std::memory_order_relaxed) != wantGlsc ||
                    g_optGlUniformCache.exchange(wantGluc, std::memory_order_relaxed) != wantGluc) {
                    g_glCacheResetGen.fetch_add(1, std::memory_order_release);
                }
                g_optFsNegCache.store((fsnc != 0) && g_fsNegCacheActive.load(std::memory_order_acquire), std::memory_order_relaxed);
                g_optAdaptiveDeJitter.store(adjt != 0, std::memory_order_relaxed);
                g_visualAnisoLevel.store(af, std::memory_order_relaxed);
                const bool prevIniLm = g_visualFullresLightmap.exchange(flm != 0, std::memory_order_relaxed);
                g_visualMipLodBias.store(mips ? -0.45f : 0.0f, std::memory_order_relaxed);
                g_optHd2dWorldShaders.store(hd2d != 0, std::memory_order_relaxed);
                if (g_lightmapScaleSlot) *g_lightmapScaleSlot = (flm != 0) ? 1.0f : 0.5f;
                if (prevIniLm != (flm != 0)) {
                    g_lightmapRebuildGen.fetch_add(1, std::memory_order_release);
                }
                LogMsg("Hot-reloaded dst_fps.ini -> FPS=%d, Skeletal240=%d, Vfx240=%d, WorldPos240=%d, D3D9ExLat=%d, GpuSync=%d, SpatialZC=%d, TexSlice=%d, AF=%dx, FullLM=%d, MipSharp=%d, HD2D=%d",
                       fps, skel ? 1 : 0, vfx ? 1 : 0, wpos ? 1 : 0, d3d9lat ? 1 : 0, gsync ? 1 : 0, spq ? 1 : 0, texs ? 1 : 0, af, flm ? 1 : 0, mips ? 1 : 0, hd2d ? 1 : 0);
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
