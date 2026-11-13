/* Original rising sparkles, expanded 3x from a 16 x 10 pixel grid. */
#define FX_PIXEL(bits, bit) (((bits) & (1u << (bit))) ? 255 : 0)
#define FX_TRIPLE(bits, bit) FX_PIXEL(bits, bit), FX_PIXEL(bits, bit), FX_PIXEL(bits, bit)
#define FX_BYTE(bits) FX_TRIPLE(bits, 7), FX_TRIPLE(bits, 6), FX_TRIPLE(bits, 5), FX_TRIPLE(bits, 4), \
                      FX_TRIPLE(bits, 3), FX_TRIPLE(bits, 2), FX_TRIPLE(bits, 1), FX_TRIPLE(bits, 0)
#define FX_ROW(a, b) FX_BYTE(a), FX_BYTE(b), FX_BYTE(a), FX_BYTE(b), FX_BYTE(a), FX_BYTE(b)

static const uint8_t rising_0_pixels[] = {
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x10, 0x00), /* ...#............ */
    FX_ROW(0x00, 0x00), /* ................ */
};
FX_IMAGE(rising_0);

static const uint8_t rising_1_pixels[] = {
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x10, 0x00), /* ...#............ */
    FX_ROW(0x28, 0x00), /* ..#.#........... */
    FX_ROW(0x10, 0x00), /* ...#............ */
    FX_ROW(0x00, 0x10), /* ...........#.... */
    FX_ROW(0x00, 0x00), /* ................ */
};
FX_IMAGE(rising_1);

static const uint8_t rising_2_pixels[] = {
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x10, 0x00), /* ...#............ */
    FX_ROW(0x28, 0x00), /* ..#.#........... */
    FX_ROW(0x10, 0x10), /* ...#.......#.... */
    FX_ROW(0x00, 0x28), /* ..........#.#... */
    FX_ROW(0x00, 0x10), /* ...........#.... */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x01, 0x00), /* .......#........ */
};
FX_IMAGE(rising_2);

static const uint8_t rising_3_pixels[] = {
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x10, 0x00), /* ...#............ */
    FX_ROW(0x28, 0x00), /* ..#.#........... */
    FX_ROW(0x10, 0x10), /* ...#.......#.... */
    FX_ROW(0x00, 0x28), /* ..........#.#... */
    FX_ROW(0x00, 0x10), /* ...........#.... */
    FX_ROW(0x01, 0x00), /* .......#........ */
    FX_ROW(0x02, 0x80), /* ......#.#....... */
    FX_ROW(0x01, 0x00), /* .......#........ */
    FX_ROW(0x00, 0x00), /* ................ */
};
FX_IMAGE(rising_3);

static const uint8_t rising_4_pixels[] = {
    FX_ROW(0x10, 0x00), /* ...#............ */
    FX_ROW(0x00, 0x10), /* ...........#.... */
    FX_ROW(0x00, 0x28), /* ..........#.#... */
    FX_ROW(0x00, 0x10), /* ...........#.... */
    FX_ROW(0x01, 0x00), /* .......#........ */
    FX_ROW(0x02, 0x80), /* ......#.#....... */
    FX_ROW(0x01, 0x00), /* .......#........ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
};
FX_IMAGE(rising_4);

static const uint8_t rising_5_pixels[] = {
    FX_ROW(0x00, 0x10), /* ...........#.... */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x01, 0x00), /* .......#........ */
    FX_ROW(0x02, 0x80), /* ......#.#....... */
    FX_ROW(0x01, 0x00), /* .......#........ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
};
FX_IMAGE(rising_5);

static const uint8_t rising_6_pixels[] = {
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x01, 0x00), /* .......#........ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
};
FX_IMAGE(rising_6);

static const uint8_t rising_7_pixels[] = {
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
    FX_ROW(0x00, 0x00), /* ................ */
};
FX_IMAGE(rising_7);

#undef FX_ROW
#undef FX_BYTE
#undef FX_TRIPLE
#undef FX_PIXEL
