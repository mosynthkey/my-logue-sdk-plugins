#!/usr/bin/env python3
"""Pack a TR-909 HiHat 27C256 / Intel HEX dump as 6-bit PCM for the NTS-3 unit.

The 9090 / MAME HiHat image is 32 KB with audio in bits 7:2 (unsigned offset
binary, midpoint 32). Bits 1:0 are unused. This script keeps that 6-bit stream
and packs four samples into three bytes for the 32 KB genericfx budget.
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import zlib
import hashlib


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
    # Hi-hat MAME image keeps occasional low bits; audio is still in bits 7:2.
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


def write_header(path: pathlib.Path, codes: bytes, packed: bytes, rom_crc: int, rom_sha1: str) -> None:
    lines = [
        "#pragma once",
        "",
        "// Shared TR-909 Hi-Hat full ROM (6-bit packed). Used by HHat.",
        f"// Source ROM CRC32={rom_crc:08x} SHA1={rom_sha1}",
        "// MAME: hn61256p__c43.ic69 / Colin Fraser r909hh.wav",
        "// Each sample is unsigned offset-binary in bits 7:2 of the original byte.",
        "",
        "#include <stdint.h>",
        "",
        f"static const uint32_t kTr909HhPcmLength = {len(codes)}u;",
        "static constexpr float kTr909HhRomClockHz = 30000.f;",
        f"static const uint32_t kTr909HhPcmPackedSize = {len(packed)}u;",
        "",
        "static const uint8_t kTr909HhPcmPacked[] = {",
    ]
    bytes_per_line = 16
    for offset in range(0, len(packed), bytes_per_line):
        chunk = packed[offset : offset + bytes_per_line]
        joined = ", ".join(f"0x{byte:02x}" for byte in chunk)
        lines.append(f"  {joined},")
    lines.append("};")
    lines.append("")
    path.write_text("\n".join(lines))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=pathlib.Path, required=True)
    parser.add_argument(
        "--out",
        type=pathlib.Path,
        default=pathlib.Path("plugins/common/tr909_hh_pcm.h"),
    )
    args = parser.parse_args()

    rom = load_rom(args.rom)
    crc = zlib.crc32(rom) & 0xFFFFFFFF
    sha1 = hashlib.sha1(rom).hexdigest()
    codes = extract_pcm6(rom)
    packed = pack_pcm6(codes)
    write_header(args.out, codes, packed, crc, sha1)
    duration_ms = 1000.0 * len(codes) / 30000.0
    print(
        f"Wrote {args.out} ({len(codes)} x 6-bit, {duration_ms:.0f} ms @ 30 kHz, "
        f"{len(packed)} packed bytes, crc32={crc:08x})"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
