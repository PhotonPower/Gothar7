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


LOOP = 4.5  # s generated for the newer loops (4 s after the seamless overlap): smaller files


def scatter(length: int, count: int, make: Callable[[], Samples], rng: random.Random) -> Samples:
    """`count` short sounds made by `make`, at random places in `length` samples."""
    out = [0.0] * length
    for _ in range(count):
        s = make()
        at = rng.randrange(max(1, length - len(s)))
        for i, x in enumerate(s):
            out[at + i] += x
    return out


def murmur(seconds: float, voices: int, rng: random.Random) -> Samples:
    """People talking further away: band-limited noise in syllables."""
    n = int(seconds * 22050)
    out = [0.0] * n
    for _ in range(voices):
        talk = highpass(lowpass(noise(seconds, rng), rng.uniform(700.0, 1100.0)), 200.0)
        t = 0
        gain = [0.0] * n
        while t < n:  # syllables of 0.1-0.25 s, pauses between phrases
            d = int(rng.uniform(0.1, 0.25) * 22050)
            level = rng.uniform(0.4, 1.0) if rng.random() > 0.2 else 0.0
            for i in range(t, min(n, t + d)):
                gain[i] = level * math.sin(math.pi * (i - t) / d)
            t += d
        out = [o + x * g for o, x, g in zip(out, talk, gain, strict=True)]
    return out


def crackles(seconds: float, count: int, rng: random.Random) -> Samples:
    return scatter(
        int(seconds * 22050),
        count,
        lambda: [
            x * rng.uniform(0.3, 1.0) for x in envelope(highpass(noise(0.05, rng), 1500.0), 0.0005, 0.008)
        ],
        rng,
    )


def amb_field(rng: random.Random) -> Samples:
    air = lowpass(noise(LOOP, rng), 350.0)
    insects = [
        x * (0.5 + 0.5 * math.sin(2 * math.pi * 0.4 * i / 22050)) for i, x in enumerate(tone(3100.0, LOOP))
    ]
    return normalise(seamless(mix((air, 0.7), (insects, 0.02))), 0.45)


def amb_town(rng: random.Random) -> Samples:
    return normalise(
        seamless(mix((murmur(LOOP, 3, rng), 0.5), (lowpass(noise(LOOP, rng), 300.0), 0.6))), 0.45
    )


def amb_market(rng: random.Random) -> Samples:
    return normalise(
        seamless(mix((murmur(LOOP, 8, rng), 0.6), (lowpass(noise(LOOP, rng), 400.0), 0.3))), 0.55
    )


def amb_water(rng: random.Random) -> Samples:
    babble = lowpass(highpass(noise(LOOP, rng), 400.0), 2500.0)
    wobble = [
        x * (0.6 + 0.4 * math.sin(2 * math.pi * 3.1 * i / 22050 + math.sin(i / 9000.0)))
        for i, x in enumerate(babble)
    ]
    return normalise(seamless(wobble), 0.45)


def amb_fountain(rng: random.Random) -> Samples:
    splash = highpass(noise(LOOP, rng), 1200.0)
    drops = scatter(
        int(LOOP * 22050), 60, lambda: envelope(tone(rng.uniform(900, 1600), 0.04), 0.001, 0.01), rng
    )
    return normalise(seamless(mix((splash, 0.5), (drops, 0.4))), 0.5)


def amb_fire(rng: random.Random) -> Samples:
    roar = lowpass(noise(LOOP, rng), 180.0)
    return normalise(seamless(mix((roar, 1.0), (crackles(LOOP, 50, rng), 0.5))), 0.55)


def amb_tavern(rng: random.Random) -> Samples:
    return normalise(seamless(mix((murmur(LOOP, 5, rng), 0.6), (crackles(LOOP, 25, rng), 0.3))), 0.5)


def amb_room(rng: random.Random) -> Samples:
    return normalise(seamless(lowpass(noise(LOOP, rng), 150.0)), 0.5)  # quiet by its volume in sounds.toml


def crow(rng: random.Random) -> Samples:
    caw = lowpass(tone(lambda t: 700.0 - 300.0 * t / 0.3, 0.3, "saw"), 1800.0)
    one = fade(mix((caw, 1.0), (lowpass(noise(0.3, rng), 1500.0), 0.3)), 0.02, 0.1)
    return normalise(one + silence(0.15) + one, 0.6)


def dog_bark(rng: random.Random) -> Samples:
    woof = envelope(lowpass(tone(lambda t: 420.0 - 200.0 * t / 0.18, 0.18, "saw"), 1400.0), 0.005, 0.06)
    return normalise(woof + silence(0.2) + woof, 0.7)


def wood_creak(rng: random.Random) -> Samples:
    creak = lowpass(tone(lambda t: 180.0 + 60.0 * math.sin(2 * math.pi * 1.5 * t), 0.7, "saw"), 900.0)
    return normalise(fade(creak, 0.1, 0.2), 0.5)


def mug_clink(rng: random.Random) -> Samples:
    ring = mix(
        *[(envelope(tone(f, 0.4), 0.001, 0.08), g) for f, g in ((2100.0, 1.0), (3400.0, 0.5), (5200.0, 0.3))]
    )
    return normalise(ring, 0.5)


def frog(rng: random.Random) -> Samples:
    croak = [
        x * (1.0 if (i // 300) % 2 else 0.3) for i, x in enumerate(lowpass(tone(240.0, 0.35, "saw"), 1200.0))
    ]
    return normalise(fade(croak, 0.02, 0.1), 0.6)


# --- Footsteps (M13 E): six materials, four variants each (footstep_<material>_<n>) --------------------------


def footstep_stone(rng: random.Random) -> Samples:
    heel = envelope(tone(rng.uniform(90.0, 120.0), 0.12), 0.001, 0.025)
    click = envelope(highpass(noise(0.08, rng), 2500.0), 0.0005, 0.012)
    scrape = envelope(highpass(noise(0.12, rng), 1200.0), 0.01, 0.04)
    return normalise(mix((heel, 0.8), (click, 0.7), (scrape, 0.15)), rng.uniform(0.6, 0.8))


def footstep_wood(rng: random.Random) -> Samples:
    knock = envelope(tone(rng.uniform(150.0, 210.0), 0.2), 0.001, 0.05)
    body = envelope(tone(rng.uniform(380.0, 460.0), 0.15), 0.001, 0.03)
    tap = envelope(lowpass(noise(0.1, rng), 3000.0), 0.0005, 0.015)
    return normalise(mix((knock, 1.0), (body, 0.4), (tap, 0.5)), rng.uniform(0.6, 0.8))


def footstep_grass(rng: random.Random) -> Samples:
    rustle = envelope(highpass(lowpass(noise(0.2, rng), 5000.0), 900.0), 0.02, 0.07)
    thud = envelope(tone(rng.uniform(70.0, 90.0), 0.1), 0.003, 0.03)
    return normalise(mix((rustle, 1.0), (thud, 0.35)), rng.uniform(0.45, 0.6))


def footstep_dirt(rng: random.Random) -> Samples:
    thud = envelope(tone(rng.uniform(60.0, 85.0), 0.15), 0.002, 0.04)
    crunch = envelope(lowpass(noise(0.14, rng), 1800.0), 0.004, 0.04)
    return normalise(mix((thud, 1.0), (crunch, 0.6)), rng.uniform(0.5, 0.7))


def footstep_gravel(rng: random.Random) -> Samples:
    bits = crackles(0.16, rng.randint(9, 14), rng)
    base = envelope(lowpass(noise(0.16, rng), 2500.0), 0.005, 0.05)
    return normalise(mix((bits, 1.0), (base, 0.5)), rng.uniform(0.55, 0.7))


def footstep_water(rng: random.Random) -> Samples:
    out = envelope(highpass(lowpass(noise(0.35, rng), 4000.0), 400.0), 0.01, 0.12)
    for _ in range(3):
        f = rng.uniform(500.0, 900.0)
        bubble = envelope(tone(lambda t, f=f: f * (1.0 + 2.0 * t), 0.06), 0.002, 0.02)
        at = rng.randrange(len(out) - len(bubble))
        for i, x in enumerate(bubble):
            out[at + i] += 0.3 * x
    return normalise(fade(out, 0.005, 0.1), rng.uniform(0.5, 0.65))


FOOTSTEP_MATERIALS: dict[str, Callable[[random.Random], Samples]] = {
    "stone": footstep_stone,
    "wood": footstep_wood,
    "grass": footstep_grass,
    "dirt": footstep_dirt,
    "gravel": footstep_gravel,
    "water": footstep_water,
}
FOOTSTEP_VARIANTS = 4


#: name -> generator; the file is sounds/<name>.wav
SOUNDS: dict[str, Callable[[random.Random], Samples]] = {
    "amb_field": amb_field,
    "amb_town": amb_town,
    "amb_market": amb_market,
    "amb_water": amb_water,
    "amb_fountain": amb_fountain,
    "amb_fire": amb_fire,
    "amb_tavern": amb_tavern,
    "amb_room": amb_room,
    "crow": crow,
    "dog_bark": dog_bark,
    "wood_creak": wood_creak,
    "mug_clink": mug_clink,
    "frog": frog,
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
SOUNDS.update(
    {
        f"footstep_{material}_{n}": make
        for material, make in FOOTSTEP_MATERIALS.items()
        for n in range(1, FOOTSTEP_VARIANTS + 1)
    }
)


def generate(name: str, seed: int = 7) -> Samples:
    """The sound `name`, the same for the same seed."""
    return SOUNDS[name](random.Random(f"{seed}:{name}"))
