#!/usr/bin/env python3
"""Embed a local 1-bar break WAV as 8-bit PCM for AmenWav.

The Winstons' Amen, Brother recording is not fetched or committed here.
Put your own 1-bar WAV at assets/break.wav (gitignored). Without that file
this script writes an original click-grid placeholder so CI can still build.
Do not commit PCM derived from a recording you do not have rights to ship.
"""

from __future__ import annotations

import argparse
import math
import pathlib
import shutil
import struct
import subprocess
import tempfile
import wave

PCM_RATE = 12000
DEFAULT_BPM = 136.0
CHUNKS = 32


def pcm_length_for_bpm(bpm: float) -> int:
    bar_seconds = 4.0 * 60.0 / bpm
    length = int(round(PCM_RATE * bar_seconds))
    length += (CHUNKS - (length % CHUNKS)) % CHUNKS
    return length


def to_pcm8(samples: list[float]) -> list[int]:
    peak = max(abs(sample) for sample in samples) or 1.0
    scale = 0.97 / peak
    codes: list[int] = []
    for sample in samples:
        quantized = int(round(sample * scale * 127.0))
        codes.append(max(-127, min(127, quantized)))
    return codes


def write_header(path: pathlib.Path, codes: list[int], bpm: float, origin: str) -> None:
    lines = [
        "#pragma once",
        "",
        f"// {origin}",
        f"// {PCM_RATE} Hz, 8-bit signed, {bpm:.0f} BPM, 16 equal 16th slices.",
        "",
        "#include <stdint.h>",
        "",
        f"static const uint32_t kAmenSampleRate = {PCM_RATE}u;",
        f"static const float kAmenSourceBpm = {bpm:.1f}f;",
        f"static const uint32_t kAmenSixteenths = 16u;",
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


def synthesize_click_bar(bpm: float) -> list[float]:
    length = pcm_length_for_bpm(bpm)
    bar = [0.0] * length
    slice_len = length // 16
    for step_index in range(16):
        accent = 1.0 if (step_index % 4) == 0 else 0.45
        start = step_index * slice_len
        for sample_index in range(start, min(start + 180, length)):
            age = (sample_index - start) / float(PCM_RATE)
            click = math.sin(2.0 * math.pi * 1800.0 * age) * math.exp(-age / 0.004)
            body = math.sin(2.0 * math.pi * (90.0 if accent > 0.7 else 220.0) * age)
            body *= math.exp(-age / 0.05)
            bar[sample_index] += accent * (click * 0.35 + body * 0.8)
    return bar


def load_wav_python(path: pathlib.Path) -> tuple[list[float], int]:
    with wave.open(str(path), "rb") as handle:
        channels = handle.getnchannels()
        width = handle.getsampwidth()
        rate = handle.getframerate()
        frames = handle.readframes(handle.getnframes())
    if width != 2:
        raise SystemExit(f"{path} must be 16-bit PCM, or install ffmpeg")
    raw = struct.unpack(f"<{len(frames) // 2}h", frames)
    samples: list[float] = []
    if channels == 1:
        samples = [sample / 32768.0 for sample in raw]
    else:
        for frame_index in range(0, len(raw), channels):
            mix = sum(raw[frame_index : frame_index + channels]) / float(channels)
            samples.append(mix / 32768.0)
    return samples, rate


def load_mono_ffmpeg(path: pathlib.Path, target_rate: int) -> list[float]:
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
    samples, rate = load_wav_python(tmp_path)
    tmp_path.unlink(missing_ok=True)
    if rate != target_rate:
        raise SystemExit(f"ffmpeg resample failed, got {rate}")
    return samples


def fit_length(samples: list[float], dst_len: int) -> list[float]:
    if dst_len <= 1 or not samples:
        return [0.0] * dst_len
    if len(samples) == dst_len:
        return list(samples)
    out: list[float] = []
    scale = (len(samples) - 1) / float(dst_len - 1)
    for dst_index in range(dst_len):
        src_pos = dst_index * scale
        index0 = int(src_pos)
        index1 = min(index0 + 1, len(samples) - 1)
        frac = src_pos - index0
        out.append(samples[index0] + (samples[index1] - samples[index0]) * frac)
    return out


def resample_linear(samples: list[float], src_rate: int, dst_rate: int) -> list[float]:
    if src_rate == dst_rate:
        return list(samples)
    dst_len = max(1, int(round(len(samples) * dst_rate / float(src_rate))))
    return fit_length(samples, dst_len)


def cut_and_fit(samples: list[float], sample_rate: int, start_sec: float, bpm: float) -> list[float]:
    bar_seconds = 4.0 * 60.0 / bpm
    start = max(0, int(round(start_sec * sample_rate)))
    stop = start + int(round(bar_seconds * sample_rate))
    if start >= len(samples):
        raise SystemExit("start is past the end of the WAV")
    region = samples[start:stop]
    if len(region) < 32:
        raise SystemExit("WAV region is too short for one bar")
    needed = stop - start
    if len(region) < needed:
        region = region + [0.0] * (needed - len(region))
    return fit_length(region, pcm_length_for_bpm(bpm))


def load_break(path: pathlib.Path, bpm: float, start_sec: float) -> list[float]:
    if shutil.which("ffmpeg"):
        samples = load_mono_ffmpeg(path, PCM_RATE)
        return cut_and_fit(samples, PCM_RATE, start_sec, bpm)
    samples, rate = load_wav_python(path)
    samples = resample_linear(samples, rate, PCM_RATE)
    return cut_and_fit(samples, PCM_RATE, start_sec, bpm)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        default=pathlib.Path("plugins/amenwav/dsp/amenwav_pcm.h"),
    )
    parser.add_argument(
        "--wav",
        type=pathlib.Path,
        default=pathlib.Path("plugins/amenwav/assets/break.wav"),
    )
    parser.add_argument("--bpm", type=float, default=DEFAULT_BPM)
    parser.add_argument("--start", type=float, default=0.0, help="Start time in seconds")
    args = parser.parse_args()

    if args.wav.is_file():
        pcm = load_break(args.wav, args.bpm, args.start)
        origin = f"Embedded from local WAV {args.wav.name}. Not committed."
        print(f"embedding {args.wav}")
    else:
        pcm = synthesize_click_bar(args.bpm)
        origin = "Placeholder click grid (no assets/break.wav). Not the Winstons recording."
        print(f"no {args.wav}, writing click placeholder")

    if len(pcm) % CHUNKS != 0:
        raise SystemExit(f"PCM length {len(pcm)} is not divisible by {CHUNKS}")
    codes = to_pcm8(pcm)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_header(args.out, codes, args.bpm, origin)
    peak = max(abs(code) for code in codes)
    rms = math.sqrt(sum(code * code for code in codes) / float(len(codes)))
    print(f"wrote {args.out} length={len(codes)} peak={peak} rms={rms:.1f}")


if __name__ == "__main__":
    main()
