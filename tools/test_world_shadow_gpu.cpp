#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#include "world_shadow_engine.h"
#include "water_reflection_engine.h"
#include "foliage_field.h"
#include "crash_detective.h"
#include "sprite_batcher.h"
#include "ui_batcher.h"
#include "async_save_commit.h"
#include "dxgi_flip_present.h"
#include "combat_impact.h"
#include "pbo_tex_ring.h"
#include "gpu_clarity_engine.h"

static void TestLog(const char* msg) {
    printf("  [LOG] %s\n", msg);
}

using GlReadPixels_t           = void (WINAPI*)(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void*);
using GlGetIntegerv_t          = void (WINAPI*)(uint32_t, int32_t*);
using GlGetBufferParameteriv_t = void (WINAPI*)(uint32_t, uint32_t, int32_t*);

struct BackendEnv {
    hs_world_shadow::GlShadowOps ops{};
    GlReadPixels_t           glReadPixels           = nullptr;
    GlGetIntegerv_t          glGetIntegerv          = nullptr;
    GlGetBufferParameteriv_t glGetBufferParameteriv = nullptr;
    bool                     isNativeGl             = false;
    const char*              name                   = "";
};

static bool RunPixelExactSuite(const BackendEnv& env) {
    printf("\n=== Pixel-Exact Live GPU Test: %s ===\n", env.name);
    const auto& ops = env.ops;
    auto checkNoErr = [&](const char* stage) -> bool {
        uint32_t e = ops.glGetError ? ops.glGetError() : 0u;
        if (e != 0u) {
            printf("  [FAIL] GL error 0x%X at %s\n", e, stage);
            return false;
        }
        return true;
    };

    // 1. Create mock PrimaryRT (256x256 RGBA8)
    uint32_t primaryFbo = 0, primaryTex = 0;
    ops.glGenFramebuffers(1, &primaryFbo);
    ops.glGenTextures(1, &primaryTex);
    ops.glActiveTexture(hs_world_shadow::kGlTexture0);
    ops.glBindTexture(hs_world_shadow::kGlTexture2D, primaryTex);
    ops.glTexParameteri(hs_world_shadow::kGlTexture2D, hs_world_shadow::kGlTextureMinFilter, hs_world_shadow::kGlLinear);
    ops.glTexParameteri(hs_world_shadow::kGlTexture2D, hs_world_shadow::kGlTextureMagFilter, hs_world_shadow::kGlLinear);
    ops.glTexImage2D(hs_world_shadow::kGlTexture2D, 0, hs_world_shadow::kGlRgba, 256, 256, 0,
                     hs_world_shadow::kGlRgba, hs_world_shadow::kGlUnsignedByte, nullptr);
    ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, primaryFbo);
    ops.glFramebufferTexture2D(hs_world_shadow::kGlFramebuffer, hs_world_shadow::kGlColorAttachment0,
                               hs_world_shadow::kGlTexture2D, primaryTex, 0);
    if (ops.glCheckFramebufferStatus(hs_world_shadow::kGlFramebuffer) != hs_world_shadow::kGlFramebufferComplete) {
        printf("  [FAIL] PrimaryRT FBO incomplete!\n");
        return false;
    }

    // 2. Create solid white 4x4 sprite atlas texture on GL_TEXTURE6 (slot 0)
    uint32_t spriteTex = 0;
    uint8_t white4x4[4 * 4 * 4];
    memset(white4x4, 255, sizeof(white4x4));
    ops.glGenTextures(1, &spriteTex);
    ops.glActiveTexture(hs_world_shadow::kGlTexture0 + 6u);
    ops.glBindTexture(hs_world_shadow::kGlTexture2D, spriteTex);
    ops.glTexParameteri(hs_world_shadow::kGlTexture2D, hs_world_shadow::kGlTextureMinFilter, hs_world_shadow::kGlLinear);
    ops.glTexParameteri(hs_world_shadow::kGlTexture2D, hs_world_shadow::kGlTextureMagFilter, hs_world_shadow::kGlLinear);
    ops.glTexImage2D(hs_world_shadow::kGlTexture2D, 0, hs_world_shadow::kGlRgba, 4, 4, 0,
                     hs_world_shadow::kGlRgba, hs_world_shadow::kGlUnsignedByte, white4x4);
    ops.glActiveTexture(hs_world_shadow::kGlTexture0);

    std::vector<hs_sprite_batch::BatchedVertex> verts;
    std::vector<uint16_t> idxs;
    auto pushQuad = [&](float cx, float cz, float halfW, float hTop, float shadowFlag) {
        uint16_t base = static_cast<uint16_t>(verts.size());
        auto mk = [&](float lx, float wy, float u, float v) {
            hs_sprite_batch::BatchedVertex bv{};
            bv.x = shadowFlag; bv.y = 0.0f; bv.u = u; bv.v = v;
            bv.wx = cx + lx; bv.wy = wy; bv.wz = cz; bv.slot = 0.0f;
            bv.mr = 1.0f; bv.mg = 1.0f; bv.mb = 1.0f; bv.ma = 1.0f;
            bv.ar = 0.0f; bv.ag = 0.0f; bv.ab = 0.0f; bv.pad = lx;
            return bv;
        };
        verts.push_back(mk(-halfW, 0.0f, 0.1f, 0.1f));
        verts.push_back(mk(+halfW, 0.0f, 0.9f, 0.1f));
        verts.push_back(mk(-halfW, hTop, 0.1f, 0.9f));
        verts.push_back(mk(+halfW, hTop, 0.9f, 0.9f));
        idxs.push_back(base + 0); idxs.push_back(base + 1); idxs.push_back(base + 2);
        idxs.push_back(base + 1); idxs.push_back(base + 3); idxs.push_back(base + 2);
    };
    pushQuad(0.0f, 0.0f, 1.5f, 4.0f, 1.0f);   // Sprite A
    pushQuad(-12.0f, 0.0f, 3.0f, 4.0f, 0.0f); // Sprite C (OnGround -> MUST BE CULLED!)

    uint32_t vbo = 0, ibo = 0;
    ops.glGenBuffers(1, &vbo);
    ops.glGenBuffers(1, &ibo);

    auto runCase = [&](float dirX, float dirZ, bool addOverlap, std::vector<uint8_t>& outRgba, bool useDeferred = false) -> bool {
        auto vCopy = verts;
        auto iCopy = idxs;
        if (addOverlap) {
            for (int rep = 0; rep < 3; ++rep) {
                uint16_t base = static_cast<uint16_t>(vCopy.size());
                for (int k = 0; k < 4; ++k) vCopy.push_back(verts[k]);
                iCopy.push_back(base + 0); iCopy.push_back(base + 1); iCopy.push_back(base + 2);
                iCopy.push_back(base + 1); iCopy.push_back(base + 3); iCopy.push_back(base + 2);
            }
        }
        ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, vbo);
        ops.glBufferData(hs_world_shadow::kGlArrayBuffer, vCopy.size() * sizeof(hs_sprite_batch::BatchedVertex), vCopy.data(), hs_world_shadow::kGlStaticDraw);
        ops.glBindBuffer(hs_world_shadow::kGlElementArrayBuffer, ibo);
        ops.glBufferData(hs_world_shadow::kGlElementArrayBuffer, iCopy.size() * sizeof(uint16_t), iCopy.data(), hs_world_shadow::kGlStaticDraw);

        hs_world_shadow::WorldShadowEngine eng;
        eng.SetOps(ops, env.isNativeGl);
        hs_gl_cache::ThreadState glCache;
        glCache.caps[0] = 1;
        glCache.caps[1] = 0;
        glCache.caps[2] = 0;
        glCache.caps[3] = 0;
        glCache.blendSrc = hs_world_shadow::kGlSrcAlpha;
        glCache.blendDst = hs_world_shadow::kGlOneMinusSrcAlpha;
        glCache.blendEq  = hs_world_shadow::kGlFuncAdd;
        glCache.colorMask = 15;
        glCache.depthMask = 0;

        const float pv[16] = {
            1.0f / 20.0f, 0, 0, 0,
            0,            0, 0, 0,
            0, 1.0f / 20.0f, 0, 0,
            0,            0, 0, 1
        };
        hs_world_shadow::ShadowConfig cfg{};
        cfg.dirWx = dirX; cfg.dirWz = dirZ; cfg.stretch = 2.0f; cfg.projOn = 1.0f;
        cfg.windTime = 0.0f; cfg.swayAmp = 0.0f; cfg.heightFade = 0.0f; cfg.penumbraBias = 1.0f;
        cfg.density = 0.60f; cfg.soft = 0.80f;
        cfg.tintR = 0.10f; cfg.tintG = 0.15f; cfg.tintB = 0.40f;
        cfg.hdMode = true; cfg.skipNightmare = true;
        cfg.hasLuaCam = true; cfg.luaCamX = 0.0f; cfg.luaCamZ = 0.0f;
        cfg.luaCamRightX = 1.0f; cfg.luaCamRightZ = 0.0f;

        const int32_t vp[4] = { 0, 0, 256, 256 };
        hs_world_shadow::ShadowPassState saved{};
        saved.dstFbo = primaryFbo; saved.dstProg = 0;
        memcpy(saved.vp, vp, sizeof(vp));
        saved.activeUnit = 0; saved.boundArrayBuf = vbo; saved.boundElemBuf = ibo;
        saved.hasClearColor = true;
        saved.clearColor[0] = 0.4f; saved.clearColor[1] = 0.7f; saved.clearColor[2] = 0.3f; saved.clearColor[3] = 0.8f;

        // Frame 1: Render shadows into ping-pong slot 0
        eng.OnGroundOrOceanDraw(primaryFbo, vp);
        eng.OnMatrixPV(pv);
        if (!eng.OnBeforeFirstStandingEntity(cfg, saved, glCache)) {
            printf("  [FAIL] Frame 1 OnBeforeFirstStandingEntity failed!\n");
            return false;
        }
        if (useDeferred) {
            // Split into 2 deferred batches (with non-zero indexByteOffset!) and replay in 1 single FBO bind!
            const uint32_t texArr[1] = { spriteTex };
            const uint32_t firstCnt  = static_cast<uint32_t>(idxs.size());
            const uint32_t secondCnt = static_cast<uint32_t>(iCopy.size()) - firstCnt;
            if (!eng.RecordBatchedShadow(vbo, ibo, firstCnt, 0u, 0u, 1u, texArr, nullptr, primaryFbo)) {
                printf("  [FAIL] Deferred RecordBatchedShadow #1 failed!\n");
                return false;
            }
            if (secondCnt > 0u) {
                if (!eng.RecordBatchedShadow(vbo, ibo, secondCnt, 0u,
                                             static_cast<uintptr_t>(firstCnt) * sizeof(uint16_t),
                                             1u, texArr, nullptr, primaryFbo)) {
                    printf("  [FAIL] Deferred RecordBatchedShadow #2 failed!\n");
                    return false;
                }
            }
            eng.FlushDeferredBatchedShadows(cfg, saved, glCache);
        } else {
            ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, vbo);
            ops.glBindBuffer(hs_world_shadow::kGlElementArrayBuffer, ibo);
            ops.glEnableVertexAttribArray(0);
            ops.glVertexAttribPointer(0, 4, hs_world_shadow::kGlFloat, 0, sizeof(hs_sprite_batch::BatchedVertex),
                                      reinterpret_cast<const void*>(offsetof(hs_sprite_batch::BatchedVertex, x)));
            ops.glEnableVertexAttribArray(7);
            ops.glVertexAttribPointer(7, 4, hs_world_shadow::kGlFloat, 0, sizeof(hs_sprite_batch::BatchedVertex),
                                      reinterpret_cast<const void*>(offsetof(hs_sprite_batch::BatchedVertex, wx)));
            ops.glEnableVertexAttribArray(5);
            ops.glVertexAttribPointer(5, 4, hs_world_shadow::kGlFloat, 0, sizeof(hs_sprite_batch::BatchedVertex),
                                      reinterpret_cast<const void*>(offsetof(hs_sprite_batch::BatchedVertex, mr)));
            ops.glEnableVertexAttribArray(6);
            ops.glVertexAttribPointer(6, 4, hs_world_shadow::kGlFloat, 0, sizeof(hs_sprite_batch::BatchedVertex),
                                      reinterpret_cast<const void*>(offsetof(hs_sprite_batch::BatchedVertex, ar)));
            eng.MirrorBatchedSprites(static_cast<uint32_t>(iCopy.size()), cfg, saved, glCache);
            ops.glDisableVertexAttribArray(5);
            ops.glDisableVertexAttribArray(6);
            ops.glDisableVertexAttribArray(7);
        }
        eng.OnEndFrame();
        if (useDeferred && eng.LastFrameDeferredBatches() != 2u) {
            printf("  [FAIL] Deferred replay batch count mismatch: got=%u exp=2\n", eng.LastFrameDeferredBatches());
            return false;
        }
        if (!checkNoErr("Frame 1")) return false;

        // Frame 2: Clear PrimaryRT to turf green (0.4, 0.7, 0.3, 0.8), composite shadow quad onto PrimaryRT!
        ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, primaryFbo);
        ops.glViewport(0, 0, 256, 256);
        ops.glColorMask(1, 1, 1, 1);
        ops.glClearColor(0.4f, 0.7f, 0.3f, 0.8f);
        ops.glClear(hs_world_shadow::kGlColorBufferBit);
        eng.OnGroundOrOceanDraw(primaryFbo, vp);
        eng.OnMatrixPV(pv);
        if (!eng.OnBeforeFirstStandingEntity(cfg, saved, glCache)) {
            printf("  [FAIL] Frame 2 OnBeforeFirstStandingEntity failed!\n");
            return false;
        }
        eng.OnEndFrame();
        if (!checkNoErr("Frame 2")) return false;

        outRgba.resize(256 * 256 * 4);
        ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, primaryFbo);
        env.glReadPixels(0, 0, 256, 256, hs_world_shadow::kGlRgba, hs_world_shadow::kGlUnsignedByte, outRgba.data());
        eng.ResetContext(true);
        return checkNoErr("ReadPixels + ResetContext");
    };

    auto analyze = [](const std::vector<uint8_t>& rgba, int& shadowedPx, int& minG, int& onGroundAreaShadowed, bool& alphaIntact) {
        shadowedPx = 0; minG = 255; onGroundAreaShadowed = 0; alphaIntact = true;
        for (int y = 0; y < 256; ++y) {
            for (int x = 0; x < 256; ++x) {
                const size_t i = (static_cast<size_t>(y) * 256 + x) * 4;
                const int g = rgba[i + 1];
                const int a = rgba[i + 3];
                if (abs(a - 204) > 2) alphaIntact = false;
                if (g < 168) {
                    ++shadowedPx;
                    if (g < minG) minG = g;
                    if (x >= 20 && x <= 75 && y >= 110 && y <= 185) ++onGroundAreaShadowed;
                }
            }
        }
    };

    std::vector<uint8_t> imgFront1, imgFront4, imgSide1;
    if (!runCase(0.0f, 1.0f, false, imgFront1)) return false;
    if (!runCase(0.0f, 1.0f, true, imgFront4)) return false;
    if (!runCase(1.0f, 0.0f, false, imgSide1)) return false;
    if (env.isNativeGl) {
        std::vector<uint8_t> imgFront4Def;
        if (!runCase(0.0f, 1.0f, true, imgFront4Def, true)) return false;
        int maxDiff = 0;
        for (size_t i = 0; i < imgFront4.size(); ++i) {
            int d = abs(static_cast<int>(imgFront4[i]) - static_cast<int>(imgFront4Def[i]));
            if (d > maxDiff) maxDiff = d;
        }
        printf("  Deferred Shadow Replay (2 batches -> 1 FBO bind): maxPixelDiff=%d -> %s\n",
               maxDiff, maxDiff == 0 ? "BIT_EXACT_PASS" : "DIFF_FAIL");
        if (maxDiff != 0) return false;
    }

    int pxF1 = 0, minGF1 = 0, ogF1 = 0; bool aF1 = false;
    int pxF4 = 0, minGF4 = 0, ogF4 = 0; bool aF4 = false;
    int pxS1 = 0, minGS1 = 0, ogS1 = 0; bool aS1 = false;
    analyze(imgFront1, pxF1, minGF1, ogF1, aF1);
    analyze(imgFront4, pxF4, minGF4, ogF4, aF4);
    analyze(imgSide1,  pxS1, minGS1, ogS1, aS1);

    printf("  Frontal 1x: shadowedPx=%d minG=%d (turfG=179) onGroundCulled=%s alphaIntact=%d\n",
           pxF1, minGF1, ogF1 == 0 ? "YES" : "NO", aF1 ? 1 : 0);
    printf("  Overlap 4x: shadowedPx=%d minG=%d (diffMinG=%d -> %s)\n",
           pxF4, minGF4, abs(minGF4 - minGF1), abs(minGF4 - minGF1) <= 2 ? "ZERO_BLOB_PASS" : "BLOB_FAIL");
    printf("  Side90 1x : shadowedPx=%d minG=%d (areaRatio=%.2f vs Frontal -> %s)\n",
           pxS1, minGS1, pxF1 > 0 ? (double)pxS1 / pxF1 : 0.0,
           (pxF1 > 0 && pxS1 > pxF1 * 0.70) ? "FULL_WIDTH_PASS" : "RAZOR_COLLAPSE_FAIL");

    // Case D: 3D Entity Shadow Receiving verification
    bool entityRecvPass = false;
    {
        std::vector<hs_sprite_batch::BatchedVertex> ev;
        std::vector<uint16_t> ei;
        auto addSp = [&](float cx, float cz, float halfW, float hTop) {
            uint16_t base = static_cast<uint16_t>(ev.size());
            auto mk = [&](float lx, float wy, float u, float v) {
                hs_sprite_batch::BatchedVertex bv{};
                bv.x = 1.0f; bv.y = 0.0f; bv.u = u; bv.v = v;
                bv.wx = cx + lx; bv.wy = wy; bv.wz = cz; bv.slot = 0.0f;
                bv.mr = 1.0f; bv.mg = 1.0f; bv.mb = 1.0f; bv.ma = 1.0f;
                bv.ar = 0.0f; bv.ag = 0.0f; bv.ab = 0.0f; bv.pad = lx;
                return bv;
            };
            ev.push_back(mk(-halfW, 0.0f, 0.1f, 0.1f));
            ev.push_back(mk(+halfW, 0.0f, 0.9f, 0.1f));
            ev.push_back(mk(-halfW, hTop, 0.1f, 0.9f));
            ev.push_back(mk(+halfW, hTop, 0.9f, 0.9f));
            ei.push_back(base + 0); ei.push_back(base + 1); ei.push_back(base + 2);
            ei.push_back(base + 1); ei.push_back(base + 3); ei.push_back(base + 2);
        };
        addSp(0.0f,  0.0f, 2.0f, 6.0f);
        addSp(0.0f,  4.0f, 0.35f, 1.8f);
        addSp(15.0f, 0.0f, 0.35f, 1.8f);

        ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, vbo);
        ops.glBufferData(hs_world_shadow::kGlArrayBuffer, ev.size() * sizeof(hs_sprite_batch::BatchedVertex), ev.data(), hs_world_shadow::kGlStaticDraw);
        ops.glBindBuffer(hs_world_shadow::kGlElementArrayBuffer, ibo);
        ops.glBufferData(hs_world_shadow::kGlElementArrayBuffer, ei.size() * sizeof(uint16_t), ei.data(), hs_world_shadow::kGlStaticDraw);

        hs_world_shadow::WorldShadowEngine eng;
        eng.SetOps(ops, env.isNativeGl);
        hs_gl_cache::ThreadState glCache;
        glCache.caps[0] = 1; glCache.caps[1] = 0; glCache.caps[2] = 0; glCache.caps[3] = 0;
        glCache.blendSrc = hs_world_shadow::kGlSrcAlpha; glCache.blendDst = hs_world_shadow::kGlOneMinusSrcAlpha;
        glCache.blendEq = hs_world_shadow::kGlFuncAdd; glCache.colorMask = 15; glCache.depthMask = 0;

        const float pv[16] = { 1.0f/20.0f,0,0,0, 0,0,0,0, 0,1.0f/20.0f,0,0, 0,0,0,1 };
        hs_world_shadow::ShadowConfig cfg{};
        cfg.dirWx = 0.0f; cfg.dirWz = 1.0f; cfg.stretch = 1.5f; cfg.projOn = 1.0f;
        cfg.density = 0.70f; cfg.soft = 1.0f; cfg.tintR = 0.1f; cfg.tintG = 0.15f; cfg.tintB = 0.4f;
        cfg.hdMode = true; cfg.hasLuaCam = true; cfg.luaCamRightX = 1.0f; cfg.luaCamRightZ = 0.0f;
        const int32_t vp[4] = { 0, 0, 256, 256 };
        hs_world_shadow::ShadowPassState saved{};
        saved.dstFbo = primaryFbo; memcpy(saved.vp, vp, sizeof(vp));
        saved.boundArrayBuf = vbo; saved.boundElemBuf = ibo;

        eng.OnGroundOrOceanDraw(primaryFbo, vp); eng.OnMatrixPV(pv);
        eng.OnBeforeFirstStandingEntity(cfg, saved, glCache);
        ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, vbo);
        ops.glBindBuffer(hs_world_shadow::kGlElementArrayBuffer, ibo);
        ops.glEnableVertexAttribArray(0); ops.glVertexAttribPointer(0, 4, hs_world_shadow::kGlFloat, 0, 64, (void*)0);
        ops.glEnableVertexAttribArray(7); ops.glVertexAttribPointer(7, 4, hs_world_shadow::kGlFloat, 0, 64, (void*)16);
        ops.glEnableVertexAttribArray(5); ops.glVertexAttribPointer(5, 4, hs_world_shadow::kGlFloat, 0, 64, (void*)32);
        ops.glEnableVertexAttribArray(6); ops.glVertexAttribPointer(6, 4, hs_world_shadow::kGlFloat, 0, 64, (void*)48);
        eng.MirrorBatchedSprites(static_cast<uint32_t>(ei.size()), cfg, saved, glCache);
        ops.glDisableVertexAttribArray(5); ops.glDisableVertexAttribArray(6); ops.glDisableVertexAttribArray(7);
        eng.OnEndFrame();

        eng.OnGroundOrOceanDraw(primaryFbo, vp); eng.OnMatrixPV(pv);
        eng.OnBeforeFirstStandingEntity(cfg, saved, glCache);
        float rBounds[4], rTint[4];
        eng.GetEntityReceiveUniforms(cfg, rBounds, rTint);
        hs_world_shadow::ShadowConfig cfgZeroRecv = cfg;
        cfgZeroRecv.entityRecv = 0.0f;
        float zBounds[4], zTint[4];
        eng.GetEntityReceiveUniforms(cfgZeroRecv, zBounds, zTint);
        const bool recvKnobOk = (std::fabs(rTint[3] - cfg.density * cfg.entityRecv) < 1e-4f) &&
                                (zBounds[3] == 0.0f) && (zTint[3] == 0.0f);

        std::string rVs = std::string(env.isNativeGl ? "#version 100\n" : "") +
            "attribute vec2 a_p; varying vec2 v_p; void main() { v_p = a_p; gl_Position = vec4(a_p, 0.0, 1.0); }\n";
        std::string rFs = std::string(env.isNativeGl ? "#version 100\n" : "") +
            "#if defined(GL_ES)\nprecision highp float;\n#endif\n"
            "uniform sampler2D HS_SHADOW_MASK; uniform vec4 HS_RECV_BOUNDS; uniform vec4 HS_HD2D_SHAD_PROJ;\n"
            "uniform vec4 u_probePos;\n"
            "void main() {\n"
            "  float hs_py = max(u_probePos.y, 0.0);\n"
            "  vec2 hs_rdir = normalize(HS_HD2D_SHAD_PROJ.xy + vec2(1e-6, 0.0));\n"
            "  vec2 hs_rperp = vec2(-hs_rdir.y, hs_rdir.x);\n"
            "  vec2 hs_rg = u_probePos.xz + hs_rdir * (hs_py * HS_HD2D_SHAD_PROJ.z);\n"
            "  vec2 hs_ruv = (hs_rg - HS_RECV_BOUNDS.xy) * (HS_RECV_BOUNDS.z * 0.5) + 0.5;\n"
            "  float hs_uvs = HS_RECV_BOUNDS.z * 0.5;\n"
            "  vec2 hs_dp1 = hs_rperp * (((HS_RECV_BOUNDS.w > 1.5) ? 0.62 : 0.28) * hs_uvs);\n"
            "  vec2 hs_dp2 = (HS_RECV_BOUNDS.w > 1.5) ? (hs_rperp * (0.95 * hs_uvs)) : (hs_rdir * (0.22 * hs_uvs));\n"
            "  vec4 hs_t0 = texture2D(HS_SHADOW_MASK, hs_ruv);\n"
            "  vec4 hs_t1 = texture2D(HS_SHADOW_MASK, hs_ruv + hs_dp1);\n"
            "  vec4 hs_t2 = texture2D(HS_SHADOW_MASK, hs_ruv - hs_dp1);\n"
            "  vec4 hs_t3 = texture2D(HS_SHADOW_MASK, hs_ruv + hs_dp2);\n"
            "  vec4 hs_t4 = texture2D(HS_SHADOW_MASK, hs_ruv - hs_dp2);\n"
            "  float hs_o0 = hs_t0.r * smoothstep(0.45, 1.15, hs_t0.b * 7.5 - hs_py);\n"
            "  float hs_o1 = hs_t1.r * smoothstep(0.45, 1.15, hs_t1.b * 7.5 - hs_py);\n"
            "  float hs_o2 = hs_t2.r * smoothstep(0.45, 1.15, hs_t2.b * 7.5 - hs_py);\n"
            "  float hs_o3 = hs_t3.r * smoothstep(0.45, 1.15, hs_t3.b * 7.5 - hs_py);\n"
            "  float hs_o4 = hs_t4.r * smoothstep(0.45, 1.15, hs_t4.b * 7.5 - hs_py);\n"
            "  float hs_occ = (HS_RECV_BOUNDS.w > 1.5) ? max(hs_o0, max((hs_o1 + hs_o2) * 0.50, min(hs_o3, hs_o4))) : (hs_o0 * 0.40 + (hs_o1 + hs_o2 + hs_o3 + hs_o4) * 0.15);\n"
            "  gl_FragColor = vec4(clamp(hs_occ, 0.0, 1.0), hs_t0.b, hs_t0.r, 1.0);\n"
            "}\n";
        auto mkSh = [&](uint32_t ty, const std::string& s) {
            uint32_t sh = ops.glCreateShader(ty); const char* p = s.c_str(); int32_t l = (int32_t)s.size();
            ops.glShaderSource(sh, 1, &p, &l); ops.glCompileShader(sh); return sh;
        };
        uint32_t rvs = mkSh(hs_world_shadow::kGlVertexShader, rVs);
        uint32_t rfs = mkSh(hs_world_shadow::kGlFragmentShader, rFs);
        uint32_t rpr = ops.glCreateProgram();
        ops.glAttachShader(rpr, rvs); ops.glAttachShader(rpr, rfs);
        ops.glBindAttribLocation(rpr, 0, "a_p");
        ops.glLinkProgram(rpr);
        ops.glDeleteShader(rvs); ops.glDeleteShader(rfs);
        ops.glUseProgram(rpr);
        ops.glUniform1i(ops.glGetUniformLocation(rpr, "HS_SHADOW_MASK"), 14);
        ops.glUniform4fv(ops.glGetUniformLocation(rpr, "HS_RECV_BOUNDS"), 1, rBounds);
        const float sp4[4] = { 0.0f, 1.0f, 1.5f, 1.0f };
        ops.glUniform4fv(ops.glGetUniformLocation(rpr, "HS_HD2D_SHAD_PROJ"), 1, sp4);
        int32_t locPr = ops.glGetUniformLocation(rpr, "u_probePos");

        float qv[8] = { -1,-1, 1,-1, -1,1, 1,1 };
        uint32_t qvbo = 0; ops.glGenBuffers(1, &qvbo);
        ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, qvbo);
        ops.glBufferData(hs_world_shadow::kGlArrayBuffer, sizeof(qv), qv, hs_world_shadow::kGlStaticDraw);
        ops.glEnableVertexAttribArray(0); ops.glVertexAttribPointer(0, 2, hs_world_shadow::kGlFloat, 0, 8, nullptr);
        ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, primaryFbo);
        ops.glViewport(0, 0, 16, 16);
        ops.glDisable(hs_world_shadow::kGlBlend);
        ops.glColorMask(1, 1, 1, 1);

        auto probe = [&](float px, float py, float pz) -> float {
            float v4[4] = { px, py, pz, 1.0f };
            ops.glUniform4fv(locPr, 1, v4);
            ops.glDrawArrays(hs_world_shadow::kGlTriangleStrip, 0, 4);
            uint8_t px4[4] = {};
            env.glReadPixels(8, 8, 1, 1, hs_world_shadow::kGlRgba, hs_world_shadow::kGlUnsignedByte, px4);
            return px4[0] / 255.0f;
        };
        float occInShade   = probe(0.0f,  1.0f, 4.0f);
        float occOpenSun   = probe(15.0f, 1.0f, 0.0f);
        float occPastTip   = probe(0.0f,  1.8f, 7.5f);
        float occSideOut   = probe(2.75f, 1.0f, 4.0f);
        printf("  3D Entity Recv (BlueNoise=%d, recvKnob=%d): inTreeShade=%.2f (>0.70) | selfInSun=%.2f | headPastTip=%.2f | sideOut=%.2f\n",
               eng.HasBlueNoise() ? 1 : 0, recvKnobOk ? 1 : 0, occInShade, occOpenSun, occPastTip, occSideOut);
        entityRecvPass = eng.HasBlueNoise() && recvKnobOk && (occInShade > 0.70f) && (occOpenSun < 0.02f) && (occPastTip < 0.02f) && (occSideOut < 0.02f);

        ops.glDeleteBuffers(1, &qvbo);
        ops.glDeleteProgram(rpr);
        eng.OnEndFrame();
        eng.ResetContext(true);
    }

    // Case E (Round 2 P1 #3 + Round 3 P1 #2): Real GPU Host GL State Preservation across:
    //   1) First enable with unknown glCache (ResetBindingsOnly),
    //   2) Resolution switch (1024 -> 512), and
    //   3) glCache disabled (glCacheTrusted = false)
    //   INCLUDING asymmetric RGB vs Alpha blend factors AND blend equations!
    bool statePreservePass = true;
    {
        static constexpr uint32_t kGlFuncReverseSubtract = 0x800Bu;
        uint32_t hostRbo = 0;
        ops.glGenRenderbuffers(1, &hostRbo);
        ops.glBindRenderbuffer(hs_world_shadow::kGlRenderbuffer, hostRbo);
        ops.glRenderbufferStorage(hs_world_shadow::kGlRenderbuffer, hs_world_shadow::kGlDepthComponent16, 64, 64);

        ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, primaryFbo);
        ops.glDepthMask(1);
        ops.glEnable(hs_world_shadow::kGlDepthTest);
        ops.glColorMask(1, 1, 0, 1);
        ops.glEnable(hs_world_shadow::kGlBlend);
        if (!ops.glBlendFuncSeparate || !ops.glBlendEquationSeparate) {
            printf("  [FAIL] Case E: glBlendFuncSeparate=%p glBlendEquationSeparate=%p missing!\n",
                   reinterpret_cast<void*>(ops.glBlendFuncSeparate),
                   reinterpret_cast<void*>(ops.glBlendEquationSeparate));
            statePreservePass = false;
        } else {
            ops.glBlendFuncSeparate(hs_world_shadow::kGlOne, hs_world_shadow::kGlOneMinusSrcAlpha,
                                    hs_world_shadow::kGlZero, hs_world_shadow::kGlOne);
            ops.glBlendEquationSeparate(hs_world_shadow::kGlFuncAdd, kGlFuncReverseSubtract);
        }
        ops.glUseProgram(0u);
        (void)ops.glGetError();

        hs_world_shadow::WorldShadowEngine eng;
        eng.SetOps(ops, env.isNativeGl);
        hs_gl_cache::ThreadState glCache;
        glCache.ResetBindingsOnly(); // All state slots UNKNOWN!
        // Round 4 P1 #2 (exact auditor regression): Host sets symmetric blend through tracked
        // setters (populating glCache RGB slots), then diverges Alpha via separate setters!
        if (glCache.ShouldCallBlendFunc(hs_world_shadow::kGlOne, hs_world_shadow::kGlOneMinusSrcAlpha))
            ops.glBlendFunc(hs_world_shadow::kGlOne, hs_world_shadow::kGlOneMinusSrcAlpha);
        if (glCache.ShouldCallBlendEquation(hs_world_shadow::kGlFuncAdd))
            ops.glBlendEquation(hs_world_shadow::kGlFuncAdd);
        // Also enable GL_STENCIL_TEST so we verify WorldShadowEngine saves/disables/restores stencil test!
        ops.glEnable(hs_world_shadow::kGlStencilTest);
        if (ops.glBlendFuncSeparate && ops.glBlendEquationSeparate) {
            ops.glBlendFuncSeparate(hs_world_shadow::kGlOne, hs_world_shadow::kGlOneMinusSrcAlpha,
                                    hs_world_shadow::kGlZero, hs_world_shadow::kGlOne);
            ops.glBlendEquationSeparate(hs_world_shadow::kGlFuncAdd, kGlFuncReverseSubtract);
        }

        const float pv[16] = { 1.0f/20.0f,0,0,0, 0,0,0,0, 0,1.0f/20.0f,0,0, 0,0,0,1 };
        const int32_t vp[4] = { 0, 0, 256, 256 };
        hs_world_shadow::ShadowConfig cfg{};
        cfg.dirWx = 0.0f; cfg.dirWz = 1.0f; cfg.stretch = 1.5f; cfg.projOn = 1.0f;
        cfg.density = 0.60f; cfg.soft = 1.0f; cfg.hdMode = true;
        hs_world_shadow::ShadowPassState saved{};
        saved.dstFbo = primaryFbo; saved.dstProg = 0u;
        memcpy(saved.vp, vp, sizeof(vp));
        saved.boundArrayBuf = vbo; saved.boundElemBuf = ibo;
        saved.glCacheTrusted = true;

        auto verifyHostState = [&](const char* tag) -> bool {
            int32_t dm = -1, dt = -1, st = -1, rbo = -1;
            int32_t bsRgb = -1, bdRgb = -1, bsA = -1, bdA = -1;
            int32_t eqRgb = -1, eqA = -1;
            int32_t cm[4] = {-1,-1,-1,-1};
            env.glGetIntegerv(hs_world_shadow::kGlDepthWritemask, &dm);
            env.glGetIntegerv(hs_world_shadow::kGlDepthTest, &dt);
            env.glGetIntegerv(hs_world_shadow::kGlStencilTest, &st);
            env.glGetIntegerv(hs_world_shadow::kGlRenderbufferBinding, &rbo);
            env.glGetIntegerv(hs_world_shadow::kGlBlendSrcRgb, &bsRgb);
            env.glGetIntegerv(hs_world_shadow::kGlBlendDstRgb, &bdRgb);
            env.glGetIntegerv(hs_world_shadow::kGlBlendSrcAlpha, &bsA);
            env.glGetIntegerv(hs_world_shadow::kGlBlendDstAlpha, &bdA);
            env.glGetIntegerv(hs_world_shadow::kGlBlendEquationRgb, &eqRgb);
            env.glGetIntegerv(hs_world_shadow::kGlBlendEquationAlpha, &eqA);
            env.glGetIntegerv(hs_world_shadow::kGlColorWritemask, cm);
            uint32_t err = ops.glGetError();
            bool ok = (err == 0u) && (dm == 1) && (dt == 1) && (st == 1) &&
                      (static_cast<uint32_t>(rbo) == hostRbo) &&
                      (static_cast<uint32_t>(bsRgb) == hs_world_shadow::kGlOne) &&
                      (static_cast<uint32_t>(bdRgb) == hs_world_shadow::kGlOneMinusSrcAlpha) &&
                      (static_cast<uint32_t>(bsA) == hs_world_shadow::kGlZero) &&
                      (static_cast<uint32_t>(bdA) == hs_world_shadow::kGlOne) &&
                      (static_cast<uint32_t>(eqRgb) == hs_world_shadow::kGlFuncAdd) &&
                      (static_cast<uint32_t>(eqA) == kGlFuncReverseSubtract) &&
                      (cm[0] == 1 && cm[1] == 1 && cm[2] == 0 && cm[3] == 1);
            if (!ok) {
                printf("  [FAIL] Case E (%s): err=0x%X dm=%d dt=%d st=%d rbo=%d(exp %u) rgb=(0x%X,0x%X) a=(0x%X,0x%X) eq=(0x%X,0x%X) cm=%d,%d,%d,%d\n",
                       tag, err, dm, dt, st, rbo, hostRbo, bsRgb, bdRgb, bsA, bdA, eqRgb, eqA, cm[0], cm[1], cm[2], cm[3]);
            }
            return ok;
        };

        // Step 1: First enable (1024x1024) with unknown glCache + MirrorBatchedSprites (exercises ApplyShadowWriteRasterState!)
        eng.OnGroundOrOceanDraw(primaryFbo, vp);
        eng.OnMatrixPV(pv);
        if (!eng.OnBeforeFirstStandingEntity(cfg, saved, glCache) || !verifyHostState("first-init unknown glCache")) {
            statePreservePass = false;
        }
        ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, vbo);
        ops.glBindBuffer(hs_world_shadow::kGlElementArrayBuffer, ibo);
        ops.glEnableVertexAttribArray(0); ops.glVertexAttribPointer(0, 4, hs_world_shadow::kGlFloat, 0, 64, (void*)0);
        ops.glEnableVertexAttribArray(7); ops.glVertexAttribPointer(7, 4, hs_world_shadow::kGlFloat, 0, 64, (void*)16);
        ops.glEnableVertexAttribArray(5); ops.glVertexAttribPointer(5, 4, hs_world_shadow::kGlFloat, 0, 64, (void*)32);
        ops.glEnableVertexAttribArray(6); ops.glVertexAttribPointer(6, 4, hs_world_shadow::kGlFloat, 0, 64, (void*)48);
        eng.MirrorBatchedSprites(6u, cfg, saved, glCache);
        ops.glDisableVertexAttribArray(5); ops.glDisableVertexAttribArray(6); ops.glDisableVertexAttribArray(7);
        if (!verifyHostState("after MirrorBatchedSprites")) {
            statePreservePass = false;
        }
        eng.OnEndFrame();

        // Step 1b: Frame 2 (same 1024x1024) exercises CompositeGroundQuad (mutates glBlendEquation + glBlendFunc!)
        eng.OnGroundOrOceanDraw(primaryFbo, vp);
        eng.OnMatrixPV(pv);
        if (!eng.OnBeforeFirstStandingEntity(cfg, saved, glCache) || !verifyHostState("after CompositeGroundQuad")) {
            statePreservePass = false;
        }
        eng.OnEndFrame();

        // Step 2: Resolution switch (1024 -> 512) with ResetBindingsOnly (simulating post-SwapBuffers)
        glCache.ResetBindingsOnly();
        cfg.hdMode = false;
        eng.OnGroundOrOceanDraw(primaryFbo, vp);
        eng.OnMatrixPV(pv);
        if (!eng.OnBeforeFirstStandingEntity(cfg, saved, glCache) || !verifyHostState("res-switch 1024->512")) {
            statePreservePass = false;
        }
        eng.OnEndFrame();

        // Step 3: glCache disabled (glCacheTrusted = false) with stale/wrong values inside glCache!
        glCache.depthMask = 0; // Stale wrong value — MUST be ignored when glCacheTrusted=false!
        glCache.caps[1] = 0;
        saved.glCacheTrusted = false;
        eng.OnGroundOrOceanDraw(primaryFbo, vp);
        eng.OnMatrixPV(pv);
        if (!eng.OnBeforeFirstStandingEntity(cfg, saved, glCache) || !verifyHostState("glCache OFF")) {
            statePreservePass = false;
        }
        eng.OnEndFrame();
        eng.ResetContext(true);

        // Restore clean defaults for tear-down
        ops.glBindRenderbuffer(hs_world_shadow::kGlRenderbuffer, 0u);
        ops.glDeleteRenderbuffers(1, &hostRbo);
        ops.glColorMask(1, 1, 1, 1);
        ops.glBlendEquation(hs_world_shadow::kGlFuncAdd);
        ops.glBlendFunc(hs_world_shadow::kGlSrcAlpha, hs_world_shadow::kGlOneMinusSrcAlpha);
        ops.glDisable(hs_world_shadow::kGlDepthTest);
        ops.glDisable(hs_world_shadow::kGlStencilTest);
        printf("  Host GL State Preservation (tracked RGB + separate Alpha + Stencil + 1024->512 + cache OFF): %s\n",
               statePreservePass ? "PASS" : "FAIL");
    }

    // Live GPU check: verify glGetBufferParameteriv(GL_BUFFER_SIZE) works on real Desktop GL & ANGLE 1.0
    bool bufParamPass = false;
    {
        if (env.glGetBufferParameteriv) {
            uint32_t probeBuf = 0u;
            ops.glGenBuffers(1, &probeBuf);
            ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, probeBuf);
            constexpr ptrdiff_t kProbeBytes = 1048576; // 1 MiB SpriteBatcher ring slot size
            ops.glBufferData(hs_world_shadow::kGlArrayBuffer, kProbeBytes, nullptr, hs_sprite_batch::kGlStreamDraw);
            int32_t actualBytes = 0;
            env.glGetBufferParameteriv(hs_world_shadow::kGlArrayBuffer, hs_sprite_batch::kGlBufferSize, &actualBytes);
            ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, 0u);
            ops.glDeleteBuffers(1, &probeBuf);
            const uint32_t err = ops.glGetError();
            bufParamPass = (err == 0u) && (actualBytes >= static_cast<int32_t>(kProbeBytes));
            printf("  Live GPU glGetBufferParameteriv(GL_BUFFER_SIZE): got=%d (exp>=%d) err=0x%X -> %s\n",
                   actualBytes, static_cast<int>(kProbeBytes), err, bufParamPass ? "PASS" : "FAIL");
        } else {
            printf("  [FAIL] Live GPU glGetBufferParameteriv missing!\n");
        }
    }

    // Case P1-P4 (v2.9.5 Multi-Light Dynamic Point Shadows): Live Dual-Backend Pixel-Exact Suite
    //   P1: 360-deg opposing radial projection from campfire at (0,0) with dirStrength=0 (pure night)
    //   P2: Near-flame self-shadow exclusion (dist=0.1 < 0.42) + NaN/Inf point light sanitization
    //   P3: Strong campfire canopy vs weak distant lantern root overlap -> ZERO bright hole on ANGLE DEPTH16 & ZERO double-black on GL_MAX
    //   P4: CPU Batch AABB culling -> 0 GPU shadow draw calls when batch is outside campfire radius
    bool pointLightPass = false;
    {
        auto renderPtScene = [&](const std::vector<hs_sprite_batch::BatchedVertex>& vData,
                                 const std::vector<uint16_t>& iData,
                                 const hs_world_shadow::ShadowConfig& cfg,
                                 const float* batchBoundsXZ,
                                 std::vector<uint8_t>& outRgba,
                                 uint32_t& outShadowDraws) -> bool {
            ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, vbo);
            ops.glBufferData(hs_world_shadow::kGlArrayBuffer, vData.size() * sizeof(hs_sprite_batch::BatchedVertex), vData.data(), hs_world_shadow::kGlStaticDraw);
            ops.glBindBuffer(hs_world_shadow::kGlElementArrayBuffer, ibo);
            ops.glBufferData(hs_world_shadow::kGlElementArrayBuffer, iData.size() * sizeof(uint16_t), iData.data(), hs_world_shadow::kGlStaticDraw);

            hs_world_shadow::WorldShadowEngine eng;
            eng.SetOps(ops, env.isNativeGl);
            hs_gl_cache::ThreadState glCache;
            glCache.caps[0] = 1; glCache.caps[1] = 0; glCache.caps[2] = 0; glCache.caps[3] = 0;
            glCache.blendSrc = hs_world_shadow::kGlSrcAlpha; glCache.blendDst = hs_world_shadow::kGlOneMinusSrcAlpha;
            glCache.blendEq = hs_world_shadow::kGlFuncAdd; glCache.colorMask = 15; glCache.depthMask = 0;

            const float pv[16] = { 1.0f/20.0f,0,0,0, 0,0,0,0, 0,1.0f/20.0f,0,0, 0,0,0,1 };
            const int32_t vp[4] = { 0, 0, 256, 256 };
            hs_world_shadow::ShadowPassState saved{};
            saved.dstFbo = primaryFbo; memcpy(saved.vp, vp, sizeof(vp));
            saved.boundArrayBuf = vbo; saved.boundElemBuf = ibo;
            saved.hasClearColor = true; saved.clearColor[0] = saved.clearColor[1] = saved.clearColor[2] = 200.0f / 255.0f; saved.clearColor[3] = 1.0f;

            // Frame 1: write point-light shadows into ShadowMask FBO
            eng.OnGroundOrOceanDraw(primaryFbo, vp);
            eng.OnMatrixPV(pv);
            if (!eng.OnBeforeFirstStandingEntity(cfg, saved, glCache)) {
                eng.ResetContext(true);
                return false;
            }
            ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, vbo);
            ops.glBindBuffer(hs_world_shadow::kGlElementArrayBuffer, ibo);
            ops.glEnableVertexAttribArray(0); ops.glVertexAttribPointer(0, 4, hs_world_shadow::kGlFloat, 0, 64, (void*)0);
            ops.glEnableVertexAttribArray(7); ops.glVertexAttribPointer(7, 4, hs_world_shadow::kGlFloat, 0, 64, (void*)16);
            ops.glEnableVertexAttribArray(5); ops.glVertexAttribPointer(5, 4, hs_world_shadow::kGlFloat, 0, 64, (void*)32);
            ops.glEnableVertexAttribArray(6); ops.glVertexAttribPointer(6, 4, hs_world_shadow::kGlFloat, 0, 64, (void*)48);
            eng.MirrorBatchedSprites(static_cast<uint32_t>(iData.size()), cfg, saved, glCache, batchBoundsXZ);
            ops.glDisableVertexAttribArray(5); ops.glDisableVertexAttribArray(6); ops.glDisableVertexAttribArray(7);
            eng.OnEndFrame();
            outShadowDraws = eng.LastFrameShadowDraws();

            // Frame 2: composite previous ShadowMask onto clean PrimaryRT
            ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, primaryFbo);
            ops.glViewport(0, 0, 256, 256);
            ops.glClearColor(200.0f / 255.0f, 200.0f / 255.0f, 200.0f / 255.0f, 1.0f);
            ops.glClear(hs_world_shadow::kGlColorBufferBit);
            eng.OnGroundOrOceanDraw(primaryFbo, vp);
            eng.OnMatrixPV(pv);
            eng.OnBeforeFirstStandingEntity(cfg, saved, glCache);
            eng.OnEndFrame();

            outRgba.resize(256 * 256 * 4);
            ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, primaryFbo);
            env.glReadPixels(0, 0, 256, 256, hs_world_shadow::kGlRgba, hs_world_shadow::kGlUnsignedByte, outRgba.data());
            eng.ResetContext(true);
            return checkNoErr("pointLightScene");
        };
        auto addPtQuad = [](std::vector<hs_sprite_batch::BatchedVertex>& v,
                            std::vector<uint16_t>& idx,
                            float cx, float cz, float halfW, float hTop) {
            uint16_t base = static_cast<uint16_t>(v.size());
            auto mk = [&](float lx, float wy, float u, float vt) {
                hs_sprite_batch::BatchedVertex bv{};
                bv.x = 1.0f; bv.y = 0.0f; bv.u = u; bv.v = vt;
                bv.wx = cx + lx; bv.wy = wy; bv.wz = cz; bv.slot = 0.0f;
                bv.mr = 1.0f; bv.mg = 1.0f; bv.mb = 1.0f; bv.ma = 1.0f;
                bv.ar = 0.0f; bv.ag = 0.0f; bv.ab = 0.0f; bv.pad = lx;
                return bv;
            };
            v.push_back(mk(-halfW, 0.0f, 0.1f, 0.1f));
            v.push_back(mk(+halfW, 0.0f, 0.9f, 0.1f));
            v.push_back(mk(-halfW, hTop, 0.1f, 0.9f));
            v.push_back(mk(+halfW, hTop, 0.9f, 0.9f));
            idx.push_back(base + 0); idx.push_back(base + 1); idx.push_back(base + 2);
            idx.push_back(base + 1); idx.push_back(base + 3); idx.push_back(base + 2);
        };
        auto sampleG = [](const std::vector<uint8_t>& rgba, float wx, float wz) -> int {
            int px = std::clamp(static_cast<int>((wx + 20.0f) / 40.0f * 256.0f), 0, 255);
            int py = std::clamp(static_cast<int>((wz + 20.0f) / 40.0f * 256.0f), 0, 255);
            return rgba[(py * 256 + px) * 4 + 1];
        };

        // P1 + P2: 360-deg opposing shadows + campfire self-shadow exclusion + NaN light sanitization
        std::vector<hs_sprite_batch::BatchedVertex> v12;
        std::vector<uint16_t> i12;
        addPtQuad(v12, i12, +4.0f, 0.0f, 1.2f, 4.0f); // East entity -> shadow projects +X (+6.0, 0)
        addPtQuad(v12, i12, -4.0f, 0.0f, 1.2f, 4.0f); // West entity -> shadow projects -X (-6.0, 0)
        addPtQuad(v12, i12, +0.1f, 0.0f, 1.2f, 4.0f); // Flame self-sprite (dist=0.1 < 0.42 -> MUST CULL!)

        hs_world_shadow::ShadowConfig cfg12{};
        cfg12.projOn = 0.0f; cfg12.dirStrength = 0.0f; cfg12.pointStretchScale = 1.0f;
        cfg12.density = 0.65f; cfg12.soft = 0.60f;
        cfg12.tintR = 0.10f; cfg12.tintG = 0.14f; cfg12.tintB = 0.38f;
        cfg12.hdMode = true; cfg12.hasLuaCam = true; cfg12.luaCamRightX = 1.0f; cfg12.luaCamRightZ = 0.0f;
        cfg12.pointLightCount = 2u;
        cfg12.pointLights[0] = { 0.0f, 0.0f, 16.0f, 1.0f };
        cfg12.pointLights[1] = { std::nanf(""), INFINITY, -5.0f, 2.0f }; // Poisoned slot -> safely skipped!

        std::vector<uint8_t> rgba12;
        uint32_t draws12 = 0;
        const bool ok12 = renderPtScene(v12, i12, cfg12, nullptr, rgba12, draws12);
        const int gEast   = ok12 ? sampleG(rgba12, +6.0f, 0.0f) : 255;
        const int gWest   = ok12 ? sampleG(rgba12, -6.0f, 0.0f) : 255;
        const int gCenter = ok12 ? sampleG(rgba12,  0.0f, 0.0f) : 0;
        const int gInE    = ok12 ? sampleG(rgba12, +1.8f, 0.0f) : 0;
        const int gNorth  = ok12 ? sampleG(rgba12,  0.0f, +6.0f) : 0;
        const bool p12Ok = ok12 && (draws12 == 1u) && (gEast < 125) && (gWest < 125) &&
                           (gCenter >= 195) && (gInE >= 195) && (gNorth >= 195);

        // P3: Multi-light overlap (Strong campfire tall canopy vs Weak distant lantern low root at (+2.5, 0.0))
        std::vector<hs_sprite_batch::BatchedVertex> v3;
        std::vector<uint16_t> i3;
        addPtQuad(v3, i3, 0.0f,  0.0f, 2.0f, 6.0f); // Entity A: tall canopy cast East onto (+2.5, 0) by strong Light 0
        addPtQuad(v3, i3, 2.5f, -0.8f, 2.0f, 1.8f); // Entity B: low root cast North onto (+2.5, 0) by weak Light 1

        hs_world_shadow::ShadowConfig cfg3Ref = cfg12;
        cfg3Ref.pointLightCount = 1u;
        cfg3Ref.pointLights[0] = { -3.0f, 0.0f, 18.0f, 0.95f };
        std::vector<uint8_t> rgba3Ref;
        uint32_t draws3Ref = 0;
        const bool ok3Ref = renderPtScene(v3, i3, cfg3Ref, nullptr, rgba3Ref, draws3Ref);
        const int gRef = ok3Ref ? sampleG(rgba3Ref, +2.5f, 0.0f) : 255;

        hs_world_shadow::ShadowConfig cfg3Mix = cfg3Ref;
        cfg3Mix.pointLightCount = 2u;
        cfg3Mix.pointLights[1] = { +2.5f, -6.0f, 12.0f, 0.22f };
        std::vector<uint8_t> rgba3Mix;
        uint32_t draws3Mix = 0;
        const bool ok3Mix = renderPtScene(v3, i3, cfg3Mix, nullptr, rgba3Mix, draws3Mix);
        const int gMix = ok3Mix ? sampleG(rgba3Mix, +2.5f, 0.0f) : 255;
        // On BOTH GL_MAX and ANGLE DEPTH16, weak Light 1 MUST NOT punch a bright hole or double-blacken!
        const bool p3Ok = ok3Ref && ok3Mix && (draws3Mix == 2u) && (gRef < 130) && (gMix < 135) && (abs(gMix - gRef) <= 12);

        // P4: CPU Batch AABB Culling -> far batch outside radius 10.0 issues ZERO shadow draw calls!
        const float farAabb[4] = { 25.0f, 30.0f, 25.0f, 30.0f };
        hs_world_shadow::ShadowConfig cfg4 = cfg12;
        cfg4.pointLightCount = 1u;
        cfg4.pointLights[0] = { 0.0f, 0.0f, 10.0f, 1.0f };
        std::vector<uint8_t> rgba4;
        uint32_t draws4 = 99u;
        const bool ok4 = renderPtScene(v12, i12, cfg4, farAabb, rgba4, draws4);
        const bool p4Ok = ok4 && (draws4 == 0u);

        // P5 (v2.9.5-E): Crisp shadow_soft = 0.00 on 2048x2048 FBO -> 1-tap crisp silhouette, 0 blur, 0 dark hole!
        hs_world_shadow::ShadowConfig cfg5 = cfg12;
        cfg5.soft = 0.0f;
        cfg5.hdMode = true;
        std::vector<uint8_t> rgba5;
        uint32_t draws5 = 0u;
        const bool ok5 = renderPtScene(v12, i12, cfg5, nullptr, rgba5, draws5);
        const int g5East   = ok5 ? sampleG(rgba5, +6.0f, 0.0f) : 255;
        const int g5Center = ok5 ? sampleG(rgba5,  0.0f, 0.0f) : 0;
        const bool p5Ok = ok5 && (draws5 == 1u) && (g5East < 115) && (g5Center >= 195);

        pointLightPass = p12Ok && p3Ok && p4Ok && p5Ok;
        printf("  v2.9.5 Multi-Light Point Shadows: P1/P2(E=%d W=%d Ctr=%d N=%d dr=%u)=%s | P3(ref=%d mix=%d diff=%d)=%s | P4(culledDr=%u)=%s | P5_Crisp2048(E=%d Ctr=%d)=%s\n",
               gEast, gWest, gCenter, gNorth, draws12, p12Ok ? "PASS" : "FAIL",
               gRef, gMix, abs(gMix - gRef), p3Ok ? "PASS" : "FAIL",
               draws4, p4Ok ? "PASS" : "FAIL",
               g5East, g5Center, p5Ok ? "PASS" : "FAIL");
    }

    // -------------------------------------------------------------------------
    // Case M3: True Planar Water Reflections (Dual-Backend Pixel-Exact + DEPTH16 Occlusion + Inland 0-Draw)
    // -------------------------------------------------------------------------
    bool waterReflPass = false;
    {
        hs_water_refl::WaterReflectionEngine weng;
        weng.SetOps(ops, env.isNativeGl);
        hs_water_refl::WaterReflConfig wcfg{};
        wcfg.strength    = 0.65f;
        wcfg.rippleAmp   = 0.55f;
        wcfg.lengthScale = 1.00f;

        const float kS = 0.70710678f;
        const float matV[16] = {
            1.0f,  0.0f,  0.0f,  0.0f,
            0.0f,  kS,    kS,    0.0f,
            0.0f, -kS,    kS,    0.0f,
            0.0f,  0.0f, -42.4264069f, 1.0f
        };
        weng.OnMatrixV(matV);

        // M3a) Inland frame (OnOceanDraw NOT called) -> MUST NOT begin write, 0 draws, 0 prev map!
        const bool inlandSkip = !weng.NeedsBeginWrite(primaryFbo, wcfg);
        weng.OnEndFrame();
        const bool inlandEmpty = !weng.HasPrevMap() && (weng.LastFrameReflDraws() == 0u);

        // M3b) Coastal frame: draw RED nearer boat (z=+1.5, h=0..3) FIRST, then GREEN farther tree (z=0.0, h=0..5) SECOND
        //      with POS2D_UV.x = 5.0 (shadowFlag=1 + 2*TallTree) to verify M3+M4 parity AND DEPTH16 occlusion!
        const int32_t m3Vp[4] = { 0, 0, 256, 256 };
        hs_gl_cache::ThreadState m3Cache;
        m3Cache.OnMakeCurrent(reinterpret_cast<void*>(0x1234));
        hs_world_shadow::ShadowPassState m3Saved{};
        m3Saved.dstFbo = primaryFbo;
        m3Saved.dstProg = 0u;
        std::memcpy(m3Saved.vp, m3Vp, sizeof(m3Vp));
        m3Saved.glCacheTrusted = true;
        ops.glEnable(hs_world_shadow::kGlStencilTest);
        weng.OnOceanDraw(primaryFbo, m3Vp);
        const bool coastalNeed = weng.NeedsBeginWrite(primaryFbo, wcfg);
        bool beginOk = weng.OnBeforeFirstStandingEntity(wcfg, m3Saved, m3Cache);
        int32_t stAfterBegin = -1;
        env.glGetIntegerv(hs_world_shadow::kGlStencilTest, &stAfterBegin);

        std::vector<hs_sprite_batch::BatchedVertex> rVerts;
        std::vector<uint16_t> rIdx;
        auto pushStandingQuad = [&](float cx, float cz, float halfW, float hTop, float mr, float mg, float mb, float packedFlag) {
            uint16_t b = static_cast<uint16_t>(rVerts.size());
            const float xs[4] = { cx - halfW, cx + halfW, cx + halfW, cx - halfW };
            const float ys[4] = { 0.0f, 0.0f, hTop, hTop };
            const float us[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
            const float vs[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
            for (int k = 0; k < 4; ++k) {
                hs_sprite_batch::BatchedVertex v{};
                v.x = packedFlag; v.y = 0.0f; v.u = us[k]; v.v = vs[k];
                v.wx = xs[k]; v.wy = ys[k]; v.wz = cz; v.slot = 0.0f;
                v.mr = mr; v.mg = mg; v.mb = mb; v.ma = 1.0f;
                v.ar = 0.0f; v.ag = 0.0f; v.ab = 0.0f; v.pad = xs[k] - cx;
                rVerts.push_back(v);
            }
            rIdx.push_back(b + 0); rIdx.push_back(b + 1); rIdx.push_back(b + 2);
            rIdx.push_back(b + 0); rIdx.push_back(b + 2); rIdx.push_back(b + 3);
        };
        // Boat at z=+1.5 (height 0..2.2 -> reflects z=+1.5 .. +3.45) drawn FIRST!
        pushStandingQuad(0.0f, +1.5f, 1.8f, 2.2f, 0.95f, 0.10f, 0.10f, 1.0f);
        // Farther tree at z=0.0 (height 0..5.0 -> reflects z=0.0 .. +4.29) drawn SECOND with packedFlag=5.0!
        pushStandingQuad(0.0f,  0.0f, 1.8f, 5.0f, 0.10f, 0.95f, 0.15f, 5.0f);

        ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, vbo);
        ops.glBufferData(hs_world_shadow::kGlArrayBuffer, static_cast<ptrdiff_t>(rVerts.size() * sizeof(hs_sprite_batch::BatchedVertex)), rVerts.data(), hs_world_shadow::kGlStaticDraw);
        ops.glBindBuffer(hs_world_shadow::kGlElementArrayBuffer, ibo);
        ops.glBufferData(hs_world_shadow::kGlElementArrayBuffer, static_cast<ptrdiff_t>(rIdx.size() * sizeof(uint16_t)), rIdx.data(), hs_world_shadow::kGlStaticDraw);
        ops.glActiveTexture(hs_world_shadow::kGlTexture0 + 6u);
        ops.glBindTexture(hs_world_shadow::kGlTexture2D, spriteTex);
        ops.glEnableVertexAttribArray(0u);
        ops.glVertexAttribPointer(0u, 4, hs_world_shadow::kGlFloat, 0, sizeof(hs_sprite_batch::BatchedVertex), reinterpret_cast<const void*>(offsetof(hs_sprite_batch::BatchedVertex, x)));
        ops.glEnableVertexAttribArray(5u);
        ops.glVertexAttribPointer(5u, 4, hs_world_shadow::kGlFloat, 0, sizeof(hs_sprite_batch::BatchedVertex), reinterpret_cast<const void*>(offsetof(hs_sprite_batch::BatchedVertex, mr)));
        ops.glEnableVertexAttribArray(6u);
        ops.glVertexAttribPointer(6u, 4, hs_world_shadow::kGlFloat, 0, sizeof(hs_sprite_batch::BatchedVertex), reinterpret_cast<const void*>(offsetof(hs_sprite_batch::BatchedVertex, ar)));
        ops.glEnableVertexAttribArray(7u);
        ops.glVertexAttribPointer(7u, 4, hs_world_shadow::kGlFloat, 0, sizeof(hs_sprite_batch::BatchedVertex), reinterpret_cast<const void*>(offsetof(hs_sprite_batch::BatchedVertex, wx)));

        weng.MirrorBatchedSprites(static_cast<uint32_t>(rIdx.size()), wcfg, m3Saved, m3Cache);
        int32_t stAfterMirror = -1;
        env.glGetIntegerv(hs_world_shadow::kGlStencilTest, &stAfterMirror);
        ops.glDisable(hs_world_shadow::kGlStencilTest);
        weng.OnEndFrame();

        // Verify sampling binding & read back the completed reflection texture!
        float outB[4]{}, outC[4]{};
        const bool bindOk = weng.BindForOceanSampling(wcfg, 0u, m3Cache, true, outB, outC);
        const uint32_t reflTex = weng.ReadTextureId();
        bool readOk = false;
        uint8_t pxBoat[4]{}, pxTreeOnly[4]{}, pxBehind[4]{};
        if (bindOk && reflTex != 0u && env.glReadPixels) {
            uint32_t readFbo = 0u;
            ops.glGenFramebuffers(1, &readFbo);
            ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, readFbo);
            ops.glFramebufferTexture2D(hs_world_shadow::kGlFramebuffer, hs_world_shadow::kGlColorAttachment0,
                                       hs_world_shadow::kGlTexture2D, reflTex, 0);
            auto readWorldPx = [&](float wx, float wz, uint8_t outPx[4]) {
                int rx = static_cast<int>(((wx - outB[0]) * outB[2] * 0.5f + 0.5f) * static_cast<float>(hs_water_refl::kReflRes));
                int ry = static_cast<int>(((wz - outB[1]) * outB[2] * 0.5f + 0.5f) * static_cast<float>(hs_water_refl::kReflRes));
                rx = std::clamp(rx, 0, static_cast<int>(hs_water_refl::kReflRes - 1u));
                ry = std::clamp(ry, 0, static_cast<int>(hs_water_refl::kReflRes - 1u));
                env.glReadPixels(rx, ry, 1, 1, hs_world_shadow::kGlRgba, hs_world_shadow::kGlUnsignedByte, outPx);
            };
            readWorldPx(0.0f, +2.3f, pxBoat);     // Overlap zone: nearer RED boat MUST win DEPTH16 over GREEN tree!
            readWorldPx(0.0f, +0.9f, pxTreeOnly); // Closer to tree base (behind boat): GREEN tree reflection visible!
            readWorldPx(0.0f, -3.5f, pxBehind);   // Inland behind tree: MUST be empty (A == 0)!
            ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, primaryFbo);
            ops.glDeleteFramebuffers(1, &readFbo);
            readOk = true;
        }

        // Next inland frame MUST clear hasPrevMap cleanly (zero stale coastal flash!)
        weng.OnEndFrame();
        const bool staleCleared = !weng.HasPrevMap();
        const bool glErrOk = checkNoErr("M3 WaterReflectionEngine");
        weng.ResetContext(true);

        const bool boatWon  = readOk && (pxBoat[0] > 180) && (pxBoat[1] < 60) && (pxBoat[3] > 80);
        const bool treeVis  = readOk && (pxTreeOnly[1] > 180) && (pxTreeOnly[0] < 60) && (pxTreeOnly[3] > 80);
        const bool emptyBck = readOk && (pxBehind[3] == 0);
        const bool stencilPreserved = (stAfterBegin == 1) && (stAfterMirror == 1);
        waterReflPass = inlandSkip && inlandEmpty && coastalNeed && beginOk && bindOk &&
                        boatWon && treeVis && emptyBck && staleCleared && stencilPreserved && glErrOk;
        printf("  M3 WaterReflectionEngine: inlandSkip=%d coastalDr=%u boat(RGB=%u,%u,%u A=%u) tree(RGB=%u,%u,%u A=%u) backA=%u staleClr=%d stPres=%d -> %s\n",
               (inlandSkip && inlandEmpty) ? 1 : 0, weng.LastFrameReflDraws(),
               pxBoat[0], pxBoat[1], pxBoat[2], pxBoat[3],
               pxTreeOnly[0], pxTreeOnly[1], pxTreeOnly[2], pxTreeOnly[3],
               pxBehind[3], staleCleared ? 1 : 0, stencilPreserved ? 1 : 0, waterReflPass ? "PASS" : "FAIL");
    }

    // Case A3 (v2.9.5-A Track A3-1): Real-Hardware Dual-Backend GLSL ES 1.00 Derivative Shaders
    //   Compiles & renders our exact GROUND micro-grain + ANIM high-DPI edge/ink shaders with
    //   GL_OES_standard_derivatives on BOTH Native OpenGL (#version 100) AND ANGLE D3D9!
    bool detailShaderPass = false;
    {
        auto compileProg = [&](const char* vsBody, const char* fsBody) -> uint32_t {
            std::string vsSrc = env.isNativeGl ? (std::string("#version 100\n") + vsBody) : std::string(vsBody);
            std::string fsSrc = env.isNativeGl ? (std::string("#version 100\n") + fsBody) : std::string(fsBody);
            auto mkSh = [&](uint32_t kind, const std::string& src) -> uint32_t {
                uint32_t sh = ops.glCreateShader(kind);
                const char* p = src.c_str();
                int32_t len = static_cast<int32_t>(src.size());
                ops.glShaderSource(sh, 1, &p, &len);
                ops.glCompileShader(sh);
                int32_t ok = 0;
                ops.glGetShaderiv(sh, hs_world_shadow::kGlCompileStatus, &ok);
                if (!ok) {
                    char log[512]{}; int32_t ll = 0;
                    ops.glGetShaderInfoLog(sh, 511, &ll, log);
                    printf("  [A3 SHADER ERR] kind=0x%X: %s\n", kind, log);
                    ops.glDeleteShader(sh);
                    return 0u;
                }
                return sh;
            };
            uint32_t vs = mkSh(hs_world_shadow::kGlVertexShader, vsSrc);
            uint32_t fs = mkSh(hs_world_shadow::kGlFragmentShader, fsSrc);
            if (!vs || !fs) { if (vs) ops.glDeleteShader(vs); if (fs) ops.glDeleteShader(fs); return 0u; }
            uint32_t prog = ops.glCreateProgram();
            ops.glAttachShader(prog, vs);
            ops.glAttachShader(prog, fs);
            ops.glLinkProgram(prog);
            ops.glDeleteShader(vs);
            ops.glDeleteShader(fs);
            int32_t lok = 0;
            ops.glGetProgramiv(prog, hs_world_shadow::kGlLinkStatus, &lok);
            if (!lok) {
                char log[512]{}; int32_t ll = 0;
                ops.glGetProgramInfoLog(prog, 511, &ll, log);
                printf("  [A3 LINK ERR]: %s\n", log);
                ops.glDeleteProgram(prog);
                return 0u;
            }
            return prog;
        };

        static constexpr const char* kDetVs =
            "attribute vec2 aPos;\n"
            "uniform vec4 uSpan;\n"
            "varying vec3 PS_POS;\n"
            "varying vec2 vUv;\n"
            "void main() {\n"
            "  vUv = aPos * 0.5 + 0.5;\n"
            "  PS_POS = vec3(vUv.x * uSpan.x, 0.0, vUv.y * uSpan.y);\n"
            "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
            "}\n";

        static constexpr const char* kGroundFs =
            "#ifdef GL_OES_standard_derivatives\n"
            "#extension GL_OES_standard_derivatives : enable\n"
            "#endif\n"
            "#if defined(GL_ES)\n"
            "precision highp float;\n"
            "#endif\n"
            "uniform vec4 HS_DETAIL_PARAMS;\n"
            "uniform vec4 uBaseCol;\n"
            "varying vec3 PS_POS;\n"
            "float hs_ghash(vec2 p) {\n"
            "  vec3 p3 = fract(vec3(p.xyx) * 0.1031);\n"
            "  p3 += dot(p3, p3.yzx + 33.33);\n"
            "  return fract((p3.x + p3.y) * p3.z);\n"
            "}\n"
            "float hs_gnoise(vec2 p) {\n"
            "  vec2 i = floor(p); vec2 f = fract(p);\n"
            "  vec2 u = f * f * (3.0 - 2.0 * f);\n"
            "  return mix(mix(hs_ghash(i), hs_ghash(i + vec2(1.0, 0.0)), u.x),\n"
            "             mix(hs_ghash(i + vec2(0.0, 1.0)), hs_ghash(i + vec2(1.0, 1.0)), u.x), u.y);\n"
            "}\n"
            "void main() {\n"
            "  vec4 base_colour = uBaseCol;\n"
            "  if (HS_DETAIL_PARAMS.x > 0.001) {\n"
            "    float hs_gd = clamp(HS_DETAIL_PARAMS.x, 0.0, 1.5);\n"
            "#ifdef GL_OES_standard_derivatives\n"
            "    float hs_wpix = length(fwidth(PS_POS.xz));\n"
            "#else\n"
            "    float hs_wpix = 0.045;\n"
            "#endif\n"
            "    float hs_w1 = 1.0 - smoothstep(0.055, 0.155, hs_wpix);\n"
            "    float hs_w2 = 1.0 - smoothstep(0.020, 0.058, hs_wpix);\n"
            "    if (hs_w1 > 0.005) {\n"
            "      vec2 hs_wp = mod(PS_POS.xz, 128.0);\n"
            "      vec2 hs_rp1 = vec2(hs_wp.x * 0.883 - hs_wp.y * 0.469, hs_wp.x * 0.469 + hs_wp.y * 0.883);\n"
            "      vec2 hs_rp2 = vec2(hs_wp.x * 0.469 + hs_wp.y * 0.883, -hs_wp.x * 0.883 + hs_wp.y * 0.469);\n"
            "      float hs_gn1 = hs_gnoise(hs_rp1 * vec2(3.4, 4.25)) - 0.5;\n"
            "      float hs_gn2 = (hs_w2 > 0.005) ? (hs_gnoise(hs_rp2 * vec2(10.8, 13.5)) - 0.5) : 0.0;\n"
            "      float hs_gtex = hs_gn1 * (0.62 * hs_w1) + hs_gn2 * (0.38 * hs_w2);\n"
            "      float hs_gl = dot(base_colour.rgb, vec3(0.299, 0.587, 0.114));\n"
            "      float hs_gm = smoothstep(0.018, 0.14, hs_gl) * (1.0 - 0.32 * smoothstep(0.72, 0.98, hs_gl));\n"
            "      base_colour.rgb = clamp(base_colour.rgb * (1.0 + hs_gtex * (0.16 * hs_gd * hs_gm)), vec3(0.0), vec3(1.0));\n"
            "    }\n"
            "  }\n"
            "  gl_FragColor = vec4(base_colour.rgb, 1.0);\n"
            "}\n";

        static constexpr const char* kAnimDetFs =
            "#ifdef GL_OES_standard_derivatives\n"
            "#extension GL_OES_standard_derivatives : enable\n"
            "#endif\n"
            "#if defined(GL_ES)\n"
            "precision highp float;\n"
            "#endif\n"
            "uniform vec4 HS_DETAIL_PARAMS;\n"
            "uniform vec4 uMode;\n" // x=lightOverride
            "varying vec2 vUv;\n"
            "void main() {\n"
            "  // Left half: steep 4px alpha silhouette at x=0.25; Right half: solid sprite with washed-out dark ink line at x=0.75\n"
            "  float a = (vUv.x < 0.5) ? clamp((vUv.x - 0.242) / 0.016, 0.0, 1.0) : 1.0;\n"
            "  float stripe = exp(-pow((vUv.x - 0.75) / 0.008, 2.0));\n"
            "  vec3 col = mix(vec3(0.72, 0.68, 0.60), vec3(0.16, 0.15, 0.14), stripe);\n"
            "  gl_FragColor = vec4(col * a, a);\n"
            "  float LIGHT_OVERRIDE = uMode.x;\n"
            "  if (HS_DETAIL_PARAMS.y > 0.001 || HS_DETAIL_PARAMS.z > 0.001) {\n"
            "    float hs_a = gl_FragColor.a;\n"
            "    float hs_effMask = step(0.08, hs_a) * (1.0 - smoothstep(0.05, 0.35, LIGHT_OVERRIDE));\n"
            "    float hs_aSafe = max(hs_a, 0.001);\n"
            "    vec3 hs_col = gl_FragColor.rgb / hs_aSafe;\n"
            "    float hs_luma = dot(hs_col, vec3(0.299, 0.587, 0.114));\n"
            "    vec3 hs_res = hs_col;\n"
            "    float hs_outA = hs_a;\n"
            "#ifdef GL_OES_standard_derivatives\n"
            "    float hs_fa = fwidth(hs_a);\n"
            "    float hs_lgrad = length(vec2(dFdx(hs_luma), dFdy(hs_luma)));\n"
            "    float hs_litGate = 1.0 - smoothstep(0.05, 0.28, LIGHT_OVERRIDE);\n"
            "    if (HS_DETAIL_PARAMS.y > 0.001 && hs_a > 0.005 && hs_a < 0.995) {\n"
            "      float hs_eMask = smoothstep(0.14, 0.44, hs_fa) * hs_litGate;\n"
            "      float hs_crisp = clamp((hs_a - 0.48) / max(hs_fa * 0.72, 0.22) + 0.50, 0.0, 1.0);\n"
            "      hs_outA = mix(hs_a, hs_crisp, clamp(clamp(HS_DETAIL_PARAMS.y, 0.0, 1.5) * 0.55 * hs_eMask, 0.0, 0.85));\n"
            "    }\n"
            "    if (HS_DETAIL_PARAMS.z > 0.001 && hs_a > 0.50) {\n"
            "      float hs_dInkStr = clamp(HS_DETAIL_PARAMS.z, 0.0, 1.5);\n"
            "      float hs_dStroke = smoothstep(0.045, 0.22, hs_lgrad) * smoothstep(0.015, 0.06, hs_luma) * (1.0 - smoothstep(0.12, 0.38, hs_luma));\n"
            "      hs_res *= (1.0 - 0.22 * hs_dInkStr * hs_dStroke);\n"
            "      float hs_solidMask = smoothstep(0.85, 0.98, hs_a) * (1.0 - smoothstep(0.18, 0.42, hs_fa));\n"
            "      float hs_bEdge = smoothstep(0.06, 0.26, hs_lgrad) * smoothstep(0.28, 0.65, hs_luma) * (1.0 - smoothstep(0.82, 0.98, hs_luma));\n"
            "      hs_res *= (1.0 + 0.075 * hs_dInkStr * hs_bEdge * hs_solidMask);\n"
            "    }\n"
            "#endif\n"
            "    vec3 hs_origScaled = (hs_a > 0.001) ? (gl_FragColor.rgb * (hs_outA / hs_a)) : vec3(0.0);\n"
            "    gl_FragColor.rgb = mix(hs_origScaled, min(max(hs_res, vec3(0.0)) * hs_outA, vec3(hs_outA)), hs_effMask);\n"
            "    gl_FragColor.a = hs_outA;\n"
            "  }\n"
            "}\n";

        uint32_t pGround = compileProg(kDetVs, kGroundFs);
        uint32_t pAnim   = compileProg(kDetVs, kAnimDetFs);
        bool gOk = false, aOk = false;
        if (pGround && pAnim && env.glReadPixels) {
            const float quad[8] = { -1,-1, 1,-1, -1,1, 1,1 };
            uint32_t qvbo = 0u;
            ops.glGenBuffers(1, &qvbo);
            ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, qvbo);
            ops.glBufferData(hs_world_shadow::kGlArrayBuffer, sizeof(quad), quad, hs_world_shadow::kGlStaticDraw);
            ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, primaryFbo);
            ops.glViewport(0, 0, 256, 256);
            ops.glDisable(hs_world_shadow::kGlBlend);
            ops.glDisable(hs_world_shadow::kGlDepthTest);

            auto renderAndRead = [&](uint32_t prog, const float span[4], const float det[4], const float extra[4], const char* extraName, std::vector<uint8_t>& out) {
                ops.glUseProgram(prog);
                int32_t lSpan = ops.glGetUniformLocation(prog, "uSpan");
                int32_t lDet  = ops.glGetUniformLocation(prog, "HS_DETAIL_PARAMS");
                int32_t lExt  = ops.glGetUniformLocation(prog, extraName);
                if (lSpan >= 0) ops.glUniform4fv(lSpan, 1, span);
                if (lDet >= 0)  ops.glUniform4fv(lDet,  1, det);
                if (lExt >= 0)  ops.glUniform4fv(lExt,  1, extra);
                ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, qvbo);
                ops.glEnableVertexAttribArray(0);
                ops.glVertexAttribPointer(0, 2, hs_world_shadow::kGlFloat, 0, 0, nullptr);
                ops.glDrawArrays(hs_world_shadow::kGlTriangleStrip, 0, 4);
                out.resize(256u * 256u * 4u);
                env.glReadPixels(0, 0, 256, 256, hs_world_shadow::kGlRgba, hs_world_shadow::kGlUnsignedByte, out.data());
            };

            auto calcStats = [](const std::vector<uint8_t>& img, double& outMean, double& outVar, int& outMax) {
                double sum = 0.0, sum2 = 0.0;
                int mx = 0;
                const int n = 200 * 200;
                for (int y = 28; y < 228; ++y) {
                    for (int x = 28; x < 228; ++x) {
                        int v = img[(static_cast<size_t>(y) * 256u + static_cast<size_t>(x)) * 4u];
                        sum += v; sum2 += static_cast<double>(v) * v;
                        if (v > mx) mx = v;
                    }
                }
                outMean = sum / n;
                outVar  = std::max(0.0, sum2 / n - outMean * outMean);
                outMax  = mx;
            };

            const float spanNear[4] = { 4.0f, 4.0f, 0, 0 };
            const float spanFar[4]  = { 64.0f, 64.0f, 0, 0 };
            const float detOff[4]   = { 0.0f, 0.0f, 0.0f, 0.0f };
            const float detG1[4]    = { 1.0f, 0.0f, 0.0f, 0.0f };
            const float baseMid[4]  = { 0.5f, 0.5f, 0.5f, 1.0f };
            const float baseBlk[4]  = { 0.0f, 0.0f, 0.0f, 1.0f };
            std::vector<uint8_t> gOff, gNear, gFar, gBlk;
            renderAndRead(pGround, spanNear, detOff, baseMid, "uBaseCol", gOff);
            renderAndRead(pGround, spanNear, detG1,  baseMid, "uBaseCol", gNear);
            renderAndRead(pGround, spanFar,  detG1,  baseMid, "uBaseCol", gFar);
            renderAndRead(pGround, spanNear, detG1,  baseBlk, "uBaseCol", gBlk);
            double mOff = 0, vOff = 0, mNear = 0, vNear = 0, mFar = 0, vFar = 0, mBlk = 0, vBlk = 0;
            int mxOff = 0, mxNear = 0, mxFar = 0, mxBlk = 0;
            calcStats(gOff, mOff, vOff, mxOff);
            calcStats(gNear, mNear, vNear, mxNear);
            calcStats(gFar, mFar, vFar, mxFar);
            calcStats(gBlk, mBlk, vBlk, mxBlk);
            gOk = (vOff < 0.01) && (vNear > 1.5) && (std::fabs(mNear - mOff) < 3.0) &&
                  (vFar < 0.05) && (mxBlk == 0);

            // ANIM edge crispness + premul invariant + ink deepening + LIGHT_OVERRIDE immunity
            const float detA1[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
            const float modeLit[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            const float modeGlow[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
            std::vector<uint8_t> aOff, aOn, aGlowOff, aGlowOn;
            renderAndRead(pAnim, spanNear, detOff, modeLit,  "uMode", aOff);
            renderAndRead(pAnim, spanNear, detA1,  modeLit,  "uMode", aOn);
            renderAndRead(pAnim, spanNear, detOff, modeGlow, "uMode", aGlowOff);
            renderAndRead(pAnim, spanNear, detA1,  modeGlow, "uMode", aGlowOn);

            bool premulOk = true;
            int edgeTightDelta = 0;
            int inkDarkDelta = 0;
            const int rowY = 128;
            for (int x = 0; x < 256; ++x) {
                const size_t off = (static_cast<size_t>(rowY) * 256u + static_cast<size_t>(x)) * 4u;
                if (aOn[off + 0] > aOn[off + 3] + 1 || aOn[off + 1] > aOn[off + 3] + 1 || aOn[off + 2] > aOn[off + 3] + 1) {
                    premulOk = false;
                }
                if (x >= 60 && x <= 68) {
                    edgeTightDelta += std::abs(static_cast<int>(aOn[off + 3]) - static_cast<int>(aOff[off + 3]));
                }
                if (x >= 189 && x <= 195) {
                    inkDarkDelta += static_cast<int>(aOff[off + 0]) - static_cast<int>(aOn[off + 0]);
                }
            }
            bool glowUntouched = (aGlowOff == aGlowOn);
            aOk = premulOk && (edgeTightDelta > 5) && (inkDarkDelta > 5) && glowUntouched;

            ops.glDeleteBuffers(1, &qvbo);
            printf("  A3-1 High-DPI Detail Shaders: ground(vOff=%.2f vNear=%.2f mean=%.1f vFar=%.3f caveMax=%d) anim(premul=%d edgeD=%d inkD=%d glowSame=%d) -> %s\n",
                   vOff, vNear, mNear, vFar, mxBlk, premulOk ? 1 : 0, edgeTightDelta, inkDarkDelta, glowUntouched ? 1 : 0,
                   (gOk && aOk) ? "PASS" : "FAIL");
        }
        if (pGround) ops.glDeleteProgram(pGround);
        if (pAnim)   ops.glDeleteProgram(pAnim);
        ops.glUseProgram(0u);
        detailShaderPass = gOk && aOk && checkNoErr("A3-1 Detail Shaders");
    }

    // Case B5 (v2.9.5-B Track B5): Single-DrawCall Procedural GPU Combat VFX Renderer
    //   Verifies 0-draw idle fast-path, live Heavy/Chop slash+sparks+ring rendering into PrimaryRT,
    //   non-black output pixels via glReadPixels, glGetError()==0, and 100% GL state restoration
    //   on BOTH Native OpenGL (#version 100) AND ANGLE D3D9!
    bool combatVfxPass = false;
    {
        hs_combat::CombatImpactEngine cEng;
        hs_combat::CombatVfxRenderer    cVfx;
        cVfx.SetOps(ops, env.isNativeGl);
        cEng.SetConfig(true, 1.0f, 1.0f, 1.0f, 1.2f);

        const int32_t vp[4] = { 0, 0, 256, 256 };
        // Simple column-major ortho P * V mapping world [-3,+3]^2 around (0,1,0) into clip space [-1,+1]^2
        const float orthoP[16] = {
            1.0f / 3.0f, 0, 0, 0,
            0, 1.0f / 3.0f, 0, 0,
            0, 0, -0.01f, 0,
            0, -1.0f / 3.0f, 0, 1.0f
        };
        const float idV[16] = {
            1, 0, 0, 0,
            0, 1, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
        };
        cVfx.OnMatrixP(orthoP);
        cVfx.OnMatrixV(idV);

        // Prepare PostProcess-style quad VBO (POSITION loc 0 + TEXCOORD0 loc 1) and leave BOTH attribs enabled,
        // simulating DST vfx_particle.ksh -> Klei RenderDevice::BindVertexFormat (0x384DC0) CPU bitmask cache!
        const float ppQuad[20] = {
            -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,
             1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
            -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,
             1.0f,  1.0f, 0.0f,  1.0f, 1.0f,
        };
        uint32_t ppVbo = 0u;
        ops.glGenBuffers(1, &ppVbo);
        ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, ppVbo);
        ops.glBufferData(hs_world_shadow::kGlArrayBuffer, sizeof(ppQuad), ppQuad, hs_world_shadow::kGlStaticDraw);
        ops.glEnableVertexAttribArray(0u);
        ops.glEnableVertexAttribArray(1u);
        ops.glVertexAttribPointer(0u, 3, hs_world_shadow::kGlFloat, 0u, 20, reinterpret_cast<const void*>(0));
        ops.glVertexAttribPointer(1u, 2, hs_world_shadow::kGlFloat, 0u, 20, reinterpret_cast<const void*>(12));

        // Compile tiny UV-visualizer PostProcess shader (loc 0 = POSITION, loc 1 = TEXCOORD0)
        uint32_t ppProg = 0u;
        {
            static constexpr const char* kPpVs =
                "attribute vec3 POSITION;\n"
                "attribute vec2 TEXCOORD0;\n"
                "varying vec2 vUv;\n"
                "void main() { vUv = TEXCOORD0; gl_Position = vec4(POSITION, 1.0); }\n";
            static constexpr const char* kPpFs =
                "#if defined(GL_ES)\nprecision mediump float;\n#endif\n"
                "varying vec2 vUv;\n"
                "void main() { gl_FragColor = vec4(vUv.x, vUv.y, 0.25, 1.0); }\n";
            std::string vsS = env.isNativeGl ? (std::string("#version 100\n") + kPpVs) : std::string(kPpVs);
            std::string fsS = env.isNativeGl ? (std::string("#version 100\n") + kPpFs) : std::string(kPpFs);
            uint32_t vs = ops.glCreateShader(hs_world_shadow::kGlVertexShader);
            uint32_t fs = ops.glCreateShader(hs_world_shadow::kGlFragmentShader);
            const char* vp0 = vsS.c_str(); int32_t vl0 = static_cast<int32_t>(vsS.size());
            const char* fp0 = fsS.c_str(); int32_t fl0 = static_cast<int32_t>(fsS.size());
            ops.glShaderSource(vs, 1, &vp0, &vl0); ops.glCompileShader(vs);
            ops.glShaderSource(fs, 1, &fp0, &fl0); ops.glCompileShader(fs);
            ppProg = ops.glCreateProgram();
            ops.glAttachShader(ppProg, vs);
            ops.glAttachShader(ppProg, fs);
            ops.glBindAttribLocation(ppProg, 0u, "POSITION");
            ops.glBindAttribLocation(ppProg, 1u, "TEXCOORD0");
            ops.glLinkProgram(ppProg);
            ops.glDeleteShader(vs);
            ops.glDeleteShader(fs);
        }

        hs_gl_cache::ThreadState b5Cache;
        b5Cache.OnMakeCurrent(reinterpret_cast<void*>(0x5678));
        hs_world_shadow::ShadowPassState b5Saved{};
        b5Saved.dstFbo        = primaryFbo;
        b5Saved.dstProg       = ppProg;
        b5Saved.boundArrayBuf = ppVbo;
        b5Saved.boundElemBuf  = ibo;
        b5Saved.attr0         = { ppVbo, 3, hs_world_shadow::kGlFloat, 0u, 20, reinterpret_cast<const void*>(0),  true, true };
        b5Saved.attr1         = { ppVbo, 2, hs_world_shadow::kGlFloat, 0u, 20, reinterpret_cast<const void*>(12), true, true };
        std::memcpy(b5Saved.vp, vp, sizeof(vp));
        b5Saved.glCacheTrusted = true;

        ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, primaryFbo);
        ops.glViewport(0, 0, 256, 256);
        ops.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        ops.glClear(hs_world_shadow::kGlColorBufferBit);
        ops.glEnable(hs_world_shadow::kGlDepthTest);
        ops.glDepthMask(1u);
        ops.glEnable(hs_world_shadow::kGlBlend);
        ops.glBlendFunc(hs_world_shadow::kGlSrcAlpha, hs_world_shadow::kGlOneMinusSrcAlpha);
        ops.glUseProgram(ppProg);
        ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, ppVbo);
        ops.glBindBuffer(hs_world_shadow::kGlElementArrayBuffer, ibo);

        const int64_t qpcFreq = 10000000LL;
        const double invFreq = 1.0 / static_cast<double>(qpcFreq);
        const int64_t t0 = 900000000LL;

        // Idle fast-path: zero active VFX -> zero draws
        cVfx.OnGroundOrOceanDraw(primaryFbo, vp);
        cVfx.OnStandingEntityDraw(primaryFbo);
        bool leaveArmed1 = cVfx.ShouldRenderOnLeaveFbo(primaryFbo, 0u);
        bool idleOk = cVfx.RenderIntoPrimaryRt(cEng, t0, invFreq, b5Saved, b5Cache) && (cVfx.TotalDraws() == 0u);
        cVfx.OnEndFrame();

        // Trigger Heavy + Chop bursts and render at t0 + 25ms into primaryFbo
        cEng.TriggerImpact(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.35f, hs_combat::ImpactStyle::Heavy, false, true, t0, qpcFreq);
        cEng.TriggerImpact(0.4f, 0.0f, 0.2f, -1.0f, 0.0f, 1.00f, hs_combat::ImpactStyle::Chop, false, true, t0, qpcFreq);
        cVfx.OnGroundOrOceanDraw(primaryFbo, vp);
        cVfx.OnStandingEntityDraw(primaryFbo);
        bool leaveArmed2 = cVfx.ShouldRenderOnLeaveFbo(primaryFbo, 0u);
        const int64_t t25ms = t0 + static_cast<int64_t>(0.025 * qpcFreq);
        bool drawOk = cVfx.RenderIntoPrimaryRt(cEng, t25ms, invFreq, b5Saved, b5Cache);

        int32_t stDepth = -1, stDMask = -1, stBlendSrc = -1, stProg = -1, stArrBuf = -1;
        if (env.glGetIntegerv) {
            env.glGetIntegerv(hs_world_shadow::kGlDepthTest, &stDepth);
            env.glGetIntegerv(hs_world_shadow::kGlDepthWritemask, &stDMask);
            env.glGetIntegerv(hs_world_shadow::kGlBlendSrcRgb, &stBlendSrc);
            env.glGetIntegerv(hs_world_shadow::kGlCurrentProgram, &stProg);
            env.glGetIntegerv(0x8894u /*GL_ARRAY_BUFFER_BINDING*/, &stArrBuf);
        }
        bool stateOk = (stDepth == 1) && (stDMask == 1) &&
                       (stBlendSrc == static_cast<int32_t>(hs_world_shadow::kGlSrcAlpha)) &&
                       (stProg == static_cast<int32_t>(ppProg)) && (stArrBuf == static_cast<int32_t>(ppVbo));

        std::vector<uint8_t> pix(256u * 256u * 4u, 0u);
        env.glReadPixels(0, 0, 256, 256, 0x1908u /*GL_RGBA*/, 0x1401u /*GL_UNSIGNED_BYTE*/, pix.data());
        int litPx = 0;
        for (size_t i = 0; i < pix.size(); i += 4u) {
            if (pix[i + 0] > 18u || pix[i + 1] > 18u || pix[i + 2] > 18u) {
                ++litPx;
            }
        }

        // CRITICAL REGRESSION CHECK (White-Screen Flash Bug):
        // Immediately draw PostProcess fullscreen quad WITHOUT re-calling glEnableVertexAttribArray(0/1)
        // or glVertexAttribPointer(0/1)! Both attrib 0 & attrib 1 (TEXCOORD0) MUST still be enabled and
        // bound to ppVbo, producing a smooth (0,0)->(255,255) UV gradient instead of flat solid color!
        ops.glDisable(hs_world_shadow::kGlBlend);
        ops.glDisable(hs_world_shadow::kGlDepthTest);
        ops.glClear(hs_world_shadow::kGlColorBufferBit);
        ops.glDrawArrays(hs_world_shadow::kGlTriangleStrip, 0, 4);
        uint8_t pxBL[4]{}, pxTR[4]{};
        env.glReadPixels(16, 16, 1, 1, 0x1908u, 0x1401u, pxBL);
        env.glReadPixels(240, 240, 1, 1, 0x1908u, 0x1401u, pxTR);
        bool attribRestoreOk = (pxBL[0] < 32u && pxBL[1] < 32u && pxTR[0] > 220u && pxTR[1] > 220u);

        ops.glDisableVertexAttribArray(1u);
        ops.glUseProgram(0u);
        ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, vbo);
        if (ppProg) ops.glDeleteProgram(ppProg);
        if (ppVbo)  ops.glDeleteBuffers(1, &ppVbo);

        combatVfxPass = leaveArmed1 && idleOk && leaveArmed2 && drawOk &&
                        (cVfx.TotalDraws() == 1u) && (cVfx.LastDrawVerts() > 60u) &&
                        (litPx > 20) && stateOk && attribRestoreOk && checkNoErr("B5 CombatVfxRenderer");
        printf("  B5 CombatVfxRenderer: draws=%u verts=%u litPx=%d stateOk=%d attribRestore=%d(BL=%u,%u TR=%u,%u) -> %s\n",
               cVfx.TotalDraws(), cVfx.LastDrawVerts(), litPx, stateOk ? 1 : 0, attribRestoreOk ? 1 : 0,
               pxBL[0], pxBL[1], pxTR[0], pxTR[1], combatVfxPass ? "PASS" : "FAIL");
        cVfx.ResetContext(true);
    }

    // Case F1 (v2.9.5-F): Dual-Backend Real-GPU Verification for PBR Ocean Optics (OCEAN_COMB)
    //   + 2D Paper Wave Hide (WAVES_VS) + WaterReflectionEngine CamEye Extraction!
    //   CRITICAL: Compiles the EXACT full OCEAN_COMB fragment shader (vanilla + caustics + SIMD analytic PBR
    //   waves/sky/Mie + M3 planar refl) on BOTH RTX Native OpenGL (#version 100) AND ANGLE 1.0 D3D9 (ps_3_0)!
    bool oceanPbrPass = false;
    {
        auto compileProgF1 = [&](const char* vsBody, const char* fsBody) -> uint32_t {
            std::string vsSrc = env.isNativeGl ? (std::string("#version 100\n") + vsBody) : std::string(vsBody);
            std::string fsSrc = env.isNativeGl ? (std::string("#version 100\n") + fsBody) : std::string(fsBody);
            auto mkSh = [&](uint32_t kind, const std::string& src) -> uint32_t {
                uint32_t sh = ops.glCreateShader(kind);
                const char* p = src.c_str();
                int32_t len = static_cast<int32_t>(src.size());
                ops.glShaderSource(sh, 1, &p, &len);
                ops.glCompileShader(sh);
                int32_t ok = 0;
                ops.glGetShaderiv(sh, hs_world_shadow::kGlCompileStatus, &ok);
                if (!ok) {
                    char log[512]{}; int32_t ll = 0;
                    ops.glGetShaderInfoLog(sh, 511, &ll, log);
                    printf("  [F1 SHADER ERR] kind=0x%X: %s\n", kind, log);
                    ops.glDeleteShader(sh);
                    return 0u;
                }
                return sh;
            };
            uint32_t vs = mkSh(hs_world_shadow::kGlVertexShader, vsSrc);
            uint32_t fs = mkSh(hs_world_shadow::kGlFragmentShader, fsSrc);
            if (!vs || !fs) { if (vs) ops.glDeleteShader(vs); if (fs) ops.glDeleteShader(fs); return 0u; }
            uint32_t prog = ops.glCreateProgram();
            ops.glAttachShader(prog, vs);
            ops.glAttachShader(prog, fs);
            ops.glLinkProgram(prog);
            ops.glDeleteShader(vs);
            ops.glDeleteShader(fs);
            int32_t lok = 0;
            ops.glGetProgramiv(prog, hs_world_shadow::kGlLinkStatus, &lok);
            if (!lok) {
                char log[512]{}; int32_t ll = 0;
                ops.glGetProgramInfoLog(prog, 511, &ll, log);
                printf("  [F1 LINK ERR]: %s\n", log);
                ops.glDeleteProgram(prog);
                return 0u;
            }
            return prog;
        };

        static constexpr const char* kOceanVs =
            "attribute vec2 aPos;\n"
            "varying vec3 PS_POS;\n"
            "void main() {\n"
            "  vec2 uv = aPos * 0.5 + 0.5;\n"
            "  PS_POS = vec3((uv.x - 0.5) * 40.0, 0.0, (uv.y - 0.5) * 40.0);\n"
            "  gl_Position = vec4(aPos, 0.0, 1.0);\n"
            "}\n";

        static constexpr const char* kOceanPbrFs =
            "#if defined(GL_ES)\n"
            "precision highp float;\n"
            "#endif\n"
            "varying vec3 PS_POS;\n"
            "uniform vec4 OCEAN_UV0_PARAMS;\n"
            "uniform vec4 HS_HD2D_PARAMS;\n"
            "uniform vec4 HS_HD2D_SHADE2;\n"
            "uniform vec4 HS_HD2D_SHAD_PROJ;\n"
            "uniform vec4 HS_HD2D_OCEAN;\n"
            "uniform vec4 HS_OCEAN_PBR;\n"
            "uniform vec4 HS_OCEAN_EYE;\n"
            "uniform sampler2D HS_REFL_TEX;\n"
            "uniform vec4 HS_REFL_BOUNDS;\n"
            "uniform vec4 HS_REFL_CFG;\n"
            "vec4 hs_ohash4(vec2 i) {\n"
            "  vec4 x = fract(vec4(i.x, i.x + 1.0, i.x, i.x + 1.0) * 0.1031);\n"
            "  vec4 y = fract(vec4(i.y, i.y, i.y + 1.0, i.y + 1.0) * 0.1031);\n"
            "  vec4 dp = x * (y + 33.33) + y * (x + 33.33) + x * (x + 33.33);\n"
            "  return fract((x + y + 2.0 * dp) * (x + dp));\n"
            "}\n"
            "float hs_onoise(vec2 p) {\n"
            "  vec2 i = floor(p); vec2 f = fract(p);\n"
            "  vec2 u = f * f * (3.0 - 2.0 * f);\n"
            "  vec4 h = hs_ohash4(i);\n"
            "  vec2 m = mix(h.xz, h.yw, u.x);\n"
            "  return mix(m.x, m.y, u.y);\n"
            "}\n"
            "vec3 hs_onoise_d(vec2 p) {\n"
            "  vec2 i = floor(p); vec2 f = fract(p);\n"
            "  vec2 u = f * f * (3.0 - 2.0 * f);\n"
            "  vec2 du = 6.0 * f * (1.0 - f);\n"
            "  vec4 h = hs_ohash4(i);\n"
            "  float k1 = h.y - h.x; float k2 = h.z - h.x; float k3 = h.x - h.y - h.z + h.w;\n"
            "  return vec3(h.x + k1 * u.x + k2 * u.y + k3 * u.x * u.y, du * vec2(k1 + k3 * u.y, k2 + k3 * u.x));\n"
            "}\n"
            "float hs_omie(float cth, float g) {\n"
            "  float g2 = g * g; float d = max(1.0 + g2 - 2.0 * g * cth, 0.004);\n"
            "  return (1.0 - g2) / (12.56637 * d * sqrt(d));\n"
            "}\n"
            "float hs_caustic_web(vec2 p, vec2 drift) {\n"
            "  vec2 w = vec2(hs_onoise(p * 0.45 + drift * 0.35), hs_onoise(p * 0.45 - drift * 0.28 + vec2(7.3, 4.1)));\n"
            "  vec2 q = p + (w - 0.5) * 1.45;\n"
            "  float n1 = abs(hs_onoise(q + drift) * 2.0 - 1.0);\n"
            "  float n2 = abs(hs_onoise(q * 1.37 - drift * 0.85 + vec2(3.7, 9.2)) * 2.0 - 1.0);\n"
            "  float ridge = (1.0 - n1) * (1.0 - n2);\n"
            "  return clamp(ridge * ridge, 0.0, 1.0);\n"
            "}\n"
            "void main() {\n"
            "  vec4 base_colour = vec4(0.14, 0.32, 0.42, 1.0);\n"
            "  float noise0 = 0.4, noise1 = 0.5, noise2 = 0.6;\n"
            "  float noise_opacity = 0.65;\n"
            "  vec3 hs_lm_c = vec3(1.0, 1.0, 1.0);\n"
            "  vec4 colour = vec4(base_colour.rgb * hs_lm_c, 1.0);\n"
            "  if (HS_HD2D_PARAMS.y > 0.001) {\n"
            "    float hs_s = min(HS_HD2D_PARAMS.y, 1.25);\n"
            "    float hs_lmLuma = dot(hs_lm_c, vec3(0.299, 0.587, 0.114));\n"
            "    float hs_shallow = clamp(1.0 - noise_opacity, 0.0, 1.0);\n"
            "    colour.rgb += vec3(0.008, 0.022, 0.026) * (hs_shallow * 0.85 * hs_s * hs_lmLuma);\n"
            "  }\n"
            "  if (HS_HD2D_OCEAN.x > 0.001) {\n"
            "    float hs_gstr = clamp(HS_HD2D_OCEAN.x, 0.0, 2.0);\n"
            "    float hs_gden = clamp(HS_HD2D_OCEAN.y, 0.50, 0.998);\n"
            "    float hs_ggrn = clamp(HS_HD2D_OCEAN.z, 0.35, 2.50);\n"
            "    float hs_gsft = clamp(HS_HD2D_OCEAN.w, 0.0, 1.0);\n"
            "    float hs_sh = clamp(1.0 - noise_opacity, 0.0, 1.0);\n"
            "    vec2 hs_drift = OCEAN_UV0_PARAMS.xy * 18.0;\n"
            "    vec2 hs_wp = PS_POS.xz * (0.42 * hs_ggrn);\n"
            "    vec2 hs_stretch = mix(vec2(1.0, 1.0), vec2(1.35, 0.65), noise_opacity);\n"
            "    float hs_web = hs_caustic_web(hs_wp * hs_stretch, hs_drift);\n"
            "    float hs_gate = smoothstep(0.22, 0.78, hs_onoise(PS_POS.xz * 0.09 + hs_drift * 0.15));\n"
            "    float hs_pow = mix(3.8, 1.4, (hs_gden - 0.50) / 0.498);\n"
            "    float hs_glint = pow(hs_web, hs_pow) * (0.45 + 0.55 * hs_gate);\n"
            "    float hs_sand = hs_onoise(PS_POS.xz * 0.25) * 0.55 + hs_onoise(PS_POS.xz * 1.15) * 0.45;\n"
            "    vec3 hs_sand_col = mix(vec3(0.18, 0.32, 0.30), vec3(0.42, 0.48, 0.36), hs_sand);\n"
            "    vec3 hs_caustic_col = mix(vec3(0.45, 0.85, 0.88), vec3(1.00, 0.90, 0.62), hs_sh);\n"
            "    vec3 hs_blended_col = mix(hs_caustic_col, mix(hs_caustic_col, colour.rgb * 1.6 + vec3(0.12, 0.20, 0.18), 0.45), hs_gsft);\n"
            "    float hs_depth_fade = mix(1.0, 0.32, smoothstep(0.15, 0.92, noise_opacity));\n"
            "    vec3 hs_lm_scale = hs_lm_c * (0.55 + 0.45 * clamp(dot(hs_lm_c, vec3(0.3, 0.6, 0.1)), 0.0, 1.0));\n"
            "    colour.rgb += hs_sand_col * (hs_sh * hs_sh * 0.16 * hs_gstr * hs_gsft) * hs_lm_c;\n"
            "    colour.rgb += hs_blended_col * (hs_glint * 0.42 * hs_gstr * hs_depth_fade) * hs_lm_scale;\n"
            "  }\n"
            "  if (HS_OCEAN_PBR.x > 0.001 || HS_OCEAN_PBR.y > 0.001 || HS_OCEAN_PBR.z > 0.001) {\n"
            "    float hs_pbr  = clamp(HS_OCEAN_PBR.x, 0.0, 1.5);\n"
            "    float hs_sky  = clamp(HS_OCEAN_PBR.y, 0.0, 1.5);\n"
            "    float hs_mieS = clamp(HS_OCEAN_PBR.z, 0.0, 1.5);\n"
            "    float hs_ot   = HS_OCEAN_EYE.w;\n"
            "    vec3  hs_eye  = vec3(HS_OCEAN_EYE.x, max(HS_OCEAN_EYE.y, 6.0), HS_OCEAN_EYE.z);\n"
            "    vec3  hs_V    = normalize(hs_eye - PS_POS);\n"
            "    float hs_elev = clamp(HS_HD2D_SHADE2.z, 0.22, 1.25);\n"
            "    vec2  hs_s2   = normalize(-HS_HD2D_SHAD_PROJ.xy + vec2(1e-5, 0.0));\n"
            "    vec3  hs_L    = normalize(vec3(hs_s2.x * cos(hs_elev), sin(hs_elev), hs_s2.y * cos(hs_elev)));\n"
            "    float hs_day  = clamp(HS_HD2D_SHADE2.w, 0.0, 1.0);\n"
            "    if (hs_pbr > 0.001) {\n"
            "      float hs_sh0 = clamp(1.0 - noise_opacity, 0.0, 1.0);\n"
            "      float hs_dp0 = smoothstep(0.18, 0.92, noise_opacity);\n"
            "      vec3 hs_bm = mix(vec3(0.92, 1.12, 1.04), vec3(0.84, 0.94, 1.22), hs_dp0);\n"
            "      colour.rgb = mix(colour.rgb, colour.rgb * hs_bm, clamp(hs_pbr * 0.55, 0.0, 0.85));\n"
            "      colour.rgb += vec3(0.014, 0.036, 0.034) * (hs_sh0 * hs_sh0 * hs_pbr) * hs_lm_c;\n"
            "    }\n"
            "    float hs_pAmp = max(hs_pbr, max(hs_sky, hs_mieS) * 0.55);\n"
            "    vec2  hs_sp   = PS_POS.xz * 0.22;\n"
            "    vec3  hs_w1   = hs_onoise_d(hs_sp + vec2(hs_ot * 0.115, hs_ot * 0.048));\n"
            "    vec2  hs_pp   = hs_sp - (hs_V.xz / max(hs_V.y, 0.18)) * ((hs_w1.x - 0.5) * 0.14 * hs_pAmp);\n"
            "    vec3  hs_w2   = hs_onoise_d(hs_pp * 1.95 + vec2(-hs_ot * 0.085, hs_ot * 0.125) + vec2(3.1, 1.7));\n"
            "    vec3  hs_w3   = hs_onoise_d(hs_pp * 4.35 + vec2(hs_ot * 0.22, -hs_ot * 0.17) + vec2(7.4, 5.2));\n"
            "    vec2  hs_gLo  = hs_w1.yz * 0.55 + hs_w2.yz * 0.45;\n"
            "    vec2  hs_gHi  = hs_gLo * 0.65 + hs_w3.yz * 0.55;\n"
            "    vec3  hs_N    = normalize(vec3(-hs_gLo.x * 0.16 * hs_pAmp, 1.0, -hs_gLo.y * 0.16 * hs_pAmp));\n"
            "    vec3  hs_Nd   = normalize(vec3(-hs_gLo.x * 0.72 * hs_pAmp, 1.0, -hs_gLo.y * 0.72 * hs_pAmp));\n"
            "    float hs_NdV  = clamp(dot(hs_V, hs_Nd), 0.0, 1.0);\n"
            "    float hs_F    = 0.02 + 0.98 * pow(1.0 - hs_NdV, 5.0);\n"
            "    if (hs_pbr > 0.001) {\n"
            "      float hs_NdL = clamp(dot(hs_Nd, hs_L), 0.0, 1.0);\n"
            "      float hs_wDiff = mix(-0.12, 0.34, hs_NdL) * (1.0 - hs_F) * hs_pbr;\n"
            "      colour.rgb = max(colour.rgb + vec3(0.20, 0.34, 0.40) * (hs_wDiff * (0.28 + 0.72 * hs_day)) * hs_lm_c, vec3(0.0));\n"
            "    }\n"
            "    if (hs_sky > 0.001 || hs_mieS > 0.001) {\n"
            "      vec3  hs_R = reflect(-hs_V, hs_N);\n"
            "      vec2  hs_skyUv = hs_R.xz / max(hs_R.y, 0.25);\n"
            "      vec2  hs_cOff = vec2(hs_ot * 0.020, hs_ot * 0.007);\n"
            "      float hs_cNoise = hs_onoise(hs_skyUv * 1.40 + hs_cOff) * 0.65 + hs_onoise(hs_skyUv * 2.95 + hs_cOff * 1.6 + vec2(4.2, 2.8)) * 0.35;\n"
            "      float hs_skyC = smoothstep(0.26, 0.78, hs_cNoise);\n"
            "      vec3  hs_Ng  = normalize(vec3(-hs_gHi.x * 0.32 * hs_pAmp, 1.0, -hs_gHi.y * 0.32 * hs_pAmp));\n"
            "      vec3  hs_Rg  = reflect(-hs_V, hs_Ng);\n"
            "      float hs_mie = hs_omie(max(dot(hs_Rg, hs_L), 0.0), 0.90) * (0.14 * hs_mieS * hs_day);\n"
            "      float hs_fBoost = 0.55 + 1.45 * hs_F;\n"
            "      vec3  hs_cloudCol = mix(vec3(0.26, 0.34, 0.44), vec3(0.92, 0.95, 0.98), hs_skyC);\n"
            "      colour.rgb += hs_cloudCol * (hs_sky * 0.20 * (0.25 + 0.75 * hs_skyC) * hs_fBoost) * hs_lm_c;\n"
            "      vec3  hs_sunCol = mix(vec3(0.74, 0.86, 1.00), vec3(1.00, 0.92, 0.74), clamp(hs_elev * 1.2, 0.0, 1.0));\n"
            "      colour.rgb += hs_sunCol * (min(hs_mie, 2.2) * 0.38 * hs_fBoost) * hs_lm_c;\n"
            "    }\n"
            "  }\n"
            "  if (HS_REFL_CFG.x > 0.001 && HS_REFL_BOUNDS.w > 0.5) {\n"
            "    vec2 hs_ruv0 = (PS_POS.xz - HS_REFL_BOUNDS.xy) * (HS_REFL_BOUNDS.z * 0.5) + 0.5;\n"
            "    vec2 hs_redge = abs(hs_ruv0 * 2.0 - 1.0);\n"
            "    float hs_rin = 1.0 - smoothstep(0.88, 1.0, max(hs_redge.x, hs_redge.y));\n"
            "    if (hs_rin > 0.001) {\n"
            "      vec2 hs_rwave = vec2((noise0 - noise1), (noise1 - noise2)) * (0.011 * clamp(HS_REFL_CFG.y, 0.0, 1.5));\n"
            "      vec4 hs_rc = texture2D(HS_REFL_TEX, clamp(hs_ruv0 + hs_rwave, vec2(0.002), vec2(0.998)));\n"
            "      float hs_rmask = hs_rc.a * hs_rin * smoothstep(0.04, 0.36, base_colour.a + noise_opacity);\n"
            "      float hs_rstr = clamp(HS_REFL_CFG.x, 0.0, 1.2) * 0.58 * hs_rmask;\n"
            "      vec3 hs_rcol = hs_rc.rgb * hs_lm_c * vec3(0.84, 0.94, 1.04);\n"
            "      colour.rgb = mix(colour.rgb, hs_rcol, clamp(hs_rstr, 0.0, 0.72));\n"
            "    }\n"
            "  }\n"
            "  gl_FragColor = vec4(clamp(colour.rgb, vec3(0.0), vec3(1.0)), 1.0);\n"
            "}\n";

        static constexpr const char* kWaveClipVs =
            "uniform vec4 HS_OCEAN_PBR;\n"
            "attribute vec2 aPos;\n"
            "void main() {\n"
            "  gl_Position = vec4(aPos * 0.5, 0.0, 1.0);\n"
            "  if (HS_OCEAN_PBR.w > 0.5) { gl_Position = vec4(2.0, 2.0, 2.0, 1.0); }\n"
            "}\n";
        static constexpr const char* kWaveClipFs =
            "#if defined(GL_ES)\n"
            "precision mediump float;\n"
            "#endif\n"
            "void main() { gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0); }\n";

        uint32_t pOc = compileProgF1(kOceanVs, kOceanPbrFs);
        uint32_t pWv = compileProgF1(kWaveClipVs, kWaveClipFs);
        bool ocOk = false, wvOk = false, eyeOk = false;

        // Verify WaterReflectionEngine::OnMatrixV extracts exact camera world eye!
        {
            hs_water_refl::WaterReflectionEngine tmpW;
            const float kS = 0.70710678f;
            const float matV[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, kS,   kS,   0.0f,
                0.0f,-kS,   kS,   0.0f,
                0.0f, 0.0f,-42.4264069f, 1.0f
            };
            tmpW.OnMatrixV(matV);
            // Eye = -R^T * t -> x=0, y=+30, z=+30
            eyeOk = tmpW.HasCamEye() &&
                    fabsf(tmpW.CamEyeX() - 0.0f) < 0.05f &&
                    fabsf(tmpW.CamEyeY() - 30.0f) < 0.05f &&
                    fabsf(tmpW.CamEyeZ() - 30.0f) < 0.05f;
        }

        if (pOc && pWv && env.glReadPixels) {
            const float quad[8] = { -1,-1, 1,-1, -1,1, 1,1 };
            uint32_t qvbo = 0u;
            ops.glGenBuffers(1, &qvbo);
            ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, qvbo);
            ops.glBufferData(hs_world_shadow::kGlArrayBuffer, sizeof(quad), quad, hs_world_shadow::kGlStaticDraw);
            ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, primaryFbo);
            ops.glViewport(0, 0, 256, 256);
            ops.glDisable(hs_world_shadow::kGlBlend);
            ops.glDisable(hs_world_shadow::kGlDepthTest);

            auto drawOcean = [&](const float pbr[4], std::vector<uint8_t>& out) {
                ops.glUseProgram(pOc);
                const float z4[4] = { 0, 0, 0, 0 };
                const float sh2[4] = { 0.5f, 0.785f, 0.62f, 1.0f };
                const float proj[4] = { 0.0f, 1.0f, 1.4f, 1.0f }; // Sun at -Z so reflection toward +Z camera catches Mie glint!
                const float eye[4] = { 0.0f, 24.0f, 32.0f, 4.5f };
                auto set4 = [&](const char* n, const float v[4]) {
                    int32_t l = ops.glGetUniformLocation(pOc, n);
                    if (l >= 0) ops.glUniform4fv(l, 1, v);
                };
                set4("OCEAN_UV0_PARAMS", z4);
                set4("HS_HD2D_PARAMS", z4);
                set4("HS_HD2D_OCEAN", z4);
                set4("HS_HD2D_SHADE2", sh2);
                set4("HS_HD2D_SHAD_PROJ", proj);
                set4("HS_OCEAN_PBR", pbr);
                set4("HS_OCEAN_EYE", eye);
                set4("HS_REFL_BOUNDS", z4);
                set4("HS_REFL_CFG", z4);
                ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, qvbo);
                ops.glEnableVertexAttribArray(0);
                ops.glVertexAttribPointer(0, 2, hs_world_shadow::kGlFloat, 0, 0, nullptr);
                ops.glDrawArrays(hs_world_shadow::kGlTriangleStrip, 0, 4);
                out.resize(256u * 256u * 4u);
                env.glReadPixels(0, 0, 256, 256, hs_world_shadow::kGlRgba, hs_world_shadow::kGlUnsignedByte, out.data());
            };

            const float pbrOff[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            const float pbrOn[4]  = { 1.0f, 1.0f, 1.0f, 0.0f };
            std::vector<uint8_t> imgOff, imgOn;
            drawOcean(pbrOff, imgOff);
            drawOcean(pbrOn,  imgOn);

            int maxBaseErr = 0;
            double sumB = 0.0, sumB2 = 0.0;
            int maxBright = 0;
            for (size_t i = 0; i < 256u * 256u; ++i) {
                int r0 = imgOff[i * 4 + 0], g0 = imgOff[i * 4 + 1], b0 = imgOff[i * 4 + 2];
                maxBaseErr = std::max(maxBaseErr, std::max({ abs(r0 - 36), abs(g0 - 82), abs(b0 - 107) }));
                int b1 = imgOn[i * 4 + 2];
                sumB += b1; sumB2 += static_cast<double>(b1) * b1;
                maxBright = std::max(maxBright, static_cast<int>(imgOn[i * 4 + 0]) + imgOn[i * 4 + 1] + imgOn[i * 4 + 2]);
            }
            const double meanB = sumB / (256.0 * 256.0);
            const double varB  = std::max(0.0, sumB2 / (256.0 * 256.0) - meanB * meanB);
            ocOk = (maxBaseErr <= 1) && (meanB > 120.0) && (varB > 25.0) && (maxBright > 420);

            // Verify WAVES_VS paper wave hide toggle (0.0 draws pixels, 1.0 clips 100% to 0 pixels!)
            auto drawWaves = [&](float hideFlag) -> int {
                ops.glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                ops.glClear(hs_world_shadow::kGlColorBufferBit);
                ops.glUseProgram(pWv);
                const float wv[4] = { 0, 0, 0, hideFlag };
                int32_t l = ops.glGetUniformLocation(pWv, "HS_OCEAN_PBR");
                if (l >= 0) ops.glUniform4fv(l, 1, wv);
                ops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, qvbo);
                ops.glEnableVertexAttribArray(0);
                ops.glVertexAttribPointer(0, 2, hs_world_shadow::kGlFloat, 0, 0, nullptr);
                ops.glDrawArrays(hs_world_shadow::kGlTriangleStrip, 0, 4);
                std::vector<uint8_t> buf(256u * 256u * 4u);
                env.glReadPixels(0, 0, 256, 256, hs_world_shadow::kGlRgba, hs_world_shadow::kGlUnsignedByte, buf.data());
                int lit = 0;
                for (size_t i = 0; i < 256u * 256u; ++i) if (buf[i * 4] > 200) ++lit;
                return lit;
            };
            const int wPxShown  = drawWaves(0.0f);
            const int wPxHidden = drawWaves(1.0f);
            wvOk = (wPxShown > 10000) && (wPxHidden == 0);
            ops.glDeleteBuffers(1, &qvbo);
            printf("  F1 PBR Ocean (%s): baseErr=%d meanB=%.1f varB=%.1f maxSum=%d waves=%d->%d camEye=%d -> %s\n",
                   env.isNativeGl ? "GL4.6" : "ANGLE_ps30", maxBaseErr, meanB, varB, maxBright,
                   wPxShown, wPxHidden, eyeOk ? 1 : 0, (ocOk && wvOk && eyeOk) ? "PASS" : "FAIL");
        } else {
            printf("  F1 PBR Ocean: compile/link FAILED (pOc=%u pWv=%u) -> FAIL\n", pOc, pWv);
        }
        if (pOc) ops.glDeleteProgram(pOc);
        if (pWv) ops.glDeleteProgram(pWv);
        ops.glUseProgram(0u);
        oceanPbrPass = ocOk && wvOk && eyeOk && checkNoErr("F1 PBR Ocean");
    }

    bool pass = (pxF1 > 400) && (minGF1 < 120) && (minGF1 > 60) && (ogF1 == 0) && aF1 &&
                (abs(minGF4 - minGF1) <= 2) && (abs(pxF4 - pxF1) <= 10) &&
                (pxS1 > pxF1 * 0.70) && aS1 && entityRecvPass && statePreservePass && bufParamPass && pointLightPass && waterReflPass && detailShaderPass && combatVfxPass && oceanPbrPass;

    ops.glDeleteBuffers(1, &vbo);
    ops.glDeleteBuffers(1, &ibo);
    ops.glDeleteTextures(1, &spriteTex);
    ops.glDeleteTextures(1, &primaryTex);
    ops.glDeleteFramebuffers(1, &primaryFbo);
    printf("  ==> %s: %s\n", env.name, pass ? "ALL PIXEL-EXACT ASSERTIONS PASS!" : "FAILED!");
    return pass;
}

// =============================================================================
// Round 2 Mock-GL Targeted Verification (Cases F, G, H)
// =============================================================================
namespace mock_gl {

struct DriverState {
    uint32_t curProgram       = 42u;
    uint32_t curFbo           = 5u;
    uint32_t curRenderbuffer  = 77u;
    uint32_t curArrayBuf      = 9u;
    uint32_t curElemBuf       = 10u;
    uint32_t activeUnit       = 0u;
    uint8_t  depthMask        = 1u;
    uint8_t  colorMask[4]     = { 1, 1, 1, 1 };
    bool     capBlend         = true;
    bool     capDepthTest     = true;
    bool     capCullFace      = false;
    bool     capScissorTest   = false;
    uint32_t blendEq          = hs_world_shadow::kGlFuncAdd;
    uint32_t blendEqAlpha     = hs_world_shadow::kGlFuncAdd;
    uint32_t blendSrc         = hs_world_shadow::kGlSrcAlpha;
    uint32_t blendDst         = hs_world_shadow::kGlOneMinusSrcAlpha;
    uint32_t blendSrcAlpha    = hs_world_shadow::kGlSrcAlpha;
    uint32_t blendDstAlpha    = hs_world_shadow::kGlOneMinusSrcAlpha;
    uint32_t depthFunc        = hs_world_shadow::kGlLess;

    uint32_t nextProgId       = 101u;
    uint32_t nextBufId        = 201u;
    uint32_t nextTexId        = 301u;
    uint32_t nextFboId        = 401u;
    uint32_t nextRboId        = 501u;
    int      liveBuffers      = 0;
    int      peakLiveBuffers  = 0;
    int      failLinkOnCall   = -1; // 1-based link call index to fail
    int      linkCalls        = 0;
    bool     failGenBuffers   = false;
    int      failGenBufAtIdx  = -1; // 1-based generated buffer index to fail
    int      genBufCount      = 0;
    int      checkFboCalls    = 0;
    int      failFboAfter     = -1; // fail glCheckFramebufferStatus when checkFboCalls > failFboAfter
    int      drawElementsCalls = 0;
    int      bufferDataCalls  = 0;
    int      bufferSubDataCalls = 0;
    int      mapRangeCalls    = 0;
    ptrdiff_t lastVboDataSize = 0;
    ptrdiff_t lastIboDataSize = 0;
    ptrdiff_t lastVboMapOff   = -1;
    ptrdiff_t lastIboMapOff   = -1;
    uint32_t lastVboMapFlags  = 0u;
    uint32_t lastIboMapFlags  = 0u;
    bool     mapReturnNull    = false;
    bool     unmapReturnFalse = false;
    bool     vboReOrphanedBeforeMap = false;
    bool     iboReOrphanedBeforeMap = false;
    uint32_t lastLinkedProg   = 0u;
    std::unordered_map<uint32_t, bool> progLinked;
    std::unordered_map<uint32_t, int32_t> bufSizes;
};

static DriverState g_drv;
static std::vector<uint8_t> g_mapScratch(512 * 1024, 0);

static void WINAPI mGenFbos(int32_t n, uint32_t* ids) { for (int i = 0; i < n; ++i) ids[i] = g_drv.nextFboId++; }
static void WINAPI mDelFbos(int32_t, const uint32_t*) {}
static void WINAPI mBindFbo(uint32_t, uint32_t id) { g_drv.curFbo = id; }
static void WINAPI mFboTex2D(uint32_t, uint32_t, uint32_t, uint32_t, int32_t) {}
static uint32_t WINAPI mCheckFbo(uint32_t) {
    ++g_drv.checkFboCalls;
    if (g_drv.failFboAfter >= 0 && g_drv.checkFboCalls > g_drv.failFboAfter) {
        return 0x8CD6u; // GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT
    }
    return hs_world_shadow::kGlFramebufferComplete;
}
static void WINAPI mGenRbos(int32_t n, uint32_t* ids) { for (int i = 0; i < n; ++i) ids[i] = g_drv.nextRboId++; }
static void WINAPI mDelRbos(int32_t, const uint32_t*) {}
static void WINAPI mBindRbo(uint32_t, uint32_t id) { g_drv.curRenderbuffer = id; }
static void WINAPI mRboStorage(uint32_t, uint32_t, int32_t, int32_t) {}
static void WINAPI mFboRbo(uint32_t, uint32_t, uint32_t, uint32_t) {}
static void WINAPI mGenTex(int32_t n, uint32_t* ids) { for (int i = 0; i < n; ++i) ids[i] = g_drv.nextTexId++; }
static void WINAPI mDelTex(int32_t, const uint32_t*) {}
static void WINAPI mActiveTex(uint32_t u) { g_drv.activeUnit = u - hs_world_shadow::kGlTexture0; }
static void WINAPI mBindTex(uint32_t, uint32_t) {}
static void WINAPI mTexImg2D(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*) {}
static void WINAPI mTexParamI(uint32_t, uint32_t, int32_t) {}
static uint32_t WINAPI mCreateShader(uint32_t) { static uint32_t s = 1; return s++; }
static void WINAPI mShaderSrc(uint32_t, int32_t, const char* const*, const int32_t*) {}
static void WINAPI mCompileShader(uint32_t) {}
static void WINAPI mGetShaderiv(uint32_t, uint32_t p, int32_t* out) { *out = (p == hs_world_shadow::kGlCompileStatus) ? 1 : 0; }
static void WINAPI mGetShaderLog(uint32_t, int32_t, int32_t* l, char* b) { if (l) *l = 0; if (b) b[0] = 0; }
static void WINAPI mDelShader(uint32_t) {}
static uint32_t WINAPI mCreateProg() { return g_drv.nextProgId++; }
static void WINAPI mAttachShader(uint32_t, uint32_t) {}
static void WINAPI mBindAttrib(uint32_t, uint32_t, const char*) {}
static void WINAPI mLinkProg(uint32_t p) {
    ++g_drv.linkCalls;
    g_drv.lastLinkedProg = p;
    g_drv.progLinked[p] = (g_drv.linkCalls != g_drv.failLinkOnCall);
}
static void WINAPI mGetProgiv(uint32_t p, uint32_t pn, int32_t* out) {
    if (pn == hs_world_shadow::kGlLinkStatus) *out = g_drv.progLinked[p] ? 1 : 0;
    else *out = 0;
}
static void WINAPI mGetProgLog(uint32_t, int32_t, int32_t* l, char* b) { if (l) *l = 0; if (b) b[0] = 0; }
static void WINAPI mDelProg(uint32_t) {}
static void WINAPI mUseProg(uint32_t p) { g_drv.curProgram = p; }
static int32_t WINAPI mGetUniLoc(uint32_t, const char*) { return 1; }
static void WINAPI mUni1f(int32_t, float) {}
static void WINAPI mUni1i(int32_t, int32_t) {}
static void WINAPI mUni4fv(int32_t, int32_t, const float*) {}
static void WINAPI mUniMat4fv(int32_t, int32_t, uint8_t, const float*) {}
static void WINAPI mGenBufs(int32_t n, uint32_t* ids) {
    for (int i = 0; i < n; ++i) {
        ++g_drv.genBufCount;
        if (g_drv.failGenBuffers || (g_drv.failGenBufAtIdx > 0 && g_drv.genBufCount >= g_drv.failGenBufAtIdx)) {
            ids[i] = 0u;
        } else {
            ids[i] = g_drv.nextBufId++;
            ++g_drv.liveBuffers;
            if (g_drv.liveBuffers > g_drv.peakLiveBuffers) g_drv.peakLiveBuffers = g_drv.liveBuffers;
        }
    }
}
static void WINAPI mDelBufs(int32_t n, const uint32_t* ids) {
    for (int i = 0; i < n; ++i) {
        if (ids[i] != 0u) {
            --g_drv.liveBuffers;
            g_drv.bufSizes.erase(ids[i]);
        }
    }
}
static void WINAPI mBindBuf(uint32_t t, uint32_t id) {
    if (t == hs_world_shadow::kGlArrayBuffer) g_drv.curArrayBuf = id;
    else if (t == hs_world_shadow::kGlElementArrayBuffer) g_drv.curElemBuf = id;
}
static void WINAPI mBufData(uint32_t target, ptrdiff_t sz, const void* data, uint32_t) {
    ++g_drv.bufferDataCalls;
    if (target == hs_world_shadow::kGlArrayBuffer) {
        if (g_drv.curArrayBuf != 0u) g_drv.bufSizes[g_drv.curArrayBuf] = static_cast<int32_t>(sz);
        g_drv.lastVboDataSize = sz;
        if (data == nullptr && sz >= static_cast<ptrdiff_t>(hs_ui_batch::kUiStreamBufVerts * sizeof(hs_ui_batch::UiBatchedVertex))) {
            g_drv.vboReOrphanedBeforeMap = true;
        }
    } else if (target == hs_world_shadow::kGlElementArrayBuffer) {
        if (g_drv.curElemBuf != 0u) g_drv.bufSizes[g_drv.curElemBuf] = static_cast<int32_t>(sz);
        g_drv.lastIboDataSize = sz;
        if (data == nullptr && sz >= static_cast<ptrdiff_t>(hs_ui_batch::kUiStreamBufIndices * sizeof(uint16_t))) {
            g_drv.iboReOrphanedBeforeMap = true;
        }
    }
}
static void WINAPI mGetBufParamiv(uint32_t target, uint32_t pname, int32_t* out) {
    if (!out) return;
    if (pname == hs_sprite_batch::kGlBufferSize) {
        const uint32_t id = (target == hs_world_shadow::kGlArrayBuffer) ? g_drv.curArrayBuf
                          : (target == hs_world_shadow::kGlElementArrayBuffer) ? g_drv.curElemBuf : 0u;
        auto it = g_drv.bufSizes.find(id);
        *out = (it != g_drv.bufSizes.end()) ? it->second : 0;
    } else {
        *out = 0;
    }
}
static void WINAPI mBufSubData(uint32_t, ptrdiff_t, ptrdiff_t, const void*) {
    ++g_drv.bufferSubDataCalls;
}
static void* WINAPI mMapBufRange(uint32_t target, ptrdiff_t off, ptrdiff_t len, uint32_t flags) {
    ++g_drv.mapRangeCalls;
    if (target == hs_world_shadow::kGlArrayBuffer) {
        g_drv.lastVboMapOff = off;
        g_drv.lastVboMapFlags = flags;
    } else if (target == hs_world_shadow::kGlElementArrayBuffer) {
        g_drv.lastIboMapOff = off;
        g_drv.lastIboMapFlags = flags;
    }
    if (g_drv.mapReturnNull || len <= 0 || static_cast<size_t>(len) > g_mapScratch.size()) {
        return nullptr;
    }
    return g_mapScratch.data();
}
static uint8_t WINAPI mUnmapBuf(uint32_t) {
    return g_drv.unmapReturnFalse ? 0u : 1u;
}
static void WINAPI mEnAttr(uint32_t) {}
static void WINAPI mDisAttr(uint32_t) {}
static void WINAPI mAttrPtr(uint32_t, int32_t, uint32_t, uint8_t, int32_t, const void*) {}
static void WINAPI mDrawArr(uint32_t, int32_t, int32_t) {}
static void WINAPI mDrawElem(uint32_t, int32_t, uint32_t, const void*) { ++g_drv.drawElementsCalls; }
static void WINAPI mViewport(int32_t, int32_t, int32_t, int32_t) {}
static void WINAPI mClearColor(float, float, float, float) {}
static void WINAPI mClear(uint32_t) {}
static void WINAPI mEnable(uint32_t c) {
    if (c == hs_world_shadow::kGlBlend) g_drv.capBlend = true;
    else if (c == hs_world_shadow::kGlDepthTest) g_drv.capDepthTest = true;
    else if (c == hs_world_shadow::kGlCullFace) g_drv.capCullFace = true;
    else if (c == hs_world_shadow::kGlScissorTest) g_drv.capScissorTest = true;
}
static void WINAPI mDisable(uint32_t c) {
    if (c == hs_world_shadow::kGlBlend) g_drv.capBlend = false;
    else if (c == hs_world_shadow::kGlDepthTest) g_drv.capDepthTest = false;
    else if (c == hs_world_shadow::kGlCullFace) g_drv.capCullFace = false;
    else if (c == hs_world_shadow::kGlScissorTest) g_drv.capScissorTest = false;
}
static void WINAPI mBlendFunc(uint32_t s, uint32_t d) {
    g_drv.blendSrc = g_drv.blendSrcAlpha = s;
    g_drv.blendDst = g_drv.blendDstAlpha = d;
}
static void WINAPI mBlendFuncSep(uint32_t sr, uint32_t dr, uint32_t sa, uint32_t da) {
    g_drv.blendSrc = sr; g_drv.blendDst = dr;
    g_drv.blendSrcAlpha = sa; g_drv.blendDstAlpha = da;
}
static void WINAPI mBlendEq(uint32_t e) { g_drv.blendEq = g_drv.blendEqAlpha = e; }
static void WINAPI mBlendEqSep(uint32_t er, uint32_t ea) { g_drv.blendEq = er; g_drv.blendEqAlpha = ea; }
static void WINAPI mDepthFunc(uint32_t f) { g_drv.depthFunc = f; }
static void WINAPI mDepthMask(uint8_t m) { g_drv.depthMask = m ? 1u : 0u; }
static void WINAPI mColorMask(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    g_drv.colorMask[0] = r ? 1u : 0u; g_drv.colorMask[1] = g ? 1u : 0u;
    g_drv.colorMask[2] = b ? 1u : 0u; g_drv.colorMask[3] = a ? 1u : 0u;
}
static void WINAPI mGetIntv(uint32_t p, int32_t* out) {
    switch (p) {
        case hs_world_shadow::kGlMaxTextureImageUnits: *out = 16; break;
        case hs_world_shadow::kGlCurrentProgram:       *out = static_cast<int32_t>(g_drv.curProgram); break;
        case hs_world_shadow::kGlRenderbufferBinding:  *out = static_cast<int32_t>(g_drv.curRenderbuffer); break;
        case hs_world_shadow::kGlDepthWritemask:       *out = g_drv.depthMask; break;
        case hs_world_shadow::kGlDepthTest:            *out = g_drv.capDepthTest ? 1 : 0; break;
        case hs_world_shadow::kGlBlend:                *out = g_drv.capBlend ? 1 : 0; break;
        case hs_world_shadow::kGlCullFace:             *out = g_drv.capCullFace ? 1 : 0; break;
        case hs_world_shadow::kGlScissorTest:          *out = g_drv.capScissorTest ? 1 : 0; break;
        case hs_world_shadow::kGlBlendEquationRgb:     *out = static_cast<int32_t>(g_drv.blendEq); break;
        case hs_world_shadow::kGlBlendEquationAlpha:   *out = static_cast<int32_t>(g_drv.blendEqAlpha); break;
        case hs_world_shadow::kGlBlendSrcRgb:          *out = static_cast<int32_t>(g_drv.blendSrc); break;
        case hs_world_shadow::kGlBlendDstRgb:          *out = static_cast<int32_t>(g_drv.blendDst); break;
        case hs_world_shadow::kGlBlendSrcAlpha:        *out = static_cast<int32_t>(g_drv.blendSrcAlpha); break;
        case hs_world_shadow::kGlBlendDstAlpha:        *out = static_cast<int32_t>(g_drv.blendDstAlpha); break;
        case hs_world_shadow::kGlDepthFuncEnum:        *out = static_cast<int32_t>(g_drv.depthFunc); break;
        case hs_world_shadow::kGlColorWritemask:
            out[0] = g_drv.colorMask[0]; out[1] = g_drv.colorMask[1];
            out[2] = g_drv.colorMask[2]; out[3] = g_drv.colorMask[3];
            break;
        default: break;
    }
}
static uint32_t WINAPI mGetErr() { return 0u; }

static hs_world_shadow::GlShadowOps MakeShadowOps() {
    hs_world_shadow::GlShadowOps o{};
    o.glGenFramebuffers = &mGenFbos; o.glDeleteFramebuffers = &mDelFbos;
    o.glBindFramebuffer = &mBindFbo; o.glFramebufferTexture2D = &mFboTex2D;
    o.glCheckFramebufferStatus = &mCheckFbo; o.glGenRenderbuffers = &mGenRbos;
    o.glDeleteRenderbuffers = &mDelRbos; o.glBindRenderbuffer = &mBindRbo;
    o.glRenderbufferStorage = &mRboStorage; o.glFramebufferRenderbuffer = &mFboRbo;
    o.glGenTextures = &mGenTex; o.glDeleteTextures = &mDelTex;
    o.glActiveTexture = &mActiveTex; o.glBindTexture = &mBindTex;
    o.glTexImage2D = &mTexImg2D; o.glTexParameteri = &mTexParamI;
    o.glCreateShader = &mCreateShader; o.glShaderSource = &mShaderSrc;
    o.glCompileShader = &mCompileShader; o.glGetShaderiv = &mGetShaderiv;
    o.glGetShaderInfoLog = &mGetShaderLog; o.glDeleteShader = &mDelShader;
    o.glCreateProgram = &mCreateProg; o.glAttachShader = &mAttachShader;
    o.glBindAttribLocation = &mBindAttrib; o.glLinkProgram = &mLinkProg;
    o.glGetProgramiv = &mGetProgiv; o.glGetProgramInfoLog = &mGetProgLog;
    o.glDeleteProgram = &mDelProg; o.glUseProgram = &mUseProg;
    o.glGetUniformLocation = &mGetUniLoc; o.glUniform1f = &mUni1f;
    o.glUniform1i = &mUni1i; o.glUniform4fv = &mUni4fv;
    o.glUniformMatrix4fv = &mUniMat4fv; o.glGenBuffers = &mGenBufs;
    o.glDeleteBuffers = &mDelBufs; o.glBindBuffer = &mBindBuf;
    o.glBufferData = &mBufData; o.glEnableVertexAttribArray = &mEnAttr;
    o.glDisableVertexAttribArray = &mDisAttr; o.glVertexAttribPointer = &mAttrPtr;
    o.glDrawArrays = &mDrawArr; o.glDrawElements = &mDrawElem;
    o.glViewport = &mViewport; o.glClearColor = &mClearColor;
    o.glClear = &mClear; o.glEnable = &mEnable; o.glDisable = &mDisable;
    o.glBlendFunc = &mBlendFunc; o.glBlendFuncSeparate = &mBlendFuncSep;
    o.glBlendEquation = &mBlendEq; o.glBlendEquationSeparate = &mBlendEqSep;
    o.glDepthFunc = &mDepthFunc; o.glDepthMask = &mDepthMask;
    o.glColorMask = &mColorMask; o.glGetIntegerv = &mGetIntv;
    o.glGetError = &mGetErr;
    return o;
}

static bool TestRound2MockCases() {
    printf("\n=== Round 2 & Round 3 Targeted Mock-GL Tests (Cases F, G, H, K, M, N, O) ===\n");
    bool allOk = true;

    // Case F (P1 #2): EnsureResources 2nd shader link failure MUST restore program 42 & glCache 42!
    {
        g_drv = DriverState{};
        g_drv.curProgram = 42u;
        g_drv.curRenderbuffer = 77u;
        g_drv.depthMask = 1u;
        g_drv.failLinkOnCall = 2; // progBatch_ (101) succeeds & binds; progComposite_ (102) fails link!

        hs_world_shadow::WorldShadowEngine eng;
        eng.SetOps(MakeShadowOps(), false);
        hs_gl_cache::ThreadState glCache;
        glCache.curProgram = 42u;

        const int32_t vp[4] = { 0, 0, 1280, 720 };
        hs_world_shadow::ShadowConfig cfg{};
        cfg.projOn = 1.0f; cfg.density = 0.6f; cfg.hdMode = true;
        hs_world_shadow::ShadowPassState saved{};
        saved.dstFbo = 5u; saved.dstProg = 42u;
        memcpy(saved.vp, vp, sizeof(vp));

        eng.OnGroundOrOceanDraw(5u, vp);
        const bool okInit = eng.OnBeforeFirstStandingEntity(cfg, saved, glCache);
        const bool passF = (!okInit) && eng.IsFailed() &&
                           (g_drv.curProgram == 42u) &&
                           (glCache.curProgram == 42u) &&
                           (g_drv.curRenderbuffer == 77u) &&
                           (g_drv.depthMask == 1u);
        printf("  Case F (P1 #2 EnsureResources link-fail prog restore): init=%d failed=%d drvProg=%u cacheProg=%u rbo=%u dm=%u -> %s\n",
               okInit ? 1 : 0, eng.IsFailed() ? 1 : 0, g_drv.curProgram, glCache.curProgram,
               g_drv.curRenderbuffer, g_drv.depthMask, passF ? "PASS" : "FAIL");
        if (!passF) allOk = false;
    }

    // Case G (P1 #3): Mock-GL ANGLE init + res switch + cache OFF with depthMask=1 & rbo=77 & unknown glCache!
    {
        g_drv = DriverState{};
        g_drv.curProgram = 42u;
        g_drv.curRenderbuffer = 77u;
        g_drv.depthMask = 1u;
        g_drv.failLinkOnCall = -1;

        hs_world_shadow::WorldShadowEngine eng;
        eng.SetOps(MakeShadowOps(), false);
        hs_gl_cache::ThreadState glCache;
        glCache.ResetBindingsOnly(); // Unknown!

        const int32_t vp[4] = { 0, 0, 1280, 720 };
        hs_world_shadow::ShadowConfig cfg{};
        cfg.projOn = 1.0f; cfg.density = 0.6f; cfg.hdMode = true;
        hs_world_shadow::ShadowPassState saved{};
        saved.dstFbo = 5u; saved.dstProg = 42u;
        memcpy(saved.vp, vp, sizeof(vp));

        eng.OnGroundOrOceanDraw(5u, vp);
        bool ok1 = eng.OnBeforeFirstStandingEntity(cfg, saved, glCache);
        bool p1  = ok1 && (g_drv.curProgram == 42u) && (g_drv.curRenderbuffer == 77u) && (g_drv.depthMask == 1u);
        eng.OnEndFrame();

        glCache.ResetBindingsOnly();
        cfg.hdMode = false; // 1024 -> 512 resolution switch
        eng.OnGroundOrOceanDraw(5u, vp);
        bool ok2 = eng.OnBeforeFirstStandingEntity(cfg, saved, glCache);
        bool p2  = ok2 && (g_drv.curProgram == 42u) && (g_drv.curRenderbuffer == 77u) && (g_drv.depthMask == 1u);
        eng.OnEndFrame();

        saved.glCacheTrusted = false;
        glCache.depthMask = 0; // Stale cache must be ignored!
        eng.OnGroundOrOceanDraw(5u, vp);
        bool ok3 = eng.OnBeforeFirstStandingEntity(cfg, saved, glCache);
        bool p3  = ok3 && (g_drv.curProgram == 42u) && (g_drv.curRenderbuffer == 77u) && (g_drv.depthMask == 1u);
        eng.OnEndFrame();

        const bool passG = p1 && p2 && p3;
        printf("  Case G (P1 #3 ANGLE rbo=77 dm=1 unknown-cache/res-switch/cache-off): p1=%d p2=%d p3=%d (drvProg=%u rbo=%u dm=%u) -> %s\n",
               p1 ? 1 : 0, p2 ? 1 : 0, p3 ? 1 : 0, g_drv.curProgram, g_drv.curRenderbuffer, g_drv.depthMask,
               passG ? "PASS" : "FAIL");
        if (!passG) allOk = false;
    }

    // Case H (P2 #5): UiBatcher first-init failure MUST reject draw #1 BEFORE accepting vertices!
    {
        auto runUiFail = [&](bool failBufs, bool failLink, bool noBackingStore, const char* tag) -> bool {
            g_drv = DriverState{};
            g_drv.failGenBuffers = failBufs;
            g_drv.failLinkOnCall = failLink ? 1 : -1;

            hs_ui_batch::UiGlOps uops{};
            uops.glGenBuffers = &mGenBufs; uops.glDeleteBuffers = &mDelBufs;
            uops.glBindBuffer = &mBindBuf;
            uops.glBufferData = noBackingStore
                ? +[](uint32_t, ptrdiff_t, const void*, uint32_t) { ++g_drv.bufferDataCalls; }
                : &mBufData;
            uops.glGetBufferParameteriv = &mGetBufParamiv;
            uops.glActiveTexture = &mActiveTex; uops.glBindTexture = &mBindTex;
            uops.glUseProgram = &mUseProg; uops.glCreateShader = &mCreateShader;
            uops.glShaderSource = &mShaderSrc; uops.glCompileShader = &mCompileShader;
            uops.glGetShaderiv = &mGetShaderiv; uops.glDeleteShader = &mDelShader;
            uops.glCreateProgram = &mCreateProg; uops.glAttachShader = &mAttachShader;
            uops.glBindAttribLocation = &mBindAttrib; uops.glLinkProgram = &mLinkProg;
            uops.glGetProgramiv = &mGetProgiv; uops.glDeleteProgram = &mDelProg;
            uops.glGetUniformLocation = &mGetUniLoc; uops.glUniform1i = &mUni1i;
            uops.glUniformMatrix4fv = &mUniMat4fv; uops.glVertexAttribPointer = &mAttrPtr;
            uops.glEnableVertexAttribArray = &mEnAttr; uops.glDisableVertexAttribArray = &mDisAttr;
            uops.glDrawElements = &mDrawElem; uops.glGetIntegerv = &mGetIntv;

            hs_ui_batch::UiBatcher ub;
            ub.SetOps(uops);
            ub.SetEnabled(true);

            hs_ui_batch::UiProgInfo info{};
            info.kind = hs_ui_batch::UiProgKind::UiBasic;
            info.eligible = true; info.locPos = 0; info.locUv = 1; info.locDiffuse = 2;
            info.locMatrixPVW = 3; info.locSampler0 = 4; info.samplerUnit0 = 0;
            info.hasPVW = true;
            for (int i = 0; i < 4; ++i) info.matPVW[i * 5] = 1.0f;

            ub.OnActiveTexture(hs_world_shadow::kGlTexture0);
            ub.OnBindTexture(hs_world_shadow::kGlTexture2D, 99u);
            ub.OnUseProgram(77u, &info);

            float pos[9] = { 0,0,0, 1,0,0, 0,1,0 };
            float uv[6]  = { 0,0, 1,0, 0,1 };
            float col[12] = { 1,1,1,1, 1,1,1,1, 1,1,1,1 };
            uint16_t idx[3] = { 0, 1, 2 };
            ub.OnEnableDisableVertexAttribArray(0, true);
            ub.OnVertexAttribPointer(0, 3, hs_world_shadow::kGlFloat, 0, 12, pos);
            ub.OnEnableDisableVertexAttribArray(1, true);
            ub.OnVertexAttribPointer(1, 2, hs_world_shadow::kGlFloat, 0, 8, uv);
            ub.OnEnableDisableVertexAttribArray(2, true);
            ub.OnVertexAttribPointer(2, 4, hs_world_shadow::kGlFloat, 0, 16, col);

            const bool accepted = ub.TryBatchDrawElements(hs_world_shadow::kGlTriangles, 3, hs_world_shadow::kGlUnsignedShort, idx);
            const auto& st = ub.CumulativeStats();
            const bool ok = (!accepted) && (st.drawsBatched == 0u) && (st.vertsBatched == 0u) &&
                            (!ub.HasPending()) && (g_drv.liveBuffers == 0);
            printf("  Case H (P2 #5 UiBatcher %s): accepted=%d batched=%llu pending=%d liveBufs=%d -> %s\n",
                   tag, accepted ? 1 : 0, static_cast<unsigned long long>(st.drawsBatched),
                   ub.HasPending() ? 1 : 0, g_drv.liveBuffers, ok ? "PASS" : "FAIL");
            return ok;
        };
        if (!runUiFail(false, true,  false, "uber-link-fail")) allOk = false;
        if (!runUiFail(true,  false, false, "vbo-alloc-fail")) allOk = false;
        if (!runUiFail(false, false, true,  "no-backing-store")) allOk = false;
    }

    // Helper for SpriteBatcher cases (K & N)
    auto makeSpriteOps = [&]() {
        hs_sprite_batch::GlBatchOps sops{};
        sops.glGenBuffers = &mGenBufs;
        sops.glDeleteBuffers = &mDelBufs;
        sops.glBindBuffer = &mBindBuf;
        sops.glBufferData = &mBufData;
        sops.glBufferSubData = &mBufSubData;
        sops.glActiveTexture = &mActiveTex;
        sops.glBindTexture = &mBindTex;
        sops.glUniform1f = &mUni1f;
        sops.glUniform1i = &mUni1i;
        sops.glUniformMatrix4fv = &mUniMat4fv;
        sops.glVertexAttribPointer = &mAttrPtr;
        sops.glEnableVertexAttribArray = &mEnAttr;
        sops.glDisableVertexAttribArray = &mDisAttr;
        sops.glDrawElements = &mDrawElem;
        sops.glGetIntegerv = &mGetIntv;
        sops.glGetBufferParameteriv = &mGetBufParamiv;
        return sops;
    };
    auto setupSpriteState = [&](hs_sprite_batch::SpriteBatcher& sb,
                                hs_sprite_batch::BatchProgInfo& info,
                                const float* verts, uint32_t hostVbo, uint32_t hostIbo) {
        info = hs_sprite_batch::BatchProgInfo{};
        info.eligible = true;
        info.locBatchActive = 1;
        info.locMatrixW = 2;
        info.locPos2dUv = 0;
        info.locBatchWpos = 7;
        info.locBatchCMult = 5;
        info.locBatchCAdd = 6;
        info.locSampler0 = 10;
        info.samplerUnit0 = 0;
        for (uint32_t s = 0; s < hs_sprite_batch::kMaxBatchTexSlots; ++s) {
            info.locBatchTex[s] = static_cast<int32_t>(20 + s);
        }
        sb.OnActiveTexture(hs_world_shadow::kGlTexture0);
        sb.OnBindTexture(hs_world_shadow::kGlTexture2D, 333u);
        sb.OnBindBuffer(hs_world_shadow::kGlArrayBuffer, hostVbo);
        sb.OnBindBuffer(hs_world_shadow::kGlElementArrayBuffer, hostIbo);
        sb.OnUseProgram(55u, &info);
        const float matW[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        sb.TryAbsorbMatrixW(2, 1, 0, matW);
        sb.OnEnableDisableVertexAttribArray(0, true);
        sb.OnBindBuffer(hs_world_shadow::kGlArrayBuffer, 0u);
        sb.OnVertexAttribPointer(0, 4, hs_world_shadow::kGlFloat, 0, 16, verts);
        sb.OnBindBuffer(hs_world_shadow::kGlArrayBuffer, hostVbo);
    };

    // Case K (Round 3 P1 #1): SpriteBatcher GPU ring alloc failure MUST NEVER swallow draws or leak buffers!
    {
        const float tri[12] = { 0,0, 0.2f,0.2f, 1,0, 0.8f,0.2f, 0,1, 0.2f,0.8f };
        auto runSpriteAllocCase = [&](bool failAll, int failAtIdx, const char* tag) -> bool {
            g_drv = DriverState{};
            g_drv.curArrayBuf = 77u;
            g_drv.curElemBuf  = 88u;
            g_drv.failGenBuffers = failAll;
            g_drv.failGenBufAtIdx = failAtIdx;

            hs_sprite_batch::SpriteBatcher sb;
            sb.SetOps(makeSpriteOps());
            sb.SetEnabled(true);
            hs_sprite_batch::BatchProgInfo info{};
            setupSpriteState(sb, info, tri, 77u, 88u);

            const bool acc1 = sb.TryBatchDrawArrays(hs_world_shadow::kGlTriangles, 0, 3);
            const bool acc2 = sb.TryBatchDrawArrays(hs_world_shadow::kGlTriangles, 0, 3);
            sb.Flush(hs_sprite_batch::FlushReason::End);
            bool ok = false;
            if (!failAll && failAtIdx < 0) {
                // Control: both draws accepted & flushed into 1 merged GPU draw call, then cleanly released
                ok = acc1 && acc2 && (g_drv.drawElementsCalls == 1) && (g_drv.liveBuffers == 16);
                sb.ReleaseGpuResources();
                ok = ok && (g_drv.liveBuffers == 0);
            } else {
                // Failure (full or partial): 0 accepted, 0 pending, 0 draws swallowed, 0 leaked buffers, host VBO/IBO restored!
                ok = (!acc1) && (!acc2) && (!sb.HasPending()) &&
                     (g_drv.drawElementsCalls == 0) && (g_drv.liveBuffers == 0) &&
                     (g_drv.curArrayBuf == 77u) && (g_drv.curElemBuf == 88u);
            }
            printf("  Case K (R3 P1 #1 SpriteBatcher %s): acc1=%d acc2=%d draws=%d pending=%d liveBufs=%d vbo=%u ibo=%u -> %s\n",
                   tag, acc1 ? 1 : 0, acc2 ? 1 : 0, g_drv.drawElementsCalls,
                   sb.HasPending() ? 1 : 0, g_drv.liveBuffers,
                   g_drv.curArrayBuf, g_drv.curElemBuf, ok ? "PASS" : "FAIL");
            return ok;
        };
        if (!runSpriteAllocCase(false, -1, "control-ok")) allOk = false;
        if (!runSpriteAllocCase(true,  -1, "full-alloc-fail")) allOk = false;
        if (!runSpriteAllocCase(false,  4, "partial-alloc-fail")) allOk = false;

        // Round 4 P1 #1 (exact auditor regression): glGenBuffers succeeds, glBufferData fails to allocate backing store!
        {
            g_drv = DriverState{};
            g_drv.curArrayBuf = 77u;
            g_drv.curElemBuf  = 88u;
            auto noStore = makeSpriteOps();
            noStore.glBufferData = [](uint32_t, ptrdiff_t, const void*, uint32_t) { ++g_drv.bufferDataCalls; };
            noStore.glDrawElements = [](uint32_t, int32_t, uint32_t, const void*) { ++g_drv.drawElementsCalls; };
            hs_sprite_batch::SpriteBatcher sb;
            sb.SetOps(noStore);
            sb.SetEnabled(true);
            hs_sprite_batch::BatchProgInfo info{};
            setupSpriteState(sb, info, tri, 77u, 88u);
            const bool accepted = sb.TryBatchDrawArrays(hs_world_shadow::kGlTriangles, 0, 3);
            sb.Flush(hs_sprite_batch::FlushReason::End);
            const bool okNoStore = (!accepted) && (g_drv.drawElementsCalls == 0) &&
                                   (!sb.HasPending()) && (g_drv.liveBuffers == 0) &&
                                   (g_drv.curArrayBuf == 77u) && (g_drv.curElemBuf == 88u);
            printf("  Case K (R4 P1 #1 no backing store): accepted=%d allocationAttempts=%d drawAttempts=%d pending=%d liveBufs=%d -> %s\n",
                   accepted ? 1 : 0, g_drv.bufferDataCalls, g_drv.drawElementsCalls,
                   sb.HasPending() ? 1 : 0, g_drv.liveBuffers, okNoStore ? "PASS" : "FAIL");
            if (!okNoStore) allOk = false;
            sb.ReleaseGpuResources();
        }
    }

    // Case M (Round 3 P2 #5): WorldShadowEngine rebuild failure after ready MUST clear ready, set failed & trigger legacy restore!
    {
        g_drv = DriverState{};
        g_drv.curProgram = 42u;
        g_drv.curRenderbuffer = 77u;
        g_drv.depthMask = 1u;
        g_drv.failFboAfter = 2; // Initial 2 FBOs (1024x1024) succeed; 512x512 rebuild FBO #3 fails incomplete!

        hs_world_shadow::WorldShadowEngine eng;
        eng.SetOps(MakeShadowOps(), false);
        hs_gl_cache::ThreadState glCache;
        glCache.ResetBindingsOnly();

        bool gReady = false, gFailed = false;
        uint32_t gRes = 0u, gHd2dGen = 1u;
        auto publishTransition = [&](bool ok) {
            const bool wasReady = gReady, wasFailed = gFailed;
            const bool nowReady = ok && eng.IsReady() && !eng.IsFailed();
            const bool nowFailed = eng.IsFailed();
            if (nowReady) {
                gFailed = false; gReady = true; gRes = eng.Resolution();
                if (!wasReady || wasFailed) ++gHd2dGen;
            } else if (nowFailed) {
                gReady = false; gFailed = true; gRes = 0u;
                if (wasReady || !wasFailed) ++gHd2dGen;
            }
        };

        const int32_t vp[4] = { 0, 0, 1280, 720 };
        hs_world_shadow::ShadowConfig cfg{};
        cfg.projOn = 1.0f; cfg.density = 0.6f; cfg.hdMode = true;
        hs_world_shadow::ShadowPassState saved{};
        saved.dstFbo = 5u; saved.dstProg = 42u; memcpy(saved.vp, vp, sizeof(vp));

        // Frame 1: 2048x2048 succeeds -> ready=true, failed=false, gen=2
        eng.OnGroundOrOceanDraw(5u, vp);
        bool ok1 = eng.OnBeforeFirstStandingEntity(cfg, saved, glCache);
        publishTransition(ok1);
        eng.OnEndFrame();
        const bool step1Ok = ok1 && eng.IsReady() && !eng.IsFailed() && gReady && !gFailed && (gRes == 2048u) && (gHd2dGen == 2u);

        // Frame 2: 2048->512 switch fails FBO completeness -> ready=false, failed=true, res=0, gen=3 (AnimBloom restores legacy!)
        glCache.ResetBindingsOnly();
        cfg.hdMode = false;
        eng.OnGroundOrOceanDraw(5u, vp);
        bool ok2 = eng.OnBeforeFirstStandingEntity(cfg, saved, glCache);
        publishTransition(ok2);
        eng.OnEndFrame();
        const bool suppressLegacy = gReady && !gFailed;
        const bool step2Ok = (!ok2) && (!eng.IsReady()) && eng.IsFailed() &&
                             (!gReady) && gFailed && (gRes == 0u) && (gHd2dGen == 3u) && (!suppressLegacy) &&
                             (g_drv.curProgram == 42u) && (g_drv.curRenderbuffer == 77u) && (g_drv.depthMask == 1u);
        const bool passM = step1Ok && step2Ok;
        printf("  Case M (R3 P2 #5 shadow rebuild-after-ready fail): s1=%d s2=%d ready=%d failed=%d res=%u gen=%u suppressLegacy=%d -> %s\n",
               step1Ok ? 1 : 0, step2Ok ? 1 : 0, gReady ? 1 : 0, gFailed ? 1 : 0, gRes, gHd2dGen,
               suppressLegacy ? 1 : 0, passM ? "PASS" : "FAIL");
        if (!passM) allOk = false;

        // Case M2 (v2.9.5-E): SpriteBatcher minShadowHeight filters low ground items while preserving tall sprites!
        {
            g_drv = DriverState{};
            const float lowTri[12]  = { 0.0f, 0.0f, 0.2f, 0.2f, 0.5f, 0.0f, 0.8f, 0.2f, 0.25f, 0.55f, 0.5f, 0.8f };
            const float tallTri[12] = { 0.0f, 0.0f, 0.2f, 0.2f, 1.0f, 0.0f, 0.8f, 0.2f, 0.50f, 3.00f, 0.5f, 0.8f };
            uint32_t shadowMirrorCalls = 0u;
            auto mhOps = makeSpriteOps();
            mhOps.onShadowMirrorBatch = [](uint32_t, void* ctx) { ++(*reinterpret_cast<uint32_t*>(ctx)); };
            mhOps.callbackCtx = &shadowMirrorCalls;
            hs_sprite_batch::SpriteBatcher sb;
            sb.SetOps(mhOps);
            sb.SetEnabled(true);
            hs_sprite_batch::BatchProgInfo info{};

            // With minShadowHeight = 0.85, low ground item (maxWy=0.55) MUST NOT cast shadow!
            setupSpriteState(sb, info, lowTri, 77u, 88u);
            sb.SetShadowHints(true, false, 0.707f, -0.707f, 0.85f);
            bool aLow = sb.TryBatchDrawArrays(hs_world_shadow::kGlTriangles, 0, 3);
            sb.Flush(hs_sprite_batch::FlushReason::End);
            const uint32_t lowMirrors = shadowMirrorCalls;

            // Tall sprite (maxWy=3.00) with minShadowHeight = 0.85 MUST still cast shadow!
            setupSpriteState(sb, info, tallTri, 77u, 88u);
            sb.SetShadowHints(true, false, 0.707f, -0.707f, 0.85f);
            bool aTall = sb.TryBatchDrawArrays(hs_world_shadow::kGlTriangles, 0, 3);
            sb.Flush(hs_sprite_batch::FlushReason::End);
            const uint32_t tallMirrors = shadowMirrorCalls - lowMirrors;

            // With minShadowHeight = 0.00 (default), low ground item casts shadow normally!
            setupSpriteState(sb, info, lowTri, 77u, 88u);
            sb.SetShadowHints(true, false, 0.707f, -0.707f, 0.00f);
            bool aLowOff = sb.TryBatchDrawArrays(hs_world_shadow::kGlTriangles, 0, 3);
            sb.Flush(hs_sprite_batch::FlushReason::End);
            const uint32_t lowOffMirrors = shadowMirrorCalls - lowMirrors - tallMirrors;

            sb.ReleaseGpuResources();
            const bool passM2 = aLow && aTall && aLowOff && (lowMirrors == 0u) && (tallMirrors == 1u) && (lowOffMirrors == 1u);
            printf("  Case M2 (v2.9.5-E SpriteBatcher minShadowHeight): low085=%u tall085=%u low000=%u -> %s\n",
                   lowMirrors, tallMirrors, lowOffMirrors, passM2 ? "PASS" : "FAIL");
            if (!passM2) allOk = false;
        }
    }

    // Case N (Round 3 P2 #6): SpriteBatcher multi-round A -> B -> A context switches MUST leak 0 GPU buffers!
    {
        g_drv = DriverState{};
        const float tri[12] = { 0,0, 0.2f,0.2f, 1,0, 0.8f,0.2f, 0,1, 0.2f,0.8f };
        hs_sprite_batch::SpriteBatcher sb;
        sb.SetOps(makeSpriteOps());
        sb.SetEnabled(true);
        hs_sprite_batch::BatchProgInfo info{};
        void* ctxA = reinterpret_cast<void*>(0x1001);
        void* ctxB = reinterpret_cast<void*>(0x2002);

        bool roundsOk = true;
        for (int r = 0; r < 5; ++r) {
            sb.ResetContext(ctxA, false);
            setupSpriteState(sb, info, tri, 11u, 12u);
            bool acc = sb.TryBatchDrawArrays(hs_world_shadow::kGlTriangles, 0, 3);
            sb.Flush(hs_sprite_batch::FlushReason::End);
            if (!acc || g_drv.liveBuffers != 16) roundsOk = false;
            // Exact HookEglMakeCurrent sequence when leaving ctxA -> ctxB:
            sb.ReleaseGpuResources();
            if (g_drv.liveBuffers != 0) roundsOk = false;
            sb.ResetContext(ctxB, false);
        }
        const bool passN = roundsOk && (g_drv.liveBuffers == 0) && (g_drv.peakLiveBuffers == 16) && (g_drv.drawElementsCalls == 5);
        printf("  Case N (R3 P2 #6 SpriteBatcher 5x A->B->A zero VRAM leak): live=%d peak=%d(exp 16) draws=%d -> %s\n",
               g_drv.liveBuffers, g_drv.peakLiveBuffers, g_drv.drawElementsCalls, passN ? "PASS" : "FAIL");
        if (!passN) allOk = false;

        // Round 4 P2 #6 (exact auditor regression): Same-context MakeCurrent failure (prevCtx == ctx)
        // skips pre-switch cleanup; ResetContext(ctxA) MUST preserve live buffer handles so ReleaseGpuResources() frees all 16!
        {
            g_drv = DriverState{};
            sb.ResetContext(ctxA, false);
            setupSpriteState(sb, info, tri, 11u, 12u);
            const bool accepted = sb.TryBatchDrawArrays(hs_world_shadow::kGlTriangles, 0, 3);
            const int liveBefore = g_drv.liveBuffers;
            sb.ResetContext(ctxA);
            const bool pendingAfterReset = sb.HasPending();
            sb.ReleaseGpuResources();
            const bool okSameCtx = accepted && (liveBefore == 16) && (g_drv.liveBuffers == 0) && (!pendingAfterReset);
            printf("  Case N (R4 P2 #6 same-context failure reset): accepted=%d liveBefore=%d liveAfterRelease=%d pending=%d -> %s\n",
                   accepted ? 1 : 0, liveBefore, g_drv.liveBuffers, pendingAfterReset ? 1 : 0,
                   okSameCtx ? "PASS" : "FAIL");
            if (!okSameCtx) allOk = false;
        }
    }

    // Case O (Round 3 P2 #7): UiBatcher fallback upload advances cursor, checks glUnmapBuffer()==0, and re-orphans shrunk store!
    {
        g_drv = DriverState{};
        hs_ui_batch::UiGlOps uops{};
        uops.glGenBuffers = &mGenBufs; uops.glDeleteBuffers = &mDelBufs;
        uops.glBindBuffer = &mBindBuf; uops.glBufferData = &mBufData;
        uops.glBufferSubData = &mBufSubData;
        uops.glMapBufferRange = &mMapBufRange; uops.glUnmapBuffer = &mUnmapBuf;
        uops.glActiveTexture = &mActiveTex; uops.glBindTexture = &mBindTex;
        uops.glUseProgram = &mUseProg; uops.glCreateShader = &mCreateShader;
        uops.glShaderSource = &mShaderSrc; uops.glCompileShader = &mCompileShader;
        uops.glGetShaderiv = &mGetShaderiv; uops.glDeleteShader = &mDelShader;
        uops.glCreateProgram = &mCreateProg; uops.glAttachShader = &mAttachShader;
        uops.glBindAttribLocation = &mBindAttrib; uops.glLinkProgram = &mLinkProg;
        uops.glGetProgramiv = &mGetProgiv; uops.glDeleteProgram = &mDelProg;
        uops.glGetUniformLocation = &mGetUniLoc; uops.glUniform1i = &mUni1i;
        uops.glUniformMatrix4fv = &mUniMat4fv; uops.glVertexAttribPointer = &mAttrPtr;
        uops.glEnableVertexAttribArray = &mEnAttr; uops.glDisableVertexAttribArray = &mDisAttr;
        uops.glDrawElements = &mDrawElem; uops.glGetIntegerv = &mGetIntv;

        hs_ui_batch::UiBatcher ub;
        ub.SetOps(uops);
        ub.SetEnabled(true);

        hs_ui_batch::UiProgInfo info{};
        info.kind = hs_ui_batch::UiProgKind::UiBasic;
        info.eligible = true; info.locPos = 0; info.locUv = 1; info.locDiffuse = 2;
        info.locMatrixPVW = 3; info.locSampler0 = 4; info.samplerUnit0 = 0;
        info.hasPVW = true;
        for (int i = 0; i < 4; ++i) info.matPVW[i * 5] = 1.0f;

        ub.OnActiveTexture(hs_world_shadow::kGlTexture0);
        ub.OnBindTexture(hs_world_shadow::kGlTexture2D, 99u);
        ub.OnUseProgram(77u, &info);

        float pos[9] = { 0,0,0, 1,0,0, 0,1,0 };
        float uv[6]  = { 0,0, 1,0, 0,1 };
        float col[12] = { 1,1,1,1, 1,1,1,1, 1,1,1,1 };
        uint16_t idx[3] = { 0, 1, 2 };
        ub.OnEnableDisableVertexAttribArray(0, true);
        ub.OnVertexAttribPointer(0, 3, hs_world_shadow::kGlFloat, 0, 12, pos);
        ub.OnEnableDisableVertexAttribArray(1, true);
        ub.OnVertexAttribPointer(1, 2, hs_world_shadow::kGlFloat, 0, 8, uv);
        ub.OnEnableDisableVertexAttribArray(2, true);
        ub.OnVertexAttribPointer(2, 4, hs_world_shadow::kGlFloat, 0, 16, col);

        const ptrdiff_t expVBytes   = static_cast<ptrdiff_t>(3u * sizeof(hs_ui_batch::UiBatchedVertex));
        const ptrdiff_t expIAligned = static_cast<ptrdiff_t>((3u * sizeof(uint16_t) + 3u) & ~static_cast<size_t>(3u));

        // Step 1: MapBufferRange returns NULL -> fallback uploads [0, vBytes) via BufferSubData
        g_drv.mapReturnNull = true;
        bool a1 = ub.TryBatchDrawElements(hs_world_shadow::kGlTriangles, 3, hs_world_shadow::kGlUnsignedShort, idx);
        ub.Flush();
        bool s1Ok = a1 && (g_drv.bufferSubDataCalls == 2);

        // Step 2: MapBufferRange succeeds -> MUST map at offset == expVBytes / expIAligned (NEVER 0!)
        g_drv.mapReturnNull = false;
        bool a2 = ub.TryBatchDrawElements(hs_world_shadow::kGlTriangles, 3, hs_world_shadow::kGlUnsignedShort, idx);
        ub.Flush();
        bool s2Ok = a2 && (g_drv.lastVboMapOff == expVBytes) && (g_drv.lastIboMapOff == expIAligned);

        // Step 3: glUnmapBuffer returns 0 (corrupted unmap) -> MUST fall back to BufferSubData & advance cursor!
        const int subBefore3 = g_drv.bufferSubDataCalls;
        g_drv.unmapReturnFalse = true;
        bool a3 = ub.TryBatchDrawElements(hs_world_shadow::kGlTriangles, 3, hs_world_shadow::kGlUnsignedShort, idx);
        ub.Flush();
        g_drv.unmapReturnFalse = false;
        bool a4 = ub.TryBatchDrawElements(hs_world_shadow::kGlTriangles, 3, hs_world_shadow::kGlUnsignedShort, idx);
        ub.Flush();
        bool s3Ok = a3 && a4 && (g_drv.bufferSubDataCalls == subBefore3 + 2) &&
                    (g_drv.lastVboMapOff == expVBytes) && (g_drv.lastIboMapOff == expIAligned);

        // Step 4: Fallback without glBufferSubData shrinks buffer -> next MapBufferRange MUST re-orphan full capacity first!
        uops.glBufferSubData = nullptr;
        ub.SetOps(uops);
        g_drv.mapReturnNull = true;
        bool a5 = ub.TryBatchDrawElements(hs_world_shadow::kGlTriangles, 3, hs_world_shadow::kGlUnsignedShort, idx);
        ub.Flush();
        const bool shrunkOk = (g_drv.lastVboDataSize == expVBytes);
        g_drv.vboReOrphanedBeforeMap = false;
        g_drv.iboReOrphanedBeforeMap = false;
        g_drv.mapReturnNull = false;
        bool a6 = ub.TryBatchDrawElements(hs_world_shadow::kGlTriangles, 3, hs_world_shadow::kGlUnsignedShort, idx);
        ub.Flush();
        bool s4Ok = a5 && a6 && shrunkOk && g_drv.vboReOrphanedBeforeMap && g_drv.iboReOrphanedBeforeMap &&
                    (g_drv.lastVboMapOff == 0) && (g_drv.lastIboMapOff == 0);

        ub.ResetContextState(true);
        const bool passO = s1Ok && s2Ok && s3Ok && s4Ok && (g_drv.liveBuffers == 0);
        printf("  Case O (R3 P2 #7 UiBatcher cursor/unmap/shrink-reorphan): s1=%d s2=%d s3=%d s4=%d liveBufs=%d -> %s\n",
               s1Ok ? 1 : 0, s2Ok ? 1 : 0, s3Ok ? 1 : 0, s4Ok ? 1 : 0, g_drv.liveBuffers, passO ? "PASS" : "FAIL");
        if (!passO) allOk = false;
    }
    return allOk;
}

} // namespace mock_gl

// =============================================================================
// Case I (Round 2 P1 #1): AsyncSaveQueue Exclusive Lock + FlushFileBuffers Failure
// =============================================================================
static BOOL WINAPI FailingFlushFileBuffers(HANDLE) {
    SetLastError(ERROR_IO_DEVICE);
    return FALSE;
}

static std::string ReadFileAll(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    char buf[512]{};
    DWORD got = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    return std::string(buf, got);
}

static bool TestAsyncSaveVisibility() {
    printf("\n=== Round 2 Async Save Failure-Visibility Test (P1 #1) ===\n");
    wchar_t tmpDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmpDir);
    std::wstring target = std::wstring(tmpDir) + L"hs_round2_save_test_" + std::to_wstring(GetCurrentProcessId()) + L".sav";
    DeleteFileW(target.c_str());
    DeleteFileW((target + L".hc_tmp").c_str());

    hc_async_save::AsyncSaveQueue q;
    const char* v1 = "OLD_GOOD_SAVE_V1";
    DWORD winErr = 0;
    bool okSeed = q.CommitSyncAtomic(target.c_str(), v1, std::strlen(v1), &winErr);

    // I1: Exclusive lock on target file (exact Round 1 auditor repro!)
    HANDLE hLock = CreateFileW(target.c_str(), GENERIC_READ | GENERIC_WRITE, 0 /* exclusive */,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    bool lockOpen = (hLock != INVALID_HANDLE_VALUE);

    DWORD errSync = 0;
    const char* v2 = "NEW_SAVE_V2_SHOULD_FAIL";
    bool okSyncLocked = q.CommitSyncAtomic(target.c_str(), v2, std::strlen(v2), &errSync);

    DWORD errComp = 0;
    bool okCompLocked = q.CommitSyncCompressAtomic(target.c_str(), v2, std::strlen(v2), 6, &errComp);

    uint64_t seq = 0;
    bool enqOk = q.TryEnqueueEx(target.c_str(), v2, std::strlen(v2), &seq);
    bool waitOk = enqOk ? q.WaitForSeq(seq) : false;

    CloseHandle(hLock);
    std::string afterLock = ReadFileAll(target);
    bool passI1 = okSeed && lockOpen && (!okSyncLocked) && (errSync != 0) &&
                  (!okCompLocked) && (errComp != 0) &&
                  enqOk && (!waitOk) && (afterLock == v1);
    printf("  Case I1 (exclusive lock): syncOk=%d(err=%lu) compOk=%d(err=%lu) waitSeqOk=%d disk='%s' -> %s\n",
           okSyncLocked ? 1 : 0, static_cast<unsigned long>(errSync),
           okCompLocked ? 1 : 0, static_cast<unsigned long>(errComp),
           waitOk ? 1 : 0, afterLock.c_str(), passI1 ? "PASS" : "FAIL");

    // I2: FlushFileBuffers failure MUST fail CommitSyncAtomic and preserve old file!
    q.SetFlushFileBuffersFnForTest(&FailingFlushFileBuffers);
    DWORD errFlush = 0;
    const char* v3 = "UNFLUSHED_V3_SHOULD_FAIL";
    bool okFlushFail = q.CommitSyncAtomic(target.c_str(), v3, std::strlen(v3), &errFlush);
    WIN32_FIND_DATAW fdI2{};
    HANDLE hFindI2 = FindFirstFileW((target + L".hc_tmp*").c_str(), &fdI2);
    bool tmpExists = (hFindI2 != INVALID_HANDLE_VALUE);
    if (hFindI2 != INVALID_HANDLE_VALUE) FindClose(hFindI2);
    std::string afterFlush = ReadFileAll(target);
    bool passI2 = (!okFlushFail) && (errFlush == ERROR_IO_DEVICE) && (!tmpExists) && (afterFlush == v1);
    printf("  Case I2 (FlushFileBuffers=FALSE): ok=%d err=%lu tmpLeft=%d disk='%s' -> %s\n",
           okFlushFail ? 1 : 0, static_cast<unsigned long>(errFlush), tmpExists ? 1 : 0,
           afterFlush.c_str(), passI2 ? "PASS" : "FAIL");

    // I3: Restored real FlushFileBuffers commits cleanly!
    q.SetFlushFileBuffersFnForTest(nullptr);
    DWORD errOk = 0;
    const char* v4 = "VERIFIED_SAVE_V4";
    bool okGood = q.CommitSyncAtomic(target.c_str(), v4, std::strlen(v4), &errOk);
    std::string afterGood = ReadFileAll(target);
    bool passI3 = okGood && (errOk == 0) && (afterGood == v4);
    printf("  Case I3 (normal commit): ok=%d err=%lu disk='%s' -> %s\n",
           okGood ? 1 : 0, static_cast<unsigned long>(errOk), afterGood.c_str(), passI3 ? "PASS" : "FAIL");

    // Case L (Round 3 P1 #4): Concurrent Multi-Writer CommitSyncAtomic on SAME Target File (3 threads x 40 = 120 commits)
    bool passL = false;
    {
        struct WriterArg {
            hc_async_save::AsyncSaveQueue* q;
            std::wstring path;
            int writerId;
            int iters;
            std::atomic<int>* okCount;
            std::atomic<int>* failCount;
        };
        auto writerFn = [](LPVOID lp) -> DWORD {
            auto* a = static_cast<WriterArg*>(lp);
            for (int i = 0; i < a->iters; ++i) {
                char payload[96]{};
                std::snprintf(payload, sizeof(payload), "WRITER_%02d_ITER_%04d_PAYLOAD_END", a->writerId, i);
                DWORD err = 0;
                if (a->q->CommitSyncAtomic(a->path.c_str(), payload, std::strlen(payload), &err)) {
                    a->okCount->fetch_add(1, std::memory_order_relaxed);
                } else {
                    a->failCount->fetch_add(1, std::memory_order_relaxed);
                }
            }
            return 0;
        };
        constexpr int kThreads = 3;
        constexpr int kIters = 40;
        std::atomic<int> okCnt{0}, failCnt{0};
        WriterArg args[kThreads];
        HANDLE threads[kThreads]{};
        for (int t = 0; t < kThreads; ++t) {
            args[t] = WriterArg{ &q, target, t, kIters, &okCnt, &failCnt };
            threads[t] = CreateThread(nullptr, 0, writerFn, &args[t], 0, nullptr);
        }
        WaitForMultipleObjects(kThreads, threads, TRUE, 30000);
        for (int t = 0; t < kThreads; ++t) {
            if (threads[t]) CloseHandle(threads[t]);
        }
        std::string finalDisk = ReadFileAll(target);
        const bool validPayload = (finalDisk.size() == 31u) &&
                                  (finalDisk.compare(0, 7, "WRITER_") == 0) &&
                                  (finalDisk.compare(finalDisk.size() - 12, 12, "_PAYLOAD_END") == 0);
        WIN32_FIND_DATAW fd{};
        HANDLE hFind = FindFirstFileW((target + L".hc_tmp*").c_str(), &fd);
        const bool noLeftoverTmp = (hFind == INVALID_HANDLE_VALUE);
        if (hFind != INVALID_HANDLE_VALUE) FindClose(hFind);
        passL = (okCnt.load() == kThreads * kIters) && (failCnt.load() == 0) && validPayload && noLeftoverTmp;
        printf("  Case L (R3 P1 #4 concurrent 3x40 same-path commits): ok=%d/%d fail=%d validDisk=%d('%s') noTmp=%d -> %s\n",
               okCnt.load(), kThreads * kIters, failCnt.load(), validPayload ? 1 : 0, finalDisk.c_str(),
               noLeftoverTmp ? 1 : 0, passL ? "PASS" : "FAIL");
    }

    q.ShutdownAndFlush();
    DeleteFileW(target.c_str());
    DeleteFileW((target + L".hc_tmp").c_str());
    return passI1 && passI2 && passI3 && passL;
}

// =============================================================================
// Case J (Round 2 P2 #4): Dual Alive WGL Context A -> B -> A Program Reuse
// =============================================================================
static bool TestDualContextABAReuse() {
    printf("\n=== Round 2 Dual Alive Context A -> B -> A Test (P2 #4) ===\n");
    HMODULE hGl = LoadLibraryW(L"opengl32.dll");
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"hs_aba", 0, 0, 0, 128, 128, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    HDC hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.cColorBits = 32; pfd.cDepthBits = 24;
    SetPixelFormat(hdc, ChoosePixelFormat(hdc, &pfd), &pfd);
    using WglCreate_t = HGLRC (WINAPI*)(HDC);
    using WglDelete_t = BOOL (WINAPI*)(HGLRC);
    using WglMake_t   = BOOL (WINAPI*)(HDC, HGLRC);
    using WglGet_t    = PROC (WINAPI*)(LPCSTR);
    auto wCreate = reinterpret_cast<WglCreate_t>(GetProcAddress(hGl, "wglCreateContext"));
    auto wDelete = reinterpret_cast<WglDelete_t>(GetProcAddress(hGl, "wglDeleteContext"));
    auto wMake   = reinterpret_cast<WglMake_t>(GetProcAddress(hGl, "wglMakeCurrent"));
    auto wGet    = reinterpret_cast<WglGet_t>(GetProcAddress(hGl, "wglGetProcAddress"));
    HGLRC ctxA = wCreate(hdc);
    HGLRC ctxB = wCreate(hdc);
    wMake(hdc, ctxA);

    auto res = [&](const char* n) -> void* {
        void* p = reinterpret_cast<void*>(wGet(n));
        if (!p || reinterpret_cast<uintptr_t>(p) <= 3u) p = reinterpret_cast<void*>(GetProcAddress(hGl, n));
        return p;
    };
    hs_world_shadow::GlShadowOps ops{};
    ops.glGenFramebuffers = reinterpret_cast<decltype(ops.glGenFramebuffers)>(res("glGenFramebuffers"));
    ops.glDeleteFramebuffers = reinterpret_cast<decltype(ops.glDeleteFramebuffers)>(res("glDeleteFramebuffers"));
    ops.glBindFramebuffer = reinterpret_cast<decltype(ops.glBindFramebuffer)>(res("glBindFramebuffer"));
    ops.glFramebufferTexture2D = reinterpret_cast<decltype(ops.glFramebufferTexture2D)>(res("glFramebufferTexture2D"));
    ops.glCheckFramebufferStatus = reinterpret_cast<decltype(ops.glCheckFramebufferStatus)>(res("glCheckFramebufferStatus"));
    ops.glGenRenderbuffers = reinterpret_cast<decltype(ops.glGenRenderbuffers)>(res("glGenRenderbuffers"));
    ops.glDeleteRenderbuffers = reinterpret_cast<decltype(ops.glDeleteRenderbuffers)>(res("glDeleteRenderbuffers"));
    ops.glBindRenderbuffer = reinterpret_cast<decltype(ops.glBindRenderbuffer)>(res("glBindRenderbuffer"));
    ops.glRenderbufferStorage = reinterpret_cast<decltype(ops.glRenderbufferStorage)>(res("glRenderbufferStorage"));
    ops.glFramebufferRenderbuffer = reinterpret_cast<decltype(ops.glFramebufferRenderbuffer)>(res("glFramebufferRenderbuffer"));
    ops.glGenTextures = reinterpret_cast<decltype(ops.glGenTextures)>(res("glGenTextures"));
    ops.glDeleteTextures = reinterpret_cast<decltype(ops.glDeleteTextures)>(res("glDeleteTextures"));
    ops.glActiveTexture = reinterpret_cast<decltype(ops.glActiveTexture)>(res("glActiveTexture"));
    ops.glBindTexture = reinterpret_cast<decltype(ops.glBindTexture)>(res("glBindTexture"));
    ops.glTexImage2D = reinterpret_cast<decltype(ops.glTexImage2D)>(res("glTexImage2D"));
    ops.glTexParameteri = reinterpret_cast<decltype(ops.glTexParameteri)>(res("glTexParameteri"));
    ops.glCreateShader = reinterpret_cast<decltype(ops.glCreateShader)>(res("glCreateShader"));
    ops.glShaderSource = reinterpret_cast<decltype(ops.glShaderSource)>(res("glShaderSource"));
    ops.glCompileShader = reinterpret_cast<decltype(ops.glCompileShader)>(res("glCompileShader"));
    ops.glGetShaderiv = reinterpret_cast<decltype(ops.glGetShaderiv)>(res("glGetShaderiv"));
    ops.glGetShaderInfoLog = reinterpret_cast<decltype(ops.glGetShaderInfoLog)>(res("glGetShaderInfoLog"));
    ops.glDeleteShader = reinterpret_cast<decltype(ops.glDeleteShader)>(res("glDeleteShader"));
    ops.glCreateProgram = reinterpret_cast<decltype(ops.glCreateProgram)>(res("glCreateProgram"));
    ops.glAttachShader = reinterpret_cast<decltype(ops.glAttachShader)>(res("glAttachShader"));
    ops.glBindAttribLocation = reinterpret_cast<decltype(ops.glBindAttribLocation)>(res("glBindAttribLocation"));
    ops.glLinkProgram = reinterpret_cast<decltype(ops.glLinkProgram)>(res("glLinkProgram"));
    ops.glGetProgramiv = reinterpret_cast<decltype(ops.glGetProgramiv)>(res("glGetProgramiv"));
    ops.glGetProgramInfoLog = reinterpret_cast<decltype(ops.glGetProgramInfoLog)>(res("glGetProgramInfoLog"));
    ops.glDeleteProgram = reinterpret_cast<decltype(ops.glDeleteProgram)>(res("glDeleteProgram"));
    ops.glUseProgram = reinterpret_cast<decltype(ops.glUseProgram)>(res("glUseProgram"));
    ops.glGetUniformLocation = reinterpret_cast<decltype(ops.glGetUniformLocation)>(res("glGetUniformLocation"));
    ops.glUniform1f = reinterpret_cast<decltype(ops.glUniform1f)>(res("glUniform1f"));
    ops.glUniform1i = reinterpret_cast<decltype(ops.glUniform1i)>(res("glUniform1i"));
    ops.glUniform4fv = reinterpret_cast<decltype(ops.glUniform4fv)>(res("glUniform4fv"));
    ops.glUniformMatrix4fv = reinterpret_cast<decltype(ops.glUniformMatrix4fv)>(res("glUniformMatrix4fv"));
    ops.glGenBuffers = reinterpret_cast<decltype(ops.glGenBuffers)>(res("glGenBuffers"));
    ops.glDeleteBuffers = reinterpret_cast<decltype(ops.glDeleteBuffers)>(res("glDeleteBuffers"));
    ops.glBindBuffer = reinterpret_cast<decltype(ops.glBindBuffer)>(res("glBindBuffer"));
    ops.glBufferData = reinterpret_cast<decltype(ops.glBufferData)>(res("glBufferData"));
    ops.glEnableVertexAttribArray = reinterpret_cast<decltype(ops.glEnableVertexAttribArray)>(res("glEnableVertexAttribArray"));
    ops.glDisableVertexAttribArray = reinterpret_cast<decltype(ops.glDisableVertexAttribArray)>(res("glDisableVertexAttribArray"));
    ops.glVertexAttribPointer = reinterpret_cast<decltype(ops.glVertexAttribPointer)>(res("glVertexAttribPointer"));
    ops.glDrawArrays = reinterpret_cast<decltype(ops.glDrawArrays)>(res("glDrawArrays"));
    ops.glDrawElements = reinterpret_cast<decltype(ops.glDrawElements)>(res("glDrawElements"));
    ops.glViewport = reinterpret_cast<decltype(ops.glViewport)>(res("glViewport"));
    ops.glClearColor = reinterpret_cast<decltype(ops.glClearColor)>(res("glClearColor"));
    ops.glClear = reinterpret_cast<decltype(ops.glClear)>(res("glClear"));
    ops.glEnable = reinterpret_cast<decltype(ops.glEnable)>(res("glEnable"));
    ops.glDisable = reinterpret_cast<decltype(ops.glDisable)>(res("glDisable"));
    ops.glBlendFunc = reinterpret_cast<decltype(ops.glBlendFunc)>(res("glBlendFunc"));
    ops.glBlendFuncSeparate = reinterpret_cast<decltype(ops.glBlendFuncSeparate)>(res("glBlendFuncSeparate"));
    ops.glBlendEquation = reinterpret_cast<decltype(ops.glBlendEquation)>(res("glBlendEquation"));
    ops.glBlendEquationSeparate = reinterpret_cast<decltype(ops.glBlendEquationSeparate)>(res("glBlendEquationSeparate"));
    ops.glDepthFunc = reinterpret_cast<decltype(ops.glDepthFunc)>(res("glDepthFunc"));
    ops.glDepthMask = reinterpret_cast<decltype(ops.glDepthMask)>(res("glDepthMask"));
    ops.glColorMask = reinterpret_cast<decltype(ops.glColorMask)>(res("glColorMask"));
    ops.glGetIntegerv = reinterpret_cast<decltype(ops.glGetIntegerv)>(res("glGetIntegerv"));
    ops.glGetError = reinterpret_cast<decltype(ops.glGetError)>(res("glGetError"));

    auto makeMinimalProg = [&]() -> uint32_t {
        const char* vs = "#version 100\nattribute vec2 a; void main(){ gl_Position=vec4(a,0.0,1.0); }\n";
        const char* fs = "#version 100\nvoid main(){ gl_FragColor=vec4(1.0); }\n";
        uint32_t s1 = ops.glCreateShader(hs_world_shadow::kGlVertexShader);
        uint32_t s2 = ops.glCreateShader(hs_world_shadow::kGlFragmentShader);
        int32_t l1 = (int32_t)strlen(vs), l2 = (int32_t)strlen(fs);
        ops.glShaderSource(s1, 1, &vs, &l1); ops.glCompileShader(s1);
        ops.glShaderSource(s2, 1, &fs, &l2); ops.glCompileShader(s2);
        uint32_t p = ops.glCreateProgram();
        ops.glAttachShader(p, s1); ops.glAttachShader(p, s2);
        ops.glLinkProgram(p);
        ops.glDeleteShader(s1); ops.glDeleteShader(s2);
        return p;
    };

    // Simulate our per-context map preservation logic across real wglMakeCurrent(A -> B -> A):
    std::unordered_map<HGLRC, std::unordered_map<uint32_t, int>> ctxTables;
    std::unordered_map<uint32_t, int> activeTable;

    // 1) In Context A: link progA, bind it, run WorldShadowEngine Frame 1
    uint32_t progA = makeMinimalProg();
    ops.glUseProgram(progA);
    activeTable[progA] = 1; // Ground/AnimWorld role in A

    hs_world_shadow::WorldShadowEngine eng;
    eng.SetOps(ops, true);
    hs_gl_cache::ThreadState glCache;
    glCache.OnMakeCurrent(ctxA);
    const int32_t vp[4] = { 0, 0, 128, 128 };
    hs_world_shadow::ShadowConfig cfg{};
    cfg.projOn = 1.0f; cfg.density = 0.6f; cfg.hdMode = false;
    hs_world_shadow::ShadowPassState saved{};
    saved.dstFbo = 0u; saved.dstProg = progA; memcpy(saved.vp, vp, sizeof(vp));

    eng.OnGroundOrOceanDraw(0u, vp);
    bool readyA1 = eng.OnBeforeFirstStandingEntity(cfg, saved, glCache);
    eng.OnEndFrame();

    // 2) Switch A -> B: release scratch FBOs while A current, save A's program table, switch to B
    eng.ResetContext(true);
    bool swAB = (wMake(hdc, ctxB) != FALSE);
    ctxTables[ctxA] = std::move(activeTable);
    activeTable.clear();
    glCache.OnMakeCurrent(ctxB);

    // 3) Switch B -> A: save B, restore A's table WITHOUT re-linking progA, query GL_CURRENT_PROGRAM!
    eng.ResetContext(true);
    bool swBA = (wMake(hdc, ctxA) != FALSE);
    ctxTables[ctxB] = std::move(activeTable);
    activeTable = std::move(ctxTables[ctxA]);
    glCache.OnMakeCurrent(ctxA);

    int32_t curProgAfterReturn = 0;
    ops.glGetIntegerv(hs_world_shadow::kGlCurrentProgram, &curProgAfterReturn);
    const bool reusedRoleFound = (activeTable.find(static_cast<uint32_t>(curProgAfterReturn)) != activeTable.end());

    // 4) Reuse progA directly in A without re-linking -> WorldShadowEngine MUST recover cleanly!
    eng.OnGroundOrOceanDraw(0u, vp);
    bool readyA2 = eng.OnBeforeFirstStandingEntity(cfg, saved, glCache);
    eng.OnEndFrame();
    int32_t curProgPostShadow = 0;
    ops.glGetIntegerv(hs_world_shadow::kGlCurrentProgram, &curProgPostShadow);
    eng.ResetContext(true);
    ops.glDeleteProgram(progA);

    wMake(nullptr, nullptr);
    wDelete(ctxB);
    wDelete(ctxA);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);

    const bool passJ = swAB && swBA && readyA1 && reusedRoleFound && readyA2 &&
                       (static_cast<uint32_t>(curProgAfterReturn) == progA) &&
                       (static_cast<uint32_t>(curProgPostShadow) == progA);
    printf("  Case J (Real WGL A->B->A): readyA1=%d reusedProg=%d(exp %u) roleFound=%d readyA2=%d postProg=%d -> %s\n",
           readyA1 ? 1 : 0, curProgAfterReturn, progA, reusedRoleFound ? 1 : 0,
           readyA2 ? 1 : 0, curProgPostShadow, passJ ? "PASS" : "FAIL");
    return passJ;
}

static bool TestNativeOpenGl() {
    HMODULE hGl = LoadLibraryW(L"opengl32.dll");
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"hs_gl", 0, 0, 0, 256, 256, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    HDC hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd); pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.cColorBits = 32; pfd.cDepthBits = 24;
    int pf = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pf, &pfd);
    using WglCreate_t = HGLRC (WINAPI*)(HDC);
    using WglMake_t   = BOOL (WINAPI*)(HDC, HGLRC);
    using WglGet_t    = PROC (WINAPI*)(LPCSTR);
    auto wCreate = reinterpret_cast<WglCreate_t>(GetProcAddress(hGl, "wglCreateContext"));
    auto wMake   = reinterpret_cast<WglMake_t>(GetProcAddress(hGl, "wglMakeCurrent"));
    auto wGet    = reinterpret_cast<WglGet_t>(GetProcAddress(hGl, "wglGetProcAddress"));
    HGLRC rc = wCreate(hdc);
    wMake(hdc, rc);

    auto res = [&](const char* n) -> void* {
        void* p = reinterpret_cast<void*>(wGet(n));
        if (!p || reinterpret_cast<uintptr_t>(p) <= 3u) p = reinterpret_cast<void*>(GetProcAddress(hGl, n));
        return p;
    };

    BackendEnv env{};
    env.isNativeGl = true;
    env.name = "Native Desktop OpenGL 4.6 (GL_MAX mode)";
    auto& o = env.ops;
    o.glGenFramebuffers          = reinterpret_cast<decltype(o.glGenFramebuffers)>(res("glGenFramebuffers"));
    o.glDeleteFramebuffers       = reinterpret_cast<decltype(o.glDeleteFramebuffers)>(res("glDeleteFramebuffers"));
    o.glBindFramebuffer          = reinterpret_cast<decltype(o.glBindFramebuffer)>(res("glBindFramebuffer"));
    o.glFramebufferTexture2D     = reinterpret_cast<decltype(o.glFramebufferTexture2D)>(res("glFramebufferTexture2D"));
    o.glCheckFramebufferStatus   = reinterpret_cast<decltype(o.glCheckFramebufferStatus)>(res("glCheckFramebufferStatus"));
    o.glGenRenderbuffers         = reinterpret_cast<decltype(o.glGenRenderbuffers)>(res("glGenRenderbuffers"));
    o.glDeleteRenderbuffers      = reinterpret_cast<decltype(o.glDeleteRenderbuffers)>(res("glDeleteRenderbuffers"));
    o.glBindRenderbuffer         = reinterpret_cast<decltype(o.glBindRenderbuffer)>(res("glBindRenderbuffer"));
    o.glRenderbufferStorage      = reinterpret_cast<decltype(o.glRenderbufferStorage)>(res("glRenderbufferStorage"));
    o.glFramebufferRenderbuffer  = reinterpret_cast<decltype(o.glFramebufferRenderbuffer)>(res("glFramebufferRenderbuffer"));
    o.glGenTextures              = reinterpret_cast<decltype(o.glGenTextures)>(res("glGenTextures"));
    o.glDeleteTextures           = reinterpret_cast<decltype(o.glDeleteTextures)>(res("glDeleteTextures"));
    o.glActiveTexture            = reinterpret_cast<decltype(o.glActiveTexture)>(res("glActiveTexture"));
    o.glBindTexture              = reinterpret_cast<decltype(o.glBindTexture)>(res("glBindTexture"));
    o.glTexImage2D               = reinterpret_cast<decltype(o.glTexImage2D)>(res("glTexImage2D"));
    o.glTexParameteri            = reinterpret_cast<decltype(o.glTexParameteri)>(res("glTexParameteri"));
    o.glCreateShader             = reinterpret_cast<decltype(o.glCreateShader)>(res("glCreateShader"));
    o.glShaderSource             = reinterpret_cast<decltype(o.glShaderSource)>(res("glShaderSource"));
    o.glCompileShader            = reinterpret_cast<decltype(o.glCompileShader)>(res("glCompileShader"));
    o.glGetShaderiv              = reinterpret_cast<decltype(o.glGetShaderiv)>(res("glGetShaderiv"));
    o.glGetShaderInfoLog         = reinterpret_cast<decltype(o.glGetShaderInfoLog)>(res("glGetShaderInfoLog"));
    o.glDeleteShader             = reinterpret_cast<decltype(o.glDeleteShader)>(res("glDeleteShader"));
    o.glCreateProgram            = reinterpret_cast<decltype(o.glCreateProgram)>(res("glCreateProgram"));
    o.glAttachShader             = reinterpret_cast<decltype(o.glAttachShader)>(res("glAttachShader"));
    o.glBindAttribLocation       = reinterpret_cast<decltype(o.glBindAttribLocation)>(res("glBindAttribLocation"));
    o.glLinkProgram              = reinterpret_cast<decltype(o.glLinkProgram)>(res("glLinkProgram"));
    o.glGetProgramiv             = reinterpret_cast<decltype(o.glGetProgramiv)>(res("glGetProgramiv"));
    o.glGetProgramInfoLog        = reinterpret_cast<decltype(o.glGetProgramInfoLog)>(res("glGetProgramInfoLog"));
    o.glDeleteProgram            = reinterpret_cast<decltype(o.glDeleteProgram)>(res("glDeleteProgram"));
    o.glUseProgram               = reinterpret_cast<decltype(o.glUseProgram)>(res("glUseProgram"));
    o.glGetUniformLocation       = reinterpret_cast<decltype(o.glGetUniformLocation)>(res("glGetUniformLocation"));
    o.glUniform1f                = reinterpret_cast<decltype(o.glUniform1f)>(res("glUniform1f"));
    o.glUniform1i                = reinterpret_cast<decltype(o.glUniform1i)>(res("glUniform1i"));
    o.glUniform4fv               = reinterpret_cast<decltype(o.glUniform4fv)>(res("glUniform4fv"));
    o.glUniformMatrix4fv         = reinterpret_cast<decltype(o.glUniformMatrix4fv)>(res("glUniformMatrix4fv"));
    o.glGenBuffers               = reinterpret_cast<decltype(o.glGenBuffers)>(res("glGenBuffers"));
    o.glDeleteBuffers            = reinterpret_cast<decltype(o.glDeleteBuffers)>(res("glDeleteBuffers"));
    o.glBindBuffer               = reinterpret_cast<decltype(o.glBindBuffer)>(res("glBindBuffer"));
    o.glBufferData               = reinterpret_cast<decltype(o.glBufferData)>(res("glBufferData"));
    o.glEnableVertexAttribArray  = reinterpret_cast<decltype(o.glEnableVertexAttribArray)>(res("glEnableVertexAttribArray"));
    o.glDisableVertexAttribArray = reinterpret_cast<decltype(o.glDisableVertexAttribArray)>(res("glDisableVertexAttribArray"));
    o.glVertexAttribPointer      = reinterpret_cast<decltype(o.glVertexAttribPointer)>(res("glVertexAttribPointer"));
    o.glDrawArrays               = reinterpret_cast<decltype(o.glDrawArrays)>(res("glDrawArrays"));
    o.glDrawElements             = reinterpret_cast<decltype(o.glDrawElements)>(res("glDrawElements"));
    o.glViewport                 = reinterpret_cast<decltype(o.glViewport)>(res("glViewport"));
    o.glClearColor               = reinterpret_cast<decltype(o.glClearColor)>(res("glClearColor"));
    o.glClear                    = reinterpret_cast<decltype(o.glClear)>(res("glClear"));
    o.glEnable                   = reinterpret_cast<decltype(o.glEnable)>(res("glEnable"));
    o.glDisable                  = reinterpret_cast<decltype(o.glDisable)>(res("glDisable"));
    o.glBlendFunc                = reinterpret_cast<decltype(o.glBlendFunc)>(res("glBlendFunc"));
    o.glBlendFuncSeparate        = reinterpret_cast<decltype(o.glBlendFuncSeparate)>(res("glBlendFuncSeparate"));
    o.glBlendEquation            = reinterpret_cast<decltype(o.glBlendEquation)>(res("glBlendEquation"));
    o.glBlendEquationSeparate    = reinterpret_cast<decltype(o.glBlendEquationSeparate)>(res("glBlendEquationSeparate"));
    o.glDepthFunc                = reinterpret_cast<decltype(o.glDepthFunc)>(res("glDepthFunc"));
    o.glDepthMask                = reinterpret_cast<decltype(o.glDepthMask)>(res("glDepthMask"));
    o.glColorMask                = reinterpret_cast<decltype(o.glColorMask)>(res("glColorMask"));
    o.glGetIntegerv              = reinterpret_cast<decltype(o.glGetIntegerv)>(res("glGetIntegerv"));
    o.glGetError                 = reinterpret_cast<decltype(o.glGetError)>(res("glGetError"));
    o.logFn                      = &TestLog;
    env.glReadPixels             = reinterpret_cast<GlReadPixels_t>(res("glReadPixels"));
    env.glGetIntegerv            = reinterpret_cast<GlGetIntegerv_t>(res("glGetIntegerv"));
    env.glGetBufferParameteriv   = reinterpret_cast<GlGetBufferParameteriv_t>(res("glGetBufferParameteriv"));
    bool ok = RunPixelExactSuite(env);
    wMake(nullptr, nullptr);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return ok;
}

static bool TestAngleD3d9() {
    SetDllDirectoryW(L"D:\\steam\\steamapps\\common\\Don't Starve Together\\bin64");
    HMODULE hEgl = LoadLibraryW(L"libEGL.dll");
    HMODULE hGl  = LoadLibraryW(L"libGLESv2.dll");
    if (!hEgl || !hGl) { printf("[FAIL] Could not load ANGLE DLLs\n"); return false; }

    using EglGetDisplay_t = void* (WINAPI*)(HDC);
    using EglInit_t = uint32_t (WINAPI*)(void*, int*, int*);
    using EglChoose_t = uint32_t (WINAPI*)(void*, const int*, void**, int, int*);
    using EglCreateSurf_t = void* (WINAPI*)(void*, void*, HWND, const int*);
    using EglCreateCtx_t = void* (WINAPI*)(void*, void*, void*, const int*);
    using EglMakeCur_t = uint32_t (WINAPI*)(void*, void*, void*, void*);

    HWND hwnd = CreateWindowExW(0, L"STATIC", L"hs_angle", 0, 0, 0, 256, 256, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    HDC hdc = GetDC(hwnd);
    void* dpy = reinterpret_cast<EglGetDisplay_t>(GetProcAddress(hEgl, "eglGetDisplay"))(hdc);
    int maj = 0, min = 0;
    reinterpret_cast<EglInit_t>(GetProcAddress(hEgl, "eglInitialize"))(dpy, &maj, &min);
    const int cfgA[] = { 0x3024, 8, 0x3023, 8, 0x3022, 8, 0x3021, 8, 0x3040, 4, 0x3038 };
    void* cfg = nullptr; int n = 0;
    reinterpret_cast<EglChoose_t>(GetProcAddress(hEgl, "eglChooseConfig"))(dpy, cfgA, &cfg, 1, &n);
    void* surf = reinterpret_cast<EglCreateSurf_t>(GetProcAddress(hEgl, "eglCreateWindowSurface"))(dpy, cfg, hwnd, nullptr);
    const int ctxA[] = { 0x3098, 2, 0x3038 };
    void* ctx = reinterpret_cast<EglCreateCtx_t>(GetProcAddress(hEgl, "eglCreateContext"))(dpy, cfg, nullptr, ctxA);
    reinterpret_cast<EglMakeCur_t>(GetProcAddress(hEgl, "eglMakeCurrent"))(dpy, surf, surf, ctx);

    auto res = [&](const char* nm) -> void* { return reinterpret_cast<void*>(GetProcAddress(hGl, nm)); };
    BackendEnv env{};
    env.isNativeGl = false;
    env.name = "ANGLE 1.0.0.2249 D3D9 (DEPTH16 mode)";
    auto& o = env.ops;
    o.glGenFramebuffers          = reinterpret_cast<decltype(o.glGenFramebuffers)>(res("glGenFramebuffers"));
    o.glDeleteFramebuffers       = reinterpret_cast<decltype(o.glDeleteFramebuffers)>(res("glDeleteFramebuffers"));
    o.glBindFramebuffer          = reinterpret_cast<decltype(o.glBindFramebuffer)>(res("glBindFramebuffer"));
    o.glFramebufferTexture2D     = reinterpret_cast<decltype(o.glFramebufferTexture2D)>(res("glFramebufferTexture2D"));
    o.glCheckFramebufferStatus   = reinterpret_cast<decltype(o.glCheckFramebufferStatus)>(res("glCheckFramebufferStatus"));
    o.glGenRenderbuffers         = reinterpret_cast<decltype(o.glGenRenderbuffers)>(res("glGenRenderbuffers"));
    o.glDeleteRenderbuffers      = reinterpret_cast<decltype(o.glDeleteRenderbuffers)>(res("glDeleteRenderbuffers"));
    o.glBindRenderbuffer         = reinterpret_cast<decltype(o.glBindRenderbuffer)>(res("glBindRenderbuffer"));
    o.glRenderbufferStorage      = reinterpret_cast<decltype(o.glRenderbufferStorage)>(res("glRenderbufferStorage"));
    o.glFramebufferRenderbuffer  = reinterpret_cast<decltype(o.glFramebufferRenderbuffer)>(res("glFramebufferRenderbuffer"));
    o.glGenTextures              = reinterpret_cast<decltype(o.glGenTextures)>(res("glGenTextures"));
    o.glDeleteTextures           = reinterpret_cast<decltype(o.glDeleteTextures)>(res("glDeleteTextures"));
    o.glActiveTexture            = reinterpret_cast<decltype(o.glActiveTexture)>(res("glActiveTexture"));
    o.glBindTexture              = reinterpret_cast<decltype(o.glBindTexture)>(res("glBindTexture"));
    o.glTexImage2D               = reinterpret_cast<decltype(o.glTexImage2D)>(res("glTexImage2D"));
    o.glTexParameteri            = reinterpret_cast<decltype(o.glTexParameteri)>(res("glTexParameteri"));
    o.glCreateShader             = reinterpret_cast<decltype(o.glCreateShader)>(res("glCreateShader"));
    o.glShaderSource             = reinterpret_cast<decltype(o.glShaderSource)>(res("glShaderSource"));
    o.glCompileShader            = reinterpret_cast<decltype(o.glCompileShader)>(res("glCompileShader"));
    o.glGetShaderiv              = reinterpret_cast<decltype(o.glGetShaderiv)>(res("glGetShaderiv"));
    o.glGetShaderInfoLog         = reinterpret_cast<decltype(o.glGetShaderInfoLog)>(res("glGetShaderInfoLog"));
    o.glDeleteShader             = reinterpret_cast<decltype(o.glDeleteShader)>(res("glDeleteShader"));
    o.glCreateProgram            = reinterpret_cast<decltype(o.glCreateProgram)>(res("glCreateProgram"));
    o.glAttachShader             = reinterpret_cast<decltype(o.glAttachShader)>(res("glAttachShader"));
    o.glBindAttribLocation       = reinterpret_cast<decltype(o.glBindAttribLocation)>(res("glBindAttribLocation"));
    o.glLinkProgram              = reinterpret_cast<decltype(o.glLinkProgram)>(res("glLinkProgram"));
    o.glGetProgramiv             = reinterpret_cast<decltype(o.glGetProgramiv)>(res("glGetProgramiv"));
    o.glGetProgramInfoLog        = reinterpret_cast<decltype(o.glGetProgramInfoLog)>(res("glGetProgramInfoLog"));
    o.glDeleteProgram            = reinterpret_cast<decltype(o.glDeleteProgram)>(res("glDeleteProgram"));
    o.glUseProgram               = reinterpret_cast<decltype(o.glUseProgram)>(res("glUseProgram"));
    o.glGetUniformLocation       = reinterpret_cast<decltype(o.glGetUniformLocation)>(res("glGetUniformLocation"));
    o.glUniform1f                = reinterpret_cast<decltype(o.glUniform1f)>(res("glUniform1f"));
    o.glUniform1i                = reinterpret_cast<decltype(o.glUniform1i)>(res("glUniform1i"));
    o.glUniform4fv               = reinterpret_cast<decltype(o.glUniform4fv)>(res("glUniform4fv"));
    o.glUniformMatrix4fv         = reinterpret_cast<decltype(o.glUniformMatrix4fv)>(res("glUniformMatrix4fv"));
    o.glGenBuffers               = reinterpret_cast<decltype(o.glGenBuffers)>(res("glGenBuffers"));
    o.glDeleteBuffers            = reinterpret_cast<decltype(o.glDeleteBuffers)>(res("glDeleteBuffers"));
    o.glBindBuffer               = reinterpret_cast<decltype(o.glBindBuffer)>(res("glBindBuffer"));
    o.glBufferData               = reinterpret_cast<decltype(o.glBufferData)>(res("glBufferData"));
    o.glEnableVertexAttribArray  = reinterpret_cast<decltype(o.glEnableVertexAttribArray)>(res("glEnableVertexAttribArray"));
    o.glDisableVertexAttribArray = reinterpret_cast<decltype(o.glDisableVertexAttribArray)>(res("glDisableVertexAttribArray"));
    o.glVertexAttribPointer      = reinterpret_cast<decltype(o.glVertexAttribPointer)>(res("glVertexAttribPointer"));
    o.glDrawArrays               = reinterpret_cast<decltype(o.glDrawArrays)>(res("glDrawArrays"));
    o.glDrawElements             = reinterpret_cast<decltype(o.glDrawElements)>(res("glDrawElements"));
    o.glViewport                 = reinterpret_cast<decltype(o.glViewport)>(res("glViewport"));
    o.glClearColor               = reinterpret_cast<decltype(o.glClearColor)>(res("glClearColor"));
    o.glClear                    = reinterpret_cast<decltype(o.glClear)>(res("glClear"));
    o.glEnable                   = reinterpret_cast<decltype(o.glEnable)>(res("glEnable"));
    o.glDisable                  = reinterpret_cast<decltype(o.glDisable)>(res("glDisable"));
    o.glBlendFunc                = reinterpret_cast<decltype(o.glBlendFunc)>(res("glBlendFunc"));
    o.glBlendFuncSeparate        = reinterpret_cast<decltype(o.glBlendFuncSeparate)>(res("glBlendFuncSeparate"));
    o.glBlendEquation            = reinterpret_cast<decltype(o.glBlendEquation)>(res("glBlendEquation"));
    o.glBlendEquationSeparate    = reinterpret_cast<decltype(o.glBlendEquationSeparate)>(res("glBlendEquationSeparate"));
    o.glDepthFunc                = reinterpret_cast<decltype(o.glDepthFunc)>(res("glDepthFunc"));
    o.glDepthMask                = reinterpret_cast<decltype(o.glDepthMask)>(res("glDepthMask"));
    o.glColorMask                = reinterpret_cast<decltype(o.glColorMask)>(res("glColorMask"));
    o.glGetIntegerv              = reinterpret_cast<decltype(o.glGetIntegerv)>(res("glGetIntegerv"));
    o.glGetError                 = reinterpret_cast<decltype(o.glGetError)>(res("glGetError"));
    o.logFn                      = &TestLog;
    env.glReadPixels             = reinterpret_cast<GlReadPixels_t>(res("glReadPixels"));
    env.glGetIntegerv            = reinterpret_cast<GlGetIntegerv_t>(res("glGetIntegerv"));
    env.glGetBufferParameteriv   = reinterpret_cast<GlGetBufferParameteriv_t>(res("glGetBufferParameteriv"));
    bool ok = RunPixelExactSuite(env);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return ok;
}

static bool TestM1M2M4UnitSuite() {
    printf("\n=== v2.9.5 M1 + M2 + M4 Native Unit Suite ===\n");
    // M1: gl_state_cache.h StencilFunc / StencilMask / StencilOp / PolygonOffset dedup + ResetBindingsOnly
    bool m1Ok = false;
    {
        hs_gl_cache::ThreadState st;
        bool sf1 = st.ShouldCallStencilFunc(0x0207u, 1, 0xFFu);
        bool sf2 = st.ShouldCallStencilFunc(0x0207u, 1, 0xFFu);
        bool sf3 = st.ShouldCallStencilFunc(0x0202u, 1, 0xFFu);
        bool sm1 = st.ShouldCallStencilMask(0xFFu);
        bool sm2 = st.ShouldCallStencilMask(0xFFu);
        bool so1 = st.ShouldCallStencilOp(0x1E00u, 0x1E00u, 0x1E01u);
        bool so2 = st.ShouldCallStencilOp(0x1E00u, 0x1E00u, 0x1E01u);
        bool po1 = st.ShouldCallPolygonOffset(-1.0f, -2.0f);
        bool po2 = st.ShouldCallPolygonOffset(-1.0f, -2.0f);
        bool poNan = st.ShouldCallPolygonOffset(std::nanf(""), 0.0f);
        st.ResetBindingsOnly();
        bool sfAfterReset = st.ShouldCallStencilFunc(0x0202u, 1, 0xFFu);
        bool poAfterReset = st.ShouldCallPolygonOffset(-1.0f, -2.0f);
        // Non-mutating peek + commit ordering check (prevents stale glCache capture during pending batch flush!)
        st.RecordColorMask(1, 1, 1, 1);
        st.RecordEnableDisable(hs_gl_cache::kGlStencilTest, true);
        st.RecordViewport(0, 0, 1280, 720);
        const bool cmMiss = !st.IsColorMaskRedundant(0, 0, 0, 0);
        const bool cmStillOld = (st.colorMask == 15); // MUST still be (1,1,1,1) before flush!
        const bool stMiss = !st.IsEnableDisableRedundant(hs_gl_cache::kGlStencilTest, false);
        const bool stStillOld = (st.caps[4] == 1);    // MUST still be ON before flush!
        const bool vpMiss = !st.IsViewportRedundant(0, 0, 512, 512);
        const bool vpStillOld = (st.vpValid && st.vp[2] == 1280 && st.vp[3] == 720);
        st.RecordColorMask(0, 0, 0, 0);
        const bool cmHit = st.IsColorMaskRedundant(0, 0, 0, 0);
        const bool peekOrderOk = cmMiss && cmStillOld && stMiss && stStillOld && vpMiss && vpStillOld && cmHit;
        m1Ok = sf1 && !sf2 && sf3 && sm1 && !sm2 && so1 && !so2 && po1 && !po2 && poNan &&
               sfAfterReset && poAfterReset && peekOrderOk;
        printf("  M1 GLStateCache Stencil/PolyOff + NonMutatingPeek: sf(%d,%d,%d) sm(%d,%d) so(%d,%d) po(%d,%d,nan=%d) reset(%d,%d) peekOrder=%d -> %s\n",
               sf1, sf2, sf3, sm1, sm2, so1, so2, po1, po2, poNan, sfAfterReset, poAfterReset, peekOrderOk ? 1 : 0, m1Ok ? "PASS" : "FAIL");
    }

    // M2: CrashDetective bilingual report + Lua Mod blame + CursorClipGuard safety truth table
    bool m2Ok = false;
    {
        using CG = hs_crash_util::CursorClipGuard;
        bool cAll   = CG::EvaluateShouldClip(true,  true,  true,  false, true);
        bool cOff   = CG::EvaluateShouldClip(false, true,  true,  false, true);
        bool cPause = CG::EvaluateShouldClip(true,  false, true,  false, true);
        bool cBg    = CG::EvaluateShouldClip(true,  true,  false, false, true);
        bool cMin   = CG::EvaluateShouldClip(true,  true,  true,  true,  true);
        bool cRect  = CG::EvaluateShouldClip(true,  true,  true,  false, false);

        hs_crash_util::CrashDetective det;
        det.UpdateTelemetry("opengl", true, 239.5f, 198.2f, 2.4f, 0.3f);
        det.ReportLuaError(100000ULL, "workshop-987654321", "Test Mod Alpha", "attempt to index field 'bad' (a nil value)");
        void* fakeFrames[2] = { reinterpret_cast<void*>(&TestM1M2M4UnitSuite), reinterpret_cast<void*>(&TestLog) };
        char buf[4096]{};
        size_t len = det.FormatReport(buf, sizeof(buf), "UnitTest", EXCEPTION_ACCESS_VIOLATION,
                                      reinterpret_cast<const void*>(&TestM1M2M4UnitSuite),
                                      fakeFrames, 2u, 102000ULL);
        bool repOk = (len > 200u) &&
                     (std::strstr(buf, "EXCEPTION_ACCESS_VIOLATION") != nullptr) &&
                     (std::strstr(buf, "SUSPECTED MOD") != nullptr) &&
                     (std::strstr(buf, "workshop-987654321") != nullptr) &&
                     (std::strstr(buf, "Test Mod Alpha") != nullptr) &&
                     (std::strstr(buf, "#00") != nullptr);
        m2Ok = cAll && !cOff && !cPause && !cBg && !cMin && !cRect && repOk;
        printf("  M2 CrashDetective+ClipCursor: clipTruth=%d reportLen=%zu blameOk=%d -> %s\n",
               (cAll && !cOff && !cPause && !cBg && !cMin && !cRect) ? 1 : 0, len, repOk ? 1 : 0,
               m2Ok ? "PASS" : "FAIL");
    }

    // M4: FoliageTable O(1) L1 Spatial Hash (cell-boundary hits, strict rigid neighbor rejection, Clear)
    bool m4Ok = false;
    {
        hs_foliage::FoliageTable ft;
        bool ins1 = ft.Insert(1.49f, 2.51f, hs_foliage::SoftBrush);
        bool ins2 = ft.Insert(-12.00f, 8.75f, hs_foliage::TallTree);
        bool insBad = ft.Insert(0.0f, 0.0f, hs_foliage::Rigid); // must reject Rigid
        // Query across cell boundary (1.49 vs 1.52 -> dist = 0.036 < 0.12)
        uint8_t qBrush = ft.QueryClass(1.52f, 2.49f);
        uint8_t qTree  = ft.QueryClass(-12.02f, 8.76f);
        // Adjacent player/wall only 0.15 units away MUST return Rigid(0) under tightened 0.12m radius!
        uint8_t qClose = ft.QueryClass(1.49f + 0.15f, 2.51f);
        uint8_t qWall  = ft.QueryClass(1.49f + 0.60f, 2.51f);
        uint8_t qFar   = ft.QueryClass(50.0f, 50.0f);
        ft.Clear();
        uint8_t qClr   = ft.QueryClass(1.49f, 2.51f);
        // Smooth CPU ComputeEntitySwayBase continuity across dot(dp, camRight) == 0 (NO +/-1 snap tear!)
        hs_foliage::WindFieldState wf{};
        wf.windTime = 3.0f; wf.swayAmp = 0.50f; wf.wakeAmp = 1.00f;
        wf.playerWx = 10.0f; wf.playerWz = 10.0f; wf.hasPlayer = 1.0f;
        float swNeg = 0.0f, wkNeg = 0.0f, swZero = 0.0f, wkZero = 0.0f, swPos = 0.0f, wkPos = 0.0f;
        float swFar = 0.0f, wkFar = 0.0f;
        hs_foliage::ComputeEntitySwayBase(hs_foliage::SoftBrush,  9.98f, 10.0f, wf, 1.0f, 0.0f, swNeg,  wkNeg);
        hs_foliage::ComputeEntitySwayBase(hs_foliage::SoftBrush, 10.00f, 10.0f, wf, 1.0f, 0.0f, swZero, wkZero);
        hs_foliage::ComputeEntitySwayBase(hs_foliage::SoftBrush, 10.02f, 10.0f, wf, 1.0f, 0.0f, swPos,  wkPos);
        hs_foliage::ComputeEntitySwayBase(hs_foliage::SoftBrush, 10.80f, 10.0f, wf, 1.0f, 0.0f, swFar,  wkFar);
        const bool wakeContOk = (std::fabs(wkZero) < 1e-4f) &&
                                (wkNeg < 0.0f && wkPos > 0.0f) &&
                                (std::fabs(wkPos - wkNeg) < 0.04f) && // continuous slope, NOT 0.84m jump!
                                (wkFar > 0.20f);                      // strong smooth parting at 0.8m!
        m4Ok = ins1 && ins2 && !insBad &&
               (qBrush == hs_foliage::SoftBrush) && (qTree == hs_foliage::TallTree) &&
               (qClose == hs_foliage::Rigid) && (qWall == hs_foliage::Rigid) &&
               (qFar == hs_foliage::Rigid) && (qClr == hs_foliage::Rigid) &&
               (ft.Count() == 0u) && wakeContOk;
        printf("  M4 FoliageTable+SmoothWake: brush=%u tree=%u close015=%u wall=%u clr=%u wk(%.3f,%.3f,%.3f,far=%.3f) -> %s\n",
               qBrush, qTree, qClose, qWall, qClr, wkNeg, wkZero, wkPos, wkFar, m4Ok ? "PASS" : "FAIL");
    }
    return m1Ok && m2Ok && m4Ok;
}

static bool TestDxgiFlipLiveGpu() {
    printf("\n=== Live RTX GPU Test: WGL_NV_DX_interop2 DXGI Flip-Model ===\n");
    hs_native_gl::WglRuntime rt{};
    if (!rt.Load()) {
        printf("  [FAIL] WglRuntime::Load failed\n");
        return false;
    }

    // 1. Live hardware probe (must succeed with AllowTearing on RTX 4060)
    const auto probe = hs_dxgi_flip::ProbeDxgiFlipSupport(hs_dxgi_flip::FlipMode::Auto, rt);
    printf("  1. ProbeDxgiFlipSupport: supported=%d allowTearing=%d reason=%s\n",
           probe.supported ? 1 : 0, probe.allowTearing ? 1 : 0, probe.reason.c_str());
    if (!probe.supported) {
        printf("  [FAIL] ProbeDxgiFlipSupport failed on NVIDIA GPU!\n");
        return false;
    }

    // 2. Create real Win32 test window (client rect 256x256)
    RECT wr{0, 0, 256, 256};
    AdjustWindowRect(&wr, WS_POPUP, FALSE);
    HWND gameHwnd = CreateWindowExW(0, L"STATIC", L"DxgiFlipGpuTest", WS_POPUP,
                                    -2000, -2000, wr.right - wr.left, wr.bottom - wr.top,
                                    nullptr, nullptr, nullptr, nullptr);
    if (!gameHwnd) {
        printf("  [FAIL] CreateWindowExW failed\n");
        return false;
    }

    hs_dxgi_flip::DxgiFlipPresenter flip;
    std::string reason;
    if (!flip.InitSwapChain(rt, gameHwnd, reason)) {
        printf("  [FAIL] InitSwapChain failed: %s\n", reason.c_str());
        DestroyWindow(gameHwnd);
        return false;
    }

    // Verify real gameHwnd NEVER received legacy WGL SetPixelFormat!
    HDC gameDc = GetDC(gameHwnd);
    const int gamePf = gameDc ? rt.pGetPixelFormat(gameDc) : -1;
    if (gameDc) ReleaseDC(gameHwnd, gameDc);
    printf("  2. SwapChain (%ux%u %s buffers=%u tearing=%d) | gameHwnd pixelFormat=%d (expect 0)\n",
           flip.Width(), flip.Height(), flip.SwapEffectName(), flip.BufferCount(),
           flip.AllowTearing() ? 1 : 0, gamePf);
    if (gamePf != 0 || flip.Width() != 256u || flip.Height() != 256u) {
        printf("  [FAIL] gameHwnd pixelFormat polluted or wrong dimensions!\n");
        flip.Shutdown(false);
        DestroyWindow(gameHwnd);
        return false;
    }

    // 3. Create Desktop GL 4.1+ Compat HGLRC on dummy HDC and initialize interop
    HDC dummyDc = flip.DummyHdc();
    HGLRC rc = nullptr;
    if (!hs_native_gl::CreateGameEs2Context(rt, flip.DummyHwnd(), dummyDc, rc, nullptr) || !rc) {
        printf("  [FAIL] CreateGameEs2Context on dummy HWND failed\n");
        flip.Shutdown(false);
        DestroyWindow(gameHwnd);
        return false;
    }
    if (!rt.pWglMakeCurrent(dummyDc, rc)) {
        printf("  [FAIL] wglMakeCurrent on dummy HWND failed\n");
        rt.pWglDeleteContext(rc);
        flip.Shutdown(false);
        DestroyWindow(gameHwnd);
        return false;
    }

    if (!flip.InitGlResources(rt, reason) || !flip.IsActive() || flip.OffscreenFbo() == 0u) {
        printf("  [FAIL] InitGlResources failed: %s\n", reason.c_str());
        rt.pWglMakeCurrent(nullptr, nullptr);
        rt.pWglDeleteContext(rc);
        flip.Shutdown(false);
        DestroyWindow(gameHwnd);
        return false;
    }

    auto fnClearColor  = reinterpret_cast<void (WINAPI*)(float, float, float, float)>(rt.ResolveGlProc("glClearColor"));
    auto fnClear       = reinterpret_cast<void (WINAPI*)(uint32_t)>(rt.ResolveGlProc("glClear"));
    auto fnEnable      = reinterpret_cast<void (WINAPI*)(uint32_t)>(rt.ResolveGlProc("glEnable"));
    auto fnDisable     = reinterpret_cast<void (WINAPI*)(uint32_t)>(rt.ResolveGlProc("glDisable"));
    auto fnScissor     = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, int32_t, int32_t)>(rt.ResolveGlProc("glScissor"));
    auto fnIsEnabled   = reinterpret_cast<uint8_t (WINAPI*)(uint32_t)>(rt.ResolveGlProc("glIsEnabled"));
    auto fnGetIntegerv = reinterpret_cast<void (WINAPI*)(uint32_t, int32_t*)>(rt.ResolveGlProc("glGetIntegerv"));
    auto fnGetError    = reinterpret_cast<uint32_t (WINAPI*)()>(rt.ResolveGlProc("glGetError"));
    if (!fnClearColor || !fnClear || !fnEnable || !fnDisable || !fnScissor || !fnIsEnabled || !fnGetIntegerv || !fnGetError) {
        printf("  [FAIL] Missing GL test procs\n");
        flip.Shutdown(true);
        rt.pWglMakeCurrent(nullptr, nullptr);
        rt.pWglDeleteContext(rc);
        DestroyWindow(gameHwnd);
        return false;
    }

    // 4. Pixel-Exact Y-Flip + Scissor-Test Preservation Test!
    // Paint 4 distinct quadrants in GL Y-up coordinates into offscreenFbo (already bound!):
    //   GL Bottom-Left  (x=0..128,   y=0..128)   -> RED    (255, 0,   0, 255)
    //   GL Bottom-Right (x=128..256, y=0..128)   -> GREEN  (0,   255, 0, 255)
    //   GL Top-Left     (x=0..128,   y=128..256) -> BLUE   (0,   0, 255, 255)
    //   GL Top-Right    (x=128..256, y=128..256) -> YELLOW (255, 255, 0, 255)
    fnEnable(hs_dxgi_flip::kGlScissorTest);
    fnScissor(0, 0, 128, 128);
    fnClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    fnClear(hs_dxgi_flip::kGlColorBufferBit);

    fnScissor(128, 0, 128, 128);
    fnClearColor(0.0f, 1.0f, 0.0f, 1.0f);
    fnClear(hs_dxgi_flip::kGlColorBufferBit);

    fnScissor(0, 128, 128, 128);
    fnClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    fnClear(hs_dxgi_flip::kGlColorBufferBit);

    fnScissor(128, 128, 128, 128);
    fnClearColor(1.0f, 1.0f, 0.0f, 1.0f);
    fnClear(hs_dxgi_flip::kGlColorBufferBit);

    // Leave tiny 10x10 scissor ENABLED across Present(0) to prove glBlitFramebuffer ignores & restores GL_SCISSOR_TEST!
    fnScissor(0, 0, 10, 10);
    const bool presOk = flip.Present(0);
    const bool scissorStillOn = (fnIsEnabled(hs_dxgi_flip::kGlScissorTest) != 0u);
    int32_t boundDrawFbo = -1;
    fnGetIntegerv(hs_dxgi_flip::kGlDrawFramebufferBinding, &boundDrawFbo);
    const uint32_t err1 = fnGetError();

    std::vector<uint8_t> rgba;
    uint32_t rw = 0, rh = 0;
    const bool rbOk = flip.ReadbackSharedTextureRgba(rgba, rw, rh);
    auto px = [&](uint32_t x, uint32_t y) -> const uint8_t* {
        return rgba.data() + (static_cast<size_t>(y) * rw + x) * 4u;
    };
    auto eqRgba = [](const uint8_t* p, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
        return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
    };

    bool quadOk = false;
    if (rbOk && rw == 256u && rh == 256u) {
        // In D3D11 top-down coords: y=64 is TOP (GL y=191 -> BLUE / YELLOW), y=192 is BOTTOM (GL y=63 -> RED / GREEN)
        const uint8_t* tl = px(64, 64);
        const uint8_t* tr = px(192, 64);
        const uint8_t* bl = px(64, 192);
        const uint8_t* br = px(192, 192);
        quadOk = eqRgba(tl, 0, 0, 255, 255) &&
                 eqRgba(tr, 255, 255, 0, 255) &&
                 eqRgba(bl, 255, 0, 0, 255) &&
                 eqRgba(br, 0, 255, 0, 255);
        printf("  3. Pixel-Exact Y-Flip+Scissor: pres=%d scissorRestored=%d fboMatch=%d glErr=0x%X | D3D11 TL=(%u,%u,%u) TR=(%u,%u,%u) BL=(%u,%u,%u) BR=(%u,%u,%u) -> %s\n",
               presOk ? 1 : 0, scissorStillOn ? 1 : 0,
               (boundDrawFbo == static_cast<int32_t>(flip.OffscreenFbo())) ? 1 : 0, err1,
               tl[0], tl[1], tl[2], tr[0], tr[1], tr[2], bl[0], bl[1], bl[2], br[0], br[1], br[2],
               quadOk ? "PASS" : "FAIL");
    } else {
        printf("  3. [FAIL] ReadbackSharedTextureRgba failed (rbOk=%d %ux%u)\n", rbOk ? 1 : 0, rw, rh);
    }

    // 5. Dynamic Resize Test (256x256 -> 384x192)
    fnDisable(hs_dxgi_flip::kGlScissorTest);
    RECT wr2{0, 0, 384, 192};
    AdjustWindowRect(&wr2, WS_POPUP, FALSE);
    SetWindowPos(gameHwnd, nullptr, 0, 0, wr2.right - wr2.left, wr2.bottom - wr2.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    const uint32_t fboBeforeResize = flip.OffscreenFbo();
    const bool rszOk = flip.EnsureSizeForCurrentWindow();
    fnClearColor(0.0f, 1.0f, 1.0f, 1.0f); // CYAN
    fnClear(hs_dxgi_flip::kGlColorBufferBit);
    const bool pres2Ok = flip.Present(0);
    std::vector<uint8_t> rgba2;
    uint32_t rw2 = 0, rh2 = 0;
    const bool rb2Ok = flip.ReadbackSharedTextureRgba(rgba2, rw2, rh2);
    bool resizePixelOk = false;
    if (rb2Ok && rw2 == 384u && rh2 == 192u) {
        const uint8_t* p = rgba2.data() + (static_cast<size_t>(150) * rw2 + 300) * 4u;
        resizePixelOk = eqRgba(p, 0, 255, 255, 255);
    }
    const bool resizeAllOk = rszOk && pres2Ok && resizePixelOk &&
                             (flip.Width() == 384u) && (flip.Height() == 192u) &&
                             (flip.ResizeCount() == 1u) && (flip.OffscreenFbo() == fboBeforeResize);
    printf("  4. Dynamic Resize (256x256 -> %ux%u, sameFboId=%d, resizeCnt=%u, cyanOk=%d) -> %s\n",
           flip.Width(), flip.Height(), (flip.OffscreenFbo() == fboBeforeResize) ? 1 : 0,
           flip.ResizeCount(), resizePixelOk ? 1 : 0, resizeAllOk ? "PASS" : "FAIL");

    // 6. 200-Frame Uncapped Present(0, ALLOW_TEARING) Pacing Benchmark
    LARGE_INTEGER freq{}, t0{}, t1{};
    QueryPerformanceFrequency(&freq);
    std::vector<double> latMs;
    latMs.reserve(200);
    bool benchOk = true;
    for (int i = 0; i < 200; ++i) {
        fnClearColor(static_cast<float>(i & 15) / 15.0f, 0.25f, 0.5f, 1.0f);
        fnClear(hs_dxgi_flip::kGlColorBufferBit);
        QueryPerformanceCounter(&t0);
        if (!flip.Present(0)) {
            benchOk = false;
            break;
        }
        QueryPerformanceCounter(&t1);
        latMs.push_back(static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart));
    }
    const uint32_t errEnd = fnGetError();
    double avgMs = 0.0, p99Ms = 0.0, maxMs = 0.0;
    if (!latMs.empty()) {
        for (double v : latMs) avgMs += v;
        avgMs /= static_cast<double>(latMs.size());
        std::sort(latMs.begin(), latMs.end());
        p99Ms = latMs[std::min<size_t>(latMs.size() - 1, (latMs.size() * 99) / 100)];
        maxMs = latMs.back();
    }
    printf("  5. 200-Frame Present(0, ALLOW_TEARING) Bench: avg=%.3fms p99=%.3fms max=%.3fms glErr=0x%X -> %s\n",
           avgMs, p99Ms, maxMs, errEnd, (benchOk && errEnd == 0u) ? "PASS" : "FAIL");

    flip.Shutdown(true);
    rt.pWglMakeCurrent(nullptr, nullptr);
    rt.pWglDeleteContext(rc);
    DestroyWindow(gameHwnd);

    return presOk && scissorStillOn && (boundDrawFbo == static_cast<int32_t>(fboBeforeResize)) &&
           (err1 == 0u) && quadOk && resizeAllOk && benchOk && (errEnd == 0u);
}

static bool TestSpriteBatchAzdoLiveGpu() {
    printf("\n=== Live RTX GPU Test: SpriteBatcher AZDO Persistent-Coherent Ring (GL_ARB_buffer_storage + glFenceSync) ===\n");
    hs_native_gl::WglRuntime rt{};
    const auto probe = hs_native_gl::ProbeNativeEs2Support(hs_native_gl::BackendMode::Auto, rt);
    if (!probe.supported) {
        printf("  [SKIP] Native GL not supported on host GPU\n");
        return true;
    }

    HWND hwnd = CreateWindowExW(0, L"STATIC", L"HS_AZDO_TEST", WS_POPUP, 0, 0, 128, 128, nullptr, nullptr, nullptr, nullptr);
    HDC hdc = nullptr;
    HGLRC rc = nullptr;
    if (!hwnd || !hs_native_gl::CreateGameEs2Context(rt, hwnd, hdc, rc) || !rt.pWglMakeCurrent(hdc, rc)) {
        if (hwnd) DestroyWindow(hwnd);
        printf("  [FAIL] Could not create Desktop OpenGL 4.6 context\n");
        return false;
    }

    void* procs[76]{};
    hs_native_gl::OptionalGlProcs opt{};
    hs_native_gl::ResolveLiveGlesTable(rt, procs, opt);
    auto findP = [&](const char* name) -> void* {
        for (size_t i = 0; i < 76; ++i) {
            if (std::strcmp(hs_native_gl::kDstGles2Imports[i].name, name) == 0) return procs[i];
        }
        return nullptr;
    };

    static constexpr const char* kAnimVs =
        "#ifdef SKINNED\n"
        "\tuniform mat4 pv;\n"
        "#else\n"
        "\tuniform mat4 MatrixP;\n"
        "\tuniform mat4 MatrixV;\n"
        "\tuniform mat4 MatrixW;\n"
        "#endif\n"
        "uniform vec4 TIMEPARAMS;\n"
        "uniform vec3 FLOAT_PARAMS;\n"
        "attribute vec4 POS2D_UV;\n"
        "varying vec3 PS_TEXCOORD;\n"
        "varying vec3 PS_POS;\n"
        "void main() {\n"
        "#ifdef SKINNED\n"
        "\tvec4 world_pos = vec4(0.0);\n"
        "\tvec3 TEXCOORD0 = vec3(0.0);\n"
        "#else\n"
        "\tvec3 POSITION = vec3(POS2D_UV.xy, 0);\n"
        "\tfloat samplerIndex = floor(POS2D_UV.z/2.0);\n"
        "\tvec3 TEXCOORD0 = vec3(POS2D_UV.z - 2.0*samplerIndex, POS2D_UV.w, samplerIndex);\n"
        "\tvec3 object_pos = POSITION.xyz;\n"
        "\tvec4 world_pos = MatrixW * vec4( object_pos, 1.0 );\n"
        "\tif(FLOAT_PARAMS.z > 0.0) { world_pos.y += sin(MatrixW[3][0] + MatrixW[3][2] + TIMEPARAMS.x * 3.0) * 0.025; }\n"
        "\tmat4 mtxPV = MatrixP * MatrixV;\n"
        "\tgl_Position = mtxPV * world_pos;\n"
        "#endif\n"
        "\tPS_TEXCOORD = TEXCOORD0;\n"
        "\tPS_POS = world_pos.xyz;\n"
        "}\n";
    static constexpr const char* kAnimPs =
        "#if defined( GL_ES )\n"
        "precision mediump float;\n"
        "#endif\n"
        "uniform mat4 MatrixW;\n"
        "uniform sampler2D SAMPLER[6];\n"
        "varying vec3 PS_POS;\n"
        "varying vec3 PS_TEXCOORD;\n"
        "uniform vec4 LIGHTMAP_WORLD_EXTENTS;\n"
        "uniform mat4 COLOUR_XFORM;\n"
        "uniform vec3 PARAMS;\n"
        "uniform vec3 FLOAT_PARAMS;\n"
        "uniform vec4 OCEAN_BLEND_PARAMS;\n"
        "uniform vec4 OCEAN_WORLD_EXTENTS;\n"
        "#define ALPHA_TEST PARAMS.x\n"
        "#define LIGHT_OVERRIDE PARAMS.y\n"
        "#define BLOOM_TOGGLE PARAMS.z\n"
        "#define OCEAN_SAMPLER SAMPLER[4]\n"
        "vec3 CalculateLightingContribution() { return texture2D(SAMPLER[3], (PS_POS.xz - LIGHTMAP_WORLD_EXTENTS.xy) * LIGHTMAP_WORLD_EXTENTS.zw).rgb; }\n"
        "void main() {\n"
        "    vec4 colour;\n"
        "#if defined( TRIPLE_ATLAS )\n"
        "    if( PS_TEXCOORD.z < 0.5 ) { colour.rgba = texture2D( SAMPLER[0], PS_TEXCOORD.xy ); }\n"
        "    else if( PS_TEXCOORD.z < 1.5 ) { colour.rgba = texture2D( SAMPLER[1], PS_TEXCOORD.xy ); }\n"
        "    else { colour.rgba = texture2D( SAMPLER[5], PS_TEXCOORD.xy ); }\n"
        "#else\n"
        "    if( PS_TEXCOORD.z < 0.5 ) { colour.rgba = texture2D( SAMPLER[0], PS_TEXCOORD.xy ); }\n"
        "    else { colour.rgba = texture2D( SAMPLER[1], PS_TEXCOORD.xy ); }\n"
        "#endif\n"
        "    if (BLOOM_TOGGLE == 1.0) { gl_FragColor.rgba = vec4(0.0, 0.0, 0.0, colour.a); return; }\n"
        "    if(FLOAT_PARAMS.y > 0.0) { if(PS_POS.y < FLOAT_PARAMS.x) { discard; } }\n"
        "    gl_FragColor.rgba = colour.rgba * COLOUR_XFORM;\n"
        "    vec2 world_uv = ( PS_POS.xz - OCEAN_WORLD_EXTENTS.xy ) * OCEAN_WORLD_EXTENTS.zw;\n"
        "    vec3 world_tint = texture2D( OCEAN_SAMPLER, world_uv ).rgb;\n"
        "    gl_FragColor.rgb = mix(gl_FragColor.rgb, gl_FragColor.rgb * world_tint.rgb, OCEAN_BLEND_PARAMS.x);\n"
        "    vec3 light = CalculateLightingContribution();\n"
        "    gl_FragColor.rgb *= max( light.rgb, vec3( LIGHT_OVERRIDE, LIGHT_OVERRIDE, LIGHT_OVERRIDE ) );\n"
        "}\n";

    std::string vsOut, psOut;
    const char* tag = nullptr;
    bool txOk = hs_sprite_batch::TransformAnimBatchShaderSource(kAnimVs, vsOut, tag) &&
                hs_sprite_batch::TransformAnimBatchShaderSource(kAnimPs, psOut, tag, true);

    auto pCreateSh   = reinterpret_cast<uint32_t (WINAPI*)(uint32_t)>(findP("glCreateShader"));
    auto pShSrc      = reinterpret_cast<void (WINAPI*)(uint32_t, int32_t, const char* const*, const int32_t*)>(findP("glShaderSource"));
    auto pCompSh     = reinterpret_cast<void (WINAPI*)(uint32_t)>(findP("glCompileShader"));
    auto pGetShIv    = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t, int32_t*)>(findP("glGetShaderiv"));
    auto pGetShLog   = reinterpret_cast<void (WINAPI*)(uint32_t, int32_t, int32_t*, char*)>(findP("glGetShaderInfoLog"));
    auto pCreatePr   = reinterpret_cast<uint32_t (WINAPI*)()>(findP("glCreateProgram"));
    auto pAttachSh   = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t)>(findP("glAttachShader"));
    auto pLinkPr     = reinterpret_cast<void (WINAPI*)(uint32_t)>(findP("glLinkProgram"));
    auto pGetPrIv    = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t, int32_t*)>(findP("glGetProgramiv"));
    auto pGetPrLog   = reinterpret_cast<void (WINAPI*)(uint32_t, int32_t, int32_t*, char*)>(findP("glGetProgramInfoLog"));
    auto pDelSh      = reinterpret_cast<void (WINAPI*)(uint32_t)>(findP("glDeleteShader"));
    auto pDelPr      = reinterpret_cast<void (WINAPI*)(uint32_t)>(findP("glDeleteProgram"));
    auto pUsePr      = reinterpret_cast<void (WINAPI*)(uint32_t)>(findP("glUseProgram"));
    auto pGetUni     = reinterpret_cast<int32_t (WINAPI*)(uint32_t, const char*)>(findP("glGetUniformLocation"));
    auto pGetAtt     = reinterpret_cast<int32_t (WINAPI*)(uint32_t, const char*)>(findP("glGetAttribLocation"));
    auto pUniMat4    = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, uint8_t, const float*)>(findP("glUniformMatrix4fv"));
    auto pUni1iv     = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, const int32_t*)>(findP("glUniform1iv"));
    auto pUni3fv     = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, const float*)>(findP("glUniform3fv"));
    auto pUni4fv     = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, const float*)>(findP("glUniform4fv"));
    auto pGenTex     = reinterpret_cast<void (WINAPI*)(int32_t, uint32_t*)>(findP("glGenTextures"));
    auto pDelTex     = reinterpret_cast<void (WINAPI*)(int32_t, const uint32_t*)>(findP("glDeleteTextures"));
    auto pTexImg     = reinterpret_cast<void (WINAPI*)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*)>(findP("glTexImage2D"));
    auto pTexPar     = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t, int32_t)>(findP("glTexParameteri"));
    auto pGenFbo     = reinterpret_cast<void (WINAPI*)(int32_t, uint32_t*)>(findP("glGenFramebuffers"));
    auto pDelFbo     = reinterpret_cast<void (WINAPI*)(int32_t, const uint32_t*)>(findP("glDeleteFramebuffers"));
    auto pBindFbo    = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t)>(findP("glBindFramebuffer"));
    auto pFboTex     = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t)>(findP("glFramebufferTexture2D"));
    auto pVp         = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, int32_t, int32_t)>(findP("glViewport"));
    auto pClrCol     = reinterpret_cast<void (WINAPI*)(float, float, float, float)>(findP("glClearColor"));
    auto pClr        = reinterpret_cast<void (WINAPI*)(uint32_t)>(findP("glClear"));
    auto pReadPix    = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void*)>(findP("glReadPixels"));
    auto pFinish     = reinterpret_cast<void (WINAPI*)()>(findP("glFinish"));
    auto pGetErr     = reinterpret_cast<uint32_t (WINAPI*)()>(findP("glGetError"));

    uint32_t vs = pCreateSh(0x8B31u), fs = pCreateSh(0x8B30u);
    const char* vsP = vsOut.c_str(); int32_t vsL = static_cast<int32_t>(vsOut.size());
    const char* fsP = psOut.c_str(); int32_t fsL = static_cast<int32_t>(psOut.size());
    pShSrc(vs, 1, &vsP, &vsL); pCompSh(vs);
    pShSrc(fs, 1, &fsP, &fsL); pCompSh(fs);
    int32_t vsOk = 0, fsOk = 0, linkOk = 0;
    if (pGetShIv) { pGetShIv(vs, 0x8B81u, &vsOk); pGetShIv(fs, 0x8B81u, &fsOk); }
    auto pBindAtt    = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t, const char*)>(findP("glBindAttribLocation"));
    uint32_t progId = pCreatePr();
    pAttachSh(progId, vs); pAttachSh(progId, fs);
    if (pBindAtt) pBindAtt(progId, 0u, "POS2D_UV");
    pLinkPr(progId);
    if (pGetPrIv) pGetPrIv(progId, 0x8B82u, &linkOk);
    if (!vsOk || !fsOk || !linkOk) {
        char logBuf[512]{}; int32_t len = 0;
        if (!vsOk && pGetShLog) { pGetShLog(vs, 511, &len, logBuf); printf("  AZDO VS log: %s\n", logBuf); }
        if (!fsOk && pGetShLog) { pGetShLog(fs, 511, &len, logBuf); printf("  AZDO FS log: %s\n", logBuf); }
        if (!linkOk && pGetPrLog) { pGetPrLog(progId, 511, &len, logBuf); printf("  AZDO Link log: %s\n", logBuf); }
    }
    pDelSh(vs); pDelSh(fs);

    hs_sprite_batch::GlBatchOps ops{};
    ops.glGenBuffers               = reinterpret_cast<hs_sprite_batch::GlBatchOps::GenBuffers_t>(findP("glGenBuffers"));
    ops.glDeleteBuffers            = reinterpret_cast<hs_sprite_batch::GlBatchOps::DeleteBuffers_t>(findP("glDeleteBuffers"));
    ops.glBindBuffer               = reinterpret_cast<hs_sprite_batch::GlBatchOps::BindBuffer_t>(findP("glBindBuffer"));
    ops.glBufferData               = reinterpret_cast<hs_sprite_batch::GlBatchOps::BufferData_t>(findP("glBufferData"));
    ops.glBufferSubData            = reinterpret_cast<hs_sprite_batch::GlBatchOps::BufferSubData_t>(opt.glBufferSubData);
    ops.glMapBufferRange           = reinterpret_cast<hs_sprite_batch::GlBatchOps::MapBufferRange_t>(opt.glMapBufferRange);
    ops.glUnmapBuffer              = reinterpret_cast<hs_sprite_batch::GlBatchOps::UnmapBuffer_t>(opt.glUnmapBuffer);
    ops.glActiveTexture            = reinterpret_cast<hs_sprite_batch::GlBatchOps::ActiveTexture_t>(findP("glActiveTexture"));
    ops.glBindTexture              = reinterpret_cast<hs_sprite_batch::GlBatchOps::BindTexture_t>(findP("glBindTexture"));
    ops.glUniform1f                = reinterpret_cast<hs_sprite_batch::GlBatchOps::Uniform1f_t>(findP("glUniform1f"));
    ops.glUniform1i                = reinterpret_cast<hs_sprite_batch::GlBatchOps::Uniform1i_t>(findP("glUniform1i"));
    ops.glUniform3fv               = pUni3fv;
    ops.glUniform4fv               = pUni4fv;
    ops.glUniformMatrix4fv         = pUniMat4;
    ops.glVertexAttribPointer      = reinterpret_cast<hs_sprite_batch::GlBatchOps::VertexAttribPointer_t>(findP("glVertexAttribPointer"));
    ops.glEnableVertexAttribArray  = reinterpret_cast<hs_sprite_batch::GlBatchOps::EnableVertexAttribArray_t>(findP("glEnableVertexAttribArray"));
    ops.glDisableVertexAttribArray = reinterpret_cast<hs_sprite_batch::GlBatchOps::DisableVertexAttribArray_t>(findP("glDisableVertexAttribArray"));
    ops.glDrawElements             = reinterpret_cast<hs_sprite_batch::GlBatchOps::DrawElements_t>(findP("glDrawElements"));
    ops.glDrawElementsBaseVertex   = reinterpret_cast<hs_sprite_batch::GlBatchOps::DrawElementsBaseVertex_t>(opt.glDrawElementsBaseVertex);
    ops.glGetIntegerv              = reinterpret_cast<hs_sprite_batch::GlBatchOps::GetIntegerv_t>(findP("glGetIntegerv"));
    ops.glGetBufferParameteriv     = reinterpret_cast<hs_sprite_batch::GlBatchOps::GetBufferParameteriv_t>(opt.glGetBufferParameteriv);
    ops.glGetBufferSubData         = reinterpret_cast<hs_sprite_batch::GlBatchOps::GetBufferSubData_t>(opt.glGetBufferSubData);
    ops.glBufferStorage            = reinterpret_cast<hs_sprite_batch::GlBatchOps::BufferStorage_t>(opt.glBufferStorage ? opt.glBufferStorage : opt.glBufferStorageExt);
    ops.glFenceSync                = reinterpret_cast<hs_sprite_batch::GlBatchOps::FenceSync_t>(opt.glFenceSync);
    ops.glClientWaitSync           = reinterpret_cast<hs_sprite_batch::GlBatchOps::ClientWaitSync_t>(opt.glClientWaitSync);
    ops.glDeleteSync               = reinterpret_cast<hs_sprite_batch::GlBatchOps::DeleteSync_t>(opt.glDeleteSync);

    uint32_t fbo = 0, fboTex = 0, texSprite = 0, texLight = 0;
    pGenFbo(1, &fbo); pGenTex(1, &fboTex); pGenTex(1, &texSprite); pGenTex(1, &texLight);
    ops.glActiveTexture(hs_sprite_batch::kGlTexture0);
    ops.glBindTexture(hs_sprite_batch::kGlTexture2D, fboTex);
    pTexPar(hs_sprite_batch::kGlTexture2D, 0x2801u, 0x2600);
    pTexPar(hs_sprite_batch::kGlTexture2D, 0x2800u, 0x2600);
    pTexImg(hs_sprite_batch::kGlTexture2D, 0, 0x8058, 64, 64, 0, 0x1908u, 0x1401u, nullptr);
    pBindFbo(0x8D40u, fbo);
    pFboTex(0x8D40u, 0x8CE0u, hs_sprite_batch::kGlTexture2D, fboTex, 0);
    pVp(0, 0, 64, 64);

    const uint8_t whitePx[4] = { 255,255,255,255 };
    ops.glActiveTexture(hs_sprite_batch::kGlTexture0 + 3u);
    ops.glBindTexture(hs_sprite_batch::kGlTexture2D, texLight);
    pTexPar(hs_sprite_batch::kGlTexture2D, 0x2801u, 0x2600);
    pTexPar(hs_sprite_batch::kGlTexture2D, 0x2800u, 0x2600);
    pTexImg(hs_sprite_batch::kGlTexture2D, 0, 0x8058, 1, 1, 0, 0x1908u, 0x1401u, whitePx);

    const uint8_t orange[16] = { 255,140,20,255, 255,140,20,255, 255,140,20,255, 255,140,20,255 };
    ops.glActiveTexture(hs_sprite_batch::kGlTexture0);
    ops.glBindTexture(hs_sprite_batch::kGlTexture2D, texSprite);
    pTexPar(hs_sprite_batch::kGlTexture2D, 0x2801u, 0x2600);
    pTexPar(hs_sprite_batch::kGlTexture2D, 0x2800u, 0x2600);
    pTexImg(hs_sprite_batch::kGlTexture2D, 0, 0x8058, 2, 2, 0, 0x1908u, 0x1401u, orange);

    pUsePr(progId);
    const float idMtx[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    const int32_t sampUnits[6] = { 0, 1, 2, 3, 4, 5 };
    const float paramsV[3] = { 0.0f, 1.0f, 0.0f };
    const float extV[4]    = { 0.0f, 0.0f, 1.0f, 1.0f };
    if (pUni1iv && pGetUni(progId, "SAMPLER") >= 0) pUni1iv(pGetUni(progId, "SAMPLER"), 6, sampUnits);
    if (pUni3fv && pGetUni(progId, "PARAMS") >= 0) pUni3fv(pGetUni(progId, "PARAMS"), 1, paramsV);
    if (pUni4fv && pGetUni(progId, "LIGHTMAP_WORLD_EXTENTS") >= 0) pUni4fv(pGetUni(progId, "LIGHTMAP_WORLD_EXTENTS"), 1, extV);
    pUniMat4(pGetUni(progId, "MatrixP"), 1, 0, idMtx);
    pUniMat4(pGetUni(progId, "MatrixV"), 1, 0, idMtx);
    pUniMat4(pGetUni(progId, "COLOUR_XFORM"), 1, 0, idMtx);

    hs_sprite_batch::BatchProgInfo info{};
    info.checked        = true;
    info.eligible       = true;
    info.locBatchActive = pGetUni(progId, "HS_BATCH_ACTIVE");
    info.locMatrixW     = pGetUni(progId, "MatrixW");
    info.locColourXform = pGetUni(progId, "COLOUR_XFORM");
    info.locFloatParams = pGetUni(progId, "FLOAT_PARAMS");
    info.locPos2dUv     = pGetAtt(progId, "POS2D_UV");
    info.locBatchWpos   = pGetAtt(progId, "HS_BATCH_WPOS");
    info.locBatchCMult  = pGetAtt(progId, "HS_BATCH_CMULT");
    info.locBatchCAdd   = pGetAtt(progId, "HS_BATCH_CADD");
    info.progMaxTexSlots = 16u;
    for (uint32_t s = 0; s < hs_sprite_batch::kMaxBatchTexSlots; ++s) {
        char nm[32]{};
        snprintf(nm, sizeof(nm), "HS_BATCH_TEX%u", s);
        info.locBatchTex[s] = pGetUni(progId, nm);
    }

    const float qv[16] = { -0.5f,-0.5f,0,0,  0.5f,-0.5f,1,0,  0.5f,0.5f,1,1,  -0.5f,0.5f,0,1 };
    const uint16_t qi[6] = { 0,1,2, 0,2,3 };

    hs_sprite_batch::SpriteBatcher sb;
    sb.SetOps(ops);
    sb.SetPersistentRingPreferred(true);
    sb.SetShadowCacheMaxBytes(256u * 1024u * 1024u);
    sb.SetEnabled(true);
    sb.OnUseProgram(progId, &info);
    ops.glEnableVertexAttribArray(static_cast<uint32_t>(info.locPos2dUv));
    sb.OnEnableDisableVertexAttribArray(static_cast<uint32_t>(info.locPos2dUv), true);
    sb.OnActiveTexture(hs_sprite_batch::kGlTexture0);
    sb.OnBindTexture(hs_sprite_batch::kGlTexture2D, texSprite);
    sb.OnBindBuffer(hs_sprite_batch::kGlArrayBuffer, 0u);
    sb.OnVertexAttribPointer(static_cast<uint32_t>(info.locPos2dUv), 4, hs_sprite_batch::kGlFloat, 0, 16, qv);
    sb.OnBindBuffer(hs_sprite_batch::kGlElementArrayBuffer, 0u);

    // Run 20 consecutive flushes (> 2.5x kRingSlots=8) exercising glFenceSync + wait wraparound!
    pClrCol(0, 0, 0, 1);
    pClr(0x4000u);
    bool batchAcc = false;
    uint32_t rejCode = 0;
    for (int f = 0; f < 20; ++f) {
        sb.TryAbsorbMatrixW(info.locMatrixW, 1, 0, idMtx);
        sb.TryAbsorbColourXform(info.locColourXform, 1, 0, idMtx);
        if (sb.TryBatchDrawElements(hs_sprite_batch::kGlTriangles, 6, hs_sprite_batch::kGlUnsignedShort, qi)) {
            batchAcc = true;
        } else if (rejCode == 0) {
            rejCode = sb.LastRejectCode();
        }
        sb.Flush(hs_sprite_batch::FlushReason::End);
    }
    pFinish();
    const bool azdoWasActive = sb.IsPersistentRingActive();
    uint8_t px[4]{};
    pReadPix(32, 32, 1, 1, 0x1908u, 0x1401u, px);
    const uint32_t err1 = pGetErr();
    const bool pixelOk = (px[0] > 230 && px[1] > 120 && px[1] < 170 && px[2] < 40 && px[3] == 255);

    // Sticky AZDO BaseVertex live GPU verification (2 consecutive flushes, read flush #2 with basevertex != 0)
    sb.SetDeferHostRestore(true);
    sb.TryAbsorbMatrixW(info.locMatrixW, 1, 0, idMtx);
    sb.TryAbsorbColourXform(info.locColourXform, 1, 0, idMtx);
    sb.TryBatchDrawElements(hs_sprite_batch::kGlTriangles, 6, hs_sprite_batch::kGlUnsignedShort, qi);
    sb.Flush(hs_sprite_batch::FlushReason::State);
    pClrCol(0, 0, 0, 1);
    pClr(0x4000u);
    sb.TryAbsorbMatrixW(info.locMatrixW, 1, 0, idMtx);
    sb.TryAbsorbColourXform(info.locColourXform, 1, 0, idMtx);
    sb.TryBatchDrawElements(hs_sprite_batch::kGlTriangles, 6, hs_sprite_batch::kGlUnsignedShort, qi);
    sb.Flush(hs_sprite_batch::FlushReason::State);
    const bool stickyActiveOk = sb.IsStickySessionActive();
    sb.EndStickyBatchSession();
    const bool stickyEndedOk = !sb.IsStickySessionActive();
    pFinish();
    uint8_t stickyPx[4]{};
    pReadPix(32, 32, 1, 1, 0x1908u, 0x1401u, stickyPx);
    const uint32_t errSticky = pGetErr();
    const bool stickyPixelOk = (stickyPx[0] > 230 && stickyPx[1] > 120 && stickyPx[1] < 170 && stickyPx[2] < 40 && stickyPx[3] == 255);
    sb.SetDeferHostRestore(false);

    // Switch dynamically to Low-tier (SetPersistentRingPreferred(false)) -> tears down AZDO ring cleanly!
    sb.SetPersistentRingPreferred(false);
    sb.TryAbsorbMatrixW(info.locMatrixW, 1, 0, idMtx);
    sb.TryAbsorbColourXform(info.locColourXform, 1, 0, idMtx);
    sb.TryBatchDrawElements(hs_sprite_batch::kGlTriangles, 6, hs_sprite_batch::kGlUnsignedShort, qi);
    sb.Flush(hs_sprite_batch::FlushReason::End);
    pFinish();
    const bool fallbackInactive = !sb.IsPersistentRingActive();
    sb.ReleaseGpuResources();
    const uint32_t err2 = pGetErr();

    // ANGLE fallback check when glBufferStorage == nullptr
    auto angleOps = ops;
    angleOps.glBufferStorage = nullptr;
    hs_sprite_batch::SpriteBatcher sbAngle;
    sbAngle.SetOps(angleOps);
    sbAngle.SetPersistentRingPreferred(true);
    sbAngle.SetEnabled(true);
    sbAngle.OnUseProgram(progId, &info);
    sbAngle.OnEnableDisableVertexAttribArray(static_cast<uint32_t>(info.locPos2dUv), true);
    sbAngle.OnActiveTexture(hs_sprite_batch::kGlTexture0);
    sbAngle.OnBindTexture(hs_sprite_batch::kGlTexture2D, texSprite);
    sbAngle.OnBindBuffer(hs_sprite_batch::kGlArrayBuffer, 0u);
    sbAngle.OnVertexAttribPointer(static_cast<uint32_t>(info.locPos2dUv), 4, hs_sprite_batch::kGlFloat, 0, 16, qv);
    sbAngle.OnBindBuffer(hs_sprite_batch::kGlElementArrayBuffer, 0u);
    sbAngle.TryAbsorbMatrixW(info.locMatrixW, 1, 0, idMtx);
    sbAngle.TryAbsorbColourXform(info.locColourXform, 1, 0, idMtx);
    sbAngle.TryBatchDrawElements(hs_sprite_batch::kGlTriangles, 6, hs_sprite_batch::kGlUnsignedShort, qi);
    sbAngle.Flush(hs_sprite_batch::FlushReason::End);
    pFinish();
    const bool angleFallbackOk = !sbAngle.IsPersistentRingActive();
    sbAngle.ReleaseGpuResources();

    pBindFbo(0x8D40u, 0u);
    pDelFbo(1, &fbo);
    pDelTex(1, &fboTex);
    pDelTex(1, &texSprite);
    pDelTex(1, &texLight);
    pDelPr(progId);
    rt.pWglMakeCurrent(nullptr, nullptr);
    rt.pWglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);

    const bool ok = txOk && vsOk && fsOk && linkOk && batchAcc && azdoWasActive && pixelOk && (err1 == 0u) &&
                    stickyActiveOk && stickyEndedOk && stickyPixelOk && (errSticky == 0u) &&
                    fallbackInactive && (err2 == 0u) && angleFallbackOk;
    printf("  AZDO Persistent-Coherent Ring (GL4.6): tx=%d sh=%d/%d/%d acc=%d(rej=%u) locs(act=%d mw=%d pos=%d wp=%d t0=%d) procs(bs=%d fs=%d cws=%d map=%d bv=%d) active=%d px=(%u,%u,%u,%u) sticky=(%d/%d px=%u,%u,%u,%u err=0x%X) err=(0x%X,0x%X) tierOffFallback=%d angleFallback=%d -> %s\n",
           txOk ? 1 : 0, vsOk, fsOk, linkOk, batchAcc ? 1 : 0, rejCode,
           info.locBatchActive, info.locMatrixW, info.locPos2dUv, info.locBatchWpos, info.locBatchTex[0],
           ops.glBufferStorage ? 1 : 0, ops.glFenceSync ? 1 : 0, ops.glClientWaitSync ? 1 : 0, ops.glMapBufferRange ? 1 : 0,
           ops.glDrawElementsBaseVertex ? 1 : 0,
           azdoWasActive ? 1 : 0, px[0], px[1], px[2], px[3],
           stickyActiveOk ? 1 : 0, stickyEndedOk ? 1 : 0, stickyPx[0], stickyPx[1], stickyPx[2], stickyPx[3], errSticky,
           err1, err2, fallbackInactive ? 1 : 0, angleFallbackOk ? 1 : 0, ok ? "PASS" : "FAIL");
    return ok;
}

static bool TestGpuDrivenBindlessAndShadowParityLiveGpu() {
    printf("\n=== Live RTX GPU Test: GPU-Driven Bindless+SSBO (32 Tex -> 1 Flush) & Deferred Shadow Parity ===\n");
    hs_native_gl::WglRuntime rt{};
    const auto probe = hs_native_gl::ProbeNativeEs2Support(hs_native_gl::BackendMode::Auto, rt);
    if (!probe.supported) {
        printf("  [SKIP] Native GL not supported on host GPU\n");
        return true;
    }

    HWND hwnd = CreateWindowExW(0, L"STATIC", L"HS_GPUDRV_TEST", WS_POPUP, 0, 0, 256, 256, nullptr, nullptr, nullptr, nullptr);
    HDC hdc = nullptr;
    HGLRC rc = nullptr;
    if (!hwnd || !hs_native_gl::CreateGameEs2Context(rt, hwnd, hdc, rc) || !rt.pWglMakeCurrent(hdc, rc)) {
        if (hwnd) DestroyWindow(hwnd);
        printf("  [FAIL] Could not create Desktop OpenGL 4.6 context\n");
        return false;
    }

    void* procs[76]{};
    hs_native_gl::OptionalGlProcs opt{};
    hs_native_gl::ResolveLiveGlesTable(rt, procs, opt);
    auto findP = [&](const char* name) -> void* {
        for (size_t i = 0; i < 76; ++i) {
            if (std::strcmp(hs_native_gl::kDstGles2Imports[i].name, name) == 0) return procs[i];
        }
        return nullptr;
    };
    if (!opt.glBindBufferBase || !opt.glGetTextureHandleARB || !opt.glMakeTextureHandleResidentARB ||
        !opt.glMakeTextureHandleNonResidentARB || !opt.glDrawElementsBaseVertex ||
        !(opt.glBufferStorage || opt.glBufferStorageExt)) {
        printf("  [FAIL] Missing Bindless/SSBO/BufferStorage/BaseVertex procs on host GPU\n");
        rt.pWglMakeCurrent(nullptr, nullptr);
        rt.pWglDeleteContext(rc);
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return false;
    }

    static constexpr const char* kAnimVs =
        "#ifdef SKINNED\n"
        "\tuniform mat4 pv;\n"
        "#else\n"
        "\tuniform mat4 MatrixP;\n"
        "\tuniform mat4 MatrixV;\n"
        "\tuniform mat4 MatrixW;\n"
        "#endif\n"
        "uniform vec4 TIMEPARAMS;\n"
        "uniform vec3 FLOAT_PARAMS;\n"
        "attribute vec4 POS2D_UV;\n"
        "varying vec3 PS_TEXCOORD;\n"
        "varying vec3 PS_POS;\n"
        "void main() {\n"
        "#ifdef SKINNED\n"
        "\tvec4 world_pos = vec4(0.0);\n"
        "\tvec3 TEXCOORD0 = vec3(0.0);\n"
        "#else\n"
        "\tvec3 POSITION = vec3(POS2D_UV.xy, 0);\n"
        "\tfloat samplerIndex = floor(POS2D_UV.z/2.0);\n"
        "\tvec3 TEXCOORD0 = vec3(POS2D_UV.z - 2.0*samplerIndex, POS2D_UV.w, samplerIndex);\n"
        "\tvec3 object_pos = POSITION.xyz;\n"
        "\tvec4 world_pos = MatrixW * vec4( object_pos, 1.0 );\n"
        "\tif(FLOAT_PARAMS.z > 0.0) { world_pos.y += sin(MatrixW[3][0] + MatrixW[3][2] + TIMEPARAMS.x * 3.0) * 0.025; }\n"
        "\tmat4 mtxPV = MatrixP * MatrixV;\n"
        "\tgl_Position = mtxPV * world_pos;\n"
        "#endif\n"
        "\tPS_TEXCOORD = TEXCOORD0;\n"
        "\tPS_POS = world_pos.xyz;\n"
        "}\n";
    static constexpr const char* kAnimPs =
        "#if defined( GL_ES )\n"
        "precision mediump float;\n"
        "#endif\n"
        "uniform mat4 MatrixW;\n"
        "uniform sampler2D SAMPLER[6];\n"
        "varying vec3 PS_POS;\n"
        "varying vec3 PS_TEXCOORD;\n"
        "uniform vec4 LIGHTMAP_WORLD_EXTENTS;\n"
        "uniform mat4 COLOUR_XFORM;\n"
        "uniform vec3 PARAMS;\n"
        "uniform vec3 FLOAT_PARAMS;\n"
        "uniform vec4 OCEAN_BLEND_PARAMS;\n"
        "uniform vec4 OCEAN_WORLD_EXTENTS;\n"
        "#define ALPHA_TEST PARAMS.x\n"
        "#define LIGHT_OVERRIDE PARAMS.y\n"
        "#define BLOOM_TOGGLE PARAMS.z\n"
        "#define OCEAN_SAMPLER SAMPLER[4]\n"
        "vec3 CalculateLightingContribution() { return texture2D(SAMPLER[3], (PS_POS.xz - LIGHTMAP_WORLD_EXTENTS.xy) * LIGHTMAP_WORLD_EXTENTS.zw).rgb; }\n"
        "void main() {\n"
        "    vec4 colour;\n"
        "#if defined( TRIPLE_ATLAS )\n"
        "    if( PS_TEXCOORD.z < 0.5 ) { colour.rgba = texture2D( SAMPLER[0], PS_TEXCOORD.xy ); }\n"
        "    else if( PS_TEXCOORD.z < 1.5 ) { colour.rgba = texture2D( SAMPLER[1], PS_TEXCOORD.xy ); }\n"
        "    else { colour.rgba = texture2D( SAMPLER[5], PS_TEXCOORD.xy ); }\n"
        "#else\n"
        "    if( PS_TEXCOORD.z < 0.5 ) { colour.rgba = texture2D( SAMPLER[0], PS_TEXCOORD.xy ); }\n"
        "    else { colour.rgba = texture2D( SAMPLER[1], PS_TEXCOORD.xy ); }\n"
        "#endif\n"
        "    if (BLOOM_TOGGLE == 1.0) { gl_FragColor.rgba = vec4(0.0, 0.0, 0.0, colour.a); return; }\n"
        "    if(FLOAT_PARAMS.y > 0.0) { if(PS_POS.y < FLOAT_PARAMS.x) { discard; } }\n"
        "    gl_FragColor.rgba = colour.rgba * COLOUR_XFORM;\n"
        "    vec2 world_uv = ( PS_POS.xz - OCEAN_WORLD_EXTENTS.xy ) * OCEAN_WORLD_EXTENTS.zw;\n"
        "    vec3 world_tint = texture2D( OCEAN_SAMPLER, world_uv ).rgb;\n"
        "    gl_FragColor.rgb = mix(gl_FragColor.rgb, gl_FragColor.rgb * world_tint.rgb, OCEAN_BLEND_PARAMS.x);\n"
        "    vec3 light = CalculateLightingContribution();\n"
        "    gl_FragColor.rgb *= max( light.rgb, vec3( LIGHT_OVERRIDE, LIGHT_OVERRIDE, LIGHT_OVERRIDE ) );\n"
        "}\n";

    std::string vsOut, psOut;
    const char* tag = nullptr;
    const bool txOk = hs_sprite_batch::TransformAnimBatchShaderSource(kAnimVs, vsOut, tag, true, true) &&
                      hs_sprite_batch::TransformAnimBatchShaderSource(kAnimPs, psOut, tag, true, true);

    auto pCreateSh   = reinterpret_cast<uint32_t (WINAPI*)(uint32_t)>(findP("glCreateShader"));
    auto pShSrc      = reinterpret_cast<void (WINAPI*)(uint32_t, int32_t, const char* const*, const int32_t*)>(findP("glShaderSource"));
    auto pCompSh     = reinterpret_cast<void (WINAPI*)(uint32_t)>(findP("glCompileShader"));
    auto pGetShIv    = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t, int32_t*)>(findP("glGetShaderiv"));
    auto pGetShLog   = reinterpret_cast<void (WINAPI*)(uint32_t, int32_t, int32_t*, char*)>(findP("glGetShaderInfoLog"));
    auto pCreatePr   = reinterpret_cast<uint32_t (WINAPI*)()>(findP("glCreateProgram"));
    auto pAttachSh   = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t)>(findP("glAttachShader"));
    auto pLinkPr     = reinterpret_cast<void (WINAPI*)(uint32_t)>(findP("glLinkProgram"));
    auto pGetPrIv    = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t, int32_t*)>(findP("glGetProgramiv"));
    auto pGetPrLog   = reinterpret_cast<void (WINAPI*)(uint32_t, int32_t, int32_t*, char*)>(findP("glGetProgramInfoLog"));
    auto pDelSh      = reinterpret_cast<void (WINAPI*)(uint32_t)>(findP("glDeleteShader"));
    auto pDelPr      = reinterpret_cast<void (WINAPI*)(uint32_t)>(findP("glDeleteProgram"));
    auto pUsePr      = reinterpret_cast<void (WINAPI*)(uint32_t)>(findP("glUseProgram"));
    auto pGetUni     = reinterpret_cast<int32_t (WINAPI*)(uint32_t, const char*)>(findP("glGetUniformLocation"));
    auto pGetAtt     = reinterpret_cast<int32_t (WINAPI*)(uint32_t, const char*)>(findP("glGetAttribLocation"));
    auto pUniMat4    = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, uint8_t, const float*)>(findP("glUniformMatrix4fv"));
    auto pUni1iv     = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, const int32_t*)>(findP("glUniform1iv"));
    auto pUni3fv     = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, const float*)>(findP("glUniform3fv"));
    auto pUni4fv     = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, const float*)>(findP("glUniform4fv"));
    auto pGenTex     = reinterpret_cast<void (WINAPI*)(int32_t, uint32_t*)>(findP("glGenTextures"));
    auto pDelTex     = reinterpret_cast<void (WINAPI*)(int32_t, const uint32_t*)>(findP("glDeleteTextures"));
    auto pTexImg     = reinterpret_cast<void (WINAPI*)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*)>(findP("glTexImage2D"));
    auto pTexPar     = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t, int32_t)>(findP("glTexParameteri"));
    auto pGenFbo     = reinterpret_cast<void (WINAPI*)(int32_t, uint32_t*)>(findP("glGenFramebuffers"));
    auto pDelFbo     = reinterpret_cast<void (WINAPI*)(int32_t, const uint32_t*)>(findP("glDeleteFramebuffers"));
    auto pBindFbo    = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t)>(findP("glBindFramebuffer"));
    auto pFboTex     = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t)>(findP("glFramebufferTexture2D"));
    auto pVp         = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, int32_t, int32_t)>(findP("glViewport"));
    auto pClrCol     = reinterpret_cast<void (WINAPI*)(float, float, float, float)>(findP("glClearColor"));
    auto pClr        = reinterpret_cast<void (WINAPI*)(uint32_t)>(findP("glClear"));
    auto pReadPix    = reinterpret_cast<void (WINAPI*)(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void*)>(findP("glReadPixels"));
    auto pFinish     = reinterpret_cast<void (WINAPI*)()>(findP("glFinish"));
    auto pGetErr     = reinterpret_cast<uint32_t (WINAPI*)()>(findP("glGetError"));

    uint32_t vs = pCreateSh(0x8B31u), fs = pCreateSh(0x8B30u);
    const char* vsP = vsOut.c_str(); int32_t vsL = static_cast<int32_t>(vsOut.size());
    const char* fsP = psOut.c_str(); int32_t fsL = static_cast<int32_t>(psOut.size());
    pShSrc(vs, 1, &vsP, &vsL); pCompSh(vs);
    pShSrc(fs, 1, &fsP, &fsL); pCompSh(fs);
    int32_t vsOk = 0, fsOk = 0, linkOk = 0;
    if (pGetShIv) { pGetShIv(vs, 0x8B81u, &vsOk); pGetShIv(fs, 0x8B81u, &fsOk); }
    auto pBindAtt    = reinterpret_cast<void (WINAPI*)(uint32_t, uint32_t, const char*)>(findP("glBindAttribLocation"));
    uint32_t progId = pCreatePr();
    pAttachSh(progId, vs); pAttachSh(progId, fs);
    if (pBindAtt) pBindAtt(progId, 0u, "POS2D_UV");
    pLinkPr(progId);
    if (pGetPrIv) pGetPrIv(progId, 0x8B82u, &linkOk);
    if (!vsOk || !fsOk || !linkOk) {
        char logBuf[512]{}; int32_t len = 0;
        if (!vsOk && pGetShLog) { pGetShLog(vs, 511, &len, logBuf); printf("  GPU-Driven VS log: %s\n", logBuf); }
        if (!fsOk && pGetShLog) { pGetShLog(fs, 511, &len, logBuf); printf("  GPU-Driven FS log: %s\n", logBuf); }
        if (!linkOk && pGetPrLog) { pGetPrLog(progId, 511, &len, logBuf); printf("  GPU-Driven Link log: %s\n", logBuf); }
    }
    pDelSh(vs); pDelSh(fs);

    hs_sprite_batch::GlBatchOps ops{};
    ops.glGenBuffers               = reinterpret_cast<hs_sprite_batch::GlBatchOps::GenBuffers_t>(findP("glGenBuffers"));
    ops.glDeleteBuffers            = reinterpret_cast<hs_sprite_batch::GlBatchOps::DeleteBuffers_t>(findP("glDeleteBuffers"));
    ops.glBindBuffer               = reinterpret_cast<hs_sprite_batch::GlBatchOps::BindBuffer_t>(findP("glBindBuffer"));
    ops.glBufferData               = reinterpret_cast<hs_sprite_batch::GlBatchOps::BufferData_t>(findP("glBufferData"));
    ops.glBufferSubData            = reinterpret_cast<hs_sprite_batch::GlBatchOps::BufferSubData_t>(opt.glBufferSubData);
    ops.glMapBufferRange           = reinterpret_cast<hs_sprite_batch::GlBatchOps::MapBufferRange_t>(opt.glMapBufferRange);
    ops.glUnmapBuffer              = reinterpret_cast<hs_sprite_batch::GlBatchOps::UnmapBuffer_t>(opt.glUnmapBuffer);
    ops.glActiveTexture            = reinterpret_cast<hs_sprite_batch::GlBatchOps::ActiveTexture_t>(findP("glActiveTexture"));
    ops.glBindTexture              = reinterpret_cast<hs_sprite_batch::GlBatchOps::BindTexture_t>(findP("glBindTexture"));
    ops.glUniform1f                = reinterpret_cast<hs_sprite_batch::GlBatchOps::Uniform1f_t>(findP("glUniform1f"));
    ops.glUniform1i                = reinterpret_cast<hs_sprite_batch::GlBatchOps::Uniform1i_t>(findP("glUniform1i"));
    ops.glUniform3fv               = pUni3fv;
    ops.glUniform4fv               = pUni4fv;
    ops.glUniformMatrix4fv         = pUniMat4;
    ops.glVertexAttribPointer      = reinterpret_cast<hs_sprite_batch::GlBatchOps::VertexAttribPointer_t>(findP("glVertexAttribPointer"));
    ops.glEnableVertexAttribArray  = reinterpret_cast<hs_sprite_batch::GlBatchOps::EnableVertexAttribArray_t>(findP("glEnableVertexAttribArray"));
    ops.glDisableVertexAttribArray = reinterpret_cast<hs_sprite_batch::GlBatchOps::DisableVertexAttribArray_t>(findP("glDisableVertexAttribArray"));
    ops.glDrawElements             = reinterpret_cast<hs_sprite_batch::GlBatchOps::DrawElements_t>(findP("glDrawElements"));
    ops.glDrawElementsBaseVertex   = reinterpret_cast<hs_sprite_batch::GlBatchOps::DrawElementsBaseVertex_t>(opt.glDrawElementsBaseVertex);
    ops.glGetIntegerv              = reinterpret_cast<hs_sprite_batch::GlBatchOps::GetIntegerv_t>(findP("glGetIntegerv"));
    ops.glGetBufferParameteriv     = reinterpret_cast<hs_sprite_batch::GlBatchOps::GetBufferParameteriv_t>(opt.glGetBufferParameteriv);
    ops.glGetBufferSubData         = reinterpret_cast<hs_sprite_batch::GlBatchOps::GetBufferSubData_t>(opt.glGetBufferSubData);
    ops.glBufferStorage            = reinterpret_cast<hs_sprite_batch::GlBatchOps::BufferStorage_t>(opt.glBufferStorage ? opt.glBufferStorage : opt.glBufferStorageExt);
    ops.glFenceSync                = reinterpret_cast<hs_sprite_batch::GlBatchOps::FenceSync_t>(opt.glFenceSync);
    ops.glClientWaitSync           = reinterpret_cast<hs_sprite_batch::GlBatchOps::ClientWaitSync_t>(opt.glClientWaitSync);
    ops.glDeleteSync               = reinterpret_cast<hs_sprite_batch::GlBatchOps::DeleteSync_t>(opt.glDeleteSync);
    ops.glBindBufferBase           = reinterpret_cast<hs_sprite_batch::GlBatchOps::BindBufferBase_t>(opt.glBindBufferBase);
    ops.glGetTextureHandleARB      = reinterpret_cast<hs_sprite_batch::GlBatchOps::GetTextureHandleArb_t>(opt.glGetTextureHandleARB);
    ops.glMakeTextureHandleResidentARB    = reinterpret_cast<hs_sprite_batch::GlBatchOps::MakeTextureHandleResidentArb_t>(opt.glMakeTextureHandleResidentARB);
    ops.glMakeTextureHandleNonResidentARB = reinterpret_cast<hs_sprite_batch::GlBatchOps::MakeTextureHandleNonResidentArb_t>(opt.glMakeTextureHandleNonResidentARB);

    uint32_t fbo = 0, fboTex = 0, texLight = 0;
    uint32_t tex32[32]{};
    pGenFbo(1, &fbo); pGenTex(1, &fboTex); pGenTex(1, &texLight); pGenTex(32, tex32);
    ops.glActiveTexture(hs_sprite_batch::kGlTexture0);
    ops.glBindTexture(hs_sprite_batch::kGlTexture2D, fboTex);
    pTexPar(hs_sprite_batch::kGlTexture2D, 0x2801u, 0x2600);
    pTexPar(hs_sprite_batch::kGlTexture2D, 0x2800u, 0x2600);
    pTexImg(hs_sprite_batch::kGlTexture2D, 0, 0x8058, 64, 64, 0, 0x1908u, 0x1401u, nullptr);
    pBindFbo(0x8D40u, fbo);
    pFboTex(0x8D40u, 0x8CE0u, hs_sprite_batch::kGlTexture2D, fboTex, 0);
    pVp(0, 0, 64, 64);

    const uint8_t whitePx[4] = { 255,255,255,255 };
    ops.glActiveTexture(hs_sprite_batch::kGlTexture0 + 3u);
    ops.glBindTexture(hs_sprite_batch::kGlTexture2D, texLight);
    pTexPar(hs_sprite_batch::kGlTexture2D, 0x2801u, 0x2600);
    pTexPar(hs_sprite_batch::kGlTexture2D, 0x2800u, 0x2600);
    pTexImg(hs_sprite_batch::kGlTexture2D, 0, 0x8058, 1, 1, 0, 0x1908u, 0x1401u, whitePx);

    ops.glActiveTexture(hs_sprite_batch::kGlTexture0);
    for (uint32_t i = 0u; i < 32u; ++i) {
        const uint8_t r = static_cast<uint8_t>(8u + i * 7u);
        const uint8_t g = static_cast<uint8_t>(250u - i * 7u);
        const uint8_t b = static_cast<uint8_t>((i * 37u) & 0xFFu);
        const uint8_t pxData[16] = { r,g,b,255, r,g,b,255, r,g,b,255, r,g,b,255 };
        ops.glBindTexture(hs_sprite_batch::kGlTexture2D, tex32[i]);
        pTexPar(hs_sprite_batch::kGlTexture2D, 0x2801u, 0x2600);
        pTexPar(hs_sprite_batch::kGlTexture2D, 0x2800u, 0x2600);
        pTexPar(hs_sprite_batch::kGlTexture2D, 0x2802u, 0x812F);
        pTexPar(hs_sprite_batch::kGlTexture2D, 0x2803u, 0x812F);
        pTexImg(hs_sprite_batch::kGlTexture2D, 0, 0x8058, 2, 2, 0, 0x1908u, 0x1401u, pxData);
    }

    pUsePr(progId);
    const float idMtx[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    const int32_t sampUnits[6] = { 0, 1, 2, 3, 4, 5 };
    const float paramsV[3] = { 0.0f, 1.0f, 0.0f };
    const float extV[4]    = { 0.0f, 0.0f, 1.0f, 1.0f };
    if (pUni1iv && pGetUni(progId, "SAMPLER") >= 0) pUni1iv(pGetUni(progId, "SAMPLER"), 6, sampUnits);
    if (pUni3fv && pGetUni(progId, "PARAMS") >= 0) pUni3fv(pGetUni(progId, "PARAMS"), 1, paramsV);
    if (pUni4fv && pGetUni(progId, "LIGHTMAP_WORLD_EXTENTS") >= 0) pUni4fv(pGetUni(progId, "LIGHTMAP_WORLD_EXTENTS"), 1, extV);
    pUniMat4(pGetUni(progId, "MatrixP"), 1, 0, idMtx);
    pUniMat4(pGetUni(progId, "MatrixV"), 1, 0, idMtx);
    pUniMat4(pGetUni(progId, "COLOUR_XFORM"), 1, 0, idMtx);

    hs_sprite_batch::BatchProgInfo info{};
    info.checked         = true;
    info.eligible        = true;
    info.hasGpuDriven    = true;
    info.locBatchActive  = pGetUni(progId, "HS_BATCH_ACTIVE");
    info.locMatrixW      = pGetUni(progId, "MatrixW");
    info.locColourXform  = pGetUni(progId, "COLOUR_XFORM");
    info.locFloatParams  = pGetUni(progId, "FLOAT_PARAMS");
    info.locOceanBlend   = pGetUni(progId, "OCEAN_BLEND_PARAMS");
    info.locSampler0     = pGetUni(progId, "SAMPLER[0]");
    info.locSampler1     = pGetUni(progId, "SAMPLER[1]");
    info.locSampler5     = pGetUni(progId, "SAMPLER[5]");
    info.locPos2dUv      = pGetAtt(progId, "POS2D_UV");
    info.locBatchWpos    = pGetAtt(progId, "HS_BATCH_WPOS");
    info.locBatchCMult   = pGetAtt(progId, "HS_BATCH_CMULT");
    info.locBatchCAdd    = pGetAtt(progId, "HS_BATCH_CADD");
    info.progMaxTexSlots = 16u;
    for (uint32_t s = 0; s < hs_sprite_batch::kMaxBatchTexSlots; ++s) {
        char nm[32]{};
        snprintf(nm, sizeof(nm), "HS_BATCH_TEX%u", s);
        info.locBatchTex[s] = pGetUni(progId, nm);
    }

    const float qv[16] = { -1.0f,-1.0f,0,0,  1.0f,-1.0f,1,0,  1.0f,1.0f,1,1,  -1.0f,1.0f,0,1 };
    const uint16_t qi[6] = { 0,1,2, 0,2,3 };

    hs_sprite_batch::SpriteBatcher sb;
    sb.SetOps(ops);
    sb.SetPersistentRingPreferred(true);
    sb.SetGpuDrivenPreferred(true);
    sb.SetElasticRingBudget(262144u, 524288u, 32768u);
    sb.SetShadowCacheMaxBytes(256u * 1024u * 1024u);
    sb.SetDeferHostRestore(true);
    sb.SetEnabled(true);
    sb.OnUseProgram(progId, &info);
    ops.glEnableVertexAttribArray(static_cast<uint32_t>(info.locPos2dUv));
    sb.OnEnableDisableVertexAttribArray(static_cast<uint32_t>(info.locPos2dUv), true);
    sb.OnActiveTexture(hs_sprite_batch::kGlTexture0);
    sb.OnBindBuffer(hs_sprite_batch::kGlArrayBuffer, 0u);
    sb.OnVertexAttribPointer(static_cast<uint32_t>(info.locPos2dUv), 4, hs_sprite_batch::kGlFloat, 0, 16, qv);
    sb.OnBindBuffer(hs_sprite_batch::kGlElementArrayBuffer, 0u);

    pClrCol(0, 0, 0, 1);
    pClr(0x4000u);
    while (pGetErr() != 0u) {}
    for (uint32_t i = 0u; i < 32u; ++i) {
        float mw[16] = {
            1.0f / 32.0f, 0, 0, 0,
            0,            1, 0, 0,
            0,            0, 1, 0,
            -1.0f + (2.0f * static_cast<float>(i) + 1.0f) / 32.0f, 0.0f, 0.0f, 1.0f
        };
        const float fpVal[3] = { 0.0f, (i & 1u) ? 1.0f : 0.0f, 0.0f };
        const float obVal[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        sb.TryAbsorbFloatParams(info.locFloatParams, 1, fpVal);
        sb.TryAbsorbOceanBlend(info.locOceanBlend, 1, obVal);
        sb.OnBindTexture(hs_sprite_batch::kGlTexture2D, tex32[i]);
        sb.TryAbsorbMatrixW(info.locMatrixW, 1, 0, mw);
        sb.TryAbsorbColourXform(info.locColourXform, 1, 0, idMtx);
        sb.TryBatchDrawElements(hs_sprite_batch::kGlTriangles, 6, hs_sprite_batch::kGlUnsignedShort, qi);
    }
    sb.Flush(hs_sprite_batch::FlushReason::End);
    sb.EndStickyBatchSession();
    pFinish();

    std::vector<uint8_t> fboBytes(64u * 64u * 4u, 0u);
    pReadPix(0, 0, 64, 64, 0x1908u, 0x1401u, fboBytes.data());
    const uint32_t errPartA = pGetErr();

    int partAMaxDiff = 0;
    for (uint32_t i = 0u; i < 32u; ++i) {
        const uint32_t pxX = 2u * i + 1u;
        const size_t topIdx = (48u * 64u + pxX) * 4u; // y > 0 -> always kept!
        const int er = static_cast<int>(8u + i * 7u);
        const int eg = static_cast<int>(250u - i * 7u);
        const int eb = static_cast<int>((i * 37u) & 0xFFu);
        partAMaxDiff = (std::max)(partAMaxDiff, abs(static_cast<int>(fboBytes[topIdx + 0]) - er));
        partAMaxDiff = (std::max)(partAMaxDiff, abs(static_cast<int>(fboBytes[topIdx + 1]) - eg));
        partAMaxDiff = (std::max)(partAMaxDiff, abs(static_cast<int>(fboBytes[topIdx + 2]) - eb));
        partAMaxDiff = (std::max)(partAMaxDiff, abs(static_cast<int>(fboBytes[topIdx + 3]) - 255));
        // Odd quads have FLOAT_PARAMS = (0, 1, 0), so bottom half (y < 0) is clipped to clear black!
        const size_t botIdx = (16u * 64u + pxX) * 4u;
        if (i & 1u) {
            partAMaxDiff = (std::max)(partAMaxDiff, static_cast<int>(fboBytes[botIdx + 0]));
            partAMaxDiff = (std::max)(partAMaxDiff, static_cast<int>(fboBytes[botIdx + 1]));
            partAMaxDiff = (std::max)(partAMaxDiff, static_cast<int>(fboBytes[botIdx + 2]));
        } else {
            partAMaxDiff = (std::max)(partAMaxDiff, abs(static_cast<int>(fboBytes[botIdx + 0]) - er));
            partAMaxDiff = (std::max)(partAMaxDiff, abs(static_cast<int>(fboBytes[botIdx + 1]) - eg));
            partAMaxDiff = (std::max)(partAMaxDiff, abs(static_cast<int>(fboBytes[botIdx + 2]) - eb));
        }
    }
    const bool gpuActiveOk = sb.IsGpuDrivenActive();
    const bool flush1Ok    = (sb.GetStats().flushes == 1);
    const bool res32Ok     = (sb.BindlessResidentCount() == 32u);
    const bool draws32Ok   = (sb.GpuDrivenDraws() == 32u);
    const long long flushesA = static_cast<long long>(sb.GetStats().flushes);
    const uint32_t resA    = sb.BindlessResidentCount();
    const unsigned long long gdrA = static_cast<unsigned long long>(sb.GpuDrivenDraws());
    sb.ReleaseGpuResources();

    // Part B: Deferred Shadow Replay Bit-Exact Parity (CPU 16-Slot vs GPU-Driven SSBO+Bindless)
    hs_world_shadow::GlShadowOps wops{};
    wops.glGenFramebuffers          = reinterpret_cast<decltype(wops.glGenFramebuffers)>(findP("glGenFramebuffers"));
    wops.glDeleteFramebuffers       = reinterpret_cast<decltype(wops.glDeleteFramebuffers)>(findP("glDeleteFramebuffers"));
    wops.glBindFramebuffer          = pBindFbo;
    wops.glFramebufferTexture2D     = pFboTex;
    wops.glCheckFramebufferStatus   = reinterpret_cast<decltype(wops.glCheckFramebufferStatus)>(findP("glCheckFramebufferStatus"));
    wops.glGenRenderbuffers         = reinterpret_cast<decltype(wops.glGenRenderbuffers)>(findP("glGenRenderbuffers"));
    wops.glDeleteRenderbuffers      = reinterpret_cast<decltype(wops.glDeleteRenderbuffers)>(findP("glDeleteRenderbuffers"));
    wops.glBindRenderbuffer         = reinterpret_cast<decltype(wops.glBindRenderbuffer)>(findP("glBindRenderbuffer"));
    wops.glRenderbufferStorage      = reinterpret_cast<decltype(wops.glRenderbufferStorage)>(findP("glRenderbufferStorage"));
    wops.glFramebufferRenderbuffer  = reinterpret_cast<decltype(wops.glFramebufferRenderbuffer)>(findP("glFramebufferRenderbuffer"));
    wops.glGenTextures              = pGenTex;
    wops.glDeleteTextures           = pDelTex;
    wops.glActiveTexture            = ops.glActiveTexture;
    wops.glBindTexture              = ops.glBindTexture;
    wops.glTexImage2D               = pTexImg;
    wops.glTexParameteri            = pTexPar;
    wops.glCreateShader             = pCreateSh;
    wops.glShaderSource             = pShSrc;
    wops.glCompileShader            = pCompSh;
    wops.glGetShaderiv              = pGetShIv;
    wops.glGetShaderInfoLog         = pGetShLog;
    wops.glDeleteShader             = pDelSh;
    wops.glCreateProgram            = pCreatePr;
    wops.glAttachShader             = pAttachSh;
    wops.glBindAttribLocation       = reinterpret_cast<decltype(wops.glBindAttribLocation)>(findP("glBindAttribLocation"));
    wops.glLinkProgram              = pLinkPr;
    wops.glGetProgramiv             = pGetPrIv;
    wops.glGetProgramInfoLog        = pGetPrLog;
    wops.glDeleteProgram            = pDelPr;
    wops.glUseProgram               = pUsePr;
    wops.glGetUniformLocation       = pGetUni;
    wops.glUniform1f                = ops.glUniform1f;
    wops.glUniform1i                = ops.glUniform1i;
    wops.glUniform4fv               = pUni4fv;
    wops.glUniformMatrix4fv         = pUniMat4;
    wops.glGenBuffers               = ops.glGenBuffers;
    wops.glDeleteBuffers            = ops.glDeleteBuffers;
    wops.glBindBuffer               = ops.glBindBuffer;
    wops.glBufferData               = ops.glBufferData;
    wops.glEnableVertexAttribArray  = ops.glEnableVertexAttribArray;
    wops.glDisableVertexAttribArray = ops.glDisableVertexAttribArray;
    wops.glVertexAttribPointer      = ops.glVertexAttribPointer;
    wops.glDrawArrays               = reinterpret_cast<decltype(wops.glDrawArrays)>(findP("glDrawArrays"));
    wops.glDrawElements             = ops.glDrawElements;
    wops.glDrawElementsBaseVertex   = ops.glDrawElementsBaseVertex;
    wops.glBindBufferBase           = ops.glBindBufferBase;
    wops.glViewport                 = pVp;
    wops.glClearColor               = pClrCol;
    wops.glClear                    = pClr;
    wops.glEnable                   = reinterpret_cast<decltype(wops.glEnable)>(findP("glEnable"));
    wops.glDisable                  = reinterpret_cast<decltype(wops.glDisable)>(findP("glDisable"));
    wops.glBlendFunc                = reinterpret_cast<decltype(wops.glBlendFunc)>(findP("glBlendFunc"));
    wops.glBlendFuncSeparate        = reinterpret_cast<decltype(wops.glBlendFuncSeparate)>(findP("glBlendFuncSeparate"));
    wops.glBlendEquation            = reinterpret_cast<decltype(wops.glBlendEquation)>(findP("glBlendEquation"));
    wops.glBlendEquationSeparate    = reinterpret_cast<decltype(wops.glBlendEquationSeparate)>(findP("glBlendEquationSeparate"));
    wops.glDepthFunc                = reinterpret_cast<decltype(wops.glDepthFunc)>(findP("glDepthFunc"));
    wops.glDepthMask                = reinterpret_cast<decltype(wops.glDepthMask)>(findP("glDepthMask"));
    wops.glColorMask                = reinterpret_cast<decltype(wops.glColorMask)>(findP("glColorMask"));
    wops.glGetIntegerv              = ops.glGetIntegerv;
    wops.glGetError                 = pGetErr;
    wops.logFn                      = &TestLog;

    uint32_t rtFbo = 0, rtTex = 0;
    pGenFbo(1, &rtFbo); pGenTex(1, &rtTex);
    ops.glActiveTexture(hs_sprite_batch::kGlTexture0);
    ops.glBindTexture(hs_sprite_batch::kGlTexture2D, rtTex);
    pTexPar(hs_sprite_batch::kGlTexture2D, 0x2801u, 0x2601);
    pTexPar(hs_sprite_batch::kGlTexture2D, 0x2800u, 0x2601);
    pTexImg(hs_sprite_batch::kGlTexture2D, 0, 0x8058, 256, 256, 0, 0x1908u, 0x1401u, nullptr);
    pBindFbo(0x8D40u, rtFbo);
    pFboTex(0x8D40u, 0x8CE0u, hs_sprite_batch::kGlTexture2D, rtTex, 0);

    bool wsGpuHasProg = false;
    uint32_t errPartB = 0u;
    auto runShadowParity = [&](bool useGpuDriven, std::vector<uint8_t>& outRgba) -> bool {
        hs_world_shadow::WorldShadowEngine ws;
        ws.SetOps(wops, true);
        hs_gl_cache::ThreadState glCache;
        glCache.caps[0] = 1; glCache.caps[1] = 0; glCache.caps[2] = 0; glCache.caps[3] = 0;
        glCache.blendSrc = hs_world_shadow::kGlSrcAlpha;
        glCache.blendDst = hs_world_shadow::kGlOneMinusSrcAlpha;
        glCache.blendEq  = hs_world_shadow::kGlFuncAdd;
        glCache.colorMask = 15;
        glCache.depthMask = 0;

        hs_sprite_batch::SpriteBatcher sbSh;
        sbSh.SetOps(ops);
        sbSh.SetPersistentRingPreferred(true);
        sbSh.SetGpuDrivenPreferred(useGpuDriven);
        sbSh.SetElasticRingBudget(262144u, 524288u, 32768u);
        sbSh.SetDeferHostRestore(true);
        sbSh.SetShadowActive(true);
        sbSh.SetEnabled(true);

        const float standV[16] = { -1.0f, 0.0f, 0.1f, 0.1f,  1.0f, 0.0f, 0.9f, 0.1f,  1.0f, 4.0f, 0.9f, 0.9f,  -1.0f, 4.0f, 0.1f, 0.9f };
        sbSh.OnUseProgram(progId, &info);
        sbSh.OnEnableDisableVertexAttribArray(static_cast<uint32_t>(info.locPos2dUv), true);
        sbSh.OnActiveTexture(hs_sprite_batch::kGlTexture0);
        sbSh.OnBindBuffer(hs_sprite_batch::kGlArrayBuffer, 0u);
        sbSh.OnVertexAttribPointer(static_cast<uint32_t>(info.locPos2dUv), 4, hs_sprite_batch::kGlFloat, 0, 16, standV);
        sbSh.OnBindBuffer(hs_sprite_batch::kGlElementArrayBuffer, 0u);

        const float pv[16] = {
            1.0f / 20.0f, 0, 0, 0,
            0,            0, 0, 0,
            0, 1.0f / 20.0f, 0, 0,
            0,            0, 0, 1
        };
        hs_world_shadow::ShadowConfig cfg{};
        cfg.dirWx = 0.0f; cfg.dirWz = 1.0f; cfg.stretch = 2.0f; cfg.projOn = 1.0f;
        cfg.windTime = 1.25f; cfg.swayAmp = 0.50f; cfg.heightFade = 0.50f; cfg.penumbraBias = 1.0f;
        cfg.density = 0.60f; cfg.soft = 0.80f;
        cfg.tintR = 0.10f; cfg.tintG = 0.15f; cfg.tintB = 0.40f;
        cfg.hdMode = true; cfg.skipNightmare = true;
        cfg.hasLuaCam = true; cfg.luaCamX = 0.0f; cfg.luaCamZ = 0.0f;
        cfg.luaCamRightX = 1.0f; cfg.luaCamRightZ = 0.0f;

        const int32_t vp[4] = { 0, 0, 256, 256 };
        hs_world_shadow::ShadowPassState saved{};
        saved.dstFbo = rtFbo; saved.dstProg = progId;
        memcpy(saved.vp, vp, sizeof(vp));
        saved.activeUnit = 0;

        // Frame 1: Render 4 standing foliage sprites into ShadowMask via Deferred Replay
        pBindFbo(0x8D40u, rtFbo);
        pVp(0, 0, 256, 256);
        ws.OnGroundOrOceanDraw(rtFbo, vp);
        ws.OnMatrixPV(pv);
        if (!ws.OnBeforeFirstStandingEntity(cfg, saved, glCache)) return false;
        if (useGpuDriven) wsGpuHasProg = ws.HasGpuBatchProgram();

        const float xOffsets[4] = { -8.0f, -2.5f, 3.0f, 8.5f };
        for (int k = 0; k < 4; ++k) {
            const float mw[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, xOffsets[k], 0.0f, 0.0f, 1.0f };
            sbSh.OnBindTexture(hs_sprite_batch::kGlTexture2D, tex32[k]);
            sbSh.TryAbsorbMatrixW(info.locMatrixW, 1, 0, mw);
            sbSh.TryAbsorbColourXform(info.locColourXform, 1, 0, idMtx);
            sbSh.TryBatchDrawElements(hs_sprite_batch::kGlTriangles, 6, hs_sprite_batch::kGlUnsignedShort, qi);
        }
        sbSh.Flush(hs_sprite_batch::FlushReason::End);
        sbSh.EndStickyBatchSession();

        const bool recOk = useGpuDriven
            ? ws.RecordBatchedShadowGpu(sbSh.LastFlushVboId(), sbSh.LastFlushIboId(), sbSh.LastFlushSsboId(),
                                        24u, sbSh.LastFlushBaseVertex(), sbSh.LastFlushIndexByteOffset(),
                                        sbSh.LastFlushShadowBoundsXZ(), rtFbo)
            : ws.RecordBatchedShadow(sbSh.LastFlushVboId(), sbSh.LastFlushIboId(), 24u,
                                     sbSh.LastFlushVertByteOffset(), sbSh.LastFlushIndexByteOffset(),
                                     sbSh.LastFlushTexCount(), sbSh.LastFlushTexIds(),
                                     sbSh.LastFlushShadowBoundsXZ(), rtFbo);
        if (!recOk) return false;
        ws.FlushDeferredBatchedShadows(cfg, saved, glCache);
        sbSh.OnEndFrame();
        ws.OnEndFrame();

        // Frame 2: Composite ShadowMask over turf green PrimaryRT & read back
        pBindFbo(0x8D40u, rtFbo);
        pVp(0, 0, 256, 256);
        wops.glColorMask(1, 1, 1, 1);
        pClrCol(0.4f, 0.7f, 0.3f, 1.0f);
        pClr(0x4000u);
        ws.OnGroundOrOceanDraw(rtFbo, vp);
        ws.OnMatrixPV(pv);
        if (!ws.OnBeforeFirstStandingEntity(cfg, saved, glCache)) return false;
        ws.OnEndFrame();
        pFinish();

        outRgba.resize(256u * 256u * 4u);
        pBindFbo(0x8D40u, rtFbo);
        pReadPix(0, 0, 256, 256, 0x1908u, 0x1401u, outRgba.data());
        errPartB |= pGetErr();

        sbSh.ReleaseGpuResources();
        ws.ResetContext(true);
        return true;
    };

    std::vector<uint8_t> imgCpu, imgGpu;
    const bool cpuOk = runShadowParity(false, imgCpu);
    const bool gpuOk = runShadowParity(true, imgGpu);
    int shadowMaxDiff = 0, shadowedPx = 0;
    if (cpuOk && gpuOk && imgCpu.size() == imgGpu.size()) {
        for (size_t i = 0; i + 3u < imgCpu.size(); i += 4u) {
            if (imgCpu[i + 1] < 168u) ++shadowedPx;
            for (int c = 0; c < 4; ++c) {
                const int d = abs(static_cast<int>(imgCpu[i + c]) - static_cast<int>(imgGpu[i + c]));
                if (d > shadowMaxDiff) shadowMaxDiff = d;
            }
        }
    }

    // Part C: Live GPU-Driven anim_bloom.ksh (Bindless + SSBO, outputs (0, 0, 0, alpha))
    static constexpr const char* kBloomVs =
        "#ifdef SKINNED\n"
        "\tuniform mat4 pv;\n"
        "#else\n"
        "\tuniform mat4 MatrixP;\n"
        "\tuniform mat4 MatrixV;\n"
        "\tuniform mat4 MatrixW;\n"
        "#endif\n"
        "attribute vec4 POS2D_UV;\n"
        "varying vec3 PS_TEXCOORD;\n"
        "void main() {\n"
        "#ifdef SKINNED\n"
        "\tvec3 TEXCOORD0 = vec3(0.0);\n"
        "#else\n"
        "    vec3 POSITION = vec3(POS2D_UV.xy, 0);\n"
        "    float samplerIndex = floor(POS2D_UV.z/2.0);\n"
        "    vec3 TEXCOORD0 = vec3(POS2D_UV.z - 2.0*samplerIndex, POS2D_UV.w, samplerIndex);\n"
        "    mat4 mtxPVW = MatrixP * MatrixV * MatrixW;\n"
        "    gl_Position = mtxPVW * vec4( POSITION.xyz, 1.0 );\n"
        "#endif\n"
        "\tPS_TEXCOORD = TEXCOORD0;\n"
        "}\n";
    static constexpr const char* kBloomPs =
        "#if defined( GL_ES )\n"
        "precision mediump float;\n"
        "#endif\n"
        "#if defined( TRIPLE_ATLAS )\n"
        "    uniform sampler2D SAMPLER[6];\n"
        "#else\n"
        "    uniform sampler2D SAMPLER[2];\n"
        "#endif\n"
        "varying vec3 PS_TEXCOORD;\n"
        "void main() {\n"
        "    vec4 colour;\n"
        "#if defined( TRIPLE_ATLAS )\n"
        "    colour.rgba = texture2D( SAMPLER[0], PS_TEXCOORD.xy );\n"
        "#else\n"
        "    colour.rgba = texture2D( SAMPLER[0], PS_TEXCOORD.xy );\n"
        "#endif\n"
        "    gl_FragColor.rgba = vec4( 0, 0, 0, colour.a );\n"
        "}\n";
    std::string bVsOut, bPsOut;
    const char* bTag = nullptr;
    const bool bTxOk = hs_sprite_batch::TransformAnimBatchShaderSource(kBloomVs, bVsOut, bTag, true, true) &&
                       hs_sprite_batch::TransformAnimBatchShaderSource(kBloomPs, bPsOut, bTag, true, true);
    int32_t bVsOk = 0, bPsOk = 0, bLinkOk = 0;
    uint32_t bProgId = 0u;
    bool bGpuOk = false;
    int bMaxDiff = 255;
    uint32_t errPartC = 0u;
    if (bTxOk) {
        uint32_t bvs = pCreateSh(0x8B31u), bfs = pCreateSh(0x8B30u);
        const char* bvsP = bVsOut.c_str(); int32_t bvsL = static_cast<int32_t>(bVsOut.size());
        const char* bfsP = bPsOut.c_str(); int32_t bfsL = static_cast<int32_t>(bPsOut.size());
        pShSrc(bvs, 1, &bvsP, &bvsL); pCompSh(bvs);
        pShSrc(bfs, 1, &bfsP, &bfsL); pCompSh(bfs);
        if (pGetShIv) { pGetShIv(bvs, 0x8B81u, &bVsOk); pGetShIv(bfs, 0x8B81u, &bPsOk); }
        bProgId = pCreatePr();
        pAttachSh(bProgId, bvs); pAttachSh(bProgId, bfs);
        if (pBindAtt) pBindAtt(bProgId, 0u, "POS2D_UV");
        pLinkPr(bProgId);
        if (pGetPrIv) pGetPrIv(bProgId, 0x8B82u, &bLinkOk);
        pDelSh(bvs); pDelSh(bfs);
        if (bVsOk && bPsOk && bLinkOk) {
            hs_sprite_batch::BatchProgInfo bInfo{};
            bInfo.checked         = true;
            bInfo.eligible        = true;
            bInfo.hasGpuDriven    = true;
            bInfo.isBloomPass     = true;
            bInfo.locBatchActive  = pGetUni(bProgId, "HS_BATCH_ACTIVE");
            bInfo.locMatrixW      = pGetUni(bProgId, "MatrixW");
            bInfo.locSampler0     = pGetUni(bProgId, "SAMPLER[0]");
            bInfo.locSampler1     = pGetUni(bProgId, "SAMPLER[1]");
            bInfo.locPos2dUv      = pGetAtt(bProgId, "POS2D_UV");
            bInfo.locBatchWpos    = pGetAtt(bProgId, "HS_BATCH_WPOS");
            bInfo.progMaxTexSlots = 16u;
            for (uint32_t s = 0; s < hs_sprite_batch::kMaxBatchTexSlots; ++s) {
                char nm[32]{};
                snprintf(nm, sizeof(nm), "HS_BATCH_TEX%u", s);
                bInfo.locBatchTex[s] = pGetUni(bProgId, nm);
            }
            pBindFbo(0x8D40u, fbo);
            pVp(0, 0, 64, 64);
            pUsePr(bProgId);
            pUniMat4(pGetUni(bProgId, "MatrixP"), 1, 0, idMtx);
            pUniMat4(pGetUni(bProgId, "MatrixV"), 1, 0, idMtx);
            pClrCol(0.5f, 0.5f, 0.5f, 0.0f);
            pClr(0x4000u);
            while (pGetErr() != 0u) {}

            hs_sprite_batch::SpriteBatcher sbBloom;
            sbBloom.SetOps(ops);
            sbBloom.SetPersistentRingPreferred(true);
            sbBloom.SetGpuDrivenPreferred(true);
            sbBloom.SetElasticRingBudget(262144u, 524288u, 32768u);
            sbBloom.SetDeferHostRestore(true);
            sbBloom.SetEnabled(true);
            sbBloom.SetShadowActive(false);
            sbBloom.OnUseProgram(bProgId, &bInfo);
            ops.glEnableVertexAttribArray(static_cast<uint32_t>(bInfo.locPos2dUv));
            sbBloom.OnEnableDisableVertexAttribArray(static_cast<uint32_t>(bInfo.locPos2dUv), true);
            sbBloom.OnActiveTexture(hs_sprite_batch::kGlTexture0);
            sbBloom.OnBindBuffer(hs_sprite_batch::kGlArrayBuffer, 0u);
            sbBloom.OnVertexAttribPointer(static_cast<uint32_t>(bInfo.locPos2dUv), 4, hs_sprite_batch::kGlFloat, 0, 16, qv);
            sbBloom.OnBindBuffer(hs_sprite_batch::kGlElementArrayBuffer, 0u);
            for (uint32_t i = 0u; i < 8u; ++i) {
                float mw[16] = {
                    1.0f / 8.0f, 0, 0, 0,
                    0,           1, 0, 0,
                    0,           0, 1, 0,
                    -1.0f + (2.0f * static_cast<float>(i) + 1.0f) / 8.0f, 0.0f, 0.0f, 1.0f
                };
                sbBloom.OnBindTexture(hs_sprite_batch::kGlTexture2D, tex32[i]);
                sbBloom.TryAbsorbMatrixW(bInfo.locMatrixW, 1, 0, mw);
                sbBloom.TryBatchDrawElements(hs_sprite_batch::kGlTriangles, 6, hs_sprite_batch::kGlUnsignedShort, qi);
            }
            sbBloom.Flush(hs_sprite_batch::FlushReason::End);
            sbBloom.EndStickyBatchSession();
            pFinish();
            std::vector<uint8_t> bPx(64u * 64u * 4u, 0u);
            pReadPix(0, 0, 64, 64, 0x1908u, 0x1401u, bPx.data());
            errPartC = pGetErr();
            bMaxDiff = 0;
            for (uint32_t i = 0u; i < 8u; ++i) {
                const uint32_t pxX = 8u * i + 4u;
                const size_t idx = (32u * 64u + pxX) * 4u;
                bMaxDiff = (std::max)(bMaxDiff, static_cast<int>(bPx[idx + 0]));
                bMaxDiff = (std::max)(bMaxDiff, static_cast<int>(bPx[idx + 1]));
                bMaxDiff = (std::max)(bMaxDiff, static_cast<int>(bPx[idx + 2]));
                bMaxDiff = (std::max)(bMaxDiff, abs(static_cast<int>(bPx[idx + 3]) - 255));
            }
            bGpuOk = sbBloom.IsGpuDrivenActive() && (sbBloom.GetStats().flushes == 1) && (sbBloom.GpuDrivenDraws() == 8u);
            sbBloom.ReleaseGpuResources();
        }
    }

    pBindFbo(0x8D40u, 0u);
    pDelFbo(1, &fbo);
    pDelTex(1, &fboTex);
    pDelFbo(1, &rtFbo);
    pDelTex(1, &rtTex);
    pDelTex(1, &texLight);
    pDelTex(32, tex32);
    pDelPr(progId);
    if (bProgId != 0u) pDelPr(bProgId);
    rt.pWglMakeCurrent(nullptr, nullptr);
    rt.pWglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);

    const bool ok = txOk && vsOk && fsOk && linkOk &&
                    gpuActiveOk && flush1Ok && res32Ok && draws32Ok &&
                    (partAMaxDiff == 0) && (errPartA == 0u) &&
                    cpuOk && gpuOk && wsGpuHasProg && (shadowedPx > 100) &&
                    (shadowMaxDiff == 0) && (errPartB == 0u) &&
                    bTxOk && bVsOk && bPsOk && bLinkOk && bGpuOk &&
                    (bMaxDiff == 0) && (errPartC == 0u);
    printf("  GPU-Driven Bindless+SSBO (32 tex -> 1 flush): tx=%d sh=%d/%d/%d active=%d flush=%lld res=%u gdr=%llu maxDiff=%d err=0x%X\n",
           txOk ? 1 : 0, vsOk, fsOk, linkOk, gpuActiveOk ? 1 : 0,
           flushesA, resA, gdrA, partAMaxDiff, errPartA);
    printf("  GPU-Driven anim_bloom (8 tex -> 1 flush): tx=%d sh=%d/%d/%d gpu=%d maxDiff=%d err=0x%X\n",
           bTxOk ? 1 : 0, bVsOk, bPsOk, bLinkOk, bGpuOk ? 1 : 0, bMaxDiff, errPartC);
    printf("  Deferred Shadow Parity (CPU 16-slot vs GPU SSBO+Bindless): cpu=%d gpu=%d gpuProg=%d shadowedPx=%d maxPixelDiff=%d err=0x%X -> %s\n",
           cpuOk ? 1 : 0, gpuOk ? 1 : 0, wsGpuHasProg ? 1 : 0, shadowedPx, shadowMaxDiff, errPartB,
           ok ? "PASS" : "FAIL");
    return ok;
}

static bool TestPboTexRingLiveGpu() {
    printf("\n=== Live RTX GPU Pixel-Exact Verification: PersistentPboTexRing (DXT5 + RGBA8) ===\n");
    hs_native_gl::WglRuntime rt{};
    const auto probe = hs_native_gl::ProbeNativeEs2Support(hs_native_gl::BackendMode::Auto, rt);
    if (!probe.supported || !probe.hasBufferStorage) {
        printf("  [SKIP] BufferStorage unavailable -> PASS\n");
        return true;
    }
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"HS_PboTexLive", WS_POPUP, 0, 0, 64, 64,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return false;
    HDC hdc = nullptr;
    HGLRC rc = nullptr;
    if (!hs_native_gl::CreateGameEs2Context(rt, hwnd, hdc, rc) || !rt.pWglMakeCurrent(hdc, rc)) {
        if (hdc) ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return false;
    }
    void* procs[76]{};
    hs_native_gl::OptionalGlProcs opt{};
    if (hs_native_gl::ResolveLiveGlesTable(rt, procs, opt) != 76) {
        rt.pWglMakeCurrent(nullptr, nullptr);
        rt.pWglDeleteContext(rc);
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return false;
    }
    auto findP = [&](const char* nm) -> void* {
        for (size_t i = 0; i < 76; ++i)
            if (std::strcmp(hs_native_gl::kDstGles2Imports[i].name, nm) == 0) return procs[i];
        return nullptr;
    };

    using GenTex_t   = void (WINAPI*)(int32_t, uint32_t*);
    using DelTex_t   = void (WINAPI*)(int32_t, const uint32_t*);
    using BindTex_t  = void (WINAPI*)(uint32_t, uint32_t);
    using TexPar_t   = void (WINAPI*)(uint32_t, uint32_t, int32_t);
    using TexImg_t   = void (WINAPI*)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*);
    using CompImg_t  = void (WINAPI*)(uint32_t, int32_t, uint32_t, int32_t, int32_t, int32_t, int32_t, const void*);
    using GenFbo_t   = void (WINAPI*)(int32_t, uint32_t*);
    using DelFbo_t   = void (WINAPI*)(int32_t, const uint32_t*);
    using BindFbo_t  = void (WINAPI*)(uint32_t, uint32_t);
    using FboTex_t   = void (WINAPI*)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t);
    using CreateSh_t = uint32_t (WINAPI*)(uint32_t);
    using ShSrc_t    = void (WINAPI*)(uint32_t, int32_t, const char* const*, const int32_t*);
    using CompSh_t   = void (WINAPI*)(uint32_t);
    using DelSh_t    = void (WINAPI*)(uint32_t);
    using CreatePr_t = uint32_t (WINAPI*)();
    using AttachSh_t = void (WINAPI*)(uint32_t, uint32_t);
    using BindAtt_t  = void (WINAPI*)(uint32_t, uint32_t, const char*);
    using LinkPr_t   = void (WINAPI*)(uint32_t);
    using DelPr_t    = void (WINAPI*)(uint32_t);
    using UsePr_t    = void (WINAPI*)(uint32_t);
    using GetUni_t   = int32_t (WINAPI*)(uint32_t, const char*);
    using Uni1i_t    = void (WINAPI*)(int32_t, int32_t);
    using ActTex_t   = void (WINAPI*)(uint32_t);
    using Vp_t       = void (WINAPI*)(int32_t, int32_t, int32_t, int32_t);
    using EnAtt_t    = void (WINAPI*)(uint32_t);
    using AttPtr_t   = void (WINAPI*)(uint32_t, int32_t, uint32_t, uint8_t, int32_t, const void*);
    using DrawArr_t  = void (WINAPI*)(uint32_t, int32_t, int32_t);
    using ReadPix_t  = void (WINAPI*)(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void*);
    using GetErr_t   = uint32_t (WINAPI*)();
    using GetInt_t   = void (WINAPI*)(uint32_t, int32_t*);
    using Finish_t   = void (WINAPI*)();

    auto pGenTex   = reinterpret_cast<GenTex_t>(findP("glGenTextures"));
    auto pDelTex   = reinterpret_cast<DelTex_t>(findP("glDeleteTextures"));
    auto pBindTex  = reinterpret_cast<BindTex_t>(findP("glBindTexture"));
    auto pTexPar   = reinterpret_cast<TexPar_t>(findP("glTexParameteri"));
    auto pTexImg   = reinterpret_cast<TexImg_t>(findP("glTexImage2D"));
    auto pCompImg  = reinterpret_cast<CompImg_t>(findP("glCompressedTexImage2D"));
    auto pGenFbo   = reinterpret_cast<GenFbo_t>(findP("glGenFramebuffers"));
    auto pDelFbo   = reinterpret_cast<DelFbo_t>(findP("glDeleteFramebuffers"));
    auto pBindFbo  = reinterpret_cast<BindFbo_t>(findP("glBindFramebuffer"));
    auto pFboTex   = reinterpret_cast<FboTex_t>(findP("glFramebufferTexture2D"));
    auto pCreateSh = reinterpret_cast<CreateSh_t>(findP("glCreateShader"));
    auto pShSrc    = reinterpret_cast<ShSrc_t>(findP("glShaderSource"));
    auto pCompSh   = reinterpret_cast<CompSh_t>(findP("glCompileShader"));
    auto pDelSh    = reinterpret_cast<DelSh_t>(findP("glDeleteShader"));
    auto pCreatePr = reinterpret_cast<CreatePr_t>(findP("glCreateProgram"));
    auto pAttachSh = reinterpret_cast<AttachSh_t>(findP("glAttachShader"));
    auto pBindAtt  = reinterpret_cast<BindAtt_t>(findP("glBindAttribLocation"));
    auto pLinkPr   = reinterpret_cast<LinkPr_t>(findP("glLinkProgram"));
    auto pDelPr    = reinterpret_cast<DelPr_t>(findP("glDeleteProgram"));
    auto pUsePr    = reinterpret_cast<UsePr_t>(findP("glUseProgram"));
    auto pGetUni   = reinterpret_cast<GetUni_t>(findP("glGetUniformLocation"));
    auto pUni1i    = reinterpret_cast<Uni1i_t>(findP("glUniform1i"));
    auto pActTex   = reinterpret_cast<ActTex_t>(findP("glActiveTexture"));
    auto pVp       = reinterpret_cast<Vp_t>(findP("glViewport"));
    auto pEnAtt    = reinterpret_cast<EnAtt_t>(findP("glEnableVertexAttribArray"));
    auto pAttPtr   = reinterpret_cast<AttPtr_t>(findP("glVertexAttribPointer"));
    auto pDrawArr  = reinterpret_cast<DrawArr_t>(findP("glDrawArrays"));
    auto pReadPix  = reinterpret_cast<ReadPix_t>(findP("glReadPixels"));
    auto pGetErr   = reinterpret_cast<GetErr_t>(findP("glGetError"));
    auto pGetInt   = reinterpret_cast<GetInt_t>(findP("glGetIntegerv"));
    auto pFinish   = reinterpret_cast<Finish_t>(findP("glFinish"));

    hs_pbo_tex::GlPboOps pops{};
    pops.glGenBuffers           = reinterpret_cast<hs_pbo_tex::GlPboOps::GenBuffers_t>(findP("glGenBuffers"));
    pops.glDeleteBuffers        = reinterpret_cast<hs_pbo_tex::GlPboOps::DeleteBuffers_t>(findP("glDeleteBuffers"));
    pops.glBindBuffer           = reinterpret_cast<hs_pbo_tex::GlPboOps::BindBuffer_t>(findP("glBindBuffer"));
    pops.glBufferStorage        = reinterpret_cast<hs_pbo_tex::GlPboOps::BufferStorage_t>(
        opt.glBufferStorage ? opt.glBufferStorage : opt.glBufferStorageExt);
    pops.glMapBufferRange       = reinterpret_cast<hs_pbo_tex::GlPboOps::MapBufferRange_t>(opt.glMapBufferRange);
    pops.glUnmapBuffer          = reinterpret_cast<hs_pbo_tex::GlPboOps::UnmapBuffer_t>(opt.glUnmapBuffer);
    pops.glGetBufferParameteriv = reinterpret_cast<hs_pbo_tex::GlPboOps::GetBufferParameteriv_t>(opt.glGetBufferParameteriv);
    pops.glFenceSync            = reinterpret_cast<hs_pbo_tex::GlPboOps::FenceSync_t>(opt.glFenceSync);
    pops.glClientWaitSync       = reinterpret_cast<hs_pbo_tex::GlPboOps::ClientWaitSync_t>(opt.glClientWaitSync);
    pops.glDeleteSync           = reinterpret_cast<hs_pbo_tex::GlPboOps::DeleteSync_t>(opt.glDeleteSync);

    hs_pbo_tex::PersistentPboTexRing pbo;
    pbo.SetOps(pops);
    pbo.SetSlotBytes(4u * 1024u * 1024u);
    pbo.SetEnabled(true);

    // Simple passthrough textured quad program
    static constexpr const char* kVs =
        "#version 100\n"
        "attribute vec2 aPos;\n"
        "varying vec2 vUv;\n"
        "void main() { vUv = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
    static constexpr const char* kFs =
        "#version 100\n"
        "precision highp float;\n"
        "uniform sampler2D uTex;\n"
        "varying vec2 vUv;\n"
        "void main() { gl_FragColor = texture2D(uTex, vUv); }\n";
    uint32_t vs = pCreateSh(0x8B31u), fs = pCreateSh(0x8B30u);
    pShSrc(vs, 1, &kVs, nullptr); pCompSh(vs);
    pShSrc(fs, 1, &kFs, nullptr); pCompSh(fs);
    uint32_t prog = pCreatePr();
    pAttachSh(prog, vs); pAttachSh(prog, fs);
    pBindAtt(prog, 0u, "aPos");
    pLinkPr(prog);
    pDelSh(vs); pDelSh(fs);

    uint32_t fbo = 0u, fboTex = 0u, texDxtDir = 0u, texDxtPbo = 0u, texRgbDir = 0u, texRgbPbo = 0u;
    pGenFbo(1, &fbo);
    pGenTex(1, &fboTex);
    pGenTex(1, &texDxtDir);
    pGenTex(1, &texDxtPbo);
    pGenTex(1, &texRgbDir);
    pGenTex(1, &texRgbPbo);

    auto setupNearest = [&](uint32_t tex) {
        pBindTex(0x0DE1u, tex);
        pTexPar(0x0DE1u, 0x2801u, 0x2600);
        pTexPar(0x0DE1u, 0x2800u, 0x2600);
        pTexPar(0x0DE1u, 0x2802u, 0x812F);
        pTexPar(0x0DE1u, 0x2803u, 0x812F);
    };
    setupNearest(fboTex);
    pTexImg(0x0DE1u, 0, 0x1908, 256, 256, 0, 0x1908u, 0x1401u, nullptr);
    pBindFbo(0x8D40u, fbo);
    pFboTex(0x8D40u, 0x8CE0u, 0x0DE1u, fboTex, 0);

    const float quad[8] = { -1,-1, 1,-1, -1,1, 1,1 };
    auto drawAndRead = [&](uint32_t tex, int32_t sz, std::vector<uint8_t>& out) {
        pBindFbo(0x8D40u, fbo);
        pVp(0, 0, sz, sz);
        pUsePr(prog);
        pActTex(0x84C0u);
        pBindTex(0x0DE1u, tex);
        pUni1i(pGetUni(prog, "uTex"), 0);
        pops.glBindBuffer(0x8892u /*GL_ARRAY_BUFFER*/, 0u);
        pEnAtt(0u);
        pAttPtr(0u, 2, 0x1406u, 0u, 0, quad);
        pDrawArr(0x0005u /*GL_TRIANGLE_STRIP*/, 0, 4);
        pFinish();
        out.resize(static_cast<size_t>(sz) * static_cast<size_t>(sz) * 4u);
        pReadPix(0, 0, sz, sz, 0x1908u, 0x1401u, out.data());
    };

    while (pGetErr() != 0u) {}

    // Part A: 128x128 DXT5 compressed texture (16,384 bytes)
    std::vector<uint8_t> dxt5(128u * 128u);
    for (size_t i = 0; i < dxt5.size(); ++i) {
        dxt5[i] = static_cast<uint8_t>((i * 53u + (i >> 4) * 19u + 11u) & 0xFFu);
    }
    setupNearest(texDxtDir);
    pCompImg(0x0DE1u, 0, 0x83F3u, 128, 128, 0, static_cast<int32_t>(dxt5.size()), dxt5.data());
    setupNearest(texDxtPbo);
    const bool dxtPboOk = pbo.TryUploadCompressedTexImage2D(
        pCompImg, 0x0DE1u, 0, 0x83F3u, 128, 128, 0, static_cast<int32_t>(dxt5.size()), dxt5.data());
    pbo.OnEndFrame();

    std::vector<uint8_t> pxDxtDir, pxDxtPbo;
    drawAndRead(texDxtDir, 128, pxDxtDir);
    drawAndRead(texDxtPbo, 128, pxDxtPbo);
    int dxtMaxDiff = 0;
    for (size_t i = 0; i < pxDxtDir.size(); ++i) {
        const int d = std::abs(static_cast<int>(pxDxtDir[i]) - static_cast<int>(pxDxtPbo[i]));
        if (d > dxtMaxDiff) dxtMaxDiff = d;
    }

    // Part B: 4 consecutive frames of 256x256 RGBA8 (262,144 bytes each) -> wraps 3-frame ring & waits slot 0 fence!
    std::vector<uint8_t> rgba(256u * 256u * 4u);
    bool rgbaPboAllOk = true;
    for (int f = 0; f < 4; ++f) {
        for (size_t i = 0; i < rgba.size(); ++i) {
            rgba[i] = static_cast<uint8_t>(((i * 17u) + static_cast<size_t>(f) * 61u + 5u) & 0xFFu);
        }
        setupNearest(texRgbPbo);
        if (!pbo.TryUploadTexImage2D(pTexImg, 0x0DE1u, 0, 0x1908, 256, 256, 0, 0x1908u, 0x1401u, rgba.data())) {
            rgbaPboAllOk = false;
        }
        pbo.OnEndFrame();
    }
    setupNearest(texRgbDir);
    pTexImg(0x0DE1u, 0, 0x1908, 256, 256, 0, 0x1908u, 0x1401u, rgba.data());

    std::vector<uint8_t> pxRgbDir, pxRgbPbo;
    drawAndRead(texRgbDir, 256, pxRgbDir);
    drawAndRead(texRgbPbo, 256, pxRgbPbo);
    int rgbaMaxDiff = 0;
    for (size_t i = 0; i < pxRgbDir.size(); ++i) {
        const int d = std::abs(static_cast<int>(pxRgbDir[i]) - static_cast<int>(pxRgbPbo[i]));
        if (d > rgbaMaxDiff) rgbaMaxDiff = d;
    }

    int32_t boundUnpack = -1;
    pGetInt(0x88EFu /*GL_PIXEL_UNPACK_BUFFER_BINDING*/, &boundUnpack);
    const uint32_t glErr = pGetErr();
    const bool activeOk = pbo.IsActive();
    const uint64_t upCnt = pbo.UploadCount();
    const uint64_t ovfCnt = pbo.OverflowFallbacks();
    const uint64_t toCnt = pbo.WaitTimeouts();

    pbo.ReleaseGpu(true);
    pBindFbo(0x8D40u, 0u);
    pDelFbo(1, &fbo);
    pDelTex(1, &fboTex);
    pDelTex(1, &texDxtDir);
    pDelTex(1, &texDxtPbo);
    pDelTex(1, &texRgbDir);
    pDelTex(1, &texRgbPbo);
    pDelPr(prog);
    rt.pWglMakeCurrent(nullptr, nullptr);
    rt.pWglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);

    const bool ok = activeOk && dxtPboOk && rgbaPboAllOk &&
                    (dxtMaxDiff == 0) && (rgbaMaxDiff == 0) &&
                    (boundUnpack == 0) && (glErr == 0u) &&
                    (upCnt == 5u) && (ovfCnt == 0u) && (toCnt == 0u);
    printf("  PersistentPboTexRing Live RTX: active=%d up=%llu ovf=%llu to=%llu dxt5MaxDiff=%d rgba8MaxDiff=%d unpackBind=%d err=0x%X -> %s\n",
           activeOk ? 1 : 0, static_cast<unsigned long long>(upCnt),
           static_cast<unsigned long long>(ovfCnt), static_cast<unsigned long long>(toCnt),
           dxtMaxDiff, rgbaMaxDiff, boundUnpack, glErr, ok ? "PASS" : "FAIL");
    return ok;
}

static bool TestPhase2LiveGpuSuite() {
    printf("\n=== Live RTX GPU Phase 2 Verification: PreallocTexIdPool + Vectorized GpuDriven + Zero-Stall Shadow/Clarity ===\n");
    hs_native_gl::WglRuntime rt{};
    const auto probe = hs_native_gl::ProbeNativeEs2Support(hs_native_gl::BackendMode::Auto, rt);
    if (!probe.supported || !probe.hasBufferStorage || !probe.hasSsbo ||
        !probe.hasBindlessTexture || !probe.hasComputeShader || !probe.hasImageLoadStore) {
        printf("  [SKIP] GL 4.3+ / Bindless / Compute unavailable -> PASS\n");
        return true;
    }
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"HS_Phase2Live", WS_POPUP, 0, 0, 64, 64,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return false;
    HDC hdc = nullptr;
    HGLRC rc = nullptr;
    if (!hs_native_gl::CreateGameEs2Context(rt, hwnd, hdc, rc) || !rt.pWglMakeCurrent(hdc, rc)) {
        if (hdc) ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return false;
    }
    void* procs[76]{};
    hs_native_gl::OptionalGlProcs opt{};
    if (hs_native_gl::ResolveLiveGlesTable(rt, procs, opt) != 76) {
        rt.pWglMakeCurrent(nullptr, nullptr);
        rt.pWglDeleteContext(rc);
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return false;
    }
    auto findP = [&](const char* nm) -> void* {
        for (size_t i = 0; i < 76; ++i)
            if (std::strcmp(hs_native_gl::kDstGles2Imports[i].name, nm) == 0) return procs[i];
        return nullptr;
    };

    using GenTex_t    = void (WINAPI*)(int32_t, uint32_t*);
    using DelTex_t    = void (WINAPI*)(int32_t, const uint32_t*);
    using BindTex_t   = void (WINAPI*)(uint32_t, uint32_t);
    using TexPar_t    = void (WINAPI*)(uint32_t, uint32_t, int32_t);
    using TexImg_t    = void (WINAPI*)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*);
    using CompImg_t   = void (WINAPI*)(uint32_t, int32_t, uint32_t, int32_t, int32_t, int32_t, int32_t, const void*);
    using IsTex_t     = uint8_t (WINAPI*)(uint32_t);
    using GenFbo_t    = void (WINAPI*)(int32_t, uint32_t*);
    using DelFbo_t    = void (WINAPI*)(int32_t, const uint32_t*);
    using BindFbo_t   = void (WINAPI*)(uint32_t, uint32_t);
    using FboTex_t    = void (WINAPI*)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t);
    using CreateSh_t  = uint32_t (WINAPI*)(uint32_t);
    using ShSrc_t     = void (WINAPI*)(uint32_t, int32_t, const char* const*, const int32_t*);
    using CompSh_t    = void (WINAPI*)(uint32_t);
    using GetShIv_t   = void (WINAPI*)(uint32_t, uint32_t, int32_t*);
    using DelSh_t     = void (WINAPI*)(uint32_t);
    using CreatePr_t  = uint32_t (WINAPI*)();
    using AttachSh_t  = void (WINAPI*)(uint32_t, uint32_t);
    using BindAtt_t   = void (WINAPI*)(uint32_t, uint32_t, const char*);
    using LinkPr_t    = void (WINAPI*)(uint32_t);
    using GetPrIv_t   = void (WINAPI*)(uint32_t, uint32_t, int32_t*);
    using DelPr_t     = void (WINAPI*)(uint32_t);
    using UsePr_t     = void (WINAPI*)(uint32_t);
    using GetUni_t    = int32_t (WINAPI*)(uint32_t, const char*);
    using GetAtt_t    = int32_t (WINAPI*)(uint32_t, const char*);
    using Uni1i_t     = void (WINAPI*)(int32_t, int32_t);
    using Uni1iv_t    = void (WINAPI*)(int32_t, int32_t, const int32_t*);
    using Uni3fv_t    = void (WINAPI*)(int32_t, int32_t, const float*);
    using Uni4fv_t    = void (WINAPI*)(int32_t, int32_t, const float*);
    using UniMat4_t   = void (WINAPI*)(int32_t, int32_t, uint8_t, const float*);
    using ActTex_t    = void (WINAPI*)(uint32_t);
    using Vp_t        = void (WINAPI*)(int32_t, int32_t, int32_t, int32_t);
    using ClrCol_t    = void (WINAPI*)(float, float, float, float);
    using Clr_t       = void (WINAPI*)(uint32_t);
    using EnAtt_t     = void (WINAPI*)(uint32_t);
    using AttPtr_t    = void (WINAPI*)(uint32_t, int32_t, uint32_t, uint8_t, int32_t, const void*);
    using DrawArr_t   = void (WINAPI*)(uint32_t, int32_t, int32_t);
    using ReadPix_t   = void (WINAPI*)(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void*);
    using GetErr_t    = uint32_t (WINAPI*)();
    using GetInt_t    = void (WINAPI*)(uint32_t, int32_t*);
    using IsEn_t      = uint8_t (WINAPI*)(uint32_t);
    using Finish_t    = void (WINAPI*)();

    auto pGenTex   = reinterpret_cast<GenTex_t>(findP("glGenTextures"));
    auto pDelTex   = reinterpret_cast<DelTex_t>(findP("glDeleteTextures"));
    auto pBindTex  = reinterpret_cast<BindTex_t>(findP("glBindTexture"));
    auto pTexPar   = reinterpret_cast<TexPar_t>(findP("glTexParameteri"));
    auto pTexImg   = reinterpret_cast<TexImg_t>(findP("glTexImage2D"));
    auto pCompImg  = reinterpret_cast<CompImg_t>(findP("glCompressedTexImage2D"));
    auto pIsTex    = reinterpret_cast<IsTex_t>(rt.ResolveGlProc("glIsTexture"));
    auto pGenFbo   = reinterpret_cast<GenFbo_t>(findP("glGenFramebuffers"));
    auto pDelFbo   = reinterpret_cast<DelFbo_t>(findP("glDeleteFramebuffers"));
    auto pBindFbo  = reinterpret_cast<BindFbo_t>(findP("glBindFramebuffer"));
    auto pFboTex   = reinterpret_cast<FboTex_t>(findP("glFramebufferTexture2D"));
    auto pCreateSh = reinterpret_cast<CreateSh_t>(findP("glCreateShader"));
    auto pShSrc    = reinterpret_cast<ShSrc_t>(findP("glShaderSource"));
    auto pCompSh   = reinterpret_cast<CompSh_t>(findP("glCompileShader"));
    auto pGetShIv  = reinterpret_cast<GetShIv_t>(findP("glGetShaderiv"));
    auto pDelSh    = reinterpret_cast<DelSh_t>(findP("glDeleteShader"));
    auto pCreatePr = reinterpret_cast<CreatePr_t>(findP("glCreateProgram"));
    auto pAttachSh = reinterpret_cast<AttachSh_t>(findP("glAttachShader"));
    auto pBindAtt  = reinterpret_cast<BindAtt_t>(findP("glBindAttribLocation"));
    auto pLinkPr   = reinterpret_cast<LinkPr_t>(findP("glLinkProgram"));
    auto pGetPrIv  = reinterpret_cast<GetPrIv_t>(findP("glGetProgramiv"));
    auto pDelPr    = reinterpret_cast<DelPr_t>(findP("glDeleteProgram"));
    auto pUsePr    = reinterpret_cast<UsePr_t>(findP("glUseProgram"));
    auto pGetUni   = reinterpret_cast<GetUni_t>(findP("glGetUniformLocation"));
    auto pGetAtt   = reinterpret_cast<GetAtt_t>(findP("glGetAttribLocation"));
    auto pUni1i    = reinterpret_cast<Uni1i_t>(findP("glUniform1i"));
    auto pUni1iv   = reinterpret_cast<Uni1iv_t>(findP("glUniform1iv"));
    auto pUni3fv   = reinterpret_cast<Uni3fv_t>(findP("glUniform3fv"));
    auto pUni4fv   = reinterpret_cast<Uni4fv_t>(findP("glUniform4fv"));
    auto pUniMat4  = reinterpret_cast<UniMat4_t>(findP("glUniformMatrix4fv"));
    auto pActTex   = reinterpret_cast<ActTex_t>(findP("glActiveTexture"));
    auto pVp       = reinterpret_cast<Vp_t>(findP("glViewport"));
    auto pClrCol   = reinterpret_cast<ClrCol_t>(findP("glClearColor"));
    auto pClr      = reinterpret_cast<Clr_t>(findP("glClear"));
    auto pEnAtt    = reinterpret_cast<EnAtt_t>(findP("glEnableVertexAttribArray"));
    auto pAttPtr   = reinterpret_cast<AttPtr_t>(findP("glVertexAttribPointer"));
    auto pDrawArr  = reinterpret_cast<DrawArr_t>(findP("glDrawArrays"));
    auto pReadPix  = reinterpret_cast<ReadPix_t>(findP("glReadPixels"));
    auto pGetErr   = reinterpret_cast<GetErr_t>(findP("glGetError"));
    auto pGetInt   = reinterpret_cast<GetInt_t>(findP("glGetIntegerv"));
    auto pIsEn     = reinterpret_cast<IsEn_t>(opt.glIsEnabled);
    auto pFinish   = reinterpret_cast<Finish_t>(findP("glFinish"));

    while (pGetErr() != 0u) {}

    // -------------------------------------------------------------------------
    // Part A: Live RTX 4060 PreallocTexIdPool (acquire, upload RGBA8/DXT5,
    //         pixel-exact readback, low-water refill & zero-leak ReleaseGpu)
    // -------------------------------------------------------------------------
    bool partAOk = false;
    {
        hs_pbo_tex::GlTexPoolOps tops{};
        tops.glGenTextures    = pGenTex;
        tops.glDeleteTextures = pDelTex;
        hs_pbo_tex::PreallocTexIdPool pool;
        pool.SetOps(tops);
        pool.SetEnabled(true);
        pool.RefillIfLow();

        uint32_t texPoolRgba = 0u, texPoolDxt = 0u, texDirRgba = 0u, texDirDxt = 0u;
        uint32_t batch8[8]{};
        const bool acqOk = (pool.AvailableCount() == 256u) &&
                           pool.TryAcquire(1, &texPoolRgba) &&
                           pool.TryAcquire(1, &texPoolDxt) &&
                           pool.TryAcquire(8, batch8) &&
                           (pool.AvailableCount() == 246u) &&
                           (pool.HitCount() == 3u) &&
                           (texPoolRgba != 0u && texPoolDxt != 0u && texPoolRgba != texPoolDxt);
        pGenTex(1, &texDirRgba);
        pGenTex(1, &texDirDxt);

        static constexpr const char* kVs =
            "#version 100\nattribute vec2 aPos; varying vec2 vUv;\n"
            "void main() { vUv = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
        static constexpr const char* kFs =
            "#version 100\nprecision highp float; uniform sampler2D uTex; varying vec2 vUv;\n"
            "void main() { gl_FragColor = texture2D(uTex, vUv); }\n";
        uint32_t vs = pCreateSh(0x8B31u), fs = pCreateSh(0x8B30u);
        pShSrc(vs, 1, &kVs, nullptr); pCompSh(vs);
        pShSrc(fs, 1, &kFs, nullptr); pCompSh(fs);
        uint32_t pr = pCreatePr();
        pAttachSh(pr, vs); pAttachSh(pr, fs); pBindAtt(pr, 0u, "aPos"); pLinkPr(pr);
        pDelSh(vs); pDelSh(fs);

        uint32_t fbo = 0u, fboTex = 0u;
        pGenFbo(1, &fbo); pGenTex(1, &fboTex);
        auto prepTex = [&](uint32_t t) {
            pActTex(0x84C0u); pBindTex(0x0DE1u, t);
            pTexPar(0x0DE1u, 0x2801u, 0x2600); pTexPar(0x0DE1u, 0x2800u, 0x2600);
            pTexPar(0x0DE1u, 0x2802u, 0x812F); pTexPar(0x0DE1u, 0x2803u, 0x812F);
        };
        prepTex(fboTex);
        pTexImg(0x0DE1u, 0, 0x1908, 64, 64, 0, 0x1908u, 0x1401u, nullptr);
        pBindFbo(0x8D40u, fbo); pFboTex(0x8D40u, 0x8CE0u, 0x0DE1u, fboTex, 0);

        std::vector<uint8_t> rgba(64u * 64u * 4u);
        for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = static_cast<uint8_t>((i * 41u + 9u) & 0xFFu);
        std::vector<uint8_t> dxt5(64u * 64u);
        for (size_t i = 0; i < dxt5.size(); ++i) dxt5[i] = static_cast<uint8_t>((i * 23u + 17u) & 0xFFu);
        prepTex(texPoolRgba); pTexImg(0x0DE1u, 0, 0x1908, 64, 64, 0, 0x1908u, 0x1401u, rgba.data());
        prepTex(texDirRgba);  pTexImg(0x0DE1u, 0, 0x1908, 64, 64, 0, 0x1908u, 0x1401u, rgba.data());
        prepTex(texPoolDxt);  pCompImg(0x0DE1u, 0, 0x83F3u, 64, 64, 0, static_cast<int32_t>(dxt5.size()), dxt5.data());
        prepTex(texDirDxt);   pCompImg(0x0DE1u, 0, 0x83F3u, 64, 64, 0, static_cast<int32_t>(dxt5.size()), dxt5.data());

        const float quad[8] = { -1,-1, 1,-1, -1,1, 1,1 };
        auto readTex = [&](uint32_t t, std::vector<uint8_t>& out) {
            pBindFbo(0x8D40u, fbo); pVp(0, 0, 64, 64); pUsePr(pr);
            pActTex(0x84C0u); pBindTex(0x0DE1u, t); pUni1i(pGetUni(pr, "uTex"), 0);
            reinterpret_cast<void(WINAPI*)(uint32_t,uint32_t)>(findP("glBindBuffer"))(0x8892u, 0u);
            pEnAtt(0u); pAttPtr(0u, 2, 0x1406u, 0u, 0, quad);
            pDrawArr(0x0005u, 0, 4); pFinish();
            out.resize(64u * 64u * 4u);
            pReadPix(0, 0, 64, 64, 0x1908u, 0x1401u, out.data());
        };
        std::vector<uint8_t> rP, rD, dP, dD;
        readTex(texPoolRgba, rP); readTex(texDirRgba, rD);
        readTex(texPoolDxt,  dP); readTex(texDirDxt,  dD);
        int maxDiff = 0;
        for (size_t i = 0; i < rP.size(); ++i) {
            maxDiff = (std::max)(maxDiff, abs(static_cast<int>(rP[i]) - static_cast<int>(rD[i])));
            maxDiff = (std::max)(maxDiff, abs(static_cast<int>(dP[i]) - static_cast<int>(dD[i])));
        }
        // Drain below low-water (64) & refill -> 256, then ReleaseGpu(true) deletes all unused IDs with 0 GL error
        std::vector<uint32_t> drain(190u, 0u);
        const bool drainOk = pool.TryAcquire(190, drain.data()) && (pool.AvailableCount() == 56u);
        pool.RefillIfLow(64u);
        const bool refillOk = (pool.AvailableCount() == 256u) && (pool.RefillCount() == 2u);
        pDelTex(190, drain.data());
        pDelTex(8, batch8);
        pDelTex(1, &texPoolRgba); pDelTex(1, &texPoolDxt);
        pDelTex(1, &texDirRgba);  pDelTex(1, &texDirDxt);
        pool.ReleaseGpu(true);
        const bool releasedOk = (pool.AvailableCount() == 0u) && (!pIsTex || pIsTex(texPoolRgba) == 0u);
        pBindFbo(0x8D40u, 0u);
        pDelFbo(1, &fbo); pDelTex(1, &fboTex); pDelPr(pr);
        const uint32_t errA = pGetErr();
        partAOk = acqOk && drainOk && refillOk && releasedOk && (maxDiff == 0) && (errA == 0u);
        printf("  Part A PreallocTexIdPool Live RTX: acq=%d refill=%d rel=%d maxDiff=%d err=0x%X -> %s\n",
               acqOk ? 1 : 0, refillOk ? 1 : 0, releasedOk ? 1 : 0, maxDiff, errA, partAOk ? "PASS" : "FAIL");
    }

    // -------------------------------------------------------------------------
    // Part B: Live RTX 4060 Vectorized TryBatchGpuDrivenInternal Parity
    //         (odd vertex counts 5/7/13, stride=16, trackMaxWy minShadowHeight=1.5f,
    //          hasCamRight=true & false, CPU 16-slot vs GPU-Driven SSBO+Bindless)
    // -------------------------------------------------------------------------
    bool partBOk = false;
    {
        static constexpr const char* kAnimVs =
            "uniform mat4 MatrixP; uniform mat4 MatrixV; uniform mat4 MatrixW;\n"
            "uniform vec3 FLOAT_PARAMS; uniform vec4 TIMEPARAMS;\n"
            "attribute vec4 POS2D_UV; varying vec3 PS_TEXCOORD; varying vec3 PS_POS;\n"
            "void main() {\n"
            "#if defined(SKINNED)\n"
            "\tvec4 world_pos = vec4(0.0); vec3 TEXCOORD0 = vec3(0.0);\n"
            "#else\n"
            "\tvec3 POSITION = vec3(POS2D_UV.xy, 0.0);\n"
            "\tfloat samplerIndex = floor(POS2D_UV.z/2.0);\n"
            "\tvec3 TEXCOORD0 = vec3(POS2D_UV.z - 2.0*samplerIndex, POS2D_UV.w, samplerIndex);\n"
            "\tvec3 object_pos = POSITION.xyz;\n"
            "\tvec4 world_pos = MatrixW * vec4( object_pos, 1.0 );\n"
            "\tmat4 mtxPV = MatrixP * MatrixV;\n"
            "\tgl_Position = mtxPV * world_pos;\n"
            "#endif\n"
            "\tPS_TEXCOORD = TEXCOORD0; PS_POS = world_pos.xyz;\n"
            "}\n";
        static constexpr const char* kAnimPs =
            "#if defined( GL_ES )\nprecision mediump float;\n#endif\n"
            "uniform sampler2D SAMPLER[6]; uniform vec4 LIGHTMAP_WORLD_EXTENTS;\n"
            "uniform mat4 COLOUR_XFORM; uniform vec3 PARAMS; uniform vec3 FLOAT_PARAMS;\n"
            "uniform vec4 OCEAN_BLEND_PARAMS;\n"
            "varying vec3 PS_POS; varying vec3 PS_TEXCOORD;\n"
            "#define LIGHT_OVERRIDE PARAMS.y\n#define BLOOM_TOGGLE PARAMS.z\n"
            "vec3 CalculateLightingContribution() { return vec3(1.0); }\n"
            "void main() {\n"
            "\tvec4 colour;\n"
            "#if defined( TRIPLE_ATLAS )\n"
            "\tcolour.rgba = texture2D( SAMPLER[0], PS_TEXCOORD.xy );\n"
            "#else\n"
            "\tcolour.rgba = texture2D( SAMPLER[0], PS_TEXCOORD.xy );\n"
            "#endif\n"
            "\tif (BLOOM_TOGGLE == 1.0) { gl_FragColor = vec4(0.0, 0.0, 0.0, colour.a); return; }\n"
            "\tif(FLOAT_PARAMS.y > 0.0) { if(PS_POS.y < FLOAT_PARAMS.x) { discard; } }\n"
            "\tgl_FragColor.rgba = colour.rgba * COLOUR_XFORM;\n"
            "\tvec3 light = CalculateLightingContribution();\n"
            "\tgl_FragColor.rgb *= max( light.rgb, vec3( LIGHT_OVERRIDE, LIGHT_OVERRIDE, LIGHT_OVERRIDE ) );\n"
            "}\n";

        std::string gVs, gFs;
        const char* tag = nullptr;
        const bool txOk = hs_sprite_batch::TransformAnimBatchShaderSource(kAnimVs, gVs, tag, true, true) &&
                          hs_sprite_batch::TransformAnimBatchShaderSource(kAnimPs, gFs, tag, true, true);
        uint32_t vs = pCreateSh(0x8B31u), fs = pCreateSh(0x8B30u);
        const char* vp = gVs.c_str(); int32_t vl = static_cast<int32_t>(gVs.size());
        const char* fp = gFs.c_str(); int32_t fl = static_cast<int32_t>(gFs.size());
        pShSrc(vs, 1, &vp, &vl); pCompSh(vs);
        pShSrc(fs, 1, &fp, &fl); pCompSh(fs);
        uint32_t prog = pCreatePr();
        pAttachSh(prog, vs); pAttachSh(prog, fs); pBindAtt(prog, 0u, "POS2D_UV"); pLinkPr(prog);
        pDelSh(vs); pDelSh(fs);
        int32_t linkOk = 0; pGetPrIv(prog, 0x8B82u, &linkOk);

        hs_sprite_batch::GlBatchOps bops{};
        bops.glGenBuffers               = reinterpret_cast<decltype(bops.glGenBuffers)>(findP("glGenBuffers"));
        bops.glDeleteBuffers            = reinterpret_cast<decltype(bops.glDeleteBuffers)>(findP("glDeleteBuffers"));
        bops.glBindBuffer               = reinterpret_cast<decltype(bops.glBindBuffer)>(findP("glBindBuffer"));
        bops.glBufferData               = reinterpret_cast<decltype(bops.glBufferData)>(findP("glBufferData"));
        bops.glBufferSubData            = reinterpret_cast<decltype(bops.glBufferSubData)>(opt.glBufferSubData);
        bops.glMapBufferRange           = reinterpret_cast<decltype(bops.glMapBufferRange)>(opt.glMapBufferRange);
        bops.glUnmapBuffer              = reinterpret_cast<decltype(bops.glUnmapBuffer)>(opt.glUnmapBuffer);
        bops.glBufferStorage            = reinterpret_cast<decltype(bops.glBufferStorage)>(opt.glBufferStorage ? opt.glBufferStorage : opt.glBufferStorageExt);
        bops.glFenceSync                = reinterpret_cast<decltype(bops.glFenceSync)>(opt.glFenceSync);
        bops.glClientWaitSync           = reinterpret_cast<decltype(bops.glClientWaitSync)>(opt.glClientWaitSync);
        bops.glDeleteSync               = reinterpret_cast<decltype(bops.glDeleteSync)>(opt.glDeleteSync);
        bops.glActiveTexture            = pActTex;
        bops.glBindTexture              = pBindTex;
        bops.glUniform1f                = reinterpret_cast<decltype(bops.glUniform1f)>(findP("glUniform1f"));
        bops.glUniform1i                = pUni1i;
        bops.glUniform3fv               = pUni3fv;
        bops.glUniform4fv               = pUni4fv;
        bops.glUniformMatrix4fv         = pUniMat4;
        bops.glVertexAttribPointer      = pAttPtr;
        bops.glEnableVertexAttribArray  = pEnAtt;
        bops.glDisableVertexAttribArray = reinterpret_cast<decltype(bops.glDisableVertexAttribArray)>(findP("glDisableVertexAttribArray"));
        bops.glDrawElements             = reinterpret_cast<decltype(bops.glDrawElements)>(findP("glDrawElements"));
        bops.glDrawElementsBaseVertex   = reinterpret_cast<decltype(bops.glDrawElementsBaseVertex)>(opt.glDrawElementsBaseVertex);
        bops.glBindBufferBase           = reinterpret_cast<decltype(bops.glBindBufferBase)>(opt.glBindBufferBase);
        bops.glGetIntegerv              = pGetInt;
        bops.glGetBufferParameteriv     = reinterpret_cast<decltype(bops.glGetBufferParameteriv)>(opt.glGetBufferParameteriv);
        bops.glGetTextureHandleARB      = reinterpret_cast<decltype(bops.glGetTextureHandleARB)>(opt.glGetTextureHandleARB);
        bops.glMakeTextureHandleResidentARB    = reinterpret_cast<decltype(bops.glMakeTextureHandleResidentARB)>(opt.glMakeTextureHandleResidentARB);
        bops.glMakeTextureHandleNonResidentARB = reinterpret_cast<decltype(bops.glMakeTextureHandleNonResidentARB)>(opt.glMakeTextureHandleNonResidentARB);

        hs_world_shadow::GlShadowOps wops{};
        wops.glGenFramebuffers          = pGenFbo;
        wops.glDeleteFramebuffers       = pDelFbo;
        wops.glBindFramebuffer          = pBindFbo;
        wops.glFramebufferTexture2D     = pFboTex;
        wops.glCheckFramebufferStatus   = reinterpret_cast<decltype(wops.glCheckFramebufferStatus)>(findP("glCheckFramebufferStatus"));
        wops.glGenRenderbuffers         = reinterpret_cast<decltype(wops.glGenRenderbuffers)>(findP("glGenRenderbuffers"));
        wops.glDeleteRenderbuffers      = reinterpret_cast<decltype(wops.glDeleteRenderbuffers)>(findP("glDeleteRenderbuffers"));
        wops.glBindRenderbuffer         = reinterpret_cast<decltype(wops.glBindRenderbuffer)>(findP("glBindRenderbuffer"));
        wops.glRenderbufferStorage      = reinterpret_cast<decltype(wops.glRenderbufferStorage)>(findP("glRenderbufferStorage"));
        wops.glFramebufferRenderbuffer  = reinterpret_cast<decltype(wops.glFramebufferRenderbuffer)>(findP("glFramebufferRenderbuffer"));
        wops.glGenTextures              = pGenTex;
        wops.glDeleteTextures           = pDelTex;
        wops.glActiveTexture            = pActTex;
        wops.glBindTexture              = pBindTex;
        wops.glTexImage2D               = pTexImg;
        wops.glTexParameteri            = pTexPar;
        wops.glCreateShader             = pCreateSh;
        wops.glShaderSource             = pShSrc;
        wops.glCompileShader            = pCompSh;
        wops.glGetShaderiv              = pGetShIv;
        wops.glGetShaderInfoLog         = reinterpret_cast<decltype(wops.glGetShaderInfoLog)>(findP("glGetShaderInfoLog"));
        wops.glDeleteShader             = pDelSh;
        wops.glCreateProgram            = pCreatePr;
        wops.glAttachShader             = pAttachSh;
        wops.glBindAttribLocation       = pBindAtt;
        wops.glLinkProgram              = pLinkPr;
        wops.glGetProgramiv             = pGetPrIv;
        wops.glGetProgramInfoLog        = reinterpret_cast<decltype(wops.glGetProgramInfoLog)>(findP("glGetProgramInfoLog"));
        wops.glDeleteProgram            = pDelPr;
        wops.glUseProgram               = pUsePr;
        wops.glGetUniformLocation       = pGetUni;
        wops.glUniform1f                = bops.glUniform1f;
        wops.glUniform1i                = pUni1i;
        wops.glUniform4fv               = pUni4fv;
        wops.glUniformMatrix4fv         = pUniMat4;
        wops.glGenBuffers               = bops.glGenBuffers;
        wops.glDeleteBuffers            = bops.glDeleteBuffers;
        wops.glBindBuffer               = bops.glBindBuffer;
        wops.glBufferData               = bops.glBufferData;
        wops.glEnableVertexAttribArray  = pEnAtt;
        wops.glDisableVertexAttribArray = bops.glDisableVertexAttribArray;
        wops.glVertexAttribPointer      = pAttPtr;
        wops.glDrawArrays               = pDrawArr;
        wops.glDrawElements             = bops.glDrawElements;
        wops.glDrawElementsBaseVertex   = bops.glDrawElementsBaseVertex;
        wops.glBindBufferBase           = bops.glBindBufferBase;
        wops.glViewport                 = pVp;
        wops.glClearColor               = pClrCol;
        wops.glClear                    = pClr;
        wops.glEnable                   = reinterpret_cast<decltype(wops.glEnable)>(findP("glEnable"));
        wops.glDisable                  = reinterpret_cast<decltype(wops.glDisable)>(findP("glDisable"));
        wops.glBlendFunc                = reinterpret_cast<decltype(wops.glBlendFunc)>(findP("glBlendFunc"));
        wops.glBlendFuncSeparate        = reinterpret_cast<decltype(wops.glBlendFuncSeparate)>(findP("glBlendFuncSeparate"));
        wops.glBlendEquation            = reinterpret_cast<decltype(wops.glBlendEquation)>(findP("glBlendEquation"));
        wops.glBlendEquationSeparate    = reinterpret_cast<decltype(wops.glBlendEquationSeparate)>(findP("glBlendEquationSeparate"));
        wops.glDepthFunc                = reinterpret_cast<decltype(wops.glDepthFunc)>(findP("glDepthFunc"));
        wops.glDepthMask                = reinterpret_cast<decltype(wops.glDepthMask)>(findP("glDepthMask"));
        wops.glColorMask                = reinterpret_cast<decltype(wops.glColorMask)>(findP("glColorMask"));
        wops.glGetIntegerv              = pGetInt;
        wops.glGetError                 = pGetErr;
        wops.logFn                      = &TestLog;

        uint32_t rtFbo = 0u, rtTex = 0u, spTex = 0u;
        pGenFbo(1, &rtFbo); pGenTex(1, &rtTex); pGenTex(1, &spTex);
        pActTex(0x84C0u); pBindTex(0x0DE1u, rtTex);
        pTexPar(0x0DE1u, 0x2801u, 0x2601); pTexPar(0x0DE1u, 0x2800u, 0x2601);
        pTexImg(0x0DE1u, 0, 0x8058, 256, 256, 0, 0x1908u, 0x1401u, nullptr);
        pBindFbo(0x8D40u, rtFbo); pFboTex(0x8D40u, 0x8CE0u, 0x0DE1u, rtTex, 0);

        uint8_t white4[4 * 4 * 4]; std::memset(white4, 255, sizeof(white4));
        pBindTex(0x0DE1u, spTex);
        pTexPar(0x0DE1u, 0x2801u, 0x2600); pTexPar(0x0DE1u, 0x2800u, 0x2600);
        pTexImg(0x0DE1u, 0, 0x8058, 4, 4, 0, 0x1908u, 0x1401u, white4);

        hs_sprite_batch::BatchProgInfo binfo{};
        binfo.checked = true; binfo.eligible = true; binfo.hasGpuDriven = true;
        binfo.locBatchActive = pGetUni(prog, "HS_BATCH_ACTIVE");
        binfo.locMatrixW     = pGetUni(prog, "MatrixW");
        binfo.locColourXform = pGetUni(prog, "COLOUR_XFORM");
        binfo.locFloatParams = pGetUni(prog, "FLOAT_PARAMS");
        binfo.locOceanBlend  = pGetUni(prog, "OCEAN_BLEND_PARAMS");
        binfo.locSampler0    = pGetUni(prog, "SAMPLER[0]");
        binfo.locSampler1    = pGetUni(prog, "SAMPLER[1]");
        binfo.locPos2dUv     = pGetAtt(prog, "POS2D_UV");
        binfo.locBatchWpos   = pGetAtt(prog, "HS_BATCH_WPOS");
        binfo.locBatchCMult  = pGetAtt(prog, "HS_BATCH_CMULT");
        binfo.locBatchCAdd   = pGetAtt(prog, "HS_BATCH_CADD");
        binfo.progMaxTexSlots = 16u;
        for (uint32_t s = 0; s < hs_sprite_batch::kMaxBatchTexSlots; ++s) {
            char nm[32]{}; snprintf(nm, sizeof(nm), "HS_BATCH_TEX%u", s);
            binfo.locBatchTex[s] = pGetUni(prog, nm);
        }

        const float idMtx[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        const float pv[16] = { 1.0f/20.0f,0,0,0, 0,0,0,0, 0,1.0f/20.0f,0,0, 0,0,0,1 };
        const int32_t sampUnits[6] = { 0,1,2,3,4,5 };
        const float paramsV[3] = { 0.0f, 0.0f, 0.0f };
        pUsePr(prog);
        if (pUni1iv && pGetUni(prog, "SAMPLER") >= 0) pUni1iv(pGetUni(prog, "SAMPLER"), 6, sampUnits);
        if (pUni3fv && pGetUni(prog, "PARAMS") >= 0) pUni3fv(pGetUni(prog, "PARAMS"), 1, paramsV);
        pUniMat4(pGetUni(prog, "MatrixP"), 1, 0, idMtx);
        pUniMat4(pGetUni(prog, "MatrixV"), 1, 0, idMtx);

        // Odd-vertex fan mesh builder (5, 7, 13 verts -> tests SIMD 4x loop + remainder 1/3!)
        auto buildFan = [](uint32_t nv, float halfW, float hTop, std::vector<float>& outV, std::vector<uint16_t>& outI) {
            outV.clear(); outI.clear();
            // v0 = center, v1..v(nv-1) = perimeter covering [-halfW..+halfW] x [0..hTop]
            outV.insert(outV.end(), { 0.0f, hTop * 0.5f, 0.5f, 0.5f });
            const uint32_t rim = nv - 1u;
            for (uint32_t i = 0u; i < rim; ++i) {
                const float ang = static_cast<float>(i) * (6.2831853f / static_cast<float>(rim));
                const float vx = std::cos(ang) * halfW;
                const float vy = (0.5f + 0.5f * std::sin(ang)) * hTop;
                outV.insert(outV.end(), { vx, vy, 0.5f + 0.4f * std::cos(ang), 0.5f + 0.4f * std::sin(ang) });
            }
            for (uint32_t i = 0u; i < rim; ++i) {
                outI.push_back(0u);
                outI.push_back(static_cast<uint16_t>(1u + i));
                outI.push_back(static_cast<uint16_t>(1u + ((i + 1u) % rim)));
            }
        };

        auto runOddVertShadow = [&](bool useGpuDriven, bool hasCamRight, std::vector<uint8_t>& outShadowRgba) -> bool {
            hs_world_shadow::WorldShadowEngine ws;
            ws.SetOps(wops, true);
            hs_gl_cache::ThreadState glCache;
            glCache.caps[0] = 1; glCache.caps[1] = 0; glCache.caps[2] = 0; glCache.caps[3] = 0; glCache.caps[4] = 0;
            glCache.blendSrc = hs_world_shadow::kGlSrcAlpha;
            glCache.blendDst = hs_world_shadow::kGlOneMinusSrcAlpha;
            glCache.blendEq  = hs_world_shadow::kGlFuncAdd;
            glCache.depthFunc = hs_world_shadow::kGlLess;
            glCache.colorMask = 15; glCache.depthMask = 0;

            hs_sprite_batch::SpriteBatcher sb;
            sb.SetOps(bops);
            sb.SetPersistentRingPreferred(true);
            sb.SetGpuDrivenPreferred(useGpuDriven);
            sb.SetElasticRingBudget(262144u, 524288u, 32768u);
            sb.SetDeferHostRestore(true);
            sb.SetShadowActive(true);
            sb.SetShadowHints(true, hasCamRight, 1.0f, 0.0f, 1.5f); // trackMaxWy active! (minShadowHeight=1.5)
            sb.SetEnabled(true);
            sb.OnUseProgram(prog, &binfo);
            bops.glEnableVertexAttribArray(static_cast<uint32_t>(binfo.locPos2dUv));
            sb.OnEnableDisableVertexAttribArray(static_cast<uint32_t>(binfo.locPos2dUv), true);
            sb.OnActiveTexture(hs_sprite_batch::kGlTexture0);
            sb.OnBindBuffer(hs_sprite_batch::kGlArrayBuffer, 0u);
            sb.OnBindBuffer(hs_sprite_batch::kGlElementArrayBuffer, 0u);

            hs_world_shadow::ShadowConfig cfg{};
            cfg.dirWx = 0.0f; cfg.dirWz = 1.0f; cfg.stretch = 2.0f; cfg.projOn = 1.0f;
            cfg.minHeight = 1.5f; cfg.density = 0.65f; cfg.soft = 0.60f;
            cfg.tintR = 0.10f; cfg.tintG = 0.15f; cfg.tintB = 0.40f;
            cfg.hdMode = true; cfg.skipNightmare = true;
            cfg.hasLuaCam = true; cfg.luaCamRightX = 1.0f; cfg.luaCamRightZ = 0.0f;
            const int32_t vp[4] = { 0, 0, 256, 256 };
            hs_world_shadow::ShadowPassState saved{};
            saved.dstFbo = rtFbo; saved.dstProg = prog;
            memcpy(saved.vp, vp, sizeof(vp));
            saved.glCacheTrusted = true; saved.blendCacheTrusted = true;

            pBindFbo(0x8D40u, rtFbo); pVp(0, 0, 256, 256);
            ws.OnGroundOrOceanDraw(rtFbo, vp); ws.OnMatrixPV(pv);
            if (!ws.OnBeforeFirstStandingEntity(cfg, saved, glCache)) return false;

            const uint32_t oddCounts[3] = { 5u, 7u, 13u };
            uint32_t totalIdx = 0u;
            for (int k = 0; k < 3; ++k) {
                std::vector<float> vf; std::vector<uint16_t> idf;
                // Tall mesh (h=4.0 >= 1.5 -> casts shadow at x=-10,-2,+6)
                buildFan(oddCounts[k], 1.4f, 4.0f, vf, idf);
                const float mwTall[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, -10.0f + k * 8.0f, 0.0f, -4.0f, 1.0f };
                sb.OnVertexAttribPointer(static_cast<uint32_t>(binfo.locPos2dUv), 4, hs_sprite_batch::kGlFloat, 0, 16, vf.data());
                sb.OnBindTexture(hs_sprite_batch::kGlTexture2D, spTex);
                sb.TryAbsorbMatrixW(binfo.locMatrixW, 1, 0, mwTall);
                sb.TryAbsorbColourXform(binfo.locColourXform, 1, 0, idMtx);
                if (!sb.TryBatchDrawElements(hs_sprite_batch::kGlTriangles, static_cast<int32_t>(idf.size()), hs_sprite_batch::kGlUnsignedShort, idf.data())) return false;
                totalIdx += static_cast<uint32_t>(idf.size());

                // Short mesh (h=0.5 < 1.5 -> MUST be filtered out by vectorized trackMaxWy!)
                buildFan(oddCounts[k], 1.4f, 0.5f, vf, idf);
                const float mwShort[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, -10.0f + k * 8.0f, 0.0f, 10.0f, 1.0f };
                sb.OnVertexAttribPointer(static_cast<uint32_t>(binfo.locPos2dUv), 4, hs_sprite_batch::kGlFloat, 0, 16, vf.data());
                sb.TryAbsorbMatrixW(binfo.locMatrixW, 1, 0, mwShort);
                sb.TryAbsorbColourXform(binfo.locColourXform, 1, 0, idMtx);
                if (!sb.TryBatchDrawElements(hs_sprite_batch::kGlTriangles, static_cast<int32_t>(idf.size()), hs_sprite_batch::kGlUnsignedShort, idf.data())) return false;
                totalIdx += static_cast<uint32_t>(idf.size());
            }
            sb.Flush(hs_sprite_batch::FlushReason::End);
            sb.EndStickyBatchSession();
            const bool recOk = useGpuDriven
                ? ws.RecordBatchedShadowGpu(sb.LastFlushVboId(), sb.LastFlushIboId(), sb.LastFlushSsboId(),
                                            totalIdx, sb.LastFlushBaseVertex(), sb.LastFlushIndexByteOffset(),
                                            sb.LastFlushShadowBoundsXZ(), rtFbo)
                : ws.RecordBatchedShadow(sb.LastFlushVboId(), sb.LastFlushIboId(), totalIdx,
                                         sb.LastFlushVertByteOffset(), sb.LastFlushIndexByteOffset(),
                                         sb.LastFlushTexCount(), sb.LastFlushTexIds(),
                                         sb.LastFlushShadowBoundsXZ(), rtFbo);
            if (!recOk) return false;
            ws.FlushDeferredBatchedShadows(cfg, saved, glCache);
            sb.OnEndFrame(); ws.OnEndFrame();

            pBindFbo(0x8D40u, rtFbo); pVp(0, 0, 256, 256);
            wops.glColorMask(1, 1, 1, 1); pClrCol(0.4f, 0.7f, 0.3f, 1.0f); pClr(0x4000u);
            ws.OnGroundOrOceanDraw(rtFbo, vp); ws.OnMatrixPV(pv);
            if (!ws.OnBeforeFirstStandingEntity(cfg, saved, glCache)) return false;
            ws.OnEndFrame(); pFinish();

            outShadowRgba.resize(256u * 256u * 4u);
            pBindFbo(0x8D40u, rtFbo);
            pReadPix(0, 0, 256, 256, 0x1908u, 0x1401u, outShadowRgba.data());
            sb.ReleaseGpuResources(); ws.ResetContext(true);
            return true;
        };

        std::vector<uint8_t> imgCpuT, imgGpuT, imgCpuF, imgGpuF;
        const bool ok1 = txOk && linkOk &&
                         runOddVertShadow(false, true,  imgCpuT) &&
                         runOddVertShadow(true,  true,  imgGpuT) &&
                         runOddVertShadow(false, false, imgCpuF) &&
                         runOddVertShadow(true,  false, imgGpuF);
        int maxDiffT = 0, maxDiffF = 0, tallShadPx = 0, shortShadPx = 0;
        if (ok1) {
            for (int y = 0; y < 256; ++y) {
                for (int x = 0; x < 256; ++x) {
                    const size_t i = (static_cast<size_t>(y) * 256u + static_cast<size_t>(x)) * 4u;
                    for (int c = 0; c < 4; ++c) {
                        maxDiffT = (std::max)(maxDiffT, abs(static_cast<int>(imgCpuT[i + c]) - static_cast<int>(imgGpuT[i + c])));
                        maxDiffF = (std::max)(maxDiffF, abs(static_cast<int>(imgCpuF[i + c]) - static_cast<int>(imgGpuF[i + c])));
                    }
                    // z=-4 -> y ~ 102..165 (tall); z=+10 -> y ~ 192..230 (short MUST be 0 shadowed!)
                    if (y >= 90 && y <= 170 && imgGpuT[i + 1] < 168u) ++tallShadPx;
                    if (y >= 195 && y <= 245 && imgGpuT[i + 1] < 168u) ++shortShadPx;
                }
            }
        }
        pBindFbo(0x8D40u, 0u);
        pDelFbo(1, &rtFbo); pDelTex(1, &rtTex); pDelTex(1, &spTex); pDelPr(prog);
        const uint32_t errB = pGetErr();
        partBOk = ok1 && (maxDiffT == 0) && (maxDiffF == 0) && (tallShadPx > 100) && (shortShadPx == 0) && (errB == 0u);
        printf("  Part B Vectorized GpuDriven (5/7/13v + trackMaxWy + camR T/F): diffT=%d diffF=%d tallPx=%d shortPx=%d err=0x%X -> %s\n",
               maxDiffT, maxDiffF, tallShadPx, shortShadPx, errB, partBOk ? "PASS" : "FAIL");
    }

    // -------------------------------------------------------------------------
    // Part C: Live RTX 4060 Zero-Stall Counters:
    //   1) WorldShadowEngine blendCacheTrusted=true (0 glGetIntegerv) + DirShadowCullBox (0 draws on far batch)
    //   2) GpuClarityEngine KnownGlState (0 glGetIntegerv / 0 glIsEnabled / 0 queryObj) +
    //      Grade 2 & Grade 3 RGB10_A2 odd-resolution (641x361) bit-exact parity vs uncached path!
    // -------------------------------------------------------------------------
    bool partCOk = false;
    {
        struct LiveCounters {
            GetInt_t realGetInt = nullptr;
            IsEn_t   realIsEn   = nullptr;
            hs_gpu_clarity::GlComputeOps::GetQueryObjectiv_t realGetQiv = nullptr;
            uint32_t getIntCnt = 0u;
            uint32_t isEnCnt   = 0u;
            uint32_t qObjCnt   = 0u;
        };
        static LiveCounters* s_lc = nullptr;
        LiveCounters lc{};
        lc.realGetInt = pGetInt;
        lc.realIsEn   = pIsEn;
        lc.realGetQiv = reinterpret_cast<hs_gpu_clarity::GlComputeOps::GetQueryObjectiv_t>(opt.glGetQueryObjectiv);
        s_lc = &lc;

        auto countGetInt = [](uint32_t p, int32_t* o) WINAPI { ++s_lc->getIntCnt; s_lc->realGetInt(p, o); };
        auto countIsEn   = [](uint32_t c) WINAPI -> uint8_t { ++s_lc->isEnCnt; return s_lc->realIsEn ? s_lc->realIsEn(c) : 0u; };
        auto countGetQiv = [](uint32_t q, uint32_t p, int32_t* o) WINAPI { ++s_lc->qObjCnt; if (s_lc->realGetQiv) s_lc->realGetQiv(q, p, o); };

        // C1: WorldShadowEngine blendCacheTrusted + DirShadowCullBox
        hs_world_shadow::GlShadowOps wops{};
        wops.glGenFramebuffers          = pGenFbo;
        wops.glDeleteFramebuffers       = pDelFbo;
        wops.glBindFramebuffer          = pBindFbo;
        wops.glFramebufferTexture2D     = pFboTex;
        wops.glCheckFramebufferStatus   = reinterpret_cast<decltype(wops.glCheckFramebufferStatus)>(findP("glCheckFramebufferStatus"));
        wops.glGenRenderbuffers         = reinterpret_cast<decltype(wops.glGenRenderbuffers)>(findP("glGenRenderbuffers"));
        wops.glDeleteRenderbuffers      = reinterpret_cast<decltype(wops.glDeleteRenderbuffers)>(findP("glDeleteRenderbuffers"));
        wops.glBindRenderbuffer         = reinterpret_cast<decltype(wops.glBindRenderbuffer)>(findP("glBindRenderbuffer"));
        wops.glRenderbufferStorage      = reinterpret_cast<decltype(wops.glRenderbufferStorage)>(findP("glRenderbufferStorage"));
        wops.glFramebufferRenderbuffer  = reinterpret_cast<decltype(wops.glFramebufferRenderbuffer)>(findP("glFramebufferRenderbuffer"));
        wops.glGenTextures              = pGenTex;
        wops.glDeleteTextures           = pDelTex;
        wops.glActiveTexture            = pActTex;
        wops.glBindTexture              = pBindTex;
        wops.glTexImage2D               = pTexImg;
        wops.glTexParameteri            = pTexPar;
        wops.glCreateShader             = pCreateSh;
        wops.glShaderSource             = pShSrc;
        wops.glCompileShader            = pCompSh;
        wops.glGetShaderiv              = pGetShIv;
        wops.glGetShaderInfoLog         = reinterpret_cast<decltype(wops.glGetShaderInfoLog)>(findP("glGetShaderInfoLog"));
        wops.glDeleteShader             = pDelSh;
        wops.glCreateProgram            = pCreatePr;
        wops.glAttachShader             = pAttachSh;
        wops.glBindAttribLocation       = pBindAtt;
        wops.glLinkProgram              = pLinkPr;
        wops.glGetProgramiv             = pGetPrIv;
        wops.glGetProgramInfoLog        = reinterpret_cast<decltype(wops.glGetProgramInfoLog)>(findP("glGetProgramInfoLog"));
        wops.glDeleteProgram            = pDelPr;
        wops.glUseProgram               = pUsePr;
        wops.glGetUniformLocation       = pGetUni;
        wops.glUniform1f                = reinterpret_cast<decltype(wops.glUniform1f)>(findP("glUniform1f"));
        wops.glUniform1i                = pUni1i;
        wops.glUniform4fv               = pUni4fv;
        wops.glUniformMatrix4fv         = pUniMat4;
        wops.glGenBuffers               = reinterpret_cast<decltype(wops.glGenBuffers)>(findP("glGenBuffers"));
        wops.glDeleteBuffers            = reinterpret_cast<decltype(wops.glDeleteBuffers)>(findP("glDeleteBuffers"));
        wops.glBindBuffer               = reinterpret_cast<decltype(wops.glBindBuffer)>(findP("glBindBuffer"));
        wops.glBufferData               = reinterpret_cast<decltype(wops.glBufferData)>(findP("glBufferData"));
        wops.glEnableVertexAttribArray  = pEnAtt;
        wops.glDisableVertexAttribArray = reinterpret_cast<decltype(wops.glDisableVertexAttribArray)>(findP("glDisableVertexAttribArray"));
        wops.glVertexAttribPointer      = pAttPtr;
        wops.glDrawArrays               = pDrawArr;
        wops.glDrawElements             = reinterpret_cast<decltype(wops.glDrawElements)>(findP("glDrawElements"));
        wops.glDrawElementsBaseVertex   = reinterpret_cast<decltype(wops.glDrawElementsBaseVertex)>(opt.glDrawElementsBaseVertex);
        wops.glBindBufferBase           = reinterpret_cast<decltype(wops.glBindBufferBase)>(opt.glBindBufferBase);
        wops.glViewport                 = pVp;
        wops.glClearColor               = pClrCol;
        wops.glClear                    = pClr;
        wops.glEnable                   = reinterpret_cast<decltype(wops.glEnable)>(findP("glEnable"));
        wops.glDisable                  = reinterpret_cast<decltype(wops.glDisable)>(findP("glDisable"));
        wops.glBlendFunc                = reinterpret_cast<decltype(wops.glBlendFunc)>(findP("glBlendFunc"));
        wops.glBlendFuncSeparate        = reinterpret_cast<decltype(wops.glBlendFuncSeparate)>(findP("glBlendFuncSeparate"));
        wops.glBlendEquation            = reinterpret_cast<decltype(wops.glBlendEquation)>(findP("glBlendEquation"));
        wops.glBlendEquationSeparate    = reinterpret_cast<decltype(wops.glBlendEquationSeparate)>(findP("glBlendEquationSeparate"));
        wops.glDepthFunc                = reinterpret_cast<decltype(wops.glDepthFunc)>(findP("glDepthFunc"));
        wops.glDepthMask                = reinterpret_cast<decltype(wops.glDepthMask)>(findP("glDepthMask"));
        wops.glColorMask                = reinterpret_cast<decltype(wops.glColorMask)>(findP("glColorMask"));
        wops.glGetIntegerv              = countGetInt;
        wops.glGetError                 = pGetErr;
        wops.logFn                      = &TestLog;

        uint32_t wsFbo = 0u, wsTex = 0u, dummyVbo = 0u, dummyIbo = 0u;
        pGenFbo(1, &wsFbo); pGenTex(1, &wsTex);
        wops.glGenBuffers(1, &dummyVbo); wops.glGenBuffers(1, &dummyIbo);
        pActTex(0x84C0u); pBindTex(0x0DE1u, wsTex);
        pTexImg(0x0DE1u, 0, 0x8058, 128, 128, 0, 0x1908u, 0x1401u, nullptr);
        pBindFbo(0x8D40u, wsFbo); pFboTex(0x8D40u, 0x8CE0u, 0x0DE1u, wsTex, 0);

        hs_world_shadow::WorldShadowEngine ws;
        ws.SetOps(wops, true);
        hs_gl_cache::ThreadState glc;
        glc.curProgram = 0u;
        glc.caps[0] = 1; glc.caps[1] = 0; glc.caps[2] = 0; glc.caps[3] = 0; glc.caps[4] = 0;
        glc.blendSrc = hs_world_shadow::kGlSrcAlpha; glc.blendDst = hs_world_shadow::kGlOneMinusSrcAlpha;
        glc.blendEq = hs_world_shadow::kGlFuncAdd; glc.depthFunc = hs_world_shadow::kGlLess;
        glc.colorMask = 15; glc.depthMask = 0;

        const float pv[16] = { 1.0f/20.0f,0,0,0, 0,0,0,0, 0,1.0f/20.0f,0,0, 0,0,0,1 };
        const int32_t vp128[4] = { 0, 0, 128, 128 };
        hs_world_shadow::ShadowConfig scfg{};
        scfg.dirWx = 0.0f; scfg.dirWz = 1.0f; scfg.stretch = 1.5f; scfg.projOn = 1.0f;
        scfg.density = 0.6f; scfg.soft = 0.8f; scfg.hdMode = true; scfg.hasLuaCam = true;
        hs_world_shadow::ShadowPassState saved{};
        saved.dstFbo = wsFbo; saved.dstProg = 0u; memcpy(saved.vp, vp128, sizeof(vp128));
        saved.glCacheTrusted = true; saved.blendCacheTrusted = true;

        // Warmup frame 1 initializes shadow FBOs/programs
        ws.OnGroundOrOceanDraw(wsFbo, vp128); ws.OnMatrixPV(pv);
        ws.OnBeforeFirstStandingEntity(scfg, saved, glc);
        ws.OnEndFrame();

        // Steady-state frame 2 with blendCacheTrusted=true + far out-of-box batch -> 0 glGetIntegerv & 0 shadow draws!
        lc.getIntCnt = 0u;
        ws.OnGroundOrOceanDraw(wsFbo, vp128); ws.OnMatrixPV(pv);
        const bool wsBeginOk = ws.OnBeforeFirstStandingEntity(scfg, saved, glc);
        const float farBounds[4] = { 480.0f, 520.0f, 480.0f, 520.0f };
        const uint32_t tArr[1] = { wsTex };
        ws.RecordBatchedShadow(dummyVbo, dummyIbo, 6u, 0u, 0u, 1u, tArr, farBounds, wsFbo);
        ws.FlushDeferredBatchedShadows(scfg, saved, glc);
        ws.OnEndFrame();
        const uint32_t wsGetIntCnt = lc.getIntCnt;
        const uint32_t wsFarDraws  = ws.LastFrameShadowDraws();
        ws.ResetContext(true);
        wops.glDeleteBuffers(1, &dummyVbo); wops.glDeleteBuffers(1, &dummyIbo);
        pBindFbo(0x8D40u, 0u); pDelFbo(1, &wsFbo); pDelTex(1, &wsTex);

        // C2: GpuClarityEngine KnownGlState zero-stall + Grade 2 & Grade 3 RGB10_A2 odd resolution (641x361) parity!
        hs_gpu_clarity::GlComputeOps cops{};
        cops.glGenFramebuffers        = pGenFbo;
        cops.glDeleteFramebuffers     = pDelFbo;
        cops.glBindFramebuffer        = pBindFbo;
        cops.glFramebufferTexture2D   = pFboTex;
        cops.glCheckFramebufferStatus = reinterpret_cast<decltype(cops.glCheckFramebufferStatus)>(findP("glCheckFramebufferStatus"));
        cops.glBlitFramebuffer        = reinterpret_cast<decltype(cops.glBlitFramebuffer)>(opt.glBlitFramebuffer);
        cops.glGenTextures            = pGenTex;
        cops.glDeleteTextures         = pDelTex;
        cops.glActiveTexture          = pActTex;
        cops.glBindTexture            = pBindTex;
        cops.glTexStorage2D           = reinterpret_cast<decltype(cops.glTexStorage2D)>(opt.glTexStorage2D);
        cops.glTexParameteri          = pTexPar;
        cops.glCreateShader           = pCreateSh;
        cops.glShaderSource           = pShSrc;
        cops.glCompileShader          = pCompSh;
        cops.glGetShaderiv            = pGetShIv;
        cops.glGetShaderInfoLog       = reinterpret_cast<decltype(cops.glGetShaderInfoLog)>(findP("glGetShaderInfoLog"));
        cops.glDeleteShader           = pDelSh;
        cops.glCreateProgram          = pCreatePr;
        cops.glAttachShader           = pAttachSh;
        cops.glLinkProgram            = pLinkPr;
        cops.glGetProgramiv           = pGetPrIv;
        cops.glGetProgramInfoLog      = reinterpret_cast<decltype(cops.glGetProgramInfoLog)>(findP("glGetProgramInfoLog"));
        cops.glDeleteProgram          = pDelPr;
        cops.glUseProgram             = pUsePr;
        cops.glGetUniformLocation     = pGetUni;
        cops.glUniform4f              = reinterpret_cast<decltype(cops.glUniform4f)>(opt.glUniform4f);
        cops.glDispatchCompute        = reinterpret_cast<decltype(cops.glDispatchCompute)>(opt.glDispatchCompute);
        cops.glMemoryBarrier          = reinterpret_cast<decltype(cops.glMemoryBarrier)>(opt.glMemoryBarrier);
        cops.glBindImageTexture       = reinterpret_cast<decltype(cops.glBindImageTexture)>(opt.glBindImageTexture);
        cops.glGetIntegerv            = countGetInt;
        cops.glIsEnabled              = countIsEn;
        cops.glEnable                 = reinterpret_cast<decltype(cops.glEnable)>(findP("glEnable"));
        cops.glDisable                = reinterpret_cast<decltype(cops.glDisable)>(findP("glDisable"));
        cops.glGenQueries             = reinterpret_cast<decltype(cops.glGenQueries)>(opt.glGenQueries);
        cops.glDeleteQueries          = reinterpret_cast<decltype(cops.glDeleteQueries)>(opt.glDeleteQueries);
        cops.glBeginQuery             = reinterpret_cast<decltype(cops.glBeginQuery)>(opt.glBeginQuery);
        cops.glEndQuery               = reinterpret_cast<decltype(cops.glEndQuery)>(opt.glEndQuery);
        cops.glGetQueryObjectiv       = countGetQiv;
        cops.glGetQueryObjectui64v    = reinterpret_cast<decltype(cops.glGetQueryObjectui64v)>(opt.glGetQueryObjectui64v);
        cops.logFn                    = &TestLog;

        const int32_t oddW = 641, oddH = 361;
        std::vector<uint8_t> srcImg(static_cast<size_t>(oddW) * static_cast<size_t>(oddH) * 4u);
        for (int32_t y = 0; y < oddH; ++y) {
            for (int32_t x = 0; x < oddW; ++x) {
                const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(oddW) + static_cast<size_t>(x)) * 4u;
                const bool ink = (std::abs(y - (x / 2 + 40)) < 2);
                srcImg[i + 0] = ink ? 24u : static_cast<uint8_t>(140 + (x * 90) / oddW);
                srcImg[i + 1] = ink ? 20u : static_cast<uint8_t>(130 + (y * 90) / oddH);
                srcImg[i + 2] = ink ? 22u : 115u;
                srcImg[i + 3] = 255u;
            }
        }
        uint32_t clFbo = 0u, clTex = 0u;
        pGenFbo(1, &clFbo); pGenTex(1, &clTex);
        pActTex(0x84C0u); pBindTex(0x0DE1u, clTex);
        pTexPar(0x0DE1u, 0x2801u, 0x2601); pTexPar(0x0DE1u, 0x2800u, 0x2601);
        pTexImg(0x0DE1u, 0, 0x8058, oddW, oddH, 0, 0x1908u, 0x1401u, srcImg.data());
        pBindFbo(0x8D40u, clFbo); pFboTex(0x8D40u, 0x8CE0u, 0x0DE1u, clTex, 0);

        hs_gpu_clarity::GpuClarityEngine cl;
        cl.SetOps(cops, true);
        int g2Diff = 255, g3Diff = 255;
        uint32_t clGetIntSteady = 999u, clIsEnSteady = 999u, clQObjSteady = 999u;
        bool scPreserved = false;
        auto runClarity = [&](int grade, bool useKnown, std::vector<uint8_t>& outPx, bool measureCounters) -> bool {
            pActTex(0x84C0u); pBindTex(0x0DE1u, clTex);
            pTexImg(0x0DE1u, 0, 0x8058, oddW, oddH, 0, 0x1908u, 0x1401u, srcImg.data());
            pBindFbo(0x8D40u, clFbo);
            pUsePr(0u);
            cops.glEnable(hs_gpu_clarity::kGlScissorTest);
            hs_gpu_clarity::KnownGlState ks{};
            ks.drawFbo = static_cast<int32_t>(clFbo);
            ks.readFbo = static_cast<int32_t>(clFbo);
            ks.curProgram = 0;
            ks.activeTexEnum = static_cast<int32_t>(hs_gpu_clarity::kGlTexture0);
            ks.boundTex0 = static_cast<int32_t>(clTex);
            ks.scissorEnabled = 1;
            ks.sampleTimer = false;
            if (measureCounters) { lc.getIntCnt = 0u; lc.isEnCnt = 0u; lc.qObjCnt = 0u; }
            const bool r = cl.ProcessFrame(clFbo, oddW, oddH, { grade, 1.0f }, useKnown ? &ks : nullptr);
            if (measureCounters) {
                clGetIntSteady = lc.getIntCnt;
                clIsEnSteady   = lc.isEnCnt;
                clQObjSteady   = lc.qObjCnt;
                scPreserved    = (pIsEn && pIsEn(hs_gpu_clarity::kGlScissorTest) != 0u);
            }
            cops.glDisable(hs_gpu_clarity::kGlScissorTest);
            pFinish();
            outPx.resize(srcImg.size());
            pBindFbo(0x8CA8u /*GL_READ_FRAMEBUFFER*/, clFbo);
            pReadPix(0, 0, oddW, oddH, 0x1908u, 0x1401u, outPx.data());
            return r;
        };

        std::vector<uint8_t> g2Ref, g2Fast, g3Ref, g3Fast;
        const bool clOk = runClarity(2, false, g2Ref,  false) &&
                          runClarity(2, true,  g2Fast, false) &&
                          runClarity(3, false, g3Ref,  false) &&
                          runClarity(3, true,  g3Fast, true);
        if (clOk) {
            g2Diff = 0; g3Diff = 0;
            for (size_t i = 0; i < g2Ref.size(); ++i) {
                g2Diff = (std::max)(g2Diff, abs(static_cast<int>(g2Ref[i]) - static_cast<int>(g2Fast[i])));
                g3Diff = (std::max)(g3Diff, abs(static_cast<int>(g3Ref[i]) - static_cast<int>(g3Fast[i])));
            }
        }
        cl.OnContextReset(true);
        pBindFbo(0x8D40u, 0u); pDelFbo(1, &clFbo); pDelTex(1, &clTex);
        s_lc = nullptr;
        const uint32_t errC = pGetErr();
        partCOk = wsBeginOk && (wsGetIntCnt == 0u) && (wsFarDraws == 0u) &&
                  clOk && (g2Diff == 0) && (g3Diff == 0) &&
                  (clGetIntSteady == 0u) && (clIsEnSteady == 0u) && (clQObjSteady == 0u) &&
                  scPreserved && (errC == 0u);
        printf("  Part C Zero-Stall Live RTX: wsGetInt=%u farDraws=%u | cl641x361(getInt=%u isEn=%u qObj=%u g2Diff=%d g3Diff=%d scPres=%d) err=0x%X -> %s\n",
               wsGetIntCnt, wsFarDraws, clGetIntSteady, clIsEnSteady, clQObjSteady,
               g2Diff, g3Diff, scPreserved ? 1 : 0, errC, partCOk ? "PASS" : "FAIL");
    }

    rt.pWglMakeCurrent(nullptr, nullptr);
    rt.pWglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return partAOk && partBOk && partCOk;
}

static bool TestPhase3LiveGpuSuite() {
    printf("\n=== Live RTX GPU Phase 3 Verification: 6-Slot Zero-Wait PBO + PARAMS SSBO + Shadow Reuse/Coalescing + Adaptive Clarity ===\n");
    hs_native_gl::WglRuntime rt{};
    const auto probe = hs_native_gl::ProbeNativeEs2Support(hs_native_gl::BackendMode::Auto, rt);
    if (!probe.supported || !probe.hasBufferStorage || !probe.hasSsbo ||
        !probe.hasBindlessTexture || !probe.hasComputeShader || !probe.hasImageLoadStore) {
        printf("  [SKIP] GL 4.3+ / Bindless / Compute unavailable -> PASS\n");
        return true;
    }
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"HS_Phase3Live", WS_POPUP, 0, 0, 64, 64,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return false;
    HDC hdc = nullptr;
    HGLRC rc = nullptr;
    if (!hs_native_gl::CreateGameEs2Context(rt, hwnd, hdc, rc) || !rt.pWglMakeCurrent(hdc, rc)) {
        if (hdc) ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return false;
    }
    void* procs[76]{};
    hs_native_gl::OptionalGlProcs opt{};
    if (hs_native_gl::ResolveLiveGlesTable(rt, procs, opt) != 76) {
        rt.pWglMakeCurrent(nullptr, nullptr);
        rt.pWglDeleteContext(rc);
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return false;
    }
    auto findP = [&](const char* nm) -> void* {
        for (size_t i = 0; i < 76; ++i)
            if (std::strcmp(hs_native_gl::kDstGles2Imports[i].name, nm) == 0) return procs[i];
        return nullptr;
    };

    using GenTex_t    = void (WINAPI*)(int32_t, uint32_t*);
    using DelTex_t    = void (WINAPI*)(int32_t, const uint32_t*);
    using BindTex_t   = void (WINAPI*)(uint32_t, uint32_t);
    using TexPar_t    = void (WINAPI*)(uint32_t, uint32_t, int32_t);
    using TexImg_t    = void (WINAPI*)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*);
    using CompImg_t   = void (WINAPI*)(uint32_t, int32_t, uint32_t, int32_t, int32_t, int32_t, int32_t, const void*);
    using GenFbo_t    = void (WINAPI*)(int32_t, uint32_t*);
    using DelFbo_t    = void (WINAPI*)(int32_t, const uint32_t*);
    using BindFbo_t   = void (WINAPI*)(uint32_t, uint32_t);
    using FboTex_t    = void (WINAPI*)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t);
    using CreateSh_t  = uint32_t (WINAPI*)(uint32_t);
    using ShSrc_t     = void (WINAPI*)(uint32_t, int32_t, const char* const*, const int32_t*);
    using CompSh_t    = void (WINAPI*)(uint32_t);
    using GetShIv_t   = void (WINAPI*)(uint32_t, uint32_t, int32_t*);
    using DelSh_t     = void (WINAPI*)(uint32_t);
    using CreatePr_t  = uint32_t (WINAPI*)();
    using AttachSh_t  = void (WINAPI*)(uint32_t, uint32_t);
    using BindAtt_t   = void (WINAPI*)(uint32_t, uint32_t, const char*);
    using LinkPr_t    = void (WINAPI*)(uint32_t);
    using GetPrIv_t   = void (WINAPI*)(uint32_t, uint32_t, int32_t*);
    using DelPr_t     = void (WINAPI*)(uint32_t);
    using UsePr_t     = void (WINAPI*)(uint32_t);
    using GetUni_t    = int32_t (WINAPI*)(uint32_t, const char*);
    using GetAtt_t    = int32_t (WINAPI*)(uint32_t, const char*);
    using Uni1f_t     = void (WINAPI*)(int32_t, float);
    using Uni1i_t     = void (WINAPI*)(int32_t, int32_t);
    using Uni1iv_t    = void (WINAPI*)(int32_t, int32_t, const int32_t*);
    using Uni3fv_t    = void (WINAPI*)(int32_t, int32_t, const float*);
    using Uni4fv_t    = void (WINAPI*)(int32_t, int32_t, const float*);
    using UniMat4_t   = void (WINAPI*)(int32_t, int32_t, uint8_t, const float*);
    using ActTex_t    = void (WINAPI*)(uint32_t);
    using Vp_t        = void (WINAPI*)(int32_t, int32_t, int32_t, int32_t);
    using ClrCol_t    = void (WINAPI*)(float, float, float, float);
    using Clr_t       = void (WINAPI*)(uint32_t);
    using EnAtt_t     = void (WINAPI*)(uint32_t);
    using DisAtt_t    = void (WINAPI*)(uint32_t);
    using AttPtr_t    = void (WINAPI*)(uint32_t, int32_t, uint32_t, uint8_t, int32_t, const void*);
    using DrawArr_t   = void (WINAPI*)(uint32_t, int32_t, int32_t);
    using DrawEl_t    = void (WINAPI*)(uint32_t, int32_t, uint32_t, const void*);
    using ReadPix_t   = void (WINAPI*)(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void*);
    using GetErr_t    = uint32_t (WINAPI*)();
    using GetInt_t    = void (WINAPI*)(uint32_t, int32_t*);
    using Finish_t    = void (WINAPI*)();

    auto pGenTex   = reinterpret_cast<GenTex_t>(findP("glGenTextures"));
    auto pDelTex   = reinterpret_cast<DelTex_t>(findP("glDeleteTextures"));
    auto pBindTex  = reinterpret_cast<BindTex_t>(findP("glBindTexture"));
    auto pTexPar   = reinterpret_cast<TexPar_t>(findP("glTexParameteri"));
    auto pTexImg   = reinterpret_cast<TexImg_t>(findP("glTexImage2D"));
    auto pCompImg  = reinterpret_cast<CompImg_t>(findP("glCompressedTexImage2D"));
    auto pGenFbo   = reinterpret_cast<GenFbo_t>(findP("glGenFramebuffers"));
    auto pDelFbo   = reinterpret_cast<DelFbo_t>(findP("glDeleteFramebuffers"));
    auto pBindFbo  = reinterpret_cast<BindFbo_t>(findP("glBindFramebuffer"));
    auto pFboTex   = reinterpret_cast<FboTex_t>(findP("glFramebufferTexture2D"));
    auto pCreateSh = reinterpret_cast<CreateSh_t>(findP("glCreateShader"));
    auto pShSrc    = reinterpret_cast<ShSrc_t>(findP("glShaderSource"));
    auto pCompSh   = reinterpret_cast<CompSh_t>(findP("glCompileShader"));
    auto pGetShIv  = reinterpret_cast<GetShIv_t>(findP("glGetShaderiv"));
    auto pDelSh    = reinterpret_cast<DelSh_t>(findP("glDeleteShader"));
    auto pCreatePr = reinterpret_cast<CreatePr_t>(findP("glCreateProgram"));
    auto pAttachSh = reinterpret_cast<AttachSh_t>(findP("glAttachShader"));
    auto pBindAtt  = reinterpret_cast<BindAtt_t>(findP("glBindAttribLocation"));
    auto pLinkPr   = reinterpret_cast<LinkPr_t>(findP("glLinkProgram"));
    auto pGetPrIv  = reinterpret_cast<GetPrIv_t>(findP("glGetProgramiv"));
    auto pDelPr    = reinterpret_cast<DelPr_t>(findP("glDeleteProgram"));
    auto pUsePr    = reinterpret_cast<UsePr_t>(findP("glUseProgram"));
    auto pGetUni   = reinterpret_cast<GetUni_t>(findP("glGetUniformLocation"));
    auto pGetAtt   = reinterpret_cast<GetAtt_t>(findP("glGetAttribLocation"));
    auto pUni1f    = reinterpret_cast<Uni1f_t>(findP("glUniform1f"));
    auto pUni1i    = reinterpret_cast<Uni1i_t>(findP("glUniform1i"));
    auto pUni1iv   = reinterpret_cast<Uni1iv_t>(findP("glUniform1iv"));
    auto pUni3fv   = reinterpret_cast<Uni3fv_t>(findP("glUniform3fv"));
    auto pUni4fv   = reinterpret_cast<Uni4fv_t>(findP("glUniform4fv"));
    auto pUniMat4  = reinterpret_cast<UniMat4_t>(findP("glUniformMatrix4fv"));
    auto pActTex   = reinterpret_cast<ActTex_t>(findP("glActiveTexture"));
    auto pVp       = reinterpret_cast<Vp_t>(findP("glViewport"));
    auto pClrCol   = reinterpret_cast<ClrCol_t>(findP("glClearColor"));
    auto pClr      = reinterpret_cast<Clr_t>(findP("glClear"));
    auto pEnAtt    = reinterpret_cast<EnAtt_t>(findP("glEnableVertexAttribArray"));
    auto pDisAtt   = reinterpret_cast<DisAtt_t>(findP("glDisableVertexAttribArray"));
    auto pAttPtr   = reinterpret_cast<AttPtr_t>(findP("glVertexAttribPointer"));
    auto pDrawArr  = reinterpret_cast<DrawArr_t>(findP("glDrawArrays"));
    auto pDrawEl   = reinterpret_cast<DrawEl_t>(findP("glDrawElements"));
    auto pReadPix  = reinterpret_cast<ReadPix_t>(findP("glReadPixels"));
    auto pGetErr   = reinterpret_cast<GetErr_t>(findP("glGetError"));
    auto pGetInt   = reinterpret_cast<GetInt_t>(findP("glGetIntegerv"));
    auto pFinish   = reinterpret_cast<Finish_t>(findP("glFinish"));
    auto pMultiDrawBv = reinterpret_cast<hs_world_shadow::GlShadowOps::MultiDrawElementsBaseVertex_t>(
        rt.ResolveGlProc("glMultiDrawElementsBaseVertex"));

    while (pGetErr() != 0u) {}

    // -------------------------------------------------------------------------
    // Part A: Live RTX 4060 6-Slot Zero-Wait PBO Ring (8-frame wrap, flags=0,
    //         timeout=0 enforced, pixel-exact RGBA8 + DXT5 readback, 0 leaks)
    // -------------------------------------------------------------------------
    bool partAOk = false;
    {
        struct PboWaitSpy {
            hs_pbo_tex::GlPboOps::ClientWaitSync_t realWait = nullptr;
            uint32_t maxFlags = 0u;
            uint64_t maxTimeout = 0ULL;
            uint32_t waitCalls = 0u;
        };
        static PboWaitSpy* s_spy = nullptr;
        PboWaitSpy spy{};
        spy.realWait = reinterpret_cast<hs_pbo_tex::GlPboOps::ClientWaitSync_t>(opt.glClientWaitSync);
        s_spy = &spy;

        hs_pbo_tex::GlPboOps pops{};
        pops.glGenBuffers           = reinterpret_cast<decltype(pops.glGenBuffers)>(findP("glGenBuffers"));
        pops.glDeleteBuffers        = reinterpret_cast<decltype(pops.glDeleteBuffers)>(findP("glDeleteBuffers"));
        pops.glBindBuffer           = reinterpret_cast<decltype(pops.glBindBuffer)>(findP("glBindBuffer"));
        pops.glBufferStorage        = reinterpret_cast<decltype(pops.glBufferStorage)>(opt.glBufferStorage ? opt.glBufferStorage : opt.glBufferStorageExt);
        pops.glMapBufferRange       = reinterpret_cast<decltype(pops.glMapBufferRange)>(opt.glMapBufferRange);
        pops.glUnmapBuffer          = reinterpret_cast<decltype(pops.glUnmapBuffer)>(opt.glUnmapBuffer);
        pops.glGetBufferParameteriv = reinterpret_cast<decltype(pops.glGetBufferParameteriv)>(opt.glGetBufferParameteriv);
        pops.glFenceSync            = reinterpret_cast<decltype(pops.glFenceSync)>(opt.glFenceSync);
        pops.glClientWaitSync       = [](void* sync, uint32_t flags, uint64_t timeout) WINAPI -> uint32_t {
            ++s_spy->waitCalls;
            if (flags > s_spy->maxFlags) s_spy->maxFlags = flags;
            if (timeout > s_spy->maxTimeout) s_spy->maxTimeout = timeout;
            return s_spy->realWait(sync, flags, timeout);
        };
        pops.glDeleteSync           = reinterpret_cast<decltype(pops.glDeleteSync)>(opt.glDeleteSync);

        hs_pbo_tex::PersistentPboTexRing pbo;
        pbo.SetOps(pops);
        pbo.SetSlotBytes(hs_pbo_tex::kMinSlotBytes); // 6 x 4MB = 24MB
        pbo.SetEnabled(true);

        static constexpr const char* kVs =
            "#version 100\nattribute vec2 aPos; varying vec2 vUv;\n"
            "void main() { vUv = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.0, 1.0); }\n";
        static constexpr const char* kFs =
            "#version 100\nprecision highp float; uniform sampler2D uTex; varying vec2 vUv;\n"
            "void main() { gl_FragColor = texture2D(uTex, vUv); }\n";
        uint32_t vs = pCreateSh(0x8B31u), fs = pCreateSh(0x8B30u);
        pShSrc(vs, 1, &kVs, nullptr); pCompSh(vs);
        pShSrc(fs, 1, &kFs, nullptr); pCompSh(fs);
        uint32_t pr = pCreatePr();
        pAttachSh(pr, vs); pAttachSh(pr, fs); pBindAtt(pr, 0u, "aPos"); pLinkPr(pr);
        pDelSh(vs); pDelSh(fs);

        uint32_t fbo = 0u, fboTex = 0u, tPbo = 0u, tDir = 0u;
        pGenFbo(1, &fbo); pGenTex(1, &fboTex); pGenTex(1, &tPbo); pGenTex(1, &tDir);
        auto prep = [&](uint32_t t) {
            pActTex(0x84C0u); pBindTex(0x0DE1u, t);
            pTexPar(0x0DE1u, 0x2801u, 0x2600); pTexPar(0x0DE1u, 0x2800u, 0x2600);
            pTexPar(0x0DE1u, 0x2802u, 0x812F); pTexPar(0x0DE1u, 0x2803u, 0x812F);
        };
        prep(fboTex);
        pTexImg(0x0DE1u, 0, 0x1908, 128, 128, 0, 0x1908u, 0x1401u, nullptr);
        pBindFbo(0x8D40u, fbo); pFboTex(0x8D40u, 0x8CE0u, 0x0DE1u, fboTex, 0);

        std::vector<uint8_t> rgba(128u * 128u * 4u);
        std::vector<uint8_t> dxt5(128u * 128u);
        for (size_t i = 0; i < dxt5.size(); ++i) dxt5[i] = static_cast<uint8_t>((i * 47u + 19u) & 0xFFu);
        bool allUp = true;
        for (int f = 0; f < 8; ++f) {
            for (size_t i = 0; i < rgba.size(); ++i) rgba[i] = static_cast<uint8_t>((i * 31u + static_cast<size_t>(f) * 73u + 11u) & 0xFFu);
            prep(tPbo);
            const bool okR = (f == 0)
                ? pbo.TryUploadCompressedTexImage2D(pCompImg, 0x0DE1u, 0, 0x83F3u, 128, 128, 0, static_cast<int32_t>(dxt5.size()), dxt5.data())
                : pbo.TryUploadTexImage2D(pTexImg, 0x0DE1u, 0, 0x1908, 128, 128, 0, 0x1908u, 0x1401u, rgba.data());
            if (!okR) {
                pFinish();
                allUp = ((f == 0)
                    ? pbo.TryUploadCompressedTexImage2D(pCompImg, 0x0DE1u, 0, 0x83F3u, 128, 128, 0, static_cast<int32_t>(dxt5.size()), dxt5.data())
                    : pbo.TryUploadTexImage2D(pTexImg, 0x0DE1u, 0, 0x1908, 128, 128, 0, 0x1908u, 0x1401u, rgba.data())) && allUp;
            }
            pbo.OnEndFrame();
        }
        prep(tDir);
        pTexImg(0x0DE1u, 0, 0x1908, 128, 128, 0, 0x1908u, 0x1401u, rgba.data());

        const float quad[8] = { -1,-1, 1,-1, -1,1, 1,1 };
        auto readTex = [&](uint32_t t, std::vector<uint8_t>& out) {
            pBindFbo(0x8D40u, fbo); pVp(0, 0, 128, 128); pUsePr(pr);
            pActTex(0x84C0u); pBindTex(0x0DE1u, t); pUni1i(pGetUni(pr, "uTex"), 0);
            pops.glBindBuffer(0x8892u, 0u);
            pEnAtt(0u); pAttPtr(0u, 2, 0x1406u, 0u, 0, quad);
            pDrawArr(0x0005u, 0, 4); pFinish();
            out.resize(128u * 128u * 4u);
            pReadPix(0, 0, 128, 128, 0x1908u, 0x1401u, out.data());
        };
        std::vector<uint8_t> pxPbo, pxDir;
        readTex(tPbo, pxPbo); readTex(tDir, pxDir);
        int maxDiff = 0;
        for (size_t i = 0; i < pxPbo.size(); ++i) {
            maxDiff = (std::max)(maxDiff, abs(static_cast<int>(pxPbo[i]) - static_cast<int>(pxDir[i])));
        }
        int32_t unpackBind = -1;
        pGetInt(0x88EFu, &unpackBind);
        const uint64_t skips = pbo.SlotSkips();
        const uint64_t upCnt = pbo.UploadCount();
        pbo.ReleaseGpu(true);
        pBindFbo(0x8D40u, 0u); pDelFbo(1, &fbo); pDelTex(1, &fboTex); pDelTex(1, &tPbo); pDelTex(1, &tDir); pDelPr(pr);
        const uint32_t errA = pGetErr();
        s_spy = nullptr;
        partAOk = allUp && (maxDiff == 0) && (unpackBind == 0) &&
                  (spy.maxFlags == 0u) && (spy.maxTimeout == 0ULL) &&
                  (upCnt == 8u) && (errA == 0u);
        printf("  Part A 6-Slot Zero-Wait PBO Live RTX: up=%llu sk=%llu waits=%u flags=%u toNs=%llu maxDiff=%d err=0x%X -> %s\n",
               static_cast<unsigned long long>(upCnt), static_cast<unsigned long long>(skips),
               spy.waitCalls, spy.maxFlags, static_cast<unsigned long long>(spy.maxTimeout),
               maxDiff, errA, partAOk ? "PASS" : "FAIL");
    }

    // -------------------------------------------------------------------------
    // Part B: Live RTX 4060 PARAMS SSBO Absorption (5 sprites with distinct
    //         LIGHT_OVERRIDE 0.0 / 0.502 / 1.0 + ALPHA_TEST + BLOOM_TOGGLE in
    //         1 SINGLE FLUSH [FlushReason::Uni3 == 0], maxDiff == 0 vs unbatched,
    //         plus PrepareForUnbatchedDraw() & CPU 16-slot fallback parity!)
    // -------------------------------------------------------------------------
    bool partBOk = false;
    {
        static constexpr const char* kAnimVs =
            "uniform mat4 MatrixP; uniform mat4 MatrixV; uniform mat4 MatrixW;\n"
            "uniform vec3 FLOAT_PARAMS; uniform vec4 TIMEPARAMS;\n"
            "attribute vec4 POS2D_UV; varying vec3 PS_TEXCOORD; varying vec3 PS_POS;\n"
            "void main() {\n#if defined(SKINNED)\n\tvec4 world_pos=vec4(0.0); vec3 TEXCOORD0=vec3(0.0);\n#else\n"
            "\tvec3 POSITION = vec3(POS2D_UV.xy, 0.0);\n"
            "\tfloat samplerIndex = floor(POS2D_UV.z/2.0);\n"
            "\tvec3 TEXCOORD0 = vec3(POS2D_UV.z - 2.0*samplerIndex, POS2D_UV.w, samplerIndex);\n"
            "\tvec3 object_pos = POSITION.xyz;\n"
            "\tvec4 world_pos = MatrixW * vec4( object_pos, 1.0 );\n"
            "\tmat4 mtxPV = MatrixP * MatrixV;\n"
            "\tgl_Position = mtxPV * world_pos;\n#endif\n"
            "\tPS_TEXCOORD = TEXCOORD0; PS_POS = world_pos.xyz;\n}\n";
        // Faithful anim.ksh fragment shader with non-1.0 lighting (0.30, 0.40, 0.50) + HD-2D LIGHT_OVERRIDE gate!
        static constexpr const char* kAnimPs =
            "#if defined( GL_ES )\nprecision mediump float;\n#endif\n"
            "uniform sampler2D SAMPLER[6]; uniform vec4 LIGHTMAP_WORLD_EXTENTS;\n"
            "uniform mat4 COLOUR_XFORM; uniform vec3 PARAMS; uniform vec3 FLOAT_PARAMS;\n"
            "uniform vec4 OCEAN_BLEND_PARAMS;\n"
            "varying vec3 PS_POS; varying vec3 PS_TEXCOORD;\n"
            "#define ALPHA_TEST PARAMS.x\n#define LIGHT_OVERRIDE PARAMS.y\n#define BLOOM_TOGGLE PARAMS.z\n"
            "vec3 CalculateLightingContribution() { return vec3(0.30, 0.40, 0.50); }\n"
            "void main() {\n\tvec4 colour;\n#if defined( TRIPLE_ATLAS )\n"
            "\tcolour.rgba = texture2D( SAMPLER[0], PS_TEXCOORD.xy );\n#else\n"
            "\tcolour.rgba = texture2D( SAMPLER[0], PS_TEXCOORD.xy );\n#endif\n"
            "\tif (BLOOM_TOGGLE == 1.0) { gl_FragColor.rgba = vec4(0.0, 0.0, 0.0, colour.a); return; }\n"
            "\tif(FLOAT_PARAMS.y > 0.0) { if(PS_POS.y < FLOAT_PARAMS.x) { discard; } }\n"
            "\tif (ALPHA_TEST > 0.0) { if (colour.a >= ALPHA_TEST) { gl_FragColor = colour.rgba; } else { discard; } } else {\n"
            "\t\tgl_FragColor.rgba = colour.rgba * COLOUR_XFORM;\n"
            "\t\tvec3 light = CalculateLightingContribution();\n"
            "\t\tgl_FragColor.rgb *= max( light.rgb, vec3( LIGHT_OVERRIDE, LIGHT_OVERRIDE, LIGHT_OVERRIDE ) );\n"
            "\t\tfloat hdGate = 1.0 - smoothstep(0.05, 0.35, LIGHT_OVERRIDE);\n"
            "\t\tgl_FragColor.r = clamp(gl_FragColor.r + 0.08 * hdGate, 0.0, 1.0);\n"
            "\t}\n}\n";

        std::string gVs, gFs;
        const char* tag = nullptr;
        const bool txOk = hs_sprite_batch::TransformAnimBatchShaderSource(kAnimVs, gVs, tag, true, true) &&
                          hs_sprite_batch::TransformAnimBatchShaderSource(kAnimPs, gFs, tag, true, true);
        uint32_t vs = pCreateSh(0x8B31u), fs = pCreateSh(0x8B30u);
        const char* vp = gVs.c_str(); int32_t vl = static_cast<int32_t>(gVs.size());
        const char* fp = gFs.c_str(); int32_t fl = static_cast<int32_t>(gFs.size());
        pShSrc(vs, 1, &vp, &vl); pCompSh(vs);
        pShSrc(fs, 1, &fp, &fl); pCompSh(fs);
        uint32_t prog = pCreatePr();
        pAttachSh(prog, vs); pAttachSh(prog, fs); pBindAtt(prog, 0u, "POS2D_UV"); pLinkPr(prog);
        pDelSh(vs); pDelSh(fs);
        int32_t linkOk = 0; pGetPrIv(prog, 0x8B82u, &linkOk);

        hs_sprite_batch::GlBatchOps bops{};
        bops.glGenBuffers               = reinterpret_cast<decltype(bops.glGenBuffers)>(findP("glGenBuffers"));
        bops.glDeleteBuffers            = reinterpret_cast<decltype(bops.glDeleteBuffers)>(findP("glDeleteBuffers"));
        bops.glBindBuffer               = reinterpret_cast<decltype(bops.glBindBuffer)>(findP("glBindBuffer"));
        bops.glBufferData               = reinterpret_cast<decltype(bops.glBufferData)>(findP("glBufferData"));
        bops.glBufferSubData            = reinterpret_cast<decltype(bops.glBufferSubData)>(opt.glBufferSubData);
        bops.glMapBufferRange           = reinterpret_cast<decltype(bops.glMapBufferRange)>(opt.glMapBufferRange);
        bops.glUnmapBuffer              = reinterpret_cast<decltype(bops.glUnmapBuffer)>(opt.glUnmapBuffer);
        bops.glBufferStorage            = reinterpret_cast<decltype(bops.glBufferStorage)>(opt.glBufferStorage ? opt.glBufferStorage : opt.glBufferStorageExt);
        bops.glFenceSync                = reinterpret_cast<decltype(bops.glFenceSync)>(opt.glFenceSync);
        bops.glClientWaitSync           = reinterpret_cast<decltype(bops.glClientWaitSync)>(opt.glClientWaitSync);
        bops.glDeleteSync               = reinterpret_cast<decltype(bops.glDeleteSync)>(opt.glDeleteSync);
        bops.glActiveTexture            = pActTex;
        bops.glBindTexture              = pBindTex;
        bops.glUniform1f                = pUni1f;
        bops.glUniform1i                = pUni1i;
        bops.glUniform3fv               = pUni3fv;
        bops.glUniform4fv               = pUni4fv;
        bops.glUniformMatrix4fv         = pUniMat4;
        bops.glVertexAttribPointer      = pAttPtr;
        bops.glEnableVertexAttribArray  = pEnAtt;
        bops.glDisableVertexAttribArray = pDisAtt;
        bops.glDrawElements             = pDrawEl;
        bops.glDrawElementsBaseVertex   = reinterpret_cast<decltype(bops.glDrawElementsBaseVertex)>(opt.glDrawElementsBaseVertex);
        bops.glBindBufferBase           = reinterpret_cast<decltype(bops.glBindBufferBase)>(opt.glBindBufferBase);
        bops.glGetIntegerv              = pGetInt;
        bops.glGetBufferParameteriv     = reinterpret_cast<decltype(bops.glGetBufferParameteriv)>(opt.glGetBufferParameteriv);
        bops.glGetTextureHandleARB      = reinterpret_cast<decltype(bops.glGetTextureHandleARB)>(opt.glGetTextureHandleARB);
        bops.glMakeTextureHandleResidentARB    = reinterpret_cast<decltype(bops.glMakeTextureHandleResidentARB)>(opt.glMakeTextureHandleResidentARB);
        bops.glMakeTextureHandleNonResidentARB = reinterpret_cast<decltype(bops.glMakeTextureHandleNonResidentARB)>(opt.glMakeTextureHandleNonResidentARB);

        uint32_t fbo = 0u, rtTex = 0u, spOpaque = 0u, spHalf = 0u, spCpuOnly = 0u;
        pGenFbo(1, &fbo); pGenTex(1, &rtTex); pGenTex(1, &spOpaque); pGenTex(1, &spHalf); pGenTex(1, &spCpuOnly);
        auto makeTex = [&](uint32_t t, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            uint8_t px[4 * 4 * 4];
            for (int i = 0; i < 16; ++i) { px[i*4+0] = r; px[i*4+1] = g; px[i*4+2] = b; px[i*4+3] = a; }
            pActTex(0x84C0u); pBindTex(0x0DE1u, t);
            pTexPar(0x0DE1u, 0x2801u, 0x2600); pTexPar(0x0DE1u, 0x2800u, 0x2600);
            pTexImg(0x0DE1u, 0, 0x8058, 4, 4, 0, 0x1908u, 0x1401u, px);
        };
        pActTex(0x84C0u); pBindTex(0x0DE1u, rtTex);
        pTexPar(0x0DE1u, 0x2801u, 0x2600); pTexPar(0x0DE1u, 0x2800u, 0x2600);
        pTexImg(0x0DE1u, 0, 0x8058, 256, 64, 0, 0x1908u, 0x1401u, nullptr);
        pBindFbo(0x8D40u, fbo); pFboTex(0x8D40u, 0x8CE0u, 0x0DE1u, rtTex, 0);
        makeTex(spOpaque,  200u, 180u, 160u, 255u);
        makeTex(spHalf,    220u, 140u, 100u, 102u); // alpha = 0.40 < 0.50 (discarded when ALPHA_TEST=0.50!)
        makeTex(spCpuOnly, 180u, 210u, 140u, 255u);

        hs_sprite_batch::BatchProgInfo binfo{};
        binfo.checked = true; binfo.eligible = true; binfo.hasGpuDriven = true;
        binfo.locBatchActive = pGetUni(prog, "HS_BATCH_ACTIVE");
        binfo.locMatrixW     = pGetUni(prog, "MatrixW");
        binfo.locColourXform = pGetUni(prog, "COLOUR_XFORM");
        binfo.locFloatParams = pGetUni(prog, "FLOAT_PARAMS");
        binfo.locParams      = pGetUni(prog, "PARAMS");
        binfo.locOceanBlend  = pGetUni(prog, "OCEAN_BLEND_PARAMS");
        binfo.locSampler0    = pGetUni(prog, "SAMPLER[0]");
        binfo.locSampler1    = pGetUni(prog, "SAMPLER[1]");
        binfo.locPos2dUv     = pGetAtt(prog, "POS2D_UV");
        binfo.locBatchWpos   = pGetAtt(prog, "HS_BATCH_WPOS");
        binfo.locBatchCMult  = pGetAtt(prog, "HS_BATCH_CMULT");
        binfo.locBatchCAdd   = pGetAtt(prog, "HS_BATCH_CADD");
        binfo.hasParamsAbsorption = binfo.hasGpuDriven && !binfo.isBloomPass && (binfo.locParams >= 0);
        binfo.progMaxTexSlots = 16u;
        for (uint32_t s = 0u; s < hs_sprite_batch::kMaxBatchTexSlots; ++s) {
            char nm[32]{}; snprintf(nm, sizeof(nm), "HS_BATCH_TEX%u", s);
            binfo.locBatchTex[s] = pGetUni(prog, nm);
        }

        const float idMtx[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        const int32_t sampUnits[6] = { 0,1,2,3,4,5 };
        const float qv[16] = { -0.14f,-0.6f,0.2f,0.2f, 0.14f,-0.6f,0.8f,0.2f, 0.14f,0.6f,0.8f,0.8f, -0.14f,0.6f,0.2f,0.8f };
        const uint16_t qi[6] = { 0,1,2, 0,2,3 };
        struct Case { float tx; uint32_t tex; float params[3]; };
        const Case cases[5] = {
            { -0.75f, spOpaque, { 0.0f, 0.0f,   0.0f } }, // normal lit + HD2D gate
            { -0.40f, spOpaque, { 0.0f, 0.502f, 0.0f } }, // self-lit 0.502
            { -0.05f, spOpaque, { 0.0f, 1.0f,   0.0f } }, // fullbright 1.0
            {  0.30f, spHalf,   { 0.5f, 0.0f,   0.0f } }, // ALPHA_TEST=0.5 -> discarded!
            {  0.65f, spOpaque, { 0.0f, 0.0f,   1.0f } }  // BLOOM_TOGGLE=1.0 -> black rgb, alpha 255!
        };
        const Case unbatchedCase{ 0.88f, spOpaque, { 0.0f, 0.75f, 0.0f } };
        const Case cpuFallbackCase{ -0.92f, spCpuOnly, { 0.0f, 0.62f, 0.0f } };

        // 1. Reference unbatched draw of all 7 sprites
        auto setupCommon = [&]() {
            pBindFbo(0x8D40u, fbo); pVp(0, 0, 256, 64);
            pClrCol(0.12f, 0.16f, 0.24f, 1.0f); pClr(0x4000u);
            pUsePr(prog);
            if (pUni1iv && pGetUni(prog, "SAMPLER") >= 0) pUni1iv(pGetUni(prog, "SAMPLER"), 6, sampUnits);
            pUniMat4(pGetUni(prog, "MatrixP"), 1, 0, idMtx);
            pUniMat4(pGetUni(prog, "MatrixV"), 1, 0, idMtx);
            pUniMat4(binfo.locColourXform, 1, 0, idMtx);
            const float z3[3] = { 0,0,0 }, z4[4] = { 0,0,0,0 };
            if (binfo.locFloatParams >= 0) pUni3fv(binfo.locFloatParams, 1, z3);
            if (binfo.locOceanBlend >= 0) pUni4fv(binfo.locOceanBlend, 1, z4);
            pUni1f(binfo.locBatchActive, 0.0f);
            bops.glBindBuffer(0x8892u, 0u); bops.glBindBuffer(0x8893u, 0u);
            pEnAtt(static_cast<uint32_t>(binfo.locPos2dUv));
            pAttPtr(static_cast<uint32_t>(binfo.locPos2dUv), 4, 0x1406u, 0, 16, qv);
        };
        auto drawRef = [&](const Case& c) {
            float mw[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, c.tx,0,0,1 };
            pActTex(0x84C0u); pBindTex(0x0DE1u, c.tex);
            pUniMat4(binfo.locMatrixW, 1, 0, mw);
            pUni3fv(binfo.locParams, 1, c.params);
            pDrawEl(0x0004u, 6, 0x1403u, qi);
        };

        std::vector<uint8_t> pxRef(256u * 64u * 4u), pxBat(256u * 64u * 4u);
        setupCommon();
        for (const auto& c : cases) drawRef(c);
        drawRef(unbatchedCase);
        drawRef(cpuFallbackCase);
        pFinish();
        pReadPix(0, 0, 256, 64, 0x1908u, 0x1401u, pxRef.data());

        // 2. Batched GPU-Driven SSBO PARAMS absorption (5 cases -> 1 single flush, Uni3 == 0!)
        //    + PrepareForUnbatchedDraw() commit (unbatchedCase) + CPU 16-slot fallback (cpuFallbackCase)
        setupCommon();
        hs_sprite_batch::SpriteBatcher sb;
        sb.SetOps(bops);
        sb.SetPersistentRingPreferred(true);
        sb.SetGpuDrivenPreferred(true);
        sb.SetEnabled(true);
        sb.OnUseProgram(prog, &binfo);
        sb.OnEnableDisableVertexAttribArray(static_cast<uint32_t>(binfo.locPos2dUv), true);
        sb.OnActiveTexture(0x84C0u);
        sb.OnBindBuffer(0x8892u, 0u);
        sb.OnBindBuffer(0x8893u, 0u);
        sb.OnVertexAttribPointer(static_cast<uint32_t>(binfo.locPos2dUv), 4, 0x1406u, 0, 16, qv);
        sb.OnTexImageMutated(spCpuOnly, true); // forces spCpuOnly onto CPU 16-slot fallback!

        // Draw 0 establishes GPU ring, then draws 1..4 vary PARAMS with TryAbsorbParams -> 1 flush total!
        int64_t flushesAfter5 = 0, uni3After5 = 0;
        bool absorbAllOk = true;
        for (int k = 0; k < 5; ++k) {
            float mw[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, cases[k].tx,0,0,1 };
            sb.OnBindTexture(0x0DE1u, cases[k].tex);
            if (k == 0) {
                pUni3fv(binfo.locParams, 1, cases[k].params);
                sb.OnUniform3fv(binfo.locParams, 1, cases[k].params, false);
            } else {
                if (!sb.TryAbsorbParams(binfo.locParams, 1, cases[k].params)) absorbAllOk = false;
            }
            sb.TryAbsorbMatrixW(binfo.locMatrixW, 1, 0, mw);
            sb.TryAbsorbColourXform(binfo.locColourXform, 1, 0, idMtx);
            if (!sb.TryBatchDrawElements(0x0004u, 6, 0x1403u, qi)) absorbAllOk = false;
        }
        sb.Flush(hs_sprite_batch::FlushReason::End);
        flushesAfter5 = sb.GetStats().flushes;
        uni3After5    = sb.GetStats().flReason[static_cast<size_t>(hs_sprite_batch::FlushReason::Uni3)];

        // Test PrepareForUnbatchedDraw() commits dirty PARAMS=(0, 0.75, 0) to live GL!
        {
            float mw[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, unbatchedCase.tx,0,0,1 };
            absorbAllOk = absorbAllOk && sb.TryAbsorbParams(binfo.locParams, 1, unbatchedCase.params);
            sb.TryAbsorbMatrixW(binfo.locMatrixW, 1, 0, mw);
            sb.TryAbsorbColourXform(binfo.locColourXform, 1, 0, idMtx);
            sb.PrepareForUnbatchedDraw(); // MUST commit dirty PARAMS & MatrixW & COLOUR_XFORM!
            pActTex(0x84C0u); pBindTex(0x0DE1u, unbatchedCase.tex);
            pDrawEl(0x0004u, 6, 0x1403u, qi);
        }

        // Test CPU 16-slot fallback commits dirty PARAMS=(0, 0.62, 0) before CPU batch!
        {
            float mw[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, cpuFallbackCase.tx,0,0,1 };
            absorbAllOk = absorbAllOk && sb.TryAbsorbParams(binfo.locParams, 1, cpuFallbackCase.params);
            sb.OnBindTexture(0x0DE1u, cpuFallbackCase.tex);
            sb.TryAbsorbMatrixW(binfo.locMatrixW, 1, 0, mw);
            sb.TryAbsorbColourXform(binfo.locColourXform, 1, 0, idMtx);
            absorbAllOk = absorbAllOk && sb.TryBatchDrawElements(0x0004u, 6, 0x1403u, qi);
            sb.Flush(hs_sprite_batch::FlushReason::End);
        }
        pFinish();
        pReadPix(0, 0, 256, 64, 0x1908u, 0x1401u, pxBat.data());
        sb.ReleaseGpuResources();

        int maxDiff = 0;
        for (size_t i = 0; i < pxRef.size(); ++i) {
            maxDiff = (std::max)(maxDiff, abs(static_cast<int>(pxRef[i]) - static_cast<int>(pxBat[i])));
        }
        pBindFbo(0x8D40u, 0u);
        pDelFbo(1, &fbo); pDelTex(1, &rtTex); pDelTex(1, &spOpaque); pDelTex(1, &spHalf); pDelTex(1, &spCpuOnly); pDelPr(prog);
        const uint32_t errB = pGetErr();
        partBOk = txOk && linkOk && absorbAllOk && (flushesAfter5 == 1) && (uni3After5 == 0) &&
                  (maxDiff == 0) && (errB == 0u);
        printf("  Part B PARAMS SSBO Live RTX (5->1 flush, unbatched + CPU fallback): flushes=%lld uni3=%lld maxDiff=%d err=0x%X -> %s\n",
               static_cast<long long>(flushesAfter5), static_cast<long long>(uni3After5),
               maxDiff, errB, partBOk ? "PASS" : "FAIL");
    }

    // -------------------------------------------------------------------------
    // Part C: Live RTX 4060 WorldShadowEngine Deferred Coalescing (6 -> 1 draw,
    //         maxDiff == 0) + Temporal Reuse (+2.0wu walk locked cachedBounds_,
    //         0 shadow swimming, teleport invalidates immediately!)
    // -------------------------------------------------------------------------
    bool partCOk = false;
    {
        hs_world_shadow::GlShadowOps wops{};
        wops.glGenFramebuffers             = pGenFbo;
        wops.glDeleteFramebuffers          = pDelFbo;
        wops.glBindFramebuffer             = pBindFbo;
        wops.glFramebufferTexture2D        = pFboTex;
        wops.glCheckFramebufferStatus      = reinterpret_cast<decltype(wops.glCheckFramebufferStatus)>(findP("glCheckFramebufferStatus"));
        wops.glGenRenderbuffers            = reinterpret_cast<decltype(wops.glGenRenderbuffers)>(findP("glGenRenderbuffers"));
        wops.glDeleteRenderbuffers         = reinterpret_cast<decltype(wops.glDeleteRenderbuffers)>(findP("glDeleteRenderbuffers"));
        wops.glBindRenderbuffer            = reinterpret_cast<decltype(wops.glBindRenderbuffer)>(findP("glBindRenderbuffer"));
        wops.glRenderbufferStorage         = reinterpret_cast<decltype(wops.glRenderbufferStorage)>(findP("glRenderbufferStorage"));
        wops.glFramebufferRenderbuffer     = reinterpret_cast<decltype(wops.glFramebufferRenderbuffer)>(findP("glFramebufferRenderbuffer"));
        wops.glGenTextures                 = pGenTex;
        wops.glDeleteTextures              = pDelTex;
        wops.glActiveTexture               = pActTex;
        wops.glBindTexture                 = pBindTex;
        wops.glTexImage2D                  = pTexImg;
        wops.glTexParameteri               = pTexPar;
        wops.glCreateShader                = pCreateSh;
        wops.glShaderSource                = pShSrc;
        wops.glCompileShader               = pCompSh;
        wops.glGetShaderiv                 = pGetShIv;
        wops.glGetShaderInfoLog            = reinterpret_cast<decltype(wops.glGetShaderInfoLog)>(findP("glGetShaderInfoLog"));
        wops.glDeleteShader                = pDelSh;
        wops.glCreateProgram               = pCreatePr;
        wops.glAttachShader                = pAttachSh;
        wops.glBindAttribLocation          = pBindAtt;
        wops.glLinkProgram                 = pLinkPr;
        wops.glGetProgramiv                = pGetPrIv;
        wops.glGetProgramInfoLog           = reinterpret_cast<decltype(wops.glGetProgramInfoLog)>(findP("glGetProgramInfoLog"));
        wops.glDeleteProgram               = pDelPr;
        wops.glUseProgram                  = pUsePr;
        wops.glGetUniformLocation          = pGetUni;
        wops.glUniform1f                   = pUni1f;
        wops.glUniform1i                   = pUni1i;
        wops.glUniform4fv                  = pUni4fv;
        wops.glUniformMatrix4fv            = pUniMat4;
        wops.glGenBuffers                  = reinterpret_cast<decltype(wops.glGenBuffers)>(findP("glGenBuffers"));
        wops.glDeleteBuffers               = reinterpret_cast<decltype(wops.glDeleteBuffers)>(findP("glDeleteBuffers"));
        wops.glBindBuffer                  = reinterpret_cast<decltype(wops.glBindBuffer)>(findP("glBindBuffer"));
        wops.glBufferData                  = reinterpret_cast<decltype(wops.glBufferData)>(findP("glBufferData"));
        wops.glEnableVertexAttribArray     = pEnAtt;
        wops.glDisableVertexAttribArray    = pDisAtt;
        wops.glVertexAttribPointer         = pAttPtr;
        wops.glDrawArrays                  = pDrawArr;
        wops.glDrawElements                = pDrawEl;
        wops.glDrawElementsBaseVertex      = reinterpret_cast<decltype(wops.glDrawElementsBaseVertex)>(opt.glDrawElementsBaseVertex);
        wops.glMultiDrawElementsBaseVertex = pMultiDrawBv;
        wops.glBindBufferBase              = reinterpret_cast<decltype(wops.glBindBufferBase)>(opt.glBindBufferBase);
        wops.glViewport                    = pVp;
        wops.glClearColor                  = pClrCol;
        wops.glClear                       = pClr;
        wops.glEnable                      = reinterpret_cast<decltype(wops.glEnable)>(findP("glEnable"));
        wops.glDisable                     = reinterpret_cast<decltype(wops.glDisable)>(findP("glDisable"));
        wops.glBlendFunc                   = reinterpret_cast<decltype(wops.glBlendFunc)>(findP("glBlendFunc"));
        wops.glBlendFuncSeparate           = reinterpret_cast<decltype(wops.glBlendFuncSeparate)>(findP("glBlendFuncSeparate"));
        wops.glBlendEquation               = reinterpret_cast<decltype(wops.glBlendEquation)>(findP("glBlendEquation"));
        wops.glBlendEquationSeparate       = reinterpret_cast<decltype(wops.glBlendEquationSeparate)>(findP("glBlendEquationSeparate"));
        wops.glDepthFunc                   = reinterpret_cast<decltype(wops.glDepthFunc)>(findP("glDepthFunc"));
        wops.glDepthMask                   = reinterpret_cast<decltype(wops.glDepthMask)>(findP("glDepthMask"));
        wops.glColorMask                   = reinterpret_cast<decltype(wops.glColorMask)>(findP("glColorMask"));
        wops.glGetIntegerv                 = pGetInt;
        wops.glGetError                    = pGetErr;
        wops.logFn                         = &TestLog;

        auto pGetTexH = reinterpret_cast<uint64_t (WINAPI*)(uint32_t)>(opt.glGetTextureHandleARB);
        auto pMakeRes = reinterpret_cast<void (WINAPI*)(uint64_t)>(opt.glMakeTextureHandleResidentARB);
        auto pMakeNon = reinterpret_cast<void (WINAPI*)(uint64_t)>(opt.glMakeTextureHandleNonResidentARB);

        uint32_t fbo = 0u, rtTex = 0u, spTex = 0u, vbo = 0u, ibo = 0u, ssbo = 0u;
        pGenFbo(1, &fbo); pGenTex(1, &rtTex); pGenTex(1, &spTex);
        wops.glGenBuffers(1, &vbo); wops.glGenBuffers(1, &ibo); wops.glGenBuffers(1, &ssbo);
        pActTex(0x84C0u); pBindTex(0x0DE1u, rtTex);
        pTexPar(0x0DE1u, 0x2801u, 0x2601); pTexPar(0x0DE1u, 0x2800u, 0x2601);
        pTexImg(0x0DE1u, 0, 0x8058, 256, 256, 0, 0x1908u, 0x1401u, nullptr);
        pBindFbo(0x8D40u, fbo); pFboTex(0x8D40u, 0x8CE0u, 0x0DE1u, rtTex, 0);

        uint8_t white4[4 * 4 * 4]; std::memset(white4, 255, sizeof(white4));
        pBindTex(0x0DE1u, spTex);
        pTexPar(0x0DE1u, 0x2801u, 0x2600); pTexPar(0x0DE1u, 0x2800u, 0x2600);
        pTexImg(0x0DE1u, 0, 0x8058, 4, 4, 0, 0x1908u, 0x1401u, white4);
        uint64_t hTex = pGetTexH(spTex);
        pMakeRes(hTex);

        // Build 6 standing quads in GPU-Driven VBO/IBO/SSBO (4 contiguous @ baseVertex=0 + 2 @ baseVertex=16, 20)
        std::vector<hs_sprite_batch::GpuBatchVertex> gv;
        std::vector<uint16_t> gi;
        std::vector<hs_sprite_batch::GpuSpriteInstance> gins(6);
        for (uint32_t k = 0u; k < 6u; ++k) {
            const float wx = -10.0f + static_cast<float>(k) * 4.0f;
            auto& ins = gins[k];
            ins.m0[0] = 1.0f; ins.m1[1] = 1.0f;
            ins.m2[0] = wx; ins.m2[1] = 0.0f; ins.m2[2] = -2.0f; ins.m2[3] = 0.0f;
            ins.cmult[0] = ins.cmult[1] = ins.cmult[2] = ins.cmult[3] = 1.0f;
            ins.cadd[3] = 1.0f; // casts shadow!
            ins.meta[0] = 1.0f; ins.meta[1] = 0.0f; ins.meta[2] = 0.0f;
            std::memcpy(ins.tex0, &hTex, 8u);
            std::memcpy(ins.tex1, &hTex, 8u);
            std::memcpy(ins.tex2, &hTex, 8u);

            const float idf = static_cast<float>(k);
            gv.push_back({ -1.0f, 0.0f, 0.2f, 0.2f, idf });
            gv.push_back({  1.0f, 0.0f, 0.8f, 0.2f, idf });
            gv.push_back({ -1.0f, 4.0f, 0.2f, 0.8f, idf });
            gv.push_back({  1.0f, 4.0f, 0.8f, 0.8f, idf });
            const uint16_t b = (k < 4u) ? static_cast<uint16_t>(k * 4u) : 0u;
            gi.push_back(b + 0); gi.push_back(b + 1); gi.push_back(b + 2);
            gi.push_back(b + 1); gi.push_back(b + 3); gi.push_back(b + 2);
        }
        wops.glBindBuffer(hs_world_shadow::kGlArrayBuffer, vbo);
        wops.glBufferData(hs_world_shadow::kGlArrayBuffer, gv.size() * sizeof(hs_sprite_batch::GpuBatchVertex), gv.data(), hs_world_shadow::kGlStaticDraw);
        wops.glBindBuffer(hs_world_shadow::kGlElementArrayBuffer, ibo);
        wops.glBufferData(hs_world_shadow::kGlElementArrayBuffer, gi.size() * sizeof(uint16_t), gi.data(), hs_world_shadow::kGlStaticDraw);
        wops.glBindBuffer(hs_world_shadow::kGlShaderStorageBuffer, ssbo);
        wops.glBufferData(hs_world_shadow::kGlShaderStorageBuffer, gins.size() * sizeof(hs_sprite_batch::GpuSpriteInstance), gins.data(), hs_world_shadow::kGlStaticDraw);

        const float pv[16] = { 1.0f/20.0f,0,0,0, 0,0,0,0, 0,1.0f/20.0f,0,0, 0,0,0,1 };
        const int32_t vp[4] = { 0, 0, 256, 256 };
        hs_world_shadow::ShadowConfig cfg{};
        cfg.dirWx = 0.0f; cfg.dirWz = 1.0f; cfg.stretch = 2.0f; cfg.projOn = 1.0f;
        cfg.density = 0.65f; cfg.soft = 0.80f; cfg.tintR = 0.1f; cfg.tintG = 0.15f; cfg.tintB = 0.4f;
        cfg.hdMode = true; cfg.hasLuaCam = true; cfg.luaCamX = 0.0f; cfg.luaCamZ = 0.0f;
        cfg.luaCamRightX = 1.0f; cfg.luaCamRightZ = 0.0f;
        hs_world_shadow::ShadowPassState saved{};
        saved.dstFbo = fbo; saved.dstProg = 0u; memcpy(saved.vp, vp, sizeof(vp));
        saved.glCacheTrusted = true; saved.blendCacheTrusted = true;
        hs_gl_cache::ThreadState glc;
        glc.caps[0] = 1; glc.caps[1] = 0; glc.caps[2] = 0; glc.caps[3] = 0; glc.caps[4] = 0;
        glc.blendSrc = hs_world_shadow::kGlSrcAlpha; glc.blendDst = hs_world_shadow::kGlOneMinusSrcAlpha;
        glc.blendEq = hs_world_shadow::kGlFuncAdd; glc.depthFunc = hs_world_shadow::kGlLess;
        glc.colorMask = 15; glc.depthMask = 0;

        auto renderComposite = [&](hs_world_shadow::GlShadowOps useOps, bool testReuse,
                                   uint32_t& outDraws, uint32_t& outCoal, bool& outReused, bool& outTeleportFresh,
                                   std::vector<uint8_t>& outFrame2, std::vector<uint8_t>& outReuseImg) -> bool {
            hs_world_shadow::WorldShadowEngine ws;
            ws.SetOps(useOps, true);
            hs_world_shadow::ShadowConfig c = cfg;
            c.temporalStride = testReuse ? 2u : 1u;

            // Frame 1: record 6 GPU batches (4 contiguous baseVertex=0 + 2 distinct baseVertex=16, 20)
            ws.OnGroundOrOceanDraw(fbo, vp); ws.OnMatrixPV(pv);
            if (!ws.OnBeforeFirstStandingEntity(c, saved, glc)) return false;
            for (uint32_t k = 0u; k < 4u; ++k) {
                if (!ws.RecordBatchedShadowGpu(vbo, ibo, ssbo, 6u, 0, static_cast<uintptr_t>(k * 12u), nullptr, fbo)) return false;
            }
            if (!ws.RecordBatchedShadowGpu(vbo, ibo, ssbo, 6u, 16, 48u, nullptr, fbo)) return false;
            if (!ws.RecordBatchedShadowGpu(vbo, ibo, ssbo, 6u, 20, 60u, nullptr, fbo)) return false;
            ws.FlushDeferredBatchedShadows(c, saved, glc);
            ws.OnEndFrame();
            outDraws = ws.LastFrameShadowDraws();
            outCoal  = ws.LastFrameCoalescedDraws();
            float cachedB[4]{};
            ws.GetCachedBounds(cachedB);

            // Frame 2: composite onto turf green (when testReuse=true, walk +2.0 wu -> MUST reuse cached map & lock bounds!)
            pBindFbo(0x8D40u, fbo); pVp(0, 0, 256, 256);
            wops.glColorMask(1, 1, 1, 1); pClrCol(0.4f, 0.7f, 0.3f, 1.0f); pClr(0x4000u);
            if (testReuse) c.luaCamX = 2.0f;
            ws.OnGroundOrOceanDraw(fbo, vp); ws.OnMatrixPV(pv);
            if (!ws.OnBeforeFirstStandingEntity(c, saved, glc)) return false;
            float recvB[4]{}, recvT[4]{};
            ws.GetEntityReceiveUniforms(c, recvB, recvT);
            const bool boundsLocked = (recvB[0] == cachedB[0] && recvB[1] == cachedB[1]);
            ws.OnEndFrame(); pFinish();
            outReused = ws.LastFrameReused() && boundsLocked && (ws.ShadowReuseSkips() == 1ULL) && (ws.LastFrameShadowDraws() == 0u);

            outFrame2.resize(256u * 256u * 4u);
            pBindFbo(0x8D40u, fbo);
            pReadPix(0, 0, 256, 256, 0x1908u, 0x1401u, outFrame2.data());
            if (testReuse) {
                outReuseImg = outFrame2;
                // Frame 3: teleport +12.0 wu with stride=3 -> MUST invalidate immediately!
                c.temporalStride = 3u;
                c.luaCamX = 12.0f;
                ws.OnGroundOrOceanDraw(fbo, vp); ws.OnMatrixPV(pv);
                if (!ws.OnBeforeFirstStandingEntity(c, saved, glc)) return false;
                outTeleportFresh = ws.BeganWriteThisFrame();
                ws.OnEndFrame();
            }
            ws.ResetContext(true);
            return true;
        };

        // Run without MultiDraw (reference) vs with MultiDraw + Temporal Reuse!
        hs_world_shadow::GlShadowOps opsNoMd = wops;
        opsNoMd.glMultiDrawElementsBaseVertex = nullptr;
        uint32_t drawsRef = 0u, coalRef = 0u, drawsMd = 0u, coalMd = 0u;
        bool reusedRef = false, teleRef = false, reusedMd = false, teleMd = false;
        std::vector<uint8_t> imgRef, imgMd, imgReuse, dummyReuse;
        const bool cRunOk = renderComposite(opsNoMd, false, drawsRef, coalRef, reusedRef, teleRef, imgRef, dummyReuse) &&
                            renderComposite(wops,    true,  drawsMd,  coalMd,  reusedMd,  teleMd,  imgMd,  imgReuse);
        int coalMaxDiff = 255;
        if (cRunOk) {
            coalMaxDiff = 0;
            for (size_t i = 0; i < imgRef.size(); ++i) {
                coalMaxDiff = (std::max)(coalMaxDiff, abs(static_cast<int>(imgRef[i]) - static_cast<int>(imgMd[i])));
            }
        }
        pMakeNon(hTex);
        wops.glDeleteBuffers(1, &vbo); wops.glDeleteBuffers(1, &ibo); wops.glDeleteBuffers(1, &ssbo);
        pBindFbo(0x8D40u, 0u); pDelFbo(1, &fbo); pDelTex(1, &rtTex); pDelTex(1, &spTex);
        const uint32_t errC = pGetErr();
        const bool mdExpect = (pMultiDrawBv != nullptr) ? (drawsMd == 1u && coalMd == 5u) : (drawsMd == 3u && coalMd == 3u);
        partCOk = cRunOk && (drawsRef == 3u && coalRef == 3u) && mdExpect &&
                  reusedMd && teleMd && (coalMaxDiff == 0) && (errC == 0u);
        printf("  Part C WorldShadow Coalesce+Reuse Live RTX: ref(drw=%u coal=%u) md(drw=%u coal=%u) reused=%d teleFresh=%d maxDiff=%d err=0x%X -> %s\n",
               drawsRef, coalRef, drawsMd, coalMd, reusedMd ? 1 : 0, teleMd ? 1 : 0,
               coalMaxDiff, errC, partCOk ? "PASS" : "FAIL");
    }

    // -------------------------------------------------------------------------
    // Part D: Live RTX 4060 GpuClarityEngine Adaptive Grade 3 <-> Grade 2
    //         Smooth Hysteresis with ZERO Texture/FBO Reallocations!
    // -------------------------------------------------------------------------
    bool partDOk = false;
    {
        struct ClarityAllocSpy {
            GenTex_t realGenTex = nullptr;
            DelTex_t realDelTex = nullptr;
            hs_gpu_clarity::GlComputeOps::TexStorage2D_t realTexStor = nullptr;
            GenFbo_t realGenFbo = nullptr;
            DelFbo_t realDelFbo = nullptr;
            uint32_t genTex = 0u, delTex = 0u, texStor = 0u, genFbo = 0u, delFbo = 0u;
        };
        static ClarityAllocSpy* s_cs = nullptr;
        ClarityAllocSpy cs{};
        cs.realGenTex  = pGenTex;
        cs.realDelTex  = pDelTex;
        cs.realTexStor = reinterpret_cast<decltype(cs.realTexStor)>(opt.glTexStorage2D);
        cs.realGenFbo  = pGenFbo;
        cs.realDelFbo  = pDelFbo;
        s_cs = &cs;

        hs_gpu_clarity::GlComputeOps cops{};
        cops.glGenFramebuffers        = [](int32_t n, uint32_t* p) WINAPI { s_cs->genFbo += static_cast<uint32_t>(n); s_cs->realGenFbo(n, p); };
        cops.glDeleteFramebuffers     = [](int32_t n, const uint32_t* p) WINAPI { s_cs->delFbo += static_cast<uint32_t>(n); s_cs->realDelFbo(n, p); };
        cops.glBindFramebuffer        = pBindFbo;
        cops.glFramebufferTexture2D   = pFboTex;
        cops.glCheckFramebufferStatus = reinterpret_cast<decltype(cops.glCheckFramebufferStatus)>(findP("glCheckFramebufferStatus"));
        cops.glBlitFramebuffer        = reinterpret_cast<decltype(cops.glBlitFramebuffer)>(opt.glBlitFramebuffer);
        cops.glGenTextures            = [](int32_t n, uint32_t* p) WINAPI { s_cs->genTex += static_cast<uint32_t>(n); s_cs->realGenTex(n, p); };
        cops.glDeleteTextures         = [](int32_t n, const uint32_t* p) WINAPI { s_cs->delTex += static_cast<uint32_t>(n); s_cs->realDelTex(n, p); };
        cops.glActiveTexture          = pActTex;
        cops.glBindTexture            = pBindTex;
        cops.glTexStorage2D           = [](uint32_t t, int32_t l, uint32_t f, int32_t w, int32_t h) WINAPI { ++s_cs->texStor; s_cs->realTexStor(t, l, f, w, h); };
        cops.glTexParameteri          = pTexPar;
        cops.glCreateShader           = pCreateSh;
        cops.glShaderSource           = pShSrc;
        cops.glCompileShader          = pCompSh;
        cops.glGetShaderiv            = pGetShIv;
        cops.glGetShaderInfoLog       = reinterpret_cast<decltype(cops.glGetShaderInfoLog)>(findP("glGetShaderInfoLog"));
        cops.glDeleteShader           = pDelSh;
        cops.glCreateProgram          = pCreatePr;
        cops.glAttachShader           = pAttachSh;
        cops.glLinkProgram            = pLinkPr;
        cops.glGetProgramiv           = pGetPrIv;
        cops.glGetProgramInfoLog      = reinterpret_cast<decltype(cops.glGetProgramInfoLog)>(findP("glGetProgramInfoLog"));
        cops.glDeleteProgram          = pDelPr;
        cops.glUseProgram             = pUsePr;
        cops.glGetUniformLocation     = pGetUni;
        cops.glUniform4f              = reinterpret_cast<decltype(cops.glUniform4f)>(opt.glUniform4f);
        cops.glDispatchCompute        = reinterpret_cast<decltype(cops.glDispatchCompute)>(opt.glDispatchCompute);
        cops.glMemoryBarrier          = reinterpret_cast<decltype(cops.glMemoryBarrier)>(opt.glMemoryBarrier);
        cops.glBindImageTexture       = reinterpret_cast<decltype(cops.glBindImageTexture)>(opt.glBindImageTexture);
        cops.glGetIntegerv            = pGetInt;
        cops.glIsEnabled              = reinterpret_cast<decltype(cops.glIsEnabled)>(opt.glIsEnabled);
        cops.glEnable                 = reinterpret_cast<decltype(cops.glEnable)>(findP("glEnable"));
        cops.glDisable                = reinterpret_cast<decltype(cops.glDisable)>(findP("glDisable"));
        cops.logFn                    = &TestLog;

        const int32_t oddW = 641, oddH = 361;
        uint32_t clFbo = 0u, clTex = 0u;
        pGenFbo(1, &clFbo); pGenTex(1, &clTex);
        pActTex(0x84C0u); pBindTex(0x0DE1u, clTex);
        pTexPar(0x0DE1u, 0x2801u, 0x2601); pTexPar(0x0DE1u, 0x2800u, 0x2601);
        std::vector<uint8_t> img(static_cast<size_t>(oddW) * static_cast<size_t>(oddH) * 4u, 160u);
        pTexImg(0x0DE1u, 0, 0x8058, oddW, oddH, 0, 0x1908u, 0x1401u, img.data());
        pBindFbo(0x8D40u, clFbo); pFboTex(0x8D40u, 0x8CE0u, 0x0DE1u, clTex, 0);

        hs_gpu_clarity::GpuClarityEngine cl;
        cl.SetOps(cops, true);
        hs_gpu_clarity::KnownGlState ks{};
        ks.drawFbo = static_cast<int32_t>(clFbo); ks.readFbo = static_cast<int32_t>(clFbo);
        ks.curProgram = 0; ks.activeTexEnum = static_cast<int32_t>(hs_gpu_clarity::kGlTexture0);
        ks.boundTex0 = static_cast<int32_t>(clTex); ks.scissorEnabled = 0; ks.sampleTimer = false;

        // Warmup Grade 3
        bool ok = cl.ProcessFrame(clFbo, oddW, oddH, { 3, 1.0f, true, 3.0f, 5.5f }, &ks);
        cs.genTex = cs.delTex = cs.texStor = cs.genFbo = cs.delFbo = 0u;

        // 45 frames @ 7.5ms -> smooth step 3 -> 2
        for (int f = 0; f < 45 && ok; ++f) {
            ok = cl.ProcessFrame(clFbo, oddW, oddH, { 3, 1.0f, true, 7.5f, 5.5f }, &ks);
        }
        const int effAfterDown = cl.EffectiveGrade();
        const uint64_t stepsAfterDown = cl.AdaptiveGradeSteps();

        // 120 frames @ 3.5ms -> smooth step 2 -> 3
        for (int f = 0; f < 120 && ok; ++f) {
            ok = cl.ProcessFrame(clFbo, oddW, oddH, { 3, 1.0f, true, 3.5f, 5.5f }, &ks);
        }
        const int effAfterUp = cl.EffectiveGrade();
        const uint64_t stepsAfterUp = cl.AdaptiveGradeSteps();
        const uint32_t reallocTotal = cs.genTex + cs.delTex + cs.texStor + cs.genFbo + cs.delFbo;
        pFinish();

        cl.OnContextReset(true);
        pBindFbo(0x8D40u, 0u); pDelFbo(1, &clFbo); pDelTex(1, &clTex);
        const uint32_t errD = pGetErr();
        s_cs = nullptr;
        partDOk = ok && (effAfterDown == 2) && (stepsAfterDown == 1ULL) &&
                  (effAfterUp == 3) && (stepsAfterUp == 2ULL) &&
                  (reallocTotal == 0u) && (errD == 0u);
        printf("  Part D Adaptive GpuClarity Live RTX (641x361 3->2->3): effDown=%d effUp=%d steps=%llu reallocs=%u err=0x%X -> %s\n",
               effAfterDown, effAfterUp, static_cast<unsigned long long>(stepsAfterUp),
               reallocTotal, errD, partDOk ? "PASS" : "FAIL");
    }

    rt.pWglMakeCurrent(nullptr, nullptr);
    rt.pWglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return partAOk && partBOk && partCOk && partDOk;
}

static bool TestPhase4LiveGpuSuite() {
    printf("\n=== Live RTX GPU Phase 4 Verification: ANIM Early-Out + 1-Tap Recv + 64-Stencil-Flush Batcher + Clarity 3->2->1->0->3 ===\n");
    hs_native_gl::WglRuntime rt{};
    const auto probe = hs_native_gl::ProbeNativeEs2Support(hs_native_gl::BackendMode::Auto, rt);
    if (!probe.supported || !probe.hasBufferStorage || !probe.hasSsbo ||
        !probe.hasBindlessTexture || !probe.hasComputeShader || !probe.hasImageLoadStore) {
        printf("  [SKIP] GL 4.3+ / Bindless / Compute unavailable -> PASS\n");
        return true;
    }
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"HS_Phase4Live", WS_POPUP, 0, 0, 64, 64,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return false;
    HDC hdc = nullptr;
    HGLRC rc = nullptr;
    if (!hs_native_gl::CreateGameEs2Context(rt, hwnd, hdc, rc) || !rt.pWglMakeCurrent(hdc, rc)) {
        if (hdc) ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return false;
    }
    void* procs[76]{};
    hs_native_gl::OptionalGlProcs opt{};
    if (hs_native_gl::ResolveLiveGlesTable(rt, procs, opt) != 76) {
        rt.pWglMakeCurrent(nullptr, nullptr);
        rt.pWglDeleteContext(rc);
        ReleaseDC(hwnd, hdc);
        DestroyWindow(hwnd);
        return false;
    }
    auto findP = [&](const char* nm) -> void* {
        for (size_t i = 0; i < 76; ++i)
            if (std::strcmp(hs_native_gl::kDstGles2Imports[i].name, nm) == 0) return procs[i];
        return nullptr;
    };

    using GenTex_t       = void (WINAPI*)(int32_t, uint32_t*);
    using DelTex_t       = void (WINAPI*)(int32_t, const uint32_t*);
    using BindTex_t      = void (WINAPI*)(uint32_t, uint32_t);
    using TexPar_t       = void (WINAPI*)(uint32_t, uint32_t, int32_t);
    using TexImg_t       = void (WINAPI*)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void*);
    using GenFbo_t       = void (WINAPI*)(int32_t, uint32_t*);
    using DelFbo_t       = void (WINAPI*)(int32_t, const uint32_t*);
    using BindFbo_t      = void (WINAPI*)(uint32_t, uint32_t);
    using FboTex_t       = void (WINAPI*)(uint32_t, uint32_t, uint32_t, uint32_t, int32_t);
    using GenRbo_t       = void (WINAPI*)(int32_t, uint32_t*);
    using DelRbo_t       = void (WINAPI*)(int32_t, const uint32_t*);
    using BindRbo_t      = void (WINAPI*)(uint32_t, uint32_t);
    using RboStor_t      = void (WINAPI*)(uint32_t, uint32_t, int32_t, int32_t);
    using FboRbo_t       = void (WINAPI*)(uint32_t, uint32_t, uint32_t, uint32_t);
    using CreateSh_t     = uint32_t (WINAPI*)(uint32_t);
    using ShSrc_t        = void (WINAPI*)(uint32_t, int32_t, const char* const*, const int32_t*);
    using CompSh_t       = void (WINAPI*)(uint32_t);
    using GetShIv_t      = void (WINAPI*)(uint32_t, uint32_t, int32_t*);
    using DelSh_t        = void (WINAPI*)(uint32_t);
    using CreatePr_t     = uint32_t (WINAPI*)();
    using AttachSh_t     = void (WINAPI*)(uint32_t, uint32_t);
    using BindAtt_t      = void (WINAPI*)(uint32_t, uint32_t, const char*);
    using LinkPr_t       = void (WINAPI*)(uint32_t);
    using GetPrIv_t      = void (WINAPI*)(uint32_t, uint32_t, int32_t*);
    using DelPr_t        = void (WINAPI*)(uint32_t);
    using UsePr_t        = void (WINAPI*)(uint32_t);
    using GetUni_t       = int32_t (WINAPI*)(uint32_t, const char*);
    using GetAtt_t       = int32_t (WINAPI*)(uint32_t, const char*);
    using Uni1f_t        = void (WINAPI*)(int32_t, float);
    using Uni1i_t        = void (WINAPI*)(int32_t, int32_t);
    using Uni1iv_t       = void (WINAPI*)(int32_t, int32_t, const int32_t*);
    using Uni3fv_t       = void (WINAPI*)(int32_t, int32_t, const float*);
    using Uni4fv_t       = void (WINAPI*)(int32_t, int32_t, const float*);
    using UniMat4_t      = void (WINAPI*)(int32_t, int32_t, uint8_t, const float*);
    using ActTex_t       = void (WINAPI*)(uint32_t);
    using Vp_t           = void (WINAPI*)(int32_t, int32_t, int32_t, int32_t);
    using ClrCol_t       = void (WINAPI*)(float, float, float, float);
    using ClrStencil_t   = void (WINAPI*)(int32_t);
    using Clr_t          = void (WINAPI*)(uint32_t);
    using En_t           = void (WINAPI*)(uint32_t);
    using Dis_t          = void (WINAPI*)(uint32_t);
    using StencilFunc_t  = void (WINAPI*)(uint32_t, int32_t, uint32_t);
    using StencilMask_t  = void (WINAPI*)(uint32_t);
    using StencilOp_t    = void (WINAPI*)(uint32_t, uint32_t, uint32_t);
    using EnAtt_t        = void (WINAPI*)(uint32_t);
    using DisAtt_t       = void (WINAPI*)(uint32_t);
    using AttPtr_t       = void (WINAPI*)(uint32_t, int32_t, uint32_t, uint8_t, int32_t, const void*);
    using DrawArr_t      = void (WINAPI*)(uint32_t, int32_t, int32_t);
    using DrawEl_t       = void (WINAPI*)(uint32_t, int32_t, uint32_t, const void*);
    using ReadPix_t      = void (WINAPI*)(int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, void*);
    using GetErr_t       = uint32_t (WINAPI*)();
    using GetInt_t       = void (WINAPI*)(uint32_t, int32_t*);
    using Finish_t       = void (WINAPI*)();

    auto pGenTex      = reinterpret_cast<GenTex_t>(findP("glGenTextures"));
    auto pDelTex      = reinterpret_cast<DelTex_t>(findP("glDeleteTextures"));
    auto pBindTex     = reinterpret_cast<BindTex_t>(findP("glBindTexture"));
    auto pTexPar      = reinterpret_cast<TexPar_t>(findP("glTexParameteri"));
    auto pTexImg      = reinterpret_cast<TexImg_t>(findP("glTexImage2D"));
    auto pGenFbo      = reinterpret_cast<GenFbo_t>(findP("glGenFramebuffers"));
    auto pDelFbo      = reinterpret_cast<DelFbo_t>(findP("glDeleteFramebuffers"));
    auto pBindFbo     = reinterpret_cast<BindFbo_t>(findP("glBindFramebuffer"));
    auto pFboTex      = reinterpret_cast<FboTex_t>(findP("glFramebufferTexture2D"));
    auto pGenRbo      = reinterpret_cast<GenRbo_t>(findP("glGenRenderbuffers"));
    auto pDelRbo      = reinterpret_cast<DelRbo_t>(findP("glDeleteRenderbuffers"));
    auto pBindRbo     = reinterpret_cast<BindRbo_t>(findP("glBindRenderbuffer"));
    auto pRboStor     = reinterpret_cast<RboStor_t>(findP("glRenderbufferStorage"));
    auto pFboRbo      = reinterpret_cast<FboRbo_t>(findP("glFramebufferRenderbuffer"));
    auto pCreateSh    = reinterpret_cast<CreateSh_t>(findP("glCreateShader"));
    auto pShSrc       = reinterpret_cast<ShSrc_t>(findP("glShaderSource"));
    auto pCompSh      = reinterpret_cast<CompSh_t>(findP("glCompileShader"));
    auto pGetShIv     = reinterpret_cast<GetShIv_t>(findP("glGetShaderiv"));
    auto pDelSh       = reinterpret_cast<DelSh_t>(findP("glDeleteShader"));
    auto pCreatePr    = reinterpret_cast<CreatePr_t>(findP("glCreateProgram"));
    auto pAttachSh    = reinterpret_cast<AttachSh_t>(findP("glAttachShader"));
    auto pBindAtt     = reinterpret_cast<BindAtt_t>(findP("glBindAttribLocation"));
    auto pLinkPr      = reinterpret_cast<LinkPr_t>(findP("glLinkProgram"));
    auto pGetPrIv     = reinterpret_cast<GetPrIv_t>(findP("glGetProgramiv"));
    auto pDelPr       = reinterpret_cast<DelPr_t>(findP("glDeleteProgram"));
    auto pUsePr       = reinterpret_cast<UsePr_t>(findP("glUseProgram"));
    auto pGetUni      = reinterpret_cast<GetUni_t>(findP("glGetUniformLocation"));
    auto pGetAtt      = reinterpret_cast<GetAtt_t>(findP("glGetAttribLocation"));
    auto pUni1f       = reinterpret_cast<Uni1f_t>(findP("glUniform1f"));
    auto pUni1i       = reinterpret_cast<Uni1i_t>(findP("glUniform1i"));
    auto pUni1iv      = reinterpret_cast<Uni1iv_t>(findP("glUniform1iv"));
    auto pUni3fv      = reinterpret_cast<Uni3fv_t>(findP("glUniform3fv"));
    auto pUni4fv      = reinterpret_cast<Uni4fv_t>(findP("glUniform4fv"));
    auto pUniMat4     = reinterpret_cast<UniMat4_t>(findP("glUniformMatrix4fv"));
    auto pActTex      = reinterpret_cast<ActTex_t>(findP("glActiveTexture"));
    auto pVp          = reinterpret_cast<Vp_t>(findP("glViewport"));
    auto pClrCol      = reinterpret_cast<ClrCol_t>(findP("glClearColor"));
    auto pClrStencil  = reinterpret_cast<ClrStencil_t>(findP("glClearStencil"));
    auto pClr         = reinterpret_cast<Clr_t>(findP("glClear"));
    auto pEn          = reinterpret_cast<En_t>(findP("glEnable"));
    auto pDis         = reinterpret_cast<Dis_t>(findP("glDisable"));
    auto pStFunc      = reinterpret_cast<StencilFunc_t>(findP("glStencilFunc"));
    auto pStMask      = reinterpret_cast<StencilMask_t>(findP("glStencilMask"));
    auto pStOp        = reinterpret_cast<StencilOp_t>(findP("glStencilOp"));
    auto pEnAtt       = reinterpret_cast<EnAtt_t>(findP("glEnableVertexAttribArray"));
    auto pDisAtt      = reinterpret_cast<DisAtt_t>(findP("glDisableVertexAttribArray"));
    auto pAttPtr      = reinterpret_cast<AttPtr_t>(findP("glVertexAttribPointer"));
    auto pDrawArr     = reinterpret_cast<DrawArr_t>(findP("glDrawArrays"));
    auto pDrawEl      = reinterpret_cast<DrawEl_t>(findP("glDrawElements"));
    auto pReadPix     = reinterpret_cast<ReadPix_t>(findP("glReadPixels"));
    auto pGetErr      = reinterpret_cast<GetErr_t>(findP("glGetError"));
    auto pGetInt      = reinterpret_cast<GetInt_t>(findP("glGetIntegerv"));
    auto pFinish      = reinterpret_cast<Finish_t>(findP("glFinish"));
    auto pBindBuf     = reinterpret_cast<hs_sprite_batch::GlBatchOps::BindBuffer_t>(findP("glBindBuffer"));
    auto pGenBuf      = reinterpret_cast<hs_sprite_batch::GlBatchOps::GenBuffers_t>(findP("glGenBuffers"));
    auto pDelBuf      = reinterpret_cast<hs_sprite_batch::GlBatchOps::DeleteBuffers_t>(findP("glDeleteBuffers"));
    auto pBufData     = reinterpret_cast<hs_sprite_batch::GlBatchOps::BufferData_t>(findP("glBufferData"));

    while (pGetErr() != 0u) {}

    // -------------------------------------------------------------------------
    // Part A: Live RTX 4060 TransformHd2dShaderSource ANIM Fragment Shader
    //         Comparing Pre-Phase4 vs Phase4 `if (hs_effMask > 0.001)` early-out
    //         across opaque, semi-transparent (0.04, 0.25, 0.5, 1.0), transparent
    //         margin (0.0), and glowing (LIGHT_OVERRIDE=0.5) quads with active
    //         derivatives + HD2D shade + 5-tap shadow -> maxDiff == 0, err == 0x0!
    //         Plus HS_RECV_BOUNDS.w = 0.65f 1-tap shadow receive verification!
    // -------------------------------------------------------------------------
    bool partAOk = false;
    {
        static constexpr const char* kAnimVs =
            "#version 100\n"
            "attribute vec4 aPosUv;\n"
            "varying vec2 vUv;\n"
            "varying vec3 PS_POS;\n"
            "varying vec2 HS_LOCAL_METRES;\n"
            "void main() {\n"
            "  vUv = aPosUv.zw;\n"
            "  PS_POS = vec3(aPosUv.x * 2.0, (aPosUv.y + 1.0) * 1.2, 0.0);\n"
            "  HS_LOCAL_METRES = vec2(aPosUv.x * 1.5, (aPosUv.y + 1.0) * 1.2);\n"
            "  gl_Position = vec4(aPosUv.xy, 0.0, 1.0);\n"
            "}\n";

        static constexpr const char* kAnimFsHead =
            "#version 100\n"
            "#extension GL_OES_standard_derivatives : enable\n"
            "precision highp float;\n"
            "uniform sampler2D uTex;\n"
            "uniform sampler2D HS_SHADOW_MASK;\n"
            "uniform vec4 HS_HD2D_PARAMS;\n"
            "uniform vec4 HS_HD2D_SHADE;\n"
            "uniform vec4 HS_HD2D_SHADE2;\n"
            "uniform vec4 HS_HD2D_SHAD_PROJ;\n"
            "uniform vec4 HS_DETAIL_PARAMS;\n"
            "uniform vec4 HS_RECV_BOUNDS;\n"
            "uniform vec4 HS_RECV_TINT;\n"
            "uniform float LIGHT_OVERRIDE;\n"
            "varying vec2 vUv;\n"
            "varying vec3 PS_POS;\n"
            "varying vec2 HS_LOCAL_METRES;\n"
            "void main() {\n"
            "  vec4 c = texture2D(uTex, vUv);\n"
            "  gl_FragColor = vec4(c.rgb * c.a, c.a);\n";

        // Pre-Phase 4 ANIM body (no hs_effMask > 0.001 early-out)
        static constexpr const char* kAnimBodyOld =
            "  if (HS_HD2D_PARAMS.x > 0.001 || HS_HD2D_SHADE.x > 0.001 || HS_RECV_TINT.a > 0.001 || HS_DETAIL_PARAMS.y > 0.001 || HS_DETAIL_PARAMS.z > 0.001) {\n"
            "    float hs_a = gl_FragColor.a;\n"
            "    float hs_effMask = step(0.08, hs_a) * (1.0 - smoothstep(0.05, 0.35, LIGHT_OVERRIDE));\n"
            "    float hs_aSafe = max(hs_a, 0.001);\n"
            "    vec3 hs_col = gl_FragColor.rgb / hs_aSafe;\n"
            "    float hs_luma = dot(hs_col, vec3(0.299, 0.587, 0.114));\n"
            "    float hs_ink = smoothstep(0.05, 0.22, hs_luma);\n"
            "    vec3 hs_res = hs_col;\n"
            "    float hs_outA = hs_a;\n"
            "#ifdef GL_OES_standard_derivatives\n"
            "    if (HS_DETAIL_PARAMS.y > 0.001 || HS_DETAIL_PARAMS.z > 0.001) {\n"
            "      float hs_fa = fwidth(hs_a);\n"
            "      float hs_lgrad = length(vec2(dFdx(hs_luma), dFdy(hs_luma)));\n"
            "      float hs_litGate = 1.0 - smoothstep(0.05, 0.28, LIGHT_OVERRIDE);\n"
            "      if (HS_DETAIL_PARAMS.y > 0.001 && hs_a > 0.005 && hs_a < 0.995) {\n"
            "        float hs_eMask = smoothstep(0.14, 0.44, hs_fa) * hs_litGate;\n"
            "        float hs_crisp = clamp((hs_a - 0.48) / max(hs_fa * 0.72, 0.22) + 0.50, 0.0, 1.0);\n"
            "        hs_outA = mix(hs_a, hs_crisp, clamp(clamp(HS_DETAIL_PARAMS.y, 0.0, 1.5) * 0.55 * hs_eMask, 0.0, 0.85));\n"
            "      }\n"
            "      if (HS_DETAIL_PARAMS.z > 0.001 && hs_a > 0.50) {\n"
            "        float hs_dInkStr = clamp(HS_DETAIL_PARAMS.z, 0.0, 1.5);\n"
            "        float hs_dStroke = smoothstep(0.045, 0.22, hs_lgrad) * smoothstep(0.015, 0.06, hs_luma) * (1.0 - smoothstep(0.12, 0.38, hs_luma));\n"
            "        hs_res *= (1.0 - 0.22 * hs_dInkStr * hs_dStroke);\n"
            "        float hs_solidMask = smoothstep(0.85, 0.98, hs_a) * (1.0 - smoothstep(0.18, 0.42, hs_fa));\n"
            "        float hs_bEdge = smoothstep(0.06, 0.26, hs_lgrad) * smoothstep(0.28, 0.65, hs_luma) * (1.0 - smoothstep(0.82, 0.98, hs_luma));\n"
            "        hs_res *= (1.0 + 0.075 * hs_dInkStr * hs_bEdge * hs_solidMask);\n"
            "      }\n"
            "    }\n"
            "#endif\n"
            "    if (HS_HD2D_PARAMS.x > 0.001) {\n"
            "      float hs_s = min(HS_HD2D_PARAMS.x, 1.50);\n"
            "      float hs_y = max(PS_POS.y, 0.0);\n"
            "      float hs_ao = mix(1.0 - 0.13 * hs_s, 1.0, smoothstep(0.0, 0.45, hs_y));\n"
            "      hs_res *= mix(1.0, hs_ao, hs_ink);\n"
            "      vec3 hs_over = max(hs_res - vec3(0.86), vec3(0.0));\n"
            "      hs_res -= hs_over * (0.35 * min(hs_s, 1.0));\n"
            "    }\n"
            "    if (HS_HD2D_SHADE.x > 0.001 && HS_HD2D_SHADE2.w > 0.005) {\n"
            "      float hs_sl = clamp(HS_HD2D_SHADE.x, 0.0, 1.5) * clamp(HS_HD2D_SHADE2.w, 0.0, 1.0);\n"
            "      float hs_warm = clamp(HS_HD2D_SHADE.y, 0.0, 1.0);\n"
            "      float hs_con = clamp(HS_HD2D_SHADE.z, 0.0, 1.0);\n"
            "      float hs_scool = clamp(HS_HD2D_SHADE.w, 0.0, 1.0);\n"
            "      float hs_sdark = clamp(HS_HD2D_SHADE2.x, 0.0, 1.0);\n"
            "      float hs_theta = HS_HD2D_SHADE2.y;\n"
            "      float hs_el = clamp(HS_HD2D_SHADE2.z, 0.08, 1.50);\n"
            "      float hs_elev_n = clamp(hs_el * 0.63661977, 0.0, 1.0);\n"
            "      float hs_sun_x = cos(hs_theta);\n"
            "      float hs_sun_f = sin(hs_theta);\n"
            "      float hs_lx = HS_LOCAL_METRES.x;\n"
            "      float hs_nx = hs_lx / sqrt(hs_lx * hs_lx + 0.72);\n"
            "      float hs_roof = smoothstep(0.30, 2.30, HS_LOCAL_METRES.y);\n"
            "      float hs_nup = 0.22 + hs_roof * 0.28;\n"
            "      float hs_nfront = sqrt(max(0.06, 1.0 - hs_nx * hs_nx));\n"
            "      vec2 hs_sun2 = normalize(vec2(hs_sun_x, sin(hs_el) * 2.60 + 0.18));\n"
            "      float hs_diff = max(hs_nx * hs_sun2.x + hs_nup * 1.35 * hs_sun2.y + hs_nfront * 0.50 * hs_sun_f, 0.0);\n"
            "      float hs_direct = smoothstep(0.08, 0.90, hs_diff);\n"
            "      float hs_back = 1.0 - hs_direct;\n"
            "      float hs_amp = 0.40 + 1.20 * hs_con;\n"
            "      float hs_warmth = 1.0 - smoothstep(0.10, 0.85, hs_elev_n);\n"
            "      vec3 hs_sun_col = mix(vec3(1.0, 0.96, 0.86), vec3(1.0, 0.70, 0.34), hs_warmth);\n"
            "      vec3 hs_lit_hue = mix(vec3(0.86, 0.98, 1.18), vec3(1.14, 1.02, 0.82), hs_warm);\n"
            "      vec3 hs_back_hue = mix(vec3(1.14, 1.00, 0.84), vec3(0.84, 0.95, 1.22), hs_scool);\n"
            "      float hs_dark_amp = 0.40 + 1.20 * hs_sdark;\n"
            "      float hs_h_mask = smoothstep(0.05, 0.25, max(PS_POS.y, HS_LOCAL_METRES.y));\n"
            "      float hs_str = hs_sl * hs_ink * (0.35 + 0.65 * hs_h_mask);\n"
            "      vec3 hs_lit_mul = mix(vec3(1.0), hs_sun_col * hs_lit_hue * (1.0 + 0.16 * hs_amp), hs_direct * hs_str);\n"
            "      vec3 hs_shd_mul = mix(vec3(1.0), hs_back_hue * (1.0 - 0.17 * hs_amp * hs_dark_amp), hs_back * hs_str);\n"
            "      float hs_rim = clamp(-hs_sun_f, 0.0, 1.0) * pow(1.0 - hs_nfront, 1.6) * (0.20 * hs_amp * hs_str);\n"
            "      hs_res = hs_res * hs_lit_mul * hs_shd_mul + hs_sun_col * hs_lit_hue * hs_rim;\n"
            "    }\n"
            "    if (HS_RECV_TINT.a > 0.001 && HS_RECV_BOUNDS.w > 0.5) {\n"
            "      float hs_py = max(PS_POS.y, 0.0);\n"
            "      vec2 hs_rdir = normalize(HS_HD2D_SHAD_PROJ.xy + vec2(1e-6, 0.0));\n"
            "      vec2 hs_rperp = vec2(-hs_rdir.y, hs_rdir.x);\n"
            "      vec2 hs_rg = PS_POS.xz + hs_rdir * (hs_py * HS_HD2D_SHAD_PROJ.z);\n"
            "      vec2 hs_ruv = (hs_rg - HS_RECV_BOUNDS.xy) * (HS_RECV_BOUNDS.z * 0.5) + 0.5;\n"
            "      vec2 hs_redge = abs(hs_ruv * 2.0 - 1.0);\n"
            "      float hs_rin = 1.0 - smoothstep(0.90, 1.0, max(hs_redge.x, hs_redge.y));\n"
            "      if (hs_rin > 0.001) {\n"
            "        float hs_uvs = HS_RECV_BOUNDS.z * 0.5;\n"
            "        vec2 hs_dp1 = hs_rperp * (((HS_RECV_BOUNDS.w > 1.5) ? 0.62 : 0.28) * hs_uvs);\n"
            "        vec2 hs_dp2 = (HS_RECV_BOUNDS.w > 1.5) ? (hs_rperp * (0.95 * hs_uvs)) : (hs_rdir * (0.22 * hs_uvs));\n"
            "        vec4 hs_t0 = texture2D(HS_SHADOW_MASK, hs_ruv);\n"
            "        vec4 hs_t1 = texture2D(HS_SHADOW_MASK, hs_ruv + hs_dp1);\n"
            "        vec4 hs_t2 = texture2D(HS_SHADOW_MASK, hs_ruv - hs_dp1);\n"
            "        vec4 hs_t3 = texture2D(HS_SHADOW_MASK, hs_ruv + hs_dp2);\n"
            "        vec4 hs_t4 = texture2D(HS_SHADOW_MASK, hs_ruv - hs_dp2);\n"
            "        float hs_o0 = hs_t0.r * smoothstep(0.45, 1.15, hs_t0.b * 7.5 - hs_py);\n"
            "        float hs_o1 = hs_t1.r * smoothstep(0.45, 1.15, hs_t1.b * 7.5 - hs_py);\n"
            "        float hs_o2 = hs_t2.r * smoothstep(0.45, 1.15, hs_t2.b * 7.5 - hs_py);\n"
            "        float hs_o3 = hs_t3.r * smoothstep(0.45, 1.15, hs_t3.b * 7.5 - hs_py);\n"
            "        float hs_o4 = hs_t4.r * smoothstep(0.45, 1.15, hs_t4.b * 7.5 - hs_py);\n"
            "        float hs_occ = (HS_RECV_BOUNDS.w > 1.5) ? max(hs_o0, max((hs_o1 + hs_o2) * 0.50, min(hs_o3, hs_o4))) : (hs_o0 * 0.40 + (hs_o1 + hs_o2 + hs_o3 + hs_o4) * 0.15);\n"
            "        float hs_rstr = clamp(hs_occ, 0.0, 1.0) * hs_rin * clamp(HS_RECV_TINT.a, 0.0, 1.0);\n"
            "        hs_res *= mix(vec3(1.0), clamp(HS_RECV_TINT.rgb, vec3(0.0), vec3(1.0)), hs_rstr * hs_ink);\n"
            "      }\n"
            "    }\n"
            "    vec3 hs_origScaled = (hs_a > 0.001) ? (gl_FragColor.rgb * (hs_outA / hs_a)) : vec3(0.0);\n"
            "    gl_FragColor.rgb = mix(hs_origScaled, min(max(hs_res, vec3(0.0)) * hs_outA, vec3(hs_outA)), hs_effMask);\n"
            "    gl_FragColor.a = hs_outA;\n"
            "  }\n}\n";

        // Exact Phase 4 ANIM body from native_plugin/dinput8.cpp:L7210-L7318
        static constexpr const char* kAnimBodyNew =
            "  if (HS_HD2D_PARAMS.x > 0.001 || HS_HD2D_SHADE.x > 0.001 || HS_RECV_TINT.a > 0.001 || HS_DETAIL_PARAMS.y > 0.001 || HS_DETAIL_PARAMS.z > 0.001) {\n"
            "    float hs_a = gl_FragColor.a;\n"
            "    float hs_effMask = step(0.08, hs_a) * (1.0 - smoothstep(0.05, 0.35, LIGHT_OVERRIDE));\n"
            "    float hs_aSafe = max(hs_a, 0.001);\n"
            "    vec3 hs_col = gl_FragColor.rgb / hs_aSafe;\n"
            "    float hs_luma = dot(hs_col, vec3(0.299, 0.587, 0.114));\n"
            "    vec3 hs_res = hs_col;\n"
            "    float hs_outA = hs_a;\n"
            "#ifdef GL_OES_standard_derivatives\n"
            "    if (HS_DETAIL_PARAMS.y > 0.001 || HS_DETAIL_PARAMS.z > 0.001) {\n"
            "      float hs_fa = fwidth(hs_a);\n"
            "      float hs_lgrad = length(vec2(dFdx(hs_luma), dFdy(hs_luma)));\n"
            "      float hs_litGate = 1.0 - smoothstep(0.05, 0.28, LIGHT_OVERRIDE);\n"
            "      if (HS_DETAIL_PARAMS.y > 0.001 && hs_a > 0.005 && hs_a < 0.995) {\n"
            "        float hs_eMask = smoothstep(0.14, 0.44, hs_fa) * hs_litGate;\n"
            "        float hs_crisp = clamp((hs_a - 0.48) / max(hs_fa * 0.72, 0.22) + 0.50, 0.0, 1.0);\n"
            "        hs_outA = mix(hs_a, hs_crisp, clamp(clamp(HS_DETAIL_PARAMS.y, 0.0, 1.5) * 0.55 * hs_eMask, 0.0, 0.85));\n"
            "      }\n"
            "      if (HS_DETAIL_PARAMS.z > 0.001 && hs_a > 0.50) {\n"
            "        float hs_dInkStr = clamp(HS_DETAIL_PARAMS.z, 0.0, 1.5);\n"
            "        float hs_dStroke = smoothstep(0.045, 0.22, hs_lgrad) * smoothstep(0.015, 0.06, hs_luma) * (1.0 - smoothstep(0.12, 0.38, hs_luma));\n"
            "        hs_res *= (1.0 - 0.22 * hs_dInkStr * hs_dStroke);\n"
            "        float hs_solidMask = smoothstep(0.85, 0.98, hs_a) * (1.0 - smoothstep(0.18, 0.42, hs_fa));\n"
            "        float hs_bEdge = smoothstep(0.06, 0.26, hs_lgrad) * smoothstep(0.28, 0.65, hs_luma) * (1.0 - smoothstep(0.82, 0.98, hs_luma));\n"
            "        hs_res *= (1.0 + 0.075 * hs_dInkStr * hs_bEdge * hs_solidMask);\n"
            "      }\n"
            "    }\n"
            "#endif\n"
            "    if (hs_effMask > 0.001) {\n"
            "      float hs_ink = smoothstep(0.05, 0.22, hs_luma);\n"
            "      if (HS_HD2D_PARAMS.x > 0.001) {\n"
            "        float hs_s = min(HS_HD2D_PARAMS.x, 1.50);\n"
            "        float hs_y = max(PS_POS.y, 0.0);\n"
            "        float hs_ao = mix(1.0 - 0.13 * hs_s, 1.0, smoothstep(0.0, 0.45, hs_y));\n"
            "        hs_res *= mix(1.0, hs_ao, hs_ink);\n"
            "        vec3 hs_over = max(hs_res - vec3(0.86), vec3(0.0));\n"
            "        hs_res -= hs_over * (0.35 * min(hs_s, 1.0));\n"
            "      }\n"
            "      if (HS_HD2D_SHADE.x > 0.001 && HS_HD2D_SHADE2.w > 0.005) {\n"
            "        float hs_sl = clamp(HS_HD2D_SHADE.x, 0.0, 1.5) * clamp(HS_HD2D_SHADE2.w, 0.0, 1.0);\n"
            "        float hs_warm = clamp(HS_HD2D_SHADE.y, 0.0, 1.0);\n"
            "        float hs_con = clamp(HS_HD2D_SHADE.z, 0.0, 1.0);\n"
            "        float hs_scool = clamp(HS_HD2D_SHADE.w, 0.0, 1.0);\n"
            "        float hs_sdark = clamp(HS_HD2D_SHADE2.x, 0.0, 1.0);\n"
            "        float hs_theta = HS_HD2D_SHADE2.y;\n"
            "        float hs_el = clamp(HS_HD2D_SHADE2.z, 0.08, 1.50);\n"
            "        float hs_elev_n = clamp(hs_el * 0.63661977, 0.0, 1.0);\n"
            "        float hs_sun_x = cos(hs_theta);\n"
            "        float hs_sun_f = sin(hs_theta);\n"
            "        float hs_lx = HS_LOCAL_METRES.x;\n"
            "        float hs_nx = hs_lx / sqrt(hs_lx * hs_lx + 0.72);\n"
            "        float hs_roof = smoothstep(0.30, 2.30, HS_LOCAL_METRES.y);\n"
            "        float hs_nup = 0.22 + hs_roof * 0.28;\n"
            "        float hs_nfront = sqrt(max(0.06, 1.0 - hs_nx * hs_nx));\n"
            "        vec2 hs_sun2 = normalize(vec2(hs_sun_x, sin(hs_el) * 2.60 + 0.18));\n"
            "        float hs_diff = max(hs_nx * hs_sun2.x + hs_nup * 1.35 * hs_sun2.y + hs_nfront * 0.50 * hs_sun_f, 0.0);\n"
            "        float hs_direct = smoothstep(0.08, 0.90, hs_diff);\n"
            "        float hs_back = 1.0 - hs_direct;\n"
            "        float hs_amp = 0.40 + 1.20 * hs_con;\n"
            "        float hs_warmth = 1.0 - smoothstep(0.10, 0.85, hs_elev_n);\n"
            "        vec3 hs_sun_col = mix(vec3(1.0, 0.96, 0.86), vec3(1.0, 0.70, 0.34), hs_warmth);\n"
            "        vec3 hs_lit_hue = mix(vec3(0.86, 0.98, 1.18), vec3(1.14, 1.02, 0.82), hs_warm);\n"
            "        vec3 hs_back_hue = mix(vec3(1.14, 1.00, 0.84), vec3(0.84, 0.95, 1.22), hs_scool);\n"
            "        float hs_dark_amp = 0.40 + 1.20 * hs_sdark;\n"
            "        float hs_h_mask = smoothstep(0.05, 0.25, max(PS_POS.y, HS_LOCAL_METRES.y));\n"
            "        float hs_str = hs_sl * hs_ink * (0.35 + 0.65 * hs_h_mask);\n"
            "        vec3 hs_lit_mul = mix(vec3(1.0), hs_sun_col * hs_lit_hue * (1.0 + 0.16 * hs_amp), hs_direct * hs_str);\n"
            "        vec3 hs_shd_mul = mix(vec3(1.0), hs_back_hue * (1.0 - 0.17 * hs_amp * hs_dark_amp), hs_back * hs_str);\n"
            "        float hs_rim = clamp(-hs_sun_f, 0.0, 1.0) * pow(1.0 - hs_nfront, 1.6) * (0.20 * hs_amp * hs_str);\n"
            "        hs_res = hs_res * hs_lit_mul * hs_shd_mul + hs_sun_col * hs_lit_hue * hs_rim;\n"
            "      }\n"
            "      if (HS_RECV_TINT.a > 0.001 && HS_RECV_BOUNDS.w > 0.5) {\n"
            "        float hs_py = max(PS_POS.y, 0.0);\n"
            "        vec2 hs_rdir = normalize(HS_HD2D_SHAD_PROJ.xy + vec2(1e-6, 0.0));\n"
            "        vec2 hs_rg = PS_POS.xz + hs_rdir * (hs_py * HS_HD2D_SHAD_PROJ.z);\n"
            "        vec2 hs_ruv = (hs_rg - HS_RECV_BOUNDS.xy) * (HS_RECV_BOUNDS.z * 0.5) + 0.5;\n"
            "        vec2 hs_redge = abs(hs_ruv * 2.0 - 1.0);\n"
            "        float hs_rin = 1.0 - smoothstep(0.90, 1.0, max(hs_redge.x, hs_redge.y));\n"
            "        if (hs_rin > 0.001) {\n"
            "          float hs_occ = 0.0;\n"
            "          if (HS_RECV_BOUNDS.w < 0.75) {\n"
            "            vec4 hs_t0 = texture2D(HS_SHADOW_MASK, hs_ruv);\n"
            "            hs_occ = hs_t0.r * smoothstep(0.45, 1.15, hs_t0.b * 7.5 - hs_py);\n"
            "          } else {\n"
            "            vec2 hs_rperp = vec2(-hs_rdir.y, hs_rdir.x);\n"
            "            float hs_uvs = HS_RECV_BOUNDS.z * 0.5;\n"
            "            vec2 hs_dp1 = hs_rperp * (((HS_RECV_BOUNDS.w > 1.5) ? 0.62 : 0.28) * hs_uvs);\n"
            "            vec2 hs_dp2 = (HS_RECV_BOUNDS.w > 1.5) ? (hs_rperp * (0.95 * hs_uvs)) : (hs_rdir * (0.22 * hs_uvs));\n"
            "            vec4 hs_t0 = texture2D(HS_SHADOW_MASK, hs_ruv);\n"
            "            vec4 hs_t1 = texture2D(HS_SHADOW_MASK, hs_ruv + hs_dp1);\n"
            "            vec4 hs_t2 = texture2D(HS_SHADOW_MASK, hs_ruv - hs_dp1);\n"
            "            vec4 hs_t3 = texture2D(HS_SHADOW_MASK, hs_ruv + hs_dp2);\n"
            "            vec4 hs_t4 = texture2D(HS_SHADOW_MASK, hs_ruv - hs_dp2);\n"
            "            float hs_o0 = hs_t0.r * smoothstep(0.45, 1.15, hs_t0.b * 7.5 - hs_py);\n"
            "            float hs_o1 = hs_t1.r * smoothstep(0.45, 1.15, hs_t1.b * 7.5 - hs_py);\n"
            "            float hs_o2 = hs_t2.r * smoothstep(0.45, 1.15, hs_t2.b * 7.5 - hs_py);\n"
            "            float hs_o3 = hs_t3.r * smoothstep(0.45, 1.15, hs_t3.b * 7.5 - hs_py);\n"
            "            float hs_o4 = hs_t4.r * smoothstep(0.45, 1.15, hs_t4.b * 7.5 - hs_py);\n"
            "            hs_occ = (HS_RECV_BOUNDS.w > 1.5) ? max(hs_o0, max((hs_o1 + hs_o2) * 0.50, min(hs_o3, hs_o4))) : (hs_o0 * 0.40 + (hs_o1 + hs_o2 + hs_o3 + hs_o4) * 0.15);\n"
            "          }\n"
            "          float hs_rstr = clamp(hs_occ, 0.0, 1.0) * hs_rin * clamp(HS_RECV_TINT.a, 0.0, 1.0);\n"
            "          hs_res *= mix(vec3(1.0), clamp(HS_RECV_TINT.rgb, vec3(0.0), vec3(1.0)), hs_rstr * hs_ink);\n"
            "        }\n"
            "      }\n"
            "    }\n"
            "    vec3 hs_origScaled = (hs_a > 0.001) ? (gl_FragColor.rgb * (hs_outA / hs_a)) : vec3(0.0);\n"
            "    gl_FragColor.rgb = mix(hs_origScaled, min(max(hs_res, vec3(0.0)) * hs_outA, vec3(hs_outA)), hs_effMask);\n"
            "    gl_FragColor.a = hs_outA;\n"
            "  }\n}\n";

        auto buildAnimProg = [&](const char* body, int32_t& outOk) -> uint32_t {
            std::string fsSrc = std::string(kAnimFsHead) + body;
            uint32_t vs = pCreateSh(0x8B31u), fs = pCreateSh(0x8B30u);
            const char* vp = kAnimVs; int32_t vl = static_cast<int32_t>(std::strlen(kAnimVs));
            const char* fp = fsSrc.c_str(); int32_t fl = static_cast<int32_t>(fsSrc.size());
            pShSrc(vs, 1, &vp, &vl); pCompSh(vs);
            pShSrc(fs, 1, &fp, &fl); pCompSh(fs);
            uint32_t pr = pCreatePr();
            pAttachSh(pr, vs); pAttachSh(pr, fs);
            pBindAtt(pr, 0u, "aPosUv");
            pLinkPr(pr);
            pDelSh(vs); pDelSh(fs);
            pGetPrIv(pr, 0x8B82u, &outOk);
            return pr;
        };
        int32_t okOld = 0, okNew = 0;
        uint32_t prOld = buildAnimProg(kAnimBodyOld, okOld);
        uint32_t prNew = buildAnimProg(kAnimBodyNew, okNew);

        // Build 64x64 sprite test texture with alpha bands (0.00, 0.04, 0.25, 0.50, 1.00 + soft edge gradient for dFdx/dFdy!)
        // and a 64x64 shadow mask texture with full shadow coverage + height=6.0m (b=0.80)
        uint32_t fbo = 0u, rtTex = 0u, spTex = 0u, shTex = 0u;
        pGenFbo(1, &fbo); pGenTex(1, &rtTex); pGenTex(1, &spTex); pGenTex(1, &shTex);
        pActTex(0x84C0u); pBindTex(0x0DE1u, rtTex);
        pTexPar(0x0DE1u, 0x2801u, 0x2600); pTexPar(0x0DE1u, 0x2800u, 0x2600);
        pTexImg(0x0DE1u, 0, 0x8058, 64, 64, 0, 0x1908u, 0x1401u, nullptr);
        pBindFbo(0x8D40u, fbo); pFboTex(0x8D40u, 0x8CE0u, 0x0DE1u, rtTex, 0);

        std::vector<uint8_t> spPx(64u * 64u * 4u);
        for (int y = 0; y < 64; ++y) {
            for (int x = 0; x < 64; ++x) {
                float a = 1.0f;
                if (x < 10) a = 0.0f;
                else if (x < 20) a = 0.04f;
                else if (x < 30) a = 0.25f;
                else if (x < 40) a = 0.50f;
                else if (x < 48) a = static_cast<float>(x - 40) / 8.0f; // active alpha edge ramp for dFdx/dFdy!
                else a = 1.0f;
                const size_t i = (static_cast<size_t>(y) * 64u + static_cast<size_t>(x)) * 4u;
                spPx[i + 0] = static_cast<uint8_t>(90 + ((x * 5 + y * 3) & 127));
                spPx[i + 1] = static_cast<uint8_t>(110 + ((x * 3 + y * 7) & 95));
                spPx[i + 2] = static_cast<uint8_t>(80 + ((x * 7 + y * 5) & 111));
                spPx[i + 3] = static_cast<uint8_t>(a * 255.0f + 0.5f);
            }
        }
        pActTex(0x84C0u); pBindTex(0x0DE1u, spTex);
        pTexPar(0x0DE1u, 0x2801u, 0x2601); pTexPar(0x0DE1u, 0x2800u, 0x2601);
        pTexImg(0x0DE1u, 0, 0x8058, 64, 64, 0, 0x1908u, 0x1401u, spPx.data());

        std::vector<uint8_t> shPx(64u * 64u * 4u);
        for (size_t i = 0; i < 64u * 64u; ++i) {
            shPx[i * 4 + 0] = 240u; // coverage ~0.94
            shPx[i * 4 + 1] = 128u;
            shPx[i * 4 + 2] = 220u; // caster height ~6.47m > hs_py
            shPx[i * 4 + 3] = 255u;
        }
        pActTex(0x84C1u); pBindTex(0x0DE1u, shTex);
        pTexPar(0x0DE1u, 0x2801u, 0x2601); pTexPar(0x0DE1u, 0x2800u, 0x2601);
        pTexImg(0x0DE1u, 0, 0x8058, 64, 64, 0, 0x1908u, 0x1401u, shPx.data());
        pActTex(0x84C0u);

        const float quad[16] = { -1,-1,0,0, 1,-1,1,0, -1,1,0,1, 1,1,1,1 };
        auto renderAnim = [&](uint32_t prog, float lightOv, float recvW, std::vector<uint8_t>& out) {
            pBindFbo(0x8D40u, fbo); pVp(0, 0, 64, 64);
            pDis(0x0BE2u /*GL_BLEND*/); pDis(0x0B90u /*GL_STENCIL_TEST*/);
            pClrCol(0.0f, 0.0f, 0.0f, 0.0f); pClr(0x4000u);
            pUsePr(prog);
            pUni1i(pGetUni(prog, "uTex"), 0);
            pUni1i(pGetUni(prog, "HS_SHADOW_MASK"), 1);
            const float hdP[4]   = { 1.0f, 0.0f, 0.0f, 0.0f };
            const float hdS[4]   = { 1.0f, 0.7f, 0.8f, 0.6f };
            const float hdS2[4]  = { 0.7f, 0.9f, 0.6f, 1.0f };
            const float shPr[4]  = { 0.0f, 1.0f, 1.5f, 1.0f };
            const float detP[4]  = { 0.0f, 0.8f, 0.8f, 0.0f };
            const float recvB[4] = { 0.0f, 0.0f, 0.05f, recvW };
            const float recvT[4] = { 0.12f, 0.16f, 0.38f, 0.65f };
            pUni4fv(pGetUni(prog, "HS_HD2D_PARAMS"), 1, hdP);
            pUni4fv(pGetUni(prog, "HS_HD2D_SHADE"), 1, hdS);
            pUni4fv(pGetUni(prog, "HS_HD2D_SHADE2"), 1, hdS2);
            pUni4fv(pGetUni(prog, "HS_HD2D_SHAD_PROJ"), 1, shPr);
            pUni4fv(pGetUni(prog, "HS_DETAIL_PARAMS"), 1, detP);
            pUni4fv(pGetUni(prog, "HS_RECV_BOUNDS"), 1, recvB);
            pUni4fv(pGetUni(prog, "HS_RECV_TINT"), 1, recvT);
            pUni1f(pGetUni(prog, "LIGHT_OVERRIDE"), lightOv);
            pBindBuf(0x8892u, 0u); pBindBuf(0x8893u, 0u);
            pEnAtt(0u); pAttPtr(0u, 4, 0x1406u, 0u, 16, quad);
            pDrawArr(0x0005u, 0, 4);
            pFinish();
            out.resize(64u * 64u * 4u);
            pReadPix(0, 0, 64, 64, 0x1908u, 0x1401u, out.data());
        };

        std::vector<uint8_t> oldLit, newLit, oldGlow, newGlow, new1Tap, newNoShad;
        renderAnim(prOld, 0.0f, 1.0f, oldLit);
        renderAnim(prNew, 0.0f, 1.0f, newLit);
        renderAnim(prOld, 0.5f, 1.0f, oldGlow);
        renderAnim(prNew, 0.5f, 1.0f, newGlow);
        renderAnim(prNew, 0.0f, 0.65f, new1Tap);
        renderAnim(prNew, 0.0f, 0.0f, newNoShad);

        int maxDiff = 0;
        for (size_t i = 0; i < oldLit.size(); ++i) {
            maxDiff = (std::max)(maxDiff, abs(static_cast<int>(oldLit[i]) - static_cast<int>(newLit[i])));
            maxDiff = (std::max)(maxDiff, abs(static_cast<int>(oldGlow[i]) - static_cast<int>(newGlow[i])));
        }
        // Verify 1-tap shadow receive (w=0.65) genuinely darkens opaque lit pixel (x=56, y=32) vs unshadowed (w=0.0)!
        const size_t probeIdx = (32u * 64u + 56u) * 4u;
        const int gNoShad = newNoShad[probeIdx + 1];
        const int g1Tap   = new1Tap[probeIdx + 1];
        const bool oneTapActive = (g1Tap + 10 < gNoShad);

        pBindFbo(0x8D40u, 0u);
        pDelFbo(1, &fbo); pDelTex(1, &rtTex); pDelTex(1, &spTex); pDelTex(1, &shTex);
        pDelPr(prOld); pDelPr(prNew);
        const uint32_t errA = pGetErr();
        partAOk = okOld && okNew && (maxDiff == 0) && oneTapActive && (errA == 0u);
        printf("  Part A ANIM Early-Out + 1-Tap Recv Live RTX: maxDiff=%d 1TapG=%d(unshad=%d) err=0x%X -> %s\n",
               maxDiff, g1Tap, gNoShad, errA, partAOk ? "PASS" : "FAIL");
    }

    // -------------------------------------------------------------------------
    // Part B: Live RTX 4060 SpriteBatcher 64-Consecutive-Stencil-Flush +
    //         Interleaved Unbatched Draw + FLOAT_PARAMS.z Ocean Bobbing Parity!
    // -------------------------------------------------------------------------
    bool partBOk = false;
    {
        static constexpr const char* kAnimVs =
            "uniform mat4 MatrixP; uniform mat4 MatrixV; uniform mat4 MatrixW;\n"
            "uniform vec3 FLOAT_PARAMS; uniform vec4 TIMEPARAMS;\n"
            "attribute vec4 POS2D_UV; varying vec3 PS_TEXCOORD; varying vec3 PS_POS;\n"
            "void main() {\n#if defined(SKINNED)\n\tvec4 world_pos=vec4(0.0); vec3 TEXCOORD0=vec3(0.0);\n#else\n"
            "\tvec3 POSITION = vec3(POS2D_UV.xy, 0.0);\n"
            "\tfloat samplerIndex = floor(POS2D_UV.z/2.0);\n"
            "\tvec3 TEXCOORD0 = vec3(POS2D_UV.z - 2.0*samplerIndex, POS2D_UV.w, samplerIndex);\n"
            "\tvec3 object_pos = POSITION.xyz;\n"
            "\tvec4 world_pos = MatrixW * vec4( object_pos, 1.0 );\n"
            "\tif (FLOAT_PARAMS.z > 0.0) { world_pos.y += sin(MatrixW[3][0] + MatrixW[3][2] + TIMEPARAMS.x * 3.0) * 0.025; }\n"
            "\tmat4 mtxPV = MatrixP * MatrixV;\n"
            "\tgl_Position = mtxPV * world_pos;\n#endif\n"
            "\tPS_TEXCOORD = TEXCOORD0; PS_POS = world_pos.xyz;\n}\n";
        static constexpr const char* kAnimPs =
            "#if defined( GL_ES )\nprecision mediump float;\n#endif\n"
            "uniform sampler2D SAMPLER[6]; uniform vec4 LIGHTMAP_WORLD_EXTENTS;\n"
            "uniform mat4 COLOUR_XFORM; uniform vec3 PARAMS; uniform vec3 FLOAT_PARAMS;\n"
            "uniform vec4 OCEAN_BLEND_PARAMS;\n"
            "varying vec3 PS_POS; varying vec3 PS_TEXCOORD;\n"
            "#define ALPHA_TEST PARAMS.x\n#define LIGHT_OVERRIDE PARAMS.y\n#define BLOOM_TOGGLE PARAMS.z\n"
            "vec3 CalculateLightingContribution() { return vec3(1.0, 1.0, 1.0); }\n"
            "void main() {\n\tvec4 colour;\n#if defined( TRIPLE_ATLAS )\n"
            "\tcolour.rgba = texture2D( SAMPLER[0], PS_TEXCOORD.xy );\n#else\n"
            "\tcolour.rgba = texture2D( SAMPLER[0], PS_TEXCOORD.xy );\n#endif\n"
            "\tif (BLOOM_TOGGLE == 1.0) { gl_FragColor.rgba = vec4(0.0, 0.0, 0.0, colour.a); return; }\n"
            "\tif(FLOAT_PARAMS.y > 0.0) { if(PS_POS.y < FLOAT_PARAMS.x) { discard; } }\n"
            "\tif (ALPHA_TEST > 0.0) { if (colour.a >= ALPHA_TEST) { gl_FragColor = colour.rgba; } else { discard; } } else {\n"
            "\t\tgl_FragColor.rgba = colour.rgba * COLOUR_XFORM;\n"
            "\t\tvec3 light = CalculateLightingContribution();\n"
            "\t\tgl_FragColor.rgb *= max( light.rgb, vec3( LIGHT_OVERRIDE, LIGHT_OVERRIDE, LIGHT_OVERRIDE ) );\n"
            "\t}\n}\n";

        std::string gVs, gFs;
        const char* tag = nullptr;
        const bool txOk = hs_sprite_batch::TransformAnimBatchShaderSource(kAnimVs, gVs, tag, true, true) &&
                          hs_sprite_batch::TransformAnimBatchShaderSource(kAnimPs, gFs, tag, true, true);
        uint32_t vs = pCreateSh(0x8B31u), fs = pCreateSh(0x8B30u);
        const char* vp = gVs.c_str(); int32_t vl = static_cast<int32_t>(gVs.size());
        const char* fp = gFs.c_str(); int32_t fl = static_cast<int32_t>(gFs.size());
        pShSrc(vs, 1, &vp, &vl); pCompSh(vs);
        pShSrc(fs, 1, &fp, &fl); pCompSh(fs);
        uint32_t prog = pCreatePr();
        pAttachSh(prog, vs); pAttachSh(prog, fs); pBindAtt(prog, 0u, "POS2D_UV"); pLinkPr(prog);
        pDelSh(vs); pDelSh(fs);
        int32_t linkOk = 0; pGetPrIv(prog, 0x8B82u, &linkOk);

        // Build 256x64 FBO with RGBA8 color + DEPTH24_STENCIL8 stencil attachment!
        uint32_t fbo = 0u, rtTex = 0u, dsRbo = 0u, spTexA = 0u, spTexB = 0u;
        pGenFbo(1, &fbo); pGenTex(1, &rtTex); pGenRbo(1, &dsRbo);
        pGenTex(1, &spTexA); pGenTex(1, &spTexB);
        pActTex(0x84C0u); pBindTex(0x0DE1u, rtTex);
        pTexPar(0x0DE1u, 0x2801u, 0x2600); pTexPar(0x0DE1u, 0x2800u, 0x2600);
        pTexImg(0x0DE1u, 0, 0x8058, 256, 64, 0, 0x1908u, 0x1401u, nullptr);
        pBindRbo(0x8D41u, dsRbo);
        pRboStor(0x8D41u, 0x88F0u /*GL_DEPTH24_STENCIL8*/, 256, 64);
        pBindFbo(0x8D40u, fbo);
        pFboTex(0x8D40u, 0x8CE0u, 0x0DE1u, rtTex, 0);
        pFboRbo(0x8D40u, 0x821Au /*GL_DEPTH_STENCIL_ATTACHMENT*/, 0x8D41u, dsRbo);

        auto makeSolidTex = [&](uint32_t t, uint8_t r, uint8_t g, uint8_t b) {
            uint8_t px[4 * 4 * 4];
            for (int i = 0; i < 16; ++i) { px[i*4+0] = r; px[i*4+1] = g; px[i*4+2] = b; px[i*4+3] = 255u; }
            pActTex(0x84C0u); pBindTex(0x0DE1u, t);
            pTexPar(0x0DE1u, 0x2801u, 0x2600); pTexPar(0x0DE1u, 0x2800u, 0x2600);
            pTexImg(0x0DE1u, 0, 0x8058, 4, 4, 0, 0x1908u, 0x1401u, px);
        };
        makeSolidTex(spTexA, 210u, 150u, 90u);
        makeSolidTex(spTexB, 90u, 180u, 220u);

        hs_sprite_batch::GlBatchOps bops{};
        bops.glGenBuffers               = pGenBuf;
        bops.glDeleteBuffers            = pDelBuf;
        bops.glBindBuffer               = pBindBuf;
        bops.glBufferData               = pBufData;
        bops.glBufferSubData            = reinterpret_cast<decltype(bops.glBufferSubData)>(opt.glBufferSubData);
        bops.glMapBufferRange           = reinterpret_cast<decltype(bops.glMapBufferRange)>(opt.glMapBufferRange);
        bops.glUnmapBuffer              = reinterpret_cast<decltype(bops.glUnmapBuffer)>(opt.glUnmapBuffer);
        bops.glBufferStorage            = reinterpret_cast<decltype(bops.glBufferStorage)>(opt.glBufferStorage ? opt.glBufferStorage : opt.glBufferStorageExt);
        bops.glFenceSync                = reinterpret_cast<decltype(bops.glFenceSync)>(opt.glFenceSync);
        bops.glClientWaitSync           = reinterpret_cast<decltype(bops.glClientWaitSync)>(opt.glClientWaitSync);
        bops.glDeleteSync               = reinterpret_cast<decltype(bops.glDeleteSync)>(opt.glDeleteSync);
        bops.glActiveTexture            = pActTex;
        bops.glBindTexture              = pBindTex;
        bops.glUniform1f                = pUni1f;
        bops.glUniform1i                = pUni1i;
        bops.glUniform3fv               = pUni3fv;
        bops.glUniform4fv               = pUni4fv;
        bops.glUniformMatrix4fv         = pUniMat4;
        bops.glVertexAttribPointer      = pAttPtr;
        bops.glEnableVertexAttribArray  = pEnAtt;
        bops.glDisableVertexAttribArray = pDisAtt;
        bops.glDrawElements             = pDrawEl;
        bops.glDrawElementsBaseVertex   = reinterpret_cast<decltype(bops.glDrawElementsBaseVertex)>(opt.glDrawElementsBaseVertex);
        bops.glBindBufferBase           = reinterpret_cast<decltype(bops.glBindBufferBase)>(opt.glBindBufferBase);
        bops.glGetIntegerv              = pGetInt;
        bops.glGetBufferParameteriv     = reinterpret_cast<decltype(bops.glGetBufferParameteriv)>(opt.glGetBufferParameteriv);
        bops.glGetTextureHandleARB      = reinterpret_cast<decltype(bops.glGetTextureHandleARB)>(opt.glGetTextureHandleARB);
        bops.glMakeTextureHandleResidentARB    = reinterpret_cast<decltype(bops.glMakeTextureHandleResidentARB)>(opt.glMakeTextureHandleResidentARB);
        bops.glMakeTextureHandleNonResidentARB = reinterpret_cast<decltype(bops.glMakeTextureHandleNonResidentARB)>(opt.glMakeTextureHandleNonResidentARB);

        hs_sprite_batch::BatchProgInfo binfo{};
        binfo.checked = true; binfo.eligible = true; binfo.hasGpuDriven = true;
        binfo.locBatchActive = pGetUni(prog, "HS_BATCH_ACTIVE");
        binfo.locMatrixW     = pGetUni(prog, "MatrixW");
        binfo.locColourXform = pGetUni(prog, "COLOUR_XFORM");
        binfo.locFloatParams = pGetUni(prog, "FLOAT_PARAMS");
        binfo.locParams      = pGetUni(prog, "PARAMS");
        binfo.locOceanBlend  = pGetUni(prog, "OCEAN_BLEND_PARAMS");
        binfo.locSampler0    = pGetUni(prog, "SAMPLER[0]");
        binfo.locSampler1    = pGetUni(prog, "SAMPLER[1]");
        binfo.locPos2dUv     = pGetAtt(prog, "POS2D_UV");
        binfo.locBatchWpos   = pGetAtt(prog, "HS_BATCH_WPOS");
        binfo.locBatchCMult  = pGetAtt(prog, "HS_BATCH_CMULT");
        binfo.locBatchCAdd   = pGetAtt(prog, "HS_BATCH_CADD");
        binfo.hasParamsAbsorption = binfo.hasGpuDriven && !binfo.isBloomPass && (binfo.locParams >= 0);
        binfo.progMaxTexSlots = 16u;
        for (uint32_t s = 0u; s < hs_sprite_batch::kMaxBatchTexSlots; ++s) {
            char nm[32]{}; snprintf(nm, sizeof(nm), "HS_BATCH_TEX%u", s);
            binfo.locBatchTex[s] = pGetUni(prog, nm);
        }

        const float idMtx[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
        const int32_t sampUnits[6] = { 0,1,2,3,4,5 };
        const float timeP[4] = { 1.25f, 0.0f, 0.0f, 0.0f };
        const float qv[16] = { -0.014f,-0.5f,0.2f,0.2f, 0.014f,-0.5f,0.8f,0.2f, 0.014f,0.5f,0.8f,0.8f, -0.014f,0.5f,0.2f,0.8f };
        const uint16_t qi[6] = { 0,1,2, 0,2,3 };
        uint32_t hostVbo = 0u, hostIbo = 0u;
        pGenBuf(1, &hostVbo); pGenBuf(1, &hostIbo);
        pBindBuf(0x8892u, hostVbo); pBufData(0x8892u, sizeof(qv), qv, 0x88E4u);
        pBindBuf(0x8893u, hostIbo); pBufData(0x8893u, sizeof(qi), qi, 0x88E4u);

        auto setupPass = [&]() {
            pBindFbo(0x8D40u, fbo); pVp(0, 0, 256, 64);
            pDis(0x0BE2u);
            pClrCol(0.08f, 0.10f, 0.14f, 1.0f);
            pClrStencil(0);
            pStMask(0xFFu);
            pClr(0x4000u | 0x0400u /*GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT*/);
            pEn(0x0B90u /*GL_STENCIL_TEST*/);
            pUsePr(prog);
            if (pUni1iv && pGetUni(prog, "SAMPLER") >= 0) pUni1iv(pGetUni(prog, "SAMPLER"), 6, sampUnits);
            pUniMat4(pGetUni(prog, "MatrixP"), 1, 0, idMtx);
            pUniMat4(pGetUni(prog, "MatrixV"), 1, 0, idMtx);
            pUniMat4(binfo.locColourXform, 1, 0, idMtx);
            if (pGetUni(prog, "TIMEPARAMS") >= 0) pUni4fv(pGetUni(prog, "TIMEPARAMS"), 1, timeP);
            const float z3[3] = { 0,0,0 }, z4[4] = { 0,0,0,0 };
            if (binfo.locFloatParams >= 0) pUni3fv(binfo.locFloatParams, 1, z3);
            if (binfo.locParams >= 0) pUni3fv(binfo.locParams, 1, z3);
            if (binfo.locOceanBlend >= 0) pUni4fv(binfo.locOceanBlend, 1, z4);
            pUni1f(binfo.locBatchActive, 0.0f);
            pBindBuf(0x8892u, hostVbo); pBindBuf(0x8893u, hostIbo);
            pEnAtt(static_cast<uint32_t>(binfo.locPos2dUv));
            pAttPtr(static_cast<uint32_t>(binfo.locPos2dUv), 4, 0x1406u, 0, 16, nullptr);
        };

        auto makeMtx = [](int idx) {
            const float tx = -0.95f + static_cast<float>(idx) * (1.90f / 64.0f);
            return std::vector<float>{ 1,0,0,0, 0,1,0,0, 0,0,1,0, tx,0,0,1 };
        };
        const float midMtx[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0.0f,0.12f,0,1 };
        const float fpBob[3]   = { -0.05f, 1.0f, 1.0f };
        const float fpZero[3]  = {  0.0f,  0.0f, 0.0f };

        // 1. Reference unbatched draw: 64 stencil-stepped quads + 1 interleaved unbatched quad at #32
        std::vector<uint8_t> pxRef(256u * 64u * 4u), pxBat(256u * 64u * 4u);
        setupPass();
        for (int i = 0; i < 64; ++i) {
            if (i == 32) {
                pStFunc(0x0207u /*GL_ALWAYS*/, 7, 0xFFu);
                pStOp(0x1E00u, 0x1E00u, 0x1E01u);
                pActTex(0x84C0u); pBindTex(0x0DE1u, spTexB);
                pUniMat4(binfo.locMatrixW, 1, 0, midMtx);
                pUni3fv(binfo.locFloatParams, 1, fpZero);
                pDrawEl(0x0004u, 6, 0x1403u, nullptr);
            }
            pStFunc(0x0207u /*GL_ALWAYS*/, (i & 15) + 1, 0xFFu);
            pStOp(0x1E00u, 0x1E00u, 0x1E01u);
            auto mw = makeMtx(i);
            pActTex(0x84C0u); pBindTex(0x0DE1u, (i & 1) ? spTexB : spTexA);
            pUniMat4(binfo.locMatrixW, 1, 0, mw.data());
            pUni3fv(binfo.locFloatParams, 1, (i & 1) ? fpBob : fpZero);
            pDrawEl(0x0004u, 6, 0x1403u, nullptr);
        }
        pFinish();
        pReadPix(0, 0, 256, 64, 0x1908u, 0x1401u, pxRef.data());

        // 2. Batched GPU-Driven 64 consecutive stencil flushes + interleaved unbatched draw at #32!
        setupPass();
        hs_sprite_batch::SpriteBatcher sb;
        sb.SetOps(bops);
        sb.SetPersistentRingPreferred(true);
        sb.SetGpuDrivenPreferred(true);
        sb.SetDeferHostRestore(false);
        sb.SetEnabled(true);
        sb.OnUseProgram(prog, &binfo);
        sb.OnEnableDisableVertexAttribArray(static_cast<uint32_t>(binfo.locPos2dUv), true);
        sb.OnActiveTexture(0x84C0u);
        sb.OnBindBuffer(0x8892u, hostVbo);
        sb.OnBindBuffer(0x8893u, hostIbo);
        sb.OnVertexAttribPointer(static_cast<uint32_t>(binfo.locPos2dUv), 4, 0x1406u, 0, 16, nullptr);

        bool allBatchOk = true;
        for (int i = 0; i < 64; ++i) {
            if (i == 32) {
                // Interleaved unbatched draw in the middle!
                sb.PrepareForUnbatchedDraw();
                pStFunc(0x0207u, 7, 0xFFu);
                pStOp(0x1E00u, 0x1E00u, 0x1E01u);
                pActTex(0x84C0u); pBindTex(0x0DE1u, spTexB);
                sb.OnBindTexture(0x0DE1u, spTexB);
                pUniMat4(binfo.locMatrixW, 1, 0, midMtx);
                pUni3fv(binfo.locFloatParams, 1, fpZero);
                sb.OnUniform3fv(binfo.locFloatParams, 1, fpZero, false);
                pDrawEl(0x0004u, 6, 0x1403u, nullptr);
            }
            // Stencil state change flushes batcher every single iteration (64 consecutive flushes!)
            if (sb.HasPending()) sb.Flush(hs_sprite_batch::FlushReason::State);
            pStFunc(0x0207u, (i & 15) + 1, 0xFFu);
            pStOp(0x1E00u, 0x1E00u, 0x1E01u);
            auto mw = makeMtx(i);
            const uint32_t tid = (i & 1) ? spTexB : spTexA;
            sb.OnBindTexture(0x0DE1u, tid);
            const float* fp = (i & 1) ? fpBob : fpZero;
            if (!sb.TryAbsorbFloatParams(binfo.locFloatParams, 1, fp)) {
                pUni3fv(binfo.locFloatParams, 1, fp);
                sb.OnUniform3fv(binfo.locFloatParams, 1, fp, false);
            }
            sb.TryAbsorbMatrixW(binfo.locMatrixW, 1, 0, mw.data());
            sb.TryAbsorbColourXform(binfo.locColourXform, 1, 0, idMtx);
            if (!sb.TryBatchDrawElements(0x0004u, 6, 0x1403u, nullptr)) allBatchOk = false;
        }
        sb.Flush(hs_sprite_batch::FlushReason::End);
        pFinish();
        pReadPix(0, 0, 256, 64, 0x1908u, 0x1401u, pxBat.data());
        const int64_t totalFlushes = sb.GetStats().flushes;
        const int64_t uni3Flushes  = sb.GetStats().flReason[static_cast<size_t>(hs_sprite_batch::FlushReason::Uni3)];
        sb.ReleaseGpuResources();

        int maxDiff = 0;
        for (size_t i = 0; i < pxRef.size(); ++i) {
            maxDiff = (std::max)(maxDiff, abs(static_cast<int>(pxRef[i]) - static_cast<int>(pxBat[i])));
        }
        pDis(0x0B90u);
        pBindFbo(0x8D40u, 0u);
        pDelBuf(1, &hostVbo); pDelBuf(1, &hostIbo);
        pDelRbo(1, &dsRbo); pDelFbo(1, &fbo);
        pDelTex(1, &rtTex); pDelTex(1, &spTexA); pDelTex(1, &spTexB); pDelPr(prog);
        const uint32_t errB = pGetErr();
        partBOk = txOk && linkOk && allBatchOk && (totalFlushes == 64) && (uni3Flushes == 0) &&
                  (maxDiff == 0) && (errB == 0u);
        printf("  Part B 64-Stencil-Flush + Unbatched Mid + FloatParamsZ Live RTX: flushes=%lld uni3=%lld maxDiff=%d err=0x%X -> %s\n",
               static_cast<long long>(totalFlushes), static_cast<long long>(uni3Flushes),
               maxDiff, errB, partBOk ? "PASS" : "FAIL");
    }

    // -------------------------------------------------------------------------
    // Part C: Live RTX 4060 GpuClarityEngine Full 3->2->1->0->1->2->3 Ladder
    //         with strictly ZERO GL Texture/FBO Reallocations & err == 0x0!
    // -------------------------------------------------------------------------
    bool partCOk = false;
    {
        struct ClaritySpy4 {
            GenTex_t realGenTex = nullptr;
            DelTex_t realDelTex = nullptr;
            hs_gpu_clarity::GlComputeOps::TexStorage2D_t realTexStor = nullptr;
            GenFbo_t realGenFbo = nullptr;
            DelFbo_t realDelFbo = nullptr;
            uint32_t genTex = 0u, delTex = 0u, texStor = 0u, genFbo = 0u, delFbo = 0u;
        };
        static ClaritySpy4* s_c4 = nullptr;
        ClaritySpy4 cs{};
        cs.realGenTex  = pGenTex;
        cs.realDelTex  = pDelTex;
        cs.realTexStor = reinterpret_cast<decltype(cs.realTexStor)>(opt.glTexStorage2D);
        cs.realGenFbo  = pGenFbo;
        cs.realDelFbo  = pDelFbo;
        s_c4 = &cs;

        hs_gpu_clarity::GlComputeOps cops{};
        cops.glGenFramebuffers        = [](int32_t n, uint32_t* p) WINAPI { s_c4->genFbo += static_cast<uint32_t>(n); s_c4->realGenFbo(n, p); };
        cops.glDeleteFramebuffers     = [](int32_t n, const uint32_t* p) WINAPI { s_c4->delFbo += static_cast<uint32_t>(n); s_c4->realDelFbo(n, p); };
        cops.glBindFramebuffer        = pBindFbo;
        cops.glFramebufferTexture2D   = pFboTex;
        cops.glCheckFramebufferStatus = reinterpret_cast<decltype(cops.glCheckFramebufferStatus)>(findP("glCheckFramebufferStatus"));
        cops.glBlitFramebuffer        = reinterpret_cast<decltype(cops.glBlitFramebuffer)>(opt.glBlitFramebuffer);
        cops.glGenTextures            = [](int32_t n, uint32_t* p) WINAPI { s_c4->genTex += static_cast<uint32_t>(n); s_c4->realGenTex(n, p); };
        cops.glDeleteTextures         = [](int32_t n, const uint32_t* p) WINAPI { s_c4->delTex += static_cast<uint32_t>(n); s_c4->realDelTex(n, p); };
        cops.glActiveTexture          = pActTex;
        cops.glBindTexture            = pBindTex;
        cops.glTexStorage2D           = [](uint32_t t, int32_t l, uint32_t f, int32_t w, int32_t h) WINAPI { ++s_c4->texStor; s_c4->realTexStor(t, l, f, w, h); };
        cops.glTexParameteri          = pTexPar;
        cops.glCreateShader           = pCreateSh;
        cops.glShaderSource           = pShSrc;
        cops.glCompileShader          = pCompSh;
        cops.glGetShaderiv            = pGetShIv;
        cops.glGetShaderInfoLog       = reinterpret_cast<decltype(cops.glGetShaderInfoLog)>(findP("glGetShaderInfoLog"));
        cops.glDeleteShader           = pDelSh;
        cops.glCreateProgram          = pCreatePr;
        cops.glAttachShader           = pAttachSh;
        cops.glLinkProgram            = pLinkPr;
        cops.glGetProgramiv           = pGetPrIv;
        cops.glGetProgramInfoLog      = reinterpret_cast<decltype(cops.glGetProgramInfoLog)>(findP("glGetProgramInfoLog"));
        cops.glDeleteProgram          = pDelPr;
        cops.glUseProgram             = pUsePr;
        cops.glGetUniformLocation     = pGetUni;
        cops.glUniform4f              = reinterpret_cast<decltype(cops.glUniform4f)>(opt.glUniform4f);
        cops.glDispatchCompute        = reinterpret_cast<decltype(cops.glDispatchCompute)>(opt.glDispatchCompute);
        cops.glMemoryBarrier          = reinterpret_cast<decltype(cops.glMemoryBarrier)>(opt.glMemoryBarrier);
        cops.glBindImageTexture       = reinterpret_cast<decltype(cops.glBindImageTexture)>(opt.glBindImageTexture);
        cops.glGetIntegerv            = pGetInt;
        cops.glIsEnabled              = reinterpret_cast<decltype(cops.glIsEnabled)>(opt.glIsEnabled);
        cops.glEnable                 = pEn;
        cops.glDisable                = pDis;
        cops.logFn                    = &TestLog;

        const int32_t oddW = 641, oddH = 361;
        uint32_t clFbo = 0u, clTex = 0u;
        pGenFbo(1, &clFbo); pGenTex(1, &clTex);
        pActTex(0x84C0u); pBindTex(0x0DE1u, clTex);
        pTexPar(0x0DE1u, 0x2801u, 0x2601); pTexPar(0x0DE1u, 0x2800u, 0x2601);
        std::vector<uint8_t> img(static_cast<size_t>(oddW) * static_cast<size_t>(oddH) * 4u, 148u);
        pTexImg(0x0DE1u, 0, 0x8058, oddW, oddH, 0, 0x1908u, 0x1401u, img.data());
        pBindFbo(0x8D40u, clFbo); pFboTex(0x8D40u, 0x8CE0u, 0x0DE1u, clTex, 0);

        hs_gpu_clarity::GpuClarityEngine cl;
        cl.SetOps(cops, true);
        hs_gpu_clarity::KnownGlState ks{};
        ks.drawFbo = static_cast<int32_t>(clFbo); ks.readFbo = static_cast<int32_t>(clFbo);
        ks.curProgram = 0; ks.activeTexEnum = static_cast<int32_t>(hs_gpu_clarity::kGlTexture0);
        ks.boundTex0 = static_cast<int32_t>(clTex); ks.scissorEnabled = 0; ks.sampleTimer = false;

        // Warmup Grade 3, then zero alloc counters
        bool ok = cl.ProcessFrame(clFbo, oddW, oddH, { 3, 1.0f, true, 3.0f, 5.5f }, &ks);
        cs.genTex = cs.delTex = cs.texStor = cs.genFbo = cs.delFbo = 0u;

        // 3 -> 2 (45 frames @ 6.2ms)
        for (int f = 0; f < 45 && ok; ++f) ok = cl.ProcessFrame(clFbo, oddW, oddH, { 3, 1.0f, true, 6.2f, 5.5f }, &ks);
        const int g2 = cl.EffectiveGrade();
        // 2 -> 1 (45 frames @ 7.0ms)
        for (int f = 0; f < 45 && ok; ++f) ok = cl.ProcessFrame(clFbo, oddW, oddH, { 3, 1.0f, true, 7.0f, 5.5f }, &ks);
        const int g1 = cl.EffectiveGrade();
        // 1 -> 0 (60 frames @ 8.0ms; 60th frame returns false!)
        for (int f = 0; f < 59 && ok; ++f) ok = cl.ProcessFrame(clFbo, oddW, oddH, { 3, 1.0f, true, 8.0f, 5.5f }, &ks);
        const bool ranAt0 = cl.ProcessFrame(clFbo, oddW, oddH, { 3, 1.0f, true, 8.0f, 5.5f }, &ks);
        const int g0 = cl.EffectiveGrade();
        const bool bypassAt0 = !ranAt0 && cl.IsAdaptiveBypassed();
        // 0 -> 1 (120 frames @ 5.5ms; 120th frame returns true!)
        for (int f = 0; f < 119; ++f) (void)cl.ProcessFrame(clFbo, oddW, oddH, { 3, 1.0f, true, 5.5f, 5.5f }, &ks);
        ok = ok && cl.ProcessFrame(clFbo, oddW, oddH, { 3, 1.0f, true, 5.5f, 5.5f }, &ks);
        const int g1Up = cl.EffectiveGrade();
        // 1 -> 2 (120 frames @ 4.5ms)
        for (int f = 0; f < 120 && ok; ++f) ok = cl.ProcessFrame(clFbo, oddW, oddH, { 3, 1.0f, true, 4.5f, 5.5f }, &ks);
        const int g2Up = cl.EffectiveGrade();
        // 2 -> 3 (120 frames @ 3.5ms)
        for (int f = 0; f < 120 && ok; ++f) ok = cl.ProcessFrame(clFbo, oddW, oddH, { 3, 1.0f, true, 3.5f, 5.5f }, &ks);
        const int g3Up = cl.EffectiveGrade();
        const uint64_t totalSteps = cl.AdaptiveGradeSteps();
        const uint32_t reallocTotal = cs.genTex + cs.delTex + cs.texStor + cs.genFbo + cs.delFbo;
        pFinish();

        cl.OnContextReset(true);
        pBindFbo(0x8D40u, 0u); pDelFbo(1, &clFbo); pDelTex(1, &clTex);
        const uint32_t errC = pGetErr();
        s_c4 = nullptr;
        partCOk = ok && (g2 == 2) && (g1 == 1) && (g0 == 0) && bypassAt0 &&
                  (g1Up == 1) && (g2Up == 2) && (g3Up == 3) && (totalSteps == 6ULL) &&
                  (reallocTotal == 0u) && (errC == 0u);
        printf("  Part C GpuClarity Full 3->2->1->0->1->2->3 Live RTX: path=%d->%d->%d->%d->%d->%d steps=%llu reallocs=%u err=0x%X -> %s\n",
               g2, g1, g0, g1Up, g2Up, g3Up, static_cast<unsigned long long>(totalSteps),
               reallocTotal, errC, partCOk ? "PASS" : "FAIL");
    }

    rt.pWglMakeCurrent(nullptr, nullptr);
    rt.pWglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return partAOk && partBOk && partCOk;
}

int main() {
    bool okGl    = TestNativeOpenGl();
    bool okAngle = TestAngleD3d9();
    bool okMock  = mock_gl::TestRound2MockCases();
    bool okSave  = TestAsyncSaveVisibility();
    bool okAba   = TestDualContextABAReuse();
    bool okM124  = TestM1M2M4UnitSuite();
    bool okFlip  = TestDxgiFlipLiveGpu();
    bool okAzdo  = TestSpriteBatchAzdoLiveGpu();
    bool okGpuDriven = TestGpuDrivenBindlessAndShadowParityLiveGpu();
    bool okPboTex = TestPboTexRingLiveGpu();
    bool okPhase2 = TestPhase2LiveGpuSuite();
    bool okPhase3 = TestPhase3LiveGpuSuite();
    bool okPhase4 = TestPhase4LiveGpuSuite();
    if (okGl && okAngle && okMock && okSave && okAba && okM124 && okFlip && okAzdo && okGpuDriven && okPboTex && okPhase2 && okPhase3 && okPhase4) {
        printf("\n[PASS] ALL 13 DUAL-BACKEND GPU (SHADOW + WATER_REFL + DXGI-FLIP + AZDO-RING + GPU-DRIVEN-BINDLESS + PBO-TEX-RING + PHASE2 + PHASE3 + PHASE4-ANIM-EARLYOUT/1TAP-RECV/64-STENCIL-FLUSH/CLARITY-3-2-1-0-3) + MOCK-GL + CONCURRENT-SAVE + DUAL-CTX + M1/M2/M4 TESTS VERIFIED!\n");
        return 0;
    }
    printf("\n[FAIL] gl=%d angle=%d mock=%d save=%d aba=%d m124=%d flip=%d azdo=%d gpuDriven=%d pboTex=%d phase2=%d phase3=%d phase4=%d\n",
           okGl ? 1 : 0, okAngle ? 1 : 0, okMock ? 1 : 0, okSave ? 1 : 0, okAba ? 1 : 0, okM124 ? 1 : 0, okFlip ? 1 : 0, okAzdo ? 1 : 0, okGpuDriven ? 1 : 0, okPboTex ? 1 : 0, okPhase2 ? 1 : 0, okPhase3 ? 1 : 0, okPhase4 ? 1 : 0);
    return 1;
}
