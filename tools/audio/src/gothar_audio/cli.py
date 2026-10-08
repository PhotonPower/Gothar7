"""gothar-audio: generates the placeholder sounds (M13, owner decision: own placeholders first)."""

from __future__ import annotations

import argparse
from pathlib import Path

from gothar_audio.music import MUSIC, generate_music
from gothar_audio.placeholders import SOUNDS, generate
from gothar_audio.synth import write_wav

DEFAULT_OUT = Path(__file__).resolve().parents[4] / "assets" / "source" / "sounds"
DEFAULT_MUSIC_OUT = Path(__file__).resolve().parents[4] / "assets" / "source" / "music"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="gothar-audio", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("placeholders", help="write sounds/<name>.wav for every placeholder sound")
    p.add_argument(
        "--out", type=Path, default=DEFAULT_OUT, help="output directory (default: assets/source/sounds)"
    )
    p.add_argument("names", nargs="*", help="only these sounds (default: all)")
    m = sub.add_parser("music", help="write music/<name>.wav for every placeholder music segment and stinger")
    m.add_argument(
        "--out", type=Path, default=DEFAULT_MUSIC_OUT, help="output directory (default: assets/source/music)"
    )
    m.add_argument("names", nargs="*", help="only these pieces (default: all)")
    args = parser.parse_args(argv)

    if args.command == "placeholders":
        names = args.names or sorted(SOUNDS)
        unknown = [n for n in names if n not in SOUNDS]
        if unknown:
            parser.error(f"unknown sounds: {', '.join(unknown)} (known: {', '.join(sorted(SOUNDS))})")
        for name in names:
            path = args.out / f"{name}.wav"
            write_wav(path, generate(name))
            print(path)
    elif args.command == "music":
        names = args.names or sorted(MUSIC)
        unknown = [n for n in names if n not in MUSIC]
        if unknown:
            parser.error(f"unknown pieces: {', '.join(unknown)} (known: {', '.join(sorted(MUSIC))})")
        for name in names:
            path = args.out / f"{name}.wav"
            write_wav(path, generate_music(name))
            print(path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
