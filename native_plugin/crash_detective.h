#pragma once
// =============================================================================
// HyperSmooth M2: Crash Detective + Safe Dual-Monitor Cursor Lock (ClipCursor)
// =============================================================================
// 1. Crash Detective:
//    - Zero-heap-alloc exception & MiniDumpWriteDump interceptor writing a
//      human-readable bilingual `hypersmooth_crash_report.txt` containing:
//        * Exception code & human description
//        * Faulting module basename + RVA & smart blame hint (GPU/DST/Mod/HS)
//        * Native 24-frame stack backtrace (`RtlCaptureStackBackTrace`)
//        * Recent Lua error / Mod attribution ring (`modId`, `modName`, stack)
//    - Chains cleanly to previous UnhandledExceptionFilter & real MiniDumpWriteDump.
// 2. Dual-Monitor Cursor Lock (`ClipCursor`):
//    - Opt-in (default OFF); locks cursor ONLY when enabled + game HWND is
//      foreground + not minimized + active unpaused gameplay.
//    - Unconditionally releases `ClipCursor(nullptr)` on Alt+Tab, ESC/pause,
//      console open, focus loss, menu return, crash, or DLL_PROCESS_DETACH.
// =============================================================================

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace hs_crash_util {

inline constexpr size_t kLuaErrRingCap   = 8u;
inline constexpr size_t kMaxCrashFrames  = 24u;
inline constexpr size_t kCrashBufBytes   = 16384u;

struct LuaErrorEntry {
    uint64_t epochMs = 0;
    char     modId[48]{};
    char     modName[96]{};
    char     summary[320]{};
};

struct RuntimeTelemetry {
    char     backend[32] = "unknown";
    char     version[24] = "2.9.5";
    bool     inGameplay  = false;
    float    avgFps      = 0.0f;
    float    low1Fps     = 0.0f;
    float    busyMs      = 0.0f;
    float    swapMs      = 0.0f;
};

// =============================================================================
// Safe Dual-Monitor Cursor Lock (ClipCursor)
// =============================================================================
class CursorClipGuard {
public:
    static constexpr bool EvaluateShouldClip(bool enabled,
                                             bool allowGameplay,
                                             bool isForeground,
                                             bool isMinimized,
                                             bool rectValid) noexcept {
        return enabled && allowGameplay && isForeground && !isMinimized && rectValid;
    }

    void SetConfig(bool enabled, bool allowGameplay) noexcept {
        enabled_.store(enabled, std::memory_order_release);
        allowGameplay_.store(allowGameplay, std::memory_order_release);
        if (!enabled || !allowGameplay) {
            ReleaseUnconditionally(false);
        }
    }

    void SetGameHwnd(HWND hwnd) noexcept {
        if (hwnd && IsWindow(hwnd)) {
            gameHwnd_.store(hwnd, std::memory_order_release);
        }
    }

    bool IsEnabled() const noexcept { return enabled_.load(std::memory_order_acquire); }
    bool IsAllowGameplay() const noexcept { return allowGameplay_.load(std::memory_order_acquire); }
    bool IsClippedNow() const noexcept { return currentlyClipped_.load(std::memory_order_acquire); }

    bool Update(HWND hwndHint = nullptr) noexcept {
        if (hwndHint && IsWindow(hwndHint)) {
            gameHwnd_.store(hwndHint, std::memory_order_release);
        }
        const bool en  = enabled_.load(std::memory_order_acquire);
        const bool okG = allowGameplay_.load(std::memory_order_acquire);
        if (!en || !okG) {
            ReleaseUnconditionally(false);
            return false;
        }

        const DWORD selfPid = GetCurrentProcessId();
        HWND fg = GetForegroundWindow();
        DWORD fgPid = 0;
        if (fg) GetWindowThreadProcessId(fg, &fgPid);
        const bool fgIsSelf = (fg != nullptr && fgPid == selfPid);

        HWND target = gameHwnd_.load(std::memory_order_acquire);
        if (!target || !IsWindow(target)) {
            target = fgIsSelf ? fg : nullptr;
            if (target) gameHwnd_.store(target, std::memory_order_release);
        }
        const bool isFg   = fgIsSelf && (target != nullptr) && (fg == target);
        const bool isMin  = (target == nullptr) || (IsIconic(target) != FALSE);

        RECT screenRc{};
        bool rectOk = false;
        if (isFg && !isMin && target) {
            RECT clientRc{};
            if (GetClientRect(target, &clientRc)) {
                const LONG w = clientRc.right - clientRc.left;
                const LONG h = clientRc.bottom - clientRc.top;
                if (w >= 64 && h >= 64) {
                    POINT tl{ clientRc.left, clientRc.top };
                    POINT br{ clientRc.right, clientRc.bottom };
                    if (ClientToScreen(target, &tl) && ClientToScreen(target, &br) &&
                        br.x - tl.x >= 64 && br.y - tl.y >= 64) {
                        // 1px inset prevents cursor edge-bleed onto adjacent monitor seam
                        screenRc.left   = tl.x + 1;
                        screenRc.top    = tl.y + 1;
                        screenRc.right  = br.x - 1;
                        screenRc.bottom = br.y - 1;
                        rectOk = (screenRc.right > screenRc.left && screenRc.bottom > screenRc.top);
                    }
                }
            }
        }

        const bool wantClip = EvaluateShouldClip(en, okG, isFg, isMin, rectOk);
        if (wantClip) {
            if (ClipCursor(&screenRc) != FALSE) {
                currentlyClipped_.store(true, std::memory_order_release);
                return true;
            }
            return currentlyClipped_.load(std::memory_order_acquire);
        }
        ReleaseUnconditionally(false);
        return false;
    }

    void ReleaseUnconditionally(bool forceCall = false) noexcept {
        const bool was = currentlyClipped_.exchange(false, std::memory_order_acq_rel);
        if (was || forceCall) {
            ClipCursor(nullptr);
        }
    }

private:
    std::atomic<bool> enabled_{false};
    std::atomic<bool> allowGameplay_{false};
    std::atomic<bool> currentlyClipped_{false};
    std::atomic<HWND> gameHwnd_{nullptr};
};

// =============================================================================
// Crash Detective (Zero-Heap-Alloc Exception + MiniDump + Lua Mod Blame)
// =============================================================================
class CrashDetective {
public:
    static const char* ExceptionCodeName(DWORD code) noexcept {
        switch (code) {
            case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION (内存非法访问/空指针)";
            case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED (数组越界)";
            case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT (数据对齐异常)";
            case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO (浮点除零)";
            case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION (非法指令)";
            case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR (页面读取失败/磁盘异常)";
            case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO (整数除零)";
            case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION (特权指令)";
            case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW (栈溢出/无限递归)";
            case 0xE06D7363u:                        return "MSVC_CPP_EXCEPTION (C++ 未捕获异常)";
            case 0xC0000374u:                        return "STATUS_HEAP_CORRUPTION (堆内存损坏)";
            case 0xC0000409u:                        return "STATUS_STACK_BUFFER_OVERRUN (栈缓冲区溢出/快速失败)";
            default:                                 return "UNKNOWN_EXCEPTION (未知系统异常)";
        }
    }

    static const char* ClassifyBlameModule(const char* modBaseLower) noexcept {
        if (!modBaseLower || !modBaseLower[0]) {
            return "Unknown module (check recent Lua/Mod errors below) / 未知模块(请优先核查下方近期 Lua 模组报错)";
        }
        auto has = [modBaseLower](const char* needle) -> bool {
            return std::strstr(modBaseLower, needle) != nullptr;
        };
        if (has("nvoglv") || has("nvd3dum") || has("nvwgf2um") ||
            has("atio6axx") || has("aticfx") || has("amdxc") ||
            has("ig9icd") || has("igxelpicd") || has("igd10") || has("igdgmm")) {
            return "GPU Graphics Driver / 显卡驱动层 (建议切换 Native OpenGL / ANGLE 后端或更新显卡驱动)";
        }
        if (has("dinput8.dll")) {
            return "HyperSmooth Native Plugin (dinput8.dll) / 顺滑插件原生层 (请将本报告反馈排查)";
        }
        if (has("libglesv2") || has("libegl") || has("d3d9") || has("opengl32")) {
            return "Graphics API / ANGLE Layer / 图形接口层 (可尝试在配置中切换渲染后端)";
        }
        if (has("fmod")) {
            return "FMOD Audio Engine / 音频引擎层";
        }
        if (has("steam_api") || has("gameoverlayrenderer")) {
            return "Steam Overlay / SteamAPI (建议尝试关闭 Steam 游戏内覆盖)";
        }
        if (has("dontstarve_steam")) {
            return "DST Core Engine (often triggered by a Lua Mod C-API call) / 饥荒主程序(常见于模组传入非法参数触发)";
        }
        return "External DLL / OS Component / 外部组件或系统库";
    }

    static void ResolveModuleAndRva(const void* addr,
                                    char* outMod, size_t outModCap,
                                    uintptr_t& outRva) noexcept {
        outRva = 0;
        if (!outMod || outModCap == 0) return;
        outMod[0] = '\0';
        if (!addr) {
            std::snprintf(outMod, outModCap, "<null>");
            return;
        }
        HMODULE hMod = nullptr;
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(addr), &hMod) && hMod) {
            char full[MAX_PATH]{};
            const DWORD len = GetModuleFileNameA(hMod, full, MAX_PATH);
            if (len > 0 && len < MAX_PATH) {
                const char* base = full;
                for (const char* p = full; *p; ++p) {
                    if (*p == '\\' || *p == '/') base = p + 1;
                }
                std::snprintf(outMod, outModCap, "%s", base);
                outRva = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(hMod);
                return;
            }
        }
        std::snprintf(outMod, outModCap, "<unknown>");
    }

    void Init(const wchar_t* binDir, CursorClipGuard* clipGuard, const char* versionStr) noexcept {
        clipGuard_ = clipGuard;
        if (versionStr && versionStr[0]) {
            std::snprintf(telemetry_.version, sizeof(telemetry_.version), "%s", versionStr);
        }
        if (binDir && binDir[0]) {
            _snwprintf_s(reportPathBin_, _TRUNCATE, L"%s%s", binDir, L"hypersmooth_crash_report.txt");
            WideCharToMultiByte(CP_UTF8, 0, reportPathBin_, -1,
                                reportPathUtf8_, static_cast<int>(sizeof(reportPathUtf8_)), nullptr, nullptr);
        }
        // Best-effort secondary copy into %USERPROFILE%\Documents\Klei\DoNotStarveTogether\ if it exists
        wchar_t userProf[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", userProf, MAX_PATH) > 0) {
            wchar_t kleiDir[MAX_PATH]{};
            _snwprintf_s(kleiDir, _TRUNCATE, L"%s\\Documents\\Klei\\DoNotStarveTogether", userProf);
            const DWORD attr = GetFileAttributesW(kleiDir);
            if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                _snwprintf_s(reportPathDocs_, _TRUNCATE, L"%s\\hypersmooth_crash_report.txt", kleiDir);
            }
        }
    }

    void InstallUnhandledFilter() noexcept {
        if (filterInstalled_.exchange(true, std::memory_order_acq_rel)) return;
        s_activeInstance_ = this;
        prevFilter_ = SetUnhandledExceptionFilter(&StaticUnhandledExceptionFilter);
    }

    const char* ReportPathUtf8() const noexcept {
        return reportPathUtf8_;
    }

    void UpdateTelemetry(const char* backend, bool inGameplay,
                         float avgFps, float low1Fps, float busyMs, float swapMs) noexcept {
        AcquireSRWLockExclusive(&lock_);
        if (backend && backend[0]) {
            std::snprintf(telemetry_.backend, sizeof(telemetry_.backend), "%s", backend);
        }
        telemetry_.inGameplay = inGameplay;
        telemetry_.avgFps     = avgFps;
        telemetry_.low1Fps    = low1Fps;
        telemetry_.busyMs     = busyMs;
        telemetry_.swapMs     = swapMs;
        ReleaseSRWLockExclusive(&lock_);
    }

    void ReportLuaError(uint64_t epochMs, const char* modId,
                        const char* modName, const char* summary) noexcept {
        AcquireSRWLockExclusive(&lock_);
        const size_t idx = luaErrHead_ % kLuaErrRingCap;
        LuaErrorEntry& e = luaErrRing_[idx];
        e.epochMs = epochMs;
        CopySanitized(e.modId,   sizeof(e.modId),   modId ? modId : "");
        CopySanitized(e.modName, sizeof(e.modName), modName ? modName : "");
        CopySanitized(e.summary, sizeof(e.summary), summary ? summary : "");
        ++luaErrHead_;
        if (luaErrCount_ < kLuaErrRingCap) ++luaErrCount_;
        ReleaseSRWLockExclusive(&lock_);
    }

    uint32_t LuaErrorCount() const noexcept {
        return static_cast<uint32_t>(luaErrCount_);
    }

    // Pure zero-heap formatter (also directly unit-testable!)
    size_t FormatReport(char* out, size_t cap,
                        const char* triggerSource,
                        DWORD excCode,
                        const void* excAddr,
                        void* const* frames,
                        uint32_t frameCount,
                        uint64_t nowEpochMs) const noexcept {
        if (!out || cap == 0) return 0;
        size_t pos = 0;
        auto append = [&](const char* fmt, auto... args) {
            if (pos + 1 >= cap) return;
            const int n = std::snprintf(out + pos, cap - pos, fmt, args...);
            if (n > 0) {
                const size_t add = static_cast<size_t>(n);
                pos = (pos + add < cap) ? (pos + add) : (cap - 1);
            }
        };
        auto appendLit = [&](const char* lit) {
            if (!lit || pos + 1 >= cap) return;
            const int n = std::snprintf(out + pos, cap - pos, "%s", lit);
            if (n > 0) {
                const size_t add = static_cast<size_t>(n);
                pos = (pos + add < cap) ? (pos + add) : (cap - 1);
            }
        };

        SYSTEMTIME st{};
        GetLocalTime(&st);

        char faultMod[96]{};
        uintptr_t faultRva = 0;
        ResolveModuleAndRva(excAddr, faultMod, sizeof(faultMod), faultRva);

        char faultModLower[96]{};
        for (size_t i = 0; i + 1 < sizeof(faultModLower) && faultMod[i]; ++i) {
            char c = faultMod[i];
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
            faultModLower[i] = c;
        }

        appendLit("=============================================================================\r\n");
        appendLit("  DST HyperSmooth Crash Detective Report / 饥荒联机版崩溃侦探诊断报告\r\n");
        appendLit("=============================================================================\r\n");
        append("Time / 时间      : %04u-%02u-%02u %02u:%02u:%02u.%03u\r\n",
               st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
        append("Trigger / 触发源 : %s\r\n", triggerSource ? triggerSource : "UnhandledException");
        append("Plugin / 插件版本: DST-HyperSmooth/%s | Backend=%s | InGameplay=%d\r\n",
               telemetry_.version, telemetry_.backend, telemetry_.inGameplay ? 1 : 0);
        append("Perf / 崩溃前性能: Avg=%.1f FPS | 1%%Low=%.1f FPS | Busy=%.2f ms | Swap=%.2f ms\r\n",
               static_cast<double>(telemetry_.avgFps), static_cast<double>(telemetry_.low1Fps),
               static_cast<double>(telemetry_.busyMs), static_cast<double>(telemetry_.swapMs));
        appendLit("-----------------------------------------------------------------------------\r\n");
        append("Exception / 异常 : 0x%08lX - %s\r\n",
               static_cast<unsigned long>(excCode), ExceptionCodeName(excCode));
        append("Faulting / 位置  : %s+0x%llX (0x%p)\r\n",
               faultMod, static_cast<unsigned long long>(faultRva), excAddr);
        append("Blame / 初步归因 : %s\r\n", ClassifyBlameModule(faultModLower));

        // Check recent Lua errors / suspected Mod attribution
        if (luaErrCount_ > 0) {
            appendLit("-----------------------------------------------------------------------------\r\n");
            append("Recent Lua / Mod Errors (%zu captured) / 近期 Lua 模组报错记录:\r\n", luaErrCount_);
            const size_t start = (luaErrHead_ >= luaErrCount_) ? (luaErrHead_ - luaErrCount_) : 0;
            bool flaggedSuspect = false;
            for (size_t i = 0; i < luaErrCount_; ++i) {
                const size_t idx = (start + luaErrCount_ - 1 - i) % kLuaErrRingCap;
                const LuaErrorEntry& e = luaErrRing_[idx];
                const bool recent = (nowEpochMs >= e.epochMs) && ((nowEpochMs - e.epochMs) <= 15000ULL);
                if (recent && !flaggedSuspect && e.modId[0]) {
                    append("  >>> [SUSPECTED MOD / 高度疑似罪魁模组] %s (%s) <<<\r\n",
                           e.modName[0] ? e.modName : e.modId, e.modId);
                    flaggedSuspect = true;
                }
                append("  [%zu] Mod=%s (%s) | %s\r\n",
                       i + 1,
                       e.modName[0] ? e.modName : "<core/unknown>",
                       e.modId[0] ? e.modId : "-",
                       e.summary[0] ? e.summary : "(no detail)");
            }
        } else {
            appendLit("-----------------------------------------------------------------------------\r\n");
            appendLit("Recent Lua / Mod Errors: None captured before native exception.\r\n");
        }

        appendLit("-----------------------------------------------------------------------------\r\n");
        append("Native Call Stack (%u frames) / 原生调用栈:\r\n", frameCount);
        const uint32_t lim = (frameCount <= kMaxCrashFrames) ? frameCount : static_cast<uint32_t>(kMaxCrashFrames);
        for (uint32_t i = 0; i < lim; ++i) {
            char fm[96]{};
            uintptr_t rva = 0;
            ResolveModuleAndRva(frames[i], fm, sizeof(fm), rva);
            append("  #%02u  %s+0x%llX (0x%p)\r\n",
                   i, fm, static_cast<unsigned long long>(rva), frames[i]);
        }
        appendLit("=============================================================================\r\n");
        return pos;
    }

    void OnCrashTriggered(EXCEPTION_POINTERS* ep, const char* triggerSource) noexcept {
        // 1. ALWAYS release ClipCursor immediately so user's mouse is NEVER trapped on crash!
        if (clipGuard_) {
            clipGuard_->ReleaseUnconditionally(true);
        } else {
            ClipCursor(nullptr);
        }

        // 2. Write crash report once per crash (prevent recursive double-dump)
        if (reportWritten_.exchange(true, std::memory_order_acq_rel)) {
            return;
        }

        DWORD excCode = 0;
        const void* excAddr = nullptr;
        if (ep && ep->ExceptionRecord) {
            excCode = ep->ExceptionRecord->ExceptionCode;
            excAddr = ep->ExceptionRecord->ExceptionAddress;
        }

        void* frames[kMaxCrashFrames]{};
        const uint32_t frameCount = static_cast<uint32_t>(
            RtlCaptureStackBackTrace(0, static_cast<ULONG>(kMaxCrashFrames), frames, nullptr));

        FILETIME ft{};
        GetSystemTimeAsFileTime(&ft);
        const uint64_t t = (static_cast<uint64_t>(ft.dwHighDateTime) << 32) |
                           static_cast<uint64_t>(ft.dwLowDateTime);
        constexpr uint64_t kEpochDiff = 116444736000000000ULL;
        const uint64_t nowMs = (t > kEpochDiff) ? ((t - kEpochDiff) / 10000ULL) : 0ULL;

        const size_t n = FormatReport(crashBuf_, sizeof(crashBuf_), triggerSource,
                                      excCode, excAddr, frames, frameCount, nowMs);
        if (n > 0) {
            WriteRawFile(reportPathBin_, crashBuf_, n);
            if (reportPathDocs_[0]) {
                WriteRawFile(reportPathDocs_, crashBuf_, n);
            }
        }
    }

private:
    static void CopySanitized(char* dst, size_t cap, const char* src) noexcept {
        if (!dst || cap == 0) return;
        size_t o = 0;
        for (size_t i = 0; src && src[i] && o + 1 < cap; ++i) {
            const unsigned char c = static_cast<unsigned char>(src[i]);
            if (c == '\r' || c == '\n' || c == '\t') {
                dst[o++] = ' ';
            } else if (c >= 0x20u) {
                dst[o++] = static_cast<char>(c);
            }
        }
        dst[o] = '\0';
    }

    static void WriteRawFile(const wchar_t* path, const char* data, size_t len) noexcept {
        if (!path || !path[0] || !data || len == 0) return;
        HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr);
        CloseHandle(h);
    }

    static LONG WINAPI StaticUnhandledExceptionFilter(EXCEPTION_POINTERS* ep) noexcept {
        CrashDetective* self = s_activeInstance_;
        LPTOP_LEVEL_EXCEPTION_FILTER prev = nullptr;
        if (self) {
            prev = self->prevFilter_;
            self->OnCrashTriggered(ep, "UnhandledExceptionFilter");
        } else {
            ClipCursor(nullptr);
        }
        return prev ? prev(ep) : EXCEPTION_CONTINUE_SEARCH;
    }

    SRWLOCK                      lock_ = SRWLOCK_INIT;
    CursorClipGuard*             clipGuard_ = nullptr;
    LPTOP_LEVEL_EXCEPTION_FILTER prevFilter_ = nullptr;
    std::atomic<bool>            filterInstalled_{false};
    std::atomic<bool>            reportWritten_{false};
    RuntimeTelemetry             telemetry_{};
    LuaErrorEntry                luaErrRing_[kLuaErrRingCap]{};
    size_t                       luaErrHead_  = 0;
    size_t                       luaErrCount_ = 0;
    wchar_t                      reportPathBin_[MAX_PATH]{};
    wchar_t                      reportPathDocs_[MAX_PATH]{};
    char                         reportPathUtf8_[MAX_PATH * 3]{};
    char                         crashBuf_[kCrashBufBytes]{};

    static inline CrashDetective* s_activeInstance_ = nullptr;
};

} // namespace hs_crash_util
