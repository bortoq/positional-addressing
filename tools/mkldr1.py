#!/usr/bin/env python3
"""
Build an LDR1 stdin stream for vm1.

Input format (text):
  - one instruction per line
  - 4 unsigned 16-bit integers per line:
      dst_low dst_high src_low src_high
  - blank lines and '#' comments are ignored

Output format (binary):
  magic "LDR1"
  u16le word_count
  u16le dst_word_base
  payload words (u16le)
  optional raw tail bytes (for CHANNEL_IN runtime input)
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


def parse_words(path: Path) -> list[int]:
    words: list[int] = []
    for ln, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        s = raw.split("#", 1)[0].strip()
        if not s:
            continue
        parts = s.split()
        if len(parts) != 4:
            raise ValueError(f"{path}:{ln}: expected 4 integers, got {len(parts)}")
        vals = [int(p, 0) for p in parts]
        for v in vals:
            if v < 0 or v > 0xFFFF:
                raise ValueError(f"{path}:{ln}: value out of uint16 range: {v}")
        words.extend(vals)
    return words


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--in-prog", required=True, help="text vm1 instruction file")
    ap.add_argument("--out", required=True, help="output binary stream")
    ap.add_argument("--dst-word-base", type=int, default=0, help="destination word base in space")
    ap.add_argument("--tail-bytes", default=None, help="optional file appended after payload")
    args = ap.parse_args()

    in_prog = Path(args.in_prog)
    out = Path(args.out)
    if args.dst_word_base < 0 or args.dst_word_base > 0xFFFF:
        raise SystemExit("dst-word-base must be uint16")

    words = parse_words(in_prog)
    if len(words) > 0xFFFF:
        raise SystemExit("word_count exceeds uint16")

    blob = bytearray()
    blob += b"LDR1"
    blob += struct.pack("<H", len(words))
    blob += struct.pack("<H", args.dst_word_base)
    for w in words:
        blob += struct.pack("<H", w)

    if args.tail_bytes:
        blob += Path(args.tail_bytes).read_bytes()

    out.write_bytes(blob)
    print(f"wrote {out} bytes={len(blob)} words={len(words)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

