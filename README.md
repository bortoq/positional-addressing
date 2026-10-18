# vm1 — Positional Addressing (Demo 2)

`vm1` models parallel copies between bit positions using comparison and
copying of positional keys, without arithmetic on addresses in the mechanism.

- **Instruction:** `[dst_low, dst_high, src_low, src_high]`, specifying two
  equal-length half-open ranges `[low, high)`.
- **Cycle:** classify half-open ranges, then stably order complete data records.
  The pairwise network itself need not be stable: two strict comparisons and an
  existing scratch flag implement each lexicographic data comparator.
- **Network:** 65,520 positions, 3,997,039 comparator pairs, 136 layers per pass.
- **State:** the same five 16-bit key fields and three bit fields per position.
  Phase 1 decodes slots independently and classifies all intervals together.
- **Parallel depth:** **449 network layers + 5 local steps = 454** at the
  default size, independent of active copies. Two phases use one marking sort,
  monotone interval splitting, and two comparator rounds per data-network layer.
  The same cells, five key words, three flags and graph links are reused;
  C simulates their work sequentially. The configured processor has 1–128 slots.

The former third phase repaired order lost by the nonstable network. Explicit
source-origin tie breaking now makes that repair unnecessary; the old two-phase
implementation lacked this guarantee. See the
[explanation and algorithm](doc/README.md#why-three-phases-were-restored),
[current verification](doc/audit.md), and [hardware limits](doc/hardware.md).
The functional tests pass; physical transistor/energy equivalence still needs RTL.
The [ordering experiment](doc/ordering.md) measures a 12–15% reduction in host
C-model time for the chosen kernel; FPGA speed and energy are not inferred.

This repo is the addressing demo. The address space and future channel
protocol are separate work. The current byte-channel loop and LDR1 framing
remain supported. Old open-interval `.vm1`/LDR1 programs are incompatible:
recompile their copy3 sources for the new half-open format.

## Build and run

```sh
make all
make sorter
./tools/compile --in-copy3 examples/echo.asm --out-vm1 /tmp/echo.vm1
./tools/loader --in-prog /tmp/echo.vm1 --out /tmp/prog.ldrpkt
cat /tmp/prog.ldrpkt input.bin | ./vm1 sorter.txt > out.bin
make check
make check-addressing
make check-parallel
```

The legacy loop reads one byte per logical cycle and writes the previously
committed output byte: direct echo starts with zero, followed by preceding
input bytes. EOF or `LIFE` ends the run; pending results are not drained at EOF.

## Layout

- `src/machine.c/.h`: two-phase addressing, packed memory and channel loop.
- `src/network.c`: pairwise network generator; `src/loader.h`: loader words.
- `tools/`: C compiler from copy3 and LDR1 packer.
- `tests/`: units, exact channel checks and independent simultaneous-copy oracle and parallel schedule checks.
- `doc/`: [algorithm](doc/README.md), [hardware](doc/hardware.md),
  [roadmap](doc/roadmap.md) and [audit](doc/audit.md).
- `examples/`: echo; `sort256_runtime.vm1` remains an explicit unfinished stub.

License: MIT — Dmitri Bortoq 2024-2026.
