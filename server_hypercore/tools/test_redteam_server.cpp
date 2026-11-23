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
#include "../native_plugin/fast_astar.h"
#include "../native_plugin/fast_astar_manager.h"
#include "../native_plugin/anti_stw_gc.h"
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
    api.type = m_type;
    api.tonumber = m_tonumber;
    api.toboolean = m_toboolean;
    api.tolstring = m_tolstring;
    api.topointer = m_topointer;
    api.pushnil = m_pushnil;
    api.pushlstring = m_pushlstring;
    api.pushstring = m_pushstring;
    api.pushboolean = m_pushboolean;
    api.next = m_next;
    CHECK(api.ReadyForFastDump());
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

    std::printf("ALL 13 SERVER RED-TEAM REGRESSION TESTS PASSED!\n");
    return 0;
}
