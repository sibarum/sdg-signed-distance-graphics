"""Makes the README's pictures from the conformance drawings, every one drawn by the C rasterizer (c/build/sdgrender).

    python docs/make-images.py          (after c/build.cmd; needs Pillow)
"""
import os, subprocess, sys, tempfile
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "c", "build", "sdgrender" + (".exe" if os.name == "nt" else ""))
DRAW = os.path.join(ROOT, "conformance", "drawings")
OUT = os.path.join(ROOT, "docs", "images")
TMP = tempfile.mkdtemp()

BG, CARD, EDGE = (13, 17, 23), (22, 27, 34), (48, 54, 61)
INK, DIM, ACCENT = (230, 237, 243), (139, 148, 158), (99, 212, 227)


def font(size, bold=False):
    for name in (["segoeuib.ttf", "DejaVuSans-Bold.ttf"] if bold else ["segoeui.ttf", "DejaVuSans.ttf"]):
        for d in (r"C:\Windows\Fonts", "/usr/share/fonts/truetype/dejavu"):
            p = os.path.join(d, name)
            if os.path.exists(p):
                return ImageFont.truetype(p, size)
    return ImageFont.load_default()


def sdg(name, *args):
    """Draws conformance/drawings/<name>.sdgb with sdgrender; args as sdgrender takes them after the out path."""
    out = os.path.join(TMP, f"{name}-{'-'.join(str(a) for a in args)}.png")
    subprocess.run([EXE, os.path.join(DRAW, name + ".sdgb"), out, *map(str, args)], check=True)
    return Image.open(out).convert("RGBA")


def canvas(w, h):
    im = Image.new("RGBA", (w, h), BG + (255,))
    ImageDraw.Draw(im).rounded_rectangle((0, 0, w - 1, h - 1), 18, fill=BG, outline=EDGE)
    return im


def card(im, box, label=None, sub=None):
    d = ImageDraw.Draw(im)
    d.rounded_rectangle(box, 12, fill=CARD, outline=EDGE)
    x0, y0, x1, y1 = box
    if label:
        d.text(((x0 + x1) / 2, y1 - (40 if sub else 24)), label, font=font(17, True), fill=INK, anchor="mm")
    if sub:
        d.text(((x0 + x1) / 2, y1 - 17), sub, font=font(14), fill=DIM, anchor="mm")


def paste(im, pic, cx, cy):
    im.alpha_composite(pic, (int(cx - pic.width / 2), int(cy - pic.height / 2)))


def save(im, name):
    im.convert("RGB").save(os.path.join(OUT, name), optimize=True)
    print("wrote", name)


def hero():
    names = ["vexelray-logo", "mainframe", "rasterware", "background-icon", "badge", "traced-badge", "file-icon"]
    s, pad, gap = 132, 28, 18
    w = pad * 2 + len(names) * s + (len(names) - 1) * gap
    im = canvas(w, s + pad * 2)
    for i, n in enumerate(names):
        x = pad + i * (s + gap)
        paste(im, sdg(n, s), x + s / 2, pad + s / 2)
    save(im, "hero.png")


def sizes():
    px = [16, 24, 32, 48, 64, 96, 128, 192]
    gap, pad = 26, 32
    w = pad * 2 + sum(px) + gap * (len(px) - 1)
    h = pad + max(px) + 56
    im = canvas(w, h)
    d = ImageDraw.Draw(im)
    x, base = pad, pad + max(px)
    for p in px:
        paste(im, sdg("vexelray-logo", p), x + p / 2, base - p / 2)
        d.text((x + p / 2, base + 26), f"{p}", font=font(15), fill=DIM, anchor="mm")
        x += p + gap
    save(im, "sizes.png")


def zoom():
    # The VexelRay mark's canvas is 100 units. At 64 px a pixel is 1.5625 units; the 32 px from column 19, top row,
    # cover the sphere and the tops of the V. The SDG draws the same 50 units straight into 512 px.
    n, small_px, x0, side, zoomed = "vexelray-logo", 64, 19, 32, 512
    upp = 100 / small_px
    small = sdg(n, small_px)
    crop = small.crop((x0, 0, x0 + side, side))
    near = crop.resize((zoomed, zoomed), Image.NEAREST)
    bil = crop.resize((zoomed, zoomed), Image.BILINEAR)
    vec = sdg(n, zoomed, zoomed, x0 * upp, 0, side * upp / zoomed)
    kb = os.path.getsize(os.path.join(DRAW, n + ".sdgb")) / 1000
    times = zoomed // side
    pad, gap, foot = 28, 22, 70
    w = pad * 2 + 3 * zoomed + 2 * gap
    im = canvas(w, pad + zoomed + foot)
    for i, (pic, label, sub) in enumerate([
        (near, f"A {small_px} px PNG, {times}×", "nearest neighbour"),
        (bil, f"A {small_px} px PNG, {times}×", "bilinear"),
        (vec, f"The SDG, {times}×", f"drawn at {zoomed} px from the {kb:.1f} kB file"),
    ]):
        x = pad + i * (zoomed + gap)
        bg = Image.new("RGBA", (zoomed, zoomed), CARD + (255,))
        bg.alpha_composite(pic)
        im.alpha_composite(bg, (x, pad))
        ImageDraw.Draw(im).rectangle((x - 1, pad - 1, x + zoomed, pad + zoomed), outline=ACCENT if i == 2 else EDGE)
        d = ImageDraw.Draw(im)
        d.text((x + zoomed / 2, pad + zoomed + 24), label, font=font(18, True), fill=INK, anchor="mm")
        d.text((x + zoomed / 2, pad + zoomed + 48), sub, font=font(15), fill=DIM, anchor="mm")
    save(im, "zoom.png")


def groups():
    cases = [("group-opacity", "Opacity", "one 50% over the overlap"),
             ("group-clip", "Clip", "a square, clipped to a circle"),
             ("group-mask", "Mask", "luminance, half and quarter"),
             ("group-nested", "All three, nested", "opacity · clip · mask")]
    s, pad, gap, foot = 200, 24, 18, 64
    cw = s + 40
    w = pad * 2 + len(cases) * cw + (len(cases) - 1) * gap
    im = canvas(w, pad * 2 + s + 20 + foot)
    for i, (n, label, sub) in enumerate(cases):
        x = pad + i * (cw + gap)
        card(im, (x, pad, x + cw, pad + s + 20 + foot), label, sub)
        paste(im, sdg(n, s), x + cw / 2, pad + 10 + s / 2)
    save(im, "groups.png")


if not os.path.exists(EXE):
    sys.exit(f"build the C rasterizer first: {EXE} is missing")
os.makedirs(OUT, exist_ok=True)
hero(); sizes(); zoom(); groups()
