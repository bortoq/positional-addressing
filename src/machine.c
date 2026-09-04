/*
 * vm1 — 2-phase positional addressing
 *
 * Interval = open (low,high) — both bounds excluded.
 * Bits [low+1, high-1] are copied. Example: n=8, dst=65504
 *   -> low=65503, high=65512 -> copies 65504..65511.
 *
 * Stable pairwise network → 2 phases are enough:
 *   stage1: spread interval attributes along pairs,
 *           where pos is inside (low,high) set sort=partner.
 *   stage2: compare sort keys, swap sort/partner and move value.
 * Only key operation is '>' on 16-bit sort keys.
 *
 * 2024-11-02, dmitri bortoq (bortoq@gmail.com), MIT license
 */
#include "machine.h"

#include "loader.h"

// ---------- helpers ----------

// open interval check: pos is strictly inside (low,high)
static inline int inside_open(idx pos, idx low, idx high)
{
    return pos > low && pos < high;
}

// ---------- network storage ----------

// detect repeated wire in current layer → next layer
static int last(idx i)
{
    static char used[SPACE_SIZE];
    if(!used[i])
    {
        used[i] = 1;
        return 0;
    }
    memset(used, 0, SPACE_SIZE);
    return 1;
}

static void layer_reserve(layer* l, size_t need)
{
    if(need <= l->cap) return;
    size_t cap = l->cap ? l->cap : 8;
    while(cap < need) cap *= 2;
    pair* p = realloc(l->pairs, cap * sizeof *p);
    E(p == NULL, "cannot allocate network layer");
    l->pairs = p;
    l->cap = cap;
}

static void network_reserve(network* n, size_t need)
{
    if(need <= n->cap) return;
    size_t cap = n->cap ? n->cap : 8;
    while(cap < need) cap *= 2;
    layer* l = realloc(n->layers, cap * sizeof *l);
    E(l == NULL, "cannot allocate network");
    n->layers = l;
    n->cap = cap;
}

static void network_add_layer(network* n)
{
    network_reserve(n, n->count + 1);
    memset(&n->layers[n->count], 0, sizeof n->layers[n->count]);
    n->count++;
}

static layer* network_last_layer(network* n)
{
    E(n->count == 0, "internal error: missing network layer");
    return &n->layers[n->count - 1];
}

static void network_push_pair(network* n, idx a, idx b)
{
    if(!n) return;  // guard
    layer* l = network_last_layer(n);
    layer_reserve(l, l->count + 1);
    l->pairs[l->count++] = (pair){a, b};
}

static void network_free(network* n)
{
    if(!n) return;
    for(size_t i = 0; i < n->count; i++) free(n->layers[i].pairs);
    free(n->layers);
    n->layers = NULL;
    n->count = 0;
    n->cap = 0;
}

// ---------- sorter file → fabric ----------

static int read_pair(FILE* f, idx* a, idx* b)
{
    if(!f || !a || !b) return 0;
    int c = fscanf(f, "%hu%hu", a, b);
    return c == 2 && !feof(f);
}

static void trim_empty_tail_layer(network* net)
{
    if(!net || net->count == 0) return;
    layer* tail = &net->layers[net->count - 1];
    if(tail->count == 0)
    {
        free(tail->pairs);
        net->count--;
    }
}

// split pairs into layers by wire reuse (like hardware)
static void sorter_to_network(machine* vm1, FILE* sorter)
{
    if(!vm1 || !sorter) return;
    idx l = 0, n = 0, a, b;
    memset(&vm1->fabric, 0, sizeof vm1->fabric);
    network_add_layer(&vm1->fabric);
    while(read_pair(sorter, &a, &b))
    {
        E(a >= SPACE_SIZE || b >= SPACE_SIZE, "sorter pair out of range");
        if(last(a) || last(b))
        {
            n = 0;
            l++;
            E(l >= LAYER_N, "number of layer of sorter is too big");
            network_add_layer(&vm1->fabric);
        }
        network_push_pair(&vm1->fabric, a, b);
        n++;
        E(n >= SPACE_SIZE, "number of pairs of layer is too big");
    }
    trim_empty_tail_layer(&vm1->fabric);
}

// ---------- I/O ----------

static int channel_read(machine* vm1)
{
    if(!vm1) return -1;
    return fread(&vm1->space[CHANNEL_IN], 1, 1, stdin) == 1 ? 0 : -1;
}

static int channel_write(machine* vm1)
{
    if(!vm1) return -1;
    return fwrite(&vm1->space[CHANNEL_OUT], 1, 1, stdout) == 1 ? 0 : -1;
}

static int stream_read_u8(uint8_t* out)
{
    if(!out) return -1;
    int c = fgetc(stdin);
    if(c == EOF) return -1;
    *out = (uint8_t)c;
    return 0;
}

static int stream_read_u16le(uint16_t* out)
{
    if(!out) return -1;
    uint8_t lo, hi;
    if(stream_read_u8(&lo) || stream_read_u8(&hi)) return -1;
    *out = (uint16_t)lo | ((uint16_t)hi << 8);
    return 0;
}

// ---------- space words ----------

static idx read_word(const machine* vm1, idx word)
{
    idx v;
    memcpy(&v, vm1->space + word * sizeof v, sizeof v);
    return v;
}

static void write_word(machine* vm1, idx word, idx value)
{
    memcpy(vm1->space + word * sizeof value, &value, sizeof value);
}

// Optional LDR1 loader: "LDR1" + u16 count + u16 base + payload
static void maybe_load_program_from_stdin(machine* vm1)
{
    if(!vm1) return;
    uint8_t m[4];
    long pos = ftell(stdin);
    if(stream_read_u8(&m[0]) || stream_read_u8(&m[1]) || stream_read_u8(&m[2]) ||
       stream_read_u8(&m[3]))
        return;
    if(!(m[0] == 'L' && m[1] == 'D' && m[2] == 'R' && m[3] == '1'))
    {
        if(pos >= 0) (void)fseek(stdin, pos, SEEK_SET);
        return;
    }
    uint16_t words = 0, base = 0;
    E(stream_read_u16le(&words), "loader protocol: missing word_count");
    E(stream_read_u16le(&base), "loader protocol: missing dst_word_base");

    idx cap = (idx)(sizeof vm1->space / sizeof(idx));
    E((idx)base >= cap, "loader protocol: dst base out of range");
    E((idx)words > (idx)(cap - (idx)base), "loader protocol: payload out of range");

    for(idx i = 0; i < (idx)words; i++)
    {
        uint16_t w = 0;
        E(stream_read_u16le(&w), "loader protocol: truncated payload");
        write_word(vm1, (idx)(base + i), (idx)w);
    }
}

// ---------- init ----------

static void parse_args_or_die(int argc)
{
    E(argc != 2, HELP);
}

static void init_origin_keys(machine* vm1)
{
    if(!vm1) return;
    for(idx i = 0; i < SPACE_SIZE; i++) vm1->subspace[i].key.origin = i;
}

static void install_loader(machine* vm1)
{
    if(!vm1) return;
    idx cap = (idx)(sizeof vm1->space / sizeof(idx));
    idx n = (idx)ARR_SZ(loader_words);
    if(n > cap) return;  // guard: space too small
    for(idx i = 0; i < n; i++) write_word(vm1, i, loader_words[i]);
}

static machine* allocate_machine(int argc, char** argv)
{
    parse_args_or_die(argc);
    machine* vm1 = calloc(1, sizeof *vm1);
    E(vm1 == NULL, "cannot allocate machine memory");

    FILE* sorter = fopen(argv[1], "r");
    E(sorter == NULL, "cannot open sorter");

    sorter_to_network(vm1, sorter);
    fclose(sorter);

    init_origin_keys(vm1);
    install_loader(vm1);
    return vm1;
}

// ---------- addressing: stage1 ----------

// sync: dst owns value -> write to space, else read from space
static void sync_space_and_subspace(machine* vm1)
{
    if(!vm1) return;
    for(idx i = 0; i < SPACE_SIZE; i++)
    {
        if(vm1->subspace[i].role == 1)
            SETBIT(vm1->space[i / CHAR_BIT], i % CHAR_BIT, vm1->subspace[i].value);
        else
            vm1->subspace[i].value = GETBIT(vm1->space[i / CHAR_BIT], i % CHAR_BIT);
    }
}

static void reset_stage1_state(machine* vm1)
{
    if(!vm1) return;
    for(idx i = 0; i < SPACE_SIZE; i++)
    {
        vm1->subspace[i].role = 0;
        vm1->subspace[i].active = 0;
        vm1->subspace[i].key.sort = vm1->subspace[i].key.origin;
    }
}

static idx program_word_limit(const machine* vm1)
{
    (void)vm1;
    idx total = (idx)(sizeof((machine*)0)->space / sizeof(idx));
    idx proc = PROCESSOR_N * EDGES * WAYS;
    return proc < total ? proc : total;
}

// seed one instruction: 4 words -> two interval descriptors
static void seed_interval_pair(machine* vm1, idx base_word)
{
    if(!vm1) return;
    idx dst_low = read_word(vm1, base_word);
    idx dst_high = read_word(vm1, base_word + 1);
    idx src_low = read_word(vm1, base_word + 2);
    idx src_high = read_word(vm1, base_word + 3);
    if(src_low == src_high) return;  // empty

    // dst side
    vm1->subspace[base_word].key.low = dst_low;
    vm1->subspace[base_word].key.high = dst_high;
    vm1->subspace[base_word].key.partner = src_low;
    vm1->subspace[base_word].role = 0;
    vm1->subspace[base_word].active = 1;

    // src side
    vm1->subspace[base_word + 1].key.low = src_low;
    vm1->subspace[base_word + 1].key.high = src_high;
    vm1->subspace[base_word + 1].key.partner = dst_low;
    vm1->subspace[base_word + 1].role = 1;
    vm1->subspace[base_word + 1].active = 1;
}

static void seed_intervals_from_program(machine* vm1)
{
    if(!vm1) return;
    idx words = program_word_limit(vm1);
    for(idx i = 0; i + EDGES * WAYS <= words; i += EDGES * WAYS)
        seed_interval_pair(vm1, i);
}

static void stage1_init(machine* vm1)
{
    if(!vm1) return;
    sync_space_and_subspace(vm1);
    reset_stage1_state(vm1);
    seed_intervals_from_program(vm1);
}

// copy/move helpers
static inline void copy_pos(machine* vm1, idx dst, idx src)
{
    vm1->subspace[dst].key.low = vm1->subspace[src].key.low;
    vm1->subspace[dst].key.high = vm1->subspace[src].key.high;
    vm1->subspace[dst].key.partner = vm1->subspace[src].key.partner;
    vm1->subspace[dst].role = vm1->subspace[src].role;
    vm1->subspace[dst].active = 1;
}

static inline void move_pos(machine* vm1, idx dst, idx src)
{
    copy_pos(vm1, dst, src);
    vm1->subspace[src].active = 0;
    vm1->subspace[src].role = 0;
}

// stage1: propagate active interval to the right
static void stage1_from_left(machine* vm1, idx left, idx right)
{
    if(!vm1 || vm1->subspace[left].active == 0) return;

    // interval (low,high) open
    idx low = vm1->subspace[left].key.low;
    idx high = vm1->subspace[left].key.high;

    if(low > left && low > right)
    {
        // both outside on the left -> move
        move_pos(vm1, right, left);
    }
    else if(low > left && inside_open(right, low, high))
    {
        // right is first position inside -> move + mark sort
        move_pos(vm1, right, left);
        vm1->subspace[right].key.sort = vm1->subspace[left].key.partner;
    }
    else if(left > low && high > right)
    {
        // both inside (left<right assumed) -> copy + mark
        copy_pos(vm1, right, left);
        vm1->subspace[right].key.sort = vm1->subspace[left].key.partner;
    }
}

static void stage1_from_right(machine* vm1, idx left, idx right)
{
    if(!vm1 || vm1->subspace[right].active == 0) return;

    idx low = vm1->subspace[right].key.low;
    idx high = vm1->subspace[right].key.high;

    if(right > high && left > high)
    {
        move_pos(vm1, left, right);
    }
    else if(right > high && low > left && left > high)
    {
        move_pos(vm1, left, right);
        vm1->subspace[left].key.sort = vm1->subspace[right].key.partner;
    }
    else if(high > right && left > low)
    {
        copy_pos(vm1, left, right);
        vm1->subspace[left].key.sort = vm1->subspace[right].key.partner;
    }
}

static void stage1_pair(machine* vm1, idx a, idx b)
{
    if(!vm1) return;
    if(vm1->subspace[a].active)
        stage1_from_left(vm1, a, b);
    else if(vm1->subspace[b].active)
        stage1_from_right(vm1, a, b);
}

// ---------- addressing: stage2 ----------

static void stage2_init(machine* vm1)
{
    if(!vm1) return;
    for(idx i = 0; i < SPACE_SIZE; i++)
        vm1->subspace[i].key.partner = vm1->subspace[i].key.origin;
}

static inline void swap_sort_and_partner(machine* vm1, idx l, idx r)
{
    idx tmp_sort = vm1->subspace[l].key.sort;
    idx tmp_partner = vm1->subspace[l].key.partner;
    vm1->subspace[l].key.sort = vm1->subspace[r].key.sort;
    vm1->subspace[r].key.sort = tmp_sort;
    vm1->subspace[l].key.partner = vm1->subspace[r].key.partner;
    vm1->subspace[r].key.partner = tmp_partner;
}

static inline void move_value_and_role(machine* vm1, idx l, idx r)
{
    if(!vm1 || vm1->subspace[l].active == 0) return;
    if(vm1->subspace[l].role == 1)
    {
        vm1->subspace[r].value = vm1->subspace[l].value;
        vm1->subspace[r].role = 1;
        vm1->subspace[l].role = 0;
    }
    else
    {
        vm1->subspace[l].value = vm1->subspace[r].value;
        vm1->subspace[r].role = 0;
        vm1->subspace[l].role = 1;
    }
}

static void stage2_pair(machine* vm1, idx a, idx b)
{
    if(!vm1) return;
    if(vm1->subspace[a].key.sort <= vm1->subspace[b].key.sort) return;
    swap_sort_and_partner(vm1, a, b);
    move_value_and_role(vm1, a, b);
}

// ---------- run ----------

static void run_network(machine* vm1, void (*pair_op)(machine*, idx, idx))
{
    if(!vm1 || !pair_op) return;
    for(size_t l = 0; l < vm1->fabric.count; l++)
        for(size_t p = 0; p < vm1->fabric.layers[l].count; p++)
            pair_op(vm1, vm1->fabric.layers[l].pairs[p].a,
                    vm1->fabric.layers[l].pairs[p].b);
}

static void run_cycle(machine* vm1, void (**inits)(machine*),
                      void (**pairs)(machine*, idx, idx), idx n)
{
    if(!vm1 || !inits || !pairs) return;
    for(idx t = 0; t < n; t++)
    {
        inits[t](vm1);
        run_network(vm1, pairs[t]);
    }
}

static void run_channels(machine* vm1, void (**inits)(machine*),
                         void (**pairs)(machine*, idx, idx), idx n)
{
    if(!vm1) return;
    maybe_load_program_from_stdin(vm1);
    uint64_t life = LIFE;
    while(life-- && channel_read(vm1) == 0)
    {
        run_cycle(vm1, inits, pairs, n);
        if(channel_write(vm1) != 0) break;
    }
}

int main(int argc, char** argv)
{
    machine* vm1 = allocate_machine(argc, argv);
    void (*inits[])(machine*) = {stage1_init, stage2_init};
    void (*pairs[])(machine*, idx, idx) = {stage1_pair, stage2_pair};

    run_channels(vm1, inits, pairs, ARR_SZ(pairs));

    network_free(&vm1->fabric);
    free(vm1);
    return 0;
}
