#pragma once
// =============================================================================
// HyperSmooth Generic GPU Driver Compatibility & ColourCube Self-Adapter
// -----------------------------------------------------------------------------
// Pure-GL/WGL hardware abstraction layer isolating 4 independent mechanisms:
//   Layer 1: Legal desktop GLSL compat promotion + texelFetch bilinear LUT
//            sampling (bypasses texture sampler AF / LOD bias / driver grad).
//   Layer 2: Confirmed-identity ColourCube 24-bit GL_RGB -> 32-bit GL_RGBA8
//            promotion strictly on draw-time confirmed LUT sampler slots
//            (never guesses by 1024x32 size; guards PBO / UNPACK / SubImage /
//             nullptr FBO allocations; preserves exact Alpha=1.0 semantics).
//   Layer 3: Exact wglChoosePixelFormatARB selection + mandatory attribute
//            readback verification (treats 10-bit RGBA non-destructively).
//   Layer 4: Startup & live-context real-path ColourCube Canary verification
//            reusing production shader/upload transforms across Identity LUT,
//            Non-Identity LUT, Combine-FBO chain, and Premultiplied UI_CC
//            alpha blend with gap-free [0..2]/[3..4]/[5..255] verdicts.
//
// Zero intrusion into animation, particles, physics, Lua VM, networking, or
// save subsystems. Zero third-party runtime dependencies. C++20 -fno-exceptions.
// =============================================================================

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace hs_gpu_compat {

inline constexpr uint32_t kGlTexture2D              = 0x0DE1u;
inline constexpr uint32_t kGlTexture0               = 0x84C0u;
inline constexpr uint32_t kGlTextureBinding2D       = 0x8069u;
inline constexpr uint32_t kGlActiveTexture          = 0x84E0u;
inline constexpr uint32_t kGlPixelUnpackBuffer      = 0x88ECu;
inline constexpr uint32_t kGlPixelUnpackBufferBind  = 0x88EFu;
inline constexpr uint32_t kGlArrayBuffer            = 0x8892u;
inline constexpr uint32_t kGlArrayBufferBind        = 0x8894u;
inline constexpr uint32_t kGlElementArrayBufferBind = 0x8895u;
inline constexpr uint32_t kGlUnpackAlignment        = 0x0CF5u;
inline constexpr uint32_t kGlPackAlignment          = 0x0D05u;
inline constexpr uint32_t kGlRgb                    = 0x1907u;
inline constexpr uint32_t kGlRgba                   = 0x1908u;
inline constexpr uint32_t kGlRgba8                  = 0x8058u;
inline constexpr uint32_t kGlUnsignedByte           = 0x1401u;
inline constexpr uint32_t kGlFloat                  = 0x1406u;
inline constexpr uint32_t kGlFramebuffer            = 0x8D40u;
inline constexpr uint32_t kGlDrawFramebufferBind    = 0x8CA6u;
inline constexpr uint32_t kGlReadFramebufferBind    = 0x8CAAu;
inline constexpr uint32_t kGlColorAttachment0       = 0x8CE0u;
inline constexpr uint32_t kGlFramebufferComplete    = 0x8CD5u;
inline constexpr uint32_t kGlTextureMinFilter       = 0x2801u;
inline constexpr uint32_t kGlTextureMagFilter       = 0x2800u;
inline constexpr uint32_t kGlTextureWrapS           = 0x2802u;
inline constexpr uint32_t kGlTextureWrapT           = 0x2803u;
inline constexpr uint32_t kGlTextureMaxAnisotropyExt = 0x84FEu;
inline constexpr uint32_t kGlMaxTexMaxAnisotropyExt  = 0x84FFu;
inline constexpr uint32_t kGlTextureLodBias         = 0x8501u;
inline constexpr uint32_t kGlNearest                = 0x2600u;
inline constexpr uint32_t kGlLinear                 = 0x2601u;
inline constexpr uint32_t kGlClampToEdge            = 0x812Fu;
inline constexpr uint32_t kGlVertexShader           = 0x8B31u;
inline constexpr uint32_t kGlFragmentShader         = 0x8B30u;
inline constexpr uint32_t kGlCompileStatus          = 0x8B81u;
inline constexpr uint32_t kGlLinkStatus             = 0x8B82u;
inline constexpr uint32_t kGlCurrentProgram         = 0x8B8Du;
inline constexpr uint32_t kGlStaticDraw             = 0x88E4u;
inline constexpr uint32_t kGlTriangleStrip          = 0x0005u;
inline constexpr uint32_t kGlColorBufferBit         = 0x00004000u;
inline constexpr uint32_t kGlViewport               = 0x0BA2u;
inline constexpr uint32_t kGlBlend                  = 0x0BE2u;
inline constexpr uint32_t kGlBlendSrcRgb            = 0x80C9u;
inline constexpr uint32_t kGlBlendDstRgb            = 0x80C8u;
inline constexpr uint32_t kGlBlendSrcAlpha          = 0x80CBu;
inline constexpr uint32_t kGlBlendDstAlpha          = 0x80CAu;
inline constexpr uint32_t kGlScissorTest            = 0x0C11u;
inline constexpr uint32_t kGlDepthTest              = 0x0B71u;
inline constexpr uint32_t kGlStencilTest            = 0x0B90u;
inline constexpr uint32_t kGlCullFace               = 0x0B44u;
inline constexpr uint32_t kGlOne                    = 1u;
inline constexpr uint32_t kGlOneMinusSrcAlpha       = 0x0303u;

inline constexpr int kCanaryPassMaxErr = 2; // [0, 2] -> Pass
inline constexpr int kCanaryWarnMaxErr = 4; // [3, 4] -> PassWithWarn; >=5 or <0 -> Fail

struct CompatConfig {
    bool enableCcTexelFetch   = true; // Layer 1
    bool enableCcRgbNormalize = true; // Layer 2
    bool enableWglExactPfd    = true; // Layer 3
    bool enableGlCanaryProbe  = true; // Layer 4
};

enum class CanaryVerdict : uint8_t {
    Pass         = 0,
    PassWithWarn = 1,
    Fail         = 2,
};

inline CanaryVerdict ClassifyCanaryError(int maxChannelErr) noexcept {
    if (maxChannelErr < 0 || maxChannelErr > kCanaryWarnMaxErr) {
        return CanaryVerdict::Fail;
    }
    if (maxChannelErr > kCanaryPassMaxErr) {
        return CanaryVerdict::PassWithWarn;
    }
    return CanaryVerdict::Pass;
}

inline const char* CanaryVerdictName(CanaryVerdict v) noexcept {
    switch (v) {
        case CanaryVerdict::Pass:         return "PASS";
        case CanaryVerdict::PassWithWarn: return "PASS_WARN";
        default:                          return "FAIL";
    }
}

struct PixelFormatInfo {
    int  pixelFormat           = 0;
    int  redBits               = 0;
    int  greenBits             = 0;
    int  blueBits              = 0;
    int  alphaBits             = 0;
    int  depthBits             = 0;
    int  stencilBits           = 0;
    bool isRgba                = false;
    bool isFloat               = false;
    bool isHardwareAccelerated = false;
    bool usedArbChoose         = false;
    bool is10BitRgb            = false;
    bool isExact8888           = false;
    bool isValidForProbe       = false;
};

struct CanaryReport {
    bool            ran             = false;
    bool            passed          = false;
    bool            texelFetchReady = false;
    CanaryVerdict   verdict         = CanaryVerdict::Fail;
    std::string     stage;
    int             worstX          = -1;
    int             worstY          = -1;
    uint8_t         expRgba[4]{};
    uint8_t         gotRgba[4]{};
    int             maxErr          = -1;
    char            worstChannel    = '?';
    uint32_t        glError         = 0u;
    double          elapsedMs       = 0.0;
    std::string     vendor;
    std::string     renderer;
    std::string     version;
    PixelFormatInfo pfd{};
    std::string     decision;
    std::string     fallbackReason;
    std::string     detail;
};

// =============================================================================
// Layer 1: Legal Desktop GLSL Compatibility Promotion & texelFetch Transform
// =============================================================================

inline int SelectLegalCompatGlslVersion(int glMajor, int glMinor, int minExistingVer) noexcept {
    if (glMajor < 3) return 0; // texelFetch / textureSize require GL 3.0+ (GLSL 1.30+)
    int target = 130;
    if (glMajor > 3 || (glMajor == 3 && glMinor >= 3)) {
        target = 330; // `#version 330 compatibility`
    } else if (glMajor == 3 && glMinor == 2) {
        target = 150; // `#version 150 compatibility`
    } else {
        target = 130; // `#version 130` (never emit illegal `#version 140 compatibility`!)
    }
    return (minExistingVer > target) ? minExistingVer : target;
}

inline std::string FormatGlslCompatVersionLine(int versionNum) {
    char buf[48]{};
    if (versionNum >= 150) {
        std::snprintf(buf, sizeof(buf), "#version %d compatibility\n", versionNum);
    } else {
        std::snprintf(buf, sizeof(buf), "#version %d\n", versionNum);
    }
    return std::string(buf);
}

inline bool FindGlslVersionLineRange(const std::string& src,
                                     size_t& outStart,
                                     size_t& outEnd,
                                     int& outVersion) noexcept {
    outStart = 0u;
    outEnd = 0u;
    outVersion = 0;
    size_t i = 0u;
    const size_t n = src.size();
    bool inBlockComment = false;
    while (i < n) {
        if (inBlockComment) {
            if (i + 1u < n && src[i] == '*' && src[i + 1u] == '/') {
                inBlockComment = false;
                i += 2u;
            } else {
                ++i;
            }
            continue;
        }
        while (i < n && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r' || src[i] == '\n')) ++i;
        if (i >= n) break;
        if (i + 1u < n && src[i] == '/' && src[i + 1u] == '/') {
            while (i < n && src[i] != '\n') ++i;
            continue;
        }
        if (i + 1u < n && src[i] == '/' && src[i + 1u] == '*') {
            inBlockComment = true;
            i += 2u;
            continue;
        }
        if (src[i] == '#') {
            const size_t hashPos = i;
            size_t j = i + 1u;
            while (j < n && (src[j] == ' ' || src[j] == '\t')) ++j;
            if (j + 7u <= n && std::memcmp(src.data() + j, "version", 7u) == 0 &&
                (j + 7u == n || src[j + 7u] == ' ' || src[j + 7u] == '\t')) {
                j += 7u;
                while (j < n && (src[j] == ' ' || src[j] == '\t')) ++j;
                int v = 0;
                while (j < n && src[j] >= '0' && src[j] <= '9') {
                    v = v * 10 + (src[j] - '0');
                    ++j;
                }
                const size_t nl = src.find('\n', hashPos);
                outStart = hashPos;
                outEnd = (nl == std::string::npos) ? n : (nl + 1u);
                outVersion = v;
                return v > 0;
            }
        }
        return false; // First active directive/token is not `#version`
    }
    return false;
}

inline int ParseGlslVersionNumberCompat(const std::string& src) noexcept {
    size_t s = 0u, e = 0u;
    int v = 0;
    return FindGlslVersionLineRange(src, s, e, v) ? v : 0;
}

inline bool PromoteGlslVersionCompat(std::string& src, int targetVer) {
    if (targetVer < 130 || targetVer > 460) return false;
    size_t vStart = 0u, vEnd = 0u;
    int existingVer = 0;
    if (FindGlslVersionLineRange(src, vStart, vEnd, existingVer)) {
        if (existingVer >= targetVer) {
            if (targetVer >= 150) {
                const std::string existingLine = src.substr(vStart, vEnd - vStart);
                if (existingLine.find("core") == std::string::npos &&
                    existingLine.find("compatibility") == std::string::npos) {
                    src.replace(vStart, vEnd - vStart, FormatGlslCompatVersionLine(existingVer));
                }
            }
            return true;
        }
        src.replace(vStart, vEnd - vStart, FormatGlslCompatVersionLine(targetVer));
        return true;
    }
    src.insert(0u, FormatGlslCompatVersionLine(targetVer));
    return true;
}

enum class CcShaderKind : uint8_t {
    None               = 0,
    ApplyColourCube    = 1, // postprocess_colourcube, linedistort, ui_cc, ui_anim_cc
    CombineColourCubes = 2, // combine_colour_cubes
};

inline bool HasDefineMacroCompat(const std::string& src, const char* macro) noexcept {
    if (!macro || macro[0] == '\0') return false;
    const size_t macroLen = std::strlen(macro);
    size_t pos = 0u;
    while (pos < src.size()) {
        size_t lineEnd = src.find('\n', pos);
        if (lineEnd == std::string::npos) lineEnd = src.size();
        size_t i = pos;
        while (i < lineEnd && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r')) ++i;
        if (i < lineEnd && src[i] == '#') {
            ++i;
            while (i < lineEnd && (src[i] == ' ' || src[i] == '\t')) ++i;
            if (i + 6u <= lineEnd && std::memcmp(src.data() + i, "define", 6u) == 0 &&
                (i + 6u == lineEnd || src[i + 6u] == ' ' || src[i + 6u] == '\t')) {
                i += 6u;
                while (i < lineEnd && (src[i] == ' ' || src[i] == '\t')) ++i;
                if (i + macroLen <= lineEnd && std::memcmp(src.data() + i, macro, macroLen) == 0) {
                    const size_t after = i + macroLen;
                    if (after == lineEnd || src[after] == ' ' || src[after] == '\t' || src[after] == '\r') {
                        return true;
                    }
                }
            }
        }
        pos = (lineEnd < src.size()) ? (lineEnd + 1u) : lineEnd;
    }
    return false;
}

inline CcShaderKind DetectColourCubeShaderKind(const std::string& src) noexcept {
    if (src.find("gl_FragColor") == std::string::npos) {
        return CcShaderKind::None;
    }
    if (src.find("SampleSourceDest") != std::string::npos &&
        src.find("CUBE_DIMENSION") != std::string::npos) {
        return CcShaderKind::CombineColourCubes;
    }
    if (src.find("COLOUR_CUBE") != std::string::npos &&
        src.find("ApplyColourCube") != std::string::npos) {
        if ((src.find("#if defined( UI_CC )") != std::string::npos ||
             src.find("#if defined(UI_CC)") != std::string::npos ||
             src.find("#ifdef UI_CC") != std::string::npos) &&
            !HasDefineMacroCompat(src, "UI_CC")) {
            return CcShaderKind::None;
        }
        return CcShaderKind::ApplyColourCube;
    }
    return CcShaderKind::None;
}

inline bool ExtractColourCubeSamplerExpr(const std::string& src, std::string& outExpr) {
    outExpr.clear();
    size_t pos = 0u;
    while (pos < src.size()) {
        size_t lineEnd = src.find('\n', pos);
        if (lineEnd == std::string::npos) lineEnd = src.size();
        size_t i = pos;
        while (i < lineEnd && (src[i] == ' ' || src[i] == '\t' || src[i] == '\r')) ++i;
        if (i < lineEnd && src[i] == '#') {
            ++i;
            while (i < lineEnd && (src[i] == ' ' || src[i] == '\t')) ++i;
            if (i + 6u <= lineEnd && std::memcmp(src.data() + i, "define", 6u) == 0 &&
                (i + 6u == lineEnd || src[i + 6u] == ' ' || src[i + 6u] == '\t')) {
                i += 6u;
                while (i < lineEnd && (src[i] == ' ' || src[i] == '\t')) ++i;
                if (i + 11u <= lineEnd && std::memcmp(src.data() + i, "COLOUR_CUBE", 11u) == 0 &&
                    (i + 11u == lineEnd || src[i + 11u] == ' ' || src[i + 11u] == '\t')) {
                    i += 11u;
                    while (i < lineEnd && (src[i] == ' ' || src[i] == '\t')) ++i;
                    size_t exprStart = i;
                    while (i < lineEnd) {
                        if (src[i] == '\r') break;
                        if (i + 1u < lineEnd && src[i] == '/' && (src[i + 1u] == '/' || src[i + 1u] == '*')) break;
                        ++i;
                    }
                    while (i > exprStart && (src[i - 1u] == ' ' || src[i - 1u] == '\t')) --i;
                    if (i > exprStart) {
                        outExpr.assign(src, exprStart, i - exprStart);
                        return true;
                    }
                }
            }
        }
        pos = (lineEnd < src.size()) ? (lineEnd + 1u) : lineEnd;
    }
    return false;
}

inline bool ParseSamplerArrayIndex(const std::string& expr, int32_t& outIdx) noexcept {
    outIdx = -1;
    if (expr.size() < 10u || expr.compare(0u, 8u, "SAMPLER[") != 0) return false;
    size_t p = 8u;
    while (p < expr.size() && (expr[p] == ' ' || expr[p] == '\t')) ++p;
    if (p >= expr.size() || expr[p] < '0' || expr[p] > '9') return false;
    int32_t v = 0;
    while (p < expr.size() && expr[p] >= '0' && expr[p] <= '9') {
        v = v * 10 + (expr[p] - '0');
        ++p;
    }
    while (p < expr.size() && (expr[p] == ' ' || expr[p] == '\t')) ++p;
    if (p >= expr.size() || expr[p] != ']' || v < 0 || v >= 16) return false;
    outIdx = v;
    return true;
}

inline bool IsCompatIdentChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

inline bool TransformColourCubeTexelFetchShaderSource(std::string& src, uint32_t& outReplacedCalls) {
    outReplacedCalls = 0u;
    const CcShaderKind kind = DetectColourCubeShaderKind(src);
    if (kind == CcShaderKind::None || src.find("hs_cc_sample_2d") != std::string::npos) {
        return false;
    }

    const char* targets[2]{};
    uint32_t targetCount = 0u;
    if (kind == CcShaderKind::ApplyColourCube) {
        targets[targetCount++] = "COLOUR_CUBE";
    } else {
        targets[targetCount++] = "source_cc";
        targets[targetCount++] = "dest_cc";
    }

    std::string work = src;
    for (size_t i = 0u; i < work.size();) {
        if (i + 1u < work.size() && work[i] == '/' && work[i + 1u] == '/') {
            i += 2u;
            while (i < work.size() && work[i] != '\n') ++i;
            continue;
        }
        if (i + 1u < work.size() && work[i] == '/' && work[i + 1u] == '*') {
            i += 2u;
            while (i + 1u < work.size() && !(work[i] == '*' && work[i + 1u] == '/')) ++i;
            i = (i + 1u < work.size()) ? (i + 2u) : work.size();
            continue;
        }
        if (i + 9u <= work.size() &&
            work.compare(i, 9u, "texture2D") == 0 &&
            (i == 0u || !IsCompatIdentChar(work[i - 1u])) &&
            (i + 9u == work.size() || !IsCompatIdentChar(work[i + 9u]))) {
            size_t j = i + 9u;
            while (j < work.size() && (work[j] == ' ' || work[j] == '\t' || work[j] == '\r' || work[j] == '\n')) ++j;
            if (j < work.size() && work[j] == '(') {
                ++j;
                while (j < work.size() && (work[j] == ' ' || work[j] == '\t' || work[j] == '\r' || work[j] == '\n')) ++j;
                const char* matched = nullptr;
                size_t matchedLen = 0u;
                for (uint32_t ti = 0u; ti < targetCount; ++ti) {
                    const size_t tlen = std::strlen(targets[ti]);
                    if (j + tlen <= work.size() &&
                        work.compare(j, tlen, targets[ti]) == 0 &&
                        (j + tlen == work.size() || !IsCompatIdentChar(work[j + tlen]))) {
                        matched = targets[ti];
                        matchedLen = tlen;
                        break;
                    }
                }
                if (matched != nullptr) {
                    j += matchedLen;
                    while (j < work.size() && (work[j] == ' ' || work[j] == '\t' || work[j] == '\r' || work[j] == '\n')) ++j;
                    if (j < work.size() && work[j] == ',') {
                        ++j;
                        int depth = 0;
                        bool twoArgCall = false;
                        while (j < work.size()) {
                            if (j + 1u < work.size() && work[j] == '/' && work[j + 1u] == '/') {
                                j += 2u;
                                while (j < work.size() && work[j] != '\n') ++j;
                                continue;
                            }
                            if (j + 1u < work.size() && work[j] == '/' && work[j + 1u] == '*') {
                                j += 2u;
                                while (j + 1u < work.size() && !(work[j] == '*' && work[j + 1u] == '/')) ++j;
                                j = (j + 1u < work.size()) ? (j + 2u) : work.size();
                                continue;
                            }
                            const char c = work[j];
                            if (c == '(') {
                                ++depth;
                            } else if (c == ')') {
                                if (depth == 0) {
                                    twoArgCall = true;
                                    break;
                                }
                                --depth;
                            } else if (c == ',' && depth == 0) {
                                break; // 3-arg texture2D(s, uv, bias) untouched
                            }
                            ++j;
                        }
                        if (twoArgCall) {
                            work.replace(i, 9u, "hs_cc_sample_2d");
                            ++outReplacedCalls;
                            i += 15u;
                            continue;
                        }
                    }
                }
            }
        }
        ++i;
    }

    if (outReplacedCalls == 0u) {
        return false;
    }

    // Inject right before ApplyColourCube / SampleSourceDest so scope matches `#if defined(UI_CC)`!
    size_t helperPos = std::string::npos;
    if (kind == CcShaderKind::ApplyColourCube) {
        helperPos = work.find("vec3 ApplyColourCube");
    } else {
        helperPos = work.find("vec3 SampleSourceDest");
    }
    if (helperPos == std::string::npos) {
        helperPos = work.find("void main");
    }
    if (helperPos == std::string::npos) {
        return false;
    }

    static constexpr const char* kTexelFetchFn =
        "vec4 hs_cc_sample_2d(sampler2D s, vec2 uv) {\n"
        "\tivec2 sz = textureSize(s, 0);\n"
        "\tvec2  p  = clamp(uv * vec2(sz) - vec2(0.5), vec2(-0.5), vec2(sz) - vec2(0.5));\n"
        "\tvec2  f  = fract(p);\n"
        "\tivec2 p0 = ivec2(floor(p));\n"
        "\tivec2 p1 = p0 + ivec2(1);\n"
        "\tivec2 mx = sz - ivec2(1);\n"
        "\tp0 = clamp(p0, ivec2(0), mx);\n"
        "\tp1 = clamp(p1, ivec2(0), mx);\n"
        "\tvec4 c00 = texelFetch(s, ivec2(p0.x, p0.y), 0);\n"
        "\tvec4 c10 = texelFetch(s, ivec2(p1.x, p0.y), 0);\n"
        "\tvec4 c01 = texelFetch(s, ivec2(p0.x, p1.y), 0);\n"
        "\tvec4 c11 = texelFetch(s, ivec2(p1.x, p1.y), 0);\n"
        "\treturn mix(mix(c00, c10, f.x), mix(c01, c11, f.x), f.y);\n"
        "}\n";

    work.insert(helperPos, kTexelFetchFn);
    src = std::move(work);
    return true;
}

// =============================================================================
// Layer 2: Confirmed-Identity ColourCube RGB24 -> RGBA8 Promotion Tracker
// =============================================================================

inline bool IsValidUnpackAlignment(int32_t align) noexcept {
    return align == 1 || align == 2 || align == 4 || align == 8;
}

inline bool ExpandRgb24ToRgba32(const uint8_t* src,
                                int32_t width,
                                int32_t height,
                                int32_t unpackAlignment,
                                std::vector<uint8_t>& outRgba) noexcept {
    outRgba.clear();
    if (!src || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        return false;
    }
    if (!IsValidUnpackAlignment(unpackAlignment)) {
        return false;
    }
    const size_t w = static_cast<size_t>(width);
    const size_t h = static_cast<size_t>(height);
    const size_t rawRow = w * 3u;
    const size_t align = static_cast<size_t>(unpackAlignment);
    const size_t srcStride = ((rawRow + align - 1u) / align) * align;
    const size_t dstRow = w * 4u;
    if (dstRow > (SIZE_MAX / h)) return false;
    outRgba.resize(dstRow * h);

    for (size_t y = 0u; y < h; ++y) {
        const uint8_t* sRow = src + y * srcStride;
        uint8_t* dRow = outRgba.data() + y * dstRow;
        for (size_t x = 0u; x < w; ++x) {
            dRow[x * 4u + 0u] = sRow[x * 3u + 0u];
            dRow[x * 4u + 1u] = sRow[x * 3u + 1u];
            dRow[x * 4u + 2u] = sRow[x * 3u + 2u];
            dRow[x * 4u + 3u] = 255u; // Exact OpenGL GL_RGB -> Alpha = 1.0
        }
    }
    return true;
}

struct CcProgramBindingInfo {
    CcShaderKind kind = CcShaderKind::None;
    uint32_t     samplerCount = 0u;
    int32_t      samplerLocs[6]{ -1, -1, -1, -1, -1, -1 };
    int32_t      samplerUnits[6]{ 0, 0, 0, 0, 0, 0 };
    bool         samplerUnitSet[6]{};

    bool OnUniform1i(int32_t loc, int32_t val) noexcept {
        if (loc < 0 || val < 0 || val >= 32) return false;
        bool updated = false;
        for (uint32_t i = 0u; i < samplerCount && i < 6u; ++i) {
            if (samplerLocs[i] == loc) {
                samplerUnits[i] = val;
                samplerUnitSet[i] = true;
                updated = true;
            }
        }
        return updated;
    }
};

struct PromoteGlOps {
    using ActiveTexture_t = void (WINAPI*)(uint32_t);
    using BindTexture_t   = void (WINAPI*)(uint32_t, uint32_t);
    using BindBuffer_t    = void (WINAPI*)(uint32_t, uint32_t);
    using PixelStorei_t   = void (WINAPI*)(uint32_t, int32_t);
    using TexImage2D_t    = void (WINAPI*)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*);

    ActiveTexture_t glActiveTexture = nullptr;
    BindTexture_t   glBindTexture   = nullptr;
    BindBuffer_t    glBindBuffer    = nullptr;
    PixelStorei_t   glPixelStorei   = nullptr;
    TexImage2D_t    glTexImage2D    = nullptr;

    bool IsComplete() const noexcept {
        return glActiveTexture && glBindTexture && glBindBuffer && glPixelStorei && glTexImage2D;
    }
};

class ConfirmedCcRgbTracker {
public:
    static constexpr size_t kMaxTrackedEntries    = 64u;
    static constexpr size_t kMaxSingleRgbBytes    = 512u * 1024u;
    static constexpr size_t kMaxTotalShadowBytes  = 8u * 1024u * 1024u;

    void OnTexImage2D(uint32_t texId,
                      uint32_t target,
                      int32_t level,
                      int32_t internalformat,
                      int32_t width,
                      int32_t height,
                      int32_t border,
                      uint32_t format,
                      uint32_t type,
                      const void* pixels,
                      uint32_t boundUnpackPbo,
                      int32_t unpackAlignment,
                      bool unpackComplexState = false) noexcept {
        if (texId == 0u || target != kGlTexture2D) return;
        if (level == 0) {
            EraseEntry(texId);
        } else {
            return;
        }
        if (pixels == nullptr) {
            ++skippedNullCount_;
            return;
        }
        if (boundUnpackPbo != 0u) {
            ++skippedPboCount_;
            return;
        }
        if (unpackComplexState || !IsValidUnpackAlignment(unpackAlignment)) {
            ++skippedUnpackCount_;
            return;
        }
        if (border != 0 ||
            internalformat != static_cast<int32_t>(kGlRgb) ||
            format != kGlRgb ||
            type != kGlUnsignedByte ||
            width <= 0 || height <= 0 || width > 4096 || height > 4096) {
            return;
        }
        const size_t rgbBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
        if (rgbBytes > kMaxSingleRgbBytes) {
            return;
        }

        std::vector<uint8_t> rgba;
        if (!ExpandRgb24ToRgba32(static_cast<const uint8_t*>(pixels), width, height, unpackAlignment, rgba)) {
            return;
        }

        while ((entries_.size() >= kMaxTrackedEntries || (totalShadowBytes_ + rgba.size()) > kMaxTotalShadowBytes) &&
               !entries_.empty()) {
            EvictOldest();
        }
        if (entries_.size() >= kMaxTrackedEntries || (totalShadowBytes_ + rgba.size()) > kMaxTotalShadowBytes) {
            return;
        }

        totalShadowBytes_ += rgba.size();
        Entry e{};
        e.width = width;
        e.height = height;
        e.promoted = false;
        e.lruSeq = ++seqCounter_;
        e.rgbaBytes = std::move(rgba);
        entries_.emplace(texId, std::move(e));
        ++capturedCount_;
    }

    void OnTexSubImage2D(uint32_t texId) noexcept {
        if (texId != 0u && EraseEntry(texId)) {
            ++invalidatedCount_;
        }
    }

    void OnCompressedTexImage2D(uint32_t texId) noexcept {
        if (texId != 0u && EraseEntry(texId)) {
            ++invalidatedCount_;
        }
    }

    void OnDeleteTextures(int32_t n, const uint32_t* textures) noexcept {
        if (n <= 0 || !textures) return;
        for (int32_t i = 0; i < n; ++i) {
            if (textures[i] != 0u && EraseEntry(textures[i])) {
                ++invalidatedCount_;
            }
        }
    }

    void Reset() noexcept {
        entries_.clear();
        totalShadowBytes_ = 0u;
    }

    bool TryPromoteConfirmedTexture(uint32_t texId,
                                    uint32_t samplerUnit,
                                    const PromoteGlOps& ops,
                                    uint32_t curActiveUnit,
                                    uint32_t curBoundTexOnTargetUnit,
                                    uint32_t curUnpackPbo,
                                    int32_t curUnpackAlign,
                                    int32_t* outW = nullptr,
                                    int32_t* outH = nullptr) noexcept {
        if (texId == 0u || samplerUnit >= 32u || !ops.IsComplete()) return false;
        auto it = entries_.find(texId);
        if (it == entries_.end()) return false;
        Entry& e = it->second;
        if (e.promoted || e.rgbaBytes.empty()) return false;

        if (curActiveUnit != samplerUnit) {
            ops.glActiveTexture(kGlTexture0 + samplerUnit);
        }
        if (curBoundTexOnTargetUnit != texId) {
            ops.glBindTexture(kGlTexture2D, texId);
        }
        if (curUnpackPbo != 0u) {
            ops.glBindBuffer(kGlPixelUnpackBuffer, 0u);
        }
        if (curUnpackAlign != 4) {
            ops.glPixelStorei(kGlUnpackAlignment, 4);
        }

        ops.glTexImage2D(kGlTexture2D, 0, static_cast<int32_t>(kGlRgba8),
                         e.width, e.height, 0, kGlRgba, kGlUnsignedByte, e.rgbaBytes.data());

        // Restore exact caller GL state (zero leakage into host state or GlStateCache!)
        if (curUnpackAlign != 4) {
            ops.glPixelStorei(kGlUnpackAlignment, curUnpackAlign);
        }
        if (curUnpackPbo != 0u) {
            ops.glBindBuffer(kGlPixelUnpackBuffer, curUnpackPbo);
        }
        if (curBoundTexOnTargetUnit != texId) {
            ops.glBindTexture(kGlTexture2D, curBoundTexOnTargetUnit);
        }
        if (curActiveUnit != samplerUnit) {
            ops.glActiveTexture(kGlTexture0 + curActiveUnit);
        }

        if (outW) *outW = e.width;
        if (outH) *outH = e.height;
        totalShadowBytes_ -= e.rgbaBytes.size();
        std::vector<uint8_t>().swap(e.rgbaBytes);
        e.promoted = true;
        e.lruSeq = ++seqCounter_;
        ++promotedCount_;
        return true;
    }

    bool IsTrackedPending(uint32_t texId) const noexcept {
        auto it = entries_.find(texId);
        return it != entries_.end() && !it->second.promoted && !it->second.rgbaBytes.empty();
    }

    bool IsPromoted(uint32_t texId) const noexcept {
        auto it = entries_.find(texId);
        return it != entries_.end() && it->second.promoted;
    }

    size_t   TrackedCount()       const noexcept { return entries_.size(); }
    size_t   ShadowBytes()        const noexcept { return totalShadowBytes_; }
    uint64_t CapturedCount()      const noexcept { return capturedCount_; }
    uint64_t PromotedCount()      const noexcept { return promotedCount_; }
    uint64_t SkippedPboCount()    const noexcept { return skippedPboCount_; }
    uint64_t SkippedNullCount()   const noexcept { return skippedNullCount_; }
    uint64_t SkippedUnpackCount() const noexcept { return skippedUnpackCount_; }
    uint64_t InvalidatedCount()   const noexcept { return invalidatedCount_; }

private:
    struct Entry {
        int32_t              width    = 0;
        int32_t              height   = 0;
        bool                 promoted = false;
        uint64_t             lruSeq   = 0u;
        std::vector<uint8_t> rgbaBytes;
    };

    bool EraseEntry(uint32_t texId) noexcept {
        auto it = entries_.find(texId);
        if (it == entries_.end()) return false;
        totalShadowBytes_ -= it->second.rgbaBytes.size();
        entries_.erase(it);
        return true;
    }

    void EvictOldest() noexcept {
        if (entries_.empty()) return;
        auto victim = entries_.begin();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.lruSeq < victim->second.lruSeq) {
                victim = it;
            }
        }
        totalShadowBytes_ -= victim->second.rgbaBytes.size();
        entries_.erase(victim);
    }

    std::unordered_map<uint32_t, Entry> entries_;
    size_t   totalShadowBytes_   = 0u;
    uint64_t seqCounter_         = 0u;
    uint64_t capturedCount_      = 0u;
    uint64_t promotedCount_      = 0u;
    uint64_t skippedPboCount_    = 0u;
    uint64_t skippedNullCount_   = 0u;
    uint64_t skippedUnpackCount_ = 0u;
    uint64_t invalidatedCount_   = 0u;
};

// =============================================================================
// Reference LUT Builders & Mathematical Ground Truth Evaluators
// =============================================================================

inline void BuildIdentityColourCubeRgb24(std::vector<uint8_t>& outRgb) {
    // Exact DST 1024x32 RGB24 ColourCube LUT layout (y=0 bottom is G=1.0, y=31 top is G=0.0)
    outRgb.resize(1024u * 32u * 3u);
    for (int y = 0; y < 32; ++y) {
        const float gf = static_cast<float>(31 - y) / 31.0f;
        const uint8_t gb = static_cast<uint8_t>(std::lround(gf * 255.0f));
        for (int b = 0; b < 32; ++b) {
            const float bf = static_cast<float>(b) / 31.0f;
            const uint8_t bb = static_cast<uint8_t>(std::lround(bf * 255.0f));
            for (int r = 0; r < 32; ++r) {
                const float rf = static_cast<float>(r) / 31.0f;
                const uint8_t rb = static_cast<uint8_t>(std::lround(rf * 255.0f));
                const size_t x = static_cast<size_t>(b * 32 + r);
                const size_t idx = (static_cast<size_t>(y) * 1024u + x) * 3u;
                outRgb[idx + 0u] = rb;
                outRgb[idx + 1u] = gb;
                outRgb[idx + 2u] = bb;
            }
        }
    }
}

inline void BuildStylizedNonIdentityColourCubeRgb24(std::vector<uint8_t>& outRgb) {
    // Non-linear cross-channel LUT so any channel swap, V-flip, or slice bleed fails cleanly (>40/255 error)
    outRgb.resize(1024u * 32u * 3u);
    for (int y = 0; y < 32; ++y) {
        const float g = static_cast<float>(31 - y) / 31.0f;
        for (int b = 0; b < 32; ++b) {
            const float bf = static_cast<float>(b) / 31.0f;
            for (int r = 0; r < 32; ++r) {
                const float rf = static_cast<float>(r) / 31.0f;
                const float nr = std::clamp(0.60f * rf + 0.30f * bf + 0.08f * g, 0.0f, 1.0f);
                const float ng = std::clamp(0.14f * rf + 0.68f * g + 0.14f * (1.0f - bf), 0.0f, 1.0f);
                const float nb = std::clamp(0.22f * g + 0.62f * bf + 0.12f * rf * rf + 0.04f, 0.0f, 1.0f);
                const size_t x = static_cast<size_t>(b * 32 + r);
                const size_t idx = (static_cast<size_t>(y) * 1024u + x) * 3u;
                outRgb[idx + 0u] = static_cast<uint8_t>(std::lround(nr * 255.0f));
                outRgb[idx + 1u] = static_cast<uint8_t>(std::lround(ng * 255.0f));
                outRgb[idx + 2u] = static_cast<uint8_t>(std::lround(nb * 255.0f));
            }
        }
    }
}

inline void SampleLutBilinearRef(const uint8_t* lutRgb24, float px, float py, float out[3]) noexcept {
    px = std::clamp(px, -0.5f, 1023.5f);
    py = std::clamp(py, -0.5f, 31.5f);
    const int rawX0 = static_cast<int>(std::floor(px));
    const int rawY0 = static_cast<int>(std::floor(py));
    const int x0 = std::clamp(rawX0, 0, 1023);
    const int x1 = std::clamp(rawX0 + 1, 0, 1023);
    const int y0 = std::clamp(rawY0, 0, 31);
    const int y1 = std::clamp(rawY0 + 1, 0, 31);
    const float fx = px - static_cast<float>(rawX0);
    const float fy = py - static_cast<float>(rawY0);

    for (int c = 0; c < 3; ++c) {
        const float c00 = static_cast<float>(lutRgb24[(static_cast<size_t>(y0) * 1024u + x0) * 3u + c]) / 255.0f;
        const float c10 = static_cast<float>(lutRgb24[(static_cast<size_t>(y0) * 1024u + x1) * 3u + c]) / 255.0f;
        const float c01 = static_cast<float>(lutRgb24[(static_cast<size_t>(y1) * 1024u + x0) * 3u + c]) / 255.0f;
        const float c11 = static_cast<float>(lutRgb24[(static_cast<size_t>(y1) * 1024u + x1) * 3u + c]) / 255.0f;
        const float top = c00 + (c10 - c00) * fx;
        const float bot = c01 + (c11 - c01) * fx;
        out[c] = top + (bot - top) * fy;
    }
}

inline void EvalReferenceApplyColourCubeRgb(const uint8_t* lutRgb24,
                                            float rIn,
                                            float gIn,
                                            float bIn,
                                            float outRgb[3]) noexcept {
    const float ir = rIn * 31.0f;
    const float ig = gIn * 31.0f;
    const float ib = bIn * 31.0f;
    const float bf = std::floor(ib);
    const float bc = std::ceil(ib);
    const float rx = std::min(ir + 0.5f, 31.0f);
    const float gy = std::min(ig + 0.5f, 31.0f);

    const float u0 = (rx + bf * 32.0f) / 1024.0f;
    const float u1 = (rx + bc * 32.0f) / 1024.0f;
    const float v  = 1.0f - (gy / 32.0f);

    float c0[3]{}, c1[3]{};
    SampleLutBilinearRef(lutRgb24, u0 * 1024.0f - 0.5f, v * 32.0f - 0.5f, c0);
    SampleLutBilinearRef(lutRgb24, u1 * 1024.0f - 0.5f, v * 32.0f - 0.5f, c1);
    const float w = ib - bf;
    for (int c = 0; c < 3; ++c) {
        outRgb[c] = std::clamp(c0[c] + (c1[c] - c0[c]) * w, 0.0f, 1.0f);
    }
}

inline void BuildAdversarialSceneRgba16x16(std::vector<uint8_t>& outRgba) {
    // 16x16 adversarial image combining primaries, exact midpoint texels, and adjacent-pixel
    // extreme B-slice jumps within every 2x2 quad to expose AF / gradient bugs.
    outRgba.resize(16u * 16u * 4u);
    auto setPx = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255u) {
        const size_t idx = (static_cast<size_t>(y) * 16u + static_cast<size_t>(x)) * 4u;
        outRgba[idx + 0u] = r;
        outRgba[idx + 1u] = g;
        outRgba[idx + 2u] = b;
        outRgba[idx + 3u] = a;
    };
    const uint8_t palette[16][3] = {
        {   0,   0,   0 }, { 255, 255, 255 }, { 255,   0,   0 }, {   0, 255,   0 },
        {   0,   0, 255 }, { 255, 255,   0 }, {   0, 255, 255 }, { 255,   0, 255 },
        { 128, 128, 128 }, { 247,   8, 255 }, {   8, 247,   0 }, { 123, 211,  45 },
        {  37,  91, 203 }, { 210, 140,  17 }, {  64, 192, 128 }, { 255, 128,  64 },
    };
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (y == 0) {
                setPx(x, y, palette[x][0], palette[x][1], palette[x][2], 255u);
            } else if ((y & 1) == 1) {
                // High-frequency adjacent-pixel B-slice ping-pong inside every 2x2 quad
                const uint8_t r = static_cast<uint8_t>((x * 17) & 0xFF);
                const uint8_t g = static_cast<uint8_t>((y * 16) & 0xFF);
                const uint8_t b = (((x + y) & 1) == 0) ? 0u : 255u;
                setPx(x, y, r, g, b, 255u);
            } else {
                // Fractional R/G/B ramp covering slice boundaries (e.g. ~8, 16, 24, ... out of 255)
                const uint8_t r = static_cast<uint8_t>((x * 15 + 4) & 0xFF);
                const uint8_t g = static_cast<uint8_t>((y * 14 + 12) & 0xFF);
                const uint8_t b = static_cast<uint8_t>(((x * 23 + y * 11) % 256));
                setPx(x, y, r, g, b, 255u);
            }
        }
    }
}

// =============================================================================
// Layer 4: Real-Path Startup & Live-Context ColourCube Canary Probe
// =============================================================================

struct CanaryGlOps {
    void (WINAPI* glGenFramebuffers)(int32_t, uint32_t*) = nullptr;
    void (WINAPI* glDeleteFramebuffers)(int32_t, const uint32_t*) = nullptr;
    void (WINAPI* glBindFramebuffer)(uint32_t, uint32_t) = nullptr;
    void (WINAPI* glFramebufferTexture2D)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t) = nullptr;
    uint32_t (WINAPI* glCheckFramebufferStatus)(uint32_t) = nullptr;
    void (WINAPI* glGenTextures)(int32_t, uint32_t*) = nullptr;
    void (WINAPI* glDeleteTextures)(int32_t, const uint32_t*) = nullptr;
    void (WINAPI* glActiveTexture)(uint32_t) = nullptr;
    void (WINAPI* glBindTexture)(uint32_t, uint32_t) = nullptr;
    void (WINAPI* glTexImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*) = nullptr;
    void (WINAPI* glTexParameteri)(uint32_t, uint32_t, int32_t) = nullptr;
    void (WINAPI* glTexParameterf)(uint32_t, uint32_t, float) = nullptr;
    uint32_t (WINAPI* glCreateShader)(uint32_t) = nullptr;
    void (WINAPI* glShaderSource)(uint32_t, int32_t, const char* const*, const int32_t*) = nullptr;
    void (WINAPI* glCompileShader)(uint32_t) = nullptr;
    void (WINAPI* glGetShaderiv)(uint32_t, uint32_t, int32_t*) = nullptr;
    void (WINAPI* glGetShaderInfoLog)(uint32_t, int32_t, int32_t*, char*) = nullptr;
    void (WINAPI* glDeleteShader)(uint32_t) = nullptr;
    uint32_t (WINAPI* glCreateProgram)() = nullptr;
    void (WINAPI* glAttachShader)(uint32_t, uint32_t) = nullptr;
    void (WINAPI* glBindAttribLocation)(uint32_t, uint32_t, const char*) = nullptr;
    void (WINAPI* glLinkProgram)(uint32_t) = nullptr;
    void (WINAPI* glGetProgramiv)(uint32_t, uint32_t, int32_t*) = nullptr;
    void (WINAPI* glGetProgramInfoLog)(uint32_t, int32_t, int32_t*, char*) = nullptr;
    void (WINAPI* glDeleteProgram)(uint32_t) = nullptr;
    void (WINAPI* glUseProgram)(uint32_t) = nullptr;
    int32_t (WINAPI* glGetUniformLocation)(uint32_t, const char*) = nullptr;
    void (WINAPI* glUniform1f)(int32_t, float) = nullptr;
    void (WINAPI* glUniform1i)(int32_t, int32_t) = nullptr;
    void (WINAPI* glUniform2fv)(int32_t, int32_t, const float*) = nullptr;
    void (WINAPI* glUniform3fv)(int32_t, int32_t, const float*) = nullptr;
    void (WINAPI* glUniform4fv)(int32_t, int32_t, const float*) = nullptr;
    void (WINAPI* glUniformMatrix4fv)(int32_t, int32_t, uint8_t, const float*) = nullptr;
    void (WINAPI* glGenBuffers)(int32_t, uint32_t*) = nullptr;
    void (WINAPI* glDeleteBuffers)(int32_t, const uint32_t*) = nullptr;
    void (WINAPI* glBindBuffer)(uint32_t, uint32_t) = nullptr;
    void (WINAPI* glBufferData)(uint32_t, ptrdiff_t, const void*, uint32_t) = nullptr;
    void (WINAPI* glEnableVertexAttribArray)(uint32_t) = nullptr;
    void (WINAPI* glDisableVertexAttribArray)(uint32_t) = nullptr;
    void (WINAPI* glVertexAttribPointer)(uint32_t, int32_t, uint32_t, uint8_t, int32_t, const void*) = nullptr;
    void (WINAPI* glDrawArrays)(uint32_t, int32_t, int32_t) = nullptr;
    void (WINAPI* glViewport)(int32_t, int32_t, int32_t, int32_t) = nullptr;
    void (WINAPI* glEnable)(uint32_t) = nullptr;
    void (WINAPI* glDisable)(uint32_t) = nullptr;
    void (WINAPI* glBlendFunc)(uint32_t, uint32_t) = nullptr;
    void (WINAPI* glClearColor)(float, float, float, float) = nullptr;
    void (WINAPI* glClear)(uint32_t) = nullptr;
    void (WINAPI* glPixelStorei)(uint32_t, int32_t) = nullptr;
    void (WINAPI* glReadPixels)(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void*) = nullptr;
    void (WINAPI* glFinish)() = nullptr;
    uint32_t (WINAPI* glGetError)() = nullptr;
    void (WINAPI* glGetIntegerv)(uint32_t, int32_t*) = nullptr;
    void (WINAPI* glGetFloatv)(uint32_t, float*) = nullptr;
    uint8_t (WINAPI* glIsEnabled)(uint32_t) = nullptr;

    bool IsComplete() const noexcept {
        return glGenFramebuffers && glDeleteFramebuffers && glBindFramebuffer &&
               glFramebufferTexture2D && glCheckFramebufferStatus &&
               glGenTextures && glDeleteTextures && glActiveTexture && glBindTexture &&
               glTexImage2D && glTexParameteri &&
               glCreateShader && glShaderSource && glCompileShader && glGetShaderiv && glDeleteShader &&
               glCreateProgram && glAttachShader && glBindAttribLocation && glLinkProgram && glGetProgramiv &&
               glDeleteProgram && glUseProgram && glGetUniformLocation &&
               glUniform1f && glUniform1i && glUniform2fv && glUniform3fv && glUniform4fv && glUniformMatrix4fv &&
               glGenBuffers && glDeleteBuffers && glBindBuffer && glBufferData &&
               glEnableVertexAttribArray && glDisableVertexAttribArray && glVertexAttribPointer &&
               glDrawArrays && glViewport && glEnable && glDisable && glBlendFunc &&
               glClearColor && glClear && glPixelStorei && glReadPixels &&
               glFinish && glGetError && glGetIntegerv && glIsEnabled;
    }
};

// Verbatim stock DST shaders (from data/databundles/shaders.zip) for real-path canary compilation
inline constexpr const char* kDstStockPostProcessCcVs = R"GLSL(
attribute vec3 POSITION;
attribute vec2 TEXCOORD0;

varying vec2 PS_TEXCOORD0;

void main()
{
	gl_Position = vec4( POSITION.xyz, 1.0 );
	PS_TEXCOORD0.xy = TEXCOORD0.xy;
}
)GLSL";

inline constexpr const char* kDstStockPostProcessCcFs = R"GLSL(
#if defined( GL_ES )
precision highp float;
#endif

uniform sampler2D SAMPLER[2];

varying vec2 PS_TEXCOORD0;

#define SRC_IMAGE SAMPLER[0]

#define COLOUR_CUBE SAMPLER[1]
#ifndef COLOURCUBE_H
#define COLOURCUBE_H

#ifndef COLOUR_CUBE
	#error If you use colourcube, you must #define the sampler that the colourcube belongs to
#endif

const float CUBE_DIMENSION = 32.0;
const float CUBE_WIDTH = ( CUBE_DIMENSION * CUBE_DIMENSION );
const float CUBE_HEIGHT =( CUBE_DIMENSION );
const float ONE_OVER_CUBE_WIDTH =  1.0 / CUBE_WIDTH;
const float ONE_OVER_CUBE_HEIGHT =  1.0 / CUBE_HEIGHT;

//make sure to premultiply the alpha if its value isn't 1!
vec3 ApplyColourCube(vec3 colour)
{
	vec3 intermediate = colour.rgb * vec3( CUBE_DIMENSION - 1.0, CUBE_DIMENSION - 1.0, CUBE_DIMENSION - 1.0 );

	vec2 floor_uv = vec2( ( min( intermediate.r + 0.5, 31.0 ) + floor( intermediate.b ) * CUBE_DIMENSION ) * ONE_OVER_CUBE_WIDTH,1.0 - ( min( intermediate.g + 0.5, 31.0 ) * ONE_OVER_CUBE_HEIGHT ) );
	vec2 ceil_uv = vec2( ( min( intermediate.r + 0.5, 31.0 ) + ceil( intermediate.b ) * CUBE_DIMENSION ) * ONE_OVER_CUBE_WIDTH,1.0 - ( min( intermediate.g + 0.5, 31.0 ) * ONE_OVER_CUBE_HEIGHT ) );
	vec3 floor_col = texture2D( COLOUR_CUBE, floor_uv.xy ).rgb;
	vec3 ceil_col = texture2D( COLOUR_CUBE, ceil_uv.xy ).rgb;
	return mix(floor_col, ceil_col, intermediate.b - floor(intermediate.b) );
}

#endif //COLOURCUBE.h


void main()
{
    gl_FragColor = vec4( ApplyColourCube(texture2D( SRC_IMAGE, PS_TEXCOORD0.xy ).rgb), 1.0 );
}
)GLSL";

inline constexpr const char* kDstStockCombineCcVs = R"GLSL(
attribute vec3 POSITION;
attribute vec2 TEXCOORD0;

varying vec2 PS_TEXCOORD;

void main()
{
	gl_Position = vec4( POSITION.xyz, 1.0 );
	PS_TEXCOORD.xy = TEXCOORD0.xy;
}
)GLSL";

inline constexpr const char* kDstStockCombineCcFs = R"GLSL(
#if defined( GL_ES )
precision highp float;
#endif

uniform vec3 CC_LERP_PARAMS;
#define CC_LERP0			CC_LERP_PARAMS.x
#define CC_LERP1			CC_LERP_PARAMS.y
#define CC_LERP2			CC_LERP_PARAMS.z

uniform vec2 CC_LAYER_PARAMS;
#define CC_LAYER_PARAMS0			CC_LAYER_PARAMS.x
#define CC_LAYER_PARAMS1			CC_LAYER_PARAMS.y

uniform float INTENSITY_MODIFIER;

uniform sampler2D SAMPLER[6];

#define CC_SRC0		SAMPLER[0]
#define CC_DEST0	SAMPLER[1]
#define CC_SRC1		SAMPLER[2]
#define CC_DEST1	SAMPLER[3]
#define CC_SRC2		SAMPLER[4]
#define CC_DEST2	SAMPLER[5]

varying vec2 PS_TEXCOORD;

#define CUBE_DIMENSION 32.0
#define CUBE_WIDTH ( CUBE_DIMENSION * CUBE_DIMENSION )
#define CUBE_HEIGHT ( CUBE_DIMENSION )

vec3 SampleSourceDest( sampler2D source_cc, sampler2D dest_cc, vec2 uv, float lerp )
{
	vec3 cc_src = texture2D( source_cc, uv ).rgb;
	vec3 cc_dest = texture2D( dest_cc, uv ).rgb;
	vec3 cc_mixed = mix( cc_src, cc_dest, lerp );
	return cc_mixed;
}

void main()
{
	vec2 uv = PS_TEXCOORD.xy;

	vec3 cc0 = SampleSourceDest(CC_SRC0, CC_DEST0, uv, CC_LERP0);
	vec3 cc1 = SampleSourceDest(CC_SRC1, CC_DEST1, uv, CC_LERP1);
	vec3 cc2 = SampleSourceDest(CC_SRC2, CC_DEST2, uv, CC_LERP2);

	vec3 cc_mixed = cc0;
	if(CC_LAYER_PARAMS0 > 0.0)
	{
		cc_mixed = mix(cc_mixed, cc1, CC_LAYER_PARAMS0);
	}
	if(CC_LAYER_PARAMS1 > 0.0)
	{
		cc_mixed = mix(cc_mixed, cc2, CC_LAYER_PARAMS1);
	}

	cc_mixed *= INTENSITY_MODIFIER;

    gl_FragColor = vec4( cc_mixed, 1.0 );
}
)GLSL";

inline constexpr const char* kDstStockUiCcVs = R"GLSL(
uniform mat4 MatrixPVW;

attribute vec3 POSITION;
attribute vec2 TEXCOORD0;
attribute vec4 DIFFUSE;

varying vec2 PS_TEXCOORD;
varying vec4 PS_COLOUR;

void main()
{
	gl_Position = MatrixPVW * vec4( POSITION.xyz, 1.0 );
	PS_TEXCOORD.xy = TEXCOORD0.xy;
	PS_COLOUR.rgba = vec4( DIFFUSE.rgb * DIFFUSE.a, DIFFUSE.a ); // premultiply the alpha
}
)GLSL";

inline constexpr const char* kDstStockUiCcFs = R"GLSL(
#if defined( GL_ES )
precision mediump float;
#endif

uniform sampler2D SAMPLER[2];
varying vec2 PS_TEXCOORD;
varying vec4 PS_COLOUR;

uniform vec4 IMAGE_PARAMS;

#define COLOUR_CUBE SAMPLER[1]
#ifndef COLOURCUBE_H
#define COLOURCUBE_H

#ifndef COLOUR_CUBE
	#error If you use colourcube, you must #define the sampler that the colourcube belongs to
#endif

const float CUBE_DIMENSION = 32.0;
const float CUBE_WIDTH = ( CUBE_DIMENSION * CUBE_DIMENSION );
const float CUBE_HEIGHT =( CUBE_DIMENSION );
const float ONE_OVER_CUBE_WIDTH =  1.0 / CUBE_WIDTH;
const float ONE_OVER_CUBE_HEIGHT =  1.0 / CUBE_HEIGHT;

//make sure to premultiply the alpha if its value isn't 1!
vec3 ApplyColourCube(vec3 colour)
{
	vec3 intermediate = colour.rgb * vec3( CUBE_DIMENSION - 1.0, CUBE_DIMENSION - 1.0, CUBE_DIMENSION - 1.0 );

	vec2 floor_uv = vec2( ( min( intermediate.r + 0.5, 31.0 ) + floor( intermediate.b ) * CUBE_DIMENSION ) * ONE_OVER_CUBE_WIDTH,1.0 - ( min( intermediate.g + 0.5, 31.0 ) * ONE_OVER_CUBE_HEIGHT ) );
	vec2 ceil_uv = vec2( ( min( intermediate.r + 0.5, 31.0 ) + ceil( intermediate.b ) * CUBE_DIMENSION ) * ONE_OVER_CUBE_WIDTH,1.0 - ( min( intermediate.g + 0.5, 31.0 ) * ONE_OVER_CUBE_HEIGHT ) );
	vec3 floor_col = texture2D( COLOUR_CUBE, floor_uv.xy ).rgb;
	vec3 ceil_col = texture2D( COLOUR_CUBE, ceil_uv.xy ).rgb;
	return mix(floor_col, ceil_col, intermediate.b - floor(intermediate.b) );
}

#endif //COLOURCUBE.h


void main()
{
    vec4 colour = texture2D( SAMPLER[0], PS_TEXCOORD.xy );
	colour.rgba *= PS_COLOUR.rgba;
	colour.rgba *= IMAGE_PARAMS.rgba;

    gl_FragColor = vec4(ApplyColourCube(colour.rgb) * colour.a, colour.a);
}
)GLSL";

namespace detail {

inline bool CompileCanaryProgram(const CanaryGlOps& gl,
                                 const std::string& vsSrcIn,
                                 const std::string& fsSrcIn,
                                 int compatVer,
                                 bool useTexelFetch,
                                 bool bindUiDiffuse,
                                 uint32_t& outProg,
                                 std::string& outStage,
                                 std::string& outErrLog) {
    outProg = 0u;
    std::string vsSrc = vsSrcIn;
    std::string fsSrc = fsSrcIn;
    if (useTexelFetch) {
        if (!PromoteGlslVersionCompat(vsSrc, compatVer) ||
            !PromoteGlslVersionCompat(fsSrc, compatVer)) {
            outStage = "texel_fetch_version";
            outErrLog = "illegal_glsl_version";
            return false;
        }
        uint32_t taps = 0u;
        if (!TransformColourCubeTexelFetchShaderSource(fsSrc, taps) || taps == 0u) {
            outStage = "texel_fetch_transform";
            outErrLog = "no_cc_taps_replaced";
            return false;
        }
    } else {
        // Clean baseline ES 1.00 path (matching desktop OpenGL Compat without CC transform)
        size_t s = 0u, e = 0u;
        int v = 0;
        if (!FindGlslVersionLineRange(vsSrc, s, e, v)) vsSrc.insert(0u, "#version 100\n");
        if (!FindGlslVersionLineRange(fsSrc, s, e, v)) fsSrc.insert(0u, "#version 100\n");
    }

    auto compOne = [&](uint32_t type, const std::string& src, const char* label) -> uint32_t {
        const uint32_t sh = gl.glCreateShader(type);
        if (!sh) return 0u;
        const char* ptr = src.c_str();
        const int32_t len = static_cast<int32_t>(src.size());
        gl.glShaderSource(sh, 1, &ptr, &len);
        gl.glCompileShader(sh);
        int32_t ok = 0;
        gl.glGetShaderiv(sh, kGlCompileStatus, &ok);
        if (!ok) {
            char buf[512]{};
            int32_t ilen = 0;
            if (gl.glGetShaderInfoLog) {
                gl.glGetShaderInfoLog(sh, static_cast<int32_t>(sizeof(buf) - 1u), &ilen, buf);
            }
            outStage = useTexelFetch ? "texel_fetch_compile" : "vanilla_cc_compile";
            outErrLog = std::string(label) + ": " + buf;
            gl.glDeleteShader(sh);
            return 0u;
        }
        return sh;
    };

    const uint32_t vs = compOne(kGlVertexShader, vsSrc, "VS");
    if (!vs) return false;
    const uint32_t fs = compOne(kGlFragmentShader, fsSrc, "FS");
    if (!fs) {
        gl.glDeleteShader(vs);
        return false;
    }

    const uint32_t prog = gl.glCreateProgram();
    if (!prog) {
        gl.glDeleteShader(vs);
        gl.glDeleteShader(fs);
        outStage = useTexelFetch ? "texel_fetch_link" : "vanilla_cc_link";
        outErrLog = "glCreateProgram_failed";
        return false;
    }
    gl.glAttachShader(prog, vs);
    gl.glAttachShader(prog, fs);
    gl.glBindAttribLocation(prog, 0u, "POSITION");
    gl.glBindAttribLocation(prog, 1u, "TEXCOORD0");
    if (bindUiDiffuse) {
        gl.glBindAttribLocation(prog, 2u, "DIFFUSE");
    }
    gl.glLinkProgram(prog);
    gl.glDeleteShader(vs);
    gl.glDeleteShader(fs);

    int32_t linkOk = 0;
    gl.glGetProgramiv(prog, kGlLinkStatus, &linkOk);
    if (!linkOk) {
        char buf[512]{};
        int32_t ilen = 0;
        if (gl.glGetProgramInfoLog) {
            gl.glGetProgramInfoLog(prog, static_cast<int32_t>(sizeof(buf) - 1u), &ilen, buf);
        }
        outStage = useTexelFetch ? "texel_fetch_link" : "vanilla_cc_link";
        outErrLog = buf;
        gl.glDeleteProgram(prog);
        return false;
    }
    outProg = prog;
    return true;
}

inline bool RunSingleCcModeCanary(const CanaryGlOps& gl,
                                  int compatVer,
                                  bool useTexelFetch,
                                  bool normalizeRgbToRgba8,
                                  bool hasAniso,
                                  CanaryReport& report) {
    // 1. Build identity & non-identity 1024x32 RGB24 LUTs + 16x16 adversarial scene
    std::vector<uint8_t> idRgb24, styleRgb24, sceneRgba;
    BuildIdentityColourCubeRgb24(idRgb24);
    BuildStylizedNonIdentityColourCubeRgb24(styleRgb24);
    BuildAdversarialSceneRgba16x16(sceneRgba);

    std::vector<uint8_t> blendLutRef(1024u * 32u * 3u);
    for (size_t i = 0u; i < blendLutRef.size(); ++i) {
        const float mixed = 0.5f * static_cast<float>(idRgb24[i]) + 0.5f * static_cast<float>(styleRgb24[i]);
        blendLutRef[i] = static_cast<uint8_t>(std::lround(mixed));
    }

    uint32_t progPp = 0u, progComb = 0u, progUi = 0u;
    uint32_t fbo16 = 0u, fboLut = 0u;
    uint32_t texScene = 0u, texOut16 = 0u, texIdLut = 0u, texStyleLut = 0u, texCombLut = 0u;
    uint32_t vbo = 0u;

    auto cleanupObjs = [&]() {
        gl.glUseProgram(0u);
        gl.glBindFramebuffer(kGlFramebuffer, 0u);
        gl.glDisableVertexAttribArray(0u);
        gl.glDisableVertexAttribArray(1u);
        gl.glDisableVertexAttribArray(2u);
        gl.glBindBuffer(kGlArrayBuffer, 0u);
        if (vbo) gl.glDeleteBuffers(1, &vbo);
        const uint32_t texs[5] = { texScene, texOut16, texIdLut, texStyleLut, texCombLut };
        for (uint32_t t : texs) {
            if (t) gl.glDeleteTextures(1, &t);
        }
        if (fbo16) gl.glDeleteFramebuffers(1, &fbo16);
        if (fboLut) gl.glDeleteFramebuffers(1, &fboLut);
        if (progPp) gl.glDeleteProgram(progPp);
        if (progComb) gl.glDeleteProgram(progComb);
        if (progUi) gl.glDeleteProgram(progUi);
    };

    std::string errLog;
    if (!CompileCanaryProgram(gl, kDstStockPostProcessCcVs, kDstStockPostProcessCcFs,
                              compatVer, useTexelFetch, false, progPp, report.stage, errLog) ||
        !CompileCanaryProgram(gl, kDstStockCombineCcVs, kDstStockCombineCcFs,
                              compatVer, useTexelFetch, false, progComb, report.stage, errLog) ||
        !CompileCanaryProgram(gl, kDstStockUiCcVs, kDstStockUiCcFs,
                              compatVer, useTexelFetch, true, progUi, report.stage, errLog)) {
        report.detail = errLog;
        cleanupObjs();
        return false;
    }

    gl.glGenFramebuffers(1, &fbo16);
    gl.glGenFramebuffers(1, &fboLut);
    uint32_t genTex[5]{};
    gl.glGenTextures(5, genTex);
    texScene = genTex[0]; texOut16 = genTex[1]; texIdLut = genTex[2]; texStyleLut = genTex[3]; texCombLut = genTex[4];
    gl.glGenBuffers(1, &vbo);

    if (!fbo16 || !fboLut || !texScene || !texOut16 || !texIdLut || !texStyleLut || !texCombLut || !vbo) {
        report.stage = "gl_alloc_failed";
        cleanupObjs();
        return false;
    }

    auto setupTexParams = [&](uint32_t tex, uint32_t filter, bool applyHostileAf) {
        gl.glActiveTexture(kGlTexture0);
        gl.glBindTexture(kGlTexture2D, tex);
        gl.glTexParameteri(kGlTexture2D, kGlTextureMinFilter, static_cast<int32_t>(filter));
        gl.glTexParameteri(kGlTexture2D, kGlTextureMagFilter, static_cast<int32_t>(filter));
        gl.glTexParameteri(kGlTexture2D, kGlTextureWrapS, static_cast<int32_t>(kGlClampToEdge));
        gl.glTexParameteri(kGlTexture2D, kGlTextureWrapT, static_cast<int32_t>(kGlClampToEdge));
        if (applyHostileAf && hasAniso && gl.glTexParameterf) {
            float hwMax = 1.0f;
            if (gl.glGetFloatv) gl.glGetFloatv(kGlMaxTexMaxAnisotropyExt, &hwMax);
            if (hwMax > 1.0f && hwMax <= 64.0f) {
                gl.glTexParameterf(kGlTexture2D, kGlTextureMaxAnisotropyExt, hwMax);
            }
        }
    };

    gl.glPixelStorei(kGlUnpackAlignment, 4);
    gl.glPixelStorei(kGlPackAlignment, 4);
    gl.glBindBuffer(kGlPixelUnpackBuffer, 0u);

    setupTexParams(texScene, kGlNearest, false);
    gl.glTexImage2D(kGlTexture2D, 0, static_cast<int32_t>(kGlRgba8), 16, 16, 0, kGlRgba, kGlUnsignedByte, sceneRgba.data());

    setupTexParams(texOut16, kGlNearest, false);
    gl.glTexImage2D(kGlTexture2D, 0, static_cast<int32_t>(kGlRgba8), 16, 16, 0, kGlRgba, kGlUnsignedByte, nullptr);

    setupTexParams(texCombLut, kGlLinear, useTexelFetch);
    gl.glTexImage2D(kGlTexture2D, 0, static_cast<int32_t>(kGlRgba8), 1024, 32, 0, kGlRgba, kGlUnsignedByte, nullptr);

    auto uploadLut = [&](uint32_t tex, const std::vector<uint8_t>& rgb24) -> bool {
        setupTexParams(tex, kGlLinear, useTexelFetch);
        if (normalizeRgbToRgba8) {
            std::vector<uint8_t> rgba;
            if (!ExpandRgb24ToRgba32(rgb24.data(), 1024, 32, 4, rgba)) return false;
            gl.glTexImage2D(kGlTexture2D, 0, static_cast<int32_t>(kGlRgba8), 1024, 32, 0, kGlRgba, kGlUnsignedByte, rgba.data());
        } else {
            // Test exact vanilla GL_RGB upload path (testing Layer 2 disable independence)
            gl.glPixelStorei(kGlUnpackAlignment, 4);
            gl.glTexImage2D(kGlTexture2D, 0, static_cast<int32_t>(kGlRgb), 1024, 32, 0, kGlRgb, kGlUnsignedByte, rgb24.data());
        }
        return true;
    };

    if (!uploadLut(texIdLut, idRgb24) || !uploadLut(texStyleLut, styleRgb24)) {
        report.stage = "lut_upload_failed";
        cleanupObjs();
        return false;
    }

    gl.glBindFramebuffer(kGlFramebuffer, fbo16);
    gl.glFramebufferTexture2D(kGlFramebuffer, kGlColorAttachment0, kGlTexture2D, texOut16, 0);
    if (gl.glCheckFramebufferStatus(kGlFramebuffer) != kGlFramebufferComplete) {
        report.stage = "fbo_incomplete";
        report.detail = "fbo16";
        cleanupObjs();
        return false;
    }
    gl.glBindFramebuffer(kGlFramebuffer, fboLut);
    gl.glFramebufferTexture2D(kGlFramebuffer, kGlColorAttachment0, kGlTexture2D, texCombLut, 0);
    if (gl.glCheckFramebufferStatus(kGlFramebuffer) != kGlFramebufferComplete) {
        report.stage = "fbo_incomplete";
        report.detail = "fboLut";
        cleanupObjs();
        return false;
    }

    // Quad vertices: { pos.xyz, uv.xy, rgba(0.5 premul) } stride = 9 floats = 36 bytes
    static constexpr float kQuadVerts[4 * 9] = {
        -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,  1.0f, 1.0f, 1.0f, 0.5f,
         1.0f, -1.0f, 0.0f,  1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 0.5f,
        -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,  1.0f, 1.0f, 1.0f, 0.5f,
         1.0f,  1.0f, 0.0f,  1.0f, 1.0f,  1.0f, 1.0f, 1.0f, 0.5f,
    };
    gl.glBindBuffer(kGlArrayBuffer, vbo);
    gl.glBufferData(kGlArrayBuffer, sizeof(kQuadVerts), kQuadVerts, kGlStaticDraw);
    gl.glEnableVertexAttribArray(0u);
    gl.glEnableVertexAttribArray(1u);
    gl.glEnableVertexAttribArray(2u);
    gl.glVertexAttribPointer(0u, 3, kGlFloat, 0u, 36, reinterpret_cast<const void*>(0));
    gl.glVertexAttribPointer(1u, 2, kGlFloat, 0u, 36, reinterpret_cast<const void*>(12));
    gl.glVertexAttribPointer(2u, 4, kGlFloat, 0u, 36, reinterpret_cast<const void*>(20));

    gl.glDisable(kGlScissorTest);
    gl.glDisable(kGlDepthTest);
    gl.glDisable(kGlStencilTest);
    gl.glDisable(kGlCullFace);
    gl.glDisable(kGlBlend);

    auto compare16x16 = [&](const char* stageName,
                            const std::vector<uint8_t>& got,
                            const std::vector<uint8_t>& expected) -> bool {
        int worstErr = 0;
        int wx = 0, wy = 0;
        char wCh = 'R';
        uint8_t wExp[4]{}, wGot[4]{};
        static constexpr char kChNames[4] = { 'R', 'G', 'B', 'A' };
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                const size_t idx = (static_cast<size_t>(y) * 16u + static_cast<size_t>(x)) * 4u;
                for (int c = 0; c < 4; ++c) {
                    const int d = std::abs(static_cast<int>(got[idx + c]) - static_cast<int>(expected[idx + c]));
                    if (d > worstErr) {
                        worstErr = d;
                        wx = x;
                        wy = y;
                        wCh = kChNames[c];
                        std::memcpy(wExp, &expected[idx], 4u);
                        std::memcpy(wGot, &got[idx], 4u);
                    }
                }
            }
        }
        if (worstErr > report.maxErr) {
            report.maxErr = worstErr;
            report.worstX = wx;
            report.worstY = wy;
            report.worstChannel = wCh;
            std::memcpy(report.expRgba, wExp, 4u);
            std::memcpy(report.gotRgba, wGot, 4u);
        }
        const CanaryVerdict v = ClassifyCanaryError(worstErr);
        if (v == CanaryVerdict::Fail) {
            report.stage = stageName;
            report.maxErr = worstErr;
            report.worstX = wx;
            report.worstY = wy;
            report.worstChannel = wCh;
            std::memcpy(report.expRgba, wExp, 4u);
            std::memcpy(report.gotRgba, wGot, 4u);
            report.verdict = CanaryVerdict::Fail;
            return false;
        }
        if (v == CanaryVerdict::PassWithWarn && report.verdict == CanaryVerdict::Pass) {
            report.verdict = CanaryVerdict::PassWithWarn;
        }
        return true;
    };

    auto evalSceneRef = [&](const uint8_t* lutRgb24, std::vector<uint8_t>& outExpected) {
        outExpected.resize(16u * 16u * 4u);
        for (int i = 0; i < 256; ++i) {
            const size_t idx = static_cast<size_t>(i) * 4u;
            const float r = static_cast<float>(sceneRgba[idx + 0u]) / 255.0f;
            const float g = static_cast<float>(sceneRgba[idx + 1u]) / 255.0f;
            const float b = static_cast<float>(sceneRgba[idx + 2u]) / 255.0f;
            float res[3]{};
            EvalReferenceApplyColourCubeRgb(lutRgb24, r, g, b, res);
            outExpected[idx + 0u] = static_cast<uint8_t>(std::lround(res[0] * 255.0f));
            outExpected[idx + 1u] = static_cast<uint8_t>(std::lround(res[1] * 255.0f));
            outExpected[idx + 2u] = static_cast<uint8_t>(std::lround(res[2] * 255.0f));
            outExpected[idx + 3u] = 255u;
        }
    };

    auto renderPp = [&](uint32_t lutTex, std::vector<uint8_t>& outGot) {
        gl.glBindFramebuffer(kGlFramebuffer, fbo16);
        gl.glViewport(0, 0, 16, 16);
        gl.glUseProgram(progPp);
        gl.glActiveTexture(kGlTexture0);
        gl.glBindTexture(kGlTexture2D, texScene);
        gl.glActiveTexture(kGlTexture0 + 1u);
        gl.glBindTexture(kGlTexture2D, lutTex);
        gl.glUniform1i(gl.glGetUniformLocation(progPp, "SAMPLER[0]"), 0);
        gl.glUniform1i(gl.glGetUniformLocation(progPp, "SAMPLER[1]"), 1);
        gl.glDrawArrays(kGlTriangleStrip, 0, 4);
        gl.glFinish();
        outGot.resize(16u * 16u * 4u);
        gl.glReadPixels(0, 0, 16, 16, kGlRgba, kGlUnsignedByte, outGot.data());
    };

    report.verdict = CanaryVerdict::Pass;
    report.maxErr = 0;

    // Pass 1: Identity LUT + adversarial scene
    std::vector<uint8_t> got, exp;
    renderPp(texIdLut, got);
    evalSceneRef(idRgb24.data(), exp);
    if (!compare16x16("identity_lut_readback", got, exp)) {
        cleanupObjs();
        return false;
    }

    // Pass 2: Non-Identity nonlinear LUT + adversarial scene
    renderPp(texStyleLut, got);
    evalSceneRef(styleRgb24.data(), exp);
    if (!compare16x16("non_identity_lut_readback", got, exp)) {
        cleanupObjs();
        return false;
    }

    // Pass 3: CombineColourCubes FBO chain (0.5 * Identity + 0.5 * Style -> FBO -> PostProcess)
    {
        gl.glBindFramebuffer(kGlFramebuffer, fboLut);
        gl.glViewport(0, 0, 1024, 32);
        gl.glUseProgram(progComb);
        for (uint32_t s = 0u; s < 6u; ++s) {
            gl.glActiveTexture(kGlTexture0 + s);
            gl.glBindTexture(kGlTexture2D, (s & 1u) ? texStyleLut : texIdLut);
            char nm[16]{};
            std::snprintf(nm, sizeof(nm), "SAMPLER[%u]", s);
            gl.glUniform1i(gl.glGetUniformLocation(progComb, nm), static_cast<int32_t>(s));
        }
        const float lerps[3] = { 0.5f, 0.0f, 0.0f };
        const float layers[2] = { 0.0f, 0.0f };
        gl.glUniform3fv(gl.glGetUniformLocation(progComb, "CC_LERP_PARAMS"), 1, lerps);
        gl.glUniform2fv(gl.glGetUniformLocation(progComb, "CC_LAYER_PARAMS"), 1, layers);
        gl.glUniform1f(gl.glGetUniformLocation(progComb, "INTENSITY_MODIFIER"), 1.0f);
        gl.glDrawArrays(kGlTriangleStrip, 0, 4);
        gl.glFinish();

        renderPp(texCombLut, got);
        evalSceneRef(blendLutRef.data(), exp);
        if (!compare16x16("combine_fbo_chain_readback", got, exp)) {
            cleanupObjs();
            return false;
        }
    }

    // Pass 4: ui_cc premultiplied alpha composite onto cleared semi-transparent FBO (tests RGB + Alpha!)
    {
        gl.glBindFramebuffer(kGlFramebuffer, fbo16);
        gl.glViewport(0, 0, 16, 16);
        gl.glClearColor(30.0f / 255.0f, 60.0f / 255.0f, 120.0f / 255.0f, 0.4f);
        gl.glClear(kGlColorBufferBit);
        gl.glEnable(kGlBlend);
        gl.glBlendFunc(kGlOne, kGlOneMinusSrcAlpha);
        gl.glUseProgram(progUi);
        static constexpr float kIdentMat[16] = {
            1,0,0,0,  0,1,0,0,  0,0,1,0,  0,0,0,1
        };
        static constexpr float kImgParams[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        gl.glUniformMatrix4fv(gl.glGetUniformLocation(progUi, "MatrixPVW"), 1, 0u, kIdentMat);
        gl.glUniform4fv(gl.glGetUniformLocation(progUi, "IMAGE_PARAMS"), 1, kImgParams);
        gl.glActiveTexture(kGlTexture0);
        gl.glBindTexture(kGlTexture2D, texScene);
        gl.glActiveTexture(kGlTexture0 + 1u);
        gl.glBindTexture(kGlTexture2D, texStyleLut);
        gl.glUniform1i(gl.glGetUniformLocation(progUi, "SAMPLER[0]"), 0);
        gl.glUniform1i(gl.glGetUniformLocation(progUi, "SAMPLER[1]"), 1);
        gl.glDrawArrays(kGlTriangleStrip, 0, 4);
        gl.glDisable(kGlBlend);
        gl.glFinish();
        gl.glReadPixels(0, 0, 16, 16, kGlRgba, kGlUnsignedByte, got.data());

        // Reference premul UI_CC: src = tex * 0.5, ApplyColourCube(src.rgb) * 0.5 + dst * (1 - 0.5)
        exp.resize(16u * 16u * 4u);
        const float bg[4] = { 30.0f / 255.0f, 60.0f / 255.0f, 120.0f / 255.0f, 0.4f };
        for (int i = 0; i < 256; ++i) {
            const size_t idx = static_cast<size_t>(i) * 4u;
            const float sr = (static_cast<float>(sceneRgba[idx + 0u]) / 255.0f) * 0.5f;
            const float sg = (static_cast<float>(sceneRgba[idx + 1u]) / 255.0f) * 0.5f;
            const float sb = (static_cast<float>(sceneRgba[idx + 2u]) / 255.0f) * 0.5f;
            float cc[3]{};
            EvalReferenceApplyColourCubeRgb(styleRgb24.data(), sr, sg, sb, cc);
            for (int c = 0; c < 3; ++c) {
                const float outC = cc[c] * 0.5f + bg[c] * 0.5f;
                exp[idx + c] = static_cast<uint8_t>(std::lround(std::clamp(outC, 0.0f, 1.0f) * 255.0f));
            }
            const float outA = 0.5f + bg[3] * 0.5f;
            exp[idx + 3u] = static_cast<uint8_t>(std::lround(std::clamp(outA, 0.0f, 1.0f) * 255.0f));
        }
        if (!compare16x16("ui_cc_alpha_blend_readback", got, exp)) {
            cleanupObjs();
            return false;
        }
    }

    const uint32_t gErr = gl.glGetError();
    if (gErr != 0u) {
        report.stage = "gl_error";
        report.glError = gErr;
        report.verdict = CanaryVerdict::Fail;
        cleanupObjs();
        return false;
    }

    cleanupObjs();
    report.stage = "all_passed";
    report.glError = 0u;
    return true;
}

} // namespace detail

inline bool RunLiveColourCubeCanary(const CanaryGlOps& gl,
                                    int glMajor,
                                    int glMinor,
                                    bool hasAniso,
                                    const CompatConfig& cfg,
                                    const std::string& vendor,
                                    const std::string& renderer,
                                    const std::string& version,
                                    const PixelFormatInfo& pfd,
                                    bool preserveHostState,
                                    CanaryReport& outReport) {
    outReport = CanaryReport{};
    outReport.vendor = vendor;
    outReport.renderer = renderer;
    outReport.version = version;
    outReport.pfd = pfd;

    if (!cfg.enableGlCanaryProbe) {
        outReport.ran = false;
        outReport.passed = true;
        outReport.verdict = CanaryVerdict::Pass;
        const int tv = SelectLegalCompatGlslVersion(glMajor, glMinor, 100);
        outReport.texelFetchReady = cfg.enableCcTexelFetch && (tv >= 130);
        outReport.stage = "disabled_by_config";
        outReport.decision = "disabled_by_config";
        return true;
    }

    if (!gl.IsComplete()) {
        outReport.ran = false;
        outReport.passed = false;
        outReport.verdict = CanaryVerdict::Fail;
        outReport.stage = "missing_gl_procs";
        outReport.decision = "fallback_d3d9";
        outReport.fallbackReason = "missing_canary_gl_procs";
        return false;
    }

    LARGE_INTEGER q0{}, q1{}, qFreq{};
    QueryPerformanceFrequency(&qFreq);
    QueryPerformanceCounter(&q0);

    // Save all touched host context state cleanly when probing on real game context
    struct SavedState {
        int32_t curProg = 0, drawFbo = 0, readFbo = 0, arrBuf = 0, unpackBuf = 0;
        int32_t activeTex = 0x84C0, tex[6]{}, unpackAlign = 4, packAlign = 4;
        int32_t vp[4]{};
        float   clearCol[4]{};
        uint8_t blend = 0, scissor = 0, depth = 0, stencil = 0, cull = 0;
        int32_t blendSrcRgb = 1, blendDstRgb = 0;
    } saved{};
    if (preserveHostState) {
        gl.glGetIntegerv(kGlCurrentProgram, &saved.curProg);
        gl.glGetIntegerv(kGlDrawFramebufferBind, &saved.drawFbo);
        gl.glGetIntegerv(kGlReadFramebufferBind, &saved.readFbo);
        gl.glGetIntegerv(kGlArrayBufferBind, &saved.arrBuf);
        gl.glGetIntegerv(kGlPixelUnpackBufferBind, &saved.unpackBuf);
        gl.glGetIntegerv(kGlActiveTexture, &saved.activeTex);
        for (uint32_t u = 0u; u < 6u; ++u) {
            gl.glActiveTexture(kGlTexture0 + u);
            gl.glGetIntegerv(kGlTextureBinding2D, &saved.tex[u]);
        }
        gl.glGetIntegerv(kGlUnpackAlignment, &saved.unpackAlign);
        gl.glGetIntegerv(kGlPackAlignment, &saved.packAlign);
        gl.glGetIntegerv(kGlViewport, saved.vp);
        if (gl.glGetFloatv) {
            gl.glGetFloatv(0x0C22u /*GL_COLOR_CLEAR_VALUE*/, saved.clearCol);
        }
        saved.blend   = gl.glIsEnabled(kGlBlend);
        saved.scissor = gl.glIsEnabled(kGlScissorTest);
        saved.depth   = gl.glIsEnabled(kGlDepthTest);
        saved.stencil = gl.glIsEnabled(kGlStencilTest);
        saved.cull    = gl.glIsEnabled(kGlCullFace);
        gl.glGetIntegerv(kGlBlendSrcRgb, &saved.blendSrcRgb);
        gl.glGetIntegerv(kGlBlendDstRgb, &saved.blendDstRgb);
    }

    while (gl.glGetError() != 0u) {}
    outReport.ran = true;

    const int targetVer = SelectLegalCompatGlslVersion(glMajor, glMinor, 100);
    bool ok = false;
    if (cfg.enableCcTexelFetch && targetVer >= 130) {
        ok = detail::RunSingleCcModeCanary(
            gl, targetVer, true, cfg.enableCcRgbNormalize, hasAniso, outReport);
        if (ok) {
            outReport.passed = true;
            outReport.texelFetchReady = true;
            outReport.decision = "native_gl_texelfetch";
        } else {
            // Fallback probe: test plain ES100 ColourCube on this driver/PFD (without texelFetch)
            CanaryReport fb = outReport;
            fb.maxErr = -1;
            fb.detail.clear();
            if (detail::RunSingleCcModeCanary(
                    gl, targetVer, false, cfg.enableCcRgbNormalize, hasAniso, fb)) {
                outReport = fb;
                outReport.ran = true;
                outReport.passed = true;
                outReport.texelFetchReady = false;
                outReport.decision = "native_gl_vanilla_cc";
                outReport.fallbackReason = "texelfetch_canary_failed_vanilla_passed";
                ok = true;
            } else {
                outReport.passed = false;
                outReport.texelFetchReady = false;
                outReport.decision = "fallback_d3d9";
                outReport.fallbackReason = "canary_failed:" + outReport.stage;
            }
        }
    } else {
        ok = detail::RunSingleCcModeCanary(
            gl, targetVer, false, cfg.enableCcRgbNormalize, hasAniso, outReport);
        if (ok) {
            outReport.passed = true;
            outReport.texelFetchReady = false;
            outReport.decision = "native_gl_vanilla_cc";
        } else {
            outReport.passed = false;
            outReport.texelFetchReady = false;
            outReport.decision = "fallback_d3d9";
            outReport.fallbackReason = "canary_failed:" + outReport.stage;
        }
    }

    if (preserveHostState) {
        gl.glBindBuffer(kGlPixelUnpackBuffer, static_cast<uint32_t>(saved.unpackBuf));
        gl.glBindBuffer(kGlArrayBuffer, static_cast<uint32_t>(saved.arrBuf));
        gl.glPixelStorei(kGlUnpackAlignment, saved.unpackAlign);
        gl.glPixelStorei(kGlPackAlignment, saved.packAlign);
        for (uint32_t u = 0u; u < 6u; ++u) {
            gl.glActiveTexture(kGlTexture0 + u);
            gl.glBindTexture(kGlTexture2D, static_cast<uint32_t>(saved.tex[u]));
        }
        gl.glActiveTexture(static_cast<uint32_t>(saved.activeTex));
        gl.glBindFramebuffer(0x8CA9u, static_cast<uint32_t>(saved.drawFbo));
        gl.glBindFramebuffer(0x8CA8u, static_cast<uint32_t>(saved.readFbo));
        gl.glUseProgram(static_cast<uint32_t>(saved.curProg));
        gl.glViewport(saved.vp[0], saved.vp[1], saved.vp[2], saved.vp[3]);
        gl.glBlendFunc(static_cast<uint32_t>(saved.blendSrcRgb), static_cast<uint32_t>(saved.blendDstRgb));
        if (saved.blend) gl.glEnable(kGlBlend); else gl.glDisable(kGlBlend);
        if (saved.scissor) gl.glEnable(kGlScissorTest); else gl.glDisable(kGlScissorTest);
        if (saved.depth) gl.glEnable(kGlDepthTest); else gl.glDisable(kGlDepthTest);
        if (saved.stencil) gl.glEnable(kGlStencilTest); else gl.glDisable(kGlStencilTest);
        if (saved.cull) gl.glEnable(kGlCullFace); else gl.glDisable(kGlCullFace);
        gl.glClearColor(saved.clearCol[0], saved.clearCol[1], saved.clearCol[2], saved.clearCol[3]);
        while (gl.glGetError() != 0u) {}
    }

    QueryPerformanceCounter(&q1);
    if (qFreq.QuadPart > 0) {
        outReport.elapsedMs =
            static_cast<double>(q1.QuadPart - q0.QuadPart) * 1000.0 / static_cast<double>(qFreq.QuadPart);
    }
    return ok;
}

} // namespace hs_gpu_compat
