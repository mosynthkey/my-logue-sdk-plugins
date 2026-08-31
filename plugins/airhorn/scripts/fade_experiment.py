#!/usr/bin/env python3
"""Simulate AirHorn voice decay curves and render comparison WAVs."""

from __future__ import annotations

import math
import re
import struct
import wave
from dataclasses import dataclass
from pathlib import Path

HOST_RATE = 48000
EMBED_RATE = 24000
BASE_RATE = EMBED_RATE / HOST_RATE
LOOP_CROSSFADE = 96.0
PITCH_SETTLED = 0.025
OUTPUT_DIR = Path(__file__).resolve().parent / "fade_experiment_out"


@dataclass(frozen=True)
class Horn:
    offset: int
    length: int
    start_ratio: float
    env_coeff: float
    looping: bool


def parse_pcm_header(path: Path) -> tuple[list[int], list[Horn]]:
    text = path.read_text()
    pcm_match = re.search(r"static const int16_t kAirhornPcm16\[\] = \{(.*?)\};", text, re.S)
    if pcm_match is None:
        raise RuntimeError("PCM blob not found")

    pcm = [int(value.strip()) for value in pcm_match.group(1).split(",") if value.strip()]
    horns: list[Horn] = []
    for row in re.finditer(
        r"\{\s*(\d+)u,\s*(\d+)u,\s*([\d.]+)f,\s*([\d.eE+-]+)f,\s*(\d+)u\s*\}",
        text,
    ):
        horns.append(
            Horn(
                offset=int(row.group(1)),
                length=int(row.group(2)),
                start_ratio=float(row.group(3)),
                env_coeff=float(row.group(4)),
                looping=bool(int(row.group(5))),
            )
        )
    return pcm, horns


def pcm_to_float(sample: int) -> float:
    return sample / 32768.0


def wrap_index(index: int, length: int) -> int:
    wrapped = index % length
    if wrapped < 0:
        wrapped += length
    return wrapped


def clamp_index(index: int, length: int) -> int:
    if index < 0:
        return 0
    if index >= length:
        return length - 1
    return index


def linear_sample(pcm: list[int], horn: Horn, position: float) -> float:
    index = int(position)
    frac = position - index
    if horn.looping:
        sample_index0 = wrap_index(index, horn.length)
        sample_index1 = wrap_index(index + 1, horn.length)
    else:
        sample_index0 = clamp_index(index, horn.length)
        sample_index1 = clamp_index(index + 1, horn.length)
    y0 = pcm_to_float(pcm[horn.offset + sample_index0])
    y1 = pcm_to_float(pcm[horn.offset + sample_index1])
    return y0 + frac * (y1 - y0)


def hermite(y0: float, y1: float, y2: float, y3: float, frac: float) -> float:
    c1 = 0.5 * (y2 - y0)
    c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3
    c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2)
    return ((c3 * frac + c2) * frac + c1) * frac + y1


def sample_at(pcm: list[int], horn: Horn, position: float, sustain_phase: bool) -> float:
    if sustain_phase:
        return linear_sample(pcm, horn, position)

    index = int(position)
    frac = position - index
    if horn.looping:
        i0 = wrap_index(index - 1, horn.length)
        i1 = wrap_index(index, horn.length)
        i2 = wrap_index(index + 1, horn.length)
        i3 = wrap_index(index + 2, horn.length)
    else:
        i0 = clamp_index(index - 1, horn.length)
        i1 = clamp_index(index, horn.length)
        i2 = clamp_index(index + 1, horn.length)
        i3 = clamp_index(index + 2, horn.length)

    base = horn.offset
    return hermite(
        pcm_to_float(pcm[base + i0]),
        pcm_to_float(pcm[base + i1]),
        pcm_to_float(pcm[base + i2]),
        pcm_to_float(pcm[base + i3]),
        frac,
    )


def loop_sample_with_crossfade(
    pcm: list[int], horn: Horn, position: float, sustain_phase: bool
) -> float:
    loop_length = float(horn.length)
    output = sample_at(pcm, horn, position, sustain_phase)
    if not horn.looping or LOOP_CROSSFADE <= 1.0:
        return output

    dist_to_end = loop_length - position
    if dist_to_end <= 0.0 or dist_to_end >= LOOP_CROSSFADE:
        return output

    blend = 1.0 - dist_to_end / LOOP_CROSSFADE
    wrapped = sample_at(pcm, horn, position - loop_length, sustain_phase)
    return output * (1.0 - blend) + wrapped * blend


def pitch_settled(horn: Horn, pitch_ratio: float) -> bool:
    return horn.looping and (1.0 - PITCH_SETTLED) < pitch_ratio < (1.0 + PITCH_SETTLED)


def exp_coeff(tau_seconds: float) -> float:
    return math.exp(-1.0 / (tau_seconds * HOST_RATE))


def seam_jump_rms(pcm: list[int], horn: Horn) -> float:
    if not horn.looping:
        return 0.0
    start = linear_sample(pcm, horn, 0.0)
    end = linear_sample(pcm, horn, float(horn.length - 1))
    return abs(start - end)


@dataclass(frozen=True)
class DecayProfile:
    name: str
    hold_after_settle: float
    tau_while_fading: float
    lpf_coeff: float
    crossfade_scale: float
    min_hold_seconds: float


PROFILES = [
    DecayProfile("A_flat_sustain", 999.0, 0.0, 0.42, 1.0, 0.4),
    DecayProfile("B_slow_3s", 0.25, 3.0, 0.42, 1.0, 0.0),
    DecayProfile("C_medium_2s", 0.25, 2.0, 0.42, 1.25, 0.0),
    DecayProfile("D_fast_1_2s", 0.20, 1.2, 0.50, 1.5, 0.0),
    DecayProfile("E_two_stage", 0.35, 2.5, 0.45, 1.25, 0.0),
    DecayProfile("F_natural_1_8s", 0.25, 1.8, 0.48, 1.35, 0.0),
]


def render_voice(
    pcm: list[int],
    horn: Horn,
    duration_seconds: float,
    profile: DecayProfile,
) -> list[float]:
    attack_inc = 1.0 / (HOST_RATE * 0.004)
    min_amp = 0.0005
    min_hold = int(profile.min_hold_seconds * HOST_RATE)
    release_coeff = 0.999792
    hold_after_settle = int(profile.hold_after_settle * HOST_RATE)

    total_frames = int(duration_seconds * HOST_RATE)
    output: list[float] = []
    pos = 0.0
    amp = 0.0
    pitch_ratio = horn.start_ratio
    sustain_lpf = 0.0
    age = 0
    settled_age = 0
    releasing = False
    fading = False
    gated = True
    crossfade = LOOP_CROSSFADE * profile.crossfade_scale

    for _ in range(total_frames):
        age += 1
        settled = pitch_settled(horn, pitch_ratio)
        if settled:
            settled_age += 1
        else:
            settled_age = 0

        if settled and settled_age >= hold_after_settle:
            fading = True

        if fading:
            amp *= exp_coeff(profile.tau_while_fading) if profile.tau_while_fading > 0.0 else 1.0
        elif not releasing:
            amp += attack_inc
            if amp > 1.0:
                amp = 1.0

        if not gated and not releasing and horn.looping and age >= min_hold:
            releasing = True

        if releasing:
            amp *= release_coeff

        if amp < min_amp:
            output.extend([0.0] * (total_frames - len(output)))
            break

        sustain_phase = settled
        sample = loop_sample_with_crossfade(pcm, horn, pos, sustain_phase)

        if horn.looping and crossfade > 1.0:
            loop_length = float(horn.length)
            dist_to_end = loop_length - pos
            if 0.0 < dist_to_end < crossfade:
                blend = 1.0 - dist_to_end / crossfade
                wrapped = sample_at(pcm, horn, pos - loop_length, sustain_phase)
                sample = sample * (1.0 - blend) + wrapped * blend

        frame = sample * amp
        if settled or profile.lpf_coeff > 0.0:
            sustain_lpf += profile.lpf_coeff * (frame - sustain_lpf)
            frame = sustain_lpf
        else:
            sustain_lpf = frame

        output.append(max(-1.0, min(1.0, frame)))

        pos += BASE_RATE * pitch_ratio
        if horn.looping:
            loop_length = float(horn.length)
            while pos >= loop_length:
                pos -= loop_length
        elif pos >= float(horn.length - 2):
            pos = float(horn.length - 2)
            releasing = True

        if horn.env_coeff > 0.0:
            pitch_ratio = 1.0 + (pitch_ratio - 1.0) * horn.env_coeff

    while len(output) < total_frames:
        output.append(0.0)
    return output


def write_wav(path: Path, samples: list[float]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    pcm = b"".join(
        struct.pack("<h", max(-32768, min(32767, int(sample * 32767.0)))) for sample in samples
    )
    with wave.open(str(path), "wb") as handle:
        handle.setnchannels(1)
        handle.setsampwidth(2)
        handle.setframerate(HOST_RATE)
        handle.writeframes(pcm)


def rms(samples: list[float]) -> float:
    if not samples:
        return 0.0
    return math.sqrt(sum(sample * sample for sample in samples) / len(samples))


def envelope_stats(samples: list[float], window: int = 480) -> dict[str, float]:
    peaks: list[float] = []
    for start in range(0, len(samples) - window, window):
        chunk = samples[start : start + window]
        peaks.append(max(abs(sample) for sample in chunk))
    if not peaks:
        return {"peak": 0.0, "tail_db": -120.0}

    peak = max(peaks)
    tail = peaks[-1] if peaks else 0.0
    tail_db = 20.0 * math.log10(max(tail, 1e-9) / max(peak, 1e-9))
    return {"peak": peak, "tail_db": tail_db, "duration_active_s": len(peaks) * window / HOST_RATE}


def loop_wrap_click(samples: list[float], horn: Horn, embed_rate: int = EMBED_RATE) -> float:
    if not horn.looping:
        return 0.0
    loop_host = int(horn.length / BASE_RATE)
    if loop_host <= 0 or loop_host >= len(samples):
        return 0.0
    clicks: list[float] = []
    wrap_index = loop_host
    while wrap_index < len(samples):
        delta = samples[wrap_index] - samples[wrap_index - 1]
        clicks.append(abs(delta))
        wrap_index += loop_host
    return max(clicks) if clicks else 0.0


def main() -> int:
    repo_root = Path(__file__).resolve().parents[3]
    pcm_path = repo_root / "plugins/airhorn/dsp/airhorn_pcm.h"
    pcm, horns = parse_pcm_header(pcm_path)

    print("Loop seam jump (linear, no crossfade):")
    for horn_index, horn in enumerate(horns):
        print(f"  horn {horn_index}: {seam_jump_rms(pcm, horn):.5f}")

    duration = 4.0
    summary: list[tuple[str, int, dict[str, float]]] = []

    for profile in PROFILES:
        for horn_index, horn in enumerate(horns):
            if not horn.looping:
                continue
            rendered = render_voice(pcm, horn, duration, profile)
            stats = envelope_stats(rendered)
            click = loop_wrap_click(rendered, horn)
            out_path = OUTPUT_DIR / f"{profile.name}_horn{horn_index}.wav"
            write_wav(out_path, rendered)
            summary.append((profile.name, horn_index, stats, click))
            print(
                f"{profile.name} horn{horn_index}: peak={stats['peak']:.3f} "
                f"tail={stats['tail_db']:.1f} dB active={stats['duration_active_s']:.2f}s "
                f"wrap_click={click:.5f} -> {out_path.name}"
            )

    print("\nRecommendation heuristic (loop horns, prefer smooth tail ~ -36 to -48 dB at 4s):")
    scored: list[tuple[float, str, int]] = []
    for name, horn_index, stats, click in summary:
        target_tail = -42.0
        score = abs(stats["tail_db"] - target_tail) + click * 120.0
        scored.append((score, name, horn_index))
    scored.sort()
    for score, name, horn_index in scored[:5]:
        print(f"  score={score:.1f} {name} horn{horn_index}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
