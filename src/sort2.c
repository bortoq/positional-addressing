/*
 * BLOCK SORTING NETWORK GENERATOR
 *
 * Recursive odd-even mergesort with a 16-input optimal base network.
 * Generates sorter pairs for an arbitrary working set by:
 * 1) building network for next power-of-two size;
 * 2) filtering pairs outside requested position range.
 *
 * The 16-input network below matches the 60-comparator, 10-layer network
 * commonly attributed to Green.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t a;
  uint32_t b;
} pair_t;

typedef struct {
  pair_t *pairs;
  size_t count;
  size_t cap;
} layer_t;

typedef struct {
  layer_t *layers;
  size_t count;
  size_t cap;
} network_t;

static void die(const char *msg)
{
  fputs(msg, stderr);
  fputc('\n', stderr);
  exit(EXIT_FAILURE);
}

static uint32_t parse_u32(const char *s)
{
  char *end = NULL;
  unsigned long v = strtoul(s, &end, 10);
  if(s[0] == '\0' || (end && *end != '\0') || v > UINT32_MAX)
    die("invalid size");
  return (uint32_t)v;
}

static int is_pow2(uint32_t n)
{
  return n != 0 && (n & (n - 1)) == 0;
}

static uint32_t next_pow2(uint32_t n)
{
  if(n <= 1)
    return 1;

  --n;
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  return n + 1;
}

static void layer_reserve(layer_t *layer, size_t need)
{
  if(need <= layer->cap)
    return;

  size_t cap = layer->cap ? layer->cap : 8;
  while(cap < need)
    cap *= 2;

  pair_t *pairs = realloc(layer->pairs, cap * sizeof *pairs);
  if(!pairs)
    die("cannot allocate layer");
  layer->pairs = pairs;
  layer->cap = cap;
}

static void network_reserve(network_t *net, size_t need)
{
  if(need <= net->cap)
    return;

  size_t cap = net->cap ? net->cap : 8;
  while(cap < need)
    cap *= 2;

  layer_t *layers = realloc(net->layers, cap * sizeof *layers);
  if(!layers)
    die("cannot allocate network");
  net->layers = layers;
  net->cap = cap;
}

static void layer_push(layer_t *layer, uint32_t a, uint32_t b)
{
  layer_reserve(layer, layer->count + 1);
  layer->pairs[layer->count++] = (pair_t){a, b};
}

static void network_init(network_t *net)
{
  memset(net, 0, sizeof *net);
}

static void layer_free(layer_t *layer)
{
  free(layer->pairs);
  layer->pairs = NULL;
  layer->count = 0;
  layer->cap = 0;
}

static void network_free(network_t *net)
{
  if(!net)
    return;
  for(size_t i = 0; i < net->count; ++i)
    layer_free(&net->layers[i]);
  free(net->layers);
  net->layers = NULL;
  net->count = 0;
  net->cap = 0;
}

static void network_add_layer(network_t *net)
{
  network_reserve(net, net->count + 1);
  memset(&net->layers[net->count], 0, sizeof net->layers[net->count]);
  ++net->count;
}

static layer_t *network_last_layer(network_t *net)
{
  if(net->count == 0)
    die("internal error: missing layer");
  return &net->layers[net->count - 1];
}

static void network_push_pair(network_t *net, uint32_t a, uint32_t b)
{
  layer_push(network_last_layer(net), a, b);
}

static void network_append_layer_copy(network_t *dst, const layer_t *src, uint32_t offset)
{
  network_add_layer(dst);
  layer_t *out = network_last_layer(dst);
  for(size_t i = 0; i < src->count; ++i)
    layer_push(out, src->pairs[i].a + offset, src->pairs[i].b + offset);
}

static network_t make_base16(uint32_t base)
{
  static const pair_t layers[][8] = {
    {{0,13},{1,12},{2,15},{3,14},{4,8},{5,6},{7,11},{9,10}},
    {{0,5},{1,7},{2,9},{3,4},{6,13},{8,14},{10,15},{11,12}},
    {{0,1},{2,3},{4,5},{6,8},{7,9},{10,11},{12,13},{14,15}},
    {{0,2},{1,3},{4,10},{5,11},{6,7},{8,9},{12,14},{13,15}},
    {{1,2},{3,12},{4,6},{5,7},{8,10},{9,11},{13,14}},
    {{1,4},{2,6},{5,8},{7,10},{9,13},{11,14}},
    {{2,4},{3,6},{9,12},{11,13}},
    {{3,5},{6,8},{7,9},{10,12}},
    {{3,4},{5,6},{7,8},{9,10},{11,12}},
    {{6,7},{8,9}},
  };
  static const size_t counts[] = {8, 8, 8, 8, 7, 6, 4, 4, 5, 2};

  network_t net;
  network_init(&net);
  for(size_t l = 0; l < sizeof counts / sizeof counts[0]; ++l) {
    network_add_layer(&net);
    layer_t *layer = network_last_layer(&net);
    for(size_t i = 0; i < counts[l]; ++i)
      layer_push(layer, base + layers[l][i].a, base + layers[l][i].b);
  }
  return net;
}

static network_t merge_network(uint32_t base, uint32_t n, uint32_t step)
{
  if(step * 2 < n) {
    network_t left = merge_network(base, n, step * 2);
    network_t right = merge_network(base + step, n, step * 2);
    if(left.count != right.count)
      die("internal error: merge depth mismatch");

    network_t net;
    network_init(&net);
    for(size_t l = 0; l < left.count; ++l) {
      network_add_layer(&net);
      layer_t *out = network_last_layer(&net);
      for(size_t i = 0; i < left.layers[l].count; ++i)
        layer_push(out, left.layers[l].pairs[i].a, left.layers[l].pairs[i].b);
      for(size_t i = 0; i < right.layers[l].count; ++i)
        layer_push(out, right.layers[l].pairs[i].a, right.layers[l].pairs[i].b);
    }

    network_free(&left);
    network_free(&right);

    network_add_layer(&net);
    layer_t *tail = network_last_layer(&net);
    for(uint32_t i = base + step; i + step < base + n; i += step * 2)
      layer_push(tail, i, i + step);
    return net;
  }

  network_t net;
  network_init(&net);
  network_add_layer(&net);
  network_push_pair(&net, base, base + step);
  return net;
}

static network_t sort_network(uint32_t base, uint32_t n)
{
  if(n == 16)
    return make_base16(base);

  uint32_t half = n / 2;
  network_t left = sort_network(base, half);
  network_t right = sort_network(base + half, half);
  if(left.count != right.count)
    die("internal error: sort depth mismatch");

  network_t net;
  network_init(&net);
  for(size_t l = 0; l < left.count; ++l) {
    network_add_layer(&net);
    layer_t *out = network_last_layer(&net);
    for(size_t i = 0; i < left.layers[l].count; ++i)
      layer_push(out, left.layers[l].pairs[i].a, left.layers[l].pairs[i].b);
    for(size_t i = 0; i < right.layers[l].count; ++i)
      layer_push(out, right.layers[l].pairs[i].a, right.layers[l].pairs[i].b);
  }

  network_free(&left);
  network_free(&right);

  network_t merge = merge_network(base, n, 1);
  for(size_t l = 0; l < merge.count; ++l)
    network_append_layer_copy(&net, &merge.layers[l], 0);
  network_free(&merge);
  return net;
}

static void print_network_trimmed(const network_t *net, uint32_t limit)
{
  for(size_t l = 0; l < net->count; ++l)
    for(size_t i = 0; i < net->layers[l].count; ++i)
      if(net->layers[l].pairs[i].a < limit
         && net->layers[l].pairs[i].b < limit)
        printf("%u %u\n",
               net->layers[l].pairs[i].a,
               net->layers[l].pairs[i].b);
}

int main(int argc, char **argv)
{
  uint32_t space_size = 65520;
  uint32_t network_size;

  if(argc > 2) {
    fputs("usage: sort2 [space_size]\n", stderr);
    return EXIT_FAILURE;
  }
  if(argc == 2)
    space_size = parse_u32(argv[1]);

  if(space_size < 16)
    die("space_size must be at least 16");

  network_size = next_pow2(space_size);
  if(!is_pow2(network_size))
    die("internal error: next power-of-two failed");

  setvbuf(stdout, NULL, _IOFBF, 1 << 20);

  network_t net = sort_network(0, network_size);
  print_network_trimmed(&net, space_size);
  network_free(&net);
  return EXIT_SUCCESS;
}
