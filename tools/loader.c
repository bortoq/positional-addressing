/*
 * mkldr1 — pack VM program into LDR1 stream
 *
 * Input  (.vm1): 4× u16 per line: dst_low dst_high src_low src_high
 *                comments after '#', blank lines ignored
 * Output (binary): "LDR1" + u16 count + u16 base + u16 payload
 *                [+ tail bytes appended verbatim]
 *
 * Example:
 *   ./tools/mkldr1 --in-prog examples/echo.vm1 --out prog.ldrpkt
 *   cat prog.ldrpkt tail.bin | ./vm1 sorter.txt > out.bin
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- helpers ---------- */

static void die(const char *msg)
{
  fprintf(stderr, "loader: %s\n", msg);
  exit(1);
}

static void die2(const char *msg, const char *arg)
{
  fprintf(stderr, "loader: %s: %s\n", msg, arg);
  exit(1);
}

static int parse_u16_strict(const char *s, long *out)
{
  if (!s || !out) {
    return -1;
  }
  errno = 0;
  char *end = NULL;
  long v = strtol(s, &end, 0);
  if (errno != 0 || end == s || *end != '\0') {
    return -1;
  }
  *out = v;
  return 0;
}

static int parse_u16(const char *s, uint16_t *out)
{
  if (!s || !out) {
    return -1;
  }
  char *end = NULL;
  long v = strtol(s, &end, 0);
  if (end == s || *end != '\0' || v < 0 || v > 0xFFFF) {
    return -1;
  }
  *out = (uint16_t)v;
  return 0;
}

/* trim leading/trailing whitespace in place, return trimmed start */
static char *trim(char *s)
{
  if (!s) {
    return s;
  }
  while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') {
    s++;
  }
  if (*s == '\0') {
    return s;
  }
  char *e = s + strlen(s) - 1;
  while (e > s && (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n')) {
    *e-- = '\0';
  }
  return s;
}

/* parse one .vm1 file -> array of u16 words (caller frees) */
static uint16_t *parse_vm1_file(const char *path, size_t *out_len)
{
  if (!path || !out_len) {
    die("internal: null arg");
  }

  FILE *f = fopen(path, "r");
  if (!f) {
    die2("cannot open in-prog", path);
  }

  uint16_t *words = NULL;
  size_t cap = 0, len = 0;
  char line[4096];
  int lineno = 0;

  while (fgets(line, sizeof line, f)) {
    lineno++;
    char *hash = strchr(line, '#');
    if (hash) {
      *hash = '\0';
    }

    char *p = trim(line);
    if (*p == '\0') {
      continue;
    }

    /* split into exactly 4 tokens */
    char *tok[4];
    int ntok = 0;
    char *t = strtok(p, " \t");
    while (t && ntok < 4) {
      tok[ntok++] = t;
      t = strtok(NULL, " \t");
    }
    if (t) {
      fprintf(stderr, "loader: %s:%d: expected 4 ints, got >4\n", path, lineno);
      exit(1);
    }
    if (ntok != 4) {
      fprintf(stderr, "loader: %s:%d: expected 4 ints, got %d\n", path, lineno, ntok);
      exit(1);
    }
    for (int i = 0; i < 4; i++) {
      uint16_t v;
      if (parse_u16(tok[i], &v)) {
        fprintf(stderr, "mkldr1: %s:%d: value out of uint16: %s\n", path, lineno, tok[i]);
        exit(1);
      }
      if (len >= cap) {
        size_t ncap = cap ? cap * 2 : 256;
        uint16_t *nw = realloc(words, ncap * sizeof *nw);
        if (!nw) {
          die("out of memory");
        }
        words = nw;
        cap = ncap;
      }
      words[len++] = v;
    }
  }
  fclose(f);
  if (len > 0xFFFF) {
    die("word_count exceeds uint16");
  }
  if (len == 0) {
    /* Пустая программа валидна: вернуть не-NULL заглушку под free(). */
    static uint16_t empty_sentinel[1];
    *out_len = 0;
    return empty_sentinel;
  }
  *out_len = len;
  return words;
}

static void write_ldr1(const char *out_path, const uint16_t *words, size_t nwords, long dst_base,
                       const char *tail_path)
{
  if (!out_path || (!words && nwords != 0)) {
    die("internal: null arg");
  }

  FILE *out = fopen(out_path, "wb");
  if (!out) {
    die2("cannot open out", out_path);
  }

  /* header */
  if (fwrite("LDR1", 1, 4, out) != 4) {
    die("write failed");
  }
  uint8_t hdr[4] = {(uint8_t)(nwords & 0xFF), (uint8_t)(nwords >> 8), (uint8_t)(dst_base & 0xFF),
                    (uint8_t)(dst_base >> 8)};
  if (fwrite(hdr, 1, 4, out) != 4) {
    die("write failed");
  }

  /* payload */
  for (size_t i = 0; i < nwords; i++) {
    uint8_t b[2] = {(uint8_t)(words[i] & 0xFF), (uint8_t)(words[i] >> 8)};
    if (fwrite(b, 1, 2, out) != 2) {
      die("write failed");
    }
  }

  size_t total = 8 + nwords * 2;

  /* optional tail (raw CHANNEL_IN bytes) */
  if (tail_path) {
    FILE *tf = fopen(tail_path, "rb");
    if (!tf) {
      die2("cannot open tail", tail_path);
    }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, tf)) > 0) {
      if (fwrite(buf, 1, n, out) != n) {
        die("write failed");
      }
      total += n;
    }
    fclose(tf);
  }
  fclose(out);
  printf("wrote %s bytes=%zu words=%zu\n", out_path, total, nwords);
}

/* ---------- main ---------- */

#ifndef UNIT_TEST
int main(int argc, char **argv)
{
  const char *in_prog = NULL;
  const char *out_file = NULL;
  const char *tail_file = NULL;
  long dst_base = 0;

  /* simple arg parser with guard */
  if (argc < 5) {
    fprintf(stderr, "usage: loader --in-prog FILE --out FILE "
                    "[--dst-word-base N] [--tail-bytes FILE]\n");
    return 2;
  }
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--in-prog") == 0 && i + 1 < argc) {
      in_prog = argv[++i];
    }
    else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
      out_file = argv[++i];
    else if (strcmp(argv[i], "--dst-word-base") == 0 && i + 1 < argc) {
      long v = 0;
      if (parse_u16_strict(argv[++i], &v) != 0) {
        fprintf(stderr, "loader: --dst-word-base must be uint16, got '%s'\n", argv[i]);
        return 2;
      }
      dst_base = v;
    }
    else if (strcmp(argv[i], "--tail-bytes") == 0 && i + 1 < argc)
      tail_file = argv[++i];
    else if (strncmp(argv[i], "--in-prog=", 10) == 0)
      in_prog = argv[i] + 10;
    else if (strncmp(argv[i], "--out=", 6) == 0)
      out_file = argv[i] + 6;
    else {
      fprintf(stderr, "unknown arg: %s\n", argv[i]);
      return 2;
    }
  }
  if (!in_prog || !out_file) {
    die("missing --in-prog or --out");
  }
  if (dst_base < 0 || dst_base > 0xFFFF) {
    die("dst-word-base must be uint16");
  }

  size_t nwords = 0;
  uint16_t *words = parse_vm1_file(in_prog, &nwords);
  write_ldr1(out_file, words, nwords, dst_base, tail_file);
  if (nwords != 0) {
    free(words);
  }
  return 0;
}
#endif
