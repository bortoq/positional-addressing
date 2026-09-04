/*
 * Pairwise sorting network generator (stable, for 2-phase positional addressing)
 *
 * Generates a pairwise network (power-of-two only, padded otherwise).
 * Pairwise is stable: equal keys keep order, so 2-phase addressing works
 * without the third reordering step.
 *
 * For arbitrary SPACE_SIZE:
 *  1) build pairwise network for next power of two >= SPACE_SIZE
 *  2) if SPACE_SIZE is not power of two: filter pairs with a,b < SPACE_SIZE
 *     into a sequence, then repack greedily by wire readiness (like seq_pack)
 *  3) print layers with blank line between layers
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t left, right; } pair_t;
typedef struct { pair_t *pair; size_t count, cap; } layer_t;
typedef struct { layer_t *layer; size_t layers, cap; } net_t;
typedef struct { pair_t *pair; size_t count, cap; } seq_t;

static void die(const char *msg){ fputs(msg, stderr); fputc('\n', stderr); exit(EXIT_FAILURE); }

static uint32_t parse_u32(const char *s){
    char *end=NULL; unsigned long v=strtoul(s,&end,10);
    if(s[0]=='\0' || (end && *end!='\0') || v>UINT32_MAX) die("invalid size");
    return (uint32_t)v;
}
static int is_pow2(uint32_t n){ return n!=0 && (n&(n-1))==0; }
static uint32_t next_pow2(uint32_t n){
    if(n<=1) return 1;
    --n; n|=n>>1; n|=n>>2; n|=n>>4; n|=n>>8; n|=n>>16; return n+1;
}

/* seq helpers */
static void seq_add(seq_t *s, uint32_t a, uint32_t b)
{
    if(a == b) return;
    if(s->count >= s->cap)
      {
        size_t cap = s->cap ? 2 * s->cap : 64;
        pair_t *p = realloc(s->pair, cap * sizeof *p); 
        if(!p) 
          die("alloc seq"); 
        s->pair = p;
        s->cap = cap; 
      }
    s->pair[s->count++] = (pair_t){a, b};
}

static void seq_free(seq_t *s)
{ 
  free(s->pair); 
  s->pair = NULL;
  s->count = s->cap = 0;
}

/* net helpers */
static void net_reserve(net_t *net, size_t need){
    if(need<=net->cap) return;
    size_t cap=net->cap?net->cap:8;
    while(cap<need) cap*=2;
    layer_t *l=realloc(net->layer, cap*sizeof *l);
    if(!l) die("alloc net");
    for(size_t i=net->cap;i<cap;i++) memset(&l[i],0,sizeof l[i]);
    net->layer=l; net->cap=cap;
}
static void net_ensure_layer(net_t *net, size_t idx){
    if(idx>=net->cap) net_reserve(net, idx+1);
    if(idx>=net->layers){ net->layers=idx+1; }
}
static void net_add(net_t *net, size_t layer, uint32_t a, uint32_t b){
    if(a==b) return;
    net_ensure_layer(net, layer);
    layer_t *l=&net->layer[layer];
    if(l->count>=l->cap){ size_t cap=l->cap?l->cap*2:8; pair_t *p=realloc(l->pair, cap*sizeof *p); if(!p) die("alloc layer"); l->pair=p; l->cap=cap; }
    l->pair[l->count++]=(pair_t){a,b};
}
static void net_free(net_t *net){
    if(!net) return;
    for(size_t i=0;i<net->layers;i++) free(net->layer[i].pair);
    free(net->layer); net->layer=NULL; net->layers=net->cap=0;
}
static void net_print(const net_t *net){
    for(size_t i=0;i<net->layers;i++){
        for(size_t j=0;j<net->layer[i].count;j++) printf("%u %u\n", net->layer[i].pair[j].left, net->layer[i].pair[j].right);
        if(i+1<net->layers) putchar('\n');
    }
}

/* pairwise for power of two n */
static net_t gen_pairwise_pow2(uint32_t n){
    net_t out={0};
    if(n==0 || n>65536) die("n out of range 1..65536");
    for(uint32_t p=n/2; p>=1; p/=2){
        size_t layer=out.layers;
        net_ensure_layer(&out, layer);
        for(uint32_t a=0;a<n;a+=p*2) for(uint32_t b=0;b<p;b++) net_add(&out, layer, a+b, a+b+p);
        for(uint32_t q=n/2; q>=p*2; q/=2){
            layer=out.layers;
            net_ensure_layer(&out, layer);
            for(uint32_t c=0;c<n;c+=p*2) for(uint32_t d=0;d<p;d++) if(c+d+q<n) net_add(&out, layer, c+d+p, c+d+q);
        }
        if(p==1) break;
    }
    return out;
}

/* pack sequence greedily by wire readiness */
static net_t seq_pack(const seq_t *seq, uint32_t wires){
    net_t out={0};
    uint32_t *ready=calloc(wires, sizeof *ready);
    if(!ready) die("alloc ready");
    for(size_t i=0;i<seq->count;i++){
        uint32_t a=seq->pair[i].left, b=seq->pair[i].right;
        uint32_t layer = ready[a] > ready[b] ? ready[a] : ready[b];
        net_add(&out, layer, a, b);
        ready[a]=ready[b]=layer+1;
    }
    free(ready);
    return out;
}

int main(int argc, char **argv){
    uint32_t space_size=65520;
    if(argc>2){ fputs("usage: sort [space_size]\n", stderr); return EXIT_FAILURE; }
    if(argc==2) space_size=parse_u32(argv[1]);
    if(space_size<2) die("space_size must be >=2");
    if(space_size>65536) die("space_size max 65536");

    setvbuf(stdout,NULL,_IOFBF,1<<20);

    if(is_pow2(space_size)){
        net_t net=gen_pairwise_pow2(space_size);
        net_print(&net);
        net_free(&net);
        return EXIT_SUCCESS;
    }
    uint32_t padded=next_pow2(space_size);
    net_t big=gen_pairwise_pow2(padded);
    // filter into seq
    seq_t seq={0};
    for(size_t l=0;l<big.layers;l++) for(size_t k=0;k<big.layer[l].count;k++){
        uint32_t a=big.layer[l].pair[k].left, b=big.layer[l].pair[k].right;
        if(a<space_size && b<space_size) seq_add(&seq,a,b);
    }
    net_free(&big);
    net_t packed=seq_pack(&seq, space_size);
    seq_free(&seq);
    net_print(&packed);
    net_free(&packed);
    return EXIT_SUCCESS;
}
