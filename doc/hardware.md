# Hardware notes — VM1 positional addressing

Behavioral spec: `src/vm1.c`. Not intended for direct C→HDL.

## Cost estimate (current vm1, 65,520 positions, 16-bit index)

- 1 memory bit = 6 transistors
- 1 `>` comparator bit = 8 transistors

Per position: `value(1)+active(1)+role(1)+key(5×16=80)=83` bits.
- subspace: 5,438,160 bits
- space: 65,520 bits
- network ROM: `pairs*2*16` bits → 3,984,771×32 = 127,512,672 bits

6T:
- w/o ROM: (5,438,160+65,520)×6 = **33,022,080**
- with ROM: +127,512,672×6 = **798,098,112**

Per pair-unit:
- `stage1_pair` ≤10×16-bit compares ≈1,280 tr,
- `stage2_pair` 1×16-bit ≈128 tr.
Widest layer (32,760 pairs): `stage1≈41.9M`, `stage2≈4.1M`. Dominant is network ROM (depends on `pairs_total`).

## RTL blocks

- `top` → `control` + `network_rom` + `subspace_ram` + `space_mem` + `stage1/2 datapath` + `io`
- `control` FSM: `IDLE, CHANNEL_READ, STAGE1_INIT, STAGE1_RUN, STAGE2_INIT, STAGE2_RUN, CHANNEL_WRITE, DONE`
- `network_rom`: pairs `(a,b)` + layer bounds
- `subspace_ram`: `value/active/role/key`, `space_mem`: packed bits
- `stage1/2 datapath`: `stage1_pair` / `stage2_pair`, `io`: `CHANNEL_IN/OUT`

Suggested path: 1) small model, 2) check `stage1/2` vs C, 3) pipeline by layers, 4) optimize network storage.

*Estimates are illustrative, not from silicon expertise.*
