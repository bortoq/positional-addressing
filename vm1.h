/*
 * PARALLEL ADDRESS SPACE DEFINITIONS
 *
 * 2025-02-05, dmitri bortoq (bortoq@gmail.com), mit license
 */
#ifndef VM1_H_
#define VM1_H_

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* constants */
#define EDGES 2             /* number of channel ends: argument, result */
#define WAYS 2              /* number of space directions: lesser, greater */
#ifndef SPACE_SIZE
#define SPACE_SIZE 0xfff0   /* number of positions in space */
#endif
#ifndef PROCESSOR_N
#define PROCESSOR_N 128     /* number of instructions in processor (> 1) */
#endif
#ifndef LIFE
#define LIFE 0x10           /* maximum number of emulator compute cycles */
#endif
#define INDEX_SIZE LOG2(SPACE_SIZE)
#define LAYER_N (LOG2(SPACE_SIZE) * (LOG2(SPACE_SIZE) + 1) / 2)
#define CHANNEL_IN  (SPACE_SIZE / CHAR_BIT - 1)
#define CHANNEL_OUT (SPACE_SIZE / CHAR_BIT - 2)
#define CHANNEL_IN_LO  (CHANNEL_IN * CHAR_BIT)
#define CHANNEL_IN_HI  (CHANNEL_IN_LO + CHAR_BIT)
#define CHANNEL_OUT_LO (CHANNEL_OUT * CHAR_BIT)
#define CHANNEL_OUT_HI (CHANNEL_OUT_LO + CHAR_BIT)
#define TEST_BUF0 0x4000
#define TEST_BUF1 (TEST_BUF0 + CHAR_BIT)
#define TEST_CYCLES 2
#define HELP "command line:\nvm1 [sorter.txt] [test] [</dev/stdin >/dev/stdout]"

/* macros */
#if defined(_MSC_VER)       /* returns roundup(log2(n + 1)), n > 0 */
  #define LOG2(N) (sizeof(N) * CHAR_BIT - _BitScanReverse64(N))
#elif defined(__GNUC__) || defined(__GNUG__)
  #define LOG2(N) (sizeof(N) * CHAR_BIT - __builtin_clz(N))
#endif
#define ARR_SZ(A) (sizeof(A) / sizeof *(A))
#define GETBIT(SRC, OFFSET) (1u & ((SRC) >> (OFFSET)))
#define SETBIT(DST, OFFSET, SRC) DST ^= (-(SRC) ^ (DST)) & (1u << (OFFSET))
#define E(C, M) if(C) {fprintf(stderr, M); exit(__LINE__);}

/* types */
typedef uint8_t   bit;      /* large enough to hold 1 bit */
typedef bit       data;     /* data type */
typedef uint16_t  idx;      /* large enough to hold index */

typedef struct {
  idx a;
  idx b;
} pair;

typedef struct {
  pair  *pairs;
  size_t count;
  size_t cap;
} layer;

typedef struct {
  layer  *layers;
  size_t count;
  size_t cap;
} network;

typedef struct {
  idx low;                 /* lower interval bound */
  idx high;                /* upper interval bound */
  idx partner;             /* paired interval lower bound */
  idx sort;                /* sortable temporary key */
  idx origin;              /* invariant position index */
} position_key;

typedef struct {
  data space[SPACE_SIZE / CHAR_BIT];
  struct {                  /* construction of space position */
    data value;             /* data value from space */
    bit  active;            /* participation in copying */
    bit  role;              /* direction of copy (src or dst) */
    position_key key;       /* lexicographic addressing key */
  } subspace[SPACE_SIZE];
  network fabric;           /* CONST: sorting network links */
} machine;                  /* machine definition */

#endif /* VM1_H_ */
