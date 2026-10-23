#!/usr/bin/env python3
"""Validate an Android sparse image and print its logical block/byte capacity."""

from __future__ import annotations

import argparse
from pathlib import Path
import struct


MAGIC = 0xED26FF3A
RAW = 0xCAC1
FILL = 0xCAC2
DONT_CARE = 0xCAC3
CRC32 = 0xCAC4


def fail(item: Path, message: str) -> None:
    raise SystemExit(f"{item}: {message}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    item = args.image

    with item.open("rb") as stream:
        header = stream.read(28)
        if len(header) != 28:
            fail(item, "truncated sparse header")
        magic, major, _, file_header, chunk_header, block, blocks, chunks, _ = (
            struct.unpack("<IHHHHIIII", header)
        )
        if magic != MAGIC:
            fail(item, f"bad magic {magic:#x}")
        if major != 1:
            fail(item, f"unsupported sparse major {major}")
        if (file_header, chunk_header) != (28, 12):
            fail(item, "unexpected sparse header sizes")
        if block == 0:
            fail(item, "zero sparse block size")

        seen = 0
        for index in range(chunks):
            raw = stream.read(12)
            if len(raw) != 12:
                fail(item, f"truncated chunk {index} header")
            kind, _, count, total_size = struct.unpack("<HHII", raw)
            if count == 0:
                fail(item, f"zero-length chunk {index}")
            if kind in (RAW, FILL):
                payload = count * block
                expected = chunk_header + (4 if kind == FILL else payload)
            elif kind == DONT_CARE:
                payload = count * block
                expected = chunk_header
            elif kind == CRC32:
                payload = 0
                expected = chunk_header + 4
            else:
                fail(item, f"unknown chunk type {kind:#x}")
            if total_size != expected:
                fail(
                    item,
                    f"chunk {index} size {total_size}, expected {expected}",
                )
            remaining = total_size - chunk_header
            while remaining:
                data = stream.read(min(remaining, 8 << 20))
                if not data:
                    fail(item, f"truncated chunk {index} payload")
                remaining -= len(data)
            seen += count

        if seen != blocks:
            fail(item, f"chunks cover {seen} blocks, header says {blocks}")
        if stream.read(1):
            fail(item, "trailing data after final chunk")

    print(f"blocks={blocks}")
    print(f"chunks={chunks}")
    print(f"bytes={blocks * block}")


if __name__ == "__main__":
    main()
