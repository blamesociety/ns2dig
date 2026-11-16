"""Draw a generic red game-card icon (no logos) and save a multi-size .ico."""
from PIL import Image, ImageDraw, ImageFont, ImageFilter

S = 1024  # master canvas


def card(size_hint):
    """Master artwork. Small sizes get a simpler, bolder version."""
    simple = size_hint <= 32
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))

    # card body: portrait rounded rectangle with the top-right corner clipped
    x0, y0, x1, y1 = 196, 92, 828, 932
    r, clip, inset = 70, 130, 22
    red, dark_red = (214, 26, 44, 255), (150, 12, 26, 255)

    def shape(ins):
        m = Image.new("L", (S, S), 0)
        md = ImageDraw.Draw(m)
        md.rounded_rectangle((x0 + ins, y0 + ins, x1 - ins, y1 - ins), radius=max(r - ins, 8), fill=255)
        # clip corner: everything above the diagonal through (x1-clip, y0) .. (x1, y0+clip), moved inward by ins
        k = ins * 0.4142  # keeps the bevel width even
        md.polygon([(x1 - clip - k, y0 - 5), (x1 + 5, y0 - 5), (x1 + 5, y0 + clip + k)], fill=0)
        return m

    outer, inner = shape(0), shape(inset)
    sh = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sh.paste((0, 0, 0, 110), (18, 26), outer)
    sh = sh.filter(ImageFilter.GaussianBlur(22))
    img.alpha_composite(sh)
    layer = Image.new("RGBA", (S, S), dark_red); img.paste(layer, (0, 0), outer)
    layer = Image.new("RGBA", (S, S), red);      img.paste(layer, (0, 0), inner)
    d = ImageDraw.Draw(img)

    # top grip ridges
    if not simple:
        for i in range(5):
            gx = x0 + 110 + i * 70
            d.rounded_rectangle((gx, y0 + 52, gx + 36, y0 + 142), radius=16, fill=dark_red)

    # label
    lx0, ly0, lx1, ly1 = x0 + 78, y0 + (190 if not simple else 150), x1 - 78, y1 - (150 if not simple else 110)
    d.rounded_rectangle((lx0, ly0, lx1, ly1), radius=44, fill=(250, 250, 248, 255))
    if not simple:
        d.rounded_rectangle((lx0, ly0, lx1, ly0 + 70), radius=44, fill=(232, 236, 240, 255))
        d.rectangle((lx0, ly0 + 40, lx1, ly0 + 70), fill=(232, 236, 240, 255))

    # "{ }" glyph = JSON
    try:
        font = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", 250 if not simple else 330)
    except OSError:
        font = ImageFont.load_default()
    text = "{ }"
    tb = d.textbbox((0, 0), text, font=font)
    tw, th = tb[2] - tb[0], tb[3] - tb[1]
    cx, cy = (lx0 + lx1) / 2, (ly0 + (70 if not simple else 0) + ly1) / 2
    d.text((cx - tw / 2 - tb[0], cy - th / 2 - tb[1]), text, font=font, fill=(40, 44, 52, 255))
    return img


sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
frames = []
for s in sizes:
    im = card(s).resize((s, s), Image.LANCZOS)
    frames.append(im)

frames[-1].save("app.ico", format="ICO", sizes=[(s, s) for s in sizes], append_images=frames[:-1])
# preview sheet
sheet = Image.new("RGBA", (sum(sizes) + 20 * len(sizes) + 20, 300), (255, 255, 255, 255))
x = 20
for s, im in zip(sizes, frames):
    sheet.alpha_composite(im, (x, 150 - s // 2))
    x += s + 20
sheet.save("preview.png")
dark = Image.new("RGBA", sheet.size, (32, 32, 32, 255)); x = 20
for s, im in zip(sizes, frames):
    dark.alpha_composite(im, (x, 150 - s // 2)); x += s + 20
dark.save("preview_dark.png")
print("ok")
