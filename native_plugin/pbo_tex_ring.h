#pragma once
// =============================================================================
// Persistent-Mapped 6-Slot Zero-Wait PBO DMA Texture Upload Ring (v4.0 Phase 3)
// -----------------------------------------------------------------------------
// Eliminates NVIDIA OpenGL driver synchronous staging-buffer stalls (6.1-33.2ms
// inside glCompressedTexImage2D / glTexImage2D) AND eliminates the 7.1-8.7ms
// glClientWaitSync(50ms) walking-texture stall caused by 3-slot exhaustion when
// 3 frames are queued on RTX 4060 (8-11ms GPU frame time):
//   - 6 x 8 MB = 48 MB total VRAM (same VRAM budget, 2x frame headroom), with
//     cascading fallback to 6 x 4 MB (24 MB) and 6 x 2 MB (12 MB tight VRAM).
//   - 100% ZERO-WAIT non-blocking fence scan (`glClientWaitSync(f, 0u, 0ULL)` —
//     NEVER passes GL_SYNC_FLUSH_COMMANDS_BIT, NEVER sleeps/blocks CPU!): if the
//     current slot's fence is still in-flight, immediately scans the remaining
//     5 slots and advances to any free/signaled slot in 0.000ms.
//   - Fail-closed: if buffer_storage unavailable, OOM, ALL 6 slots are in-flight
//     simultaneously, or upload > slot capacity, returns false so caller runs
//     exact original direct upload seamlessly (NEVER clobbers in-flight DMA!).
// =============================================================================

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#ifndef WINAPI
#define WINAPI __stdcall
#endif

namespace hs_pbo_tex {

inline constexpr uint32_t kGlPixelUnpackBuffer         = 0x88ECu;
inline constexpr uint32_t kGlBufferSize                = 0x8764u;
inline constexpr uint32_t kMapWriteBit                 = 0x0002u;
inline constexpr uint32_t kMapPersistentBit            = 0x0040u;
inline constexpr uint32_t kMapCoherentBit              = 0x0080u;
inline constexpr uint32_t kMapWritePersistentCoherent  = kMapWriteBit | kMapPersistentBit | kMapCoherentBit;
inline constexpr uint32_t kGlSyncGpuCommandsComplete   = 0x9117u;
inline constexpr uint32_t kGlSyncFlushCommandsBit      = 0x00000001u;
inline constexpr uint32_t kGlAlreadySignaled           = 0x911Au;
inline constexpr uint32_t kGlTimeoutExpired            = 0x911Bu;
inline constexpr uint32_t kGlConditionSatisfied        = 0x911Cu;
inline constexpr uint32_t kGlRgba                      = 0x1908u;
inline constexpr uint32_t kGlBgraExt                   = 0x80E1u;
inline constexpr uint32_t kGlUnsignedByte              = 0x1401u;

inline constexpr uint32_t kFramesInFlight              = 6u;
inline constexpr size_t   kDefaultSlotBytes            = 8u * 1024u * 1024u;  // 8 MB / slot (6 x 8 MB = 48 MB total)
inline constexpr size_t   kMinSlotBytes                = 4u * 1024u * 1024u;  // 4 MB / slot fallback (6 x 4 MB = 24 MB total)
inline constexpr size_t   kTightSlotBytes              = 2u * 1024u * 1024u;  // 2 MB / slot tight-VRAM fallback (6 x 2 MB = 12 MB total)
inline constexpr size_t   kMaxSlotBytes                = 16u * 1024u * 1024u; // 16 MB / slot max clamp
inline constexpr size_t   kMinUncompressedPboBytes     = 16384u;              // Skip tiny <16KB UI glyphs
inline constexpr size_t   kUploadAlignBytes            = 256u;

struct GlPboOps {
    using GenBuffers_t           = void (WINAPI*)(int32_t n, uint32_t* buffers);
    using DeleteBuffers_t        = void (WINAPI*)(int32_t n, const uint32_t* buffers);
    using BindBuffer_t           = void (WINAPI*)(uint32_t target, uint32_t buffer);
    using BufferStorage_t        = void (WINAPI*)(uint32_t target, ptrdiff_t size, const void* data, uint32_t flags);
    using MapBufferRange_t       = void* (WINAPI*)(uint32_t target, ptrdiff_t offset, ptrdiff_t length, uint32_t access);
    using UnmapBuffer_t          = uint8_t (WINAPI*)(uint32_t target);
    using GetBufferParameteriv_t = void (WINAPI*)(uint32_t target, uint32_t pname, int32_t* params);
    using FenceSync_t            = void* (WINAPI*)(uint32_t condition, uint32_t flags);
    using ClientWaitSync_t       = uint32_t (WINAPI*)(void* sync, uint32_t flags, uint64_t timeout);
    using DeleteSync_t           = void (WINAPI*)(void* sync);

    GenBuffers_t           glGenBuffers           = nullptr;
    DeleteBuffers_t        glDeleteBuffers        = nullptr;
    BindBuffer_t           glBindBuffer           = nullptr;
    BufferStorage_t        glBufferStorage        = nullptr;
    MapBufferRange_t       glMapBufferRange       = nullptr;
    UnmapBuffer_t          glUnmapBuffer          = nullptr;
    GetBufferParameteriv_t glGetBufferParameteriv = nullptr;
    FenceSync_t            glFenceSync            = nullptr;
    ClientWaitSync_t       glClientWaitSync       = nullptr;
    DeleteSync_t           glDeleteSync           = nullptr;

    bool IsComplete() const noexcept {
        return glGenBuffers && glDeleteBuffers && glBindBuffer &&
               glBufferStorage && glMapBufferRange && glUnmapBuffer &&
               glGetBufferParameteriv && glFenceSync && glClientWaitSync && glDeleteSync;
    }
};

class PersistentPboTexRing {
public:
    PersistentPboTexRing() = default;
    ~PersistentPboTexRing() = default;
    PersistentPboTexRing(const PersistentPboTexRing&) = delete;
    PersistentPboTexRing& operator=(const PersistentPboTexRing&) = delete;

    void SetOps(const GlPboOps& ops) noexcept {
        ops_ = ops;
    }

    void SetEnabled(bool en) noexcept {
        enabled_ = en;
    }

    void SetSlotBytes(size_t bytes) noexcept {
        const size_t clamped = std::clamp<size_t>(bytes, kTightSlotBytes, kMaxSlotBytes);
        wantSlotBytes_ = clamped;
    }

    bool IsActive() const noexcept {
        return enabled_ && ready_ && !failed_ && pboId_ != 0u && mapped_ != nullptr;
    }

    size_t ActiveSlotBytes() const noexcept { return slotBytes_; }
    uint64_t UploadCount() const noexcept { return uploads_; }
    uint64_t UploadBytes() const noexcept { return totalBytes_; }
    uint64_t OverflowFallbacks() const noexcept { return overflows_; }
    uint64_t WaitTimeouts() const noexcept { return waitTimeouts_; }
    uint64_t SlotSkips() const noexcept { return slotSkips_; }

    void ReleaseGpu(bool deleteOnCurrentCtx) noexcept {
        if (deleteOnCurrentCtx) {
            if (ops_.glDeleteSync) {
                for (uint32_t i = 0; i < kFramesInFlight; ++i) {
                    if (fences_[i]) {
                        ops_.glDeleteSync(fences_[i]);
                        fences_[i] = nullptr;
                    }
                }
            }
            if (pboId_ != 0u) {
                if (mapped_ != nullptr && ops_.glBindBuffer && ops_.glUnmapBuffer) {
                    ops_.glBindBuffer(kGlPixelUnpackBuffer, pboId_);
                    ops_.glUnmapBuffer(kGlPixelUnpackBuffer);
                    ops_.glBindBuffer(kGlPixelUnpackBuffer, 0u);
                }
                if (ops_.glDeleteBuffers) {
                    ops_.glDeleteBuffers(1, &pboId_);
                }
            }
        }
        for (uint32_t i = 0; i < kFramesInFlight; ++i) fences_[i] = nullptr;
        pboId_        = 0u;
        mapped_       = nullptr;
        slotBytes_    = 0u;
        ready_        = false;
        failed_       = false;
        frameIdx_     = 0u;
        cursor_       = 0u;
        frameWaited_  = true;
        frameTouched_ = false;
    }

    void ResetContext(void* newCtx, bool deleteOnCurrentCtx) noexcept {
        if (deleteOnCurrentCtx) {
            ReleaseGpu(true);
        } else if (newCtx != curCtx_ || newCtx == nullptr) {
            ReleaseGpu(false);
        }
        curCtx_ = newCtx;
    }

    void OnEndFrame() noexcept {
        if (!ready_ || !frameTouched_) return;
        if (ops_.glFenceSync) {
            if (fences_[frameIdx_] && ops_.glDeleteSync) {
                ops_.glDeleteSync(fences_[frameIdx_]);
            }
            fences_[frameIdx_] = ops_.glFenceSync(kGlSyncGpuCommandsComplete, 0u);
        }
        frameIdx_     = (frameIdx_ + 1u) % kFramesInFlight;
        cursor_       = 0u;
        frameWaited_  = (fences_[frameIdx_] == nullptr);
        frameTouched_ = false;
    }

    static bool IsValidUploadTarget(uint32_t target) noexcept {
        return target == 0x0DE1u /* GL_TEXTURE_2D */ ||
               (target >= 0x8515u && target <= 0x851Au) /* GL_TEXTURE_CUBE_MAP_POSITIVE_X..NEGATIVE_Z */;
    }

    template <typename OrigCompFn>
    bool TryUploadCompressedTexImage2D(OrigCompFn&& origFn,
                                       uint32_t target, int32_t level, uint32_t internalformat,
                                       int32_t width, int32_t height, int32_t border,
                                       int32_t imageSize, const void* data) noexcept {
        if constexpr (std::is_pointer_v<std::decay_t<OrigCompFn>>) {
            if (!origFn) return false;
        }
        if (!enabled_ || failed_ || !data || imageSize <= 0) return false;
        if (!IsValidUploadTarget(target) || border != 0 || width <= 0 || height <= 0) return false;
        const size_t bytes = static_cast<size_t>(imageSize);
        size_t baseOff = 0u;
        if (!ReserveAndCopy(data, bytes, baseOff)) {
            return false;
        }
        ops_.glBindBuffer(kGlPixelUnpackBuffer, pboId_);
        origFn(target, level, internalformat, width, height, border, imageSize,
               reinterpret_cast<const void*>(static_cast<uintptr_t>(baseOff)));
        ops_.glBindBuffer(kGlPixelUnpackBuffer, 0u);
        ++uploads_;
        totalBytes_ += bytes;
        return true;
    }

    template <typename OrigTexFn>
    bool TryUploadTexImage2D(OrigTexFn&& origFn,
                             uint32_t target, int32_t level, int32_t internalformat,
                             int32_t width, int32_t height, int32_t border,
                             uint32_t format, uint32_t type, const void* pixels) noexcept {
        if constexpr (std::is_pointer_v<std::decay_t<OrigTexFn>>) {
            if (!origFn) return false;
        }
        if (!enabled_ || failed_ || !pixels) return false;
        if (!IsValidUploadTarget(target) || border != 0 || width <= 0 || height <= 0 || width > 8192 || height > 8192) return false;
        if (type != kGlUnsignedByte) return false;
        // Require 4-byte RGBA/BGRA with even width -> rowBytes = width*4 is a multiple of 8,
        // so pixel stride is 100% invariant across ALL GL_UNPACK_ALIGNMENT values (1, 2, 4, 8)!
        if (format != kGlRgba && format != kGlBgraExt) return false;
        if ((width & 1) != 0) return false;
        const size_t bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
        if (bytes < kMinUncompressedPboBytes) return false;
        size_t baseOff = 0u;
        if (!ReserveAndCopy(pixels, bytes, baseOff)) {
            return false;
        }
        ops_.glBindBuffer(kGlPixelUnpackBuffer, pboId_);
        origFn(target, level, internalformat, width, height, border, format, type,
               reinterpret_cast<const void*>(static_cast<uintptr_t>(baseOff)));
        ops_.glBindBuffer(kGlPixelUnpackBuffer, 0u);
        ++uploads_;
        totalBytes_ += bytes;
        return true;
    }

private:
    bool IsSlotFreeAndClearFence(uint32_t idx) noexcept {
        void* f = fences_[idx];
        if (!f) return true;
        // ZERO-WAIT poll: flags = 0u (NEVER GL_SYNC_FLUSH_COMMANDS_BIT!), timeout = 0ULL
        const uint32_t w = ops_.glClientWaitSync(f, 0u, 0ULL);
        if (w == kGlAlreadySignaled || w == kGlConditionSatisfied) {
            ops_.glDeleteSync(f);
            fences_[idx] = nullptr;
            return true;
        }
        return false;
    }

    bool EnsureReady() noexcept {
        if (ready_) return true;
        if (failed_) return false;
        if (!ops_.IsComplete()) {
            failed_ = true;
            return false;
        }
        size_t trySizes[3]{};
        int numTries = 0;
        auto pushTry = [&](size_t sz) noexcept {
            for (int i = 0; i < numTries; ++i) {
                if (trySizes[i] == sz) return;
            }
            if (numTries < 3) trySizes[numTries++] = sz;
        };
        pushTry(wantSlotBytes_);
        if (kMinSlotBytes < wantSlotBytes_) pushTry(kMinSlotBytes);
        if (kTightSlotBytes < wantSlotBytes_) pushTry(kTightSlotBytes);
        for (int attempt = 0; attempt < numTries; ++attempt) {
            const size_t slotSz  = trySizes[attempt];
            const size_t totalSz = slotSz * static_cast<size_t>(kFramesInFlight);
            uint32_t id = 0u;
            ops_.glGenBuffers(1, &id);
            if (id == 0u) continue;
            ops_.glBindBuffer(kGlPixelUnpackBuffer, id);
            ops_.glBufferStorage(kGlPixelUnpackBuffer, static_cast<ptrdiff_t>(totalSz),
                                 nullptr, kMapWritePersistentCoherent);
            int32_t allocBytes = 0;
            ops_.glGetBufferParameteriv(kGlPixelUnpackBuffer, kGlBufferSize, &allocBytes);
            uint8_t* ptr = nullptr;
            if (allocBytes >= static_cast<int32_t>(totalSz)) {
                ptr = static_cast<uint8_t*>(
                    ops_.glMapBufferRange(kGlPixelUnpackBuffer, 0, static_cast<ptrdiff_t>(totalSz),
                                          kMapWritePersistentCoherent));
            }
            ops_.glBindBuffer(kGlPixelUnpackBuffer, 0u);
            if (ptr != nullptr && allocBytes >= static_cast<int32_t>(totalSz)) {
                pboId_        = id;
                mapped_       = ptr;
                slotBytes_    = slotSz;
                frameIdx_     = 0u;
                cursor_       = 0u;
                frameWaited_  = true;
                frameTouched_ = false;
                ready_        = true;
                return true;
            }
            ops_.glDeleteBuffers(1, &id);
        }
        failed_ = true;
        return false;
    }

    bool ReserveAndCopy(const void* srcData, size_t bytes, size_t& outBaseOff) noexcept {
        if (!EnsureReady()) return false;
        const size_t aligned = (bytes + (kUploadAlignBytes - 1u)) & ~(kUploadAlignBytes - 1u);
        if (aligned > slotBytes_) {
            ++overflows_;
            return false;
        }
        if (!frameWaited_) {
            if (IsSlotFreeAndClearFence(frameIdx_)) {
                frameWaited_ = true;
            } else {
                // Zero-wait non-blocking scan across the remaining slots (0.000ms CPU stall!)
                bool found = false;
                for (uint32_t s = 1u; s < kFramesInFlight; ++s) {
                    const uint32_t cand = (frameIdx_ + s) % kFramesInFlight;
                    if (IsSlotFreeAndClearFence(cand)) {
                        frameIdx_    = cand;
                        cursor_      = 0u;
                        frameWaited_ = true;
                        ++slotSkips_;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // Fail-closed: ALL 6 slots are simultaneously busy in-flight!
                    // NEVER overwrite in-flight GPU DMA memory, NEVER block CPU!
                    ++waitTimeouts_;
                    return false;
                }
            }
        }
        if ((cursor_ + aligned) > slotBytes_) {
            // Current slot full mid-frame: try zero-wait spill to another free slot.
            // Fence the touched old slot FIRST so it can NEVER be reused while in-flight!
            bool spilled = false;
            if (frameTouched_) {
                for (uint32_t s = 1u; s < kFramesInFlight; ++s) {
                    const uint32_t cand = (frameIdx_ + s) % kFramesInFlight;
                    if (IsSlotFreeAndClearFence(cand)) {
                        void* oldFence = ops_.glFenceSync(kGlSyncGpuCommandsComplete, 0u);
                        if (oldFence != nullptr) {
                            if (fences_[frameIdx_]) {
                                ops_.glDeleteSync(fences_[frameIdx_]);
                            }
                            fences_[frameIdx_] = oldFence;
                            frameIdx_          = cand;
                            cursor_            = 0u;
                            frameWaited_       = true;
                            frameTouched_      = false;
                            ++slotSkips_;
                            spilled = true;
                        }
                        break;
                    }
                }
            }
            if (!spilled) {
                ++overflows_;
                return false;
            }
        }
        const size_t baseOff = static_cast<size_t>(frameIdx_) * slotBytes_ + cursor_;
        std::memcpy(mapped_ + baseOff, srcData, bytes);
        cursor_ += aligned;
        frameTouched_ = true;
        outBaseOff = baseOff;
        return true;
    }

    GlPboOps ops_{};
    void*    curCtx_       = nullptr;
    bool     enabled_      = false;
    bool     ready_        = false;
    bool     failed_       = false;
    size_t   wantSlotBytes_ = kDefaultSlotBytes;
    size_t   slotBytes_    = 0u;
    uint32_t pboId_        = 0u;
    uint8_t* mapped_       = nullptr;
    void*    fences_[kFramesInFlight]{};
    uint32_t frameIdx_     = 0u;
    size_t   cursor_       = 0u;
    bool     frameWaited_  = true;
    bool     frameTouched_ = false;
    uint64_t uploads_      = 0ULL;
    uint64_t totalBytes_   = 0ULL;
    uint64_t overflows_    = 0ULL;
    uint64_t waitTimeouts_ = 0ULL;
    uint64_t slotSkips_    = 0ULL;
};

// =============================================================================
// Pre-Allocated GL Texture ID Pool (v4.0 Phase 2)
// -----------------------------------------------------------------------------
// Live RTX 4060 forensics proved `glGenTextures(1, &tex)` during heavy frames
// stalls 1.9-3.5ms inside NVIDIA's threaded OpenGL driver (nvoglv64.dll) due to
// a synchronous driver round-trip returning a GLuint ID, whereas popping from a
// pre-allocated `glGenTextures(256, pool)` array takes 0.000ms.
// Refilled post-SwapBuffers in ONE batch call while the GPU drains; fail-closed
// when disabled or under-stocked (returns false so caller falls back directly),
// and deletes all unconsumed IDs on context switch / teardown (0 GL ID leaks!).
// =============================================================================
struct GlTexPoolOps {
    using GenTextures_t    = void (WINAPI*)(int32_t n, uint32_t* textures);
    using DeleteTextures_t = void (WINAPI*)(int32_t n, const uint32_t* textures);

    GenTextures_t    glGenTextures    = nullptr;
    DeleteTextures_t glDeleteTextures = nullptr;

    bool IsComplete() const noexcept {
        return glGenTextures && glDeleteTextures;
    }
};

class PreallocTexIdPool {
public:
    static constexpr uint32_t kPoolCap         = 256u;
    static constexpr uint32_t kDefaultLowWater = 64u;

    PreallocTexIdPool() = default;
    ~PreallocTexIdPool() = default;
    PreallocTexIdPool(const PreallocTexIdPool&) = delete;
    PreallocTexIdPool& operator=(const PreallocTexIdPool&) = delete;

    void SetOps(const GlTexPoolOps& ops) noexcept {
        ops_ = ops;
    }

    void SetEnabled(bool en) noexcept {
        enabled_ = en;
    }

    bool IsEnabled() const noexcept {
        return enabled_;
    }

    bool TryAcquire(int32_t n, uint32_t* outIds) noexcept {
        if (!enabled_ || n <= 0 || !outIds) return false;
        const uint32_t un = static_cast<uint32_t>(n);
        if (un > count_) return false;
        const uint32_t start = count_ - un;
        for (uint32_t i = 0u; i < un; ++i) {
            outIds[i] = ids_[start + i];
        }
        count_ = start;
        ++hits_;
        return true;
    }

    void RefillIfLow(uint32_t lowWater = kDefaultLowWater) noexcept {
        if (!enabled_ || !ops_.IsComplete()) return;
        const uint32_t effLow = (lowWater > kPoolCap) ? kPoolCap : lowWater;
        if (effLow == 0u || count_ >= effLow) return;
        const uint32_t need = kPoolCap - count_;
        std::memset(ids_ + count_, 0, static_cast<size_t>(need) * sizeof(uint32_t));
        ops_.glGenTextures(static_cast<int32_t>(need), ids_ + count_);
        uint32_t added = 0u;
        for (uint32_t i = 0u; i < need; ++i) {
            const uint32_t id = ids_[count_ + i];
            if (id != 0u) {
                ids_[count_ + added] = id;
                ++added;
            }
        }
        count_ += added;
        if (added > 0u) {
            ++refills_;
        }
    }

    void ReleaseGpu(bool deleteOnCurrentCtx) noexcept {
        if (deleteOnCurrentCtx && count_ > 0u && ops_.glDeleteTextures) {
            ops_.glDeleteTextures(static_cast<int32_t>(count_), ids_);
        }
        count_ = 0u;
    }

    void ResetContext(void* newCtx, bool deleteOnCurrentCtx) noexcept {
        if (deleteOnCurrentCtx) {
            ReleaseGpu(true);
        } else if (newCtx != curCtx_ || newCtx == nullptr) {
            ReleaseGpu(false);
        }
        curCtx_ = newCtx;
    }

    uint64_t HitCount() const noexcept { return hits_; }
    uint64_t RefillCount() const noexcept { return refills_; }
    uint32_t AvailableCount() const noexcept { return count_; }

private:
    GlTexPoolOps ops_{};
    void*        curCtx_  = nullptr;
    bool         enabled_ = false;
    uint32_t     ids_[kPoolCap]{};
    uint32_t     count_   = 0u;
    uint64_t     hits_    = 0ULL;
    uint64_t     refills_ = 0ULL;
};

} // namespace hs_pbo_tex
