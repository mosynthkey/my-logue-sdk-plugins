#!/usr/bin/env python3
"""Extract loopable 16-bit horn PCM plus pitch-envelope metadata."""

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
    if not track:
        return 0, sample_count, 1.0

    frequencies = [freq for _, freq, _ in track]
    median = sorted(frequencies)[len(frequencies) // 2]
    stable_starts = [start for start, freq, _ in track if abs(1200.0 * math.log2(freq / median)) < 25.0]
    if not stable_starts:
        start = track[len(track) // 2][0]
        return start, sample_count, median

    region_start = stable_starts[0]
    region_end = min(sample_count, stable_starts[-1] + int(0.04 * 48000))
    if region_end - region_start < 1024:
        region_end = min(sample_count, region_start + 4096)
    return region_start, region_end, median


def seam_cost(samples: list[float], loop_length: int) -> float:
    if loop_length < 2:
        return 1.0
    head = samples[0]
    tail = samples[loop_length - 1]
    head_slope = samples[1] - samples[0]
    tail_slope = samples[loop_length - 1] - samples[loop_length - 2]
    return abs(head - tail) + 0.35 * abs(head_slope - tail_slope)


def best_loop(samples: list[float], period: float, min_periods: int, max_periods: int) -> tuple[int, int]:
    period_samples = max(8, int(round(period)))
    best_score = -1.0
    best_start = 0
    best_length = min(len(samples), period_samples * min_periods)
    if best_length < 32:
        return 0, len(samples)

    for period_count in range(min_periods, max_periods + 1):
        loop_length = period_samples * period_count
        compare_count = min(period_samples * 2, 256, loop_length // 2)
        if loop_length + compare_count > len(samples):
            break
        step = max(1, period_samples // 8)
        last_start = len(samples) - loop_length - compare_count
        start = 0
        while start <= last_start:
            acc = 0.0
            for sample_index in range(compare_count):
                acc += samples[start + sample_index] * samples[start + loop_length + sample_index]
            score = acc / float(compare_count)
            score -= seam_cost(samples[start : start + loop_length], loop_length) * 3.0
            if score > best_score:
                best_score = score
                best_start = start
                best_length = loop_length
            start += step

    if best_start + best_length > len(samples):
        best_length = len(samples) - best_start
    return best_start, best_length


def rotate_to_best_seam(samples: list[float], period_samples: int) -> list[float]:
    if len(samples) < period_samples * 2:
        return samples
    best_offset = 0
    best_cost = seam_cost(samples, len(samples))
    for offset in range(period_samples, len(samples) - period_samples, period_samples):
        rotated = samples[offset:] + samples[:offset]
        cost = seam_cost(rotated, len(rotated))
        if cost < best_cost:
            best_cost = cost
            best_offset = offset
    if best_offset == 0:
        return samples
    return samples[best_offset:] + samples[:best_offset]


def crossfade_loop(samples: list[float], xfade: int) -> list[float]:
    if xfade < 4 or len(samples) <= xfade * 2:
        return samples
    body_length = len(samples) - xfade
    out = samples[:body_length]
    for fade_index in range(xfade):
        fade = fade_index / float(xfade)
        tail = samples[body_length + fade_index]
        out[fade_index] = out[fade_index] * (1.0 - fade) + tail * fade
    return out


def gentle_lowpass(samples: list[float], sample_rate: int, cutoff_hz: float) -> list[float]:
    coeff = math.exp((-2.0 * math.pi * cutoff_hz) / sample_rate)
    state = samples[0] if samples else 0.0
    out: list[float] = []
    for sample in samples:
        state = coeff * state + (1.0 - coeff) * sample
        out.append(state)
    return out


def fade_edges(samples: list[float], fade_in: int, fade_out: int) -> list[float]:
    out = list(samples)
    fade_in = min(fade_in, len(out) // 4)
    fade_out = min(fade_out, len(out) // 4)
    for sample_index in range(fade_in):
        out[sample_index] *= sample_index / float(fade_in)
    for sample_index in range(fade_out):
        out[len(out) - 1 - sample_index] *= sample_index / float(fade_out)
    return out


def trim_first_burst(samples: list[float], sample_rate: int, max_seconds: float) -> list[float]:
    hop = max(1, sample_rate // 200)
    threshold = 0.04
    start = 0
    for sample_index in range(0, len(samples), hop):
        if abs(samples[sample_index]) > threshold:
            start = max(0, sample_index - hop)
            break
    max_length = int(sample_rate * max_seconds)
    end = min(len(samples), start + max_length)
    quiet_needed = int(sample_rate * 0.04)
    quiet_run = 0
    for sample_index in range(start + int(sample_rate * 0.08), end):
        if abs(samples[sample_index]) < threshold * 0.5:
            quiet_run += 1
            if quiet_run >= quiet_needed:
                end = sample_index - quiet_needed + hop
                break
        else:
            quiet_run = 0
    if end <= start:
        end = min(len(samples), start + max_length)
    return samples[start:end]


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


def extract_horn(path: pathlib.Path, sample_rate: int, looping: bool, start_ratio: float, tau: float) -> dict:
    samples = load_mono_wav(path, sample_rate)
    track = pitch_track(samples, sample_rate)

    if looping:
        region_start, region_end, settled_hz = stable_region(track, len(samples))
        max_region = int(sample_rate * 0.9)
        if region_end - region_start > max_region:
            region_end = region_start + max_region
        region = samples[region_start:region_end]
        print(
            f"  region {region_start}:{region_end} ({len(region)} samples) "
            f"settled={settled_hz:.1f} Hz"
        )
        period = sample_rate / settled_hz if settled_hz > 1.0 else 64.0
        measured = yin_period(region[: min(len(region), int(sample_rate * 0.08))], sample_rate, 80.0, 1600.0)
        if measured:
            period = measured
        period_samples = max(8, int(round(period)))
        min_periods = max(16, int(math.ceil(0.15 * sample_rate / period_samples)))
        max_periods = max(min_periods + 4, int(math.floor(0.22 * sample_rate / period_samples)))
        loop_start, loop_length = best_loop(region, period, min_periods, max_periods)
        loop = list(region[loop_start : loop_start + loop_length])
        loop = rotate_to_best_seam(loop, period_samples)
        if seam_cost(loop, len(loop)) > 0.02:
            loop = crossfade_loop(loop, 96)
        loop = gentle_lowpass(loop, sample_rate, 9000.0)
        if len(loop) < 64:
            loop = region[: min(len(region), period_samples * 16)]
        pcm = to_pcm16(loop)
        attack_hz = track[0][1] if track else settled_hz
        ratio = start_ratio if start_ratio > 0.0 else attack_hz / settled_hz
    else:
        oneshot = trim_first_burst(samples, sample_rate, 0.22)
        pcm = to_pcm16(fade_edges(oneshot, 16, int(sample_rate * 0.03)))
        ratio = 1.0
        settled_hz = track[len(track) // 2][1] if track else 0.0

    env_coeff = math.exp(-1.0 / (tau * HOST_RATE)) if tau > 0.0 else 0.0
    return {
        "name": path.stem.upper().replace("-", "_")[:8],
        "pcm": pcm,
        "looping": looping,
        "start_ratio": ratio,
        "env_coeff": env_coeff,
        "settled_hz": settled_hz,
        "length": len(pcm),
    }


def write_header(path: pathlib.Path, horns: list[dict], sample_rate: int) -> None:
    lines = [
        "#pragma once",
        "",
        "// Auto-generated 16-bit PCM loops / one-shots with pitch-envelope metadata.",
        f"// Embedded sample rate: {sample_rate} Hz (host playback is {HOST_RATE} Hz).",
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
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("wav", nargs="+", type=pathlib.Path)
    args = parser.parse_args()

    specs = [
        {"looping": True, "start_ratio": 0.0, "tau": 0.12},
        {"looping": True, "start_ratio": 1.0, "tau": 0.08},
        {"looping": False, "start_ratio": 1.0, "tau": 0.0},
    ]

    horns: list[dict] = []
    for wav_path, spec in zip(args.wav, specs):
        horn = extract_horn(wav_path, args.rate, spec["looping"], spec["start_ratio"], spec["tau"])
        horns.append(horn)
        kind = "loop" if horn["looping"] else "oneshot"
        duration_ms = 1000.0 * horn["length"] / args.rate
        print(
            f"{horn['name']}: {kind} {horn['length']} samples ({duration_ms:.0f} ms) "
            f"ratio={horn['start_ratio']:.3f} settled={horn['settled_hz']:.1f} Hz"
        )

    write_header(args.out, horns, args.rate)
    total_bytes = sum(horn["length"] * 2 for horn in horns)
    print(f"Wrote {args.out} ({total_bytes} bytes PCM16)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
