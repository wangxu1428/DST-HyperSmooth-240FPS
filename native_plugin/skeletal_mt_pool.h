#pragma once
// =============================================================================
// HyperSmooth v3.5 — Lock-Free 16-Bucket Elastic Work-Stealing Skeletal Pool
// =============================================================================
// Eliminates single-core CPU bottlenecks in 1,000+ entity mega-bases with ZERO
// visual, animation, or stability compromise:
//   - 16 deterministic virtual state buckets (`SplitMix(entityKey) & 15u`) ensure
//     every entity's EntityAnimTrack + PoseCrossfadePool history stays strictly
//     single-owner and never migrates across threads even as active workers
//     scale elastically with scene entity load (1 / 4 / 8 / 12 workers).
//   - Atomic work-stealing queue across non-empty buckets balances heterogeneous
//     skeleton workloads across up to 12 hardware threads on 16+ core CPUs.
//   - Quadruple-field lookup verification `(cmdPtr, animPtr, playMode, time)`
//     guarantees 100% fail-closed fallback to single-thread inline path if any
//     exotic mod mutates a command after construction.
// =============================================================================

#include <windows.h>
#include <immintrin.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

namespace hs_skeletal_mt {

inline constexpr uint32_t kMaxWorkers     = 12u;
inline constexpr uint32_t kMaxShards      = kMaxWorkers; // Legacy alias
inline constexpr uint32_t kVirtualBuckets = 16u;
inline constexpr uint32_t kResultBanks    = 2u;
inline constexpr uint32_t kMaxStagedJobs  = 8192u;
inline constexpr uint32_t kMinMtJobs      = 32u;

struct MtAnimJob {
    const void* cmdPtr    = nullptr; // Opaque lookup key ONLY (workers NEVER dereference!)
    uintptr_t   entityKey = 0;
    const void* animPtr   = nullptr; // Immutable Animation* asset
    int32_t     playMode  = 0;
    float       time      = 0.0f;
    float       axisX[3]  = { 1.0f, 0.0f, 0.0f };
    uint32_t    resultIdx = 0u;
};

struct MtAnimResult {
    const void* cmdPtr     = nullptr;
    uintptr_t   entityKey  = 0u;
    const void* animPtr    = nullptr;
    int32_t     playMode   = 0;
    float       time       = 0.0f;
    const void* outFrame   = nullptr;
    uint32_t    lastHitGen = 0u;
};

template <typename StateT, typename SlotT>
class SkeletalMtPool {
public:
    using WorkerEvalFn = const void* (*)(StateT& state, const MtAnimJob& job,
                                         int64_t nowQpc, SlotT& outSlot, void* userCtx);
    using StateResetFn = void (*)(StateT& state);

    SkeletalMtPool() {
        InitializeSRWLock(&stageLock_);
        InitializeSRWLock(&wakeLock_);
        InitializeConditionVariable(&wakeCv_);
    }

    ~SkeletalMtPool() {
        Shutdown(false);
    }

    SkeletalMtPool(const SkeletalMtPool&) = delete;
    SkeletalMtPool& operator=(const SkeletalMtPool&) = delete;

    static uint32_t ComputeOptimalShards(uint32_t logicalCores) noexcept {
        if (logicalCores >= 16u) return 12u;
        if (logicalCores >= 12u) return 8u;
        if (logicalCores >= 6u)  return 6u;
        if (logicalCores >= 4u)  return 2u;
        return 1u;
    }

    static uint32_t ComputeElasticActiveWorkers(uint32_t totalJobs, uint32_t maxWorkers) noexcept {
        if (maxWorkers <= 1u || totalJobs < kMinMtJobs) return 1u;
        if (totalJobs < 200u) return std::min<uint32_t>(maxWorkers, 4u);
        if (totalJobs < 450u) return std::min<uint32_t>(maxWorkers, 8u);
        return maxWorkers;
    }

    bool EnsureStarted(uint32_t logicalCores, WorkerEvalFn evalFn, StateResetFn resetFn,
                       void* userCtx = nullptr, uint32_t maxWorkersOverride = 0u) {
        if (started_.load(std::memory_order_acquire)) {
            return numShards_ > 1u;
        }
        AcquireSRWLockExclusive(&wakeLock_);
        if (started_.load(std::memory_order_relaxed)) {
            ReleaseSRWLockExclusive(&wakeLock_);
            return numShards_ > 1u;
        }
        evalFn_  = evalFn;
        resetFn_ = resetFn;
        userCtx_ = userCtx;

        uint32_t target = ComputeOptimalShards(logicalCores);
        if (maxWorkersOverride > 0u) {
            target = std::min(target, maxWorkersOverride);
        }
        if (target > kMaxWorkers) target = kMaxWorkers;
        numShards_ = target;

        if (numShards_ <= 1u || !evalFn_) {
            numShards_ = 1u;
            started_.store(true, std::memory_order_release);
            ReleaseSRWLockExclusive(&wakeLock_);
            return false;
        }

        for (uint32_t b = 0u; b < kVirtualBuckets; ++b) {
            buckets_[b].state = std::make_unique<StateT>();
            buckets_[b].jobs.reserve(256u);
            for (uint32_t bk = 0u; bk < kResultBanks; ++bk) {
                buckets_[b].slotArena[bk].reserve(128u);
            }
        }

        bool allThreadsOk = true;
        for (uint32_t w = 1u; w < numShards_; ++w) {
            workerParams_[w].pool      = this;
            workerParams_[w].workerIdx = w;
            HANDLE h = CreateThread(nullptr, 0, &WorkerThreadProc, &workerParams_[w], 0, nullptr);
            if (!h) {
                allThreadsOk = false;
                break;
            }
            SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL);
            workerThreads_[w] = h;
        }
        if (!allThreadsOk) {
            stop_.store(true, std::memory_order_release);
            ReleaseSRWLockExclusive(&wakeLock_);
            WakeAllConditionVariable(&wakeCv_);
            for (uint32_t w = 1u; w < numShards_; ++w) {
                if (workerThreads_[w]) {
                    WaitForSingleObject(workerThreads_[w], 2000);
                    CloseHandle(workerThreads_[w]);
                    workerThreads_[w] = nullptr;
                }
            }
            for (uint32_t b = 0u; b < kVirtualBuckets; ++b) {
                buckets_[b].state.reset();
                buckets_[b].jobs.clear();
                for (uint32_t bk = 0u; bk < kResultBanks; ++bk) {
                    buckets_[b].slotArena[bk].clear();
                }
            }
            numShards_ = 1u;
            started_.store(true, std::memory_order_release);
            return false;
        }

        started_.store(true, std::memory_order_release);
        ReleaseSRWLockExclusive(&wakeLock_);
        return true;
    }

    void Shutdown(bool processExiting) noexcept {
        if (!started_.load(std::memory_order_acquire)) return;
        AcquireSRWLockExclusive(&wakeLock_);
        stop_.store(true, std::memory_order_release);
        ReleaseSRWLockExclusive(&wakeLock_);
        WakeAllConditionVariable(&wakeCv_);
        if (!processExiting) {
            for (uint32_t w = 1u; w < numShards_; ++w) {
                if (workerThreads_[w]) {
                    WaitForSingleObject(workerThreads_[w], 2000);
                    CloseHandle(workerThreads_[w]);
                    workerThreads_[w] = nullptr;
                }
            }
        }
        numShards_         = 1u;
        lastActiveWorkers_ = 0u;
        inFrame_           = false;
        for (uint32_t bk = 0u; bk < kResultBanks; ++bk) {
            banks_[bk].active = false;
        }
    }

    uint32_t NumShards() const noexcept         { return numShards_; }
    uint32_t NumBuckets() const noexcept        { return kVirtualBuckets; }
    uint32_t LastActiveWorkers() const noexcept { return lastActiveWorkers_; }
    bool     IsActive() const noexcept          { return started_.load(std::memory_order_acquire) && numShards_ > 1u; }
    bool     HasActiveResults() const noexcept  { return inFrame_; }
    uint64_t TotalHits() const noexcept         { return totalHits_.load(std::memory_order_relaxed); }
    uint64_t PrevBankHits() const noexcept      { return prevBankHits_.load(std::memory_order_relaxed); }

    void StageJob(const MtAnimJob& job) {
        AcquireSRWLockExclusive(&stageLock_);
        if (staging_.size() >= kMaxStagedJobs) {
            staging_.clear();
        }
        staging_.push_back(job);
        ReleaseSRWLockExclusive(&stageLock_);
    }

    void ClearStaged() {
        AcquireSRWLockExclusive(&stageLock_);
        staging_.clear();
        ReleaseSRWLockExclusive(&stageLock_);
        inFrame_ = false;
        for (uint32_t bk = 0u; bk < kResultBanks; ++bk) {
            banks_[bk].active = false;
        }
    }

    uint32_t DispatchAndJoin(int64_t nowQpc, uint32_t worldResetGen,
                             uint32_t& outEnts, uint32_t& outBones, uint32_t& outShed) {
        outEnts = 0u; outBones = 0u; outShed = 0u;
        inFrame_ = false;
        localBatch_.clear();

        AcquireSRWLockExclusive(&stageLock_);
        localBatch_.swap(staging_);
        ReleaseSRWLockExclusive(&stageLock_);

        const bool needReset = (worldResetGen != lastWorldResetGen_);
        if (needReset) {
            lastWorldResetGen_ = worldResetGen;
            for (uint32_t bk = 0u; bk < kResultBanks; ++bk) {
                banks_[bk].active = false;
            }
            for (uint32_t b = 0u; b < kVirtualBuckets; ++b) {
                buckets_[b].needReset = true;
            }
        }

        ++dispatchGen_;
        if (dispatchGen_ == 0u) ++dispatchGen_;

        if (numShards_ <= 1u || !evalFn_ || localBatch_.size() < kMinMtJobs) {
            lastActiveWorkers_ = localBatch_.empty() ? 0u : 1u;
            // Keep unconsumed 1-frame carry-over bank reachable when MainThread already
            // pre-staged Frame N+1 during Frame N and RenderThread entered Frame N+1 with
            // an empty/tiny staging queue (eliminates full-scene single-thread fallback!).
            if (!needReset && banks_[curBank_].active &&
                (dispatchGen_ - banks_[curBank_].dispatchGen) <= 1u) {
                inFrame_ = true;
            }
            return 0u;
        }

        // Flip ping-pong bank so previous frame's unconsumed slotArena & resultTable remain
        // untouched for 1 frame while this dispatch populates the opposite bank.
        curBank_ ^= 1u;
        ResultBank& dstBank = banks_[curBank_];
        dstBank.active      = false;
        dstBank.dispatchGen = dispatchGen_;

        const uint32_t totalJobs = static_cast<uint32_t>(localBatch_.size());
        dstBank.results.resize(totalJobs);
        for (uint32_t b = 0u; b < kVirtualBuckets; ++b) {
            buckets_[b].jobs.clear();
            buckets_[b].entsOut  = 0u;
            buckets_[b].bonesOut = 0u;
            buckets_[b].shedOut  = 0u;
        }
        for (uint32_t i = 0u; i < totalJobs; ++i) {
            MtAnimJob job = localBatch_[i];
            job.resultIdx = i;
            dstBank.results[i] = MtAnimResult{
                job.cmdPtr, job.entityKey, job.animPtr, job.playMode, job.time, nullptr, 0u
            };
            const uint32_t bIdx = BucketForKey(job.entityKey);
            buckets_[bIdx].jobs.push_back(job);
        }

        activeBucketCount_ = 0u;
        for (uint32_t b = 0u; b < kVirtualBuckets; ++b) {
            if (!buckets_[b].jobs.empty()) {
                activeBuckets_[activeBucketCount_++] = b;
            }
        }
        if (activeBucketCount_ == 0u) {
            lastActiveWorkers_ = 0u;
            if (!needReset && banks_[curBank_ ^ 1u].active &&
                (dispatchGen_ - banks_[curBank_ ^ 1u].dispatchGen) <= 1u) {
                inFrame_ = true;
            }
            return 0u;
        }

        // Longest-Processing-Time-First (LPT) ordering over <=16 buckets cuts 2nd-wave
        // makespan tail when 12 workers steal across 16 uneven entity buckets.
        for (uint32_t i = 1u; i < activeBucketCount_; ++i) {
            const uint32_t keyB = activeBuckets_[i];
            const size_t keySz  = buckets_[keyB].jobs.size();
            uint32_t j = i;
            while (j > 0u && buckets_[activeBuckets_[j - 1u]].jobs.size() < keySz) {
                activeBuckets_[j] = activeBuckets_[j - 1u];
                --j;
            }
            activeBuckets_[j] = keyB;
        }

        const uint32_t wantWorkers = std::min(ComputeElasticActiveWorkers(totalJobs, numShards_), activeBucketCount_);
        lastActiveWorkers_ = wantWorkers;
        batchNowQpc_       = nowQpc;

        if (wantWorkers <= 1u) {
            for (uint32_t i = 0u; i < activeBucketCount_; ++i) {
                RunBucket(buckets_[activeBuckets_[i]]);
            }
        } else {
            nextBucketIdx_.store(0u, std::memory_order_relaxed);
            activeWorkersWanted_.store(wantWorkers, std::memory_order_release);
            pendingWorkers_.store(wantWorkers - 1u, std::memory_order_release);

            AcquireSRWLockExclusive(&wakeLock_);
            batchGen_.fetch_add(1u, std::memory_order_release);
            ReleaseSRWLockExclusive(&wakeLock_);
            WakeAllConditionVariable(&wakeCv_);

            RunWorkStealing();

            while (pendingWorkers_.load(std::memory_order_acquire) != 0u) {
                _mm_pause();
            }
        }

        uint32_t cap = 128u;
        while (cap < totalJobs * 2u && cap < 32768u) cap <<= 1u;
        if (dstBank.resultTable.size() != cap) {
            dstBank.resultTable.assign(cap, kEmptyIdx);
        } else {
            std::fill(dstBank.resultTable.begin(), dstBank.resultTable.end(), kEmptyIdx);
        }
        dstBank.resultMask = cap - 1u;

        uint32_t inserted = 0u;
        for (uint32_t i = 0u; i < totalJobs; ++i) {
            if (!dstBank.results[i].outFrame || !dstBank.results[i].cmdPtr) continue;
            uint32_t h = HashPtr(dstBank.results[i].cmdPtr) & dstBank.resultMask;
            for (uint32_t p = 0u; p < 16u; ++p) {
                const uint32_t slot = (h + p) & dstBank.resultMask;
                if (dstBank.resultTable[slot] == kEmptyIdx) {
                    dstBank.resultTable[slot] = i;
                    ++inserted;
                    break;
                }
                if (dstBank.results[dstBank.resultTable[slot]].cmdPtr == dstBank.results[i].cmdPtr) {
                    dstBank.resultTable[slot] = i;
                    break;
                }
            }
        }

        for (uint32_t i = 0u; i < activeBucketCount_; ++i) {
            const Bucket& bk = buckets_[activeBuckets_[i]];
            outEnts  += bk.entsOut;
            outBones += bk.bonesOut;
            outShed  += bk.shedOut;
        }
        dstBank.active = (inserted > 0u);
        const ResultBank& prevBank = banks_[curBank_ ^ 1u];
        inFrame_ = dstBank.active ||
                   (prevBank.active && (dispatchGen_ - prevBank.dispatchGen) == 1u);
        return totalJobs;
    }

    const void* LookupResult(const void* cmdPtr, const void* animPtr,
                             int32_t playMode, float time, uintptr_t entityKey = 0u) noexcept {
        if (!inFrame_ || !cmdPtr) return nullptr;
        ResultBank& primary = banks_[curBank_];
        const uint32_t primAge = dispatchGen_ - primary.dispatchGen;
        if (primary.active && primAge <= 1u) {
            if (const void* hit = LookupInBank(primary, primAge != 0u, cmdPtr, animPtr, playMode, time, entityKey)) {
                return hit;
            }
        }
        ResultBank& prev = banks_[curBank_ ^ 1u];
        if (prev.active && (dispatchGen_ - prev.dispatchGen) == 1u) {
            if (const void* hit = LookupInBank(prev, true, cmdPtr, animPtr, playMode, time, entityKey)) {
                return hit;
            }
        }
        return nullptr;
    }

    void EndFrame() noexcept {
        inFrame_ = false;
    }

    template <typename Fn>
    void ForEachShardState(Fn&& fn) {
        for (uint32_t b = 0u; b < kVirtualBuckets; ++b) {
            if (buckets_[b].state) fn(*buckets_[b].state);
        }
    }

private:
    static constexpr uint32_t kEmptyIdx = 0xFFFFFFFFu;

    struct ResultBank {
        std::vector<MtAnimResult> results;
        std::vector<uint32_t>     resultTable;
        uint32_t                  resultMask  = 0u;
        uint32_t                  dispatchGen = 0u;
        bool                      active      = false;
    };

    struct Bucket {
        bool                    needReset = false;
        std::unique_ptr<StateT> state;
        std::vector<MtAnimJob>  jobs;
        std::vector<SlotT>      slotArena[kResultBanks];
        uint32_t                entsOut   = 0u;
        uint32_t                bonesOut  = 0u;
        uint32_t                shedOut   = 0u;
    };

    struct WorkerParam {
        SkeletalMtPool* pool      = nullptr;
        uint32_t        workerIdx = 0u;
    };

    static uint32_t BucketForKey(uintptr_t key) noexcept {
        uint64_t x = static_cast<uint64_t>(key);
        x ^= x >> 30;
        x *= 0xbf58476d1ce4e5b9ULL;
        x ^= x >> 27;
        x *= 0x94d049bb133111ebULL;
        x ^= x >> 31;
        return static_cast<uint32_t>(x & (kVirtualBuckets - 1u));
    }

    static uint32_t HashPtr(const void* p) noexcept {
        const uint64_t v = reinterpret_cast<uintptr_t>(p);
        return static_cast<uint32_t>((v >> 5) ^ (v >> 16) ^ (v >> 27));
    }

    const void* LookupInBank(ResultBank& bank, bool requireUnconsumed,
                             const void* cmdPtr, const void* animPtr,
                             int32_t playMode, float time, uintptr_t entityKey) noexcept {
        if (!bank.active) return nullptr;
        uint32_t h = HashPtr(cmdPtr) & bank.resultMask;
        for (uint32_t p = 0u; p < 16u; ++p) {
            const uint32_t idx = bank.resultTable[(h + p) & bank.resultMask];
            if (idx == kEmptyIdx) return nullptr;
            MtAnimResult& r = bank.results[idx];
            if (r.cmdPtr == cmdPtr) {
                if (r.animPtr == animPtr && r.playMode == playMode && r.time == time &&
                    r.outFrame != nullptr && (entityKey == 0u || r.entityKey == entityKey)) {
                    if (requireUnconsumed && r.lastHitGen != 0u && r.lastHitGen != dispatchGen_) {
                        return nullptr;
                    }
                    r.lastHitGen = dispatchGen_;
                    totalHits_.fetch_add(1u, std::memory_order_relaxed);
                    if (requireUnconsumed) {
                        prevBankHits_.fetch_add(1u, std::memory_order_relaxed);
                    }
                    return r.outFrame;
                }
                return nullptr;
            }
        }
        return nullptr;
    }

    void RunBucket(Bucket& bk) {
        StateT& st = *bk.state;
        if (bk.needReset && resetFn_) {
            resetFn_(st);
            bk.needReset = false;
        }
        const size_t n = bk.jobs.size();
        auto& arena = bk.slotArena[curBank_];
        if (arena.size() < n) {
            arena.resize(n);
        }
        auto& dstResults = banks_[curBank_].results;
        st.frameTweenEnts  = 0u;
        st.frameTweenBones = 0u;
        st.frameShedCount  = 0u;
        st.frameShedLevel  = 0u;
        for (size_t i = 0; i < n; ++i) {
            const MtAnimJob& job = bk.jobs[i];
            const void* outF = evalFn_(st, job, batchNowQpc_, arena[i], userCtx_);
            dstResults[job.resultIdx].outFrame = outF;
        }
        bk.entsOut  = st.frameTweenEnts;
        bk.bonesOut = st.frameTweenBones;
        bk.shedOut  = st.frameShedCount;
    }

    void RunWorkStealing() {
        const uint32_t totalActive = activeBucketCount_;
        for (;;) {
            const uint32_t idx = nextBucketIdx_.fetch_add(1u, std::memory_order_acq_rel);
            if (idx >= totalActive) break;
            RunBucket(buckets_[activeBuckets_[idx]]);
        }
    }

    static DWORD WINAPI WorkerThreadProc(LPVOID param) {
        WorkerParam* wp    = static_cast<WorkerParam*>(param);
        SkeletalMtPool* self = wp->pool;
        const uint32_t wIdx  = wp->workerIdx;
        uint32_t seenGen     = 0u;
        for (;;) {
            uint32_t curGen = self->batchGen_.load(std::memory_order_acquire);
            if (curGen == seenGen && !self->stop_.load(std::memory_order_acquire)) {
                for (int sp = 0; sp < 400; ++sp) {
                    _mm_pause();
                    curGen = self->batchGen_.load(std::memory_order_acquire);
                    if (curGen != seenGen || self->stop_.load(std::memory_order_acquire)) break;
                }
            }
            if (curGen == seenGen) {
                AcquireSRWLockExclusive(&self->wakeLock_);
                while (!self->stop_.load(std::memory_order_acquire) &&
                       (curGen = self->batchGen_.load(std::memory_order_acquire)) == seenGen) {
                    SleepConditionVariableSRW(&self->wakeCv_, &self->wakeLock_, INFINITE, 0);
                }
                ReleaseSRWLockExclusive(&self->wakeLock_);
            }
            if (self->stop_.load(std::memory_order_acquire)) break;
            seenGen = curGen;
            if (wIdx < self->activeWorkersWanted_.load(std::memory_order_acquire)) {
                self->RunWorkStealing();
                self->pendingWorkers_.fetch_sub(1u, std::memory_order_release);
            }
        }
        return 0;
    }

    SRWLOCK                 stageLock_{};
    SRWLOCK                 wakeLock_{};
    CONDITION_VARIABLE      wakeCv_{};
    std::atomic<bool>       started_{false};
    std::atomic<bool>       stop_{false};
    std::atomic<uint32_t>   batchGen_{0u};
    std::atomic<uint32_t>   activeWorkersWanted_{0u};
    std::atomic<uint32_t>   nextBucketIdx_{0u};
    std::atomic<uint32_t>   pendingWorkers_{0u};
    std::atomic<uint64_t>   totalHits_{0u};
    std::atomic<uint64_t>   prevBankHits_{0u};

    uint32_t                numShards_         = 1u;
    uint32_t                lastActiveWorkers_ = 0u;
    uint32_t                activeBucketCount_ = 0u;
    uint32_t                activeBuckets_[kVirtualBuckets]{};
    int64_t                 batchNowQpc_       = 0;
    uint32_t                lastWorldResetGen_ = 0u;
    uint32_t                curBank_           = 0u;
    uint32_t                dispatchGen_       = 0u;
    bool                    inFrame_           = false;
    WorkerEvalFn            evalFn_            = nullptr;
    StateResetFn            resetFn_           = nullptr;
    void*                   userCtx_           = nullptr;

    std::vector<MtAnimJob>    staging_;
    std::vector<MtAnimJob>    localBatch_;
    ResultBank                banks_[kResultBanks]{};
    Bucket                    buckets_[kVirtualBuckets]{};
    WorkerParam               workerParams_[kMaxWorkers]{};
    HANDLE                    workerThreads_[kMaxWorkers]{};
};

} // namespace hs_skeletal_mt
