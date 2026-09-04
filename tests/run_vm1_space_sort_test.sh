#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"

cc -Wall -Wextra -std=c11 -O2 "$ROOT/vm1.c" -o "$ROOT/vm1"
python3 "$ROOT/tests/test_vm1_space_sort.py" --verbose
