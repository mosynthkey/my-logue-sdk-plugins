#!/usr/bin/env python3
"""Pack trimmed mono WAV files as 8-bit PCM for the AirHorn logue unit."""

from __future__ import annotations

import argparse
import pathlib
import struct
import subprocess
import sys
import wave


def load_mono_wav(path: pathlib.Path, target_rate: int) -> list[int]:
    tmp = path.with_suffix(".tmp.wav")
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
            str(tmp),
        ],
        check=True,
    )
    with wave.open(str(tmp), "rb") as handle:
        if handle.getnchannels() != 1:
            raise SystemExit(f"{path}: expected mono")
        frames = handle.readframes(handle.getnframes())
    tmp.unlink(missing_ok=True)
    samples = struct.unpack(f"<{len(frames) // 2}h", frames)
    return list(samples)


def trim_to_peak(samples: list[int], max_samples: int) -> list[int]:
    if len(samples) <= max_samples:
        return samples

    best_start = 0
    best_energy = -1.0
    window = max_samples
    for start_index in range(0, len(samples) - window + 1):
        energy = 0.0
        for sample_index in range(start_index, start_index + window):
            value = samples[sample_index]
            energy += float(value * value)
        if energy > best_energy:
            best_energy = energy
            best_start = start_index
    return samples[best_start : best_start + window]


def to_pcm8(samples: list[int]) -> bytes:
    peak = max(abs(sample) for sample in samples) or 1
    scale = 127.0 / float(peak)
    packed = bytearray(len(samples))
    for sample_index, sample in enumerate(samples):
        scaled = int(round(float(sample) * scale))
        if scaled > 127:
            scaled = 127
        if scaled < -128:
            scaled = -128
        packed[sample_index] = (scaled + 128) & 0xFF
    return bytes(packed)


def write_header(
    path: pathlib.Path,
    horns: list[tuple[str, bytes]],
    sample_rate: int,
) -> None:
    lines = [
        "#pragma once",
        "",
        "// Auto-generated 8-bit unsigned PCM horns.",
        f"// Embedded sample rate: {sample_rate} Hz (host playback is 48 kHz).",
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
        "  const char *name;",
        "} AirhornSample;",
        "",
        "static const uint8_t kAirhornPcm8[] = {",
    ]

    offset = 0
    table_rows: list[str] = []
    for name, blob in horns:
        for byte_index in range(0, len(blob), 16):
            chunk = blob[byte_index : byte_index + 16]
            joined = ", ".join(f"0x{byte:02x}" for byte in chunk)
            lines.append(f"  {joined},")
        table_rows.append(
            f"  {{ {offset}u, {len(blob)}u, \"{name}\" }},"
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
    parser.add_argument("--rate", type=int, default=22050)
    parser.add_argument("--max-seconds", type=float, default=0.65)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("wav", nargs="+", type=pathlib.Path)
    args = parser.parse_args()

    max_samples = int(args.rate * args.max_seconds)
    horns: list[tuple[str, bytes]] = []
    for wav_path in args.wav:
        label = wav_path.stem.upper().replace("-", "_")[:8]
        samples = load_mono_wav(wav_path, args.rate)
        trimmed = trim_to_peak(samples, max_samples)
        horns.append((label, to_pcm8(trimmed)))
        duration_ms = 1000.0 * len(trimmed) / args.rate
        print(f"{label}: {len(trimmed)} samples ({duration_ms:.0f} ms)")

    write_header(args.out, horns, args.rate)
    total_bytes = sum(len(blob) for _, blob in horns)
    print(f"Wrote {args.out} ({total_bytes} bytes PCM8)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
