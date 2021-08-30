/*
 * amiga.c
 * 
 * Amiga sector and track management.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

static uint32_t _amigados_checksum(const void *dat, unsigned int longs)
{
    const uint32_t *p = dat;
    uint32_t csum = 0;
    while (longs--)
        csum ^= be32toh(*p++);
    return csum;
}

static uint32_t amigados_mfm_checksum(const void *dat, unsigned int longs)
{
    return _amigados_checksum(dat, longs) & 0x55555555;
}

static uint32_t amigados_dat_checksum(const void *dat, unsigned int bytes)
{
    uint32_t csum = _amigados_checksum(dat, bytes/4);
    csum ^= csum >> 1;
    csum &= 0x55555555;
    return csum;
}

/* Shift even/odd bits into MFM data-bit positions */
#define even(x) ((x)>>1)
#define odd(x) (x)

static uint32_t get_long(uint32_t *p)
{
    return be32toh(((p[0] & 0x55555555) << 1) | (p[1] & 0x55555555));
}

void amiga_track_read(void *buf, unsigned int track, unsigned int nsec)
{
    const static unsigned int sec_bytes = 544;
    unsigned int track_bytes = sec_bytes * nsec - 2;
    struct read rd;
    uint32_t *q, *p = bc_buf_alloc(track_bytes);
    uint32_t info, csum;
    uint8_t format, trk, sec, togo;
    int i, j;

    rd.p = p;
    rd.nr_words = 6;
    rd.sync = SYNC_mfm;

    /* Scan for the last sector before track gap. Then read track all 
     * in one go (no track gap). */
    index.count = 0;
    do {
        WARN_ON(index.count >= 2);
        floppy_read_prep(&rd);
        floppy_read(&rd);
        info = get_long(p+1);
    } while ((uint8_t)info != 1);

    rd.nr_words = track_bytes;
    floppy_read_prep(&rd);
    floppy_read(&rd);

    /* Check MFM validity. */
    for (i = 0; i < nsec; i++) {
        WARN_ON(p[i*272] != htobe32(0x44894489));
        p[i*272] = htobe32(0x44a944a9);
    }
    mfm_check(p, track_bytes);

    for (i = 0; i < nsec; i++) {

        /* Info longword (format, track, sector, togo). */
        info = get_long(p+1);
        format = info >> 24;
        trk = info >> 16;
        sec = info >> 8;
        togo = info;
        WARN_ON(format != 0xff);
        WARN_ON(trk != track);
        WARN_ON(sec >= nsec);
        WARN_ON(togo != (nsec-i));

        /* Header checksum. */
        csum = amigados_mfm_checksum(p+1, 10);
        WARN_ON(csum != get_long(p+11));

        /* Data checksum. */
        csum = amigados_mfm_checksum(p+15, 256);
        WARN_ON(csum != get_long(p+13));

        /* Decode the data. */
        p += 15;
        q = p + 512/4;
        for (j = 0; j < 512/4; j++) {
            *p = ((*p & 0x55555555) << 1) | (*q & 0x55555555);
            p++; q++;
        }
        memcpy((uint8_t *)buf + sec*512, p - 512/4, 512);

        /* Skip the inter-sector gap. */
        p = q + 1;

    }
}

void amiga_track_write(const void *buf, unsigned int track, unsigned int nsec)
{
    unsigned int track_bytes = (110000 / 32) * 2;
    uint32_t csum, info, *q, *p, *end, pr = 0;
    const uint32_t *b;
    unsigned int i, sec;
    struct write wr;

    if (nsec != 11) {
        /* Amiga HD track */
        ASSERT(nsec == 22);
        track_bytes *= 2;
    }

    p = q = bc_buf_alloc(track_bytes);
    end = p + track_bytes/2;
    b = buf;

#define emit_raw(r) ({                          \
    uint32_t _r = (r);                          \
    *q = htobe32(_r & ~(pr << 31));             \
    pr = _r;                                    \
    q++; })
#define emit_long(l) ({                         \
    uint32_t _l = (l), __l = _l;                \
    _l &= 0x55555555u; /* data bits */          \
    _l |= (~((__l>>2)|__l) & 0x55555555u) << 1; \
    emit_raw(_l); })

    /* Post-index gap (1024 bitcells). */
    for (i = 0; i < 32; i++)
        emit_long(0);

    for (sec = 0; sec < nsec; sec++) {

        /* Sector header */

        /* sector gap */
        emit_long(0);
        /* sync */
        emit_raw(0x44894489);
        /* info word */
        info = (0xff << 24) | (track << 16) | (sec << 8) | (nsec - sec);
        emit_long(even(info));
        emit_long(odd(info));
        /* label */
        for (i = 0; i < 8; i++)
            emit_long(0);
        /* header checksum */
        csum = info ^ (info >> 1);
        emit_long(0);
        emit_long(odd(csum));
        /* data checksum */
        csum = amigados_dat_checksum(b, 512);
        emit_long(0);
        emit_long(odd(csum));

        /* Sector data */

        for (i = 0; i < 512/4; i++)
            emit_long(even(be32toh(b[i])));
        for (i = 0; i < 512/4; i++)
            emit_long(odd(be32toh(b[i])));

        b += 512/4;
    }

    /* Pre-index gap */
    while (q < end)
        emit_long(0);

    /* Do the write, index-to-index. */
    wr.p = p;
    wr.nr_words = track_bytes;
    wr.terminate_at_index = 1;
    floppy_write_prep(&wr);
    index.count = 0;
    while (index.count == 0)
        continue;
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
