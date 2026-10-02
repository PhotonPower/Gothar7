"""Naming conventions for clips and events (characters-pipeline.md §3)."""

from __future__ import annotations

import re

MODES = ("none", "fist", "1h", "2h", "bow", "cbow", "mag", "swim", "dive", "dlg", "amb")

# <mode>/<type>_<action>[_<variant>][_t<talent>], mode may be mob/<mobtype>
CLIP_NAME_RE = re.compile(
    r"^(?:(?:" + "|".join(MODES) + r")|mob/[a-z0-9]+(?:_[a-z0-9]+)*)"
    r"/[sta]_[a-z0-9]+(?:_[a-z0-9]+)*$"
)

# footstep_l, hit_start, sound:sword_swing_01 ...
EVENT_NAME_RE = re.compile(r"^[a-z][a-z0-9_]*(?::[a-z0-9_./-]+)?$")


def is_clip_name(name: str) -> bool:
    return CLIP_NAME_RE.match(name) is not None


def is_loop_clip(name: str) -> bool:
    """State/loop clips (``s_*``): their last frame repeats frame 0."""
    return name.rsplit("/", 1)[-1].startswith("s_")


def is_event_name(name: str) -> bool:
    return EVENT_NAME_RE.match(name) is not None
