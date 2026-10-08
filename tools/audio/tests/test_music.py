"""Placeholder music: segments are whole bars (on whole frames at 22.05 and 48 kHz) and loop without a click;
stingers are short; everything is deterministic and audible."""

from __future__ import annotations

import itertools
from pathlib import Path

from gothar_audio.cli import main
from gothar_audio.music import MUSIC, PIECES, Piece, generate_music, hz, pluck, render
from gothar_audio.synth import RATE


def test_note_names() -> None:
    assert abs(hz("A4") - 440.0) < 1e-9
    assert abs(hz("A3") - 220.0) < 1e-9
    assert abs(hz("C#5") - hz("Db5")) < 1e-9
    assert abs(hz("C4") - 261.6256) < 1e-3


def test_bars_on_whole_frames_at_both_rates() -> None:
    for name, piece in PIECES.items():
        bar = piece.beats * piece.beat
        for rate in (RATE, 48000):
            assert abs(bar * rate - round(bar * rate)) < 1e-6, name


def test_segments_are_whole_bars_audible_and_seamless() -> None:
    for name, piece in PIECES.items():
        for key in MUSIC:
            if not key.startswith(name):
                continue
            s = generate_music(key)
            assert len(s) == piece.frames, key
            peak = max(abs(x) for x in s)
            assert 0.3 < peak <= 1.0, key
            # Chained to itself: the jump from the last sample to the first is no larger than within the piece.
            steps = max(abs(b - a) for a, b in itertools.pairwise(s))
            assert abs(s[0] - s[-1]) <= steps * 1.5, key


def test_tails_wrap_around() -> None:
    piece = Piece(bpm=120, beats=4, bars=1)
    out = render(piece, [(3.5, pluck(440.0, 1.0), 1.0)])  # rings 0.75 s past the end
    assert len(out) == piece.frames
    assert max(abs(x) for x in out[: RATE // 4]) > 0.01


def test_stingers_short_and_deterministic() -> None:
    for key in MUSIC:
        if key.startswith("stinger_"):
            s = generate_music(key)
            assert 0.5 * RATE < len(s) < 5.0 * RATE, key
            assert max(abs(x) for x in s) > 0.3, key
    assert generate_music("stinger_quest") == generate_music("stinger_quest")
    assert generate_music("common_fgt", seed=1) != generate_music("common_fgt", seed=2)


def test_cli_writes_music(tmp_path: Path) -> None:
    assert main(["music", "--out", str(tmp_path), "stinger_quest"]) == 0
    assert (tmp_path / "stinger_quest.wav").is_file()
