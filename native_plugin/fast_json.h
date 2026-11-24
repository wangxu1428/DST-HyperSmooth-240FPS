#pragma once

#include "fast_dumper.h"

#include <charconv>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_set>

namespace fast_json {

static constexpr size_t   kMaxJsonBytes = fast_dumper::kMaxDumpBytes;
static constexpr uint32_t kMaxJsonNodes = fast_dumper::kMaxDumpNodes;
static constexpr int      kMaxJsonDepth = fast_dumper::kMaxDumpDepth;

struct JsonOutBuf {
    char   inlineBuf[4096];
    char*  data     = inlineBuf;
    size_t len      = 0;
    size_t cap      = sizeof(inlineBuf);
    size_t maxBytes = kMaxJsonBytes;
    bool   onHeap   = false;
    bool   oom      = false;

    ~JsonOutBuf() {
        if (onHeap && data) {
            std::free(data);
        }
    }

    bool Reserve(size_t want) noexcept {
        if (oom) return false;
        if (want > maxBytes) {
            oom = true;
            return false;
        }
        if (want <= cap) return true;
        size_t ncap = cap ? cap : sizeof(inlineBuf);
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
        char* p = nullptr;
        if (onHeap) {
            p = static_cast<char*>(std::realloc(data, ncap));
        } else {
            p = static_cast<char*>(std::malloc(ncap));
            if (p && len > 0) {
                std::memcpy(p, inlineBuf, len);
            }
        }
        if (!p) {
            oom = true;
            return false;
        }
        data = p;
        cap = ncap;
        onHeap = true;
        return true;
    }

    bool PushChar(char c) noexcept {
        if (oom || len >= maxBytes) {
            oom = true;
            return false;
        }
        if (len + 1 > cap && !Reserve(len + 1)) return false;
        data[len++] = c;
        return true;
    }

    bool Append(const char* s, size_t n) noexcept {
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
};

inline bool AppendJsonNumber(JsonOutBuf& out, double d) noexcept {
    if (std::isnan(d) || std::isinf(d)) return false;
    if (d == 0.0) return out.PushChar('0');
    if (d >= -9007199254740992.0 && d <= 9007199254740992.0) {
        int64_t iv = static_cast<int64_t>(d);
        if (static_cast<double>(iv) == d) {
            char tmp[32];
            char* p = tmp + sizeof(tmp);
            uint64_t u = (iv < 0) ? (0ULL - static_cast<uint64_t>(iv)) : static_cast<uint64_t>(iv);
            do {
                *--p = static_cast<char>('0' + (u % 10));
                u /= 10;
            } while (u != 0);
            if (iv < 0) *--p = '-';
            return out.Append(p, static_cast<size_t>((tmp + sizeof(tmp)) - p));
        }
    }
    char tmp[64];
    auto res = std::to_chars(tmp, tmp + sizeof(tmp), d, std::chars_format::general, 14);
    if (res.ec != std::errc{}) return false;
    return out.Append(tmp, static_cast<size_t>(res.ptr - tmp));
}

// Parity with DST scripts/json.lua encodeString vs encodeString_compliant:
// Escapes \, ", \n, \t, \r, plus ' when compliant == false.
inline bool AppendJsonString(JsonOutBuf& out, const char* s, size_t n, bool escapeSingleQuote) noexcept {
    if (out.maxBytes < 2 || n > out.maxBytes - 2 || out.len > out.maxBytes - n - 2) {
        out.oom = true;
        return false;
    }
    if (!out.Reserve(out.len + n + 2)) return false;
    if (!out.PushChar('"')) return false;
    const char* seg = s;
    for (size_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const char* esc = nullptr;
        switch (c) {
            case '\\': esc = "\\\\"; break;
            case '"':  esc = "\\\""; break;
            case '\n': esc = "\\n";  break;
            case '\t': esc = "\\t";  break;
            case '\r': esc = "\\r";  break;
            case '\'': if (escapeSingleQuote) esc = "\\'"; break;
            default: break;
        }
        if (esc) {
            if (s + i > seg && !out.Append(seg, static_cast<size_t>((s + i) - seg))) return false;
            if (!out.Append(esc, 2)) return false;
            seg = s + i + 1;
        }
    }
    if (s + n > seg && !out.Append(seg, static_cast<size_t>((s + n) - seg))) return false;
    return out.PushChar('"');
}

struct EncodeCtx {
    std::unordered_set<const void*> activeTables;
    uint32_t visitedNodes = 0;
};

inline bool IsEncodableLuaType(int t) noexcept {
    return t == HC_LUA_TNIL || t == HC_LUA_TBOOLEAN || t == HC_LUA_TNUMBER ||
           t == HC_LUA_TSTRING || t == HC_LUA_TTABLE;
}

inline fast_dumper::Status EncodeValue(void* L, const LuaApiTable& api, int idx,
                                       JsonOutBuf& out, EncodeCtx& ctx, int depth, bool compliant) {
    if (out.oom || depth > kMaxJsonDepth || ++ctx.visitedNodes > kMaxJsonNodes) {
        return fast_dumper::Status::FallbackNeeded;
    }
    const int t = api.type(L, idx);
    switch (t) {
        case HC_LUA_TNIL:
            return out.Append("null", 4) ? fast_dumper::Status::Ok : fast_dumper::Status::FallbackNeeded;
        case HC_LUA_TBOOLEAN:
            return (api.toboolean(L, idx) ? out.Append("true", 4) : out.Append("false", 5))
                ? fast_dumper::Status::Ok : fast_dumper::Status::FallbackNeeded;
        case HC_LUA_TNUMBER:
            return AppendJsonNumber(out, api.tonumber(L, idx))
                ? fast_dumper::Status::Ok : fast_dumper::Status::FallbackNeeded;
        case HC_LUA_TSTRING: {
            size_t slen = 0;
            const char* s = api.tolstring(L, idx, &slen);
            if (!s) return fast_dumper::Status::FallbackNeeded;
            return AppendJsonString(out, s, slen, !compliant)
                ? fast_dumper::Status::Ok : fast_dumper::Status::FallbackNeeded;
        }
        case HC_LUA_TTABLE: {
            if (!api.checkstack(L, 6)) return fast_dumper::Status::FallbackNeeded;
            const int absIdx = (idx < 0 && idx > HC_LUA_GLOBALSINDEX) ? (api.gettop(L) + idx + 1) : idx;
            const void* tptr = api.topointer(L, absIdx);
            if (!tptr || !ctx.activeTables.insert(tptr).second) {
                return fast_dumper::Status::FallbackNeeded;
            }
            struct ActiveGuard {
                std::unordered_set<const void*>& s;
                const void* p;
                ~ActiveGuard() { s.erase(p); }
            } guard{ctx.activeTables, tptr};

            // Pass 1: classify table (empty -> "[]", dense sequential array, or object)
            uint64_t count = 0;
            uint64_t maxIndex = 0;
            bool seqDense = true;
            bool hasObjectKey = false;

            const int baseTop = api.gettop(L);
            api.pushnil(L);
            while (api.next(L, absIdx) != 0) {
                const int kt = api.type(L, -2);
                const int vt = api.type(L, -1);
                if (!IsEncodableLuaType(vt)) {
                    // e.g. json.null or unsupported type -> let json.lua handle cleanly
                    api.settop(L, baseTop);
                    return fast_dumper::Status::FallbackNeeded;
                }
                if (kt == HC_LUA_TNUMBER) {
                    const double kd = api.tonumber(L, -2);
                    if (std::isnan(kd) || std::isinf(kd)) {
                        api.settop(L, baseTop);
                        return fast_dumper::Status::FallbackNeeded;
                    }
                    if (kd >= 1.0 && kd <= 9007199254740992.0 && std::floor(kd) == kd) {
                        const uint64_t ki = static_cast<uint64_t>(kd);
                        if (ki > maxIndex) maxIndex = ki;
                        if (ki != count + 1ULL) seqDense = false;
                    } else {
                        seqDense = false;
                        hasObjectKey = true;
                    }
                } else if (kt == HC_LUA_TSTRING) {
                    size_t klen = 0;
                    const char* ks = api.tolstring(L, -2, &klen);
                    if (!ks || (klen == 1 && ks[0] == 'n')) {
                        // 'n' special-case in json.lua isArray -> delegate to Lua
                        api.settop(L, baseTop);
                        return fast_dumper::Status::FallbackNeeded;
                    }
                    seqDense = false;
                    hasObjectKey = true;
                } else {
                    api.settop(L, baseTop);
                    return fast_dumper::Status::FallbackNeeded;
                }
                ++count;
                api.settop(L, baseTop + 1); // keep key, pop value
            }

            if (count == 0) {
                return out.Append("[]", 2) ? fast_dumper::Status::Ok : fast_dumper::Status::FallbackNeeded;
            }

            if (!hasObjectKey) {
                // Sparse or non-sequential array -> let Lua loop for i=1,maxCount handle nil holes
                if (!seqDense || count != maxIndex) {
                    return fast_dumper::Status::FallbackNeeded;
                }
                if (!out.PushChar('[')) return fast_dumper::Status::FallbackNeeded;
                uint64_t verifyIdx = 0;
                bool first = true;
                api.pushnil(L);
                while (api.next(L, absIdx) != 0) {
                    if (api.type(L, -2) != HC_LUA_TNUMBER ||
                        api.tonumber(L, -2) != static_cast<double>(++verifyIdx)) {
                        api.settop(L, baseTop);
                        return fast_dumper::Status::FallbackNeeded;
                    }
                    if (!first && !out.PushChar(',')) {
                        api.settop(L, baseTop);
                        return fast_dumper::Status::FallbackNeeded;
                    }
                    first = false;
                    if (EncodeValue(L, api, -1, out, ctx, depth + 1, compliant) != fast_dumper::Status::Ok) {
                        api.settop(L, baseTop);
                        return fast_dumper::Status::FallbackNeeded;
                    }
                    api.settop(L, baseTop + 1);
                }
                return out.PushChar(']') ? fast_dumper::Status::Ok : fast_dumper::Status::FallbackNeeded;
            }

            // Object path
            if (!out.PushChar('{')) return fast_dumper::Status::FallbackNeeded;
            bool first = true;
            api.pushnil(L);
            while (api.next(L, absIdx) != 0) {
                if (!first && !out.PushChar(',')) {
                    api.settop(L, baseTop);
                    return fast_dumper::Status::FallbackNeeded;
                }
                first = false;
                const int kt = api.type(L, -2);
                if (kt == HC_LUA_TSTRING) {
                    size_t klen = 0;
                    const char* ks = api.tolstring(L, -2, &klen);
                    if (!ks || !AppendJsonString(out, ks, klen, !compliant)) {
                        api.settop(L, baseTop);
                        return fast_dumper::Status::FallbackNeeded;
                    }
                } else if (kt == HC_LUA_TNUMBER) {
                    if (!out.PushChar('"') || !AppendJsonNumber(out, api.tonumber(L, -2)) || !out.PushChar('"')) {
                        api.settop(L, baseTop);
                        return fast_dumper::Status::FallbackNeeded;
                    }
                } else {
                    api.settop(L, baseTop);
                    return fast_dumper::Status::FallbackNeeded;
                }
                if (!out.PushChar(':')) {
                    api.settop(L, baseTop);
                    return fast_dumper::Status::FallbackNeeded;
                }
                if (EncodeValue(L, api, -1, out, ctx, depth + 1, compliant) != fast_dumper::Status::Ok) {
                    api.settop(L, baseTop);
                    return fast_dumper::Status::FallbackNeeded;
                }
                api.settop(L, baseTop + 1);
            }
            return out.PushChar('}') ? fast_dumper::Status::Ok : fast_dumper::Status::FallbackNeeded;
        }
        default:
            return fast_dumper::Status::FallbackNeeded;
    }
}

inline fast_dumper::Status EncodeRoot(void* L, const LuaApiTable& api, int valIdx,
                                      JsonOutBuf& out, bool compliant) {
    if (!api.checkstack || !api.gettop || !api.settop || !api.type || !api.tonumber ||
        !api.toboolean || !api.tolstring || !api.topointer || !api.pushnil || !api.next) {
        return fast_dumper::Status::FallbackNeeded;
    }
    const int savedTop = api.gettop(L);
    out.len = 0;
    out.oom = false;
    EncodeCtx ctx;
    ctx.activeTables.reserve(32);
    const auto st = EncodeValue(L, api, valIdx, out, ctx, 0, compliant);
    if (st != fast_dumper::Status::Ok) {
        api.settop(L, savedTop);
    }
    return st;
}

inline const char* SkipWs(const char* p, const char* end) noexcept {
    while (p < end) {
        const char c = *p;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            ++p;
        } else {
            break;
        }
    }
    return p;
}

inline fast_dumper::Status DecodeString(void* L, const LuaApiTable& api,
                                        const char*& p, const char* end) {
    if (p >= end || *p != '"') return fast_dumper::Status::FallbackNeeded;
    ++p;
    const char* rawStart = p;
    bool hasEscape = false;
    while (p < end) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '"') {
            if (!hasEscape) {
                api.pushlstring(L, rawStart, static_cast<size_t>(p - rawStart));
                ++p;
                return fast_dumper::Status::Ok;
            }
            break;
        }
        if (c == '\\') {
            hasEscape = true;
            ++p;
            if (p >= end) return fast_dumper::Status::FallbackNeeded;
            const char e = *p;
            // Only unescape sequences with guaranteed Lua 5.1 loadstring parity.
            // \uXXXX, \/, numeric \ddd, \xXX fall back to scripts/json.lua.
            if (e != '"' && e != '\\' && e != '\'' && e != 'n' && e != 'r' &&
                e != 't' && e != 'b' && e != 'f') {
                return fast_dumper::Status::FallbackNeeded;
            }
            ++p;
            continue;
        }
        if (c < 0x20) return fast_dumper::Status::FallbackNeeded;
        ++p;
    }
    if (p >= end || *p != '"') return fast_dumper::Status::FallbackNeeded;

    const char* rawEnd = p;
    ++p; // consume closing '"'

    JsonOutBuf dec;
    const size_t maxOut = static_cast<size_t>(rawEnd - rawStart);
    if (!dec.Reserve(maxOut)) return fast_dumper::Status::FallbackNeeded;
    for (const char* q = rawStart; q < rawEnd; ++q) {
        if (*q != '\\') {
            if (!dec.PushChar(*q)) return fast_dumper::Status::FallbackNeeded;
        } else {
            ++q;
            if (q >= rawEnd) return fast_dumper::Status::FallbackNeeded;
            char outC = *q;
            switch (*q) {
                case '"':  outC = '"';  break;
                case '\\': outC = '\\'; break;
                case '\'': outC = '\''; break;
                case 'n':  outC = '\n'; break;
                case 'r':  outC = '\r'; break;
                case 't':  outC = '\t'; break;
                case 'b':  outC = '\b'; break;
                case 'f':  outC = '\f'; break;
                default:   return fast_dumper::Status::FallbackNeeded;
            }
            if (!dec.PushChar(outC)) return fast_dumper::Status::FallbackNeeded;
        }
    }
    api.pushlstring(L, dec.data, dec.len);
    return fast_dumper::Status::Ok;
}

inline fast_dumper::Status DecodeNumber(void* L, const LuaApiTable& api,
                                        const char*& p, const char* end) {
    const char* start = p;
    if (p < end && *p == '-') ++p;
    if (p >= end || *p < '0' || *p > '9') return fast_dumper::Status::FallbackNeeded;
    if (*p == '0') {
        ++p;
        if (p < end && *p >= '0' && *p <= '9') return fast_dumper::Status::FallbackNeeded;
    } else {
        while (p < end && *p >= '0' && *p <= '9') ++p;
    }
    if (p < end && *p == '.') {
        ++p;
        if (p >= end || *p < '0' || *p > '9') return fast_dumper::Status::FallbackNeeded;
        while (p < end && *p >= '0' && *p <= '9') ++p;
    }
    if (p < end && *p == 'e') {
        ++p;
        if (p < end && (*p == '+' || *p == '-')) ++p;
        if (p >= end || *p < '0' || *p > '9') return fast_dumper::Status::FallbackNeeded;
        while (p < end && *p >= '0' && *p <= '9') ++p;
    }
    // Check next byte wouldn't be swallowed by JSON4Lua decode_scanNumber's "+-0123456789.e" loop
    if (p < end) {
        const char nc = *p;
        if ((nc >= '0' && nc <= '9') || nc == '+' || nc == '-' || nc == '.' || nc == 'e' || nc == 'E') {
            return fast_dumper::Status::FallbackNeeded;
        }
    }

    double val = 0.0;
    auto res = std::from_chars(start, p, val);
    if (res.ec != std::errc{} || res.ptr != p || !std::isfinite(val)) {
        return fast_dumper::Status::FallbackNeeded;
    }
    api.pushnumber(L, val);
    return fast_dumper::Status::Ok;
}

inline fast_dumper::Status DecodeValue(void* L, const LuaApiTable& api,
                                       const char*& p, const char* end, uint32_t& nodes, int depth) {
    if (depth > kMaxJsonDepth || ++nodes > kMaxJsonNodes || !api.checkstack(L, 6)) {
        return fast_dumper::Status::FallbackNeeded;
    }
    p = SkipWs(p, end);
    if (p >= end) return fast_dumper::Status::FallbackNeeded;

    const char c = *p;
    if (c == '{') {
        ++p;
        p = SkipWs(p, end);
        api.createtable(L, 0, 8);
        if (p < end && *p == '}') {
            ++p;
            return fast_dumper::Status::Ok;
        }
        while (true) {
            p = SkipWs(p, end);
            if (p >= end || *p != '"') return fast_dumper::Status::FallbackNeeded;
            if (DecodeString(L, api, p, end) != fast_dumper::Status::Ok) {
                return fast_dumper::Status::FallbackNeeded;
            }
            p = SkipWs(p, end);
            if (p >= end || *p != ':') return fast_dumper::Status::FallbackNeeded;
            ++p;
            if (DecodeValue(L, api, p, end, nodes, depth + 1) != fast_dumper::Status::Ok) {
                return fast_dumper::Status::FallbackNeeded;
            }
            api.rawset(L, -3);
            p = SkipWs(p, end);
            if (p >= end) return fast_dumper::Status::FallbackNeeded;
            if (*p == ',') {
                ++p;
                continue;
            }
            if (*p == '}') {
                ++p;
                return fast_dumper::Status::Ok;
            }
            return fast_dumper::Status::FallbackNeeded;
        }
    }

    if (c == '[') {
        ++p;
        p = SkipWs(p, end);
        api.createtable(L, 8, 0);
        if (p < end && *p == ']') {
            ++p;
            return fast_dumper::Status::Ok;
        }
        int arrIdx = 0;
        while (true) {
            if (arrIdx == INT_MAX) return fast_dumper::Status::FallbackNeeded;
            if (DecodeValue(L, api, p, end, nodes, depth + 1) != fast_dumper::Status::Ok) {
                return fast_dumper::Status::FallbackNeeded;
            }
            ++arrIdx;
            api.rawseti(L, -2, arrIdx);
            p = SkipWs(p, end);
            if (p >= end) return fast_dumper::Status::FallbackNeeded;
            if (*p == ',') {
                ++p;
                continue;
            }
            if (*p == ']') {
                ++p;
                return fast_dumper::Status::Ok;
            }
            return fast_dumper::Status::FallbackNeeded;
        }
    }

    if (c == '"') {
        return DecodeString(L, api, p, end);
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
        return DecodeNumber(L, api, p, end);
    }
    if (c == 't' && (end - p) >= 4 && std::memcmp(p, "true", 4) == 0) {
        api.pushboolean(L, 1);
        p += 4;
        return fast_dumper::Status::Ok;
    }
    if (c == 'f' && (end - p) >= 5 && std::memcmp(p, "false", 5) == 0) {
        api.pushboolean(L, 0);
        p += 5;
        return fast_dumper::Status::Ok;
    }
    if (c == 'n' && (end - p) >= 4 && std::memcmp(p, "null", 4) == 0) {
        api.pushnil(L);
        p += 4;
        return fast_dumper::Status::Ok;
    }
    return fast_dumper::Status::FallbackNeeded;
}

inline fast_dumper::Status DecodeRoot(void* L, const LuaApiTable& api,
                                      const char* s, size_t len, size_t* outNextPos1Based) {
    if (!s || len == 0 || !api.ReadyForFastJson()) {
        return fast_dumper::Status::FallbackNeeded;
    }
    const int savedTop = api.gettop(L);
    const char* p = s;
    const char* end = s + len;
    uint32_t nodes = 0;
    if (DecodeValue(L, api, p, end, nodes, 0) != fast_dumper::Status::Ok) {
        api.settop(L, savedTop);
        return fast_dumper::Status::FallbackNeeded;
    }
    if (outNextPos1Based) {
        *outNextPos1Based = static_cast<size_t>((p - s) + 1);
    }
    return fast_dumper::Status::Ok;
}

} // namespace fast_json
