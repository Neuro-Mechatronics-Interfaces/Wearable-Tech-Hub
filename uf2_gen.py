#!/usr/bin/env python3
"""
Minimal .bin → .uf2 converter for Raspberry Pi microcontrollers.

Usage:
    uf2_gen.py <input.bin> <output.uf2> <flash_start_hex> <family_id_hex>

Example (Pico 2 W, RP2350-ARM-S):
    uf2_gen.py mudra_hub.bin mudra_hub.uf2 0x10000000 0xe48bff59

UF2 block layout (512 bytes each):
    Offset  Size  Description
    0       4     Magic 0x0A324655 ("UF2\n")
    4       4     Magic 0x9E5D5157
    8       4     Flags  (0x00002000 = family ID present)
    12      4     Target address in flash
    16      4     Payload size (always 256)
    20      4     Sequential block number
    24      4     Total block count
    28      4     Family ID
    32      256   Payload data
    288     220   Zero padding
    508     4     End magic 0x0AB16F30

See https://github.com/microsoft/uf2 for the full spec.
"""

import struct
import sys

MAGIC_START0 = 0x0A324655
MAGIC_START1 = 0x9E5D5157
MAGIC_END    = 0x0AB16F30
FLAG_FAMILY  = 0x00002000
BLOCK_DATA   = 256
BLOCK_TOTAL  = 512
PADDING      = BLOCK_TOTAL - 32 - BLOCK_DATA - 4  # 220 bytes


def bin_to_uf2(bin_path: str, uf2_path: str, flash_start: int, family_id: int) -> None:
    with open(bin_path, "rb") as f:
        data = f.read()

    # Pad binary to a multiple of BLOCK_DATA
    remainder = len(data) % BLOCK_DATA
    if remainder:
        data += b"\x00" * (BLOCK_DATA - remainder)

    num_blocks = len(data) // BLOCK_DATA

    with open(uf2_path, "wb") as f:
        for i in range(num_blocks):
            chunk = data[i * BLOCK_DATA : (i + 1) * BLOCK_DATA]
            header = struct.pack(
                "<IIIIIIII",
                MAGIC_START0,
                MAGIC_START1,
                FLAG_FAMILY,
                flash_start + i * BLOCK_DATA,
                BLOCK_DATA,
                i,
                num_blocks,
                family_id,
            )
            block = header + chunk + b"\x00" * PADDING + struct.pack("<I", MAGIC_END)
            assert len(block) == BLOCK_TOTAL
            f.write(block)

    print(
        f"[uf2_gen] {uf2_path}  "
        f"({num_blocks} blocks, {len(data)} bytes, family 0x{family_id:08x})"
    )


if __name__ == "__main__":
    if len(sys.argv) != 5:
        print("Usage: uf2_gen.py <input.bin> <output.uf2> <flash_start_hex> <family_id_hex>")
        sys.exit(1)

    try:
        flash_start = int(sys.argv[3], 16)
        family_id   = int(sys.argv[4], 16)
    except ValueError as e:
        print(f"Error parsing arguments: {e}")
        sys.exit(1)

    bin_to_uf2(sys.argv[1], sys.argv[2], flash_start, family_id)
