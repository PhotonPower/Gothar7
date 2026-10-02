"""Generates terrain.r16 of the test world (257 x 257 samples, 2 m apart, -20..60 m).

Flat ground (y = 0) where the camp and its forest stand (r < 80 m), hills rising around it and a
basin in the north-west. Format: .g7world "terrain" block (docs/modules/world.md): uint16 little
endian, row 0 = north (-Z), column 0 = west (-X), y = minY + v / 65535 * (maxY - minY).

    uv run --no-project python make_terrain.py
"""

import math
import struct

SIZE, CELL, FIRST, MIN_Y, MAX_Y = 257, 2.0, -256.0, -20.0, 60.0


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
