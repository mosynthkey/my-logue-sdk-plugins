#!/usr/bin/env python3
"""Embed a 1-bar WAV as 8-bit PCM for WavSlice.

Resolution order when --wav is omitted:
  1. assets/loop.wav          (local override, gitignored)
  2. assets/default-loop.wav  (shipped CC0 backbeat)
  3. click-grid placeholder   (so CI still builds if the WAV is missing)

BPM: pass --bpm, or it is inferred from the file length as --bars bars
(bpm = 240 * bars / duration). Use --start to pick one bar out of a longer file.

Do not commit PCM or loop.wav derived from a recording you cannot redistribute.
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
CHUNKS = 32
PLUGIN_ROOT = pathlib.Path(__file__).resolve().parents[1]
OVERRIDE_WAV = PLUGIN_ROOT / "assets" / "loop.wav"
DEFAULT_WAV = PLUGIN_ROOT / "assets" / "default-loop.wav"


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
        f"static const uint32_t kSliceSampleRate = {PCM_RATE}u;",
        f"static const float kSliceSourceBpm = {bpm:.1f}f;",
        f"static const uint32_t kSliceSixteenths = 16u;",
        f"static const uint32_t kSlicePcmLength = {len(codes)}u;",
        "",
        "static const int8_t kSlicePcm8[] = {",
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


def load_mono(path: pathlib.Path) -> tuple[list[float], int]:
    if shutil.which("ffmpeg"):
        try:
            samples, rate = load_wav_python(path)
            if rate > 0:
                return samples, rate
        except SystemExit:
            pass
        samples = load_mono_ffmpeg(path, 44100)
        return samples, 44100
    return load_wav_python(path)


def bpm_from_duration(n_samples: int, sample_rate: int, bars: float) -> float:
    duration = n_samples / float(sample_rate)
    if duration < 0.12:
        raise SystemExit("WAV is too short to infer BPM")
    return 240.0 * bars / duration


def cut_bar(samples: list[float], sample_rate: int, start_sec: float, bpm: float) -> list[float]:
    bar_seconds = 4.0 * 60.0 / bpm
    start = max(0, int(round(start_sec * sample_rate)))
    needed = int(round(bar_seconds * sample_rate))
    if start >= len(samples):
        raise SystemExit("start is past the end of the WAV")
    region = samples[start : start + needed]
    if len(region) < 32:
        raise SystemExit("WAV region is too short for one bar")
    if len(region) < needed:
        region = region + [0.0] * (needed - len(region))
    return region


def resolve_wav(explicit: pathlib.Path | None) -> pathlib.Path | None:
    if explicit is not None:
        if not explicit.is_file():
            raise SystemExit(f"WAV not found: {explicit}")
        return explicit
    if OVERRIDE_WAV.is_file():
        return OVERRIDE_WAV
    if DEFAULT_WAV.is_file():
        return DEFAULT_WAV
    return None


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        default=PLUGIN_ROOT / "dsp" / "wavslice_pcm.h",
    )
    parser.add_argument(
        "--wav",
        type=pathlib.Path,
        default=None,
        help="WAV to embed. Default: assets/loop.wav, else assets/default-loop.wav",
    )
    parser.add_argument(
        "--bpm",
        type=float,
        default=None,
        help="Source BPM of the WAV. Inferred from duration if omitted",
    )
    parser.add_argument("--start", type=float, default=0.0, help="Start time in seconds")
    parser.add_argument(
        "--bars",
        type=float,
        default=1.0,
        help="How many bars the (remaining) file is, used only to infer BPM",
    )
    parser.add_argument(
        "--write-loop",
        type=pathlib.Path,
        default=None,
        help="Write the cut 1-bar region as 16-bit WAV (typically assets/loop.wav)",
    )
    args = parser.parse_args()

    wav_path = resolve_wav(args.wav)
    origin: str
    bpm: float
    pcm: list[float]

    if wav_path is None:
        bpm = args.bpm if args.bpm is not None else 120.0
        pcm = synthesize_click_bar(bpm)
        origin = "Placeholder click grid (no default-loop.wav or loop.wav)."
        print("no WAV found, writing click placeholder")
    else:
        samples, rate = load_mono(wav_path)
        start = max(0, int(round(args.start * rate)))
        remaining = samples[start:]
        if args.bpm is not None:
            bpm = args.bpm
        else:
            bpm = bpm_from_duration(len(remaining), rate, args.bars)
            print(f"inferred bpm={bpm:.3f} from {len(remaining) / float(rate):.3f}s / {args.bars:g} bar(s)")
        if bpm < 40.0 or bpm > 300.0:
            raise SystemExit(
                f"BPM {bpm:.2f} is outside 40-300. Pass --bpm or --bars (and --start) explicitly."
            )
        if bpm < 60.0 or bpm > 220.0:
            print(f"warning: unusual BPM {bpm:.2f}; pass --bpm if this is a longer loop")

        bar = cut_bar(samples, rate, args.start, bpm)
        if args.write_loop is not None:
            write_wav(args.write_loop, bar, rate)
            print(f"wrote 1-bar WAV {args.write_loop}")

        pcm = fit_length(resample_linear(bar, rate, PCM_RATE), pcm_length_for_bpm(bpm))
        origin = f"Embedded from {wav_path.name}."
        print(f"embedding {wav_path}")

    if len(pcm) % CHUNKS != 0:
        raise SystemExit(f"PCM length {len(pcm)} is not divisible by {CHUNKS}")
    codes = to_pcm8(pcm)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    write_header(args.out, codes, bpm, origin)
    peak = max(abs(code) for code in codes)
    rms = math.sqrt(sum(code * code for code in codes) / float(len(codes)))
    print(f"wrote {args.out} length={len(codes)} bpm={bpm:.2f} peak={peak} rms={rms:.1f}")


if __name__ == "__main__":
    main()
