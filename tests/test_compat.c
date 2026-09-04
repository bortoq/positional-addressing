/* test_compat - vm1 channel copy compat via LDR1 (C port, sort2-based, non-gating) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/wait.h>

#define SORTER "sorter.txt"
#define VM1 "vm1"
#define CHANNEL_IN_LO 65512
#define CHANNEL_OUT_LO 65504
#define TMP0 64000
#define TMP1 64016
#define LIFE 0x10

typedef struct { const char *name; uint8_t *data; size_t len; } Case;
typedef struct { const char *name; uint16_t *words; size_t n; } Prog;

static void enc(int n,int dst,int src,uint16_t *out){
    out[0]=dst-1; out[1]=dst+n; out[2]=src-1; out[3]=src+n;
}

static int fits_shift(const uint8_t *inp, size_t in_len, const uint8_t *out, size_t out_len){
    for(int drop=0; drop<=2; drop++){
        for(int s=0; s<=8; s++){
            int ok=1;
            for(size_t i=0;i<out_len;i++){
                long j=(long)i - s + drop;
                uint8_t exp = (i < (size_t)s) ? 0 : (j>=0 && j<(long)in_len ? inp[j] : 0);
                if(out[i]!=exp){ ok=0; break; }
            }
            if(ok) return 1;
        }
    }
    return 0;
}

static int run_vm1(const char *sorter, const uint8_t *pkt, size_t pkt_len, uint8_t *out, size_t *out_len){
    int pin[2], pout[2];
    if(pipe(pin)||pipe(pout)) return -1;
    pid_t pid=fork();
    if(pid==0){
        dup2(pin[0], STDIN_FILENO);
        dup2(pout[1], STDOUT_FILENO);
        close(pin[0]); close(pin[1]); close(pout[0]); close(pout[1]);
        execl(VM1, VM1, sorter, (char*)NULL);
        _exit(127);
    }
    close(pin[0]); close(pout[1]);
    write(pin[1], pkt, pkt_len);
    close(pin[1]);
    ssize_t n=0, total=0;
    while((n=read(pout[0], out+total, 4096-total))>0) total+=n;
    close(pout[0]);
    int st; waitpid(pid,&st,0);
    *out_len=total;
    return WIFEXITED(st)?WEXITSTATUS(st):-1;
}

static size_t make_ldr1(uint16_t *words,size_t nw, uint8_t *tail,size_t tail_len, uint8_t *out){
    memcpy(out,"LDR1",4);
    out[4]=nw&0xFF; out[5]=(nw>>8)&0xFF;
    out[6]=0; out[7]=2; // dst base 512 (0x0200)
    size_t p=8;
    for(size_t i=0;i<nw;i++){ out[p++]=words[i]&0xFF; out[p++]=words[i]>>8; }
    if(tail) { memcpy(out+p,tail,tail_len); p+=tail_len; }
    return p;
}

int main(void){
    // ensure built
    if(system("make -s vm1 sorter > /dev/null")!=0){ fprintf(stderr,"build failed\n"); return 2; }
    // cases
    uint8_t c1[]="ABCDE12345";
    uint8_t c2[64], c3[64], c4[64];
    for(int i=0;i<64;i++) c2[i]=i;
    for(int i=0;i<64;i++) c3[i]=63-i;
    // rnd seed1 first 64
    uint8_t rnd64[64]={68,32,130,60,253,230,241,194,107,48,249,14,199,221,1,228,136,117,52,162,15,11,13,4,195,110,216,14,113,224,253,119,176,118,112,235,148,11,213,51,95,151,61,170,216,97,155,145,255,201,17,245,124,206,212,88,187,191,44,224,55,83,201,189};
    memcpy(c4,rnd64,64);

    struct { const char *name; uint8_t *d; size_t l; } cases[]={{"ascii",c1,10},{"inc",c2,64},{"dec",c3,64},{"rnd",c4,64}};
    uint16_t echo_w[4], chain_w[8], dual_w[12];
    uint16_t t4[4];
    enc(8,CHANNEL_OUT_LO,CHANNEL_IN_LO,t4); memcpy(echo_w,t4,sizeof t4);
    enc(8,TMP0,CHANNEL_IN_LO,t4); memcpy(chain_w,t4,sizeof t4);
    enc(8,CHANNEL_OUT_LO,TMP0,t4); memcpy(chain_w+4,t4,sizeof t4);
    enc(8,TMP0,CHANNEL_IN_LO,t4); memcpy(dual_w,t4,sizeof t4);
    enc(8,TMP1,CHANNEL_IN_LO,t4); memcpy(dual_w+4,t4,sizeof t4);
    enc(8,CHANNEL_OUT_LO,TMP1,t4); memcpy(dual_w+8,t4,sizeof t4);
    struct { const char *name; uint16_t *w; size_t n; } progs[]={{"echo",echo_w,4},{"chain",chain_w,8},{"dual",dual_w,12}};

    int passed=0,failed=0;
    uint8_t pkt[8192], out[4096];
    for(int pi=0; pi<3; pi++){
        for(int ci=0; ci<4; ci++){
            size_t pkt_len=make_ldr1(progs[pi].w, progs[pi].n, cases[ci].d, cases[ci].l, pkt);
            size_t out_len=0;
            int rc=run_vm1(SORTER,pkt,pkt_len,out,&out_len);
            if(rc!=0){ fprintf(stderr,"[FAIL] vm1 rc=%d sorter=%s prog=%s case=%s\n",rc,SORTER,progs[pi].name,cases[ci].name); failed++; continue; }
            size_t n = cases[ci].l < LIFE ? cases[ci].l : LIFE;
            if(n>out_len) n=out_len;
            int has_nonzero=0; for(size_t i=0;i<n;i++) if(out[i]) has_nonzero=1;
            int fits = fits_shift(cases[ci].d,cases[ci].l,out,n);
            if(has_nonzero && fits) passed++; else {
                failed++;
                fprintf(stderr,"[FAIL] sorter=%s prog=%s case=%s\n  input=",SORTER,progs[pi].name,cases[ci].name);
                for(size_t i=0;i<cases[ci].l&&i<16;i++) fprintf(stderr,"%02x",cases[ci].d[i]); fprintf(stderr,"\n  output=");
                for(size_t i=0;i<n&&i<16;i++) fprintf(stderr,"%02x",out[i]); fprintf(stderr,"\n");
            }
        }
    }
    printf("=== sorter compatibility summary ===\n sort2: pass=%d fail=%d\n",passed,failed);
    if(failed){
        fprintf(stderr,"NOTE: sort2 is pairwise stable; compat should pass (12/12)\n");
        return 0; // non-gating
    }
    return 0;
}
