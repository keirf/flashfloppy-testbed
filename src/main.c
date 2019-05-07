/*
 * main.c
 * 
 * System initialisation and navigation main loop.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

int EXC_reset(void) __attribute__((alias("main")));

uint8_t board_id;

static struct timer button_timer;
static volatile uint8_t buttons;
#define B_LEFT 1
#define B_RIGHT 2
static void button_timer_fn(void *unused)
{
    static uint16_t _b[2]; /* 0 = left, 1 = right */
    uint8_t b = 0;
    int i;

    /* We debounce the switches by waiting for them to be pressed continuously 
     * for 16 consecutive sample periods (16 * 5ms == 80ms) */
    for (i = 0; i < 2; i++) {
        _b[i] <<= 1;
        _b[i] |= gpio_read_pin(gpioc, 8-i);
    }

    if (_b[0] == 0)
        b |= B_LEFT;

    if (_b[1] == 0)
        b |= B_RIGHT;

    /* Latch final button state and reset the timer. */
    buttons = b;
    timer_set(&button_timer, button_timer.deadline + time_ms(5));
}

static void canary_init(void)
{
    _irq_stackbottom[0] = _thread_stackbottom[0] = 0xdeadbeef;
}

static void canary_check(void)
{
    ASSERT(_irq_stackbottom[0] == 0xdeadbeef);
    ASSERT(_thread_stackbottom[0] == 0xdeadbeef);
}

static void noinline hfe_test(void)
{
    unsigned int tlen = 16384;
    uint8_t *p = bc_buf_alloc(tlen);
    uint16_t *q = (uint16_t *)p;
    struct read rd;
    struct write wr;
    int i;

    printk("\nHFE TEST:\n");

    da_select_image("hd.hfe");
    floppy_select(0, 5, 1);
    cur_drive->ticks_per_cell = sysclk_us(1);

    test_ready();

    memset(p, 0xaa, tlen*2);

    wr.p = p;
    wr.nr_words = tlen;
    wr.terminate_at_index = FALSE;
    floppy_write_prep(&wr);
    index.count = 0;
    while (index.count == 0)
        continue;
    floppy_write(&wr);

    cur_drive->ticks_per_cell = 65;
    for (i = 0; i < 6000; i++)
        q[i] = htobe16(0x4489);
    for (i ; i < tlen; i++)
        q[i] = htobe16(0x4444);
    wr.terminate_at_index = TRUE;
    floppy_write_prep(&wr);
    index.count = 0;
    while (index.count == 0)
        continue;
    floppy_write(&wr);

    cur_drive->ticks_per_cell = sysclk_us(1);
    rd.p = p;
    rd.nr_words = tlen;
    rd.sync = SYNC_mfm;
    floppy_read_prep(&rd);
    index.count = 0;
    while (index.count == 0)
        continue;
    floppy_read(&rd);

    for (i = 0; i < 8000; i++)
        if (be16toh(q[i]) != 0x4489)
            break;
    printk("We have %d 4489s\n", i);
}

static void noinline adf_test(unsigned int nsec)
{
    uint8_t *p = alloca(nsec*512), *q = alloca(nsec*512);
    unsigned int i, track = 5;
    char name[16];

    printk("\nADF TEST:\n");

    snprintf(name, sizeof(name), "amiga_%u", nsec*80);
    da_select_image(name);
    floppy_select(0, track/2, track&1);
    cur_drive->ticks_per_cell = sysclk_us(2);

    for (i = 0; i < nsec*512; i++)
        p[i] = rand()>>8;
    amiga_track_write(p, track, nsec);

    amiga_track_read(q, track, nsec);
    BUG_ON(memcmp(p, q, nsec*512));

    printk("Amiga %s - OK\n", (nsec == 11) ? "DD" : "HD");
}

static void noinline mfm_rw_sector(struct idam *idam, uint8_t base, uint8_t nr)
{
    unsigned int sz = 128 << idam->n;
    struct ibm_scan_info info[64];
    uint8_t *p = alloca(sz), *q = alloca(sz);
    time_t index_timestamp;
    unsigned int index_period, orig_index_period, gap3;
    int i;

    BUG_ON(ibm_mfm_scan(info, ARRAY_SIZE(info), &gap3) != nr);
    for (i = 0; i < nr; i++) {
        BUG_ON(info[i].idam.c != idam->c);
        BUG_ON(info[i].idam.h != idam->h);
        BUG_ON(info[i].idam.r < base);
        BUG_ON(info[i].idam.r >= (base+nr));
        BUG_ON(info[i].idam.n != idam->n);
    }

    index_timestamp = index.timestamp;
    while (index_timestamp == index.timestamp)
        continue;
    orig_index_period = index.timestamp - index_timestamp;

    printk("Period: %u ms ;; GAP3: %u\n",
           orig_index_period / time_ms(1), gap3);

    for (i = 0; i < sz; i++)
        p[i] = rand()>>8;
    ibm_mfm_write_sector(p, idam, gap3/2);

    index_timestamp = index.timestamp;
    while (index_timestamp == index.timestamp)
        continue;
    index_period = index.timestamp - index_timestamp;
    printk("Index delayed by %d ms\n",
           (int)(index_period - orig_index_period) / (int)time_ms(1));

    ibm_mfm_read_sector(q, idam);
    BUG_ON(memcmp(p, q, sz));
    printk("MFM %u r/w sector - OK\n", sz);
}

static void noinline fm_rw_sector(struct idam *idam, uint8_t base, uint8_t nr)
{
    unsigned int sz = 128 << idam->n;
    struct ibm_scan_info info[64];
    uint8_t *p = alloca(sz), *q = alloca(sz);
    time_t index_timestamp;
    unsigned int index_period, orig_index_period, gap3;
    int i;

    BUG_ON(ibm_fm_scan(info, ARRAY_SIZE(info), &gap3) != nr);
    for (i = 0; i < nr; i++) {
        BUG_ON(info[i].idam.c != idam->c);
        BUG_ON(info[i].idam.h != idam->h);
        BUG_ON(info[i].idam.r < base);
        BUG_ON(info[i].idam.r >= (base+nr));
        BUG_ON(info[i].idam.n != idam->n);
    }

    index_timestamp = index.timestamp;
    while (index_timestamp == index.timestamp)
        continue;
    orig_index_period = index.timestamp - index_timestamp;

    printk("Period: %u ms ;; GAP3: %u\n",
           orig_index_period / time_ms(1), gap3);

    for (i = 0; i < sz; i++)
        p[i] = rand()>>8;
    ibm_fm_write_sector(p, idam, gap3/2);

    index_timestamp = index.timestamp;
    while (index_timestamp == index.timestamp)
        continue;
    index_period = index.timestamp - index_timestamp;
    printk("Index delayed by %d ms\n",
           (int)(index_period - orig_index_period) / (int)time_ms(1));

    ibm_fm_read_sector(q, idam);
    BUG_ON(memcmp(p, q, sz));
    printk("FM %u r/w sector - OK\n", sz);
}

static void noinline dsk_test(void)
{
    struct idam idam_8k = { 2, 0, 3, 6 };
    struct idam idam_512 = { 0, 0, 18, 2 };

    printk("\nDSK TEST:\n");
    da_select_image("tst_dsk");
    floppy_select(0, 2, 0);
    cur_drive->ticks_per_cell = sysclk_us(2);
    mfm_rw_sector(&idam_8k, 3, 1);
    floppy_select(0, 0, 0);
    cur_drive->ticks_per_cell = sysclk_us(2);
    mfm_rw_sector(&idam_512, 3, 16);
    idam_512.r = 3;
    mfm_rw_sector(&idam_512, 3, 16);
}

static void noinline img_test(void)
{
    struct idam idam = { 0, 0, 1, 2 };

    printk("\nIMG TEST:\n");

    da_select_image("720k");
    floppy_select(0, 0, 0);
    idam.n = 2;
    cur_drive->ticks_per_cell = sysclk_us(2);
    mfm_rw_sector(&idam, 1, 9);

    da_select_image("200k");
    floppy_select(0, 0, 0);
    idam.n = 1;
    cur_drive->ticks_per_cell = sysclk_us(4);
    fm_rw_sector(&idam, 0, 10);

    da_select_image("8k.8k");
    floppy_select(0, 0, 0);
    idam.n = 6;
    cur_drive->ticks_per_cell = sysclk_us(1);
    mfm_rw_sector(&idam, 1, 1);
}

int main(void)
{
    /* Relocate DATA. Initialise BSS. */
    if (_sdat != _ldat)
        memcpy(_sdat, _ldat, _edat-_sdat);
    memset(_sbss, 0, _ebss-_sbss);

    canary_init();
    stm32_init();
    time_init();
    console_init();
    console_crash_on_input();
    board_init();
    delay_ms(200); /* 5v settle */

    printk("\n** FlashFloppy TestBed for Gotek\n");
    printk("** Keir Fraser <keir.xen@gmail.com>\n");
    printk("** https://github.com/keirf/FlashFloppy\n\n");

    floppy_init();

    led_7seg_init();
    led_7seg_write_string("FFT");

    timer_init(&button_timer, button_timer_fn, NULL);
    timer_set(&button_timer, time_now());

    for (;;) {
        da_test();
        hfe_test();
        dsk_test();
        adf_test(11);
        adf_test(22);
        img_test();
        canary_check();
    }

    return 0;
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
