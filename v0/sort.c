/*
 * SORTING NETWORK
 *
 * batcher odd-even mergesort from wikipedia TODO: put url, check license
 * pairwise sorting network: n(log n)(log n - 1)/4 + n - 1,  (log n)(log n + 1)/2
 * 2025-02-05, dmitri bortoq, mit license
 */
#include <time.h>
#include "space.h"

#define HELP "usage: sort [command]\ncommands:\n\tbld\tbuild and print sorter to stdout\n\ttest\tread and test sorter from stdin\n"

/* generate sorter */
static void pairs_ctor()
{
  uint64_t t = LOG2(SPACE_SIZE),
           p = SPACE_SIZE * t * (t - 1) / 4 + SPACE_SIZE - 1,
           c = 0,
           i;
  for(p = 1u << (t - 1); p > 0; p /= 2)
  {
    uint64_t q = 1u << (t - 1);
    uint64_t r = 0;
    uint64_t d = p;

    while(d > 0)
    {
      for(i = 0; i < SPACE_SIZE - d; ++i)
        if((i & p) == r)
        {
          ++c;  /* new pair */
          printf("%lu %lu\n", i, i + d);
        }

      d = q - p;
      q /= 2;
      r = p;
    }
  }
}

/* return 1 if i already exists in layer */
int last(size i)
{
  static char used[SPACE_SIZE];
  if(used[i] == 0)
  {
    used[i] = 1;
    return 0;
  }
  else
  {
    memset(used, 0, SPACE_SIZE);
    return 1;
  }
}

/* deserialize text and return binary sorter. NOTE: calloc() */
size *deserialize()
{
#define L (LOG2(SPACE_SIZE) * (LOG2(SPACE_SIZE) + 1) / 2)
#define E(C, M, R) if(C) {fprintf(stderr, M); exit(R);}
  size  l = 0,
        n = 0,
        p0,
        p1,
        (*sorter)[SPACE_SIZE][2] = calloc(L, sizeof*sorter);

  E(sorter == NULL, "cannot allocate sorter space", 1);

  for(;;)
  {
    scanf("%hu%hu", &p0, &p1);
    if(feof(stdin))
      break;

    if(last(p0) || last(p1))
    {
      n = 0;
      ++l;
      E(l >= L, "number of layer of sorter is too big", 1);
    }

    sorter[l][n][0] = p0;
    sorter[l][n][1] = p1;
    ++n;
    E(n >= SPACE_SIZE, "number of pairs of layer is too big", 1);
  }
  return (void*)sorter;
}

/* compare and swap positions values */
static inline void cmp_swap(size *data, size *s, size l, size n)
{
  size (*sorter)[SPACE_SIZE][2] = (void*)s;
  if(data[sorter[l][n][0]] > data[sorter[l][n][1]])
  {
    size a = data[sorter[l][n][0]];
    size b = data[sorter[l][n][1]];
    data[sorter[l][n][1]] = a;
    data[sorter[l][n][0]] = b;
  }
}

int test(size *s)
{
  size  l,
        n,
        *data = malloc(SPACE_SIZE * sizeof *data),
        (*sorter)[SPACE_SIZE][2] = (void*)s;

  /* generate data */
  srand(time(NULL));
  for(n = 0; n < SPACE_SIZE; ++n)
    data[n] = (size)rand();
  fprintf(stderr, "data generated\n");

  /* sort it */
  for(l = 0; sorter[l][0][1]; ++l)
    for(n = 0; sorter[l][n][1]; ++n)
      if(sorter[l][n][0] != sorter[l][n][1])  // skip (0, 0) pairs
        cmp_swap(data, s, l, n);
  fprintf(stderr, "data sorted\n");

  /* test it */
  for(n = 1; n < SPACE_SIZE; ++n)
    if(data[n - 1] > data[n])
      break;
  fprintf(stderr, "%s\n", n < SPACE_SIZE ? "sort failed" : "sort OK");

  free(data);
  free(sorter);
  return n < SPACE_SIZE;
}

int main(int argc, char **argv)
{
  switch(--argc ? *(int*)argv[1] : 0)
  {
    case /* test */ 0x74736574: return test(deserialize()); break;
    case /* bld  */ 0x646C62: pairs_ctor(); break;
    default: printf(HELP);
  }
  return 0;
}
