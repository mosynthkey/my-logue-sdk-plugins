#!/usr/bin/env python3
"""Extract a seamless 16-bit DJ horn loop plus pitch-envelope metadata.

The sustained horn tone drifts slowly in pitch and brightness, so a loop cut
straight out of the recording restarts on material that no longer matches what
preceded it and that step is heard as a click once per loop. Two things keep the
wrap inaudible here:

* the loop length is picked so the material one loop later still lines up with
  the loop start, both in phase and in level;
* the last stretch of the loop is folded back into its head as a crossfade, so
  the wrap lands in the middle of a gradual blend instead of on a hard edge.

Because the crossfade is baked into the PCM, playback stays a plain wrapping
read - no per-sample crossfade work on the MCU.
"""

from __future__ import annotations

import argparse
import math
import pathlib
import struct
import subprocess
import sys
import tempfile
import wave


HOST_RATE = 48000


def load_mono_wav(path: pathlib.Path, target_rate: int) -> list[float]:
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as tmp:
        tmp_path = pathlib.Path(tmp.name)
    subprocess.run(
        [
            "ffmpeg",
            "-y",
            "-loglevel",
            "error",
            "-i",
            str(path),
            "-ar",
            str(target_rate),
            "-ac",
            "1",
            "-sample_fmt",
            "s16",
            str(tmp_path),
        ],
        check=True,
    )
    with wave.open(str(tmp_path), "rb") as handle:
        frames = handle.readframes(handle.getnframes())
    tmp_path.unlink(missing_ok=True)
    samples = struct.unpack(f"<{len(frames) // 2}h", frames)
    return [sample / 32768.0 for sample in samples]


def rms(samples: list[float]) -> float:
    if not samples:
        return 0.0
    return math.sqrt(sum(sample * sample for sample in samples) / len(samples))


def yin_period(samples: list[float], sample_rate: int, fmin: float, fmax: float) -> float | None:
    length = len(samples)
    tau_min = max(2, int(sample_rate / fmax))
    tau_max = min(length - 2, int(sample_rate / fmin))
    if tau_max <= tau_min + 2:
        return None

    diff = [0.0] * (tau_max + 1)
    for tau in range(tau_min, tau_max + 1):
        acc = 0.0
        for sample_index in range(length - tau):
            delta = samples[sample_index] - samples[sample_index + tau]
            acc += delta * delta
        diff[tau] = acc

    running = 0.0
    cmnd = [1.0] * (tau_max + 1)
    for tau in range(1, tau_max + 1):
        running += diff[tau]
        cmnd[tau] = diff[tau] * tau / running if running > 0.0 else 1.0

    best_tau = None
    for tau in range(tau_min, tau_max):
        if cmnd[tau] < 0.15 and cmnd[tau] <= cmnd[tau - 1] and cmnd[tau] <= cmnd[tau + 1]:
            best_tau = tau
            break
    if best_tau is None:
        best_tau = min(range(tau_min, tau_max + 1), key=lambda tau: cmnd[tau])
        if cmnd[best_tau] > 0.35:
            return None

    tau = best_tau
    if 1 <= tau < tau_max:
        s0, s1, s2 = cmnd[tau - 1], cmnd[tau], cmnd[tau + 1]
        denom = s0 - 2.0 * s1 + s2
        if denom != 0.0:
            tau = tau + 0.5 * (s0 - s2) / denom
    return tau


def pitch_track(samples: list[float], sample_rate: int) -> list[tuple[int, float, float]]:
    window = int(sample_rate * 0.04)
    hop = max(1, window // 4)
    track: list[tuple[int, float, float]] = []
    for start in range(0, len(samples) - window, hop):
        chunk = samples[start : start + window]
        energy = rms(chunk)
        if energy < 0.03:
            continue
        period = yin_period(chunk, sample_rate, 80.0, 1600.0)
        if period is None:
            continue
        track.append((start, sample_rate / period, energy))
    return track


def stable_region(track: list[tuple[int, float, float]], sample_count: int) -> tuple[int, int, float]:
    """Span where both pitch and level have settled, i.e. after the drop and before the release."""
    if not track:
        return 0, sample_count, 1.0

    frequencies = [freq for _, freq, _ in track]
    median = sorted(frequencies)[len(frequencies) // 2]
    energies = sorted(energy for _, _, energy in track)
    sustain_level = energies[len(energies) // 2]
    stable_starts = [
        start
        for start, freq, energy in track
        if abs(1200.0 * math.log2(freq / median)) < 25.0 and energy > 0.6 * sustain_level
    ]
    if not stable_starts:
        start = track[len(track) // 2][0]
        return start, sample_count, median

    region_start = stable_starts[0]
    region_end = min(sample_count, stable_starts[-1] + int(0.04 * HOST_RATE))
    if region_end - region_start < 1024:
        region_end = min(sample_count, region_start + 4096)
    return region_start, region_end, median


def refine_period(samples: list[float], start: int, sample_rate: int, window: int = 4096) -> float:
    """Autocorrelation period of the settled tone, in fractional samples.

    Plain autocorrelation happily locks onto an octave (or two) below the real
    pitch, which would make every loop-length candidate that much coarser, so the
    best lag is pulled back to the shortest submultiple that correlates nearly as
    well.
    """
    chunk = [value for value in samples[start : start + window]]
    if len(chunk) < 64:
        return 64.0
    mean = sum(chunk) / len(chunk)
    chunk = [value - mean for value in chunk]

    tau_min = max(2, int(sample_rate / 800.0))
    tau_max = min(len(chunk) - 2, int(sample_rate / 100.0))
    energy = sum(value * value for value in chunk)
    if energy <= 0.0:
        return 64.0

    correlation = [0.0] * (tau_max + 2)
    for tau in range(tau_min, tau_max + 2):
        acc = 0.0
        for sample_index in range(len(chunk) - tau):
            acc += chunk[sample_index] * chunk[sample_index + tau]
        correlation[tau] = acc / energy

    best_tau = max(range(tau_min, tau_max + 1), key=lambda tau: correlation[tau])
    for divisor in range(8, 1, -1):
        candidate = int(round(best_tau / divisor))
        if candidate < tau_min + 1 or candidate + 1 > tau_max:
            continue
        neighbourhood = range(candidate - 1, candidate + 2)
        local = max(neighbourhood, key=lambda tau: correlation[tau])
        if correlation[local] >= 0.92 * correlation[best_tau]:
            best_tau = local
            break

    s0, s1, s2 = correlation[best_tau - 1], correlation[best_tau], correlation[best_tau + 1]
    denom = s0 - 2.0 * s1 + s2
    period = float(best_tau)
    if denom != 0.0:
        period += 0.5 * (s0 - s2) / denom
    return period


def match_score(samples: list[float], start: int, length: int, window: int, stride: int = 1) -> float:
    """Similarity between the loop head and the material one loop later.

    Returns the correlation scaled by how well the two levels agree, so a candidate
    only wins when the crossfade will be both phase coherent and level neutral.
    """
    head_end = start + window
    tail_start = start + length
    if tail_start + window > len(samples):
        return -1.0

    dot = head_energy = tail_energy = 0.0
    for sample_index in range(0, window, stride):
        head = samples[start + sample_index]
        tail = samples[tail_start + sample_index]
        dot += head * tail
        head_energy += head * head
        tail_energy += tail * tail
    if head_energy <= 1e-12 or tail_energy <= 1e-12:
        return -1.0

    head_norm = math.sqrt(head_energy)
    tail_norm = math.sqrt(tail_energy)
    correlation = dot / (head_norm * tail_norm)
    level_match = min(head_norm, tail_norm) / max(head_norm, tail_norm)
    return correlation * level_match


def find_loop(
    samples: list[float],
    region: tuple[int, int],
    period: float,
    crossfade: int,
    min_cycles: int,
    max_cycles: int,
) -> tuple[int, int, float]:
    """Pick the loop start and length whose head and tail match best."""
    region_start, region_end = region
    period_samples = max(8, int(round(period)))
    start_step = period_samples * 4
    last_start = region_end - int(min_cycles * period) - crossfade
    starts = list(range(region_start, max(region_start + 1, last_start), start_step))

    coarse_window = min(crossfade, period_samples * 2)
    candidates: list[tuple[float, int, int]] = []
    for start in starts:
        for cycles in range(min_cycles, max_cycles + 1):
            center = int(round(cycles * period))
            for length in range(center - period_samples, center + period_samples + 1):
                if start + length + crossfade > len(samples):
                    continue
                score = match_score(samples, start, length, coarse_window, stride=2)
                candidates.append((score, start, length))

    if not candidates:
        return region_start, min(len(samples) - region_start, int(min_cycles * period)), 0.0

    candidates.sort(reverse=True)
    best = max(
        ((match_score(samples, start, length, crossfade), start, length) for _, start, length in candidates[:64]),
    )
    return best[1], best[2], best[0]


def bake_crossfade(samples: list[float], start: int, length: int, crossfade: int) -> list[float]:
    """Fold the loop tail back into the loop head with a raised-cosine crossfade.

    The gain correction keeps the blend at constant power: two partly correlated
    takes of the same tone would otherwise dip in the middle of the fade.
    """
    loop = list(samples[start : start + length])
    tail = list(samples[start + length : start + length + crossfade])
    if len(tail) < crossfade or crossfade < 8:
        return loop

    head_rms = rms(loop[:crossfade])
    tail_rms = rms(tail)
    if tail_rms > 1e-9:
        gain = head_rms / tail_rms
        tail = [value * gain for value in tail]

    dot = sum(loop[i] * tail[i] for i in range(crossfade))
    norm = math.sqrt(sum(v * v for v in loop[:crossfade]) * sum(v * v for v in tail))
    correlation = dot / norm if norm > 1e-12 else 0.0

    for fade_index in range(crossfade):
        phase = fade_index / float(crossfade)
        fade_in = 0.5 - 0.5 * math.cos(math.pi * phase)
        fade_out = 1.0 - fade_in
        power = fade_in * fade_in + fade_out * fade_out + 2.0 * fade_in * fade_out * correlation
        compensation = 1.0 / math.sqrt(power) if power > 1e-9 else 1.0
        loop[fade_index] = (loop[fade_index] * fade_in + tail[fade_index] * fade_out) * compensation
    return loop


def circular_rms(samples: list[float], window: int) -> list[float]:
    """Sliding RMS that wraps at the ends, so the loop is treated as a ring."""
    length = len(samples)
    if length == 0 or window < 1:
        return [0.0] * length
    window = min(window, length)
    # One full pass of squared samples so every window sum is O(1).
    doubled = samples + samples
    squares = [value * value for value in doubled]
    running = sum(squares[:window])
    half = window // 2
    out = [0.0] * length
    for sample_index in range(length):
        out[(sample_index + half) % length] = math.sqrt(running / window)
        running += squares[sample_index + window] - squares[sample_index]
    return out


def hann_kernel(radius: int) -> list[float]:
    size = radius * 2 + 1
    weights = [0.5 - 0.5 * math.cos(2.0 * math.pi * index / (size - 1)) for index in range(size)]
    total = sum(weights)
    return [weight / total for weight in weights]


def circular_smooth(values: list[float], radius: int) -> list[float]:
    if radius < 1:
        return list(values)
    length = len(values)
    kernel = hann_kernel(radius)
    out = [0.0] * length
    for sample_index in range(length):
        acc = 0.0
        for tap_index, weight in enumerate(kernel):
            acc += values[(sample_index + tap_index - radius) % length] * weight
        out[sample_index] = acc
    return out


def flatten_loop_level(loop: list[float], period: float,
                       window_periods: float = 2.0, smooth_periods: float = 4.0) -> tuple[list[float], float]:
    """Divide out the slow loudness swell so looping does not sound like an amp LFO.

    The source horn is not level-flat across a few hundred milliseconds. Once that
    contour is locked into a loop it repeats at the loop rate and reads as a
    ~2 Hz tremolo. A short circular RMS (a couple of fundamental periods) tracks
    the swell without touching the tone's own within-cycle ripple; dividing it
    out leaves a loop whose loudness no longer pumps.
    """
    if len(loop) < 32 or period < 2.0:
        return list(loop), 0.0

    window = max(8, int(round(period * window_periods)))
    radius = max(2, int(round(period * smooth_periods)))
    envelope = circular_smooth(circular_rms(loop, window), radius)
    before = level_excursion_db(envelope)

    target = sum(envelope) / len(envelope)
    flat = [
        sample * (target / env) if env > 1e-6 else sample
        for sample, env in zip(loop, envelope)
    ]
    after = level_excursion_db(circular_smooth(circular_rms(flat, window), radius))
    return flat, before - after


def level_excursion_db(envelope: list[float]) -> float:
    peak = max(envelope) if envelope else 0.0
    floor = min(envelope) if envelope else 0.0
    if peak <= 0.0 or floor <= 0.0:
        return 0.0
    return 20.0 * math.log10(peak / floor)


def seam_report(loop: list[float], period: float) -> dict:
    """Numbers that describe how much the wrap stands out from the loop interior."""
    length = len(loop)
    step = max(1, int(round(period)))

    def residual_at(index: int) -> float:
        acc = 0.0
        for offset in range(step):
            here = loop[(index + offset) % length]
            prev = loop[(index + offset - step) % length]
            acc += (here - prev) ** 2
        return math.sqrt(acc / step)

    seam = max(residual_at(index) for index in range(length - step, length + step))
    interior = sorted(residual_at(index) for index in range(step, length - 2 * step, step))
    reference = interior[int(len(interior) * 0.99)] if interior else 1e-9
    return {
        "sample_jump": abs(loop[0] - loop[-1]),
        "seam_excess_db": 20.0 * math.log10(max(seam, 1e-9) / max(reference, 1e-9)),
        "cycles": length / period,
    }


def to_pcm16(samples: list[float]) -> list[int]:
    peak = max((abs(sample) for sample in samples), default=1.0)
    scale = 0.97 / peak if peak > 0.0 else 1.0
    packed: list[int] = []
    for sample in samples:
        value = int(round(sample * scale * 32767.0))
        if value > 32767:
            value = 32767
        if value < -32767:
            value = -32767
        packed.append(value)
    return packed


def extract_dj_loop(
    path: pathlib.Path,
    sample_rate: int,
    max_samples: int,
    start_ratio: float,
    tau: float,
    crossfade_cycles: float,
    min_seconds: float,
) -> dict:
    samples = load_mono_wav(path, sample_rate)
    track = pitch_track(samples, sample_rate)
    region_start, region_end, settled_hz = stable_region(track, len(samples))
    print(
        f"  region {region_start}:{region_end} ({(region_end - region_start) / sample_rate * 1000:.0f} ms) "
        f"settled={settled_hz:.1f} Hz"
    )

    # The tone drifts a little across the sustain, so take the median of several probes.
    probes = sorted(
        refine_period(samples, region_start + (region_end - region_start) * step // 8, sample_rate)
        for step in range(1, 7)
    )
    period = probes[len(probes) // 2]
    crossfade = int(round(crossfade_cycles * period))
    print(f"  period={period:.3f} samples ({sample_rate / period:.2f} Hz) crossfade={crossfade} samples")

    min_cycles = max(8, int(math.ceil(min_seconds * sample_rate / period)))
    max_cycles = max(min_cycles, int(math.floor((max_samples - crossfade) / period)))
    loop_start, loop_length, match = find_loop(
        samples, (region_start, region_end), period, crossfade, min_cycles, max_cycles
    )
    print(
        f"  loop start={loop_start} ({loop_start / sample_rate * 1000:.0f} ms) "
        f"length={loop_length} cycles={loop_length / period:.2f} match={match:.3f}"
    )

    loop = bake_crossfade(samples, loop_start, loop_length, crossfade)
    window = max(8, int(round(period * 2.0)))
    radius = max(2, int(round(period * 4.0)))
    level_before = level_excursion_db(circular_smooth(circular_rms(loop, window), radius))
    loop, level_removed_db = flatten_loop_level(loop, period)
    level_after = level_excursion_db(circular_smooth(circular_rms(loop, window), radius))
    print(
        f"  level flatten: swell {level_before:.2f} dB -> {level_after:.2f} dB "
        f"(removed {level_removed_db:.2f} dB)"
    )
    report = seam_report(loop, period)
    report["level_swell_db"] = level_after
    pcm = to_pcm16(loop)

    attack_hz = track[0][1] if track else settled_hz
    settled_hz = sample_rate / period
    ratio = start_ratio if start_ratio > 0.0 else attack_hz / settled_hz
    env_coeff = math.exp(-1.0 / (tau * HOST_RATE)) if tau > 0.0 else 0.0
    return {
        "name": "DJ",
        "pcm": pcm,
        "looping": True,
        "start_ratio": ratio,
        "env_coeff": env_coeff,
        "settled_hz": settled_hz,
        "length": len(pcm),
        "period_samples": period,
        "crossfade": crossfade,
        "match": match,
        "report": report,
    }


def write_header(path: pathlib.Path, horns: list[dict], sample_rate: int) -> None:
    lines = [
        "#pragma once",
        "",
        "// Auto-generated 16-bit PCM loop with pitch-envelope metadata.",
        f"// Embedded sample rate: {sample_rate} Hz (host playback is {HOST_RATE} Hz).",
        "// The loop wrap is already crossfaded into the head, so playback is a plain",
        "// wrapping read; do not add another crossfade at run time.",
        "",
        "#include <stdint.h>",
        "",
        f"static const uint32_t kAirhornSampleRate = {sample_rate}u;",
        f"static const uint32_t kAirhornCount = {len(horns)}u;",
        "",
        "typedef struct AirhornSample",
        "{",
        "  uint32_t offset;",
        "  uint32_t length;",
        "  float start_ratio;",
        "  float env_coeff;",
        "  uint8_t looping;",
        "} AirhornSample;",
        "",
        "static const int16_t kAirhornPcm16[] = {",
    ]

    offset = 0
    table_rows: list[str] = []
    for horn in horns:
        blob = horn["pcm"]
        for byte_index in range(0, len(blob), 12):
            chunk = blob[byte_index : byte_index + 12]
            joined = ", ".join(str(value) for value in chunk)
            lines.append(f"  {joined},")
        looping = 1 if horn["looping"] else 0
        table_rows.append(
            "  { "
            f"{offset}u, {len(blob)}u, {horn['start_ratio']:.6f}f, "
            f"{horn['env_coeff']:.8f}f, {looping}u "
            "},"
        )
        offset += len(blob)

    lines.extend(
        [
            "};",
            "",
            "static const AirhornSample kAirhornSamples[kAirhornCount] = {",
            *table_rows,
            "};",
            "",
        ]
    )
    path.write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rate", type=int, default=24000)
    parser.add_argument("--max-bytes", type=int, default=28000)
    parser.add_argument(
        "--crossfade-cycles",
        type=float,
        default=12.0,
        help="loop crossfade length in fundamental periods",
    )
    parser.add_argument("--min-seconds", type=float, default=0.40, help="shortest acceptable loop")
    parser.add_argument("--start-ratio", type=float, default=1.448008, help="0 = derive from the attack")
    parser.add_argument("--pitch-tau", type=float, default=0.12, help="pitch envelope time constant")
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("wav", type=pathlib.Path)
    args = parser.parse_args()

    max_samples = max(64, args.max_bytes // 2)
    horn = extract_dj_loop(
        args.wav,
        args.rate,
        max_samples,
        start_ratio=args.start_ratio,
        tau=args.pitch_tau,
        crossfade_cycles=args.crossfade_cycles,
        min_seconds=args.min_seconds,
    )
    duration_ms = 1000.0 * horn["length"] / args.rate
    report = horn["report"]
    print(
        f"{horn['name']}: loop {horn['length']} samples ({duration_ms:.0f} ms) "
        f"ratio={horn['start_ratio']:.3f} settled={horn['settled_hz']:.1f} Hz "
        f"cycles={report['cycles']:.2f}"
    )
    print(
        f"  seam: sample jump={report['sample_jump']:.5f} "
        f"excess over loop interior={report['seam_excess_db']:+.2f} dB "
        f"level swell={report['level_swell_db']:.2f} dB"
    )
    write_header(args.out, [horn], args.rate)
    print(f"Wrote {args.out} ({horn['length'] * 2} bytes PCM16)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
