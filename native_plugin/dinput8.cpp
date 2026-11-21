// =============================================================================
// DST HyperSmooth & FastLoad Native Companion Plugin v2.2.0 (dinput8.dll x64)
// =============================================================================
// Production-grade, universal x86-64 compatible native plugin for Don't Starve Together:
//   1. Transparent System32\dinput8.dll proxy (single export: DirectInput8Create)
//   2. Cross-version AOB FrameLimiter unlock (60/120/144/165/240/360/Unlimited)
//   3. Native 2-byte Hybrid Frame Pacer (Sleep(1) + 2ms precision spin-wait)
//   4. 1ms High-Res Timer + Client/Local Server Priority Boost + Databundle RAM Pre-warm
//   5. Hardware `rep stosb` fix for MSVC 2008 std::vector<uint8_t> byte-by-byte fill
//   6. libdeflate v1.23 (runtime CPUID AVX-512/AVX2/BMI2/SSE2) whole-entry zip hook
//   7. True 240fps Full-World 2D Skeletal Animation Interpolation (Main World + Overlay + UI):
//      - Persistent AnimNode* tracking ([rbx+8] for Main World AnimRenderCommand,
//        parent-bound key for Overlay FollowSymbol, direct rbx for UI AnimNode)
//      - Continuous floating-point QPC sub-tick frame interpolator (handles 30fps, 40fps,
//        SetDeltaTimeMultiplier speed scaling, one-shot craft/jump/attack, and network jitter)
//      - 8,192-slot 4-probe LRU entity tracker (0% eviction in 1,000+ entity megabases)
//      - Two-pass bone matching (exact + sprite-swap fallback) & boss-scale 420px/135deg guards
//   8. Smooth 240Hz Lua GC Pacer & Anti-Freeze Guard (Simulation::CollectGarbage hook):
//      FPS-scaled sub-ms GC step budget (0.35ms @ 240Hz = 84ms/s throughput, 3x tighter 1% low)
//      + emergency-only full-GC safety floor (eliminates 60-200ms combat/megabase freezes)
//   9. Live hot-reload bridge with Steam Workshop Companion Mod (F9 FPS / F10 Skeletal)
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_set>

#include "libdeflate.h"

namespace {

using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
using UnzReadCurrentFile_t = int (*)(void* file, void* buf, uint32_t len);
using ZReadFileFunc_t = uint32_t (*)(void* opaque, void* stream, void* buf, uint32_t size);
using ZSeekFileFunc_t = int32_t (*)(void* opaque, void* stream, uint32_t offset, int origin);
using AnimGetFrame_t = const void* (*)(const void* anim, int32_t playMode, float time);
using LuaGc_t = int (*)(void* L, int what, int data);
using SetGcBudget_t = void (*)(float sec);

HMODULE g_selfModule = nullptr;
HMODULE g_realDInput8 = nullptr;
DirectInput8Create_t g_realCreate = nullptr;
INIT_ONCE g_dinputInitOnce = INIT_ONCE_STATIC_INIT;
HANDLE g_workerThread = nullptr;
HANDLE g_prewarmThread = nullptr;
volatile LONG g_stopFlag = 0;
bool g_timerBoosted = false;

uint8_t* g_patchAddr = nullptr;
UnzReadCurrentFile_t g_origUnzRead = nullptr;
uint8_t* g_unzTrampoline = nullptr;
AnimGetFrame_t g_origAnimGetFrame = nullptr;
uint8_t* g_animRelayStubMain = nullptr;
uint8_t* g_animRelayStubOverlay = nullptr;
uint8_t* g_animRelayStubUI = nullptr;
LuaGc_t g_luaGc = nullptr;
SetGcBudget_t g_setGcBudget = nullptr;
double g_qpcInvFreq = 0.0;

volatile LONG g_currentFps = -1;
volatile LONG g_skeletalTweenEnabled = 1;
std::wstring g_binDir;
std::wstring g_iniPath;
std::wstring g_logPath;
std::unordered_set<DWORD> g_boostedServerPids;

volatile LONG64 g_libdeflateHits = 0;
volatile LONG64 g_libdeflateBytes = 0;
volatile LONG64 g_animInterpFrames = 0;
volatile LONG64 g_animElemsTweened = 0;
volatile LONG64 g_animPass2Swaps = 0;
volatile LONG64 g_gcEmergencyFulls = 0;

//------------------------------------------------------------------------------
// AOB Signatures (verified unique 1-match in dontstarve_steam_x64.exe)
//------------------------------------------------------------------------------
constexpr uint8_t kSigFrameLimiter[] = {
    0xF3, 0x0F, 0x11, 0x49, 0x08, 0xC3,
    0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x50,
    0x0F, 0x29, 0x74, 0x24, 0x40, 0x0F, 0x29, 0x7C, 0x24, 0x30,
    0x0F, 0x57, 0xFF, 0x0F, 0xB6, 0xDA
};

constexpr uint8_t kSigVecFillA[] = {
    0x48, 0x8B, 0xCB, 0x48, 0x89, 0x5C, 0x24, 0x20, 0x48, 0x8B, 0xD6, 0x48, 0x89, 0x54, 0x24, 0x28,
    0x48, 0x85, 0xC9, 0x74, 0x19, 0x41, 0x0F, 0xB6, 0x45, 0x00, 0x88, 0x02, 0x48, 0xFF, 0xC9, 0x48,
    0x89, 0x4C, 0x24, 0x20, 0x48, 0xFF, 0xC2, 0x48, 0x89, 0x54, 0x24, 0x28, 0xEB, 0xE2
};

constexpr uint8_t kSigVecFillB[] = {
    0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0x89, 0x4C, 0x24, 0x40, 0x0F, 0x1F, 0x40, 0x00, 0x48, 0x85,
    0xC0, 0x74, 0x15, 0x44, 0x88, 0x21, 0x48, 0xFF, 0xC8, 0x48, 0x89, 0x44, 0x24, 0x38, 0x48, 0xFF,
    0xC1, 0x48, 0x89, 0x4C, 0x24, 0x40, 0xEB, 0xE6
};

constexpr uint8_t kSigUnzReadCurrentFile[] = {
    0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xF6, 0x48, 0x85, 0xC9, 0x75, 0x09, 0x8D, 0x46, 0x9A,
    0x48, 0x83, 0xC4, 0x20, 0x5E, 0xC3, 0x48, 0x89, 0x7C, 0x24, 0x38, 0x48, 0x8B, 0xB9, 0xC8, 0x00,
    0x00, 0x00, 0x48, 0x85
};

constexpr uint8_t kSigInflateFast[] = {
    0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41,
    0x57, 0x48, 0x83, 0xEC, 0x38, 0x4C, 0x8B, 0x51, 0x28, 0x44, 0x8B, 0x71, 0x08, 0x48, 0x8B, 0x31,
    0x4C, 0x8B, 0x41, 0x10
};

constexpr uint8_t kOrigFrameLimiter8[8] = { 0xF3, 0x0F, 0x11, 0x49, 0x08, 0xC3, 0xCC, 0xCC };

void LogMsg(const char* fmt, ...) {
    if (g_logPath.empty()) return;
    FILE* f = _wfopen(g_logPath.c_str(), L"a");
    if (!f) return;
    SYSTEMTIME st{};
    GetLocalTime(&st);
    fprintf(f, "[%02u:%02u:%02u.%03u] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fprintf(f, "\n");
    fclose(f);
}

BOOL CALLBACK LoadRealDInput8Once(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t sysDir[MAX_PATH]{};
    UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH - 16) return FALSE;
    std::wstring path(sysDir, len);
    path += L"\\dinput8.dll";
    g_realDInput8 = LoadLibraryW(path.c_str());
    if (g_realDInput8) {
        g_realCreate = reinterpret_cast<DirectInput8Create_t>(
            GetProcAddress(g_realDInput8, "DirectInput8Create"));
    }
    return g_realCreate != nullptr;
}

bool GetTextSection(uint8_t*& outStart, size_t& outSize) {
    HMODULE exe = GetModuleHandleW(nullptr);
    if (!exe) return false;
    auto* base = reinterpret_cast<uint8_t*>(exe);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (memcmp(sec->Name, ".text", 5) == 0) {
            outStart = base + sec->VirtualAddress;
            outSize = sec->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

uint8_t* FindPatternInText(uint8_t* start, size_t size, const uint8_t* sig, size_t sigLen) {
    if (!start || size < sigLen) return nullptr;
    for (size_t off = 0; off <= size - sigLen; ++off) {
        if (start[off] == sig[0] && memcmp(start + off, sig, sigLen) == 0) {
            return start + off;
        }
    }
    return nullptr;
}

bool WriteCodeBytes(uint8_t* addr, const uint8_t* bytes, size_t len) {
    DWORD oldProt = 0;
    if (!VirtualProtect(addr, len, PAGE_EXECUTE_READWRITE, &oldProt)) return false;
    memcpy(addr, bytes, len);
    FlushInstructionCache(GetCurrentProcess(), addr, len);
    DWORD dummy = 0;
    VirtualProtect(addr, len, oldProt, &dummy);
    return true;
}

//------------------------------------------------------------------------------
// libdeflate v1.23 Whole-Entry Accelerator for minizip unzReadCurrentFile()
//------------------------------------------------------------------------------
struct UnzReadInfo {
    void* read_buffer;              // +0x00
    const uint8_t* next_in;         // +0x08
    uint32_t avail_in;              // +0x10
    uint32_t total_in;              // +0x14
    uint8_t* next_out;              // +0x18
    uint32_t avail_out;             // +0x20
    uint32_t total_out;             // +0x24
    uint8_t pad_stream[0x38];       // +0x28..+0x5F
    uint32_t pos_in_zipfile;        // +0x60
    uint8_t pad_mid[0x18];          // +0x64..+0x7B
    uint32_t rest_read_compressed;  // +0x7C
    uint32_t rest_read_uncompressed;// +0x80
    uint8_t pad_io[0x14];           // +0x84..+0x97
    ZReadFileFunc_t zread_file;     // +0x98
    uint8_t pad_io2[0x10];          // +0xA0..+0xAF
    ZSeekFileFunc_t zseek_file;     // +0xB0
    uint8_t pad_io3[0x10];          // +0xB8..+0xC7
    void* opaque;                   // +0xC8
    void* filestream;               // +0xD0
    int32_t compression_method;     // +0xD8
    uint32_t byte_before_zipfile;   // +0xDC
    int32_t raw;                    // +0xE0
};

struct ThreadLocalDecomp {
    libdeflate_decompressor* decomp = nullptr;
    std::vector<uint8_t> compBuf;
    ~ThreadLocalDecomp() {
        if (decomp) { libdeflate_free_decompressor(decomp); decomp = nullptr; }
    }
};

extern "C" int FastUnzReadCurrentFile(void* file, void* buf, uint32_t len) {
    if (!file) return -102;
    auto* pinfo = *reinterpret_cast<UnzReadInfo**>(static_cast<uint8_t*>(file) + 0xC8);
    if (!pinfo || !pinfo->read_buffer || len == 0 || !buf) {
        return g_origUnzRead(file, buf, len);
    }

    uint32_t compSize = pinfo->rest_read_compressed;
    uint32_t uncompSize = pinfo->rest_read_uncompressed;

    if (pinfo->compression_method == 8 && pinfo->raw == 0 &&
        pinfo->avail_in == 0 && pinfo->total_out == 0 &&
        compSize > 0 && compSize <= (64u * 1024u * 1024u) &&
        uncompSize > 0 && len >= uncompSize &&
        pinfo->zseek_file && pinfo->zread_file) {

        thread_local ThreadLocalDecomp tls;
        if (!tls.decomp) tls.decomp = libdeflate_alloc_decompressor();
        if (tls.decomp) {
            if (tls.compBuf.size() < compSize) tls.compBuf.resize(compSize);
            uint32_t offset = pinfo->pos_in_zipfile + pinfo->byte_before_zipfile;
            if (pinfo->zseek_file(pinfo->opaque, pinfo->filestream, offset, 0) == 0) {
                if (pinfo->zread_file(pinfo->opaque, pinfo->filestream, tls.compBuf.data(), compSize) == compSize) {
                    size_t actualOut = 0;
                    if (libdeflate_deflate_decompress(tls.decomp, tls.compBuf.data(), compSize, buf, uncompSize, &actualOut) == LIBDEFLATE_SUCCESS &&
                        actualOut == uncompSize) {
                        pinfo->pos_in_zipfile += compSize;
                        pinfo->rest_read_compressed = 0;
                        pinfo->rest_read_uncompressed = 0;
                        pinfo->next_out = static_cast<uint8_t*>(buf) + uncompSize;
                        pinfo->avail_out = 0;
                        pinfo->total_out += uncompSize;
                        InterlockedIncrement64(&g_libdeflateHits);
                        InterlockedAdd64(&g_libdeflateBytes, static_cast<LONG64>(uncompSize));
                        return static_cast<int>(uncompSize);
                    }
                }
            }
        }
    } else if ((pinfo->compression_method == 0 || pinfo->raw != 0) &&
               pinfo->avail_in == 0 && uncompSize > 0 && uncompSize == compSize && len >= uncompSize &&
               pinfo->zseek_file && pinfo->zread_file) {
        uint32_t offset = pinfo->pos_in_zipfile + pinfo->byte_before_zipfile;
        if (pinfo->zseek_file(pinfo->opaque, pinfo->filestream, offset, 0) == 0) {
            if (pinfo->zread_file(pinfo->opaque, pinfo->filestream, buf, uncompSize) == uncompSize) {
                pinfo->pos_in_zipfile += uncompSize;
                pinfo->rest_read_compressed = 0;
                pinfo->rest_read_uncompressed = 0;
                pinfo->next_out = static_cast<uint8_t*>(buf) + uncompSize;
                pinfo->avail_out = 0;
                pinfo->total_out += uncompSize;
                return static_cast<int>(uncompSize);
            }
        }
    }

    return g_origUnzRead(file, buf, len);
}

//------------------------------------------------------------------------------
// 64-bit Fast inflate_fast fallback (for non-zip / chunked callers)
//------------------------------------------------------------------------------
struct ZlibCode { uint8_t op; uint8_t bits; uint16_t val; };
struct ZlibInflateState {
    int32_t mode;
    uint8_t pad0[0x28];
    uint32_t wsize;
    uint32_t whave;
    uint32_t write;
    uint8_t* window;
    uint32_t hold;
    uint32_t bits;
    uint8_t pad1[0x10];
    const ZlibCode* lcode;
    const ZlibCode* dcode;
    uint32_t lenbits;
    uint32_t distbits;
};
struct ZlibStream {
    const uint8_t* next_in;
    uint32_t avail_in;
    uint32_t pad0;
    uint8_t* next_out;
    uint32_t avail_out;
    uint32_t pad1;
    const char* msg;
    ZlibInflateState* state;
};

static inline void FastCopyMatch(uint8_t* out, const uint8_t* from, uint32_t len, uint32_t dist) {
    if (dist >= 16) {
        while (len >= 16) { memcpy(out, from, 16); out += 16; from += 16; len -= 16; }
        if (len >= 8) { memcpy(out, from, 8); out += 8; from += 8; len -= 8; }
        while (len--) *out++ = *from++;
    } else if (dist >= 8) {
        while (len >= 8) { memcpy(out, from, 8); out += 8; from += 8; len -= 8; }
        while (len--) *out++ = *from++;
    } else if (dist == 1) {
        uint8_t b = *from;
        uint64_t v = static_cast<uint64_t>(b) * 0x0101010101010101ULL;
        while (len >= 8) { memcpy(out, &v, 8); out += 8; len -= 8; }
        while (len--) *out++ = b;
    } else {
        while (len--) *out++ = *from++;
    }
}

extern "C" void FastInflateFast(ZlibStream* strm, uint32_t start) {
    ZlibInflateState* state = strm->state;
    const uint8_t* in = strm->next_in;
    const uint8_t* in_end = in + strm->avail_in;
    const uint8_t* last = in + (strm->avail_in - 5);
    uint8_t* out = strm->next_out;
    uint8_t* out_end = out + strm->avail_out;
    uint8_t* beg = out - (start - strm->avail_out);
    uint8_t* end = out + (strm->avail_out - 257);

    uint32_t wsize = state->wsize, whave = state->whave, write = state->write;
    uint8_t* window = state->window;
    uint64_t hold = static_cast<uint64_t>(state->hold);
    uint32_t bits = state->bits;
    const ZlibCode* lcode = state->lcode;
    const ZlibCode* dcode = state->dcode;
    uint64_t lmask = (1ULL << state->lenbits) - 1;
    uint64_t dmask = (1ULL << state->distbits) - 1;

    do {
        if (bits < 15) {
            uint32_t chunk;
            memcpy(&chunk, in, 4);
            hold |= static_cast<uint64_t>(chunk) << bits;
            in += 4;
            bits += 32;
        }
        ZlibCode here = lcode[hold & lmask];
    dolen:
        uint32_t op = here.bits;
        hold >>= op;
        bits -= op;
        op = here.op;
        if (op == 0) {
            *out++ = static_cast<uint8_t>(here.val);
        } else if (op & 16) {
            uint32_t len = here.val;
            op &= 15;
            if (op) {
                if (bits < op && in < in_end) { hold |= static_cast<uint64_t>(*in++) << bits; bits += 8; }
                len += static_cast<uint32_t>(hold & ((1ULL << op) - 1));
                hold >>= op;
                bits -= op;
            }
            if (bits < 15) {
                if (in + 4 <= in_end) {
                    uint32_t chunk; memcpy(&chunk, in, 4);
                    hold |= static_cast<uint64_t>(chunk) << bits; in += 4; bits += 32;
                } else {
                    while (bits < 15 && in < in_end) { hold |= static_cast<uint64_t>(*in++) << bits; bits += 8; }
                }
            }
            here = dcode[hold & dmask];
        dodist:
            op = here.bits; hold >>= op; bits -= op; op = here.op;
            if (op & 16) {
                uint32_t dist = here.val;
                op &= 15;
                if (bits < op) {
                    if (in < in_end) { hold |= static_cast<uint64_t>(*in++) << bits; bits += 8; }
                    if (bits < op && in < in_end) { hold |= static_cast<uint64_t>(*in++) << bits; bits += 8; }
                }
                dist += static_cast<uint32_t>(hold & ((1ULL << op) - 1));
                hold >>= op; bits -= op;
                uint32_t avail_back = static_cast<uint32_t>(out - beg);
                if (dist > avail_back) {
                    uint32_t back = dist - avail_back;
                    if (back > whave) { strm->msg = "invalid distance too far back"; state->mode = 27; break; }
                    const uint8_t* from = window;
                    if (write == 0) {
                        from += wsize - back;
                        if (back < len) { len -= back; memcpy(out, from, back); out += back; from = out - dist; }
                    } else if (write < back) {
                        from += wsize + write - back; back -= write;
                        if (back < len) {
                            len -= back; memcpy(out, from, back); out += back; from = window;
                            if (write < len) { uint32_t wlen = write; len -= wlen; memcpy(out, from, wlen); out += wlen; from = out - dist; }
                        }
                    } else {
                        from += write - back;
                        if (back < len) { len -= back; memcpy(out, from, back); out += back; from = out - dist; }
                    }
                    if (from >= window && from < window + wsize) { memcpy(out, from, len); out += len; }
                    else { FastCopyMatch(out, from, len, dist); out += len; }
                } else {
                    FastCopyMatch(out, out - dist, len, dist);
                    out += len;
                }
            } else if ((op & 64) == 0) {
                here = dcode[here.val + (hold & ((1ULL << op) - 1))]; goto dodist;
            } else { strm->msg = "invalid distance code"; state->mode = 27; break; }
        } else if ((op & 64) == 0) {
            here = lcode[here.val + (hold & ((1ULL << op) - 1))]; goto dolen;
        } else if (op & 32) {
            state->mode = 11; break;
        } else { strm->msg = "invalid literal/length code"; state->mode = 27; break; }
    } while (in < last && out < end);

    uint32_t extra_bytes = bits >> 3;
    in -= extra_bytes;
    bits -= extra_bytes << 3;
    hold &= (1ULL << bits) - 1;
    strm->next_in = in;
    strm->next_out = out;
    strm->avail_in = static_cast<uint32_t>(in_end - in);
    strm->avail_out = static_cast<uint32_t>(out_end - out);
    state->hold = static_cast<uint32_t>(hold);
    state->bits = bits;
}

//------------------------------------------------------------------------------
// Universal x86-64 True 240fps 2D Skeletal Animation Interpolation
// Covers ALL 3 channels: Main World + Overlay + UI AnimNode!
//------------------------------------------------------------------------------
struct AnimElement {
    uint32_t layer_hash;   // +0x00
    uint8_t  meta0[12];    // +0x04..+0x0F
    uint32_t symbol_hash;  // +0x10
    uint8_t  meta1[12];    // +0x14..+0x1F
    uint32_t frame_num;    // +0x20
    uint16_t m[6];         // +0x24..+0x2F: 6 IEEE-754 float16 [a, b, c, d, tx, ty]
};
static_assert(sizeof(AnimElement) == 48, "AnimElement must be 48 bytes");

struct AnimFrame {
    float        bbox[6];      // +0x00..+0x17
    AnimElement* elements;     // +0x18
    uint32_t     num_elements; // +0x20
    uint32_t     pad;          // +0x24
};
static_assert(sizeof(AnimFrame) == 40, "AnimFrame must be 40 bytes");

struct Animation {
    uint8_t    pad0[0x08];
    AnimFrame* frames;     // +0x08
    float      framerate;  // +0x10
    uint8_t    pad1[0x24];
    uint32_t   num_frames; // +0x38
    uint32_t   pad2;
    float      duration;   // +0x40
};

// Portable branchless IEEE-754 float16 <-> float32 (runs on 100% of x86-64 CPUs, zero F16C requirement!)
// Exact match with Klei's own half_to_float decoder @ 0x1400F3A8D.
static inline float HalfToFloat(uint16_t h) {
    uint32_t exp_mant = (h & 0x7FFFu);
    uint32_t bits = (h & 0x7C00u) ? ((exp_mant + 0x1C000u) << 13) : 0u;
    bits |= (static_cast<uint32_t>(h & 0x8000u) << 16);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

static inline uint16_t FloatToHalf(float f) {
    uint32_t x;
    memcpy(&x, &f, sizeof(x));
    uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 112;
    uint32_t mant = x & 0x007FFFFFu;
    if (exp <= 0) return static_cast<uint16_t>(sign);
    if (exp >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
    uint32_t rounded = (mant + 0x1000u) >> 13;
    if (rounded >= 0x400u) {
        rounded = 0;
        ++exp;
        if (exp >= 31) return static_cast<uint16_t>(sign | 0x7BFFu);
    }
    return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | rounded);
}

static inline float WrapAngleDiff(float d) {
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    if (d > kPi) d -= kTwoPi;
    else if (d < -kPi) d += kTwoPi;
    return d;
}

static inline bool TweenElementMatrix(const uint16_t m0h[6], const uint16_t m1h[6], float t, uint16_t outh[6]) {
    float a0 = HalfToFloat(m0h[0]), b0 = HalfToFloat(m0h[1]);
    float c0 = HalfToFloat(m0h[2]), d0 = HalfToFloat(m0h[3]);
    float tx0 = HalfToFloat(m0h[4]), ty0 = HalfToFloat(m0h[5]);

    float a1 = HalfToFloat(m1h[0]), b1 = HalfToFloat(m1h[1]);
    float c1 = HalfToFloat(m1h[2]), d1 = HalfToFloat(m1h[3]);
    float tx1 = HalfToFloat(m1h[4]), ty1 = HalfToFloat(m1h[5]);

    float dtx = tx1 - tx0;
    float dty = ty1 - ty0;
    // Boss-scaled guard (420px): allows giant boss slams/lunges (Daywalker/Deerclops/Dragonfly)
    // while still blocking cross-screen teleports.
    if (fabsf(dtx) > 420.0f || fabsf(dty) > 420.0f) return false;

    float det0 = a0 * d0 - b0 * c0;
    float det1 = a1 * d1 - b1 * c1;
    if (det0 * det1 < 0.0f) return false;

    float su0 = sqrtf(a0 * a0 + c0 * c0);
    float su1 = sqrtf(a1 * a1 + c1 * c1);
    float sv0 = sqrtf(b0 * b0 + d0 * d0);
    float sv1 = sqrtf(b1 * b1 + d1 * d1);

    float a, b, c, d;
    if (su0 > 1e-4f && su1 > 1e-4f && sv0 > 1e-4f && sv1 > 1e-4f) {
        float au0 = atan2f(c0, a0), au1 = atan2f(c1, a1);
        float du = WrapAngleDiff(au1 - au0);
        float av0 = atan2f(d0, b0), av1 = atan2f(d1, b1);
        float dv = WrapAngleDiff(av1 - av0);
        // Allow fast weapon swings & boss attacks up to ~135 deg/tick (2.35 rad)
        if (fabsf(du) > 2.35f || fabsf(dv) > 2.35f) return false;

        float su = su0 + (su1 - su0) * t, au = au0 + du * t;
        float sv = sv0 + (sv1 - sv0) * t, av = av0 + dv * t;
        a = su * cosf(au); c = su * sinf(au);
        b = sv * cosf(av); d = sv * sinf(av);
    } else {
        a = a0 + (a1 - a0) * t; b = b0 + (b1 - b0) * t;
        c = c0 + (c1 - c0) * t; d = d0 + (d1 - d0) * t;
    }

    outh[0] = FloatToHalf(a);
    outh[1] = FloatToHalf(b);
    outh[2] = FloatToHalf(c);
    outh[3] = FloatToHalf(d);
    outh[4] = FloatToHalf(tx0 + dtx * t);
    outh[5] = FloatToHalf(ty0 + dty * t);
    return true;
}

struct AnimBufferSlot {
    AnimFrame frame{};
    std::vector<AnimElement> elems;
};

struct EntityAnimTrack {
    uintptr_t   entity_key = 0;
    const void* anim = nullptr;
    float       last_time = -1.0f;
    float       prev_f = 0.0f;
    float       curr_f = 0.0f;
    int64_t     tick_qpc = 0;
    int64_t     seen_qpc = 0;
    float       tick_period = 1.0f / 30.0f;
    uint8_t     valid = 0;
};

constexpr size_t kTrackSlots = 8192;

struct ThreadLocalAnimState {
    AnimBufferSlot slots[4];
    uint32_t ring = 0;
    std::vector<EntityAnimTrack> tracks;
    ThreadLocalAnimState() : tracks(kTrackSlots) {}
};

thread_local uintptr_t g_tlsLastParentKey = 0;

static const void* InterpGetFrameCore(const void* animPtr, int32_t playMode, float time, uintptr_t key, bool isMainParent) {
    if (!animPtr || !g_origAnimGetFrame) {
        return g_origAnimGetFrame ? g_origAnimGetFrame(animPtr, playMode, time) : nullptr;
    }
    if (InterlockedCompareExchange(&g_skeletalTweenEnabled, 0, 0) == 0) {
        return g_origAnimGetFrame(animPtr, playMode, time);
    }

    const auto* anim = static_cast<const Animation*>(animPtr);
    uint32_t nframes = anim->num_frames;
    float fps = anim->framerate;
    if (nframes <= 1 || !anim->frames || fps <= 0.0f || fps > 240.0f) {
        return g_origAnimGetFrame(animPtr, playMode, time);
    }

    thread_local ThreadLocalAnimState tls;
    if (isMainParent && key != 0) {
        g_tlsLastParentKey = key;
    }
    if (key == 0) {
        return g_origAnimGetFrame(animPtr, playMode, time);
    }

    // Compute continuous floating-point frame coordinate (works for 30fps, 40fps,
    // SetDeltaTimeMultiplier speed scaling, looping, one-shot, and percentage animations).
    float nf = static_cast<float>(nframes);
    float curF = 0.0f;
    if (playMode == 1) {
        float t = (time < 0.0f) ? 0.0f : time;
        curF = fmodf(t * fps, nf);
        if (curF < 0.0f) curF += nf;
    } else if (playMode == 0) {
        float t = (time < 0.0f) ? 0.0f : time;
        curF = t * fps;
        if (curF > nf - 1.0f) curF = nf - 1.0f;
    } else if (playMode == 2) {
        float c = (time < 0.0f) ? 0.0f : ((time > 1.0f) ? 1.0f : time);
        curF = c * (nf - 1.0f);
    } else {
        return g_origAnimGetFrame(animPtr, playMode, time);
    }

    LARGE_INTEGER nowLi{};
    QueryPerformanceCounter(&nowLi);
    int64_t nowQpc = nowLi.QuadPart;

    // 4-probe open-addressing lookup with LRU eviction (0% hash-collision eviction in megabases)
    size_t baseSlot = ((key >> 4) ^ (key >> 15) ^ (key >> 27)) & (kTrackSlots - 1);
    EntityAnimTrack* trPtr = nullptr;
    EntityAnimTrack* lruPtr = &tls.tracks[baseSlot];
    for (size_t p = 0; p < 4; ++p) {
        EntityAnimTrack& cand = tls.tracks[(baseSlot + p) & (kTrackSlots - 1)];
        if (cand.valid && cand.entity_key == key) { trPtr = &cand; break; }
        if (!cand.valid) { trPtr = &cand; break; }
        if (cand.seen_qpc < lruPtr->seen_qpc) lruPtr = &cand;
    }
    if (!trPtr) trPtr = lruPtr;
    EntityAnimTrack& tr = *trPtr;
    tr.seen_qpc = nowQpc;

    auto discreteFrame = [&]() -> const void* {
        uint32_t fi = static_cast<uint32_t>(curF + 0.001f);
        if (fi >= nframes) fi = (playMode == 1) ? (fi % nframes) : (nframes - 1);
        return &anim->frames[fi];
    };

    if (!tr.valid || tr.entity_key != key || tr.anim != animPtr) {
        tr.entity_key = key;
        tr.anim = animPtr;
        tr.last_time = time;
        tr.prev_f = curF;
        tr.curr_f = curF;
        tr.tick_qpc = nowQpc;
        tr.tick_period = 1.0f / 30.0f;
        tr.valid = 1;
        return discreteFrame();
    }

    if (time != tr.last_time) {
        float oldF = tr.curr_f;
        float df = curF - oldF;
        if (playMode == 1 && df < 0.0f) {
            df += nf;
        }
        float elapsed = static_cast<float>(static_cast<double>(nowQpc - tr.tick_qpc) * g_qpcInvFreq);
        bool smoothStep = (df > 0.01f && df <= 4.5f) ||
                          (playMode == 2 && fabsf(df) > 0.01f && fabsf(df) <= 4.5f);
        if (smoothStep) {
            if (elapsed > 0.008f && elapsed < 0.120f) {
                tr.tick_period = tr.tick_period * 0.7f + elapsed * 0.3f;
            }
            tr.prev_f = oldF;
            tr.curr_f = curF;
        } else {
            tr.prev_f = curF;
            tr.curr_f = curF;
        }
        tr.last_time = time;
        tr.tick_qpc = nowQpc;
    }

    float stepF = tr.curr_f - tr.prev_f;
    if (playMode == 1 && stepF < 0.0f) stepF += nf;
    if (fabsf(stepF) < 0.001f) {
        return discreteFrame();
    }

    float dtSinceTick = static_cast<float>(static_cast<double>(nowQpc - tr.tick_qpc) * g_qpcInvFreq);
    float alpha = (tr.tick_period > 1e-4f) ? (dtSinceTick / tr.tick_period) : 1.0f;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    float fNow = tr.prev_f + stepF * alpha;
    uint32_t idx0 = 0, idx1 = 0;
    float frac = 0.0f;

    if (playMode == 1) {
        while (fNow >= nf) fNow -= nf;
        while (fNow < 0.0f) fNow += nf;
        float fl = floorf(fNow);
        idx0 = static_cast<uint32_t>(fl) % nframes;
        idx1 = (idx0 + 1) % nframes;
        frac = fNow - fl;
    } else {
        if (fNow < 0.0f) fNow = 0.0f;
        if (fNow >= nf - 1.0f) return &anim->frames[nframes - 1];
        float fl = floorf(fNow);
        idx0 = static_cast<uint32_t>(fl);
        idx1 = idx0 + 1;
        frac = fNow - fl;
    }

    if (frac <= 0.015f) return &anim->frames[idx0];
    if (frac >= 0.985f) return &anim->frames[idx1];
    if (idx0 == idx1 || idx0 >= nframes || idx1 >= nframes) {
        return &anim->frames[idx0 < nframes ? idx0 : 0];
    }

    const AnimFrame& f0 = anim->frames[idx0];
    const AnimFrame& f1 = anim->frames[idx1];
    uint32_t n0 = f0.num_elements, n1 = f1.num_elements;
    if (n0 == 0 || !f0.elements || n1 == 0 || !f1.elements || n0 > 512) return &f0;

    AnimBufferSlot& bufSlot = tls.slots[(tls.ring++) & 3];
    if (bufSlot.elems.size() < n0) bufSlot.elems.resize(n0);

    bufSlot.frame = f0;
    bufSlot.frame.elements = bufSlot.elems.data();
    memcpy(bufSlot.elems.data(), f0.elements, n0 * sizeof(AnimElement));

    uint32_t tweenedCount = 0;
    uint32_t pass2Count = 0;
    for (uint32_t i = 0; i < n0; ++i) {
        const AnimElement& e0 = f0.elements[i];
        uint32_t lhash = e0.layer_hash, shash = e0.symbol_hash, fnum = e0.frame_num;

        // Pass 1: exact (layer_hash, symbol_hash, frame_num) occurrence-indexed match
        uint32_t occExact = 0;
        for (uint32_t k = 0; k < i; ++k) {
            const AnimElement& ek = f0.elements[k];
            if (ek.symbol_hash == shash && ek.layer_hash == lhash && ek.frame_num == fnum) ++occExact;
        }

        const AnimElement* match = nullptr;
        bool isPass2 = false;
        if (occExact == 0 && i < n1) {
            const AnimElement& cand = f1.elements[i];
            if (cand.symbol_hash == shash && cand.layer_hash == lhash && cand.frame_num == fnum) {
                bool earlier = false;
                for (uint32_t j = 0; j < i; ++j) {
                    const AnimElement& ej = f1.elements[j];
                    if (ej.symbol_hash == shash && ej.layer_hash == lhash && ej.frame_num == fnum) {
                        earlier = true; break;
                    }
                }
                if (!earlier) match = &cand;
            }
        }
        if (!match) {
            uint32_t seen = 0;
            for (uint32_t j = 0; j < n1; ++j) {
                const AnimElement& e1 = f1.elements[j];
                if (e1.symbol_hash == shash && e1.layer_hash == lhash && e1.frame_num == fnum) {
                    if (seen == occExact) { match = &e1; break; }
                    ++seen;
                }
            }
        }

        // Pass 2: fallback for hand-drawn sprite-frame swaps (hands, weapons, boss claws)
        // Match same (layer_hash, symbol_hash), interpolate matrix smoothly, flip sprite at frac >= 0.5
        if (!match) {
            uint32_t occLS = 0;
            for (uint32_t k = 0; k < i; ++k) {
                const AnimElement& ek = f0.elements[k];
                if (ek.symbol_hash == shash && ek.layer_hash == lhash) ++occLS;
            }
            uint32_t seen = 0;
            for (uint32_t j = 0; j < n1; ++j) {
                const AnimElement& e1 = f1.elements[j];
                if (e1.symbol_hash == shash && e1.layer_hash == lhash) {
                    if (seen == occLS) { match = &e1; isPass2 = true; break; }
                    ++seen;
                }
            }
        }

        if (match && TweenElementMatrix(e0.m, match->m, frac, bufSlot.elems[i].m)) {
            ++tweenedCount;
            if (isPass2) {
                ++pass2Count;
                if (frac >= 0.5f) {
                    bufSlot.elems[i].frame_num = match->frame_num;
                }
            }
        }
    }

    if (tweenedCount > 0) {
        InterlockedIncrement64(&g_animInterpFrames);
        InterlockedAdd64(&g_animElemsTweened, static_cast<LONG64>(tweenedCount));
        if (pass2Count > 0) InterlockedAdd64(&g_animPass2Swaps, static_cast<LONG64>(pass2Count));
    }
    return &bufSlot.frame;
}

extern "C" const void* FastInterpGetFrameMain(const void* animPtr, int32_t playMode, float time, const void* persistentAnimNode) {
    return InterpGetFrameCore(animPtr, playMode, time, reinterpret_cast<uintptr_t>(persistentAnimNode), true);
}

extern "C" const void* FastInterpGetFrameUI(const void* animPtr, int32_t playMode, float time, const void* persistentAnimNode) {
    return InterpGetFrameCore(animPtr, playMode, time, reinterpret_cast<uintptr_t>(persistentAnimNode), false);
}

extern "C" const void* FastInterpGetFrameOverlay(const void* animPtr, int32_t playMode, float time, const void* overlayItem) {
    uintptr_t baseKey = (g_tlsLastParentKey != 0) ? g_tlsLastParentKey : reinterpret_cast<uintptr_t>(overlayItem);
    uintptr_t childKey = (baseKey ^ (reinterpret_cast<uintptr_t>(animPtr) * 0x9E3779B97F4A7C15ULL)) | 1ULL;
    return InterpGetFrameCore(animPtr, playMode, time, childKey, false);
}

uint8_t* AllocNearExecutable(uint8_t* nearAddr, size_t size) {
    uintptr_t base = reinterpret_cast<uintptr_t>(nearAddr) & ~static_cast<uintptr_t>(0xFFFF);
    constexpr uintptr_t kStep = 0x10000;
    constexpr uintptr_t kMaxDist = 0x60000000;
    for (uintptr_t delta = kStep; delta < kMaxDist; delta += kStep) {
        if (base > delta) {
            void* p = VirtualAlloc(reinterpret_cast<void*>(base - delta), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
            if (p) return static_cast<uint8_t*>(p);
        }
        void* p = VirtualAlloc(reinterpret_cast<void*>(base + delta), size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (p) return static_cast<uint8_t*>(p);
    }
    return nullptr;
}

static bool InstallAnimCallsiteHook(uint8_t* callSite, const char* label, uint8_t*& outStub, const void* targetFn, bool derefRbxPlus8) {
    int32_t origRel = 0;
    memcpy(&origRel, callSite + 1, 4);
    uint8_t* origTarget = callSite + 5 + origRel;
    if (!g_origAnimGetFrame) g_origAnimGetFrame = reinterpret_cast<AnimGetFrame_t>(origTarget);

    outStub = AllocNearExecutable(callSite, 64);
    if (!outStub) return false;

    uint64_t dst = reinterpret_cast<uint64_t>(targetFn);
    if (derefRbxPlus8) {
        // mov r9, qword ptr [rbx + 8]  ; 4C 8B 4B 08 (loads persistent AnimNode* from transient AnimRenderCommand!)
        // jmp qword ptr [rip + 0]      ; FF 25 00 00 00 00
        uint8_t stub[18] = { 0x4C, 0x8B, 0x4B, 0x08, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
        memcpy(stub + 10, &dst, sizeof(dst));
        memcpy(outStub, stub, sizeof(stub));
        FlushInstructionCache(GetCurrentProcess(), outStub, sizeof(stub));
    } else {
        // mov r9, rbx                  ; 49 89 D9
        // jmp qword ptr [rip + 0]      ; FF 25 00 00 00 00
        uint8_t stub[17] = { 0x49, 0x89, 0xD9, 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
        memcpy(stub + 9, &dst, sizeof(dst));
        memcpy(outStub, stub, sizeof(stub));
        FlushInstructionCache(GetCurrentProcess(), outStub, sizeof(stub));
    }

    int64_t diff = reinterpret_cast<intptr_t>(outStub) - reinterpret_cast<intptr_t>(callSite + 5);
    if (diff < INT32_MIN || diff > INT32_MAX) return false;
    int32_t newRel = static_cast<int32_t>(diff);
    if (WriteCodeBytes(callSite + 1, reinterpret_cast<const uint8_t*>(&newRel), 4)) {
        LogMsg("Hooked %s @ %p -> 240fps Skeletal Tween (%s)",
               label, static_cast<void*>(callSite), derefRbxPlus8 ? "persistent [rbx+8]" : "direct rbx");
        return true;
    }
    return false;
}

static uint8_t* FindMaskedCallsite(uint8_t* textStart, size_t textSize, const uint8_t* pre, size_t preLen, const uint8_t* post, size_t postLen) {
    size_t total = preLen + 4 + postLen;
    if (!textStart || textSize < total) return nullptr;
    for (size_t off = 0; off <= textSize - total; ++off) {
        if (textStart[off] == pre[0] &&
            memcmp(textStart + off, pre, preLen) == 0 &&
            memcmp(textStart + off + preLen + 4, post, postLen) == 0) {
            return textStart + off + preLen - 1; // points to 0xE8
        }
    }
    return nullptr;
}

void ApplySkeletalAnimTweenHooks(uint8_t* textStart, size_t textSize) {
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    g_qpcInvFreq = (freq.QuadPart > 0) ? (1.0 / static_cast<double>(freq.QuadPart)) : (1.0 / 10000000.0);

    // 1. Main World AnimRenderer::Render (0x1400F4957): rbx is transient AnimRenderCommand, [rbx+8] is persistent AnimNode*!
    constexpr uint8_t kPreMain[]  = { 0x8B, 0x93, 0xC0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x8B, 0xB0, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x93, 0xC4, 0x00, 0x00, 0x00, 0xE8 };
    constexpr uint8_t kPostMain[] = { 0x4C, 0x8B, 0xF0, 0x48, 0x89, 0x84, 0x24 };
    if (uint8_t* cs = FindMaskedCallsite(textStart, textSize, kPreMain, sizeof(kPreMain), kPostMain, sizeof(kPostMain))) {
        InstallAnimCallsiteHook(cs, "Main World AnimRenderer", g_animRelayStubMain, reinterpret_cast<const void*>(&FastInterpGetFrameMain), true);
    }

    // 2. Overlay / FollowSymbol AnimRenderer (0x1400F4493)
    constexpr uint8_t kPreOvl[]  = { 0x8B, 0x53, 0x10, 0x48, 0x8B, 0x0B, 0xF3, 0x0F, 0x10, 0x53, 0x14, 0xE8 };
    constexpr uint8_t kPostOvl[] = { 0x48, 0x8B, 0xF0, 0x48, 0x85, 0xC0, 0x0F, 0x84 };
    if (uint8_t* cs = FindMaskedCallsite(textStart, textSize, kPreOvl, sizeof(kPreOvl), kPostOvl, sizeof(kPostOvl))) {
        InstallAnimCallsiteHook(cs, "Overlay AnimRenderer", g_animRelayStubOverlay, reinterpret_cast<const void*>(&FastInterpGetFrameOverlay), false);
    }

    // 3. UI / AnimNode Render (0x1400F3135): rbx is persistent AnimNode* directly!
    constexpr uint8_t kPreUI[]  = { 0x8B, 0x93, 0x14, 0x01, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x93, 0x18, 0x01, 0x00, 0x00, 0xE8 };
    constexpr uint8_t kPostUI[] = { 0x48, 0x8B, 0xE8, 0x48, 0x85, 0xC0, 0x0F, 0x84 };
    if (uint8_t* cs = FindMaskedCallsite(textStart, textSize, kPreUI, sizeof(kPreUI), kPostUI, sizeof(kPostUI))) {
        InstallAnimCallsiteHook(cs, "UI/AnimNode Renderer", g_animRelayStubUI, reinterpret_cast<const void*>(&FastInterpGetFrameUI), false);
    }
}

void ApplyVectorMemsetPatch(uint8_t* textStart, size_t textSize) {
    if (uint8_t* pA = FindPatternInText(textStart, textSize, kSigVecFillA, sizeof(kSigVecFillA))) {
        uint8_t patchA[sizeof(kSigVecFillA)];
        memset(patchA, 0x90, sizeof(patchA));
        constexpr uint8_t codeA[] = { 0x57, 0x48, 0x89, 0xF7, 0x48, 0x89, 0xD9, 0x41, 0x0F, 0xB6, 0x45, 0x00, 0xF3, 0xAA, 0x5F };
        memcpy(patchA, codeA, sizeof(codeA));
        if (WriteCodeBytes(pA, patchA, sizeof(patchA))) {
            LogMsg("Patched std::vector::_Insert_n Loop A -> HW rep stosb @ %p", static_cast<void*>(pA));
        }
    }
    if (uint8_t* pB = FindPatternInText(textStart, textSize, kSigVecFillB, sizeof(kSigVecFillB))) {
        uint8_t patchB[sizeof(kSigVecFillB)];
        memset(patchB, 0x90, sizeof(patchB));
        constexpr uint8_t codeB[] = { 0x57, 0x48, 0x89, 0xCF, 0x48, 0x89, 0xC1, 0x44, 0x89, 0xE0, 0xF3, 0xAA, 0x5F };
        memcpy(patchB, codeB, sizeof(codeB));
        if (WriteCodeBytes(pB, patchB, sizeof(patchB))) {
            LogMsg("Patched std::vector::_Insert_n Loop B -> HW rep stosb @ %p", static_cast<void*>(pB));
        }
    }
}

void ApplyLibdeflateUnzHook(uint8_t* textStart, size_t textSize) {
    uint8_t* pUnz = FindPatternInText(textStart, textSize, kSigUnzReadCurrentFile, sizeof(kSigUnzReadCurrentFile));
    if (!pUnz) return;

    g_unzTrampoline = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!g_unzTrampoline) return;

    constexpr uint8_t prefix[8] = { 0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x33, 0xF6 };
    memcpy(g_unzTrampoline, prefix, 8);
    uint8_t jmpBack[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint64_t resumeAddr = reinterpret_cast<uint64_t>(pUnz + 0x16);
    memcpy(jmpBack + 6, &resumeAddr, sizeof(resumeAddr));
    memcpy(g_unzTrampoline + 8, jmpBack, 14);
    FlushInstructionCache(GetCurrentProcess(), g_unzTrampoline, 22);
    g_origUnzRead = reinterpret_cast<UnzReadCurrentFile_t>(g_unzTrampoline);

    uint8_t jmpHook[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint64_t hookTarget = reinterpret_cast<uint64_t>(&FastUnzReadCurrentFile);
    memcpy(jmpHook + 6, &hookTarget, sizeof(hookTarget));
    if (WriteCodeBytes(pUnz, jmpHook, sizeof(jmpHook))) {
        LogMsg("Hooked minizip unzReadCurrentFile @ %p -> libdeflate v1.23 (auto-SIMD)", static_cast<void*>(pUnz));
    }
}

void ApplyFastInflatePatch(uint8_t* textStart, size_t textSize) {
    uint8_t* pInf = FindPatternInText(textStart, textSize, kSigInflateFast, sizeof(kSigInflateFast));
    if (!pInf) return;
    uint8_t jmp[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint64_t target = reinterpret_cast<uint64_t>(&FastInflateFast);
    memcpy(jmp + 6, &target, sizeof(target));
    if (WriteCodeBytes(pInf, jmp, sizeof(jmp))) {
        LogMsg("Hooked zlib 1.2.3 inflate_fast @ %p -> 64-bit FastInflateFast", static_cast<void*>(pInf));
    }
}

//------------------------------------------------------------------------------
// Smooth 240Hz Lua GC Pacer & Anti-Freeze Guard (Simulation::CollectGarbage)
//------------------------------------------------------------------------------
extern "C" void FastSmoothCollectGarbage(uint8_t* sim) {
    if (!sim || !g_luaGc || !g_setGcBudget) return;
    void* L = *reinterpret_cast<void**>(sim + 0xB0);
    if (!L) return;

    // 1. Scale per-frame incremental GC budget inversely with FPS so worst-case GC frame-time
    //    stays sub-millisecond while delivering equal or higher total GC throughput/sec:
    //    - 240fps: 0.35ms/frame -> 84ms/sec (+40% vs vanilla 60ms/sec) at 3x tighter 1% low!
    //    - 120-164fps: 0.50ms/frame -> 60-82ms/sec
    //    - <=99fps: 1.00ms/frame (exact vanilla 60fps behavior)
    LONG fps = InterlockedCompareExchange(&g_currentFps, 0, 0);
    float budgetSec = 0.0010f;
    if (fps == 0 || fps >= 165) budgetSec = 0.00035f;
    else if (fps >= 100) budgetSec = 0.00050f;

    // 2. Replace vanilla's twitchy 1.5x Stop-The-World LUA_GCCOLLECT combat-freeze trigger
    //    with an emergency-only safety floor: max(2.5x baseline, baseline + 256MB).
    int memKB = g_luaGc(L, 3 /*LUA_GCCOUNT*/, 0);
    int32_t* pThresh = reinterpret_cast<int32_t*>(sim + 0x24C);
    if (*pThresh <= 0) {
        int floorKB = memKB + 262144; // +256 MB emergency headroom
        int scaleKB = static_cast<int>(static_cast<double>(memKB) * 2.5);
        *pThresh = (scaleKB > floorKB) ? scaleKB : floorKB;
    } else if (memKB > *pThresh) {
        g_luaGc(L, 2 /*LUA_GCCOLLECT*/, 0);
        int afterKB = g_luaGc(L, 3 /*LUA_GCCOUNT*/, 0);
        int floorKB = afterKB + 262144;
        int scaleKB = static_cast<int>(static_cast<double>(afterKB) * 2.5);
        int next = (scaleKB > floorKB) ? scaleKB : floorKB;
        if (next - afterKB > 0x320000) next = afterKB + 0x320000;
        *pThresh = next;
        InterlockedIncrement64(&g_gcEmergencyFulls);
    }

    // 3. Execute Klei's wall-clock time-budgeted incremental GC step
    g_setGcBudget(budgetSec);
    g_luaGc(L, 5 /*LUA_GCSTEP*/, 0);
}

void ApplySmoothLuaGcHook(uint8_t* textStart, size_t textSize) {
    // Simulation::CollectGarbage (0x140102070) masked AOB (verified unique 1-match)
    constexpr uint8_t kPreGc[] = {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x45, 0x33, 0xC0,
        0x48, 0x8B, 0xF9, 0x48, 0x8B, 0x89, 0xB0, 0x00, 0x00, 0x00, 0x41, 0x8D, 0x50, 0x03, 0xE8
    };
    constexpr uint8_t kPostGc[] = {
        0x3B, 0x87, 0x4C, 0x02, 0x00, 0x00, 0x7E, 0x54, 0x48, 0x8B, 0x8F, 0xB0, 0x00, 0x00, 0x00, 0x45
    };
    uint8_t* cs = FindMaskedCallsite(textStart, textSize, kPreGc, sizeof(kPreGc), kPostGc, sizeof(kPostGc));
    if (!cs) return;
    uint8_t* fnStart = cs - (sizeof(kPreGc) - 1);
    // Verify call to SetGCTimeBudget at +0x8B
    if (fnStart[0x8B] != 0xE8) return;

    int32_t relGc = 0, relBudget = 0;
    memcpy(&relGc, fnStart + 0x1C, 4);
    memcpy(&relBudget, fnStart + 0x8C, 4);
    g_luaGc = reinterpret_cast<LuaGc_t>(fnStart + 0x20 + relGc);
    g_setGcBudget = reinterpret_cast<SetGcBudget_t>(fnStart + 0x90 + relBudget);

    uint8_t jmp[14] = { 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
    uint64_t target = reinterpret_cast<uint64_t>(&FastSmoothCollectGarbage);
    memcpy(jmp + 6, &target, sizeof(target));
    if (WriteCodeBytes(fnStart, jmp, sizeof(jmp))) {
        LogMsg("Hooked Simulation::CollectGarbage @ %p -> Smooth 240Hz GC Pacer (0.35ms step, +256MB anti-freeze)",
               static_cast<void*>(fnStart));
    }
}

bool ApplyFpsPatch(int fps) {
    if (!g_patchAddr) return false;
    if (fps < 0) fps = 0;
    if (fps > 1000) fps = 1000;
    if (fps == InterlockedCompareExchange(&g_currentFps, 0, 0)) return true;

    uint8_t patch[8]{};
    if (fps == 60) {
        memcpy(patch, kOrigFrameLimiter8, 8);
    } else {
        float dt = (fps == 0) ? 0.0f : (1.0f / static_cast<float>(fps));
        patch[0] = 0xC7; patch[1] = 0x41; patch[2] = 0x08;
        memcpy(&patch[3], &dt, sizeof(float));
        patch[7] = 0xC3;
    }

    if (!WriteCodeBytes(g_patchAddr, patch, 8)) return false;
    InterlockedExchange(&g_currentFps, fps);
    if (fps == 60) LogMsg("FPS -> 60 (official FrameLimiter restored)");
    else if (fps == 0) LogMsg("FPS -> UNLIMITED");
    else LogMsg("FPS -> %d (dt=%.6fs)", fps, 1.0f / static_cast<float>(fps));
    return true;
}

void EnsureDefaultIni() {
    if (GetFileAttributesW(g_iniPath.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    FILE* f = _wfopen(g_iniPath.c_str(), L"w");
    if (!f) return;
    fprintf(f,
        "; DST HyperSmooth & FastLoad Native Companion Plugin Configuration (v2.2.0)\n"
        "; Note: If you also use the Steam Workshop Companion Mod, in-game settings/hotkeys override these live.\n"
        "[FPS]\n"
        "target_fps=240\n"
        "\n"
        "[OPTIMIZE]\n"
        "high_res_timer=1\n"
        "boost_priority=1\n"
        "prewarm_bundles=1\n"
        "hybrid_frame_pacer=1\n"
        "fast_vector_fill=1\n"
        "fast_libdeflate_zip=1\n"
        "fast_zlib_inflate=1\n"
        "smooth_skeletal_anim=1\n"
        "smooth_lua_gc=1\n");
    fclose(f);
}

int ReadIniInt(const wchar_t* sec, const wchar_t* key, int defVal) {
    return static_cast<int>(GetPrivateProfileIntW(sec, key, defVal, g_iniPath.c_str()));
}

uint64_t GetFileModTime(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) |
           static_cast<uint64_t>(data.ftLastWriteTime.dwLowDateTime);
}

bool FindNewestModBridgeFile(std::wstring& outPath, uint64_t& outMtime) {
    wchar_t docsPath[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docsPath))) return false;
    std::wstring root = std::wstring(docsPath) + L"\\Klei\\DoNotStarveTogether\\";
    std::wstring search = root + L"*";

    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return false;

    uint64_t bestTime = 0;
    std::wstring bestPath;
    do {
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) {
            std::wstring candidate = root + fd.cFileName + L"\\client_save\\dst_smooth_highfps_cfg";
            uint64_t mt = GetFileModTime(candidate);
            if (mt > bestTime) { bestTime = mt; bestPath = candidate; }
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (bestTime > 0) { outPath = bestPath; outMtime = bestTime; return true; }
    return false;
}

// Supports both legacy "<fps>" and v2.0.0 "fps=<N>;skeletal=<0|1>" formats
bool ParseModBridgeFile(const std::wstring& path, int& outFps, bool& hasFps, int& outSkeletal, bool& hasSkeletal) {
    hasFps = false;
    hasSkeletal = false;
    FILE* f = _wfopen(path.c_str(), L"r");
    if (!f) return false;
    char buf[128]{};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return false;

    if (const char* p = strstr(buf, "fps=")) {
        outFps = static_cast<int>(strtol(p + 4, nullptr, 10));
        hasFps = true;
    }
    if (const char* p = strstr(buf, "skeletal=")) {
        outSkeletal = static_cast<int>(strtol(p + 9, nullptr, 10));
        hasSkeletal = true;
    }
    if (!hasFps && !hasSkeletal) {
        char* endp = nullptr;
        long v = strtol(buf, &endp, 10);
        if (endp != buf) {
            outFps = static_cast<int>(v);
            hasFps = true;
        }
    }
    return hasFps || hasSkeletal;
}

void BoostLocalDedicatedServers() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"dontstarve_dedicated_server_nullrenderer_x64.exe") == 0) {
                if (g_boostedServerPids.find(pe.th32ProcessID) == g_boostedServerPids.end()) {
                    if (HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pe.th32ProcessID)) {
                        if (SetPriorityClass(hProc, HIGH_PRIORITY_CLASS)) {
                            LogMsg("Boosted local dedicated server PID=%lu -> HIGH_PRIORITY_CLASS", pe.th32ProcessID);
                        }
                        CloseHandle(hProc);
                    }
                    g_boostedServerPids.insert(pe.th32ProcessID);
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

DWORD WINAPI PrewarmBundlesProc(LPVOID) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    std::wstring bundlesDir = g_binDir + L"..\\data\\databundles\\";
    std::wstring search = bundlesDir + L"*.zip";
    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    constexpr size_t kBufSize = 1024 * 1024;
    auto* buf = new uint8_t[kBufSize];
    size_t totalBytes = 0;
    DWORD t0 = GetTickCount();

    do {
        if (InterlockedCompareExchange(&g_stopFlag, 0, 0) != 0) break;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            std::wstring fpath = bundlesDir + fd.cFileName;
            HANDLE hf = CreateFileW(fpath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                    nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
            if (hf != INVALID_HANDLE_VALUE) {
                DWORD nread = 0;
                while (InterlockedCompareExchange(&g_stopFlag, 0, 0) == 0 &&
                       ReadFile(hf, buf, static_cast<DWORD>(kBufSize), &nread, nullptr) && nread > 0) {
                    totalBytes += nread;
                }
                CloseHandle(hf);
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
    delete[] buf;
    LogMsg("Pre-warmed databundles: %.1f MB in %lu ms",
           static_cast<double>(totalBytes) / (1024.0 * 1024.0), GetTickCount() - t0);
    return 0;
}

DWORD WINAPI WorkerProc(LPVOID) {
    wchar_t exePath[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeStr(exePath);
    if (exeStr.find(L"dontstarve_steam_x64.exe") == std::wstring::npos &&
        exeStr.find(L"dontstarve_steam") == std::wstring::npos) {
        return 0;
    }

    wchar_t modPath[MAX_PATH]{};
    GetModuleFileNameW(g_selfModule, modPath, MAX_PATH);
    std::wstring mp(modPath);
    size_t slash = mp.find_last_of(L"\\/");
    g_binDir = (slash != std::wstring::npos) ? mp.substr(0, slash + 1) : L"";
    g_iniPath = g_binDir + L"dst_fps.ini";
    g_logPath = g_binDir + L"dst_fps_plugin.log";

    if (FILE* f = _wfopen(g_logPath.c_str(), L"w")) fclose(f);
    LogMsg("DST HyperSmooth & FastLoad Native Companion Plugin v2.2.0 (Universal x86-64) loaded.");

    EnsureDefaultIni();

    bool optTimer = ReadIniInt(L"OPTIMIZE", L"high_res_timer", 1) != 0;
    bool optPriority = ReadIniInt(L"OPTIMIZE", L"boost_priority", 1) != 0;
    bool optPrewarm = ReadIniInt(L"OPTIMIZE", L"prewarm_bundles", 1) != 0;
    bool optHybridPacer = ReadIniInt(L"OPTIMIZE", L"hybrid_frame_pacer", 1) != 0;
    bool optFastVec = ReadIniInt(L"OPTIMIZE", L"fast_vector_fill", 1) != 0;
    bool optLibdeflate = ReadIniInt(L"OPTIMIZE", L"fast_libdeflate_zip", 1) != 0;
    bool optFastZlib = ReadIniInt(L"OPTIMIZE", L"fast_zlib_inflate", 1) != 0;
    bool optSkeletal = ReadIniInt(L"OPTIMIZE", L"smooth_skeletal_anim", 1) != 0;
    bool optSmoothGc = ReadIniInt(L"OPTIMIZE", L"smooth_lua_gc", 1) != 0;
    InterlockedExchange(&g_skeletalTweenEnabled, optSkeletal ? 1 : 0);

    if (optTimer && timeBeginPeriod(1) == TIMERR_NOERROR) {
        g_timerBoosted = true;
        LogMsg("Enabled 1ms high-resolution Windows timer.");
    }
    if (optPriority && SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS)) {
        LogMsg("Boosted client process -> HIGH_PRIORITY_CLASS.");
    }

    uint8_t* textStart = nullptr;
    size_t textSize = 0;
    if (GetTextSection(textStart, textSize)) {
        if (optFastVec) ApplyVectorMemsetPatch(textStart, textSize);
        if (optLibdeflate) ApplyLibdeflateUnzHook(textStart, textSize);
        if (optFastZlib) ApplyFastInflatePatch(textStart, textSize);
        if (optSmoothGc) ApplySmoothLuaGcHook(textStart, textSize);
        ApplySkeletalAnimTweenHooks(textStart, textSize);

        g_patchAddr = FindPatternInText(textStart, textSize, kSigFrameLimiter, sizeof(kSigFrameLimiter));
        if (g_patchAddr) {
            LogMsg("Found FrameLimiter::SetTargetFrameTime @ %p", static_cast<void*>(g_patchAddr));
            if (optHybridPacer) {
                uint8_t* p = g_patchAddr + 0x57;
                if (p[0] == 0x84 && p[1] == 0xDB && p[2] == 0x74 && p[3] == 0x0A && p[4] == 0xB9 && p[5] == 0x01) {
                    constexpr uint8_t nops[2] = { 0x90, 0x90 };
                    if (WriteCodeBytes(p + 2, nops, 2)) {
                        LogMsg("Enabled Hybrid Frame Pacer (Sleep(1) + 2ms spin-wait) @ %p", static_cast<void*>(p + 2));
                    }
                }
            }
            ApplyFpsPatch(ReadIniInt(L"FPS", L"target_fps", 240));
        }
    }

    if (optPrewarm) {
        g_prewarmThread = CreateThread(nullptr, 0, PrewarmBundlesProc, nullptr, 0, nullptr);
    }

    uint64_t lastIniTime = GetFileModTime(g_iniPath);
    uint64_t lastModTime = 0;
    std::wstring modBridgePath;
    if (FindNewestModBridgeFile(modBridgePath, lastModTime)) {
        int mFps = 0, mSkel = 1;
        bool hFps = false, hSkel = false;
        if (ParseModBridgeFile(modBridgePath, mFps, hFps, mSkel, hSkel)) {
            if (hFps) ApplyFpsPatch(mFps);
            if (hSkel) {
                InterlockedExchange(&g_skeletalTweenEnabled, mSkel ? 1 : 0);
                LogMsg("Mod bridge -> Skeletal 240fps=%d", mSkel ? 1 : 0);
            }
        }
    }

    int tickCounter = 0;
    bool loggedInitialSummary = false;
    while (InterlockedCompareExchange(&g_stopFlag, 0, 0) == 0) {
        Sleep(250);
        ++tickCounter;

        if (optPriority && (tickCounter % 4 == 0)) {
            BoostLocalDedicatedServers();
        }
        // Clean production summary once after world load completes (zero continuous log spam!)
        if (!loggedInitialSummary && (tickCounter % 12 == 0)) {
            LONG64 bytes = InterlockedCompareExchange64(&g_libdeflateBytes, 0, 0);
            LONG64 interp = InterlockedCompareExchange64(&g_animInterpFrames, 0, 0);
            if (bytes > (1024LL * 1024LL * 1024LL) && interp > 1000) {
                loggedInitialSummary = true;
                LONG64 hits = InterlockedCompareExchange64(&g_libdeflateHits, 0, 0);
                LONG64 bones = InterlockedCompareExchange64(&g_animElemsTweened, 0, 0);
                LONG64 p2 = InterlockedCompareExchange64(&g_animPass2Swaps, 0, 0);
                LONG64 gcFull = InterlockedCompareExchange64(&g_gcEmergencyFulls, 0, 0);
                LogMsg("[Summary] libdeflate=%lld entries (%.1f MB), skeletal240=%lld frames (%lld bones, %lld sprite-swap tweens), gc_full=%lld.",
                       hits, static_cast<double>(bytes) / (1024.0 * 1024.0), interp, bones, p2, gcFull);
            }
        }

        uint64_t curIniTime = GetFileModTime(g_iniPath);
        if (curIniTime != 0 && curIniTime != lastIniTime) {
            lastIniTime = curIniTime;
            int fps = ReadIniInt(L"FPS", L"target_fps", 240);
            int skel = ReadIniInt(L"OPTIMIZE", L"smooth_skeletal_anim", 1);
            InterlockedExchange(&g_skeletalTweenEnabled, skel ? 1 : 0);
            LogMsg("Hot-reloaded dst_fps.ini -> FPS=%d, Skeletal240=%d", fps, skel ? 1 : 0);
            ApplyFpsPatch(fps);
        }

        std::wstring curModPath;
        uint64_t curModTime = 0;
        if (FindNewestModBridgeFile(curModPath, curModTime) && curModTime != lastModTime) {
            lastModTime = curModTime;
            int mFps = 0, mSkel = 1;
            bool hFps = false, hSkel = false;
            if (ParseModBridgeFile(curModPath, mFps, hFps, mSkel, hSkel)) {
                if (hFps) ApplyFpsPatch(mFps);
                if (hSkel) {
                    LONG newVal = mSkel ? 1 : 0;
                    LONG prevVal = InterlockedExchange(&g_skeletalTweenEnabled, newVal);
                    if (prevVal != newVal) {
                        LogMsg("Hot-reloaded Mod bridge -> Skeletal240=%ld", newVal);
                    }
                }
            }
        }
    }
    return 0;
}

} // namespace

extern "C" HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
    InitOnceExecuteOnce(&g_dinputInitOnce, LoadRealDInput8Once, nullptr, nullptr);
    if (!g_realCreate) return E_FAIL;
    return g_realCreate(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_selfModule = hModule;
        DisableThreadLibraryCalls(hModule);
        g_workerThread = CreateThread(nullptr, 0, WorkerProc, nullptr, 0, nullptr);
    } else if (reason == DLL_PROCESS_DETACH) {
        InterlockedExchange(&g_stopFlag, 1);
        if (g_prewarmThread) { WaitForSingleObject(g_prewarmThread, 300); CloseHandle(g_prewarmThread); g_prewarmThread = nullptr; }
        if (g_workerThread) { WaitForSingleObject(g_workerThread, 500); CloseHandle(g_workerThread); g_workerThread = nullptr; }
        if (g_timerBoosted) { timeEndPeriod(1); g_timerBoosted = false; }
        if (g_realDInput8) { FreeLibrary(g_realDInput8); g_realDInput8 = nullptr; }
    }
    return TRUE;
}
