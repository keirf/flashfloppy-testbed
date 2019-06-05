/*
 * fm.c
 * 
 * FM-conversion routines.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

uint16_t fm_sync(uint8_t dat, uint8_t clk)
{
    uint16_t _dat = mfmtab[dat] & 0x5555;
    uint16_t _clk = (mfmtab[clk] & 0x5555) << 1;
    return _clk | _dat;
}

void bin_to_fm(void *p, unsigned int nr)
{
    const uint8_t *in = (const uint8_t *)p + nr;
    uint16_t *out = (uint16_t *)p + nr;
    uint8_t x = *--in, y;
    while (nr--) {
        y = *--in;
        *--out = htobe16(mfmtab[x] | 0xaaaa);
        x = y;
    }
}

void fm_check(const void *p, unsigned int nr)
{
    const uint16_t *in = (const uint16_t *)p;
    uint16_t b, c;
    int i, j;
    for (i = 0; i < nr; i++) {
        b = be16toh(in[i]);
        c = mfmtab[mfmtobin(b)] | 0xaaaa;
        if (b != c) {
            printk("Bad FM at word %u: ", i);
            for (j = max_t(int, i-2, 0); j < i; j++)
                printk("%04x ", be16toh(in[j]));
            printk("[%04x] ", be16toh(in[j]));
            for (j = i+1; j < min_t(int, i+3, nr); j++)
                printk("%04x ", be16toh(in[j]));
            printk("\n");
            WARN_ON(TRUE);
        }
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
