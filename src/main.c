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
    floppy_select(0);

    da_select_image("hd.hfe");
    floppy_seek(5, 1);
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
    floppy_select(0);

    snprintf(name, sizeof(name), "amiga_%u", nsec*80);
    da_select_image(name);
    floppy_seek(track/2, track&1);
    cur_drive->ticks_per_cell = sysclk_us(2);

    for (i = 0; i < nsec*512; i++)
        p[i] = rand()>>8;
    amiga_track_write(p, track, nsec);

    amiga_track_read(q, track, nsec);
    WARN_ON(memcmp(p, q, nsec*512));

    printk("Amiga %s - OK\n", (nsec == 11) ? "DD" : "HD");
}

static void mk_ibm_idams(
    struct idam *idam, unsigned int nr,
    unsigned int interleave, unsigned int cskew, unsigned int hskew)
{
    unsigned int i, pos;
    struct idam i0 = *idam;

    memset(idam, 0xff, nr * sizeof(*idam));
    pos = ((i0.c * cskew) + (i0.h * hskew)) % nr;
    for (i = 0; i < nr; i++) {
        while (idam[pos].r != 0xff)
            pos = (pos + 1) % nr;
        memcpy(&idam[pos], &i0, sizeof(i0));
        idam[pos].r = i + i0.r;
        pos = (pos + interleave) % nr;
    }
}

static void check_ibm_idams(
    const struct idam *expected, unsigned int exp_nr,
    const struct ibm_scan_info *seen, unsigned int seen_nr)
{
    unsigned int i;

    if (exp_nr != seen_nr)
        goto fail;
    for (i = 0; i < exp_nr; i++)
        if (memcmp(&expected[i], &seen[i].idam, sizeof(*expected)))
            goto fail;
    return;

fail:
    printk("Expected %u sectors:\n", exp_nr);
    for (i = 0; i < exp_nr; i++) {
        printk(" %u: (%u, %u, %u, %u)\n", i,
               expected[i].c, expected[i].h,
               expected[i].r, expected[i].n);
    }
    printk("Seen %u sectors:\n", seen_nr);
    for (i = 0; i < seen_nr; i++) {
        printk(" %u: (%u, %u, %u, %u)\n", i,
               seen[i].idam.c, seen[i].idam.h,
               seen[i].idam.r, seen[i].idam.n);
    }
    WARN_ON(TRUE);
}

static void noinline mfm_rw_sector(struct idam *idam, uint8_t base, uint8_t nr)
{
    unsigned int sz = 128 << idam->n;
    struct ibm_scan_info info[64];
    struct idam expected[nr];
    uint8_t *p = alloca(sz), *q = alloca(sz);
    time_t index_timestamp;
    unsigned int index_period, orig_index_period, gap3, seen_nr;
    int i;

    seen_nr = ibm_mfm_scan(info, ARRAY_SIZE(info), &gap3);

    memcpy(&expected[0], idam, sizeof(*idam));
    expected[0].r = base;
    mk_ibm_idams(expected, nr, 1, 0, 0);
    check_ibm_idams(expected, nr, info, seen_nr);

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
    WARN_ON(memcmp(p, q, sz));
    printk("MFM %u r/w sector - OK\n", sz);
}

static void noinline fm_rw_sector(struct idam *idam, uint8_t base, uint8_t nr)
{
    unsigned int sz = 128 << idam->n;
    struct ibm_scan_info info[64];
    struct idam expected[nr];
    uint8_t *p = alloca(sz), *q = alloca(sz);
    time_t index_timestamp;
    unsigned int index_period, orig_index_period, gap3, seen_nr;
    int i;

    seen_nr = ibm_fm_scan(info, ARRAY_SIZE(info), &gap3);

    memcpy(&expected[0], idam, sizeof(*idam));
    expected[0].r = base;
    mk_ibm_idams(expected, nr, 1, 0, 0);
    check_ibm_idams(expected, nr, info, seen_nr);

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
    WARN_ON(memcmp(p, q, sz));
    printk("FM %u r/w sector - OK\n", sz);
}

static void noinline dsk_test(void)
{
    struct idam idam_8k = { 2, 0, 3, 6 };
    struct idam idam_512 = { 0, 0, 18, 2 };

    printk("\nDSK TEST:\n");
    floppy_select(0);

    da_select_image("tst_dsk");
    floppy_seek(2, 0);
    cur_drive->ticks_per_cell = sysclk_us(2);
    mfm_rw_sector(&idam_8k, 3, 1);
    floppy_seek(0, 0);
    cur_drive->ticks_per_cell = sysclk_us(2);
    mfm_rw_sector(&idam_512, 3, 16);
    idam_512.r = 3;
    mfm_rw_sector(&idam_512, 3, 16);
}

static void noinline img_test(void)
{
    struct idam idam = { 0, 0, 1, 2 };

    printk("\nIMG TEST:\n");
    floppy_select(0);

    da_select_image("2m88");
    floppy_seek(0, 0);
    idam.n = 2;
    cur_drive->ticks_per_cell = sysclk_ns(500);
    mfm_rw_sector(&idam, 1, 36);

    da_select_image("720k");
    floppy_seek(0, 0);
    idam.n = 2;
    cur_drive->ticks_per_cell = sysclk_us(2);
    mfm_rw_sector(&idam, 1, 9);

    da_select_image("200k");
    floppy_seek(0, 0);
    idam.n = 1;
    cur_drive->ticks_per_cell = sysclk_us(4);
    fm_rw_sector(&idam, 0, 10);

    da_select_image("8k.8k");
    floppy_seek(0, 0);
    idam.n = 6;
    cur_drive->ticks_per_cell = sysclk_us(1);
    mfm_rw_sector(&idam, 1, 1);
}

static void wait_rdata(void)
{
    int i;
    for (i = 0; i < 3; i++) {
        /* Wait for RDATA active. */
        exti->pr = 1<<8;
        while (!(exti->pr & (1<<8)))
            continue;
    }
}

static const uint8_t mfm_idam_mark[4] = { 0xa1, 0xa1, 0xa1, 0xfe };
static void ibm_find_any(struct read *rd, struct idam *idam)
{
    uint8_t *p = bc_buf_alloc(10);

    rd->p = p;
    rd->nr_words = 10;
    rd->sync = SYNC_mfm;

    index.count = 0;

    do {
        WARN_ON(index.count >= 2);
        floppy_read_prep(rd);
        floppy_read(rd);
        mfm_to_bin(p, 10);
    } while (memcmp(p, mfm_idam_mark, 4));
    memcpy(idam, p+4, 4);
}

static void noinline speed_test(void)
{
    struct read rd;
    struct idam idam;
    int cyls[] = { 23, 9, 8, 7, 8, 6, 3, 22, 45, 19, 29, 1, 18, 38 }, i, j, k;
    int secs[36] = {1,3,5,7,9,11,13,15,17,19,21,23,25,27,29,31,33,35,2,4,6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36};
//    int secs[] = { 3, 7, 11 };
    const int sz = 512;
    uint8_t *p = alloca(sz);
    time_t t[2], t_w;

    for (i = 0; i < sz; i++)
        p[i] = rand()>>8;
    for (i = 0; i < ARRAY_SIZE(cyls); i++)
        cyls[i] = i;

    floppy_select(0);
    cur_drive->ticks_per_cell = sysclk_ns(1000);

    for (i = 0; i < ARRAY_SIZE(cyls); i++) {
        for (k = 0; k < 2; k++) {
            floppy_seek(cyls[i], k);
            t[0] = time_now();
            wait_rdata();
            t[1] = time_now();
            ibm_find_any(&rd, &idam);
            printk("\nS%u.%u: %u+%u=%u %u,%u,%u,%u\n", cyls[i], k,
                   (t[1]-t[0])/TIME_MHZ, time_since(t[1])/TIME_MHZ,
                   time_since(t[0])/TIME_MHZ,
                   idam.c, idam.h, idam.r, idam.n);
            if(1){
                for (j = 0; j < ARRAY_SIZE(secs); j++) {
                    idam.r = secs[j];
                    t_w = time_now();
                    ibm_mfm_write_sector(p, &idam, 40);
                    if (1) {
                        t[0] = time_now();
                        wait_rdata();
                        t[1] = time_now();
                        ibm_find_any(&rd, &idam);
                        printk("W%u(%u): %u,%u %u,%u,%u,%u\n", secs[j],
                               (t[0]-t_w)/TIME_MHZ,
                               (t[1]-t[0])/TIME_MHZ, time_since(t[1])/TIME_MHZ,
                               idam.c, idam.h, idam.r, idam.n);
                    }
                }
            }
        }
    }
}

int main(void)
{
    unsigned int i;

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

    for (i = 0; ; i++) {
        printk("\n*** ROUND %u ***\n", i);
        if (0) {
        da_test();
        hfe_test();
        dsk_test();
        adf_test(11);
        adf_test(22);
        img_test();
        } else {
            speed_test();
        }
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
