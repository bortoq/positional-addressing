#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
SORTER="${ROOT}/sorter.txt"

cc -Wall -Wextra -std=c11 -O2 "$ROOT/src/vm1.c" -o "$ROOT/vm1"
make -C "$ROOT" sorter
python3 "$ROOT/tools/copy3_to_vm1.py" \
  --in-copy3 "$ROOT/programs/echo.copy3" \
  --out-vm1 "$ROOT/tmp_echo.vm1"
python3 "$ROOT/tools/mkldr1.py" \
  --in-prog "$ROOT/tmp_echo.vm1" \
  --out "$ROOT/tmp_echo.ldrpkt" \
  --dst-word-base 0

python3 - <<'PY' "$ROOT/tmp_echo.ldrpkt" "$ROOT/tmp_echo.input"
import sys
from pathlib import Path
payload = bytes([0x41, 0x42, 0x43, 0x44, 0x45])  # ABCDE
pkt = Path(sys.argv[1]).read_bytes()
Path(sys.argv[1]).write_bytes(pkt + payload)
Path(sys.argv[2]).write_bytes(payload)
PY

"$ROOT/vm1" "$SORTER" < "$ROOT/tmp_echo.ldrpkt" > "$ROOT/tmp_echo.out"

echo "input : $(xxd -p -c 256 "$ROOT/tmp_echo.input")"
echo "output: $(xxd -p -c 256 "$ROOT/tmp_echo.out")"
