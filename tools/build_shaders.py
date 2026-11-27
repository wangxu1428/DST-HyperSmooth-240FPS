#!/usr/bin/env python3
"""
Builds native Klei .ksh shader packages for DST HyperSmooth v2.8.0+ Visual Studio:
  1. hs_kawase_down.ksh        - Karis-weighted 5-tap Dual-Kawase downsample (strips blue shadow markers)
  2. hs_kawase_up.ksh          - Wide tent upsample for smooth atmospheric halo
  3. hs_postprocess_bloom.ksh  - Full Glow Pyramid Studio (Threshold/Knee/Warmth/Spread/Tail/Wrap/Rim/GodRays/Compress/Protect)
  4. hs_visual_final.ksh       - Single-pass BCAS Pro + Ink Deconv + AURA + Full Cinema Grading + ASC CDL
                                 + Soft Ground Shadow Composite + Vignette + Animated Film Grain + TPDF Dithering

Algorithms for BCAS bilateral sharpening, ink deconvolution, AURA anti-overshoot,
ACES/CDL grading, and Contract v17 blue-channel shadow separation adapted from
DST-BCAS-Studio (MIT License, Copyright 2026 Nanmianyi).

Format verified 1:1 against all 70/70 stock Klei shaders.zip (.ksh) binaries:
  [uint32 len + ascii name]
  [uint32 num_uniforms]
    foreach uniform:
      [uint32 len + ascii name]
      [uint32 len + ascii semantic (empty)]
      [uint32 type: 0x00=float, 0x02=vec2, 0x03=vec3, 0x04=vec4, 0x14=mat4, 0x2B=sampler2D]
      [uint32 array_count]
      if type != 0x2B: [uint32 num_floats][float32 * num_floats]
  [uint32 len + ascii vs_filename][uint32 len + vs_source_with_nul]
  [uint32 len + ascii ps_filename][uint32 len + ps_source_with_nul]
  [uint32 num_vs_uniform_indices][uint32 indices...]
  [uint32 num_ps_uniform_indices][uint32 indices...]
"""

import os
import struct

UNIFORM_FLOAT = 0x00
UNIFORM_VEC2 = 0x02
UNIFORM_VEC3 = 0x03
UNIFORM_VEC4 = 0x04
UNIFORM_MAT4 = 0x14
UNIFORM_SAMPLER = 0x2B


def pack_str(s: str) -> bytes:
    b = s.encode("ascii")
    return struct.pack("<I", len(b)) + b


def pack_zstr(s: str) -> bytes:
    b = s.encode("ascii") + b"\x00"
    return struct.pack("<I", len(b)) + b


def build_ksh(
    effect_name: str,
    uniforms: list,
    vs_name: str,
    vs_src: str,
    ps_name: str,
    ps_src: str,
    vs_uniform_indices: list,
    ps_uniform_indices: list,
) -> bytes:
    out = bytearray()
    out += pack_str(effect_name)
    out += struct.pack("<I", len(uniforms))
    for u in uniforms:
        uname, utype, ucount = u[0], u[1], u[2]
        out += pack_str(uname)
        out += pack_str("")  # semantic name (empty in Klei postprocess shaders)
        out += struct.pack("<II", utype, ucount)
        if utype != UNIFORM_SAMPLER:
            elem_floats = 16 if utype == UNIFORM_MAT4 else (3 if utype == UNIFORM_VEC3 else (2 if utype == UNIFORM_VEC2 else (1 if utype == UNIFORM_FLOAT else 4)))
            vals = u[3] if len(u) > 3 else ([0.0] * elem_floats) * ucount
            assert len(vals) == elem_floats * ucount, f"uniform {uname}: got {len(vals)} floats, expected {elem_floats * ucount}"
            out += struct.pack("<I", len(vals))
            out += struct.pack(f"<{len(vals)}f", *vals)

    out += pack_str(vs_name)
    out += pack_zstr(vs_src)
    out += pack_str(ps_name)
    out += pack_zstr(ps_src)

    out += struct.pack("<I", len(vs_uniform_indices))
    for idx in vs_uniform_indices:
        out += struct.pack("<I", idx)

    out += struct.pack("<I", len(ps_uniform_indices))
    for idx in ps_uniform_indices:
        out += struct.pack("<I", idx)
    return bytes(out)


def verify_ksh_roundtrip(blob: bytes) -> dict:
    off = 0

    def rd_u32():
        nonlocal off
        (v,) = struct.unpack_from("<I", blob, off)
        off += 4
        return v

    def rd_bytes():
        nonlocal off
        n = rd_u32()
        b = blob[off : off + n]
        off += n
        return b

    def rd_str():
        return rd_bytes().decode("ascii")

    name = rd_str()
    nu = rd_u32()
    uniforms = []
    for _ in range(nu):
        un = rd_str()
        sem = rd_str()
        ut = rd_u32()
        uc = rd_u32()
        if ut != UNIFORM_SAMPLER:
            nf = rd_u32()
            off += 4 * nf
        uniforms.append((un, sem, ut, uc))

    vs_name = rd_str()
    vs_raw = rd_bytes()
    assert vs_raw.endswith(b"\x00"), f"{name}: VS source missing NUL terminator"
    vs_src = vs_raw[:-1].decode("ascii")

    ps_name = rd_str()
    ps_raw = rd_bytes()
    assert ps_raw.endswith(b"\x00"), f"{name}: PS source missing NUL terminator"
    ps_src = ps_raw[:-1].decode("ascii")

    n_vsu = rd_u32()
    vs_idx = [rd_u32() for _ in range(n_vsu)]

    n_psu = rd_u32()
    ps_idx = [rd_u32() for _ in range(n_psu)]

    assert off == len(blob), f"Trailing bytes: {len(blob) - off}"
    return {
        "name": name,
        "uniforms": uniforms,
        "vs_name": vs_name,
        "ps_name": ps_name,
        "vs_idx": vs_idx,
        "ps_idx": ps_idx,
        "vs_src": vs_src,
        "ps_src": ps_src,
        "size": len(blob),
    }


# Stock Klei ndc.vs (for SamplerEffect passes)
VS_NDC = """attribute vec3 POSITION;
attribute vec2 TEXCOORD0;

varying vec2 PS_TEXCOORD;

void main()
{
\tgl_Position = vec4( POSITION.xyz, 1.0 );
\tPS_TEXCOORD = TEXCOORD0.xy;
}
"""

# Stock Klei postprocess_base.vs (for PostProcessEffect passes)
VS_POSTPROCESS = """attribute vec3 POSITION;
attribute vec2 TEXCOORD0;

varying vec2 PS_TEXCOORD0;

void main()
{
\tgl_Position = vec4( POSITION.xyz, 1.0 );
\tPS_TEXCOORD0 = TEXCOORD0.xy;
}
"""

# 1. Karis-weighted 5-tap Dual-Kawase downsample (anti-flicker + ultra-smooth halo)
#    Strips pure low-RG blue shadow projection markers (Contract v17) so shadows NEVER bloom!
PS_KAWASE_DOWN = """#if defined( GL_ES )
precision highp float;
#endif

uniform sampler2D SAMPLER[1];
uniform vec4 SAMPLER_PARAMS;

varying vec2 PS_TEXCOORD;

vec3 StripShadowMarker(vec3 c)
{
\tfloat rg = max(c.r, c.g);
\tfloat is_mk = step(rg + 0.005, c.b) * step(rg, 0.025);
\treturn c * (1.0 - is_mk);
}

float KarisWeight(vec3 c)
{
\tfloat luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
\treturn 1.0 / (1.0 + luma * 0.35);
}

void main()
{
\tvec2 uv = PS_TEXCOORD.xy;
\tvec2 hp = SAMPLER_PARAMS.zw * 0.75;

\tvec3 c0 = StripShadowMarker(texture2D(SAMPLER[0], uv).rgb);
\tvec3 c1 = StripShadowMarker(texture2D(SAMPLER[0], uv + vec2(-hp.x, -hp.y)).rgb);
\tvec3 c2 = StripShadowMarker(texture2D(SAMPLER[0], uv + vec2( hp.x, -hp.y)).rgb);
\tvec3 c3 = StripShadowMarker(texture2D(SAMPLER[0], uv + vec2(-hp.x,  hp.y)).rgb);
\tvec3 c4 = StripShadowMarker(texture2D(SAMPLER[0], uv + vec2( hp.x,  hp.y)).rgb);

\tfloat w0 = KarisWeight(c0) * 4.0;
\tfloat w1 = KarisWeight(c1);
\tfloat w2 = KarisWeight(c2);
\tfloat w3 = KarisWeight(c3);
\tfloat w4 = KarisWeight(c4);

\tvec3 sum = c0 * w0 + c1 * w1 + c2 * w2 + c3 * w3 + c4 * w4;
\tfloat wsum = max(w0 + w1 + w2 + w3 + w4, 0.0001);
\tgl_FragColor = vec4(sum / wsum, 1.0);
}
"""

# 2. Wide 3-level tent upsample for smooth wide atmospheric halo (Level 3 smoothed)
#    SAMPLER[0] = Level 1 (0.50x), SAMPLER[1] = Level 2 (0.25x), SAMPLER[2] = Level 3 (0.125x)
PS_KAWASE_UP = """#if defined( GL_ES )
precision highp float;
#endif

uniform sampler2D SAMPLER[3];
uniform vec4 SAMPLER_PARAMS;

varying vec2 PS_TEXCOORD;

vec3 Tent4(sampler2D tex, vec2 uv, vec2 off)
{
\tvec3 s = vec3(0.0);
\ts += texture2D(tex, uv + vec2(-off.x, -off.y)).rgb;
\ts += texture2D(tex, uv + vec2( off.x, -off.y)).rgb;
\ts += texture2D(tex, uv + vec2(-off.x,  off.y)).rgb;
\ts += texture2D(tex, uv + vec2( off.x,  off.y)).rgb;
\treturn s * 0.25;
}

void main()
{
\tvec2 uv = PS_TEXCOORD.xy;
\tvec2 px = SAMPLER_PARAMS.zw;

\tvec3 l1 = texture2D(SAMPLER[0], uv).rgb;
\tvec3 l2 = Tent4(SAMPLER[1], uv, px * 1.5);
\tvec3 l3 = Tent4(SAMPLER[2], uv, px * 3.5);

\tvec3 wide = l1 * 0.10 + l2 * 0.30 + l3 * 0.60;
\tgl_FragColor = vec4(wide, 1.0);
}
"""

# 3. Full Multi-Level Glow Pyramid & Atmospheric FX Composite
#    SAMPLER[0] = SRC_IMAGE
#    SAMPLER[1] = HS_KawaseDown1 (0.50x tight core)
#    SAMPLER[2] = HS_KawaseDown2 (0.25x mid halo)
#    SAMPLER[3] = HS_KawaseUp    (wide atmospheric tail)
#    HS_BLOOM_PARAMS = (strength, soft_knee, chroma_keep, screen_protect)
#    HS_BLOOM_EX     = (threshold, warmth, spread, glow_tail)
#    HS_BLOOM_FX     = (light_wrap, glow_rim, god_rays, bloom_compress)
#    HS_BLOOM_SUN    = (sun_u, sun_v, sun_vis, 0.0)
PS_BLOOM_COMPOSITE = """#if defined( GL_ES )
precision highp float;
#endif

uniform sampler2D SAMPLER[4];
uniform vec4 SCREEN_PARAMS;
uniform vec4 HS_BLOOM_PARAMS;
uniform vec4 HS_BLOOM_EX;
uniform vec4 HS_BLOOM_FX;
uniform vec4 HS_BLOOM_SUN;

#define SRC_IMAGE SAMPLER[0]
#define BL_L1     SAMPLER[1]
#define BL_L2     SAMPLER[2]
#define BL_L3     SAMPLER[3]

varying vec2 PS_TEXCOORD0;

float BLuma(vec3 c)
{
\treturn dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 SoftThresh(vec3 c, float thresh, float knee)
{
\tfloat br = max(c.r, max(c.g, c.b));
\tfloat k = max(knee, 0.02);
\tfloat soft = clamp(br - thresh + k, 0.0, 2.0 * k);
\tsoft = (soft * soft) / (4.0 * k + 1e-5);
\tfloat contrib = max(soft, br - thresh) / max(br, 1e-5);
\treturn c * max(contrib, 0.0);
}

void main()
{
\tvec2 uv = PS_TEXCOORD0.xy;
\tvec2 px = SCREEN_PARAMS.zw;
\tif (px.x <= 0.0 || px.y <= 0.0) {
\t\tpx = vec2(1.0 / 1280.0, 1.0 / 720.0);
\t}

\tvec3 src = texture2D(SRC_IMAGE, uv).rgb;
\tvec3 l1  = texture2D(BL_L1, uv).rgb;
\tvec3 l2  = texture2D(BL_L2, uv).rgb;
\tvec3 l3  = texture2D(BL_L3, uv).rgb;

\tfloat b_str      = max(HS_BLOOM_PARAMS.x, 0.0);
\tfloat b_knee     = clamp(HS_BLOOM_PARAMS.y, 0.0, 1.0);
\tfloat b_chr      = HS_BLOOM_PARAMS.z;
\tfloat b_prot     = clamp(HS_BLOOM_PARAMS.w, 0.0, 0.92);
\tfloat b_thresh   = clamp(HS_BLOOM_EX.x, 0.0, 1.0);
\tfloat b_warmth   = clamp(HS_BLOOM_EX.y, -1.0, 1.0);
\tfloat b_spread   = clamp(HS_BLOOM_EX.z, 0.0, 1.0);
\tfloat b_tail     = clamp(HS_BLOOM_EX.w, 0.0, 1.0);
\tfloat light_wrap = clamp(HS_BLOOM_FX.x, 0.0, 1.0);
\tfloat glow_rim   = clamp(HS_BLOOM_FX.y, 0.0, 1.0);
\tfloat god_rays   = clamp(HS_BLOOM_FX.z, 0.0, 2.0);
\tfloat b_comp     = clamp(HS_BLOOM_FX.w, 0.0, 1.0);

\tif (b_thresh > 0.001) {
\t\tl1 = SoftThresh(l1, b_thresh, b_knee * 0.45 + 0.05);
\t\tl2 = SoftThresh(l2, b_thresh * 0.85, b_knee * 0.45 + 0.05);
\t\tl3 = SoftThresh(l3, b_thresh * 0.70, b_knee * 0.45 + 0.05);
\t}

\tvec3 pal = mix(vec3(0.86, 0.95, 1.08), vec3(1.10, 0.93, 0.76), b_warmth * 0.5 + 0.5);
\tl1 *= mix(vec3(1.0), pal, 0.15);
\tl2 *= mix(vec3(1.0), pal, 0.45);
\tl3 *= mix(vec3(1.0), pal, 0.80);

\tif (b_tail > 0.001) {
\t\tfloat hl = BLuma(l3);
\t\tfloat tail = clamp(1.0 - hl * 4.0, 0.0, 1.0);
\t\tl3 *= 1.0 + b_tail * 0.65 * tail * tail;
\t}

\tvec3 w_tight = vec3(0.62, 0.26, 0.12);
\tvec3 w_wide  = vec3(0.20, 0.34, 0.46);
\tvec3 w = mix(w_tight, w_wide, b_spread);
\tvec3 blm = l1 * w.x + l2 * w.y + l3 * w.z;

\tfloat bl_luma = BLuma(blm);
\tblm = max(mix(vec3(bl_luma), blm, max(0.0, 1.0 + b_chr)), vec3(0.0));

\tvec3 scaled = blm * b_str;
\tvec3 soft_blm = scaled / (vec3(1.0) + scaled * max(b_knee, 0.0));
\tif (b_comp > 0.001) {
\t\tfloat sl = BLuma(soft_blm);
\t\tfloat sc = sl / (1.0 + sl * b_comp * 2.4);
\t\tsoft_blm *= mix(1.0, sc / max(sl, 1e-4), smoothstep(0.35, 1.10, sl));
\t}

\tvec3 result = src + soft_blm * (vec3(1.0) - src * b_prot);

\t// Optional dark-air atmospheric light wrap
\tif (light_wrap > 0.001) {
\t\tfloat src_l = clamp(BLuma(src), 0.0, 1.0);
\t\tfloat dark_m = (1.0 - src_l) * (1.0 - src_l);
\t\tresult += l3 * (light_wrap * 0.35 * dark_m * max(b_str, 0.35));
\t}

\t// Optional halo-gated 1-2px luminous silhouette rim
\tif (glow_rim > 0.001) {
\t\tfloat halo_g = clamp(BLuma(l2 + l3) * 2.4, 0.0, 1.0);
\t\tif (halo_g > 0.01) {
\t\t\tfloat yc = BLuma(src);
\t\t\tfloat y1 = BLuma(texture2D(SRC_IMAGE, uv + vec2( px.x * 1.4, 0.0)).rgb);
\t\t\tfloat y2 = BLuma(texture2D(SRC_IMAGE, uv + vec2(-px.x * 1.4, 0.0)).rgb);
\t\t\tfloat y3 = BLuma(texture2D(SRC_IMAGE, uv + vec2(0.0,  px.y * 1.4)).rgb);
\t\t\tfloat y4 = BLuma(texture2D(SRC_IMAGE, uv + vec2(0.0, -px.y * 1.4)).rgb);
\t\t\tfloat edge = smoothstep(0.04, 0.22, abs(y1 - y2) + abs(y3 - y4));
\t\t\tresult += (l2 + vec3(0.08, 0.06, 0.03)) * (edge * halo_g * glow_rim * 0.45 * (1.0 - yc * 0.5));
\t\t}
\t}

\t// Optional directional crepuscular sun shafts along sun screen vector
\tif (god_rays > 0.001 && HS_BLOOM_SUN.z > 0.01) {
\t\tvec2 sun_uv = HS_BLOOM_SUN.xy;
\t\tvec2 step_v = (sun_uv - uv) * 0.065;
\t\tvec3 ray_acc = vec3(0.0);
\t\tfloat decay = 1.0;
\t\tvec2 ruv = uv;
\t\tfor (int i = 0; i < 6; ++i) {
\t\t\truv += step_v;
\t\t\tvec3 rs = texture2D(BL_L3, clamp(ruv, vec2(0.0), vec2(1.0))).rgb;
\t\t\tray_acc += rs * decay;
\t\t\tdecay *= 0.80;
\t\t}
\t\tfloat rad_fall = clamp(1.15 - length(uv - sun_uv) * 0.75, 0.0, 1.0);
\t\tresult += ray_acc * pal * (0.045 * god_rays * HS_BLOOM_SUN.z * rad_fall) * (1.0 - src * 0.55);
\t}

\tgl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
"""

# 4. Single-pass Full Visual Studio Final Pass:
#    - Full Cinema Grading (Exposure/WB/Temp/Tint/ASC CDL Slope-Offset-Power/Lightness/Contrast/Gamma/
#      SunFill/ACES Filmic/Sat/Vibrance/HL_Desat/OriginalMix)
#    - Full BCAS Pro Sharpening + Ink Deconvolution (DeconvGate/DeconvPenetr) + Adaptive Bilateral
#      (RangeSigma/SpatialSigma/NoiseFloor) + AURA Envelope (AR_Threshold/AR_L_Over/AR_D_Over/ChromaProtect)
#    - Zero-Extra-Pass Soft Ground Shadow Composite (reads BloomSampler Contract v17 blue marker!)
#    - Vignette + Animated Film Grain (8Hz clock) + TPDF 8-bit Anti-Banding Dithering
#    Runs AFTER ColourCube and BEFORE UI -> never sharpens/tints HUD fonts or health badges!
#
#    SAMPLER[0]       = SRC_IMAGE (post-Bloom + post-ColourCube scene)
#    SAMPLER[1]       = BLOOM_RAW (SEB.BloomSampler for Contract v17 shadow marker)
#    HS_VISUAL_PARAMS = (bcas_str, deconv_str, dither_str, vig_str)
#    HS_SHARP_EX      = (anti_ring, dark_prot, chroma_prot, noise_reduce)
#    HS_SHARP_PRO     = (deconv_gate, deconv_penetr, range_sigma, noise_floor)
#    HS_AURA_EX       = (ar_thresh, ar_l_over, ar_d_over, spatial_sigma)
#    HS_GRADE_A       = (exposure_ev, wb_temp, wb_tint, saturation)
#    HS_GRADE_B       = (vibrance, contrast, filmic_aces, hl_desat)
#    HS_GRADE_C       = (lightness, gamma, sun_fill, original_mix)
#    HS_CDL_S         = (slope_r, slope_g, slope_b, grain_time)
#    HS_CDL_O         = (offset_r, offset_g, offset_b, shadow_soft)
#    HS_CDL_P         = (power_r, power_g, power_b, grain_str)
#    HS_SHADOW_CFG    = (shadow_tint_r, shadow_tint_g, shadow_tint_b, shadow_eff_density)
PS_VISUAL_FINAL = """#if defined( GL_ES )
precision highp float;
#endif

uniform sampler2D SAMPLER[2];
uniform vec4 SCREEN_PARAMS;
uniform vec4 HS_VISUAL_PARAMS;
uniform vec4 HS_SHARP_EX;
uniform vec4 HS_SHARP_PRO;
uniform vec4 HS_AURA_EX;
uniform vec4 HS_GRADE_A;
uniform vec4 HS_GRADE_B;
uniform vec4 HS_GRADE_C;
uniform vec4 HS_CDL_S;
uniform vec4 HS_CDL_O;
uniform vec4 HS_CDL_P;
uniform vec4 HS_SHADOW_CFG;

#define SRC_IMAGE SAMPLER[0]
#define BLOOM_RAW SAMPLER[1]

varying vec2 PS_TEXCOORD0;

vec3 ToLinear(vec3 c)
{
\tvec3 hi = pow((c + 0.055) / 1.055, vec3(2.4));
\treturn mix(c / 12.92, hi, step(vec3(0.04045), c));
}

vec3 ToGamma(vec3 c)
{
\tvec3 hi = 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
\treturn mix(c * 12.92, hi, step(vec3(0.0031308), c));
}

float Slt(float x, float s)
{
\ts = max(s, 1e-6);
\treturn s * x / (abs(x) + s);
}

float ShadowMarker(vec3 bm)
{
\tfloat d = max(bm.b - max(bm.r, bm.g), 0.0) / 0.06;
\treturn 1.0 - exp(-d * 1.45);
}

vec3 GradePixel(vec3 color, vec2 uv)
{
\tvec3 orig      = color;
\tfloat exp_ev   = HS_GRADE_A.x;
\tfloat wb_temp  = HS_GRADE_A.y;
\tfloat wb_tint  = HS_GRADE_A.z;
\tfloat sat_base = HS_GRADE_A.w;
\tfloat vib      = HS_GRADE_B.x;
\tfloat con      = HS_GRADE_B.y;
\tfloat filmic   = clamp(HS_GRADE_B.z, 0.0, 1.0);
\tfloat hl_desat = clamp(HS_GRADE_B.w, 0.0, 1.0);
\tfloat light_o  = HS_GRADE_C.x;
\tfloat gam      = max(HS_GRADE_C.y, 0.10);
\tfloat sun_fill = clamp(HS_GRADE_C.z, 0.0, 1.0);
\tfloat orig_mix = clamp(HS_GRADE_C.w, 0.0, 1.0);

\tcolor *= exp2(exp_ev);

\tvec3 wb = vec3(exp2(0.20 * wb_temp), exp2(0.12 * wb_tint), exp2(-0.20 * wb_temp));
\tcolor *= wb * (3.0 / (wb.r + wb.g + wb.b + 1e-6));

\t// ASC CDL (Slope / Offset / Power)
\tvec3 cdl = clamp(color * HS_CDL_S.xyz + HS_CDL_O.xyz, 0.0, 1.0);
\tcolor = pow(max(cdl, vec3(1e-6)), max(HS_CDL_P.xyz, vec3(0.10)));

\tcolor += light_o;
\tcolor = (color - 0.5) * (1.0 + con) + 0.5;
\tcolor = pow(max(color, vec3(0.0)), vec3(1.0 / gam));

\tif (sun_fill > 0.001) {
\t\tfloat lu = dot(color, vec3(0.2126, 0.7152, 0.0722));
\t\tfloat head = smoothstep(1.0, 0.45, lu);
\t\tfloat sg = clamp(1.35 - (uv.x * 0.55 + uv.y * 0.60), 0.0, 1.0);
\t\tsg = sg * sg;
\t\tvec3 st = vec3(1.12, 1.04, 0.92);
\t\tcolor = mix(color, color * st + vec3(0.045, 0.032, 0.014) * sg, sun_fill * sg * 0.42 * head);
\t}

\tif (filmic > 0.001) {
\t\tvec3 lin = ToLinear(clamp(color, 0.0, 1.0));
\t\tvec3 aces = (lin * (2.51 * lin + 0.03)) / (lin * (2.43 * lin + 0.59) + 0.14);
\t\tcolor = mix(color, ToGamma(clamp(aces, 0.0, 1.0)), filmic);
\t}

\tfloat yt = 0.25 * color.r + 0.5 * color.g + 0.25 * color.b;
\tfloat ct = color.g - 0.5 * (color.r + color.b);
\tfloat co = color.r - color.b;
\tfloat vib_m = clamp(1.0 - sqrt(co * co + ct * ct) / 0.6, 0.0, 1.0);
\tfloat wl = pow(clamp(4.0 * yt * (1.0 - yt), 0.0, 1.0), 1.1);
\tfloat luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
\tfloat hl_m = smoothstep(0.6, 1.0, luma);
\tfloat sf = (max(sat_base, 0.0) + vib * vib_m * wl) * (1.0 - hl_m * hl_desat);
\tcolor = mix(vec3(yt), color, sf);

\tif (orig_mix > 0.001) {
\t\tcolor = mix(color, orig, orig_mix);
\t}

\treturn clamp(color, 0.0, 1.0);
}

void main()
{
\tvec2 uv = PS_TEXCOORD0.xy;
\tvec2 px = SCREEN_PARAMS.zw;
\tif (px.x <= 0.0 || px.y <= 0.0) {
\t\tpx = vec2(1.0 / 1280.0, 1.0 / 720.0);
\t}

\t// Unconditional sample keeps SAMPLER[1] active across all ANGLE D3D9 / GLES2 paths!
\tvec3 bm0 = texture2D(BLOOM_RAW, uv).rgb;

\tfloat bcas_str      = max(HS_VISUAL_PARAMS.x, 0.0);
\tfloat deconv_str    = max(HS_VISUAL_PARAMS.y, 0.0);
\tfloat dither_str    = clamp(HS_VISUAL_PARAMS.z, 0.0, 2.5);
\tfloat vig_str       = clamp(HS_VISUAL_PARAMS.w, 0.0, 0.60);
\tfloat anti_ring     = clamp(HS_SHARP_EX.x, 0.0, 1.0);
\tfloat dark_prot     = clamp(HS_SHARP_EX.y, 0.0, 1.0);
\tfloat chroma_prot   = clamp(HS_SHARP_EX.z, 0.0, 1.0);
\tfloat noise_reduce  = clamp(HS_SHARP_EX.w, 0.0, 1.0);
\tfloat deconv_gate   = clamp(HS_SHARP_PRO.x, 0.005, 0.12);
\tfloat deconv_penetr = clamp(HS_SHARP_PRO.y, 0.0, 1.0);
\tfloat range_sigma   = clamp(HS_SHARP_PRO.z, 0.04, 2.0);
\tfloat noise_floor   = clamp(HS_SHARP_PRO.w, 0.001, 0.08);
\tfloat ar_thresh     = clamp(HS_AURA_EX.x, 0.0, 0.05);
\tfloat ar_l_over     = clamp(HS_AURA_EX.y, 0.0, 0.15);
\tfloat ar_d_over     = clamp(HS_AURA_EX.z, 0.0, 0.15);
\tfloat spatial_sig   = clamp(HS_AURA_EX.w, 0.20, 4.0);
\tfloat grain_time    = HS_CDL_S.w;
\tfloat shadow_soft   = clamp(HS_CDL_O.w, 0.4, 2.0);
\tfloat grain_str     = clamp(HS_CDL_P.w, 0.0, 1.0);
\tfloat shadow_dens   = clamp(HS_SHADOW_CFG.w, 0.0, 1.0);

\tfloat cdl_act = length(HS_CDL_S.xyz - vec3(1.0)) + length(HS_CDL_O.xyz) + length(HS_CDL_P.xyz - vec3(1.0));
\tfloat grade_act = abs(HS_GRADE_A.x) + abs(HS_GRADE_A.y) + abs(HS_GRADE_A.z)
\t\t+ abs(HS_GRADE_A.w - 1.0) + abs(HS_GRADE_B.x) + abs(HS_GRADE_B.y)
\t\t+ abs(HS_GRADE_B.z) + abs(HS_GRADE_B.w)
\t\t+ abs(HS_GRADE_C.x) + abs(HS_GRADE_C.y - 1.0) + abs(HS_GRADE_C.z) + cdl_act;
\tbool do_grade = grade_act > 0.001;

\tvec3 e = texture2D(SRC_IMAGE, uv).rgb;
\tif (do_grade) {
\t\te = GradePixel(e, uv);
\t}

\tvec3 color = e;
\tif (bcas_str > 0.001 || deconv_str > 0.001) {
\t\tvec3 b3 = texture2D(SRC_IMAGE, uv + vec2(0.0, -px.y)).rgb;
\t\tvec3 d3 = texture2D(SRC_IMAGE, uv + vec2(-px.x, 0.0)).rgb;
\t\tvec3 f3 = texture2D(SRC_IMAGE, uv + vec2( px.x, 0.0)).rgb;
\t\tvec3 h3 = texture2D(SRC_IMAGE, uv + vec2(0.0,  px.y)).rgb;
\t\tif (do_grade) {
\t\t\tb3 = GradePixel(b3, uv + vec2(0.0, -px.y));
\t\t\td3 = GradePixel(d3, uv + vec2(-px.x, 0.0));
\t\t\tf3 = GradePixel(f3, uv + vec2( px.x, 0.0));
\t\t\th3 = GradePixel(h3, uv + vec2(0.0,  px.y));
\t\t}

\t\tvec3 WY = vec3(0.25, 0.5, 0.25);
\t\tvec3 CO = vec3(1.0, 0.0, -1.0);
\t\tvec3 CG = vec3(-0.5, 1.0, -0.5);
\t\tvec4 nbrY = vec4(dot(b3, WY), dot(d3, WY), dot(f3, WY), dot(h3, WY));
\t\tvec4 Co4  = vec4(dot(b3, CO), dot(d3, CO), dot(f3, CO), dot(h3, CO));
\t\tvec4 Cg4  = vec4(dot(b3, CG), dot(d3, CG), dot(f3, CG), dot(h3, CG));

\t\tfloat Ye  = dot(e, WY);
\t\tfloat Coe = dot(e, CO);
\t\tfloat Cge = dot(e, CG);
\t\tvec4 diffY = nbrY - Ye;

\t\tfloat madY = dot(abs(diffY), vec4(0.25));
\t\tfloat mc = 0.5 * (dot(abs(Co4 - Coe), vec4(0.25)) + dot(abs(Cg4 - Cge), vec4(0.25)));
\t\tfloat eu  = clamp(1.0 - (abs(nbrY.x - nbrY.w) + abs(nbrY.y - nbrY.z)) / (4.0 * madY + 1e-6), 0.0, 1.0);
\t\tfloat iso = clamp((madY - 2.0 * min(madY, mc)) / max(madY, 1e-6), 0.0, 1.0);

\t\tfloat sg = clamp(range_sigma * (1.0 + 0.6 * (1.0 - clamp(madY / 0.05, 0.0, 1.0))), 0.01, 2.5);
\t\tfloat i2 = 0.5 / max(sg * sg, 1e-6);
\t\tfloat sp_w = exp(-0.5 / max(spatial_sig * spatial_sig, 0.04));
\t\tvec4 w4 = exp(-(diffY * diffY + vec4(1.5e-5)) * i2) * sp_w;
\t\tfloat Ybase = (dot(nbrY, w4) + Ye) / max(dot(w4, vec4(1.0)) + 1.0, 1e-6);

\t\tfloat lapY = dot(diffY, vec4(0.25));
\t\tfloat edgeAct = smoothstep(deconv_gate * 0.40, deconv_gate * 1.60, madY);
\t\tfloat darkPen = mix(smoothstep(0.05, 0.25, Ye), 1.0, deconv_penetr);
\t\tfloat inkDeepen = min(0.0, -lapY) * edgeAct * darkPen * (deconv_str * 2.2);

\t\tfloat det = (Ye - Ybase) * mix(1.0, 0.6, iso) * mix(1.0, 0.8, eu);
\t\tfloat nm  = mix(1.0 - noise_reduce, 1.0, clamp(madY / max(noise_floor, 0.001), 0.0, 1.0));
\t\tfloat cpg = 1.0 - chroma_prot * clamp(mc / (madY + 1e-6), 0.0, 1.0);

\t\tfloat s0 = 0.16 * (1.0 + 3.0 * clamp(madY / 0.05, 0.0, 1.0));
\t\ts0 *= (0.7 + 0.6 * (4.0 * Ye * (1.0 - Ye)));
\t\ts0 *= (0.75 + 0.25 * cpg);

\t\tfloat raw = det * (bcas_str * 1.15) * nm * cpg;
\t\tfloat y0 = Ye + Slt(max(0.0, raw), s0 * 0.72) + Slt(min(0.0, raw), s0 * 1.10);
\t\tfloat th = max(1e-6, 0.01 * dark_prot);
\t\tfloat y1 = mix(Ye, y0, clamp((y0 - th) / max(0.04, th), 0.0, 1.0));

\t\tfloat mn5 = min(Ye, min(min(nbrY.x, nbrY.y), min(nbrY.z, nbrY.w)));
\t\tfloat mx5 = max(Ye, max(max(nbrY.x, nbrY.y), max(nbrY.z, nbrY.w)));
\t\tfloat Ysharp = y1;
\t\tif (anti_ring > 0.001) {
\t\t\tfloat localSpan = mx5 - mn5;
\t\t\tfloat sd = y1 - Ye;
\t\t\tfloat ar_gate = smoothstep(ar_thresh, ar_thresh + 0.008, localSpan);
\t\t\tfloat brightLim = max(mx5 - Ye, localSpan * 0.24) + ar_l_over + 0.012;
\t\t\tfloat darkLim   = max(Ye - mn5, max(localSpan * 0.35, 0.05)) + ar_d_over + 0.018;
\t\t\tfloat clamped = Ye + Slt(max(0.0, sd), brightLim) + Slt(min(0.0, sd), darkLim);
\t\t\tclamped = min(clamped, mx5 + ar_l_over + min(localSpan * 0.22, 0.065));
\t\t\tYsharp = mix(y1, clamped, anti_ring * ar_gate);
\t\t}

\t\tYsharp += inkDeepen * nm * cpg;
\t\tfloat posCap = min(mx5 + ar_l_over + 0.075, Ye + 0.085 * clamp(bcas_str, 0.0, 1.5) + ar_l_over + 0.015);
\t\tYsharp = clamp(min(Ysharp, posCap), 0.0, 1.0);

\t\tfloat sc = 1.0;
\t\tif (Ye > 1e-6) {
\t\t\tsc = mix(1.0, clamp(Ysharp / Ye, 0.82, 1.18), smoothstep(0.0, 0.1, Ye));
\t\t}
\t\tfloat tmp = Ysharp - 0.5 * sc * Cge;
\t\tcolor = vec3(tmp + 0.5 * sc * Coe, sc * Cge + tmp, tmp - 0.5 * sc * Coe);
\t}

\t// Soft Ground Shadow Composite (AFTER sharpen -> 9-tap Vogel disk silky penumbra, zero pixel crawl!)
\tif (shadow_dens > 0.001) {
\t\tvec2 r1 = px * (0.65 + 1.45 * shadow_soft);
\t\tvec2 r2 = px * (1.30 + 2.85 * shadow_soft);
\t\tfloat mk0 = ShadowMarker(bm0);
\t\tfloat mk1 = ShadowMarker(texture2D(BLOOM_RAW, uv + vec2( r1.x,  r1.y * 0.55)).rgb);
\t\tfloat mk2 = ShadowMarker(texture2D(BLOOM_RAW, uv + vec2(-r1.x, -r1.y * 0.55)).rgb);
\t\tfloat mk3 = ShadowMarker(texture2D(BLOOM_RAW, uv + vec2(-r1.x * 0.55,  r1.y)).rgb);
\t\tfloat mk4 = ShadowMarker(texture2D(BLOOM_RAW, uv + vec2( r1.x * 0.55, -r1.y)).rgb);
\t\tfloat mk5 = ShadowMarker(texture2D(BLOOM_RAW, uv + vec2( r2.x * 0.88, -r2.y * 0.48)).rgb);
\t\tfloat mk6 = ShadowMarker(texture2D(BLOOM_RAW, uv + vec2(-r2.x * 0.88,  r2.y * 0.48)).rgb);
\t\tfloat mk7 = ShadowMarker(texture2D(BLOOM_RAW, uv + vec2( r2.x * 0.48,  r2.y * 0.88)).rgb);
\t\tfloat mk8 = ShadowMarker(texture2D(BLOOM_RAW, uv + vec2(-r2.x * 0.48, -r2.y * 0.88)).rgb);
\t\tfloat cov = (mk0 * 2.4 + (mk1 + mk2 + mk3 + mk4) * 1.15 + (mk5 + mk6 + mk7 + mk8) * 0.75) * 0.10;
\t\tcov = pow(clamp(cov, 0.0, 1.0), 1.0 / max(shadow_soft * 0.92, 0.35));
\t\tfloat luma_g = smoothstep(0.015, 0.14, dot(color, vec3(0.2126, 0.7152, 0.0722)));
\t\tcolor = mix(color, color * clamp(HS_SHADOW_CFG.rgb, vec3(0.0), vec3(1.0)), cov * min(shadow_dens * 1.12, 1.0) * luma_g);
\t}

\tif (vig_str > 0.001) {
\t\tvec2 vc = uv - 0.5;
\t\tcolor *= 1.0 - vig_str * smoothstep(0.35, 0.95, dot(vc, vc) * 2.4);
\t}

\tif (grain_str > 0.001) {
\t\tfloat gn = fract(sin(dot(gl_FragCoord.xy + vec2(grain_time * 13.7, grain_time * 7.3), vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
\t\tfloat gr_w = 1.0 - abs(dot(color, vec3(0.2126, 0.7152, 0.0722)) * 2.0 - 1.0);
\t\tcolor += vec3(gn * (0.075 * grain_str * (0.35 + 0.65 * gr_w)));
\t}

\tif (dither_str > 0.001) {
\t\tfloat d1 = fract(dot(gl_FragCoord.xy, vec2(12.9898, 78.233)) * 43758.5453);
\t\tfloat d2 = fract(dot(gl_FragCoord.yx, vec2(78.233, 12.9898)) * 24634.6345);
\t\tcolor += (d1 - d2) * (dither_str / 512.0);
\t}

\tgl_FragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
"""


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out_dir = os.path.join(root, "workshop_mod", "dst_smooth_highfps", "shaders")
    os.makedirs(out_dir, exist_ok=True)

    specs = [
        (
            "hs_kawase_down.ksh",
            "hs_kawase_down",
            [
                ("SAMPLER", UNIFORM_SAMPLER, 1),
                ("SAMPLER_PARAMS", UNIFORM_VEC4, 1, [0.0, 0.0, 0.0, 0.0]),
            ],
            "ndc.vs", VS_NDC,
            "hs_kawase_down.ps", PS_KAWASE_DOWN,
            [], [0, 1],
        ),
        (
            "hs_kawase_up.ksh",
            "hs_kawase_up",
            [
                ("SAMPLER", UNIFORM_SAMPLER, 3),
                ("SAMPLER_PARAMS", UNIFORM_VEC4, 1, [0.0, 0.0, 0.0, 0.0]),
            ],
            "ndc.vs", VS_NDC,
            "hs_kawase_up.ps", PS_KAWASE_UP,
            [], [0, 1],
        ),
        (
            "hs_postprocess_bloom.ksh",
            "hs_postprocess_bloom",
            [
                ("SAMPLER", UNIFORM_SAMPLER, 4),
                ("SCREEN_PARAMS", UNIFORM_VEC4, 1, [0.0, 0.0, 0.0, 0.0]),
                ("HS_BLOOM_PARAMS", UNIFORM_VEC4, 1, [0.70, 0.55, 0.12, 0.85]),
                ("HS_BLOOM_EX", UNIFORM_VEC4, 1, [0.04, 0.25, 0.55, 0.30]),
                ("HS_BLOOM_FX", UNIFORM_VEC4, 1, [0.0, 0.0, 0.0, 0.10]),
                ("HS_BLOOM_SUN", UNIFORM_VEC4, 1, [0.22, 0.14, 0.0, 0.0]),
            ],
            "postprocess_base.vs", VS_POSTPROCESS,
            "hs_postprocess_bloom.ps", PS_BLOOM_COMPOSITE,
            [], [0, 1, 2, 3, 4, 5],
        ),
        (
            "hs_visual_final.ksh",
            "hs_visual_final",
            [
                ("SAMPLER", UNIFORM_SAMPLER, 2),
                ("SCREEN_PARAMS", UNIFORM_VEC4, 1, [0.0, 0.0, 0.0, 0.0]),
                ("HS_VISUAL_PARAMS", UNIFORM_VEC4, 1, [0.0, 0.0, 1.0, 0.0]),
                ("HS_SHARP_EX", UNIFORM_VEC4, 1, [0.90, 0.25, 1.0, 0.65]),
                ("HS_SHARP_PRO", UNIFORM_VEC4, 1, [0.025, 0.80, 0.26, 0.01]),
                ("HS_AURA_EX", UNIFORM_VEC4, 1, [0.0, 0.001, 0.001, 1.10]),
                ("HS_GRADE_A", UNIFORM_VEC4, 1, [0.0, 0.0, 0.0, 1.0]),
                ("HS_GRADE_B", UNIFORM_VEC4, 1, [0.0, 0.0, 0.0, 0.0]),
                ("HS_GRADE_C", UNIFORM_VEC4, 1, [0.0, 1.0, 0.0, 0.0]),
                ("HS_CDL_S", UNIFORM_VEC4, 1, [1.0, 1.0, 1.0, 0.0]),
                ("HS_CDL_O", UNIFORM_VEC4, 1, [0.0, 0.0, 0.0, 1.15]),
                ("HS_CDL_P", UNIFORM_VEC4, 1, [1.0, 1.0, 1.0, 0.0]),
                ("HS_SHADOW_CFG", UNIFORM_VEC4, 1, [0.10, 0.12, 0.20, 0.0]),
            ],
            "postprocess_base.vs", VS_POSTPROCESS,
            "hs_visual_final.ps", PS_VISUAL_FINAL,
            [], [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12],
        ),
    ]

    for fname, name, uniforms, vsn, vsc, psn, psc, vsi, psi in specs:
        blob = build_ksh(name, uniforms, vsn, vsc, psn, psc, vsi, psi)
        info = verify_ksh_roundtrip(blob)
        out_path = os.path.join(out_dir, fname)
        with open(out_path, "wb") as f:
            f.write(blob)
        print(f"OK {fname:26s} {info['size']:5d}B  uniforms={len(info['uniforms'])}  ps_idx={info['ps_idx']}")


if __name__ == "__main__":
    main()
