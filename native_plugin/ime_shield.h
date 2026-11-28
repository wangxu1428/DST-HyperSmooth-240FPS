#pragma once
// =============================================================================
// HyperSmooth Smart IME Shield + Focus Modifier Self-Heal (ime_shield.h)
// =============================================================================
// Prevents CJK IME composition hijacking single-key WASD/action bindings during
// gameplay (especially in Exclusive Fullscreen / Borderless), while immediately
// restoring full native IME input whenever any TextEdit widget (chat, console,
// signs, search boxes) or menu screen becomes active.
// Also tracks Win32 focus loss/regain transitions so stuck Alt/Ctrl/Shift
// modifier states are flushed cleanly via TheInputProxy:FlushInput() once on
// focus regain without interfering with normal key holds.
//
// Thread-safety invariant:
//   imm32.dll calls (ImmAssociateContext / ImmAssociateContextEx) MUST run only
//   on the thread owning the target HWND (`GetWindowThreadProcessId == curTid`).
//   Off-thread setters (INI reload, etc.) only flip lock-free atomics and mark
//   dirty; HookPeekMessageW applies the actual ImmAssociateContext transition on
//   the window message-loop thread with near-zero overhead.
// =============================================================================

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace hs_ime {

class SmartImeShield {
public:
    using HimcHandle = HANDLE;
    using ImmAssociateContext_t   = HimcHandle(WINAPI*)(HWND hWnd, HimcHandle hIMC);
    using ImmAssociateContextEx_t = BOOL(WINAPI*)(HWND hWnd, HimcHandle hIMC, DWORD dwFlags);

    static constexpr DWORD kIaceDefault = 0x0010u; // IACE_DEFAULT

    static constexpr bool ComputeWantSuppress(bool enabled, bool textInputActive, bool inGameplay) noexcept {
        return enabled && inGameplay && !textInputActive;
    }

    void SetEnabled(bool enabled) noexcept {
        const bool prev = enabled_.exchange(enabled, std::memory_order_acq_rel);
        if (prev != enabled) {
            dirty_.store(true, std::memory_order_release);
        }
    }

    bool IsEnabled() const noexcept {
        return enabled_.load(std::memory_order_acquire);
    }

    bool IsTextInputActive() const noexcept {
        return textInputActive_.load(std::memory_order_acquire);
    }

    bool IsInGameplay() const noexcept {
        return inGameplay_.load(std::memory_order_acquire);
    }

    bool IsSuppressedNow() const noexcept {
        return suppressedNow_.load(std::memory_order_acquire);
    }

    uint32_t SuppressCount() const noexcept {
        return suppressCount_.load(std::memory_order_relaxed);
    }

    uint32_t RestoreCount() const noexcept {
        return restoreCount_.load(std::memory_order_relaxed);
    }

    uint32_t FocusRegains() const noexcept {
        return focusRegains_.load(std::memory_order_relaxed);
    }

    bool HasPendingFlushInput() const noexcept {
        return pendingFlushInput_.load(std::memory_order_acquire);
    }

    void ResetForNewVm() noexcept {
        textInputActive_.store(false, std::memory_order_release);
        inGameplay_.store(false, std::memory_order_release);
        dirty_.store(true, std::memory_order_release);
    }

    // Called from Lua or state transition hooks. Returns true once if a Win32
    // focus regain occurred so Lua can call TheInputProxy:FlushInput() once.
    bool OnTextInputStateChanged(bool textInputActive, bool inGameplay, HWND optHwnd = nullptr) noexcept {
        const bool prevText = textInputActive_.exchange(textInputActive, std::memory_order_acq_rel);
        const bool prevGame = inGameplay_.exchange(inGameplay, std::memory_order_acq_rel);
        if (prevText != textInputActive || prevGame != inGameplay) {
            dirty_.store(true, std::memory_order_release);
        }
        if (optHwnd != nullptr && IsWindowOwnedByCurrentThread(optHwnd)) {
            SyncOnOwnerThread(optHwnd);
        }
        return ConsumeFocusRegainFlush();
    }

    bool ConsumeFocusRegainFlush() noexcept {
        return pendingFlushInput_.exchange(false, std::memory_order_acq_rel);
    }

    // Called from HookPeekMessageW on every removed message (wRemoveMsg & PM_REMOVE).
    // Returns true if the message indicates focus was just lost (so CursorClipGuard
    // can release clip immediately without polling delay).
    bool OnPeekMessage(const MSG* msg, HWND fallbackHwnd) noexcept {
        if (!msg) return false;
        HWND hwnd = (msg->hwnd && IsWindow(msg->hwnd)) ? msg->hwnd : fallbackHwnd;
        bool lostFocusNow = false;

        switch (msg->message) {
            case WM_KILLFOCUS: {
                focusLost_.store(true, std::memory_order_release);
                lostFocusNow = true;
                break;
            }
            case WM_ACTIVATE: {
                const WORD state = LOWORD(msg->wParam);
                if (state == WA_INACTIVE) {
                    focusLost_.store(true, std::memory_order_release);
                    lostFocusNow = true;
                } else {
                    HandleFocusRegain();
                }
                break;
            }
            case WM_ACTIVATEAPP: {
                if (msg->wParam == FALSE) {
                    focusLost_.store(true, std::memory_order_release);
                    lostFocusNow = true;
                } else {
                    HandleFocusRegain();
                }
                break;
            }
            case WM_SETFOCUS:
            case WM_INPUTLANGCHANGE:
            case 0x0281u: /* WM_IME_SETCONTEXT */ {
                if (msg->message == WM_SETFOCUS) {
                    HandleFocusRegain();
                } else {
                    dirty_.store(true, std::memory_order_release);
                }
                break;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                if (msg->wParam == VK_PROCESSKEY &&
                    ComputeWantSuppress(IsEnabled(), IsTextInputActive(), IsInGameplay())) {
                    dirty_.store(true, std::memory_order_release);
                }
                break;
            }
            default:
                break;
        }

        if (hwnd && IsWindowOwnedByCurrentThread(hwnd)) {
            const uint64_t nowMs = GetTickCount64();
            const uint64_t lastMs = lastSyncTickMs_.load(std::memory_order_relaxed);
            const bool wantSup = ComputeWantSuppress(IsEnabled(), IsTextInputActive(), IsInGameplay());
            const bool curSup  = suppressedNow_.load(std::memory_order_acquire);
            if (dirty_.load(std::memory_order_acquire) ||
                wantSup != curSup ||
                (nowMs - lastMs >= 500ULL && wantSup)) {
                SyncOnOwnerThread(hwnd);
            }
        }
        return lostFocusNow;
    }

    void SyncOnOwnerThread(HWND hwnd) noexcept {
        if (!hwnd || !IsWindow(hwnd) || !IsWindowOwnedByCurrentThread(hwnd)) return;
        EnsureImmResolved();
        lastSyncTickMs_.store(GetTickCount64(), std::memory_order_relaxed);
        const bool forceReapply = dirty_.exchange(false, std::memory_order_acq_rel);

        if (!immAssociateContext_ && !immAssociateContextEx_) return;

        if (activeHwnd_ != hwnd) {
            savedHimc_ = nullptr;
        }

        const bool wantSup = ComputeWantSuppress(IsEnabled(), IsTextInputActive(), IsInGameplay());
        if (wantSup) {
            if (!suppressedNow_.load(std::memory_order_acquire) || activeHwnd_ != hwnd || forceReapply) {
                if (immAssociateContext_) {
                    HimcHandle prev = immAssociateContext_(hwnd, nullptr);
                    if (prev != nullptr) {
                        savedHimc_ = prev;
                    }
                } else if (immAssociateContextEx_) {
                    immAssociateContextEx_(hwnd, nullptr, 0u);
                }
                activeHwnd_ = hwnd;
                suppressedNow_.store(true, std::memory_order_release);
                suppressCount_.fetch_add(1u, std::memory_order_relaxed);
            }
        } else {
            if (suppressedNow_.load(std::memory_order_acquire)) {
                HWND target = IsWindow(hwnd) ? hwnd : activeHwnd_;
                if (target && IsWindow(target)) {
                    if (savedHimc_ != nullptr && immAssociateContext_) {
                        immAssociateContext_(target, savedHimc_);
                    } else if (immAssociateContextEx_) {
                        immAssociateContextEx_(target, nullptr, kIaceDefault);
                    }
                }
                suppressedNow_.store(false, std::memory_order_release);
                restoreCount_.fetch_add(1u, std::memory_order_relaxed);
            }
        }
    }

private:
    static bool IsWindowOwnedByCurrentThread(HWND hwnd) noexcept {
        if (!hwnd || !IsWindow(hwnd)) return false;
        DWORD pid = 0;
        const DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
        return tid != 0 && tid == GetCurrentThreadId();
    }

    void HandleFocusRegain() noexcept {
        const uint64_t nowMs = GetTickCount64();
        const bool wasLost = focusLost_.exchange(false, std::memory_order_acq_rel);
        const uint64_t prevRegain = lastFocusRegainMs_.load(std::memory_order_relaxed);
        if (wasLost || (nowMs - prevRegain >= 250ULL)) {
            lastFocusRegainMs_.store(nowMs, std::memory_order_relaxed);
            if (!pendingFlushInput_.exchange(true, std::memory_order_acq_rel) || wasLost) {
                focusRegains_.fetch_add(1u, std::memory_order_relaxed);
            }
        }
        dirty_.store(true, std::memory_order_release);
    }

    void EnsureImmResolved() noexcept {
        if (immResolved_) return;
        immResolved_ = true;
        HMODULE hImm = GetModuleHandleW(L"imm32.dll");
        if (!hImm) {
            hImm = LoadLibraryW(L"imm32.dll");
        }
        if (hImm) {
            immAssociateContext_   = reinterpret_cast<ImmAssociateContext_t>(GetProcAddress(hImm, "ImmAssociateContext"));
            immAssociateContextEx_ = reinterpret_cast<ImmAssociateContextEx_t>(GetProcAddress(hImm, "ImmAssociateContextEx"));
        }
    }

    std::atomic<bool>     enabled_{true};
    std::atomic<bool>     textInputActive_{false};
    std::atomic<bool>     inGameplay_{false};
    std::atomic<bool>     suppressedNow_{false};
    std::atomic<bool>     dirty_{false};
    std::atomic<bool>     focusLost_{false};
    std::atomic<bool>     pendingFlushInput_{false};
    std::atomic<uint32_t> suppressCount_{0};
    std::atomic<uint32_t> restoreCount_{0};
    std::atomic<uint32_t> focusRegains_{0};
    std::atomic<uint64_t> lastSyncTickMs_{0};
    std::atomic<uint64_t> lastFocusRegainMs_{0};

    HWND                  activeHwnd_{nullptr};
    HimcHandle            savedHimc_{nullptr};
    bool                  immResolved_{false};
    ImmAssociateContext_t   immAssociateContext_{nullptr};
    ImmAssociateContextEx_t immAssociateContextEx_{nullptr};
};

} // namespace hs_ime
