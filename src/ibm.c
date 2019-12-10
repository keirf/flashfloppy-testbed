/*
 * ibm.c
 * 
 * IBM sector and track management.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

static const uint8_t mfm_gap_sync = 12;
static const uint8_t mfm_gap2 = 22;
static const uint8_t mfm_idam_mark[4] = { 0xa1, 0xa1, 0xa1, 0xfe };

static const uint8_t fm_gap_sync = 6;
static const uint8_t fm_gap2 = 11;

#define round_div(x,y) (((x)+((y)/2)) / (y))

unsigned int ibm_mfm_scan(
    struct ibm_scan_info *info, unsigned int max, unsigned int *p_gap3)
{
    struct read rd;
    uint8_t *p = bc_buf_alloc(10);
    unsigned int i = 0;
    time_t index_timestamp;

    rd.p = p;
    rd.nr_words = 10;
    rd.sync = SYNC_mfm;

    index.count = 0;
    while (index.count == 0)
        continue;
    index_timestamp = index.timestamp;

    for (;;) {
        floppy_read_prep(&rd);
        floppy_read(&rd);
        if (index.count != 1)
            break;
        mfm_to_bin(p, 10);
        if (memcmp(p, mfm_idam_mark, 4)
            || crc16_ccitt(p, 10, 0xffff))
            continue;
        if (i < max) {
            memcpy(&info[i].idam, p+4, 4);
            info[i].ticks_past_index = rd.start - index_timestamp;
        }
        i++;
    }

    if (p_gap3 && (i >= 2) && (max >= 2)) {
        int sec_bytes = round_div(
            info[1].ticks_past_index - info[0].ticks_past_index,
            time_sysclk(16 * cur_drive->ticks_per_cell));
        int gap3 = sec_bytes
            - (mfm_gap_sync + 8 + 2 + mfm_gap2)
            - (mfm_gap_sync + 4 + (128<<info[0].idam.n) + 2);
        WARN_ON(gap3 < 0);
        *p_gap3 = (unsigned int)gap3;
    } else if (p_gap3) {
        *p_gap3 = 84; /* make one up */
    }

    return i;
}

void ibm_mfm_search(struct read *rd, const struct idam *idam)
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
    } while (memcmp(p, mfm_idam_mark, 4)
             || memcmp(p+4, idam, 4)
             || crc16_ccitt(p, 10, 0xffff));
}

void ibm_mfm_read_sector(void *buf, const struct idam *idam)
{
    unsigned int dam_bytes = 4 + (128<<idam->n) + 2;
    uint8_t *p = bc_buf_alloc(dam_bytes);
    struct read rd;

    ibm_mfm_search(&rd, idam);

    rd.p = p;
    rd.nr_words = dam_bytes;
    rd.sync = SYNC_mfm;

    floppy_read_prep(&rd);
    floppy_read(&rd);
    mfm_check(p+6, dam_bytes-3);
    mfm_to_bin(p, dam_bytes);
    WARN_ON(memcmp(p, mfm_idam_mark, 3));
    WARN_ON(p[3] != 0xfb);
    WARN_ON(crc16_ccitt(p, dam_bytes, 0xffff));

    memcpy(buf, p+4, 128<<idam->n);
}

void ibm_mfm_write_sector(
    const void *buf, const struct idam *idam, unsigned int gap3)
{
    unsigned int dam_bytes;
    uint8_t *q, *p;
    uint16_t crc, *sync;
    int32_t delta;
    time_t deadline;
    struct write wr;
    struct read rd;
    int i;

    /* Calculate write length: must be an even number of bytes. */
    dam_bytes = mfm_gap_sync + 4 + (128<<idam->n) + 2 + gap3;
    if (dam_bytes & 1) {
        /* We write multiples of 32 bitcells: pad the write with extra GAP3 
         * to achieve this. */
        dam_bytes++;
        gap3++;
    }

    wr.p = p = bc_buf_alloc(dam_bytes);
    wr.nr_words = dam_bytes;
    wr.terminate_at_index = FALSE;

    /* Generate the sector data. */
    q = p;
    /* Pre-sync gap */
    memset(q, 0x00, mfm_gap_sync);
    q += mfm_gap_sync;
    /* Sync */
    memcpy(q, mfm_idam_mark, 3);
    q += 3;
    /* DAM */
    *q++ = 0xfb;
    /* Data */
    memcpy(q, buf, 128 << idam->n);
    q += 128 << idam->n;
    /* CRC */
    crc = crc16_ccitt(p+mfm_gap_sync, q-(p+mfm_gap_sync), 0xffff);
    *q++ = crc >> 8;
    *q++ = crc;
    /* Post-data gap */
    memset(q, 0x4e, gap3);
    q += gap3;

    /* Convert to MFM and fix the sync words. */
    bin_to_mfm(p, dam_bytes);
    mfm_check(p+2, dam_bytes-1);
    sync = (uint16_t *)&p[2*mfm_gap_sync];
    for (i = 0; i < 3; i++)
        *sync++ = htobe16(0x4489);

    /* Prepare the write. */
    floppy_write_prep(&wr);

    /* Find the sector. */
    ibm_mfm_search(&rd, idam);

    /* Wait for end of GAP2. */
    deadline = rd.end + time_sysclk(mfm_gap2 * 16 * cur_drive->ticks_per_cell);
    delta = time_diff(time_now(), deadline);
    if (delta > 0)
        delay_ticks(delta);

    /* Do the write. */
    floppy_write(&wr);
}

void ibm_mfm_write_track(
    const struct idam *idam, unsigned int nr, unsigned int gap3)
{
    unsigned int idam_bytes, dam_bytes, track_bytes;
    unsigned int sync_off[nr*2];
    uint8_t *q, *p, *s;
    uint16_t crc, *sync;
    struct write wr;
    int i;

    idam_bytes = mfm_gap_sync + 8 + 2 + mfm_gap2;
    dam_bytes = mfm_gap_sync + 4 + 2 + gap3;

    /* XXX TODO: Write IAM. Construct suitable pre-index gap. */

    /* Calculate write length: must be an even number of bytes. */
    track_bytes = 64;
    for (i = 0; i < nr; i++) {
        track_bytes += idam_bytes;
        track_bytes += dam_bytes;
        track_bytes += 128 << idam[i].n;
    }
    track_bytes += 64;
    if (track_bytes & 1) {
        /* We write multiples of 32 bitcells: pad the write with extra GAP
         * to achieve this. */
        track_bytes++;
    }

    wr.p = p = bc_buf_alloc(track_bytes);
    wr.nr_words = track_bytes;
    wr.terminate_at_index = TRUE;

    /* Generate the sector data. */
    q = p;
    /* Post-index gap */
    memset(q, 0x4e, 64);
    q += 64;
    for (i = 0; i < nr; i++) {
        /* Pre-sync gap */
        memset(q, 0x00, mfm_gap_sync);
        q += mfm_gap_sync;
        /* Sync + IDAM */
        sync_off[i*2] = q-p; s = q;
        memcpy(q, mfm_idam_mark, 4);
        q += 4;
        /* IDAM */
        memcpy(q, &idam[i], 4);
        q += 4;
        /* CRC */
        crc = crc16_ccitt(s, q-s, 0xffff);
        *q++ = crc >> 8;
        *q++ = crc;
        /* GAP2 */
        memset(q, 0x4e, mfm_gap2);
        q += mfm_gap2;

        /* Pre-sync gap */
        memset(q, 0x00, mfm_gap_sync);
        q += mfm_gap_sync;
        /* Sync */
        sync_off[i*2+1] = q-p; s = q;
        memcpy(q, mfm_idam_mark, 3);
        q += 3;
        /* DAM */
        *q++ = 0xfb;
        /* Data */
        memset(q, 0xe2, 128 << idam[i].n);
        q += 128 << idam[i].n;
        /* CRC */
        crc = crc16_ccitt(s, q-s, 0xffff);
        *q++ = crc >> 8;
        *q++ = crc;
        /* Post-data gap */
        memset(q, 0x4e, gap3);
        q += gap3;
    }
    /* Pre-index gap */
    memset(q, 0x4e, 64);
    q += 64;

    /* Convert to MFM and fix the sync words. */
    bin_to_mfm(p, track_bytes);
    mfm_check(p+2, track_bytes-1);
    for (i = 0; i < nr*2; i++) {
        sync = (uint16_t *)&p[2*sync_off[i]];
        *sync++ = htobe16(0x4489);
        *sync++ = htobe16(0x4489);
        *sync++ = htobe16(0x4489);
    }

    /* Do the write, index-to-index. */
    floppy_write_prep(&wr);
    index.count = 0;
    while (index.count == 0)
        continue;
    floppy_write(&wr);
}

unsigned int ibm_fm_scan(
    struct ibm_scan_info *info, unsigned int max, unsigned int *p_gap3)
{
    struct read rd;
    uint8_t *p = bc_buf_alloc(8);
    unsigned int i = 0;
    time_t index_timestamp;

    rd.p = p;
    rd.nr_words = 8;
    rd.sync = SYNC_fm;

    index.count = 0;
    while (index.count == 0)
        continue;
    index_timestamp = index.timestamp;

    for (;;) {
        floppy_read_prep(&rd);
        floppy_read(&rd);
        if (index.count != 1)
            break;
        mfm_to_bin(p, 8);
        if ((p[1] != 0xfe)
            || crc16_ccitt(p+1, 7, 0xffff))
            continue;
        if (i < max) {
            memcpy(&info[i].idam, p+2, 4);
            info[i].ticks_past_index = rd.start - index_timestamp;
        }
        i++;
    }

    if (p_gap3 && (i >= 2) && (max >= 2)) {
        int sec_bytes = round_div(
            info[1].ticks_past_index - info[0].ticks_past_index,
            time_sysclk(16 * cur_drive->ticks_per_cell));
        int gap3 = sec_bytes
            - (fm_gap_sync + 5 + 2 + fm_gap2)
            - (fm_gap_sync + 1 + (128<<info[0].idam.n) + 2);
        WARN_ON(gap3 < 0);
        *p_gap3 = (unsigned int)gap3;
    }

    return i;
}

void ibm_fm_search(struct read *rd, const struct idam *idam)
{
    uint8_t *p = bc_buf_alloc(8);

    rd->p = p;
    rd->nr_words = 8;
    rd->sync = SYNC_fm;

    index.count = 0;

    do {
        WARN_ON(index.count >= 2);
        floppy_read_prep(rd);
        floppy_read(rd);
        fm_to_bin(p, 8);
    } while ((p[1] != 0xfe)
             || memcmp(p+2, idam, 4)
             || crc16_ccitt(p+1, 7, 0xffff));
}

void ibm_fm_read_sector(void *buf, const struct idam *idam)
{
    unsigned int dam_bytes = 2 + (128<<idam->n) + 2;
    uint8_t *p = bc_buf_alloc(dam_bytes);
    struct read rd;

    ibm_fm_search(&rd, idam);

    rd.p = p;
    rd.nr_words = dam_bytes;
    rd.sync = SYNC_fm;

    floppy_read_prep(&rd);
    floppy_read(&rd);
    fm_check(p+4, dam_bytes-2);
    fm_to_bin(p, dam_bytes);
    WARN_ON(p[1] != 0xfb);
    WARN_ON(crc16_ccitt(p+1, dam_bytes-1, 0xffff));

    memcpy(buf, p+2, 128<<idam->n);
}

void ibm_fm_write_sector(
    const void *buf, const struct idam *idam, unsigned int gap3)
{
    unsigned int dam_bytes;
    uint8_t *q, *p;
    uint16_t crc, *sync;
    int32_t delta;
    time_t deadline;
    struct write wr;
    struct read rd;

    /* Calculate write length: must be an even number of bytes. */
    dam_bytes = fm_gap_sync + 1 + (128<<idam->n) + 2 + gap3;
    if (dam_bytes & 1) {
        /* We write multiples of 32 bitcells: pad the write with extra GAP3 
         * to achieve this. */
        dam_bytes++;
        gap3++;
    }

    wr.p = p = bc_buf_alloc(dam_bytes);
    wr.nr_words = dam_bytes;
    wr.terminate_at_index = FALSE;

    /* Generate the sector data. */
    q = p;
    /* Pre-sync gap */
    memset(q, 0x00, fm_gap_sync);
    q += fm_gap_sync;
    /* DAM */
    *q++ = 0xfb;
    /* Data */
    memcpy(q, buf, 128 << idam->n);
    q += 128 << idam->n;
    /* CRC */
    crc = crc16_ccitt(p+fm_gap_sync, q-(p+fm_gap_sync), 0xffff);
    *q++ = crc >> 8;
    *q++ = crc;
    /* Post-data gap */
    memset(q, 0xff, gap3);
    q += gap3;

    /* Convert to FM and fix the sync words. */
    bin_to_fm(p, dam_bytes);
    fm_check(p, dam_bytes);
    sync = (uint16_t *)&p[2*fm_gap_sync];
    *sync = htobe16(fm_sync(0xfb, FM_SYNC_CLK));

    /* Prepare the write. */
    floppy_write_prep(&wr);

    /* Find the sector. */
    ibm_fm_search(&rd, idam);

    /* Wait for end of GAP2. */
    deadline = rd.end + time_sysclk(fm_gap2 * 16 * cur_drive->ticks_per_cell);
    delta = time_diff(time_now(), deadline);
    if (delta > 0)
        delay_ticks(delta);

    /* Do the write. */
    floppy_write(&wr);
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
