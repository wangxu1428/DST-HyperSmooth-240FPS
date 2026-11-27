#pragma once
// =============================================================================
// Klei Persistent-String Codec (Pure Cross-Platform C++ / SIMD libdeflate)
// =============================================================================
// Implements DST's exact encoded save format:
//   "KLEI     1D" + Base64( [uint32 ver=1][uint32 hdrSz=16][uint32 rawLen]
//                           [uint32 compLen] + zlib_stream )
// Verified against dontstarve_steam_x64.exe EncodePersistentString @ 0x14012dfc0
// and ZipAndBase64Encode @ 0x1403c8400.
// Zero STL string ABI coupling across MSVC 2008 / libstdc++.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef KLEI_SAVE_CODEC_NO_STL
#include <vector>
#endif

#if __has_include("libdeflate.h")
#include "libdeflate.h"
#else
#include "../third_party/libdeflate-1.23/libdeflate.h"
#endif

namespace klei_save_codec {

inline constexpr char   kHeaderCompressed[12] = "KLEI     1D";
inline constexpr char   kHeaderPlain[12]      = "KLEI     1 ";
inline constexpr size_t kHeaderPrefixLen      = 11u;
inline constexpr size_t kMinDeferRawBytes     = 4096u;
inline constexpr size_t kMaxDeferRawBytes     = 64u * 1024u * 1024u;
inline constexpr size_t kDeferRawTokenLen     = 29u; // "__HC_DEFER_%016llX__"
inline constexpr size_t kDeferEncodedTokenLen = kHeaderPrefixLen + kDeferRawTokenLen; // 40 bytes

inline void FormatDeferRawToken(uint64_t seq, char out[32]) noexcept {
    std::snprintf(out, 32, "__HC_DEFER_%016llX__", static_cast<unsigned long long>(seq));
}

inline bool MatchDeferEncodedToken(const void* data, size_t size, uint64_t expectedSeq) noexcept {
    if (!data || size != kDeferEncodedTokenLen) return false;
    char expected[48];
    const int n = std::snprintf(expected, sizeof(expected), "KLEI     1 __HC_DEFER_%016llX__",
                                static_cast<unsigned long long>(expectedSeq));
    if (n != static_cast<int>(kDeferEncodedTokenLen)) return false;
    return std::memcmp(data, expected, kDeferEncodedTokenLen) == 0;
}

inline bool EncodeKleiZlibBase64Malloc(const void* rawData,
                                       size_t rawLen,
                                       int deflateLevel,
                                       uint8_t** outBuf,
                                       size_t* outLen,
                                       libdeflate_compressor* optComp = nullptr) noexcept {
    if (outBuf) *outBuf = nullptr;
    if (outLen) *outLen = 0;
    if (!rawData || rawLen == 0 || rawLen > 0x7FFFFFFFu || !outBuf || !outLen) {
        return false;
    }
    const int lvl = (deflateLevel >= 1 && deflateLevel <= 12) ? deflateLevel : 6;
    libdeflate_compressor* comp = optComp;
    bool ownComp = false;
    if (!comp) {
        comp = libdeflate_alloc_compressor(lvl);
        if (!comp) return false;
        ownComp = true;
    }

    const size_t compBound = libdeflate_zlib_compress_bound(comp, rawLen);
    auto* tmp = static_cast<uint8_t*>(std::malloc(16u + compBound));
    if (!tmp) {
        if (ownComp) libdeflate_free_compressor(comp);
        return false;
    }
    const size_t compLen = libdeflate_zlib_compress(
        comp, rawData, rawLen, tmp + 16u, compBound);

    if (ownComp) {
        libdeflate_free_compressor(comp);
    }
    if (compLen == 0 || compLen > 0x7FFFFFFFu) {
        std::free(tmp);
        return false;
    }

    const uint32_t hdr[4] = {
        1u,
        16u,
        static_cast<uint32_t>(rawLen),
        static_cast<uint32_t>(compLen)
    };
    std::memcpy(tmp, hdr, 16u);
    const size_t totalBin = 16u + compLen;

    static constexpr char kB64[65] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const size_t b64Len = ((totalBin + 2u) / 3u) * 4u;
    const size_t finalLen = kHeaderPrefixLen + b64Len;
    auto* encoded = static_cast<uint8_t*>(std::malloc(finalLen));
    if (!encoded) {
        std::free(tmp);
        return false;
    }
    std::memcpy(encoded, kHeaderCompressed, kHeaderPrefixLen);

    uint8_t* dst = encoded + kHeaderPrefixLen;
    size_t i = 0;
    size_t j = 0;
    while (i + 3u <= totalBin) {
        const uint32_t v = (static_cast<uint32_t>(tmp[i]) << 16) |
                           (static_cast<uint32_t>(tmp[i + 1]) << 8) |
                            static_cast<uint32_t>(tmp[i + 2]);
        dst[j]     = static_cast<uint8_t>(kB64[(v >> 18) & 0x3Fu]);
        dst[j + 1] = static_cast<uint8_t>(kB64[(v >> 12) & 0x3Fu]);
        dst[j + 2] = static_cast<uint8_t>(kB64[(v >> 6)  & 0x3Fu]);
        dst[j + 3] = static_cast<uint8_t>(kB64[ v        & 0x3Fu]);
        i += 3u;
        j += 4u;
    }
    const size_t rem = totalBin - i;
    if (rem == 1u) {
        const uint32_t v = static_cast<uint32_t>(tmp[i]) << 16;
        dst[j]     = static_cast<uint8_t>(kB64[(v >> 18) & 0x3Fu]);
        dst[j + 1] = static_cast<uint8_t>(kB64[(v >> 12) & 0x3Fu]);
        dst[j + 2] = '=';
        dst[j + 3] = '=';
    } else if (rem == 2u) {
        const uint32_t v = (static_cast<uint32_t>(tmp[i]) << 16) |
                           (static_cast<uint32_t>(tmp[i + 1]) << 8);
        dst[j]     = static_cast<uint8_t>(kB64[(v >> 18) & 0x3Fu]);
        dst[j + 1] = static_cast<uint8_t>(kB64[(v >> 12) & 0x3Fu]);
        dst[j + 2] = static_cast<uint8_t>(kB64[(v >> 6)  & 0x3Fu]);
        dst[j + 3] = '=';
    }
    std::free(tmp);
    *outBuf = encoded;
    *outLen = finalLen;
    return true;
}

inline int DecodeB64Char(uint8_t c) noexcept {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

inline bool DecodeKleiZlibBase64Malloc(const void* encoded,
                                       size_t encLen,
                                       uint8_t** outBuf,
                                       size_t* outLen) noexcept {
    if (outBuf) *outBuf = nullptr;
    if (outLen) *outLen = 0;
    if (!encoded || encLen < kHeaderPrefixLen + 24u || !outBuf || !outLen) return false;
    const auto* src = static_cast<const uint8_t*>(encoded);
    if (std::memcmp(src, kHeaderCompressed, kHeaderPrefixLen) != 0) return false;

    const uint8_t* b64 = src + kHeaderPrefixLen;
    const size_t b64Len = encLen - kHeaderPrefixLen;
    if ((b64Len % 4u) != 0u || b64Len == 0u) return false;

    auto* bin = static_cast<uint8_t*>(std::malloc((b64Len / 4u) * 3u));
    if (!bin) return false;
    size_t binLen = 0;
    for (size_t i = 0; i < b64Len; i += 4u) {
        const int a = DecodeB64Char(b64[i]);
        const int b = DecodeB64Char(b64[i + 1]);
        if (a < 0 || b < 0) { std::free(bin); return false; }
        const bool pad2 = (b64[i + 2] == '=');
        const bool pad3 = (b64[i + 3] == '=');
        if (pad2 && !pad3) { std::free(bin); return false; }
        const int c = pad2 ? 0 : DecodeB64Char(b64[i + 2]);
        const int d = pad3 ? 0 : DecodeB64Char(b64[i + 3]);
        if (c < 0 || d < 0) { std::free(bin); return false; }
        if ((pad2 || pad3) && (i + 4u != b64Len)) { std::free(bin); return false; }

        const uint32_t v = (static_cast<uint32_t>(a) << 18) |
                           (static_cast<uint32_t>(b) << 12) |
                           (static_cast<uint32_t>(c) << 6)  |
                            static_cast<uint32_t>(d);
        bin[binLen++] = static_cast<uint8_t>((v >> 16) & 0xFFu);
        if (!pad2) bin[binLen++] = static_cast<uint8_t>((v >> 8) & 0xFFu);
        if (!pad3) bin[binLen++] = static_cast<uint8_t>(v & 0xFFu);
    }

    if (binLen < 16u) { std::free(bin); return false; }
    uint32_t hdr[4]{};
    std::memcpy(hdr, bin, 16u);
    if (hdr[0] != 1u || hdr[1] != 16u ||
        static_cast<size_t>(hdr[3]) + 16u != binLen ||
        hdr[2] == 0u || hdr[2] > kMaxDeferRawBytes) {
        std::free(bin);
        return false;
    }

    auto* raw = static_cast<uint8_t*>(std::malloc(hdr[2]));
    if (!raw) { std::free(bin); return false; }
    libdeflate_decompressor* decomp = libdeflate_alloc_decompressor();
    if (!decomp) { std::free(raw); std::free(bin); return false; }
    size_t actualOut = 0;
    const auto res = libdeflate_zlib_decompress(
        decomp, bin + 16u, hdr[3], raw, hdr[2], &actualOut);
    libdeflate_free_decompressor(decomp);
    std::free(bin);
    if (res != LIBDEFLATE_SUCCESS || actualOut != hdr[2]) {
        std::free(raw);
        return false;
    }
    *outBuf = raw;
    *outLen = hdr[2];
    return true;
}

#ifndef KLEI_SAVE_CODEC_NO_STL
inline bool EncodeKleiZlibBase64(const void* rawData,
                                 size_t rawLen,
                                 int deflateLevel,
                                 std::vector<uint8_t>& outEncoded,
                                 libdeflate_compressor* optComp = nullptr) {
    uint8_t* buf = nullptr;
    size_t len = 0;
    if (!EncodeKleiZlibBase64Malloc(rawData, rawLen, deflateLevel, &buf, &len, optComp)) {
        return false;
    }
    outEncoded.assign(buf, buf + len);
    std::free(buf);
    return true;
}

inline bool DecodeKleiZlibBase64(const void* encoded, size_t encLen, std::vector<uint8_t>& outRaw) {
    uint8_t* buf = nullptr;
    size_t len = 0;
    if (!DecodeKleiZlibBase64Malloc(encoded, encLen, &buf, &len)) {
        return false;
    }
    outRaw.assign(buf, buf + len);
    std::free(buf);
    return true;
}
#endif

} // namespace klei_save_codec
