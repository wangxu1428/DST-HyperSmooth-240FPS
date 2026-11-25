#pragma once
// =============================================================================
// Async Atomic Save-Commit Queue (Win32 Native, Strict Causal FIFO & Barriers)
// =============================================================================
// Offloads synchronous World/Session/Persistent compression (libdeflate +
// Klei Base64) AND disk writes onto a dedicated low-contention background
// commit thread while preserving:
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
#include <unordered_set>
#include <utility>
#include <vector>

#include "klei_save_codec.h"

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

    // Lock-free fast-path gate: 0 lock overhead when idle
    bool HasPending() const noexcept {
        return (pendingCount_.load(std::memory_order_acquire) != 0) ||
               (activeSyncCount_.load(std::memory_order_acquire) != 0);
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

    using FlushFileBuffersFn_t = BOOL (WINAPI*)(HANDLE);

    bool TryEnqueue(const wchar_t* path, const void* data, size_t size) {
        return TryEnqueueEx(path, data, size, nullptr);
    }

    bool TryEnqueueEx(const wchar_t* path, const void* data, size_t size, uint64_t* outSeq) {
        if (outSeq) *outSeq = 0;
        if (!path || path[0] == L'\0' || !data || size == 0) {
            return false;
        }
        if (size > maxQueueBytes_) {
            FlushAll();
            return false;
        }
        std::vector<uint8_t> copyBuf(size);
        std::memcpy(copyBuf.data(), data, size);
        return EnqueueInternal(path, std::move(copyBuf), false, 6, outSeq);
    }

    bool TryEnqueueCompress(const wchar_t* path, std::vector<uint8_t>&& rawData, int deflateLevel = 6) {
        return TryEnqueueCompressEx(path, std::move(rawData), deflateLevel, nullptr);
    }

    bool TryEnqueueCompressEx(const wchar_t* path, std::vector<uint8_t>&& rawData, int deflateLevel, uint64_t* outSeq) {
        if (outSeq) *outSeq = 0;
        if (!path || path[0] == L'\0' || rawData.empty()) {
            return false;
        }
        if (rawData.size() > maxQueueBytes_) {
            FlushAll();
            return false;
        }
        return EnqueueInternal(path, std::move(rawData), true, deflateLevel, outSeq);
    }

    // Waits until job `seq` finishes on the worker thread and returns its EXACT commit outcome (true/false).
    // Fail-closed: if `seq` is 0 or no longer in the completion ring, returns false.
    bool WaitForSeq(uint64_t seq) {
        if (seq == 0) return false;
        EnterCriticalSection(&cs_);
        if (seq > completedSeq_) {
            flushCount_.fetch_add(1, std::memory_order_relaxed);
            while (seq > completedSeq_ && (workerBusy_ || !queue_.empty())) {
                SleepConditionVariableCS(&cvDone_, &cs_, INFINITE);
            }
        }
        bool ok = false;
        const size_t slot = static_cast<size_t>(seq & (kResultRingSize - 1u));
        if (resultRing_[slot].seq == seq) {
            ok = resultRing_[slot].ok;
        }
        LeaveCriticalSection(&cs_);
        return ok;
    }

    bool CommitSyncAtomic(const wchar_t* path, const void* data, size_t size, DWORD* outWinErr = nullptr) {
        if (!path || path[0] == L'\0' || !data || size == 0) {
            if (outWinErr) *outWinErr = ERROR_INVALID_PARAMETER;
            return false;
        }
        const std::wstring canon = CanonicalizePath(path);
        if (canon.empty()) {
            if (outWinErr) *outWinErr = ERROR_INVALID_NAME;
            return false;
        }
        ScopedSyncPathLock pathLock(*this, canon);
        const int64_t t0 = NowQpc();
        const bool ok = WriteAtomicFile(std::wstring(path), data, size, outWinErr, flushFn_);
        const uint64_t ioUs = QpcUs(t0, NowQpc());
        if (ok) {
            committedCount_.fetch_add(1, std::memory_order_relaxed);
            committedBytes_.fetch_add(size, std::memory_order_relaxed);
            ioUsSum_.fetch_add(ioUs, std::memory_order_relaxed);
        } else {
            errorCount_.fetch_add(1, std::memory_order_relaxed);
        }
        return ok;
    }

    bool CommitSyncCompressAtomic(const wchar_t* path, const void* rawData, size_t rawSize,
                                  int deflateLevel = 6, DWORD* outWinErr = nullptr) {
        if (!path || path[0] == L'\0' || !rawData || rawSize == 0) {
            if (outWinErr) *outWinErr = ERROR_INVALID_PARAMETER;
            return false;
        }
        const std::wstring canon = CanonicalizePath(path);
        if (canon.empty()) {
            if (outWinErr) *outWinErr = ERROR_INVALID_NAME;
            return false;
        }
        ScopedSyncPathLock pathLock(*this, canon);
        const int lvl = (deflateLevel >= 1 && deflateLevel <= 12) ? deflateLevel : 6;
        std::vector<uint8_t> encoded;
        const int64_t tc0 = NowQpc();
        if (!klei_save_codec::EncodeKleiZlibBase64(rawData, rawSize, lvl, encoded)) {
            errorCount_.fetch_add(1, std::memory_order_relaxed);
            if (outWinErr) *outWinErr = ERROR_OUTOFMEMORY;
            return false;
        }
        asyncCompressedCount_.fetch_add(1, std::memory_order_relaxed);
        asyncCompressUsSum_.fetch_add(QpcUs(tc0, NowQpc()), std::memory_order_relaxed);
        const int64_t t0 = NowQpc();
        const bool ok = WriteAtomicFile(std::wstring(path), encoded.data(), encoded.size(), outWinErr, flushFn_);
        const uint64_t ioUs = QpcUs(t0, NowQpc());
        if (ok) {
            committedCount_.fetch_add(1, std::memory_order_relaxed);
            committedBytes_.fetch_add(encoded.size(), std::memory_order_relaxed);
            ioUsSum_.fetch_add(ioUs, std::memory_order_relaxed);
        } else {
            errorCount_.fetch_add(1, std::memory_order_relaxed);
        }
        return ok;
    }

    void SetFlushFileBuffersFnForTest(FlushFileBuffersFn_t fn) noexcept {
        flushFn_ = fn ? fn : &FlushFileBuffers;
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
        bool waited = false;
        while (true) {
            const bool hasSync = (activeSyncPaths_.count(canon) > 0);
            uint64_t targetSeq = 0;
            if (workerBusy_ && activeCanonPath_ == canon && activeSeq_ > targetSeq) {
                targetSeq = activeSeq_;
            }
            for (const auto& j : queue_) {
                if (j.canonPath == canon && j.seq > targetSeq) {
                    targetSeq = j.seq;
                }
            }
            if (!hasSync && targetSeq <= completedSeq_) {
                break;
            }
            if (!waited) {
                flushCount_.fetch_add(1, std::memory_order_relaxed);
                waited = true;
            }
            SleepConditionVariableCS(&cvDone_, &cs_, INFINITE);
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
        bool waited = false;
        while (true) {
            bool hasSyncUnderDir = false;
            for (const auto& sp : activeSyncPaths_) {
                if (underDir(sp)) { hasSyncUnderDir = true; break; }
            }
            uint64_t targetSeq = 0;
            if (workerBusy_ && underDir(activeCanonPath_) && activeSeq_ > targetSeq) {
                targetSeq = activeSeq_;
            }
            for (const auto& j : queue_) {
                if (underDir(j.canonPath) && j.seq > targetSeq) {
                    targetSeq = j.seq;
                }
            }
            if (!hasSyncUnderDir && targetSeq <= completedSeq_) {
                break;
            }
            if (!waited) {
                flushCount_.fetch_add(1, std::memory_order_relaxed);
                waited = true;
            }
            SleepConditionVariableCS(&cvDone_, &cs_, INFINITE);
        }
        LeaveCriticalSection(&cs_);
    }

    void FlushAll() {
        EnterCriticalSection(&cs_);
        if (workerBusy_ || !queue_.empty() || !activeSyncPaths_.empty()) {
            flushCount_.fetch_add(1, std::memory_order_relaxed);
            while (workerBusy_ || !queue_.empty() || !activeSyncPaths_.empty()) {
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
    uint64_t CompressedCount() const noexcept { return asyncCompressedCount_.load(std::memory_order_relaxed); }
    uint64_t CompressUsSum() const noexcept { return asyncCompressUsSum_.load(std::memory_order_relaxed); }

    void ResetTelemetry() noexcept {
        enqueuedCount_.store(0, std::memory_order_relaxed);
        committedCount_.store(0, std::memory_order_relaxed);
        committedBytes_.store(0, std::memory_order_relaxed);
        enqueueUsSum_.store(0, std::memory_order_relaxed);
        ioUsSum_.store(0, std::memory_order_relaxed);
        flushCount_.store(0, std::memory_order_relaxed);
        errorCount_.store(0, std::memory_order_relaxed);
        backpressureWaits_.store(0, std::memory_order_relaxed);
        asyncCompressedCount_.store(0, std::memory_order_relaxed);
        asyncCompressUsSum_.store(0, std::memory_order_relaxed);
    }

private:
    struct SaveJob {
        uint64_t             seq = 0;
        std::wstring         rawPath;
        std::wstring         canonPath;
        std::vector<uint8_t> data;
        bool                 compressKlei = false;
        int                  deflateLevel = 6;
    };

    bool EnqueueInternal(const wchar_t* path, std::vector<uint8_t>&& buf, bool compressKlei,
                         int deflateLevel, uint64_t* outSeq) {
        const int64_t t0 = NowQpc();
        const size_t size = buf.size();
        std::wstring canon = CanonicalizePath(path);
        if (canon.empty()) {
            FlushAll();
            return false;
        }

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
        job.data = std::move(buf);
        job.compressKlei = compressKlei;
        job.deflateLevel = (deflateLevel >= 1 && deflateLevel <= 12) ? deflateLevel : 6;

        pendingBytes_ += size;
        queue_.push_back(std::move(job));
        pendingCount_.fetch_add(1, std::memory_order_release);
        WakeConditionVariable(&cvWork_);
        LeaveCriticalSection(&cs_);

        if (outSeq) *outSeq = seq;
        enqueuedCount_.fetch_add(1, std::memory_order_relaxed);
        enqueueUsSum_.fetch_add(QpcUs(t0, NowQpc()), std::memory_order_relaxed);
        return true;
    }

    static int64_t NowQpc() noexcept {
        LARGE_INTEGER li{};
        QueryPerformanceCounter(&li);
        return li.QuadPart;
    }

    uint64_t QpcUs(int64_t t0, int64_t t1) const noexcept {
        if (t1 <= t0) return 0;
        return static_cast<uint64_t>(static_cast<double>(t1 - t0) * qpcToUs_);
    }

    static bool WriteAtomicFile(const std::wstring& rawPath, const void* data, size_t size,
                                DWORD* outWinErr = nullptr,
                                FlushFileBuffersFn_t flushFn = &FlushFileBuffers) noexcept {
        if (outWinErr) *outWinErr = 0;
        // Globally unique per-commit temp file name prevents concurrent writers or stale leftovers
        // from ever truncating/deleting each other's temporary file!
        static std::atomic<uint64_t> s_tmpNonce{0};
        const uint64_t nonce = s_tmpNonce.fetch_add(1, std::memory_order_relaxed) + 1ULL;
        const std::wstring tmpPath = rawPath + L".hc_tmp." +
            std::to_wstring(GetCurrentProcessId()) + L"." +
            std::to_wstring(GetCurrentThreadId()) + L"." +
            std::to_wstring(nonce);
        HANDLE hFile = CreateFileW(
            tmpPath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (hFile == INVALID_HANDLE_VALUE) {
            if (outWinErr) *outWinErr = GetLastError();
            return false;
        }

        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        size_t rem = size;
        bool writeOk = true;
        DWORD lastErr = 0;
        while (rem > 0) {
            const DWORD chunk = static_cast<DWORD>(rem > (32u * 1024u * 1024u) ? (32u * 1024u * 1024u) : rem);
            DWORD written = 0;
            if (!WriteFile(hFile, ptr, chunk, &written, nullptr) || written == 0) {
                lastErr = GetLastError();
                writeOk = false;
                break;
            }
            ptr += written;
            rem -= written;
        }

        if (writeOk) {
            FlushFileBuffersFn_t fn = flushFn ? flushFn : &FlushFileBuffers;
            if (!fn(hFile)) {
                lastErr = GetLastError();
                if (lastErr == 0) lastErr = ERROR_WRITE_FAULT;
                writeOk = false;
            }
        }
        CloseHandle(hFile);

        if (!writeOk) {
            DeleteFileW(tmpPath.c_str());
            if (outWinErr) *outWinErr = lastErr ? lastErr : ERROR_WRITE_FAULT;
            return false;
        }

        bool renamed = false;
        DWORD renameErr = 0;
        for (int attempt = 0; attempt < 12; ++attempt) {
            if (MoveFileExW(tmpPath.c_str(), rawPath.c_str(),
                            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                renamed = true;
                break;
            }
            renameErr = GetLastError();
            if (renameErr != ERROR_SHARING_VIOLATION &&
                renameErr != ERROR_ACCESS_DENIED &&
                renameErr != ERROR_LOCK_VIOLATION) {
                break;
            }
            Sleep(5);
        }

        if (!renamed) {
            DeleteFileW(tmpPath.c_str());
            if (outWinErr) *outWinErr = renameErr ? renameErr : ERROR_WRITE_FAULT;
            return false;
        }
        return true;
    }

    static DWORD WINAPI WorkerEntry(LPVOID param) {
        static_cast<AsyncSaveQueue*>(param)->WorkerLoop();
        return 0;
    }

    void WorkerLoop() {
        libdeflate_compressor* workerComp = nullptr;
        int workerCompLevel = 0;
        std::vector<uint8_t> encodedBuf;

        while (true) {
            SaveJob job;
            EnterCriticalSection(&cs_);
            while (true) {
                while (queue_.empty() && !stopRequested_) {
                    SleepConditionVariableCS(&cvWork_, &cs_, INFINITE);
                }
                if (queue_.empty() && stopRequested_) {
                    LeaveCriticalSection(&cs_);
                    goto worker_exit;
                }
                if (activeSyncPaths_.count(queue_.front().canonPath) > 0) {
                    SleepConditionVariableCS(&cvDone_, &cs_, INFINITE);
                    continue;
                }
                job = std::move(queue_.front());
                queue_.pop_front();
                workerBusy_ = true;
                activeSeq_ = job.seq;
                activeCanonPath_ = job.canonPath;
                break;
            }
            LeaveCriticalSection(&cs_);

            const size_t queuedSize = job.data.size();
            const void* writePtr = job.data.data();
            size_t writeSize = queuedSize;
            bool readyToWrite = true;

            if (job.compressKlei) {
                const int lvl = (job.deflateLevel >= 1 && job.deflateLevel <= 12) ? job.deflateLevel : 6;
                if (!workerComp || workerCompLevel != lvl) {
                    if (workerComp) libdeflate_free_compressor(workerComp);
                    workerComp = libdeflate_alloc_compressor(lvl);
                    workerCompLevel = lvl;
                }
                const int64_t tc0 = NowQpc();
                if (workerComp && klei_save_codec::EncodeKleiZlibBase64(
                                      job.data.data(), job.data.size(), lvl, encodedBuf, workerComp)) {
                    asyncCompressedCount_.fetch_add(1, std::memory_order_relaxed);
                    asyncCompressUsSum_.fetch_add(QpcUs(tc0, NowQpc()), std::memory_order_relaxed);
                    writePtr = encodedBuf.data();
                    writeSize = encodedBuf.size();
                } else {
                    readyToWrite = false;
                }
            }

            const int64_t t0 = NowQpc();
            const bool ok = readyToWrite && WriteAtomicFile(job.rawPath, writePtr, writeSize, nullptr, flushFn_);
            const uint64_t ioUs = QpcUs(t0, NowQpc());

            if (ok) {
                committedCount_.fetch_add(1, std::memory_order_relaxed);
                committedBytes_.fetch_add(writeSize, std::memory_order_relaxed);
                ioUsSum_.fetch_add(ioUs, std::memory_order_relaxed);
            } else {
                errorCount_.fetch_add(1, std::memory_order_relaxed);
            }

            std::vector<uint8_t>().swap(job.data);
            if (encodedBuf.capacity() > 4u * 1024u * 1024u) {
                std::vector<uint8_t>().swap(encodedBuf);
            }

            EnterCriticalSection(&cs_);
            const size_t rslot = static_cast<size_t>(job.seq & (kResultRingSize - 1u));
            resultRing_[rslot].seq = job.seq;
            resultRing_[rslot].ok  = ok;
            completedSeq_ = job.seq;
            if (pendingBytes_ >= queuedSize) {
                pendingBytes_ -= queuedSize;
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

    worker_exit:
        if (workerComp) {
            libdeflate_free_compressor(workerComp);
        }
    }

    bool HasQueuedOrActiveAsyncPathLocked(const std::wstring& canon) const noexcept {
        if (workerBusy_ && activeCanonPath_ == canon) return true;
        for (const auto& j : queue_) {
            if (j.canonPath == canon) return true;
        }
        return false;
    }

    struct ScopedSyncPathLock {
        AsyncSaveQueue&     owner;
        const std::wstring& canon;

        ScopedSyncPathLock(AsyncSaveQueue& q, const std::wstring& c) noexcept : owner(q), canon(c) {
            EnterCriticalSection(&owner.cs_);
            while (owner.activeSyncPaths_.count(canon) > 0 || owner.HasQueuedOrActiveAsyncPathLocked(canon)) {
                SleepConditionVariableCS(&owner.cvDone_, &owner.cs_, INFINITE);
            }
            owner.activeSyncPaths_.insert(canon);
            owner.activeSyncCount_.fetch_add(1, std::memory_order_release);
            LeaveCriticalSection(&owner.cs_);
        }

        ~ScopedSyncPathLock() noexcept {
            EnterCriticalSection(&owner.cs_);
            owner.activeSyncPaths_.erase(canon);
            owner.activeSyncCount_.fetch_sub(1, std::memory_order_acq_rel);
            WakeAllConditionVariable(&owner.cvDone_);
            WakeAllConditionVariable(&owner.cvWork_);
            LeaveCriticalSection(&owner.cs_);
        }
    };

    static constexpr size_t kResultRingSize = 256u;
    struct SeqResult {
        uint64_t seq = 0;
        bool     ok  = false;
    };

    size_t                           maxQueueBytes_;
    double                           qpcToUs_ = 0.1;
    CRITICAL_SECTION                 cs_{};
    CONDITION_VARIABLE               cvWork_{};
    CONDITION_VARIABLE               cvDone_{};
    CONDITION_VARIABLE               cvSpace_{};
    std::deque<SaveJob>              queue_;
    std::unordered_set<std::wstring> activeSyncPaths_;
    bool                             workerStarted_ = false;
    bool                             stopRequested_ = false;
    bool                             shuttingDown_  = false;
    bool                             workerBusy_    = false;
    std::wstring                     activeCanonPath_;
    uint64_t                         activeSeq_     = 0;
    uint64_t                         nextSeq_       = 0;
    uint64_t                         completedSeq_  = 0;
    size_t                           pendingBytes_  = 0;
    HANDLE                           workerThread_  = nullptr;
    SeqResult                        resultRing_[kResultRingSize]{};
    FlushFileBuffersFn_t             flushFn_       = &FlushFileBuffers;

    std::atomic<uint32_t>            pendingCount_{0};
    std::atomic<uint32_t>            activeSyncCount_{0};
    std::atomic<uint64_t> enqueuedCount_{0};
    std::atomic<uint64_t> committedCount_{0};
    std::atomic<uint64_t> committedBytes_{0};
    std::atomic<uint64_t> enqueueUsSum_{0};
    std::atomic<uint64_t> ioUsSum_{0};
    std::atomic<uint64_t> flushCount_{0};
    std::atomic<uint64_t> errorCount_{0};
    std::atomic<uint64_t> backpressureWaits_{0};
    std::atomic<uint64_t> asyncCompressedCount_{0};
    std::atomic<uint64_t> asyncCompressUsSum_{0};
};

} // namespace hc_async_save
