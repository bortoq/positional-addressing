/*
 * vm1 — positional addressing definitions
 *
 * Demo 2: 2-phase addressing via stable pairwise sorting network.
 * Interval = open (low,high) -> bits [low+1, high-1] copied.
 * Only operation on keys is '>' compare.
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

// how many channel ends per instruction (dst + src)
#define EDGES 2
// directions per edge (low/high)
#define WAYS 2

#ifndef SPACE_SIZE
#define SPACE_SIZE 0xfff0        // number of bit positions (65,520)
#endif
#ifndef PROCESSOR_N
#define PROCESSOR_N 128          // instruction slots in processor area
#endif
#ifndef LIFE
#define LIFE 0x10                // max compute cycles per run
#endif

/* --- derived --- */

// ceil(log2(n)) for n>0 — used for INDEX_SIZE / LAYER_N estimates
#if defined(_MSC_VER)
  #define LOG2(N) (int)(sizeof(N) * CHAR_BIT - __builtin_clz((unsigned)(N)))
#elif defined(__GNUC__) || defined(__GNUG__)
  #define LOG2(N) (int)(sizeof(N) * CHAR_BIT - __builtin_clz((unsigned)(N)))
#else
  #define LOG2(N) (32 - __builtin_clz((unsigned)(N)))
#endif

#define INDEX_SIZE LOG2(SPACE_SIZE)                          // bits per index
#define LAYER_N    (LOG2(SPACE_SIZE) * (LOG2(SPACE_SIZE)+1)/2) // max layers

#define CHANNEL_IN  (SPACE_SIZE / CHAR_BIT - 1)              // byte offset
#define CHANNEL_OUT (SPACE_SIZE / CHAR_BIT - 2)

#define CHANNEL_IN_LO  (CHANNEL_IN  * CHAR_BIT)
#define CHANNEL_IN_HI  (CHANNEL_IN_LO  + CHAR_BIT)
#define CHANNEL_OUT_LO (CHANNEL_OUT * CHAR_BIT)
#define CHANNEL_OUT_HI (CHANNEL_OUT_LO + CHAR_BIT)

#define HELP "command line:\nvm1 [sorter.txt] [</dev/stdin >/dev/stdout]"

/* --- helpers --- */

#define ARR_SZ(A) (sizeof(A) / sizeof *(A))
#define GETBIT(SRC, OFF) (1u & ((SRC) >> (OFF)))
// branchless bit set: DST[OFF] = SRC (0/1)
#define SETBIT(DST, OFF, SRC) \
    DST ^= (-(int)(SRC) ^ (DST)) & (1u << (OFF))

// fail fast with file:line
#define E(COND, MSG) do { \
    if (COND) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, MSG); exit(1); } \
} while (0)

/* --- types --- */

typedef uint8_t  bit;   // 0/1
typedef bit      data;  // bit in space
typedef uint16_t idx;   // position index (fits SPACE_SIZE)

typedef struct { idx a, b; } pair;          // one comparator

typedef struct {                            // one layer (parallel)
    pair  *pairs;
    size_t count, cap;
} layer;

typedef struct {                            // full network
    layer  *layers;
    size_t count, cap;
} network;

// addressing key per position — see doc/README.md
typedef struct {
    idx low;      // interval low (excluded)
    idx high;     // interval high (excluded)
    idx partner;  // paired interval low
    idx sort;     // current sort key
    idx origin;   // fixed position id
} position_key;

typedef struct {
    data space[SPACE_SIZE / CHAR_BIT];      // packed bits
    struct {
        data value;
        bit  active;   // participates this phase?
        bit  role;     // 0=dst side, 1=src side (owns value)
        position_key key;
    } subspace[SPACE_SIZE];
    network fabric;                         // sorting network
} machine;

#endif // VM1_H_
