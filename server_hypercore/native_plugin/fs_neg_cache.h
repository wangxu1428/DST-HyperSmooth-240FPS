#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace hs_fs_neg {

static constexpr size_t kTableCap  = 16384; // must be power of 2
static constexpr size_t kMaxPathLen = 512;

struct Entry {
    uint64_t h1        = 0;
    uint64_t h2        = 0;
    uint64_t expireQpc = 0;
    uint32_t epoch     = 0;
};

static inline wchar_t ToLowerNorm(wchar_t c) {
    if (c == L'/') return L'\\';
    if (c >= L'A' && c <= L'Z') return static_cast<wchar_t>(c + (L'a' - L'A'));
    return c;
}

static inline bool ContainsSubstr(const wchar_t* hay, size_t hayLen, const wchar_t* needle, size_t nLen) {
    if (!hay || !needle || nLen == 0 || hayLen < nLen) return false;
    const size_t limit = hayLen - nLen;
    for (size_t i = 0; i <= limit; ++i) {
        if (hay[i] == needle[0] && std::wmemcmp(hay + i, needle, nLen) == 0) {
            return true;
        }
    }
    return false;
}

static inline bool EndsWithExt(const wchar_t* s, size_t len, const wchar_t* ext, size_t extLen) {
    return len >= extLen && std::wmemcmp(s + (len - extLen), ext, extLen) == 0;
}

// Strict Fail-Closed Read-Only Mod/Data Asset Path Allowlist:
//   - Rejects null, empty, >512 chars, UNC/device oddities without mod/data anchor.
//   - REQUIRES \mods\, \data\, mods\, or data\ prefix/segment.
//   - REJECTS ANY mutable/config/user/binary path segment (klei, save, backup, modoverrides,
//     modsettings, cluster, profile, .tmp, bin64, log).
//   - REQUIRES an immutable read-only game/mod asset extension.
inline bool IsCacheableReadOnlyAssetPath(const wchar_t* rawPath, uint64_t* outH1 = nullptr, uint64_t* outH2 = nullptr) {
    if (!rawPath || rawPath[0] == L'\0') return false;

    wchar_t buf[kMaxPathLen + 1];
    size_t len = 0;
    for (; rawPath[len] != L'\0'; ++len) {
        if (len >= kMaxPathLen) return false;
        buf[len] = ToLowerNorm(rawPath[len]);
    }
    if (len < 6) return false;
    buf[len] = L'\0';

    const bool hasRootAnchor =
        ContainsSubstr(buf, len, L"\\mods\\", 6) ||
        ContainsSubstr(buf, len, L"\\data\\", 6) ||
        (len >= 5 && std::wmemcmp(buf, L"mods\\", 5) == 0) ||
        (len >= 5 && std::wmemcmp(buf, L"data\\", 5) == 0);
    if (!hasRootAnchor) return false;

    struct Forbidden { const wchar_t* tok; size_t len; };
    static constexpr Forbidden kForbidden[] = {
        { L"klei",         4 },
        { L"save",         4 },
        { L"backup",       6 },
        { L"modoverrides", 12 },
        { L"modsettings",  11 },
        { L"cluster",      7 },
        { L"profile",      7 },
        { L".tmp",         4 },
        { L"bin64",        5 },
        { L"log",          3 },
    };
    for (const auto& f : kForbidden) {
        if (ContainsSubstr(buf, len, f.tok, f.len)) return false;
    }

    struct AllowedExt { const wchar_t* ext; size_t len; };
    static constexpr AllowedExt kExts[] = {
        { L".tex",   4 },
        { L".xml",   4 },
        { L".zip",   4 },
        { L".bin",   4 },
        { L".ksh",   4 },
        { L".fsb",   4 },
        { L".fev",   4 },
        { L".dyn",   4 },
        { L".png",   4 },
        { L".lua",   4 },
        { L".scml",  5 },
        { L".anim",  5 },
        { L".build", 6 },
    };
    bool extOk = false;
    for (const auto& e : kExts) {
        if (EndsWithExt(buf, len, e.ext, e.len)) {
            extOk = true;
            break;
        }
    }
    if (!extOk) return false;

    if (outH1 || outH2) {
        uint64_t h1 = 14695981039346656037ULL;
        uint64_t h2 = 0x9E3779B97F4A7C15ULL;
        for (size_t i = 0; i < len; ++i) {
            const uint64_t ch = static_cast<uint16_t>(buf[i]);
            h1 ^= ch;
            h1 *= 1099511628211ULL;
            h2 ^= ch + 0x9E3779B97F4A7C15ULL + (h2 << 6) + (h2 >> 2);
            h2 *= 0xBF58476D1CE4E5B9ULL;
        }
        h1 ^= (h1 >> 33);
        h1 *= 0xFF51AFD7ED558CCDULL;
        h1 ^= (h1 >> 33);
        h1 |= 1ULL; // reserve 0 as empty sentinel
        h2 ^= (h2 >> 30);
        h2 *= 0x94D049BB133111EBULL;
        h2 ^= (h2 >> 31);
        if (outH1) *outH1 = h1;
        if (outH2) *outH2 = h2;
    }
    return true;
}

class NegativeStatCache {
public:
#ifdef _WIN32
    SRWLOCK lock = SRWLOCK_INIT;
#endif
    std::atomic<uint32_t> epoch{1};
    Entry* slots = nullptr; // lazily heap-allocated on first Insert so DLL image stays compact & zero non-DST overhead!

    NegativeStatCache() = default;
    ~NegativeStatCache() {
        std::free(slots);
        slots = nullptr;
    }
    NegativeStatCache(const NegativeStatCache&) = delete;
    NegativeStatCache& operator=(const NegativeStatCache&) = delete;

    void InvalidateAll() {
        epoch.fetch_add(1, std::memory_order_release);
    }

    bool Lookup(uint64_t h1, uint64_t h2, uint64_t nowTick) {
        const uint32_t curEp = epoch.load(std::memory_order_acquire);
        const size_t base = static_cast<size_t>(h1) & (kTableCap - 1);
#ifdef _WIN32
        AcquireSRWLockShared(&lock);
#endif
        bool hit = false;
        if (slots) {
            for (size_t p = 0; p < 4; ++p) {
                const Entry& e = slots[(base + p) & (kTableCap - 1)];
                if (e.h1 == 0) break;
                if (e.h1 == h1 && e.h2 == h2 && e.epoch == curEp && nowTick < e.expireQpc) {
                    hit = true;
                    break;
                }
            }
        }
#ifdef _WIN32
        ReleaseSRWLockShared(&lock);
#endif
        return hit;
    }

    void Insert(uint64_t h1, uint64_t h2, uint64_t nowTick, uint64_t ttlTicks) {
        const uint32_t curEp = epoch.load(std::memory_order_acquire);
        const uint64_t exp = nowTick + ttlTicks;
        const size_t base = static_cast<size_t>(h1) & (kTableCap - 1);
#ifdef _WIN32
        AcquireSRWLockExclusive(&lock);
#endif
        if (!slots) {
            slots = static_cast<Entry*>(std::calloc(kTableCap, sizeof(Entry)));
        }
        if (slots) {
            Entry* target = &slots[base];
            for (size_t p = 0; p < 4; ++p) {
                Entry& e = slots[(base + p) & (kTableCap - 1)];
                if (e.h1 == 0 || (e.h1 == h1 && e.h2 == h2) || e.epoch != curEp || e.expireQpc <= nowTick) {
                    target = &e;
                    break;
                }
            }
            target->h1 = h1;
            target->h2 = h2;
            target->expireQpc = exp;
            target->epoch = curEp;
        }
#ifdef _WIN32
        ReleaseSRWLockExclusive(&lock);
#endif
    }
};

} // namespace hs_fs_neg
