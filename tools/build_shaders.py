#!/usr/bin/env python3
"""
Compile DST HyperSmooth Visual Fidelity Tier-1 GLSL ES 1.00 shaders into native
Klei .ksh binary format (schema verified 70/70 exact-byte match against vanilla shaders.zip).

Generated shaders (workshop_mod/dst_smooth_highfps/shaders/):
  1. hs_kawase_down.ksh       - Karis-weighted 5-tap Dual-Kawase downsample (anti-firefly shimmer)
  2. hs_kawase_up.ksh         - 3-level Dual-Kawase pyramid tent combine (0.5x + 0.25x + 0.125x)
  3. hs_postprocess_bloom.ksh - Soft-knee filmic Bloom composite (eliminates vanilla white blowout)
  4. hs_visual_final.ksh      - Hand-drawn-safe FidelityFX CAS + Vibrance + Vignette + IGN Dithering
"""

import os
import struct
import sys

UNIFORM_FLOAT   = 0x00
UNIFORM_VEC2    = 0x02
UNIFORM_VEC3    = 0x03
UNIFORM_VEC4    = 0x04
UNIFORM_MAT4    = 0x14
UNIFORM_SAMPLER = 0x2B

UNIFORM_FLOATS = {
    UNIFORM_FLOAT: 1,
    UNIFORM_VEC2:  2,
    UNIFORM_VEC3:  3,
    UNIFORM_VEC4:  4,
    UNIFORM_MAT4:  16,
}


def pack_str(s: str) -> bytes:
    b = s.encode("utf-8")
    return struct.pack("<I", len(b)) + b


def build_ksh(name: str, uniforms: list, vs_name: str, vs_code: str,
              ps_name: str, ps_code: str, vs_u_indices: list, ps_u_indices: list) -> bytes:
    """
    uniforms: list of tuples (uname, utype, count[, default_floats])
    """
    out = bytearray()
    out += pack_str(name)
    out += struct.pack("<I", len(uniforms))
    for u in uniforms:
        uname, utype, ucnt = u[0], u[1], u[2]
        out += pack_str(uname)
        out += struct.pack("<III", 0, utype, ucnt)
        if utype != UNIFORM_SAMPLER:
            nf = UNIFORM_FLOATS[utype] * ucnt
            defaults = list(u[3]) if len(u) > 3 else [0.0] * nf
            assert len(defaults) == nf, f"Uniform {uname} expects {nf} floats, got {len(defaults)}"
            out += struct.pack("<I", nf)
            out += struct.pack(f"<{nf}f", *defaults)
    # Match vanilla CRLF line endings + trailing NUL byte!
    # CRITICAL: dontstarve_steam_x64.exe passes .ksh shader code directly to
    # glShaderSource(sh, 1, &ptr, NULL), so every vs_code / ps_code MUST end with '\0'.
    vs_norm = vs_code.replace("\r\n", "\n").replace("\n", "\r\n") + "\x00"
    ps_norm = ps_code.replace("\r\n", "\n").replace("\n", "\r\n") + "\x00"
    out += pack_str(vs_name)
    out += pack_str(vs_norm)
    out += pack_str(ps_name)
    out += pack_str(ps_norm)
    out += struct.pack("<I", len(vs_u_indices))
    if vs_u_indices:
        out += struct.pack(f"<{len(vs_u_indices)}I", *vs_u_indices)
    out += struct.pack("<I", len(ps_u_indices))
    if ps_u_indices:
        out += struct.pack(f"<{len(ps_u_indices)}I", *ps_u_indices)
    return bytes(out)


def verify_ksh_roundtrip(buf: bytes) -> dict:
    o = 0
    def g32():
        nonlocal o
        v = struct.unpack_from("<I", buf, o)[0]
        o += 4
        return v
    def gs():
        nonlocal o
        n = g32()
        s = buf[o:o + n].decode("utf-8")
        o += n
        return s

    sname = gs()
    nu = g32()
    us = []
    for _ in range(nu):
        un = gs()
        f1, f2, f3 = struct.unpack_from("<III", buf, o)
        o += 12
        assert f1 == 0
        if f2 != UNIFORM_SAMPLER:
            nf = g32()
            vals = struct.unpack_from(f"<{nf}f", buf, o)
            o += 4 * nf
        else:
            vals = ()
        us.append((un, f2, f3, vals))
    vsn = gs()
    vsc = gs()
    psn = gs()
    psc = gs()
    assert vsc.endswith("\x00") and psc.endswith("\x00"), f"{sname}: shader source missing trailing NUL!"
    nvs = g32()
    vsi = struct.unpack_from(f"<{nvs}I", buf, o) if nvs else ()
    o += 4 * nvs
    nps = g32()
    psi = struct.unpack_from(f"<{nps}I", buf, o) if nps else ()
    o += 4 * nps
    assert o == len(buf), f"Byte length mismatch: consumed={o}, total={len(buf)}"
    for idx in vsi + psi:
        assert 0 <= idx < nu, f"Uniform index out of range: {idx} >= {nu}"
    return {
        "name": sname, "uniforms": [u[0] for u in us],
        "vs": vsn, "ps": psn, "size": len(buf),
        "vs_idx": vsi, "ps_idx": psi,
    }


VS_POSTPROCESS = """attribute vec3 POSITION;
attribute vec2 TEXCOORD0;

varying vec2 PS_TEXCOORD0;

void main()
{
\tgl_Position = vec4( POSITION.xyz, 1.0 );
\tPS_TEXCOORD0.xy = TEXCOORD0.xy;
}
"""

VS_NDC = """attribute vec3 POSITION;
attribute vec2 TEXCOORD0;

varying vec2 PS_TEXCOORD;

void main()
{
\tgl_Position = vec4( POSITION.xyz, 1.0 );
\tPS_TEXCOORD.xy = TEXCOORD0.xy;
}
"""

# 1. Karis-weighted 5-tap Dual-Kawase downsample (anti-flicker + ultra-smooth halo)
PS_KAWASE_DOWN = """#if defined( GL_ES )
precision highp float;
#endif

uniform sampler2D SAMPLER[1];
uniform vec4 SAMPLER_PARAMS;

varying vec2 PS_TEXCOORD;

float KarisWeight(vec3 c)
{
\tfloat luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
\treturn 1.0 / (1.0 + luma * 0.35);
}

void main()
{
\tvec2 uv = PS_TEXCOORD.xy;
\tvec2 hp = SAMPLER_PARAMS.zw * 0.75;

\tvec3 c0 = texture2D(SAMPLER[0], uv).rgb;
\tvec3 c1 = texture2D(SAMPLER[0], uv + vec2(-hp.x, -hp.y)).rgb;
\tvec3 c2 = texture2D(SAMPLER[0], uv + vec2( hp.x, -hp.y)).rgb;
\tvec3 c3 = texture2D(SAMPLER[0], uv + vec2(-hp.x,  hp.y)).rgb;
\tvec3 c4 = texture2D(SAMPLER[0], uv + vec2( hp.x,  hp.y)).rgb;

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

# 2. Multi-frequency Dual-Kawase 3-level tent upsample & pyramid combine
#    SAMPLER[0] = Level 1 (0.50x tight core)
#    SAMPLER[1] = Level 2 (0.25x mid halo)
#    SAMPLER[2] = Level 3 (0.125x wide atmospheric glow)
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

\tvec3 combined = l1 * 0.44 + l2 * 0.34 + l3 * 0.28;
\tgl_FragColor = vec4(combined, 1.0);
}
"""

# 3. Soft-knee filmic Bloom composite (replaces vanilla harsh linear SRC + BLOOM)
#    HS_BLOOM_PARAMS = (strength, soft_knee, chroma_keep, screen_protect)
PS_BLOOM_COMPOSITE = """#if defined( GL_ES )
precision highp float;
#endif

uniform sampler2D SAMPLER[2];
uniform vec4 HS_BLOOM_PARAMS;

#define SRC_IMAGE    SAMPLER[0]
#define BLOOM_BUFFER SAMPLER[1]

varying vec2 PS_TEXCOORD0;

void main()
{
\tvec3 src = texture2D(SRC_IMAGE, PS_TEXCOORD0.xy).rgb;
\tvec3 blm = texture2D(BLOOM_BUFFER, PS_TEXCOORD0.xy).rgb;

\tfloat blm_luma = dot(blm, vec3(0.2126, 0.7152, 0.0722));
\tblm = max(mix(vec3(blm_luma), blm, 1.0 + HS_BLOOM_PARAMS.z), vec3(0.0));

\tvec3 scaled = blm * HS_BLOOM_PARAMS.x;
\tvec3 soft_blm = scaled / (vec3(1.0) + scaled * max(HS_BLOOM_PARAMS.y, 0.0));

\tvec3 result = src + soft_blm * (vec3(1.0) - src * clamp(HS_BLOOM_PARAMS.w, 0.0, 0.92));
\tgl_FragColor = vec4(clamp(result, 0.0, 1.0), 1.0);
}
"""

# 4. Hand-drawn-safe FidelityFX CAS + restrained Vibrance + subtle Vignette + IGN 8-bit Dithering
#    Runs AFTER ColourCube and BEFORE UI -> never sharpens/blurs HUD fonts or health badges!
#    HS_VISUAL_PARAMS = (cas_strength, cinematic_grade, dither_strength, vignette_strength)
PS_VISUAL_FINAL = """#if defined( GL_ES )
precision highp float;
#endif

uniform sampler2D SAMPLER[1];
uniform vec4 SCREEN_PARAMS;
uniform vec4 HS_VISUAL_PARAMS;

#define SRC_IMAGE SAMPLER[0]

varying vec2 PS_TEXCOORD0;

void main()
{
\tvec2 uv = PS_TEXCOORD0.xy;
\tvec2 px = SCREEN_PARAMS.zw;

\tvec3 c = texture2D(SRC_IMAGE, uv).rgb;
\tvec3 n = texture2D(SRC_IMAGE, uv + vec2(0.0, -px.y)).rgb;
\tvec3 s = texture2D(SRC_IMAGE, uv + vec2(0.0,  px.y)).rgb;
\tvec3 w = texture2D(SRC_IMAGE, uv + vec2(-px.x, 0.0)).rgb;
\tvec3 e = texture2D(SRC_IMAGE, uv + vec2( px.x, 0.0)).rgb;

\tfloat cas_amt = clamp(HS_VISUAL_PARAMS.x, 0.0, 1.0);
\tvec3 luma_w = vec3(0.299, 0.587, 0.114);
\tfloat lc = dot(c, luma_w);
\tfloat ln = dot(n, luma_w);
\tfloat ls = dot(s, luma_w);
\tfloat lw = dot(w, luma_w);
\tfloat le = dot(e, luma_w);

\tfloat mn = min(lc, min(min(ln, ls), min(lw, le)));
\tfloat mx = max(lc, max(max(ln, ls), max(lw, le)));

\tfloat amp = sqrt(clamp(min(mn, 1.0 - mx) / max(mx, 0.0001), 0.0, 1.0));
\tfloat peak = -1.0 / mix(8.0, 5.0, cas_amt);
\tfloat w_cas = amp * peak * step(0.001, cas_amt);

\tvec3 cas_col = (c + (n + s + w + e) * w_cas) / (1.0 + 4.0 * w_cas);
\tvec3 env_min = min(c, min(min(n, s), min(w, e)));
\tvec3 env_max = max(c, max(max(n, s), max(w, e)));
\tvec3 col = mix(c, clamp(cas_col, env_min, env_max), step(0.001, cas_amt));

\tfloat grade = clamp(HS_VISUAL_PARAMS.y, 0.0, 1.0);
\tfloat cmax = max(col.r, max(col.g, col.b));
\tfloat cmin = min(col.r, min(col.g, col.b));
\tfloat sat = cmax - cmin;
\tfloat l = dot(col, vec3(0.2126, 0.7152, 0.0722));
\tfloat hi_guard = 1.0 - smoothstep(0.74, 0.96, l);
\tfloat vib = grade * 0.20 * (1.0 - sat) * hi_guard;
\tcol = mix(vec3(l), col, 1.0 + vib);

\tfloat vig_amt = clamp(HS_VISUAL_PARAMS.w, 0.0, 0.25);
\tvec2 d = (uv - vec2(0.5, 0.5)) * vec2(1.12, 1.0);
\tfloat vig = smoothstep(0.42, 0.98, dot(d, d));
\tcol *= 1.0 - vig * vig_amt;

\tfloat dither = clamp(HS_VISUAL_PARAMS.z, 0.0, 2.5);
\tfloat ign = fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
\tcol += (ign - 0.5) * (dither / 255.0);

\tgl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
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
                ("SAMPLER", UNIFORM_SAMPLER, 2),
                ("HS_BLOOM_PARAMS", UNIFORM_VEC4, 1, [0.95, 0.45, 0.15, 0.38]),
            ],
            "postprocess_base.vs", VS_POSTPROCESS,
            "hs_postprocess_bloom.ps", PS_BLOOM_COMPOSITE,
            [], [0, 1],
        ),
        (
            "hs_visual_final.ksh",
            "hs_visual_final",
            [
                ("SAMPLER", UNIFORM_SAMPLER, 1),
                ("SCREEN_PARAMS", UNIFORM_VEC4, 1, [0.0, 0.0, 0.0, 0.0]),
                ("HS_VISUAL_PARAMS", UNIFORM_VEC4, 1, [0.42, 0.0, 1.5, 0.0]),
            ],
            "postprocess_base.vs", VS_POSTPROCESS,
            "hs_visual_final.ps", PS_VISUAL_FINAL,
            [], [0, 1, 2],
        ),
    ]

    for fname, name, uniforms, vsn, vsc, psn, psc, vsi, psi in specs:
        blob = build_ksh(name, uniforms, vsn, vsc, psn, psc, vsi, psi)
        info = verify_ksh_roundtrip(blob)
        out_path = os.path.join(out_dir, fname)
        with open(out_path, "wb") as f:
            f.write(blob)
        print(f"OK {fname:26s} {info['size']:5d}B  uniforms={info['uniforms']}  ps_idx={info['ps_idx']}")


if __name__ == "__main__":
    main()
