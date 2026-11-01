"""Review sheets for the UI frames (M14): a Gothic-like mock-up screen built from the frames and a
sheet with every part, native and stretched. Needs Pillow (review only, not a package dependency;
``cli`` imports this module only for ``--sheet``). 9-slice drawing follows the engine
(``Canvas::frame``): corners keep their size, edges stretch along one axis, the centre in both,
borders shrink when the rectangle is too small.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from gothar_chargen.ui_frames import SCALE, SPECS, FrameSpec, spec

TEXT = (232, 220, 192)
TEXT_DIM = (168, 156, 128)
HIGHLIGHT = (255, 211, 107)
INK = (58, 42, 24)
INK_SELECTED = (122, 42, 20)


@dataclass
class _Ctx:
    frames: Path
    fonts: Path | None
    scale: float  # screen pixels per UI unit

    def image(self, name: str) -> Image.Image:
        return Image.open(self.frames / f"{name}.png").convert("RGBA")

    def font(self, size: float, caps: bool = False) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
        px = max(8, round(size * self.scale))
        if self.fonts is not None:
            file = self.fonts / ("AlegreyaSC-Regular.ttf" if caps else "Alegreya-Regular.ttf")
            if file.is_file():
                return ImageFont.truetype(str(file), px)
        return ImageFont.load_default(px)


def draw_frame(
    canvas: Image.Image, img: Image.Image, s: FrameSpec, box: tuple[float, ...], scale: float
) -> None:
    """9-slice ``img`` over ``box`` = (x0, y0, x1, y1) in screen pixels; ``scale`` = screen pixels
    per UI unit (image pixels are SCALE UI units)."""
    x0, y0, x1, y1 = (round(v) for v in box)
    w, h = x1 - x0, y1 - y0
    if w <= 0 or h <= 0:
        return
    left, top, right, bottom = s.slice
    k = SCALE * scale
    bl, bt, br, bb = left * k, top * k, right * k, bottom * k
    fx = min(1.0, w / (bl + br)) if bl + br > 0 else 1.0
    fy = min(1.0, h / (bt + bb)) if bt + bb > 0 else 1.0
    bl, br, bt, bb = bl * fx, br * fx, bt * fy, bb * fy
    xs_src = [0, left, s.width - right, s.width]
    ys_src = [0, top, s.height - bottom, s.height]
    xs_dst = [0, round(bl), w - round(br), w]
    ys_dst = [0, round(bt), h - round(bb), h]
    layer = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for j in range(3):
        for i in range(3):
            sw, sh = xs_src[i + 1] - xs_src[i], ys_src[j + 1] - ys_src[j]
            dw, dh = xs_dst[i + 1] - xs_dst[i], ys_dst[j + 1] - ys_dst[j]
            if sw <= 0 or sh <= 0 or dw <= 0 or dh <= 0:
                continue
            part = img.crop((xs_src[i], ys_src[j], xs_src[i + 1], ys_src[j + 1]))
            layer.paste(part.resize((dw, dh), Image.BICUBIC), (xs_dst[i], ys_dst[j]))
    canvas.alpha_composite(layer, (x0, y0))


def _frame(c: _Ctx, canvas: Image.Image, name: str, box: tuple[float, ...]) -> None:
    draw_frame(canvas, c.image(name), spec(name), box, c.scale)


def _text(
    c: _Ctx,
    canvas: Image.Image,
    xy: tuple[float, float],
    text: str,
    size: float,
    color: tuple[int, int, int],
    caps: bool = False,
    outline: float = 0.0,
    anchor: str = "la",
) -> None:
    draw = ImageDraw.Draw(canvas)
    draw.text(
        (round(xy[0]), round(xy[1])),
        text,
        font=c.font(size, caps),
        fill=color,
        anchor=anchor,
        stroke_width=round(outline * c.scale),
        stroke_fill=(0, 0, 0),
    )


def _bar(c: _Ctx, canvas: Image.Image, x: float, y: float, fill: str, fraction: float) -> None:
    """A HUD bar of 260 x 24 UI units: frame on top of the fill (opening inset 7 x 5 UI units)."""
    s = c.scale
    w, h = 260 * s, 24 * s
    ix, iy = 7 * s, 5 * s
    _frame(c, canvas, fill, (x + ix, y + iy, x + ix + (w - 2 * ix) * fraction, y + h - iy))
    _frame(c, canvas, "bar", (x, y, x + w, y + h))


def _background(size: tuple[int, int], background: Path | None) -> Image.Image:
    w, h = size
    if background is not None and background.is_file():
        bg = Image.open(background).convert("RGB")
        r = max(w / bg.width, h / bg.height)
        bg = bg.resize((round(bg.width * r), round(bg.height * r)), Image.BICUBIC)
        left, top = (bg.width - w) // 2, (bg.height - h) // 2
        bg = bg.crop((left, top, left + w, top + h))
        bg = Image.blend(bg, Image.new("RGB", (w, h), (0, 0, 0)), 0.4)
        return bg.convert("RGBA")
    out = Image.new("RGBA", (w, h))
    draw = ImageDraw.Draw(out)
    for y in range(h):  # dusk sky to dark ground
        t = y / h
        col = (int(60 - 40 * t), int(64 - 44 * t), int(70 - 50 * t), 255)
        draw.line([(0, y), (w, y)], fill=col)
    return out


def mockup(
    frames: Path,
    out: Path,
    fonts: Path | None = None,
    items: Path | None = None,
    background: Path | None = None,
    height: int = 1080,
) -> Path:
    """Gothic-like screen: HUD bars, inventory with tabs, grid and info box, dialog on parchment,
    subtitle and a screen message."""
    s = height / 1080.0
    c = _Ctx(frames, fonts, s)
    width = round(height * 16 / 9)
    canvas = _background((width, height), background)

    # HUD (F3): health bottom left, air above it, mana bottom right, enemy top centre
    _bar(c, canvas, 32 * s, height - 56 * s, "bar_hp", 0.72)
    _bar(c, canvas, 32 * s, height - 90 * s, "bar_air", 0.45)
    _bar(c, canvas, width - 292 * s, height - 56 * s, "bar_mana", 0.4)
    _text(c, canvas, (width / 2, 26 * s), "Wolf", 22, TEXT, outline=1.5, anchor="mt")
    _bar(c, canvas, width / 2 - 130 * s, 56 * s, "bar_enemy", 0.55)
    _text(
        c,
        canvas,
        (width / 2, 110 * s),
        "Erfahrung +50",
        30,
        TEXT,
        caps=True,
        outline=2.0,
        anchor="mt",
    )

    # inventory (F4): wooden window, tabs, leather grid, info box
    x0, y0, x1, y1 = width - 600 * s, 40 * s, width - 24 * s, height - 100 * s
    _frame(c, canvas, "panel", (x0, y0, x1, y1))
    _text(
        c,
        canvas,
        ((x0 + x1) / 2, y0 + 20 * s),
        "Inventar",
        34,
        HIGHLIGHT,
        caps=True,
        outline=1.5,
        anchor="mt",
    )
    tabs = ["Waffen", "Rüstung", "Magie", "Artefakte", "Essen", "Schriften", "Sonstiges"]
    tx, ty, tw = x0 + 28 * s, y0 + 70 * s, (x1 - x0 - 56 * s) / len(tabs)
    for i, label in enumerate(tabs):
        name = "tab_active" if i == 0 else "tab"
        _frame(c, canvas, name, (tx + i * tw, ty, tx + (i + 1) * tw, ty + 36 * s))
        _text(
            c,
            canvas,
            (tx + (i + 0.5) * tw, ty + 21 * s),
            label,
            16,
            HIGHLIGHT if i == 0 else TEXT_DIM,
            anchor="mm",
        )
    gx0, gy0 = x0 + 28 * s, ty + 36 * s
    gx1 = x1 - 28 * s
    cell = (gx1 - gx0 - 16 * s) / 5
    gy1 = gy0 + 16 * s + 4 * cell
    _frame(c, canvas, "leather", (gx0, gy0, gx1, gy1))
    stock = [
        ("it_sword_old", "", "cell_equipped"),
        ("it_sword_2h", "", "cell"),
        ("it_axe", "", "cell_selected"),
        ("it_club", "", "cell"),
        ("it_bow_short", "", "cell_equipped"),
        ("it_arrow", "42", "cell"),
        ("it_bolt", "12", "cell"),
        ("it_crossbow", "", "cell"),
        ("it_hammer", "", "cell"),
        ("it_torch", "3", "cell"),
    ]
    for k in range(20):
        r, q = divmod(k, 5)
        bx, by = gx0 + 8 * s + q * cell, gy0 + 8 * s + r * cell
        item, count, frame = stock[k] if k < len(stock) else ("", "", "cell")
        _frame(c, canvas, frame, (bx, by, bx + cell, by + cell))
        if items is not None and item and (items / f"{item}.png").is_file():
            icon = Image.open(items / f"{item}.png").convert("RGBA")
            size = round(cell - 16 * s)
            canvas.alpha_composite(
                icon.resize((size, size), Image.LANCZOS), (round(bx + 8 * s), round(by + 8 * s))
            )
        if count:
            _text(
                c,
                canvas,
                (bx + cell - 12 * s, by + cell - 10 * s),
                count,
                19,
                TEXT,
                outline=1.5,
                anchor="rd",
            )
    # info box with the selected item large
    ix0, iy0, ix1, iy1 = gx0, gy1 + 14 * s, gx1, y1 - 28 * s
    _frame(c, canvas, "tooltip", (ix0, iy0, ix1, iy1))
    pv = 190 * s
    if items is not None and (items / "it_axe.png").is_file():
        icon = Image.open(items / "it_axe.png").convert("RGBA")
        canvas.alpha_composite(
            icon.resize((round(pv), round(pv)), Image.LANCZOS),
            (round(ix0 + 20 * s), round(iy0 + 20 * s)),
        )
    lx = ix0 + pv + 40 * s
    _text(c, canvas, (lx, iy0 + 26 * s), "Holzfälleraxt", 30, HIGHLIGHT, caps=True)
    rows = [
        ("Schaden", "28"),
        ("Benötigt Stärke", "20"),
        ("Einhandwaffe", ""),
        ("Wert", "60 Gulden"),
    ]
    for i, (k_, v) in enumerate(rows):
        yy = iy0 + 80 * s + i * 32 * s
        _text(c, canvas, (lx, yy), k_, 22, TEXT_DIM)
        _text(c, canvas, (ix1 - 26 * s, yy), v, 22, TEXT, anchor="ra")
    by = iy1 - 66 * s
    bw = (ix1 - 26 * s - lx - 12 * s) / 2
    _frame(c, canvas, "button_focus", (lx, by, lx + bw, by + 44 * s))
    _text(c, canvas, (lx + bw / 2, by + 22 * s), "Anlegen", 22, HIGHLIGHT, anchor="mm")
    _frame(c, canvas, "button", (lx + bw + 12 * s, by, lx + 2 * bw + 12 * s, by + 44 * s))
    _text(c, canvas, (lx + 1.5 * bw + 12 * s, by + 22 * s), "Ablegen", 22, TEXT, anchor="mm")

    # dialog (M10 E3): answers on parchment at the bottom, subtitle with speaker above
    dx0, dx1 = 340 * s, width - 660 * s
    dy1 = height - 24 * s
    dy0 = dy1 - 200 * s
    sy1 = dy0 - 14 * s
    _frame(c, canvas, "subtitle", (dx0 + 40 * s, sy1 - 92 * s, dx1 - 40 * s, sy1))
    _text(
        c,
        canvas,
        ((dx0 + dx1) / 2, sy1 - 78 * s),
        "Bauer Kurt",
        22,
        HIGHLIGHT,
        outline=1.5,
        anchor="mt",
    )
    _text(
        c,
        canvas,
        ((dx0 + dx1) / 2, sy1 - 48 * s),
        "Was willst du hier? Das ist mein Feld.",
        28,
        TEXT,
        outline=2.0,
        anchor="mt",
    )
    _frame(c, canvas, "parchment", (dx0, dy0, dx1, dy1))
    answers = [
        "Ich suche Arbeit.",
        "Was weißt du über das Lager im Wald?",
        "Verkaufst du etwas?",
        "Ende",
    ]
    for i, line in enumerate(answers):
        yy = dy0 + 26 * s + i * 38 * s
        if i == 1:
            _frame(c, canvas, "highlight", (dx0 + 24 * s, yy - 4 * s, dx1 - 24 * s, yy + 34 * s))
        _text(c, canvas, (dx0 + 40 * s, yy), line, 24, INK_SELECTED if i == 1 else INK)

    # options strip: slider (part F)
    sx0, sy = 32 * s, 200 * s
    _frame(c, canvas, "tooltip", (sx0, sy, sx0 + 300 * s, sy + 110 * s))
    _text(c, canvas, (sx0 + 24 * s, sy + 20 * s), "Musik", 22, TEXT)
    _frame(c, canvas, "slider_track", (sx0 + 24 * s, sy + 62 * s, sx0 + 276 * s, sy + 78 * s))
    kx = sx0 + 24 * s + 0.65 * 252 * s
    _frame(c, canvas, "slider_knob", (kx - 12 * s, sy + 58 * s, kx + 12 * s, sy + 82 * s))

    out.parent.mkdir(parents=True, exist_ok=True)
    canvas.convert("RGB").save(out)
    return out


def parts_sheet(frames: Path, out: Path, fonts: Path | None = None) -> Path:
    """Every part: native image (image pixels), as at 1080p, stretched 9-slice, tiles 2 x 2."""
    c = _Ctx(frames, fonts, 1.0)
    rows = list(SPECS)
    row_h = [max(s.height, 140) + 40 for s in rows]
    width = 1500
    canvas = Image.new("RGBA", (width, sum(row_h) + 60), (74, 70, 64, 255))
    _text(
        c,
        canvas,
        (20, 16),
        "UI-Rahmen M14 (gothar-chargen ui-frames): Bild | 1080p | gestreckt",
        24,
        TEXT,
    )
    y = 60
    for s, rh in zip(rows, row_h, strict=True):
        img = c.image(s.name)
        _text(c, canvas, (20, y), s.name, 22, HIGHLIGHT)
        _text(c, canvas, (20, y + 28), f"{s.width}x{s.height}", 16, TEXT_DIM)
        _text(c, canvas, (20, y + 48), f"slice {list(s.slice)}", 16, TEXT_DIM)
        x = 230
        canvas.alpha_composite(img, (x, y))
        x += s.width + 30
        if s.background:
            for j in range(2):
                for i in range(2):
                    small = img.resize((s.width // 2, s.height // 2), Image.LANCZOS)
                    canvas.alpha_composite(small, (x + i * s.width // 2, y + j * s.height // 2))
            x += s.width + 30
            _text(c, canvas, (x, y), "Kachel 2 x 2 (nahtlos)", 18, TEXT_DIM)
        elif s.name != "slider_knob":  # the knob is drawn at its size
            draw_frame(canvas, img, s, (x, y, x + s.width / 2, y + s.height / 2), 1.0)
            x += s.width // 2 + 30
            w = min(width - x - 20, s.width * 3)
            h = s.height * (1.6 if s.height < 100 else 1.0)
            draw_frame(canvas, img, s, (x, y, x + w, y + h), 1.0)
        y += rh
    out.parent.mkdir(parents=True, exist_ok=True)
    canvas.convert("RGB").save(out)
    return out
