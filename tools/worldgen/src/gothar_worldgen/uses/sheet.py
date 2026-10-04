"""The suggestion as a labelled top-down map and a table, for choosing (W7)."""

from __future__ import annotations

from collections.abc import Sequence
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont
from shapely.geometry import Polygon

from gothar_worldgen.uses.suggest import USES, Suggestion

COLORS = {
    "gasthaus": (200, 60, 40),
    "baecker": (230, 160, 40),
    "metzger": (170, 40, 90),
    "haendler": (40, 120, 200),
    "schmiede": (60, 60, 60),
    "werkstatt": (120, 90, 50),
    "bader": (90, 170, 170),
    "kraeuter": (60, 150, 60),
    "amtshaus": (130, 60, 170),
    "wache": (20, 40, 110),
    "pfarrhaus": (150, 150, 40),
    "bauer": (110, 140, 40),
    "wohnhaus": (210, 190, 150),
}


def _font(size: int) -> ImageFont.ImageFont:
    for name in ("arialbd.ttf", "arial.ttf", "DejaVuSans-Bold.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            continue
    return ImageFont.load_default()


def draw_map(path: Path, houses: dict[str, Polygon], picks: Sequence[Suggestion],
             centre: tuple[float, float], half: float, px: int = 2400) -> None:  # fmt: skip
    """All houses grey, the suggested ones coloured by use with short name and use."""
    s = px / (2 * half)

    def m(x: float, z: float) -> tuple[float, float]:
        return ((x - centre[0] + half) * s, (z - centre[1] + half) * s)

    im = Image.new("RGB", (px, px + 60), (240, 238, 230))
    d = ImageDraw.Draw(im)
    chosen = {p.id: p for p in picks}
    for hid, poly in houses.items():
        for g in getattr(poly, "geoms", [poly]):
            if g.geom_type != "Polygon":
                continue
            col = COLORS[chosen[hid].use] if hid in chosen else (185, 180, 172)
            d.polygon([m(x, z) for x, z in g.exterior.coords], fill=col, outline=(90, 90, 90))
    font, small = _font(18), _font(15)
    boxes: list[tuple[float, float, float, float]] = []

    def free(b: tuple[float, float, float, float]) -> bool:
        return all(b[2] < o[0] or b[0] > o[2] or b[3] < o[1] or b[1] > o[3] for o in boxes)

    for k, p in enumerate(picks, start=1):  # number as in the table, short name, use
        x, y = m(*p.at)
        top, bottom = f"{k} {p.short}", USES[p.use][0]
        w = max(d.textlength(top, font=font), d.textlength(bottom, font=small)) + 6
        for dx, dy in ((0, 0), (0, -46), (0, 46), (-w, 0), (w, 0), (0, -92), (0, 92), (-w, -46),
                       (w, 46), (-w, 46), (w, -46)):  # fmt: skip
            b = (x + dx - w / 2, y + dy - 22, x + dx + w / 2, y + dy + 22)
            if free(b):
                break
        boxes.append(b)
        cx, cy = (b[0] + b[2]) / 2, (b[1] + b[3]) / 2
        if (cx, cy) != (x, y):
            d.line([(x, y), (cx, cy)], fill=(0, 0, 0), width=2)
        d.rectangle(b, fill=(255, 255, 255), outline=(60, 60, 60))
        d.text((cx - d.textlength(top, font=font) / 2, b[1] + 1), top, fill=(0, 0, 0), font=font)
        d.text((cx - d.textlength(bottom, font=small) / 2, b[1] + 23), bottom, fill=(0, 0, 0),
               font=small)  # fmt: skip
    xl = 10  # legend
    for use, col in COLORS.items():
        d.rectangle([xl, px + 18, xl + 22, px + 40], fill=col, outline=(0, 0, 0))
        d.text((xl + 28, px + 20), USES[use][0], fill=(0, 0, 0), font=small)
        xl += 40 + int(d.textlength(USES[use][0], font=small))
    im.save(path)


def table_md(picks: Sequence[Suggestion]) -> str:
    rows = ["| # | Kürzel | Gebäude | Nutzung | Bewohner | begehbar? | Grund |",
            "|---|---|---|---|---|---|---|"]  # fmt: skip
    for k, p in enumerate(picks, start=1):
        inside = "Kandidat" if USES[p.use][2] else ""
        rows.append(f"| {k} | {p.short} | {p.id} | {USES[p.use][0]} | {p.residents} | {inside} | "
                    f"{'; '.join(p.reasons)} |")  # fmt: skip
    return "\n".join(rows) + "\n"
