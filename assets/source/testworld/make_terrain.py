"""Generates the terrain of the test world (.g7world "terrain" block, docs/modules/world.md).

- terrain.r16: 257 x 257 samples, 2 m apart, -20..60 m. Flat ground (y = 0) where the camp and its
  forest stand (r < 80 m), hills rising around it and a basin in the north-west. uint16 little
  endian, row 0 = north (-Z), column 0 = west (-X), y = minY + v / 65535 * (maxY - minY).
- splat0.png: 129 x 129 RGBA weights of the layers grass, earth, rock, path (pixel centres on every
  second sample: first pixel on the first sample, last on the last). Linear data.
- holes.r8: 256 x 256 bytes, one per cell, 255 = ground, 0 = hole (a small pit east of the camp).
- layer_*.png: 128 x 128 procedural layer albedos (own content, sRGB).

    uv run --no-project python make_terrain.py
"""

import math
import random
import struct
import zlib

SIZE, CELL, FIRST, MIN_Y, MAX_Y = 257, 2.0, -256.0, -20.0, 60.0
SPLAT, LAYER = 129, 128


def smoothstep(a: float, b: float, x: float) -> float:
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def height(x: float, z: float) -> float:
    r = math.hypot(x, z)
    outside = smoothstep(80.0, 150.0, r)
    hills = 6.0 + 9.0 * math.sin(x / 37.0) * math.cos(z / 51.0) + 5.0 * math.sin((x + z) / 23.0)
    rise = max(r - 80.0, 0.0) * 0.12
    basin = -26.0 * math.exp(-((x + 150.0) ** 2 + (z + 120.0) ** 2) / (2 * 45.0**2))
    return outside * (hills + rise) + smoothstep(60.0, 120.0, r) * basin


def write_png(path: str, width: int, height: int, rgba: bytes) -> None:
    """8-bit RGBA PNG without colour-space chunks (stdlib only)."""

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = b"".join(b"\0" + rgba[y * width * 4 : (y + 1) * width * 4] for y in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(rows, 9)))
        f.write(chunk(b"IEND", b""))


# Heights.
values = []
for row in range(SIZE):
    for col in range(SIZE):
        y = height(FIRST + col * CELL, FIRST + row * CELL)
        v = round((y - MIN_Y) / (MAX_Y - MIN_Y) * 65535)
        values.append(min(max(v, 0), 65535))
with open("terrain.r16", "wb") as f:
    f.write(struct.pack(f"<{len(values)}H", *values))
print(f"terrain.r16: {SIZE} x {SIZE}, heights {MIN_Y + min(values) / 65535 * (MAX_Y - MIN_Y):.2f} .. "
      f"{MIN_Y + max(values) / 65535 * (MAX_Y - MIN_Y):.2f} m")

# Splat weights: grass, earth on slopes, rock on cliffs, a path ring around the camp and east.
step = (SIZE - 1) * CELL / (SPLAT - 1)
splat = bytearray()
for j in range(SPLAT):
    for i in range(SPLAT):
        x, z = FIRST + i * step, FIRST + j * step
        dx = height(x + 1.0, z) - height(x - 1.0, z)
        dz = height(x, z + 1.0) - height(x, z - 1.0)
        slope = 1.0 - 2.0 / math.sqrt(dx * dx + 4.0 + dz * dz)  # 1 - normal.y
        rock = smoothstep(0.25, 0.4, slope)
        earth = smoothstep(0.06, 0.16, slope) * (1.0 - rock)
        r = math.hypot(x, z)
        path = max(1.0 - abs(r - 88.0) / 4.0, 0.0)
        path = max(path, (1.0 - abs(z) / 3.0) * smoothstep(84.0, 92.0, x) if abs(z) < 3.0 else 0.0)
        path *= 1.0 - rock
        grass = max(1.0 - earth - rock - path, 0.0)
        splat += bytes(round(255 * min(w, 1.0)) for w in (grass, earth, rock, path))
write_png("splat0.png", SPLAT, SPLAT, bytes(splat))

# Holes: a 6 x 4-cell pit at x 120..132, z -8..0 (cells from x = -256 + 2c).
cells = SIZE - 1
holes = bytearray([255]) * (cells * cells)
for r in range(124, 128):
    for c in range(188, 194):
        holes[r * cells + c] = 0
with open("holes.r8", "wb") as f:
    f.write(bytes(holes))

# Layer albedos: base colour with two octaves of tileable noise (seeded: same bytes every run).
rng = random.Random(7)


def noise(size: int, cells_: int) -> list[float]:
    grid = [[rng.random() for _ in range(cells_)] for _ in range(cells_)]
    out = []
    for y in range(size):
        for x in range(size):
            fx, fy = x * cells_ / size, y * cells_ / size
            x0, y0 = int(fx), int(fy)
            tx, ty = smoothstep(0, 1, fx - x0), smoothstep(0, 1, fy - y0)
            a, b = grid[y0 % cells_][x0 % cells_], grid[y0 % cells_][(x0 + 1) % cells_]
            c, d = grid[(y0 + 1) % cells_][x0 % cells_], grid[(y0 + 1) % cells_][(x0 + 1) % cells_]
            out.append((a + (b - a) * tx) * (1 - ty) + (c + (d - c) * tx) * ty)
    return out


for name, base, contrast in (("grass", (70, 100, 40), 0.35), ("earth", (110, 82, 58), 0.3),
                             ("rock", (125, 120, 112), 0.45), ("path", (150, 130, 96), 0.25)):
    coarse, fine = noise(LAYER, 8), noise(LAYER, 32)
    pixels = bytearray()
    for n1, n2 in zip(coarse, fine):
        k = 1.0 + contrast * ((n1 - 0.5) + 0.6 * (n2 - 0.5))
        pixels += bytes(min(max(round(ch * k), 0), 255) for ch in base) + b"\xff"
    write_png(f"layer_{name}.png", LAYER, LAYER, bytes(pixels))
print(f"splat0.png {SPLAT} x {SPLAT}, holes.r8 {cells} x {cells}, 4 layers {LAYER} x {LAYER}")
