#pragma once
// =============================================================================
// Async Atomic Save-Commit Queue (Win32 Native, Strict Causal FIFO & Barriers)
// =============================================================================
// Offloads synchronous World/Session/Persistent disk writes onto a dedicated
// low-contention background commit thread while preserving:
//   1. Strict causal FIFO order across all persistent writes (snap -> meta -> index)
//   2. Atomic commit via <target>.hc_tmp -> WriteFile -> FlushFileBuffers ->
//      MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
//   3. Zero-overhead fast-path read/stat/rename/delete/unlink/find barriers
//      (`pendingCount_ == 0` -> zero lock acquisition!)
//   4. Bounded memory cap (default 64 MB) with ordered backpressure (drains
//      queue synchronously before falling back so newer writes NEVER overtake)
//   5. Clean idempotent ShutdownAndFlush() for CRT exit / Ctrl+C handlers
// =============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <deque>
#include <new>
#include <string>
#include <vector>

namespace hc_async_save {

class AsyncSaveQueue {
public:
    static constexpr size_t kDefaultMaxQueueBytes = 64u * 1024u * 1024u;

    explicit AsyncSaveQueue(size_t maxQueueBytes = kDefaultMaxQueueBytes)
        : maxQueueBytes_(maxQueueBytes > 0 ? maxQueueBytes : kDefaultMaxQueueBytes) {
        InitializeCriticalSection(&cs_);
        InitializeConditionVariable(&cvWork_);
        InitializeConditionVariable(&cvDone_);
        InitializeConditionVariable(&cvSpace_);
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        qpcToUs_ = (f.QuadPart > 0) ? (1000000.0 / static_cast<double>(f.QuadPart)) : 0.1;
    }

    ~AsyncSaveQueue() {
        ShutdownAndFlush();
        DeleteCriticalSection(&cs_);
    }

    AsyncSaveQueue(const AsyncSaveQueue&) = delete;
    AsyncSaveQueue& operator=(const AsyncSaveQueue&) = delete;

    // Lock-free fast-path gate: 0 atomic overhead when idle
    bool HasPending() const noexcept {
        return pendingCount_.load(std::memory_order_acquire) != 0;
    }

    static std::wstring CanonicalizePath(const wchar_t* raw) {
        if (!raw || raw[0] == L'\0') return {};

        DWORD req = GetFullPathNameW(raw, 0, nullptr, nullptr);
        std::wstring out;
        if (req > 0 && req < 32768) {
            out.resize(req, L'\0');
            DWORD written = GetFullPathNameW(raw, req, out.data(), nullptr);
            if (written > 0 && written < req) {
                out.resize(written);
            } else {
                out = raw;
            }
        } else {
            out = raw;
        }

        if (out.size() > 6 &&
            out[0] == L'\\' && out[1] == L'\\' && out[2] == L'?' && out[3] == L'\\' &&
            ((out[4] >= L'a' && out[4] <= L'z') || (out[4] >= L'A' && out[4] <= L'Z')) &&
            out[5] == L':') {
            out.erase(0, 4);
        }

        for (wchar_t& ch : out) {
            if (ch == L'/') {
                ch = L'\\';
            } else if (ch >= L'A' && ch <= L'Z') {
                ch = static_cast<wchar_t>(ch + (L'a' - L'A'));
            }
        }

        while (out.size() > 3 && out.back() == L'\\') {
            out.pop_back();
        }
        return out;
    }

    static std::wstring Utf8OrAcpToWide(const char* s) {
        if (!s || s[0] == '\0') return {};
        auto convert = [&](UINT cp, DWORD flags) -> std::wstring {
            int n = MultiByteToWideChar(cp, flags, s, -1, nullptr, 0);
            if (n <= 1) return {};
            std::wstring w(static_cast<size_t>(n), L'\0');
            int wlen = MultiByteToWideChar(cp, flags, s, -1, w.data(), n);
            if (wlen <= 1) return {};
            w.resize(static_cast<size_t>(wlen - 1));
            return w;
        };
        std::wstring out = convert(CP_UTF8, MB_ERR_INVALID_CHARS);
        if (out.empty()) {
            out = convert(CP_ACP, 0);
        }
        return out;
    }

    bool TryEnqueue(const wchar_t* path, const void* data, size_t size) {
        if (!path || path[0] == L'\0' || !data || size == 0) {
            return false;
        }
        const int64_t t0 = NowQpc();

        // Payload larger than entire bounded buffer: drain earlier writes first so
        // synchronous fallback preserves strict FIFO ordering on disk.
        if (size > maxQueueBytes_) {
            FlushAll();
            return false;
        }

        std::wstring canon = CanonicalizePath(path);
        if (canon.empty()) {
            FlushAll();
            return false;
        }

        std::vector<uint8_t> copyBuf(size);
        std::memcpy(copyBuf.data(), data, size);

        EnterCriticalSection(&cs_);
        while (!shuttingDown_ && !stopRequested_ && (pendingBytes_ + size > maxQueueBytes_)) {
            backpressureWaits_.fetch_add(1, std::memory_order_relaxed);
            SleepConditionVariableCS(&cvSpace_, &cs_, INFINITE);
        }

        if (shuttingDown_ || stopRequested_) {
            LeaveCriticalSection(&cs_);
            return false;
        }

        if (!workerStarted_) {
            workerThread_ = CreateThread(nullptr, 0, &AsyncSaveQueue::WorkerEntry, this, 0, nullptr);
            if (!workerThread_) {
                LeaveCriticalSection(&cs_);
                FlushAll();
                return false;
            }
            workerStarted_ = true;
        }

        const uint64_t seq = ++nextSeq_;
        SaveJob job;
        job.seq = seq;
        job.rawPath = path;
        job.canonPath = std::move(canon);
        job.data = std::move(copyBuf);

        pendingBytes_ += size;
        queue_.push_back(std::move(job));
        pendingCount_.fetch_add(1, std::memory_order_release);
        WakeConditionVariable(&cvWork_);
        LeaveCriticalSection(&cs_);

        enqueuedCount_.fetch_add(1, std::memory_order_relaxed);
        enqueueUsSum_.fetch_add(QpcUs(t0, NowQpc()), std::memory_order_relaxed);
        return true;
    }

    void FlushPendingForPath(const wchar_t* path) {
        if (!HasPending()) return;
        if (!path || path[0] == L'\0') {
            FlushAll();
            return;
        }
        const std::wstring canon = CanonicalizePath(path);
        if (canon.empty()) {
            FlushAll();
            return;
        }

        EnterCriticalSection(&cs_);
        uint64_t targetSeq = 0;
        if (workerBusy_ && activeCanonPath_ == canon && activeSeq_ > targetSeq) {
            targetSeq = activeSeq_;
        }
        for (const auto& j : queue_) {
            if (j.canonPath == canon && j.seq > targetSeq) {
                targetSeq = j.seq;
            }
        }
        if (targetSeq > completedSeq_) {
            flushCount_.fetch_add(1, std::memory_order_relaxed);
            while (targetSeq > completedSeq_ && (workerBusy_ || !queue_.empty())) {
                SleepConditionVariableCS(&cvDone_, &cs_, INFINITE);
            }
        }
        LeaveCriticalSection(&cs_);
    }

    void FlushPendingForAnsiPath(const char* path) {
        if (!HasPending()) return;
        if (!path || path[0] == '\0') {
            FlushAll();
            return;
        }
        std::wstring w = Utf8OrAcpToWide(path);
        if (w.empty()) {
            FlushAll();
            return;
        }
        FlushPendingForPath(w.c_str());
    }

    void FlushPendingForDirPattern(const wchar_t* pattern) {
        if (!HasPending()) return;
        if (!pattern || pattern[0] == L'\0') {
            FlushAll();
            return;
        }

        // Extract directory component from FindFirstFileW wildcard or bare dir path
        std::wstring dir;
        const wchar_t* lastFwd = std::wcsrchr(pattern, L'/');
        const wchar_t* lastBck = std::wcsrchr(pattern, L'\\');
        const wchar_t* lastSep = (lastFwd && (!lastBck || lastFwd > lastBck)) ? lastFwd : lastBck;

        const bool hasWildcard = (std::wcspbrk(pattern, L"*?") != nullptr);
        if (hasWildcard || lastSep != nullptr) {
            if (lastSep) {
                const size_t sepIdx = static_cast<size_t>(lastSep - pattern);
                if (sepIdx == 0) {
                    dir = L"\\";
                } else if (sepIdx == 2 && pattern[1] == L':') {
                    dir.assign(pattern, 3);
                } else {
                    dir.assign(pattern, sepIdx);
                }
            } else {
                const size_t len = std::wcslen(pattern);
                if (len >= 2 && pattern[1] == L':') {
                    dir.assign(pattern, 2);
                    dir.push_back(L'.');
                } else {
                    dir = L".";
                }
            }
        } else {
            dir = pattern;
        }

        const std::wstring canonDir = CanonicalizePath(dir.c_str());
        if (canonDir.empty()) {
            FlushAll();
            return;
        }

        auto underDir = [&](const std::wstring& jobPath) -> bool {
            if (jobPath == canonDir) return true;
            if (canonDir.back() == L'\\') {
                return jobPath.size() >= canonDir.size() &&
                       jobPath.compare(0, canonDir.size(), canonDir) == 0;
            }
            return jobPath.size() > canonDir.size() &&
                   jobPath[canonDir.size()] == L'\\' &&
                   jobPath.compare(0, canonDir.size(), canonDir) == 0;
        };

        EnterCriticalSection(&cs_);
        uint64_t targetSeq = 0;
        if (workerBusy_ && underDir(activeCanonPath_) && activeSeq_ > targetSeq) {
            targetSeq = activeSeq_;
        }
        for (const auto& j : queue_) {
            if (underDir(j.canonPath) && j.seq > targetSeq) {
                targetSeq = j.seq;
            }
        }
        if (targetSeq > completedSeq_) {
            flushCount_.fetch_add(1, std::memory_order_relaxed);
            while (targetSeq > completedSeq_ && (workerBusy_ || !queue_.empty())) {
                SleepConditionVariableCS(&cvDone_, &cs_, INFINITE);
            }
        }
        LeaveCriticalSection(&cs_);
    }

    void FlushAll() {
        EnterCriticalSection(&cs_);
        if (workerBusy_ || !queue_.empty()) {
            flushCount_.fetch_add(1, std::memory_order_relaxed);
            while (workerBusy_ || !queue_.empty()) {
                SleepConditionVariableCS(&cvDone_, &cs_, INFINITE);
            }
        }
        LeaveCriticalSection(&cs_);
    }

    // Idempotent drain & clean join of writer thread (never call inside DllMain detach!).
    void ShutdownAndFlush() {
        EnterCriticalSection(&cs_);
        shuttingDown_ = true;
        stopRequested_ = true;
        WakeAllConditionVariable(&cvWork_);
        WakeAllConditionVariable(&cvSpace_);
        HANDLE th = workerThread_;
        workerThread_ = nullptr;
        LeaveCriticalSection(&cs_);

        if (th) {
            WaitForSingleObject(th, 30000);
            CloseHandle(th);
        }

        EnterCriticalSection(&cs_);
        workerStarted_ = false;
        LeaveCriticalSection(&cs_);
    }

    uint64_t EnqueuedCount() const noexcept { return enqueuedCount_.load(std::memory_order_relaxed); }
    uint64_t CommittedCount() const noexcept { return committedCount_.load(std::memory_order_relaxed); }
    uint64_t CommittedBytes() const noexcept { return committedBytes_.load(std::memory_order_relaxed); }
    uint64_t EnqueueUsSum() const noexcept { return enqueueUsSum_.load(std::memory_order_relaxed); }
    uint64_t IoUsSum() const noexcept { return ioUsSum_.load(std::memory_order_relaxed); }
    uint64_t FlushCount() const noexcept { return flushCount_.load(std::memory_order_relaxed); }
    uint64_t ErrorCount() const noexcept { return errorCount_.load(std::memory_order_relaxed); }
    uint64_t BackpressureWaits() const noexcept { return backpressureWaits_.load(std::memory_order_relaxed); }

    void ResetTelemetry() noexcept {
        enqueuedCount_.store(0, std::memory_order_relaxed);
        committedCount_.store(0, std::memory_order_relaxed);
        committedBytes_.store(0, std::memory_order_relaxed);
        enqueueUsSum_.store(0, std::memory_order_relaxed);
        ioUsSum_.store(0, std::memory_order_relaxed);
        flushCount_.store(0, std::memory_order_relaxed);
        errorCount_.store(0, std::memory_order_relaxed);
        backpressureWaits_.store(0, std::memory_order_relaxed);
    }

private:
    struct SaveJob {
        uint64_t             seq = 0;
        std::wstring         rawPath;
        std::wstring         canonPath;
        std::vector<uint8_t> data;
    };

    static int64_t NowQpc() noexcept {
        LARGE_INTEGER li{};
        QueryPerformanceCounter(&li);
        return li.QuadPart;
    }

    uint64_t QpcUs(int64_t t0, int64_t t1) const noexcept {
        if (t1 <= t0) return 0;
        return static_cast<uint64_t>(static_cast<double>(t1 - t0) * qpcToUs_);
    }

    bool CommitJobAtomic(const SaveJob& job) noexcept {
        const std::wstring tmpPath = job.rawPath + L".hc_tmp";
        HANDLE hFile = CreateFileW(
            tmpPath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            return false;
        }

        const uint8_t* ptr = job.data.data();
        size_t rem = job.data.size();
        bool writeOk = true;
        while (rem > 0) {
            const DWORD chunk = static_cast<DWORD>(rem > (32u * 1024u * 1024u) ? (32u * 1024u * 1024u) : rem);
            DWORD written = 0;
            if (!WriteFile(hFile, ptr, chunk, &written, nullptr) || written == 0) {
                writeOk = false;
                break;
            }
            ptr += written;
            rem -= written;
        }

        if (writeOk) {
            FlushFileBuffers(hFile);
        }
        CloseHandle(hFile);

        if (!writeOk) {
            DeleteFileW(tmpPath.c_str());
            return false;
        }

        bool renamed = false;
        for (int attempt = 0; attempt < 12; ++attempt) {
            if (MoveFileExW(tmpPath.c_str(), job.rawPath.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                renamed = true;
                break;
            }
            const DWORD err = GetLastError();
            if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED && err != ERROR_LOCK_VIOLATION) {
                break;
            }
            Sleep(5);
        }

        if (!renamed) {
            DeleteFileW(tmpPath.c_str());
            return false;
        }
        return true;
    }

    static DWORD WINAPI WorkerEntry(LPVOID param) {
        static_cast<AsyncSaveQueue*>(param)->WorkerLoop();
        return 0;
    }

    void WorkerLoop() {
        while (true) {
            SaveJob job;
            EnterCriticalSection(&cs_);
            while (queue_.empty() && !stopRequested_) {
                SleepConditionVariableCS(&cvWork_, &cs_, INFINITE);
            }
            if (queue_.empty() && stopRequested_) {
                LeaveCriticalSection(&cs_);
                break;
            }
            job = std::move(queue_.front());
            queue_.pop_front();
            workerBusy_ = true;
            activeSeq_ = job.seq;
            activeCanonPath_ = job.canonPath;
            LeaveCriticalSection(&cs_);

            const int64_t t0 = NowQpc();
            const bool ok = CommitJobAtomic(job);
            const uint64_t ioUs = QpcUs(t0, NowQpc());

            if (ok) {
                committedCount_.fetch_add(1, std::memory_order_relaxed);
                committedBytes_.fetch_add(job.data.size(), std::memory_order_relaxed);
                ioUsSum_.fetch_add(ioUs, std::memory_order_relaxed);
            } else {
                errorCount_.fetch_add(1, std::memory_order_relaxed);
            }

            EnterCriticalSection(&cs_);
            completedSeq_ = job.seq;
            if (pendingBytes_ >= job.data.size()) {
                pendingBytes_ -= job.data.size();
            } else {
                pendingBytes_ = 0;
            }
            workerBusy_ = false;
            activeSeq_ = 0;
            activeCanonPath_.clear();
            pendingCount_.fetch_sub(1, std::memory_order_acq_rel);
            WakeAllConditionVariable(&cvDone_);
            WakeAllConditionVariable(&cvSpace_);
            LeaveCriticalSection(&cs_);
        }
    }

    size_t                maxQueueBytes_;
    double                qpcToUs_ = 0.1;
    CRITICAL_SECTION      cs_{};
    CONDITION_VARIABLE    cvWork_{};
    CONDITION_VARIABLE    cvDone_{};
    CONDITION_VARIABLE    cvSpace_{};
    std::deque<SaveJob>   queue_;
    bool                  workerStarted_ = false;
    bool                  stopRequested_ = false;
    bool                  shuttingDown_  = false;
    bool                  workerBusy_    = false;
    std::wstring          activeCanonPath_;
    uint64_t              activeSeq_     = 0;
    uint64_t              nextSeq_       = 0;
    uint64_t              completedSeq_  = 0;
    size_t                pendingBytes_  = 0;
    HANDLE                workerThread_  = nullptr;

    std::atomic<uint32_t> pendingCount_{0};
    std::atomic<uint64_t> enqueuedCount_{0};
    std::atomic<uint64_t> committedCount_{0};
    std::atomic<uint64_t> committedBytes_{0};
    std::atomic<uint64_t> enqueueUsSum_{0};
    std::atomic<uint64_t> ioUsSum_{0};
    std::atomic<uint64_t> flushCount_{0};
    std::atomic<uint64_t> errorCount_{0};
    std::atomic<uint64_t> backpressureWaits_{0};
};

} // namespace hc_async_save
