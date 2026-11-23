#pragma once
// =============================================================================
// DST HyperCore Server — O(N log N) Flat-Hash + Binary-Heap + Arena A* Engine
// =============================================================================
// Replaces Klei's quadratic O(S^2 log S) Pathfinder AStar::Step (which runs
// std::sort on the entire openList every step + O(N) pointer-chasing linear
// scans of both closedList and openList per neighbor + per-node operator new)
// while calling Klei's EXACT GetNeighbors*, Heuristic, and resultPath helpers
// for 100% vanilla wall/ocean/boat/road/pathfinding parity & zero save impact.
// =============================================================================

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace fast_astar {

struct TileCoord {
    int32_t type;
    int32_t x;
    int32_t z;

    bool operator==(const TileCoord& o) const noexcept {
        return type == o.type && x == o.x && z == o.z;
    }
};
static_assert(sizeof(TileCoord) == 12, "TileCoord must match Klei 12-byte PathCoord");

struct NeighborEntry {
    float     cost;
    TileCoord pos;
};
static_assert(sizeof(NeighborEntry) == 16, "NeighborEntry must match Klei 16-byte layout");

#ifdef _WIN32
// Exact MSVC90 std::vector<NeighborEntry> layout (0x20 bytes)
struct MsvcNeighborVec {
    void*          proxy;
    NeighborEntry* begin;
    NeighborEntry* end;
    NeighborEntry* cap;
};
static_assert(sizeof(MsvcNeighborVec) == 32, "MsvcNeighborVec must be 32 bytes");
using NeighborVec = MsvcNeighborVec;
#else
// Exact GCC/libstdc++ std::vector<NeighborEntry> layout (0x18 bytes)
struct GccNeighborVec {
    NeighborEntry* begin;
    NeighborEntry* end;
    NeighborEntry* cap;
};
static_assert(sizeof(GccNeighborVec) == 24, "GccNeighborVec must be 24 bytes");
using NeighborVec = GccNeighborVec;
#endif

struct EngineCallbacks {
    void  (*reserveNeighbors)(NeighborVec* vec, uint64_t count) = nullptr;
    void  (*getNeighborsNormal)(void* ctx, const TileCoord* pos, NeighborVec* out) = nullptr;
    void  (*getNeighborsType2)(void* ctx, const TileCoord* pos, NeighborVec* out) = nullptr;
    float (*heuristic)(void* ctx, const TileCoord* pos) = nullptr;
    void  (*clearPath)(void* pathVec) = nullptr;
    void  (*pushPathNode)(void* pathVec, const TileCoord* pos) = nullptr;
    void  (*freeMem)(void* ptr) = nullptr;
};

static inline uint64_t HashCoord(const TileCoord& c) noexcept {
    uint64_t h = (static_cast<uint64_t>(static_cast<uint32_t>(c.x))) |
                 (static_cast<uint64_t>(static_cast<uint32_t>(c.z)) << 32);
    h ^= static_cast<uint64_t>(static_cast<uint32_t>(c.type)) * 0x9E3779B97F4A7C15ULL;
    h ^= h >> 30;
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27;
    h *= 0x94D049BB133111EBULL;
    h ^= h >> 31;
    return h;
}

struct Node {
    int32_t   parent;   // -1 for root
    TileCoord pos;
    float     g;
    float     h;
    int32_t   heapPos;  // >=0 when in OPEN heap, -1 when CLOSED
};

struct HashSlot {
    uint32_t stamp;     // == tableStamp iff occupied
    uint32_t nodeIdx;
};

class SearchState {
public:
    static constexpr uint32_t kInitialHashCap = 256u; // power of 2

    SearchState() {
        nodes_.reserve(256);
        heap_.reserve(128);
        table_.assign(kInitialHashCap, HashSlot{0u, 0u});
        hashMask_ = kInitialHashCap - 1u;
        tableStamp_ = 1u;
    }

    void Init(const TileCoord& start, float startH) {
        ClearFast();
        AddNode(-1, start, 0.0f, startH);
    }

    void ClearFast() {
        nodes_.clear();
        heap_.clear();
        occupied_ = 0;
        ++tableStamp_;
        if (tableStamp_ == 0u) {
            std::memset(table_.data(), 0, table_.size() * sizeof(HashSlot));
            tableStamp_ = 1u;
        }
        // Keep buffers warm, but shrink pathological outliers (>16384 nodes)
        if (table_.size() > 32768u) {
            table_.assign(kInitialHashCap, HashSlot{0u, 0u});
            table_.shrink_to_fit();
            hashMask_ = kInitialHashCap - 1u;
            tableStamp_ = 1u;
        }
        if (nodes_.capacity() > 16384u) {
            std::vector<Node>().swap(nodes_);
            nodes_.reserve(256);
        }
        if (heap_.capacity() > 8192u) {
            std::vector<uint32_t>().swap(heap_);
            heap_.reserve(128);
        }
    }

    // Runs up to maxSteps A* iterations.
    // Returns number of steps executed (matching Klei exact return semantics:
    // goal hit on iteration `s` returns `s`; exhaustion returns iterations run).
    // Writes status out: 0=running, 1=found, 2=failed.
    // When status==1, reconstructs goal->start path via cb.pushPathNode(pathVec, &coord).
    uint32_t Step(void* ctx, const TileCoord& goal, uint32_t maxSteps,
                  const EngineCallbacks& cb, void* pathVec,
                  int32_t& outStatus, uint32_t* outExpandedNodes = nullptr) {
        outStatus = 0;
        if (maxSteps == 0u) return 0u;

        NeighborVec nbVec = {};
        if (cb.reserveNeighbors) {
            cb.reserveNeighbors(&nbVec, 8u);
        }

        uint32_t steps = 0u;
        while (steps < maxSteps) {
            if (heap_.empty()) {
                outStatus = 2; // failed (no path)
                break;
            }

            const uint32_t curIdx = PopMin();
            const TileCoord curPos = nodes_[curIdx].pos;
            const float curG = nodes_[curIdx].g;

            if (curPos == goal) {
                outStatus = 1; // found!
                if (cb.clearPath) cb.clearPath(pathVec);
                int32_t trace = static_cast<int32_t>(curIdx);
                while (trace >= 0) {
                    cb.pushPathNode(pathVec, &nodes_[static_cast<size_t>(trace)].pos);
                    trace = nodes_[static_cast<size_t>(trace)].parent;
                }
                break;
            }

            // Clear neighbor vector without freeing capacity (matches Klei 0x1400b2878)
            nbVec.end = nbVec.begin;
            if (curPos.type == 2) {
                cb.getNeighborsType2(ctx, &curPos, &nbVec);
            } else {
                cb.getNeighborsNormal(ctx, &curPos, &nbVec);
            }

            const NeighborEntry* nb = nbVec.begin;
            const NeighborEntry* nbEnd = nbVec.end;
            for (; nb < nbEnd; ++nb) {
                const TileCoord& np = nb->pos;
                if (np == curPos) continue;

                const uint64_t hVal = HashCoord(np);
                const uint32_t existing = FindNode(np, hVal);
                if (existing != kNotFound) {
                    Node& exNode = nodes_[existing];
                    if (exNode.heapPos < 0) {
                        continue; // already CLOSED
                    }
                    const float newG = curG + nb->cost;
                    if (newG < exNode.g) {
                        const float oldF = exNode.g + exNode.h;
                        const float newH = cb.heuristic(ctx, &np);
                        exNode.parent = static_cast<int32_t>(curIdx);
                        exNode.g = newG;
                        exNode.h = newH;
                        if (newG + newH <= oldF) {
                            SiftUp(static_cast<uint32_t>(exNode.heapPos));
                        } else {
                            SiftDown(static_cast<uint32_t>(exNode.heapPos));
                        }
                    }
                } else {
                    const float newG = curG + nb->cost;
                    const float newH = cb.heuristic(ctx, &np);
                    AddNode(static_cast<int32_t>(curIdx), np, newG, newH, hVal);
                }
            }

            ++steps;
        }

        if (nbVec.begin && cb.freeMem) {
            cb.freeMem(nbVec.begin);
        }
        if (outExpandedNodes) *outExpandedNodes = steps;
        return steps;
    }

    size_t NodeCount() const noexcept { return nodes_.size(); }
    size_t OpenCount() const noexcept { return heap_.size(); }

private:
    static constexpr uint32_t kNotFound = 0xFFFFFFFFu;

    std::vector<Node>     nodes_;
    std::vector<uint32_t> heap_;
    std::vector<HashSlot> table_;
    uint32_t              hashMask_   = kInitialHashCap - 1u;
    uint32_t              tableStamp_ = 1u;
    uint32_t              occupied_   = 0u;

    inline bool Better(uint32_t idxA, uint32_t idxB) const noexcept {
        const Node& a = nodes_[idxA];
        const Node& b = nodes_[idxB];
        const float fa = a.g + a.h;
        const float fb = b.g + b.h;
        if (fa != fb) return fa < fb;
        if (a.h != b.h) return a.h < b.h;
        return idxA < idxB;
    }

    void SiftUp(uint32_t pos) noexcept {
        const uint32_t idx = heap_[pos];
        while (pos > 0u) {
            const uint32_t parentPos = (pos - 1u) >> 1;
            const uint32_t parentIdx = heap_[parentPos];
            if (!Better(idx, parentIdx)) break;
            heap_[pos] = parentIdx;
            nodes_[parentIdx].heapPos = static_cast<int32_t>(pos);
            pos = parentPos;
        }
        heap_[pos] = idx;
        nodes_[idx].heapPos = static_cast<int32_t>(pos);
    }

    void SiftDown(uint32_t pos) noexcept {
        const uint32_t n = static_cast<uint32_t>(heap_.size());
        const uint32_t idx = heap_[pos];
        while (true) {
            uint32_t left = (pos << 1) + 1u;
            if (left >= n) break;
            uint32_t best = left;
            uint32_t right = left + 1u;
            if (right < n && Better(heap_[right], heap_[left])) {
                best = right;
            }
            if (!Better(heap_[best], idx)) break;
            heap_[pos] = heap_[best];
            nodes_[heap_[pos]].heapPos = static_cast<int32_t>(pos);
            pos = best;
        }
        heap_[pos] = idx;
        nodes_[idx].heapPos = static_cast<int32_t>(pos);
    }

    uint32_t PopMin() noexcept {
        const uint32_t topIdx = heap_[0];
        nodes_[topIdx].heapPos = -1; // mark CLOSED
        const uint32_t lastIdx = heap_.back();
        heap_.pop_back();
        if (!heap_.empty()) {
            heap_[0] = lastIdx;
            nodes_[lastIdx].heapPos = 0;
            SiftDown(0u);
        }
        return topIdx;
    }

    uint32_t FindNode(const TileCoord& pos, uint64_t hVal) const noexcept {
        uint32_t slot = static_cast<uint32_t>(hVal) & hashMask_;
        const uint32_t st = tableStamp_;
        while (true) {
            const HashSlot& hs = table_[slot];
            if (hs.stamp != st) return kNotFound;
            if (nodes_[hs.nodeIdx].pos == pos) return hs.nodeIdx;
            slot = (slot + 1u) & hashMask_;
        }
    }

    void GrowHashTable() {
        const uint32_t newCap = static_cast<uint32_t>(table_.size()) << 1;
        table_.assign(newCap, HashSlot{0u, 0u});
        hashMask_ = newCap - 1u;
        tableStamp_ = 1u;
        const uint32_t count = static_cast<uint32_t>(nodes_.size());
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t slot = static_cast<uint32_t>(HashCoord(nodes_[i].pos)) & hashMask_;
            while (table_[slot].stamp == 1u) {
                slot = (slot + 1u) & hashMask_;
            }
            table_[slot] = HashSlot{1u, i};
        }
    }

    uint32_t AddNode(int32_t parent, const TileCoord& pos, float g, float h) {
        return AddNode(parent, pos, g, h, HashCoord(pos));
    }

    uint32_t AddNode(int32_t parent, const TileCoord& pos, float g, float h, uint64_t hVal) {
        if ((occupied_ + 1u) * 10u >= static_cast<uint32_t>(table_.size()) * 6u) {
            GrowHashTable();
        }
        const uint32_t nodeIdx = static_cast<uint32_t>(nodes_.size());
        const uint32_t heapPos = static_cast<uint32_t>(heap_.size());
        nodes_.push_back(Node{parent, pos, g, h, static_cast<int32_t>(heapPos)});
        heap_.push_back(nodeIdx);
        SiftUp(heapPos);

        uint32_t slot = static_cast<uint32_t>(hVal) & hashMask_;
        const uint32_t st = tableStamp_;
        while (table_[slot].stamp == st) {
            slot = (slot + 1u) & hashMask_;
        }
        table_[slot] = HashSlot{st, nodeIdx};
        ++occupied_;
        return nodeIdx;
    }
};

} // namespace fast_astar
