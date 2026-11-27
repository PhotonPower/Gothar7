"""Can every part of a level be reached, and is there always a way back up? (plan 3.3 checks)

The search runs over (open movers, cell): walking between floor cells (closed gates and secret
walls block, doors open by hand), teleporters, and switches and plates that set their targets
(a plate can be held with an item, so both of its states count). Reaching a trapdoor is a fall
into the level below (allowed, plan F6); every other place must keep a way to the stairs up. A
trapdoor that drops into the same level (W8 E7) leads on to its arrival cell, from where the way
back must be found like from anywhere else.

Relays (``relays`` in the JSON) pass events on as the engine does (dungeon.md "Stand D1"): ``any``
forwards the "on" side as its action and the "off" side as the counterpart; ``all`` fires when all
its inputs (the senders naming it) are on and the counterpart when one goes off again; ``count``
fires every ``count`` "on" events; ``invert`` and ``once`` as in the engine. Their inputs, counters
and spent flags are part of the search state, and so is whether a teleporter is on (``active``;
``on``/``open`` switch it on, ``off``/``close`` off, ``toggle`` over): an inactive pad is floor.
A plate's release sends its ``release_action`` if it has one; a switch with ``once`` works once.
A lever goes back (the counterpart) and a plate is released; a wall button only ever sends its
``action`` (dungeon.md "Stand D3").

A relay with ``delay`` (a time gate, W8 E4) does not fire at once: its event waits while the hero
runs ``window`` cells (``(delay - TIMER_MARGIN_S) * RUN_M_S / cellM``, the margin for the clip and
the gate's travel), then arrives. So a gate that a button opens and a delayed relay shuts again is
open only for the next few cells: what lies further away than that stays shut. Pending events are
part of the search state with the cells still left. A sarcophagus (``props``) fills its cell.
A spinner (``spin``) only turns the hero on his cell: for the search it is floor.
"""

from __future__ import annotations

from collections import deque
from collections.abc import Iterator
from dataclasses import dataclass
from typing import Any

from gothar_worldgen.dungeon.level import DIRS, FLOOR_CHARS, Level, _cell

BLOCKING = {"gate", "secret_wall"}
OFF = {"close", "off", "lock"}  # the "off" side of an event (dungeon.md "Polarität")
COUNTER = {"open": "close", "close": "open", "on": "off", "off": "on", "lock": "unlock",
           "unlock": "lock"}  # fmt: skip
MAX_DEPTH = 16  # relays naming relays: a cycle stops here (the engine warns about cycles)
RUN_M_S = 4.0  # the hero's run (movement.toml [speed] run)
TIMER_MARGIN_S = 2.0  # of a delay: pressing the button (clip) and the gate's travel

State = frozenset[str]
Node = tuple[State, tuple[int, int]]
# targets, action, sender, release, once, goes back (lever, plate; not a wall button)
Action = tuple[list[str], str, str, str | None, bool, bool]


@dataclass(frozen=True)
class Reach:
    cells: set[tuple[int, int]]  # reached in any state
    unreached: list[tuple[int, int]]  # floor cells never reached
    stuck: list[tuple[int, int]]  # cells reached in some state without a way back up
    states: int  # distinct mover states seen


def _movers(level: Level) -> dict[str, tuple[int, int]]:
    return {level.name(at): at for at in level.cells(set("GH"))}


def _teleporters(level: Level) -> dict[str, tuple[int, int]]:
    return {level.name(at): at for at in level.cells("T")}


def _initial(level: Level) -> State:
    out = {name for name, at in _teleporters(level).items()
           if level.entries[at]["teleport"].get("active", True)}  # fmt: skip
    for name, at in _movers(level).items():
        spec = level.entries[at][level.kind(at)]  # type: ignore[index]
        if spec.get("open"):
            out.add(name)
    return frozenset(out)


def _counter(action: str) -> str:
    return COUNTER.get(action, action)


def _senders(level: Level) -> list[tuple[str, dict[str, Any]]]:
    out = [(level.name(at), level.entries[at]["switch" if level.char(*at) == "S" else "plate"])
           for at in level.cells(set("SO"))]  # fmt: skip
    return out + list(level.relays.items())


def _inputs(level: Level) -> dict[str, list[str]]:
    """Relay -> the senders naming it (switches, plates and other relays)."""
    out: dict[str, list[str]] = {name: [] for name in level.relays}
    for name, spec in _senders(level):
        for t in spec.get("targets", []):
            if t in out:
                out[t].append(name)
    return out


class _Net:
    """Sends events through movers and relays: a pure step from one state to the next."""

    def __init__(self, level: Level, movers: set[str]) -> None:
        self.level = level
        self.movers = movers | set(_teleporters(level))  # both are on/off
        self.inputs = _inputs(level)

    def window(self, relay: dict[str, Any]) -> int:
        """Cells the hero runs before a delayed event arrives."""
        free = float(relay.get("delay", 0.0)) - TIMER_MARGIN_S
        return max(0, int(free * RUN_M_S / self.level.cell_m))

    def tick(self, state: State) -> State:
        """One cell run: pending events come one cell closer; those due arrive."""
        pend = sorted(f for f in state if f.startswith("PEND:"))
        if not pend:
            return state
        out = set(state) - set(pend)
        due = []
        for f in pend:
            _, name, act, n = f.split(":")
            if int(n) <= 1:
                due.append((name, act))
            else:
                out.add(f"PEND:{name}:{act}:{int(n) - 1}")
        for name, act in due:
            for t in self.level.relays[name]["targets"]:
                self._one(out, t, act, name, 1)
        return frozenset(out)

    def send(self, state: State, targets: list[str], action: str, source: str) -> State:
        out = set(state)
        for t in targets:
            self._one(out, t, action, source, 0)
        return frozenset(out)

    def _one(self, s: set[str], target: str, action: str, source: str, depth: int) -> None:
        if depth > MAX_DEPTH:
            return
        if target in self.movers:
            if action in ("open", "on"):
                s.add(target)
            elif action in ("close", "off"):
                s.discard(target)
            elif action in ("toggle", "trigger"):
                s ^= {target}
            return
        r = self.level.relays.get(target)
        if r is None:
            return
        on = action not in OFF
        act = str(r.get("action", "trigger"))
        mode = r.get("mode", "any")
        if mode == "any":
            self._fire(s, target, r, act if on else _counter(act), depth)
        elif mode == "all":
            flags = [f"IN:{target}:{i}" for i in self.inputs[target]]
            before = all(f in s for f in flags)
            flag = f"IN:{target}:{source}"
            if action in ("toggle", "trigger"):
                s ^= {flag}
            elif on:
                s.add(flag)
            else:
                s.discard(flag)
            after = all(f in s for f in flags)
            if after and not before:
                self._fire(s, target, r, act, depth)
            elif before and not after:
                self._fire(s, target, r, _counter(act), depth)
        elif on:  # count: every ``count`` events of the "on" side
            key = f"CNT:{target}:"
            n = next((int(f[len(key) :]) for f in s if f.startswith(key)), 0)
            s.discard(f"{key}{n}")
            n += 1
            if n >= int(r["count"]):
                self._fire(s, target, r, act, depth)
            else:
                s.add(f"{key}{n}")

    def _fire(self, s: set[str], name: str, r: dict[str, Any], action: str, depth: int) -> None:
        if r.get("once"):
            if f"SPENT:{name}" in s:
                return
            s.add(f"SPENT:{name}")
        act = _counter(action) if r.get("invert") else action
        n = self.window(r) if float(r.get("delay", 0.0)) > 0 else 0
        if n > 0:  # arrives after ``n`` cells run
            s.add(f"PEND:{name}:{act}:{n}")
            return
        for t in r["targets"]:
            self._one(s, t, act, name, depth + 1)


def _actions(level: Level) -> dict[tuple[int, int], list[Action]]:
    """Floor cell -> what can be set from there: (targets, action, sender)."""
    out: dict[tuple[int, int], list[Action]] = {}
    for at in level.cells(set("SO")):
        spec = level.entries[at]["switch" if level.char(*at) == "S" else "plate"]
        if level.char(*at) == "S":
            dx, dy = DIRS[level.facing(at)]
            stand = (at[0] + dx, at[1] + dy)
        else:
            stand = at
        back = spec.get("type") != "wall_button"
        out.setdefault(stand, []).append(
            (list(spec["targets"]), spec.get("action", "trigger"), level.name(at),
             spec.get("release_action"), bool(spec.get("once")), back)
        )  # fmt: skip
    return out


def _set(net: _Net, state: State, targets: list[str], action: str, sender: str,
         release: str | None, back: bool = True) -> Iterator[State]:  # fmt: skip
    yield net.send(state, targets, action, sender)
    if not back:  # a wall button: only its action
        return
    if release:  # a plate's own release
        yield net.send(state, targets, release, sender)
    elif action in COUNTER:  # a plate or lever also goes back (the counterpart)
        yield net.send(state, targets, _counter(action), sender)


def _passable(level: Level, at: tuple[int, int], state: State, water: bool = True) -> bool:
    if not level.is_floor(*at) or (not water and level.char(*at) == "~"):
        return False
    if at in level.blocked:
        return False
    if level.char(*at) in "GH":
        return level.name(at) in state
    return True


def _steps(level: Level, node: Node, acts: dict[tuple[int, int], list[Action]], net: _Net,
           water: bool = True) -> Iterator[Node]:  # fmt: skip
    state, at = node
    if level.char(*at) == "P":
        arrival = level.same_level_fall(at)
        if arrival is not None:  # down into the lower part of this level (W8 E7)
            yield net.tick(state), arrival["at"]
        return  # fell through the trapdoor
    if level.char(*at) == "T" and level.name(at) in state:
        spec = level.entries[at]["teleport"]
        if "to" in spec:
            yield net.tick(state), _cell(spec["to"], "teleport")
            return
    for dx, dy in DIRS.values():
        nxt = (at[0] + dx, at[1] + dy)
        if _passable(level, nxt, state, water):
            yield net.tick(state), nxt
    for targets, action, sender, release, once, back in acts.get(at, []):
        used = f"USED:{sender}"
        if once and used in state:
            continue
        for s in _set(net, state, targets, action, sender, release, back):
            if once:
                s = s | {used}
            if s != state:
                yield s, at


def check_reach(level: Level, water: bool = True) -> Reach:
    """``water=False``: the basins count as walls (no wading and climbing out); then the
    riddles must still open every dry cell."""
    movers = _movers(level)
    acts = _actions(level)
    net = _Net(level, set(movers))
    start = (_initial(level), level.cells("<")[0])
    seen: set[Node] = {start}
    edges: dict[Node, list[Node]] = {}
    queue = deque([start])
    while queue:
        node = queue.popleft()
        edges[node] = list(_steps(level, node, acts, net, water))
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
    floor = [a for a in level.cells(FLOOR_CHARS if water else FLOOR_CHARS - {"~"})
             if a not in level.blocked]  # fmt: skip
    out = {at for at in level.cells("P") if level.same_level_fall(at) is None}  # left the level
    stuck = sorted({n[1] for n in seen - ok if n[1] not in out})
    states = {frozenset(f for f in s if f in movers) for s, _ in seen}
    return Reach(cells, sorted(set(floor) - cells), stuck, len(states))
