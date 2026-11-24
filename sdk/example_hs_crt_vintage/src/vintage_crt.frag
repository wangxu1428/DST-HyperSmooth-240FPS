// HyperSmooth HS_EXT_V1 Reference Algorithm: Vintage Cinema & CRT
// Author writes ONLY pure pixel math - zero Klei PostProcessor / UI / Clock boilerplate!
//
// Parameter Packing (auto-matched to modmain.lua spec.params):
//   U0.x = scanline   (0.0 .. 1.0)  CRT horizontal scanline strength
//   U0.y = curvature  (0.0 .. 0.35) Subtle CRT barrel screen curvature
//   U0.z = chroma_ab  (0.0 .. 1.0)  Subtle retro RGB lens edge dispersion
//   U0.w = sepia_warm (0.0 .. 1.0)  Warm vintage cinema film stock tint
//   U1.x = flicker    (0.0 | 1.0)   Optional subtle 60Hz film projector breath
//   HS_CTX_TIME.x     = real_time_sec (auto-fed by HyperSmooth Core)

vec4 hs_main(vec2 uv, vec4 color)
{
    float scanline   = U0.x;
    float curvature  = U0.y;
    float chroma_ab  = U0.z;
    float sepia_warm = U0.w;
    float flicker_on = U1.x;

    // 1. Optional gentle CRT glass barrel warp
    vec2 p = uv * 2.0 - 1.0;
    vec2 warped_uv = uv;
    float edge_mask = 1.0;
    if (curvature > 0.001) {
        vec2 off = p.yx * p.yx * (curvature * 0.45);
        vec2 wp = p * (1.0 + off);
        warped_uv = wp * 0.5 + 0.5;
        vec2 edge = smoothstep(vec2(0.0), vec2(0.012), warped_uv)
                  * smoothstep(vec2(0.0), vec2(0.012), vec2(1.0) - warped_uv);
        edge_mask = edge.x * edge.y;
        warped_uv = clamp(warped_uv, vec2(0.0), vec2(1.0));
    }

    // 2. Sample with optional radial chromatic aberration
    vec3 rgb;
    if (chroma_ab > 0.001) {
        vec2 ca = (warped_uv - 0.5) * (chroma_ab * 0.0045);
        rgb.r = texture2D(SRC_IMAGE, clamp(warped_uv + ca, vec2(0.0), vec2(1.0))).r;
        rgb.g = texture2D(SRC_IMAGE, warped_uv).g;
        rgb.b = texture2D(SRC_IMAGE, clamp(warped_uv - ca, vec2(0.0), vec2(1.0))).b;
    } else if (curvature > 0.001) {
        rgb = texture2D(SRC_IMAGE, warped_uv).rgb;
    } else {
        rgb = color.rgb;
    }

    // 3. Vintage cinema warm tint + soft phosphor glow lift
    if (sepia_warm > 0.001) {
        vec3 warm_mat = vec3(
            dot(rgb, vec3(1.06, 0.09, -0.04)),
            dot(rgb, vec3(0.04, 0.99,  0.02)),
            dot(rgb, vec3(-0.02, 0.05, 0.86))
        );
        rgb = mix(rgb, clamp(warm_mat, 0.0, 1.0), sepia_warm * 0.85);
    }

    // 4. Crisp resolution-aware scanlines
    if (scanline > 0.001) {
        float row = warped_uv.y * max(SCREEN_PARAMS.y, 360.0) * 1.5707963;
        float sl = 0.5 + 0.5 * sin(row);
        rgb *= mix(1.0, 0.62 + 0.38 * sl, scanline);
    }

    // 5. Optional subtle film projector flicker driven by HS_CTX_TIME
    if (flicker_on > 0.5) {
        float fl = 1.0 + 0.018 * sin(HS_CTX_TIME.x * 23.0) * sin(HS_CTX_TIME.x * 7.3);
        rgb *= fl;
    }

    return vec4(rgb * edge_mask, 1.0);
}
