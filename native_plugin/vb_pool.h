#pragma once

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>

namespace hs_vb_pool {

inline constexpr uint32_t kGlArrayBuffer        = 0x8892u;
inline constexpr uint32_t kGlElementArrayBuffer = 0x8893u;
inline constexpr uint32_t kGlStreamDraw         = 0x88E0u;
inline constexpr uint32_t kGlStaticDraw         = 0x88E4u;
inline constexpr uint32_t kGlDynamicDraw        = 0x88E8u;

inline constexpr uint32_t kMapWriteBit              = 0x0002u;
inline constexpr uint32_t kMapInvalidateBufferBit   = 0x0008u;
inline constexpr uint32_t kMapUnsynchronizedBit     = 0x0020u;
// Pure WRITE|INVALIDATE_BUFFER (0x0A): safe zero-stall driver orphaning with 0% GPU/CPU race.
inline constexpr uint32_t kMapWriteInvalidateUnsync = kMapWriteBit | kMapInvalidateBufferBit; // 0x0A

inline constexpr size_t   kMinBucketBytes     = 256u;
inline constexpr size_t   kPow2ThresholdBytes = 65536u;             // 64 KB
inline constexpr size_t   kMaxPoolableBytes   = 2u * 1024u * 1024u; // 2 MB
inline constexpr size_t   kMaxPooledBytes     = 32u * 1024u * 1024u;// 32 MB
inline constexpr size_t   kMaxPooledCount     = 512u;
inline constexpr uint64_t kMinRetireAgeFrames = 6u;
inline constexpr uint64_t kIdleEvictFrames    = 600u;

struct GlBufferOps {
    using GenBuffers_t      = void (WINAPI*)(int32_t n, uint32_t* buffers);
    using BindBuffer_t      = void (WINAPI*)(uint32_t target, uint32_t buffer);
    using BufferData_t      = void (WINAPI*)(uint32_t target, ptrdiff_t size, const void* data, uint32_t usage);
    using BufferSubData_t   = void (WINAPI*)(uint32_t target, ptrdiff_t offset, ptrdiff_t size, const void* data);
    using DeleteBuffers_t   = void (WINAPI*)(int32_t n, const uint32_t* buffers);
    using MapBufferRange_t  = void* (WINAPI*)(uint32_t target, ptrdiff_t offset, ptrdiff_t length, uint32_t access);
    using UnmapBuffer_t     = uint8_t (WINAPI*)(uint32_t target);
    using CacheNotifyBind_t = void (*)(uint32_t target, uint32_t buffer, void* ctx);
    using CacheInvalidate_t = void (*)(uint32_t buffer, void* ctx);

    GenBuffers_t      glGenBuffers       = nullptr;
    BindBuffer_t      glBindBuffer       = nullptr;
    BufferData_t      glBufferData       = nullptr;
    BufferSubData_t   glBufferSubData    = nullptr;
    DeleteBuffers_t   glDeleteBuffers    = nullptr;
    MapBufferRange_t  glMapBufferRange   = nullptr;
    UnmapBuffer_t     glUnmapBuffer      = nullptr;
    CacheNotifyBind_t onBindBuffer       = nullptr;
    CacheInvalidate_t onInvalidateBuffer = nullptr;
    void*             callbackCtx        = nullptr;
};

struct Stats {
    int64_t  hits           = 0;
    int64_t  misses         = 0;
    int64_t  recycled       = 0;
    int64_t  evicted        = 0;
    int64_t  mapUploads     = 0;
    int64_t  subDataUploads = 0;
    size_t   activeCount    = 0;
    size_t   pooledCount    = 0;
    size_t   pooledBytes    = 0;
};

class VbPool {
public:
    VbPool() noexcept {
        InitializeSRWLock(&lock_);
    }

    static inline size_t BucketCapacity(size_t size) noexcept {
        if (size <= kMinBucketBytes) return kMinBucketBytes;
        if (size > kMaxPoolableBytes) return 0;
        if (size <= kPow2ThresholdBytes) {
            size_t b = kMinBucketBytes;
            while (b < size) b <<= 1;
            return b;
        }
        return (size + kPow2ThresholdBytes - 1u) & ~(kPow2ThresholdBytes - 1u);
    }

    static inline bool IsPoolable(uint32_t target, uint32_t usageEnum, uint32_t stride, uint32_t count,
                                  size_t& outSize, uint32_t& outGlUsage) noexcept {
        if (target != kGlArrayBuffer && target != kGlElementArrayBuffer) return false;
        if (usageEnum == 10u) {
            outGlUsage = kGlStreamDraw;
        } else if (usageEnum == 12u) {
            outGlUsage = kGlDynamicDraw;
        } else {
            return false; // 9 (GL_STATIC_DRAW) and unknown enums bypass pool
        }
        if (stride == 0u || count == 0u) return false;
        const uint64_t bytes64 = static_cast<uint64_t>(stride) * static_cast<uint64_t>(count);
        if (bytes64 == 0u || bytes64 > static_cast<uint64_t>(kMaxPoolableBytes)) return false;
        outSize = static_cast<size_t>(bytes64);
        return true;
    }

    void SetOps(const GlBufferOps& ops) noexcept {
        AcquireSRWLockExclusive(&lock_);
        ops_ = ops;
        ReleaseSRWLockExclusive(&lock_);
    }

    void SetLimits(size_t maxBytes, size_t maxCount, uint64_t idleEvictFrames) noexcept {
        AcquireSRWLockExclusive(&lock_);
        maxPooledBytes_  = std::max<size_t>(maxBytes, 8u * 1024u * 1024u);
        maxPooledCount_  = std::max<size_t>(maxCount, 128u);
        idleEvictFrames_ = std::max<uint64_t>(idleEvictFrames, 120u);
        ReleaseSRWLockExclusive(&lock_);
    }

    size_t MaxPooledBytes() noexcept {
        AcquireSRWLockShared(&lock_);
        const size_t mb = maxPooledBytes_;
        ReleaseSRWLockShared(&lock_);
        return mb;
    }

    bool AcquireAndUpload(uint32_t target, uint32_t glUsage, size_t size, const void* data,
                          bool useMapRange, uint32_t& outId) {
        outId = 0;
        if (size == 0 || size > kMaxPoolableBytes) return false;
        const size_t cap = BucketCapacity(size);
        if (cap == 0) return false;

        GlBufferOps ops{};
        uint32_t hitId = 0;
        const BucketKey key{target, glUsage, cap};

        AcquireSRWLockExclusive(&lock_);
        ops = ops_;
        if (!ops.glGenBuffers || !ops.glBindBuffer || !ops.glBufferData || !ops.glBufferSubData) {
            ReleaseSRWLockExclusive(&lock_);
            return false;
        }
        auto it = ready_.find(key);
        if (it != ready_.end() && !it->second.empty()) {
            ReadyEntry re = it->second.back();
            it->second.pop_back();
            if (it->second.empty()) ready_.erase(it);
            hitId = re.id;
            pooledBytes_ -= std::min(pooledBytes_, cap);
            if (pooledCount_ > 0) --pooledCount_;
            activeMap_[hitId] = ActiveEntry{target, glUsage, cap};
            ++hits_;
        }
        ReleaseSRWLockExclusive(&lock_);

        auto bindAndNotify = [&](uint32_t id) {
            ops.glBindBuffer(target, id);
            if (ops.onBindBuffer) {
                ops.onBindBuffer(target, id, ops.callbackCtx);
            }
        };

        if (hitId != 0) {
            bindAndNotify(hitId);
            if (data != nullptr) {
                bool uploaded = false;
                if (useMapRange && ops.glMapBufferRange && ops.glUnmapBuffer) {
                    void* ptr = ops.glMapBufferRange(target, 0, static_cast<ptrdiff_t>(size), kMapWriteInvalidateUnsync);
                    if (ptr != nullptr) {
                        std::memcpy(ptr, data, size);
                        if (ops.glUnmapBuffer(target) != 0) {
                            uploaded = true;
                            mapUploads_.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
                if (!uploaded) {
                    // Mode 4: explicit driver-side orphan at bucket capacity before upload
                    if (size == cap) {
                        ops.glBufferData(target, static_cast<ptrdiff_t>(cap), data, glUsage);
                    } else {
                        ops.glBufferData(target, static_cast<ptrdiff_t>(cap), nullptr, glUsage);
                        ops.glBufferSubData(target, 0, static_cast<ptrdiff_t>(size), data);
                    }
                    subDataUploads_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            outId = hitId;
            return true;
        }

        // Pool miss: allocate new GL buffer at bucket capacity
        uint32_t newId = 0;
        ops.glGenBuffers(1, &newId);
        if (newId == 0) return false;

        bindAndNotify(newId);
        if (size == cap) {
            ops.glBufferData(target, static_cast<ptrdiff_t>(cap), data, glUsage);
        } else {
            ops.glBufferData(target, static_cast<ptrdiff_t>(cap), nullptr, glUsage);
            if (data != nullptr) {
                ops.glBufferSubData(target, 0, static_cast<ptrdiff_t>(size), data);
            }
        }

        AcquireSRWLockExclusive(&lock_);
        activeMap_[newId] = ActiveEntry{target, glUsage, cap};
        ++misses_;
        ReleaseSRWLockExclusive(&lock_);

        outId = newId;
        return true;
    }

    bool TryRecycle(uint32_t id) {
        if (id == 0) return false;
        AcquireSRWLockExclusive(&lock_);
        auto it = activeMap_.find(id);
        if (it == activeMap_.end()) {
            ReleaseSRWLockExclusive(&lock_);
            return false;
        }
        const ActiveEntry entry = it->second;
        activeMap_.erase(it);

        if (pooledBytes_ + entry.cap <= maxPooledBytes_ && pooledCount_ < maxPooledCount_) {
            retiredRing_.push_back(RetiredEntry{id, entry.target, entry.glUsage, entry.cap, curFrame_});
            pooledBytes_ += entry.cap;
            ++pooledCount_;
            ++recycled_;
            ReleaseSRWLockExclusive(&lock_);
            return true;
        }
        ReleaseSRWLockExclusive(&lock_);
        return false; // Over cap: let caller call glDeleteBuffers normally
    }

    void OnEndFrame() {
        GlBufferOps::DeleteBuffers_t delFn = nullptr;
        GlBufferOps::CacheInvalidate_t invFn = nullptr;
        void* cbCtx = nullptr;
        std::vector<uint32_t> toDelete;

        AcquireSRWLockExclusive(&lock_);
        ++curFrame_;
        while (!retiredRing_.empty()) {
            const RetiredEntry& front = retiredRing_.front();
            if (curFrame_ - front.retiredFrame < kMinRetireAgeFrames) {
                break;
            }
            const BucketKey key{front.target, front.glUsage, front.cap};
            ready_[key].push_back(ReadyEntry{front.id, curFrame_});
            retiredRing_.pop_front();
        }

        if ((curFrame_ % 60u) == 0u && !ready_.empty()) {
            delFn = ops_.glDeleteBuffers;
            invFn = ops_.onInvalidateBuffer;
            cbCtx = ops_.callbackCtx;
            for (auto mapIt = ready_.begin(); mapIt != ready_.end();) {
                auto& vec = mapIt->second;
                size_t writeIdx = 0;
                for (size_t i = 0; i < vec.size(); ++i) {
                    if (curFrame_ - vec[i].readySinceFrame > idleEvictFrames_) {
                        toDelete.push_back(vec[i].id);
                        pooledBytes_ -= std::min(pooledBytes_, mapIt->first.cap);
                        if (pooledCount_ > 0) --pooledCount_;
                        ++evicted_;
                    } else {
                        if (writeIdx != i) vec[writeIdx] = vec[i];
                        ++writeIdx;
                    }
                }
                vec.resize(writeIdx);
                if (vec.empty()) {
                    mapIt = ready_.erase(mapIt);
                } else {
                    ++mapIt;
                }
            }
        }
        ReleaseSRWLockExclusive(&lock_);

        if (!toDelete.empty()) {
            if (invFn) {
                for (uint32_t id : toDelete) invFn(id, cbCtx);
            }
            if (delFn) {
                delFn(static_cast<int32_t>(toDelete.size()), toDelete.data());
            }
        }
    }

    void ResetAndRelease(bool deleteGlObjects) {
        GlBufferOps::DeleteBuffers_t delFn = nullptr;
        GlBufferOps::CacheInvalidate_t invFn = nullptr;
        void* cbCtx = nullptr;
        std::vector<uint32_t> toDelete;

        AcquireSRWLockExclusive(&lock_);
        delFn = ops_.glDeleteBuffers;
        invFn = ops_.onInvalidateBuffer;
        cbCtx = ops_.callbackCtx;
        toDelete.reserve(retiredRing_.size() + ready_.size() * 2u);
        for (const auto& r : retiredRing_) {
            toDelete.push_back(r.id);
        }
        retiredRing_.clear();
        for (const auto& kv : ready_) {
            for (const auto& re : kv.second) {
                toDelete.push_back(re.id);
            }
        }
        ready_.clear();
        activeMap_.clear();
        pooledBytes_ = 0;
        pooledCount_ = 0;
        ReleaseSRWLockExclusive(&lock_);

        if (invFn && !toDelete.empty()) {
            for (uint32_t id : toDelete) invFn(id, cbCtx);
        }
        if (deleteGlObjects && delFn && !toDelete.empty()) {
            delFn(static_cast<int32_t>(toDelete.size()), toDelete.data());
        }
    }

    Stats GetStats() noexcept {
        Stats st{};
        AcquireSRWLockShared(&lock_);
        st.hits        = hits_;
        st.misses      = misses_;
        st.recycled    = recycled_;
        st.evicted     = evicted_;
        st.activeCount = activeMap_.size();
        st.pooledCount = pooledCount_;
        st.pooledBytes = pooledBytes_;
        ReleaseSRWLockShared(&lock_);
        st.mapUploads     = mapUploads_.load(std::memory_order_relaxed);
        st.subDataUploads = subDataUploads_.load(std::memory_order_relaxed);
        return st;
    }

    bool HasEntries() noexcept {
        AcquireSRWLockShared(&lock_);
        const bool any = (pooledCount_ > 0) || !activeMap_.empty();
        ReleaseSRWLockShared(&lock_);
        return any;
    }

private:
    struct BucketKey {
        uint32_t target;
        uint32_t glUsage;
        size_t   cap;

        bool operator==(const BucketKey& o) const noexcept {
            return target == o.target && glUsage == o.glUsage && cap == o.cap;
        }
    };

    struct BucketKeyHash {
        size_t operator()(const BucketKey& k) const noexcept {
            uint64_t h = static_cast<uint64_t>(k.target) * 0x9E3779B97F4A7C15ULL;
            h ^= static_cast<uint64_t>(k.glUsage) * 0xBF58476D1CE4E5B9ULL;
            h ^= static_cast<uint64_t>(k.cap) * 0x94D049BB133111EBULL;
            return static_cast<size_t>(h ^ (h >> 32));
        }
    };

    struct ActiveEntry {
        uint32_t target;
        uint32_t glUsage;
        size_t   cap;
    };

    struct RetiredEntry {
        uint32_t id;
        uint32_t target;
        uint32_t glUsage;
        size_t   cap;
        uint64_t retiredFrame;
    };

    struct ReadyEntry {
        uint32_t id;
        uint64_t readySinceFrame;
    };

    SRWLOCK                                                                  lock_{};
    GlBufferOps                                                              ops_{};
    uint64_t                                                                 curFrame_    = 0;
    size_t                                                                   maxPooledBytes_  = kMaxPooledBytes;
    size_t                                                                   maxPooledCount_  = kMaxPooledCount;
    uint64_t                                                                 idleEvictFrames_ = kIdleEvictFrames;
    std::unordered_map<uint32_t, ActiveEntry>                                activeMap_;
    std::deque<RetiredEntry>                                                 retiredRing_;
    std::unordered_map<BucketKey, std::vector<ReadyEntry>, BucketKeyHash>    ready_;
    size_t                                                                   pooledBytes_ = 0;
    size_t                                                                   pooledCount_ = 0;
    int64_t                                                                  hits_        = 0;
    int64_t                                                                  misses_      = 0;
    int64_t                                                                  recycled_    = 0;
    int64_t                                                                  evicted_     = 0;
    std::atomic<int64_t>                                                     mapUploads_{0};
    std::atomic<int64_t>                                                     subDataUploads_{0};
};

} // namespace hs_vb_pool
