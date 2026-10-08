"""Worn cloth textures baked per garment (F3n): a tileable fabric at an even thread density plus
wear (faded, stains, dirty seams and hems), as data in ``data/fabrics.toml``.

The fabric sources (ambientCG, CC0) stay in DATA_ROOT; only the baked textures go into the
repository, under the names the parts already use (``textures/cloth/<name>``), so the parts do not
change. Pure numpy here (testable); ``gothar-chargen fabrics`` reads and writes images in Blender.

Data (version 1)::

    [tiles.linen]
    source = "Fabric066/Fabric066_1K-JPG_Color.jpg"   # below the fabric sources folder
    size = 0.25                                        # metres per repeat

    [textures."elvs_crude_t-shirt_male_neutral.jpg"]   # file in textures/cloth/
    part = "parts/cloth_m_average/elvs_crude_t-shirt_male.glb"
    material = "cloth_elvs_crude_t-shirt_male"         # whose UVs the texture follows
    tile = "linen"
    wear = 0.8                                         # 0 clean .. 1 worn out
    tint = "#b5a68a"                                   # optional: coloured instead of neutral grey
    fray = 0.6                                         # optional: frayed hems 0..1 (file .png)
    soil = "flour"                                     # optional: flour | clay | dark (trades)
    soil_amount = 0.6                                  # 0..1 (default 0.5)

Frayed hems (alpha test, contract with engine: MASK, cutoff 0.5, double-sided): the texture gets
an alpha channel that cuts an irregular, jagged band out of the cloth along its hems – the open
edges of the garment in 3D (hems, cuffs, collars), not its UV seams. `gothar-chargen fabrics` then
switches the materials of every part that uses the texture to MASK; `part-data` keeps the body
under a frayed garment's hems (`partdata.HEM_KEEP`), so one sees skin, not through the figure.
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass
from importlib import resources
from pathlib import Path

import numpy as np

from gothar_chargen.gltf import Gltf

FORMAT_VERSION = 1
SIZE = 512  # texture size (contract: cloth <= 1024; repo size)
NEUTRAL_MEAN = 0.55  # same as the kit textures: the palette multiplies it
CONTRAST = 1.1  # fabric pattern contrast kept before wear
PAD = 8  # pixels of colour bled outside the UV islands (mipmaps)
SEAM = 9  # pixels: widest dirty band along seams and hems at wear 1
FRAY = 14  # pixels: deepest fraying cut into a hem at fray 1 (512 px: about 4 cm on a shirt)
ALPHA_CUTOFF = 0.5  # contract with engine (§2.3)
SOILS = ("flour", "clay", "dark")  # trade soiling: baker's flour, potter's clay, dark stains
CLAY = (0.66, 0.54, 0.46)  # relative colour of dried clay on neutral cloth (the palette tints it)
_COLOR = re.compile(r"^#[0-9a-fA-F]{6}$")


class FabricError(Exception):
    """The fabric data is malformed or does not match a part."""


@dataclass(frozen=True)
class Tile:
    name: str
    source: str
    size: float  # metres per repeat


@dataclass(frozen=True)
class Target:
    file: str  # textures/cloth/<file>
    part: str
    material: str
    tile: str
    wear: float
    tint: tuple[float, float, float] | None = None
    fray: float = 0.0
    soil: str | None = None
    soil_amount: float = 0.5


@dataclass(frozen=True)
class FabricData:
    tiles: dict[str, Tile]
    targets: tuple[Target, ...]


def parse_fabrics(data: dict) -> FabricData:
    if data.get("version") != FORMAT_VERSION:
        raise FabricError(f"version must be {FORMAT_VERSION}")
    tiles: dict[str, Tile] = {}
    for name, t in data.get("tiles", {}).items():
        if not isinstance(t, dict) or not isinstance(t.get("source"), str):
            raise FabricError(f"tile {name}: needs a source path")
        size = t.get("size")
        if not isinstance(size, int | float) or not 0.02 <= size <= 2.0:
            raise FabricError(f"tile {name}: size must be 0.02..2 m")
        tiles[name] = Tile(name, t["source"], float(size))
    targets = []
    for file, t in sorted(data.get("textures", {}).items()):
        where = f"texture {file}"
        if not file.endswith((".jpg", ".png")) or "/" in file:
            raise FabricError(f"{where}: a file name in textures/cloth/")
        keys = {"part", "material", "tile", "wear", "tint", "fray", "soil", "soil_amount"}
        if set(t) - keys:
            raise FabricError(f"{where}: unknown keys {sorted(set(t) - keys)}")
        if t.get("tile") not in tiles:
            raise FabricError(f"{where}: unknown tile {t.get('tile')!r}")
        wear = t.get("wear", 0.5)
        if not isinstance(wear, int | float) or not 0.0 <= wear <= 1.0:
            raise FabricError(f"{where}: wear must be 0..1")
        tint = t.get("tint")
        if tint is not None and (not isinstance(tint, str) or not _COLOR.match(tint)):
            raise FabricError(f"{where}: tint must be '#rrggbb'")
        fray = t.get("fray", 0.0)
        if not isinstance(fray, int | float) or not 0.0 <= fray <= 1.0:
            raise FabricError(f"{where}: fray must be 0..1")
        if fray > 0 and not file.endswith(".png"):
            raise FabricError(f"{where}: frayed textures need an alpha channel (.png)")
        soil = t.get("soil")
        if soil is not None and soil not in SOILS:
            raise FabricError(f"{where}: soil must be one of {', '.join(SOILS)}")
        soil_amount = t.get("soil_amount", 0.5)
        if "soil_amount" in t and soil is None:
            raise FabricError(f"{where}: soil_amount needs soil")
        if not isinstance(soil_amount, int | float) or not 0.0 <= soil_amount <= 1.0:
            raise FabricError(f"{where}: soil_amount must be 0..1")
        if not isinstance(t.get("part"), str) or not isinstance(t.get("material"), str):
            raise FabricError(f"{where}: needs part and material")
        targets.append(
            Target(
                file,
                t["part"],
                t["material"],
                t["tile"],
                float(wear),
                tuple(int(tint[i : i + 2], 16) / 255 for i in (1, 3, 5)) if tint else None,
                float(fray),
                soil,
                float(soil_amount),
            )
        )
    return FabricData(tiles, tuple(targets))


def load_fabrics(path: Path | None = None) -> FabricData:
    if path is None:
        text = resources.files("gothar_chargen.data").joinpath("fabrics.toml").read_text("utf-8")
    else:
        text = path.read_text(encoding="utf-8")
    try:
        return parse_fabrics(tomllib.loads(text))
    except tomllib.TOMLDecodeError as e:
        raise FabricError(str(e)) from e


# --- geometry of a garment -------------------------------------------------------------------


@dataclass(frozen=True)
class Garment:
    uv: np.ndarray  # (n, 2) glTF UVs (v down)
    positions: np.ndarray  # (n, 3)
    triangles: np.ndarray  # (m, 3)


def garment_of(gltf: Gltf, material: str) -> Garment:
    """UVs, positions and triangles of the lod0 primitive(s) with this material."""
    nodes = [n for n in gltf.list("nodes") if "mesh" in n]
    lod0 = [n for n in nodes if str(n.get("name", "")).endswith("_lod0")] or nodes
    mats = gltf.list("materials")
    uvs, pos, tris, base = [], [], [], 0
    for node in lod0:
        for prim in gltf.doc["meshes"][node["mesh"]]["primitives"]:
            m = prim.get("material")
            if m is None or str(mats[m].get("name", "")).split(".")[0] != material:
                continue
            a = prim["attributes"]
            if "TEXCOORD_0" not in a:
                raise FabricError(f"{material}: no UVs")
            uv = np.asarray(gltf.accessor(a["TEXCOORD_0"]), dtype=np.float64)
            uvs.append(uv)
            pos.append(np.asarray(gltf.accessor(a["POSITION"]), dtype=np.float64))
            idx = np.asarray(gltf.accessor(prim["indices"]), dtype=np.int64).reshape(-1, 3)
            tris.append(idx + base)
            base += len(uv)
    if not uvs:
        raise FabricError(f"no primitive with material {material}")
    return Garment(np.concatenate(uvs), np.concatenate(pos), np.concatenate(tris))


def metres_per_uv(g: Garment) -> float:
    """Average scale of the UV layout: sqrt(3D area / UV area)."""
    p = g.positions[g.triangles]
    a3 = 0.5 * np.linalg.norm(np.cross(p[:, 1] - p[:, 0], p[:, 2] - p[:, 0]), axis=1).sum()
    u = g.uv[g.triangles]
    e1, e2 = u[:, 1] - u[:, 0], u[:, 2] - u[:, 0]
    a2 = 0.5 * np.abs(e1[:, 0] * e2[:, 1] - e1[:, 1] * e2[:, 0]).sum()
    if a2 <= 0:
        raise FabricError("degenerate UV layout")
    return float(np.sqrt(a3 / a2))


def rasterize(g: Garment, size: int = SIZE) -> np.ndarray:
    """Texels covered by the UV islands (size, size), row = v * size (glTF, v down)."""
    covered = np.zeros((size, size), dtype=bool)
    pix = g.uv[g.triangles] * size - 0.5  # texel centres
    for t in pix:
        lo = np.floor(t.min(axis=0)).astype(int)
        hi = np.ceil(t.max(axis=0)).astype(int)
        x0, y0 = max(lo[0], 0), max(lo[1], 0)
        x1, y1 = min(hi[0], size - 1), min(hi[1], size - 1)
        if x1 < x0 or y1 < y0:
            continue
        xs, ys = np.meshgrid(np.arange(x0, x1 + 1), np.arange(y0, y1 + 1))
        a, b, c = t
        d = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1])
        if abs(d) < 1e-12:
            continue
        w0 = ((b[1] - c[1]) * (xs - c[0]) + (c[0] - b[0]) * (ys - c[1])) / d
        w1 = ((c[1] - a[1]) * (xs - c[0]) + (a[0] - c[0]) * (ys - c[1])) / d
        inside = (w0 >= -0.02) & (w1 >= -0.02) & (1 - w0 - w1 >= -0.02)
        covered[ys[inside], xs[inside]] = True
    return covered


def distance_inside(mask: np.ndarray, limit: int) -> np.ndarray:
    """Chessboard distance (texels) of covered texels to the nearest uncovered one, up to limit."""
    dist = np.full(mask.shape, limit, dtype=np.float64)
    current = mask.copy()
    for step in range(limit):
        shrunk = current.copy()
        shrunk[1:, :] &= current[:-1, :]
        shrunk[:-1, :] &= current[1:, :]
        shrunk[:, 1:] &= current[:, :-1]
        shrunk[:, :-1] &= current[:, 1:]
        dist[current & ~shrunk] = step
        current = shrunk
    dist[~mask] = 0
    return dist


def value_noise(size: int, cells: int, rng: np.random.Generator) -> np.ndarray:
    """Smooth noise (size, size) in 0..1 from a random grid of `cells` cells, tiling."""
    grid = rng.random((cells, cells))
    t = np.arange(size) * cells / size
    i = np.floor(t).astype(int)
    f = t - i
    f = f * f * (3 - 2 * f)
    i0, i1 = i % cells, (i + 1) % cells
    rows = grid[i0][:, i0] * (1 - f)[None, :] + grid[i0][:, i1] * f[None, :]
    rows1 = grid[i1][:, i0] * (1 - f)[None, :] + grid[i1][:, i1] * f[None, :]
    return rows * (1 - f)[:, None] + rows1 * f[:, None]


def fractal(size: int, rng: np.random.Generator, octaves: int = 4, base: int = 4) -> np.ndarray:
    total = np.zeros((size, size))
    amp, weight = 1.0, 0.0
    for o in range(octaves):
        total += amp * value_noise(size, base * 2**o, rng)
        weight += amp
        amp *= 0.5
    return total / weight


def sample_tile(tile: np.ndarray, u: np.ndarray, v: np.ndarray) -> np.ndarray:
    """Bilinear sample of a tiling (h, w) luminance image at fractional coordinates."""
    h, w = tile.shape
    x, y = (u % 1.0) * w - 0.5, (v % 1.0) * h - 0.5
    x0, y0 = np.floor(x).astype(int), np.floor(y).astype(int)
    fx, fy = x - x0, y - y0
    x0, y0, x1, y1 = x0 % w, y0 % h, (x0 + 1) % w, (y0 + 1) % h
    top = tile[y0, x0] * (1 - fx) + tile[y0, x1] * fx
    bottom = tile[y1, x0] * (1 - fx) + tile[y1, x1] * fx
    return top * (1 - fy) + bottom * fy


def bake(
    tile: np.ndarray,
    tile_size: float,
    garment: Garment,
    wear: float,
    seed: int,
    tint: tuple[float, float, float] | None = None,
    size: int = SIZE,
    soil: str | None = None,
    soil_amount: float = 0.5,
) -> np.ndarray:
    """(size, size, 3) colour in 0..1, row = v * size (glTF). `tile` is the fabric's luminance.
    `soil` adds a trade's marks after the wear (drawn last, so other textures stay the same)."""
    rng = np.random.default_rng(seed)
    covered = rasterize(garment, size)
    repeats = metres_per_uv(garment) / tile_size  # tile repeats per UV unit
    v, u = (np.mgrid[0:size, 0:size] + 0.5) / size
    lum = sample_tile(tile, u * repeats, v * repeats)
    mean = max(float(tile.mean()), 1e-3)
    lum = (lum - mean) / mean  # pattern around 0
    # fade: worn cloth loses contrast and colour depth
    lum = NEUTRAL_MEAN * (1 + CONTRAST * (1 - 0.5 * wear) * lum) + 0.04 * wear
    # stains: large soft blotches
    stains = fractal(size, rng, base=6)
    blot = np.clip((stains - (0.75 - 0.15 * wear)) / 0.08, 0, 1)
    lum *= 1 - 0.22 * wear * blot
    # grime along seams and hems, irregular width
    dist = distance_inside(covered, SEAM + 2)
    width = (2 + SEAM * wear) * np.clip(1.6 * fractal(size, rng, base=16) - 0.3, 0.05, 1.2)
    band = np.clip(1 - dist / np.maximum(width, 1), 0, 1) ** 2
    lum *= 1 - 0.35 * wear * band
    lum = np.clip(lum, 0, 1)
    colour = lum[:, :, None] * (np.array(tint)[None, None, :] if tint else np.ones(3))
    if soil is not None:
        colour = _soil(colour, soil, soil_amount, rng, size)
    return bleed(colour, covered, PAD)


def _soil(
    colour: np.ndarray, soil: str, amount: float, rng: np.random.Generator, size: int
) -> np.ndarray:
    """Trade marks on the cloth: flour (light dust and specks), clay (reddish-grey smears),
    dark (darker, warm blotches, kept restrained - no blood red)."""
    if soil == "flour":
        dust = np.clip((fractal(size, rng, base=8) - 0.42) / 0.3, 0, 1) * amount
        specks = (rng.random((size, size)) < 0.18 * dust).astype(float)
        light = np.clip(0.45 * dust + 0.5 * specks, 0, 0.85)[:, :, None]
        return colour + (0.97 - colour) * light
    blot = np.clip((fractal(size, rng, base=10) - (0.72 - 0.2 * amount)) / 0.1, 0, 1)
    if soil == "clay":
        k = (0.65 * amount * blot)[:, :, None]
        return (
            colour * (1 - k)
            + np.array(CLAY)[None, None, :] * colour.mean(2, keepdims=True) * 1.25 * k
        )
    k = (0.4 * amount * blot)[:, :, None]  # dark
    return colour * (1 - k * np.array([0.9, 1.0, 1.15])[None, None, :])


def hem_lines(g: Garment, size: int = SIZE) -> np.ndarray:
    """Texels (size, size) on the garment's hems: its open edges in 3D (welded across UV seams),
    drawn in UV space; UV seams inside the cloth are not hems."""
    _, weld = np.unique(np.round(g.positions / 1e-5).astype(np.int64), axis=0, return_inverse=True)
    weld = weld.ravel()
    corners = ((0, 1), (1, 2), (2, 0))
    keys = np.concatenate([np.sort(weld[g.triangles[:, list(c)]], axis=1) for c in corners])
    _, inverse, counts = np.unique(keys, axis=0, return_inverse=True, return_counts=True)
    open_edge = counts[inverse.ravel()] == 1
    ends = np.concatenate([g.triangles[:, list(c)] for c in corners])[open_edge]
    hem = np.zeros((size, size), dtype=bool)
    for a, b in ends:
        pa, pb = g.uv[a] * size - 0.5, g.uv[b] * size - 0.5
        steps = 2 * int(np.ceil(np.abs(pb - pa).max())) + 2  # half-texel steps: no gaps
        pts = np.rint(pa[None] + (pb - pa)[None] * np.linspace(0, 1, steps)[:, None]).astype(int)
        ok = (pts >= 0).all(axis=1) & (pts < size).all(axis=1)
        hem[pts[ok, 1], pts[ok, 0]] = True
    return hem


def fray_alpha(g: Garment, fray: float, seed: int, size: int = SIZE) -> np.ndarray:
    """Alpha (size, size) in {0, 1}: jagged cuts along the hems, deeper with `fray`."""
    if fray <= 0:
        return np.ones((size, size))
    rng = np.random.default_rng(seed + 1)
    hem = hem_lines(g, size)
    dist = distance_inside(~hem, FRAY + 2)  # texels to the nearest hem texel
    # teeth: fine noise along the hem, a slower one for torn stretches
    jag = 0.6 * value_noise(size, 128, rng) + 0.4 * fractal(size, rng, octaves=2, base=12)
    depth = fray * FRAY * np.clip(1.5 * jag - 0.25, 0.1, 1.0)
    return (dist >= depth).astype(np.float64)


def bleed(colour: np.ndarray, mask: np.ndarray, steps: int) -> np.ndarray:
    """Spreads colours outward from the covered texels (no dark seams in mipmaps)."""
    out = colour.copy()
    known = mask.copy()
    for _ in range(steps):
        acc = np.zeros_like(out)
        cnt = np.zeros(mask.shape)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            shifted = np.roll(np.roll(out * known[:, :, None], dy, 0), dx, 1)
            k = np.roll(np.roll(known, dy, 0), dx, 1)
            acc += shifted
            cnt += k
        grow = ~known & (cnt > 0)
        out[grow] = acc[grow] / cnt[grow][:, None]
        known |= grow
    out[~known] = colour[known].mean(axis=0) if known.any() else 0.5
    return out


def apply_fray_materials(characters: Path, target: Target) -> list[Path]:
    """Points every part that uses the target's texture (any extension) at the frayed .png and
    switches those materials to MASK, cutoff 0.5, double-sided. Returns the parts written."""
    stem = Path(target.file).stem
    written = []
    for path in sorted((characters / "parts").rglob("*.glb")):
        g = Gltf.load(path)
        images = [
            i for i, img in enumerate(g.list("images"))
            if Path(str(img.get("uri", ""))).stem == stem
        ]  # fmt: skip
        if not images:
            continue
        changed = False
        for i in images:
            img = g.doc["images"][i]
            new = str(Path(img["uri"]).with_name(target.file).as_posix())
            changed |= img["uri"] != new
            img["uri"] = new
        textures = {k for k, tex in enumerate(g.list("textures")) if tex.get("source") in images}
        for mat in g.list("materials"):
            base = mat.get("pbrMetallicRoughness", {}).get("baseColorTexture", {}).get("index")
            if base in textures:
                want = {"alphaMode": "MASK", "alphaCutoff": ALPHA_CUTOFF, "doubleSided": True}
                changed |= any(mat.get(k) != v for k, v in want.items())
                mat.update(want)
        if changed:
            path.write_bytes(g.to_bytes())
            written.append(path)
    return written
