#!/usr/bin/env python3
"""Encode the ride WAV as G.711 mu-law for the NTS-3 32 KB unit budget."""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import struct
import sys

BIAS = 0x84
CLIP = 32635


def linear_to_ulaw(sample: int) -> int:
    sign = 0
    if sample < 0:
        sign = 0x80
        sample = -sample
    if sample > CLIP:
        sample = CLIP
    sample += BIAS
    exponent = 7
    mask = 0x4000
    while exponent > 0 and (sample & mask) == 0:
        exponent -= 1
        mask >>= 1
    mantissa = (sample >> (exponent + 3)) & 0x0F
    return (~(sign | (exponent << 4) | mantissa)) & 0xFF


def load_wav_f32(path: pathlib.Path, sample_rate: int) -> list[float]:
    result = subprocess.run(
        [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "error",
            "-i",
            str(path),
            "-ac",
            "1",
            "-ar",
            str(sample_rate),
            "-f",
            "f32le",
            "-",
        ],
        check=True,
        stdout=subprocess.PIPE,
    )
    raw = result.stdout
    count = len(raw) // 4
    return list(struct.unpack("<" + "f" * count, raw))


def encode_ulaw(samples: list[float]) -> bytes:
    peak = max(abs(value) for value in samples) if samples else 1.0
    scale = (0.99 * 32767.0) / peak if peak > 0.0 else 1.0
    encoded = bytearray()
    for sample in samples:
        pcm = int(round(sample * scale))
        if pcm > 32767:
            pcm = 32767
        elif pcm < -32768:
            pcm = -32768
        encoded.append(linear_to_ulaw(pcm))
    return bytes(encoded)


def write_header(path: pathlib.Path, sample_rate: int, payload: bytes) -> None:
    lines = [
        "#pragma once",
        "",
        "// MARS 909 ride, G.711 mu-law. Kept small for the NTS-3 32 KB unit limit.",
        "// Source: plugins/ride909/assets/MARS_909_ride_smooth_mid.wav",
        "",
        "#include <stdint.h>",
        "",
        f"static const uint32_t kRide909SampleLength = {len(payload)}u;",
        f"static const float kRide909SampleRate = {sample_rate}.f;",
        "",
        "static const uint8_t kRide909SampleData[] = {",
    ]
    bytes_per_line = 16
    for offset in range(0, len(payload), bytes_per_line):
        chunk = payload[offset : offset + bytes_per_line]
        joined = ", ".join(f"0x{byte:02x}" for byte in chunk)
        lines.append(f"  {joined},")
    lines.append("};")
    lines.append("")
    path.write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--wav", type=pathlib.Path, required=True)
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--rate", type=int, default=22050)
    args = parser.parse_args()

    samples = load_wav_f32(args.wav, args.rate)
    payload = encode_ulaw(samples)
    write_header(args.out, args.rate, payload)
    duration_ms = 1000.0 * len(payload) / args.rate
    print(
        f"Wrote {args.out} ({len(payload)} samples, {duration_ms:.0f} ms, "
        f"{args.rate} Hz mu-law, {len(payload)} bytes)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
