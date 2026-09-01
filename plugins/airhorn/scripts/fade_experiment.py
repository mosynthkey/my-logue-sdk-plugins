#!/usr/bin/env python3
"""Simulate AirHorn voice decay curves, render comparison WAVs, and score the loop wrap.

The voice model here mirrors dsp/airhorn_engine.h. Keep the two in step: the
engine reads the loop with a plain wrapping interpolation because the wrap
crossfade is baked into the embedded PCM by embed_pcm.py.

Pass --compare-loop-modes to score a ping-pong dual-player crossfade against the
shipping single-wrap player. On the baked loop the dual player increases level
pumping without improving the wrap seam.
"""

from __future__ import annotations

import argparse
import cmath
import math
import re
import struct
import wave
from dataclasses import dataclass
from enum import Enum
from pathlib import Path

HOST_RATE = 48000
EMBED_RATE = 24000
BASE_RATE = EMBED_RATE / HOST_RATE
PITCH_SETTLED = 0.025
# Matches embed_pcm.py default (12 fundamental periods at ~79.5 samples).
LOOP_CROSSFADE_SAMPLES = 955
OUTPUT_DIR = Path(__file__).resolve().parent / "fade_experiment_out"


class LoopMode(str, Enum):
    SINGLE_WRAP = "single_wrap"
    DUAL_CROSSFADE = "dual_crossfade"


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


def hermite(y0: float, y1: float, y2: float, y3: float, frac: float) -> float:
    c1 = 0.5 * (y2 - y0)
    c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3
    c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2)
    return ((c3 * frac + c2) * frac + c1) * frac + y1


def sample_at(pcm: list[int], horn: Horn, position: float) -> float:
    index = int(position)
    frac = position - index
    if horn.looping:
        taps = [wrap_index(index + offset, horn.length) for offset in (-1, 0, 1, 2)]
    else:
        taps = [clamp_index(index + offset, horn.length) for offset in (-1, 0, 1, 2)]
    base = horn.offset
    return hermite(*(pcm_to_float(pcm[base + tap]) for tap in taps), frac)


def pitch_settled(horn: Horn, pitch_ratio: float) -> bool:
    return horn.looping and (1.0 - PITCH_SETTLED) < pitch_ratio < (1.0 + PITCH_SETTLED)


def exp_coeff(tau_seconds: float) -> float:
    return math.exp(-1.0 / (tau_seconds * HOST_RATE))


@dataclass(frozen=True)
class DecayProfile:
    name: str
    hold_after_settle: float
    tau_while_fading: float
    min_hold_seconds: float


PROFILES = [
    DecayProfile("A_flat_sustain", 999.0, 0.0, 0.4),
    DecayProfile("B_slow_3s", 0.25, 3.0, 0.0),
    DecayProfile("C_medium_2s", 0.25, 2.0, 0.0),
    DecayProfile("D_fast_1_2s", 0.20, 1.2, 0.0),
    DecayProfile("E_two_stage", 0.35, 2.5, 0.0),
    DecayProfile("F_natural_1_8s", 0.25, 1.8, 0.0),
]


def loop_crossfade_samples(horn: Horn) -> int:
    return min(LOOP_CROSSFADE_SAMPLES, horn.length // 4)


def equal_power_fade(t: float) -> tuple[float, float]:
    fade_in = 0.5 - 0.5 * math.cos(math.pi * t)
    return fade_in, 1.0 - fade_in


def render_voice(
    pcm: list[int],
    horn: Horn,
    duration_seconds: float,
    profile: DecayProfile,
    loop_mode: LoopMode = LoopMode.SINGLE_WRAP,
) -> tuple[list[float], list[int]]:
    attack_inc = 1.0 / (HOST_RATE * 0.004)
    min_amp = 0.0005
    min_hold = int(profile.min_hold_seconds * HOST_RATE)
    release_coeff = 0.999792
    hold_after_settle = int(profile.hold_after_settle * HOST_RATE)

    total_frames = int(duration_seconds * HOST_RATE)
    output: list[float] = []
    wraps: list[int] = []
    pos = 0.0
    pos_alt = 0.0
    active_player = 0
    crossfade_remain = 0
    crossfade_total = loop_crossfade_samples(horn)
    amp = 0.0
    pitch_ratio = horn.start_ratio
    age = 0
    settled_age = 0
    releasing = False
    fading = False
    gated = True

    for frame_index in range(total_frames):
        age += 1
        settled = pitch_settled(horn, pitch_ratio)
        settled_age = settled_age + 1 if settled else 0

        if settled and settled_age >= hold_after_settle:
            fading = True

        if fading:
            amp *= exp_coeff(profile.tau_while_fading) if profile.tau_while_fading > 0.0 else 1.0
        elif not releasing:
            amp = min(1.0, amp + attack_inc)

        if not gated and not releasing and horn.looping and age >= min_hold:
            releasing = True
        if releasing:
            amp *= release_coeff

        if amp < min_amp:
            output.extend([0.0] * (total_frames - len(output)))
            break

        playback_rate = BASE_RATE * pitch_ratio
        use_dual = (
            horn.looping
            and loop_mode == LoopMode.DUAL_CROSSFADE
            and settled
            and crossfade_total > 0
        )

        if use_dual:
            primary_pos = pos_alt if active_player else pos
            frame = sample_at(pcm, horn, primary_pos) * amp
            if crossfade_remain > 0:
                secondary_pos = pos if active_player else pos_alt
                secondary = sample_at(pcm, horn, secondary_pos) * amp
                fade_phase = 1.0 - crossfade_remain / crossfade_total
                fade_in, fade_out = equal_power_fade(fade_phase)
                frame = frame * fade_out + secondary * fade_in
                crossfade_remain -= 1
                pos += playback_rate
                pos_alt += playback_rate
                if crossfade_remain == 0:
                    active_player = 1 - active_player
                    if active_player:
                        pos = pos_alt
                    else:
                        pos_alt = pos
            else:
                pos += playback_rate if active_player == 0 else 0.0
                pos_alt += playback_rate if active_player == 1 else 0.0
                primary_pos = pos_alt if active_player else pos
                if primary_pos >= float(horn.length) - playback_rate * crossfade_total:
                    if active_player == 0:
                        pos_alt = 0.0
                    else:
                        pos = 0.0
                    crossfade_remain = crossfade_total
                    wraps.append(frame_index)
            loop_length = float(horn.length)
            while pos >= loop_length:
                pos -= loop_length
            while pos_alt >= loop_length:
                pos_alt -= loop_length
        else:
            frame = sample_at(pcm, horn, pos) * amp
            pos += playback_rate
            if horn.looping:
                while pos >= float(horn.length):
                    pos -= float(horn.length)
                    wraps.append(frame_index)
                pos_alt = pos
                active_player = 0
                crossfade_remain = 0
            elif pos >= float(horn.length - 2):
                pos = float(horn.length - 2)
                releasing = True

        output.append(max(-1.0, min(1.0, frame)))

        if horn.env_coeff > 0.0:
            pitch_ratio = 1.0 + (pitch_ratio - 1.0) * horn.env_coeff

    while len(output) < total_frames:
        output.append(0.0)
    return output, wraps


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


def level_excursion_db(samples: list[float], window: int = 480, hop: int = 240) -> float:
    """Peak-to-floor short-window RMS swing during sustain; lower is steadier."""
    peaks: list[float] = []
    for start in range(0, len(samples) - window, hop):
        chunk = samples[start : start + window]
        peaks.append(math.sqrt(sum(sample * sample for sample in chunk) / window))
    if not peaks:
        return 0.0
    peak = max(peaks)
    floor = min(peaks)
    if floor <= 0.0:
        return 0.0
    return 20.0 * math.log10(peak / floor)


def find_settled_start(horn: Horn, duration_seconds: float) -> int:
    pitch_ratio = horn.start_ratio
    for frame_index in range(int(duration_seconds * HOST_RATE)):
        if pitch_settled(horn, pitch_ratio):
            return frame_index
        if horn.env_coeff > 0.0:
            pitch_ratio = 1.0 + (pitch_ratio - 1.0) * horn.env_coeff
    return 0


def compare_loop_modes(pcm: list[int], horns: list[Horn], duration_seconds: float = 4.0) -> int:
    profile = DecayProfile("flat_sustain", 999.0, 0.0, 0.0)
    print("Loop playback strategy comparison on the baked PCM (lower is better):")
    print("  level_exc = short-window RMS swell during flat sustain")
    print("  wrap_click = spectral-flux seam score (negative means below the tone)")
    print()

    for horn_index, horn in enumerate(horns):
        if not horn.looping:
            continue
        settled_start = find_settled_start(horn, duration_seconds)
        for loop_mode in LoopMode:
            rendered, wraps = render_voice(pcm, horn, duration_seconds, profile, loop_mode)
            sustain = rendered[settled_start:]
            level_exc = level_excursion_db(sustain)
            click = wrap_click_db(rendered, wraps)
            print(
                f"  horn{horn_index} {loop_mode.value:16s} "
                f"level_exc={level_exc:.2f} dB wrap_click={click:+.2f} dB"
            )
        print()

    print(
        "Recommendation: keep single_wrap. dual_crossfade re-blends the loop tail into a "
        "head that already contains that tail and increases level pumping."
    )
    return 0


def envelope_stats(samples: list[float], window: int = 480) -> dict[str, float]:
    peaks: list[float] = []
    for start in range(0, len(samples) - window, window):
        chunk = samples[start : start + window]
        peaks.append(max(abs(sample) for sample in chunk))
    if not peaks:
        return {"peak": 0.0, "tail_db": -120.0, "duration_active_s": 0.0}

    peak = max(peaks)
    tail = peaks[-1]
    tail_db = 20.0 * math.log10(max(tail, 1e-9) / max(peak, 1e-9))
    return {"peak": peak, "tail_db": tail_db, "duration_active_s": len(peaks) * window / HOST_RATE}


def fft(values: list[complex]) -> list[complex]:
    """Iterative radix-2 FFT; `values` length must be a power of two."""
    size = len(values)
    data = list(values)
    bit = 0
    while (1 << bit) < size:
        bit += 1
    for index in range(size):
        mirrored = int(f"{index:0{bit}b}"[::-1], 2)
        if mirrored > index:
            data[index], data[mirrored] = data[mirrored], data[index]

    span = 2
    while span <= size:
        step = cmath.exp(-2j * math.pi / span)
        for block in range(0, size, span):
            twiddle = 1 + 0j
            for offset in range(span // 2):
                even = data[block + offset]
                odd = data[block + offset + span // 2] * twiddle
                data[block + offset] = even + odd
                data[block + offset + span // 2] = even - odd
                twiddle *= step
        span *= 2
    return data


def spectral_flux(samples: list[float], size: int = 512, hop: int = 128,
                  min_hz: float = 500.0) -> tuple[list[float], int]:
    """Positive spectral flux per frame: the standard onset/click detector.

    A loop seam makes many harmonics step at once, which a per-bin sum of positive
    magnitude changes picks up far better than a broadband envelope does.
    """
    window = [0.5 - 0.5 * math.cos(2.0 * math.pi * index / size) for index in range(size)]
    first_bin = max(1, int(min_hz * size / HOST_RATE))
    flux: list[float] = []
    previous: list[float] | None = None
    for start in range(0, len(samples) - size, hop):
        spectrum = fft([samples[start + index] * window[index] for index in range(size)])
        magnitude = [abs(spectrum[bin_index]) for bin_index in range(first_bin, size // 2 + 1)]
        if previous is None:
            flux.append(0.0)
        else:
            flux.append(sum(max(now - was, 0.0) for now, was in zip(magnitude, previous)))
        previous = magnitude
    return flux, hop


def wrap_click_db(samples: list[float], wraps: list[int]) -> float:
    """How far the loop wrap sticks out of the tone's own spectral movement.

    The peak spectral flux inside each wrap window is compared against the 99th
    percentile of the same measurement over windows that contain no wrap, so 0 dB
    means the wrap is indistinguishable from ordinary signal movement. The loop
    this replaced measured about +3 dB, which is what was heard as a click.
    """
    if not wraps:
        return 0.0
    flux, hop = spectral_flux(samples)
    if len(flux) < 8:
        return 0.0

    guard = max(1, int(0.003 * HOST_RATE) // hop)
    span = max(2, int(0.011 * HOST_RATE) // hop)
    blocked = [False] * len(flux)
    wrap_peaks: list[float] = []
    for wrap in wraps:
        frame = wrap // hop
        low, high = max(0, frame - guard), min(len(flux), frame + span)
        if high > low:
            wrap_peaks.append(max(flux[low:high]))
        for index in range(max(0, frame - 3 * guard), min(len(flux), frame + 3 * span)):
            blocked[index] = True

    clean_peaks = [
        max(flux[start : start + span])
        for start in range(1, len(flux) - span, span)
        if not any(blocked[start : start + span])
    ]
    if not wrap_peaks or not clean_peaks:
        return 0.0
    clean_peaks.sort()
    reference = clean_peaks[int(len(clean_peaks) * 0.99)]
    average_wrap = sum(wrap_peaks) / len(wrap_peaks)
    return 20.0 * math.log10(max(average_wrap, 1e-12) / max(reference, 1e-12))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--compare-loop-modes",
        action="store_true",
        help="score single-wrap vs dual-player sustain crossfade and exit",
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[3]
    pcm_path = repo_root / "plugins/airhorn/dsp/airhorn_pcm.h"
    pcm, horns = parse_pcm_header(pcm_path)

    duration = 4.0
    if args.compare_loop_modes:
        return compare_loop_modes(pcm, horns, duration)
    summary: list[tuple[str, int, dict[str, float], float]] = []

    for profile in PROFILES:
        for horn_index, horn in enumerate(horns):
            if not horn.looping:
                continue
            rendered, wraps = render_voice(pcm, horn, duration, profile)
            stats = envelope_stats(rendered)
            click = wrap_click_db(rendered, wraps)
            out_path = OUTPUT_DIR / f"{profile.name}_horn{horn_index}.wav"
            write_wav(out_path, rendered)
            summary.append((profile.name, horn_index, stats, click))
            print(
                f"{profile.name} horn{horn_index}: peak={stats['peak']:.3f} "
                f"tail={stats['tail_db']:.1f} dB active={stats['duration_active_s']:.2f}s "
                f"wrap_click={click:+.2f} dB over {len(wraps)} wraps -> {out_path.name}"
            )

    print("\nRecommendation heuristic (loop horns, prefer smooth tail ~ -36 to -48 dB at 4s):")
    scored: list[tuple[float, str, int]] = []
    for name, horn_index, stats, click in summary:
        target_tail = -42.0
        score = abs(stats["tail_db"] - target_tail) + max(click, 0.0) * 4.0
        scored.append((score, name, horn_index))
    scored.sort()
    for score, name, horn_index in scored[:5]:
        print(f"  score={score:.1f} {name} horn{horn_index}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
