#!/usr/bin/env python3
"""Pack TR-909 Hi-Hat ROM into the shared crop header for Trap808 / Trance.

Closed = top quarter of the 32 KB ROM. Open = truncated start of the lower
3/4 so the NTS-3 32 KB unit budget still fits kick/snare/808 DSP.

Usage:
  python3 plugins/trap808/scripts/embed_hh_rom.py \\
    --rom /path/to/909hh.hex \\
    --out plugins/common/tr909_hh_crop_pcm.h
"""

from __future__ import annotations

import argparse
import hashlib
import pathlib
import zlib


def parse_ihex(path: pathlib.Path) -> bytes:
    data = bytearray(0x8000)
    filled = 0
    with path.open("r", encoding="ascii", newline="") as handle:
        for line in handle:
            line = line.strip()
            if not line or not line.startswith(":"):
                continue
            rec = bytes.fromhex(line[1:])
            count = rec[0]
            addr = (rec[1] << 8) | rec[2]
            rtype = rec[3]
            payload = rec[4 : 4 + count]
            if ((sum(rec[: 5 + count])) & 0xFF) != 0:
                raise SystemExit(f"bad Intel HEX checksum at 0x{addr:04x}")
            if rtype == 0:
                data[addr : addr + count] = payload
                filled += count
            elif rtype == 1:
                break
    if filled != 0x8000:
        raise SystemExit(f"expected 32768 data bytes, filled {filled}")
    return bytes(data)


def load_rom(path: pathlib.Path) -> bytes:
    if path.suffix.lower() in {".hex", ".ihex"}:
        return parse_ihex(path)
    raw = path.read_bytes()
    if len(raw) != 0x8000:
        raise SystemExit(f"expected 32768-byte ROM, got {len(raw)}")
    return raw


def extract_pcm6(rom: bytes) -> bytes:
    return bytes((byte >> 2) & 0x3F for byte in rom)


def pack_pcm6(codes: bytes) -> bytes:
    packed = bytearray((len(codes) * 6 + 7) // 8 + 1)
    for sample_index, code in enumerate(codes):
        bit_index = sample_index * 6
        byte_index = bit_index >> 3
        shift = bit_index & 7
        packed[byte_index] |= (code << shift) & 0xFF
        packed[byte_index + 1] |= (code << shift) >> 8
    return bytes(packed)


def write_array(name: str, packed: bytes, lines: list[str]) -> None:
    lines.append(f"static const uint32_t k{name}PackedSize = {len(packed)}u;")
    lines.append(f"static const uint8_t k{name}Packed[] = {{")
    for offset in range(0, len(packed), 16):
        chunk = packed[offset : offset + 16]
        joined = ", ".join(f"0x{byte:02x}" for byte in chunk)
        lines.append(f"  {joined},")
    lines.append("};")
    lines.append("")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=pathlib.Path, required=True)
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        default=pathlib.Path("plugins/common/tr909_hh_crop_pcm.h"),
    )
    parser.add_argument("--open-samples", type=int, default=8192)
    args = parser.parse_args()

    rom = load_rom(args.rom)
    crc = zlib.crc32(rom) & 0xFFFFFFFF
    sha1 = hashlib.sha1(rom).hexdigest()
    if crc != 0x2AAAE11B:
        raise SystemExit(f"unexpected ROM CRC32={crc:08x} (want 2aaae11b)")

    codes = extract_pcm6(rom)
    closed = bytes(codes[0x6000:0x8000])
    open_len = max(1024, min(args.open_samples, 0x6000))
    opened = bytes(codes[:open_len])
    closed_packed = pack_pcm6(closed)
    open_packed = pack_pcm6(opened)

    lines = [
        "#pragma once",
        "",
        "// Shared TR-909 Hi-Hat crop (6-bit packed) for Trap808 / Trance.",
        f"// Source ROM CRC32={crc:08x} SHA1={sha1}",
        "// MAME: hn61256p__c43.ic69  9090: 909hh.hex",
        "// Closed = top quarter; open = truncated start of lower 3/4 (unit size).",
        "// Bytes may have low bits set in the 9090 dump; we keep bits 7:2 only.",
        "",
        "#include <stdint.h>",
        "",
        f"static const uint32_t kTr909HhCropClosedLength = {len(closed)}u;",
        f"static const uint32_t kTr909HhCropOpenLength = {len(opened)}u;",
        "static constexpr float kTr909HhCropRomClockHz = 30000.f;",
        "",
    ]
    write_array("Tr909HhCropClosed", closed_packed, lines)
    write_array("Tr909HhCropOpen", open_packed, lines)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text("\n".join(lines) + "\n")
    print(
        f"Wrote {args.out} CH={len(closed)} ({len(closed_packed)}B) "
        f"OH={len(opened)} ({len(open_packed)}B) crc32={crc:08x}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
