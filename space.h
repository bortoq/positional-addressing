/*
 * SORTING NETWORK BUILD SETTINGS
 *
 * Shared by sort.c. The address-space emulator itself is defined in vm1.h.
 */
#ifndef SPACE_H_
#define SPACE_H_

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#define LOG2(N) (sizeof(N) * CHAR_BIT - _BitScanReverse64(N))
#elif defined(__GNUC__) || defined(__GNUG__)
#define LOG2(N) (sizeof(N) * CHAR_BIT - __builtin_clz(N))
#endif

#ifndef SPACE_SIZE
#define SPACE_SIZE 0xfff0
#endif

typedef uint16_t size;

#endif /* SPACE_H_ */
