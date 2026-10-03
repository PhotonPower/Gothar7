"""Face morph targets (data/faces/morphs.toml): contract names and the MPFB targets they mix.

Pure Python: read by the Blender scripts (mpfb_human.py loads the sources, conform_human.py builds
the targets) and by tests. The names and their order are the contract with engine (§6).
"""

from __future__ import annotations

import re
import tomllib
from dataclasses import dataclass
from importlib import resources

FORMAT_VERSION = 1
MAX_TARGETS = 16  # per mesh (engine: skinning shader weight palette)
_SOURCE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")


class FaceError(Exception):
    """The morph table is malformed."""


@dataclass(frozen=True)
class Morph:
    name: str
    mix: tuple[tuple[str, float], ...]  # (MPFB target, weight)


def parse_morphs(data: dict, contract: tuple[str, ...] | None = None) -> tuple[Morph, ...]:
    """Morph table; with `contract`, names and order must equal the rig's morph_targets."""
    if data.get("version") != FORMAT_VERSION:
        raise FaceError(f"version must be {FORMAT_VERSION}")
    morphs = []
    for i, raw in enumerate(data.get("morph", [])):
        name = raw.get("name")
        mix = raw.get("mix")
        if not isinstance(name, str) or not name:
            raise FaceError(f"morph #{i}: missing name")
        if not isinstance(mix, dict) or not mix:
            raise FaceError(f"morph '{name}': mix must name at least one MPFB target")
        for source, weight in mix.items():
            if not _SOURCE.match(source):
                raise FaceError(f"morph '{name}': bad MPFB target name '{source}'")
            if not isinstance(weight, int | float) or not 0.0 < weight <= 1.5:
                raise FaceError(f"morph '{name}': weight of '{source}' must be in (0, 1.5]")
        morphs.append(Morph(name, tuple((s, float(w)) for s, w in mix.items())))
    names = [m.name for m in morphs]
    if len(set(names)) != len(names):
        raise FaceError("duplicate morph names")
    if len(morphs) > MAX_TARGETS:
        raise FaceError(f"{len(morphs)} morphs exceed the engine limit of {MAX_TARGETS} per mesh")
    if contract is not None and tuple(names) != contract:
        raise FaceError(f"morph names/order {names} differ from the rig contract {list(contract)}")
    return tuple(morphs)


def load_morphs(contract: tuple[str, ...] | None = None) -> tuple[Morph, ...]:
    text = resources.files("gothar_chargen.data.faces").joinpath("morphs.toml").read_text("utf-8")
    try:
        return parse_morphs(tomllib.loads(text), contract)
    except tomllib.TOMLDecodeError as e:
        raise FaceError(str(e)) from e


def source_targets(morphs: tuple[Morph, ...]) -> list[str]:
    """All MPFB targets the table needs, in first-use order."""
    seen: dict[str, None] = {}
    for m in morphs:
        for source, _ in m.mix:
            seen.setdefault(source, None)
    return list(seen)
