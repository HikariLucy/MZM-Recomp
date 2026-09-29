#!/usr/bin/env python3
"""
Extracts the decompressed NES ARM payload from Metroid Zero Mission USA ROM
via standard BIOS-compatible LZ77 decompression.
Outputs to .local/nes-payload-usa.bin (532 bytes, SHA-256 e94f6dba...).
"""

import hashlib
import os
import sys

EXPECTED_ROM_SHA1 = "5de8536afe1f0078ee6fe1089f890e8c7aa0a6e8"
EXPECTED_PAYLOAD_SHA256 = "e94f6dba7b7ec0dd183335fa2efdd5bb5a1f4dc1f7593d8e8961b1e2ce681f44"
PAYLOAD_SIZE = 0x214
ROM_STREAM_OFFSET = 0x7D8150


def lz77_decompress(rom: bytes, offset: int, expected_size: int) -> bytes:
    assert offset + 4 <= len(rom)
    assert rom[offset] == 0x10, f"Expected LZ77 header 0x10, got {rom[offset]:#x}"
    size = rom[offset + 1] | (rom[offset + 2] << 8) | (rom[offset + 3] << 16)
    assert size == expected_size, f"Expected size {expected_size}, got {size}"
    cursor = offset + 4
    out = bytearray()
    while len(out) < size:
        flags = rom[cursor]
        cursor += 1
        for bit in range(7, -1, -1):
            if len(out) >= size:
                break
            if (flags & (1 << bit)) == 0:
                out.append(rom[cursor])
                cursor += 1
            else:
                hi = rom[cursor]
                lo = rom[cursor + 1]
                cursor += 2
                length = (hi >> 4) + 3
                dist = ((hi & 0x0F) << 8) | lo + 1
                assert dist <= len(out), f"Invalid back-reference distance {dist}"
                for _ in range(length):
                    if len(out) < size:
                        out.append(out[-dist])
    return bytes(out)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <path_to_mzm_usa.gba> [output_path]")
        sys.exit(1)

    rom_path = sys.argv[1]
    out_path = sys.argv[2] if len(sys.argv) > 2 else ".local/nes-payload-usa.bin"

    with open(rom_path, "rb") as f:
        rom = f.read()

    sha1 = hashlib.sha1(rom).hexdigest()
    if sha1 != EXPECTED_ROM_SHA1:
        print(f"ERROR: ROM SHA-1 {sha1} does not match expected USA ROM ({EXPECTED_ROM_SHA1})")
        sys.exit(1)

    payload = lz77_decompress(rom, ROM_STREAM_OFFSET, PAYLOAD_SIZE)
    sha256 = hashlib.sha256(payload).hexdigest()
    if sha256 != EXPECTED_PAYLOAD_SHA256:
        print(f"ERROR: Payload SHA-256 {sha256} does not match expected ({EXPECTED_PAYLOAD_SHA256})")
        sys.exit(1)

    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(payload)

    print(f"Successfully extracted {len(payload)} bytes to {out_path} (SHA-256: {sha256})")


if __name__ == "__main__":
    main()
