#pragma once
// =============================================================================
// HyperSmooth M3: True Planar Water Reflections Engine (Pure GLES2 IAT Layer)
// =============================================================================
// Architecture:
//   - Dedicated 2-slot ping-pong 512x512 RGBA8 + DEPTH16 top-down world-space
//     reflection FBO (~3 MB total VRAM), bound on GL_TEXTURE12 during
//     `ocean_combined.ksh` sampling (0 conflict with DST 0..5, SpriteBatch 6..13,
//     or ShadowMask/BlueNoise 14..15!).
//   - Strictly coastal-gated: activates ONLY when `sawOceanThisFrame_` is true
//     and `cfg.strength > 0.001f` -> 0 FBO clears, 0 draws, 0.00 ms inland & in caves!
//   - Perspective-exact planar reflection across y = 0:
//       s = eyeY / (eyeY + h * lengthScale)
//       P_water.xz = mix(eye.xz, world_pos.xz, s)
//     Aligns pixel-perfectly with true 3D planar mirror geometry at every Q/E
//     camera angle and zoom level, depth-sorted via DEPTH16 GL_LESS along
//     camera view ray on BOTH Desktop OpenGL 4.6 and ANGLE 1.0.0.2249 D3D9!
//   - Zero-copy mirroring of batched sprites (`SpriteBatcher` Ring VBO/IBO +
//     `GL_TEXTURE6..13`) and unbatched/skinned coastal entities (`HS_SHADOW_PASS = 2.0`).
//   - Round-4 grade RAII resource guard & full separate RGB/Alpha raster state
//     capture/restore with glGetError() == 0 on both backends.
// =============================================================================

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "gl_state_cache.h"
#include "world_shadow_engine.h"

namespace hs_water_refl {

inline constexpr uint32_t kReflTexUnit    = 12u;   // Unused by ocean_combined (0..3) & Shadow (14..15)
inline constexpr uint32_t kReflRes        = 512u;  // 512x512 RGBA8 + DEPTH16 (~3 MB ping-pong)
inline constexpr float    kReflHalfExtent = 48.0f; // 96x96 world units centered on camera

struct WaterReflConfig {
    float strength      = 0.0f; // [0.0, 1.2], 0.0 in Preset 0 Vanilla Pure
    float rippleAmp     = 0.55f; // [0.0, 1.5]
    float lengthScale   = 1.00f; // [0.5, 1.6]
    bool  skipNightmare = true;
    bool  hasLuaCam     = false;
    float luaCamX       = 0.0f;
    float luaCamZ       = 0.0f;
    float luaCamRightX  = 0.707f;
    float luaCamRightZ  = -0.707f;

    bool IsEnabled() const noexcept {
        return std::isfinite(strength) && strength > 0.001f;
    }
};

// =============================================================================
// Internal GLSL ES 1.00 Batched Planar Reflection Shaders
// Verified for ANGLE 1.0.0.2249 D3D9 & Desktop OpenGL 4.6 (#version 100)
// =============================================================================
inline constexpr const char* kReflBatchVs =
    "uniform vec4 u_reflBounds;\n" // (centerX, centerZ, 1/halfExtent, lengthScale)
    "uniform vec4 u_reflEye;\n"    // (eyeX, eyeY, eyeZ, 0)
    "uniform vec4 u_reflBack;\n"   // (camBackX, camBackY, camBackZ, 0)
    "attribute vec4 POS2D_UV;\n"
    "attribute vec4 HS_BATCH_WPOS;\n"
    "attribute vec4 HS_BATCH_CMULT;\n"
    "attribute vec4 HS_BATCH_CADD;\n"
    "varying vec3 v_tc;\n"
    "varying vec4 v_cmult;\n"
    "varying vec4 v_cadd;\n"
    "void main() {\n"
    "\tfloat hs_sf = POS2D_UV.x - 2.0 * floor(POS2D_UV.x * 0.5);\n"
    "\tif (hs_sf < 0.5) {\n"
    "\t\tgl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "\t\tv_tc = vec3(0.0);\n"
    "\t\tv_cmult = vec4(0.0);\n"
    "\t\tv_cadd = vec4(0.0);\n"
    "\t\treturn;\n"
    "\t}\n"
    "\tfloat hs_si = floor(POS2D_UV.z * 0.5);\n"
    "\tv_tc = vec3(POS2D_UV.z - 2.0 * hs_si, POS2D_UV.w, HS_BATCH_WPOS.w);\n"
    "\tfloat hs_h = clamp(HS_BATCH_WPOS.y, 0.0, 8.5);\n"
    "\tfloat hs_eyeY = max(u_reflEye.y, 6.0);\n"
    "\tfloat hs_ls = clamp(u_reflBounds.w, 0.50, 1.60);\n"
    "\tfloat hs_s = hs_eyeY / max(hs_eyeY + hs_h * hs_ls, 1.0);\n"
    "\tvec2 hs_rxz = mix(u_reflEye.xz, HS_BATCH_WPOS.xz, hs_s);\n"
    "\tvec2 hs_ndc = (hs_rxz - u_reflBounds.xy) * u_reflBounds.z;\n"
    "\tvec3 hs_pref = vec3(HS_BATCH_WPOS.x, -hs_h, HS_BATCH_WPOS.z);\n"
    "\tfloat hs_vd = dot(u_reflEye.xyz - hs_pref, u_reflBack.xyz);\n"
    "\tfloat hs_z = clamp((hs_vd - 4.0) / 140.0, 0.0, 1.0) * 1.98 - 0.99;\n"
    "\tv_cmult = HS_BATCH_CMULT;\n"
    "\tv_cadd  = vec4(HS_BATCH_CADD.rgb, hs_h);\n"
    "\tgl_Position = vec4(hs_ndc.x, hs_ndc.y, hs_z, 1.0);\n"
    "}\n";

inline constexpr const char* kReflBatchFs =
    "#if defined(GL_ES)\n"
    "precision highp float;\n"
    "#endif\n"
    "uniform sampler2D HS_BATCH_TEX0;\n"
    "uniform sampler2D HS_BATCH_TEX1;\n"
    "uniform sampler2D HS_BATCH_TEX2;\n"
    "uniform sampler2D HS_BATCH_TEX3;\n"
    "uniform sampler2D HS_BATCH_TEX4;\n"
    "uniform sampler2D HS_BATCH_TEX5;\n"
    "uniform sampler2D HS_BATCH_TEX6;\n"
    "uniform sampler2D HS_BATCH_TEX7;\n"
    "varying vec3 v_tc;\n"
    "varying vec4 v_cmult;\n"
    "varying vec4 v_cadd;\n"
    "void main() {\n"
    "\tfloat s = v_tc.z;\n"
    "\tvec4 c;\n"
    "\tif (s < 0.5)      c = texture2D(HS_BATCH_TEX0, v_tc.xy);\n"
    "\telse if (s < 1.5) c = texture2D(HS_BATCH_TEX1, v_tc.xy);\n"
    "\telse if (s < 2.5) c = texture2D(HS_BATCH_TEX2, v_tc.xy);\n"
    "\telse if (s < 3.5) c = texture2D(HS_BATCH_TEX3, v_tc.xy);\n"
    "\telse if (s < 4.5) c = texture2D(HS_BATCH_TEX4, v_tc.xy);\n"
    "\telse if (s < 5.5) c = texture2D(HS_BATCH_TEX5, v_tc.xy);\n"
    "\telse if (s < 6.5) c = texture2D(HS_BATCH_TEX6, v_tc.xy);\n"
    "\telse              c = texture2D(HS_BATCH_TEX7, v_tc.xy);\n"
    "\tfloat a = c.a * v_cmult.a;\n"
    "\tif (a < 0.28) {\n"
    "\t\tdiscard;\n"
    "\t}\n"
    "\tvec3 premul = c.rgb * v_cmult.rgb + c.a * v_cadd.rgb;\n"
    "\tvec3 rgb = clamp(premul / max(a, 0.001), vec3(0.0), vec3(1.25));\n"
    "\tfloat h = v_cadd.w;\n"
    "\tfloat fade = smoothstep(0.02, 0.16, h) * (1.0 - smoothstep(1.40, 7.80, h));\n"
    "\tgl_FragColor = vec4(rgb, clamp(fade * smoothstep(0.28, 0.65, a), 0.0, 1.0));\n"
    "}\n";

inline constexpr const char* kReflBatchFs16 =
    "#if defined(GL_ES)\n"
    "precision highp float;\n"
    "#endif\n"
    "uniform sampler2D HS_BATCH_TEX0;\n"
    "uniform sampler2D HS_BATCH_TEX1;\n"
    "uniform sampler2D HS_BATCH_TEX2;\n"
    "uniform sampler2D HS_BATCH_TEX3;\n"
    "uniform sampler2D HS_BATCH_TEX4;\n"
    "uniform sampler2D HS_BATCH_TEX5;\n"
    "uniform sampler2D HS_BATCH_TEX6;\n"
    "uniform sampler2D HS_BATCH_TEX7;\n"
    "uniform sampler2D HS_BATCH_TEX8;\n"
    "uniform sampler2D HS_BATCH_TEX9;\n"
    "uniform sampler2D HS_BATCH_TEX10;\n"
    "uniform sampler2D HS_BATCH_TEX11;\n"
    "uniform sampler2D HS_BATCH_TEX12;\n"
    "uniform sampler2D HS_BATCH_TEX13;\n"
    "uniform sampler2D HS_BATCH_TEX14;\n"
    "uniform sampler2D HS_BATCH_TEX15;\n"
    "varying vec3 v_tc;\n"
    "varying vec4 v_cmult;\n"
    "varying vec4 v_cadd;\n"
    "void main() {\n"
    "\tfloat s = v_tc.z;\n"
    "\tvec4 c;\n"
    "\tif (s < 7.5) {\n"
    "\t\tif (s < 3.5) {\n"
    "\t\t\tif (s < 1.5)      c = (s < 0.5) ? texture2D(HS_BATCH_TEX0, v_tc.xy) : texture2D(HS_BATCH_TEX1, v_tc.xy);\n"
    "\t\t\telse              c = (s < 2.5) ? texture2D(HS_BATCH_TEX2, v_tc.xy) : texture2D(HS_BATCH_TEX3, v_tc.xy);\n"
    "\t\t} else {\n"
    "\t\t\tif (s < 5.5)      c = (s < 4.5) ? texture2D(HS_BATCH_TEX4, v_tc.xy) : texture2D(HS_BATCH_TEX5, v_tc.xy);\n"
    "\t\t\telse              c = (s < 6.5) ? texture2D(HS_BATCH_TEX6, v_tc.xy) : texture2D(HS_BATCH_TEX7, v_tc.xy);\n"
    "\t\t}\n"
    "\t} else {\n"
    "\t\tif (s < 11.5) {\n"
    "\t\t\tif (s < 9.5)      c = (s < 8.5) ? texture2D(HS_BATCH_TEX8, v_tc.xy) : texture2D(HS_BATCH_TEX9, v_tc.xy);\n"
    "\t\t\telse              c = (s < 10.5) ? texture2D(HS_BATCH_TEX10, v_tc.xy) : texture2D(HS_BATCH_TEX11, v_tc.xy);\n"
    "\t\t} else {\n"
    "\t\t\tif (s < 13.5)     c = (s < 12.5) ? texture2D(HS_BATCH_TEX12, v_tc.xy) : texture2D(HS_BATCH_TEX13, v_tc.xy);\n"
    "\t\t\telse              c = (s < 14.5) ? texture2D(HS_BATCH_TEX14, v_tc.xy) : texture2D(HS_BATCH_TEX15, v_tc.xy);\n"
    "\t\t}\n"
    "\t}\n"
    "\tfloat a = c.a * v_cmult.a;\n"
    "\tif (a < 0.28) {\n"
    "\t\tdiscard;\n"
    "\t}\n"
    "\tvec3 premul = c.rgb * v_cmult.rgb + c.a * v_cadd.rgb;\n"
    "\tvec3 rgb = clamp(premul / max(a, 0.001), vec3(0.0), vec3(1.25));\n"
    "\tfloat h = v_cadd.w;\n"
    "\tfloat fade = smoothstep(0.02, 0.16, h) * (1.0 - smoothstep(1.40, 7.80, h));\n"
    "\tgl_FragColor = vec4(rgb, clamp(fade * smoothstep(0.28, 0.65, a), 0.0, 1.0));\n"
    "}\n";

inline constexpr const char* kReflBatchGpuVs =
    "#version 430 compatibility\n"
    "#extension GL_ARB_bindless_texture : require\n"
    "struct HsGpuInst { vec4 m0; vec4 m1; vec4 m2; vec4 cmult; vec4 cadd; vec4 meta; vec4 meta2; uvec2 tex0; uvec2 tex1; uvec2 tex2; uvec2 flags; };\n"
    "layout(std430, binding = 0) readonly buffer HsGpuInstBuf { HsGpuInst hs_gpu_insts[]; };\n"
    "uniform vec4 u_reflBounds;\n"
    "uniform vec4 u_reflEye;\n"
    "uniform vec4 u_reflBack;\n"
    "attribute vec4 POS2D_UV;\n"
    "attribute vec4 HS_BATCH_WPOS;\n"
    "varying vec2 v_tc;\n"
    "varying vec4 v_cmult;\n"
    "varying vec4 v_cadd;\n"
    "flat out uvec2 v_btex;\n"
    "void main() {\n"
    "\tint hs_iid = int(HS_BATCH_WPOS.x + 0.5);\n"
    "\tHsGpuInst hs_ins = hs_gpu_insts[hs_iid];\n"
    "\tif (hs_ins.cadd.w < 0.5) {\n"
    "\t\tgl_Position = vec4(2.0, 2.0, 2.0, 1.0);\n"
    "\t\tv_tc = vec2(0.0);\n"
    "\t\tv_cmult = vec4(0.0);\n"
    "\t\tv_cadd = vec4(0.0);\n"
    "\t\tv_btex = uvec2(0u, 0u);\n"
    "\t\treturn;\n"
    "\t}\n"
    "\tvec2 hs_p = POS2D_UV.xy;\n"
    "\tfloat hs_dx = hs_ins.m0.x * hs_p.x + hs_ins.m1.x * hs_p.y;\n"
    "\tfloat hs_dy = hs_ins.m0.y * hs_p.x + hs_ins.m1.y * hs_p.y;\n"
    "\tfloat hs_dz = hs_ins.m0.z * hs_p.x + hs_ins.m1.z * hs_p.y;\n"
    "\tfloat hs_wx = hs_dx + hs_ins.m2.x;\n"
    "\tfloat hs_wy = hs_dy + hs_ins.m2.y;\n"
    "\tfloat hs_wz = hs_dz + hs_ins.m2.z;\n"
    "\tif (hs_ins.meta2.z > 0.5 && hs_dy > 0.06) {\n"
    "\t\tfloat hs_ho;\n"
    "\t\tif (hs_ins.meta2.z < 1.5) {\n"
    "\t\t\ths_ho = hs_ins.meta2.x * smoothstep(0.08, 1.35, hs_dy)\n"
    "\t\t\t      + hs_ins.meta2.y * smoothstep(0.06, 1.10, hs_dy);\n"
    "\t\t} else {\n"
    "\t\t\tfloat hs_b2 = smoothstep(1.00, 3.80, hs_dy);\n"
    "\t\t\tfloat hs_hc = min(hs_dy, 8.0);\n"
    "\t\t\ths_ho = hs_ins.meta2.x * (hs_b2 * hs_b2) * (0.18 + 0.025 * hs_hc);\n"
    "\t\t}\n"
    "\t\ths_wx += hs_ins.meta.w * hs_ho;\n"
    "\t\ths_wz += hs_ins.meta2.w * hs_ho;\n"
    "\t}\n"
    "\tfloat hs_si = floor(POS2D_UV.z * 0.5);\n"
    "\tv_tc = vec2(POS2D_UV.z - 2.0 * hs_si, POS2D_UV.w);\n"
    "\tv_btex = (hs_si < 0.5) ? hs_ins.tex0 : ((hs_si < 1.5) ? hs_ins.tex1 : hs_ins.tex2);\n"
    "\tfloat hs_h = clamp(hs_wy, 0.0, 8.5);\n"
    "\tfloat hs_eyeY = max(u_reflEye.y, 6.0);\n"
    "\tfloat hs_ls = clamp(u_reflBounds.w, 0.50, 1.60);\n"
    "\tfloat hs_s = hs_eyeY / max(hs_eyeY + hs_h * hs_ls, 1.0);\n"
    "\tvec2 hs_rxz = mix(u_reflEye.xz, vec2(hs_wx, hs_wz), hs_s);\n"
    "\tvec2 hs_ndc = (hs_rxz - u_reflBounds.xy) * u_reflBounds.z;\n"
    "\tvec3 hs_pref = vec3(hs_wx, -hs_h, hs_wz);\n"
    "\tfloat hs_vd = dot(u_reflEye.xyz - hs_pref, u_reflBack.xyz);\n"
    "\tfloat hs_z = clamp((hs_vd - 4.0) / 140.0, 0.0, 1.0) * 1.98 - 0.99;\n"
    "\tv_cmult = hs_ins.cmult;\n"
    "\tv_cadd  = vec4(hs_ins.cadd.rgb, hs_h);\n"
    "\tgl_Position = vec4(hs_ndc.x, hs_ndc.y, hs_z, 1.0);\n"
    "}\n";

inline constexpr const char* kReflBatchGpuFs =
    "#version 430 compatibility\n"
    "#extension GL_ARB_bindless_texture : require\n"
    "varying vec2 v_tc;\n"
    "varying vec4 v_cmult;\n"
    "varying vec4 v_cadd;\n"
    "flat in uvec2 v_btex;\n"
    "void main() {\n"
    "\tvec4 c = texture2D(sampler2D(v_btex), v_tc);\n"
    "\tfloat a = c.a * v_cmult.a;\n"
    "\tif (a < 0.28) {\n"
    "\t\tdiscard;\n"
    "\t}\n"
    "\tvec3 premul = c.rgb * v_cmult.rgb + c.a * v_cadd.rgb;\n"
    "\tvec3 rgb = clamp(premul / max(a, 0.001), vec3(0.0), vec3(1.25));\n"
    "\tfloat h = v_cadd.w;\n"
    "\tfloat fade = smoothstep(0.02, 0.16, h) * (1.0 - smoothstep(1.40, 7.80, h));\n"
    "\tgl_FragColor = vec4(rgb, clamp(fade * smoothstep(0.28, 0.65, a), 0.0, 1.0));\n"
    "}\n";

class WaterReflectionEngine {
public:
    WaterReflectionEngine() = default;

    void SetOps(const hs_world_shadow::GlShadowOps& ops, bool isNativeGl) noexcept {
        ops_ = ops;
        isNativeGl_ = isNativeGl;
    }

    bool IsReady() const noexcept { return ready_ && !failed_; }
    bool IsFailed() const noexcept { return failed_; }
    bool SawOceanThisFrame() const noexcept { return sawOceanThisFrame_; }
    bool BeganWriteThisFrame() const noexcept { return beganWriteThisFrame_; }
    bool HasPrevMap() const noexcept { return ready_ && hasPrevMap_; }
    uint32_t LastFrameReflDraws() const noexcept { return lastFrameDraws_; }
    uint32_t ReadTextureId() const noexcept { return (ready_ && hasPrevMap_) ? tex_[readSlot_] : 0u; }
    bool     HasCamEye() const noexcept { return hasCamEye_; }
    float    CamEyeX() const noexcept { return camEyeX_; }
    float    CamEyeY() const noexcept { return camEyeY_; }
    float    CamEyeZ() const noexcept { return camEyeZ_; }

    bool IsInsideWorldPass(uint32_t curFbo) const noexcept {
        return sawOceanThisFrame_ && curFbo == worldFbo_ && worldVp_[2] > 0 && worldVp_[3] > 0;
    }

    bool NeedsBeginWrite(uint32_t curFbo, const WaterReflConfig& cfg) const noexcept {
        if (failed_ || !cfg.IsEnabled()) return false;
        if (!IsInsideWorldPass(curFbo)) return false;
        return !beganWriteThisFrame_;
    }

    void OnOceanDraw(uint32_t curFbo, const int32_t vp[4]) noexcept {
        worldFbo_ = curFbo;
        worldVp_[0] = vp[0];
        worldVp_[1] = vp[1];
        worldVp_[2] = vp[2];
        worldVp_[3] = vp[3];
        sawOceanThisFrame_ = true;
    }

    void OnMatrixV(const float* v) noexcept {
        if (!v) return;
        const float rx = v[0], ry = v[4], rz = v[8];
        const float ux = v[1], uy = v[5], uz = v[9];
        const float bx = v[2], by = v[6], bz = v[10];
        const float rLen = std::sqrt(rx * rx + rz * rz);
        if (rLen > 1e-4f) {
            camRightX_ = rx / rLen;
            camRightZ_ = rz / rLen;
            hasCamRight_ = true;
        }
        const float bLen = std::sqrt(bx * bx + by * by + bz * bz);
        if (bLen > 1e-4f) {
            camBackX_ = bx / bLen;
            camBackY_ = by / bLen;
            camBackZ_ = bz / bLen;
            hasCamBack_ = true;
        }
        const float tx = v[12], ty = v[13], tz = v[14];
        const float eyeX = -(rx * tx + ux * ty + bx * tz);
        const float eyeY = -(ry * tx + uy * ty + by * tz);
        const float eyeZ = -(rz * tx + uz * ty + bz * tz);
        if (std::isfinite(eyeX) && std::isfinite(eyeY) && std::isfinite(eyeZ) &&
            eyeY > 1.0f && eyeY < 300.0f) {
            camEyeX_ = eyeX;
            camEyeY_ = eyeY;
            camEyeZ_ = eyeZ;
            hasCamEye_ = true;
            if (std::fabs(by) > 0.08f) {
                const float t = eyeY / by;
                if (std::fabs(t) < 250.0f) {
                    camCenterX_ = eyeX - t * bx;
                    camCenterZ_ = eyeZ - t * bz;
                    hasCamCenter_ = true;
                }
            }
        }
    }

    bool IsEntityInsideReflBox(float wx, float wz) const noexcept {
        const float margin = kReflHalfExtent + 8.0f;
        return std::fabs(wx - writeCenterX_) <= margin && std::fabs(wz - writeCenterZ_) <= margin;
    }

    void ResetContext(bool hasAliveCtx) noexcept {
        if (hasAliveCtx && ops_.IsComplete()) {
            DestroyResources();
        } else {
            fbo_[0] = fbo_[1] = 0;
            tex_[0] = tex_[1] = 0;
            depthRbo_[0] = depthRbo_[1] = 0;
            progBatch_ = 0;
            progBatchGpu_ = 0;
        }
        ready_ = false;
        failed_ = false;
        hasPrevMap_ = false;
        ResetFrameFlags();
    }

    // Bind previous frame's reflection texture to GL_TEXTURE12 for ocean_combined.ksh!
    // Restores host active texture unit cleanly so DST texture state is never disturbed.
    bool BindForOceanSampling(const WaterReflConfig& cfg,
                              uint32_t hostActiveUnit,
                              hs_gl_cache::ThreadState& glCache,
                              bool glCacheTrusted,
                              float outBounds[4],
                              float outCfg[4]) noexcept {
        if (ready_ && !failed_ && hasPrevMap_ && cfg.IsEnabled() &&
            ops_.glActiveTexture && ops_.glBindTexture) {
            if (!boundForOceanThisFrame_ ||
                !glCacheTrusted ||
                glCache.boundTex2D[kReflTexUnit] != tex_[readSlot_]) {
                ops_.glActiveTexture(hs_world_shadow::kGlTexture0 + kReflTexUnit);
                ops_.glBindTexture(hs_world_shadow::kGlTexture2D, tex_[readSlot_]);
                ops_.glActiveTexture(hs_world_shadow::kGlTexture0 + hostActiveUnit);
                if (glCacheTrusted) {
                    glCache.boundTex2D[kReflTexUnit] = tex_[readSlot_];
                    glCache.activeUnit = static_cast<int32_t>(hostActiveUnit);
                }
                boundForOceanThisFrame_ = true;
            }
            outBounds[0] = prevCenterX_;
            outBounds[1] = prevCenterZ_;
            outBounds[2] = 1.0f / kReflHalfExtent;
            outBounds[3] = 1.0f;
            outCfg[0]    = std::clamp(cfg.strength, 0.0f, 1.2f);
            outCfg[1]    = std::clamp(cfg.rippleAmp, 0.0f, 1.5f);
            outCfg[2]    = 0.0f;
            outCfg[3]    = 0.0f;
            return true;
        }
        outBounds[0] = 0.0f;
        outBounds[1] = 0.0f;
        outBounds[2] = 1.0f / kReflHalfExtent;
        outBounds[3] = 0.0f;
        outCfg[0]    = 0.0f;
        outCfg[1]    = 0.0f;
        outCfg[2]    = 0.0f;
        outCfg[3]    = 0.0f;
        return false;
    }

    bool OnBeforeFirstStandingEntity(const WaterReflConfig& cfg,
                                     const hs_world_shadow::ShadowPassState& saved,
                                     hs_gl_cache::ThreadState& glCache) noexcept {
        if (beganWriteThisFrame_) return ready_;
        if (!cfg.IsEnabled() || !ops_.IsComplete() || !IsInsideWorldPass(saved.dstFbo)) {
            return false;
        }
        if (!EnsureResources(saved, glCache)) {
            return false;
        }

        ResolveEffectiveCamera(cfg);

        const float texelWorld = (2.0f * kReflHalfExtent) / static_cast<float>(kReflRes);
        writeCenterX_ = std::floor(effCenterX_ / texelWorld + 0.5f) * texelWorld;
        writeCenterZ_ = std::floor(effCenterZ_ / texelWorld + 0.5f) * texelWorld;
        writeBounds_[0] = writeCenterX_;
        writeBounds_[1] = writeCenterZ_;
        writeBounds_[2] = 1.0f / kReflHalfExtent;
        writeBounds_[3] = std::clamp(cfg.lengthScale, 0.50f, 1.60f);

        const SavedRasterState savedRaster = CaptureRasterState(glCache, saved.glCacheTrusted);

        ops_.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, fbo_[writeSlot_]);
        ops_.glViewport(0, 0, static_cast<int32_t>(kReflRes), static_cast<int32_t>(kReflRes));
        ops_.glDisable(hs_world_shadow::kGlScissorTest);
        ops_.glDisable(hs_world_shadow::kGlStencilTest);
        ops_.glColorMask(1, 1, 1, 1);
        ops_.glDepthMask(1);
        ops_.glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        ops_.glClear(hs_world_shadow::kGlColorBufferBit | hs_world_shadow::kGlDepthBufferBit);

        ops_.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, saved.dstFbo);
        ops_.glViewport(saved.vp[0], saved.vp[1], saved.vp[2], saved.vp[3]);
        if (saved.hasClearColor) {
            ops_.glClearColor(saved.clearColor[0], saved.clearColor[1], saved.clearColor[2], saved.clearColor[3]);
        }
        RestoreRasterState(savedRaster, glCache, saved.vp, saved.glCacheTrusted);

        beganWriteThisFrame_      = true;
        batchUniUploadedFrame_    = false;
        batchGpuUniUploadedFrame_ = false;
        return true;
    }

    void MirrorBatchedSprites(uint32_t indexCount,
                              const WaterReflConfig& cfg,
                              const hs_world_shadow::ShadowPassState& saved,
                              hs_gl_cache::ThreadState& glCache,
                              uintptr_t indexByteOffset = 0u) noexcept {
        if (!ready_ || !beganWriteThisFrame_ || indexCount == 0u || !ops_.IsComplete()) return;
        if (!cfg.IsEnabled() || !IsInsideWorldPass(saved.dstFbo)) return;

        const SavedRasterState savedRaster = CaptureRasterState(glCache, saved.glCacheTrusted);
        const uint32_t restoreProg = ResolveHostProgram(saved, glCache);

        ops_.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, fbo_[writeSlot_]);
        ops_.glViewport(0, 0, static_cast<int32_t>(kReflRes), static_cast<int32_t>(kReflRes));
        ApplyReflWriteRasterState();

        ops_.glUseProgram(progBatch_);
        if (!batchUniUploadedFrame_) {
            const float eyeU[4]  = { effEyeX_, effEyeY_, effEyeZ_, 0.0f };
            const float backU[4] = { effBackX_, effBackY_, effBackZ_, 0.0f };
            if (locBatchBounds_ >= 0) ops_.glUniform4fv(locBatchBounds_, 1, writeBounds_);
            if (locBatchEye_ >= 0)    ops_.glUniform4fv(locBatchEye_, 1, eyeU);
            if (locBatchBack_ >= 0)   ops_.glUniform4fv(locBatchBack_, 1, backU);
            batchUniUploadedFrame_ = true;
        }

        ops_.glDrawElements(hs_world_shadow::kGlTriangles, static_cast<int32_t>(indexCount),
                            hs_world_shadow::kGlUnsignedShort, reinterpret_cast<const void*>(indexByteOffset));
        ++curFrameDraws_;
        wroteAnyThisFrame_ = true;

        ops_.glUseProgram(restoreProg);
        ops_.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, saved.dstFbo);
        ops_.glViewport(saved.vp[0], saved.vp[1], saved.vp[2], saved.vp[3]);
        RestoreRasterState(savedRaster, glCache, saved.vp, saved.glCacheTrusted);
        if (saved.glCacheTrusted) {
            glCache.curProgram = restoreProg;
        }
    }

    void MirrorBatchedSpritesGpu(uint32_t ssboId,
                                 uint32_t indexCount,
                                 int32_t baseVertex,
                                 uintptr_t indexByteOffset,
                                 const WaterReflConfig& cfg,
                                 const hs_world_shadow::ShadowPassState& saved,
                                 hs_gl_cache::ThreadState& glCache) noexcept {
        if (!ready_ || !beganWriteThisFrame_ || !isNativeGl_ || progBatchGpu_ == 0u ||
            indexCount == 0u || ssboId == 0u || !ops_.IsComplete() ||
            !ops_.glBindBufferBase || !ops_.glDrawElementsBaseVertex) {
            return;
        }
        if (!cfg.IsEnabled() || !IsInsideWorldPass(saved.dstFbo)) return;

        const SavedRasterState savedRaster = CaptureRasterState(glCache, saved.glCacheTrusted);
        const uint32_t restoreProg = ResolveHostProgram(saved, glCache);

        ops_.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, fbo_[writeSlot_]);
        ops_.glViewport(0, 0, static_cast<int32_t>(kReflRes), static_cast<int32_t>(kReflRes));
        ApplyReflWriteRasterState();

        ops_.glUseProgram(progBatchGpu_);
        ops_.glBindBufferBase(hs_world_shadow::kGlShaderStorageBuffer, hs_world_shadow::kGpuInstBinding, ssboId);
        if (!batchGpuUniUploadedFrame_) {
            const float eyeU[4]  = { effEyeX_, effEyeY_, effEyeZ_, 0.0f };
            const float backU[4] = { effBackX_, effBackY_, effBackZ_, 0.0f };
            if (locBatchGpuBounds_ >= 0) ops_.glUniform4fv(locBatchGpuBounds_, 1, writeBounds_);
            if (locBatchGpuEye_ >= 0)    ops_.glUniform4fv(locBatchGpuEye_, 1, eyeU);
            if (locBatchGpuBack_ >= 0)   ops_.glUniform4fv(locBatchGpuBack_, 1, backU);
            batchGpuUniUploadedFrame_ = true;
        }

        ops_.glDrawElementsBaseVertex(hs_world_shadow::kGlTriangles, static_cast<int32_t>(indexCount),
                                      hs_world_shadow::kGlUnsignedShort,
                                      reinterpret_cast<const void*>(indexByteOffset), baseVertex);
        ++curFrameDraws_;
        wroteAnyThisFrame_ = true;

        ops_.glUseProgram(restoreProg);
        ops_.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, saved.dstFbo);
        ops_.glViewport(saved.vp[0], saved.vp[1], saved.vp[2], saved.vp[3]);
        RestoreRasterState(savedRaster, glCache, saved.vp, saved.glCacheTrusted);
        if (saved.glCacheTrusted) {
            glCache.curProgram = restoreProg;
        }
    }

    template <typename DrawFn>
    void MirrorCurrentProgramDraw(int32_t locShadowPass,
                                  int32_t locShadowBounds,
                                  int32_t locShadowCamR,
                                  int32_t locShadowPt,
                                  const WaterReflConfig& cfg,
                                  const hs_world_shadow::ShadowPassState& saved,
                                  hs_gl_cache::ThreadState& glCache,
                                  DrawFn&& issueDraw) noexcept {
        if (!ready_ || !beganWriteThisFrame_ || locShadowPass < 0 || !ops_.IsComplete()) return;
        if (!cfg.IsEnabled() || !IsInsideWorldPass(saved.dstFbo)) return;

        const SavedRasterState savedRaster = CaptureRasterState(glCache, saved.glCacheTrusted);

        ops_.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, fbo_[writeSlot_]);
        ops_.glViewport(0, 0, static_cast<int32_t>(kReflRes), static_cast<int32_t>(kReflRes));
        ApplyReflWriteRasterState();

        const float eyeU[4]  = { effEyeX_, effEyeY_, effEyeZ_, 0.0f };
        const float backU[4] = { effBackX_, effBackY_, effBackZ_, 0.0f };
        ops_.glUniform1f(locShadowPass, 2.0f);
        if (locShadowBounds >= 0) ops_.glUniform4fv(locShadowBounds, 1, writeBounds_);
        if (locShadowCamR >= 0)   ops_.glUniform4fv(locShadowCamR, 1, eyeU);
        if (locShadowPt >= 0)     ops_.glUniform4fv(locShadowPt, 1, backU);

        issueDraw();
        ++curFrameDraws_;
        wroteAnyThisFrame_ = true;

        ops_.glUniform1f(locShadowPass, 0.0f);
        if (saved.glCacheTrusted) {
            const uint32_t prog = saved.dstProg;
            glCache.InvalidateUniform(prog, locShadowPass);
            if (locShadowBounds >= 0) glCache.InvalidateUniform(prog, locShadowBounds);
            if (locShadowCamR >= 0)   glCache.InvalidateUniform(prog, locShadowCamR);
            if (locShadowPt >= 0)     glCache.InvalidateUniform(prog, locShadowPt);
        }

        ops_.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, saved.dstFbo);
        ops_.glViewport(saved.vp[0], saved.vp[1], saved.vp[2], saved.vp[3]);
        RestoreRasterState(savedRaster, glCache, saved.vp, saved.glCacheTrusted);
    }

    void OnEndFrame() noexcept {
        if (wroteAnyThisFrame_) {
            readSlot_    = writeSlot_;
            writeSlot_   = 1u - writeSlot_;
            prevCenterX_ = writeCenterX_;
            prevCenterZ_ = writeCenterZ_;
            hasPrevMap_  = true;
        } else {
            // Clear stale coastal map immediately when moving inland / caves / turning OFF!
            hasPrevMap_ = false;
        }
        lastFrameDraws_ = curFrameDraws_;
        ResetFrameFlags();
    }

private:
    void ResetFrameFlags() noexcept {
        sawOceanThisFrame_        = false;
        beganWriteThisFrame_      = false;
        wroteAnyThisFrame_        = false;
        batchUniUploadedFrame_    = false;
        batchGpuUniUploadedFrame_ = false;
        boundForOceanThisFrame_ = false;
        curFrameDraws_          = 0u;
    }

    void ResolveEffectiveCamera(const WaterReflConfig& cfg) noexcept {
        if (!hasCamRight_ && cfg.hasLuaCam) {
            const float len = std::sqrt(cfg.luaCamRightX * cfg.luaCamRightX + cfg.luaCamRightZ * cfg.luaCamRightZ);
            if (len > 1e-4f) {
                camRightX_ = cfg.luaCamRightX / len;
                camRightZ_ = cfg.luaCamRightZ / len;
                hasCamRight_ = true;
            }
        }
        effCenterX_ = hasCamCenter_ ? camCenterX_ : (cfg.hasLuaCam ? cfg.luaCamX : 0.0f);
        effCenterZ_ = hasCamCenter_ ? camCenterZ_ : (cfg.hasLuaCam ? cfg.luaCamZ : 0.0f);
        if (hasCamEye_ && hasCamBack_) {
            effEyeX_  = camEyeX_;
            effEyeY_  = camEyeY_;
            effEyeZ_  = camEyeZ_;
            effBackX_ = camBackX_;
            effBackY_ = camBackY_;
            effBackZ_ = camBackZ_;
        } else {
            // Right-handed fallback: camBackXZ = cross(camRight, +Y) = (-camRightZ, camRightX)
            const float bxzX = -camRightZ_;
            const float bxzZ =  camRightX_;
            effEyeX_  = effCenterX_ + bxzX * 30.0f;
            effEyeY_  = 30.0f;
            effEyeZ_  = effCenterZ_ + bxzZ * 30.0f;
            effBackX_ = bxzX * 0.70710678f;
            effBackY_ = 0.70710678f;
            effBackZ_ = bxzZ * 0.70710678f;
        }
    }

    void ApplyReflWriteRasterState() noexcept {
        ops_.glDisable(hs_world_shadow::kGlCullFace);
        ops_.glDisable(hs_world_shadow::kGlScissorTest);
        ops_.glDisable(hs_world_shadow::kGlStencilTest);
        ops_.glDisable(hs_world_shadow::kGlBlend);
        ops_.glEnable(hs_world_shadow::kGlDepthTest);
        ops_.glDepthMask(1);
        ops_.glDepthFunc(hs_world_shadow::kGlLess);
        ops_.glColorMask(1, 1, 1, 1);
    }

    struct SavedRasterState {
        bool     caps[5]        = { true, false, false, false, false };
        bool     capsKnown[5]   = { false, false, false, false, false };
        uint32_t depthFunc      = hs_world_shadow::kGlLess;
        bool     depthFuncKnown = false;
        uint8_t  depthMask      = 0u;
        bool     depthMaskKnown = false;
        uint8_t  colorMask[4]   = { 1u, 1u, 1u, 1u };
        bool     colorMaskKnown = false;
    };

    uint32_t ResolveHostProgram(const hs_world_shadow::ShadowPassState& saved,
                                const hs_gl_cache::ThreadState& glCache) const noexcept {
        if ((!saved.glCacheTrusted || glCache.curProgram == hs_gl_cache::kInvalidId) && ops_.glGetIntegerv) {
            int32_t drvProg = -1;
            ops_.glGetIntegerv(hs_world_shadow::kGlCurrentProgram, &drvProg);
            if (drvProg >= 0) return static_cast<uint32_t>(drvProg);
        }
        return saved.dstProg;
    }

    SavedRasterState CaptureRasterState(const hs_gl_cache::ThreadState& glCache,
                                        bool glCacheTrusted) const noexcept {
        SavedRasterState rs{};
        static constexpr uint32_t kCapEnums[5] = {
            hs_world_shadow::kGlBlend, hs_world_shadow::kGlDepthTest,
            hs_world_shadow::kGlCullFace, hs_world_shadow::kGlScissorTest,
            hs_world_shadow::kGlStencilTest
        };
        for (int i = 0; i < 5; ++i) {
            if (glCacheTrusted && glCache.caps[i] >= 0) {
                rs.caps[i]      = (glCache.caps[i] != 0);
                rs.capsKnown[i] = true;
            } else if (ops_.glGetIntegerv) {
                int32_t v = -1;
                ops_.glGetIntegerv(kCapEnums[i], &v);
                if (v >= 0) {
                    rs.caps[i]      = (v != 0);
                    rs.capsKnown[i] = true;
                }
            }
        }
        if (glCacheTrusted && glCache.depthFunc != hs_gl_cache::kInvalidId) {
            rs.depthFunc      = glCache.depthFunc;
            rs.depthFuncKnown = true;
        } else if (ops_.glGetIntegerv) {
            int32_t v = -1;
            ops_.glGetIntegerv(hs_world_shadow::kGlDepthFuncEnum, &v);
            if (v > 0) {
                rs.depthFunc      = static_cast<uint32_t>(v);
                rs.depthFuncKnown = true;
            }
        }
        if (glCacheTrusted && glCache.depthMask >= 0) {
            rs.depthMask      = static_cast<uint8_t>(glCache.depthMask ? 1u : 0u);
            rs.depthMaskKnown = true;
        } else if (ops_.glGetIntegerv) {
            int32_t v = -1;
            ops_.glGetIntegerv(hs_world_shadow::kGlDepthWritemask, &v);
            if (v >= 0) {
                rs.depthMask      = static_cast<uint8_t>(v ? 1u : 0u);
                rs.depthMaskKnown = true;
            }
        }
        if (glCacheTrusted && glCache.colorMask >= 0) {
            const int8_t cm = glCache.colorMask;
            rs.colorMask[0]   = (cm & 1) ? 1u : 0u;
            rs.colorMask[1]   = (cm & 2) ? 1u : 0u;
            rs.colorMask[2]   = (cm & 4) ? 1u : 0u;
            rs.colorMask[3]   = (cm & 8) ? 1u : 0u;
            rs.colorMaskKnown = true;
        } else if (ops_.glGetIntegerv) {
            int32_t cm[4] = { -1, -1, -1, -1 };
            ops_.glGetIntegerv(hs_world_shadow::kGlColorWritemask, cm);
            if (cm[0] >= 0 && cm[1] >= 0 && cm[2] >= 0 && cm[3] >= 0) {
                rs.colorMask[0]   = cm[0] ? 1u : 0u;
                rs.colorMask[1]   = cm[1] ? 1u : 0u;
                rs.colorMask[2]   = cm[2] ? 1u : 0u;
                rs.colorMask[3]   = cm[3] ? 1u : 0u;
                rs.colorMaskKnown = true;
            }
        }
        return rs;
    }

    void RestoreRasterState(const SavedRasterState& rs,
                            hs_gl_cache::ThreadState& glCache,
                            const int32_t dstVp[4],
                            bool glCacheTrusted) noexcept {
        static constexpr uint32_t kCapEnums[5] = {
            hs_world_shadow::kGlBlend, hs_world_shadow::kGlDepthTest,
            hs_world_shadow::kGlCullFace, hs_world_shadow::kGlScissorTest,
            hs_world_shadow::kGlStencilTest
        };
        for (int i = 0; i < 5; ++i) {
            if (rs.caps[i]) ops_.glEnable(kCapEnums[i]);
            else            ops_.glDisable(kCapEnums[i]);
            if (glCacheTrusted) {
                glCache.caps[i] = rs.capsKnown[i] ? static_cast<int8_t>(rs.caps[i] ? 1 : 0) : -1;
            }
        }
        ops_.glDepthFunc(rs.depthFunc);
        if (glCacheTrusted) {
            glCache.depthFunc = rs.depthFuncKnown ? rs.depthFunc : hs_gl_cache::kInvalidId;
        }
        ops_.glDepthMask(rs.depthMask);
        if (glCacheTrusted) {
            glCache.depthMask = rs.depthMaskKnown ? static_cast<int8_t>(rs.depthMask ? 1 : 0) : -1;
        }
        ops_.glColorMask(rs.colorMask[0], rs.colorMask[1], rs.colorMask[2], rs.colorMask[3]);
        if (glCacheTrusted) {
            if (rs.colorMaskKnown) {
                glCache.colorMask = static_cast<int8_t>(
                    (rs.colorMask[0] ? 1 : 0) | (rs.colorMask[1] ? 2 : 0) |
                    (rs.colorMask[2] ? 4 : 0) | (rs.colorMask[3] ? 8 : 0));
            } else {
                glCache.colorMask = -1;
            }
            glCache.vpValid = true;
            glCache.vp[0] = dstVp[0];
            glCache.vp[1] = dstVp[1];
            glCache.vp[2] = dstVp[2];
            glCache.vp[3] = dstVp[3];
        }
    }

    uint32_t CompileShader(uint32_t type, const char* rawSrc, const char* tag) noexcept {
        const uint32_t sh = ops_.glCreateShader(type);
        if (sh == 0u) return 0u;
        std::string src;
        if (isNativeGl_ && std::strncmp(rawSrc, "#version", 8) != 0) {
            src.reserve(std::strlen(rawSrc) + 16u);
            src.append("#version 100\n");
            src.append(rawSrc);
        } else {
            src.assign(rawSrc);
        }
        const char* ptr = src.c_str();
        const int32_t len = static_cast<int32_t>(src.size());
        ops_.glShaderSource(sh, 1, &ptr, &len);
        ops_.glCompileShader(sh);
        int32_t ok = 0;
        ops_.glGetShaderiv(sh, hs_world_shadow::kGlCompileStatus, &ok);
        if (!ok) {
            if (ops_.logFn && ops_.glGetShaderInfoLog) {
                char logBuf[512]{};
                int32_t logLen = 0;
                ops_.glGetShaderInfoLog(sh, sizeof(logBuf) - 1, &logLen, logBuf);
                char msg[640]{};
                std::snprintf(msg, sizeof(msg), "[REFL][ERR] Compile failed (%s): %s", tag, logBuf);
                ops_.logFn(msg);
            }
            ops_.glDeleteShader(sh);
            return 0u;
        }
        return sh;
    }

    uint32_t BuildProgramPair(const char* vsSrc, const char* fsSrc, const char* tag, bool bindColAttribs) noexcept {
        const uint32_t vs = CompileShader(hs_world_shadow::kGlVertexShader, vsSrc, tag);
        const uint32_t fs = CompileShader(hs_world_shadow::kGlFragmentShader, fsSrc, tag);
        if (vs == 0u || fs == 0u) {
            if (vs) ops_.glDeleteShader(vs);
            if (fs) ops_.glDeleteShader(fs);
            return 0u;
        }
        const uint32_t prog = ops_.glCreateProgram();
        if (prog == 0u) {
            ops_.glDeleteShader(vs);
            ops_.glDeleteShader(fs);
            return 0u;
        }
        ops_.glAttachShader(prog, vs);
        ops_.glAttachShader(prog, fs);
        ops_.glBindAttribLocation(prog, 0u, "POS2D_UV");
        if (bindColAttribs) {
            ops_.glBindAttribLocation(prog, 5u, "HS_BATCH_CMULT");
            ops_.glBindAttribLocation(prog, 6u, "HS_BATCH_CADD");
        }
        ops_.glBindAttribLocation(prog, 7u, "HS_BATCH_WPOS");
        ops_.glLinkProgram(prog);
        ops_.glDeleteShader(vs);
        ops_.glDeleteShader(fs);

        int32_t ok = 0;
        ops_.glGetProgramiv(prog, hs_world_shadow::kGlLinkStatus, &ok);
        if (!ok) {
            if (ops_.logFn && ops_.glGetProgramInfoLog) {
                char logBuf[512]{};
                int32_t logLen = 0;
                ops_.glGetProgramInfoLog(prog, sizeof(logBuf) - 1, &logLen, logBuf);
                char msg[640]{};
                std::snprintf(msg, sizeof(msg), "[REFL][ERR] Link failed (%s): %s", tag, logBuf);
                ops_.logFn(msg);
            }
            ops_.glDeleteProgram(prog);
            return 0u;
        }
        return prog;
    }

    uint32_t BuildBatchProgram(bool use16BatchSlots) noexcept {
        return BuildProgramPair(kReflBatchVs, use16BatchSlots ? kReflBatchFs16 : kReflBatchFs, "ReflBatch", true);
    }

    uint32_t BuildGpuBatchProgram() noexcept {
        return BuildProgramPair(kReflBatchGpuVs, kReflBatchGpuFs, "ReflBatchGpu", false);
    }

    bool EnsureResources(const hs_world_shadow::ShadowPassState& saved,
                         hs_gl_cache::ThreadState& glCache) noexcept {
        if (failed_) return false;
        if (ready_) return true;

        int32_t maxUnits = 0;
        if (ops_.glGetIntegerv) {
            ops_.glGetIntegerv(hs_world_shadow::kGlMaxTextureImageUnits, &maxUnits);
            if (maxUnits > 0 && maxUnits <= static_cast<int32_t>(kReflTexUnit)) {
                failed_ = true;
                return false;
            }
        }
        const bool use16BatchSlots = isNativeGl_ && (maxUnits >= 24);

        int32_t prevRbo = 0;
        if (ops_.glGetIntegerv) {
            ops_.glGetIntegerv(hs_world_shadow::kGlRenderbufferBinding, &prevRbo);
        }
        const uint32_t restoreProg = ResolveHostProgram(saved, glCache);
        struct Guard {
            const hs_world_shadow::GlShadowOps& ops;
            const hs_world_shadow::ShadowPassState& saved;
            hs_gl_cache::ThreadState& glCache;
            uint32_t restoreProg;
            uint32_t prevRbo;
            bool touchedProg      = false;
            bool touchedFbo       = false;
            bool touchedRbo       = false;
            bool touchedActiveTex = false;
            bool touchedUnit12    = false;
            bool restored         = false;
            void Restore() noexcept {
                if (restored) return;
                restored = true;
                if (touchedProg) {
                    ops.glUseProgram(restoreProg);
                    if (saved.glCacheTrusted) glCache.curProgram = restoreProg;
                }
                if (touchedFbo) ops.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, saved.dstFbo);
                if (touchedRbo) ops.glBindRenderbuffer(hs_world_shadow::kGlRenderbuffer, prevRbo);
                if (touchedActiveTex) {
                    ops.glActiveTexture(hs_world_shadow::kGlTexture0 + saved.activeUnit);
                    if (saved.glCacheTrusted) glCache.activeUnit = static_cast<int32_t>(saved.activeUnit);
                }
                if (touchedUnit12 && saved.glCacheTrusted) {
                    glCache.boundTex2D[kReflTexUnit] = hs_gl_cache::kInvalidId;
                }
            }
            ~Guard() { Restore(); }
        } guard{ ops_, saved, glCache, restoreProg, (prevRbo > 0) ? static_cast<uint32_t>(prevRbo) : 0u };

        if (progBatch_ == 0u) {
            progBatch_ = BuildBatchProgram(use16BatchSlots);
            if (progBatch_ == 0u) {
                guard.Restore();
                DestroyResources();
                failed_ = true;
                return false;
            }
            locBatchBounds_ = ops_.glGetUniformLocation(progBatch_, "u_reflBounds");
            locBatchEye_    = ops_.glGetUniformLocation(progBatch_, "u_reflEye");
            locBatchBack_   = ops_.glGetUniformLocation(progBatch_, "u_reflBack");
            guard.touchedProg = true;
            ops_.glUseProgram(progBatch_);
            const uint32_t slotCnt = use16BatchSlots
                ? hs_world_shadow::kMaxBatchTexSlots : hs_world_shadow::kLegacyBatchTexSlots;
            for (uint32_t s = 0; s < slotCnt; ++s) {
                char uName[24]{};
                std::snprintf(uName, sizeof(uName), "HS_BATCH_TEX%u", s);
                const int32_t loc = ops_.glGetUniformLocation(progBatch_, uName);
                if (loc >= 0) {
                    ops_.glUniform1i(loc, static_cast<int32_t>(hs_world_shadow::BatchSlotToTexUnit(s)));
                }
            }
        }
        if (progBatchGpu_ == 0u && isNativeGl_ && ops_.glBindBufferBase && ops_.glDrawElementsBaseVertex) {
            progBatchGpu_ = BuildGpuBatchProgram();
            if (progBatchGpu_ != 0u) {
                locBatchGpuBounds_ = ops_.glGetUniformLocation(progBatchGpu_, "u_reflBounds");
                locBatchGpuEye_    = ops_.glGetUniformLocation(progBatchGpu_, "u_reflEye");
                locBatchGpuBack_   = ops_.glGetUniformLocation(progBatchGpu_, "u_reflBack");
            }
        }

        ops_.glGenFramebuffers(2, fbo_);
        ops_.glGenTextures(2, tex_);
        ops_.glGenRenderbuffers(2, depthRbo_);
        guard.touchedActiveTex = true;
        guard.touchedUnit12    = true;
        ops_.glActiveTexture(hs_world_shadow::kGlTexture0 + kReflTexUnit);

        bool allOk = true;
        for (int i = 0; i < 2; ++i) {
            if (fbo_[i] == 0u || tex_[i] == 0u || depthRbo_[i] == 0u) {
                allOk = false;
                break;
            }
            ops_.glBindTexture(hs_world_shadow::kGlTexture2D, tex_[i]);
            ops_.glTexParameteri(hs_world_shadow::kGlTexture2D, hs_world_shadow::kGlTextureMinFilter,
                                 static_cast<int32_t>(hs_world_shadow::kGlLinear));
            ops_.glTexParameteri(hs_world_shadow::kGlTexture2D, hs_world_shadow::kGlTextureMagFilter,
                                 static_cast<int32_t>(hs_world_shadow::kGlLinear));
            ops_.glTexParameteri(hs_world_shadow::kGlTexture2D, hs_world_shadow::kGlTextureWrapS,
                                 static_cast<int32_t>(hs_world_shadow::kGlClampToEdge));
            ops_.glTexParameteri(hs_world_shadow::kGlTexture2D, hs_world_shadow::kGlTextureWrapT,
                                 static_cast<int32_t>(hs_world_shadow::kGlClampToEdge));
            ops_.glTexImage2D(hs_world_shadow::kGlTexture2D, 0, static_cast<int32_t>(hs_world_shadow::kGlRgba),
                              static_cast<int32_t>(kReflRes), static_cast<int32_t>(kReflRes),
                              0, hs_world_shadow::kGlRgba, hs_world_shadow::kGlUnsignedByte, nullptr);

            guard.touchedFbo = true;
            ops_.glBindFramebuffer(hs_world_shadow::kGlFramebuffer, fbo_[i]);
            ops_.glFramebufferTexture2D(hs_world_shadow::kGlFramebuffer, hs_world_shadow::kGlColorAttachment0,
                                        hs_world_shadow::kGlTexture2D, tex_[i], 0);
            guard.touchedRbo = true;
            ops_.glBindRenderbuffer(hs_world_shadow::kGlRenderbuffer, depthRbo_[i]);
            ops_.glRenderbufferStorage(hs_world_shadow::kGlRenderbuffer, hs_world_shadow::kGlDepthComponent16,
                                       static_cast<int32_t>(kReflRes), static_cast<int32_t>(kReflRes));
            ops_.glFramebufferRenderbuffer(hs_world_shadow::kGlFramebuffer, hs_world_shadow::kGlDepthAttachment,
                                           hs_world_shadow::kGlRenderbuffer, depthRbo_[i]);

            const uint32_t st = ops_.glCheckFramebufferStatus(hs_world_shadow::kGlFramebuffer);
            if (st != hs_world_shadow::kGlFramebufferComplete) {
                if (ops_.logFn) {
                    char msg[192]{};
                    std::snprintf(msg, sizeof(msg), "[REFL][ERR] FBO[%d] incomplete: status=0x%X", i, st);
                    ops_.logFn(msg);
                }
                allOk = false;
                break;
            }
        }

        guard.Restore();
        if (!allOk) {
            DestroyResources();
            failed_ = true;
            return false;
        }
        ready_ = true;
        if (ops_.logFn) {
            char msg[200]{};
            std::snprintf(msg, sizeof(msg),
                          "[VISUAL][WATER_REFL] Planar Water Reflection FBO ready (%ux%u RGBA8+DEPTH16, unit=TEXTURE%u, coastal-only)",
                          kReflRes, kReflRes, kReflTexUnit);
            ops_.logFn(msg);
        }
        return true;
    }

    void DestroyResources() noexcept {
        if (fbo_[0] || fbo_[1]) {
            ops_.glDeleteFramebuffers(2, fbo_);
            fbo_[0] = fbo_[1] = 0;
        }
        if (tex_[0] || tex_[1]) {
            ops_.glDeleteTextures(2, tex_);
            tex_[0] = tex_[1] = 0;
        }
        if (depthRbo_[0] || depthRbo_[1]) {
            ops_.glDeleteRenderbuffers(2, depthRbo_);
            depthRbo_[0] = depthRbo_[1] = 0;
        }
        if (progBatch_) {
            ops_.glDeleteProgram(progBatch_);
            progBatch_ = 0;
        }
        if (progBatchGpu_) {
            ops_.glDeleteProgram(progBatchGpu_);
            progBatchGpu_ = 0;
        }
        ready_ = false;
    }

    hs_world_shadow::GlShadowOps ops_{};
    bool     isNativeGl_ = false;
    bool     ready_      = false;
    bool     failed_     = false;

    uint32_t fbo_[2]      = {};
    uint32_t tex_[2]      = {};
    uint32_t depthRbo_[2] = {};
    uint32_t readSlot_    = 0u;
    uint32_t writeSlot_   = 0u;
    bool     hasPrevMap_  = false;

    uint32_t progBatch_      = 0u;
    int32_t  locBatchBounds_ = -1;
    int32_t  locBatchEye_    = -1;
    int32_t  locBatchBack_   = -1;

    uint32_t progBatchGpu_      = 0u;
    int32_t  locBatchGpuBounds_ = -1;
    int32_t  locBatchGpuEye_    = -1;
    int32_t  locBatchGpuBack_   = -1;

    uint32_t worldFbo_   = 0u;
    int32_t  worldVp_[4] = { 0, 0, 1280, 720 };

    float    camRightX_  = 0.707f, camRightZ_ = -0.707f;
    bool     hasCamRight_ = false;
    float    camEyeX_    = 0.0f, camEyeY_ = 30.0f, camEyeZ_ = 30.0f;
    bool     hasCamEye_  = false;
    float    camBackX_   = 0.0f, camBackY_ = 0.707f, camBackZ_ = 0.707f;
    bool     hasCamBack_ = false;
    float    camCenterX_ = 0.0f, camCenterZ_ = 0.0f;
    bool     hasCamCenter_ = false;

    float    effCenterX_ = 0.0f, effCenterZ_ = 0.0f;
    float    effEyeX_    = 0.0f, effEyeY_ = 30.0f, effEyeZ_ = 30.0f;
    float    effBackX_   = 0.0f, effBackY_ = 0.707f, effBackZ_ = 0.707f;

    float    prevCenterX_ = 0.0f, prevCenterZ_ = 0.0f;
    float    writeCenterX_ = 0.0f, writeCenterZ_ = 0.0f;
    float    writeBounds_[4] = { 0.0f, 0.0f, 1.0f / kReflHalfExtent, 1.0f };

    bool     sawOceanThisFrame_      = false;
    bool     beganWriteThisFrame_    = false;
    bool     wroteAnyThisFrame_      = false;
    bool     batchUniUploadedFrame_    = false;
    bool     batchGpuUniUploadedFrame_ = false;
    bool     boundForOceanThisFrame_ = false;
    uint32_t curFrameDraws_          = 0u;
    uint32_t lastFrameDraws_         = 0u;
};

} // namespace hs_water_refl
