#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""
Generate a minimal valid SMEM v11 image for GLINK testing.

The image contains:
  - SMEM header with initialized=1, version[7]=0x000B0000 (v11)
  - TOC entries for items 478 (descriptor), 479 (FIFO 0), 480 (FIFO 1)
  - Pre-allocated data regions for each item

Usage:
    python3 gen_smem_image.py [output_path] [size_in_bytes]

Default output: /tmp/smem.bin
Default size:   2097152 (2 MiB)
"""

import struct
import sys

# SMEM constants
SMEM_ITEM_COUNT = 512

# Item IDs
SMEM_GLINK_DESCRIPTOR = 478
SMEM_GLINK_FIFO_0 = 479
SMEM_GLINK_FIFO_1 = 480

# Data layout within the SMEM region (after header)
DESCRIPTOR_OFFSET = 0x4000  # Well past the header
DESCRIPTOR_SIZE = 32
FIFO_0_OFFSET = 0x5000
FIFO_0_SIZE = 16384  # 16K
FIFO_1_OFFSET = 0x9000
FIFO_1_SIZE = 16384  # 16K
FREE_OFFSET = 0xD000


def le32(val):
    return struct.pack('<I', val & 0xFFFFFFFF)


def build_smem_image(total_size):
    """Build a minimal SMEM v11 image."""
    img = bytearray(total_size)

    # --- SMEM Header ---
    # struct smem_proc_comm proc_comm[4]: 4 * 16 = 64 bytes at offset 0
    # (leave as zeros)

    # __le32 version[32]: 32 * 4 = 128 bytes at offset 64
    version_offset = 64
    # version[7] = 0x000B0000 (SMEM v11)
    struct.pack_into('<I', img, version_offset + 7 * 4, 0x000B0000)

    # __le32 initialized: at offset 64 + 128 = 192
    struct.pack_into('<I', img, 192, 1)

    # __le32 free_offset: at offset 196
    struct.pack_into('<I', img, 196, FREE_OFFSET)

    # __le32 available: at offset 200
    struct.pack_into('<I', img, 200, total_size - FREE_OFFSET)

    # __le32 reserved: at offset 204 (leave as 0)

    # --- TOC entries ---
    # struct smem_global_entry toc[512] starts at offset 208 (0xD0)
    # Each entry: allocated(4) + offset(4) + size(4) + aux_base(4) = 16 bytes
    toc_base = 208

    def set_toc_entry(item_id, offset, size):
        entry_offset = toc_base + item_id * 16
        struct.pack_into('<I', img, entry_offset + 0, 1)       # allocated = 1
        struct.pack_into('<I', img, entry_offset + 4, offset)   # offset
        struct.pack_into('<I', img, entry_offset + 8, size)     # size
        # aux_base = 0 (default region)

    set_toc_entry(SMEM_GLINK_DESCRIPTOR, DESCRIPTOR_OFFSET, DESCRIPTOR_SIZE)
    set_toc_entry(SMEM_GLINK_FIFO_0, FIFO_0_OFFSET, FIFO_0_SIZE)
    set_toc_entry(SMEM_GLINK_FIFO_1, FIFO_1_OFFSET, FIFO_1_SIZE)

    # Data regions are already zeroed (descriptor pointers start at 0,
    # FIFOs are empty with head=tail=0)

    return bytes(img)


def main():
    output_path = sys.argv[1] if len(sys.argv) > 1 else '/tmp/smem.bin'
    total_size = int(sys.argv[2]) if len(sys.argv) > 2 else 2 * 1024 * 1024

    img = build_smem_image(total_size)

    with open(output_path, 'wb') as f:
        f.write(img)

    print(f'Generated SMEM image: {output_path} ({total_size} bytes)')
    print(f'  Descriptor (item 478): offset=0x{DESCRIPTOR_OFFSET:x}, '
          f'size={DESCRIPTOR_SIZE}')
    print(f'  FIFO 0 (item 479):     offset=0x{FIFO_0_OFFSET:x}, '
          f'size={FIFO_0_SIZE}')
    print(f'  FIFO 1 (item 480):     offset=0x{FIFO_1_OFFSET:x}, '
          f'size={FIFO_1_SIZE}')
    print(f'  Free offset:           0x{FREE_OFFSET:x}')


if __name__ == '__main__':
    main()
