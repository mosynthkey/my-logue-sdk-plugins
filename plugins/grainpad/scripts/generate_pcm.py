#!/usr/bin/env python3
"""Generate a rich vocal-ish texture for GrainPad granular playback."""

from __future__ import annotations

import math
import pathlib
import random
import struct
import wave


SAMPLE_RATE = 24000
DURATION_SEC = 3.0
OUTPUT_HEADER = pathlib.Path(__file__).resolve().parents[1] / "dsp" / "grainpad_pcm.h"


def synthesize_texture() -> list[float]:
    sample_count = int(SAMPLE_RATE * DURATION_SEC)
    samples: list[float] = [0.0] * sample_count

    random.seed(0x6A1D00D)

    for sample_index in range(sample_count):
        time_sec = sample_index / SAMPLE_RATE
        phase = time_sec * math.tau

        vowel = (
            0.45 * math.sin(phase * 5.1)
            + 0.30 * math.sin(phase * 10.3 + 0.4)
            + 0.18 * math.sin(phase * 15.7 + 1.1)
            + 0.12 * math.sin(phase * 21.2 + 2.0)
        )

        formant = (
            0.22 * math.sin(phase * 2.7 + math.sin(phase * 0.35) * 1.2)
            + 0.14 * math.sin(phase * 4.9 + math.sin(phase * 0.22) * 0.8)
        )

        breath = random.uniform(-1.0, 1.0) * 0.035
        shimmer = 1.0 + 0.08 * math.sin(phase * 0.17) + 0.05 * math.sin(phase * 0.43 + 1.7)
        fade = min(1.0, time_sec / 0.08) * min(1.0, (DURATION_SEC - time_sec) / 0.25)

        sample = (vowel + formant + breath) * shimmer * fade * 0.55
        if sample > 1.0:
            sample = 1.0
        if sample < -1.0:
            sample = -1.0
        samples[sample_index] = sample

    return samples


def crossfade_loop(samples: list[float], fade_ms: float = 40.0) -> list[float]:
    fade_samples = max(8, int(SAMPLE_RATE * fade_ms / 1000.0))
    length = len(samples)
    for blend_index in range(fade_samples):
        head = blend_index / fade_samples
        tail = 1.0 - head
        tail_index = length - fade_samples + blend_index
        samples[blend_index] = samples[blend_index] * tail + samples[tail_index] * head
    return samples


def float_to_pcm16(sample: float) -> int:
    scaled = int(round(sample * 32767.0))
    if scaled > 32767:
        return 32767
    if scaled < -32768:
        return -32768
    return scaled


def write_header(samples: list[float], path: pathlib.Path) -> None:
    pcm = [float_to_pcm16(sample) for sample in samples]
    lines: list[str] = []
    for row_start in range(0, len(pcm), 12):
        chunk = ", ".join(str(value) for value in pcm[row_start : row_start + 12])
        lines.append(f"  {chunk},")

    body = "\n".join(lines)
    path.write_text(
        f"""#pragma once

// Auto-generated vocal texture for GrainPad granular synthesis.
// Embedded sample rate: {SAMPLE_RATE} Hz (host playback is 48000 Hz).

#include <stdint.h>

static const uint32_t kGrainPadSampleRate = {SAMPLE_RATE}u;
static const uint32_t kGrainPadLength = {len(samples)}u;

static const int16_t kGrainPadPcm16[] = {{
{body}
}};
"""
    )


def main() -> None:
    samples = crossfade_loop(synthesize_texture())
    OUTPUT_HEADER.parent.mkdir(parents=True, exist_ok=True)
    write_header(samples, OUTPUT_HEADER)
    print(f"Wrote {len(samples)} samples to {OUTPUT_HEADER}")


if __name__ == "__main__":
    main()
