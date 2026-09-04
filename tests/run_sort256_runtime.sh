#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
SORTER="${ROOT}/sorter.txt"

cc -Wall -Wextra -std=c11 -O2 "$ROOT/vm1.c" -o "$ROOT/vm1"
python3 "$ROOT/tools/gen_pairwise_sorter.py" --out "$SORTER"
python3 "$ROOT/tools/mkldr1.py" \
  --in-prog "$ROOT/programs/sort256_runtime.vm1" \
  --out "$ROOT/tmp_sort256.ldrpkt" \
  --dst-word-base 0

python3 - <<'PY' "$ROOT/tmp_sort256.ldrpkt" "$ROOT/tmp_input256.bin"
import sys
from pathlib import Path

pkt = Path(sys.argv[1]).read_bytes()
# deterministic unsorted payload for runtime channel input
tail = bytes(((i * 73 + 19) ^ (255 - i)) & 0xFF for i in range(256))
Path(sys.argv[1]).write_bytes(pkt + tail)
Path(sys.argv[2]).write_bytes(tail)
PY

"$ROOT/vm1" "$SORTER" < "$ROOT/tmp_sort256.ldrpkt" > "$ROOT/tmp_sort256.out"
echo "Wrote $ROOT/tmp_sort256.out"
