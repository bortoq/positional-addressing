# Two-phase data ordering: experiment and decision

2026-10-18. The production C model now uses **serial-once**: one 16-bit strict
comparator in two rounds per data-network layer, one existing scratch flag and
at most one record exchange. The separate order-restoration phase is removed.

## Method

`tests/bench_ordering.c` compares the same classification with five data kernels:

- `three-phase`: scalar group sort followed by the former group-order repair.
- `three-phase-direct`: the same reference with direct per-pair calls, controlling
  for indirect-call/inlining effects in the C benchmark.
- `lex`: one compound `(group,origin)` comparison per node.
- `serial-double`: first order complete records by origin, then by group. This
  implements the same two-element ordering using two strict comparators in time,
  but can swap the record out and back.
- `serial-once`: compare origins into `active`, then use a directional strict
  group comparison and exchange the complete record once. Uninstrumented timing
  invokes the actual production cycle, not a copy of that kernel.

Compiler GCC 12.3.0, `-Wall -Wextra -Werror -pedantic -std=c11 -O2`, AMD Ryzen 5
PRO 5650U. N=65520, 128 slots. Process CPU time, network loading and fixture setup
excluded, two warmup cycles, seven rounds of eight measured cycles per fixture
and variant. Variant order is shuffled deterministically. Each fixture is checked
against an independent snapshot-copy oracle before and after timing. No channels
or file I/O occur inside the timed region. These are host C-model measurements,
not FPGA measurements. Thermal/compiler effects and other workloads can change
absolute times and speedups.

## Median milliseconds per complete logical cycle

| Fixture | Three-phase | Direct three-phase | Lex | Serial double | Serial once |
|---|---:|---:|---:|---:|---:|
| NOP | 19.869 | 20.185 | 12.392 | 14.328 | 17.401 |
| singleton | 19.975 | 19.864 | 12.410 | 14.379 | 17.561 |
| long-copy | 20.202 | 20.388 | 12.791 | 15.767 | 17.431 |
| 64-short | 20.060 | 20.077 | 12.406 | 14.607 | 17.515 |
| 128-tail | 19.810 | 19.959 | 12.469 | 14.616 | 17.445 |
| 128-long | 20.315 | 20.532 | 12.909 | 21.592 | 17.661 |
| unaligned-copy | 20.351 | 20.112 | 12.560 | 15.643 | 17.241 |
| 128-unaligned | 20.353 | 20.148 | 12.802 | 18.226 | 17.797 |

Production time decreased by about **12–15%** against these three-phase controls.
Lex is faster on the host, but evaluating a compound comparison in one hardware
step must not be priced as one 16-bit strict comparison without checking its
implementation. Serial-double is rejected: it needlessly moves complete records,
even when the final comparison leaves them in their original order.

## Data-register activity, separate instrumented run

Counts below cover the DATA phase only. One full exchange writes four 16-bit
words across two positions; a former restoration exchange writes two. Source and
write flags are also exchanged but not counted as word writes. Payload bit flips
include words and transported bit flags; scratch-flag flips are separate. These
are changes of modeled register outputs, not measured joules, gate transitions,
clock-tree energy, or a whole-cycle switching trace. Classification/reset can
also see different initial scratch flags between variants.

| Fixture / variant | Word writes | Payload bit flips | Scratch flag flips |
|---|---:|---:|---:|
| singleton / three-phase | 20 | 40 | 0 |
| singleton / lex | 20 | 40 | 0 |
| singleton / serial-double | 964 | 5,976 | 0 |
| singleton / serial-once | 20 | 40 | 214 |
| 128-long / three-phase | 458,752 | 638,272 | 0 |
| 128-long / lex | 458,752 | 638,272 | 0 |
| 128-long / serial-double | 12,453,888 | 45,502,788 | 0 |
| 128-long / serial-once | 458,752 | 638,272 | 589,026 |
| unaligned-copy / three-phase | 212,974 | 921,266 | 0 |
| unaligned-copy / lex | 262,036 | 945,764 | 0 |
| unaligned-copy / serial-double | 2,749,444 | 17,624,064 | 0 |
| unaligned-copy / serial-once | 262,036 | 945,764 | 404,214 |
| 128-unaligned / three-phase | 509,708 | 1,747,610 | 0 |
| 128-unaligned / lex | 565,232 | 1,789,614 | 0 |
| 128-unaligned / serial-double | 6,485,456 | 30,715,518 | 0 |
| 128-unaligned / serial-once | 565,232 | 1,789,614 | 493,906 |

Serial-double raises word writes 27.1 times and payload bit flips 71.3 times on
`128-long`. Serial-once has exactly the lex variant's record exchanges and adds
scratch-flag activity. It is NOT established to save energy: flags switch, and
unaligned fixtures also increase complete-record movement relative to the old
partial repair. An energy/area claim requires RTL and synthesis.

## Comparison depth and resources

For one strict word comparator per pair, let `r = origin_a > origin_b`:

```
swap = r ? !(group_b > group_a) : (group_a > group_b)
```

If groups differ, both choices order groups correctly; if groups are equal,
`swap=r`. This is a lexicographic comparator with one stored bit, two 16-bit
strict comparisons and one final exchange. The bit uses the existing `active`
field, which is scratch after marking. No 32-bit stored key, adder, new state
word, or second comparator is required. Data-dependent input reversal and result
inversion still need control routing; unchanged transistor count is not proved
by this C allocation.

The previous count of 454 treated an entire former restoration node as one
operation, although it tested group equality and origins. It is not a count of
single-comparator hardware clocks. Current production explicitly schedules the
two comparison rounds: 136 marking-sort layers + 272 data comparison layers +
41 splitting/scratch layers + 5 local steps = **454**. One compound comparator
per data node would instead give 318 coarse steps; that is not the selected
single-comparator implementation.

In a strict-only comparator decomposition the old DATA portion can use L group
sort rounds plus 2L repair rounds: after group sorting `group_a<=group_b`, so one
reverse comparison suffices to reject unequal groups, followed by origin comparison.
The new data portion uses 2L rounds. This is a reduction from 408 to 272 comparison
rounds in that specific model, not an FPGA clock/frequency result. Marking is common
and its own word-level operations also need implementation timing.

## Reproduction

```
make tests/bench_ordering tests/bench_ordering_stats
./tests/bench_ordering sorter.txt > /tmp/ordering-time.csv
./tests/bench_ordering_stats sorter.txt > /tmp/ordering-activity.csv
make check-ordering
```

The saved raw measurements are [timings](ordering-benchmark.csv) and
[activity](ordering-activity.csv). `make clean` removes every generated probe
binary. No runtime selector is added to vm1; the alternatives exist only in tests.
The ordinary suite and ASan/UBSan validate the selected production kernel; the
optional oracle target also validates the reference and rejected controls.

## Actual CLI process comparison

A separate experiment launches two ordinary, uninstrumented `vm1` executables:
three-phase sources extracted from commit `9886b1c`, and the current two-phase
working tree. Each executable is built from its own `machine.c`, `machine.h` and
`loader.h`, with identical GCC 12.3.0 strict C11 `-O2` flags and no configuration
overrides. Both use N=65520, 128 instruction slots and the default LIFE=16.
The host is the same Ryzen 5 PRO 5650U; parent and children are pinned to CPU 0.

The measured interval is subprocess launch through exit, including allocation,
loading/parsing the same `sorter.txt`, loading the LDR1 memory image, channel I/O,
all 16 cycles and cleanup. Input/output use OS pipes. The network is warm in the
OS file cache; this is not a cold-storage benchmark. Each variant has one warmup
process. Nine rounds shuffle fixture order and the two variants within each
fixture. The table reports the median of nine complete process measurements.
Child user+system CPU time is recorded separately from elapsed wall time.

Every active fixture copies the input channel to the output channel and adds
zero, one or 127 independent bulk copies. All source/destination half-open ranges
are disjoint, including channel positions. Bulk payloads are deterministic random
bits; processor descriptors remain unchanged across cycles. Fixtures cover a
long unaligned copy, short/long groups, sources near the end of memory and many
unaligned copies. Each process must exit successfully, emit no diagnostics and
produce exactly the expected 16-byte channel output (initial zero followed by
the first 15 input bytes, respecting cycle-boundary channel latency). This checks
observable channel output; it does not inspect the CLI's internal memory. The
independent full-memory oracle checks are described above.

| Fixture | Three-phase wall, ms | Two-phase wall, ms | Elapsed time reduction | Three-phase CPU, ms | Two-phase CPU, ms |
|---|---:|---:|---:|---:|---:|
| echo | 921.411 | 858.236 | 6.86% | 920.054 | 857.000 |
| long | 923.380 | 863.246 | 6.51% | 922.082 | 861.946 |
| 127-short | 926.736 | 866.315 | 6.52% | 925.527 | 864.958 |
| 127-long | 926.054 | 865.589 | 6.53% | 924.620 | 864.368 |
| 127-tail | 924.015 | 867.747 | 6.09% | 922.684 | 866.533 |
| 127-unaligned | 925.793 | 867.352 | 6.31% | 924.488 | 866.132 |
| startup-only | 575.625 | 574.252 | 0.24% | 574.717 | 573.310 |

Thus the actual default CLI run takes **6.1–6.9% less elapsed time**, or roughly
**1.065–1.074 times the speed**, on these fixtures. This is smaller than the
kernel-only improvement because startup alone costs about **0.575 seconds**.
The `startup-only` control loads the same memory packet but supplies no channel
bytes, so it executes zero cycles. Subtracting each variant's startup median
from its 16-cycle median suggests roughly 16–18% lower execution time for this
standalone build; that subtraction is an estimate, not a directly isolated
kernel measurement, and differs from the in-process build above. The direct
complete-process result is the 6.1–6.9% reduction.

All **126 measured processes** and the two warmups passed their output checks.
Saved artifacts: [raw CLI timings](ordering-cli.csv),
[compiler/configuration/source hashes](ordering-cli-metadata.json).
To rebuild both real executables and repeat this measurement:

```
python3 tests/bench_cli.py --rounds 9 --output /tmp/vm1-cli-comparison
```

The optional benchmark requires Python 3, Git and a C compiler on Linux. It
writes its sources, binaries, input packets and results under the selected
`/tmp` directory, leaving the repository build untouched. The old revision can
be selected with `--baseline`; the current implementation always comes from the
working tree. The ordinary C test suite does not depend on this script.
