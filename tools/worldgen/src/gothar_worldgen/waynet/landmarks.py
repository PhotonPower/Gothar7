"""Landmarks of the assembled world for the waynet: fountains, the market square, gates, beds."""

from __future__ import annotations

from typing import Any

# fountain vobs and their rim radius (models in assets/source/worlds/<site>/handmade)
FOUNTAINS = {
    "HANDMADE_MARKTBRUNNEN": ("marktbrunnen", 3.56),
    "HANDMADE_OBELISKBRUNNEN": ("obeliskbrunnen", 3.55),
    "HANDMADE_GARTENBRUNNEN_W": ("gartenbrunnen_w", 1.88),
    "HANDMADE_GARTENBRUNNEN_O": ("gartenbrunnen_o", 1.88),
}
SQUARE_R = 9.0  # small talk on a ring around the market fountain


def landmarks(world: dict[str, Any], city_wall: dict[str, Any] | None = None) -> dict[str, Any]:
    out: dict[str, Any] = {"fountains": {}, "gates": {}, "centre": (0.0, 0.0)}
    for v in world.get("vobs", []):
        if v.get("name") in FOUNTAINS:
            key, r = FOUNTAINS[v["name"]]
            out["fountains"][key] = (float(v["pos"][0]), float(v["pos"][2]), r)
    if "marktbrunnen" in out["fountains"]:
        x, z, _ = out["fountains"]["marktbrunnen"]
        out["square"] = ((x, z), SQUARE_R)
        out["centre"] = (x, z)
    for g in (city_wall or {}).get("gates", []):
        if g.get("wall", "ring") == "ring":
            out["gates"][g["key"]] = (float(g["at"][0]), float(g["at"][1]))
    return out


def garden_beds(spec: dict[str, Any]) -> list[tuple[float, float]]:
    """A watering spot on the middle path beside every second garden bed (the castle parterre)."""
    from gothar_worldgen.handmade import schloss_geometry

    lay = schloss_geometry().garden_layout(spec)
    if lay is None:
        return []
    frame = lay["wing"]
    tc = frame.centre[1]
    out = []
    for bed in lay["beds"][::2]:
        s = (min(p[0] for p in bed) + max(p[0] for p in bed)) / 2
        side = 1.0 if (min(p[1] for p in bed) + max(p[1] for p in bed)) / 2 > tc else -1.0
        x, _, z = frame.p(s, tc + 0.5 * side, 0)
        out.append((float(x), float(z)))
    return out


def garden_links(spec: dict[str, Any], posterns: list[tuple[float, float]] = ()) -> list[Any]:
    """Explicit links through the castle garden's terrace stairs (``terracePlan`` of the built
    castle spec) and through Zwinger posterns in front of them: (hint, (xa, za), (xb, zb)) pairs
    the waynet connects without the line checks (the stair ramps are collision bodies)."""
    import math

    from gothar_worldgen.handmade import schloss_geometry

    plan = spec.get("garden", {}).get("terracePlan")
    if not plan:
        return []
    frame = schloss_geometry().garden_layout(spec)["wing"]
    cs, _ = frame.centre
    inner = float(spec["garden"]["railing"]["innerS"])
    rise, run = float(plan["rise"]), float(plan["run"])

    def xz(s: float, t: float) -> tuple[float, float]:
        x, _, z = frame.p(s, t, 0)
        return (float(x), float(z))

    links = []
    for k, st in enumerate(plan["stairs"]):
        (ts, tt), (ds, dt) = st["top"], st["dir"]
        length = math.ceil((float(st["y1"]) - float(st["y0"])) / rise) * run
        a = (ts - ds * 1.2, tt - dt * 1.2)
        b = (ts + ds * (length + 1.2), tt + dt * (length + 1.2))
        if st.get("side"):  # landing at the gate, then the flight along the wall
            s_b = min((cs - inner, cs + inner), key=lambda v: abs(v - ts))
            toward = 1.0 if ts > s_b else -1.0
            gate_half = (s_b - toward * 1.5, tt - 1.2)
            landing = (ts, tt - 1.2)
            links.append((f"GARTEN_TOR_{k + 1}", xz(*gate_half), xz(*landing)))
            links.append((f"GARTEN_TREPPE_{k + 1}", xz(*landing), xz(*b)))
            continue
        links.append((f"GARTEN_TREPPE_{k + 1}", xz(*a), xz(*b)))
        for px, pz in posterns:  # a Zwinger postern right in front of the flight's foot
            bx, bz = xz(*b)
            if math.hypot(bx - px, bz - pz) < 4.0:
                beyond = xz(ts + ds * (length + 4.5), tt + dt * (length + 4.5))
                links.append((f"GARTEN_PFORTE_{k + 1}", (bx, bz), beyond))
    return links
