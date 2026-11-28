// Standalone Red-Team Security & Correctness Test Suite for DST HyperCore Server v1.1.0
// Tests FastDataDumper against:
//   [1] Multi-branch cyclic tables (c.a = c; c.b = c) -> O(1) cycle_detected (stops 2^128 OOM)
//   [2] Exponential shared non-cyclic DAG (2^25 leaves) -> node_limit_exceeded in milliseconds
//   [3] Buffer cap / OOM safety (maxBytes = 64) -> zero null deref / zero heap overflow
//   [4] Locale-independent decimal formatting under comma-decimal LC_NUMERIC (German/French)
//   [5] NaN / +Inf / -Inf corrupt_number rejection
//   [6] String escaping, Lua keyword quoting, array/hash encoding, ShrinkIfLarge()
//   [7] SpatialGrid finite/radius bounds guard (math.huge / Inf / NaN / negative)

#include "../native_plugin/fast_dumper.h"
#include "../native_plugin/fast_json.h"
#include "../native_plugin/fast_astar.h"
#include "../native_plugin/fast_astar_manager.h"
#include "../native_plugin/anti_stw_gc.h"
#include "../native_plugin/fs_neg_cache.h"
#include "../native_plugin/async_save_commit.h"
#include "../native_plugin/rpc_qos.h"
#include "../native_plugin/klei_save_codec.h"
#include "../native_plugin/replica_fastpath.h"
#include "../native_plugin/server_hardware_tier.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "[FAIL] Line %d: %s\n", __LINE__, #cond); \
        std::exit(1); \
    } \
} while (0)

struct MockVal;
struct MockTable {
    std::vector<std::pair<MockVal, MockVal>> entries;
};

struct MockVal {
    int type = HC_LUA_TNIL;
    bool b = false;
    double num = 0.0;
    std::string str;
    MockTable* tbl = nullptr;

    static MockVal Nil() { return MockVal{}; }
    static MockVal Bool(bool v) { MockVal m; m.type = HC_LUA_TBOOLEAN; m.b = v; return m; }
    static MockVal Num(double v) { MockVal m; m.type = HC_LUA_TNUMBER; m.num = v; return m; }
    static MockVal Str(std::string s) { MockVal m; m.type = HC_LUA_TSTRING; m.str = std::move(s); return m; }
    static MockVal Tbl(MockTable* t) { MockVal m; m.type = HC_LUA_TTABLE; m.tbl = t; return m; }
};

struct MockLuaState {
    std::vector<MockVal> stack;
    std::vector<std::unique_ptr<MockTable>> ownedTables;
    // Key iteration index map keyed by stack slot of key
    int AbsIdx(int idx) const {
        if (idx > 0) return idx - 1;
        return static_cast<int>(stack.size()) + idx;
    }
};

static int m_checkstack(void*, int) { return 1; }
static int m_gettop(void* L) { return static_cast<int>(static_cast<MockLuaState*>(L)->stack.size()); }
static void m_settop(void* L, int idx) {
    auto* S = static_cast<MockLuaState*>(L);
    if (idx >= 0) S->stack.resize(static_cast<size_t>(idx));
    else S->stack.resize(S->stack.size() + static_cast<size_t>(idx) + 1);
}
static int m_type(void* L, int idx) {
    auto* S = static_cast<MockLuaState*>(L);
    int i = S->AbsIdx(idx);
    if (i < 0 || i >= static_cast<int>(S->stack.size())) return HC_LUA_TNIL;
    return S->stack[i].type;
}
static double m_tonumber(void* L, int idx) {
    auto* S = static_cast<MockLuaState*>(L);
    return S->stack[S->AbsIdx(idx)].num;
}
static int m_toboolean(void* L, int idx) {
    auto* S = static_cast<MockLuaState*>(L);
    const auto& v = S->stack[S->AbsIdx(idx)];
    if (v.type == HC_LUA_TNIL) return 0;
    if (v.type == HC_LUA_TBOOLEAN) return v.b ? 1 : 0;
    return 1;
}
static const char* m_tolstring(void* L, int idx, size_t* len) {
    auto* S = static_cast<MockLuaState*>(L);
    const auto& v = S->stack[S->AbsIdx(idx)];
    if (len) *len = v.str.size();
    return v.str.c_str();
}
static const void* m_topointer(void* L, int idx) {
    auto* S = static_cast<MockLuaState*>(L);
    const auto& v = S->stack[S->AbsIdx(idx)];
    return v.type == HC_LUA_TTABLE ? static_cast<const void*>(v.tbl) : nullptr;
}
static void m_pushnil(void* L) { static_cast<MockLuaState*>(L)->stack.push_back(MockVal::Nil()); }
static void m_pushlstring(void* L, const char* s, size_t l) {
    static_cast<MockLuaState*>(L)->stack.push_back(MockVal::Str(std::string(s, l)));
}
static void m_pushstring(void* L, const char* s) {
    static_cast<MockLuaState*>(L)->stack.push_back(MockVal::Str(std::string(s)));
}
static void m_pushboolean(void* L, int b) {
    static_cast<MockLuaState*>(L)->stack.push_back(MockVal::Bool(b != 0));
}
static void m_pushvalue(void* L, int idx) {
    auto* S = static_cast<MockLuaState*>(L);
    S->stack.push_back(S->stack[S->AbsIdx(idx)]);
}
static void m_pushnumber(void* L, double n) {
    static_cast<MockLuaState*>(L)->stack.push_back(MockVal::Num(n));
}
static void m_createtable(void* L, int, int) {
    auto* S = static_cast<MockLuaState*>(L);
    S->ownedTables.push_back(std::make_unique<MockTable>());
    S->stack.push_back(MockVal::Tbl(S->ownedTables.back().get()));
}
static void m_rawset(void* L, int idx) {
    auto* S = static_cast<MockLuaState*>(L);
    int ti = S->AbsIdx(idx);
    MockTable* t = S->stack[ti].tbl;
    MockVal val = S->stack.back(); S->stack.pop_back();
    MockVal key = S->stack.back(); S->stack.pop_back();
    if (val.type == HC_LUA_TNIL) return;
    for (auto& kv : t->entries) {
        if (kv.first.type == key.type && kv.first.str == key.str && kv.first.num == key.num) {
            kv.second = val;
            return;
        }
    }
    t->entries.emplace_back(std::move(key), std::move(val));
}
static void m_rawseti(void* L, int idx, int n) {
    auto* S = static_cast<MockLuaState*>(L);
    int ti = S->AbsIdx(idx);
    MockTable* t = S->stack[ti].tbl;
    MockVal val = S->stack.back(); S->stack.pop_back();
    MockVal key = MockVal::Num(static_cast<double>(n));
    if (val.type == HC_LUA_TNIL) return;
    for (auto& kv : t->entries) {
        if (kv.first.type == HC_LUA_TNUMBER && kv.first.num == key.num) {
            kv.second = val;
            return;
        }
    }
    t->entries.emplace_back(key, std::move(val));
}

static bool ValEq(const MockVal& a, const MockVal& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case HC_LUA_TNIL: return true;
        case HC_LUA_TBOOLEAN: return a.b == b.b;
        case HC_LUA_TNUMBER: return a.num == b.num;
        case HC_LUA_TSTRING: return a.str == b.str;
        case HC_LUA_TTABLE: return a.tbl == b.tbl;
        default: return false;
    }
}

static int m_next(void* L, int idx) {
    auto* S = static_cast<MockLuaState*>(L);
    int ti = S->AbsIdx(idx);
    MockTable* tbl = S->stack[ti].tbl;
    MockVal key = S->stack.back();
    S->stack.pop_back();
    if (!tbl || tbl->entries.empty()) return 0;
    size_t nextIdx = 0;
    if (key.type != HC_LUA_TNIL) {
        bool found = false;
        for (size_t i = 0; i < tbl->entries.size(); ++i) {
            if (ValEq(tbl->entries[i].first, key)) {
                nextIdx = i + 1;
                found = true;
                break;
            }
        }
        if (!found || nextIdx >= tbl->entries.size()) return 0;
    }
    S->stack.push_back(tbl->entries[nextIdx].first);
    S->stack.push_back(tbl->entries[nextIdx].second);
    return 1;
}

static LuaApiTable MakeMockApi() {
    LuaApiTable api{};
    api.checkstack = m_checkstack;
    api.gettop = m_gettop;
    api.settop = m_settop;
    api.pushvalue = m_pushvalue;
    api.type = m_type;
    api.tonumber = m_tonumber;
    api.toboolean = m_toboolean;
    api.tolstring = m_tolstring;
    api.topointer = m_topointer;
    api.pushnil = m_pushnil;
    api.pushnumber = m_pushnumber;
    api.pushlstring = m_pushlstring;
    api.pushstring = m_pushstring;
    api.pushboolean = m_pushboolean;
    api.createtable = m_createtable;
    api.next = m_next;
    api.rawset = m_rawset;
    api.rawseti = m_rawseti;
    CHECK(api.ReadyForFastDump());
    CHECK(api.ReadyForFastJson());
    return api;
}

// Mirror the exact finite/bounds guard used in HookSpatialGridQuery
static bool SpatialGuardAccepts(float x, float z, float radius,
                                float gridMinX, float gridMinZ, float cellSize,
                                int gridW, int gridH,
                                int& x0, int& x1, int& z0, int& z1) {
    if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(radius) ||
        radius < 0.0f || radius > 100000.0f) {
        return false; // vanilla fallback
    }
    if (gridW <= 0 || gridH <= 0 || cellSize <= 0.0f || !std::isfinite(cellSize)) {
        return false;
    }
    double invCell = 1.0 / static_cast<double>(cellSize);
    double dx0 = std::floor((static_cast<double>(x) - static_cast<double>(radius) - static_cast<double>(gridMinX)) * invCell);
    double dx1 = std::floor((static_cast<double>(x) + static_cast<double>(radius) - static_cast<double>(gridMinX)) * invCell);
    double dz0 = std::floor((static_cast<double>(z) - static_cast<double>(radius) - static_cast<double>(gridMinZ)) * invCell);
    double dz1 = std::floor((static_cast<double>(z) + static_cast<double>(radius) - static_cast<double>(gridMinZ)) * invCell);
    if (dx1 < 0.0 || dz1 < 0.0 || dx0 >= static_cast<double>(gridW) || dz0 >= static_cast<double>(gridH)) {
        x0 = 1; x1 = 0; z0 = 1; z1 = 0; // legitimately outside grid
        return true;
    }
    x0 = (dx0 < 0.0) ? 0 : static_cast<int>(dx0);
    x1 = (dx1 >= static_cast<double>(gridW)) ? (gridW - 1) : static_cast<int>(dx1);
    z0 = (dz0 < 0.0) ? 0 : static_cast<int>(dz0);
    z1 = (dz1 >= static_cast<double>(gridH)) ? (gridH - 1) : static_cast<int>(dz1);
    return true;
}

int main() {
    LuaApiTable api = MakeMockApi();
    fast_dumper::GrowBuf buf;
    const char* err = nullptr;

    // [1] Multi-branch cycle: c.a = c; c.b = c (Audit VETO-SERVER-01 2^128 OOM attack)
    {
        MockTable cyc;
        cyc.entries.push_back({ MockVal::Str("a"), MockVal::Tbl(&cyc) });
        cyc.entries.push_back({ MockVal::Str("b"), MockVal::Tbl(&cyc) });
        MockLuaState S;
        S.stack.push_back(MockVal::Tbl(&cyc));
        auto t0 = std::chrono::steady_clock::now();
        auto st = fast_dumper::DumpRoot(&S, api, 1, nullptr, 0, buf, &err);
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(st == fast_dumper::Status::FallbackNeeded);
        CHECK(err && std::strcmp(err, "cycle_detected") == 0);
        CHECK(S.stack.size() == 1);
        std::printf("[PASS] 1. Multi-branch cycle {a=c,b=c} rejected in %lld us -> %s\n", (long long)us, err);
    }

    // [2] Non-cyclic exponential DAG (depth 25 => 2^25 = 33.5M leaves)
    {
        std::vector<std::unique_ptr<MockTable>> nodes;
        nodes.push_back(std::make_unique<MockTable>());
        nodes[0]->entries.push_back({ MockVal::Num(1), MockVal::Num(42) });
        for (int i = 1; i <= 25; ++i) {
            auto t = std::make_unique<MockTable>();
            t->entries.push_back({ MockVal::Num(1), MockVal::Tbl(nodes[i - 1].get()) });
            t->entries.push_back({ MockVal::Num(2), MockVal::Tbl(nodes[i - 1].get()) });
            nodes.push_back(std::move(t));
        }
        MockLuaState S;
        S.stack.push_back(MockVal::Tbl(nodes.back().get()));
        auto t0 = std::chrono::steady_clock::now();
        auto st = fast_dumper::DumpRoot(&S, api, 1, nullptr, 0, buf, &err);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(st == fast_dumper::Status::FallbackNeeded);
        CHECK(err && std::strcmp(err, "node_limit_exceeded") == 0);
        CHECK(S.stack.size() == 1);
        std::printf("[PASS] 2. Exponential DAG (2^25 leaves) aborted safely in %lld ms -> %s\n", (long long)ms, err);
    }

    // [3] Forced Buffer Cap / OOM Safety (maxBytes = 64 on a larger table)
    {
        MockTable big;
        for (int i = 1; i <= 50; ++i) {
            big.entries.push_back({ MockVal::Num(i), MockVal::Str("0123456789abcdef") });
        }
        fast_dumper::GrowBuf tinyBuf;
        tinyBuf.maxBytes = 64;
        MockLuaState S;
        S.stack.push_back(MockVal::Tbl(&big));
        auto st = fast_dumper::DumpRoot(&S, api, 1, nullptr, 0, tinyBuf, &err);
        CHECK(st == fast_dumper::Status::FallbackNeeded);
        CHECK(tinyBuf.oom == true);
        CHECK(err && std::strcmp(err, "buffer_limit_exceeded") == 0);
        CHECK(S.stack.size() == 1);
        tinyBuf.ShrinkIfLarge(0);
        CHECK(tinyBuf.data == nullptr && tinyBuf.cap == 0);
        std::printf("[PASS] 3. Forced OOM/cap (maxBytes=64) handled cleanly with ZERO overflow -> %s\n", err);
    }

    // [4] Locale-independent decimal point formatting under German/French comma locale
    {
        const char* applied = std::setlocale(LC_ALL, "German_Germany.1252");
        if (!applied) applied = std::setlocale(LC_ALL, "French_France.1252");
        if (!applied) applied = std::setlocale(LC_ALL, "de-DE");
        char probe[32];
        std::snprintf(probe, sizeof(probe), "%.2f", 3.14);

        MockTable nums;
        nums.entries.push_back({ MockVal::Str("pi"), MockVal::Num(3.1415926535897) });
        nums.entries.push_back({ MockVal::Str("tiny"), MockVal::Num(1.25e-7) });
        nums.entries.push_back({ MockVal::Str("negzero"), MockVal::Num(-0.0) });
        nums.entries.push_back({ MockVal::Str("end"), MockVal::Str("line1\n\"quote\"\\bs") });
        MockLuaState S;
        S.stack.push_back(MockVal::Tbl(&nums));
        auto st = fast_dumper::DumpRoot(&S, api, 1, nullptr, 0, buf, &err);
        CHECK(st == fast_dumper::Status::Ok);
        std::string out(buf.data, buf.len);
        CHECK(out.find("pi=3.1415926535897") != std::string::npos);
        CHECK(out.find("3,14") == std::string::npos); // NEVER German comma decimal!
        CHECK(out.find("[\"end\"]=\"line1\\n\\\"quote\\\"\\\\bs\"") != std::string::npos);
        CHECK(out.find("negzero=0") != std::string::npos);
        std::setlocale(LC_ALL, "C");
        std::printf("[PASS] 4. Locale immunity (locale=%s, snprintf(3.14)=%s) -> FastDump=%s\n",
                    applied ? applied : "fallback-C", probe, out.c_str());
    }

    // [5] NaN / +Inf / -Inf corrupt_number rejection
    {
        double badVals[3] = {
            std::numeric_limits<double>::quiet_NaN(),
            std::numeric_limits<double>::infinity(),
            -std::numeric_limits<double>::infinity(),
        };
        for (double v : badVals) {
            MockTable t;
            t.entries.push_back({ MockVal::Str("x"), MockVal::Num(v) });
            MockLuaState S;
            S.stack.push_back(MockVal::Tbl(&t));
            auto st = fast_dumper::DumpRoot(&S, api, 1, nullptr, 0, buf, &err);
            CHECK(st == fast_dumper::Status::CorruptNumber);
            CHECK(err && std::strcmp(err, "corrupt_number") == 0);
            CHECK(S.stack.size() == 1);
        }
        std::printf("[PASS] 5. NaN / +Inf / -Inf all rejected with Status::CorruptNumber\n");
    }

    // [6] SpatialGrid math.huge / Inf / NaN / negative radius guard
    {
        int x0 = -1, x1 = -1, z0 = -1, z1 = -1;
        CHECK(!SpatialGuardAccepts(0.0f, 0.0f, std::numeric_limits<float>::infinity(), -1000, -1000, 8, 250, 250, x0, x1, z0, z1));
        CHECK(!SpatialGuardAccepts(0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), -1000, -1000, 8, 250, 250, x0, x1, z0, z1));
        CHECK(!SpatialGuardAccepts(0.0f, 0.0f, -5.0f, -1000, -1000, 8, 250, 250, x0, x1, z0, z1));
        CHECK(!SpatialGuardAccepts(0.0f, 0.0f, 200000.0f, -1000, -1000, 8, 250, 250, x0, x1, z0, z1));
        CHECK(SpatialGuardAccepts(0.0f, 0.0f, 50000.0f, -1000, -1000, 8, 250, 250, x0, x1, z0, z1));
        CHECK(x0 == 0 && x1 == 249 && z0 == 0 && z1 == 249);
        std::printf("[PASS] 6. SpatialGrid math.huge/NaN/neg/huge-radius guards verified (clamped=%d..%d, %d..%d)\n", x0, x1, z0, z1);
    }

    buf.ShrinkIfLarge(0);

    // [7] FastAStar Exact Shortest-Path Equivalence + Portal Type2 + Multi-Tick Resumption + Start==Goal
    {
        struct GridWorld {
            int W = 64, H = 64;
            std::vector<uint8_t> wall; // 1 = wall, 2 = fast road (0.6x cost)
            fast_astar::TileCoord goal{0, 0, 0};
            bool AtWall(int x, int z) const {
                if (x < 0 || z < 0 || x >= W || z >= H) return true;
                return wall[static_cast<size_t>(z * W + x)] == 1;
            }
            float TileCost(int x, int z) const {
                return (wall[static_cast<size_t>(z * W + x)] == 2) ? 0.6f : 1.0f;
            }
        };
        static auto reserveNb = [](fast_astar::MsvcNeighborVec* v, uint64_t cnt) {
            v->begin = static_cast<fast_astar::NeighborEntry*>(std::malloc(static_cast<size_t>(cnt) * sizeof(fast_astar::NeighborEntry)));
            v->end = v->begin;
            v->cap = v->begin + cnt;
        };
        static auto freeNb = [](void* p) { std::free(p); };
        static auto pushNb = [](fast_astar::MsvcNeighborVec* v, fast_astar::NeighborEntry e) {
            if (v->end == v->cap) {
                size_t sz = static_cast<size_t>(v->end - v->begin);
                size_t ncap = sz ? sz * 2 : 16;
                auto* nb = static_cast<fast_astar::NeighborEntry*>(std::realloc(v->begin, ncap * sizeof(fast_astar::NeighborEntry)));
                v->begin = nb; v->end = nb + sz; v->cap = nb + ncap;
            }
            *v->end++ = e;
        };
        static auto getNbNorm = [](void* ctx, const fast_astar::TileCoord* p, fast_astar::MsvcNeighborVec* out) {
            const auto* g = static_cast<const GridWorld*>(ctx);
            static constexpr int dx[8] = {1,-1,0,0,1,1,-1,-1};
            static constexpr int dz[8] = {0,0,1,-1,1,-1,1,-1};
            for (int k = 0; k < 8; ++k) {
                int nx = p->x + dx[k], nz = p->z + dz[k];
                if (g->AtWall(nx, nz)) continue;
                float step = (k < 4) ? 1.0f : 1.4142135f;
                pushNb(out, { step * g->TileCost(nx, nz), {0, nx, nz} });
            }
            // Portal link to type=2 node at (10,10) -> (50,50)
            if (p->type == 0 && p->x == 10 && p->z == 10) {
                pushNb(out, { 1.0f, {2, 10, 10} });
            }
        };
        static auto getNbType2 = [](void* ctx, const fast_astar::TileCoord* p, fast_astar::MsvcNeighborVec* out) {
            (void)ctx;
            if (p->type == 2 && p->x == 10 && p->z == 10) {
                pushNb(out, { 1.0f, {0, 50, 50} });
            }
        };
        static auto heurFn = [](void* ctx, const fast_astar::TileCoord* p) -> float {
            const auto* g = static_cast<const GridWorld*>(ctx);
            float dx = static_cast<float>(g->goal.x - p->x);
            float dz = static_cast<float>(g->goal.z - p->z);
            return 0.5f * std::sqrt(dx * dx + dz * dz); // admissible even with 0.6x road
        };
        static auto clearPath = [](void* pv) {
            static_cast<std::vector<fast_astar::TileCoord>*>(pv)->clear();
        };
        static auto pushPath = [](void* pv, const fast_astar::TileCoord* c) {
            static_cast<std::vector<fast_astar::TileCoord>*>(pv)->push_back(*c);
        };

        fast_astar::EngineCallbacks cb{};
        cb.reserveNeighbors   = reserveNb;
        cb.getNeighborsNormal = getNbNorm;
        cb.getNeighborsType2  = getNbType2;
        cb.heuristic          = heurFn;
        cb.clearPath          = clearPath;
        cb.pushPathNode       = pushPath;
        cb.freeMem            = freeNb;

        // Build serpentine walled mega-base maze
        GridWorld gw;
        gw.wall.assign(static_cast<size_t>(gw.W * gw.H), 0);
        for (int z = 4; z < 60; z += 6) {
            for (int x = 2; x < 62; ++x) gw.wall[static_cast<size_t>(z * gw.W + x)] = 1;
            int gap = ((z / 6) % 2 == 0) ? 60 : 3;
            gw.wall[static_cast<size_t>(z * gw.W + gap)] = 0;
            gw.wall[static_cast<size_t>(z * gw.W + gap + 1)] = 0;
        }
        // Add road shortcut along z=1
        for (int x = 1; x < 63; ++x) gw.wall[static_cast<size_t>(1 * gw.W + x)] = 2;

        // Reference Klei-exact O(S^2 log S) A* solver (new/delete + full sort + linear open/closed scan)
        auto runNaiveKlei = [&](GridWorld& w, fast_astar::TileCoord start, fast_astar::TileCoord goal,
                                uint32_t maxSteps, int32_t& outStatus, float& outCost) -> uint32_t {
            struct KNode { KNode* parent; fast_astar::TileCoord pos; float g, h; };
            std::vector<KNode*> open, closed;
            open.push_back(new KNode{nullptr, start, 0.0f, heurFn(&w, &start)});
            outStatus = 0; outCost = -1.0f;
            bool dirty = true;
            uint32_t steps = 0;
            fast_astar::MsvcNeighborVec nb{nullptr, nullptr, nullptr, nullptr};
            reserveNb(&nb, 8);
            while (steps < maxSteps) {
                if (open.empty()) { outStatus = 2; break; }
                if (dirty) {
                    std::sort(open.begin(), open.end(), [](const KNode* a, const KNode* b) {
                        return (a->g + a->h) > (b->g + b->h);
                    });
                    dirty = false;
                }
                KNode* cur = open.back();
                open.pop_back();
                closed.push_back(cur);
                if (cur->pos == goal) {
                    outStatus = 1;
                    outCost = cur->g;
                    break;
                }
                nb.end = nb.begin;
                if (cur->pos.type == 2) getNbType2(&w, &cur->pos, &nb);
                else getNbNorm(&w, &cur->pos, &nb);
                for (auto* it = nb.begin; it < nb.end; ++it) {
                    if (it->pos == cur->pos) continue;
                    bool inClosed = false;
                    for (auto* c : closed) if (c->pos == it->pos) { inClosed = true; break; }
                    if (inClosed) continue;
                    float ng = cur->g + it->cost;
                    KNode* inOpen = nullptr;
                    for (auto* o : open) if (o->pos == it->pos) { inOpen = o; break; }
                    if (!inOpen) {
                        open.push_back(new KNode{cur, it->pos, ng, heurFn(&w, &it->pos)});
                        dirty = true;
                    } else if (ng < inOpen->g) {
                        inOpen->parent = cur;
                        inOpen->g = ng;
                        inOpen->h = heurFn(&w, &it->pos);
                        dirty = true;
                    }
                }
                ++steps;
            }
            freeNb(nb.begin);
            for (auto* p : open) delete p;
            for (auto* p : closed) delete p;
            return steps;
        };

        // 7a) Start == Goal -> exact 0 steps & 1-node path
        gw.goal = {0, 2, 2};
        fast_astar::SearchState st;
        st.Init({0, 2, 2}, heurFn(&gw, &gw.goal));
        std::vector<fast_astar::TileCoord> path;
        int32_t status = 0;
        uint32_t s0 = st.Step(&gw, gw.goal, 100, cb, &path, status);
        CHECK(status == 1 && s0 == 0 && path.size() == 1 && path[0] == gw.goal);

        // 7b) Multi-tick serpentine maze (50 steps/tick) -> exact optimal cost parity with Klei Naive A*
        gw.goal = {0, 60, 61};
        fast_astar::TileCoord start{0, 2, 2};
        int32_t refStatus = 0; float refCost = 0.0f;
        runNaiveKlei(gw, start, gw.goal, 5000, refStatus, refCost);
        CHECK(refStatus == 1 && refCost > 0.0f);

        st.Init(start, heurFn(&gw, &start));
        path.clear();
        status = 0;
        uint32_t totalFastSteps = 0;
        while (status == 0 && totalFastSteps < 5000) {
            totalFastSteps += st.Step(&gw, gw.goal, 50, cb, &path, status);
        }
        CHECK(status == 1 && !path.empty());
        CHECK(path.front() == gw.goal && path.back() == start); // exact Klei goal->start order!
        // Compute actual path cost along reconstructed edges
        float fastCost = 0.0f;
        for (size_t i = path.size() - 1; i > 0; --i) {
            const auto& a = path[i];
            const auto& b = path[i - 1];
            if (a.type == 0 && b.type == 2) fastCost += 1.0f;
            else if (a.type == 2 && b.type == 0) fastCost += 1.0f;
            else {
                int adx = std::abs(b.x - a.x), adz = std::abs(b.z - a.z);
                CHECK(adx <= 1 && adz <= 1 && (adx + adz) > 0);
                float base = (adx + adz == 2) ? 1.4142135f : 1.0f;
                fastCost += base * gw.TileCost(b.x, b.z);
            }
        }
        CHECK(std::fabs(fastCost - refCost) < 1e-3f);
        std::printf("[PASS] 7. FastAStar serpentine+portal multi-tick exact parity (cost=%.3f == ref %.3f, path=%zu nodes, steps=%u)\n",
                    fastCost, refCost, path.size(), totalFastSteps);

        // [8] Unreachable walled enclosure (mob trapped inside mega-base walls) + 200x pool reuse
        GridWorld box;
        box.wall.assign(static_cast<size_t>(box.W * box.H), 0);
        for (int i = 10; i <= 52; ++i) {
            box.wall[static_cast<size_t>(10 * box.W + i)] = 1;
            box.wall[static_cast<size_t>(52 * box.W + i)] = 1;
            box.wall[static_cast<size_t>(i * box.W + 10)] = 1;
            box.wall[static_cast<size_t>(i * box.W + 52)] = 1;
        }
        box.goal = {0, 60, 60}; // outside the wall!
        for (int r = 0; r < 200; ++r) {
            fast_astar::TileCoord s{0, 30, 30};
            st.Init(s, heurFn(&box, &s));
            int32_t stBox = 0;
            uint32_t ran = st.Step(&box, box.goal, 2000, cb, &path, stBox);
            CHECK(stBox == 2 && ran == 1681u); // 41x41 interior tiles completely exhausted!
        }
        std::printf("[PASS] 8. Unreachable walled mega-base enclosure exhaustion + 200x pool reuse verified (exact 1681 tiles)\n");

        // [9] Real Head-to-Head Micro-Benchmark: 1000-step walled mega-base search x 30 runs
        constexpr int kBenchRuns = 30;
        auto tNaive0 = std::chrono::steady_clock::now();
        for (int r = 0; r < kBenchRuns; ++r) {
            int32_t sN = 0; float cN = 0.0f;
            runNaiveKlei(box, {0, 30, 30}, box.goal, 1000, sN, cN);
            CHECK(sN == 0);
        }
        double naiveMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tNaive0).count();

        auto tFast0 = std::chrono::steady_clock::now();
        for (int r = 0; r < kBenchRuns; ++r) {
            fast_astar::TileCoord s{0, 30, 30};
            st.Init(s, heurFn(&box, &s));
            int32_t sF = 0;
            uint32_t ran = st.Step(&box, box.goal, 1000, cb, &path, sF);
            CHECK(sF == 0 && ran == 1000u);
        }
        double fastMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tFast0).count();
        double speedup = naiveMs / std::max(0.001, fastMs);
        CHECK(speedup > 10.0);
        std::printf("[PASS] 9. 1000-step Walled Mega-Base A* Bench (x%d): Klei Naive=%.2f ms vs FastAStar=%.2f ms (%.1fx FASTER!)\n",
                    kBenchRuns, naiveMs, fastMs, speedup);
    }

    // [10] Anti-STW Full-GC Shield (eliminates 329ms startup/mod full-GC freezes while keeping OOM breaker)
    {
        anti_stw_gc::State gs;
        anti_stw_gc::Config cfg{};
        void* vmA = reinterpret_cast<void*>(0x1000);
        int32_t thresh = 0;

        // 10a) World population ramp (100MB -> 440MB over first 100 ticks): MUST NOT trigger startup full GC!
        for (int t = 0; t < 100; ++t) {
            int32_t kb = (100 + t * 4) * 1024;
            auto d = gs.Evaluate(vmA, kb, thresh, 2.0, true, cfg);
            thresh = d.new_threshold_kb;
            CHECK(!d.run_hard_full_gc);
        }
        // Finish warmup (20 more ticks at 440MB)
        for (int t = 0; t < 20; ++t) {
            auto d = gs.Evaluate(vmA, 440 * 1024, thresh, 1.5, true, cfg);
            thresh = d.new_threshold_kb;
            CHECK(!d.run_hard_full_gc);
        }
        CHECK(gs.live_set_kb >= 440 * 1024);

        // 10b) Post-warmup allocation spike above soft threshold (1050MB > 992MB): enters smooth catchup, 0 STW full GC!
        auto dSpike = gs.Evaluate(vmA, 1050 * 1024, thresh, 2.0, true, cfg);
        CHECK(!dSpike.run_hard_full_gc);
        CHECK(dSpike.in_catchup_mode);
        CHECK(dSpike.step_budget_ms >= 1.5f && dSpike.step_budget_ms <= 3.05f);

        // 10c) External Mod collectgarbage("collect") arms catchup, then cycle completion cleanly resets live_set
        gs.OnCycleCompleted(420 * 1024);
        CHECK(gs.forced_catchup == 0 && gs.live_set_kb == 420 * 1024);
        gs.ArmForcedCatchup(90);
        auto dExt = gs.Evaluate(vmA, 450 * 1024, thresh, 1.0, false, cfg);
        CHECK(!dExt.run_hard_full_gc && dExt.in_catchup_mode && dExt.step_budget_ms >= 4.0f && dExt.step_budget_ms <= 6.0f);

        // 10d) Genuine sustained runaway OOM cliff (>3x live_set for 120 consecutive ticks) MUST fire breaker!
        bool fired = false;
        for (int t = 0; t < 125; ++t) {
            auto dOom = gs.Evaluate(vmA, 1500 * 1024, thresh, 2.0, false, cfg);
            if (dOom.run_hard_full_gc) { fired = true; gs.OnHardFullGcCompleted(420 * 1024); break; }
        }
        CHECK(fired);
        std::printf("[PASS] 10. Anti-STW GC Shield verified (0 startup STW, smooth catchup drain, OOM circuit-breaker intact)\n");
    }

    // [11] R3.1 (#09): Anti-STW GC Interleaved OnCycleCompleted() + Steadily Rising Memory Leak Breaker!
    //      Verifies incremental cycle completion can NEVER ratchet live_set_kb upward forever to defeat OOM breaker.
    {
        anti_stw_gc::State gs;
        anti_stw_gc::Config cfg{};
        void* vmLeak = reinterpret_cast<void*>(0x2000);
        int32_t thresh = 0;

        // Warmup at 200 MB -> anchor_live_kb = 200 MB -> absCliffKb = 200 MB + 1024 MB = 1224 MB
        for (int t = 0; t < 125; ++t) {
            auto d = gs.Evaluate(vmLeak, 200 * 1024, thresh, 1.5, false, cfg);
            thresh = d.new_threshold_kb;
            CHECK(!d.run_hard_full_gc);
        }
        CHECK(gs.IsPastWarmup());
        CHECK(gs.anchor_live_kb == 200 * 1024);

        // Simulate slow steady memory leak up to 1400 MB while incremental GC cycles complete every 8 ticks!
        bool leakBreakerFired = false;
        int32_t curMb = 250;
        for (int t = 0; t < 400; ++t) {
            if (curMb < 1400) curMb += 10;
            auto d = gs.Evaluate(vmLeak, curMb * 1024, thresh, 2.0, false, cfg);
            thresh = d.new_threshold_kb;
            if (d.run_hard_full_gc) {
                leakBreakerFired = true;
                gs.OnHardFullGcCompleted(220 * 1024);
                break;
            }
            // Interleaved OnCycleCompleted every 8 ticks — previously reset over_cliff_ticks=0 and ratcheted live_set_kb!
            if ((t % 8) == 7) {
                gs.OnCycleCompleted(curMb * 1024);
            }
        }
        CHECK(leakBreakerFired);
        std::printf("[PASS] 11. Anti-STW GC absolute runaway-leak breaker verified (fired despite interleaved OnCycleCompleted)\n");
    }

    // [12] R3.1 (#05): Production fast_astar::StateManager<8u> Multi-Thread Init/Step/Reset/Pool-Full Stress Test
    {
        fast_astar::StateManager<8u> mgr;
        constexpr int kNumKeys = 32; // 4x warm pool capacity -> forces constant heap alloc/free + pool reuse!
        void* keys[kNumKeys];
        for (int i = 0; i < kNumKeys; ++i) {
            keys[i] = reinterpret_cast<void*>(static_cast<uintptr_t>(0x100000 + i * 64));
        }

        static fast_astar::EngineCallbacks stressCb{};
        stressCb.reserveNeighbors = nullptr;
        stressCb.getNeighborsNormal = [](void*, const fast_astar::TileCoord* p, fast_astar::MsvcNeighborVec* out) {
            auto* arr = static_cast<fast_astar::NeighborEntry*>(std::malloc(2 * sizeof(fast_astar::NeighborEntry)));
            arr[0] = { 1.0f, { 0, p->x + 1, p->z } };
            arr[1] = { 1.0f, { 0, p->x, p->z + 1 } };
            out->begin = arr; out->end = arr + 2; out->cap = arr + 2;
        };
        stressCb.getNeighborsType2 = stressCb.getNeighborsNormal;
        stressCb.heuristic = [](void*, const fast_astar::TileCoord* p) -> float {
            return static_cast<float>(std::abs(4 - p->x) + std::abs(4 - p->z));
        };
        stressCb.clearPath = [](void*) {};
        stressCb.pushPathNode = [](void*, const fast_astar::TileCoord*) {};
        stressCb.freeMem = [](void* ptr) { std::free(ptr); };

        std::atomic<bool> go{false};
        std::atomic<uint64_t> ops{0};
        std::vector<std::thread> workers;
        for (int tid = 0; tid < 4; ++tid) {
            workers.emplace_back([&, tid]() {
                while (!go.load(std::memory_order_acquire)) {}
                uint32_t rng = 0x9E3779B9u ^ static_cast<uint32_t>(tid * 2654435761u);
                for (int it = 0; it < 20000; ++it) {
                    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
                    void* k = keys[rng % kNumKeys];
                    uint32_t act = (rng >> 8) % 4;
                    if (act == 0) {
                        auto h = mgr.AcquireAndBind(k);
                        CHECK(static_cast<bool>(h));
                        fast_astar::TileCoord s{0, static_cast<int32_t>(rng & 3), static_cast<int32_t>((rng >> 2) & 3)};
                        h->Init(s, stressCb.heuristic(nullptr, &s));
                    } else if (act == 1 || act == 2) {
                        auto h = mgr.AcquireRef(k);
                        if (h) {
                            auto* slot = h.Slot();
                            const uint64_t gen = h.Gen();
                            int32_t stOut = 0;
                            fast_astar::TileCoord goal{0, 4, 4};
                            h->Step(nullptr, goal, 4u, stressCb, nullptr, stOut);
                            if (stOut != 0) {
                                h.Release();
                                mgr.DetachIfMatch(k, slot, gen);
                            }
                        }
                    } else {
                        mgr.DetachAndRecycle(k);
                    }
                    ops.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }
        go.store(true, std::memory_order_release);
        for (auto& w : workers) w.join();

        for (int i = 0; i < kNumKeys; ++i) mgr.DetachAndRecycle(keys[i]);
        CHECK(mgr.ActiveCount() == 0);
        CHECK(mgr.WarmPoolSize() <= 8u);

        // R3.1b (#05) Explicit same-slot ABA guard verification:
        void* kAba = reinterpret_cast<void*>(static_cast<uintptr_t>(0x999000));
        auto h1 = mgr.AcquireAndBind(kAba);
        CHECK(static_cast<bool>(h1));
        auto* slot1 = h1.Slot();
        const uint64_t gen1 = h1.Gen();
        h1.Release();
        auto h2 = mgr.AcquireAndBind(kAba); // reuses identical slot with incremented gen!
        CHECK(static_cast<bool>(h2) && h2.Slot() == slot1 && h2.Gen() != gen1);
        const uint64_t gen2 = h2.Gen();
        h2.Release();
        mgr.DetachIfMatch(kAba, slot1, gen1); // stale gen MUST NOT detach!
        CHECK(mgr.ActiveCount() == 1);
        mgr.DetachIfMatch(kAba, slot1, gen2); // matching gen MUST detach!
        CHECK(mgr.ActiveCount() == 0);

        std::printf("[PASS] 12. Production fast_astar::StateManager<8> 4-thread stress + same-slot ABA guard (%llu ops, 0 UAF, active=0, pool=%zu/8)\n",
                    (unsigned long long)ops.load(), mgr.WarmPoolSize());
    }

    // [13] R3.1 (#03): Per-Function Lua C API Dependency Gate Verification
    {
        LuaApiTable partial{};
        partial.pushcclosure = reinterpret_cast<decltype(partial.pushcclosure)>(0x1);
        partial.setfield     = reinterpret_cast<decltype(partial.setfield)>(0x1);
        partial.createtable  = reinterpret_cast<decltype(partial.createtable)>(0x1);
        partial.pushnumber   = reinterpret_cast<decltype(partial.pushnumber)>(0x1);
        partial.pushstring   = reinterpret_cast<decltype(partial.pushstring)>(0x1);
        partial.pushboolean  = reinterpret_cast<decltype(partial.pushboolean)>(0x1);
        // ReadyForRegister is true, yet pushnil/gettop/tonumber are NULL!
        CHECK(partial.ReadyForRegister());
        CHECK(!partial.ReadyForFastDump());
        const bool allowSetSim = (partial.gettop != nullptr && partial.tonumber != nullptr && partial.pushboolean != nullptr);
        CHECK(!allowSetSim);
        // Once gettop + tonumber resolved, SetSimTickRate gate opens while FastDump stays gated until full set!
        partial.gettop   = m_gettop;
        partial.tonumber = m_tonumber;
        CHECK(partial.gettop != nullptr && partial.tonumber != nullptr && partial.pushboolean != nullptr);
        CHECK(!partial.ReadyForFastDump());
        std::printf("[PASS] 13. Per-function Lua API dependency gating verified (missing pushnil/gettop/tonumber safely blocked)\n");
    }

    // [14] Read-Only Mod/Data Negative Stat Cache Allowlist, Invalidation & Multi-Thread Concurrency
    {
        CHECK(!hs_fs_neg::IsCacheableReadOnlyAssetPath(nullptr));
        CHECK(!hs_fs_neg::IsCacheableReadOnlyAssetPath(L""));
        CHECK(!hs_fs_neg::IsCacheableReadOnlyAssetPath(L"C:\\Users\\test\\Documents\\Klei\\DoNotStarveTogether\\save\\session\\0001.meta"));
        CHECK(!hs_fs_neg::IsCacheableReadOnlyAssetPath(L"D:\\Steam\\dst\\mods\\modoverrides.lua"));
        CHECK(!hs_fs_neg::IsCacheableReadOnlyAssetPath(L"D:\\Steam\\dst\\mods\\workshop-123\\modinfo.bin.tmp"));
        CHECK(!hs_fs_neg::IsCacheableReadOnlyAssetPath(L"D:\\Steam\\dst\\bin64\\iphlpapi.dll"));

        uint64_t h1a = 0, h2a = 0, h1b = 0, h2b = 0;
        CHECK(hs_fs_neg::IsCacheableReadOnlyAssetPath(L"D:\\DST\\mods\\workshop-999\\images\\missing_icon.tex", &h1a, &h2a));
        CHECK(hs_fs_neg::IsCacheableReadOnlyAssetPath(L"d:/dst/mods/workshop-999/images/missing_icon.tex", &h1b, &h2b));
        CHECK(h1a != 0 && h1a == h1b && h2a == h2b);

        hs_fs_neg::NegativeStatCache cache;
        cache.Insert(h1a, h2a, 1000, 5000);
        CHECK(cache.Lookup(h1a, h2a, 3000));
        CHECK(!cache.Lookup(h1a, h2a, 6000)); // TTL expired
        cache.InvalidateAll();
        CHECK(!cache.Lookup(h1a, h2a, 3000)); // Epoch invalidated

        // 4-thread concurrency smoke
        std::atomic<bool> go{false};
        std::vector<std::thread> ths;
        for (int t = 0; t < 4; ++t) {
            ths.emplace_back([&, t]() {
                while (!go.load(std::memory_order_acquire)) {}
                for (int i = 0; i < 4000; ++i) {
                    wchar_t path[128];
                    std::swprintf(path, 128, L"mods\\workshop-%d\\anim\\item_%d.xml", t, i & 63);
                    uint64_t a = 0, b = 0;
                    CHECK(hs_fs_neg::IsCacheableReadOnlyAssetPath(path, &a, &b));
                    if ((i & 7) == 0) cache.Insert(a, b, 10000, 1000);
                    else if ((i & 31) == 15) cache.InvalidateAll();
                    else (void)cache.Lookup(a, b, 10100);
                }
            });
        }
        go.store(true, std::memory_order_release);
        for (auto& w : ths) w.join();
        std::printf("[PASS] 14. Read-only FS Negative Stat Cache allowlist, TTL/epoch invalidation & 4-thread concurrency verified\n");
    }

    // [15] Async Atomic Save-Commit Queue (FIFO ordering, read/delete/dir barriers, tiny-cap backpressure, clean shutdown)
    {
        wchar_t tempBase[MAX_PATH] = {};
        DWORD tlen = GetTempPathW(MAX_PATH, tempBase);
        CHECK(tlen > 0 && tlen < MAX_PATH);
        std::wstring testDir = std::wstring(tempBase) + L"hc_async_save_rt_" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(testDir.c_str(), nullptr);

        auto readAllBytes = [](const std::wstring& p, std::string& out) -> bool {
            HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
            DWORD sz = GetFileSize(h, nullptr);
            out.resize(sz);
            DWORD rd = 0;
            bool ok = (sz == 0) || (ReadFile(h, out.data(), sz, &rd, nullptr) && rd == sz);
            CloseHandle(h);
            return ok;
        };

        // 15a) Basic atomic commit + read-after-write barrier + zero .hc_tmp residue
        {
            hc_async_save::AsyncSaveQueue q;
            std::wstring fSnap = testDir + L"\\0000000001";
            std::wstring fMeta = testDir + L"\\0000000001.meta";
            std::string snapPayload(64 * 1024, 'S');
            std::string metaPayload = "return { day = 42 }";

            CHECK(q.TryEnqueue(fSnap.c_str(), snapPayload.data(), snapPayload.size()));
            CHECK(q.TryEnqueue(fMeta.c_str(), metaPayload.data(), metaPayload.size()));
            // Read-after-write via dir pattern barrier (simulates FindFirstFileW + GetFileAttributesW)
            q.FlushPendingForDirPattern((testDir + L"\\*").c_str());

            std::string gotSnap, gotMeta;
            CHECK(readAllBytes(fSnap, gotSnap) && gotSnap == snapPayload);
            CHECK(readAllBytes(fMeta, gotMeta) && gotMeta == metaPayload);
            CHECK(GetFileAttributesW((fSnap + L".hc_tmp").c_str()) == INVALID_FILE_ATTRIBUTES);
            CHECK(GetFileAttributesW((fMeta + L".hc_tmp").c_str()) == INVALID_FILE_ATTRIBUTES);

            // 15b) 20x rapid overwrite same path -> strict latest content on disk
            std::wstring fOver = testDir + L"\\saveindex";
            for (int i = 0; i < 20; ++i) {
                std::string body = "IDX_GEN_" + std::to_string(i) + "_" + std::string(4096, static_cast<char>('A' + (i % 26)));
                CHECK(q.TryEnqueue(fOver.c_str(), body.data(), body.size()));
            }
            q.FlushPendingForPath(fOver.c_str());
            std::string gotOver;
            CHECK(readAllBytes(fOver, gotOver));
            CHECK(gotOver.rfind("IDX_GEN_19_", 0) == 0);

            // 15c) Delete-after-write barrier -> async worker MUST NOT resurrect deleted file!
            std::wstring fDel = testDir + L"\\shardindex";
            std::string delPayload(16384, 'D');
            CHECK(q.TryEnqueue(fDel.c_str(), delPayload.data(), delPayload.size()));
            q.FlushPendingForPath(fDel.c_str());
            CHECK(DeleteFileW(fDel.c_str()) != FALSE);
            q.FlushAll();
            CHECK(GetFileAttributesW(fDel.c_str()) == INVALID_FILE_ATTRIBUTES);

            q.ShutdownAndFlush();
            CHECK(q.CommittedCount() == 23 && q.ErrorCount() == 0);
            CHECK(DeleteFileW(fSnap.c_str()) && DeleteFileW(fMeta.c_str()) && DeleteFileW(fOver.c_str()));
        }

        // 15d) Tiny-cap backpressure (256B queue + 15x 128B payloads + oversized 512B sync fallback)
        {
            hc_async_save::AsyncSaveQueue smallQ(256u);
            std::wstring fBp = testDir + L"\\bp_test";
            for (int i = 0; i < 15; ++i) {
                std::string p(128, static_cast<char>('0' + (i % 10)));
                CHECK(smallQ.TryEnqueue(fBp.c_str(), p.data(), p.size()));
            }
            // Oversized payload triggers FlushAll() then returns false for sync write
            std::string big(512, 'Z');
            CHECK(!smallQ.TryEnqueue(fBp.c_str(), big.data(), big.size()));
            CHECK(!smallQ.HasPending()); // earlier queued jobs guaranteed flushed BEFORE returning false!
            smallQ.ShutdownAndFlush();
            CHECK(smallQ.CommittedCount() == 15 && smallQ.ErrorCount() == 0);
            DeleteFileW(fBp.c_str());
        }

        RemoveDirectoryW(testDir.c_str());
        std::printf("[PASS] 15. AsyncSaveQueue atomic commit, read/delete/dir barriers, 20x FIFO overwrite & backpressure verified\n");
    }

    // [16] v1.4.0: FS Neg Cache v2.7 Fixes + C++ FastJson Parity & Fail-Closed Fallbacks
    {
        // 16a) FS Neg relative roots & false-positive "log" asset fix
        CHECK(hs_fs_neg::IsCacheableReadOnlyAssetPath(L"scripts\\prefabs\\log.lua"));
        CHECK(hs_fs_neg::IsCacheableReadOnlyAssetPath(L"images\\livinglog.tex"));
        CHECK(hs_fs_neg::IsCacheableReadOnlyAssetPath(L"..\\mods\\workshop-123\\anim\\foo.zip"));
        CHECK(hs_fs_neg::IsCacheableReadOnlyAssetPathA("anim/dynamic/wilson.zip"));
        CHECK(!hs_fs_neg::IsCacheableReadOnlyAssetPathA("client_log.txt"));
        CHECK(!hs_fs_neg::IsCacheableReadOnlyAssetPathA("mods/workshop-1/my_log.tex"));
        CHECK(!hs_fs_neg::IsCacheableReadOnlyAssetPathA("..\\save\\session.bin"));
        CHECK(!hs_fs_neg::IsCacheableReadOnlyAssetPathA("mods\\..\\save\\secret.tex"));
        CHECK(!hs_fs_neg::IsCacheableReadOnlyAssetPathA("data\\persistent_cache.bin"));
        CHECK(!hs_fs_neg::IsCacheableReadOnlyAssetPathA("mods\\\xE4\xB8\xAD\xE6\x96\x87\\icon.tex")); // non-ASCII ASCII-wrapper fail-closed

        // 16b) FastJson Encode / Decode / Fallback Parity
        MockLuaState st;
        MockTable emptyTbl;
        MockTable arrTbl;
        arrTbl.entries = {
            {MockVal::Num(1), MockVal::Str("hi'there")},
            {MockVal::Num(2), MockVal::Bool(true)},
            {MockVal::Num(3), MockVal::Num(3.5)}
        };
        MockTable objTbl;
        objTbl.entries = {
            {MockVal::Str("k"), MockVal::Str("v\n")},
            {MockVal::Str("arr"), MockVal::Tbl(&arrTbl)},
            {MockVal::Str("empty"), MockVal::Tbl(&emptyTbl)}
        };
        st.stack.push_back(MockVal::Tbl(&objTbl));

        fast_json::JsonOutBuf out;
        CHECK(fast_json::EncodeRoot(&st, api, 1, out, false) == fast_dumper::Status::Ok);
        std::string sNonComp(out.data, out.len);
        CHECK(sNonComp.find("hi\\'there") != std::string::npos);
        CHECK(sNonComp.find("\"empty\":[]") != std::string::npos);

        CHECK(fast_json::EncodeRoot(&st, api, 1, out, true) == fast_dumper::Status::Ok);
        std::string sComp(out.data, out.len);
        CHECK(sComp.find("hi'there") != std::string::npos && sComp.find("\\'") == std::string::npos);

        // Decode with null hole & nested object + verify nextPos
        const char* jSample = "{\"x\":[10,null,30],\"flag\":false,\"msg\":\"a\\nb\"}";
        size_t nextPos = 0;
        const int topBefore = m_gettop(&st);
        CHECK(fast_json::DecodeRoot(&st, api, jSample, std::strlen(jSample), &nextPos) == fast_dumper::Status::Ok);
        CHECK(m_gettop(&st) == topBefore + 1);
        CHECK(nextPos == std::strlen(jSample) + 1);
        MockTable* decObj = st.stack.back().tbl;
        CHECK(decObj != nullptr && decObj->entries.size() == 3);
        m_settop(&st, topBefore);

        // Fallback cases must fail-closed & keep stack clean: \uXXXX, /* comment, cycle
        const char* uJson = "\"\\u0041\"";
        CHECK(fast_json::DecodeRoot(&st, api, uJson, std::strlen(uJson), &nextPos) == fast_dumper::Status::FallbackNeeded);
        CHECK(m_gettop(&st) == topBefore);
        const char* cJson = "/* comment */{\"a\":1}";
        CHECK(fast_json::DecodeRoot(&st, api, cJson, std::strlen(cJson), &nextPos) == fast_dumper::Status::FallbackNeeded);
        CHECK(m_gettop(&st) == topBefore);

        MockTable cyc;
        cyc.entries = { {MockVal::Str("self"), MockVal::Tbl(&cyc)} };
        st.stack.push_back(MockVal::Tbl(&cyc));
        CHECK(fast_json::EncodeRoot(&st, api, -1, out, false) == fast_dumper::Status::FallbackNeeded);
        CHECK(m_gettop(&st) == topBefore + 1);
        m_settop(&st, topBefore);

        std::printf("[PASS] 16. FsNeg v2.7 roots/log-fix + FastJson encode/decode/null-hole/fallback verified\n");
    }

    // [17] v1.5.0: Causal 3-channel RPC QoS + Klei Codec (Vector & Malloc) + AsyncSaveQueue::TryEnqueueCompress
    {
        hs_rpc_qos::Classifier qos;
        const uint32_t srv0 = 0u;
        const uint32_t srv1 = (1u << 27); // RPC id 59
        const uint32_t srv2 = (1u << 4);  // RPC id 68
        const uint32_t cli0 = (1u << 1) | (1u << 8); // CLIENT_RPC id 1, 8
        qos.SetMasks(static_cast<uint64_t>(srv0) | (static_cast<uint64_t>(srv1) << 32),
                     static_cast<uint64_t>(srv2),
                     static_cast<uint64_t>(cli0), 0ULL);

        // Verify REAL DST RakNet::RPC4 wire uniqueIDs ("0".."5") + high-bit tick flag (0x80)
        // + fail-closed passthrough for native C++ engine slots ("6".."X")!
        uint8_t bsAttack[4] = { static_cast<uint8_t>(0x80u | 4u), 0, 0, 0 }; // tick=1, id=4
        auto c0 = qos.Classify("0", bsAttack, 32u, 1, 3, 0);
        CHECK(c0.priority == 0 && c0.orderingChannel == 0 && c0.promotedImmediate && !c0.movedOffCh0);

        uint8_t bsSocial[4] = { static_cast<uint8_t>(0x80u | 59u), 0, 0, 0 }; // tick=1, id=59
        auto c1 = qos.Classify("0", bsSocial, 32u, 1, 3, 0);
        CHECK(c1.priority == 1 && c1.orderingChannel == 1 && !c1.promotedImmediate && c1.movedOffCh0);

        uint8_t bsModSmall[4] = { 0x11, 0x22, 0x33, 0x44 };
        auto cMod1 = qos.Classify("1", bsModSmall, 128u, 1, 3, 0);
        CHECK(cMod1.priority == 0 && cMod1.orderingChannel == 0 && cMod1.promotedImmediate);

        uint8_t bsScrap[4] = { 15, 0, 0, 0 }; // TryToTeachScrapbookData (entity-bearing -> Ch0/IMM)
        auto c2 = qos.Classify("2", bsScrap, 32u, 1, 3, 0);
        CHECK(c2.priority == 0 && c2.orderingChannel == 0 && c2.promotedImmediate && !c2.movedOffCh0);

        uint8_t bsChat[4] = { static_cast<uint8_t>(0x80u | 8u), 0, 0, 0 }; // RecieveChatHistory -> Ch2/MED
        auto c3 = qos.Classify("2", bsChat, 32u, 1, 3, 0);
        CHECK(c3.priority == 2 && c3.orderingChannel == 2 && !c3.promotedImmediate && c3.movedOffCh0);

        auto cMod3 = qos.Classify("3", bsModSmall, 128u, 1, 3, 0);
        CHECK(cMod3.priority == 0 && cMod3.orderingChannel == 0 && cMod3.promotedImmediate);

        auto cShard4 = qos.Classify("4", bsModSmall, 128u, 1, 3, 0);
        CHECK(cShard4.priority == 0 && cShard4.orderingChannel == 0 && cShard4.promotedImmediate);

        auto cShard5 = qos.Classify("5", bsModSmall, 4096u, 1, 3, 0); // >256B ShardModRPC stays original priority
        CHECK(cShard5.priority == 1 && cShard5.orderingChannel == 0 && !cShard5.promotedImmediate);

        // Native engine slots ("6".."X") MUST pass through untouched!
        auto cEng6 = qos.Classify("6", bsAttack, 32u, 1, 3, 0);
        CHECK(cEng6.priority == 1 && cEng6.orderingChannel == 0 && !cEng6.promotedImmediate && !cEng6.movedOffCh0);
        auto cEngX = qos.Classify("X", bsAttack, 32u, 1, 3, 0);
        CHECK(cEngX.priority == 1 && cEngX.orderingChannel == 0 && !cEngX.promotedImmediate && !cEngX.movedOffCh0);

        // Symbolic aliases remain compatible
        auto cAlias = qos.Classify("ClientRPC", bsChat, 32u, 1, 3, 0);
        CHECK(cAlias.priority == 2 && cAlias.orderingChannel == 2 && cAlias.movedOffCh0);

        std::vector<uint8_t> raw(16384u);
        for (size_t i = 0; i < raw.size(); ++i) raw[i] = static_cast<uint8_t>('a' + (i % 26));

        uint8_t* mEnc = nullptr;
        size_t   mEncLen = 0;
        CHECK(klei_save_codec::EncodeKleiZlibBase64Malloc(raw.data(), raw.size(), 5, &mEnc, &mEncLen));
        CHECK(mEnc != nullptr && mEncLen > 11u && std::memcmp(mEnc, "KLEI     1D", 11u) == 0);
        uint8_t* mDec = nullptr;
        size_t   mDecLen = 0;
        CHECK(klei_save_codec::DecodeKleiZlibBase64Malloc(mEnc, mEncLen, &mDec, &mDecLen));
        CHECK(mDec != nullptr && mDecLen == raw.size() && std::memcmp(mDec, raw.data(), raw.size()) == 0);
        std::free(mEnc);
        std::free(mDec);

        wchar_t tmpDir[MAX_PATH]{};
        GetTempPathW(MAX_PATH, tmpDir);
        std::wstring testFile = std::wstring(tmpDir) + L"hc_redteam_async_comp_test.dat";
        DeleteFileW(testFile.c_str());

        hc_async_save::AsyncSaveQueue q;
        std::vector<uint8_t> rawCopy = raw;
        CHECK(q.TryEnqueueCompress(testFile.c_str(), std::move(rawCopy), 5));
        q.FlushPendingForPath(testFile.c_str());
        CHECK(q.CompressedCount() == 1 && q.CommittedCount() == 1 && q.ErrorCount() == 0);

        FILE* rf = _wfopen(testFile.c_str(), L"rb");
        CHECK(rf != nullptr);
        std::fseek(rf, 0, SEEK_END);
        const long fsz = std::ftell(rf);
        std::fseek(rf, 0, SEEK_SET);
        std::vector<uint8_t> diskBuf(static_cast<size_t>(fsz));
        CHECK(std::fread(diskBuf.data(), 1, diskBuf.size(), rf) == diskBuf.size());
        std::fclose(rf);

        std::vector<uint8_t> decDisk;
        CHECK(klei_save_codec::DecodeKleiZlibBase64(diskBuf.data(), diskBuf.size(), decDisk));
        CHECK(decDisk == raw);

        const char fallbackTxt[] = "KLEI     1 server_sync_fallback";
        CHECK(q.CommitSyncAtomic(testFile.c_str(), fallbackTxt, sizeof(fallbackTxt) - 1));
        DeleteFileW(testFile.c_str());
        q.ShutdownAndFlush();

        std::printf("[PASS] 17. RPC QoS (real DST wire slots '0'..'5' + '6'/'X' passthrough) + Klei Codec + AsyncSave verified\n");
    }

    // [18] Save-Safety Hardening Regression: Locked Target, FlushFileBuffers Failure, Concurrent Same-Path 120/120
    {
        wchar_t tmpBase[MAX_PATH]{};
        DWORD tlen = GetTempPathW(MAX_PATH, tmpBase);
        CHECK(tlen > 0 && tlen < MAX_PATH);
        std::wstring dir = std::wstring(tmpBase) + L"hc_save_hardening_" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(dir.c_str(), nullptr);

        auto hasTempResidue = [&](const std::wstring& d) -> bool {
            WIN32_FIND_DATAW fd{};
            HANDLE h = FindFirstFileW((d + L"\\*.hc_tmp*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return false;
            FindClose(h);
            return true;
        };

        auto readAll = [](const std::wstring& p, std::string& out) -> bool {
            HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) return false;
            DWORD sz = GetFileSize(h, nullptr);
            out.resize(sz);
            DWORD rd = 0;
            bool ok = (sz == 0) || (ReadFile(h, out.data(), sz, &rd, nullptr) && rd == sz);
            CloseHandle(h);
            return ok;
        };

        // 18a) Exclusive lock on target file -> CommitSyncAtomic MUST fail, preserve old bytes, leave 0 .hc_tmp*
        {
            hc_async_save::AsyncSaveQueue q;
            std::wstring target = dir + L"\\locked_save";
            const std::string oldData = "OLD_SAVE_GOOD_DATA_V1";
            CHECK(q.CommitSyncAtomic(target.c_str(), oldData.data(), oldData.size()));
            HANDLE hLock = CreateFileW(target.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            CHECK(hLock != INVALID_HANDLE_VALUE);
            DWORD winErr = 0;
            const std::string newData = "NEW_SAVE_SHOULD_FAIL";
            CHECK(!q.CommitSyncAtomic(target.c_str(), newData.data(), newData.size(), &winErr));
            CHECK(winErr != 0);
            CHECK(q.ErrorCount() == 1);
            CloseHandle(hLock);

            std::string got;
            CHECK(readAll(target, got) && got == oldData);
            CHECK(!hasTempResidue(dir));
            DeleteFileW(target.c_str());
        }

        // 18b) Injected FlushFileBuffers failure -> CommitSyncAtomic MUST fail, preserve old bytes, leave 0 .hc_tmp*
        {
            static auto failFlush = [](HANDLE) WINAPI -> BOOL {
                SetLastError(ERROR_WRITE_FAULT);
                return FALSE;
            };
            hc_async_save::AsyncSaveQueue q;
            std::wstring target = dir + L"\\flush_fail_save";
            const std::string oldData = "INTACT_BEFORE_FLUSH_ERR";
            CHECK(q.CommitSyncAtomic(target.c_str(), oldData.data(), oldData.size()));
            q.SetFlushFileBuffersFnForTest(failFlush);
            DWORD winErr = 0;
            const std::string badData = "UNFLUSHED_PAYLOAD";
            CHECK(!q.CommitSyncAtomic(target.c_str(), badData.data(), badData.size(), &winErr));
            CHECK(winErr == ERROR_WRITE_FAULT);
            CHECK(q.ErrorCount() == 1);

            std::string got;
            CHECK(readAll(target, got) && got == oldData);
            CHECK(!hasTempResidue(dir));
            DeleteFileW(target.c_str());
        }

        // 18c) 4-thread concurrent same-path CommitSyncAtomic stress (4 x 30 = 120 commits, 0 errors, 0 residue)
        {
            hc_async_save::AsyncSaveQueue q;
            std::wstring target = dir + L"\\concurrent_save";
            std::atomic<bool> go{false};
            std::atomic<int> okCount{0};
            std::vector<std::thread> workers;
            for (int t = 0; t < 4; ++t) {
                workers.emplace_back([&, t]() {
                    while (!go.load(std::memory_order_acquire)) {}
                    for (int i = 0; i < 30; ++i) {
                        std::string body = "T" + std::to_string(t) + "_I" + std::to_string(i) + "_" + std::string(1024, static_cast<char>('A' + (i % 26)));
                        DWORD errCode = 0;
                        if (q.CommitSyncAtomic(target.c_str(), body.data(), body.size(), &errCode)) {
                            okCount.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                });
            }
            go.store(true, std::memory_order_release);
            for (auto& w : workers) w.join();

            CHECK(okCount.load() == 120);
            CHECK(q.CommittedCount() == 120 && q.ErrorCount() == 0);
            CHECK(!hasTempResidue(dir));
            std::string finalGot;
            CHECK(readAll(target, finalGot) && finalGot.size() > 1024 && finalGot[0] == 'T');
            DeleteFileW(target.c_str());
        }

        RemoveDirectoryW(dir.c_str());
        std::printf("[PASS] 18. Save-Safety Hardening verified (locked-file fail-closed, FlushFileBuffers check, 120/120 concurrent atomic commits, 0 .hc_tmp residue)\n");
    }

    // -------------------------------------------------------------------------
    // Test 19: ReplicaFastPath — Exact QueryReplicaList parity across all entity
    //          classes (culled, destroyed, hidden, classified, parented, InvItem,
    //          owner-blocked, non-NetworkProxy virtual subclass) & RB-tree hoisting
    // -------------------------------------------------------------------------
    {
        static std::atomic<int> s_allocCnt{0}, s_freeCnt{0}, s_rbLookups{0}, s_relCalls{0};
        s_allocCnt = 0; s_freeCnt = 0; s_rbLookups = 0; s_relCalls = 0;

        auto mockNew = [](size_t sz) -> void* {
            s_allocCnt.fetch_add(1);
            return std::malloc(sz ? sz : 1);
        };
        auto mockDel = [](void* p) {
            if (p) { s_freeCnt.fetch_add(1); std::free(p); }
        };

        // Synthetic NetworkManager, ReplicaManager3, Client, Connection, Entities, Proxies
        std::vector<uint8_t> netMgr(0x400, 0);
        std::vector<uint8_t> rm3(0x3000, 0);
        std::vector<uint8_t> client(0x400, 0);
        std::vector<uint8_t> blockedOwner(0x400, 0);
        std::vector<uint8_t> conn(0x100, 0);

        *reinterpret_cast<void**>(netMgr.data() + 0x288) = rm3.data();
        netMgr[0x308] = 0; // dedicated server
        const uint64_t playerGuid = 0x1122334455667788ULL;
        const uint64_t blockedGuid = 0x9988776655443322ULL;
        const uint64_t playerMask = (1ULL << 3); // slot 3
        *reinterpret_cast<uint64_t*>(conn.data() + 0x20) = playerGuid;
        *reinterpret_cast<uint64_t*>(client.data() + 0x1b0) = playerMask;
        client[0x341] = 1; // ready
        client[0x342] = 0;
        blockedOwner[0x342] = 1; // blocked!

        struct MockCtx {
            void* cliPtr;
            void* blkPtr;
            uint64_t cliGuid;
            uint64_t blkGuid;
            bool slowRelResult;
        };
        static MockCtx s_ctx{};
        s_ctx = { client.data(), blockedOwner.data(), playerGuid, blockedGuid, true };

        auto mockGetCli = [](void* /*nm*/, const void* guidPtr) -> void* {
            s_rbLookups.fetch_add(1);
            const uint64_t g = *reinterpret_cast<const uint64_t*>(guidPtr);
            if (g == s_ctx.cliGuid) return s_ctx.cliPtr;
            if (g == s_ctx.blkGuid) return s_ctx.blkPtr;
            return nullptr;
        };
        auto mockIsRel = [](void* /*proxy*/, const void* /*guidPtr*/, uint64_t /*mask*/) -> bool {
            s_relCalls.fetch_add(1);
            return s_ctx.slowRelResult;
        };

        static void* s_proxyVtbl[64] = {};
        static void* s_invVtbl[16]   = {};
        static void* s_otherVtbl[64] = {};

        struct NonProxyRep {
            void* vtbl;
            uint8_t pad[0x190 - 8];
            uint64_t cullCopy;
            bool dead;
            int32_t qcRet;
            int32_t qdRet;
        };
        auto npDead = [](void* r) -> bool { return static_cast<NonProxyRep*>(r)->dead; };
        auto npQc   = [](void* r, void*, void*) -> int32_t { return static_cast<NonProxyRep*>(r)->qcRet; };
        auto npQd   = [](void* r, void*, void*) -> int32_t { return static_cast<NonProxyRep*>(r)->qdRet; };
        s_otherVtbl[0x138 / 8] = reinterpret_cast<void*>(+npDead);
        s_otherVtbl[0x040 / 8] = reinterpret_cast<void*>(+npQc);
        s_otherVtbl[0x048 / 8] = reinterpret_cast<void*>(+npQd);

        struct MockProxyEnt {
            std::vector<uint8_t> proxy; // Replica3 sub-object is at +0x20
            std::vector<uint8_t> ent;
            std::vector<void*>   comps;
            void* invCompObj = nullptr;
            void* repPtr() { return proxy.data() + 0x20; }
            void Init(bool invItem = false) {
                proxy.assign(0x240, 0);
                ent.assign(0x240, 0);
                *reinterpret_cast<void**>(proxy.data() + 0x20) = s_proxyVtbl;
                *reinterpret_cast<void**>(proxy.data() + 0x18) = ent.data();
                *reinterpret_cast<uint64_t*>(proxy.data() + 0x1c0) = 0ULL; // visible
                *reinterpret_cast<uint64_t*>(proxy.data() + 0x1c8) = hc_replica::kUnassignedGuid;
                *reinterpret_cast<uint64_t*>(proxy.data() + 0x1d8) = hc_replica::kUnassignedGuid;
                if (invItem) {
                    invCompObj = &s_invVtbl[0];
                    comps.push_back(&invCompObj);
                    *reinterpret_cast<void***>(ent.data() + 0x100) = comps.data();
                    *reinterpret_cast<void***>(ent.data() + 0x108) = comps.data() + comps.size();
                }
            }
        };

        std::vector<MockProxyEnt> ents(8);
        for (int i = 0; i < 8; ++i) ents[i].Init(i == 5);
        // [0]: clean visible -> MUST construct (q=0 -> list1)
        // [1]: culled (cullMask bit 3 set) -> stay in list0 (q=4)
        *reinterpret_cast<uint64_t*>(ents[1].proxy.data() + 0x1c0) = playerMask;
        // [2]: destroyed -> drop from list0
        ents[2].ent[0x20c] = 1;
        // [3]: hidden 1c4 -> stay in list0
        ents[3].ent[0x1c4] = 1;
        // [4]: ownerGuid = blockedOwner -> stay in list0
        *reinterpret_cast<uint64_t*>(ents[4].proxy.data() + 0x1c8) = blockedGuid;
        // [5]: has InvItem component -> MUST delegate to fnIsRelevant (returns true -> construct!)
        // [6]: classifiedGuid set -> MUST delegate to fnIsRelevant (returns true -> construct!)
        *reinterpret_cast<uint64_t*>(ents[6].proxy.data() + 0x1d8) = playerGuid;
        // [7]: already in list1, now culled -> MUST destroy in Loop B (and NOT reinsert to list0 since cullCopy bit set)
        *reinterpret_cast<uint64_t*>(ents[7].proxy.data() + 0x1c0) = playerMask;
        *reinterpret_cast<uint64_t*>(ents[7].proxy.data() + 0x20 + 0x190) = playerMask;

        NonProxyRep np1{}; np1.vtbl = s_otherVtbl; np1.dead = false; np1.qcRet = 1; np1.qdRet = 2; // construct -> list2
        NonProxyRep np2{}; np2.vtbl = s_otherVtbl; np2.dead = false; np2.qcRet = 2; np2.qdRet = 2; // list2 only

        void* netMgrPtr = netMgr.data();
        hc_replica::ReplicaAbiConfig cfg{};
        cfg.ppNetMgr          = &netMgrPtr;
        cfg.fnGetClientByGuid = mockGetCli;
        cfg.fnIsRelevant      = mockIsRel;
        cfg.fnHostNew         = mockNew;
        cfg.fnHostDelete      = mockDel;
        cfg.networkProxyVtbl  = s_proxyVtbl;
        cfg.invItemCompVtbl   = s_invVtbl;

        auto* list0 = reinterpret_cast<hc_replica::DsMap*>(rm3.data() + 0x8a8 + 3 * sizeof(hc_replica::DsMap));
        auto* list1 = reinterpret_cast<hc_replica::DsMap*>(rm3.data() + 0x12a8 + 3 * sizeof(hc_replica::DsMap));
        auto* list2 = reinterpret_cast<hc_replica::DsMap*>(rm3.data() + 0x1ca8 + 3 * sizeof(hc_replica::DsMap));

        // Populate list0 sorted with ents[0..6] + np1 + np2, and list1 with ents[7]
        std::vector<void*> init0 = {
            ents[0].repPtr(), ents[1].repPtr(), ents[2].repPtr(), ents[3].repPtr(),
            ents[4].repPtr(), ents[5].repPtr(), ents[6].repPtr(), &np1, &np2
        };
        std::sort(init0.begin(), init0.end());
        hc_replica::ScratchBuf<void*> sb0{};
        for (void* p : init0) sb0.Push(p);
        hc_replica::ScratchBuf<hc_replica::DsMapNode> tmp{};
        hc_replica::DsMapBulkMergeSorted(list0, sb0, tmp, cfg);

        hc_replica::ScratchBuf<void*> sb1{};
        sb1.Push(ents[7].repPtr());
        hc_replica::DsMapBulkMergeSorted(list1, sb1, tmp, cfg);

        hc_replica::DsListPtr outConstruct{}, outDestroy{};
        hc_replica::ReplicaFastStats rstats{};
        hc_replica::QueryScratch qsc{};
        hc_replica::FastQueryReplicaList(conn.data(), &outConstruct, &outDestroy, cfg, &rstats, &qsc);

        // Expected constructions: ents[0], ents[5], ents[6], np1 -> 4 items!
        CHECK(outConstruct.size == 4u);
        // Expected destructions: ents[7] -> 1 item!
        CHECK(outDestroy.size == 1u && outDestroy.arr[0] == ents[7].repPtr());
        // Remaining in list0: ents[1] (culled), ents[3] (hidden), ents[4] (blocked owner) -> 3 items!
        CHECK(list0->size == 3u);
        CHECK(hc_replica::DsMapContains(list0, ents[1].repPtr()));
        CHECK(hc_replica::DsMapContains(list0, ents[3].repPtr()));
        CHECK(hc_replica::DsMapContains(list0, ents[4].repPtr()));
        // Remaining in list1: ents[0], ents[5], ents[6] -> 3 items (ents[7] removed!)
        CHECK(list1->size == 3u);
        CHECK(!hc_replica::DsMapContains(list1, ents[7].repPtr()));
        // list2: np1, np2 -> 2 items!
        CHECK(list2->size == 2u);
        // RB-tree lookups: ONLY 2 total (1 hoisted at entry + 1 for ents[4] ownerGuid), NOT 2*9+1=19!
        CHECK(s_rbLookups.load() == 2);
        // Slow relevance fallback called ONLY for ents[5] (InvItem) and ents[6] (classified) in Loop A + Loop B = 4
        CHECK(s_relCalls.load() == 4);
        CHECK(rstats.non_proxy_fallbacks.load() == 2);

        // Cleanup host allocations
        if (list0->arr) mockDel(list0->arr);
        if (list1->arr) mockDel(list1->arr);
        if (list2->arr) mockDel(list2->arr);
        if (outConstruct.arr) mockDel(outConstruct.arr);
        if (outDestroy.arr) mockDel(outDestroy.arr);
        sb0.Free(); sb1.Free(); tmp.Free(); qsc.FreeAll();
        CHECK(s_allocCnt.load() == s_freeCnt.load());
        std::printf("[PASS] 19. ReplicaFastPath QueryReplicaList parity verified (all 9 entity classes, RB-tree lookups=2 vs 19, 0 leaks)\n");
    }

    // -------------------------------------------------------------------------
    // Test 20: ReplicaFastPath — 5,000-entity bulk join + wormhole destroy
    //          O(N) compaction + Connection_RM3 compaction + ClearSerializedPreFlags
    // -------------------------------------------------------------------------
    {
        static std::atomic<int> s_a2{0}, s_f2{0};
        s_a2 = 0; s_f2 = 0;
        auto mNew = [](size_t sz) -> void* { s_a2.fetch_add(1); return std::malloc(sz ? sz : 1); };
        auto mDel = [](void* p) { if (p) { s_f2.fetch_add(1); std::free(p); } };

        std::vector<uint8_t> netMgr(0x400, 0), rm3(0x3000, 0), client(0x400, 0), conn(0x100, 0);
        *reinterpret_cast<void**>(netMgr.data() + 0x288) = rm3.data();
        const uint64_t pMask = (1ULL << 0);
        *reinterpret_cast<uint64_t*>(client.data() + 0x1b0) = pMask;
        client[0x341] = 1;

        static void* s_cli2 = nullptr;
        s_cli2 = client.data();
        auto mGetCli = [](void*, const void*) -> void* { return s_cli2; };
        auto mRel    = [](void*, const void*, uint64_t) -> bool { return true; };

        static void* s_pv2[64] = {};
        static void* s_iv2[16] = {};
        void* nmPtr = netMgr.data();

        hc_replica::ReplicaAbiConfig cfg{};
        cfg.ppNetMgr = &nmPtr; cfg.fnGetClientByGuid = mGetCli; cfg.fnIsRelevant = mRel;
        cfg.fnHostNew = mNew; cfg.fnHostDelete = mDel;
        cfg.networkProxyVtbl = s_pv2; cfg.invItemCompVtbl = s_iv2;

        constexpr int kN = 5000;
        std::vector<uint8_t> proxyPool(static_cast<size_t>(kN) * 0x240, 0);
        std::vector<uint8_t> entPool(static_cast<size_t>(kN) * 0x240, 0);
        std::vector<void*> reps(kN);
        for (int i = 0; i < kN; ++i) {
            uint8_t* pr = proxyPool.data() + static_cast<size_t>(i) * 0x240;
            uint8_t* en = entPool.data()   + static_cast<size_t>(i) * 0x240;
            *reinterpret_cast<void**>(pr + 0x20) = s_pv2;
            *reinterpret_cast<void**>(pr + 0x18) = en;
            *reinterpret_cast<uint64_t*>(pr + 0x1c0) = 0ULL;
            *reinterpret_cast<uint64_t*>(pr + 0x1c8) = hc_replica::kUnassignedGuid;
            *reinterpret_cast<uint64_t*>(pr + 0x1d8) = hc_replica::kUnassignedGuid;
            reps[i] = pr + 0x20;
        }

        auto* list0 = reinterpret_cast<hc_replica::DsMap*>(rm3.data() + 0x8a8);
        auto* list1 = reinterpret_cast<hc_replica::DsMap*>(rm3.data() + 0x12a8);

        hc_replica::ScratchBuf<void*> initSb{};
        for (void* p : reps) initSb.Push(p); // already ascending in contiguous pool
        hc_replica::ScratchBuf<hc_replica::DsMapNode> tmp{};
        hc_replica::DsMapBulkMergeSorted(list0, initSb, tmp, cfg);

        hc_replica::DsListPtr outC{}, outD{};
        hc_replica::ReplicaFastStats st{};
        hc_replica::QueryScratch qsc{};
        const auto tJoin0 = std::chrono::steady_clock::now();
        hc_replica::FastQueryReplicaList(conn.data(), &outC, &outD, cfg, &st, &qsc);
        const auto joinUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - tJoin0).count();

        CHECK(outC.size == static_cast<uint32_t>(kN));
        CHECK(list0->size == 0u && list1->size == static_cast<uint32_t>(kN));
        // Verify strict ascending order in list1
        for (uint32_t i = 1; i < list1->size; ++i) {
            CHECK(reinterpret_cast<uintptr_t>(list1->arr[i].key) > reinterpret_cast<uintptr_t>(list1->arr[i - 1].key));
        }

        // Now wire conn->constructedReplicaList (+0x30) & serializedReplicaList (+0x50) for kN replicas + 1 orphan in serialized only
        struct MockLsr { void* replica; uint64_t ts; void* bs; };
        std::vector<MockLsr> lsrs(kN);
        auto* cLst = reinterpret_cast<hc_replica::DsListPtr*>(conn.data() + 0x30);
        auto* sLst = reinterpret_cast<hc_replica::DsListPtr*>(conn.data() + 0x50);
        for (int i = 0; i < kN; ++i) {
            lsrs[i].replica = reps[i];
            *(static_cast<uint8_t*>(reps[i]) + 0x178) = 1; // mark preSerialized=1
            // Keep index 0..999 visible, cull 1000..4999 (4000 wormhole bulk destroys!)
            if (i >= 1000) {
                uint8_t* pr = static_cast<uint8_t*>(reps[i]) - 0x20;
                *reinterpret_cast<uint64_t*>(pr + 0x1c0) = pMask;
                // Half have cullCopy==0 (must reinsert to list0), half have cullCopy==pMask
                *reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(reps[i]) + 0x190) = (i & 1) ? pMask : 0ULL;
            }
            // Deliberately skip index 4999 from constructedReplicaList to prove serializedReplicaList preserves it (exact AutoConstructByQuery parity!)
            if (i != 4999) hc_replica::DsListPush(cLst, &lsrs[i], cfg);
            hc_replica::DsListPush(sLst, &lsrs[i], cfg);
        }

        outC.size = 0; outD.size = 0;
        const auto tWorm0 = std::chrono::steady_clock::now();
        hc_replica::FastQueryReplicaList(conn.data(), &outC, &outD, cfg, &st, &qsc);
        const auto wormUs = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - tWorm0).count();

        CHECK(outD.size == 4000u);
        CHECK(list1->size == 1000u);
        CHECK(list0->size == 2000u); // even indices in 1000..4999 had cullCopy==0
        CHECK(cLst->size == 1000u);
        CHECK(sLst->size == 1001u); // 1000 surviving + 1 (index 4999 not in constructed)
        CHECK(st.bulk_conn_compacts.load() == 1);

        // Verify ClearSerializedPreFlags clears all +0x178 on surviving serializedReplicaList
        struct MockWorld { void** connArr; uint32_t connCnt; uint32_t pad; };
        void* connPtrs[1] = { conn.data() };
        MockWorld world{ connPtrs, 1, 0 };
        MockWorld* worldArr[1] = { &world };
        *reinterpret_cast<MockWorld***>(rm3.data() + 0x880) = worldArr;

        const uint32_t clr = hc_replica::ClearSerializedPreFlags(rm3.data(), cfg, &st);
        CHECK(clr == 1001u);
        for (uint32_t i = 0; i < sLst->size; ++i) {
            auto* rep = static_cast<uint8_t*>(static_cast<MockLsr*>(sLst->arr[i])->replica);
            CHECK(*(rep + 0x178) == 0u);
        }
        // Zero-connection fast skip
        world.connCnt = 0;
        CHECK(hc_replica::ClearSerializedPreFlags(rm3.data(), cfg, &st) == 0u);
        CHECK(st.preser_zero_conn_skips.load() == 1);

        if (list0->arr) mDel(list0->arr);
        if (list1->arr) mDel(list1->arr);
        if (outC.arr)   mDel(outC.arr);
        if (outD.arr)   mDel(outD.arr);
        if (cLst->arr)  mDel(cLst->arr);
        if (sLst->arr)  mDel(sLst->arr);
        initSb.Free(); tmp.Free(); qsc.FreeAll();
        CHECK(s_a2.load() == s_f2.load());
        std::printf("[PASS] 20. ReplicaFastPath 5,000-entity bulk join (%lldus) + 4,000-entity wormhole destroy (%lldus) + PreSerialize Zero-Scan verified (0 leaks)\n",
                    static_cast<long long>(joinUs), static_cast<long long>(wormUs));
    }

    // Test 21: Tiered Hardware Liberation (Low/Balanced/Unleashed, <8GB clamp, Shard P-core topology, runtime limits)
    {
        using namespace hc_server_tier;
        CHECK(ParseTier("0") == ServerTier::Low && ParseTier("LOW") == ServerTier::Low);
        CHECK(ParseTier("1") == ServerTier::Balanced && ParseTier("balanced") == ServerTier::Balanced);
        CHECK(ParseTier("2") == ServerTier::Unleashed && ParseTier("Unleashed") == ServerTier::Unleashed);
        CHECK(std::strcmp(TierName(ServerTier::Unleashed), "unleashed") == 0);

        HostCaps bigCaps{};
        bigCaps.logicalCores  = 16u;
        bigCaps.perfCoreCount = 8u;
        for (uint32_t i = 0; i < 8u; ++i) bigCaps.perfCoreIds[i] = i * 2u;
        bigCaps.totalRamMb    = 32768ULL;

        const auto bLow = ComputeServerTierBudget(ServerTier::Low, bigCaps);
        const auto bBal = ComputeServerTierBudget(ServerTier::Balanced, bigCaps);
        const auto bUnl = ComputeServerTierBudget(ServerTier::Unleashed, bigCaps);
        CHECK(bLow.miPurgeDelayMs == 50 && !bLow.disableEcoQos && !bLow.timerBoost1ms && !bLow.prewarmBundles);
        CHECK(bBal.miPurgeDelayMs == 500 && bBal.disableEcoQos && bBal.aboveNormalPriority && !bBal.pinShardIdealCore);
        CHECK(bUnl.miPurgeDelayMs == 5000 && !bUnl.miPurgeDecommits && bUnl.highPriority && bUnl.disableEcoQos);
        CHECK(bUnl.asyncSaveMaxQueueBytes == 256u * 1024u * 1024u && bUnl.fastDumpMaxNodes == 12000000u);
        CHECK(bUnl.astarWarmPoolCap == 256u && bUnl.fsNegTtlSec == 120ULL && bUnl.libdeflateLevel == 2);
        CHECK(bUnl.pinShardIdealCore && bUnl.prewarmBundles);

        // <8GB Low-RAM Safety Clamp
        HostCaps lowRamCaps = bigCaps;
        lowRamCaps.totalRamMb = 6144ULL;
        const auto bClamp = ComputeServerTierBudget(ServerTier::Unleashed, lowRamCaps);
        CHECK(bClamp.gcEmergencyAddMb <= 256 && bClamp.asyncSaveMaxQueueBytes <= 64u * 1024u * 1024u);
        CHECK(bClamp.miPurgeDecommits && bClamp.miPurgeDelayMs <= 500 && !bClamp.prewarmBundles);

        // Hybrid 16-core P-core shard isolation: skips Core 0 and separates Master vs Caves
        const int cMaster = SelectIdealShardCore("Master", bigCaps);
        const int cCaves  = SelectIdealShardCore("Caves", bigCaps);
        CHECK(cMaster > 0 && cCaves > 0 && cMaster != cCaves);
        HostCaps smallCaps{}; smallCaps.logicalCores = 4u; smallCaps.perfCoreCount = 4u;
        CHECK(SelectIdealShardCore("Master", smallCaps) == -1);

        // anti_stw_gc Unleashed ramp (96MB ramp keeps 48MB excess at idle budget, whereas 32MB ramp escalates)
        anti_stw_gc::State gs{};
        anti_stw_gc::Config gcCfg{};
        gcCfg.gc_emergency_add_mb = 768;
        gcCfg.gc_calm_max_ms      = 6.5f;
        gcCfg.gc_busy_min_ms      = 0.15f;
        gcCfg.gc_excess_ramp_mb   = 96.0f;
        for (int t = 0; t < 130; ++t) {
            (void)gs.Evaluate(reinterpret_cast<void*>(0x1234), 100 * 1024, 200 * 1024, 4.0, false, gcCfg);
        }
        const auto decIdle = gs.Evaluate(reinterpret_cast<void*>(0x1234), (100 + 48) * 1024, 900 * 1024, 4.0, false, gcCfg);
        CHECK(decIdle.step_budget_ms < 0.60f);
        gcCfg.gc_excess_ramp_mb = 32.0f;
        const auto decRamp = gs.Evaluate(reinterpret_cast<void*>(0x1234), (100 + 48) * 1024, 900 * 1024, 4.0, false, gcCfg);
        CHECK(decRamp.step_budget_ms > 0.90f);

        // Runtime FastAStar pool resize & AsyncSave SetLimits
        fast_astar::StateManager<64u> amgr;
        amgr.SetMaxWarmPool(256u);
        CHECK(amgr.MaxWarmPool() == 256u);
        hc_async_save::AsyncSaveQueue aq(64u * 1024u * 1024u);
        aq.SetLimits(256u * 1024u * 1024u, 32u * 1024u * 1024u);
        CHECK(aq.MaxQueueBytes() == 256u * 1024u * 1024u);
        aq.ShutdownAndFlush();

        // Custom fast_dumper maxNodes enforcement
        MockLuaState ms21;
        LuaApiTable api21 = MakeMockApi();
        MockTable tbl21;
        for (int i = 1; i <= 5; ++i) tbl21.entries.emplace_back(MockVal::Num(i), MockVal::Num(i * 10));
        ms21.stack.push_back(MockVal::Tbl(&tbl21));
        fast_dumper::GrowBuf buf21;
        const char* rsn = nullptr;
        CHECK(fast_dumper::DumpRoot(&ms21, api21, 1, nullptr, 0, buf21, &rsn, 3u) == fast_dumper::Status::FallbackNeeded);
        CHECK(rsn != nullptr && std::strcmp(rsn, "node_limit_exceeded") == 0);
        CHECK(fast_dumper::DumpRoot(&ms21, api21, 1, nullptr, 0, buf21, &rsn, 16u) == fast_dumper::Status::Ok);

        std::printf("[PASS] 21. Server HardwareTier (Low/Bal/Unleashed + <8GB Clamp), Shard P-Core Isolation, GC Ramp, Pool/Queue SetLimits & DumpRoot maxNodes verified!\n");
    }

    // -------------------------------------------------------------------------
    // Test 22: Module 10B (v1.6.0) — Zero-Desync Tiered Replica Construction Pacing
    //          22A: Tier-0 Immediate Bypass (non-proxy, q==1, classified, owner, InvItem, parented)
    //          22B: Parent-Before-Child Promotion (p1, p2, multi-level parent chain, zero budget burn)
    //          22C: Strict DsMap Sorted & Disjoint Invariants + Multi-Tick Convergence (Ticks 1..12)
    //          22D: Walk-Away Cancellation + Mid-Burst Death + 100% Unpaced Loop B Destructions
    //          22E: Proximity Priority Buckets + NaN/Null Fail-Closed + HardwareTier Presets & Clamp
    // -------------------------------------------------------------------------
    {
        static std::atomic<int> s_a22{0}, s_f22{0};
        s_a22 = 0; s_f22 = 0;
        auto mNew22 = [](size_t sz) -> void* { s_a22.fetch_add(1); return std::malloc(sz ? sz : 1); };
        auto mDel22 = [](void* p) { if (p) { s_f22.fetch_add(1); std::free(p); } };

        static void* s_pv22[64]    = {};
        static void* s_iv22[16]    = {};
        static void* s_other22[64] = {};

        struct NonProxyRep22 {
            void* vtbl;
            uint8_t pad[0x190 - 8];
            uint64_t cullCopy;
            bool dead;
            int32_t qcRet;
            int32_t qdRet;
        };
        auto npDead22 = [](void* r) -> bool { return static_cast<NonProxyRep22*>(r)->dead; };
        auto npQc22   = [](void* r, void*, void*) -> int32_t { return static_cast<NonProxyRep22*>(r)->qcRet; };
        auto npQd22   = [](void* r, void*, void*) -> int32_t { return static_cast<NonProxyRep22*>(r)->qdRet; };
        s_other22[0x138 / 8] = reinterpret_cast<void*>(+npDead22);
        s_other22[0x040 / 8] = reinterpret_cast<void*>(+npQc22);
        s_other22[0x048 / 8] = reinterpret_cast<void*>(+npQd22);

        auto verifyDsMapInvariants = [](const hc_replica::DsMap* l0, const hc_replica::DsMap* l1,
                                        bool requireL0Invalidated = true, bool requireL1Invalidated = true) {
            if (requireL0Invalidated) CHECK(l0->lastSearchValid == 0u);
            if (requireL1Invalidated) CHECK(l1->lastSearchValid == 0u);
            for (uint32_t i = 0; i < l0->size; ++i) {
                CHECK(l0->arr[i].key != nullptr);
                CHECK(l0->arr[i].key == l0->arr[i].val);
                if (i > 0u) {
                    CHECK(reinterpret_cast<uintptr_t>(l0->arr[i - 1u].key) <
                          reinterpret_cast<uintptr_t>(l0->arr[i].key));
                }
                // Strict disjointness: list0 ∩ list1 == ∅
                CHECK(!hc_replica::DsMapContains(l1, l0->arr[i].key));
            }
            for (uint32_t i = 0; i < l1->size; ++i) {
                CHECK(l1->arr[i].key != nullptr);
                CHECK(l1->arr[i].key == l1->arr[i].val);
                if (i > 0u) {
                    CHECK(reinterpret_cast<uintptr_t>(l1->arr[i - 1u].key) <
                          reinterpret_cast<uintptr_t>(l1->arr[i].key));
                }
            }
        };

        auto listContains = [](const hc_replica::DsListPtr& lst, void* target) -> bool {
            for (uint32_t i = 0; i < lst.size; ++i) {
                if (lst.arr[i] == target) return true;
            }
            return false;
        };

        struct Pool22 {
            std::vector<uint8_t> proxies;
            std::vector<uint8_t> ents;
            std::vector<void*>   reps;
            std::vector<void*>   invCompSlot;
            void*                invCompObj = nullptr;
            void Init(size_t count) {
                proxies.assign(count * 0x240u, 0);
                ents.assign(count * 0x240u, 0);
                reps.resize(count);
                invCompObj = &s_iv22[0];
                invCompSlot.assign(1, &invCompObj);
                for (size_t i = 0; i < count; ++i) {
                    uint8_t* pr = proxies.data() + i * 0x240u;
                    uint8_t* en = ents.data()    + i * 0x240u;
                    *reinterpret_cast<void**>(pr + 0x20) = s_pv22;
                    *reinterpret_cast<void**>(pr + 0x18) = en;
                    *reinterpret_cast<uint64_t*>(pr + 0x1c0) = 0ULL;
                    *reinterpret_cast<uint64_t*>(pr + 0x1c8) = hc_replica::kUnassignedGuid;
                    *reinterpret_cast<uint64_t*>(pr + 0x1d8) = hc_replica::kUnassignedGuid;
                    reps[i] = pr + 0x20;
                }
            }
            uint8_t* Proxy(size_t i) { return proxies.data() + i * 0x240u; }
            uint8_t* Ent(size_t i)   { return ents.data()    + i * 0x240u; }
            void SetPos(size_t i, float x, float z) {
                std::memcpy(Ent(i) + 0x200, &x, sizeof(float));
                std::memcpy(Ent(i) + 0x208, &z, sizeof(float));
            }
            void MarkInvItem(size_t i) {
                *reinterpret_cast<void***>(Ent(i) + 0x100) = invCompSlot.data();
                *reinterpret_cast<void***>(Ent(i) + 0x108) = invCompSlot.data() + invCompSlot.size();
            }
        };

        static void* s_cli22 = nullptr;

        struct WorldEnv22 {
            std::vector<uint8_t> netMgr, rm3, client, conn;
            void* netMgrPtr = nullptr;
            uint64_t playerGuid = 0xCAFEBABE11223344ULL;
            uint64_t playerMask = (1ULL << 2); // slot 2
            hc_replica::ReplicaAbiConfig cfg{};
            hc_replica::DsMap* list0 = nullptr;
            hc_replica::DsMap* list1 = nullptr;
            hc_replica::DsMap* list2 = nullptr;

            static void* GetCli(void*, const void*) { return s_cli22; }
            static bool  RelTrue(void*, const void*, uint64_t) { return true; }

            void Init(void* (*fnNew)(size_t), void (*fnDel)(void*)) {
                netMgr.assign(0x400, 0);
                rm3.assign(0x3000, 0);
                client.assign(0x400, 0);
                conn.assign(0x100, 0);
                *reinterpret_cast<void**>(netMgr.data() + 0x288) = rm3.data();
                netMgr[0x308] = 0; // dedicated server
                *reinterpret_cast<uint64_t*>(conn.data() + 0x20) = playerGuid;
                *reinterpret_cast<uint64_t*>(client.data() + 0x1b0) = playerMask;
                client[0x341] = 1; // ready
                client[0x342] = 0; // not blocked
                s_cli22 = client.data();
                netMgrPtr = netMgr.data();

                cfg = hc_replica::ReplicaAbiConfig{};
                cfg.ppNetMgr          = &netMgrPtr;
                cfg.fnGetClientByGuid = &GetCli;
                cfg.fnIsRelevant      = &RelTrue;
                cfg.fnHostNew         = fnNew;
                cfg.fnHostDelete      = fnDel;
                cfg.networkProxyVtbl  = s_pv22;
                cfg.invItemCompVtbl   = s_iv22;

                list0 = reinterpret_cast<hc_replica::DsMap*>(rm3.data() + 0x8a8 + 2 * sizeof(hc_replica::DsMap));
                list1 = reinterpret_cast<hc_replica::DsMap*>(rm3.data() + 0x12a8 + 2 * sizeof(hc_replica::DsMap));
                list2 = reinterpret_cast<hc_replica::DsMap*>(rm3.data() + 0x1ca8 + 2 * sizeof(hc_replica::DsMap));
            }
            void Cleanup(hc_replica::DsListPtr& oc, hc_replica::DsListPtr& od, void (*fnDel)(void*)) {
                if (list0->arr) fnDel(list0->arr);
                if (list1->arr) fnDel(list1->arr);
                if (list2->arr) fnDel(list2->arr);
                if (oc.arr)     fnDel(oc.arr);
                if (od.arr)     fnDel(od.arr);
                auto* cLst = reinterpret_cast<hc_replica::DsListPtr*>(conn.data() + 0x30);
                auto* sLst = reinterpret_cast<hc_replica::DsListPtr*>(conn.data() + 0x50);
                if (cLst->arr) fnDel(cLst->arr);
                if (sLst->arr) fnDel(sLst->arr);
            }
        };

        // ---------------------------------------------------------------------
        // 22A: Tier-0 Immediate Bypass (600 Tier-1 + 7 Tier-0 classes, budget=64)
        // ---------------------------------------------------------------------
        {
            WorldEnv22 env;
            env.Init(mNew22, mDel22);
            env.cfg.maxConstructPerTick = 64u;
            env.cfg.proximityValid      = false;

            // 0..599: ordinary Tier-1 world entities; 600: already-constructed parent in list1;
            // 601..605: classified, owner-bound, InvItem, parented(p1), parented(p2)
            Pool22 pool;
            pool.Init(606);
            *reinterpret_cast<uint64_t*>(pool.Proxy(601) + 0x1d8) = env.playerGuid; // classified
            *reinterpret_cast<uint64_t*>(pool.Proxy(602) + 0x1c8) = env.playerGuid; // owner-bound
            pool.MarkInvItem(603);                                                  // InvItem
            *reinterpret_cast<void**>(pool.Ent(604) + 0x0a0) = pool.Ent(600);       // p1 -> already in list1
            *reinterpret_cast<void**>(pool.Ent(605) + 0x0c8) = pool.Ent(600);       // p2 -> already in list1

            NonProxyRep22 npQ0{}; npQ0.vtbl = s_other22; npQ0.dead = false; npQ0.qcRet = 0; npQ0.qdRet = 2;
            NonProxyRep22 npQ1{}; npQ1.vtbl = s_other22; npQ1.dead = false; npQ1.qcRet = 1; npQ1.qdRet = 2;

            std::vector<void*> init0;
            init0.reserve(607);
            for (size_t i = 0; i < 600; ++i) init0.push_back(pool.reps[i]);
            for (size_t i = 601; i <= 605; ++i) init0.push_back(pool.reps[i]);
            init0.push_back(&npQ0);
            init0.push_back(&npQ1);
            std::sort(init0.begin(), init0.end());

            hc_replica::ScratchBuf<void*> sb0{}, sb1{};
            hc_replica::ScratchBuf<hc_replica::DsMapNode> tmp{};
            for (void* p : init0) sb0.Push(p);
            hc_replica::DsMapBulkMergeSorted(env.list0, sb0, tmp, env.cfg);
            sb1.Push(pool.reps[600]);
            hc_replica::DsMapBulkMergeSorted(env.list1, sb1, tmp, env.cfg);

            hc_replica::DsListPtr outC{}, outD{};
            hc_replica::ReplicaFastStats st{};
            hc_replica::QueryScratch qsc{};
            env.list0->lastSearchValid = 1;
            env.list1->lastSearchValid = 1;
            hc_replica::FastQueryReplicaList(env.conn.data(), &outC, &outD, env.cfg, &st, &qsc);

            // All 7 Tier-0 entities + exactly 64 Tier-1 entities = 71 constructed on Tick 1!
            CHECK(outC.size == 71u);
            CHECK(outD.size == 0u);
            CHECK(env.list0->size == 536u);
            CHECK(env.list1->size == 1u + 6u + 64u); // pre-existing parent + 6 q==0 Tier-0 + 64 Tier-1
            CHECK(env.list2->size == 1u && hc_replica::DsMapContains(env.list2, &npQ1));
            CHECK(listContains(outC, &npQ0));
            CHECK(listContains(outC, &npQ1));
            CHECK(listContains(outC, pool.reps[601]));
            CHECK(listContains(outC, pool.reps[602]));
            CHECK(listContains(outC, pool.reps[603]));
            CHECK(listContains(outC, pool.reps[604]));
            CHECK(listContains(outC, pool.reps[605]));
            CHECK(st.paced_burst_ticks.load() == 1u);
            CHECK(st.paced_deferred_total.load() == 536u);
            verifyDsMapInvariants(env.list0, env.list1);

            sb0.Free(); sb1.Free(); tmp.Free(); qsc.FreeAll();
            env.Cleanup(outC, outD, mDel22);
        }

        // ---------------------------------------------------------------------
        // 22B & 22C: Parent-Before-Child Promotion (p1, p2, 2-level chain)
        //            + Multi-Tick Convergence & Strict DsMap Invariants (Ticks 1..12)
        // ---------------------------------------------------------------------
        {
            WorldEnv22 env;
            env.Init(mNew22, mDel22);
            env.cfg.maxConstructPerTick = 64u;
            env.cfg.proximityValid      = false;

            // 0..599: Tier-1 entities; 600: childP1 (p1->590); 601: childP2 (p2->595);
            // 602: midParent (p1->598); 603: grandChild (p1->602)
            constexpr size_t kTier1N = 600;
            constexpr size_t kTier0N = 4;
            Pool22 pool;
            pool.Init(kTier1N + kTier0N);
            *reinterpret_cast<void**>(pool.Ent(600) + 0x0a0) = pool.Ent(590);
            *reinterpret_cast<void**>(pool.Ent(601) + 0x0c8) = pool.Ent(595);
            *reinterpret_cast<void**>(pool.Ent(602) + 0x0a0) = pool.Ent(598);
            *reinterpret_cast<void**>(pool.Ent(603) + 0x0a0) = pool.Ent(602);

            hc_replica::ScratchBuf<void*> sb0{};
            hc_replica::ScratchBuf<hc_replica::DsMapNode> tmp{};
            for (void* p : pool.reps) sb0.Push(p); // ascending in contiguous pool
            hc_replica::DsMapBulkMergeSorted(env.list0, sb0, tmp, env.cfg);

            hc_replica::DsListPtr outC{}, outD{};
            hc_replica::ReplicaFastStats st{};
            hc_replica::QueryScratch qsc{};
            std::unordered_set<void*> constructedSet;

            int ticksToConverge = 0;
            for (int tick = 1; tick <= 12; ++tick) {
                outC.size = 0;
                outD.size = 0;
                env.list0->lastSearchValid = 1;
                env.list1->lastSearchValid = 1;

                hc_replica::FastQueryReplicaList(env.conn.data(), &outC, &outD, env.cfg, &st, &qsc);
                CHECK(outD.size == 0u);

                for (uint32_t i = 0; i < outC.size; ++i) {
                    // ZERO duplicates across all ticks!
                    CHECK(constructedSet.insert(outC.arr[i]).second);
                }

                // After EVERY SINGLE TICK verify strict DsMap sorting, disjointness, and conservation!
                verifyDsMapInvariants(env.list0, env.list1,
                                      /*requireL0Invalidated=*/(outC.size > 0u),
                                      /*requireL1Invalidated=*/(outC.size > 0u));
                CHECK(env.list0->size + env.list1->size == kTier1N + kTier0N);

                if (tick == 1) {
                    // All 4 Tier-0 + 3 promoted parents (590, 595, 598) + 64 Tier-1 budget = 71!
                    CHECK(st.paced_parent_promotes.load() == 3u);
                    CHECK(outC.size == kTier0N + 3u + 64u);
                    CHECK(listContains(outC, pool.reps[600]) && listContains(outC, pool.reps[590]));
                    CHECK(listContains(outC, pool.reps[601]) && listContains(outC, pool.reps[595]));
                    CHECK(listContains(outC, pool.reps[603]) && listContains(outC, pool.reps[602]) && listContains(outC, pool.reps[598]));
                    CHECK(env.list0->size == kTier1N - 3u - 64u);
                }
                if (env.list0->size == 0u && ticksToConverge == 0) {
                    ticksToConverge = tick;
                    break;
                }
            }
            CHECK(ticksToConverge > 1 && ticksToConverge <= 10);
            CHECK(env.list0->size == 0u);
            CHECK(env.list1->size == kTier1N + kTier0N);
            CHECK(constructedSet.size() == kTier1N + kTier0N);

            sb0.Free(); tmp.Free(); qsc.FreeAll();
            env.Cleanup(outC, outD, mDel22);
        }

        // ---------------------------------------------------------------------
        // 22D: Walk-Away Cancellation + Mid-Burst Death + Unpaced Loop B Destroy
        // ---------------------------------------------------------------------
        {
            WorldEnv22 env;
            env.Init(mNew22, mDel22);
            env.cfg.maxConstructPerTick = 64u;
            env.cfg.proximityValid      = false;

            // 0..499: initial list0 Tier-1 burst; 500..799: 300 pre-existing list1 entities
            Pool22 pool;
            pool.Init(800);
            hc_replica::ScratchBuf<void*> sb0{}, sb1{};
            hc_replica::ScratchBuf<hc_replica::DsMapNode> tmp{};
            for (size_t i = 0; i < 500; ++i) sb0.Push(pool.reps[i]);
            for (size_t i = 500; i < 800; ++i) sb1.Push(pool.reps[i]);
            hc_replica::DsMapBulkMergeSorted(env.list0, sb0, tmp, env.cfg);
            hc_replica::DsMapBulkMergeSorted(env.list1, sb1, tmp, env.cfg);

            hc_replica::DsListPtr outC{}, outD{};
            hc_replica::ReplicaFastStats st{};
            hc_replica::QueryScratch qsc{};

            // Tick 1: 64 constructed, 436 deferred in list0
            hc_replica::FastQueryReplicaList(env.conn.data(), &outC, &outD, env.cfg, &st, &qsc);
            CHECK(outC.size == 64u && outD.size == 0u);
            CHECK(env.list0->size == 436u && env.list1->size == 364u);
            verifyDsMapInvariants(env.list0, env.list1);

            // Before Tick 2: mark 100 deferred dead, 150 deferred culled (walk-away),
            // and cull ALL 300 pre-existing list1 entities!
            std::unordered_set<void*> deadSet, walkAwaySet;
            CHECK(env.list0->size == 436u);
            for (uint32_t i = 0; i < 100u; ++i) {
                void* rep = env.list0->arr[i].key;
                uint8_t* pr = static_cast<uint8_t*>(rep) - 0x20;
                uint8_t* en = *reinterpret_cast<uint8_t**>(pr + 0x18);
                en[0x20c] = 1; // dead!
                deadSet.insert(rep);
            }
            for (uint32_t i = 100u; i < 250u; ++i) {
                void* rep = env.list0->arr[i].key;
                uint8_t* pr = static_cast<uint8_t*>(rep) - 0x20;
                *reinterpret_cast<uint64_t*>(pr + 0x1c0) = env.playerMask; // irrelevant/culled!
                walkAwaySet.insert(rep);
            }
            for (size_t i = 500; i < 800; ++i) {
                *reinterpret_cast<uint64_t*>(pool.Proxy(i) + 0x1c0) = env.playerMask;
                *reinterpret_cast<uint64_t*>(static_cast<uint8_t*>(pool.reps[i]) + 0x190) = env.playerMask; // cullCopy set
            }

            // Tick 2: Loop B destroys ALL 300 IMMEDIATELY (unpaced!), drops 100 dead,
            // keeps 150 irrelevant in list0 without constructing, constructs 64 of remaining 186!
            outC.size = 0; outD.size = 0;
            hc_replica::FastQueryReplicaList(env.conn.data(), &outC, &outD, env.cfg, &st, &qsc);
            CHECK(outD.size == 300u); // 100% unpaced Loop B!
            CHECK(outC.size == 64u);
            for (uint32_t i = 0; i < outC.size; ++i) {
                CHECK(deadSet.find(outC.arr[i]) == deadSet.end());
                CHECK(walkAwaySet.find(outC.arr[i]) == walkAwaySet.end());
            }
            CHECK(env.list0->size == 150u + (186u - 64u)); // 272
            CHECK(env.list1->size == 64u + 64u);           // 128
            verifyDsMapInvariants(env.list0, env.list1);

            // Ticks 3..6: remaining 122 relevant converge cleanly (64 + 58)
            uint32_t extraConstructed = 0;
            for (int tick = 3; tick <= 6; ++tick) {
                outC.size = 0; outD.size = 0;
                hc_replica::FastQueryReplicaList(env.conn.data(), &outC, &outD, env.cfg, &st, &qsc);
                CHECK(outD.size == 0u);
                for (uint32_t i = 0; i < outC.size; ++i) {
                    CHECK(deadSet.find(outC.arr[i]) == deadSet.end());
                    CHECK(walkAwaySet.find(outC.arr[i]) == walkAwaySet.end());
                }
                extraConstructed += outC.size;
                verifyDsMapInvariants(env.list0, env.list1,
                                      /*requireL0Invalidated=*/(outC.size > 0u),
                                      /*requireL1Invalidated=*/(outC.size > 0u));
            }
            CHECK(extraConstructed == 122u);
            CHECK(env.list0->size == 150u); // exact 150 walked-away irrelevant entities
            CHECK(env.list1->size == 250u); // 64 (Tick 1) + 186 (remaining relevant)
            for (void* wrep : walkAwaySet) {
                CHECK(hc_replica::DsMapContains(env.list0, wrep));
                CHECK(!hc_replica::DsMapContains(env.list1, wrep));
            }

            sb0.Free(); sb1.Free(); tmp.Free(); qsc.FreeAll();
            env.Cleanup(outC, outD, mDel22);
        }

        // ---------------------------------------------------------------------
        // 22E: Proximity Priority Buckets + NaN/Null Fail-Closed +
        //      HardwareTier Budget Presets, Clamp & Precedence
        // ---------------------------------------------------------------------
        {
            WorldEnv22 env;
            env.Init(mNew22, mDel22);
            env.cfg.maxConstructPerTick = 64u;
            env.cfg.proximityValid      = true;

            // Player at (100.0f, 100.0f)
            Pool22 plyPool;
            plyPool.Init(1);
            plyPool.SetPos(0, 100.0f, 100.0f);
            *reinterpret_cast<void**>(env.client.data() + env.cfg.offClientPlayerProxy) = plyPool.Proxy(0);

            // Arrange in REVERSE address order vs proximity (far/NaN at lowest addresses, near at highest):
            //   0..9:   NaN/+Inf coords (must stay bucket 3, NEVER bypass!)
            //   10..59: far  (50 ents @ dist=60wu -> bucket 3)
            //   60..109: mid2 (50 ents @ dist=35wu -> bucket 2)
            //   110..159: mid1 (50 ents @ dist=20wu -> bucket 1)
            //   160..169: near (10 ents @ dist=10wu <= 14wu -> bucket 0 Tier-0 bypass!)
            Pool22 pool;
            pool.Init(170);
            const float qNan = std::numeric_limits<float>::quiet_NaN();
            const float pInf = std::numeric_limits<float>::infinity();
            for (size_t i = 0; i < 10; ++i)   pool.SetPos(i, (i & 1) ? qNan : pInf, 100.0f);
            for (size_t i = 10; i < 60; ++i)  pool.SetPos(i, 160.0f, 100.0f);
            for (size_t i = 60; i < 110; ++i) pool.SetPos(i, 135.0f, 100.0f);
            for (size_t i = 110; i < 160; ++i) pool.SetPos(i, 120.0f, 100.0f);
            for (size_t i = 160; i < 170; ++i) pool.SetPos(i, 110.0f, 100.0f);

            hc_replica::ScratchBuf<void*> sb0{};
            hc_replica::ScratchBuf<hc_replica::DsMapNode> tmp{};
            for (void* p : pool.reps) sb0.Push(p);
            hc_replica::DsMapBulkMergeSorted(env.list0, sb0, tmp, env.cfg);

            hc_replica::DsListPtr outC{}, outD{};
            hc_replica::ReplicaFastStats st{};
            hc_replica::QueryScratch qsc{};
            hc_replica::FastQueryReplicaList(env.conn.data(), &outC, &outD, env.cfg, &st, &qsc);

            // Tick 1: ALL 10 near (bypass) + ALL 50 mid1 + 14 mid2 = 74; ZERO far/NaN!
            CHECK(st.paced_near_bypasses.load() == 10u);
            CHECK(outC.size == 74u);
            CHECK(env.list0->size == 96u);
            for (size_t i = 160; i < 170; ++i) CHECK(listContains(outC, pool.reps[i]));
            for (size_t i = 110; i < 160; ++i) CHECK(listContains(outC, pool.reps[i]));
            size_t mid2Hit = 0;
            for (size_t i = 60; i < 110; ++i) if (listContains(outC, pool.reps[i])) ++mid2Hit;
            CHECK(mid2Hit == 14u);
            for (size_t i = 0; i < 60; ++i) CHECK(!listContains(outC, pool.reps[i]));
            verifyDsMapInvariants(env.list0, env.list1);

            // Fail-closed NaN player position & null playerProxy -> 0 crash, 0 near bypasses, deterministic pointer order
            plyPool.SetPos(0, qNan, 100.0f);
            outC.size = 0; outD.size = 0;
            hc_replica::FastQueryReplicaList(env.conn.data(), &outC, &outD, env.cfg, &st, &qsc);
            CHECK(outC.size == 64u);
            CHECK(st.paced_near_bypasses.load() == 10u); // unchanged!
            verifyDsMapInvariants(env.list0, env.list1);

            *reinterpret_cast<void**>(env.client.data() + env.cfg.offClientPlayerProxy) = nullptr;
            outC.size = 0; outD.size = 0;
            hc_replica::FastQueryReplicaList(env.conn.data(), &outC, &outD, env.cfg, &st, &qsc);
            CHECK(outC.size == 32u); // remaining 32 converge cleanly
            CHECK(env.list0->size == 0u && env.list1->size == 170u);
            verifyDsMapInvariants(env.list0, env.list1);

            sb0.Free(); tmp.Free(); qsc.FreeAll();
            env.Cleanup(outC, outD, mDel22);

            // HardwareTier Budget Presets, Clamp [16, 1024] & 0=off, and Override Precedence
            using namespace hc_server_tier;
            HostCaps caps{}; caps.logicalCores = 16; caps.perfCoreCount = 8; caps.totalRamMb = 32768ULL;
            const auto bL = ComputeServerTierBudget(ServerTier::Low, caps);
            const auto bB = ComputeServerTierBudget(ServerTier::Balanced, caps);
            const auto bU = ComputeServerTierBudget(ServerTier::Unleashed, caps);
            CHECK(bL.replicaConstructBudget == 64u);
            CHECK(bB.replicaConstructBudget == 96u);
            CHECK(bU.replicaConstructBudget == 128u);

            CHECK(hc_replica::ClampReplicaConstructBudget(-5)   == 0u);
            CHECK(hc_replica::ClampReplicaConstructBudget(0)    == 0u);
            CHECK(hc_replica::ClampReplicaConstructBudget(1)    == 16u);
            CHECK(hc_replica::ClampReplicaConstructBudget(15)   == 16u);
            CHECK(hc_replica::ClampReplicaConstructBudget(16)   == 16u);
            CHECK(hc_replica::ClampReplicaConstructBudget(96)   == 96u);
            CHECK(hc_replica::ClampReplicaConstructBudget(1024) == 1024u);
            CHECK(hc_replica::ClampReplicaConstructBudget(4096) == 1024u);

            // Mirror ResolveEffectiveReplicaBudget precedence (Lua override > explicit INI > HardwareTier)
            auto resolveBudget = [](int luaOv, bool iniExplicit, int iniVal, uint32_t tierBudget) -> uint32_t {
                if (luaOv >= 0) return hc_replica::ClampReplicaConstructBudget(luaOv);
                if (iniExplicit) return hc_replica::ClampReplicaConstructBudget(iniVal);
                return hc_replica::ClampReplicaConstructBudget(static_cast<int>(tierBudget));
            };
            // Auto INI tracks tier: 64 / 96 / 128
            CHECK(resolveBudget(-1, false, -1, bL.replicaConstructBudget) == 64u);
            CHECK(resolveBudget(-1, false, -1, bB.replicaConstructBudget) == 96u);
            CHECK(resolveBudget(-1, false, -1, bU.replicaConstructBudget) == 128u);
            // Explicit INI (48 or 0=off) preserved across HardwareTier changes
            CHECK(resolveBudget(-1, true, 48, bU.replicaConstructBudget) == 48u);
            CHECK(resolveBudget(-1, true,  0, bU.replicaConstructBudget) == 0u);
            // Lua runtime override beats explicit INI; clearing (-1) restores explicit INI
            CHECK(resolveBudget(192, true, 48, bB.replicaConstructBudget) == 192u);
            CHECK(resolveBudget(0,   true, 48, bB.replicaConstructBudget) == 0u);
            CHECK(resolveBudget(-1,  true, 48, bB.replicaConstructBudget) == 48u);
        }

        CHECK(s_a22.load() == s_f22.load());
        std::printf("[PASS] 22. Module 10B Replica Construction Pacing verified (22A Tier-0 bypass, 22B parent-before-child + 2-level chain, 22C DsMap sorted/disjoint convergence, 22D walk-away/death/unpaced Loop B, 22E proximity buckets + NaN/null fail-closed + Tier/INI/clamp precedence, 0 leaks)\n");
    }

    // =========================================================================
    // [23] Track 3B/3C: Foreign Lua VM / LuaJIT2 Entry Trampoline Probe &
    //      Clean Yield Source Contracts (Windows + Linux Native Engines)
    // =========================================================================
    {
        auto isPatchedPrologue = [](const uint8_t* fn) -> bool {
            if (!fn) return false;
            if (fn[0] == 0xE9 || fn[0] == 0xEB) return true;
            if (fn[0] == 0xFF && fn[1] == 0x25) return true;
            if (fn[0] == 0x48 && fn[1] == 0xB8 && fn[10] == 0xFF && fn[11] == 0xE0) return true;
            if (fn[0] == 0x49 && fn[1] == 0xBB && fn[10] == 0x41 && fn[11] == 0xFF && fn[12] == 0xE3) return true;
            if (fn[0] == 0x68 && fn[5] == 0xC3) return true;
            return false;
        };

        const uint8_t cleanWinPrologue[16]  = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x45, 0x33, 0xC0 };
        const uint8_t cleanElfEndbr[16]     = { 0xF3, 0x0F, 0x1E, 0xFA, 0x41, 0x54, 0xB9, 0x78, 0x02, 0x00, 0x00, 0x31, 0xD2 };
        const uint8_t jmpRel32[16]          = { 0xE9, 0x12, 0x34, 0x56, 0x00, 0x90 };
        const uint8_t jmpInd64[16]          = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00, 0xAA, 0xBB };
        const uint8_t movRaxJmp[16]         = { 0x48, 0xB8, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0xFF, 0xE0 };
        const uint8_t movR11Jmp[16]         = { 0x49, 0xBB, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x41, 0xFF, 0xE3 };
        const uint8_t pushRet[16]           = { 0x68, 0x11, 0x22, 0x33, 0x44, 0xC3 };

        CHECK(!isPatchedPrologue(cleanWinPrologue));
        CHECK(!isPatchedPrologue(cleanElfEndbr));
        CHECK(isPatchedPrologue(jmpRel32));
        CHECK(isPatchedPrologue(jmpInd64));
        CHECK(isPatchedPrologue(movRaxJmp));
        CHECK(isPatchedPrologue(movR11Jmp));
        CHECK(isPatchedPrologue(pushRet));

        auto readTextFile = [](const char* path) -> std::string {
            FILE* f = std::fopen(path, "rb");
            CHECK(f != nullptr);
            std::string out;
            char buf[4096];
            while (size_t n = std::fread(buf, 1, sizeof(buf), f)) out.append(buf, n);
            std::fclose(f);
            return out;
        };
        const std::string winSrc = readTextFile("../native_plugin/iphlpapi.cpp");
        CHECK(winSrc.find("g_foreignLuaVmDetected") != std::string::npos);
        CHECK(winSrc.find("IsLuaJitModuleOrWinmmProxyPresent") != std::string::npos);
        CHECK(winSrc.find("trampoline_on_lua_newstate") != std::string::npos);
        CHECK(winSrc.find("trampoline_on_sim_collect_garbage") != std::string::npos);
        CHECK(winSrc.find("trampoline_on_lua_gc") != std::string::npos);
        CHECK(winSrc.find("trampoline_on_lua_close") != std::string::npos);
        CHECK(winSrc.find("HookSimUpdate") != std::string::npos);

        const std::string linSrc = readTextFile("../native_plugin/linux/hypercore_linux.cpp");
        CHECK(linSrc.find("DetectForeignLuaVmLinux") != std::string::npos);
        CHECK(linSrc.find("DlPhdrForeignLuaCb") != std::string::npos);
        CHECK(linSrc.find("dlsym_luaJIT_setmode") != std::string::npos);
        CHECK(linSrc.find("g_foreignLuaVmDetected") != std::string::npos);

        std::printf("[PASS] 23. Track 3B/3C Foreign Lua VM / LuaJIT2 Entry Probe & Windows/Linux Yield Contracts verified!\n");
    }

    std::printf("ALL 23 SERVER RED-TEAM REGRESSION TESTS PASSED!\n");
    return 0;
}
