# Hardware notes — VM1 positional addressing

`src/machine.c` is the behavioral model of three-phase addressing. It is not
a direct C-to-HDL design. The pairwise network and state cells are reused;
phase 1 streams descriptors rather than addressing all intervals in one pass.
See [the algorithm and reasons for restoring phase 3](README.md).

## State and ROM budget

Per position, unchanged from the preceding model:

```text
value(1) + active(1) + role(1) + five 16-bit key words = 83 logical bits
```

For 65520 positions: 5,438,160 subspace bits plus 65,520 packed memory bits.
`origin` is a fixed positional label; a physical implementation may wire it
as a constant. C struct padding is not a physical cell count.
The current generated network has **3,997,039 actual pairs**, with 136 layers.
Blank separator lines are not comparator pairs. At two 16-bit endpoints per
pair, a literal network ROM would hold 127,905,248 bits. The same ROM is
reused in all passes; no copies are added for phase 3 or streamed intervals.

Using the old illustrative 6-transistor storage-bit convention gives
33,022,080 transistors without ROM or 800,453,568 with literal ROM. These
numbers cover storage only, not a synthesized controller, muxes, clocking,
buffers or comparison logic. They correct the preceding inconsistent ROM
pair count and are not a silicon estimate.

## Reuse across phases

| Field | Classification | Transpose / restore |
|---|---|---|
| `low/high` | Current broadcast bounds | Available scratch words |
| `partner` | Paired lower bound | Moving original position |
| `sort` | Retained position/group classification | Primary group key |
| `origin` | Fixed position label | Fixed position label |
| `value` | One-bit descriptor direction | Actual data bit |
| `active` | Broadcast token has arrived | Available scratch bit |
| `role` | Position belongs to a source interval | Record must be committed |

The bit data stays in `space` until phase 2 starts, so token direction needs
no additional state bit. Transporting a token never transports or clears an
already retained `role/sort` classification. Phase 2 swaps complete data
records. Phase 3 uses the already existing transported `partner` word to
restore source order; it does not append origin to a wider comparison key.
C swap temporaries describe old combinational inputs, not mandatory new
register cells. Existing scratch fields are available for serialized swaps.

Classification uses two boundary comparisons when a token arrives. Phase 2
uses one strict primary-key comparison per pair. Phase 3 can use two strict
comparisons to test group-key equality and a third to order origins. These
operations can time-share the preceding pair-unit comparator budget instead
of instantiating new comparison cells. A concrete mux/control implementation
and its timing must still be designed and checked in RTL.

Strict and non-strict magnitude comparison can share identical bit cells:
from low bit to high bit, use `r_next = (A_i == B_i) ? r : A_i`; initialize
`r=0` for `A>B`, or `r=1` for `A>=B`. This changes a constant connection,
not a new inverter or equality comparator. It demonstrates feasibility at
the comparator level, not unchanged transistor count for a complete datapath.

## Timing and energy tradeoff

The owner explicitly accepted additional work within classification without
new position cells (2026-10-18). For K active copies:

```text
2K classification broadcasts + 1 transpose pass + 1 restore pass = 2K+2 passes
```

Each pass uses the same 136-layer network at the default size. There are also
resets, descriptor injection, loading data and boundary commit operations.
Three algorithmic phases do not mean three hardware clocks or three network
passes. The former two-pass latency estimate is invalid. Additional passes
increase switching activity and can increase energy and time per useful copy;
unchanged state capacity is not an unchanged-energy claim.

## Remaining physical implementation work

Suggested blocks: controller, one network ROM, subspace storage, packed
memory, a reused comparison/copy datapath, and existing byte-channel IO.
The controller must sequence descriptor selection/broadcast, transpose,
restore and commit. Injection/selection logic is global control work, not
an added bank of per-position cells; its physical cost is still unmeasured.

No RTL, cell library, transistor netlist or power model is shipped yet.
Before accepting hardware budgets, compare whole datapaths under identical
technology, loads, buffering and constraints: per-position and per-pair
transistors, controller/ROM cost, area, critical path, leakage and dynamic
energy for matched workloads. Functional tests certify the software model,
not those physical properties. Future optimization must keep the independent
copy oracle and must not assume stability of the existing sorting network.
