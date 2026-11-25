"""
Generates workshop_mod/dst_smooth_highfps/images/hs_cloud_shade.tex
Native Klei KTEX DXT5 (256x256, 9 mipmaps, flags=0xfffd2220) matching images/tree.tex polarity:
  RGB = (0, 0, 0) everywhere
  Alpha = soft organic multi-octave cumulus cloud silhouette, strictly 0 at all borders
          so ShadeRenderer casts seamless drifting cloud shadows with zero edge artifacts.
"""

import math
import struct
from pathlib import Path
from PIL import Image, ImageFilter

from build_modicon import write_ktex_dxt5

OUT_TEX = Path(
    r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_fps_unlocker"
    r"\workshop_mod\dst_smooth_highfps\images\hs_cloud_shade.tex"
)


def hash2(ix: int, iy: int, seed: int) -> float:
    n = (ix * 374761393 + iy * 668265263 + seed * 1442695041) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    n = n ^ (n >> 16)
    return (n & 0xFFFF) / 65535.0


def value_noise(x: float, y: float, seed: int) -> float:
    ix = math.floor(x)
    iy = math.floor(y)
    fx = x - ix
    fy = y - iy
    ux = fx * fx * (3.0 - 2.0 * fx)
    uy = fy * fy * (3.0 - 2.0 * fy)
    a = hash2(ix, iy, seed)
    b = hash2(ix + 1, iy, seed)
    c = hash2(ix, iy + 1, seed)
    d = hash2(ix + 1, iy + 1, seed)
    return (a + (b - a) * ux) * (1.0 - uy) + (c + (d - c) * ux) * uy


def fbm(x: float, y: float, seed: int) -> float:
    val = 0.0
    amp = 0.52
    freq = 1.0
    norm = 0.0
    for octv in range(4):
        val += value_noise(x * freq, y * freq, seed + octv * 17) * amp
        norm += amp
        freq *= 2.05
        amp *= 0.48
    return val / norm


def smoothstep(e0: float, e1: float, x: float) -> float:
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def generate_cloud_image(size: int = 256) -> Image.Image:
    # Multi-puff cumulus cloud silhouette with zero hard border
    puffs = [
        (0.00,  0.00, 0.34, 0.27, 1.00),
        (-0.20, 0.06, 0.26, 0.22, 0.88),
        (0.21, -0.05, 0.27, 0.21, 0.86),
        (-0.07, -0.16, 0.24, 0.20, 0.78),
        (0.11,  0.16, 0.23, 0.19, 0.74),
    ]
    alpha_img = Image.new("L", (size, size), 0)
    apx = alpha_img.load()
    for py in range(size):
        ny = (py + 0.5) / size * 2.0 - 1.0  # [-1, 1]
        for px in range(size):
            nx = (px + 0.5) / size * 2.0 - 1.0
            # Edge safety mask: strictly 0 beyond radius 0.90
            r_edge = math.hypot(nx, ny)
            edge_mask = 1.0 - smoothstep(0.60, 0.90, r_edge)
            if edge_mask <= 0.001:
                apx[px, py] = 0
                continue

            # Organic multi-lobe base
            base = 0.0
            for cx, cy, rx, ry, w in puffs:
                d = math.hypot((nx - cx) / rx, (ny - cy) / ry)
                v = (1.0 - smoothstep(0.20, 1.15, d)) * w
                if v > base:
                    base = v

            # Soft wispy fBm modulation
            n = fbm((nx + 1.0) * 2.3, (ny + 1.0) * 2.3, seed=97)
            shaped = smoothstep(0.16, 0.88, base * (0.62 + 0.58 * n)) * edge_mask
            apx[px, py] = max(0, min(255, int(shaped * 238.0 + 0.5)))

    # Slight gaussian soften for velvet-smooth cloud penumbra
    alpha_img = alpha_img.filter(ImageFilter.GaussianBlur(radius=4.5))
    # Force outer 4px frame strictly to 0 to guarantee zero wrap/clamp seam
    apx = alpha_img.load()
    for i in range(size):
        for b in range(4):
            apx[i, b] = 0
            apx[i, size - 1 - b] = 0
            apx[b, i] = 0
            apx[size - 1 - b, i] = 0

    rgba = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    rgba.putalpha(alpha_img)
    return rgba


def main():
    OUT_TEX.parent.mkdir(parents=True, exist_ok=True)
    img = generate_cloud_image(256)
    write_ktex_dxt5(img, OUT_TEX)
    # Sanity check pixels
    px = img.load()
    print(f"[OK] Built {OUT_TEX} ({OUT_TEX.stat().st_size} bytes)")
    print(f"     corner={px[0,0]} border={px[2,128]} center={px[128,128]} quarter={px[80,128]}")
    assert px[0, 0] == (0, 0, 0, 0) and px[2, 128] == (0, 0, 0, 0)
    assert px[128, 128][0:3] == (0, 0, 0) and px[128, 128][3] > 180


if __name__ == "__main__":
    main()
