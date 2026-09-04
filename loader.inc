/* Loader program encoded as vm1 instruction words:
 * [dst_low, dst_high, src_low, src_high]
 */
static const idx loader_words[] = {
  0x003f, 0x2000, 0x1fff, 0x3fc0
};

/* Optional design notes for a channel-code loader (not active in vm1 yet):
 *
 * Protocol idea:
 * 1) Read header bytes from CHANNEL_IN:
 *    - code_size_words (16-bit)
 *    - code_dst_word_base (16-bit)
 * 2) For each incoming byte:
 *    - write byte into a staging area in address space
 * 3) Use staged bytes to self-modify copy instructions and transfer payload
 *    from staging area into processor area [code_dst_word_base ...].
 * 4) Set a "ready" flag byte and jump to loaded code region.
 *
 * This keeps vm1 contract unchanged: all logic runs as copy instructions
 * inside address space.
 */
