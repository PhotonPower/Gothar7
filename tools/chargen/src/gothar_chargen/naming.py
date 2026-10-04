"""Naming conventions for clips and events (characters-pipeline.md §3)."""

from __future__ import annotations

import re
from functools import cache

from gothar_chargen.skeleton import packaged_species

MODES = ("none", "fist", "1h", "2h", "bow", "cbow", "mag", "swim", "dive", "dlg", "amb")

# <mode>/<type>_<action>[_<variant>][_t<talent>], mode may be mob/<mobtype>
CLIP_NAME_RE = re.compile(
    r"^(?:(?:" + "|".join(MODES) + r")|mob/[a-z0-9]+(?:_[a-z0-9]+)*)"
    r"/[sta]_[a-z0-9]+(?:_[a-z0-9]+)*$"
)
# monsters (§7): <species>/<type>_<action>, species = a packaged monster rig
MONSTER_CLIP_RE = re.compile(r"^(?P<species>[a-z][a-z0-9_]*)/[sta]_[a-z0-9]+(?:_[a-z0-9]+)*$")

# footstep_l, hit_start, sound:sword_swing_01 ...
EVENT_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*(?::[a-z0-9_./-]+)?$")


@cache
def _species() -> frozenset[str]:
    return frozenset(packaged_species())


def clip_mode(name: str) -> str:
    """``wolf/s_walk`` -> ``wolf``; ``mob/chest/t_open`` -> ``mob/chest``."""
    return name.rsplit("/", 1)[0]


def is_monster_clip(name: str, species: str | None = None) -> bool:
    m = MONSTER_CLIP_RE.match(name)
    if m is None or m["species"] not in _species():
        return False
    return species is None or m["species"] == species


def is_clip_name(name: str) -> bool:
    return CLIP_NAME_RE.match(name) is not None or is_monster_clip(name)


def is_loop_clip(name: str) -> bool:
    """State/loop clips (``s_*``): their last frame repeats frame 0."""
    return name.rsplit("/", 1)[-1].startswith("s_")


def is_additive_clip(name: str) -> bool:
    """Additive overlay clips (``a_*``, dialogue gestures): deviation from their first frame,
    played from spine_02 upwards (characters-pipeline.md §3)."""
    return name.rsplit("/", 1)[-1].startswith("a_")


ADDITIVE_ROOT = "spine_02"  # additive clips move this bone and the bones below it only


def is_event_name(name: str) -> bool:
    return EVENT_NAME_RE.match(name) is not None
