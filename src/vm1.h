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

/* Purpose: number of channel ends in one copy instruction (dst and src). */
#define EDGES 2
/* Purpose: number of interval directions in one instruction edge. */
#define WAYS 2
#ifndef SPACE_SIZE
/* Purpose: number of positions in address space. */
#define SPACE_SIZE 0xfff0
#endif
#ifndef PROCESSOR_N
/* Purpose: number of instruction slots in processor area. */
#define PROCESSOR_N 128
#endif
#ifndef LIFE
/* Purpose: upper bound of compute cycles in one run. */
#define LIFE 0x10
#endif
/* Purpose: index width estimate for current address-space size. */
#define INDEX_SIZE LOG2(SPACE_SIZE)
/* Purpose: theoretical upper bound for sorting-network layer count. */
#define LAYER_N (LOG2(SPACE_SIZE) * (LOG2(SPACE_SIZE) + 1) / 2)
/* Purpose: byte offset of input channel in packed address space. */
#define CHANNEL_IN  (SPACE_SIZE / CHAR_BIT - 1)
/* Purpose: byte offset of output channel in packed address space. */
#define CHANNEL_OUT (SPACE_SIZE / CHAR_BIT - 2)
/* Purpose: lower bit index of input channel interval. */
#define CHANNEL_IN_LO  (CHANNEL_IN * CHAR_BIT)
/* Purpose: upper bit index of input channel interval. */
#define CHANNEL_IN_HI  (CHANNEL_IN_LO + CHAR_BIT)
/* Purpose: lower bit index of output channel interval. */
#define CHANNEL_OUT_LO (CHANNEL_OUT * CHAR_BIT)
/* Purpose: upper bit index of output channel interval. */
#define CHANNEL_OUT_HI (CHANNEL_OUT_LO + CHAR_BIT)
/* Purpose: command-line usage string for vm1 executable. */
#define HELP "command line:\nvm1 [sorter.txt] [</dev/stdin >/dev/stdout]"

/* Purpose: compute ceil(log2(n + 1)) for positive integer n. */
#if defined(_MSC_VER)
  #define LOG2(N) (sizeof(N) * CHAR_BIT - _BitScanReverse64(N))
#elif defined(__GNUC__) || defined(__GNUG__)
  #define LOG2(N) (sizeof(N) * CHAR_BIT - __builtin_clz(N))
#endif
/* Purpose: get compile-time element count of static array. */
#define ARR_SZ(A) (sizeof(A) / sizeof *(A))
/* Purpose: read one bit from integer source by bit offset. */
#define GETBIT(SRC, OFFSET) (1u & ((SRC) >> (OFFSET)))
/* Purpose: write one bit into integer destination by bit offset. */
#define SETBIT(DST, OFFSET, SRC) DST ^= (-(SRC) ^ (DST)) & (1u << (OFFSET))
/* Purpose: fail fast on fatal condition with message and source line. */
#define E(C, M) if(C) {fprintf(stderr, M); exit(__LINE__);}

/* Purpose: single-bit storage type in emulator state. */
typedef uint8_t   bit;
/* Purpose: data-bit type in address space. */
typedef bit       data;
/* Purpose: address-space index type. */
typedef uint16_t  idx;

/* Purpose: one comparator pair in sorting network layer. */
typedef struct {
  idx a;
  idx b;
} pair;

/* Purpose: one sorting-network layer with many comparator pairs. */
typedef struct {
  pair  *pairs;
  size_t count;
  size_t cap;
} layer;

/* Purpose: full sorting network as ordered array of layers. */
typedef struct {
  layer  *layers;
  size_t count;
  size_t cap;
} network;

/* Purpose: lexicographic addressing key of one address-space position. */
typedef struct {
  idx low;                 /* Purpose: lower bound of current interval marker. */
  idx high;                /* Purpose: upper bound of current interval marker. */
  idx partner;             /* Purpose: lower bound of paired interval marker. */
  idx sort;                /* Purpose: temporary sort key for network compare. */
  idx origin;              /* Purpose: invariant origin index of this position. */
} position_key;

/* Purpose: complete vm1 machine state: address space, subspace, and network. */
typedef struct {
  /* Purpose: packed bit array of full address space. */
  data space[SPACE_SIZE / CHAR_BIT];
  struct {
    data value;             /* Purpose: current data bit for this position. */
    bit  active;            /* Purpose: participation flag in current stage. */
    bit  role;              /* Purpose: copy role, source or destination side. */
    position_key key;       /* Purpose: lexicographic key fields for position. */
  } subspace[SPACE_SIZE];
  network fabric;           /* Purpose: loaded sorting network links by layers. */
} machine;

#endif /* VM1_H_ */
