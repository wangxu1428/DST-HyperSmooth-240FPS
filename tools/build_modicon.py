"""
Generates DST HyperSmooth & FastLoad v2.0.0 Workshop Cover (512x512 PNG)
and native Klei KTEX DXT5 (256x256, 9 mipmaps, flags=0xfffd2220) modicon.tex + modicon.xml.
"""

import math
import struct
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

OUT_MOD = Path(r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_fps_unlocker\workshop_mod\dst_smooth_highfps")
OUT_PREVIEW = Path(r"C:\Users\Administrator\.gemini\antigravity\scratch\dst_fps_unlocker\workshop_preview.png")


def rgb_to_565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def c565_to_rgb(c: int):
    r = ((c >> 11) & 31) * 255 // 31
    g = ((c >> 5) & 63) * 255 // 63
    b = (c & 31) * 255 // 31
    return r, g, b


def encode_dxt5_mip(img: Image.Image) -> bytes:
    """Encode an RGBA PIL image (V-flipped for OpenGL KTEX) into DXT5 blocks."""
    # KTEX stores OpenGL bottom-to-top rows!
    im = img.convert("RGBA").transpose(Image.FLIP_TOP_BOTTOM)
    w, h = im.size
    px = im.load()
    bw = max(1, (w + 3) // 4)
    bh = max(1, (h + 3) // 4)
    out = bytearray(bw * bh * 16)
    pos = 0

    for by in range(bh):
        for bx in range(bw):
            block = []
            alphas = []
            for dy in range(4):
                y = min(h - 1, by * 4 + dy)
                for dx in range(4):
                    x = min(w - 1, bx * 4 + dx)
                    r, g, b, a = px[x, y]
                    block.append((r, g, b))
                    alphas.append(a)

            # Alpha block (8 bytes): min/max endpoints
            a_max = max(alphas)
            a_min = min(alphas)
            out[pos] = a_max
            out[pos + 1] = a_min
            if a_max == a_min:
                a_bits = 0
            else:
                a_lut = [
                    a_max, a_min,
                    (6 * a_max + 1 * a_min) // 7,
                    (5 * a_max + 2 * a_min) // 7,
                    (4 * a_max + 3 * a_min) // 7,
                    (3 * a_max + 4 * a_min) // 7,
                    (2 * a_max + 5 * a_min) // 7,
                    (1 * a_max + 6 * a_min) // 7,
                ]
                a_bits = 0
                for idx, av in enumerate(alphas):
                    best_i = min(range(8), key=lambda i: abs(av - a_lut[i]))
                    a_bits |= (best_i << (idx * 3))
            out[pos + 2:pos + 8] = a_bits.to_bytes(6, "little")

            # Color block (8 bytes): bounding-box endpoints in RGB565
            min_r = min(c[0] for c in block)
            min_g = min(c[1] for c in block)
            min_b = min(c[2] for c in block)
            max_r = max(c[0] for c in block)
            max_g = max(c[1] for c in block)
            max_b = max(c[2] for c in block)

            c0 = rgb_to_565(max_r, max_g, max_b)
            c1 = rgb_to_565(min_r, min_g, min_b)
            if c0 < c1:
                c0, c1 = c1, c0

            r0, g0, b0 = c565_to_rgb(c0)
            r1, g1, b1 = c565_to_rgb(c1)
            if c0 == c1:
                c_bits = 0
            else:
                pal = [
                    (r0, g0, b0),
                    (r1, g1, b1),
                    ((2 * r0 + r1) // 3, (2 * g0 + g1) // 3, (2 * b0 + b1) // 3),
                    ((r0 + 2 * r1) // 3, (g0 + 2 * g1) // 3, (b0 + 2 * b1) // 3),
                ]
                c_bits = 0
                for idx, (r, g, b) in enumerate(block):
                    best_i = min(range(4), key=lambda i: (r - pal[i][0])**2 + (g - pal[i][1])**2 + (b - pal[i][2])**2)
                    c_bits |= (best_i << (idx * 2))

            struct.pack_into("<HHI", out, pos + 8, c0, c1, c_bits)
            pos += 16

    return bytes(out)


def load_font(size: int):
    for p in [
        r"C:\Windows\Fonts\segoeuib.ttf",
        r"C:\Windows\Fonts\arialbd.ttf",
        r"C:\Windows\Fonts\impact.ttf",
    ]:
        if Path(p).exists():
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def render_icon(size: int = 512) -> Image.Image:
    img = Image.new("RGBA", (size, size), (16, 18, 22, 255))
    draw = ImageDraw.Draw(img)

    # Radial dark parchment-to-emerald-obsidian backdrop
    cx, cy = size // 2, size // 2
    for r in range(int(size * 0.72), 0, -3):
        t = r / (size * 0.72)
        cr = int(20 + (1 - t) * 24)
        cg = int(24 + (1 - t) * 48)
        cb = int(28 + (1 - t) * 34)
        draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(cr, cg, cb, 255))

    # Glowing tachometer speed arc
    glow = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(glow)
    pad = int(size * 0.13)
    arc_box = (pad, pad, size - pad, size - pad)
    gdraw.arc(arc_box, start=145, end=395, fill=(75, 255, 165, 235), width=int(size * 0.042))
    gdraw.arc(arc_box, start=340, end=395, fill=(255, 205, 80, 255), width=int(size * 0.048))
    glow_blur = glow.filter(ImageFilter.GaussianBlur(radius=size * 0.025))
    img = Image.alpha_composite(img, glow_blur)
    img = Image.alpha_composite(img, glow)
    draw = ImageDraw.Draw(img)

    # Tick marks along tachometer arc
    r_outer = size * 0.365
    r_inner = size * 0.315
    for deg in range(150, 395, 22):
        rad = math.radians(deg)
        x0, y0 = cx + math.cos(rad) * r_inner, cy + math.sin(rad) * r_inner
        x1, y1 = cx + math.cos(rad) * r_outer, cy + math.sin(rad) * r_outer
        col = (255, 215, 100, 220) if deg >= 330 else (160, 255, 205, 180)
        draw.line((x0, y0, x1, y1), fill=col, width=max(2, size // 96))

    # DST-style gold double border frame
    b1 = int(size * 0.035)
    b2 = int(size * 0.058)
    draw.rounded_rectangle((b1, b1, size - b1, size - b1), radius=size // 14, outline=(212, 168, 86, 255), width=max(3, size // 64))
    draw.rounded_rectangle((b2, b2, size - b2, size - b2), radius=size // 18, outline=(120, 96, 52, 200), width=max(2, size // 128))

    # Main "240" bold typography with subtle drop shadow
    f_big = load_font(int(size * 0.28))
    f_sub = load_font(int(size * 0.068))
    f_tag = load_font(int(size * 0.058))

    txt_main = "240"
    bbox = draw.textbbox((0, 0), txt_main, font=f_big)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    tx, ty = cx - tw // 2, int(size * 0.255)
    draw.text((tx + 4, ty + 6), txt_main, font=f_big, fill=(0, 0, 0, 190))
    draw.text((tx, ty), txt_main, font=f_big, fill=(245, 255, 248, 255))

    txt_sub = "FPS · 2X LOAD"
    sb = draw.textbbox((0, 0), txt_sub, font=f_sub)
    sw = sb[2] - sb[0]
    draw.text((cx - sw // 2 + 2, int(size * 0.605) + 3), txt_sub, font=f_sub, fill=(0, 0, 0, 180))
    draw.text((cx - sw // 2, int(size * 0.605)), txt_sub, font=f_sub, fill=(105, 255, 178, 255))

    # Bottom pill badge
    pill_w, pill_h = int(size * 0.68), int(size * 0.115)
    px0, py0 = cx - pill_w // 2, int(size * 0.745)
    draw.rounded_rectangle((px0, py0, px0 + pill_w, py0 + pill_h), radius=pill_h // 2,
                           fill=(32, 36, 42, 235), outline=(212, 168, 86, 230), width=max(2, size // 128))
    txt_tag = "HYPER SMOOTH v2"
    tb = draw.textbbox((0, 0), txt_tag, font=f_tag)
    tagw, tagh = tb[2] - tb[0], tb[3] - tb[1]
    draw.text((cx - tagw // 2, py0 + (pill_h - tagh) // 2 - tb[1]), txt_tag, font=f_tag, fill=(255, 224, 140, 255))

    return img


def write_ktex_dxt5(img_256: Image.Image, out_tex: Path):
    # Exact flags from Klei modicon.tex: platform=0, fmt=2 (DXT5), type=1 (2D), mips=9 -> 0xfffd2220
    mips_data = []
    mip_headers = []
    cur = img_256.resize((256, 256), Image.Resampling.LANCZOS)
    for level in range(9):
        w = max(1, 256 >> level)
        h = max(1, 256 >> level)
        mimg = cur if level == 0 else cur.resize((w, h), Image.Resampling.LANCZOS)
        dxt = encode_dxt5_mip(mimg)
        pitch = max(1, (w + 3) // 4) * 16
        mip_headers.append(struct.pack("<HHHI", w, h, pitch, len(dxt)))
        mips_data.append(dxt)

    header = b"KTEX" + struct.pack("<I", 0xFFFD2220)
    with open(out_tex, "wb") as f:
        f.write(header)
        for mh in mip_headers:
            f.write(mh)
        for md in mips_data:
            f.write(md)


def main():
    img512 = render_icon(512)
    img512.save(OUT_PREVIEW)
    print(f"[OK] Saved 512x512 Workshop cover -> {OUT_PREVIEW}")

    img256 = img512.resize((256, 256), Image.Resampling.LANCZOS)
    tex_path = OUT_MOD / "modicon.tex"
    xml_path = OUT_MOD / "modicon.xml"
    write_ktex_dxt5(img256, tex_path)
    xml_path.write_text(
        '<Atlas><Texture filename="modicon.tex" />'
        '<Elements><Element name="modicon.tex" u1="0.001953125" u2="0.998046875" v1="0.001953125" v2="0.998046875" /></Elements></Atlas>\n',
        encoding="utf-8",
    )
    print(f"[OK] Saved native Klei KTEX DXT5 -> {tex_path} ({tex_path.stat().st_size} bytes)")
    print(f"[OK] Saved Atlas XML -> {xml_path}")


if __name__ == "__main__":
    main()
