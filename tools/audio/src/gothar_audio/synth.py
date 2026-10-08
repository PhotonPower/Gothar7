"""Small synthesis for placeholder sounds (M13, owner decision: own placeholders until CC0 packs or real recordings).

Everything is deterministic (a seeded random source), mono floats in [-1, 1] at RATE; `write_wav` stores 16-bit PCM.
"""

from __future__ import annotations

import math
import random
import struct
import wave
from collections.abc import Callable, Sequence
from pathlib import Path

RATE = 22050

Samples = list[float]


def silence(seconds: float) -> Samples:
    return [0.0] * int(seconds * RATE)


def tone(hz: float | Callable[[float], float], seconds: float, wave_form: str = "sine") -> Samples:
    """A tone; `hz` may change over time (a glide). Wave forms: sine, saw, square."""
    out: Samples = []
    phase = 0.0
    for i in range(int(seconds * RATE)):
        t = i / RATE
        f = hz(t) if callable(hz) else hz
        phase += f / RATE
        p = phase % 1.0
        if wave_form == "saw":
            out.append(2.0 * p - 1.0)
        elif wave_form == "square":
            out.append(1.0 if p < 0.5 else -1.0)
        else:
            out.append(math.sin(2.0 * math.pi * p))
    return out


def noise(seconds: float, rng: random.Random) -> Samples:
    return [rng.uniform(-1.0, 1.0) for _ in range(int(seconds * RATE))]


def lowpass(samples: Samples, cutoff_hz: float) -> Samples:
    """One-pole low-pass."""
    a = 1.0 - math.exp(-2.0 * math.pi * cutoff_hz / RATE)
    out: Samples = []
    y = 0.0
    for x in samples:
        y += a * (x - y)
        out.append(y)
    return out


def highpass(samples: Samples, cutoff_hz: float) -> Samples:
    low = lowpass(samples, cutoff_hz)
    return [x - lo for x, lo in zip(samples, low, strict=True)]


def envelope(samples: Samples, attack: float, decay: float) -> Samples:
    """Linear attack (s), then exponential decay with time constant `decay` (s)."""
    out: Samples = []
    for i, x in enumerate(samples):
        t = i / RATE
        gain = t / attack if attack > 0 and t < attack else math.exp(-(t - attack) / decay)
        out.append(x * gain)
    return out


def fade(samples: Samples, fade_in: float, fade_out: float) -> Samples:
    n = len(samples)
    a, b = int(fade_in * RATE), int(fade_out * RATE)
    out = list(samples)
    for i in range(min(a, n)):
        out[i] *= i / a
    for i in range(min(b, n)):
        out[n - 1 - i] *= i / b
    return out


def mix(*parts: tuple[Samples, float]) -> Samples:
    """Sum of (samples, gain); the length of the longest."""
    n = max(len(s) for s, _ in parts)
    out = [0.0] * n
    for s, g in parts:
        for i, x in enumerate(s):
            out[i] += x * g
    return out


def normalise(samples: Samples, peak: float = 0.8) -> Samples:
    top = max((abs(x) for x in samples), default=0.0)
    return list(samples) if top == 0.0 else [x * peak / top for x in samples]


def write_wav(path: Path, samples: Sequence[float]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, x)) * 32767)) for x in samples)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(frames)
