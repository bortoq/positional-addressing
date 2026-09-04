/* mkldr1 - build LDR1 stdin stream for vm1 (C port of mkldr1.py) */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#define _GNU_SOURCE
#include <string.h>

static void die(const char *msg) {
    fprintf(stderr, "mkldr1: %s\n", msg);
    exit(1);
}
static void die2(const char *msg, const char *arg) {
    fprintf(stderr, "mkldr1: %s: %s\n", msg, arg);
    exit(1);
}

static int parse_u16(const char *s, uint16_t *out) {
    char *end;
    long v = strtol(s, &end, 0);
    if (end == s || *end != '\0' || v < 0 || v > 0xFFFF) return -1;
    *out = (uint16_t)v;
    return 0;
}

int main(int argc, char **argv) {
    const char *in_prog = NULL;
    const char *out_file = NULL;
    const char *tail_file = NULL;
    long dst_base = 0;
    int has_dst = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--in-prog") == 0 && i+1 < argc) in_prog = argv[++i];
        else if (strcmp(argv[i], "--out") == 0 && i+1 < argc) out_file = argv[++i];
        else if (strcmp(argv[i], "--dst-word-base") == 0 && i+1 < argc) { dst_base = strtol(argv[++i], NULL, 0); has_dst = 1; }
        else if (strcmp(argv[i], "--tail-bytes") == 0 && i+1 < argc) tail_file = argv[++i];
        else if (strncmp(argv[i], "--in-prog=", 10) == 0) in_prog = argv[i]+10;
        else if (strncmp(argv[i], "--out=", 6) == 0) out_file = argv[i]+6;
        else if (strncmp(argv[i], "--dst-word-base=", 16) == 0) { dst_base = strtol(argv[i]+16, NULL, 0); has_dst = 1; }
        else if (strncmp(argv[i], "--tail-bytes=", 13) == 0) tail_file = argv[i]+13;
        else { fprintf(stderr, "unknown arg: %s\nusage: mkldr1 --in-prog FILE --out FILE [--dst-word-base N] [--tail-bytes FILE]\n", argv[i]); return 2; }
    }
    if (!in_prog || !out_file) die("missing --in-prog or --out");
    if (!has_dst) dst_base = 0;
    if (dst_base < 0 || dst_base > 0xFFFF) die("dst-word-base must be uint16");

    FILE *fin = fopen(in_prog, "r");
    if (!fin) die2("cannot open in-prog", in_prog);

    uint16_t *words = NULL;
    size_t cap = 0, len = 0;
    char line[4096];
    int lineno = 0;
    while (fgets(line, sizeof line, fin)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        // trim
        char *p = line;
        while (*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
        if (*p=='\0') continue;
        char *end = p + strlen(p) -1;
        while (end>p && (*end==' '||*end=='\t'||*end=='\r'||*end=='\n')) *end--='\0';
        if (*p=='\0') continue;
        // split into 4 tokens
        char *tok[4];
        int ntok=0;
        char *s = p;
        // manual tokenization
        tok[ntok++] = strtok(s, " \t"); s=NULL;
        for (; ntok<4; ntok++) {
            char *t = strtok(s, " \t");
            if (!t) break;
            tok[ntok]=t;
        }
        // check extra token
        if (strtok(NULL, " \t")) { fprintf(stderr, "mkldr1: %s:%d: expected 4 ints, got >4\n", in_prog, lineno); return 1; }
        if (ntok!=4) { fprintf(stderr, "mkldr1: %s:%d: expected 4 ints, got %d\n", in_prog, lineno, ntok); return 1; }
        for (int i=0;i<4;i++) {
            uint16_t v;
            if (parse_u16(tok[i], &v)!=0) { fprintf(stderr, "mkldr1: %s:%d: value out of uint16 range: %s\n", in_prog, lineno, tok[i]); return 1; }
            if (len>=cap) {
                size_t ncap = cap?cap*2:256;
                uint16_t *nw = realloc(words, ncap*sizeof *nw);
                if (!nw) die("out of memory");
                words=nw; cap=ncap;
            }
            words[len++]=v;
        }
    }
    fclose(fin);
    if (len > 0xFFFF) die("word_count exceeds uint16");

    FILE *fout = fopen(out_file, "wb");
    if (!fout) die2("cannot open out", out_file);
    // magic
    if (fwrite("LDR1",1,4,fout)!=4) die("write failed");
    uint8_t hdr[4];
    hdr[0]= len & 0xFF; hdr[1]=(len>>8)&0xFF;
    hdr[2]= dst_base & 0xFF; hdr[3]=(dst_base>>8)&0xFF;
    if (fwrite(hdr,1,4,fout)!=4) die("write failed");
    for (size_t i=0;i<len;i++) {
        uint8_t b[2]={ words[i]&0xFF, (words[i]>>8)&0xFF };
        if (fwrite(b,1,2,fout)!=2) die("write failed");
    }
    size_t payload_bytes = 4+4+len*2;
    if (tail_file) {
        FILE *ft = fopen(tail_file,"rb");
        if (!ft) die2("cannot open tail", tail_file);
        char buf[8192];
        size_t n;
        while ((n=fread(buf,1,sizeof buf,ft))>0) {
            if (fwrite(buf,1,n,fout)!=n) die("write failed");
            payload_bytes+=n;
        }
        fclose(ft);
    }
    fclose(fout);
    free(words);
    printf("wrote %s bytes=%zu words=%zu\n", out_file, payload_bytes, len);
    return 0;
}
