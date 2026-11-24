#!/usr/bin/env python3
"""
HyperSmooth HS_EXT_V1 Shader Packer & OpenGL Validator (Zero External Dependencies)

Lets third-party DST visual/shader mod developers write ONLY pure fragment math:

    vec4 hs_main(vec2 uv, vec4 color)
    {
        // Built-ins provided automatically:
        //   SAMPLER[0] (alias SRC_IMAGE), SCREEN_PARAMS (w, h, 1/w, 1/h)
        //   U0..U3     (aliases to HSEXT_<ID>_U0..U3 packed from spec.params)
        //   HS_CTX_TIME / HS_CTX_SUN / HS_CTX_ENV / HS_CTX_CAM (if requested)
        return color;
    }

And emits a 100% Klei-compliant .ksh shader binary (verified 1:1 against stock
DST shaders.zip) with automatic `// @HS_EXT_ID:` tagging for HyperSmooth Native
hardware GL_TIME_ELAPSED GPU profiling.

Usage:
    python hs_pack_shader.py vintage_crt.frag \\
        --id demo.vintage_crt \\
        --out shaders/hs_ext_vintage_crt.ksh \\
        --vecs 2 \\
        --context TIME \\
        --validate-gl
"""

import argparse
import ctypes
import os
import re
import struct
import sys

UNIFORM_FLOAT   = 0x00
UNIFORM_VEC2    = 0x02
UNIFORM_VEC3    = 0x03
UNIFORM_VEC4    = 0x04
UNIFORM_MAT4    = 0x14
UNIFORM_SAMPLER = 0x2B

VALID_CONTEXTS = {
    "TIME": "HS_CTX_TIME",
    "SUN":  "HS_CTX_SUN",
    "ENV":  "HS_CTX_ENV",
    "CAM":  "HS_CTX_CAM",
}

VS_POSTPROCESS = """attribute vec3 POSITION;
attribute vec2 TEXCOORD0;

varying vec2 PS_TEXCOORD0;

void main()
{
\tgl_Position = vec4( POSITION.xyz, 1.0 );
\tPS_TEXCOORD0 = TEXCOORD0.xy;
}
"""

VS_SUBPASS = """attribute vec3 POSITION;
attribute vec2 TEXCOORD0;

varying vec2 PS_TEXCOORD;

void main()
{
\tgl_Position = vec4( POSITION.xyz, 1.0 );
\tPS_TEXCOORD = TEXCOORD0.xy;
}
"""


def sanitize_uniform_prefix(plugin_id: str) -> str:
    tok = re.sub(r"[^A-Z0-9]+", "_", plugin_id.upper()).strip("_")[:28]
    if not tok:
        tok = "PLUGIN"
    return f"HSEXT_{tok}_"


def pack_str(s: str) -> bytes:
    b = s.encode("ascii")
    return struct.pack("<I", len(b)) + b


def sanitize_glsl_ascii(s: str) -> str:
    # Strip non-ASCII chars inside // or /* */ comments so third-party authors can write Chinese comments freely
    return s.encode("ascii", "ignore").decode("ascii")


def pack_zstr(s: str) -> bytes:
    b = sanitize_glsl_ascii(s).encode("ascii") + b"\x00"
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
        out += pack_str("")  # semantic
        out += struct.pack("<II", utype, ucount)
        if utype != UNIFORM_SAMPLER:
            vals = u[3] if len(u) > 3 else [0.0, 0.0, 0.0, 0.0] * ucount
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


def verify_ksh_binary(blob: bytes) -> dict:
    off = 0

    def u32():
        nonlocal off
        (v,) = struct.unpack_from("<I", blob, off)
        off += 4
        return v

    def rd_bytes():
        nonlocal off
        n = u32()
        b = blob[off : off + n]
        off += n
        return b

    ename = rd_bytes().decode("ascii")
    nu = u32()
    unis = []
    for _ in range(nu):
        un = rd_bytes().decode("ascii")
        sem = rd_bytes().decode("ascii")
        ut = u32()
        cnt = u32()
        if ut != UNIFORM_SAMPLER:
            nf = u32()
            off += 4 * nf
        unis.append((un, ut, cnt))
    vsn = rd_bytes().decode("ascii")
    vs_raw = rd_bytes()
    assert vs_raw.endswith(b"\x00"), "VS missing NUL terminator"
    psn = rd_bytes().decode("ascii")
    ps_raw = rd_bytes()
    assert ps_raw.endswith(b"\x00"), "PS missing NUL terminator"
    nvs = u32()
    vsi = [u32() for _ in range(nvs)]
    nps = u32()
    psi = [u32() for _ in range(nps)]
    assert off == len(blob), f"Trailing bytes: {len(blob) - off}"
    return {
        "name": ename,
        "uniforms": unis,
        "vs_name": vsn,
        "vs_src": vs_raw[:-1].decode("ascii"),
        "ps_name": psn,
        "ps_src": ps_raw[:-1].decode("ascii"),
        "vs_idx": vsi,
        "ps_idx": psi,
        "size": len(blob),
    }


def wrap_math_fragment(
    user_glsl: str,
    plugin_id: str,
    prefix: str,
    samplers: int,
    vec_count: int,
    contexts: list,
    is_subpass: bool,
) -> str:
    varying = "PS_TEXCOORD" if is_subpass else "PS_TEXCOORD0"
    size_uni = "SAMPLER_PARAMS" if is_subpass else "SCREEN_PARAMS"
    lines = [
        f"// @HS_EXT_ID: {plugin_id}",
        "#if defined( GL_ES )",
        "precision highp float;",
        "#endif",
        "",
        f"uniform sampler2D SAMPLER[{samplers}];",
        f"uniform vec4 {size_uni};",
    ]
    for i in range(vec_count):
        lines.append(f"uniform vec4 {prefix}U{i};")
    for ck in contexts:
        lines.append(f"uniform vec4 {VALID_CONTEXTS[ck]};")

    lines.append("")
    lines.append("#define SRC_IMAGE SAMPLER[0]")
    for i in range(vec_count):
        lines.append(f"#define U{i} {prefix}U{i}")
    lines.append(f"varying vec2 {varying};")
    lines.append("")
    lines.append("// --- User HS_EXT_V1 Algorithm ---")
    lines.append(user_glsl.strip())
    lines.append("// --- End User Algorithm ---")
    lines.append("")
    lines.append("void main()")
    lines.append("{")
    lines.append(f"\tvec2 uv = {varying}.xy;")
    lines.append("\tvec4 src = texture2D(SAMPLER[0], uv);")
    lines.append("\tvec4 out_c = hs_main(uv, src);")
    # Keep declared uniforms statically referenced so ANGLE/GLES2 never drops inactive bindings
    keep_terms = [f"{size_uni}.x"]
    for i in range(vec_count):
        keep_terms.append(f"{prefix}U{i}.x")
    for ck in contexts:
        keep_terms.append(f"{VALID_CONTEXTS[ck]}.x")
    lines.append(f"\tfloat _hs_keep = ({' + '.join(keep_terms)}) * 0.0;")
    lines.append("\tgl_FragColor = vec4(clamp(out_c.rgb + _hs_keep, 0.0, 1.0), 1.0);")
    lines.append("}")
    return "\n".join(lines) + "\n"


def validate_native_opengl(info: dict) -> None:
    if sys.platform != "win32":
        print("[WARN] --validate-gl is Windows-only; skipping live GL compile check.")
        return
    from ctypes import wintypes

    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32
    opengl32 = ctypes.windll.opengl32

    class PFD(ctypes.Structure):
        _fields_ = [
            ("nSize", wintypes.WORD), ("nVersion", wintypes.WORD), ("dwFlags", wintypes.DWORD),
            ("iPixelType", ctypes.c_byte), ("cColorBits", ctypes.c_byte),
            ("cRedBits", ctypes.c_byte), ("cRedShift", ctypes.c_byte),
            ("cGreenBits", ctypes.c_byte), ("cGreenShift", ctypes.c_byte),
            ("cBlueBits", ctypes.c_byte), ("cBlueShift", ctypes.c_byte),
            ("cAlphaBits", ctypes.c_byte), ("cAlphaShift", ctypes.c_byte),
            ("cAccumBits", ctypes.c_byte), ("cAccumRedBits", ctypes.c_byte),
            ("cAccumGreenBits", ctypes.c_byte), ("cAccumBlueBits", ctypes.c_byte),
            ("cAccumAlphaBits", ctypes.c_byte), ("cDepthBits", ctypes.c_byte),
            ("cStencilBits", ctypes.c_byte), ("cAuxBuffers", ctypes.c_byte),
            ("iLayerType", ctypes.c_byte), ("bReserved", ctypes.c_byte),
            ("dwLayerMask", wintypes.DWORD), ("dwVisibleMask", wintypes.DWORD), ("dwDamageMask", wintypes.DWORD),
        ]

    user32.CreateWindowExW.restype = ctypes.c_void_p
    user32.GetDC.argtypes = [ctypes.c_void_p]
    user32.GetDC.restype = ctypes.c_void_p
    user32.ReleaseDC.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    user32.DestroyWindow.argtypes = [ctypes.c_void_p]
    gdi32.ChoosePixelFormat.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    gdi32.SetPixelFormat.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p]
    opengl32.wglCreateContext.restype = ctypes.c_void_p
    opengl32.wglCreateContext.argtypes = [ctypes.c_void_p]
    opengl32.wglDeleteContext.argtypes = [ctypes.c_void_p]
    opengl32.wglMakeCurrent.argtypes = [ctypes.c_void_p, ctypes.c_void_p]
    opengl32.wglGetProcAddress.restype = ctypes.c_void_p
    opengl32.wglGetProcAddress.argtypes = [ctypes.c_char_p]
    opengl32.glGetString.restype = ctypes.c_char_p
    opengl32.glGetString.argtypes = [ctypes.c_uint]

    hwnd = user32.CreateWindowExW(0, "STATIC", "hs_pack_gl", 0, 0, 0, 16, 16, 0, 0, 0, 0)
    hdc = user32.GetDC(hwnd)
    pfd = PFD(nSize=ctypes.sizeof(PFD), nVersion=1, dwFlags=0x25, cColorBits=32)
    pf = gdi32.ChoosePixelFormat(hdc, ctypes.byref(pfd))
    gdi32.SetPixelFormat(hdc, pf, ctypes.byref(pfd))

    hglrc = opengl32.wglCreateContext(hdc)
    assert opengl32.wglMakeCurrent(hdc, hglrc), "wglMakeCurrent failed"
    try:
        def glfn(name, rt, args):
            p = opengl32.wglGetProcAddress(name.encode())
            assert p, f"Missing GL function {name}"
            return ctypes.WINFUNCTYPE(rt, *args)(p)

        glCreateShader = glfn("glCreateShader", ctypes.c_uint, [ctypes.c_uint])
        glShaderSource = glfn("glShaderSource", None, [ctypes.c_uint, ctypes.c_int, ctypes.POINTER(ctypes.c_char_p), ctypes.POINTER(ctypes.c_int)])
        glCompileShader = glfn("glCompileShader", None, [ctypes.c_uint])
        glGetShaderiv = glfn("glGetShaderiv", None, [ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_int)])
        glGetShaderInfoLog = glfn("glGetShaderInfoLog", None, [ctypes.c_uint, ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.c_char_p])
        glCreateProgram = glfn("glCreateProgram", ctypes.c_uint, [])
        glAttachShader = glfn("glAttachShader", None, [ctypes.c_uint, ctypes.c_uint])
        glLinkProgram = glfn("glLinkProgram", None, [ctypes.c_uint])
        glGetProgramiv = glfn("glGetProgramiv", None, [ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(ctypes.c_int)])
        glGetProgramInfoLog = glfn("glGetProgramInfoLog", None, [ctypes.c_uint, ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.c_char_p])
        glGetUniformLocation = glfn("glGetUniformLocation", ctypes.c_int, [ctypes.c_uint, ctypes.c_char_p])

        def comp(stype, src):
            sh = glCreateShader(stype)
            arr = (ctypes.c_char_p * 1)(src.encode("ascii"))
            glShaderSource(sh, 1, arr, None)
            glCompileShader(sh)
            ok = ctypes.c_int(0)
            glGetShaderiv(sh, 0x8B81, ctypes.byref(ok))
            if ok.value != 1:
                buf = ctypes.create_string_buffer(4096)
                glGetShaderInfoLog(sh, 4096, None, buf)
                raise RuntimeError(f"GLSL compile error (0x{stype:X}):\n{buf.value.decode('utf-8', 'replace')}")
            return sh

        sv = comp(0x8B31, info["vs_src"])
        sf = comp(0x8B30, info["ps_src"])
        prog = glCreateProgram()
        glAttachShader(prog, sv)
        glAttachShader(prog, sf)
        glLinkProgram(prog)
        ok = ctypes.c_int(0)
        glGetProgramiv(prog, 0x8B82, ctypes.byref(ok))
        if ok.value != 1:
            buf = ctypes.create_string_buffer(4096)
            glGetProgramInfoLog(prog, 4096, None, buf)
            raise RuntimeError(f"GLSL link error:\n{buf.value.decode('utf-8', 'replace')}")

        active = []
        for un, ut, _ in info["uniforms"]:
            q = (un + "[0]") if ut == UNIFORM_SAMPLER else un
            loc = glGetUniformLocation(prog, q.encode("ascii"))
            if loc < 0:
                raise RuntimeError(f"Declared uniform '{q}' was optimized out / inactive!")
            active.append(f"{un}={loc}")
        renderer = opengl32.glGetString(0x1F01).decode("utf-8", "replace")
        print(f"[GL_OK] {renderer} | linked {len(active)}/{len(info['uniforms'])} uniforms ({', '.join(active)})")
    finally:
        opengl32.wglMakeCurrent(0, 0)
        opengl32.wglDeleteContext(hglrc)
        user32.ReleaseDC(hwnd, hdc)
        user32.DestroyWindow(hwnd)


def main():
    ap = argparse.ArgumentParser(description="Pack an HS_EXT_V1 GLSL algorithm into a Klei DST .ksh shader")
    ap.add_argument("input", help="Input .frag file implementing vec4 hs_main(vec2 uv, vec4 color)")
    ap.add_argument("--id", required=True, help="HS_EXT_V1 plugin ID (e.g. demo.vintage_crt)")
    ap.add_argument("--out", required=True, help="Output .ksh file path")
    ap.add_argument("--prefix", default=None, help="Optional custom uniform_prefix (default: HSEXT_<ID>_)")
    ap.add_argument("--vecs", type=int, default=1, help="Number of vec4 param uniforms U0..U(N-1) (0..4, default 1)")
    ap.add_argument("--samplers", type=int, default=1, help="Number of sampler2D slots in SAMPLER[] (1..7, default 1)")
    ap.add_argument("--context", default="", help="Comma-separated HS_EXT_V1 context uniforms: TIME,SUN,ENV,CAM")
    ap.add_argument("--subpass", action="store_true", help="Build as an offscreen SamplerEffect subpass (uses ndc.vs + SAMPLER_PARAMS)")
    ap.add_argument("--raw-ps", action="store_true", help="Treat input as complete Klei PS (only prepends @HS_EXT_ID tag if missing)")
    ap.add_argument("--validate-gl", action="store_true", help="Compile & link on local Windows OpenGL to verify zero GLSL errors")
    args = ap.parse_args()

    if not re.match(r"^[a-z0-9][a-z0-9_.-]{2,47}$", args.id):
        ap.error("--id must be 3..48 lowercase chars matching [a-z0-9_.-]")
    if not (0 <= args.vecs <= 4):
        ap.error("--vecs must be 0..4")
    if not (1 <= args.samplers <= 7):
        ap.error("--samplers must be 1..7")

    contexts = []
    if args.context.strip():
        for raw_c in args.context.split(","):
            ck = raw_c.strip().upper()
            if ck not in VALID_CONTEXTS:
                ap.error(f"Unknown context '{ck}' (valid: {', '.join(VALID_CONTEXTS)})")
            if ck not in contexts:
                contexts.append(ck)

    prefix = args.prefix or sanitize_uniform_prefix(args.id)
    with open(args.input, "r", encoding="utf-8") as f:
        user_src = f.read()

    if args.raw_ps:
        ps_src = user_src if "@HS_EXT_ID:" in user_src else f"// @HS_EXT_ID: {args.id}\n" + user_src
    else:
        ps_src = wrap_math_fragment(user_src, args.id, prefix, args.samplers, args.vecs, contexts, args.subpass)

    size_uni = "SAMPLER_PARAMS" if args.subpass else "SCREEN_PARAMS"
    uniforms = [
        ("SAMPLER", UNIFORM_SAMPLER, args.samplers),
        (size_uni, UNIFORM_VEC4, 1, [0.0, 0.0, 0.0, 0.0]),
    ]
    for i in range(args.vecs):
        uniforms.append((f"{prefix}U{i}", UNIFORM_VEC4, 1, [0.0, 0.0, 0.0, 0.0]))
    for ck in contexts:
        uniforms.append((VALID_CONTEXTS[ck], UNIFORM_VEC4, 1, [0.0, 0.0, 0.0, 0.0]))

    eff_name = os.path.splitext(os.path.basename(args.out))[0]
    vs_name = "ndc.vs" if args.subpass else "postprocess_base.vs"
    vs_src = VS_SUBPASS if args.subpass else VS_POSTPROCESS
    ps_name = eff_name + ".ps"
    ps_indices = list(range(len(uniforms)))

    blob = build_ksh(eff_name, uniforms, vs_name, vs_src, ps_name, ps_src, [], ps_indices)
    info = verify_ksh_binary(blob)
    if args.validate_gl:
        validate_native_opengl(info)

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(blob)
    print(f"[PACK_OK] {args.out} ({info['size']} B, id={args.id}, prefix={prefix}, uniforms={len(info['uniforms'])})")


if __name__ == "__main__":
    main()
