#!/usr/bin/env python3
"""
Extracts the 6 NES emulator executable parts from Metroid Zero Mission USA ROM.
The compressed stream lives at ROM offset 0x7D8360 (decompressed size 0x8524).
Slices correspond to the payload DMA distribution table:
  Part 1: 0x06006000..0x06007240 (size 0x1240, VRAM)
  Part 2: 0x03000000..0x03005A4C (size 0x5A4C, IWRAM)
  Part 3: 0x0600B000..0x0600B150 (size 0x150,  VRAM)
  Part 4: 0x0600C000..0x0600C060 (size 0x60,   VRAM)
  Part 5: 0x0600E000..0x0600ED88 (size 0xD88,  VRAM)
  Part 6: 0x0203E000..0x0203E8E0 (size 0x8E0,  EWRAM)
Outputs to .local/nes-emulator/part[1-6].bin.
"""

import argparse
import hashlib
import os
import sys

EXPECTED_ROM_SHA1 = "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8"
STREAM_ROM_OFFSET = 0x7D8360
DECOMPRESSED_TOTAL_SIZE = 0x8524

PARTS_SPEC = [
    {
        "name": "part1",
        "start": 0x80,
        "size": 0x1240,
        "load_addr": 0x06006000,
        "sha256": "eff34fbc387676a159dce57aff8089a2d6e355eaf96ecdef32155422c14da6d5",
        "region": "VRAM",
    },
    {
        "name": "part2",
        "start": 0x12C0,
        "size": 0x5A4C,
        "load_addr": 0x03000000,
        "sha256": "15df40ee533211143aae8cd3deb6062f9d0364135479b0c361e9a24ca6ff0fb2",
        "region": "IWRAM",
    },
    {
        "name": "part3",
        "start": 0x6D0C,
        "size": 0x150,
        "load_addr": 0x0600B000,
        "sha256": "c9978bfe63c71e714f5b95c1324abcbddfd1a613a74a44c84f158438f421c3d9",
        "region": "VRAM",
    },
    {
        "name": "part4",
        "start": 0x6E5C,
        "size": 0x60,
        "load_addr": 0x0600C000,
        "sha256": "201cd71270e85933f208ae434bb0a446d3e5b75471da7a120dcf349ef2779f31",
        "region": "VRAM",
    },
    {
        "name": "part5",
        "start": 0x6EBC,
        "size": 0xD88,
        "load_addr": 0x0600E000,
        "sha256": "027930d0edc399cc93acce1a21e15be36e34a69b51ceb803686d2ad6c580fee0",
        "region": "VRAM",
    },
    {
        "name": "part6",
        "start": 0x7C44,
        "size": 0x8E0,
        "load_addr": 0x0203E000,
        "sha256": "d3c8c872d123dea0cc39c304a959547257351eb2687d9c1c9b85d5d422938514",
        "region": "EWRAM",
    },
]


def decompress_payload_stream(rom: bytes, offset: int, target_size: int) -> bytes:
    """
    Decompresses the custom LZ stream used by the NES payload.
    Flag format: 8 bits, read MSB first using (flag << 24) | 1 marker.
    Bit = 1: 1 uncompressed literal byte.
    Bit = 0: compressed LZ reference.
      b0, b1: dist = (((b0 & 0x0F) << 8) | b1) + 1
      if b0 >= 0x10: len = (b0 >> 4) + 2
      else: b2 = next_byte(), len = b2 + 18
    """
    out = bytearray()
    cursor = offset
    r3 = 0

    while len(out) < target_size:
        if (r3 & 0x100) != 0 or r3 == 0:
            if cursor >= len(rom):
                raise ValueError("Unexpected EOF in compressed stream")
            flag = rom[cursor]
            r3 = (flag << 24) | 1
            cursor += 1

        bit = (r3 >> 31) & 1
        r3 = (r3 << 1) & 0xFFFFFFFF

        if bit == 1:
            out.append(rom[cursor])
            cursor += 1
        else:
            b0 = rom[cursor]
            b1 = rom[cursor + 1]
            cursor += 2
            dist = (((b0 & 0x0F) << 8) | b1) + 1
            if b0 >= 0x10:
                length = (b0 >> 4) + 2
            else:
                b2 = rom[cursor]
                cursor += 1
                length = b2 + 18

            if dist > len(out):
                raise ValueError(f"LZ back-reference distance {dist} exceeds decompressed length {len(out)}")

            for _ in range(length):
                out.append(out[-dist])

    return bytes(out[:target_size])


def main():
    parser = argparse.ArgumentParser(description="Extract NES emulator parts from MZM USA ROM")
    parser.add_argument("rom", help="Path to Metroid Zero Mission USA ROM (.gba)")
    parser.add_argument("--out-dir", default=".local/nes-emulator", help="Output directory for parts")
    parser.add_argument("--verify-oracle-dir", default=None, help="Directory containing guest oracle dumps to verify")
    args = parser.parse_args()

    with open(args.rom, "rb") as f:
        rom = f.read()

    sha1 = hashlib.sha1(rom).hexdigest()
    if sha1 != EXPECTED_ROM_SHA1:
        print(f"ERROR: ROM SHA-1 mismatch: expected {EXPECTED_ROM_SHA1}, got {sha1}")
        sys.exit(1)

    print(f"Verified ROM SHA-1: {sha1}")
    decompressed = decompress_payload_stream(rom, STREAM_ROM_OFFSET, DECOMPRESSED_TOTAL_SIZE)
    print(f"Decompressed {len(decompressed)} bytes (0x{len(decompressed):X}) from stream at 0x{STREAM_ROM_OFFSET:X}")

    os.makedirs(args.out_dir, exist_ok=True)

    for spec in PARTS_SPEC:
        name = spec["name"]
        start = spec["start"]
        size = spec["size"]
        expected_sha = spec["sha256"]
        load_addr = spec["load_addr"]

        data = decompressed[start:start + size]
        actual_sha = hashlib.sha256(data).hexdigest()
        if actual_sha != expected_sha:
            print(f"ERROR: {name} SHA-256 mismatch! Expected {expected_sha}, got {actual_sha}")
            sys.exit(1)

        out_path = os.path.join(args.out_dir, f"{name}.bin")
        with open(out_path, "wb") as f:
            f.write(data)

        print(f"  {name}: {size} bytes [0x{load_addr:08X}..0x{load_addr+size:08X}] -> {out_path} (SHA-256: {actual_sha[:16]}...)")

        if args.verify_oracle_dir:
            oracle_path = os.path.join(args.verify_oracle_dir, f"guest_{name}.bin")
            if os.path.exists(oracle_path):
                with open(oracle_path, "rb") as of:
                    oracle_data = of.read()
                if oracle_data != data:
                    print(f"ERROR: {name} differs from guest oracle dump at {oracle_path}!")
                    sys.exit(1)
                print(f"    Verified 100% byte match against guest oracle: {oracle_path}")

    print("All 6 parts successfully extracted and verified.")


if __name__ == "__main__":
    main()
