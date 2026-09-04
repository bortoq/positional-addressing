/*
 * copy3_to_vm1 — compile copy(n,dst,src) to VM1 words
 *
 * Input  (copy3):  n dst src   (bit counts and start positions)
 *          n >=0, dst/src >=0, comments after '#'
 * Output (vm1):   dst_low dst_high src_low src_high per line
 *          encoding: low = start-1, high = start+n  (open interval)
 *          copied bits are [start, start+n-1]
 *
 * Example: "8 65504 65512" -> "65503 65512 65511 65520"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// ---------- helpers ----------

static void die(const char *msg) {
    fprintf(stderr, "copy3_to_vm1: %s\n", msg);
    exit(1);
}

static void die2(const char *msg, const char *arg) {
    fprintf(stderr, "copy3_to_vm1: %s: %s\n", msg, arg);
    exit(1);
}

static char *trim(char *s) {
    if (!s) return s;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
    if (*s == '\0') return s;
    char *e = s + strlen(s) - 1;
    while (e > s && (*e == ' ' || *e == '\t' || *e == '\r' || *e == '\n'))
        *e-- = '\0';
    return s;
}

static long parse_long(const char *s, int *ok) {
    if (!s || !ok) return -1;
    char *end = NULL;
    long v = strtol(s, &end, 0);
    *ok = (end != s && *end == '\0');
    return v;
}

// encode one copy -> 4 vm1 words, check u16 range
static int encode_vm1(long n, long dst, long src,
                      long out[4]) {
    if (!out) return -1;
    long dl = dst - 1;
    long dh = dst + n;
    long sl = src - 1;
    long sh = src + n;
    if (dl < 0 || dl > 0xFFFF || dh < 0 || dh > 0xFFFF ||
        sl < 0 || sl > 0xFFFF || sh < 0 || sh > 0xFFFF)
        return -1;
    out[0] = dl; out[1] = dh; out[2] = sl; out[3] = sh;
    return 0;
}

// ---------- main ----------

int main(int argc, char **argv) {
    const char *in_path = NULL;
    const char *out_path = NULL;

    // guard: need both args
    if (argc < 3) {
        fprintf(stderr,
                "usage: copy3_to_vm1 --in-copy3 FILE --out-vm1 FILE\n");
        return 2;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--in-copy3") == 0 && i + 1 < argc)
            in_path = argv[++i];
        else if (strcmp(argv[i], "--out-vm1") == 0 && i + 1 < argc)
            out_path = argv[++i];
        else if (strncmp(argv[i], "--in-copy3=", 11) == 0)
            in_path = argv[i] + 11;
        else if (strncmp(argv[i], "--out-vm1=", 10) == 0)
            out_path = argv[i] + 10;
        else {
            fprintf(stderr, "unknown arg: %s\n", argv[i]);
            return 2;
        }
    }
    if (!in_path || !out_path)
        die("missing --in-copy3 or --out-vm1");

    FILE *fin = fopen(in_path, "r");
    if (!fin) die2("cannot open in-copy3", in_path);

    FILE *fout = fopen(out_path, "w");
    if (!fout) die2("cannot open out-vm1", out_path);

    char line[4096];
    int lineno = 0;
    int count = 0;

    while (fgets(line, sizeof line, fin)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *p = trim(line);
        if (*p == '\0') continue;

        // split into exactly 3 tokens
        char *tok[3];
        int ntok = 0;
        char *t = strtok(p, " \t");
        while (t && ntok < 3) {
            tok[ntok++] = t;
            t = strtok(NULL, " \t");
        }
        if (t) {
            fprintf(stderr,
                    "copy3_to_vm1: %s:%d: expected 3 fields\n",
                    in_path, lineno);
            return 1;
        }
        if (ntok != 3) {
            fprintf(stderr,
                    "copy3_to_vm1: %s:%d: expected 3 fields "
                    "'n dst src' got %d\n",
                    in_path, lineno, ntok);
            return 1;
        }

        int ok = 0;
        long n = parse_long(tok[0], &ok);
        if (!ok || n < 0) {
            fprintf(stderr,
                    "copy3_to_vm1: %s:%d: n must be >=0\n",
                    in_path, lineno);
            return 1;
        }
        long dst = parse_long(tok[1], &ok);
        if (!ok || dst < 0) {
            fprintf(stderr,
                    "copy3_to_vm1: %s:%d: dst must be >=0\n",
                    in_path, lineno);
            return 1;
        }
        long src = parse_long(tok[2], &ok);
        if (!ok || src < 0) {
            fprintf(stderr,
                    "copy3_to_vm1: %s:%d: src must be >=0\n",
                    in_path, lineno);
            return 1;
        }

        long out[4];
        if (encode_vm1(n, dst, src, out)) {
            fprintf(stderr,
                    "copy3_to_vm1: %s:%d: encoded word out of range\n",
                    in_path, lineno);
            return 1;
        }
        fprintf(fout, "%ld %ld %ld %ld\n",
                out[0], out[1], out[2], out[3]);
        count++;
    }

    fclose(fin);
    fclose(fout);
    printf("compiled %d instructions -> %s\n", count, out_path);
    return 0;
}
