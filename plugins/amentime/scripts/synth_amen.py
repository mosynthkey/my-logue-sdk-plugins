#!/usr/bin/env python3
"""Synthesize a copyright-free 1-bar amen-style break and embed it as 8-bit PCM.

The Winstons' Amen, Brother recording is not used. This is an original drum
machine reconstruction of the common 16-step chop map, rendered at 12 kHz so
the whole bar fits the NTS-3 genericfx flash budget.
"""

from __future__ import annotations

import argparse
import math
import pathlib
import struct
import wave

HOST_RATE = 48000
PCM_RATE = 12000
SOURCE_BPM = 136.0
DOWNSAMPLE = HOST_RATE // PCM_RATE
SIXTEENTHS = 16
# Host 16th length must be divisible by 8 so 12 kHz slices divide cleanly by 32.
SAMPLES_PER_16TH_HOST = int(round(HOST_RATE * 60.0 / SOURCE_BPM / 4.0))
SAMPLES_PER_16TH_HOST += (8 - (SAMPLES_PER_16TH_HOST % 8)) % 8


def lcg(state: int) -> tuple[int, float]:
    state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
    return state, (state / 2147483648.0) - 1.0


def exp_decay(age: float, tau: float) -> float:
    if tau <= 1e-6:
        return 0.0
    return math.exp(-age / tau)


def render_kick(age: float, pickup: bool) -> float:
    max_age = 0.22 if pickup else 0.38
    if age < 0.0 or age > max_age:
        return 0.0
    amp = 0.42 if pickup else 1.0
    pitch = (48.0 if pickup else 42.0) + (210.0 if pickup else 255.0) * exp_decay(age, 0.018)
    body = math.sin(2.0 * math.pi * pitch * age)
    beater = math.sin(2.0 * math.pi * 2400.0 * age) * exp_decay(age, 0.0028)
    air = math.sin(2.0 * math.pi * 90.0 * age) * exp_decay(age, 0.09)
    return amp * (body * exp_decay(age, 0.11) * 1.05 + beater * 0.28 + air * 0.22)


def render_snare(age: float, ghost: bool, crack: float, body_noise: float) -> float:
    max_age = 0.11 if ghost else 0.32
    if age < 0.0 or age > max_age:
        return 0.0
    amp = 0.32 if ghost else 1.0
    body_hz = (205.0 if ghost else 188.0) + 55.0 * exp_decay(age, 0.012)
    ring_hz = 335.0 + 40.0 * exp_decay(age, 0.01)
    body = math.sin(2.0 * math.pi * body_hz * age) * exp_decay(age, 0.038 if ghost else 0.085)
    ring = math.sin(2.0 * math.pi * ring_hz * age) * exp_decay(age, 0.028 if ghost else 0.07)
    snap = crack * exp_decay(age, 0.012 if ghost else 0.028)
    rattle = body_noise * exp_decay(age, 0.02 if ghost else 0.055)
    stick = math.sin(2.0 * math.pi * 4200.0 * age) * exp_decay(age, 0.0018)
    return amp * (body * 0.42 + ring * 0.2 + snap * 0.85 + rattle * 0.45 + stick * 0.18)


def render_hat(age: float, noise: float, open_hat: bool) -> float:
    tau = 0.09 if open_hat else 0.018
    if age < 0.0 or age > tau * 7.0:
        return 0.0
    metal = math.sin(2.0 * math.pi * 10500.0 * age) * exp_decay(age, 0.004)
    return (noise * 0.92 + metal * 0.18) * exp_decay(age, tau) * (0.26 if open_hat else 0.14)


def render_crash(age: float, noise: float) -> float:
    if age < 0.0 or age > 0.72:
        return 0.0
    bell = math.sin(2.0 * math.pi * 880.0 * age) * exp_decay(age, 0.08)
    return noise * exp_decay(age, 0.22) * 0.28 + bell * 0.05


def one_pole_lp(sample: float, state: float, coeff: float) -> tuple[float, float]:
    state += coeff * (sample - state)
    return state, state


def one_pole_hp(sample: float, state: float, coeff: float) -> tuple[float, float]:
    low, state = one_pole_lp(sample, state, coeff)
    return sample - low, state


def apply_room(bar: list[float]) -> list[float]:
    delays = (
        int(HOST_RATE * 0.019),
        int(HOST_RATE * 0.037),
        int(HOST_RATE * 0.053),
    )
    decays = (0.22, 0.16, 0.11)
    wet = list(bar)
    lp_coeff = 1.0 - math.exp(-2.0 * math.pi * 4200.0 / HOST_RATE)
    lp_state = 0.0
    for sample_index, sample in enumerate(bar):
        room = 0.0
        for delay, decay in zip(delays, decays):
            if sample_index >= delay:
                room += wet[sample_index - delay] * decay
        damped, lp_state = one_pole_lp(room, lp_state, lp_coeff)
        wet[sample_index] = sample + damped
    return wet


def synthesize_bar() -> list[float]:
    length = SAMPLES_PER_16TH_HOST * SIXTEENTHS
    bar = [0.0] * length
    rng = 0xC0FFEE01

    # Canonical 16-step amen chop map. Hits stay on the grid so equal slices land.
    kicks = {0, 6, 12}
    pickup_kicks = {10}
    snares = {1, 4, 9, 12, 15}
    ghosts = {3, 7, 11}
    closed_hats = {0, 2, 4, 6, 8, 10, 12}
    open_hats = {14}
    crashes = {0}

    hp_mid = 0.0
    hp_air = 0.0
    hp_cym = 0.0
    hp_mid_coeff = 1.0 - math.exp(-2.0 * math.pi * 1800.0 / HOST_RATE)
    hp_air_coeff = 1.0 - math.exp(-2.0 * math.pi * 3400.0 / HOST_RATE)
    hp_cym_coeff = 1.0 - math.exp(-2.0 * math.pi * 5200.0 / HOST_RATE)

    for sample_index in range(length):
        rng, white = lcg(rng)
        crack, hp_air = one_pole_hp(white, hp_air, hp_air_coeff)
        body_noise, hp_mid = one_pole_hp(white, hp_mid, hp_mid_coeff)
        cym_noise, hp_cym = one_pole_hp(white, hp_cym, hp_cym_coeff)

        mix = 0.0
        for step_index in range(SIXTEENTHS):
            age = (sample_index - step_index * SAMPLES_PER_16TH_HOST) / float(HOST_RATE)
            if step_index in kicks:
                mix += render_kick(age, False)
            if step_index in pickup_kicks:
                mix += render_kick(age, True)
            if step_index in snares:
                mix += render_snare(age, False, crack, body_noise)
            if step_index in ghosts:
                mix += render_snare(age, True, crack, body_noise)
            if step_index in closed_hats:
                mix += render_hat(age, cym_noise, False)
            if step_index in open_hats:
                mix += render_hat(age, cym_noise, True)
            if step_index in crashes:
                mix += render_crash(age, cym_noise)
        bar[sample_index] = mix

    bar = apply_room(bar)
    peak = max(abs(sample) for sample in bar) or 1.0
    scale = 0.9 / peak
    return [max(-1.0, min(1.0, math.tanh(sample * scale * 1.12))) for sample in bar]


def downsample(samples: list[float]) -> list[float]:
    # 4-sample box plus one tap of previous group: cheap anti-alias before 12 kHz.
    out: list[float] = []
    previous = 0.0
    for group_index in range(0, len(samples), DOWNSAMPLE):
        group = samples[group_index : group_index + DOWNSAMPLE]
        if len(group) < DOWNSAMPLE:
            break
        mean = sum(group) / float(DOWNSAMPLE)
        out.append(mean * 0.75 + previous * 0.25)
        previous = mean
    return out


def to_pcm8(samples: list[float]) -> list[int]:
    peak = max(abs(sample) for sample in samples) or 1.0
    scale = 0.97 / peak
    codes: list[int] = []
    for sample in samples:
        quantized = int(round(sample * scale * 127.0))
        codes.append(max(-127, min(127, quantized)))
    return codes


def write_header(path: pathlib.Path, codes: list[int]) -> None:
    lines = [
        "#pragma once",
        "",
        "// Original synthesized 1-bar amen-style break. Not the Winstons recording.",
        f"// {PCM_RATE} Hz, 8-bit signed, {SOURCE_BPM:.0f} BPM, {SIXTEENTHS} equal 16th slices.",
        "",
        "#include <stdint.h>",
        "",
        f"static const uint32_t kAmenSampleRate = {PCM_RATE}u;",
        f"static const float kAmenSourceBpm = {SOURCE_BPM:.1f}f;",
        f"static const uint32_t kAmenSixteenths = {SIXTEENTHS}u;",
        f"static const uint32_t kAmenPcmLength = {len(codes)}u;",
        "",
        "static const int8_t kAmenPcm8[] = {",
    ]
    row: list[str] = []
    for sample_index, code in enumerate(codes):
        row.append(str(code))
        if len(row) == 16 or sample_index == len(codes) - 1:
            lines.append("  " + ", ".join(row) + ",")
            row = []
    lines.append("};")
    lines.append("")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


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
        default=pathlib.Path("plugins/amentime/dsp/amentime_pcm.h"),
    )
    parser.add_argument("--wav", type=pathlib.Path, default=None)
    args = parser.parse_args()

    bar = synthesize_bar()
    pcm = downsample(bar)
    if len(pcm) % 32 != 0:
        raise SystemExit(f"PCM length {len(pcm)} is not divisible by 32")
    codes = to_pcm8(pcm)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_header(args.out, codes)
    if args.wav is not None:
        write_wav(args.wav, pcm, PCM_RATE)

    peak = max(abs(code) for code in codes)
    rms = math.sqrt(sum(code * code for code in codes) / float(len(codes)))
    print(
        f"wrote {args.out} length={len(codes)} peak={peak} rms={rms:.1f} "
        f"slice={len(codes) // SIXTEENTHS}"
    )


if __name__ == "__main__":
    main()
