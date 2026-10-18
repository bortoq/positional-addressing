/*
 * vm1 — positional addressing definitions
 *
 * Demo 2: two-phase addressing via a pairwise sorting network.
 * Interval = half-open [low,high) -> bits low through high-1 copied.
 * Boundary predicates use strict/non-strict compares; stage2 uses >.
 *
 * 2025-02-05, dmitri bortoq (bortoq@gmail.com), MIT license
 */
#ifndef VM1_H_
#define VM1_H_

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- configuration --- */

/* how many channel ends per instruction (dst + src) */
#define EDGES 2
/* directions per edge (low/high) */
#define WAYS 2

#ifndef SPACE_SIZE
#define SPACE_SIZE 0xfff0 /* number of bit positions (65,520) */
#endif
#ifndef PROCESSOR_N
#define PROCESSOR_N 128 /* instruction slots in processor area */
#endif
#ifndef LIFE
#define LIFE 0x10 /* max compute cycles per run */
#endif

/* An exclusive endpoint must fit idx without an extra address bit. */
_Static_assert(SPACE_SIZE >= 16 && SPACE_SIZE <= UINT16_MAX, "space endpoint must fit idx");
_Static_assert(SPACE_SIZE % 16 == 0, "space must contain whole idx words");

/* --- derived --- */

/* ceil(log2(n)) for n>0 — used for INDEX_SIZE / LAYER_N estimates */
static inline int ceil_log2_u32(uint32_t n)
{
  if (n <= 1)
    return 0;
  return 32 - __builtin_clz(n - 1);
}
#define CEIL_LOG2(N) ceil_log2_u32((uint32_t)(N))

#define INDEX_SIZE CEIL_LOG2(SPACE_SIZE)                                  /* bits per index */
#define LAYER_N (CEIL_LOG2(SPACE_SIZE) * (CEIL_LOG2(SPACE_SIZE) + 1) / 2) /* max layers */
#define SPACE_WORDS (SPACE_SIZE / CHAR_BIT / (int)sizeof(idx))
#define SPACE_BYTES (SPACE_SIZE / CHAR_BIT)

#define CHANNEL_IN (SPACE_SIZE / CHAR_BIT - 1) /* byte offset */
#define CHANNEL_OUT (SPACE_SIZE / CHAR_BIT - 2)

#define CHANNEL_IN_LO (CHANNEL_IN * CHAR_BIT)
#define CHANNEL_IN_HI (CHANNEL_IN_LO + CHAR_BIT)
#define CHANNEL_OUT_LO (CHANNEL_OUT * CHAR_BIT)
#define CHANNEL_OUT_HI (CHANNEL_OUT_LO + CHAR_BIT)

#define HELP "command line:\nvm1 [sorter.txt] [</dev/stdin >/dev/stdout]"

/* --- helpers --- */

#define ARR_SZ(A) (sizeof(A) / sizeof *(A))
#define GETBIT(SRC, OFF) (1u & ((SRC) >> (OFF)))
static inline void set_bit_u8(uint8_t *dst, unsigned off, unsigned src)
{
  if (src)
    *dst |= (uint8_t)(1u << off);
  else
    *dst &= (uint8_t)~(1u << off);
}
#define SETBIT(DST, OFF, SRC) set_bit_u8(&(DST), (OFF), (SRC))

/* fail fast with file:line */
#define E(COND, MSG)                                                                               \
  do {                                                                                             \
    if (COND) {                                                                                    \
      fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, MSG);                                     \
      exit(1);                                                                                     \
    }                                                                                              \
  } while (0)

/* --- types --- */

typedef uint8_t bit;  /* 0/1 */
typedef bit data;     /* bit in space */
typedef uint16_t idx; /* position index (fits SPACE_SIZE) */

typedef struct {
  idx a, b;
} pair; /* one comparator */

typedef struct { /* one layer (parallel) */
  pair *pairs;
  size_t count, cap;
} layer;

typedef struct { /* full network */
  layer *layers;
  size_t count, cap;
} network;

/* addressing key per position — see doc/README.md */
typedef struct {
  idx low;     /* stage1 primary interval lower bound */
  idx high;    /* stage1 primary interval exclusive upper bound */
  idx partner; /* stage1 paired lower bound; stage2 transported source origin */
  idx sort;    /* stage1 packed ghost bounds, then target group key */
  idx origin;  /* stage1 ghost paired bound; restored physical ID for stage2 */
} position_key;

typedef struct {
  data space[SPACE_SIZE / CHAR_BIT]; /* packed bits */
  struct {
    data value; /* stage1 primary source flag; stage2 actual bit payload */
    bit active; /* stage1 nonempty; stage2 origin-comparison scratch */
    bit role;   /* stage1 ghost source, then position source; stage2 destination to commit */
    position_key key;
  } subspace[SPACE_SIZE];
  network fabric; /* sorting network */
} machine;

#endif /* VM1_H_ */
