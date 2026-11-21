#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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

    bool ReadyForFastDump() const {
        return checkstack && gettop && settop && type && tonumber && toboolean &&
               tolstring && pushnil && pushlstring && pushstring && pushboolean && next;
    }
    bool ReadyForRegister() const {
        return pushcclosure && setfield && createtable && pushnumber && pushstring && pushboolean;
    }
};

namespace fast_dumper {

enum class Status {
    Ok = 0,
    FallbackNeeded = 1,
    CorruptNumber = 2,
};

struct GrowBuf {
    char*  data = nullptr;
    size_t len  = 0;
    size_t cap  = 0;

    void Reserve(size_t want) {
        if (want <= cap) return;
        size_t ncap = cap ? cap * 2 : 65536;
        while (ncap < want) ncap *= 2;
        char* p = static_cast<char*>(std::realloc(data, ncap));
        if (p) {
            data = p;
            cap = ncap;
        }
    }
    void Clear() { len = 0; }
    void PushChar(char c) {
        if (len + 1 > cap) Reserve(len + 1);
        data[len++] = c;
    }
    void Append(const char* s, size_t n) {
        if (n == 0) return;
        if (len + n > cap) Reserve(len + n);
        std::memcpy(data + len, s, n);
        len += n;
    }
    void AppendLit(const char* s) { Append(s, std::strlen(s)); }
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

inline void AppendQuotedString(GrowBuf& out, const char* s, size_t n) {
    out.Reserve(out.len + n + 4);
    out.PushChar('"');
    const char* seg = s;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '"' || c == '\\' || c == '\n' || c == '\r' || c == '\0') {
            if (s + i > seg) out.Append(seg, static_cast<size_t>((s + i) - seg));
            if (c == '"')       out.Append("\\\"", 2);
            else if (c == '\\') out.Append("\\\\", 2);
            else if (c == '\n') out.Append("\\n", 2);
            else if (c == '\r') out.Append("\\r", 2);
            else {
                // Escape NUL as \000 so following digits cannot be swallowed by Lua lexer
                out.Append("\\000", 4);
            }
            seg = s + i + 1;
        }
    }
    if (s + n > seg) out.Append(seg, static_cast<size_t>((s + n) - seg));
    out.PushChar('"');
}

inline Status AppendNumber(GrowBuf& out, double d) {
    if (std::isnan(d) || std::isinf(d)) {
        return Status::CorruptNumber;
    }
    if (d == 0.0) {
        // Normalize -0.0 and 0.0 cleanly (avoid "-0" edge noise while matching numeric value)
        out.PushChar('0');
        return Status::Ok;
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
            out.Append(p, static_cast<size_t>((tmp + sizeof(tmp)) - p));
            return Status::Ok;
        }
    }
    // Exact Lua 5.1 LUA_NUMBER_FMT ("%.14g")
    char tmp[48];
    int written = std::snprintf(tmp, sizeof(tmp), "%.14g", d);
    if (written <= 0 || written >= static_cast<int>(sizeof(tmp))) {
        return Status::FallbackNeeded;
    }
    out.Append(tmp, static_cast<size_t>(written));
    return Status::Ok;
}

inline Status DumpValue(void* L, const LuaApiTable& api, int idx, GrowBuf& out, int depth) {
    if (depth > 128) return Status::FallbackNeeded;

    int t = api.type(L, idx);
    switch (t) {
        case HC_LUA_TNIL:
            out.Append("nil", 3);
            return Status::Ok;
        case HC_LUA_TBOOLEAN:
            if (api.toboolean(L, idx)) out.Append("true", 4);
            else                       out.Append("false", 5);
            return Status::Ok;
        case HC_LUA_TNUMBER:
            return AppendNumber(out, api.tonumber(L, idx));
        case HC_LUA_TSTRING: {
            size_t slen = 0;
            const char* s = api.tolstring(L, idx, &slen);
            if (!s) return Status::FallbackNeeded;
            AppendQuotedString(out, s, slen);
            return Status::Ok;
        }
        case HC_LUA_TTABLE: {
            if (!api.checkstack(L, 4)) return Status::FallbackNeeded;
            // Convert negative index to absolute before pushing keys
            int abs_idx = (idx < 0 && idx > HC_LUA_GLOBALSINDEX) ? (api.gettop(L) + idx + 1) : idx;
            out.PushChar('{');
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
                        out.PushChar('[');
                        Status ks = AppendNumber(out, kd);
                        if (ks != Status::Ok) {
                            api.settop(L, api.gettop(L) - 2);
                            return ks;
                        }
                        out.Append("]=", 2);
                        emit_key = false;
                    }
                } else if (kt == HC_LUA_TSTRING) {
                    size_t klen = 0;
                    const char* ks = api.tolstring(L, -2, &klen);
                    if (!ks) {
                        api.settop(L, api.gettop(L) - 2);
                        return Status::FallbackNeeded;
                    }
                    if (IsPlainLuaIdent(ks, klen)) {
                        out.Append(ks, klen);
                        out.PushChar('=');
                    } else {
                        out.PushChar('[');
                        AppendQuotedString(out, ks, klen);
                        out.Append("]=", 2);
                    }
                    emit_key = false;
                } else if (kt == HC_LUA_TBOOLEAN) {
                    if (api.toboolean(L, -2)) out.Append("[true]=", 7);
                    else                      out.Append("[false]=", 8);
                    emit_key = false;
                }

                if (emit_key) {
                    // Unsupported key type (table/function/userdata key) -> safe fallback
                    api.settop(L, api.gettop(L) - 2);
                    return Status::FallbackNeeded;
                }

                Status vs = DumpValue(L, api, -1, out, depth + 1);
                if (vs != Status::Ok) {
                    api.settop(L, api.gettop(L) - 2);
                    return vs;
                }
                out.PushChar(',');
                has_elem = true;

                // Pop value, keep key for next lua_next
                api.settop(L, api.gettop(L) - 1);
            }

            if (has_elem && out.len > 0 && out.data[out.len - 1] == ',') {
                out.len--;
            }
            out.PushChar('}');
            return Status::Ok;
        }
        default:
            // function / userdata / thread / lightuserdata -> let vanilla DataDumper handle it
            return Status::FallbackNeeded;
    }
}

inline Status DumpRoot(void* L, const LuaApiTable& api, int val_idx, const char* varname, size_t varlen, GrowBuf& out) {
    out.Clear();
    if (!varname) {
        out.Append("return ", 7);
    } else if (IsPlainLuaIdent(varname, varlen)) {
        out.Append(varname, varlen);
        out.Append(" = ", 3);
    } else {
        out.Append(varname, varlen);
    }
    return DumpValue(L, api, val_idx, out, 0);
}

} // namespace fast_dumper
