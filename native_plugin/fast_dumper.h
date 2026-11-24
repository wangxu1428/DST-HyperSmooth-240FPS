#pragma once
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_set>

// Minimal Lua 5.1.4 ABI constants (verified against dontstarve_dedicated_server_nullrenderer_x64.exe)
static constexpr int HC_LUA_TNIL          = 0;
static constexpr int HC_LUA_TBOOLEAN      = 1;
static constexpr int HC_LUA_TLIGHTUSERDATA= 2;
static constexpr int HC_LUA_TNUMBER       = 3;
static constexpr int HC_LUA_TSTRING       = 4;
static constexpr int HC_LUA_TTABLE        = 5;
static constexpr int HC_LUA_TFUNCTION     = 6;
static constexpr int HC_LUA_TUSERDATA     = 7;
static constexpr int HC_LUA_TTHREAD       = 8;
static constexpr int HC_LUA_GLOBALSINDEX  = -10002;

struct LuaApiTable {
    int         (*checkstack)(void* L, int extra) = nullptr;
    int         (*gettop)(void* L) = nullptr;
    void        (*settop)(void* L, int idx) = nullptr;
    void        (*pushvalue)(void* L, int idx) = nullptr;
    int         (*type)(void* L, int idx) = nullptr;
    double      (*tonumber)(void* L, int idx) = nullptr;
    int         (*toboolean)(void* L, int idx) = nullptr;
    const char* (*tolstring)(void* L, int idx, size_t* len) = nullptr;
    const void* (*topointer)(void* L, int idx) = nullptr;
    void        (*pushnil)(void* L) = nullptr;
    void        (*pushnumber)(void* L, double n) = nullptr;
    void        (*pushlstring)(void* L, const char* s, size_t l) = nullptr;
    void        (*pushstring)(void* L, const char* s) = nullptr;
    void        (*pushcclosure)(void* L, int (*fn)(void*), int n) = nullptr;
    void        (*pushboolean)(void* L, int b) = nullptr;
    void        (*getfield)(void* L, int idx, const char* k) = nullptr;
    void        (*createtable)(void* L, int narr, int nrec) = nullptr;
    void        (*setfield)(void* L, int idx, const char* k) = nullptr;
    int         (*gc)(void* L, int what, int data) = nullptr;
    int         (*next)(void* L, int idx) = nullptr;
    void        (*rawset)(void* L, int idx) = nullptr;
    void        (*rawseti)(void* L, int idx, int n) = nullptr;

    bool ReadyForFastDump() const {
        return checkstack && gettop && settop && type && tonumber && toboolean &&
               tolstring && topointer && pushnil && pushlstring && pushstring && pushboolean && next;
    }
    bool ReadyForRegister() const {
        return pushcclosure && setfield && createtable && pushnumber && pushstring && pushboolean;
    }
    bool ReadyForFastJson() const {
        return checkstack && gettop && settop && pushvalue && type && tonumber && toboolean &&
               tolstring && topointer && pushnil && pushnumber && pushlstring && pushstring &&
               pushboolean && createtable && next && rawset && rawseti;
    }
};

namespace fast_dumper {

static constexpr size_t   kMaxDumpBytes     = 256ULL * 1024ULL * 1024ULL; // 256 MB hard safety cap
static constexpr size_t   kShrinkKeepBytes  = 4ULL * 1024ULL * 1024ULL;   // release spike buffers > 4 MB
static constexpr uint32_t kMaxDumpNodes     = 2500000u;                   // stops exponential shared DAGs in O(1)
static constexpr int      kMaxDumpDepth     = 128;

enum class Status {
    Ok = 0,
    FallbackNeeded = 1,
    CorruptNumber = 2,
};

// Memory-safe growable buffer: EVERY mutation verifies capacity & returns bool.
// Under OOM, overflow, or > kMaxDumpBytes, it sets oom=true and NEVER writes out of bounds.
struct GrowBuf {
    char*  data = nullptr;
    size_t len  = 0;
    size_t cap  = 0;
    size_t maxBytes = kMaxDumpBytes;
    bool   oom  = false;

    bool Reserve(size_t want) {
        if (oom) return false;
        if (want > maxBytes) {
            oom = true;
            return false;
        }
        if (want <= cap) return true;
        size_t ncap = cap ? cap : 65536;
        while (ncap < want) {
            if (ncap > maxBytes / 2) {
                ncap = maxBytes;
                break;
            }
            ncap *= 2;
        }
        if (ncap < want) {
            oom = true;
            return false;
        }
        char* p = static_cast<char*>(std::realloc(data, ncap));
        if (!p) {
            oom = true;
            return false;
        }
        data = p;
        cap = ncap;
        return true;
    }

    void Clear() {
        len = 0;
        oom = false;
    }

    void ShrinkIfLarge(size_t keepLimit = kShrinkKeepBytes) {
        len = 0;
        oom = false;
        if (cap > keepLimit) {
            std::free(data);
            data = nullptr;
            cap = 0;
        }
    }

    bool PushChar(char c) {
        if (oom || len >= maxBytes) {
            oom = true;
            return false;
        }
        if (len + 1 > cap && !Reserve(len + 1)) return false;
        data[len++] = c;
        return true;
    }

    bool Append(const char* s, size_t n) {
        if (n == 0) return !oom;
        if (oom || n > maxBytes - len) {
            oom = true;
            return false;
        }
        if (len + n > cap && !Reserve(len + n)) return false;
        std::memcpy(data + len, s, n);
        len += n;
        return true;
    }

    bool AppendLit(const char* s) { return Append(s, std::strlen(s)); }
    ~GrowBuf() { std::free(data); }
};

inline bool IsAsciiAlpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
inline bool IsAsciiAlnum(unsigned char c) {
    return IsAsciiAlpha(c) || (c >= '0' && c <= '9');
}

inline bool IsLuaReserved(const char* s, size_t n) {
    switch (n) {
        case 2:
            return std::memcmp(s, "do", 2) == 0 || std::memcmp(s, "if", 2) == 0 ||
                   std::memcmp(s, "in", 2) == 0 || std::memcmp(s, "or", 2) == 0;
        case 3:
            return std::memcmp(s, "and", 3) == 0 || std::memcmp(s, "end", 3) == 0 ||
                   std::memcmp(s, "for", 3) == 0 || std::memcmp(s, "nil", 3) == 0 ||
                   std::memcmp(s, "not", 3) == 0;
        case 4:
            return std::memcmp(s, "else", 4) == 0 || std::memcmp(s, "then", 4) == 0 ||
                   std::memcmp(s, "true", 4) == 0;
        case 5:
            return std::memcmp(s, "break", 5) == 0 || std::memcmp(s, "false", 5) == 0 ||
                   std::memcmp(s, "local", 5) == 0 || std::memcmp(s, "until", 5) == 0 ||
                   std::memcmp(s, "while", 5) == 0;
        case 6:
            return std::memcmp(s, "elseif", 6) == 0 || std::memcmp(s, "repeat", 6) == 0 ||
                   std::memcmp(s, "return", 6) == 0;
        case 8:
            return std::memcmp(s, "function", 8) == 0;
        default:
            return false;
    }
}

inline bool IsPlainLuaIdent(const char* s, size_t n) {
    if (n == 0) return false;
    if (!IsAsciiAlpha(static_cast<unsigned char>(s[0]))) return false;
    for (size_t i = 1; i < n; ++i) {
        if (!IsAsciiAlnum(static_cast<unsigned char>(s[i]))) return false;
    }
    return !IsLuaReserved(s, n);
}

inline bool AppendQuotedString(GrowBuf& out, const char* s, size_t n) {
    if (n > out.maxBytes || out.len > out.maxBytes - n - 4) {
        out.oom = true;
        return false;
    }
    if (!out.Reserve(out.len + n + 4)) return false;
    if (!out.PushChar('"')) return false;
    const char* seg = s;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\0') {
            if (s + i > seg && !out.Append(seg, static_cast<size_t>((s + i) - seg))) return false;
            bool ok = true;
            if (c == '"')       ok = out.Append("\\\"", 2);
            else if (c == '\\') ok = out.Append("\\\\", 2);
            else if (c == '\n') ok = out.Append("\\n", 2);
            else if (c == '\r') ok = out.Append("\\r", 2);
            else {
                // Escape NUL as \000 so following digits cannot be swallowed by Lua lexer
                ok = out.Append("\\000", 4);
            }
            if (!ok) return false;
            seg = s + i + 1;
        }
    }
    if (s + n > seg && !out.Append(seg, static_cast<size_t>((s + n) - seg))) return false;
    return out.PushChar('"');
}

inline Status AppendNumber(GrowBuf& out, double d) {
    if (std::isnan(d) || std::isinf(d)) {
        return Status::CorruptNumber;
    }
    if (d == 0.0) {
        return out.PushChar('0') ? Status::Ok : Status::FallbackNeeded;
    }
    // Fast exact integer path (covers GUIDs, counts, ticks, coordinates, etc.)
    if (d >= -9007199254740992.0 && d <= 9007199254740992.0) {
        int64_t iv = static_cast<int64_t>(d);
        if (static_cast<double>(iv) == d) {
            char tmp[32];
            char* p = tmp + sizeof(tmp);
            uint64_t u = (iv < 0) ? static_cast<uint64_t>(-iv) : static_cast<uint64_t>(iv);
            do {
                *--p = static_cast<char>('0' + (u % 10));
                u /= 10;
            } while (u != 0);
            if (iv < 0) *--p = '-';
            return out.Append(p, static_cast<size_t>((tmp + sizeof(tmp)) - p))
                ? Status::Ok : Status::FallbackNeeded;
        }
    }
    // Locale-independent C++17 std::to_chars (immune to LC_NUMERIC comma decimal separator!)
    // Exact 14-digit general format parity with Lua 5.1 LUA_NUMBER_FMT ("%.14g").
    char tmp[64];
    auto res = std::to_chars(tmp, tmp + sizeof(tmp), d, std::chars_format::general, 14);
    if (res.ec != std::errc{}) {
        return Status::FallbackNeeded;
    }
    return out.Append(tmp, static_cast<size_t>(res.ptr - tmp))
        ? Status::Ok : Status::FallbackNeeded;
}

struct DumpCtx {
    std::unordered_set<const void*> activeTables;
    uint32_t visitedNodes = 0;
    uint32_t maxNodes = kMaxDumpNodes;
    const char* failReason = nullptr;
};

inline Status DumpValue(void* L, const LuaApiTable& api, int idx, GrowBuf& out, DumpCtx& ctx, int depth) {
    if (out.oom) { ctx.failReason = "buffer_limit_exceeded"; return Status::FallbackNeeded; }
    if (depth > kMaxDumpDepth) { ctx.failReason = "depth_limit"; return Status::FallbackNeeded; }
    if (++ctx.visitedNodes > ctx.maxNodes) { ctx.failReason = "node_limit_exceeded"; return Status::FallbackNeeded; }

    int t = api.type(L, idx);
    switch (t) {
        case HC_LUA_TNIL:
            return out.Append("nil", 3) ? Status::Ok : Status::FallbackNeeded;
        case HC_LUA_TBOOLEAN:
            return (api.toboolean(L, idx) ? out.Append("true", 4) : out.Append("false", 5))
                ? Status::Ok : Status::FallbackNeeded;
        case HC_LUA_TNUMBER: {
            Status ns = AppendNumber(out, api.tonumber(L, idx));
            if (ns == Status::CorruptNumber) ctx.failReason = "corrupt_number";
            return ns;
        }
        case HC_LUA_TSTRING: {
            size_t slen = 0;
            const char* s = api.tolstring(L, idx, &slen);
            if (!s) return Status::FallbackNeeded;
            return AppendQuotedString(out, s, slen) ? Status::Ok : Status::FallbackNeeded;
        }
        case HC_LUA_TTABLE: {
            if (!api.checkstack(L, 4)) { ctx.failReason = "stack_limit"; return Status::FallbackNeeded; }
            int abs_idx = (idx < 0 && idx > HC_LUA_GLOBALSINDEX) ? (api.gettop(L) + idx + 1) : idx;

            // O(1) active-path cycle detection (catches single- and multi-branch cycles instantly!)
            const void* tptr = api.topointer(L, abs_idx);
            if (!tptr || !ctx.activeTables.insert(tptr).second) {
                ctx.failReason = "cycle_detected";
                return Status::FallbackNeeded;
            }
            struct ActiveGuard {
                std::unordered_set<const void*>& set;
                const void* ptr;
                ~ActiveGuard() { set.erase(ptr); }
            } guard{ctx.activeTables, tptr};

            if (!out.PushChar('{')) return Status::FallbackNeeded;
            int64_t numidx = 1;
            bool has_elem = false;

            api.pushnil(L);
            while (api.next(L, abs_idx) != 0) {
                // Stack: ..., key (-2), val (-1)
                int kt = api.type(L, -2);
                bool emit_key = true;

                if (kt == HC_LUA_TNUMBER) {
                    double kd = api.tonumber(L, -2);
                    if (kd == static_cast<double>(numidx)) {
                        numidx++;
                        emit_key = false;
                    } else {
                        if (!out.PushChar('[')) {
                            api.settop(L, api.gettop(L) - 2);
                            return Status::FallbackNeeded;
                        }
                        Status ks = AppendNumber(out, kd);
                        if (ks != Status::Ok || !out.Append("]=", 2)) {
                            api.settop(L, api.gettop(L) - 2);
                            return (ks != Status::Ok) ? ks : Status::FallbackNeeded;
                        }
                        emit_key = false;
                    }
                } else if (kt == HC_LUA_TSTRING) {
                    size_t klen = 0;
                    const char* ks = api.tolstring(L, -2, &klen);
                    if (!ks) {
                        api.settop(L, api.gettop(L) - 2);
                        return Status::FallbackNeeded;
                    }
                    bool ok = true;
                    if (IsPlainLuaIdent(ks, klen)) {
                        ok = out.Append(ks, klen) && out.PushChar('=');
                    } else {
                        ok = out.PushChar('[') && AppendQuotedString(out, ks, klen) && out.Append("]=", 2);
                    }
                    if (!ok) {
                        api.settop(L, api.gettop(L) - 2);
                        return Status::FallbackNeeded;
                    }
                    emit_key = false;
                } else if (kt == HC_LUA_TBOOLEAN) {
                    bool ok = api.toboolean(L, -2) ? out.Append("[true]=", 7) : out.Append("[false]=", 8);
                    if (!ok) {
                        api.settop(L, api.gettop(L) - 2);
                        return Status::FallbackNeeded;
                    }
                    emit_key = false;
                }

                if (emit_key) {
                    // Unsupported key type (table/function/userdata key) -> safe fallback
                    api.settop(L, api.gettop(L) - 2);
                    return Status::FallbackNeeded;
                }

                Status vs = DumpValue(L, api, -1, out, ctx, depth + 1);
                if (vs != Status::Ok) {
                    api.settop(L, api.gettop(L) - 2);
                    return vs;
                }
                if (!out.PushChar(',')) {
                    api.settop(L, api.gettop(L) - 2);
                    return Status::FallbackNeeded;
                }
                has_elem = true;

                // Pop value, keep key for next lua_next
                api.settop(L, api.gettop(L) - 1);
            }

            if (has_elem && out.len > 0 && out.data[out.len - 1] == ',') {
                out.len--;
            }
            return out.PushChar('}') ? Status::Ok : Status::FallbackNeeded;
        }
        default:
            // function / userdata / thread / lightuserdata -> let vanilla DataDumper handle it
            return Status::FallbackNeeded;
    }
}

inline Status DumpRoot(void* L, const LuaApiTable& api, int val_idx, const char* varname, size_t varlen, GrowBuf& out, const char** outReason = nullptr) {
    out.Clear();
    bool ok = true;
    if (!varname) {
        ok = out.Append("return ", 7);
    } else if (IsPlainLuaIdent(varname, varlen)) {
        ok = out.Append(varname, varlen) && out.Append(" = ", 3);
    } else {
        ok = out.Append(varname, varlen);
    }
    if (!ok) {
        if (outReason) *outReason = out.oom ? "buffer_limit_exceeded" : "fallback";
        return Status::FallbackNeeded;
    }
    DumpCtx ctx;
    ctx.activeTables.reserve(64);
    Status st = DumpValue(L, api, val_idx, out, ctx, 0);
    if (outReason) {
        if (st == Status::Ok) *outReason = nullptr;
        else if (ctx.failReason) *outReason = ctx.failReason;
        else if (st == Status::CorruptNumber) *outReason = "corrupt_number";
        else if (out.oom) *outReason = "buffer_limit_exceeded";
        else *outReason = "fallback";
    }
    return st;
}

} // namespace fast_dumper
