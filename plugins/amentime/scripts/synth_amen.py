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


def render_kick(age: float) -> float:
    if age < 0.0 or age > 0.45:
        return 0.0
    pitch = 52.0 + 155.0 * exp_decay(age, 0.028)
    body = math.sin(2.0 * math.pi * pitch * age)
    click = math.sin(2.0 * math.pi * 1800.0 * age) * exp_decay(age, 0.004)
    return body * exp_decay(age, 0.16) * 1.15 + click * 0.22


def render_snare(age: float, ghost: bool, noise: float) -> float:
    if age < 0.0:
        return 0.0
    max_age = 0.14 if ghost else 0.38
    if age > max_age:
        return 0.0
    amp = 0.28 if ghost else 1.0
    body_tau = 0.055 if ghost else 0.12
    snap_tau = 0.018 if ghost else 0.045
    body = math.sin(2.0 * math.pi * 188.0 * age) * exp_decay(age, body_tau)
    ring = math.sin(2.0 * math.pi * 327.0 * age) * exp_decay(age, body_tau * 0.85)
    snap = noise * exp_decay(age, snap_tau)
    return amp * (body * 0.55 + ring * 0.22 + snap * 0.7)


def render_hat(age: float, noise: float, open_hat: bool) -> float:
    if age < 0.0:
        return 0.0
    tau = 0.085 if open_hat else 0.028
    if age > tau * 6.0:
        return 0.0
    return noise * exp_decay(age, tau) * (0.22 if open_hat else 0.16)


def render_splash(age: float, noise: float) -> float:
    if age < 0.0 or age > 0.55:
        return 0.0
    return noise * exp_decay(age, 0.16) * 0.18


def one_pole_hp(sample: float, state: float, coeff: float) -> tuple[float, float]:
    state += coeff * (sample - state)
    return sample - state, state


def synthesize_bar() -> list[float]:
    length = SAMPLES_PER_16TH_HOST * SIXTEENTHS
    bar = [0.0] * length
    rng = 0xC0FFEE01

    kicks = {0, 6, 12}
    snares = {1, 4, 9, 12, 15}
    ghosts = {3, 7, 11}
    hats = {0, 2, 4, 6, 8, 10, 12, 14}
    open_hats = {14}
    splashes = {0}

    hp_state = 0.0
    hp_coeff = 1.0 - math.exp(-2.0 * math.pi * 1800.0 / HOST_RATE)
    splash_hp_state = 0.0
    splash_hp_coeff = 1.0 - math.exp(-2.0 * math.pi * 4000.0 / HOST_RATE)

    for sample_index in range(length):
        rng, white = lcg(rng)
        hat_noise, hp_state = one_pole_hp(white, hp_state, hp_coeff)
        splash_noise, splash_hp_state = one_pole_hp(white, splash_hp_state, splash_hp_coeff)

        mix = 0.0
        for step_index in range(SIXTEENTHS):
            age = (sample_index - step_index * SAMPLES_PER_16TH_HOST) / float(HOST_RATE)
            if step_index in kicks:
                mix += render_kick(age)
            if step_index in snares:
                mix += render_snare(age, False, hat_noise)
            if step_index in ghosts:
                mix += render_snare(age, True, hat_noise)
            if step_index in hats:
                mix += render_hat(age, hat_noise, step_index in open_hats)
            if step_index in splashes:
                mix += render_splash(age, splash_noise)
        bar[sample_index] = mix

    # Short comb so the kit sits in a room instead of dry hits.
    delay = int(HOST_RATE * 0.037)
    wet = 0.18
    for sample_index in range(delay, length):
        bar[sample_index] += bar[sample_index - delay] * wet

    peak = max(abs(sample) for sample in bar) or 1.0
    scale = 0.89 / peak
    return [max(-1.0, min(1.0, math.tanh(sample * scale * 1.15))) for sample in bar]


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
