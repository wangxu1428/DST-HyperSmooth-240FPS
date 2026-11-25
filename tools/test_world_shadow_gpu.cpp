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

    auto runCase = [&](float dirX, float dirZ, bool addOverlap, std::vector<uint8_t>& outRgba) -> bool {
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
        eng.OnEndFrame();
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

int main() {
    bool okGl    = TestNativeOpenGl();
    bool okAngle = TestAngleD3d9();
    bool okMock  = mock_gl::TestRound2MockCases();
    bool okSave  = TestAsyncSaveVisibility();
    bool okAba   = TestDualContextABAReuse();
    bool okM124  = TestM1M2M4UnitSuite();
    bool okFlip  = TestDxgiFlipLiveGpu();
    if (okGl && okAngle && okMock && okSave && okAba && okM124 && okFlip) {
        printf("\n[PASS] ALL DUAL-BACKEND GPU (SHADOW + WATER_REFL + DXGI-FLIP) + MOCK-GL + CONCURRENT-SAVE + DUAL-CTX + M1/M2/M4 TESTS VERIFIED!\n");
        return 0;
    }
    printf("\n[FAIL] gl=%d angle=%d mock=%d save=%d aba=%d m124=%d flip=%d\n",
           okGl ? 1 : 0, okAngle ? 1 : 0, okMock ? 1 : 0, okSave ? 1 : 0, okAba ? 1 : 0, okM124 ? 1 : 0, okFlip ? 1 : 0);
    return 1;
}
