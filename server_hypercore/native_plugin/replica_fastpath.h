#pragma once
// ============================================================================
// DST-HyperCore Server — ReplicaManager3 / QueryReplicaList Fast-Path Engine
// ============================================================================
// 100% bit-for-bit equivalent, zero-desync replacement for Klei's
// cNetworkConnection::QueryReplicaList + zero-scan PreSerialize maintenance:
//   1. Hoists cNetworkManager::GetClientObjectByGuid RB-tree lookup ONCE per
//      connection instead of repeating it 2x per pending replica and 1x per
//      in-range replica (eliminates 3,000-28,000+ RB-tree lookups/player/tick).
//   2. Fast-paths NetworkProxy IsDestroyed / IsRelevantToClient /
//      QueryConstruction / QueryDestruction in 4-6 loads with 0 virtual calls
//      on 98%+ of world entities, falling back to Klei's exact
//      NetworkProxy::IsRelevantToClient (for classified/parented/inventory
//      entities) and replica vtable methods (for non-NetworkProxy subclasses).
//   3. Replaces O(K*N) per-item binary-search + shift removals and O(K*N)
//      sorted insertions in list0/list1/list2 AND O(K*M) destruction scans in
//      Connection_RM3::constructedReplicaList / serializedReplicaList with
//      single-pass O(N) compaction + O(A+B) sorted bulk merge — eliminating
//      50-60ms player-join / wormhole / cave-transition spikes.
//   4. Maintains Replica3::preSerialized (+0x178) on serializedReplicaList
//      entries only, eliminating the 17,000+ world userReplicaList full scan.
//
// Hermetic ABI: Zero STL containers, zero exceptions, zero non-trivial
// thread_local destructors (strictly compatible with Linux GLIBC <= 2.17 and
// zero libstdc++.so.6 DT_NEEDED).
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace hc_replica {

inline constexpr uint64_t kUnassignedGuid = 0xFFFFFFFFFFFFFFFFULL;

// Exact RakNet DataStructures::Map<Replica3*, Replica3*>::MapNode (16 bytes)
struct DsMapNode {
    void* key;
    void* val;
};
static_assert(sizeof(DsMapNode) == 16, "DsMapNode must be 16 bytes");

// Exact RakNet DataStructures::Map<Replica3*, Replica3*> (40 bytes / 0x28)
struct DsMap {
    DsMapNode* arr;              // +0x00
    uint32_t   size;             // +0x08
    uint32_t   alloc;            // +0x0C
    uint8_t    lastSearch[16];   // +0x10..+0x1F (lastSearchKey / lastSearchIndex)
    uint8_t    lastSearchValid;  // +0x20
    uint8_t    pad21[7];         // +0x21..+0x27
};
static_assert(sizeof(DsMap) == 40, "DsMap must be 40 bytes");

// Exact RakNet DataStructures::List<void*> (16 bytes)
struct DsListPtr {
    void**   arr;    // +0x00
    uint32_t size;   // +0x08
    uint32_t alloc;  // +0x0C
};
static_assert(sizeof(DsListPtr) == 16, "DsListPtr must be 16 bytes");

using HostNewFn_t         = void* (*)(size_t bytes);
using HostDeleteFn_t      = void  (*)(void* ptr);
using GetClientByGuidFn_t = void* (*)(void* netMgr, const void* guidPtr);
using IsRelevantFn_t      = bool  (*)(void* networkProxy, const void* guidPtr, uint64_t playerMask);

// Virtual method signatures on RakNet::Replica3*
using RepIsDestroyedFn_t       = bool    (*)(void* rep);
using RepQueryConstructionFn_t = int32_t (*)(void* rep, void* conn, void* rm3);
using RepQueryDestructionFn_t  = int32_t (*)(void* rep, void* conn, void* rm3);

struct ReplicaAbiConfig {
    // Host pointers (dynamically extracted from verified AOBs)
    void**              ppNetMgr          = nullptr; // &g_netMgr
    GetClientByGuidFn_t fnGetClientByGuid = nullptr;
    IsRelevantFn_t      fnIsRelevant      = nullptr;
    HostNewFn_t         fnHostNew         = nullptr;
    HostDeleteFn_t      fnHostDelete      = nullptr;
    const void*         networkProxyVtbl  = nullptr; // NetworkProxy secondary Replica3 vtable
    const void*         invItemCompVtbl   = nullptr; // Unique component vtable for type 0x3f8e281e

    // ABI-specific struct offsets (verified on Windows PE & Linux ELF Build 762172)
    uint32_t offNetMgrIsClient     = 0x308; // Win: 0x308, Linux: 0x2d0
    uint32_t offNetMgrReplicaMgr   = 0x288; // Win: 0x288, Linux: 0x250
    uint32_t offClientPlayerMask   = 0x1b0; // Both: 0x1b0
    uint32_t offClientReady        = 0x341; // Win: 0x341, Linux: 0x329
    uint32_t offClientBlocked      = 0x342; // Win: 0x342, Linux: 0x32a
    uint32_t offEntDestroyed       = 0x20c; // Win: 0x20c, Linux: 0x1f4
    uint32_t offEntHidden1c4       = 0x1c4; // Win: 0x1c4, Linux: 0x1ac
    uint32_t offEntHidden1c8       = 0x1c8; // Win: 0x1c8, Linux: 0x1b0
    uint32_t offEntParent1         = 0x0a0; // Win: 0x0a0, Linux: 0x088
    uint32_t offEntParent2         = 0x0c8; // Win: 0x0c8, Linux: 0x0a8
    uint32_t offEntCompBegin       = 0x100; // Win: 0x100, Linux: 0x0d0
    uint32_t offEntCompEnd         = 0x108; // Win: 0x108, Linux: 0x0d8

    // Replica3 vtable byte offsets (Itanium ABI has +8 due to complete+deleting dtors)
    uint32_t vtblOffQueryConstruct = 0x040; // Win: 0x040, Linux: 0x048
    uint32_t vtblOffQueryDestruct  = 0x048; // Win: 0x048, Linux: 0x050
    uint32_t vtblOffIsDestroyed    = 0x138; // Win: 0x138, Linux: 0x140

    // Shared RakNet offsets (identical on Win & Linux)
    uint32_t offRm3List0           = 0x08a8;
    uint32_t offRm3List1           = 0x12a8;
    uint32_t offRm3List2           = 0x1ca8;
    uint32_t offRm3WorldListPtr    = 0x0880;
    uint32_t offConnGuid           = 0x0020;
    uint32_t offConnConstructedLst = 0x0030;
    uint32_t offConnSerializedLst  = 0x0050;
    uint32_t offRepPreSerialized   = 0x0178;
    uint32_t offRepCullMaskCopy    = 0x0190;

    // Module 10B (v1.6.0): Zero-Desync Tiered Replica Construction Pacing
    // Default 0u = disabled/unlimited (exact v1.5.0 behavior; preserves tests #19/#20).
    uint32_t maxConstructPerTick   = 0u;

    // Fail-closed verified proximity offsets (active ONLY when proximityValid == true):
    //   client->playerProxy (+0x1b8 on Win & Linux) -> playerProxy->ent (+0x18) -> pos(x,z)
    //   Entity world pos: Win (+0x200, +0x208), Linux (+0x1e8, +0x1f0)
    bool     proximityValid        = false;
    uint32_t offClientPlayerProxy  = 0x1b8; // Both Win & Linux: 0x1b8
    uint32_t offEntPosX            = 0x200; // Win: 0x200, Linux: 0x1e8
    uint32_t offEntPosZ            = 0x208; // Win: 0x208, Linux: 0x1f0
    float    nearRadiusWu          = 14.0f; // <= 14 wu (3.5 tiles): Tier-0 immediate bypass
    float    midRadiusWu           = 28.0f; // 14..28 wu: Tier-1 Bucket 1 (nearest)
    float    farRadiusWu           = 42.0f; // 28..42 wu: Tier-1 Bucket 2; >42 wu: Bucket 3

    bool IsValid() const noexcept {
        return ppNetMgr && fnGetClientByGuid && fnIsRelevant &&
               fnHostNew && fnHostDelete && networkProxyVtbl && invItemCompVtbl;
    }
};

inline uint32_t ClampReplicaConstructBudget(int raw) noexcept {
    if (raw <= 0) return 0u;
    if (raw < 16) return 16u;
    if (raw > 1024) return 1024u;
    return static_cast<uint32_t>(raw);
}

inline bool IsFiniteFloat(float v) noexcept {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return (bits & 0x7F800000u) != 0x7F800000u;
}

struct ReplicaFastStats {
    std::atomic<uint64_t> qrl_calls{0};
    std::atomic<uint64_t> qrl_us_sum{0};
    std::atomic<uint64_t> qrl_us_max{0};
    std::atomic<uint64_t> list0_scanned{0};
    std::atomic<uint64_t> list1_scanned{0};
    std::atomic<uint64_t> fast_proxy_hits{0};
    std::atomic<uint64_t> slow_rel_fallbacks{0};
    std::atomic<uint64_t> non_proxy_fallbacks{0};
    std::atomic<uint64_t> constructed_total{0};
    std::atomic<uint64_t> destroyed_total{0};
    std::atomic<uint64_t> bulk_conn_compacts{0};
    std::atomic<uint64_t> preser_cleared{0};
    std::atomic<uint64_t> preser_zero_conn_skips{0};
    std::atomic<uint64_t> paced_deferred_total{0};
    std::atomic<uint64_t> paced_burst_ticks{0};
    std::atomic<uint64_t> paced_parent_promotes{0};
    std::atomic<uint64_t> paced_near_bypasses{0};
};

// Trivial-destructor POD growable scratch buffer (zero libstdc++, zero __cxa_thread_atexit).
template <typename T>
struct ScratchBuf {
    T*     data = nullptr;
    size_t len  = 0;
    size_t cap  = 0;

    void Clear() noexcept { len = 0; }

    bool Reserve(size_t need) noexcept {
        if (need <= cap) return true;
        size_t newCap = cap ? cap : 64u;
        while (newCap < need) {
            if (newCap > (size_t(1) << 28)) return false;
            newCap *= 2u;
        }
        T* mem = static_cast<T*>(std::realloc(data, newCap * sizeof(T)));
        if (!mem) return false;
        data = mem;
        cap  = newCap;
        return true;
    }

    bool Push(const T& val) noexcept {
        if (len >= cap && !Reserve(len + 1u)) return false;
        data[len++] = val;
        return true;
    }

    void Free() noexcept {
        if (data) {
            std::free(data);
            data = nullptr;
        }
        len = 0;
        cap = 0;
    }
};

struct Tier1Candidate {
    DsMapNode      node;
    const uint8_t* ent;
    uint8_t        bucket;
    uint8_t        chosen;
};

// Reusable immortal scratch buffers (single sim thread, trivial destructor).
struct QueryScratch {
    ScratchBuf<void*>          addList1;
    ScratchBuf<void*>          addList2;
    ScratchBuf<void*>          addList0;
    ScratchBuf<DsMapNode>      mergeTmp;
    ScratchBuf<void*>          removedFromConstructed;
    ScratchBuf<void*>          sortTmp;
    ScratchBuf<void*>          neededParentEnts;
    ScratchBuf<Tier1Candidate> tier1Candidates;
    ScratchBuf<void*>          deferredList0;

    void FreeAll() noexcept {
        addList1.Free();
        addList2.Free();
        addList0.Free();
        mergeTmp.Free();
        removedFromConstructed.Free();
        sortTmp.Free();
        neededParentEnts.Free();
        tier1Candidates.Free();
        deferredList0.Free();
    }
};

inline void SortPtrsAsc(void** arr, size_t n) noexcept {
    if (!arr || n < 2u) return;
    // In-place bottom-up heapsort: guaranteed O(N log N) worst-case, 0 stack recursion, 0 STL.
    auto siftDown = [arr](size_t root, size_t end) noexcept {
        while (true) {
            size_t child = root * 2u + 1u;
            if (child >= end) break;
            if (child + 1u < end &&
                reinterpret_cast<uintptr_t>(arr[child]) < reinterpret_cast<uintptr_t>(arr[child + 1u])) {
                ++child;
            }
            if (reinterpret_cast<uintptr_t>(arr[root]) < reinterpret_cast<uintptr_t>(arr[child])) {
                void* tmp = arr[root];
                arr[root] = arr[child];
                arr[child] = tmp;
                root = child;
            } else {
                break;
            }
        }
    };
    for (size_t start = n / 2u; start > 0u; --start) {
        siftDown(start - 1u, n);
    }
    for (size_t end = n - 1u; end > 0u; --end) {
        void* tmp = arr[end];
        arr[end] = arr[0];
        arr[0] = tmp;
        siftDown(0u, end);
    }
}

inline void EnsureSortedPtrsAsc(void** arr, size_t n) noexcept {
    if (!arr || n < 2u) return;
    for (size_t i = 1u; i < n; ++i) {
        if (reinterpret_cast<uintptr_t>(arr[i]) < reinterpret_cast<uintptr_t>(arr[i - 1u])) {
            SortPtrsAsc(arr, n);
            return;
        }
    }
}

inline bool PtrBinarySearch(const void* const* sortedArr, size_t n, const void* key) noexcept {
    if (!sortedArr || n == 0u) return false;
    const uintptr_t k = reinterpret_cast<uintptr_t>(key);
    size_t lo = 0u, hi = n;
    while (lo < hi) {
        const size_t mid = lo + ((hi - lo) >> 1);
        const uintptr_t mk = reinterpret_cast<uintptr_t>(sortedArr[mid]);
        if (mk < k) lo = mid + 1u;
        else if (mk > k) hi = mid;
        else return true;
    }
    return false;
}

inline int FindFirstSetBit64(uint64_t mask) noexcept {
    if (mask == 0) return -1;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(mask);
#else
    for (int i = 0; i < 64; ++i) {
        if (mask & (1ULL << i)) return i;
    }
    return -1;
#endif
}

inline bool DsListPush(DsListPtr* list, void* item, const ReplicaAbiConfig& cfg) noexcept {
    if (!list) return false;
    if (list->size >= list->alloc) {
        uint32_t newCap = (list->alloc == 0) ? 16u : (list->alloc * 2u);
        if (newCap < list->alloc || newCap > (0x3FFFFFFFu / sizeof(void*))) {
            return false;
        }
        void** newArr = static_cast<void**>(cfg.fnHostNew(static_cast<size_t>(newCap) * sizeof(void*)));
        if (!newArr) return false;
        if (list->arr) {
            if (list->size > 0) {
                std::memcpy(newArr, list->arr, static_cast<size_t>(list->size) * sizeof(void*));
            }
            cfg.fnHostDelete(list->arr);
        }
        list->arr   = newArr;
        list->alloc = newCap;
    }
    list->arr[list->size++] = item;
    return true;
}

inline bool DsMapContains(const DsMap* m, void* key) noexcept {
    if (!m || m->size == 0 || !m->arr) return false;
    const uintptr_t k = reinterpret_cast<uintptr_t>(key);
    uint32_t lo = 0;
    uint32_t hi = m->size;
    const DsMapNode* arr = m->arr;
    while (lo < hi) {
        const uint32_t mid = lo + ((hi - lo) >> 1);
        const uintptr_t mk = reinterpret_cast<uintptr_t>(arr[mid].key);
        if (mk < k) {
            lo = mid + 1;
        } else if (mk > k) {
            hi = mid;
        } else {
            return true;
        }
    }
    return false;
}

// O(|dst| + |sortedKeys|) sorted bulk merge into RakNet DataStructures::Map<Replica3*, Replica3*>.
// Deduplicates exact keys (matching DS_Map::Set overwrite semantics) and guarantees strictly
// ascending pointer order with zero O(K*N) per-element shifts.
inline void DsMapBulkMergeSorted(DsMap* dst, const ScratchBuf<void*>& sortedKeys,
                                 ScratchBuf<DsMapNode>& scratch, const ReplicaAbiConfig& cfg) noexcept {
    if (!dst || sortedKeys.len == 0u) return;

    const uint32_t oldN = dst->size;
    const size_t   addK = sortedKeys.len;
    scratch.Clear();
    if (!scratch.Reserve(static_cast<size_t>(oldN) + addK)) return;

    const DsMapNode* a = dst->arr;
    uint32_t ia = 0;
    size_t   ib = 0;
    while (ia < oldN && ib < addK) {
        const uintptr_t ka = reinterpret_cast<uintptr_t>(a[ia].key);
        const uintptr_t kb = reinterpret_cast<uintptr_t>(sortedKeys.data[ib]);
        if (ka < kb) {
            scratch.data[scratch.len++] = a[ia++];
        } else if (kb < ka) {
            void* p = sortedKeys.data[ib++];
            if (scratch.len == 0u || scratch.data[scratch.len - 1u].key != p) {
                scratch.data[scratch.len++] = DsMapNode{p, p};
            }
        } else {
            void* p = sortedKeys.data[ib++];
            if (scratch.len == 0u || scratch.data[scratch.len - 1u].key != p) {
                scratch.data[scratch.len++] = DsMapNode{p, p};
            }
            ++ia;
        }
    }
    while (ia < oldN) {
        scratch.data[scratch.len++] = a[ia++];
    }
    while (ib < addK) {
        void* p = sortedKeys.data[ib++];
        if (scratch.len == 0u || scratch.data[scratch.len - 1u].key != p) {
            scratch.data[scratch.len++] = DsMapNode{p, p};
        }
    }

    const uint32_t newN = static_cast<uint32_t>(scratch.len);
    if (newN > dst->alloc) {
        uint32_t newCap = dst->alloc ? dst->alloc : 16u;
        while (newCap < newN) {
            if (newCap > (0x3FFFFFFFu / sizeof(DsMapNode))) {
                newCap = newN;
                break;
            }
            newCap *= 2u;
        }
        auto* newArr = static_cast<DsMapNode*>(cfg.fnHostNew(static_cast<size_t>(newCap) * sizeof(DsMapNode)));
        if (!newArr) return;
        if (newN > 0u) {
            std::memcpy(newArr, scratch.data, static_cast<size_t>(newN) * sizeof(DsMapNode));
        }
        if (dst->arr) {
            cfg.fnHostDelete(dst->arr);
        }
        dst->arr   = newArr;
        dst->alloc = newCap;
    } else if (newN > 0u && dst->arr) {
        std::memcpy(dst->arr, scratch.data, static_cast<size_t>(newN) * sizeof(DsMapNode));
    }
    dst->size            = newN;
    dst->lastSearchValid = 0;
}

// Single-pass O(M) compaction of Connection_RM3::constructedReplicaList (+0x30) and
// serializedReplicaList (+0x50) for bulk destruction (eliminates O(K*M) shifts in AutoConstructByQuery).
// Mirrors exact AutoConstructByQuery semantics: only removes from serializedReplicaList if the
// replica was actually found and removed from constructedReplicaList.
inline void CompactConnectionDestroyed(void* conn, const DsListPtr* destroyList,
                                       QueryScratch& sc, const ReplicaAbiConfig& cfg,
                                       ReplicaFastStats* stats) noexcept {
    if (!conn || !destroyList || destroyList->size < 8u || !destroyList->arr) return;

    auto* connBytes   = static_cast<uint8_t*>(conn);
    auto* constructed = reinterpret_cast<DsListPtr*>(connBytes + cfg.offConnConstructedLst);
    auto* serialized  = reinterpret_cast<DsListPtr*>(connBytes + cfg.offConnSerializedLst);
    if (!constructed->arr || constructed->size == 0u) return;

    void** dArr = destroyList->arr;
    const uint32_t dCnt = destroyList->size;
    bool sorted = true;
    for (uint32_t i = 1; i < dCnt; ++i) {
        if (reinterpret_cast<uintptr_t>(dArr[i]) < reinterpret_cast<uintptr_t>(dArr[i - 1])) {
            sorted = false;
            break;
        }
    }
    const void* const* searchArr = const_cast<const void* const*>(dArr);
    size_t searchCnt = dCnt;
    if (!sorted) {
        sc.sortTmp.Clear();
        if (!sc.sortTmp.Reserve(dCnt)) return;
        std::memcpy(sc.sortTmp.data, dArr, static_cast<size_t>(dCnt) * sizeof(void*));
        sc.sortTmp.len = dCnt;
        SortPtrsAsc(sc.sortTmp.data, sc.sortTmp.len);
        searchArr = const_cast<const void* const*>(sc.sortTmp.data);
        searchCnt = sc.sortTmp.len;
    }

    auto containsPtr = [](const void* const* arr, size_t n, const void* key) noexcept -> bool {
        const uintptr_t k = reinterpret_cast<uintptr_t>(key);
        size_t lo = 0, hi = n;
        while (lo < hi) {
            const size_t mid = lo + ((hi - lo) >> 1);
            const uintptr_t mk = reinterpret_cast<uintptr_t>(arr[mid]);
            if (mk < k) lo = mid + 1;
            else if (mk > k) hi = mid;
            else return true;
        }
        return false;
    };

    sc.removedFromConstructed.Clear();
    uint32_t w = 0;
    const uint32_t cN = constructed->size;
    void** cArr = constructed->arr;
    for (uint32_t i = 0; i < cN; ++i) {
        void* lsr = cArr[i];
        void* rep = lsr ? *reinterpret_cast<void**>(lsr) : nullptr;
        if (rep && containsPtr(searchArr, searchCnt, rep)) {
            if (!sc.removedFromConstructed.Push(rep)) {
                // OOM guard: keep entry intact so vanilla AutoConstructByQuery handles it
                if (w != i) cArr[w] = lsr;
                ++w;
            }
        } else {
            if (w != i) cArr[w] = lsr;
            ++w;
        }
    }
    if (w == cN) return;
    constructed->size = w;

    if (sc.removedFromConstructed.len > 0u && serialized->arr && serialized->size > 0u) {
        SortPtrsAsc(sc.removedFromConstructed.data, sc.removedFromConstructed.len);
        const void* const* rArr = const_cast<const void* const*>(sc.removedFromConstructed.data);
        const size_t rN = sc.removedFromConstructed.len;

        uint32_t sw = 0;
        const uint32_t sN = serialized->size;
        void** sArr = serialized->arr;
        for (uint32_t i = 0; i < sN; ++i) {
            void* lsr = sArr[i];
            void* rep = lsr ? *reinterpret_cast<void**>(lsr) : nullptr;
            if (rep && containsPtr(rArr, rN, rep)) {
                continue;
            }
            if (sw != i) sArr[sw] = lsr;
            ++sw;
        }
        serialized->size = sw;
    }

    if (stats) stats->bulk_conn_compacts.fetch_add(1, std::memory_order_relaxed);
}

inline bool HasInvItemComponentFast(const uint8_t* ent, const ReplicaAbiConfig& cfg) noexcept {
    void* const* beg = *reinterpret_cast<void* const* const*>(ent + cfg.offEntCompBegin);
    void* const* end = *reinterpret_cast<void* const* const*>(ent + cfg.offEntCompEnd);
    if (!beg || end <= beg) return false;
    const void* targetVtbl = cfg.invItemCompVtbl;
    for (void* const* p = beg; p < end; ++p) {
        void* comp = *p;
        if (comp && *reinterpret_cast<void* const*>(comp) == targetVtbl) {
            return true;
        }
    }
    return false;
}

// 100% exact fast-path equivalent of NetworkProxy::IsRelevantToClient(proxy, &guid, playerMask),
// additionally reporting whether the entity is structurally Tier-0 (classified / parented /
// inventoryitem) and its parent pointers (p1, p2) in a single pass with zero redundant scans.
inline bool EvalNetworkProxyRelevant(uint8_t* proxy, const uint8_t* ent, const void* connGuidPtr,
                                     uint64_t playerMask, const ReplicaAbiConfig& cfg,
                                     ReplicaFastStats* stats,
                                     bool& outStructuralTier0,
                                     void*& outP1, void*& outP2) noexcept {
    outStructuralTier0 = false;
    outP1 = nullptr;
    outP2 = nullptr;
    if ((*reinterpret_cast<const uint64_t*>(proxy + 0x1c0) & playerMask) != 0ULL) {
        return false;
    }
    if (!ent) return false;
    if (*(ent + cfg.offEntHidden1c4) != 0u) return false;
    if (*(ent + cfg.offEntDestroyed) != 0u) return false;

    const uint64_t classifiedGuid = *reinterpret_cast<const uint64_t*>(proxy + 0x1d8);
    void* p1 = *reinterpret_cast<void* const*>(ent + cfg.offEntParent1);
    void* p2 = *reinterpret_cast<void* const*>(ent + cfg.offEntParent2);
    outP1 = p1;
    outP2 = p2;

    if (classifiedGuid != kUnassignedGuid) {
        outStructuralTier0 = true;
        if (stats) stats->slow_rel_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return cfg.fnIsRelevant(proxy, connGuidPtr, playerMask);
    }
    if (*(ent + cfg.offEntHidden1c8) != 0u) return false;

    if (p1 != nullptr || p2 != nullptr || HasInvItemComponentFast(ent, cfg)) {
        outStructuralTier0 = true;
        if (stats) stats->slow_rel_fallbacks.fetch_add(1, std::memory_order_relaxed);
        return cfg.fnIsRelevant(proxy, connGuidPtr, playerMask);
    }
    return true;
}

// 100% exact fast-path equivalent of NetworkProxy::IsRelevantToClient(proxy, &guid, playerMask).
// Handles 98%+ of world entities inline with 0 virtual calls & 0 RB-tree lookups, delegating
// classified/parented/inventory entities directly to Klei's fnIsRelevant (still skipping the
// redundant GetClientObjectByGuid RB-tree lookup).
inline bool IsNetworkProxyRelevantFast(uint8_t* proxy, const uint8_t* ent, const void* connGuidPtr,
                                       uint64_t playerMask, const ReplicaAbiConfig& cfg,
                                       ReplicaFastStats* stats) noexcept {
    bool unusedTier0 = false;
    void* unusedP1 = nullptr;
    void* unusedP2 = nullptr;
    return EvalNetworkProxyRelevant(proxy, ent, connGuidPtr, playerMask, cfg, stats,
                                    unusedTier0, unusedP1, unusedP2);
}

// Complete drop-in replacement for cNetworkConnection::QueryReplicaList(conn, outConstruct, outDestroy).
inline void FastQueryReplicaList(void* conn, DsListPtr* outConstruct, DsListPtr* outDestroy,
                                 const ReplicaAbiConfig& cfg, ReplicaFastStats* stats,
                                 QueryScratch* optScratch = nullptr) noexcept {
    if (!conn || !outConstruct || !outDestroy || !cfg.IsValid()) return;

    void* netMgr = *cfg.ppNetMgr;
    if (!netMgr) return;

    auto* netMgrBytes = static_cast<uint8_t*>(netMgr);
    auto* rm3Bytes    = *reinterpret_cast<uint8_t**>(netMgrBytes + cfg.offNetMgrReplicaMgr);
    if (!rm3Bytes) return;

    const void* connGuidPtr = static_cast<const uint8_t*>(conn) + cfg.offConnGuid;
    void* client = cfg.fnGetClientByGuid(netMgr, connGuidPtr);
    if (!client) return;

    auto* clientBytes = static_cast<const uint8_t*>(client);
    const uint64_t playerMask = *reinterpret_cast<const uint64_t*>(clientBytes + cfg.offClientPlayerMask);
    const int slot = FindFirstSetBit64(playerMask);
    if (slot < 0 || slot >= 64) return;

    const bool isClient    = (*(netMgrBytes + cfg.offNetMgrIsClient) != 0u);
    const bool clientReady = (*(clientBytes + cfg.offClientReady) != 0u);

    auto* list0 = reinterpret_cast<DsMap*>(rm3Bytes + cfg.offRm3List0 + static_cast<size_t>(slot) * sizeof(DsMap));
    auto* list1 = reinterpret_cast<DsMap*>(rm3Bytes + cfg.offRm3List1 + static_cast<size_t>(slot) * sizeof(DsMap));
    auto* list2 = reinterpret_cast<DsMap*>(rm3Bytes + cfg.offRm3List2 + static_cast<size_t>(slot) * sizeof(DsMap));

    // Trivial-destructor POD static scratch (single sim-thread caller; zero __cxa_thread_atexit).
    static QueryScratch s_defaultScratch{};
    QueryScratch& sc = optScratch ? *optScratch : s_defaultScratch;
    sc.addList1.Clear();
    sc.addList2.Clear();
    sc.addList0.Clear();

    const bool pacingEnabled = (cfg.maxConstructPerTick > 0u);
    bool  hasPlayerPos  = false;
    float playerX       = 0.0f;
    float playerZ       = 0.0f;
    const bool  hasNearRadius = (cfg.nearRadiusWu > 0.0f);
    const float nearSq  = hasNearRadius ? (cfg.nearRadiusWu * cfg.nearRadiusWu) : 0.0f;
    const float midSq   = (cfg.midRadiusWu > 0.0f) ? (cfg.midRadiusWu * cfg.midRadiusWu) : (28.0f * 28.0f);
    const float farSq   = (cfg.farRadiusWu > 0.0f) ? (cfg.farRadiusWu * cfg.farRadiusWu) : (42.0f * 42.0f);

    if (pacingEnabled) {
        sc.neededParentEnts.Clear();
        sc.tier1Candidates.Clear();
        sc.deferredList0.Clear();
        if (cfg.proximityValid) {
            const auto* playerProxy = *reinterpret_cast<const uint8_t* const*>(clientBytes + cfg.offClientPlayerProxy);
            if (playerProxy) {
                const auto* playerEnt = *reinterpret_cast<const uint8_t* const*>(playerProxy + 0x18);
                if (playerEnt && *(playerEnt + cfg.offEntDestroyed) == 0u) {
                    float px = 0.0f, pz = 0.0f;
                    std::memcpy(&px, playerEnt + cfg.offEntPosX, sizeof(float));
                    std::memcpy(&pz, playerEnt + cfg.offEntPosZ, sizeof(float));
                    if (IsFiniteFloat(px) && IsFiniteFloat(pz)) {
                        playerX = px;
                        playerZ = pz;
                        hasPlayerPos = true;
                    }
                }
            }
        }
    }

    uint64_t fastHits = 0;
    uint64_t nonProxyHits = 0;
    uint64_t constructedNow = 0;
    uint64_t destroyedNow = 0;

    // ------------------------------------------------------------------------
    // Loop A: Pending-construction list0[slot] with O(N) in-place compaction
    //         and Module 10B Zero-Desync Tiered Construction Pacing
    // ------------------------------------------------------------------------
    const uint32_t n0 = list0->size;
    if (n0 > 0u && list0->arr) {
        if (stats) stats->list0_scanned.fetch_add(n0, std::memory_order_relaxed);
        DsMapNode* arr0 = list0->arr;
        uint32_t w0 = 0;
        for (uint32_t i = 0; i < n0; ++i) {
            void* rep = arr0[i].val;
            if (!rep) continue;

            void* const* vtbl = *reinterpret_cast<void* const* const*>(rep);
            const bool isProxy = (vtbl == cfg.networkProxyVtbl);

            bool dead = false;
            int32_t q = 4; // RM3CS_NO_ACTION
            const uint8_t* ent = nullptr;
            bool structuralTier0 = false;
            void* p1 = nullptr;
            void* p2 = nullptr;

            if (isProxy) {
                ++fastHits;
                auto* proxy = static_cast<uint8_t*>(rep) - 0x20;
                ent = *reinterpret_cast<const uint8_t* const*>(proxy + 0x18);
                dead = (ent != nullptr) && (*(ent + cfg.offEntDestroyed) != 0u);
                if (!dead) {
                    if (isClient) {
                        q = 3; // RM3CS_NEVER_CONSTRUCT
                    } else if (!clientReady) {
                        q = 4;
                    } else if (EvalNetworkProxyRelevant(proxy, ent, connGuidPtr, playerMask, cfg, stats,
                                                        structuralTier0, p1, p2)) {
                        const uint64_t ownerGuid = *reinterpret_cast<const uint64_t*>(proxy + 0x1c8);
                        if (ownerGuid != kUnassignedGuid) {
                            structuralTier0 = true;
                            void* ownerCli = cfg.fnGetClientByGuid(netMgr, proxy + 0x1c8);
                            if (ownerCli && *(static_cast<const uint8_t*>(ownerCli) + cfg.offClientBlocked) == 0u) {
                                q = 0; // RM3CS_SEND_CONSTRUCTION
                            } else {
                                q = 4;
                            }
                        } else {
                            q = 0; // RM3CS_SEND_CONSTRUCTION
                        }
                    } else {
                        q = 4;
                    }
                }
            } else {
                ++nonProxyHits;
                structuralTier0 = true;
                auto fnDead = *reinterpret_cast<const RepIsDestroyedFn_t*>(
                    reinterpret_cast<const uint8_t*>(vtbl) + cfg.vtblOffIsDestroyed);
                dead = fnDead(rep);
                if (!dead) {
                    auto fnQc = *reinterpret_cast<const RepQueryConstructionFn_t*>(
                        reinterpret_cast<const uint8_t*>(vtbl) + cfg.vtblOffQueryConstruct);
                    q = fnQc(rep, conn, nullptr);
                }
            }

            if (dead) {
                continue;
            }
            if (q == 0) {
                if (!DsMapContains(list1, rep)) {
                    if (!pacingEnabled || structuralTier0) {
                        DsListPush(outConstruct, rep, cfg);
                        sc.addList1.Push(rep);
                        ++constructedNow;
                        if (pacingEnabled) {
                            if (p1) sc.neededParentEnts.Push(p1);
                            if (p2) sc.neededParentEnts.Push(p2);
                        }
                    } else {
                        uint8_t bucket = 3u;
                        if (hasPlayerPos && ent) {
                            float ex = 0.0f, ez = 0.0f;
                            std::memcpy(&ex, ent + cfg.offEntPosX, sizeof(float));
                            std::memcpy(&ez, ent + cfg.offEntPosZ, sizeof(float));
                            if (IsFiniteFloat(ex) && IsFiniteFloat(ez)) {
                                const float dx = ex - playerX;
                                const float dz = ez - playerZ;
                                const float d2 = dx * dx + dz * dz;
                                if (hasNearRadius && d2 <= nearSq) bucket = 0u;
                                else if (d2 <= midSq)              bucket = 1u;
                                else if (d2 <= farSq)              bucket = 2u;
                                else                               bucket = 3u;
                            }
                        }
                        if (!sc.tier1Candidates.Push(Tier1Candidate{arr0[i], ent, bucket, 0u})) {
                            // Fail-closed OOM guard: never drop a replica
                            DsListPush(outConstruct, rep, cfg);
                            sc.addList1.Push(rep);
                            ++constructedNow;
                        }
                    }
                }
            } else if (q == 1) {
                if (!DsMapContains(list2, rep)) {
                    DsListPush(outConstruct, rep, cfg);
                    ++constructedNow;
                    if (pacingEnabled) {
                        if (p1) sc.neededParentEnts.Push(p1);
                        if (p2) sc.neededParentEnts.Push(p2);
                    }
                }
                sc.addList2.Push(rep);
            } else if (q == 2) {
                sc.addList2.Push(rep);
            } else if (q == 3) {
                // Drop from list0
            } else {
                if (w0 != i) arr0[w0] = arr0[i];
                ++w0;
            }
        }
        if (w0 != n0) {
            list0->size            = w0;
            list0->lastSearchValid = 0;
        }

        // Resolve Tier-1 candidates (only populated when pacingEnabled == true)
        if (pacingEnabled && sc.tier1Candidates.len > 0u) {
            const size_t candN = sc.tier1Candidates.len;
            if (candN <= cfg.maxConstructPerTick || !sc.mergeTmp.Reserve(n0)) {
                // Normal walking (<= budget) or OOM fail-closed: construct ALL in ascending pointer order
                for (size_t k = 0; k < candN; ++k) {
                    void* rep = sc.tier1Candidates.data[k].node.val;
                    DsListPush(outConstruct, rep, cfg);
                    sc.addList1.Push(rep);
                    ++constructedNow;
                }
            } else {
                // Burst pacing active: Tier-0 parent-before-child & <=nearRadiusWu bypass first,
                // then nearest-first Tier-1 buckets up to maxConstructPerTick; defer rest in ascending order.
                if (sc.neededParentEnts.len > 1u) {
                    SortPtrsAsc(sc.neededParentEnts.data, sc.neededParentEnts.len);
                }
                const void* const* parentArr = const_cast<const void* const*>(sc.neededParentEnts.data);
                const size_t parentN = sc.neededParentEnts.len;

                uint64_t parentPromotes = 0;
                uint64_t nearBypasses   = 0;
                size_t   bucketCounts[4] = {0u, 0u, 0u, 0u};

                for (size_t k = 0; k < candN; ++k) {
                    auto& c = sc.tier1Candidates.data[k];
                    if (parentN > 0u && c.ent && PtrBinarySearch(parentArr, parentN, c.ent)) {
                        c.chosen = 1u;
                        ++parentPromotes;
                    } else if (c.bucket == 0u) {
                        c.chosen = 1u;
                        ++nearBypasses;
                    } else {
                        ++bucketCounts[c.bucket];
                    }
                }

                uint32_t budgetLeft = cfg.maxConstructPerTick;
                for (uint8_t b = 1u; b <= 3u && budgetLeft > 0u; ++b) {
                    if (bucketCounts[b] == 0u) continue;
                    for (size_t k = 0; k < candN && budgetLeft > 0u; ++k) {
                        auto& c = sc.tier1Candidates.data[k];
                        if (c.chosen == 0u && c.bucket == b) {
                            c.chosen = 1u;
                            --budgetLeft;
                        }
                    }
                }

                uint64_t deferredNow = 0;
                for (size_t k = 0; k < candN; ++k) {
                    const auto& c = sc.tier1Candidates.data[k];
                    void* rep = c.node.val;
                    if (c.chosen != 0u) {
                        DsListPush(outConstruct, rep, cfg);
                        sc.addList1.Push(rep);
                        ++constructedNow;
                    } else {
                        if (sc.deferredList0.Push(rep)) {
                            ++deferredNow;
                        } else {
                            // Fail-closed OOM guard: never drop a replica
                            DsListPush(outConstruct, rep, cfg);
                            sc.addList1.Push(rep);
                            ++constructedNow;
                        }
                    }
                }

                if (sc.deferredList0.len > 0u) {
                    EnsureSortedPtrsAsc(sc.deferredList0.data, sc.deferredList0.len);
                    DsMapBulkMergeSorted(list0, sc.deferredList0, sc.mergeTmp, cfg);
                }

                if (stats) {
                    if (deferredNow > 0u) {
                        stats->paced_burst_ticks.fetch_add(1, std::memory_order_relaxed);
                        stats->paced_deferred_total.fetch_add(deferredNow, std::memory_order_relaxed);
                    }
                    if (parentPromotes > 0u) {
                        stats->paced_parent_promotes.fetch_add(parentPromotes, std::memory_order_relaxed);
                    }
                    if (nearBypasses > 0u) {
                        stats->paced_near_bypasses.fetch_add(nearBypasses, std::memory_order_relaxed);
                    }
                }
            }
        }

        if (sc.addList1.len > 0u) {
            if (pacingEnabled) {
                EnsureSortedPtrsAsc(sc.addList1.data, sc.addList1.len);
            }
            DsMapBulkMergeSorted(list1, sc.addList1, sc.mergeTmp, cfg);
        }
        if (sc.addList2.len > 0u) {
            DsMapBulkMergeSorted(list2, sc.addList2, sc.mergeTmp, cfg);
        }
    }

    // ------------------------------------------------------------------------
    // Loop B: Active constructed list1[slot] with O(N) in-place compaction
    // ------------------------------------------------------------------------
    const uint32_t n1 = list1->size;
    if (n1 > 0u && list1->arr) {
        if (stats) stats->list1_scanned.fetch_add(n1, std::memory_order_relaxed);
        DsMapNode* arr1 = list1->arr;
        uint32_t w1 = 0;
        for (uint32_t i = 0; i < n1; ++i) {
            void* rep = arr1[i].key;
            if (!rep) {
                if (w1 != i) arr1[w1] = arr1[i];
                ++w1;
                continue;
            }

            void* const* vtbl = *reinterpret_cast<void* const* const*>(rep);
            const bool isProxy = (vtbl == cfg.networkProxyVtbl);

            int32_t qd = 2; // RM3DS_DO_NOT_DESTROY
            if (isProxy) {
                ++fastHits;
                auto* proxy = static_cast<uint8_t*>(rep) - 0x20;
                const auto* ent = *reinterpret_cast<const uint8_t* const*>(proxy + 0x18);
                if (isClient) {
                    qd = 1;
                } else if (ent && *(ent + cfg.offEntDestroyed) != 0u) {
                    qd = 2;
                } else if (IsNetworkProxyRelevantFast(proxy, ent, connGuidPtr, playerMask, cfg, stats)) {
                    qd = 2;
                } else {
                    qd = 0; // RM3DS_SEND_DESTRUCTION
                }
            } else {
                ++nonProxyHits;
                auto fnQd = *reinterpret_cast<const RepQueryDestructionFn_t*>(
                    reinterpret_cast<const uint8_t*>(vtbl) + cfg.vtblOffQueryDestruct);
                qd = fnQd(rep, conn, nullptr);
            }

            if (qd == 0) {
                DsListPush(outDestroy, rep, cfg);
                ++destroyedNow;
                const uint64_t cullCopy = *reinterpret_cast<const uint64_t*>(
                    static_cast<const uint8_t*>(rep) + cfg.offRepCullMaskCopy);
                if ((cullCopy & playerMask) == 0ULL) {
                    sc.addList0.Push(rep);
                }
            } else {
                if (w1 != i) arr1[w1] = arr1[i];
                ++w1;
            }
        }
        if (w1 != n1) {
            list1->size            = w1;
            list1->lastSearchValid = 0;
        }

        if (sc.addList0.len > 0u) {
            DsMapBulkMergeSorted(list0, sc.addList0, sc.mergeTmp, cfg);
        }

        if (outDestroy->size >= 8u) {
            CompactConnectionDestroyed(conn, outDestroy, sc, cfg, stats);
        }
    }

    if (stats) {
        stats->qrl_calls.fetch_add(1, std::memory_order_relaxed);
        if (fastHits)       stats->fast_proxy_hits.fetch_add(fastHits, std::memory_order_relaxed);
        if (nonProxyHits)   stats->non_proxy_fallbacks.fetch_add(nonProxyHits, std::memory_order_relaxed);
        if (constructedNow) stats->constructed_total.fetch_add(constructedNow, std::memory_order_relaxed);
        if (destroyedNow)   stats->destroyed_total.fetch_add(destroyedNow, std::memory_order_relaxed);
    }
}

// Clears Replica3::preSerialized (+0x178) ONLY on in-range replicas inside each active
// connection's serializedReplicaList (+0x50/+0x58), replacing the 17,000+ world userReplicaList scan.
inline uint32_t ClearSerializedPreFlags(void* rm3, const ReplicaAbiConfig& cfg,
                                        ReplicaFastStats* stats) noexcept {
    if (!rm3) return 0;
    auto* rm3Bytes = static_cast<uint8_t*>(rm3);
    auto** worldListPtr = *reinterpret_cast<uint8_t***>(rm3Bytes + cfg.offRm3WorldListPtr);
    if (!worldListPtr || !worldListPtr[0]) return 0;

    uint8_t* world = worldListPtr[0];
    auto* connArr = *reinterpret_cast<uint8_t***>(world + 0x00);
    const uint32_t connCnt = *reinterpret_cast<const uint32_t*>(world + 0x08);
    if (connCnt == 0u || !connArr) {
        if (stats) stats->preser_zero_conn_skips.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    uint32_t cleared = 0;
    for (uint32_t c = 0; c < connCnt; ++c) {
        uint8_t* conn = connArr[c];
        if (!conn) continue;
        auto* serList = reinterpret_cast<const DsListPtr*>(conn + cfg.offConnSerializedLst);
        if (!serList->arr || serList->size == 0u) continue;
        void* const* arr = serList->arr;
        const uint32_t n = serList->size;
        for (uint32_t i = 0; i < n; ++i) {
            void* lsr = arr[i];
            if (!lsr) continue;
            auto* rep = *reinterpret_cast<uint8_t**>(lsr);
            if (rep && *(rep + cfg.offRepPreSerialized) != 0u) {
                *(rep + cfg.offRepPreSerialized) = 0u;
                ++cleared;
            }
        }
    }
    if (stats && cleared > 0u) {
        stats->preser_cleared.fetch_add(cleared, std::memory_order_relaxed);
    }
    return cleared;
}

} // namespace hc_replica
