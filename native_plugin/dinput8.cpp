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
#include <cerrno>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "libdeflate.h"
#include "mimalloc.h"
#include "gl_state_cache.h"
#include "fs_neg_cache.h"
#include "fast_astar.h"
#include "fast_astar_manager.h"
#include "fast_dumper.h"
#include "fast_json.h"
#include "native_gl_bridge.h"
#include "dxgi_flip_present.h"
#include "vb_pool.h"
#include "rpc_qos.h"
#include "klei_save_codec.h"
#include "async_save_commit.h"
#include "spike_blackbox.h"
#include "sprite_batcher.h"
#include "ui_batcher.h"
#include "world_shadow_engine.h"
#include "crash_detective.h"
#include "water_reflection_engine.h"
#include "foliage_field.h"
#include "world_pos_smooth.h"
#include "skeletal_fluid.h"
#include "combat_impact.h"
#include "look_matcher.h"

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
using EglGetError_t = uint32_t (WINAPI*)();
using EglGetDisplay_t = void* (WINAPI*)(void* display_id);
using EglInitialize_t = uint32_t (WINAPI*)(void* dpy, int32_t* major, int32_t* minor);
using EglGetConfigs_t = uint32_t (WINAPI*)(void* dpy, void** configs, int32_t config_size, int32_t* num_config);
using EglChooseConfig_t = uint32_t (WINAPI*)(void* dpy, const int32_t* attrib_list, void** configs, int32_t config_size, int32_t* num_config);
using EglGetConfigAttrib_t = uint32_t (WINAPI*)(void* dpy, void* config, int32_t attribute, int32_t* value);
using EglCreateWindowSurface_t = void* (WINAPI*)(void* dpy, void* config, HWND win, const int32_t* attrib_list);
using EglSwapInterval_t = uint32_t (WINAPI*)(void* dpy, int32_t interval);
using EglCreateContext_t = void* (WINAPI*)(void* dpy, void* config, void* share_context, const int32_t* attrib_list);
using EglGetProcAddress_t = void* (WINAPI*)(const char* procname);
using GlFinish_t = void (WINAPI*)();
using GlGenBuffers_t = void (WINAPI*)(int32_t n, uint32_t* buffers);
using GlBufferData_t = void (WINAPI*)(uint32_t target, ptrdiff_t size, const void* data, uint32_t usage);
using GlBufferSubData_t = void (WINAPI*)(uint32_t target, ptrdiff_t offset, ptrdiff_t size, const void* data);
using GlMapBufferRange_t = void* (WINAPI*)(uint32_t target, ptrdiff_t offset, ptrdiff_t length, uint32_t access);
using GlUnmapBuffer_t = uint8_t (WINAPI*)(uint32_t target);
using GlGetBufferParameteriv_t = void (WINAPI*)(uint32_t target, uint32_t pname, int32_t* params);
using GlGetBufferSubData_t = void (WINAPI*)(uint32_t target, ptrdiff_t offset, ptrdiff_t size, void* data);
using HWBufferInit_t = uint8_t (*)(void* self, const void* data);
using GlTexParameteri_t = void (WINAPI*)(uint32_t target, uint32_t pname, int32_t param);
using GlTexParameterf_t = void (WINAPI*)(uint32_t target, uint32_t pname, float param);
using GlGetFloatv_t = void (WINAPI*)(uint32_t pname, float* params);
using GlGetString_t = const char* (WINAPI*)(uint32_t name);
using GlShaderSource_t = void (WINAPI*)(uint32_t shader, int32_t count, const char* const* string, const int32_t* length);
using GlAttachShader_t = void (WINAPI*)(uint32_t program, uint32_t shader);
using GlCreateShader_t = uint32_t (WINAPI*)(uint32_t type);
using GlCompileShader_t = void (WINAPI*)(uint32_t shader);
using GlGetShaderiv_t = void (WINAPI*)(uint32_t shader, uint32_t pname, int32_t* params);
using GlDeleteShader_t = void (WINAPI*)(uint32_t shader);
using GlCreateProgram_t = uint32_t (WINAPI*)();
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
using GlBlendFuncSeparate_t = void (WINAPI*)(uint32_t srcRGB, uint32_t dstRGB, uint32_t srcAlpha, uint32_t dstAlpha);
using GlDepthMask_t = void (WINAPI*)(uint8_t flag);
using GlEnable_t = void (WINAPI*)(uint32_t cap);
using GlDisable_t = void (WINAPI*)(uint32_t cap);
using GlBindBuffer_t = void (WINAPI*)(uint32_t target, uint32_t buffer);
using GlDeleteBuffers_t = void (WINAPI*)(int32_t n, const uint32_t* buffers);
using GlBlendEquation_t = void (WINAPI*)(uint32_t mode);
using GlBlendEquationSeparate_t = void (WINAPI*)(uint32_t modeRGB, uint32_t modeAlpha);
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
using GlBindAttribLocation_t = void (WINAPI*)(uint32_t program, uint32_t index, const char* name);
using GlGetAttribLocation_t = int32_t (WINAPI*)(uint32_t program, const char* name);
using GlGetIntegerv_t = void (WINAPI*)(uint32_t pname, int32_t* params);
using GlVertexAttribPointer_t = void (WINAPI*)(uint32_t index, int32_t size, uint32_t type, uint8_t normalized, int32_t stride, const void* ptr);
using GlDrawArrays_t = void (WINAPI*)(uint32_t mode, int32_t first, int32_t count);
using GlDrawElements_t = void (WINAPI*)(uint32_t mode, int32_t count, uint32_t type, const void* indices);
using GlBindFramebuffer_t = void (WINAPI*)(uint32_t target, uint32_t framebuffer);
using GlClear_t = void (WINAPI*)(uint32_t mask);
using GlClearColor_t = void (WINAPI*)(float r, float g, float b, float a);
using GlGenFramebuffers_t = void (WINAPI*)(int32_t n, uint32_t* framebuffers);
using GlDeleteFramebuffers_t = void (WINAPI*)(int32_t n, const uint32_t* framebuffers);
using GlFramebufferTexture2D_t = void (WINAPI*)(uint32_t target, uint32_t attachment, uint32_t textarget, uint32_t texture, int32_t level);
using GlCheckFramebufferStatus_t = uint32_t (WINAPI*)(uint32_t target);
using GlPixelStorei_t = void (WINAPI*)(uint32_t pname, int32_t param);
using GlBlitFramebuffer_t = void (WINAPI*)(int32_t srcX0, int32_t srcY0, int32_t srcX1, int32_t srcY1,
                                           int32_t dstX0, int32_t dstY0, int32_t dstX1, int32_t dstY1,
                                           uint32_t mask, uint32_t filter);
using GlFenceSync_t = void* (WINAPI*)(uint32_t condition, uint32_t flags);
using GlClientWaitSync_t = uint32_t (WINAPI*)(void* sync, uint32_t flags, uint64_t timeout);
using GlDeleteSync_t = void (WINAPI*)(void* sync);
using GlGenRenderbuffers_t = void (WINAPI*)(int32_t n, uint32_t* renderbuffers);
using GlDeleteRenderbuffers_t = void (WINAPI*)(int32_t n, const uint32_t* renderbuffers);
using GlBindRenderbuffer_t = void (WINAPI*)(uint32_t target, uint32_t renderbuffer);
using GlRenderbufferStorage_t = void (WINAPI*)(uint32_t target, uint32_t internalformat, int32_t width, int32_t height);
using GlFramebufferRenderbuffer_t = void (WINAPI*)(uint32_t target, uint32_t attachment, uint32_t renderbuffertarget, uint32_t renderbuffer);
using GlGenTextures_t = void (WINAPI*)(int32_t n, uint32_t* textures);
using GlGetError_t = uint32_t (WINAPI*)();
using GlGetShaderInfoLog_t = void (WINAPI*)(uint32_t shader, int32_t bufSize, int32_t* length, char* infoLog);
using GlGetProgramInfoLog_t = void (WINAPI*)(uint32_t program, int32_t bufSize, int32_t* length, char* infoLog);
using GlReadPixels_t = void (WINAPI*)(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format, uint32_t type, void* pixels);
using GlStencilFunc_t = void (WINAPI*)(uint32_t func, int32_t ref, uint32_t mask);
using GlStencilMask_t = void (WINAPI*)(uint32_t mask);
using GlStencilOp_t = void (WINAPI*)(uint32_t fail, uint32_t zfail, uint32_t zpass);
using GlPolygonOffset_t = void (WINAPI*)(float factor, float units);
using GlTexImage2D_t = void (WINAPI*)(uint32_t target, int32_t level, int32_t internalformat, int32_t width, int32_t height, int32_t border, uint32_t format, uint32_t type, const void* pixels);
using GlCompressedTexImage2D_t = void (WINAPI*)(uint32_t target, int32_t level, uint32_t internalformat, int32_t width, int32_t height, int32_t border, int32_t imageSize, const void* data);
using GetFileAttributesW_t = DWORD (WINAPI*)(LPCWSTR lpFileName);
using MoveFileExW_t = BOOL (WINAPI*)(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, DWORD dwFlags);
using CopyFileW_t = BOOL (WINAPI*)(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists);
using DeleteFileW_t = BOOL (WINAPI*)(LPCWSTR lpFileName);
using CreateDirectoryW_t = BOOL (WINAPI*)(LPCWSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes);
using RemoveDirectoryW_t = BOOL (WINAPI*)(LPCWSTR lpPathName);
using SetCurrentDirectoryW_t = BOOL (WINAPI*)(LPCWSTR lpPathName);
using FindFirstFileW_t = HANDLE (WINAPI*)(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData);
using CreateFileW_t = HANDLE (WINAPI*)(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile);
using MsvcrFopenA_t = FILE* (__cdecl*)(const char* path, const char* mode);
using MsvcrWfopen_t = FILE* (__cdecl*)(const wchar_t* path, const wchar_t* mode);
using MsvcrWfopenS_t = int (__cdecl*)(FILE** pFile, const wchar_t* path, const wchar_t* mode);
using MsvcrFwrite_t = size_t (__cdecl*)(const void* buf, size_t size, size_t count, FILE* stream);
using MsvcrFclose_t = int (__cdecl*)(FILE* stream);
using MsvcrStat64A_t = int (__cdecl*)(const char* path, void* buf);
using MsvcrStat64i32A_t = int (__cdecl*)(const char* path, void* buf);
using MsvcrWstat64i32_t = int (__cdecl*)(const wchar_t* path, void* buf);
using MsvcrOpenA_t = int (__cdecl*)(const char* path, int oflag, int pmode);
using MsvcrErrnoFn_t = int* (__cdecl*)();
using ExeExit_t = void (__cdecl*)(int code);
using ExeCexit_t = void (__cdecl*)();
using ClientUncompress_t = int (*)(uint8_t* dest, uint32_t* destLen, const uint8_t* src, uint32_t srcLen);
using ClientLuaUpdate_t = uint64_t (*)(void* sim, uint32_t a2, uint32_t a3);
using ClientNetUpdate_t = void (*)(void* netMgr, float dt);
using Rpc4Call_t = void (*)(void* thisPtr, const char* uniqueID, void* bitStream,
                            int priority, int reliability, uint8_t orderingChannel,
                            const void* systemIdentifier, uint8_t broadcast, uint8_t callLocally);
using WritePersistentFile_t = uint8_t (*)(const char* utf8Path, const void* data, uint32_t size,
                                          void* delegate, uint32_t arg5, void* arg6);
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
std::atomic<bool> g_optSpriteBatch{true};
std::atomic<bool> g_spriteBatchActive{false};
std::atomic<uint32_t> g_sbShadersInjected{0};
std::atomic<uint32_t> g_sbProgsLinked{0};
std::atomic<uint32_t> g_sbOpsGen{1};
volatile LONG64   g_sbDrawsIn = 0;
volatile LONG64   g_sbDrawsBatched = 0;
volatile LONG64   g_sbFlushes = 0;
volatile LONG64   g_sbVertsBatched = 0;
volatile LONG64   g_sbBypassDraws = 0;
volatile LONG64   g_sbFlReason[static_cast<size_t>(hs_sprite_batch::FlushReason::Count)]{};
enum class StateFlushCause : uint8_t {
    Blend = 0,
    Depth,
    Cap,
    Stencil,
    PolyOff,
    Viewport,
    TexImg,
    Other,
    Count
};
volatile LONG64   g_sbStateCause[static_cast<size_t>(StateFlushCause::Count)]{};
std::atomic<bool> g_optFsNegCache{true};
std::atomic<bool> g_optAdaptiveDeJitter{true};
std::atomic<bool> g_optRpcQos{true};
std::atomic<bool> g_optAsyncSave{false};
std::atomic<bool> g_optBlackbox{true};
std::atomic<int>  g_blackboxThresholdMs{25};
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

// Visual Fidelity World Studio Shaders (Player-First Default OFF: all strengths = 0.0)
enum class WorldProgRole : uint8_t {
    None = 0,
    Ground = 1,
    Splat = 2,
    AnimWorld = 3,
    AnimBloom = 4,
    Ocean = 5,
};

struct Hd2dUniformState {
    float params[4];   // (contactAoStr, calmEnvStr, darkClarityStr, sunElev)
    float sun[4];      // (sunWorldX, sunWorldZ, lightDitherStr, camViewZ)
    float tint[4];     // (tintR, tintG, tintB, rainWetness)
    float shade[4];    // (surfaceLightStr, shadeWarm, shadeContrast, shadeCool)
    float shade2[4];   // (shadeDark, sunThetaCam, sunElevRad, dayFactor)
    float ocean[4];    // (glintStr, glintDensity, glintGrain, glintSoft)
    float shadProj[4]; // (shadowDirWx, shadowDirWz, shadowStretch, shadowProjOn)
    float shadDyn[4];  // (windTimeSec, swayAmp, heightFade, penumbraSoft)
    // v2.9.0 World-Space ShadowMask FBO config (pushed via HyperSmooth_SetShadowConfig)
    float shadowDensity;
    float shadowSoft;
    float shadowTint[3];
    float shadowEntityRecv;
    float shadowOvalKeep;
    float shadowMinHeight;
    bool  shadowHd;
    bool  shadowSkipNightmare;
    bool  hasLuaCam;
    float luaCam[4];   // (centerX, centerZ, rightX, rightZ)
    // v2.9.5 Multi-Light Near-Field Point Shadow config (pushed via HyperSmooth_SetPointLights)
    float    shadowDirStrength;
    float    shadowPointStretch;
    uint32_t pointLightCount;
    float    pointLights[hs_world_shadow::kMaxPointShadowLights][4]; // (wx, wz, radius, strength)
    // v2.9.5 M3 Planar Water Reflection config (pushed via HyperSmooth_SetWaterRefl; default 0.0 OFF)
    float    waterReflStr;
    float    waterReflRipple;
    float    waterReflLength;
    // v2.9.5-A Real-Time High-DPI Visual Detail (pushed via HyperSmooth_SetDetailParams; default 0.0 OFF)
    float    detail[4]; // (groundDetailStr, spriteEdgeStr, spriteInkStr, reserved)
    // v2.9.5-F Native PBR Ocean Optics (pushed via HyperSmooth_SetOceanPbr; default 0.0 OFF)
    float    oceanPbr[4]; // (pbrWaveStr, skyCloudRefl, sunMieGlint, hidePaperWaves)
};
struct Hd2dProgSlot {
    void*         ctx;
    bool          checked;
    bool          linkedOk;
    bool          hasAny;
    WorldProgRole role;
    int32_t       locParams;
    int32_t       locSun;
    int32_t       locTint;
    int32_t       locShade;
    int32_t       locShade2;
    int32_t       locOcean;
    int32_t       locOceanPbr;
    int32_t       locShadProj;
    int32_t       locShadDyn;
    int32_t       locDetail;
    int32_t       locSplatPt[hs_world_shadow::kMaxPointShadowLights];
    uint32_t      lastGen;
};
constexpr uint32_t kMaxHd2dProgs = 4096u;
SRWLOCK               g_hd2dStateLock = SRWLOCK_INIT;
SRWLOCK               g_hd2dProgLock  = SRWLOCK_INIT;
Hd2dUniformState      g_hd2dState = {
    { 0.0f, 0.0f, 0.0f, 0.70f },
    { 0.707f, 0.707f, 0.0f, -1.0f },
    { 1.0f, 0.94f, 0.82f, 0.0f },
    { 0.0f, 0.50f, 0.50f, 0.50f },
    { 0.50f, 0.785f, 0.75f, 1.0f },
    { 0.0f, 0.94f, 0.75f, 0.85f },
    { 0.707f, 0.707f, 1.40f, 0.0f },
    { 0.0f, 0.45f, 0.55f, 1.0f },
    0.0f, 1.15f, { 0.10f, 0.14f, 0.38f }, 0.68f, 0.24f, 0.0f, true, true, false, { 0.0f, 0.0f, 0.707f, -0.707f },
    1.0f, 1.0f, 0u, { { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f, 0.0f } },
    0.0f, 0.55f, 1.00f,
    { 0.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 0.0f }
};
std::atomic<bool>     g_waterReflReady{false};
std::atomic<uint32_t> g_waterReflDraws{0};

// v2.9.5 M4 Independent Foliage Wind/Wake state (decoupled from WorldShadowEngine!)
SRWLOCK                    g_windLock = SRWLOCK_INIT;
hs_foliage::WindFieldState g_windState{};
std::atomic<uint32_t>      g_windStateGen{1};
SRWLOCK                    g_foliageLock = SRWLOCK_INIT;
hs_foliage::FoliageTable   g_foliageStage;
std::atomic<uint32_t>      g_foliageTableGen{1};

// v2.9.5-B Pure Client-Side Combat Hit-Feel Engine (B1 Hit-Stop, B2 Recoil/Squash, B3 Additive Flash, B5 GPU VFX)
hs_combat::CombatImpactEngine g_combatImpact;
std::atomic<uint32_t>         g_combatVfxDraws{0};

// v2.9.5 M2 Crash Detective + Dual-Monitor Safe Cursor Clip
hs_crash_util::CursorClipGuard g_cursorClip;
hs_crash_util::CrashDetective  g_crashDetective;
std::atomic<uint32_t> g_hd2dStateGen{1};
std::atomic<bool>     g_optHd2dWorldShaders{true};
std::atomic<float>    g_hd2dMasterStr{0.0f};
std::atomic<uint32_t> g_hd2dShadersInjected{0};
std::atomic<uint32_t> g_hd2dProgsLinked{0};
std::atomic<uint32_t> g_hd2dAnimVsInjected{0};
std::atomic<uint32_t> g_hd2dShadowBloomInjected{0};
std::atomic<bool>     g_worldShadowReady{false};
std::atomic<bool>     g_worldShadowFailed{false};
std::atomic<bool>     g_worldShadowUsesMax{false};
std::atomic<uint32_t> g_worldShadowRes{0};
std::atomic<uint32_t> g_worldShadowDraws{0};
std::atomic<uint32_t> g_worldShadowBatchDraws{0};
std::atomic<uint32_t> g_worldShadowUnbatchedDraws{0};
std::atomic<uint32_t> g_wsOpsGen{1};
Hd2dProgSlot          g_hd2dProgs[kMaxHd2dProgs]{};
// Multi-context overflow map when two distinct non-shared EGL contexts use the same program ID
struct Hd2dCtxProgEntry {
    void*        ctx;
    uint32_t     program;
    Hd2dProgSlot slot;
};
std::vector<Hd2dCtxProgEntry> g_hd2dOverflowProgs;
GlShaderSource_t      g_origGlShaderSource = nullptr;
GlAttachShader_t      g_origGlAttachShader = nullptr;
GlCreateShader_t      g_fnGlCreateShader   = nullptr;
GlCompileShader_t     g_fnGlCompileShader  = nullptr;
GlGetShaderiv_t       g_fnGlGetShaderiv    = nullptr;
GlDeleteShader_t      g_fnGlDeleteShader   = nullptr;
GlCreateProgram_t     g_fnGlCreateProgram  = nullptr;
GlLinkProgram_t       g_origGlLinkProgram = nullptr;
std::atomic<uint32_t> g_uiProgsLinked{0};
volatile LONG64       g_uiDrawsIn      = 0;
volatile LONG64       g_uiDrawsBatched = 0;
volatile LONG64       g_uiFlushes      = 0;
volatile LONG64       g_uiVertsBatched = 0;
volatile LONG64       g_uiBypassDraws  = 0;
volatile LONG64       g_uiProgSkips    = 0;
volatile LONG64       g_uiReadbacks    = 0;
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
GetFileAttributesW_t   g_origGetFileAttributesW = nullptr;
MoveFileExW_t          g_origMoveFileExW = nullptr;
CopyFileW_t            g_origCopyFileW = nullptr;
DeleteFileW_t          g_origDeleteFileW = nullptr;
CreateDirectoryW_t     g_origCreateDirectoryW = nullptr;
RemoveDirectoryW_t     g_origRemoveDirectoryW = nullptr;
SetCurrentDirectoryW_t g_origSetCurrentDirectoryW = nullptr;
FindFirstFileW_t       g_origFindFirstFileW = nullptr;
CreateFileW_t          g_origCreateFileW = nullptr;
MsvcrFopenA_t          g_origMsvcrFopen = nullptr;
MsvcrWfopen_t          g_origMsvcrWfopen = nullptr;
MsvcrWfopenS_t         g_origMsvcrWfopenS = nullptr;
MsvcrFwrite_t          g_origMsvcrFwrite = nullptr;
MsvcrFclose_t          g_origMsvcrFclose = nullptr;
MsvcrStat64A_t         g_origMsvcrStat64 = nullptr;
MsvcrStat64i32A_t      g_origMsvcrStat64i32 = nullptr;
MsvcrWstat64i32_t      g_origMsvcrWstat64i32 = nullptr;
MsvcrOpenA_t           g_origMsvcrOpen = nullptr;
MsvcrErrnoFn_t         g_msvcr90Errno = nullptr;
ExeExit_t              g_origExeExit = nullptr;
ExeExit_t              g_origExe_exit = nullptr;
ExeCexit_t             g_origExeCexit = nullptr;
std::atomic<bool>      g_fsNegCacheActive{false};
std::atomic<bool>      g_asyncBarriersReady{false};
std::atomic<bool>      g_asyncSaveActive{false};
FILE* const            kAsyncSentinelFile = reinterpret_cast<FILE*>(static_cast<uintptr_t>(0x1ULL));
alignas(hc_async_save::AsyncSaveQueue) static uint8_t g_asyncSaveStorage[sizeof(hc_async_save::AsyncSaveQueue)];
std::atomic<hc_async_save::AsyncSaveQueue*> g_asyncSavePtr{nullptr};
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
WritePersistentFile_t    g_origWritePersistentFile = nullptr;
std::atomic<uint64_t>    g_asyncSaveFallbacks{0};
struct ClientAsyncCaptureState {
    bool        active = false;
    const void* data = nullptr;
    uint32_t    size = 0;
};
thread_local ClientAsyncCaptureState tl_asyncCapture{};

hs_rpc_qos::Classifier   g_rpcQos{};
Rpc4Call_t               g_origRpc4Call = nullptr;
std::atomic<bool>        g_rpcQosHooked{false};
std::atomic<bool>        g_netConnected{false};
std::atomic<int>         g_netPingAvg{-1};
std::atomic<int>         g_netPingLast{-1};
std::atomic<float>       g_netLossCur{0.0f};
std::atomic<float>       g_netLossAvg{0.0f};
std::atomic<uint32_t>    g_netResendMsgs{0};
std::atomic<uint64_t>    g_netTxBps{0};
std::atomic<uint64_t>    g_netRxBps{0};
std::atomic<int64_t>     g_lastNetSampleQpc{0};

size_t g_rpcQosPatchIdx         = static_cast<size_t>(-1);
size_t g_writePersistPatchIdx   = static_cast<size_t>(-1);
volatile LONG64        g_fsNegQueries = 0;
volatile LONG64        g_fsNegHits = 0;
volatile LONG64        g_deJitterHits = 0;
std::atomic<uint32_t>  g_deJitterPeakUs{0};

ClientUncompress_t     g_origClientUncompress = nullptr;
volatile LONG64        g_clientUncompCalls = 0;
volatile LONG64        g_clientUncompBytes = 0;
volatile LONG64        g_clientFastJsonEncCalls = 0;
volatile LONG64        g_clientFastJsonDecCalls = 0;
volatile LONG64        g_clientFastJsonFallbacks = 0;
volatile LONG64        g_shadowPos240Count = 0;
std::atomic<bool>      g_shadow240Hooked{false};
ClientLuaUpdate_t      g_origClientLuaUpdate = nullptr;
ClientNetUpdate_t      g_origClientNetUpdate = nullptr;

// Cold-start patch indices (status flags flip ONLY after ApplyPendingPatchesSafely confirms applied!)
size_t g_vfxPmPatchIdx         = static_cast<size_t>(-1);
size_t g_vfxCmdPatchIdx        = static_cast<size_t>(-1);
size_t g_vfxEmitPatchIdx       = static_cast<size_t>(-1);
size_t g_angleAfPatchIdx       = static_cast<size_t>(-1);
size_t g_angleSamplerPatchIdx  = static_cast<size_t>(-1);
size_t g_lightmapScalePatchIdx = static_cast<size_t>(-1);
size_t g_lightmapHotSwapIdx    = static_cast<size_t>(-1);
size_t g_light240PatchIdx      = static_cast<size_t>(-1);
size_t g_shadow240PatchIdx     = static_cast<size_t>(-1);
size_t g_animCmdCtorPatchIdx   = static_cast<size_t>(-1);
size_t g_gameRenderPatchIdx    = static_cast<size_t>(-1);
size_t g_postProcPatchIdx      = static_cast<size_t>(-1);
size_t g_vbPoolPatchIdx        = static_cast<size_t>(-1);
std::atomic<int>  g_cfgRenderBackend{2}; // 0=d3d9, 1=opengl, 2=auto
std::atomic<int>  g_cfgDxgiFlipPresent{0}; // 0=off (default), 1=on, 2=auto
std::atomic<bool> g_nativeGlActive{false};
std::atomic<bool> g_nativeGlProbed{false};
std::atomic<bool> g_dxgiFlipProbedOk{false};
std::atomic<bool> g_nativeIatBound{false};
std::atomic<bool> g_optVbPool{true};
std::atomic<bool> g_vbPoolHooked{false};
hs_native_gl::WglRuntime g_wglRt{};
// Intentionally leak-safe singleton so DXGI/D3D11/HWND teardown NEVER runs under DLL_PROCESS_DETACH loader lock.
hs_dxgi_flip::DxgiFlipPresenter& GetDxgiFlip() {
    static hs_dxgi_flip::DxgiFlipPresenter* s_inst = new hs_dxgi_flip::DxgiFlipPresenter();
    return *s_inst;
}
SRWLOCK  g_nativeGlLock = SRWLOCK_INIT;
HWND     g_nativeGameHwnd = nullptr;
HDC      g_nativeGameHdc  = nullptr;
std::atomic<int32_t> g_pendingSwapInterval{0};
std::atomic<uint32_t> g_lastEglError{hs_native_gl::kEglSuccess};
std::string g_activeGlRendererName = "ANGLE D3D9";
EglGetError_t            g_origEglGetError = nullptr;
EglGetDisplay_t          g_origEglGetDisplay = nullptr;
EglInitialize_t          g_origEglInitialize = nullptr;
EglGetConfigs_t          g_origEglGetConfigs = nullptr;
EglChooseConfig_t        g_origEglChooseConfig = nullptr;
EglGetConfigAttrib_t     g_origEglGetConfigAttrib = nullptr;
EglCreateWindowSurface_t g_origEglCreateWindowSurface = nullptr;
EglSwapInterval_t        g_origEglSwapInterval = nullptr;
EglCreateContext_t       g_origEglCreateContext = nullptr;
EglGetProcAddress_t      g_origEglGetProcAddress = nullptr;
EglMakeCurrent_t      g_origEglMakeCurrent = nullptr;
EglSwapBuffers_t      g_origEglSwapBuffers = nullptr;
GlGenBuffers_t        g_fnGlGenBuffers = nullptr;
GlBufferData_t        g_fnGlBufferData = nullptr;
GlBufferSubData_t     g_fnGlBufferSubData = nullptr;
GlMapBufferRange_t       g_fnGlMapBufferRange = nullptr;
GlUnmapBuffer_t          g_fnGlUnmapBuffer = nullptr;
GlGetBufferParameteriv_t g_fnGlGetBufferParameteriv = nullptr;
GlGetBufferSubData_t     g_fnGlGetBufferSubData = nullptr;
GlFinish_t               g_fnGlFinishNative = nullptr;
HWBufferInit_t        g_origHWBufferInit = nullptr;
hs_vb_pool::VbPool    g_vbPool;
GlActiveTexture_t     g_origGlActiveTexture = nullptr;
GlBindTexture_t       g_origGlBindTexture = nullptr;
GlDeleteTextures_t    g_origGlDeleteTextures = nullptr;
GlBlendFunc_t         g_origGlBlendFunc = nullptr;
GlBlendFuncSeparate_t g_fnGlBlendFuncSeparate = nullptr;
GlDepthMask_t         g_origGlDepthMask = nullptr;
GlEnable_t                   g_origGlEnable = nullptr;
GlDisable_t                  g_origGlDisable = nullptr;
GlBindBuffer_t               g_origGlBindBuffer = nullptr;
GlDeleteBuffers_t            g_origGlDeleteBuffers = nullptr;
GlBlendEquation_t            g_origGlBlendEquation = nullptr;
GlBlendEquationSeparate_t    g_fnGlBlendEquationSeparate = nullptr;
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
GlBindAttribLocation_t       g_fnGlBindAttribLocation = nullptr;
GlGetAttribLocation_t        g_fnGlGetAttribLocation = nullptr;
GlGetIntegerv_t              g_fnGlGetIntegerv = nullptr;
GlBufferData_t               g_origGlBufferData = nullptr;
GlVertexAttribPointer_t      g_origGlVertexAttribPointer = nullptr;
GlDrawArrays_t               g_origGlDrawArrays = nullptr;
GlDrawElements_t             g_origGlDrawElements = nullptr;
GlBindFramebuffer_t          g_origGlBindFramebuffer = nullptr;
GlDeleteFramebuffers_t       g_origGlDeleteFramebuffers = nullptr;
GlClear_t                    g_origGlClear = nullptr;
GlClearColor_t               g_origGlClearColor = nullptr;
GlGenFramebuffers_t          g_fnGlGenFramebuffers = nullptr;
GlDeleteFramebuffers_t       g_fnGlDeleteFramebuffers = nullptr;
GlFramebufferTexture2D_t     g_fnGlFramebufferTexture2D = nullptr;
GlCheckFramebufferStatus_t   g_fnGlCheckFramebufferStatus = nullptr;
GlGenRenderbuffers_t         g_fnGlGenRenderbuffers = nullptr;
GlDeleteRenderbuffers_t      g_fnGlDeleteRenderbuffers = nullptr;
GlBindRenderbuffer_t         g_fnGlBindRenderbuffer = nullptr;
GlRenderbufferStorage_t      g_fnGlRenderbufferStorage = nullptr;
GlFramebufferRenderbuffer_t  g_fnGlFramebufferRenderbuffer = nullptr;
GlGenTextures_t              g_fnGlGenTextures = nullptr;
GlGetError_t                 g_fnGlGetError = nullptr;
GlGetShaderInfoLog_t         g_fnGlGetShaderInfoLog = nullptr;
GlGetProgramInfoLog_t        g_fnGlGetProgramInfoLog = nullptr;
GlReadPixels_t               g_origGlReadPixels = nullptr;
GlPixelStorei_t              g_fnGlPixelStorei = nullptr;
GlBlitFramebuffer_t          g_fnGlBlitFramebuffer = nullptr;
GlFenceSync_t                g_fnGlFenceSync = nullptr;
GlClientWaitSync_t           g_fnGlClientWaitSync = nullptr;
GlDeleteSync_t               g_fnGlDeleteSync = nullptr;
hs_look_match::LookMatchManager g_lookMatcher;
GlFinish_t                   g_origGlFinish = nullptr;
GlStencilFunc_t              g_origGlStencilFunc = nullptr;
GlStencilMask_t              g_origGlStencilMask = nullptr;
GlStencilOp_t                g_origGlStencilOp = nullptr;
GlPolygonOffset_t            g_origGlPolygonOffset = nullptr;
GlTexImage2D_t               g_origGlTexImage2D = nullptr;
GlCompressedTexImage2D_t     g_origGlCompressedTexImage2D = nullptr;
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
std::atomic<int> g_statsTransitionHold{0};
std::atomic<int64_t> g_statsResumeGraceUntilQpc{0};
std::atomic<uint32_t> g_frameSeq{0};
std::atomic<uint32_t> g_rtSwapSeq{0};
std::atomic<DWORD>    g_rtThreadId{0};
int64_t               g_qpcOrigin = 0;
using AnimRenderCmdCtor_t = void* (*)(void* cmdMem, void* animNode, void* worldMtx);
AnimRenderCmdCtor_t   g_origAnimRenderCmdCtor = nullptr;
std::atomic<bool>     g_animCmdCtorHooked{false};
std::atomic<int64_t> g_lastFrameUs{0};
std::atomic<LONG> g_inGameplay{0};
std::atomic<uint32_t> g_worldResetGen{0};
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
volatile LONG64 g_animSplineBones = 0;
volatile LONG64 g_animCrossfadeStarts = 0;
volatile LONG64 g_animCrossfadeFrames = 0;
std::atomic<float> g_animSplineStrength{0.75f};
std::atomic<float> g_animCrossfadeMs{55.0f};
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
std::atomic<uint32_t> g_frameLuaUs{0};
std::atomic<uint32_t> g_frameNetUs{0};
std::atomic<uint32_t> g_frameGcUs{0};
std::atomic<uint32_t> g_frameGcFull{0};
std::atomic<uint32_t> g_frameTexUploadUs{0};
std::atomic<uint32_t> g_frameSwapUs{0};
std::atomic<uint32_t> g_frameWorldUs{0};
std::atomic<uint32_t> g_framePostUs{0};
std::atomic<uint32_t> g_frameAnimUs{0};
std::atomic<uint32_t> g_frameAnimEnts{0};
std::atomic<uint32_t> g_frameAnimBones{0};
std::atomic<uint32_t> g_frameAnimShed{0};
std::atomic<uint32_t> g_frameHostUs{0};
std::atomic<uint32_t> g_frameRenderThreadUs{0};
std::atomic<uint32_t> g_lastRenderThreadUs{0};
std::atomic<uint32_t> g_frameRmUpUs{0};
std::atomic<uint32_t> g_frameRmRxUs{0};
std::atomic<uint32_t> g_frameRmRxCnt{0};
std::atomic<uint32_t> g_frameRpcRxUs{0};
std::atomic<uint32_t> g_frameRpcRxCnt{0};
std::atomic<uint32_t> g_frameSlowPktUs{0};
std::atomic<uint32_t> g_frameSlowPktMeta{0}; // bits: [31]=isRpc, [30:23]=pktId, [22:0]=bytes
std::atomic<uint32_t> g_frameWallLuaUs{0};
std::atomic<uint32_t> g_frameEntWallUs{0};
std::atomic<uint32_t> g_frameEntUpdUs{0};
std::atomic<uint32_t> g_frameEntPostUs{0};
std::atomic<uint32_t> g_frameCacheRndUs{0};
std::atomic<uint32_t> g_frameUiDrawUs{0};
std::atomic<uint32_t> g_frameSoundUs{0};
std::atomic<uint32_t> g_frameSteamUs{0};
std::atomic<uint32_t> g_frameMsgUs{0};
std::atomic<float>    g_emaLuaUs{0.0f};
std::atomic<float>    g_emaNetUs{0.0f};
std::atomic<float>    g_emaAnimUs{0.0f};
std::atomic<float>    g_emaGcUs{0.0f};
std::atomic<float>    g_emaRenderThreadUs{0.0f};
std::atomic<float>    g_emaWallLuaUs{0.0f};
std::atomic<float>    g_emaEntWallUs{0.0f};
std::atomic<float>    g_emaEntUpdUs{0.0f};
std::atomic<float>    g_emaEntPostUs{0.0f};
std::atomic<float>    g_emaCacheRndUs{0.0f};
std::atomic<float>    g_emaUiDrawUs{0.0f};
std::atomic<float>    g_emaSoundUs{0.0f};

// Tear-free Hitch & Severe-Stall telemetry snapshot (protected by lightweight SRWLOCK only on >25ms frames & 10s flush)
struct HitchSnapshot {
    uint32_t pendingCount = 0;
    uint32_t severeCount  = 0; // frames >= 500ms (up to 60s)
    uint32_t severeMaxUs  = 0;
    uint32_t peakUs       = 0;
    uint32_t busyUs       = 0;
    uint32_t gpuDrainUs   = 0;
    uint32_t swapUs       = 0;
    uint32_t rtUs         = 0;
    uint32_t worldUs      = 0;
    uint32_t postUs       = 0;
    uint32_t unzUs        = 0;
    uint32_t shaderUs     = 0;
    uint32_t luaUs        = 0;
    uint32_t wallLuaUs    = 0;
    uint32_t entWallUs    = 0;
    uint32_t entUpdUs     = 0;
    uint32_t entPostUs    = 0;
    uint32_t cacheRndUs   = 0;
    uint32_t uiDrawUs     = 0;
    uint32_t soundUs      = 0;
    uint32_t steamUs      = 0;
    uint32_t msgUs        = 0;
    uint32_t netUs        = 0;
    uint32_t rmUpUs       = 0;
    uint32_t rmRxUs       = 0;
    uint32_t rmRxCnt      = 0;
    uint32_t rpcRxUs      = 0;
    uint32_t rpcRxCnt     = 0;
    uint32_t slowPktUs    = 0;
    uint32_t slowPktMeta  = 0;
    uint32_t animUs       = 0;
    uint32_t animEnts     = 0;
    uint32_t animBones    = 0;
    uint32_t animShed     = 0;
    uint32_t gcUs         = 0;
    int32_t  gcKb         = 0;
    uint8_t  gcFull       = 0;
    uint32_t texUs        = 0;
    uint32_t hostUs       = 0;
    int32_t  pingAvgMs    = -1;
    int32_t  pingLastMs   = -1;
    uint16_t lossX10      = 0;
    uint32_t resendMsgs   = 0;
};
SRWLOCK       g_hitchLock = SRWLOCK_INIT;
HitchSnapshot g_hitchSnap{};

SRWLOCK g_bbLock = SRWLOCK_INIT;
hs_blackbox::Ring<hs_blackbox::ClientEvent> g_clientBlackbox;

static inline uint64_t NowEpochMs() noexcept {
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    const uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
                       static_cast<uint64_t>(ft.dwLowDateTime);
    constexpr uint64_t kEpochDiff = 116444736000000000ULL;
    return (t > kEpochDiff) ? ((t - kEpochDiff) / 10000ULL) : 0ULL;
}

static uint8_t* FindUniqueAobInt16(uint8_t* textStart, size_t textSize, const int16_t* pat, size_t len);

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

    void PushFiltered(float us, bool hold) {
        AcquireSRWLockExclusive(&lock);
        if (!hold || count < 64) {
            samplesUs[writeIdx] = us;
            writeIdx = (writeIdx + 1) & (kCap - 1);
            if (count < kCap) ++count;
        }
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

static bool QueuePrologueTrampolineHook(
    std::vector<PendingCodePatch>& patches, uint8_t* fnAddr, size_t stealLen,
    const void* hookFn, void*& outOrigFn, const char* logText);

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
// Universal x86-64 True 240fps 2D Skeletal Animation Interpolation (v2.9.5-A)
// Covers ALL 3 channels: Main World + Overlay + UI AnimNode!
//   - A1: Cross-Clip Pose Crossfade (hs_skeletal::PoseCrossfadePool)
//   - A2: 4-Keyframe C1 Monotone PCHIP Spline (hs_skeletal::TweenElementMatrixSpline)
//------------------------------------------------------------------------------
using hs_skeletal::AnimElement;
using hs_skeletal::AnimFrame;
using hs_skeletal::Animation;
using hs_skeletal::HalfToFloat;
using hs_skeletal::FloatToHalf;
using hs_skeletal::WrapAngleDiff;
using hs_skeletal::TweenElementMatrix;

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
    float       hitstop_frozen_f = 0.0f;
    uint32_t    sim_gen = 0;
    uint8_t     hitstop_active = 0;
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
    const void* last_anim_ptr = nullptr;
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
    hs_world_pos::SmoothTimelineTable posTimeline;
    hs_skeletal::PoseCrossfadePool    posePool;

    // Unified Main-Thread (or fallback RT) frame clock for WorldPos + Camera + Shadow + Light lockstep
    uint64_t posClockSeq = 0;
    uint64_t fallbackPosSeq = 0;
    int64_t  posClockQpc = 0;
    double   posClockSec = 0.0;
    double   posClockDt  = 0.005;
    uint32_t lastWorldResetGen = 0u;

    // Frame-aware QPC caching, zero-LOCK batched counters & swarm hitch-proof load guard
    uint32_t lastFrameSeq = 0xFFFFFFFFu;
    int64_t  cachedQpc = 0;
    int64_t  frameAnimStartQpc = 0;
    int64_t  frameHookAccumQpc = 0;
    uint32_t qpcCountdown = 0;
    uint32_t frameSampleIdx = 0;
    uint32_t frameTweenEnts = 0;
    uint32_t frameTweenBones = 0;
    uint32_t frameShedCount = 0;
    uint8_t  frameShedLevel = 0; // 0=full quality, 1=fast linear only, 2=pos240-only skeletal shed

    int64_t  batchInterpFrames = 0;
    int64_t  batchElemsTweened = 0;
    int64_t  batchPass2Swaps = 0;
    int64_t  batchSplineBones = 0;
    int64_t  batchCrossfadeStarts = 0;
    int64_t  batchCrossfadeFrames = 0;
    int64_t  batchPosInterp = 0;
    int64_t  batchPosBypass = 0;
    int64_t  batchShedFrames = 0;
    int64_t  batchDeJitterHits = 0;

    ThreadLocalAnimState() : tracks(kTrackSlots) {}

    AnimBufferSlot& NextSlot(AnimChannel ch) {
        if (ch == AnimChannel::Main) return mainSlots[(mainRing++) & 1u];
        if (ch == AnimChannel::Overlay) return overlaySlots[(overlayRing++) & 7u];
        return uiSlots[(uiRing++) & 1u];
    }

    void FlushCounters() {
        if (batchInterpFrames)    { InterlockedAdd64(&g_animInterpFrames, batchInterpFrames);       batchInterpFrames = 0; }
        if (batchElemsTweened)    { InterlockedAdd64(&g_animElemsTweened, batchElemsTweened);       batchElemsTweened = 0; }
        if (batchPass2Swaps)      { InterlockedAdd64(&g_animPass2Swaps, batchPass2Swaps);           batchPass2Swaps = 0; }
        if (batchSplineBones)     { InterlockedAdd64(&g_animSplineBones, batchSplineBones);         batchSplineBones = 0; }
        if (batchCrossfadeStarts) { InterlockedAdd64(&g_animCrossfadeStarts, batchCrossfadeStarts); batchCrossfadeStarts = 0; }
        if (batchCrossfadeFrames) { InterlockedAdd64(&g_animCrossfadeFrames, batchCrossfadeFrames); batchCrossfadeFrames = 0; }
        if (batchPosInterp)       { InterlockedAdd64(&g_worldPosInterpCount, batchPosInterp);       batchPosInterp = 0; }
        if (batchPosBypass)       { InterlockedAdd64(&g_worldPosBypassCount, batchPosBypass);       batchPosBypass = 0; }
        if (batchShedFrames)      { InterlockedAdd64(&g_animSwarmShedFrames, batchShedFrames);      batchShedFrames = 0; }
        if (batchDeJitterHits)    { InterlockedAdd64(&g_deJitterHits, batchDeJitterHits);           batchDeJitterHits = 0; }
    }

    // Unified single-timestamp-per-frame clock for WorldPos / Camera / Shadow / Light:
    //   - On Main Thread: keyed by g_frameSeq (incremented once per FrameLimiter::Wait), so FollowCamera,
    //     LightMap, DynamicShadow, and AnimRenderCmd::AnimRenderCmd share bit-identical (seq, nowSec, dtSec)!
    //   - On Render Thread (fallback only): keyed by g_rtSwapSeq (incremented once per SwapBuffers).
    void GetPosClock(uint64_t& outSeq, double& outNowSec, double& outDtSec, bool forceRtClock = false) {
        uint64_t curSeq = 0ULL;
        if (forceRtClock || !g_frameLimiterHooked.load(std::memory_order_acquire)) {
            const uint32_t rtSeq = g_rtSwapSeq.load(std::memory_order_acquire);
            if (rtSeq != 0u) {
                curSeq = static_cast<uint64_t>(rtSeq) + 1ULL;
            } else {
                const int64_t now = NowQpc();
                if (posClockQpc == 0 || static_cast<double>(now - posClockQpc) * g_qpcInvFreq >= 0.0005) {
                    ++fallbackPosSeq;
                }
                curSeq = fallbackPosSeq ? fallbackPosSeq : 1ULL;
            }
        } else {
            curSeq = static_cast<uint64_t>(g_frameSeq.load(std::memory_order_acquire)) + 1ULL;
        }
        if (curSeq != posClockSeq || posClockQpc == 0) {
            const int64_t now = NowQpc();
            posClockDt  = (posClockQpc > 0) ? (static_cast<double>(now - posClockQpc) * g_qpcInvFreq) : 0.005;
            posClockQpc = now;
            posClockSec = static_cast<double>(now - g_qpcOrigin) * g_qpcInvFreq;
            posClockSeq = curSeq;
            FlushCounters();
        }
        outSeq    = posClockSeq;
        outNowSec = posClockSec;
        outDtSec  = posClockDt;
    }

    int64_t GetFrameQpc() {
        // Key Render-Thread skeletal tweening off Render-Thread g_rtSwapSeq (incremented in HookEglSwapBuffers),
        // NEVER Main-Thread g_frameSeq! Prevents <16-entity open-savanna 1-2 frame QPC freezes under
        // use_threaded_renderer=true, and NEVER mutates cachedQpc mid-frame!
        const DWORD rtTid = g_rtThreadId.load(std::memory_order_relaxed);
        const bool isRt = (rtTid != 0 && rtTid == GetCurrentThreadId());
        const uint32_t curSeq = isRt
            ? g_rtSwapSeq.load(std::memory_order_acquire)
            : g_frameSeq.load(std::memory_order_acquire);
        if (curSeq != lastFrameSeq || cachedQpc == 0) {
            const int64_t now = NowQpc();
            if (frameAnimStartQpc != 0 &&
                (frameHookAccumQpc > 0 || frameTweenEnts > 0 || frameTweenBones > 0 || frameShedCount > 0)) {
                const uint32_t hookUs = static_cast<uint32_t>(
                    std::clamp(static_cast<double>(frameHookAccumQpc) * g_qpcToUs, 0.0, 500000.0));
                g_lastAnimHookUs.store(hookUs, std::memory_order_relaxed);
                g_lastAnimEnts.store(frameTweenEnts, std::memory_order_relaxed);
                g_lastAnimBones.store(frameTweenBones, std::memory_order_relaxed);
                g_lastAnimShed.store(frameShedCount, std::memory_order_relaxed);
                g_frameAnimUs.fetch_add(hookUs, std::memory_order_relaxed);
                g_frameAnimEnts.fetch_add(frameTweenEnts, std::memory_order_relaxed);
                g_frameAnimBones.fetch_add(frameTweenBones, std::memory_order_relaxed);
                g_frameAnimShed.fetch_add(frameShedCount, std::memory_order_relaxed);
            }
            FlushCounters();
            lastFrameSeq = curSeq;
            cachedQpc = now;
            frameAnimStartQpc = now;
            frameHookAccumQpc = 0;
            qpcCountdown = 16;
            frameSampleIdx = 0;
            frameTweenEnts = 0;
            frameTweenBones = 0;
            frameShedCount = 0;
            frameShedLevel = 0;
            return now;
        }
        if (qpcCountdown == 0) {
            qpcCountdown = 16;
            const double workUs = static_cast<double>(frameHookAccumQpc) * g_qpcToUs;
            if (workUs > 4000.0 || frameTweenEnts >= 1800) {
                frameShedLevel = 2;
            } else if (workUs > 2200.0 || frameTweenEnts >= 900) {
                frameShedLevel = 1;
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
    ThreadLocalAnimState& tls = *g_tlsAnimState;
    const uint32_t resetGen = g_worldResetGen.load(std::memory_order_acquire);
    if (tls.lastWorldResetGen != resetGen) {
        tls.lastWorldResetGen = resetGen;
        tls.posTimeline.Clear();
        for (auto& t : tls.tracks) {
            t.valid = 0;
            t.entity_key = 0;
            t.anim = nullptr;
        }
        new (&tls.posePool) hs_skeletal::PoseCrossfadePool();
        tls.posClockSeq = 0;
        tls.posClockQpc = 0;
        tls.fallbackPosSeq = 0;
    }
    return tls;
}

// Unified Main-Thread (with automatic RT fallback) 240FPS PLL Snapshot-Timeline world-position smoother:
//   1) AnimRenderCmd::AnimRenderCmd (Main Thread @ 0x1400fae80 -> characters, mobs, projectiles)
//   2) DynamicShadow (Main Thread @ 0x140090a27 -> co-phase ground shadows)
//   3) LightMap::GenerateRenderData (Main Thread @ 0x140026291 -> moving torches/lanterns/fireflies)
//   4) FollowCamera (Main Thread via HyperSmooth_SmoothCameraTarget -> 0.00px camera/character lockstep!)
static bool InterpolateWorldPosCore(float* pX, float* pY, float* pZ, uintptr_t key,
                                    ThreadLocalAnimState& tls, int64_t /*nowQpc*/, bool countAsAnim,
                                    const void* animPtr = nullptr, bool forceRtClock = false) {
    if (!pX || !pY || !pZ || key == 0) return false;
    if (g_worldPosInterpEnabled.load(std::memory_order_relaxed) == 0 ||
        g_skeletalTweenEnabled.load(std::memory_order_relaxed) == 0) {
        return false;
    }

    uint64_t frameSeq = 0;
    double nowSec = 0.0, dtSec = 0.005;
    tls.GetPosClock(frameSeq, nowSec, dtSec, forceRtClock);

    const int curSimHz = g_clientSimTickRate.load(std::memory_order_relaxed);
    const uint32_t curSimGen = g_clientSimTickGen.load(std::memory_order_relaxed);
    const bool optDeJitter = g_optAdaptiveDeJitter.load(std::memory_order_relaxed);

    float outX = *pX, outY = *pY, outZ = *pZ;
    hs_world_pos::SmoothStats st{};
    const bool smoothed = tls.posTimeline.Evaluate(
        key, *pX, *pY, *pZ,
        frameSeq, nowSec, dtSec,
        curSimHz, curSimGen,
        animPtr, optDeJitter,
        outX, outY, outZ, &st);

    if (st.deJittered) {
        ++tls.batchDeJitterHits;
        uint32_t prevPeak = g_deJitterPeakUs.load(std::memory_order_relaxed);
        while (st.jitterUs > prevPeak &&
               !g_deJitterPeakUs.compare_exchange_weak(prevPeak, st.jitterUs, std::memory_order_relaxed)) {}
    }
    if (countAsAnim) {
        if (st.bypassed) {
            ++tls.batchPosBypass;
        } else if (smoothed) {
            ++tls.batchPosInterp;
        }
    }
    if (!smoothed) return false;

    *pX = outX;
    *pY = outY;
    *pZ = outZ;
    return true;
}

static inline bool IsGridPlacerAnim(uint32_t layer, uint32_t orientMode, const void* animPtr) noexcept {
    if (orientMode != 1u || animPtr == nullptr) return false; // OnGround only
    if (layer < 3u || layer > 5u) return false;               // LAYER_WORLD_BACKGROUND(3)..LAYER_WORLD_CEILING(5)
    const auto* anim = static_cast<const Animation*>(animPtr);
    if (anim->num_frames == 0u || anim->frames == nullptr) return false;
    const AnimFrame& f0 = anim->frames[0];
    if (f0.num_elements == 0u || f0.elements == nullptr) return false;
    const uint32_t sym0 = f0.elements[0].symbol_hash;
    // Vanilla gridplacer.zip (0x006719C9), Geometric Placement buildgridplacer.zip (0x7DE50C20) & geo_gridplacer.zip (0x98AD297F)
    return (sym0 == 0x006719C9u || sym0 == 0x7DE50C20u || sym0 == 0x98AD297Fu);
}

static void InterpolateAnimCmdWorldPos(uint8_t* cmd, const void* animPtr, uintptr_t key,
                                       ThreadLocalAnimState& tls, int64_t nowQpc, bool forceRtClock = false) {
    if (!cmd || key == 0) return;
    // [cmd+0x60] == layer (verified @ 0x1400F49C9: cmp [rbx+60h], 8):
    // Skip LAYER_WORLD_DEBUG (7), LAYER_FRONTEND (8), LAYER_FRONTEND_DEBUG (9)
    const uint32_t layer = *reinterpret_cast<const uint32_t*>(cmd + 0x60);
    if (layer >= 7u) return;

    // [cmd+0x64] == orientation (verified @ 0x1400F49D3: 0=BillBoard, 1=OnGround, 2=OnGroundFixed):
    // Skip official & Geometric Placement gridplacer decals (Layer 3..5 OnGround).
    const uint32_t orientMode = *reinterpret_cast<const uint32_t*>(cmd + 0x64);
    if (IsGridPlacerAnim(layer, orientMode, animPtr)) {
        return;
    }

    // 4x4 row-major World Transform Matrix at [cmd + 0x18]:
    // Translation column: X = +0x24 (+0x0C), Y = +0x34 (+0x1C), Z = +0x44 (+0x2C)
    InterpolateWorldPosCore(
        reinterpret_cast<float*>(cmd + 0x24),
        reinterpret_cast<float*>(cmd + 0x34),
        reinterpret_cast<float*>(cmd + 0x44),
        key, tls, nowQpc, true, animPtr, forceRtClock);
}

static void* HookAnimRenderCmdCtor(void* cmdMem, void* animNode, void* worldMtx) {
    void* cmd = g_origAnimRenderCmdCtor ? g_origAnimRenderCmdCtor(cmdMem, animNode, worldMtx) : cmdMem;
    if (cmd && animNode) {
        auto* bytes = static_cast<uint8_t*>(cmd);
        const uintptr_t entityKey = reinterpret_cast<uintptr_t>(animNode);
        const float rawX = *reinterpret_cast<const float*>(bytes + 0x24);
        const float rawY = *reinterpret_cast<const float*>(bytes + 0x34);
        const float rawZ = *reinterpret_cast<const float*>(bytes + 0x44);
        if (g_worldPosInterpEnabled.load(std::memory_order_relaxed) != 0 &&
            g_skeletalTweenEnabled.load(std::memory_order_relaxed) != 0) {
            const void* animPtr = *reinterpret_cast<const void* const*>(bytes + 0xB0);
            ThreadLocalAnimState& tls = GetTlsAnimState();
            InterpolateAnimCmdWorldPos(bytes, animPtr, entityKey, tls, 0, false);
        }
        if (g_combatImpact.ActiveImpulseCount() > 0u) {
            g_combatImpact.BindAndApplyOnMainThread(bytes, entityKey, rawX, rawY, rawZ, NowQpc(), g_qpcInvFreq);
        }
    }
    return cmd;
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
                                      int64_t optNowQpc = 0, const float* curAxisX = nullptr) {
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
    // Percentage meters & discrete state overlays (SetPercent -> playMode == 2: UI status_meter,
    // world nutrients_overlay / farm_soil_moisture, crop, rainometer, winterometer) step discretely
    // by design; cross-frame interpolation + half-way frame_num swaps cause visible flicker.
    if (playMode == 2) {
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
    const float crossfadeSec = (channel == AnimChannel::Main)
        ? (std::clamp(g_animCrossfadeMs.load(std::memory_order_relaxed), 0.0f, 120.0f) * 0.001f)
        : 0.0f;
    const float splineStr = std::clamp(g_animSplineStrength.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const bool fastOnly = (tls.frameShedLevel >= 1 && channel != AnimChannel::UI);

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
    const bool wasSameEntity = (tr.valid && tr.entity_key == key);
    const int64_t prevAnimSeenQpc = tr.seen_qpc;
    const double animUnseenSec = wasSameEntity
        ? (static_cast<double>(nowQpc - prevAnimSeenQpc) * g_qpcInvFreq)
        : 999.0;
    tr.seen_qpc = nowQpc;

    auto discreteFrame = [&]() -> const AnimFrame* {
        float safeF = (curF >= 0.0f && std::isfinite(curF)) ? curF : 0.0f;
        uint32_t fi = static_cast<uint32_t>(safeF + 0.001f);
        if (fi >= nframes) fi = (playMode == 1) ? (fi % nframes) : (nframes - 1);
        return &anim->frames[fi];
    };

    // A1 Cross-Clip Pose Crossfade finalize wrapper:
    //   - Applied on EVERY normal return so crossfades never flicker when frac hits 0.0/1.0!
    //   - Copies read-only anim->frames[] into a writable AnimBufferSlot before blending.
    //   - Records final blended pose so rapid chained clip switches stay C0-continuous!
    auto finalizeFrame = [&](const AnimFrame* outFrame, bool isWritable) -> const void* {
        if (!outFrame) return nullptr;
        if (channel != AnimChannel::Main || crossfadeSec < 0.005f) return outFrame;
        if (!outFrame->elements || outFrame->num_elements == 0 ||
            outFrame->num_elements > hs_skeletal::kMaxPoseBones) {
            return outFrame;
        }
        hs_skeletal::PoseCrossfadeSlot* pslot = tls.posePool.FindOrAlloc(key, nowQpc);
        if (!pslot) return outFrame;

        const AnimFrame* finalOut = outFrame;
        if (pslot->blending) {
            AnimElement* wElems = nullptr;
            if (isWritable) {
                wElems = const_cast<AnimElement*>(outFrame->elements);
            } else {
                AnimBufferSlot& cslot = tls.NextSlot(channel);
                cslot.frame = *outFrame;
                cslot.frame.elements = cslot.elems;
                memcpy(cslot.elems, outFrame->elements, outFrame->num_elements * sizeof(AnimElement));
                wElems = cslot.elems;
                finalOut = &cslot.frame;
            }
            const uint32_t blended = hs_skeletal::PoseCrossfadePool::ApplyCrossfade(
                *pslot, wElems, finalOut->num_elements, nowQpc, g_qpcInvFreq, curAxisX, fastOnly);
            if (blended > 0u) {
                ++tls.batchCrossfadeFrames;
            }
        }
        hs_skeletal::PoseCrossfadePool::RecordPose(*pslot, *finalOut, nowQpc, curAxisX);
        return finalOut;
    };

    const uint32_t curSimGen = g_clientSimTickGen.load(std::memory_order_relaxed);
    const int curSimHz = g_clientSimTickRate.load(std::memory_order_relaxed);
    if (!wasSameEntity || tr.anim != animPtr) {
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
        tr.hitstop_active = 0;
        tr.valid = 1;
        const AnimFrame* df = discreteFrame();
        if (wasSameEntity && channel == AnimChannel::Main && crossfadeSec >= 0.005f && !fastOnly && df) {
            if (hs_skeletal::PoseCrossfadeSlot* pslot = tls.posePool.Find(key)) {
                if (hs_skeletal::PoseCrossfadePool::TryStartCrossfade(
                        *pslot, *df, nowQpc, crossfadeSec, animUnseenSec, curAxisX)) {
                    ++tls.batchCrossfadeStarts;
                }
            }
        }
        return finalizeFrame(df, false);
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
        return finalizeFrame(discreteFrame(), false);
    }

    float dtSinceTick = static_cast<float>(static_cast<double>(nowQpc - tr.seg_start_qpc) * g_qpcInvFreq);
    float alpha = (tr.tick_period > 1e-4f) ? (dtSinceTick / tr.tick_period) : 1.0f;
    if (!std::isfinite(alpha) || alpha < 0.0f) alpha = 0.0f;
    if (alpha >= 1.0f) {
        alpha = 1.0f;
        tr.prev_f = tr.curr_f;
    }

    float fNow = tr.prev_f + stepF * alpha;
    if (!std::isfinite(fNow)) {
        tr.last_rendered_f = curF;
        return finalizeFrame(discreteFrame(), false);
    }
    if (isWrappingSeg) {
        fNow = fmodf(fNow, nf);
        if (fNow < 0.0f) fNow += nf;
    } else if (fNow < 0.0f) {
        fNow = 0.0f;
    } else if (fNow > nf - 1.0f) {
        fNow = nf - 1.0f;
    }
    if (channel == AnimChannel::Main && g_combatImpact.IsInHitStop(key, nowQpc)) {
        if (!tr.hitstop_active) {
            tr.hitstop_active = 1;
            tr.hitstop_frozen_f = fNow;
        } else {
            fNow = tr.hitstop_frozen_f;
        }
    } else if (tr.hitstop_active) {
        tr.hitstop_active = 0;
        // Re-anchor smoothly from frozen pose toward live sim pose (0ms gameplay lag!)
        tr.prev_f = tr.hitstop_frozen_f;
        tr.seg_start_qpc = nowQpc;
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
        if (fNow >= nf - 1.0f) return finalizeFrame(&anim->frames[nframes - 1], false);
        float fl = floorf(fNow);
        idx0 = static_cast<uint32_t>(fl);
        idx1 = idx0 + 1;
        frac = fNow - fl;
    }

    if (idx0 >= nframes) idx0 = nframes - 1;
    if (idx1 >= nframes) idx1 = nframes - 1;
    if (frac <= 0.015f) return finalizeFrame(&anim->frames[idx0], false);
    if (frac >= 0.985f) return finalizeFrame(&anim->frames[idx1], false);
    if (idx0 == idx1) {
        return finalizeFrame(&anim->frames[idx0], false);
    }

    const AnimFrame& f0 = anim->frames[idx0];
    const AnimFrame& f1 = anim->frames[idx1];
    uint32_t n0 = f0.num_elements, n1 = f1.num_elements;
    if (n0 == 0 || !f0.elements || n1 == 0 || !f1.elements || n0 > kMaxAnimElements || n1 > kMaxAnimElements) {
        return finalizeFrame(&f0, false);
    }
    if (n0 == n1 && n0 <= 16u) {
        bool allIdentical = true;
        for (uint32_t k = 0; k < n0; ++k) {
            if (f0.elements[k].symbol_hash != f1.elements[k].symbol_hash ||
                f0.elements[k].layer_hash  != f1.elements[k].layer_hash  ||
                f0.elements[k].frame_num   != f1.elements[k].frame_num   ||
                memcmp(f0.elements[k].m, f1.elements[k].m, 12) != 0) {
                allIdentical = false;
                break;
            }
        }
        if (allIdentical) {
            if (channel != AnimChannel::Main || crossfadeSec < 0.005f) {
                return &f0;
            }
            hs_skeletal::PoseCrossfadeSlot* pExist = tls.posePool.Find(key);
            if (!pExist || !pExist->blending) {
                return &f0;
            }
            return finalizeFrame(&f0, false);
        }
    }

    const int64_t workStartQpc = ((tls.frameSampleIdx++ & 15u) == 0u) ? NowQpc() : 0;

    AnimBufferSlot& bufSlot = tls.NextSlot(channel);
    bufSlot.frame = f0;
    bufSlot.frame.elements = bufSlot.elems;
    memcpy(bufSlot.elems, f0.elements, n0 * sizeof(AnimElement));

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
    uint8_t uniqueRigid[kMaxAnimElements];
    uint64_t usedF1[kMaxAnimElements / 64] = { 0 }; // 512 bits (matches kMaxAnimElements)

    memset(headLS, -1, sizeof(headLS));
    memset(f0BucketCnt, 0, sizeof(f0BucketCnt));
    memset(matchedF1, -1, n0 * sizeof(int16_t));
    memset(f0Processed, 0, n0 * sizeof(uint8_t));
    memset(uniqueRigid, 0, n0 * sizeof(uint8_t));

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
            const bool partLayer = isParticleLayer(lhash);
            if (d2 <= kDupMaxDistSq || !partLayer) {
                matchedF1[i] = static_cast<int16_t>(firstJ);
                markUsedF1(static_cast<uint32_t>(firstJ));
                if (!partLayer) uniqueRigid[i] = 1;
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

            // Detect zero-mutation rigid multi-part buckets (e.g. Deadly Brightshade head_parts/vine_parts,
            // Poison Birchnut, Deerclops, Evergreen tiers) vs flipbook/cycling particle swarms (e.g. fireflies):
            //   1) Distinct frame_num set is identical between f0 and f1 (zero part-ID creation/extinction)
            //   2) Multiset L1 count delta == |m0 - m1| <= 1 (zero fn->fn+1 or mod-N flipbook shifts!)
            //   3) At least 2 distinct static part-IDs in the bucket
            bool isRigidPartBucket = false;
            {
                const uint32_t dm = (m0 >= m1) ? (m0 - m1) : (m1 - m0);
                if (dm <= 1u) {
                    uint32_t uniqFns[kMaxDupBucket];
                    uint32_t uniqCnt = 0;
                    uint32_t l1Delta = 0;
                    bool setMatches = true;
                    for (uint32_t a = 0; a < m0; ++a) {
                        const uint32_t fa = fn0[a];
                        bool seen = false;
                        for (uint32_t u = 0; u < uniqCnt; ++u) {
                            if (uniqFns[u] == fa) { seen = true; break; }
                        }
                        if (seen) continue;
                        uniqFns[uniqCnt++] = fa;
                        uint32_t c0 = 0, c1 = 0;
                        for (uint32_t k = 0; k < m0; ++k) { if (fn0[k] == fa) ++c0; }
                        for (uint32_t k = 0; k < m1; ++k) { if (fn1[k] == fa) ++c1; }
                        if (c1 == 0u) { setMatches = false; break; }
                        l1Delta += (c0 >= c1) ? (c0 - c1) : (c1 - c0);
                    }
                    if (setMatches) {
                        for (uint32_t b = 0; b < m1; ++b) {
                            const uint32_t fb = fn1[b];
                            bool inSet = false;
                            for (uint32_t u = 0; u < uniqCnt; ++u) {
                                if (uniqFns[u] == fb) { inSet = true; break; }
                            }
                            if (!inSet) { setMatches = false; break; }
                        }
                    }
                    isRigidPartBucket = (setMatches && uniqCnt >= 2u && l1Delta == dm);
                }
            }

            // Step B1: Safe exact-(layer,sym,fn) lock:
            //   - Rigid multi-part buckets (Deadly Brightshade / Birchnut / Deerclops / Evergreen):
            //     allow up to 280px/frame without proximity veto (prevents 30fps freezing & cross-part spinning!).
            //   - Particle/flipbook buckets (fireflies / sparks / leaves): keep strict <=48px & minD2+16px^2 guard!
            constexpr float kRigidPartMaxDistSq = 280.0f * 280.0f;
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
                if (isRigidPartBucket) {
                    if (d2Ex <= kRigidPartMaxDistSq) {
                        locMatch[a] = static_cast<int16_t>(exB);
                        locUsed1[exB] = 1;
                    }
                    continue;
                }
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

    // Pass 2: Cross-Layer Orbit Handoff (e.g. Enlightened Crown `hat_alterguardian_equipped`
    // activate_loop where orbiting crown shards step ~1.9px while crossing between front & back
    // layers). Only considers still-unmatched elements with exact same symbol_hash, consistent
    // determinant sign, and tight <=20px proximity (zero false matches on fireflies/ripples).
    constexpr float kCrossLayerMaxDistSq = 20.0f * 20.0f;
    for (;;) {
        float bestD2 = kCrossLayerMaxDistSq + 1.0f;
        int32_t bestI = -1, bestJ = -1;
        for (uint32_t i = 0; i < n0; ++i) {
            if (matchedF1[i] >= 0) continue;
            const AnimElement& e0 = f0.elements[i];
            const float x0i = HalfToFloat(e0.m[4]), y0i = HalfToFloat(e0.m[5]);
            const float d0i = elemDet(e0.m);
            for (uint32_t j = 0; j < n1; ++j) {
                if (isUsedF1(j)) continue;
                const AnimElement& e1 = f1.elements[j];
                if (e1.symbol_hash != e0.symbol_hash) continue;
                if (d0i * elemDet(e1.m) < 0.0f) continue;
                const float dx = HalfToFloat(e1.m[4]) - x0i;
                const float dy = HalfToFloat(e1.m[5]) - y0i;
                const float d2 = dx * dx + dy * dy;
                if (d2 <= kCrossLayerMaxDistSq && d2 < bestD2) {
                    bestD2 = d2;
                    bestI = static_cast<int32_t>(i);
                    bestJ = static_cast<int32_t>(j);
                }
            }
        }
        if (bestI < 0) break;
        matchedF1[bestI] = static_cast<int16_t>(bestJ);
        markUsedF1(static_cast<uint32_t>(bestJ));
    }

    // A2: Optional 4-keyframe C1 Monotone PCHIP Spline neighbor lookup (fM1, fP2)
    //     Strictly limited to unique rigid 1-to-1 bones (never particle swarms!) and auto-bypassed
    //     under swarm load shedding (fastOnly) or splineStr == 0.
    const bool useSpline = (splineStr > 0.001f && !fastOnly && nframes >= 3u);
    const AnimFrame* fM1 = nullptr;
    const AnimFrame* fP2 = nullptr;
    int16_t headM1[kHashSize];
    int16_t nextM1[kMaxAnimElements];
    int16_t headP2[kHashSize];
    int16_t nextP2[kMaxAnimElements];
    if (useSpline) {
        const uint32_t idxM1 = isWrappingSeg ? ((idx0 + nframes - 1u) % nframes) : ((idx0 > 0u) ? (idx0 - 1u) : idx0);
        const uint32_t idxP2 = isWrappingSeg ? ((idx1 + 1u) % nframes) : ((idx1 + 1u < nframes) ? (idx1 + 1u) : idx1);
        if (idxM1 != idx0 && idxM1 != idx1) {
            const AnimFrame& cand = anim->frames[idxM1];
            if (cand.elements && cand.num_elements > 0u && cand.num_elements <= kMaxAnimElements) {
                fM1 = &cand;
                memset(headM1, -1, sizeof(headM1));
                for (int32_t k = static_cast<int32_t>(cand.num_elements) - 1; k >= 0; --k) {
                    const uint32_t h = ((cand.elements[k].layer_hash * 2654435761u) ^ cand.elements[k].symbol_hash) & kHashMask;
                    nextM1[k] = headM1[h];
                    headM1[h] = static_cast<int16_t>(k);
                }
            }
        }
        if (idxP2 != idx1 && idxP2 != idx0) {
            const AnimFrame& cand = anim->frames[idxP2];
            if (cand.elements && cand.num_elements > 0u && cand.num_elements <= kMaxAnimElements) {
                fP2 = &cand;
                memset(headP2, -1, sizeof(headP2));
                for (int32_t k = static_cast<int32_t>(cand.num_elements) - 1; k >= 0; --k) {
                    const uint32_t h = ((cand.elements[k].layer_hash * 2654435761u) ^ cand.elements[k].symbol_hash) & kHashMask;
                    nextP2[k] = headP2[h];
                    headP2[h] = static_cast<int16_t>(k);
                }
            }
        }
    }

    auto findUniqueNeighborM = [&](const AnimFrame* fn, const int16_t* heads, const int16_t* nexts,
                                   uint32_t lhash, uint32_t shash) -> const uint16_t* {
        if (!fn) return nullptr;
        const uint32_t h = ((lhash * 2654435761u) ^ shash) & kHashMask;
        int32_t hit = -1;
        for (int16_t k = heads[h]; k >= 0; k = nexts[k]) {
            const AnimElement& ek = fn->elements[k];
            if (ek.symbol_hash == shash && ek.layer_hash == lhash) {
                if (hit >= 0) return nullptr; // duplicate -> bail cleanly
                hit = k;
            }
        }
        return (hit >= 0) ? fn->elements[hit].m : nullptr;
    };

    uint32_t tweenedCount = 0;
    uint32_t pass2Count = 0;
    uint32_t splineCount = 0;
    for (uint32_t i = 0; i < n0; ++i) {
        const int16_t mj = matchedF1[i];
        if (mj < 0) continue;
        const AnimElement& e0 = f0.elements[i];
        const AnimElement& e1 = f1.elements[mj];
        const bool isPass2 = (e1.frame_num != e0.frame_num);
        if (isPass2 && fastOnly) continue;
        bool okTween = false;
        if (useSpline && uniqueRigid[i] && (fM1 || fP2)) {
            const uint16_t* mM1 = findUniqueNeighborM(fM1, headM1, nextM1, e0.layer_hash, e0.symbol_hash);
            const uint16_t* mP2 = findUniqueNeighborM(fP2, headP2, nextP2, e0.layer_hash, e0.symbol_hash);
            bool usedSpl = false;
            okTween = hs_skeletal::TweenElementMatrixSpline(
                mM1, e0.m, e1.m, mP2, frac, splineStr, bufSlot.elems[i].m, fastOnly, &usedSpl);
            if (okTween && usedSpl) ++splineCount;
        } else {
            okTween = TweenElementMatrix(e0.m, e1.m, frac, bufSlot.elems[i].m, fastOnly);
        }
        if (okTween) {
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
        // Sampled per-16-entity work estimator; clamp any single-entity OS-preemption / complex-FX
        // outlier to <=250us before x16 scaling so one spike cannot falsely trip Level-2 shedding.
        int64_t deltaQpc = NowQpc() - workStartQpc;
        const int64_t maxSampleQpc = static_cast<int64_t>(250.0 / g_qpcToUs);
        if (deltaQpc > maxSampleQpc && maxSampleQpc > 0) deltaQpc = maxSampleQpc;
        if (deltaQpc > 0) tls.frameHookAccumQpc += deltaQpc * 16;
    }

    if (tweenedCount > 0) {
        ++tls.frameTweenEnts;
        tls.frameTweenBones += tweenedCount;
        ++tls.batchInterpFrames;
        tls.batchElemsTweened += static_cast<int64_t>(tweenedCount);
        if (pass2Count > 0) tls.batchPass2Swaps += static_cast<int64_t>(pass2Count);
        if (splineCount > 0) tls.batchSplineBones += static_cast<int64_t>(splineCount);
    }
    return finalizeFrame(&bufSlot.frame, true);
}

extern "C" const void* FastInterpGetFrameMain(const void* animPtr, int32_t playMode, float time, const void* animRenderCmd) {
    if (!animRenderCmd) {
        return g_origAnimGetFrame ? g_origAnimGetFrame(animPtr, playMode, time) : nullptr;
    }
    auto* cmd = const_cast<uint8_t*>(static_cast<const uint8_t*>(animRenderCmd));
    const uint32_t cmdLayer  = *reinterpret_cast<const uint32_t*>(cmd + 0x60);
    const uint32_t cmdOrient = *reinterpret_cast<const uint32_t*>(cmd + 0x64);
    if (IsGridPlacerAnim(cmdLayer, cmdOrient, animPtr)) {
        return g_origAnimGetFrame ? g_origAnimGetFrame(animPtr, playMode, time) : nullptr;
    }
    const void* persistentAnimNode = *reinterpret_cast<const void* const*>(cmd + 8);
    ThreadLocalAnimState& tls = GetTlsAnimState();
    const int64_t nowQpc = tls.GetFrameQpc();
    if (persistentAnimNode) {
        const uintptr_t entityKey = reinterpret_cast<uintptr_t>(persistentAnimNode);
        g_tlsLastParentKey = entityKey;
        // Fallback only when Main-Thread AnimRenderCmd::AnimRenderCmd hook isn't active.
        // Under normal operation, HookAnimRenderCmdCtor has ALREADY interpolated [cmd+0x18..0x57]
        // on the Main Thread in 0.00px lockstep with FollowCamera, DynamicShadow, and LightMap!
        if (!g_animCmdCtorHooked.load(std::memory_order_relaxed)) {
            InterpolateAnimCmdWorldPos(cmd, animPtr, entityKey, tls, nowQpc, true);
        }
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
    // WorldMtx row-major at [cmd + 0x18]: Column 0 = transformed local X basis (detects Left<->Right mirror flip)
    const auto* wm = reinterpret_cast<const float*>(cmd + 0x18);
    const float axisX[3] = { wm[0], wm[4], wm[8] };
    return InterpGetFrameCore(animPtr, playMode, time, reinterpret_cast<uintptr_t>(persistentAnimNode),
                              AnimChannel::Main, &tls, nowQpc, axisX);
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

static int HookClientUncompress(uint8_t* dest, uint32_t* destLen, const uint8_t* src, uint32_t srcLen) {
    if (dest && destLen && src && srcLen > 0 && *destLen > 0) {
        static thread_local struct DecWrap {
            libdeflate_decompressor* d = nullptr;
            ~DecWrap() { if (d) libdeflate_free_decompressor(d); }
        } tl_dec;
        if (!tl_dec.d) tl_dec.d = libdeflate_alloc_decompressor();
        if (tl_dec.d) {
            const int64_t t0 = NowQpc();
            size_t actualOut = 0;
            const auto res = libdeflate_zlib_decompress(
                tl_dec.d, src, static_cast<size_t>(srcLen),
                dest, static_cast<size_t>(*destLen), &actualOut);
            if (res == LIBDEFLATE_SUCCESS && actualOut <= 0xFFFFFFFFULL) {
                const int64_t t1 = NowQpc();
                if (t1 > t0 && g_qpcToUs > 0.0) {
                    const uint32_t uUs = static_cast<uint32_t>(std::min(static_cast<double>(t1 - t0) * g_qpcToUs, 500000.0));
                    if (uUs > 0) g_frameUnzUs.fetch_add(uUs, std::memory_order_relaxed);
                }
                *destLen = static_cast<uint32_t>(actualOut);
                InterlockedIncrement64(&g_clientUncompCalls);
                InterlockedAdd64(&g_clientUncompBytes, static_cast<LONG64>(actualOut));
                return 0; // Z_OK
            }
        }
    }
    return g_origClientUncompress ? g_origClientUncompress(dest, destLen, src, srcLen) : -3;
}

void PrepareClientUncompressHook(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    // Unique 1-of-1 AOB for Client zlib::uncompress (14-byte zero-RIP-rel prologue @ 0x1403d78e0)
    static constexpr int16_t kAobClientUncompress[] = {
        0x40, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x8B, 0x02, 0x48, 0x8B, 0xFA,
        0x4C, 0x89, 0x44, 0x24, 0x20, 0x89, 0x44, 0x24, 0x38, 0x33, 0xC0, 0x48, 0x89, 0x4C, 0x24, 0x30
    };
    uint8_t* pUncomp = FindUniqueAobInt16(textStart, textSize, kAobClientUncompress,
                                          sizeof(kAobClientUncompress) / sizeof(kAobClientUncompress[0]));
    if (!pUncomp) {
        LogMsg("[FASTLOAD] Client zlib::uncompress skipped (AOB miss; fail-closed)");
        return;
    }
    void* origFn = nullptr;
    char msg[192];
    snprintf(msg, sizeof(msg), "Hooked Client zlib::uncompress @ %p -> SIMD libdeflate v1.23",
             static_cast<void*>(pUncomp));
    if (QueuePrologueTrampolineHook(patches, pUncomp, 14, reinterpret_cast<const void*>(&HookClientUncompress), origFn, msg)) {
        g_origClientUncompress = reinterpret_cast<ClientUncompress_t>(origFn);
    }
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
    bool ranEmergencyFull = false;
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
        g_frameGcFull.store(1, std::memory_order_relaxed);
        ranEmergencyFull = true;
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
                const uint32_t earlyUs = ranEmergencyFull
                    ? static_cast<uint32_t>(std::clamp(static_cast<double>(NowQpc() - nowQpc) * g_qpcToUs, 0.0, 60000000.0))
                    : 0u;
                g_lastGcUs.store(earlyUs, std::memory_order_relaxed);
                if (earlyUs > 0) g_frameGcUs.fetch_add(earlyUs, std::memory_order_relaxed);
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
        std::clamp(static_cast<double>(NowQpc() - nowQpc) * g_qpcToUs, 0.0, 60000000.0));
    g_lastGcUs.store(gcUs, std::memory_order_relaxed);
    g_frameGcUs.fetch_add(gcUs, std::memory_order_relaxed);
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

static void* WINAPI NativeEglGetCurrentContextProxy() {
    return g_wglRt.pWglGetCurrentContext ? reinterpret_cast<void*>(g_wglRt.pWglGetCurrentContext()) : nullptr;
}

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
        if (g_nativeGlActive.load(std::memory_order_acquire)) {
            if (g_wglRt.pWglGetCurrentContext && g_fnGlFinishNative) {
                pEglGetCurrentContext = &NativeEglGetCurrentContextProxy;
                pGlFinish = g_fnGlFinishNative;
                return true;
            }
            return false;
        }
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
    uint32_t frameCpuBusyUs = 0;
    uint32_t frameGpuDrainUs = 0;
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

            const int64_t preGpuDrainQpc = nowQpc;
            if (baseStart > 0 && preGpuDrainQpc >= baseStart) {
                const float cpuBusyUs = static_cast<float>((preGpuDrainQpc - baseStart) * g_qpcToUs);
                UpdateAtomicEma(g_emaFrameBusyUs, cpuBusyUs, 0.06f);
                frameCpuBusyUs = static_cast<uint32_t>(std::clamp(cpuBusyUs, 0.0f, 60000000.0f));
                g_lastFrameBusyUs.store(frameCpuBusyUs, std::memory_order_relaxed);
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
                    const double drainUsD = static_cast<double>(nowQpc - tGpu0) * g_qpcToUs;
                    frameGpuDrainUs = static_cast<uint32_t>(std::clamp(drainUsD, 0.0, 60000000.0));
                    UpdateAtomicEma(g_emaGpuDrainUs, static_cast<float>(drainUsD), 0.06f);
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
            if (baseStart > 0 && nowQpc >= baseStart) {
                const float cpuBusyUs = static_cast<float>((nowQpc - baseStart) * g_qpcToUs);
                UpdateAtomicEma(g_emaFrameBusyUs, cpuBusyUs, 0.06f);
                frameCpuBusyUs = static_cast<uint32_t>(std::clamp(cpuBusyUs, 0.0f, 60000000.0f));
                g_lastFrameBusyUs.store(frameCpuBusyUs, std::memory_order_relaxed);
            }
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
            const float cpuBusyUs = static_cast<float>((nowQpc - fl->startQpc) * g_qpcToUs);
            UpdateAtomicEma(g_emaFrameBusyUs, cpuBusyUs, 0.06f);
            frameCpuBusyUs = static_cast<uint32_t>(std::clamp(cpuBusyUs, 0.0f, 60000000.0f));
            g_lastFrameBusyUs.store(frameCpuBusyUs, std::memory_order_relaxed);
        }
        tls.nextDeadlineQpc = 0;
    }

    // Publish exact frame start & sequence for RenderThread/GC/TexStream + 1% Low ring & Hitch breakdown
    g_frameStartQpc.store(nowQpc, std::memory_order_release);
    const uint32_t completedFrameSeq = g_frameSeq.fetch_add(1, std::memory_order_release);
    const uint32_t frameUnzUs       = g_frameUnzUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameShaderUs    = g_frameShaderUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameLuaUs       = g_frameLuaUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameNetUs       = g_frameNetUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameGcUs        = g_frameGcUs.exchange(0, std::memory_order_acq_rel);
    const uint8_t  frameGcFull      = (g_frameGcFull.exchange(0, std::memory_order_acq_rel) != 0) ? 1u : 0u;
    const uint32_t frameTexUs       = g_frameTexUploadUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameSwapUs      = g_frameSwapUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameWorldUs     = g_frameWorldUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t framePostUs      = g_framePostUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameAnimUs      = g_frameAnimUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameAnimEnts    = g_frameAnimEnts.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameAnimBones   = g_frameAnimBones.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameAnimShed    = g_frameAnimShed.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameHostUs      = g_frameHostUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameRtUs        = g_frameRenderThreadUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameRmUpUs      = g_frameRmUpUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameRmRxUs      = g_frameRmRxUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameRmRxCnt     = g_frameRmRxCnt.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameRpcRxUs     = g_frameRpcRxUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameRpcRxCnt    = g_frameRpcRxCnt.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameSlowPktUs   = g_frameSlowPktUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameSlowPktMeta = g_frameSlowPktMeta.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameWallLuaUs   = g_frameWallLuaUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameEntWallUs   = g_frameEntWallUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameEntUpdUs    = g_frameEntUpdUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameEntPostUs   = g_frameEntPostUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameCacheRndUs  = g_frameCacheRndUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameUiDrawUs    = g_frameUiDrawUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameSoundUs     = g_frameSoundUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameSteamUs     = g_frameSteamUs.exchange(0, std::memory_order_acq_rel);
    const uint32_t frameMsgUs       = g_frameMsgUs.exchange(0, std::memory_order_acq_rel);
    UpdateAtomicEma(g_emaLuaUs,      static_cast<float>(frameLuaUs),      0.04f);
    UpdateAtomicEma(g_emaNetUs,      static_cast<float>(frameNetUs),      0.04f);
    UpdateAtomicEma(g_emaAnimUs,     static_cast<float>(frameAnimUs),     0.04f);
    UpdateAtomicEma(g_emaGcUs,       static_cast<float>(frameGcUs),       0.04f);
    UpdateAtomicEma(g_emaWallLuaUs,  static_cast<float>(frameWallLuaUs),  0.04f);
    UpdateAtomicEma(g_emaEntWallUs,  static_cast<float>(frameEntWallUs),  0.04f);
    UpdateAtomicEma(g_emaEntUpdUs,   static_cast<float>(frameEntUpdUs),   0.04f);
    UpdateAtomicEma(g_emaEntPostUs,  static_cast<float>(frameEntPostUs),  0.04f);
    UpdateAtomicEma(g_emaCacheRndUs, static_cast<float>(frameCacheRndUs), 0.04f);
    UpdateAtomicEma(g_emaUiDrawUs,   static_cast<float>(frameUiDrawUs),   0.04f);
    UpdateAtomicEma(g_emaSoundUs,    static_cast<float>(frameSoundUs),    0.04f);
    if (tls.lastEndQpc != 0) {
        double frameUs = static_cast<double>(nowQpc - tls.lastEndQpc) * g_qpcToUs;
        if (frameUs >= 100.0 && frameUs <= 60000000.0) {
            if (frameUs <= 1000000.0) {
                g_lastFrameUs.store(static_cast<int64_t>(frameUs), std::memory_order_relaxed);
                // Honest 1% Low transition guard: hold ring push ONLY while paused/fading/modal menu
                // or Alt-Tab unfocused (+ brief resume settle); 100% of live HUD gameplay frames push raw!
                static const DWORD kSelfPid = GetCurrentProcessId();
                static bool s_appFocused = true;
                static bool s_wasUnfocused = false;
                if ((completedFrameSeq & 7u) == 0u || frameUs > 8000.0) {
                    HWND fg = GetForegroundWindow();
                    DWORD fgPid = 0;
                    if (fg) GetWindowThreadProcessId(fg, &fgPid);
                    const bool focusedNow = (fg != nullptr && fgPid == kSelfPid);
                    if (!focusedNow) {
                        s_wasUnfocused = true;
                    } else if (s_wasUnfocused) {
                        s_wasUnfocused = false;
                        const int64_t graceEnd = nowQpc + (g_qpcFreq * 200) / 1000;
                        const int64_t prevGrace = g_statsResumeGraceUntilQpc.load(std::memory_order_relaxed);
                        if (graceEnd > prevGrace) {
                            g_statsResumeGraceUntilQpc.store(graceEnd, std::memory_order_relaxed);
                        }
                    }
                    s_appFocused = focusedNow;
                    g_cursorClip.Update(g_nativeGameHwnd);
                }
                const bool holdStats = (!s_appFocused) ||
                                       (g_statsTransitionHold.load(std::memory_order_relaxed) != 0) ||
                                       (g_statsResumeGraceUntilQpc.load(std::memory_order_relaxed) > nowQpc);
                g_frameRing.PushFiltered(static_cast<float>(frameUs), holdStats);
            } else {
                g_lastFrameUs.store(static_cast<int64_t>(std::min(frameUs, 60000000.0)), std::memory_order_relaxed);
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
            } else {
                const uint32_t fUs = static_cast<uint32_t>(std::min(frameUs, 60000000.0));
                const uint32_t effBusyUs = (frameCpuBusyUs > 0) ? frameCpuBusyUs
                    : g_lastFrameBusyUs.load(std::memory_order_relaxed);
                const int32_t  netPingAvg = g_netPingAvg.load(std::memory_order_relaxed);
                const int32_t  netPingLast = g_netPingLast.load(std::memory_order_relaxed);
                const float    netLossCur = g_netLossCur.load(std::memory_order_relaxed);
                const uint16_t netLossX10 = static_cast<uint16_t>(
                    std::clamp(static_cast<double>(netLossCur) * 10.0 + 0.5, 0.0, 1000.0));
                const uint32_t netResend = g_netResendMsgs.load(std::memory_order_relaxed);
                const uint64_t netRxBps = g_netRxBps.load(std::memory_order_relaxed);

                if (g_optBlackbox.load(std::memory_order_relaxed)) {
                    const double threshUs = static_cast<double>(g_blackboxThresholdMs.load(std::memory_order_relaxed)) * 1000.0;
                    uint8_t trig = 0;
                    if (frameUs > threshUs) trig |= hs_blackbox::kTrigFrame;
                    if (frameSwapUs >= 8000u || frameGpuDrainUs >= 8000u) trig |= hs_blackbox::kTrigGpu;
                    if (trig != 0) {
                        hs_blackbox::ClientEvent ev{};
                        ev.epochMs    = NowEpochMs();
                        ev.frameSeq   = completedFrameSeq;
                        ev.frameUs    = fUs;
                        ev.cpuBusyUs  = effBusyUs;
                        ev.gpuDrainUs = frameGpuDrainUs;
                        ev.swapUs     = frameSwapUs;
                        ev.worldUs    = frameWorldUs;
                        ev.postUs     = framePostUs;
                        ev.luaUs      = frameLuaUs;
                        ev.netCpuUs   = frameNetUs;
                        ev.gcUs       = frameGcUs;
                        ev.gcKb       = g_lastGcKb.load(std::memory_order_relaxed);
                        ev.animUs     = frameAnimUs;
                        ev.animEnts   = frameAnimEnts;
                        ev.texUs      = frameTexUs;
                        ev.unzUs      = frameUnzUs;
                        ev.shaderUs   = frameShaderUs;
                        ev.hostUs     = frameHostUs;
                        ev.pingAvgMs  = netPingAvg;
                        ev.pingLastMs = netPingLast;
                        ev.lossX10    = netLossX10;
                        ev.resendMsgs = netResend;
                        ev.rxKbps     = static_cast<uint32_t>(std::min<uint64_t>(netRxBps / 1024ULL, UINT32_MAX));
                        ev.gcFull     = frameGcFull;
                        ev.triggers   = trig;
                        ev.isNativeGl = g_nativeGlActive.load(std::memory_order_relaxed) ? 1u : 0u;
                        hs_blackbox::ComputeClientDerived(ev);
                        AcquireSRWLockExclusive(&g_bbLock);
                        g_clientBlackbox.Push(ev);
                        ReleaseSRWLockExclusive(&g_bbLock);
                    }
                }

                if (frameUs > 25000.0) {
                    // Tear-free Hitch & Severe-Stall (>=500ms) capture (0 MainThread I/O; WorkerProc logs max 1x/10s)
                    const bool isSevere = (frameUs >= 500000.0);
                    AcquireSRWLockExclusive(&g_hitchLock);
                    if (isSevere) {
                        ++g_hitchSnap.severeCount;
                        if (fUs > g_hitchSnap.severeMaxUs) g_hitchSnap.severeMaxUs = fUs;
                    } else {
                        ++g_hitchSnap.pendingCount;
                    }
                    if (fUs >= g_hitchSnap.peakUs) {
                        g_hitchSnap.peakUs     = fUs;
                        g_hitchSnap.busyUs     = effBusyUs;
                        g_hitchSnap.gpuDrainUs = frameGpuDrainUs;
                        g_hitchSnap.swapUs     = frameSwapUs;
                        g_hitchSnap.rtUs       = frameRtUs;
                        g_hitchSnap.worldUs    = frameWorldUs;
                        g_hitchSnap.postUs     = framePostUs;
                        g_hitchSnap.unzUs      = frameUnzUs;
                        g_hitchSnap.shaderUs   = frameShaderUs;
                        g_hitchSnap.luaUs      = frameLuaUs;
                        g_hitchSnap.wallLuaUs  = frameWallLuaUs;
                        g_hitchSnap.entWallUs  = frameEntWallUs;
                        g_hitchSnap.entUpdUs   = frameEntUpdUs;
                        g_hitchSnap.entPostUs  = frameEntPostUs;
                        g_hitchSnap.cacheRndUs = frameCacheRndUs;
                        g_hitchSnap.uiDrawUs   = frameUiDrawUs;
                        g_hitchSnap.soundUs    = frameSoundUs;
                        g_hitchSnap.steamUs    = frameSteamUs;
                        g_hitchSnap.msgUs      = frameMsgUs;
                        g_hitchSnap.netUs      = frameNetUs;
                        g_hitchSnap.rmUpUs     = frameRmUpUs;
                        g_hitchSnap.rmRxUs     = frameRmRxUs;
                        g_hitchSnap.rmRxCnt    = frameRmRxCnt;
                        g_hitchSnap.rpcRxUs    = frameRpcRxUs;
                        g_hitchSnap.rpcRxCnt   = frameRpcRxCnt;
                        g_hitchSnap.slowPktUs  = frameSlowPktUs;
                        g_hitchSnap.slowPktMeta = frameSlowPktMeta;
                        g_hitchSnap.animUs     = frameAnimUs;
                        g_hitchSnap.animEnts   = frameAnimEnts;
                        g_hitchSnap.animBones  = frameAnimBones;
                        g_hitchSnap.animShed   = frameAnimShed;
                        g_hitchSnap.gcUs       = frameGcUs;
                        g_hitchSnap.gcKb       = g_lastGcKb.load(std::memory_order_relaxed);
                        g_hitchSnap.gcFull     = frameGcFull;
                        g_hitchSnap.texUs      = frameTexUs;
                        g_hitchSnap.hostUs     = frameHostUs;
                        g_hitchSnap.pingAvgMs  = netPingAvg;
                        g_hitchSnap.pingLastMs = netPingLast;
                        g_hitchSnap.lossX10    = netLossX10;
                        g_hitchSnap.resendMsgs = netResend;
                    }
                    ReleaseSRWLockExclusive(&g_hitchLock);
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

int LuaHyperSmoothSetAnimFluid(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    auto clampF = [](float v, float lo, float hi) {
        if (!(v >= lo)) return lo;
        if (v > hi) return hi;
        return v;
    };
    const float splineStr   = clampF(static_cast<float>(g_luaToNumber(L, 1)), 0.0f, 1.0f);
    const float crossfadeMs = clampF(static_cast<float>(g_luaToNumber(L, 2)), 0.0f, 120.0f);
    const float prevSpl = g_animSplineStrength.exchange(splineStr, std::memory_order_relaxed);
    const float prevXf  = g_animCrossfadeMs.exchange(crossfadeMs, std::memory_order_relaxed);
    if (fabsf(prevSpl - splineStr) > 1e-3f || fabsf(prevXf - crossfadeMs) > 0.5f) {
        LogMsg("Lua Bridge -> AnimFluid(spline=%.2f, crossfade=%.0fms)", splineStr, crossfadeMs);
    }
    g_luaPushBoolean(L, 1);
    return 1;
}

int LuaHyperSmoothSetCombatConfig(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    auto clampF = [](float v, float lo, float hi) {
        if (!(v >= lo)) return lo;
        if (v > hi) return hi;
        return v;
    };
    const bool prevEn = g_combatImpact.IsEnabled();
    const bool en = g_luaToBoolean
        ? (g_luaToBoolean(L, 1) != 0)
        : (g_luaToNumber(L, 1) > 0.5);
    const float hitStop = clampF(static_cast<float>(g_luaToNumber(L, 2)), 0.0f, 1.5f);
    const float recoil  = clampF(static_cast<float>(g_luaToNumber(L, 3)), 0.0f, 1.5f);
    const float flash   = clampF(static_cast<float>(g_luaToNumber(L, 4)), 0.0f, 1.5f);
    const float vfx     = clampF(static_cast<float>(g_luaToNumber(L, 5)), 0.0f, 1.5f);
    const float lunge   = clampF(static_cast<float>(g_luaToNumber(L, 6)), 0.0f, 1.5f);
    g_combatImpact.SetConfig(en, hitStop, recoil, flash, vfx, lunge);
    if (prevEn != en) {
        LogMsg("Lua Bridge -> CombatHitFeel(en=%d hs=%.2f rc=%.2f fl=%.2f vfx=%.2f lg=%.2f)",
               en ? 1 : 0, hitStop, recoil, flash, vfx, lunge);
    }
    g_luaPushBoolean(L, 1);
    return 1;
}

int LuaHyperSmoothTriggerImpact(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    const float wx        = static_cast<float>(g_luaToNumber(L, 1));
    const float wy        = static_cast<float>(g_luaToNumber(L, 2));
    const float wz        = static_cast<float>(g_luaToNumber(L, 3));
    const float dirX      = static_cast<float>(g_luaToNumber(L, 4));
    const float dirZ      = static_cast<float>(g_luaToNumber(L, 5));
    const float intensity = static_cast<float>(g_luaToNumber(L, 6));
    int styleInt          = static_cast<int>(g_luaToNumber(L, 7));
    if (styleInt < 0) styleInt = 0;
    if (styleInt > 6) styleInt = 6;
    const auto style      = static_cast<hs_combat::ImpactStyle>(styleInt);
    const bool isAttacker = g_luaToBoolean ? (g_luaToBoolean(L, 8) != 0) : (g_luaToNumber(L, 8) > 0.5);
    const bool spawnVfx   = g_luaToBoolean ? (g_luaToBoolean(L, 9) != 0) : (g_luaToNumber(L, 9) > 0.5);
    const bool ok = g_combatImpact.TriggerImpact(
        wx, wy, wz, dirX, dirZ, intensity, style, isAttacker, spawnVfx, NowQpc(), g_qpcFreq);
    g_luaPushBoolean(L, ok ? 1 : 0);
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
        const int64_t tFallback0 = NowQpc();
        if (g_origProcessAsyncTexQueue) g_origProcessAsyncTexQueue(loader);
        const uint32_t fbUs = static_cast<uint32_t>(
            std::clamp(static_cast<double>(NowQpc() - tFallback0) * g_qpcToUs, 0.0, 60000000.0));
        if (fbUs > 0) {
            g_lastTexUploadUs.store(fbUs, std::memory_order_relaxed);
            g_frameTexUploadUs.fetch_add(fbUs, std::memory_order_relaxed);
        }
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
    // Boss-burst protection: Ancient Fuelweaver / crowded combat spikes can dip frame time up to ~149ms and queue >48 textures;
    // keep 1.2ms budget slicing active unless truly in a bulk loading screen (>150ms or >128 queued textures).
    const bool limitActive = (g_inGameplay.load(std::memory_order_relaxed) != 0) &&
                             (lastFrameUs > 0 && lastFrameUs < 150000) &&
                             (initCount <= 128);
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
        std::clamp(static_cast<double>(NowQpc() - t0) * g_qpcToUs, 0.0, 60000000.0));
    tl_frameTexUs += usedUs;
    g_lastTexUploadUs.store(tl_frameTexUs, std::memory_order_relaxed);
    g_frameTexUploadUs.fetch_add(usedUs, std::memory_order_relaxed);
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

static void FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason reason = hs_sprite_batch::FlushReason::State);

void WINAPI HookGlTexParameteri(uint32_t target, uint32_t pname, int32_t param) {
    FlushSpriteBatchIfPending();
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
        if (!g_nativeGlActive.load(std::memory_order_acquire)) {
            if (!g_fnGlTexParameterf) g_fnGlTexParameterf = reinterpret_cast<GlTexParameterf_t>(GetProcAddress(hGles, "glTexParameterf"));
            if (!g_fnGlGetFloatv)     g_fnGlGetFloatv     = reinterpret_cast<GlGetFloatv_t>(GetProcAddress(hGles, "glGetFloatv"));
            if (!g_fnGlGetString)     g_fnGlGetString     = reinterpret_cast<GlGetString_t>(GetProcAddress(hGles, "glGetString"));
        }

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

static void InsertDerivExtension(std::string& s) {
    if (s.find("GL_OES_standard_derivatives") != std::string::npos) return;
    static constexpr const char* kExt =
        "#ifdef GL_OES_standard_derivatives\n"
        "#extension GL_OES_standard_derivatives : enable\n"
        "#endif\n";
    size_t insPos = 0;
    size_t p = 0;
    while (p < s.size() && (s[p] == ' ' || s[p] == '\t' || s[p] == '\r' || s[p] == '\n')) ++p;
    if (p + 8 <= s.size() && s.compare(p, 8, "#version") == 0) {
        size_t nl = s.find('\n', p);
        insPos = (nl == std::string::npos) ? s.size() : (nl + 1);
    }
    s.insert(insPos, kExt);
}

static bool TransformHd2dShaderSource(const std::string& src, std::string& outSrc, const char*& outTag) {
    outTag = nullptr;

    // -------------------------------------------------------------------------
    // VERTEX SHADERS (no gl_FragColor): ANIM_VS (3D relief coords) & ANIM_BLOOM_VS (ground shadow proj)
    // -------------------------------------------------------------------------
    if (src.find("gl_FragColor") == std::string::npos) {
        // V1. World ANIM vertex shaders (anim, anim_skinned, anim_fade, anim_haunted, anim_holo):
        //     a) Computes per-entity local relief metres (HS_LOCAL_METRES) with 0% batching loss!
        //     b) v2.9.0 World-Space ShadowMask FBO pass (HS_SHADOW_PASS > 0.5) for anim_skinned & unbatched anim:
        //        height-blended 360-deg side-light billboard reorientation + canopy breeze -> ortho NDC!
        if (src.find("attribute vec4 POS2D_UV;") != std::string::npos &&
            src.find("PS_POS = world_pos.xyz;") != std::string::npos &&
            src.find("HS_LOCAL_METRES") == std::string::npos) {
            std::string s = src;
            const size_t posAttr = s.find("attribute vec4 POS2D_UV;");
            if (posAttr != std::string::npos) {
                s.insert(posAttr,
                         "uniform float HS_SHADOW_PASS;\n"
                         "uniform vec4 HS_SHADOW_BOUNDS;\n"
                         "uniform vec4 HS_SHADOW_CAMR;\n"
                         "uniform vec4 HS_SHADOW_PT;\n"
                         "uniform vec4 HS_HD2D_SHAD_PROJ;\n"
                         "uniform vec4 HS_HD2D_SHAD_DYN;\n"
                         "uniform vec4 HS_WIND_PARAMS;\n"
                         "uniform vec4 HS_WIND_PLAYER;\n"
                         "uniform float HS_FOLIAGE_CLS;\n"
                         "varying vec2 HS_LOCAL_METRES;\n");
                static constexpr const char* kPsPosLine = "PS_POS = world_pos.xyz;";
                const size_t posP = s.find(kPsPosLine);
                if (posP != std::string::npos) {
                    const bool isBatched = (s.find("HS_BATCH_ACTIVE") != std::string::npos);
                    if (isBatched) {
                        static constexpr const char* kBatchLocal =
                            "PS_POS = world_pos.xyz;\n"
                            "\tif (HS_BATCH_ACTIVE > 0.5) {\n"
                            "\t\tHS_LOCAL_METRES = vec2(HS_BATCH_CADD.w, max(HS_BATCH_WPOS.y, 0.0));\n"
                            "\t} else {\n"
                            "\t\tvec3 hs_r = MatrixW[0].xyz;\n"
                            "\t\ths_r /= max(length(hs_r), 0.0001);\n"
                            "\t\tvec3 hs_d = world_pos.xyz - MatrixW[3].xyz;\n"
                            "\t\tHS_LOCAL_METRES = vec2(dot(hs_d, hs_r), max(hs_d.y, 0.0));\n"
                            "\t\tif (HS_SHADOW_PASS > 1.5) {\n"
                            "\t\t\tfloat hs_rh = clamp(world_pos.y, 0.0, 8.5);\n"
                            "\t\t\tfloat hs_eyeY = max(HS_SHADOW_CAMR.y, 6.0);\n"
                            "\t\t\tfloat hs_ls = clamp(HS_SHADOW_BOUNDS.w, 0.50, 1.60);\n"
                            "\t\t\tfloat hs_rs = hs_eyeY / max(hs_eyeY + hs_rh * hs_ls, 1.0);\n"
                            "\t\t\tvec2 hs_rxz = mix(HS_SHADOW_CAMR.xz, world_pos.xz, hs_rs);\n"
                            "\t\t\tvec2 hs_rndc = (hs_rxz - HS_SHADOW_BOUNDS.xy) * HS_SHADOW_BOUNDS.z;\n"
                            "\t\t\tfloat hs_rvd = dot(HS_SHADOW_CAMR.xyz - vec3(world_pos.x, -hs_rh, world_pos.z), HS_SHADOW_PT.xyz);\n"
                            "\t\t\tfloat hs_rz = clamp((hs_rvd - 4.0) / 140.0, 0.0, 1.0) * 1.98 - 0.99;\n"
                            "\t\t\tgl_Position = vec4(hs_rndc.x, hs_rndc.y, hs_rz, 1.0);\n"
                            "\t\t\tHS_LOCAL_METRES = vec2(1.0, hs_rh);\n"
                            "\t\t} else if (HS_SHADOW_PASS > 0.5) {\n"
                            "\t\t\tfloat hs_h = clamp(world_pos.y, 0.0, 7.5);\n"
                            "\t\t\tvec2 hs_anch = MatrixW[3].xz;\n"
                            "\t\t\tvec2 hs_cr = normalize(HS_SHADOW_CAMR.xy + vec2(1e-6, 0.0));\n"
                            "\t\t\tvec2 hs_cf = vec2(-hs_cr.y, hs_cr.x);\n"
                            "\t\t\tvec2 hs_dir = normalize(HS_HD2D_SHAD_PROJ.xy + vec2(1e-6, 0.0));\n"
                            "\t\t\tfloat hs_stretch = HS_HD2D_SHAD_PROJ.z;\n"
                            "\t\t\tfloat hs_lw = clamp(HS_SHADOW_PT.w, 0.0, 1.0);\n"
                            "\t\t\ths_lw *= clamp(1.0 - max(MatrixW[3].y - 0.45, 0.0) / 8.5, 0.08, 1.0);\n"
                            "\t\t\tbool hs_cull = false;\n"
                            "\t\t\tif (HS_SHADOW_PT.z > 0.5) {\n"
                            "\t\t\t\tvec2 hs_pd = hs_anch - HS_SHADOW_PT.xy;\n"
                            "\t\t\t\tfloat hs_pdist = length(hs_pd);\n"
                            "\t\t\t\tfloat hs_prad = max(HS_SHADOW_PT.z, 1.0);\n"
                            "\t\t\t\ths_lw = smoothstep(0.42, 1.15, hs_pdist) * smoothstep(hs_prad, hs_prad * 0.25, hs_pdist) * hs_lw;\n"
                            "\t\t\t\tif (hs_lw <= 0.008) { hs_cull = true; }\n"
                            "\t\t\t\telse {\n"
                            "\t\t\t\t\ths_dir = hs_pd / max(hs_pdist, 0.42);\n"
                            "\t\t\t\t\ths_stretch = (0.55 + clamp(hs_pdist / hs_prad, 0.0, 1.0) * 1.70) * clamp(HS_SHADOW_CAMR.w, 0.50, 1.80);\n"
                            "\t\t\t\t}\n"
                            "\t\t\t} else if (hs_lw <= 0.005) { hs_cull = true; }\n"
                            "\t\t\tif (hs_cull) {\n"
                            "\t\t\t\tgl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
                            "\t\t\t\tHS_LOCAL_METRES = vec2(0.0, 0.0);\n"
                            "\t\t\t} else {\n"
                            "\t\t\t\tvec2 hs_perp = vec2(-hs_dir.y, hs_dir.x);\n"
                            "\t\t\t\tif (dot(hs_perp, hs_cr) + 0.05 * dot(hs_perp, hs_cf) < 0.0) { hs_perp = -hs_perp; }\n"
                            "\t\t\t\tfloat hs_lx = dot(world_pos.xz - hs_anch, hs_cr);\n"
                            "\t\t\t\tvec2 hs_flat = (world_pos.xz - hs_cr * hs_lx) + hs_perp * hs_lx;\n"
                            "\t\t\t\tfloat hs_wt = HS_HD2D_SHAD_DYN.x;\n"
                            "\t\t\t\tfloat hs_ph = hs_anch.x * 0.29 + hs_anch.y * 0.21;\n"
                            "\t\t\t\tfloat hs_bz = sin(hs_wt * 1.15 + hs_ph + hs_h * 0.14) * 0.65 + sin(hs_wt * 2.03 + hs_ph * 1.70 - hs_h * 0.22) * 0.35;\n"
                            "\t\t\t\tfloat hs_sw0 = smoothstep(0.60, 3.60, hs_h);\n"
                            "\t\t\t\tfloat hs_soff = hs_bz * (hs_sw0 * hs_sw0) * clamp(HS_HD2D_SHAD_DYN.y, 0.0, 1.5) * (0.28 + 0.06 * hs_h);\n"
                            "\t\t\t\tvec2 hs_wvec = hs_perp * hs_soff + hs_dir * (hs_soff * 0.35);\n"
                            "\t\t\t\tvec2 hs_pxz = hs_flat + hs_dir * (hs_h * hs_stretch) + hs_wvec;\n"
                            "\t\t\t\tvec2 hs_ndc = (hs_pxz - HS_SHADOW_BOUNDS.xy) * HS_SHADOW_BOUNDS.z;\n"
                            "\t\t\t\tfloat hs_hn = hs_h / 7.5;\n"
                            "\t\t\t\tfloat hs_zk = (HS_SHADOW_BOUNDS.w > 0.5) ? ((1.0 - hs_lw) * 0.92 + hs_hn * 0.08) : hs_hn;\n"
                            "\t\t\t\tgl_Position = vec4(hs_ndc.x, hs_ndc.y, hs_zk * 1.98 - 0.99, 1.0);\n"
                            "\t\t\t\tHS_LOCAL_METRES = vec2(hs_lw, hs_h);\n"
                            "\t\t\t}\n"
                            "\t\t} else if (HS_FOLIAGE_CLS > 0.5 && (HS_WIND_PARAMS.y > 0.001 || HS_WIND_PARAMS.z > 0.001)) {\n"
                            "\t\t\tvec2 hs_wcr = normalize(HS_WIND_PLAYER.zw + vec2(1e-6, 0.0));\n"
                            "\t\t\tvec2 hs_wa = MatrixW[3].xz;\n"
                            "\t\t\tfloat hs_wh = max(hs_d.y, 0.0);\n"
                            "\t\t\tfloat hs_wph = hs_wa.x * 0.29 + hs_wa.y * 0.21;\n"
                            "\t\t\tfloat hs_wbz = sin(HS_WIND_PARAMS.x * 1.15 + hs_wph) * 0.65 + sin(HS_WIND_PARAMS.x * 2.03 + hs_wph * 1.70) * 0.35;\n"
                            "\t\t\tif (HS_FOLIAGE_CLS < 1.5) {\n"
                            "\t\t\t\tfloat hs_sw = hs_wbz * smoothstep(0.08, 1.35, hs_wh) * clamp(HS_WIND_PARAMS.y, 0.0, 1.5) * 0.14;\n"
                            "\t\t\t\tfloat hs_bend = 0.0;\n"
                            "\t\t\t\tif (HS_WIND_PARAMS.w > 0.5 && HS_WIND_PARAMS.z > 0.001) {\n"
                            "\t\t\t\t\tvec2 hs_wdp = hs_wa - HS_WIND_PLAYER.xy;\n"
                            "\t\t\t\t\tfloat hs_wpd = length(hs_wdp);\n"
                            "\t\t\t\t\tfloat hs_wk = (1.0 - smoothstep(0.30, 2.40, hs_wpd)) * clamp(HS_WIND_PARAMS.z, 0.0, 1.5);\n"
                            "\t\t\t\t\tfloat hs_side = clamp(dot(hs_wdp, hs_wcr) / 0.55, -1.0, 1.0);\n"
                            "\t\t\t\t\ths_bend = hs_side * hs_wk * smoothstep(0.06, 1.10, hs_wh) * 0.34;\n"
                            "\t\t\t\t}\n"
                            "\t\t\t\tworld_pos.xz += hs_wcr * (hs_sw + hs_bend);\n"
                            "\t\t\t} else {\n"
                            "\t\t\t\tfloat hs_b2 = smoothstep(1.00, 3.80, hs_wh);\n"
                            "\t\t\t\tfloat hs_sw2 = hs_wbz * (hs_b2 * hs_b2) * clamp(HS_WIND_PARAMS.y, 0.0, 1.5) * (0.18 + 0.025 * min(hs_wh, 8.0));\n"
                            "\t\t\t\tworld_pos.xz += hs_wcr * hs_sw2;\n"
                            "\t\t\t}\n"
                            "\t\t\tPS_POS = world_pos.xyz;\n"
                            "\t\t\tgl_Position = mtxPV * world_pos;\n"
                            "\t\t}\n"
                            "\t}";
                        s.replace(posP, strlen(kPsPosLine), kBatchLocal);
                    } else {
                        static constexpr const char* kStdLocal =
                            "PS_POS = world_pos.xyz;\n"
                            "#ifdef SKINNED\n"
                            "\tvec3 hs_o = fastanim_xform[3].xyz;\n"
                            "\tvec3 hs_r = fastanim_xform[0].xyz;\n"
                            "#else\n"
                            "\tvec3 hs_o = MatrixW[3].xyz;\n"
                            "\tvec3 hs_r = MatrixW[0].xyz;\n"
                            "#endif\n"
                            "\ths_r /= max(length(hs_r), 0.0001);\n"
                            "\tvec3 hs_d = world_pos.xyz - hs_o;\n"
                            "\tHS_LOCAL_METRES = vec2(dot(hs_d, hs_r), max(hs_d.y, 0.0));\n"
                            "\tif (HS_SHADOW_PASS > 1.5) {\n"
                            "\t\tfloat hs_rh = clamp(world_pos.y, 0.0, 8.5);\n"
                            "\t\tfloat hs_eyeY = max(HS_SHADOW_CAMR.y, 6.0);\n"
                            "\t\tfloat hs_ls = clamp(HS_SHADOW_BOUNDS.w, 0.50, 1.60);\n"
                            "\t\tfloat hs_rs = hs_eyeY / max(hs_eyeY + hs_rh * hs_ls, 1.0);\n"
                            "\t\tvec2 hs_rxz = mix(HS_SHADOW_CAMR.xz, world_pos.xz, hs_rs);\n"
                            "\t\tvec2 hs_rndc = (hs_rxz - HS_SHADOW_BOUNDS.xy) * HS_SHADOW_BOUNDS.z;\n"
                            "\t\tfloat hs_rvd = dot(HS_SHADOW_CAMR.xyz - vec3(world_pos.x, -hs_rh, world_pos.z), HS_SHADOW_PT.xyz);\n"
                            "\t\tfloat hs_rz = clamp((hs_rvd - 4.0) / 140.0, 0.0, 1.0) * 1.98 - 0.99;\n"
                            "\t\tgl_Position = vec4(hs_rndc.x, hs_rndc.y, hs_rz, 1.0);\n"
                            "\t\tHS_LOCAL_METRES = vec2(1.0, hs_rh);\n"
                            "\t} else if (HS_SHADOW_PASS > 0.5) {\n"
                            "\t\tfloat hs_h = clamp(world_pos.y, 0.0, 7.5);\n"
                            "\t\tvec2 hs_anch = hs_o.xz;\n"
                            "\t\tvec2 hs_cr = normalize(HS_SHADOW_CAMR.xy + vec2(1e-6, 0.0));\n"
                            "\t\tvec2 hs_cf = vec2(-hs_cr.y, hs_cr.x);\n"
                            "\t\tvec2 hs_dir = normalize(HS_HD2D_SHAD_PROJ.xy + vec2(1e-6, 0.0));\n"
                            "\t\tfloat hs_stretch = HS_HD2D_SHAD_PROJ.z;\n"
                            "\t\tfloat hs_lw = clamp(HS_SHADOW_PT.w, 0.0, 1.0);\n"
                            "\t\ths_lw *= clamp(1.0 - max(hs_o.y - 0.45, 0.0) / 8.5, 0.08, 1.0);\n"
                            "\t\tbool hs_cull = false;\n"
                            "\t\tif (HS_SHADOW_PT.z > 0.5) {\n"
                            "\t\t\tvec2 hs_pd = hs_anch - HS_SHADOW_PT.xy;\n"
                            "\t\t\tfloat hs_pdist = length(hs_pd);\n"
                            "\t\t\tfloat hs_prad = max(HS_SHADOW_PT.z, 1.0);\n"
                            "\t\t\ths_lw = smoothstep(0.42, 1.15, hs_pdist) * smoothstep(hs_prad, hs_prad * 0.25, hs_pdist) * hs_lw;\n"
                            "\t\t\tif (hs_lw <= 0.008) { hs_cull = true; }\n"
                            "\t\t\telse {\n"
                            "\t\t\t\ths_dir = hs_pd / max(hs_pdist, 0.42);\n"
                            "\t\t\t\ths_stretch = (0.55 + clamp(hs_pdist / hs_prad, 0.0, 1.0) * 1.70) * clamp(HS_SHADOW_CAMR.w, 0.50, 1.80);\n"
                            "\t\t\t}\n"
                            "\t\t} else if (hs_lw <= 0.005) { hs_cull = true; }\n"
                            "\t\tif (hs_cull) {\n"
                            "\t\t\tgl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
                            "\t\t\tHS_LOCAL_METRES = vec2(0.0, 0.0);\n"
                            "\t\t} else {\n"
                            "\t\t\tvec2 hs_perp = vec2(-hs_dir.y, hs_dir.x);\n"
                            "\t\t\tif (dot(hs_perp, hs_cr) + 0.05 * dot(hs_perp, hs_cf) < 0.0) { hs_perp = -hs_perp; }\n"
                            "\t\t\tfloat hs_lx = dot(world_pos.xz - hs_anch, hs_cr);\n"
                            "\t\t\tvec2 hs_flat = (world_pos.xz - hs_cr * hs_lx) + hs_perp * hs_lx;\n"
                            "\t\t\tfloat hs_wt = HS_HD2D_SHAD_DYN.x;\n"
                            "\t\t\tfloat hs_ph = hs_anch.x * 0.29 + hs_anch.y * 0.21;\n"
                            "\t\t\tfloat hs_bz = sin(hs_wt * 1.15 + hs_ph + hs_h * 0.14) * 0.65 + sin(hs_wt * 2.03 + hs_ph * 1.70 - hs_h * 0.22) * 0.35;\n"
                            "\t\t\tfloat hs_sw0 = smoothstep(0.60, 3.60, hs_h);\n"
                            "\t\t\tfloat hs_soff = hs_bz * (hs_sw0 * hs_sw0) * clamp(HS_HD2D_SHAD_DYN.y, 0.0, 1.5) * (0.28 + 0.06 * hs_h);\n"
                            "\t\t\tvec2 hs_wvec = hs_perp * hs_soff + hs_dir * (hs_soff * 0.35);\n"
                            "\t\t\tvec2 hs_pxz = hs_flat + hs_dir * (hs_h * hs_stretch) + hs_wvec;\n"
                            "\t\t\tvec2 hs_ndc = (hs_pxz - HS_SHADOW_BOUNDS.xy) * HS_SHADOW_BOUNDS.z;\n"
                            "\t\t\tfloat hs_hn = hs_h / 7.5;\n"
                            "\t\t\tfloat hs_zk = (HS_SHADOW_BOUNDS.w > 0.5) ? ((1.0 - hs_lw) * 0.92 + hs_hn * 0.08) : hs_hn;\n"
                            "\t\t\tgl_Position = vec4(hs_ndc.x, hs_ndc.y, hs_zk * 1.98 - 0.99, 1.0);\n"
                            "\t\t\tHS_LOCAL_METRES = vec2(hs_lw, hs_h);\n"
                            "\t\t}\n"
                            "\t} else if (HS_FOLIAGE_CLS > 0.5 && (HS_WIND_PARAMS.y > 0.001 || HS_WIND_PARAMS.z > 0.001)) {\n"
                            "\t\tvec2 hs_wcr = normalize(HS_WIND_PLAYER.zw + vec2(1e-6, 0.0));\n"
                            "\t\tvec2 hs_wa = hs_o.xz;\n"
                            "\t\tfloat hs_wh = max(hs_d.y, 0.0);\n"
                            "\t\tfloat hs_wph = hs_wa.x * 0.29 + hs_wa.y * 0.21;\n"
                            "\t\tfloat hs_wbz = sin(HS_WIND_PARAMS.x * 1.15 + hs_wph) * 0.65 + sin(HS_WIND_PARAMS.x * 2.03 + hs_wph * 1.70) * 0.35;\n"
                            "\t\tif (HS_FOLIAGE_CLS < 1.5) {\n"
                            "\t\t\tfloat hs_sw = hs_wbz * smoothstep(0.08, 1.35, hs_wh) * clamp(HS_WIND_PARAMS.y, 0.0, 1.5) * 0.14;\n"
                            "\t\t\tfloat hs_bend = 0.0;\n"
                            "\t\t\tif (HS_WIND_PARAMS.w > 0.5 && HS_WIND_PARAMS.z > 0.001) {\n"
                            "\t\t\t\tvec2 hs_wdp = hs_wa - HS_WIND_PLAYER.xy;\n"
                            "\t\t\t\tfloat hs_wpd = length(hs_wdp);\n"
                            "\t\t\t\tfloat hs_wk = (1.0 - smoothstep(0.30, 2.40, hs_wpd)) * clamp(HS_WIND_PARAMS.z, 0.0, 1.5);\n"
                            "\t\t\t\tfloat hs_side = clamp(dot(hs_wdp, hs_wcr) / 0.55, -1.0, 1.0);\n"
                            "\t\t\t\ths_bend = hs_side * hs_wk * smoothstep(0.06, 1.10, hs_wh) * 0.34;\n"
                            "\t\t\t}\n"
                            "\t\t\tworld_pos.xz += hs_wcr * (hs_sw + hs_bend);\n"
                            "\t\t} else {\n"
                            "\t\t\tfloat hs_b2 = smoothstep(1.00, 3.80, hs_wh);\n"
                            "\t\t\tfloat hs_sw2 = hs_wbz * (hs_b2 * hs_b2) * clamp(HS_WIND_PARAMS.y, 0.0, 1.5) * (0.18 + 0.025 * min(hs_wh, 8.0));\n"
                            "\t\t\tworld_pos.xz += hs_wcr * hs_sw2;\n"
                            "\t\t}\n"
                            "\t\tPS_POS = world_pos.xyz;\n"
                            "#ifdef SKINNED\n"
                            "\t\tgl_Position = pv * world_pos;\n"
                            "#else\n"
                            "\t\tgl_Position = (MatrixP * MatrixV) * world_pos;\n"
                            "#endif\n"
                            "\t}";
                        s.replace(posP, strlen(kPsPosLine), kStdLocal);
                    }
                    g_hd2dAnimVsInjected.fetch_add(1, std::memory_order_relaxed);
                    outSrc = std::move(s);
                    outTag = "ANIM_VS";
                    return true;
                }
            }
        }

        // V2. Native Bloom Occluder Pass vertex shaders (anim_bloom.ksh & anim_bloom_skinned.ksh):
        //     Projects standing world sprites obliquely onto ground plane y=0.06 when ShadowProj ON,
        //     with continuous dt-driven height-weighted canopy breeze & zero root/rock drift;
        //     zero Lua per-entity hooks, zero sprite_batcher disruption, bit-identical when OFF!
        if (src.find("attribute vec4 POS2D_UV;") != std::string::npos &&
            src.find("PS_TEXCOORD = TEXCOORD0;") != std::string::npos &&
            src.find("mat4 mtxPVW = MatrixP * MatrixV * MatrixW;") != std::string::npos &&
            src.find("PS_POS") == std::string::npos &&
            src.find("HS_SHAD_H") == std::string::npos) {
            std::string s = src;
            const size_t posAttr = s.find("attribute vec4 POS2D_UV;");
            if (posAttr != std::string::npos) {
                s.insert(posAttr, "uniform vec4 HS_HD2D_SHAD_PROJ;\nuniform vec4 HS_HD2D_SHAD_DYN;\nvarying float HS_SHAD_H;\n");
                static constexpr const char* kSkinPos = "gl_Position = pvw * vec4(POSITION, 1.0);";
                const size_t posSk = s.find(kSkinPos);
                if (posSk != std::string::npos) {
                    static constexpr const char* kSkinProj =
                        "vec4 hs_swp = mat * vec4(POSITION, 1.0);\n"
                        "\tHS_SHAD_H = clamp(hs_swp.y, 0.0, 7.5);\n"
                        "\tif (HS_HD2D_SHAD_PROJ.w > 0.5) {\n"
                        "\t\tvec2 hs_anch = fastanim_xform[3].xz;\n"
                        "\t\tfloat hs_wt = HS_HD2D_SHAD_DYN.x;\n"
                        "\t\tfloat hs_ph = hs_anch.x * 0.29 + hs_anch.y * 0.21;\n"
                        "\t\tfloat hs_bz = sin(hs_wt * 1.15 + hs_ph + HS_SHAD_H * 0.14) * 0.65 + sin(hs_wt * 2.03 + hs_ph * 1.70 - HS_SHAD_H * 0.22) * 0.35;\n"
                        "\t\tfloat hs_sw0 = smoothstep(0.60, 3.60, HS_SHAD_H);\n"
                        "\t\tfloat hs_soff = hs_bz * (hs_sw0 * hs_sw0) * clamp(HS_HD2D_SHAD_DYN.y, 0.0, 1.5) * (0.28 + 0.06 * HS_SHAD_H);\n"
                        "\t\tvec2 hs_perp = vec2(-HS_HD2D_SHAD_PROJ.y, HS_HD2D_SHAD_PROJ.x);\n"
                        "\t\tvec2 hs_wvec = hs_perp * hs_soff + HS_HD2D_SHAD_PROJ.xy * (hs_soff * 0.35);\n"
                        "\t\tvec4 hs_sgp = vec4(hs_swp.x + HS_HD2D_SHAD_PROJ.x * HS_SHAD_H * HS_HD2D_SHAD_PROJ.z + hs_wvec.x, 0.06, hs_swp.z + HS_HD2D_SHAD_PROJ.y * HS_SHAD_H * HS_HD2D_SHAD_PROJ.z + hs_wvec.y, 1.0);\n"
                        "\t\tgl_Position = pv * hs_sgp;\n"
                        "\t} else {\n"
                        "\t\tgl_Position = pvw * vec4(POSITION, 1.0);\n"
                        "\t}";
                    s.replace(posSk, strlen(kSkinPos), kSkinProj);
                }
                static constexpr const char* kNonSkinStart = "mat4 mtxPVW = MatrixP * MatrixV * MatrixW;";
                const size_t posNs = s.find(kNonSkinStart);
                if (posNs != std::string::npos) {
                    const size_t posGlPos = s.find("gl_Position = mtxPVW", posNs);
                    const size_t posSemi  = (posGlPos != std::string::npos) ? s.find(';', posGlPos) : std::string::npos;
                    if (posSemi != std::string::npos) {
                        static constexpr const char* kNsProj =
                            "vec4 hs_swp = MatrixW * vec4(POSITION.xyz, 1.0);\n"
                            "    HS_SHAD_H = clamp(hs_swp.y, 0.0, 7.5);\n"
                            "    if (HS_HD2D_SHAD_PROJ.w > 0.5) {\n"
                            "        vec2 hs_anch = MatrixW[3].xz;\n"
                            "        float hs_wt = HS_HD2D_SHAD_DYN.x;\n"
                            "        float hs_ph = hs_anch.x * 0.29 + hs_anch.y * 0.21;\n"
                            "        float hs_bz = sin(hs_wt * 1.15 + hs_ph + HS_SHAD_H * 0.14) * 0.65 + sin(hs_wt * 2.03 + hs_ph * 1.70 - HS_SHAD_H * 0.22) * 0.35;\n"
                            "        float hs_sw0 = smoothstep(0.60, 3.60, HS_SHAD_H);\n"
                            "        float hs_soff = hs_bz * (hs_sw0 * hs_sw0) * clamp(HS_HD2D_SHAD_DYN.y, 0.0, 1.5) * (0.28 + 0.06 * HS_SHAD_H);\n"
                            "        vec2 hs_perp = vec2(-HS_HD2D_SHAD_PROJ.y, HS_HD2D_SHAD_PROJ.x);\n"
                            "        vec2 hs_wvec = hs_perp * hs_soff + HS_HD2D_SHAD_PROJ.xy * (hs_soff * 0.35);\n"
                            "        vec4 hs_sgp = vec4(hs_swp.x + HS_HD2D_SHAD_PROJ.x * HS_SHAD_H * HS_HD2D_SHAD_PROJ.z + hs_wvec.x, 0.06, hs_swp.z + HS_HD2D_SHAD_PROJ.y * HS_SHAD_H * HS_HD2D_SHAD_PROJ.z + hs_wvec.y, 1.0);\n"
                            "        gl_Position = (MatrixP * MatrixV) * hs_sgp;\n"
                            "    } else {\n"
                            "        mat4 mtxPVW = MatrixP * MatrixV * MatrixW;\n"
                            "        gl_Position = mtxPVW * vec4(POSITION.xyz, 1.0);\n"
                            "    }";
                        s.replace(posNs, (posSemi + 1) - posNs, kNsProj);
                        g_hd2dShadowBloomInjected.fetch_add(1, std::memory_order_relaxed);
                        outSrc = std::move(s);
                        outTag = "ANIM_BLOOM_VS";
                        return true;
                    }
                }
            }
        }

        // V3. Vanilla 2D Paper Ocean Waves vertex shader (shaders/waves.ksh):
        //     Optional instant GPU hide when PBR mirror ocean mode enables ocean_hide_waves (HS_OCEAN_PBR.w > 0.5).
        //     100% reversible & zero extra .ksh assets!
        if (src.find("WavesOffset[32]") != std::string::npos &&
            src.find("WavesRepeat") != std::string::npos &&
            src.find("HS_OCEAN_PBR") == std::string::npos) {
            static constexpr const char* kWaveAnchor = "gl_Position = MatrixP * MatrixV * gl_Position;";
            const size_t posAttr = src.find("attribute vec4 POSITION;");
            const size_t posGl   = src.find(kWaveAnchor);
            if (posAttr != std::string::npos && posGl != std::string::npos) {
                std::string s = src;
                s.insert(posAttr, "uniform vec4 HS_OCEAN_PBR;\n");
                const size_t posGl2 = s.find(kWaveAnchor);
                if (posGl2 != std::string::npos) {
                    s.replace(posGl2, strlen(kWaveAnchor),
                              "gl_Position = MatrixP * MatrixV * gl_Position;\n"
                              "\tif (HS_OCEAN_PBR.w > 0.5) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); }");
                    outSrc = std::move(s);
                    outTag = "WAVES_VS";
                    return true;
                }
            }
        }
        return false;
    }

    if (src.find("HS_HD2D_PARAMS") != std::string::npos ||
        src.find("HS_HD2D_SHAD_PROJ") != std::string::npos) {
        return false;
    }

    static constexpr const char* kUniformDecls =
        "\nuniform vec4 HS_HD2D_PARAMS;\n"
        "uniform vec4 HS_HD2D_SUN;\n"
        "uniform vec4 HS_HD2D_TINT;\n";

    // 0. ANIM_BLOOM_FS (anim_bloom.ksh & anim_bloom_skinned.ksh):
    //    BCAS Contract v17 additive blue shadow marker with physical contact-to-tip height penumbra fade
    //    (alpha=0 -> preserves 100% of fire/lantern glow!)
    static constexpr const char* kBloomOccLine = "gl_FragColor.rgba = vec4( 0, 0, 0, colour.a );";
    if (src.find(kBloomOccLine) != std::string::npos &&
        src.find("PS_TEXCOORD") != std::string::npos &&
        src.find("LIGHTMAP_TEXTURE") == std::string::npos) {
        std::string s = src;
        size_t uOff = FindPrecisionEndOffset(s);
        s.insert(uOff, "\nuniform vec4 HS_HD2D_SHAD_PROJ;\nuniform vec4 HS_HD2D_SHAD_DYN;\nvarying float HS_SHAD_H;\n");
        size_t posB = s.find(kBloomOccLine);
        if (posB != std::string::npos) {
            static constexpr const char* kBloomShadBody =
                "if (HS_HD2D_SHAD_PROJ.w > 0.5) {\n"
                "        float hs_hf = clamp(HS_HD2D_SHAD_DYN.z, 0.0, 1.0);\n"
                "        float hs_hfade = mix(1.0, 1.0 - 0.62 * smoothstep(0.55, 5.80, HS_SHAD_H), hs_hf);\n"
                "        float hs_root = smoothstep(0.01, 0.16, HS_SHAD_H);\n"
                "        float hs_scov = smoothstep(0.16, 0.66, colour.a) * hs_root * hs_hfade;\n"
                "        gl_FragColor = vec4(0.0, 0.0, hs_scov * 0.06, 0.0);\n"
                "    } else {\n"
                "        gl_FragColor.rgba = vec4(0.0, 0.0, 0.0, colour.a);\n"
                "    }";
            s.replace(posB, strlen(kBloomOccLine), kBloomShadBody);
            outSrc = std::move(s);
            outTag = "ANIM_BLOOM_FS";
            return true;
        }
    }

    // 1. OCEAN_COMB (ocean_combined.ksh): whisper-soft coastal blend + Zero-Overdraw Procedural Ocean Caustics & Sandy Shallows!
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
            static constexpr const char* kOceanDecls =
                "\nuniform vec4 HS_HD2D_PARAMS;\n"
                "uniform vec4 HS_HD2D_SHADE2;\n"
                "uniform vec4 HS_HD2D_SHAD_PROJ;\n"
                "uniform vec4 HS_HD2D_OCEAN;\n"
                "uniform vec4 HS_OCEAN_PBR;\n"
                "uniform vec4 HS_OCEAN_EYE;\n"
                "uniform sampler2D HS_REFL_TEX;\n"
                "uniform vec4 HS_REFL_BOUNDS;\n"
                "uniform vec4 HS_REFL_CFG;\n"
                "vec4 hs_ohash4(vec2 i) {\n"
                "\tvec4 x = fract(vec4(i.x, i.x + 1.0, i.x, i.x + 1.0) * 0.1031);\n"
                "\tvec4 y = fract(vec4(i.y, i.y, i.y + 1.0, i.y + 1.0) * 0.1031);\n"
                "\tvec4 dp = x * (y + 33.33) + y * (x + 33.33) + x * (x + 33.33);\n"
                "\treturn fract((x + y + 2.0 * dp) * (x + dp));\n"
                "}\n"
                "float hs_onoise(vec2 p) {\n"
                "\tvec2 i = floor(p);\n"
                "\tvec2 f = fract(p);\n"
                "\tvec2 u = f * f * (3.0 - 2.0 * f);\n"
                "\tvec4 h = hs_ohash4(i);\n"
                "\tvec2 m = mix(h.xz, h.yw, u.x);\n"
                "\treturn mix(m.x, m.y, u.y);\n"
                "}\n"
                "vec3 hs_onoise_d(vec2 p) {\n"
                "\tvec2 i = floor(p);\n"
                "\tvec2 f = fract(p);\n"
                "\tvec2 u = f * f * (3.0 - 2.0 * f);\n"
                "\tvec2 du = 6.0 * f * (1.0 - f);\n"
                "\tvec4 h = hs_ohash4(i);\n"
                "\tfloat k1 = h.y - h.x;\n"
                "\tfloat k2 = h.z - h.x;\n"
                "\tfloat k3 = h.x - h.y - h.z + h.w;\n"
                "\treturn vec3(h.x + k1 * u.x + k2 * u.y + k3 * u.x * u.y,\n"
                "\t            du * vec2(k1 + k3 * u.y, k2 + k3 * u.x));\n"
                "}\n"
                "float hs_omie(float cth, float g) {\n"
                "\tfloat g2 = g * g;\n"
                "\tfloat d = max(1.0 + g2 - 2.0 * g * cth, 0.004);\n"
                "\treturn (1.0 - g2) / (12.56637 * d * sqrt(d));\n"
                "}\n"
                "float hs_caustic_web(vec2 p, vec2 drift) {\n"
                "\tvec2 w = vec2(hs_onoise(p * 0.45 + drift * 0.35), hs_onoise(p * 0.45 - drift * 0.28 + vec2(7.3, 4.1)));\n"
                "\tvec2 q = p + (w - 0.5) * 1.45;\n"
                "\tfloat n1 = abs(hs_onoise(q + drift) * 2.0 - 1.0);\n"
                "\tfloat n2 = abs(hs_onoise(q * 1.37 - drift * 0.85 + vec2(3.7, 9.2)) * 2.0 - 1.0);\n"
                "\tfloat ridge = (1.0 - n1) * (1.0 - n2);\n"
                "\treturn clamp(ridge * ridge, 0.0, 1.0);\n"
                "}\n";
            s.insert(uOff, kOceanDecls);
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
                "\t}\n"
                "\tif (HS_HD2D_OCEAN.x > 0.001) {\n"
                "\t\tfloat hs_gstr = clamp(HS_HD2D_OCEAN.x, 0.0, 2.0);\n"
                "\t\tfloat hs_gden = clamp(HS_HD2D_OCEAN.y, 0.50, 0.998);\n"
                "\t\tfloat hs_ggrn = clamp(HS_HD2D_OCEAN.z, 0.35, 2.50);\n"
                "\t\tfloat hs_gsft = clamp(HS_HD2D_OCEAN.w, 0.0, 1.0);\n"
                "\t\tfloat hs_sh = clamp(1.0 - noise_opacity, 0.0, 1.0);\n"
                "\t\tvec2 hs_drift = OCEAN_UV0_PARAMS.xy * 18.0;\n"
                "\t\tvec2 hs_wp = PS_POS.xz * (0.42 * hs_ggrn);\n"
                "\t\tvec2 hs_stretch = mix(vec2(1.0, 1.0), vec2(1.35, 0.65), noise_opacity);\n"
                "\t\tfloat hs_web = hs_caustic_web(hs_wp * hs_stretch, hs_drift);\n"
                "\t\tfloat hs_gate = smoothstep(0.22, 0.78, hs_onoise(PS_POS.xz * 0.09 + hs_drift * 0.15));\n"
                "\t\tfloat hs_pow = mix(3.8, 1.4, (hs_gden - 0.50) / 0.498);\n"
                "\t\tfloat hs_glint = pow(hs_web, hs_pow) * (0.45 + 0.55 * hs_gate);\n"
                "\t\tfloat hs_sand = hs_onoise(PS_POS.xz * 0.25) * 0.55 + hs_onoise(PS_POS.xz * 1.15) * 0.45;\n"
                "\t\tvec3 hs_sand_col = mix(vec3(0.18, 0.32, 0.30), vec3(0.42, 0.48, 0.36), hs_sand);\n"
                "\t\tvec3 hs_caustic_col = mix(vec3(0.45, 0.85, 0.88), vec3(1.00, 0.90, 0.62), hs_sh);\n"
                "\t\tvec3 hs_blended_col = mix(hs_caustic_col, mix(hs_caustic_col, colour.rgb * 1.6 + vec3(0.12, 0.20, 0.18), 0.45), hs_gsft);\n"
                "\t\tfloat hs_depth_fade = mix(1.0, 0.32, smoothstep(0.15, 0.92, noise_opacity));\n"
                "\t\tvec3 hs_lm_scale = hs_lm_c * (0.55 + 0.45 * clamp(dot(hs_lm_c, vec3(0.3, 0.6, 0.1)), 0.0, 1.0));\n"
                "\t\tcolour.rgb += hs_sand_col * (hs_sh * hs_sh * 0.16 * hs_gstr * hs_gsft) * hs_lm_c;\n"
                "\t\tcolour.rgb += hs_blended_col * (hs_glint * 0.42 * hs_gstr * hs_depth_fade) * hs_lm_scale;\n"
                "\t}\n"
                "\tif (HS_OCEAN_PBR.x > 0.001 || HS_OCEAN_PBR.y > 0.001 || HS_OCEAN_PBR.z > 0.001) {\n"
                "\t\tfloat hs_pbr  = clamp(HS_OCEAN_PBR.x, 0.0, 1.5);\n"
                "\t\tfloat hs_sky  = clamp(HS_OCEAN_PBR.y, 0.0, 1.5);\n"
                "\t\tfloat hs_mieS = clamp(HS_OCEAN_PBR.z, 0.0, 1.5);\n"
                "\t\tfloat hs_ot   = HS_OCEAN_EYE.w;\n"
                "\t\tvec3  hs_eye  = vec3(HS_OCEAN_EYE.x, max(HS_OCEAN_EYE.y, 6.0), HS_OCEAN_EYE.z);\n"
                "\t\tvec3  hs_V    = normalize(hs_eye - PS_POS);\n"
                "\t\tfloat hs_elev = clamp(HS_HD2D_SHADE2.z, 0.22, 1.25);\n"
                "\t\tvec2  hs_s2   = normalize(-HS_HD2D_SHAD_PROJ.xy + vec2(1e-5, 0.0));\n"
                "\t\tvec3  hs_L    = normalize(vec3(hs_s2.x * cos(hs_elev), sin(hs_elev), hs_s2.y * cos(hs_elev)));\n"
                "\t\tfloat hs_day  = clamp(HS_HD2D_SHADE2.w, 0.0, 1.0);\n"
                "\t\tif (hs_pbr > 0.001) {\n"
                "\t\t\tfloat hs_sh0 = clamp(1.0 - noise_opacity, 0.0, 1.0);\n"
                "\t\t\tfloat hs_dp0 = smoothstep(0.18, 0.92, noise_opacity);\n"
                "\t\t\tvec3 hs_bm = mix(vec3(0.92, 1.12, 1.04), vec3(0.84, 0.94, 1.22), hs_dp0);\n"
                "\t\t\tcolour.rgb = mix(colour.rgb, colour.rgb * hs_bm, clamp(hs_pbr * 0.55, 0.0, 0.85));\n"
                "\t\t\tcolour.rgb += vec3(0.014, 0.036, 0.034) * (hs_sh0 * hs_sh0 * hs_pbr) * hs_lm_c;\n"
                "\t\t}\n"
                "\t\tfloat hs_pAmp = max(hs_pbr, max(hs_sky, hs_mieS) * 0.55);\n"
                "\t\tvec2  hs_sp   = PS_POS.xz * 0.22;\n"
                "\t\tvec3  hs_w1   = hs_onoise_d(hs_sp + vec2(hs_ot * 0.115, hs_ot * 0.048));\n"
                "\t\tvec2  hs_pp   = hs_sp - (hs_V.xz / max(hs_V.y, 0.18)) * ((hs_w1.x - 0.5) * 0.14 * hs_pAmp);\n"
                "\t\tvec3  hs_w2   = hs_onoise_d(hs_pp * 1.95 + vec2(-hs_ot * 0.085, hs_ot * 0.125) + vec2(3.1, 1.7));\n"
                "\t\tvec3  hs_w3   = hs_onoise_d(hs_pp * 4.35 + vec2(hs_ot * 0.22, -hs_ot * 0.17) + vec2(7.4, 5.2));\n"
                "\t\tvec2  hs_gLo  = hs_w1.yz * 0.55 + hs_w2.yz * 0.45;\n"
                "\t\tvec2  hs_gHi  = hs_gLo * 0.65 + hs_w3.yz * 0.55;\n"
                "\t\tvec3  hs_N    = normalize(vec3(-hs_gLo.x * 0.16 * hs_pAmp, 1.0, -hs_gLo.y * 0.16 * hs_pAmp));\n"
                "\t\tvec3  hs_Nd   = normalize(vec3(-hs_gLo.x * 0.72 * hs_pAmp, 1.0, -hs_gLo.y * 0.72 * hs_pAmp));\n"
                "\t\tfloat hs_NdV  = clamp(dot(hs_V, hs_Nd), 0.0, 1.0);\n"
                "\t\tfloat hs_F    = 0.02 + 0.98 * pow(1.0 - hs_NdV, 5.0);\n"
                "\t\tif (hs_pbr > 0.001) {\n"
                "\t\t\tfloat hs_NdL = clamp(dot(hs_Nd, hs_L), 0.0, 1.0);\n"
                "\t\t\tfloat hs_wDiff = mix(-0.12, 0.34, hs_NdL) * (1.0 - hs_F) * hs_pbr;\n"
                "\t\t\tcolour.rgb = max(colour.rgb + vec3(0.20, 0.34, 0.40) * (hs_wDiff * (0.28 + 0.72 * hs_day)) * hs_lm_c, vec3(0.0));\n"
                "\t\t}\n"
                "\t\tif (hs_sky > 0.001 || hs_mieS > 0.001) {\n"
                "\t\t\tvec3  hs_R = reflect(-hs_V, hs_N);\n"
                "\t\t\tvec2  hs_skyUv = hs_R.xz / max(hs_R.y, 0.25);\n"
                "\t\t\tvec2  hs_cOff = vec2(hs_ot * 0.020, hs_ot * 0.007);\n"
                "\t\t\tfloat hs_cNoise = hs_onoise(hs_skyUv * 1.40 + hs_cOff) * 0.65 + hs_onoise(hs_skyUv * 2.95 + hs_cOff * 1.6 + vec2(4.2, 2.8)) * 0.35;\n"
                "\t\t\tfloat hs_skyC = smoothstep(0.26, 0.78, hs_cNoise);\n"
                "\t\t\tvec3  hs_Ng  = normalize(vec3(-hs_gHi.x * 0.32 * hs_pAmp, 1.0, -hs_gHi.y * 0.32 * hs_pAmp));\n"
                "\t\t\tvec3  hs_Rg  = reflect(-hs_V, hs_Ng);\n"
                "\t\t\tfloat hs_mie = hs_omie(max(dot(hs_Rg, hs_L), 0.0), 0.90) * (0.14 * hs_mieS * hs_day);\n"
                "\t\t\tfloat hs_fBoost = 0.55 + 1.45 * hs_F;\n"
                "\t\t\tvec3  hs_cloudCol = mix(vec3(0.26, 0.34, 0.44), vec3(0.92, 0.95, 0.98), hs_skyC);\n"
                "\t\t\tcolour.rgb += hs_cloudCol * (hs_sky * 0.20 * (0.25 + 0.75 * hs_skyC) * hs_fBoost) * hs_lm_c;\n"
                "\t\t\tvec3  hs_sunCol = mix(vec3(0.74, 0.86, 1.00), vec3(1.00, 0.92, 0.74), clamp(hs_elev * 1.2, 0.0, 1.0));\n"
                "\t\t\tcolour.rgb += hs_sunCol * (min(hs_mie, 2.2) * 0.38 * hs_fBoost) * hs_lm_c;\n"
                "\t\t}\n"
                "\t}\n"
                "\tif (HS_REFL_CFG.x > 0.001 && HS_REFL_BOUNDS.w > 0.5) {\n"
                "\t\tvec2 hs_ruv0 = (PS_POS.xz - HS_REFL_BOUNDS.xy) * (HS_REFL_BOUNDS.z * 0.5) + 0.5;\n"
                "\t\tvec2 hs_redge = abs(hs_ruv0 * 2.0 - 1.0);\n"
                "\t\tfloat hs_rin = 1.0 - smoothstep(0.88, 1.0, max(hs_redge.x, hs_redge.y));\n"
                "\t\tif (hs_rin > 0.001) {\n"
                "\t\t\tvec2 hs_rwave = vec2((noise0 - noise1), (noise1 - noise2)) * (0.011 * clamp(HS_REFL_CFG.y, 0.0, 1.5));\n"
                "\t\t\tvec4 hs_rc = texture2D(HS_REFL_TEX, clamp(hs_ruv0 + hs_rwave, vec2(0.002), vec2(0.998)));\n"
                "\t\t\tfloat hs_rmask = hs_rc.a * hs_rin * smoothstep(0.04, 0.36, base_colour.a + noise_opacity);\n"
                "\t\t\tfloat hs_rstr = clamp(HS_REFL_CFG.x, 0.0, 1.2) * 0.58 * hs_rmask;\n"
                "\t\t\tvec3 hs_rcol = hs_rc.rgb * hs_lm_c * vec3(0.84, 0.94, 1.04);\n"
                "\t\t\tcolour.rgb = mix(colour.rgb, hs_rcol, clamp(hs_rstr, 0.0, 0.72));\n"
                "\t\t}\n"
                "\t}\n\t";
            s.insert(posEnc, kOceanBody);
            outSrc = std::move(s);
            outTag = "OCEAN_COMB";
            return true;
        }
    }

    // 2. GROUND (ground.ksh, ground_underground.ksh):
    //    a) Subtle matte rain-dampening ONLY while raining (HS_HD2D_PARAMS.y & HS_HD2D_TINT.a)
    //    b) v2.9.5-A Bandwidth-limited 2-octave procedural parchment/turf micro-grain (HS_DETAIL_PARAMS.x):
    //       eliminates 512x512 Ground_noise magnification blur on 1440p/4K with fwidth() anti-shimmer fade
    //       and zero brightness/tint drift in caves or bright snow!
    if (src.find("NOISE_REPEAT_SIZE") != std::string::npos &&
        src.find("BASE_TEXTURE") != std::string::npos &&
        src.find("NOISE_TEXTURE") != std::string::npos &&
        src.find("base_colour.rgb *= CalculateLightingContribution();") != std::string::npos) {
        static constexpr const char* kLmLine = "base_colour.rgb *= CalculateLightingContribution();";
        std::string s = src;
        InsertDerivExtension(s);
        size_t uOff = FindPrecisionEndOffset(s);
        static constexpr const char* kGroundDecls =
            "\nuniform vec4 HS_HD2D_PARAMS;\n"
            "uniform vec4 HS_HD2D_SUN;\n"
            "uniform vec4 HS_HD2D_TINT;\n"
            "uniform vec4 HS_DETAIL_PARAMS;\n"
            "float hs_ghash(vec2 p) {\n"
            "\tvec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
            "\tp3 += dot(p3, p3.yzx + 33.33);\n"
            "\treturn fract((p3.x + p3.y) * p3.z);\n"
            "}\n"
            "float hs_gnoise(vec2 p) {\n"
            "\tvec2 i = floor(p);\n"
            "\tvec2 f = fract(p);\n"
            "\tvec2 u = f * f * (3.0 - 2.0 * f);\n"
            "\treturn mix(mix(hs_ghash(i), hs_ghash(i + vec2(1.0, 0.0)), u.x),\n"
            "\t           mix(hs_ghash(i + vec2(0.0, 1.0)), hs_ghash(i + vec2(1.0, 1.0)), u.x), u.y);\n"
            "}\n";
        s.insert(uOff, kGroundDecls);
        size_t posL = s.find(kLmLine);
        if (posL != std::string::npos) {
            static constexpr const char* kGroundBody =
                "base_colour.rgb *= CalculateLightingContribution();\n"
                "\tif (HS_HD2D_PARAMS.y > 0.001 && HS_HD2D_TINT.a > 0.005) {\n"
                "\t\tfloat hs_wet = clamp(HS_HD2D_TINT.a * min(HS_HD2D_PARAMS.y, 1.2), 0.0, 1.0);\n"
                "\t\tfloat hs_edge = smoothstep(0.15, 0.85, base_colour.a);\n"
                "\t\tbase_colour.rgb *= mix(1.0, 1.0 - 0.09 * hs_wet, hs_edge);\n"
                "\t}\n"
                "\tif (HS_DETAIL_PARAMS.x > 0.001) {\n"
                "\t\tfloat hs_gd = clamp(HS_DETAIL_PARAMS.x, 0.0, 1.5);\n"
                "#ifdef GL_OES_standard_derivatives\n"
                "\t\tfloat hs_wpix = length(fwidth(PS_POS.xz));\n"
                "#else\n"
                "\t\tfloat hs_wpix = 0.045;\n"
                "#endif\n"
                "\t\tfloat hs_w1 = 1.0 - smoothstep(0.055, 0.155, hs_wpix);\n"
                "\t\tfloat hs_w2 = 1.0 - smoothstep(0.020, 0.058, hs_wpix);\n"
                "\t\tif (hs_w1 > 0.005) {\n"
                "\t\t\tvec2 hs_wp = mod(PS_POS.xz, 128.0);\n"
                "\t\t\tvec2 hs_rp1 = vec2(hs_wp.x * 0.883 - hs_wp.y * 0.469, hs_wp.x * 0.469 + hs_wp.y * 0.883);\n"
                "\t\t\tvec2 hs_rp2 = vec2(hs_wp.x * 0.469 + hs_wp.y * 0.883, -hs_wp.x * 0.883 + hs_wp.y * 0.469);\n"
                "\t\t\tfloat hs_gn1 = hs_gnoise(hs_rp1 * vec2(3.4, 4.25)) - 0.5;\n"
                "\t\t\tfloat hs_gn2 = (hs_w2 > 0.005) ? (hs_gnoise(hs_rp2 * vec2(10.8, 13.5)) - 0.5) : 0.0;\n"
                "\t\t\tfloat hs_gtex = hs_gn1 * (0.62 * hs_w1) + hs_gn2 * (0.38 * hs_w2);\n"
                "\t\t\tfloat hs_gl = dot(base_colour.rgb, vec3(0.299, 0.587, 0.114));\n"
                "\t\t\tfloat hs_gm = smoothstep(0.018, 0.14, hs_gl) * (1.0 - 0.32 * smoothstep(0.72, 0.98, hs_gl));\n"
                "\t\t\tbase_colour.rgb = clamp(base_colour.rgb * (1.0 + hs_gtex * (0.16 * hs_gd * hs_gm)), vec3(0.0), vec3(1.0));\n"
                "\t\t}\n"
                "\t}";
            s.replace(posL, strlen(kLmLine), kGroundBody);
            outSrc = std::move(s);
            outTag = "GROUND";
            return true;
        }
    }

    // 3. ANIM (anim, anim_skinned, anim_fade, anim_haunted, anim_fade_haunted, anim_holo;
    //    strictly excludes BLOOM & UI_CC shaders!):
    //    a) Ground Contact AO + soft highlight shoulder (HS_HD2D_PARAMS.x)
    //    b) 3D Rounded-Relief Directional Surface Lighting (HS_HD2D_SHADE & HS_HD2D_SHADE2)
    //    c) v2.9.5-A High-DPI Derivative-Adaptive Ink Silhouette Crispness & Internal Linework Clarity (HS_DETAIL_PARAMS.yz)
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
        InsertDerivExtension(s);
        size_t uOff = FindPrecisionEndOffset(s);
        static constexpr const char* kAnimDecls =
            "\nuniform vec4 HS_HD2D_PARAMS;\n"
            "uniform vec4 HS_HD2D_SUN;\n"
            "uniform vec4 HS_HD2D_TINT;\n"
            "uniform vec4 HS_HD2D_SHADE;\n"
            "uniform vec4 HS_HD2D_SHADE2;\n"
            "uniform vec4 HS_DETAIL_PARAMS;\n"
            "uniform float HS_SHADOW_PASS;\n"
            "uniform vec4 HS_SHADOW_BOUNDS;\n"
            "uniform vec4 HS_HD2D_SHAD_PROJ;\n"
            "uniform vec4 HS_HD2D_SHAD_DYN;\n"
            "uniform sampler2D HS_SHADOW_MASK;\n"
            "uniform vec4 HS_RECV_BOUNDS;\n"
            "uniform vec4 HS_RECV_TINT;\n"
            "varying vec2 HS_LOCAL_METRES;\n";
        s.insert(uOff, kAnimDecls);
        size_t pos = s.find(kAnimAnchor);
        if (pos != std::string::npos) {
            static constexpr const char* kAnimPassPre =
                "if (HS_SHADOW_PASS > 0.5) {\n"
                "\t\t\tfloat hs_sa = gl_FragColor.a;\n"
                "\t\t\tfloat hs_sh = HS_LOCAL_METRES.y;\n"
                "\t\t\tif (HS_SHADOW_PASS > 1.5) {\n"
                "\t\t\t\tif (hs_sa < 0.28) { discard; }\n"
                "\t\t\t\tvec3 hs_rrgb = clamp(gl_FragColor.rgb / max(hs_sa, 0.001), vec3(0.0), vec3(1.25));\n"
                "\t\t\t\tfloat hs_rfade = smoothstep(0.02, 0.16, hs_sh) * (1.0 - smoothstep(1.40, 7.80, hs_sh));\n"
                "\t\t\t\tgl_FragColor = vec4(hs_rrgb, clamp(hs_rfade * smoothstep(0.28, 0.65, hs_sa), 0.0, 1.0));\n"
                "\t\t\t} else {\n"
                "\t\t\t\tfloat hs_hf = clamp(HS_HD2D_SHAD_DYN.z, 0.0, 1.0);\n"
                "\t\t\t\tfloat hs_hfade = mix(1.0, 1.0 - 0.58 * smoothstep(0.60, 5.80, hs_sh), hs_hf);\n"
                "\t\t\t\tfloat hs_root = smoothstep(0.01, 0.14, hs_sh);\n"
                "\t\t\t\tfloat hs_cov = smoothstep(0.16, 0.66, hs_sa) * hs_root * hs_hfade * clamp(HS_LOCAL_METRES.x, 0.0, 1.0);\n"
                "\t\t\t\tif (HS_SHADOW_BOUNDS.w > 0.5 && hs_cov < 0.08) { discard; }\n"
                "\t\t\t\tfloat hs_hn = clamp(hs_sh / 7.5, 0.0, 1.0);\n"
                "\t\t\t\tgl_FragColor = vec4(hs_cov, (1.0 - hs_hn) * step(0.05, hs_cov), hs_hn * step(0.12, hs_cov), 1.0);\n"
                "\t\t\t}\n"
                "\t\t\treturn;\n"
                "\t\t}\n\t\t";
            static constexpr const char* kAnimBody =
                "\n\t\tif (HS_HD2D_PARAMS.x > 0.001 || HS_HD2D_SHADE.x > 0.001 || HS_RECV_TINT.a > 0.001 || HS_DETAIL_PARAMS.y > 0.001 || HS_DETAIL_PARAMS.z > 0.001) {\n"
                "\t\t\tfloat hs_a = gl_FragColor.a;\n"
                "\t\t\tfloat hs_effMask = step(0.08, hs_a) * (1.0 - smoothstep(0.05, 0.35, LIGHT_OVERRIDE));\n"
                "\t\t\tfloat hs_aSafe = max(hs_a, 0.001);\n"
                "\t\t\tvec3 hs_col = gl_FragColor.rgb / hs_aSafe;\n"
                "\t\t\tfloat hs_luma = dot(hs_col, vec3(0.299, 0.587, 0.114));\n"
                "\t\t\tfloat hs_ink = smoothstep(0.05, 0.22, hs_luma);\n"
                "\t\t\tvec3 hs_res = hs_col;\n"
                "\t\t\tfloat hs_outA = hs_a;\n"
                "#ifdef GL_OES_standard_derivatives\n"
                "\t\t\tif (HS_DETAIL_PARAMS.y > 0.001 || HS_DETAIL_PARAMS.z > 0.001) {\n"
                "\t\t\t\tfloat hs_fa = fwidth(hs_a);\n"
                "\t\t\t\tfloat hs_lgrad = length(vec2(dFdx(hs_luma), dFdy(hs_luma)));\n"
                "\t\t\t\tfloat hs_litGate = 1.0 - smoothstep(0.05, 0.28, LIGHT_OVERRIDE);\n"
                "\t\t\t\tif (HS_DETAIL_PARAMS.y > 0.001 && hs_a > 0.005 && hs_a < 0.995) {\n"
                "\t\t\t\t\tfloat hs_eMask = smoothstep(0.14, 0.44, hs_fa) * hs_litGate;\n"
                "\t\t\t\t\tfloat hs_crisp = clamp((hs_a - 0.48) / max(hs_fa * 0.72, 0.22) + 0.50, 0.0, 1.0);\n"
                "\t\t\t\t\ths_outA = mix(hs_a, hs_crisp, clamp(clamp(HS_DETAIL_PARAMS.y, 0.0, 1.5) * 0.55 * hs_eMask, 0.0, 0.85));\n"
                "\t\t\t\t}\n"
                "\t\t\t\tif (HS_DETAIL_PARAMS.z > 0.001 && hs_a > 0.50) {\n"
                "\t\t\t\t\tfloat hs_dInkStr = clamp(HS_DETAIL_PARAMS.z, 0.0, 1.5);\n"
                "\t\t\t\t\tfloat hs_dStroke = smoothstep(0.045, 0.22, hs_lgrad) * smoothstep(0.015, 0.06, hs_luma) * (1.0 - smoothstep(0.12, 0.38, hs_luma));\n"
                "\t\t\t\t\ths_res *= (1.0 - 0.22 * hs_dInkStr * hs_dStroke);\n"
                "\t\t\t\t\tfloat hs_solidMask = smoothstep(0.85, 0.98, hs_a) * (1.0 - smoothstep(0.18, 0.42, hs_fa));\n"
                "\t\t\t\t\tfloat hs_bEdge = smoothstep(0.06, 0.26, hs_lgrad) * smoothstep(0.28, 0.65, hs_luma) * (1.0 - smoothstep(0.82, 0.98, hs_luma));\n"
                "\t\t\t\t\ths_res *= (1.0 + 0.075 * hs_dInkStr * hs_bEdge * hs_solidMask);\n"
                "\t\t\t\t}\n"
                "\t\t\t}\n"
                "#endif\n"
                "\t\t\tif (HS_HD2D_PARAMS.x > 0.001) {\n"
                "\t\t\t\tfloat hs_s = min(HS_HD2D_PARAMS.x, 1.50);\n"
                "\t\t\t\tfloat hs_y = max(PS_POS.y, 0.0);\n"
                "\t\t\t\tfloat hs_ao = mix(1.0 - 0.13 * hs_s, 1.0, smoothstep(0.0, 0.45, hs_y));\n"
                "\t\t\t\ths_res *= mix(1.0, hs_ao, hs_ink);\n"
                "\t\t\t\tvec3 hs_over = max(hs_res - vec3(0.86), vec3(0.0));\n"
                "\t\t\t\ths_res -= hs_over * (0.35 * min(hs_s, 1.0));\n"
                "\t\t\t}\n"
                "\t\t\tif (HS_HD2D_SHADE.x > 0.001 && HS_HD2D_SHADE2.w > 0.005) {\n"
                "\t\t\t\tfloat hs_sl = clamp(HS_HD2D_SHADE.x, 0.0, 1.5) * clamp(HS_HD2D_SHADE2.w, 0.0, 1.0);\n"
                "\t\t\t\tfloat hs_warm = clamp(HS_HD2D_SHADE.y, 0.0, 1.0);\n"
                "\t\t\t\tfloat hs_con = clamp(HS_HD2D_SHADE.z, 0.0, 1.0);\n"
                "\t\t\t\tfloat hs_scool = clamp(HS_HD2D_SHADE.w, 0.0, 1.0);\n"
                "\t\t\t\tfloat hs_sdark = clamp(HS_HD2D_SHADE2.x, 0.0, 1.0);\n"
                "\t\t\t\tfloat hs_theta = HS_HD2D_SHADE2.y;\n"
                "\t\t\t\tfloat hs_el = clamp(HS_HD2D_SHADE2.z, 0.08, 1.50);\n"
                "\t\t\t\tfloat hs_elev_n = clamp(hs_el * 0.63661977, 0.0, 1.0);\n"
                "\t\t\t\tfloat hs_sun_x = cos(hs_theta);\n"
                "\t\t\t\tfloat hs_sun_f = sin(hs_theta);\n"
                "\t\t\t\tfloat hs_lx = HS_LOCAL_METRES.x;\n"
                "\t\t\t\tfloat hs_nx = hs_lx / sqrt(hs_lx * hs_lx + 0.72);\n"
                "\t\t\t\tfloat hs_roof = smoothstep(0.30, 2.30, HS_LOCAL_METRES.y);\n"
                "\t\t\t\tfloat hs_nup = 0.22 + hs_roof * 0.28;\n"
                "\t\t\t\tfloat hs_nfront = sqrt(max(0.06, 1.0 - hs_nx * hs_nx));\n"
                "\t\t\t\tvec2 hs_sun2 = normalize(vec2(hs_sun_x, sin(hs_el) * 2.60 + 0.18));\n"
                "\t\t\t\tfloat hs_diff = max(hs_nx * hs_sun2.x + hs_nup * 1.35 * hs_sun2.y + hs_nfront * 0.50 * hs_sun_f, 0.0);\n"
                "\t\t\t\tfloat hs_direct = smoothstep(0.08, 0.90, hs_diff);\n"
                "\t\t\t\tfloat hs_back = 1.0 - hs_direct;\n"
                "\t\t\t\tfloat hs_amp = 0.40 + 1.20 * hs_con;\n"
                "\t\t\t\tfloat hs_warmth = 1.0 - smoothstep(0.10, 0.85, hs_elev_n);\n"
                "\t\t\t\tvec3 hs_sun_col = mix(vec3(1.0, 0.96, 0.86), vec3(1.0, 0.70, 0.34), hs_warmth);\n"
                "\t\t\t\tvec3 hs_lit_hue = mix(vec3(0.86, 0.98, 1.18), vec3(1.14, 1.02, 0.82), hs_warm);\n"
                "\t\t\t\tvec3 hs_back_hue = mix(vec3(1.14, 1.00, 0.84), vec3(0.84, 0.95, 1.22), hs_scool);\n"
                "\t\t\t\tfloat hs_dark_amp = 0.40 + 1.20 * hs_sdark;\n"
                "\t\t\t\tfloat hs_h_mask = smoothstep(0.05, 0.25, max(PS_POS.y, HS_LOCAL_METRES.y));\n"
                "\t\t\t\tfloat hs_str = hs_sl * hs_ink * (0.35 + 0.65 * hs_h_mask);\n"
                "\t\t\t\tvec3 hs_lit_mul = mix(vec3(1.0), hs_sun_col * hs_lit_hue * (1.0 + 0.16 * hs_amp), hs_direct * hs_str);\n"
                "\t\t\t\tvec3 hs_shd_mul = mix(vec3(1.0), hs_back_hue * (1.0 - 0.17 * hs_amp * hs_dark_amp), hs_back * hs_str);\n"
                "\t\t\t\tfloat hs_rim = clamp(-hs_sun_f, 0.0, 1.0) * pow(1.0 - hs_nfront, 1.6) * (0.20 * hs_amp * hs_str);\n"
                "\t\t\t\ths_res = hs_res * hs_lit_mul * hs_shd_mul + hs_sun_col * hs_lit_hue * hs_rim;\n"
                "\t\t\t}\n"
                "\t\t\tif (HS_RECV_TINT.a > 0.001 && HS_RECV_BOUNDS.w > 0.5) {\n"
                "\t\t\t\tfloat hs_py = max(PS_POS.y, 0.0);\n"
                "\t\t\t\tvec2 hs_rdir = normalize(HS_HD2D_SHAD_PROJ.xy + vec2(1e-6, 0.0));\n"
                "\t\t\t\tvec2 hs_rperp = vec2(-hs_rdir.y, hs_rdir.x);\n"
                "\t\t\t\tvec2 hs_rg = PS_POS.xz + hs_rdir * (hs_py * HS_HD2D_SHAD_PROJ.z);\n"
                "\t\t\t\tvec2 hs_ruv = (hs_rg - HS_RECV_BOUNDS.xy) * (HS_RECV_BOUNDS.z * 0.5) + 0.5;\n"
                "\t\t\t\tvec2 hs_redge = abs(hs_ruv * 2.0 - 1.0);\n"
                "\t\t\t\tfloat hs_rin = 1.0 - smoothstep(0.90, 1.0, max(hs_redge.x, hs_redge.y));\n"
                "\t\t\t\tif (hs_rin > 0.001) {\n"
                "\t\t\t\t\tfloat hs_uvs = HS_RECV_BOUNDS.z * 0.5;\n"
                "\t\t\t\t\tvec2 hs_dp1 = hs_rperp * (((HS_RECV_BOUNDS.w > 1.5) ? 0.62 : 0.28) * hs_uvs);\n"
                "\t\t\t\t\tvec2 hs_dp2 = (HS_RECV_BOUNDS.w > 1.5) ? (hs_rperp * (0.95 * hs_uvs)) : (hs_rdir * (0.22 * hs_uvs));\n"
                "\t\t\t\t\tvec4 hs_t0 = texture2D(HS_SHADOW_MASK, hs_ruv);\n"
                "\t\t\t\t\tvec4 hs_t1 = texture2D(HS_SHADOW_MASK, hs_ruv + hs_dp1);\n"
                "\t\t\t\t\tvec4 hs_t2 = texture2D(HS_SHADOW_MASK, hs_ruv - hs_dp1);\n"
                "\t\t\t\t\tvec4 hs_t3 = texture2D(HS_SHADOW_MASK, hs_ruv + hs_dp2);\n"
                "\t\t\t\t\tvec4 hs_t4 = texture2D(HS_SHADOW_MASK, hs_ruv - hs_dp2);\n"
                "\t\t\t\t\tfloat hs_o0 = hs_t0.r * smoothstep(0.45, 1.15, hs_t0.b * 7.5 - hs_py);\n"
                "\t\t\t\t\tfloat hs_o1 = hs_t1.r * smoothstep(0.45, 1.15, hs_t1.b * 7.5 - hs_py);\n"
                "\t\t\t\t\tfloat hs_o2 = hs_t2.r * smoothstep(0.45, 1.15, hs_t2.b * 7.5 - hs_py);\n"
                "\t\t\t\t\tfloat hs_o3 = hs_t3.r * smoothstep(0.45, 1.15, hs_t3.b * 7.5 - hs_py);\n"
                "\t\t\t\t\tfloat hs_o4 = hs_t4.r * smoothstep(0.45, 1.15, hs_t4.b * 7.5 - hs_py);\n"
                "\t\t\t\t\tfloat hs_occ = (HS_RECV_BOUNDS.w > 1.5) ? max(hs_o0, max((hs_o1 + hs_o2) * 0.50, min(hs_o3, hs_o4))) : (hs_o0 * 0.40 + (hs_o1 + hs_o2 + hs_o3 + hs_o4) * 0.15);\n"
                "\t\t\t\t\tfloat hs_rstr = clamp(hs_occ, 0.0, 1.0) * hs_rin * clamp(HS_RECV_TINT.a, 0.0, 1.0);\n"
                "\t\t\t\t\ths_res *= mix(vec3(1.0), clamp(HS_RECV_TINT.rgb, vec3(0.0), vec3(1.0)), hs_rstr * hs_ink);\n"
                "\t\t\t\t}\n"
                "\t\t\t}\n"
                "\t\t\tvec3 hs_origScaled = (hs_a > 0.001) ? (gl_FragColor.rgb * (hs_outA / hs_a)) : vec3(0.0);\n"
                "\t\t\tgl_FragColor.rgb = mix(hs_origScaled, min(max(hs_res, vec3(0.0)) * hs_outA, vec3(hs_outA)), hs_effMask);\n"
                "\t\t\tgl_FragColor.a = hs_outA;\n"
                "\t\t}";
            s.insert(pos, kAnimPassPre);
            pos += strlen(kAnimPassPre);
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

    // 6. SPLAT (shaders/splat.ksh — Klei ShadowRenderer DynamicShadow oval ground shadows):
    //    Per-position world-space auto-hide/restore: suppresses vanilla oval shadows where an
    //    entity receives a live directional or near-field point-light projection, and automatically
    //    restores 100% vanilla ovals outside campfire range, directly under the flame, or in darkness!
    static constexpr const char* kSplatAnchor = "gl_FragColor.rgba = colour.rgba;";
    if (src.find("uniform sampler2D SAMPLER[4];") != std::string::npos &&
        src.find("vec3 light = CalculateLightingContribution();") != std::string::npos &&
        src.find(kSplatAnchor) != std::string::npos &&
        src.find("OCEAN_BLEND_PARAMS") == std::string::npos) {
        std::string s = src;
        size_t uOff = FindPrecisionEndOffset(s);
        s.insert(uOff,
                 "\nuniform vec4 HS_HD2D_SHAD_PROJ;\n"
                 "uniform vec4 HS_SPLAT_PT0;\n"
                 "uniform vec4 HS_SPLAT_PT1;\n"
                 "uniform vec4 HS_SPLAT_PT2;\n");
        size_t posSp = s.find(kSplatAnchor);
        if (posSp != std::string::npos) {
            static constexpr const char* kSplatBody =
                "if (HS_HD2D_SHAD_PROJ.w > 0.001) {\n"
                "\t\tfloat hs_loc = clamp(HS_HD2D_SHAD_PROJ.x, 0.0, 1.0);\n"
                "\t\tif (HS_SPLAT_PT0.z > 0.5) {\n"
                "\t\t\tfloat hs_d0 = length(PS_POS.xz - HS_SPLAT_PT0.xy);\n"
                "\t\t\tfloat hs_w0 = smoothstep(0.35, 1.05, hs_d0) * smoothstep(HS_SPLAT_PT0.z, HS_SPLAT_PT0.z * 0.25, hs_d0) * HS_SPLAT_PT0.w;\n"
                "\t\t\ths_loc = max(hs_loc, hs_w0);\n"
                "\t\t}\n"
                "\t\tif (HS_SPLAT_PT1.z > 0.5) {\n"
                "\t\t\tfloat hs_d1 = length(PS_POS.xz - HS_SPLAT_PT1.xy);\n"
                "\t\t\tfloat hs_w1 = smoothstep(0.35, 1.05, hs_d1) * smoothstep(HS_SPLAT_PT1.z, HS_SPLAT_PT1.z * 0.25, hs_d1) * HS_SPLAT_PT1.w;\n"
                "\t\t\ths_loc = max(hs_loc, hs_w1);\n"
                "\t\t}\n"
                "\t\tif (HS_SPLAT_PT2.z > 0.5) {\n"
                "\t\t\tfloat hs_d2 = length(PS_POS.xz - HS_SPLAT_PT2.xy);\n"
                "\t\t\tfloat hs_w2 = smoothstep(0.35, 1.05, hs_d2) * smoothstep(HS_SPLAT_PT2.z, HS_SPLAT_PT2.z * 0.25, hs_d2) * HS_SPLAT_PT2.w;\n"
                "\t\t\ths_loc = max(hs_loc, hs_w2);\n"
                "\t\t}\n"
                "\t\tfloat hs_vis = smoothstep(0.02, 0.28, hs_loc * HS_HD2D_SHAD_PROJ.y);\n"
                "\t\tfloat hs_hide = clamp(hs_vis * HS_HD2D_SHAD_PROJ.z, 0.0, 1.0);\n"
                "\t\tcolour.rgba *= clamp(1.0 - hs_hide, 0.0, 1.0);\n"
                "\t}\n"
                "\tgl_FragColor.rgba = colour.rgba;";
            s.replace(posSp, strlen(kSplatAnchor), kSplatBody);
            outSrc = std::move(s);
            outTag = "SPLAT";
            return true;
        }
    }

    return false;
}

static thread_local std::unordered_map<uint32_t, hs_ui_batch::UiShaderTag> tl_uiShaderTags;
static thread_local std::unordered_map<uint32_t, std::pair<hs_ui_batch::UiShaderTag, hs_ui_batch::UiShaderTag>> tl_uiProgAttached;
static thread_local std::unordered_map<uint32_t, hs_ui_batch::UiProgInfo> tl_uiProgs;
static thread_local std::unordered_map<uint32_t, WorldProgRole> tl_shaderWorldRole;
static thread_local std::unordered_map<uint32_t, WorldProgRole> tl_progWorldRole;
static thread_local std::unordered_map<uint32_t, uint8_t>       tl_hauntShaders;
static thread_local std::unordered_map<uint32_t, uint8_t>       tl_hauntProgs;
// Look Matcher RenderThread shader/program role map: 1=COLOURCUBE, 2=UI/Font, 3=HS_VISUAL_FINAL
static thread_local std::unordered_map<uint32_t, uint8_t>       tl_lookShaderRole;
static thread_local std::unordered_map<uint32_t, uint8_t>       tl_lookProgRole;
static thread_local uint8_t                                     tl_curLookProgRole = 0u;
// =============================================================================
// HS_EXT_V1 Per-Plugin Hardware GPU Pass Profiler (0.000ms Pipeline Stall)
//   - Identifies third-party & core post-process passes by `// @HS_EXT_ID: <id>`
//     comment OR Lua-registered probe uniform name (e.g. HSEXT_<ID>_U0).
//   - On Native OpenGL (RTX/Radeon/Arc): 4-frame async GL_TIME_ELAPSED ring buffer
//     (zero glFinish / zero stall!) measuring true hardware GPU pass time in ms.
//   - On ANGLE D3D9 fallback: QPC submit timing (isHwGpu=false).
// =============================================================================
static constexpr uint32_t kMaxExtProfSlots = 64u;
static constexpr uint32_t kExtQueryRing    = 4u;
static constexpr uint32_t kGlTimeElapsed   = 0x88BFu;
static constexpr uint32_t kGlQueryResult   = 0x8866u;
static constexpr uint32_t kGlQueryAvail    = 0x8867u;

using GlGenQueries_t          = void (WINAPI*)(int32_t n, uint32_t* ids);
using GlDeleteQueries_t       = void (WINAPI*)(int32_t n, const uint32_t* ids);
using GlBeginQuery_t          = void (WINAPI*)(uint32_t target, uint32_t id);
using GlEndQuery_t            = void (WINAPI*)(uint32_t target);
using GlGetQueryObjectiv_t    = void (WINAPI*)(uint32_t id, uint32_t pname, int32_t* params);
using GlGetQueryObjectui64v_t = void (WINAPI*)(uint32_t id, uint32_t pname, uint64_t* params);

struct ExtProfSlot {
    char                  id[64]{};
    char                  probeUniform[64]{};
    std::atomic<uint32_t> linkedProgs{0};
    std::atomic<uint32_t> drawsTotal{0};
    std::atomic<float>    emaMs{0.0f};
    std::atomic<float>    peakMs{0.0f};
    std::atomic<bool>     hwGpu{false};
    std::atomic<int64_t>  lastDrawQpc{0};
};

SRWLOCK               g_extProfLock = SRWLOCK_INIT;
ExtProfSlot           g_extProfSlots[kMaxExtProfSlots]{};
std::atomic<uint32_t> g_extProfCount{0};

static int32_t UpsertExtProfSlot(const char* pluginId, const char* probeUniform) {
    if (!pluginId || !pluginId[0]) return -1;
    AcquireSRWLockExclusive(&g_extProfLock);
    const uint32_t curN = g_extProfCount.load(std::memory_order_relaxed);
    for (uint32_t i = 0; i < curN; ++i) {
        if (strncmp(g_extProfSlots[i].id, pluginId, sizeof(g_extProfSlots[i].id) - 1) == 0) {
            if (probeUniform && probeUniform[0] && !g_extProfSlots[i].probeUniform[0]) {
                strncpy_s(g_extProfSlots[i].probeUniform, sizeof(g_extProfSlots[i].probeUniform), probeUniform, _TRUNCATE);
            }
            ReleaseSRWLockExclusive(&g_extProfLock);
            return static_cast<int32_t>(i);
        }
    }
    if (curN >= kMaxExtProfSlots) {
        ReleaseSRWLockExclusive(&g_extProfLock);
        return -1;
    }
    ExtProfSlot& slot = g_extProfSlots[curN];
    strncpy_s(slot.id, sizeof(slot.id), pluginId, _TRUNCATE);
    if (probeUniform && probeUniform[0]) {
        strncpy_s(slot.probeUniform, sizeof(slot.probeUniform), probeUniform, _TRUNCATE);
    } else {
        slot.probeUniform[0] = '\0';
    }
    slot.linkedProgs.store(0, std::memory_order_relaxed);
    slot.drawsTotal.store(0, std::memory_order_relaxed);
    slot.emaMs.store(0.0f, std::memory_order_relaxed);
    slot.peakMs.store(0.0f, std::memory_order_relaxed);
    slot.hwGpu.store(false, std::memory_order_relaxed);
    slot.lastDrawQpc.store(0, std::memory_order_relaxed);
    g_extProfCount.store(curN + 1u, std::memory_order_release);
    ReleaseSRWLockExclusive(&g_extProfLock);
    return static_cast<int32_t>(curN);
}

static int32_t FindExtProfSlot(const char* pluginId) {
    if (!pluginId || !pluginId[0]) return -1;
    AcquireSRWLockShared(&g_extProfLock);
    const uint32_t curN = g_extProfCount.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < curN; ++i) {
        if (strncmp(g_extProfSlots[i].id, pluginId, sizeof(g_extProfSlots[i].id) - 1) == 0) {
            ReleaseSRWLockShared(&g_extProfLock);
            return static_cast<int32_t>(i);
        }
    }
    ReleaseSRWLockShared(&g_extProfLock);
    return -1;
}

static void ResetExtProfMetricsOnly() {
    const uint32_t curN = g_extProfCount.load(std::memory_order_acquire);
    for (uint32_t i = 0; i < curN; ++i) {
        g_extProfSlots[i].emaMs.store(0.0f, std::memory_order_relaxed);
        g_extProfSlots[i].peakMs.store(0.0f, std::memory_order_relaxed);
        g_extProfSlots[i].drawsTotal.store(0, std::memory_order_relaxed);
        g_extProfSlots[i].lastDrawQpc.store(0, std::memory_order_relaxed);
    }
}

struct TlExtQueryRing {
    uint32_t q[kExtQueryRing]{};
    bool     pending[kExtQueryRing]{};
    uint32_t head = 0;
};

struct TlExtProfState {
    bool                                    resolved = false;
    bool                                    hasHwTimer = false;
    bool                                    queryOpen = false;
    int32_t                                 curExtSlot = -1;
    GlGenQueries_t                          pGenQueries = nullptr;
    GlDeleteQueries_t                       pDeleteQueries = nullptr;
    GlBeginQuery_t                          pBeginQuery = nullptr;
    GlEndQuery_t                            pEndQuery = nullptr;
    GlGetQueryObjectiv_t                    pGetQueryObjectiv = nullptr;
    GlGetQueryObjectui64v_t                 pGetQueryObjectui64v = nullptr;
    TlExtQueryRing                          rings[kMaxExtProfSlots]{};
    std::unordered_map<uint32_t, std::string> shaderExtId;
    std::unordered_map<uint32_t, std::string> progExtId;
    std::unordered_map<uint32_t, int32_t>     progSlotMap;

    void ResolveIfNeeded() {
        if (resolved) return;
        resolved = true;
        hasHwTimer = false;
        if (!g_nativeGlActive.load(std::memory_order_acquire)) return;
        pGenQueries       = reinterpret_cast<GlGenQueries_t>(g_wglRt.ResolveGlProc("glGenQueries"));
        pDeleteQueries    = reinterpret_cast<GlDeleteQueries_t>(g_wglRt.ResolveGlProc("glDeleteQueries"));
        pBeginQuery       = reinterpret_cast<GlBeginQuery_t>(g_wglRt.ResolveGlProc("glBeginQuery"));
        pEndQuery         = reinterpret_cast<GlEndQuery_t>(g_wglRt.ResolveGlProc("glEndQuery"));
        pGetQueryObjectiv = reinterpret_cast<GlGetQueryObjectiv_t>(g_wglRt.ResolveGlProc("glGetQueryObjectiv"));
        pGetQueryObjectui64v = reinterpret_cast<GlGetQueryObjectui64v_t>(g_wglRt.ResolveGlProc("glGetQueryObjectui64v"));
        if (!pGetQueryObjectui64v) {
            pGetQueryObjectui64v = reinterpret_cast<GlGetQueryObjectui64v_t>(g_wglRt.ResolveGlProc("glGetQueryObjectui64vEXT"));
        }
        hasHwTimer = (pGenQueries && pDeleteQueries && pBeginQuery && pEndQuery && pGetQueryObjectiv && pGetQueryObjectui64v);
        if (hasHwTimer) {
            LogMsg("[EXT][GPU] Hardware GL_TIME_ELAPSED async query ring active (0ms stall)");
        }
    }

    void DrainSlotQueries(int32_t slotIdx) {
        if (!hasHwTimer || slotIdx < 0 || static_cast<uint32_t>(slotIdx) >= kMaxExtProfSlots) return;
        TlExtQueryRing& r = rings[slotIdx];
        ExtProfSlot& slot = g_extProfSlots[slotIdx];
        for (uint32_t k = 0; k < kExtQueryRing; ++k) {
            if (!r.pending[k] || r.q[k] == 0u) continue;
            int32_t avail = 0;
            pGetQueryObjectiv(r.q[k], kGlQueryAvail, &avail);
            if (avail == 0) continue;
            uint64_t ns = 0;
            pGetQueryObjectui64v(r.q[k], kGlQueryResult, &ns);
            r.pending[k] = false;
            const float ms = static_cast<float>(static_cast<double>(ns) * 1e-6);
            if (ms >= 0.0f && ms < 250.0f) {
                UpdateAtomicEma(slot.emaMs, ms, 0.12f);
                float pk = slot.peakMs.load(std::memory_order_relaxed);
                while (ms > pk && !slot.peakMs.compare_exchange_weak(pk, ms, std::memory_order_relaxed)) {}
                slot.hwGpu.store(true, std::memory_order_relaxed);
            }
        }
    }

    void DrainAllActive() {
        if (!hasHwTimer) return;
        const uint32_t n = g_extProfCount.load(std::memory_order_acquire);
        for (uint32_t i = 0; i < n; ++i) {
            DrainSlotQueries(static_cast<int32_t>(i));
        }
    }

    void ResetContext(bool hasAliveCtx) {
        if (hasAliveCtx && hasHwTimer && pDeleteQueries) {
            for (uint32_t i = 0; i < kMaxExtProfSlots; ++i) {
                for (uint32_t k = 0; k < kExtQueryRing; ++k) {
                    if (rings[i].q[k] != 0u) {
                        pDeleteQueries(1, &rings[i].q[k]);
                    }
                    rings[i].q[k] = 0u;
                    rings[i].pending[k] = false;
                }
                rings[i].head = 0;
            }
        } else {
            for (uint32_t i = 0; i < kMaxExtProfSlots; ++i) {
                rings[i] = {};
            }
        }
        resolved = false;
        hasHwTimer = false;
        queryOpen = false;
        curExtSlot = -1;
        shaderExtId.clear();
        progExtId.clear();
        progSlotMap.clear();
    }
};

static thread_local TlExtProfState tl_extProf;

static bool ExtractExtIdTag(const std::string& src, std::string& outId) {
    static constexpr const char* kTag = "@HS_EXT_ID:";
    const size_t p = src.find(kTag);
    if (p == std::string::npos) return false;
    size_t i = p + strlen(kTag);
    while (i < src.size() && (src[i] == ' ' || src[i] == '\t')) ++i;
    size_t j = i;
    while (j < src.size() && j - i < 62u) {
        const char c = src[j];
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-') {
            ++j;
        } else {
            break;
        }
    }
    if (j <= i) return false;
    outId.assign(src, i, j - i);
    return true;
}

static bool HasActiveGlslVersionDirective(const std::string& src) noexcept {
    size_t i = 0;
    const size_t n = src.size();
    bool inBlockComment = false;
    while (i < n) {
        if (inBlockComment) {
            if (i + 1 < n && src[i] == '*' && src[i + 1] == '/') {
                inBlockComment = false;
                i += 2;
            } else {
                ++i;
            }
            continue;
        }
        while (i < n && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n')) ++i;
        if (i >= n) break;
        if (i + 1 < n && src[i] == '/' && src[i + 1] == '/') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (i + 1 < n && src[i] == '/' && src[i + 1] == '*') {
            inBlockComment = true;
            i += 2;
            continue;
        }
        if (src[i] == '#') {
            size_t j = i + 1;
            while (j < n && (src[j] == ' ' || src[j] == '\t')) ++j;
            if (j + 7 <= n && memcmp(src.data() + j, "version", 7) == 0 &&
                (j + 7 == n || src[j + 7] == ' ' || src[j + 7] == '\t')) {
                return true;
            }
        }
        // Any non-comment token before #version means no valid #version directive exists
        return false;
    }
    return false;
}

static thread_local bool tl_inCacheRenderUi = false;

static void WINAPI HookGlShaderSource(uint32_t shader, int32_t count, const char* const* string, const int32_t* length) {
    if (!g_origGlShaderSource) return;
    // Defensive seatbelt: a malformed third-party .ksh can pass string[i] == nullptr, which
    // segfaults nvoglv64.dll inside strlen! Sanitize null string pointers cleanly instead of crashing.
    if (count > 0 && string != nullptr) {
        bool hasNullStr = false;
        for (int32_t i = 0; i < count; ++i) {
            if (!string[i]) { hasNullStr = true; break; }
        }
        if (hasNullStr) {
            LogMsg("[WARN][GL] Blocked NULL shader string pointer in glShaderSource(shader=%u, count=%d) to prevent driver crash!", shader, count);
            const char* emptyStr = "";
            const int32_t zeroLen = 0;
            g_origGlShaderSource(shader, 1, &emptyStr, &zeroLen);
            return;
        }
    }
    const bool wantHighp = g_nativeGlActive.load(std::memory_order_acquire);
    const bool wantHd2d  = g_optHd2dWorldShaders.load(std::memory_order_relaxed);
    const bool wantBatch = g_optSpriteBatch.load(std::memory_order_relaxed);
    if ((wantHighp || wantHd2d || wantBatch || shader != 0u) && count > 0 && string != nullptr) {
        std::string cur;
        for (int32_t i = 0; i < count; ++i) {
            if (!string[i]) continue;
            if (length && length[i] >= 0) {
                cur.append(string[i], static_cast<size_t>(length[i]));
            } else {
                cur.append(string[i]);
            }
        }
        if (shader != 0u) {
            std::string extId;
            if (ExtractExtIdTag(cur, extId)) {
                tl_extProf.shaderExtId[shader] = std::move(extId);
            } else {
                tl_extProf.shaderExtId.erase(shader);
            }
            if (hs_sprite_batch::HasDefineLine(cur, "HAUNT")) {
                tl_hauntShaders[shader] = 1u;
            } else {
                tl_hauntShaders.erase(shader);
            }
            // Classify Look Matcher RenderThread roles unconditionally (1=COLOURCUBE, 2=UI/Font, 3=HS_VISUAL_FINAL)
            uint8_t lookRole = 0u;
            if (cur.find("#define SRC_IMAGE SAMPLER[0]") != std::string::npos &&
                cur.find("#define COLOUR_CUBE SAMPLER[1]") != std::string::npos &&
                cur.find("UI_CC") == std::string::npos) {
                lookRole = 1u;
            } else if (cur.find("HS_GRADE_A") != std::string::npos &&
                       cur.find("HS_CDL_S") != std::string::npos) {
                lookRole = 3u;
            } else if (hs_ui_batch::ClassifyUiShaderSource(cur) != hs_ui_batch::UiShaderTag::None) {
                lookRole = 2u;
            }
            if (lookRole != 0u) {
                tl_lookShaderRole[shader] = lookRole;
            } else {
                tl_lookShaderRole.erase(shader);
            }
        }
        if (wantBatch && shader != 0u) {
            const auto uiTag = hs_ui_batch::ClassifyUiShaderSource(cur);
            if (uiTag != hs_ui_batch::UiShaderTag::None) {
                tl_uiShaderTags[shader] = uiTag;
            } else {
                tl_uiShaderTags.erase(shader);
            }
        }
        bool modified = false;
        // Native OpenGL ES 3.x (e.g. RTX 4060 NVIDIA 610.x) compiles `precision mediump float;`
        // to true hardware FP16 (10-bit mantissa), quantizing world coords PS_POS.xz at |x,z|>=128
        // into 0.125-1.0u pixel blocks on ground/road/ocean shaders! Promote to FP32 highp to
        // match ANGLE D3D9 100% pixel-for-pixel at zero desktop GPU cost.
        if (wantHighp) {
            static constexpr const char* kMed = "precision mediump float;";
            static constexpr const char* kHi  = "precision highp float;";
            size_t pos = 0;
            while ((pos = cur.find(kMed, pos)) != std::string::npos) {
                cur.replace(pos, strlen(kMed), kHi);
                pos += strlen(kHi);
                modified = true;
            }
        }
        // CRITICAL ORDER: Run 2D SpriteBatcher injection FIRST so TransformHd2dShaderSource's
        // ANIM_VS branch sees HS_BATCH_ACTIVE & packs HS_LOCAL_METRES cleanly from HS_BATCH_CADD.w!
        if (wantBatch) {
            std::string sbSrc;
            const char* sbTag = nullptr;
            if (hs_sprite_batch::TransformAnimBatchShaderSource(cur, sbSrc, sbTag)) {
                const uint32_t n = g_sbShadersInjected.fetch_add(1, std::memory_order_relaxed) + 1;
                LogMsg("[PERF][SBATCH] Injected 2D batch shader #%u (%s, shader=%u, %zu -> %zu bytes)",
                       n, sbTag ? sbTag : "?", shader, cur.size(), sbSrc.size());
                cur = std::move(sbSrc);
                modified = true;
            }
        }
        if (wantHd2d) {
            std::string hdSrc;
            const char* hdTag = nullptr;
            if (TransformHd2dShaderSource(cur, hdSrc, hdTag)) {
                const uint32_t n = g_hd2dShadersInjected.fetch_add(1, std::memory_order_relaxed) + 1;
                LogMsg("[VISUAL][HD2D] Injected world shader #%u (%s, shader=%u, %zu -> %zu bytes)",
                       n, hdTag ? hdTag : "?", shader, cur.size(), hdSrc.size());
                if (shader != 0u && hdTag != nullptr) {
                    if (strcmp(hdTag, "GROUND") == 0) {
                        tl_shaderWorldRole[shader] = WorldProgRole::Ground;
                    } else if (strcmp(hdTag, "OCEAN_COMB") == 0) {
                        tl_shaderWorldRole[shader] = WorldProgRole::Ocean;
                    } else if (strcmp(hdTag, "SPLAT") == 0) {
                        tl_shaderWorldRole[shader] = WorldProgRole::Splat;
                    } else if (strcmp(hdTag, "ANIM") == 0) {
                        tl_shaderWorldRole[shader] = WorldProgRole::AnimWorld;
                    } else if (strcmp(hdTag, "ANIM_BLOOM_FS") == 0) {
                        tl_shaderWorldRole[shader] = WorldProgRole::AnimBloom;
                    }
                }
                cur = std::move(hdSrc);
                modified = true;
            } else if (shader != 0u) {
                tl_shaderWorldRole.erase(shader);
            }
        }
        // Desktop OpenGL 4.1+ Compatibility Profile (GL_ARB_ES2_compatibility):
        // Prepend `#version 100` to shaders without an explicit `#version` so both NVIDIA & AMD
        // compile DST's GLSL ES 1.00 shaders (`GL_ES`, `precision highp float;`, etc.) identically!
        if (wantHighp && !HasActiveGlslVersionDirective(cur)) {
            cur.insert(0, "#version 100\n");
            modified = true;
        }
        if (modified) {
            const char* ptr = cur.c_str();
            const int32_t len = static_cast<int32_t>(cur.size());
            g_origGlShaderSource(shader, 1, &ptr, &len);
            return;
        }
    }
    g_origGlShaderSource(shader, count, string, length);
}

static void WINAPI HookGlAttachShader(uint32_t program, uint32_t shader) {
    if (program != 0u && shader != 0u) {
        auto it = tl_uiShaderTags.find(shader);
        if (it != tl_uiShaderTags.end()) {
            auto& pair = tl_uiProgAttached[program];
            const auto tag = it->second;
            if (tag == hs_ui_batch::UiShaderTag::UiVs || tag == hs_ui_batch::UiShaderTag::FontVs) {
                pair.first = tag;
            } else {
                pair.second = tag;
            }
        }
        auto eit = tl_extProf.shaderExtId.find(shader);
        if (eit != tl_extProf.shaderExtId.end()) {
            tl_extProf.progExtId[program] = eit->second;
        }
        auto wit = tl_shaderWorldRole.find(shader);
        if (wit != tl_shaderWorldRole.end() && wit->second != WorldProgRole::None) {
            tl_progWorldRole[program] = wit->second;
        }
        if (tl_hauntShaders.find(shader) != tl_hauntShaders.end()) {
            tl_hauntProgs[program] = 1u;
        }
        auto lrit = tl_lookShaderRole.find(shader);
        if (lrit != tl_lookShaderRole.end() && lrit->second != 0u) {
            auto& dstR = tl_lookProgRole[program];
            // Priority: COLOURCUBE(1) / HS_VISUAL_FINAL(3) override generic UI(2) if ever mixed
            if (dstR == 0u || lrit->second == 1u || lrit->second == 3u) {
                dstR = lrit->second;
            }
        }
    }
    if (g_origGlAttachShader) {
        g_origGlAttachShader(program, shader);
    }
}

// =============================================================================
// Per-Thread GLES2 State-Mirror Cache (cuts redundant 2015 ANGLE D3D9 calls)
// + Multi-Texture 2D Sprite Draw-Call Batcher
// + Unified UI + Font Multi-Texture Uber-Batcher
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

static thread_local hs_sprite_batch::SpriteBatcher tl_spriteBatcher;
static thread_local uint32_t                 tl_sbOpsGen = 0;
static thread_local std::unordered_map<uint32_t, hs_sprite_batch::BatchProgInfo> tl_sbProgs;
static thread_local hs_ui_batch::UiBatcher   tl_uiBatcher;
static thread_local uint32_t                 tl_uiOpsGen = 0;

struct WorldProgInfo {
    WorldProgRole role             = WorldProgRole::None;
    bool          isSkinned        = false;
    bool          isHaunted        = false;
    bool          canCastShadow    = false;
    int32_t       locMatrixP       = -1;
    int32_t       locMatrixV       = -1;
    int32_t       locMatrixW       = -1;
    int32_t       locPv            = -1;
    int32_t       locFastAnimXform = -1;
    int32_t       locColourXform   = -1;
    int32_t       locParams        = -1;
    int32_t       locShadowPass       = -1;
    int32_t       locShadowBounds     = -1;
    int32_t       locShadowCamR       = -1;
    int32_t       locShadowPt         = -1;
    int32_t       locShadowMask       = -1;
    int32_t       locRecvBounds       = -1;
    int32_t       locRecvTint         = -1;
    int32_t       locReflTex          = -1;
    int32_t       locReflBounds       = -1;
    int32_t       locReflCfg          = -1;
    int32_t       locOceanEye         = -1;
    int32_t       locWindParams       = -1;
    int32_t       locWindPlayer       = -1;
    int32_t       locFoliageCls       = -1;
    bool          shadowMaskUnitBound = false;
    bool          reflTexUnitBound    = false;
    uint32_t      lastRecvGen         = 0u;
    uint32_t      lastWindGen         = 0u;
    float         lastFoliageCls      = -1.0f;
    float         lastMatrixW[16]     = {};
    float         lastXform[16]    = {};
    bool          hasMatrixW       = false;
    bool          hasXform         = false;
    float         lastColorAlpha   = 1.0f;
    float         lastParamsY      = 0.0f;
    float         lastOceanEye[4]   = {};
    float         lastReflBounds[4] = {};
    float         lastReflCfg[4]    = {};
    bool          oceanEyeValid     = false;
    bool          reflBoundsValid   = false;
    bool          reflCfgValid      = false;
};

static thread_local hs_world_shadow::WorldShadowEngine   tl_worldShadow;
static thread_local hs_water_refl::WaterReflectionEngine tl_waterRefl;
static thread_local hs_combat::CombatVfxRenderer         tl_combatVfx;
static thread_local uint32_t                             tl_wsOpsGen = 0;
static thread_local std::unordered_map<uint32_t, WorldProgInfo> tl_worldProgs;
static thread_local WorldProgInfo*                       tl_curWorldProg = nullptr;
static thread_local uint32_t                             tl_curBoundProg = 0u;
static thread_local uint32_t                             tl_curBoundFbo  = 0u;
static thread_local int32_t                              tl_curVp[4]     = { 0, 0, 1280, 720 };
static thread_local float                                tl_curClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
static thread_local bool                                 tl_hasClearColor = false;
static thread_local hs_world_shadow::ShadowConfig        tl_cachedShadowCfg{};
static thread_local hs_water_refl::WaterReflConfig       tl_cachedReflCfg{};
static thread_local uint32_t                             tl_cachedShadowCfgGen = 0;
static thread_local hs_foliage::WindFieldState           tl_cachedWind{};
static thread_local uint32_t                             tl_cachedWindGen = 0;
static thread_local hs_foliage::FoliageTable             tl_foliageTable;
static thread_local uint32_t                             tl_foliageTableGen = 0;

struct GlContextProgTables {
    std::unordered_map<uint32_t, WorldProgInfo>                 worldProgs;
    std::unordered_map<uint32_t, WorldProgRole>                 shaderWorldRole;
    std::unordered_map<uint32_t, WorldProgRole>                 progWorldRole;
    std::unordered_map<uint32_t, uint8_t>                       hauntShaders;
    std::unordered_map<uint32_t, uint8_t>                       hauntProgs;
    std::unordered_map<uint32_t, hs_sprite_batch::BatchProgInfo> sbProgs;
    std::unordered_map<uint32_t, hs_ui_batch::UiProgInfo>       uiProgs;
    std::unordered_map<uint32_t, std::pair<hs_ui_batch::UiShaderTag, hs_ui_batch::UiShaderTag>> uiProgAttached;
    std::unordered_map<uint32_t, hs_ui_batch::UiShaderTag>      uiShaderTags;
};
static thread_local std::unordered_map<const void*, GlContextProgTables> tl_ctxProgTables;

static inline void SaveActiveContextProgTables(const void* ctx) {
    if (!ctx) return;
    if (tl_ctxProgTables.size() >= 16u && tl_ctxProgTables.find(ctx) == tl_ctxProgTables.end()) {
        tl_ctxProgTables.clear();
    }
    auto& dst = tl_ctxProgTables[ctx];
    dst.worldProgs      = std::move(tl_worldProgs);
    dst.shaderWorldRole = std::move(tl_shaderWorldRole);
    dst.progWorldRole   = std::move(tl_progWorldRole);
    dst.hauntShaders    = std::move(tl_hauntShaders);
    dst.hauntProgs      = std::move(tl_hauntProgs);
    dst.sbProgs         = std::move(tl_sbProgs);
    dst.uiProgs         = std::move(tl_uiProgs);
    dst.uiProgAttached  = std::move(tl_uiProgAttached);
    dst.uiShaderTags    = std::move(tl_uiShaderTags);
    tl_worldProgs.clear();
    tl_shaderWorldRole.clear();
    tl_progWorldRole.clear();
    tl_hauntShaders.clear();
    tl_hauntProgs.clear();
    tl_sbProgs.clear();
    tl_uiProgs.clear();
    tl_uiProgAttached.clear();
    tl_uiShaderTags.clear();
}

static inline void LoadContextProgTables(const void* ctx) {
    tl_worldProgs.clear();
    tl_shaderWorldRole.clear();
    tl_progWorldRole.clear();
    tl_hauntShaders.clear();
    tl_hauntProgs.clear();
    tl_sbProgs.clear();
    tl_uiProgs.clear();
    tl_uiProgAttached.clear();
    tl_uiShaderTags.clear();
    if (!ctx) return;
    auto it = tl_ctxProgTables.find(ctx);
    if (it != tl_ctxProgTables.end()) {
        tl_worldProgs      = std::move(it->second.worldProgs);
        tl_shaderWorldRole = std::move(it->second.shaderWorldRole);
        tl_progWorldRole   = std::move(it->second.progWorldRole);
        tl_hauntShaders    = std::move(it->second.hauntShaders);
        tl_hauntProgs      = std::move(it->second.hauntProgs);
        tl_sbProgs         = std::move(it->second.sbProgs);
        tl_uiProgs         = std::move(it->second.uiProgs);
        tl_uiProgAttached  = std::move(it->second.uiProgAttached);
        tl_uiShaderTags    = std::move(it->second.uiShaderTags);
        tl_ctxProgTables.erase(it);
        for (auto& kv : tl_worldProgs) {
            kv.second.shadowMaskUnitBound = false;
            kv.second.reflTexUnitBound    = false;
            kv.second.lastRecvGen         = 0u;
            kv.second.lastWindGen         = 0u;
            kv.second.lastFoliageCls      = -1.0f;
        }
    }
}

static inline void EraseContextProgTables(const void* ctx) {
    if (!ctx) return;
    tl_ctxProgTables.erase(ctx);
}

static void WorldShadowLogBridge(const char* msg) {
    if (msg && msg[0]) {
        LogMsg("%s", msg);
    }
}

static inline const hs_world_shadow::ShadowConfig& GetCachedShadowConfig() {
    const uint32_t gen = g_hd2dStateGen.load(std::memory_order_acquire);
    if (tl_cachedShadowCfgGen != gen) {
        Hd2dUniformState st{};
        AcquireSRWLockShared(&g_hd2dStateLock);
        st = g_hd2dState;
        ReleaseSRWLockShared(&g_hd2dStateLock);
        tl_cachedShadowCfg.dirWx         = st.shadProj[0];
        tl_cachedShadowCfg.dirWz         = st.shadProj[1];
        tl_cachedShadowCfg.stretch       = st.shadProj[2];
        tl_cachedShadowCfg.projOn        = st.shadProj[3];
        tl_cachedShadowCfg.windTime      = st.shadDyn[0];
        tl_cachedShadowCfg.swayAmp       = st.shadDyn[1];
        tl_cachedShadowCfg.heightFade    = st.shadDyn[2];
        tl_cachedShadowCfg.penumbraBias  = st.shadDyn[3];
        tl_cachedShadowCfg.density       = st.shadowDensity;
        tl_cachedShadowCfg.soft          = st.shadowSoft;
        tl_cachedShadowCfg.tintR         = st.shadowTint[0];
        tl_cachedShadowCfg.tintG         = st.shadowTint[1];
        tl_cachedShadowCfg.tintB         = st.shadowTint[2];
        tl_cachedShadowCfg.entityRecv    = st.shadowEntityRecv;
        tl_cachedShadowCfg.ovalKeep      = st.shadowOvalKeep;
        tl_cachedShadowCfg.minHeight     = st.shadowMinHeight;
        tl_cachedShadowCfg.hdMode        = st.shadowHd;
        tl_cachedShadowCfg.skipNightmare = st.shadowSkipNightmare;
        tl_cachedShadowCfg.hasLuaCam     = st.hasLuaCam;
        tl_cachedShadowCfg.luaCamX       = st.luaCam[0];
        tl_cachedShadowCfg.luaCamZ       = st.luaCam[1];
        tl_cachedShadowCfg.luaCamRightX      = st.luaCam[2];
        tl_cachedShadowCfg.luaCamRightZ      = st.luaCam[3];
        tl_cachedShadowCfg.dirStrength       = st.shadowDirStrength;
        tl_cachedShadowCfg.pointStretchScale = st.shadowPointStretch;
        tl_cachedShadowCfg.pointLightCount   = (st.pointLightCount <= hs_world_shadow::kMaxPointShadowLights)
            ? st.pointLightCount : hs_world_shadow::kMaxPointShadowLights;
        for (uint32_t li = 0u; li < hs_world_shadow::kMaxPointShadowLights; ++li) {
            tl_cachedShadowCfg.pointLights[li].wx       = st.pointLights[li][0];
            tl_cachedShadowCfg.pointLights[li].wz       = st.pointLights[li][1];
            tl_cachedShadowCfg.pointLights[li].radius   = st.pointLights[li][2];
            tl_cachedShadowCfg.pointLights[li].strength = st.pointLights[li][3];
        }
        tl_cachedReflCfg.strength      = st.waterReflStr;
        tl_cachedReflCfg.rippleAmp     = st.waterReflRipple;
        tl_cachedReflCfg.lengthScale   = st.waterReflLength;
        tl_cachedReflCfg.skipNightmare = st.shadowSkipNightmare;
        tl_cachedReflCfg.hasLuaCam     = st.hasLuaCam;
        tl_cachedReflCfg.luaCamX       = st.luaCam[0];
        tl_cachedReflCfg.luaCamZ       = st.luaCam[1];
        tl_cachedReflCfg.luaCamRightX  = st.luaCam[2];
        tl_cachedReflCfg.luaCamRightZ  = st.luaCam[3];
        tl_cachedShadowCfgGen = gen;
    }
    return tl_cachedShadowCfg;
}

static inline const hs_water_refl::WaterReflConfig& GetCachedWaterReflConfig() {
    (void)GetCachedShadowConfig();
    return tl_cachedReflCfg;
}

static inline const hs_foliage::WindFieldState& SyncFoliageAndWind() {
    const uint32_t wGen = g_windStateGen.load(std::memory_order_acquire);
    if (tl_cachedWindGen != wGen) {
        AcquireSRWLockShared(&g_windLock);
        tl_cachedWind = g_windState;
        ReleaseSRWLockShared(&g_windLock);
        tl_cachedWindGen = wGen;
    }
    if (tl_cachedWind.IsActive()) {
        const uint32_t fGen = g_foliageTableGen.load(std::memory_order_acquire);
        if (tl_foliageTableGen != fGen) {
            AcquireSRWLockShared(&g_foliageLock);
            tl_foliageTable = g_foliageStage;
            ReleaseSRWLockShared(&g_foliageLock);
            tl_foliageTableGen = fGen;
        }
    }
    return tl_cachedWind;
}

static void WINAPI BindFramebufferResolved(uint32_t target, uint32_t framebuffer) {
    if (!g_origGlBindFramebuffer) return;
    uint32_t actual = framebuffer;
    if (actual == 0u) {
        const uint32_t offFbo = GetDxgiFlip().OffscreenFbo();
        if (offFbo != 0u) actual = offFbo;
    }
    g_origGlBindFramebuffer(target, actual);
}

static inline void SyncWorldShadowEngine() {
    const uint32_t gen = g_wsOpsGen.load(std::memory_order_acquire);
    if (tl_wsOpsGen != gen) {
        hs_world_shadow::GlShadowOps ops{};
        ops.glGenFramebuffers          = g_fnGlGenFramebuffers;
        ops.glDeleteFramebuffers       = g_fnGlDeleteFramebuffers;
        ops.glBindFramebuffer          = g_origGlBindFramebuffer ? &BindFramebufferResolved : nullptr;
        ops.glFramebufferTexture2D     = g_fnGlFramebufferTexture2D;
        ops.glCheckFramebufferStatus   = g_fnGlCheckFramebufferStatus;
        ops.glGenRenderbuffers         = g_fnGlGenRenderbuffers;
        ops.glDeleteRenderbuffers      = g_fnGlDeleteRenderbuffers;
        ops.glBindRenderbuffer         = g_fnGlBindRenderbuffer;
        ops.glRenderbufferStorage      = g_fnGlRenderbufferStorage;
        ops.glFramebufferRenderbuffer  = g_fnGlFramebufferRenderbuffer;
        ops.glGenTextures              = g_fnGlGenTextures;
        ops.glDeleteTextures           = g_origGlDeleteTextures;
        ops.glActiveTexture            = g_origGlActiveTexture;
        ops.glBindTexture              = g_origGlBindTexture;
        ops.glTexImage2D               = g_origGlTexImage2D;
        ops.glTexParameteri            = g_origGlTexParameteri;
        ops.glCreateShader             = g_fnGlCreateShader;
        ops.glShaderSource             = g_origGlShaderSource;
        ops.glCompileShader            = g_fnGlCompileShader;
        ops.glGetShaderiv              = g_fnGlGetShaderiv;
        ops.glGetShaderInfoLog         = g_fnGlGetShaderInfoLog;
        ops.glDeleteShader             = g_fnGlDeleteShader;
        ops.glCreateProgram            = g_fnGlCreateProgram;
        ops.glAttachShader             = g_origGlAttachShader;
        ops.glBindAttribLocation       = g_fnGlBindAttribLocation;
        ops.glLinkProgram              = g_origGlLinkProgram;
        ops.glGetProgramiv             = g_fnGlGetProgramiv;
        ops.glGetProgramInfoLog        = g_fnGlGetProgramInfoLog;
        ops.glDeleteProgram            = g_origGlDeleteProgram;
        ops.glUseProgram               = g_origGlUseProgram;
        ops.glGetUniformLocation       = g_fnGlGetUniformLocation;
        ops.glUniform1f                = g_origGlUniform1f;
        ops.glUniform1i                = g_origGlUniform1i;
        ops.glUniform4fv               = g_origGlUniform4fv ? g_origGlUniform4fv : g_fnGlUniform4fv;
        ops.glUniformMatrix4fv         = g_origGlUniformMatrix4fv;
        ops.glGenBuffers               = g_fnGlGenBuffers;
        ops.glDeleteBuffers            = g_origGlDeleteBuffers;
        ops.glBindBuffer               = g_origGlBindBuffer;
        ops.glBufferData               = g_origGlBufferData ? g_origGlBufferData : g_fnGlBufferData;
        ops.glEnableVertexAttribArray  = g_origGlEnableVertexAttribArray;
        ops.glDisableVertexAttribArray = g_origGlDisableVertexAttribArray;
        ops.glVertexAttribPointer      = g_origGlVertexAttribPointer;
        ops.glDrawArrays               = g_origGlDrawArrays;
        ops.glDrawElements             = g_origGlDrawElements;
        ops.glViewport                 = g_origGlViewport;
        ops.glClearColor               = g_origGlClearColor;
        ops.glClear                    = g_origGlClear;
        ops.glEnable                   = g_origGlEnable;
        ops.glDisable                  = g_origGlDisable;
        ops.glBlendFunc                = g_origGlBlendFunc;
        ops.glBlendFuncSeparate        = g_fnGlBlendFuncSeparate;
        ops.glBlendEquation            = g_origGlBlendEquation;
        ops.glBlendEquationSeparate    = g_fnGlBlendEquationSeparate;
        ops.glDepthFunc                = g_origGlDepthFunc;
        ops.glDepthMask                = g_origGlDepthMask;
        ops.glColorMask                = g_origGlColorMask;
        ops.glGetIntegerv              = g_fnGlGetIntegerv;
        ops.glGetError                 = g_fnGlGetError;
        ops.logFn                      = &WorldShadowLogBridge;
        const bool isNat = g_nativeGlActive.load(std::memory_order_acquire);
        tl_worldShadow.SetOps(ops, isNat);
        tl_waterRefl.SetOps(ops, isNat);
        tl_combatVfx.SetOps(ops, isNat);
        tl_wsOpsGen = gen;
    }
}

static inline bool IsGlCacheOperational();

static inline hs_world_shadow::ShadowPassState BuildShadowPassState(uint32_t overrideProg = 0u) {
    hs_world_shadow::ShadowPassState s{};
    s.dstFbo        = tl_curBoundFbo;
    s.dstProg       = (overrideProg != 0u) ? overrideProg : tl_curBoundProg;
    s.vp[0]         = tl_curVp[0];
    s.vp[1]         = tl_curVp[1];
    s.vp[2]         = tl_curVp[2];
    s.vp[3]         = tl_curVp[3];
    s.activeUnit    = tl_spriteBatcher.ActiveUnit();
    s.boundArrayBuf = tl_spriteBatcher.BoundArrayBuf();
    s.boundElemBuf  = tl_spriteBatcher.BoundElemBuf();
    s.hasClearColor  = tl_hasClearColor;
    s.glCacheTrusted = IsGlCacheOperational();
    for (int i = 0; i < 4; ++i) s.clearColor[i] = tl_curClearColor[i];
    hs_sprite_batch::AttribState a0{}, a1{};
    bool a0En = false, a1En = false;
    tl_spriteBatcher.GetAttribState(0u, a0, a0En);
    tl_spriteBatcher.GetAttribState(1u, a1, a1En);
    s.attr0.buffer     = a0.buffer;
    s.attr0.size       = a0.size;
    s.attr0.type       = a0.type;
    s.attr0.normalized = a0.normalized;
    s.attr0.stride     = a0.stride;
    s.attr0.pointer    = a0.pointer;
    s.attr0.valid      = a0.valid;
    s.attr0.enabled    = a0En;
    s.attr1.buffer     = a1.buffer;
    s.attr1.size       = a1.size;
    s.attr1.type       = a1.type;
    s.attr1.normalized = a1.normalized;
    s.attr1.stride     = a1.stride;
    s.attr1.pointer    = a1.pointer;
    s.attr1.valid      = a1.valid;
    s.attr1.enabled    = a1En;
    return s;
}

struct TlLookCaptureState {
    hs_look_match::GlAsyncFrameSampler sampler;
    uint32_t                           opsGen         = 0u;
    uint32_t                           activeJobId    = 0u;
    bool                               needDual       = false;
    bool                               slot0Issued    = false;
    bool                               slot1Issued    = false;
    bool                               slot0Ready     = false;
    bool                               slot1Ready     = false;
    hs_look_match::ImageRgb            img[2]{};
    std::string                        readbackPath;

    void ResetJobState() noexcept {
        activeJobId = 0u;
        needDual    = false;
        slot0Issued = false;
        slot1Issued = false;
        slot0Ready  = false;
        slot1Ready  = false;
        img[0]      = hs_look_match::ImageRgb{};
        img[1]      = hs_look_match::ImageRgb{};
        readbackPath.clear();
    }

    void OnContextReset() noexcept {
        sampler.OnContextReset();
        ResetJobState();
    }
};

static thread_local TlLookCaptureState tl_lookCap;

static inline void SyncLookSamplerOps() {
    const uint32_t gen = g_wsOpsGen.load(std::memory_order_acquire);
    if (tl_lookCap.opsGen != gen || gen == 0u) {
        hs_look_match::GlSamplerOps ops{};
        ops.glGenFramebuffers        = g_fnGlGenFramebuffers;
        ops.glDeleteFramebuffers     = g_fnGlDeleteFramebuffers;
        ops.glBindFramebuffer        = g_origGlBindFramebuffer; // RAW unmasked FBO bind!
        ops.glFramebufferTexture2D   = g_fnGlFramebufferTexture2D;
        ops.glCheckFramebufferStatus = g_fnGlCheckFramebufferStatus;
        ops.glGenTextures            = g_fnGlGenTextures;
        ops.glDeleteTextures         = g_origGlDeleteTextures;
        ops.glBindTexture            = g_origGlBindTexture;
        ops.glTexImage2D             = g_origGlTexImage2D;
        ops.glTexParameteri          = g_origGlTexParameteri;
        ops.glGenBuffers             = g_fnGlGenBuffers;
        ops.glDeleteBuffers          = g_origGlDeleteBuffers;
        ops.glBindBuffer             = g_origGlBindBuffer;
        ops.glBufferData             = g_fnGlBufferData ? g_fnGlBufferData : g_origGlBufferData;
        ops.glMapBufferRange         = g_fnGlMapBufferRange;
        ops.glUnmapBuffer            = g_fnGlUnmapBuffer;
        ops.glReadPixels             = g_origGlReadPixels;
        ops.glPixelStorei            = g_fnGlPixelStorei;
        ops.glGetIntegerv            = g_fnGlGetIntegerv;       // RAW unmasked FBO query!
        ops.glEnable                 = g_origGlEnable;
        ops.glDisable                = g_origGlDisable;
        ops.glBlitFramebuffer        = g_fnGlBlitFramebuffer;
        ops.glFenceSync              = g_fnGlFenceSync;
        ops.glClientWaitSync         = g_fnGlClientWaitSync;
        ops.glDeleteSync             = g_fnGlDeleteSync;
        tl_lookCap.sampler.SetOps(ops, tl_glCache.curContext);
        tl_lookCap.opsGen = gen;
    }
}

static inline uint32_t ResolveActualCurrentFbo() noexcept {
    if (tl_curBoundFbo != 0u) return tl_curBoundFbo;
    const uint32_t off = GetDxgiFlip().OffscreenFbo();
    return off != 0u ? off : 0u;
}

static inline void TryDeliverLookCaptureIfReady() {
    if (tl_lookCap.activeJobId == 0u) return;
    const bool allReady = tl_lookCap.slot0Ready && (!tl_lookCap.needDual || tl_lookCap.slot1Ready);
    if (!allReady) return;
    const uint32_t jid = tl_lookCap.activeJobId;
    hs_look_match::ImageRgb s0 = std::move(tl_lookCap.img[0]);
    hs_look_match::ImageRgb s1 = std::move(tl_lookCap.img[1]);
    std::string rpath = std::move(tl_lookCap.readbackPath);
    tl_lookCap.ResetJobState();
    g_lookMatcher.OnRenderCaptureComplete(jid, std::move(s0), std::move(s1), rpath);
}

static inline void LookCaptureSyncArmedJob() {
    bool dual = false;
    const uint32_t jid = g_lookMatcher.ArmedJobId(dual);
    if (jid != 0u && jid != tl_lookCap.activeJobId) {
        tl_lookCap.sampler.CancelPending();
        tl_lookCap.ResetJobState();
        tl_lookCap.activeJobId = jid;
        tl_lookCap.needDual    = dual;
    }
}

static inline void LookCaptureIssueSlot(int slot) {
    SyncLookSamplerOps();
    hs_look_match::ImageRgb syncImg{};
    bool completedNow = false;
    std::string rpath;
    const uint32_t srcFbo = ResolveActualCurrentFbo();
    if (tl_lookCap.sampler.CaptureCurrentFbo(slot, srcFbo, tl_curVp, syncImg, completedNow, rpath)) {
        if (slot == 0) tl_lookCap.slot0Issued = true;
        else           tl_lookCap.slot1Issued = true;
        if (!rpath.empty()) tl_lookCap.readbackPath = rpath;
        if (completedNow) {
            tl_lookCap.img[slot] = std::move(syncImg);
            if (slot == 0) tl_lookCap.slot0Ready = true;
            else           tl_lookCap.slot1Ready = true;
        }
        const bool allIssued = tl_lookCap.slot0Issued && (!tl_lookCap.needDual || tl_lookCap.slot1Issued);
        if (allIssued) {
            g_lookMatcher.DisarmCapture(tl_lookCap.activeJobId);
        }
        TryDeliverLookCaptureIfReady();
    }
}

static inline void LookCaptureOnPreDraw() {
    if (!g_lookMatcher.IsCaptureArmed()) return;
    LookCaptureSyncArmedJob();
    if (tl_lookCap.activeJobId == 0u) return;
    // In dual-slot same_frame mode, grab slot 1 right BEFORE HS_VisualFinal(3) or first UI/Font(2) draw!
    if (tl_lookCap.needDual && tl_lookCap.slot0Issued && !tl_lookCap.slot1Issued &&
        (tl_curLookProgRole == 2u || tl_curLookProgRole == 3u)) {
        LookCaptureIssueSlot(1);
    }
}

static inline void LookCaptureOnPostDraw() {
    if (!g_lookMatcher.IsCaptureArmed()) return;
    LookCaptureSyncArmedJob();
    if (tl_lookCap.activeJobId == 0u) return;
    // Capture slot 0 immediately AFTER COLOURCUBE(1) draws (clean pre-HS_VisualFinal / pre-UI baseline!)
    if (!tl_lookCap.slot0Issued && tl_curLookProgRole == 1u) {
        LookCaptureIssueSlot(0);
    }
}

static inline void LookCaptureOnSwapBuffers() {
    if (g_lookMatcher.IsCaptureArmed()) {
        LookCaptureSyncArmedJob();
        if (tl_lookCap.activeJobId != 0u && tl_lookCap.needDual &&
            tl_lookCap.slot0Issued && !tl_lookCap.slot1Issued) {
            // Fallback when HUD is hidden (F1) and HS_VisualFinal is OFF
            LookCaptureIssueSlot(1);
        }
    }
    if (tl_lookCap.activeJobId == 0u) return;
    // Non-blocking poll (timeout = 0 ns) for in-flight Native GL PBO fences
    if (tl_lookCap.slot0Issued && !tl_lookCap.slot0Ready && tl_lookCap.sampler.HasPendingSlot(0)) {
        if (tl_lookCap.sampler.TryHarvestAsync(0, tl_lookCap.img[0])) {
            tl_lookCap.slot0Ready = true;
        }
    }
    if (tl_lookCap.needDual && tl_lookCap.slot1Issued && !tl_lookCap.slot1Ready && tl_lookCap.sampler.HasPendingSlot(1)) {
        if (tl_lookCap.sampler.TryHarvestAsync(1, tl_lookCap.img[1])) {
            tl_lookCap.slot1Ready = true;
        }
    }
    TryDeliverLookCaptureIfReady();
}

static inline void SyncCurProgEntityReceiveUniforms(const hs_world_shadow::ShadowConfig& cfg) {
    if (!tl_curWorldProg || tl_curWorldProg->locRecvTint < 0) return;
    if (!tl_curWorldProg->shadowMaskUnitBound && tl_curWorldProg->locShadowMask >= 0 && g_origGlUniform1i) {
        g_origGlUniform1i(tl_curWorldProg->locShadowMask, static_cast<int32_t>(hs_world_shadow::kCompositeTexUnit));
        tl_curWorldProg->shadowMaskUnitBound = true;
    }
    const uint32_t curGen = tl_worldShadow.FrameRecvGen();
    if (tl_curWorldProg->lastRecvGen != curGen && g_fnGlUniform4fv) {
        float rBounds[4], rTint[4];
        tl_worldShadow.GetEntityReceiveUniforms(cfg, rBounds, rTint);
        if (tl_curWorldProg->locRecvBounds >= 0) {
            g_fnGlUniform4fv(tl_curWorldProg->locRecvBounds, 1, rBounds);
            tl_glCache.InvalidateUniform(tl_curBoundProg, tl_curWorldProg->locRecvBounds);
        }
        g_fnGlUniform4fv(tl_curWorldProg->locRecvTint, 1, rTint);
        tl_glCache.InvalidateUniform(tl_curBoundProg, tl_curWorldProg->locRecvTint);
        tl_curWorldProg->lastRecvGen = curGen;
    }
}

static inline void EnsureShadowCompositeAndBeginWrite(const hs_world_shadow::ShadowConfig& cfg) {
    if (!tl_worldShadow.NeedsCompositeOrBeginWrite(tl_curBoundFbo, cfg)) return;
    // Flush any pending OnGround LAYER_WORLD_BACKGROUND sprites FIRST so our ground shadow quad
    // composites ON TOP of carpets/ponds/boats and STRICTLY BEFORE the first standing entity!
    if (tl_spriteBatcher.HasPending()) {
        tl_spriteBatcher.Flush(hs_sprite_batch::FlushReason::State);
    }
    SyncWorldShadowEngine();
    const bool wasReady  = g_worldShadowReady.load(std::memory_order_relaxed);
    const bool wasFailed = g_worldShadowFailed.load(std::memory_order_relaxed);
    const auto saved = BuildShadowPassState();
    const bool ok = tl_worldShadow.OnBeforeFirstStandingEntity(cfg, saved, GetSyncedGlCache());
    const bool nowReady  = ok && tl_worldShadow.IsReady() && !tl_worldShadow.IsFailed();
    const bool nowFailed = tl_worldShadow.IsFailed();
    if (nowReady) {
        g_worldShadowFailed.store(false, std::memory_order_release);
        g_worldShadowReady.store(true, std::memory_order_release);
        g_worldShadowUsesMax.store(tl_worldShadow.UsesGlMax(), std::memory_order_relaxed);
        g_worldShadowRes.store(tl_worldShadow.Resolution(), std::memory_order_relaxed);
        if (!wasReady || wasFailed) {
            // Bump HD-2D state gen so AnimBloom immediately drops legacy blue-channel projection!
            g_hd2dStateGen.fetch_add(1, std::memory_order_release);
        }
    } else if (nowFailed) {
        g_worldShadowReady.store(false, std::memory_order_release);
        g_worldShadowFailed.store(true, std::memory_order_release);
        g_worldShadowRes.store(0u, std::memory_order_relaxed);
        if (wasReady || !wasFailed) {
            // Bump HD-2D state gen so AnimBloom immediately restores legacy blue-channel projection!
            g_hd2dStateGen.fetch_add(1, std::memory_order_release);
        }
    }
    // Immediately arm 3D height-compared shadow receiving on the already-bound standing program!
    SyncCurProgEntityReceiveUniforms(cfg);
}

static inline void ResolveWindCamRight(float& outCrX, float& outCrZ) noexcept {
    float crX = tl_worldShadow.HasCamRight() ? tl_worldShadow.CamRightX() : tl_cachedShadowCfg.luaCamRightX;
    float crZ = tl_worldShadow.HasCamRight() ? tl_worldShadow.CamRightZ() : tl_cachedShadowCfg.luaCamRightZ;
    if (!std::isfinite(crX) || !std::isfinite(crZ) || (crX * crX + crZ * crZ) < 1e-4f) {
        crX = 0.70710678f;
        crZ = -0.70710678f;
    }
    outCrX = crX;
    outCrZ = crZ;
}

static inline void SyncCurProgWindUniforms() {
    if (!tl_curWorldProg || tl_curWorldProg->locWindParams < 0 || !g_fnGlUniform4fv) return;
    const auto& w = SyncFoliageAndWind();
    const uint32_t curGen = tl_cachedWindGen;
    if (tl_curWorldProg->lastWindGen != curGen) {
        const float wp[4] = { w.windTime, w.swayAmp, w.wakeAmp, w.hasPlayer };
        float crX = 0.70710678f, crZ = -0.70710678f;
        ResolveWindCamRight(crX, crZ);
        const float pl[4] = { w.playerWx, w.playerWz, crX, crZ };
        g_fnGlUniform4fv(tl_curWorldProg->locWindParams, 1, wp);
        tl_glCache.InvalidateUniform(tl_curBoundProg, tl_curWorldProg->locWindParams);
        if (tl_curWorldProg->locWindPlayer >= 0) {
            g_fnGlUniform4fv(tl_curWorldProg->locWindPlayer, 1, pl);
            tl_glCache.InvalidateUniform(tl_curBoundProg, tl_curWorldProg->locWindPlayer);
        }
        tl_curWorldProg->lastWindGen = curGen;
    }
}

static inline void SyncOceanReflUniforms() {
    if (!tl_curWorldProg || tl_curWorldProg->role != WorldProgRole::Ocean) return;
    if (!g_fnGlUniform4fv) return;
    SyncWorldShadowEngine();
    const auto& scfg = GetCachedShadowConfig();
    auto vec4Differs = [](const float a[4], const float b[4]) noexcept {
        return fabsf(a[0] - b[0]) > 1e-5f || fabsf(a[1] - b[1]) > 1e-5f ||
               fabsf(a[2] - b[2]) > 1e-5f || fabsf(a[3] - b[3]) > 1e-5f;
    };
    if (tl_curWorldProg->locOceanEye >= 0) {
        float eye[4];
        if (tl_waterRefl.HasCamEye()) {
            eye[0] = tl_waterRefl.CamEyeX();
            eye[1] = tl_waterRefl.CamEyeY();
            eye[2] = tl_waterRefl.CamEyeZ();
        } else if (scfg.hasLuaCam) {
            eye[0] = scfg.luaCamX + 18.0f;
            eye[1] = 26.0f;
            eye[2] = scfg.luaCamZ + 18.0f;
        } else {
            eye[0] = 18.0f;
            eye[1] = 26.0f;
            eye[2] = 18.0f;
        }
        eye[3] = scfg.windTime;
        if (!tl_curWorldProg->oceanEyeValid || vec4Differs(tl_curWorldProg->lastOceanEye, eye)) {
            g_fnGlUniform4fv(tl_curWorldProg->locOceanEye, 1, eye);
            tl_glCache.InvalidateUniform(tl_curBoundProg, tl_curWorldProg->locOceanEye);
            std::memcpy(tl_curWorldProg->lastOceanEye, eye, sizeof(eye));
            tl_curWorldProg->oceanEyeValid = true;
        }
    }
    if (tl_curWorldProg->locReflCfg < 0) return;
    if (!tl_curWorldProg->reflTexUnitBound && tl_curWorldProg->locReflTex >= 0 && g_origGlUniform1i) {
        g_origGlUniform1i(tl_curWorldProg->locReflTex, static_cast<int32_t>(hs_water_refl::kReflTexUnit));
        tl_curWorldProg->reflTexUnitBound = true;
    }
    const auto& rcfg = GetCachedWaterReflConfig();
    float rBounds[4], rCfg[4];
    tl_waterRefl.BindForOceanSampling(rcfg, tl_spriteBatcher.ActiveUnit(),
                                      GetSyncedGlCache(), IsGlCacheOperational(),
                                      rBounds, rCfg);
    if (tl_curWorldProg->locReflBounds >= 0 &&
        (!tl_curWorldProg->reflBoundsValid || vec4Differs(tl_curWorldProg->lastReflBounds, rBounds))) {
        g_fnGlUniform4fv(tl_curWorldProg->locReflBounds, 1, rBounds);
        tl_glCache.InvalidateUniform(tl_curBoundProg, tl_curWorldProg->locReflBounds);
        std::memcpy(tl_curWorldProg->lastReflBounds, rBounds, sizeof(rBounds));
        tl_curWorldProg->reflBoundsValid = true;
    }
    if (!tl_curWorldProg->reflCfgValid || vec4Differs(tl_curWorldProg->lastReflCfg, rCfg)) {
        g_fnGlUniform4fv(tl_curWorldProg->locReflCfg, 1, rCfg);
        tl_glCache.InvalidateUniform(tl_curBoundProg, tl_curWorldProg->locReflCfg);
        std::memcpy(tl_curWorldProg->lastReflCfg, rCfg, sizeof(rCfg));
        tl_curWorldProg->reflCfgValid = true;
    }
}

static inline void EnsureWaterReflBeginWrite(const hs_water_refl::WaterReflConfig& rcfg) {
    if (!tl_waterRefl.NeedsBeginWrite(tl_curBoundFbo, rcfg)) return;
    if (tl_spriteBatcher.HasPending()) {
        tl_spriteBatcher.Flush(hs_sprite_batch::FlushReason::State);
    }
    SyncWorldShadowEngine();
    const auto saved = BuildShadowPassState();
    const bool ok = tl_waterRefl.OnBeforeFirstStandingEntity(rcfg, saved, GetSyncedGlCache());
    g_waterReflReady.store(ok && tl_waterRefl.IsReady() && !tl_waterRefl.IsFailed(), std::memory_order_relaxed);
}

static void OnSpriteBatchShadowMirror(uint32_t indexCount, void*) {
    if (indexCount == 0u) return;
    const bool wantShadow = tl_worldShadow.BeganWriteThisFrame() &&
                            hs_world_shadow::HasAnyShadowLight(GetCachedShadowConfig());
    const auto& rcfg = GetCachedWaterReflConfig();
    const bool wantRefl   = tl_waterRefl.BeganWriteThisFrame() && rcfg.IsEnabled();
    if (!wantShadow && !wantRefl) return;
    SyncWorldShadowEngine();
    const auto saved = BuildShadowPassState(tl_spriteBatcher.CurrentProgramId());
    if (wantShadow) {
        tl_worldShadow.MirrorBatchedSprites(indexCount, GetCachedShadowConfig(), saved, GetSyncedGlCache(),
                                            tl_spriteBatcher.LastFlushShadowBoundsXZ());
    }
    if (wantRefl) {
        tl_waterRefl.MirrorBatchedSprites(indexCount, rcfg, saved, GetSyncedGlCache());
    }
}

static void OnSpriteBatchPostFlush(uint32_t activeUnit, uint32_t arrayBuf, uint32_t elemBuf, uint32_t batchWposLoc, void*) {
    auto& st = GetSyncedGlCache();
    st.activeUnit      = static_cast<int32_t>(activeUnit);
    st.boundArrayBuf   = arrayBuf;
    st.boundElementBuf = elemBuf;
    for (uint32_t s = 0; s < hs_sprite_batch::kMaxBatchTexSlots; ++s) {
        const uint32_t u = hs_sprite_batch::kBatchTexUnitBase + s;
        if (u < hs_gl_cache::kMaxTexUnits) {
            st.boundTex2D[u] = hs_gl_cache::kInvalidId;
        }
    }
    auto markAttrOff = [&](uint32_t loc) {
        if (loc < hs_gl_cache::kMaxVertexAttribs) {
            const uint16_t bit = static_cast<uint16_t>(1u << loc);
            st.attribKnownMask   |= bit;
            st.attribEnabledMask &= static_cast<uint16_t>(~bit);
        }
    };
    markAttrOff(batchWposLoc);
    markAttrOff(5u); // HS_BATCH_CMULT
    markAttrOff(6u); // HS_BATCH_CADD
    const int32_t locAct = tl_spriteBatcher.CurrentBatchActiveLoc();
    if (locAct >= 0) {
        st.InvalidateUniform(tl_spriteBatcher.CurrentProgramId(), locAct);
    }
}

static inline void SyncSpriteBatcher() {
    const uint32_t gen = g_sbOpsGen.load(std::memory_order_acquire);
    if (tl_sbOpsGen != gen) {
        hs_sprite_batch::GlBatchOps ops{};
        ops.glGenBuffers               = g_fnGlGenBuffers;
        ops.glDeleteBuffers            = g_origGlDeleteBuffers;
        ops.glBindBuffer               = g_origGlBindBuffer;
        ops.glBufferData               = g_origGlBufferData ? g_origGlBufferData : g_fnGlBufferData;
        ops.glBufferSubData            = g_fnGlBufferSubData;
        ops.glMapBufferRange           = g_fnGlMapBufferRange;
        ops.glUnmapBuffer              = g_fnGlUnmapBuffer;
        ops.glActiveTexture            = g_origGlActiveTexture;
        ops.glBindTexture              = g_origGlBindTexture;
        ops.glUniform1f                = g_origGlUniform1f;
        ops.glUniform1i                = g_origGlUniform1i;
        ops.glUniformMatrix4fv         = g_origGlUniformMatrix4fv;
        ops.glVertexAttribPointer      = g_origGlVertexAttribPointer;
        ops.glEnableVertexAttribArray  = g_origGlEnableVertexAttribArray;
        ops.glDisableVertexAttribArray = g_origGlDisableVertexAttribArray;
        ops.glDrawElements             = g_origGlDrawElements;
        ops.glGetIntegerv              = g_fnGlGetIntegerv;
        ops.glGetBufferParameteriv     = g_fnGlGetBufferParameteriv;
        ops.glGetBufferSubData         = g_fnGlGetBufferSubData;
        ops.onPostFlushRestore         = &OnSpriteBatchPostFlush;
        ops.onShadowMirrorBatch        = &OnSpriteBatchShadowMirror;
        ops.callbackCtx                = nullptr;
        tl_spriteBatcher.SetOps(ops);
        tl_sbOpsGen = gen;
    }
    const bool want = g_spriteBatchActive.load(std::memory_order_acquire) &&
                      g_optSpriteBatch.load(std::memory_order_relaxed);
    if (tl_spriteBatcher.IsEnabled() != want) {
        tl_spriteBatcher.SetEnabled(want);
    }
}

static void OnUiBatchPostFlush(uint32_t dstProg, uint32_t activeUnit, uint32_t arrayBuf,
                               uint32_t elemBuf, uint16_t enabledMask, void*) {
    auto& st = GetSyncedGlCache();
    st.curProgram      = dstProg;
    st.activeUnit      = static_cast<int32_t>(activeUnit);
    st.boundArrayBuf   = arrayBuf;
    st.boundElementBuf = elemBuf;
    for (uint32_t s = 0; s < hs_ui_batch::kMaxUiTexSlots; ++s) {
        const uint32_t u = hs_ui_batch::kUiTexUnitBase + s;
        if (u < hs_gl_cache::kMaxTexUnits) {
            st.boundTex2D[u] = hs_gl_cache::kInvalidId;
        }
    }
    for (uint32_t loc = 0u; loc < 4u; ++loc) {
        const uint16_t bit = static_cast<uint16_t>(1u << loc);
        st.attribKnownMask |= bit;
        if ((enabledMask & bit) != 0u) st.attribEnabledMask |= bit;
        else                           st.attribEnabledMask &= static_cast<uint16_t>(~bit);
    }
}

static inline void SyncUiBatcher() {
    const uint32_t gen = g_sbOpsGen.load(std::memory_order_acquire);
    if (tl_uiOpsGen != gen) {
        hs_ui_batch::UiGlOps ops{};
        ops.glCreateShader             = g_fnGlCreateShader;
        ops.glShaderSource             = g_origGlShaderSource;
        ops.glCompileShader            = g_fnGlCompileShader;
        ops.glGetShaderiv              = g_fnGlGetShaderiv;
        ops.glDeleteShader             = g_fnGlDeleteShader;
        ops.glCreateProgram            = g_fnGlCreateProgram;
        ops.glAttachShader             = g_origGlAttachShader;
        ops.glBindAttribLocation       = g_fnGlBindAttribLocation;
        ops.glLinkProgram              = g_origGlLinkProgram;
        ops.glGetProgramiv             = g_fnGlGetProgramiv;
        ops.glDeleteProgram            = g_origGlDeleteProgram;
        ops.glUseProgram               = g_origGlUseProgram;
        ops.glGetUniformLocation       = g_fnGlGetUniformLocation;
        ops.glGenBuffers               = g_fnGlGenBuffers;
        ops.glDeleteBuffers            = g_origGlDeleteBuffers;
        ops.glBindBuffer               = g_origGlBindBuffer;
        ops.glBufferData               = g_origGlBufferData ? g_origGlBufferData : g_fnGlBufferData;
        ops.glBufferSubData            = g_fnGlBufferSubData;
        ops.glMapBufferRange           = g_fnGlMapBufferRange;
        ops.glUnmapBuffer              = g_fnGlUnmapBuffer;
        ops.glActiveTexture            = g_origGlActiveTexture;
        ops.glBindTexture              = g_origGlBindTexture;
        ops.glUniform1i                = g_origGlUniform1i;
        ops.glUniformMatrix4fv         = g_origGlUniformMatrix4fv;
        ops.glVertexAttribPointer      = g_origGlVertexAttribPointer;
        ops.glEnableVertexAttribArray  = g_origGlEnableVertexAttribArray;
        ops.glDisableVertexAttribArray = g_origGlDisableVertexAttribArray;
        ops.glDrawElements             = g_origGlDrawElements;
        ops.glGetIntegerv              = g_fnGlGetIntegerv;
        ops.glGetBufferParameteriv     = g_fnGlGetBufferParameteriv;
        ops.onPostFlushRestore         = &OnUiBatchPostFlush;
        ops.callbackCtx                = nullptr;
        ops.preferHighp                = g_nativeGlActive.load(std::memory_order_acquire);
        tl_uiBatcher.SetOps(ops);
        tl_uiOpsGen = gen;
    }
    const bool want = g_spriteBatchActive.load(std::memory_order_acquire) &&
                      g_optSpriteBatch.load(std::memory_order_relaxed);
    if (tl_uiBatcher.IsEnabled() != want) {
        tl_uiBatcher.SetEnabled(want);
    }
}

static thread_local int64_t tl_rtFrameStartQpc = 0;

static inline void StampRtFrameStart() noexcept {
    if (tl_rtFrameStartQpc == 0) {
        tl_rtFrameStartQpc = NowQpc();
    }
}

static void FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason reason) {
    if (tl_spriteBatcher.HasPending()) {
        SyncSpriteBatcher();
        tl_spriteBatcher.Flush(reason);
    }
    if (tl_uiBatcher.HasPending()) {
        SyncUiBatcher();
        tl_uiBatcher.Flush(reason);
    }
}

static inline void FlushSpriteBatchStateCause(StateFlushCause cause) {
    if (tl_spriteBatcher.HasPending()) {
        const size_t idx = static_cast<size_t>(cause);
        if (idx < static_cast<size_t>(StateFlushCause::Count)) {
            InterlockedIncrement64(&g_sbStateCause[idx]);
        }
        SyncSpriteBatcher();
        tl_spriteBatcher.Flush(hs_sprite_batch::FlushReason::State);
    }
    if (tl_uiBatcher.HasPending()) {
        SyncUiBatcher();
        tl_uiBatcher.Flush(hs_sprite_batch::FlushReason::State);
    }
}

static inline void EnsureUiDstProgramBoundAndSyncCache() {
    if (tl_uiBatcher.EnsureDstProgramBound()) {
        tl_glCache.curProgram = tl_uiBatcher.CurrentProgramId();
    }
}

static inline void FlushSpriteBatchStatsToGlobals() {
    const auto d = tl_spriteBatcher.TakeStatsDelta();
    if (d.drawsIn)      InterlockedAdd64(&g_sbDrawsIn,      static_cast<LONG64>(d.drawsIn));
    if (d.drawsBatched) InterlockedAdd64(&g_sbDrawsBatched, static_cast<LONG64>(d.drawsBatched));
    if (d.flushes)      InterlockedAdd64(&g_sbFlushes,      static_cast<LONG64>(d.flushes));
    if (d.vertsBatched) InterlockedAdd64(&g_sbVertsBatched, static_cast<LONG64>(d.vertsBatched));
    if (d.bypassDraws)  InterlockedAdd64(&g_sbBypassDraws,  static_cast<LONG64>(d.bypassDraws));
    for (size_t i = 0; i < static_cast<size_t>(hs_sprite_batch::FlushReason::Count); ++i) {
        if (d.flReason[i]) InterlockedAdd64(&g_sbFlReason[i], static_cast<LONG64>(d.flReason[i]));
    }
    const auto u = tl_uiBatcher.TakeStatsDelta();
    if (u.drawsIn)      InterlockedAdd64(&g_uiDrawsIn,      static_cast<LONG64>(u.drawsIn));
    if (u.drawsBatched) InterlockedAdd64(&g_uiDrawsBatched, static_cast<LONG64>(u.drawsBatched));
    if (u.flushes)      InterlockedAdd64(&g_uiFlushes,      static_cast<LONG64>(u.flushes));
    if (u.vertsBatched) InterlockedAdd64(&g_uiVertsBatched, static_cast<LONG64>(u.vertsBatched));
    if (u.bypassDraws)  InterlockedAdd64(&g_uiBypassDraws,  static_cast<LONG64>(u.bypassDraws));
    if (u.progSkips)    InterlockedAdd64(&g_uiProgSkips,    static_cast<LONG64>(u.progSkips));
    if (u.readbacks)    InterlockedAdd64(&g_uiReadbacks,    static_cast<LONG64>(u.readbacks));
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

static void ActivateNativeOpenGlOnFirstMakeCurrent();

static void OnVbPoolBindBuffer(uint32_t target, uint32_t buffer, void*) {
    tl_spriteBatcher.OnBindBuffer(target, buffer);
    tl_uiBatcher.OnBindBuffer(target, buffer);
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        st.ShouldCallBindBuffer(target, buffer);
    }
}

static void OnVbPoolInvalidateBuffer(uint32_t buffer, void*) {
    if (buffer != 0) {
        tl_spriteBatcher.OnDeleteBuffers(1, &buffer);
        tl_uiBatcher.OnDeleteBuffers(1, &buffer);
        tl_glCache.OnDeleteBuffers(1, &buffer);
    }
}

static void* const kSynthEglDisplay = reinterpret_cast<void*>(static_cast<uintptr_t>(0x48534531ULL));
static void* const kSynthEglSurface = reinterpret_cast<void*>(static_cast<uintptr_t>(0x48535331ULL));
static void* const kSynthEglConfig  = reinterpret_cast<void*>(static_cast<uintptr_t>(0x00000001ULL));

static uint32_t WINAPI HookEglGetError() {
    if (g_nativeGlActive.load(std::memory_order_acquire)) {
        return g_lastEglError.exchange(hs_native_gl::kEglSuccess, std::memory_order_relaxed);
    }
    return g_origEglGetError ? g_origEglGetError() : hs_native_gl::kEglSuccess;
}

static void* WINAPI HookEglGetDisplay(void* display_id) {
    if (!g_nativeGlProbed.load(std::memory_order_acquire)) {
        AcquireSRWLockExclusive(&g_nativeGlLock);
        if (!g_nativeGlProbed.load(std::memory_order_acquire)) {
            const auto mode = static_cast<hs_native_gl::BackendMode>(g_cfgRenderBackend.load(std::memory_order_relaxed));
            const auto res = hs_native_gl::ProbeNativeEs2Support(mode, g_wglRt);
            if (res.supported) {
                g_activeGlRendererName = !res.renderer.empty() ? res.renderer : "Native OpenGL Compat";
                g_nativeGlActive.store(true, std::memory_order_release);
                LogMsg("[GL_NATIVE] Probe PASS -> activating Native WGL OpenGL Compat (ES2+SteamOverlay, mode=%s, vendor=%s, renderer=%s, version=%s, procs=%d/76, aniso=%d, bufStorage=%d, mapRange=%d)",
                       hs_native_gl::BackendModeName(mode), res.vendor.c_str(), res.renderer.c_str(), res.version.c_str(),
                       res.resolvedProcs, res.hasAniso ? 1 : 0, res.hasBufferStorage ? 1 : 0, res.hasMapBufferRange ? 1 : 0);

                const auto flipMode = static_cast<hs_dxgi_flip::FlipMode>(g_cfgDxgiFlipPresent.load(std::memory_order_relaxed));
                const auto flipRes = hs_dxgi_flip::ProbeDxgiFlipSupport(flipMode, g_wglRt);
                g_dxgiFlipProbedOk.store(flipRes.supported, std::memory_order_release);
                if (flipRes.supported) {
                    LogMsg("[GL_NATIVE][DXGI_FLIP] Probe PASS -> WGL_NV_DX_interop2 Flip-Model ready (mode=%s, allowTearing=%d)",
                           hs_dxgi_flip::FlipModeName(flipMode), flipRes.allowTearing ? 1 : 0);
                } else {
                    LogMsg("[GL_NATIVE][DXGI_FLIP] Disabled / classic WGL fallback (mode=%s, reason=%s)",
                           hs_dxgi_flip::FlipModeName(flipMode), flipRes.reason.c_str());
                }
            } else {
                g_activeGlRendererName = "ANGLE D3D9";
                g_nativeGlActive.store(false, std::memory_order_release);
                LogMsg("[GL_NATIVE] Using ANGLE D3D9 backend (mode=%s, reason=%s, vendor=%s, renderer=%s)",
                       hs_native_gl::BackendModeName(mode), res.reason.c_str(),
                       res.vendor.c_str(), res.renderer.c_str());
            }
            g_nativeGlProbed.store(true, std::memory_order_release);
        }
        ReleaseSRWLockExclusive(&g_nativeGlLock);
    }

    if (g_nativeGlActive.load(std::memory_order_acquire)) {
        return kSynthEglDisplay;
    }
    return g_origEglGetDisplay ? g_origEglGetDisplay(display_id) : nullptr;
}

static uint32_t WINAPI HookEglInitialize(void* dpy, int32_t* major, int32_t* minor) {
    if (g_nativeGlActive.load(std::memory_order_acquire)) {
        if (dpy != kSynthEglDisplay) {
            g_lastEglError.store(hs_native_gl::kEglBadDisplay, std::memory_order_relaxed);
            return 0u;
        }
        if (major) *major = 1;
        if (minor) *minor = 4;
        g_lastEglError.store(hs_native_gl::kEglSuccess, std::memory_order_relaxed);
        return 1u;
    }
    return g_origEglInitialize ? g_origEglInitialize(dpy, major, minor) : 0u;
}

static uint32_t WINAPI HookEglGetConfigs(void* dpy, void** configs, int32_t config_size, int32_t* num_config) {
    if (g_nativeGlActive.load(std::memory_order_acquire)) {
        if (!num_config) {
            g_lastEglError.store(hs_native_gl::kEglBadAttribute, std::memory_order_relaxed);
            return 0u;
        }
        if (!configs || config_size <= 0) {
            *num_config = 1;
        } else {
            configs[0] = kSynthEglConfig;
            *num_config = 1;
        }
        g_lastEglError.store(hs_native_gl::kEglSuccess, std::memory_order_relaxed);
        return 1u;
    }
    return g_origEglGetConfigs ? g_origEglGetConfigs(dpy, configs, config_size, num_config) : 0u;
}

static uint32_t WINAPI HookEglChooseConfig(void* dpy, const int32_t* attrib_list, void** configs, int32_t config_size, int32_t* num_config) {
    if (g_nativeGlActive.load(std::memory_order_acquire)) {
        return HookEglGetConfigs(dpy, configs, config_size, num_config);
    }
    return g_origEglChooseConfig ? g_origEglChooseConfig(dpy, attrib_list, configs, config_size, num_config) : 0u;
}

static uint32_t WINAPI HookEglGetConfigAttrib(void* dpy, void* config, int32_t attribute, int32_t* value) {
    if (g_nativeGlActive.load(std::memory_order_acquire)) {
        if (!value || !hs_native_gl::GetEglConfigAttribValue(attribute, value)) {
            g_lastEglError.store(hs_native_gl::kEglBadAttribute, std::memory_order_relaxed);
            return 0u;
        }
        g_lastEglError.store(hs_native_gl::kEglSuccess, std::memory_order_relaxed);
        return 1u;
    }
    return g_origEglGetConfigAttrib ? g_origEglGetConfigAttrib(dpy, config, attribute, value) : 0u;
}

static void* WINAPI HookEglCreateWindowSurface(void* dpy, void* config, HWND win, const int32_t* attrib_list) {
    if (g_nativeGlActive.load(std::memory_order_acquire)) {
        if (!win) {
            g_lastEglError.store(hs_native_gl::kEglBadNativeWindow, std::memory_order_relaxed);
            return nullptr;
        }
        AcquireSRWLockExclusive(&g_nativeGlLock);
        if (g_dxgiFlipProbedOk.load(std::memory_order_acquire)) {
            auto& flip = GetDxgiFlip();
            if (flip.IsSwapChainReady() && flip.GameHwnd() == win) {
                ReleaseSRWLockExclusive(&g_nativeGlLock);
                g_lastEglError.store(hs_native_gl::kEglSuccess, std::memory_order_relaxed);
                return kSynthEglSurface;
            }
            std::string flipReason;
            if (flip.InitSwapChain(g_wglRt, win, flipReason)) {
                if (g_nativeGameHwnd && g_nativeGameHdc) {
                    ReleaseDC(g_nativeGameHwnd, g_nativeGameHdc);
                    g_nativeGameHdc = nullptr;
                }
                g_nativeGameHwnd = win;
                g_nativeGameHdc  = nullptr;
                LogMsg("[GL_NATIVE][DXGI_FLIP] SwapChain ready (%ux%u, %s, buffers=%u, allowTearing=%d, dummyHwnd=%p, gameHwnd=%p)",
                       flip.Width(), flip.Height(), flip.SwapEffectName(), flip.BufferCount(),
                       flip.AllowTearing() ? 1 : 0, flip.DummyHwnd(), win);
                ReleaseSRWLockExclusive(&g_nativeGlLock);
                g_lastEglError.store(hs_native_gl::kEglSuccess, std::memory_order_relaxed);
                return kSynthEglSurface;
            }
            LogMsg("[GL_NATIVE][DXGI_FLIP] SwapChain init failed (%s) -> fail-closed fallback to classic WGL",
                   flipReason.c_str());
            g_dxgiFlipProbedOk.store(false, std::memory_order_release);
        }
        HDC hdc = nullptr;
        if (g_nativeGameHwnd == win && g_nativeGameHdc != nullptr) {
            hdc = g_nativeGameHdc;
            if (!hs_native_gl::EnsureWindowPixelFormat(g_wglRt, hdc)) {
                ReleaseSRWLockExclusive(&g_nativeGlLock);
                g_lastEglError.store(hs_native_gl::kEglBadMatch, std::memory_order_relaxed);
                return nullptr;
            }
        } else {
            hdc = GetDC(win);
            if (!hdc || !hs_native_gl::EnsureWindowPixelFormat(g_wglRt, hdc)) {
                if (hdc) ReleaseDC(win, hdc);
                ReleaseSRWLockExclusive(&g_nativeGlLock);
                g_lastEglError.store(hs_native_gl::kEglBadMatch, std::memory_order_relaxed);
                return nullptr;
            }
            if (g_nativeGameHwnd && g_nativeGameHdc) {
                ReleaseDC(g_nativeGameHwnd, g_nativeGameHdc);
            }
            g_nativeGameHwnd = win;
            g_nativeGameHdc  = hdc;
        }
        ReleaseSRWLockExclusive(&g_nativeGlLock);
        g_lastEglError.store(hs_native_gl::kEglSuccess, std::memory_order_relaxed);
        return kSynthEglSurface;
    }
    return g_origEglCreateWindowSurface ? g_origEglCreateWindowSurface(dpy, config, win, attrib_list) : nullptr;
}

static uint32_t WINAPI HookEglSwapInterval(void* dpy, int32_t interval) {
    g_pendingSwapInterval.store(interval, std::memory_order_relaxed);
    if (g_nativeGlActive.load(std::memory_order_acquire)) {
        if (!GetDxgiFlip().IsSwapChainReady()) {
            if (g_wglRt.pWglSwapIntervalExt && g_wglRt.pWglGetCurrentContext && g_wglRt.pWglGetCurrentContext()) {
                g_wglRt.pWglSwapIntervalExt(interval);
            }
        }
        g_lastEglError.store(hs_native_gl::kEglSuccess, std::memory_order_relaxed);
        return 1u;
    }
    return g_origEglSwapInterval ? g_origEglSwapInterval(dpy, interval) : 0u;
}

static void* WINAPI HookEglCreateContext(void* dpy, void* config, void* share_context, const int32_t* attrib_list) {
    if (g_nativeGlActive.load(std::memory_order_acquire)) {
        AcquireSRWLockExclusive(&g_nativeGlLock);
        auto& flip = GetDxgiFlip();
        const bool useFlipDummy = flip.IsSwapChainReady();
        HWND hwnd = useFlipDummy ? flip.DummyHwnd() : g_nativeGameHwnd;
        HDC  hdc  = useFlipDummy ? flip.DummyHdc()  : g_nativeGameHdc;
        HGLRC rc = nullptr;
        const bool ok = hs_native_gl::CreateGameEs2Context(
            g_wglRt, hwnd, hdc, rc, reinterpret_cast<HGLRC>(share_context));
        if (!useFlipDummy) {
            g_nativeGameHdc = hdc;
        }
        HWND logGameHwnd = g_nativeGameHwnd;
        ReleaseSRWLockExclusive(&g_nativeGlLock);
        if (!ok || !rc) {
            g_lastEglError.store(hs_native_gl::kEglBadContext, std::memory_order_relaxed);
            return nullptr;
        }
        if (useFlipDummy) {
            LogMsg("[GL_NATIVE] Created hardware WGL Compat HGLRC=%p on DXGI-Flip dummy HWND=%p (presenting to game HWND=%p)",
                   rc, hwnd, logGameHwnd);
        } else {
            LogMsg("[GL_NATIVE] Created hardware WGL ES2 HGLRC=%p on game HWND=%p", rc, hwnd);
        }
        g_lastEglError.store(hs_native_gl::kEglSuccess, std::memory_order_relaxed);
        EraseContextProgTables(reinterpret_cast<void*>(rc));
        return reinterpret_cast<void*>(rc);
    }
    void* created = g_origEglCreateContext ? g_origEglCreateContext(dpy, config, share_context, attrib_list) : nullptr;
    if (created) {
        EraseContextProgTables(created);
    }
    return created;
}

static void* WINAPI HookEglGetProcAddress(const char* procname) {
    if (g_nativeGlActive.load(std::memory_order_acquire)) {
        if (!procname || strncmp(procname, "egl", 3) == 0 || strncmp(procname, "glEGL", 5) == 0) {
            return nullptr;
        }
        if (!g_wglRt.pWglGetCurrentContext || g_wglRt.pWglGetCurrentContext() == nullptr) {
            return nullptr;
        }
        return g_wglRt.ResolveGlProc(procname);
    }
    return g_origEglGetProcAddress ? g_origEglGetProcAddress(procname) : nullptr;
}

static uint32_t WINAPI HookEglMakeCurrent(void* dpy, void* draw, void* read, void* ctx) {
    const void* prevCtx = tl_glCache.curContext;
    if (prevCtx != nullptr) {
        // Always flush pending sprite/UI batches while prevCtx is still current (even if prevCtx == ctx,
        // because eglMakeCurrent may be rebinding draw/read surfaces!).
        FlushSpriteBatchIfPending();
        tl_spriteBatcher.CommitDeferredMatrixW();
        tl_uiBatcher.FlushDeferredStateForCurrentProg();
    }
    if (prevCtx != ctx) {
        if (prevCtx != nullptr) {
            // Free per-context scratch GPU objects WHILE prevCtx is still current,
            // but NEVER discard per-context program tables before eglMakeCurrent succeeds (preserves A->B->A!).
            tl_spriteBatcher.ReleaseGpuResources();
            tl_uiBatcher.ResetContextState(true);
            tl_extProf.ResetContext(true);
            tl_worldShadow.ResetContext(true);
            tl_waterRefl.ResetContext(true);
            tl_combatVfx.ResetContext(true);
            tl_lookCap.OnContextReset();
            if (GetDxgiFlip().IsActive()) {
                GetDxgiFlip().ReleaseGlResources(true);
            }
            g_vbPool.ResetAndRelease(true);
        } else {
            tl_uiBatcher.ResetContextState(false);
            tl_extProf.ResetContext(false);
            tl_worldShadow.ResetContext(false);
            tl_waterRefl.ResetContext(false);
            tl_combatVfx.ResetContext(false);
            tl_lookCap.OnContextReset();
            g_vbPool.ResetAndRelease(false);
        }
    }
    uint32_t ok = 0u;
    if (g_nativeGlActive.load(std::memory_order_acquire)) {
        if (!ctx) {
            ok = (g_wglRt.pWglMakeCurrent && g_wglRt.pWglMakeCurrent(nullptr, nullptr)) ? 1u : 0u;
        } else {
            auto& flip = GetDxgiFlip();
            const bool useFlipDummy = flip.IsSwapChainReady();
            HDC hdc = useFlipDummy ? flip.DummyHdc() : g_nativeGameHdc;
            if (!useFlipDummy && !hdc && g_nativeGameHwnd) {
                hdc = GetDC(g_nativeGameHwnd);
                g_nativeGameHdc = hdc;
            }
            ok = (g_wglRt.pWglMakeCurrent && hdc &&
                  g_wglRt.pWglMakeCurrent(hdc, reinterpret_cast<HGLRC>(ctx))) ? 1u : 0u;
            if (ok && !g_nativeIatBound.load(std::memory_order_acquire)) {
                AcquireSRWLockExclusive(&g_nativeGlLock);
                if (!g_nativeIatBound.load(std::memory_order_acquire)) {
                    ActivateNativeOpenGlOnFirstMakeCurrent();
                    g_nativeIatBound.store(true, std::memory_order_release);
                }
                ReleaseSRWLockExclusive(&g_nativeGlLock);
            }
            if (ok && useFlipDummy && !flip.IsActive()) {
                std::string flipReason;
                if (flip.InitGlResources(g_wglRt, flipReason)) {
                    AcquireSRWLockExclusive(&g_nativeGlLock);
                    if (g_activeGlRendererName.find("[DXGI-Flip]") == std::string::npos) {
                        g_activeGlRendererName += " [DXGI-Flip]";
                    }
                    ReleaseSRWLockExclusive(&g_nativeGlLock);
                    LogMsg("[GL_NATIVE][DXGI_FLIP] Interop ACTIVE (offscreenFbo=%u, %ux%u, %s, allowTearing=%d)",
                           flip.OffscreenFbo(), flip.Width(), flip.Height(),
                           flip.SwapEffectName(), flip.AllowTearing() ? 1 : 0);
                } else {
                    LogMsg("[GL_NATIVE][DXGI_FLIP] Interop InitGlResources FAILED (%s)", flipReason.c_str());
                }
            }
            if (ok && !useFlipDummy && g_wglRt.pWglSwapIntervalExt) {
                g_wglRt.pWglSwapIntervalExt(g_pendingSwapInterval.load(std::memory_order_relaxed));
            }
        }
    } else {
        ok = g_origEglMakeCurrent ? g_origEglMakeCurrent(dpy, draw, read, ctx) : 0u;
    }
    if (ok) {
        tl_glCache.OnMakeCurrent(ctx);
        if (prevCtx != ctx) {
            if (prevCtx != nullptr) {
                SaveActiveContextProgTables(prevCtx);
            }
            LoadContextProgTables(ctx);
            tl_spriteBatcher.ResetContext(ctx);
            tl_curWorldProg = nullptr;
            tl_curBoundProg = 0u;
            tl_curBoundFbo  = 0u;
            const bool wasShadowReady = g_worldShadowReady.exchange(false, std::memory_order_acq_rel);
            g_worldShadowFailed.store(false, std::memory_order_release);
            g_worldShadowRes.store(0u, std::memory_order_relaxed);
            g_waterReflReady.store(false, std::memory_order_relaxed);
            g_waterReflDraws.store(0u, std::memory_order_relaxed);
            if (wasShadowReady) {
                g_hd2dStateGen.fetch_add(1, std::memory_order_release);
            }
            if (ctx != nullptr && g_fnGlGetIntegerv) {
                int32_t drvFbo = 0;
                g_fnGlGetIntegerv(0x8CA6u /* GL_FRAMEBUFFER_BINDING */, &drvFbo);
                const uint32_t offFbo = GetDxgiFlip().OffscreenFbo();
                if (drvFbo > 0 && static_cast<uint32_t>(drvFbo) != offFbo) {
                    tl_curBoundFbo = static_cast<uint32_t>(drvFbo);
                }
                int32_t drvVp[4] = {};
                g_fnGlGetIntegerv(0x0BA2u /* GL_VIEWPORT */, drvVp);
                if (drvVp[2] > 0 && drvVp[3] > 0) {
                    std::memcpy(tl_curVp, drvVp, sizeof(drvVp));
                }
                int32_t drvProg = 0;
                g_fnGlGetIntegerv(0x8B8Du /* GL_CURRENT_PROGRAM */, &drvProg);
                if (drvProg > 0) {
                    tl_curBoundProg = static_cast<uint32_t>(drvProg);
                    auto wit = tl_worldProgs.find(tl_curBoundProg);
                    if (wit != tl_worldProgs.end()) {
                        tl_curWorldProg = &wit->second;
                    }
                }
            }
        }
    } else {
        // MakeCurrent failed: keep prevCtx's program tables intact (do NOT wipe!) and invalidate GL bindings only.
        // When prevCtx == ctx (pre-switch cleanup was skipped), preserve live GPU handles in SpriteBatcher /
        // UiBatcher / WorldShadow so nothing is orphaned or leaked while prevCtx stays active!
        tl_glCache.ResetBindingsOnly();
        tl_spriteBatcher.ResetContext(const_cast<void*>(prevCtx));
        if (prevCtx == ctx && prevCtx != nullptr) {
            tl_uiBatcher.ResetBindingsOnly();
        } else {
            tl_uiBatcher.ResetContextState(false);
            const bool wasShadowReady = g_worldShadowReady.exchange(false, std::memory_order_acq_rel);
            g_worldShadowRes.store(0u, std::memory_order_relaxed);
            if (wasShadowReady) {
                g_hd2dStateGen.fetch_add(1, std::memory_order_release);
            }
        }
    }
    return ok;
}

static uint32_t WINAPI HookEglSwapBuffers(void* dpy, void* surface) {
    FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason::End);
    FlushSpriteBatchStatsToGlobals();
    if (tl_combatVfx.ShouldRenderOnLeaveFbo(tl_curBoundFbo, 0xFFFFFFFFu)) {
        const auto saved = BuildShadowPassState();
        tl_combatVfx.RenderIntoPrimaryRt(g_combatImpact, NowQpc(), g_qpcInvFreq, saved, GetSyncedGlCache());
    }
    tl_combatVfx.OnEndFrame();
    g_combatVfxDraws.store(tl_combatVfx.TotalDraws(), std::memory_order_relaxed);
    tl_worldShadow.OnEndFrame();
    tl_waterRefl.OnEndFrame();
    g_waterReflReady.store(tl_waterRefl.IsReady() && !tl_waterRefl.IsFailed(), std::memory_order_relaxed);
    g_waterReflDraws.store(tl_waterRefl.LastFrameReflDraws(), std::memory_order_relaxed);
    if (tl_worldShadow.IsReady() && !tl_worldShadow.IsFailed()) {
        g_worldShadowReady.store(true, std::memory_order_relaxed);
        g_worldShadowUsesMax.store(tl_worldShadow.UsesGlMax(), std::memory_order_relaxed);
        g_worldShadowRes.store(tl_worldShadow.Resolution(), std::memory_order_relaxed);
        g_worldShadowDraws.store(tl_worldShadow.LastFrameShadowDraws(), std::memory_order_relaxed);
        g_worldShadowBatchDraws.store(tl_worldShadow.LastFrameBatchedDraws(), std::memory_order_relaxed);
        g_worldShadowUnbatchedDraws.store(tl_worldShadow.LastFrameUnbatchedDraws(), std::memory_order_relaxed);
    }
    if (g_extProfCount.load(std::memory_order_acquire) > 0u) {
        tl_extProf.DrainAllActive();
    }
    LookCaptureOnSwapBuffers();
    const int64_t t0 = NowQpc();
    if (tl_rtFrameStartQpc != 0 && t0 > tl_rtFrameStartQpc && g_qpcToUs > 0.0) {
        const double rtUs = static_cast<double>(t0 - tl_rtFrameStartQpc) * g_qpcToUs;
        if (rtUs >= 0.0 && rtUs <= 60000000.0) {
            const uint32_t rtU32 = static_cast<uint32_t>(rtUs);
            g_lastRenderThreadUs.store(rtU32, std::memory_order_relaxed);
            g_frameRenderThreadUs.fetch_add(rtU32, std::memory_order_relaxed);
            if (rtUs < 500000.0) {
                UpdateAtomicEma(g_emaRenderThreadUs, static_cast<float>(rtUs), 0.06f);
            }
        }
    }
    uint32_t rc = 0u;
    if (g_nativeGlActive.load(std::memory_order_acquire)) {
        tl_uiBatcher.EnsureDstProgramBound();
        auto& flip = GetDxgiFlip();
        if (flip.IsActive()) {
            const int32_t interval = g_pendingSwapInterval.load(std::memory_order_relaxed);
            rc = flip.Present(interval) ? 1u : 0u;
        } else {
            rc = (g_wglRt.pSwapBuffers && g_nativeGameHdc && g_wglRt.pSwapBuffers(g_nativeGameHdc)) ? 1u : 0u;
        }
        // Steam Overlay / RTSS / GPU overlays may mutate GL bindings inside SwapBuffers:
        // invalidate cached bindings (keeping per-program uniform shadow table) so next frame is 100% immune!
        tl_glCache.ResetBindingsOnly();
        tl_uiBatcher.OnPostSwapBuffers();
    } else if (g_origEglSwapBuffers) {
        rc = g_origEglSwapBuffers(dpy, surface);
    }
    const int64_t t1 = NowQpc();
    g_rtThreadId.store(GetCurrentThreadId(), std::memory_order_relaxed);
    g_rtSwapSeq.fetch_add(1u, std::memory_order_release);
    const double swapUs = static_cast<double>(t1 - t0) * g_qpcToUs;
    tl_rtFrameStartQpc = 0;
    if (swapUs >= 0.0 && swapUs <= 60000000.0) {
        const uint32_t swapU32 = static_cast<uint32_t>(swapUs);
        g_lastSwapUs.store(swapU32, std::memory_order_relaxed);
        g_frameSwapUs.fetch_add(swapU32, std::memory_order_relaxed);
        if (swapUs < 1000000.0) {
            UpdateAtomicEma(g_emaSwapUs, static_cast<float>(swapUs), 0.06f);
        }
    }
    if (g_vbPoolHooked.load(std::memory_order_acquire)) {
        if (g_optVbPool.load(std::memory_order_relaxed)) {
            g_vbPool.OnEndFrame();
        } else if (g_vbPool.HasEntries()) {
            g_vbPool.ResetAndRelease(true);
        }
    }
    return rc;
}

static void WINAPI HookGlActiveTexture(uint32_t texture) {
    tl_spriteBatcher.OnActiveTexture(texture);
    tl_uiBatcher.OnActiveTexture(texture);
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
    if (tl_spriteBatcher.HasPending()) SyncSpriteBatcher();
    tl_spriteBatcher.OnBindTexture(target, texture);
    tl_uiBatcher.OnBindTexture(target, texture);
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
    if (tl_spriteBatcher.HasPending()) SyncSpriteBatcher();
    tl_spriteBatcher.OnDeleteTextures(n, textures);
    if (tl_uiBatcher.HasPending()) SyncUiBatcher();
    tl_uiBatcher.OnDeleteTextures(n, textures);
    tl_glCache.OnDeleteTextures(n, textures);
    if (g_origGlDeleteTextures) {
        g_origGlDeleteTextures(n, textures);
    }
}

static void WINAPI HookGlBlendFunc(uint32_t sfactor, uint32_t dfactor) {
    if (!g_origGlBlendFunc) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsBlendFuncRedundant(sfactor, dfactor);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Blend);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordBlendFunc(sfactor, dfactor);
    }
    g_origGlBlendFunc(sfactor, dfactor);
}

static void WINAPI HookGlDepthMask(uint8_t flag) {
    if (!g_origGlDepthMask) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsDepthMaskRedundant(flag);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Depth);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordDepthMask(flag);
    }
    g_origGlDepthMask(flag);
}

static void WINAPI HookGlEnable(uint32_t cap) {
    if (!g_origGlEnable) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsEnableDisableRedundant(cap, true);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Cap);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordEnableDisable(cap, true);
    }
    g_origGlEnable(cap);
}

static void WINAPI HookGlDisable(uint32_t cap) {
    if (!g_origGlDisable) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsEnableDisableRedundant(cap, false);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Cap);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordEnableDisable(cap, false);
    }
    g_origGlDisable(cap);
}

static void WINAPI HookGlBindBuffer(uint32_t target, uint32_t buffer) {
    tl_spriteBatcher.OnBindBuffer(target, buffer);
    tl_uiBatcher.OnBindBuffer(target, buffer);
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
    if (n <= 0 || !buffers) return;
    tl_spriteBatcher.OnDeleteBuffers(n, buffers);
    tl_uiBatcher.OnDeleteBuffers(n, buffers);
    tl_glCache.OnDeleteBuffers(n, buffers);
    if (g_optVbPool.load(std::memory_order_relaxed) && g_vbPoolHooked.load(std::memory_order_acquire)) {
        if (n <= 64) {
            uint32_t keep[64];
            int32_t keepN = 0;
            for (int32_t i = 0; i < n; ++i) {
                const uint32_t id = buffers[i];
                if (id == 0u || !g_vbPool.TryRecycle(id)) {
                    keep[keepN++] = id;
                }
            }
            if (keepN > 0 && g_origGlDeleteBuffers) {
                g_origGlDeleteBuffers(keepN, keep);
            }
            return;
        }
        std::vector<uint32_t> keepVec;
        keepVec.reserve(static_cast<size_t>(n));
        for (int32_t i = 0; i < n; ++i) {
            const uint32_t id = buffers[i];
            if (id == 0u || !g_vbPool.TryRecycle(id)) {
                keepVec.push_back(id);
            }
        }
        if (!keepVec.empty() && g_origGlDeleteBuffers) {
            g_origGlDeleteBuffers(static_cast<int32_t>(keepVec.size()), keepVec.data());
        }
        return;
    }
    if (g_origGlDeleteBuffers) g_origGlDeleteBuffers(n, buffers);
}

static void WINAPI HookGlBlendEquation(uint32_t mode) {
    if (!g_origGlBlendEquation) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsBlendEquationRedundant(mode);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Blend);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordBlendEquation(mode);
    }
    g_origGlBlendEquation(mode);
}

static void WINAPI HookGlColorMask(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    if (!g_origGlColorMask) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsColorMaskRedundant(r, g, b, a);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Other);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordColorMask(r, g, b, a);
    }
    g_origGlColorMask(r, g, b, a);
}

static void WINAPI HookGlCullFace(uint32_t mode) {
    if (!g_origGlCullFace) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsCullFaceRedundant(mode);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Other);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordCullFace(mode);
    }
    g_origGlCullFace(mode);
}

static void WINAPI HookGlDepthFunc(uint32_t func) {
    if (!g_origGlDepthFunc) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsDepthFuncRedundant(func);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Depth);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordDepthFunc(func);
    }
    g_origGlDepthFunc(func);
}

static void WINAPI HookGlEnableVertexAttribArray(uint32_t index) {
    if (tl_spriteBatcher.HasPending()) SyncSpriteBatcher();
    tl_spriteBatcher.OnEnableDisableVertexAttribArray(index, true);
    tl_uiBatcher.OnEnableDisableVertexAttribArray(index, true);
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
    if (tl_spriteBatcher.HasPending()) SyncSpriteBatcher();
    tl_spriteBatcher.OnEnableDisableVertexAttribArray(index, false);
    tl_uiBatcher.OnEnableDisableVertexAttribArray(index, false);
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
        const bool redundant = st.IsFrontFaceRedundant(mode);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Other);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordFrontFace(mode);
    }
    g_origGlFrontFace(mode);
}

static void WINAPI HookGlScissor(int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!g_origGlScissor) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsScissorRedundant(x, y, width, height);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Viewport);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordScissor(x, y, width, height);
    }
    g_origGlScissor(x, y, width, height);
}

static void WINAPI HookGlViewport(int32_t x, int32_t y, int32_t width, int32_t height) {
    if (!g_origGlViewport) {
        tl_curVp[0] = x; tl_curVp[1] = y; tl_curVp[2] = width; tl_curVp[3] = height;
        return;
    }
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsViewportRedundant(x, y, width, height);
        FlushGlCacheStatsIfReady(st);
        if (redundant) {
            tl_curVp[0] = x; tl_curVp[1] = y; tl_curVp[2] = width; tl_curVp[3] = height;
            return;
        }
    }
    FlushSpriteBatchStateCause(StateFlushCause::Viewport);
    tl_curVp[0] = x;
    tl_curVp[1] = y;
    tl_curVp[2] = width;
    tl_curVp[3] = height;
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordViewport(x, y, width, height);
    }
    g_origGlViewport(x, y, width, height);
}

static void WINAPI HookGlClearColor(float r, float g, float b, float a) {
    tl_curClearColor[0] = r;
    tl_curClearColor[1] = g;
    tl_curClearColor[2] = b;
    tl_curClearColor[3] = a;
    tl_hasClearColor = true;
    if (g_origGlClearColor) {
        g_origGlClearColor(r, g, b, a);
    }
}

static void WINAPI HookGlUniform1f(int32_t location, float x) {
    if (!g_origGlUniform1f) return;
    EnsureUiDstProgramBoundAndSyncCache();
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, 1, 0, hs_gl_cache::UNI_1F, &x, sizeof(float));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason::UniOther);
    g_origGlUniform1f(location, x);
}

static void WINAPI HookGlUniform1i(int32_t location, int32_t x) {
    if (!g_origGlUniform1i) return;
    if (tl_uiBatcher.IsCurrentProgramEligible()) {
        SyncUiBatcher();
        if (tl_uiBatcher.OnUniform1i(location, x)) {
            tl_glCache.InvalidateUniform(tl_uiBatcher.CurrentProgramId(), location);
            return;
        }
    }
    EnsureUiDstProgramBoundAndSyncCache();
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, 1, 0, hs_gl_cache::UNI_1I, &x, sizeof(int32_t));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    if (tl_spriteBatcher.HasPending()) SyncSpriteBatcher();
    tl_spriteBatcher.OnUniform1i(location, x);
    g_origGlUniform1i(location, x);
}

static void WINAPI HookGlUniform2fv(int32_t location, int32_t count, const float* v) {
    if (!g_origGlUniform2fv) return;
    EnsureUiDstProgramBoundAndSyncCache();
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, count, 0, hs_gl_cache::UNI_2FV, v, 2 * sizeof(float));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason::UniOther);
    g_origGlUniform2fv(location, count, v);
}

static void WINAPI HookGlUniform3fv(int32_t location, int32_t count, const float* v) {
    if (!g_origGlUniform3fv) return;
    if (tl_curWorldProg && location >= 0 && location == tl_curWorldProg->locParams && count == 1 && v != nullptr) {
        tl_curWorldProg->lastParamsY = v[1];
    }
    EnsureUiDstProgramBoundAndSyncCache();
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, count, 0, hs_gl_cache::UNI_3FV, v, 3 * sizeof(float));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    if (tl_spriteBatcher.HasPending()) SyncSpriteBatcher();
    tl_spriteBatcher.OnUniform3fv(location, count, v);
    if (tl_uiBatcher.HasPending()) {
        SyncUiBatcher();
        tl_uiBatcher.Flush(hs_sprite_batch::FlushReason::Uni3);
    }
    g_origGlUniform3fv(location, count, v);
}

static void WINAPI HookGlUniform4fv(int32_t location, int32_t count, const float* v) {
    if (!g_origGlUniform4fv) return;
    EnsureUiDstProgramBoundAndSyncCache();
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, count, 0, hs_gl_cache::UNI_4FV, v, 4 * sizeof(float));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason::Uni4);
    g_origGlUniform4fv(location, count, v);
}

static void WINAPI HookGlUniformMatrix3fv(int32_t location, int32_t count, uint8_t transpose, const float* value) {
    if (!g_origGlUniformMatrix3fv) return;
    EnsureUiDstProgramBoundAndSyncCache();
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, count, transpose, hs_gl_cache::UNI_MAT3, value, 9 * sizeof(float));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason::UniMat);
    g_origGlUniformMatrix3fv(location, count, transpose, value);
}

static void WINAPI HookGlUniformMatrix4fv(int32_t location, int32_t count, uint8_t transpose, const float* value) {
    if (!g_origGlUniformMatrix4fv) return;
    if (tl_curWorldProg && location >= 0 && count == 1 && transpose == 0 && value != nullptr) {
        if (location == tl_curWorldProg->locMatrixP) {
            tl_worldShadow.OnMatrixP(value);
            tl_combatVfx.OnMatrixP(value);
        } else if (location == tl_curWorldProg->locMatrixV) {
            tl_worldShadow.OnMatrixV(value);
            tl_waterRefl.OnMatrixV(value);
            tl_combatVfx.OnMatrixV(value);
        } else if (location == tl_curWorldProg->locPv) {
            tl_worldShadow.OnMatrixPV(value);
            tl_combatVfx.OnMatrixPV(value);
        } else if (location == tl_curWorldProg->locMatrixW) {
            std::memcpy(tl_curWorldProg->lastMatrixW, value, sizeof(tl_curWorldProg->lastMatrixW));
            tl_curWorldProg->hasMatrixW = true;
        } else if (location == tl_curWorldProg->locFastAnimXform) {
            std::memcpy(tl_curWorldProg->lastXform, value, sizeof(tl_curWorldProg->lastXform));
            tl_curWorldProg->hasXform = true;
        } else if (location == tl_curWorldProg->locColourXform) {
            tl_curWorldProg->lastColorAlpha = value[15];
        }
    }
    SyncSpriteBatcher();
    if (tl_spriteBatcher.TryAbsorbMatrixW(location, count, transpose, value) ||
        tl_spriteBatcher.TryAbsorbColourXform(location, count, transpose, value)) {
        tl_glCache.InvalidateUniform(tl_spriteBatcher.CurrentProgramId(), location);
        return;
    }
    if (tl_uiBatcher.IsCurrentProgramEligible()) {
        SyncUiBatcher();
        if (tl_uiBatcher.TryAbsorbMatrix4(location, count, transpose, value)) {
            tl_glCache.InvalidateUniform(tl_uiBatcher.CurrentProgramId(), location);
            return;
        }
    }
    EnsureUiDstProgramBoundAndSyncCache();
    if (IsGlUniformCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool call = st.ShouldCallUniform(location, count, transpose, hs_gl_cache::UNI_MAT4, value, 16 * sizeof(float));
        FlushGlCacheStatsIfReady(st);
        if (!call) return;
    }
    FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason::UniMat);
    g_origGlUniformMatrix4fv(location, count, transpose, value);
}

static void WINAPI HookGlBufferData(uint32_t target, ptrdiff_t size, const void* data, uint32_t usage) {
    SyncSpriteBatcher();
    const uint32_t bufId = tl_spriteBatcher.BoundBufferForTarget(target);
    if (bufId != 0u) {
        const bool isDyn = (usage != 0x88E4u /*GL_STATIC_DRAW*/);
        // Capture all small VBOs/IBOs (<= 64 KB) on RenderThread so UI/Font draws NEVER hit
        // synchronous glMapBufferRange(GL_MAP_READ_BIT) GPU pipeline stalls (tl_inCacheRenderUi
        // lives on MainThread, so gating on it missed RenderThread UI uploads!).
        const bool captureUi = g_optSpriteBatch.load(std::memory_order_relaxed) &&
                               (size > 0 && static_cast<size_t>(size) <= hs_ui_batch::kMaxUiShadowCaptureBytes);
        if (size > 0 && data != nullptr) {
            tl_spriteBatcher.OnBufferUpload(bufId, data, static_cast<size_t>(size), isDyn);
            tl_uiBatcher.OnBufferUpload(bufId, data, static_cast<size_t>(size), captureUi);
        } else {
            tl_spriteBatcher.OnBufferUpload(bufId, nullptr, 0u, isDyn);
            tl_uiBatcher.OnBufferUpload(bufId, nullptr, 0u, false);
        }
    }
    if (g_origGlBufferData) {
        g_origGlBufferData(target, size, data, usage);
    }
}

static void WINAPI HookGlVertexAttribPointer(uint32_t index, int32_t size, uint32_t type, uint8_t normalized, int32_t stride, const void* ptr) {
    tl_spriteBatcher.OnVertexAttribPointer(index, size, type, normalized, stride, ptr);
    tl_uiBatcher.OnVertexAttribPointer(index, size, type, normalized, stride, ptr);
    if (g_origGlVertexAttribPointer) {
        g_origGlVertexAttribPointer(index, size, type, normalized, stride, ptr);
    }
}

static std::atomic<uint32_t> g_sbDiagSeenMask{0};

static void LogSpriteBatchDiagOnce(const char* fn, uint32_t mode, int32_t firstOrType, int32_t count) {
    if (!tl_spriteBatcher.IsCurrentProgramEligible()) return;
    const uint32_t rc = tl_spriteBatcher.LastRejectCode();
    if (rc >= 32u) return;
    const uint32_t bit = 1u << rc;
    if ((g_sbDiagSeenMask.fetch_or(bit, std::memory_order_relaxed) & bit) != 0u) return;
    bool hasW = false;
    float fpZ = 0.0f;
    uint16_t enMask = 0;
    hs_sprite_batch::AttribState attr{};
    uint32_t elemBuf = 0, tex0 = 0, tex1 = 0, maxSlots = 0;
    tl_spriteBatcher.GetDiagState(hasW, fpZ, enMask, attr, elemBuf, tex0, tex1, maxSlots);
    size_t scCnt = 0, scBytes = 0, foundSz = 0;
    uint64_t scEv = 0, scUp = 0;
    uint32_t scMaxId = 0;
    tl_spriteBatcher.GetShadowDiag(attr.buffer, scCnt, scBytes, scEv, scUp, scMaxId, foundSz);
    LogMsg("[PERF][SBATCH][DIAG] %s reject=%u prog=%u mode=%u a=%d cnt=%d | hasW=%d fpZ=%.2f en=0x%x slots=%u | attr(v=%d sz=%d ty=0x%x n=%d st=%d buf=%u ptr=%p) ibo=%u tex=(%u,%u) | sc(cnt=%zu kb=%zu ev=%llu up=%llu maxId=%u fsz=%zu)",
           fn, rc, tl_spriteBatcher.CurrentProgramId(), mode, firstOrType, count,
           hasW ? 1 : 0, static_cast<double>(fpZ), static_cast<unsigned>(enMask), maxSlots,
           attr.valid ? 1 : 0, attr.size, attr.type, static_cast<int>(attr.normalized), attr.stride,
           attr.buffer, attr.pointer, elemBuf, tex0, tex1,
           scCnt, scBytes / 1024u, static_cast<unsigned long long>(scEv),
           static_cast<unsigned long long>(scUp), scMaxId, foundSz);
}

static std::atomic<uint32_t> g_uiDiagSeenMask{0};

static void LogUiBatchDiagOnce(const char* fn, uint32_t mode, int32_t count) {
    if (!tl_uiBatcher.IsCurrentProgramEligible()) return;
    const uint32_t rc = tl_uiBatcher.LastRejectCode();
    if (rc >= 32u) return;
    const uint32_t bit = 1u << rc;
    if ((g_uiDiagSeenMask.fetch_or(bit, std::memory_order_relaxed) & bit) != 0u) return;
    LogMsg("[PERF][UIBATCH][DIAG] %s reject rc=%u (prog=%u mode=0x%X cnt=%d)",
           fn, rc, tl_uiBatcher.CurrentProgramId(), mode, count);
}

template <typename Fn>
static inline void ExecuteExtProfiledDraw(int32_t extSlot, Fn&& drawCall) {
    ExtProfSlot& slot = g_extProfSlots[extSlot];
    tl_extProf.ResolveIfNeeded();
    if (tl_extProf.hasHwTimer && !tl_extProf.queryOpen) {
        tl_extProf.DrainSlotQueries(extSlot);
        TlExtQueryRing& ring = tl_extProf.rings[extSlot];
        const uint32_t idx = ring.head;
        if (!ring.pending[idx]) {
            if (ring.q[idx] == 0u) {
                tl_extProf.pGenQueries(1, &ring.q[idx]);
            }
            if (ring.q[idx] != 0u) {
                tl_extProf.queryOpen = true;
                tl_extProf.pBeginQuery(kGlTimeElapsed, ring.q[idx]);
                drawCall();
                tl_extProf.pEndQuery(kGlTimeElapsed);
                tl_extProf.queryOpen = false;
                ring.pending[idx] = true;
                ring.head = (idx + 1u) % kExtQueryRing;
                slot.drawsTotal.fetch_add(1u, std::memory_order_relaxed);
                slot.lastDrawQpc.store(NowQpc(), std::memory_order_relaxed);
                return;
            }
        }
    }
    const int64_t t0 = NowQpc();
    drawCall();
    const int64_t t1 = NowQpc();
    const float ms = static_cast<float>(static_cast<double>(t1 - t0) * g_qpcToUs * 0.001);
    if (!tl_extProf.hasHwTimer && ms >= 0.0f && ms < 250.0f) {
        UpdateAtomicEma(slot.emaMs, ms, 0.12f);
        float pk = slot.peakMs.load(std::memory_order_relaxed);
        while (ms > pk && !slot.peakMs.compare_exchange_weak(pk, ms, std::memory_order_relaxed)) {}
        slot.hwGpu.store(false, std::memory_order_relaxed);
    }
    slot.drawsTotal.fetch_add(1u, std::memory_order_relaxed);
    slot.lastDrawQpc.store(t1, std::memory_order_relaxed);
}

static std::atomic<uint32_t> g_hauntDiagDraws{0};

static void LogHauntedDrawDiag(const char* fn, uint32_t mode, int32_t count,
                               bool mirrorShadow, bool mirrorRefl) {
    if (!tl_curWorldProg || !tl_curWorldProg->isHaunted) return;
    const uint32_t n = g_hauntDiagDraws.fetch_add(1u, std::memory_order_relaxed) + 1u;
    if (n != 1u && n != 60u) return;
    int32_t blend = -1, sRgb = -1, dRgb = -1, sA = -1, dA = -1, eqRgb = -1;
    int32_t cm[4] = {-1, -1, -1, -1};
    int32_t stEn = -1, stFunc = -1, stRef = -1, stVMask = -1, stWMask = -1;
    int32_t dEn = -1, dMask = -1, dFunc = -1, scEn = -1, cullEn = -1;
    int32_t fbo = -1, prog = -1, vp[4] = {0, 0, 0, 0};
    if (g_fnGlGetIntegerv) {
        g_fnGlGetIntegerv(0x0BE2u /*GL_BLEND*/, &blend);
        g_fnGlGetIntegerv(0x80C9u /*GL_BLEND_SRC_RGB*/, &sRgb);
        g_fnGlGetIntegerv(0x80C8u /*GL_BLEND_DST_RGB*/, &dRgb);
        g_fnGlGetIntegerv(0x80CBu /*GL_BLEND_SRC_ALPHA*/, &sA);
        g_fnGlGetIntegerv(0x80CAu /*GL_BLEND_DST_ALPHA*/, &dA);
        g_fnGlGetIntegerv(0x8009u /*GL_BLEND_EQUATION_RGB*/, &eqRgb);
        g_fnGlGetIntegerv(0x0C23u /*GL_COLOR_WRITEMASK*/, cm);
        g_fnGlGetIntegerv(0x0B90u /*GL_STENCIL_TEST*/, &stEn);
        g_fnGlGetIntegerv(0x0B92u /*GL_STENCIL_FUNC*/, &stFunc);
        g_fnGlGetIntegerv(0x0B97u /*GL_STENCIL_REF*/, &stRef);
        g_fnGlGetIntegerv(0x0B93u /*GL_STENCIL_VALUE_MASK*/, &stVMask);
        g_fnGlGetIntegerv(0x0B98u /*GL_STENCIL_WRITEMASK*/, &stWMask);
        g_fnGlGetIntegerv(0x0B71u /*GL_DEPTH_TEST*/, &dEn);
        g_fnGlGetIntegerv(0x0B72u /*GL_DEPTH_WRITEMASK*/, &dMask);
        g_fnGlGetIntegerv(0x0B74u /*GL_DEPTH_FUNC*/, &dFunc);
        g_fnGlGetIntegerv(0x0C11u /*GL_SCISSOR_TEST*/, &scEn);
        g_fnGlGetIntegerv(0x0B44u /*GL_CULL_FACE*/, &cullEn);
        g_fnGlGetIntegerv(0x8CA6u /*GL_FRAMEBUFFER_BINDING*/, &fbo);
        g_fnGlGetIntegerv(0x8B8Du /*GL_CURRENT_PROGRAM*/, &prog);
        g_fnGlGetIntegerv(0x0BA2u /*GL_VIEWPORT*/, vp);
    }
    LogMsg("[DIAG][HAUNT] #%u %s mode=0x%X cnt=%d prog=%u(drv=%d) fbo=%u(drv=%d) vp=(%d,%d,%d,%d) | "
           "blend=%d fn=(0x%X,0x%X/0x%X,0x%X) eq=0x%X cm=(%d,%d,%d,%d) | "
           "stencil=(en=%d fn=0x%X ref=%d vm=0x%X wm=0x%X) depth=(en=%d m=%d fn=0x%X) sc=%d cull=%d | "
           "a=%.3f lOv=%.3f fCls=%.1f mir=(%d,%d) pos=(%.2f,%.2f,%.2f)",
           n, fn, mode, count, tl_curBoundProg, prog, tl_curBoundFbo, fbo, vp[0], vp[1], vp[2], vp[3],
           blend, sRgb, dRgb, sA, dA, eqRgb, cm[0], cm[1], cm[2], cm[3],
           stEn, stFunc, stRef, static_cast<uint32_t>(stVMask), static_cast<uint32_t>(stWMask),
           dEn, dMask, dFunc, scEn, cullEn,
           static_cast<double>(tl_curWorldProg->lastColorAlpha),
           static_cast<double>(tl_curWorldProg->lastParamsY),
           static_cast<double>(tl_curWorldProg->lastFoliageCls),
           mirrorShadow ? 1 : 0, mirrorRefl ? 1 : 0,
           static_cast<double>(tl_curWorldProg->lastMatrixW[12]),
           static_cast<double>(tl_curWorldProg->lastMatrixW[13]),
           static_cast<double>(tl_curWorldProg->lastMatrixW[14]));
}

static void WINAPI HookGlDrawArrays(uint32_t mode, int32_t first, int32_t count) {
    if (g_lookMatcher.IsCaptureArmed()) {
        LookCaptureOnPreDraw();
    }
    SyncSpriteBatcher();
    if (tl_curWorldProg) {
        if (tl_curWorldProg->role == WorldProgRole::Ground) {
            tl_worldShadow.OnGroundOrOceanDraw(tl_curBoundFbo, tl_curVp);
            tl_combatVfx.OnGroundOrOceanDraw(tl_curBoundFbo, tl_curVp);
        } else if (tl_curWorldProg->role == WorldProgRole::Ocean) {
            tl_worldShadow.OnGroundOrOceanDraw(tl_curBoundFbo, tl_curVp);
            tl_combatVfx.OnGroundOrOceanDraw(tl_curBoundFbo, tl_curVp);
            tl_waterRefl.OnOceanDraw(tl_curBoundFbo, tl_curVp);
            SyncOceanReflUniforms();
        }
    }
    if (tl_spriteBatcher.IsEnabled() && tl_spriteBatcher.IsCurrentProgramEligible()) {
        const auto& scfg = GetCachedShadowConfig();
        const auto& rcfg = GetCachedWaterReflConfig();
        const auto& wcfg = SyncFoliageAndWind();
        SyncCurProgWindUniforms();
        tl_spriteBatcher.SetShadowHints(scfg.skipNightmare, tl_worldShadow.HasCamRight(),
                                        tl_worldShadow.CamRightX(), tl_worldShadow.CamRightZ(), scfg.minHeight);
        const bool isStand = tl_spriteBatcher.IsCurrentMatrixWStanding();
        if (isStand) {
            tl_combatVfx.OnStandingEntityDraw(tl_curBoundFbo);
            if (tl_worldShadow.NeedsCompositeOrBeginWrite(tl_curBoundFbo, scfg)) {
                EnsureShadowCompositeAndBeginWrite(scfg);
            }
            if (tl_waterRefl.NeedsBeginWrite(tl_curBoundFbo, rcfg)) {
                EnsureWaterReflBeginWrite(rcfg);
            }
        }
        uint8_t fCls = 0u;
        float swayBase = 0.0f, wakeBase = 0.0f;
        if (isStand && wcfg.IsActive()) {
            if (const float* mw = tl_spriteBatcher.CurrentMatrixW()) {
                fCls = tl_foliageTable.QueryClass(mw[12], mw[14]);
                if (fCls != 0u) {
                    float crX = 0.70710678f, crZ = -0.70710678f;
                    ResolveWindCamRight(crX, crZ);
                    hs_foliage::ComputeEntitySwayBase(fCls, mw[12], mw[14], wcfg, crX, crZ, swayBase, wakeBase);
                }
            }
        }
        if (tl_spriteBatcher.TryBatchDrawArrays(mode, first, count, fCls, swayBase, wakeBase)) {
            return;
        }
        LogSpriteBatchDiagOnce("DA", mode, first, count);
    } else if (tl_uiBatcher.IsCurrentProgramEligible()) {
        SyncUiBatcher();
        if (tl_uiBatcher.IsEnabled()) {
            if (tl_uiBatcher.TryBatchDrawArrays(mode, first, count)) {
                return;
            }
            LogUiBatchDiagOnce("DA", mode, count);
        }
    }
    tl_spriteBatcher.PrepareForUnbatchedDraw();
    tl_uiBatcher.PrepareForUnbatchedDraw();
    bool mirrorShadow = false;
    bool mirrorRefl   = false;
    float mirrorEntX = 0.0f, mirrorEntZ = 0.0f;
    if (tl_curWorldProg && tl_curWorldProg->canCastShadow) {
        const auto& scfg = GetCachedShadowConfig();
        const auto& rcfg = GetCachedWaterReflConfig();
        const auto& wcfg = SyncFoliageAndWind();
        SyncCurProgWindUniforms();
        const float* mat = tl_curWorldProg->isSkinned
            ? (tl_curWorldProg->hasXform ? tl_curWorldProg->lastXform : nullptr)
            : (tl_curWorldProg->hasMatrixW ? tl_curWorldProg->lastMatrixW : nullptr);
        const bool isStand = (mat != nullptr) && hs_world_shadow::IsStandingMatrix(mat);
        if (tl_curWorldProg->locFoliageCls >= 0 && g_origGlUniform1f) {
            // Hard-exclude skinned & haunted programs (players, mobs, bosses) from foliage deformation!
            const float wantCls = (isStand && !tl_curWorldProg->isSkinned && !tl_curWorldProg->isHaunted && wcfg.IsActive())
                ? static_cast<float>(tl_foliageTable.QueryClass(mat[12], mat[14])) : 0.0f;
            if (tl_curWorldProg->lastFoliageCls != wantCls) {
                g_origGlUniform1f(tl_curWorldProg->locFoliageCls, wantCls);
                tl_glCache.InvalidateUniform(tl_curBoundProg, tl_curWorldProg->locFoliageCls);
                tl_curWorldProg->lastFoliageCls = wantCls;
            }
        }
        if (isStand) {
            tl_combatVfx.OnStandingEntityDraw(tl_curBoundFbo);
            if (hs_world_shadow::HasAnyShadowLight(scfg)) {
                if (tl_worldShadow.NeedsCompositeOrBeginWrite(tl_curBoundFbo, scfg)) {
                    EnsureShadowCompositeAndBeginWrite(scfg);
                }
                if (tl_worldShadow.BeganWriteThisFrame() &&
                    tl_worldShadow.IsInsideWorldPass(tl_curBoundFbo) &&
                    tl_curWorldProg->lastParamsY <= 0.35f &&
                    (!scfg.skipNightmare || tl_curWorldProg->lastColorAlpha >= 0.72f) &&
                    tl_worldShadow.IsEntityInsideShadowBox(mat[12], mat[14])) {
                    mirrorShadow = true;
                    mirrorEntX   = mat[12];
                    mirrorEntZ   = mat[14];
                }
            }
            if (rcfg.IsEnabled()) {
                if (tl_waterRefl.NeedsBeginWrite(tl_curBoundFbo, rcfg)) {
                    EnsureWaterReflBeginWrite(rcfg);
                }
                if (tl_waterRefl.BeganWriteThisFrame() &&
                    tl_waterRefl.IsInsideWorldPass(tl_curBoundFbo) &&
                    tl_curWorldProg->lastParamsY <= 0.35f &&
                    (!rcfg.skipNightmare || tl_curWorldProg->lastColorAlpha >= 0.72f) &&
                    tl_waterRefl.IsEntityInsideReflBox(mat[12], mat[14])) {
                    mirrorRefl = true;
                }
            }
        }
    }
    if (g_origGlDrawArrays) {
        if (tl_curWorldProg && tl_curWorldProg->isHaunted &&
            g_hauntDiagDraws.load(std::memory_order_relaxed) < 60u) {
            LogHauntedDrawDiag("DA", mode, count, mirrorShadow, mirrorRefl);
        }
        const int32_t extSlot = tl_extProf.curExtSlot;
        if (extSlot >= 0) {
            ExecuteExtProfiledDraw(extSlot, [&]() { g_origGlDrawArrays(mode, first, count); });
        } else {
            g_origGlDrawArrays(mode, first, count);
        }
        if (g_lookMatcher.IsCaptureArmed()) {
            LookCaptureOnPostDraw();
        }
        if (mirrorShadow && tl_curWorldProg) {
            const auto& scfg = GetCachedShadowConfig();
            const auto saved = BuildShadowPassState();
            tl_worldShadow.MirrorCurrentProgramDraw(
                tl_curWorldProg->locShadowPass,
                tl_curWorldProg->locShadowBounds,
                tl_curWorldProg->locShadowCamR,
                scfg, saved, GetSyncedGlCache(),
                [&]() { g_origGlDrawArrays(mode, first, count); },
                tl_curWorldProg->locShadowPt, true, mirrorEntX, mirrorEntZ);
        }
        if (mirrorRefl && tl_curWorldProg) {
            const auto& rcfg = GetCachedWaterReflConfig();
            const auto saved = BuildShadowPassState();
            tl_waterRefl.MirrorCurrentProgramDraw(
                tl_curWorldProg->locShadowPass,
                tl_curWorldProg->locShadowBounds,
                tl_curWorldProg->locShadowCamR,
                tl_curWorldProg->locShadowPt,
                rcfg, saved, GetSyncedGlCache(),
                [&]() { g_origGlDrawArrays(mode, first, count); });
        }
    }
}

static void WINAPI HookGlDrawElements(uint32_t mode, int32_t count, uint32_t type, const void* indices) {
    if (g_lookMatcher.IsCaptureArmed()) {
        LookCaptureOnPreDraw();
    }
    SyncSpriteBatcher();
    if (tl_curWorldProg) {
        if (tl_curWorldProg->role == WorldProgRole::Ground) {
            tl_worldShadow.OnGroundOrOceanDraw(tl_curBoundFbo, tl_curVp);
            tl_combatVfx.OnGroundOrOceanDraw(tl_curBoundFbo, tl_curVp);
        } else if (tl_curWorldProg->role == WorldProgRole::Ocean) {
            tl_worldShadow.OnGroundOrOceanDraw(tl_curBoundFbo, tl_curVp);
            tl_combatVfx.OnGroundOrOceanDraw(tl_curBoundFbo, tl_curVp);
            tl_waterRefl.OnOceanDraw(tl_curBoundFbo, tl_curVp);
            SyncOceanReflUniforms();
        }
    }
    if (tl_spriteBatcher.IsEnabled() && tl_spriteBatcher.IsCurrentProgramEligible()) {
        const auto& scfg = GetCachedShadowConfig();
        const auto& rcfg = GetCachedWaterReflConfig();
        const auto& wcfg = SyncFoliageAndWind();
        SyncCurProgWindUniforms();
        tl_spriteBatcher.SetShadowHints(scfg.skipNightmare, tl_worldShadow.HasCamRight(),
                                        tl_worldShadow.CamRightX(), tl_worldShadow.CamRightZ(), scfg.minHeight);
        const bool isStand = tl_spriteBatcher.IsCurrentMatrixWStanding();
        if (isStand) {
            tl_combatVfx.OnStandingEntityDraw(tl_curBoundFbo);
            if (tl_worldShadow.NeedsCompositeOrBeginWrite(tl_curBoundFbo, scfg)) {
                EnsureShadowCompositeAndBeginWrite(scfg);
            }
            if (tl_waterRefl.NeedsBeginWrite(tl_curBoundFbo, rcfg)) {
                EnsureWaterReflBeginWrite(rcfg);
            }
        }
        uint8_t fCls = 0u;
        float swayBase = 0.0f, wakeBase = 0.0f;
        if (isStand && wcfg.IsActive()) {
            if (const float* mw = tl_spriteBatcher.CurrentMatrixW()) {
                fCls = tl_foliageTable.QueryClass(mw[12], mw[14]);
                if (fCls != 0u) {
                    float crX = 0.70710678f, crZ = -0.70710678f;
                    ResolveWindCamRight(crX, crZ);
                    hs_foliage::ComputeEntitySwayBase(fCls, mw[12], mw[14], wcfg, crX, crZ, swayBase, wakeBase);
                }
            }
        }
        if (tl_spriteBatcher.TryBatchDrawElements(mode, count, type, indices, fCls, swayBase, wakeBase)) {
            return;
        }
        LogSpriteBatchDiagOnce("DE", mode, static_cast<int32_t>(type), count);
    } else if (tl_uiBatcher.IsCurrentProgramEligible()) {
        SyncUiBatcher();
        if (tl_uiBatcher.IsEnabled()) {
            if (tl_uiBatcher.TryBatchDrawElements(mode, count, type, indices)) {
                return;
            }
            LogUiBatchDiagOnce("DE", mode, count);
        }
    }
    tl_spriteBatcher.PrepareForUnbatchedDraw();
    tl_uiBatcher.PrepareForUnbatchedDraw();
    bool mirrorShadow = false;
    bool mirrorRefl   = false;
    float mirrorEntX = 0.0f, mirrorEntZ = 0.0f;
    if (tl_curWorldProg && tl_curWorldProg->canCastShadow) {
        const auto& scfg = GetCachedShadowConfig();
        const auto& rcfg = GetCachedWaterReflConfig();
        const auto& wcfg = SyncFoliageAndWind();
        SyncCurProgWindUniforms();
        const float* mat = tl_curWorldProg->isSkinned
            ? (tl_curWorldProg->hasXform ? tl_curWorldProg->lastXform : nullptr)
            : (tl_curWorldProg->hasMatrixW ? tl_curWorldProg->lastMatrixW : nullptr);
        const bool isStand = (mat != nullptr) && hs_world_shadow::IsStandingMatrix(mat);
        if (tl_curWorldProg->locFoliageCls >= 0 && g_origGlUniform1f) {
            // Hard-exclude skinned & haunted programs (players, mobs, bosses) from foliage deformation!
            const float wantCls = (isStand && !tl_curWorldProg->isSkinned && !tl_curWorldProg->isHaunted && wcfg.IsActive())
                ? static_cast<float>(tl_foliageTable.QueryClass(mat[12], mat[14])) : 0.0f;
            if (tl_curWorldProg->lastFoliageCls != wantCls) {
                g_origGlUniform1f(tl_curWorldProg->locFoliageCls, wantCls);
                tl_glCache.InvalidateUniform(tl_curBoundProg, tl_curWorldProg->locFoliageCls);
                tl_curWorldProg->lastFoliageCls = wantCls;
            }
        }
        if (isStand) {
            tl_combatVfx.OnStandingEntityDraw(tl_curBoundFbo);
            if (hs_world_shadow::HasAnyShadowLight(scfg)) {
                if (tl_worldShadow.NeedsCompositeOrBeginWrite(tl_curBoundFbo, scfg)) {
                    EnsureShadowCompositeAndBeginWrite(scfg);
                }
                if (tl_worldShadow.BeganWriteThisFrame() &&
                    tl_worldShadow.IsInsideWorldPass(tl_curBoundFbo) &&
                    tl_curWorldProg->lastParamsY <= 0.35f &&
                    (!scfg.skipNightmare || tl_curWorldProg->lastColorAlpha >= 0.72f) &&
                    tl_worldShadow.IsEntityInsideShadowBox(mat[12], mat[14])) {
                    mirrorShadow = true;
                    mirrorEntX   = mat[12];
                    mirrorEntZ   = mat[14];
                }
            }
            if (rcfg.IsEnabled()) {
                if (tl_waterRefl.NeedsBeginWrite(tl_curBoundFbo, rcfg)) {
                    EnsureWaterReflBeginWrite(rcfg);
                }
                if (tl_waterRefl.BeganWriteThisFrame() &&
                    tl_waterRefl.IsInsideWorldPass(tl_curBoundFbo) &&
                    tl_curWorldProg->lastParamsY <= 0.35f &&
                    (!rcfg.skipNightmare || tl_curWorldProg->lastColorAlpha >= 0.72f) &&
                    tl_waterRefl.IsEntityInsideReflBox(mat[12], mat[14])) {
                    mirrorRefl = true;
                }
            }
        }
    }
    if (g_origGlDrawElements) {
        if (tl_curWorldProg && tl_curWorldProg->isHaunted &&
            g_hauntDiagDraws.load(std::memory_order_relaxed) < 60u) {
            LogHauntedDrawDiag("DE", mode, count, mirrorShadow, mirrorRefl);
        }
        const int32_t extSlot = tl_extProf.curExtSlot;
        if (extSlot >= 0) {
            ExecuteExtProfiledDraw(extSlot, [&]() { g_origGlDrawElements(mode, count, type, indices); });
        } else {
            g_origGlDrawElements(mode, count, type, indices);
        }
        if (g_lookMatcher.IsCaptureArmed()) {
            LookCaptureOnPostDraw();
        }
        if (mirrorShadow && tl_curWorldProg) {
            const auto& scfg = GetCachedShadowConfig();
            const auto saved = BuildShadowPassState();
            tl_worldShadow.MirrorCurrentProgramDraw(
                tl_curWorldProg->locShadowPass,
                tl_curWorldProg->locShadowBounds,
                tl_curWorldProg->locShadowCamR,
                scfg, saved, GetSyncedGlCache(),
                [&]() { g_origGlDrawElements(mode, count, type, indices); },
                tl_curWorldProg->locShadowPt, true, mirrorEntX, mirrorEntZ);
        }
        if (mirrorRefl && tl_curWorldProg) {
            const auto& rcfg = GetCachedWaterReflConfig();
            const auto saved = BuildShadowPassState();
            tl_waterRefl.MirrorCurrentProgramDraw(
                tl_curWorldProg->locShadowPass,
                tl_curWorldProg->locShadowBounds,
                tl_curWorldProg->locShadowCamR,
                tl_curWorldProg->locShadowPt,
                rcfg, saved, GetSyncedGlCache(),
                [&]() { g_origGlDrawElements(mode, count, type, indices); });
        }
    }
}

static void WINAPI HookGlBindFramebuffer(uint32_t target, uint32_t framebuffer) {
    StampRtFrameStart();
    FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason::End);
    if ((target == 0x8D40u /*GL_FRAMEBUFFER*/ || target == 0x8CA9u /*GL_DRAW_FRAMEBUFFER*/) &&
        g_combatImpact.IsEnabled()) {
        SyncWorldShadowEngine();
        if (tl_combatVfx.ShouldRenderOnLeaveFbo(tl_curBoundFbo, framebuffer)) {
            const auto saved = BuildShadowPassState();
            tl_combatVfx.RenderIntoPrimaryRt(g_combatImpact, NowQpc(), g_qpcInvFreq, saved, GetSyncedGlCache());
        }
    }
    BindFramebufferResolved(target, framebuffer);
    if (target == 0x8D40u /*GL_FRAMEBUFFER*/ || target == 0x8CA9u /*GL_DRAW_FRAMEBUFFER*/) {
        tl_curBoundFbo = framebuffer;
    }
}

static void WINAPI HookGlDeleteFramebuffers(int32_t n, const uint32_t* framebuffers) {
    if (tl_spriteBatcher.HasPending()) {
        FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason::End);
    }
    if (n > 0 && framebuffers != nullptr && tl_curBoundFbo != 0u) {
        for (int32_t i = 0; i < n; ++i) {
            if (framebuffers[i] != 0u && framebuffers[i] == tl_curBoundFbo) {
                tl_curBoundFbo = 0u;
                break;
            }
        }
    }
    if (g_origGlDeleteFramebuffers) {
        g_origGlDeleteFramebuffers(n, framebuffers);
    } else if (g_fnGlDeleteFramebuffers) {
        g_fnGlDeleteFramebuffers(n, framebuffers);
    }
}

static void WINAPI HookGlGetIntegerv(uint32_t pname, int32_t* params) {
    if (!g_fnGlGetIntegerv) return;
    g_fnGlGetIntegerv(pname, params);
    if (params && (pname == 0x8CA6u /*GL_FRAMEBUFFER_BINDING*/ || pname == 0x8CAAu /*GL_READ_FRAMEBUFFER_BINDING*/)) {
        const uint32_t offFbo = GetDxgiFlip().OffscreenFbo();
        if (offFbo != 0u && static_cast<uint32_t>(*params) == offFbo) {
            *params = 0;
        }
    }
}

static void WINAPI HookGlClear(uint32_t mask) {
    StampRtFrameStart();
    FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason::End);
    if (tl_curBoundFbo == 0u && (mask & 0x4000u /*GL_COLOR_BUFFER_BIT*/) != 0u && GetDxgiFlip().IsActive()) {
        GetDxgiFlip().EnsureSizeForCurrentWindow();
    }
    if (g_origGlClear) {
        g_origGlClear(mask);
    }
}

static void WINAPI HookGlReadPixels(int32_t x, int32_t y, int32_t width, int32_t height, uint32_t format, uint32_t type, void* pixels) {
    FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason::End);
    if (g_origGlReadPixels) {
        g_origGlReadPixels(x, y, width, height, format, type, pixels);
    }
}

static void WINAPI HookGlFinish() {
    FlushSpriteBatchIfPending(hs_sprite_batch::FlushReason::End);
    if (g_origGlFinish) {
        g_origGlFinish();
    }
}

// Split-peek GlStateCache dedup for stencil & polygon-offset calls:
//   - DST only imports glStencilFunc/Mask/Op (never *Separate), and neither WorldShadow nor WaterRefl
//     mutates stencil func/mask/op. ResetBindingsOnly() clears all stencil state on every SwapBuffers
//     and eglMakeCurrent (immune to Steam/RTSS overlays). Eliminates ~932k redundant sn flushes!
static void WINAPI HookGlStencilFunc(uint32_t func, int32_t ref, uint32_t mask) {
    if (!g_origGlStencilFunc) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsStencilFuncRedundant(func, ref, mask);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Stencil);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordStencilFunc(func, ref, mask);
    }
    g_origGlStencilFunc(func, ref, mask);
}

static void WINAPI HookGlStencilMask(uint32_t mask) {
    if (!g_origGlStencilMask) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsStencilMaskRedundant(mask);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Stencil);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordStencilMask(mask);
    }
    g_origGlStencilMask(mask);
}

static void WINAPI HookGlStencilOp(uint32_t fail, uint32_t zfail, uint32_t zpass) {
    if (!g_origGlStencilOp) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsStencilOpRedundant(fail, zfail, zpass);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::Stencil);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordStencilOp(fail, zfail, zpass);
    }
    g_origGlStencilOp(fail, zfail, zpass);
}

static void WINAPI HookGlPolygonOffset(float factor, float units) {
    if (!g_origGlPolygonOffset) return;
    if (IsGlCacheOperational()) {
        auto& st = GetSyncedGlCache();
        const bool redundant = st.IsPolygonOffsetRedundant(factor, units);
        FlushGlCacheStatsIfReady(st);
        if (redundant) return;
    }
    FlushSpriteBatchStateCause(StateFlushCause::PolyOff);
    if (IsGlCacheOperational()) {
        GetSyncedGlCache().RecordPolygonOffset(factor, units);
    }
    g_origGlPolygonOffset(factor, units);
}

static void WINAPI HookGlTexImage2D(uint32_t target, int32_t level, int32_t internalformat, int32_t width,
                                    int32_t height, int32_t border, uint32_t format, uint32_t type, const void* pixels) {
    FlushSpriteBatchStateCause(StateFlushCause::TexImg);
    if (g_origGlTexImage2D) {
        // Desktop GL Compatibility spec: GL_BGRA_EXT (0x80E1) is valid as `format`,
        // while `internalformat` must be GL_RGBA (0x1908) on strict desktop GL drivers (AMD/Intel).
        if (internalformat == 0x80E1 && g_nativeGlActive.load(std::memory_order_acquire)) {
            internalformat = 0x1908;
        }
        g_origGlTexImage2D(target, level, internalformat, width, height, border, format, type, pixels);
    }
}

static void WINAPI HookGlCompressedTexImage2D(uint32_t target, int32_t level, uint32_t internalformat, int32_t width,
                                              int32_t height, int32_t border, int32_t imageSize, const void* data) {
    FlushSpriteBatchStateCause(StateFlushCause::TexImg);
    if (g_origGlCompressedTexImage2D) {
        g_origGlCompressedTexImage2D(target, level, internalformat, width, height, border, imageSize, data);
    }
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
    if (tl_spriteBatcher.HasPending()) SyncSpriteBatcher();
    tl_spriteBatcher.OnDeleteOrLinkProgram(program);
    if (tl_uiBatcher.HasPending()) SyncUiBatcher();
    tl_uiBatcher.OnDeleteOrLinkProgram(program);
    tl_glCache.OnDeleteOrLinkProgram(program);
    if (program != 0u && g_fnGlBindAttribLocation &&
        g_sbShadersInjected.load(std::memory_order_relaxed) > 0u) {
        // Pin POS2D_UV to slot 0 (matching DST VertexDecl) and batch attribs to slots 5/6/7 (<8 GLES2 safe)
        // Per GLES2 spec §2.10.4, bindings for inactive attributes are cleanly ignored by the linker.
        g_fnGlBindAttribLocation(program, 0u, "POS2D_UV");
        g_fnGlBindAttribLocation(program, 5u, "HS_BATCH_CMULT");
        g_fnGlBindAttribLocation(program, 6u, "HS_BATCH_CADD");
        g_fnGlBindAttribLocation(program, 7u, "HS_BATCH_WPOS");
    }
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
        if (!linkedOk && g_fnGlGetProgramInfoLog) {
            static std::atomic<uint32_t> s_linkErrLogs{0};
            if (s_linkErrLogs.fetch_add(1, std::memory_order_relaxed) < 16u) {
                char infoLog[512]{};
                int32_t logLen = 0;
                g_fnGlGetProgramInfoLog(program, static_cast<int32_t>(sizeof(infoLog) - 1), &logLen, infoLog);
                LogMsg("[WARN][GL] Program %u link FAILED (nativeGl=%d): %.400s",
                       program, g_nativeGlActive.load(std::memory_order_relaxed) ? 1 : 0, infoLog);
            }
        }
    }

    int32_t locP = -1, locS = -1, locT = -1;
    int32_t locSh = -1, locSh2 = -1, locOc = -1, locOcPbr = -1, locSp = -1, locSd = -1, locDet = -1;
    int32_t locSplatPt[hs_world_shadow::kMaxPointShadowLights] = { -1, -1, -1 };
    if (linkedOk && g_fnGlGetUniformLocation) {
        locP     = g_fnGlGetUniformLocation(program, "HS_HD2D_PARAMS");
        locS     = g_fnGlGetUniformLocation(program, "HS_HD2D_SUN");
        locT     = g_fnGlGetUniformLocation(program, "HS_HD2D_TINT");
        locSh    = g_fnGlGetUniformLocation(program, "HS_HD2D_SHADE");
        locSh2   = g_fnGlGetUniformLocation(program, "HS_HD2D_SHADE2");
        locOc    = g_fnGlGetUniformLocation(program, "HS_HD2D_OCEAN");
        locOcPbr = g_fnGlGetUniformLocation(program, "HS_OCEAN_PBR");
        locSp    = g_fnGlGetUniformLocation(program, "HS_HD2D_SHAD_PROJ");
        locSd    = g_fnGlGetUniformLocation(program, "HS_HD2D_SHAD_DYN");
        locDet   = g_fnGlGetUniformLocation(program, "HS_DETAIL_PARAMS");
        locSplatPt[0] = g_fnGlGetUniformLocation(program, "HS_SPLAT_PT0");
        locSplatPt[1] = g_fnGlGetUniformLocation(program, "HS_SPLAT_PT1");
        locSplatPt[2] = g_fnGlGetUniformLocation(program, "HS_SPLAT_PT2");
    }

    void* const curCtx = tl_glCache.curContext;
    if (linkedOk && g_fnGlGetUniformLocation && g_fnGlGetAttribLocation) {
        hs_sprite_batch::BatchProgInfo binfo{};
        binfo.ctx            = curCtx;
        binfo.checked        = true;
        binfo.locBatchActive = g_fnGlGetUniformLocation(program, "HS_BATCH_ACTIVE");
        if (binfo.locBatchActive >= 0) {
            binfo.locMatrixW     = g_fnGlGetUniformLocation(program, "MatrixW");
            binfo.locColourXform = g_fnGlGetUniformLocation(program, "COLOUR_XFORM");
            binfo.locFloatParams = g_fnGlGetUniformLocation(program, "FLOAT_PARAMS");
            binfo.locParams      = g_fnGlGetUniformLocation(program, "PARAMS");
            binfo.locPos2dUv     = g_fnGlGetAttribLocation(program, "POS2D_UV");
            binfo.locBatchWpos   = g_fnGlGetAttribLocation(program, "HS_BATCH_WPOS");
            binfo.locBatchCMult  = g_fnGlGetAttribLocation(program, "HS_BATCH_CMULT");
            binfo.locBatchCAdd   = g_fnGlGetAttribLocation(program, "HS_BATCH_CADD");
            binfo.locSampler0    = g_fnGlGetUniformLocation(program, "SAMPLER[0]");
            binfo.locSampler1    = g_fnGlGetUniformLocation(program, "SAMPLER[1]");
            binfo.locSampler5    = g_fnGlGetUniformLocation(program, "SAMPLER[5]");
            int validTexLocs = 0;
            for (uint32_t s = 0; s < hs_sprite_batch::kMaxBatchTexSlots; ++s) {
                char uName[24];
                snprintf(uName, sizeof(uName), "HS_BATCH_TEX%u", s);
                binfo.locBatchTex[s] = g_fnGlGetUniformLocation(program, uName);
                if (binfo.locBatchTex[s] >= 0) ++validTexLocs;
            }
            binfo.eligible = (binfo.locMatrixW >= 0 &&
                              binfo.locPos2dUv == 0 &&
                              binfo.locBatchWpos >= 0 &&
                              binfo.locBatchWpos != binfo.locPos2dUv &&
                              validTexLocs >= 2 &&
                              binfo.locSampler0 >= 0 &&
                              binfo.locSampler1 >= 0);
            if (binfo.eligible) {
                tl_sbProgs[program] = binfo;
                const uint32_t total = g_sbProgsLinked.fetch_add(1, std::memory_order_relaxed) + 1;
                LogMsg("[PERF][SBATCH] Linked batchable anim program #%u (prog=%u, pos=%d wpos=%d cm=%d ca=%d matW=%d cx=%d act=%d texLocs=%d triple=%d)",
                       total, program, binfo.locPos2dUv, binfo.locBatchWpos,
                       binfo.locBatchCMult, binfo.locBatchCAdd,
                       binfo.locMatrixW, binfo.locColourXform, binfo.locBatchActive, validTexLocs,
                       binfo.locSampler5 >= 0 ? 1 : 0);
            } else {
                tl_sbProgs.erase(program);
            }
        } else {
            tl_sbProgs.erase(program);
        }

        // Unified UI + Font Uber-Batcher program registration (pure observation, 0 source edits)
        auto attIt = tl_uiProgAttached.find(program);
        if (attIt != tl_uiProgAttached.end()) {
            const auto kind = hs_ui_batch::MatchUiProgKind(attIt->second.first, attIt->second.second);
            if (kind != hs_ui_batch::UiProgKind::None) {
                hs_ui_batch::UiProgInfo uinfo{};
                uinfo.ctx            = curCtx;
                uinfo.kind           = kind;
                uinfo.locPos         = g_fnGlGetAttribLocation(program, "POSITION");
                uinfo.locUv          = g_fnGlGetAttribLocation(program, "TEXCOORD0");
                uinfo.locDiffuse     = g_fnGlGetAttribLocation(program, "DIFFUSE");
                uinfo.locTc1         = g_fnGlGetAttribLocation(program, "TEXCOORD1");
                uinfo.locMatrixPVW   = g_fnGlGetUniformLocation(program, "MatrixPVW");
                uinfo.locMatrixP     = g_fnGlGetUniformLocation(program, "MatrixP");
                uinfo.locMatrixV     = g_fnGlGetUniformLocation(program, "MatrixV");
                uinfo.locMatrixW     = g_fnGlGetUniformLocation(program, "MatrixW");
                uinfo.locColourXform = g_fnGlGetUniformLocation(program, "COLOUR_XFORM");
                uinfo.locSampler0    = g_fnGlGetUniformLocation(program, "SAMPLER[0]");
                if (uinfo.locSampler0 < 0) {
                    uinfo.locSampler0 = g_fnGlGetUniformLocation(program, "SAMPLER");
                }
                const bool needTc1 = (kind == hs_ui_batch::UiProgKind::FontPacked ||
                                      kind == hs_ui_batch::UiProgKind::FontPackedOutline);
                const bool matsOk = (kind == hs_ui_batch::UiProgKind::UiBasic ||
                                     kind == hs_ui_batch::UiProgKind::UiCTransform)
                                        ? (uinfo.locMatrixPVW >= 0)
                                        : (uinfo.locMatrixP >= 0 && uinfo.locMatrixV >= 0 && uinfo.locMatrixW >= 0);
                const bool cxOk = (kind != hs_ui_batch::UiProgKind::UiCTransform) || (uinfo.locColourXform >= 0);
                uinfo.eligible = (uinfo.locPos >= 0 && uinfo.locUv >= 0 && uinfo.locDiffuse >= 0 &&
                                  (!needTc1 || uinfo.locTc1 >= 0) &&
                                  matsOk && cxOk && uinfo.locSampler0 >= 0);
                if (uinfo.eligible) {
                    tl_uiProgs[program] = uinfo;
                    const uint32_t total = g_uiProgsLinked.fetch_add(1, std::memory_order_relaxed) + 1;
                    LogMsg("[PERF][UIBATCH] Linked batchable UI/Font program #%u (prog=%u, kind=%s, pos=%d uv=%d col=%d tc1=%d pvw=%d p/v/w=%d/%d/%d cx=%d s0=%d)",
                           total, program, hs_ui_batch::UiProgKindName(kind),
                           uinfo.locPos, uinfo.locUv, uinfo.locDiffuse, uinfo.locTc1,
                           uinfo.locMatrixPVW, uinfo.locMatrixP, uinfo.locMatrixV, uinfo.locMatrixW,
                           uinfo.locColourXform, uinfo.locSampler0);
                } else {
                    tl_uiProgs.erase(program);
                }
            } else {
                tl_uiProgs.erase(program);
            }
        } else {
            tl_uiProgs.erase(program);
        }
    } else {
        tl_sbProgs.erase(program);
        tl_uiProgs.erase(program);
    }

    WorldProgRole wRole = WorldProgRole::None;
    if (linkedOk && program != 0u && g_fnGlGetUniformLocation) {
        auto writ = tl_progWorldRole.find(program);
        if (writ != tl_progWorldRole.end()) {
            wRole = writ->second;
        }
        if (wRole == WorldProgRole::None) {
            if (locSh >= 0) wRole = WorldProgRole::AnimWorld;
            else if (locOc >= 0) wRole = WorldProgRole::Ocean;
        }
        WorldProgInfo winfo{};
        winfo.role             = wRole;
        winfo.locMatrixP       = g_fnGlGetUniformLocation(program, "MatrixP");
        winfo.locMatrixV       = g_fnGlGetUniformLocation(program, "MatrixV");
        winfo.locMatrixW       = g_fnGlGetUniformLocation(program, "MatrixW");
        winfo.locPv            = g_fnGlGetUniformLocation(program, "pv");
        winfo.locFastAnimXform = g_fnGlGetUniformLocation(program, "fastanim_xform");
        winfo.locColourXform   = g_fnGlGetUniformLocation(program, "COLOUR_XFORM");
        winfo.locParams        = g_fnGlGetUniformLocation(program, "PARAMS");
        winfo.locShadowPass    = g_fnGlGetUniformLocation(program, "HS_SHADOW_PASS");
        winfo.locShadowBounds  = g_fnGlGetUniformLocation(program, "HS_SHADOW_BOUNDS");
        winfo.locShadowCamR    = g_fnGlGetUniformLocation(program, "HS_SHADOW_CAMR");
        winfo.locShadowPt      = g_fnGlGetUniformLocation(program, "HS_SHADOW_PT");
        winfo.locShadowMask    = g_fnGlGetUniformLocation(program, "HS_SHADOW_MASK");
        winfo.locRecvBounds    = g_fnGlGetUniformLocation(program, "HS_RECV_BOUNDS");
        winfo.locRecvTint      = g_fnGlGetUniformLocation(program, "HS_RECV_TINT");
        winfo.locReflTex       = g_fnGlGetUniformLocation(program, "HS_REFL_TEX");
        winfo.locReflBounds    = g_fnGlGetUniformLocation(program, "HS_REFL_BOUNDS");
        winfo.locReflCfg       = g_fnGlGetUniformLocation(program, "HS_REFL_CFG");
        winfo.locOceanEye      = g_fnGlGetUniformLocation(program, "HS_OCEAN_EYE");
        winfo.locWindParams    = g_fnGlGetUniformLocation(program, "HS_WIND_PARAMS");
        winfo.locWindPlayer    = g_fnGlGetUniformLocation(program, "HS_WIND_PLAYER");
        winfo.locFoliageCls    = g_fnGlGetUniformLocation(program, "HS_FOLIAGE_CLS");
        winfo.isSkinned        = (winfo.locFastAnimXform >= 0);
        winfo.isHaunted        = (tl_hauntProgs.find(program) != tl_hauntProgs.end());
        winfo.canCastShadow    = (wRole == WorldProgRole::AnimWorld && winfo.locShadowPass >= 0 &&
                                  (winfo.isSkinned ? (winfo.locFastAnimXform >= 0) : (winfo.locMatrixW >= 0)));
        const bool isUiProg = (tl_uiProgs.find(program) != tl_uiProgs.end());
        if (!isUiProg && (wRole != WorldProgRole::None || winfo.locMatrixP >= 0 || winfo.locMatrixV >= 0 || winfo.locPv >= 0)) {
            tl_worldProgs[program] = winfo;
            if (tl_curBoundProg == program) {
                tl_curWorldProg = &tl_worldProgs[program];
            }
            if (winfo.canCastShadow) {
                LogMsg("[VISUAL][SHADOW_V2] Linked shadow-capable world program (prog=%u, skinned=%d, spLoc=%d, sbLoc=%d, crLoc=%d)",
                       program, winfo.isSkinned ? 1 : 0, winfo.locShadowPass, winfo.locShadowBounds, winfo.locShadowCamR);
            }
        } else {
            if (tl_curBoundProg == program) tl_curWorldProg = nullptr;
            tl_worldProgs.erase(program);
        }
    } else if (program != 0u) {
        if (tl_curBoundProg == program) tl_curWorldProg = nullptr;
        tl_worldProgs.erase(program);
    }

    bool newlyLinkedHd2d = false;
    AcquireSRWLockExclusive(&g_hd2dProgLock);
    Hd2dProgSlot& slot = UpsertProgSlotLocked(curCtx, program);
    const bool hadAny = slot.hasAny;
    slot.ctx       = curCtx;
    slot.checked   = true;
    slot.linkedOk  = linkedOk;
    slot.role      = wRole;
    slot.locParams   = locP;
    slot.locSun      = locS;
    slot.locTint     = locT;
    slot.locShade    = locSh;
    slot.locShade2   = locSh2;
    slot.locOcean    = locOc;
    slot.locOceanPbr = locOcPbr;
    slot.locShadProj = locSp;
    slot.locShadDyn  = locSd;
    slot.locDetail   = locDet;
    for (uint32_t si = 0u; si < hs_world_shadow::kMaxPointShadowLights; ++si) {
        slot.locSplatPt[si] = locSplatPt[si];
    }
    slot.hasAny      = linkedOk && (locP >= 0 || locS >= 0 || locT >= 0 || locSh >= 0 || locSh2 >= 0 || locOc >= 0 || locOcPbr >= 0 || locSp >= 0 || locSd >= 0 || locDet >= 0 || locSplatPt[0] >= 0);
    slot.lastGen     = 0;
    if (slot.hasAny && !hadAny) {
        newlyLinkedHd2d = true;
    } else if (!slot.hasAny && hadAny) {
        uint32_t cur = g_hd2dProgsLinked.load(std::memory_order_relaxed);
        while (cur > 0 && !g_hd2dProgsLinked.compare_exchange_weak(cur, cur - 1, std::memory_order_relaxed)) {}
    }
    ReleaseSRWLockExclusive(&g_hd2dProgLock);

    if (newlyLinkedHd2d) {
        const uint32_t total = g_hd2dProgsLinked.fetch_add(1, std::memory_order_relaxed) + 1;
        LogMsg("[VISUAL][HD2D] Linked HD-2D program #%u (ctx=%p prog=%u, locs=%d/%d/%d sh=%d/%d oc=%d/%d sp=%d sd=%d det=%d)",
               total, curCtx, program, locP, locS, locT, locSh, locSh2, locOc, locOcPbr, locSp, locSd, locDet);
    }

    // Bind HS_EXT_V1 per-plugin hardware GPU profiler slot (tag first, probe-uniform fallback)
    if (linkedOk && program != 0u) {
        int32_t extSlot = -1;
        auto peIt = tl_extProf.progExtId.find(program);
        if (peIt != tl_extProf.progExtId.end()) {
            extSlot = UpsertExtProfSlot(peIt->second.c_str(), nullptr);
        } else if (g_fnGlGetUniformLocation) {
            const uint32_t cnt = g_extProfCount.load(std::memory_order_acquire);
            if (cnt > 0u) {
                AcquireSRWLockShared(&g_extProfLock);
                for (uint32_t i = 0; i < cnt; ++i) {
                    if (g_extProfSlots[i].probeUniform[0] &&
                        g_fnGlGetUniformLocation(program, g_extProfSlots[i].probeUniform) >= 0) {
                        extSlot = static_cast<int32_t>(i);
                        break;
                    }
                }
                ReleaseSRWLockShared(&g_extProfLock);
            }
        }
        auto prevIt = tl_extProf.progSlotMap.find(program);
        if (prevIt != tl_extProf.progSlotMap.end() && prevIt->second != extSlot) {
            const int32_t oldS = prevIt->second;
            if (oldS >= 0 && static_cast<uint32_t>(oldS) < kMaxExtProfSlots) {
                uint32_t c = g_extProfSlots[oldS].linkedProgs.load(std::memory_order_relaxed);
                while (c > 0u && !g_extProfSlots[oldS].linkedProgs.compare_exchange_weak(c, c - 1u, std::memory_order_relaxed)) {}
            }
            tl_extProf.progSlotMap.erase(prevIt);
        }
        if (extSlot >= 0) {
            if (tl_extProf.progSlotMap.find(program) == tl_extProf.progSlotMap.end()) {
                tl_extProf.progSlotMap[program] = extSlot;
                g_extProfSlots[extSlot].linkedProgs.fetch_add(1u, std::memory_order_relaxed);
                LogMsg("[EXT][GPU] Bound profiled pass -> slot=%d id=%s prog=%u",
                       extSlot, g_extProfSlots[extSlot].id, program);
            }
        }
    } else if (program != 0u) {
        auto prevIt = tl_extProf.progSlotMap.find(program);
        if (prevIt != tl_extProf.progSlotMap.end()) {
            const int32_t oldS = prevIt->second;
            if (oldS >= 0 && static_cast<uint32_t>(oldS) < kMaxExtProfSlots) {
                uint32_t c = g_extProfSlots[oldS].linkedProgs.load(std::memory_order_relaxed);
                while (c > 0u && !g_extProfSlots[oldS].linkedProgs.compare_exchange_weak(c, c - 1u, std::memory_order_relaxed)) {}
            }
            tl_extProf.progSlotMap.erase(prevIt);
        }
    }
}

static void WINAPI HookGlUseProgram(uint32_t program) {
    if (program > 0u && !tl_extProf.progSlotMap.empty()) {
        auto eit = tl_extProf.progSlotMap.find(program);
        tl_extProf.curExtSlot = (eit != tl_extProf.progSlotMap.end()) ? eit->second : -1;
    } else {
        tl_extProf.curExtSlot = -1;
    }
    void* const curCtx = tl_glCache.curContext;
    bool progLinkedOk = false;
    bool hasHd2d = false;
    WorldProgRole slotRole = WorldProgRole::None;
    int32_t locP = -1, locS = -1, locT = -1;
    int32_t locSh = -1, locSh2 = -1, locOc = -1, locOcPbr = -1, locSp = -1, locSd = -1, locDet = -1;
    int32_t locSplatPt[hs_world_shadow::kMaxPointShadowLights] = { -1, -1, -1 };
    uint32_t slotLastGen = 0;

    if (program > 0) {
        AcquireSRWLockShared(&g_hd2dProgLock);
        if (Hd2dProgSlot* slot = FindProgSlotLocked(curCtx, program)) {
            progLinkedOk = slot->linkedOk;
            hasHd2d      = slot->hasAny && slot->linkedOk;
            slotRole     = slot->role;
            locP         = slot->locParams;
            locS         = slot->locSun;
            locT         = slot->locTint;
            locSh        = slot->locShade;
            locSh2       = slot->locShade2;
            locOc        = slot->locOcean;
            locOcPbr     = slot->locOceanPbr;
            locSp        = slot->locShadProj;
            locSd        = slot->locShadDyn;
            locDet       = slot->locDetail;
            for (uint32_t si = 0u; si < hs_world_shadow::kMaxPointShadowLights; ++si) {
                locSplatPt[si] = slot->locSplatPt[si];
            }
            slotLastGen  = slot->lastGen;
        }
        ReleaseSRWLockShared(&g_hd2dProgLock);
    }

    hs_sprite_batch::BatchProgInfo* sbInfo = nullptr;
    hs_ui_batch::UiProgInfo*        uiInfo = nullptr;
    if (program > 0) {
        auto it = tl_sbProgs.find(program);
        if (it != tl_sbProgs.end() && it->second.eligible) {
            sbInfo = &it->second;
        } else {
            auto uit = tl_uiProgs.find(program);
            if (uit != tl_uiProgs.end() && uit->second.eligible && uit->second.ctx == curCtx) {
                uiInfo = &uit->second;
            }
        }
    }

    // CRITICAL ORDERING: Flush any pending Sprite/UI batch BEFORE updating glCache.curProgram!
    // Otherwise OnUiBatchPostFlush (which restores old dstProg) would clobber glCache.curProgram
    // after ShouldCallUseProgramChecked already set it to the new program, desyncing uniform caches
    // and skipping subsequent glUseProgram calls.
    SyncSpriteBatcher();
    tl_spriteBatcher.OnUseProgram(program, sbInfo);
    SyncUiBatcher();
    const bool uiDeferred = tl_uiBatcher.OnUseProgram(program, uiInfo);

    tl_curBoundProg = program;
    if (program > 0u) {
        auto wit = tl_worldProgs.find(program);
        tl_curWorldProg = (wit != tl_worldProgs.end()) ? &wit->second : nullptr;
        auto lrit = tl_lookProgRole.find(program);
        tl_curLookProgRole = (lrit != tl_lookProgRole.end()) ? lrit->second : 0u;
    } else {
        tl_curWorldProg = nullptr;
        tl_curLookProgRole = 0u;
    }

    if (uiDeferred) {
        // UI program switch was absorbed without calling glUseProgram on the GPU:
        // mark glCache.curProgram unknown so subsequent real program switches NEVER get skipped!
        tl_glCache.curProgram = hs_gl_cache::kInvalidId;
    } else {
        bool shouldCall = true;
        if (IsGlCacheOperational()) {
            auto& st = GetSyncedGlCache();
            shouldCall = st.ShouldCallUseProgramChecked(program, progLinkedOk);
            FlushGlCacheStatsIfReady(st);
        }
        if (shouldCall && g_origGlUseProgram) {
            g_origGlUseProgram(program);
        }
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
            if (locSh >= 0) {
                g_fnGlUniform4fv(locSh, 1, st.shade);
                tl_glCache.InvalidateUniform(program, locSh);
            }
            if (locSh2 >= 0) {
                g_fnGlUniform4fv(locSh2, 1, st.shade2);
                tl_glCache.InvalidateUniform(program, locSh2);
            }
            if (locOc >= 0) {
                g_fnGlUniform4fv(locOc, 1, st.ocean);
                tl_glCache.InvalidateUniform(program, locOc);
            }
            if (locOcPbr >= 0) {
                g_fnGlUniform4fv(locOcPbr, 1, st.oceanPbr);
                tl_glCache.InvalidateUniform(program, locOcPbr);
            }
            if (locSp >= 0) {
                float spVal[4] = { st.shadProj[0], st.shadProj[1], st.shadProj[2], st.shadProj[3] };
                if (slotRole == WorldProgRole::AnimBloom &&
                    g_worldShadowReady.load(std::memory_order_acquire) &&
                    !g_worldShadowFailed.load(std::memory_order_acquire)) {
                    spVal[3] = 0.0f; // World-Space ShadowMask FBO owns shadows; keep BloomSampler 100% pure!
                } else if (slotRole == WorldProgRole::Splat) {
                    const float keep = std::clamp(st.shadowOvalKeep, 0.0f, 1.0f);
                    const float dirProj = (st.shadProj[3] > 0.5f) ? std::clamp(st.shadowDirStrength, 0.0f, 1.0f) : 0.0f;
                    const float den = std::clamp(st.shadowDensity, 0.0f, 1.0f);
                    const float maxSuppress = 1.0f - keep;
                    const bool anyProj = (dirProj > 0.005f) || (st.pointLightCount > 0u);
                    spVal[0] = dirProj;
                    spVal[1] = den;
                    spVal[2] = maxSuppress;
                    spVal[3] = (anyProj && den > 0.001f && maxSuppress > 0.001f) ? 1.0f : 0.0f;
                    for (uint32_t si = 0u; si < hs_world_shadow::kMaxPointShadowLights; ++si) {
                        if (locSplatPt[si] >= 0) {
                            const float zeroPt[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                            const float* pDat = (si < st.pointLightCount) ? st.pointLights[si] : zeroPt;
                            g_fnGlUniform4fv(locSplatPt[si], 1, pDat);
                            tl_glCache.InvalidateUniform(program, locSplatPt[si]);
                        }
                    }
                }
                g_fnGlUniform4fv(locSp, 1, spVal);
                tl_glCache.InvalidateUniform(program, locSp);
            }
            if (locSd >= 0) {
                g_fnGlUniform4fv(locSd, 1, st.shadDyn);
                tl_glCache.InvalidateUniform(program, locSd);
            }
            if (locDet >= 0) {
                g_fnGlUniform4fv(locDet, 1, st.detail);
                tl_glCache.InvalidateUniform(program, locDet);
            }
            AcquireSRWLockExclusive(&g_hd2dProgLock);
            if (Hd2dProgSlot* slot = FindProgSlotLocked(curCtx, program)) {
                slot->lastGen = curGen;
            }
            ReleaseSRWLockExclusive(&g_hd2dProgLock);
        }
    }
    if (!uiDeferred && tl_curWorldProg) {
        if (tl_curWorldProg->locRecvTint >= 0) {
            SyncCurProgEntityReceiveUniforms(GetCachedShadowConfig());
        }
        if (tl_curWorldProg->locWindParams >= 0) {
            SyncCurProgWindUniforms();
        }
    }
}

static void WINAPI HookGlDeleteProgram(uint32_t program) {
    if (tl_spriteBatcher.HasPending()) SyncSpriteBatcher();
    tl_spriteBatcher.OnDeleteOrLinkProgram(program);
    tl_sbProgs.erase(program);
    if (tl_uiBatcher.HasPending()) SyncUiBatcher();
    tl_uiBatcher.OnDeleteOrLinkProgram(program);
    tl_uiProgs.erase(program);
    tl_uiProgAttached.erase(program);
    tl_progWorldRole.erase(program);
    tl_hauntProgs.erase(program);
    tl_lookProgRole.erase(program);
    if (tl_curBoundProg == program) tl_curLookProgRole = 0u;
    auto wpit = tl_worldProgs.find(program);
    if (wpit != tl_worldProgs.end()) {
        if (tl_curWorldProg == &wpit->second) tl_curWorldProg = nullptr;
        tl_worldProgs.erase(wpit);
    }
    if (tl_curBoundProg == program) tl_curBoundProg = 0u;
    tl_extProf.progExtId.erase(program);
    auto epIt = tl_extProf.progSlotMap.find(program);
    if (epIt != tl_extProf.progSlotMap.end()) {
        const int32_t oldS = epIt->second;
        if (oldS >= 0 && static_cast<uint32_t>(oldS) < kMaxExtProfSlots) {
            uint32_t c = g_extProfSlots[oldS].linkedProgs.load(std::memory_order_relaxed);
            while (c > 0u && !g_extProfSlots[oldS].linkedProgs.compare_exchange_weak(c, c - 1u, std::memory_order_relaxed)) {}
        }
        if (tl_extProf.curExtSlot == oldS) tl_extProf.curExtSlot = -1;
        tl_extProf.progSlotMap.erase(epIt);
    }
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
    const uint32_t worldU32 = static_cast<uint32_t>(std::clamp(worldUs, 0.0, 60000000.0));
    g_lastWorldRenderUs.store(worldU32, std::memory_order_relaxed);
    g_frameWorldUs.fetch_add(worldU32, std::memory_order_relaxed);
    UpdateAtomicEma(g_emaWorldRenderUs, static_cast<float>(worldUs), 0.06f);

    if (g_tlsAnimState) {
        ThreadLocalAnimState& tls = *g_tlsAnimState;
        if (tls.frameHookAccumQpc > 0 || tls.frameTweenEnts > 0 ||
            tls.frameTweenBones > 0 || tls.frameShedCount > 0) {
            const uint32_t hookUs = static_cast<uint32_t>(
                std::clamp(static_cast<double>(tls.frameHookAccumQpc) * g_qpcToUs, 0.0, 60000000.0));
            g_lastAnimHookUs.store(hookUs, std::memory_order_relaxed);
            g_lastAnimEnts.store(tls.frameTweenEnts, std::memory_order_relaxed);
            g_lastAnimBones.store(tls.frameTweenBones, std::memory_order_relaxed);
            g_lastAnimShed.store(tls.frameShedCount, std::memory_order_relaxed);
            g_frameAnimUs.fetch_add(hookUs, std::memory_order_relaxed);
            g_frameAnimEnts.fetch_add(tls.frameTweenEnts, std::memory_order_relaxed);
            g_frameAnimBones.fetch_add(tls.frameTweenBones, std::memory_order_relaxed);
            g_frameAnimShed.fetch_add(tls.frameShedCount, std::memory_order_relaxed);
            tls.FlushCounters();
            tls.frameHookAccumQpc = 0;
            tls.frameSampleIdx = 0;
            tls.frameTweenEnts = 0;
            tls.frameTweenBones = 0;
            tls.frameShedCount = 0;
            tls.frameShedLevel = 0;
        }
    }
}

static void HookPostProcessorRender(void* pp, void* renderData, uint32_t primaryTexHandle) {
    const int64_t t0 = NowQpc();
    if (g_origPostProcessorRender) {
        g_origPostProcessorRender(pp, renderData, primaryTexHandle);
    }
    const double postUs = static_cast<double>(NowQpc() - t0) * g_qpcToUs;
    g_tlsLastPostProcUs = postUs;
    const uint32_t postU32 = static_cast<uint32_t>(std::clamp(postUs, 0.0, 60000000.0));
    g_lastPostProcUs.store(postU32, std::memory_order_relaxed);
    g_framePostUs.fetch_add(postU32, std::memory_order_relaxed);
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
    if (!g_origLightMapRender || !lightMap) return;
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

static void OnDynamicShadowInterpPos(const void* dsPtr, float* pos3) {
    if (!dsPtr || !pos3) return;
    if (g_worldPosInterpEnabled.load(std::memory_order_relaxed) == 0 ||
        g_skeletalTweenEnabled.load(std::memory_order_relaxed) == 0) {
        return;
    }
    ThreadLocalAnimState& tls = GetTlsAnimState();
    const int64_t nowQpc = tls.GetFrameQpc();
    const uintptr_t shadowKey = reinterpret_cast<uintptr_t>(dsPtr) | 3ULL;
    if (InterpolateWorldPosCore(&pos3[0], &pos3[1], &pos3[2], shadowKey, tls, nowQpc, false)) {
        static thread_local uint32_t tl_shadowHits = 0;
        if (++tl_shadowHits >= 64u) {
            InterlockedAdd64(&g_shadowPos240Count, static_cast<LONG64>(tl_shadowHits));
            tl_shadowHits = 0;
        }
    }
}

static void PrepareDynamicShadow240Hook(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    // ShadowManager::BuildDynamicShadowMesh @ 0x14008f100
    static constexpr uint8_t kSigBuildDynShadow[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x54,
        0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0x41, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xF1,
        0x8B, 0x90, 0xD4, 0x00
    };
    static constexpr uint8_t kPreEmit[] = {
        0x8B, 0x81, 0x08, 0x02, 0x00, 0x00, 0x48, 0x8B, 0xCE, 0x41, 0x89, 0x40, 0x08,
        0x48, 0x8D, 0x44, 0x24, 0x70, 0x4C, 0x8D, 0x44, 0x24, 0x30, 0x48, 0x89, 0x44, 0x24, 0x20, 0xE8
    };
    static constexpr uint8_t kPostEmit[] = {
        0x48, 0x83, 0xC2, 0x78, 0x49, 0x83, 0xC2, 0x08, 0x4C, 0x3B, 0x57, 0x10, 0x75, 0x88
    };
    uint8_t* pFn = FindUniquePatternInText(textStart, textSize, kSigBuildDynShadow, sizeof(kSigBuildDynShadow));
    if (!pFn) {
        LogMsg("[SHADOW240] DynamicShadow 240Hz skipped (AOB miss; fail-closed)");
        return;
    }
    if (memcmp(pFn + 0x13B, kPreEmit, sizeof(kPreEmit)) != 0 ||
        pFn[0x157] != 0xE8 ||
        memcmp(pFn + 0x15C, kPostEmit, sizeof(kPostEmit)) != 0) {
        LogMsg("[SHADOW240] DynamicShadow 240Hz skipped (callsite +0x157 signature mismatch; fail-closed)");
        return;
    }
    uint8_t* cs = pFn + 0x157;
    uint8_t* origEmit = ResolveRel32Call(cs);
    const uintptr_t textLo = reinterpret_cast<uintptr_t>(textStart);
    const uintptr_t textHi = textLo + textSize;
    if (!origEmit || reinterpret_cast<uintptr_t>(origEmit) < textLo || reinterpret_cast<uintptr_t>(origEmit) >= textHi) {
        LogMsg("[SHADOW240] DynamicShadow 240Hz skipped (bad EmitShadowQuad target)");
        return;
    }

    // Capstone-verified ABI-safe 56-byte W^X near stub: preserves live rcx/rdx/r8/r9/r10 across EmitShadowQuad loop!
    uint8_t* stubMem = AllocNearExecutable(cs, 64);
    if (!stubMem) return;
    intptr_t disp = reinterpret_cast<intptr_t>(stubMem) - reinterpret_cast<intptr_t>(cs + 5);
    if (disp < INT32_MIN || disp > INT32_MAX) {
        VirtualFree(stubMem, 0, MEM_RELEASE);
        return;
    }
    uint8_t stub[56] = {
        0x51, 0x52, 0x41, 0x50, 0x41, 0x51, 0x41, 0x52,       // push rcx,rdx,r8,r9,r10 (rsp stays 16B aligned)
        0x48, 0x83, 0xEC, 0x20,                               // sub rsp, 0x20
        0x49, 0x8B, 0x0A,                                     // mov rcx, [r10] (DynamicShadow*)
        0x4C, 0x89, 0xC2,                                     // mov rdx, r8    (float* pos3)
        0x48, 0xB8, 0,0,0,0,0,0,0,0,                          // movabs rax, &OnDynamicShadowInterpPos (+20)
        0xFF, 0xD0,                                           // call rax
        0x48, 0x83, 0xC4, 0x20,                               // add rsp, 0x20
        0x41, 0x5A, 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59,       // pop r10,r9,r8,rdx,rcx
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,                   // jmp [rip+0] -> origEmit
        0,0,0,0,0,0,0,0                                       // (+48)
    };
    const uint64_t cbAddr = reinterpret_cast<uint64_t>(&OnDynamicShadowInterpPos);
    const uint64_t origAddr = reinterpret_cast<uint64_t>(origEmit);
    memcpy(stub + 20, &cbAddr, sizeof(cbAddr));
    memcpy(stub + 48, &origAddr, sizeof(origAddr));
    memcpy(stubMem, stub, sizeof(stub));
    if (!FinalizeExecPageWX(stubMem, sizeof(stub))) {
        VirtualFree(stubMem, 0, MEM_RELEASE);
        return;
    }

    PendingCodePatch p{};
    p.addr = cs + 1;
    p.len = 4;
    p.ownedExecPage = stubMem;
    const int32_t rel = static_cast<int32_t>(disp);
    memcpy(p.bytes, &rel, sizeof(rel));
    char msg[224];
    snprintf(msg, sizeof(msg), "[SHADOW240] Hooked DynamicShadow @ %p (emit=%p) -> Co-Phase 240Hz Ground Shadows",
             static_cast<void*>(cs), static_cast<void*>(origEmit));
    p.logMsg = msg;
    g_shadow240PatchIdx = patches.size();
    patches.push_back(std::move(p));
}

static void PrepareAnimCmdMainThreadPosHook(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    // Unique Main-Thread AnimRenderCmd::AnimRenderCmd callsite @ 0x1400fae80 (AOB start 0x1400fae2f, callsite +81).
    // Moving WorldPos interpolation from Render Thread -> Main Thread eliminates cross-thread queue phase drift
    // and locks Character Sprite + Ground Shadow + Dynamic Light + FollowCamera onto one single Main-Thread frame clock!
    static constexpr int16_t kAobAnimCmdCtor[] = {
        0xF6,0x81,0x74,0x02,0x00,0x00,0x01, 0x49,0x8B,0xF8, 0x48,0x8B,0xEA, 0x48,0x8B,0xF1,
        0x0F,0x85,-1,-1,-1,-1,
        0x48,0x83,0xB9,0xD0,0x00,0x00,0x00,0x00, 0x0F,0x84,-1,-1,-1,-1,
        0x48,0x83,0xB9,0xD8,0x00,0x00,0x00,0x00, 0x0F,0x84,-1,-1,-1,-1,
        0xB9,0x00,0x02,0x00,0x00, 0x48,0x89,0x5C,0x24,0x38, 0xE8,-1,-1,-1,-1,
        0x33,0xDB, 0x48,0x85,0xC0, 0x74,0x13, 0x4C,0x8B,0xC5, 0x48,0x8B,0xD6, 0x48,0x8B,0xC8,
        0xE8,-1,-1,-1,-1, 0x4C,0x8B,0xD8
    };
    uint8_t* hit = FindUniqueAobInt16(textStart, textSize, kAobAnimCmdCtor, sizeof(kAobAnimCmdCtor) / sizeof(kAobAnimCmdCtor[0]));
    if (!hit) {
        LogMsg("[POS240] Main-Thread AnimRenderCmd ctor hook skipped (AOB miss; RT fallback active)");
        return;
    }
    uint8_t* cs = hit + 81;
    uint8_t* fn = ResolveRel32Call(cs);
    const uintptr_t textLo = reinterpret_cast<uintptr_t>(textStart);
    const uintptr_t textHi = textLo + textSize;
    if (!fn || reinterpret_cast<uintptr_t>(fn) < textLo || reinterpret_cast<uintptr_t>(fn) >= textHi) {
        LogMsg("[POS240] Main-Thread AnimRenderCmd ctor hook skipped (bad target; RT fallback active)");
        return;
    }
    g_origAnimRenderCmdCtor = reinterpret_cast<AnimRenderCmdCtor_t>(fn);
    char msg[224];
    snprintf(msg, sizeof(msg), "[POS240] Hooked Main-Thread AnimRenderCmd::AnimRenderCmd @ %p (fn=%p) -> Unified PLL Timeline Lockstep",
             static_cast<void*>(cs), static_cast<void*>(fn));
    const size_t idx = patches.size();
    if (QueueRel32NearRelayHook(patches, cs, reinterpret_cast<const void*>(&HookAnimRenderCmdCtor), msg)) {
        g_animCmdCtorPatchIdx = idx;
    }
}

static uint64_t HookClientLuaUpdate(void* sim, uint32_t a2, uint32_t a3) {
    if (!g_origClientLuaUpdate) return 0;
    const int64_t t0 = NowQpc();
    const uint64_t ret = g_origClientLuaUpdate(sim, a2, a3);
    const int64_t t1 = NowQpc();
    if (t1 > t0 && g_qpcToUs > 0.0) {
        const uint32_t us = static_cast<uint32_t>(std::min(static_cast<double>(t1 - t0) * g_qpcToUs, 2000000.0));
        if (us > 0) g_frameLuaUs.fetch_add(us, std::memory_order_relaxed);
    }
    return ret;
}

static void SampleClientNetTelemetry(void* netMgr, int64_t nowQpc) noexcept {
    if (!netMgr || g_qpcFreq <= 0) return;
    const int64_t prev = g_lastNetSampleQpc.load(std::memory_order_relaxed);
    if (prev > 0 && ((nowQpc - prev) * 2 < g_qpcFreq)) return;
    g_lastNetSampleQpc.store(nowQpc, std::memory_order_relaxed);

    const uint8_t* nm = static_cast<const uint8_t*>(netMgr);
    const bool connected = (nm[0x30A] != 0);
    void* peer = *reinterpret_cast<void* const*>(nm + 0x2B0);
    static uint32_t s_prevResendMsgs = 0;
    static int64_t  s_lastNetBbQpc = 0;
    if (!connected || !peer) {
        s_prevResendMsgs = 0;
        g_netConnected.store(false, std::memory_order_relaxed);
        g_netPingAvg.store(-1, std::memory_order_relaxed);
        g_netPingLast.store(-1, std::memory_order_relaxed);
        g_netLossCur.store(0.0f, std::memory_order_relaxed);
        g_netLossAvg.store(0.0f, std::memory_order_relaxed);
        g_netResendMsgs.store(0, std::memory_order_relaxed);
        g_netTxBps.store(0, std::memory_order_relaxed);
        g_netRxBps.store(0, std::memory_order_relaxed);
        return;
    }

    alignas(8) uint8_t aog[40]{};
    std::memset(aog, 0xFF, 10);
    aog[0x10] = 2;    // AF_INET (UNASSIGNED_SYSTEM_ADDRESS starts at +0x10, 20 bytes)
    aog[0x22] = 0xFF; // systemIndex = 0xFFFF
    aog[0x23] = 0xFF;

    void* const* vtbl = *reinterpret_cast<void* const* const*>(peer);
    int pingAvg = -1;
    int pingLast = -1;
    const uint8_t* serverGuid = *reinterpret_cast<const uint8_t* const*>(nm + 0x328);
    if (serverGuid && vtbl) {
        uint64_t guidHead = 0;
        std::memcpy(&guidHead, serverGuid, sizeof(guidHead));
        if (guidHead != 0xFFFFFFFFFFFFFFFFULL) {
            std::memcpy(aog, serverGuid, 10);
            using PingFn_t = int (*)(void*, const void*);
            if (vtbl[0x150 / 8]) {
                pingAvg = reinterpret_cast<PingFn_t>(vtbl[0x150 / 8])(peer, aog);
            }
            if (vtbl[0x158 / 8]) {
                pingLast = reinterpret_cast<PingFn_t>(vtbl[0x158 / 8])(peer, aog);
            }
        }
    }

    uint64_t txBps = 0, rxBps = 0;
    uint32_t resendMsgs = 0;
    float lossCur = 0.0f, lossAvg = 0.0f;
    if (vtbl && vtbl[0x2B0 / 8]) {
        using GetStatsFn_t = const uint8_t* (*)(void*, const void*, void*);
        const uint8_t* rns = reinterpret_cast<GetStatsFn_t>(vtbl[0x2B0 / 8])(peer, aog + 0x10, nullptr);
        if (rns) {
            std::memcpy(&txBps, rns + 0x28, sizeof(txBps));
            std::memcpy(&rxBps, rns + 0x30, sizeof(rxBps));
            std::memcpy(&resendMsgs, rns + 0xC8, sizeof(resendMsgs));
            float rawCur = 0.0f, rawAvg = 0.0f;
            std::memcpy(&rawCur, rns + 0xD8, sizeof(rawCur));
            std::memcpy(&rawAvg, rns + 0xDC, sizeof(rawAvg));
            lossCur = std::isfinite(rawCur) ? std::clamp(rawCur * 100.0f, 0.0f, 100.0f) : 0.0f;
            lossAvg = std::isfinite(rawAvg) ? std::clamp(rawAvg * 100.0f, 0.0f, 100.0f) : 0.0f;
        }
    }

    g_netConnected.store(true, std::memory_order_relaxed);
    g_netPingAvg.store(pingAvg, std::memory_order_relaxed);
    g_netPingLast.store(pingLast, std::memory_order_relaxed);
    g_netLossCur.store(lossCur, std::memory_order_relaxed);
    g_netLossAvg.store(lossAvg, std::memory_order_relaxed);
    g_netResendMsgs.store(resendMsgs, std::memory_order_relaxed);
    g_netTxBps.store(txBps, std::memory_order_relaxed);
    g_netRxBps.store(rxBps, std::memory_order_relaxed);

    if (g_inGameplay.load(std::memory_order_relaxed) != 0 &&
        g_optBlackbox.load(std::memory_order_relaxed) &&
        pingAvg >= 0) {
        const bool netSpike =
            (pingLast >= 0 && ((pingLast - pingAvg) >= 60 || pingLast >= 180)) ||
            (lossCur >= 2.0f) ||
            (resendMsgs > s_prevResendMsgs && (resendMsgs - s_prevResendMsgs) >= 4u);
        if (netSpike && (s_lastNetBbQpc == 0 || (nowQpc - s_lastNetBbQpc) >= (g_qpcFreq * 2))) {
            s_lastNetBbQpc = nowQpc;
            hs_blackbox::ClientEvent ev{};
            ev.epochMs    = NowEpochMs();
            ev.frameSeq   = g_frameSeq.load(std::memory_order_relaxed);
            const int64_t lastFUs = g_lastFrameUs.load(std::memory_order_relaxed);
            ev.frameUs    = (lastFUs > 0) ? static_cast<uint32_t>(std::min<int64_t>(lastFUs, 60000000LL)) : 0u;
            ev.cpuBusyUs  = g_lastFrameBusyUs.load(std::memory_order_relaxed);
            ev.gcKb       = g_lastGcKb.load(std::memory_order_relaxed);
            ev.pingAvgMs  = pingAvg;
            ev.pingLastMs = pingLast;
            ev.lossX10    = static_cast<uint16_t>(std::clamp(static_cast<double>(lossCur) * 10.0 + 0.5, 0.0, 1000.0));
            ev.resendMsgs = resendMsgs;
            ev.rxKbps     = static_cast<uint32_t>(std::min<uint64_t>(rxBps / 1024ULL, UINT32_MAX));
            ev.triggers   = hs_blackbox::kTrigNet;
            ev.isNativeGl = g_nativeGlActive.load(std::memory_order_relaxed) ? 1u : 0u;
            hs_blackbox::ComputeClientDerived(ev);
            AcquireSRWLockExclusive(&g_bbLock);
            g_clientBlackbox.Push(ev);
            ReleaseSRWLockExclusive(&g_bbLock);
        }
    }
    s_prevResendMsgs = resendMsgs;
}

static void HookClientNetUpdate(void* netMgr, float dt) {
    if (!g_origClientNetUpdate) return;
    const int64_t t0 = NowQpc();
    g_origClientNetUpdate(netMgr, dt);
    const int64_t t1 = NowQpc();
    if (t1 > t0 && g_qpcToUs > 0.0) {
        const uint32_t us = static_cast<uint32_t>(std::min(static_cast<double>(t1 - t0) * g_qpcToUs, 2000000.0));
        if (us > 0) g_frameNetUs.fetch_add(us, std::memory_order_relaxed);
    }
    SampleClientNetTelemetry(netMgr, t1);
}

static void PrepareLuaAndNetTelemetryHooks(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    const uintptr_t textLo = reinterpret_cast<uintptr_t>(textStart);
    const uintptr_t textHi = textLo + textSize;

    // 1. Simulation::luaUpdate callsite @ 0x140108e77 (window starts at 0x140108e5c, E8 at +27)
    static constexpr int16_t kAobLuaUpdateCs[] = {
        0x48, 0x8B, 0x8B, 0xB0, 0x00, 0x00, 0x00, 0x66, 0x41, 0x0F, 0x28, 0xCB, 0xE8, -1, -1, -1, -1,
        0x45, 0x33, 0xC0, 0x48, 0x8B, 0xCB, 0x41, 0x8D, 0x50, 0x01, 0xE8, -1, -1, -1, -1,
        0x48, 0x8D, 0x8C, 0x24, 0xA0, 0x00, 0x00, 0x00, 0xE8, -1, -1, -1, -1,
        0x48, 0x8D, 0x8C, 0x24, 0xA8, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x58, 0x83, 0x14, 0x01, 0x00, 0x00,
        0xF3, 0x0F, 0x11, 0x83, 0x14, 0x01, 0x00, 0x00, 0xE8, -1, -1, -1, -1,
        0x80, 0xBB, 0x30, 0x02, 0x00, 0x00, 0x00
    };
    if (uint8_t* pLuaWin = FindUniqueAobInt16(textStart, textSize, kAobLuaUpdateCs,
                                              sizeof(kAobLuaUpdateCs) / sizeof(kAobLuaUpdateCs[0]))) {
        uint8_t* cs = pLuaWin + 27;
        uint8_t* fn = ResolveRel32Call(cs);
        if (fn && reinterpret_cast<uintptr_t>(fn) >= textLo && reinterpret_cast<uintptr_t>(fn) < textHi) {
            g_origClientLuaUpdate = reinterpret_cast<ClientLuaUpdate_t>(fn);
            char msg[192];
            snprintf(msg, sizeof(msg), "[TELEM] Hooked Simulation::luaUpdate callsite @ %p (fn=%p) -> Hitch lua= subphase",
                     static_cast<void*>(cs), static_cast<void*>(fn));
            QueueRel32NearRelayHook(patches, cs, reinterpret_cast<const void*>(&HookClientLuaUpdate), msg);
        }
    }

    // 2. cNetworkManager::Update @ 0x1401cde50 (21-byte zero-RIP-rel prologue)
    static constexpr int16_t kAobNetUpdate[] = {
        0x40, 0x55, 0x41, 0x55, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x30, 0x0A, 0x00, 0x00,
        0x0F, 0x29, 0xB4, 0x24, 0x00, 0x0A, 0x00, 0x00
    };
    if (uint8_t* pNet = FindUniqueAobInt16(textStart, textSize, kAobNetUpdate,
                                           sizeof(kAobNetUpdate) / sizeof(kAobNetUpdate[0]))) {
        void* origFn = nullptr;
        char msg[192];
        snprintf(msg, sizeof(msg), "[TELEM] Hooked cNetworkManager::Update @ %p -> Hitch net= subphase",
                 static_cast<void*>(pNet));
        if (QueuePrologueTrampolineHook(patches, pNet, 21, reinterpret_cast<const void*>(&HookClientNetUpdate), origFn, msg)) {
            g_origClientNetUpdate = reinterpret_cast<ClientNetUpdate_t>(origFn);
        }
    }

    // 3. Read-only microsecond sub-phase breakdown inside cNetworkManager::Update via .rdata vtable slots
    //    (ReplicaManager3::Update, ReplicaManager3::OnReceive, RPC4::OnReceive — 0 .text writes, fail-closed!)
    using Rm3Update_t  = void (*)(void* self);
    using PluginRx_t   = int32_t (*)(void* self, void* packet);
    static Rm3Update_t s_origRm3Update = nullptr;
    static PluginRx_t  s_origRm3Rx     = nullptr;
    static PluginRx_t  s_origRpc4Rx    = nullptr;

    struct NetSubHooks {
        static void RecordSlowPkt(uint32_t us, void* pkt, bool isRpc) noexcept {
            const uint32_t prev = g_frameSlowPktUs.load(std::memory_order_relaxed);
            if (us <= prev) return;
            uint32_t bytes = 0;
            uint8_t  pktId = 0;
            if (pkt) {
                const auto* raw = static_cast<const uint8_t*>(pkt);
                bytes = *reinterpret_cast<const uint32_t*>(raw + 0x28);
                const uint8_t* data = *reinterpret_cast<const uint8_t* const*>(raw + 0x30);
                if (data && bytes >= 1u) {
                    pktId = data[0];
                    if (pktId == 0x1Bu && bytes >= 10u) pktId = data[9]; // skip ID_TIMESTAMP
                }
            }
            const uint32_t meta = (isRpc ? 0x80000000u : 0u) |
                                  (static_cast<uint32_t>(pktId) << 23) |
                                  (bytes & 0x7FFFFFu);
            g_frameSlowPktUs.store(us, std::memory_order_relaxed);
            g_frameSlowPktMeta.store(meta, std::memory_order_relaxed);
        }
        static void HookRm3Update(void* self) {
            if (!s_origRm3Update) return;
            const int64_t t0 = NowQpc();
            s_origRm3Update(self);
            const int64_t t1 = NowQpc();
            if (t1 > t0 && g_qpcToUs > 0.0) {
                const uint32_t us = static_cast<uint32_t>(std::min(static_cast<double>(t1 - t0) * g_qpcToUs, 2000000.0));
                if (us > 0) g_frameRmUpUs.fetch_add(us, std::memory_order_relaxed);
            }
        }
        static int32_t HookRm3Rx(void* self, void* pkt) {
            if (!s_origRm3Rx) return 1;
            const int64_t t0 = NowQpc();
            const int32_t rc = s_origRm3Rx(self, pkt);
            const int64_t t1 = NowQpc();
            if (t1 > t0 && g_qpcToUs > 0.0) {
                const uint32_t us = static_cast<uint32_t>(std::min(static_cast<double>(t1 - t0) * g_qpcToUs, 2000000.0));
                if (us > 0) {
                    g_frameRmRxUs.fetch_add(us, std::memory_order_relaxed);
                    RecordSlowPkt(us, pkt, false);
                }
            }
            g_frameRmRxCnt.fetch_add(1, std::memory_order_relaxed);
            return rc;
        }
        static int32_t HookRpc4Rx(void* self, void* pkt) {
            if (!s_origRpc4Rx) return 1;
            const int64_t t0 = NowQpc();
            const int32_t rc = s_origRpc4Rx(self, pkt);
            const int64_t t1 = NowQpc();
            if (t1 > t0 && g_qpcToUs > 0.0) {
                const uint32_t us = static_cast<uint32_t>(std::min(static_cast<double>(t1 - t0) * g_qpcToUs, 2000000.0));
                if (us > 0) {
                    g_frameRpcRxUs.fetch_add(us, std::memory_order_relaxed);
                    RecordSlowPkt(us, pkt, true);
                }
            }
            g_frameRpcRxCnt.fetch_add(1, std::memory_order_relaxed);
            return rc;
        }
    };

    static constexpr int16_t kAobRm3Update[] = {
        0x40,0x56,0x48,0x81,0xEC,0x00,0x02,0x00,0x00,0x48,0x8B,0x05,-1,-1,-1,-1,
        0x48,0x33,0xC4,0x48,0x89,0x84,0x24,0xD0,0x01,0x00,0x00,0x48,0x8B,0xF1,
        0x4C,0x8D,0x05,-1,-1,-1,-1,0x48,0x8D,0x15,-1,-1,-1,-1,0x48,0x8D,0x4C,0x24,0x41,
        0x41,0xB9,0x73,0x00,0x00,0x00
    };
    static constexpr int16_t kAobRm3DerivedRx[] = {
        0x40,0x57,0x48,0x83,0xEC,0x40,0x4C,0x8B,0xCA,0x4C,0x8B,0xC1,0x48,0x85,0xD2,
        0x0F,0x84,0x2B,0x01,0x00,0x00,0x80,0x7A,0x39,0x00,0x0F,0x85,0x21,0x01,0x00,0x00,0x8B
    };
    static constexpr int16_t kAobRm3BaseRx[] = {
        0x40,0x53,0x57,0x48,0x81,0xEC,0xA8,0x01,0x00,0x00,0x48,0x8B,0x05,-1,-1,-1,-1,
        0x48,0x33,0xC4,0x48,0x89,0x84,0x24,0x90,0x01,0x00,0x00,0x48,0x8B,0xF9,
        0x8B,0x4A,0x28,0x48,0x8B,0xDA,0x83,0xF9,0x01
    };
    static constexpr int16_t kAobRpc4Rx[] = {
        0x40,0x53,0x55,0x56,0x57,0x48,0x81,0xEC,0x28,0x05,0x00,0x00,0x48,0x8B,0x05,-1,-1,-1,-1,
        0x48,0x33,0xC4,0x48,0x89,0x84,0x24,0x10,0x05,0x00,0x00,0x48,0x8B,0xF1,
        0x48,0x8B,0x4A,0x30,0x48,0x8B,0xFA,0x80,0x39,0x4A
    };

    uint8_t* fnRmUp        = FindUniqueAobInt16(textStart, textSize, kAobRm3Update,    sizeof(kAobRm3Update) / sizeof(kAobRm3Update[0]));
    uint8_t* fnRmDerivedRx = FindUniqueAobInt16(textStart, textSize, kAobRm3DerivedRx, sizeof(kAobRm3DerivedRx) / sizeof(kAobRm3DerivedRx[0]));
    uint8_t* fnRmBaseRx    = FindUniqueAobInt16(textStart, textSize, kAobRm3BaseRx,    sizeof(kAobRm3BaseRx) / sizeof(kAobRm3BaseRx[0]));
    uint8_t* fnRpcRx       = FindUniqueAobInt16(textStart, textSize, kAobRpc4Rx,       sizeof(kAobRpc4Rx) / sizeof(kAobRpc4Rx[0]));

    if (fnRmUp || fnRpcRx) {
        HMODULE hExe = GetModuleHandleW(nullptr);
        auto* base = reinterpret_cast<uint8_t*>(hExe);
        auto* dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        auto* nt   = (dos && dos->e_magic == IMAGE_DOS_SIGNATURE)
            ? reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew) : nullptr;
        if (nt && nt->Signature == IMAGE_NT_SIGNATURE) {
            IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
            for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
                if (memcmp(sec[i].Name, ".rdata", 6) == 0) {
                    auto** rPtrs = reinterpret_cast<void**>(base + sec[i].VirtualAddress);
                    const size_t nPtrs = sec[i].Misc.VirtualSize / sizeof(void*);
                    auto patchSlot = [](void** slot, void* newFn) -> bool {
                        DWORD oldProt = 0;
                        if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &oldProt)) return false;
                        InterlockedExchangePointer(slot, newFn);
                        DWORD dummy = 0;
                        VirtualProtect(slot, sizeof(void*), oldProt, &dummy);
                        return true;
                    };
                    bool okRmUp = false, okRmRx = false, okRpc = false;
                    uint8_t* rmRxCandidates[2] = { fnRmDerivedRx, fnRmBaseRx };
                    if (fnRmUp) {
                        for (uint8_t* fnRmRx : rmRxCandidates) {
                            if (!fnRmRx) continue;
                            void** rmUpSlot = nullptr;
                            size_t rmHits = 0;
                            for (size_t k = 0; k + 1 < nPtrs; ++k) {
                                if (rPtrs[k] == fnRmUp && rPtrs[k + 1] == fnRmRx) {
                                    rmUpSlot = &rPtrs[k];
                                    ++rmHits;
                                }
                            }
                            if (rmHits == 1 && rmUpSlot) {
                                s_origRm3Update = reinterpret_cast<Rm3Update_t>(fnRmUp);
                                s_origRm3Rx     = reinterpret_cast<PluginRx_t>(fnRmRx);
                                okRmUp = patchSlot(rmUpSlot,     reinterpret_cast<void*>(&NetSubHooks::HookRm3Update));
                                okRmRx = patchSlot(rmUpSlot + 1, reinterpret_cast<void*>(&NetSubHooks::HookRm3Rx));
                                break;
                            }
                        }
                    }
                    if (fnRpcRx) {
                        void** rpcSlot = nullptr;
                        size_t rpcHits = 0;
                        for (size_t k = 0; k < nPtrs; ++k) {
                            if (rPtrs[k] == fnRpcRx) {
                                rpcSlot = &rPtrs[k];
                                ++rpcHits;
                            }
                        }
                        if (rpcHits == 1 && rpcSlot) {
                            s_origRpc4Rx = reinterpret_cast<PluginRx_t>(fnRpcRx);
                            okRpc = patchSlot(rpcSlot, reinterpret_cast<void*>(&NetSubHooks::HookRpc4Rx));
                        }
                    }
                    LogMsg("[TELEM][NETBRK] RakNet vtable sub-phase telemetry (rmUp=%d rmRx=%d rpcRx=%d)",
                           okRmUp ? 1 : 0, okRmRx ? 1 : 0, okRpc ? 1 : 0);
                    break;
                }
            }
        }
    }

    // 4. Main-Loop 7-Phase X-Ray Telemetry (zero-overhead 5-byte callsite hooks covering 100% of Application::DoLoop)
    //    Exposes every unmeasured main-thread bucket: LuaWallUpdate (240Hz), EntityManagerWallUpdate (240Hz),
    //    EntityManagerUpdate (60Hz), EntityManagerPostUpdate (240Hz), ActualCacheRender (240Hz),
    //    CacheRenderUI (240Hz), and mSoundSystem::Update (240Hz).
    using WallLuaFn_t   = uint64_t (*)(void* sim, uint32_t a2, uint32_t a3);
    using EntDtFn_t     = void (*)(void* em, float dt);
    using VoidPtrFn_t   = void (*)(void* self);
    using CacheRndFn_t  = void (*)(void* game, void* cache, float dt);
    using UiDrawFn_t    = void (*)(void* game, void* uiCache);
    static WallLuaFn_t  s_origWallLua  = nullptr;
    static EntDtFn_t    s_origEntWall  = nullptr;
    static EntDtFn_t    s_origEntUpd   = nullptr;
    static VoidPtrFn_t  s_origEntPost  = nullptr;
    static CacheRndFn_t s_origCacheRnd = nullptr;
    static UiDrawFn_t   s_origUiDraw   = nullptr;
    static VoidPtrFn_t  s_origSoundUpd = nullptr;

    struct LoopXrayHooks {
        static inline void AddUs(std::atomic<uint32_t>& dst, int64_t t0) noexcept {
            const int64_t t1 = NowQpc();
            if (t1 > t0 && g_qpcToUs > 0.0) {
                const uint32_t us = static_cast<uint32_t>(std::min(static_cast<double>(t1 - t0) * g_qpcToUs, 60000000.0));
                if (us > 0) dst.fetch_add(us, std::memory_order_relaxed);
            }
        }
        static uint64_t HookWallLua(void* sim, uint32_t a2, uint32_t a3) {
            if (!s_origWallLua) return 0;
            const int64_t t0 = NowQpc();
            const uint64_t r = s_origWallLua(sim, a2, a3);
            AddUs(g_frameWallLuaUs, t0);
            return r;
        }
        static void HookEntWall(void* em, float dt) {
            if (!s_origEntWall) return;
            const int64_t t0 = NowQpc();
            s_origEntWall(em, dt);
            AddUs(g_frameEntWallUs, t0);
        }
        static void HookEntUpd(void* em, float dt) {
            if (!s_origEntUpd) return;
            const int64_t t0 = NowQpc();
            s_origEntUpd(em, dt);
            AddUs(g_frameEntUpdUs, t0);
        }
        static void HookEntPost(void* em) {
            if (!s_origEntPost) return;
            const int64_t t0 = NowQpc();
            s_origEntPost(em);
            AddUs(g_frameEntPostUs, t0);
        }
        static void HookCacheRnd(void* game, void* cache, float dt) {
            if (!s_origCacheRnd) return;
            const int64_t t0 = NowQpc();
            s_origCacheRnd(game, cache, dt);
            AddUs(g_frameCacheRndUs, t0);
        }
        static void HookUiDraw(void* game, void* uiCache) {
            if (!s_origUiDraw) return;
            const int64_t t0 = NowQpc();
            const bool prevUi = tl_inCacheRenderUi;
            tl_inCacheRenderUi = true;
            s_origUiDraw(game, uiCache);
            tl_inCacheRenderUi = prevUi;
            AddUs(g_frameUiDrawUs, t0);
        }
        static void HookSoundUpd(void* snd) {
            if (!s_origSoundUpd) return;
            const int64_t t0 = NowQpc();
            s_origSoundUpd(snd);
            AddUs(g_frameSoundUs, t0);
        }
    };

    auto hookLoopSite = [&](const int16_t* pat, size_t patLen, size_t callOff, const void* hookFn, auto& origFnOut) -> bool {
        uint8_t* win = FindUniqueAobInt16(textStart, textSize, pat, patLen);
        if (!win) return false;
        uint8_t* cs = win + callOff;
        uint8_t* fn = ResolveRel32Call(cs);
        if (!fn || reinterpret_cast<uintptr_t>(fn) < textLo || reinterpret_cast<uintptr_t>(fn) >= textHi) return false;
        if (!origFnOut) origFnOut = reinterpret_cast<std::remove_reference_t<decltype(origFnOut)>>(fn);
        return QueueRel32NearRelayHook(patches, cs, hookFn, "");
    };

    static constexpr int16_t kAobWallLua[] = {
        0x41,0x0F,0x5A,0xC9,0xE8,-1,-1,-1,-1,0x45,0x33,0xC0,0x41,0x8D,0x50,0x01,
        0x49,0x8B,0xCD,0xE8,-1,-1,-1,-1,0x48,0x8D,0x4C,0x24,0x70
    };
    static constexpr int16_t kAobEntWall[] = {
        0x49,0x8B,0x4D,0x78,0x41,0x0F,0x28,0xC9,0xE8,-1,-1,-1,-1,0x44,0x0F,0x2F,0x15,-1,-1,-1,-1,0x76,0x08
    };
    static constexpr int16_t kAobEntUpdA[] = {
        0x49,0x8B,0x4D,0x78,0x41,0x0F,0x28,0xC9,0xE8,-1,-1,-1,-1,0x41,0x80,0x7D,0x40,0x00,0x75,0x20
    };
    static constexpr int16_t kAobEntUpdB[] = {
        0x48,0x8B,0x4B,0x78,0xF3,0x0F,0x10,0x0D,-1,-1,-1,-1,0xE8,-1,-1,-1,-1,0x48,0x8B,0x8B,0xB8,0x00,0x00,0x00
    };
    static constexpr int16_t kAobEntPost[] = {
        0x49,0x8B,0x4D,0x78,0xE8,-1,-1,-1,-1,0x41,0xFF,0x85,0x80,0x00,0x00,0x00,0xC6,0x05
    };
    static constexpr int16_t kAobCacheRnd[] = {
        0x40,0x84,0xF6,0x75,0x0F,0x48,0x8B,0x4B,0x18,0x0F,0x28,0xD6,0x48,0x8B,0xD5,0xE8,-1,-1,-1,-1,
        0x48,0x8D,0x8C,0x24,0x80,0x00,0x00,0x00
    };
    static constexpr int16_t kAobUiDraw[] = {
        0x48,0x8B,0xCB,0x4C,0x89,0x9F,0xB8,0x05,0x00,0x00,0xE8,-1,-1,-1,-1,0x48,0x8B,0x0D
    };
    static constexpr int16_t kAobSoundUpd[] = {
        0x41,0xB9,-1,-1,0x00,0x00,0xE8,-1,-1,-1,-1,0x48,0x8B,0x8F,0xB8,0x00,0x00,0x00,
        0xE8,-1,-1,-1,-1,0x48,0x8D,0x4C,0x24,0x50,0xE8
    };

    const bool okWL  = hookLoopSite(kAobWallLua,  sizeof(kAobWallLua)/sizeof(kAobWallLua[0]),   19, reinterpret_cast<const void*>(&LoopXrayHooks::HookWallLua),  s_origWallLua);
    const bool okEW  = hookLoopSite(kAobEntWall,  sizeof(kAobEntWall)/sizeof(kAobEntWall[0]),    8, reinterpret_cast<const void*>(&LoopXrayHooks::HookEntWall),  s_origEntWall);
    const bool okEUA = hookLoopSite(kAobEntUpdA,  sizeof(kAobEntUpdA)/sizeof(kAobEntUpdA[0]),    8, reinterpret_cast<const void*>(&LoopXrayHooks::HookEntUpd),   s_origEntUpd);
    const bool okEUB = hookLoopSite(kAobEntUpdB,  sizeof(kAobEntUpdB)/sizeof(kAobEntUpdB[0]),   12, reinterpret_cast<const void*>(&LoopXrayHooks::HookEntUpd),   s_origEntUpd);
    const bool okEP  = hookLoopSite(kAobEntPost,  sizeof(kAobEntPost)/sizeof(kAobEntPost[0]),    4, reinterpret_cast<const void*>(&LoopXrayHooks::HookEntPost),  s_origEntPost);
    const bool okCR  = hookLoopSite(kAobCacheRnd, sizeof(kAobCacheRnd)/sizeof(kAobCacheRnd[0]), 15, reinterpret_cast<const void*>(&LoopXrayHooks::HookCacheRnd), s_origCacheRnd);
    const bool okUI  = hookLoopSite(kAobUiDraw,   sizeof(kAobUiDraw)/sizeof(kAobUiDraw[0]),     10, reinterpret_cast<const void*>(&LoopXrayHooks::HookUiDraw),   s_origUiDraw);
    const bool okSND = hookLoopSite(kAobSoundUpd, sizeof(kAobSoundUpd)/sizeof(kAobSoundUpd[0]), 18, reinterpret_cast<const void*>(&LoopXrayHooks::HookSoundUpd), s_origSoundUpd);
    LogMsg("[TELEM][LOOP] Main-loop 7-phase X-Ray (wLua=%d eWall=%d eUpd=%d/2 ePost=%d cRnd=%d ui=%d snd=%d)",
           okWL ? 1 : 0, okEW ? 1 : 0, (okEUA ? 1 : 0) + (okEUB ? 1 : 0),
           okEP ? 1 : 0, okCR ? 1 : 0, okUI ? 1 : 0, okSND ? 1 : 0);
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
    if (g_optRpcQos.load(std::memory_order_relaxed) &&
        g_rpcQosHooked.load(std::memory_order_acquire) &&
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

static void PrepareRpcQosHook(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    // RakNet::RPC4::Call @ 0x1403b7b60 (14-byte zero-RIP-rel prologue, 36B unique AOB)
    static constexpr int16_t kAobRpc4Call[] = {
        0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x48, 0x81, 0xEC, 0x30, 0x03, 0x00, 0x00,
        0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24,
        0x20, 0x03, 0x00, 0x00, 0x48, 0x8B, 0x9C, 0x24
    };
    if (uint8_t* pRpc = FindUniqueAobInt16(textStart, textSize, kAobRpc4Call,
                                           sizeof(kAobRpc4Call) / sizeof(kAobRpc4Call[0]))) {
        void* origFn = nullptr;
        char msg[192];
        snprintf(msg, sizeof(msg), "[NET][QOS] Hooked RakNet::RPC4::Call @ %p -> Causal 3-Channel RPC QoS (Ch0/Ch1/Ch2)",
                 static_cast<void*>(pRpc));
        if (QueuePrologueTrampolineHook(patches, pRpc, 14, reinterpret_cast<const void*>(&HookRpc4Call), origFn, msg)) {
            g_origRpc4Call = reinterpret_cast<Rpc4Call_t>(origFn);
            g_rpcQosPatchIdx = patches.size() - 1;
        }
    } else {
        LogMsg("[NET][QOS] Skipped RakNet::RPC4::Call hook (AOB miss; fail-closed)");
    }
}

static uint8_t HookWritePersistentFile(const char* utf8Path, const void* data, uint32_t size,
                                       void* delegate, uint32_t arg5, void* arg6) {
    struct CaptureGuard {
        ClientAsyncCaptureState prev;
        CaptureGuard(bool enable, const void* d, uint32_t sz) noexcept
            : prev(tl_asyncCapture) {
            if (enable) {
                tl_asyncCapture.active = true;
                tl_asyncCapture.data = d;
                tl_asyncCapture.size = sz;
            }
        }
        ~CaptureGuard() noexcept { tl_asyncCapture = prev; }
    };

    if (!g_origWritePersistentFile) return 0;

    // EncodePersistentString is never substituted with placeholder tokens: data is ALWAYS complete,
    // independently decodable save bytes. HookFsWfopen commits it atomically with per-path serialization
    // and verified FlushFileBuffers + MoveFileExW before reporting success.
    const bool asyncOn = g_optAsyncSave.load(std::memory_order_acquire) &&
                         g_asyncSaveActive.load(std::memory_order_acquire);
    const bool enablePlain = asyncOn && data != nullptr && size > 0;
    CaptureGuard guard(enablePlain, data, size);
    return g_origWritePersistentFile(utf8Path, data, size, delegate, arg5, arg6);
}

static void PrepareAsyncSaveHooks(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    // WritePersistentFile @ 0x14018d600 (17-byte zero-RIP-rel prologue, 32B unique AOB)
    static constexpr int16_t kAobWritePersistentFile[] = {
        0x40, 0x53, 0x55, 0x56, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
        0x48, 0x81, 0xEC, 0xD8, 0x00, 0x00, 0x00,
        0x48, 0x8B, 0x05, -1, -1, -1, -1, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xB8
    };
    if (uint8_t* pWpf = FindUniqueAobInt16(textStart, textSize, kAobWritePersistentFile,
                                           sizeof(kAobWritePersistentFile) / sizeof(kAobWritePersistentFile[0]))) {
        void* origFn = nullptr;
        char msg[192];
        snprintf(msg, sizeof(msg), "[PERF][SAVE] Hooked WritePersistentFile @ %p -> Verified Atomic Save-Commit",
                 static_cast<void*>(pWpf));
        if (QueuePrologueTrampolineHook(patches, pWpf, 17, reinterpret_cast<const void*>(&HookWritePersistentFile), origFn, msg)) {
            g_origWritePersistentFile = reinterpret_cast<WritePersistentFile_t>(origFn);
            g_writePersistPatchIdx = patches.size() - 1;
        }
    } else {
        LogMsg("[PERF][SAVE] Skipped WritePersistentFile hook (AOB miss; fail-closed)");
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

static uint8_t HookHWBufferInit(void* self, const void* data) {
    if (self && g_optVbPool.load(std::memory_order_relaxed)) {
        auto* raw = static_cast<uint8_t*>(self);
        auto vtbl = *reinterpret_cast<void** const*>(self);
        if (vtbl && vtbl[2]) {
            using GetTarget_t = uint32_t (*)(void*);
            const uint32_t target    = reinterpret_cast<GetTarget_t>(vtbl[2])(self);
            const uint32_t stride    = *reinterpret_cast<const uint32_t*>(raw + 0x08);
            const uint32_t count     = *reinterpret_cast<const uint32_t*>(raw + 0x0C);
            const uint32_t usageEnum = *reinterpret_cast<const uint32_t*>(raw + 0x14);
            size_t size = 0;
            uint32_t glUsage = 0;
            uint32_t bufId = 0;
            if (hs_vb_pool::VbPool::IsPoolable(target, usageEnum, stride, count, size, glUsage) &&
                g_vbPool.AcquireAndUpload(target, glUsage, size, data,
                                          g_nativeGlActive.load(std::memory_order_relaxed), bufId)) {
                SyncSpriteBatcher();
                tl_spriteBatcher.OnBindBuffer(target, bufId);
                tl_spriteBatcher.OnBufferUpload(bufId, data, size, /*isDynamic=*/true);
                tl_uiBatcher.OnBindBuffer(target, bufId);
                const bool captureUi = g_optSpriteBatch.load(std::memory_order_relaxed) &&
                                       (size <= hs_ui_batch::kMaxUiShadowCaptureBytes);
                tl_uiBatcher.OnBufferUpload(bufId, data, size, captureUi);
                *reinterpret_cast<uint32_t*>(raw + 0x10) = bufId;
                return 1u;
            }
        }
    }
    return g_origHWBufferInit ? g_origHWBufferInit(self, data) : 0u;
}

static void PrepareVbPoolHook(std::vector<PendingCodePatch>& patches, uint8_t* textStart, size_t textSize) {
    // HWBuffer::Init @ 0x140383c30 (first 15 bytes: mov [rsp+8],rbx; mov [rsp+10],rbp; mov [rsp+18],rsi; 0 RIP-rel)
    static constexpr uint8_t kSigHWBufferInit[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
        0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x8B, 0xE2, 0x48,
        0x8D, 0x51, 0x10, 0x48, 0x8B, 0xE9, 0xB9, 0x01, 0x00, 0x00, 0x00
    };
    uint8_t* pInit = FindUniquePatternInText(textStart, textSize, kSigHWBufferInit, sizeof(kSigHWBufferInit));
    if (!pInit) {
        LogMsg("[PERF][VBPOOL] Skipped HWBuffer::Init hook (AOB miss; fail-closed)");
        return;
    }
    void* origFn = nullptr;
    char msg[192];
    snprintf(msg, sizeof(msg), "[PERF][VBPOOL] Hooked HWBuffer::Init @ %p -> Dual-Track GPU Buffer Pool (32MB/2-frame retire)",
             static_cast<void*>(pInit));
    if (QueuePrologueTrampolineHook(patches, pInit, 15, reinterpret_cast<const void*>(&HookHWBufferInit), origFn, msg)) {
        g_origHWBufferInit = reinterpret_cast<HWBufferInit_t>(origFn);
        g_vbPoolPatchIdx = patches.size() - 1;
    }
}

static bool IsOurGlHookPointer(void* p) noexcept {
    return p == reinterpret_cast<void*>(&HookGlTexParameteri) ||
           p == reinterpret_cast<void*>(&HookGlShaderSource) ||
           p == reinterpret_cast<void*>(&HookGlAttachShader) ||
           p == reinterpret_cast<void*>(&HookGlLinkProgram) ||
           p == reinterpret_cast<void*>(&HookGlUseProgram) ||
           p == reinterpret_cast<void*>(&HookGlDeleteProgram) ||
           p == reinterpret_cast<void*>(&HookGlActiveTexture) ||
           p == reinterpret_cast<void*>(&HookGlBindTexture) ||
           p == reinterpret_cast<void*>(&HookGlDeleteTextures) ||
           p == reinterpret_cast<void*>(&HookGlBlendFunc) ||
           p == reinterpret_cast<void*>(&HookGlDepthMask) ||
           p == reinterpret_cast<void*>(&HookGlEnable) ||
           p == reinterpret_cast<void*>(&HookGlDisable) ||
           p == reinterpret_cast<void*>(&HookGlBindBuffer) ||
           p == reinterpret_cast<void*>(&HookGlDeleteBuffers) ||
           p == reinterpret_cast<void*>(&HookGlBlendEquation) ||
           p == reinterpret_cast<void*>(&HookGlColorMask) ||
           p == reinterpret_cast<void*>(&HookGlCullFace) ||
           p == reinterpret_cast<void*>(&HookGlDepthFunc) ||
           p == reinterpret_cast<void*>(&HookGlEnableVertexAttribArray) ||
           p == reinterpret_cast<void*>(&HookGlDisableVertexAttribArray) ||
           p == reinterpret_cast<void*>(&HookGlFrontFace) ||
           p == reinterpret_cast<void*>(&HookGlScissor) ||
           p == reinterpret_cast<void*>(&HookGlViewport) ||
           p == reinterpret_cast<void*>(&HookGlUniform1f) ||
           p == reinterpret_cast<void*>(&HookGlUniform1i) ||
           p == reinterpret_cast<void*>(&HookGlUniform2fv) ||
           p == reinterpret_cast<void*>(&HookGlUniform3fv) ||
           p == reinterpret_cast<void*>(&HookGlUniform4fv) ||
           p == reinterpret_cast<void*>(&HookGlUniformMatrix3fv) ||
           p == reinterpret_cast<void*>(&HookGlUniformMatrix4fv) ||
           p == reinterpret_cast<void*>(&HookGlBufferData) ||
           p == reinterpret_cast<void*>(&HookGlVertexAttribPointer) ||
           p == reinterpret_cast<void*>(&HookGlDrawArrays) ||
           p == reinterpret_cast<void*>(&HookGlDrawElements) ||
           p == reinterpret_cast<void*>(&HookGlBindFramebuffer) ||
           p == reinterpret_cast<void*>(&HookGlDeleteFramebuffers) ||
           p == reinterpret_cast<void*>(&HookGlGetIntegerv) ||
           p == reinterpret_cast<void*>(&HookGlClear) ||
           p == reinterpret_cast<void*>(&HookGlClearColor) ||
           p == reinterpret_cast<void*>(&HookGlReadPixels) ||
           p == reinterpret_cast<void*>(&HookGlFinish) ||
           p == reinterpret_cast<void*>(&HookGlStencilFunc) ||
           p == reinterpret_cast<void*>(&HookGlStencilMask) ||
           p == reinterpret_cast<void*>(&HookGlStencilOp) ||
           p == reinterpret_cast<void*>(&HookGlPolygonOffset) ||
           p == reinterpret_cast<void*>(&HookGlTexImage2D) ||
           p == reinterpret_cast<void*>(&HookGlCompressedTexImage2D);
}

static void ActivateNativeOpenGlOnFirstMakeCurrent() {
    void* procs[76]{};
    hs_native_gl::OptionalGlProcs opt{};
    const int nResolved = hs_native_gl::ResolveLiveGlesTable(g_wglRt, procs, opt);

    int directPatched = 0;
    int wrapperUpdated = 0;
    for (size_t i = 0; i < 76; ++i) {
        void* p = procs[i];
        if (!p) continue;
        const auto& imp = hs_native_gl::kDstGles2Imports[i];
        switch (imp.ordinal) {
            case   1: g_origGlActiveTexture              = reinterpret_cast<GlActiveTexture_t>(p); break;
            case   2: g_origGlAttachShader               = reinterpret_cast<GlAttachShader_t>(p); break;
            case   3: g_fnGlBindAttribLocation           = reinterpret_cast<GlBindAttribLocation_t>(p); break;
            case   4: g_origGlBindBuffer                 = reinterpret_cast<GlBindBuffer_t>(p); break;
            case   5: g_origGlBindFramebuffer            = reinterpret_cast<GlBindFramebuffer_t>(p); break;
            case   6: g_fnGlBindRenderbuffer             = reinterpret_cast<GlBindRenderbuffer_t>(p); break;
            case   7: g_origGlBindTexture                = reinterpret_cast<GlBindTexture_t>(p); break;
            case   9: g_origGlBlendEquation              = reinterpret_cast<GlBlendEquation_t>(p); break;
            case  11: g_origGlBlendFunc                  = reinterpret_cast<GlBlendFunc_t>(p); break;
            case  13:
                g_fnGlBufferData   = reinterpret_cast<GlBufferData_t>(p);
                g_origGlBufferData = g_fnGlBufferData;
                break;
            case  15: g_fnGlCheckFramebufferStatus       = reinterpret_cast<GlCheckFramebufferStatus_t>(p); break;
            case  16: g_origGlClear                      = reinterpret_cast<GlClear_t>(p); break;
            case  17: g_origGlClearColor                 = reinterpret_cast<GlClearColor_t>(p); break;
            case  20: g_origGlColorMask                  = reinterpret_cast<GlColorMask_t>(p); break;
            case  21: g_fnGlCompileShader                = reinterpret_cast<GlCompileShader_t>(p); break;
            case  22: g_origGlCompressedTexImage2D       = reinterpret_cast<GlCompressedTexImage2D_t>(p); break;
            case  26: g_fnGlCreateProgram                = reinterpret_cast<GlCreateProgram_t>(p); break;
            case  27: g_fnGlCreateShader                 = reinterpret_cast<GlCreateShader_t>(p); break;
            case  28: g_origGlCullFace                   = reinterpret_cast<GlCullFace_t>(p); break;
            case  29: g_origGlDeleteBuffers              = reinterpret_cast<GlDeleteBuffers_t>(p); break;
            case  30:
                g_fnGlDeleteFramebuffers   = reinterpret_cast<GlDeleteFramebuffers_t>(p);
                g_origGlDeleteFramebuffers = g_fnGlDeleteFramebuffers;
                break;
            case  31: g_origGlDeleteTextures             = reinterpret_cast<GlDeleteTextures_t>(p); break;
            case  32: g_origGlDeleteProgram              = reinterpret_cast<GlDeleteProgram_t>(p); break;
            case  33: g_fnGlDeleteRenderbuffers          = reinterpret_cast<GlDeleteRenderbuffers_t>(p); break;
            case  34: g_fnGlDeleteShader                 = reinterpret_cast<GlDeleteShader_t>(p); break;
            case  36: g_origGlDepthFunc                  = reinterpret_cast<GlDepthFunc_t>(p); break;
            case  37: g_origGlDepthMask                  = reinterpret_cast<GlDepthMask_t>(p); break;
            case  39: g_origGlDisable                    = reinterpret_cast<GlDisable_t>(p); break;
            case  40: g_origGlDisableVertexAttribArray   = reinterpret_cast<GlDisableVertexAttribArray_t>(p); break;
            case  41: g_origGlDrawArrays                 = reinterpret_cast<GlDrawArrays_t>(p); break;
            case  42: g_origGlDrawElements               = reinterpret_cast<GlDrawElements_t>(p); break;
            case  43: g_origGlEnable                     = reinterpret_cast<GlEnable_t>(p); break;
            case  44: g_origGlEnableVertexAttribArray    = reinterpret_cast<GlEnableVertexAttribArray_t>(p); break;
            case  45:
                g_fnGlFinishNative = reinterpret_cast<GlFinish_t>(p);
                g_origGlFinish     = g_fnGlFinishNative;
                break;
            case  47: g_fnGlFramebufferRenderbuffer      = reinterpret_cast<GlFramebufferRenderbuffer_t>(p); break;
            case  48: g_fnGlFramebufferTexture2D         = reinterpret_cast<GlFramebufferTexture2D_t>(p); break;
            case  49: g_origGlFrontFace                  = reinterpret_cast<GlFrontFace_t>(p); break;
            case  50: g_fnGlGenBuffers                   = reinterpret_cast<GlGenBuffers_t>(p); break;
            case  52: g_fnGlGenFramebuffers              = reinterpret_cast<GlGenFramebuffers_t>(p); break;
            case  53: g_fnGlGenRenderbuffers             = reinterpret_cast<GlGenRenderbuffers_t>(p); break;
            case  54: g_fnGlGenTextures                  = reinterpret_cast<GlGenTextures_t>(p); break;
            case  58: g_fnGlGetAttribLocation            = reinterpret_cast<GlGetAttribLocation_t>(p); break;
            case  61: g_fnGlGetError                     = reinterpret_cast<GlGetError_t>(p); break;
            case  64: g_fnGlGetIntegerv                  = reinterpret_cast<GlGetIntegerv_t>(p); break;
            case  65: g_fnGlGetProgramiv                 = reinterpret_cast<GlGetProgramiv_t>(p); break;
            case  66: g_fnGlGetProgramInfoLog            = reinterpret_cast<GlGetProgramInfoLog_t>(p); break;
            case  68: g_fnGlGetShaderiv                  = reinterpret_cast<GlGetShaderiv_t>(p); break;
            case  69: g_fnGlGetShaderInfoLog             = reinterpret_cast<GlGetShaderInfoLog_t>(p); break;
            case  72: g_fnGlGetString                    = reinterpret_cast<GlGetString_t>(p); break;
            case  77: g_fnGlGetUniformLocation           = reinterpret_cast<GlGetUniformLocation_t>(p); break;
            case  90: g_origGlLinkProgram                = reinterpret_cast<GlLinkProgram_t>(p); break;
            case  91: g_fnGlPixelStorei                  = reinterpret_cast<GlPixelStorei_t>(p); break;
            case  92: g_origGlPolygonOffset              = reinterpret_cast<GlPolygonOffset_t>(p); break;
            case  93: g_origGlReadPixels                 = reinterpret_cast<GlReadPixels_t>(p); break;
            case  95: g_fnGlRenderbufferStorage          = reinterpret_cast<GlRenderbufferStorage_t>(p); break;
            case  97: g_origGlScissor                    = reinterpret_cast<GlScissor_t>(p); break;
            case  99: g_origGlShaderSource               = reinterpret_cast<GlShaderSource_t>(p); break;
            case 100: g_origGlStencilFunc                = reinterpret_cast<GlStencilFunc_t>(p); break;
            case 102: g_origGlStencilMask                = reinterpret_cast<GlStencilMask_t>(p); break;
            case 104: g_origGlStencilOp                  = reinterpret_cast<GlStencilOp_t>(p); break;
            case 106: g_origGlTexImage2D                 = reinterpret_cast<GlTexImage2D_t>(p); break;
            case 109: g_origGlTexParameteri              = reinterpret_cast<GlTexParameteri_t>(p); break;
            case 112: g_origGlUniform1f                  = reinterpret_cast<GlUniform1f_t>(p); break;
            case 114: g_origGlUniform1i                  = reinterpret_cast<GlUniform1i_t>(p); break;
            case 117: g_origGlUniform2fv                 = reinterpret_cast<GlUniform2fv_t>(p); break;
            case 121: g_origGlUniform3fv                 = reinterpret_cast<GlUniform3fv_t>(p); break;
            case 125:
                g_origGlUniform4fv = reinterpret_cast<GlUniform4fv_t>(p);
                g_fnGlUniform4fv   = reinterpret_cast<GlUniform4fv_t>(p);
                break;
            case 129: g_origGlUniformMatrix3fv           = reinterpret_cast<GlUniformMatrix3fv_t>(p); break;
            case 130: g_origGlUniformMatrix4fv           = reinterpret_cast<GlUniformMatrix4fv_t>(p); break;
            case 131: g_origGlUseProgram                 = reinterpret_cast<GlUseProgram_t>(p); break;
            case 141: g_origGlVertexAttribPointer        = reinterpret_cast<GlVertexAttribPointer_t>(p); break;
            case 142: g_origGlViewport                   = reinterpret_cast<GlViewport_t>(p); break;
            default: break;
        }
        void** iatSlot = FindExeIatSlot("libGLESv2.dll", imp.ordinal, imp.name);
        if (iatSlot && *iatSlot) {
            if (IsOurGlHookPointer(*iatSlot)) {
                ++wrapperUpdated;
            } else {
                DWORD oldProt = 0;
                if (VirtualProtect(iatSlot, sizeof(void*), PAGE_READWRITE, &oldProt)) {
                    InterlockedExchangePointer(iatSlot, p);
                    DWORD dummy = 0;
                    VirtualProtect(iatSlot, sizeof(void*), oldProt, &dummy);
                    ++directPatched;
                }
            }
        }
    }

    if (opt.glTexParameterf) g_fnGlTexParameterf = reinterpret_cast<GlTexParameterf_t>(opt.glTexParameterf);
    if (opt.glGetFloatv)     g_fnGlGetFloatv     = reinterpret_cast<GlGetFloatv_t>(opt.glGetFloatv);
    if (opt.glBufferSubData) g_fnGlBufferSubData = reinterpret_cast<GlBufferSubData_t>(opt.glBufferSubData);
    if (opt.glMapBufferRange) g_fnGlMapBufferRange = reinterpret_cast<GlMapBufferRange_t>(opt.glMapBufferRange);
    if (opt.glUnmapBuffer)   g_fnGlUnmapBuffer   = reinterpret_cast<GlUnmapBuffer_t>(opt.glUnmapBuffer);
    if (opt.glGetBufferParameteriv) g_fnGlGetBufferParameteriv = reinterpret_cast<GlGetBufferParameteriv_t>(opt.glGetBufferParameteriv);
    if (opt.glGetBufferSubData)     g_fnGlGetBufferSubData     = reinterpret_cast<GlGetBufferSubData_t>(opt.glGetBufferSubData);
    if (opt.glBlendFuncSeparate)     g_fnGlBlendFuncSeparate     = reinterpret_cast<GlBlendFuncSeparate_t>(opt.glBlendFuncSeparate);
    if (opt.glBlendEquationSeparate) g_fnGlBlendEquationSeparate = reinterpret_cast<GlBlendEquationSeparate_t>(opt.glBlendEquationSeparate);
    if (opt.glBlitFramebuffer)       g_fnGlBlitFramebuffer       = reinterpret_cast<GlBlitFramebuffer_t>(opt.glBlitFramebuffer);
    if (opt.glFenceSync)             g_fnGlFenceSync             = reinterpret_cast<GlFenceSync_t>(opt.glFenceSync);
    if (opt.glClientWaitSync)        g_fnGlClientWaitSync        = reinterpret_cast<GlClientWaitSync_t>(opt.glClientWaitSync);
    if (opt.glDeleteSync)            g_fnGlDeleteSync            = reinterpret_cast<GlDeleteSync_t>(opt.glDeleteSync);
    if (opt.glFinish && !g_fnGlFinishNative) g_fnGlFinishNative = reinterpret_cast<GlFinish_t>(opt.glFinish);

    hs_vb_pool::GlBufferOps vbOps{};
    vbOps.glGenBuffers       = g_fnGlGenBuffers;
    vbOps.glBindBuffer       = g_origGlBindBuffer;
    vbOps.glBufferData       = g_fnGlBufferData;
    vbOps.glBufferSubData    = g_fnGlBufferSubData;
    vbOps.glDeleteBuffers    = g_origGlDeleteBuffers;
    vbOps.glMapBufferRange   = g_fnGlMapBufferRange;
    vbOps.glUnmapBuffer      = g_fnGlUnmapBuffer;
    vbOps.onBindBuffer       = &OnVbPoolBindBuffer;
    vbOps.onInvalidateBuffer = &OnVbPoolInvalidateBuffer;
    g_vbPool.SetOps(vbOps);

    g_sbOpsGen.fetch_add(1, std::memory_order_release);
    g_wsOpsGen.fetch_add(1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_release);
    g_afCapsProbeState.store(0, std::memory_order_release);
    g_hd2dIatHooked = g_optHd2dWorldShaders.load(std::memory_order_relaxed) &&
                      (g_fnGlGetUniformLocation && g_fnGlGetProgramiv && g_fnGlUniform4fv);
    LogMsg("[GL_NATIVE] Live GLES2 table bound (resolved=%d/76, directIAT=%d, wrappersUpdated=%d, mapRange=%d, lazyReadback=%d)",
           nResolved, directPatched, wrapperUpdated,
           (g_fnGlMapBufferRange && g_fnGlUnmapBuffer) ? 1 : 0,
           (g_fnGlGetBufferParameteriv && g_fnGlMapBufferRange && g_fnGlUnmapBuffer) ? 1 : 0);
}

static void InstallGlIatHooks() {
    HMODULE hGles = GetModuleHandleW(L"libGLESv2.dll");
    if (hGles) {
        if (!g_fnGlTexParameterf) g_fnGlTexParameterf = reinterpret_cast<GlTexParameterf_t>(GetProcAddress(hGles, "glTexParameterf"));
        if (!g_fnGlGetFloatv)     g_fnGlGetFloatv     = reinterpret_cast<GlGetFloatv_t>(GetProcAddress(hGles, "glGetFloatv"));
        if (!g_fnGlGetString)     g_fnGlGetString     = reinterpret_cast<GlGetString_t>(GetProcAddress(hGles, "glGetString"));
        g_fnGlBindAttribLocation = reinterpret_cast<GlBindAttribLocation_t>(GetProcAddress(hGles, "glBindAttribLocation"));
        g_fnGlGetAttribLocation  = reinterpret_cast<GlGetAttribLocation_t>(GetProcAddress(hGles, "glGetAttribLocation"));
        g_fnGlGetIntegerv        = reinterpret_cast<GlGetIntegerv_t>(GetProcAddress(hGles, "glGetIntegerv"));
        g_fnGlGetUniformLocation = reinterpret_cast<GlGetUniformLocation_t>(GetProcAddress(hGles, "glGetUniformLocation"));
        g_fnGlGetProgramiv       = reinterpret_cast<GlGetProgramiv_t>(GetProcAddress(hGles, "glGetProgramiv"));
        g_fnGlCreateShader       = reinterpret_cast<GlCreateShader_t>(GetProcAddress(hGles, "glCreateShader"));
        g_fnGlCompileShader      = reinterpret_cast<GlCompileShader_t>(GetProcAddress(hGles, "glCompileShader"));
        g_fnGlGetShaderiv        = reinterpret_cast<GlGetShaderiv_t>(GetProcAddress(hGles, "glGetShaderiv"));
        g_fnGlDeleteShader       = reinterpret_cast<GlDeleteShader_t>(GetProcAddress(hGles, "glDeleteShader"));
        g_fnGlCreateProgram      = reinterpret_cast<GlCreateProgram_t>(GetProcAddress(hGles, "glCreateProgram"));
        g_fnGlUniform4fv         = reinterpret_cast<GlUniform4fv_t>(GetProcAddress(hGles, "glUniform4fv"));
        g_fnGlGenBuffers         = reinterpret_cast<GlGenBuffers_t>(GetProcAddress(hGles, "glGenBuffers"));
        g_fnGlBufferData         = reinterpret_cast<GlBufferData_t>(GetProcAddress(hGles, "glBufferData"));
        g_fnGlBufferSubData      = reinterpret_cast<GlBufferSubData_t>(GetProcAddress(hGles, "glBufferSubData"));
        g_fnGlGetBufferParameteriv = reinterpret_cast<GlGetBufferParameteriv_t>(GetProcAddress(hGles, "glGetBufferParameteriv"));
        g_origGlBindBuffer       = reinterpret_cast<GlBindBuffer_t>(GetProcAddress(hGles, "glBindBuffer"));
        g_origGlDeleteBuffers    = reinterpret_cast<GlDeleteBuffers_t>(GetProcAddress(hGles, "glDeleteBuffers"));
        g_fnGlGenFramebuffers         = reinterpret_cast<GlGenFramebuffers_t>(GetProcAddress(hGles, "glGenFramebuffers"));
        g_fnGlDeleteFramebuffers      = reinterpret_cast<GlDeleteFramebuffers_t>(GetProcAddress(hGles, "glDeleteFramebuffers"));
        g_fnGlFramebufferTexture2D    = reinterpret_cast<GlFramebufferTexture2D_t>(GetProcAddress(hGles, "glFramebufferTexture2D"));
        g_fnGlCheckFramebufferStatus  = reinterpret_cast<GlCheckFramebufferStatus_t>(GetProcAddress(hGles, "glCheckFramebufferStatus"));
        g_fnGlGenRenderbuffers        = reinterpret_cast<GlGenRenderbuffers_t>(GetProcAddress(hGles, "glGenRenderbuffers"));
        g_fnGlDeleteRenderbuffers     = reinterpret_cast<GlDeleteRenderbuffers_t>(GetProcAddress(hGles, "glDeleteRenderbuffers"));
        g_fnGlBindRenderbuffer        = reinterpret_cast<GlBindRenderbuffer_t>(GetProcAddress(hGles, "glBindRenderbuffer"));
        g_fnGlRenderbufferStorage     = reinterpret_cast<GlRenderbufferStorage_t>(GetProcAddress(hGles, "glRenderbufferStorage"));
        g_fnGlFramebufferRenderbuffer = reinterpret_cast<GlFramebufferRenderbuffer_t>(GetProcAddress(hGles, "glFramebufferRenderbuffer"));
        g_fnGlGenTextures             = reinterpret_cast<GlGenTextures_t>(GetProcAddress(hGles, "glGenTextures"));
        g_origGlClearColor            = reinterpret_cast<GlClearColor_t>(GetProcAddress(hGles, "glClearColor"));
        g_fnGlGetError                = reinterpret_cast<GlGetError_t>(GetProcAddress(hGles, "glGetError"));
        g_fnGlGetShaderInfoLog        = reinterpret_cast<GlGetShaderInfoLog_t>(GetProcAddress(hGles, "glGetShaderInfoLog"));
        g_fnGlGetProgramInfoLog       = reinterpret_cast<GlGetProgramInfoLog_t>(GetProcAddress(hGles, "glGetProgramInfoLog"));
        g_fnGlBlendFuncSeparate       = reinterpret_cast<GlBlendFuncSeparate_t>(GetProcAddress(hGles, "glBlendFuncSeparate"));
        g_fnGlBlendEquationSeparate   = reinterpret_cast<GlBlendEquationSeparate_t>(GetProcAddress(hGles, "glBlendEquationSeparate"));
        g_fnGlPixelStorei             = reinterpret_cast<GlPixelStorei_t>(GetProcAddress(hGles, "glPixelStorei"));
    }

    if (PatchExeIatEntry("libGLESv2.dll", 109, "glTexParameteri", reinterpret_cast<void*>(&HookGlTexParameteri), g_origGlTexParameteri)) {
        g_afIatHooked = true;
        LogMsg("[VISUAL] Hooked libGLESv2!glTexParameteri (@109) IAT -> 16x AF + Trilinear (target=%dx)",
               g_visualAnisoLevel.load(std::memory_order_relaxed));
    } else {
        LogMsg("[VISUAL] glTexParameteri (@109) IAT hook skipped (slot not found)");
    }

    if (g_optVbPool.load(std::memory_order_relaxed)) {
        PatchExeIatEntry("libGLESv2.dll", 29, "glDeleteBuffers", reinterpret_cast<void*>(&HookGlDeleteBuffers), g_origGlDeleteBuffers);
        hs_vb_pool::GlBufferOps vbOps{};
        vbOps.glGenBuffers       = g_fnGlGenBuffers;
        vbOps.glBindBuffer       = g_origGlBindBuffer;
        vbOps.glBufferData       = g_fnGlBufferData;
        vbOps.glBufferSubData    = g_fnGlBufferSubData;
        vbOps.glDeleteBuffers    = g_origGlDeleteBuffers;
        vbOps.onBindBuffer       = &OnVbPoolBindBuffer;
        vbOps.onInvalidateBuffer = &OnVbPoolInvalidateBuffer;
        g_vbPool.SetOps(vbOps);
    }

    const bool wantSpriteBatch = g_optSpriteBatch.load(std::memory_order_relaxed);
    const bool wantNativeGl    = (g_cfgRenderBackend.load(std::memory_order_relaxed) != static_cast<int>(hs_native_gl::BackendMode::D3D9));
    bool okSrc = false, okLnk = false, okUse = false, okDel = false;
    if (g_optHd2dWorldShaders.load(std::memory_order_relaxed) ||
        g_optGlStateCache.load(std::memory_order_relaxed) ||
        wantSpriteBatch || wantNativeGl) {
        okSrc = (g_optHd2dWorldShaders.load(std::memory_order_relaxed) || wantSpriteBatch || wantNativeGl)
            ? PatchExeIatEntry("libGLESv2.dll", 99,  "glShaderSource",  reinterpret_cast<void*>(&HookGlShaderSource),  g_origGlShaderSource)
            : false;
        if (wantSpriteBatch || g_optHd2dWorldShaders.load(std::memory_order_relaxed)) {
            PatchExeIatEntry("libGLESv2.dll", 2, "glAttachShader", reinterpret_cast<void*>(&HookGlAttachShader), g_origGlAttachShader);
        }
        okLnk = PatchExeIatEntry("libGLESv2.dll", 90,  "glLinkProgram",   reinterpret_cast<void*>(&HookGlLinkProgram),   g_origGlLinkProgram);
        okUse = PatchExeIatEntry("libGLESv2.dll", 131, "glUseProgram",    reinterpret_cast<void*>(&HookGlUseProgram),    g_origGlUseProgram);
        okDel = PatchExeIatEntry("libGLESv2.dll", 32,  "glDeleteProgram", reinterpret_cast<void*>(&HookGlDeleteProgram), g_origGlDeleteProgram);
        g_hd2dIatHooked = (g_optHd2dWorldShaders.load(std::memory_order_relaxed) &&
                           okSrc && okLnk && okUse && okDel && g_fnGlGetUniformLocation && g_fnGlGetProgramiv && g_fnGlUniform4fv);
        LogMsg("[VISUAL][HD2D] libGLESv2 IAT hooks (src=%d lnk=%d use=%d del=%d loc=%d piv=%d uni=%d) -> active=%d",
               okSrc ? 1 : 0, okLnk ? 1 : 0, okUse ? 1 : 0, okDel ? 1 : 0,
               g_fnGlGetUniformLocation ? 1 : 0, g_fnGlGetProgramiv ? 1 : 0, g_fnGlUniform4fv ? 1 : 0,
               g_hd2dIatHooked ? 1 : 0);
    }

    // Complete 12/12 libEGL IAT hooks (enables Native WGL ES2 backend switch + SwapBuffers telemetry + State-Cache resets)
    {
        bool okErr = PatchExeIatEntry("libEGL.dll",  1, "eglGetError",             reinterpret_cast<void*>(&HookEglGetError),             g_origEglGetError);
        bool okGd  = PatchExeIatEntry("libEGL.dll",  2, "eglGetDisplay",           reinterpret_cast<void*>(&HookEglGetDisplay),           g_origEglGetDisplay);
        bool okIni = PatchExeIatEntry("libEGL.dll",  3, "eglInitialize",           reinterpret_cast<void*>(&HookEglInitialize),           g_origEglInitialize);
        bool okGc  = PatchExeIatEntry("libEGL.dll",  6, "eglGetConfigs",           reinterpret_cast<void*>(&HookEglGetConfigs),           g_origEglGetConfigs);
        bool okCc  = PatchExeIatEntry("libEGL.dll",  7, "eglChooseConfig",         reinterpret_cast<void*>(&HookEglChooseConfig),         g_origEglChooseConfig);
        bool okGa  = PatchExeIatEntry("libEGL.dll",  8, "eglGetConfigAttrib",      reinterpret_cast<void*>(&HookEglGetConfigAttrib),      g_origEglGetConfigAttrib);
        bool okCws = PatchExeIatEntry("libEGL.dll",  9, "eglCreateWindowSurface",  reinterpret_cast<void*>(&HookEglCreateWindowSurface),  g_origEglCreateWindowSurface);
        bool okSi  = PatchExeIatEntry("libEGL.dll", 22, "eglSwapInterval",         reinterpret_cast<void*>(&HookEglSwapInterval),         g_origEglSwapInterval);
        bool okCctx= PatchExeIatEntry("libEGL.dll", 23, "eglCreateContext",        reinterpret_cast<void*>(&HookEglCreateContext),        g_origEglCreateContext);
        bool okMc  = PatchExeIatEntry("libEGL.dll", 25, "eglMakeCurrent",          reinterpret_cast<void*>(&HookEglMakeCurrent),          g_origEglMakeCurrent);
        bool okSw  = PatchExeIatEntry("libEGL.dll", 32, "eglSwapBuffers",          reinterpret_cast<void*>(&HookEglSwapBuffers),          g_origEglSwapBuffers);
        bool okGpa = PatchExeIatEntry("libEGL.dll", 34, "eglGetProcAddress",       reinterpret_cast<void*>(&HookEglGetProcAddress),       g_origEglGetProcAddress);
        g_eglSwapIatHooked = okSw;
        const int eglHookedCount = (okErr?1:0)+(okGd?1:0)+(okIni?1:0)+(okGc?1:0)+(okCc?1:0)+(okGa?1:0)+
                                   (okCws?1:0)+(okSi?1:0)+(okCctx?1:0)+(okMc?1:0)+(okSw?1:0)+(okGpa?1:0);
        LogMsg("[PERF][EGL] libEGL IAT hooks (%d/12: getDpy=%d makeCurrent=%d swapBuffers=%d)",
               eglHookedCount, okGd ? 1 : 0, okMc ? 1 : 0, okSw ? 1 : 0);

        // ANGLE 1.0 (D3D9) Redundant GL State-Mirror & Uniform Shadow Cache Deduplication
        // (Also provides the state-change & uniform interceptors required by the 2D Sprite Batcher!)
        const bool wantWorldShadow = g_optHd2dWorldShaders.load(std::memory_order_relaxed);
        bool okCoreState = false, okBb = false, okDb = false, okEv = false, okDv = false, okUni = false;
        if ((g_optGlStateCache.load(std::memory_order_relaxed) || wantSpriteBatch || wantWorldShadow) && okMc && okLnk && okUse && okDel) {
            bool okAt = PatchExeIatEntry("libGLESv2.dll",  1, "glActiveTexture",  reinterpret_cast<void*>(&HookGlActiveTexture),  g_origGlActiveTexture);
            bool okBt = PatchExeIatEntry("libGLESv2.dll",  7, "glBindTexture",    reinterpret_cast<void*>(&HookGlBindTexture),    g_origGlBindTexture);
            bool okDt = PatchExeIatEntry("libGLESv2.dll", 31, "glDeleteTextures", reinterpret_cast<void*>(&HookGlDeleteTextures), g_origGlDeleteTextures);
            bool okBf = PatchExeIatEntry("libGLESv2.dll", 11, "glBlendFunc",      reinterpret_cast<void*>(&HookGlBlendFunc),      g_origGlBlendFunc);
            bool okDm = PatchExeIatEntry("libGLESv2.dll", 37, "glDepthMask",      reinterpret_cast<void*>(&HookGlDepthMask),      g_origGlDepthMask);
            bool okEn = PatchExeIatEntry("libGLESv2.dll", 43, "glEnable",         reinterpret_cast<void*>(&HookGlEnable),         g_origGlEnable);
            bool okDi = PatchExeIatEntry("libGLESv2.dll", 39, "glDisable",        reinterpret_cast<void*>(&HookGlDisable),        g_origGlDisable);
            okCoreState = (okMc && okLnk && okUse && okDel &&
                           okAt && okBt && okDt && okBf && okDm && okEn && okDi);
            if (g_optGlStateCache.load(std::memory_order_relaxed)) {
                g_glCacheIatHooked = okCoreState;
                g_glCacheActive.store(g_glCacheIatHooked, std::memory_order_release);
                if (!g_glCacheIatHooked) {
                    g_optGlStateCache.store(false, std::memory_order_release);
                }
            }
            if (okCoreState) {
                // Extended buffer / vertex-attrib / raster state deduplication (fail-closed pairs)
                if (FindExeIatSlot("libGLESv2.dll", 4, "glBindBuffer") &&
                    FindExeIatSlot("libGLESv2.dll", 29, "glDeleteBuffers")) {
                    okBb = PatchExeIatEntry("libGLESv2.dll",  4, "glBindBuffer",    reinterpret_cast<void*>(&HookGlBindBuffer),    g_origGlBindBuffer);
                    okDb = PatchExeIatEntry("libGLESv2.dll", 29, "glDeleteBuffers", reinterpret_cast<void*>(&HookGlDeleteBuffers), g_origGlDeleteBuffers);
                }
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
                bool okSf = PatchExeIatEntry("libGLESv2.dll", 100,"glStencilFunc",   reinterpret_cast<void*>(&HookGlStencilFunc),   g_origGlStencilFunc);
                bool okSm = PatchExeIatEntry("libGLESv2.dll", 102,"glStencilMask",   reinterpret_cast<void*>(&HookGlStencilMask),   g_origGlStencilMask);
                bool okSo = PatchExeIatEntry("libGLESv2.dll", 104,"glStencilOp",     reinterpret_cast<void*>(&HookGlStencilOp),     g_origGlStencilOp);
                bool okPo = PatchExeIatEntry("libGLESv2.dll",  92,"glPolygonOffset", reinterpret_cast<void*>(&HookGlPolygonOffset), g_origGlPolygonOffset);

                if (g_optGlUniformCache.load(std::memory_order_relaxed) || wantSpriteBatch || wantWorldShadow) {
                    bool u1f = PatchExeIatEntry("libGLESv2.dll", 112, "glUniform1f",        reinterpret_cast<void*>(&HookGlUniform1f),        g_origGlUniform1f);
                    bool u1i = PatchExeIatEntry("libGLESv2.dll", 114, "glUniform1i",        reinterpret_cast<void*>(&HookGlUniform1i),        g_origGlUniform1i);
                    bool u2  = PatchExeIatEntry("libGLESv2.dll", 117, "glUniform2fv",       reinterpret_cast<void*>(&HookGlUniform2fv),       g_origGlUniform2fv);
                    bool u3  = PatchExeIatEntry("libGLESv2.dll", 121, "glUniform3fv",       reinterpret_cast<void*>(&HookGlUniform3fv),       g_origGlUniform3fv);
                    bool u4  = PatchExeIatEntry("libGLESv2.dll", 125, "glUniform4fv",       reinterpret_cast<void*>(&HookGlUniform4fv),       g_origGlUniform4fv);
                    bool um3 = PatchExeIatEntry("libGLESv2.dll", 129, "glUniformMatrix3fv", reinterpret_cast<void*>(&HookGlUniformMatrix3fv), g_origGlUniformMatrix3fv);
                    bool um4 = PatchExeIatEntry("libGLESv2.dll", 130, "glUniformMatrix4fv", reinterpret_cast<void*>(&HookGlUniformMatrix4fv), g_origGlUniformMatrix4fv);
                    okUni = (u1f && u1i && u2 && u3 && u4 && um3 && um4);
                    if (g_optGlUniformCache.load(std::memory_order_relaxed)) {
                        g_glUniformCacheActive.store(okUni, std::memory_order_release);
                        if (!okUni) g_optGlUniformCache.store(false, std::memory_order_release);
                    }
                }
                LogMsg("[PERF][GLCACHE+] Extended state(buf=%d attr=%d eq=%d cm=%d cf=%d df=%d ff=%d sc=%d vp=%d sn=%d po=%d) | UniformShadow=%d",
                       (okBb && okDb)?1:0, (okEv && okDv)?1:0, okBe?1:0, okCm?1:0, okCf?1:0, okDf?1:0, okFf?1:0, okSc?1:0, okVp?1:0,
                       (okSf && okSm && okSo)?1:0, okPo?1:0, okUni?1:0);
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

        // Multi-Texture 2D Sprite Draw-Call Batcher + World-Space ShadowMask FBO hooks
        // Fail-closed: ALL geometry/draw/barrier hooks + core state/uniform/shader hooks MUST succeed!
        if ((wantSpriteBatch || wantWorldShadow) && okSrc && okCoreState && okBb && okDb && okEv && okDv && okUni && okSw &&
            g_fnGlBindAttribLocation && g_fnGlGetAttribLocation && g_fnGlGetUniformLocation && g_fnGlGenBuffers) {
            bool okBd = PatchExeIatEntry("libGLESv2.dll",  13, "glBufferData",          reinterpret_cast<void*>(&HookGlBufferData),          g_origGlBufferData);
            bool okVp = PatchExeIatEntry("libGLESv2.dll", 141, "glVertexAttribPointer", reinterpret_cast<void*>(&HookGlVertexAttribPointer), g_origGlVertexAttribPointer);
            bool okDa = PatchExeIatEntry("libGLESv2.dll",  41, "glDrawArrays",          reinterpret_cast<void*>(&HookGlDrawArrays),          g_origGlDrawArrays);
            bool okDe = PatchExeIatEntry("libGLESv2.dll",  42, "glDrawElements",        reinterpret_cast<void*>(&HookGlDrawElements),        g_origGlDrawElements);
            bool okFb = PatchExeIatEntry("libGLESv2.dll",   5, "glBindFramebuffer",     reinterpret_cast<void*>(&HookGlBindFramebuffer),     g_origGlBindFramebuffer);
            PatchExeIatEntry("libGLESv2.dll",  30, "glDeleteFramebuffers",  reinterpret_cast<void*>(&HookGlDeleteFramebuffers),  g_origGlDeleteFramebuffers);
            bool okCl = PatchExeIatEntry("libGLESv2.dll",  16, "glClear",               reinterpret_cast<void*>(&HookGlClear),               g_origGlClear);
            PatchExeIatEntry("libGLESv2.dll",  17, "glClearColor",          reinterpret_cast<void*>(&HookGlClearColor),          g_origGlClearColor);
            bool okRp = PatchExeIatEntry("libGLESv2.dll",  93, "glReadPixels",          reinterpret_cast<void*>(&HookGlReadPixels),          g_origGlReadPixels);
            bool okFi = PatchExeIatEntry("libGLESv2.dll",  45, "glFinish",              reinterpret_cast<void*>(&HookGlFinish),              g_origGlFinish);
            PatchExeIatEntry("libGLESv2.dll", 100, "glStencilFunc",          reinterpret_cast<void*>(&HookGlStencilFunc),          g_origGlStencilFunc);
            PatchExeIatEntry("libGLESv2.dll", 102, "glStencilMask",          reinterpret_cast<void*>(&HookGlStencilMask),          g_origGlStencilMask);
            PatchExeIatEntry("libGLESv2.dll", 104, "glStencilOp",            reinterpret_cast<void*>(&HookGlStencilOp),            g_origGlStencilOp);
            PatchExeIatEntry("libGLESv2.dll",  92, "glPolygonOffset",        reinterpret_cast<void*>(&HookGlPolygonOffset),        g_origGlPolygonOffset);
            PatchExeIatEntry("libGLESv2.dll", 106, "glTexImage2D",           reinterpret_cast<void*>(&HookGlTexImage2D),           g_origGlTexImage2D);
            PatchExeIatEntry("libGLESv2.dll",  22, "glCompressedTexImage2D", reinterpret_cast<void*>(&HookGlCompressedTexImage2D), g_origGlCompressedTexImage2D);
            const bool sbReady = wantSpriteBatch && (okBd && okVp && okDa && okDe && okFb && okCl && okRp && okFi);
            g_spriteBatchActive.store(sbReady, std::memory_order_release);
            g_sbOpsGen.fetch_add(1, std::memory_order_release);
            g_wsOpsGen.fetch_add(1, std::memory_order_release);
            if (wantSpriteBatch && !sbReady) {
                g_optSpriteBatch.store(false, std::memory_order_release);
            }
            LogMsg("[PERF][SBATCH] Multi-Texture 2D Sprite Batcher IAT hooks (bd=%d vp=%d da=%d de=%d fb=%d cl=%d rp=%d fi=%d) -> active=%d",
                   okBd?1:0, okVp?1:0, okDa?1:0, okDe?1:0, okFb?1:0, okCl?1:0, okRp?1:0, okFi?1:0, sbReady?1:0);
        } else if (wantSpriteBatch) {
            g_spriteBatchActive.store(false, std::memory_order_release);
            g_optSpriteBatch.store(false, std::memory_order_release);
            LogMsg("[PERF][SBATCH] Disabled fail-closed (missing prereqs: src=%d core=%d buf=%d attr=%d uni=%d sw=%d)",
                   okSrc?1:0, okCoreState?1:0, (okBb&&okDb)?1:0, (okEv&&okDv)?1:0, okUni?1:0, okSw?1:0);
        }

        // Ensure FBO 0 redirection (glBindFramebuffer, glClear, glGetIntegerv) is ALWAYS active for DXGI Flip-Model
        // even if both SpriteBatch and HD2D World Shaders are disabled by user INI!
        if (wantNativeGl) {
            if (!g_origGlBindFramebuffer) {
                PatchExeIatEntry("libGLESv2.dll", 5, "glBindFramebuffer", reinterpret_cast<void*>(&HookGlBindFramebuffer), g_origGlBindFramebuffer);
            }
            if (!g_origGlDeleteFramebuffers) {
                PatchExeIatEntry("libGLESv2.dll", 30, "glDeleteFramebuffers", reinterpret_cast<void*>(&HookGlDeleteFramebuffers), g_origGlDeleteFramebuffers);
            }
            if (!g_origGlClear) {
                PatchExeIatEntry("libGLESv2.dll", 16, "glClear", reinterpret_cast<void*>(&HookGlClear), g_origGlClear);
            }
            PatchExeIatEntry("libGLESv2.dll", 64, "glGetIntegerv", reinterpret_cast<void*>(&HookGlGetIntegerv), g_fnGlGetIntegerv);
        }
    }
}

static DWORD WINAPI HookFsGetFileAttributesW(LPCWSTR lpFileName) {
    if (!g_origGetFileAttributesW) return INVALID_FILE_ATTRIBUTES;
    GetAsyncSave().FlushPendingForPath(lpFileName);
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

static bool ModeHasWriteIntentA(const char* mode) {
    if (!mode) return true;
    for (const char* p = mode; *p; ++p) {
        if (*p == 'w' || *p == 'a' || *p == '+') return true;
    }
    return false;
}

static bool ModeHasWriteIntentW(const wchar_t* mode) {
    if (!mode) return true;
    for (const wchar_t* p = mode; *p; ++p) {
        if (*p == L'w' || *p == L'a' || *p == L'+') return true;
    }
    return false;
}

static void SetMsvcrErrno(int e) {
    if (g_msvcr90Errno) {
        if (int* ep = g_msvcr90Errno()) *ep = e;
    } else {
        errno = e;
    }
}

static bool IsMsvcrEnoent(DWORD lastErr) {
    if (g_msvcr90Errno) {
        int* ep = g_msvcr90Errno();
        if (ep) return *ep == ENOENT;
    }
    return lastErr == ERROR_FILE_NOT_FOUND || lastErr == ERROR_PATH_NOT_FOUND;
}

static BOOL WINAPI HookFsMoveFileExW(LPCWSTR a, LPCWSTR b, DWORD f) {
    GetAsyncSave().FlushPendingForPath(a);
    GetAsyncSave().FlushPendingForPath(b);
    g_fsNegCache.InvalidateAll();
    return g_origMoveFileExW ? g_origMoveFileExW(a, b, f) : FALSE;
}

static BOOL WINAPI HookFsCopyFileW(LPCWSTR a, LPCWSTR b, BOOL f) {
    GetAsyncSave().FlushPendingForPath(a);
    GetAsyncSave().FlushPendingForPath(b);
    g_fsNegCache.InvalidateAll();
    return g_origCopyFileW ? g_origCopyFileW(a, b, f) : FALSE;
}

static BOOL WINAPI HookFsDeleteFileW(LPCWSTR a) {
    GetAsyncSave().FlushPendingForPath(a);
    g_fsNegCache.InvalidateAll();
    return g_origDeleteFileW ? g_origDeleteFileW(a) : FALSE;
}

static BOOL WINAPI HookFsCreateDirectoryW(LPCWSTR a, LPSECURITY_ATTRIBUTES sa) {
    GetAsyncSave().FlushPendingForPath(a);
    g_fsNegCache.InvalidateAll();
    return g_origCreateDirectoryW ? g_origCreateDirectoryW(a, sa) : FALSE;
}

static BOOL WINAPI HookFsRemoveDirectoryW(LPCWSTR a) {
    GetAsyncSave().FlushPendingForDirPattern(a);
    g_fsNegCache.InvalidateAll();
    return g_origRemoveDirectoryW ? g_origRemoveDirectoryW(a) : FALSE;
}

static HANDLE WINAPI HookFsFindFirstFileW(LPCWSTR lpFileName, LPWIN32_FIND_DATAW lpFindFileData) {
    GetAsyncSave().FlushPendingForDirPattern(lpFileName);
    return g_origFindFirstFileW ? g_origFindFirstFileW(lpFileName, lpFindFileData) : INVALID_HANDLE_VALUE;
}

static BOOL WINAPI HookFsSetCurrentDirectoryW(LPCWSTR a) {
    GetAsyncSave().FlushAll();
    g_fsNegCache.InvalidateAll();
    return g_origSetCurrentDirectoryW ? g_origSetCurrentDirectoryW(a) : FALSE;
}

static HANDLE WINAPI HookFsCreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                       LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                       DWORD dwFlagsAndAttributes, HANDLE hTemplateFile) {
    GetAsyncSave().FlushPendingForPath(lpFileName);
    if ((dwDesiredAccess & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE)) != 0 ||
        dwCreationDisposition != OPEN_EXISTING) {
        g_fsNegCache.InvalidateAll();
    }
    return g_origCreateFileW ? g_origCreateFileW(lpFileName, dwDesiredAccess, dwShareMode,
                                                  lpSecurityAttributes, dwCreationDisposition,
                                                  dwFlagsAndAttributes, hTemplateFile)
                             : INVALID_HANDLE_VALUE;
}

static int __cdecl HookFsOpenA(const char* path, int oflag, int pmode) {
    GetAsyncSave().FlushPendingForAnsiPath(path);
    if ((oflag & 0x0303) != 0) {
        g_fsNegCache.InvalidateAll();
    }
    return g_origMsvcrOpen ? g_origMsvcrOpen(path, oflag, pmode) : -1;
}

static FILE* __cdecl HookFsFopen(const char* path, const char* mode) {
    if (!g_origMsvcrFopen) return nullptr;
    GetAsyncSave().FlushPendingForAnsiPath(path);
    if (ModeHasWriteIntentA(mode)) {
        g_fsNegCache.InvalidateAll();
        return g_origMsvcrFopen(path, mode);
    }
    if (g_fsNegCacheActive.load(std::memory_order_acquire) &&
        g_optFsNegCache.load(std::memory_order_relaxed) &&
        mode && (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0)) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPathA(path, &h1, &h2)) {
            InterlockedIncrement64(&g_fsNegQueries);
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (g_fsNegCache.Lookup(h1, h2, now)) {
                InterlockedIncrement64(&g_fsNegHits);
                SetMsvcrErrno(ENOENT);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return nullptr;
            }
            FILE* f = g_origMsvcrFopen(path, mode);
            if (!f) {
                const DWORD err = GetLastError();
                if (IsMsvcrEnoent(err)) {
                    g_fsNegCache.Insert(h1, h2, now, static_cast<uint64_t>(g_qpcFreq) * 15ULL);
                }
            }
            return f;
        }
    }
    return g_origMsvcrFopen(path, mode);
}

static FILE* __cdecl HookFsWfopen(const wchar_t* path, const wchar_t* mode) {
    if (!g_origMsvcrWfopen) return nullptr;
    if (tl_asyncCapture.active && path && mode && wcscmp(mode, L"wb") == 0) {
        const void* capData = tl_asyncCapture.data;
        const uint32_t capSize = tl_asyncCapture.size;
        tl_asyncCapture.active = false;
        if (CanCaptureAsyncWfopen(path) && capData && capSize > 0) {
            DWORD winErr = 0;
            // Synchronous verified atomic commit (per-path mutex + unique temp file + FlushFileBuffers + MoveFileExW):
            // NEVER fabricate success before disk durability is verified!
            if (GetAsyncSave().CommitSyncAtomic(path, capData, capSize, &winErr)) {
                g_fsNegCache.InvalidateAll();
                return kAsyncSentinelFile;
            }
            g_asyncSaveFallbacks.fetch_add(1, std::memory_order_relaxed);
            const bool isLockErr = (winErr == ERROR_SHARING_VIOLATION ||
                                    winErr == ERROR_ACCESS_DENIED ||
                                    winErr == ERROR_LOCK_VIOLATION);
            SetMsvcrErrno(isLockErr ? EACCES : EIO);
            SetLastError(winErr ? winErr : ERROR_WRITE_FAULT);
            return nullptr;
        }
        g_asyncSaveFallbacks.fetch_add(1, std::memory_order_relaxed);
    }
    GetAsyncSave().FlushPendingForPath(path);
    if (ModeHasWriteIntentW(mode)) {
        g_fsNegCache.InvalidateAll();
        return g_origMsvcrWfopen(path, mode);
    }
    if (g_fsNegCacheActive.load(std::memory_order_acquire) &&
        g_optFsNegCache.load(std::memory_order_relaxed) &&
        mode && (wcscmp(mode, L"r") == 0 || wcscmp(mode, L"rb") == 0)) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPath(path, &h1, &h2)) {
            InterlockedIncrement64(&g_fsNegQueries);
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (g_fsNegCache.Lookup(h1, h2, now)) {
                InterlockedIncrement64(&g_fsNegHits);
                SetMsvcrErrno(ENOENT);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return nullptr;
            }
            FILE* f = g_origMsvcrWfopen(path, mode);
            if (!f) {
                const DWORD err = GetLastError();
                if (IsMsvcrEnoent(err)) {
                    g_fsNegCache.Insert(h1, h2, now, static_cast<uint64_t>(g_qpcFreq) * 15ULL);
                }
            }
            return f;
        }
    }
    return g_origMsvcrWfopen(path, mode);
}

static size_t __cdecl HookFsFwrite(const void* buf, size_t size, size_t count, FILE* stream) {
    if (stream == kAsyncSentinelFile) {
        return count;
    }
    return g_origMsvcrFwrite ? g_origMsvcrFwrite(buf, size, count, stream) : 0;
}

static int __cdecl HookFsFclose(FILE* stream) {
    if (stream == kAsyncSentinelFile) {
        return 0;
    }
    return g_origMsvcrFclose ? g_origMsvcrFclose(stream) : EOF;
}

static int __cdecl HookFsWfopenS(FILE** pFile, const wchar_t* path, const wchar_t* mode) {
    if (!g_origMsvcrWfopenS) return EINVAL;
    GetAsyncSave().FlushPendingForPath(path);
    if (ModeHasWriteIntentW(mode)) {
        g_fsNegCache.InvalidateAll();
        return g_origMsvcrWfopenS(pFile, path, mode);
    }
    if (g_fsNegCacheActive.load(std::memory_order_acquire) &&
        g_optFsNegCache.load(std::memory_order_relaxed) &&
        pFile && mode && (wcscmp(mode, L"r") == 0 || wcscmp(mode, L"rb") == 0)) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPath(path, &h1, &h2)) {
            InterlockedIncrement64(&g_fsNegQueries);
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (g_fsNegCache.Lookup(h1, h2, now)) {
                *pFile = nullptr;
                InterlockedIncrement64(&g_fsNegHits);
                SetMsvcrErrno(ENOENT);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return ENOENT;
            }
            const int rc = g_origMsvcrWfopenS(pFile, path, mode);
            if (rc == ENOENT) {
                g_fsNegCache.Insert(h1, h2, now, static_cast<uint64_t>(g_qpcFreq) * 15ULL);
            }
            return rc;
        }
    }
    return g_origMsvcrWfopenS(pFile, path, mode);
}

static int __cdecl HookFsStat64(const char* path, void* buf) {
    if (!g_origMsvcrStat64) return -1;
    GetAsyncSave().FlushPendingForAnsiPath(path);
    if (g_fsNegCacheActive.load(std::memory_order_acquire) &&
        g_optFsNegCache.load(std::memory_order_relaxed)) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPathA(path, &h1, &h2)) {
            InterlockedIncrement64(&g_fsNegQueries);
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (g_fsNegCache.Lookup(h1, h2, now)) {
                InterlockedIncrement64(&g_fsNegHits);
                SetMsvcrErrno(ENOENT);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return -1;
            }
            const int rc = g_origMsvcrStat64(path, buf);
            if (rc == -1 && IsMsvcrEnoent(GetLastError())) {
                g_fsNegCache.Insert(h1, h2, now, static_cast<uint64_t>(g_qpcFreq) * 15ULL);
            }
            return rc;
        }
    }
    return g_origMsvcrStat64(path, buf);
}

static int __cdecl HookFsStat64i32(const char* path, void* buf) {
    if (!g_origMsvcrStat64i32) return -1;
    GetAsyncSave().FlushPendingForAnsiPath(path);
    if (g_fsNegCacheActive.load(std::memory_order_acquire) &&
        g_optFsNegCache.load(std::memory_order_relaxed)) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPathA(path, &h1, &h2)) {
            InterlockedIncrement64(&g_fsNegQueries);
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (g_fsNegCache.Lookup(h1, h2, now)) {
                InterlockedIncrement64(&g_fsNegHits);
                SetMsvcrErrno(ENOENT);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return -1;
            }
            const int rc = g_origMsvcrStat64i32(path, buf);
            if (rc == -1 && IsMsvcrEnoent(GetLastError())) {
                g_fsNegCache.Insert(h1, h2, now, static_cast<uint64_t>(g_qpcFreq) * 15ULL);
            }
            return rc;
        }
    }
    return g_origMsvcrStat64i32(path, buf);
}

static int __cdecl HookFsWstat64i32(const wchar_t* path, void* buf) {
    if (!g_origMsvcrWstat64i32) return -1;
    GetAsyncSave().FlushPendingForPath(path);
    if (g_fsNegCacheActive.load(std::memory_order_acquire) &&
        g_optFsNegCache.load(std::memory_order_relaxed)) {
        uint64_t h1 = 0, h2 = 0;
        if (hs_fs_neg::IsCacheableReadOnlyAssetPath(path, &h1, &h2)) {
            InterlockedIncrement64(&g_fsNegQueries);
            const uint64_t now = static_cast<uint64_t>(NowQpc());
            if (g_fsNegCache.Lookup(h1, h2, now)) {
                InterlockedIncrement64(&g_fsNegHits);
                SetMsvcrErrno(ENOENT);
                SetLastError(ERROR_FILE_NOT_FOUND);
                return -1;
            }
            const int rc = g_origMsvcrWstat64i32(path, buf);
            if (rc == -1 && IsMsvcrEnoent(GetLastError())) {
                g_fsNegCache.Insert(h1, h2, now, static_cast<uint64_t>(g_qpcFreq) * 15ULL);
            }
            return rc;
        }
    }
    return g_origMsvcrWstat64i32(path, buf);
}

static void FlushClientSavesOnShutdown() noexcept {
    if (auto* p = g_asyncSavePtr.load(std::memory_order_acquire)) {
        p->ShutdownAndFlush();
    }
}

static void __cdecl HookExeExit(int code) {
    FlushClientSavesOnShutdown();
    if (g_origExeExit) g_origExeExit(code);
}

static void __cdecl HookExe_exit(int code) {
    FlushClientSavesOnShutdown();
    if (g_origExe_exit) g_origExe_exit(code);
}

static void __cdecl HookExeCexit() {
    FlushClientSavesOnShutdown();
    if (g_origExeCexit) g_origExeCexit();
}

static BOOL WINAPI HyperSmoothConsoleCtrlHandler(DWORD) {
    FlushClientSavesOnShutdown();
    return FALSE;
}

static void InstallFsNegCacheHooks() {
    (void)GetAsyncSave();
    const bool wantFsNeg = g_optFsNegCache.load(std::memory_order_relaxed);
    const bool wantAsync = g_optAsyncSave.load(std::memory_order_relaxed);
    if (!wantFsNeg && !wantAsync) return;
    if (HMODULE hCrt = GetModuleHandleA("MSVCR90.dll")) {
        g_msvcr90Errno = reinterpret_cast<MsvcrErrnoFn_t>(GetProcAddress(hCrt, "_errno"));
    }
    // Fail-closed: install ALL read/write/rename/delete/unlink/find/exit barriers for both FSNegCache and AsyncSave!
    bool okMv   = PatchExeIatEntry("KERNEL32.dll", 0, "MoveFileExW",          reinterpret_cast<void*>(&HookFsMoveFileExW),          g_origMoveFileExW);
    bool okCp   = PatchExeIatEntry("KERNEL32.dll", 0, "CopyFileW",            reinterpret_cast<void*>(&HookFsCopyFileW),            g_origCopyFileW);
    bool okDl   = PatchExeIatEntry("KERNEL32.dll", 0, "DeleteFileW",          reinterpret_cast<void*>(&HookFsDeleteFileW),          g_origDeleteFileW);
    bool okMd   = PatchExeIatEntry("KERNEL32.dll", 0, "CreateDirectoryW",     reinterpret_cast<void*>(&HookFsCreateDirectoryW),     g_origCreateDirectoryW);
    bool okRd   = PatchExeIatEntry("KERNEL32.dll", 0, "RemoveDirectoryW",     reinterpret_cast<void*>(&HookFsRemoveDirectoryW),     g_origRemoveDirectoryW);
    bool okScd  = PatchExeIatEntry("KERNEL32.dll", 0, "SetCurrentDirectoryW", reinterpret_cast<void*>(&HookFsSetCurrentDirectoryW), g_origSetCurrentDirectoryW);
    bool okCf   = PatchExeIatEntry("KERNEL32.dll", 0, "CreateFileW",          reinterpret_cast<void*>(&HookFsCreateFileW),          g_origCreateFileW);
    bool okFf   = PatchExeIatEntry("KERNEL32.dll", 0, "FindFirstFileW",       reinterpret_cast<void*>(&HookFsFindFirstFileW),       g_origFindFirstFileW);
    bool okOpen = PatchExeIatEntry("MSVCR90.dll",  0, "_open",                reinterpret_cast<void*>(&HookFsOpenA),                g_origMsvcrOpen);

    bool okGa = false, okFo = false, okWf = false, okWfs = false, okFw = false, okFc = false;
    bool okS64 = false, okS32 = false, okWs = false;
    bool okEx = false, okEx2 = false, okCex = false;
    const bool barriersOk = okMv && okCp && okDl && okMd && okRd && okScd && okCf && okOpen;
    if (barriersOk) {
        okGa  = PatchExeIatEntry("KERNEL32.dll", 0, "GetFileAttributesW", reinterpret_cast<void*>(&HookFsGetFileAttributesW), g_origGetFileAttributesW);
        okFo  = PatchExeIatEntry("MSVCR90.dll",  0, "fopen",              reinterpret_cast<void*>(&HookFsFopen),              g_origMsvcrFopen);
        okWf  = PatchExeIatEntry("MSVCR90.dll",  0, "_wfopen",            reinterpret_cast<void*>(&HookFsWfopen),             g_origMsvcrWfopen);
        okWfs = PatchExeIatEntry("MSVCR90.dll",  0, "_wfopen_s",          reinterpret_cast<void*>(&HookFsWfopenS),            g_origMsvcrWfopenS);
        okFw  = PatchExeIatEntry("MSVCR90.dll",  0, "fwrite",             reinterpret_cast<void*>(&HookFsFwrite),             g_origMsvcrFwrite);
        okFc  = PatchExeIatEntry("MSVCR90.dll",  0, "fclose",             reinterpret_cast<void*>(&HookFsFclose),             g_origMsvcrFclose);
        okS64 = PatchExeIatEntry("MSVCR90.dll",  0, "_stat64",            reinterpret_cast<void*>(&HookFsStat64),             g_origMsvcrStat64);
        okS32 = PatchExeIatEntry("MSVCR90.dll",  0, "_stat64i32",         reinterpret_cast<void*>(&HookFsStat64i32),          g_origMsvcrStat64i32);
        okWs  = PatchExeIatEntry("MSVCR90.dll",  0, "_wstat64i32",        reinterpret_cast<void*>(&HookFsWstat64i32),         g_origMsvcrWstat64i32);
        okEx  = PatchExeIatEntry("MSVCR90.dll",  0, "exit",               reinterpret_cast<void*>(&HookExeExit),              g_origExeExit);
        okEx2 = PatchExeIatEntry("MSVCR90.dll",  0, "_exit",              reinterpret_cast<void*>(&HookExe_exit),             g_origExe_exit);
        okCex = PatchExeIatEntry("MSVCR90.dll",  0, "_cexit",             reinterpret_cast<void*>(&HookExeCexit),             g_origExeCexit);
        SetConsoleCtrlHandler(&HyperSmoothConsoleCtrlHandler, TRUE);
    }
    const bool fsNegActive = wantFsNeg && barriersOk && (okGa || okFo || okWf || okWfs || okS64 || okS32 || okWs);
    g_fsNegCacheActive.store(fsNegActive, std::memory_order_release);
    if (wantFsNeg && !fsNegActive) g_optFsNegCache.store(false, std::memory_order_release);

    const bool asyncBarriersOk = barriersOk && okFf && okGa && okFo && okWf && okWfs &&
                                 okFw && okFc && okS64 && okS32 && okWs && okEx && okEx2 && okCex;
    g_asyncBarriersReady.store(asyncBarriersOk, std::memory_order_release);

    LogMsg("[PERF][FSNEG] KERNEL32+MSVCR90 Mod/Data Negative Stat Cache (barriers=%d ga=%d fo=%d wf=%d wfs=%d fw=%d fc=%d ff=%d s64=%d s32=%d ws=%d ex=%d/%d/%d errno=%d) -> fsNeg=%d asyncBarriers=%d",
           barriersOk?1:0, okGa?1:0, okFo?1:0, okWf?1:0, okWfs?1:0, okFw?1:0, okFc?1:0, okFf?1:0,
           okS64?1:0, okS32?1:0, okWs?1:0, okEx?1:0, okEx2?1:0, okCex?1:0, g_msvcr90Errno?1:0,
           fsNegActive?1:0, asyncBarriersOk?1:0);
}

// =============================================================================
// M1 + M2 IAT Hooks: Hitch Main-Thread Probes (SteamAPI_RunCallbacks, PeekMessageW)
//                  + Crash Detective (dbghelp.dll!MiniDumpWriteDump)
// =============================================================================
using SteamRunCallbacks_t  = void (__cdecl*)();
using PeekMessageW_t       = BOOL (WINAPI*)(LPMSG, HWND, UINT, UINT, UINT);
using XInputGetState_t     = DWORD (WINAPI*)(DWORD, void*);
using MiniDumpWriteDump_t  = BOOL (WINAPI*)(HANDLE, DWORD, HANDLE, uint32_t, void*, void*, void*);

SteamRunCallbacks_t g_origSteamRunCallbacks = nullptr;
PeekMessageW_t      g_origPeekMessageW      = nullptr;
XInputGetState_t    g_origXInputGetState    = nullptr;
MiniDumpWriteDump_t g_origMiniDumpWriteDump = nullptr;

// Per-controller-slot disconnect throttle (re-probes disconnected Xbox slots every 500ms instead of 240Hz;
// connected slots pass through at 0ms latency, and WM_DEVICECHANGE clears the cache instantaneously!)
static std::atomic<int64_t> g_xinputDiscQpc[4]{};
static std::atomic<uint64_t> g_mouseMoveCoalesced{0};
static std::atomic<uint64_t> g_xinputThrottleHits{0};

struct MinidumpExcParamLite {
    DWORD                ThreadId;
    PEXCEPTION_POINTERS  ExceptionPointers;
    BOOL                 ClientPointers;
};

static DWORD WINAPI HookXInputGetState(DWORD dwUserIndex, void* pState) {
    if (!g_origXInputGetState) return ERROR_DEVICE_NOT_CONNECTED;
    if (dwUserIndex < 4u && g_qpcFreq > 0) {
        const int64_t lastDisc = g_xinputDiscQpc[dwUserIndex].load(std::memory_order_relaxed);
        if (lastDisc != 0) {
            const int64_t now = NowQpc();
            if (now >= lastDisc && (now - lastDisc) < (g_qpcFreq / 2)) {
                g_xinputThrottleHits.fetch_add(1, std::memory_order_relaxed);
                return ERROR_DEVICE_NOT_CONNECTED;
            }
        }
    }
    const DWORD rc = g_origXInputGetState(dwUserIndex, pState);
    if (dwUserIndex < 4u) {
        if (rc == ERROR_DEVICE_NOT_CONNECTED) {
            g_xinputDiscQpc[dwUserIndex].store(NowQpc(), std::memory_order_relaxed);
        } else {
            g_xinputDiscQpc[dwUserIndex].store(0, std::memory_order_relaxed);
        }
    }
    return rc;
}

static void __cdecl HookSteamApiRunCallbacks() {
    if (!g_origSteamRunCallbacks) return;
    const int64_t t0 = NowQpc();
    g_origSteamRunCallbacks();
    if (g_qpcToUs > 0.0) {
        const double us = static_cast<double>(NowQpc() - t0) * g_qpcToUs;
        if (us > 0.0 && us < 60000000.0) {
            g_frameSteamUs.fetch_add(static_cast<uint32_t>(us), std::memory_order_relaxed);
        }
    }
}

static BOOL WINAPI HookPeekMessageW(LPMSG lpMsg, HWND hWnd, UINT wMsgFilterMin, UINT wMsgFilterMax, UINT wRemoveMsg) {
    if (!g_origPeekMessageW) return FALSE;
    const int64_t t0 = NowQpc();
    const BOOL rc = g_origPeekMessageW(lpMsg, hWnd, wMsgFilterMin, wMsgFilterMax, wRemoveMsg);
    if (rc && lpMsg && (wRemoveMsg & PM_REMOVE) != 0 && wMsgFilterMin == 0u && wMsgFilterMax == 0u) {
        if (lpMsg->message == 0x0219u /*WM_DEVICECHANGE*/) {
            for (int i = 0; i < 4; ++i) {
                g_xinputDiscQpc[i].store(0, std::memory_order_relaxed);
            }
        } else if (lpMsg->message == WM_MOUSEMOVE) {
            // Order-preserving high-polling-rate mouse coalescing (4K/8KHz mice):
            // Only collapse strictly adjacent WM_MOUSEMOVE messages with identical (hwnd, wParam),
            // NEVER crossing button clicks, wheel events, keyboard input, or modifier changes!
            uint32_t collapsed = 0;
            MSG nextMsg{};
            while (collapsed < 64u &&
                   g_origPeekMessageW(&nextMsg, hWnd, 0u, 0u, PM_NOREMOVE) &&
                   nextMsg.message == WM_MOUSEMOVE &&
                   nextMsg.hwnd == lpMsg->hwnd &&
                   nextMsg.wParam == lpMsg->wParam) {
                if (!g_origPeekMessageW(&nextMsg, hWnd, 0u, 0u, PM_REMOVE)) break;
                *lpMsg = nextMsg;
                ++collapsed;
            }
            if (collapsed > 0u) {
                g_mouseMoveCoalesced.fetch_add(collapsed, std::memory_order_relaxed);
            }
        }
    }
    if (g_qpcToUs > 0.0) {
        const double us = static_cast<double>(NowQpc() - t0) * g_qpcToUs;
        if (us > 0.0 && us < 60000000.0) {
            g_frameMsgUs.fetch_add(static_cast<uint32_t>(us), std::memory_order_relaxed);
        }
    }
    return rc;
}

static BOOL WINAPI HookMiniDumpWriteDump(HANDLE hProcess, DWORD processId, HANDLE hFile,
                                         uint32_t dumpType, void* excParam,
                                         void* userStreamParam, void* callbackParam) {
    EXCEPTION_POINTERS* ep = nullptr;
    if (excParam != nullptr) {
        const auto* p = static_cast<const MinidumpExcParamLite*>(excParam);
        if (!p->ClientPointers) {
            ep = p->ExceptionPointers;
        }
    }
    g_crashDetective.OnCrashTriggered(ep, "MiniDumpWriteDump");
    if (g_origMiniDumpWriteDump) {
        return g_origMiniDumpWriteDump(hProcess, processId, hFile, dumpType, excParam, userStreamParam, callbackParam);
    }
    return FALSE;
}

static void InstallDiagnosticsAndUtilityIatHooks() {
    g_crashDetective.Init(g_binDir.c_str(), &g_cursorClip, "2.9.6");
    g_crashDetective.InstallUnhandledFilter();
    const bool okDump   = PatchExeIatEntry("dbghelp.dll",     0, "MiniDumpWriteDump",     reinterpret_cast<void*>(&HookMiniDumpWriteDump),    g_origMiniDumpWriteDump);
    const bool okSteam  = PatchExeIatEntry("steam_api64.dll", 0, "SteamAPI_RunCallbacks", reinterpret_cast<void*>(&HookSteamApiRunCallbacks), g_origSteamRunCallbacks);
    const bool okPeek   = PatchExeIatEntry("USER32.dll",      0, "PeekMessageW",          reinterpret_cast<void*>(&HookPeekMessageW),         g_origPeekMessageW);
    const bool okXInput = PatchExeIatEntry("XINPUT1_3.dll",   2, "XInputGetState",        reinterpret_cast<void*>(&HookXInputGetState),       g_origXInputGetState);
    LogMsg("[DIAG][M1+M2] CrashDetective+HitchProbes+InputOpt IAT hooks (minidump=%d steamCb=%d peekMsg=%d xinput=%d, report=%s)",
           okDump ? 1 : 0, okSteam ? 1 : 0, okPeek ? 1 : 0, okXInput ? 1 : 0, g_crashDetective.ReportPathUtf8());
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
    float masterMax = 0.0f;
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
        for (int i = 0; i < 4; ++i) {
            g_hd2dState.params[i] = next.params[i];
            g_hd2dState.sun[i]    = next.sun[i];
            g_hd2dState.tint[i]   = next.tint[i];
        }
    }
    masterMax = std::max({
        g_hd2dState.params[0], g_hd2dState.params[1], g_hd2dState.params[2], g_hd2dState.sun[2],
        g_hd2dState.shade[0], g_hd2dState.ocean[0], g_hd2dState.shadProj[3],
        g_hd2dState.detail[0], g_hd2dState.detail[1], g_hd2dState.detail[2],
        g_hd2dState.oceanPbr[0], g_hd2dState.oceanPbr[1], g_hd2dState.oceanPbr[2]
    });
    ReleaseSRWLockExclusive(&g_hd2dStateLock);

    if (changed) {
        g_hd2dMasterStr.store(masterMax, std::memory_order_relaxed);
        g_hd2dStateGen.fetch_add(1, std::memory_order_release);
    }
    g_luaPushBoolean(L, g_hd2dIatHooked ? 1 : 0);
    return 1;
}

int LuaHyperSmoothSetDetailParams(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    auto clampF = [](float v, float lo, float hi) {
        if (!(v >= lo)) return lo;
        if (v > hi) return hi;
        return v;
    };
    float nDet[4] = {
        clampF(static_cast<float>(g_luaToNumber(L, 1)), 0.0f, 1.50f), // groundDetailStr
        clampF(static_cast<float>(g_luaToNumber(L, 2)), 0.0f, 1.50f), // spriteEdgeStr
        clampF(static_cast<float>(g_luaToNumber(L, 3)), 0.0f, 1.50f), // spriteInkStr
        clampF(static_cast<float>(g_luaToNumber(L, 4)), 0.0f, 1.50f), // reserved
    };
    bool changed = false;
    float masterMax = 0.0f;
    AcquireSRWLockExclusive(&g_hd2dStateLock);
    for (int i = 0; i < 4; ++i) {
        if (fabsf(g_hd2dState.detail[i] - nDet[i]) > 1e-4f) {
            changed = true;
            g_hd2dState.detail[i] = nDet[i];
        }
    }
    masterMax = std::max({
        g_hd2dState.params[0], g_hd2dState.params[1], g_hd2dState.params[2], g_hd2dState.sun[2],
        g_hd2dState.shade[0], g_hd2dState.ocean[0], g_hd2dState.shadProj[3],
        g_hd2dState.detail[0], g_hd2dState.detail[1], g_hd2dState.detail[2],
        g_hd2dState.oceanPbr[0], g_hd2dState.oceanPbr[1], g_hd2dState.oceanPbr[2]
    });
    ReleaseSRWLockExclusive(&g_hd2dStateLock);
    if (changed) {
        g_hd2dMasterStr.store(masterMax, std::memory_order_relaxed);
        g_hd2dStateGen.fetch_add(1, std::memory_order_release);
    }
    g_luaPushBoolean(L, g_hd2dIatHooked ? 1 : 0);
    return 1;
}

// Native World Studio uniform bus: 3D Entity Surface Lighting + Procedural Ocean Caustics + Vertex-Projected Ground Shadows
int LuaHyperSmoothSetWorldStudio(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    auto clampF = [](float v, float lo, float hi) {
        if (!(v >= lo)) return lo;
        if (v > hi) return hi;
        return v;
    };
    float nShade[4]    = {
        clampF(static_cast<float>(g_luaToNumber(L, 1)), 0.0f, 1.5f),   // surfaceLightStr
        clampF(static_cast<float>(g_luaToNumber(L, 2)), 0.0f, 1.0f),   // shadeWarm
        clampF(static_cast<float>(g_luaToNumber(L, 3)), 0.0f, 1.0f),   // shadeContrast
        clampF(static_cast<float>(g_luaToNumber(L, 4)), 0.0f, 1.0f),   // shadeCool
    };
    float nShade2[4]   = {
        clampF(static_cast<float>(g_luaToNumber(L, 5)), 0.0f, 1.0f),   // shadeDark
        clampF(static_cast<float>(g_luaToNumber(L, 6)), -3.15f, 3.15f),// sunThetaCam (rad)
        clampF(static_cast<float>(g_luaToNumber(L, 7)), 0.06f, 1.55f), // sunElevRad
        clampF(static_cast<float>(g_luaToNumber(L, 8)), 0.0f, 1.0f),   // dayFactor / lightPresence
    };
    float nOcean[4]    = {
        clampF(static_cast<float>(g_luaToNumber(L, 9)),  0.0f, 2.0f),  // glintStr
        clampF(static_cast<float>(g_luaToNumber(L, 10)), 0.50f, 0.998f),// glintDensity
        clampF(static_cast<float>(g_luaToNumber(L, 11)), 0.35f, 2.50f),// glintGrain
        clampF(static_cast<float>(g_luaToNumber(L, 12)), 0.0f, 1.0f),  // glintSoft
    };
    float nShadProj[4] = {
        clampF(static_cast<float>(g_luaToNumber(L, 13)), -1.0f, 1.0f), // shadowDirWx
        clampF(static_cast<float>(g_luaToNumber(L, 14)), -1.0f, 1.0f), // shadowDirWz
        clampF(static_cast<float>(g_luaToNumber(L, 15)), 0.35f, 4.20f),// shadowStretch (1/tan(el) * lengthMult)
        clampF(static_cast<float>(g_luaToNumber(L, 16)), 0.0f, 1.0f),  // shadowProjOn (0/1)
    };

    bool changed = false;
    float masterMax = 0.0f;
    AcquireSRWLockExclusive(&g_hd2dStateLock);
    for (int i = 0; i < 4; ++i) {
        if (fabsf(g_hd2dState.shade[i]    - nShade[i])    > 1e-6f ||
            fabsf(g_hd2dState.shade2[i]   - nShade2[i])   > 1e-6f ||
            fabsf(g_hd2dState.ocean[i]    - nOcean[i])    > 1e-6f ||
            fabsf(g_hd2dState.shadProj[i] - nShadProj[i]) > 1e-6f) {
            changed = true;
            break;
        }
    }
    if (changed) {
        for (int i = 0; i < 4; ++i) {
            g_hd2dState.shade[i]    = nShade[i];
            g_hd2dState.shade2[i]   = nShade2[i];
            g_hd2dState.ocean[i]    = nOcean[i];
            g_hd2dState.shadProj[i] = nShadProj[i];
        }
    }
    masterMax = std::max({
        g_hd2dState.params[0], g_hd2dState.params[1], g_hd2dState.params[2], g_hd2dState.sun[2],
        g_hd2dState.shade[0], g_hd2dState.ocean[0], g_hd2dState.shadProj[3],
        g_hd2dState.detail[0], g_hd2dState.detail[1], g_hd2dState.detail[2],
        g_hd2dState.oceanPbr[0], g_hd2dState.oceanPbr[1], g_hd2dState.oceanPbr[2]
    });
    ReleaseSRWLockExclusive(&g_hd2dStateLock);

    if (changed) {
        g_hd2dMasterStr.store(masterMax, std::memory_order_relaxed);
        g_hd2dStateGen.fetch_add(1, std::memory_order_release);
    }
    g_luaPushBoolean(L, g_hd2dIatHooked ? 1 : 0);
    return 1;
}

int LuaHyperSmoothSetShadowDyn(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    auto clampF = [](float v, float lo, float hi) {
        if (!(v >= lo)) return lo;
        if (v > hi) return hi;
        return v;
    };
    float nShadDyn[4] = {
        clampF(static_cast<float>(g_luaToNumber(L, 1)), 0.0f, 100000.0f), // windTimeSec (continuous dt)
        clampF(static_cast<float>(g_luaToNumber(L, 2)), 0.0f, 1.50f),     // swayAmp (canopy breeze)
        clampF(static_cast<float>(g_luaToNumber(L, 3)), 0.0f, 1.00f),     // heightFade (contact-to-tip dispersal)
        clampF(static_cast<float>(g_luaToNumber(L, 4)), 0.25f, 2.50f),    // penumbraBias
    };
    bool changed = false;
    AcquireSRWLockExclusive(&g_hd2dStateLock);
    const bool shadowsActive =
        (g_hd2dState.shadowDensity > 0.001f) ||
        (g_hd2dState.shadProj[3] > 0.5f) ||
        (g_hd2dState.pointLightCount > 0u) ||
        (g_hd2dState.oceanPbr[0] > 0.001f) ||
        (g_hd2dState.oceanPbr[1] > 0.001f) ||
        (g_hd2dState.oceanPbr[2] > 0.001f);
    const int startIdx = shadowsActive ? 0 : 1; // Ignore idle windTime clock drift when shadows & ocean PBR are OFF
    for (int i = startIdx; i < 4; ++i) {
        if (fabsf(g_hd2dState.shadDyn[i] - nShadDyn[i]) > 1e-6f) {
            changed = true;
            break;
        }
    }
    if (changed) {
        for (int i = 0; i < 4; ++i) {
            g_hd2dState.shadDyn[i] = nShadDyn[i];
        }
    }
    ReleaseSRWLockExclusive(&g_hd2dStateLock);
    if (changed) {
        g_hd2dStateGen.fetch_add(1, std::memory_order_release);
    }
    g_luaPushBoolean(L, g_hd2dIatHooked ? 1 : 0);
    return 1;
}

int LuaHyperSmoothSetShadowConfig(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    auto clampF = [](float v, float lo, float hi) {
        if (!(v >= lo)) return lo;
        if (v > hi) return hi;
        return v;
    };
    const float nDen   = clampF(static_cast<float>(g_luaToNumber(L, 1)), 0.0f, 1.0f);
    const float nSoft  = clampF(static_cast<float>(g_luaToNumber(L, 2)), 0.00f, 2.00f);
    const float nTintR = clampF(static_cast<float>(g_luaToNumber(L, 3)), 0.0f, 1.0f);
    const float nTintG = clampF(static_cast<float>(g_luaToNumber(L, 4)), 0.0f, 1.0f);
    const float nTintB = clampF(static_cast<float>(g_luaToNumber(L, 5)), 0.0f, 1.0f);
    const bool  nHd    = (g_luaToNumber(L, 6) > 0.5);
    const bool  nSkip  = (g_luaToNumber(L, 7) > 0.5);
    bool hasCam = false;
    float nCam[4] = { 0.0f, 0.0f, 0.707f, -0.707f };
    {
        const float cx = static_cast<float>(g_luaToNumber(L, 8));
        const float cz = static_cast<float>(g_luaToNumber(L, 9));
        const float rx = static_cast<float>(g_luaToNumber(L, 10));
        const float rz = static_cast<float>(g_luaToNumber(L, 11));
        if (std::isfinite(cx) && std::isfinite(cz) && std::isfinite(rx) && std::isfinite(rz) &&
            (rx * rx + rz * rz) > 1e-4f) {
            hasCam = true;
            nCam[0] = cx;
            nCam[1] = cz;
            nCam[2] = rx;
            nCam[3] = rz;
        }
    }

    // Optional args 12 & 13 (v2.9.0 Phase 1): 3D entity shadow receiving strength & vanilla oval shadow keep.
    // In Lua 5.1, numeric 0.0 is truthy (g_luaToBoolean != 0) while nil/omitted is falsy (0),
    // preserving 100% backward compatibility with 7-arg / 11-arg callers!
    const float nRecv = (g_luaToBoolean && g_luaToBoolean(L, 12))
        ? clampF(static_cast<float>(g_luaToNumber(L, 12)), 0.0f, 1.0f)
        : 0.68f;
    const float nOvalKeep = (g_luaToBoolean && g_luaToBoolean(L, 13))
        ? clampF(static_cast<float>(g_luaToNumber(L, 13)), 0.0f, 1.0f)
        : 0.24f;
    // Optional arg 14 (v2.9.5): Sprite minimum world-height cutoff for shadow casting (0 = all standing sprites)
    const float nMinHeight = (g_luaToBoolean && g_luaToBoolean(L, 14))
        ? clampF(static_cast<float>(g_luaToNumber(L, 14)), 0.0f, 1.50f)
        : 0.0f;

    bool changed = false;
    AcquireSRWLockExclusive(&g_hd2dStateLock);
    if (fabsf(g_hd2dState.shadowDensity - nDen) > 1e-6f ||
        fabsf(g_hd2dState.shadowSoft - nSoft) > 1e-6f ||
        fabsf(g_hd2dState.shadowTint[0] - nTintR) > 1e-6f ||
        fabsf(g_hd2dState.shadowTint[1] - nTintG) > 1e-6f ||
        fabsf(g_hd2dState.shadowTint[2] - nTintB) > 1e-6f ||
        fabsf(g_hd2dState.shadowEntityRecv - nRecv) > 1e-6f ||
        fabsf(g_hd2dState.shadowOvalKeep - nOvalKeep) > 1e-6f ||
        fabsf(g_hd2dState.shadowMinHeight - nMinHeight) > 1e-6f ||
        g_hd2dState.shadowHd != nHd ||
        g_hd2dState.shadowSkipNightmare != nSkip ||
        g_hd2dState.hasLuaCam != hasCam ||
        (hasCam && (fabsf(g_hd2dState.luaCam[0] - nCam[0]) > 1e-3f ||
                    fabsf(g_hd2dState.luaCam[1] - nCam[1]) > 1e-3f ||
                    fabsf(g_hd2dState.luaCam[2] - nCam[2]) > 1e-4f ||
                    fabsf(g_hd2dState.luaCam[3] - nCam[3]) > 1e-4f))) {
        g_hd2dState.shadowDensity       = nDen;
        g_hd2dState.shadowSoft          = nSoft;
        g_hd2dState.shadowTint[0]       = nTintR;
        g_hd2dState.shadowTint[1]       = nTintG;
        g_hd2dState.shadowTint[2]       = nTintB;
        g_hd2dState.shadowEntityRecv    = nRecv;
        g_hd2dState.shadowOvalKeep      = nOvalKeep;
        g_hd2dState.shadowMinHeight     = nMinHeight;
        g_hd2dState.shadowHd            = nHd;
        g_hd2dState.shadowSkipNightmare = nSkip;
        g_hd2dState.hasLuaCam           = hasCam;
        if (hasCam) {
            for (int i = 0; i < 4; ++i) g_hd2dState.luaCam[i] = nCam[i];
        }
        changed = true;
    }
    ReleaseSRWLockExclusive(&g_hd2dStateLock);
    if (changed) {
        g_hd2dStateGen.fetch_add(1, std::memory_order_release);
    }
    g_luaPushBoolean(L, g_worldShadowReady.load(std::memory_order_acquire) ? 1 : 0);
    g_luaPushBoolean(L, g_worldShadowFailed.load(std::memory_order_acquire) ? 1 : 0);
    return 2;
}

// v2.9.5: HyperSmooth_SetPointLights(dirStrength, pointStretchScale, count, x0, z0, r0, s0, ...) -> acceptedCount
// Strictly finite & range sanitized: decouples directional sun/moon pass from Top-3 radial point lights!
int LuaHyperSmoothSetPointLights(void* L) {
    if (!g_luaToNumber || !g_luaPushNumber) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    auto clampF = [](float v, float lo, float hi) {
        if (!std::isfinite(v) || !(v >= lo)) return lo;
        if (v > hi) return hi;
        return v;
    };
    const float nDirStr   = clampF(static_cast<float>(g_luaToNumber(L, 1)), 0.0f, 1.0f);
    const float nPtScale  = clampF(static_cast<float>(g_luaToNumber(L, 2)), 0.50f, 1.80f);
    const double rawCnt   = g_luaToNumber(L, 3);
    uint32_t reqCnt = 0u;
    if (std::isfinite(rawCnt) && rawCnt >= 1.0) {
        const uint32_t u = static_cast<uint32_t>(rawCnt);
        reqCnt = (u <= hs_world_shadow::kMaxPointShadowLights) ? u : hs_world_shadow::kMaxPointShadowLights;
    }
    uint32_t outCnt = 0u;
    float outLights[hs_world_shadow::kMaxPointShadowLights][4]{};
    for (uint32_t i = 0u; i < reqCnt; ++i) {
        const int base = 4 + static_cast<int>(i) * 4;
        const float wx  = static_cast<float>(g_luaToNumber(L, base + 0));
        const float wz  = static_cast<float>(g_luaToNumber(L, base + 1));
        const float rad = clampF(static_cast<float>(g_luaToNumber(L, base + 2)), 0.0f, 48.0f);
        const float str = clampF(static_cast<float>(g_luaToNumber(L, base + 3)), 0.0f, 1.0f);
        if (!std::isfinite(wx) || !std::isfinite(wz) || rad <= 0.5f || str <= 0.008f) {
            continue;
        }
        outLights[outCnt][0] = wx;
        outLights[outCnt][1] = wz;
        outLights[outCnt][2] = rad;
        outLights[outCnt][3] = str;
        ++outCnt;
    }

    bool changed = false;
    AcquireSRWLockExclusive(&g_hd2dStateLock);
    if (fabsf(g_hd2dState.shadowDirStrength - nDirStr) > 1e-4f ||
        fabsf(g_hd2dState.shadowPointStretch - nPtScale) > 1e-4f ||
        g_hd2dState.pointLightCount != outCnt) {
        changed = true;
    } else {
        for (uint32_t i = 0u; i < outCnt; ++i) {
            if (fabsf(g_hd2dState.pointLights[i][0] - outLights[i][0]) > 1e-3f ||
                fabsf(g_hd2dState.pointLights[i][1] - outLights[i][1]) > 1e-3f ||
                fabsf(g_hd2dState.pointLights[i][2] - outLights[i][2]) > 1e-3f ||
                fabsf(g_hd2dState.pointLights[i][3] - outLights[i][3]) > 1e-4f) {
                changed = true;
                break;
            }
        }
    }
    if (changed) {
        g_hd2dState.shadowDirStrength  = nDirStr;
        g_hd2dState.shadowPointStretch = nPtScale;
        g_hd2dState.pointLightCount    = outCnt;
        for (uint32_t i = 0u; i < hs_world_shadow::kMaxPointShadowLights; ++i) {
            for (int c = 0; c < 4; ++c) {
                g_hd2dState.pointLights[i][c] = (i < outCnt) ? outLights[i][c] : 0.0f;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_hd2dStateLock);
    if (changed) {
        g_hd2dStateGen.fetch_add(1, std::memory_order_release);
    }
    g_luaPushNumber(L, static_cast<double>(outCnt));
    return 1;
}

// v2.9.5 M3: HyperSmooth_SetWaterRefl(strength, rippleAmp, lengthScale) -> ready, lastFrameDraws
int LuaHyperSmoothSetWaterRefl(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean || !g_luaPushNumber) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    auto clampF = [](float v, float lo, float hi) {
        if (!std::isfinite(v) || !(v >= lo)) return lo;
        if (v > hi) return hi;
        return v;
    };
    const float nStr = clampF(static_cast<float>(g_luaToNumber(L, 1)), 0.0f, 1.20f);
    const float nRip = clampF(static_cast<float>(g_luaToNumber(L, 2)), 0.0f, 1.50f);
    float nLen = static_cast<float>(g_luaToNumber(L, 3));
    if (!std::isfinite(nLen) || nLen <= 0.01f) nLen = 1.00f;
    nLen = clampF(nLen, 0.50f, 1.60f);

    bool changed = false;
    AcquireSRWLockExclusive(&g_hd2dStateLock);
    if (fabsf(g_hd2dState.waterReflStr - nStr) > 1e-4f ||
        fabsf(g_hd2dState.waterReflRipple - nRip) > 1e-4f ||
        fabsf(g_hd2dState.waterReflLength - nLen) > 1e-4f) {
        g_hd2dState.waterReflStr    = nStr;
        g_hd2dState.waterReflRipple = nRip;
        g_hd2dState.waterReflLength = nLen;
        changed = true;
    }
    ReleaseSRWLockExclusive(&g_hd2dStateLock);
    if (changed) {
        g_hd2dStateGen.fetch_add(1, std::memory_order_release);
    }
    g_luaPushBoolean(L, g_waterReflReady.load(std::memory_order_relaxed) ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(g_waterReflDraws.load(std::memory_order_relaxed)));
    return 2;
}

// v2.9.5-F: HyperSmooth_SetOceanPbr(pbrWaveStr, skyCloudRefl, sunMieGlint, hidePaperWaves) -> hooked
int LuaHyperSmoothSetOceanPbr(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    auto clampF = [](float v, float lo, float hi) {
        if (!std::isfinite(v) || !(v >= lo)) return lo;
        if (v > hi) return hi;
        return v;
    };
    const float nPbr[4] = {
        clampF(static_cast<float>(g_luaToNumber(L, 1)), 0.0f, 1.50f),
        clampF(static_cast<float>(g_luaToNumber(L, 2)), 0.0f, 1.50f),
        clampF(static_cast<float>(g_luaToNumber(L, 3)), 0.0f, 1.50f),
        (static_cast<float>(g_luaToNumber(L, 4)) > 0.5f) ? 1.0f : 0.0f
    };

    bool changed = false;
    float masterMax = 0.0f;
    AcquireSRWLockExclusive(&g_hd2dStateLock);
    for (int i = 0; i < 4; ++i) {
        if (fabsf(g_hd2dState.oceanPbr[i] - nPbr[i]) > 1e-4f) {
            g_hd2dState.oceanPbr[i] = nPbr[i];
            changed = true;
        }
    }
    masterMax = std::max({
        g_hd2dState.params[0], g_hd2dState.params[1], g_hd2dState.params[2], g_hd2dState.sun[2],
        g_hd2dState.shade[0], g_hd2dState.ocean[0], g_hd2dState.shadProj[3],
        g_hd2dState.detail[0], g_hd2dState.detail[1], g_hd2dState.detail[2],
        g_hd2dState.oceanPbr[0], g_hd2dState.oceanPbr[1], g_hd2dState.oceanPbr[2]
    });
    ReleaseSRWLockExclusive(&g_hd2dStateLock);
    if (changed) {
        g_hd2dMasterStr.store(masterMax, std::memory_order_relaxed);
        g_hd2dStateGen.fetch_add(1, std::memory_order_release);
    }
    g_luaPushBoolean(L, g_hd2dIatHooked ? 1 : 0);
    return 1;
}

// v2.9.5 M4: HyperSmooth_SetWindField(windTime, swayAmp, wakeAmp, playerX, playerZ, hasPlayer) -> active
int LuaHyperSmoothSetWindField(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    auto clampF = [](float v, float lo, float hi) {
        if (!std::isfinite(v) || !(v >= lo)) return lo;
        if (v > hi) return hi;
        return v;
    };
    const float wt  = static_cast<float>(g_luaToNumber(L, 1));
    const float sw  = clampF(static_cast<float>(g_luaToNumber(L, 2)), 0.0f, 1.50f);
    const float wk  = clampF(static_cast<float>(g_luaToNumber(L, 3)), 0.0f, 1.50f);
    const float px  = static_cast<float>(g_luaToNumber(L, 4));
    const float pz  = static_cast<float>(g_luaToNumber(L, 5));
    const bool  hp  = g_luaToBoolean ? (g_luaToBoolean(L, 6) != 0) : (std::isfinite(px) && std::isfinite(pz));

    hs_foliage::WindFieldState ns{};
    ns.windTime  = std::isfinite(wt) ? wt : 0.0f;
    ns.swayAmp   = sw;
    ns.wakeAmp   = wk;
    ns.playerWx  = std::isfinite(px) ? px : 0.0f;
    ns.playerWz  = std::isfinite(pz) ? pz : 0.0f;
    ns.hasPlayer = (hp && std::isfinite(px) && std::isfinite(pz)) ? 1.0f : 0.0f;

    bool changed = false;
    AcquireSRWLockExclusive(&g_windLock);
    const bool bothOff =
        (g_windState.swayAmp <= 0.001f && g_windState.wakeAmp <= 0.001f &&
         ns.swayAmp <= 0.001f && ns.wakeAmp <= 0.001f);
    if (!bothOff &&
        (fabsf(g_windState.windTime - ns.windTime) > 1e-4f ||
         fabsf(g_windState.swayAmp - ns.swayAmp) > 1e-4f ||
         fabsf(g_windState.wakeAmp - ns.wakeAmp) > 1e-4f ||
         fabsf(g_windState.playerWx - ns.playerWx) > 1e-3f ||
         fabsf(g_windState.playerWz - ns.playerWz) > 1e-3f ||
         g_windState.hasPlayer != ns.hasPlayer)) {
        g_windState = ns;
        changed = true;
    }
    ReleaseSRWLockExclusive(&g_windLock);
    if (changed) {
        g_windStateGen.fetch_add(1, std::memory_order_release);
    }
    g_luaPushBoolean(L, ns.IsActive() ? 1 : 0);
    return 1;
}

// v2.9.5 M4: HyperSmooth_SetFoliageAnchors(count, x0, z0, cls0, ...) -> acceptedCount
int LuaHyperSmoothSetFoliageAnchors(void* L) {
    if (!g_luaToNumber || !g_luaPushNumber) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    const double rawCnt = g_luaToNumber(L, 1);
    uint32_t reqCnt = 0u;
    if (std::isfinite(rawCnt) && rawCnt >= 1.0) {
        const uint32_t u = static_cast<uint32_t>(rawCnt);
        reqCnt = (u <= 384u) ? u : 384u;
    }
    AcquireSRWLockExclusive(&g_foliageLock);
    g_foliageStage.Clear();
    uint32_t accepted = 0u;
    for (uint32_t i = 0u; i < reqCnt; ++i) {
        const int base = 2 + static_cast<int>(i) * 3;
        const float wx = static_cast<float>(g_luaToNumber(L, base + 0));
        const float wz = static_cast<float>(g_luaToNumber(L, base + 1));
        const int cls  = static_cast<int>(g_luaToNumber(L, base + 2));
        if (cls == 1 || cls == 2) {
            if (g_foliageStage.Insert(wx, wz, static_cast<uint8_t>(cls))) {
                ++accepted;
            }
        }
    }
    ReleaseSRWLockExclusive(&g_foliageLock);
    g_foliageTableGen.fetch_add(1, std::memory_order_release);
    g_luaPushNumber(L, static_cast<double>(accepted));
    return 1;
}

int LuaHyperSmoothGetWorldStudioStatus(void* L) {
    if (!g_luaPushNumber) return 0;
    g_luaPushNumber(L, static_cast<double>(g_hd2dProgsLinked.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_hd2dShadersInjected.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_hd2dAnimVsInjected.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_hd2dShadowBloomInjected.load(std::memory_order_relaxed)));
    if (g_luaPushBoolean) {
        g_luaPushBoolean(L, g_worldShadowReady.load(std::memory_order_acquire) ? 1 : 0);
        g_luaPushNumber(L, static_cast<double>(g_worldShadowRes.load(std::memory_order_relaxed)));
        g_luaPushBoolean(L, g_worldShadowUsesMax.load(std::memory_order_relaxed) ? 1 : 0);
        g_luaPushNumber(L, static_cast<double>(g_worldShadowDraws.load(std::memory_order_relaxed)));
        g_luaPushNumber(L, static_cast<double>(g_worldShadowBatchDraws.load(std::memory_order_relaxed)));
        return 9;
    }
    return 4;
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
                if (us > 0) {
                    g_frameHostUs.fetch_add(static_cast<uint32_t>(std::min<LONG64>(us, 60000000LL)),
                                            std::memory_order_relaxed);
                }
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
    if (us > 0) {
        g_frameHostUs.fetch_add(static_cast<uint32_t>(std::min<LONG64>(us, 60000000LL)),
                                std::memory_order_relaxed);
    }
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
    if (us > 0.0) {
        g_frameHostUs.fetch_add(static_cast<uint32_t>(std::clamp(us, 0.0, 60000000.0)),
                                std::memory_order_relaxed);
    }

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
        if (us > 0) {
            g_frameHostUs.fetch_add(static_cast<uint32_t>(std::min<LONG64>(us, 60000000LL)),
                                    std::memory_order_relaxed);
        }
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

int LuaHyperSmoothGetRenderBackend(void* L) {
    if (!g_luaPushString) return 0;
    const bool nat = g_nativeGlActive.load(std::memory_order_acquire);
    g_luaPushString(L, nat ? "opengl" : "d3d9");
    char rendererBuf[128] = {};
    AcquireSRWLockShared(&g_nativeGlLock);
    if (!g_activeGlRendererName.empty()) {
        snprintf(rendererBuf, sizeof(rendererBuf), "%s", g_activeGlRendererName.c_str());
    }
    ReleaseSRWLockShared(&g_nativeGlLock);
    g_luaPushString(L, rendererBuf[0] ? rendererBuf : (nat ? "OpenGL ES 2.0" : "ANGLE D3D9"));
    return 2;
}

int LuaHyperSmoothGetVbPoolStats(void* L) {
    if (!g_luaPushNumber) return 0;
    const auto st = g_vbPool.GetStats();
    g_luaPushNumber(L, static_cast<double>(st.hits));
    g_luaPushNumber(L, static_cast<double>(st.misses));
    g_luaPushNumber(L, static_cast<double>(st.recycled));
    g_luaPushNumber(L, static_cast<double>(st.activeCount));
    g_luaPushNumber(L, static_cast<double>(st.pooledCount));
    g_luaPushNumber(L, static_cast<double>(st.pooledBytes) / 1024.0);
    return 6;
}

int LuaHyperSmoothGetNetStats(void* L) {
    if (!g_luaPushBoolean || !g_luaPushNumber) return 0;
    g_luaPushBoolean(L, g_netConnected.load(std::memory_order_relaxed) ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(g_netPingAvg.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_netPingLast.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_netLossCur.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_netLossAvg.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_netResendMsgs.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_netTxBps.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_netRxBps.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(g_rpcQos.ImmCount()));
    g_luaPushNumber(L, static_cast<double>(g_rpcQos.OffCh0Count()));
    return 10;
}

int LuaHyperSmoothSetRpcChannelMasks(void* L) {
    if (!g_luaToNumber || !g_luaPushBoolean) return 0;
    const uint32_t srv0 = static_cast<uint32_t>(static_cast<int64_t>(g_luaToNumber(L, 1)));
    const uint32_t srv1 = static_cast<uint32_t>(static_cast<int64_t>(g_luaToNumber(L, 2)));
    const uint32_t srv2 = static_cast<uint32_t>(static_cast<int64_t>(g_luaToNumber(L, 3)));
    const uint32_t cli0 = static_cast<uint32_t>(static_cast<int64_t>(g_luaToNumber(L, 4)));
    const uint32_t cli1 = static_cast<uint32_t>(static_cast<int64_t>(g_luaToNumber(L, 5))); // optional; nil/omitted -> 0
    const uint64_t srvLo = static_cast<uint64_t>(srv0) | (static_cast<uint64_t>(srv1) << 32);
    const uint64_t srvHi = static_cast<uint64_t>(srv2);
    const uint64_t cliLo = static_cast<uint64_t>(cli0) | (static_cast<uint64_t>(cli1) << 32);
    g_rpcQos.SetMasks(srvLo, srvHi, cliLo, 0ULL);
    g_luaPushBoolean(L, 1);
    return 1;
}

int LuaHyperSmoothGetAsyncSaveStats(void* L) {
    if (!g_luaPushBoolean || !g_luaPushNumber) return 0;
    auto& q = GetAsyncSave();
    g_luaPushBoolean(L, g_asyncSaveActive.load(std::memory_order_acquire) ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(q.EnqueuedCount()));
    g_luaPushNumber(L, static_cast<double>(q.CommittedCount()));
    g_luaPushNumber(L, static_cast<double>(q.CommittedBytes()));
    g_luaPushNumber(L, static_cast<double>(q.CompressedCount()));
    g_luaPushNumber(L, static_cast<double>(q.CompressUsSum()) * 0.001);
    g_luaPushNumber(L, static_cast<double>(q.FlushCount()));
    g_luaPushNumber(L, static_cast<double>(g_asyncSaveFallbacks.load(std::memory_order_relaxed)));
    g_luaPushNumber(L, static_cast<double>(q.ErrorCount()));
    g_luaPushNumber(L, static_cast<double>(q.IoUsSum()) * 0.001);
    return 10;
}

int LuaHyperSmoothSetSpriteBatch(void* L) {
    if (!g_luaToBoolean || !g_luaPushBoolean) return 0;
    const bool en = (g_luaToBoolean(L, 1) != 0) && g_spriteBatchActive.load(std::memory_order_acquire);
    g_optSpriteBatch.store(en, std::memory_order_release);
    LogMsg("[PERF][SBATCH] Lua bridge -> SpriteBatch=%d (active=%d)",
           en ? 1 : 0, g_spriteBatchActive.load(std::memory_order_acquire) ? 1 : 0);
    g_luaPushBoolean(L, en ? 1 : 0);
    return 1;
}

int LuaHyperSmoothSetStatsHold(void* L) {
    if (!g_luaToBoolean) return 0;
    const int want = (g_luaToBoolean(L, 1) != 0) ? 1 : 0;
    const int prev = g_statsTransitionHold.exchange(want, std::memory_order_acq_rel);
    if (prev != 0 && want == 0 && g_qpcFreq > 0) {
        // 150ms settle window when returning from Pause / Modal Menu / Black-Fade back to HUD
        const int64_t graceEnd = NowQpc() + (g_qpcFreq * 150) / 1000;
        const int64_t prevGrace = g_statsResumeGraceUntilQpc.load(std::memory_order_relaxed);
        if (graceEnd > prevGrace) {
            g_statsResumeGraceUntilQpc.store(graceEnd, std::memory_order_relaxed);
        }
    }
    if (g_luaPushBoolean) {
        g_luaPushBoolean(L, want);
        return 1;
    }
    return 0;
}

int LuaHyperSmoothGetSpriteBatchStats(void* L) {
    if (!g_luaPushBoolean || !g_luaPushNumber) return 0;
    const bool active = g_spriteBatchActive.load(std::memory_order_acquire) &&
                        g_optSpriteBatch.load(std::memory_order_relaxed);
    const LONG64 drawsIn      = InterlockedCompareExchange64(&g_sbDrawsIn, 0, 0);
    const LONG64 drawsBatched = InterlockedCompareExchange64(&g_sbDrawsBatched, 0, 0);
    const LONG64 flushes      = InterlockedCompareExchange64(&g_sbFlushes, 0, 0);
    const LONG64 vertsBatched = InterlockedCompareExchange64(&g_sbVertsBatched, 0, 0);
    const LONG64 bypassDraws  = InterlockedCompareExchange64(&g_sbBypassDraws, 0, 0);
    g_luaPushBoolean(L, active ? 1 : 0);
    g_luaPushNumber(L, static_cast<double>(drawsIn));
    g_luaPushNumber(L, static_cast<double>(drawsBatched));
    g_luaPushNumber(L, static_cast<double>(flushes));
    g_luaPushNumber(L, static_cast<double>(vertsBatched));
    g_luaPushNumber(L, static_cast<double>(bypassDraws));
    g_luaPushNumber(L, static_cast<double>(g_sbProgsLinked.load(std::memory_order_relaxed)));
    return 7;
}

int LuaHyperSmoothVersion(void* L) {
    if (!g_luaPushString) return 0;
    g_luaPushString(L, "DST-HyperSmooth/2.9.6");
    return 1;
}

static int LuaHyperSmoothGetBlackbox(void* L) {
    if (!g_luaPushNumber || !g_luaPushString) return 0;
    int limit = 20;
    if (g_luaToNumber && (!g_clientLuaApi.gettop || g_clientLuaApi.gettop(L) >= 1)) {
        const int req = static_cast<int>(g_luaToNumber(L, 1));
        if (req > 0) limit = std::clamp(req, 1, static_cast<int>(hs_blackbox::kCap));
    }
    hs_blackbox::ClientEvent evs[hs_blackbox::kCap]{};
    uint64_t total = 0;
    AcquireSRWLockShared(&g_bbLock);
    const size_t n = g_clientBlackbox.SnapshotLatest(evs, static_cast<size_t>(limit), total);
    ReleaseSRWLockShared(&g_bbLock);

    const uint32_t threshMs = static_cast<uint32_t>(std::max(1, g_blackboxThresholdMs.load(std::memory_order_relaxed)));
    constexpr size_t kJsonCap = 32768;
    char* jsonBuf = static_cast<char*>(std::malloc(kJsonCap));
    size_t written = 0;
    if (jsonBuf) {
        hs_blackbox::FormatClientJson(jsonBuf, kJsonCap, evs, n, total, threshMs, &written);
        g_luaPushNumber(L, static_cast<double>(total));
        g_luaPushNumber(L, static_cast<double>(written));
        g_luaPushString(L, jsonBuf);
        std::free(jsonBuf);
    } else {
        char emptyBuf[128]{};
        std::snprintf(emptyBuf, sizeof(emptyBuf),
                      "{\"role\":\"client\",\"total\":%llu,\"count\":0,\"threshold_ms\":%u,\"events\":[]}",
                      static_cast<unsigned long long>(total), static_cast<unsigned>(threshMs));
        g_luaPushNumber(L, static_cast<double>(total));
        g_luaPushNumber(L, 0.0);
        g_luaPushString(L, emptyBuf);
    }
    return 3;
}

static int LuaHyperSmoothClearBlackbox(void* L) {
    AcquireSRWLockExclusive(&g_bbLock);
    g_clientBlackbox.Clear();
    ReleaseSRWLockExclusive(&g_bbLock);
    if (g_luaPushBoolean) {
        g_luaPushBoolean(L, 1);
        return 1;
    }
    return 0;
}

static int LuaHyperSmoothExportBlackbox(void* L) {
    if (!g_luaPushBoolean || !g_luaPushString) return 0;
    std::wstring outWPath;
    if (!g_logPath.empty()) {
        const size_t sep = g_logPath.find_last_of(L"\\/");
        outWPath = (sep != std::wstring::npos) ? g_logPath.substr(0, sep + 1) : L"";
    } else {
        outWPath = g_binDir;
    }
    outWPath += L"dst_hs_blackbox.json";

    char pathUtf8[MAX_PATH * 4]{};
    const int u8Len = WideCharToMultiByte(
        CP_UTF8, 0, outWPath.c_str(), -1, pathUtf8, static_cast<int>(sizeof(pathUtf8)), nullptr, nullptr);
    if (u8Len <= 0) {
        std::snprintf(pathUtf8, sizeof(pathUtf8), "dst_hs_blackbox.json");
    }

    hs_blackbox::ClientEvent evs[hs_blackbox::kCap]{};
    uint64_t total = 0;
    AcquireSRWLockShared(&g_bbLock);
    const size_t n = g_clientBlackbox.SnapshotLatest(evs, hs_blackbox::kCap, total);
    ReleaseSRWLockShared(&g_bbLock);

    const uint32_t threshMs = static_cast<uint32_t>(std::max(1, g_blackboxThresholdMs.load(std::memory_order_relaxed)));
    constexpr size_t kJsonCap = 32768;
    char* jsonBuf = static_cast<char*>(std::malloc(kJsonCap));
    bool ok = false;
    if (jsonBuf) {
        size_t written = 0;
        const size_t jsonLen = hs_blackbox::FormatClientJson(jsonBuf, kJsonCap, evs, n, total, threshMs, &written);
        if (jsonLen > 0) {
            if (FILE* f = _wfopen(outWPath.c_str(), L"wb")) {
                ok = (std::fwrite(jsonBuf, 1, jsonLen, f) == jsonLen);
                std::fclose(f);
            }
        }
        std::free(jsonBuf);
    }
    g_luaPushBoolean(L, ok ? 1 : 0);
    g_luaPushString(L, pathUtf8);
    return 2;
}

static int LuaHyperSmoothFastJsonEncode(void* L) {
    if (!g_clientLuaApi.ReadyForFastJson()) {
        if (g_clientLuaApi.pushnil) { g_clientLuaApi.pushnil(L); return 1; }
        return 0;
    }
    const int nargs = g_clientLuaApi.gettop(L);
    if (nargs < 1) {
        g_clientLuaApi.pushnil(L);
        return 1;
    }
    const bool compliant = (nargs >= 2 && g_clientLuaApi.type(L, 2) == HC_LUA_TBOOLEAN)
        ? (g_clientLuaApi.toboolean(L, 2) != 0) : false;
    fast_json::JsonOutBuf out;
    if (fast_json::EncodeRoot(L, g_clientLuaApi, 1, out, compliant) == fast_dumper::Status::Ok &&
        !out.oom && out.data) {
        InterlockedIncrement64(&g_clientFastJsonEncCalls);
        g_clientLuaApi.pushlstring(L, out.data, out.len);
        return 1;
    }
    InterlockedIncrement64(&g_clientFastJsonFallbacks);
    g_clientLuaApi.pushnil(L);
    return 1;
}

static int LuaHyperSmoothFastJsonDecode(void* L) {
    if (!g_clientLuaApi.ReadyForFastJson()) {
        if (g_clientLuaApi.pushboolean) { g_clientLuaApi.pushboolean(L, 0); return 1; }
        return 0;
    }
    const int baseTop = g_clientLuaApi.gettop(L);
    if (baseTop < 1 || g_clientLuaApi.type(L, 1) != HC_LUA_TSTRING) {
        InterlockedIncrement64(&g_clientFastJsonFallbacks);
        g_clientLuaApi.pushboolean(L, 0);
        return 1;
    }
    size_t slen = 0;
    const char* s = g_clientLuaApi.tolstring(L, 1, &slen);
    size_t nextPos = 1;
    g_clientLuaApi.pushboolean(L, 1); // placeholder ok=true before decoded value
    if (fast_json::DecodeRoot(L, g_clientLuaApi, s, slen, &nextPos) == fast_dumper::Status::Ok) {
        InterlockedIncrement64(&g_clientFastJsonDecCalls);
        g_clientLuaApi.pushnumber(L, static_cast<double>(nextPos));
        return 3; // (true, value, nextPos)
    }
    g_clientLuaApi.settop(L, baseTop);
    InterlockedIncrement64(&g_clientFastJsonFallbacks);
    g_clientLuaApi.pushboolean(L, 0);
    return 1;
}

static int LuaHyperSmoothSetClipboard(void* L) {
    if (!g_luaPushBoolean) return 0;
    bool ok = false;
    if (g_clientLuaApi.tolstring && g_clientLuaApi.type && g_clientLuaApi.type(L, 1) == HC_LUA_TSTRING) {
        size_t slen = 0;
        const char* utf8 = g_clientLuaApi.tolstring(L, 1, &slen);
        if (utf8 && slen > 0 && slen <= 16384) {
            const int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, static_cast<int>(slen), nullptr, 0);
            if (wlen > 0) {
                HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, static_cast<size_t>(wlen + 1) * sizeof(wchar_t));
                if (hMem) {
                    wchar_t* dst = static_cast<wchar_t*>(GlobalLock(hMem));
                    if (dst) {
                        MultiByteToWideChar(CP_UTF8, 0, utf8, static_cast<int>(slen), dst, wlen);
                        dst[wlen] = L'\0';
                        GlobalUnlock(hMem);
                        if (OpenClipboard(nullptr)) {
                            EmptyClipboard();
                            if (SetClipboardData(CF_UNICODETEXT, hMem) != nullptr) {
                                ok = true;
                                hMem = nullptr; // ownership transferred to OS clipboard
                            }
                            CloseClipboard();
                        }
                    }
                    if (hMem) GlobalFree(hMem);
                }
            }
        }
    }
    g_luaPushBoolean(L, ok ? 1 : 0);
    return 1;
}

static int LuaHyperSmoothExtRegisterProbe(void* L) {
    if (!L || !g_luaPushNumber || !g_clientLuaApi.tolstring || !g_clientLuaApi.type) return 0;
    if (g_clientLuaApi.type(L, 1) != HC_LUA_TSTRING) {
        g_luaPushNumber(L, -1.0);
        return 1;
    }
    size_t idLen = 0;
    const char* id = g_clientLuaApi.tolstring(L, 1, &idLen);
    const char* uni = nullptr;
    if (g_clientLuaApi.type(L, 2) == HC_LUA_TSTRING) {
        size_t uLen = 0;
        uni = g_clientLuaApi.tolstring(L, 2, &uLen);
    }
    const int32_t slot = UpsertExtProfSlot(id, uni);
    g_luaPushNumber(L, static_cast<double>(slot >= 0 ? (slot + 1) : -1));
    return 1;
}

static int LuaHyperSmoothExtGetGpuStats(void* L) {
    if (!L || !g_luaPushNumber || !g_luaPushBoolean || !g_clientLuaApi.tolstring || !g_clientLuaApi.type) return 0;
    int32_t idx = -1;
    if (g_clientLuaApi.type(L, 1) == HC_LUA_TSTRING) {
        size_t idLen = 0;
        const char* id = g_clientLuaApi.tolstring(L, 1, &idLen);
        idx = FindExtProfSlot(id);
    }
    if (idx < 0 || static_cast<uint32_t>(idx) >= kMaxExtProfSlots) {
        g_luaPushNumber(L, 0.0);
        g_luaPushNumber(L, 0.0);
        g_luaPushNumber(L, 0.0);
        g_luaPushNumber(L, 0.0);
        g_luaPushBoolean(L, 0);
        return 5;
    }
    const ExtProfSlot& slot = g_extProfSlots[idx];
    const uint32_t progs = slot.linkedProgs.load(std::memory_order_relaxed);
    const uint32_t draws = slot.drawsTotal.load(std::memory_order_relaxed);
    const int64_t lastQ  = slot.lastDrawQpc.load(std::memory_order_relaxed);
    const int64_t nowQ   = NowQpc();
    const double staleUs = (lastQ > 0 && nowQ > lastQ) ? static_cast<double>(nowQ - lastQ) * g_qpcToUs : 1e9;
    const float emaMs    = (staleUs < 500000.0) ? slot.emaMs.load(std::memory_order_relaxed) : 0.0f;
    const float peakMs   = slot.peakMs.load(std::memory_order_relaxed);
    const bool  isHw     = slot.hwGpu.load(std::memory_order_relaxed);
    g_luaPushNumber(L, static_cast<double>(progs));
    g_luaPushNumber(L, static_cast<double>(emaMs));
    g_luaPushNumber(L, static_cast<double>(peakMs));
    g_luaPushNumber(L, static_cast<double>(draws));
    g_luaPushBoolean(L, isHw ? 1 : 0);
    return 5;
}

static int __cdecl LuaHyperSmoothExtResetStats(void* L) {
    ResetExtProfMetricsOnly();
    if (L && g_luaPushBoolean) {
        g_luaPushBoolean(L, 1);
        return 1;
    }
    return 0;
}

// v2.9.5 M2: HyperSmooth_SetClipCursor(enabled [, allowGameplay]) -> activeNow
static int __cdecl LuaHyperSmoothSetClipCursor(void* L) {
    if (!g_luaToBoolean || !g_luaPushBoolean) return 0;
    g_luaBridgeActive.store(1, std::memory_order_release);
    const bool en = (g_luaToBoolean(L, 1) != 0);
    bool allowG = g_cursorClip.IsAllowGameplay();
    if (g_clientLuaApi.type) {
        const int t2 = g_clientLuaApi.type(L, 2);
        if (t2 > 0) { // not LUA_TNONE(-1) and not LUA_TNIL(0)
            allowG = (g_luaToBoolean(L, 2) != 0);
        }
    } else if (g_luaToBoolean(L, 2) != 0) {
        allowG = true;
    }
    g_cursorClip.SetConfig(en, allowG);
    const bool active = g_cursorClip.Update(g_nativeGameHwnd);
    g_luaPushBoolean(L, active ? 1 : 0);
    return 1;
}

static int __cdecl LuaHyperSmoothGetClipCursor(void* L) {
    if (!g_luaPushBoolean) return 0;
    g_luaPushBoolean(L, g_cursorClip.IsEnabled() ? 1 : 0);
    g_luaPushBoolean(L, g_cursorClip.IsAllowGameplay() ? 1 : 0);
    g_luaPushBoolean(L, g_cursorClip.IsClippedNow() ? 1 : 0);
    return 3;
}

// v2.9.5 M2: HyperSmooth_ReportLuaCrash(modId, modName, summary)
static int __cdecl LuaHyperSmoothReportLuaCrash(void* L) {
    if (!g_clientLuaApi.tolstring || !g_clientLuaApi.type) return 0;
    auto readStr = [&](int idx) -> const char* {
        if (g_clientLuaApi.type(L, idx) != 4 /*LUA_TSTRING*/) return "";
        size_t len = 0;
        const char* s = g_clientLuaApi.tolstring(L, idx, &len);
        return s ? s : "";
    };
    g_crashDetective.ReportLuaError(NowEpochMs(), readStr(1), readStr(2), readStr(3));
    return 0;
}

// Unified Main-Thread PLL Snapshot-Timeline Camera Target Smoother:
// HyperSmooth_SmoothCameraTarget(rx, ry, rz, resetFlag) -> sx, sy, sz, active
// Evaluates on the EXACT same Main-Thread frame clock (g_frameSeq) and SmoothTimelineTable as
// HookAnimRenderCmdCtor, DynamicShadow, and LightMap -> 0.00px character-vs-camera phase error!
static int __cdecl LuaHyperSmoothSmoothCameraTarget(void* L) {
    if (!g_luaToNumber || !g_luaPushNumber) return 0;
    const float rx = static_cast<float>(g_luaToNumber(L, 1));
    const float ry = static_cast<float>(g_luaToNumber(L, 2));
    const float rz = static_cast<float>(g_luaToNumber(L, 3));
    const bool resetFlag = (g_luaToNumber(L, 4) != 0.0);

    static constexpr uintptr_t kCamTargetKey = 0xCAFE000000000007ULL;
    ThreadLocalAnimState& tls = GetTlsAnimState();

    if (resetFlag || !std::isfinite(rx) || !std::isfinite(ry) || !std::isfinite(rz) ||
        g_worldPosInterpEnabled.load(std::memory_order_relaxed) == 0 ||
        g_skeletalTweenEnabled.load(std::memory_order_relaxed) == 0) {
        if (resetFlag) {
            tls.posTimeline.ResetKey(kCamTargetKey);
        }
        g_luaPushNumber(L, static_cast<double>(rx));
        g_luaPushNumber(L, static_cast<double>(ry));
        g_luaPushNumber(L, static_cast<double>(rz));
        if (g_luaPushBoolean) g_luaPushBoolean(L, 0);
        return g_luaPushBoolean ? 4 : 3;
    }

    uint64_t frameSeq = 0;
    double nowSec = 0.0, dtSec = 0.005;
    tls.GetPosClock(frameSeq, nowSec, dtSec, false);

    float sx = rx, sy = ry, sz = rz;
    const bool active = tls.posTimeline.Evaluate(
        kCamTargetKey, rx, ry, rz,
        frameSeq, nowSec, dtSec,
        g_clientSimTickRate.load(std::memory_order_relaxed),
        g_clientSimTickGen.load(std::memory_order_relaxed),
        nullptr,
        g_optAdaptiveDeJitter.load(std::memory_order_relaxed),
        sx, sy, sz, nullptr);

    g_luaPushNumber(L, static_cast<double>(sx));
    g_luaPushNumber(L, static_cast<double>(sy));
    g_luaPushNumber(L, static_cast<double>(sz));
    if (g_luaPushBoolean) g_luaPushBoolean(L, active ? 1 : 0);
    return g_luaPushBoolean ? 4 : 3;
}

static int __cdecl LuaHyperSmoothGetCrashReportPath(void* L) {
    if (!g_luaPushString) return 0;
    g_luaPushString(L, g_crashDetective.ReportPathUtf8());
    return 1;
}

// HyperShade Visual Closed-Loop Native Bridge:
//   HyperSmooth_StartLookMatch(modeStr [, filePathUtf8]) -> ok, jobIdOrErr
//   HyperSmooth_PollLookMatch()                          -> stateStr [, resultTableOrErr]
//   HyperSmooth_GetLookCapabilities()                    -> capabilityWhitelistTable
static int __cdecl LuaHyperSmoothStartLookMatch(void* L) {
    if (!g_luaPushBoolean || !g_luaPushString) return 0;
    std::string modeStr = "clipboard";
    std::string fileUtf8;
    if (g_clientLuaApi.tolstring && g_clientLuaApi.type) {
        if (g_clientLuaApi.type(L, 1) == HC_LUA_TSTRING) {
            size_t len = 0;
            const char* s = g_clientLuaApi.tolstring(L, 1, &len);
            if (s && len > 0) modeStr.assign(s, len);
        }
        if (g_clientLuaApi.type(L, 2) == HC_LUA_TSTRING) {
            size_t len = 0;
            const char* s = g_clientLuaApi.tolstring(L, 2, &len);
            if (s && len > 0) fileUtf8.assign(s, len);
        }
    }
    for (char& c : modeStr) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }

    std::string err;
    uint32_t jobId = 0u;
    bool ok = false;
    const bool inGameplay = (g_inGameplay.load(std::memory_order_acquire) != 0);

    if (modeStr == "same_frame" || modeStr == "frame") {
        ok = g_lookMatcher.StartSameFrameJob(err, jobId);
    } else {
        hs_look_match::FitModeRequest req = hs_look_match::FitModeRequest::Auto;
        if (modeStr == "general" || modeStr == "clipboard_general" || modeStr == "file_general") {
            req = hs_look_match::FitModeRequest::General;
        } else if (modeStr == "exact" || modeStr == "same_scene" || modeStr == "clipboard_exact" || modeStr == "file_exact") {
            req = hs_look_match::FitModeRequest::SameScene;
        }
        std::wstring widePath;
        const std::wstring* pathPtr = nullptr;
        if (!fileUtf8.empty()) {
            const int wlen = MultiByteToWideChar(CP_UTF8, 0, fileUtf8.c_str(), static_cast<int>(fileUtf8.size()), nullptr, 0);
            if (wlen > 0 && wlen < 32760) {
                widePath.resize(static_cast<size_t>(wlen));
                MultiByteToWideChar(CP_UTF8, 0, fileUtf8.c_str(), static_cast<int>(fileUtf8.size()), widePath.data(), wlen);
                pathPtr = &widePath;
            }
        }
        const bool wantLiveCapture = inGameplay && (g_origGlReadPixels != nullptr);
        ok = g_lookMatcher.StartClipboardOrFileJob(req, pathPtr, wantLiveCapture, err, jobId);
    }

    g_luaPushBoolean(L, ok ? 1 : 0);
    if (ok && g_luaPushNumber) {
        g_luaPushNumber(L, static_cast<double>(jobId));
    } else {
        g_luaPushString(L, err.empty() ? "look_match_start_failed" : err.c_str());
    }
    return 2;
}

static int __cdecl LuaHyperSmoothPollLookMatch(void* L) {
    if (!g_luaPushString) return 0;
    hs_look_match::LookMatchResult res{};
    std::string err;
    const auto st = g_lookMatcher.Poll(res, err);

    switch (st) {
        case hs_look_match::JobState::Idle:
            g_luaPushString(L, "idle");
            return 1;
        case hs_look_match::JobState::Capturing:
            g_luaPushString(L, "capturing");
            return 1;
        case hs_look_match::JobState::Solving:
            g_luaPushString(L, "solving");
            return 1;
        case hs_look_match::JobState::Error:
            g_lookMatcher.ResetToIdle();
            g_luaPushString(L, "error");
            g_luaPushString(L, err.empty() ? "unknown_error" : err.c_str());
            return 2;
        case hs_look_match::JobState::Done:
            break;
    }

    g_lookMatcher.ResetToIdle();
    if (!g_clientLuaApi.createtable || !g_clientLuaApi.setfield || !g_luaPushNumber || !g_luaPushBoolean) {
        g_luaPushString(L, "error");
        g_luaPushString(L, "lua_table_api_unavailable");
        return 2;
    }

    g_luaPushString(L, "done");
    g_clientLuaApi.createtable(L, 0, 18);
    auto setStr = [&](const char* k, const std::string& v) {
        g_luaPushString(L, v.c_str());
        g_clientLuaApi.setfield(L, -2, k);
    };
    auto setNum = [&](const char* k, double v) {
        g_luaPushNumber(L, v);
        g_clientLuaApi.setfield(L, -2, k);
    };
    auto setBool = [&](const char* k, bool v) {
        g_luaPushBoolean(L, v ? 1 : 0);
        g_clientLuaApi.setfield(L, -2, k);
    };

    setStr("mode",           res.usedMode);
    setStr("input_source",   res.inputSource);
    setStr("readback_path",  res.readbackPath);
    setStr("warning",        res.warningCode);
    setNum("ref_width",      static_cast<double>(res.refWidth));
    setNum("ref_height",     static_cast<double>(res.refHeight));
    setNum("elapsed_ms",     static_cast<double>(res.elapsedMs));
    setNum("zncc",           static_cast<double>(res.zncc));
    setNum("grad_corr",      static_cast<double>(res.gradCorr));
    setNum("delta_e_before", static_cast<double>(res.deltaEBefore));
    setNum("delta_e_after",  static_cast<double>(res.deltaEAfter));
    setNum("confidence",         static_cast<double>(res.overallConfidence));
    setNum("color_confidence",   static_cast<double>(res.colorConfidence));
    setNum("spatial_confidence", static_cast<double>(res.spatialConfidence));
    setBool("vignette_fitted",   res.vignetteFitted);
    setBool("sharp_fitted",      res.sharpFitted);

    g_clientLuaApi.createtable(L, 0, hs_look_match::kParamCount);
    for (int i = 0; i < hs_look_match::kParamCount; ++i) {
        const auto& spec = hs_look_match::kCapabilityWhitelist[i];
        // Emit sharp ONLY when actually fitted (Same-Scene mode); keep General mode crisp!
        if (i == hs_look_match::kP_Sharp && !res.sharpFitted) continue;
        g_luaPushNumber(L, static_cast<double>(res.params.v[i]));
        g_clientLuaApi.setfield(L, -2, spec.key);
    }
    g_clientLuaApi.setfield(L, -2, "params");
    return 2;
}

static int __cdecl LuaHyperSmoothGetLookCapabilities(void* L) {
    if (!g_clientLuaApi.createtable || !g_clientLuaApi.setfield || !g_clientLuaApi.rawseti ||
        !g_luaPushString || !g_luaPushNumber || !g_luaPushBoolean) {
        return 0;
    }
    g_clientLuaApi.createtable(L, hs_look_match::kParamCount, 0);
    for (int i = 0; i < hs_look_match::kParamCount; ++i) {
        const auto& spec = hs_look_match::kCapabilityWhitelist[i];
        g_clientLuaApi.createtable(L, 0, 7);
        g_luaPushString(L, spec.key);                  g_clientLuaApi.setfield(L, -2, "key");
        g_luaPushNumber(L, spec.minVal);               g_clientLuaApi.setfield(L, -2, "min");
        g_luaPushNumber(L, spec.maxVal);               g_clientLuaApi.setfield(L, -2, "max");
        g_luaPushNumber(L, spec.defVal);               g_clientLuaApi.setfield(L, -2, "default");
        g_luaPushBoolean(L, spec.observableGeneral ? 1 : 0);   g_clientLuaApi.setfield(L, -2, "general");
        g_luaPushBoolean(L, spec.observableSameScene ? 1 : 0); g_clientLuaApi.setfield(L, -2, "same_scene");
        g_luaPushNumber(L, static_cast<double>(spec.stage));   g_clientLuaApi.setfield(L, -2, "stage");
        g_clientLuaApi.rawseti(L, -2, i + 1);
    }
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
    reg("HyperSmooth_SetAnimFluid", &LuaHyperSmoothSetAnimFluid);
    reg("HyperSmooth_SmoothCameraTarget", &LuaHyperSmoothSmoothCameraTarget);
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
    reg("HyperSmooth_SetDetailParams", &LuaHyperSmoothSetDetailParams);
    reg("HyperSmooth_SetCombatConfig", &LuaHyperSmoothSetCombatConfig);
    reg("HyperSmooth_TriggerImpact", &LuaHyperSmoothTriggerImpact);
    reg("HyperSmooth_SetWorldStudio", &LuaHyperSmoothSetWorldStudio);
    reg("HyperSmooth_SetShadowDyn", &LuaHyperSmoothSetShadowDyn);
    reg("HyperSmooth_SetShadowConfig", &LuaHyperSmoothSetShadowConfig);
    reg("HyperSmooth_SetPointLights", &LuaHyperSmoothSetPointLights);
    reg("HyperSmooth_SetWaterRefl", &LuaHyperSmoothSetWaterRefl);
    reg("HyperSmooth_SetOceanPbr", &LuaHyperSmoothSetOceanPbr);
    reg("HyperSmooth_SetWindField", &LuaHyperSmoothSetWindField);
    reg("HyperSmooth_SetFoliageAnchors", &LuaHyperSmoothSetFoliageAnchors);
    reg("HyperSmooth_SetClipCursor", &LuaHyperSmoothSetClipCursor);
    reg("HyperSmooth_GetClipCursor", &LuaHyperSmoothGetClipCursor);
    reg("HyperSmooth_GetWorldStudioStatus", &LuaHyperSmoothGetWorldStudioStatus);
    reg("HyperSmooth_GetVisualPerf", &LuaHyperSmoothGetVisualPerf);
    reg("HyperSmooth_GetHostPerf", &LuaHyperSmoothGetHostPerf);
    reg("HyperSmooth_GetRenderBackend", &LuaHyperSmoothGetRenderBackend);
    reg("HyperSmooth_GetVbPoolStats", &LuaHyperSmoothGetVbPoolStats);
    reg("HyperSmooth_GetNetStats", &LuaHyperSmoothGetNetStats);
    reg("HyperSmooth_SetRpcChannelMasks", &LuaHyperSmoothSetRpcChannelMasks);
    reg("HyperSmooth_GetAsyncSaveStats", &LuaHyperSmoothGetAsyncSaveStats);
    reg("HyperSmooth_SetSpriteBatch", &LuaHyperSmoothSetSpriteBatch);
    reg("HyperSmooth_GetSpriteBatchStats", &LuaHyperSmoothGetSpriteBatchStats);
    reg("HyperSmooth_SetStatsHold", &LuaHyperSmoothSetStatsHold);
    if (g_clientLuaApi.ReadyForFastDump()) {
        reg("HyperSmooth_FastDump", &LuaHyperSmoothFastDump);
    }
    if (g_clientLuaApi.ReadyForFastJson()) {
        reg("HyperSmooth_FastJsonEncode", &LuaHyperSmoothFastJsonEncode);
        reg("HyperSmooth_FastJsonDecode", &LuaHyperSmoothFastJsonDecode);
    }
    if (g_luaPushString) {
        reg("HyperSmooth_Version", &LuaHyperSmoothVersion);
        reg("HyperSmooth_GetBlackbox", &LuaHyperSmoothGetBlackbox);
        reg("HyperSmooth_ExportBlackbox", &LuaHyperSmoothExportBlackbox);
        reg("HyperSmooth_GetCrashReportPath", &LuaHyperSmoothGetCrashReportPath);
    }
    reg("HyperSmooth_ClearBlackbox", &LuaHyperSmoothClearBlackbox);
    if (g_clientLuaApi.tolstring && g_clientLuaApi.type) {
        reg("HyperSmooth_SetClipboard", &LuaHyperSmoothSetClipboard);
        reg("HyperSmooth_ExtRegisterProbe", &LuaHyperSmoothExtRegisterProbe);
        reg("HyperSmooth_ExtGetGpuStats", &LuaHyperSmoothExtGetGpuStats);
        reg("HyperSmooth_ReportLuaCrash", &LuaHyperSmoothReportLuaCrash);
    }
    reg("HyperSmooth_ExtResetStats", &LuaHyperSmoothExtResetStats);
    if (g_clientLuaApi.createtable && g_clientLuaApi.setfield && g_luaPushString && g_luaPushNumber && g_luaPushBoolean) {
        reg("HyperSmooth_StartLookMatch", &LuaHyperSmoothStartLookMatch);
        reg("HyperSmooth_PollLookMatch", &LuaHyperSmoothPollLookMatch);
        if (g_clientLuaApi.rawseti) {
            reg("HyperSmooth_GetLookCapabilities", &LuaHyperSmoothGetLookCapabilities);
        }
    }
}

void HookLuaOpenLibs(void* L) {
    // CRITICAL SAFETY INVARIANT: every fresh Lua VM (Main Menu, Public Server, Shard Switch)
    // unconditionally resets Client SimTick back to vanilla 30Hz so 60Hz can NEVER leak,
    // and temporarily unlocks unlimited Texture Streaming upload speed during loading screens!
    g_inGameplay.store(0, std::memory_order_release);
    g_worldResetGen.fetch_add(1u, std::memory_order_release);
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
        g_hd2dState.params[0]   = 0.0f;
        g_hd2dState.params[1]   = 0.0f;
        g_hd2dState.params[2]   = 0.0f;
        g_hd2dState.sun[2]      = 0.0f;
        g_hd2dState.shade[0]    = 0.0f;
        g_hd2dState.ocean[0]    = 0.0f;
        g_hd2dState.shadProj[3] = 0.0f;
        g_hd2dState.shadDyn[0]  = 0.0f;
        g_hd2dState.shadDyn[1]  = 0.45f;
        g_hd2dState.shadDyn[2]  = 0.55f;
        g_hd2dState.shadDyn[3]  = 1.0f;
        g_hd2dState.shadowDensity   = 0.0f;
        g_hd2dState.pointLightCount = 0u;
        g_hd2dState.waterReflStr    = 0.0f;
        g_hd2dState.detail[0] = 0.0f;
        g_hd2dState.detail[1] = 0.0f;
        g_hd2dState.detail[2] = 0.0f;
        g_hd2dState.detail[3] = 0.0f;
        g_hd2dState.oceanPbr[0] = 0.0f;
        g_hd2dState.oceanPbr[1] = 0.0f;
        g_hd2dState.oceanPbr[2] = 0.0f;
        g_hd2dState.oceanPbr[3] = 0.0f;
        ReleaseSRWLockExclusive(&g_hd2dStateLock);
        g_hd2dMasterStr.store(0.0f, std::memory_order_relaxed);
        g_hd2dStateGen.fetch_add(1, std::memory_order_release);
        {
            AcquireSRWLockExclusive(&g_windLock);
            g_windState = {};
            ReleaseSRWLockExclusive(&g_windLock);
            g_windStateGen.fetch_add(1, std::memory_order_release);
            AcquireSRWLockExclusive(&g_foliageLock);
            g_foliageStage.Clear();
            ReleaseSRWLockExclusive(&g_foliageLock);
            g_foliageTableGen.fetch_add(1, std::memory_order_release);
        }
        g_cursorClip.SetConfig(g_cursorClip.IsEnabled(), false);
        g_combatImpact.SetConfig(false, 1.0f, 1.0f, 1.0f, 1.0f);
        g_visualMipLodBias.store(0.0f, std::memory_order_relaxed);
        ResetExtProfMetricsOnly();
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
    static constexpr int16_t kAobCLuaCreateTable[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x48, 0x83, 0xEC, 0x20, 0x80, 0xB9, 0xC0, 0x00, 0x00, 0x00, 0x00, 0x41, 0x8B, 0xF0, 0x8B, 0xEA, 0x48, 0x8B, 0xF9
    };
    static constexpr int16_t kAobCLuaRawSet[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
        0x8B, 0xF9, 0xE8, -1, -1, -1, -1, 0x48, 0x8B, 0x5F, 0x10, 0x48, 0x8B, 0x10, 0x4C, 0x8D,
        0x43, 0xE0, 0x48, 0x8B, 0xCF, 0x48, 0x8B, 0xF0
    };
    static constexpr int16_t kAobCLuaRawSetI[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x45,
        0x8B, 0xD0, 0x48, 0x8B, 0xF9, 0xE8, -1, -1, -1, -1, 0x48, 0x8B, 0x10, 0x48, 0x8B, 0x5F,
        0x10, 0x45, 0x8B, 0xC2
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
    g_clientLuaApi.createtable  = reinterpret_cast<decltype(g_clientLuaApi.createtable)>(findU16(kAobCLuaCreateTable));
    g_clientLuaApi.setfield     = g_luaSetField;
    g_clientLuaApi.next         = reinterpret_cast<decltype(g_clientLuaApi.next)>(findU16(kAobCLuaNext));
    g_clientLuaApi.rawset       = reinterpret_cast<decltype(g_clientLuaApi.rawset)>(findU16(kAobCLuaRawSet));
    g_clientLuaApi.rawseti      = reinterpret_cast<decltype(g_clientLuaApi.rawseti)>(findU16(kAobCLuaRawSetI));
    LogMsg("[HOST] Client C++ Lua API ready: FastDataDumper=%d FastJson=%d",
           g_clientLuaApi.ReadyForFastDump() ? 1 : 0,
           g_clientLuaApi.ReadyForFastJson() ? 1 : 0);

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
        "; DST HyperSmooth & FastLoad Native Companion Plugin Configuration (v2.8.0)\n"
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
        "enable_rpc_qos=1\n"
        "enable_async_save=0\n"
        "render_backend=auto\n"
        "dxgi_flip_present=off\n"
        "enable_vb_pool=1\n"
        "enable_sprite_batch=1\n"
        "\n"
        "[VISUAL]\n"
        "; Player-First Default: 100%% Vanilla Pure (all visual enhancements strictly opt-in)\n"
        "anisotropic_filter=1\n"
        "fullres_lightmap=0\n"
        "mip_lod_sharp=0\n"
        "hd2d_world_shaders=1\n"
        "\n"
        "[TELEMETRY]\n"
        "enable_blackbox=1\n"
        "blackbox_threshold_ms=25\n");
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

    LogMsg("DST HyperSmooth & FastLoad Native Companion Plugin v2.8.0 (240Hz Skeletal+VFX + Native GL/VBPool + RPC QoS + AsyncSave + RTSS Pacer + mimalloc + Player-First Visual Fidelity) loaded.");

    // Initialize QPC frequency scale BEFORE installing any timing/pacer/GC/anim hooks
    {
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        g_qpcFreq = (freq.QuadPart > 0) ? freq.QuadPart : 10000000;
        g_qpcInvFreq = 1.0 / static_cast<double>(g_qpcFreq);
        g_qpcToUs = 1000000.0 / static_cast<double>(g_qpcFreq);
        g_qpcOrigin = NowQpc();
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
    bool optRpcQos = ReadIniInt(L"OPTIMIZE", L"enable_rpc_qos", 1) != 0;
    bool optAsyncSave = ReadIniInt(L"OPTIMIZE", L"enable_async_save", 0) != 0;
    wchar_t optBackendBuf[32] = L"auto";
    GetPrivateProfileStringW(L"OPTIMIZE", L"render_backend", L"auto", optBackendBuf, 32, g_iniPath.c_str());
    wchar_t optDxgiFlipBuf[32] = L"off";
    GetPrivateProfileStringW(L"OPTIMIZE", L"dxgi_flip_present", L"off", optDxgiFlipBuf, 32, g_iniPath.c_str());
    bool optVbPool = ReadIniInt(L"OPTIMIZE", L"enable_vb_pool", 1) != 0;
    bool optSpriteBatch = ReadIniInt(L"OPTIMIZE", L"enable_sprite_batch", 1) != 0;
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
    bool optBlackbox = ReadIniInt(L"TELEMETRY", L"enable_blackbox", 1) != 0;
    int  bbThreshMs = std::clamp(ReadIniInt(L"TELEMETRY", L"blackbox_threshold_ms", 25), 5, 500);
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
    g_optRpcQos.store(optRpcQos, std::memory_order_relaxed);
    g_optAsyncSave.store(optAsyncSave, std::memory_order_relaxed);
    g_cfgRenderBackend.store(static_cast<int>(hs_native_gl::ParseBackendMode(optBackendBuf)), std::memory_order_release);
    g_cfgDxgiFlipPresent.store(static_cast<int>(hs_dxgi_flip::ParseFlipMode(optDxgiFlipBuf)), std::memory_order_release);
    g_optVbPool.store(optVbPool, std::memory_order_release);
    g_optSpriteBatch.store(optSpriteBatch, std::memory_order_release);
    g_visualAnisoLevel.store(optAniso, std::memory_order_relaxed);
    g_visualFullresLightmap.store(optFullresLm, std::memory_order_relaxed);
    g_visualMipLodBias.store(optMipSharp ? -0.45f : 0.0f, std::memory_order_relaxed);
    g_optHd2dWorldShaders.store(optHd2dWorld, std::memory_order_release);
    g_optBlackbox.store(optBlackbox, std::memory_order_relaxed);
    g_blackboxThresholdMs.store(bbThreshMs, std::memory_order_relaxed);

    // Install libGLESv2 + KERNEL32 PE IAT hooks FIRST (at ~t=+1ms, BEFORE AOB scan) so glShaderSource
    // / glLinkProgram / glUseProgram and Mod Negative Stat Cache are live before DST loads bundles/shaders!
    InstallGlIatHooks();
    InstallFsNegCacheHooks();
    InstallDiagnosticsAndUtilityIatHooks();

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
        patches.reserve(44);

        PrepareVisualFidelityHooks(patches, textStart, textSize);
        PrepareRenderPerfHooks(patches, textStart, textSize);
        PrepareLightMap240AndHotSwapHooks(patches, textStart, textSize);
        PrepareDynamicShadow240Hook(patches, textStart, textSize);
        PrepareAnimCmdMainThreadPosHook(patches, textStart, textSize);
        PrepareLuaAndNetTelemetryHooks(patches, textStart, textSize);
        if (optRpcQos) PrepareRpcQosHook(patches, textStart, textSize);
        if (optAsyncSave && g_asyncBarriersReady.load(std::memory_order_acquire)) {
            PrepareAsyncSaveHooks(patches, textStart, textSize);
        }
        if (optVbPool) PrepareVbPoolHook(patches, textStart, textSize);
        if (optMimallocLua) PrepareClientMimallocHook(patches, textStart, textSize);
        if (optFastVec) PrepareVectorMemsetPatch(patches, textStart, textSize);
        if (optLibdeflate) {
            PrepareLibdeflateUnzHook(patches, textStart, textSize);
            PrepareClientUncompressHook(patches, textStart, textSize);
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
        if (isApplied(g_animCmdCtorPatchIdx)) {
            g_animCmdCtorHooked.store(true, std::memory_order_release);
        }
        if (isApplied(g_light240PatchIdx)) {
            g_lightPos240Hooked.store(true, std::memory_order_release);
        }
        if (isApplied(g_shadow240PatchIdx)) {
            g_shadow240Hooked.store(true, std::memory_order_release);
        }
        if (isApplied(g_gameRenderPatchIdx) && isApplied(g_postProcPatchIdx)) {
            g_renderPerfHooked = true;
        }
        if (isApplied(g_vbPoolPatchIdx)) {
            g_vbPoolHooked.store(true, std::memory_order_release);
        }
        if (isApplied(g_rpcQosPatchIdx)) {
            g_rpcQosHooked.store(true, std::memory_order_release);
        }
        if (isApplied(g_writePersistPatchIdx) && g_asyncBarriersReady.load(std::memory_order_acquire)) {
            g_asyncSaveActive.store(true, std::memory_order_release);
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
                LONG64 splBones = InterlockedCompareExchange64(&g_animSplineBones, 0, 0);
                LONG64 xfStarts = InterlockedCompareExchange64(&g_animCrossfadeStarts, 0, 0);
                LONG64 xfFrames = InterlockedCompareExchange64(&g_animCrossfadeFrames, 0, 0);
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
                LONG64 light240  = InterlockedCompareExchange64(&g_lightPos240Count, 0, 0);
                LONG64 shadow240 = InterlockedCompareExchange64(&g_shadowPos240Count, 0, 0);
                LONG64 uncompC   = InterlockedCompareExchange64(&g_clientUncompCalls, 0, 0);
                LONG64 uncompB   = InterlockedCompareExchange64(&g_clientUncompBytes, 0, 0);
                LONG64 jsonEnc   = InterlockedCompareExchange64(&g_clientFastJsonEncCalls, 0, 0);
                LONG64 jsonDec   = InterlockedCompareExchange64(&g_clientFastJsonDecCalls, 0, 0);
                LONG64 jsonFb    = InterlockedCompareExchange64(&g_clientFastJsonFallbacks, 0, 0);
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
                const auto vbSt = g_vbPool.GetStats();
                const int64_t vbTotal = vbSt.hits + vbSt.misses;
                const double vbPct = (vbTotal > 0) ? (100.0 * static_cast<double>(vbSt.hits) / static_cast<double>(vbTotal)) : 0.0;
                const LONG64 sbBat = InterlockedCompareExchange64(&g_sbDrawsBatched, 0, 0);
                const LONG64 sbFlu = InterlockedCompareExchange64(&g_sbFlushes, 0, 0);
                const double sbRed = (sbBat > 0 && sbBat >= sbFlu) ? (100.0 * static_cast<double>(sbBat - sbFlu) / static_cast<double>(sbBat)) : 0.0;
                const auto& asyncQ = GetAsyncSave();
                double avgFps = 0.0, low1Fps = 0.0, p99Ms = 0.0, jitterMs = 0.0;
                g_frameRing.ComputeStats(avgFps, low1Fps, p99Ms, jitterMs);
                LogMsg("[Summary] avg=%.0f 1%%L=%.0f p99=%.2fms jit=%.2fms sim=%dHz disp=%uHz | libdeflate=%lld (%.1fMB) uncomp=%lld (%.1fMB) | skeletal240=%lld (%lld bones, %lld spline, %lld swap, xfade=%lld/%lld, %lld shed) | vfx240=%lld (%lld pts) | light240=%lld shadow240=%lld | pos240=%lld (byp=%lld deJit=%lld/%.2fms) | d3d9ex_lat=%u/%u | glCache=%.1f%% (%lld/%lld) glUni=%.1f%% (%lld/%lld) sbatch=%.1f%%(%lld->%lld) fsNeg=%lld/%lld json=%lld/%lld/%lld lua=%.2fms net=%.2fms swap=%.2fms | spatial=%lld (%lld cells) | tex=%lld (slice=%lld) | visual(af=%dx/hw=%.0fx hits=%lld tri=%lld lm=%.1fx mip=%.2f hd2d(inj=%u progs=%u str=%.2f) busy=%.2fms world=%.2fms post=%.2fms gpu=%.2fms vram=%.0fMB/pk=%.0fMB) | gc(full=%lld defer=%lld) | mi_allocs=%lld | gl=%s vbPool=%.1f%% (%lld/%lld %uKB) | ping=%d/%dms loss=%.1f%% rpcQos=%lld/%lld asyncSave=%llu/%llu(c=%llu) combat=%u/%u(vfx=%u).",
                       avgFps, low1Fps, p99Ms, jitterMs, g_clientSimTickRate.load(std::memory_order_relaxed),
                       g_displayRefreshHz.load(std::memory_order_relaxed),
                       hits, static_cast<double>(bytes) / (1024.0 * 1024.0),
                       uncompC, static_cast<double>(uncompB) / (1024.0 * 1024.0),
                       interp, bones, splBones, p2, xfStarts, xfFrames, shed,
                       vfxCmds, vfxPts, light240, shadow240,
                       posInterp, posBypass, djH, djPk,
                       g_d3d9ExCurLatency.load(std::memory_order_relaxed),
                       g_d3d9ExOrigLatency.load(std::memory_order_relaxed),
                       glHitPct, glHits, glCalls,
                       uniPct, uniHits, uniCalls,
                       sbRed, sbBat, sbFlu,
                       fsH, fsQ, jsonEnc, jsonDec, jsonFb,
                       static_cast<double>(g_emaLuaUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaNetUs.load(std::memory_order_relaxed)) * 0.001,
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
                       gcFull, gcDef, miAlloc,
                       g_nativeGlActive.load(std::memory_order_acquire)
                           ? (GetDxgiFlip().IsActive() ? "opengl+dxgi" : "opengl")
                           : "d3d9",
                       vbPct, static_cast<long long>(vbSt.hits), static_cast<long long>(vbTotal),
                       static_cast<unsigned>(vbSt.pooledBytes / 1024),
                       g_netPingAvg.load(std::memory_order_relaxed),
                       g_netPingLast.load(std::memory_order_relaxed),
                       static_cast<double>(g_netLossCur.load(std::memory_order_relaxed)),
                       static_cast<long long>(g_rpcQos.ImmCount()),
                       static_cast<long long>(g_rpcQos.OffCh0Count()),
                       static_cast<unsigned long long>(asyncQ.EnqueuedCount()),
                       static_cast<unsigned long long>(asyncQ.CommittedCount()),
                       static_cast<unsigned long long>(asyncQ.CompressedCount()),
                       g_combatImpact.TotalTriggers(),
                       g_combatImpact.TotalBound(),
                       g_combatVfxDraws.load(std::memory_order_relaxed));
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
                char netBrkBuf[160] = "";
                if (snap.netUs >= 5000u) {
                    const bool isRpc = (snap.slowPktMeta & 0x80000000u) != 0u;
                    const unsigned pktId = (snap.slowPktMeta >> 23) & 0xFFu;
                    const unsigned pktBytes = snap.slowPktMeta & 0x7FFFFFu;
                    snprintf(netBrkBuf, sizeof(netBrkBuf),
                             " | netbrk=(rmUp=%.2f rmRx=%.2f[%u] rpc=%.2f[%u] slow=%s:0x%02x/%uB/%.1fms)",
                             static_cast<double>(snap.rmUpUs) * 0.001,
                             static_cast<double>(snap.rmRxUs) * 0.001, snap.rmRxCnt,
                             static_cast<double>(snap.rpcRxUs) * 0.001, snap.rpcRxCnt,
                             isRpc ? "RPC" : "RM3", pktId, pktBytes,
                             static_cast<double>(snap.slowPktUs) * 0.001);
                }
                LogMsg("[Hitch] peak=%.1fms (cnt=%u severe=%u[max=%.1fms] | avg=%.0f 1%%L=%.0f p99=%.1fms) | busy=%.1fms rt=%.1fms gpu=%.2fms lua=%.2fms net=%.2fms swap=%.1fms world=%.1fms post=%.1fms | loop=(wLua=%.2f eWall=%.2f eUpd=%.2f ePost=%.2f cRnd=%.2f ui=%.2f snd=%.2f stm=%.2f msg=%.2f) | anim=%.2fms (ents=%u bones=%u shed=%u) | gc=%.2fms (kb=%d full=%u/%lld) | tex=%.2fms (up=%lld slice=%lld) | zip=%.2fms shdr=%.2fms host=%.2fms | netstat=(ping=%d/%dms loss=%u.%u%% resend=%u) | spatial=%lld%s",
                       static_cast<double>(snap.peakUs) * 0.001, snap.pendingCount,
                       snap.severeCount, static_cast<double>(snap.severeMaxUs) * 0.001,
                       avgFps, low1Fps, p99Ms,
                       static_cast<double>(snap.busyUs) * 0.001,
                       static_cast<double>(snap.rtUs) * 0.001,
                       static_cast<double>(snap.gpuDrainUs) * 0.001,
                       static_cast<double>(snap.luaUs) * 0.001,
                       static_cast<double>(snap.netUs) * 0.001,
                       static_cast<double>(snap.swapUs) * 0.001,
                       static_cast<double>(snap.worldUs) * 0.001, static_cast<double>(snap.postUs) * 0.001,
                       static_cast<double>(snap.wallLuaUs) * 0.001,
                       static_cast<double>(snap.entWallUs) * 0.001,
                       static_cast<double>(snap.entUpdUs) * 0.001,
                       static_cast<double>(snap.entPostUs) * 0.001,
                       static_cast<double>(snap.cacheRndUs) * 0.001,
                       static_cast<double>(snap.uiDrawUs) * 0.001,
                       static_cast<double>(snap.soundUs) * 0.001,
                       static_cast<double>(snap.steamUs) * 0.001,
                       static_cast<double>(snap.msgUs) * 0.001,
                       static_cast<double>(snap.animUs) * 0.001, snap.animEnts, snap.animBones, snap.animShed,
                       static_cast<double>(snap.gcUs) * 0.001, snap.gcKb,
                       static_cast<unsigned>(snap.gcFull), gcFull,
                       static_cast<double>(snap.texUs) * 0.001, texUp, texSl,
                       static_cast<double>(snap.unzUs) * 0.001, static_cast<double>(snap.shaderUs) * 0.001,
                       static_cast<double>(snap.hostUs) * 0.001,
                       snap.pingAvgMs, snap.pingLastMs,
                       static_cast<unsigned>(snap.lossX10 / 10u), static_cast<unsigned>(snap.lossX10 % 10u),
                       snap.resendMsgs, spQ, netBrkBuf);
            }
        }

        // Compact 1-line steady-state full-budget accounting & SpriteBatcher flush histogram
        // (logs every 15s while avg < 235 or 1%L < 180, or every 30s at steady 240 FPS)
        static uint64_t s_lastPerfSnapTick = 0;
        if (g_inGameplay.load(std::memory_order_acquire) != 0 && (tickCounter - s_lastPerfSnapTick) >= 30) {
            double avgFps = 0.0, low1Fps = 0.0, p99Ms = 0.0, jitterMs = 0.0;
            g_frameRing.ComputeStats(avgFps, low1Fps, p99Ms, jitterMs);
            if (avgFps > 1.0 && (avgFps < 235.0 || low1Fps < 180.0 || (tickCounter - s_lastPerfSnapTick) >= 60)) {
                s_lastPerfSnapTick = tickCounter;
                const LONG64 sbBat = InterlockedCompareExchange64(&g_sbDrawsBatched, 0, 0);
                const LONG64 sbFlu = InterlockedCompareExchange64(&g_sbFlushes, 0, 0);
                const double sbPct = (sbBat > 0) ? (100.0 * static_cast<double>(sbBat - sbFlu) / static_cast<double>(sbBat)) : 0.0;
                const double sbAvg = (sbFlu > 0) ? (static_cast<double>(sbBat) / static_cast<double>(sbFlu)) : 0.0;
                const LONG64 uiBat = InterlockedCompareExchange64(&g_uiDrawsBatched, 0, 0);
                const LONG64 uiFlu = InterlockedCompareExchange64(&g_uiFlushes, 0, 0);
                const LONG64 uiByp = InterlockedCompareExchange64(&g_uiBypassDraws, 0, 0);
                const LONG64 uiSkp = InterlockedCompareExchange64(&g_uiProgSkips, 0, 0);
                const LONG64 uiRb  = InterlockedCompareExchange64(&g_uiReadbacks, 0, 0);
                const double uiPct = (uiBat > 0) ? (100.0 * static_cast<double>(uiBat - uiFlu) / static_cast<double>(uiBat)) : 0.0;
                const double uiAvg = (uiFlu > 0) ? (static_cast<double>(uiBat) / static_cast<double>(uiFlu)) : 0.0;
                using FR = hs_sprite_batch::FlushReason;
                auto fr = [](FR r) { return InterlockedCompareExchange64(&g_sbFlReason[static_cast<size_t>(r)], 0, 0); };
                auto sc = [](StateFlushCause c) { return InterlockedCompareExchange64(&g_sbStateCause[static_cast<size_t>(c)], 0, 0); };
                LogMsg("[PerfSnap] avg=%.0f 1%%L=%.0f p99=%.1fms | busy=%.2fms (lua=%.2f net=%.2f gc=%.2f world=%.2f[anim=%.2f ents=%u bones=%u] post=%.2f) | loop=(wLua=%.2f eWall=%.2f eUpd=%.2f ePost=%.2f cRnd=%.2f ui=%.2f snd=%.2f) | rt=%.2fms swap=%.2fms | sbatch=%.1f%%(avg=%.1f fl:p=%lld u4=%lld u3=%lld um=%lld uo=%lld st=%lld[bl=%lld dp=%lld cp=%lld sn=%lld po=%lld vp=%lld ti=%lld ot=%lld] tx=%lld fu=%lld end=%lld byp=%lld) | uibatch=%.1f%%(avg=%.1f %lld->%lld skp=%lld byp=%lld rb=%lld)",
                       avgFps, low1Fps, p99Ms,
                       static_cast<double>(g_emaFrameBusyUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaLuaUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaNetUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaGcUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaWorldRenderUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaAnimUs.load(std::memory_order_relaxed)) * 0.001,
                       g_lastAnimEnts.load(std::memory_order_relaxed),
                       g_lastAnimBones.load(std::memory_order_relaxed),
                       static_cast<double>(g_emaPostProcUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaWallLuaUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaEntWallUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaEntUpdUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaEntPostUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaCacheRndUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaUiDrawUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaSoundUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaRenderThreadUs.load(std::memory_order_relaxed)) * 0.001,
                       static_cast<double>(g_emaSwapUs.load(std::memory_order_relaxed)) * 0.001,
                       sbPct, sbAvg,
                       fr(FR::Prog), fr(FR::Uni4), fr(FR::Uni3), fr(FR::UniMat), fr(FR::UniOther),
                       fr(FR::State),
                       sc(StateFlushCause::Blend), sc(StateFlushCause::Depth), sc(StateFlushCause::Cap),
                       sc(StateFlushCause::Stencil), sc(StateFlushCause::PolyOff), sc(StateFlushCause::Viewport),
                       sc(StateFlushCause::TexImg), sc(StateFlushCause::Other),
                       fr(FR::Tex), fr(FR::Full), fr(FR::End), fr(FR::Bypass),
                       uiPct, uiAvg, uiBat, uiFlu, uiSkp, uiByp, uiRb);
            }
        }
        if ((tickCounter & 1u) == 0u) {
            double avgFps = 0.0, low1Fps = 0.0, p99Ms = 0.0, jitterMs = 0.0;
            g_frameRing.ComputeStats(avgFps, low1Fps, p99Ms, jitterMs);
            g_crashDetective.UpdateTelemetry(
                g_nativeGlActive.load(std::memory_order_acquire)
                    ? (GetDxgiFlip().IsActive() ? "opengl+dxgi" : "opengl")
                    : "d3d9",
                g_inGameplay.load(std::memory_order_acquire) != 0,
                static_cast<float>(avgFps), static_cast<float>(low1Fps),
                g_emaFrameBusyUs.load(std::memory_order_relaxed) * 0.001f,
                g_emaSwapUs.load(std::memory_order_relaxed) * 0.001f);
            g_cursorClip.Update(g_nativeGameHwnd);
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
                int rqos = ReadIniInt(L"OPTIMIZE", L"enable_rpc_qos", 1);
                int asav = ReadIniInt(L"OPTIMIZE", L"enable_async_save", 0);
                int vbp = ReadIniInt(L"OPTIMIZE", L"enable_vb_pool", 1);
                int sbat = ReadIniInt(L"OPTIMIZE", L"enable_sprite_batch", 1);
                int af = ReadIniInt(L"VISUAL", L"anisotropic_filter", 1);
                if (af < 1) af = 1;
                if (af > 16) af = 16;
                int flm = ReadIniInt(L"VISUAL", L"fullres_lightmap", 0);
                int mips = ReadIniInt(L"VISUAL", L"mip_lod_sharp", 0);
                int hd2d = ReadIniInt(L"VISUAL", L"hd2d_world_shaders", 1);
                int bbOn = ReadIniInt(L"TELEMETRY", L"enable_blackbox", 1);
                int bbMs = std::clamp(ReadIniInt(L"TELEMETRY", L"blackbox_threshold_ms", 25), 5, 500);
                g_optBlackbox.store(bbOn != 0, std::memory_order_relaxed);
                g_blackboxThresholdMs.store(bbMs, std::memory_order_relaxed);
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
                g_optRpcQos.store((rqos != 0) && g_rpcQosHooked.load(std::memory_order_acquire), std::memory_order_relaxed);
                g_optAsyncSave.store((asav != 0) && g_asyncSaveActive.load(std::memory_order_acquire), std::memory_order_relaxed);
                g_optVbPool.store((vbp != 0) && g_vbPoolHooked.load(std::memory_order_acquire), std::memory_order_relaxed);
                g_optSpriteBatch.store((sbat != 0) && g_spriteBatchActive.load(std::memory_order_acquire), std::memory_order_release);
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
        g_cursorClip.ReleaseUnconditionally(true);
        // Process terminating (lpReserved != nullptr): OS reclaims threads, timers, and DLLs atomically.
        // NEVER WaitForSingleObject / FreeLibrary / timeEndPeriod under Loader Lock!
        if (lpReserved != nullptr) {
            return TRUE;
        }
    }
    return TRUE;
}
