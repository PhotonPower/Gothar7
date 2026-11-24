"""Can every part of a level be reached, and is there always a way back up? (plan 3.3 checks)

The search runs over (open movers, cell): walking between floor cells (closed gates and secret
walls block, doors open by hand), teleporters, and switches and plates that set their targets
(a plate can be held with an item, so both of its states count). Reaching a trapdoor is a fall
into the level below (allowed, plan F6); every other place must keep a way to the stairs up.
"""

from __future__ import annotations

from collections import deque
from collections.abc import Iterator
from dataclasses import dataclass

from gothar_worldgen.dungeon.level import DIRS, FLOOR_CHARS, Level, _cell

BLOCKING = {"gate", "secret_wall"}

State = frozenset[str]
Node = tuple[State, tuple[int, int]]


@dataclass(frozen=True)
class Reach:
    cells: set[tuple[int, int]]  # reached in any state
    unreached: list[tuple[int, int]]  # floor cells never reached
    stuck: list[tuple[int, int]]  # cells reached in some state without a way back up
    states: int


def _movers(level: Level) -> dict[str, tuple[int, int]]:
    return {level.name(at): at for at in level.cells(set("GH"))}


def _initial(level: Level) -> State:
    out = set()
    for name, at in _movers(level).items():
        spec = level.entries[at][level.kind(at)]  # type: ignore[index]
        if spec.get("open"):
            out.add(name)
    return frozenset(out)


def _actions(level: Level) -> dict[tuple[int, int], list[tuple[list[str], str]]]:
    """Floor cell -> what can be set from there: (targets, action)."""
    out: dict[tuple[int, int], list[tuple[list[str], str]]] = {}
    for at in level.cells(set("SO")):
        spec = level.entries[at]["switch" if level.char(*at) == "S" else "plate"]
        if level.char(*at) == "S":
            dx, dy = DIRS[level.facing(at)]
            stand = (at[0] + dx, at[1] + dy)
        else:
            stand = at
        out.setdefault(stand, []).append((list(spec["targets"]), spec.get("action", "trigger")))
    return out


def _set(state: State, targets: list[str], action: str, movers: set[str]) -> Iterator[State]:
    names = [t for t in targets if t in movers]
    if not names:
        return
    if action in ("toggle", "trigger"):
        yield state ^ frozenset(names)
    if action in ("open", "on", "close", "off"):  # a plate or lever also goes back
        yield state | frozenset(names)
        yield state - frozenset(names)


def _passable(level: Level, at: tuple[int, int], state: State, movers: dict[str, tuple[int, int]]
              ) -> bool:  # fmt: skip
    if not level.is_floor(*at):
        return False
    if level.char(*at) in "GH":
        return level.name(at) in state
    return True


def _steps(level: Level, node: Node, movers: dict[str, tuple[int, int]],
           acts: dict[tuple[int, int], list[tuple[list[str], str]]]) -> Iterator[Node]:  # fmt: skip
    state, at = node
    if level.char(*at) == "P":
        return  # fell through the trapdoor
    if level.char(*at) == "T":
        spec = level.entries[at]["teleport"]
        if "to" in spec:
            yield state, _cell(spec["to"], "teleport")
            return
    for dx, dy in DIRS.values():
        nxt = (at[0] + dx, at[1] + dy)
        if _passable(level, nxt, state, movers):
            yield state, nxt
    for targets, action in acts.get(at, []):
        for s in _set(state, targets, action, set(movers)):
            if s != state:
                yield s, at


def check_reach(level: Level) -> Reach:
    movers = _movers(level)
    acts = _actions(level)
    start = (_initial(level), level.cells("<")[0])
    seen: set[Node] = {start}
    edges: dict[Node, list[Node]] = {}
    queue = deque([start])
    while queue:
        node = queue.popleft()
        edges[node] = list(_steps(level, node, movers, acts))
        for nxt in edges[node]:
            if nxt not in seen:
                seen.add(nxt)
                queue.append(nxt)
    back: dict[Node, list[Node]] = {}
    for a, outs in edges.items():
        for b in outs:
            back.setdefault(b, []).append(a)
    home = {n for n in seen if level.char(*n[1]) == "<"}
    ok = set(home)
    queue = deque(home)
    while queue:
        for prev in back.get(queue.popleft(), []):
            if prev not in ok:
                ok.add(prev)
                queue.append(prev)
    cells = {at for _, at in seen}
    floor = level.cells(FLOOR_CHARS)
    stuck = sorted({at for n in seen - ok for at in [n[1]] if level.char(*at) != "P"})
    return Reach(cells, sorted(set(floor) - cells), stuck, len({s for s, _ in seen}))
