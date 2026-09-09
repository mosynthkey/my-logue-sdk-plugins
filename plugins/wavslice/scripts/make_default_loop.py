#!/usr/bin/env python3
"""Write the stock 1-bar drum loop shipped with WavSlice.

This is an original backbeat (kick 1+3, snare 2+4, eighth hats). It is not
an amen, Funky Drummer, or any other copyrighted break. CC0 / public domain.
"""

from __future__ import annotations

import argparse
import math
import pathlib
import struct
import wave

SAMPLE_RATE = 44100
BPM = 120.0
SIXTEENTHS = 16


def lcg(state: int) -> tuple[int, float]:
    state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
    return state, (state / 2147483648.0) - 1.0


def exp_decay(age: float, tau: float) -> float:
    if tau <= 1e-6:
        return 0.0
    return math.exp(-age / tau)


def render_kick(age: float) -> float:
    if age < 0.0 or age > 0.55:
        return 0.0
    pitch = 48.0 + 120.0 * exp_decay(age, 0.032)
    body = math.sin(2.0 * math.pi * pitch * age)
    click = math.sin(2.0 * math.pi * 1600.0 * age) * exp_decay(age, 0.0035)
    return body * exp_decay(age, 0.18) * 1.2 + click * 0.18


def render_snare(age: float, ghost: bool, noise: float) -> float:
    if age < 0.0:
        return 0.0
    max_age = 0.12 if ghost else 0.32
    if age > max_age:
        return 0.0
    amp = 0.32 if ghost else 1.0
    body_tau = 0.045 if ghost else 0.10
    snap_tau = 0.016 if ghost else 0.040
    body = math.sin(2.0 * math.pi * 196.0 * age) * exp_decay(age, body_tau)
    ring = math.sin(2.0 * math.pi * 333.0 * age) * exp_decay(age, body_tau * 0.8)
    snap = noise * exp_decay(age, snap_tau)
    return amp * (body * 0.5 + ring * 0.2 + snap * 0.75)


def render_hat(age: float, noise: float, open_hat: bool) -> float:
    if age < 0.0:
        return 0.0
    tau = 0.09 if open_hat else 0.024
    if age > tau * 6.0:
        return 0.0
    return noise * exp_decay(age, tau) * (0.24 if open_hat else 0.15)


def one_pole_hp(sample: float, state: float, coeff: float) -> tuple[float, float]:
    state += coeff * (sample - state)
    return sample - state, state


def synthesize_bar(sample_rate: int, bpm: float) -> list[float]:
    bar_seconds = 4.0 * 60.0 / bpm
    length = int(round(sample_rate * bar_seconds))
    bar = [0.0] * length
    step_samples = sample_rate * 60.0 / bpm / 4.0
    rng = 0x51CED00D

    kicks = {0, 8}
    snares = {4, 12}
    ghosts = {11}
    hats = {0, 2, 4, 6, 8, 10, 12, 14}
    open_hats = {14}

    hp_state = 0.0
    hp_coeff = 1.0 - math.exp(-2.0 * math.pi * 1800.0 / sample_rate)

    for sample_index in range(length):
        rng, white = lcg(rng)
        hat_noise, hp_state = one_pole_hp(white, hp_state, hp_coeff)
        mix = 0.0
        for step_index in range(SIXTEENTHS):
            age = (sample_index - step_index * step_samples) / float(sample_rate)
            if step_index in kicks:
                mix += render_kick(age)
            if step_index in snares:
                mix += render_snare(age, False, hat_noise)
            if step_index in ghosts:
                mix += render_snare(age, True, hat_noise)
            if step_index in hats:
                mix += render_hat(age, hat_noise, step_index in open_hats)
        bar[sample_index] = mix

    delay = int(sample_rate * 0.034)
    wet = 0.16
    for sample_index in range(delay, length):
        bar[sample_index] += bar[sample_index - delay] * wet

    peak = max(abs(sample) for sample in bar) or 1.0
    scale = 0.89 / peak
    return [max(-1.0, min(1.0, math.tanh(sample * scale * 1.12))) for sample in bar]


def write_wav(path: pathlib.Path, samples: list[float], sample_rate: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(sample_rate)
        frames = b"".join(
            struct.pack("<h", max(-32767, min(32767, int(round(sample * 32767.0)))))
            for sample in samples
        )
        handle.writeframes(frames)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        default=pathlib.Path("plugins/wavslice/assets/default-loop.wav"),
    )
    parser.add_argument("--rate", type=int, default=SAMPLE_RATE)
    parser.add_argument("--bpm", type=float, default=BPM)
    args = parser.parse_args()

    samples = synthesize_bar(args.rate, args.bpm)
    write_wav(args.out, samples, args.rate)
    duration = len(samples) / float(args.rate)
    peak = max(abs(sample) for sample in samples)
    print(
        f"wrote {args.out} rate={args.rate} bpm={args.bpm:.1f} "
        f"seconds={duration:.3f} peak={peak:.3f} (CC0 original backbeat)"
    )


if __name__ == "__main__":
    main()
