"""Placeholder music (M13 part C, owner decision 2: own synthesized placeholders until real music).

Each segment is a whole number of bars at its theme's tempo; note tails wrap around to the start, so a segment
chained to itself (or to another of its set) has no click. Tempos are chosen so a bar is a whole number of
frames at 22.05 kHz and at the mixer's 48 kHz (96, 120, 80, 150 BPM) - the engine changes on bar boundaries.
"""

from __future__ import annotations

import math
import random
from collections.abc import Callable
from dataclasses import dataclass

from gothar_audio.synth import RATE, Samples, lowpass, noise, normalise

# Semitones above A4 (440 Hz).
NOTE = {"C": -9, "D": -7, "E": -5, "F": -4, "G": -2, "A": 0, "B": 2}


def hz(name: str) -> float:
    """'A4', 'C#3', 'Bb2' -> frequency."""
    letter, rest = name[0], name[1:]
    shift = 0
    while rest and rest[0] in "#b":
        shift += 1 if rest[0] == "#" else -1
        rest = rest[1:]
    semis = NOTE[letter] + shift + 12 * (int(rest) - 4)
    return 440.0 * 2.0 ** (semis / 12.0)


@dataclass(frozen=True)
class Piece:
    bpm: float
    beats: int
    bars: int

    @property
    def beat(self) -> float:
        return 60.0 / self.bpm

    @property
    def frames(self) -> int:
        return round(self.bars * self.beats * self.beat * RATE)


def render(piece: Piece, notes: list[tuple[float, Samples, float]]) -> Samples:
    """Adds each (start beat, sound, gain) into a buffer of the piece's length; tails wrap to the start."""
    out = [0.0] * piece.frames
    n = len(out)
    for start, sound, gain in notes:
        at = round(start * piece.beat * RATE)
        for i, x in enumerate(sound):
            out[(at + i) % n] += x * gain
    return out


def pluck(freq: float, seconds: float, bright: float = 0.5) -> Samples:
    """A plucked string: a few decaying partials (lute, harp)."""
    out = []
    for i in range(int(seconds * RATE)):
        t = i / RATE
        env = math.exp(-t * 4.0) * min(1.0, t / 0.003)
        s = 0.0
        for k, g in ((1, 1.0), (2, 0.5 * bright), (3, 0.3 * bright), (4, 0.15 * bright)):
            s += g * math.exp(-t * 2.0 * k) * math.sin(2 * math.pi * freq * k * t)
        out.append(s * env)
    return out


def pad(freqs: list[float], seconds: float, vibrato: float = 4.5) -> Samples:
    """A soft sustained chord (strings, choir), swelling in and out."""
    out = []
    for i in range(int(seconds * RATE)):
        t = i / RATE
        env = math.sin(math.pi * min(t / seconds, 1.0)) ** 1.5
        s = sum(
            math.sin(2 * math.pi * f * t + 0.003 * f * math.sin(2 * math.pi * vibrato * t)) for f in freqs
        )
        out.append(env * s / len(freqs))
    return out


def bass(freq: float, seconds: float) -> Samples:
    out = []
    for i in range(int(seconds * RATE)):
        t = i / RATE
        env = math.exp(-t * 2.5) * min(1.0, t / 0.01)
        out.append(env * (math.sin(2 * math.pi * freq * t) + 0.3 * math.sin(4 * math.pi * freq * t)))
    return out


def kick(seconds: float = 0.35) -> Samples:
    out = []
    phase = 0.0
    for i in range(int(seconds * RATE)):
        t = i / RATE
        phase += 2 * math.pi * (45.0 + 90.0 * math.exp(-t * 30.0)) / RATE
        out.append(math.sin(phase) * math.exp(-t * 9.0))
    return out


def drum(rng: random.Random, seconds: float = 0.25, cutoff: float = 1800.0) -> Samples:
    hit = lowpass(noise(seconds, rng), cutoff)
    return [x * math.exp(-i / RATE * 18.0) for i, x in enumerate(hit)]


def drone(freqs: list[float], seconds: float, pulse_hz: float) -> Samples:
    """A dark held chord with a slow pulse (threat). It lasts the whole segment: each frequency is rounded to a
    whole number of periods in it, so the loop joins without a jump."""
    freqs = [round(f * seconds) / seconds for f in freqs]
    out = []
    for i in range(int(seconds * RATE)):
        t = i / RATE
        p = 0.65 + 0.35 * math.sin(2 * math.pi * pulse_hz * t)
        s = sum(math.sin(2 * math.pi * f * t) + 0.2 * math.sin(6 * math.pi * f * t) for f in freqs)
        out.append(p * s / len(freqs))
    return out


def chord(names: str) -> list[float]:
    return [hz(n) for n in names.split()]


def arpeggio(piece: Piece, chords: list[str], pattern: list[int], gain: float, bright: float) -> list:
    """One chord per bar, its notes plucked in `pattern` (indices into the chord), one per eighth."""
    notes = []
    for bar, names in enumerate(chords):
        tones = chord(names)
        for step, index in enumerate(pattern):
            notes.append(
                (bar * piece.beats + step * 0.5, pluck(tones[index % len(tones)], 1.2, bright), gain)
            )
    return notes


# --- the segments -----------------------------------------------------------------------------------------

LAGER = Piece(bpm=96, beats=4, bars=4)  # 10 s
STADT = Piece(bpm=120, beats=4, bars=4)  # 8 s
THREAT = Piece(bpm=80, beats=4, bars=2)  # 6 s
FIGHT = Piece(bpm=150, beats=4, bars=4)  # 6.4 s


def lager_day(rng: random.Random) -> Samples:
    # D major pentatonic around the fire: lute arpeggios over a quiet pad.
    chords = ["D3 A3 D4 F#4", "G3 B3 D4 G4", "B2 F#3 B3 D4", "A2 E3 A3 C#4"]
    notes = arpeggio(LAGER, chords, [0, 1, 2, 3, 2, 1, 2, 3], 0.5, 0.6)
    for bar, names in enumerate(chords):
        notes.append((bar * 4, pad(chord(names)[1:], 4 * LAGER.beat), 0.18))
        notes.append((bar * 4, bass(chord(names)[0] / 2, 2.0), 0.4))
    return normalise(render(LAGER, notes), 0.7)


def lager_night(rng: random.Random) -> Samples:
    chords = ["B2 F#3 B3 D4", "G2 D3 G3 B3", "D3 A3 D4 F#4", "A2 E3 A3 C#4"]
    notes = arpeggio(LAGER, chords, [0, 2, 1, 3, -1, -1, -1, -1], 0.4, 0.3)
    notes = [n for n in notes if n[0] % 4 < 2]  # half the bar: sparse, sleepy
    for bar, names in enumerate(chords):
        notes.append((bar * 4, pad(chord(names), 4 * LAGER.beat, 3.0), 0.25))
    return normalise(render(LAGER, notes), 0.6)


def stadt_day(rng: random.Random) -> Samples:
    # G major, brisker: a market tune with a light drum.
    chords = ["G3 B3 D4 G4", "C4 E4 G4 C5", "D4 F#4 A4 D5", "G3 B3 D4 G4"]
    notes = arpeggio(STADT, chords, [0, 2, 1, 3, 0, 2, 3, 2], 0.45, 0.8)
    for bar, names in enumerate(chords):
        for beat in range(4):
            notes.append((bar * 4 + beat, bass(chord(names)[0] / 2, 0.45), 0.35 if beat % 2 == 0 else 0.2))
            notes.append((bar * 4 + beat + 0.5, drum(rng, 0.12, 4000.0), 0.12))
    return normalise(render(STADT, notes), 0.7)


def stadt_night(rng: random.Random) -> Samples:
    chords = ["E3 G3 B3 E4", "C3 E3 G3 C4", "D3 F#3 A3 D4", "B2 D3 F#3 B3"]
    notes = arpeggio(STADT, chords, [0, 1, 2, 3, 2, 1, 0, 1], 0.3, 0.3)
    notes = [n for n in notes if int(n[0] * 2) % 2 == 0]  # quarters only
    for bar, names in enumerate(chords):
        notes.append((bar * 4, pad(chord(names), 4 * STADT.beat, 3.5), 0.3))
    return normalise(render(STADT, notes), 0.6)


def threat(rng: random.Random) -> Samples:
    # D minor drone pulsing with the beat, a low heartbeat.
    held = drone(chord("D2 A2 F3"), THREAT.bars * THREAT.beats * THREAT.beat, THREAT.bpm / 60.0 / 2.0)
    notes = [(0.0, held, 0.5)]
    for beat in range(THREAT.bars * THREAT.beats):
        notes.append((beat, kick(0.4), 0.45))
        notes.append((beat + 0.3, kick(0.3), 0.25))
    return normalise(render(THREAT, notes), 0.65)


def fight(rng: random.Random) -> Samples:
    # A minor, driving drums and a hammered low riff.
    riff = ["A2", "A2", "C3", "A2", "E3", "D3", "C3", "B2"]
    notes = []
    for bar in range(FIGHT.bars):
        for step in range(8):
            at = bar * 4 + step * 0.5
            notes.append((at, pluck(hz(riff[step]), 0.4, 1.0), 0.45))
            if step % 2 == 0:
                notes.append((at, kick(), 0.6))
            if step % 4 == 2:
                notes.append((at, drum(rng, 0.2, 2500.0), 0.5))
            notes.append((at + 0.25, drum(rng, 0.06, 6000.0), 0.12))
        top = chord(["A3 C4 E4", "F3 A3 C4", "D3 F3 A3", "E3 G#3 B3"][bar])
        notes.append((bar * 4, pad(top, 4 * FIGHT.beat, 6.0), 0.2))
    return normalise(render(FIGHT, notes), 0.75)


def _stinger(tones: list[str], step: float, last: float, bright: float) -> Samples:
    notes: Samples = [0.0] * int((step * len(tones) + last) * RATE)
    for i, name in enumerate(tones):
        sound = pluck(hz(name), last if i == len(tones) - 1 else 1.0, bright)
        at = int(i * step * RATE)
        for j, x in enumerate(sound[: len(notes) - at]):
            notes[at + j] += x
    return normalise(notes, 0.7)


def stinger_quest(rng: random.Random) -> Samples:
    return _stinger(["D4", "F#4", "A4", "D5"], 0.12, 1.6, 0.7)


def stinger_level_up(rng: random.Random) -> Samples:
    return _stinger(["G4", "B4", "D5", "G5", "B5"], 0.09, 1.8, 0.9)


def stinger_death(rng: random.Random) -> Samples:
    return _stinger(["A3", "F3", "D3", "A2"], 0.35, 2.4, 0.3)


def stinger_chapter(rng: random.Random) -> Samples:
    tones = ["D3", "A3", "D4", "F#4", "A4", "D5"]
    plucked = _stinger(tones, 0.18, 2.5, 0.6)
    swell = pad(chord("D3 A3 D4 F#4"), len(plucked) / RATE)
    return normalise([a + 0.4 * b for a, b in zip(plucked, swell, strict=True)], 0.75)


MUSIC: dict[str, Callable[[random.Random], Samples]] = {
    "lager_std_day": lager_day,
    "lager_std_ngt": lager_night,
    "stadt_std_day": stadt_day,
    "stadt_std_ngt": stadt_night,
    "common_thr": threat,
    "common_fgt": fight,
    "stinger_quest": stinger_quest,
    "stinger_level_up": stinger_level_up,
    "stinger_death": stinger_death,
    "stinger_chapter": stinger_chapter,
}

PIECES = {"lager": LAGER, "stadt": STADT, "common_thr": THREAT, "common_fgt": FIGHT}


def generate_music(name: str, seed: int = 11) -> Samples:
    return MUSIC[name](random.Random(f"{seed}:{name}"))
