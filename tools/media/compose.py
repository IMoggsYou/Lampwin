"""Builds the README images in docs/media from settings-window snapshots.

    MacWin.Settings.exe --snapshot <shots>\\en-light --lang en --theme light --transparent
    (also en-dark --theme dark, and ja, ar, hi, pl light), --export-icon <shots>\\icon.png
    MacWin.exe --widget-snapshot <shots>\\widgets-dark --lang en --theme dark
    python tools/media/compose.py <shots>

Take the snapshots while MacWin is running (the sidebar shows its state), with a few
icons given to programs on the App Icons page.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

SHOTS = sys.argv[1]
OUT = os.path.join(os.path.dirname(__file__), "..", "..", "docs", "media")
FONTS = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts")


def font(name, size):
    return ImageFont.truetype(os.path.join(FONTS, name), size)


def gradient(w, h, stops, blobs=()):
    """Diagonal gradient through `stops` plus soft colour blobs, like a desktop wallpaper."""
    base = Image.new("RGB", (w, h))
    px = base.load()
    for y in range(h):
        for x in range(w):
            t = (x / w * 0.6 + y / h * 0.4)
            seg = min(int(t * (len(stops) - 1)), len(stops) - 2)
            f = t * (len(stops) - 1) - seg
            a, b = stops[seg], stops[seg + 1]
            px[x, y] = tuple(int(a[i] + (b[i] - a[i]) * f) for i in range(3))
    for (cx, cy, r, color, alpha) in blobs:
        layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(layer).ellipse((cx - r, cy - r, cx + r, cy + r), fill=color + (alpha,))
        base.paste(layer.filter(ImageFilter.GaussianBlur(r * 0.6)), (0, 0), layer.filter(ImageFilter.GaussianBlur(r * 0.6)))
    return base


def with_shadow(canvas, window, x, y, blur=34, offset=22, opacity=110):
    alpha = window.split()[3]
    shadow = Image.new("RGBA", window.size, (0, 0, 0, 0))
    shadow.putalpha(alpha.point(lambda a: a * opacity // 255))
    pad = blur * 3
    big = Image.new("RGBA", (window.width + 2 * pad, window.height + 2 * pad), (0, 0, 0, 0))
    big.paste(shadow, (pad, pad))
    big = big.filter(ImageFilter.GaussianBlur(blur))
    canvas.alpha_composite(big, (x - pad, y - pad + offset))
    canvas.alpha_composite(window, (x, y))


def shot(name, page):
    return Image.open(os.path.join(SHOTS, name, f"page{page}.png")).convert("RGBA")


def save(img, name):
    path = os.path.join(OUT, name)
    img.convert("RGB").save(path, optimize=True)
    print(name, img.size, os.path.getsize(path) // 1024, "KB")


LIGHT = [(122, 140, 255), (190, 140, 230), (255, 182, 200)]
LIGHT_BLOBS = [(200, 150, 420, (255, 255, 255), 70), (1150, 900, 380, (255, 214, 170), 90)]
DARK = [(22, 28, 64), (58, 40, 96), (110, 46, 96)]
DARK_BLOBS = [(1100, 120, 380, (80, 120, 255), 70), (150, 900, 360, (255, 90, 150), 60)]

os.makedirs(OUT, exist_ok=True)

# Settings window, light (the Animations page with its live genie) and dark (Corners).
for theme, stops, blobs, page, name in (("en-light", LIGHT, LIGHT_BLOBS, 0, "settings-light.png"),
                                        ("en-dark", DARK, DARK_BLOBS, 1, "settings-dark.png")):
    win = shot(theme, page)
    canvas = gradient(1300, 980, stops, blobs).convert("RGBA")
    with_shadow(canvas, win, (1300 - win.width) // 2, (980 - win.height) // 2 - 14)
    save(canvas, name)

# Pages of their own: App Icons (dark, its list of icons) and Privacy (light).
for theme, stops, blobs, file, name in (("en-dark", DARK, DARK_BLOBS, "page3-end.png", "app-icons.png"),
                                        ("en-light", LIGHT, LIGHT_BLOBS, "page6.png", "privacy.png")):
    win = Image.open(os.path.join(SHOTS, theme, file)).convert("RGBA")
    canvas = gradient(1300, 980, stops, blobs).convert("RGBA")
    with_shadow(canvas, win, (1300 - win.width) // 2, (980 - win.height) // 2 - 14)
    save(canvas, name)

# Desktop widgets: the first rows of the widget sheet (clock, calendar, weather, gauges, note).
sheet = Image.open(os.path.join(SHOTS, "widgets-dark", "widgets.png")).convert("RGBA")
k = sheet.width / 1526   # the sheet's rows, measured at 1526 px wide
save(sheet.crop((0, 0, sheet.width, int(1010 * k))), "widgets.png")

# Four languages at once (Japanese, Arabic, Hindi, Polish).
# English labels: Pillow without libraqm cannot shape Arabic or Devanagari.
langs = [("ja", "Japanese"), ("ar", "Arabic (right-to-left)"), ("hi", "Hindi"), ("pl", "Polish")]
cell_w, cell_h = 660, 520
canvas = gradient(cell_w * 2 + 60, cell_h * 2 + 40, LIGHT, LIGHT_BLOBS).convert("RGBA")
for i, (code, label) in enumerate(langs):
    win = shot(code, 0)
    win = win.resize((550, 400), Image.LANCZOS)
    x = 30 + (i % 2) * (cell_w) + (cell_w - win.width) // 2
    y = 24 + (i // 2) * cell_h
    with_shadow(canvas, win, x, y, blur=22, offset=14, opacity=100)
    d = ImageDraw.Draw(canvas)
    f = font("segoeuib.ttf", 30)
    tw = d.textlength(label, font=f)
    d.text((x + (win.width - tw) / 2, y + win.height + 30), label, font=f, fill=(255, 255, 255, 255))
save(canvas, "languages.png")

# App icon for the README header.
icon = Image.open(os.path.join(SHOTS, "icon.png")).convert("RGBA")
icon.save(os.path.join(OUT, "icon.png"), optimize=True)
print("icon.png", icon.size)

# Social preview (GitHub: Settings -> General -> Social preview), 1280 x 640.
canvas = gradient(1280, 640, DARK, [(1050, 80, 330, (80, 120, 255), 80), (120, 620, 300, (255, 90, 150), 70)]).convert("RGBA")
win = shot("en-light", 0).resize((660, 480), Image.LANCZOS)
with_shadow(canvas, win, 640, 120, blur=26, offset=16, opacity=120)
big_icon = icon.resize((150, 150), Image.LANCZOS)
canvas.alpha_composite(big_icon, (70, 120))
d = ImageDraw.Draw(canvas)
d.text((66, 290), "MacWin", font=font("segoeuib.ttf", 92), fill=(255, 255, 255, 255))
d.text((72, 410), "The macOS look for Windows 11:", font=font("segoeui.ttf", 32), fill=(230, 230, 245, 255))
d.text((72, 452), "genie, corners, widgets, app icons", font=font("segoeui.ttf", 32), fill=(230, 230, 245, 255))
d.text((72, 530), "Free & open source  ·  11 languages", font=font("segoeuib.ttf", 26), fill=(170, 190, 255, 255))
save(canvas, "social-preview.png")
