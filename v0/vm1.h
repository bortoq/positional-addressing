/*
 * PARALLEL ADDRESS SPACE DEFINITIONS
 *
 * 2025-02-05, dmitri bortoq (bortoq@gmail.com), mit license
 */
#ifndef VM1_H_
#define VM1_H_

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

/* constants */
#define EDGES 2             /* number of channel ends: argument, result */
#define WAYS 2              /* number of space directions: lesser, greater */
#define SPACE_SIZE 0xfff0   /* number of positions in space */
#define PROCESSOR_N 128     /* number of instructions in processor (> 1) */
#define INDEX_SIZE LOG2(SPACE_SIZE)
#define LAYER_N (LOG2(SPACE_SIZE) * (LOG2(SPACE_SIZE) + 1) / 2)
#define CHANNEL_IN  (SPACE_SIZE/CHAR_BIT - 1)
#define CHANNEL_OUT (SPACE_SIZE/CHAR_BIT - 2)
#define HELP "command line:\nvm1 [sorter.txt] [</dev/stdin >/dev/stdout]"
uint64_t loader = 0x3fc01fff2000003f; // src_high src_low dst_high dst_low
uint64_t echo = 0xFFF0FFE7FFE8FFDF; // CHANNEL_IN CHANNEL_OUT

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
  data space[SPACE_SIZE/CHAR_BIT];
  struct {                  /* construction of space position */
      data  value;          /* data value from space */
      bit   copy;           /* participation in copying */
      bit   way;            /* direction of copy (src or dst) */
      idx   low;            /* INSTRUCTION: low bound of src/dst index */
      idx   high;           /* INSTRUCTION: high bound of src/dst index */
      idx   mark;           /* INSTRUCTION: low bound of dst/src index */
      idx   swap;           /* INSTRUCTION: buffer */
      idx   index;          /* INVARIANT: position index */
      idx   link[LAYER_N];  /* CONST: links to other positions. sorter */
      bit   link0[LAYER_N]; /* CONST: time optimization. 0 means "no link". sorter */
  } subspace[SPACE_SIZE];
} machine;                  /* machine definition */

#endif /* VM1_H_ */
