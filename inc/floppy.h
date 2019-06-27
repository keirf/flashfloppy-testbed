/*
 * floppy.h
 * 
 * Floppy interface control and image management.
 * 
 * Written & released by Keir Fraser <keir.xen@gmail.com>
 * 
 * This is free and unencumbered software released into the public domain.
 * See the file COPYING for more details, or visit <http://unlicense.org>.
 */

#define bc_buf_alloc(words) (alloca((words)*2))

/*
 * READ PATH
 */

struct read {
    /** INPUTS **/
    /* Bitcell input buffer. Must be 32-bit aligned. */
    void *p;
    /* Number of words (16-bitcells) to read. Must be multiple of two. */
    unsigned int nr_words;
    /* SYNC_*: If non-zero, delay read until indicated FM/MFM sync mark. */
    enum { SYNC_none=0, SYNC_fm, SYNC_mfm } sync;

    /** OUTPUTS **/
    /* Time at which the read started. */
    time_t start;
    /* Time at which the read ended. */
    time_t end;

    /** PRIVATE **/
    /* Tail of bitcell stream. */
    uint32_t bc_window;
    /* Progress through output buffer (in bitcells). */
    uint32_t bc_prod;
};

void floppy_read_prep(struct read *rd);
void floppy_read(struct read *rd);

/*
 * WRITE PATH
 */

struct write {
    /** INPUTS **/
    /* Bitcell output buffer. Must be 32-bit aligned. */
    const void *p;
    /* Number of words (16-bitcells) to write. Must be multiple of two. */
    unsigned int nr_words;
    /* Terminate write early at index hole? */
    bool_t terminate_at_index;

    /** PRIVATE **/
    /* Accumulated ticks (SYSCLK*16) since previous flux reversal. */
    uint32_t ticks_since_flux;
    /* Progress through input buffer (in bitcells). */
    uint32_t bc_cons;
};

void floppy_write_prep(struct write *wr);
void floppy_write(struct write *wr);

/*  
 * ASYNCHRONOUS (INTERRUPT-DRIVEN) FLOPPY SIGNALS
 */

extern volatile struct index {
    unsigned int count;
    time_t timestamp;
} index;

extern volatile struct dskchg {
    unsigned int count;
} dskchg;

/*
 * IBM TRACK FORMAT
 */

struct idam {
    uint8_t c, h, r, n;
};

struct ibm_scan_info {
    struct idam idam;
    unsigned int ticks_past_index;
};

unsigned int ibm_mfm_scan(
    struct ibm_scan_info *info, unsigned int max, unsigned int *p_gap3);
void ibm_mfm_read_sector(void *buf, const struct idam *idam);
void ibm_mfm_write_sector(
    const void *buf, const struct idam *idam, unsigned int gap3);

unsigned int ibm_fm_scan(
    struct ibm_scan_info *info, unsigned int max, unsigned int *p_gap3);
void ibm_fm_read_sector(void *buf, const struct idam *idam);
void ibm_fm_write_sector(
    const void *buf, const struct idam *idam, unsigned int gap3);

/*
 * AMIGA TRACK FORMAT
 */

void amiga_track_read(void *buf, unsigned int track, unsigned int nsec);
void amiga_track_write(const void *buf, unsigned int track, unsigned int nsec);

/*
 * MISCELLANEOUS
 */

struct drive {
    uint8_t cyl;
    /* Expected SYSCLK ticks per bitcell. */
    unsigned int ticks_per_cell;
};
extern struct drive *cur_drive;

/* MFM conversion. */
extern const uint16_t mfmtab[];
static inline uint16_t bintomfm(uint8_t x) { return mfmtab[x]; }
void bin_to_mfm(void *p, unsigned int nr);
uint8_t mfmtobin(uint16_t x);
void mfm_to_bin(void *p, unsigned int nr);
void mfm_check(const void *p, unsigned int nr);

/* FM conversion. */
#define FM_SYNC_CLK 0xc7
uint16_t fm_sync(uint8_t dat, uint8_t clk);
void bin_to_fm(void *p, unsigned int nr);
#define fm_to_bin(p, n) (mfm_to_bin((p), (n)))
void fm_check(const void *p, unsigned int nr);

/* External API. */
void floppy_init(void);
void floppy_select(unsigned int unit);
void floppy_seek(unsigned int cyl, unsigned int side);
void floppy_disk_change(void);
void test_ready(void);

/*
 * Local variables:
 * mode: C
 * c-file-style: "Linux"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
