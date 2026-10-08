"""Placeholder sounds: every one is generated, deterministic, audible and within range; the WAV is 16-bit mono."""

from __future__ import annotations

import itertools
import wave
from pathlib import Path

from gothar_audio.placeholders import SOUNDS, generate
from gothar_audio.synth import RATE, write_wav


def test_every_sound_is_audible_in_range_and_short() -> None:
    for name in SOUNDS:
        s = generate(name)
        assert 0.05 * RATE < len(s) < 7.0 * RATE, name
        peak = max(abs(x) for x in s)
        assert 0.3 < peak <= 1.0, name


def test_deterministic() -> None:
    assert generate("anvil_hit") == generate("anvil_hit")
    assert generate("anvil_hit", seed=1) != generate("anvil_hit", seed=2)


def test_wav_file(tmp_path: Path) -> None:
    path = tmp_path / "x.wav"
    samples = generate("wood_chop")
    write_wav(path, samples)
    with wave.open(str(path), "rb") as w:
        assert w.getnchannels() == 1
        assert w.getsampwidth() == 2
        assert w.getframerate() == RATE
        assert w.getnframes() == len(samples)


def test_loops_are_seamless() -> None:
    for name in [n for n in SOUNDS if n.startswith("amb_")]:
        s = generate(name)
        inner = max(abs(b - a) for a, b in itertools.pairwise(s))
        assert abs(s[0] - s[-1]) <= inner, name  # the seam jumps no more than the sound itself does
