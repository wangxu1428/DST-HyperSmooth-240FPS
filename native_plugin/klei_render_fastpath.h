#pragma once
// =============================================================================
// DST HyperSmooth v4.0 Phase 4 — Klei Renderer CPU Fast-Path Engine
// -----------------------------------------------------------------------------
// Eliminates the two largest single-threaded Klei WorldRender CPU bottlenecks
// exposed by Round 3 mega-base profiling (15,268 sprites / 884,178 verts / frame)
// and verified via Capstone disassembly of dontstarve_steam_x64.exe:
//
// 1. HWEffect::ApplyShaderUniforms (RVA 0x385F30..0x3863CE) -> FastApplyShaderUniforms:
//    - Vanilla Klei executes 300,000+ `div r8d` modulo instructions and chained
//      hash-table walks per frame only to skip ~90% of uniforms whose stamp did
//      not change (`bind->lastStamp == top.stamp`).
//    - FastApplyShaderUniforms compiles an immutable flat L1-hot plan per
//      ShaderPart (0 heap allocs, O(1) epoch invalidation, full pointer/layout
//      fingerprint, transactional pre-validation so fallback NEVER leaves partial
//      stamp mutations), reducing unchanged uniform checks to ~6 instructions.
//
// 2. HWRenderer::DrawPrimitives (RVA 0x378C10..0x378CED) -> FastDrawPrimitives
//    & HWRenderer::DrawIndexed (RVA 0x379320..0x37940A) -> FastDrawIndexed:
//    - Vanilla Klei acquires 3 (DrawPrimitives) or 4 (DrawIndexed) Windows
//      CRITICAL_SECTION locks per draw via ResourcePool::Get (91,608 locks/frame),
//      even looking up the exact same Effect handle TWICE per draw!
//    - FastDrawPrimitives / FastDrawIndexed maintain an epoch-invalidated
//      RenderThread handle cache (`DrawPrimCache`), executing ZERO CriticalSection
//      locks on consecutive same-handle draws and eliminating the duplicate Effect
//      lookup even on slow-path misses.
//
// Safety & Parity Invariants:
//   - 100% Fail-Closed: any ABI mismatch at hook-install time, unsupported
//     uniform type (9, 11..14, 16..19, <0, >20), missing constant hash, empty/
//     corrupt constant stack, null dataPtr, count==0, or out-of-range primType
//     falls back cleanly to Klei's original trampoline with ZERO partial side
//     effects.
//   - All emitted GL calls go through injected GlUniformDispatchOps /
//     GlDrawDispatchOps (populated with HookGlUniform* / HookGlBindBuffer /
//     HookGlDrawArrays / HookGlDrawElements) so SpriteBatcher, GlStateCache, and
//     WorldShadowEngine observe 100% identical call streams.
//   - Hermetic C++20: zero STL containers, zero heap allocs on hot path, zero
//     exceptions/RTTI, zero <windows.h> dependency, zero strict-aliasing UB.
// =============================================================================

#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef WINAPI
#define WINAPI __stdcall
#endif

#if defined(__GNUC__) || defined(__clang__)
#define HS_KFP_LIKELY(x)   __builtin_expect(!!(x), 1)
#define HS_KFP_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define HS_KFP_LIKELY(x)   (x)
#define HS_KFP_UNLIKELY(x) (x)
#endif

namespace hs_klei_fastpath {

// -----------------------------------------------------------------------------
// Verified Klei x64 Binary ABI Offsets (dontstarve_steam_x64.exe)
// -----------------------------------------------------------------------------
inline constexpr size_t   kOffShaderPartIdxBegin      = 0x40u;
inline constexpr size_t   kOffShaderPartIdxEnd        = 0x48u;

inline constexpr size_t   kOffUniformTableGroupsBase  = 0x08u;

inline constexpr size_t   kSizeParamGroup             = 0x48u; // 72 bytes
inline constexpr size_t   kOffParamGroupType          = 0x00u; // int32_t (0..20)
inline constexpr size_t   kOffParamGroupArrBegin      = 0x10u; // uint8_t*
inline constexpr size_t   kOffParamGroupArrEnd        = 0x18u; // uint8_t*
inline constexpr size_t   kOffParamGroupBindBegin     = 0x30u; // uint8_t*
inline constexpr size_t   kOffParamGroupBindEnd       = 0x38u; // uint8_t*

inline constexpr size_t   kSizeUniformBinding         = 0x18u; // 24 bytes
inline constexpr size_t   kOffBindingNameHash         = 0x00u; // uint32_t
inline constexpr size_t   kOffBindingLastStamp        = 0x10u; // uint32_t
inline constexpr size_t   kOffBindingGlLoc            = 0x14u; // int32_t

inline constexpr size_t   kOffConstStoreBuckets       = 0x8010u; // HashNode**
inline constexpr size_t   kOffConstStoreNumBuckets    = 0x8018u; // uint32_t
inline constexpr size_t   kOffConstStoreSlotsBase     = 0x8388u; // uint8_t*

inline constexpr size_t   kOffHashNodeKey             = 0x00u; // uint32_t
inline constexpr size_t   kOffHashNodeSlotIdx         = 0x04u; // uint32_t
inline constexpr size_t   kOffHashNodeNext            = 0x08u; // HashNode*

inline constexpr size_t   kSizeConstantSlot           = 0x90u; // 144 bytes
inline constexpr size_t   kOffSlotStackBase           = 0x08u; // StackEntry[8]
inline constexpr size_t   kOffSlotStackCount          = 0x88u; // uint32_t
inline constexpr size_t   kSizeStackEntry             = 0x10u; // 16 bytes: {u32 stamp, u32 pad, const float* dataPtr}
inline constexpr uint32_t kMaxConstantStackDepth      = 8u;
inline constexpr uint32_t kMaxSafeSlotIndex           = 0x01FFFFFFu; // slotIdx * 144 fits cleanly in uint32_t

inline constexpr size_t   kOffRendererStateBase       = 0x10u; // &hwRenderer->boundVb (r8 to HWEffect::Apply)
inline constexpr size_t   kOffRendererBoundVb         = 0x10u; // int32_t
inline constexpr size_t   kOffRendererWantVb          = 0x14u; // int32_t
inline constexpr size_t   kOffRendererBoundDecl       = 0x18u; // int32_t
inline constexpr size_t   kOffRendererWantDecl        = 0x1Cu; // int32_t
inline constexpr size_t   kOffRendererBoundIbo        = 0x20u; // int32_t (-1 = unbound)
inline constexpr size_t   kOffRendererWantIbo         = 0x24u; // int32_t
inline constexpr size_t   kOffRendererBoundEffect     = 0x28u; // int32_t
inline constexpr size_t   kOffRendererWantEffect      = 0x2Cu; // int32_t
inline constexpr size_t   kOffRendererLastEffectMask  = 0x30u; // uint32_t (compared vs effect->attribMask +0xF0)
inline constexpr size_t   kOffRendererAttribCache     = 0x34u; // void* passed as rdx to VertexDecl::Apply
inline constexpr size_t   kOffRendererConstStore      = 0x188u; // void*
inline constexpr size_t   kOffRendererDeclPool        = 0x1A0u; // void*
inline constexpr size_t   kOffRendererVbPool          = 0x1A8u; // void*
inline constexpr size_t   kOffRendererIboPool         = 0x1B0u; // void*
inline constexpr size_t   kOffRendererEffectPool      = 0x1B8u; // void*
inline constexpr size_t   kVtableSlotFlushRenderState = 2u;     // vtable + 0x10

inline constexpr size_t   kVtableSlotEffectApply      = 2u;     // vtable + 0x10
inline constexpr size_t   kOffEffectGlProgramId       = 0xECu;  // uint32_t
inline constexpr size_t   kOffEffectAttribMask        = 0xF0u;  // uint32_t

inline constexpr size_t   kOffVbGlId                  = 0x10u;  // uint32_t
inline constexpr size_t   kOffIboNumElements          = 0x08u;  // uint32_t
inline constexpr size_t   kOffIboGlId                 = 0x10u;  // uint32_t

inline constexpr int32_t  kMaxPrimTypes               = 7;
inline constexpr uint32_t kDefaultPrimTable[7]        = { 0u, 3u, 2u, 1u, 5u, 6u, 4u };
inline constexpr uint32_t kGlArrayBuffer              = 0x8892u;
inline constexpr uint32_t kGlElementArrayBuffer       = 0x8893u;
inline constexpr uint32_t kGlUnsignedShort            = 0x1403u;
inline constexpr uint32_t kMatrixWConstSlot           = 7u;

// -----------------------------------------------------------------------------
// Verified 1-of-1 Unique AOB Signatures & Zero-RIP-Rel Prologue Steal Lengths
// Verified against dontstarve_steam_x64.exe (.text VA 0x1000, Size 0x4B8DAE)
// -----------------------------------------------------------------------------

// 1. HWEffect::ApplyShaderUniforms @ RVA 0x385F30 (1-of-1 unique, stealLen = 20)
//    Stolen bytes [0..20): mov rax,rsp; mov [rax+18],r8; mov [rax+10],rdx; push rbx; push rdi; sub rsp,0xa8
inline constexpr size_t kStealLenApplyShaderUniforms = 20u;
inline constexpr int16_t kAobApplyShaderUniforms[55] = {
    0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x40, 0x18, 0x48, 0x89, 0x50, 0x10, 0x53, 0x57, 0x48, 0x81, 0xEC,
    0xA8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x59, 0x40, 0x48, 0x8B, 0x79, 0x48, 0x48, 0x89, 0x5C, 0x24,
    0x38, 0x48, 0x89, 0x7C, 0x24, 0x40, 0x48, 0x3B, 0xDF, 0x0F, 0x84,   -1,   -1,   -1,   -1, 0x48,
    0x89, 0x68, 0x20, 0x48, 0x89, 0x70, 0xE8
};

// 2. HWRenderer::DrawPrimitives (5-arg) @ RVA 0x378C10 (1-of-1 unique, stealLen = 20)
//    Stolen bytes [0..20): mov [rsp+8],rbx; mov [rsp+10],rbp; mov [rsp+18],rsi; push rdi; sub rsp,0x20
inline constexpr size_t kStealLenDrawPrimitives = 20u;
inline constexpr int16_t kAobDrawPrimitives[77] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
    0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x01, 0x41, 0x8B, 0xF1, 0x41, 0x8B, 0xE8, 0x48, 0x8B, 0xDA,
    0x48, 0x8B, 0xF9, 0xFF, 0x50, 0x10, 0x4C, 0x8B, 0xC3, 0xBA, 0x07, 0x00, 0x00, 0x00, 0x48, 0x8B,
    0xCF, 0xE8,   -1,   -1,   -1,   -1, 0x48, 0x8B, 0xCF, 0xE8,   -1,   -1,   -1,   -1, 0x8B, 0x57,
    0x2C, 0x48, 0x8B, 0x8F, 0xB8, 0x01, 0x00, 0x00, 0xE8,   -1,   -1,   -1,   -1
};

// 3. HWRenderer::DrawIndexed (3-arg) @ RVA 0x379320 (1-of-1 unique, stealLen = 15)
//    Stolen bytes [0..15): mov [rsp+8],rbx; mov [rsp+10],rsi; push rdi; sub rsp,0x20
inline constexpr size_t kStealLenDrawIndexed = 15u;
inline constexpr int16_t kAobDrawIndexed[83] = {
    0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48,
    0x8B, 0x01, 0x49, 0x63, 0xF0, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0xFF, 0x50, 0x10, 0x4C, 0x8B,
    0xC3, 0xBA, 0x07, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xCF, 0xE8,   -1,   -1,   -1,   -1, 0x48, 0x8B,
    0xCF, 0xE8,   -1,   -1,   -1,   -1, 0x8B, 0x57, 0x24, 0x48, 0x8B, 0x8F, 0xB0, 0x01, 0x00, 0x00,
    0xE8,   -1,   -1,   -1,   -1, 0x8B, 0x4F, 0x24, 0x48, 0x8B, 0xD8, 0x39, 0x4F, 0x20, 0x74, 0x16,
    0x8B, 0x50, 0x10
};

// -----------------------------------------------------------------------------
// Function Pointer & Dispatch Struct Definitions
// -----------------------------------------------------------------------------
using ApplyShaderUniforms_t = void (*)(void* shaderPart, void* effectUniformTable, void* constantStore, void* rendererState);
using DrawPrimitives_t      = void (*)(void* hwRenderer, const void* matW, uint32_t first, uint32_t count, int32_t primType);
using DrawIndexed_t         = void (*)(void* hwRenderer, const void* matW, int32_t primType);

using PushMatrixW_t         = void (*)(void* hwRenderer, uint32_t slot, const void* matW);
using BindVBAndDecl_t       = void* (*)(void* hwRenderer);
using ResourcePoolGet_t     = void* (*)(void* pool, int32_t handle);
using VertexDeclApply_t     = void (*)(void* declPtr, void* attribCache, uint32_t attribMask);
using PopConstant_t         = void (*)(void* hwRenderer, uint32_t slot);
using FlushRenderState_t    = void (*)(void* hwRenderer);
using EffectApply_t         = void (*)(void* effectPtr, void* constantStore, void* rendererState);

struct GlUniformDispatchOps {
    using Uniform1f_t        = void (WINAPI*)(int32_t loc, float v0);
    using Uniform1fv_t       = void (WINAPI*)(int32_t loc, int32_t count, const float* value);
    using Uniform2fv_t       = void (WINAPI*)(int32_t loc, int32_t count, const float* value);
    using Uniform3fv_t       = void (WINAPI*)(int32_t loc, int32_t count, const float* value);
    using Uniform4fv_t       = void (WINAPI*)(int32_t loc, int32_t count, const float* value);
    using UniformMatrix2fv_t = void (WINAPI*)(int32_t loc, int32_t count, uint8_t transpose, const float* value);
    using UniformMatrix3fv_t = void (WINAPI*)(int32_t loc, int32_t count, uint8_t transpose, const float* value);
    using UniformMatrix4fv_t = void (WINAPI*)(int32_t loc, int32_t count, uint8_t transpose, const float* value);

    Uniform1f_t        glUniform1f        = nullptr;
    Uniform1fv_t       glUniform1fv       = nullptr;
    Uniform2fv_t       glUniform2fv       = nullptr;
    Uniform3fv_t       glUniform3fv       = nullptr;
    Uniform4fv_t       glUniform4fv       = nullptr;
    UniformMatrix2fv_t glUniformMatrix2fv = nullptr;
    UniformMatrix3fv_t glUniformMatrix3fv = nullptr;
    UniformMatrix4fv_t glUniformMatrix4fv = nullptr;

    bool IsComplete() const noexcept {
        return glUniform1f && glUniform1fv && glUniform2fv && glUniform3fv &&
               glUniform4fv && glUniformMatrix2fv && glUniformMatrix3fv && glUniformMatrix4fv;
    }
};

struct GlDrawDispatchOps {
    using BindBuffer_t   = void (WINAPI*)(uint32_t target, uint32_t buffer);
    using DrawArrays_t   = void (WINAPI*)(uint32_t mode, int32_t first, int32_t count);
    using DrawElements_t = void (WINAPI*)(uint32_t mode, int32_t count, uint32_t type, const void* indices);

    BindBuffer_t   glBindBuffer   = nullptr;
    DrawArrays_t   glDrawArrays   = nullptr;
    DrawElements_t glDrawElements = nullptr;

    bool IsCompleteForDrawPrimitives() const noexcept {
        return glBindBuffer && glDrawArrays;
    }
    bool IsCompleteForDrawIndexed() const noexcept {
        return glBindBuffer && glDrawElements;
    }
};

struct KleiRendererHelpers {
    PushMatrixW_t     fnPushMatrixW     = nullptr;
    BindVBAndDecl_t   fnBindVBAndDecl   = nullptr;
    ResourcePoolGet_t fnResourcePoolGet = nullptr;
    PopConstant_t     fnPopConstant     = nullptr;
    const uint32_t*   primTable         = nullptr;
    VertexDeclApply_t fnVertexDeclApply = nullptr; // optional VertexDecl::Apply (+0x9B in BindVBAndDecl)

    bool IsComplete() const noexcept {
        return fnPushMatrixW && fnBindVBAndDecl && fnResourcePoolGet && fnPopConstant && primTable;
    }
};

// -----------------------------------------------------------------------------
// Zero-UB Unaligned Memory Access Helpers
// -----------------------------------------------------------------------------
namespace detail {

inline uint32_t ReadU32(const void* p) noexcept {
    uint32_t v = 0u;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline int32_t ReadI32(const void* p) noexcept {
    int32_t v = 0;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline float ReadF32(const void* p) noexcept {
    float v = 0.0f;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline void WriteU32(void* p, uint32_t v) noexcept {
    std::memcpy(p, &v, sizeof(v));
}

inline void WriteI32(void* p, int32_t v) noexcept {
    std::memcpy(p, &v, sizeof(v));
}

template <typename T>
inline T* ReadPtr(const void* p) noexcept {
    T* v = nullptr;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

inline uint32_t HashPtr(const void* p) noexcept {
    uint64_t x = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p));
    x ^= x >> 33u;
    x *= 0xFF51AFD7ED558CCDULL;
    x ^= x >> 33u;
    return static_cast<uint32_t>(x);
}

inline bool BytesMatchAt(const uint8_t* base, size_t maxBytes, size_t off, const uint8_t* expected, size_t len) noexcept {
    if (!base || off > maxBytes || len > maxBytes - off) return false;
    return std::memcmp(base + off, expected, len) == 0;
}

inline bool PtrInRange(const uint8_t* ptr, size_t sizeNeeded, const uint8_t* rangeStart, size_t rangeSize) noexcept {
    if (!ptr || !rangeStart || sizeNeeded > rangeSize) return false;
    const uintptr_t p  = reinterpret_cast<uintptr_t>(ptr);
    const uintptr_t lo = reinterpret_cast<uintptr_t>(rangeStart);
    const uintptr_t hi = lo + rangeSize;
    return (p >= lo) && (p <= hi - sizeNeeded);
}

inline const uint8_t* DecodeRel32Branch(const uint8_t* insn, uint8_t expectedOpcode) noexcept {
    if (!insn || insn[0] != expectedOpcode) return nullptr;
    const int32_t rel = ReadI32(insn + 1u);
    return insn + 5u + static_cast<ptrdiff_t>(rel);
}

inline const uint8_t* DecodeRipLea7(const uint8_t* insn, uint8_t rex, uint8_t modrm) noexcept {
    if (!insn || insn[0] != rex || insn[1] != 0x8Du || insn[2] != modrm) return nullptr;
    const int32_t disp = ReadI32(insn + 3u);
    return insn + 7u + static_cast<ptrdiff_t>(disp);
}

} // namespace detail

// -----------------------------------------------------------------------------
// Fail-Closed Binary ABI Validators (Run Once at Hook Install Time)
// -----------------------------------------------------------------------------

inline bool VerifyApplyShaderUniformsAbi(const uint8_t* fn, const uint8_t* textStart, size_t textSize) noexcept {
    if (!detail::PtrInRange(fn, 0x370u, textStart, textSize)) return false;
    const size_t maxLen = static_cast<size_t>((textStart + textSize) - fn);

    static constexpr uint8_t kIdxBegin[]     = { 0x48, 0x8B, 0x59, 0x40 };
    static constexpr uint8_t kIdxEnd[]       = { 0x48, 0x8B, 0x79, 0x48 };
    static constexpr uint8_t kGroupsBase[]   = { 0x4C, 0x8B, 0x7A, 0x08 };
    static constexpr uint8_t kBindEnd[]      = { 0x4B, 0x8B, 0x4C, 0xF7, 0x38 };
    static constexpr uint8_t kBindBegin[]    = { 0x4B, 0x2B, 0x4C, 0xF7, 0x30 };
    static constexpr uint8_t kNumBuckets[]   = { 0x44, 0x8B, 0x87, 0x18, 0x80, 0x00, 0x00 };
    static constexpr uint8_t kBuckets[]      = { 0x4C, 0x8B, 0x8F, 0x10, 0x80, 0x00, 0x00 };
    static constexpr uint8_t kSlotsBase[]    = { 0x48, 0x03, 0x8F, 0x88, 0x83, 0x00, 0x00 };
    static constexpr uint8_t kStackCnt[]     = { 0x39, 0xB1, 0x88, 0x00, 0x00, 0x00 };
    static constexpr uint8_t kLastStamp[]    = { 0x43, 0x39, 0x44, 0x2C, 0x10 };
    static constexpr uint8_t kMaxType[]      = { 0x83, 0xF8, 0x14 };
    static constexpr uint8_t kGlLoc[]        = { 0x43, 0x8B, 0x4C, 0x2C, 0x14 };
    static constexpr uint8_t kMat2Cnt[]      = { 0xBA, 0x01, 0x00, 0x00, 0x00 };
    static constexpr uint8_t kMat2Transp[]   = { 0x41, 0xB0, 0x01 };
    static constexpr uint8_t kMat3Cnt[]      = { 0xBA, 0x01, 0x00, 0x00, 0x00 };
    static constexpr uint8_t kMat3Transp[]   = { 0x41, 0xB0, 0x01 };
    static constexpr uint8_t kMat4Transp[]   = { 0x45, 0x33, 0xC0 };
    static constexpr uint8_t kMat4Cnt[]      = { 0x41, 0x8D, 0x50, 0x01 };

    return detail::BytesMatchAt(fn, maxLen, 0x014u, kIdxBegin,   sizeof(kIdxBegin))   &&
           detail::BytesMatchAt(fn, maxLen, 0x018u, kIdxEnd,     sizeof(kIdxEnd))     &&
           detail::BytesMatchAt(fn, maxLen, 0x051u, kGroupsBase, sizeof(kGroupsBase)) &&
           detail::BytesMatchAt(fn, maxLen, 0x067u, kBindEnd,    sizeof(kBindEnd))    &&
           detail::BytesMatchAt(fn, maxLen, 0x06Cu, kBindBegin,  sizeof(kBindBegin))  &&
           detail::BytesMatchAt(fn, maxLen, 0x0A5u, kNumBuckets, sizeof(kNumBuckets)) &&
           detail::BytesMatchAt(fn, maxLen, 0x0ACu, kBuckets,    sizeof(kBuckets))    &&
           detail::BytesMatchAt(fn, maxLen, 0x127u, kSlotsBase,  sizeof(kSlotsBase))  &&
           detail::BytesMatchAt(fn, maxLen, 0x12Eu, kStackCnt,   sizeof(kStackCnt))   &&
           detail::BytesMatchAt(fn, maxLen, 0x1C3u, kLastStamp,  sizeof(kLastStamp))  &&
           detail::BytesMatchAt(fn, maxLen, 0x1DBu, kMaxType,    sizeof(kMaxType))    &&
           detail::BytesMatchAt(fn, maxLen, 0x204u, kGlLoc,      sizeof(kGlLoc))      &&
           detail::BytesMatchAt(fn, maxLen, 0x32Fu, kMat2Cnt,    sizeof(kMat2Cnt))    &&
           detail::BytesMatchAt(fn, maxLen, 0x334u, kMat2Transp, sizeof(kMat2Transp)) &&
           detail::BytesMatchAt(fn, maxLen, 0x34Au, kMat3Cnt,    sizeof(kMat3Cnt))    &&
           detail::BytesMatchAt(fn, maxLen, 0x34Fu, kMat3Transp, sizeof(kMat3Transp)) &&
           detail::BytesMatchAt(fn, maxLen, 0x365u, kMat4Transp, sizeof(kMat4Transp)) &&
           detail::BytesMatchAt(fn, maxLen, 0x368u, kMat4Cnt,    sizeof(kMat4Cnt));
}

inline bool VerifyBindVBAndDeclAbi(const uint8_t* fn, const uint8_t* textStart, size_t textSize) noexcept {
    if (!detail::PtrInRange(fn, 0x60u, textStart, textSize)) return false;
    const size_t maxLen = static_cast<size_t>((textStart + textSize) - fn);

    static constexpr uint8_t kWantVb[]       = { 0x8B, 0x51, 0x14 };
    static constexpr uint8_t kVbPool[]       = { 0x48, 0x8B, 0x89, 0xA8, 0x01, 0x00, 0x00 };
    static constexpr uint8_t kWantEff[]      = { 0x8B, 0x53, 0x2C };
    static constexpr uint8_t kEffPool[]      = { 0x48, 0x8B, 0x8B, 0xB8, 0x01, 0x00, 0x00 };
    static constexpr uint8_t kWantDecl[]     = { 0x8B, 0x4B, 0x1C };
    static constexpr uint8_t kBoundVb[]      = { 0x39, 0x43, 0x10 };
    static constexpr uint8_t kBoundDecl[]    = { 0x39, 0x4B, 0x18 };
    static constexpr uint8_t kEffMask[]      = { 0x8B, 0x8E, 0xF0, 0x00, 0x00, 0x00 };
    static constexpr uint8_t kLastEffMask[]  = { 0x39, 0x4B, 0x30 };

    return detail::BytesMatchAt(fn, maxLen, 0x00Fu, kWantVb,      sizeof(kWantVb))      &&
           detail::BytesMatchAt(fn, maxLen, 0x015u, kVbPool,      sizeof(kVbPool))      &&
           detail::BytesMatchAt(fn, maxLen, 0x021u, kWantEff,     sizeof(kWantEff))     &&
           detail::BytesMatchAt(fn, maxLen, 0x024u, kEffPool,     sizeof(kEffPool))     &&
           detail::BytesMatchAt(fn, maxLen, 0x033u, kWantDecl,    sizeof(kWantDecl))    &&
           detail::BytesMatchAt(fn, maxLen, 0x03Cu, kBoundVb,     sizeof(kBoundVb))     &&
           detail::BytesMatchAt(fn, maxLen, 0x042u, kBoundDecl,   sizeof(kBoundDecl))   &&
           detail::BytesMatchAt(fn, maxLen, 0x04Bu, kEffMask,     sizeof(kEffMask))     &&
           detail::BytesMatchAt(fn, maxLen, 0x051u, kLastEffMask, sizeof(kLastEffMask));
}

inline VertexDeclApply_t TryResolveVertexDeclApply(const uint8_t* pBind, const uint8_t* textStart, size_t textSize) noexcept {
    if (!detail::PtrInRange(pBind, 0xC0u, textStart, textSize)) return nullptr;
    const size_t maxLen = static_cast<size_t>((textStart + textSize) - pBind);

    static constexpr uint8_t kSetBoundVb[]    = { 0x89, 0x43, 0x10 };                         // +0x6D: mov [rbx+0x10], eax
    static constexpr uint8_t kLoadVbGlId[]    = { 0x8B, 0x57, 0x10 };                         // +0x70: mov edx, [rdi+0x10]
    static constexpr uint8_t kArrayBufConst[] = { 0xB9, 0x92, 0x88, 0x00, 0x00 };             // +0x73: mov ecx, GL_ARRAY_BUFFER
    static constexpr uint8_t kLoadEffMask[]   = { 0x44, 0x8B, 0x86, 0xF0, 0x00, 0x00, 0x00 }; // +0x8D: mov r8d, [rsi+0xf0]
    static constexpr uint8_t kLoadAttribC[]   = { 0x48, 0x8D, 0x53, 0x34 };                   // +0x94: lea rdx, [rbx+0x34]
    static constexpr uint8_t kSetBoundDecl[]  = { 0x44, 0x89, 0x5B, 0x18 };                   // +0xA4: mov [rbx+0x18], r11d
    static constexpr uint8_t kSetLastMask[]   = { 0x89, 0x43, 0x30 };                         // +0xB3: mov [rbx+0x30], eax

    if (!detail::BytesMatchAt(pBind, maxLen, 0x6Du, kSetBoundVb,    sizeof(kSetBoundVb))    ||
        !detail::BytesMatchAt(pBind, maxLen, 0x70u, kLoadVbGlId,    sizeof(kLoadVbGlId))    ||
        !detail::BytesMatchAt(pBind, maxLen, 0x73u, kArrayBufConst, sizeof(kArrayBufConst)) ||
        !detail::BytesMatchAt(pBind, maxLen, 0x8Du, kLoadEffMask,   sizeof(kLoadEffMask))   ||
        !detail::BytesMatchAt(pBind, maxLen, 0x94u, kLoadAttribC,   sizeof(kLoadAttribC))   ||
        !detail::BytesMatchAt(pBind, maxLen, 0xA4u, kSetBoundDecl,  sizeof(kSetBoundDecl))  ||
        !detail::BytesMatchAt(pBind, maxLen, 0xB3u, kSetLastMask,   sizeof(kSetLastMask))) {
        return nullptr;
    }

    const uint8_t* pDeclApply = detail::DecodeRel32Branch(pBind + 0x9Bu, 0xE8u);
    if (!detail::PtrInRange(pDeclApply, 16u, textStart, textSize)) return nullptr;
    return reinterpret_cast<VertexDeclApply_t>(const_cast<uint8_t*>(pDeclApply));
}

inline bool VerifyPrimTableValues(const uint8_t* ptr, const uint8_t* imgStart, size_t imgSize) noexcept {
    if (!detail::PtrInRange(ptr, sizeof(kDefaultPrimTable), imgStart, imgSize)) return false;
    for (int32_t i = 0; i < kMaxPrimTypes; ++i) {
        if (detail::ReadU32(ptr + static_cast<size_t>(i) * 4u) != kDefaultPrimTable[i]) return false;
    }
    return true;
}

inline bool ResolveAndVerifyDrawPrimitivesAbi(
    const uint8_t* fn,
    const uint8_t* textStart, size_t textSize,
    const uint8_t* imgStart,  size_t imgSize,
    KleiRendererHelpers& outHelpers) noexcept {
    outHelpers = KleiRendererHelpers{};
    if (!detail::PtrInRange(fn, 0xE0u, textStart, textSize)) return false;
    const size_t maxLen = static_cast<size_t>((textStart + textSize) - fn);

    static constexpr uint8_t kFlush[]      = { 0xFF, 0x50, 0x10 };
    static constexpr uint8_t kSlot7[]      = { 0xBA, 0x07, 0x00, 0x00, 0x00 };
    static constexpr uint8_t kWantEff[]    = { 0x8B, 0x57, 0x2C };
    static constexpr uint8_t kEffPool[]    = { 0x48, 0x8B, 0x8F, 0xB8, 0x01, 0x00, 0x00 };
    static constexpr uint8_t kConstStore[] = { 0x48, 0x8B, 0x97, 0x88, 0x01, 0x00, 0x00 };
    static constexpr uint8_t kStateBase[]  = { 0x4C, 0x8D, 0x47, 0x10 };
    static constexpr uint8_t kEffApply[]   = { 0x41, 0xFF, 0x51, 0x10 };
    static constexpr uint8_t kBoundIbo[]   = { 0x83, 0x7F, 0x20, 0xFF };

    if (!detail::BytesMatchAt(fn, maxLen, 0x023u, kFlush,      sizeof(kFlush))      ||
        !detail::BytesMatchAt(fn, maxLen, 0x029u, kSlot7,      sizeof(kSlot7))      ||
        !detail::BytesMatchAt(fn, maxLen, 0x03Eu, kWantEff,    sizeof(kWantEff))    ||
        !detail::BytesMatchAt(fn, maxLen, 0x041u, kEffPool,    sizeof(kEffPool))    ||
        !detail::BytesMatchAt(fn, maxLen, 0x04Du, kConstStore, sizeof(kConstStore)) ||
        !detail::BytesMatchAt(fn, maxLen, 0x054u, kStateBase,  sizeof(kStateBase))  ||
        !detail::BytesMatchAt(fn, maxLen, 0x05Eu, kEffApply,   sizeof(kEffApply))   ||
        !detail::BytesMatchAt(fn, maxLen, 0x062u, kBoundIbo,   sizeof(kBoundIbo))) {
        return false;
    }

    const uint8_t* pPush   = detail::DecodeRel32Branch(fn + 0x31u, 0xE8u);
    const uint8_t* pBind   = detail::DecodeRel32Branch(fn + 0x39u, 0xE8u);
    const uint8_t* pPool   = detail::DecodeRel32Branch(fn + 0x48u, 0xE8u);
    const uint8_t* pPrim   = detail::DecodeRipLea7(fn + 0xA7u, 0x48u, 0x0Du);
    const uint8_t* pPop    = detail::DecodeRel32Branch(fn + 0xD8u, 0xE9u);

    if (!detail::PtrInRange(pPush, 16u, textStart, textSize) ||
        !detail::PtrInRange(pBind, 0x60u, textStart, textSize) ||
        !detail::PtrInRange(pPool, 16u, textStart, textSize) ||
        !detail::PtrInRange(pPop,  16u, textStart, textSize)) {
        return false;
    }
    if (!VerifyBindVBAndDeclAbi(pBind, textStart, textSize)) return false;
    if (!VerifyPrimTableValues(pPrim, imgStart, imgSize)) return false;

    outHelpers.fnPushMatrixW     = reinterpret_cast<PushMatrixW_t>(const_cast<uint8_t*>(pPush));
    outHelpers.fnBindVBAndDecl   = reinterpret_cast<BindVBAndDecl_t>(const_cast<uint8_t*>(pBind));
    outHelpers.fnResourcePoolGet = reinterpret_cast<ResourcePoolGet_t>(const_cast<uint8_t*>(pPool));
    outHelpers.fnPopConstant     = reinterpret_cast<PopConstant_t>(const_cast<uint8_t*>(pPop));
    outHelpers.primTable         = reinterpret_cast<const uint32_t*>(pPrim);
    outHelpers.fnVertexDeclApply = TryResolveVertexDeclApply(pBind, textStart, textSize);
    return outHelpers.IsComplete();
}

inline bool ResolveAndVerifyDrawIndexedAbi(
    const uint8_t* fn,
    const uint8_t* textStart, size_t textSize,
    const uint8_t* imgStart,  size_t imgSize,
    KleiRendererHelpers& outHelpers) noexcept {
    outHelpers = KleiRendererHelpers{};
    if (!detail::PtrInRange(fn, 0xECu, textStart, textSize)) return false;
    const size_t maxLen = static_cast<size_t>((textStart + textSize) - fn);

    static constexpr uint8_t kFlush[]      = { 0xFF, 0x50, 0x10 };
    static constexpr uint8_t kSlot7[]      = { 0xBA, 0x07, 0x00, 0x00, 0x00 };
    static constexpr uint8_t kWantIbo[]    = { 0x8B, 0x57, 0x24 };
    static constexpr uint8_t kIboPool[]    = { 0x48, 0x8B, 0x8F, 0xB0, 0x01, 0x00, 0x00 };
    static constexpr uint8_t kBoundIbo[]   = { 0x39, 0x4F, 0x20 };
    static constexpr uint8_t kIboGlId[]    = { 0x8B, 0x50, 0x10 };
    static constexpr uint8_t kWantEff[]    = { 0x8B, 0x57, 0x2C };
    static constexpr uint8_t kEffPool[]    = { 0x48, 0x8B, 0x8F, 0xB8, 0x01, 0x00, 0x00 };
    static constexpr uint8_t kConstStore[] = { 0x48, 0x8B, 0x97, 0x88, 0x01, 0x00, 0x00 };
    static constexpr uint8_t kEffApply[]   = { 0x41, 0xFF, 0x51, 0x10 };
    static constexpr uint8_t kIboNumElem[] = { 0x83, 0x7B, 0x08, 0x00 };
    static constexpr uint8_t kUShort[]     = { 0x41, 0xB8, 0x03, 0x14, 0x00, 0x00 };

    if (!detail::BytesMatchAt(fn, maxLen, 0x01Bu, kFlush,      sizeof(kFlush))      ||
        !detail::BytesMatchAt(fn, maxLen, 0x021u, kSlot7,      sizeof(kSlot7))      ||
        !detail::BytesMatchAt(fn, maxLen, 0x036u, kWantIbo,    sizeof(kWantIbo))    ||
        !detail::BytesMatchAt(fn, maxLen, 0x039u, kIboPool,    sizeof(kIboPool))    ||
        !detail::BytesMatchAt(fn, maxLen, 0x04Bu, kBoundIbo,   sizeof(kBoundIbo))   ||
        !detail::BytesMatchAt(fn, maxLen, 0x050u, kIboGlId,    sizeof(kIboGlId))    ||
        !detail::BytesMatchAt(fn, maxLen, 0x066u, kWantEff,    sizeof(kWantEff))    ||
        !detail::BytesMatchAt(fn, maxLen, 0x069u, kEffPool,    sizeof(kEffPool))    ||
        !detail::BytesMatchAt(fn, maxLen, 0x075u, kConstStore, sizeof(kConstStore)) ||
        !detail::BytesMatchAt(fn, maxLen, 0x086u, kEffApply,   sizeof(kEffApply))   ||
        !detail::BytesMatchAt(fn, maxLen, 0x08Au, kIboNumElem, sizeof(kIboNumElem)) ||
        !detail::BytesMatchAt(fn, maxLen, 0x0C2u, kUShort,     sizeof(kUShort))) {
        return false;
    }

    const uint8_t* pPush  = detail::DecodeRel32Branch(fn + 0x29u, 0xE8u);
    const uint8_t* pBind  = detail::DecodeRel32Branch(fn + 0x31u, 0xE8u);
    const uint8_t* pPool1 = detail::DecodeRel32Branch(fn + 0x40u, 0xE8u);
    const uint8_t* pPool2 = detail::DecodeRel32Branch(fn + 0x70u, 0xE8u);
    const uint8_t* pPrim  = detail::DecodeRipLea7(fn + 0xB5u, 0x48u, 0x0Du);
    const uint8_t* pPop   = detail::DecodeRel32Branch(fn + 0xE5u, 0xE9u);

    if (!pPool1 || pPool1 != pPool2) return false;
    if (!detail::PtrInRange(pPush,  16u,   textStart, textSize) ||
        !detail::PtrInRange(pBind,  0x60u, textStart, textSize) ||
        !detail::PtrInRange(pPool1, 16u,   textStart, textSize) ||
        !detail::PtrInRange(pPop,   16u,   textStart, textSize)) {
        return false;
    }
    if (!VerifyBindVBAndDeclAbi(pBind, textStart, textSize)) return false;
    if (!VerifyPrimTableValues(pPrim, imgStart, imgSize)) return false;

    outHelpers.fnPushMatrixW     = reinterpret_cast<PushMatrixW_t>(const_cast<uint8_t*>(pPush));
    outHelpers.fnBindVBAndDecl   = reinterpret_cast<BindVBAndDecl_t>(const_cast<uint8_t*>(pBind));
    outHelpers.fnResourcePoolGet = reinterpret_cast<ResourcePoolGet_t>(const_cast<uint8_t*>(pPool1));
    outHelpers.fnPopConstant     = reinterpret_cast<PopConstant_t>(const_cast<uint8_t*>(pPop));
    outHelpers.primTable         = reinterpret_cast<const uint32_t*>(pPrim);
    outHelpers.fnVertexDeclApply = TryResolveVertexDeclApply(pBind, textStart, textSize);
    return outHelpers.IsComplete();
}

// -----------------------------------------------------------------------------
// Compiled Uniform Plan Data Structures (Zero Heap Alloc, L1-Friendly)
// -----------------------------------------------------------------------------
enum UniformDispatchKind : uint8_t {
    kDispFloat1 = 0u, // types 0, 1  -> glUniform1f(loc, data[0])
    kDispVec1   = 1u, // type  5     -> glUniform1fv(loc, 1, data)
    kDispVec2   = 2u, // types 2, 6  -> glUniform2fv(loc, elemCount, data)
    kDispVec3   = 3u, // types 3, 7  -> glUniform3fv(loc, elemCount, data)
    kDispVec4   = 4u, // types 4, 8  -> glUniform4fv(loc, elemCount, data)
    kDispMat2T  = 5u, // type  10    -> glUniformMatrix2fv(loc, 1, GL_TRUE,  data)
    kDispMat3T  = 6u, // type  15    -> glUniformMatrix3fv(loc, 1, GL_TRUE,  data)
    kDispMat4   = 7u, // type  20    -> glUniformMatrix4fv(loc, 1, GL_FALSE, data)
};

enum PlanStatus : uint8_t {
    kPlanEmpty       = 0u,
    kPlanValid       = 1u,
    kPlanUnsupported = 2u,
};

struct CompiledUniformEntry {
    uint8_t* lastStampAddr = nullptr; // &UniformBinding::lastStamp (+0x10)
    uint32_t slotByteOff   = 0u;      // slotIdx * 144 (pre-multiplied)
    int32_t  glLoc         = -1;      // UniformBinding::glLocation (+0x14)
    int32_t  elemCount     = 1;       // exact count passed to glUniform*fv
    uint8_t  dispatchKind  = 0u;      // UniformDispatchKind (0..7)
    uint8_t  pad[3]        = {};
};
static_assert(sizeof(CompiledUniformEntry) == 24u, "CompiledUniformEntry must remain compact (24B)");

struct UniformPlanSlot {
    const void*    shaderPart         = nullptr;
    const void*    idxBegin           = nullptr;
    const void*    idxEnd             = nullptr;
    const void*    effectUniformTable = nullptr;
    const uint8_t* groupsBase         = nullptr;
    const void*    constantStore      = nullptr;
    const void*    buckets            = nullptr;
    const uint8_t* slotsBase          = nullptr;
    uint32_t       numBuckets         = 0u;
    uint32_t       planGen            = 0u;
    uint16_t       entryOffset        = 0u;
    uint16_t       entryCount         = 0u;
    uint8_t        status             = kPlanEmpty;
    uint8_t        pad[3]             = {};
};

struct DrawPrimCache {
    static constexpr uint32_t kHandleCacheSlots = 64u;
    static constexpr uint32_t kHandleCacheMask  = kHandleCacheSlots - 1u;

    struct EffectEntry {
        void*    pool       = nullptr;
        void*    ptr        = nullptr;
        int32_t  handle     = -1;
        uint32_t progId     = 0u;
        uint32_t attribMask = 0u;
        uint32_t gen        = 0u;
    };
    struct HandleEntry {
        void*    pool   = nullptr;
        void*    ptr    = nullptr;
        int32_t  handle = -1;
        uint32_t gen    = 0u;
    };

    uint64_t    cachedEpoch      = 0u;
    void*       cachedHwRenderer = nullptr;
    uint32_t    cacheGen         = 1u;
    EffectEntry effectTab[kHandleCacheSlots]{};
    HandleEntry iboTab[kHandleCacheSlots]{};
    HandleEntry vbTab[kHandleCacheSlots]{};
    HandleEntry declTab[kHandleCacheSlots]{};

    void BumpGen() noexcept {
        ++cacheGen;
        if (HS_KFP_UNLIKELY(cacheGen == 0u)) {
            for (uint32_t i = 0u; i < kHandleCacheSlots; ++i) {
                effectTab[i].gen = 0u;
                iboTab[i].gen    = 0u;
                vbTab[i].gen     = 0u;
                declTab[i].gen   = 0u;
            }
            cacheGen = 1u;
        }
    }

    void Invalidate() noexcept {
        cachedEpoch      = 0u;
        cachedHwRenderer = nullptr;
        BumpGen();
    }
};

struct FastPathStats {
    uint64_t uniformPlanHits      = 0u;
    uint64_t uniformPlanCompiles  = 0u;
    uint64_t uniformSkipped       = 0u;
    uint64_t uniformEmitted       = 0u;
    uint64_t uniformFallbacks     = 0u;

    uint64_t drawFastHits         = 0u;
    uint64_t drawSlowMisses       = 0u;
    uint64_t drawFallbacks        = 0u;

    uint64_t drawIndexedFastHits  = 0u;
    uint64_t drawIndexedSlowMisses = 0u;
    uint64_t drawIndexedFallbacks = 0u;

    uint64_t locksSaved           = 0u;
};

// -----------------------------------------------------------------------------
// KleiRenderFastPath — Render-Thread Engine Instance
// -----------------------------------------------------------------------------
class KleiRenderFastPath {
public:
    static constexpr uint32_t kPlanSlotCount     = 128u;
    static constexpr uint32_t kPlanSlotMask      = kPlanSlotCount - 1u;
    static constexpr uint32_t kMaxProbe          = 16u;
    static constexpr uint32_t kMaxEntriesPerPlan = 48u;
    static constexpr uint32_t kEntryPoolCap      = 2048u;
    static constexpr uint32_t kMaxHashChainWalk  = 256u;

    KleiRenderFastPath() noexcept = default;
    ~KleiRenderFastPath() noexcept = default;
    KleiRenderFastPath(const KleiRenderFastPath&) = delete;
    KleiRenderFastPath& operator=(const KleiRenderFastPath&) = delete;

    void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool IsEnabled() const noexcept { return enabled_; }

    void SetUniformOps(const GlUniformDispatchOps& ops) noexcept { uniformOps_ = ops; }
    void SetDrawOps(const GlDrawDispatchOps& ops) noexcept { drawOps_ = ops; }
    void SetHelpers(const KleiRendererHelpers& helpers) noexcept { helpers_ = helpers; }

    void InvalidateAll() noexcept {
        InvalidateUniformPlans();
        drawCache_.Invalidate();
    }

    void ResetStats() noexcept { stats_ = FastPathStats{}; }
    const FastPathStats& GetStats() const noexcept { return stats_; }

    // Convenience telemetry getters
    uint64_t UniformPlanHits() const noexcept       { return stats_.uniformPlanHits; }
    uint64_t UniformPlanCompiles() const noexcept   { return stats_.uniformPlanCompiles; }
    uint64_t UniformSkipped() const noexcept        { return stats_.uniformSkipped; }
    uint64_t UniformEmitted() const noexcept        { return stats_.uniformEmitted; }
    uint64_t UniformFallbacks() const noexcept      { return stats_.uniformFallbacks; }
    uint64_t DrawFastHits() const noexcept          { return stats_.drawFastHits; }
    uint64_t DrawSlowMisses() const noexcept        { return stats_.drawSlowMisses; }
    uint64_t DrawFallbacks() const noexcept         { return stats_.drawFallbacks; }
    uint64_t DrawIndexedFastHits() const noexcept   { return stats_.drawIndexedFastHits; }
    uint64_t DrawIndexedSlowMisses() const noexcept { return stats_.drawIndexedSlowMisses; }
    uint64_t DrawIndexedFallbacks() const noexcept  { return stats_.drawIndexedFallbacks; }
    uint64_t LocksSaved() const noexcept            { return stats_.locksSaved; }

    // -------------------------------------------------------------------------
    // 1. FastApplyShaderUniforms — Exact Drop-In for HWEffect::ApplyShaderUniforms (0x385F30)
    // -------------------------------------------------------------------------
    void FastApplyShaderUniforms(
        void* shaderPart,
        void* effectUniformTable,
        void* constantStore,
        void* rendererState,
        uint64_t liveEpoch,
        ApplyShaderUniforms_t origFn) noexcept {
        if (HS_KFP_UNLIKELY(!enabled_ || !uniformOps_.IsComplete() ||
                            !shaderPart || !effectUniformTable || !constantStore)) {
            ++stats_.uniformFallbacks;
            if (origFn) origFn(shaderPart, effectUniformTable, constantStore, rendererState);
            return;
        }

        const uint8_t* sp = static_cast<const uint8_t*>(shaderPart);
        const uint8_t* idxBegin = detail::ReadPtr<const uint8_t>(sp + kOffShaderPartIdxBegin);
        const uint8_t* idxEnd   = detail::ReadPtr<const uint8_t>(sp + kOffShaderPartIdxEnd);

        // Exact Klei early-out (0x385F56: cmp rbx, rdi; je exit)
        if (idxBegin == idxEnd) {
            ++stats_.uniformPlanHits;
            return;
        }
        if (HS_KFP_UNLIKELY(!idxBegin || !idxEnd || idxEnd < idxBegin ||
                            ((static_cast<size_t>(idxEnd - idxBegin) & 3u) != 0u))) {
            ++stats_.uniformFallbacks;
            if (origFn) origFn(shaderPart, effectUniformTable, constantStore, rendererState);
            return;
        }

        const uint8_t* ut = static_cast<const uint8_t*>(effectUniformTable);
        const uint8_t* cs = static_cast<const uint8_t*>(constantStore);
        const uint8_t* groupsBase = detail::ReadPtr<const uint8_t>(ut + kOffUniformTableGroupsBase);
        const void*    buckets    = detail::ReadPtr<const void>(cs + kOffConstStoreBuckets);
        const uint32_t numBuckets = detail::ReadU32(cs + kOffConstStoreNumBuckets);
        const uint8_t* slotsBase  = detail::ReadPtr<const uint8_t>(cs + kOffConstStoreSlotsBase);

        if (HS_KFP_UNLIKELY(!groupsBase || !buckets || numBuckets == 0u || !slotsBase)) {
            ++stats_.uniformFallbacks;
            if (origFn) origFn(shaderPart, effectUniformTable, constantStore, rendererState);
            return;
        }

        // O(1) epoch invalidation on shader link/delete, context switch, or frame swap
        if (HS_KFP_UNLIKELY(!planEpochValid_ || planEpoch_ != liveEpoch)) {
            InvalidateUniformPlans();
            planEpoch_      = liveEpoch;
            planEpochValid_ = true;
        }

        const uint32_t baseSlot = detail::HashPtr(shaderPart) & kPlanSlotMask;
        UniformPlanSlot* matchedSlot = nullptr;
        UniformPlanSlot* emptySlot   = nullptr;

        for (uint32_t probe = 0u; probe < kMaxProbe; ++probe) {
            UniformPlanSlot& s = planSlots_[(baseSlot + probe) & kPlanSlotMask];
            if (s.planGen != planGen_) {
                emptySlot = &s;
                break;
            }
            if (s.shaderPart == shaderPart) {
                matchedSlot = &s;
                break;
            }
        }

        const bool fpMatch = matchedSlot != nullptr &&
                             matchedSlot->idxBegin           == idxBegin &&
                             matchedSlot->idxEnd             == idxEnd &&
                             matchedSlot->effectUniformTable == effectUniformTable &&
                             matchedSlot->groupsBase         == groupsBase &&
                             matchedSlot->constantStore      == constantStore &&
                             matchedSlot->buckets            == buckets &&
                             matchedSlot->numBuckets         == numBuckets &&
                             matchedSlot->slotsBase          == slotsBase;

        if (HS_KFP_LIKELY(fpMatch)) {
            if (HS_KFP_UNLIKELY(matchedSlot->status != kPlanValid)) {
                ++stats_.uniformFallbacks;
                if (origFn) origFn(shaderPart, effectUniformTable, constantStore, rendererState);
                return;
            }
            ++stats_.uniformPlanHits;
            ExecuteCompiledPlan(*matchedSlot, slotsBase, shaderPart, effectUniformTable, constantStore, rendererState, origFn);
            return;
        }

        // Compile (or recompile in-place on fingerprint mismatch)
        UniformPlanSlot* targetSlot = matchedSlot ? matchedSlot : (emptySlot ? emptySlot : &planSlots_[baseSlot]);
        UniformPlanSlot* compiledSlot = nullptr;
        const CompileResult cr = CompileUniformPlan(
            targetSlot, compiledSlot, shaderPart, idxBegin, idxEnd, effectUniformTable, groupsBase,
            constantStore, buckets, numBuckets, slotsBase);

        if (HS_KFP_UNLIKELY(cr != CompileResult::kOk || !compiledSlot)) {
            ++stats_.uniformFallbacks;
            if (origFn) origFn(shaderPart, effectUniformTable, constantStore, rendererState);
            return;
        }

        ExecuteCompiledPlan(*compiledSlot, slotsBase, shaderPart, effectUniformTable, constantStore, rendererState, origFn);
    }

    // -------------------------------------------------------------------------
    // 2. FastDrawPrimitives — Exact Drop-In for HWRenderer::DrawPrimitives (0x378C10)
    // -------------------------------------------------------------------------
    void FastDrawPrimitives(
        void* hwRenderer,
        const void* matW,
        uint32_t first,
        uint32_t count,
        int32_t primType,
        uint64_t liveEpoch,
        DrawPrimitives_t origFn) noexcept {
        if (HS_KFP_UNLIKELY(!enabled_ || !helpers_.IsComplete() ||
                            !drawOps_.IsCompleteForDrawPrimitives() ||
                            !hwRenderer || !matW || count == 0u ||
                            primType < 0 || primType >= kMaxPrimTypes)) {
            ++stats_.drawFallbacks;
            if (origFn) origFn(hwRenderer, matW, first, count, primType);
            return;
        }

        uint8_t* const r = static_cast<uint8_t*>(hwRenderer);
        void* const* rVtable = detail::ReadPtr<void* const>(r);
        const FlushRenderState_t fnFlush = rVtable
            ? reinterpret_cast<FlushRenderState_t>(rVtable[kVtableSlotFlushRenderState])
            : nullptr;
        if (HS_KFP_UNLIKELY(!fnFlush)) {
            ++stats_.drawFallbacks;
            if (origFn) origFn(hwRenderer, matW, first, count, primType);
            return;
        }

        // Step 1 & 2: Exact Klei order — FlushRenderState, then PushMatrixW(slot 7)
        fnFlush(hwRenderer);
        helpers_.fnPushMatrixW(hwRenderer, kMatrixWConstSlot, matW);

        // Step 3: Resolve Effect + VB/Decl binding (0 locks on hit!)
        bool wasFastHit = false;
        void* const effectPtr = ResolveEffectAndBindState(r, liveEpoch, wasFastHit);
        if (HS_KFP_UNLIKELY(!effectPtr)) {
            helpers_.fnPopConstant(hwRenderer, kMatrixWConstSlot);
            ++stats_.drawFallbacks;
            return;
        }

        void* const* effVtable = detail::ReadPtr<void* const>(effectPtr);
        const EffectApply_t fnEffectApply = effVtable
            ? reinterpret_cast<EffectApply_t>(effVtable[kVtableSlotEffectApply])
            : nullptr;
        if (HS_KFP_UNLIKELY(!fnEffectApply)) {
            helpers_.fnPopConstant(hwRenderer, kMatrixWConstSlot);
            ++stats_.drawFallbacks;
            return;
        }

        if (wasFastHit) {
            ++stats_.drawFastHits;
            stats_.locksSaved += 3u;
        } else {
            ++stats_.drawSlowMisses;
        }

        // Step 4: HWEffect::Apply(effectPtr, constStore, &hwRenderer->boundVb)
        void* const constStore = detail::ReadPtr<void>(r + kOffRendererConstStore);
        fnEffectApply(effectPtr, constStore, r + kOffRendererStateBase);

        // Step 5: Unbind IBO if boundIbo != -1 (exact match to 0x378C72..0x378C8C)
        if (detail::ReadI32(r + kOffRendererBoundIbo) != -1) {
            drawOps_.glBindBuffer(kGlElementArrayBuffer, 0u);
            detail::WriteI32(r + kOffRendererBoundIbo, -1);
        }

        // Step 6: glDrawArrays(primTable[primType], first, count) via hooked ops
        drawOps_.glDrawArrays(helpers_.primTable[primType],
                              static_cast<int32_t>(first),
                              static_cast<int32_t>(count));

        // Step 7: PopConstant(hwRenderer, 7)
        helpers_.fnPopConstant(hwRenderer, kMatrixWConstSlot);
    }

    // -------------------------------------------------------------------------
    // 3. FastDrawIndexed — Exact Drop-In for HWRenderer::DrawIndexed (0x379320)
    // -------------------------------------------------------------------------
    void FastDrawIndexed(
        void* hwRenderer,
        const void* matW,
        int32_t primType,
        uint64_t liveEpoch,
        DrawIndexed_t origFn) noexcept {
        if (HS_KFP_UNLIKELY(!enabled_ || !helpers_.IsComplete() ||
                            !drawOps_.IsCompleteForDrawIndexed() ||
                            !hwRenderer || !matW ||
                            primType < 0 || primType >= kMaxPrimTypes)) {
            ++stats_.drawIndexedFallbacks;
            if (origFn) origFn(hwRenderer, matW, primType);
            return;
        }

        uint8_t* const r = static_cast<uint8_t*>(hwRenderer);
        void* const* rVtable = detail::ReadPtr<void* const>(r);
        const FlushRenderState_t fnFlush = rVtable
            ? reinterpret_cast<FlushRenderState_t>(rVtable[kVtableSlotFlushRenderState])
            : nullptr;
        if (HS_KFP_UNLIKELY(!fnFlush)) {
            ++stats_.drawIndexedFallbacks;
            if (origFn) origFn(hwRenderer, matW, primType);
            return;
        }

        // Pre-state zero-element check when cached IBO is valid
        {
            const int32_t preWantIbo = detail::ReadI32(r + kOffRendererWantIbo);
            if (preWantIbo != -1 &&
                drawCache_.cachedEpoch == liveEpoch &&
                drawCache_.cachedHwRenderer == hwRenderer) {
                const auto& iboE = drawCache_.iboTab[static_cast<uint32_t>(preWantIbo) & DrawPrimCache::kHandleCacheMask];
                void* const preIboPool = detail::ReadPtr<void>(r + kOffRendererIboPool);
                if (iboE.gen == drawCache_.cacheGen &&
                    iboE.handle == preWantIbo &&
                    iboE.pool == preIboPool &&
                    iboE.ptr != nullptr) {
                    if (HS_KFP_UNLIKELY(detail::ReadU32(static_cast<const uint8_t*>(iboE.ptr) + kOffIboNumElements) == 0u)) {
                        ++stats_.drawIndexedFallbacks;
                        if (origFn) origFn(hwRenderer, matW, primType);
                        return;
                    }
                }
            }
        }

        // Step 1 & 2: Exact Klei order — FlushRenderState, then PushMatrixW(slot 7)
        fnFlush(hwRenderer);
        helpers_.fnPushMatrixW(hwRenderer, kMatrixWConstSlot, matW);

        // Step 3: Resolve Effect + VB/Decl binding
        bool effectFastHit = false;
        void* const effectPtr = ResolveEffectAndBindState(r, liveEpoch, effectFastHit);
        if (HS_KFP_UNLIKELY(!effectPtr)) {
            helpers_.fnPopConstant(hwRenderer, kMatrixWConstSlot);
            ++stats_.drawIndexedFallbacks;
            return;
        }

        // Step 4: Resolve IBO + bind GL_ELEMENT_ARRAY_BUFFER if needed (exact Klei order: BEFORE HWEffect::Apply!)
        const int32_t wantIbo  = detail::ReadI32(r + kOffRendererWantIbo);
        const int32_t boundIbo = detail::ReadI32(r + kOffRendererBoundIbo);
        void* const iboPool    = detail::ReadPtr<void>(r + kOffRendererIboPool);

        bool iboFastHit = false;
        void* iboPtr = nullptr;
        if (wantIbo != -1) {
            auto& iboSlot = drawCache_.iboTab[static_cast<uint32_t>(wantIbo) & DrawPrimCache::kHandleCacheMask];
            if (iboSlot.gen == drawCache_.cacheGen &&
                iboSlot.handle == wantIbo &&
                iboSlot.pool == iboPool &&
                iboSlot.ptr != nullptr) {
                iboPtr = iboSlot.ptr;
                iboFastHit = true;
            } else {
                iboPtr = iboPool ? helpers_.fnResourcePoolGet(iboPool, wantIbo) : nullptr;
                if (HS_KFP_UNLIKELY(!iboPtr)) {
                    helpers_.fnPopConstant(hwRenderer, kMatrixWConstSlot);
                    ++stats_.drawIndexedFallbacks;
                    return;
                }
                iboSlot.pool   = iboPool;
                iboSlot.handle = wantIbo;
                iboSlot.ptr    = iboPtr;
                iboSlot.gen    = drawCache_.cacheGen;
            }
        } else {
            helpers_.fnPopConstant(hwRenderer, kMatrixWConstSlot);
            ++stats_.drawIndexedFallbacks;
            return;
        }

        if (boundIbo != wantIbo) {
            const uint32_t iboGlId = detail::ReadU32(static_cast<const uint8_t*>(iboPtr) + kOffIboGlId);
            drawOps_.glBindBuffer(kGlElementArrayBuffer, iboGlId);
            detail::WriteI32(r + kOffRendererBoundIbo, wantIbo);
        }

        void* const* effVtable = detail::ReadPtr<void* const>(effectPtr);
        const EffectApply_t fnEffectApply = effVtable
            ? reinterpret_cast<EffectApply_t>(effVtable[kVtableSlotEffectApply])
            : nullptr;
        if (HS_KFP_UNLIKELY(!fnEffectApply)) {
            helpers_.fnPopConstant(hwRenderer, kMatrixWConstSlot);
            ++stats_.drawIndexedFallbacks;
            return;
        }

        if (effectFastHit && iboFastHit) {
            ++stats_.drawIndexedFastHits;
            stats_.locksSaved += 4u;
        } else {
            ++stats_.drawIndexedSlowMisses;
            if (effectFastHit) stats_.locksSaved += 3u;
            if (iboFastHit)    stats_.locksSaved += 1u;
        }

        // Step 5: HWEffect::Apply(effectPtr, constStore, &hwRenderer->boundVb)
        void* const constStore = detail::ReadPtr<void>(r + kOffRendererConstStore);
        fnEffectApply(effectPtr, constStore, r + kOffRendererStateBase);

        // Step 6: Validate ib->NumElements() > 0, then glDrawElements(..., GL_UNSIGNED_SHORT, nullptr)
        const uint32_t numElements = detail::ReadU32(static_cast<const uint8_t*>(iboPtr) + kOffIboNumElements);
        if (HS_KFP_UNLIKELY(numElements == 0u)) {
            helpers_.fnPopConstant(hwRenderer, kMatrixWConstSlot);
            ++stats_.drawIndexedFallbacks;
            return;
        }
        drawOps_.glDrawElements(helpers_.primTable[primType],
                                static_cast<int32_t>(numElements),
                                kGlUnsignedShort,
                                nullptr);

        // Step 7: PopConstant(hwRenderer, 7)
        helpers_.fnPopConstant(hwRenderer, kMatrixWConstSlot);
    }

private:
    enum class CompileResult : uint8_t {
        kOk          = 0u,
        kUnsupported = 1u,
        kTransientFail = 2u,
    };

    struct PendingEmit {
        uint32_t     entryIdx;
        uint32_t     stamp;
        const float* dataPtr;
    };

    void InvalidateUniformPlans() noexcept {
        entriesUsed_ = 0u;
        ++planGen_;
        if (HS_KFP_UNLIKELY(planGen_ == 0u)) {
            // Wraparound guard (once per 4 billion epochs)
            for (uint32_t i = 0u; i < kPlanSlotCount; ++i) {
                planSlots_[i].planGen = 0u;
                planSlots_[i].status  = kPlanEmpty;
            }
            planGen_ = 1u;
        }
    }

    static bool MapUniformType(int32_t uniformType, const uint8_t* groupPtr,
                               uint8_t& outKind, int32_t& outElemCount) noexcept {
        switch (uniformType) {
            case 0:
            case 1:
                outKind      = kDispFloat1;
                outElemCount = 1;
                return true;
            case 2:
            case 3:
            case 4: {
                const uint8_t* arrBegin = detail::ReadPtr<const uint8_t>(groupPtr + kOffParamGroupArrBegin);
                const uint8_t* arrEnd   = detail::ReadPtr<const uint8_t>(groupPtr + kOffParamGroupArrEnd);
                if (HS_KFP_UNLIKELY(((arrBegin == nullptr) != (arrEnd == nullptr)) || arrEnd < arrBegin)) {
                    return false;
                }
                const ptrdiff_t diff = arrEnd - arrBegin;
                const int32_t rawCnt = static_cast<int32_t>(diff / static_cast<ptrdiff_t>(kSizeUniformBinding));
                outElemCount = (rawCnt >= 1) ? rawCnt : 1;
                outKind = static_cast<uint8_t>(kDispVec2 + (uniformType - 2));
                return true;
            }
            case 5:
                outKind      = kDispVec1;
                outElemCount = 1;
                return true;
            case 6:
                outKind      = kDispVec2;
                outElemCount = 2;
                return true;
            case 7:
                outKind      = kDispVec3;
                outElemCount = 3;
                return true;
            case 8:
                outKind      = kDispVec4;
                outElemCount = 4;
                return true;
            case 10:
                outKind      = kDispMat2T;
                outElemCount = 1;
                return true;
            case 15:
                outKind      = kDispMat3T;
                outElemCount = 1;
                return true;
            case 20:
                outKind      = kDispMat4;
                outElemCount = 1;
                return true;
            default:
                return false;
        }
    }

    static bool LookupConstantSlot(const void* bucketsVoid, uint32_t numBuckets,
                                   uint32_t nameHash, uint32_t& outSlotIdx) noexcept {
        const uint8_t* const* buckets = static_cast<const uint8_t* const*>(bucketsVoid);
        const uint32_t bucketIdx = nameHash % numBuckets;
        const uint8_t* const endSentinel = buckets[numBuckets];
        const uint8_t* node = buckets[bucketIdx];

        for (uint32_t steps = 0u; node != nullptr && node != endSentinel && steps < kMaxHashChainWalk; ++steps) {
            if (detail::ReadU32(node + kOffHashNodeKey) == nameHash) {
                outSlotIdx = detail::ReadU32(node + kOffHashNodeSlotIdx);
                return true;
            }
            node = detail::ReadPtr<const uint8_t>(node + kOffHashNodeNext);
        }
        return false;
    }

    CompileResult CompileUniformPlan(
        UniformPlanSlot*  targetSlot,
        UniformPlanSlot*& outSlot,
        const void*    shaderPart,
        const uint8_t* idxBegin,
        const uint8_t* idxEnd,
        const void*    effectUniformTable,
        const uint8_t* groupsBase,
        const void*    constantStore,
        const void*    buckets,
        uint32_t       numBuckets,
        const uint8_t* slotsBase) noexcept {
        outSlot = nullptr;
        ++stats_.uniformPlanCompiles;

        CompiledUniformEntry tmp[kMaxEntriesPerPlan];
        uint32_t count = 0u;

        for (const uint8_t* p = idxBegin; p < idxEnd; p += 4u) {
            const uint32_t paramIdx = detail::ReadU32(p);
            const uint8_t* groupPtr = groupsBase + static_cast<size_t>(paramIdx) * kSizeParamGroup;

            uint8_t* bindBegin = detail::ReadPtr<uint8_t>(groupPtr + kOffParamGroupBindBegin);
            uint8_t* bindEnd   = detail::ReadPtr<uint8_t>(groupPtr + kOffParamGroupBindEnd);
            if (bindBegin == bindEnd) continue;
            if (HS_KFP_UNLIKELY(!bindBegin || !bindEnd || bindEnd < bindBegin)) {
                return CompileResult::kTransientFail;
            }
            const size_t bindBytes = static_cast<size_t>(bindEnd - bindBegin);
            if (HS_KFP_UNLIKELY((bindBytes % kSizeUniformBinding) != 0u)) {
                return CompileResult::kTransientFail;
            }
            const size_t bindCount = bindBytes / kSizeUniformBinding;

            const int32_t uniformType = detail::ReadI32(groupPtr + kOffParamGroupType);
            uint8_t dispKind = 0u;
            int32_t elemCount = 1;
            if (HS_KFP_UNLIKELY(!MapUniformType(uniformType, groupPtr, dispKind, elemCount))) {
                // Unsupported uniform type (9, 11..14, 16..19, <0, >20): cache unsupported & fail-closed!
                targetSlot->shaderPart         = shaderPart;
                targetSlot->idxBegin           = idxBegin;
                targetSlot->idxEnd             = idxEnd;
                targetSlot->effectUniformTable = effectUniformTable;
                targetSlot->groupsBase         = groupsBase;
                targetSlot->constantStore      = constantStore;
                targetSlot->buckets            = buckets;
                targetSlot->slotsBase          = slotsBase;
                targetSlot->numBuckets         = numBuckets;
                targetSlot->planGen            = planGen_;
                targetSlot->entryOffset        = 0u;
                targetSlot->entryCount         = 0u;
                targetSlot->status             = kPlanUnsupported;
                return CompileResult::kUnsupported;
            }

            for (size_t bi = 0u; bi < bindCount; ++bi) {
                if (HS_KFP_UNLIKELY(count >= kMaxEntriesPerPlan)) {
                    return CompileResult::kTransientFail;
                }
                uint8_t* bindPtr = bindBegin + bi * kSizeUniformBinding;
                const uint32_t nameHash = detail::ReadU32(bindPtr + kOffBindingNameHash);
                uint32_t slotIdx = 0u;
                if (HS_KFP_UNLIKELY(!LookupConstantSlot(buckets, numBuckets, nameHash, slotIdx))) {
                    // Missing constant hash: do NOT cache as valid; fail-closed immediately to origFn!
                    return CompileResult::kTransientFail;
                }
                if (HS_KFP_UNLIKELY(slotIdx > kMaxSafeSlotIndex)) {
                    return CompileResult::kTransientFail;
                }

                CompiledUniformEntry& e = tmp[count++];
                e.lastStampAddr = bindPtr + kOffBindingLastStamp;
                e.slotByteOff   = slotIdx * static_cast<uint32_t>(kSizeConstantSlot);
                e.glLoc         = detail::ReadI32(bindPtr + kOffBindingGlLoc);
                e.elemCount     = elemCount;
                e.dispatchKind  = dispKind;
            }
        }

        // Ensure entry arena space (O(1) clear on rare overflow -> land directly in home bucket!)
        UniformPlanSlot* dstSlot = targetSlot;
        if (HS_KFP_UNLIKELY(entriesUsed_ + count > kEntryPoolCap)) {
            InvalidateUniformPlans();
            dstSlot = &planSlots_[detail::HashPtr(shaderPart) & kPlanSlotMask];
        }

        const uint16_t entryOffset = static_cast<uint16_t>(entriesUsed_);
        if (count > 0u) {
            std::memcpy(&entryPool_[entriesUsed_], tmp, count * sizeof(CompiledUniformEntry));
            entriesUsed_ += count;
        }

        dstSlot->shaderPart         = shaderPart;
        dstSlot->idxBegin           = idxBegin;
        dstSlot->idxEnd             = idxEnd;
        dstSlot->effectUniformTable = effectUniformTable;
        dstSlot->groupsBase         = groupsBase;
        dstSlot->constantStore      = constantStore;
        dstSlot->buckets            = buckets;
        dstSlot->slotsBase          = slotsBase;
        dstSlot->numBuckets         = numBuckets;
        dstSlot->planGen            = planGen_;
        dstSlot->entryOffset        = entryOffset;
        dstSlot->entryCount         = static_cast<uint16_t>(count);
        dstSlot->status             = kPlanValid;
        outSlot                     = dstSlot;
        return CompileResult::kOk;
    }

    void ExecuteCompiledPlan(
        const UniformPlanSlot& plan,
        const uint8_t* slotsBase,
        void* shaderPart,
        void* effectUniformTable,
        void* constantStore,
        void* rendererState,
        ApplyShaderUniforms_t origFn) noexcept {
        const uint32_t entryCount = plan.entryCount;
        if (entryCount == 0u) return;

        const CompiledUniformEntry* const entries = &entryPool_[plan.entryOffset];

        // Phase 1 (Transactional Pre-Validation + Stamp Filter):
        // Check ALL stack depths & dirty dataPtrs BEFORE mutating any lastStamp or calling GL!
        PendingEmit pending[kMaxEntriesPerPlan];
        uint32_t pendingCount = 0u;
        uint32_t skippedCount = 0u;

        for (uint32_t i = 0u; i < entryCount; ++i) {
            const CompiledUniformEntry& e = entries[i];
            const uint8_t* const slotPtr = slotsBase + e.slotByteOff;
            const uint32_t cnt = detail::ReadU32(slotPtr + kOffSlotStackCount);
            if (HS_KFP_UNLIKELY((cnt - 1u) >= kMaxConstantStackDepth)) {
                ++stats_.uniformFallbacks;
                if (origFn) origFn(shaderPart, effectUniformTable, constantStore, rendererState);
                return;
            }
            const uint8_t* const topPtr = slotPtr + kOffSlotStackBase + (static_cast<size_t>(cnt - 1u) << 4u);
            const uint32_t stamp = detail::ReadU32(topPtr);
            if (HS_KFP_LIKELY(detail::ReadU32(e.lastStampAddr) == stamp)) {
                ++skippedCount;
                continue;
            }
            const float* const dataPtr = detail::ReadPtr<const float>(topPtr + 8u);
            if (HS_KFP_UNLIKELY(dataPtr == nullptr)) {
                ++stats_.uniformFallbacks;
                if (origFn) origFn(shaderPart, effectUniformTable, constantStore, rendererState);
                return;
            }
            pending[pendingCount++] = PendingEmit{ i, stamp, dataPtr };
        }

        stats_.uniformSkipped += skippedCount;
        if (pendingCount == 0u) return;
        stats_.uniformEmitted += pendingCount;

        // Phase 2 (Commit lastStamp & Emit GL Uniforms in Exact Vanilla Order)
        for (uint32_t k = 0u; k < pendingCount; ++k) {
            const PendingEmit& p = pending[k];
            const CompiledUniformEntry& e = entries[p.entryIdx];
            detail::WriteU32(e.lastStampAddr, p.stamp);
            switch (e.dispatchKind) {
                case kDispFloat1:
                    uniformOps_.glUniform1f(e.glLoc, detail::ReadF32(p.dataPtr));
                    break;
                case kDispVec1:
                    uniformOps_.glUniform1fv(e.glLoc, 1, p.dataPtr);
                    break;
                case kDispVec2:
                    uniformOps_.glUniform2fv(e.glLoc, e.elemCount, p.dataPtr);
                    break;
                case kDispVec3:
                    uniformOps_.glUniform3fv(e.glLoc, e.elemCount, p.dataPtr);
                    break;
                case kDispVec4:
                    uniformOps_.glUniform4fv(e.glLoc, e.elemCount, p.dataPtr);
                    break;
                case kDispMat2T:
                    uniformOps_.glUniformMatrix2fv(e.glLoc, 1, 1u /*GL_TRUE*/, p.dataPtr);
                    break;
                case kDispMat3T:
                    uniformOps_.glUniformMatrix3fv(e.glLoc, 1, 1u /*GL_TRUE*/, p.dataPtr);
                    break;
                case kDispMat4:
                    uniformOps_.glUniformMatrix4fv(e.glLoc, 1, 0u /*GL_FALSE*/, p.dataPtr);
                    break;
                default:
                    break;
            }
        }
    }

    // Shared Effect + VB/Decl zero-lock 64-entry cache resolver for FastDrawPrimitives & FastDrawIndexed
    void* ResolveEffectAndBindState(uint8_t* r, uint64_t liveEpoch, bool& outWasFastHit) noexcept {
        outWasFastHit = false;

        const int32_t  boundVb        = detail::ReadI32(r + kOffRendererBoundVb);
        const int32_t  wantVb         = detail::ReadI32(r + kOffRendererWantVb);
        const int32_t  boundDecl      = detail::ReadI32(r + kOffRendererBoundDecl);
        const int32_t  wantDecl       = detail::ReadI32(r + kOffRendererWantDecl);
        const int32_t  wantEffect     = detail::ReadI32(r + kOffRendererWantEffect);
        const uint32_t lastEffectMask = detail::ReadU32(r + kOffRendererLastEffectMask);
        void* const    effectPool     = detail::ReadPtr<void>(r + kOffRendererEffectPool);

        // Epoch or HWRenderer instance change invalidates cached pool pointers immediately
        if (HS_KFP_UNLIKELY(drawCache_.cachedEpoch != liveEpoch ||
                            drawCache_.cachedHwRenderer != static_cast<void*>(r))) {
            drawCache_.cachedEpoch      = liveEpoch;
            drawCache_.cachedHwRenderer = static_cast<void*>(r);
            drawCache_.BumpGen();
        }

        const uint32_t gen = drawCache_.cacheGen;
        DrawPrimCache::EffectEntry* effSlot = (wantEffect != -1)
            ? &drawCache_.effectTab[static_cast<uint32_t>(wantEffect) & DrawPrimCache::kHandleCacheMask]
            : nullptr;

        const bool effectCachedHit =
            effSlot != nullptr &&
            effSlot->gen == gen &&
            effSlot->handle == wantEffect &&
            effSlot->pool == effectPool &&
            effSlot->ptr != nullptr &&
            detail::ReadU32(static_cast<const uint8_t*>(effSlot->ptr) + kOffEffectGlProgramId) == effSlot->progId &&
            detail::ReadU32(static_cast<const uint8_t*>(effSlot->ptr) + kOffEffectAttribMask) == effSlot->attribMask;

        // FAST PATH 1: VB, Decl, Effect, AttribMask, ProgId, and Epoch all match -> 0 CriticalSection locks!
        if (HS_KFP_LIKELY(effectCachedHit &&
                          wantVb != -1 && wantVb == boundVb &&
                          wantDecl != -1 && wantDecl == boundDecl &&
                          lastEffectMask == effSlot->attribMask)) {
            outWasFastHit = true;
            return effSlot->ptr;
        }

        // Lock-free 64-entry VB/Decl/Effect resolution when VertexDecl::Apply (+0x9B in BindVBAndDecl) is verified
        if (helpers_.fnVertexDeclApply != nullptr &&
            drawOps_.glBindBuffer != nullptr &&
            wantEffect != -1 && wantVb != -1 && wantDecl != -1) {
            void* effPtr = nullptr;
            uint32_t effMask = 0u;
            if (effectCachedHit) {
                effPtr  = effSlot->ptr;
                effMask = effSlot->attribMask;
            } else {
                effPtr = effectPool ? helpers_.fnResourcePoolGet(effectPool, wantEffect) : nullptr;
                if (HS_KFP_UNLIKELY(!effPtr)) {
                    return nullptr;
                }
                effMask = detail::ReadU32(static_cast<const uint8_t*>(effPtr) + kOffEffectAttribMask);
                effSlot->pool       = effectPool;
                effSlot->handle     = wantEffect;
                effSlot->ptr        = effPtr;
                effSlot->progId     = detail::ReadU32(static_cast<const uint8_t*>(effPtr) + kOffEffectGlProgramId);
                effSlot->attribMask = effMask;
                effSlot->gen        = gen;
            }

            if (wantVb == boundVb && wantDecl == boundDecl && lastEffectMask == effMask) {
                outWasFastHit = effectCachedHit;
                if (!effectCachedHit) {
                    stats_.locksSaved += 2u; // Skipped no-op BindVBAndDecl (vbPool + effPool)
                }
                return effPtr;
            }

            void* const vbPool   = detail::ReadPtr<void>(r + kOffRendererVbPool);
            void* const declPool = detail::ReadPtr<void>(r + kOffRendererDeclPool);

            auto& vbSlot   = drawCache_.vbTab[static_cast<uint32_t>(wantVb) & DrawPrimCache::kHandleCacheMask];
            auto& declSlot = drawCache_.declTab[static_cast<uint32_t>(wantDecl) & DrawPrimCache::kHandleCacheMask];

            const bool vbHit = (vbSlot.gen == gen && vbSlot.handle == wantVb && vbSlot.pool == vbPool && vbSlot.ptr != nullptr);
            void* vbPtr = vbHit ? vbSlot.ptr : (vbPool ? helpers_.fnResourcePoolGet(vbPool, wantVb) : nullptr);
            if (HS_KFP_UNLIKELY(!vbPtr)) {
                return nullptr;
            }
            if (!vbHit) {
                vbSlot.pool   = vbPool;
                vbSlot.handle = wantVb;
                vbSlot.ptr    = vbPtr;
                vbSlot.gen    = gen;
            }

            const bool declHit = (declSlot.gen == gen && declSlot.handle == wantDecl && declSlot.pool == declPool && declSlot.ptr != nullptr);
            void* declPtr = declHit ? declSlot.ptr : (declPool ? helpers_.fnResourcePoolGet(declPool, wantDecl) : nullptr);
            if (HS_KFP_UNLIKELY(!declPtr)) {
                return nullptr;
            }
            if (!declHit) {
                declSlot.pool   = declPool;
                declSlot.handle = wantDecl;
                declSlot.ptr    = declPtr;
                declSlot.gen    = gen;
            }

            // Exact Klei BindVBAndDecl execution order (EXE+0x378ACD..0x378B16)
            if (wantVb != boundVb) {
                detail::WriteI32(r + kOffRendererBoundVb, wantVb);
                const uint32_t vbGlId = detail::ReadU32(static_cast<const uint8_t*>(vbPtr) + kOffVbGlId);
                drawOps_.glBindBuffer(kGlArrayBuffer, vbGlId);
            }
            helpers_.fnVertexDeclApply(declPtr, r + kOffRendererAttribCache, effMask);
            detail::WriteI32(r + kOffRendererBoundDecl, wantDecl);
            detail::WriteU32(r + kOffRendererLastEffectMask, effMask);

            if (effectCachedHit && vbHit && declHit) {
                outWasFastHit = true; // Zero CriticalSection locks; caller credits +3 locksSaved!
            } else {
                uint64_t saved = effectCachedHit ? 2u : 1u; // Avoided duplicate effPool lock
                if (vbHit)   ++saved;
                if (declHit) ++saved;
                stats_.locksSaved += saved;
            }
            return effPtr;
        }

        // Legacy fallback when fnVertexDeclApply is not present:
        // SLOW PATH A: Effect pointer is still valid in this epoch, only VB/Decl/AttribMask changed.
        if (effectCachedHit) {
            helpers_.fnBindVBAndDecl(static_cast<void*>(r));
            stats_.locksSaved += 1u;
            return effSlot->ptr;
        }

        // SLOW PATH B: Effect handle changed or epoch bumped.
        const bool vbOrDeclDirty = (wantVb == -1 || wantVb != boundVb ||
                                    wantDecl == -1 || wantDecl != boundDecl);
        if (vbOrDeclDirty) {
            helpers_.fnBindVBAndDecl(static_cast<void*>(r));
        }

        void* const effectPtr = (effectPool && wantEffect != -1)
            ? helpers_.fnResourcePoolGet(effectPool, wantEffect)
            : nullptr;
        if (HS_KFP_UNLIKELY(!effectPtr)) {
            return nullptr;
        }

        const uint32_t effProgId = detail::ReadU32(static_cast<const uint8_t*>(effectPtr) + kOffEffectGlProgramId);
        const uint32_t effMask   = detail::ReadU32(static_cast<const uint8_t*>(effectPtr) + kOffEffectAttribMask);

        if (!vbOrDeclDirty) {
            if (lastEffectMask == effMask) {
                stats_.locksSaved += 2u;
            } else {
                helpers_.fnBindVBAndDecl(static_cast<void*>(r));
            }
        }

        if (effSlot != nullptr) {
            effSlot->pool       = effectPool;
            effSlot->handle     = wantEffect;
            effSlot->ptr        = effectPtr;
            effSlot->progId     = effProgId;
            effSlot->attribMask = effMask;
            effSlot->gen        = gen;
        }
        return effectPtr;
    }

    bool                 enabled_        = true;
    GlUniformDispatchOps uniformOps_     = {};
    GlDrawDispatchOps    drawOps_        = {};
    KleiRendererHelpers  helpers_        = {};
    FastPathStats        stats_          = {};
    DrawPrimCache        drawCache_      = {};

    uint64_t             planEpoch_      = 0u;
    bool                 planEpochValid_ = false;
    uint32_t             planGen_        = 1u;
    uint32_t             entriesUsed_    = 0u;
    UniformPlanSlot      planSlots_[kPlanSlotCount] = {};
    CompiledUniformEntry entryPool_[kEntryPoolCap]  = {};
};

} // namespace hs_klei_fastpath
