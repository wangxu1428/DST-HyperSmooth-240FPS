#pragma once
// =============================================================================
// Thread-Safe Lifetime-Managed FastAStar StateManager (Shared Server + Client)
// =============================================================================
// Guarantees ZERO use-after-free, ZERO data races, and ZERO same-slot ABA even
// under arbitrary concurrent Init / Step / Reset / pool-eviction interleavings:
//   - Each StateSlot owns an intrusive atomic refcount + per-slot opLock.
//   - Each map binding receives a monotonically increasing 64-bit generation
//     (`nextGen_`, mutated exclusively under `mapLock_`).
//   - AcquireAndBind / AcquireRef increment refs and snapshot `gen` BEFORE
//     releasing mapLock_, returning an RAII StateHandle that acquires opLock
//     AFTER mapLock_ exits.
//   - DetachIfMatch(key, slot, gen) detaches ONLY when both slot pointer AND
//     binding generation match, preventing stale Step completions from detaching
//     a re-bound search that reused the same slot.
//   - Strictly acyclic locking: mapLock_, opLock, and poolLock_ are NEVER nested
//     in conflicting orders.
// =============================================================================

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

#include "fast_astar.h"

namespace fast_astar {

struct RwLock {
#ifdef _WIN32
    SRWLOCK lk = SRWLOCK_INIT;
    inline void LockExclusive() noexcept { AcquireSRWLockExclusive(&lk); }
    inline void UnlockExclusive() noexcept { ReleaseSRWLockExclusive(&lk); }
    inline void LockShared() noexcept { AcquireSRWLockShared(&lk); }
    inline void UnlockShared() noexcept { ReleaseSRWLockShared(&lk); }
#else
    pthread_rwlock_t lk = PTHREAD_RWLOCK_INITIALIZER;
    inline void LockExclusive() noexcept { pthread_rwlock_wrlock(&lk); }
    inline void UnlockExclusive() noexcept { pthread_rwlock_unlock(&lk); }
    inline void LockShared() noexcept { pthread_rwlock_rdlock(&lk); }
    inline void UnlockShared() noexcept { pthread_rwlock_unlock(&lk); }
#endif
};

template <size_t kMaxWarmPool = 64u>
class StateManager {
public:
    struct StateSlot {
        SearchState           state;
        RwLock                opLock;
        std::atomic<uint32_t> refs{1};
    };

    class StateHandle {
    public:
        StateHandle() = default;
        StateHandle(StateManager* mgr, StateSlot* slot, uint64_t gen, bool lockOp) noexcept
            : mgr_(mgr), slot_(slot), gen_(gen), locked_(false) {
            if (slot_ && lockOp) {
                slot_->opLock.LockExclusive();
                locked_ = true;
            }
        }
        StateHandle(const StateHandle&) = delete;
        StateHandle& operator=(const StateHandle&) = delete;
        StateHandle(StateHandle&& o) noexcept
            : mgr_(o.mgr_), slot_(o.slot_), gen_(o.gen_), locked_(o.locked_) {
            o.mgr_ = nullptr;
            o.slot_ = nullptr;
            o.gen_ = 0;
            o.locked_ = false;
        }
        StateHandle& operator=(StateHandle&& o) noexcept {
            if (this != &o) {
                Release();
                mgr_ = o.mgr_;
                slot_ = o.slot_;
                gen_ = o.gen_;
                locked_ = o.locked_;
                o.mgr_ = nullptr;
                o.slot_ = nullptr;
                o.gen_ = 0;
                o.locked_ = false;
            }
            return *this;
        }
        ~StateHandle() { Release(); }

        void Release() noexcept {
            if (slot_) {
                if (locked_) {
                    slot_->opLock.UnlockExclusive();
                    locked_ = false;
                }
                if (mgr_) {
                    mgr_->ReleaseSlotRef(slot_);
                }
                slot_ = nullptr;
                mgr_ = nullptr;
                gen_ = 0;
            }
        }

        explicit operator bool() const noexcept { return slot_ != nullptr; }
        StateSlot*   Slot() const noexcept { return slot_; }
        uint64_t     Gen() const noexcept { return gen_; }
        SearchState* Get() const noexcept { return slot_ ? &slot_->state : nullptr; }
        SearchState* operator->() const noexcept { return &slot_->state; }

    private:
        StateManager* mgr_    = nullptr;
        StateSlot*    slot_   = nullptr;
        uint64_t      gen_    = 0;
        bool          locked_ = false;
    };

    StateManager() = default;
    ~StateManager() {
        mapLock_.LockExclusive();
        for (auto& e : map_) {
            if (e.key && reinterpret_cast<uintptr_t>(e.key) != kTombstoneVal && e.slot) {
                delete e.slot;
                e.slot = nullptr;
                e.gen = 0;
            }
        }
        map_.clear();
        count_ = 0;
        used_ = 0;
        mapLock_.UnlockExclusive();

        poolLock_.LockExclusive();
        for (StateSlot* s : pool_) {
            delete s;
        }
        pool_.clear();
        poolLock_.UnlockExclusive();
    }

    StateHandle AcquireAndBind(void* key) {
        if (!key) return StateHandle{};

        StateSlot* candidate = nullptr;
        StateSlot* chosen = nullptr;
        uint64_t chosenGen = 0;

        mapLock_.LockExclusive();
        EnsureMapCapLocked();
        const size_t mask = map_.size() - 1;
        size_t idx = HashPtr(key) & mask;
        size_t firstTomb = SIZE_MAX;
        while (true) {
            void* k = map_[idx].key;
            if (k == key) {
                chosen = map_[idx].slot;
                if (chosen) {
                    const uint64_t g = ++nextGen_;
                    map_[idx].gen = g;
                    chosenGen = g;
                    chosen->refs.fetch_add(1, std::memory_order_acq_rel);
                }
                break;
            }
            if (k == nullptr) {
                size_t dst = (firstTomb != SIZE_MAX) ? firstTomb : idx;
                candidate = PopFromPoolOrAlloc();
                if (candidate) {
                    // 1 ref for map_ + 1 ref for returned StateHandle
                    candidate->refs.store(2, std::memory_order_release);
                    const uint64_t g = ++nextGen_;
                    map_[dst] = Entry{key, candidate, g};
                    ++count_;
                    if (firstTomb == SIZE_MAX) ++used_;
                    chosen = candidate;
                    chosenGen = g;
                }
                break;
            }
            if (reinterpret_cast<uintptr_t>(k) == kTombstoneVal && firstTomb == SIZE_MAX) {
                firstTomb = idx;
            }
            idx = (idx + 1) & mask;
        }
        mapLock_.UnlockExclusive();

        return StateHandle(this, chosen, chosenGen, true);
    }

    StateHandle AcquireRef(void* key) {
        if (!key) return StateHandle{};

        StateSlot* chosen = nullptr;
        uint64_t chosenGen = 0;
        mapLock_.LockShared();
        if (!map_.empty()) {
            const size_t mask = map_.size() - 1;
            size_t idx = HashPtr(key) & mask;
            while (true) {
                void* k = map_[idx].key;
                if (k == key) {
                    chosen = map_[idx].slot;
                    if (chosen) {
                        chosenGen = map_[idx].gen;
                        chosen->refs.fetch_add(1, std::memory_order_acq_rel);
                    }
                    break;
                }
                if (k == nullptr) break;
                idx = (idx + 1) & mask;
            }
        }
        mapLock_.UnlockShared();

        return StateHandle(this, chosen, chosenGen, true);
    }

    void DetachAndRecycle(void* key) {
        DetachInternal(key, nullptr, 0, false);
    }

    // Detaches key ONLY if it is still bound to (expectedSlot, expectedGen),
    // eliminating same-slot ABA across concurrent re-Init/Reset interleavings.
    void DetachIfMatch(void* key, StateSlot* expectedSlot, uint64_t expectedGen) {
        if (!expectedSlot || expectedGen == 0) return;
        DetachInternal(key, expectedSlot, expectedGen, true);
    }

    // Diagnostic counters (useful for red-team stress tests)
    size_t ActiveCount() {
        mapLock_.LockShared();
        size_t n = count_;
        mapLock_.UnlockShared();
        return n;
    }

    size_t WarmPoolSize() {
        poolLock_.LockShared();
        size_t n = pool_.size();
        poolLock_.UnlockShared();
        return n;
    }

private:
    struct Entry {
        void*      key;   // nullptr = empty, kTombstoneVal = deleted
        StateSlot* slot;
        uint64_t   gen = 0;
    };
    static constexpr uintptr_t kTombstoneVal = 1u;

    RwLock                  mapLock_;
    RwLock                  poolLock_;
    std::vector<Entry>      map_;
    size_t                  count_   = 0;
    size_t                  used_    = 0;
    uint64_t                nextGen_ = 1; // monotonically incremented ONLY under exclusive mapLock_
    std::vector<StateSlot*> pool_;

    static inline size_t HashPtr(void* p) noexcept {
        uint64_t x = reinterpret_cast<uintptr_t>(p);
        x ^= x >> 33;
        x *= 0xFF51AFD7ED558CCDULL;
        x ^= x >> 33;
        return static_cast<size_t>(x);
    }

    void EnsureMapCapLocked() {
        if (map_.empty()) {
            map_.assign(128, Entry{nullptr, nullptr, 0});
            return;
        }
        if ((used_ + 1) * 10 >= map_.size() * 6) {
            size_t newCap = (count_ * 10 >= map_.size() * 5) ? (map_.size() << 1) : map_.size();
            std::vector<Entry> old = std::move(map_);
            map_.assign(newCap, Entry{nullptr, nullptr, 0});
            count_ = 0;
            used_  = 0;
            const size_t mask = newCap - 1;
            for (const auto& e : old) {
                if (e.key && reinterpret_cast<uintptr_t>(e.key) != kTombstoneVal) {
                    size_t idx = HashPtr(e.key) & mask;
                    while (map_[idx].key != nullptr) idx = (idx + 1) & mask;
                    map_[idx] = e;
                    ++count_;
                    ++used_;
                }
            }
        }
    }

    StateSlot* PopFromPoolOrAlloc() {
        StateSlot* slot = nullptr;
        poolLock_.LockExclusive();
        if (!pool_.empty()) {
            slot = pool_.back();
            pool_.pop_back();
        }
        poolLock_.UnlockExclusive();
        if (!slot) {
            slot = new (std::nothrow) StateSlot();
        }
        return slot;
    }

    void DetachInternal(void* key, StateSlot* expectedSlot, uint64_t expectedGen, bool checkMatch) {
        if (!key) return;
        StateSlot* detached = nullptr;
        mapLock_.LockExclusive();
        if (!map_.empty()) {
            const size_t mask = map_.size() - 1;
            size_t idx = HashPtr(key) & mask;
            while (true) {
                void* k = map_[idx].key;
                if (k == key) {
                    if (!checkMatch || (map_[idx].slot == expectedSlot && map_[idx].gen == expectedGen)) {
                        detached = map_[idx].slot;
                        map_[idx].key = reinterpret_cast<void*>(kTombstoneVal);
                        map_[idx].slot = nullptr;
                        map_[idx].gen = 0;
                        --count_;
                    }
                    break;
                }
                if (k == nullptr) break;
                idx = (idx + 1) & mask;
            }
        }
        mapLock_.UnlockExclusive();

        if (detached) {
            ReleaseSlotRef(detached);
        }
    }

    void ReleaseSlotRef(StateSlot* slot) noexcept {
        if (!slot) return;
        if (slot->refs.fetch_sub(1, std::memory_order_acq_rel) == 1u) {
            slot->state.ClearFast();
            bool recycled = false;
            poolLock_.LockExclusive();
            if (pool_.size() < kMaxWarmPool) {
                pool_.push_back(slot);
                recycled = true;
            }
            poolLock_.UnlockExclusive();
            if (!recycled) {
                delete slot;
            }
        }
    }
};

} // namespace fast_astar
