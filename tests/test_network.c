/* test_network56 - validate 256-element sorting with network (C port) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SORTER "sorter.txt"

static int load_and_test(const char *path)
{
  FILE *f = fopen(path, "r");
  if (!f) {
    perror(path);
    return 2;
  }
  /* read all layers into memory filtered to <256 */
  typedef struct {
    int a, b;
  } pair;
  typedef struct {
    pair *pairs;
    size_t cnt, cap;
  } layer;
  layer *layers = NULL;
  size_t lcap = 0, lcnt = 0;
  layer cur = {0};
  char line[256];
  while (fgets(line, sizeof line, f)) {
    char *p = line;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
      p++;
    }
    if (*p == '\0' || *p == '\n') {
      if (cur.cnt) {
        if (lcnt >= lcap) {
          lcap = lcap ? lcap * 2 : 16;
          layers = realloc(layers, lcap * sizeof *layers);
        }
        layers[lcnt++] = cur;
        cur = (layer){0};
      }
      continue;
    }
    int a, b;
    if (sscanf(p, "%d %d", &a, &b) != 2) {
      continue;
    }
    if (a < 256 && b < 256) {
      if (cur.cnt >= cur.cap) {
        cur.cap = cur.cap ? cur.cap * 2 : 16;
        cur.pairs = realloc(cur.pairs, cur.cap * sizeof *cur.pairs);
      }
      cur.pairs[cur.cnt++] = (pair){a, b};
    }
  }
  if (cur.cnt) {
    if (lcnt >= lcap) {
      lcap = lcap ? lcap * 2 : 16;
      layers = realloc(layers, lcap * sizeof *layers);
    }
    layers[lcnt++] = cur;
  } else
    free(cur.pairs);
  fclose(f);
  if (lcnt == 0) {
    fprintf(stderr, "ERROR: no layers for 256-element projection\n");
    return 2;
  }

  /* test cases */
  int cases[4][256];
  for (int i = 0; i < 256; i++) {
    cases[0][i] = i;
  }
  for (int i = 0; i < 256; i++) {
    cases[1][i] = 255 - i;
  }
  for (int i = 0; i < 256; i++) {
    cases[2][i] = (i * 73 + 19) & 0xFF;
  }
  /* random seed 1 sequence from python Random(1) */
  int rnd[256] = {
      68,  32,  130, 60,  253, 230, 241, 194, 107, 48,  249, 14,  199, 221, 1,   228, 136, 117, 52,
      162, 15,  11,  13,  4,   195, 110, 216, 14,  113, 224, 253, 119, 176, 118, 112, 235, 148, 11,
      213, 51,  95,  151, 61,  170, 216, 97,  155, 145, 255, 201, 17,  245, 124, 206, 212, 88,  187,
      191, 44,  224, 55,  83,  201, 189, 250, 15,  240, 22,  157, 201, 87,  86,  116, 6,   102, 118,
      207, 176, 180, 235, 137, 2,   196, 66,  105, 218, 28,  246, 186, 102, 211, 248, 182, 212, 177,
      0,   169, 234, 14,  117, 90,  92,  46,  130, 16,  36,  42,  8,   231, 7,   143, 127, 137, 56,
      94,  176, 148, 35,  85,  81,  130, 86,  139, 150, 232, 164, 254, 242, 58,  12,  159, 197, 175,
      215, 96,  132, 55,  129, 107, 221, 10,  115, 9,   203, 74,  18,  82,  228, 218, 112, 230, 114,
      15,  202, 164, 218, 30,  152, 64,  108, 24,  156, 36,  39,  158, 152, 81,  213, 129, 66,  4,
      19,  111, 235, 87,  19,  193, 102, 177, 50,  105, 221, 99,  252, 53,  199, 151, 255, 8,   166,
      205, 144, 9,   80,  102, 167, 69,  173, 219, 109, 136, 49,  194, 176, 248, 120, 33,  20,  43,
      68,  86,  85,  109, 137, 170, 130, 188, 173, 174, 58,  149, 120, 250, 69,  53,  164, 20,  208,
      37,  194, 75,  64,  174, 58,  193, 39,  114, 41,  136, 186, 151, 58,  234, 141, 55,  23,  151,
      6,   7,   46,  211, 58,  20,  96,  122, 215};
  for (int i = 0; i < 256; i++) {
    cases[3][i] = rnd[i];
  }

  int failed = 0;
  for (int c = 0; c < 4; c++) {
    int arr[256];
    memcpy(arr, cases[c], sizeof arr);
    for (size_t l = 0; l < lcnt; l++) {
      for (size_t k = 0; k < layers[l].cnt; k++) {
        int a = layers[l].pairs[k].a, b = layers[l].pairs[k].b;
        if (arr[a] > arr[b]) {
          int t = arr[a];
          arr[a] = arr[b];
          arr[b] = t;
        }
      }
    }
    int sorted = 1;
    for (int i = 0; i < 255; i++)
      if (arr[i] > arr[i + 1]) {
        sorted = 0;
        break;
      }
    if (!sorted) {
      failed++;
      fprintf(stderr, "[FAIL] case#%d\n", c + 1);
    }
  }
  for (size_t i = 0; i < lcnt; i++) {
    free(layers[i].pairs);
  }
  free(layers);
  if (failed) {
    fprintf(stderr, "TOTAL: %d failed case(s)\n", failed);
    return 1;
  }
  printf("TOTAL: all cases passed\n");
  return 0;
}

int main(void)
{
  /* ensure sorter exists, build if missing */
  FILE *f = fopen(SORTER, "r");
  if (!f) {
    int rc = system("make -s sorter");
    if (rc != 0) {
      fprintf(stderr, "make sorter failed\n");
      return 2;
    }
  } else
    fclose(f);
  return load_and_test(SORTER);
}
