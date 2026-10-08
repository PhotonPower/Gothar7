"""The placeholder sounds of the game (M13 part A): one generator per sound name of data/sounds.toml.

Names follow the animation events `sound:<name>` (figuren's clips) and the spells. Real sounds replace them later
(CC0 packs with a licence note in assets/LICENSES.md, or recordings); the file names stay.
"""

from __future__ import annotations

import math
import random
from collections.abc import Callable

from gothar_audio.synth import (
    Samples,
    envelope,
    fade,
    highpass,
    lowpass,
    mix,
    noise,
    normalise,
    silence,
    tone,
)


def anvil_hit(rng: random.Random) -> Samples:
    partials = [(523.0, 1.0), (1247.0, 0.6), (2371.0, 0.4), (3601.0, 0.25)]
    ring = mix(
        *[(envelope(tone(f, 0.9), 0.002, 0.25 / (1 + i * 0.4)), g) for i, (f, g) in enumerate(partials)]
    )
    click = envelope(highpass(noise(0.05, rng), 2000.0), 0.0005, 0.01)
    return normalise(mix((ring, 1.0), (click, 0.6)))


def wood_chop(rng: random.Random) -> Samples:
    thump = envelope(tone(110.0, 0.3), 0.002, 0.06)
    crack = envelope(lowpass(noise(0.3, rng), 2500.0), 0.001, 0.04)
    return normalise(mix((thump, 1.0), (crack, 0.8)))


def schinder_call(rng: random.Random) -> Samples:
    def glide(t: float) -> float:
        return 420.0 + 260.0 * math.sin(math.pi * min(t / 1.1, 1.0)) + 12.0 * math.sin(2 * math.pi * 6.0 * t)

    howl = fade(tone(glide, 1.2, "saw"), 0.15, 0.4)
    return normalise(lowpass(mix((howl, 1.0), (noise(1.2, rng), 0.05)), 1800.0))


def quaderbuckel_grind(rng: random.Random) -> Samples:
    rumble = lowpass(noise(0.9, rng), 300.0)
    wobble = [x * (0.6 + 0.4 * math.sin(2 * math.pi * 9.0 * i / 22050)) for i, x in enumerate(rumble)]
    return normalise(fade(wobble, 0.05, 0.3))


def glemsmahr_hiss(rng: random.Random) -> Samples:
    return normalise(fade(highpass(noise(0.8, rng), 3000.0), 0.08, 0.4), 0.6)


def bergleu_roar(rng: random.Random) -> Samples:
    def growl(t: float) -> float:
        return 85.0 + 25.0 * math.sin(math.pi * min(t / 1.5, 1.0))

    body = lowpass(tone(growl, 1.6, "saw"), 700.0)
    breath = lowpass(noise(1.6, rng), 1200.0)
    return normalise(fade(mix((body, 1.0), (breath, 0.5)), 0.2, 0.5))


def spell_cast(rng: random.Random) -> Samples:
    sweep = fade(tone(lambda t: 300.0 + 900.0 * t / 0.5, 0.5), 0.02, 0.2)
    shimmer = fade(highpass(noise(0.5, rng), 4000.0), 0.1, 0.3)
    return normalise(mix((sweep, 1.0), (shimmer, 0.3)))


def fire_impact(rng: random.Random) -> Samples:
    boom = envelope(tone(60.0, 0.7), 0.003, 0.12)
    crackle = envelope(lowpass(noise(0.7, rng), 3000.0), 0.002, 0.18)
    return normalise(mix((boom, 1.0), (crackle, 0.9)))


def heal(rng: random.Random) -> Samples:
    notes = [523.25, 659.25, 783.99, 1046.5]  # a rising C major arpeggio
    return normalise(
        mix(*[(silence(0.12 * i) + envelope(tone(f, 0.6), 0.01, 0.2), 0.5) for i, f in enumerate(notes)]), 0.6
    )


def summon(rng: random.Random) -> Samples:
    swell = fade(lowpass(tone(55.0, 1.2, "saw"), 400.0), 0.6, 0.4)
    air = fade(lowpass(noise(1.2, rng), 800.0), 0.5, 0.5)
    return normalise(mix((swell, 1.0), (air, 0.4)))


def seamless(samples: Samples, overlap: float = 0.5) -> Samples:
    """A loop without a click: the last `overlap` seconds are faded into the start."""
    n = int(overlap * 22050)
    body = samples[: len(samples) - n]
    tail = samples[len(samples) - n :]
    for i in range(n):
        w = i / n
        body[i] = body[i] * w + tail[i] * (1.0 - w)
    return body


def amb_wind(rng: random.Random) -> Samples:
    gust = [0.6 + 0.4 * math.sin(2 * math.pi * 0.15 * i / 22050) for i in range(int(6.5 * 22050))]
    air = lowpass(noise(6.5, rng), 500.0)
    return normalise(seamless([a * g for a, g in zip(air, gust, strict=True)]), 0.5)


def amb_camp(rng: random.Random) -> Samples:
    air = lowpass(noise(6.5, rng), 400.0)
    crackle = [0.0] * len(air)
    for _ in range(40):  # a fire somewhere: short crackles
        at = rng.randrange(len(air) - 2000)
        pop = envelope(highpass(noise(0.05, rng), 1500.0), 0.0005, 0.008)
        for i, x in enumerate(pop):
            crackle[at + i] += x * rng.uniform(0.3, 1.0)
    return normalise(seamless(mix((air, 0.6), (crackle, 0.5))), 0.5)


def amb_night(rng: random.Random) -> Samples:
    air = lowpass(noise(6.5, rng), 300.0)
    chirps = [0.0] * len(air)
    t = 0
    while t < len(air) - 4000:  # crickets: bursts of a high chirp
        burst = mix(
            *[(silence(0.035 * k) + envelope(tone(4400.0, 0.03), 0.002, 0.008), 1.0) for k in range(3)]
        )
        for i, x in enumerate(burst):
            chirps[t + i] += x
        t += int(rng.uniform(0.25, 0.7) * 22050)
    return normalise(seamless(mix((air, 0.4), (chirps, 0.25))), 0.4)


def bird(rng: random.Random) -> Samples:
    notes = [(rng.uniform(2200, 3400), rng.uniform(0.05, 0.12)) for _ in range(rng.randint(3, 5))]
    out: Samples = []
    for f, d in notes:  # each a short rising whistle
        out += envelope(tone(lambda t, f=f, d=d: f * (1.0 + 0.3 * t / d), d), 0.005, d / 3) + silence(0.04)
    return normalise(out, 0.6)


def owl(rng: random.Random) -> Samples:
    hoot = fade(lowpass(tone(380.0, 0.35), 900.0), 0.05, 0.15)
    return normalise(hoot + silence(0.25) + fade(lowpass(tone(360.0, 0.6), 900.0), 0.05, 0.3), 0.6)


#: name -> generator; the file is sounds/<name>.wav
SOUNDS: dict[str, Callable[[random.Random], Samples]] = {
    "amb_wind": amb_wind,
    "amb_camp": amb_camp,
    "amb_night": amb_night,
    "bird": bird,
    "owl": owl,
    "anvil_hit": anvil_hit,
    "wood_chop": wood_chop,
    "schinder_call": schinder_call,
    "quaderbuckel_grind": quaderbuckel_grind,
    "glemsmahr_hiss": glemsmahr_hiss,
    "bergleu_roar": bergleu_roar,
    "spell_cast": spell_cast,
    "fire_impact": fire_impact,
    "heal": heal,
    "summon": summon,
}


def generate(name: str, seed: int = 7) -> Samples:
    """The sound `name`, the same for the same seed."""
    return SOUNDS[name](random.Random(f"{seed}:{name}"))
